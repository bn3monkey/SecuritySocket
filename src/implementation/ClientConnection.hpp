#if !defined(__BN3MONKEY_CLIENT_CONNECTION__)
#define __BN3MONKEY_CLIENT_CONNECTION__

#include "../SecuritySocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"

#include "connection/ConnectionState.hpp"
#include "connection/ConnectionPhase.hpp"
#include "connection/SniffPhase.hpp"
#include "connection/CustomPhase.hpp"

#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace Bn3Monkey
{
    class HttpRouterImpl;   // borrowed by pointer; full type only used in the .cpp

    // Host of one accepted client socket and driver of its protocol state
    // machine (docs/workplan/client_connection.html). It owns the shared
    // resources — socket, the single input/output buffer, the lazy SLOW worker,
    // and all listener manipulation — and delegates per-protocol parsing and
    // next-state decisions to a ConnectionPhase strategy that it swaps as the
    // connection moves between protocol groups (Sniff -> Http / Custom; Http ->
    // WebSocket). It implements PhaseHost to expose exactly those shared
    // resources to the active phase.
    //
    // Phase 6 staging: SniffPhase + CustomPhase are wired now; HttpPhase and
    // WebSocketPhase arrive in following slices (their groups currently resolve
    // to no phase and the host closes the connection — unreachable for a
    // Custom-only handler).
    class ClientConnectionImpl : public ClientConnection,
                                 public SocketEventContext,
                                 public PhaseHost
    {
    public:
        enum class Disposition { KEEP, CLOSE };

        ClientConnectionImpl(ServerActiveSocketContainer&  container,
                             RequestHandler&               handler,
                             HttpRouterImpl*               router,   // null => no HTTP
                             CustomProtocolRequestHandler* custom,   // null => no Custom
                             size_t                        pdu_size,
                             bool                          is_secure);
        ~ClientConnectionImpl() override;

        // ── ClientConnection (user-facing) ──
        const char* ip()          const override { return _socket->ip(); }
        uint32_t    port()        const override { return static_cast<uint32_t>(_socket->port()); }
        bool        isSecure()    const override { return _is_secure; }
        bool        isWebSocket() const override { return _is_websocket; }

        // Set the initial state/phase from handler capabilities. Called by the
        // server right after acquire(), before the first event.
        void onAccept();

        // Drive one socket event. The listener is threaded through so the active
        // phase can hand a SLOW dispatch back to the host (which removes the
        // socket before queueing the worker). Returns CLOSE when the server must
        // tear the connection down (onDisconnected + removeEvent + release).
        Disposition handleEvent(SocketEventType ev, SocketMultiEventListener& listener);

        // Close the underlying socket fd (idempotent). Called by the server
        // during teardown, after onDisconnected and before pool release.
        void closeSocket();

        ConnectionState state() const { return _state; }

        // ── PhaseHost ──
        const char* input()      const override { return _input_buffer.data(); }
        size_t      inputSize()  const override { return _input_received; }
        void        ensureInputCapacity(size_t total_bytes) override;
        void        consumeInput(size_t n) override;

        char*  output()         override { return _output_buffer.data(); }
        size_t outputCapacity() const override { return _output_buffer.size(); }
        void   ensureOutputCapacity(size_t bytes) override;
        void   setOutputSize(size_t n) override { _output_size = n; _output_written = 0; }

        ClientConnection&             connection()    override { return *this; }
        HttpRouterImpl*               router()        override { return _router; }
        CustomProtocolRequestHandler* customHandler() override { return _custom; }
        const char*                   wsPattern() const override { return _ws_pattern; }

        void setWebSocket(bool on) override { _is_websocket = on; }
        void dispatchSlow(std::function<void()> call,
                          SocketMultiEventListener& listener) override;

    private:
        // Drive the read loop: run the current phase's onReadable repeatedly
        // while it keeps making progress on buffered bytes (sniff carry-over,
        // pipelined / keep-alive messages).
        Disposition driveRead(SocketMultiEventListener& listener);
        Disposition onWriteEvent(SocketMultiEventListener& listener);

        // The active phase for a given state (null for host-handled lifecycle
        // states and for groups not yet implemented).
        ConnectionPhase* phaseForState(ConnectionState s);

        // Re-arm the listener (READ/WRITE) to match the current state. No-op if
        // already correct, or if the socket was detached for a SLOW dispatch.
        void armListener(SocketMultiEventListener& listener);

        int  recvChunk();      // >0 bytes read, 0 would-block, -1 peer close/fatal
        bool flushOutput();    // false on socket error; sets _output_fully_sent

        void queueToWorker(std::function<void()> call, SocketMultiEventListener& listener);
        void workerLoop();

        // ── context ──
        ServerActiveSocketContainer   _container{};
        ServerActiveSocket*           _socket{ nullptr };
        RequestHandler&               _handler;
        HttpRouterImpl*               _router{ nullptr };
        CustomProtocolRequestHandler* _custom{ nullptr };
        const char*                   _ws_pattern{ nullptr };
        bool                          _is_secure{ false };
        bool                          _is_websocket{ false };
        bool                          _closed{ false };

        ConnectionState _state{ ConnectionState::Sniffing };
        SocketEventType _listener_event{ SocketEventType::READ };
        bool            _detached{ false };   // SLOW handed the socket to worker

        // ── phases (per-connection strategies) ──
        SniffPhase  _sniff;
        CustomPhase _custom_phase;

        // ── single input accumulation buffer (message starts at offset 0) ──
        std::vector<char> _input_buffer;
        size_t            _input_received{ 0 };

        // ── response output buffer ──
        std::vector<char> _output_buffer;
        size_t            _output_size{ 0 };
        size_t            _output_written{ 0 };
        bool              _output_fully_sent{ false };

        size_t _pdu_size{ 0 };

        // ── SLOW worker (lazy, single task slot; listener==nullptr => empty) ──
        std::thread             _worker_thread;
        bool                    _worker_should_stop{ false };
        struct WorkerTask {
            SocketMultiEventListener* listener{ nullptr };
            std::function<void()>     call;
        };
        WorkerTask              _pending_task;
        std::mutex              _task_mtx;
        std::condition_variable _task_cv;
    };
}

#endif // __BN3MONKEY_CLIENT_CONNECTION__
