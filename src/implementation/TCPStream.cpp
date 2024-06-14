#include "TCPStream.hpp"
using namespace Bn3Monkey;

TCPStream::TCPStream(TCPSocket* socket, size_t max_retries, size_t pdu_size) :
	_socket(socket), _max_retries(max_retries), _pdu_size(pdu_size)
	{

	}

void TCPStream::open(const std::shared_ptr<TCPEventHandler>& handler)
{
	if (!_is_running)
	{
		_is_running = true;
		_handler = handler;
		_handler->onConnected();
		_reader = std::thread(&TCPStream::readRoutine, this);
		_writer = std::thread(&TCPStream::writeRoutine, this);
	}
}
void TCPStream::close()
{
	if (_is_running) {
		_is_running = false;
		_reader.join();
		_writer.join();
		_handler->onDisconnected();
		_handler.reset();
	}
}


ConnectionResult TCPStream::read(char* buffer, size_t* read_length)
{
	ConnectionResult result;
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

		read_size = socket->read(buffer, _pdu_size);
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
			return result;
		}
	}
	else if (result.code != ConnectionCode::SUCCESS)
	{
		setLastError(result);
		return result;
	}
	else {
		if (read_size > 0)
			*read_length = read_size;
	}
	return result;
}
ConnectionResult TCPStream::write(char* buffer, size_t length)
{
	ConnectionResult result;
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

		auto ret = socket->write(buffer.data() + written_size, total_size - written_size);
		result = socket->result(ret);
		if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
			trial++;
			continue;
		}
		else if (result.code != ConnectionCode::SUCCESS) {
			break;
		}

		written_size += (size_t)ret;
		if (written_size == total_size)
			break;
	}

	if (result.code == ConnectionCode::SOCKET_TIMEOUT)
	{
		result = socket->isConnected();
		if (result.code != ConnectionCode::SUCCESS)
		{
			setLastError(result);
			break;
		}
	}
	else if (result.code != ConnectionCode::SUCCESS)
	{
		setLastError(result);
		break;
	}
	return result;
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

void TCPStream::readRoutine()
{
	ConnectionResult result;
	std::vector<char> buffer(_pdu_size + 1);

	while (_is_running)
	{
		size_t read_size{ 0 };
		memset(buffer.data(), 0, _pdu_size);
		result = read(buffer.data(), &read_size);
		if (result.code == ConnectionCode::SUCCESS)
		{
			_handler->onRead(buffer.data(), read_size);
		}
		else
		{
			_handler->onError(result);
		}
	}
}
void TCPStream::writeRoutine()
{
    ConnectionResult result;
	std::vector<char> buffer(_pdu_size + 1);

	while (_is_running)
	{
		size_t written_size{ 0 };
		memset(buffer.data(), 0, _pdu_size);

		size_t total_size = _handler->onWrite(buffer.data(), _pdu_size);
		if (total_size == 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		result = write(buffer.data(), &written_size);
		if (result.code == ConnectionCode::SUCCESS)
		{
			_handler->onWrite(buffer.data(), read_size);
		}
		else
		{
			_handler->onError(result);
		}

	}
}
