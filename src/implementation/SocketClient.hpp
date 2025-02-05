#if !defined(__BN3MONKEY__TCPCLIENT__)
#define __BN3MONKEY__TCPCLIENT__

#include "../SecuritySocket.hpp"
#include "ActiveSocket.hpp"
#include "SocketEvent.hpp"

#include <type_traits>
#include <atomic>

#include <mutex>
#include <thread>
#include <condition_variable>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
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
		explicit SocketClientImpl(const SocketConfiguration& configuration) : _configuration(configuration) {};
		virtual ~SocketClientImpl();

		SocketResult open();
        void close();

		SocketResult connect();
		SocketResult read(void* buffer, size_t size);
		SocketResult write(const void* buffer, size_t size);

	private:
		
		ActiveSocket* _socket{ nullptr };
		static constexpr size_t container_size = sizeof(ActiveSocket) > sizeof(TLSActiveSocket) ? sizeof(ActiveSocket) : sizeof(TLSActiveSocket);
		char _container[container_size]{ 0 };

		SocketConfiguration _configuration;

	};
}

#endif