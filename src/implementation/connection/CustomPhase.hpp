#if !defined(__BN3MONKEY_CUSTOM_PHASE__)
#define __BN3MONKEY_CUSTOM_PHASE__

#include "ConnectionPhase.hpp"

namespace Bn3Monkey
{
    // Raw (non-tunnelled) Custom Protocol group: ReceivingCustomMessage ->
    // SendingCustomResponse -> WaitingForNextCustomMessage (state-machine.html
    // §6-4). A message is a fixed-size header (handler.headerSize()) followed by
    // a payload whose length the handler derives from the header.
    class CustomPhase : public ConnectionPhase
    {
    public:
        ConnectionState onReadable(PhaseHost& host,
                                   SocketMultiEventListener& listener) override;
        ConnectionState onSendComplete(PhaseHost& host) override;

    private:
        // Per-action result (state-machine.html §7-3), mapped to ConnectionState.
        enum class CustomMessageResult {
            NEED_MORE_BYTES,
            DISPATCHED_FAST,              // process() ran, response in output()
            DISPATCHED_FAST_NO_RESPONSE, // STREAM mode (processWithoutResponse)
            DISPATCHED_SLOW,             // handed to the worker
        };

        CustomMessageResult handle(PhaseHost& host, SocketMultiEventListener& listener);
        static ConnectionState mapResultToNextState(CustomMessageResult r);
    };
}

#endif // __BN3MONKEY_CUSTOM_PHASE__
