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
		PassiveSocket(bool is_unix_domain = false);
		virtual void close();

		virtual NetworkResult bind(const SocketAddress& address);
		virtual NetworkResult listen();
		virtual ServerActiveSocketContainer accept();

	private:
	};

	class TLSPassiveSocket : public PassiveSocket
	{
	public:
		TLSPassiveSocket(bool is_unix_domain = false);
		virtual void close();

		virtual NetworkResult bind(const SocketAddress& address);
		virtual NetworkResult listen();
		virtual ServerActiveSocketContainer accept();
	};

	using PassiveSocketContainer = SocketContainer<PassiveSocket, TLSPassiveSocket>;
}

#endif // __BN3MONKEY__PASSIVESOCKET__