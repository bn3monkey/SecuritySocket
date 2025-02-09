#include "SocketBroadcastServer.hpp"
#include <stdexcept>

using namespace Bn3Monkey;

SocketBroadcastServerImpl::~SocketBroadcastServerImpl()
{
    close();
}
SocketResult SocketBroadcastServerImpl::open(size_t num_of_clients)
{
	SocketResult result = SocketResult(SocketCode::SUCCESS);

	bool is_unix_domain = SocketAddress::checkUnixDomain(_configuration.ip());
	_container = PassiveSocketContainer(_configuration.tls(), is_unix_domain);
	_socket = _container.get();
	result = _socket->valid();
	if (result.code() != SocketCode::SUCCESS)
	{
		return result;
	}

	SocketAddress address{ _configuration.ip(), _configuration.port(), true };
	result = address;
	if (result.code() != SocketCode::SUCCESS) {
		return result;
	}

	result = _socket->bind(address);
	if (result.code() != SocketCode::SUCCESS) {
		return result;
	}

	result = _socket->listen();
	if (result.code() != SocketCode::SUCCESS)
	{
		return result;
	}

	return result;
}

SocketResult Bn3Monkey::SocketBroadcastServerImpl::enumerate()
{
	SocketResult result;

	SocketEventListener listener;
	listener.open(*_socket, SocketEventType::ACCEPT);

	for (int i = 0; i < _configuration.max_retries();)
	{
		result = listener.wait(_configuration.read_timeout());
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			i++;
			continue;
		}
		else if (result.code() != SocketCode::SUCCESS)
			break;
		else {
			auto client_container = _socket->accept();
			_front_client_containers->push_back(client_container);
		}
	}

	return result;
}

SocketResult SocketBroadcastServerImpl::write(const void* buffer, size_t size)
{
	SocketResult result;

	SocketEventListener listener;
	_back_client_containers->clear();
	for (auto& client_container : *_front_client_containers)
	{
		auto* sock = client_container.get();
		listener.open(*sock, SocketEventType::WRITE);
		size_t written_size = 0;

		for (auto i = 0; i < _configuration.max_retries(); i++)
		{
			auto result = listener.wait(_configuration.write_timeout());
			if (result.code() == SocketCode::SOCKET_TIMEOUT)
			{
				i++;
				continue;
			}
			else if (result.code() == SocketCode::SOCKET_CLOSED)
			{
				sock->close();
			}
			else if (result.code() != SocketCode::SUCCESS)
				break;

			auto ret = sock->write((char*)buffer + written_size, size - written_size);
			result = createResult(ret);
			if (result.code() == SocketCode::SOCKET_TIMEOUT)
			{
				i++;
			}
			else if (result.code() != SocketCode::SUCCESS)
			{
				_back_client_containers->push_back(client_container);
			}
			written_size += (size_t)ret;
			if (written_size == size)
			{
				_back_client_containers->push_back(client_container);
			}
		}
	}

	auto* temp = _front_client_containers;
	_front_client_containers = _back_client_containers;
	_back_client_containers = temp;

	return result;
}

void SocketBroadcastServerImpl::close()
{
	_socket->close();
}