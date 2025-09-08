#if !defined(__BN3MONKEY_SOCKET_CONNECTION__)
#define __BN3MONKEY_SOCKET_CONNECTION__

#include "../SecuritySocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"

#include <thread>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace Bn3Monkey
{
    class SocketConnection : public SocketEventContext
    {
    public:
        enum class ProcessState {
            READING_HEADER,
            READING_PAYLOAD,
            WRITING_RESPONSE,
        };

        SocketConnection(ServerActiveSocketContainer& container, SocketRequestHandler& handler, size_t pdu_size) :
            _container(container),
            _handler(handler) {
            _socket = _container.get();
            fd = _socket->descriptor();

            input_header_buffer.resize(handler.headerSize());
            input_payload_buffer.resize(pdu_size);
            output_buffer.resize(pdu_size);
        }
        virtual ~SocketConnection() {}


        void connectClient();
        void disconnectClient();

        ProcessState state;

        // false : READING_HEADER | true : READING_PAYLOAD
        ProcessState readHeader();
        
        // false : READING_PAYLOAD | true : HANDLE_TASK
        ProcessState readPayload();

        // false : WRITING_RESPONSE | true : READING_HEADER
        ProcessState  writeResponse();
        
        void flush();
        
    private:

        ServerActiveSocketContainer _container;
        ServerActiveSocket* _socket;

        SocketRequestHandler& _handler;
        
        // Read Header
        size_t total_input_header_read_size{ 0 };
        std::vector<char> input_header_buffer{ 0, std::allocator<char>() };

        // Reading Payload
        size_t payload_size{ 0 };
        size_t total_input_payload_read_size{ 0 };
        std::vector<char> input_payload_buffer{ 0, std::allocator<char>() };

        SocketRequestMode mode;

        size_t response_size;
        size_t total_output_write_size{ 0 };
        std::vector<char> output_buffer{ 0, std::allocator<char>() };

        // Worker Thread

        std::thread _worker;
        bool _is_running{ false };
        std::queue<std::function<void()>> _tasks;
        std::mutex _mtx;
        std::condition_variable _cv;


        void startWorker();
        void stopWorker();
        void routine();
        void addTask(std::function<void()> task);
    };
}

#endif // __BN3MONKEY_SOCKET_CONNECTION__