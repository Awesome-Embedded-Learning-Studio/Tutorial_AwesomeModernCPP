// tests/test_vector.cpp —— Vector 接口黑盒测试
//
// 契约同 test_raw_buffer:只依据 vector.hpp 公开签名 + 教程承诺的行为;
// 不读实现、不迁就实现。断言失败 = 规格被违背,先怀疑实现。
// 全绿标准:打印 VECTOR ALL GREEN 正常退出,探测器零报告。

#include <cassert>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>

#include "tamcpp_ministl/vector.hpp"

namespace {

// 普查员:与 test_raw_buffer 同款,出生 +1 死亡 -1,账本不平立刻报警
struct Census {
    static inline int alive = 0;
    int v;
    explicit Census(int x = 0) : v(x) { ++alive; }
    Census(const Census& o) : v(o.v) { ++alive; }
    Census(Census&& o) noexcept : v(o.v) { ++alive; }
    ~Census() { --alive; }
};

} // namespace

// 移动俩必须不抛:第二场 move_if_noexcept 的入场券,承诺先钉死
static_assert(std::is_nothrow_move_constructible_v<tamcpp::ministl::Vector<int>>);
static_assert(std::is_nothrow_move_assignable_v<tamcpp::ministl::Vector<int>>);

int main() {
    using tamcpp::ministl::Vector;

    // ---------- 出生 ----------
    Vector<int> e;
    assert(e.empty() && e.size() == 0 && e.capacity() == 0);

    // ---------- init-list 构造 ----------
    Vector<int> v{1, 2, 3, 4};
    assert(v.size() == 4 && v.capacity() >= 4);
    assert(v[0] == 1 && v[3] == 4);

    // ---------- 追加 + 扩容:教程的下标账 ----------
    for (int i = 0; i < 100; ++i)
        v.push_back(i);
    assert(v.size() == 104);
    assert(v[50] == 46); // {1,2,3,4} 占了前 4 格,第 50 格装的是 46

    // ---------- 容量构造(工程自有扩展):只圈地、不生对象 ----------
    Vector<int> pre(8);
    assert(pre.capacity() == 8 && pre.size() == 0 && pre.empty());
    pre.push_back(7);
    assert(pre.size() == 1 && pre[0] == 7);

    // ---------- reserve:只涨不缩 ----------
    Vector<int> r;
    r.reserve(10);
    assert(r.capacity() == 10);
    r.reserve(5);
    assert(r.capacity() == 10);

    // ---------- emplace_back 原地构造 ----------
    Vector<std::string> s;
    s.emplace_back(5, 'x');
    s.push_back("hello");
    for (int i = 0; i < 50; ++i)
        s.push_back("filler");
    assert(s[0] == "xxxxx" && s[1] == "hello" && s.size() == 52);

    // ---------- 普查员:增、删、清、复用 ----------
    {
        Vector<Census> c;
        for (int i = 0; i < 100; ++i)
            c.emplace_back(i);
        assert(Census::alive == 100);
        c.pop_back();
        assert(Census::alive == 99 && c.size() == 99);
        c.pop_back();
        c.pop_back();
        assert(Census::alive == 97);
        c.clear();
        assert(Census::alive == 0 && c.empty());
        c.emplace_back(1); // clear 之后必须还能接着用
        assert(Census::alive == 1);
    }
    assert(Census::alive == 0);

    // ---------- 扩容搬迁:计数不增减、值不错位 ----------
    {
        Vector<Census> c;
        for (int i = 0; i < 33; ++i)
            c.emplace_back(i); // 4→8→16→32→64,扩了四轮
        assert(Census::alive == 33);
        for (int i = 0; i < 33; ++i)
            assert(c[i].v == i);
    }
    assert(Census::alive == 0);

    // ---------- swap:指针与计数整体对换 ----------
    {
        Vector<int> a{1, 2}, b{9, 8, 7};
        a.swap(b);
        assert(a.size() == 3 && a[2] == 7);
        assert(b.size() == 2 && b[1] == 2);
    }

    // ---------- 拷贝构造:等值 + 深拷贝 ----------
    Vector<int> origin{1, 2, 3};
    Vector<int> twin(origin);
    assert(twin.size() == 3 && twin[2] == 3);
    twin[0] = 100;
    assert(origin[0] == 1); // 改副本,原件不许动

    // ---------- 拷贝构造:普查员双份账 ----------
    {
        Vector<Census> c;
        for (int i = 0; i < 5; ++i)
            c.emplace_back(i);
        Vector<Census> d(c);
        assert(Census::alive == 10);
        assert(d[4].v == 4);
    }
    assert(Census::alive == 0);

    // ---------- 拷贝赋值:copy-and-swap ----------
    Vector<int> assigned{7};
    assigned = origin;
    assert(assigned.size() == 3 && assigned[0] == 1);
    Vector<int>& alias = assigned; // 换个名字拐弯,躲开 clangd 对故意自赋值的唠叨
    assigned = alias;              // 自赋值,规格要求安全
    assert(assigned.size() == 3 && assigned[0] == 1);

    // ---------- 移动构造:产权转移,moved-from 归零且可复用 ----------
    Vector<int> donor{1, 2, 3};
    Vector<int> heir(std::move(donor));
    assert(heir.size() == 3 && heir[0] == 1 && heir[2] == 3);
    assert(donor.empty() && donor.size() == 0 && donor.capacity() == 0);
    donor.push_back(42); // moved-from 是合法空对象,必须还能接着用
    assert(donor.size() == 1 && donor[0] == 42);

    // ---------- 移动赋值:内容改姓,旧家当清账 ----------
    Vector<int> looted{9};
    looted = std::move(heir);
    assert(looted.size() == 3 && looted[0] == 1);
    assert(heir.empty());
    Vector<int>& self_move = looted; // 自移动,照样拐弯躲唠叨
    looted = std::move(self_move);
    assert(looted.size() == 3 && looted[0] == 1);

    // ---------- 移动的账:换主人,不是换人 ----------
    {
        Vector<Census> a;
        for (int i = 0; i < 5; ++i)
            a.emplace_back(i);
        Vector<Census> b(std::move(a));
        assert(Census::alive == 5); // 移动构造不增减人口
        assert(a.size() == 0);

        Vector<Census> c;
        c.emplace_back(100);
        c.emplace_back(101);
        assert(Census::alive == 7);
        c = std::move(b); // c 原 2 个死,c 现在持有 b 的 5 个
        assert(Census::alive == 5);
        assert(c.size() == 5 && c[4].v == 4);
        assert(b.empty());
    }
    assert(Census::alive == 0);

    // ---------- visit_at:界内读写 ----------
    Vector<int> guard{1, 2, 3};
    guard.visit_at(1) = 20;
    assert(guard[1] == 20);
    const Vector<int>& cguard = guard;
    assert(cguard.visit_at(2) == 3);

    // ---------- resize:变大,默认构造补位 ----------
    Vector<int> big;
    big.resize(4); // 0 → 4,int 补的是零值
    assert(big.size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
        assert(big[i] == 0);
    big[3] = 9;
    big.resize(6);
    assert(big.size() == 6 && big[3] == 9 && big[5] == 0);

    // ---------- resize:等大,内容不许变 ----------
    Vector<int> same{1, 2, 3};
    same.resize(3);
    assert(same.size() == 3 && same[2] == 3);

    // ---------- resize:变小,杀尾巴、账要平、size 要跟着缩 ----------
    {
        Vector<Census> c;
        c.resize(5);
        assert(Census::alive == 5 && c.size() == 5);
        c.resize(2);
        assert(Census::alive == 2); // 账:杀掉 3 个
        assert(c.size() == 2);      // 账本上人数也得缩
        assert(c[1].v == 0);        // 幸存者前缀完好
    }
    assert(Census::alive == 0);

    // ---------- resize:缩小后再放大,新位是默认值 ----------
    Vector<std::string> vs;
    vs.resize(3);
    assert(vs.size() == 3 && vs[0].empty());
    vs[1] = "mid";
    vs.resize(1); // 杀掉 2 个 string(含 "mid")
    assert(vs.size() == 1 && vs[0].empty());
    vs.resize(2); // [1] 重新出生,是新的空串
    assert(vs.size() == 2 && vs[1].empty());

    std::puts("VECTOR ALL GREEN");
    return 0;
}
