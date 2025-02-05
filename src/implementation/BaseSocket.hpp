#if !defined(__BN3MONKEY_BASESOCKET__)
#define __BN3MONKEY_BASESOCKET__

#include <cstdint>

namespace Bn3Monkey
{
    class BaseSocket
    {
    public:
        inline int descriptor() { return _socket; }
    
    protected:
        int32_t _socket;
    };
}

#endif // __BN3MONKEY_BASESOCKET__