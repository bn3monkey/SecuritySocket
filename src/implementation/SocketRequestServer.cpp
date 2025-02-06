#include "SocketRequestServer.hpp"
#include "SocketResult.hpp"
#include <vector>
#include <future>

Bn3Monkey::SocketRequestServerImpl::~SocketRequestServerImpl()
{
	close();
}

Bn3Monkey::SocketResult Bn3Monkey::SocketRequestServerImpl::open(SocketRequestHandler& handler, size_t num_of_clients)
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

	SocketAddress address{ _configuration.ip(), _configuration.port(), false };
	result = address;
	if (result.code() != SocketCode::SUCCESS) {
		return result;
	}
	
	result = _socket->listen(address, num_of_clients);
	if (result.code() != SocketCode::SUCCESS)
	{
		return result;
	}

	_is_running = true;
	_routine = std::thread{ run, _is_running, *_socket, _configuration, handler };
	return result;
}

void Bn3Monkey::SocketRequestServerImpl::close()
{
	if (!_is_running)
	{
		_is_running = false;
		_routine.join();

		_socket->close();
	}
}

void Bn3Monkey::SocketRequestServerImpl::run(
	SocketRequestServerImpl* self)
{
	
	SocketMultiEventListener listener;
	listener.open();

	SocketEventContext server_context;
	server_context.fd = self->_socket->descriptor();
	listener.addEvent(&server_context, SocketEventType::ACCEPT);

	while (self->_is_running)
	{
		auto eventlist = listener.wait(self->_configuration.read_timeout());
	
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
					auto socket_container = self->_socket->accept();
					SocketConnection* connection = self->_socket_connection_pool.acquire(socket_container);
					listener.addEvent(connection, SocketEventType::READ);
				}
				break;
			case SocketEventType::READ:
				{
					auto* connection = reinterpret_cast<SocketConnection*>(context);
					auto* sock = connection->socket();
					connection->read_size = sock->read(connection->input_buffer.data(), connection->input_buffer.size());
					
					std::async sans{std::launch::async, [](){}};
					// int32_t read_size = context->read(context->input_buffer.data(), context->read_size);
					// async {
					// 		bool isFinished = handler.onProcessed(target_socket->input_buffer(), read_size);
					//		if (isFinished) {
					//      	listener.modifyEvent(*socket, SocketEventType::WRITE); <- PostEvent
					//		}
					//   }

					// launch process asnyc
				}
				break;
			case SocketEventType::WRITE:
				{
					auto* connection = reinterpret_cast<SocketConnection*>(context);
					// wait for process
					// context = event.context;
					// 
					// int32_t written_size = context->written_size;
					// int32_t send_size = context->send(context->output_buffer.data() + send_size, context->written_size - sent_size);
					// if (send_size <= 0) break;
					// context->sent_size += send_size;
					// {
					//    context->sent_size = 0;
					//    context->written_size = 0;
					// 	  listener.modifyEvent(*socket, SocketEventType::READ);
					// }
				}
				break;
			case SocketEventType::DISCONNECTED:
				{
					auto* connection = reinterpret_cast<SocketConnection*>(context);
					listener.removeEvent(connection);
					connection->close();			
				}
				break;
			}
		}
		//@Todo : Reap the result from thread pool and send the result to the clients
	}

	// handler.onDisconnected();
	listener.close();
}

inline void processRequest(int32_t target_socket, Bn3Monkey::ActiveSocket& sock, Bn3Monkey::SocketConfiguration& config, Bn3Monkey::SocketRequestHandler& handler)
{
	using namespace Bn3Monkey;

	thread_local std::vector<char> input{ SocketConfiguration::MAX_PDU_SIZE, std::allocator<char>() };
	thread_local std::vector<char> output{ SocketConfiguration::MAX_PDU_SIZE, std::allocator<char>() };
	
	
	int input_length = sock.read(target_socket, input.data(), config.pdu_size());

	if (input_length == 0)
	{
		sock.drop(target_socket);
	}
	else if (input_length < 0)
	{
		auto result = createResult(input_length);
		handler.onError(result);
	}
	

	size_t output_length;
	handler.onRequestReceived(input.data(), input_length, output.data(), &output_length);

	size_t written_size{ 0 };

	for (size_t trial = 0; trial < config.max_retries(); )
	{
		int ret = sock.write(target_socket, output.data() + written_size, output_length - written_size);
		if (ret == 0)
		{
			sock.drop(target_socket);
		}
		else if (ret < 0)
		{
			auto result = createResult(ret);
			handler.onError(result);
		}
		written_size += ret;
	}
}