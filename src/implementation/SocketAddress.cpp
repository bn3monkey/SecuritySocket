#include "SocketAddress.hpp"
#include <cstring>

static inline bool checkUnixDomain(const char* ip)
{
	static constexpr char* unix_domain_prefix = "/tmp/";
	return !strncmp(ip, unix_domain_prefix, strlen(unix_domain_prefix));
}

Bn3Monkey::SocketAddress::SocketAddress(const char* ip, const char* port, bool is_server)
{
	_is_unix_domain = checkUnixDomain(ip);
	if (_is_unix_domain)
	{
		struct sockaddr_un* addr = new (_socket_address) struct sockaddr_un;
		addr->sun_family = AF_UNIX;
		strncpy(addr->sun_path, ip, strlen(ip));
		_socket_address_size = sizeof(struct sockaddr_un);
		return;
	}

	addrinfo hints;
	::memset(&hints, 0, sizeof(hints));

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = is_server ? AI_PASSIVE : 0; // ACTIVTE

	addrinfo* address{ nullptr };
	auto ret = getaddrinfo(ip, port, &hints, &address);
	if (ret != 0)
	{
#if defined(_WIN32)
		const char* error_description = gai_strerrorA(ret);
#else
		const char* error_description = gai_strerror(ret);
#endif
		switch (ret)
		{
		case EAI_AGAIN:
		case EAI_FAIL:
		case EAI_MEMORY:
		case EAI_NONAME:
		case EAI_SERVICE:
			_result = SocketResult{ SocketCode::ADDRESS_NOT_AVAILABLE, error_description };
		}
		return;
	}

	_socket_address_size = address->ai_addrlen;
	::memcpy(_socket_address, address->ai_addr, address->ai_addrlen);
	freeaddrinfo(address);
}
Bn3Monkey::SocketAddress::~SocketAddress()
{

}
