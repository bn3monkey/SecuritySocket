#include "SecuritySocket.hpp"
#include "implementation/TCPStream.hpp"
#include "implementation/TCPClient.hpp"
#include "implementation/TCPServer.hpp"

using namespace Bn3Monkey;


Bn3Monkey::TCPClient::TCPClient(const TCPConfiguration& configuration)
{
	_impl = std::make_shared<TCPClientImpl>(configuration);
}

Bn3Monkey::TCPClient::~TCPClient()
{
	_impl.reset();
}

ConnectionResult Bn3Monkey::TCPClient::getLastError()
{
	return _impl->getLastError();
}
void Bn3Monkey::TCPClient::open(const std::shared_ptr<TCPEventHandler>& handler)
{
	_impl->open(handler);
}	
void Bn3Monkey::TCPClient::close()
{
	_impl->close();
}

ConnectionResult Bn3Monkey::TCPClient::read(char* buffer, size_t size)
{
	return _impl->read(buffer, size);
}
ConnectionResult Bn3Monkey::TCPClient::write(char* buffer, size_t size)
{
	return _impl->write(buffer, size);
}


Bn3Monkey::TCPServer::TCPServer(const TCPConfiguration& configuration, TCPEventHandler& handler)
{
	_impl = std::make_shared<TCPServerImpl>(configuration, handler);
}

Bn3Monkey::TCPServer::~TCPServer()
{
	_impl.reset();
}

ConnectionResult Bn3Monkey::TCPServer::getLastError()
{
	return _impl->getLastError();
}

void Bn3Monkey::TCPServer::close()
{
	_impl->close();
}
