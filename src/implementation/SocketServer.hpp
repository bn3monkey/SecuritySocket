#if !defined(__BN3MONKEY__TCPSERVER__)
#define __BN3MONKEY__TCPSERVER__
#include "../SecuritySocket.hpp"
#include "ActiveSocket.hpp"

#include <type_traits>
#include <atomic>

#include <mutex>
#include <thread>
#include <condition_variable>

namespace Bn3Monkey
{
	class SocketServerImpl
	{
	public:
		SocketServerImpl(const SocketConfiguration& configuration) : _configuration(configuration) {}
		virtual ~SocketServerImpl();

		SocketResult open(SocketEventHandler& handler, size_t num_of_clients);
		void close();

	private:
				
		static constexpr size_t container_size = sizeof(ActiveSocket) > sizeof(TLSActiveSocket) ? sizeof(ActiveSocket) : sizeof(TLSActiveSocket);
		char _container[container_size]{ 0 };
		ActiveSocket* _socket{ nullptr };

		SocketConfiguration _configuration;

		std::atomic<bool> _is_running{ false };
		std::thread _routine;

		static void run(std::atomic<bool>& is_running, size_t num_of_clients, ActiveSocket& sock, SocketConfiguration& config, SocketEventHandler& handler);
	};
}


#endif