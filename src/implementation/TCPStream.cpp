#include "TCPStream.hpp"
using namespace Bn3Monkey;

TCPStream::TCPStream(TCPEventHandler& handler, size_t max_retries, size_t pdu_size) : 
	_handler(handler), _max_retries(max_retries), _pdu_size(pdu_size)
	{

	}

void TCPStream::start(TCPSocket* socket)
{
	if (!_is_running)
	{
		_is_running = true;
		_reader = std::thread(&TCPStream::read, this, socket);
		_writer = std::thread(&TCPStream::write, this, socket);
	}
}
void TCPStream::stop()
{
	if (_is_running) {
		_is_running = false;
		_reader.join();
		_writer.join();
	}
}

ConnectionResult TCPStream::getLastError()
{
	{
		std::lock_guard<std::mutex> lock(_last_error_lock);
		return _last_error;
	}
}
void TCPStream::setLastError(ConnectionResult& result)
{
	{
		std::lock_guard<std::mutex> lock(_last_error_lock);
		_last_error = result;
	}
}

void TCPStream::read(TCPSocket* socket)
{
	ConnectionResult result;
	std::vector<char> buffer(_pdu_size + 1);

	while (_is_running)
	{
		size_t read_size{ 0 };
		memset(buffer.data(), 0, _pdu_size);

		for (size_t trial = 0; trial < _max_retries; )
		{
			result = socket->poll(PollType::READ);
			if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
				trial++;
				continue;
			}
			else if (result.code != ConnectionCode::SUCCESS) {
				break;
			}

			read_size = socket->read(buffer.data(), _pdu_size);
			result = socket->result((int)read_size);
			if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
				trial++;
				continue;
			}
			else {
				break;
			}
		}

		if (result.code == ConnectionCode::SOCKET_TIMEOUT)
		{
			result = socket->isConnected();
			if (result.code != ConnectionCode::SUCCESS)
			{
				setLastError(result);
				_handler.onError(result);
				break;
			}
		}
		else if (result.code != ConnectionCode::SUCCESS)
		{
			setLastError(result);
			_handler.onError(result);
			break;
		}
		else {
			_handler.onRead(buffer.data(), read_size);
		}
	}
}
void TCPStream::write(TCPSocket* socket)
{
    ConnectionResult result;
	std::vector<char> buffer(_pdu_size + 1);

	while (_is_running)
	{
		size_t written_size{ 0 };
		memset(buffer.data(), 0, _pdu_size);

		_handler.onWrite(buffer.data(), _pdu_size);

		for (size_t trial = 0; trial < _max_retries; )
		{
			result = socket->poll(PollType::WRITE);
			if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
				trial++;
				continue;
			}
			else if (result.code != ConnectionCode::SUCCESS) {
				break;
			}

			auto ret = socket->write(buffer.data() + written_size, _pdu_size - written_size);
			result = socket->result(ret);
			if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
				trial++;
				continue;
			}
			else if (result.code != ConnectionCode::SUCCESS) {
				break;
			}

			written_size += (size_t)ret;
			if (written_size == _pdu_size)
				break;
		}

		if (result.code == ConnectionCode::SOCKET_TIMEOUT)
		{
			result = socket->isConnected();
			if (result.code != ConnectionCode::SUCCESS)
			{
				setLastError(result);
				_handler.onError(result);
				break;
			}
		}
		else if (result.code != ConnectionCode::SUCCESS)
		{
			setLastError(result);
			_handler.onError(result);
			break;
		}
	}
}
