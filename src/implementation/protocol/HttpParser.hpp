#if !defined(__BN3MONKEY_HTTP_PARSER__)
#define __BN3MONKEY_HTTP_PARSER__

#include "../../SecuritySocket.hpp"

#include "picohttpparser.h"

#include <cstddef>

namespace Bn3Monkey
{
    // Stateless HTTP/1.1 request helpers used by ClientConnectionImpl's HTTP
    // states (mileston_http.md §2-8, state-machine.html §6-2).
    //
    // Wraps the vendored picohttpparser for incremental header parsing and
    // adds the request-level inspection the dispatch flow needs: Content-Length,
    // keep-alive resolution, WebSocket Upgrade detection, and the RFC 6455
    // Sec-WebSocket-Accept computation (SHA1 + base64, implemented locally so
    // the HTTP path stays independent of SECURITYSOCKET_USING_TLS / OpenSSL).
    //
    // Everything is static + non-destructive: ParsedRequest holds slices into
    // the caller's buffer (never NUL-terminated here), so the same buffer can
    // be re-parsed as more bytes arrive. The connection layer does any in-place
    // tokenisation it needs separately.
    class SECURITYSOCKET_API HttpParser
    {
    public:
        enum class ParseStatus {
            INCOMPLETE,   // headers not fully received yet — recv more
            OK,           // full header block parsed
            MALFORMED,    // unparseable request line / headers → 400
        };

        // Upper bound on headers captured from one request. Excess headers
        // beyond this make picohttpparser report the request as malformed
        // (its num_headers cap), which the dispatch flow turns into a 400.
        static constexpr size_t MAX_HEADERS = 64;

        struct ParsedRequest
        {
            ParseStatus status = ParseStatus::INCOMPLETE;

            const char* method     = nullptr;
            size_t      method_len = 0;
            const char* path       = nullptr;
            size_t      path_len   = 0;
            int         minor_version = 0;   // 0 => HTTP/1.0, 1 => HTTP/1.1

            struct phr_header headers[MAX_HEADERS];
            size_t            num_headers = 0;

            // Bytes consumed by the request line + headers (incl. the final
            // CRLF CRLF). The body, if any, starts here.
            size_t header_end = 0;
        };

        // Incrementally parse the header block at buf[0..len). last_len is the
        // buffer length at the previous call (picohttpparser uses it to skip
        // re-scanning already-seen bytes); 0 is always safe.
        static ParsedRequest parse(const char* buf, size_t len, size_t last_len = 0);

        // Case-insensitive header lookup. Returns nullptr if absent.
        static const struct phr_header* findHeader(const ParsedRequest& req,
                                                   const char* name);

        // Content-Length value, or -1 if the header is absent or non-numeric.
        static long contentLength(const ParsedRequest& req);

        // RFC 6455: Upgrade: websocket AND Connection: Upgrade (both
        // case-insensitive, Connection compared token-wise).
        static bool isWebSocketUpgrade(const ParsedRequest& req);

        // HTTP/1.1 defaults to keep-alive unless "Connection: close";
        // HTTP/1.0 defaults to close unless "Connection: keep-alive".
        static bool keepAlive(const ParsedRequest& req);

        // Compute the Sec-WebSocket-Accept header value from the request's
        // Sec-WebSocket-Key. Writes a NUL-terminated 28-char base64 string to
        // out (needs >= 29 bytes). Returns false if the key header is missing
        // or out is too small.
        static bool computeAccept(const ParsedRequest& req,
                                  char* out, size_t out_cap);
        // Same, from an explicit key slice — used by unit tests and by the
        // ParsedRequest overload above.
        static bool computeAccept(const char* key, size_t key_len,
                                  char* out, size_t out_cap);

        // Serialize a "101 Switching Protocols" handshake response carrying the
        // given accept value. Returns bytes written, or 0 on overflow.
        static size_t serializeHandshake(const char* accept,
                                         char* out, size_t out_cap);

        // Length of a Sec-WebSocket-Accept value (28 base64 chars) + NUL.
        static constexpr size_t ACCEPT_BUF_SIZE = 29;
    };
}

#endif // __BN3MONKEY_HTTP_PARSER__
