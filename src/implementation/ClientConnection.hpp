#if !defined(__BN3MONKEY_CLIENT_CONNECTION__)
#define __BN3MONKEY_CLIENT_CONNECTION__

#include "../SecuritySocket.hpp"
#include "ServerActiveSocket.hpp"
#include "SocketEvent.hpp"

#include "connection/ConnectionState.hpp"
#include "connection/ConnectionPhase.hpp"
#include "connection/SniffPhase.hpp"
#include "connection/CustomPhase.hpp"
#include "connection/HttpPhase.hpp"
#include "connection/WebSocketPhase.hpp"
#include "core/memory/buffer.hpp"

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
    // All four phases (Sniff / Custom / Http / WebSocket) are wired: phaseForState
    // maps each protocol group to its phase, and an HTTP Upgrade crosses from the
    // HTTP group into the WebSocket group (the one sanctioned cross-group move).
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
        StagingBuffer& input()  override { return _input; }
        StagingBuffer& output() override { return _output; }

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
        SniffPhase     _sniff;
        CustomPhase    _custom_phase;
        HttpPhase      _http_phase;
        WebSocketPhase _websocket_phase;

        // ── input / output staging buffers (see StagingBuffer) ──
        // input:  recv appends at tail(); the phase parses head()..pending() and
        //         drain()s consumed messages (pipelined bytes stay pending).
        // output: the phase fills a response from data(); the host drains
        //         head()..pending() to the socket, flagging _output_fully_sent
        //         when empty(). _pdu_size is the initial capacity of both.
        size_t        _pdu_size{ 0 };
        StagingBuffer _input;
        StagingBuffer _output;
        bool          _output_fully_sent{ false };

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
