---
title: "OnceCallback 前置知识（五）：std::move_only_function (C++23)"
description: "深入理解 C++23 的 std::move_only_function——OnceCallback 的核心存储类型，从 std::function 的演进动机到 SBO 行为，再到为什么 OnceCallback 需要独立的三态管理"
chapter: 0
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 函数对象
  - 智能指针
difficulty: intermediate
platform: host
cpp_standard: [23]
reading_time_minutes: 10
prerequisites:
  - "OnceCallback 前置知识速查：C++11/14/17 核心特性回顾"
  - "OnceCallback 前置知识（一）：函数类型与模板偏特化"
related:
  - "OnceCallback 实战（二）：核心骨架搭建"
  - "OnceCallback 实战（六）：测试与性能对比"
---

# OnceCallback 前置知识（五）：std::move_only_function (C++23)

## 引言

`std::move_only_function` 是 OnceCallback 的心脏——它承担了所有类型擦除的脏活累活。OnceCallback 的 `func_` 成员就是 `std::move_only_function<FuncSig>` 类型，它把 lambda、函数指针、仿函数等各种形态的可调用对象统一包装成同一个已知签名的调用接口。

这一篇我们要搞清楚三件事：`std::move_only_function` 和 `std::function` 到底有什么区别、它的 SBO（Small Buffer Optimization）行为是怎么工作的、以及为什么 OnceCallback 不能直接依赖它的判空机制而需要自己搞一套三态管理。

> **学习目标**
>
> - 理解 `std::move_only_function` 的设计动机——为什么 `std::function` 不够用
> - 掌握构造、移动、调用、判空四个核心操作
> - 理解 SBO 的原理和 `std::move_only_function` 的分配行为
> - 明白为什么 OnceCallback 需要独立的 `Status` 枚举

---

## 从 std::function 到 std::move_only_function

### std::function 的局限

`std::function` 是 C++11 引入的通用可调用对象容器，它通过类型擦除把各种可调用对象统一成同一个接口。但 `std::function` 有一个根本性的限制：它要求存储的可调用对象**必须可拷贝**。

原因在于 `std::function` 自身是可拷贝的——当你拷贝一个 `std::function` 时，它需要把内部存储的可调用对象也拷贝一份。如果你试图用一个捕获了 `std::unique_ptr` 的 lambda 来构造 `std::function`，编译器会在拷贝语义上直接报错：

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// 编译错误！unique_ptr 不可拷贝，std::function 要求可拷贝
std::function<int()> f = [p = std::move(ptr)]() { return *p; };
```

这个限制在 OnceCallback 的场景里是致命的——OnceCallback 的核心卖点就是 move-only，它必须支持捕获 `unique_ptr` 的 lambda。

### std::move_only_function 的解决方案

`std::move_only_function`（C++23，定义在 `<functional>` 中）就是"move-only 版本的 `std::function`"。它删除了拷贝操作，只保留移动操作，从而不再要求存储的可调用对象可拷贝。

```cpp
#include <functional>
#include <memory>

auto ptr = std::make_unique<int>(42);

// OK！move_only_function 不要求可拷贝
std::move_only_function<int()> f = [p = std::move(ptr)]() { return *p; };

int result = f();  // result == 42
```

两个类型在接口上的关键区别可以概括为：`std::function` 可拷贝可移动，要求存储对象可拷贝；`std::move_only_function` 不可拷贝只可移动，只要求存储对象可移动。

---

## 四个核心操作

### 构造：从可调用对象创建

`std::move_only_function<R(Args...)>` 接受任何匹配签名 `R(Args...)` 的可调用对象——lambda、函数指针、仿函数，甚至另一个 `std::move_only_function`：

```cpp
// 从 lambda 构造
std::move_only_function<int(int, int)> f1 = [](int a, int b) { return a + b; };

// 从函数指针构造
int add(int a, int b) { return a + b; }
std::move_only_function<int(int, int)> f2 = &add;

// 从仿函数构造
struct Multiplier {
    int operator()(int a, int b) { return a * b; }
};
std::move_only_function<int(int, int)> f3 = Multiplier{};

// 默认构造：创建空的 move_only_function
std::move_only_function<int()> f4;  // f4 == nullptr
```

### 移动：转移所有权

移动操作把源对象的可调用对象转移到目标对象。移动之后，源对象的状态是**未指定的**——标准没有保证它一定为空。

```cpp
std::move_only_function<int()> f = []() { return 42; };
auto g = std::move(f);
// f 的状态未指定——可能为空，也可能不为空
// 不要依赖 f 在移动后的行为
```

这一点非常重要——也是 OnceCallback 需要自己的 `Status` 枚举的原因之一。我们后面会展开讲。

### 调用：通过 operator() 执行

调用语法和 `std::function` 一样——直接用 `()` 运算符：

```cpp
std::move_only_function<int(int, int)> f = [](int a, int b) { return a + b; };
int result = f(3, 4);  // result == 7
```

如果 `f` 为空（通过默认构造或 `= nullptr`），调用会抛出 `std::bad_function_call` 异常。

### 判空：检查是否持有可调用对象

通过 `operator bool()` 或与 `nullptr` 比较：

```cpp
std::move_only_function<int()> f;
if (!f) {
    std::cout << "f is empty\n";
}
// 等价于
if (f == nullptr) {
    std::cout << "f is empty\n";
}

f = []() { return 42; };
if (f) {
    std::cout << "f is not empty\n";
}
```

也可以通过赋值 `nullptr` 来主动清空：

```cpp
f = nullptr;  // 清空 f，析构之前持有的可调用对象
```

---

## SBO：小对象优化

### 什么是 SBO

`std::move_only_function`（和 `std::function` 一样）内部实现了**小对象优化**（Small Buffer Optimization，SBO）。思路很简单：对象内部预留一块固定大小的缓冲区（通常是几个指针大小），如果可调用对象足够小，就把它直接存到缓冲区里，避免堆分配；如果太大，就在堆上分配内存来存储。

```text
┌──────────────────────────────────┐
│ std::move_only_function          │
│ ┌──────────────────────────────┐ │
│ │ 函数指针/虚表指针            │ │  ← 用于类型擦除的调用分派
│ ├──────────────────────────────┤ │
│ │ SBO 缓冲区（通常 16-32 字节）│ │  ← 小对象直接存这里
│ └──────────────────────────────┘ │
│ 或                               │
│ ┌──────────────────────────────┐ │
│ │ 堆指针（指向动态分配的对象） │ │  ← 大对象存在堆上
│ └──────────────────────────────┘ │
└──────────────────────────────────┘
```

SBO 的阈值是实现定义的——通常在 2-3 个指针大小（16-24 字节）左右。捕获少量参数的 lambda（比如 `[x = 42]` 或 `[&ref]`）通常能放进 SBO，不会触发堆分配。但如果 lambda 捕获了大量数据（比如一个 `std::string` + 几个 `int`），超过了 SBO 阈值，构造时就会在堆上分配。

### sizeof 对比

```cpp
#include <functional>
#include <iostream>

int main() {
    std::cout << "sizeof(std::function<void()>):        "
              << sizeof(std::function<void()>) << "\n";
    std::cout << "sizeof(std::move_only_function<void()>): "
              << sizeof(std::move_only_function<void()>) << "\n";
}
```

在 GCC 上，典型值是 `std::function<void()>` 约 32 字节，`std::move_only_function<void()>` 也约 32 字节。两者大小差不多，因为它们使用类似的 SBO 策略。

---

## 为什么 OnceCallback 需要独立的 Status 枚举

你可能已经注意到了一个细节——OnceCallback 在 `std::move_only_function` 之外又加了一个自己的 `Status` 枚举来追踪状态。为什么不直接用 `std::move_only_function` 的判空机制？

原因是 `std::move_only_function` 的判空无法区分三种不同的状态：

```cpp
enum class Status : uint8_t {
    kEmpty,     // 从未被赋值（默认构造）
    kValid,     // 持有有效的可调用对象
    kConsumed   // 已被 run() 调用过
};
```

`std::move_only_function` 的 `operator bool()` 只能区分"空"和"非空"两种状态。但 OnceCallback 需要知道一个回调是"从来没被赋过值"（kEmpty）还是"曾经有值但已经被调用了"（kConsumed）。这两种情况在调试时的含义完全不同——kEmpty 意味着"你忘了给回调赋值"，kConsumed 意味着"回调已经被正确调用了，你不应该再使用它"。

还有一个更微妙的问题：`std::move_only_function` 移动后的状态是**未指定的**——标准不保证移动后源对象的 `operator bool()` 返回 `false`。某些实现可能仍然返回 `true`，只是内部数据已经无效了。如果 OnceCallback 依赖 `std::move_only_function` 的判空来判断状态，在移动操作之后可能会得到错误的结果。独立的 `Status` 枚举完全由我们控制——移动构造函数显式把源对象设为 `kEmpty`，不存在歧义。

---

## 与 Chromium BindState 的对比

Chromium 没有使用标准库的类型擦除设施——它手写了一套 `BindState` 系统。对比一下两种方案的核心差异。

Chromium 的 `BindState<Functor, BoundArgs...>` 是一个堆分配的对象，存储了可调用对象和所有绑定参数。`OnceCallback` 本身只持有一个指向 `BindState` 的智能指针（`scoped_refptr`），大小只有 8 字节——一个指针。所有状态都放在堆上的 `BindState` 里，回调对象本身只是一个"瘦代理"。

我们的方案用 `std::move_only_function` 替代了整个 `BindState` 层——它内部实现了类型擦除和 SBO，省去了我们手写函数指针表、SBO 缓冲区、移动/析构操作的工作。代价是对象大小从 8 字节膨胀到约 32 字节（`std::move_only_function` 本身的大小），再加上 `Status` 枚举和可选的 `CancelableToken` 指针，整个 `OnceCallback` 大约 56-64 字节。

| 指标 | Chromium BindState | 我们的 std::move_only_function |
|------|-------------------|-------------------------------|
| 回调对象大小 | 8 字节（一个指针） | 56-64 字节 |
| 堆分配 | 总是（new BindState） | 仅当 lambda 超过 SBO 阈值 |
| 移动代价 | 复制一个指针 | 复制 32+ 字节 |
| 实现复杂度 | 很高（手写引用计数+函数指针表） | 低（复用标准库） |

对于教学目的和大多数实际场景，56-64 字节的回调对象完全不是瓶颈。如果你的项目确实需要极致紧凑，可以参考 Chromium 的方案——核心思路我们在后续实战篇里会讲清楚。

---

## 小结

这一篇我们搞清楚了 `std::move_only_function` 的来龙去脉。它是 C++23 引入的 move-only 版本的 `std::function`，删除了拷贝操作以支持 move-only 的可调用对象。内部实现了 SBO 来优化小对象的存储。但它的移动后状态未指定，且只能区分"空"和"非空"两种状态——这就是 OnceCallback 需要独立的三态 `Status` 枚举的原因。与 Chromium 手写的 `BindState` 相比，我们用对象大小的膨胀换来了实现简洁性的大幅提升。

下一篇我们去看 OnceCallback 的最后一个前置知识点——C++23 的 deducing this（显式对象参数），它是 `run()` 方法实现编译期左值/右值拦截的核心机制。

## 参考资源

- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0288R9 - move_only_function 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html)
- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
