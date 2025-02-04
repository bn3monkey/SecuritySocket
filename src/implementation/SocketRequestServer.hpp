#if !defined(__BN3MONKEY__SOCKETREQUESTSERVER__)
#define __BN3MONKEY__SOCKETREQUESTSERVER__
#include "../SecuritySocket.hpp"
#include "ActiveSocket.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace Bn3Monkey
{
	class SocketRequestServerImpl
	{
	public:
		SocketRequestServerImpl(const SocketConfiguration& configuration) : _configuration(configuration) {}
		virtual ~SocketRequestServerImpl();

		SocketResult open(SocketRequestHandler& handler, size_t num_of_clients);
		void close();

	private:
				
		static constexpr size_t container_size = sizeof(ActiveSocket) > sizeof(TLSActiveSocket) ? sizeof(ActiveSocket) : sizeof(TLSActiveSocket);
		char _container[container_size]{ 0 };
		ActiveSocket* _socket{ nullptr };

		SocketConfiguration _configuration;

		std::atomic<bool> _is_running{ false };
		std::thread _routine;

		static void run(std::atomic<bool>& is_running, size_t num_of_clients, ActiveSocket& sock, SocketConfiguration& config, SocketRequestHandler& handler);
	};
}


#endif