#include "TCPClient.hpp"



Bn3Monkey::TCPClientImpl::TCPClientImpl(const TCPClientConfiguration& configuration, TCPEventHandler& handler)
	: _handler(handler)
{

	ConnectionResult result;

#ifdef _WIN32
	WSADATA data;
	int ret = WSAStartup(MAKEWORD(2, 2), &data);
	if (ret != 0) {
		setLastError(
			ConnectionResult(
				ConnectionCode::WINDOWS_SOCKET_INITIALIZATION_FAIL, "Cannot start windows socket"
			)
		);
		return;
	}
#endif

	TCPAddress address {configuration.ip, configuration.port};
	result = address;
	if (result.code != ConnectionCode::SUCCESS) {
		setLastError(result);
		return;
	}

	if (configuration.tls) {
		new (_container) TCPSocket {address };
	}
	else {
		new (_container) TLSSocket{ address };
	}

	result = _socket;
	if (result.code != ConnectionCode::SUCCESS) {
		setLastError(result);
		return;
	}

	size_t max_retries = configuration.max_retries;
	size_t timeout_milliseconds = configuration.timeout_milliseconds;
}

Bn3Monkey::TCPClientImpl::~TCPClientImpl()
{


#ifdef _WIN32
	WSACleanup();
#endif
}

