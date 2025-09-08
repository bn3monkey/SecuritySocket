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
        SocketConnection(ServerActiveSocketContainer& container, SocketRequestHandler& handler, size_t pdu_size) :
            _container(container),
            _handler(handler) {
            _socket = _container.get();
            fd = _socket->descriptor();

            initialize(_handler.headerSize(), pdu_size);
        }
        virtual ~SocketConnection() {}


        void connectClient(SocketMultiEventListener& listener);
        void disconnectClient(SocketMultiEventListener& listener);

        SocketRequestHeader* readHeader();
        void runTask(SocketRequestHeader* header);
        void runNoResponseTask(SocketRequestHeader* header);
        void runSlowTask(SocketMultiEventListener& listener, SocketRequestHeader* header);
        
    private:

        ServerActiveSocketContainer _container;
        ServerActiveSocket* _socket;
        SocketRequestHandler& _handler;

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