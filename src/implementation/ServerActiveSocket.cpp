#include "ServerActiveSocket.hpp"
#include "NetworkResult.hpp"
#include "SocketHelper.hpp"
#include "TlsTrace.hpp"

#ifdef _WIN32
#include <Winsock2.h>
#include <WS2tcpip.h>
#else
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h> // inet_ntop
#endif

using namespace Bn3Monkey;

ServerActiveSocket::ServerActiveSocket(int32_t sock, void* addr, void* ssl_context)
{
	(void)ssl_context;

    struct sockaddr_in* address = (struct sockaddr_in*)addr;
    _socket = sock;
    _result = createResult(_socket);
    if (_result.code() != NetworkResultCode::SUCCESS)
    {
        return;
    }

	if (sock >= 0) {
		
		inet_ntop(AF_INET, &(address->sin_addr), _client_ip, sizeof(_client_ip));
		_client_port = ntohs(address->sin_port);
		// printf("Connected client ip : %s port : %d\n", _client_ip, _client_port);
	}

    setNonBlockingMode(_socket);
}
ServerActiveSocket::~ServerActiveSocket()
{
    // close();
}
void ServerActiveSocket::close()
{
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
    _socket = -1;
}
NetworkResult ServerActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), static_cast<int32_t>(size), 0);
	if (ret == 0)
		return NetworkResult(NetworkResultCode::SOCKET_CLOSED, 0);
	return createResult(ret);
}
NetworkResult ServerActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret{0};
#ifdef __linux__
	ret = send(_socket, buffer, size, MSG_NOSIGNAL);
#else
	ret = send(_socket, static_cast<const char*>(buffer), static_cast<int32_t>(size), 0);
#endif
	if (ret == 0)
		return NetworkResult(NetworkResultCode::SOCKET_CLOSED, 0);
	return createResult(ret);
}

void ServerActiveSocket::setSocketBufferSize(size_t size)
{
	setsockopt(_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), sizeof(size));
	setsockopt(_socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size), sizeof(size));
}

void ServerActiveSocket::setNoDelay()
{
	::setNoDelay(_socket);
}

// ── TLS accepted connection ──────────────────────────────────────────────────
//
// Never throws; failures surface through _result, which the accept loops test via
// ServerActiveSocket::result(). The accept loop runs on the server's own thread
// with no handler above it, so an exception here would take the server down.
//
// The constructor does NOT perform the handshake. The accept loop is single
// threaded: a blocking SSL_accept() would let one slow (or malicious) client
// stall every other connection — the classic slow-loris. Instead the session is
// created in "accept" role and the host drives handshake() from the event loop,
// one step per readiness event, exactly like any other I/O.

TlsServerActiveSocket::TlsServerActiveSocket(int32_t sock, void* addr, void* ssl_context)
	: ServerActiveSocket(sock, addr, ssl_context)
{
	if (_result.code() != NetworkResultCode::SUCCESS)
		return;   // the base failed on the accepted fd; it owns that failure

	auto* context = static_cast<SSL_CTX*>(ssl_context);
	if (context == nullptr) {
		_result = NetworkResult(NetworkResultCode::TLS_CONTEXT_INITIALIZATION_FAIL);
		return;
	}

	// SSL_new() takes a reference on the context, so the listening socket may be
	// closed (freeing its own reference) while this session is still alive.
	ssl = SSL_new(context);
	if (ssl == nullptr) {
		_result = NetworkResult(NetworkResultCode::TLS_INITIALIZATION_FAIL);
		return;
	}

	if (SSL_set_fd(ssl, _socket) == 0) {
		_result = NetworkResult(NetworkResultCode::TLS_SETFD_ERROR);
		return;
	}

	// The info callback is registered on the shared context, but it recovers the
	// user's TlsEventCallback from the *session's* ex_data. Copy the pointer the
	// listening socket stashed on the context onto this session.
	if (void* on_tls_event = SSL_CTX_get_ex_data(context, kTlsEventCallbackExDataIndex))
		SSL_set_ex_data(ssl, kTlsEventCallbackExDataIndex, on_tls_event);

	// Server role: the next SSL_accept()/SSL_read() negotiates as the responder.
	SSL_set_accept_state(ssl);
}

TlsServerActiveSocket::~TlsServerActiveSocket()
{
	// Deliberately empty. Destructors never release; close() is the sole release
	// point (see the ownership note in BaseSocket.hpp).
}

TLSHandshakeState TlsServerActiveSocket::handshake()
{
	if (ssl == nullptr)
		return TLSHandshakeState::FAILED;

	// Stale entries would make createTLSResult() below misdiagnose this failure.
	ERR_clear_error();

	const int ret = SSL_accept(ssl);
	if (ret == 1)
		return TLSHandshakeState::DONE;

	switch (SSL_get_error(ssl, ret))
	{
	case SSL_ERROR_WANT_READ:
		return TLSHandshakeState::WANT_READ;
	case SSL_ERROR_WANT_WRITE:
		return TLSHandshakeState::WANT_WRITE;
	default:
		// Keep the specific diagnosis (bad cipher, rejected client cert, ...) on
		// _result; the host only learns FAILED and tears the connection down.
		_result = createTLSResult(ssl, ret);
		return TLSHandshakeState::FAILED;
	}
}

bool TlsServerActiveSocket::hasBufferedInput() const
{
	return ssl != nullptr && SSL_pending(ssl) > 0;
}

void TlsServerActiveSocket::close()
{
	if (ssl) {
		// One-shot close_notify. We do not wait for the peer's reply (that would
		// need a Closing state in the event loop); the fd is torn down right after.
		SSL_shutdown(ssl);
		SSL_free(ssl);
		ssl = nullptr;
	}
	ServerActiveSocket::close();
}

// Record which readiness this operation stalled on, so the host can arm the
// listener in that direction. SSL_read() stalling on *write* is normal (it may
// have to send a TLS 1.3 KeyUpdate response before it can decrypt further), and
// the NetworkResult alone cannot express it.
static TlsWant wantOf(SSL* ssl, int ret)
{
	if (ret > 0) return TlsWant::NONE;
	switch (SSL_get_error(ssl, ret))
	{
	case SSL_ERROR_WANT_READ:  return TlsWant::READ;
	case SSL_ERROR_WANT_WRITE: return TlsWant::WRITE;
	default:                   return TlsWant::NONE;
	}
}

NetworkResult TlsServerActiveSocket::read(void* buffer, size_t size)
{
	if (ssl == nullptr)
		return NetworkResult(NetworkResultCode::TLS_INITIALIZATION_FAIL);

	ERR_clear_error();
	const int ret = SSL_read(ssl, buffer, static_cast<int32_t>(size));
	_io_want = wantOf(ssl, ret);

	// A clean close_notify from the peer. SSL_read() returns 0 for it, which
	// createTLSResult() would otherwise not treat as a close.
	if (ret == 0 && _io_want == TlsWant::NONE)
		return NetworkResult(NetworkResultCode::SOCKET_CLOSED, 0);
	return createTLSResult(ssl, ret);
}

NetworkResult TlsServerActiveSocket::write(const void* buffer, size_t size)
{
	if (ssl == nullptr)
		return NetworkResult(NetworkResultCode::TLS_INITIALIZATION_FAIL);

	ERR_clear_error();
	const int ret = SSL_write(ssl, buffer, static_cast<int32_t>(size));
	_io_want = wantOf(ssl, ret);
	return createTLSResult(ssl, ret);
}
