#if !defined(__BN3MONKEY_HTTP_CLIENT__)
#define __BN3MONKEY_HTTP_CLIENT__

// ── CANDIDATE (Phase 7) ──────────────────────────────────────────────────────
// PUBLIC `HttpClient` -> move to src/SecuritySocket.hpp on integration (it is a
// thin protocol layer over the existing public `Client`, mileston_http.md §2-7).
// No separate *Impl: HttpClient reuses Client's socket lifecycle (open/connect/
// read/write) and adds request serialisation + response parsing on top.
//
// Integration notes:
//   - HttpClient is declared a friend of HttpClientRequest/HttpClientResponse so
//     it can reach their containers (serialize the request Impl, fill the
//     response Impl). Keep those friend declarations.
//   - The Host authority is captured from NetworkConfiguration at construction.
// ─────────────────────────────────────────────────────────────────────────────

#include "../../SecuritySocket.hpp"

#include "HttpClientRequest.hpp"
#include "HttpClientResponse.hpp"

#include <string>

namespace Bn3Monkey
{
    // HTTP/HTTPS client. Each call performs one request/response exchange over
    // the inherited Client socket, reconnecting if the connection is not open.
    // Responses carry both the transport result (resultCode) and the HTTP
    // status, so callers check one object (mileston_http.md §2-7).
    class SECURITYSOCKET_API HttpClient : public Client
    {
    public:
        explicit HttpClient(const NetworkConfiguration& cfg);
        HttpClient(const NetworkConfiguration& cfg,
                   const TlsClientConfiguration& tls);

        // Bodyless helpers.
        HttpClientResponse get    (const char* path);
        HttpClientResponse head   (const char* path);
        HttpClientResponse del    (const char* path);
        HttpClientResponse options(const char* path);

        // Helpers carrying a body.
        HttpClientResponse post (const char* path, const void* body, size_t len);
        HttpClientResponse put  (const char* path, const void* body, size_t len);
        HttpClientResponse patch(const char* path, const void* body, size_t len);

        // Full control — custom headers / query / printf-path, etc.
        HttpClientResponse request(const HttpClientRequest& req);

    private:
        // One exchange: ensure connected -> serialize -> write -> read full
        // response -> parse. Fills a response (with resultCode set on any
        // transport failure) and returns it by value.
        HttpClientResponse exchange(const HttpClientRequest& req);

        // Build a simple request (method + path [+ body]) and run it.
        HttpClientResponse simple(const char* method, const char* path,
                                  const void* body, size_t len);

        // Lazily open()+connect() the inherited Client. Returns the transport
        // result; SUCCESS means the socket is ready.
        NetworkResult ensureConnected();

        std::string _host;          // "ip:port" for the Host header
        bool        _connected{ false };
    };
}

#endif // __BN3MONKEY_HTTP_CLIENT__
