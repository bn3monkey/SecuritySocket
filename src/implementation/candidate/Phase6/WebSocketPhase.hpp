#if !defined(__BN3MONKEY_WEBSOCKET_PHASE__)
#define __BN3MONKEY_WEBSOCKET_PHASE__

// ── CANDIDATE (Phase 6 slice) ────────────────────────────────────────────────
// Pre-staged, NOT wired into the build. Intended destination on integration:
//   src/implementation/connection/WebSocketPhase.{hpp,cpp}
// Include paths are written as if it lives in connection/ (copy-and-compile).
//
// Integration checklist (ClientConnection.{hpp,cpp}):
//   - add `WebSocketPhase _websocket_phase;` member
//   - phaseForState(): SendingHandshakeResponse / ReceivingWebSocketFrame /
//     SendingWebSocketResponse / WaitingForNextWebSocketMessage -> &_websocket_phase
//   - HttpPhase produces the 101 handshake and returns SendingHandshakeResponse;
//     when the host flushes it, the *WebSocket* phase's onSendComplete runs
//     (group already switched), moving to WaitingForNextWebSocketMessage.
//
// Buffer model: StagingBuffer-based PhaseHost. WsFrameCodec::decode unmasks the
// payload in place; it takes char* and StagingBuffer::head() is a mutable void*,
// so no const_cast is needed. Frames are drain()ed from input() as consumed;
// responses are clear()/encode()/fill()'d into output().
// ─────────────────────────────────────────────────────────────────────────────

#include "ConnectionPhase.hpp"

#include <cstdint>
#include <vector>

namespace Bn3Monkey
{
    // WebSocket group: the transport for tunnelled Custom Protocol
    // (mileston_http.md §2-8). A complete (possibly fragmented) BINARY message
    // is the Custom message — header + payload — and flows through the exact
    // same CustomProtocolRequestHandler path as raw TCP. TEXT is rejected
    // (1003); control frames (PING/PONG/CLOSE) are handled inline.
    //
    // Reassembly + the response scratch live in the phase object (one per
    // connection) so they survive a SLOW dispatch, the same lifetime trick
    // CustomPhase uses for its in-buffer message.
    class WebSocketPhase : public ConnectionPhase
    {
    public:
        ConnectionState onReadable(PhaseHost& host,
                                   SocketMultiEventListener& listener) override;
        ConnectionState onSendComplete(PhaseHost& host) override;
        void            reset() override;

    private:
        // Per-action result (state-machine.html §7-3), mapped to ConnectionState.
        enum class WebSocketFrameResult {
            NEED_MORE_BYTES,             // partial frame -> stay reading
            CONTINUATION_PENDING,        // data frame buffered, FIN not yet seen
            MESSAGE_DISPATCHED_FAST,     // full message processed, response queued
            MESSAGE_DISPATCHED_SLOW,     // handed to the worker
            MESSAGE_NO_RESPONSE,         // STREAM mode (processWithoutResponse)
            CONTROL_RESPONSE,            // PONG / CLOSE echo queued to output
            CONTROL_NOOP,                // PONG received: counters only, keep reading
            CLOSING,                     // CLOSE echo / protocol-error close queued
        };

        WebSocketFrameResult handle(PhaseHost& host, SocketMultiEventListener& listener);
        ConnectionState      mapResultToNextState(WebSocketFrameResult r) const;

        // Dispatch a fully-reassembled Custom message (in _fragment) to the
        // handler, wrapping any response in a single BINARY frame in output().
        WebSocketFrameResult dispatchMessage(PhaseHost& host,
                                             SocketMultiEventListener& listener);

        // ── phase-local state ──
        std::vector<char> _fragment;          // reassembled BINARY payload
        bool              _message_started{ false };
        std::vector<char> _resp_scratch;      // handler writes here, then framed
        uint64_t          _pong_count{ 0 };   // §2-8: 0xA Pong -> stats only
    };
}

#endif // __BN3MONKEY_WEBSOCKET_PHASE__
