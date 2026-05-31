#include "HttpClientRequest.hpp"

#include "Url.hpp"          // query auto-encode (§2-9)

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

namespace Bn3Monkey
{
    namespace
    {
        // Portable case-insensitive full-string compare (MSVC has no strcasecmp).
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
    }

    // ── PUBLIC container shims ──────────────────────────────────────────────

    HttpClientRequest::HttpClientRequest()
    {
        static_assert(sizeof(HttpClientRequestImpl) <= IMPLEMENTATION_SIZE,
                      "container too small — increase IMPLEMENTATION_SIZE");
        new (_container) HttpClientRequestImpl();
    }

    HttpClientRequest::~HttpClientRequest()
    {
        reinterpret_cast<HttpClientRequestImpl*>(_container)->~HttpClientRequestImpl();
    }

    HttpClientRequest& HttpClientRequest::method(const char* m)
    {
        reinterpret_cast<HttpClientRequestImpl*>(_container)->setMethod(m);
        return *this;
    }

    HttpClientRequest& HttpClientRequest::path(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        reinterpret_cast<HttpClientRequestImpl*>(_container)->setPathV(fmt, ap);
        va_end(ap);
        return *this;
    }

    HttpClientRequest& HttpClientRequest::query(const char* name, const char* value)
    {
        reinterpret_cast<HttpClientRequestImpl*>(_container)->addQuery(name, value);
        return *this;
    }

    HttpClientRequest& HttpClientRequest::header(const char* name, const char* value)
    {
        reinterpret_cast<HttpClientRequestImpl*>(_container)->addHeader(name, value);
        return *this;
    }

    HttpClientRequest& HttpClientRequest::body(const void* data, size_t len)
    {
        reinterpret_cast<HttpClientRequestImpl*>(_container)->setBody(data, len);
        return *this;
    }

    HttpClientRequest& HttpClientRequest::json(const char* json_str)
    {
        reinterpret_cast<HttpClientRequestImpl*>(_container)->setJson(json_str);
        return *this;
    }

    // ── Impl ────────────────────────────────────────────────────────────────

    HttpClientRequestImpl::HttpClientRequestImpl() = default;

    void HttpClientRequestImpl::setMethod(const char* m)
    {
        if (m) _method = m;
    }

    void HttpClientRequestImpl::setPathV(const char* fmt, va_list ap)
    {
        if (!fmt) return;
        // Two-pass vsnprintf: size, then fill.
        va_list ap2;
        va_copy(ap2, ap);
        const int n = std::vsnprintf(nullptr, 0, fmt, ap2);
        va_end(ap2);
        if (n < 0) return;
        _path.resize(static_cast<size_t>(n) + 1);
        std::vsnprintf(&_path[0], _path.size(), fmt, ap);
        _path.resize(static_cast<size_t>(n));   // drop the trailing NUL
    }

    void HttpClientRequestImpl::addQuery(const char* name, const char* value)
    {
        if (!name) return;
        if (!_query.empty()) _query.push_back('&');
        _query += Url::encode(name);
        _query.push_back('=');
        if (value) _query += Url::encode(value);
    }

    void HttpClientRequestImpl::addHeader(const char* name, const char* value)
    {
        if (!name || !value) return;
        if (ciEqual(name, "Content-Type")) {
            _has_content_type = true;
        }
        _headers += name;
        _headers += ": ";
        _headers += value;
        _headers += "\r\n";
    }

    void HttpClientRequestImpl::setBody(const void* data, size_t len)
    {
        _body.assign(static_cast<const char*>(data),
                     static_cast<const char*>(data) + len);
    }

    void HttpClientRequestImpl::setJson(const char* json_str)
    {
        if (json_str) {
            const size_t n = std::strlen(json_str);
            _body.assign(json_str, json_str + n);
        } else {
            _body.clear();
        }
        if (!_has_content_type) addHeader("Content-Type", "application/json");
    }

    void HttpClientRequestImpl::serialize(const char* host, std::vector<char>& out) const
    {
        out.clear();
        auto put = [&out](const char* s, size_t n) {
            out.insert(out.end(), s, s + n);
        };
        auto puts = [&put](const char* s) { put(s, std::strlen(s)); };

        // Request line.
        puts(_method.c_str());
        put(" ", 1);
        puts(_path.c_str());
        if (!_query.empty()) { put("?", 1); puts(_query.c_str()); }
        puts(" HTTP/1.1\r\n");

        // Host (mandatory in HTTP/1.1).
        puts("Host: ");
        puts(host ? host : "");
        puts("\r\n");

        // User headers, in declaration order.
        if (!_headers.empty()) puts(_headers.c_str());

        // Content-Length (always present; 0 for bodyless requests).
        char cl[48];
        const int n = std::snprintf(cl, sizeof(cl), "Content-Length: %zu\r\n", _body.size());
        if (n > 0) put(cl, static_cast<size_t>(n));

        // Header/body separator + body.
        puts("\r\n");
        if (!_body.empty()) put(_body.data(), _body.size());
    }
}
