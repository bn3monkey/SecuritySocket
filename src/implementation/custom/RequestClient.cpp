#include "RequestClient.hpp"

#include "../protocol/WsFrameCodec.hpp"
#include "../protocol/HttpParser.hpp"
#include "../thirdparty/picohttpparser/picohttpparser.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>

using namespace Bn3Monkey;

namespace
{
    const char* B64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    // base64-encode 'in' (len bytes) into NUL-terminated 'out'.
    void base64(const unsigned char* in, size_t len, char* out)
    {
        size_t o = 0;
        size_t i = 0;
        for (; i + 3 <= len; i += 3) {
            uint32_t n = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
            out[o++] = B64[(n >> 18) & 0x3F];
            out[o++] = B64[(n >> 12) & 0x3F];
            out[o++] = B64[(n >> 6) & 0x3F];
            out[o++] = B64[n & 0x3F];
        }
        size_t rem = len - i;
        if (rem == 1) {
            uint32_t n = in[i] << 16;
            out[o++] = B64[(n >> 18) & 0x3F];
            out[o++] = B64[(n >> 12) & 0x3F];
            out[o++] = '=';
            out[o++] = '=';
        } else if (rem == 2) {
            uint32_t n = (in[i] << 16) | (in[i + 1] << 8);
            out[o++] = B64[(n >> 18) & 0x3F];
            out[o++] = B64[(n >> 12) & 0x3F];
            out[o++] = B64[(n >> 6) & 0x3F];
            out[o++] = '=';
        }
        out[o] = '\0';
    }
}

// ── RequestClientImpl ──────────────────────────────────────────────────────

RequestClientImpl::RequestClientImpl(const NetworkConfiguration& cfg,
                                     const WebSocketConfiguration& ws)
    : _is_ws(ws.valid())
{
    NetworkConfiguration& c = const_cast<NetworkConfiguration&>(cfg);
    snprintf(_host, sizeof(_host), "%s:%s", c.ip(), c.port());
    snprintf(_ws_path, sizeof(_ws_path), "%s",
             (ws.valid() && ws.pattern) ? ws.pattern : "/");

    std::random_device rd;
    _rng_state = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
    if (_rng_state == 0) _rng_state = 0x9E3779B97F4A7C15ULL;   // avoid all-zero
}

void RequestClientImpl::fillMaskKey(unsigned char out[4])
{
    // xorshift64
    uint64_t x = _rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    _rng_state = x;
    uint32_t v = static_cast<uint32_t>(x);
    out[0] = static_cast<unsigned char>(v & 0xFF);
    out[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
    out[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
    out[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
}

NetworkResult RequestClientImpl::connect(Client& transport)
{
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
    if (_is_ws)
        return handshake(transport);
    return NetworkResult(NetworkResultCode::SUCCESS);
}

NetworkResult RequestClientImpl::handshake(Client& transport)
{
    // 16-byte random nonce -> base64 Sec-WebSocket-Key.
    unsigned char nonce[16];
    for (size_t i = 0; i < sizeof(nonce); i += 4) {
        unsigned char k[4];
        fillMaskKey(k);
        std::memcpy(nonce + i, k, 4);
    }
    char key[25];
    base64(nonce, sizeof(nonce), key);   // 24 chars + NUL

    char req[512];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        _ws_path, _host, key);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(req))
        return NetworkResult(NetworkResultCode::UNKNOWN_ERROR);

    NetworkResult w = transport.write(req, static_cast<size_t>(n));
    if (w.code() != NetworkResultCode::SUCCESS ||
        static_cast<size_t>(w.bytes()) != static_cast<size_t>(n))
        return NetworkResult(w.code() == NetworkResultCode::SUCCESS
                                 ? NetworkResultCode::SOCKET_CLOSED : w.code());

    // Read the 101 response.
    std::vector<char> buf;
    char scratch[4096];
    int    minor = 0, status = 0;
    const char* msg = nullptr;
    size_t msg_len = 0;
    struct phr_header headers[64];
    size_t num_headers = 0;
    size_t header_end = 0;
    size_t last_len = 0;

    for (;;) {
        if (!buf.empty()) {
            num_headers = sizeof(headers) / sizeof(headers[0]);
            int ret = phr_parse_response(buf.data(), buf.size(),
                                         &minor, &status, &msg, &msg_len,
                                         headers, &num_headers, last_len);
            if (ret > 0) { header_end = static_cast<size_t>(ret); break; }
            if (ret == -1) return NetworkResult(NetworkResultCode::UNKNOWN_ERROR);
            last_len = buf.size();
        }
        NetworkResult r = transport.read(scratch, sizeof(scratch));
        if (r.code() == NetworkResultCode::SUCCESS && r.bytes() > 0) {
            buf.insert(buf.end(), scratch, scratch + r.bytes());
            continue;
        }
        return NetworkResult(r.code() == NetworkResultCode::SUCCESS
                                 ? NetworkResultCode::SOCKET_CLOSED : r.code());
    }

    if (status != 101)
        return NetworkResult(NetworkResultCode::UNKNOWN_ERROR);

    // Verify Sec-WebSocket-Accept against SHA1(key + GUID).
    char expected[HttpParser::ACCEPT_BUF_SIZE];
    if (!HttpParser::computeAccept(key, std::strlen(key),
                                   expected, sizeof(expected)))
        return NetworkResult(NetworkResultCode::UNKNOWN_ERROR);

    bool accept_ok = false;
    for (size_t i = 0; i < num_headers; ++i) {
        const struct phr_header& h = headers[i];
        if (h.name_len != 20) continue;   // "Sec-WebSocket-Accept"
        bool name_match = true;
        const char* want = "sec-websocket-accept";
        for (size_t k = 0; k < 20; ++k)
            if (std::tolower(static_cast<unsigned char>(h.name[k])) != want[k]) {
                name_match = false; break;
            }
        if (!name_match) continue;
        size_t elen = std::strlen(expected);
        if (h.value_len == elen && std::memcmp(h.value, expected, elen) == 0)
            accept_ok = true;
        break;
    }
    if (!accept_ok)
        return NetworkResult(NetworkResultCode::UNKNOWN_ERROR);

    // Any bytes past the handshake response are the first WS frame(s).
    if (header_end < buf.size())
        _recv.assign(buf.begin() + header_end, buf.end());

    return NetworkResult(NetworkResultCode::SUCCESS);
}

NetworkResult RequestClientImpl::sendFrame(Client& transport, uint8_t opcode,
                                           const char* payload, size_t len)
{
    unsigned char mask[4];
    fillMaskKey(mask);

    std::vector<char> frame(len + 14);
    size_t n = WsFrameCodec::encodeMasked(static_cast<WsOpcode>(opcode),
                                          payload, len, true, mask,
                                          frame.data(), frame.size());
    if (n == 0)
        return NetworkResult(NetworkResultCode::UNKNOWN_ERROR);

    NetworkResult w = transport.write(frame.data(), n);
    if (w.code() != NetworkResultCode::SUCCESS ||
        static_cast<size_t>(w.bytes()) != n)
        return NetworkResult(w.code() == NetworkResultCode::SUCCESS
                                 ? NetworkResultCode::SOCKET_CLOSED : w.code());
    return NetworkResult(NetworkResultCode::SUCCESS);
}

NetworkResult RequestClientImpl::send(Client& transport, const void* data, size_t len)
{
    if (!_is_ws)
        return transport.write(data, len);
    if (_ws_closed)
        return NetworkResult(NetworkResultCode::SOCKET_CLOSED);
    return sendFrame(transport, static_cast<uint8_t>(WsOpcode::BINARY),
                     static_cast<const char*>(data), len);
}

NetworkResult RequestClientImpl::receive(Client& transport, void* buf,
                                         size_t buf_size, size_t* received,
                                         uint64_t /*timeout_ms*/)
{
    if (received) *received = 0;

    if (!_is_ws) {
        NetworkResult r = transport.read(buf, buf_size);
        if (r.code() == NetworkResultCode::SUCCESS && received)
            *received = static_cast<size_t>(r.bytes());
        return r;
    }

    if (_ws_closed)
        return NetworkResult(NetworkResultCode::SOCKET_CLOSED);

    char scratch[65536];

    for (;;) {
        // Drain whatever full frames are already buffered.
        while (!_recv.empty()) {
            WsFrameView view;
            WsFrameCodec::DecodeResult dr =
                WsFrameCodec::decodeServer(_recv.data(), _recv.size(), view);

            if (dr == WsFrameCodec::DecodeResult::NEED_MORE)
                break;   // fall through to read more bytes
            if (dr == WsFrameCodec::DecodeResult::INVALID) {
                sendFrame(transport, static_cast<uint8_t>(WsOpcode::CLOSE),
                          nullptr, 0);
                _ws_closed = true;
                return NetworkResult(NetworkResultCode::SSL_PROTOCOL_ERROR);
            }

            const WsOpcode op = view.opcode;
            const size_t   flen = view.frame_len;

            if (op == WsOpcode::CLOSE) {
                // Echo a (masked) close and report closure.
                sendFrame(transport, static_cast<uint8_t>(WsOpcode::CLOSE),
                          view.payload, view.payload_len);
                _ws_closed = true;
                _recv.erase(_recv.begin(), _recv.begin() + flen);
                return NetworkResult(NetworkResultCode::SOCKET_CLOSED);
            }
            if (op == WsOpcode::PING) {
                sendFrame(transport, static_cast<uint8_t>(WsOpcode::PONG),
                          view.payload, view.payload_len);
                _recv.erase(_recv.begin(), _recv.begin() + flen);
                continue;
            }
            if (op == WsOpcode::PONG) {
                _recv.erase(_recv.begin(), _recv.begin() + flen);
                continue;
            }

            // Data frame (BINARY / TEXT / CONTINUATION) — reassemble.
            _assembly.insert(_assembly.end(), view.payload,
                             view.payload + view.payload_len);
            _recv.erase(_recv.begin(), _recv.begin() + flen);

            if (view.fin) {
                size_t n = _assembly.size();
                if (n > buf_size) n = buf_size;   // truncate to caller buffer
                if (n) std::memcpy(buf, _assembly.data(), n);
                if (received) *received = n;
                _assembly.clear();
                return NetworkResult(NetworkResultCode::SUCCESS,
                                     static_cast<int32_t>(n));
            }
            // else: more fragments follow — keep draining.
        }

        // Need more bytes from the wire.
        NetworkResult r = transport.read(scratch, sizeof(scratch));
        if (r.code() == NetworkResultCode::SUCCESS && r.bytes() > 0) {
            _recv.insert(_recv.end(), scratch, scratch + r.bytes());
            continue;
        }
        return r;   // timeout / closed / error
    }
}

// ── RequestClient (public facade) ──────────────────────────────────────────

static RequestClientImpl* impl_of(char* c)
{
    return reinterpret_cast<RequestClientImpl*>(c);
}

Bn3Monkey::RequestClient::RequestClient(const NetworkConfiguration& cfg)
    : Client(cfg)
{
    static_assert(sizeof(RequestClientImpl) <= IMPLEMENTATION_SIZE,
                  "RequestClientImpl no longer fits in _rc_container; "
                  "raise RequestClient::IMPLEMENTATION_SIZE");
    new (_rc_container) RequestClientImpl(cfg, WebSocketConfiguration{});
}

Bn3Monkey::RequestClient::RequestClient(const NetworkConfiguration& cfg,
                                        const TlsClientConfiguration& tls)
    : Client(cfg, tls)
{
    static_assert(sizeof(RequestClientImpl) <= IMPLEMENTATION_SIZE,
                  "RequestClientImpl no longer fits in _rc_container; "
                  "raise RequestClient::IMPLEMENTATION_SIZE");
    new (_rc_container) RequestClientImpl(cfg, WebSocketConfiguration{});
}

Bn3Monkey::RequestClient::RequestClient(const NetworkConfiguration& cfg,
                                        const WebSocketConfiguration& ws)
    : Client(cfg)
{
    static_assert(sizeof(RequestClientImpl) <= IMPLEMENTATION_SIZE,
                  "RequestClientImpl no longer fits in _rc_container; "
                  "raise RequestClient::IMPLEMENTATION_SIZE");
    new (_rc_container) RequestClientImpl(cfg, ws);
}

Bn3Monkey::RequestClient::RequestClient(const NetworkConfiguration& cfg,
                                        const TlsClientConfiguration& tls,
                                        const WebSocketConfiguration& ws)
    : Client(cfg, tls)
{
    static_assert(sizeof(RequestClientImpl) <= IMPLEMENTATION_SIZE,
                  "RequestClientImpl no longer fits in _rc_container; "
                  "raise RequestClient::IMPLEMENTATION_SIZE");
    new (_rc_container) RequestClientImpl(cfg, ws);
}

Bn3Monkey::RequestClient::~RequestClient()
{
    impl_of(_rc_container)->~RequestClientImpl();
}

NetworkResult Bn3Monkey::RequestClient::connect()
{
    return impl_of(_rc_container)->connect(*this);
}
NetworkResult Bn3Monkey::RequestClient::send(const void* data, size_t len)
{
    return impl_of(_rc_container)->send(*this, data, len);
}
NetworkResult Bn3Monkey::RequestClient::receive(void* buf, size_t buf_size,
                                                size_t* received, uint64_t timeout_ms)
{
    return impl_of(_rc_container)->receive(*this, buf, buf_size, received, timeout_ms);
}
bool Bn3Monkey::RequestClient::isWebSocket() const
{
    return reinterpret_cast<const RequestClientImpl*>(_rc_container)->isWebSocket();
}
