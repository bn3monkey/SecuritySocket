#include "WebSocketPhase.hpp"

#include "../../SecuritySocket.hpp"              // CustomProtocolRequestHandler
#include "../custom/CustomProtocolRequest.hpp"   // CustomProtocolRequestImpl
#include "../custom/CustomProtocolResponse.hpp"  // CustomProtocolResponseImpl
#include "../protocol/WsFrameCodec.hpp"          // WsFrameCodec, WsOpcode, WsFrameView

#include <cstring>

namespace Bn3Monkey
{
    using StreamProgress = CustomProtocolRequestHandler::StreamProgress;

    namespace
    {
        // RFC 6455 close status codes used here (mileston_http.md §2-8).
        constexpr uint16_t kCloseProtocolError = 1002;
        constexpr uint16_t kCloseUnsupported   = 1003;  // TEXT frame
        constexpr uint16_t kCloseServerError   = 1011;

        // Fill the output buffer with an unmasked Close frame and return CLOSING.
        void emitClose(StagingBuffer& out, uint16_t code)
        {
            out.clear();
            out.fill(WsFrameCodec::encodeClose(
                code, static_cast<char*>(out.data()), out.capacity()));
        }
    }

    void WebSocketPhase::reset()
    {
        _fragment.clear();
        _message_started = false;
        _resp_scratch.clear();
        _pong_count = 0;
        _read_last = false;
        _resume_pending = false;
        _resume_after_control = ConnectionState::WaitingForNextWebSocketMessage;
    }

    ConnectionState WebSocketPhase::onReadable(PhaseHost& host,
                                               SocketMultiEventListener& listener)
    {
        return handle(host, listener);
    }

    ConnectionState WebSocketPhase::handle(PhaseHost& host, SocketMultiEventListener& listener)
    {
        StagingBuffer& in  = host.input();
        StagingBuffer& out = host.output();

        const ConnectionState entry = host.state();
        // Where "keep reading" lands: stay mid-stream if we are streaming.
        const ConnectionState readState =
            (entry == ConnectionState::ReceivingWebSocketStream)
                ? ConnectionState::ReceivingWebSocketStream
                : ConnectionState::ReceivingWebSocketFrame;

        // decode() unmasks the (client->server) payload in place, so it writes
        // through head(); StagingBuffer::head() is a mutable void*.
        WsFrameView view;
        const WsFrameCodec::DecodeResult dr =
            WsFrameCodec::decode(static_cast<char*>(in.head()), in.pending(), view);

        if (dr == WsFrameCodec::DecodeResult::NEED_MORE) {
            return readState;   // host recv reserves
        }
        if (dr == WsFrameCodec::DecodeResult::INVALID) {
            emitClose(out, kCloseProtocolError);
            return ConnectionState::Closing;
        }

        switch (view.opcode) {
        case WsOpcode::TEXT:
            // Custom headers can't be valid UTF-8 → TEXT is never ours (§2-8).
            emitClose(out, kCloseUnsupported);
            in.drain(view.frame_len);
            return ConnectionState::Closing;

        case WsOpcode::PING: {
            out.clear();
            const size_t n = WsFrameCodec::encodePong(
                view.payload, view.payload_len,
                static_cast<char*>(out.data()), out.capacity());
            in.drain(view.frame_len);
            if (n == 0) {   // shouldn't happen (control payload <=125), be safe
                emitClose(out, kCloseServerError);
                return ConnectionState::Closing;
            }
            out.fill(n);
            // Resume the stream after the PONG flushes, if we were mid-stream.
            if (readState == ConnectionState::ReceivingWebSocketStream) {
                _resume_pending = true;
                _resume_after_control = ConnectionState::ReceivingWebSocketStream;
            }
            return ConnectionState::SendingWebSocketResponse;
        }

        case WsOpcode::PONG:
            ++_pong_count;
            in.drain(view.frame_len);
            return readState;

        case WsOpcode::CLOSE:
            out.clear();
            out.fill(WsFrameCodec::encodeCloseEcho(
                view.payload, view.payload_len,
                static_cast<char*>(out.data()), out.capacity()));
            in.drain(view.frame_len);
            return ConnectionState::Closing;

        case WsOpcode::CONTINUATION:
            if (!_message_started) {
                // Continuation with no initiating data frame — protocol error.
                emitClose(out, kCloseProtocolError);
                in.drain(view.frame_len);
                return ConnectionState::Closing;
            }
            // fallthrough to data accumulation
        case WsOpcode::BINARY: {
            if (view.opcode == WsOpcode::BINARY) _message_started = true;
            // Accumulate this frame's (unmasked) payload, then drop the frame
            // from the input buffer. The message lives in _fragment so it
            // survives a SLOW dispatch even as the buffer shifts.
            _fragment.insert(_fragment.end(),
                             view.payload, view.payload + view.payload_len);
            in.drain(view.frame_len);

            if (!view.fin) {
                return readState;   // continuation pending
            }
            return dispatchComplete(host, listener, entry);
        }

        default:
            emitClose(out, kCloseProtocolError);
            in.drain(view.frame_len);
            return ConnectionState::Closing;
        }
    }

    ConnectionState WebSocketPhase::dispatchComplete(PhaseHost& host,
                                                     SocketMultiEventListener& listener,
                                                     ConnectionState entry)
    {
        StagingBuffer& out = host.output();
        CustomProtocolRequestHandler* handler = host.customHandler();
        // WS is only reached for a Custom handler that advertised a WS pattern,
        // so handler is non-null here.
        const char*  fmsg     = _fragment.data();
        const size_t fmsg_len = _fragment.size();

        const size_t header_len = handler->headerSize();
        if (fmsg_len < header_len) {
            // A complete WS message that doesn't even cover the Custom header is
            // a framing/contract violation, not "need more" (FIN already seen).
            emitClose(out, kCloseProtocolError);
            _fragment.clear(); _message_started = false;
            return ConnectionState::Closing;
        }
        const size_t payload_len = handler->payloadSize(fmsg);
        if (fmsg_len < header_len + payload_len) {
            emitClose(out, kCloseProtocolError);
            _fragment.clear(); _message_started = false;
            return ConnectionState::Closing;
        }

        // ── mid WRITE_STREAM: this reassembled message is a chunk ──
        if (entry == ConnectionState::ReceivingWebSocketStream) {
            CustomProtocolRequestImpl req(fmsg, header_len, fmsg + header_len, payload_len);
            const StreamProgress r = handler->onWriteStreamData(host.connection(), req);
            _fragment.clear(); _message_started = false;
            if (r == StreamProgress::COMPLETE) {
                return ConnectionState::WaitingForNextWebSocketMessage;
            }
            if (r == StreamProgress::ABORT) {
                emitClose(out, kCloseServerError);
                return ConnectionState::Closing;
            }
            return ConnectionState::ReceivingWebSocketStream;   // CONTINUE
        }

        const RequestProcessingMode mode = handler->classifyMode(fmsg);

        // ── stream entry ──
        if (mode == RequestProcessingMode::WRITE_STREAM) {
            CustomProtocolRequestImpl req(fmsg, header_len, fmsg + header_len, payload_len);
            handler->onWriteStreamBegin(host.connection(), req);
            _fragment.clear(); _message_started = false;
            return ConnectionState::ReceivingWebSocketStream;
        }
        if (mode == RequestProcessingMode::READ_STREAM) {
            CustomProtocolRequestImpl req(fmsg, header_len, fmsg + header_len, payload_len);
            handler->onReadStreamBegin(host.connection(), req);
            _fragment.clear(); _message_started = false;
            return produceReadChunk(host);   // first chunk -> SendingWebSocketStream
        }

        // ── FAST / SLOW: one request, one response ──
        // The handler writes its response into _resp_scratch; we then frame it
        // (BINARY, FIN=1, unmasked) into the host output buffer. _fragment +
        // _resp_scratch are phase members, so a SLOW worker can run this safely
        // after the socket is detached.
        auto call = [this, &host, handler, header_len, payload_len]() {
            StagingBuffer& out = host.output();
            const char* fmsg = _fragment.data();
            CustomProtocolRequestImpl req(fmsg, header_len, fmsg + header_len, payload_len);

            if (_resp_scratch.size() < out.capacity()) {
                _resp_scratch.resize(out.capacity());
            }
            size_t produced = 0;
            CustomProtocolResponseImpl resp(_resp_scratch.data(),
                                            _resp_scratch.size(), &produced);
            handler->process(host.connection(), req, resp);

            out.clear();
            out.reserve(produced + WsFrameCodec::MAX_SERVER_HEADER);
            const size_t framed = WsFrameCodec::encode(
                WsOpcode::BINARY, _resp_scratch.data(), produced,
                /*fin*/ true, static_cast<char*>(out.data()), out.capacity());
            out.fill(framed);

            _fragment.clear();
            _message_started = false;
        };

        if (mode == RequestProcessingMode::SLOW) {
            host.dispatchSlow(call, listener);
            return ConnectionState::SendingWebSocketResponse;
        }
        call();
        return ConnectionState::SendingWebSocketResponse;
    }

    ConnectionState WebSocketPhase::produceReadChunk(PhaseHost& host)
    {
        StagingBuffer& out = host.output();
        CustomProtocolRequestHandler* handler = host.customHandler();

        if (_resp_scratch.size() < out.capacity()) {
            _resp_scratch.resize(out.capacity());
        }
        size_t produced = 0;
        CustomProtocolResponseImpl resp(_resp_scratch.data(),
                                        _resp_scratch.size(), &produced);
        const StreamProgress r = handler->onReadStreamData(host.connection(), resp);
        if (r == StreamProgress::ABORT) {
            emitClose(out, kCloseServerError);
            return ConnectionState::Closing;
        }

        out.clear();
        out.reserve(produced + WsFrameCodec::MAX_SERVER_HEADER);
        const size_t framed = WsFrameCodec::encode(
            WsOpcode::BINARY, _resp_scratch.data(), produced,
            /*fin*/ true, static_cast<char*>(out.data()), out.capacity());
        out.fill(framed);

        _read_last = (r == StreamProgress::COMPLETE);
        return ConnectionState::SendingWebSocketStream;
    }

    ConnectionState WebSocketPhase::onSendComplete(PhaseHost& host)
    {
        // A control frame (PONG/CLOSE echo) was answered mid-stream: resume the
        // stream state the host was in before it (milestone_write_stream.md §4).
        if (_resume_pending) {
            _resume_pending = false;
            return _resume_after_control;
        }

        // READ_STREAM: the chunk we just flushed has drained. If it was the last
        // one, the stream is done; otherwise produce + frame the next chunk and
        // keep sending (the host re-arms WRITE and flushes it).
        if (host.state() == ConnectionState::SendingWebSocketStream) {
            if (_read_last) {
                return ConnectionState::WaitingForNextWebSocketMessage;
            }
            return produceReadChunk(host);
        }

        // Covers both the 101 handshake completion (entered from HttpPhase) and
        // a normal frame response: the socket is idle, ready for the next frame.
        return ConnectionState::WaitingForNextWebSocketMessage;
    }
}
