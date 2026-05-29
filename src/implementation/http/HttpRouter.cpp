#include "HttpRouter.hpp"

#include <cstring>
#include <utility>

namespace Bn3Monkey
{
    HttpRouterImpl::HttpRouterImpl() = default;
    HttpRouterImpl::~HttpRouterImpl() = default;

    void HttpRouterImpl::get    (const char* pattern, HandlerFn fn, RequestProcessingMode mode) { registerRoute(Method::GET,     pattern, std::move(fn), mode); }
    void HttpRouterImpl::post   (const char* pattern, HandlerFn fn, RequestProcessingMode mode) { registerRoute(Method::POST,    pattern, std::move(fn), mode); }
    void HttpRouterImpl::put    (const char* pattern, HandlerFn fn, RequestProcessingMode mode) { registerRoute(Method::PUT,     pattern, std::move(fn), mode); }
    void HttpRouterImpl::del    (const char* pattern, HandlerFn fn, RequestProcessingMode mode) { registerRoute(Method::DEL,     pattern, std::move(fn), mode); }
    void HttpRouterImpl::patch  (const char* pattern, HandlerFn fn, RequestProcessingMode mode) { registerRoute(Method::PATCH,   pattern, std::move(fn), mode); }
    void HttpRouterImpl::head   (const char* pattern, HandlerFn fn, RequestProcessingMode mode) { registerRoute(Method::HEAD,    pattern, std::move(fn), mode); }
    void HttpRouterImpl::options(const char* pattern, HandlerFn fn, RequestProcessingMode mode) { registerRoute(Method::OPTIONS, pattern, std::move(fn), mode); }

    void HttpRouterImpl::fallback(HandlerFn fn, RequestProcessingMode mode)
    {
        _fallback.fn   = std::move(fn);
        _fallback.mode = mode;
        _has_fallback  = static_cast<bool>(_fallback.fn);
    }

    void HttpRouterImpl::registerRoute(Method m, const char* pattern,
                                       HandlerFn fn, RequestProcessingMode mode)
    {
        if (!pattern || !fn) return;
        const size_t mi = static_cast<size_t>(m);

        // Own the pattern for the router's lifetime — the trie's segments alias
        // into it (static-segment matching AND param-name pointers).
        _patterns.emplace_back(pattern);
        const std::string& stored = _patterns.back();

        // add() returns the leaf; bind action_id immediately (the reference is
        // only valid until the next add(), which may grow the pool).
        Segment& leaf = _tries[mi].add(stored.c_str(), stored.size());
        if (leaf.action_id == NULL_ACTION_ID) {
            // First handler at this (path, method): allocate an action slot.
            leaf.action_id = _actions[mi].size();
            _actions[mi].push_back(Route{ std::move(fn), mode });
        } else {
            // Re-registration of the same (path, method) — last writer wins.
            _actions[mi][leaf.action_id] = Route{ std::move(fn), mode };
        }
    }

    HttpRouterImpl::Method HttpRouterImpl::parseMethod(const char* m, size_t len)
    {
        // Tight switch on length first — typical HTTP methods are 3-7 chars.
        if (!m) return Method::COUNT;
        switch (len) {
        case 3:
            if (std::memcmp(m, "GET", 3) == 0) return Method::GET;
            if (std::memcmp(m, "PUT", 3) == 0) return Method::PUT;
            break;
        case 4:
            if (std::memcmp(m, "POST", 4) == 0) return Method::POST;
            if (std::memcmp(m, "HEAD", 4) == 0) return Method::HEAD;
            break;
        case 5:
            if (std::memcmp(m, "PATCH", 5) == 0) return Method::PATCH;
            break;
        case 6:
            if (std::memcmp(m, "DELETE", 6) == 0) return Method::DEL;
            break;
        case 7:
            if (std::memcmp(m, "OPTIONS", 7) == 0) return Method::OPTIONS;
            break;
        default: break;
        }
        return Method::COUNT;
    }

    HttpRouterImpl::MatchResult HttpRouterImpl::match(
        const char* method, const char* path) const
    {
        return match(method, method ? std::strlen(method) : 0,
                     path,   path   ? std::strlen(path)   : 0);
    }

    HttpRouterImpl::MatchResult HttpRouterImpl::match(
        const char* method, size_t method_len,
        const char* path,   size_t path_len) const
    {
        MatchResult result;

        const Method m = parseMethod(method, method_len);
        if (m == Method::COUNT) {
            // Unknown verb — behaves like a path miss → fallback (if any).
            if (_has_fallback) {
                result.fn            = &_fallback.fn;
                result.mode          = _fallback.mode;
                result.fallback_used = true;
            }
            return result;
        }
        const size_t mi = static_cast<size_t>(m);

        // Match in the requested method's trie, binding params as we descend.
        // The trie hands back the raw param Segment + URL slice; we strip the
        // ':' here and point name into the router-owned pattern (stable deque)
        // and value into the caller-owned URL.
        bool overflow = false;
        const Segment* leaf = _tries[mi].match(path, path_len,
            [&](const Segment& p, const char* value, size_t value_len) {
                if (result.param_count >= MAX_PATH_PARAMS) { overflow = true; return; }
                PathParamView& pv = result.params[result.param_count++];
                const char* name = p.content;
                size_t      nlen = p.size;
                // Strip the ':' (param) or '*' (catch-all) decoration so the
                // bound name is the bare identifier.
                if (nlen && (name[0] == ':' || name[0] == '*')) { ++name; --nlen; }
                pv.name      = name;
                pv.name_len  = nlen;
                pv.value     = value;
                pv.value_len = value_len;
            });

        if (!overflow && leaf && leaf->action_id != NULL_ACTION_ID) {
            const Route& route = _actions[mi][leaf->action_id];
            result.fn   = &route.fn;
            result.mode = route.mode;
            return result;
        }

        // Not a hit for this method — discard any params collected along the
        // (failed or handler-less) descent.
        result.param_count = 0;

        // 405 candidate: the path may be registered under a different method.
        for (size_t i = 0; i < static_cast<size_t>(Method::COUNT); ++i) {
            if (i == mi) continue;
            const Segment* other = _tries[i].match(path, path_len);
            if (other && other->action_id != NULL_ACTION_ID) {
                result.method_not_allowed = true;
                return result;
            }
        }

        // No path anywhere → fallback (if any).
        if (_has_fallback) {
            result.fn            = &_fallback.fn;
            result.mode          = _fallback.mode;
            result.fallback_used = true;
        }
        return result;
    }
}
