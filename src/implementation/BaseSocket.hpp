#if !defined(__BN3MONKEY_BASESOCKET__)
#define __BN3MONKEY_BASESOCKET__

#include <cstdint>
#include <type_traits>

namespace Bn3Monkey
{
    class BaseSocket
    {
    public:
        inline int descriptor() { return _socket; }
        inline SocketResult valid() { return _result; }
    
    protected:
        SocketResult _result;
        int32_t _socket;
    };

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
            _is_initialized = true;
        }

        SocketContainer(const SocketContainer& container) {
            _is_initialized = container._is_initialized;
            memcpy(buffer, container.buffer, size);
        }

        ~SocketContainer() {
            auto* sock = get();
            if (sock != nullptr) {
                sock->~PlainSocket();
            }
        }

        inline PlainSocket* get() {
            if (!_is_initialized)
                return nullptr;
            return reinterpret_cast<PlainSocket*>(buffer);
        }

    private:
        bool _is_initialized{ false };
        static constexpr size_t size = sizeof(PlainSocket) > sizeof(TLSSocket) ? sizeof(PlainSocket) : sizeof(TLSSocket);
        static_assert(sizeof(PlainSocket) <= 64, "");
        static_assert(sizeof(TLSSocket) <= 64, "");
        char buffer[size]{ 0 };
    };
}

#endif // __BN3MONKEY_BASESOCKET__