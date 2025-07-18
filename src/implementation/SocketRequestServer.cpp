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

class SocketRequestWorker
{
public:
	SocketRequestWorker(SocketRequestHandler* handler, SocketMultiEventListener& event_listener)
		: _handler(handler), _event_listener(event_listener)
	{
	}
	void start()
	{
		{
			std::unique_lock<std::mutex> lock(_mtx);
			_is_running = true;
		}
		_thread = std::thread(&SocketRequestWorker::routine, this);
	}
	void stop()
	{
		{
			std::unique_lock<std::mutex> lock(_mtx);
			_is_running = false;
			_cv.notify_all();
		}
		_thread.join();
	}
	void run(SocketConnection* connection)
	{
		_handler->onProcessedWithoutResponse(
			connection->input_buffer.data(),
			connection->total_input_size);
	}
	void async(SocketConnection* connection)
	{
		{
			std::unique_lock<std::mutex> lock(_mtx);
			_queue.push(connection);
			_cv.notify_all();
		}
	}
	SocketTaskType await(SocketConnection* connection)
	{
		return connection->taskResult();
	}

private:
	void routine()
	{
		while (true)
		{
			SocketConnection* connection{ nullptr };
			
			{
				std::unique_lock<std::mutex> lock(_mtx);
				_cv.wait(lock, [&]() {
					return !_is_running || !_queue.empty();
					});

				if (!_is_running) return;

				connection = _queue.front();
				_queue.pop();
			}
			
			bool is_finished = _handler->onProcessed(
				connection->input_buffer.data(),
				connection->total_input_size,
				connection->output_buffer.data(),
				connection->total_output_size);

			connection->finishTask(is_finished);
			_event_listener.addEvent(connection, SocketEventType::WRITE);
		}
	}

	std::thread _thread;
	SocketRequestHandler* _handler;
	SocketMultiEventListener& _event_listener;

	std::atomic_bool _is_running;
	std::queue<SocketConnection*> _queue;
	std::mutex _mtx;
	std::condition_variable _cv;
};


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
						connection->initialize(_configuration.pdu_size());
						auto* sock = connection->socket();
						handler->onClientConnected(sock->ip(), sock->port());
						listener.addEvent(connection, SocketEventType::READ);
					}
				}
				break;
			case SocketEventType::READ:
				{
					auto* connection = static_cast<SocketConnection*>(context);
					auto* sock = connection->socket();
					auto result = sock->read(connection->input_buffer.data() + connection->total_input_size, connection->input_buffer.size());
					if (result.bytes() > 0)
					{
						auto state = handler->onDataReceived(
							connection->input_buffer.data(), 
							connection->total_input_size,
							result.bytes()
							);
						connection->total_input_size += result.bytes();

						if (state == SocketRequestHandler::ProcessState::READY)
						{
							listener.removeEvent(connection);

							worker.async(connection);
						}
						else if (state == SocketRequestHandler::ProcessState::READY_BUT_NO_RESPONSE)
						{
							worker.run(connection);
							connection->flush();
						}
						else if (state == SocketRequestHandler::ProcessState::INCOMPLETE)
						{
							// Do nothing, wait for more data
						}
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

                default:
                    break;
			}
		}

	}

	worker.stop();
	listener.close();
}
