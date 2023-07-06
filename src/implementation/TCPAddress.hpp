#if !defined(__BN3MONKEY__TCPADDRESS__)
#define __BN3MONKEY__TCPADDRESS__

#include "../SecuritySocket.hpp"
#include <vector>

namespace Bn3Monkey
{
	class TCPAddress
	{
	public:
		explicit TCPAddress(const std::string& ip, const std::string& port);
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