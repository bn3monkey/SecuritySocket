#include "ClientConnection.hpp"

#include "NetworkResult.hpp"

#include <cstring>

using namespace Bn3Monkey;

namespace
{
    // recv chunk granularity; the buffer grows by at least this when it runs low.
    constexpr size_t kRecvChunk = 16 * 1024;

    // "The socket had nothing to give / could take nothing more" — retry on the
    // next readiness event rather than tearing the connection down.
    //
    // Both codes must be tested. createResult() maps EWOULDBLOCK / EAGAIN /
    // WSAEWOULDBLOCK to SOCKET_CONNECTION_NEED_TO_BE_BLOCKED, *not* to
    // SOCKET_TIMEOUT (which comes from an SO_RCVTIMEO/SO_SNDTIMEO lapse), and
    // createTLSResult() maps SSL_ERROR_WANT_READ / WANT_WRITE to the same code.
    inline bool isWouldBlock(NetworkResultCode code)
    {
        return code == NetworkResultCode::SOCKET_TIMEOUT
            || code == NetworkResultCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED;
    }
}

ClientConnectionImpl::ClientConnectionImpl(ServerActiveSocketContainer&& container,
                                           RequestHandler&               handler,
                                           HttpRouterImpl*               router,
                                           CustomProtocolRequestHandler* custom,
                                           size_t                        pdu_size,
                                           size_t                        max_http_request_body_size)
    : _container(std::move(container)),
      _handler(handler),
      _router(router),
      _custom(custom),
      _max_http_request_body_size(max_http_request_body_size),
      _pdu_size(pdu_size ? pdu_size : kRecvChunk),
      _input(pdu_size ? pdu_size : kRecvChunk),
      _output(pdu_size ? pdu_size : kRecvChunk)
{
    _socket = _container.get();
    _is_secure = _socket->isTls();
    fd = _socket->descriptor();   // SocketEventContext::fd — listener key

    if (_custom && _custom->supportWebSocket()) {
        _ws_pattern = _custom->webSocketConfig().pattern;
    }
}

ClientConnectionImpl::~ClientConnectionImpl()
{
    if (_worker_thread.joinable()) {
        {
            std::lock_guard<std::mutex> lock(_task_mtx);
            _worker_should_stop = true;
        }
        _task_cv.notify_one();
        _worker_thread.join();
    }
}

void ClientConnectionImpl::onAccept()
{
    const bool has_http   = _router != nullptr;
    const bool has_custom = _custom != nullptr;

    // The state a plaintext connection starts in, decided by handler capability.
    if (has_http && has_custom) {
        _post_handshake_state = ConnectionState::Sniffing;
    } else if (has_http) {
        _post_handshake_state = ConnectionState::ReceivingHttpRequest;
    } else {
        _post_handshake_state = ConnectionState::ReceivingCustomMessage;
    }

    // A TLS connection waits in the antechamber until SSL_accept() completes, then
    // enters that same state. Plaintext skips the antechamber entirely, so its code
    // path below is byte-for-byte what it was before TLS existed.
    _state = _socket->isTls() ? ConnectionState::TlsHandshaking
                              : _post_handshake_state;

    _listener_event = SocketEventType::READ;   // server registered READ on accept
    _detached = false;
    _tls_want = TlsWant::NONE;

    // Plaintext: the server fires onConnected() right after this returns, so the
    // connection counts as announced from the outset. TLS: nothing is announced
    // until the handshake completes (driveHandshake sets this), which is what
    // keeps onConnected/onDisconnected paired for a handshake that fails.
    _connected_notified = !_socket->isTls();
    _input.clear();
    _output.clear();
    _output_fully_sent = false;
    _is_websocket = false;
    _closed = false;

    // Clear phase-local accumulation — this object is reused from a pool, so a
    // previous connection's HTTP header index / WS reassembly / stream
    // bookkeeping must not leak in. (Sniff phase is stateless.)
    _http_phase.reset();
    _websocket_phase.reset();
    _custom_phase.reset();
}

void ClientConnectionImpl::closeSocket()
{
    if (_closed) return;
    _socket->close();
    _closed = true;
}

// ── socket I/O ───────────────────────────────────────────────────────────────

int ClientConnectionImpl::recvChunk()
{
    if (_input.empty()) _input.clear();          // fully consumed → reset to offset 0
    if (!_input.reserve(kRecvChunk)) return -1;   // OOM → treat as fatal

    auto r = _socket->read(_input.tail(), _input.remaining());
    // Under TLS a read can stall wanting *write*; mirror the direction so
    // armListener() overrides the one the state implies. Plaintext reports NONE.
    _tls_want = _socket->ioWant();
    const NetworkResultCode code = r.code();
    if (code == NetworkResultCode::SOCKET_CLOSED) return -1;
    if (isWouldBlock(code)) return 0;
    const int32_t n = r.bytes();
    if (n < 0) return -1;    // TLS protocol error / fatal alert
    if (n == 0) return 0;
    _input.fill(static_cast<size_t>(n));
    return n;
}

bool ClientConnectionImpl::flushOutput()
{
    _output_fully_sent = false;
    auto r = _socket->write(_output.head(), _output.pending());
    _tls_want = _socket->ioWant();   // a write can stall wanting *read* (see above)
    const NetworkResultCode code = r.code();
    if (code == NetworkResultCode::SOCKET_CLOSED) return false;
    // The send buffer is full. Keep the unsent bytes staged and stay in the
    // write state; the next POLLOUT retries. This MUST be checked before the
    // `n < 0` fatal test below, because a would-block carries bytes() == -1.
    if (isWouldBlock(code)) return true;
    const int32_t n = r.bytes();
    if (n < 0) return false;
    _output.drain(static_cast<size_t>(n));
    _output_fully_sent = _output.empty();
    return true;
}

// ── listener / phase routing ─────────────────────────────────────────────────

ConnectionPhase* ClientConnectionImpl::phaseForState(ConnectionState s)
{
    switch (s) {
    case ConnectionState::Sniffing:
        return &_sniff;
    case ConnectionState::ReceivingCustomMessage:
    case ConnectionState::SendingCustomResponse:
    case ConnectionState::WaitingForNextCustomMessage:
    case ConnectionState::ReceivingCustomStream:
    case ConnectionState::SendingCustomStream:
        return &_custom_phase;
    case ConnectionState::ReceivingHttpRequest:
    case ConnectionState::SendingHttpResponse:
    case ConnectionState::WaitingForNextHttpRequest:
        return &_http_phase;
    case ConnectionState::SendingHandshakeResponse:
    case ConnectionState::ReceivingWebSocketFrame:
    case ConnectionState::SendingWebSocketResponse:
    case ConnectionState::WaitingForNextWebSocketMessage:
    case ConnectionState::ReceivingWebSocketStream:
    case ConnectionState::SendingWebSocketStream:
        // The HTTP Upgrade flips state to SendingHandshakeResponse, so the WS
        // phase's onSendComplete runs once the host flushes the 101 — the group
        // switch happens implicitly through this state->phase mapping.
        return &_websocket_phase;
    default:
        // Lifecycle states (Closing/Closed) are host-handled.
        return nullptr;
    }
}

void ClientConnectionImpl::armListener(SocketMultiEventListener& listener)
{
    // A TLS stall on the opposite direction outranks the state: SSL_read() that
    // returned WANT_WRITE will never make progress on a READ event, and with a
    // level-triggered epoll (no EPOLLET here) an unread-byte-less socket never
    // fires READ again — the connection would hang forever.
    SocketEventType desired;
    if (_tls_want == TlsWant::READ)       desired = SocketEventType::READ;
    else if (_tls_want == TlsWant::WRITE) desired = SocketEventType::WRITE;
    else desired = isWriteState(_state) ? SocketEventType::WRITE : SocketEventType::READ;

    armListener(listener, desired);
}

void ClientConnectionImpl::armListener(SocketMultiEventListener& listener,
                                       SocketEventType desired)
{
    if (desired != _listener_event) {
        listener.modifyEvent(this, desired);
        _listener_event = desired;
    }
}

void ClientConnectionImpl::dispatchSlow(std::function<void()> call,
                                        SocketMultiEventListener& listener)
{
    // INVARIANT (state-machine.html §8): remove the socket from the listener
    // BEFORE the worker can run, so a peer's extra bytes can't race the buffer.
    listener.removeEvent(this);
    _detached = true;
    queueToWorker(std::move(call), listener);
}

// ── event entry ──────────────────────────────────────────────────────────────

ClientConnectionImpl::Disposition
ClientConnectionImpl::handleEvent(SocketEventType ev, SocketMultiEventListener& listener)
{
    if (ev == SocketEventType::DISCONNECTED) return Disposition::CLOSE;

    // Intercepted before any phase routing. TlsHandshaking is a Lifecycle state:
    // phaseForState() returns null for it and driveRead() would close on that.
    // Note this ignores `ev` — the handshake may have been waiting on either
    // direction, and SSL_accept() itself decides what it needs next.
    if (_state == ConnectionState::TlsHandshaking) return driveHandshake(listener);

    if (ev == SocketEventType::WRITE)        return onWriteEvent(listener);

    // READ. Loop rather than read once: SSL_read() decrypts a whole TLS record,
    // and if _input could not take all of it the remainder sits inside the SSL
    // object. The TCP socket then has no unread bytes, so a level-triggered epoll
    // never fires again — those bytes must be drained here or they are lost.
    // hasBufferedInput() is always false for plaintext, so this runs exactly once.
    for (;;) {
        const int n = recvChunk();
        if (n < 0)  return Disposition::CLOSE;
        if (n == 0) {                       // would-block
            armListener(listener);          // _tls_want may have flipped direction
            return Disposition::KEEP;
        }

        const Disposition d = driveRead(listener);
        if (d == Disposition::CLOSE) return d;
        if (_detached)               return d;   // handed to the SLOW worker
        if (isWriteState(_state))    return d;   // now waiting to send a response
        if (!_socket->hasBufferedInput()) return d;
    }
}

ClientConnectionImpl::Disposition
ClientConnectionImpl::driveHandshake(SocketMultiEventListener& listener)
{
    switch (_socket->handshake())
    {
    case TLSHandshakeState::WANT_READ:
        armListener(listener, SocketEventType::READ);
        return Disposition::KEEP;

    case TLSHandshakeState::WANT_WRITE:
        armListener(listener, SocketEventType::WRITE);
        return Disposition::KEEP;

    case TLSHandshakeState::FAILED:
        // onConnected() never fired, so the server must not fire onDisconnected()
        // either — it is gated on connectedNotified().
        return Disposition::CLOSE;

    case TLSHandshakeState::DONE:
        break;
    }

    // Leave the antechamber. From here the connection is indistinguishable from a
    // plaintext one: the same phases run over the same staging buffers, and only
    // recvChunk()/flushOutput() know that the bytes pass through SSL_read/SSL_write.
    _state = _post_handshake_state;
    _handler.onConnected(*this);
    _connected_notified = true;

    armListener(listener, SocketEventType::READ);

    // The client may have pipelined application data into the same TCP segment as
    // its final handshake flight; SSL_accept() has already decrypted and buffered
    // it, and epoll will not re-fire for it.
    if (_socket->hasBufferedInput()) {
        const int n = recvChunk();
        if (n < 0) return Disposition::CLOSE;
        if (n > 0) return driveRead(listener);
    }
    return Disposition::KEEP;
}

ClientConnectionImpl::Disposition
ClientConnectionImpl::driveRead(SocketMultiEventListener& listener)
{
    for (;;) {
        ConnectionPhase* p = phaseForState(_state);
        if (!p) return Disposition::CLOSE;

        const ConnectionState prev = _state;
        const size_t before = _input.pending();

        _state = p->onReadable(*this, listener);

        if (_detached) { _detached = false; return Disposition::KEEP; }  // SLOW
        if (_state == ConnectionState::Closed) return Disposition::CLOSE;

        if (isWriteState(_state)) {       // Sending* or Closing (output filled)
            armListener(listener);
            return Disposition::KEEP;
        }

        // read-state: re-drive only if we made progress and bytes remain
        const bool state_changed = (_state != prev);
        const bool consumed      = (_input.pending() < before);
        if (_input.pending() > 0 && (state_changed || consumed)) continue;

        armListener(listener);
        return Disposition::KEEP;
    }
}

ClientConnectionImpl::Disposition
ClientConnectionImpl::onWriteEvent(SocketMultiEventListener& listener)
{
    if (!flushOutput()) return Disposition::CLOSE;
    if (!_output_fully_sent) {
        // Partial send; stay in the write state. Re-arm anyway: under TLS the
        // write may have stalled wanting *read*, and staying armed for WRITE
        // would spin (or hang) instead of retrying when the peer's bytes arrive.
        armListener(listener);
        return Disposition::KEEP;
    }

    if (_state == ConnectionState::Closing) return Disposition::CLOSE;

    ConnectionPhase* p = phaseForState(_state);
    if (!p) return Disposition::CLOSE;

    _state = p->onSendComplete(*this);

    if (_state == ConnectionState::Closed) return Disposition::CLOSE;
    if (isWriteState(_state)) { armListener(listener); return Disposition::KEEP; }

    // read-state: re-arm READ, then drain any pipelined bytes immediately.
    armListener(listener);
    if (_input.pending() > 0) return driveRead(listener);
    return Disposition::KEEP;
}

// ── SLOW worker ──────────────────────────────────────────────────────────────

void ClientConnectionImpl::queueToWorker(std::function<void()> call,
                                         SocketMultiEventListener& listener)
{
    if (!_worker_thread.joinable()) {
        _worker_thread = std::thread(&ClientConnectionImpl::workerLoop, this);
    }
    {
        std::lock_guard<std::mutex> lock(_task_mtx);
        _pending_task.listener = &listener;
        _pending_task.call     = std::move(call);
    }
    _task_cv.notify_one();
}

void ClientConnectionImpl::workerLoop()
{
    for (;;) {
        SocketMultiEventListener* listener = nullptr;
        std::function<void()>     call;
        {
            std::unique_lock<std::mutex> lock(_task_mtx);
            _task_cv.wait(lock, [this] {
                return _pending_task.listener != nullptr || _worker_should_stop;
            });
            if (_worker_should_stop) return;
            listener = _pending_task.listener;
            call     = std::move(_pending_task.call);
            _pending_task.listener = nullptr;   // slot empty
        }

        call();   // handler + serialize + consume (heavy part)

        // Re-arm WRITE. Keep _listener_event consistent so the post-send
        // armListener() on the main thread correctly modifies back to READ.
        // The listener's lock + internal wakeup establish happens-before with
        // the main thread, which doesn't touch this connection while detached.
        _listener_event = SocketEventType::WRITE;
        listener->addEvent(this, SocketEventType::WRITE);
    }
}
