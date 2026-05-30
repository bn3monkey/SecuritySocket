#if !defined(__BN3MONKEY_SECURITY_SOCKET__)
#define __BN3MONKEY_SECURITY_SOCKET__

#if defined(_WIN32) || defined(_WIN64) // Windows
#ifdef SECURITYSOCKET_EXPORTS
#define SECURITYSOCKET_API __declspec(dllexport)
#else
#define SECURITYSOCKET_API /*__declspec(dllimport)*/
#endif
#elif defined(__linux__) || defined(__unix__) || defined(__ANDROID__) // Linux / Android
#ifdef SECURITYSOCKET_EXPORTS
#define SECURITYSOCKET_API __attribute__((visibility("default")))
#else
#define SECURITYSOCKET_API
#endif
#else 
#define SECURITYSOCKET_API
#pragma warning Unknown dynamic link import/export semantics.
#endif


#include <cstring>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <initializer_list>
#include <functional>

#define WIN32_LEAN_AND_MEAN

#define BN3MONKEY_SECURITYSOCKET_VERSION_MAJOR 2
#define BN3MONKEY_SECURITYSOCKET_VERSION_MINOR 3
#define BN3MONKEY_SECURITYSOCKET_VERSION_REVISION 1

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define BN3MONKEY_SECURITYSOCKET_VERSION \
"v" TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_MAJOR) "." \
    TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_MINOR) "." \
    TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_REVISION)



namespace Bn3Monkey
{

    enum class NetworkResultCode
    {
        SUCCESS,

        ADDRESS_NOT_AVAILABLE,

        WINDOWS_SOCKET_INITIALIZATION_FAIL,
        TLS_CONTEXT_INITIALIZATION_FAIL,
        TLS_INITIALIZATION_FAIL,

        // - SOCKET INITIALIZATION
        SOCKET_PERMISSION_DENIED,
        SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED,
        SOCKET_INVALID_ARGUMENT,
        SOCKET_CANNOT_CREATED,
        SOCKET_CANNOT_ALLOC,
        SOCKET_OPTION_ERROR,

        // - SOCKET CONNECTION
        // SOCKET_PERMISSION_DENIED,
        // SOCKET_ADDRESS_NOT_SUPPORTED
        SOCKET_CONNECTION_NOT_RESPOND,
        SOCKET_CONNECTION_ADDRESS_IN_USE,
        SOCKET_CONNECTION_BAD_DESCRIPTOR,
        SOCKET_CONNECTION_REFUSED,
        SOCKET_CONNECTION_BAD_ADDRESS,
        SOCKET_CONNECTION_UNREACHED,
        SOCKET_CONNECTION_INTERRUPTED,
        
        SOCKET_ALREADY_CONNECTED,
        SOCKET_CONNECTION_IN_PROGRESS,
        SOCKET_CONNECTION_NEED_TO_BE_BLOCKED,

        SOCKET_BIND_FAILED,
        SOCKET_LISTEN_FAILED,

        TLS_SETFD_ERROR,

        TLS_VERSION_NOT_SUPPORTED,
        TLS_CIPHER_SUITE_MISMATCH,
        TLS_SERVER_CERT_INVALID,
        TLS_CLIENT_CERT_REJECTED,
        TLS_HOSTNAME_MISMATCH,

        SSL_PROTOCOL_ERROR,
        SSL_ERROR_CLOSED_BY_PEER,

        SOCKET_TIMEOUT,
        SOCKET_CLOSED,

        SOCKET_EVENT_ERROR,
        SOCKET_EVENT_OBJECT_NOT_CREATED,
        SOCKET_EVENT_CANNOT_ADDED,

        SOCKET_SERVER_ALREADY_RUNNING,
        

        UNKNOWN_ERROR,

        LENGTH,
    };
    struct SECURITYSOCKET_API NetworkResult
    {
        inline NetworkResultCode code() { return _code; }
        inline int32_t bytes() { return _bytes; }
        const char* message();
                
        NetworkResult(
            const NetworkResultCode& code = NetworkResultCode::SUCCESS,
            int32_t bytes = -1) : _code(code), _bytes(bytes) {
            }
    private:
        NetworkResultCode _code;
        int32_t _bytes;
    };


    class SECURITYSOCKET_API NetworkConfiguration {
    public:
        constexpr static size_t MAX_PDU_SIZE = 65536;

        inline char* ip() { return _ip; } 
        inline char* port() {return _port;} 
        inline bool is_unix_domain() { return _is_unix_domain; }
        inline size_t pdu_size() { return _pdu_size;} 
        inline uint32_t max_retries() { return _max_retries; } 
        inline uint32_t read_timeout() { return _read_timeout; } 
        inline uint32_t write_timeout() { return _write_timeout; }
        inline uint32_t time_between_retries() { return _time_between_retries;  }


        explicit NetworkConfiguration(
            const char* ip,
            uint32_t port,
            bool is_unix_domain = false,
            uint32_t max_retries = 3,
            uint32_t read_timeout = 2000,
            uint32_t write_timeout = 2000,
            uint32_t time_between_retries = 100,
            size_t pdu_size = MAX_PDU_SIZE) : 
            _pdu_size(pdu_size),
            _max_retries(max_retries),
            _read_timeout(read_timeout),
            _write_timeout(write_timeout),
            _time_between_retries(time_between_retries),
            _is_unix_domain(is_unix_domain)
        {
            ::memcpy(_ip, ip, strlen(ip));
            snprintf(_port,16, "%d", port);
        }


    private:
        char _ip[128] {0};
        char _port[16] {0};
        size_t _pdu_size { MAX_PDU_SIZE};
        uint32_t _max_retries{ 0 };
        uint32_t _read_timeout{ 0 };
        uint32_t _write_timeout{ 0 };
        uint32_t _time_between_retries{ 0 };
        bool _is_unix_domain{ false };
    };


    enum class TlsVersion {
        TLS1_2 = 1 << 0,
        TLS1_3 = 1 << 1,
	};
    enum class TlsV12CipherSuite {
        ECDHE_ECDSA_AES256_GCM_SHA384 = 1 << 0,
        ECDHE_RSA_AES256_GCM_SHA384 = 1 << 1,
        ECDHE_ECDSA_CHACHA20_POLY1305 = 1 << 2, 
        ECDHE_RSA_CHACHA20_POLY1305 = 1 << 3,
    };
    enum class TlsV13CipherSuite {
        TLS_AES_128_GCM_SHA256 = 1 << 0,
        TLS_AES_256_GCM_SHA384 = 1 << 1,
        TLS_CHACHA20_POLY1305_SHA256 = 1 << 2,
        TLS_AES_128_CCM_SHA256 = 1 << 3,
        TLS_AES_128_CCM8_SHA256 = 1 << 4
	};
    enum class TlsClientAuthMode {
        AUTH_MODE_NONE,
        AUTH_MODE_OPTIONAL,
        AUTH_MODE_REQUIRED
	};

    class SECURITYSOCKET_API TlsClientConfiguration
    {
    public:
        explicit TlsClientConfiguration(
            std::initializer_list<TlsVersion> support_versions = { },
            std::initializer_list<TlsV12CipherSuite> tls_1_2_cipher_suites = {},
            std::initializer_list<TlsV13CipherSuite> tls_1_3_cipher_suites = {},
            bool verify_server = false,
            bool verify_hostname = false,
            const char* server_trust_store_path = nullptr,

            bool use_client_certificate = false,
            const char* client_cert_file_path = nullptr,
            const char* client_key_file_path = nullptr,
            const char* client_key_password = nullptr
        ) : _verify_server(verify_server),
            _verify_hostname(verify_hostname),
            _use_client_certificate(use_client_certificate)
        {
            for (auto& version : support_versions) {
                _tls_versions |= static_cast<int32_t>(version);
            }
            for (auto& cipher_suite : tls_1_2_cipher_suites) {
                _tls_1_2_cipher_suites |= static_cast<int32_t>(cipher_suite);
            }
            for (auto& cipher_suite : tls_1_3_cipher_suites) {
                _tls_1_3_cipher_suites |= static_cast<int32_t>(cipher_suite);
            }
            if (server_trust_store_path)
                snprintf(_server_trust_store_path, sizeof(_server_trust_store_path), "%s", server_trust_store_path);
            if (client_cert_file_path)
                snprintf(_client_cert_file_path, sizeof(_client_cert_file_path), "%s", client_cert_file_path);
            if (client_key_file_path)
                snprintf(_client_key_file_path, sizeof(_client_key_file_path), "%s", client_key_file_path);
            if (client_key_password)
                snprintf(_client_key_password, sizeof(_client_key_password), "%s", client_key_password);
        }

        using TlsEventCallback = void(*)(const char*);
        inline void setOnTLSEvent(TlsEventCallback on_tls_event) {
            _on_tls_event = on_tls_event;
        }
        inline TlsEventCallback getOnTLSEvent() const {
            return _on_tls_event;
        }


        inline bool valid() const { return _tls_versions != 0; }
        inline bool isVersionSupported(TlsVersion version) const { return _tls_versions & static_cast<int32_t>(version); }
        void generateTLS12CipherSuites(char* ret) const;
        void generateTLS13CipherSuites(char* ret) const;
        inline const char* serverTrustStorePath() const { return _server_trust_store_path; }
        inline const char* clientCertFilePath() const { return _client_cert_file_path; }
        inline const char* clientKeyFilePath() const { return _client_key_file_path; }
        inline const char* clientKeyPassword() const { return _client_key_password; }
        inline bool shouldVerifyServer() const { return _verify_server; }
        inline bool shouldVerifyHostname() const { return _verify_hostname; }
        inline bool shouldUseClientCertificate() const { return _use_client_certificate; }

    private:
        int32_t _tls_versions{ 0 };
        int32_t _tls_1_2_cipher_suites{ 0 };
        int32_t _tls_1_3_cipher_suites{ 0 };

        bool _verify_server{ false };
        bool _verify_hostname{ false };
        bool _use_client_certificate{ false };
        bool _reserved{ false };

        char _server_trust_store_path[256]{ 0 };
        char _client_cert_file_path[256]{ 0 };
        char _client_key_file_path[256]{ 0 };
        char _client_key_password[256]{ 0 };

        TlsEventCallback _on_tls_event{ nullptr };
    };

    class SECURITYSOCKET_API TlsServerConfiguration
    {
    public:
        explicit TlsServerConfiguration(
            std::initializer_list<TlsVersion> support_versions = { },
            std::initializer_list<TlsV12CipherSuite> tls_1_2_cipher_suites = {},
            std::initializer_list<TlsV13CipherSuite> tls_1_3_cipher_suites = {},

            const char* server_cert_file_path = nullptr,
            const char* server_key_file_path = nullptr,
            const char* server_key_password = nullptr,

			TlsClientAuthMode client_authentication_mode = TlsClientAuthMode::AUTH_MODE_NONE,
            const char* client_trust_store_path = nullptr
		) : _client_authentication_mode(client_authentication_mode)
        {
            for (auto& version : support_versions) {
                _tls_versions |= static_cast<int32_t>(version);
            }
            for (auto& cipher_suite : tls_1_2_cipher_suites) {
                _tls_1_2_cipher_suites |= static_cast<int32_t>(cipher_suite);
            }
            for (auto& cipher_suite : tls_1_3_cipher_suites) {
                _tls_1_3_cipher_suites |= static_cast<int32_t>(cipher_suite);
            }
            if (client_trust_store_path)
                snprintf(_client_trust_store_path, sizeof(_client_trust_store_path), "%s", client_trust_store_path);
            if (server_cert_file_path)
                snprintf(_server_cert_file_path, sizeof(_server_cert_file_path), "%s", server_cert_file_path);
            if (server_key_file_path)
                snprintf(_server_key_file_path, sizeof(_server_key_file_path), "%s", server_key_file_path);
            if (server_key_password)
                snprintf(_server_key_password, sizeof(_server_key_password), "%s", server_key_password);
        }

        using TlsEventCallback = void(*)(const char*);
        inline void setOnTLSEvent(TlsEventCallback on_tls_event) {
            _on_tls_event = on_tls_event;
        }
        inline TlsEventCallback getOnTLSEvent() const {
            return _on_tls_event;
        }
        inline bool valid() const { return _tls_versions != 0; }
        inline bool isVersionSupported(TlsVersion version) const { return _tls_versions & static_cast<int32_t>(version); }
        void generateTLS12CipherSuites(char* ret) const;
        void generateTLS13CipherSuites(char* ret) const;
        inline const char* clientTrustStorePath() const { return _client_trust_store_path; }
        inline const char* serverCertFilePath() const { return _server_cert_file_path; }
        inline const char* serverKeyFilePath() const { return _server_key_file_path; }
        inline const char* serverKeyPassword() const { return _server_key_password; }
        inline TlsClientAuthMode clientAuthenticationMode() const { return _client_authentication_mode; }

    private:
		int32_t _tls_versions{ 0 };
		int32_t _tls_1_2_cipher_suites{ 0 };
		int32_t _tls_1_3_cipher_suites{ 0 };
		TlsClientAuthMode _client_authentication_mode{ TlsClientAuthMode::AUTH_MODE_NONE };

        char _client_trust_store_path[256]{ 0 };
        char _server_cert_file_path[256]{ 0 };
        char _server_key_file_path[256]{ 0 };
        char _server_key_password[256]{ 0 };
        TlsEventCallback _on_tls_event{ nullptr };
    };


    class SECURITYSOCKET_API Client
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 2048;

        explicit Client(const NetworkConfiguration& configuration);
        explicit Client(const NetworkConfiguration& configuration, const TlsClientConfiguration& tls_configuration);
        virtual ~Client();

        NetworkResult open();
        void close();

        NetworkResult connect();
        NetworkResult read(void* buffer, size_t size);
        NetworkResult write(const void* buffer, size_t size);
        NetworkResult isConnected();

    private:
        char _container[IMPLEMENTATION_SIZE]{ 0 };
    };


    /*
    struct SECURITYSOCKET_API CustomProtocolRequestHandler
    {
        enum class ProcessState
        {
            INCOMPLETE,
            READY,
            READY_BUT_NO_RESPONSE,
        };

        virtual void onClientConnected(const char* ip, int port) = 0;
        virtual void onClientDisconnected(const char* ip, int port) = 0;

        virtual ProcessState onDataReceived(const void* input_buffer, size_t offset, size_t read_size) = 0;
        
        virtual void onProcessedWithoutResponse(
            const void* input_buffer,
            size_t input_size) = 0;

        virtual bool onProcessed(
            const void* input_buffer,
            size_t intput_size,
            void* output_buffer,
            size_t& output_size) = 0;

    };
    */

    // 오래 걸릴 것 같은 작업은 다른 쓰레드에서 처리하게 함.
    // 애초에 payload를 다른 쓰레드에서 read를 여러번하고 write를 하자
    // 금방 끝날 것은 이 쓰레드에서 처리하기.

    enum class RequestProcessingMode
    {
        FAST,
        SLOW,
        READ_STREAM,
        WRITE_STREAM
    };

    // Pure-abstract view of an active client connection. Handler callbacks
    // receive it instead of raw (ip, port) so future phases can attach
    // per-connection metadata (TLS state, WS upgrade, etc.) without
    // changing the public signature.
    class SECURITYSOCKET_API ClientConnection
    {
    public:
        virtual ~ClientConnection() = default;
        virtual const char* ip()          const = 0;
        virtual uint32_t    port()        const = 0;
        virtual bool        isSecure()    const = 0;
        virtual bool        isWebSocket() const = 0;
    };

    // Base for request-style handlers (CustomProtocolRequestHandler today,
    // HttpRequestHandler in later phases). Lifecycle callbacks have empty
    // defaults so derived classes only override what they need. supportXxx()
    // flags let the server pick a dispatch path without RTTI.
    //
    // BroadcastHandler is intentionally NOT derived from this base —
    // broadcast servers have a different lifecycle model and keep their
    // own callback shape.
    // Forward declarations so RequestHandler can hand back typed self-pointers
    // (the self-accessors below) without a circular definition order.
    class HttpRequestHandler;
    class CustomProtocolRequestHandler;

    class SECURITYSOCKET_API RequestHandler
    {
    public:
        virtual ~RequestHandler() = default;
        virtual void onConnected   (const ClientConnection& conn) { (void)conn; }
        virtual void onDisconnected(const ClientConnection& conn) { (void)conn; }

        // Derived classes flip the relevant flag(s) to true. The server
        // reads them in open() to choose its dispatch path.
        virtual bool supportHttp()         const { return false; }
        virtual bool supportUserProtocol() const { return false; }
        virtual bool supportWebSocket()    const { return false; }

        // Typed self-accessors. The two concrete handler types inherit
        // RequestHandler *virtually* (so a class can derive both without a
        // diamond), which makes a static_cast from RequestHandler* down to a
        // derived ill-formed; the library is also built with RTTI disabled, so
        // dynamic_cast is unavailable. Each derived overrides its own accessor
        // to return `this`; the server recovers the concrete pointer through
        // these instead of any cast.
        virtual HttpRequestHandler*           asHttpRequestHandler()           { return nullptr; }
        virtual CustomProtocolRequestHandler* asCustomProtocolRequestHandler() { return nullptr; }
    };

    // WebSocket binding for a CustomProtocolRequestHandler. Default-constructed
    // (pattern == nullptr) means "raw TCP only"; supplying a pattern flips the
    // owning handler's supportWebSocket() to true and registers an Upgrade
    // route for that path (Phase 6).
    struct SECURITYSOCKET_API WebSocketConfiguration
    {
        const char* pattern{ nullptr };
        bool valid() const { return pattern != nullptr; }
    };

    // ── Custom Protocol types (Phase 5) ──
    //
    // Pure abstract view/builder, symmetric with the HTTP pair below. The
    // concrete *Impl lives behind the DLL boundary; the server constructs them
    // on the stack per dispatch (ClientConnectionImpl wiring) and hands the
    // interfaces to the handler. User code never sees the Impls.

    // View over one incoming Custom Protocol message. header() spans the
    // fixed-size protocol header (headerSize() bytes); payload() spans the
    // body whose length classifyMode()/payloadSize() derived from the header.
    // Both pointers borrow buffers the connection owns for the duration of the
    // process() call only.
    class SECURITYSOCKET_API CustomProtocolRequest
    {
    public:
        virtual ~CustomProtocolRequest() = default;
        virtual const void* header()        const = 0;
        virtual size_t      headerLength()  const = 0;
        virtual const void* payload()       const = 0;
        virtual size_t      payloadLength() const = 0;
    };

    // Builder for the outgoing Custom Protocol response. The handler writes its
    // bytes directly into data() (up to capacity()) and then records how many
    // it produced with setLength(). data() points into the connection's output
    // buffer; capacity() is that buffer's size.
    class SECURITYSOCKET_API CustomProtocolResponse
    {
    public:
        virtual ~CustomProtocolResponse() = default;
        virtual void*  data()              = 0;
        virtual size_t capacity()    const = 0;
        virtual void   setLength(size_t n) = 0;
    };

    class SECURITYSOCKET_API CustomProtocolRequestHandler
        : public virtual RequestHandler
    {
    public:
        CustomProtocolRequestHandler() = default;
        explicit CustomProtocolRequestHandler(const WebSocketConfiguration& ws)
            : _ws_config(ws) {}

        bool supportUserProtocol() const override final { return true; }
        // Supplying a WebSocketConfiguration with a pattern flips this to true
        // (Phase 6 then auto-registers the Upgrade route for that path).
        bool supportWebSocket()    const override final { return _ws_config.valid(); }

        const WebSocketConfiguration& webSocketConfig() const { return _ws_config; }

        CustomProtocolRequestHandler* asCustomProtocolRequestHandler() override { return this; }

        // header/payload sizes are static properties of the protocol, so no
        // connection argument. classifyMode() inspects the header to pick the
        // FAST/SLOW/STREAM dispatch path per message.
        virtual size_t                headerSize()                      = 0;
        virtual size_t                payloadSize (const void* header)   = 0;
        virtual RequestProcessingMode classifyMode(const void* header)  = 0;

        // process() keeps the connection — needed to identify the response
        // target. The handler fills res and calls res.setLength().
        virtual void process(const ClientConnection&     conn,
                             const CustomProtocolRequest& req,
                             CustomProtocolResponse&      res) = 0;
        virtual void processWithoutResponse(const ClientConnection&     conn,
                                            const CustomProtocolRequest& req) = 0;

    private:
        WebSocketConfiguration _ws_config{};
    };

    // ── HTTP types (Phase 4) ──
    //
    // All pure abstract — concrete *Impl lives behind the DLL boundary. The
    // server constructs Impls on the stack per request (Phase 6 wiring); user
    // code only ever sees these interfaces.

    // View over an incoming HTTP request. pathParam/query auto-decode
    // percent-encoded input; header() returns the raw value (HTTP headers
    // have their own quoting rules, callers handle those if they need to).
    class SECURITYSOCKET_API HttpRequest
    {
    public:
        virtual ~HttpRequest() = default;
        virtual const char* method()                   const = 0;
        virtual const char* path()                     const = 0;
        virtual const char* header   (const char* name) const = 0;
        virtual const char* pathParam(const char* name) const = 0;
        virtual const char* query    (const char* name) const = 0;
        virtual const void* body()                     const = 0;
        virtual size_t      bodySize()                 const = 0;
    };

    // Builder for an outgoing HTTP response. Fluent: status().header().body()
    // chains because each call returns *this. json() is a thin convenience
    // over body() that also sets Content-Type to application/json.
    class SECURITYSOCKET_API HttpResponse
    {
    public:
        virtual ~HttpResponse() = default;
        virtual HttpResponse& status(int code)                                     = 0;
        virtual HttpResponse& header(const char* name, const char* value)          = 0;
        virtual HttpResponse& body  (const void* data, size_t size)                = 0;
        virtual HttpResponse& json  (const char* json_str)                         = 0;
    };

    // Route registry passed to HttpRequestHandler::registerRoutes. The mode
    // argument is *route-static* — it picks FAST (event-loop thread) vs SLOW
    // (worker thread) at registration time, never per-message. Custom protocol
    // does per-message classification; HTTP doesn't because path already
    // discriminates work shape.
    class SECURITYSOCKET_API HttpRouter
    {
    public:
        using HandlerFn = std::function<void(ClientConnection&, HttpRequest&, HttpResponse&)>;

        virtual ~HttpRouter() = default;

        virtual void get    (const char* pattern, HandlerFn fn,
                             RequestProcessingMode mode = RequestProcessingMode::FAST) = 0;
        virtual void post   (const char* pattern, HandlerFn fn,
                             RequestProcessingMode mode = RequestProcessingMode::FAST) = 0;
        virtual void put    (const char* pattern, HandlerFn fn,
                             RequestProcessingMode mode = RequestProcessingMode::FAST) = 0;
        virtual void del    (const char* pattern, HandlerFn fn,
                             RequestProcessingMode mode = RequestProcessingMode::FAST) = 0;
        virtual void patch  (const char* pattern, HandlerFn fn,
                             RequestProcessingMode mode = RequestProcessingMode::FAST) = 0;
        virtual void head   (const char* pattern, HandlerFn fn,
                             RequestProcessingMode mode = RequestProcessingMode::FAST) = 0;
        virtual void options(const char* pattern, HandlerFn fn,
                             RequestProcessingMode mode = RequestProcessingMode::FAST) = 0;
        // Catch-all when no method+path matches. Used to render custom 404 /
        // 405 bodies; without it the server emits a minimal default 404.
        virtual void fallback(HandlerFn fn,
                              RequestProcessingMode mode = RequestProcessingMode::FAST) = 0;
    };

    // Derived handler for HTTP traffic. registerRoutes runs once at open()
    // before the listen loop, so handlers can build their trie eagerly.
    // supportHttp() is sealed true so the server's capability check sees the
    // handler without needing dynamic_cast.
    class SECURITYSOCKET_API HttpRequestHandler
        : public virtual RequestHandler
    {
    public:
        bool supportHttp() const override final { return true; }
        virtual void registerRoutes(HttpRouter& router) = 0;

        HttpRequestHandler* asHttpRequestHandler() override { return this; }
    };

    struct SECURITYSOCKET_API BroadcastHandler {
        virtual ~BroadcastHandler() = default;
        virtual void onClientConnected(const char* ip, int port) = 0;
        virtual void onClientDisconnected(const char* ip, int port) = 0;
    };



    class SECURITYSOCKET_API RequestServer
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 2048;

        explicit RequestServer(const NetworkConfiguration& configuration);
        explicit RequestServer(const NetworkConfiguration& configuration, const TlsServerConfiguration& tls_configuration);
        virtual ~RequestServer();

        // Accepts any RequestHandler — a CustomProtocolRequestHandler today,
        // and (as Phase 6 lands) an HttpRequestHandler or a class deriving both.
        // The server inspects the handler's capabilities to choose its dispatch
        // path. A CustomProtocolRequestHandler* still binds via the implicit
        // derived->base conversion, so existing callers are source-compatible.
        NetworkResult open(RequestHandler* handler, size_t num_of_clients);
        void close();

    private:
        char _container[IMPLEMENTATION_SIZE]{ 0 };
    };

    class SECURITYSOCKET_API BroadcastServer
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 2048;

        explicit BroadcastServer(const NetworkConfiguration& configuration);
        explicit BroadcastServer(const NetworkConfiguration& configuration, const TlsServerConfiguration& tls_configuration);
        virtual ~BroadcastServer();

        NetworkResult open(BroadcastHandler* handler, size_t num_of_clients);
        void close();

        NetworkResult write(const void* buffer, size_t size);

        // Block until at least one healthy client is connected, or until timeout_ms
        // elapses. Stale clients (peer already closed) are detected and pruned as
        // part of the wait, so each successful return reflects a live peer.
        NetworkResult await(uint64_t timeout_ms);
        // Block until every currently-active client has closed (peer FIN received),
        // or until timeout_ms elapses. Use as an explicit barrier between broadcast
        // rounds so the next await() starts from a clean active list.
        NetworkResult awaitClose(uint64_t timeout_ms);

        // Forcibly disconnect every currently-active client. Closes each socket,
        // fires onClientDisconnected for each, and clears the active list. Use
        // when a peer is known to have abandoned its socket without sending FIN
        // (e.g., reconnecting via a fresh socket without closing the old one) —
        // the kernel reports no POLLHUP for those, so the accept-monitor has no
        // signal to clean them up on its own.
        void dropAll();
    private:
        char _container[IMPLEMENTATION_SIZE]{ 0 };
    };
       

    bool SECURITYSOCKET_API initializeSecuritySocket();
    void SECURITYSOCKET_API releaseSecuritySocket();
}

#endif
