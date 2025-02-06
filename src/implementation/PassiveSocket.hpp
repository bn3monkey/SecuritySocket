#if !defined(__BN3MONKEY__PASSIVESOCKET__)
#define __BN3MONKEY__PASSIVESOCKET__

#include "../SecuritySocket.hpp"
#include "SocketAddress.hpp"
#include "BaseSocket.hpp"
#include "ServerActiveSocket.hpp"

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
	
	using PassiveSocketContainer = SocketContainer<PassiveSocket, TLSPassiveSocket>;

	class PassiveSocket  : public BaseSocket {
	public:
		PassiveSocket(bool is_unix_domain = false);
		virtual void close();

		virtual SocketResult listen(const SocketAddress& address, size_t num_of_clients);		
		virtual ServerActiveSocketContainer accept();

	private:
		int32_t _socket {-1};
	};

	class TLSPassiveSocket : public PassiveSocket
	{
	public:
		TLSPassiveSocket(bool is_unix_domain = false);
		virtual void close();

		virtual SocketResult listen(const SocketAddress& address, size_t num_of_clients);
		virtual ServerActiveSocketContainer accept();
	};

}

#endif // __BN3MONKEY__PASSIVESOCKET__