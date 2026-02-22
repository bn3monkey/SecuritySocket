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
	(void)num_of_clients;

	if (_is_running)
	{
		return SocketResult(SocketCode::SOCKET_SERVER_ALREADY_RUNNING);
	}

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
					SocketConnection* connection = _socket_connection_pool.acquire(socket_container, *handler, _configuration.pdu_size());
					connection->connectClient();
					listener.addEvent(connection, Bn3Monkey::SocketEventType::READ);
				}
			}
			break;
			case SocketEventType::DISCONNECTED:
			{
				auto* connection = static_cast<SocketConnection*>(context);
				connection->disconnectClient();
				listener.removeEvent(connection);
				_socket_connection_pool.release(connection);
			}
			break;

			case SocketEventType::READ:
			{
				auto* connection = static_cast<SocketConnection*>(context);
				switch (connection->state)
				{
				case SocketConnection::ProcessState::READING_HEADER:
					{
						connection->state = connection->readHeader();
						if (connection->state == SocketConnection::ProcessState::WRITING_RESPONSE) {
							listener.modifyEvent(connection, Bn3Monkey::SocketEventType::WRITE);
						}
						else if (connection->state == SocketConnection::ProcessState::FINISH_PROCESS) {
							connection->flush();
							connection->state = SocketConnection::ProcessState::READING_HEADER;
						}
					}
					break;
				case SocketConnection::ProcessState::READING_PAYLOAD:
					{
						connection->state = connection->readPayload();
						if (connection->state == SocketConnection::ProcessState::WRITING_RESPONSE) {
							listener.modifyEvent(connection, Bn3Monkey::SocketEventType::WRITE);
						}
						else if (connection->state == SocketConnection::ProcessState::FINISH_PROCESS) {
							connection->flush();
							connection->state = SocketConnection::ProcessState::READING_HEADER;
						}
					}
					break;

				default:
					break;
				}
			}
			break;

			case SocketEventType::WRITE :
			{
				auto connection = static_cast<SocketConnection*>(context);
				switch (connection->state) {
					case SocketConnection::ProcessState::WRITING_RESPONSE:
					{
						connection->state = connection->writeResponse();
						if (connection->state == SocketConnection::ProcessState::FINISH_PROCESS) {
							connection->flush();
							listener.modifyEvent(connection, Bn3Monkey::SocketEventType::READ);
							connection->state = SocketConnection::ProcessState::READING_HEADER;
						}
					}
						break;
					default:
						break;
				}
			}

			default:
				break;
			}
		}

	}

	listener.close();
}
