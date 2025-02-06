#if !defined(__BN3MONKEY_SOCKET_CONNECTION__)
#define __BN3MONKEY_SOCKET_CONNECTION__

#include "../SecuritySocket.hpp"
#include "ServerActiveSocket.hpp"

namespace Bn3Monkey
{
    class SocketConnectionImpl : public SocketConnection
    {
    public:
        SocketConnectionImpl(SocketConfiguration& configuration, ServerActiveSocketContainer& container) : _configuration(configuration), _container(container) {
            _socket = _container.get();    
        }
        virtual ~SocketConnectionImpl() {}
        
		SocketResult read(void* buffer, size_t size);
		SocketResult write(const void* buffer, size_t size);
        void close();

    private:
        SocketConfiguration& _configuration;
        ServerActiveSocketContainer _container;
        ServerActiveSocket* _socket;
    }
}

#endif // __BN3MONKEY_SOCKET_CONNECTION__