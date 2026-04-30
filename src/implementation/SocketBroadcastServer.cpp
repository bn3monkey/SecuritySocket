#include "SocketBroadcastServer.hpp"
#include <stdexcept>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

using namespace Bn3Monkey;

namespace
{
	// Health check probe used by SocketBroadcastServerImpl::await() to decide
	// whether an entry in _active_clients still corresponds to a live peer.
	//
	// Detection covers both reliable signals of a graceful close:
	//   1. POLLHUP — peer's FIN was received and surfaced by the kernel.
	//      Reported by the project's SocketEventListener as SOCKET_CLOSED.
	//   2. POLLIN + 0-byte peek — some kernels deliver EOF only via POLLIN
	//      (without HUP). recv(MSG_PEEK) returning 0 means the peer closed
	//      its write side without sending data.
	//
	// Anything else (timeout, peeked >0 bytes, EAGAIN) is treated as alive.
	// Application-level pings are intentionally avoided so this stays
	// transparent to broadcast-listener clients.
	bool isPeerClosed(Bn3Monkey::ServerActiveSocket* sock, uint32_t timeout_ms)
	{
		Bn3Monkey::SocketEventListener listener;
		listener.open(*sock, Bn3Monkey::SocketEventType::READ);
		auto r = listener.wait(timeout_ms);
		if (r.code() == Bn3Monkey::SocketCode::SOCKET_CLOSED)
			return true;
		if (r.code() == Bn3Monkey::SocketCode::SUCCESS)
		{
			char b;
			int n = ::recv(sock->descriptor(), &b, 1, MSG_PEEK);
			if (n == 0)
				return true;
		}
		return false;
	}
}

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
		// Wake any await() blocked on _pending_cv.
		_pending_cv.notify_all();
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

SocketResult SocketBroadcastServerImpl::await(uint64_t timeout_ms)
{
	// Two-phase wait:
	//   Phase 1 — health-check existing _active_clients. Each one is given a
	//             brief grace window to surface a peer-close (FIN). Without
	//             this grace, a stale client from a previous round could be
	//             classified as alive simply because its FIN hasn't been
	//             observed yet by the kernel.
	//   Phase 2 — if nothing in _active_clients survived (or it was empty to
	//             begin with), block on _pending_cv until a new client
	//             connects, close() is called, or the timeout elapses.
	//
	// The total wall-clock cost is bounded by `timeout_ms`: phase 1 burns at
	// most `health_grace_ms` per active client, and phase 2 uses what's left.
	auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(timeout_ms);

	drainPending();

	constexpr uint32_t health_grace_ms = 100;
	for (auto it = _active_clients.begin(); it != _active_clients.end(); )
	{
		if (isPeerClosed(it->get(), health_grace_ms))
		{
			it->get()->close();
			it = _active_clients.erase(it);
		}
		else
		{
			++it;
		}
	}

	if (!_active_clients.empty())
		return SocketResult(SocketCode::SUCCESS,
			static_cast<int32_t>(_active_clients.size()));

	auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
		deadline - std::chrono::steady_clock::now());
	if (remaining.count() <= 0)
		return SocketResult(SocketCode::SOCKET_TIMEOUT);

	std::unique_lock<std::mutex> lk(_pending_mtx);
	_pending_cv.wait_for(lk, remaining,
		[this] { return !_pending_clients.empty() || !_is_monitoring; });

	if (!_is_monitoring)
		return SocketResult(SocketCode::SOCKET_CLOSED);
	if (_pending_clients.empty())
		return SocketResult(SocketCode::SOCKET_TIMEOUT);

	// Drain inline — we already hold the lock.
	_active_clients.insert(_active_clients.end(),
		_pending_clients.begin(), _pending_clients.end());
	_pending_clients.clear();

	return SocketResult(SocketCode::SUCCESS,
		static_cast<int32_t>(_active_clients.size()));
}

SocketResult SocketBroadcastServerImpl::awaitClose(uint64_t timeout_ms)
{
	// Block until every currently-active client has closed (peer FIN received),
	// or until the timeout elapses. Used by the caller as an explicit barrier
	// between broadcast rounds: it ensures the previous round's clients have
	// finished consuming and disconnected before the next await() starts.
	//
	// Strategy:
	//   - drainPending(): treat just-accepted clients as "active to drain" too,
	//     so they don't sneak through the barrier in pending state.
	//   - First pass with grace=0: prune clients whose FIN already arrived. If
	//     all of _active_clients are gone after this, return immediately —
	//     that's user's "if all dead, exit without waiting".
	//   - Otherwise, poll each remaining client in short steps (50 ms) until
	//     either everyone leaves or the deadline is reached.
	auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(timeout_ms);

	auto remaining_ms = [&]() -> int64_t {
		auto r = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now()).count();
		return r > 0 ? r : 0;
	};

	auto sweep_dead = [&](uint32_t per_check_ms) {
		drainPending();
		for (auto it = _active_clients.begin(); it != _active_clients.end(); )
		{
			if (isPeerClosed(it->get(), per_check_ms))
			{
				it->get()->close();
				it = _active_clients.erase(it);
			}
			else
			{
				++it;
			}
		}
	};

	// First pass: zero-wait probe.
	sweep_dead(0);
	if (_active_clients.empty())
		return SocketResult(SocketCode::SUCCESS, 0);

	// Subsequent passes: short blocking probes until empty or deadline.
	constexpr uint32_t step_ms = 50;
	while (remaining_ms() > 0)
	{
		auto step = static_cast<uint32_t>(std::min<int64_t>(remaining_ms(), step_ms));
		sweep_dead(step);
		if (_active_clients.empty())
			return SocketResult(SocketCode::SUCCESS, 0);
		if (!_is_monitoring)
			return SocketResult(SocketCode::SOCKET_CLOSED);
	}

	return SocketResult(SocketCode::SOCKET_TIMEOUT,
		static_cast<int32_t>(_active_clients.size()));
}

void SocketBroadcastServerImpl::close()
{
	if (_is_monitoring) {
		_is_monitoring = false;
		// Wake any await() blocked on _pending_cv so it returns SOCKET_CLOSED
		// instead of waiting out its timeout.
		_pending_cv.notify_all();
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
