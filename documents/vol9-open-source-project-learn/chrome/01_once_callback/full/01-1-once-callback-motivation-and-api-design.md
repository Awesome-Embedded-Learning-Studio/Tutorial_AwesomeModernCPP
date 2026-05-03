---
title: "OnceCallback 实战（一）：动机与接口设计"
description: "从一次真实的异步回调 bug 出发，拆解 std::function 在异步场景的三大缺陷，设计 OnceCallback 的完整目标 API"
chapter: 1
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - 回调机制
  - 函数对象
difficulty: beginner
platform: host
cpp_standard: [23]
reading_time_minutes: 11
prerequisites:
  - "OnceCallback 前置知识（一）：函数类型与模板偏特化"
  - "OnceCallback 前置知识（五）：std::move_only_function"
  - "OnceCallback 前置知识（六）：Deducing this"
related:
  - "OnceCallback 实战（二）：核心骨架搭建"
  - "OnceCallback 前置知识速查：C++11/14/17 核心特性回顾"
---

# OnceCallback 实战（一）：动机与接口设计

## 引言

说实话，笔者在做异步编程的时候，踩过最多的坑就是回调被多次调用。场景很经典——注册一个文件 I/O 完成的回调，期望它跑一次就完事，结果因为某处逻辑手滑多触发了一次，回调里释放的资源被二次访问，直接喜提段错误。这种 bug 的一大特点是——在测试里很难复现，因为正常的异步路径往往只跑一次回调；真正的触发条件是某种竞态或错误重试路径。

`std::function` 没法帮我们。它允许多次调用，允许拷贝传播，回调对象可以满天飞。我们需要的是一种**在类型系统层面就约束住回调语义**的机制——让"只能调用一次"这个规则变成编译器的检查项，而不是程序员记忆力的事。

这一篇我们从动机出发，拆清楚 `std::function` 到底哪里不对，然后设计我们的目标 API。下一篇再开始写代码。

> **学习目标**
>
> - 从一个真实的异步 bug 理解 `std::function` 在回调场景的三大缺陷
> - 掌握 Chromium OnceCallback 的设计哲学：move-only + 右值限定 + 单次消费
> - 设计出 OnceCallback 的完整公共接口

---

## 从一个 bug 说起

### 场景：异步文件读取

假设我们在写一个异步文件读取的封装。用户调用 `read_file_async(path, callback)`，I/O 完成后 `callback` 被触发一次，传入文件内容。

```cpp
void read_file_async(const std::string& path,
                     std::function<void(std::string)> callback);

// 使用
void on_file_read(std::string content) {
    process(content);        // 处理内容
    release_resources();     // 释放相关资源
}

read_file_async("data.txt", on_file_read);
```

看起来没问题。但如果 I/O 子系统因为某种错误触发了重试——回调被调用了两次。`release_resources()` 执行了两次，第二次访问的是已释放的内存。段错误。在测试环境中，这个重试路径永远不会被触发；只有在生产环境的高并发场景下，这个 bug 才会以极低的概率出现。

### std::function 没有帮我们

问题出在哪里？`std::function<void(std::string)>` 的类型签名里没有任何信息告诉我们"这个回调应该被调用几次"。类型系统没有提供约束，只能靠运行时的断言——如果你有的话——或者靠程序员的纪律来保证。

更糟糕的是，`std::function` 的特性让这个问题变得更难发现。它是可拷贝的，意味着回调可以被复制到多个地方。如果多个执行路径同时持有同一份回调的副本，竞态条件就埋伏在其中。它的 `operator()` 是 `const` 限定的——调用它不会改变 `std::function` 对象本身的状态，所以你无法通过调用接口来表达"调用即消费"这个语义。

---

## std::function 的三大缺陷

我们把问题系统化一下。`std::function` 作为通用的可调用对象容器，在设计上是成功的——但在异步回调这个特定场景下，它有三个致命的问题。

### 缺陷一：可复制

`std::function` 天生支持拷贝。当你拷贝一个 `std::function` 时，它内部的类型擦除机制会把存储的可调用对象也拷贝一份。在异步系统中，这意味着一个回调可以被复制到任意多个地方——任务队列里一份、定时器里一份、错误处理器里一份——每份都可以独立调用。

如果回调里捕获了 move-only 的资源（比如 `std::unique_ptr`），拷贝直接编译失败。如果捕获的是裸指针或引用，多个副本同时执行就会产生竞态。Chrome 团队的思路很直接：既然异步任务回调从根本上就不应该被复制，那就让它在类型层面不可拷贝。

### 缺陷二：可重复调用

`std::function::operator()` 对调用次数没有任何约束。你可以在同一个 `std::function` 上调一千次，它照跑不误。但在异步回调场景里，一个文件读取完成的回调被调用两次就是逻辑错误——它可能触发两次资源释放、两次状态转换、两次消息发送。这种错误在类型系统里完全检测不到。

### 缺陷三：无法表达消费语义

在 Chrome 的任务投递模型中，一个 `PostTask(FROM_HERE, callback)` 调用之后，`callback` 就不应该再被使用——它的所有权已经转移给了任务系统。`std::function` 的 `operator()` 是 `const` 限定的，调用它不会改变 `std::function` 对象本身的状态，所以你无法通过调用接口来表达"调用即消费"这个语义。

这三个问题归结到一点：`std::function` 的接口设计无法表达"这个回调只能被调用一次，调用后即失效"这个约束。我们的 OnceCallback 就是为了填补这个语义空白而设计的。

---

## Chromium 的回答：OnceCallback 设计哲学

Chrome 的回调系统建立在一条核心原则之上：**消息传递优于锁，序列化优于线程**。在这个原则下，每个投递到任务系统的回调都是一个独立的、一次性的消息。投递之后，回调的所有权就从调用方转移到了任务系统；执行之后，回调就被销毁。没有共享，没有复用，没有歧义。

这个哲学直接体现在 `OnceCallback` 的类型设计上，三个关键约束：

**Move-only**：`OnceCallback` 删除了拷贝构造和拷贝赋值，只保留移动操作。从类型层面保证回调在任意时刻只有一个持有者。

**右值限定 Run()**：`OnceCallback::Run()` 只能通过右值引用调用。左值调用触发编译错误。从语法层面提醒调用方："你在消费这个回调，之后别再用了。"

**单次消费**：`Run()` 内部会通过引用计数机制销毁 `BindState`，使得后续对同一对象的任何访问都是安全的空操作。

### Chromium 内部架构概览

Chromium 的回调系统由三个层次组成。底层是 `BindStateBase`——类型擦除的基类，带引用计数，不用虚函数而是用函数指针成员来实现多态。中间层是 `BindState<Functor, BoundArgs...>`——模板化的具体类，存储真正的可调用对象和绑定参数。顶层是 `OnceCallback<Signature>`——用户直接操作的类型，本质上是 `BindState` 的一个智能指针包装，大小只有 8 字节。

我们的实现会保留"外层接口 + 内部存储 + 类型擦除"的分层思路，但用 `std::move_only_function` 来替代 Chromium 手写的 `BindState` + 引用计数组合，用 deducing this 来替代双重重载 + `!sizeof` hack。

---

## 设计目标 API

我们把目标 API 定下来，再回头讨论每个设计决策。这是工程师的工作方式——先想清楚"我要什么"，再想"怎么做"。

### 构造与调用

```cpp
#include "once_callback/once_callback.hpp"

using namespace tamcpp::chrome;

// 从 lambda 构造
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// 调用：必须通过右值
int result = std::move(cb).run(3, 4);  // result == 7

// 调用后 cb 被消费
// std::move(cb).run(1, 2);  // 运行时断言失败
```

### 参数绑定

```cpp
// bind_once：预绑定部分参数，返回一个新的 OnceCallback
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // 预绑定前两个参数
);

int r = std::move(bound).run(30);  // r == 60
```

### 取消检查

```cpp
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// 检查回调是否仍然有效
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid：乐观检查
if (cb.maybe_valid()) {
    std::move(cb).run(42);
}
```

### 链式组合

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
}).then([](int sum) {
    return sum * 2;
});

int final_result = std::move(pipeline).run(3, 4);
// final_result == 14，因为 (3+4)*2 = 14
```

---

## 接口设计决策分析

### 为什么用 run() 而不是 operator()

Chromium 用的是 `Run()`（Google 风格要求大写开头）。我们用 `run()` 符合 snake_case 命名规范。更深层的原因是语义区分——`operator()` 太通用，任何可调用对象都有 `operator()`；`run()` 明确表达了"执行任务"的语义，在代码审查时一眼就能看出这是在消费一个 OnceCallback，而不是调用一个普通函数。

### 为什么 run() 必须通过右值

这是整个设计中最关键的一点。我们用 deducing this 让编译器帮我们拦截左值调用——如果写 `cb.run(args)` 而不是 `std::move(cb).run(args)`，编译器直接报错，错误信息明确告诉你该怎么做。这个机制在前置知识（六）里已经详细讲过了。

### 为什么区分 is_cancelled() 和 maybe_valid()

区别在于安全保证的强弱。`is_cancelled()` 提供确定性回答——只能在回调绑定的序列上调用，保证返回准确的结果。`maybe_valid()` 提供乐观估计——可以从任何线程调用，但结果可能过时。在 Chromium 的完整实现中，这个区分和线程安全保证有关。我们的简化版暂时让两者语义相同，但保留了接口以备后续扩展。

### 为什么 then() 消费 *this

`then()` 的语义是"把当前回调的执行结果传给下一个回调"。这要求当前回调在 `then()` 返回的新回调中被完整捕获。如果 `then()` 不消费 `*this`，同一个回调就会同时存在于两个地方——违反 move-only 的语义约束。所以 `then()` 被声明为右值限定成员函数，调用后原回调对象进入已消费状态。

---

## 环境搭建

开始写代码之前，确认一下工具链。OnceCallback 依赖 `std::move_only_function` 和 deducing this，都是 C++23 特性。

### 编译器要求

GCC 13+ 或 Clang 17+ 可以完整支持上述特性。编译时加 `-std=c++23`。

### 验证代码

```cpp
#include <functional>

// 验证 std::move_only_function 可用
static_assert(__cpp_lib_move_only_function >= 202110L);

// 验证 deducing this 可用
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

如果这段代码编译通过，环境就绑了。

### CMake 最小配置

```cmake
cmake_minimum_required(VERSION 3.20)
project(once_callback_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(once_callback INTERFACE)
target_include_directories(once_callback INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
```

---

## 小结

这一篇我们从动机出发，搞清楚了三件事。`std::function` 在异步回调场景有三大缺陷——可复制、可重复调用、无法表达消费语义——根源在于类型系统无法约束"只能调用一次"。Chromium 的 OnceCallback 通过 move-only + 右值限定 Run() + 单次消费来填补这个语义空白。我们设计了一套目标 API，包括构造与调用、参数绑定（`bind_once`）、取消检查（`is_cancelled`/`maybe_valid`）和链式组合（`then()`）四个核心功能。

下一篇我们开始搭建核心骨架——从模板偏特化到三态管理，把 OnceCallback 的类骨架搭起来。

## 参考资源

- [Chromium Callback 文档](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
