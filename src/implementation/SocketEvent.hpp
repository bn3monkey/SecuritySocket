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
#include <mutex>
#include <condition_variable>

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
    enum class SocketTaskType
    {
        PROCESSING,
        SUCCESS,
        FAIL,
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
        int32_t fd{ -1 };
        SocketEventType type { SocketEventType::UNDEFINED };

        std::vector<char> input_header_buffer {0, std::allocator<char>()};
        std::vector<char> input_payload_buffer {0, std::allocator<char>()};
        std::vector<char> output_buffer {0, std::allocator<char>()};
        
    public:
        inline void initialize(size_t header_size, size_t pdu_size)
        {
            input_header_buffer.resize(header_size);
            input_payload_buffer.resize(pdu_size);
            output_buffer.resize(pdu_size);
        }
        inline void flush() {
            memset(input_header_buffer.data(), 0, input_header_buffer.size());
            memset(input_payload_buffer.data(), 0, input_payload_buffer.size());
            memset(output_buffer.data(), 0, output_buffer.size());
        }
    
    private:
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
        void close();        
        SocketResult addEvent(SocketEventContext* context, SocketEventType eventType);
        SocketResult modifyEvent(SocketEventContext* context, SocketEventType eventType);
        SocketResult removeEvent(SocketEventContext* context);
        SocketEventResult wait(uint32_t timeout_ms);

    private:
        int32_t _server_socket {0};
        std::mutex _mtx;
        std::vector<SocketEventContext*> _contexts;
    
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