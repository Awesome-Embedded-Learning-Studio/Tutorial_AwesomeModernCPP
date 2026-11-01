// tests/test_vector_move_if_noexcept.cpp —— 第二场 move_if_noexcept 的细验收
//
// 规格:扩容搬迁对单个元素的分派,对齐 std::move_if_noexcept 的裁定:
// 1) 元素移动构造可能抛、又能拷贝,搬迁必须走拷贝。grow_to 是搬一个杀一个,
//    移动半路抛出会让旧缓冲剩半份、新缓冲攥半份,漏析构全来了;
// 2) 元素移动构造不抛,搬迁走移动,例程 vector_move_if_noexcept.cpp 数的就是它;
// 3) 元素不可拷贝,没有退路,只能移动。
// 本文件眼下预期红:Relocate 的分派条件只看"能不能移动",没看"抛不抛"。
// 实现改对,它转绿,例程那句"noexcept 的真金白银"才算兑现。

#include <cassert>
#include <cstdio>
#include <type_traits>
#include <utility>

#include "tamcpp_ministl/vector.hpp"

namespace {

// 瓷器搬运工:移动构造没标 noexcept,等于向编译器承认可能抛
struct Fragile {
    static inline int alive = 0;
    static inline int copies = 0;
    static inline int moves = 0;
    explicit Fragile(int x = 0) : v(x) { ++alive; }
    Fragile(const Fragile& o) : v(o.v) {
        ++alive;
        ++copies;
    }
    Fragile(Fragile&& o) : v(o.v) {
        ++alive;
        ++moves;
    }
    ~Fragile() { --alive; }
    int v;
};

// 安稳搬运工:移动构造标了 noexcept,扩容才敢把家当整个托付给它
struct Stable {
    static inline int alive = 0;
    static inline int copies = 0;
    static inline int moves = 0;
    explicit Stable(int x = 0) : v(x) { ++alive; }
    Stable(const Stable& o) : v(o.v) {
        ++alive;
        ++copies;
    }
    Stable(Stable&& o) noexcept : v(o.v) {
        ++alive;
        ++moves;
    }
    ~Stable() { --alive; }
    int v;
};

// 独苗:拷贝被删,再怕抛也只有移动一条路
struct MoveOnly {
    static inline int alive = 0;
    static inline int copies = 0; // 恒为 0,放着纯为对齐读数
    static inline int moves = 0;
    explicit MoveOnly(int x = 0) : v(x) { ++alive; }
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& o) : v(o.v) {
        ++alive;
        ++moves;
    } // 同样不带 noexcept
    ~MoveOnly() { --alive; }
    int v;
};

} // namespace

// 先让类型系统自己作证,move_if_noexcept 对三种类型的裁定各不相同:
static_assert(std::is_same_v<decltype(std::move_if_noexcept(std::declval<Fragile&>())),
                             const Fragile&>); // 会抛 + 能拷 → 退化成 const 引用(拷贝)
static_assert(std::is_same_v<decltype(std::move_if_noexcept(std::declval<Stable&>())),
                             Stable&&>); // 不抛 → 原样右值引用(移动)
static_assert(std::is_same_v<decltype(std::move_if_noexcept(std::declval<MoveOnly&>())),
                             MoveOnly&&>); // 不可拷贝 → 没有退路,仍是移动

int main() {
    using tamcpp::ministl::Vector;

    // ---------- 1. 会抛的移动 + 可拷贝:搬迁必须退化成拷贝 ----------
    {
        Vector<Fragile> v;
        for (int i = 0; i < 9; ++i)
            v.emplace_back(i);       // 容量从 0 起步,中途必然至少搬迁一次
        assert(Fragile::copies > 0); // 一旦走移动,半路一抛就是两边各攥半份
        assert(Fragile::moves == 0);
        assert(Fragile::alive == 9);
        for (int i = 0; i < 9; ++i)
            assert(v[i].v == i); // 搬完不错位
    }
    assert(Fragile::alive == 0);

    // ---------- 2. 不抛的移动:搬迁走移动,一次拷贝都不许发生 ----------
    {
        Vector<Stable> v;
        for (int i = 0; i < 9; ++i)
            v.emplace_back(i);
        assert(Stable::copies == 0);
        assert(Stable::moves > 0); // 例程 vector_move_if_noexcept.cpp 的教学点
        assert(Stable::alive == 9);
        for (int i = 0; i < 9; ++i)
            assert(v[i].v == i);
    }
    assert(Stable::alive == 0);

    // ---------- 3. 不可拷贝:没有退路,只能移动 ----------
    {
        Vector<MoveOnly> v;
        for (int i = 0; i < 9; ++i)
            v.emplace_back(i);
        assert(MoveOnly::copies == 0);
        assert(MoveOnly::moves > 0);
        assert(MoveOnly::alive == 9);
        for (int i = 0; i < 9; ++i)
            assert(v[i].v == i);
    }
    assert(MoveOnly::alive == 0);

    std::puts("VECTOR MOVE_IF_NOEXCEPT GREEN");
    return 0;
}
