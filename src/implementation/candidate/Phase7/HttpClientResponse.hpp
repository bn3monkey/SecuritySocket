#if !defined(__BN3MONKEY_HTTP_CLIENT_RESPONSE__)
#define __BN3MONKEY_HTTP_CLIENT_RESPONSE__

// ── CANDIDATE (Phase 7) ──────────────────────────────────────────────────────
// PUBLIC `HttpClientResponse` -> move to src/SecuritySocket.hpp on integration.
// INTERNAL `HttpClientResponseImpl` -> src/implementation/http/.
// Value-returned by HttpClient calls (move-only; the Impl is relocated between
// containers with placement-new). mileston_http.md §2-7.
// ─────────────────────────────────────────────────────────────────────────────

#include "../../SecuritySocket.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Bn3Monkey
{
    // ===== PUBLIC (move to SecuritySocket.hpp on integration) ===================
    class SECURITYSOCKET_API HttpClientResponse
    {
    public:
        static constexpr size_t IMPLEMENTATION_SIZE = 1024;

        HttpClientResponse();
        ~HttpClientResponse();
        HttpClientResponse(const HttpClientResponse&)            = delete;
        HttpClientResponse& operator=(const HttpClientResponse&) = delete;

        // Move so calls can return by value (the Impl is moved between
        // containers via placement-new).
        HttpClientResponse(HttpClientResponse&&) noexcept;
        HttpClientResponse& operator=(HttpClientResponse&&) noexcept;

        NetworkResultCode resultCode() const;          // transport success/failure
        int               status()     const;          // HTTP status code (0 if none)
        const char*       header(const char* name) const;  // nullptr if absent
        const void*       body()     const;
        size_t            bodySize() const;

    private:
        friend class HttpClient;
        alignas(std::max_align_t) char _container[IMPLEMENTATION_SIZE]{ 0 };
    };
    // ===========================================================================


    // ===== INTERNAL (stays under src/implementation/http/) ======================
    class HttpClientResponseImpl
    {
    public:
        HttpClientResponseImpl() = default;
        HttpClientResponseImpl(HttpClientResponseImpl&&) noexcept            = default;
        HttpClientResponseImpl& operator=(HttpClientResponseImpl&&) noexcept = default;

        void setResultCode(NetworkResultCode code) { _result_code = code; }
        NetworkResultCode resultCode() const       { return _result_code; }

        // Parse a full HTTP/1.1 response message (status line + headers + body).
        // Returns false if the header block is malformed; on success the
        // status/headers/body accessors are populated. Body is taken as
        // Content-Length bytes following the header terminator (the only body
        // framing this client supports — chunked is out of scope, §1-2).
        bool parse(const char* data, size_t len);

        int         status() const     { return _status; }
        const char* header(const char* name) const;
        const void* body() const       { return _body.empty() ? nullptr : _body.data(); }
        size_t      bodySize() const   { return _body.size(); }

        // Bytes the parsed message occupied (header_end + body). Lets a
        // keep-alive client know where the next response begins in a buffer
        // holding more than one.
        size_t consumed() const        { return _consumed; }

    private:
        NetworkResultCode _result_code{ NetworkResultCode::UNKNOWN_ERROR };
        int               _status{ 0 };
        std::vector<std::pair<std::string, std::string>> _headers;
        std::vector<char> _body;
        size_t            _consumed{ 0 };
    };
    // ===========================================================================
}

#endif // __BN3MONKEY_HTTP_CLIENT_RESPONSE__
