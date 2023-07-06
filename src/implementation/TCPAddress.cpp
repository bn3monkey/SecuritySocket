#include "TCPAddress.hpp"

Bn3Monkey::TCPAddress::TCPAddress(const std::string& ip, const std::string& port)
{
	addrinfo hints;
	::memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0; // ACTIVTE

	const char* node{ ip.empty() ? 0 : ip.c_str() };
	const char* service{ port.empty() ? 0 : port.c_str() };

	addrinfo* address{ nullptr };
	auto ret = getaddrinfo(node, service, &hints, &address);
	if (ret != 0)
	{
		const char* error_description = gai_strerror(ret);
		switch (ret)
		{
		case EAI_AGAIN:
		case EAI_FAIL:
		case EAI_MEMORY:
		case EAI_NONAME:
		case EAI_SERVICE:
			_result = ConnectionResult{ ConnectionCode::ADDRESS_NOT_AVAILABLE, error_description };
		}
		return;
	}

	_socket_address_size = address->ai_addrlen;
	::memcpy(_socket_address, address->ai_addr, address->ai_addrlen);
	freeaddrinfo(address);
}
Bn3Monkey::TCPAddress::~TCPAddress()
{

}