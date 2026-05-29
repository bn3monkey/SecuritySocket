#if !defined(__BN3MONKEY__TREE_HOOK__)
#define __BN3MONKEY__TREE_HOOK__

#include "../traits/invocable.hpp"
#include <cstddef>

namespace Bn3Monkey
{
    // EntityID <-> 내부 인덱스 변환 및 실제 Entity 해석 인터페이스.
    // EntityPool 등 저장소가 상속하여 구현한다. (성능 측정상 CRTP와 차이 없어 virtual 사용)
    template<typename Entity, typename EntityID>
    class EntityResolver
    {
    public:
        virtual EntityID toEntityID(size_t index) const = 0;
        virtual size_t   fromEntityID(EntityID id) const = 0;
        // const/non-const pair: mutating walks (attach/addChild via the cursor)
        // resolve through the non-const overload; read-only walks (match)
        // resolve through the const one. No const_cast — the backing pool
        // exposes both a mutable and a const get().
        virtual Entity&       resolve(EntityID id)       = 0;
        virtual const Entity& resolve(EntityID id) const = 0;
        virtual ~EntityResolver() = default;
    };

    // Entity 에 박는 침습적(intrusive) 계층 링크.
    template<typename EntityID>
    struct EntityTreeHook
    {
        EntityID parent_id;
        EntityID first_child_id;
        EntityID prev_sibling_id;
        EntityID next_sibling_id;
    };

    template<
        typename Entity,
        typename EntityID,
        EntityTreeHook<EntityID> Entity::* EntityTreeHookMember,
        EntityID NullID>
    struct EntityTreeCursor
    {
        using Resolver = EntityResolver<Entity, EntityID>;

        EntityTreeCursor(Resolver& resolver, EntityID id) :
            _resolver(resolver),
            _id(id)
        {

        }

        EntityID id() const {
            return _id;
        }
        Entity& self() const {
            return _resolver.resolve(_id);
        }
        bool valid() const {
            return _id != NullID;
        }

        EntityTreeCursor root() const {
            EntityID r = _id;
            for (EntityID p = hookOf(r).parent_id; p != NullID; p = hookOf(r).parent_id)
                r = p;
            return EntityTreeCursor(_resolver, r);
        }
        EntityTreeCursor parent() const {
            return EntityTreeCursor(_resolver, parentID());
        }
        EntityTreeCursor firstChild() const {
            return EntityTreeCursor(_resolver, firstChildID());
        }
        EntityTreeCursor prevSibling() const {
            return EntityTreeCursor(_resolver, prevSiblingID());
        }
        EntityTreeCursor nextSibling() const {
            return EntityTreeCursor(_resolver, nextSiblingID());
        }

        bool isRoot() const {
            return parentID() == NullID;
        }
        bool isLeaf() const {
            return firstChildID() == NullID;
        }

        bool operator==(const EntityTreeCursor& cursor) const {
            return _id == cursor._id;
        }
        bool operator!=(const EntityTreeCursor& cursor) const {
            return _id != cursor._id;
        }

        // 현재 노드를 parent_id 의 자식 목록 맨 앞에 끼운다.
        // 전제: 현재 노드는 detach 상태(부모/형제 링크 없음)여야 한다.
        void attach(EntityID parent_id) {
            EntityTreeCursor parent(_resolver, parent_id);
            EntityID old_first = parent.firstChildID();

            // 나의 parent_id 를 parent 로 set, 형제 목록의 새 head 가 된다.
            parentID()      = parent_id;
            prevSiblingID() = NullID;
            nextSiblingID() = old_first;

            // 기존 first child 의 prev 를 나로 연결
            if (old_first != NullID)
                EntityTreeCursor(_resolver, old_first).prevSiblingID() = _id;

            // parent 의 first_child 를 나로 교체
            parent.firstChildID() = _id;
        }
        void detach() {
            EntityID p    = parentID();
            EntityID prev = prevSiblingID();
            EntityID next = nextSiblingID();

            if (prev != NullID) {
                // 2. first_sibling 이 아닐 때: 앞 형제의 next 를 내 next 로
                EntityTreeCursor(_resolver, prev).nextSiblingID() = next;
            } else if (p != NullID) {
                // 1. 내가 first_child 일 때: 부모의 first_child 를 내 next 로
                EntityTreeCursor(_resolver, p).firstChildID() = next;
            }
            if (next != NullID)
                EntityTreeCursor(_resolver, next).prevSiblingID() = prev;

            // 내 링크 초기화
            parentID()      = NullID;
            prevSiblingID() = NullID;
            nextSiblingID() = NullID;
        }

        // 술어 fn(Entity&) 를 만족하는 첫 자식의 id 를 반환. 없으면 NullID.
        template<typename Fn>
        EntityID findChild(Fn&& fn) {
            static_assert(is_invocable<Fn, Entity&>::value,
                "Callback should be called as fn(Entity&)");
            for (EntityID c = firstChildID(); c != NullID;
                 c = EntityTreeCursor(_resolver, c).nextSiblingID()) {
                if (fn(_resolver.resolve(c)))
                    return c;
            }
            return NullID;
        }
        // 현재 노드의 부모를 parent_id 로 옮긴다 (detach 후 attach).
        void moveTo(EntityID parent_id) {
            detach();
            attach(parent_id);
        }

        void addChild(EntityID child_id) {
            EntityTreeCursor child(_resolver, child_id);
            child.attach(_id);
        }
        void removeChild(EntityID child_id) {
            EntityTreeCursor child(_resolver, child_id);
            if (child != *this)
                child.detach();
        }

        template<typename Fn>
        void forEachChilds(Fn&& fn) {
            static_assert(is_invocable<Fn, Entity&>::value,
                "Callback should be called as fn(Entity&)");
            for (EntityID c = firstChildID(); c != NullID; ) {
                // fn 이 트리를 변경(detach 등)해도 안전하도록 next 를 미리 보관
                EntityID next = EntityTreeCursor(_resolver, c).nextSiblingID();
                fn(_resolver.resolve(c));
                c = next;
            }
        }
        template<typename Fn>
        void forEachAncestors(Fn&& fn) {
            static_assert(is_invocable<Fn, Entity&>::value,
                "Callback should be called as fn(Entity&)");
            for (EntityID p = parentID(); p != NullID; ) {
                EntityID next = EntityTreeCursor(_resolver, p).parentID();
                fn(_resolver.resolve(p));
                p = next;
            }
        }

        // 서브트리 전체를 pre-order DFS 로 순회 (자기 자신 제외).
        template<typename Fn>
        void forEachDescendants(Fn&& fn) {
            static_assert(is_invocable<Fn, Entity&>::value,
                "Callback should be called as fn(Entity&)");
            forEachDescendantsImpl(_id, fn);
        }
        // 서브트리에서 술어를 만족하는 첫 노드의 id 를 반환. 없으면 NullID.
        template<typename Fn>
        EntityID findDescendant(Fn&& fn) {
            static_assert(is_invocable<Fn, Entity&>::value,
                "Callback should be called as fn(Entity&)");
            return findDescendantImpl(_id, fn);
        }

    private:
        EntityTreeHook<EntityID>& hookOf(EntityID id) const {
            return _resolver.resolve(id).*EntityTreeHookMember;
        }
        EntityTreeHook<EntityID>& hook() const {
            return hookOf(_id);
        }

        template<typename Fn>
        EntityID findDescendantImpl(EntityID node, Fn& fn) {
            for (EntityID c = EntityTreeCursor(_resolver, node).firstChildID(); c != NullID;
                 c = EntityTreeCursor(_resolver, c).nextSiblingID()) {
                if (fn(_resolver.resolve(c)))
                    return c;
                EntityID hit = findDescendantImpl(c, fn);
                if (hit != NullID)
                    return hit;
            }
            return NullID;
        }
        template<typename Fn>
        void forEachDescendantsImpl(EntityID node, Fn& fn) {
            for (EntityID c = EntityTreeCursor(_resolver, node).firstChildID(); c != NullID; ) {
                // fn 이 트리를 변경해도 안전하도록 next 를 미리 보관
                EntityID next = EntityTreeCursor(_resolver, c).nextSiblingID();
                fn(_resolver.resolve(c));       // pre-order
                forEachDescendantsImpl(c, fn);  // 자식 서브트리
                c = next;
            }
        }
        EntityID& parentID() const {
            return hook().parent_id;
        }
        EntityID& firstChildID() const {
            return hook().first_child_id;
        }
        EntityID& prevSiblingID() const {
            return hook().prev_sibling_id;
        }
        EntityID& nextSiblingID() const {
            return hook().next_sibling_id;
        }

        Resolver& _resolver;
        EntityID _id;
    };
}

#endif // __BN3MONKEY__TREE_HOOK__
