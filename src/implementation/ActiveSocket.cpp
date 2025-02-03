#include "ActiveSocket.hpp"

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

SocketResult ActiveSocket::open(const SocketAddress& address)
{
	SocketResult result;

	_address = address;

	if (address.isUnixDomain())
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
void ActiveSocket::close()
{
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
	_socket = -1;	
}
SocketResult ActiveSocket::listen(size_t num_of_clients)
{
    _num_of_clients = num_of_clients;

    int ret = ::bind(_socket, _address.address(), _address.size());
    if (ret == SOCKET_ERROR)
    {
        return SocketResult(SocketCode::SOCKET_BIND_FAILED, "Bind Fail");
    }

    ret = ::listen(_socket, SOMAXCONN);
    if (!ret)
    {
        return SocketResult(SocketCode::SOCKET_LISTEN_FAILED, "Listen Fail");
    }

#if defined(_WIN32)
    window_pollhandle = CreateIoCompletionPort((HANDLE)_socket, NULL, (ULONG_PTR)_socket, 0);
    if (window_pollhandle == nullptr)
    {
        return SocketResult(SocketCode::POLL_OBJECT_NOT_CREATED, "Poll object is not created");
    }

    loadAcceptExFunction(_socket, &acceptEx);
#else
    linux_pollhandle = epoll_create1(0);
    if (linux_pollhandle < 0)
    {
        return SocketResult(SocketCode::POLL_OBJECT_NOT_CREATED, "Poll object is not created");
    }

    epoll_event event;
    event.data.fd = _socket;
    event.evetns = EPOLLIN;

    if (epoll_ctl(linux_pollhandle, EPOLL_CTL_ADD, _socket, &event) < 0)
    {
        return SocketResult(SocketCode::POLL_EVENT_CANNOT_ADDED, "Listening Event cannot be added");
    }
#endif

    return SocketResult(SocketCode::SUCCESS);
}
ActivePollResult ActiveSocket::poll(uint32_t timeout_ms)
{
    ActivePollResult result;

#ifdef _WIN32
    OVERLAPPED_ENTRY entries[MAX_CLIENTS];
    ULONG num_entries_removed;

    BOOL success = GetQueuedCompletionStatusEx(window_pollhandle, entries, _num_of_clients, &num_entries_removed, timeout_ms, FALSE);
    if (!success)
    {
        result.result = SocketResult(SocketCode::POLL_ERROR, "Poll error");
        return result;
    }

    result.length = num_entries_removed;
    for (size_t i=0;i<num_entries_removed;i++)
    {
        auto& entry = entries[i];
        int32_t sock = static_cast<int32_t>(entry.lpCompletionKey);
        result.sockets[i] = sock;
        result.types[i] = sock == _socket ? ActivePollType::ACCEPT : ActivePollType::READ;
    }

#else
    
#endif

    return result;
}
int32_t ActiveSocket::accept()
{

}
int32_t ActiveSocket::read(int32_t client_idx, void* buffer, size_t size)
{

}
int32_t ActiveSocket::write(int32_t client_idx, const void* buffer, size_t size) 
{

}
void ActiveSocket::drop(int32_t client_idx)
{

}


SocketResult TLSActiveSocket::open(const SocketAddress& address)
{
    throw std::runtime_error("Not Implemented");
}
void TLSActiveSocket::close()
{
    throw std::runtime_error("Not Implemented");
}
SocketResult TLSActiveSocket::listen(size_t num_of_clients)
{
    throw std::runtime_error("Not Implemented");
}
ActivePollResult TLSActiveSocket::poll(uint32_t timeout_ms)
{
    throw std::runtime_error("Not Implemented");
}
int32_t TLSActiveSocket::accept()
{
    throw std::runtime_error("Not Implemented");
}
int32_t TLSActiveSocket::read(int32_t client_idx, void* buffer, size_t size)
{
    throw std::runtime_error("Not Implemented");
}
int32_t TLSActiveSocket::write(int32_t client_idx, const void* buffer, size_t size) 
{
    throw std::runtime_error("Not Implemented");
}
void TLSActiveSocket::drop(int32_t client_idx)
{
    throw std::runtime_error("Not Implemented");
}