---
chapter: 1
cpp_standard:
- 17
- 20
description: 对比 std::weak_ptr 与 Chrome WeakPtr，六种异步回调捕获模式的安全分析
difficulty: advanced
order: 5
platform: host
prerequisites:
- Chrome-like WeakPtr：引用计数控制块与 WeakPtrFactory
- 卷二 · 第一章：weak_ptr 与循环引用
reading_time_minutes: 7
related:
- 跨线程安全、性能取舍与设计原则总结
tags:
- host
- cpp-modern
- advanced
- 智能指针
- 异步编程
- 回调机制
title: std::weak_ptr 对比与异步回调实战
---
# std::weak_ptr 对比与异步回调实战

## 引言

前面四篇我们从头到尾手搓了一套非拥有指针类型——从 Borrowed 到 ObserverPtr 到各种 WeakPtr。现在到了把所有东西拉通对比的时候。

这一篇要做两件事：第一，把 `std::weak_ptr<T>` 和 Chrome-like `WeakPtr<T>` 放在一起，说清楚它们的核心差异；第二，用六种异步回调捕获模式做实战对比，让你直观感受到"错误捕获"和"正确捕获"之间的区别。

## std::weak_ptr 与 Chrome WeakPtr 的核心差异

先说一个经常被忽略的事实：**`std::weak_ptr<T>` 和 Chrome-like `WeakPtr<T>` 解决的不是同一个问题。**

`std::weak_ptr<T>` 解决的是"在 shared ownership 模型下的弱引用"。它依赖 `std::shared_ptr<T>` 的控制块，调用 `lock()` 成功后会获得一个 `shared_ptr<T>`，从而**临时延长对象的生命周期**。这意味着只要你 `lock()` 成功了，在你的 `shared_ptr` 存活期间，对象一定不会被析构。

Chrome-like `WeakPtr<T>` 解决的是"在非 shared_ptr 管理的对象上的弱引用"。它不依赖 `shared_ptr`，调用 `get()` 不会延长对象生命周期——它只是返回一个指针。对象可能在任何时候被析构，你拿到的指针可能在你用之前就失效了。它只保证你能**安全地检测到失效**，不保证你拿到指针后对象还活着。

这是两种完全不同的生命周期策略：

| 特性 | Chrome-like WeakPtr\<T\> | std::weak_ptr\<T\> |
|------|-------------------------|-------------------|
| 依赖 shared_ptr | 否 | 是 |
| 获取引用时延长生命周期 | **否** | **是**（lock 返回 shared_ptr） |
| 对象析构后安全判空 | 是 | 是 |
| 适合非 shared_ptr 管理的对象 | **是** | 否 |
| 天然跨线程安全 | 否（sequence-bound） | 部分（lock() 原子，但 T 的访问需要同步） |
| 控制块开销 | 小（自定义 ref count） | 较大（shared_ptr 的 control block） |

**什么时候用 `std::weak_ptr`？** 当对象已经由 `shared_ptr` 管理，你需要在异步场景中安全地观察它，且可能需要临时延长它的生命周期时。

**什么时候用 Chrome-like WeakPtr？** 当对象不是由 `shared_ptr` 管理（栈对象、`unique_ptr`、框架管理的对象），你需要在异步回调中安全地检测失效时。

**什么时候不应该用 `std::weak_ptr`？** 为了用 `weak_ptr` 而强行把对象改成 `shared_ptr` 管理。这会引入不必要的引用计数开销，而且在多线程下容易引发性能瓶颈（原子引用计数争用）。

## 六种异步回调捕获模式

接下来我们用实际代码来对比六种在异步回调中捕获对象引用的方式。每种方式我们都会分析：哪里危险、对象销毁后会发生什么、是不是 UB。

### 模式 1：捕获裸 `this` —— 危险

```cpp
class NetworkClient {
public:
    void start_request()
    {
        // 错误！lambda 捕获了裸 this
        timer_.schedule(1000ms, [this]() {
            process_response();  // 如果 NetworkClient 已析构，this 是悬垂指针
        });
    }

    void process_response() { /* ... */ }

private:
    Timer timer_;
};

// 使用场景
void test()
{
    auto client = std::make_unique<NetworkClient>();
    client->start_request();
    // client 在这里析构
}  // 1 秒后回调执行 → this 悬垂 → UB
```

**问题**：`this` 只是一个裸指针，不携带任何生命周期信息。对象析构后，回调中的 `this` 是悬垂指针，任何成员访问都是 UB。这是 C++ 异步编程中最常见的崩溃来源。

### 模式 2：捕获 `T*` —— 同样危险

```cpp
void start_request()
{
    auto* raw_ptr = this;
    timer_.schedule(1000ms, [raw_ptr]() {
        raw_ptr->process_response();  // 同样的悬垂问题
    });
}
```

**问题**：和捕获 `this` 没有本质区别。`T*` 不提供任何生命周期保障。唯一的区别是它"看起来"像是有意识地捕获了一个指针，但实际上没有比裸 `this` 更安全。

### 模式 3：捕获 `ObserverPtr<T>` —— 仍然危险

```cpp
void start_request()
{
    auto obs = make_observer(this);
    timer_.schedule(1000ms, [obs]() {
        if (obs) {
            obs->process_response();  // ObserverPtr::operator bool 只检查是否为 nullptr
        }                            // 对象销毁后 obs.get() 仍非 nullptr → 悬垂解引用
    });
}
```

**问题**：`ObserverPtr` 的 `operator bool()` 只检查内部指针是否为 `nullptr`。对象析构后，内部指针不是 `nullptr`（它是悬垂的），所以 `if (obs)` 会通过，然后解引用悬垂指针。UB。

### 模式 4：捕获 `UnsafeWeakPtr<T>` —— UB

```cpp
void start_request()
{
    auto weak = get_unsafe_weak_ptr();
    timer_.schedule(1000ms, [weak]() {
        if (weak.is_valid()) {  // 访问已销毁的 Flag → UB！
            // ...
        }
    });
}
```

**问题**：如第二篇详细分析的，`is_valid()` 访问的 `Flag*` 可能已经是悬垂指针。判空动作本身就是 UB。这是六种模式中最隐蔽的危险——看起来有"判活"机制，实际上连判活都不安全。

### 模式 5：捕获 Chrome-like `WeakPtr<T>` —— 正确

```cpp
class NetworkClient {
public:
    void start_request()
    {
        auto weak = factory_.get_weak_ptr();
        timer_.schedule(1000ms, [weak]() {
            if (auto* self = weak.get()) {
                self->process_response();  // 安全：get() 先检查 control block
            }                             // 失效时返回 nullptr，不会解引用
        });
    }

private:
    Timer timer_;
    WeakPtrFactory<NetworkClient> factory_{this};
};
```

**分析**：`weak.get()` 先检查 `WeakFlag::is_valid()`。由于 `WeakFlag` 是引用计数的，只要 `weak` 还活着，`WeakFlag` 就一定存在，所以 `is_valid()` 不会 UB。对象析构后 Factory 的析构函数会 invalidate `WeakFlag`，`get()` 返回 `nullptr`，回调安全跳过。

**但有一个前提**：回调的执行和对象的析构在同一个 sequence 上。如果跨 sequence，`get()` 返回非空之后、实际使用 `self` 之前，另一个 sequence 可能正在析构对象——这就是 TOCTOU 竞态。

### 模式 6：捕获 `std::weak_ptr<T>` —— 正确

```cpp
class NetworkClient : public std::enable_shared_from_this<NetworkClient> {
public:
    void start_request()
    {
        auto weak = weak_from_this();  // C++17
        timer_.schedule(1000ms, [weak]() {
            if (auto self = weak.lock()) {
                self->process_response();  // lock() 成功 → shared_ptr 延长生命周期
            }                             // 在 self 的作用域内，对象不会被析构
        });
    }

private:
    Timer timer_;
};

// 使用时必须用 shared_ptr 管理
auto client = std::make_shared<NetworkClient>();
client->start_request();
```

**分析**：`weak.lock()` 是原子操作——它要么返回一个有效的 `shared_ptr`（同时引用计数 +1），要么返回空。如果返回了有效的 `shared_ptr`，在你的 `self` 变量存活期间，对象一定不会被析构。这比 Chrome WeakPtr 更安全——它不仅检测失效，还能防止在检测和使用之间对象被析构。

**但代价是**：对象必须由 `shared_ptr` 管理，`lock()` 会增加原子引用计数操作。在高频异步场景下，这些原子操作可能成为性能瓶颈。

## 六种模式总结

| 模式 | 判活能力 | 对象销毁后的行为 | UB？ | 适合场景 |
|------|---------|----------------|------|---------|
| 裸 `this` | 无 | 悬垂指针访问 | 是 | 无——永远不要在异步回调中捕获裸 this |
| `T*` | 无 | 悬垂指针访问 | 是 | 无——同上 |
| `ObserverPtr<T>` | 无 | `operator bool` 通过但指针悬垂 | 是 | 同步观察，不用于异步回调 |
| `UnsafeWeakPtr<T>` | 假的 | 判空本身 UB | 是 | 无——不应该使用 |
| Chrome `WeakPtr<T>` | 有（control block） | 安全返回 nullptr | 否（单 sequence） | 非 shared_ptr 对象的异步回调 |
| `std::weak_ptr<T>` | 有（shared_ptr 控制） | 安全返回空 shared_ptr | 否 | shared_ptr 管理的对象的异步回调 |

## 小结

- `std::weak_ptr<T>` 依赖 `shared_ptr`，`lock()` 会临时延长对象生命周期
- Chrome-like `WeakPtr<T>` 不依赖 `shared_ptr`，不延长对象生命周期，只检测失效
- 不要为了用 `weak_ptr` 而强行把对象改成 `shared_ptr` 管理
- 异步回调中永远不要捕获裸 `this`、裸 `T*`、`ObserverPtr` 或 `UnsafeWeakPtr`
- Chrome `WeakPtr` 适合非 `shared_ptr` 场景，但要注意 sequence 绑定
- `std::weak_ptr` 适合 `shared_ptr` 场景，`lock()` 提供更强的安全保证

## 参考资源

- [std::weak_ptr - cppreference](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [std::enable_shared_from_this - cppreference](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this)
- [C++ Core Guidelines - CP.51: Do not use capturing lambdas that are coroutines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Chromium WeakPtr 设计文档](https://www.chromium.org/developers/weak-ptrs-in-chromium/)
