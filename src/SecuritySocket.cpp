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


Bn3Monkey::SocketRequestServer::SocketRequestServer(const SocketConfiguration& configuration)
{
	new (_container) SocketReuqestServerImpl(configuration);
}

Bn3Monkey::SocketRequestServer::~SocketRequestServer()
{
	SocketReuqestServerImpl* impl = static_cast<SocketReuqestServerImpl*>((void*)_container);
	impl->~SocketReuqestServerImpl();
}

SocketResult Bn3Monkey::SocketRequestServer::open(SocketRequestHandler& handler, size_t num_of_clients)
{
	SocketReuqestServerImpl* impl = static_cast<SocketReuqestServerImpl*>((void*)_container);
	return impl->open(handler, num_of_clients);
}
void Bn3Monkey::SocketRequestServer::close()
{
	SocketReuqestServerImpl* impl = static_cast<SocketReuqestServerImpl*>((void*)_container);
	return impl->close();
}