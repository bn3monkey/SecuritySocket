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
		PassiveSocket(bool is_unix_domain = false);
		virtual void close();

		virtual SocketResult bind(const SocketAddress& address);
		virtual SocketResult listen();
		virtual ServerActiveSocketContainer accept();

	private:
	};

	class TLSPassiveSocket : public PassiveSocket
	{
	public:
		TLSPassiveSocket(bool is_unix_domain = false);
		virtual void close();

		virtual SocketResult bind(const SocketAddress& address);
		virtual SocketResult listen();
		virtual ServerActiveSocketContainer accept();
	};

	using PassiveSocketContainer = SocketContainer<PassiveSocket, TLSPassiveSocket>;
}

#endif // __BN3MONKEY__PASSIVESOCKET__