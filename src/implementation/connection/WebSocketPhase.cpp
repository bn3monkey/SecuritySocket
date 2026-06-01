#include "WebSocketPhase.hpp"

#include "../../SecuritySocket.hpp"              // CustomProtocolRequestHandler
#include "../custom/CustomProtocolRequest.hpp"   // CustomProtocolRequestImpl
#include "../custom/CustomProtocolResponse.hpp"  // CustomProtocolResponseImpl
#include "../protocol/WsFrameCodec.hpp"          // WsFrameCodec, WsOpcode, WsFrameView

#include <cstring>

namespace Bn3Monkey
{
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
    }

    ConnectionState WebSocketPhase::onReadable(PhaseHost& host,
                                               SocketMultiEventListener& listener)
    {
        return mapResultToNextState(handle(host, listener));
    }

    WebSocketPhase::WebSocketFrameResult
    WebSocketPhase::handle(PhaseHost& host, SocketMultiEventListener& listener)
    {
        StagingBuffer& in  = host.input();
        StagingBuffer& out = host.output();

        // decode() unmasks the (client->server) payload in place, so it writes
        // through head(); StagingBuffer::head() is a mutable void*.
        WsFrameView view;
        const WsFrameCodec::DecodeResult dr =
            WsFrameCodec::decode(static_cast<char*>(in.head()), in.pending(), view);

        if (dr == WsFrameCodec::DecodeResult::NEED_MORE) {
            return WebSocketFrameResult::NEED_MORE_BYTES;   // host recv reserves
        }
        if (dr == WsFrameCodec::DecodeResult::INVALID) {
            emitClose(out, kCloseProtocolError);
            return WebSocketFrameResult::CLOSING;
        }

        switch (view.opcode) {
        case WsOpcode::TEXT:
            // Custom headers can't be valid UTF-8 → TEXT is never ours (§2-8).
            emitClose(out, kCloseUnsupported);
            in.drain(view.frame_len);
            return WebSocketFrameResult::CLOSING;

        case WsOpcode::PING: {
            out.clear();
            const size_t n = WsFrameCodec::encodePong(
                view.payload, view.payload_len,
                static_cast<char*>(out.data()), out.capacity());
            in.drain(view.frame_len);
            if (n == 0) {   // shouldn't happen (control payload <=125), be safe
                emitClose(out, kCloseServerError);
                return WebSocketFrameResult::CLOSING;
            }
            out.fill(n);
            return WebSocketFrameResult::CONTROL_RESPONSE;
        }

        case WsOpcode::PONG:
            ++_pong_count;
            in.drain(view.frame_len);
            return WebSocketFrameResult::CONTROL_NOOP;

        case WsOpcode::CLOSE:
            out.clear();
            out.fill(WsFrameCodec::encodeCloseEcho(
                view.payload, view.payload_len,
                static_cast<char*>(out.data()), out.capacity()));
            in.drain(view.frame_len);
            return WebSocketFrameResult::CLOSING;

        case WsOpcode::CONTINUATION:
            if (!_message_started) {
                // Continuation with no initiating data frame — protocol error.
                emitClose(out, kCloseProtocolError);
                in.drain(view.frame_len);
                return WebSocketFrameResult::CLOSING;
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
                return WebSocketFrameResult::CONTINUATION_PENDING;
            }
            return dispatchMessage(host, listener);
        }

        default:
            emitClose(out, kCloseProtocolError);
            in.drain(view.frame_len);
            return WebSocketFrameResult::CLOSING;
        }
    }

    WebSocketPhase::WebSocketFrameResult
    WebSocketPhase::dispatchMessage(PhaseHost& host, SocketMultiEventListener& listener)
    {
        StagingBuffer& out = host.output();
        CustomProtocolRequestHandler* handler = host.customHandler();
        // WS is only reached for a Custom handler that advertised a WS pattern,
        // so handler is non-null here.
        const char*  fmsg    = _fragment.data();
        const size_t fmsg_len = _fragment.size();

        const size_t header_len = handler->headerSize();
        if (fmsg_len < header_len) {
            // A complete WS message that doesn't even cover the Custom header is
            // a framing/contract violation, not "need more" (FIN already seen).
            emitClose(out, kCloseProtocolError);
            _fragment.clear(); _message_started = false;
            return WebSocketFrameResult::CLOSING;
        }
        const size_t payload_len = handler->payloadSize(fmsg);
        if (fmsg_len < header_len + payload_len) {
            emitClose(out, kCloseProtocolError);
            _fragment.clear(); _message_started = false;
            return WebSocketFrameResult::CLOSING;
        }

        const RequestProcessingMode mode = handler->classifyMode(fmsg);

        if (mode == RequestProcessingMode::READ_STREAM ||
            mode == RequestProcessingMode::WRITE_STREAM) {
            CustomProtocolRequestImpl req(fmsg, header_len, fmsg + header_len, payload_len);
            handler->processWithoutResponse(host.connection(), req);
            _fragment.clear(); _message_started = false;
            return WebSocketFrameResult::MESSAGE_NO_RESPONSE;
        }

        // FAST / SLOW share this body. The handler writes its response into
        // _resp_scratch; we then frame it (BINARY, FIN=1, unmasked) into the
        // host output buffer. _fragment + _resp_scratch are phase members, so a
        // SLOW worker can run this safely after the socket is detached.
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
            return WebSocketFrameResult::MESSAGE_DISPATCHED_SLOW;
        }
        call();
        return WebSocketFrameResult::MESSAGE_DISPATCHED_FAST;
    }

    ConnectionState WebSocketPhase::mapResultToNextState(WebSocketFrameResult r) const
    {
        switch (r) {
        case WebSocketFrameResult::NEED_MORE_BYTES:
        case WebSocketFrameResult::CONTINUATION_PENDING:
        case WebSocketFrameResult::CONTROL_NOOP:
        case WebSocketFrameResult::MESSAGE_NO_RESPONSE:
            return ConnectionState::ReceivingWebSocketFrame;   // keep reading
        case WebSocketFrameResult::MESSAGE_DISPATCHED_FAST:
        case WebSocketFrameResult::MESSAGE_DISPATCHED_SLOW:
        case WebSocketFrameResult::CONTROL_RESPONSE:
            return ConnectionState::SendingWebSocketResponse;
        case WebSocketFrameResult::CLOSING:
        default:
            return ConnectionState::Closing;
        }
    }

    ConnectionState WebSocketPhase::onSendComplete(PhaseHost& host)
    {
        (void)host;
        // Covers both the 101 handshake completion (entered from HttpPhase) and
        // a normal frame response: the socket is idle, ready for the next frame.
        return ConnectionState::WaitingForNextWebSocketMessage;
    }
}
