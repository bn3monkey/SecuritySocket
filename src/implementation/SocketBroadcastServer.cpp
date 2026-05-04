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

	// Listener is now a member so dropAll() can call removeEvent on it from
	// the broadcast caller's thread. Register the accept fd here, before the
	// monitor thread starts polling.
	_listener.open();
	_server_context = SocketEventContext{};
	_server_context.fd = _socket->descriptor();
	_listener.addEvent(&_server_context, SocketEventType::ACCEPT);

	if (!_is_monitoring) {
		_is_monitoring = true;
		_monitor_client = std::thread{ &Bn3Monkey::SocketBroadcastServerImpl::monitorClient, this };
	}

	return result;
}

void Bn3Monkey::SocketBroadcastServerImpl::monitorClient()
{
	// A single multi-event listener owns both the accept fd and every accepted
	// client fd. The kernel folds peer-close (POLLHUP / POLLERR) into a
	// DISCONNECTED event for us, so connect and disconnect detection live
	// entirely in this loop.
	//
	// The listener and the accept-context are members (initialized in open())
	// rather than locals, so dropAll() can call _listener.removeEvent() from
	// the broadcast caller's thread.

	while (_is_monitoring)
	{
		// Release the strong refs held by dropAll() from the previous round.
		// Safe here: the previous wait+dispatch cycle (which may have held
		// stale context pointers in its local snapshot) is fully done by the
		// time control reaches the top of the loop.
		{
			std::lock_guard<std::mutex> lk(_clients_mtx);
			_pending_destruction.clear();
		}

		auto eventlist = _listener.wait(_configuration.read_timeout());
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
				_listener.addEvent(client.get(), SocketEventType::READ);

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
				_listener.removeEvent(client);

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
				// Only fire close + handler if we actually owned this client.
				// If dropAll() already drained it, we get a stale DISCONNECTED
				// for a context now sitting in _pending_destruction — handler
				// already ran inside dropAll, so skip here to avoid double-fire.
				if (erased) {
					if (auto* sock = erased->container.get()) sock->close();
					if (_handler)
						_handler->onClientDisconnected(ip_buf, port);
				}
				_clients_cv.notify_all();
			}
			break;

			default:
				break;
			}
		}
	}
}

void SocketBroadcastServerImpl::dropAll()
{
	// Atomically detach every active client from both the listener and the
	// active list. The listener.removeEvent calls happen under _clients_mtx
	// so no concurrent monitor dispatch can re-find these contexts in the
	// active list mid-mutation.
	std::vector<std::shared_ptr<BroadcastClient>> dropped;
	{
		std::lock_guard<std::mutex> lk(_clients_mtx);
		dropped.swap(_active_clients);
		for (auto& client : dropped) {
			_listener.removeEvent(client.get());
		}
		// Hand off the strong refs to _pending_destruction. The monitor
		// clears that list at the top of its next iteration — by which time
		// any in-flight wait+dispatch cycle holding stale snapshot pointers
		// is finished. Releasing the refs synchronously here would race that
		// dispatch and risk dereferencing freed BroadcastClients.
		_pending_destruction.insert(_pending_destruction.end(),
			dropped.begin(), dropped.end());
	}

	// Wake any await/awaitClose waiter — active list is now empty.
	_clients_cv.notify_all();

	// Close fds and fire handler outside the lock to avoid re-entrancy
	// surprises (handler is user code; might call back into the server).
	for (auto& client : dropped) {
		char ip_buf[22]{ 0 };
		int port = 0;
		if (auto* sock = client->container.get()) {
			std::snprintf(ip_buf, sizeof(ip_buf), "%s", sock->ip());
			port = sock->port();
			sock->close();
		}
		if (_handler)
			_handler->onClientDisconnected(ip_buf, port);
	}
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
	{
		std::unique_lock<std::mutex> lk(_clients_mtx);
		_clients_cv.wait_for(lk,
			std::chrono::milliseconds(timeout_ms),
			[this] { return !_active_clients.empty() || !_is_monitoring; });

		if (!_is_monitoring)
			return SocketResult(SocketCode::SOCKET_CLOSED);
		if (_active_clients.empty())
			return SocketResult(SocketCode::SOCKET_TIMEOUT);
	}

	std::lock_guard<std::mutex> lk(_clients_mtx);
	if (!_is_monitoring)
		return SocketResult(SocketCode::SOCKET_CLOSED);
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
	// fds the test/caller didn't drain via awaitClose / dropAll first, and
	// release the deferred-destruction list now that the monitor can no
	// longer reach those context pointers.
	{
		std::lock_guard<std::mutex> lk(_clients_mtx);
		for (auto& client : _active_clients) {
			if (auto* sock = client->container.get()) sock->close();
		}
		_active_clients.clear();
		_pending_destruction.clear();
	}

	_listener.close();

	if (_socket) {
		_socket->close();
		_socket = nullptr;
	}

	_handler = nullptr;
}
