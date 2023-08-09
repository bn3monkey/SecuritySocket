#include "TCPClient.hpp"



Bn3Monkey::TCPClientImpl::TCPClientImpl(const TCPConfiguration& configuration, TCPEventHandler& handler)
	: TCPStream(handler, configuration.max_retries, configuration.pdu_size), _handler(handler)
{

	ConnectionResult result;

#ifdef _WIN32
	WSADATA data;
	int ret = WSAStartup(MAKEWORD(2, 2), &data);
	if (ret != 0) {
		result = ConnectionResult(ConnectionCode::WINDOWS_SOCKET_INITIALIZATION_FAIL, "Cannot start windows socket");
		setLastError(result);
		handler.onError(result);
		return;
	}
#endif

	TCPAddress address {configuration.ip, configuration.port, false};
	result = address;
	if (result.code != ConnectionCode::SUCCESS) {
		setLastError(result);
		handler.onError(result);
		return;
	}

	if (configuration.tls) {
		_socket = reinterpret_cast<TCPSocket *>(new(_container) TLSSocket{address,
																	 configuration.read_timeout, configuration.write_timeout});
	}
	else {
		_socket = new (_container) TCPSocket{ address, configuration.read_timeout, configuration.write_timeout };
	}

	result = *_socket;
	if (result.code != ConnectionCode::SUCCESS) {
		delete _socket;
		_socket = nullptr;
		setLastError(result);
		_handler.onError(result);
		return;
	}

	size_t max_retries = configuration.max_retries;

	// Connect
	size_t trial = 0;
	for (size_t trial =0;trial<max_retries;trial++)
	{
		result = _socket->connect();
		if (result.code == ConnectionCode::SUCCESS) {
			_handler.onConnected();
			break;
		}
		else if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
			continue;
		}
		else { // Error
			_socket->~TCPSocket();
			_socket = nullptr;
			setLastError(result);
			_handler.onError(result);
			return;
		}
	}
	if (trial == max_retries)
	{
		delete _socket;
		_socket = nullptr;
		result = ConnectionResult(ConnectionCode::SOCKET_CLOSED, "Socket is closed");
		setLastError(result);
		return;
	}

	start(_socket);
}

Bn3Monkey::TCPClientImpl::~TCPClientImpl()
{
	close();
#ifdef _WIN32
	WSACleanup();
#endif
}

void Bn3Monkey::TCPClientImpl::close()
{
	
	if (_socket)
	{
		_handler.onDisconnected();

		stop();
		_socket->disconnect();
		
		_socket->~TCPSocket();
		_socket = nullptr;
	}
}
