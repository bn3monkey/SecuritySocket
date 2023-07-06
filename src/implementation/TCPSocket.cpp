#include "TCPSocket.hpp"

using namespace Bn3Monkey;

TCPSocket::NonBlockMode::NonBlockMode(const TCPSocket& tcp_socket)  : _socket(tcp_socket._socket), _flags(0) {
#ifdef _WIN32
	unsigned long mode{ 1 };
	ioctlsocket(_socket, FIONBIO, &mode);
#else
	flags = fcntl(_socket, F_GETFL, 0);
	fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
#endif
}
TCPSocket::NonBlockMode::~NonBlockMode() {
#ifdef _WIN32
	unsigned long mode{ 0 };
	ioctlsocket(_socket, FIONBIO, &mode);
#else
	fcntl(_socket, F_SETFL, flags);
#endif
}

Bn3Monkey::TCPSocket::TCPSocket(TCPAddress& address, uint32_t timeout_milliseconds) : _address(address), _timeout_milliseconds(timeout_milliseconds)
{
	_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_socket < 0)
	{
		_result = result(_socket);
		return;
	}

#ifdef _WIN32
	const char* timeout_ref = reinterpret_cast<const char*>(&_timeout_milliseconds);
#elif 
	timeval timeout;
	timeout.tv_sec = (time_t)socket_timeout / (time_t)1000;
	timeout.tv_usec = (suseconds_t)socket_timeout * (suseconds_t)1000 - (suseconds_t)(timeout.tv_sec * (suseconds_t)1000000);
	timeval* timeout_ref = &timeout;
#endif
	if (setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, timeout_ref, sizeof(_timeout_milliseconds)) < 0)
	{
		_result = ConnectionResult(ConnectionCode::SOCKET_OPTION_ERROR, "cannot get socket receive timeout");
	}
	if (setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, timeout_ref, sizeof(_timeout_milliseconds)) < 0)
	{
		_result = ConnectionResult(ConnectionCode::SOCKET_OPTION_ERROR, "cannot get socket send timeout");
	}
}

Bn3Monkey::TCPSocket::~TCPSocket()
{
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
}

ConnectionResult Bn3Monkey::TCPSocket::connect()
{
	ConnectionResult ret;
	{
		NonBlockMode nonblock{ *this };

		int32_t res = ::connect(_socket, _address.address(), _address.size());
		if (res < 0)
		{
			// ERROR
			return result(res);
		}
		else if (res == 0)
		{
			// TIMEOUT
			return ConnectionResult(ConnectionCode::SOCKET_TIMEOUT, "");
		}

		ret = poll(PollType::CONNECT);
		
		if (ret.code == ConnectionCode::SOCKET_TIMEOUT)
		{
			return ConnectionResult(ConnectionCode::SOCKET_TIMEOUT, "");
		}
		else if (ret.code != ConnectionCode::SUCCESS)
		{
			return ret;
		}

		int optval;
		socklen_t optlen = sizeof(optval);
		if (getsockopt(_socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&optval), &optlen) < 0)
		{
			return ConnectionResult(ConnectionCode::SOCKET_OPTION_ERROR, "cannot get socket error");
		}
		if (optval != 0)
		{
			ret = result(optval);
			return ret;
		}
	}
}

void Bn3Monkey::TCPSocket::disconnect()
{
}

int Bn3Monkey::TCPSocket::write(const char* buffer, size_t size)
{
	return 0;
}

int Bn3Monkey::TCPSocket::read(char* buffer, size_t size)
{
	return 0;
}

ConnectionResult Bn3Monkey::TCPSocket::poll(const PollType& polltype)
{
	ConnectionResult ret;

	fd_set set;
	FD_ZERO(&set);
	FD_SET(_socket, &set);

	struct timeval timeout;
	timeout.tv_sec = _timeout_milliseconds % 1000;
	timeout.tv_usec = 1000 * (_timeout_milliseconds - 1000 * timeout.tv_sec);
	
	int32_t res{ 0 };
	switch (polltype)
	{
	case PollType::READ: 
		{
			res = select(_socket + 1, &set, nullptr, nullptr, &timeout);
		}
		break;
	case PollType::WRITE:
	case PollType::CONNECT: 
		{
		res = select(_socket + 1, nullptr, &set, nullptr, &timeout);
		}
		 break;
	}

	if (res < 0)
	{
		ret = ConnectionResult(ConnectionCode::POLL_ERROR, "Polling error");
	}
	else if (res == 0)
	{
		ret = ConnectionResult(ConnectionCode::SOCKET_TIMEOUT);
	}
	else {
		ret = ConnectionResult(ConnectionCode::SUCCESS);
	}
	return ret;
}

ConnectionResult Bn3Monkey::TCPSocket::result(int operation_return)
{
	if (operation_return >= 0)
	{
		return ConnectionResult(ConnectionCode::SUCCESS);
	}

#ifdef _WIN32
	int error = WSAGetLastError();
	
	switch (error)
	{
		// Socket initialization error
	case WSAEACCES:
		return ConnectionResult(ConnectionCode::SOCKET_PERMISSION_DENIED,
			"The process does not have permission to create the socket.");
	case WSAEAFNOSUPPORT:
		return ConnectionResult(ConnectionCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED,
			"Address Faimily is not supported in host system");
	case WSAEINVAL:
		return ConnectionResult(ConnectionCode::SOCKET_INVALID_ARGUMENT,
			"An invalid argument was used when creating the socket.");
	case WSAEMFILE:
		return ConnectionResult(ConnectionCode::SOCKET_CANNOT_CREATED,
			"No more sockets can be created because the file descriptor limit has been reached.");
	case WSAENOBUFS :
		return ConnectionResult(ConnectionCode::SOCKET_CANNOT_ALLOC,
			"No more sockets can be created due to insufficient memory.");


	case WSAEADDRINUSE:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_ADDRESS_IN_USE,
			"Address is already used for another socket connection");
	case WSAEBADF :
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_BAD_DESCRIPTOR,
			"Bad descriptor is used for socket connection");
	case WSAECONNREFUSED:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_REFUSED,
			"The connection was refused, or the server did not accept the connection request.");
	case WSAEFAULT:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_BAD_ADDRESS,
			"Socket connection uses bad address");
	case WSAENETUNREACH:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_UNREACHED,
			"The network is in an unavailable state.");
	case WSAEINTR:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_INTERRUPTED,
			"Socket connection was interrupted.");
	case WSAEALREADY:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_ALREADY,
			"Socket is already connected");
	case WSAEINPROGRESS:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_IN_PROGRESS,
			"");
	}
#else
	int error	= errno;
	switch (error)
	{
		// Socket initialization error
	case EACCES:
		return ConnectionResult(ConnectionCode::SOCKET_PERMISSION_DENIED,
			"The process does not have permission to create the socket.");
	case EAFNOSUPPORT:
		return ConnectionResult(ConnectionCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED,
			"Address Faimily is not supported in host system");
	case EINVAL:
		return ConnectionResult(ConnectionCode::SOCKET_INVALID_ARGUMENT,
			"An invalid argument was used when creating the socket.");
	case EMFILE:
		return ConnectionResult(ConnectionCode::SOCKET_CANNOT_CREATED,
			"No more sockets can be created because the file descriptor limit has been reached.");
	case ENOBUFS:
		return ConnectionResult(ConnectionCode::SOCKET_CANNOT_ALLOC,
			"No more sockets can be created due to insufficient memory.");


	case EADDRINUSE:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_ADDRESS_IN_USE,
			"Address is already used for another socket connection");
	case EBADF:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_BAD_DESCRIPTOR,
			"Bad descriptor is used for socket connection");
	case ECONNREFUSED:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_REFUSED,
			"The connection was refused, or the server did not accept the connection request.");
	case EFAULT:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_BAD_ADDRESS,
			"Socket connection uses bad address");
	case ENETUNREACH:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_UNREACHED,
			"The network is in an unavailable state.");
	case EINTR:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_INTERRUPTED,
			"Socket connection was interrupted.");
	case EALREADY:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_ALREADY,
			"Socket is already connected");
	case EINPROGRESS:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_IN_PROGRESS,
			"");
	}
#endif

}

Bn3Monkey::TLSSocket::TLSSocket(TCPAddress& address, uint32_t timeout_milliseconds)
	: TCPSocket(address, timeout_milliseconds)
{
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();

	auto client_method = TLSv1_2_client_method();

	_context = SSL_CTX_new(client_method);
	if (!_context) {
		_result = ConnectionResult(ConnectionCode::TLS_CONTEXT_INITIALIZATION_FAIL, "TLS Context cannot be initialized");
		return;
	}
	_ssl = SSL_new(_context);
	if (!_ssl) {
		SSL_CTX_free(_context);
		_context = nullptr;
		_result = ConnectionResult(ConnectionCode::TLS_INITIALIZATION_FAIL, "TLS Context cannot be initialized");
		return;
	}
}

Bn3Monkey::TLSSocket::~TLSSocket()
{
	if (_ssl) {
		SSL_shutdown(_ssl);
		SSL_free(_ssl);
	}
	if (_context) {
		SSL_CTX_free(_context);
	}
	TCPSocket::~TCPSocket();
}

ConnectionResult Bn3Monkey::TLSSocket::connect()
{
	ConnectionResult result = TCPSocket::connect();
	if (result.code != ConnectionCode::SUCCESS)
	{
		return result;
	}



	return result;
}

void Bn3Monkey::TLSSocket::disconnect()
{
}

int Bn3Monkey::TLSSocket::write(const char* buffer, size_t size)
{
	return 0;
}

int Bn3Monkey::TLSSocket::read(char* buffer, size_t size)
{
	return 0;
}

ConnectionResult Bn3Monkey::TLSSocket::result(int operation_return)
{
	return ConnectionResult();
}
