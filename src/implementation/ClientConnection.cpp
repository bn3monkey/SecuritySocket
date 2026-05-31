#include "ClientConnection.hpp"

#include "NetworkResult.hpp"

#include <cstring>

using namespace Bn3Monkey;

namespace
{
    // recv chunk granularity; the buffer grows by at least this when it runs low.
    constexpr size_t kRecvChunk = 16 * 1024;
}

ClientConnectionImpl::ClientConnectionImpl(ServerActiveSocketContainer&  container,
                                           RequestHandler&               handler,
                                           HttpRouterImpl*               router,
                                           CustomProtocolRequestHandler* custom,
                                           size_t                        pdu_size,
                                           bool                          is_secure)
    : _container(container),
      _handler(handler),
      _router(router),
      _custom(custom),
      _is_secure(is_secure),
      _pdu_size(pdu_size ? pdu_size : kRecvChunk),
      _input(pdu_size ? pdu_size : kRecvChunk),
      _output(pdu_size ? pdu_size : kRecvChunk)
{
    _socket = _container.get();
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

    if (has_http && has_custom) {
        _state = ConnectionState::Sniffing;
    } else if (has_http) {
        _state = ConnectionState::ReceivingHttpRequest;
    } else {
        _state = ConnectionState::ReceivingCustomMessage;
    }
    _listener_event = SocketEventType::READ;   // server registered READ on accept
    _detached = false;
    _input.clear();
    _output.clear();
    _output_fully_sent = false;
    _is_websocket = false;
    _closed = false;
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
    const NetworkResultCode code = r.code();
    if (code == NetworkResultCode::SOCKET_CLOSED) return -1;
    if (code == NetworkResultCode::SOCKET_TIMEOUT) return 0;   // would-block
    const int32_t n = r.bytes();
    if (n <= 0) return 0;
    _input.fill(static_cast<size_t>(n));
    return n;
}

bool ClientConnectionImpl::flushOutput()
{
    _output_fully_sent = false;
    auto r = _socket->write(_output.head(), _output.pending());
    const NetworkResultCode code = r.code();
    if (code == NetworkResultCode::SOCKET_CLOSED) return false;
    if (code == NetworkResultCode::SOCKET_TIMEOUT) return true;   // would-block; stay
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
        return &_custom_phase;
    default:
        // HTTP / WebSocket groups: not yet wired (Phase 6 later slices).
        return nullptr;
    }
}

void ClientConnectionImpl::armListener(SocketMultiEventListener& listener)
{
    const SocketEventType desired =
        isWriteState(_state) ? SocketEventType::WRITE : SocketEventType::READ;
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
    if (ev == SocketEventType::WRITE)        return onWriteEvent(listener);

    // READ
    const int n = recvChunk();
    if (n < 0) return Disposition::CLOSE;
    if (n == 0) return Disposition::KEEP;
    return driveRead(listener);
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
    if (!_output_fully_sent) return Disposition::KEEP;   // partial; stay WRITE

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
