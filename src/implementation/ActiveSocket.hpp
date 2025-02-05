#if !defined(__BN3MONKEY__ACTIVESOCKET__)
#define __BN3MONKEY__ACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "SocketAddress.hpp"

#include <cstdint>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <WS2tcpip.h>
#include <mswsock.h>
#else
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#endif


namespace Bn3Monkey {
	static constexpr size_t MAX_EVENTS = 16;

	enum class SocketEventType {
		UNDEFINED,
		ACCEPT,
		READ,
		WRITE
	};
	struct SocketEvent {
		SocketEventType type {SocketEventType::UNDEFINED};
		int32_t sock {-1};
	};

	struct SocketEventList {
		size_t length;
		SocketEvent events[MAX_EVENTS];
	};

	struct SocketEventResult
	{
		SocketResult result;
		SocketEvent event;
	};

	struct SocketEventListResult
	{
		SocketResult result;
		SocketEventList event_list;
	};
	
	
	class ActiveSocket {
	public:
		ActiveSocket(const SocketAddress& address) : _address(address) {}
		virtual SocketResult open();
		virtual void close();

		virtual SocketResult listen(size_t num_of_clients);
		
		virtual SocketEventListResult poll(uint32_t timeout_ms);
		virtual void addPollEvent(int32_t client_socket, SocketEventType type);
		virtual void removePollEvent(int32_t client_socket);

		virtual SocketEventResult accept();
		virtual int32_t read(int32_t client_socket, void* buffer, size_t size);
		virtual int32_t write(int32_t client_socket, const void* buffer, size_t size);	
		virtual void drop(int32_t client_socket);

		virtual SocketResult isConnected(int32_t client_socket);
	private:
		SocketAddress _address;
		int32_t _socket {-1};
		size_t _num_of_clients {0};

		
		int32_t linux_pollhandle {0};
		void* window_pollhandle {nullptr};
		SocketResult createPollHandle();		
	};

	class TLSActiveSocket : public ActiveSocket
	{
	public:
		TLSActiveSocket(const SocketAddress& address) : ActiveSocket(address) {}
		virtual SocketResult open();
		virtual void close();

		virtual SocketResult listen(size_t num_of_clients);
		virtual SocketEventListResult poll(uint32_t timeout_ms);

		virtual SocketEventResult accept();
		virtual int32_t read(int32_t client_socket, void* buffer, size_t size);
		virtual int32_t write(int32_t client_socket, const void* buffer, size_t size);
		virtual void drop(int32_t client_socket);
	};

}

#endif // __BN3MONKEY__ACTIVESOCKET__