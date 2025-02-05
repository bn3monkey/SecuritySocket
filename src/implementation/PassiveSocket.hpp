#if !defined(__BN3MONKEY__PassiveSocket__)
#define __BN3MONKEY__PassiveSocket__

#include "../SecuritySocket.hpp"
#include "SocketAddress.hpp"
#include "BaseSocket.hpp"
#include "SocketConnection.hpp"

#include <cstdint>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <WS2tcpip.h>
#include <mswsock.h>
#else
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#endif


namespace Bn3Monkey {
	
	
	class PassiveSocket  : public BaseSocket {
	public:
		PassiveSocket(const SocketAddress& address) : _address(address) {}
		virtual SocketResult open();
		virtual void close();

		virtual SocketResult listen(size_t num_of_clients);		
		virtual SocketConnection accept();

	private:
		SocketAddress _address;
		int32_t _socket {-1};
		size_t _num_of_clients {0};

	};

	class TLSPassiveSocket : public PassiveSocket
	{
	public:
		TLSPassiveSocket(const SocketAddress& address) : PassiveSocket(address) {}
		virtual SocketResult open();
		virtual void close();

		virtual SocketResult listen(size_t num_of_clients);
		virtual SocketConnection accept();
	};

}

#endif // __BN3MONKEY__PassiveSocket__