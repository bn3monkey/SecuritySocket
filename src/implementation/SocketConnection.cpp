#include "SocketConnection.hpp"
#include "SocketEvent.hpp"

using namespace Bn3Monkey;


void Bn3Monkey::SocketConnection::connectClient()
{
	_handler.onClientConnected(_socket->ip(), _socket->port());
}

void Bn3Monkey::SocketConnection::disconnectClient()
{
	_handler.onClientDisconnected(_socket->ip(), _socket->port());
	_socket->close();
	// listener.removeEvent(this);
}

Bn3Monkey::SocketConnection::ProcessState Bn3Monkey::SocketConnection::readHeader()
{
	auto result = _socket->read(reinterpret_cast<char*>(input_header_buffer.data()) + total_input_header_read_size, input_header_buffer.size() - total_input_header_read_size);
	if (result.bytes() < 0) {
		return ProcessState::READING_HEADER;
	}
	total_input_header_read_size += result.bytes();

	if (total_input_header_read_size == input_header_buffer.size()) {
		auto* header = input_header_buffer.data();
		payload_size = _handler.getPayloadSize(header);
		mode = _handler.onModeClassified(header);
		return ProcessState::READING_PAYLOAD;
	}
	return ProcessState::READING_HEADER;	
}

Bn3Monkey::SocketConnection::ProcessState Bn3Monkey::SocketConnection::readPayload()
{
	auto* payload = input_payload_buffer.data();
	auto result = _socket->read(reinterpret_cast<char*>(payload) + total_input_payload_read_size, payload_size - total_input_payload_read_size);
	if (result.bytes() < 0) {
		return ProcessState::READING_PAYLOAD;
	}
	total_input_payload_read_size += result.bytes();
	if (total_input_payload_read_size == payload_size)
	{
		switch (mode) {
		case SocketRequestMode::FAST:
			{
				auto* header = input_header_buffer.data();
				_handler.onProcessed(header, payload, payload_size, output_buffer.data(), &response_size);
				return ProcessState::WRITING_RESPONSE;
			}
			break;
		case SocketRequestMode::SLOW:
			{
				// @Todo
			}
			break;
		case SocketRequestMode::READ_STREAM:
			{
				auto* header = input_header_buffer.data();
				_handler.onProcessed(header, payload, payload_size, output_buffer.data(), &response_size);
				return ProcessState::WRITING_RESPONSE;
			}
			break;
		case SocketRequestMode::WRITE_STREAM:
			{
				auto* header = input_header_buffer.data();
				_handler.onProcessedWithoutResponse(header, payload, payload_size);
				return ProcessState::READING_HEADER;
			}
			break;
		}
	}

	return ProcessState::READING_PAYLOAD;
}

Bn3Monkey::SocketConnection::ProcessState Bn3Monkey::SocketConnection::writeResponse()
{
	auto result = _socket->write(reinterpret_cast<char*>(output_buffer.data()) + total_output_write_size, response_size - total_output_write_size);
	if (result.bytes() < 0) {
		return ProcessState::WRITING_RESPONSE;
	}

	total_output_write_size += result.bytes();

	if (total_output_write_size == response_size) {
		return ProcessState::READING_HEADER;
	}
	return ProcessState::WRITING_RESPONSE;
}

void Bn3Monkey::SocketConnection::flush()
{
	memset(input_header_buffer.data(), 0, input_header_buffer.size());
	memset(input_payload_buffer.data(), 0, input_payload_buffer.size());
	memset(output_buffer.data(), 0, output_buffer.size());

	state = ProcessState::READING_HEADER;

	total_input_header_read_size = 0;
	
	payload_size = 0;
	total_input_payload_read_size = 0;
	
	response_size;
	total_output_write_size = 0;
}


void Bn3Monkey::SocketConnection::startWorker()
{
	_is_running = true;
	_worker = std::thread{ &SocketConnection::routine, this };
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

 