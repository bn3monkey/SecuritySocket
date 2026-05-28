#include "Url.hpp"

#include <cstring>

namespace Bn3Monkey
{
    namespace
    {
        inline bool isUnreserved(unsigned char c)
        {
            // RFC 3986 §2.3 — unreserved = ALPHA / DIGIT / "-" / "_" / "." / "~"
            return (c >= 'A' && c <= 'Z')
                || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9')
                || c == '-' || c == '_' || c == '.' || c == '~';
        }

        inline int hexValue(unsigned char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            return -1;
        }

        inline char hexDigit(int n)
        {
            return static_cast<char>(n < 10 ? '0' + n : 'A' + (n - 10));
        }
    }

    std::string Url::encode(const char* input)
    {
        return input ? encode(input, std::strlen(input)) : std::string();
    }

    std::string Url::encode(const std::string& input)
    {
        return encode(input.data(), input.size());
    }

    std::string Url::encode(const char* input, size_t len)
    {
        std::string out;
        if (!input || len == 0) return out;
        out.reserve(len);  // ≥ input length; grows on encoded bytes
        for (size_t i = 0; i < len; ++i) {
            unsigned char c = static_cast<unsigned char>(input[i]);
            if (isUnreserved(c)) {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back('%');
                out.push_back(hexDigit((c >> 4) & 0x0F));
                out.push_back(hexDigit(c & 0x0F));
            }
        }
        return out;
    }

    std::string Url::decode(const char* input)
    {
        return input ? decode(input, std::strlen(input)) : std::string();
    }

    std::string Url::decode(const std::string& input)
    {
        return decode(input.data(), input.size());
    }

    std::string Url::decode(const char* input, size_t len)
    {
        std::string out;
        if (!tryDecode(input, len, out)) return std::string();
        return out;
    }

    bool Url::tryDecode(const char* input, size_t len, std::string& out)
    {
        out.clear();
        if (len == 0) return true;
        if (!input) return false;
        out.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            char c = input[i];
            if (c == '%') {
                if (i + 2 >= len) { out.clear(); return false; }
                int hi = hexValue(static_cast<unsigned char>(input[i + 1]));
                int lo = hexValue(static_cast<unsigned char>(input[i + 2]));
                if (hi < 0 || lo < 0) { out.clear(); return false; }
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else if (c == '+') {
                // '+' is form-encoding (application/x-www-form-urlencoded),
                // not RFC 3986. Keep as literal '+'; callers that need form
                // semantics translate beforehand.
                out.push_back('+');
            } else {
                out.push_back(c);
            }
        }
        return true;
    }
}
