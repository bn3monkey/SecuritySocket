#include "CustomPhase.hpp"

#include "../../SecuritySocket.hpp"                 // CustomProtocolRequestHandler, ClientConnection
#include "../custom/CustomProtocolRequest.hpp"
#include "../custom/CustomProtocolResponse.hpp"

namespace Bn3Monkey
{
    using StreamProgress = CustomProtocolRequestHandler::StreamProgress;

    void CustomPhase::reset()
    {
        _read_last = false;
    }

    ConnectionState CustomPhase::onReadable(PhaseHost& host,
                                            SocketMultiEventListener& listener)
    {
        return handle(host, listener);
    }

    ConnectionState CustomPhase::handle(PhaseHost& host, SocketMultiEventListener& listener)
    {
        CustomProtocolRequestHandler* handler = host.customHandler();
        // The host only routes to this phase when a Custom handler exists.
        StagingBuffer& in = host.input();
        const size_t header_len = handler->headerSize();

        // Local state mirror: host.state() only reflects the value we *return*,
        // so we must track in-call transitions (begin -> stream) ourselves.
        ConnectionState st = host.state();

        for (;;) {
            // ── frame one message: header, then payload ──
            if (in.pending() < header_len) {
                in.reserve(header_len - in.pending());
                return st == ConnectionState::ReceivingCustomStream
                           ? ConnectionState::ReceivingCustomStream
                           : ConnectionState::ReceivingCustomMessage;
            }
            const size_t payload_len =
                handler->payloadSize(static_cast<const char*>(in.head()));
            const size_t total = header_len + payload_len;
            if (in.pending() < total) {
                in.reserve(total - in.pending());
                return st == ConnectionState::ReceivingCustomStream
                           ? ConnectionState::ReceivingCustomStream
                           : ConnectionState::ReceivingCustomMessage;
            }

            const char* base = static_cast<const char*>(in.head());

            // ── mid WRITE_STREAM: every framed message is a chunk ──
            if (st == ConnectionState::ReceivingCustomStream) {
                CustomProtocolRequestImpl req(base, header_len, base + header_len, payload_len);
                const StreamProgress r = handler->onWriteStreamData(host.connection(), req);
                in.drain(total);
                if (r == StreamProgress::COMPLETE) {
                    return ConnectionState::WaitingForNextCustomMessage;
                }
                if (r == StreamProgress::ABORT) {
                    host.output().clear();
                    return ConnectionState::Closing;
                }
                continue;   // CONTINUE: ingest the next chunk
            }

            // ── normal receive: classify the mode ──
            const RequestProcessingMode mode =
                handler->classifyMode(static_cast<const char*>(in.head()));

            if (mode == RequestProcessingMode::WRITE_STREAM) {
                CustomProtocolRequestImpl req(base, header_len, base + header_len, payload_len);
                handler->onWriteStreamBegin(host.connection(), req);
                in.drain(total);
                st = ConnectionState::ReceivingCustomStream;   // enter stream
                continue;
            }

            if (mode == RequestProcessingMode::READ_STREAM) {
                CustomProtocolRequestImpl req(base, header_len, base + header_len, payload_len);
                handler->onReadStreamBegin(host.connection(), req);
                in.drain(total);
                return produceReadChunk(host);   // first chunk -> SendingCustomStream
            }

            // FAST and SLOW share the same body; SLOW just defers it to the
            // worker. The message bytes stay valid until the call runs because
            // the host does not recv() (so never reserve()s/compact()s the
            // buffer) while a SLOW dispatch holds the socket out of the listener.
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

            if (mode == RequestProcessingMode::FAST) {
                call();
                return ConnectionState::SendingCustomResponse;
            }

            // SLOW — host enforces removeEvent-before-queue and marks itself detached.
            host.dispatchSlow(call, listener);
            return ConnectionState::SendingCustomResponse;
        }
    }

    ConnectionState CustomPhase::produceReadChunk(PhaseHost& host)
    {
        CustomProtocolRequestHandler* handler = host.customHandler();
        StagingBuffer& out = host.output();

        out.clear();
        size_t produced = 0;
        CustomProtocolResponseImpl resp(out.data(), out.capacity(), &produced);
        const StreamProgress r = handler->onReadStreamData(host.connection(), resp);
        if (r == StreamProgress::ABORT) {
            out.clear();
            return ConnectionState::Closing;
        }
        out.fill(produced);
        _read_last = (r == StreamProgress::COMPLETE);
        return ConnectionState::SendingCustomStream;
    }

    ConnectionState CustomPhase::onSendComplete(PhaseHost& host)
    {
        // READ_STREAM: the chunk we just flushed has drained. If it was the last
        // one, the stream is done; otherwise produce the next chunk and keep
        // sending (the host re-arms WRITE and flushes it).
        if (host.state() == ConnectionState::SendingCustomStream) {
            if (_read_last) {
                return ConnectionState::WaitingForNextCustomMessage;
            }
            return produceReadChunk(host);
        }
        // FAST / SLOW single response done.
        return ConnectionState::WaitingForNextCustomMessage;
    }
}
