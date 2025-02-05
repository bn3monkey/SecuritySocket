#if !defined(__BN3MONKEY_SOCKETEVENTLISTENER__)
#define __BN3MONKEY_SOCKETEVENTLISTENER__
#include "../SecuritySocket.hpp"
#include "SocketResult.hpp"

namespace Bn3Monkey
{
    
    enum class SocketEventType
    {
        UNDEFINED,
        ACCEPT,
        READ,
        WRITE
    };
    struct SocketEvent
    {
        static constexpr size_t MAX_EVENTS = 32;
        int32_t sock {-1};
        SocketEventType type {SocketEventType::UNDEFINED};
    };
    struct SocketEventResult
    {
        SocketResult result {SocketCode::SUCCESS};
        size_t length;
        SocketEvent events[SocketEvent::MAX_EVENTS];
    };

    class SocketMultiEventListener
    {
    public:
    #ifdef _WIN32
        using ListenerHandle = void*;
    #else
        using ListenerHandle = int32_t;
    #endif

        SocketResult open(int32_t sock);
        SocketEventResult poll(uint32_t timeout_ms); 
        SocketResult addSocketEvent(int32_t sock, SocketEventType type);
        SocketResult removeSocketEvent(int32_t sock, SocketEventType type);
        void close();
    private:
        ListenerHandle _handle;   
    };
}
#endif // __BN3MONKEY_SOCKETEVENTLISTSNER__