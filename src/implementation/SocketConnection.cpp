#include "SocketConnection.hpp"
#include "SocketEvent.hpp"

using namespace Bn3Monkey;
 
SocketResult SocketConnectionImpl::read(void* buffer, size_t size)
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

    return result;
}
SocketResult SocketConnectionImpl::write(const void* buffer, size_t size)
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
		else
			break;

		written_size += (size_t)ret;
		if (written_size == size)
			break;
	}
	return result;
}
void SocketConnectionImpl::close()
{
    _socket->close();
}