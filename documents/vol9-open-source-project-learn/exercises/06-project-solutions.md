---
title: "卷 9 Project 参考实现（mini_chrome）"
description: "卷 9 综合项目 mini_chrome 的完整参考实现：四层任务（核心回调库、自动取消与命令分发、质量门、无锁消息环与编译期分派）按层组织、逐段讲解，全部在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真编译真跑，附 ASan/UBSan/TSan 零报告与编译失败验证。"
chapter: 9
order: 6
tags:
  - host
  - intermediate
  - cpp-modern
  - 回调机制
  - weak_ptr
  - map
  - 内存管理
difficulty: intermediate
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 27
prerequisites:
  - "卷 9 Project 题面"
related:
  - "卷 9 Lab 实验参考"
---

# 卷 9 Project 参考实现（mini_chrome）

> 所有输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到。参考实现按层组织，卡在哪层就读哪层；`include/` 下的四个头文件（`mini_once_callback.hpp` / `mini_weak_ptr.hpp` / `mini_flat_map.hpp` / `mini_no_destructor.hpp`）与 [Lab 实验参考](04-lab-solutions.md) 末尾「共享头文件四件套」**逐字相同**，这里不再重复贴出，本页只列 Project 新增的每个文件。工程目录：

```text
/tmp/mini_chrome/
├── include/    # 四件套头文件(见 Lab 参考)+ mini_test.hpp
└── src/        # core_demo.cpp / adv_demo.cpp / gates_test.cpp / gates_tsan.cpp / l5_main.cpp
```

## 核心任务（L2）：能跑起来的回调库 {#pj-core}

**L1 热身**：先写 `stub_once.hpp` 只放声明（`OnceCallback` 偏特化的声明 + `CancelableToken` 的前向式声明），配空 `main`，只求 `-Wall -Wextra -c` 零警告。热身的意义在于逼您先把"这个类长什么样"想清楚，再动手填实现。→ 知识点：[once_callback 设计指南（一）：动机与接口设计](../chrome/01_once_callback/hands_on/01-once-callback-design.md)「设计接口:咱们想要什么样的 API」一节

热身过后把声明填成 Lab 那份完整的 `mini_once_callback.hpp`（骨架 + `bind_once` + 取消令牌全量），然后用五个场景验收。五个场景各钉一条不变量：非 void 的 `if constexpr` 分支、void 分支、move-only 底层存储、绑定/运行时参数顺序、取消时"响亮失败"。→ 知识点：[once_callback 设计指南（三）：测试策略与性能对比](../chrome/01_once_callback/hands_on/03-once-callback-testing.md)「按『不变量』切测试」一节、[OnceCallback 实战（四）：取消令牌设计](../chrome/01_once_callback/full/01-4-once-callback-cancellation-token.md)「void 和非 void 回调,取消时为啥不一样」一节

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude src/core_demo.cpp -o core_demo.out && ./core_demo.out
1 non-void      : 7
2 void          : called=1
3 move-only     : 42
4a bind lambda  : 60
4b bind member  : 40
5 cancel        : caught std::bad_function_call
```

`src/core_demo.cpp` 全文：

```cpp
#include "mini_once_callback.hpp"
#include <cstdio>
#include <memory>

using namespace tamcpp::chrome;

struct Calc {
    int multiply(int a, int b) { return a * b; }
};

int main() {
    // 1 非 void 返回
    OnceCallback<int(int, int)> add([](int a, int b) { return a + b; });
    std::printf("1 non-void      : %d\n", std::move(add).run(3, 4));

    // 2 void 返回
    bool called = false;
    OnceCallback<void()> side([&called] { called = true; });
    std::move(side).run();
    std::printf("2 void          : called=%d\n", called ? 1 : 0);

    // 3 move-only 捕获
    auto p = std::make_unique<int>(42);
    OnceCallback<int()> cap([p = std::move(p)] { return *p; });
    std::printf("3 move-only     : %d\n", std::move(cap).run());

    // 4 bind_once:普通 lambda 部分绑定 + 成员函数绑定
    auto bound = bind_once<int(int)>([](int x, int y, int z) { return x + y + z; }, 10, 20);
    std::printf("4a bind lambda  : %d\n", std::move(bound).run(30));
    Calc calc;
    auto mbound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
    std::printf("4b bind member  : %d\n", std::move(mbound).run(8));

    // 5 取消的非 void 回调 → std::bad_function_call
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<int()> cb([] { return 1; });
    cb.set_token(token);
    token->invalidate();
    try {
        std::move(cb).run();
        std::printf("5 cancel        : no exception(?!)\n");
    } catch (const std::bad_function_call&) {
        std::printf("5 cancel        : caught std::bad_function_call\n");
    }
    return 0;
}
```

## 进阶任务（L3）：自动取消 + 命令分发 {#pj-adv}

**思路**：两个组件在 Lab 里都齐了，这层的功夫在**组装**。一个细节必须想清楚：OnceCallback 是单次消费的，而命令要能重复执行——所以分发表里存的是"回调工厂"：每次执行现绑一枚 weak 回调，对象死后工厂绑出的回调自动变 no-op。这样"只能跑一次"由 OnceCallback 保证，"死后作废"由 WeakPtr 保证，两条语义各归其位。→ 知识点：[WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环](../chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md)「闭环:01-4 手搓令牌 vs 工业 WeakPtr」一节、[flat_map 实战（一）：动机与接口设计](../chrome/03_flat_map/full/03-1-flat-map-motivation-and-api-design.md)「从一个性能痛点说起:配置表」一节

**验证输出**（会话 `inc, inc, report, dec, report, quit`）：

```text
$ g++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude src/adv_demo.cpp -o adv_demo.out
$ printf 'inc\ninc\nreport\ndec\nreport\nquit\n' | ./adv_demo.out
count=2
count=1
dead callback: (nothing above = silent no-op)
```

`src/adv_demo.cpp` 全文：

```cpp
#include "mini_once_callback.hpp"
#include "mini_flat_map.hpp"
#include "mini_weak_ptr.hpp"
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace tamcpp::chrome;

// bind_weak_once:成员方法 + WeakPtr receiver → OnceCallback<void()>
template <typename T, typename... Bound>
auto bind_weak_once(void (T::*method)(Bound...),
                     WeakPtr<T> receiver,
                     Bound... bound) {
    return OnceCallback<void()>(
        [method, receiver = std::move(receiver),
         bound = std::make_tuple(std::move(bound)...)]() mutable {
            if (!receiver) return;   // 取消点:对象死后静默 no-op
            std::apply(
                [&](auto&&... args) { (receiver.get()->*method)(args...); },
                bound);
        });
}

struct Controller {
    void inc() { ++v_; }
    void dec() { --v_; }
    void report() { std::printf("count=%d\n", v_); }
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
private:
    int v_ = 0;
    WeakPtrFactory<Controller> weak_factory_{this};   // 最后成员
};

int main() {
    Controller c;

    // 命令分发表:命令 → 回调工厂。
    // OnceCallback 是单次消费的,而命令要能重复执行,所以每次执行现绑一枚 weak 回调;
    // 对象死后工厂绑出的回调自动变 no-op——"只能跑一次"由 OnceCallback 保证,
    // "死后作废"由 WeakPtr 保证。
    flat_map<std::string, std::function<OnceCallback<void()>()>> dispatch;
    dispatch.insert_or_assign("inc",
        [wp = c.get_weak()] { return bind_weak_once(&Controller::inc, wp); });
    dispatch.insert_or_assign("dec",
        [wp = c.get_weak()] { return bind_weak_once(&Controller::dec, wp); });
    dispatch.insert_or_assign("report",
        [wp = c.get_weak()] { return bind_weak_once(&Controller::report, wp); });

    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        if (cmd == "quit") break;
        auto it = dispatch.find(cmd);
        if (it == dispatch.end()) {
            std::printf("unknown command: %s\n", cmd.c_str());
            continue;
        }
        std::move(it->second()).run();
    }

    // 死亡演示:持有一个已死对象的 WeakPtr 绑出的回调,静默 no-op
    {
        WeakPtr<Controller> dead_wp;
        {
            Controller dead;
            dead_wp = dead.get_weak();
        }   // dead 析构 → factory 失效
        auto dead_task = bind_weak_once(&Controller::report, dead_wp);
        std::printf("dead callback: ");
        std::move(dead_task).run();   // 什么都不打印
        std::printf("(nothing above = silent no-op)\n");
    }
    return 0;
}
```

## 再进阶任务（L4）：把门装上 {#pj-gates}

**思路**：零依赖迷你测试框架 + 按不变量组织的用例 + 三把 sanitizer。不变量清单照三个测试篇抄：OnceCallback"移动不消费、run 才消费"、WeakPtr"一次失效所有"与"was_invalidated 区分作废/reset"、flat_map"升序无重复 + 拒重复插入"、NoDestructor"构造一次析构永不跑"。→ 知识点：[OnceCallback 实战（六）：测试与性能对比](../chrome/01_once_callback/full/01-6-once-callback-testing-and-perf.md)、[WeakPtr 实战（六）：测试与性能对比](../chrome/02_weak_ptr/full/02-6-weak-ptr-testing-and-perf.md)、[flat_map 实战（六）：测试与性能对比](../chrome/03_flat_map/full/03-6-flat-map-testing-and-perf.md)（三个测试篇共通的"不变量驱动"方法论）

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror -Iinclude src/gates_test.cpp -o gates_test.out && ./gates_test.out
-- OnceCallback: 移动不消费,run 才消费 --
  [PASS] cb.is_null()
  [PASS] !(moved.is_cancelled())
  [PASS] moved.is_cancelled()
-- WeakPtr: invalidate 一次失效所有;was_invalidated 区分作废与 reset --
  [PASS] static_cast<bool>(wp1)
  [PASS] wp1.get() == static_cast<Foo*>(nullptr)
  [PASS] wp2.get() == static_cast<Foo*>(nullptr)
  [PASS] wp1.was_invalidated()
  [PASS] !(wp3.was_invalidated())
-- flat_map: 构造后严格升序且无重复;insert 拒重复 --
  [PASS] m.size() == static_cast<std::size_t>(3)
  [PASS] keys == std::vector<int>({1, 2, 3})
  [PASS] ok1
  [PASS] !(ok2)
-- NoDestructor: 构造一次、析构永不跑 --
  [PASS] Noisy::ctor == 1
  [PASS] Noisy::dtor == 0
  [PASS] Noisy::ctor == 1
  [PASS] Noisy::dtor == 0

passed=16 failed=0

$ g++ -std=c++23 -Wall -Wextra -fsanitize=address,undefined -g -O1 -Iinclude src/gates_test.cpp -o gates_test_asan.out
$ ASAN_OPTIONS=detect_leaks=0 ./gates_test_asan.out | tail -1
passed=16 failed=0    ← ASan/UBSan 零报告

$ g++ -std=c++23 -Wall -Wextra -fsanitize=address,undefined -g -O1 -Iinclude src/adv_demo.cpp -o adv_demo_asan.out
$ printf 'inc\nreport\nquit\n' | ASAN_OPTIONS=detect_leaks=0 ./adv_demo_asan.out
count=1
dead callback: (nothing above = silent no-op)    ← ASan/UBSan 零报告

$ g++ -std=c++23 -Wall -Wextra -fsanitize=thread -O1 -g -Iinclude src/gates_tsan.cpp -o gates_tsan.out && ./gates_tsan.out
magic statics ctor=1
posted/done executed=100000    ← TSan 零报告
```

`include/mini_test.hpp` 全文：

```cpp
#pragma once
#include <cstdio>

namespace mini_test {
inline int passed = 0;
inline int failed = 0;

inline void report(bool ok, const char* expr, const char* file, int line) {
    if (ok) {
        ++passed;
        std::printf("  [PASS] %s\n", expr);
    } else {
        ++failed;
        std::printf("  [FAIL] %s (%s:%d)\n", expr, file, line);
    }
}
}  // namespace mini_test

#define EXPECT_TRUE(cond)  ::mini_test::report((cond), #cond, __FILE__, __LINE__)
#define EXPECT_FALSE(cond) ::mini_test::report(!(cond), "!(" #cond ")", __FILE__, __LINE__)
#define EXPECT_EQ(a, b)    ::mini_test::report((a) == (b), #a " == " #b, __FILE__, __LINE__)
```

`src/gates_test.cpp` 全文（`Noisy` 要放命名空间作用域——局部类不能有静态数据成员，这是本层踩过的第一个小坑；16 个断言与上面的验证输出一一对应）：

```cpp
#include "mini_once_callback.hpp"
#include "mini_weak_ptr.hpp"
#include "mini_flat_map.hpp"
#include "mini_no_destructor.hpp"
#include "mini_test.hpp"
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace tamcpp::chrome;

// Noisy 放命名空间作用域:局部类不能有静态数据成员
namespace {
struct Noisy {
    static int ctor;
    static int dtor;
    Noisy() { ++ctor; }
    ~Noisy() { ++dtor; }
};
int Noisy::ctor = 0;
int Noisy::dtor = 0;

struct Foo {
    int x = 42;
};

NoDestructor<Noisy>& GetNoisy() {
    static NoDestructor<Noisy> n;   // 构造一次、析构永不跑
    return n;
}
}  // namespace

int main() {
    std::puts("-- OnceCallback: 移动不消费,run 才消费 --");
    {
        OnceCallback<int()> cb([] { return 1; });
        OnceCallback<int()> moved = std::move(cb);
        EXPECT_TRUE(cb.is_null());               // 移动后源对象变空
        EXPECT_FALSE(moved.is_cancelled());      // 移动只是搬家,没消费
        std::move(moved).run();
        EXPECT_TRUE(moved.is_cancelled());       // run 之后才是已消费
    }

    std::puts("-- WeakPtr: invalidate 一次失效所有;was_invalidated 区分作废与 reset --");
    {
        Foo foo;
        WeakPtrFactory<Foo> factory(&foo);
        WeakPtr<Foo> wp1 = factory.get_weak_ptr();
        WeakPtr<Foo> wp2 = factory.get_weak_ptr();
        EXPECT_TRUE(static_cast<bool>(wp1));
        factory.invalidate_weak_ptrs();          // 一次失效所有
        EXPECT_EQ(wp1.get(), static_cast<Foo*>(nullptr));
        EXPECT_EQ(wp2.get(), static_cast<Foo*>(nullptr));
        EXPECT_TRUE(wp1.was_invalidated());

        Foo bar;
        WeakPtrFactory<Foo> factory2(&bar);
        WeakPtr<Foo> wp3 = factory2.get_weak_ptr();
        wp3.reset();                             // 主动 reset,不是"被作废"
        EXPECT_FALSE(wp3.was_invalidated());
    }

    std::puts("-- flat_map: 构造后严格升序且无重复;insert 拒重复 --");
    {
        flat_map<int, std::string> m{std::vector<std::pair<int, std::string>>{
            {3, "c"}, {1, "a"}, {2, "b"}, {1, "dup"}}};
        EXPECT_EQ(m.size(), static_cast<std::size_t>(3));
        std::vector<int> keys;
        for (auto it = m.begin(); it != m.end(); ++it) keys.push_back(it->first);
        EXPECT_EQ(keys, std::vector<int>({1, 2, 3}));
        auto [it, ok1] = m.insert({4, "d"});
        EXPECT_TRUE(ok1);
        auto [it2, ok2] = m.insert({4, "again"});
        EXPECT_FALSE(ok2);
    }

    std::puts("-- NoDestructor: 构造一次、析构永不跑 --");
    {
        (void)GetNoisy();
        EXPECT_EQ(Noisy::ctor, 1);
        EXPECT_EQ(Noisy::dtor, 0);
        (void)GetNoisy();                        // 第二次:不重新构造
        EXPECT_EQ(Noisy::ctor, 1);
        EXPECT_EQ(Noisy::dtor, 0);
    }

    std::printf("\npassed=%d failed=%d\n", mini_test::passed, mini_test::failed);
    return mini_test::failed == 0 ? 0 : 1;
}
```

两个实测提醒：`WeakPtr` 的 `operator bool` 是 `explicit` 的，`EXPECT_TRUE(wp)` 要写成 `EXPECT_TRUE(static_cast<bool>(wp))`；NoDestructor 的 LSan 行为按 Lab 步骤 6 的实验结论如实记录——本卷工具链上不误报，无需 suppression，若您的环境出现误报再压。两个 ASan 构建（gates_test 与 adv_demo）都显式 `ASAN_OPTIONS=detect_leaks=0`：本卷工具链 LSan 默认开启且不误报 NoDestructor（Lab 步骤 6 的实测结论），这两个程序也都没有真泄漏——不写这句默认跑同样零报告；这里显式关掉只是把本构建的验收口径固定成「只验 memory errors（UAF/越界/UBSan）」，泄漏判定统一以 Lab 步骤 6 的专门实验为准。

## 终极挑战（L5）：无锁消息环 + 编译期分派 {#pj-l5}

**思路**：两件挑战。①编译期 weak 分派把 Homework 9.C-2 的三块拼图（`IsWeakReceiver` / `kIsWeakMethod` / 强制 void）整件搬进来，`bind` 一个入口在编译期选 `InvokeHelper<true/false>` 分支。②无锁消息环把 Lab L5 的侵入式零分配队列接上**两道关**——题面那句"每个任务先过 WeakPtr 判活再过取消令牌"就在这里兑现：`Task` 多一个 `weak` 成员，消费循环两道 `if` 依次放行（WeakPtr 在前、令牌在后），10% 任务挂已失效令牌、10% 挂已死对象的 WeakPtr，最终 80000 一分不差；再用 4×25 000 任务压测 + TSan 自证清白，最后和"互斥锁 + deque"对表。→ 知识点：[WeakPtr 实战（五）](../chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md)（编译期接线全文）、[WeakPtr 前置知识（二）](../chrome/02_weak_ptr/full/pre-02-weak-ptr-atomic-and-memory-order.md)（每个内存序的账）

**验证输出**（三份构建全绿 + 编译失败用例）：

```text
$ g++ -std=c++23 -Wall -Wextra -O2 -Iinclude src/l5_main.cpp -o l5_main.out && ./l5_main.out
bind: hits=2
mpsc : executed=80000 expected=80000  4 ms
mutex: executed=80000 expected=80000  24 ms

$ g++ -std=c++23 -Wall -Wextra -fsanitize=thread -O1 -g -Iinclude src/l5_main.cpp -o l5_main_tsan.out && ./l5_main_tsan.out
bind: hits=2
mpsc : executed=80000 expected=80000  38 ms
mutex: executed=80000 expected=80000  323 ms    ← TSan 全程零报告

$ g++ -std=c++23 -Wall -Wextra -fsanitize=address,undefined -O1 -g -Iinclude src/l5_main.cpp -o l5_main_asan.out
$ ASAN_OPTIONS=detect_leaks=0 ./l5_main_asan.out
bind: hits=2
mpsc : executed=80000 expected=80000  6 ms
mutex: executed=80000 expected=80000  75 ms    ← ASan/UBSan 全程零报告（detect_leaks=0 口径同 L4）

$ g++ -std=c++23 -Wall -Wextra -Iinclude src/l5_bad.cpp -o l5_bad.out
src/l5_bad.cpp: In instantiation of 'std::function<void()> bind(M, Receiver&&, Bound&& ...) [with M = int (Controller::*)(); Receiver = tamcpp::chrome::WeakPtr<Controller>; Bound = {}]':
src/l5_bad.cpp:88:21:   required from here
   88 |     auto task = bind(&Controller::get_value, c.get_weak());   // 应编译失败
      |                 ~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
src/l5_bad.cpp:69:28: error: static assertion failed: weak call must return void: cancellation has no value to return
   69 |     static_assert(!is_weak || std::is_void_v<typename method_traits<M>::Ret>,
      |                   ~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  • '((!(bool)is_weak) || ((bool)std::is_void_v<int>))' evaluates to false
```

`src/l5_main.cpp` 核心（编译期分派 + 侵入式队列；完整压测驱动与 Homework 9.C-3 同构，多了两道关）：

```cpp
// ============ 1. 编译期 weak 分派(bind_internal.h 复刻)============
template <template <typename...> class Template, typename T>
struct is_instantiation_of : std::false_type {};

template <template <typename...> class Template, typename... Params>
struct is_instantiation_of<Template, Template<Params...>> : std::true_type {};

template <typename T>
struct IsWeakReceiver : is_instantiation_of<WeakPtr, T> {};

static_assert(IsWeakReceiver<WeakPtr<int>>::value);
static_assert(!IsWeakReceiver<int*>::value);

template <bool is_method, typename... Args>
inline constexpr bool kIsWeakMethod = false;
template <typename T, typename... Args>
inline constexpr bool kIsWeakMethod<true, T, Args...> = IsWeakReceiver<std::decay_t<T>>::value;

template <typename Sig>
struct method_traits;
template <typename R, typename T, typename... Args>
struct method_traits<R (T::*)(Args...)> {
    using Ret = R;
    using Class = T;
};

template <bool is_weak_call>
struct InvokeHelper;

template <>
struct InvokeHelper<true> {
    template <typename M, typename Receiver, typename Tuple>
    static void make_it_so(M method, const Receiver& receiver, Tuple& bound) {
        // 先 Unwrap(取出 receiver),再判活——顺序不能反
        if (!receiver) return;   // 取消点
        std::apply(
            [&](auto&&... args) {
                (receiver.get()->*method)(std::forward<decltype(args)>(args)...);
            },
            bound);
    }
};

template <>
struct InvokeHelper<false> {
    template <typename M, typename Receiver, typename Tuple>
    static void make_it_so(M method, Receiver receiver, Tuple& bound) {
        std::apply(
            [&](auto&&... args) {
                (receiver->*method)(std::forward<decltype(args)>(args)...);
            },
            bound);
    }
};

template <typename M, typename Receiver, typename... Bound>
std::function<void()> bind(M method, Receiver&& receiver, Bound&&... bound) {
    using DecayedReceiver = std::decay_t<Receiver>;
    constexpr bool is_weak = kIsWeakMethod<true, DecayedReceiver>;

    static_assert(!is_weak || std::is_void_v<typename method_traits<M>::Ret>,
                  "weak call must return void: cancellation has no value to return");

    return [method, receiver = std::forward<Receiver>(receiver),
            bound = std::make_tuple(std::forward<Bound>(bound)...)]() mutable {
        InvokeHelper<is_weak>::make_it_so(method, receiver, bound);
    };
}

// ============ 2. 无锁消息环:侵入式零分配 MPSC(Vyukov 改编)============
struct Controller {
    void bump() { ++hits; }
    int hits = 0;
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
private:
    WeakPtrFactory<Controller> weak_factory_{this};   // 最后成员
};

struct Task {
    std::atomic<Task*> next{nullptr};
    std::move_only_function<void()> fn;
    std::shared_ptr<CancelableToken> token;   // 第二道关:取消令牌
    WeakPtr<Controller> weak;                 // 第一道关:WeakPtr 判活
};

class IntrusiveMpsc {
public:
    IntrusiveMpsc() : head_(&stub_), tail_(&stub_) {
        stub_.next.store(nullptr, std::memory_order_relaxed);
    }

    void push(Task* n) {
        n->next.store(nullptr, std::memory_order_relaxed);
        Task* prev = head_.exchange(n, std::memory_order_acq_rel);
        prev->next.store(n, std::memory_order_release);
    }

    Task* pop() {
        Task* t = tail_;
        Task* next = t->next.load(std::memory_order_acquire);
        if (t == &stub_) {
            if (next == nullptr) return nullptr;
            tail_ = next;
            t = next;
            next = t->next.load(std::memory_order_acquire);
        }
        if (next != nullptr) {
            tail_ = next;
            return t;
        }
        Task* h = head_.load(std::memory_order_acquire);
        if (t != h) return nullptr;
        push(&stub_);
        next = t->next.load(std::memory_order_acquire);
        if (next != nullptr) {
            tail_ = next;
            return t;
        }
        return nullptr;
    }

private:
    std::atomic<Task*> head_;
    Task* tail_ = nullptr;
    Task stub_;
};
```

消费循环核心（压测驱动与 Homework 9.C-3 同构：4 生产线程 × 25 000 任务，预构造时 10% 挂已失效令牌、10% 挂已死对象的 WeakPtr，预期 80000）：

```cpp
// 消费线程:两道关依次放行,顺序与题面一致——先 WeakPtr 判活,再取消令牌
while (popped < kTotal) {
    Task* n = q.pop();
    if (n == nullptr) continue;              // 空 / push 在途:再试
    if (!n->weak) { ++popped; continue; }    // ① 先过 WeakPtr 判活:对象已死 → 任务作废
    if (n->token && !n->token->is_valid()) { ++popped; continue; }  // ② 再过取消令牌
    n->fn();                                 // ③ 两道关都过:执行
    ++popped;
}
```

节点一次性、不回收（压测结束统一 `delete`），`pop` 不做断链——断链只在"节点要复用"的队列里才有意义。

每个内存序的账（这层最值钱的产出）：`next.store(relaxed)` 节点未发布、只自己可见，不需要同步；`head_.exchange(acq_rel)` 一次 RMW 同时读旧栈顶、发新栈顶；`prev->next.store(release)` 把"节点已入队 + 任务载荷"打包发布；`pop` 的三处 `load(acquire)` 接住生产者的 release，head 比对识别"交换了 head 还没接链"的在途 push。计数对账 80000 = 10 万 − 1 万（已死 WeakPtr 判活拦下）− 1 万（已失效令牌拦下），两道关各司其职、可复现。基准结论：无锁 MPSC 换来了投递路径上的常数因子（本机 4 ms 对 24 ms，TSan 构建下差距放大到 8 倍多：38 ms 对 323 ms），付的代价是正确性必须靠 TSan 自证——这正应了本卷开头那句话：读真实工程代码，是为了写出能向工具自证清白的代码。
