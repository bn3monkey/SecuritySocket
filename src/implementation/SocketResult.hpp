#if !defined(__BN3MONKEY_SOCKETRESULT__)
#define __BN3MONKEY_SOCKETRESULT__

#include "../SecuritySocket.hpp"
#include <windows.h>
#include <openssl/ssl.h>

using namespace Bn3Monkey;

inline SocketResult createResult(int operation_return)
{
	if (operation_return > 0)
	{
		return SocketResult(SocketCode::SUCCESS);
	}

#ifdef _WIN32
	int error = WSAGetLastError();
	
	switch (error)
	{
		// Socket initialization error
	case WSAEACCES:
		return SocketResult(SocketCode::SOCKET_PERMISSION_DENIED);
	case WSAEAFNOSUPPORT:
		return SocketResult(SocketCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED);
	case WSAEINVAL:
		return SocketResult(SocketCode::SOCKET_INVALID_ARGUMENT);
	case WSAEMFILE:
		return SocketResult(SocketCode::SOCKET_CANNOT_CREATED);
	case WSAENOBUFS :
		return SocketResult(SocketCode::SOCKET_CANNOT_ALLOC);
	case WSAETIMEDOUT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_NOT_RESPOND);
	case WSAEADDRINUSE:
		return SocketResult(SocketCode::SOCKET_CONNECTION_ADDRESS_IN_USE);
	case WSAEBADF :
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_DESCRIPTOR);
	case WSAECONNREFUSED:
		return SocketResult(SocketCode::SOCKET_CONNECTION_REFUSED);
	case WSAEFAULT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_ADDRESS);
	case WSAENETUNREACH:
		return SocketResult(SocketCode::SOCKET_CONNECTION_UNREACHED);
	case WSAEINTR:
		return SocketResult(SocketCode::SOCKET_CONNECTION_INTERRUPTED);
	case WSAEALREADY:
		return SocketResult(SocketCode::SOCKET_ALREADY_CONNECTED);
	case WSAEINPROGRESS:
		return SocketResult(SocketCode::SOCKET_CONNECTION_IN_PROGRESS);
	case WSAEWOULDBLOCK:
	case WSATRY_AGAIN:
		return SocketResult(SocketCode::SOCKET_HAS_NO_DATA);
	}
#else
	int error = errno;
	switch (error)
	{
		// Socket initialization error
	case EACCES:
		return SocketResult(SocketCode::SOCKET_PERMISSION_DENIED);
	case EAFNOSUPPORT:
		return SocketResult(SocketCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED);
	case EINVAL:
		return SocketResult(SocketCode::SOCKET_INVALID_ARGUMENT);
	case EMFILE:
		return SocketResult(SocketCode::SOCKET_CANNOT_CREATED);
	case ENOBUFS:
		return SocketResult(SocketCode::SOCKET_CANNOT_ALLOC);
	case ETIMEDOUT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_NOT_RESPOND);
	case EADDRINUSE:
		return SocketResult(SocketCode::SOCKET_CONNECTION_ADDRESS_IN_USE);
	case EBADF:
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_DESCRIPTOR);
	case ECONNREFUSED:
		return SocketResult(SocketCode::SOCKET_CONNECTION_REFUSED);
	case EFAULT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_ADDRESS);
	case ENETUNREACH:
		return SocketResult(SocketCode::SOCKET_CONNECTION_UNREACHED);
	case EINTR:
		return SocketResult(SocketCode::SOCKET_CONNECTION_INTERRUPTED);
	case EALREADY:
		return SocketResult(SocketCode::SOCKET_CONNECTION_ALREADY);
	case EINPROGRESS:
		return SocketResult(SocketCode::SOCKET_CONNECTION_IN_PROGRESS);
	}
#endif
	return SocketResult(SocketCode::UNKNOWN_ERROR);
}

inline SocketResult createTLSResult(SSL* ssl, int operation_return)
{
	if (operation_return >= 1)
		return SocketResult(SocketCode::SUCCESS);

	int code = SSL_get_error(ssl, operation_return);
	switch (code)
	{
	case SSL_ERROR_SSL:
		return SocketResult(SocketCode::SSL_PROTOCOL_ERROR);
	case SSL_ERROR_SYSCALL:
		// System Error / TCP Error
		return createResult(operation_return);
	case SSL_ERROR_ZERO_RETURN:
		return SocketResult(SocketCode::SSL_ERROR_CLOSED_BY_PEER);
	}
	return SocketResult(SocketCode::UNKNOWN_ERROR);
}

const char* getMessage(const SocketCode& code)
{
    switch (code)
    {
        case SocketCode::SUCCESS: return "Success";
        case SocketCode::ADDRESS_NOT_AVAILABLE: return "Address not available";

        case SocketCode::WINDOWS_SOCKET_INITIALIZATION_FAIL: return "Windows socket initialization failed";
        case SocketCode::TLS_CONTEXT_INITIALIZATION_FAIL: return "TLS context initialization failed";
        case SocketCode::TLS_INITIALIZATION_FAIL: return "TLS initialization failed";

        // - SOCKET INITIALIZATION
        case SocketCode::SOCKET_PERMISSION_DENIED: return "The process does not have permission to create the socket.";
        case SocketCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED: return "Address Faimily is not supported in host system";
        case SocketCode::SOCKET_INVALID_ARGUMENT: return "An invalid argument was used when creating the socket.";
        case SocketCode::SOCKET_CANNOT_CREATED: return "No more sockets can be created because the file descriptor limit has been reached.";
        case SocketCode::SOCKET_CANNOT_ALLOC: return "No more sockets can be created due to insufficient memory.";
        case SocketCode::SOCKET_OPTION_ERROR: return "Socket option error occurred";

        // - SOCKET CONNECTION
        case SocketCode::SOCKET_CONNECTION_NOT_RESPOND: return "Connected host is not responding to requests.";
        case SocketCode::SOCKET_CONNECTION_ADDRESS_IN_USE: return "Socket connection address already in use";
        case SocketCode::SOCKET_CONNECTION_BAD_DESCRIPTOR: return "Invalid socket descriptor";
        case SocketCode::SOCKET_CONNECTION_REFUSED: return "The connection was refused, or the server did not accept the connection request.";
        case SocketCode::SOCKET_CONNECTION_BAD_ADDRESS: return "Bad address for socket connection";
        case SocketCode::SOCKET_CONNECTION_UNREACHED: return "Socket connection unreachable";
        case SocketCode::SOCKET_CONNECTION_INTERRUPTED: return "Socket connection interrupted";

        case SocketCode::SOCKET_ALREADY_CONNECTED: return "Socket is already connected";
        case SocketCode::SOCKET_CONNECTION_IN_PROGRESS: return "Socket connection in progress";
        case SocketCode::SOCKET_HAS_NO_DATA: return "Socket has no data. call the io function again";

        case SocketCode::SOCKET_BIND_FAILED: return "Socket binding failed";
        case SocketCode::SOCKET_LISTEN_FAILED: return "Socket listen failed";

        case SocketCode::TLS_SETFD_ERROR: return "Failed to set file descriptor for TLS";

        case SocketCode::SSL_PROTOCOL_ERROR: return "SSL protocol error occurred";
        case SocketCode::SSL_ERROR_CLOSED_BY_PEER: return "SSL connection closed by peer";

        case SocketCode::SOCKET_TIMEOUT: return "Socket operation timed out";
        case SocketCode::SOCKET_CLOSED: return "Socket is closed";

        case SocketCode::SOCKET_EVENT_ERROR: return "Socket event error occurred";
        case SocketCode::SOCKET_EVENT_OBJECT_NOT_CREATED: return "Socket event object could not be created";
        case SocketCode::SOCKET_EVENT_CANNOT_ADDED: return "Failed to add socket event";

        case SocketCode::SOCKET_SERVER_ALREADY_RUNNING: return "Socket server is already running";

        case SocketCode::UNKNOWN_ERROR: return "Unknown socket error occurred";

        case SocketCode::LENGTH: return "Invalid socket code (LENGTH should not be used)";

        default: return "Unhandled socket error code";
    }
}


#endif // __BN3MONKEY_SOCKETRESULT__