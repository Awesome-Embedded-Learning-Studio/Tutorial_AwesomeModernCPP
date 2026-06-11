---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Intrusive container design
difficulty: intermediate
order: 4
platform: stm32f1
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 9
tags:
- cpp-modern
- stm32f1
- intermediate
title: Intrusive Container Design
translation:
  source: documents/vol8-domains/embedded/04-intrusive-containers.md
  source_hash: 24e237b2960b248f9f2dc4c56c20e8e807a888d72f4844b5541a5197c450bd42
  translated_at: '2026-05-26T11:37:35.486345+00:00'
  engine: anthropic
  token_count: 1424
---
# Modern C++ for Embedded Systems Tutorial — Intrusive Container Design

Do you remember what standard containers do to your data? They copy pointers, allocate nodes, maintain extra memory layouts, and at some point silently chew up your cache locality. Intrusive containers are more straightforward: data objects stick their own hands out to act as list nodes — who's paying for extra memory and indirection? Not me.

------

## What Are Intrusive Containers, and Why Are They So Great for Embedded

The key point of intrusive containers: node information (next/prev/...) lives directly inside the user object, rather than allocating a separate node to wrap the object pointer. The advantages are obvious:

- Zero extra allocation — no need to malloc/new a wrapper on every push (extremely important).
- Better cache locality — objects and metadata are together, making traversal faster.
- Smaller memory footprint and determinism — very friendly for memory-constrained or real-time systems.

The disadvantages are equally straightforward:

- Objects are coupled to the container interface (intrusion), requiring source modifications to the object structure.
- If an object needs to be in multiple lists simultaneously, it requires multiple "hook" members or multiple inheritance.
- Improper use can lead to dangling pointers or duplicate insertions, requiring more careful lifecycle management.

Applicable scenarios: task schedulers, free-block lists, driver lists, kernel/RTOS data structures, memory pool free-lists, and more.

------

## Two Common Implementation Strategies

1. **Base class hook (inheritance)**: Objects inherit a hook base class that contains next/prev. Type-safe, and easy to cast.
2. **Member hook**: Objects contain a hook member (more flexible, allowing multiple hook instances), but this requires the `container_of` trick to convert a hook pointer back to an object pointer.

Below, we first implement a clean, ready-to-use "base class hook" doubly linked list (suitable for tutorials and embedded), and then discuss the ideas and caveats of the member hook approach.

------

## Code: A Simple, Type-Safe Intrusive Doubly Linked List (Inheritance-Based)

The goal of the following implementation: small and clear, compatible with C++11, and suitable for embedded compilers.

```cpp
// intrusive_list.h
#pragma once
#include <cassert>
#include <iterator>

// Intrusive list node base — 继承它即可成为链表节点
template<typename T>
struct IntrusiveListNode {
    T* prev = nullptr;
    T* next = nullptr;
};

// Intrusive doubly linked list
template<typename T>
class IntrusiveList {
public:
    IntrusiveList() : head(nullptr), tail(nullptr) {}

    bool empty() const { return head == nullptr; }

    void push_front(T* node) {
        assert(node && node->prev == nullptr && node->next == nullptr && "节点必须处于未链接状态");
        node->next = head;
        if (head) head->prev = node;
        head = node;
        if (!tail) tail = node;
    }

    void push_back(T* node) {
        assert(node && node->prev == nullptr && node->next == nullptr && "节点必须处于未链接状态");
        node->prev = tail;
        if (tail) tail->next = node;
        tail = node;
        if (!head) head = node;
    }

    T* pop_front() {
        if (!head) return nullptr;
        T* n = head;
        head = head->next;
        if (head) head->prev = nullptr;
        else tail = nullptr;
        n->next = n->prev = nullptr;
        return n;
    }

    void erase(T* node) {
        assert(node && "erase null");
        if (node->prev) node->prev->next = node->next;
        else head = node->next;

        if (node->next) node->next->prev = node->prev;
        else tail = node->prev;

        node->prev = node->next = nullptr;
    }

    void clear() {
        T* cur = head;
        while (cur) {
            T* nxt = cur->next;
            cur->prev = cur->next = nullptr;
            cur = nxt;
        }
        head = tail = nullptr;
    }

    // 简单迭代器（只读/可写）
    struct iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using pointer = T*;
        using reference = T&;

        explicit iterator(T* p) : p(p) {}
        reference operator*() const { return *p; }
        pointer operator->() const { return p; }
        iterator& operator++() { p = p->next; return *this; }
        iterator operator++(int) { iterator tmp = *this; ++*this; return tmp; }
        bool operator==(const iterator& o) const { return p == o.p; }
        bool operator!=(const iterator& o) const { return p != o.p; }
    private:
        T* p;
    };

    iterator begin() { return iterator(head); }
    iterator end() { return iterator(nullptr); }

private:
    T* head;
    T* tail;
};

```

**How to use it:**

```cpp
// example.cpp
#include "intrusive_list.h"
#include <iostream>

struct Task : IntrusiveListNode<Task> {
    int id;
    Task(int i): id(i) {}
};

int main() {
    IntrusiveList<Task> runq;
    Task a(1), b(2), c(3);

    runq.push_back(&a);
    runq.push_back(&b);
    runq.push_front(&c); // 链表顺序： c, a, b

    for (auto &t : runq) {
        std::cout << "Task " << t.id << "\n";
    }

    runq.erase(&a);

    if (auto p = runq.pop_front()) {
        std::cout << "pop " << p->id << "\n";
    }
}

```

This code can be compiled directly with any embedded-compatible C++ compiler (as long as it supports basic templates and `nullptr`).

------

## Member Hook: When an Object Needs to Appear in Multiple Lists

The inheritance approach is simple, but if an object needs to belong to multiple lists simultaneously (for example, in both a ready_list and a wait_list), you need multiple hook members or the member hook approach.

The key to the member hook is `container_of` — given a pointer to a hook member, calculating the pointer back to the containing object (a macro commonly used in the Linux kernel).

A simple macro-based implementation (clear and widely used):

```cpp
#include <cstddef> // offsetof
#define CONTAINER_OF(ptr, type, member) \
    ((type*) ( (char*)(ptr) - offsetof(type, member) ))

```

Example structure:

```cpp
struct MyObject {
    IntrusiveListNode<MyObject> ready_hook;   // for ready list
    IntrusiveListNode<MyObject> wait_hook;    // for wait list
    int data;
};

// 操作 ready list 时，将传入 &obj->ready_hook，然后用 CONTAINER_OF 转回 MyObject*

```

The member hook is more flexible, but requires special attention during use: `offsetof` must match the actual member name, and it is strongly recommended to check whether the hook is already linked before insertion (to avoid duplicate insertions).

------

## Design Recommendations and Pitfall Guide

1. **Object lifecycles must be explicit**: Nodes in a list must be removed from all lists before being destroyed. Otherwise, dangling pointers will occur, and the consequence is usually a hard-to-locate crash.
2. **Check state before insertion**: Add a `bool linked` field or assertion to the hook to prevent duplicate insertions. Make good use of `assert` in test code.
3. **Prefer member hooks for multi-hook requirements**: If an object switches between multiple containers frequently, member hooks are more flexible.
4. **Be careful with memory barriers/atomicity in concurrent scenarios**: If you need to manipulate lists in an ISR or on multi-core systems, you must use locks, atomic CAS, or specialized lock-free algorithms (beyond the scope of this article).
5. **Provide an RAII wrapper**: Consider providing a small `IntrusiveListGuard` or `ScopedUnlink` to ensure objects are safely unregistered when exceptions or early returns occur. Embedded code might not have exceptions, but RAII helps write safer cleanup code.
6. **Debug information**: During development, printing node states (id/address/prev/next) can quickly help locate errors.
7. **Don't overuse them**: Intrusive containers are not a silver bullet. If you don't care about per-allocation overhead, or if the object is immutable (third-party library), don't intrude into the object. Standard `std::list`/`vector` are simpler, safer, and easier to maintain.

------

## When to Choose Intrusive Containers

In embedded, kernel, and real-time systems, resources and latency are the top priorities, making intrusive data structures a very natural choice in these scenarios. They are particularly suitable for:

- Systems that require determinism and avoid heap allocation (bootloaders, RTOS kernels).
- High-performance free-lists, task queues, timer wheels, and more.
- Scenarios where you want the smallest possible memory footprint.

If you are working on standard application-layer business logic, or if objects come from third-party libraries (where you cannot modify the structure), the maintenance cost of an intrusive approach may outweigh the benefits.

------

## Conclusion

The idea behind intrusive containers is not complicated: let the data take responsibility for its own "positioning." However, this requires you to have a clearer understanding of the object's responsibilities — who inserts it, who removes it, and when it gets removed. Turn those responsibilities into code, and turn that code into conventions. For embedded systems, this is a very pragmatic engineering philosophy: save a byte of memory, gain a bit more determinism.
