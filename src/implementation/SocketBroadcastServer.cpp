#include "SocketBroadcastServer.hpp"
#include <stdexcept>

using namespace Bn3Monkey;

SocketBroadcastServerImpl::~SocketBroadcastServerImpl()
{
    close();
}
SocketResult SocketBroadcastServerImpl::open(size_t num_of_clients)
{
	(void)num_of_clients;

	SocketResult result = SocketResult(SocketCode::SUCCESS);

	_container = PassiveSocketContainer(_configuration.tls(), _configuration.is_unix_domain());
	_socket = _container.get();
	result = _socket->valid();
	if (result.code() != SocketCode::SUCCESS)
	{
		return result;
	}

	SocketAddress address{ _configuration.ip(), _configuration.port(), true, _configuration.is_unix_domain() };
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

	for (auto i = 0u; i < _configuration.max_retries();)
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
			break;
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

		for (auto i = 0u; i < _configuration.max_retries(); )
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

			result = sock->write((char*)buffer + written_size, size - written_size);
			if (result.code() == SocketCode::SOCKET_TIMEOUT)
			{
				i++;
			}
			else if (result.code() != SocketCode::SUCCESS)
			{
				result = SocketResult(result.code(), static_cast<int32_t>(written_size));
				_back_client_containers->push_back(client_container);
				break;
			}
			written_size += (size_t)result.bytes();
			if (written_size == size)
			{
				result = SocketResult(result.code(), static_cast<int32_t>(written_size));
				_back_client_containers->push_back(client_container);
				break;
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