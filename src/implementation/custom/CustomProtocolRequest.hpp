#if !defined(__BN3MONKEY_CUSTOM_PROTOCOL_REQUEST__)
#define __BN3MONKEY_CUSTOM_PROTOCOL_REQUEST__

#include "../../SecuritySocket.hpp"

#include <cstddef>

namespace Bn3Monkey
{
    // Server-side view over one incoming Custom Protocol message. The
    // stack-allocated Impl is owned by the dispatch path inside
    // ClientConnectionImpl; user code only ever sees the CustomProtocolRequest
    // interface.
    //
    // header / payload point into buffers that ClientConnectionImpl owns for
    // the duration of the process()/processWithoutResponse() call. Phase 5
    // keeps the legacy layout (separate header / payload vectors), so the Impl
    // takes two independent (ptr, length) spans; Phase 6's single-buffer +
    // offset restructure will re-wire the factory without touching this
    // interface.
    class CustomProtocolRequestImpl : public CustomProtocolRequest
    {
    public:
        CustomProtocolRequestImpl(const void* header, size_t header_length,
                                  const void* payload, size_t payload_length);

        const void* header()        const override { return _header; }
        size_t      headerLength()  const override { return _header_length; }
        const void* payload()       const override { return _payload; }
        size_t      payloadLength() const override { return _payload_length; }

    private:
        const void* _header;
        size_t      _header_length;
        const void* _payload;
        size_t      _payload_length;
    };
}

#endif
