#if !defined(__BN3MONKEY_SEGMENT_TRIE__)
#define __BN3MONKEY_SEGMENT_TRIE__

#include <vector>
#include <cstring>
#include <type_traits>

namespace Bn3Monkey
{
    enum class SegmentType {
        STATIC,
        PARAM
    };
    enum class ParamSegmentStyle {
        COLON,
        BRACE
    };

    struct Segment {
        SegmentType type;
        const char* content;
        size_t size;
    };

    class SegmentPool {
    public:
        constexpr static size_t INITIAL_ARENA_CAPACITY = 1024;
        SegmentPool(size_t capacity = INITIAL_ARENA_CAPACITY) :  _arena(capacity) {}
        
        Segment append(const char* segment) {
            return append(segment, strlen(segment));
        }
        Segment append(const char* segment, size_t size) {
            size_t next_offset = offset + size + 1;
            tryGrow(next_offset);
            auto* target = _arena.data() + offset;
            memcpy(target, segment, size);
            target[size] = '\0';
            
            next_offset = offset;
            return Segment { 
                isParamSegment(segment, size) ? SegmentType::PARAM : SegmentType::STATIC, 
                target, 
                size 
            };
        }

    private:
        static bool isParamSegment(const char* segment, size_t size, ParamSegmentStyle style = ParamSegmentStyle::COLON) {
            switch (style) {
                case ParamSegmentStyle::COLON :
                    return segment[0] == ':';                    
                case ParamSegmentStyle::BRACE:
                    return segment[0] == '{' && segment[size-1] == '}';
                default:
                    return false;
            }
        }
        inline void tryGrow(uint32_t new_offset) {
            if (new_offset >= _arena.capacity())
                _arena.resize(_arena.size() << 1);
        }
        std::vector<char> _arena;
        size_t offset;
    };

    template<typename Action>
    class SegmentTrie
    {
        
        struct Node {
            Segment segment;
            uint32_t parent_id;
            
            

        public:
            Node(SegmentPool& pool, const char* segment) : _segment(pool.append(segment)) {
            }
            Node(SegmentPool& pool, const char* segment, size_t size) : _segment(pool.append(segment, size)) {
            }

            inline const char* name() const { return _segment.content; }
            inline bool compare(const char* value) const {
                return compare(value, strlen(value));
            }
            inline bool compare(const char* value, size_t size) const {
                size_t len = _segment.size < size ? _segment.size : size;
                return memcmp(_segment.content, value, len);
            }
        };
        static_assert(std::is_trivially_copyable<Node>::value, "SegmentTrie Node should be trivially-copyable");

    private:

        Node _root;
        std::vector<Node> _node_pool;
        std::vector<Action> _action_pool;
    };
}

#endif // __BN3MONKEY_SEGMENT_TRIE__