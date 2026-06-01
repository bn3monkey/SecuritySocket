#include "Client.hpp"
#include "NetworkResult.hpp"

#include <thread>
#include <chrono>

using namespace Bn3Monkey;

// Client stores ClientImpl inline via placement-new into a fixed
// char[IMPLEMENTATION_SIZE] buffer (PImpl-by-inline-storage). If the Impl
// outgrows the buffer the constructor scribbles past it and corrupts adjacent
// memory; catch any future growth at compile time. (See RequestServer.cpp for
// the teardown-AV this guards against.)
static_assert(sizeof(Bn3Monkey::ClientImpl)
                  <= Bn3Monkey::Client::IMPLEMENTATION_SIZE,
              "ClientImpl no longer fits in Client::_container; "
              "raise Client::IMPLEMENTATION_SIZE in SecuritySocket.hpp");

ClientImpl::~ClientImpl()
{
	close();
}

NetworkResult ClientImpl::open()
{
	NetworkResult result;
	
	_container = SocketContainer<ClientActiveSocket, TlsClientActiveSocket>(
		_tls_configuration.valid(),
		_configuration.is_unix_domain(),
		_tls_configuration,
		_configuration.ip());
	_socket = _container.get();
	result = _socket->valid();
	if (result.code() != NetworkResultCode::SUCCESS)
	{
		return result;
	}
	return result;	
}
void ClientImpl::close()
{
	_socket->disconnect();
	_socket->close();
}



NetworkResult ClientImpl::connect()
{
	NetworkResult result;

	SocketAddress address{_configuration.ip(), _configuration.port(), false, _configuration.is_unix_domain()};
	if (result.code() != NetworkResultCode::SUCCESS) {
		return result;
	}

	{
		SocketEventListener event_listener;
		event_listener.open(*_socket, SocketEventType::CONNECT);

		for (size_t i = 0; i < _configuration.max_retries(); i++)
		{
			result = _socket->connect(address, _configuration.read_timeout(), _configuration.write_timeout());
			if (result.code() == NetworkResultCode::SUCCESS)
			{
				break;
			}
			else if (result.code() == NetworkResultCode::SOCKET_CONNECTION_IN_PROGRESS || 
					  result.code() == NetworkResultCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED)
			{
				result = event_listener.wait(_configuration.read_timeout());
				if (result.code() == NetworkResultCode::SOCKET_TIMEOUT)
				{
					i++;
				}
				else if (result.code() != NetworkResultCode::SUCCESS)
				{
					return result;
				}
				else {
					break;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(_configuration.time_between_retries()));
		}

		if (result.code() != NetworkResultCode::SUCCESS)
		{
			return result;
		}

		// Phase 1: TLS handshake — retry reconnect(false) until SSL_connect completes
		for (size_t i = 0; i < _configuration.max_retries(); )
		{
			result = _socket->reconnect(false);
			if (result.code() == NetworkResultCode::SUCCESS)
			{
				break;
			}
			else if (result.code() == NetworkResultCode::SOCKET_CONNECTION_IN_PROGRESS
				|| result.code() == NetworkResultCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED)
			{
				result = event_listener.wait(_configuration.read_timeout());
				if (result.code() == NetworkResultCode::SOCKET_TIMEOUT)
				{
					i++;
				}
				else if (result.code() != NetworkResultCode::SUCCESS)
				{
					return result;
				}
				// Event fired — retry reconnect(false) so SSL_connect can process
				// the server response (handshake message or rejection alert)
				continue;
			}
			else
			{
				// Hard TLS error (version mismatch, cert invalid, etc.) — return immediately
				return result;
			}
		}

		if (result.code() != NetworkResultCode::SUCCESS)
		{
			return result;
		}
	}

	// Phase 2: post-handshake probe — TLS 1.3 deferred rejection detection.
	// postHandshakeProbe() returns SOCKET_CONNECTION_NEED_TO_BE_BLOCKED when no
	// data is buffered yet.  We wait once via the event listener (POLLIN):
	//   SOCKET_TIMEOUT  → no rejection alert arrived within read_timeout → accepted.
	//   SUCCESS (data)  → retry probe to process the rejection alert.
	//   other error     → propagate.
	{
		SocketEventListener event_listener;
		event_listener.open(*_socket, SocketEventType::READ);
		for (size_t i = 0; i < _configuration.max_retries(); )
		{
			result = _socket->reconnect(true);
			if (result.code() == NetworkResultCode::SUCCESS)
			{
				break;
			}
			else if (result.code() == NetworkResultCode::SOCKET_CONNECTION_IN_PROGRESS
				|| result.code() == NetworkResultCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED)
			{
				result = event_listener.wait(_configuration.read_timeout());
				if (result.code() == NetworkResultCode::SOCKET_TIMEOUT)
				{
					return NetworkResult(NetworkResultCode::SUCCESS);
				}
				else if (result.code() != NetworkResultCode::SUCCESS)
				{
					return result;
				}
				// Data arrived (POLLIN fired) — retry probe to process the alert.
				continue;
			}
			else
			{
				return result;
			}
		}
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	return result;
}
NetworkResult ClientImpl::read(void* buffer, size_t size)
{
	NetworkResult result;
	
	SocketEventListener event_listener;
	event_listener.open(*_socket, SocketEventType::READ);

	for (size_t i = 0; i < _configuration.max_retries(); i++)
	{
		result = event_listener.wait(_configuration.read_timeout());
		if (result.code() == NetworkResultCode::SOCKET_TIMEOUT)
		{
		}
		else if (result.code() != NetworkResultCode::SUCCESS)
		{
			break;
		}
		else {
			result = _socket->read((char*)buffer, size);
			if (result.bytes() == 0)
			{
				result = NetworkResult(NetworkResultCode::SOCKET_CLOSED);
				break;
			}
			if (result.code() == NetworkResultCode::SOCKET_TIMEOUT)
			{
			}
			else if (result.code() == NetworkResultCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED)
			{
			}
			else {
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(_configuration.time_between_retries()));
	}

	return result;
}
NetworkResult ClientImpl::write(const void* buffer, size_t size)
{
	size_t written_size{ 0 };
	NetworkResult result;

	SocketEventListener event_listener;
	event_listener.open(*_socket, SocketEventType::WRITE);

	for (size_t i = 0; i < _configuration.max_retries(); )
	{
		result = event_listener.wait(_configuration.write_timeout());
		if (result.code() == NetworkResultCode::SOCKET_TIMEOUT)
		{
			i++;
		}
		else if (result.code() != NetworkResultCode::SUCCESS)
		{
			break;
		}
		else {
			result = _socket->write((char*)buffer + written_size, size - written_size);
			if (result.code() == NetworkResultCode::SOCKET_TIMEOUT)
			{
				i++;
			}
			else if (result.code() == NetworkResultCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED)
			{
				// Intentional: do not increment i here.
				// SOCKET_CONNECTION_NEED_TO_BE_BLOCKED means data is likely available soon,
				// so we should keep retrying beyond max_retries rather than giving up
				// on data that could still be received.
			}
			else if (result.code() != NetworkResultCode::SUCCESS)
				break;

			written_size += (size_t)result.bytes();
			if (written_size == size)
				break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(_configuration.time_between_retries()));
	}

	result = NetworkResult(result.code(), static_cast<int32_t>(written_size));
	return result;
}
NetworkResult ClientImpl::isConnected()
{
	return _socket->isConnected();
}

