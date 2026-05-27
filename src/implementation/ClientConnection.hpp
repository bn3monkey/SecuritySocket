#if !defined(__BN3MONKEY_CLIENT_CONNECTION__)
#define __BN3MONKEY_CLIENT_CONNECTION__

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
    // Internal owner of a single accepted client socket. Replaces the
    // legacy SocketConnection and implements the public ClientConnection
    // interface so handler callbacks can be passed a stable view of the
    // connection.
    //
    // Phase 3 keeps the existing 3-buffer / ProcessState layout intact;
    // the single-buffer + 13-state ConnectionState restructure is the
    // job of Phase 6.
    class ClientConnectionImpl : public ClientConnection, public SocketEventContext
    {
    public:
        enum class ProcessState {
            READING_HEADER,
            READING_PAYLOAD,
            WRITING_RESPONSE,
            FINISH_PROCESS
        };

        ClientConnectionImpl(ServerActiveSocketContainer& container,
                             CustomProtocolRequestHandler& handler,
                             size_t pdu_size,
                             bool is_secure)
            : _container(container),
              _handler(handler),
              _is_secure(is_secure)
        {
            _socket = _container.get();
            fd = _socket->descriptor();

            input_header_buffer.resize(handler.getHeaderSize());
            input_payload_buffer.resize(pdu_size);
            output_buffer.resize(pdu_size);
        }
        virtual ~ClientConnectionImpl() {}

        // ── ClientConnection (pure abstract) ──
        // Phase 3: isWebSocket() is permanently false. Phase 6 will return
        // true based on the WebSocket-group ConnectionState.
        const char* ip()          const override { return _socket->ip(); }
        uint32_t    port()        const override { return static_cast<uint32_t>(_socket->port()); }
        bool        isSecure()    const override { return _is_secure; }
        bool        isWebSocket() const override { return false; }

        void connectClient();
        void disconnectClient();

        ProcessState state{ ProcessState::READING_HEADER };

        // false : READING_HEADER | true : READING_PAYLOAD
        ProcessState readHeader();

        // false : READING_PAYLOAD | true : HANDLE_TASK
        ProcessState readPayload();

        // false : WRITING_RESPONSE | true : READING_HEADER
        ProcessState writeResponse();

        void flush();

    private:
        ProcessState runTask(RequestProcessingMode mode, size_t payload_size);

        ServerActiveSocketContainer _container{};
        ServerActiveSocket* _socket{ nullptr };

        CustomProtocolRequestHandler& _handler;

        bool _is_secure{ false };

        // Read Header
        size_t total_input_header_read_size{ 0 };
        std::vector<char> input_header_buffer{ 0, std::allocator<char>() };

        // Reading Payload
        size_t _payload_size{ 0 };
        size_t total_input_payload_read_size{ 0 };
        std::vector<char> input_payload_buffer{ 0, std::allocator<char>() };

        RequestProcessingMode _mode{ RequestProcessingMode::FAST };

        size_t response_size{ 0 };
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

#endif // __BN3MONKEY_CLIENT_CONNECTION__
