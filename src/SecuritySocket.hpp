#if !defined(__BN3MONKEY_SECURITY_SOCKET__)
#define __BN3MONKEY_SECURITY_SOCKET__

#include <string>
#include <cstdint>
#include <cstdio>
#include <memory>

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

        SOCKET_BIND_FAILED,
        SOCKET_LISTEN_FAILED,

        TLS_SETFD_ERROR,

        SSL_PROTOCOL_ERROR,
        SSL_ERROR_CLOSED_BY_PEER,

        SOCKET_TIMEOUT,
        SOCKET_CLOSED,

        POLL_ERROR,
        POLL_OBJECT_NOT_CREATED,
        POLL_EVENT_CANNOT_ADDED,

        UNKNOWN_ERROR,

        LENGTH,
    };
    struct SocketResult
    {
        inline SocketCode code() { return _code; }
        inline const char* message() { return _message; }
                
        SocketResult(
            const SocketCode& code = SocketCode::SUCCESS, 
            const char* message = "") : _code(code) {
                ::memcpy(_message, message, strlen(message));
            }
        SocketResult(const SocketResult& result) : _code(result._code) {
            ::memcpy(_message, result._message, 256);
        }
    private:
        SocketCode _code;
        char _message[256] {0};
    };


    class SocketConfiguration {
    public:
        constexpr static size_t MAX_PDU_SIZE = 32768;

        inline const char* ip() { return _ip; }
        inline const char* port() {return _port;}
        inline bool tls() { return _tls;}
        inline size_t pdu_size() { return _pdu_size;}
        inline const uint32_t max_retries() { return _max_retries; }
        inline const uint32_t read_timeout() { return _read_timeout; }
        inline const uint32_t write_timeout() { return _write_timeout; }


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
        uint32_t _max_retries;
        uint32_t _read_timeout;
        uint32_t _write_timeout;
    };

    struct SocketEventHandler
    {
    public:        
        virtual void onConnected() = 0;
        virtual void onDisconnected() = 0;
        
        virtual int onDataProcessed(const void* input_data, size_t input_size, void* output_data, size_t output_size) = 0;
    };



    class SocketClient
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 4096;

        explicit SocketClient(const SocketConfiguration& configuration);
        virtual ~SocketClient();

        SocketResult open();
        void close();

        SocketResult read(const void* buffer, size_t size);
        SocketResult write(void* buffer, size_t size);

    private:
        char _container[IMPLEMENTATION_SIZE];
    };


    class SocketServer
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 4096;

        explicit SocketServer(const SocketConfiguration& configuration);
        virtual ~SocketServer();

        bool open(SocketEventHandler& handler);
        void close();

    private:
        char _container[IMPLEMENTATION_SIZE];
    };

}

#endif