#include "ClientActiveSocket.hpp"

#include "SocketResult.hpp"
#include "SocketHelper.hpp"

#ifndef _WIN32
#include <ctime>
#endif

using namespace Bn3Monkey;

Bn3Monkey::ClientActiveSocket::ClientActiveSocket(bool is_unix_domain)
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

	return;
}
Bn3Monkey::ClientActiveSocket::~ClientActiveSocket()
{
}

void ClientActiveSocket::close() {
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
	_socket = -1;	
}

SocketResult ClientActiveSocket::connect(const SocketAddress& address)
{
	SocketResult result;
	{
		int32_t res = ::connect(_socket, address.address(), address.size());
		if (res < 0)
		{
			// ERROR
			result = createResult(res);
		}

	}
	return result;
}



void Bn3Monkey::ClientActiveSocket::disconnect()
{
#ifdef _WIN32
	shutdown(_socket, SD_BOTH);
#else
	shutdown(_socket, SHUT_RDWR);
#endif
}

SocketResult Bn3Monkey::ClientActiveSocket::isConnected()
{
	char buf[1];
	int bytes_read = recv(_socket, buf, 1, MSG_PEEK);
	if (bytes_read > 0) {
		return SocketResult(SocketCode::SUCCESS);
	}
	return SocketResult(SocketCode::SOCKET_CLOSED);
}

int Bn3Monkey::ClientActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret{0};
#ifdef __linux__
	ret = send(_socket, buffer, size, MSG_NOSIGNAL);
#else
	ret = send(_socket, static_cast<const char*>(buffer), size, 0);
#endif
	return ret;
}

int Bn3Monkey::ClientActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), size, 0);
	return ret;
}


Bn3Monkey::TLSClientActiveSocket::TLSClientActiveSocket(bool is_unix_domain) : ClientActiveSocket(is_unix_domain)
{
	if (_result.code() != SocketCode::SUCCESS)
	{
		return;
	}

	auto client_method = TLSv1_2_client_method();

	_context = SSL_CTX_new(client_method);
	if (!_context) {
		_result = SocketResult(SocketCode::TLS_CONTEXT_INITIALIZATION_FAIL);
		return;
	}
	_ssl = SSL_new(_context);
	if (!_ssl) {
		SSL_CTX_free(_context);
		_context = nullptr;
		_result = SocketResult(SocketCode::TLS_INITIALIZATION_FAIL);
	}
}

Bn3Monkey::TLSClientActiveSocket::~TLSClientActiveSocket()
{
	disconnect();
	close();
}

void Bn3Monkey::TLSClientActiveSocket::close()
{
	if (_context) {
		SSL_CTX_free(_context);
	}
	close();
}

SocketResult Bn3Monkey::TLSClientActiveSocket::connect(const SocketAddress& address)
{
	SocketResult ret = ClientActiveSocket::connect(address);
	if (ret.code() != SocketCode::SUCCESS)
	{
		return ret;
	}

	if (SSL_set_fd(_ssl, _socket) == 0)
	{
		return SocketResult(SocketCode::TLS_SETFD_ERROR);
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

void Bn3Monkey::TLSClientActiveSocket::disconnect()
{
	if (_ssl) {
		SSL_shutdown(_ssl);
		SSL_free(_ssl);
	}
	ClientActiveSocket::disconnect();
}
SocketResult Bn3Monkey::TLSClientActiveSocket::isConnected()
{
	return ClientActiveSocket::isConnected();
}
int Bn3Monkey::TLSClientActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret = SSL_write(_ssl, buffer, size);
	return ret;
}

int Bn3Monkey::TLSClientActiveSocket::read(void* buffer, size_t size)
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