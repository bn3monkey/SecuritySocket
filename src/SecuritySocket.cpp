#include "SecuritySocket.hpp"
#include "implementation/SocketBroadcastServer.hpp"
#include "implementation/SocketRequestServer.hpp"
#include "implementation/SocketClient.hpp"
#include "implementation/SocketResult.hpp"

#include <openssl/ssl.h>
#include <openssl/err.h>

#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <WS2tcpip.h>

using namespace Bn3Monkey;

bool Bn3Monkey::initializeSecuritySocket()
{
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();
	
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


Bn3Monkey::SocketRequestServer::SocketRequestServer(const SocketConfiguration& configuration)
{
	new (_container) SocketRequestServerImpl(configuration);
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