#if defined(__linux__)
#include "SocketEvent.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>

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
SocketResult SocketEventListener::wait(uint32_t timeout_ms)
{
    SocketResult res {SocketCode::SUCCESS};
    int ret = ::poll(&_handle, 1, timeout_ms);
    if (ret == 0)
    {
        res = SocketResult(SocketCode::SOCKET_TIMEOUT);
    }
    else if (ret <0) {
        res = SocketResult(SocketCode::SOCKET_EVENT_ERROR);
    }
    else {
        if (_handle.revents & POLLERR) {
            int error{ 0 };
            socklen_t len = sizeof(error);
            getsockopt(_handle.fd, SOL_SOCKET, SO_ERROR, (char*) & error, &len);
            if (error == 0)
            {
                res = SocketResult(SocketCode::SUCCESS);
            }
            else {
                res = createResultFromSocketError(error);
            }
        }
        else if (_handle.revents & POLLHUP) {
            res = SocketResult(SocketCode::SOCKET_CLOSED);
        }
    }
    return res;
}


// Level-triggered to match WSAPoll semantics: an fd keeps firing as long as
// the condition holds. Callers that don't want re-trigger should mask out the
// event via modifyEvent.
//
// EPOLLRDHUP is OR'd in for every stream-socket event type so peer FIN
// surfaces as a disconnect. Unlike poll's POLLHUP (which Linux raises on peer
// FIN), epoll's EPOLLHUP fires only on full hangup; EPOLLRDHUP is the flag
// for one-way peer close and must be registered explicitly to be reported.
// The listen socket (ACCEPT) doesn't peer-close, so it's omitted there.
static uint32_t eventTypeToEpollEvents(SocketEventType t)
{
    switch (t)
    {
        case SocketEventType::ACCEPT:     return EPOLLIN;
        case SocketEventType::CONNECT:    return EPOLLIN | EPOLLOUT | EPOLLRDHUP;
        case SocketEventType::READ:       return EPOLLIN | EPOLLRDHUP;
        case SocketEventType::WRITE:      return EPOLLOUT | EPOLLRDHUP;
        case SocketEventType::READ_WRITE: return EPOLLIN | EPOLLOUT | EPOLLRDHUP;
        default:                          return 0;
    }
}

static void drainWakeupEventfd(int fd)
{
    uint64_t v;
    while (::read(fd, &v, sizeof(v)) == static_cast<ssize_t>(sizeof(v))) {}
}

SocketResult SocketMultiEventListener::open()
{
    _epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (_epfd < 0) {
        return SocketResult(SocketCode::SOCKET_EVENT_OBJECT_NOT_CREATED);
    }
    _wakeup_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (_wakeup_fd < 0) {
        ::close(_epfd); _epfd = -1;
        return SocketResult(SocketCode::SOCKET_EVENT_OBJECT_NOT_CREATED);
    }

    // Register the wakeup fd with a nullptr data.ptr sentinel so wait() can
    // detect and drain it without dispatching to user code.
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.ptr = nullptr;
    if (::epoll_ctl(_epfd, EPOLL_CTL_ADD, _wakeup_fd, &ev) < 0) {
        ::close(_wakeup_fd); _wakeup_fd = -1;
        ::close(_epfd); _epfd = -1;
        return SocketResult(SocketCode::SOCKET_EVENT_OBJECT_NOT_CREATED);
    }
    return SocketResult();
}

void SocketMultiEventListener::close()
{
    std::lock_guard<std::mutex> lock(_mtx);
    if (_wakeup_fd >= 0) { ::close(_wakeup_fd); _wakeup_fd = -1; }
    if (_epfd >= 0)      { ::close(_epfd);      _epfd = -1; }
}

void SocketMultiEventListener::wake()
{
    int fd;
    {
        std::lock_guard<std::mutex> lock(_mtx);
        fd = _wakeup_fd;
    }
    if (fd < 0) return;
    uint64_t one = 1;
    ssize_t r = ::write(fd, &one, sizeof(one));
    (void)r;
}

SocketResult SocketMultiEventListener::addEvent(SocketEventContext* context, SocketEventType eventType)
{
    if (eventType == SocketEventType::ACCEPT) {
        _server_socket = context->fd;
    }

    epoll_event ev{};
    ev.events = eventTypeToEpollEvents(eventType);
    ev.data.ptr = context;
    if (::epoll_ctl(_epfd, EPOLL_CTL_ADD, context->fd, &ev) < 0) {
        return SocketResult(SocketCode::SOCKET_EVENT_ERROR);
    }
    // epoll_ctl is kernel-thread-safe and a newly readable/writable fd will
    // wake a blocked epoll_wait on its own. Issue an explicit wake() anyway
    // so shutdown / "no-event-yet" cases also surface promptly.
    wake();
    return SocketResult();
}

SocketResult SocketMultiEventListener::modifyEvent(SocketEventContext* context, SocketEventType eventType)
{
    epoll_event ev{};
    ev.events = eventTypeToEpollEvents(eventType);
    ev.data.ptr = context;
    // MOD on a not-registered fd returns ENOENT; fall back to ADD so callers
    // don't need to track register state.
    if (::epoll_ctl(_epfd, EPOLL_CTL_MOD, context->fd, &ev) < 0) {
        if (errno == ENOENT) {
            if (::epoll_ctl(_epfd, EPOLL_CTL_ADD, context->fd, &ev) < 0) {
                return SocketResult(SocketCode::SOCKET_EVENT_ERROR);
            }
        } else {
            return SocketResult(SocketCode::SOCKET_EVENT_ERROR);
        }
    }
    wake();
    return SocketResult();
}

SocketResult SocketMultiEventListener::removeEvent(SocketEventContext* context)
{
    // EPOLL_CTL_DEL on an already-closed fd returns EBADF; tolerate it since
    // the caller may close the fd before unregistering.
    ::epoll_ctl(_epfd, EPOLL_CTL_DEL, context->fd, nullptr);
    wake();
    return SocketResult();
}

SocketEventResult SocketMultiEventListener::wait(uint32_t timeout_ms)
{
    SocketEventResult res;

    int epfd;
    int wakeup_fd;
    int32_t server_socket;
    {
        std::lock_guard<std::mutex> lock(_mtx);
        epfd = _epfd;
        wakeup_fd = _wakeup_fd;
        server_socket = _server_socket;
    }
    if (epfd < 0) {
        res.result = SocketResult(SocketCode::SOCKET_EVENT_ERROR);
        return res;
    }

    constexpr int kMaxEvents = 64;
    epoll_event events[kMaxEvents];
    int ret = ::epoll_wait(epfd, events, kMaxEvents,
                           static_cast<int>(timeout_ms));
    if (ret == 0) {
        res.result = SocketResult(SocketCode::SOCKET_TIMEOUT);
        return res;
    }
    if (ret < 0) {
        res.result = SocketResult(SocketCode::SOCKET_EVENT_ERROR);
        return res;
    }

    res.contexts.reserve(ret);
    for (int i = 0; i < ret; ++i) {
        auto& ev = events[i];
        auto* context = static_cast<SocketEventContext*>(ev.data.ptr);

        // Wakeup eventfd: drain and skip dispatch. The wake itself is the
        // signal; whoever called wake() owns whatever state change motivated it.
        if (context == nullptr) {
            drainWakeupEventfd(wakeup_fd);
            continue;
        }

        auto event_type = ev.events;
        if (event_type & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
            context->type = SocketEventType::DISCONNECTED;
        }
        else if (server_socket == context->fd && (event_type & EPOLLIN)) {
            context->type = SocketEventType::ACCEPT;
        }
        else if (event_type & EPOLLIN) {
            context->type = SocketEventType::READ;
        }
        else if (event_type & EPOLLOUT) {
            context->type = SocketEventType::WRITE;
        }

        res.contexts.push_back(context);
    }
    return res;
}

#endif // __linux__
