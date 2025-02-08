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

        std::vector<char> input_buffer {0, std::allocator<char>()};
        size_t total_input_size {0};
        std::vector<char> output_buffer {0, std::allocator<char>()};
        size_t total_output_size {0};
        size_t written_size{ 0 };

    public:
        
        inline void finishTask(bool result)
        {
            {
                std::unique_lock<std::mutex> lock(_mtx);
               _task_status = result ? SocketTaskType::SUCCESS : SocketTaskType::FAIL;
               _cv.notify_one();
            }
        }

        inline SocketTaskType waitTask() {
            SocketTaskType ret;
            {
                std::unique_lock<std::mutex> lock(_mtx);
                _cv.wait(lock, [&]() {
                    return _task_status != SocketTaskType::PROCESSING;
                    });
                ret = _task_status;
            }
            return ret;
        }

        inline void flush() {
            _task_status = SocketTaskType::PROCESSING;
            memset(input_buffer.data(), 0, total_input_size);
            total_input_size = 0;
            memset(output_buffer.data(), 0, total_output_size);
            total_output_size = 0;
            written_size = 0;
        }
    
    private:
        SocketTaskType _task_status{ SocketTaskType::PROCESSING };
        std::mutex _mtx;
        std::condition_variable _cv;
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