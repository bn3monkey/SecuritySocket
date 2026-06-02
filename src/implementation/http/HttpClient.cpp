#include "HttpClient.hpp"

#include "../thirdparty/picohttpparser/picohttpparser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

using namespace Bn3Monkey;

namespace
{
    bool ciEqual(const char* a, const char* b)
    {
        if (!a || !b) return false;
        while (*a && *b) {
            if (std::tolower(static_cast<unsigned char>(*a)) !=
                std::tolower(static_cast<unsigned char>(*b)))
                return false;
            ++a; ++b;
        }
        return *a == 0 && *b == 0;
    }

    // Case-insensitive name match against a phr_header (name not NUL-terminated).
    bool headerNameIs(const struct phr_header& h, const char* name)
    {
        size_t n = std::strlen(name);
        if (h.name_len != n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(static_cast<unsigned char>(h.name[i])) !=
                std::tolower(static_cast<unsigned char>(name[i])))
                return false;
        return true;
    }

    // Returns Content-Length, or -1 when absent / non-numeric.
    long findContentLength(const struct phr_header* headers, size_t n)
    {
        for (size_t i = 0; i < n; ++i) {
            if (!headerNameIs(headers[i], "Content-Length")) continue;
            char buf[32];
            size_t len = headers[i].value_len;
            if (len == 0 || len >= sizeof(buf)) return -1;
            std::memcpy(buf, headers[i].value, len);
            buf[len] = '\0';
            char* end = nullptr;
            long v = std::strtol(buf, &end, 10);
            if (end == buf || v < 0) return -1;
            return v;
        }
        return -1;
    }

    // value token contains 'token' (case-insensitive substring is enough for
    // the close/keep-alive tokens we look for).
    bool headerValueHas(const struct phr_header* headers, size_t n,
                        const char* name, const char* token)
    {
        size_t tlen = std::strlen(token);
        for (size_t i = 0; i < n; ++i) {
            if (!headerNameIs(headers[i], name)) continue;
            const char* v = headers[i].value;
            size_t vlen = headers[i].value_len;
            if (vlen < tlen) continue;
            for (size_t off = 0; off + tlen <= vlen; ++off) {
                size_t k = 0;
                for (; k < tlen; ++k)
                    if (std::tolower(static_cast<unsigned char>(v[off + k])) !=
                        std::tolower(static_cast<unsigned char>(token[k])))
                        break;
                if (k == tlen) return true;
            }
        }
        return false;
    }
}

// ── HttpClientImpl ─────────────────────────────────────────────────────────

HttpClientImpl::HttpClientImpl(const NetworkConfiguration& cfg)
{
    NetworkConfiguration& c = const_cast<NetworkConfiguration&>(cfg);
    snprintf(_host, sizeof(_host), "%s:%s", c.ip(), c.port());
}

NetworkResult HttpClientImpl::ensureConnected(Client& transport)
{
    if (_opened && _connected)
        return NetworkResult(NetworkResultCode::SUCCESS);

    if (!_opened) {
        NetworkResult r = transport.open();
        if (r.code() != NetworkResultCode::SUCCESS) return r;
        _opened = true;
    }
    if (!_connected) {
        NetworkResult r = transport.connect();
        if (r.code() != NetworkResultCode::SUCCESS) return r;
        _connected = true;
    }
    return NetworkResult(NetworkResultCode::SUCCESS);
}

void HttpClientImpl::drop(Client& transport)
{
    transport.close();
    _opened = false;
    _connected = false;
    _leftover.clear();
}

HttpClientResponseImpl HttpClientImpl::perform(Client& transport,
                                               const HttpClientRequestImpl& req)
{
    HttpClientResponseImpl resp;

    NetworkResult c = ensureConnected(transport);
    if (c.code() != NetworkResultCode::SUCCESS) {
        resp.setResultCode(c.code());
        return resp;
    }

    // Serialize and send.
    std::vector<char> out;
    req.serialize(_host, /*keep_alive*/ true, out);
    NetworkResult w = transport.write(out.data(), out.size());
    if (w.code() != NetworkResultCode::SUCCESS ||
        static_cast<size_t>(w.bytes()) != out.size()) {
        drop(transport);
        resp.setResultCode(w.code() == NetworkResultCode::SUCCESS
                               ? NetworkResultCode::SOCKET_CLOSED
                               : w.code());
        return resp;
    }

    // Seed the receive buffer with any carry-over from a previous response.
    _recv.clear();
    if (!_leftover.empty()) {
        _recv = std::move(_leftover);
        _leftover.clear();
    }

    static thread_local char scratch[65536];

    // 1) Read until the header block is fully parsed.
    int    minor = 0, status = 0;
    const char* msg = nullptr;
    size_t msg_len = 0;
    struct phr_header headers[64];
    size_t num_headers = 0;
    size_t header_end = 0;
    size_t last_len = 0;

    for (;;) {
        if (!_recv.empty()) {
            num_headers = sizeof(headers) / sizeof(headers[0]);
            int ret = phr_parse_response(_recv.data(), _recv.size(),
                                         &minor, &status, &msg, &msg_len,
                                         headers, &num_headers, last_len);
            if (ret > 0) { header_end = static_cast<size_t>(ret); break; }
            if (ret == -1) {              // malformed response
                drop(transport);
                resp.setResultCode(NetworkResultCode::UNKNOWN_ERROR);
                return resp;
            }
            last_len = _recv.size();      // -2: incomplete, need more
        }
        NetworkResult r = transport.read(scratch, sizeof(scratch));
        if (r.code() == NetworkResultCode::SUCCESS && r.bytes() > 0) {
            _recv.insert(_recv.end(), scratch, scratch + r.bytes());
            continue;
        }
        drop(transport);
        resp.setResultCode(r.code() == NetworkResultCode::SUCCESS
                               ? NetworkResultCode::SOCKET_CLOSED
                               : r.code());
        return resp;
    }

    // 2) Status + headers.
    resp.setStatus(status);
    for (size_t i = 0; i < num_headers; ++i)
        resp.addHeader(headers[i].name, headers[i].name_len,
                       headers[i].value, headers[i].value_len);

    // 3) Body framing.
    const bool head_request = ciEqual(req.method(), "HEAD");
    const bool no_body = head_request || status == 204 || status == 304 ||
                         (status >= 100 && status < 200);

    bool conn_close;
    if (headerValueHas(headers, num_headers, "Connection", "close"))
        conn_close = true;
    else if (headerValueHas(headers, num_headers, "Connection", "keep-alive"))
        conn_close = false;
    else
        conn_close = (minor == 0);   // HTTP/1.0 defaults to close

    const long content_length = findContentLength(headers, num_headers);
    size_t body_len = 0;

    if (no_body) {
        body_len = 0;
    } else if (content_length >= 0) {
        body_len = static_cast<size_t>(content_length);
        while (_recv.size() < header_end + body_len) {
            NetworkResult r = transport.read(scratch, sizeof(scratch));
            if (r.code() == NetworkResultCode::SUCCESS && r.bytes() > 0) {
                _recv.insert(_recv.end(), scratch, scratch + r.bytes());
                continue;
            }
            // Connection died before the full body arrived — return what we
            // have, flagged as closed.
            conn_close = true;
            resp.setResultCode(NetworkResultCode::SOCKET_CLOSED);
            break;
        }
    } else if (conn_close) {
        // No length, no keep-alive: body runs until the peer closes.
        for (;;) {
            NetworkResult r = transport.read(scratch, sizeof(scratch));
            if (r.code() == NetworkResultCode::SUCCESS && r.bytes() > 0) {
                _recv.insert(_recv.end(), scratch, scratch + r.bytes());
                continue;
            }
            break;
        }
        body_len = _recv.size() - header_end;
    } else {
        body_len = 0;   // keep-alive without Content-Length => no body
    }

    if (header_end + body_len > _recv.size())
        body_len = _recv.size() - header_end;

    resp.setBody(_recv.data() + header_end, body_len);

    // 4) Carry pipelined bytes over for the next keep-alive request.
    size_t consumed = header_end + body_len;
    if (!conn_close && consumed < _recv.size())
        _leftover.assign(_recv.begin() + consumed, _recv.end());

    if (resp.resultCode() != NetworkResultCode::SOCKET_CLOSED)
        resp.setResultCode(NetworkResultCode::SUCCESS);

    if (conn_close)
        drop(transport);

    return resp;
}

// ── HttpClient (public facade) ─────────────────────────────────────────────

static HttpClientImpl* impl_of(char* c)
{
    return reinterpret_cast<HttpClientImpl*>(c);
}

Bn3Monkey::HttpClient::HttpClient(const NetworkConfiguration& cfg)
    : Client(cfg)
{
    static_assert(sizeof(HttpClientImpl) <= IMPLEMENTATION_SIZE,
                  "HttpClientImpl no longer fits in _http_container; "
                  "raise HttpClient::IMPLEMENTATION_SIZE");
    new (_http_container) HttpClientImpl(cfg);
}

Bn3Monkey::HttpClient::HttpClient(const NetworkConfiguration& cfg,
                                  const TlsClientConfiguration& tls)
    : Client(cfg, tls)
{
    static_assert(sizeof(HttpClientImpl) <= IMPLEMENTATION_SIZE,
                  "HttpClientImpl no longer fits in _http_container; "
                  "raise HttpClient::IMPLEMENTATION_SIZE");
    new (_http_container) HttpClientImpl(cfg);
}

Bn3Monkey::HttpClient::~HttpClient()
{
    impl_of(_http_container)->~HttpClientImpl();
}

// Wrap a freshly built response Impl into the public move-only container.
// Defined as an HttpClient member (below, via the facade methods) so it can
// reach HttpClientResponse::_container — HttpClient is its friend.
HttpClientResponse Bn3Monkey::HttpClient::simple(const char* method, const char* path,
                                                 const void* body, size_t len)
{
    HttpClientRequestImpl req;
    req.setMethod(method);
    req.setPathRaw(path);
    if (body && len) req.setBody(body, len);

    HttpClientResponseImpl built = impl_of(_http_container)->perform(*this, req);
    HttpClientResponse resp;   // default-constructs an empty Impl
    *reinterpret_cast<HttpClientResponseImpl*>(resp._container) = std::move(built);
    return resp;
}

HttpClientResponse Bn3Monkey::HttpClient::get(const char* path)
{
    return simple("GET", path, nullptr, 0);
}
HttpClientResponse Bn3Monkey::HttpClient::head(const char* path)
{
    return simple("HEAD", path, nullptr, 0);
}
HttpClientResponse Bn3Monkey::HttpClient::del(const char* path)
{
    return simple("DELETE", path, nullptr, 0);
}
HttpClientResponse Bn3Monkey::HttpClient::options(const char* path)
{
    return simple("OPTIONS", path, nullptr, 0);
}
HttpClientResponse Bn3Monkey::HttpClient::post(const char* path, const void* body, size_t len)
{
    return simple("POST", path, body, len);
}
HttpClientResponse Bn3Monkey::HttpClient::put(const char* path, const void* body, size_t len)
{
    return simple("PUT", path, body, len);
}
HttpClientResponse Bn3Monkey::HttpClient::patch(const char* path, const void* body, size_t len)
{
    return simple("PATCH", path, body, len);
}

HttpClientResponse Bn3Monkey::HttpClient::request(const HttpClientRequest& req)
{
    const HttpClientRequestImpl* ri =
        reinterpret_cast<const HttpClientRequestImpl*>(req._container);
    HttpClientResponseImpl built = impl_of(_http_container)->perform(*this, *ri);
    HttpClientResponse resp;
    *reinterpret_cast<HttpClientResponseImpl*>(resp._container) = std::move(built);
    return resp;
}
