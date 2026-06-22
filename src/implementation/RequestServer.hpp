#if !defined(__BN3MONKEY_REQUESTSERVER__)
#define __BN3MONKEY_REQUESTSERVER__

#include "../SecuritySocket.hpp"

#include "PassiveSocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"
#include "ClientConnection.hpp"
#include "core/memory/fixed_pool.hpp"
#include "http/HttpRouter.hpp"

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

		// Public API widened in Phase 6 (D6). For now the body still requires a
		// CustomProtocolRequestHandler (recovered via asCustomProtocolRequestHandler);
		// the HTTP/WebSocket dispatch paths arrive with the Phase classes.
		NetworkResult open(RequestHandler* handler, size_t num_of_clients);
		void close();

	private:
		PassiveSocketContainer _container;
		PassiveSocket* _socket{ nullptr };

		NetworkConfiguration _configuration;
		TlsServerConfiguration _tls_configuration;

		std::atomic<bool> _is_running{ false };
		std::thread _routine;

		// Owned by the server (not a run() local) so close() can wake() it and
		// break the event loop out of epoll_wait immediately, rather than
		// waiting up to read_timeout for the next poll to lapse.
		SocketMultiEventListener _listener;

		FixedObjectPool<ClientConnectionImpl> _socket_connection_pool {32};

		// Built in open() when the handler supports HTTP; lives for the server's
		// lifetime. Connections borrow it by pointer (null when no HTTP).
		HttpRouterImpl                _router;
		bool                          _has_http{ false };
		CustomProtocolRequestHandler* _custom{ nullptr };

		void run(RequestHandler* handler);
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