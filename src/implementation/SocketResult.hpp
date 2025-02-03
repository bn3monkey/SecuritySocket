#if !defined(__BN3MONKEY_SOCKETRESULT__)
#defined __BN3MONKEY_SOCKETRESULT__

#include "../SecuritySocket.hpp"
#include <windows.h>
#include <openssl/ssl.h>

using namespace Bn3Monkey;

static inline SocketResult createResult(int operation_return)
{
	if (operation_return >= 0)
	{
		return SocketResult(SocketCode::SUCCESS);
	}

#ifdef _WIN32
	int error = WSAGetLastError();
	
	switch (error)
	{
		// Socket initialization error
	case WSAEACCES:
		return SocketResult(SocketCode::SOCKET_PERMISSION_DENIED,
			"The process does not have permission to create the socket.");
	case WSAEAFNOSUPPORT:
		return SocketResult(SocketCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED,
			"Address Faimily is not supported in host system");
	case WSAEINVAL:
		return SocketResult(SocketCode::SOCKET_INVALID_ARGUMENT,
			"An invalid argument was used when creating the socket.");
	case WSAEMFILE:
		return SocketResult(SocketCode::SOCKET_CANNOT_CREATED,
			"No more sockets can be created because the file descriptor limit has been reached.");
	case WSAENOBUFS :
		return SocketResult(SocketCode::SOCKET_CANNOT_ALLOC,
			"No more sockets can be created due to insufficient memory.");

	case WSAETIMEDOUT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_NOT_RESPOND,
			"Connected host is not responding to requests.");
	case WSAEADDRINUSE:
		return SocketResult(SocketCode::SOCKET_CONNECTION_ADDRESS_IN_USE,
			"Address is already used for another socket connection");
	case WSAEBADF :
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_DESCRIPTOR,
			"Bad descriptor is used for socket connection");
	case WSAECONNREFUSED:
		return SocketResult(SocketCode::SOCKET_CONNECTION_REFUSED,
			"The connection was refused, or the server did not accept the connection request.");
	case WSAEFAULT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_ADDRESS,
			"Socket connection uses bad address");
	case WSAENETUNREACH:
		return SocketResult(SocketCode::SOCKET_CONNECTION_UNREACHED,
			"The network is in an unavailable state.");
	case WSAEINTR:
		return SocketResult(SocketCode::SOCKET_CONNECTION_INTERRUPTED,
			"Socket connection was interrupted.");
	case WSAEALREADY:
		return SocketResult(SocketCode::SOCKET_ALREADY_CONNECTED,
			"Socket is already connected");
	case WSAEINPROGRESS:
		return SocketResult(SocketCode::SOCKET_CONNECTION_IN_PROGRESS,
			"");
	case WSAEWOULDBLOCK:
		return SocketResult(SocketCode::SOCKET_CONNECTION_IN_PROGRESS, "");
	}
#else
	int error = errno;
	switch (error)
	{
		// Socket initialization error
	case EACCES:
		return SocketResult(SocketCode::SOCKET_PERMISSION_DENIED,
			"The process does not have permission to create the socket.");
	case EAFNOSUPPORT:
		return SocketResult(SocketCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED,
			"Address Faimily is not supported in host system");
	case EINVAL:
		return SocketResult(SocketCode::SOCKET_INVALID_ARGUMENT,
			"An invalid argument was used when creating the socket.");
	case EMFILE:
		return SocketResult(SocketCode::SOCKET_CANNOT_CREATED,
			"No more sockets can be created because the file descriptor limit has been reached.");
	case ENOBUFS:
		return SocketResult(SocketCode::SOCKET_CANNOT_ALLOC,
			"No more sockets can be created due to insufficient memory.");

	case ETIMEDOUT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_NOT_RESPOND,
			"Connected host is not responding to requests.");
	case EADDRINUSE:
		return SocketResult(SocketCode::SOCKET_CONNECTION_ADDRESS_IN_USE,
			"Address is already used for another socket connection");
	case EBADF:
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_DESCRIPTOR,
			"Bad descriptor is used for socket connection");
	case ECONNREFUSED:
		return SocketResult(SocketCode::SOCKET_CONNECTION_REFUSED,
			"The connection was refused, or the server did not accept the connection request.");
	case EFAULT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_ADDRESS,
			"Socket connection uses bad address");
	case ENETUNREACH:
		return SocketResult(SocketCode::SOCKET_CONNECTION_UNREACHED,
			"The network is in an unavailable state.");
	case EINTR:
		return SocketResult(SocketCode::SOCKET_CONNECTION_INTERRUPTED,
			"Socket connection was interrupted.");
	case EALREADY:
		return SocketResult(SocketCode::SOCKET_CONNECTION_ALREADY,
			"Socket is already connected");
	case EINPROGRESS:
		return SocketResult(SocketCode::SOCKET_CONNECTION_IN_PROGRESS,
			"");
	}
#endif
	return SocketResult(SocketCode::UNKNOWN_ERROR, "");
}

static inline SocketResult createTLSResult(SSL* ssl, int operation_return)
{
	if (operation_return >= 1)
		return SocketResult(SocketCode::SUCCESS);

	int code = SSL_get_error(ssl, operation_return);
	switch (code)
	{
	case SSL_ERROR_SSL:
		return SocketResult(SocketCode::SSL_PROTOCOL_ERROR, "SSL Protocol Error");
	case SSL_ERROR_SYSCALL:
		// System Error / TCP Error
		return createResult(operation_return);
	case SSL_ERROR_ZERO_RETURN:
		return SocketResult(SocketCode::SSL_ERROR_CLOSED_BY_PEER, "the connection has been terminated by peer");
	}
	return SocketResult(SocketCode::UNKNOWN_ERROR);
}


#endif // __BN3MONKEY_SOCKETRESULT__