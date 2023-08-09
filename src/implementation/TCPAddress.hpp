#if !defined(__BN3MONKEY__TCPADDRESS__)
#define __BN3MONKEY__TCPADDRESS__

#include "../SecuritySocket.hpp"
#include <vector>

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
	class TCPAddress
	{
	public:
		explicit TCPAddress(const std::string& ip, const std::string& port, bool is_server);
		virtual ~TCPAddress();

		operator const ConnectionResult&() const { return _result; }

		inline const sockaddr* address() const {
			return reinterpret_cast<const sockaddr *>(_socket_address);
		}
		inline socklen_t size() const {
			return static_cast<socklen_t>(_socket_address_size);
		}

	private:
		ConnectionResult _result;

		size_t _socket_address_size{ 0 };
		uint8_t _socket_address[512]{ 0 };
	};
}

#endif