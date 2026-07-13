#include "RequestServer.hpp"
#include "NetworkResult.hpp"

// RequestServer stores this Impl inline via placement-new into a fixed
// char[IMPLEMENTATION_SIZE] buffer (PImpl-by-inline-storage). If the Impl
// outgrows that buffer the constructor scribbles past it and corrupts adjacent
// memory — which surfaced as a teardown access violation in ~HttpRouterImpl
// (the _router deque lives near the tail of the object). Catch any future
// growth at compile time instead of at runtime.
static_assert(sizeof(Bn3Monkey::RequestServerImpl)
                  <= Bn3Monkey::RequestServer::IMPLEMENTATION_SIZE,
              "RequestServerImpl no longer fits in RequestServer::_container; "
              "raise RequestServer::IMPLEMENTATION_SIZE in SecuritySocket.hpp");

Bn3Monkey::RequestServerImpl::~RequestServerImpl()
{
	close();
}

Bn3Monkey::NetworkResult Bn3Monkey::RequestServerImpl::open(RequestHandler* handler, size_t num_of_clients)
{
	(void)num_of_clients;

	if (_is_running)
	{
		return NetworkResult(NetworkResultCode::SOCKET_SERVER_ALREADY_RUNNING);
	}

	// Resolve handler capabilities without a cast (virtual base + RTTI-off — see
	// SecuritySocket.hpp). HTTP routes are built once here, before the loop.
	HttpRequestHandler* http = handler ? handler->asHttpRequestHandler() : nullptr;
	_custom   = handler ? handler->asCustomProtocolRequestHandler() : nullptr;
	_has_http = (http != nullptr);
	if (http == nullptr && _custom == nullptr)
	{
		return NetworkResult(NetworkResultCode::SOCKET_INVALID_ARGUMENT);
	}
	if (http)
	{
		http->registerRoutes(_router);
	}

	NetworkResult result = NetworkResult(NetworkResultCode::SUCCESS);

	_container = PassiveSocketContainer(_tls_configuration.valid(),
	                                   _configuration.is_unix_domain(), _tls_configuration);
	_socket = _container.get();
	result = _socket->valid();
	if (result.code() != NetworkResultCode::SUCCESS)
	{
		return result;
	}

	SocketAddress address{ _configuration.ip(), _configuration.port(), true, _configuration.is_unix_domain() };
	result = address;
	if (result.code() != NetworkResultCode::SUCCESS) {
		return result;
	}

	result = _socket->bind(address);
	if (result.code() != NetworkResultCode::SUCCESS) {
		return result;
	}

	result = _socket->listen();
	if (result.code() != NetworkResultCode::SUCCESS)
	{
		return result;
	}

	_is_running = true;
	_routine = std::thread{ &RequestServerImpl::run, this, handler };
	return result;
}

void Bn3Monkey::RequestServerImpl::close()
{
	if (_is_running)
	{
		_is_running = false;
		// Wake the event loop out of epoll_wait so it observes _is_running and
		// exits now, instead of blocking until the current read_timeout lapses.
		_listener.wake();
		_routine.join();

		// Tear the listener down only AFTER the run thread has fully exited.
		// Doing it here (not inside run()) guarantees close() never races the
		// wake() above: wake() writes to the wakeup fd while the loop is still
		// live, and the fd is only closed once nothing can wake() it anymore.
		_listener.close();
		_socket->close();
	}
}

void Bn3Monkey::RequestServerImpl::run(RequestHandler* handler)
{
	_listener.open();

	SocketEventContext server_context;
	server_context.fd = _socket->descriptor();
	_listener.addEvent(&server_context, SocketEventType::ACCEPT);

	HttpRouterImpl* router_ptr = _has_http ? &_router : nullptr;

	while (_is_running)
	{
		auto eventlist = _listener.wait(_configuration.read_timeout());
		const auto code = eventlist.result.code();
		if (code == NetworkResultCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (code != NetworkResultCode::SUCCESS)
		{
			break;
		}

		for (auto& context : eventlist.contexts)
		{
			const SocketEventType type = context->type;

			if (type == SocketEventType::ACCEPT)
			{
				auto socket_container = _socket->accept();
				auto* client_socket = socket_container.get();
				if (client_socket->result().code() != NetworkResultCode::SUCCESS)
				{
					continue;
				}

				ClientConnectionImpl* connection = _socket_connection_pool.acquire(
					std::move(socket_container), *handler, router_ptr, _custom,
					_configuration.pdu_size(),
					_configuration.max_http_request_body_size());
				// Fixed-size pool (32): nullptr once exhausted. acquire() returns
				// before it forwards its arguments, so the container still owns the
				// socket here — close it explicitly so its fd doesn't leak.
				if (connection == nullptr)
				{
					socket_container.get()->close();
					continue;
				}

				connection->onAccept();
				// TLS fires onConnected() itself once the handshake completes, so a
				// connection that dies mid-handshake is never announced to the
				// handler at all. Plaintext has nothing to wait for.
				if (!connection->isSecure())
				{
					handler->onConnected(*connection);
				}
				_listener.addEvent(connection, SocketEventType::READ);
			}
			else
			{
				auto* connection = static_cast<ClientConnectionImpl*>(context);
				const auto disposition = connection->handleEvent(type, _listener);
				if (disposition == ClientConnectionImpl::Disposition::CLOSE)
				{
					// Keep the callbacks paired: a TLS handshake that failed never
					// produced an onConnected(), so it must not produce an
					// onDisconnected() for a connection the handler never saw.
					if (connection->connectedNotified())
					{
						handler->onDisconnected(*connection);
					}
					_listener.removeEvent(connection);
					connection->closeSocket();
					_socket_connection_pool.release(connection);
				}
			}
		}
	}
	// NB: _listener.close() is intentionally NOT called here. The owning
	// RequestServerImpl::close() tears the listener down after join()ing this
	// thread, so a concurrent wake() can never write to a closed wakeup fd.
}
