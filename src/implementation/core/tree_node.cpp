#if !defined(__TREE_NODE__)
#define __TREE_NODE__

#include <cstdint>

namespace Bn3Monkey {
    
    class TreeNodeLink
    {
        uint32_t parent_idx;
        uint32_t first_child_idx;
        uint32_t next_sibling_idx;
    };

    template<typename T, TreeNodeLink T::* NodeLinkMember>
    class TreeNodePool
    {
        
    }
}

#endif // __TREE_NODE__