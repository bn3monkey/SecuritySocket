#include "SocketRequestServer.hpp"
#include "SocketResult.hpp"
#include <vector>
#include <queue>

Bn3Monkey::SocketRequestServerImpl::~SocketRequestServerImpl()
{
	close();
}

Bn3Monkey::SocketResult Bn3Monkey::SocketRequestServerImpl::open(SocketRequestHandler* handler, size_t num_of_clients)
{
	if (_is_running)
	{
		return SocketResult(SocketCode::SOCKET_SERVER_ALREADY_RUNNING);
	}

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

	_is_running = true;
	_routine = std::thread{ &SocketRequestServerImpl::run, this, handler };
	return result;
}

void Bn3Monkey::SocketRequestServerImpl::close()
{
	if (_is_running)
	{
		_is_running = false;
		_routine.join();

		_socket->close();
	}
}



void Bn3Monkey::SocketRequestServerImpl::run(SocketRequestHandler* handler)
{
	SocketMultiEventListener listener;
	listener.open();

	SocketEventContext server_context;
	server_context.fd = _socket->descriptor();
	listener.addEvent(&server_context, SocketEventType::ACCEPT);

	SocketRequestWorker worker{ handler, listener };
	worker.start();

	while (_is_running)
	{		
		auto eventlist = listener.wait(_configuration.read_timeout());
		if (eventlist.result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (eventlist.result.code() != SocketCode::SUCCESS)
		{
			break;
		}
		

		for (auto& context : eventlist.contexts)
		{
			auto& type = context->type;

			switch (type)
			{
			case SocketEventType::ACCEPT:
				{
					auto socket_container = _socket->accept();
					auto* client_socket = socket_container.get();
					if (client_socket->result().code() == SocketCode::SUCCESS)
					{
						SocketConnection* connection = _socket_connection_pool.acquire(socket_container);
						connection->initialize(handler->headerSize(), _configuration.pdu_size());
						auto* sock = connection->socket();
						handler->onClientConnected(sock->ip(), sock->port());
						listener.addEvent(connection, SocketEventType::READ);
					}
				}
				break;
			case SocketEventType::DISCONNECTED:
				{
					auto* connection = static_cast<SocketConnection*>(context);
					auto* sock = connection->socket();
					handler->onClientDisconnected(sock->ip(), sock->port());
					sock->close();
					
					listener.removeEvent(connection);
					_socket_connection_pool.release(connection);
				}
				break;

			case SocketEventType::READ:
				{
					auto* connection = static_cast<SocketConnection*>(context);
					auto* sock = connection->socket();

					auto header_size = handler->headerSize();
					auto* header = reinterpret_cast<SocketRequestHeader*>(connection->input_header_buffer.data());

					for (size_t total_read_size = 0; total_read_size < header_size; )
					{
						auto result = sock->read(reinterpret_cast<char*>(header) + total_read_size, header_size - total_read_size);
						if (result.bytes() < 0) {
							// Discard Header
							continue;
						}
						total_read_size += result.bytes();
					}

					auto mode = handler->onModeClassified(header);
					if (mode == SocketRequestMode::FAST) {
						auto payload_size = header->payload_size();
						auto payload = connection->input_payload_buffer.data();
						for (size_t total_read_size = 0; total_read_size < payload_size; ) {
							auto result = sock->read(payload + total_read_size, payload_size - total_read_size);
							if (result.bytes() < 0) {
								continue;
							}
							total_read_size += result.bytes();
						}

						auto* response = handler->onProcessed(header, payload, payload_size, connection->output_buffer.data());
						if (response->isValid() != nullptr) {
							auto output_size = response->length();
							auto* output = response->buffer();
							for (size_t total_write_size = 0; total_write_size < output_size; ) {
								auto result = sock->write(output + total_write_size, output_size);
								if (result.bytes() < 0) {
									continue;
								}
								total_write_size += result.bytes();
							}
						}
						connection->flush();
					}
					else if (mode == SocketRequestMode::LATENT) {
					
					}
					else if (mode == SocketRequestMode::STREAM_START) {
					}
					else if (mode == SocketRequestMode::STREAM_STOP) {
					
					}
				}
				break;
			case SocketEventType::WRITE:
				{
					auto* connection = static_cast<SocketConnection*>(context);
					SocketTaskType state = worker.await(connection);

					if (state == SocketTaskType::SUCCESS)
					{
						auto sock = connection->socket();
						auto result = sock->write(connection->output_buffer.data() + connection->written_size, connection->total_output_size);
						if (result.bytes() > 0)
						{
							connection->written_size += result.bytes();
							if (connection->total_output_size == connection->written_size)
							{
								connection->flush();

								listener.modifyEvent(connection, SocketEventType::READ);
							}
						}
					}
					else if (state == SocketTaskType::FAIL)
					{
						auto* sock = connection->socket();
						handler->onClientConnected(sock->ip(), sock->port());
						connection->flush();

						listener.modifyEvent(connection, SocketEventType::READ);
					}

				}
				break;
			default:
				break;
		}

	}

	worker.stop();
	listener.close();
}
