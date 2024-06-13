#if !defined(__BN3MONKEY_SECURITY_SOCKET__)
#define __BN3MONKEY_SECURITY_SOCKET__

#include <string>
#include <cstdint>
#include <memory>

namespace Bn3Monkey
{
    enum class ConnectionCode
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
        
        SOCKET_CONNECTION_ALREADY,
        SOCKET_CONNECTION_IN_PROGRESS,

        TLS_SETFD_ERROR,

        SSL_PROTOCOL_ERROR,
        SSL_ERROR_CLOSED_BY_PEER,

        SOCKET_TIMEOUT,
        SOCKET_CLOSED,

        POLL_ERROR,
        UNKNOWN_ERROR,

        LENGTH,
    };
    struct ConnectionResult
    {
        ConnectionCode code{ ConnectionCode::SUCCESS };
        std::string message;
        
        ConnectionResult(const ConnectionCode& code = ConnectionCode::SUCCESS, const std::string& message = "") : code(code), message(message) {}
    };


    struct TCPConfiguration {
        constexpr static size_t MAX_PDU_SIZE = 32768;

        const std::string ip;
        const std::string port;
        const bool tls;
        const uint32_t max_retries;
        const uint32_t read_timeout;
        const uint32_t write_timeout;
        const size_t pdu_size;

        explicit TCPConfiguration(
            const std::string& ip,
            const std::string& port,
            bool tls,
            uint32_t max_retries,
            uint32_t read_timeout,
            uint32_t write_timeout,
            size_t pdu_size = MAX_PDU_SIZE) : 
            ip(ip), port(port), tls(tls), 
            max_retries(max_retries), 
            read_timeout(read_timeout),
            write_timeout(write_timeout),
            pdu_size(pdu_size)
        {}
    };

    class TCPEventHandler
    {
    public:
        
        virtual void onConnected() = 0;
        virtual void onDisconnected() = 0;
        virtual void onRead(char* buffer, size_t size) = 0;
        virtual size_t onWrite(char* buffer, size_t size) = 0;
        virtual void onError(const ConnectionResult& result) = 0;

    private:
    };

    class TCPClientImpl;
    class TCPServerImpl;


    class TCPClient
    {
    public:
        explicit TCPClient(const TCPConfiguration& configuration);
        
        virtual ~TCPClient();

        ConnectionResult getLastError();

        void open(TCPEventHandler& handler);
        void close();

        ConnectionResult read(char* buffer, size_t size);
        ConnectionResult write(char* buffer, size_t size);

    private:
        std::shared_ptr<TCPClientImpl> _impl;

        friend class TCPStream;
    };

    class TCPServer
    {
    public:
        explicit TCPServer(
            const TCPConfiguration& configuration,
            TCPEventHandler& handler
        );
        virtual ~TCPServer();

        ConnectionResult getLastError();
        void close();
    private:
        std::shared_ptr<TCPServerImpl> _impl;
    };

}

#endif