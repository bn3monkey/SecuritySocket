#if defined(_WIN32)

#include "SocketEvent.hpp"


SocketResult SocketMultiEventListener::open(int32_t sock)
{
    _handle = CreateIoCompletionPort((HANDLE)sock, NULL, (ULONG_PTR)sock, 0);
    if (_handle == nullptr)
    {
        return SocketResult(SocketCode::SOCKET_EVENT_OBJECT_NOT_CREATED, "Socket Event object is not created");
    }
}
SocketEventResult poll(uint32_t timeout_ms)
{
    SocketEventResult result;
}
SocketResult SocketMultiEventListener::addSocketEvent(int32_t sock, SocketEventType type)
{

}
SocketResult SocketMultiEventListener::removeSocketEvent(int32_t sock, SocketEventType type)
{

}
void SocketMultiEventListener::close()
{
    if (_handle != nullptr)
    {
        CloseHandle(_handle);
        _handle = nullptr;
    }
}

#endif // _WIN32