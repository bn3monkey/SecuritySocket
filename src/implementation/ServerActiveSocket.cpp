#include "ServerActiveSocket.hpp"
#include "SocketResult.hpp"
#include "SocketHelper.hpp"
#include <stdexcept>

#ifdef _WIN32
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
#include <arpa/inet.h> // inet_ntop
#endif

using namespace Bn3Monkey;

ServerActiveSocket::ServerActiveSocket(int32_t sock, void* addr, void* ssl_context)
{
    struct sockaddr_in* address = (struct sockaddr_in*)addr;
    _socket = sock;
    _result = createResult(_socket);
    if (_result.code() != SocketCode::SUCCESS)
    {
        return;
    }

	if (sock >= 0) {
		
		inet_ntop(AF_INET, &(address->sin_addr), _client_ip, sizeof(_client_ip));
		_client_port = ntohs(address->sin_port);
		// printf("Connected client ip : %s port : %d\n", _client_ip, _client_port);
	}

    setNonBlockingMode(_socket);
}
ServerActiveSocket::~ServerActiveSocket()
{
    // close();
}
void ServerActiveSocket::close()
{
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
    _socket = -1;
}
SocketResult ServerActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), size, 0);
	return createResult(ret);
}
SocketResult ServerActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret{0};
#ifdef __linux__
	ret = send(_socket, buffer, size, MSG_NOSIGNAL);
#else
	ret = send(_socket, static_cast<const char*>(buffer), size, 0);
#endif
return createResult(ret);
}

void ServerActiveSocket::setSocketBufferSize(size_t size)
{
	setsockopt(_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), sizeof(size));
	setsockopt(_socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size), sizeof(size));
}

TLSServerActiveSocket::TLSServerActiveSocket(int32_t sock, void* addr, void* ssl_context)
{
	throw std::runtime_error("Not Implemented");
}
TLSServerActiveSocket::TLSServerActiveSocket::~TLSServerActiveSocket()
{
	printf("Not Implemeneted\n");
}

void TLSServerActiveSocket::close()
{
	throw std::runtime_error("Not Implemented");
}
SocketResult TLSServerActiveSocket::read(void* buffer, size_t size)
{
	throw std::runtime_error("Not Implemented");
}
SocketResult TLSServerActiveSocket::write(const void* buffer, size_t size)
{
	throw std::runtime_error("Not Implemented");
}
