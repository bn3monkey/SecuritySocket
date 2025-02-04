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
		return SocketResult(SocketCode::SOCKET_SERVER_ALREADY_RUNNING, "Socket server is already running");
	}

	SocketResult result = SocketResult(SocketCode::SUCCESS);
#ifdef _WIN32
	WSADATA data;
	int ret = WSAStartup(MAKEWORD(2, 2), &data);
	if (ret != 0) {
		result = SocketResult(SocketCode::WINDOWS_SOCKET_INITIALIZATION_FAIL, "Cannot start windows socket");
		return result;
	}
#endif


	SocketAddress address{ _configuration.ip(), _configuration.port(), false };
	result = address;
	if (result.code() != SocketCode::SUCCESS) {
		return result;
	}

	if (_configuration.tls()) {
		_socket = reinterpret_cast<ActiveSocket*>(new(_container) TLSActiveSocket{ address });
	}
	else {
		_socket = new (_container) ActiveSocket{ address };
	}

	result = _socket->open();
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
#ifdef _WIN32
		WSACleanup();
#endif
	}
}

void Bn3Monkey::SocketRequestServerImpl::run(std::atomic<bool>& is_running, size_t num_of_clients, ActiveSocket& sock, SocketConfiguration& config, SocketRequestHandler& handler)
{
	sock.listen(num_of_clients);
	handler.onConnected();

	while (is_running)
	{
		auto socketlist = sock.poll(config.read_timeout());
		if (socketlist.result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (socketlist.result.code() != SocketCode::SUCCESS)
		{
			handler.onError(socketlist.result);
		}

		auto length = socketlist.event_list.length;
		auto& events = socketlist.event_list.events;
		for (size_t i = 0; i < length; i++)
		{
			auto& type = events[i].type;
			auto& target_socket = events[i].sock;

			switch (type)
			{
			case SocketEventType::ACCEPT:
				{
					sock.accept();
				}
				break;
			case SocketEventType::READ:
				{
					// @Todo : Send Target Socket to the thread pool
					processRequest(target_socket, sock, config, handler);
				}
				break;
			}
		}

		//@Todo : Reap the result from thread pool and send the result to the clients
	}

	handler.onDisconnected();
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