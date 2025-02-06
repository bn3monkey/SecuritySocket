#include "SocketAddress.hpp"
#include <cstring>

bool Bn3Monkey::SocketAddress::checkUnixDomain(const char* ip)
{
	static constexpr char* domain_prefix =
	#if defined(_WIN32)
		"\\\\.\\pipe\\";
	#elif defined(__linux__)
		"/tmp/";
	#else
		"invalid";
	#endif 
	return !strncmp(ip, domain_prefix, strlen(domain_prefix));
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
		switch (ret)
		{
		case EAI_AGAIN:
		case EAI_FAIL:
		case EAI_MEMORY:
		case EAI_NONAME:
		case EAI_SERVICE:
			_result = SocketResult{ SocketCode::ADDRESS_NOT_AVAILABLE };
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
