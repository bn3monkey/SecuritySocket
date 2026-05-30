#if !defined(__BN3MONKEY_PROTOCOL_SNIFFER__)
#define __BN3MONKEY_PROTOCOL_SNIFFER__

#include "../../SecuritySocket.hpp"

#include <cstddef>
#include <cstdint>

namespace Bn3Monkey
{
    // Single-port protocol detection (mileston_http.md §2-10).
    //
    // The server accepts one connection and, before committing to a protocol
    // group, peeks at the first bytes to tell an HTTP/1.1 request line apart
    // from a Custom Protocol binary header. The discriminator is the HTTP
    // request-line shape: "<METHOD> " — an uppercase method token followed by
    // a space. A Custom header is arbitrary binary and (by the application's
    // contract) does not begin with an HTTP method token + space.
    //
    // detect() is pure content classification. The connection layer applies
    // capability constraints first (a handler that supports only one protocol
    // skips sniffing entirely), so detect() is only consulted for handlers
    // that support BOTH HTTP and Custom.
    enum class Protocol : uint8_t {
        NEED_MORE,   // buffer is a strict prefix of a method token — read more
        HTTP,        // "<METHOD> " confirmed
        CUSTOM,      // cannot be an HTTP request line → treat as Custom binary
    };

    class SECURITYSOCKET_API ProtocolSniffer
    {
    public:
        // Classify the bytes at buf[0..len). Returns NEED_MORE only while the
        // prefix is still ambiguous; once len reaches MAX_SNIFF_BYTES the
        // result is always HTTP or CUSTOM.
        static Protocol detect(const char* buf, size_t len);

        // Longest recognised method ("OPTIONS"/"CONNECT" = 7) + the trailing
        // space = 8. Past this many bytes detection always resolves.
        static constexpr size_t MAX_SNIFF_BYTES = 8;
    };
}

#endif // __BN3MONKEY_PROTOCOL_SNIFFER__
