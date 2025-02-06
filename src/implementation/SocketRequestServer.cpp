#include "SocketRequestServer.hpp"
#include "SocketResult.hpp"
#include <vector>

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

void Bn3Monkey::SocketRequestServerImpl::run(std::atomic<bool>& is_running, size_t num_of_clients, PassiveSocket& sock, SocketConfiguration& config, SocketRequestHandler& handler)
{
	
	SocketMultiEventListener listener;
	listener.open();
	listener.addEvent(sock, SocketEventType::ACCEPT);

	while (is_running)
	{
		auto eventlist = listener.wait(config.read_timeout());
	
		if (eventlist.result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (eventlist.result.code() != SocketCode::SUCCESS)
		{
			break;
		}

		for (auto& event : eventlist.events)
		{
			auto& target_socket = event.sock;
			auto& type = event.type;

			switch (type)
			{
			case SocketEventType::ACCEPT:
				{
					auto container = sock.accept();
					ServerActiveSocket* socket = container.get();
					listener.addEvent(*socket, SocketEventType::READ_WRITE);
				}
				break;
			case SocketEventType::READ:
				{
					// launch process asnyc
				}
				break;
			case SocketEventType::WRITE:
				{
					// wait for process
				}
				break;
			case SocketEventType::READ_WRITE:
				{
					// do process right here
				}
				break;
			case SocketEventType::DISCONNECTED:
				{

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