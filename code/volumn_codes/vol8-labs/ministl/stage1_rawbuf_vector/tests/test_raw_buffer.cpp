// tests/test_raw_buffer.cpp —— RawBuffer 接口黑盒测试
//
// 契约:只依据 include/tamcpp_ministl/raw_buffer.hpp 的公开签名、
// 与教程为这一步承诺的行为来写;不看实现、不迁就实现。
// 断言失败 = 规格被违背,先怀疑实现,不怀疑测试。
//
// 全绿标准:打印 RAW BUFFER ALL GREEN 正常退出,
// 且 ASan/UBSan/LeakSan 零报告(CMake 默认带探测器)。

#include <cassert>
#include <cstdio>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "tamcpp_ministl/memory_helper.hpp"
#include "tamcpp_ministl/raw_buffer.hpp"

namespace {

// 普查员:活着的对象计数。搬迁漏杀、重复杀,总账立刻不平。
struct Census {
    static inline int alive = 0;
    int v;
    explicit Census(int x = 0) : v(x) { ++alive; }
    Census(const Census& o) : v(o.v) { ++alive; }
    Census(Census&& o) noexcept : v(o.v) { ++alive; }
    ~Census() { --alive; }
};

// 拷贝独占型:非平凡(手写拷贝构造)、禁移动 —— 搬迁只能走拷贝兜底档。
struct CopyOnly {
    int v;
    explicit CopyOnly(int x = 0) : v(x) {}
    CopyOnly(const CopyOnly& o) : v(o.v) {}
    CopyOnly(CopyOnly&&) = delete;
};

// 搬迁分档依据的是类型的 trait,先在编译期把三档的档位钉死:
static_assert(std::is_trivially_copyable_v<int>);
static_assert(!std::is_trivially_copyable_v<Census> && std::is_move_constructible_v<Census>);
static_assert(!std::is_trivially_copyable_v<CopyOnly> && !std::is_move_constructible_v<CopyOnly>);

} // namespace

int main() {
    using tamcpp::ministl::RawBuffer;

    // ---------- 出生状态 ----------
    RawBuffer<int> empty;
    assert(empty.capacity() == 0);
    assert(empty.data() == nullptr);

    RawBuffer<int> buf(8);
    assert(buf.capacity() == 8);
    assert(buf.data() != nullptr);

    // ---------- 搬迁第一档:平凡可拷贝(int 整块 memcpy) ----------
    for (int i = 0; i < 8; ++i)
        buf[i] = i * i;
    RawBuffer<int> wide(16);
    tamcpp::ministl::helper::Relocate(buf.data(), buf.data() + 8, wide.data());
    for (int i = 0; i < 8; ++i)
        assert(wide[i] == i * i);

    // ---------- 搬迁:空区间 + 双空指针(空数组首扩容)不许炸 ----------
    RawBuffer<int> first_growth(4);
    tamcpp::ministl::helper::Relocate<int>(nullptr, nullptr, first_growth.data());

    // ---------- 移动构造:产权转移,moved-from 归零 ----------
    RawBuffer<int> stolen = std::move(wide);
    assert(stolen.capacity() == 16);
    assert(stolen[5] == 25); // 内容跟着指针走
    assert(wide.capacity() == 0);
    assert(wide.data() == nullptr);

    // ---------- 移动赋值:内容存活;旧块归还由 LeakSan 退出时对账 ----------
    RawBuffer<int> target(2);
    target = std::move(stolen);
    assert(target.capacity() == 16);
    assert(target[5] == 25);
    assert(stolen.capacity() == 0);

    const RawBuffer<int>& cview = target; // const 两条路也得通
    assert(cview[5] == 25);
    assert(cview.visit_at(5) == 25);

    // ---------- 自移动赋值:头文件注释里承诺的无操作 ----------
    RawBuffer<int>& self = target; // 换个名字拐弯,躲开 -Wself-move 对故意的自移动唠叨
    target = std::move(self);
    assert(target.capacity() == 16);
    assert(target[5] == 25);

    // ---------- visit_at:界内就是带检查的下标 ----------
    RawBuffer<int> v(4);
    v.visit_at(2) = 99;
    assert(v.visit_at(2) == 99);

    // ---------- 搬迁第二档:std::string 走移动构造,内容必须完好 ----------
    RawBuffer<std::string> s_src(4), s_dst(4);
    new (s_src.data() + 0) std::string("hello");
    new (s_src.data() + 1) std::string(5, 'x');
    tamcpp::ministl::helper::Relocate(s_src.data(), s_src.data() + 2, s_dst.data());
    assert(s_dst[0] == "hello");
    assert(s_dst[1] == "xxxxx");
    tamcpp::ministl::helper::DestroySources(s_dst.data(), s_dst.data() + 2);

    // ---------- 搬迁 + 析构:普查员对账 ----------
    {
        RawBuffer<Census> a(8), b(8);
        for (int i = 0; i < 5; ++i)
            new (a.data() + i) Census(i);
        assert(Census::alive == 5);
        tamcpp::ministl::helper::Relocate(a.data(), a.data() + 5, b.data());
        assert(Census::alive == 5); // 搬迁是换址,不是增员减员
        for (int i = 0; i < 5; ++i)
            assert(b[i].v == i);
        tamcpp::ministl::helper::DestroySources(b.data(), b.data() + 5);
        assert(Census::alive == 0);
    }

    // ---------- 搬迁第三档:禁移动类型,拷贝兜底 ----------
    {
        RawBuffer<CopyOnly> a(4), b(4);
        for (int i = 0; i < 3; ++i)
            new (a.data() + i) CopyOnly(i);
        tamcpp::ministl::helper::Relocate(a.data(), a.data() + 3, b.data());
        for (int i = 0; i < 3; ++i)
            assert(b[i].v == i);
        tamcpp::ministl::helper::DestroySources(b.data(), b.data() + 3);
    }

    // ---------- 析构:平凡可析构是规定的不作为,不许炸 ----------
    RawBuffer<int> t(4);
    for (int i = 0; i < 4; ++i)
        t[i] = i;
    tamcpp::ministl::helper::DestroySources(t.data(), t.data() + 4);
    assert(t[2] == 2);

    std::puts("RAW BUFFER ALL GREEN");
    return 0;
}
