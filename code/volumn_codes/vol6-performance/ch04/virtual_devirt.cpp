// virtual_devirt.cpp — vol6 ch04-04 / ch06-01 虚函数与去虚拟化
// 比较四种调用:普通虚函数 / final 类 / CRTP(静态多态)/ 编译器能证明类型时自动去虚化
// 编译: g++ -O2 -std=c++17 virtual_devirt.cpp -o virtual_devirt
// 跑:   taskset -c 0 ./virtual_devirt
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

// 基类 + 虚函数
struct Base {
    virtual int compute(int x) const = 0;
    virtual ~Base() = default;
};
struct Derived : Base {
    int compute(int x) const override { return x * 3 + 1; }
};
// final:告诉编译器没有更派生类,可去虚化
struct BaseF {
    virtual int compute(int x) const = 0;
    virtual ~BaseF() = default;
};
struct DerivedF final : BaseF {
    int compute(int x) const override { return x * 3 + 1; }
};
// CRTP:静态多态,编译期绑定,无虚表
template <class D> struct CrtpBase {
    int compute(int x) const { return static_cast<const D*>(this)->impl(x); }
};
struct CrtpDerived : CrtpBase<CrtpDerived> {
    int impl(int x) const { return x * 3 + 1; }
};

template <class F> double time_ns(F&& f, int repeat, int n) {
    f();
    f();
    auto t0 = clk::now();
    for (int r = 0; r < repeat; ++r)
        f();
    auto t1 = clk::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / repeat / n;
}

int main() {
    constexpr int N = 1 << 22;
    const int REP = 30;
    std::vector<int> in(N);
    for (int i = 0; i < N; ++i)
        in[i] = i;
    volatile uint64_t sink = 0;

    // 1. 普通虚函数(通过指针,编译器不能证明运行时类型)
    Base* b = new Derived();
    double t_virtual = time_ns(
        [&] {
            uint64_t s = 0;
            for (int x : in)
                s += b->compute(x);
            sink += s;
        },
        REP, N);

    // 2. final 类:编译器知道没有更派生,可去虚化
    BaseF* bf = new DerivedF();
    double t_final = time_ns(
        [&] {
            uint64_t s = 0;
            for (int x : in)
                s += bf->compute(x);
            sink += s;
        },
        REP, N);

    // 3. CRTP:静态多态,无虚表,可内联
    CrtpDerived cd;
    double t_crtp = time_ns(
        [&] {
            uint64_t s = 0;
            for (int x : in)
                s += cd.compute(x);
            sink += s;
        },
        REP, N);

    // 4. 直接对象(非指针/引用):编译器常能去虚化
    Derived d;
    double t_direct = time_ns(
        [&] {
            uint64_t s = 0;
            for (int x : in)
                s += d.compute(x);
            sink += s;
        },
        REP, N);

    do_not_optimize(sink);
    std::printf("===== 虚函数与去虚拟化(%d 次调用,平均 ns/次)=====\n", N);
    std::printf("  虚函数(指针,运行时多态):  %5.2f ns  ← 查 vtable + 间接跳转,阻碍内联\n",
                t_virtual);
    std::printf("  final 类(编译器去虚化):   %5.2f ns\n", t_final);
    std::printf("  直接对象(非指针,常去虚化):%5.2f ns\n", t_direct);
    std::printf("  CRTP(静态多态,无虚表):   %5.2f ns  ← 可内联\n", t_crtp);
    std::printf("  虚函数/CRTP = %.1fx\n", t_virtual / t_crtp);
    return 0;
}
