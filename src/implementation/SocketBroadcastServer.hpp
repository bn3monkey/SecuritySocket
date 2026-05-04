#if !defined(__BN3MONKEY__SOCKETBROADCASTSERVER__)
#define __BN3MONKEY__SOCKETBROADCASTSERVER__
#include "../SecuritySocket.hpp"

#include "ServerActiveSocket.hpp"

#include "PassiveSocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"
#include "SocketConnection.hpp"
#include "ObjectPool.hpp"

#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <condition_variable>

namespace Bn3Monkey
{
    // Per-client state used by the accept-monitor's SocketMultiEventListener.
    // Inheriting SocketEventContext lets us downcast back to BroadcastClient
    // when the listener fires DISCONNECTED for one of the registered fds.
    struct BroadcastClient : public SocketEventContext
    {
        ServerActiveSocketContainer container;
    };

    class SocketBroadcastServerImpl
    {
    public:
		SocketBroadcastServerImpl(const SocketConfiguration& configuration) : _configuration(configuration) {}
        SocketBroadcastServerImpl(const SocketConfiguration& configuration, const SocketTLSServerConfiguration& tls_configuration)
            : _configuration(configuration), _tls_configuration(tls_configuration) {}

		virtual ~SocketBroadcastServerImpl();

		SocketResult open(SocketBroadcastHandler* handler, size_t num_of_clients);
        SocketResult write(const void* buffer, size_t size);

        SocketResult await(uint64_t timeout_ms);
        SocketResult awaitClose(uint64_t timeout_ms);

        // Forcibly disconnect every currently-active client. Closes each
        // client socket, fires onClientDisconnected for each, and clears the
        // active list. Used to recover from peers that abandon their socket
        // without sending FIN (e.g., reconnecting via a fresh socket without
        // closing the old one) — the kernel never reports POLLHUP for those,
        // so the accept-monitor has no signal to detect the staleness.
        void dropAll();

        void close();

	private:
        SocketConfiguration _configuration;
        SocketTLSServerConfiguration _tls_configuration;

        PassiveSocketContainer _container;
        PassiveSocket* _socket{ nullptr };

        SocketBroadcastHandler* _handler{ nullptr };

        std::thread _monitor_client;
        std::atomic_bool _is_monitoring{ false };
        void monitorClient();

        // Listener and accept-context are members (rather than locals inside
        // monitorClient) so dropAll() running on the broadcast caller's thread
        // can call _listener.removeEvent() to take clients out of the polling
        // set without going through the monitor.
        SocketMultiEventListener _listener;
        SocketEventContext _server_context;

        // Single mutex protecting _active_clients and _pending_destruction.
        // The accept-monitor mutates _active_clients on ACCEPT/DISCONNECTED
        // events; broadcast callers snapshot it on write() and observe its
        // size on await/awaitClose.
        //
        // shared_ptr ownership lets write() snapshot the live set under lock,
        // then drop the lock and stream bytes — even if the monitor erases an
        // entry during the broadcast, the snapshot keeps the BroadcastClient
        // alive for the duration of the network I/O.
        //
        // _clients_cv is notified by the monitor (on every change), close(),
        // and dropAll(); await() waits for non-empty, awaitClose() waits for
        // empty.
        std::mutex _clients_mtx;
        std::condition_variable _clients_cv;
        std::vector<std::shared_ptr<BroadcastClient>> _active_clients;

        // Holds dropAll()'d clients until the accept-monitor's *next* loop
        // iteration. Reason: when dropAll runs, the monitor's currently
        // in-flight _listener.wait() may already hold a snapshot of context
        // pointers into these BroadcastClients; freeing them immediately
        // would race the dispatch step that dereferences context->type after
        // wait returns. The monitor clears this list at the top of each
        // iteration — by which point the previous wait+dispatch is fully
        // done, so it's safe to release the strong refs.
        std::vector<std::shared_ptr<BroadcastClient>> _pending_destruction;
    };
}

#endif // __BN3MONKEY__SOCKETEVENTSERVER__
