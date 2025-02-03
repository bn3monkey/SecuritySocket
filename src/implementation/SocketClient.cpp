#include "TCPClient.hpp"



Bn3Monkey::TCPClientImpl::TCPClientImpl(const TCPConfiguration& configuration)
	: TCPStream(createSocket(configuration), configuration.max_retries, configuration.pdu_size)
{
	auto result = connect();
	setLastError(result);
}

Bn3Monkey::TCPClientImpl::~TCPClientImpl()
{
	close();
	disconnect();
#ifdef _WIN32
	WSACleanup();
#endif
}

Bn3Monkey::TCPSocket* Bn3Monkey::TCPClientImpl::createSocket(const TCPConfiguration& configuration)
{
	ConnectionResult result;
	TCPSocket* socket{ nullptr };

#ifdef _WIN32
	WSADATA data;
	int ret = WSAStartup(MAKEWORD(2, 2), &data);
	if (ret != 0) {
		result = ConnectionResult(ConnectionCode::WINDOWS_SOCKET_INITIALIZATION_FAIL, "Cannot start windows socket");
		setLastError(result);
		return nullptr;
	}
#endif

	TCPAddress address{ configuration.ip, configuration.port, false };
	result = address;
	if (result.code != ConnectionCode::SUCCESS) {
		setLastError(result);
		return nullptr;
	}

	if (configuration.tls) {
		socket = reinterpret_cast<TCPSocket*>(new(_container) TLSSocket{ address,
																	 configuration.read_timeout, configuration.write_timeout });
	}
	else {
		socket = new (_container) TCPSocket{ address, configuration.read_timeout, configuration.write_timeout };
	}

	result = *socket;
	if (result.code != ConnectionCode::SUCCESS) {
		delete socket;
		setLastError(result);
		return nullptr;
	}

	return socket;
}