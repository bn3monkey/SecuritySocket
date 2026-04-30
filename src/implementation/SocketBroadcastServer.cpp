#include "SocketBroadcastServer.hpp"
#include <stdexcept>
#include <algorithm>

using namespace Bn3Monkey;

SocketBroadcastServerImpl::~SocketBroadcastServerImpl()
{
    close();
}

SocketResult SocketBroadcastServerImpl::open(SocketBroadcastHandler* handler, size_t num_of_clients)
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

	_handler = handler;

	if (!_is_monitoring) {
		_is_monitoring = true;
		_monitor_client = std::thread{ &Bn3Monkey::SocketBroadcastServerImpl::monitorClient, this };
	}

	return result;
}

void Bn3Monkey::SocketBroadcastServerImpl::monitorClient()
{
	// Mirror the SocketRequestServer pattern: a single multi-event listener
	// owns both the accept fd and every accepted client fd. The kernel folds
	// peer-close (POLLHUP / POLLERR) into a DISCONNECTED event for us, so we
	// don't have to poll, peek, or run health checks elsewhere — connect and
	// disconnect detection live entirely in this loop.
	SocketMultiEventListener listener;
	listener.open();

	SocketEventContext server_context;
	server_context.fd = _socket->descriptor();
	listener.addEvent(&server_context, SocketEventType::ACCEPT);

	while (_is_monitoring)
	{
		auto eventlist = listener.wait(_configuration.read_timeout());
		if (eventlist.result.code() == SocketCode::SOCKET_TIMEOUT)
			continue;
		if (eventlist.result.code() != SocketCode::SUCCESS)
			break;

		for (auto* context : eventlist.contexts)
		{
			switch (context->type)
			{
			case SocketEventType::ACCEPT:
			{
				auto socket_container = _socket->accept();
				auto* client_socket = socket_container.get();
				if (client_socket->result().code() != SocketCode::SUCCESS)
					break;

				// Broadcast latency > coalescing throughput: disable Nagle so
				// each write() reaches the wire immediately.
				client_socket->setNoDelay();

				auto client = std::make_shared<BroadcastClient>();
				client->container = socket_container;
				client->fd = client_socket->descriptor();
				listener.addEvent(client.get(), SocketEventType::READ);

				char ip_buf[22];
				int port = client_socket->port();
				std::snprintf(ip_buf, sizeof(ip_buf), "%s", client_socket->ip());

				{
					std::lock_guard<std::mutex> lk(_clients_mtx);
					_active_clients.push_back(std::move(client));
				}
				_clients_cv.notify_all();

				if (_handler)
					_handler->onClientConnected(ip_buf, port);
			}
			break;

			case SocketEventType::DISCONNECTED:
			{
				auto* client = static_cast<BroadcastClient*>(context);
				listener.removeEvent(client);

				char ip_buf[22];
				int port = 0;
				if (auto* sock = client->container.get()) {
					std::snprintf(ip_buf, sizeof(ip_buf), "%s", sock->ip());
					port = sock->port();
				}

				std::shared_ptr<BroadcastClient> erased;
				{
					std::lock_guard<std::mutex> lk(_clients_mtx);
					auto it = std::find_if(
						_active_clients.begin(), _active_clients.end(),
						[client](const std::shared_ptr<BroadcastClient>& sp) {
							return sp.get() == client;
						});
					if (it != _active_clients.end())
					{
						erased = *it;
						_active_clients.erase(it);
					}
				}
				if (erased) {
					if (auto* sock = erased->container.get()) sock->close();
				}
				_clients_cv.notify_all();

				if (_handler)
					_handler->onClientDisconnected(ip_buf, port);
			}
			break;

			default:
				break;
			}
		}
	}

	listener.close();
}

SocketResult SocketBroadcastServerImpl::write(const void* buffer, size_t size)
{
	// Snapshot the active list under lock, then stream bytes lock-free.
	// Using shared_ptr ensures that even if the monitor erases an entry
	// mid-broadcast (DISCONNECTED), the BroadcastClient stays alive for
	// the rest of this call.
	std::vector<std::shared_ptr<BroadcastClient>> snapshot;
	{
		std::lock_guard<std::mutex> lk(_clients_mtx);
		snapshot = _active_clients;
	}

	if (snapshot.empty())
		return SocketResult(SocketCode::SUCCESS, 0);

	SocketResult result{ SocketCode::SUCCESS };

	for (auto& client : snapshot)
	{
		auto* sock = client->container.get();
		if (!sock) continue;

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
			if (inner_result.code() == SocketCode::SOCKET_CLOSED)
			{
				// Peer closed mid-broadcast. The monitor's listener will fire
				// DISCONNECTED for the same fd, so we just stop targeting this
				// client here — no list mutation from the broadcast caller.
				per_client_result = SocketResult(SocketCode::SOCKET_CLOSED,
					static_cast<int32_t>(written_size));
				break;
			}
			if (inner_result.code() != SocketCode::SUCCESS)
			{
				per_client_result = SocketResult(inner_result.code(),
					static_cast<int32_t>(written_size));
				break;
			}

			inner_result = sock->write(static_cast<const char*>(buffer) + written_size,
				size - written_size);
			if (inner_result.code() == SocketCode::SOCKET_TIMEOUT)
			{
				i++;
				continue;
			}
			if (inner_result.code() == SocketCode::SOCKET_CLOSED)
			{
				per_client_result = SocketResult(SocketCode::SOCKET_CLOSED,
					static_cast<int32_t>(written_size));
				break;
			}
			if (inner_result.code() != SocketCode::SUCCESS)
			{
				per_client_result = SocketResult(inner_result.code(),
					static_cast<int32_t>(written_size));
				break;
			}

			written_size += static_cast<size_t>(inner_result.bytes());
			if (written_size >= size)
			{
				per_client_result = SocketResult(SocketCode::SUCCESS,
					static_cast<int32_t>(written_size));
				break;
			}
		}

		result = per_client_result;
	}

	return result;
}

SocketResult SocketBroadcastServerImpl::await(uint64_t timeout_ms)
{
	std::unique_lock<std::mutex> lk(_clients_mtx);
	_clients_cv.wait_for(lk,
		std::chrono::milliseconds(timeout_ms),
		[this] { return !_active_clients.empty() || !_is_monitoring; });

	if (!_is_monitoring)
		return SocketResult(SocketCode::SOCKET_CLOSED);
	if (_active_clients.empty())
		return SocketResult(SocketCode::SOCKET_TIMEOUT);

	return SocketResult(SocketCode::SUCCESS,
		static_cast<int32_t>(_active_clients.size()));
}

SocketResult SocketBroadcastServerImpl::awaitClose(uint64_t timeout_ms)
{
	std::unique_lock<std::mutex> lk(_clients_mtx);
	_clients_cv.wait_for(lk,
		std::chrono::milliseconds(timeout_ms),
		[this] { return _active_clients.empty() || !_is_monitoring; });

	if (!_is_monitoring)
		return SocketResult(SocketCode::SOCKET_CLOSED);
	if (!_active_clients.empty())
		return SocketResult(SocketCode::SOCKET_TIMEOUT,
			static_cast<int32_t>(_active_clients.size()));

	return SocketResult(SocketCode::SUCCESS, 0);
}

void SocketBroadcastServerImpl::close()
{
	if (_is_monitoring) {
		_is_monitoring = false;
		// Wake any await/awaitClose waiters so they return SOCKET_CLOSED
		// instead of waiting out their timeout.
		_clients_cv.notify_all();
		if (_monitor_client.joinable())
			_monitor_client.join();
	}

	// Monitor has joined — no more producers. Close any remaining client
	// fds the test/caller didn't drain via awaitClose first.
	{
		std::lock_guard<std::mutex> lk(_clients_mtx);
		for (auto& client : _active_clients) {
			if (auto* sock = client->container.get()) sock->close();
		}
		_active_clients.clear();
	}

	if (_socket) {
		_socket->close();
		_socket = nullptr;
	}

	_handler = nullptr;
}
