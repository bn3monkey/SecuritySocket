#include "HttpClient.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Bn3Monkey
{
    namespace
    {
        // Is 'buf' a complete HTTP/1.1 response? Requires the header terminator
        // plus, when Content-Length is present, that many body bytes. Responses
        // without Content-Length are treated as bodyless (the framework always
        // emits one; chunked is out of scope, §1-2).
        bool responseComplete(const std::vector<char>& buf)
        {
            const char* data = buf.data();
            const size_t len = buf.size();
            const char* end  = data + len;

            const char* term = nullptr;
            for (const char* p = data; p + 3 < end; ++p) {
                if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                    term = p; break;
                }
            }
            if (!term) return false;
            const size_t header_end = static_cast<size_t>(term - data) + 4;

            // Case-insensitive scan for "Content-Length:" within the header block.
            long content_length = -1;
            for (const char* p = data; p < term; ++p) {
                static const char kKey[] = "content-length:";
                size_t i = 0;
                while (i < 15 && p + i < term &&
                       std::tolower((unsigned char)p[i]) == kKey[i]) ++i;
                if (i == 15) {
                    const char* v = p + 15;
                    while (v < term && (*v == ' ' || *v == '\t')) ++v;
                    content_length = std::strtol(v, nullptr, 10);
                    break;
                }
            }
            if (content_length < 0) return true;   // no body framing -> done
            return len >= header_end + static_cast<size_t>(content_length);
        }
    }

    static std::string makeHost(const NetworkConfiguration& cfg)
    {
        NetworkConfiguration c = cfg;   // ip()/port() are non-const accessors
        std::string host = c.ip();
        host.push_back(':');
        host += c.port();
        return host;
    }

    HttpClient::HttpClient(const NetworkConfiguration& cfg)
        : Client(cfg), _host(makeHost(cfg)) {}

    HttpClient::HttpClient(const NetworkConfiguration& cfg,
                           const TlsClientConfiguration& tls)
        : Client(cfg, tls), _host(makeHost(cfg)) {}

    NetworkResult HttpClient::ensureConnected()
    {
        if (_connected) return NetworkResult(NetworkResultCode::SUCCESS);

        NetworkResult r = open();
        if (r.code() != NetworkResultCode::SUCCESS) return r;
        r = connect();
        if (r.code() != NetworkResultCode::SUCCESS) return r;
        _connected = true;
        return r;
    }

    HttpClientResponse HttpClient::exchange(const HttpClientRequest& req)
    {
        HttpClientResponse out;
        auto* out_impl = reinterpret_cast<HttpClientResponseImpl*>(out._container);

        NetworkResult conn = ensureConnected();
        if (conn.code() != NetworkResultCode::SUCCESS) {
            out_impl->setResultCode(conn.code());
            return out;
        }

        // Serialize the request (Host injected here — only we know the authority).
        const auto* req_impl =
            reinterpret_cast<const HttpClientRequestImpl*>(req._container);
        std::vector<char> wire;
        req_impl->serialize(_host.c_str(), wire);

        NetworkResult w = write(wire.data(), wire.size());
        if (w.code() != NetworkResultCode::SUCCESS) {
            _connected = false;
            out_impl->setResultCode(w.code());
            return out;
        }

        // Read until the full response is buffered (or the peer closes / errors).
        std::vector<char> buf;
        char chunk[8192];
        for (;;) {
            NetworkResult r = read(chunk, sizeof(chunk));
            if (r.code() == NetworkResultCode::SUCCESS && r.bytes() > 0) {
                buf.insert(buf.end(), chunk, chunk + r.bytes());
                if (responseComplete(buf)) break;
                continue;
            }
            if (r.code() == NetworkResultCode::SOCKET_CLOSED) {
                _connected = false;   // connection-close framing or peer FIN
                break;
            }
            // Timeout or transport error with nothing complete yet.
            if (buf.empty()) {
                out_impl->setResultCode(r.code());
                return out;
            }
            break;
        }

        if (!out_impl->parse(buf.data(), buf.size())) {
            out_impl->setResultCode(NetworkResultCode::SSL_PROTOCOL_ERROR);  // malformed
            return out;
        }
        out_impl->setResultCode(NetworkResultCode::SUCCESS);
        return out;
    }

    HttpClientResponse HttpClient::simple(const char* method, const char* path,
                                          const void* body, size_t len)
    {
        HttpClientRequest req;
        req.method(method).path("%s", path ? path : "/");
        if (body && len > 0) req.body(body, len);
        return exchange(req);
    }

    HttpClientResponse HttpClient::get    (const char* path) { return simple("GET",     path, nullptr, 0); }
    HttpClientResponse HttpClient::head   (const char* path) { return simple("HEAD",    path, nullptr, 0); }
    HttpClientResponse HttpClient::del    (const char* path) { return simple("DELETE",  path, nullptr, 0); }
    HttpClientResponse HttpClient::options(const char* path) { return simple("OPTIONS", path, nullptr, 0); }

    HttpClientResponse HttpClient::post (const char* path, const void* body, size_t len) { return simple("POST",  path, body, len); }
    HttpClientResponse HttpClient::put  (const char* path, const void* body, size_t len) { return simple("PUT",   path, body, len); }
    HttpClientResponse HttpClient::patch(const char* path, const void* body, size_t len) { return simple("PATCH", path, body, len); }

    HttpClientResponse HttpClient::request(const HttpClientRequest& req)
    {
        return exchange(req);
    }
}
