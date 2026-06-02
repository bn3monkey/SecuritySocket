#include "HttpClientResponse.hpp"

#include <cctype>
#include <cstring>
#include <new>
#include <utility>

using namespace Bn3Monkey;

namespace
{
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

// ── HttpClientResponseImpl ─────────────────────────────────────────────────

void HttpClientResponseImpl::addHeader(const char* name, size_t name_len,
                                       const char* value, size_t value_len)
{
    _headers.emplace_back(std::string(name, name_len),
                          std::string(value, value_len));
}

void HttpClientResponseImpl::setBody(const void* data, size_t len)
{
    const char* p = static_cast<const char*>(data);
    _body.assign(p, p + len);
}

const char* HttpClientResponseImpl::header(const char* name) const
{
    for (const auto& h : _headers)
        if (ciEqual(h.first.c_str(), name))
            return h.second.c_str();
    return nullptr;
}

const void* HttpClientResponseImpl::body() const
{
    return _body.empty() ? nullptr : _body.data();
}

// ── HttpClientResponse (public facade) ─────────────────────────────────────

static HttpClientResponseImpl* impl_of(char* c)
{
    return reinterpret_cast<HttpClientResponseImpl*>(c);
}
static const HttpClientResponseImpl* impl_of(const char* c)
{
    return reinterpret_cast<const HttpClientResponseImpl*>(c);
}

Bn3Monkey::HttpClientResponse::HttpClientResponse()
{
    static_assert(sizeof(HttpClientResponseImpl) <= IMPLEMENTATION_SIZE,
                  "HttpClientResponseImpl no longer fits in _container; "
                  "raise HttpClientResponse::IMPLEMENTATION_SIZE");
    static_assert(alignof(HttpClientResponseImpl) <= alignof(double),
                  "HttpClientResponseImpl over-aligned for _container");
    new (_container) HttpClientResponseImpl();
}

Bn3Monkey::HttpClientResponse::~HttpClientResponse()
{
    impl_of(_container)->~HttpClientResponseImpl();
}

Bn3Monkey::HttpClientResponse::HttpClientResponse(HttpClientResponse&& other) noexcept
{
    new (_container) HttpClientResponseImpl(std::move(*impl_of(other._container)));
}

HttpClientResponse& Bn3Monkey::HttpClientResponse::operator=(HttpClientResponse&& other) noexcept
{
    if (this != &other)
        *impl_of(_container) = std::move(*impl_of(other._container));
    return *this;
}

NetworkResultCode Bn3Monkey::HttpClientResponse::resultCode() const
{
    return impl_of(_container)->resultCode();
}
int Bn3Monkey::HttpClientResponse::status() const
{
    return impl_of(_container)->status();
}
const char* Bn3Monkey::HttpClientResponse::header(const char* name) const
{
    return impl_of(_container)->header(name);
}
const void* Bn3Monkey::HttpClientResponse::body() const
{
    return impl_of(_container)->body();
}
size_t Bn3Monkey::HttpClientResponse::bodySize() const
{
    return impl_of(_container)->bodySize();
}
