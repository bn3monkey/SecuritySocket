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
#define WIN32_LEAN_AND_MEAN

#define BN3MONKEY_SECURITYSOCKET_VERSION_MAJOR 2
#define BN3MONKEY_SECURITYSOCKET_VERSION_MINOR 1
#define BN3MONKEY_SECURITYSOCKET_VERSION_REVISION 0

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define BN3MONKEY_SECURITYSOCKET_VERSION \
"v" TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_MAJOR) "." \
    TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_MINOR) "." \
    TOSTRING(BN3MONKEY_SECURITYSOCKET_VERSION_REVISION)



namespace Bn3Monkey
{

    enum class SECURITYSOCKET_API SocketCode
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
        constexpr static size_t MAX_PDU_SIZE = 32768;

        inline char* ip() { return _ip; } 
        inline char* port() {return _port;} 
        inline bool tls() { return _tls;} 
        inline size_t pdu_size() { return _pdu_size;} 
        inline uint32_t max_retries() { return _max_retries; } 
        inline uint32_t read_timeout() { return _read_timeout; } 
        inline uint32_t write_timeout() { return _write_timeout; } 


        explicit SocketConfiguration(
            const char* ip,
            uint32_t port,
            bool tls,
            uint32_t max_retries = 3,
            uint32_t read_timeout = 2000,
            uint32_t write_timeout = 2000,
            size_t pdu_size = MAX_PDU_SIZE) : 
            _tls(tls), 
            _pdu_size(pdu_size),
            _max_retries(max_retries),
            _read_timeout(read_timeout),
            _write_timeout(write_timeout)
        {
            ::memcpy(_ip, ip, strlen(ip));
            sprintf(_port, "%d", port);
        }


    private:
        char _ip[128] {0};
        char _port[16] {0};
        bool _tls {false};
        size_t _pdu_size { MAX_PDU_SIZE};
        uint32_t _max_retries{ 0 };
        uint32_t _read_timeout{ 0 };
        uint32_t _write_timeout{ 0 };
    };

    class SECURITYSOCKET_API SocketClient
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 2048;

        explicit SocketClient(const SocketConfiguration& configuration);
        virtual ~SocketClient();

        SocketResult open();
        void close();

        SocketResult connect();
        SocketResult read(void* buffer, size_t size);
        SocketResult write(const void* buffer, size_t size);

    private:
        char _container[IMPLEMENTATION_SIZE]{ 0 };
    };


    
    struct SECURITYSOCKET_API SocketRequestHandler
    {
        enum class ProcessState
        {
            INCOMPLETE,
            READY
        };

        virtual void onClientConnected(const char* ip, int port) = 0;
        virtual void onClientDisconnected(const char* ip, int port) = 0;

        virtual ProcessState onDataReceived(const void* input_buffer, size_t offset, size_t read_size) = 0;
        virtual bool onProcessed(
            const void* input_buffer, 
            size_t intput_size,
            void* output_buffer,
            size_t& output_size) = 0;
    };
    

    class SECURITYSOCKET_API SocketRequestServer
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 2048;

        explicit SocketRequestServer(const SocketConfiguration& configuration);
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
