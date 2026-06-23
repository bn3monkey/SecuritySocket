#include "HttpPhase.hpp"

#include "../../SecuritySocket.hpp"          // HttpRouter::HandlerFn, ClientConnection
#include "../http/HttpRouter.hpp"            // HttpRouterImpl, MatchResult
#include "../http/HttpRequest.hpp"           // HttpRequestImpl, HeaderIndex
#include "../http/HttpResponse.hpp"          // HttpResponseImpl
#include "../http/Url.hpp"                   // Url::tryDecode (pathParam auto-decode)
#include "../protocol/HttpParser.hpp"        // HttpParser

#include <cstring>
#include <string>

namespace Bn3Monkey
{
    namespace
    {
        // In-place tokenise the parsed header block: NUL-terminate METHOD, PATH,
        // and every (name, value) pair so HttpRequestImpl/HeaderIndex can hand
        // back bare `const char*`. Writes directly through the input buffer's
        // head() (mutable; StagingBuffer::head() returns void*). The parsed
        // pointers alias head() and stay valid because handle() never reserve()s
        // the input between parse and here (so no realloc/compact can move them).
        void tokeniseAndIndex(const HttpParser::ParsedRequest& req,
                              HeaderIndex& out_headers,
                              const char*& out_method, const char*& out_path)
        {
            // METHOD <SP> PATH <SP> HTTP/1.x CRLF — the SP after method/path sit
            // at method[method_len] / path[path_len].
            char* method = const_cast<char*>(req.method);
            method[req.method_len] = '\0';
            out_method = method;

            char* path = const_cast<char*>(req.path);
            path[req.path_len] = '\0';
            out_path = path;

            out_headers.clear();
            for (size_t i = 0; i < req.num_headers; ++i) {
                const phr_header& h = req.headers[i];
                // picohttpparser sets name=nullptr for obs-fold continuation
                // lines; skip them (rare, and our HeaderIndex has no concept of
                // folded values).
                if (!h.name) continue;
                char* name  = const_cast<char*>(h.name);
                char* value = const_cast<char*>(h.value);
                name[h.name_len]   = '\0';   // the ':' separator
                value[h.value_len] = '\0';   // the CR before LF
                out_headers.add(name, value);
            }
        }

        // Serialize a built response into the output buffer, growing it once if
        // the first attempt overflows the current capacity. Resets prior send
        // state (clear) and marks the produced length (fill).
        void serializeInto(StagingBuffer& out, const HttpResponseImpl& resp)
        {
            out.clear();
            size_t n = resp.serialize(static_cast<char*>(out.data()), out.capacity());
            if (n == 0) {
                // Grow to fit status line + headers + body, then retry.
                out.reserve(resp.bodySize() + 1024);
                n = resp.serialize(static_cast<char*>(out.data()), out.capacity());
            }
            out.fill(n);
        }

        // Minimal "<code> <reason>" response, empty body, Connection: close.
        void emitStatus(StagingBuffer& out, int status)
        {
            HttpResponseImpl resp;
            resp.status(status);
            resp.header("Connection", "close");
            serializeInto(out, resp);
        }
    }

    void HttpPhase::reset()
    {
        _headers.clear();
        _keep_alive = false;
        _last_len   = 0;
    }

    ConnectionState HttpPhase::onReadable(PhaseHost& host,
                                          SocketMultiEventListener& listener)
    {
        return mapResultToNextState(handle(host, listener));
    }

    HttpPhase::HttpRequestResult
    HttpPhase::handle(PhaseHost& host, SocketMultiEventListener& listener)
    {
        StagingBuffer& in  = host.input();
        StagingBuffer& out = host.output();

        const char*  msg       = static_cast<const char*>(in.head());
        const size_t available = in.pending();

        // 1. Parse the header block (incremental — _last_len skips re-scanning
        //    the already-seen prefix; the message stays at head() between calls).
        HttpParser::ParsedRequest req = HttpParser::parse(msg, available, _last_len);

        if (req.status == HttpParser::ParseStatus::INCOMPLETE) {
            _last_len = available;                 // already-scanned byte count
            return HttpRequestResult::NEED_MORE_BYTES;   // do NOT drain
        }
        if (req.status == HttpParser::ParseStatus::MALFORMED) {
            emitStatus(out, 400);
            return HttpRequestResult::ERROR_CLOSE;
        }
        _last_len = 0;   // a full header block parsed; reset the re-scan hint

        // 2. Read every header-derived value NOW, while the parsed pointers are
        //    valid (handle() does not reserve() the input below, so head() and
        //    every slice into it stay put through dispatch).
        const long content_length = HttpParser::contentLength(req);
        _keep_alive               = HttpParser::keepAlive(req);
        const bool is_upgrade     = HttpParser::isWebSocketUpgrade(req);

        // 3. WebSocket Upgrade — the one cross-group exit. Handled before body
        //    accumulation (an Upgrade request carries no body).
        if (is_upgrade) {
            const char* ws_pattern = host.wsPattern();
            bool ok = false;
            if (ws_pattern) {
                const size_t plen = std::strlen(ws_pattern);
                ok = (plen == req.path_len) &&
                     (std::memcmp(ws_pattern, req.path, plen) == 0);
            }
            if (!ok) {
                emitStatus(out, ws_pattern ? 404 : 400);
                return HttpRequestResult::ERROR_CLOSE;
            }

            char accept[HttpParser::ACCEPT_BUF_SIZE];
            if (!HttpParser::computeAccept(req, accept, sizeof(accept))) {
                emitStatus(out, 400);
                return HttpRequestResult::ERROR_CLOSE;
            }
            out.clear();
            const size_t n = HttpParser::serializeHandshake(
                accept, static_cast<char*>(out.data()), out.capacity());
            if (n == 0) {
                emitStatus(out, 500);
                return HttpRequestResult::ERROR_CLOSE;
            }
            out.fill(n);
            host.setWebSocket(true);
            in.drain(req.header_end);   // upgrade request carries no body
            return HttpRequestResult::UPGRADING;
        }

        // 4. Body accumulation by Content-Length. The cap is the configured
        //    NetworkConfiguration::max_http_request_body_size (threaded through
        //    PhaseHost). Cast to long so a negative content_length (no header)
        //    stays a signed compare and never trips the cap.
        const long max_request_body = static_cast<long>(host.maxHttpRequestBodySize());
        if (content_length > max_request_body) {
            emitStatus(out, 413);
            return HttpRequestResult::ERROR_CLOSE;
        }
        const size_t body_len = (content_length > 0) ? static_cast<size_t>(content_length) : 0;
        const size_t total    = req.header_end + body_len;
        if (in.pending() < total) {
            in.reserve(total - in.pending());   // room for the full message
            return HttpRequestResult::NEED_MORE_BYTES;   // wait for the body
        }

        // 5. Route match — match on the PATH ONLY. The request-target may carry a
        //    ?query, which is not part of the route pattern (query params are
        //    parsed separately by HttpRequestImpl). Without stripping it, every
        //    GET with a query string misses the trie and falls through to 404.
        HttpRouterImpl* router = host.router();
        if (!router) {
            emitStatus(out, 404);
            in.drain(total);
            return HttpRequestResult::ERROR_CLOSE;
        }
        size_t route_path_len = req.path_len;
        if (const void* q = std::memchr(req.path, '?', req.path_len)) {
            route_path_len = static_cast<size_t>(static_cast<const char*>(q) - req.path);
        }
        HttpRouterImpl::MatchResult mr =
            router->match(req.method, req.method_len, req.path, route_path_len);

        // No handler at all: 405 if the path exists for other methods, else 404.
        // (A registered fallback fills mr.fn, so it flows through dispatch below.)
        if (!mr.fn) {
            emitStatus(out, mr.method_not_allowed ? 405 : 404);
            in.drain(total);
            return HttpRequestResult::ERROR_CLOSE;
        }

        // 6. Tokenise in place + build the request/response views. Done now (not
        //    inside the dispatch lambda) so the route mode is known; the Impls
        //    are built inside the lambda so their lifetime spans a SLOW worker.
        const char* method = nullptr;
        const char* path   = nullptr;
        tokeniseAndIndex(req, _headers, method, path);
        const void* body = static_cast<const char*>(in.head()) + req.header_end;

        const bool keep_alive = _keep_alive;
        // Shared dispatch body for FAST and SLOW. The message bytes (head()) +
        // mr params + _headers all stay valid through a SLOW run because the
        // host does not recv()/reserve() while the socket is detached.
        auto call = [this, &host, mr, method, path, body, body_len, keep_alive, total]() {
            StagingBuffer&   out = host.output();
            HttpRequestImpl  reqv(method, path, _headers, body, body_len);
            HttpResponseImpl resp;

            // Bind decoded path params (pathParam auto-decode, §2-9).
            for (size_t i = 0; i < mr.param_count; ++i) {
                const auto& pv = mr.params[i];
                std::string name(pv.name, pv.name_len);
                std::string value;
                if (Url::tryDecode(pv.value, pv.value_len, value)) {
                    reqv.setPathParam(name.c_str(), value.c_str());
                }
            }

            (*mr.fn)(host.connection(), reqv, resp);
            resp.header("Connection", keep_alive ? "keep-alive" : "close");

            serializeInto(out, resp);
            host.input().drain(total);
        };

        if (mr.mode == RequestProcessingMode::SLOW) {
            host.dispatchSlow(call, listener);
            return HttpRequestResult::DISPATCHED_SLOW;
        }
        call();
        return HttpRequestResult::RESPONDING;
    }

    ConnectionState HttpPhase::mapResultToNextState(HttpRequestResult r) const
    {
        switch (r) {
        case HttpRequestResult::NEED_MORE_BYTES:
            return ConnectionState::ReceivingHttpRequest;
        case HttpRequestResult::RESPONDING:
        case HttpRequestResult::DISPATCHED_SLOW:
            return ConnectionState::SendingHttpResponse;
        case HttpRequestResult::UPGRADING:
            return ConnectionState::SendingHandshakeResponse;
        case HttpRequestResult::ERROR_CLOSE:
        default:
            return ConnectionState::Closing;
        }
    }

    ConnectionState HttpPhase::onSendComplete(PhaseHost& host)
    {
        (void)host;
        // SendingHttpResponse finished. Keep-alive recycles the connection for
        // the next request on the same socket; otherwise close. (Error/close
        // responses returned Closing directly and never reach here.)
        if (_keep_alive) {
            _headers.clear();
            _last_len = 0;
            return ConnectionState::WaitingForNextHttpRequest;
        }
        return ConnectionState::Closed;
    }
}
