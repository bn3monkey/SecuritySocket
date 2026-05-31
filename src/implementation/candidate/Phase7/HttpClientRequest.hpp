#if !defined(__BN3MONKEY_HTTP_CLIENT_REQUEST__)
#define __BN3MONKEY_HTTP_CLIENT_REQUEST__

// ── CANDIDATE (Phase 7) ──────────────────────────────────────────────────────
// Pre-staged, NOT wired into the build. Two pieces live here:
//   1. PUBLIC `HttpClientRequest` — on integration, move this class declaration
//      into src/SecuritySocket.hpp (next to HttpClient). It is the container-
//      pattern type from mileston_http.md §2-7.
//   2. INTERNAL `HttpClientRequestImpl` — keep under
//      src/implementation/http/HttpClientRequest.{hpp,cpp}.
// Include paths assume the impl pair lands in src/implementation/http/.
// ─────────────────────────────────────────────────────────────────────────────

#include "../../SecuritySocket.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace Bn3Monkey
{
    // ===== PUBLIC (move to SecuritySocket.hpp on integration) ===================
    //
    // Fluent builder for an outgoing HTTP request. Container pattern: the Impl
    // is placement-new'd into _container so the public header stays free of
    // implementation detail (mirrors Client).
    class SECURITYSOCKET_API HttpClientRequest
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 1024;

        HttpClientRequest();
        ~HttpClientRequest();
        HttpClientRequest(const HttpClientRequest&)            = delete;
        HttpClientRequest& operator=(const HttpClientRequest&) = delete;

        HttpClientRequest& method(const char* m);

        // printf-style single path setter. The compiler validates the format
        // string against the varargs on GCC/Clang. Percent-encoding of inserted
        // values is the caller's job (Url::encode) — see §2-9.
        HttpClientRequest& path(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
            __attribute__((format(printf, 2, 3)))
#endif
            ;

        HttpClientRequest& query (const char* name, const char* value);  // auto-encode
        HttpClientRequest& header(const char* name, const char* value);
        HttpClientRequest& body  (const void* data, size_t len);
        HttpClientRequest& json  (const char* json_str);

    private:
        // HttpClient reaches the Impl through the container to serialize.
        friend class HttpClient;
        alignas(std::max_align_t) char _container[IMPLEMENTATION_SIZE]{ 0 };
    };
    // ===========================================================================


    // ===== INTERNAL (stays under src/implementation/http/) ======================
    //
    // Accumulates request components as growable strings/bytes (heap-backed, so
    // the object easily fits the 1 KB container) and flattens them into wire
    // bytes on demand. The Host header is injected by the client at serialize
    // time because only the client knows the configured authority.
    class HttpClientRequestImpl
    {
    public:
        HttpClientRequestImpl();

        void setMethod(const char* m);
        void setPathV (const char* fmt, va_list ap);
        void addQuery (const char* name, const char* value);   // percent-encoded
        void addHeader(const char* name, const char* value);
        void setBody  (const void* data, size_t len);
        void setJson  (const char* json_str);

        // Flatten into 'out' as a complete HTTP/1.1 request:
        //   "<METHOD> <path>[?query] HTTP/1.1\r\nHost: <host>\r\n<headers>
        //    Content-Length: <n>\r\n\r\n<body>"
        // 'host' is the authority for the Host header (e.g. "127.0.0.1:8080").
        void serialize(const char* host, std::vector<char>& out) const;

    private:
        std::string       _method{ "GET" };
        std::string       _path{ "/" };
        std::string       _query;       // "k1=v1&k2=v2" (already encoded), no '?'
        std::string       _headers;     // "Name: Value\r\n" blocks, user order
        std::vector<char> _body;
        bool              _has_content_type{ false };
    };
    // ===========================================================================
}

#endif // __BN3MONKEY_HTTP_CLIENT_REQUEST__
