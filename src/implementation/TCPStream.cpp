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
		_handler->onDisconnected();

		_reader.join();
		_writer.join();
		_handler.reset();
	}
}

ConnectionResult TCPStream::connect()
{
 	ConnectionResult result;
	for (size_t trial = 0; trial < _max_retries; trial++)
	{
		result = _socket->connect();
		if (result.code == ConnectionCode::SUCCESS) {
			break;
		}
		else if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
			continue;
		}
		else {
			setLastError(result);
			break;
		}
	}
	return result;
}
void TCPStream::disconnect()
{
	_socket->disconnect();
}

ConnectionResult TCPStream::read(char* buffer, size_t* read_length)
{
	ConnectionResult result;
	int length {0};
	for (size_t trial = 0; trial < _max_retries; )
	{
		result = _socket->poll(PollType::READ);
		if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
			trial++;
			continue;
		}
		else if (result.code != ConnectionCode::SUCCESS) {
			break;
		}

		length = _socket->read(buffer, _pdu_size);
		result = _socket->result(length);
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
		result = _socket->isConnected();
		if (result.code != ConnectionCode::SUCCESS)
		{
			setLastError(result);
		}
	}
	else if (result.code != ConnectionCode::SUCCESS)
	{
		setLastError(result);
	}
	else {
		*read_length = length;
	}
	return result;
}
ConnectionResult TCPStream::write(char* buffer, size_t length)
{
	ConnectionResult result;
	size_t written_size{ 0 };
	for (size_t trial = 0; trial < _max_retries; )
	{
		result = _socket->poll(PollType::WRITE);
		if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
			trial++;
			continue;
		}
		else if (result.code != ConnectionCode::SUCCESS) {
			break;
		}

		auto ret = _socket->write(buffer + written_size, length - written_size);
		result = _socket->result(ret);
		if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
			trial++;
			continue;
		}
		else if (result.code != ConnectionCode::SUCCESS) {
			break;
		}

		written_size += (size_t)ret;
		if (written_size == length)
			break;
	}

	if (result.code == ConnectionCode::SOCKET_TIMEOUT)
	{
		result = _socket->isConnected();
		if (result.code != ConnectionCode::SUCCESS)
		{
			setLastError(result);
		}
	}
	else if (result.code != ConnectionCode::SUCCESS)
	{
		setLastError(result);
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

		result = write(buffer.data(), total_size);
		if (result.code != ConnectionCode::SUCCESS)
		{
			_handler->onError(result);
		}

	}
}
