#if !defined(__BN3MONKEY_SOCKET_CONNECTION__)
#define __BN3MONKEY_SOCKET_CONNECTION__

#include "../SecuritySocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"

namespace Bn3Monkey
{
    class SocketConnection : public SocketEventContext
    {
    public:
        SocketConnection(ServerActiveSocketContainer& container) :
            _container(container) {
            _socket = _container.get();
            fd = _socket->descriptor();
        }
        virtual ~SocketConnection() {}

        inline ServerActiveSocket* socket() const { return _socket; }
    private:
        ServerActiveSocketContainer _container;
        ServerActiveSocket* _socket;
    };
}

#endif // __BN3MONKEY_SOCKET_CONNECTION__