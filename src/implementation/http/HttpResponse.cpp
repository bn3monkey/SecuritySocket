#include "HttpResponse.hpp"

#include <cstdio>
#include <cstring>

namespace Bn3Monkey
{
    namespace
    {
        // Same fixed-capacity copy used by HttpRequestImpl. Returns false if
        // src + NUL doesn't fit; in that case dst is left untouched.
        bool copyFixed(char* dst, size_t cap, const char* src)
        {
            if (!src) {
                if (cap == 0) return false;
                dst[0] = '\0';
                return true;
            }
            const size_t n = std::strlen(src);
            if (n + 1 > cap) return false;
            std::memcpy(dst, src, n);
            dst[n] = '\0';
            return true;
        }

        bool appendBytes(char* out, size_t cap, size_t& off,
                         const void* src, size_t n)
        {
            if (off + n > cap) return false;
            std::memcpy(out + off, src, n);
            off += n;
            return true;
        }

        bool appendCString(char* out, size_t cap, size_t& off, const char* s)
        {
            return appendBytes(out, cap, off, s, std::strlen(s));
        }
    }

    HttpResponseImpl::HttpResponseImpl() = default;

    HttpResponse& HttpResponseImpl::status(int code)
    {
        _status = code;
        return *this;
    }

    HttpResponse& HttpResponseImpl::header(const char* name, const char* value)
    {
        if (!name || !value) return *this;
        if (_header_count >= MAX_HEADERS) return *this;
        H& h = _headers[_header_count];
        // Reject overlong header (silently — Phase 6 dispatch shouldn't have
        // generated these). Strict alternatives would return *this without
        // incrementing, which is what we do; truncating would forge headers.
        if (!copyFixed(h.name,  HEADER_NAME_CAP,  name))  return *this;
        if (!copyFixed(h.value, HEADER_VALUE_CAP, value)) return *this;
        ++_header_count;
        return *this;
    }

    HttpResponse& HttpResponseImpl::body(const void* data, size_t size)
    {
        _body      = data;
        _body_size = size;
        return *this;
    }

    HttpResponse& HttpResponseImpl::json(const char* json_str)
    {
        // Convenience: body + Content-Type header in one call. We always
        // append the header — a duplicate Content-Type is the caller's
        // problem (they explicitly set one before json()), and that's the
        // simpler contract than searching for an existing header here.
        if (!json_str) {
            _body = nullptr;
            _body_size = 0;
        } else {
            _body = json_str;
            _body_size = std::strlen(json_str);
        }
        header("Content-Type", "application/json");
        return *this;
    }

    const char* HttpResponseImpl::reasonPhrase(int code)
    {
        // Codes the framework emits itself (or that handlers are likely to
        // produce). Anything not listed serialises with an empty phrase —
        // RFC 7230 §3.1.2 makes the phrase optional, so this is safe.
        switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 505: return "HTTP Version Not Supported";
        default:  return "";
        }
    }

    size_t HttpResponseImpl::serialize(char* out, size_t cap) const
    {
        if (!out || cap == 0) return 0;
        size_t off = 0;

        // Status line: "HTTP/1.1 <code> <reason>\r\n"
        char status_line[96];
        const char* reason = reasonPhrase(_status);
        int n;
        if (reason && reason[0] != '\0') {
            n = std::snprintf(status_line, sizeof(status_line),
                              "HTTP/1.1 %d %s\r\n", _status, reason);
        } else {
            n = std::snprintf(status_line, sizeof(status_line),
                              "HTTP/1.1 %d\r\n", _status);
        }
        if (n <= 0) return 0;
        if (!appendBytes(out, cap, off, status_line, static_cast<size_t>(n))) return 0;

        // User-supplied headers, in declaration order.
        for (size_t i = 0; i < _header_count; ++i) {
            if (!appendCString(out, cap, off, _headers[i].name))  return 0;
            if (!appendBytes  (out, cap, off, ": ", 2))           return 0;
            if (!appendCString(out, cap, off, _headers[i].value)) return 0;
            if (!appendBytes  (out, cap, off, "\r\n", 2))         return 0;
        }

        // Framework-emitted Content-Length. Phase 6 also adds Connection
        // (keep-alive vs close) — that's a per-connection decision, not the
        // response builder's job.
        char cl_line[64];
        n = std::snprintf(cl_line, sizeof(cl_line),
                          "Content-Length: %zu\r\n", _body_size);
        if (n <= 0) return 0;
        if (!appendBytes(out, cap, off, cl_line, static_cast<size_t>(n))) return 0;

        // Headers/body separator.
        if (!appendBytes(out, cap, off, "\r\n", 2)) return 0;

        // Body bytes.
        if (_body && _body_size > 0) {
            if (!appendBytes(out, cap, off, _body, _body_size)) return 0;
        }

        return off;
    }
}
