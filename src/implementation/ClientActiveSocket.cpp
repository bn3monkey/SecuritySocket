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

Bn3Monkey::ClientActiveSocket::ClientActiveSocket(bool is_unix_domain, const SocketTLSClientConfiguration& tls_configuration, const char* hostname)
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
	ret = send(_socket, static_cast<const char*>(buffer), static_cast<int32_t>(size), 0);
#endif
	
	return createResult(ret);
}

SocketResult Bn3Monkey::ClientActiveSocket::read(void* buffer, size_t size)
{
	int32_t ret{ 0 };
	ret = ::recv(_socket, static_cast<char*>(buffer), static_cast<int32_t>(size), 0);
	return createResult(ret);
}


Bn3Monkey::TLSClientActiveSocket::TLSClientActiveSocket(bool is_unix_domain, const SocketTLSClientConfiguration& tls_configuration, const char* hostname)
	: ClientActiveSocket(is_unix_domain, tls_configuration, hostname)
{
	if (_result.code() != SocketCode::SUCCESS)
		return;

	_context = SSL_CTX_new(TLS_client_method());
	if (!_context) {
		_result = SocketResult(SocketCode::TLS_CONTEXT_INITIALIZATION_FAIL);
		return;
	}

	// [1] TLS 버전 범위 설정 : TLS 1.3 우선, 실패 시 TLS 1.2 자동 협상
	{
		bool has12 = tls_configuration.isVersionSupported(SocketTLSVersion::TLS1_2);
		bool has13 = tls_configuration.isVersionSupported(SocketTLSVersion::TLS1_3);
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
		_result = SocketResult(SocketCode::TLS_INITIALIZATION_FAIL);
	}
}

Bn3Monkey::TLSClientActiveSocket::~TLSClientActiveSocket()
{
}

void Bn3Monkey::TLSClientActiveSocket::close()
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
	if (SSL_set_fd(_ssl, _socket) == 0)
		return SocketResult(SocketCode::TLS_SETFD_ERROR);

	// [SNI + 호스트명 검증] : 도메인 연결 시 서버가 올바른 인증서를 보내도록 SNI 설정,
	//                         인증서의 CN/SAN이 hostname과 일치하는지 검증
	if (_hostname != nullptr) {
		SSL_set_tlsext_host_name(_ssl, _hostname);  // SNI ClientHello extension
		SSL_set1_host(_ssl, _hostname);              // X.509 hostname verification
	}

	// [TLS Handshake] : ClientHello → ServerHello → 인증서 교환 → 키 교환
	//                   TLS 1.3 시도 후 서버가 지원하지 않으면 TLS 1.2로 자동 협상
	auto res = SSL_connect(_ssl);
	if (res != 1)
		return createTLSResult(_ssl, res);

	return SocketResult(SocketCode::SUCCESS);
}

void Bn3Monkey::TLSClientActiveSocket::disconnect()
{
	if (_ssl) {
		SSL_shutdown(_ssl);
		SSL_free(_ssl);
		_ssl = nullptr;
	}
	ClientActiveSocket::disconnect();
}
SocketResult Bn3Monkey::TLSClientActiveSocket::isConnected()
{
	return ClientActiveSocket::isConnected();
}
SocketResult Bn3Monkey::TLSClientActiveSocket::write(const void* buffer, size_t size)
{
	int32_t ret = SSL_write(_ssl, buffer, static_cast<int32_t>(size));
	return createTLSResult(_ssl, ret);
}

SocketResult Bn3Monkey::TLSClientActiveSocket::read(void* buffer, size_t size)
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