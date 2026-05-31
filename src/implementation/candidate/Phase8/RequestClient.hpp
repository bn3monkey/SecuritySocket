#if !defined(__BN3MONKEY_REQUEST_CLIENT__)
#define __BN3MONKEY_REQUEST_CLIENT__

// ── CANDIDATE (Phase 8) ──────────────────────────────────────────────────────
// PUBLIC `RequestClient` -> move to src/SecuritySocket.hpp on integration. The
// Custom-Protocol client; the ctor's WebSocketConfiguration toggles raw TCP vs
// WS-tunnelled, exactly mirroring the server's CustomProtocolRequestHandler
// (mileston_http.md §2-7). One test body exercises both transports.
//
// IMPORTANT framing note: WsFrameCodec is *server-side* — decode() rejects
// unmasked frames and encode() never masks. The client direction is the
// opposite (client->server MUST be masked; server->client is NOT), so this
// file carries its own masked encoder + unmasked decoder rather than reusing
// WsFrameCodec. Sec-WebSocket-Accept verification *does* reuse
// HttpParser::computeAccept.
// ─────────────────────────────────────────────────────────────────────────────

#include "../../SecuritySocket.hpp"

#include <string>
#include <vector>

namespace Bn3Monkey
{
    class SECURITYSOCKET_API RequestClient : public Client
    {
    public:
        // raw TCP / TLS.
        explicit RequestClient(const NetworkConfiguration& cfg);
        RequestClient(const NetworkConfiguration& cfg,
                      const TlsClientConfiguration& tls);

        // WS-tunnelled (pattern in ws). TLS form => wss://.
        RequestClient(const NetworkConfiguration& cfg,
                      const WebSocketConfiguration& ws);
        RequestClient(const NetworkConfiguration& cfg,
                      const TlsClientConfiguration& tls,
                      const WebSocketConfiguration& ws);

        // raw: TCP (+ TLS) connect. WS: connect + HTTP Upgrade handshake with
        // Sec-WebSocket-Accept verification.
        NetworkResult connect();

        // raw: straight Client::write. WS: wrap in a masked BINARY frame.
        NetworkResult send(const void* data, size_t len);

        // raw: straight Client::read. WS: decode frames, auto-PONG pings,
        // surface one (reassembled) BINARY message; CLOSE -> SOCKET_CLOSED.
        // *received gets the byte count copied into buf. timeout_ms is honoured
        // by the WS read loop; raw mode uses the NetworkConfiguration timeout.
        NetworkResult receive(void* buf, size_t buf_size, size_t* received,
                              uint64_t timeout_ms = 0);

        bool isWebSocket() const { return _is_websocket; }

    private:
        NetworkResult wsHandshake();
        NetworkResult wsReceive(void* buf, size_t buf_size, size_t* received,
                                uint64_t timeout_ms);

        std::string            _host;          // "ip:port" for Host header
        std::string            _ws_pattern;    // empty => raw mode
        bool                   _is_websocket{ false };
        bool                   _connected{ false };
        std::vector<char>      _rxbuf;         // bytes read but not yet framed
    };
}

#endif // __BN3MONKEY_REQUEST_CLIENT__
