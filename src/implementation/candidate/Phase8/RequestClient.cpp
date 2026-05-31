#include "RequestClient.hpp"

#include "../protocol/HttpParser.hpp"   // computeAccept (handshake verification)

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace Bn3Monkey
{
    namespace
    {
        const char* const kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        // Minimal RFC 4648 base64 (no line wrapping) — used for the 16-byte
        // Sec-WebSocket-Key. (Accept verification reuses HttpParser.)
        std::string base64(const unsigned char* in, size_t n)
        {
            static const char tbl[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((n + 2) / 3) * 4);
            size_t i = 0;
            for (; i + 3 <= n; i += 3) {
                const uint32_t v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
                out.push_back(tbl[(v >> 18) & 0x3F]);
                out.push_back(tbl[(v >> 12) & 0x3F]);
                out.push_back(tbl[(v >> 6) & 0x3F]);
                out.push_back(tbl[v & 0x3F]);
            }
            if (i < n) {
                const uint32_t b0 = in[i];
                const uint32_t b1 = (i + 1 < n) ? in[i + 1] : 0;
                const uint32_t v  = (b0 << 16) | (b1 << 8);
                out.push_back(tbl[(v >> 18) & 0x3F]);
                out.push_back(tbl[(v >> 12) & 0x3F]);
                out.push_back((i + 1 < n) ? tbl[(v >> 6) & 0x3F] : '=');
                out.push_back('=');
            }
            return out;
        }

        std::string makeHost(const NetworkConfiguration& cfg)
        {
            NetworkConfiguration c = cfg;   // ip()/port() are non-const accessors
            std::string host = c.ip();
            host.push_back(':');
            host += c.port();
            return host;
        }

        // Client->server frame, masked per RFC 6455 §5.3. Returns wire bytes.
        std::vector<char> encodeMaskedFrame(uint8_t opcode, bool fin,
                                            const void* payload, size_t len)
        {
            std::random_device rd;
            const uint32_t key32 = rd();
            const unsigned char mask[4] = {
                static_cast<unsigned char>(key32 & 0xFF),
                static_cast<unsigned char>((key32 >> 8) & 0xFF),
                static_cast<unsigned char>((key32 >> 16) & 0xFF),
                static_cast<unsigned char>((key32 >> 24) & 0xFF),
            };

            std::vector<char> out;
            out.push_back(static_cast<char>((fin ? 0x80 : 0x00) | (opcode & 0x0F)));

            if (len <= 125) {
                out.push_back(static_cast<char>(0x80 | len));   // MASK bit set
            } else if (len <= 0xFFFF) {
                out.push_back(static_cast<char>(0x80 | 126));
                out.push_back(static_cast<char>((len >> 8) & 0xFF));
                out.push_back(static_cast<char>(len & 0xFF));
            } else {
                out.push_back(static_cast<char>(0x80 | 127));
                const uint64_t n = len;
                for (int i = 0; i < 8; ++i)
                    out.push_back(static_cast<char>((n >> (56 - 8 * i)) & 0xFF));
            }

            out.insert(out.end(), reinterpret_cast<const char*>(mask),
                       reinterpret_cast<const char*>(mask) + 4);

            const unsigned char* p = static_cast<const unsigned char*>(payload);
            for (size_t i = 0; i < len; ++i)
                out.push_back(static_cast<char>(p[i] ^ mask[i & 3]));
            return out;
        }

        // Server->client frame decode (unmasked; tolerates a mask defensively).
        // Does NOT modify 'in' unless a mask is present (then unmasks in place).
        struct ClientFrameView {
            bool        fin{ false };
            uint8_t     opcode{ 0 };
            const char* payload{ nullptr };
            size_t      payload_len{ 0 };
            size_t      frame_len{ 0 };
        };
        enum class ClientDecode { NEED_MORE, OK, INVALID };

        ClientDecode decodeServerFrame(char* in, size_t in_len, ClientFrameView& v)
        {
            const unsigned char* p = reinterpret_cast<unsigned char*>(in);
            if (in_len < 2) return ClientDecode::NEED_MORE;

            const uint8_t b0 = p[0];
            const uint8_t b1 = p[1];
            const uint8_t rsv = (b0 >> 4) & 0x07;
            if (rsv != 0) return ClientDecode::INVALID;

            const uint8_t opcode = b0 & 0x0F;
            switch (opcode) {
                case 0x0: case 0x1: case 0x2: case 0x8: case 0x9: case 0xA: break;
                default: return ClientDecode::INVALID;
            }
            const bool masked = (b1 & 0x80) != 0;
            uint64_t   len     = b1 & 0x7F;
            size_t     off     = 2;

            if (len == 126) {
                if (in_len < off + 2) return ClientDecode::NEED_MORE;
                len = (static_cast<uint64_t>(p[2]) << 8) | p[3];
                off += 2;
            } else if (len == 127) {
                if (in_len < off + 8) return ClientDecode::NEED_MORE;
                len = 0;
                for (size_t i = 0; i < 8; ++i) len = (len << 8) | p[off + i];
                off += 8;
            }

            unsigned char mask[4] = {0, 0, 0, 0};
            if (masked) {
                if (in_len < off + 4) return ClientDecode::NEED_MORE;
                for (int i = 0; i < 4; ++i) mask[i] = p[off + i];
                off += 4;
            }
            if (in_len < off + len) return ClientDecode::NEED_MORE;

            char* payload = in + off;
            if (masked) {
                for (uint64_t i = 0; i < len; ++i)
                    payload[i] = static_cast<char>(
                        static_cast<unsigned char>(payload[i]) ^ mask[i & 3]);
            }

            v.fin         = (b0 & 0x80) != 0;
            v.opcode      = opcode;
            v.payload     = payload;
            v.payload_len = static_cast<size_t>(len);
            v.frame_len   = off + static_cast<size_t>(len);
            return ClientDecode::OK;
        }
    }

    // ── ctors ───────────────────────────────────────────────────────────────

    RequestClient::RequestClient(const NetworkConfiguration& cfg)
        : Client(cfg), _host(makeHost(cfg)) {}

    RequestClient::RequestClient(const NetworkConfiguration& cfg,
                                 const TlsClientConfiguration& tls)
        : Client(cfg, tls), _host(makeHost(cfg)) {}

    RequestClient::RequestClient(const NetworkConfiguration& cfg,
                                 const WebSocketConfiguration& ws)
        : Client(cfg), _host(makeHost(cfg)),
          _ws_pattern(ws.pattern ? ws.pattern : ""),
          _is_websocket(ws.valid()) {}

    RequestClient::RequestClient(const NetworkConfiguration& cfg,
                                 const TlsClientConfiguration& tls,
                                 const WebSocketConfiguration& ws)
        : Client(cfg, tls), _host(makeHost(cfg)),
          _ws_pattern(ws.pattern ? ws.pattern : ""),
          _is_websocket(ws.valid()) {}

    // ── connect ───────────────────────────────────────────────────────────────

    NetworkResult RequestClient::connect()
    {
        NetworkResult r = open();
        if (r.code() != NetworkResultCode::SUCCESS) return r;
        r = Client::connect();
        if (r.code() != NetworkResultCode::SUCCESS) return r;

        if (_is_websocket) {
            r = wsHandshake();
            if (r.code() != NetworkResultCode::SUCCESS) return r;
        }
        _connected = true;
        return NetworkResult(NetworkResultCode::SUCCESS);
    }

    NetworkResult RequestClient::wsHandshake()
    {
        // 16 random key bytes -> base64 (RFC 6455 §4.1).
        unsigned char key_bytes[16];
        {
            std::random_device rd;
            for (size_t i = 0; i < sizeof(key_bytes); i += 4) {
                const uint32_t v = rd();
                key_bytes[i]     = static_cast<unsigned char>(v & 0xFF);
                key_bytes[i + 1] = static_cast<unsigned char>((v >> 8) & 0xFF);
                key_bytes[i + 2] = static_cast<unsigned char>((v >> 16) & 0xFF);
                key_bytes[i + 3] = static_cast<unsigned char>((v >> 24) & 0xFF);
            }
        }
        const std::string key_b64 = base64(key_bytes, sizeof(key_bytes));

        // Upgrade request.
        std::string req;
        req  = "GET ";
        req += _ws_pattern.empty() ? "/" : _ws_pattern;
        req += " HTTP/1.1\r\nHost: ";
        req += _host;
        req += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: ";
        req += key_b64;
        req += "\r\nSec-WebSocket-Version: 13\r\n\r\n";

        NetworkResult w = write(req.data(), req.size());
        if (w.code() != NetworkResultCode::SUCCESS) return w;

        // Read until the header terminator.
        std::vector<char> buf;
        char chunk[2048];
        size_t header_end = 0;
        for (;;) {
            NetworkResult r = read(chunk, sizeof(chunk));
            if (r.code() == NetworkResultCode::SUCCESS && r.bytes() > 0) {
                buf.insert(buf.end(), chunk, chunk + r.bytes());
                for (size_t i = 0; i + 3 < buf.size(); ++i) {
                    if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                        buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                        header_end = i + 4; break;
                    }
                }
                if (header_end) break;
                continue;
            }
            return NetworkResult(r.code() == NetworkResultCode::SUCCESS
                                     ? NetworkResultCode::SOCKET_CLOSED
                                     : r.code());
        }

        // Status must be 101.
        {
            const char* d = buf.data();
            const char* sp = d;
            const char* end = d + header_end;
            while (sp < end && *sp != ' ') ++sp;
            while (sp < end && *sp == ' ') ++sp;
            const int status = std::atoi(sp);
            if (status != 101) return NetworkResult(NetworkResultCode::SSL_PROTOCOL_ERROR);
        }

        // Verify Sec-WebSocket-Accept == base64(sha1(key + GUID)).
        {
            char expected[HttpParser::ACCEPT_BUF_SIZE];
            if (!HttpParser::computeAccept(key_b64.c_str(), key_b64.size(),
                                           expected, sizeof(expected))) {
                return NetworkResult(NetworkResultCode::SSL_PROTOCOL_ERROR);
            }
            // Find the accept header value (case-insensitive name).
            std::string hdrs(buf.data(), header_end);
            std::string lower = hdrs;
            for (char& c : lower) c = static_cast<char>(std::tolower((unsigned char)c));
            const std::string needle = "sec-websocket-accept:";
            const size_t pos = lower.find(needle);
            if (pos == std::string::npos) return NetworkResult(NetworkResultCode::SSL_PROTOCOL_ERROR);
            size_t v = pos + needle.size();
            while (v < hdrs.size() && (hdrs[v] == ' ' || hdrs[v] == '\t')) ++v;
            size_t e = v;
            while (e < hdrs.size() && hdrs[e] != '\r' && hdrs[e] != '\n') ++e;
            const std::string got = hdrs.substr(v, e - v);
            if (got != expected) return NetworkResult(NetworkResultCode::TLS_HOSTNAME_MISMATCH);
        }

        // Carry leftover (already-read) bytes into the frame buffer.
        if (buf.size() > header_end) {
            _rxbuf.assign(buf.begin() + header_end, buf.end());
        }
        return NetworkResult(NetworkResultCode::SUCCESS);
    }

    // ── send ────────────────────────────────────────────────────────────────

    NetworkResult RequestClient::send(const void* data, size_t len)
    {
        if (!_is_websocket) {
            return write(data, len);
        }
        std::vector<char> frame = encodeMaskedFrame(0x2 /*BINARY*/, true, data, len);
        return write(frame.data(), frame.size());
    }

    // ── receive ───────────────────────────────────────────────────────────────

    NetworkResult RequestClient::receive(void* buf, size_t buf_size,
                                         size_t* received, uint64_t timeout_ms)
    {
        if (received) *received = 0;
        if (!_is_websocket) {
            NetworkResult r = read(buf, buf_size);
            if (received && r.bytes() > 0) *received = static_cast<size_t>(r.bytes());
            return r;
        }
        return wsReceive(buf, buf_size, received, timeout_ms);
    }

    NetworkResult RequestClient::wsReceive(void* buf, size_t buf_size,
                                           size_t* received, uint64_t timeout_ms)
    {
        (void)timeout_ms;   // raw read uses the configured timeout; see header note
        std::vector<char> message;   // reassembled BINARY payload

        for (;;) {
            // Try to frame from whatever is already buffered.
            ClientFrameView v;
            ClientDecode dr = decodeServerFrame(_rxbuf.data(), _rxbuf.size(), v);

            if (dr == ClientDecode::INVALID) {
                return NetworkResult(NetworkResultCode::SSL_PROTOCOL_ERROR);
            }
            if (dr == ClientDecode::OK) {
                const uint8_t op = v.opcode;
                if (op == 0x9 /*PING*/) {
                    std::vector<char> pong =
                        encodeMaskedFrame(0xA, true, v.payload, v.payload_len);
                    _rxbuf.erase(_rxbuf.begin(), _rxbuf.begin() + v.frame_len);
                    NetworkResult w = write(pong.data(), pong.size());
                    if (w.code() != NetworkResultCode::SUCCESS) return w;
                    continue;
                }
                if (op == 0xA /*PONG*/) {
                    _rxbuf.erase(_rxbuf.begin(), _rxbuf.begin() + v.frame_len);
                    continue;
                }
                if (op == 0x8 /*CLOSE*/) {
                    std::vector<char> echo =
                        encodeMaskedFrame(0x8, true, v.payload, v.payload_len);
                    _rxbuf.erase(_rxbuf.begin(), _rxbuf.begin() + v.frame_len);
                    write(echo.data(), echo.size());
                    return NetworkResult(NetworkResultCode::SOCKET_CLOSED);
                }
                if (op == 0x1 /*TEXT*/) {
                    return NetworkResult(NetworkResultCode::SSL_PROTOCOL_ERROR);
                }
                // BINARY (0x2) or CONTINUATION (0x0): accumulate.
                message.insert(message.end(), v.payload, v.payload + v.payload_len);
                _rxbuf.erase(_rxbuf.begin(), _rxbuf.begin() + v.frame_len);
                if (v.fin) {
                    const size_t n = (message.size() < buf_size) ? message.size() : buf_size;
                    std::memcpy(buf, message.data(), n);
                    if (received) *received = n;
                    return NetworkResult(NetworkResultCode::SUCCESS,
                                         static_cast<int32_t>(n));
                }
                continue;   // need more frames for this message
            }

            // NEED_MORE — pull more bytes.
            char chunk[8192];
            NetworkResult r = read(chunk, sizeof(chunk));
            if (r.code() == NetworkResultCode::SUCCESS && r.bytes() > 0) {
                _rxbuf.insert(_rxbuf.end(), chunk, chunk + r.bytes());
                continue;
            }
            if (r.code() == NetworkResultCode::SOCKET_CLOSED) {
                return NetworkResult(NetworkResultCode::SOCKET_CLOSED);
            }
            return r;   // timeout / transport error
        }
    }
}
