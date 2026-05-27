#if !defined(__BN3MONKEY__CLIENTACTIVESOCKET__)
#define __BN3MONKEY__CLIENTACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "SocketAddress.hpp"
#include "BaseSocket.hpp"
#include "SocketHelper.hpp"

#include <cstdint>
#include "TlsHelper.hpp"

namespace Bn3Monkey
{

	class ClientActiveSocket : public BaseSocket
	{
	public:
		ClientActiveSocket(bool is_unix_domain, const TlsClientConfiguration& tls_configuration, const char* hostname = nullptr);
		virtual ~ClientActiveSocket();

		virtual void close();

		virtual NetworkResult connect(const SocketAddress& address, uint32_t read_timeout_ms, uint32_t write_timeout_ms);
		virtual NetworkResult reconnect(bool after_handshake);

		virtual void disconnect(); 
		virtual NetworkResult isConnected();
		virtual NetworkResult read(void* buffer, size_t size);
		virtual NetworkResult write(const void* buffer, size_t size);

	protected:
	};


	class TlsClientActiveSocket : public ClientActiveSocket
	{
	public:
		TlsClientActiveSocket(bool is_unix_domain, const TlsClientConfiguration& tls_configuration, const char* hostname = nullptr);
		virtual ~TlsClientActiveSocket();

		virtual void close() override;

		NetworkResult connect(const SocketAddress& address, uint32_t read_timeout_ms, uint32_t write_timeout_ms) override;
		virtual NetworkResult reconnect(bool after_handshake) override;
		void disconnect() override;
		NetworkResult isConnected() override;
		NetworkResult read(void* buffer, size_t size) override;
		NetworkResult write(const void* buffer, size_t size) override;

	private:
		// Detects deferred client-certificate rejection alerts that arrive after
		// SSL_connect() has already returned success in TLS 1.3.
		// Must only be called when the negotiated version is TLS 1.3.
		NetworkResult postHandshakeProbe();

		SSL_CTX* _context{ nullptr };
		SSL* _ssl{ nullptr };
		const char* _hostname{ nullptr };  // points to NetworkConfiguration._ip (externally owned)
	};

	using ClientActiveSocketContainer = SocketContainer<ClientActiveSocket, TlsClientActiveSocket>;
}

#endif // __BN3MONKEY__CLIENTACTIVESOCKET__