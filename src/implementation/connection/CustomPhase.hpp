#if !defined(__BN3MONKEY_CUSTOM_PHASE__)
#define __BN3MONKEY_CUSTOM_PHASE__

#include "ConnectionPhase.hpp"

namespace Bn3Monkey
{
    // Raw (non-tunnelled) Custom Protocol group (state-machine.html §6-4).
    //
    // A message is a fixed-size header (handler.headerSize()) followed by a
    // payload whose length the handler derives from the header. classifyMode()
    // picks the dispatch path per message:
    //   FAST / SLOW  : ReceivingCustomMessage -> SendingCustomResponse -> Waiting
    //   WRITE_STREAM : the client streams response-less framed chunks into the
    //                  server. Entry fires onWriteStreamBegin and parks the
    //                  connection in ReceivingCustomStream; every later framed
    //                  message routes to onWriteStreamData until it returns
    //                  COMPLETE (-> WaitingForNextCustomMessage) or ABORT
    //                  (-> Closing). No response is ever produced.
    //   READ_STREAM  : one request triggers a server-driven chunk stream. Entry
    //                  fires onReadStreamBegin, produces the first chunk, and
    //                  parks in SendingCustomStream; each flush completion
    //                  (onSendComplete) produces the next chunk until COMPLETE.
    class CustomPhase : public ConnectionPhase
    {
    public:
        ConnectionState onReadable(PhaseHost& host,
                                   SocketMultiEventListener& listener) override;
        ConnectionState onSendComplete(PhaseHost& host) override;
        void            reset() override;

    private:
        // Parse / dispatch the next framed message according to the current
        // (local) state, looping over all complete messages in the buffer.
        ConnectionState handle(PhaseHost& host, SocketMultiEventListener& listener);

        // Produce one READ_STREAM chunk into output() and update _read_last.
        // Returns SendingCustomStream on CONTINUE/COMPLETE, Closing on ABORT.
        ConnectionState produceReadChunk(PhaseHost& host);

        // READ_STREAM bookkeeping: the last-chunk decision happens when the
        // chunk is *produced* (fill time), one tick before its flush completes
        // (the transition point). Cleared on reset().
        bool _read_last{ false };
    };
}

#endif // __BN3MONKEY_CUSTOM_PHASE__
