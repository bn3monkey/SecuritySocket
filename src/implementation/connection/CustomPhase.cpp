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
        StagingBuffer& in = host.input();

        // 1. header accumulation
        const size_t header_len = handler->headerSize();
        if (in.pending() < header_len) {
            in.reserve(header_len - in.pending());   // room to receive the rest
            return CustomMessageResult::NEED_MORE_BYTES;
        }

        // 2. payload size derived from the (now-complete) header
        const size_t payload_len = handler->payloadSize(static_cast<const char*>(in.head()));
        const size_t total = header_len + payload_len;
        if (in.pending() < total) {
            in.reserve(total - in.pending());        // room for the full message
            return CustomMessageResult::NEED_MORE_BYTES;
        }

        // 3. dispatch by mode
        const RequestProcessingMode mode = handler->classifyMode(static_cast<const char*>(in.head()));

        // WRITE_STREAM: the client only writes to the server; the server ingests
        // the payload and produces no response. (READ_STREAM — the client reading
        // from the server — needs a response, so it falls through to the FAST body
        // below; its dedicated streaming form is not implemented yet.)
        if (mode == RequestProcessingMode::WRITE_STREAM) {
            const char* base = static_cast<const char*>(in.head());
            CustomProtocolRequestImpl req(base, header_len, base + header_len, payload_len);
            handler->processWithoutResponse(host.connection(), req);
            in.drain(total);
            return CustomMessageResult::DISPATCHED_FAST_NO_RESPONSE;
        }

        // FAST and SLOW share the same body; SLOW just defers it to the worker.
        // The message bytes stay valid until the call runs because the host does
        // not recv() (and thus never reserve()s/compact()s the buffer) while a
        // SLOW dispatch holds the socket out of the listener.
        auto call = [&host, handler, header_len, payload_len, total]() {
            StagingBuffer& in  = host.input();
            StagingBuffer& out = host.output();
            const char* base = static_cast<const char*>(in.head());
            CustomProtocolRequestImpl req(base, header_len, base + header_len, payload_len);
            out.clear();                    // reset previous send state to offset 0
            size_t produced = 0;
            CustomProtocolResponseImpl resp(out.data(), out.capacity(), &produced);
            handler->process(host.connection(), req, resp);
            out.fill(produced);
            in.drain(total);
        };

        // FAST and READ_STREAM run inline and send a response. (READ_STREAM is
        // treated like FAST for now — see note above.)
        if (mode == RequestProcessingMode::FAST ||
            mode == RequestProcessingMode::READ_STREAM) {
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
