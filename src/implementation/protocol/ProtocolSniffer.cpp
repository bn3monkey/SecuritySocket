#include "ProtocolSniffer.hpp"

#include <cstring>

namespace Bn3Monkey
{
    namespace
    {
        // RFC 7231 method tokens. The router only routes a subset, but sniffing
        // recognises the full common set so an unrouted-but-valid verb still
        // classifies as HTTP (the request then 404s through the normal path,
        // rather than being mis-sniffed as Custom binary).
        struct MethodTok { const char* tok; size_t len; };
        const MethodTok kMethods[] = {
            { "GET",     3 },
            { "PUT",     3 },
            { "HEAD",    4 },
            { "POST",    4 },
            { "PATCH",   5 },
            { "TRACE",   5 },
            { "DELETE",  6 },
            { "CONNECT", 7 },
            { "OPTIONS", 7 },
        };
    } // namespace

    Protocol ProtocolSniffer::detect(const char* buf, size_t len)
    {
        if (buf == nullptr || len == 0) return Protocol::NEED_MORE;

        bool prefix_possible = false;

        for (const auto& m : kMethods) {
            const size_t cmp = (len < m.len) ? len : m.len;
            if (std::memcmp(buf, m.tok, cmp) != 0)
                continue;

            if (len > m.len) {
                // Whole token present — the next byte must be the SP separator
                // of the request line for this to be HTTP.
                if (buf[m.len] == ' ')
                    return Protocol::HTTP;
                // Token matched but no space (e.g. "GETX"): not this method.
                // No method is a prefix of another, so no other can match.
            } else {
                // buf is a (possibly whole) prefix of this method but we have
                // not yet seen the separator space — still ambiguous.
                prefix_possible = true;
            }
        }

        return prefix_possible ? Protocol::NEED_MORE : Protocol::CUSTOM;
    }
}
