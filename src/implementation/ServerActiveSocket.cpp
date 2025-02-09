#include "ServerActiveSocket.hpp"
#include "SocketResult.hpp"
#include "SocketHelper.hpp"
#include <stdexcept>
using namespace Bn3Monkey;

ServerActiveSocket::ServerActiveSocket(int32_t sock, struct sockaddr_in* addr)
{
    _socket = sock;
    _result = createResult(_socket);
    if (_result.code() != SocketCode::SUCCESS)
    {
        return;
    }

	if (sock >= 0) {
		
		inet_ntop(AF_INET, &(addr->sin_addr), _client_ip, sizeof(_client_ip));
		_client_port = ntohs(addr->sin_port);
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
int ServerActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), size, 0);
	return ret;
}
int ServerActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret{0};
#ifdef __linux__
	ret = send(_socket, buffer, size, MSG_NOSIGNAL);
#else
	ret = send(_socket, static_cast<const char*>(buffer), size, 0);
#endif
	return ret;
}

TLSServerActiveSocket::TLSServerActiveSocket(SSL_CTX* ctx, int32_t sock)
{
	throw std::runtime_error("Not Implemented");
}
TLSServerActiveSocket::TLSServerActiveSocket::~TLSServerActiveSocket()
{
	throw std::runtime_error("Not Implemented");
}

void TLSServerActiveSocket::close()
{
	throw std::runtime_error("Not Implemented");
}
int TLSServerActiveSocket::read(void* buffer, size_t size)
{
	throw std::runtime_error("Not Implemented");
}
int TLSServerActiveSocket::write(const void* buffer, size_t size)
{
	throw std::runtime_error("Not Implemented");
}