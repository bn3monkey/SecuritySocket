#include "PassiveSocket.hpp"

#include "SocketResult.hpp"
#include "SocketHelper.hpp"

#ifndef _WIN32
#include <ctime>
#endif

using namespace Bn3Monkey;

Bn3Monkey::PassiveSocket::~PassiveSocket()
{
	disconnect();
	close();
}

SocketResult PassiveSocket::open(const SocketAddress& address)
{
	SocketResult result;

	if (_socket >= 0 )
	{
		result = SocketResult(SocketCode::SOCKET_ALREADY_CONNECTED, "Socket is already connected");
		return result;
	}	

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

SocketResult Bn3Monkey::PassiveSocket::poll(const PassivePollType& polltype, uint32_t timeout_ms)
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
	struct timeval timeout;
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = 1000 * (timeout_ms - 1000 * timeout.tv_sec);

	switch (polltype)
	{
	case PassivePollType::READ: 
		{
			res = ::select(_socket + 1, &io_set, nullptr, &error_set, &timeout);
		}
		break;
	case PassivePollType::WRITE:
	case PassivePollType::CONNECT: 
		{
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


SocketResult Bn3Monkey::TLSPassiveSocket::open(const SocketAddress& address)
{
	auto result = TLSPassiveSocket::open(address);
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