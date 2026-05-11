---
title: "once_callback 设计指南（一）：动机与接口设计"
description: "从 Chromium OnceCallback 出发，设计一个 C++23 的 move-only、一次性消费回调组件——第一部分聚焦动机分析和 API 设计"
chapter: 1
order: 1
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
  - "std::function、std::invoke 与可调用对象"
  - "移动语义与完美转发"
related:
  - "OnceCallback 与 RepeatingCallback"
  - "bind_once / bind_repeating 与参数绑定"
---

# once_callback 设计指南（一）：动机与接口设计

## 引言

说实话，笔者在做异步编程的时候，踩过最多的坑就是回调被多次调用。场景很经典：注册一个文件 I/O 完成的回调，期望它跑一次就完事，结果因为某处逻辑手滑多触发了一次，回调里释放的资源被二次访问，直接喜提段错误。这种 bug 的一大特点是——在测试里很难复现，因为正常的异步路径往往只跑一次回调；真正的触发条件是某种竞态或错误重试路径。

`std::function` 没法帮我们。它允许多次调用，允许拷贝传播，回调对象可以满天飞。我们在卷二已经拆解过 `std::function` 的内部机制（类型擦除 + SBO）和它的 `LightCallback` 简化实现——那个版本解决了类型擦除的开销问题，但完全没有触及"回调应该被调用几次"这个语义问题。

Chromium 团队在设计 `base::OnceCallback` 的时候，给出了一个非常漂亮的回答：**让回调的类型系统本身来约束调用语义**。`OnceCallback` 是 move-only 类型，它的 `Run()` 方法只能通过右值引用调用（`std::move(cb).Run()`），调用一次之后回调对象就被消费掉了，再调就是空操作或者断言失败。这个设计在 Chrome 浏览器每天数百亿次的任务投递中经过了充分验证。

我们这一系列的目标不是照搬 Chromium 的实现（那个实现非常复杂，涉及手写的引用计数、`TRIVIAL_ABI` 注解、函数指针分派表），而是利用 C++23 的新特性——特别是 `std::move_only_function` 和 deducing this——来实现一个保留了 Chromium 设计精髓、但代码量可控的 `OnceCallback` 组件。

> **学习目标**
>
> - 理解"move-only + 一次性消费"为什么是回调的正确语义约束
> - 设计 `OnceCallback<R(Args...)>` 的完整公共接口
> - 分析 Chromium `OnceCallback` 的内部架构，理解每个设计决策背后的原因

---

## 我们的问题：`std::function` 在异步场景的三大缺陷

在动手设计之前，我们先把问题拆清楚。`std::function` 作为通用的可调用对象容器，在设计上是成功的——但在异步回调这个特定场景下，它有三个让笔者血压拉满的问题。

**第一，可复制。** `std::function` 天生支持拷贝，这意味着一个回调可以被复制到任意多个地方。在异步系统中，这等于允许多个执行路径同时持有同一份回调的副本。如果回调里捕获了 move-only 的资源（比如 `std::unique_ptr`），拷贝直接编译失败；如果捕获的是裸指针或引用，多个副本同时执行就会产生竞态。Chrome 团队的思路很直接：既然异步任务回调从根本上就不应该被复制，那就让它在类型层面不可拷贝。

**第二，可重复调用。** `std::function::operator()` 对调用次数没有任何约束。你可以在同一个 `std::function` 上调一千次，它照跑不误。但在异步回调场景里，一个文件读取完成的回调被调用两次就是逻辑错误——它可能触发两次资源释放、两次状态转换、两次消息发送。这种错误在类型系统里完全检测不到，只能靠运行时的断言（如果有的话）或者——更常见的情况——靠 bug 现场来发现。

**第三，无法表达消费语义。** 在 Chrome 的任务投递模型中，一个 `PostTask(FROM_HERE, callback)` 调用之后，`callback` 就不应该再被使用——它的所有权已经转移给了任务系统。`std::function` 的 `operator()` 是 `const` 限定的，调用它不会改变 `std::function` 对象本身的状态，所以你无法通过调用接口来表达"调用即消费"这个语义。

这三个问题归结到一点：`std::function` 的接口设计无法表达"这个回调只能被调用一次，调用后即失效"这个约束。Chrome 的 `OnceCallback` 正是为了填补这个语义空白而设计的。

---

## Chromium 的回答：`OnceCallback` 设计哲学

Chrome 的回调系统建立在一条核心原则之上：**消息传递优于锁，序列化优于线程**。在这个原则下，每个投递到任务系统的回调（Chrome 里叫 task）都是一个独立的、一次性的消息。投递之后，回调的所有权就从调用方转移到了任务系统；执行之后，回调就被销毁。没有共享，没有复用，没有歧义。

这个哲学直接体现在 `OnceCallback` 的类型设计上：

- **Move-only**：`OnceCallback` 删除了拷贝构造和拷贝赋值，只保留移动操作。这从类型层面保证了回调在任意时刻只有一个持有者。
- **右值限定 `Run()`**：`OnceCallback::Run()` 只能通过右值引用调用（`std::move(cb).Run(args...)`）。左值调用会触发 `static_assert`，产生一条明确的编译错误。这从语法层面提醒调用方："你在消费这个回调，之后别再用了。"
- **单次消费**：`Run()` 内部会通过引用计数机制销毁 `BindState`，使得后续对同一对象的任何访问都是安全的空操作。

Chrome 实际上还有 `RepeatingCallback`——一个可复制的、可重复调用的版本。两个回调类共享同一套 `BindState` 内部实现，区别仅在于 `Run()` 的值类别限定和 `BindState` 的所有权语义。这种设计允许同一套绑定基础设施同时服务于"一次性任务"和"重复监听器"两种截然不同的使用模式。

### Chromium 内部实现概览

我们不用深入 Chromium 的每一行源码，但需要理解它的核心架构，因为我们的 `OnceCallback` 会借鉴同样的分层思路，只是用 C++23 的标准设施来简化实现。

Chromium 的回调系统由三个层次组成，从底到顶依次是：

**底层：`BindStateBase`**——类型擦除的基类。它带引用计数，但有趣的是，它**不使用虚函数**。取而代之的是三个函数指针成员：`polymorphic_invoke_`（负责调用）、`destructor_`（负责析构）、`query_cancellation_traits_`（负责取消查询）。Chrome 团队选择函数指针而非虚函数的原因是减少二进制文件膨胀。虚函数会为每个模板实例化生成一个独立的 vtable（虚函数表），如果一个项目里有 100 种不同的 `BindState<Functor, BoundArgs...>` 实例化，就会有 100 个 vtable。而函数指针的方式可以复用同一份静态函数，只有指向函数的指针值不同，不会产生额外的代码段。

**中间层：`BindState<Functor, BoundArgs...>`**——模板化的具体类，继承自 `BindStateBase`。它存储了真正的可调用对象（`Functor`）和通过 `BindOnce` 绑定的参数（`BoundArgs...`）。你可以把它理解为一个"装着所有东西的盒子"：盒子里有你的 lambda、绑定的参数、以及基类要求的那些函数指针。这个类的实例通过 `scoped_refptr`（Chromium 自己实现的 intrusive 引用计数智能指针）管理生命周期——`OnceCallback` 在 `Run()` 时释放引用，`RepeatingCallback` 在每次 `Run()` 时保持引用。

**顶层：`OnceCallback<Signature>` 和 `RepeatingCallback<Signature>`**——用户直接操作的类型。它们本质上是 `BindStateHolder` 的薄包装，而 `BindStateHolder` 只是一个带 `TRIVIAL_ABI` 注解的 `scoped_refptr<BindStateBase>`。`TRIVIAL_ABI` 是 Clang 的扩展属性，告诉编译器"这个类型可以像 int 一样在寄存器中传递"，这使得 `OnceCallback` 的实际大小只有一个指针（8 字节），移动操作仅仅是复制一个指针——极其轻量。

这三层之间的关系可以用一句话概括：**顶层回调对象只是一个指向中间层盒子的指针，盒子里装着底层要求的函数指针和真正的数据**。我们接下来设计的 `OnceCallback` 会保留这个"外层接口 + 中间存储 + 类型擦除"的分层思路，但用 `std::move_only_function` 来替代 Chromium 手写的 `BindState` + `scoped_refptr` 组合，用 deducing this 来替代 `const&` 重载 + `static_assert` 的 hack。

---

## 环境说明

先确认一下我们的工具链。`OnceCallback` 依赖以下 C++23 特性：

- **`std::move_only_function`**（`<functional>`）：C++23 引入的 move-only 类型擦除可调用包装器，是我们的核心构建块
- **Deducing this**（显式对象参数 `this auto&& self`）：C++23 特性，允许在成员函数中推导 `this` 的值类别
- **`if consteval`**：编译期条件判断（部分实现中可能用到）

编译器要求方面，GCC 12+ 或 Clang 16+ 可以完整支持上述特性。编译时加 `-std=c++23` 即可。可以用下面这段代码快速验证环境：

```cpp
#include <functional>

// 验证 std::move_only_function 可用
static_assert(__cpp_lib_move_only_function >= 202110L);

// 验证 deducing this 可用（编译通过即说明支持）
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

如果这段代码编译通过，说明环境就绑了。不过说实话，截止笔者写这篇文章时，部分编译器的 `std::move_only_function` 实现还有 bug（比如 GCC 12 的早期版本在某些 SFINAE 场景下会编译失败），建议使用 GCC 13+ 或 Clang 17+ 的最新稳定版本。

### 前置知识

我们假设读者已经熟悉以下内容（对应的卷二文章已经覆盖）：

- **移动语义与完美转发**：`OnceCallback` 的核心就是 move-only，如果对 `std::move` 和 `std::forward` 的原理不熟，实现过程中会非常痛苦。对应文章：卷二 ch00 移动语义系列。
- **`std::function` 的类型擦除与 SBO**：我们直接在 `std::move_only_function` 之上构建，需要理解类型擦除的基本原理和小对象优化是什么、为什么重要。对应文章：卷二 ch03 `std::function` 与可调用对象。
- **`std::invoke` 与统一调用协议**：`bind_once` 内部用 `std::invoke` 来统一处理函数指针、成员函数指针、仿函数等不同类型的可调用对象。对应文章：同上。
- **可变参数模板与参数包展开**：`OnceCallback<R(Args...)>` 的模板特化、`bind_once` 的参数绑定都需要熟悉参数包语法。对应文章：卷二 ch00 完美转发、卷四 模板基础。
- **`std::invoke` 与统一调用协议**：`bind_once` 内部用 `std::invoke` 来统一处理函数指针、成员函数指针、仿函数等不同类型的可调用对象。对应文章：同上。
- **可变参数模板与参数包展开**：`OnceCallback<R(Args...)>` 的模板特化、`bind_once` 的参数绑定都需要熟悉参数包语法。对应文章：卷二 ch00 完美转发、卷四 模板基础。

---

## 设计接口：我们想要什么样的 API

我们先把目标 API 定下来，再回头讨论每个设计决策。这是工程师的工作方式——先想清楚"我要什么"，再想"怎么做"。

### 核心用法

```cpp
#include "once_callback/once_callback.hpp"

// 1. 构造：从 lambda 创建
using namespace tamcpp::chrome;
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// 2. 调用：必须通过右值（std::move）
int result = std::move(cb).run(3, 4);  // result == 7

// 3. 调用后，cb 被消费
// std::move(cb).run(1, 2);  // 运行时断言失败：callback already consumed
```

### 参数绑定

```cpp
// bind_once：预绑定部分参数，返回一个 OnceCallback
using namespace tamcpp::chrome;
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // 预绑定前两个参数
);

int r = std::move(bound).run(30);  // r == 60
```

### 取消检查

```cpp
using namespace tamcpp::chrome;
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// 检查回调是否仍然有效
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid：乐观检查，适用于跨序列场景
if (cb.maybe_valid()) {
    // "可能"有效，不保证
    std::move(cb).run(42);
}
```

### 链式组合

```cpp
using namespace tamcpp::chrome;
// then()：将当前回调的返回值传给下一个回调
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
}).then([](int sum) {
    return sum * 2;
});

int final_result = std::move(pipeline).run(3, 4);
// final_result == 14  (3+4)*2
```

### 接口设计决策分析

现在我们逐个讨论这些 API 背后的设计决策。

**为什么是 `run()` 而不是 `operator()`？**

Chromium 用的是 `Run()`（Google C++ 风格要求大写开头）。我们用 `run()` 符合 snake_case 命名规范。但更深层的原因是语义区分：`operator()` 太通用，任何可调用对象都有 `operator()`；`run()` 明确表达了"执行任务"的语义，在代码审查时一眼就能看出这是在消费一个 `OnceCallback`，而不是调用一个普通的可调用对象。

**为什么 `run()` 必须通过右值调用？**

这是整个设计中最关键的一点。我们需要一种机制，让 `cb.run(args)`（左值调用）编译失败，而 `std::move(cb).run(args)`（右值调用）编译通过。Chromium 的实现是通过两个重载来达成的：一个 `Run() &&` 是真正的执行版本，一个 `Run() const&` 内部放了一个 `static_assert(!sizeof(*this))` 来产生编译错误。这个 hack 虽然有效但很丑。

我们利用 C++23 的 **deducing this**（显式对象参数）可以做得更优雅。简单来说，deducing this 允许我们在成员函数里把 `this` 显式写成一个模板参数，编译器会根据调用时对象是左值还是右值来推导这个参数的类型。利用这个特性，`run(this auto&& self, Args... args)` 通过推导 `self` 的值类别来区分左值和右值调用，在编译期就拦截非法用法：

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    // ... 实际调用逻辑
}
```

当调用方写 `cb.run(args)` 时，`Self` 被推导为 `OnceCallback&`（左值引用），`static_assert` 触发，报错信息直接告诉调用方该怎么做。当写 `std::move(cb).run(args)` 时，`Self` 被推导为 `OnceCallback`（右值），编译通过。deducing this 的具体工作机制和与 Chromium 方案的详细对比，我们在下一篇的实现篇里会展开讲。

**为什么要区分 `is_cancelled()` 和 `maybe_valid()`？**

这个设计直接来自 Chromium 的 `CancellationQueryMode`。区别在于安全保证的强弱。`is_cancelled()` 提供确定性回答——它只能在回调绑定的序列上调用，保证返回准确的结果。`maybe_valid()` 提供乐观估计——它可以从任何线程调用，但结果可能过时。在实际使用中，`is_cancelled()` 用于"在投递前检查是否还有意义"的判断，`maybe_valid()` 用于"跨线程快速检查是否值得投递"的优化路径。

在我们的简化实现中，这两个方法都通过 `CancelableToken` 来查询——`is_cancelled()` 检查状态是否有效以及令牌是否仍然有效，`maybe_valid()` 就是 `!is_cancelled()` 的简单包装。后续如果需要更精细的线程安全语义，可以在这两个方法上做区分。

**`then()` 为什么消费 `*this`？**

`then()` 的语义是"把当前回调的执行结果传给下一个回调"。这要求当前回调在 `then()` 返回的新回调中被完整捕获（capture）。如果 `then()` 不消费 `*this`，就会导致同一个回调同时存在于两个地方——原位置和 `then()` 返回的新回调中——这违反了 move-only 的语义约束。所以 `then()` 被声明为右值限定成员函数（`then(...) &&`），调用后原回调对象进入已消费状态。

---

## 内部机制：类型擦除的两层架构

接口设计好了，我们来看看内部应该怎么组织。Chromium 用了 `BindStateBase` + `scoped_refptr` + 函数指针表这套组合拳来实现类型擦除，效果很好但代码量惊人。我们的策略是用 `std::move_only_function` 来承担类型擦除和小对象优化的脏活累活，把精力集中在消费语义、参数绑定和链式组合这些有趣的部分上。

### 为什么选 `std::move_only_function`

`std::move_only_function<R(Args...)>` 是 C++23 引入的，它的定位就是"move-only 版本的 `std::function`"。它内部实现了类型擦除和 SBO，行为和 `std::function` 类似，但删除了拷贝操作。

你可能已经注意到了 `OnceCallback<R(Args...)>` 这种写法——`R(Args...)` 看起来像一个函数声明，但在模板参数的上下文中，它是一个**函数类型**（function type）。`int(int, int)` 描述的是"接受两个 int 参数、返回 int 的函数"，它是一种合法的 C++ 类型。我们通过模板偏特化来拆解这个类型——下一篇会详细讲解这个技巧。

用 `std::move_only_function` 做内部存储有几个好处。它省去了我们手写类型擦除的工作——回想卷二的 `LightCallback`，我们花了一整个章节来手写函数指针表、SBO 缓冲区、移动/析构操作，而 `std::move_only_function` 把这些全部封装好了，直接拿来用。它也天然支持 move-only 的可调用对象——如果我们的回调捕获了 `std::unique_ptr`，`std::function` 会因为拷贝语义的要求直接编译失败，而 `std::move_only_function` 没有这个问题。而且它的 SBO 实现经过了标准库作者的精心调优，在绝大多数情况下不需要堆分配——对于捕获少量参数的 lambda 来说，性能完全够用。

### 三态管理

引入 `std::move_only_function` 之后，有一个设计问题需要解决：如何区分"空回调"和"已消费回调"？

`std::move_only_function` 本身可以是空的（默认构造或从 `nullptr` 构造），但"空"和"已被 `run()` 消费过"是两个不同的状态。空回调意味着"从未被赋值过"，调用它应该触发一个明确的错误（"callback is null"）。已消费回调意味着"曾经有值，但已经被调用过了"，调用它也应该触发错误（"callback already consumed"），但错误信息不同，这对调试很有帮助。

所以我们的内部状态需要三态：

```cpp
enum class Status : uint8_t {
    kEmpty,     // 默认构造，从未被赋值
    kValid,     // 持有有效的可调用对象
    kConsumed   // 已被 run() 消费
};
```

结合 `std::move_only_function`，我们的内部存储结构大致如下：

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    std::move_only_function<FuncSig> func_;
    Status status_ = Status::kEmpty;

    // 取消令牌（可选）
    std::shared_ptr<CancelableToken> token_;
};
```

移动构造时，`func_` 和 `status_` 一起移动过去，源对象的状态设为 `kEmpty`。`run()` 执行时，先检查 `status_` 是否为 `kValid`，执行完后将 `func_` 置空、`status_` 设为 `kConsumed`。这样在调试时就能根据 `status_` 的值给出精确的错误信息。

### 与 Chromium 原版的取舍

用 `std::move_only_function` 做底层存储，我们获得了简洁的实现，但也牺牲了一些东西。Chromium 的 `OnceCallback` 大小只有一个指针（8 字节），这得益于 `TRIVIAL_ABI` 注解和引用计数的 `BindState`——回调对象本身只是一个指向堆上 `BindState` 的指针。我们的 `OnceCallback` 包装了 `std::move_only_function`（通常 32 字节）加上 `Status` 枚举和可选的 `CancelableToken` 指针（16 字节），总大小大约在 56-64 字节左右。

另一个差异是引用计数。Chromium 的 `BindState` 是引用计数的，允许多个回调共享同一份绑定状态（这对 `RepeatingCallback` 的拷贝语义是必需的）。我们的实现里，`std::move_only_function` 本身是独占所有权的，不支持共享。对于 `OnceCallback` 的 move-only 语义来说这不是问题，但后续实现 `RepeatingCallback` 时需要重新考虑这个设计。

这些取舍是合理的——我们用大小和引用计数的灵活性，换来了大幅降低的实现复杂度。在实际使用中，56-64 字节的回调对象在绝大多数场景下都不是瓶颈，而清晰的代码结构让维护和扩展的成本低得多。

---

## 小结

这一篇我们完成了 `once_callback` 的设计基础。核心要点：

- `std::function` 在异步回调场景有三大缺陷：可复制、可重复调用、无法表达消费语义
- Chromium 的 `OnceCallback` 通过 move-only + 右值限定 `Run()` + 单次消费来约束回调语义
- 我们的 `OnceCallback` 用 `std::move_only_function` 做底层类型擦除，用 deducing this 实现右值限定的 `run()`
- 内部采用三态管理（`kEmpty` / `kValid` / `kConsumed`）区分空回调和已消费回调

下一篇我们会进入实现阶段：从核心骨架 `run()` 开始，逐步添加 `bind_once`、取消检查和 `then()` 链式组合。

## 参考资源

- [Chromium Callback 文档](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [Chromium callback.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
