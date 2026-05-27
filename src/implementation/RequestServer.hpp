#if !defined(__BN3MONKEY_REQUESTSERVER__)
#define __BN3MONKEY_REQUESTSERVER__

#include "../SecuritySocket.hpp"

#include "PassiveSocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"
#include "ClientConnection.hpp"
#include "ObjectPool.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <list>

namespace Bn3Monkey
{
	class RequestServerImpl
	{
	public:
		RequestServerImpl(const NetworkConfiguration& configuration) : _configuration(configuration) {}
		RequestServerImpl(const NetworkConfiguration& configuration, const TlsServerConfiguration& tls_configuration) 
			: _configuration(configuration), _tls_configuration(tls_configuration) {}
		virtual ~RequestServerImpl();

		NetworkResult open(CustomProtocolRequestHandler* handler, size_t num_of_clients);
		void close();

	private:
		PassiveSocketContainer _container;
		PassiveSocket* _socket{ nullptr };

		NetworkConfiguration _configuration;
		TlsServerConfiguration _tls_configuration;

		std::atomic<bool> _is_running{ false };
		std::thread _routine;
			
		ObjectPool<ClientConnectionImpl> _socket_connection_pool {32};

		void run(CustomProtocolRequestHandler* handler);
	};

	// @Todo Limit the number of request workers to the number of core and distribute socket to limited workers

	// SocketRequestWorkers -> add(ClientConnectionImpl)
	//						                         -> onProcessed
	//                                                                -> send
	//                                                  true
	//                                               -> onProcessed
	//                                                                 -> send
	//                                                  false
	//                                                  removeRequest(this)
	// receiveRequest
	// remove()

}


#endif