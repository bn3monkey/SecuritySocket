#include "CustomPhase.hpp"

#include "../../SecuritySocket.hpp"                 // CustomProtocolRequestHandler, ClientConnection
#include "../custom/CustomProtocolRequest.hpp"
#include "../custom/CustomProtocolResponse.hpp"

namespace Bn3Monkey
{
    ConnectionState CustomPhase::onReadable(PhaseHost& host,
                                            SocketMultiEventListener& listener)
    {
        return mapResultToNextState(handle(host, listener));
    }

    CustomPhase::CustomMessageResult
    CustomPhase::handle(PhaseHost& host, SocketMultiEventListener& listener)
    {
        CustomProtocolRequestHandler* handler = host.customHandler();
        // The host only routes to this phase when a Custom handler exists.
        const size_t available = host.inputSize();

        // 1. header accumulation
        const size_t header_len = handler->headerSize();
        if (available < header_len) {
            host.ensureInputCapacity(header_len);
            return CustomMessageResult::NEED_MORE_BYTES;
        }

        // 2. payload size derived from the (now-complete) header
        const size_t payload_len = handler->payloadSize(host.input());
        const size_t total = header_len + payload_len;
        host.ensureInputCapacity(total);   // may realloc — re-fetch base below
        if (host.inputSize() < total) {
            return CustomMessageResult::NEED_MORE_BYTES;
        }

        // 3. dispatch by mode
        const RequestProcessingMode mode = handler->classifyMode(host.input());

        if (mode == RequestProcessingMode::READ_STREAM ||
            mode == RequestProcessingMode::WRITE_STREAM) {
            const char* base = host.input();
            CustomProtocolRequestImpl req(base, header_len, base + header_len, payload_len);
            handler->processWithoutResponse(host.connection(), req);
            host.consumeInput(total);
            return CustomMessageResult::DISPATCHED_FAST_NO_RESPONSE;
        }

        // FAST and SLOW share the same body; SLOW just defers it to the worker.
        // The message bytes stay valid until the call runs because the host does
        // not recv() (and thus never grows/shifts the buffer) while a SLOW
        // dispatch holds the socket out of the listener.
        auto call = [&host, handler, header_len, payload_len, total]() {
            const char* base = host.input();
            CustomProtocolRequestImpl req(base, header_len, base + header_len, payload_len);
            size_t produced = 0;
            CustomProtocolResponseImpl resp(host.output(), host.outputCapacity(), &produced);
            handler->process(host.connection(), req, resp);
            host.setOutputSize(produced);
            host.consumeInput(total);
        };

        if (mode == RequestProcessingMode::FAST) {
            call();
            return CustomMessageResult::DISPATCHED_FAST;
        }

        // SLOW — host enforces removeEvent-before-queue and marks itself detached.
        host.dispatchSlow(call, listener);
        return CustomMessageResult::DISPATCHED_SLOW;
    }

    ConnectionState CustomPhase::mapResultToNextState(CustomMessageResult r)
    {
        switch (r) {
        case CustomMessageResult::NEED_MORE_BYTES:
            return ConnectionState::ReceivingCustomMessage;
        case CustomMessageResult::DISPATCHED_FAST:
        case CustomMessageResult::DISPATCHED_SLOW:
            return ConnectionState::SendingCustomResponse;
        case CustomMessageResult::DISPATCHED_FAST_NO_RESPONSE:
        default:
            return ConnectionState::WaitingForNextCustomMessage;
        }
    }

    ConnectionState CustomPhase::onSendComplete(PhaseHost& host)
    {
        (void)host;
        return ConnectionState::WaitingForNextCustomMessage;
    }
}
