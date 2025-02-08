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
            int error{ 0 };
            socklen_t len = sizeof(error);
            getsockopt(_handle.fd, SOL_SOCKET, SO_ERROR, (char*) & error, &len);
            if (error == 0)
            {
                res = SocketResult(SocketCode::SUCCESS);
            }
            else {
                printf("async connection error : %s\n", strerror(error));
                res = SocketResult(SocketCode::UNKNOWN_ERROR);
            }
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
                fd.events = POLLIN | POLLOUT;                
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

    printf("Poll (Server fd : %d)\n", _server_socket);
    for (auto& handle : _handle)
    {
        printf("- fd : %d events : ", handle.fd);
        if (handle.events & POLLIN)
            printf("POLLIN ");
        if (handle.events & POLLOUT)
            printf("POLLOUT");
        printf("\n");
    }

    char* typestr = "";
    switch (eventType)
    {
    case SocketEventType::ACCEPT:
    {
        typestr = "accept";
    }
    break;
    case SocketEventType::CONNECT:
    {
        typestr = "connect";
    }
    break;
    case SocketEventType::READ:
    {
        typestr = "read";
    }
    break;
    case SocketEventType::WRITE:
    {
        typestr = "write";
    }
    break;
    case SocketEventType::READ_WRITE:
    {
        typestr = "READ_WRITE";
    }
    break;
    default:
        break;
    }
    printf("Added Event : fd - %d %s\n", context->fd, typestr);
    
    
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
    printf("Wait / handle size : %llu\n", _handle.size());
    for (auto& handle : _handle)
    {
        printf("- fd : %d events : ", handle.fd);
        if (handle.events & POLLIN)
            printf("POLLIN ");
        if (handle.events & POLLOUT)
            printf("POLLOUT");
        printf("\n");
    }

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