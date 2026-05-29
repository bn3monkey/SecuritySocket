#if !defined(__BN3MONKEY_SEGMENT_TRIE__)
#define __BN3MONKEY_SEGMENT_TRIE__

#include <vector>
#include <cstring>
#include <type_traits>

#include "../memory/pool.hpp"
#include "../hook/tree_hook.hpp"

namespace Bn3Monkey
{
    enum class SegmentType {
        STATIC,
        PARAM,      // ":name" — matches exactly one path segment
        WILDCARD    // "*name" — catch-all; absorbs the rest of the path
    };
    enum class ParamSegmentStyle {
        COLON,
        BRACE
    };

    using SegmentID = size_t;
    static constexpr SegmentID NULL_SEGMENT_ID = static_cast<size_t>(-1);
    using ActionID = size_t;
    static constexpr ActionID NULL_ACTION_ID = static_cast<size_t>(-1);

    struct Segment {
        SegmentID id {NULL_SEGMENT_ID};
        EntityTreeHook<size_t> hook;

        SegmentType type;
        const char* content;   // non-owning; points into the registered pattern
        size_t size;
        // Opaque payload slot. SegmentTrie never reads or interprets it — the
        // caller (HttpRouter) writes the id of wherever it keeps the action.
        ActionID action_id {NULL_ACTION_ID};

        // Stays trivially copyable (copy/move/dtor untouched) so ObjectArena's
        // raw-byte view/realloc path remains valid. The ctor exists only
        // because the arena placement-news with parenthesized args, which is
        // not aggregate init before C++20.
        Segment() = default;
        Segment(SegmentID id_, EntityTreeHook<size_t> hook_, SegmentType type_,
                const char* content_, size_t size_)
            : id(id_), hook(hook_), type(type_), content(content_), size(size_) {}
    };

    class SegmentPool : public EntityResolver<Segment, SegmentID> {
    public:
        constexpr static size_t INITIAL_CAPAICTY = 32;
        SegmentPool(size_t capacity = INITIAL_CAPAICTY) : _pool(capacity) {}

        SegmentID toEntityID(size_t index) const override {
            return index;
        }
        size_t   fromEntityID(SegmentID id) const override {
            return id;
        }
        Segment&  resolve(size_t id) override {
            return *_pool.get(id);
        }
        const Segment& resolve(size_t id) const override {
            return *_pool.get(id);
        }

        Segment& allocate(const char* segment, size_t size) {
            return allocateNode(classify(segment, size), segment, size);
        }
        Segment& allocate(const char* segment) {
            return allocate(segment, strlen(segment));
        }

        // The trie root is a contentless anchor: it owns no path segment and is
        // never compared against a URL slice, so allocate it explicitly as
        // STATIC with a null body (isParamSegment would deref segment[0]).
        Segment& allocateRoot() {
            return allocateNode(SegmentType::STATIC, nullptr, 0);
        }

        // Public so SegmentTrie can classify a slice before searching for the
        // matching child (it must know static/param/wildcard prior to
        // allocating). COLON style: ':name' → PARAM, '*name' → WILDCARD,
        // otherwise STATIC.
        static SegmentType classify(const char* segment, size_t size, ParamSegmentStyle style = ParamSegmentStyle::COLON) {
            if (size == 0) return SegmentType::STATIC;
            switch (style) {
                case ParamSegmentStyle::COLON:
                    if (segment[0] == ':') return SegmentType::PARAM;
                    if (segment[0] == '*') return SegmentType::WILDCARD;
                    return SegmentType::STATIC;
                case ParamSegmentStyle::BRACE:
                    if (segment[0] == '{' && segment[size-1] == '}')
                        return (size >= 2 && segment[1] == '*') ? SegmentType::WILDCARD
                                                                : SegmentType::PARAM;
                    return SegmentType::STATIC;
                default:
                    return SegmentType::STATIC;
            }
        }

    private:
        Segment& allocateNode(SegmentType type, const char* content, size_t size) {
            auto id = _pool.allocate(
                NULL_SEGMENT_ID,
                EntityTreeHook<size_t>{
                    NULL_SEGMENT_ID,
                    NULL_SEGMENT_ID,
                    NULL_SEGMENT_ID,
                    NULL_SEGMENT_ID
                },
                type,
                content,
                size
            );
            auto* ptr = _pool.get(id);
            ptr->id = id;
            return *ptr;
        }
        ObjectPool<Segment> _pool;
    };

    // Pure structural path trie. It knows only Segment and the tree links —
    // nothing about HTTP, params-as-values, methods, or actions. Any logic
    // that needs request context is injected as a callback. ParamView /
    // MatchResult / ':' stripping / method dispatch live in the caller
    // (HttpRouter); one SegmentTrie per HTTP method is the intended use.
    //
    // Build-once / read-many:
    //   add()   mutates the trie (registration time).
    //   match() never mutates (event-loop hot path).
    //
    // Segments split on '/'. A segment beginning with ':' is a parameter
    // (matches one segment, e.g. "/user/:id"); one beginning with '*' is a
    // catch-all (must be last, absorbs the rest of the path including interior
    // '/', e.g. "/static/*filepath"). At each node the matcher tries the
    // static child first, then the single param child, then the single
    // wildcard child — so "/user/me" wins over "/user/:id", and an exact or
    // param route wins over a sibling catch-all. Matching is greedy with NO
    // backtracking (mirrors the legacy HttpRouterImpl): once it descends a
    // branch it does not retry a sibling.
    class SegmentTrie
    {
    public:
        SegmentTrie() {
            root_id = _pool.allocateRoot().id;
        }

        // Creates the path (allocating nodes as needed) and returns its leaf
        // segment. SegmentTrie does NOT touch leaf.action_id — the caller
        // reads/writes it. Adding a path never mutates existing segments'
        // data; a split only links a new sibling under an existing node.
        //
        // CAVEAT: the returned reference is valid only until the next add().
        // add() may grow the pool, which realloc-moves the arena (data and
        // SegmentIDs are preserved, addresses are not). So bind action_id
        // immediately after each add(), before registering the next route.
        Segment& add(const char* path, size_t size) {
            // "/" — the root route lives on the anchor itself.
            if (size == 1 && path[0] == '/') {
                return _pool.resolve(root_id);
            }

            SegmentID node = root_id;
            const char* cur = path;
            const char* end = path + size;
            const char* seg = nullptr;
            size_t      seg_len = 0;

            while (nextSegment(cur, end, seg, seg_len)) {
                if (seg_len == 0) break;   // "//" or trailing "/" — ignore defensively

                const SegmentType want = SegmentPool::classify(seg, seg_len);

                SegmentCursor cursor(_pool, node);
                SegmentID child = cursor.findChild([&](Segment& s) -> bool {
                    if (want == SegmentType::STATIC)
                        return s.type == SegmentType::STATIC
                            && s.size == seg_len
                            && std::memcmp(s.content, seg, seg_len) == 0;
                    // PARAM / WILDCARD: at most one child of that kind per node.
                    return s.type == want;
                });

                if (child == NULL_SEGMENT_ID) {
                    // No matching child — create one and link it in.
                    // (allocate() may grow/realloc the pool, so only ids are
                    // held across it; cursor re-resolves through the resolver.)
                    SegmentID created = _pool.allocate(seg, seg_len).id;
                    SegmentCursor(_pool, node).addChild(created);
                    child = created;
                }
                // A reused param/wildcard child is left as-is (first-writer-wins)
                // so an existing node is never rewritten by a later registration.
                node = child;

                // A catch-all must be the final segment: it is terminal and
                // owns the rest of the path, so stop here (any trailing pattern
                // segments would be unreachable — defensive).
                if (want == SegmentType::WILDCARD) break;
            }

            return _pool.resolve(node);
        }
        Segment& add(const char* path) {
            return add(path, strlen(path));
        }

        // Walks `path` from the root, static-child-first with param fallback.
        // Returns the matched leaf segment, or nullptr on structural mismatch.
        // For every PARAM node crossed during the (successful or not yet
        // failed) descent, onParam is invoked with the raw param Segment and
        // the URL slice that filled it:
        //     onParam(const Segment& paramSeg, const char* value, size_t len)
        // SegmentTrie does not interpret the param (no ':' stripping, no
        // ParamView) — that is the caller's job.
        //
        // NOTE: onParam may fire for params along a path that ultimately fails
        // to match (returns nullptr). Callers must disregard collected params
        // when the return value is nullptr.
        // match() is const: it never mutates the trie. It resolves through the
        // const EntityResolver overload and walks children via the intrusive
        // hook links directly (the mutating EntityTreeCursor is non-const and
        // is only used by add()). Returns the matched leaf, or nullptr on
        // structural mismatch.
        template<typename OnParam>
        const Segment* match(const char* path, size_t size, OnParam&& onParam) const {
            static_assert(is_invocable<OnParam, const Segment&, const char*, size_t>::value,
                "Callback should be called as onParam(const Segment&, const char* value, size_t value_len)");

            if (!path || size == 0 || path[0] != '/') return nullptr;

            // "/" — root route.
            if (size == 1) {
                return &_pool.resolve(root_id);
            }

            SegmentID node = root_id;
            const char* cur = path;
            const char* end = path + size;
            const char* seg = nullptr;
            size_t      seg_len = 0;

            while (nextSegment(cur, end, seg, seg_len)) {
                if (seg_len == 0) return nullptr;   // "//" or trailing "/" — no match

                const Segment& parent = _pool.resolve(node);

                // Priority: static (exact) > param (one segment) > wildcard
                // (catch-all). The first two consume a single segment; the
                // wildcard absorbs the rest of the path and terminates.
                SegmentID child = findStaticChild(parent, seg, seg_len);
                if (child == NULL_SEGMENT_ID) {
                    SegmentID pchild = findParamChild(parent);
                    if (pchild != NULL_SEGMENT_ID) {
                        onParam(_pool.resolve(pchild), seg, seg_len);
                        child = pchild;
                    } else {
                        SegmentID wchild = findWildcardChild(parent);
                        if (wchild == NULL_SEGMENT_ID) return nullptr;   // no structural match
                        // Catch-all: bind the remaining raw path [seg, end) —
                        // includes interior '/'. seg points at the first
                        // remaining segment, so a trailing-slash-only remainder
                        // was already rejected by the seg_len == 0 guard above.
                        onParam(_pool.resolve(wchild), seg, static_cast<size_t>(end - seg));
                        return &_pool.resolve(wchild);   // wildcard node is terminal
                    }
                }
                node = child;
            }

            return &_pool.resolve(node);
        }
        // Convenience overload when the caller does not need params.
        const Segment* match(const char* path, size_t size) const {
            return match(path, size, [](const Segment&, const char*, size_t){});
        }
        const Segment* match(const char* path) const {
            return match(path, strlen(path));
        }

        // Resolve a stable SegmentID back to its segment (survives pool growth).
        Segment&       at(SegmentID id)       { return _pool.resolve(id); }
        const Segment& at(SegmentID id) const { return _pool.resolve(id); }

    private:
        using SegmentCursor =
            EntityTreeCursor<Segment, SegmentID, &Segment::hook, NULL_SEGMENT_ID>;

        // Read-only child lookup used by const match(). Walks the sibling list
        // through the intrusive hook links so no mutating cursor is needed.
        SegmentID findStaticChild(const Segment& parent, const char* seg, size_t len) const {
            for (SegmentID c = parent.hook.first_child_id; c != NULL_SEGMENT_ID; ) {
                const Segment& s = _pool.resolve(c);
                if (s.type == SegmentType::STATIC && s.size == len
                    && std::memcmp(s.content, seg, len) == 0)
                    return c;
                c = s.hook.next_sibling_id;
            }
            return NULL_SEGMENT_ID;
        }
        SegmentID findParamChild(const Segment& parent) const {
            for (SegmentID c = parent.hook.first_child_id; c != NULL_SEGMENT_ID; ) {
                const Segment& s = _pool.resolve(c);
                if (s.type == SegmentType::PARAM) return c;
                c = s.hook.next_sibling_id;
            }
            return NULL_SEGMENT_ID;
        }
        SegmentID findWildcardChild(const Segment& parent) const {
            for (SegmentID c = parent.hook.first_child_id; c != NULL_SEGMENT_ID; ) {
                const Segment& s = _pool.resolve(c);
                if (s.type == SegmentType::WILDCARD) return c;
                c = s.hook.next_sibling_id;
            }
            return NULL_SEGMENT_ID;
        }

        // Pull the next '/'-delimited segment out of [cur, end). Returns false
        // when exhausted. Leading/consecutive '/' yield empty segments — the
        // caller rejects those rather than collapsing them (so "/foo//bar"
        // does not silently hit "/foo/bar"). Mirrors HttpRouter.cpp.
        static bool nextSegment(const char*& cur, const char* end,
                                const char*& seg, size_t& seg_len) {
            if (cur >= end) return false;
            if (*cur != '/') return false;
            ++cur;   // consume the '/'
            seg = cur;
            while (cur < end && *cur != '/') ++cur;
            seg_len = static_cast<size_t>(cur - seg);
            return true;
        }

        SegmentID  root_id {NULL_SEGMENT_ID};
        SegmentPool _pool;
    };

}

#endif // __BN3MONKEY_SEGMENT_TRIE__
