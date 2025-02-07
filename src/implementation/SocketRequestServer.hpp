#if !defined(__BN3MONKEY__SOCKETREQUESTSERVER__)
#define __BN3MONKEY__SOCKETREQUESTSERVER__
#include "../SecuritySocket.hpp"
#include "PassiveSocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"
#include "SocketConnection.hpp"
#include "ObjectPool.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <list>

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
		PassiveSocketContainer _container;
		PassiveSocket* _socket{ nullptr };

		SocketConfiguration _configuration;

		std::atomic<bool> _is_running{ false };
		std::thread _routine;
			
		ObjectPool<SocketConnection> _socket_connection_pool {32};
	};

	// @Todo Limit the number of request workers to the number of core and distribute socket to limited workers

	// SocketRequestWorkers -> add(SocketConnection)
	//						                         -> onProcessed
	//                                                                -> send
	//                                                  true
	//                                               -> onProcessed
	//                                                                 -> send
	//                                                  false
	//                                                  removeRequest(this)
	// receiveRequest
	// remove()

	class SocketRequestWorkers
	{
	public:

		void remove();
	}
}


#endif