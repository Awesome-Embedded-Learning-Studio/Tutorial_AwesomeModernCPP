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
        // 但是！敢不敢移动还得看移动构造稳不稳，这就是 move_if_noexcept 的裁定：
        // 移动构造不抛，或者类型压根没有拷贝这条路，才敢把元素移动走。
        // 为什么这么怂？搬迁是搬一个杀一个，移动构造要是半路抛了，
        // 旧缓冲已经析构一半，新缓冲攥着半份，没人收得了场。
        // 所以移动可能抛、拷贝还健在的类型，老老实实退回拷贝，慢就慢，稳。
        constexpr bool move_is_safe =
            std::is_nothrow_move_constructible_v<Sources> || !std::is_copy_constructible_v<Sources>;
        for (Sources* p = from; p != from_end; ++p, ++to) {
            if constexpr (move_is_safe && std::is_move_constructible_v<Sources>) {
                new (to) Sources(std::move(*p));
            } else {
                new (to) Sources(*p);
            }
            p->~Sources();
        }
    }
}
} // namespace tamcpp::ministl::helper
