#include "PassiveSocket.hpp"

#include "SocketResult.hpp"
#include "SocketHelper.hpp"

#include <stdexcept>

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
	::closesocket(_socket);
#else
	::close(_socket);
#endif
	_socket = -1;	
}

SocketResult PassiveSocket::bind(const SocketAddress& address)
{
    SocketResult res;

    int ret = ::bind(_socket, address.address(), address.size());
    if (ret == SOCKET_ERROR)
    {
        res = SocketResult(SocketCode::SOCKET_BIND_FAILED);
    }
    return res;
}
SocketResult PassiveSocket::listen()
{
    SocketResult res;

    auto ret = ::listen(_socket, SOMAXCONN);
    if (ret == SOCKET_ERROR)
    {
        res = SocketResult(SocketCode::SOCKET_LISTEN_FAILED);
    }
    return res;
}



ServerActiveSocketContainer PassiveSocket::accept()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int sock = ::accept(_socket, (struct sockaddr*)&client_addr, &client_len);
    ServerActiveSocketContainer container{false, sock, &client_addr};
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