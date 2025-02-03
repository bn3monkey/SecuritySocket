#include "SecuritySocket.hpp"
#include "implementation/TCPStream.hpp"
#include "implementation/TCPClient.hpp"
#include "implementation/TCPServer.hpp"

using namespace Bn3Monkey;


Bn3Monkey::SocketClient::SocketClient(const ClientSocketConfiguration& configuration)
{
}

Bn3Monkey::SocketClient::~SocketClient()
{
}

Bn3Monkey::SocketResult Bn3Monkey::SocketClient::open()
{
}	
void Bn3Monkey::SocketClient::close()
{
}

Bn3Monkey::SocketResult Bn3Monkey::SocketClient::read(const void* buffer, size_t size)
{
}
Bn3Monkey::SocketResult Bn3Monkey::SocketClient::write(void* buffer, size_t size)
{
}


Bn3Monkey::SocketServer::SocketServer(const SocketConfiguration& configuration)
{
}

Bn3Monkey::SocketServer::~SocketServer()
{
}

bool Bn3Monkey::SocketServer::open(SocketEventHandler& handler)
{

}
void Bn3Monkey::SocketServer::close()
{
	
}