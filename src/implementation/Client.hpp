#if !defined(__BN3MONKEY_CLIENT__)
#define __BN3MONKEY_CLIENT__

#include "../SecuritySocket.hpp"
#include "ClientActiveSocket.hpp"
#include "SocketEvent.hpp"
#include "SocketHelper.hpp"

#include <type_traits>
#include <atomic>

#include <mutex>
#include <thread>
#include <condition_variable>

#ifdef _WIN32
#include <Winsock2.h>
#include <WS2tcpip.h>
#else
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#endif

namespace Bn3Monkey
{
	class ClientImpl
	{
	public:
		explicit ClientImpl(const NetworkConfiguration& configuration) 
			: _configuration(configuration) {}
		explicit ClientImpl(const NetworkConfiguration& configuration, const TlsClientConfiguration& tls_configuration)
			: _configuration(configuration), _tls_configuration(tls_configuration) {
		}

		virtual ~ClientImpl();

		NetworkResult open();
        void close();

		NetworkResult connect();
		NetworkResult read(void* buffer, size_t size);
		NetworkResult write(const void* buffer, size_t size);
		NetworkResult isConnected();

	private:
		ClientActiveSocketContainer _container{};
		ClientActiveSocket* _socket{ nullptr };
		
		NetworkConfiguration _configuration;
		TlsClientConfiguration _tls_configuration;
	};
}

#endif