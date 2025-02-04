#include "SecuritySocket.hpp"
#include "implementation/SocketServer.hpp"
#include "implementation/SocketClient.hpp"

using namespace Bn3Monkey;


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
	impl->open();
}	
void Bn3Monkey::SocketClient::close()
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	impl->close();
}

Bn3Monkey::SocketResult Bn3Monkey::SocketClient::connect()
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->connect();
}
Bn3Monkey::SocketResult Bn3Monkey::SocketClient::read(void* buffer, size_t* size)
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->read(buffer, size);
}
Bn3Monkey::SocketResult Bn3Monkey::SocketClient::write(const void* buffer, size_t size)
{
	SocketClientImpl* impl = static_cast<SocketClientImpl*>((void*)_container);
	return impl->write(buffer, size);
}


Bn3Monkey::SocketServer::SocketServer(const SocketConfiguration& configuration)
{
	new (_container) SocketServerImpl(configuration);
}

Bn3Monkey::SocketServer::~SocketServer()
{
	SocketServerImpl* impl = static_cast<SocketServerImpl*>((void*)_container);
	impl->~SocketServerImpl();
}

bool Bn3Monkey::SocketServer::open(SocketEventHandler& handler, size_t num_of_clients)
{
	SocketServerImpl* impl = static_cast<SocketServerImpl*>((void*)_container);
	impl->open(handler, num_of_clients);
}
void Bn3Monkey::SocketServer::close()
{
	SocketServerImpl* impl = static_cast<SocketServerImpl*>((void*)_container);
	impl->close();
}