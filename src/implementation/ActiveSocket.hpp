#if !defined(__BN3MONKEY__ACTIVESOCKET__)
#define __BN3MONKEY__ACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "SocketAddress.hpp"

#include <cstdint>
#include <openssl/ssl.h>
#include <openssl/err.h>

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


namespace Bn3Monkey {
	class ActiveSocket {
	public:
		virtual SocketResult open(const SocketAddress& address);
		virtual void close();

		virtual SocketResult listen(size_t num_of_clients);
		virtual SocketResult poll();

		virtual int32_t accept();
		virtual int32_t read(int32_t client_idx, void* buffer, size_t size);
		virtual int32_t write(int32_t client_idx, const void* buffer, size_t size) ;
		virtual void drop(int32_t client_idx);
	};

	class TLSActiveSocket : public ActiveSocket
	{
	public:
		virtual SocketResult open(const SocketAddress& address);
		virtual void close();

		virtual SocketResult listen(size_t num_of_clients);
		virtual SocketResult poll();

		virtual int32_t accept();
		virtual int32_t read(int32_t client_idx, void* buffer, size_t size);
		virtual int32_t write(int32_t client_idx, const void* buffer, size_t size);
		virtual void drop(int32_t client_idx);
	};

}

#endif // __BN3MONKEY__ACTIVESOCKET__