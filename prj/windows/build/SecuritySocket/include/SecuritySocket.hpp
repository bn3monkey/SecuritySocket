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

        ADDRESS_ERROR,
        SOCKET_TIMEOUT,
        SOCKET_CLOSED,
        LENGTH,
    };
    struct ConnectionResult
    {
        const ConnectionCode code{ ConnectionCode::SUCCESS };
        const std::string message;
        ConnectionResult(const ConnectionCode& code = ConnectionCode::SUCCESS, const std::string& message = "") : code(code), message(message) {}
    };

    class TCPClientImpl;

    struct TCPClientConfiguration {
        const std::string ip;
        const std::string port;
        const bool tls;
        const uint32_t max_retries;
        const uint32_t timeout_milliseconds;

        explicit TCPClientConfiguration(
            const std::string& ip,
            const std::string& port,
            bool tls,
            uint32_t max_retries,
            uint32_t timeout_milliseconds) : ip(ip), port(port), tls(tls), max_retries(max_retries), timeout_milliseconds(timeout_milliseconds)
        {}
    };

    class TCPEventHandler
    {
    public:
        virtual void onConnected() = 0;
        virtual void onDisconnected() = 0;
        virtual void onRead(char* buffer, size_t size) = 0;
        virtual void onWrite(char* buffer, size_t size) = 0;
        virtual void onError(const ConnectionResult& result) = 0;
        virtual void onClosed() = 0;

    private:
    };

    

    class TCPClient
    {
    public:
        explicit TCPClient(
            const TCPClientConfiguration& configuration,
            TCPEventHandler& handler
            );
        
        virtual ~TCPClient();

        ConnectionResult getLastResult();

    private:
        std::shared_ptr<TCPClientImpl> _impl;
    };


}

#endif
