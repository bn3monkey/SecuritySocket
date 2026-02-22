#if !defined(__BN3MONKEY__TCPCLIENT__)
#define __BN3MONKEY__TCPCLIENT__

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
	class SocketClientImpl
	{
	public:
		explicit SocketClientImpl(const SocketConfiguration& configuration) 
			: _configuration(configuration) {}
		explicit SocketClientImpl(const SocketConfiguration& configuration, const SocketTLSClientConfiguration& tls_configuration)
			: _configuration(configuration), _tls_configuration(tls_configuration) {
		}

		virtual ~SocketClientImpl();

		SocketResult open();
        void close();

		SocketResult connect();
		SocketResult read(void* buffer, size_t size);
		SocketResult write(const void* buffer, size_t size);
		SocketResult isConnected();

	private:
		ClientActiveSocketContainer _container{};
		ClientActiveSocket* _socket{ nullptr };
		
		SocketConfiguration _configuration;
		SocketTLSClientConfiguration _tls_configuration;
	};
}

#endif