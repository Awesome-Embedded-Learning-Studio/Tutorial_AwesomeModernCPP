---
title: "once_callback 设计指南（三）：测试策略与性能对比"
description: "系统设计 once_callback 的测试用例，对比与 Chromium 原版和标准库方案的性能差异，总结设计取舍"
chapter: 1
order: 3
tags:
  - host
  - cpp-modern
  - advanced
  - 回调机制
  - 函数对象
difficulty: advanced
platform: host
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "once_callback 设计指南（一）：动机与接口设计"
  - "once_callback 设计指南（二）：逐步实现"
related:
  - "回调取消与组合模式"
---

# once_callback 设计指南（三）：测试策略与性能对比

## 引言

前两篇我们完成了 `OnceCallback` 的设计和实现。这一篇做两件事：第一，系统化地梳理测试策略，给出一套完整的测试用例清单，确保我们的实现在各种边界条件下都是正确的；第二，从性能角度分析我们的实现与 Chromium 原版、标准库方案之间的差异，弄清楚我们牺牲了什么、换来了什么。

> **学习目标**
>
> - 掌握 `OnceCallback` 的六类测试用例设计方法
> - 理解 `sizeof`、SBO 阈值、间接调用开销等性能指标的含义
> - 清楚我们的 `OnceCallback` 与 Chromium `OnceCallback` 的取舍关系

---

## 测试策略

我们把测试组织成六个类别，每个类别聚焦一个独立的设计不变量。这种按不变量组织测试的方式比按功能组织更不容易遗漏边界情况——因为每个不变量本身就是一种正确性保证，测试的目的就是验证这些保证在各种场景下都成立。

我们的实际测试代码使用 Catch2 框架，配合 CMake + CPM 管理依赖。下面列出的测试用例与 `code/volumn_codes/vol9/chrome_design/test/test_once_callback.cpp` 中的实际代码一一对应。

### A 类：基本调用与返回值

这类测试验证 `OnceCallback` 的基本构造和调用行为。

```cpp
TEST_CASE("non-void return", "[once_callback]") {
    OnceCallback<int(int, int)> cb([](int a, int b) { return a + b; });
    int result = std::move(cb).run(3, 4);
    REQUIRE(result == 7);
}

TEST_CASE("void return", "[once_callback]") {
    bool called = false;
    OnceCallback<void()> cb([&called] { called = true; });
    std::move(cb).run();
    REQUIRE(called);
}
```

最基本的场景——构造一个回调，调用它，验证返回值。void 返回类型走的是 `if constexpr (std::is_void_v<ReturnType>)` 的另一条分支，确认我们的编译期分支逻辑是正确的。

### B 类：移动语义

这类测试验证 move-only 约束和移动操作的正确性。

```cpp
TEST_CASE("move-only capture", "[once_callback]") {
    auto ptr = std::make_unique<int>(42);
    OnceCallback<int()> cb([p = std::move(ptr)] { return *p; });
    int result = std::move(cb).run();
    REQUIRE(result == 42);
}

TEST_CASE("move semantics: source becomes null", "[once_callback]") {
    OnceCallback<int()> cb([] { return 1; });
    OnceCallback<int()> cb2 = std::move(cb);
    REQUIRE(cb.is_null());

    int result = std::move(cb2).run();
    REQUIRE(result == 1);
}
```

move-only capture 测试（`std::make_unique<int>(42)` 被捕获进 lambda）确认了 `OnceCallback` 真正支持 move-only 的可调用对象——如果底层用的是 `std::function` 而不是 `std::move_only_function`，这段代码直接编译失败。移动语义测试验证了移动构造后源对象变为 `kEmpty` 状态（通过 `is_null()` 检查），目标对象保持有效并可以正常调用。

有一个容易搞混的概念点——移动操作转移了所有权，但不会触发消费。只有 `run()` 才会消费回调。这个区别在 Chromium 里也很重要：`PostTask(FROM_HERE, std::move(cb))` 只是转移所有权，回调在任务被执行之前一直处于活跃状态。

### C 类：单次调用约束

这类测试验证"调用一次即消费"的核心语义。在 A 类和 B 类的测试中我们已经覆盖了正常调用路径，C 类聚焦于左值调用的编译拦截。这个约束是通过 deducing this + `static_assert` 实现的——如果写 `cb.run()` 而不是 `std::move(cb).run()`，编译器会直接报错，错误信息明确告诉调用方应该用 `std::move`。这部分不需要运行时测试，编译通过本身就是验证。

### D 类：参数绑定

```cpp
TEST_CASE("bind_once basic", "[bind_once]") {
    auto bound = bind_once<int(int)>([](int a, int b) { return a * b; }, 5);
    int result = std::move(bound).run(8);
    REQUIRE(result == 40);
}

TEST_CASE("bind_once with member function", "[bind_once]") {
    struct Calc {
        int multiply(int a, int b) { return a * b; }
    };
    Calc calc;
    auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
    int result = std::move(bound).run(8);
    REQUIRE(result == 40);
}
```

`bind_once` 测试覆盖了两种典型场景：普通 lambda 的部分参数绑定和成员函数绑定。成员函数绑定测试特别值得关注——`&Calc::multiply` 是成员函数指针，`&calc` 是对象指针，`std::invoke` 在内部把它展开成 `(calc.*multiply)(5, 8)` 调用。这里有一个生命周期陷阱需要注意：`&calc` 是裸指针，`bind_once` 不会管理它的生命周期。如果 `calc` 在回调被调用之前就被销毁了，`std::invoke` 会通过悬空指针访问已释放的内存。Chromium 用 `base::Unretained` 显式标记裸指针的安全性，用 `base::Owned` 接管所有权，用 `base::WeakPtr` 在对象析构时自动取消回调。我们的简化版里，这个安全责任暂时交给调用方。

### E 类：取消机制

```cpp
TEST_CASE("is_cancelled respects cancel token", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<void()> cb([] {});
    cb.set_token(token);

    REQUIRE_FALSE(cb.is_cancelled());
    token->invalidate();
    REQUIRE(cb.is_cancelled());
}

TEST_CASE("cancelled void callback does not execute", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    bool called = false;
    OnceCallback<void()> cb([&called] { called = true; });
    cb.set_token(token);
    token->invalidate();

    std::move(cb).run();
    REQUIRE_FALSE(called);
}

TEST_CASE("cancelled non-void callback throws", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<int()> cb([] { return 1; });
    cb.set_token(token);
    token->invalidate();

    REQUIRE_THROWS_AS(std::move(cb).run(), std::bad_function_call);
}
```

取消测试覆盖了三个关键行为：令牌有效时不取消、令牌失效后 void 回调不执行、令牌失效后非 void 回调抛出 `std::bad_function_call`。第三个测试的行为值得展开说一下——我们的实现在非 void 返回的已取消回调中抛出异常，理由是调用方期望得到一个返回值，但我们无法提供一个有意义的值，所以抛异常是比返回未定义值更安全的做法。Chromium 的实现在这里会直接终止程序（`CHECK` 失败），我们选择异常是因为它在测试中更容易捕获和验证。

### F 类：Then 组合

```cpp
TEST_CASE("then chains two callbacks", "[then]") {
    auto cb = OnceCallback<int(int)>([](int x) { return x * 2; })
                  .then([](int x) { return x + 10; });
    int result = std::move(cb).run(5);
    REQUIRE(result == 20);  // 5 * 2 + 10
}

TEST_CASE("then multi-level pipeline", "[then]") {
    auto pipeline = OnceCallback<int(int)>([](int x) { return x * 2; })
                        .then([](int x) { return x + 10; })
                        .then([](int x) { return std::to_string(x); });
    std::string result = std::move(pipeline).run(5);
    REQUIRE(result == "20");  // (5*2)+10 = "20"
}

TEST_CASE("then with void first callback", "[then]") {
    int value = 0;
    auto cb = OnceCallback<void(int)>([&value](int x) { value = x; })
                  .then([&value] { return value * 3; });
    int result = std::move(cb).run(7);
    REQUIRE(result == 21);
}
```

`then()` 测试覆盖了三种组合模式：两级非 void 管道、多级管道（跨越类型边界——从 `int` 到 `std::string`）、以及 void 前缀回调。多级管道测试特别有趣——`(5*2)+10 = 20`，最终被 `std::to_string` 转换为字符串 `"20"`。这个测试验证了 `then()` 在每一级都正确地推导了返回类型，并且类型擦除（通过 `std::move_only_function`）在不同类型的 lambda 之间正确工作。void 前缀测试验证了 `if constexpr (std::is_void_v<ReturnType>)` 分支——第一个回调设置 `value = 7`，第二个回调通过引用读取 `value` 并返回 `21`。

### 测试框架与构建配置

我们使用 Catch2 v3 作为测试框架，通过 CPM（CMake Package Manager）自动拉取依赖。测试的 CMake 配置非常简洁：

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

Catch2 的 `REQUIRE` 宏比 `assert()` 强在它会报告具体的失败表达式、文件和行号，并且在同一个 `TEST_CASE` 内继续执行后续检查（而不是像 `assert()` 那样直接终止程序）。`REQUIRE_THROWS_AS` 则专门用于验证异常类型——在取消机制的测试中，我们需要确认被取消的非 void 回调抛出的是 `std::bad_function_call`，而不是其他异常。

运行测试的流程很简单——在 `build/` 目录下 `cmake --build . && ctest`。

---

## 性能考量：与 Chromium 原版对比

### 对象大小

这是最直观的差异。我们用一个简单的程序来测量：

```cpp
#include <functional>
#include <iostream>
#include "once_callback/once_callback.hpp"

int main() {
    std::cout << "sizeof(std::function<void()>):      "
              << sizeof(std::function<void()>) << " bytes\n";
    std::cout << "sizeof(std::move_only_function<void()>): "
              << sizeof(std::move_only_function<void()>) << " bytes\n";
    // Chromium OnceCallback<void()> ≈ 8 bytes（一个指针）

    using namespace tamcpp::chrome;
    std::cout << "sizeof(OnceCallback<void()>): "
              << sizeof(OnceCallback<void()>) << " bytes\n";
    // 我们的 OnceCallback 大约是：
    // move_only_function (32) + status (1) + token ptr (16) + padding
    // 预估 56-64 bytes
}
```

在 GCC 上，典型值如下：`std::function<void()>` 约 32 字节，`std::move_only_function<void()>` 约 32 字节，我们的 `OnceCallback<void()>` 加上 `Status` 枚举和可选的 `CancelableToken` 指针，大约 56-64 字节。Chromium 的 `OnceCallback<void()>` 只有 8 字节——一个指向 `BindState` 的 `scoped_refptr`。

差距的根源在于存储策略。Chromium 把所有状态（可调用对象 + 绑定参数）都放在堆上的 `BindState` 里，回调对象本身只持有一个指针。我们用 `std::move_only_function` 的 SBO 把小对象直接内联存储在回调对象内部，避免了堆分配但增大了对象大小。

### 分配行为

`std::move_only_function` 的 SBO 阈值是实现定义的，通常是 2-3 个指针大小（16-24 字节）。捕获少量参数的 lambda（比如 `[x = 42]` 或 `[&ref]`）通常能放进 SBO，不会触发堆分配。但如果 lambda 捕获了大量数据（比如一个 `std::string` + 几个 `int`），就会在构造时堆分配。

Chromium 的方案总是堆分配（`new BindState<Functor, BoundArgs...>`），但分配只发生一次——在 `BindOnce` 时。之后 `OnceCallback` 的移动操作只是复制一个指针（8 字节），代价极低。我们的方案在小对象时不分配（SBO），但移动操作需要复制整个 `std::move_only_function`（32 字节）加上 `token_` 指针，代价稍高。

两种策略在不同场景下各有优势。对于高频投递的小回调（Chrome 浏览器的主场景），Chromium 的方案更优——移动代价低、大小一致有利于 CPU 缓存。对于低频的大回调（比如一次性初始化任务），我们的方案更优——省去一次堆分配。

### 间接调用开销

两种方案的调用开销是一样的：一次间接函数调用。`std::move_only_function::operator()` 内部通过函数指针或虚函数表分派到具体的可调用对象；Chromium 的 `BindState::polymorphic_invoke_` 也是函数指针分派。在 `-O2` 优化下，这个间接调用无法被内联消除，性能上两种方案等价。

### 我们牺牲了什么，换来了什么

总结一下取舍。

我们牺牲了对象的紧凑性（56-64 字节 vs 8 字节），换来了实现简洁性——不需要手写引用计数、函数指针表、`TRIVIAL_ABI` 注解。我们牺牲了移动操作的极致性能（复制 32 字节 + 指针 vs 复制 8 字节），换来了小对象的零堆分配。我们牺牲了引用计数共享（无法让多个回调共享同一份 `BindState`），但 `OnceCallback` 本身就是独占语义，不需要共享。

这些取舍对于教学目的和大多数实际场景来说都是合理的。如果你的项目确实需要 Chromium 级别的极致性能，可以参考 Chromium 的源码做进一步优化——核心思路我们已经在这三篇设计指南里讲清楚了。

---

## 完整组件文件一览

到这里，`OnceCallback` 组件的设计、实现和测试策略都已完成。完整的文件清单：

```text
documents/vol9-open-source-project-learn/chrome/hands_on/
├── 01-once-callback-design.md           # 设计篇：动机与接口
├── 02-once-callback-implementation.md   # 实现篇：逐步实现
└── 03-once-callback-testing.md          # 验证篇：测试与性能
```

对应的可编译代码（头文件 + 测试）位于项目代码目录中：

```text
code/volumn_codes/vol9/chrome_design/
├── CMakeLists.txt
├── cmake/CPM.cmake
├── cancel_token/
│   └── cancel_token.hpp                 # 取消令牌
├── once_callback/
│   ├── CMakeLists.txt
│   ├── once_callback.hpp                # 主接口（模板声明）
│   └── once_callback_impl.hpp           # 实现（模板定义）
└── test/
    ├── CMakeLists.txt                   # Catch2 测试配置
    └── test_once_callback.cpp           # 完整测试用例
```

---

## 小结

这篇验证篇我们做了两件事。测试方面，围绕六个不变量（基本调用、移动语义、单次调用、参数绑定、取消机制、链式组合）设计了 11 个 Catch2 测试用例，覆盖了 `OnceCallback` 的所有核心行为。性能方面，对比了与 Chromium `OnceCallback` 在对象大小、分配行为和调用开销上的差异——我们的实现用紧凑性换来了简洁性，对绝大多数场景来说这个取舍是值得的。

下一步可以尝试的方向：实现 `RepeatingCallback`（可复制、可重复调用的版本），给 `bind_once` 添加 `Unretained` / `Owned` / `WeakPtr` 等生命周期辅助函数，或者用 Google Benchmark 做精确的性能测量。

## 参考资源

- [Chromium base/functional/ 源码目录](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Google Test 文档](https://google.github.io/googletest/)
- [Google Benchmark 文档](https://github.com/google/benchmark)
