#if !defined(__BN3MONKEY_CUSTOM_PROTOCOL_RESPONSE__)
#define __BN3MONKEY_CUSTOM_PROTOCOL_RESPONSE__

#include "../../SecuritySocket.hpp"

#include <cstddef>

namespace Bn3Monkey
{
    // Server-side builder for one outgoing Custom Protocol response. The
    // handler writes its bytes straight into data() (a view onto the
    // connection's output buffer) and reports the produced length through
    // setLength(), which the Impl forwards into the connection's response-size
    // field. No buffering / copying — the connection's write path consumes the
    // first n bytes directly.
    //
    // Like CustomProtocolRequestImpl, this is stack-built per dispatch and
    // borrows the connection's storage; nothing outlives the process() call.
    class CustomProtocolResponseImpl : public CustomProtocolResponse
    {
    public:
        // out_length receives the value passed to setLength(); it points at the
        // connection's response_size field so the write path picks it up.
        CustomProtocolResponseImpl(void* buffer, size_t capacity, size_t* out_length);

        void*  data()              override { return _buffer; }
        size_t capacity()    const override { return _capacity; }
        void   setLength(size_t n) override { *_out_length = n; }

    private:
        void*   _buffer;
        size_t  _capacity;
        size_t* _out_length;
    };
}

#endif
