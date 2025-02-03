#if !defined(__BN3MONKEY__PASSIVESOCKET__)
#define __BN3MONKEY__PASSIVESOCKET__

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

namespace Bn3Monkey
{
	enum class PassivePollType {
		CONNECT,
		READ,
		WRITE
	};

	class PassiveSocket : public PassiveSocket
	{
	public:
		virtual SocketResult open(const SocketAddress& address, uint32_t read_timeout, uint32_t write_timeout);
		virtual void close();

		virtual SocketResult connect();
		virtual void disconnect(); 
		virtual SocketResult isConnected();
		virtual SocketResult poll(const PassivePollType& polltype);
		virtual int read(void* buffer, size_t size);
		virtual int write(const void* buffer, size_t size);

	protected:
		SocketAddress _address;
		int32_t _socket {0};
		uint32_t _read_timeout {0};
		uint32_t _write_timeout {0};

	private:
		class NonBlockMode
		{
		public:
			explicit NonBlockMode(PassiveSocket& socket);
			~NonBlockMode();
		private:
            int32_t _socket;
			int32_t _flags;
		};
	};

	class TLSPassiveSocket : public PassiveSocket
	{
	public:
		virtual SocketResult open(const SocketAddress& address, uint32_t read_timeout, uint32_t write_timeout) override;
		virtual void close() override;

		SocketResult connect() override;

		void disconnect() override;
		SocketResult isConnected() override;
		SocketResult poll(const PassivePollType& polltype) override;
		int read(void* buffer, size_t size) override;
		int write(const void* buffer, size_t size) override;

	private:
		SSL_CTX* _context{ nullptr };
		SSL* _ssl{ nullptr };
	};

}

#endif // __BN3MONKEY__PASSIVESOCKET__