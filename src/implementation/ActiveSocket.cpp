#include "ActiveSocket.hpp"

#include "SocketResult.hpp"
#include "SocketHelper.hpp"

#ifndef _WIN32
#include <ctime>
#endif

using namespace Bn3Monkey;

Bn3Monkey::ActiveSocket::~ActiveSocket()
{
	disconnect();
	close();
}

SocketResult ActiveSocket::open()
{
	SocketResult result;

	if (_socket >= 0 )
	{
		result = SocketResult(SocketCode::SOCKET_ALREADY_CONNECTED, "Socket is already connected");
		return result;
	}	

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

void ActiveSocket::close() {
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
	_socket = -1;	
}

SocketResult ActiveSocket::connect()
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



void Bn3Monkey::ActiveSocket::disconnect()
{
#ifdef _WIN32
	shutdown(_socket, SD_BOTH);
#else
	shutdown(_socket, SHUT_RDWR);
#endif
	_socket = -1;
}

SocketResult Bn3Monkey::ActiveSocket::isConnected()
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

int Bn3Monkey::ActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret{0};
#ifdef __linux__
	ret = send(_socket, buffer, size, MSG_NOSIGNAL);
#else
	ret = send(_socket, static_cast<const char*>(buffer), size, 0);
#endif
	return ret;
}

int Bn3Monkey::ActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), size, 0);
	return ret;
}


SocketResult Bn3Monkey::TLSActiveSocket::open()
{
	auto result = TLSActiveSocket::open();
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

Bn3Monkey::TLSActiveSocket::~TLSActiveSocket()
{
	if (_ssl) {
		SSL_shutdown(_ssl);
		SSL_free(_ssl);
	}
	if (_context) {
		SSL_CTX_free(_context);
	}
	ActiveSocket::~ActiveSocket();
}

SocketResult Bn3Monkey::TLSActiveSocket::connect()
{
	SocketResult ret = TLSActiveSocket::connect();
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

void Bn3Monkey::TLSActiveSocket::disconnect()
{
	TLSActiveSocket::disconnect();
}

int Bn3Monkey::TLSActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret = SSL_write(_ssl, buffer, size);
	return ret;
}

int Bn3Monkey::TLSActiveSocket::read(void* buffer, size_t size)
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