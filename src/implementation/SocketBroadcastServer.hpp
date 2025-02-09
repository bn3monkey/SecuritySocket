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
		virtual ~SocketBroadcastServerImpl();

		SocketResult open(size_t num_of_clients);

        SocketResult enumerate();
        SocketResult write(const void* buffer, size_t size);
        void close();

	private:
        SocketConfiguration _configuration;

        PassiveSocketContainer _container;
        PassiveSocket* _socket{ nullptr };

        // Need to be vector with stack pool.
        std::vector<ServerActiveSocketContainer> _client_containers;
    };
}

#endif // __BN3MONKEY__SOCKETEVENTSERVER__