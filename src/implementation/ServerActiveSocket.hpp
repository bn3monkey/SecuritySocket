#if !defined(__BN3MONKEY__SERVERACTIVESOCKET__)
#define __BN3MONKEY__SERVERACTIVESOCKET__

#include "../SecuritySocket.hpp"
#include "BaseSocket.hpp"
#include "SocketHelper.hpp"

#include <cstdint>
#include <cstring>
#include <utility>

#include "TlsHelper.hpp"

namespace Bn3Monkey
{
    // Outcome of pushing the TLS handshake forward by one step.
    // WANT_READ / WANT_WRITE mirror OpenSSL's SSL_ERROR_WANT_READ / _WANT_WRITE:
    // the handshake made progress but needs the socket readable / writable before
    // it can continue. The caller re-arms the listener accordingly and retries on
    // the next event.
    enum class TLSHandshakeState : uint8_t { DONE, WANT_READ, WANT_WRITE, FAILED };

    // Which readiness the last TLS read()/write() said it needs before it can make
    // progress. NONE = no TLS stall; the caller infers direction from state as usual.
    //
    // This exists because NetworkResult cannot carry it: createTLSResult() collapses
    // both SSL_ERROR_WANT_READ and SSL_ERROR_WANT_WRITE into the single code
    // SOCKET_CONNECTION_NEED_TO_BE_BLOCKED. Under TLS the direction is exactly what
    // the caller needs — SSL_read() can stall wanting *write* — so the socket records
    // it here rather than losing it.
    enum class TlsWant : uint8_t { NONE, READ, WRITE };

    class ServerActiveSocket : public BaseSocket
    {
    public:
        ServerActiveSocket() {}
        ServerActiveSocket(int32_t sock, void* addr, void* ssl_context = nullptr);
        virtual ~ServerActiveSocket();

        // SocketContainer is move-only. Steal the fd so the moved-from object
        // can never close a descriptor we still own.
        ServerActiveSocket(ServerActiveSocket&& other) noexcept {
            _socket = other._socket;
            _result = other._result;
            _client_port = other._client_port;
            std::memcpy(_client_ip, other._client_ip, sizeof(_client_ip));
            other._socket = -1;
        }
        ServerActiveSocket(const ServerActiveSocket&) = delete;
        ServerActiveSocket& operator=(const ServerActiveSocket&) = delete;

        inline NetworkResult result() { return _result; }
        virtual void close();
		virtual NetworkResult read(void* buffer, size_t size);
        virtual NetworkResult write(const void* buffer, size_t size);

        // Whether this connection is encrypted. Derived from the socket that was
        // actually accepted, not from "a TLS configuration was supplied" — the
        // two are equivalent today but only the former is what isSecure() means.
        virtual bool isTls() const { return false; }

        // Push the TLS handshake forward by one step. A plaintext socket has
        // nothing to negotiate, so it is born DONE and the host's antechamber
        // state is skipped entirely.
        virtual TLSHandshakeState handshake() { return TLSHandshakeState::DONE; }

        // Plaintext already decrypted inside the SSL object but not yet handed to
        // the caller. A level-triggered epoll will NOT re-fire for these (the TCP
        // socket has no unread bytes), so the read loop must drain them itself.
        virtual bool hasBufferedInput() const { return false; }

        // Readiness the last read()/write() stalled on. Plaintext never stalls on
        // the opposite direction, so it always reports NONE.
        virtual TlsWant ioWant() const { return TlsWant::NONE; }

        inline const char* ip() const { return _client_ip; }
        inline int port() const { return _client_port; }

        void setSocketBufferSize(size_t size);
        // Disable Nagle's algorithm on this connection. Call right after accept()
        // for latency-sensitive servers (e.g. broadcast/event delivery) so each
        // write() flushes immediately instead of being coalesced by the kernel.
        void setNoDelay();

    protected:
        char _client_ip[22]{ 0 };
        int _client_port = 0;
    };

    class TlsServerActiveSocket : public ServerActiveSocket
    {
    public:
        TlsServerActiveSocket() {}
        TlsServerActiveSocket(int32_t sock, void* addr, void* ssl_context);
        virtual ~TlsServerActiveSocket();

        TlsServerActiveSocket(TlsServerActiveSocket&& other) noexcept
            : ServerActiveSocket(std::move(other)), ssl(other.ssl), _io_want(other._io_want)
        {
            other.ssl = nullptr;   // only one owner may SSL_free()
        }

        void close() override;
		NetworkResult read(void* buffer, size_t size) override;
        NetworkResult write(const void* buffer, size_t size) override;

        bool isTls() const override { return true; }
        TLSHandshakeState handshake() override;
        bool hasBufferedInput() const override;
        TlsWant ioWant() const override { return _io_want; }
    private:
        SSL* ssl {nullptr};
        TlsWant _io_want{ TlsWant::NONE };   // set by read()/write()
    };

    using ServerActiveSocketContainer = SocketContainer<ServerActiveSocket, TlsServerActiveSocket>;

}


#endif // __BN3MONKEY__SERVERACTIVESOCKET__