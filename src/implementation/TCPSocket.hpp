#if !defined(__BN3MONKEY__TCPSOCKET__)
#define __BN3MONKEY__TCPSOCKET__

#include "../SecuritySocket.hpp"
#include "TCPAddress.hpp"

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace Bn3Monkey
{
	enum class PollType {
		CONNECT,
		READ,
		WRITE
	};

	class TCPSocket
	{
	public:
		TCPSocket(const TCPAddress& address, uint32_t timeout_milliseconds);
		virtual ~TCPSocket();

		operator const ConnectionResult& () const { return _result; }

		virtual long connect();
		virtual void disconnect();
		virtual int write(const char* buffer, size_t size);
		virtual int read(char* buffer, size_t size);

		void poll(const PollType& polltype);

		virtual ConnectionResult result(int operation_return);

	protected:
		ConnectionResult _result;
		SOCKET _socket;
	};

	class TLSSocket : public TCPSocket
	{
	public:
		TLSSocket(const TCPAddress& address, uint32_t timeout_milliseconds);
		virtual ~TLSSocket();


		long connect() override;
		void disconnect() override;
		int write(const char* buffer, size_t size) override;
		int read(char* buffer, size_t size) override;

		ConnectionResult result(int operation_return) override;
	private:
		SSL_CTX* _context{ nullptr };
		SSL* _ssl{ nullptr };

	};
}

#endif