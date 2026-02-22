#if !defined(__BN3MONKEY__CLIENTACTIVESOCKET__)
#define __BN3MONKEY__CLIENTACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "SocketAddress.hpp"
#include "BaseSocket.hpp"
#include "SocketHelper.hpp"

#include <cstdint>
#include "TLSHelper.hpp"

namespace Bn3Monkey
{

	class ClientActiveSocket : public BaseSocket
	{
	public:
		ClientActiveSocket(bool is_unix_domain, const SocketTLSClientConfiguration& tls_configuration, const char* hostname = nullptr);
		virtual ~ClientActiveSocket();

		virtual void close();

		virtual SocketResult connect(const SocketAddress& address, uint32_t read_timeout_ms, uint32_t write_timeout_ms);
		virtual SocketResult reconnect();

		virtual void disconnect(); 
		virtual SocketResult isConnected();
		virtual SocketResult read(void* buffer, size_t size);
		virtual SocketResult write(const void* buffer, size_t size);

	protected:
	};


	class TLSClientActiveSocket : public ClientActiveSocket
	{
	public:
		TLSClientActiveSocket(bool is_unix_domain, const SocketTLSClientConfiguration& tls_configuration, const char* hostname = nullptr);
		virtual ~TLSClientActiveSocket();

		virtual void close() override;

		SocketResult connect(const SocketAddress& address, uint32_t read_timeout_ms, uint32_t write_timeout_ms) override;
		virtual SocketResult reconnect() override;
		void disconnect() override;
		SocketResult isConnected() override;
		SocketResult read(void* buffer, size_t size) override;
		SocketResult write(const void* buffer, size_t size) override;

	private:
		SSL_CTX* _context{ nullptr };
		SSL* _ssl{ nullptr };
		const char* _hostname{ nullptr };  // points to SocketConfiguration._ip (externally owned)
	};

	using ClientActiveSocketContainer = SocketContainer<ClientActiveSocket, TLSClientActiveSocket>;
}

#endif // __BN3MONKEY__CLIENTACTIVESOCKET__