#if !defined(__BN3MONKEY_HTTP_URL__)
#define __BN3MONKEY_HTTP_URL__

#include "../../SecuritySocket.hpp"

#include <string>

namespace Bn3Monkey
{
    // RFC 3986 percent-encoding helper.
    //
    // Unreserved set (A-Z / a-z / 0-9 / '-' / '_' / '.' / '~') passes through;
    // every other byte (including high-bit bytes from UTF-8) becomes %XX.
    // decode() is the strict inverse — bad encodings return "". tryDecode()
    // is the no-throw variant the server-side path/query auto-decoders use,
    // so a malformed request becomes a 4xx instead of a silent empty value.
    //
    // Phase 4: only the static helpers are needed (HttpClientRequest::query
    // auto-encodes outgoing, HttpRequestImpl::pathParam/query auto-decodes
    // incoming). No instance state.
    class SECURITYSOCKET_API Url
    {
    public:
        static std::string encode(const char* input);
        static std::string encode(const char* input, size_t len);
        static std::string encode(const std::string& input);

        // Returns "" on malformed input (orphan '%', non-hex digit after '%').
        static std::string decode(const char* input);
        static std::string decode(const char* input, size_t len);
        static std::string decode(const std::string& input);

        // Same as decode() but distinguishes "" (valid empty input) from
        // "malformed" via the bool return.
        static bool tryDecode(const char* input, size_t len, std::string& out);
    };
}

#endif
