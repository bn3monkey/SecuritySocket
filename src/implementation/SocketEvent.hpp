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

#include <vector>
#include <unordered_map>

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

    class SocketEventListener
    {
    public:
        void open(BaseSocket& sock, SocketEventType eventType);
        SocketResult wait(uint32_t timeout_ms);
    private:
        pollfd _handle;
    };


    /*** MULTI-EVENT LISTENER  ***/

    struct SocketEventContext
    {
    #ifdef _WIN32
        OVERLAPPED overlapped;
    #endif
        SocketEventType type { SocketEventType::UNDEFINED };
        
        int32_t fd {-1};
        std::vector<char> input_buffer {0, std::allocator<char>()};
        size_t read_size {0};
        std::vector<char> output_buffer {0, std::allocator<char>()};
        size_t write_size {0};
    };

    struct SocketEventResult
    {
        SocketResult result;
        std::vector<SocketEventContext*> contexts;
    };

    class SocketMultiEventListener
    {
    public:
        SocketResult open();
        
        SocketResult addEvent(SocketEventContext* context, SocketEventType eventType);
        SocketResult modifyEvent(SocketEventContext* context, SocketEventType eventType);
        SocketResult removeEvent(SocketEventContext* context);
        SocketEventResult wait(uint32_t timeout_ms);
        void close(); 
    private:
        int32_t _server_socket {0};
        std::unordered_map<int32_t, SocketEventContext*> _contexts;

    #if defined(_WIN32)
        std::vector<pollfd> _handle;
        // void *_handle;
    #elif defined __linux__
        std::vector<pollfd> _handle;
        // int32_t _handle;
    #endif
    };

}
#endif // __BN3MONKEY_SOCKETEVENTLISTSNER__