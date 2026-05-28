#if !defined(__BN3MONKEY_TREE_NODE__ )
#define __BN3MONKEY_TREE_NODE__ 

#include <cstdint>
#include <vector>

namespace Bn3Monkey {
    

    template<typename EntityID>
    class EntityHook
    {
        EntityID parent_id;
        EntityID first_child_id;
        EntityID prev_sibling_id;
        EntityID next_sibling_id;
    };
    struct EntityArenaIndex {
        // Marker in `next` meaning: this slot is currently allocated (not in free list).
        static constexpr size_t SLOT_ALLOCATED {static_cast<size_t>(-1)};
        // Marker in `next` meaning: end of the free-list chain (no further free slot).
        static constexpr size_t FREE_LIST_END  {static_cast<size_t>(-2)};

        size_t next { FREE_LIST_END };
    };

    class FreeStack
    {
    private:
        std::vector<EntityArenaIndex> _data;
    };

    template<typename Entity, typename EntityID>
    class EntityPool
    {
        EntityPool(
            size_t (*fromEntityID)(EntityID),
            EntityID (*toEntityID)(size_t)
        ) :
            _fromEntityID(fromEntityID),
            _toEntityID(toEntityID)
            _arena(1024)
        {

        }
        

        template<typename... Args>
        EntityID allocate(Args&&... args) {
            uint32_t new_offset = offset + 1;
            tryGrow(new_offset);
            
            Entity* node = new (_arena.data() + new_offset) Entity{std::forward<Args>(args)...};
            
            
            offset = new_offset;
            return _toEntityID(offset);
        }

        Entity& get(EntityID id) {
            auto idx = _fromEntityID(id);
            return _arena[idx];
        }

    private:
        inline void tryGrow(uint32_t new_offset) {
            if (new_offset >= _arena.capacity())
                _arena.resize(_arena.size() << 1);
        }

        EntityID (*_toEntityID)(size_t index);
        size_t (*_fromEntityID)(EntityID id);
        std::vector<Entity> _arena;
        uint32_t offset {0};        
    };

    template<
        typename Entity, 
        typename EntityID, 
        EntityHook<EntityID> Entity::* EntityHookMember,
        EntityID NullID>
    class EntityCursor
    {
    public:
        
        EntityCursor(EntityPool<Entity, EntityID>& pool, EntityID id) : 
            _pool(pool), _id(id)
        {

        }

        void clear() {
            parentID() = NullID;
            firstChildID() = NullID;
            prevSiblingID() = NullID;
            nextSiblingID() = NullID;
        }

        EntityID id() const {
            return _id;
        }
        Entity& self() const {
            return _pool.get(_id);
        }
        EntityCursor parent() const {
            return EntityCursor(_pool, _pool.get(parentID()));
        }
        EntityCursor firstChild() const {
            return EntityCursor(_pool, _pool.get(firstChildID()));
        }
        EntityCursor prevSibling() const {
            return EntityCursor(_pool, _pool.get(prevSiblingID()));
        }
        EntityCursor nextSibling() const {
            return EntityCursor(_pool, _pool.get(nextSiblingID()));
        }
        bool isRoot() const {
            return parentID() == NullID;
        }
        bool isLeaf() const {
            return firstChildID() == NullID;
        }


        void attach(EntityID parent_id) {
            // 나의 parent_id를 parent의 id로 set
            // parent의 first_sibling을 나 자신으로 교체
        }   
        void detach() {
            // 1. 내가 first_sibling_id일 때
            // 2. first_sibling_id가 아닐 때
        }

        template<typename Fn>
        EntityID findChild(Fn fn) {
            // 구현좀
        }
        void appendChild(EntityID child_id) {
            auto child = EntityCursor{_pool, child_id};
            child.attach(_id);
        }
        void removeChild(EntityID child_id) {
            auto child = EntityCursor{_pool, child_id};
            child.detach(child_id);
        }



    private:
        inline EntityID& parentID() {
            return _node.EntityHookMember.parent_id;
        }
        inline EntityID& firstChildID() {
            return _node.EntityHookMember.first_child_id;
        }
        inline EntityID& prevSiblingID() {
            return _node.EntityHookMember.prev_sibling_id;
        }
        inline EntityID& nextSiblingID() {
            return _node.EntityHookMember.next_sibling_id;
        }


        EntityPool<Entity, EntityID>& _pool;
        EntityID _id;
    };
}

#endif // __BN3MONKEY_TREE_NODE__ 