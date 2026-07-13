#if !defined(__BN3MONKEY__PASSIVESOCKET__)
#define __BN3MONKEY__PASSIVESOCKET__

#include "../SecuritySocket.hpp"
#include "SocketAddress.hpp"
#include "BaseSocket.hpp"
#include "ServerActiveSocket.hpp"

#include <cstdint>

#include "TlsHelper.hpp"



namespace Bn3Monkey {
	
	class PassiveSocket  : public BaseSocket {
	public:
		// tls_configuration is ignored by the plain socket. It is accepted so that
		// SocketContainer can forward one argument list to either socket type
		// (ClientActiveSocket does the same with TlsClientConfiguration).
		PassiveSocket(bool is_unix_domain, const TlsServerConfiguration& tls_configuration);

		// Virtual so SocketContainer's stored type is destroyed correctly and so
		// clang stops warning (-Wdelete-non-abstract-non-virtual-dtor) about a
		// non-final class with virtual functions but a non-virtual destructor.
		// Does NOT close the fd — see the ownership note in BaseSocket.hpp.
		virtual ~PassiveSocket() = default;

		// SocketContainer is move-only. The source's fd is stolen so the
		// moved-from object can never close a descriptor we still own.
		PassiveSocket(PassiveSocket&& other) noexcept {
			_socket = other._socket;
			_result = other._result;
			other._socket = -1;
		}
		PassiveSocket(const PassiveSocket&) = delete;
		PassiveSocket& operator=(const PassiveSocket&) = delete;

		virtual void close();

		virtual NetworkResult bind(const SocketAddress& address);
		virtual NetworkResult listen();
		virtual ServerActiveSocketContainer accept();

	private:
	};

	class TLSPassiveSocket : public PassiveSocket
	{
	public:
		TLSPassiveSocket(bool is_unix_domain, const TlsServerConfiguration& tls_configuration);
		virtual ~TLSPassiveSocket() = default;

		TLSPassiveSocket(TLSPassiveSocket&& other) noexcept
			: PassiveSocket(std::move(other)), _context(other._context)
		{
			other._context = nullptr;   // only one owner may SSL_CTX_free()
		}

		void close() override;

		NetworkResult bind(const SocketAddress& address) override;
		NetworkResult listen() override;
		ServerActiveSocketContainer accept() override;

	private:
		// Shared by every connection this socket accepts. SSL_new() takes a
		// reference, so freeing it in close() while sessions are still live is safe.
		SSL_CTX* _context{ nullptr };
	};

	using PassiveSocketContainer = SocketContainer<PassiveSocket, TLSPassiveSocket>;
}

#endif // __BN3MONKEY__PASSIVESOCKET__