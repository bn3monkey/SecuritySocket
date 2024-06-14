#include "TCPClient.hpp"



Bn3Monkey::TCPClientImpl::TCPClientImpl(const TCPConfiguration& configuration)
	: TCPStream(configuration.max_retries, configuration.pdu_size)
{

	ConnectionResult result;

#ifdef _WIN32
	WSADATA data;
	int ret = WSAStartup(MAKEWORD(2, 2), &data);
	if (ret != 0) {
		result = ConnectionResult(ConnectionCode::WINDOWS_SOCKET_INITIALIZATION_FAIL, "Cannot start windows socket");
		setLastError(result);
		return;
	}
#endif

	TCPAddress address {configuration.ip, configuration.port, false};
	result = address;
	if (result.code != ConnectionCode::SUCCESS) {
		setLastError(result);
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
		return;
	}

	size_t max_retries = configuration.max_retries;

	// Connect
	size_t trial = 0;
	for (size_t trial =0;trial<max_retries;trial++)
	{
		result = _socket->connect();
		if (result.code == ConnectionCode::SUCCESS) {
			break;
		}
		else if (result.code == ConnectionCode::SOCKET_TIMEOUT) {
			continue;
		}
		else { // Error
			_socket->~TCPSocket();
			_socket = nullptr;
			setLastError(result);
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

}

Bn3Monkey::TCPClientImpl::~TCPClientImpl()
{
	close();

	_socket->disconnect();
	_socket->~TCPSocket();
	_socket = nullptr;

#ifdef _WIN32
	WSACleanup();
#endif
}
