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
#include <condition_variable>

namespace Bn3Monkey
{
    class SocketBroadcastServerImpl
    {
    public:
		SocketBroadcastServerImpl(const SocketConfiguration& configuration) : _configuration(configuration) {}
        SocketBroadcastServerImpl(const SocketConfiguration& configuration, const SocketTLSServerConfiguration& tls_configuration)
            : _configuration(configuration), _tls_configuration(tls_configuration) {}

		virtual ~SocketBroadcastServerImpl();

		SocketResult open(size_t num_of_clients);
        SocketResult write(const void* buffer, size_t size);

        SocketResult await(uint64_t timeout_ms);
        SocketResult awaitClose(uint64_t timeout_ms);

        void close();

	private:
        SocketConfiguration _configuration;
        SocketTLSServerConfiguration _tls_configuration;

        PassiveSocketContainer _container;
        PassiveSocket* _socket{ nullptr };

        std::thread _monitor_client;
        std::atomic_bool _is_monitoring {false};
        void monitorClient();

        // Single-producer (monitor thread) / single-consumer (broadcast caller).
        // Monitor pushes accepted clients into _pending_clients under _pending_mtx.
        // Broadcast caller drains _pending_clients into _active_clients at the start
        // of each write, then operates on _active_clients without any lock.
        // The lock is held only for the brief drain, never during network I/O.
        std::mutex _pending_mtx;
        // Notified by monitor (after push) and close() (with _is_monitoring=false).
        // await() waits on this so it doesn't have to poll.
        std::condition_variable _pending_cv;
        std::vector<ServerActiveSocketContainer> _pending_clients;
        std::vector<ServerActiveSocketContainer> _active_clients;

        void drainPending();
    };
}

#endif // __BN3MONKEY__SOCKETEVENTSERVER__
