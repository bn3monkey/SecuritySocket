#if !defined(__BN3MONKEY_HTTP_ROUTER__)
#define __BN3MONKEY_HTTP_ROUTER__

#include "../../SecuritySocket.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bn3Monkey
{
    template<typename UserContext>
    class HttpHandler {
        HttpHandler(UserContext& context, 
            void (*function)(UserContext& context, const HttpRequest& request, HttpResponse& response)) : 
            _context(context),
            _function(function)
        {

        }

        void operator()(UserContext& context, const HttpRequest& request, HttpResponse& response) {
            _function(context, request, response);
        }

    private:
        UserContext& _context;
        void (*_function)(UserContext& context, const HttpRequest& request, HttpResponse& response);
    };

    // Trie-based router implementation behind the HttpRouter public interface.
    //
    // Lookup is O(path depth) regardless of route count. Patterns split on '/';
    // segments starting with ':' are parameters (e.g. "/user/:id"). When two
    // routes overlap, the static segment wins ("/user/me" > "/user/:id") — the
    // matcher checks the static child first at each step before falling back
    // to the param child.
    //
    // The router is build-once / read-many: registration mutates the trie,
    // match() never does. Phase 6 calls match() from the event-loop hot path,
    // so the inner loop avoids heap allocation (param results are returned in
    // an inline fixed-size array) and avoids string copies of the URL.
    // SECURITYSOCKET_API on an internal-impl class: the header is private
    // (src/implementation/), so users never see it, but the test DLL links
    // against securitysocket.dll and needs the symbols. Same pattern any
    // other impl class would need if it were directly unit-tested.
    class SECURITYSOCKET_API HttpRouterImpl : public HttpRouter
    {
    public:
        HttpRouterImpl();
        ~HttpRouterImpl() override;

        // ── Registration (build-time) ──
        // Defaults are repeated here (matching HttpRouter's interface) so the
        // impl is also usable directly without going through the base ref —
        // default arguments don't propagate through virtual dispatch in C++.
        void get    (const char* pattern, HandlerFn fn,
                     RequestProcessingMode mode = RequestProcessingMode::FAST) override;
        void post   (const char* pattern, HandlerFn fn,
                     RequestProcessingMode mode = RequestProcessingMode::FAST) override;
        void put    (const char* pattern, HandlerFn fn,
                     RequestProcessingMode mode = RequestProcessingMode::FAST) override;
        void del    (const char* pattern, HandlerFn fn,
                     RequestProcessingMode mode = RequestProcessingMode::FAST) override;
        void patch  (const char* pattern, HandlerFn fn,
                     RequestProcessingMode mode = RequestProcessingMode::FAST) override;
        void head   (const char* pattern, HandlerFn fn,
                     RequestProcessingMode mode = RequestProcessingMode::FAST) override;
        void options(const char* pattern, HandlerFn fn,
                     RequestProcessingMode mode = RequestProcessingMode::FAST) override;
        void fallback(HandlerFn fn,
                      RequestProcessingMode mode = RequestProcessingMode::FAST) override;

        // ── Matching (read-time) ──
        // Internal enum used both at registration (which slot to write) and
        // at match (which slot to read). DEL — not DELETE — because DELETE
        // is a macro on win32 (winnt.h).
        enum class Method : uint8_t {
            GET = 0, POST, PUT, DEL, PATCH, HEAD, OPTIONS, COUNT
        };

        // Inline cap for matched path params. 8 is well over realistic API
        // depth (typical resources nest 1-3 deep) but cheap on the stack.
        static constexpr size_t MAX_PATH_PARAMS = 8;

        struct PathParamView {
            const char* name;      // points into the registered pattern (router-owned)
            size_t      name_len;
            const char* value;     // points into the matched URL (caller-owned)
            size_t      value_len;
        };

        struct MatchResult {
            const HandlerFn*      fn       = nullptr;
            RequestProcessingMode mode     = RequestProcessingMode::FAST;
            // Set when the path matched some node but no handler is registered
            // for the requested method — Phase 6 turns this into 405 Method
            // Not Allowed instead of 404.
            bool                  method_not_allowed = false;
            // Set when fn comes from the fallback registration (no path
            // matched). Useful for handlers that want to log fallback hits.
            bool                  fallback_used      = false;
            PathParamView         params[MAX_PATH_PARAMS];
            size_t                param_count        = 0;
        };

        // method_len / path_len let the caller pass slices straight out of the
        // raw HTTP request line without copying or null-terminating.
        MatchResult match(const char* method, size_t method_len,
                          const char* path,   size_t path_len) const;

        // Convenience for tests; null-terminated method + path.
        MatchResult match(const char* method, const char* path) const;

        // Parse "GET", "POST", … to the internal Method enum. Returns
        // Method::COUNT for unknown verbs.
        static Method parseMethod(const char* m, size_t len);

    private:
        struct Route {
            HandlerFn             fn;
            RequestProcessingMode mode = RequestProcessingMode::FAST;
            // Param names along the path that resolved to this route, in
            // declaration order. Strings own their memory; the trie holds them
            // alive for the router's lifetime, so PathParamView::name can
            // alias .c_str() safely.
            std::vector<std::string> param_names;
        };

        struct TrieNode {
            std::unordered_map<std::string, std::unique_ptr<TrieNode>> static_children;
            std::unique_ptr<TrieNode> param_child;
            std::string param_name;  // valid only when param_child is non-null

            // One slot per method. fn==nullptr → no route registered for this
            // (path, method) pair.
            Route routes[static_cast<size_t>(Method::COUNT)];
        };

        void registerRoute(Method m, const char* pattern,
                           HandlerFn fn, RequestProcessingMode mode);

        TrieNode _root;
        Route    _fallback;
        bool     _has_fallback = false;
    };
}

#endif
