#include "ClientConnection.hpp"
#include "SocketEvent.hpp"

using namespace Bn3Monkey;


void Bn3Monkey::ClientConnectionImpl::connectClient()
{
	_handler.onConnected(*this);
}

void Bn3Monkey::ClientConnectionImpl::disconnectClient()
{
	_handler.onDisconnected(*this);
	_socket->close();
	// listener.removeEvent(this);
}

Bn3Monkey::ClientConnectionImpl::ProcessState Bn3Monkey::ClientConnectionImpl::readHeader()
{
	auto result = _socket->read(reinterpret_cast<char*>(input_header_buffer.data()) + total_input_header_read_size, input_header_buffer.size() - total_input_header_read_size);
	if (result.bytes() < 0) {
		return ProcessState::READING_HEADER;
	}
	total_input_header_read_size += result.bytes();

	if (total_input_header_read_size == input_header_buffer.size()) {
		auto* header = input_header_buffer.data();
		_payload_size = _handler.getPayloadSize(header);
		_mode = _handler.onModeClassified(header);

		if (_payload_size == 0) {
			return runTask(_mode, _payload_size);
		}

		return ProcessState::READING_PAYLOAD;
	}
	return ProcessState::READING_HEADER;
}

Bn3Monkey::ClientConnectionImpl::ProcessState Bn3Monkey::ClientConnectionImpl::readPayload()
{
	auto* payload = input_payload_buffer.data();
	auto result = _socket->read(reinterpret_cast<char*>(payload) + total_input_payload_read_size, _payload_size - total_input_payload_read_size);
	if (result.bytes() < 0) {
		return ProcessState::READING_PAYLOAD;
	}
	total_input_payload_read_size += result.bytes();
	if (total_input_payload_read_size == _payload_size)
	{
		return runTask(_mode, _payload_size);
	}
	return ProcessState::READING_PAYLOAD;
}

Bn3Monkey::ClientConnectionImpl::ProcessState Bn3Monkey::ClientConnectionImpl::writeResponse()
{
	auto result = _socket->write(reinterpret_cast<char*>(output_buffer.data()) + total_output_write_size, response_size - total_output_write_size);
	if (result.bytes() < 0) {
		return ProcessState::WRITING_RESPONSE;
	}

	total_output_write_size += result.bytes();

	if (total_output_write_size == response_size) {
		return ProcessState::FINISH_PROCESS;
	}
	return ProcessState::WRITING_RESPONSE;
}

void Bn3Monkey::ClientConnectionImpl::flush()
{
	memset(input_header_buffer.data(), 0, input_header_buffer.size());
	memset(input_payload_buffer.data(), 0, input_payload_buffer.size());
	memset(output_buffer.data(), 0, output_buffer.size());

	state = ProcessState::READING_HEADER;

	total_input_header_read_size = 0;

	_payload_size = 0;
	total_input_payload_read_size = 0;

	response_size = 0;
	total_output_write_size = 0;
}

Bn3Monkey::ClientConnectionImpl::ProcessState Bn3Monkey::ClientConnectionImpl::runTask(RequestProcessingMode mode, size_t payload_size)
{

	auto* header = input_header_buffer.data();
	auto* payload = input_payload_buffer.data();


	switch (mode) {
	case RequestProcessingMode::FAST:
	{
		_handler.onProcessed(header, payload, payload_size, output_buffer.data(), &response_size);
		return ProcessState::WRITING_RESPONSE;
	}
	break;
	case RequestProcessingMode::SLOW:
	{
		// @Todo
	}
	break;
	case RequestProcessingMode::READ_STREAM:
	{
		_handler.onProcessed(header, payload, payload_size, output_buffer.data(), &response_size);
		return ProcessState::WRITING_RESPONSE;
	}
	break;
	case RequestProcessingMode::WRITE_STREAM:
	{
		_handler.onProcessedWithoutResponse(header, payload, payload_size);
		return ProcessState::FINISH_PROCESS;
	}
	break;
	}
	return ProcessState::WRITING_RESPONSE;
}


void Bn3Monkey::ClientConnectionImpl::startWorker()
{
	_is_running = true;
	_worker = std::thread{ &ClientConnectionImpl::routine, this };
}

void Bn3Monkey::ClientConnectionImpl::stopWorker()
{
	_is_running = false;
	_cv.notify_all();
	_worker.join();
}

void Bn3Monkey::ClientConnectionImpl::routine()
{
	do {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock(_mtx);
			_cv.wait(lock, [&]() {
				return !(_is_running && _tasks.empty());
				});
			if (!_is_running && _tasks.empty())
				break;
			task = std::move(_tasks.front());
			_tasks.pop();
		}

		if (_is_running)
		{
			task();
		}

	} while (_is_running);
}
void Bn3Monkey::ClientConnectionImpl::addTask(std::function<void()> task)
{
	{
		std::unique_lock<std::mutex> lock(_mtx);
		_tasks.push(task);
	}
	_cv.notify_all();
}

