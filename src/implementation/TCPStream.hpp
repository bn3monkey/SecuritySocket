#if !defined __BN3MONKEY_TCPSTREAM__
#define __BN3MONKEY_TCPSTREAM__

#include "../SecuritySocket.hpp"
#include "TCPSocket.hpp"
#include <thread>
#include <atomic>

namespace Bn3Monkey
{
    class TCPStream
    {
    public:
        TCPStream() = delete;
        TCPStream(TCPEventHandler& handler, size_t max_retries, size_t pdu_size);
        virtual ~TCPStream() {}
        
        ConnectionResult getLastError();

    protected:
        void setLastError(ConnectionResult& result);
        void start(TCPSocket* socket);
        void stop();
        inline bool isRunning() { return _is_running.load(); }

        
    private:
        std::mutex _last_error_lock;
		ConnectionResult _last_error;

        void read(TCPSocket* socket);
        void write(TCPSocket* socket);
        TCPEventHandler& _handler;
        size_t _max_retries;
        size_t _pdu_size;

        std::thread _reader;
        std::thread _writer;
        std::atomic<bool> _is_running {false};
    };
}

#endif