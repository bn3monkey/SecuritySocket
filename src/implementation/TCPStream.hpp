#if !defined __BN3MONKEY_TCPSTREAM__
#define __BN3MONKEY_TCPSTREAM__

#include "../SecuritySocket.hpp"
#include "TCPSocket.hpp"
#include <thread>
#include <atomic>
#include <mutex>

namespace Bn3Monkey
{
    class TCPStream
    {
    public:
        TCPStream() = delete;
        TCPStream(TCPSocket* socket, size_t max_retries, size_t pdu_size);
        virtual ~TCPStream() {}
        
        void open(const std::shared_ptr<TCPEventHandler>& handler);
        void close();

        ConnectionResult connect();
        void disconnect();
        ConnectionResult read(char* buffer, size_t size);
        ConnectionResult write(char* buffer, size_t length);
        ConnectionResult getLastError();

    protected:
        void setLastError(ConnectionResult& result);
        inline bool isRunning() { return _is_running.load(); }

        
    private:
        std::mutex _last_error_lock;
		ConnectionResult _last_error;

        TCPSocket* _socket;
        void readRoutine();
        void writeRoutine();
        
        size_t _max_retries;
        size_t _pdu_size;

        std::shared_ptr<TCPEventHandler> _handler;
        std::thread _reader;
        std::thread _writer;
        std::atomic<bool> _is_running {false};
    };
}

#endif