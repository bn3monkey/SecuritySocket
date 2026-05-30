#if !defined(__BN3MONKEY_WS_FRAME_CODEC__)
#define __BN3MONKEY_WS_FRAME_CODEC__

#include "../../SecuritySocket.hpp"

#include <cstddef>
#include <cstdint>

namespace Bn3Monkey
{
    // RFC 6455 §5.2 opcodes. Custom Protocol travels only on BINARY frames
    // (mileston_http.md §2-8); TEXT is rejected, the rest are control frames.
    enum class WsOpcode : uint8_t {
        CONTINUATION = 0x0,
        TEXT         = 0x1,
        BINARY       = 0x2,
        CLOSE        = 0x8,
        PING         = 0x9,
        PONG         = 0xA,
    };

    // One decoded frame. `payload` points into the buffer handed to decode()
    // and has been unmasked in place; `frame_len` is the number of bytes the
    // whole frame (header + payload) occupied so the caller can advance past it.
    struct WsFrameView
    {
        bool        fin         = false;
        WsOpcode    opcode      = WsOpcode::CONTINUATION;
        const char* payload     = nullptr;
        size_t      payload_len = 0;
        size_t      frame_len   = 0;
    };

    // Stateless RFC 6455 frame codec used by ClientConnectionImpl's WebSocket
    // states. decode() works in place on the connection's single input buffer
    // (it unmasks the payload where it sits); the encode*() helpers produce
    // server->client frames, which per the RFC are never masked.
    //
    // SECURITYSOCKET_API: header lives under src/implementation/ (users never
    // see it) but the test DLL links securitysocket.dll and unit-tests the
    // codec directly — same export rationale as HttpRouterImpl.
    class SECURITYSOCKET_API WsFrameCodec
    {
    public:
        enum class DecodeResult {
            NEED_MORE,   // not enough bytes for the header or full payload yet
            OK,          // a full frame decoded; view filled, payload unmasked
            INVALID,     // protocol error (RSV set, unmasked client frame, ...)
        };

        // Decode the frame at in[0..in_len). On OK: `view` is filled and the
        // payload bytes inside `in` are unmasked in place. On NEED_MORE: recv
        // more and retry. On INVALID: caller sends Close(1002) and tears down.
        static DecodeResult decode(char* in, size_t in_len, WsFrameView& view);

        // Encode a server->client frame (unmasked). Returns bytes written into
        // `out`, or 0 if out_capacity is insufficient.
        static size_t encode(WsOpcode opcode, const char* payload, size_t len,
                             bool fin, char* out, size_t out_capacity);

        // Control-frame convenience builders (all unmasked, FIN=1).
        // Close with a 2-byte big-endian status code and no reason text.
        static size_t encodeClose(uint16_t status_code,
                                  char* out, size_t out_capacity);
        // Close echo: reflect the peer's close payload (status code + reason)
        // back, capped to the 125-byte control limit. < 2 bytes => bare close.
        static size_t encodeCloseEcho(const char* close_payload, size_t len,
                                      char* out, size_t out_capacity);
        // Pong carrying the ping's application data back verbatim (capped 125).
        static size_t encodePong(const char* ping_payload, size_t len,
                                 char* out, size_t out_capacity);

        // Largest server->client header we emit: 2 + 8 (64-bit length), no mask.
        static constexpr size_t MAX_SERVER_HEADER   = 10;
        // RFC 6455 §5.5 caps control-frame payloads at 125 bytes.
        static constexpr size_t MAX_CONTROL_PAYLOAD = 125;
    };
}

#endif // __BN3MONKEY_WS_FRAME_CODEC__
