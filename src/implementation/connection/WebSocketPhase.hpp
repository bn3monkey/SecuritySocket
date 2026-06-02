#if !defined(__BN3MONKEY_WEBSOCKET_PHASE__)
#define __BN3MONKEY_WEBSOCKET_PHASE__

// WebSocket phase, wired into ClientConnectionImpl (phaseForState maps the WS
// group here). HttpPhase produces the 101 handshake and returns
// SendingHandshakeResponse; once the host flushes it the group has already
// switched, so this phase's onSendComplete runs and moves to
// WaitingForNextWebSocketMessage.
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
    // is one Custom message — header + payload — and flows through the exact
    // same CustomProtocolRequestHandler path as raw TCP. TEXT is rejected
    // (1003); control frames (PING/PONG/CLOSE) are handled inline.
    //
    // Streaming (milestone_write_stream.md §4 WS variant): the raw-TCP stream
    // model with each Custom chunk wrapped in one BINARY frame.
    //   WRITE_STREAM : entry fires onWriteStreamBegin and parks in
    //                  ReceivingWebSocketStream; each later BINARY message is a
    //                  chunk routed to onWriteStreamData until COMPLETE/ABORT.
    //   READ_STREAM  : entry fires onReadStreamBegin, frames the first chunk and
    //                  parks in SendingWebSocketStream; each flush completion
    //                  produces+frames the next chunk until COMPLETE.
    // A control frame (PING/CLOSE) arriving mid WRITE_STREAM is answered with a
    // PONG/echo in SendingWebSocketResponse and then *resumes* the stream state
    // (see _resume_*), rather than falling back to WaitingForNextWebSocketMessage.
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
        // Parse / dispatch one WebSocket frame, returning the next state. The
        // "keep reading" target depends on whether we are mid-stream
        // (ReceivingWebSocketStream) or not (ReceivingWebSocketFrame).
        ConnectionState handle(PhaseHost& host, SocketMultiEventListener& listener);

        // A fully-reassembled Custom message (in _fragment) has arrived. Route
        // it by the current state: a chunk (mid WRITE_STREAM) to onWriteStreamData,
        // otherwise classifyMode() -> stream begin / FAST / SLOW.
        ConnectionState dispatchComplete(PhaseHost& host,
                                         SocketMultiEventListener& listener,
                                         ConnectionState entry);

        // Produce one READ_STREAM chunk, frame it (BINARY, FIN), update
        // _read_last. Returns SendingWebSocketStream or Closing (ABORT).
        ConnectionState produceReadChunk(PhaseHost& host);

        // ── phase-local state ──
        std::vector<char> _fragment;          // reassembled BINARY payload
        bool              _message_started{ false };
        std::vector<char> _resp_scratch;      // handler writes here, then framed
        uint64_t          _pong_count{ 0 };   // §2-8: 0xA Pong -> stats only

        bool              _read_last{ false };          // READ_STREAM last-chunk tick
        bool              _resume_pending{ false };      // control frame mid-stream
        ConnectionState   _resume_after_control{ ConnectionState::WaitingForNextWebSocketMessage };
    };
}

#endif // __BN3MONKEY_WEBSOCKET_PHASE__
