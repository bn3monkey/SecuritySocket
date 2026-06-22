#if !defined(__BN3MONKEY_CONNECTION_PHASE__)
#define __BN3MONKEY_CONNECTION_PHASE__

#include "ConnectionState.hpp"
#include "../core/memory/buffer.hpp"

#include <cstddef>
#include <functional>

namespace Bn3Monkey
{
    class ClientConnection;               // user-facing view (passed to handlers)
    class HttpRouterImpl;                 // HTTP route table
    class CustomProtocolRequestHandler;   // custom dispatch target
    class SocketMultiEventListener;       // owned by the server, threaded through

    // The slice of host services a Phase is allowed to touch. The host
    // (ClientConnectionImpl) implements this; Phases see only this interface so
    // they stay decoupled from socket/worker/listener mechanics and remain unit-
    // testable against a fake host.
    //
    // Division of labour (docs/workplan/client_connection.html §4-1, §7):
    //   - Host owns recv/send bytes, the single input/output buffer, the listener,
    //     and the lazy SLOW worker.
    //   - Phase owns parsing, handler dispatch, and the next-state decision.
    class PhaseHost
    {
    public:
        virtual ~PhaseHost() = default;

        // The connection's current state. A streaming phase reads this to know
        // whether it is entering a stream (normal receive-state) or already
        // mid-stream (a Receiving*Stream / Sending*Stream state). The host owns
        // the authoritative value and only assigns it the phase's *return*; the
        // phase must NOT assume mid-call transitions are reflected here, so it
        // tracks its own local state while looping inside onReadable().
        virtual ConnectionState state() const = 0;

        // ── input accumulation buffer ──
        // recv appends at tail(); the active phase parses the current message
        // from head() for pending() bytes, then drain()s it once consumed —
        // trailing (pipelined) bytes stay pending for the next parse. The host
        // reserve()s / compact()s it around recv, so a phase must re-fetch
        // head() after any reserve(). See StagingBuffer.
        virtual StagingBuffer& input()  = 0;

        // ── response output buffer (host flushes it after a Sending* state) ──
        // A phase clear()s it, writes the response into data(), then fill()s the
        // produced length; the host drains head()..pending() to the socket.
        virtual StagingBuffer& output() = 0;

        // ── collaborators ──
        // The ClientConnection passed to user handler callbacks (the host itself).
        virtual ClientConnection&             connection()    = 0;
        virtual HttpRouterImpl*               router()        = 0;   // null => no HTTP
        virtual CustomProtocolRequestHandler* customHandler() = 0;   // null => no Custom
        virtual const char*                   wsPattern() const = 0; // null => no WS

        // The configured HTTP body cap (NetworkConfiguration::max_http_request_
        // body_size). HttpPhase rejects a larger Content-Length with 413 before
        // allocating. Non-pure with a default so test fakes need not override it;
        // the default mirrors NetworkConfiguration's own default (64 MB).
        virtual size_t maxHttpRequestBodySize() const { return 64u * 1024 * 1024; }

        // Latch the connection as upgraded to WebSocket (affects isWebSocket()).
        virtual void setWebSocket(bool on) = 0;

        // SLOW dispatch. Encapsulates the ordering invariant
        // (listener.removeEvent BEFORE queueing the worker — state-machine.html
        // §8) so Phases cannot get it wrong: they just hand over the call and
        // return the Sending* state. The host marks itself "detached" so it does
        // NOT re-arm the listener; the worker later calls addEvent(WRITE).
        virtual void dispatchSlow(std::function<void()> call,
                                  SocketMultiEventListener& listener) = 0;
    };

    // A per-protocol strategy driving one connection's read/write while it is in
    // that protocol's state group. One instance per connection (may hold small
    // phase-local accumulation state, e.g. WebSocket fragment reassembly).
    class ConnectionPhase
    {
    public:
        virtual ~ConnectionPhase() = default;

        // Buffered input is available while in a read-state of this group.
        // Parse/dispatch as much as possible (drain()ing input as messages are
        // consumed) and return the next ConnectionState. NEED_MORE is expressed
        // by returning the same Receiving* state WITHOUT draining input (the host
        // then waits for the socket). For SLOW, call host.dispatchSlow(call,
        // listener) and return the Sending* state. To send an error/close,
        // clear()+write+fill() output() and return ConnectionState::Closing.
        virtual ConnectionState onReadable(PhaseHost& host,
                                           SocketMultiEventListener& listener) = 0;

        // The host has fully flushed output() while in a Sending* state of this
        // group. Return the next ConnectionState (e.g. keep-alive ->
        // WaitingForNextHttpRequest, handshake done -> WaitingForNextWebSocket-
        // Message, response done -> WaitingForNext..., or Closing/Closed).
        virtual ConnectionState onSendComplete(PhaseHost& host) = 0;

        // Clear phase-local accumulation when the connection (re)enters this
        // phase fresh (default: nothing to reset).
        virtual void reset() {}
    };
}

#endif // __BN3MONKEY_CONNECTION_PHASE__
