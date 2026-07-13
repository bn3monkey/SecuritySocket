#if !defined(__BN3MONKEY_BASESOCKET__)
#define __BN3MONKEY_BASESOCKET__

#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace Bn3Monkey
{
    class BaseSocket
    {
    public:
        inline int descriptor() { return _socket; }
        inline NetworkResult valid() { return _result; }

    protected:
        NetworkResult _result{};
        int32_t _socket{ 0 };
    };

    // Inline storage for exactly one of two socket types (plain or TLS), chosen
    // at construction. Neither is heap-allocated; both live in `buffer`.
    //
    // OWNERSHIP: move-only, single owner.
    //   Copying was previously a raw memcpy of the buffer, which produced two
    //   containers aliasing the same fd (and, once TLS lands, the same SSL*).
    //   That only stayed safe because every socket destructor was a deliberate
    //   no-op — a landmine for anyone adding cleanup to one. Now the container
    //   moves: the source is destroyed and marked uninitialized, so at most one
    //   live container ever refers to a given fd.
    //
    // RELEASE: still explicit. Destructors run the socket's destructor but do
    //   not close the fd; callers invoke close() at a well-defined point in the
    //   teardown order (ClientConnectionImpl::closeSocket, ClientImpl::close,
    //   RequestServerImpl::close). Unique ownership means moving cleanup into
    //   the destructor is now *possible*, but that reorders teardown across
    //   three servers and is deliberately left as a separate change.
    template <class PlainSocket, class TLSSocket,
                typename = std::enable_if_t<std::is_base_of<BaseSocket, PlainSocket>::value>,
                typename = std::enable_if_t<std::is_base_of<BaseSocket, TLSSocket>::value>
                >
    class SocketContainer
    {
    public:
        SocketContainer() {}

        template<typename ...Args>
        SocketContainer(bool tls, Args&& ...args) {
            if (tls)
            {
                new (buffer) TLSSocket(std::forward<Args>(args)...);
            }
            else {
                new (buffer) PlainSocket(std::forward<Args>(args)...);
            }
            _is_tls = tls;
            _is_initialized = true;
        }

        SocketContainer(const SocketContainer&) = delete;
        SocketContainer& operator=(const SocketContainer&) = delete;

        SocketContainer(SocketContainer&& container) noexcept {
            moveFrom(container);
        }

        SocketContainer& operator=(SocketContainer&& container) noexcept {
            if (this != &container) {
                destroy();
                moveFrom(container);
            }
            return *this;
        }

        ~SocketContainer() {
            destroy();
        }

        inline PlainSocket* get() {
            if (!_is_initialized)
                return nullptr;
            return reinterpret_cast<PlainSocket*>(buffer);
        }

    private:
        // Destroy through the *exact* stored type rather than through
        // PlainSocket*. Besides being correct regardless of whether the base
        // declares a virtual destructor, this silences clang's
        // -Wdelete-non-abstract-non-virtual-dtor, which fired on the old
        // `get()->~PlainSocket()` because PassiveSocket is non-final, has
        // virtual functions, and (until now) had no virtual destructor.
        void destroy() {
            if (!_is_initialized)
                return;
            if (_is_tls)
                reinterpret_cast<TLSSocket*>(buffer)->~TLSSocket();
            else
                reinterpret_cast<PlainSocket*>(buffer)->~PlainSocket();
            _is_initialized = false;
        }

        // Move-construct into our buffer, then destroy the source. The socket
        // types' move constructors null out the source's fd / SSL handles, so
        // the source is inert once this returns.
        void moveFrom(SocketContainer& container) {
            if (!container._is_initialized) {
                _is_initialized = false;
                return;
            }
            if (container._is_tls)
                new (buffer) TLSSocket(std::move(*reinterpret_cast<TLSSocket*>(container.buffer)));
            else
                new (buffer) PlainSocket(std::move(*reinterpret_cast<PlainSocket*>(container.buffer)));
            _is_tls = container._is_tls;
            _is_initialized = true;
            container.destroy();
        }

        static constexpr size_t size = sizeof(PlainSocket) > sizeof(TLSSocket) ? sizeof(PlainSocket) : sizeof(TLSSocket);
        // Both socket types are polymorphic, so the buffer must be at least
        // vptr-aligned. A bare char[] is 1-aligned; keep the buffer first and
        // the flags in the tail padding so no extra padding is introduced.
        static constexpr size_t alignment = alignof(PlainSocket) > alignof(TLSSocket) ? alignof(PlainSocket) : alignof(TLSSocket);
        static_assert(sizeof(PlainSocket) <= 64, "");
        static_assert(sizeof(TLSSocket) <= 64, "");
        alignas(alignment) char buffer[size]{ 0 };

        bool _is_initialized{ false };
        bool _is_tls{ false };
    };
}

#endif // __BN3MONKEY_BASESOCKET__
