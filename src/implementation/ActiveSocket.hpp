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
	static constexpr size_t MAX_CLIENTS = 16;

	enum class ActivePollType {
		UNDEFINED,
		ACCEPT,
		READ
	};

	struct ActivePollResult
	{
		SocketResult result;

		size_t length {0};
		ActivePollType types[MAX_CLIENTS] { UNDEFINED, };
		int32_t sockets[MAX_CLIENTS] {0};
	};
	
	class ActiveSocket {
	public:
		virtual SocketResult open(const SocketAddress& address);
		virtual void close();

		virtual SocketResult listen(size_t num_of_clients);
		
		virtual ActivePollResult poll(uint32_t timeout_ms);

		virtual int32_t accept();
		virtual int32_t read(int32_t client_socket, void* buffer, size_t size);
		virtual int32_t write(int32_t client_socket, const void* buffer, size_t size) ;
		
		virtual void drop(int32_t client_socket);
	private:
		SocketAddress _address;
		int32_t _socket {-1};
		size_t _num_of_clients {0};

		
		int32_t linux_pollhandle {0};
		void* window_pollhandle {nullptr};
		
#ifdef _WIN32
		// Function Pointer for acceptEx
		LPFN_ACCEPTEX acceptEx;
#endif
	};

	class TLSActiveSocket : public ActiveSocket
	{
	public:
		virtual SocketResult open(const SocketAddress& address);
		virtual void close();

		virtual SocketResult listen(size_t num_of_clients);
		virtual ActivePollResult poll(uint32_t timeout_ms);

		virtual int32_t accept();
		virtual int32_t read(int32_t client_socket, void* buffer, size_t size);
		virtual int32_t write(int32_t client_socket, const void* buffer, size_t size);
		virtual void drop(int32_t client_socket);
	};

}

#endif // __BN3MONKEY__ACTIVESOCKET__