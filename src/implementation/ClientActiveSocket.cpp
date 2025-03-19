#include "ClientActiveSocket.hpp"

#include "SocketResult.hpp"
#include "SocketHelper.hpp"

#ifdef _WIN32
#include <Winsock2.h>
#include <WS2tcpip.h>
#include <ctime>
#else
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
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

SocketResult ClientActiveSocket::connect(const SocketAddress& address, uint32_t read_timeout_ms, uint32_t write_timeout_ms)
{
	SocketResult result;
	setTimeout(_socket, read_timeout_ms, write_timeout_ms);
	setNonBlockingMode(_socket);
	{
		int32_t res = ::connect(_socket, address.address(), address.size());
		if (res < 0)
		{
			// ERROR
			result = createResult(res);
		}		
	}
	setBlockingMode(_socket);
	return result;
}

SocketResult ClientActiveSocket::reconnect()
{
	return SocketResult(SocketCode::SUCCESS);
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

SocketResult Bn3Monkey::ClientActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret{0};
#ifdef __linux__
	ret = send(_socket, buffer, size, MSG_NOSIGNAL);
#else
	ret = send(_socket, static_cast<const char*>(buffer), size, 0);
#endif
	
	return createResult(ret);
}

SocketResult Bn3Monkey::ClientActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), size, 0);
	return createResult(ret);
}


Bn3Monkey::TLSClientActiveSocket::TLSClientActiveSocket(bool is_unix_domain) : ClientActiveSocket(is_unix_domain)
{
	if (_result.code() != SocketCode::SUCCESS)
	{
		return;
	}

	auto client_method = TLS_client_method();

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
}

void Bn3Monkey::TLSClientActiveSocket::close()
{
	if (_context) {
		SSL_CTX_free(_context);
	}
	close();
}
SocketResult TLSClientActiveSocket::connect(const SocketAddress& address, uint32_t read_timeout_ms, uint32_t write_timeout_ms)
{
	SocketResult result;
	setTimeout(_socket, read_timeout_ms, write_timeout_ms);
	setNonBlockingMode(_socket);
	{
		int32_t res = ::connect(_socket, address.address(), address.size());
		if (res < 0)
		{
			// ERROR
			result = createResult(res);
		}
	}
	setBlockingMode(_socket);
	return result;
}
SocketResult Bn3Monkey::TLSClientActiveSocket::reconnect()
{	
	SocketResult result;
	if (SSL_set_fd(_ssl, _socket) == 0)
	{
		return SocketResult(SocketCode::TLS_SETFD_ERROR);
	}
	auto res = SSL_connect(_ssl);
	if (res != 1)
	{
		result = createTLSResult(_ssl, res);
		if (result.code() != SocketCode::SUCCESS)
			return result;
	}
	return result;
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
SocketResult Bn3Monkey::TLSClientActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret = SSL_write(_ssl, buffer, size);
	return createTLSResult(_ssl, ret);
}

SocketResult Bn3Monkey::TLSClientActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret = SSL_read(_ssl, buffer, size);
	return createTLSResult(_ssl, ret);
}


/*
#if !defined(_WIN32) && !defined(__linux__)
	int sigpipe{ 1 };
	setsockopt(_socket, SOL_SOCKET, SO_NOSIGPIPE, (void*)(&sigpipe), sizeof(sigpipe));
#endif
*/