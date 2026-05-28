#include "HttpRouter.hpp"

#include <cstring>

namespace Bn3Monkey
{
    namespace
    {
        // Pull the next segment out of a path string. Returns false when there
        // are no more segments to consume (cursor reached the end).
        //
        // Leading and consecutive '/' produce empty segments — the matcher
        // chooses to reject those rather than collapse silently, so a request
        // for "/foo//bar" doesn't accidentally hit the "/foo/bar" route.
        bool nextSegment(const char*& cur, const char* end,
                         const char*& seg, size_t& seg_len)
        {
            if (cur >= end) return false;
            if (*cur != '/') return false;
            ++cur; // consume the '/'
            seg = cur;
            while (cur < end && *cur != '/') ++cur;
            seg_len = static_cast<size_t>(cur - seg);
            return true;
        }
    }

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
        _fallback.fn = std::move(fn);
        _fallback.mode = mode;
        _fallback.param_names.clear();
        _has_fallback = static_cast<bool>(_fallback.fn);
    }

    void HttpRouterImpl::registerRoute(Method m, const char* pattern,
                                       HandlerFn fn, RequestProcessingMode mode)
    {
        if (!pattern || !fn) return;

        // Walk the trie, creating nodes as needed.
        TrieNode* node = &_root;
        std::vector<std::string> param_names;

        const char* cur = pattern;
        const char* end = pattern + std::strlen(pattern);

        // "/" — root route. Skip the segment loop; the root node holds it.
        if (cur < end && *cur == '/' && (cur + 1 == end)) {
            // pattern == "/"
            // fall through to writing into _root
        } else {
            const char* seg = nullptr;
            size_t seg_len = 0;
            while (nextSegment(cur, end, seg, seg_len)) {
                if (seg_len == 0) {
                    // "//" in pattern — ignore registration, defensive.
                    return;
                }
                if (seg[0] == ':') {
                    // Parameter segment. Capture name (without the ':') and
                    // descend into / create the param_child slot. Param names
                    // collide silently — last writer wins. We don't enforce
                    // uniqueness here since the same handler may want
                    // /user/:id and /post/:id with different ids.
                    if (!node->param_child) {
                        node->param_child = std::unique_ptr<TrieNode>(new TrieNode());
                    }
                    node->param_name.assign(seg + 1, seg_len - 1);
                    param_names.emplace_back(seg + 1, seg_len - 1);
                    node = node->param_child.get();
                } else {
                    std::string key(seg, seg_len);
                    auto it = node->static_children.find(key);
                    if (it == node->static_children.end()) {
                        auto inserted = node->static_children.emplace(
                            key, std::unique_ptr<TrieNode>(new TrieNode()));
                        node = inserted.first->second.get();
                    } else {
                        node = it->second.get();
                    }
                }
            }
        }

        Route& slot = node->routes[static_cast<size_t>(m)];
        slot.fn = std::move(fn);
        slot.mode = mode;
        slot.param_names = std::move(param_names);
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
        if (m == Method::COUNT || !path || path_len == 0 || path[0] != '/') {
            if (_has_fallback) {
                result.fn   = &_fallback.fn;
                result.mode = _fallback.mode;
                result.fallback_used = true;
            }
            return result;
        }

        const TrieNode* node = &_root;
        const char* cur = path;
        const char* end = path + path_len;

        // Root case: path is exactly "/"
        if (path_len == 1) {
            // node already points at root
        } else {
            const char* seg = nullptr;
            size_t seg_len = 0;

            // Stash param values as we descend. We don't know which route
            // (and therefore which param-name vector) we'll land on until we
            // reach a node with a registered handler — bind names then.
            const char* param_values[MAX_PATH_PARAMS] = { nullptr };
            size_t      param_value_lens[MAX_PATH_PARAMS] = { 0 };
            size_t      depth = 0;

            while (nextSegment(cur, end, seg, seg_len)) {
                if (seg_len == 0) {
                    // "//" or trailing "/" in request — no match. Fall back.
                    node = nullptr;
                    break;
                }
                // Static-first priority: try named child before param child.
                std::string key(seg, seg_len);
                auto it = node->static_children.find(key);
                if (it != node->static_children.end()) {
                    node = it->second.get();
                    continue;
                }
                if (node->param_child) {
                    if (depth >= MAX_PATH_PARAMS) {
                        // Pathological: more :params in the matched route
                        // than we can stash inline. Treat as no match.
                        node = nullptr;
                        break;
                    }
                    param_values[depth]     = seg;
                    param_value_lens[depth] = seg_len;
                    ++depth;
                    node = node->param_child.get();
                    continue;
                }
                // Neither static nor param — no path match.
                node = nullptr;
                break;
            }

            if (node) {
                const Route& route = node->routes[static_cast<size_t>(m)];
                if (route.fn) {
                    result.fn   = &route.fn;
                    result.mode = route.mode;
                    // Bind param names (router-owned) to values (caller-owned).
                    const size_t n = route.param_names.size();
                    for (size_t i = 0; i < n && i < depth && i < MAX_PATH_PARAMS; ++i) {
                        PathParamView& p = result.params[i];
                        p.name      = route.param_names[i].c_str();
                        p.name_len  = route.param_names[i].size();
                        p.value     = param_values[i];
                        p.value_len = param_value_lens[i];
                        ++result.param_count;
                    }
                    return result;
                }
                // Path matched but the requested method has no handler at
                // this node. Check sibling slots — if ANY method is bound
                // here, this is a 405 candidate.
                for (size_t i = 0; i < static_cast<size_t>(Method::COUNT); ++i) {
                    if (node->routes[i].fn) {
                        result.method_not_allowed = true;
                        break;
                    }
                }
            }
        }

        // Root-handler case re-checked here (covers both path=="/" and the
        // edge case where the walk landed on _root with no segments consumed).
        if (path_len == 1 && node == &_root) {
            const Route& route = _root.routes[static_cast<size_t>(m)];
            if (route.fn) {
                result.fn   = &route.fn;
                result.mode = route.mode;
                return result;
            }
            for (size_t i = 0; i < static_cast<size_t>(Method::COUNT); ++i) {
                if (_root.routes[i].fn) { result.method_not_allowed = true; break; }
            }
        }

        if (!result.fn && !result.method_not_allowed && _has_fallback) {
            result.fn   = &_fallback.fn;
            result.mode = _fallback.mode;
            result.fallback_used = true;
        }
        return result;
    }
}
