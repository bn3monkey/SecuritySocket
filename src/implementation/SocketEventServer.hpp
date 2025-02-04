#if !defined(__BN3MONKEY__SOCKETEVENTSERVER__)
#define __BN3MONKEY__SOCKETEVENTSERVER__
#include "../SecuritySocket.hpp"
#include "ActiveSocket.hpp"

namespace Bn3Monkey
{
    class SocketEventServerImpl
    {
    public:
		SocketEventServerImpl(const SocketConfiguration& configuration) : _configuration(configuration) {}
		virtual ~SocketEventServerImpl();

		SocketResult open(size_t num_of_clients);
        SocketResult write(const void* buffer, size_t size);
        void close();

	private:
				
		static constexpr size_t container_size = sizeof(ActiveSocket) > sizeof(TLSActiveSocket) ? sizeof(ActiveSocket) : sizeof(TLSActiveSocket);
		char _container[container_size]{ 0 };
		ActiveSocket* _socket{ nullptr };

		SocketConfiguration _configuration;
        
        size_t _num_of_clients;
        int32_t _connected_clients[MAX_EVENTS];
    };
}

#endif // __BN3MONKEY__SOCKETEVENTSERVER__