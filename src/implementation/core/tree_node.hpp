#if !defined(__TREE_NODE__)
#define __TREE_NODE__

#include <cstdint>

namespace Bn3Monkey {
    
    class TreeNodeLink
    {
        uint32_t parent_idx {0};
        uint32_t first_child_idx {0};

        uint32_t prev_sibling_idx {0};
        uint32_t next_sibling_idx {0};
    };

    template<typename Node>
    class TreeNodePool
    {
        template<typename... Args>
        uint32_t allocate(Args&&... args) {
            uint32_t new_offset = offset + 1;
            tryGrow(new_offset);
            
            Node* node = new (_arena.data() + new_offset) Node{std::forward<Args>(args)...};
            return offset;
        }

        Node& get(uint32_t idx) {
            return _arena[idx];
        }

    private:
        inline void tryGrow(uint32_t new_offset) {
            if (new_offset >= _arena.capacity())
                _arena.resize(_arena.size() << 1);
        }
        std::vector<Node> _arena(1024);
        uint32_t offset {0};        
    };

    template<typename Node, TreeNodeLink Node::* NodeLinkMember>
    class NodeHelper // 이름 추천 좀. Node의 위상 연산을 돕는 것임
    {
    public:
        NodeHelper(TreeNodePool<Node>& pool, Node& node) : _pool(pool), _node(node) {}

        void link(Node& parent);
        void unlink(Node& parent);
        void /*자기 부모를 계속 타고타고 가서 root까지 가서 수행하는 함수*/(void (*function)(Node& node)) {

        }

        void appendChild(Node& new_node);
        void removeChild(Node& new_node);
        template<typename Fn>
        Node& findChild(Fn fn) {

        }
        void forEachChild(void (*function)(Node& node)) {

        }


    private:
        TreeNodePool<Node>& _pool;
        Node& _node;
    }
}

#endif // __TREE_NODE__