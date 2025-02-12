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
	
	bool is_unix_domain = SocketAddress::checkUnixDomain(_configuration.ip());
	_container = SocketContainer<ClientActiveSocket, TLSClientActiveSocket>(_configuration.tls(), is_unix_domain);
	_socket = _container.get();
	result = _socket->valid();
	if (result.code() != SocketCode::SUCCESS)
	{
		return result;
	}
	return result;	
}
void SocketClientImpl::close()
{
	_socket->disconnect();
	_socket->close();
}



SocketResult SocketClientImpl::connect()
{
	SocketResult result;

	SocketAddress address{_configuration.ip(), _configuration.port(), false};
	if (result.code() != SocketCode::SUCCESS) {
		return result;
	}

	SocketEventListener event_listener;
	event_listener.open(*_socket, SocketEventType::CONNECT);

	for (size_t i = 0; i < _configuration.max_retries(); i++)
	{
		result = _socket->connect(address);
		if (result.code() == SocketCode::SUCCESS)
		{
			break;
		}
		else if (result.code() == SocketCode::SOCKET_CONNECTION_IN_PROGRESS)
		{
            result = event_listener.wait(_configuration.read_timeout());
            if (result.code() == SocketCode::SOCKET_TIMEOUT)
            {
                continue;
            }
            else if (result.code() != SocketCode::SUCCESS)
            {
                return result;
            }
            else {
                break;
            }
		}
		else if (result.code() == SocketCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED)
		{
			result = event_listener.wait(_configuration.read_timeout());
			if (result.code() == SocketCode::SOCKET_TIMEOUT)
			{
				continue;
			}
			else if (result.code() != SocketCode::SUCCESS)
			{
				return result;
			}
			else {
				break;
			}
		}
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(3000));
	return result;
}
SocketResult SocketClientImpl::read(void* buffer, size_t size)
{
	int read_size {0};
	SocketResult result;

	SocketEventListener event_listener;
	event_listener.open(*_socket, SocketEventType::READ);

	for (size_t i = 0; i < _configuration.max_retries(); i++)
	{
		result = event_listener.wait(_configuration.read_timeout());
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (result.code() != SocketCode::SUCCESS)
			break;

		int ret = _socket->read((char *)buffer + read_size, size - read_size);
		if (ret == 0)
		{
			result = SocketResult(SocketCode::SOCKET_CLOSED);
			break;
		}

		result = createResult(ret);
		if (result.code() == SocketCode::SOCKET_TIMEOUT)
		{
			continue;
		}
		else if (result.code() != SocketCode::SUCCESS)
			break;


		read_size += ret;
		if (read_size == size) {
			break;
		}
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

	SocketEventListener event_listener;
	event_listener.open(*_socket, SocketEventType::WRITE);

	for (size_t i = 0; i < _configuration.max_retries(); )
	{
		result = event_listener.wait(_configuration.write_timeout());
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
		else if (result.code() != SocketCode::SUCCESS)
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

