#if !defined(__BN3MONKEY__TCPCLIENT__)
#define __BN3MONKEY__TCPCLIENT__

#include "../SecuritySocket.hpp"
#include "TCPSocket.hpp"
#include "TCPStream.hpp"

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
	class TCPClientImpl : public Bn3Monkey::TCPStream
	{
	public:
		TCPClientImpl() = delete;
		explicit TCPClientImpl(const TCPConfiguration& configuration);
		virtual ~TCPClientImpl();


	private:
				
		static constexpr size_t container_size = sizeof(TCPSocket) > sizeof(TLSSocket) ? sizeof(TCPSocket) : sizeof(TLSSocket);
		char _container[container_size];
		
		TCPSocket* _socket{ nullptr };
	};
}

#endif