---
title: "OnceCallback 实战（六）：测试与性能对比"
description: "系统化设计六类测试用例验证 OnceCallback 的所有核心行为，对比与 Chromium 原版和标准库方案的性能差异"
chapter: 1
order: 6
tags:
  - host
  - cpp-modern
  - beginner
  - 回调机制
  - 函数对象
difficulty: beginner
platform: host
cpp_standard: [23]
reading_time_minutes: 10
prerequisites:
  - "OnceCallback 实战（二）：核心骨架搭建"
  - "OnceCallback 实战（三）：bind_once 实现"
  - "OnceCallback 实战（四）：取消令牌设计"
  - "OnceCallback 实战（五）：then 链式组合"
related:
  - "OnceCallback 前置知识（五）：std::move_only_function"
---

# OnceCallback 实战（六）：测试与性能对比

## 引言

到这里，OnceCallback 的四个核心功能——核心骨架、`bind_once`、取消令牌、`then()` 链式组合——都已经实现完了。这一篇做两件事：第一，系统化地梳理测试策略，确保实现在各种边界条件下都是正确的；第二，从性能角度分析我们的实现与 Chromium 原版、标准库方案之间的差异，弄清楚我们牺牲了什么、换来了什么。

> **学习目标**
>
> - 掌握按不变量组织测试用例的方法
> - 理解六类测试的设计意图和关键断言
> - 清楚我们的 OnceCallback 与 Chromium 原版在性能上的取舍关系

---

## 测试框架搭建

我们使用 Catch2 v3 作为测试框架，通过 CPM（CMake Package Manager）自动拉取依赖。

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

Catch2 的 `REQUIRE` 宏比 `assert()` 强在它会报告具体的失败表达式、文件和行号，并且在同一个 `TEST_CASE` 内继续执行后续检查。`REQUIRE_THROWS_AS` 专门用于验证异常类型。

运行测试：在 `build/` 目录下 `cmake --build . && ctest`。

---

## 六类测试用例

我们把测试组织成六个类别，每个类别聚焦一个独立的设计不变量。按不变量组织测试比按功能组织更不容易遗漏边界情况。

### A 类：基本调用与返回值

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

验证最基本的构造和调用行为——非 void 回调返回正确的值，void 回调正常执行。void 返回走的是 `if constexpr (std::is_void_v<ReturnType>)` 的另一条分支。

### B 类：移动语义

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

move-only capture 测试验证了 OnceCallback 真正支持 move-only 的可调用对象——如果底层用 `std::function` 而不是 `std::move_only_function`，这段代码编译失败。移动语义测试验证了移动构造后源对象变为 kEmpty 状态。

有一个容易搞混的概念点——移动操作转移了所有权，但不会触发消费。只有 `run()` 才会消费回调。`OnceCallback cb2 = std::move(cb1)` 只是转移了所有权，回调在 `cb2.run()` 之前一直处于活跃状态。

### C 类：单次调用约束

这个约束是通过 deducing this + `static_assert` 实现的——`cb.run()` 会触发编译错误，`std::move(cb).run()` 才能通过。不需要运行时测试，编译通过本身就是验证。

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

覆盖普通 lambda 的部分参数绑定和成员函数绑定。成员函数绑定的生命周期陷阱在前面的文章里已经讲过了——`&calc` 是裸指针，安全责任在调用方。

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

三个关键行为：令牌有效时不取消、令牌失效后 void 回调不执行、令牌失效后非 void 回调抛出 `std::bad_function_call`。

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
    REQUIRE(result == "20");
}

TEST_CASE("then with void first callback", "[then]") {
    int value = 0;
    auto cb = OnceCallback<void(int)>([&value](int x) { value = x; })
                  .then([&value] { return value * 3; });
    int result = std::move(cb).run(7);
    REQUIRE(result == 21);
}
```

覆盖三种组合模式：两级非 void 管道、多级管道（跨越类型边界从 int 到 string）、void 前缀回调。

---

## 性能对比：与 Chromium 原版

### 对象大小

```cpp
std::cout << "sizeof(std::function<void()>):        "
          << sizeof(std::function<void()>) << " bytes\n";
std::cout << "sizeof(std::move_only_function<void()>): "
          << sizeof(std::move_only_function<void()>) << " bytes\n";
// Chromium OnceCallback<void()> ≈ 8 bytes

std::cout << "sizeof(OnceCallback<void()>): "
          << sizeof(OnceCallback<void()>) << " bytes\n";
// 我们的：move_only_function (32) + status (1) + token ptr (16) + padding
// 预估 56-64 bytes
```

在 GCC 上，典型值是 `std::function` 约 32 字节，`std::move_only_function` 约 32 字节，我们的 `OnceCallback` 约 56-64 字节。Chromium 的只有 8 字节。

差距的根源在于存储策略。Chromium 把所有状态放在堆上的 `BindState` 里，回调对象只持有一个指针。我们用 `std::move_only_function` 的 SBO 把小对象直接内联存储，避免了堆分配但增大了对象大小。

### 分配行为

`std::move_only_function` 的 SBO 阈值通常是 2-3 个指针大小（16-24 字节）。捕获少量参数的 lambda 通常能放进 SBO，不会触发堆分配。大 lambda 则在构造时堆分配。

Chromium 总是堆分配（`new BindState`），但分配只发生一次。之后 OnceCallback 的移动操作只是复制一个指针（8 字节），代价极低。我们的方案在小对象时不分配（SBO），但移动操作需要复制 32+ 字节。

### 间接调用开销

两种方案的调用开销是一样的——一次间接函数调用。`std::move_only_function::operator()` 和 Chromium 的 `polymorphic_invoke_` 都通过函数指针分派。在 `-O2` 优化下，这个间接调用无法被内联消除。

### 取舍总结

| 指标 | 我们的方案 | Chromium 方案 |
|------|-----------|--------------|
| 回调对象大小 | 56-64 字节 | 8 字节 |
| 小 lambda 堆分配 | 不分配（SBO） | 总是分配 |
| 移动代价 | 复制 32+ 字节 | 复制 1 个指针 |
| 实现代码量 | ~200 行 | ~2000+ 行 |

我们牺牲了对象的紧凑性和移动操作的极致性能，换来了实现简洁性——不需要手写引用计数、函数指针表、`TRIVIAL_ABI` 注解。小 lambda 的零堆分配在某些低频场景下反而是优势。对教学目的和大多数实际场景来说，这个取舍是值得的。

---

## 小结

这一篇我们做了两件事。测试方面，围绕六个不变量（基本调用、移动语义、单次调用、参数绑定、取消机制、链式组合）设计了 11 个 Catch2 测试用例，覆盖了 OnceCallback 的所有核心行为。性能方面，对比了与 Chromium OnceCallback 在对象大小、分配行为和调用开销上的差异——我们的实现用紧凑性换来了简洁性。

到这里，OnceCallback 组件的设计、实现和验证就全部完成了。13 篇文章从前置知识到实战，覆盖了从 C++11 移动语义到 C++23 deducing this 的完整知识链。希望这个系列能帮助你理解"如何用现代 C++ 设计一个工业级的组件"——不仅仅是写代码，更重要的是理解每一个设计决策背后的原因。

## 参考资源

- [Chromium base/functional/ 源码目录](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Catch2 文档](https://github.com/catchorg/Catch2/tree/devel/docs)
