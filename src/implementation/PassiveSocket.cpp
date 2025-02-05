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

SocketResult PassiveSocket::open()
{
	SocketResult result;

	if (_address.isUnixDomain())
		_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
	else
		_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_socket < 0)
	{
		result = createResult(_socket);
		return result;
	}
    
	setNonBlockingMode(_socket);

	return result;
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


SocketResult PassiveSocket::listen(size_t num_of_clients)
{
    SocketResult res;
    _num_of_clients = num_of_clients;

    int ret = ::bind(_socket, _address.address(), _address.size());
    if (ret == SOCKET_ERROR)
    {
        res = SocketResult(SocketCode::SOCKET_BIND_FAILED, "Bind Fail");
    }

    ret = ::listen(_socket, num_of_clients);
    if (!ret)
    {
        res = SocketResult(SocketCode::SOCKET_LISTEN_FAILED, "Listen Fail");
    }

    return res;
}

SocketConnection PassiveSocket::accept()
{
    SocketConnection result;

    result.result = SocketResult(SocketCode::SUCCESS);

    {
        int sock = ::accept(_socket, NULL, NULL);
        if (result.event.sock < 0)
        {
            result.result = createResult(result.event.sock);
            if (result.result.code() != SocketCode::SOCKET_CONNECTION_IN_PROGRESS)
                return result;
        }
        setNonBlockingMode(result.event.sock);
    }

    return result;
}


SocketResult TLSPassiveSocket::open()
{
    throw std::runtime_error("Not Implemented");
}
void TLSPassiveSocket::close()
{
    throw std::runtime_error("Not Implemented");
}
SocketResult TLSPassiveSocket::listen(size_t num_of_clients)
{
    throw std::runtime_error("Not Implemented");
}
SocketEventListResult TLSPassiveSocket::poll(uint32_t timeout_ms)
{
    throw std::runtime_error("Not Implemented");
}
SocketEventResult TLSPassiveSocket::accept()
{
    throw std::runtime_error("Not Implemented");
}
int32_t TLSPassiveSocket::read(int32_t client_idx, void* buffer, size_t size)
{
    throw std::runtime_error("Not Implemented");
}
int32_t TLSPassiveSocket::write(int32_t client_idx, const void* buffer, size_t size) 
{
    throw std::runtime_error("Not Implemented");
}
void TLSPassiveSocket::drop(int32_t client_idx)
{
    throw std::runtime_error("Not Implemented");
}