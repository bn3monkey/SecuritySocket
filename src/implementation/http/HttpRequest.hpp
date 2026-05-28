#if !defined(__BN3MONKEY_HTTP_REQUEST__)
#define __BN3MONKEY_HTTP_REQUEST__

#include "../../SecuritySocket.hpp"

#include <cstddef>

namespace Bn3Monkey
{
    // Index over the headers of a parsed HTTP request.
    //
    // Each entry's name and value point into the request buffer that the
    // server flow (Phase 6) already owns. By that point the server has
    // written a '\0' over the CR that follows each header value, so the
    // pointers are NUL-terminated even though they're just slices of the
    // contiguous buffer — that's why the public HttpRequest::header()
    // contract can return a bare `const char*` with no length companion.
    //
    // Fixed inline capacity (no heap). Excess headers past MAX_HEADERS are
    // dropped at build time, which matches picohttpparser's own behaviour
    // (it caps via the num_headers it was passed).
    struct HeaderEntry
    {
        const char* name  = nullptr;   // NUL-terminated, lower- or original-case
        const char* value = nullptr;   // NUL-terminated
    };

    class SECURITYSOCKET_API HeaderIndex
    {
    public:
        static constexpr size_t MAX_HEADERS = 32;

        HeaderIndex() = default;

        void clear() { _count = 0; }

        // Returns false if the index is already at MAX_HEADERS.
        bool add(const char* name, const char* value);

        // Case-insensitive lookup. Returns nullptr if no matching header.
        // RFC 7230 §3.2 says field-name is case-insensitive.
        const char* find(const char* name) const;

        size_t            count() const             { return _count; }
        const HeaderEntry& at(size_t i) const       { return _entries[i]; }

    private:
        HeaderEntry _entries[MAX_HEADERS];
        size_t      _count = 0;
    };

    // Server-side view of an HTTP request. The stack-allocated Impl is owned
    // by the dispatch path inside ClientConnectionImpl (Phase 6 wiring); user
    // code only sees the HttpRequest interface.
    //
    // pathParam values are written by the router after a match() succeeds
    // (Phase 6 dispatch invokes setPathParam for each PathParamView returned).
    // Query params are parsed in the constructor — splitting on '?', then
    // '&'-separated key=value pairs, with each side run through Url::decode.
    class SECURITYSOCKET_API HttpRequestImpl : public HttpRequest
    {
    public:
        // path may include "?query" — the ctor splits it into _path_buf (the
        // bare path) and parses the query string into _query_params.
        HttpRequestImpl(const char* method,
                        const char* path,
                        const HeaderIndex& headers,
                        const void* body,
                        size_t body_size);

        const char* method()                       const override { return _method; }
        const char* path()                         const override { return _path; }
        const char* header(const char* name)       const override;
        const char* pathParam(const char* name)    const override;
        const char* query(const char* name)        const override;
        const void* body()                         const override { return _body; }
        size_t      bodySize()                     const override { return _body_size; }

        // Bind a path-param decoded value. Returns false if the name is too
        // long for the inline slot (>= 32 bytes) or the param table is full.
        // The router calls this once per PathParamView produced by match().
        bool setPathParam(const char* name, const char* value);

        // Inline-storage sizing — also used by tests to assert "no truncation"
        // bounds on chosen inputs.
        static constexpr size_t PARAM_NAME_CAP   = 32;
        static constexpr size_t PARAM_VALUE_CAP  = 128;
        static constexpr size_t MAX_PATH_PARAMS  = 8;
        static constexpr size_t MAX_QUERY_PARAMS = 16;
        static constexpr size_t PATH_BUF_CAP     = 512;

    private:
        struct Param {
            char name [PARAM_NAME_CAP];
            char value[PARAM_VALUE_CAP];
        };

        // Split the incoming 'path' at the first '?' and copy the bare path
        // into _path_buf. Body of the query string (everything after '?') is
        // then chopped into '&'-separated pairs and decoded into _query_params.
        void parseQueryString(const char* query_start, size_t query_len);

        const char* _method;
        char        _path_buf[PATH_BUF_CAP];
        const char* _path;            // points into _path_buf
        const HeaderIndex* _headers;
        const void* _body;
        size_t      _body_size;

        Param  _path_params[MAX_PATH_PARAMS];
        size_t _path_param_count = 0;

        Param  _query_params[MAX_QUERY_PARAMS];
        size_t _query_param_count = 0;
    };
}

#endif
