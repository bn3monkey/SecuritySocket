#include "HttpProcessor.hpp"

#include <cstring>
#include <cstdint>
#include <cstdlib>

namespace Bn3Monkey
{
    namespace
    {
        // ── small ASCII helpers (locale-independent) ──
        inline char lower(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool iequals(const char* a, size_t alen, const char* b)
        {
            size_t i = 0;
            for (; i < alen; ++i) {
                if (b[i] == '\0') return false;
                if (lower(a[i]) != lower(b[i])) return false;
            }
            return b[i] == '\0';
        }

        // Case-insensitive search for `needle` as a comma-separated token within
        // a header value (e.g. Connection: keep-alive, Upgrade). Leading/trailing
        // OWS around each token is ignored.
        bool hasToken(const char* value, size_t vlen, const char* needle)
        {
            const size_t nlen = std::strlen(needle);
            size_t i = 0;
            while (i < vlen) {
                // skip separators / whitespace
                while (i < vlen && (value[i] == ',' || value[i] == ' ' || value[i] == '\t'))
                    ++i;
                size_t start = i;
                while (i < vlen && value[i] != ',')
                    ++i;
                size_t end = i;
                // trim trailing OWS
                while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t'))
                    --end;
                if (end - start == nlen) {
                    bool eq = true;
                    for (size_t k = 0; k < nlen; ++k) {
                        if (lower(value[start + k]) != lower(needle[k])) { eq = false; break; }
                    }
                    if (eq) return true;
                }
            }
            return false;
        }

        // ── SHA-1 (RFC 3174), one-shot, no external deps ──
        struct Sha1
        {
            uint32_t h[5];
            uint64_t total;     // total message length in bytes
            uint8_t  block[64];
            size_t   fill;

            Sha1()
                : total(0), fill(0)
            {
                h[0] = 0x67452301u; h[1] = 0xEFCDAB89u; h[2] = 0x98BADCFEu;
                h[3] = 0x10325476u; h[4] = 0xC3D2E1F0u;
            }

            static uint32_t rol(uint32_t v, int b)
            {
                return (v << b) | (v >> (32 - b));
            }

            void transform(const uint8_t* p)
            {
                uint32_t w[80];
                for (int i = 0; i < 16; ++i) {
                    w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
                           (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
                           (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
                           (static_cast<uint32_t>(p[i * 4 + 3]));
                }
                for (int i = 16; i < 80; ++i)
                    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
                for (int i = 0; i < 80; ++i) {
                    uint32_t f, k;
                    if (i < 20)      { f = (b & c) | ((~b) & d);          k = 0x5A827999u; }
                    else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1u; }
                    else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDCu; }
                    else             { f = b ^ c ^ d;                     k = 0xCA62C1D6u; }
                    uint32_t tmp = rol(a, 5) + f + e + k + w[i];
                    e = d; d = c; c = rol(b, 30); b = a; a = tmp;
                }
                h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
            }

            void update(const uint8_t* data, size_t len)
            {
                total += len;
                while (len > 0) {
                    size_t take = 64 - fill;
                    if (take > len) take = len;
                    std::memcpy(block + fill, data, take);
                    fill += take; data += take; len -= take;
                    if (fill == 64) { transform(block); fill = 0; }
                }
            }

            void finish(uint8_t out[20])
            {
                uint64_t bits = total * 8;
                uint8_t pad = 0x80;
                update(&pad, 1);
                uint8_t zero = 0x00;
                while (fill != 56) update(&zero, 1);
                uint8_t lenbuf[8];
                for (int i = 0; i < 8; ++i)
                    lenbuf[i] = static_cast<uint8_t>((bits >> (56 - 8 * i)) & 0xFF);
                update(lenbuf, 8);   // triggers final transform, fill back to 0
                for (int i = 0; i < 5; ++i) {
                    out[i * 4]     = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
                    out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
                    out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
                    out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
                }
            }
        };

        // base64 of exactly 20 bytes -> 28 chars (incl. one '=' pad) + NUL.
        void base64_20(const uint8_t in[20], char out[29])
        {
            static const char tbl[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            size_t oi = 0;
            size_t i = 0;
            // 6 full 3-byte groups = 18 bytes -> 24 chars
            for (; i + 3 <= 20; i += 3) {
                uint32_t n = (static_cast<uint32_t>(in[i]) << 16) |
                             (static_cast<uint32_t>(in[i + 1]) << 8) |
                              static_cast<uint32_t>(in[i + 2]);
                out[oi++] = tbl[(n >> 18) & 0x3F];
                out[oi++] = tbl[(n >> 12) & 0x3F];
                out[oi++] = tbl[(n >> 6) & 0x3F];
                out[oi++] = tbl[n & 0x3F];
            }
            // 2 bytes left (18..19) -> 3 chars + one '='
            uint32_t n = (static_cast<uint32_t>(in[18]) << 16) |
                         (static_cast<uint32_t>(in[19]) << 8);
            out[oi++] = tbl[(n >> 18) & 0x3F];
            out[oi++] = tbl[(n >> 12) & 0x3F];
            out[oi++] = tbl[(n >> 6) & 0x3F];
            out[oi++] = '=';
            out[oi]   = '\0';
        }

        const char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    } // namespace

    HttpProcessor::ParsedRequest HttpProcessor::parse(const char* buf, size_t len,
                                                      size_t last_len)
    {
        ParsedRequest req;
        req.num_headers = MAX_HEADERS;

        int pret = phr_parse_request(
            buf, len,
            &req.method, &req.method_len,
            &req.path,   &req.path_len,
            &req.minor_version,
            req.headers, &req.num_headers,
            last_len);

        if (pret == -2) {
            req.status      = ParseStatus::INCOMPLETE;
            req.num_headers = 0;
        } else if (pret == -1) {
            req.status      = ParseStatus::MALFORMED;
            req.num_headers = 0;
        } else {
            req.status     = ParseStatus::OK;
            req.header_end = static_cast<size_t>(pret);
        }
        return req;
    }

    const struct phr_header* HttpProcessor::findHeader(const ParsedRequest& req,
                                                       const char* name)
    {
        for (size_t i = 0; i < req.num_headers; ++i) {
            const phr_header& h = req.headers[i];
            if (h.name == nullptr) continue;   // continuation line
            if (iequals(h.name, h.name_len, name))
                return &h;
        }
        return nullptr;
    }

    long HttpProcessor::contentLength(const ParsedRequest& req)
    {
        const phr_header* h = findHeader(req, "Content-Length");
        if (!h) return -1;
        long value = 0;
        bool any = false;
        for (size_t i = 0; i < h->value_len; ++i) {
            char c = h->value[i];
            if (c == ' ' || c == '\t') {
                if (any) break;          // trailing OWS
                continue;                 // leading OWS
            }
            if (c < '0' || c > '9') return -1;
            value = value * 10 + (c - '0');
            any = true;
        }
        return any ? value : -1;
    }

    bool HttpProcessor::isWebSocketUpgrade(const ParsedRequest& req)
    {
        const phr_header* up = findHeader(req, "Upgrade");
        if (!up || !hasToken(up->value, up->value_len, "websocket"))
            return false;
        const phr_header* conn = findHeader(req, "Connection");
        if (!conn || !hasToken(conn->value, conn->value_len, "upgrade"))
            return false;
        return true;
    }

    bool HttpProcessor::keepAlive(const ParsedRequest& req)
    {
        const phr_header* conn = findHeader(req, "Connection");
        if (req.minor_version >= 1) {
            // HTTP/1.1: keep-alive unless explicitly closed.
            if (conn && hasToken(conn->value, conn->value_len, "close"))
                return false;
            return true;
        }
        // HTTP/1.0: close unless explicitly kept alive.
        if (conn && hasToken(conn->value, conn->value_len, "keep-alive"))
            return true;
        return false;
    }

    bool HttpProcessor::computeAccept(const ParsedRequest& req,
                                      char* out, size_t out_cap)
    {
        const phr_header* key = findHeader(req, "Sec-WebSocket-Key");
        if (!key) return false;
        return computeAccept(key->value, key->value_len, out, out_cap);
    }

    bool HttpProcessor::computeAccept(const char* key, size_t key_len,
                                      char* out, size_t out_cap)
    {
        if (!key || out_cap < ACCEPT_BUF_SIZE) return false;

        Sha1 sha;
        sha.update(reinterpret_cast<const uint8_t*>(key), key_len);
        sha.update(reinterpret_cast<const uint8_t*>(kWsGuid), sizeof(kWsGuid) - 1);
        uint8_t digest[20];
        sha.finish(digest);

        base64_20(digest, out);
        return true;
    }

    size_t HttpProcessor::serializeHandshake(const char* accept,
                                             char* out, size_t out_cap)
    {
        // Fixed 101 response. Connection/Upgrade headers per RFC 6455 §4.2.2.
        static const char kPrefix[] =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: ";
        static const char kSuffix[] = "\r\n\r\n";

        const size_t plen = sizeof(kPrefix) - 1;
        const size_t alen = accept ? std::strlen(accept) : 0;
        const size_t slen = sizeof(kSuffix) - 1;
        const size_t total = plen + alen + slen;
        if (out_cap < total) return 0;

        std::memcpy(out, kPrefix, plen);
        if (alen) std::memcpy(out + plen, accept, alen);
        std::memcpy(out + plen + alen, kSuffix, slen);
        return total;
    }
}
