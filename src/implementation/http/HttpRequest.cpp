#include "HttpRequest.hpp"
#include "Url.hpp"

#include <cctype>
#include <cstring>
#include <string>

namespace Bn3Monkey
{
    namespace
    {
        // RFC 7230 §3.2 — header field names are case-insensitive. ASCII-only
        // tolower; HTTP headers don't contain non-ASCII bytes in their names.
        bool ciEqual(const char* a, const char* b)
        {
            if (!a || !b) return false;
            while (*a && *b) {
                unsigned char ca = static_cast<unsigned char>(*a);
                unsigned char cb = static_cast<unsigned char>(*b);
                if (std::tolower(ca) != std::tolower(cb)) return false;
                ++a; ++b;
            }
            return *a == 0 && *b == 0;
        }

        // Copy 'src' into a fixed-capacity buffer, NUL-terminating. Returns
        // false (without writing) if the source doesn't fit — the caller
        // chooses whether truncation is an error (path/query params) or
        // silent (best-effort copy).
        bool copyFixed(char* dst, size_t cap, const char* src, size_t src_len)
        {
            if (src_len + 1 > cap) return false;  // +1 for NUL
            std::memcpy(dst, src, src_len);
            dst[src_len] = '\0';
            return true;
        }
    }

    // ── HeaderIndex ──

    bool HeaderIndex::add(const char* name, const char* value)
    {
        if (_count >= MAX_HEADERS) return false;
        _entries[_count].name  = name;
        _entries[_count].value = value;
        ++_count;
        return true;
    }

    const char* HeaderIndex::find(const char* name) const
    {
        if (!name) return nullptr;
        for (size_t i = 0; i < _count; ++i) {
            if (ciEqual(_entries[i].name, name)) {
                return _entries[i].value;
            }
        }
        return nullptr;
    }

    // ── HttpRequestImpl ──

    HttpRequestImpl::HttpRequestImpl(const char* method,
                                     const char* path,
                                     const HeaderIndex& headers,
                                     const void* body,
                                     size_t body_size)
        : _method(method),
          _path(nullptr),
          _headers(&headers),
          _body(body),
          _body_size(body_size)
    {
        // Split 'path' at the first '?'. Bare path goes into _path_buf (so we
        // own a NUL terminator at the right offset). Query string is parsed
        // into _query_params with each key/value Url-decoded.
        if (!path) {
            _path_buf[0] = '\0';
            _path = _path_buf;
            return;
        }

        const char* q = std::strchr(path, '?');
        const size_t path_len = q ? static_cast<size_t>(q - path) : std::strlen(path);

        if (path_len + 1 > PATH_BUF_CAP) {
            // Overlong URI — Phase 6 server should already have bounced this
            // with 414 URI Too Long before reaching here. Defensive truncation.
            std::memcpy(_path_buf, path, PATH_BUF_CAP - 1);
            _path_buf[PATH_BUF_CAP - 1] = '\0';
        } else {
            std::memcpy(_path_buf, path, path_len);
            _path_buf[path_len] = '\0';
        }
        _path = _path_buf;

        if (q) {
            const char* qs = q + 1;
            const size_t qs_len = std::strlen(qs);
            parseQueryString(qs, qs_len);
        }
    }

    const char* HttpRequestImpl::header(const char* name) const
    {
        return _headers ? _headers->find(name) : nullptr;
    }

    const char* HttpRequestImpl::pathParam(const char* name) const
    {
        if (!name) return nullptr;
        for (size_t i = 0; i < _path_param_count; ++i) {
            if (std::strcmp(_path_params[i].name, name) == 0) {
                return _path_params[i].value;
            }
        }
        return nullptr;
    }

    const char* HttpRequestImpl::query(const char* name) const
    {
        if (!name) return nullptr;
        for (size_t i = 0; i < _query_param_count; ++i) {
            if (std::strcmp(_query_params[i].name, name) == 0) {
                return _query_params[i].value;
            }
        }
        return nullptr;
    }

    bool HttpRequestImpl::setPathParam(const char* name, const char* value)
    {
        if (!name || !value) return false;
        if (_path_param_count >= MAX_PATH_PARAMS) return false;
        const size_t name_len  = std::strlen(name);
        const size_t value_len = std::strlen(value);
        Param& p = _path_params[_path_param_count];
        if (!copyFixed(p.name,  PARAM_NAME_CAP,  name,  name_len))   return false;
        if (!copyFixed(p.value, PARAM_VALUE_CAP, value, value_len)) {
            // Reject the whole binding rather than store a half-truncated
            // value — silent truncation here would surface as a confusing
            // wrong-value bug at handler time.
            p.name[0] = '\0';
            return false;
        }
        ++_path_param_count;
        return true;
    }

    void HttpRequestImpl::parseQueryString(const char* qs, size_t qs_len)
    {
        // Walk "k1=v1&k2=v2&..." pairs. Each side is Url-decoded into the
        // inline param slot. Malformed pairs (no '=' / empty key) are
        // silently skipped — same as Express and Go's net/url.
        size_t i = 0;
        while (i < qs_len && _query_param_count < MAX_QUERY_PARAMS) {
            // Find pair end ('&' or end of string).
            size_t pair_end = i;
            while (pair_end < qs_len && qs[pair_end] != '&') ++pair_end;

            // Find '=' inside this pair.
            size_t eq = i;
            while (eq < pair_end && qs[eq] != '=') ++eq;

            const char* key_p   = qs + i;
            const size_t key_n  = eq - i;
            const char* val_p   = (eq < pair_end) ? qs + eq + 1 : nullptr;
            const size_t val_n  = (eq < pair_end) ? pair_end - (eq + 1) : 0;

            if (key_n > 0) {
                std::string key_decoded, val_decoded;
                const bool key_ok = Url::tryDecode(key_p, key_n, key_decoded);
                const bool val_ok = val_p
                    ? Url::tryDecode(val_p, val_n, val_decoded)
                    : true;
                if (key_ok && val_ok
                    && key_decoded.size() + 1 <= PARAM_NAME_CAP
                    && val_decoded.size() + 1 <= PARAM_VALUE_CAP)
                {
                    Param& p = _query_params[_query_param_count];
                    std::memcpy(p.name,  key_decoded.data(), key_decoded.size());
                    p.name[key_decoded.size()] = '\0';
                    std::memcpy(p.value, val_decoded.data(), val_decoded.size());
                    p.value[val_decoded.size()] = '\0';
                    ++_query_param_count;
                }
            }

            i = (pair_end < qs_len) ? pair_end + 1 : pair_end;
        }
    }
}
