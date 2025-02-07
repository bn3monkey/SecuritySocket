#if !defined(__BN3MONKEY__CLIENTACTIVESOCKET__)
#define __BN3MONKEY__CLIENTACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "SocketAddress.hpp"
#include "BaseSocket.hpp"
#include "SocketHelper.hpp"

#include <cstdint>
#include <openssl/ssl.h>
#include <openssl/err.h>


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

	class ClientActiveSocket : public BaseSocket
	{
	public:
		ClientActiveSocket(bool is_unix_domain = false);
		virtual ~ClientActiveSocket();

		virtual void close();

		virtual SocketResult connect(const SocketAddress& address);
		virtual void disconnect(); 
		virtual SocketResult isConnected();
		virtual int read(void* buffer, size_t size);
		virtual int write(const void* buffer, size_t size);

	protected:
		int32_t _socket {0};
		uint32_t _read_timeout {0};
		uint32_t _write_timeout {0};
	};

	class TLSClientActiveSocket : public ClientActiveSocket
	{
	public:
		TLSClientActiveSocket(bool is_unix_domain = false);
		virtual ~TLSClientActiveSocket();

		virtual void close() override;

		SocketResult connect(const SocketAddress& address) override;
		void disconnect() override;
		SocketResult isConnected() override;
		int read(void* buffer, size_t size) override;
		int write(const void* buffer, size_t size) override;

	private:
		SSL_CTX* _context{ nullptr };
		SSL* _ssl{ nullptr };
	};

	using ClientActiveSocketContainer = SocketContainer<ClientActiveSocket, TLSClientActiveSocket>;
}

#endif // __BN3MONKEY__CLIENTACTIVESOCKET__