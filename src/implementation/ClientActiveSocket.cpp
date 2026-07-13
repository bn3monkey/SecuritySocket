#include "ClientActiveSocket.hpp"

#include "NetworkResult.hpp"
#include "SocketHelper.hpp"
#include "TlsTrace.hpp"

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

Bn3Monkey::ClientActiveSocket::ClientActiveSocket(bool is_unix_domain, const TlsClientConfiguration& tls_configuration, const char* hostname)
{
	(void)tls_configuration;
	(void)hostname;
	if (is_unix_domain) {
		auto temp_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
		_socket = static_cast<int32_t>(temp_socket);
	}
	else {
		auto temp_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		_socket = static_cast<int32_t>(temp_socket);
	}
	if (_socket < 0)
	{
		_result = createResult(_socket);
		return;
	}
}
Bn3Monkey::ClientActiveSocket::~ClientActiveSocket()
{
	// Do NOT call close() here.
	// SocketContainer uses memcpy-based shallow copy and calls the destructor
	// on temporary objects (e.g. during operator=). Closing the socket in the
	// destructor would invalidate the fd that the surviving copy still holds,
	// causing WSAENOTSOCK (10038) on subsequent operations.
	// Resource cleanup is handled explicitly via ClientImpl::close().
}

void ClientActiveSocket::close() {
#ifdef _WIN32
	::closesocket(_socket);
#else
	::close(_socket);
#endif
	_socket = -1;	
}

NetworkResult ClientActiveSocket::connect(const SocketAddress& address, uint32_t read_timeout_ms, uint32_t write_timeout_ms)
{
	NetworkResult result;
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

NetworkResult ClientActiveSocket::reconnect(bool after_handshake)
{
	(void)after_handshake;
	return NetworkResult(NetworkResultCode::SUCCESS);
}

void Bn3Monkey::ClientActiveSocket::disconnect()
{
#ifdef _WIN32
	shutdown(_socket, SD_BOTH);
#else
	shutdown(_socket, SHUT_RDWR);
#endif
}

NetworkResult Bn3Monkey::ClientActiveSocket::isConnected()
{
	char buf[1];
	int bytes_read = recv(_socket, buf, 1, MSG_PEEK);
	if (bytes_read > 0) {
		return NetworkResult(NetworkResultCode::SUCCESS);
	}
	return NetworkResult(NetworkResultCode::SOCKET_CLOSED);
}

NetworkResult Bn3Monkey::ClientActiveSocket::write(const void* buffer, size_t size)
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

NetworkResult Bn3Monkey::ClientActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), static_cast<int32_t>(size), 0);
	if (ret == 0)
		return NetworkResult(NetworkResultCode::SOCKET_CLOSED, 0);
	return createResult(ret);
}

// trackTLSInfo moved to TlsTrace.hpp so the server (TLSPassiveSocket) shares it
// instead of duplicating the rendering. It already handled SSL_ST_ACCEPT.

Bn3Monkey::TlsClientActiveSocket::TlsClientActiveSocket(bool is_unix_domain, const TlsClientConfiguration& tls_configuration, const char* hostname)
	: ClientActiveSocket(is_unix_domain, tls_configuration, hostname)
{
	if (_result.code() != NetworkResultCode::SUCCESS)
		return;

	_context = SSL_CTX_new(TLS_client_method());
	if (!_context) {
		_result = NetworkResult(NetworkResultCode::TLS_CONTEXT_INITIALIZATION_FAIL);
		return;
	}

	// [1] TLS 버전 범위 설정 : TLS 1.3 우선, 실패 시 TLS 1.2 자동 협상
	{
		bool has12 = tls_configuration.isVersionSupported(TlsVersion::TLS1_2);
		bool has13 = tls_configuration.isVersionSupported(TlsVersion::TLS1_3);
		int min_ver = has12 ? TLS1_2_VERSION : TLS1_3_VERSION;
		int max_ver = has13 ? TLS1_3_VERSION : TLS1_2_VERSION;
		SSL_CTX_set_min_proto_version(_context, min_ver);
		SSL_CTX_set_max_proto_version(_context, max_ver);
	}

	// [2] Cipher Suite 등록
	{
		char cipher_list[512]{ 0 };
		tls_configuration.generateTLS12CipherSuites(cipher_list);
		if (cipher_list[0] != '\0')
			SSL_CTX_set_cipher_list(_context, cipher_list);

		char ciphersuites[512]{ 0 };
		tls_configuration.generateTLS13CipherSuites(ciphersuites);
		if (ciphersuites[0] != '\0')
			SSL_CTX_set_ciphersuites(_context, ciphersuites);
	}

	// [3] 서버 인증서 검증
	if (tls_configuration.shouldVerifyServer()) {
		SSL_CTX_set_verify(_context, SSL_VERIFY_PEER, nullptr);
		const char* trust_store = tls_configuration.serverTrustStorePath();
		if (trust_store[0] != '\0')
			SSL_CTX_load_verify_locations(_context, trust_store, nullptr);
		else
			SSL_CTX_set_default_verify_paths(_context);
	}
	else {
		SSL_CTX_set_verify(_context, SSL_VERIFY_NONE, nullptr);
	}

	// [4] 클라이언트 인증서 등록
	if (tls_configuration.shouldUseClientCertificate()) {
		const char* key_password = tls_configuration.clientKeyPassword();
		if (key_password[0] != '\0') {
			SSL_CTX_set_default_passwd_cb(_context, [](char* buf, int size, int /*rwflag*/, void* userdata) -> int {
				const char* pw = static_cast<const char*>(userdata);
				int len = static_cast<int>(strlen(pw));
				if (len > size) len = size;
				memcpy(buf, pw, static_cast<size_t>(len));
				return len;
			});
			SSL_CTX_set_default_passwd_cb_userdata(_context, const_cast<char*>(key_password));
		}
		SSL_CTX_use_certificate_file(_context, tls_configuration.clientCertFilePath(), SSL_FILETYPE_PEM);
		SSL_CTX_use_PrivateKey_file(_context, tls_configuration.clientKeyFilePath(), SSL_FILETYPE_PEM);
	}

	// hostname 저장 (shouldVerifyHostname이 true인 경우에만)
	if (tls_configuration.shouldVerifyHostname() && hostname != nullptr && hostname[0] != '\0')
		_hostname = hostname;

	_ssl = SSL_new(_context);
	if (!_ssl) {
		SSL_CTX_free(_context);
		_context = nullptr;
		_result = NetworkResult(NetworkResultCode::TLS_INITIALIZATION_FAIL);
		return;
	}

	// TLS info tracking
	auto on_tls_event = tls_configuration.getOnTLSEvent();
	if (on_tls_event) {
        SSL_set_ex_data(_ssl, kTlsEventCallbackExDataIndex, reinterpret_cast<void*>(on_tls_event));
		SSL_CTX_set_info_callback(_context, trackTLSInfo);
	}
}

Bn3Monkey::TlsClientActiveSocket::~TlsClientActiveSocket()
{
}

void Bn3Monkey::TlsClientActiveSocket::close()
{
	if (_ssl) {
		SSL_free(_ssl);
		_ssl = nullptr;
	}
	if (_context) {
		SSL_CTX_free(_context);
		_context = nullptr;
	}
	ClientActiveSocket::close();
}
NetworkResult TlsClientActiveSocket::connect(const SocketAddress& address, uint32_t read_timeout_ms, uint32_t write_timeout_ms)
{
	NetworkResult result;
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
NetworkResult Bn3Monkey::TlsClientActiveSocket::reconnect(bool after_handshake)
{
	if (after_handshake) {
		// In TLS 1.3 the server sends its Finished message BEFORE it processes the
		// client's Certificate message, so SSL_connect() can return 1 (success)
		// before the server has had a chance to reject a missing or untrusted client
		// certificate.  Run a short post-handshake probe to catch that deferred alert.
		// In TLS 1.2 the handshake is fully synchronous, so no probe is needed.
		if (SSL_version(_ssl) == TLS1_3_VERSION)
			return postHandshakeProbe();
		return NetworkResult(NetworkResultCode::SUCCESS);
	}

	if (SSL_set_fd(_ssl, _socket) == 0)
		return NetworkResult(NetworkResultCode::TLS_SETFD_ERROR);

	// Set SNI extension so the server can select the correct virtual-host
	// certificate, and enable X.509 hostname / IP-address verification so that
	// the peer certificate's CN / SAN is checked against _hostname.
	if (_hostname != nullptr) {
		SSL_set_tlsext_host_name(_ssl, _hostname);  // SNI ClientHello extension
		SSL_set1_host(_ssl, _hostname);              // X.509 hostname verification
	}

	// Perform the TLS handshake.  Returns 1 on success, ≤0 on failure.
	auto res = SSL_connect(_ssl);
	if (res != 1)
		return createTLSResult(_ssl, res);


	return NetworkResult(NetworkResultCode::SUCCESS);
}

NetworkResult Bn3Monkey::TlsClientActiveSocket::postHandshakeProbe()
{
	// -------------------------------------------------------------------------
	// TLS 1.3 deferred client-certificate rejection probe
	//
	// The rejection alert (certificate_required, unknown_ca, handshake_failure)
	// arrives at the socket shortly after SSL_connect() returned 1.
	//
	// We perform a non-blocking SSL_peek() to check whether an alert has
	// already arrived.  Three outcomes:
	//   peek > 0   : application data queued — connection is good.
	//   SSL_ERROR_SSL / SSL_ERROR_ZERO_RETURN : rejection alert received.
	//   SSL_ERROR_WANT_READ : no data yet — return NEED_TO_BE_BLOCKED so the
	//       caller (Client::connect Phase 2) can wait via SocketEventListener
	//       (poll/select POLLIN) and retry.  The caller treats SOCKET_TIMEOUT
	//       (no alert within read_timeout) as an accepted connection.
	// -------------------------------------------------------------------------

	// Run in non-blocking mode so SSL_peek() returns immediately when no data
	// is buffered, instead of blocking or depending on SO_RCVTIMEO behavior
	// which differs between MSVC and GCC builds of OpenSSL.
	setNonBlockingMode(_socket);
	char probe[1];
	int  peek_ret = SSL_peek(_ssl, probe, sizeof(probe));
	setBlockingMode(_socket);

	if (peek_ret > 0) {
		// Application data already in the TLS receive buffer — connection is good.
		return NetworkResult(NetworkResultCode::SUCCESS);
	}

	int ssl_err = SSL_get_error(_ssl, peek_ret);

	if (ssl_err == SSL_ERROR_SSL || ssl_err == SSL_ERROR_ZERO_RETURN) {
		// A rejection alert arrived from the server after SSL_connect()
		// had already returned success — classify it via the normal path.
		return createTLSResult(_ssl, peek_ret);
	}

	if (ssl_err == SSL_ERROR_WANT_READ) {
		// No data buffered yet — tell the caller to wait for POLLIN and retry.
		return NetworkResult(NetworkResultCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED);
	}

	// SSL_ERROR_SYSCALL or anything else — no alert, treat as success.
	ERR_clear_error();
	return NetworkResult(NetworkResultCode::SUCCESS);
}

void Bn3Monkey::TlsClientActiveSocket::disconnect()
{
	if (_ssl) {
		SSL_shutdown(_ssl);
		SSL_free(_ssl);
		_ssl = nullptr;
	}
	ClientActiveSocket::disconnect();
}
NetworkResult Bn3Monkey::TlsClientActiveSocket::isConnected()
{
	return ClientActiveSocket::isConnected();
}
NetworkResult Bn3Monkey::TlsClientActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret = SSL_write(_ssl, buffer, static_cast<int32_t>(size));
	return createTLSResult(_ssl, ret);
}

NetworkResult Bn3Monkey::TlsClientActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret = SSL_read(_ssl, buffer, static_cast<int32_t>(size));
	return createTLSResult(_ssl, ret);
}


/*
#if !defined(_WIN32) && !defined(__linux__)
	int sigpipe{ 1 };
	setsockopt(_socket, SOL_SOCKET, SO_NOSIGPIPE, (void*)(&sigpipe), sizeof(sigpipe));
#endif
*/
