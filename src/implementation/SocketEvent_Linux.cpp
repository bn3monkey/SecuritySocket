#if defined(__linux__)
#include "SocketEvent.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <algorithm>

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

    {
        std::lock_guard<std::mutex> lock(_mtx);
        _handle.push_back(fd);
        _contexts.push_back(context);
    }

    return SocketResult();
}
SocketResult SocketMultiEventListener::modifyEvent(SocketEventContext* context, SocketEventType eventType)
{
    // @Todo Need to change
    removeEvent(context);
    addEvent(context, eventType);
    return SocketResult();
}
SocketResult SocketMultiEventListener::removeEvent(SocketEventContext* context)
{
    {
        std::lock_guard<std::mutex> lock(_mtx);
        {
            auto iter = std::find_if(_handle.begin(), _handle.end(),
                [&context](pollfd& fds)
                {
                    return fds.fd == context->fd;
                });

            if (iter != _handle.end())
                _handle.erase(iter);
        }
        {
            auto iter = std::find(_contexts.begin(), _contexts.end(),
                context
            );
            if (iter != _contexts.end())
                _contexts.erase(iter);
        }
    }
    return SocketResult();
}
SocketEventResult SocketMultiEventListener::wait(uint32_t timeout_ms)
{
    SocketEventResult res;
    
    std::vector<SocketEventContext*> contexts;
    std::vector<pollfd> handle;

    {
        std::lock_guard<std::mutex> lock(_mtx);
        contexts = _contexts;
        handle = _handle;
    }


    int ret = ::poll(handle.data(), handle.size(), timeout_ms);
    if (ret == 0)
    {
        res.result = SocketResult(SocketCode::SOCKET_TIMEOUT);
    }
    else if (ret <0) {
        res.result = SocketResult(SocketCode::SOCKET_EVENT_ERROR);
    }
    else {
        res.contexts.reserve(ret);
        for (size_t i = 0; i < handle.size(); i++)
        {
            auto& event = handle[i];
            auto& event_type = event.revents;
            if (event_type == 0)
                continue;

            auto& event_fd = event.fd;

            SocketEventContext* context = nullptr;
            auto iter = std::find_if(contexts.begin(), contexts.end(), [event_fd](SocketEventContext* context) {
                return context->fd == event_fd;
                });
            if (iter != contexts.end())
            {
                context = *iter;
            }


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

#endif // __linux__