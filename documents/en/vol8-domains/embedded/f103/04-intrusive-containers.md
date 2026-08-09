---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Intrusive Container Design
difficulty: intermediate
order: 4
platform: stm32f1
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 6
tags:
- cpp-modern
- stm32f1
- intermediate
title: Intrusive Container Design
translation:
  source: documents/vol8-domains/embedded/04-intrusive-containers.md
  source_hash: d69eb18a9920d0081f39237f060a02dcc14ef81988f3a72a4fbad5ef83618a04
  translated_at: '2026-06-16T04:12:45.346240+00:00'
  engine: anthropic
  token_count: 1425
---
# Modern C++ for Embedded Tutorials — Intrusive Container Design

Do you remember what standard containers do with your data? They copy pointers, allocate nodes, maintain extra memory layouts, and silently devour your cache locality at some point. Intrusive containers are more straightforward: the data objects stick their own hands out to act as linked list nodes—who needs extra memory and indirection? Not me.

------

## What are Intrusive Containers, and Why are They Great for Embedded Systems?

The key point of intrusive containers is that node information (next/prev/...) is placed directly inside the user object, rather than allocating a separate node wrapper to hold an object pointer. The advantages are obvious:

- **Zero extra allocation** — No need to `malloc`/`new` a wrapper on every `push` (crucial).
- **Better cache locality** — Objects and metadata are together, making traversal faster.
- **Smaller memory footprint and determinism** — Very friendly for memory-constrained or real-time systems.

The disadvantages are also direct:

- **Objects are coupled to the container interface (intrusion)**, requiring source code modification of the object structure.
- **If an object needs to be in multiple lists simultaneously**, it requires multiple "hook" members or multiple inheritance.
- **Improper use can lead to dangling pointers or duplicate insertion issues**, requiring more careful lifecycle management.

**Applicable scenarios:** task schedulers, free-block lists, driver lists, kernel/RTOS data structures, memory pool free-lists, etc.

------

## Two Common Implementation Strategies

1. **Base class hook (inheritance)**: The object inherits from a hook base class containing next/prev. Type-safe, easy to cast.
2. **Member hook**: The object contains a hook member (more flexible, allows multiple hook instances), but requires `offsetof` tricks to convert the hook pointer back to the object pointer.

Below, we first implement a clean, ready-to-use "base class hook" doubly linked list (suitable for tutorials and embedded systems), then discuss the logic and caveats of member hooks.

------

## Code: Simple, Type-Safe Intrusive Doubly Linked List (Inheritance Style)

The goal of this implementation: small and clear, C++11 compatible, suitable for embedded compilers.

```cpp
// intrusive_list.hpp
#pragma once

// A minimal, type-safe intrusive doubly linked list node.
// Objects must inherit from this to be list-compatible.
class IntrusiveNode {
public:
    IntrusiveNode() : prev(nullptr), next(nullptr) {}

    // Remove from current list
    void unlink() {
        if (prev) { prev->next = next; }
        if (next) { next->prev = prev; }
        prev = next = nullptr;
    }

    // Check if linked
    bool is_linked() const { return prev != nullptr || next != nullptr; }

private:
    IntrusiveNode* prev;
    IntrusiveNode* next;

    template <typename T>
    friend class IntrusiveList;
};

// The intrusive list container itself.
// T must inherit from IntrusiveNode.
template <typename T>
class IntrusiveList {
public:
    IntrusiveList() : head_(nullptr), tail_(nullptr) {}

    // Push to the back of the list
    void push_back(T* item) {
        IntrusiveNode* node = static_cast<IntrusiveNode*>(item);
        node->prev = tail_;
        node->next = nullptr;
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node; // List was empty
        }
        tail_ = node;
    }

    // Remove specific item
    void remove(T* item) {
        IntrusiveNode* node = static_cast<IntrusiveNode*>(item);
        if (node->prev) { node->prev->next = node->next; }
        else { head_ = node->next; } // Removing head

        if (node->next) { node->next->prev = node->prev; }
        else { tail_ = node->prev; } // Removing tail

        node->prev = node->next = nullptr;
    }

    // Standard iteration interface
    T* front() { return static_cast<T*>(head_); }
    T* back()  { return static_cast<T*>(tail_); }

    // Simple iterator for range-based for
    class Iterator {
    public:
        Iterator(IntrusiveNode* ptr) : ptr_(ptr) {}
        T& operator*() { return *static_cast<T*>(ptr_); }
        T* operator->() { return static_cast<T*>(ptr_); }
        Iterator& operator++() { ptr_ = ptr_->next; return *this; }
        bool operator!=(const Iterator& other) { return ptr_ != other.ptr_; }
    private:
        IntrusiveNode* ptr_;
    };

    Iterator begin() { return Iterator(head_); }
    Iterator end()   { return Iterator(nullptr); }

    bool empty() const { return head_ == nullptr; }

private:
    IntrusiveNode* head_;
    IntrusiveNode* tail_;
};
```

**How to use:**

```cpp
#include "intrusive_list.hpp"
#include <cstdio>

// Task object managed by the scheduler
struct Task : public IntrusiveNode {
    int id;
    const char* name;

    Task(int i, const char* n) : id(i), name(n) {}
};

int main() {
    IntrusiveList<Task> ready_list;

    Task t1(1, "Idle");
    Task t2(2, "Render");

    ready_list.push_back(&t1);
    ready_list.push_back(&t2);

    // Iterate without extra indirection
    for (auto& task : ready_list) {
        printf("Running Task %d: %s\n", task.id, task.name);
    }

    // Remove specific task
    t1.unlink(); // Or: ready_list.remove(&t1);

    return 0;
}
```

This code compiles directly with embedded-compatible C++ compilers (as long as they support basic templates and `constexpr`).

------

## Member Hook: When an Object Needs to Appear in Multiple Lists

Inheritance is simple, but if an object needs to belong to multiple lists simultaneously (e.g., in both a `ready_list` and a `wait_list`), you need multiple hook members or use the member hook approach.

The key to member hooks is `offsetof` — calculating the pointer to the containing object given a pointer to the hook member (a macro commonly used in the Linux kernel).

A simple macro implementation (clear and commonly used):

```cpp
#include <cstddef>

// Safely get container pointer from member pointer
#define CONTAINER_OF(ptr, type, member) \
    reinterpret_cast<type*>( \
        reinterpret_cast<char*>(ptr) - offsetof(type, member) \
    )

// Generic hook node
struct Hook {
    Hook* prev = nullptr;
    Hook* next = nullptr;
};
```

Example structure:

```cpp
struct Device {
    int id;
    Hook all_devices_hook;  // For global device list
    Hook ready_hook;        // For ready queue
};

// Usage:
// Device* dev = CONTAINER_OF(hook_ptr, Device, all_devices_hook);
```

Member hooks are more flexible, but require special care when using: the member name in `CONTAINER_OF` must match the actual member name; and it is strongly recommended to check if the hook is already linked before insertion to avoid duplicate entries.

------

## Design Advice and Pitfall Prevention

1. **Object lifecycles must be explicit**: Nodes in a list must be removed from all lists before being destroyed. Otherwise, dangling pointers will result, usually leading to hard-to-locate crashes.
2. **Check state before insertion**: Add an `is_linked` field or assertion to the hook to prevent duplicate insertion. Use `assert` frequently in test code.
3. **Prefer member hooks for multiple hook requirements**: If an object switches between containers frequently, member hooks are more flexible.
4. **Be careful with memory barriers/atomicity in concurrent scenarios**: If operating on lists in an ISR or multi-core environment, you must use locks, atomic CAS, or specialized lock-free algorithms (beyond the scope of this article).
5. **Provide RAII wrappers**: Consider providing a small `scope_guard` or `IntrusiveListOwner` to ensure objects can be safely unlinked on exceptions or early returns. Embedded code might not use exceptions, but RAII helps write safer release code.
6. **Debugging info**: During development, printing node status (id/address/prev/next) can quickly pinpoint errors.
7. **Don't abuse**: Intrusive containers are not a universal tool. If you don't care about per-allocation overhead or the object is immutable (third-party library), don't intrude on the object; standard `std::list`/`std::vector` are simpler, safer, and easier to maintain.

------

## When to Choose Intrusive Containers

In embedded/kernel/real-time systems, resources and latency are top priorities. Intrusive data structures are a very natural choice in these scenarios. They are particularly suitable for:

- Systems requiring determinism and avoiding heap allocation (bootloaders, RTOS kernels).
- High-performance free-lists, task queues, timer wheels, etc.
- Scenarios requiring minimal memory footprint.

If you are working on general application-layer business logic, or objects come from third-party libraries (where structures cannot be modified), the maintenance cost of intrusive solutions may outweigh the benefits.

------

## Conclusion

The idea behind intrusive containers isn't complicated: let the data take responsibility for its own "position". But this requires you to be clearer about the object's responsibilities—who inserts it, who deletes it, and when. Write these responsibilities into code, and then turn that code into standards. For embedded systems, this is a very "pragmatic" engineering philosophy: save a bit of memory, gain a bit of determinism.
