#include "PassiveSocket.hpp"

#include "SocketResult.hpp"
#include "SocketHelper.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <Winsock2.h>
#include <WS2tcpip.h>
#include <mswsock.h>
#else
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#endif

using namespace Bn3Monkey;

#if defined(_WIN32)
static inline void loadAcceptExFunction(int32_t socket, LPFN_ACCEPTEX* function_ref)
{
    GUID guid_acceptEx = WSAID_ACCEPTEX;
    DWORD bytesReturned;

    WSAIoctl(socket, 
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guid_acceptEx,
        sizeof(guid_acceptEx),
        function_ref,
        sizeof(LPFN_ACCEPTEX),
        &bytesReturned,
        NULL,
        NULL);
}
#endif

PassiveSocket::PassiveSocket(bool is_unix_domain)
{
	if (is_unix_domain)
		_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
	else
		_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_socket < 0)
	{
		_result = createResult(_socket);
		return;
	}
    
	setNonBlockingMode(_socket);
}
void PassiveSocket::close()
{
#ifdef _WIN32
    shutdown(_socket, SD_BOTH);
#else
    shutdown(_socket, SHUT_RDWR);
#endif
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
	_socket = -1;	
}

SocketResult PassiveSocket::bind(const SocketAddress& address)
{
    SocketResult res;


    int opt = 1;
    setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    int ret = ::bind(_socket, address.address(), address.size());
    if (ret < 0) {
        res = createResult(ret);
    }
    return res;
}
SocketResult PassiveSocket::listen()
{
    SocketResult res;

    auto ret = ::listen(_socket, SOMAXCONN);
    if (ret < 0)
    {
        res = createResult(ret);
    }
    return res;
}



ServerActiveSocketContainer PassiveSocket::accept()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int sock = ::accept(_socket, (struct sockaddr*)&client_addr, &client_len);
    ServerActiveSocketContainer container{false, sock, (void*)&client_addr, nullptr};
    return container;   
}


TLSPassiveSocket::TLSPassiveSocket(bool is_unix_domain)
{
    throw std::runtime_error("Not Implemented");
}
void TLSPassiveSocket::close()
{
    throw std::runtime_error("Not Implemented");
}
SocketResult TLSPassiveSocket::bind(const SocketAddress& address)
{
    throw std::runtime_error("Not Implemented");
}
SocketResult TLSPassiveSocket::listen()
{
    throw std::runtime_error("Not Implemented");
}
ServerActiveSocketContainer TLSPassiveSocket::accept()
{
    throw std::runtime_error("Not Implemented");
}