#if defined(_WIN32)
#include "SocketEvent.hpp"
#include <algorithm>
#include <ws2tcpip.h>
using namespace Bn3Monkey;

void SocketEventListener::open(BaseSocket& sock, SocketEventType eventType)
{
    _handle.fd = sock.descriptor();
    switch(eventType)
    {
        case SocketEventType::ACCEPT:
            {
                _handle.events = POLLIN;
            }
            break;
        case SocketEventType::CONNECT:
            {
                _handle.events = POLLIN | POLLOUT;
            }
            break;
        case SocketEventType::READ:
            {
                _handle.events = POLLIN;
            }
            break;
        case SocketEventType::WRITE:
            {
                _handle.events = POLLOUT;
            }
            break;
        case SocketEventType::READ_WRITE:
            {
                _handle.events = POLLIN | POLLOUT;
            }
            break;
        default:
            break;
    }
}
NetworkResult SocketEventListener::wait(uint32_t timeout_ms)
{
    NetworkResult res {NetworkResultCode::SUCCESS};
    int ret = WSAPoll(&_handle, 1, timeout_ms);
    if (ret == 0)
    {
        res = NetworkResult(NetworkResultCode::SOCKET_TIMEOUT);
    }
    else if (ret <0) {
        res = NetworkResult(NetworkResultCode::SOCKET_EVENT_ERROR);
    }
    else {
        if (_handle.revents & POLLERR) {
            int error{ 0 };
            socklen_t len = sizeof(error);
            getsockopt(_handle.fd, SOL_SOCKET, SO_ERROR, (char*) & error, &len);
            if (error == 0)
            {
                res = NetworkResult(NetworkResultCode::SUCCESS);
            }
            else {
                res = NetworkResult(NetworkResultCode::UNKNOWN_ERROR);
            }
        }
        else if (_handle.revents & POLLHUP) {
            res = NetworkResult(NetworkResultCode::SOCKET_CLOSED);
        }
    }
    return res;
}


static short eventTypeToPollEvents(SocketEventType t)
{
    switch (t)
    {
        case SocketEventType::ACCEPT:     return POLLIN;
        case SocketEventType::CONNECT:    return POLLIN | POLLOUT;
        case SocketEventType::READ:       return POLLIN;
        case SocketEventType::WRITE:      return POLLOUT;
        case SocketEventType::READ_WRITE: return POLLIN | POLLOUT;
        default:                          return 0;
    }
}

// Loopback TCP socketpair. Windows has no eventfd, so we use the smallest
// self-pipe that WSAPoll can watch. The read end carries an always-armed
// POLLIN slot in the listener; writing 1+ bytes to the write end fires it.
static bool createWakeupPair(SOCKET& read_end, SOCKET& write_end)
{
    read_end = INVALID_SOCKET;
    write_end = INVALID_SOCKET;

    SOCKET acceptor = ::socket(AF_INET, SOCK_STREAM, 0);
    if (acceptor == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(acceptor, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(acceptor);
        return false;
    }
    int addrlen = sizeof(addr);
    if (::getsockname(acceptor, reinterpret_cast<sockaddr*>(&addr), &addrlen) == SOCKET_ERROR) {
        ::closesocket(acceptor);
        return false;
    }
    if (::listen(acceptor, 1) == SOCKET_ERROR) {
        ::closesocket(acceptor);
        return false;
    }

    write_end = ::socket(AF_INET, SOCK_STREAM, 0);
    if (write_end == INVALID_SOCKET) {
        ::closesocket(acceptor);
        return false;
    }
    if (::connect(write_end, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(acceptor);
        ::closesocket(write_end);
        write_end = INVALID_SOCKET;
        return false;
    }
    read_end = ::accept(acceptor, nullptr, nullptr);
    ::closesocket(acceptor);
    if (read_end == INVALID_SOCKET) {
        ::closesocket(write_end);
        write_end = INVALID_SOCKET;
        return false;
    }

    u_long nb = 1;
    ::ioctlsocket(read_end,  FIONBIO, &nb);
    ::ioctlsocket(write_end, FIONBIO, &nb);
    int one = 1;
    ::setsockopt(write_end, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&one), sizeof(one));
    return true;
}

static void drainWakeupSocket(SOCKET s)
{
    char buf[256];
    while (::recv(s, buf, sizeof(buf), 0) > 0) {}
}

NetworkResult SocketMultiEventListener::open()
{
    _handle.reserve(16);
    _contexts.reserve(16);

    if (!createWakeupPair(_wakeup_read, _wakeup_write)) {
        return NetworkResult(NetworkResultCode::SOCKET_EVENT_OBJECT_NOT_CREATED);
    }

    pollfd wakeup_pfd{};
    wakeup_pfd.fd = _wakeup_read;
    wakeup_pfd.events = POLLIN;

    std::lock_guard<std::mutex> lock(_mtx);
    _handle.push_back(wakeup_pfd);
    _contexts.push_back(nullptr);
    return NetworkResult();
}

void SocketMultiEventListener::close()
{
    std::lock_guard<std::mutex> lock(_mtx);
    if (_wakeup_read != INVALID_SOCKET)  {
        ::closesocket(_wakeup_read);
        _wakeup_read = INVALID_SOCKET;
    }
    if (_wakeup_write != INVALID_SOCKET) {
        ::closesocket(_wakeup_write);
        _wakeup_write = INVALID_SOCKET;
    }
    _handle.clear();
    _contexts.clear();
}

void SocketMultiEventListener::wake()
{
    SOCKET w;
    {
        std::lock_guard<std::mutex> lock(_mtx);
        w = _wakeup_write;
    }
    if (w == INVALID_SOCKET) return;
    char b = 1;
    ::send(w, &b, 1, 0);
}

NetworkResult SocketMultiEventListener::addEvent(SocketEventContext* context, SocketEventType eventType)
{
    pollfd fd{};
    fd.fd = context->fd;
    fd.events = eventTypeToPollEvents(eventType);
    if (eventType == SocketEventType::ACCEPT) {
        _server_socket = context->fd;
    }

    {
        std::lock_guard<std::mutex> lock(_mtx);
        _handle.push_back(fd);
        _contexts.push_back(context);
    }
    wake();
    return NetworkResult();
}

NetworkResult SocketMultiEventListener::modifyEvent(SocketEventContext* context, SocketEventType eventType)
{
    short new_events = eventTypeToPollEvents(eventType);
    {
        std::lock_guard<std::mutex> lock(_mtx);
        bool found = false;
        for (size_t i = 0; i < _handle.size(); ++i) {
            if (_handle[i].fd == context->fd && _contexts[i] == context) {
                _handle[i].events = new_events;
                _handle[i].revents = 0;
                found = true;
                break;
            }
        }
        if (!found) {
            // Not registered yet: behave like addEvent so the caller does not
            // need to track register state.
            pollfd fd{};
            fd.fd = context->fd;
            fd.events = new_events;
            _handle.push_back(fd);
            _contexts.push_back(context);
        }
    }
    wake();
    return NetworkResult();
}

NetworkResult SocketMultiEventListener::removeEvent(SocketEventContext* context)
{
    {
        std::lock_guard<std::mutex> lock(_mtx);
        // Match on both fd and pointer to preserve the slot-0 wakeup
        // sentinel even if a stale fd happens to collide.
        for (size_t i = 0; i < _handle.size(); ++i) {
            if (_handle[i].fd == context->fd && _contexts[i] == context) {
                _handle.erase(_handle.begin() + i);
                _contexts.erase(_contexts.begin() + i);
                break;
            }
        }
    }
    wake();
    return NetworkResult();
}

SocketEventResult SocketMultiEventListener::wait(uint32_t timeout_ms)
{
    SocketEventResult res;

    std::vector<SocketEventContext*> contexts;
    std::vector<pollfd> handle;
    SOCKET wakeup_read;

    {
        std::lock_guard<std::mutex> lock(_mtx);
        contexts = _contexts;
        handle = _handle;
        wakeup_read = _wakeup_read;
    }


    int ret = WSAPoll(handle.data(), static_cast<ULONG>(handle.size()), timeout_ms);
    if (ret == 0)
    {
        res.result = NetworkResult(NetworkResultCode::SOCKET_TIMEOUT);
    }
    else if (ret <0) {
        res.result = NetworkResult(NetworkResultCode::SOCKET_EVENT_ERROR);
    }
    else {
        res.contexts.reserve(ret);
        for (size_t i = 0; i < handle.size(); i++)
        {
            auto& event = handle[i];
            auto& event_type = event.revents;
            if (event_type == 0)
                continue;

            // Wakeup slot: drain bytes and skip dispatch. The wake itself is
            // the signal; whoever called wake() owns whatever state change
            // motivated it.
            if (contexts[i] == nullptr) {
                drainWakeupSocket(wakeup_read);
                continue;
            }

            SocketEventContext* context = contexts[i];

            if (event_type & POLLERR || event_type & POLLHUP || event_type & POLLNVAL)
            {
                // POLLNVAL: fd no longer valid (closed under us). Treat as
                // disconnect so the cleanup path runs and the fd doesn't keep
                // firing the same revents on every subsequent WSAPoll().
                context->type = SocketEventType::DISCONNECTED;
            }
            else if (_server_socket == context->fd && event_type & POLLIN)
            {
                context->type = SocketEventType::ACCEPT;
            }
            else if (event_type & POLLIN)
            {
                context->type = SocketEventType::READ;
            }
            else if (event_type & POLLOUT)
            {
                context->type = SocketEventType::WRITE;
            }

            res.contexts.push_back(context);
        }

    }
    return res;
}
#endif // _WIN32
