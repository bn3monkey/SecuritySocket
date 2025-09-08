#include "SocketConnection.hpp"
#include "SocketEvent.hpp"

using namespace Bn3Monkey;


void Bn3Monkey::SocketConnection::connectClient(SocketMultiEventListener& listener)
{
	_handler.onClientConnected(_socket->ip(), _socket->port());
	listener.addEvent(this, SocketEventType::READ);
}

void Bn3Monkey::SocketConnection::disconnectClient(SocketMultiEventListener& listener)
{
	_handler.onClientDisconnected(sock->ip(), sock->port());
	_socket->close();
	listener.removeEvent(this);
}

SocketRequestHeader* Bn3Monkey::SocketConnection::readHeader()
{
	auto header_size = _handler.headerSize();
	auto* header = reinterpret_cast<SocketRequestHeader*>(input_header_buffer.data());

	for (size_t total_read_size = 0; total_read_size < header_size; )
	{
		auto result = _socket->read(reinterpret_cast<char*>(header) + total_read_size, header_size - total_read_size);
		if (result.bytes() < 0) {
			return nullptr;
		}
		total_read_size += result.bytes();
	}
	return header;
}

void Bn3Monkey::SocketConnection::runTask(SocketRequestHeader* header)
{
	auto payload_size = header->payload_size();
	auto* payload = input_payload_buffer.data();
	for (size_t total_read_size = 0; total_read_size < payload_size; ) {
		auto result = _socket->read(payload + total_read_size, payload_size - total_read_size);
		if (result.bytes() < 0) {
			return;
		}
		total_read_size += result.bytes();
	}

	auto* response = _handler.onProcessed(header, payload, payload_size, output_buffer.data());
	if (response->isValid()) {
		auto output_size = response->length();
		auto* output = response->buffer();
		for (size_t total_write_size = 0; total_write_size < output_size; ) {
			auto result = _socket->write(output + total_write_size, output_size);
			if (result.bytes() < 0) {
				return;
			}
			total_write_size += result.bytes();
		}
	}
	
	flush();
}

void Bn3Monkey::SocketConnection::runNoResponseTask(SocketRequestHeader* header)
{
	auto payload_size = header->payload_size();
	auto* payload = input_payload_buffer.data();
	for (size_t total_read_size = 0; total_read_size < payload_size; ) {
		auto result = _socket->read(payload + total_read_size, payload_size - total_read_size);
		if (result.bytes() < 0) {
			return;
		}
		total_read_size += result.bytes();
	}

	_handler.onProcessedWithoutResponse(header, payload, payload_size);
	flush();
	return response->streamSession();
}

void Bn3Monkey::SocketConnection::runSlowTask(SocketMultiEventListener& listener, SocketRequestHeader* header)
{
	listener.removeEvent(this);

	addTask([&]() {
		runTask(header);
		listener.addEvent(this, SocketEventType::READ);
		});
}

void Bn3Monkey::SocketConnection::startWorker()
{
	_is_running = true;
	_worker = std::thread{ &SocketClient::routine, this };
}

void Bn3Monkey::SocketConnection::stopWorker()
{
	_is_running = false;
	_cv.notify_all();
	_worker.join();
}

void Bn3Monkey::SocketConnection::routine()
{
	do {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock(_mtx);
			_cv.wait(lock, [&]() {
				return !(_is_running && _tasks.empty());
				});
			task = std::move(_tasks.front());
			_tasks.pop();
		}
	
		if (_is_running)
		{
			task();
		}

	} while (_is_running);
}
void Bn3Monkey::SocketConnection::addTask(std::function<void()> task)
{
	{
		std::unique_lock<std::mutex> lock(_mtx);
		_tasks.push(task);
	}
	_cv.notify_all();
}

 