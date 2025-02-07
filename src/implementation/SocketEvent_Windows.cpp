#if defined(_WIN32)
#include "SocketEvent.hpp"
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
                _handle.events = POLLOUT;                
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
    int ret = WSAPoll(&_handle, 1, timeout_ms);
    if (ret == 0)
    {
        res = SocketResult(SocketCode::SOCKET_TIMEOUT);
    }
    else if (ret <0) {
        res = SocketResult(SocketCode::SOCKET_EVENT_ERROR);
    }
    else {
        if (_handle.revents & POLLERR) {
            char buffer[256]{ 0 };
            int buffer_length = 256;
            int ret = getsockopt(_handle.fd, SOL_SOCKET, SO_ERROR, buffer, &buffer_length);
            if (ret == SOCKET_ERROR)
                 res = createResult(ret);

        }
        else if (_handle.revents & POLLHUP) {
            res = SocketResult(SocketCode::SOCKET_CLOSED);
        }
    }
    return res;
}


SocketResult SocketMultiEventListener::open()
{
    _handle.reserve(16);
    return SocketResult();
}
void SocketMultiEventListener::close()
{
    // Do nothing
}
SocketResult SocketMultiEventListener::addEvent(SocketEventContext* context, SocketEventType eventType)
{
    pollfd fd;
    fd.fd = context->fd;
    
    switch(eventType)
    {
        case SocketEventType::ACCEPT:
            {
                fd.events = POLLIN;
                _server_socket = context->fd;
            }
            break;
        case SocketEventType::CONNECT:
            {
                fd.events = POLLIN;                
            }
            break;
        case SocketEventType::READ:
            {
                fd.events = POLLIN;                
            }
            break;
        case SocketEventType::WRITE:
            {
                fd.events = POLLOUT;
            }
            break;
        case SocketEventType::READ_WRITE:
            {
                fd.events = POLLIN | POLLOUT;
            }
            break;
        default:
            break;
    }  
    _handle.push_back(fd);
    
    _contexts[context->fd] = context;

    return SocketResult();
}
SocketResult SocketMultiEventListener::modifyEvent(SocketEventContext* context, SocketEventType eventType)
{
    removeEvent(context);
    addEvent(context, eventType);
    return SocketResult();
}
SocketResult SocketMultiEventListener::removeEvent(SocketEventContext* context)
{
    for (auto iter = _handle.begin(); iter != _handle.end(); iter++)
    {
        if (iter->fd == context->fd)
        {
            _handle.erase(iter);
        }
    }

    auto iter = _contexts.find(context->fd);
    if (iter != _contexts.end())
        _contexts.erase(iter);
    return SocketResult();
}
SocketEventResult SocketMultiEventListener::wait(uint32_t timeout_ms)
{
    SocketEventResult res;
    int ret = WSAPoll(_handle.data(), _handle.size(), timeout_ms);
    if (ret == 0)
    {
        res.result = SocketResult(SocketCode::SOCKET_TIMEOUT);
    }
    else if (ret <0) {
        res.result = SocketResult(SocketCode::SOCKET_EVENT_ERROR);
    }
    else {
        res.contexts.reserve(ret);
        for (size_t i = 0; i < ret; i++)
        {

            auto& event = _handle[i];
            auto& event_fd = event.fd;
            auto& event_type = event.revents;

            SocketEventContext* context = _contexts[event_fd];

            if (event_type & POLLERR || event_type & POLLHUP)
            {
                context->type = SocketEventType::DISCONNECTED;
            }
            else if (event_type & (POLLIN | POLLOUT))
            {
                context->type = SocketEventType::READ_WRITE;
            }
            else if (_server_socket == event_fd && event_type & POLLIN)
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