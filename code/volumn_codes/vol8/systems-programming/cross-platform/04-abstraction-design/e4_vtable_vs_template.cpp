// e4_vtable_vs_template.cpp —— 篇1 e4:运行期多态(虚表)与编译期多态(模板)的分派成本
//
// 策略模式(基类+虚函数)与 concepts 约束(模板静默多态)的选型之争,常被说成
// 性能之争。本实验一次把分派成本测清,口径沿用 ch00 总纲:一次系统调用约 120ns、
// 一次普通函数调用不足 1ns(那边各跑一千万次,这里同款手法)。
//
// 防去虚化的设计(否则 -O2 会把单态场景的虚调用直接消掉,量出假零):
//   - 两个派生类放进指针数组,循环里按 i&1 轮流指,动态类型编译期不可知
//   - 消费函数标 noinline,防止整场被内联合并
// 三个测量档 + 一个系统调用锚(getpid,只作对照锚,机制不重讲):
//   [A] indirect-virtual : 经基类指针的虚调用(策略模式的真实成本)
//   [B] static(template) : 模板实例化的直接调用(e2 丙场的 drain 路线)
//   [C] plain function   : 普通函数调用锚(对齐 ch00 的 plain call)
//   [D] getpid anchor    : 系统调用锚
//
// 编译口径:
//   Linux  : g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2
//            (另跑一版 -O0 存档对照;汇编证据用 objdump -d 抓 noinline 函数体)
//   Windows: /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <random>
#include <unistd.h>
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <process.h>
#    include <windows.h>
#endif

// ---- 运行期多态一侧:策略模式的形状 ----
class VirtualSource {
  public:
    virtual std::size_t read_some(void* buf, std::size_t n) = 0;
    virtual ~VirtualSource() = default;
};

class MemVirtualA final : public VirtualSource {
  public:
    std::size_t read_some(void* buf, std::size_t n) override {
        __builtin_memcpy(buf, kPayload, n < 16 ? n : 16);
        return n < 16 ? n : 16;
    }

  private:
    static inline char kPayload[16] = "payload-A-12345";
};

class MemVirtualB final : public VirtualSource {
  public:
    std::size_t read_some(void* buf, std::size_t n) override {
        __builtin_memcpy(buf, kPayload, n < 16 ? n : 16);
        return (n < 16 ? n : 16) + 1; // 与 A 略不同,防止编译器合并两个实现
    }

  private:
    static inline char kPayload[16] = "payload-B-12345";
};

// ---- 编译期多态一侧:同一形状,无虚表 ----
class MemStatic {
  public:
    std::size_t read_some(void* buf, std::size_t n) {
        __builtin_memcpy(buf, kPayload, n < 16 ? n : 16);
        return n < 16 ? n : 16;
    }

  private:
    static inline char kPayload[16] = "payload-S-12345";
};

// ---- 普通函数锚 ----
static std::size_t plain_read_some(void* buf, std::size_t n) {
    __builtin_memcpy(buf, "payload-P-12345", n < 16 ? n : 16);
    return n < 16 ? n : 16;
}

// ---- noinline 消费函数:分派发生在这里,不被内联吞掉 ----
#if defined(__GNUC__)
#    define NOINLINE __attribute__((noinline))
#else
#    define NOINLINE
#endif

NOINLINE static std::size_t consume_virtual(VirtualSource& s, void* buf) {
    return s.read_some(buf, 16); // 间接分派:call 走 vtable 槽
}

NOINLINE static std::size_t consume_static(MemStatic& s, void* buf) {
    return s.read_some(buf, 16); // 静态分派:直接 call,函数体内联于此
}

NOINLINE static std::size_t consume_plain(void* buf) {
    return plain_read_some(buf, 16);
}

NOINLINE static long consume_getpid() {
    return ::getpid(); // 系统调用锚(机制归 ch00,这里只借个对照)
}

#ifdef _WIN32
// Windows 侧的第二个锚:必进内核的真系统调用。getpid 在 UCRT 里疑似纯用户态
// (计时对拍见输出),拿它当"系统调用锚"会失真,这里放一个对照组定位。
NOINLINE static long consume_handle_count() {
    DWORD n = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &n);
    return static_cast<long>(n);
}
#endif

static double ns_per_op(auto&& fn) {
    constexpr int kCount = 20'000'000;
    volatile std::size_t sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i)
        sink += fn();
    auto t1 = std::chrono::steady_clock::now();
    (void)sink;
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / kCount;
}

int main() {
    MemVirtualA a;
    MemVirtualB b;
    VirtualSource* const pair[2] = {&a, &b}; // 轮流指,动态类型编译期不可知
    char buf[32];

    // 预热一拳
    for (int i = 0; i < 1000; ++i)
        (void)consume_virtual(*pair[i & 1], buf);

    int i = 0;
    const double v = ns_per_op([&] { return consume_virtual(*pair[(i++) & 1], buf); });

    // 乱序档:规则交替的 (i&1) 会被间接分支预测器学会,量出的是最好档;
    // 用固定 seed 的 mt19937 打乱目标序列,让预测器失效,量最坏档
    static int order[20'000'000];
    {
        std::mt19937 rng(20261005);
        for (auto& o : order)
            o = static_cast<int>(rng() & 1u);
    }
    int j = 0;
    const double vm = ns_per_op([&] { return consume_virtual(*pair[order[j++]], buf); });

    MemStatic s;
    const double t = ns_per_op([&] { return consume_static(s, buf); });
    const double p = ns_per_op([&] { return consume_plain(buf); });
    const double g = ns_per_op([&] { return consume_getpid(); });

    std::printf("[A] indirect-virtual(predictable) : %6.2f ns/op  (目标交替,BTB 全命中,最好档)\n",
                v);
    std::printf(
        "[A2] indirect-virtual(shuffled)    : %6.2f ns/op  (mt19937 乱序,预测失效,最坏档)\n", vm);
    std::printf("[B] static(template)               : %6.2f ns/op  (编译期实例化,直接调用)\n", t);
    std::printf("[C] plain function                 : %6.2f ns/op  (普通调用锚,对齐 ch00 口径)\n",
                p);
    std::printf("[D] getpid anchor                  : %6.2f ns/op  (系统调用锚)\n", g);
#ifdef _WIN32
    const double gh = ns_per_op([&] { return consume_handle_count(); });
    std::printf("[D2] GetProcessHandleCount anchor  : %6.2f ns/op  (必进内核的真系统调用对照)\n",
                gh);
    std::printf("virtual overhead vs plain : best %.2fx worst %.2fx   vs true-syscall : best "
                "%.0f%% worst %.0f%%\n",
                p > 0 ? v / p : 0.0, p > 0 ? vm / p : 0.0, gh > 0 ? 100.0 * v / gh : 0.0,
                gh > 0 ? 100.0 * vm / gh : 0.0);
#else
    std::printf("virtual overhead vs plain : best %.2fx worst %.2fx   vs syscall : best %.0f%% "
                "worst %.0f%%\n",
                p > 0 ? v / p : 0.0, p > 0 ? vm / p : 0.0, g > 0 ? 100.0 * v / g : 0.0,
                g > 0 ? 100.0 * vm / g : 0.0);
#endif
    return 0;
}
