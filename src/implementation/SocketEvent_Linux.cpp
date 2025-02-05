#if defined(__linux__)
#include "SocketEvent.hpp"

void SocketEventListener::open(BaseSocket& sock,  SocketEventType eventType)
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
                _handle.events = POLLIN;                
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
        default:

    }  
}
SocketResult SocketEventListener::wait(uint32_t timeout_ms)
{
    SocketResult res {SocketCode::SUCCESS};
    int ret = ::poll(&_handle, 1, timeout_ms);
    if (ret == 0)
    {
        res = SocketResult(SocketCode::SOCKET_TIMEOUT, "Socket Timeout");
    }
    else if (ret <0) {
        res = SocketResult(SocketCode::SOCKET_EVENT_ERROR, "Socket Event Error");
    }
    else {
        if (_handle.revents & POLLERR) {
            res = SocketResult(SocketCode::SOCKET_CONNECTION_REFUSED, "Socket Connection Refused");
        }
        else if (_handle.revents & POLLHUP) {
            res = SocketResult(SocketCode::SOCKET_CLOSED, "Socket Closed");
        }
    }
    return res;
}


SocketResult SocketMultiEventListener::open()
{
    return SocketResult();
}
SocketResult SocketMultiEventListener::addEvent(int32_t sock, SocketEventType eventType)
{
    return SocketResult();
}
SocketResult SocketMultiEventListener::removeEvent()
{
    return SocketResult();
}
SocketResult SocketMultiEventListener::wait(SocketEvent* events, uint32_t timeout_ms)
{
    return SocketResult();
}


#endif // __linux__