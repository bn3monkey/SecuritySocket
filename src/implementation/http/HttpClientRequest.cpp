#include "HttpClientRequest.hpp"
#include "Url.hpp"

#include <cstdio>
#include <cstring>
#include <cctype>

using namespace Bn3Monkey;

namespace
{
    // RFC 7230 §3.2 — header names are case-insensitive (ASCII).
    bool ciEqual(const char* a, const char* b)
    {
        if (!a || !b) return false;
        while (*a && *b) {
            if (std::tolower(static_cast<unsigned char>(*a)) !=
                std::tolower(static_cast<unsigned char>(*b)))
                return false;
            ++a; ++b;
        }
        return *a == 0 && *b == 0;
    }
}

// ── HttpClientRequestImpl ──────────────────────────────────────────────────

HttpClientRequestImpl::HttpClientRequestImpl()
{
    std::memcpy(_method, "GET", 4);   // includes NUL
}

void HttpClientRequestImpl::setMethod(const char* m)
{
    if (!m) return;
    snprintf(_method, sizeof(_method), "%s", m);
}

void HttpClientRequestImpl::setPath(const char* fmt, va_list args)
{
    if (!fmt) return;

    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) return;

    _path.resize(static_cast<size_t>(needed) + 1);
    vsnprintf(&_path[0], _path.size(), fmt, args);
    _path.resize(static_cast<size_t>(needed));   // drop the trailing NUL
}

void HttpClientRequestImpl::setPathRaw(const char* p)
{
    _path = p ? p : "";
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
    _headers.emplace_back(std::string(name), std::string(value));
}

void HttpClientRequestImpl::setBody(const void* data, size_t len)
{
    _body.assign(static_cast<const char*>(data),
                 static_cast<const char*>(data) + len);
}

void HttpClientRequestImpl::setJson(const char* json_str)
{
    if (!json_str) return;
    size_t len = std::strlen(json_str);
    _body.assign(json_str, json_str + len);
    if (!hasHeader("Content-Type"))
        addHeader("Content-Type", "application/json");
}

bool HttpClientRequestImpl::hasHeader(const char* name) const
{
    for (const auto& h : _headers)
        if (ciEqual(h.first.c_str(), name))
            return true;
    return false;
}

static void appendStr(std::vector<char>& out, const char* s, size_t n)
{
    out.insert(out.end(), s, s + n);
}
static void appendCStr(std::vector<char>& out, const char* s)
{
    appendStr(out, s, std::strlen(s));
}

void HttpClientRequestImpl::serialize(const char* host, bool keep_alive,
                                      std::vector<char>& out) const
{
    out.clear();

    // Request line: METHOD SP path[?query] SP HTTP/1.1 CRLF
    appendCStr(out, _method);
    out.push_back(' ');
    if (_path.empty()) out.push_back('/');
    else               appendStr(out, _path.data(), _path.size());
    if (!_query.empty()) {
        out.push_back('?');
        appendStr(out, _query.data(), _query.size());
    }
    appendCStr(out, " HTTP/1.1\r\n");

    // Host (only if the caller didn't supply one).
    if (host && !hasHeader("Host")) {
        appendCStr(out, "Host: ");
        appendCStr(out, host);
        appendCStr(out, "\r\n");
    }

    // Connection.
    if (!hasHeader("Connection")) {
        appendCStr(out, keep_alive ? "Connection: keep-alive\r\n"
                                   : "Connection: close\r\n");
    }

    // Content-Length whenever a body is present (and not user-set).
    if (!_body.empty() && !hasHeader("Content-Length")) {
        char line[64];
        int n = snprintf(line, sizeof(line),
                         "Content-Length: %zu\r\n", _body.size());
        appendStr(out, line, static_cast<size_t>(n));
    }

    // User headers verbatim.
    for (const auto& h : _headers) {
        appendStr(out, h.first.data(), h.first.size());
        appendCStr(out, ": ");
        appendStr(out, h.second.data(), h.second.size());
        appendCStr(out, "\r\n");
    }

    appendCStr(out, "\r\n");

    if (!_body.empty())
        appendStr(out, _body.data(), _body.size());
}

// ── HttpClientRequest (public facade) ──────────────────────────────────────

static HttpClientRequestImpl* impl_of(char* c)
{
    return reinterpret_cast<HttpClientRequestImpl*>(c);
}

Bn3Monkey::HttpClientRequest::HttpClientRequest()
{
    static_assert(sizeof(HttpClientRequestImpl) <= IMPLEMENTATION_SIZE,
                  "HttpClientRequestImpl no longer fits in _container; "
                  "raise HttpClientRequest::IMPLEMENTATION_SIZE");
    static_assert(alignof(HttpClientRequestImpl) <= alignof(double),
                  "HttpClientRequestImpl over-aligned for _container");
    new (_container) HttpClientRequestImpl();
}

Bn3Monkey::HttpClientRequest::~HttpClientRequest()
{
    impl_of(_container)->~HttpClientRequestImpl();
}

HttpClientRequest& Bn3Monkey::HttpClientRequest::method(const char* m)
{
    impl_of(_container)->setMethod(m);
    return *this;
}

HttpClientRequest& Bn3Monkey::HttpClientRequest::path(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    impl_of(_container)->setPath(fmt, args);
    va_end(args);
    return *this;
}

HttpClientRequest& Bn3Monkey::HttpClientRequest::query(const char* name, const char* value)
{
    impl_of(_container)->addQuery(name, value);
    return *this;
}

HttpClientRequest& Bn3Monkey::HttpClientRequest::header(const char* name, const char* value)
{
    impl_of(_container)->addHeader(name, value);
    return *this;
}

HttpClientRequest& Bn3Monkey::HttpClientRequest::body(const void* data, size_t len)
{
    impl_of(_container)->setBody(data, len);
    return *this;
}

HttpClientRequest& Bn3Monkey::HttpClientRequest::json(const char* json_str)
{
    impl_of(_container)->setJson(json_str);
    return *this;
}
