#include "PassiveSocket.hpp"
#include "SocketResult.hpp"
#ifndef _WIN32
#include <ctime>
#endif

using namespace Bn3Monkey;


PassiveSocket::NonBlockMode::NonBlockMode(PassiveSocket& socket) : _socket(socket._socket), _flags(0) 
{
#ifdef _WIN32
	unsigned long mode{ 1 };
	ioctlsocket(_socket, FIONBIO, &mode);
#else
	_flags = fcntl(_socket, F_GETFL, 0);
	fcntl(_socket, F_SETFL, _flags | O_NONBLOCK);
#endif
}
PassiveSocket::NonBlockMode::~NonBlockMode() {
#ifdef _WIN32
	unsigned long mode{ 0 };
	ioctlsocket(_socket, FIONBIO, &mode);
#else
	fcntl(_socket, F_SETFL, _flags);
#endif
}

Bn3Monkey::PassiveSocket::~PassiveSocket()
{
	disconnect();
	close();
}

SocketResult PassiveSocket::open(const SocketAddress& address, uint32_t read_timeout, uint32_t write_timeout)
{
	SocketResult result;

	_address = address;
	_read_timeout = read_timeout;
	_write_timeout = write_timeout;

	if (address.isUnixDomain())
		_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
	else
		_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_socket < 0)
	{
		result = createResult(_socket);
		return result;
	}

#ifdef _WIN32
	constexpr size_t size = sizeof(uint32_t);
	const char* read_timeout_ref = reinterpret_cast<const char*>(&_read_timeout);
	const char* write_timeout_ref = reinterpret_cast<const char*>(&_write_timeout);
#else
	constexpr size_t size = sizeof(timeval);
	timeval read_timeout_value;
	read_timeout_value.tv_sec = (time_t)_read_timeout / (time_t)1000;
	read_timeout_value.tv_usec = (suseconds_t)_read_timeout * (suseconds_t)1000 - (suseconds_t)(read_timeout_value.tv_sec * (suseconds_t)1000000);
	timeval* read_timeout_ref = &read_timeout_value;

	timeval write_timeout_value;
	write_timeout_value.tv_sec = (time_t)_write_timeout / (time_t)1000;
	write_timeout_value.tv_usec = (suseconds_t)_write_timeout * (suseconds_t)1000 - (suseconds_t)(write_timeout_value.tv_sec * (suseconds_t)1000000);
	timeval* write_timeout_ref = &write_timeout_value;
#endif

	if (setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, read_timeout_ref, size) < 0)
	{
		return SocketResult(SocketCode::SOCKET_OPTION_ERROR, "cannot get socket receive timeout");
	}
	if (setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, write_timeout_ref, size) < 0)
	{
		return SocketResult(SocketCode::SOCKET_OPTION_ERROR, "cannot get socket send timeout");
	}

	return result;
}

void PassiveSocket::close() {
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
	_socket = -1;	
}

SocketResult PassiveSocket::connect()
{
	SocketResult result;
	
	{
		NonBlockMode nonblock{ *this };

		int32_t res = ::connect(_socket, _address.address(), _address.size());
		if (res < 0)
		{
			// ERROR
			result = createResult(res);
			if (result.code() != SocketCode::SOCKET_CONNECTION_IN_PROGRESS)
				return result;
		}
		else if (res == 0)
		{
			// TIMEOUT
			return SocketResult(SocketCode::SOCKET_TIMEOUT, "");
		}

		result = poll(PassivePollType::CONNECT);
		
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			return SocketResult(SocketCode::SOCKET_TIMEOUT, "");
		}
		else if (result.code() != SocketCode::SUCCESS)
		{
			return result;
		}
	}
	return result;
}



void Bn3Monkey::PassiveSocket::disconnect()
{
#ifdef _WIN32
	shutdown(_socket, SD_BOTH);
#else
	shutdown(_socket, SHUT_RDWR);
#endif
	_socket = -1;
}

SocketResult Bn3Monkey::PassiveSocket::isConnected()
{
	int optval;
	socklen_t optlen = sizeof(optval);
	if (getsockopt(_socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&optval), &optlen) < 0)
	{
		return SocketResult(SocketCode::SOCKET_OPTION_ERROR, "cannot get socket error");
	}
	if (optval != 0)
	{
		return SocketResult(SocketCode::SOCKET_CLOSED, "Socket closed");
	}
	return SocketResult(SocketCode::SUCCESS);
}

int Bn3Monkey::PassiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret{0};
#ifdef __linux__
	ret = send(_socket, buffer, size, MSG_NOSIGNAL);
#else
	ret = send(_socket, static_cast<const char*>(buffer), size, 0);
#endif
	return ret;
}

int Bn3Monkey::PassiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), size, 0);
	return ret;
}

SocketResult Bn3Monkey::PassiveSocket::poll(const PassivePollType& polltype)
{
	SocketResult ret;

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
		return SocketResult(SocketCode::SOCKET_TIMEOUT);
	}
	else {
		if (fds[0].revents & io_flags) {
			ret = SocketResult(SocketCode::SUCCESS);
		}
		if (fds[0].revents & up_flags) {
			ret = SocketResult(SocketCode::SOCKET_CLOSED, "Connection is closed");
		}
		if (fds[0].revents & err_flags) {
			ret = SocketResult(SocketCode::SOCKET_CONNECTION_REFUSED, "Connection is refused");
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
	case PassivePollType::READ: 
		{
			struct timeval timeout;
			timeout.tv_sec = _read_timeout / 1000;
			timeout.tv_usec = 1000 * (_read_timeout - 1000 * timeout.tv_sec);
			res = ::select(_socket + 1, &io_set, nullptr, &error_set, &timeout);
		}
		break;
	case PassivePollType::WRITE:
	case PassivePollType::CONNECT: 
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
		ret = SocketResult(SocketCode::SOCKET_CLOSED, "Socket closed");
	}
	if (res < 0)
	{
		ret = SocketResult(SocketCode::POLL_ERROR, "Polling error");
	}
	else if (res == 0) {
		ret = SocketResult(SocketCode::SOCKET_TIMEOUT);
	}
	else {
		ret = SocketResult(SocketCode::SUCCESS);
	}
	return ret;
}


SocketResult Bn3Monkey::TLSPassiveSocket::open(const SocketAddress& address, uint32_t read_timeout, uint32_t write_timeout)
{
	auto result = TLSPassiveSocket::open(address, read_timeout, write_timeout);
	if (result.code() != SocketCode::SUCCESS)
	{
		return result;
	}

	SSL_library_init();
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();

	auto client_method = TLSv1_2_client_method();

	_context = SSL_CTX_new(client_method);
	if (!_context) {
		result = SocketResult(SocketCode::TLS_CONTEXT_INITIALIZATION_FAIL, "TLS Context cannot be initialized");
		return result;
	}
	_ssl = SSL_new(_context);
	if (!_ssl) {
		SSL_CTX_free(_context);
		_context = nullptr;
		result = SocketResult(SocketCode::TLS_INITIALIZATION_FAIL, "TLS Context cannot be initialized");
		return result;
	}

	return result;
}

Bn3Monkey::TLSPassiveSocket::~TLSPassiveSocket()
{
	if (_ssl) {
		SSL_shutdown(_ssl);
		SSL_free(_ssl);
	}
	if (_context) {
		SSL_CTX_free(_context);
	}
	PassiveSocket::~PassiveSocket();
}

SocketResult Bn3Monkey::TLSPassiveSocket::connect()
{
	SocketResult ret = TLSPassiveSocket::connect();
	if (ret.code() != SocketCode::SUCCESS)
	{
		return ret;
	}

	if (SSL_set_fd(_ssl, _socket) == 0)
	{
		return SocketResult(SocketCode::TLS_SETFD_ERROR, "tls set fd error");
	}
	auto res = SSL_connect(_ssl);
	if (res != 1)
	{
		ret = createTLSResult(_ssl, res);
		if (ret.code() != SocketCode::SUCCESS)
			return ret;
	}
	return ret;
}

void Bn3Monkey::TLSPassiveSocket::disconnect()
{
	TLSPassiveSocket::disconnect();
}

int Bn3Monkey::TLSPassiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret = SSL_write(_ssl, buffer, size);
	return ret;
}

int Bn3Monkey::TLSPassiveSocket::read(void* buffer, size_t size)
{
	int32_t ret = SSL_read(_ssl, buffer, size);
	return ret;
}


/*
#if !defined(_WIN32) && !defined(__linux__)
	int sigpipe{ 1 };
	setsockopt(_socket, SOL_SOCKET, SO_NOSIGPIPE, (void*)(&sigpipe), sizeof(sigpipe));
#endif
*/