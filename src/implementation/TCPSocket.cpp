#include "TCPSocket.hpp"

#ifndef _WIN32
#include <ctime>
#endif

using namespace Bn3Monkey;

TCPSocket::NonBlockMode::NonBlockMode(const TCPSocket& tcp_socket)  : _socket(tcp_socket._socket), _flags(0) {
#ifdef _WIN32
	unsigned long mode{ 1 };
	ioctlsocket(_socket, FIONBIO, &mode);
#else
	_flags = fcntl(_socket, F_GETFL, 0);
	fcntl(_socket, F_SETFL, _flags | O_NONBLOCK);
#endif
}
TCPSocket::NonBlockMode::~NonBlockMode() {
#ifdef _WIN32
	unsigned long mode{ 0 };
	ioctlsocket(_socket, FIONBIO, &mode);
#else
	fcntl(_socket, F_SETFL, _flags);
#endif
}

Bn3Monkey::TCPSocket::TCPSocket(TCPAddress& address, uint32_t read_timeout, uint32_t write_timeout) : _address(address), _read_timeout(read_timeout), _write_timeout(write_timeout)
{
	_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_socket < 0)
	{
		_result = result(_socket);
		return;
	}

#ifdef _WIN32
	constexpr size_t size = sizeof(uint32_t);
	const char* read_timeout_ref = reinterpret_cast<const char*>(&_read_timeout);
	const char* write_timeout_ref = reinterpret_cast<const char*>(&_write_timeout);
#else
	constexpr size_t size = sizeof(timeval);
	timeval read_timeout_value;
	timeout.tv_sec = (time_t)_read_timeout / (time_t)1000;
	timeout.tv_usec = (suseconds_t)_read_timeout * (suseconds_t)1000 - (suseconds_t)(read_timeout_value.tv_sec * (suseconds_t)1000000);
	timeval* read_time_ref = &read_timeout_value;

	timeval write_timeout_value;
	timeout.tv_sec = (time_t)_write_timeout / (time_t)1000;
	timeout.tv_usec = (suseconds_t)_write_timeout * (suseconds_t)1000 - (suseconds_t)(write_timeout_value.tv_sec * (suseconds_t)1000000);
	timeval* write_time_ref = &write_timeout_value;
#endif

	if (setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, read_timeout_ref, size) < 0)
	{
		_result = ConnectionResult(ConnectionCode::SOCKET_OPTION_ERROR, "cannot get socket receive timeout");
	}
	if (setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, write_timeout_ref, size) < 0)
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
	
#if !defined(_WIN32) && !defined(__linux__)
	int sigpipe{ 1 };
	setsockopt(_socket, SOL_SOCKET, SO_NOSIGPIPE, (void*)(&sigpipe), sizeof(sigpipe));
#endif
	
	{
		NonBlockMode nonblock{ *this };

		int32_t res = ::connect(_socket, _address.address(), _address.size());
		if (res < 0)
		{
			// ERROR
			ret = result(res);
			if (ret.code != ConnectionCode::SOCKET_CONNECTION_IN_PROGRESS)
				return ret;
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
	}
	return ret;
}

void Bn3Monkey::TCPSocket::disconnect()
{
#ifdef _WIN32
	shutdown(_socket, SD_BOTH);
#else
	shutdown(_socket, SHUT_RDWR);
#endif
}

ConnectionResult Bn3Monkey::TCPSocket::isConnected()
{
	int optval;
	socklen_t optlen = sizeof(optval);
	if (getsockopt(_socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&optval), &optlen) < 0)
	{
		return ConnectionResult(ConnectionCode::SOCKET_OPTION_ERROR, "cannot get socket error");
	}
	if (optval != 0)
	{
		return ConnectionResult(ConnectionCode::SOCKET_CLOSED, "Socket closed");
	}
	return ConnectionResult(ConnectionCode::SUCCESS);
}

int Bn3Monkey::TCPSocket::write(const char* buffer, size_t size)
{
	int32_t ret{0};
#ifdef __linux__
	ret = send(_socket, buffer, size, MSG_NOSIGNAL);
#else
	ret = send(_socket, buffer, size, 0);
#endif
	return ret;
}

int Bn3Monkey::TCPSocket::read(char* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, buffer, size, 0);
	return ret;
}

ConnectionResult Bn3Monkey::TCPSocket::poll(const PollType& polltype)
{
	ConnectionResult ret;

	/*
	//short io_flags = PollType::READ == polltype ? POLLIN : POLLOUT;
	short io_flags = POLLIN;
	short up_flags = PollType::CONNECT == polltype ? 0 : POLLHUP;
	short err_flags = POLLERR;

	pollfd fds[1];
	fds[0].fd = _socket;
	fds[0].events = io_flags | up_flags | err_flags;
	fds[0].revents = 0;
	int32_t res =
#ifdef _WIN32
		WSAPoll(fds, 1, _timeout_milliseconds);
#else
		::poll(fds, 1, _timeout_milliseconds);
#endif
	if (res < 0) {
		return result(res);
	}
	else if (res == 0)
	{
		return ConnectionResult(ConnectionCode::SOCKET_TIMEOUT);
	}
	else {
		if (fds[0].revents & io_flags) {
			ret = ConnectionResult(ConnectionCode::SUCCESS);
		}
		if (fds[0].revents & up_flags) {
			ret = ConnectionResult(ConnectionCode::SOCKET_CLOSED, "Connection is closed");
		}
		if (fds[0].revents & err_flags) {
			ret = ConnectionResult(ConnectionCode::SOCKET_CONNECTION_REFUSED, "Connection is refused");
		}
	}
	*/

	fd_set io_set;
	fd_set error_set;
	FD_ZERO(&io_set);
	FD_ZERO(&error_set);
	FD_SET(_socket, &io_set);
	FD_SET(_socket, &error_set);


	
	int32_t res{ 0 };
	switch (polltype)
	{
	case PollType::READ: 
		{
			struct timeval timeout;
			timeout.tv_sec = _read_timeout / 1000;
			timeout.tv_usec = 1000 * (_read_timeout - 1000 * timeout.tv_sec);
			res = ::select(_socket + 1, &io_set, nullptr, &error_set, &timeout);
		}
		break;
	case PollType::WRITE:
	case PollType::CONNECT: 
		{
			struct timeval timeout;
			timeout.tv_sec = _write_timeout / 1000;
			timeout.tv_usec = 1000 * (_write_timeout - 1000 * timeout.tv_sec);
			res = ::select(_socket + 1, nullptr, &io_set, &error_set, &timeout);
		}
		 break;
	}

	if (FD_ISSET(_socket, &error_set))
	{
		ret = ConnectionResult(ConnectionCode::SOCKET_CLOSED, "Socket closed");
	}
	if (res < 0)
	{
		ret = ConnectionResult(ConnectionCode::POLL_ERROR, "Polling error");
	}
	else if (res == 0) {
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

	case WSAETIMEDOUT:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_NOT_RESPOND,
			"Connected host is not responding to requests.");
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
	case WSAEWOULDBLOCK:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_IN_PROGRESS, "");
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

	case ETIMEDOUT:
		return ConnectionResult(ConnectionCode::SOCKET_CONNECTION_NOT_RESPOND,
			"Connected host is not responding to requests.");
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
	return ConnectionResult(ConnectionCode::UNKNOWN_ERROR, "");
}

Bn3Monkey::TLSSocket::TLSSocket(TCPAddress& address, uint32_t read_timeout, uint32_t write_timeout)
	: TCPSocket(address, read_timeout, write_timeout)
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
	ConnectionResult ret = TCPSocket::connect();
	if (ret.code != ConnectionCode::SUCCESS)
	{
		return ret;
	}

	if (SSL_set_fd(_ssl, _socket) == 0)
	{
		return ConnectionResult(ConnectionCode::TLS_SETFD_ERROR, "tls set fd error");
	}
	auto res = SSL_connect(_ssl);
	if (res != 1)
	{
		ret = result(res);
		if (ret.code != ConnectionCode::SUCCESS)
			return ret;
	}
	return ret;
}

void Bn3Monkey::TLSSocket::disconnect()
{
	TCPSocket::disconnect();
}

int Bn3Monkey::TLSSocket::write(const char* buffer, size_t size)
{
	int32_t ret = SSL_write(_ssl, buffer, size);
	return ret;
}

int Bn3Monkey::TLSSocket::read(char* buffer, size_t size)
{
	int32_t ret = SSL_read(_ssl, buffer, size);
	return ret;
}

ConnectionResult Bn3Monkey::TLSSocket::result(int operation_return)
{
	if (operation_return >= 1)
		return ConnectionResult(ConnectionCode::SUCCESS);

	int code = SSL_get_error(_ssl, operation_return);
	switch (code)
	{
	case SSL_ERROR_SSL:
		return ConnectionResult(ConnectionCode::SSL_PROTOCOL_ERROR, "SSL Protocol Error");
	case SSL_ERROR_SYSCALL:
		// System Error / TCP Error
		return TCPSocket::result(operation_return);
	case SSL_ERROR_ZERO_RETURN:
		return ConnectionResult(ConnectionCode::SSL_ERROR_CLOSED_BY_PEER, "the connection has been terminated by peer");
	}
	return ConnectionResult(ConnectionCode::UNKNOWN_ERROR);
}
