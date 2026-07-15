#if !defined(__BN3MONKEY_HTTP_RESPONSE__)
#define __BN3MONKEY_HTTP_RESPONSE__

#include "../../SecuritySocket.hpp"

#include <cstddef>
#include <vector>

namespace Bn3Monkey
{
    // Server-side builder for an HTTP response.
    //
    // Build-then-serialize: status() / header() / body() / json() accumulate
    // state in inline storage; serialize() flattens them into a single
    // HTTP/1.1 message that Phase 6 dispatch will hand to the write side of
    // the socket.
    //
    // Body lifetime: body(data, size) borrows — the bytes must remain valid
    // until the next serialize() call. The typical FAST dispatch path makes
    // this trivially true (handler returns → serialize() runs immediately on
    // the same thread), but SLOW dispatch needs the handler to either point
    // body at owned memory (member of the handler) or copy into a scratch
    // buffer Phase 6 supplies.
    class SECURITYSOCKET_API HttpResponseImpl : public HttpResponse
    {
    public:
        HttpResponseImpl();

        HttpResponse& status(int code)                            override;
        HttpResponse& header(const char* name, const char* value) override;
        HttpResponse& body  (const void* data, size_t size)       override;
        HttpResponse& bodyCopy(const void* data, size_t size)     override;
        HttpResponse& json  (const char* json_str)                override;

        // Flatten accumulated state into 'out' as an HTTP/1.1 message.
        // Returns bytes written, or 0 on overflow (output buffer too small).
        // 'out' must point to capacity bytes of writable memory.
        size_t serialize(char* out, size_t capacity) const;

        // Accessors used by tests + by Phase 6 dispatch for things like
        // deciding whether to keep-alive based on the resolved status.
        int  statusCode()                  const { return _status; }
        size_t headerCount()               const { return _header_count; }
        size_t bodySize()                  const { return _body_size; }
        const void* bodyData()             const { return _body; }
        // Returns reason phrase for a status code. "OK" for 200 etc.; for
        // unmapped codes returns the generic "" so the serialized status line
        // ends at the code (still RFC-valid — reason phrase is optional).
        static const char* reasonPhrase(int code);

        // Inline-storage sizing.
        static constexpr size_t MAX_HEADERS       = 16;
        static constexpr size_t HEADER_NAME_CAP   = 64;
        static constexpr size_t HEADER_VALUE_CAP  = 256;

    private:
        struct H {
            char name [HEADER_NAME_CAP];
            char value[HEADER_VALUE_CAP];
        };

        int    _status = 200;
        H      _headers[MAX_HEADERS];
        size_t _header_count = 0;

        const void* _body      = nullptr;
        size_t      _body_size = 0;

        // Backing store for bodyCopy(). _body points into this when the response
        // owns its body; empty when body() borrowed instead. Lives as long as the
        // HttpResponseImpl, which spans serialize(), so the pointer never dangles.
        std::vector<char> _owned_body;
    };
}

#endif
