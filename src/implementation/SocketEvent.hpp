#if !defined(__BN3MONKEY_SOCKETEVENTLISTENER__)
#define __BN3MONKEY_SOCKETEVENTLISTENER__
#include "../SecuritySocket.hpp"
#include "SocketResult.hpp"
#include "BaseSocket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
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
        DISCONNECTED,
        READ,
        WRITE,
        READ_WRITE
    };

    struct SocketEvent
    {
        int32_t sock {-1};
        SocketEventType type {SocketEventType::READ};
    };

    struct SocketEventResult
    {
        SocketResult result;
        std::vector<SocketEvent> events;
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
        
        SocketResult addEvent(BaseSocket& sock, SocketEventType eventType);
        SocketResult removeEvent(BaseSocket& sock);
        SocketEventResult wait(uint32_t timeout_ms);
        void close(); 
    private:
        int32_t _server_socket {0};
    #if defined(_WIN32)
        std::vector<pollfd> _handle;
    #elif defined __linux__
        std::vector<pollfd> _handle;
        // int32_t _handle;
    #endif
    };

}
#endif // __BN3MONKEY_SOCKETEVENTLISTSNER__