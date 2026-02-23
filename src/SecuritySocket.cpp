#include "SecuritySocket.hpp"
#include "implementation/SocketBroadcastServer.hpp"
#include "implementation/SocketRequestServer.hpp"
#include "implementation/SocketClient.hpp"
#include "implementation/SocketResult.hpp"
#include "implementation/TLSHelper.hpp"

#if defined _WIN32
#include <Winsock2.h>
#include <WS2tcpip.h>
#endif // _WIN32

using namespace Bn3Monkey;

bool Bn3Monkey::initializeSecuritySocket()
{	
#ifdef _WIN32
	WSADATA data;
	int ret = WSAStartup(MAKEWORD(2, 2), &data);
	if (ret != 0) {
		return false;
	}
#endif
	return true;
}
void Bn3Monkey::releaseSecuritySocket()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

const char* Bn3Monkey::SocketResult::message() {
	return getMessage(_code);
}

Bn3Monkey::SocketClient::SocketClient(const SocketConfiguration& configuration)
{
	new (_container) SocketClientImpl (configuration);
}

Bn3Monkey::SocketClient::SocketClient(const SocketConfiguration& configuration, const SocketTLSClientConfiguration& tls_configuration)
{
	new (_container) SocketClientImpl (configuration, tls_configuration);
}


Bn3Monkey::SocketClient::~SocketClient()
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	impl->~SocketClientImpl();
}

Bn3Monkey::SocketResult Bn3Monkey::SocketClient::open()
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->open();
}	
void Bn3Monkey::SocketClient::close()
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->close();
}

Bn3Monkey::SocketResult Bn3Monkey::SocketClient::connect()
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->connect();
}
Bn3Monkey::SocketResult Bn3Monkey::SocketClient::read(void* buffer, size_t size)
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->read(buffer, size);
}
Bn3Monkey::SocketResult Bn3Monkey::SocketClient::write(const void* buffer, size_t size)
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->write(buffer, size);
}
Bn3Monkey::SocketResult Bn3Monkey::SocketClient::isConnected()
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->isConnected();
}

Bn3Monkey::SocketRequestServer::SocketRequestServer(const SocketConfiguration& configuration)
{
	new (_container) SocketRequestServerImpl(configuration);
}
Bn3Monkey::SocketRequestServer::SocketRequestServer(const SocketConfiguration& configuration, const SocketTLSServerConfiguration& tls_configuration)
{
	new (_container) SocketRequestServerImpl(configuration, tls_configuration);
}

Bn3Monkey::SocketRequestServer::~SocketRequestServer()
{
	SocketRequestServerImpl* impl = static_cast<SocketRequestServerImpl*>((void*)_container);
	impl->~SocketRequestServerImpl();
}

SocketResult Bn3Monkey::SocketRequestServer::open(SocketRequestHandler* handler, size_t num_of_clients)
{
	SocketRequestServerImpl* impl = static_cast<SocketRequestServerImpl*>((void*)_container);
	return impl->open(handler, num_of_clients);
}
void Bn3Monkey::SocketRequestServer::close()
{
	SocketRequestServerImpl* impl = static_cast<SocketRequestServerImpl*>((void*)_container);
	return impl->close();
}

Bn3Monkey::SocketBroadcastServer::SocketBroadcastServer(const SocketConfiguration& configuration)
{
	new (_container) SocketBroadcastServerImpl(configuration);
}
Bn3Monkey::SocketBroadcastServer::SocketBroadcastServer(const SocketConfiguration& configuration,  const SocketTLSServerConfiguration& tls_configuration)
{
	new (_container) SocketBroadcastServerImpl(configuration, tls_configuration);
}
Bn3Monkey::SocketBroadcastServer::~SocketBroadcastServer()
{
	SocketBroadcastServerImpl* impl = static_cast<SocketBroadcastServerImpl*>((void*)_container);
	impl->~SocketBroadcastServerImpl();
}

SocketResult Bn3Monkey::SocketBroadcastServer::open(size_t num_of_clients)
{
	SocketBroadcastServerImpl* impl = static_cast<SocketBroadcastServerImpl*>((void*)_container);
	return impl->open(num_of_clients);
}
void Bn3Monkey::SocketBroadcastServer::close()
{
	SocketBroadcastServerImpl* impl = static_cast<SocketBroadcastServerImpl*>((void*)_container);
	return impl->close();
}
SocketResult Bn3Monkey::SocketBroadcastServer::enumerate()
{
	SocketBroadcastServerImpl* impl = static_cast<SocketBroadcastServerImpl*>((void*)_container);
	return impl->enumerate();
}
SocketResult Bn3Monkey::SocketBroadcastServer::write(const void* buffer, size_t size)
{
	SocketBroadcastServerImpl* impl = static_cast<SocketBroadcastServerImpl*>((void*)_container);
	return impl->write(buffer, size);
}

static size_t appendCipherString(const char* cipher_str, size_t offset, char* dest)
{
	if (offset != 0) {
		dest[offset++] = ':';
	}
	size_t len = strlen(cipher_str);
	memcpy(dest + offset, cipher_str, len);
	offset += len;
	dest[offset] = '\0';
	return offset;
}
static void generateTLS12CipherSuiteImpl(int32_t suites_bitmap, char* ret)
{
	ret[0] = '\0';
	if (suites_bitmap == 0)
		return;

	size_t offset {0};
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_2CipherSuite::ECDHE_ECDSA_AES256_GCM_SHA384))
		offset = appendCipherString("ECDHE-ECDSA-AES256-GCM-SHA384", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_2CipherSuite::ECDHE_RSA_AES256_GCM_SHA384))
		offset = appendCipherString("ECDHE-RSA-AES256-GCM-SHA384", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_2CipherSuite::ECDHE_ECDSA_CHACHA20_POLY1305))
		offset = appendCipherString("ECDHE-ECDSA-CHACHA20-POLY1305", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_2CipherSuite::ECDHE_RSA_CHACHA20_POLY1305))
		offset = appendCipherString("ECDHE-RSA-CHACHA20-POLY1305", offset, ret);
}
static void generateTLS13CipherSuiteImpl(int32_t suites_bitmap, char* ret)
{
	ret[0] = '\0';
	if (suites_bitmap == 0)
		return;

	size_t offset {0};
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_3CipherSuite::TLS_AES_128_GCM_SHA256))
		offset = appendCipherString("TLS_AES_128_GCM_SHA256", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_3CipherSuite::TLS_AES_256_GCM_SHA384))
		offset = appendCipherString("TLS_AES_256_GCM_SHA384", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_3CipherSuite::TLS_CHACHA20_POLY1305_SHA256))
		offset = appendCipherString("TLS_CHACHA20_POLY1305_SHA256", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_3CipherSuite::TLS_AES_128_CCM_SHA256))
		offset = appendCipherString("TLS_AES_128_CCM_SHA256", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(SocketTLS1_3CipherSuite::TLS_AES_128_CCM8_SHA256))
		offset = appendCipherString("TLS_AES_128_CCM_8_SHA256", offset, ret);
}

void Bn3Monkey::SocketTLSClientConfiguration::generateTLS12CipherSuites(char* ret) const
{
	generateTLS12CipherSuiteImpl(_tls_1_2_cipher_suites, ret);
}
void Bn3Monkey::SocketTLSClientConfiguration::generateTLS13CipherSuites(char* ret) const
{
	generateTLS13CipherSuiteImpl(_tls_1_3_cipher_suites, ret);
}
void Bn3Monkey::SocketTLSServerConfiguration::generateTLS12CipherSuites(char* ret) const
{
	generateTLS12CipherSuiteImpl(_tls_1_2_cipher_suites, ret);
}
void Bn3Monkey::SocketTLSServerConfiguration::generateTLS13CipherSuites(char* ret) const
{
	generateTLS13CipherSuiteImpl(_tls_1_3_cipher_suites, ret);
}
