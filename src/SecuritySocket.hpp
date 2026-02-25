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

#define WIN32_LEAN_AND_MEAN

#define BN3MONKEY_SECURITYSOCKET_VERSION_MAJOR 2
#define BN3MONKEY_SECURITYSOCKET_VERSION_MINOR 2
#define BN3MONKEY_SECURITYSOCKET_VERSION_REVISION 0

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define BN3MONKEY_SECURITYSOCKET_VERSION \
"v" TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_MAJOR) "." \
    TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_MINOR) "." \
    TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_REVISION)



namespace Bn3Monkey
{

    enum class SocketCode
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
    struct SECURITYSOCKET_API SocketResult
    {
        inline SocketCode code() { return _code; }
        inline int32_t bytes() { return _bytes; }
        const char* message();
                
        SocketResult(
            const SocketCode& code = SocketCode::SUCCESS,
            int32_t bytes = -1) : _code(code), _bytes(bytes) {
            }
    private:
        SocketCode _code;
        int32_t _bytes;
    };


    class SECURITYSOCKET_API SocketConfiguration {
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


        explicit SocketConfiguration(
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


    enum class SocketTLSVersion {
        TLS1_2 = 1 << 0,
        TLS1_3 = 1 << 1,
	};
    enum class SocketTLS1_2CipherSuite {
        ECDHE_ECDSA_AES256_GCM_SHA384 = 1 << 0,
        ECDHE_RSA_AES256_GCM_SHA384 = 1 << 1,
        ECDHE_ECDSA_CHACHA20_POLY1305 = 1 << 2, 
        ECDHE_RSA_CHACHA20_POLY1305 = 1 << 3,
    };
    enum class SocketTLS1_3CipherSuite {
        TLS_AES_128_GCM_SHA256 = 1 << 0,
        TLS_AES_256_GCM_SHA384 = 1 << 1,
        TLS_CHACHA20_POLY1305_SHA256 = 1 << 2,
        TLS_AES_128_CCM_SHA256 = 1 << 3,
        TLS_AES_128_CCM8_SHA256 = 1 << 4
	};
    enum class SocketTLSClientAuthenticationMode {
        AUTH_MODE_NONE,
        AUTH_MODE_OPTIONAL,
        AUTH_MODE_REQUIRED
	};

    class SECURITYSOCKET_API SocketTLSClientConfiguration
    {
    public:
        explicit SocketTLSClientConfiguration(
            std::initializer_list<SocketTLSVersion> support_versions = { },
            std::initializer_list<SocketTLS1_2CipherSuite> tls_1_2_cipher_suites = {},
            std::initializer_list<SocketTLS1_3CipherSuite> tls_1_3_cipher_suites = {},
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
        inline bool isVersionSupported(SocketTLSVersion version) const { return _tls_versions & static_cast<int32_t>(version); }
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

    class SECURITYSOCKET_API SocketTLSServerConfiguration
    {
    public:
        explicit SocketTLSServerConfiguration(
            std::initializer_list<SocketTLSVersion> support_versions = { },
            std::initializer_list<SocketTLS1_2CipherSuite> tls_1_2_cipher_suites = {},
            std::initializer_list<SocketTLS1_3CipherSuite> tls_1_3_cipher_suites = {},

            const char* server_cert_file_path = nullptr,
            const char* server_key_file_path = nullptr,
            const char* server_key_password = nullptr,

			SocketTLSClientAuthenticationMode client_authentication_mode = SocketTLSClientAuthenticationMode::AUTH_MODE_NONE,
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
        inline bool isVersionSupported(SocketTLSVersion version) const { return _tls_versions & static_cast<int32_t>(version); }
        void generateTLS12CipherSuites(char* ret) const;
        void generateTLS13CipherSuites(char* ret) const;
        inline const char* clientTrustStorePath() const { return _client_trust_store_path; }
        inline const char* serverCertFilePath() const { return _server_cert_file_path; }
        inline const char* serverKeyFilePath() const { return _server_key_file_path; }
        inline const char* serverKeyPassword() const { return _server_key_password; }
        inline SocketTLSClientAuthenticationMode clientAuthenticationMode() const { return _client_authentication_mode; }

    private:
		int32_t _tls_versions{ 0 };
		int32_t _tls_1_2_cipher_suites{ 0 };
		int32_t _tls_1_3_cipher_suites{ 0 };
		SocketTLSClientAuthenticationMode _client_authentication_mode{ SocketTLSClientAuthenticationMode::AUTH_MODE_NONE };

        char _client_trust_store_path[256]{ 0 };
        char _server_cert_file_path[256]{ 0 };
        char _server_key_file_path[256]{ 0 };
        char _server_key_password[256]{ 0 };
        TlsEventCallback _on_tls_event{ nullptr };
    };


    class SECURITYSOCKET_API SocketClient
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 2048;

        explicit SocketClient(const SocketConfiguration& configuration);
        explicit SocketClient(const SocketConfiguration& configuration, const SocketTLSClientConfiguration& tls_configuration);
        virtual ~SocketClient();

        SocketResult open();
        void close();

        SocketResult connect();
        SocketResult read(void* buffer, size_t size);
        SocketResult write(const void* buffer, size_t size);
        SocketResult isConnected();

    private:
        char _container[IMPLEMENTATION_SIZE]{ 0 };
    };


    /*
    struct SECURITYSOCKET_API SocketRequestHandler
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
        
    enum class SocketRequestMode
    {
        FAST,
        SLOW,
        READ_STREAM,
        WRITE_STREAM
    };

    struct SECURITYSOCKET_API SocketRequestHandler
    {
        virtual size_t getHeaderSize() = 0;
        virtual size_t getPayloadSize(const char* header) = 0;
        
        virtual SocketRequestMode onModeClassified(
            const char* header
        ) = 0;
        
        virtual void onClientConnected(const char* ip, int port) = 0;
        virtual void onClientDisconnected(const char* ip, int port) = 0;
        
        virtual void onProcessed(
            const char* header,
            const char* input_buffer,
            size_t input_size,
            char* output_buffer,
            size_t* output_size
        ) = 0;

        virtual void onProcessedWithoutResponse(
            const char* header,
            const char* input_buffer,
            size_t input_size
        ) = 0;
    };
    

    class SECURITYSOCKET_API SocketRequestServer
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 2048;

        explicit SocketRequestServer(const SocketConfiguration& configuration);
        explicit SocketRequestServer(const SocketConfiguration& configuration, const SocketTLSServerConfiguration& tls_configuration);
        virtual ~SocketRequestServer();

        SocketResult open(SocketRequestHandler* handler, size_t num_of_clients);
        void close();

    private:
        char _container[IMPLEMENTATION_SIZE]{ 0 };
    };

    class SECURITYSOCKET_API SocketBroadcastServer
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 2048;

        explicit SocketBroadcastServer(const SocketConfiguration& configuration);
        explicit SocketBroadcastServer(const SocketConfiguration& configuration, const SocketTLSServerConfiguration& tls_configuration);
        virtual ~SocketBroadcastServer();

        SocketResult open(size_t num_of_clients);
        void close();

        SocketResult enumerate();
        SocketResult write(const void* buffer, size_t size);
    private:
        char _container[IMPLEMENTATION_SIZE]{ 0 };
    };
       

    bool SECURITYSOCKET_API initializeSecuritySocket();
    void SECURITYSOCKET_API releaseSecuritySocket();
}

#endif
