#include "TCPSocket.hpp"

using namespace Bn3Monkey;

Bn3Monkey::TCPSocket::TCPSocket(const TCPAddress& address, uint32_t timeout_milliseconds)
{
	_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_socket < 0)
	{
		_result = result(_socket);
		return;
	}

#ifdef _WIN32
	uint32_t timeout = timeout_milliseconds;
	const char* timeout_ref = reinterpret_cast<const char*>(&timeout);
#elif 
	timeval timeout;
	timeout.tv_sec = (time_t)socket_timeout / (time_t)1000;
	timeout.tv_usec = (suseconds_t)socket_timeout * (suseconds_t)1000 - (suseconds_t)(timeout.tv_sec * (suseconds_t)1000000);
	timeval* timeout_ref = &timeout;
#endif
	setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, timeout_ref, sizeof(timeout));
	setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, timeout_ref, sizeof(timeout));

}

Bn3Monkey::TCPSocket::~TCPSocket()
{
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
}

long Bn3Monkey::TCPSocket::connect()
{
	return 0;
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
	}
#endif

}

Bn3Monkey::TLSSocket::TLSSocket(const TCPAddress& address, uint32_t timeout_milliseconds)
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

long Bn3Monkey::TLSSocket::connect()
{
	TCPSocket::connect();
	return 0;
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
