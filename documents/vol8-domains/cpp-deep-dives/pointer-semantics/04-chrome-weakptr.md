---
chapter: 1
cpp_standard:
- 17
- 20
description: 实现教学版 Chrome WeakPtr，理解 ref-counted control block 与序列绑定模型
difficulty: advanced
order: 4
platform: host
prerequisites:
- SimpleWeakPtr：T* + shared_ptr<Flag> 的安全改进
reading_time_minutes: 9
related:
- std::weak_ptr 对比与异步回调实战
tags:
- host
- cpp-modern
- advanced
- 智能指针
- 引用计数
- 回调机制
title: Chrome-like WeakPtr：引用计数控制块与 WeakPtrFactory
---
# Chrome-like WeakPtr：引用计数控制块与 WeakPtrFactory

## 引言

上一篇我们用 `shared_ptr<Flag>` 解决了 control block 的生命周期安全问题。它确实管用，但也带来了 `shared_ptr` 自身的开销——堆分配、两个原子引用计数（strong count + weak count）、控制块对象本身的内存占用。

对于一个只是存了 `bool alive` 的小结构来说，这些开销有点重了。

Chromium 项目很早就遇到了这个问题。Chrome 的代码库里到处都是异步回调、定时器、消息循环——它们需要 WeakPtr，但不需要也不应该用 `shared_ptr` 来管理所有对象。所以 Chrome 设计了一套自己的 WeakPtr 机制，核心思路是：**用引用计数的 control block 来管理失效状态，但这个 control block 比 `shared_ptr` 的控制块简单得多。**

这一篇我们来实现一个教学版的 Chrome-like WeakPtr，理解它为什么比 `shared_ptr<Flag>` 更轻量、比 `raw Flag*` 更安全。

## 核心设计思路

Chrome 的 WeakPtr 设计有几个关键特征：

**第一，control block 是引用计数的，但不使用 `shared_ptr`。** Chrome 自己管理引用计数，只维护一个简单的计数器——没有 weak count，没有自定义删除器，没有 allocator 支持。这意味着控制块可以更小、更快。

**第二，Factory 模式。** 创建 WeakPtr 的唯一途径是通过 `WeakPtrFactory<T>`。Factory 持有控制块并负责在 Owner 析构时 invalidate 所有 WeakPtr。这种集中式管理避免了"谁来 invalidate"的混乱。

**第三，序列绑定（Sequence-bound）。** Chrome 的 WeakPtr 设计上不是跨线程安全的——它假设所有使用同一个 WeakPtr 的代码都跑在同一个 sequence（逻辑线程）上。这和 `std::weak_ptr` 的跨线程设计有本质区别。

接下来我们实现教学版。

## 实现

### WeakFlag —— 引用计数的控制块

```cpp
// weak_flag.h
// 教学版引用计数控制块

#pragma once

#include <atomic>

class WeakFlag {
public:
    WeakFlag() = default;

    // 禁止拷贝和移动——控制块是不可复制的
    WeakFlag(const WeakFlag&) = delete;
    WeakFlag& operator=(const WeakFlag&) = delete;

    void add_ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    void release()
    {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    void invalidate()
    {
        is_valid_.store(false, std::memory_order_release);
    }

    bool is_valid() const
    {
        return is_valid_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> is_valid_{true};
    std::atomic<int> ref_count_{1};  // Factory 初始持有一份引用
    // 注意：不使用虚析构函数，不使用 delete 的自定义删除器
    // 这个控制块的设计目标是比 shared_ptr 的控制块更轻量

    ~WeakFlag() = default;
};
```

和 `shared_ptr` 的控制块相比，`WeakFlag` 只有两个原子变量：`is_valid_` 和 `ref_count_`。没有 strong/weak 双计数、没有虚析构、没有 allocator。一个 `WeakFlag` 对象只有 8 字节（`atomic<bool>` 1 字节 + 对齐填充 3 字节 + `atomic<int>` 4 字节）。

### WeakPtr\<T\>

```cpp
// weak_ptr.h
// 教学版 Chrome-like WeakPtr<T>

#pragma once

#include "weak_flag.h"

template <typename T>
class WeakPtr {
public:
    WeakPtr() : ptr_(nullptr), flag_(nullptr) {}

    WeakPtr(T* ptr, WeakFlag* flag) : ptr_(ptr), flag_(flag)
    {
        if (flag_) {
            flag_->add_ref();
        }
    }

    // 拷贝构造：增加引用计数
    WeakPtr(const WeakPtr& other) : ptr_(other.ptr_), flag_(other.flag_)
    {
        if (flag_) {
            flag_->add_ref();
        }
    }

    // 移动构造：转移引用
    WeakPtr(WeakPtr&& other) noexcept
        : ptr_(other.ptr_), flag_(other.flag_)
    {
        other.ptr_ = nullptr;
        other.flag_ = nullptr;
    }

    // 赋值
    WeakPtr& operator=(const WeakPtr& other)
    {
        if (this != &other) {
            // 先释放旧的
            if (flag_) {
                flag_->release();
            }
            ptr_ = other.ptr_;
            flag_ = other.flag_;
            if (flag_) {
                flag_->add_ref();
            }
        }
        return *this;
    }

    WeakPtr& operator=(WeakPtr&& other) noexcept
    {
        if (this != &other) {
            if (flag_) {
                flag_->release();
            }
            ptr_ = other.ptr_;
            flag_ = other.flag_;
            other.ptr_ = nullptr;
            other.flag_ = nullptr;
        }
        return *this;
    }

    // 析构：减少引用计数
    ~WeakPtr()
    {
        if (flag_) {
            flag_->release();
        }
    }

    // 检查是否有效
    bool is_valid() const { return flag_ && flag_->is_valid(); }

    // 获取指针
    T* get() const
    {
        if (is_valid()) {
            return ptr_;
        }
        return nullptr;
    }

    T& operator*() const { return *get(); }
    T* operator->() const { return get(); }
    explicit operator bool() const { return get() != nullptr; }

private:
    T* ptr_;
    WeakFlag* flag_;
};
```

### WeakPtrFactory\<T\>

```cpp
// weak_ptr_factory.h
// 教学版 WeakPtrFactory<T>

#pragma once

#include "weak_flag.h"
#include "weak_ptr.h"

template <typename T>
class WeakPtrFactory {
public:
    explicit WeakPtrFactory(T* owner) : owner_(owner)
    {
        // Factory 创建时分配 control block
        flag_ = new WeakFlag();
    }

    // 禁止拷贝和移动——Factory 和 Owner 绑定
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    // 创建一个新的 WeakPtr
    WeakPtr<T> get_weak_ptr()
    {
        return WeakPtr<T>(owner_, flag_);
    }

    // 使所有已发出的 WeakPtr 失效
    void invalidate_weak_ptrs()
    {
        if (flag_) {
            flag_->invalidate();
        }
    }

    // Factory 析构时自动 invalidate
    ~WeakPtrFactory()
    {
        invalidate_weak_ptrs();
        // Factory 释放自己持有的引用
        // 如果还有 WeakPtr 活着，flag_ 不会被 delete
        // 最后一个 WeakPtr 析构时才会 delete flag_
        if (flag_) {
            flag_->release();
        }
        flag_ = nullptr;
    }

private:
    T* owner_;
    WeakFlag* flag_;
};
```

## 为什么 control block 要引用计数

和第三篇的 `shared_ptr<Flag>` 一样，引用计数的目的是保证 control block 活得比所有 WeakPtr 都久。但 Chrome 的实现比 `shared_ptr` 更轻量，因为：

**计数器只有一个。** `shared_ptr` 内部有 strong count 和 weak count 两个原子变量。`WeakFlag` 只有一个 `ref_count_`——因为这里没有"共享所有权"的概念，只有"谁还持有着这个控制块"的计数。

**没有控制块的堆上额外管理开销。** `shared_ptr` 的控制块通常通过 `new` 分配（除非用 `make_shared`），并且要维护虚析构函数表、allocator 信息等。`WeakFlag` 就是简单的 `new` + `delete`，没有额外开销。

**失效机制更直接。** `shared_ptr<Flag>` 的失效需要通过修改 Flag 的成员变量，而 `WeakFlag::invalidate()` 直接修改原子变量——一次原子 store。

## 为什么它比 raw Flag* 安全

这个问题上一篇已经回答过了，但用 `WeakFlag` 再说一遍：

`raw Flag*` 的问题是 Flag 的生命周期绑定在 Factory/Owner 上。Factory 析构 → Flag 析构 → 外部 WeakPtr 手里的 `flag_` 悬垂 → `is_valid()` 就是 UB。

`WeakFlag*` + 引用计数解决了这个问题。Factory 析构时调用 `flag_->release()` 把引用计数 -1，但只要还有 WeakPtr 活着，引用计数就还 > 0，`WeakFlag` 对象就不会被 `delete`。`is_valid()` 访问的一定是一个还活着的 `WeakFlag` 对象。

## 为什么它比 std::weak_ptr 更适合某些场景

`std::weak_ptr<T>` 依赖 `std::shared_ptr<T>` 的控制块。如果你想用 `std::weak_ptr<T>`，你必须先用 `std::shared_ptr<T>` 来管理对象。但很多场景下对象并不是由 `shared_ptr` 管理的——它们可能是栈上对象、`unique_ptr` 管理的堆对象、或者属于某个框架的对象池。为了用 `weak_ptr` 而强行把所有对象都改成 `shared_ptr` 管理，是一种常见的过度设计。

Chrome-like WeakPtr 不要求对象由 `shared_ptr` 管理。它只要求对象内部有一个 `WeakPtrFactory<T>` 成员——对象本身可以是任何所有权模式。这使它非常适合 UI 框架、游戏引擎、网络库这些"对象生命周期由框架管理、不是由 shared_ptr 管理"的场景。

## 序列绑定模型：为什么它不是跨线程安全的

Chrome 的 WeakPtr 在设计上假设所有使用者都跑在同一个 sequence 上。一个 sequence 是一个逻辑上的执行顺序——可以是单线程，也可以是带消息循环的多线程（每个线程有自己的 task runner）。

在这个假设下，`is_valid()` 和 `get()` 之间不会有 TOCTOU 竞态——因为 invalidate 和 get 不可能同时执行（它们在同一个 sequence 上排队执行）。

但如果跨 sequence 使用——比如一个 sequence 上 invalidate，另一个 sequence 上 get——就可能出现第三篇提到的竞态问题。`atomic<bool>` 保证 `is_valid()` 本身不会 UB，但"读到 valid=true 后访问 T"和"T 的析构"之间仍然可能有竞态。

所以 Chrome-like WeakPtr 的正确使用方式是：**同一个 sequence 上创建、使用和 invalidate。** 跨 sequence 的场景应该用 `std::weak_ptr` 或者额外的同步机制。

## 使用示例

```cpp
#include <iostream>
#include <memory>
#include "weak_ptr_factory.h"

class Session {
public:
    Session(int id) : id_(id) {}

    WeakPtr<Session> get_weak_ptr()
    {
        return factory_.get_weak_ptr();
    }

    void do_work()
    {
        std::cout << "Session " << id_ << " working\n";
    }

    int id() const { return id_; }

private:
    int id_;
    // Factory 作为最后一个成员变量——确保在其他成员析构之前 invalidate
    WeakPtrFactory<Session> factory_{this};
};

int main()
{
    WeakPtr<Session> weak = [] {
        auto s = std::make_unique<Session>(42);
        auto w = s->get_weak_ptr();
        std::cout << "Before destroy: valid = " << w.is_valid() << "\n";
        return w;
        // Session 在这里析构
        // factory_ 析构 → invalidate → release (ref_count: 2→1)
        // WeakFlag 仍然活着（weak 持有）
    }();

    // Session 已经销毁
    std::cout << "After destroy: valid = " << weak.is_valid() << "\n";
    std::cout << "get() returns: "
              << (weak.get() ? "non-null" : "nullptr") << "\n";

    // weak 析构 → release (ref_count: 1→0) → delete WeakFlag
}
```

输出：

```text
Before destroy: valid = true
After destroy: valid = false
get() returns: nullptr
```

和第二篇的 `UnsafeWeakPtr` 对比——同样的场景，`UnsafeWeakPtr` 会 UB，而 Chrome-like WeakPtr 安全返回 `false`。

## 小结

- Chrome-like WeakPtr 用自定义的引用计数 control block（`WeakFlag`）替代 `shared_ptr<Flag>`，更轻量
- `WeakPtrFactory<T>` 集中管理 control block 的创建和失效，避免混乱
- 引用计数保证 control block 活得比所有 WeakPtr 都久——`is_valid()` 永远安全
- 不要求对象由 `shared_ptr` 管理——适合框架内部的对象生命周期模式
- 设计上绑定到单个 sequence，不适合任意跨线程使用
- `atomic<bool>` 解决 Flag 数据竞争，但不解决 T 的并发访问安全

## 参考资源

- [Chromium Smart Pointer Guidelines](https://www.chromium.org/developers/smart-pointer-guidelines/)
- [Chromium 源码：base/memory/weak_ptr.h](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [C++ Core Guidelines - CP.50: Define a mutex together with the data it guards](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
