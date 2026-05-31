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

	_container = PassiveSocketContainer(_tls_configuration.valid(), _configuration.is_unix_domain());
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
		_routine.join();

		_socket->close();
	}
}

void Bn3Monkey::RequestServerImpl::run(RequestHandler* handler)
{
	SocketMultiEventListener listener;
	listener.open();

	SocketEventContext server_context;
	server_context.fd = _socket->descriptor();
	listener.addEvent(&server_context, SocketEventType::ACCEPT);

	HttpRouterImpl* router_ptr = _has_http ? &_router : nullptr;

	while (_is_running)
	{
		auto eventlist = listener.wait(_configuration.read_timeout());
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
					socket_container, *handler, router_ptr, _custom,
					_configuration.pdu_size(), _tls_configuration.valid());
				// Fixed-size pool (32): nullptr once exhausted. Drop the freshly
				// accepted socket — close it explicitly so its fd doesn't leak.
				if (connection == nullptr)
				{
					client_socket->close();
					continue;
				}

				connection->onAccept();
				handler->onConnected(*connection);
				listener.addEvent(connection, SocketEventType::READ);
			}
			else
			{
				auto* connection = static_cast<ClientConnectionImpl*>(context);
				const auto disposition = connection->handleEvent(type, listener);
				if (disposition == ClientConnectionImpl::Disposition::CLOSE)
				{
					handler->onDisconnected(*connection);
					listener.removeEvent(connection);
					connection->closeSocket();
					_socket_connection_pool.release(connection);
				}
			}
		}
	}

	listener.close();
}
