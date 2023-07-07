#include "SecuritySocket.hpp"
#include "implementation/TCPClient.hpp"
#include "implementation/TCPServer.hpp"

using namespace Bn3Monkey;

Bn3Monkey::TCPClient::TCPClient(const TCPConfiguration& configuration, TCPEventHandler& handler)
{
	_impl = std::make_shared<TCPClientImpl>(configuration, handler);
}

Bn3Monkey::TCPClient::~TCPClient()
{
	_impl.reset();
}

ConnectionResult Bn3Monkey::TCPClient::getLastError()
{
	return _impl->getLastError();
}

void Bn3Monkey::TCPClient::close()
{
	_impl->close();
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
