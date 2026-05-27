#include "SecuritySocket.hpp"
#include "implementation/BroadcastServer.hpp"
#include "implementation/RequestServer.hpp"
#include "implementation/Client.hpp"
#include "implementation/NetworkResult.hpp"
#include "implementation/TlsHelper.hpp"

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

const char* Bn3Monkey::NetworkResult::message() {
	return getMessage(_code);
}

Bn3Monkey::Client::Client(const NetworkConfiguration& configuration)
{
	new (_container) ClientImpl (configuration);
}

Bn3Monkey::Client::Client(const NetworkConfiguration& configuration, const TlsClientConfiguration& tls_configuration)
{
	new (_container) ClientImpl (configuration, tls_configuration);
}


Bn3Monkey::Client::~Client()
{
	ClientImpl* impl = static_cast<ClientImpl*>((void*)_container);
	impl->~ClientImpl();
}

Bn3Monkey::NetworkResult Bn3Monkey::Client::open()
{
	ClientImpl* impl = static_cast<ClientImpl*>((void*)_container);
	return impl->open();
}	
void Bn3Monkey::Client::close()
{
	ClientImpl* impl = static_cast<ClientImpl*>((void*)_container);
	return impl->close();
}

Bn3Monkey::NetworkResult Bn3Monkey::Client::connect()
{
	ClientImpl* impl = static_cast<ClientImpl*>((void*)_container);
	return impl->connect();
}
Bn3Monkey::NetworkResult Bn3Monkey::Client::read(void* buffer, size_t size)
{
	ClientImpl* impl = static_cast<ClientImpl*>((void*)_container);
	return impl->read(buffer, size);
}
Bn3Monkey::NetworkResult Bn3Monkey::Client::write(const void* buffer, size_t size)
{
	ClientImpl* impl = static_cast<ClientImpl*>((void*)_container);
	return impl->write(buffer, size);
}
Bn3Monkey::NetworkResult Bn3Monkey::Client::isConnected()
{
	ClientImpl* impl = static_cast<ClientImpl*>((void*)_container);
	return impl->isConnected();
}

Bn3Monkey::RequestServer::RequestServer(const NetworkConfiguration& configuration)
{
	new (_container) RequestServerImpl(configuration);
}
Bn3Monkey::RequestServer::RequestServer(const NetworkConfiguration& configuration, const TlsServerConfiguration& tls_configuration)
{
	new (_container) RequestServerImpl(configuration, tls_configuration);
}

Bn3Monkey::RequestServer::~RequestServer()
{
	RequestServerImpl* impl = static_cast<RequestServerImpl*>((void*)_container);
	impl->~RequestServerImpl();
}

NetworkResult Bn3Monkey::RequestServer::open(CustomProtocolRequestHandler* handler, size_t num_of_clients)
{
	RequestServerImpl* impl = static_cast<RequestServerImpl*>((void*)_container);
	return impl->open(handler, num_of_clients);
}
void Bn3Monkey::RequestServer::close()
{
	RequestServerImpl* impl = static_cast<RequestServerImpl*>((void*)_container);
	return impl->close();
}

Bn3Monkey::BroadcastServer::BroadcastServer(const NetworkConfiguration& configuration)
{
	new (_container) BroadcastServerImpl(configuration);
}
Bn3Monkey::BroadcastServer::BroadcastServer(const NetworkConfiguration& configuration,  const TlsServerConfiguration& tls_configuration)
{
	new (_container) BroadcastServerImpl(configuration, tls_configuration);
}
Bn3Monkey::BroadcastServer::~BroadcastServer()
{
	BroadcastServerImpl* impl = static_cast<BroadcastServerImpl*>((void*)_container);
	impl->~BroadcastServerImpl();
}

NetworkResult Bn3Monkey::BroadcastServer::open(BroadcastHandler* handler, size_t num_of_clients)
{
	BroadcastServerImpl* impl = static_cast<BroadcastServerImpl*>((void*)_container);
	return impl->open(handler, num_of_clients);
}
void Bn3Monkey::BroadcastServer::close()
{
	BroadcastServerImpl* impl = static_cast<BroadcastServerImpl*>((void*)_container);
	return impl->close();
}
NetworkResult Bn3Monkey::BroadcastServer::write(const void* buffer, size_t size)
{
	BroadcastServerImpl* impl = static_cast<BroadcastServerImpl*>((void*)_container);
	return impl->write(buffer, size);
}
NetworkResult Bn3Monkey::BroadcastServer::await(uint64_t timeout_ms)
{
	BroadcastServerImpl* impl = static_cast<BroadcastServerImpl*>((void*)_container);
	return impl->await(timeout_ms);
}
NetworkResult Bn3Monkey::BroadcastServer::awaitClose(uint64_t timeout_ms)
{
	BroadcastServerImpl* impl = static_cast<BroadcastServerImpl*>((void*)_container);
	return impl->awaitClose(timeout_ms);
}
void Bn3Monkey::BroadcastServer::dropAll()
{
	BroadcastServerImpl* impl = static_cast<BroadcastServerImpl*>((void*)_container);
	impl->dropAll();
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
	if (suites_bitmap & static_cast<int32_t>(TlsV12CipherSuite::ECDHE_ECDSA_AES256_GCM_SHA384))
		offset = appendCipherString("ECDHE-ECDSA-AES256-GCM-SHA384", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(TlsV12CipherSuite::ECDHE_RSA_AES256_GCM_SHA384))
		offset = appendCipherString("ECDHE-RSA-AES256-GCM-SHA384", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(TlsV12CipherSuite::ECDHE_ECDSA_CHACHA20_POLY1305))
		offset = appendCipherString("ECDHE-ECDSA-CHACHA20-POLY1305", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(TlsV12CipherSuite::ECDHE_RSA_CHACHA20_POLY1305))
		offset = appendCipherString("ECDHE-RSA-CHACHA20-POLY1305", offset, ret);
}
static void generateTLS13CipherSuiteImpl(int32_t suites_bitmap, char* ret)
{
	ret[0] = '\0';
	if (suites_bitmap == 0)
		return;

	size_t offset {0};
	if (suites_bitmap & static_cast<int32_t>(TlsV13CipherSuite::TLS_AES_128_GCM_SHA256))
		offset = appendCipherString("TLS_AES_128_GCM_SHA256", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(TlsV13CipherSuite::TLS_AES_256_GCM_SHA384))
		offset = appendCipherString("TLS_AES_256_GCM_SHA384", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(TlsV13CipherSuite::TLS_CHACHA20_POLY1305_SHA256))
		offset = appendCipherString("TLS_CHACHA20_POLY1305_SHA256", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(TlsV13CipherSuite::TLS_AES_128_CCM_SHA256))
		offset = appendCipherString("TLS_AES_128_CCM_SHA256", offset, ret);
	if (suites_bitmap & static_cast<int32_t>(TlsV13CipherSuite::TLS_AES_128_CCM8_SHA256))
		offset = appendCipherString("TLS_AES_128_CCM_8_SHA256", offset, ret);
}

void Bn3Monkey::TlsClientConfiguration::generateTLS12CipherSuites(char* ret) const
{
	generateTLS12CipherSuiteImpl(_tls_1_2_cipher_suites, ret);
}
void Bn3Monkey::TlsClientConfiguration::generateTLS13CipherSuites(char* ret) const
{
	generateTLS13CipherSuiteImpl(_tls_1_3_cipher_suites, ret);
}
void Bn3Monkey::TlsServerConfiguration::generateTLS12CipherSuites(char* ret) const
{
	generateTLS12CipherSuiteImpl(_tls_1_2_cipher_suites, ret);
}
void Bn3Monkey::TlsServerConfiguration::generateTLS13CipherSuites(char* ret) const
{
	generateTLS13CipherSuiteImpl(_tls_1_3_cipher_suites, ret);
}
