#include "TCPServer.hpp"

Bn3Monkey::TCPServerImpl::TCPServerImpl(const TCPConfiguration& configuration, TCPEventHandler& handler)
	: TCPStream(nullptr, configuration.max_retries, configuration.pdu_size), _handler(handler)
{
}

Bn3Monkey::TCPServerImpl::~TCPServerImpl()
{
}

void Bn3Monkey::TCPServerImpl::close()
{
}
