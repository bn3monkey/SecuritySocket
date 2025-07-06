#if !defined(__BN3MONKEY_SOCKETADDRESS__)
#define __BN3MONKEY_SOCKETADDRESS__

#include "../SecuritySocket.hpp"

#if defined(_WIN32)
#include <Winsock2.h>
#include <WS2tcpip.h>
#else
#include <netdb.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#endif


namespace Bn3Monkey
{
	class SocketAddress
	{
	public:
		static bool checkUnixDomain(const char* ip);

		explicit SocketAddress() {};
		explicit SocketAddress(const char* ip, const char* port, bool is_server);
		virtual ~SocketAddress();

		operator const SocketResult&() const { return _result; }

		inline const sockaddr* address() const {
			return reinterpret_cast<const sockaddr *>(_socket_address);
		}
		inline socklen_t size() const {
			return static_cast<socklen_t>(_socket_address_size);
		}

		inline bool isUnixDomain() const {
			return _is_unix_domain;
		}

	private:
		SocketResult _result;

		size_t _socket_address_size{ 0 };
		uint8_t _socket_address[512]{ 0 };
		bool _is_unix_domain { false};
	};

}

#endif // __BN3MONKEY_SOCKETADDRESS__
