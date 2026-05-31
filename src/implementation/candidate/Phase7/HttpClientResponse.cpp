#include "HttpClientResponse.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <new>

namespace Bn3Monkey
{
    namespace
    {
        bool ciEqual(const char* a, const char* b)
        {
            if (!a || !b) return false;
            while (*a && *b) {
                if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
                    return false;
                ++a; ++b;
            }
            return *a == '\0' && *b == '\0';
        }

        // Trim leading OWS (space/tab) — header values may be padded after ':'.
        const char* skipOws(const char* p, const char* end)
        {
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            return p;
        }
    }

    // ── PUBLIC container shims ──────────────────────────────────────────────

    HttpClientResponse::HttpClientResponse()
    {
        static_assert(sizeof(HttpClientResponseImpl) <= IMPLEMENTATION_SIZE,
                      "container too small — increase IMPLEMENTATION_SIZE");
        new (_container) HttpClientResponseImpl();
    }

    HttpClientResponse::~HttpClientResponse()
    {
        reinterpret_cast<HttpClientResponseImpl*>(_container)->~HttpClientResponseImpl();
    }

    HttpClientResponse::HttpClientResponse(HttpClientResponse&& other) noexcept
    {
        auto* src = reinterpret_cast<HttpClientResponseImpl*>(other._container);
        new (_container) HttpClientResponseImpl(std::move(*src));
    }

    HttpClientResponse& HttpClientResponse::operator=(HttpClientResponse&& other) noexcept
    {
        if (this != &other) {
            auto* self = reinterpret_cast<HttpClientResponseImpl*>(_container);
            auto* src  = reinterpret_cast<HttpClientResponseImpl*>(other._container);
            *self = std::move(*src);
        }
        return *this;
    }

    NetworkResultCode HttpClientResponse::resultCode() const
    {
        return reinterpret_cast<const HttpClientResponseImpl*>(_container)->resultCode();
    }
    int HttpClientResponse::status() const
    {
        return reinterpret_cast<const HttpClientResponseImpl*>(_container)->status();
    }
    const char* HttpClientResponse::header(const char* name) const
    {
        return reinterpret_cast<const HttpClientResponseImpl*>(_container)->header(name);
    }
    const void* HttpClientResponse::body() const
    {
        return reinterpret_cast<const HttpClientResponseImpl*>(_container)->body();
    }
    size_t HttpClientResponse::bodySize() const
    {
        return reinterpret_cast<const HttpClientResponseImpl*>(_container)->bodySize();
    }

    // ── Impl ────────────────────────────────────────────────────────────────

    const char* HttpClientResponseImpl::header(const char* name) const
    {
        if (!name) return nullptr;
        for (const auto& h : _headers) {
            if (ciEqual(h.first.c_str(), name)) return h.second.c_str();
        }
        return nullptr;
    }

    bool HttpClientResponseImpl::parse(const char* data, size_t len)
    {
        if (!data || len == 0) return false;

        // Locate the header terminator.
        const char* end = data + len;
        const char* hdr_term = nullptr;
        for (const char* p = data; p + 3 < end; ++p) {
            if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                hdr_term = p;   // points at the first CR of CRLFCRLF
                break;
            }
        }
        if (!hdr_term) return false;   // header block not complete
        const size_t header_end = static_cast<size_t>(hdr_term - data) + 4;

        // Status line: "HTTP/1.1 <code> <reason>\r\n".
        const char* line_end = data;
        while (line_end + 1 < end && !(line_end[0] == '\r' && line_end[1] == '\n')) ++line_end;
        {
            const char* sp = data;
            while (sp < line_end && *sp != ' ') ++sp;     // after "HTTP/1.1"
            sp = skipOws(sp, line_end);
            _status = static_cast<int>(std::strtol(sp, nullptr, 10));
        }

        // Header lines until the blank line.
        const char* p = (line_end + 2 <= end) ? line_end + 2 : line_end;
        while (p < data + header_end) {
            const char* le = p;
            while (le + 1 < end && !(le[0] == '\r' && le[1] == '\n')) ++le;
            if (le == p) break;   // blank line: end of headers

            const char* colon = p;
            while (colon < le && *colon != ':') ++colon;
            if (colon < le) {
                std::string name(p, static_cast<size_t>(colon - p));
                const char* vp = skipOws(colon + 1, le);
                std::string value(vp, static_cast<size_t>(le - vp));
                _headers.emplace_back(std::move(name), std::move(value));
            }
            p = le + 2;
        }

        // Body by Content-Length (chunked is out of scope, §1-2).
        size_t body_len = 0;
        if (const char* cl = header("Content-Length")) {
            const long n = std::strtol(cl, nullptr, 10);
            if (n > 0) body_len = static_cast<size_t>(n);
        }
        const size_t avail_body = (len > header_end) ? (len - header_end) : 0;
        if (body_len > avail_body) body_len = avail_body;   // truncated read
        _body.assign(data + header_end, data + header_end + body_len);

        _consumed = header_end + body_len;
        return true;
    }
}
