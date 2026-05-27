#if !defined(__BN3MONKEY_SOCKETEVENTLISTENER__)
#define __BN3MONKEY_SOCKETEVENTLISTENER__
#include "../SecuritySocket.hpp"
#include "NetworkResult.hpp"
#include "BaseSocket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#elif __linux__
#include <poll.h>
#include <sys/epoll.h>
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

    class SocketEventListener
    {
    public:
        void open(BaseSocket& sock, SocketEventType eventType);
        NetworkResult wait(uint32_t timeout_ms);
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
        SocketEventType type{ SocketEventType::UNDEFINED };
    };

    struct SocketEventResult
    {
        NetworkResult result;
        std::vector<SocketEventContext*> contexts;
    };

    class SocketMultiEventListener
    {
    public:
        NetworkResult open();
        void close();
        NetworkResult addEvent(SocketEventContext* context, SocketEventType eventType);
        NetworkResult modifyEvent(SocketEventContext* context, SocketEventType eventType);
        NetworkResult removeEvent(SocketEventContext* context);
        // Unblock any thread currently in wait(). Safe to call from any thread.
        // Use for shutdown or to force a re-evaluation of registered fds.
        // add/modify/removeEvent also call this internally so a concurrent
        // wait()er observes the change on its next entry rather than after the
        // current timeout.
        void wake();
        SocketEventResult wait(uint32_t timeout_ms);

    private:
        int32_t _server_socket {0};
        std::mutex _mtx;

#if defined(_WIN32)
        // poll-based snapshot model. _handle and _contexts are index-parallel:
        // _handle[i] corresponds to _contexts[i]. Slot 0 is always the wakeup
        // read end with a nullptr context sentinel.
        std::vector<pollfd> _handle;
        std::vector<SocketEventContext*> _contexts;
        SOCKET _wakeup_read  { INVALID_SOCKET };
        SOCKET _wakeup_write { INVALID_SOCKET };
#elif defined(__linux__)
        // epoll keeps the watch list in the kernel. ev.data.ptr stores the
        // SocketEventContext* directly (nullptr for the wakeup eventfd).
        int _epfd      { -1 };
        int _wakeup_fd { -1 };
#endif
    };

}
#endif // __BN3MONKEY_SOCKETEVENTLISTSNER__