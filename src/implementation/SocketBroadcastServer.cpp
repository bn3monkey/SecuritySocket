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

	_container = PassiveSocketContainer(_tls_configuration.valid(), _configuration.is_unix_domain());
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

	if (!_is_monitoring) {
		_is_monitoring = true;
		_monitor_client = std::thread { &Bn3Monkey::SocketBroadcastServerImpl::monitorClient, this};
	}

	return result;
}

void Bn3Monkey::SocketBroadcastServerImpl::monitorClient()
{
	SocketEventListener listener;
	listener.open(*_socket, SocketEventType::ACCEPT);

	while (_is_monitoring) {
		auto result = listener.wait(_configuration.read_timeout());
		if (result.code() == SocketCode::SOCKET_TIMEOUT) {
			continue;
		}
		else if (result.code() != SocketCode::SUCCESS)
			break;

		auto client_container = _socket->accept();
		{
			std::lock_guard<std::mutex> lk(_pending_mtx);
			_pending_clients.push_back(client_container);
		}
	}
}

void SocketBroadcastServerImpl::drainPending()
{
	std::lock_guard<std::mutex> lk(_pending_mtx);
	if (_pending_clients.empty())
		return;
	_active_clients.insert(_active_clients.end(),
		_pending_clients.begin(), _pending_clients.end());
	_pending_clients.clear();
}


SocketResult SocketBroadcastServerImpl::write(const void* buffer, size_t size)
{
	drainPending();

	if (_active_clients.empty())
		return SocketResult(SocketCode::SUCCESS, 0);

	SocketResult result{ SocketCode::SUCCESS };

	for (auto it = _active_clients.begin(); it != _active_clients.end(); )
	{
		auto* sock = it->get();
		bool alive = true;
		SocketResult per_client_result{ SocketCode::SUCCESS };

		SocketEventListener listener;
		listener.open(*sock, SocketEventType::WRITE);
		size_t written_size = 0;

		for (auto i = 0u; i < _configuration.max_retries(); )
		{
			auto inner_result = listener.wait(_configuration.write_timeout());
			if (inner_result.code() == SocketCode::SOCKET_TIMEOUT)
			{
				i++;
				continue;
			}
			else if (inner_result.code() == SocketCode::SOCKET_CLOSED)
			{
				alive = false;
				break;
			}
			else if (inner_result.code() != SocketCode::SUCCESS)
			{
				per_client_result = SocketResult(inner_result.code(), static_cast<int32_t>(written_size));
				break;
			}

			inner_result = sock->write((char*)buffer + written_size, size - written_size);
			if (inner_result.code() == SocketCode::SOCKET_TIMEOUT)
			{
				i++;
				continue;
			}
			else if (inner_result.code() == SocketCode::SOCKET_CLOSED)
			{
				alive = false;
				break;
			}
			else if (inner_result.code() != SocketCode::SUCCESS)
			{
				per_client_result = SocketResult(inner_result.code(), static_cast<int32_t>(written_size));
				break;
			}

			written_size += static_cast<size_t>(inner_result.bytes());
			if (written_size >= size)
			{
				per_client_result = SocketResult(SocketCode::SUCCESS, static_cast<int32_t>(written_size));
				break;
			}
		}

		if (!alive)
		{
			sock->close();
			it = _active_clients.erase(it);
		}
		else
		{
			result = per_client_result;
			++it;
		}
	}

	return result;
}

void SocketBroadcastServerImpl::close()
{
	if (_is_monitoring) {
		_is_monitoring = false;
		if (_monitor_client.joinable())
			_monitor_client.join();
	}

	// Monitor thread is joined; no more producer. Lock kept for memory ordering
	// and to keep the access pattern uniform.
	{
		std::lock_guard<std::mutex> lk(_pending_mtx);
		for (auto& c : _pending_clients) {
			if (auto* s = c.get()) s->close();
		}
		_pending_clients.clear();
	}

	for (auto& c : _active_clients) {
		if (auto* s = c.get()) s->close();
	}
	_active_clients.clear();

	if (_socket) {
		_socket->close();
		_socket = nullptr;
	}
}
