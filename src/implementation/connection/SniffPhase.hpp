#if !defined(__BN3MONKEY_SNIFF_PHASE__)
#define __BN3MONKEY_SNIFF_PHASE__

#include "ConnectionPhase.hpp"

namespace Bn3Monkey
{
    // Lifecycle entry phase: peek the first bytes and hand the connection to the
    // HTTP or Custom group (state-machine.html §6-1). Entered at most once, only
    // when the handler supports BOTH protocols; a single-protocol handler starts
    // directly in its group and never sees this phase.
    //
    // The sniff "result" is exactly ProtocolSniffer's verdict, so this phase
    // maps Protocol -> ConnectionState directly rather than defining a redundant
    // 1:1 result enum. (The richer Http/WebSocket/Custom phases keep their own
    // per-action result enums as the design doc mandates.)
    class SniffPhase : public ConnectionPhase
    {
    public:
        ConnectionState onReadable(PhaseHost& host,
                                   SocketMultiEventListener& listener) override;
        ConnectionState onSendComplete(PhaseHost& host) override;
    };
}

#endif // __BN3MONKEY_SNIFF_PHASE__
