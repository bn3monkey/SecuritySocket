#include "SocketClient.hpp"
#include "SocketResult.hpp"

using namespace Bn3Monkey;

SocketClientImpl::~SocketClientImpl()
{
	close();
}

SocketResult SocketClientImpl::open()
{
	SocketResult result;
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
		_socket = reinterpret_cast<PassiveSocket*>(new(_container) TLSPassiveSocket { address });
	}
	else {
		_socket = new (_container) PassiveSocket { address };
	}

	result = _socket->open();
	if (result.code() != SocketCode::SUCCESS)
	{
		return result;
	}


}
void SocketClientImpl::close()
{
	_socket->disconnect();
	_socket->close();

#ifdef _WIN32
	WSACleanup();
#endif
}

SocketResult SocketClientImpl::connect()
{
	SocketResult result;

	for (size_t i = 0; i < _configuration.max_retries(); i++)
	{
		result = _socket->poll(PassivePollType::CONNECT, _configuration.write_timeout());
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (result.code() != SocketCode::SUCCESS)
		{
			return result;
		}

		result = _socket->connect();
		break;
	}
	return result;
}
SocketResult SocketClientImpl::read(void* buffer, size_t size)
{
	int read_size {0};
	SocketResult result;

	for (size_t i = 0; i < _configuration.max_retries(); i++)
	{
		result = _socket->poll(PassivePollType::READ, _configuration.read_timeout());
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (result.code() != SocketCode::SUCCESS)
			break;

		int ret = _socket->read((char *)buffer + read_size, size - read_size);
		if (ret == 0)
		{
			result = SocketResult(SocketCode::SOCKET_CLOSED, "Socket closed");
			break;
		}

		result = createResult(ret);
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (result.code() == SocketCode::SUCCESS)
		{
			read_size += ret;
			if (read_size == size) {
				break;
			}
		}
		else
			break;
	}

	// Check if connection is available when timeout occurs max_trial_time
	if (result.code() == SocketCode::SOCKET_TIMEOUT)
	{
		result = _socket->isConnected();
	}
	return result;
}
SocketResult SocketClientImpl::write(const void* buffer, size_t size)
{
	int ret{ 0 };
	size_t written_size{ 0 };
	SocketResult result;

	for (size_t i = 0; i < _configuration.max_retries(); )
	{
		result = _socket->poll(PassivePollType::WRITE, _configuration.write_timeout());
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			i++;
			continue;
		}
		else if (result.code() != SocketCode::SUCCESS)
			break;

		ret = _socket->write((char *)buffer + written_size, size - written_size);
		result = createResult(ret);
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			i++;
			continue;
		}
		else
			break;

		written_size += (size_t)ret;
		if (written_size == size)
			break;
	}

	if (result.code() == SocketCode::SOCKET_TIMEOUT)
	{
		result = _socket->isConnected();
	}
	return result;
}

