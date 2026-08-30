#pragma once

#include <cstring>
#include <type_traits>
#include <utility>

namespace tamcpp::ministl::helper {
template <typename Sources> inline void DestroySources(Sources* begin, Sources* end) {
    if constexpr (!std::is_trivially_destructible_v<Sources>) {
        for (Sources* index = begin; index < end; index++) {
            // Call Destructions，这里我们需要手动调用析构函数而不依赖编译器插桩
            index->~Sources();
        }
    }
}

template <typename Sources> inline void Relocate(Sources* from, Sources* from_end, Sources* to) {
    // 我们放掉那些干净的空东西，因为没必要
    if (from == from_end) {
        return;
    }
    // 如果我们的类型是平凡可拷贝的，比如说类似int等POD类型，直接拷贝
    if constexpr (std::is_trivially_copyable_v<Sources>) {
        std::memcpy(to, from, sizeof(Sources) * (from_end - from));
    } else {
        // 好，我们不能触发快速的拷贝，那就看能不能移动内存所属权
        // 如果不能，那就走最慢的路径
        for (Sources* p = from; p != from_end; ++p, ++to) {
            if constexpr (std::is_move_constructible_v<Sources>) {
                new (to) Sources(std::move(*p));
            } else {
                new (to) Sources(*p);
            }
            p->~Sources();
        }
    }
}
} // namespace tamcpp::ministl::helper
