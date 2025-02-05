#if !defined(__BN3MONKEY_SOCKETEVENTLISTENER__)
#define __BN3MONKEY_SOCKETEVENTLISTENER__
#include "../SecuritySocket.hpp"
#include "SocketResult.hpp"
#include "BaseSocket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#elif __linux__
#include <poll.h>
#endif

namespace Bn3Monkey
{    
    enum class SocketEventType
    {
        UNDEFINED,
        ACCEPT,
        CONNECT,
        READ,
        WRITE
    };

    struct SocketEvent
    {
        int32_t sock {-1};
        SocketEventType type {SocketEventType::READ};
    };

    class SocketEventListener
    {
    public:
        void open(BaseSocket& sock, SocketEventType eventType);
        SocketResult wait(uint32_t timeout_ms);
    private:
        pollfd _handle;
    };

    class SocketMultiEventListener
    {
    public:
        SocketResult open();
        SocketResult addEvent(int32_t sock, SocketEventType eventType);
        SocketResult removeEvent();
        SocketResult wait(SocketEvent* events, uint32_t timeout_ms);
    private:
    };

}
#endif // __BN3MONKEY_SOCKETEVENTLISTSNER__