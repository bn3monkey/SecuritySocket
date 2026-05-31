#if !defined(__BN3MONKEY_HTTP_PHASE__)
#define __BN3MONKEY_HTTP_PHASE__

// ── CANDIDATE (Phase 6 slice) ────────────────────────────────────────────────
// Pre-staged, NOT wired into the build. Intended destination on integration:
//   src/implementation/connection/HttpPhase.{hpp,cpp}
// The include paths below are written as if this file already lives in
// connection/ (same depth as SniffPhase/CustomPhase), so a plain copy compiles.
//
// Integration checklist (do these in ClientConnection.{hpp,cpp} when adopting):
//   - add `HttpPhase _http_phase;` member next to `_custom_phase`
//   - in phaseForState(): map ReceivingHttpRequest / SendingHttpResponse /
//     WaitingForNextHttpRequest -> &_http_phase
//   - SendingHandshakeResponse + the WebSocket group -> &_websocket_phase
//
// Buffer model: uses the StagingBuffer-based PhaseHost (input()/output() return
// StagingBuffer&). The header block is tokenised in place (NUL over the ':'
// after each field-name, the CR after each value, and the SP after METHOD and
// PATH); StagingBuffer::head() is a mutable void*, so the parsed pointers (which
// alias it) are const_cast for the NUL writes — sound, since the bytes are
// genuinely writable. handle() never reserve()s the input between parse and
// dispatch, so head() and every slice into it stay valid (no rebase needed).
// ─────────────────────────────────────────────────────────────────────────────

#include "ConnectionPhase.hpp"

#include "../http/HttpRequest.hpp"   // HeaderIndex (phase-local header storage)

namespace Bn3Monkey
{
    // HTTP/1.1 group: ReceivingHttpRequest -> SendingHttpResponse ->
    // WaitingForNextHttpRequest (keep-alive), plus the one sanctioned cross-group
    // exit: an Upgrade: websocket request that matches the handler's WS pattern
    // leaves through SendingHandshakeResponse into the WebSocket group
    // (mileston_http.md §2-10, state-machine.html §6-2).
    //
    // Division of labour mirrors CustomPhase: the host owns recv/send + the
    // single buffer + the SLOW worker; this phase owns header parsing, body
    // accumulation, route matching, handler dispatch and response serialisation,
    // and returns the next ConnectionState.
    class HttpPhase : public ConnectionPhase
    {
    public:
        ConnectionState onReadable(PhaseHost& host,
                                   SocketMultiEventListener& listener) override;
        ConnectionState onSendComplete(PhaseHost& host) override;
        void            reset() override;

    private:
        // Per-action result (state-machine.html §7-3), mapped to ConnectionState.
        enum class HttpRequestResult {
            NEED_MORE_BYTES,   // header/body incomplete -> stay ReceivingHttpRequest
            RESPONDING,        // response in output(), keep-alive decided below
            DISPATCHED_SLOW,   // handed to the worker; response fills later
            UPGRADING,         // 101 in output() -> SendingHandshakeResponse
            ERROR_CLOSE,       // error response in output() -> Closing
        };

        HttpRequestResult handle(PhaseHost& host, SocketMultiEventListener& listener);
        ConnectionState   mapResultToNextState(HttpRequestResult r) const;

        // ── phase-local state ──
        // NUL-terminated header view handed to HttpRequestImpl. Lives in the
        // phase (not on the onReadable stack) so it survives a SLOW dispatch.
        HeaderIndex _headers;
        // keep-alive verdict for the in-flight request, read in onSendComplete.
        bool        _keep_alive{ false };
        // picohttpparser re-scan hint: buffer length seen at the previous
        // incomplete parse (0 is always safe; this only saves re-scanning).
        size_t      _last_len{ 0 };
    };
}

#endif // __BN3MONKEY_HTTP_PHASE__
