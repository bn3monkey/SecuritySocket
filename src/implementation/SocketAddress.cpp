#include "SocketAddress.hpp"
#include <cstring>
#include <cstdio>

#ifdef __linux__
#include <netinet/in.h>
#endif

#ifdef _WIN32
	#if defined(__MINGW32__) || defined(__MINGW64__)
	#define UNIX_PATH_MAX 108
	typedef struct sockaddr_un
	{
		ADDRESS_FAMILY sun_family;     /* AF_UNIX */
		char sun_path[UNIX_PATH_MAX];  /* pathname */
	} SOCKADDR_UN, *PSOCKADDR_UN;

	#define SIO_AF_UNIX_GETPEERPID _WSAIOR(IOC_VENDOR, 256) // Returns ULONG PID of the connected peer process
	
	#else
#include <afunix.h>
	#endif
#endif

Bn3Monkey::SocketAddress::SocketAddress(const char* ip, const char* port, bool is_server, bool is_unix_domain) : _is_unix_domain(is_unix_domain)
{
	if (_is_unix_domain)
	{
		struct sockaddr_un* addr = new (_socket_address) struct sockaddr_un;
		addr->sun_family = AF_UNIX;
		
		snprintf(addr->sun_path, sizeof(addr->sun_path), "%s", ip);
		_socket_address_size = sizeof(struct sockaddr_un);
		return;
	}

	addrinfo hints;
	::memset(&hints, 0, sizeof(hints));

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = is_server ? AI_PASSIVE : 0; // ACTIVTE
	const char* input_ip = is_server ? nullptr : ip;

	addrinfo* address{ nullptr };
	auto ret = getaddrinfo(input_ip, port, &hints, &address);
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

	/*
	for (struct addrinfo* p = address; p != NULL; p = p->ai_next) {
		char ipStr[INET6_ADDRSTRLEN];

		if (p->ai_family == AF_INET) {
			struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
			inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, sizeof(ipStr));
		}
		else if (p->ai_family == AF_INET6) {
			struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)p->ai_addr;
			inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipStr, sizeof(ipStr));
		}
		else {
			continue;
		}
		printf("Resolved Address: %s\n", ipStr);
	}
	*/

}
Bn3Monkey::SocketAddress::~SocketAddress()
{

}
