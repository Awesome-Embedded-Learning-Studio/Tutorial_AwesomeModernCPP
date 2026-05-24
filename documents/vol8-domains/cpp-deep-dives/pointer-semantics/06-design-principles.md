---
title: "跨线程安全、性能取舍与设计原则总结"
description: "生命周期安全不等于线程安全——指针语义设计的最终工程规则与推荐命名"
chapter: 1
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 智能指针
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 7
prerequisites:
  - "std::weak_ptr 对比与异步回调实战"
related:
  - "卷二 · 第一章：智能指针与 RAII"
cpp_standard: [17, 20]
---

# 跨线程安全、性能取舍与设计原则总结

## 引言

到这里，我们已经把非拥有指针的整个光谱走了一遍——从最简单的 `T*` 和 `T&`，到手搓的 `Borrowed<T>` 和 `ObserverPtr<T>`，再到三种弱引用方案（`UnsafeWeakPtr`、`SimpleWeakPtr`、Chrome-like `WeakPtr`），最后和 `std::weak_ptr` 做了全面对比。

这一篇是收尾，我们把三个还没彻底展开的话题说清楚：跨线程安全的边界到底在哪里，各种类型的性能开销具体差多少，以及把所有东西综合成一组可以落地的工程规则。

## 生命周期安全 ≠ 线程安全

这是整个专题最重要的一个结论，值得反复强调。

**生命周期安全**说的是"对象销毁后你能不能安全地检测到失效"。`WeakPtr` 的 control block 解决的就是这个问题——你可以安全地调用 `is_valid()` 或 `get()` 而不会 UB。

**线程安全**说的是"多个线程同时访问对象时会不会出问题"。这和生命周期安全是完全不同的问题维度。

我们用一张 2×2 的表格来把这四个象限说清楚：

|  | 生命周期不安全 | 生命周期安全 |
|------|--------------|------------|
| **线程不安全** | `T*`、`T&`、`ObserverPtr` | Chrome `WeakPtr`（单 sequence） |
| **线程安全（部分）** | N/A（没有意义） | `std::weak_ptr`（lock 原子，但 T 的内部状态需要同步） |

`T*` 在左上角——既不解决生命周期问题，也不解决线程问题。Chrome `WeakPtr` 解决了生命周期问题，但在跨线程场景下仍然有 TOCTOU 竞态。`std::weak_ptr` 的 `lock()` 是原子的，锁住之后对象不会被析构，但对象**内部状态**的并发访问仍然需要 mutex 或其他机制保护。

所以：`WeakPtr` 解决的是"我知道对象死了没有"，不是"多个线程同时碰这个对象安不安全"。

### 为什么 Chrome WeakPtr 是 sequence-bound 的

Chrome 的设计哲学是：大多数 UI 和异步框架中的回调都跑在同一个逻辑序列上。定时器回调、事件处理、IO 完成通知——它们由同一个 task runner 分发执行。在这个模型下，invalidate 和 get 不可能同时执行，因为它们排队运行。

这比"加一个 mutex 就跨线程安全了"要高效得多——mutex 有运行时开销，而 sequence-bound 是零开销的设计约束。代价是你的使用方式被限制了：不能跨 sequence 传递 WeakPtr。但这个约束在大多数 UI / 事件循环框架里是自然成立的。

### 跨线程场景应该怎么做

如果确实需要跨线程弱引用，有几种选择：

- **用 `std::weak_ptr`**：`lock()` 原子地获取 `shared_ptr`，在你的作用域内对象不会被析构。但 T 内部的线程安全需要另外处理。
- **用 `std::atomic<std::shared_ptr<T>>`**（C++20）：提供原子操作来安全地跨线程读写 `shared_ptr`。
- **用 message passing**：不直接跨线程共享 WeakPtr，而是通过消息队列传递"请你在你的 sequence 上做这件事"的请求，让目标 sequence 自己处理。

## 性能比较

我们把本专题涉及的所有类型做一个性能对比。数字是近似值，具体取决于平台和编译器：

| 类型 | 对象大小 | 控制块分配 | 原子操作 | 适合 |
|------|---------|-----------|---------|------|
| `T*` | 8B | 无 | 无 | 同步函数参数 |
| `T&` | 8B（指针实现） | 无 | 无 | 同步函数参数 |
| `Borrowed<T>` | 8B | 无 | 无 | 同步函数参数（语义显式） |
| `ObserverPtr<T>` | 8B | 无 | 无 | 类成员观察 |
| `UnsafeWeakPtr<T>` | 16B | 无 | 无 | 不应该使用 |
| `SimpleWeakPtr<T>` | 24B（T* + shared_ptr） | 1 次 `new` | 拷贝 1 次、析构 1~2 次原子操作 | 教学、简单场景 |
| Chrome `WeakPtr<T>` | 16B（`T*` + `WeakFlag*`） | 1 次 `new` | 拷贝/析构各 1 次原子操作 | 框架内异步回调 |
| `std::weak_ptr<T>` | 16B | 由 `shared_ptr` 管理 | lock/unlock 各 2 次原子操作 | shared_ptr 体系 |

几个值得注意的细节：

`Borrowed<T>` 和 `ObserverPtr<T>` 的开销为零——编译器优化后和裸指针完全一样。它们的价值纯粹在语义层面。

`SimpleWeakPtr<T>` 比 Chrome `WeakPtr<T>` 大了 8 个字节，因为 `shared_ptr` 内部有两个指针（对象指针 + 控制块指针），而 Chrome 的 `WeakPtr` 只存 `T*` 和 `WeakFlag*`。每次拷贝 `shared_ptr` 需要两次原子操作（strong count + weak count），Chrome 只需要一次。

Chrome `WeakPtr` 的控制块（`WeakFlag`）比 `shared_ptr` 的控制块小得多——只有一个原子 bool 和一个原子 int，没有虚析构、没有 allocator、没有 weak count。

`std::weak_ptr` 的额外开销取决于它所依赖的 `shared_ptr`。如果你为了用 `weak_ptr` 而把一个本来不需要 `shared_ptr` 的对象强行改成 `shared_ptr` 管理，你不仅要付出控制块的开销，还引入了原子引用计数争用的风险。

## 工程规则

总结成一组可落地的规则：

**函数参数**优先用 `T&`、`T*` 或 `Borrowed<T>`。不要在函数参数上使用智能指针来表达非拥有关系。`Borrowed<T>` 提供最明确的语义（非空 + 非拥有），但 `const T&` 在大多数场景下也够用。

**类成员观察关系**可以用 `ObserverPtr<T>`。当你要表达"我观察它但不拥有它"时，`ObserverPtr<T>` 比裸 `T*` 的可读性好得多。但要记住它不能判活。

**异步回调**永远不要捕获裸 `this`、裸 `T*`、`ObserverPtr` 或任何没有独立 control block 的"弱引用"。正确的选择是 Chrome-like `WeakPtr<T>`（非 `shared_ptr` 场景）或 `std::weak_ptr<T>`（`shared_ptr` 场景）。

**不要把 `ObserverPtr` 当 WeakPtr 用**。`ObserverPtr` 只能表达"我不拥有它"，不能表达"我知道它还活着吗"。

**不要把 `T* + raw Flag*` 叫 WeakPtr**。如果 Flag 的生命周期绑定在 Owner 上，它不是可靠 WeakPtr。给它一个诚实的名字——`UnsafeWeakPtr` 或 `OwnerBoundWeakPtr`。

**跨线程场景**优先考虑 `std::weak_ptr<T>` 或 message passing。Chrome-like `WeakPtr` 设计上是 sequence-bound 的，不要把它当跨线程安全指针用。

**WeakPtr 解决生命周期感知，不解决线程安全**。无论用哪种 WeakPtr，T 内部状态的并发访问都需要额外的同步机制。

## 推荐命名体系

最后给出一套推荐的命名约定：

| 类型 | 命名 | 含义 |
|------|------|------|
| `Borrowed<T>` | 借用 | 非空、非拥有、短期使用、适合函数参数 |
| `ObserverPtr<T>` | 观察 | 可空、非拥有、不提供判活、适合类成员 |
| `UnsafeWeakPtr<T>` | 不安全弱引用 | `T*` + `raw Flag*`，命名明确标注不安全 |
| `WeakPtr<T>` | 安全弱引用 | 真正能在对象销毁后安全判空的弱引用 |
| `WeakPtrFactory<T>` | 弱引用工厂 | 集中创建和管理 WeakPtr 的失效 |

`UnsafeWeakPtr` 这个名字不是贬义——它是一种**诚实的命名**。当你在代码库里看到 `UnsafeWeakPtr`，你立刻就知道"这个东西有坑，使用时要注意约束条件"。比把它包装成 `WeakPtr` 然后在文档里埋一行小字说"保证 WeakPtr 不比 Owner 长"要负责任得多。

## 小结

- 生命周期安全和线程安全是两个正交的问题，WeakPtr 只解决前者
- Chrome `WeakPtr` 通过 sequence-bound 模型获得零开销的安全性，但限制跨线程使用
- `Borrowed` 和 `ObserverPtr` 运行时开销为零，价值在语义表达
- Chrome `WeakPtr` 的控制块比 `shared_ptr` 的控制块更轻量
- 不要为了用 `weak_ptr` 而强行引入 `shared_ptr`
- 命名应该诚实——不安全的东西就要叫不安全

这个专题到这里就全部结束了。我们从 `T*` 出发，手搓了 Borrowed、ObserverPtr、UnsafeWeakPtr、SimpleWeakPtr 和 Chrome-like WeakPtr，每一步都解释了设计理由和工程取舍。希望这些内容能帮你在实际工程中做出更清晰的指针语义选择。

## 参考资源

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Chromium Smart Pointer Guidelines](https://www.chromium.org/developers/smart-pointer-guidelines/)
- [std::weak_ptr - cppreference](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [GSL: Guidelines Support Library](https://github.com/microsoft/GSL)
- [P1408R0: Abandon observer_ptr (Stroustrup)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1408r0.pdf)
