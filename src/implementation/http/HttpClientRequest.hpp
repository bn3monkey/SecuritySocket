#if !defined(__BN3MONKEY_HTTP_CLIENT_REQUEST__)
#define __BN3MONKEY_HTTP_CLIENT_REQUEST__

#include "../../SecuritySocket.hpp"

#include <cstddef>
#include <cstdarg>
#include <string>
#include <vector>
#include <utility>

namespace Bn3Monkey
{
    // Backing store for HttpClientRequest. Accumulates method/path/query/
    // headers/body, then serialize() flattens everything into one HTTP/1.1
    // request message. Path is stored verbatim (caller pre-encodes); query
    // pairs are percent-encoded as they are added.
    class HttpClientRequestImpl
    {
    public:
        HttpClientRequestImpl();

        void setMethod(const char* m);
        void setPath(const char* fmt, va_list args);
        void setPathRaw(const char* p);   // literal path, no printf interpretation
        void addQuery(const char* name, const char* value);   // auto-encode
        void addHeader(const char* name, const char* value);
        void setBody(const void* data, size_t len);
        void setJson(const char* json_str);

        // Build the full request into 'out'. 'host' fills the Host header when
        // the caller did not set one. keep_alive selects the Connection header.
        // Returns the serialized bytes.
        void serialize(const char* host, bool keep_alive,
                       std::vector<char>& out) const;

        const char*  method() const { return _method; }
        bool         hasBody() const { return !_body.empty(); }

    private:
        bool hasHeader(const char* name) const;

        char _method[16];
        std::string _path;
        std::string _query;   // without leading '?'; pairs joined by '&'
        std::vector<std::pair<std::string, std::string>> _headers;
        std::vector<char> _body;
    };
}

#endif
