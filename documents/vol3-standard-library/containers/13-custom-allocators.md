---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 讲透自定义分配器：Bump/池/栈三种策略的机制与取舍、placement new 与对象构造析构、C++17 std::pmr 的 memory_resource
  体系（monotonic/pool）与 pmr 容器，以及何时该自己管内存
difficulty: advanced
order: 13
platform: host
reading_time_minutes: 7
related:
- vector 深入：三指针、扩容与迭代器失效
tags:
- host
- cpp-modern
- advanced
- 内存管理
- 容器
title: 自定义分配器与 PMR：自己管内存
---
# 自定义分配器与 PMR：自己管内存

## 为什么需要自定义分配器

默认的 `new` / `malloc` 方便，但有几个软肋：分配时机不确定（可能阻塞实时任务）、产生堆碎片、局部性差、且对所有场景一刀切。当你碰到这些需求时，默认分配器就力不从心了——实时任务不能被偶发 malloc 拖住、启动期想一次性分配避免运行时分配、固定大小小对象高频分配、或想把一大块内存划给某模块便于追踪。这些场景下，自己管内存就成了工程师的基本修行。

分配器归根结底两件事：**分配**（给一段未用内存）和**释放**（归还）。在 C++ 里还要管对齐和对象的构造/析构。下面先看三种经典策略，理解机制；再看 C++17 给的标准库答案 `std::pmr`。

## 三种经典分配策略

### Bump（线性）分配器

最简单的分配器：维护一个指针，分配时指针上移，不支持释放单个对象（只能整体 reset）。分配 O(1)，适合启动期或短周期任务。

```cpp
#include <cstddef>
#include <cstdint>
#include <new>

class BumpAllocator {
    char* start_;
    char* ptr_;
    char* end_;
public:
    BumpAllocator(void* buffer, std::size_t size)
        : start_(static_cast<char*>(buffer)),
          ptr_(start_),
          end_(start_ + size) {}

    void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t)) noexcept
    {
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr_);
        std::size_t mis = p % align;
        std::size_t offset = mis ? (align - mis) : 0;
        if (n + offset > static_cast<std::size_t>(end_ - ptr_)) {
            return nullptr;
        }
        ptr_ += offset;
        void* res = ptr_;
        ptr_ += n;
        return res;
    }

    void reset() noexcept { ptr_ = start_; }
};
```

不能释放单个对象（除非加标记/回滚），但实现极简、极快。适合「分配一堆、用完一把 reset」的场景。

### 固定大小内存池（Free-list）

大量相同大小的小对象（消息节点、连接对象），用固定大小池：每个槽固定大小，释放时把槽挂回空闲链表。分配/释放都 O(1)，碎片少。

```cpp
class SimpleFixedPool {
    struct Node { Node* next; };
    void* buffer_;
    Node* free_head_;
    std::size_t slot_size_;
public:
    SimpleFixedPool(void* buf, std::size_t slot_size, std::size_t count)
        : buffer_(buf), free_head_(nullptr),
          slot_size_(slot_size < sizeof(Node*) ? sizeof(Node*) : slot_size)
    {
        char* p = static_cast<char*>(buffer_);
        for (std::size_t i = 0; i < count; ++i) {
            Node* n = reinterpret_cast<Node*>(p + i * slot_size_);
            n->next = free_head_;
            free_head_ = n;
        }
    }
    void* allocate() noexcept
    {
        if (!free_head_) return nullptr;
        Node* n = free_head_;
        free_head_ = n->next;
        return n;
    }
    void deallocate(void* p) noexcept
    {
        Node* n = static_cast<Node*>(p);
        n->next = free_head_;
        free_head_ = n;
    }
};
```

`slot_size` 要含对齐和控制信息；要线程安全就得加锁或上 lock-free。

### Stack（LIFO）分配器

分配/释放呈后进先出时最快，支持「标记 + 回滚到标记」。适合帧分配（每帧分配、帧末统一回收）、短生命周期链。它的 `allocate` 和 Bump 一样（指针上移 + 对齐），多了 mark / rollback：

```cpp
class StackAllocator {
    char* start_;
    char* top_;
    char* end_;
public:
    using Marker = char*;
    StackAllocator(void* buf, std::size_t size)
        : start_(static_cast<char*>(buf)), top_(start_), end_(start_ + size) {}
    // allocate 同 Bump（指针上移 + 对齐处理），略
    Marker mark() noexcept { return top_; }
    void rollback(Marker m) noexcept { top_ = m; }
};
```

三种策略的取舍：Bump 最简但不支持单释放；Pool 适合固定大小高频；Stack 适合 LIFO 生命周期。它们解决的都是「怎么高效管一块预分配的内存」。

## placement new 与对象构造析构

分配器只给原始内存（字节），对象的构造/析构是你的事——用 placement new 构造、显式调析构：

```cpp
#include <new>
#include <utility>

template<typename T, typename Alloc, typename... Args>
T* construct_with(Alloc& a, Args&&... args)
{
    void* mem = a.allocate(sizeof(T), alignof(T));
    if (!mem) return nullptr;
    return new (mem) T(std::forward<Args>(args)...);
}

template<typename T, typename Alloc>
void destroy_with(Alloc& a, T* obj) noexcept
{
    if (!obj) return;
    obj->~T();
    a.deallocate(static_cast<void*>(obj));
}
```

记住：**分配 ≠ 构造**。`allocate` 给内存，`new (mem) T(...)` 才构造；`obj->~T()` 析构，`deallocate` 归还内存。这套「分配 / 构造 / 析构 / 释放」四步，是手写分配器和标准库 allocator 概念的内核。

## 标准库的答案：std::pmr（C++17）

手写分配器能帮你理解机制，但真要在 STL 容器里用「自己的分配策略」，手写一个完整的 `std::allocator` 兼容类型（一堆 typedef、`rebind`）很繁琐。C++17 给了更好的方案：**std::pmr（polymorphic memory resource）**。

pmr 的核心是 `std::pmr::memory_resource`——一个抽象基类，提供 `allocate` / `deallocate` 接口（你继承它实现自己的策略）。标准库自带几种现成实现：

- `monotonic_buffer_resource`：就是前面的 Bump 分配器，在栈 / 静态 buffer 上线性分配，极快、不释放单个、适合帧分配或一次性任务。
- `synchronized_pool_resource` / `unsynchronized_pool_resource`：固定大小池，适合大量同大小小对象（多线程用 synchronized 版）。
- `null_memory_resource`：只借不还，用于「此后禁止分配」的场景。

然后是 **pmr 容器**：`std::pmr::vector<T>`、`std::pmr::string`、`std::pmr::map` 等，内部用 `polymorphic_allocator`，构造时传一个 `memory_resource*`。换分配策略不用换容器类型（都是 `pmr::vector`），只换 resource——这是 pmr 相对手写 allocator 模板的最大优势：**类型擦除，运行时换策略**。

```cpp
#include <memory_resource>
#include <vector>
#include <cstdint>

std::byte buffer[4096];
std::pmr::monotonic_buffer_resource mbr(buffer, sizeof(buffer));
std::pmr::vector<int> v(&mbr);   // v 的内存来自 buffer，不走全局堆
```

## 跑跑看：pmr::vector 配 monotonic buffer

咱们跑一下，确认 pmr::vector 确实从栈上 buffer 分配：

```cpp
#include <memory_resource>
#include <vector>
#include <iostream>
#include <cstdint>

int main()
{
    // 栈上一块 buffer，用 monotonic_buffer_resource 当分配源
    std::byte buffer[4096];
    std::pmr::monotonic_buffer_resource mbr(buffer, sizeof(buffer));

    // pmr::vector 从这块 buffer 分配，不走全局堆
    std::pmr::vector<int> v(&mbr);
    for (int i = 0; i < 100; ++i) {
        v.push_back(i);
    }
    std::cout << "v.size() = " << v.size() << "\n";
    std::cout << "v.data() address = " << std::hex << v.data() << "\n";
    std::cout << "The range of stack buffer is [" << (void*)buffer << ","
              << (void*)(buffer + sizeof(buffer)) << "]\n";
    std::cout << "vector 的内存来自栈上 buffer，零全局堆分配\n";
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/pmr_test /tmp/pmr_test.cpp && /tmp/pmr_test
```

```text
v.size() = 100
v.data() address = 0x7fff303c500c
The range of stack buffer is [0x7fff303c4e10,0x7fff303c5e10]
vector 的内存来自栈上 buffer，零全局堆分配
```

`v.data()` 的地址 `0x7fff303c500c` 正落在 stack buffer 范围 `[0x7fff303c4e10, 0x7fff303c5e10]` 内——这就是“零全局堆分配”的硬证。栈地址每次运行会变，但 `v.data()` 总落在 buffer 区间里。

> 这段打印 `v.data()` 与栈 buffer 范围、用地址坐实“零堆分配”的证明，由 [@YukunJ](https://github.com/YukunJ) 在 [PR #77](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/pull/77) 补充。

这个 vector 的元素全来自栈上那块 4096 字节 buffer，没有一次全局 `new`。这就是 pmr + monotonic 的典型用法：把一块预分配内存（栈、静态区、或自管的堆块）喂给容器，获得确定的分配行为、零碎片、零全局堆开销。换个 resource（比如 pool）就换策略，容器代码一行不改。

## 临了收几句

自定义分配器的核心是「自己管一块内存的分配 / 释放」，三种经典策略——Bump（快、不释放单）、Pool（固定大小高频）、Stack（LIFO）——各有适用场景。理解它们之后，真要在 STL 里用，首选 C++17 的 `std::pmr`：`memory_resource` 抽象 + 标准实现（monotonic / pool）+ pmr 容器，运行时换策略、类型不爆炸。手写分配器用来理解机制、或做 pmr 不覆盖的特殊需求；常规场景，pmr 就够了。容器主线到此告一段落，下一篇我们转向标准库的迭代器与算法体系。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="自定义分配器：bump arena 与 std::pmr"
  source-path="code/examples/vol3/13_custom_allocators.cpp"
  description="手写线性分配器原型、std::pmr::monotonic_buffer_resource 让 vector 在栈 buffer 分配"
  allow-run
/>

## 参考资源

- [std::pmr（memory_resource） — cppreference](https://en.cppreference.com/w/cpp/memory/resource)
- [monotonic_buffer_resource — cppreference](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource)
- [polymorphic_allocator — cppreference](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator)
