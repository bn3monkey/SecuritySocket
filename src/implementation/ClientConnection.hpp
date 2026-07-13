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

        // The container is moved in: after this returns the caller's container is
        // empty, so exactly one object owns the accepted fd (and, under TLS, the
        // SSL session). is_secure is not a parameter — it is read off the socket
        // that was actually accepted (ServerActiveSocket::isTls()).
        ClientConnectionImpl(ServerActiveSocketContainer&& container,
                             RequestHandler&               handler,
                             HttpRouterImpl*               router,   // null => no HTTP
                             CustomProtocolRequestHandler* custom,   // null => no Custom
                             size_t                        pdu_size,
                             size_t                        max_http_request_body_size);
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

        // Whether onConnected() has fired for this connection. A TLS connection
        // that dies mid-handshake never reaches it, and the server must not then
        // report onDisconnected() for a connection the handler never saw.
        bool connectedNotified() const { return _connected_notified; }

        // Close the underlying socket fd (idempotent). Called by the server
        // during teardown, after onDisconnected and before pool release.
        void closeSocket();

        ConnectionState state() const override { return _state; }

        // ── PhaseHost ──
        StagingBuffer& input()  override { return _input; }
        StagingBuffer& output() override { return _output; }

        ClientConnection&             connection()    override { return *this; }
        HttpRouterImpl*               router()        override { return _router; }
        CustomProtocolRequestHandler* customHandler() override { return _custom; }
        const char*                   wsPattern() const override { return _ws_pattern; }

        void setWebSocket(bool on) override { _is_websocket = on; }
        size_t maxHttpRequestBodySize() const override { return _max_http_request_body_size; }
        void dispatchSlow(std::function<void()> call,
                          SocketMultiEventListener& listener) override;

    private:
        // Drive the read loop: run the current phase's onReadable repeatedly
        // while it keeps making progress on buffered bytes (sniff carry-over,
        // pipelined / keep-alive messages).
        Disposition driveRead(SocketMultiEventListener& listener);
        Disposition onWriteEvent(SocketMultiEventListener& listener);

        // Push the TLS handshake one step (state == TlsHandshaking only). On
        // completion the connection leaves the antechamber for the state a
        // plaintext connection would have started in, and onConnected() fires.
        Disposition driveHandshake(SocketMultiEventListener& listener);

        // The active phase for a given state (null for host-handled lifecycle
        // states and for groups not yet implemented).
        ConnectionPhase* phaseForState(ConnectionState s);

        // Re-arm the listener (READ/WRITE) to match the current state. No-op if
        // already correct, or if the socket was detached for a SLOW dispatch.
        // A pending _tls_want overrides the state — see TlsWant.
        void armListener(SocketMultiEventListener& listener);

        // Re-arm in an explicitly given direction. Used where the state does not
        // determine the direction: TlsHandshaking, and TLS I/O that reports it
        // needs the opposite readiness from the one its state implies.
        void armListener(SocketMultiEventListener& listener, SocketEventType desired);

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
        size_t                        _max_http_request_body_size{ 0 };

        ConnectionState _state{ ConnectionState::Sniffing };
        SocketEventType _listener_event{ SocketEventType::READ };
        bool            _detached{ false };   // SLOW handed the socket to worker

        // Where the connection goes once TLS finishes: the state a plaintext
        // connection would have started in (Sniffing / ReceivingHttpRequest /
        // ReceivingCustomMessage). onAccept() computes it from handler capability;
        // a plaintext connection just starts there directly.
        ConnectionState _post_handshake_state{ ConnectionState::Sniffing };
        bool            _connected_notified{ false };

        // TLS can need the *opposite* readiness from the one the state implies:
        // SSL_read() may return WANT_WRITE (e.g. a TLS 1.3 KeyUpdate response it
        // must send first) and SSL_write() may return WANT_READ. The logical state
        // stays put; only the listener direction flips. NONE => infer from state.
        // Mirrored from ServerActiveSocket::ioWant() after every TLS read/write.
        TlsWant _tls_want{ TlsWant::NONE };

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
