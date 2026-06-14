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
reading_time_minutes: 9
tags:
- cpp-modern
- stm32f1
- intermediate
title: Intrusive Container Design
translation:
  source: documents/vol8-domains/embedded/04-intrusive-containers.md
  source_hash: be6a1adfb9f0ecf819e11505b29abc841596da95c16afb75d38001765af4d2f5
  translated_at: '2026-06-14T00:21:18.768776+00:00'
  engine: anthropic
  token_count: 1425
---
# Modern C++ for Embedded: Intrusive Container Design

Do you remember what standard containers do with your data? They copy pointers, allocate nodes, maintain extra memory layouts, and silently devour your cache locality at some point. Intrusive containers are more straightforward: the data objects stick their own hands out to act as list nodes—who needs extra memory and indirection? Not me.

------

## What are Intrusive Containers and Why are They Great for Embedded Systems

The key point of intrusive containers is that node information (next/prev/...) is placed directly inside the user object, rather than allocating a separate node wrapper to hold the object pointer. The advantages are obvious:

- **Zero extra allocation** — No need to `malloc`/`new` a wrapper on every `push` (crucial).
- **Better cache locality** — Objects and metadata are together, making traversal faster.
- **Smaller memory footprint and determinism** — Very friendly for memory-constrained or real-time systems.

The disadvantages are equally direct:

- **Objects are coupled to the container interface** (intrusive), requiring modifications to the object structure.
- **If an object needs to be in multiple lists simultaneously**, it requires multiple "hook" members or multiple inheritance.
- **Misuse can lead to dangling pointers or duplicate insertion issues**, requiring more careful lifecycle management.

**Applicable scenarios:** task schedulers, free-lists for idle blocks, driver lists, kernel/RTOS data structures, memory pool free-lists, etc.

------

## Two Common Implementation Strategies

1. **Base class hook (inheritance)**: The object inherits from a hook base class that contains `next`/`prev`. It is type-safe and easy to cast.
2. **Member hook**: The object contains a hook member (more flexible, allows multiple hook instances), but requires the `offsetof` technique to convert the hook pointer back to the object pointer.

Below, we will first implement a clean, ready-to-use "base class hook" doubly linked list (suitable for tutorials and embedded systems), and then discuss the logic and caveats of member hooks.

------

## Code: Simple, Type-Safe Intrusive Doubly Linked List (Inheritance-based)

The goal of this implementation: small and clear, C++11 compatible, suitable for embedded compilers.

```cpp
// intrusive_list.hpp
#pragma once

// A minimal, type-safe intrusive doubly linked list node.
// T must inherit from IntrusiveNode<T>.
template <typename T>
class IntrusiveNode {
public:
    IntrusiveNode() : prev(nullptr), next(nullptr) {}

    // Check if the node is currently part of a list
    bool is_linked() const {
        return next != nullptr || prev != nullptr;
    }

    // Remove this node from the list.
    // Safe to call only if the node is actually linked.
    void unlink() {
        if (next) {
            next->prev = prev;
        }
        if (prev) {
            prev->next = next;
        }
        next = prev = nullptr;
    }

private:
    T* prev;
    T* next;

    friend class IntrusiveList<T>;
};

// The intrusive list container itself.
// Does NOT manage memory ownership; it only manages pointers.
template <typename T>
class IntrusiveList {
public:
    IntrusiveList() : head(nullptr), tail(nullptr) {}

    bool empty() const { return head == nullptr; }

    // Push to the front of the list
    void push_front(T* item) {
        if (!item) return;

        item->IntrusiveNode<T>::next = head;
        item->IntrusiveNode<T>::prev = nullptr;

        if (head) {
            head->IntrusiveNode<T>::prev = item;
        } else {
            tail = item; // List was empty
        }
        head = item;
    }

    // Push to the back of the list
    void push_back(T* item) {
        if (!item) return;

        item->IntrusiveNode<T>::prev = tail;
        item->IntrusiveNode<T>::next = nullptr;

        if (tail) {
            tail->IntrusiveNode<T>::next = item;
        } else {
            head = item; // List was empty
        }
        tail = item;
    }

    // Standard iteration support
    T* front() { return head; }
    T* back() { return tail; }
    const T* front() const { return head; }
    const T* back() const { return tail; }

    // Iterator implementation for range-based for loops
    class iterator {
    public:
        iterator(T* ptr) : current(ptr) {}

        T& operator*() { return *current; }
        T* operator->() { return current; }

        // Prefix increment
        iterator& operator++() {
            if (current) current = current->IntrusiveNode<T>::next;
            return *this;
        }

        // Postfix increment
        iterator operator++(int) {
            iterator temp = *this;
            ++(*this);
            return temp;
        }

        bool operator!=(const iterator& other) const {
            return current != other.current;
        }

    private:
        T* current;
    };

    iterator begin() { return iterator(head); }
    iterator end() { return iterator(nullptr); }

private:
    T* head;
    T* tail;
};
```

**How to use:**

```cpp
#include "intrusive_list.hpp"
#include <cstdio>

// Example 1: Task Control Block
class Task : public IntrusiveNode<Task> {
public:
    const char* name;
    int priority;

    Task(const char* n, int p) : name(n), priority(p) {}

    void run() {
        printf("Running task: %s (Priority %d)\n", name, priority);
    }
};

int main() {
    Task task1("Idle", 0);
    Task task2("Logger", 10);
    Task task3("Network", 5);

    IntrusiveList<Task> ready_queue;

    ready_queue.push_back(&task1);
    ready_queue.push_back(&task2);
    ready_queue.push_front(&task3); // Network goes to front

    printf("--- Task Queue ---\n");
    for (auto& t : ready_queue) {
        t.run();
    }

    // Remove a specific task
    task2.unlink();

    printf("\n--- After removing Logger ---\n");
    for (auto& t : ready_queue) {
        printf("Task: %s\n", t.name);
    }

    return 0;
}
```

This code compiles directly with embedded-compatible C++ compilers (as long as they support basic templates and `constexpr`).

------

## Member Hook: When Objects Need to Appear in Multiple Lists

The inheritance approach is simple, but if an object needs to belong to multiple lists simultaneously (e.g., in both a `ready_list` and a `wait_list`), you need multiple hook members or use the member hook approach.

The key to member hooks is `offsetof` — given a pointer to a hook member, calculate the pointer to the containing object (a macro commonly used in the Linux kernel).

A simple macro implementation (clear and commonly used):

```cpp
#include <cstddef>

// A generic hook node for member lists
struct LinkNode {
    LinkNode* prev = nullptr;
    LinkNode* next = nullptr;
};

// Helper macro: container_of
// Given ptr (address of member), type (container type), and member (member name)
#define GET_CONTAINER(ptr, type, member) \
    reinterpret_cast<type*>(reinterpret_cast<char*>(ptr) - offsetof(type, member))
```

Example structure:

```cpp
class Device {
public:
    int id;
    LinkNode dev_list_hook;   // Hook for global device list
    LinkNode ready_hook;      // Hook for ready queue
    LinkNode wait_hook;       // Hook for wait queue

    Device(int i) : id(i) {}
};

// Usage:
// Device* d = GET_CONTAINER(node_ptr, Device, dev_list_hook);
```

Member hooks are more flexible, but require special care when using: the `member` name in `GET_CONTAINER` must match the actual member name; and it is strongly recommended to check if the hook is already linked before insertion to avoid duplicate insertion.

------

## Design Advice and Pitfall Prevention

1. **Object lifecycles must be explicit**: Nodes in a list must be removed from all lists before being destroyed. Otherwise, dangling pointers will appear, often leading to hard-to-locate crashes.
2. **Check state before insertion**: Add an `is_linked` field or assertion to the hook to prevent duplicate insertion. Use `assert` frequently in test code.
3. **Prefer member hooks for multiple hook requirements**: If an object switches between containers frequently, member hooks are more flexible.
4. **Be careful with memory barriers/atomicity in concurrent scenarios**: If you operate on lists in an ISR or multi-core environment, you must use locks, atomic CAS, or specialized lock-free algorithms (beyond the scope of this article).
5. **Provide RAII wrappers**: Consider providing a small `ScopeGuard` or `IntrusiveListAutoUnlink` to ensure objects are safely unlinked on exceptions or early returns. Embedded code might not use exceptions, but RAII helps write safer release code.
6. **Debug information**: During development, printing node status (id/address/prev/next) can quickly pinpoint errors.
7. **Don't abuse them**: Intrusive containers are not a silver bullet. If you don't care about per-allocation overhead or the object is immutable (third-party library), don't intrude on the object; standard `std::list`/`std::vector` are simpler, safer, and easier to maintain.

------

## When to Choose Intrusive Containers

In embedded / kernel / real-time systems, resources and latency are top priorities. Intrusive data structures are a very natural choice in these scenarios. They are particularly suitable for:

- Systems requiring determinism and avoiding heap allocation (bootloaders, RTOS kernels).
- High-performance free-lists, task queues, timer wheels, etc.
- Scenarios where minimal memory footprint is desired.

If you are working on general application-layer business logic, or if objects come from third-party libraries (where structure modification is impossible), the maintenance cost of intrusive solutions may outweigh the benefits.

------

## Conclusion

The idea behind intrusive containers isn't complex: let the data take responsibility for its own "position". However, this requires you to be clearer about the object's responsibilities—who inserts it, who deletes it, and when it is deleted. Codify these responsibilities into code, and then turn that code into standards. For embedded systems, this is a very "pragmatic" engineering philosophy: save a bit of memory, gain a bit of determinism.
