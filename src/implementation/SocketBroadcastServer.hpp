#if !defined(__BN3MONKEY__SOCKETBROADCASTSERVER__)
#define __BN3MONKEY__SOCKETBROADCASTSERVER__
#include "../SecuritySocket.hpp"

#include "ServerActiveSocket.hpp"

#include "PassiveSocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"
#include "SocketConnection.hpp"
#include "ObjectPool.hpp"

namespace Bn3Monkey
{
    class SocketBroadcastServerImpl
    {
    public:
		SocketBroadcastServerImpl(const SocketConfiguration& configuration) : _configuration(configuration) {}
        SocketBroadcastServerImpl(const SocketConfiguration& configuration, const SocketTLSServerConfiguration& tls_configuration) 
            : _configuration(configuration), _tls_configuration(tls_configuration) {}

		virtual ~SocketBroadcastServerImpl();

		SocketResult open(size_t num_of_clients);

        SocketResult enumerate();
        SocketResult write(const void* buffer, size_t size);
        void close();

	private:
        SocketConfiguration _configuration;
        SocketTLSServerConfiguration _tls_configuration;

        PassiveSocketContainer _container;
        PassiveSocket* _socket{ nullptr };

        // Need to be vector with stack pool.
        std::vector<ServerActiveSocketContainer> _client_containers[2];
        std::vector<ServerActiveSocketContainer>* _front_client_containers{ &_client_containers[0]};
        std::vector<ServerActiveSocketContainer>* _back_client_containers{ &_client_containers[1] };
    };
}

#endif // __BN3MONKEY__SOCKETEVENTSERVER__