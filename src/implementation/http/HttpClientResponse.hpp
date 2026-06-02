#if !defined(__BN3MONKEY_HTTP_CLIENT_RESPONSE__)
#define __BN3MONKEY_HTTP_CLIENT_RESPONSE__

#include "../../SecuritySocket.hpp"

#include <cstddef>
#include <string>
#include <vector>
#include <utility>

namespace Bn3Monkey
{
    // Backing store for HttpClientResponse. Owns copies of the status, headers
    // and body so the public response can outlive the connection's receive
    // buffer and be returned by value (move-only). HttpClient fills it via the
    // setters and then moves it into the public container.
    class HttpClientResponseImpl
    {
    public:
        HttpClientResponseImpl() = default;
        HttpClientResponseImpl(HttpClientResponseImpl&&) noexcept            = default;
        HttpClientResponseImpl& operator=(HttpClientResponseImpl&&) noexcept = default;
        HttpClientResponseImpl(const HttpClientResponseImpl&)            = delete;
        HttpClientResponseImpl& operator=(const HttpClientResponseImpl&) = delete;

        void setResultCode(NetworkResultCode code) { _code = code; }
        void setStatus(int status)                 { _status = status; }
        void addHeader(const char* name, size_t name_len,
                       const char* value, size_t value_len);
        void setBody(const void* data, size_t len);

        NetworkResultCode resultCode()             const { return _code; }
        int               status()                 const { return _status; }
        const char*       header(const char* name) const;
        const void*       body()                   const;
        size_t            bodySize()               const { return _body.size(); }

    private:
        NetworkResultCode _code   = NetworkResultCode::UNKNOWN_ERROR;
        int               _status = 0;
        std::vector<std::pair<std::string, std::string>> _headers;
        std::vector<char> _body;
    };
}

#endif
