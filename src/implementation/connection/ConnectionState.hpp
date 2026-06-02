#if !defined(__BN3MONKEY_CONNECTION_STATE__)
#define __BN3MONKEY_CONNECTION_STATE__

#include <cstdint>

namespace Bn3Monkey
{
    // The single shared vocabulary between the host (ClientConnectionImpl) and
    // the per-protocol Phase classes. Each state is a *wait point* — "which
    // external event are we ready to receive next" — exactly as defined in
    // docs/workplan/state-machine.html §7-1. Per-action result enums live in
    // each Phase's own header (they are private to that protocol); only these
    // two enums cross the host<->phase boundary.
    enum class ConnectionState : uint8_t {
        // Lifecycle (host-driven)
        Sniffing,                        // [READ]  first bytes, protocol detect
        Closing,                         // [WRITE] last bytes (error / close frame)
        Closed,                          // (terminal) removed from listener

        // HTTP group
        ReceivingHttpRequest,            // [READ]  header + body accumulation
        SendingHttpResponse,             // [WRITE] response send
        WaitingForNextHttpRequest,       // [READ]  keep-alive

        // WebSocket group (tunnelled Custom)
        SendingHandshakeResponse,        // [WRITE] 101 send
        ReceivingWebSocketFrame,         // [READ]  frame accumulation + unmask
        SendingWebSocketResponse,        // [WRITE] frame-wrapped response send
        WaitingForNextWebSocketMessage,  // [READ]  next frame
        ReceivingWebSocketStream,        // [READ]  WS tunnel WRITE_STREAM chunks
        SendingWebSocketStream,          // [WRITE] WS tunnel READ_STREAM chunks

        // Custom group (raw)
        ReceivingCustomMessage,          // [READ]  header + payload accumulation
        SendingCustomResponse,           // [WRITE] response send
        WaitingForNextCustomMessage,     // [READ]  next message
        ReceivingCustomStream,           // [READ]  WRITE_STREAM chunk ingestion
        SendingCustomStream,             // [WRITE] READ_STREAM chunk emission
    };

    // The four external events (state-machine.html §7-2). A SLOW worker
    // completing is NOT an event — the worker calls listener.addEvent(WRITE)
    // directly, which surfaces as a normal E_Write on the next wait().
    enum class Event : uint8_t {
        E_Accept,        // listening socket: new connection
        E_Read,          // client socket: POLLIN
        E_Write,         // client socket: POLLOUT
        E_Disconnected,  // client socket: POLLHUP / POLLERR / POLLNVAL / recv()==0
    };

    // Protocol identity a connection settles into after sniffing. Used by the
    // host to pick / swap the active Phase. Fixed for the connection lifetime
    // once left Sniff (state-machine.html §3, §8) — except the HTTP->WebSocket
    // upgrade, which is the one sanctioned cross-group move.
    enum class PhaseGroup : uint8_t {
        Lifecycle,   // Sniffing / Closing / Closed (host-handled)
        Http,
        WebSocket,
        Custom,
    };

    // Which Phase group owns a given state. Pure classification (no deps) so
    // the host can decide when to swap Phase objects on a state transition.
    constexpr PhaseGroup groupOf(ConnectionState s)
    {
        return
            (s == ConnectionState::ReceivingHttpRequest ||
             s == ConnectionState::SendingHttpResponse ||
             s == ConnectionState::WaitingForNextHttpRequest)            ? PhaseGroup::Http      :
            (s == ConnectionState::SendingHandshakeResponse ||
             s == ConnectionState::ReceivingWebSocketFrame ||
             s == ConnectionState::SendingWebSocketResponse ||
             s == ConnectionState::WaitingForNextWebSocketMessage ||
             s == ConnectionState::ReceivingWebSocketStream ||
             s == ConnectionState::SendingWebSocketStream)               ? PhaseGroup::WebSocket :
            (s == ConnectionState::ReceivingCustomMessage ||
             s == ConnectionState::SendingCustomResponse ||
             s == ConnectionState::WaitingForNextCustomMessage ||
             s == ConnectionState::ReceivingCustomStream ||
             s == ConnectionState::SendingCustomStream)                  ? PhaseGroup::Custom    :
                                                                          PhaseGroup::Lifecycle;
    }

    // A read-state listens for POLLIN, a write-state for POLLOUT. The host maps
    // this to the listener event type after every transition (state-machine.html
    // §2-3). Closed is terminal (neither).
    constexpr bool isReadState(ConnectionState s)
    {
        return s == ConnectionState::Sniffing ||
               s == ConnectionState::ReceivingHttpRequest ||
               s == ConnectionState::WaitingForNextHttpRequest ||
               s == ConnectionState::ReceivingWebSocketFrame ||
               s == ConnectionState::WaitingForNextWebSocketMessage ||
               s == ConnectionState::ReceivingWebSocketStream ||
               s == ConnectionState::ReceivingCustomMessage ||
               s == ConnectionState::WaitingForNextCustomMessage ||
               s == ConnectionState::ReceivingCustomStream;
    }

    constexpr bool isWriteState(ConnectionState s)
    {
        return s == ConnectionState::Closing ||
               s == ConnectionState::SendingHttpResponse ||
               s == ConnectionState::SendingHandshakeResponse ||
               s == ConnectionState::SendingWebSocketResponse ||
               s == ConnectionState::SendingWebSocketStream ||
               s == ConnectionState::SendingCustomResponse ||
               s == ConnectionState::SendingCustomStream;
    }
}

#endif // __BN3MONKEY_CONNECTION_STATE__
