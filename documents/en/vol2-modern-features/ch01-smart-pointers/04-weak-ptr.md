---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master the weak reference mechanism of `weak_ptr` to solve circular reference
  problems with `shared_ptr`.
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 1: shared_ptr 详解'
reading_time_minutes: 14
related:
- 自定义删除器
tags:
- host
- cpp-modern
- intermediate
- weak_ptr
- 智能指针
title: '`weak_ptr` and Circular References: Breaking Ownership Deadlocks'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/04-weak-ptr.md
  source_hash: 3dc5082a69010e403c7380b487f3dc0b09613f04ad686da470a594310514c3a7
  translated_at: '2026-06-16T03:55:30.840018+00:00'
  engine: anthropic
  token_count: 2894
---
# weak_ptr and Circular References: Breaking the Ownership Deadlock

In the previous post, we discussed `shared_ptr`—implementing shared ownership via reference counting. `shared_ptr` seems ideal: as soon as the last owner leaves, the object is automatically destroyed. But in reality, this "automatic destruction" has a fatal enemy: **circular references**. When two objects hold each other's `shared_ptr`, their reference counts never reach zero—two "owners" mistakenly believe the other still holds the key, so neither dares to lock up, resulting in a memory leak.

`weak_ptr` was born to solve this problem. It is an observer pointer that "does not participate in reference counting"—you can use it to check if an object is still alive, and if so, temporarily acquire a `shared_ptr` to access it, but it does not extend the object's lifecycle itself.

## Demonstrating the Circular Reference Problem

Before diving into `weak_ptr`, let's intuitively experience the problem of circular references. A classic example is a doubly linked list: each node holds a `shared_ptr` to the next node, and if it's a doubly linked list, it also holds a `shared_ptr` to the previous node. Consequently, every node is referenced by its neighbors' `shared_ptr`, forming a ring—the reference count never reaches zero.

```cpp
#include <iostream>
#include <memory>

struct Node {
    int value;
    std::shared_ptr<Node> next;
    std::shared_ptr<Node> prev; // Problematic: strong reference to previous node

    Node(int v) : value(v) { std::cout << "Node " << value << " created\n"; }
    ~Node() { std::cout << "Node " << value << " destroyed\n"; }
};

int main() {
    // Create two nodes
    auto node1 = std::make_shared<Node>(1);
    auto node2 = std::make_shared<Node>(2);

    // Link them together
    node1->next = node2; // node2 ref count = 2
    node2->prev = node1; // node1 ref count = 2

    // When main() returns, node1 and node2 go out of scope.
    // ref count drops to 1, but not 0.
    // Memory leak!
}
```

When you run this code, you will find that the destructor output **never appears**—neither `Node 1` nor `Node 2` is printed. The two nodes hold each other's `shared_ptr`, forming a "deadlock ring," so neither is released. This is the memory leak caused by circular references.

This problem is not rare in actual engineering. In the Observer pattern, a Subject holds observers' `shared_ptr`, and observers also hold the Subject's `shared_ptr`; in tree structures, parent nodes hold children's `shared_ptr`, and children also hold parents' `shared_ptr`; in graph structures, any two adjacent nodes might reference each other. Once a ring is formed, the `shared_ptr` reference counting mechanism fails.

## weak_ptr API: lock(), expired(), use_count()

`weak_ptr` is `shared_ptr`'s partner—it points to the object managed by `shared_ptr` but does not increase the strong reference count. You can think of it as a "visitor pass": you can use it to see if the object is still there, but you cannot use the pass to prevent the object from being destroyed.

`weak_ptr` provides three core APIs:

`lock()` is the most important method. It attempts to acquire a `shared_ptr` pointing to the object. If the object still exists (strong reference count > 0), it returns a valid `shared_ptr`; if the object has already been destroyed (strong reference count = 0), it returns an empty `shared_ptr` (i.e., `nullptr`). `lock()` is thread-safe—in a multithreaded environment, multiple threads can call `lock()` simultaneously, and the standard guarantees that the returned `shared_ptr` either points to a valid object or is empty, avoiding the dangling scenario where "a pointer is obtained but the object is already deleted." See the verification code in [cppreference: std::weak_ptr::lock](https://en.cppreference.com/w/cpp/memory/weak_ptr/lock).

`expired()` returns a bool indicating whether the object has been destroyed (i.e., if the strong reference count is 0). However, in practice, we usually recommend using `lock()` directly instead of checking `expired()` first and then calling `lock()`—because in a multithreaded environment, after `expired()` returns `false` and before calling `lock()`, the object might have been destroyed by another thread, leading to a race condition. `lock()` atomically completes the two operations of "checking if the object exists" and "incrementing the reference count," avoiding this issue. See the race condition test in [C++ Smart Pointers: weak_ptr and cyclic reference](https://www.nextptr.com/tutorial/ta1382183122/using-weak_ptr-for-circular-references).

`use_count()` returns the current number of `shared_ptr` instances pointing to the object (i.e., the strong reference count). Like `expired()`, the return value may be stale by the time you use it, so it is generally only used for debugging and logging.

```cpp
#include <iostream>
#include <memory>

int main() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;

    std::cout << "use_count: " << wp.use_count() << "\n"; // 1

    if (auto locked = wp.lock()) { // Try to acquire ownership
        std::cout << "Value: " << *locked << "\n";
    } else {
        std::cout << "Object has been destroyed\n";
    }

    sp.reset(); // Destroy the shared object

    if (wp.expired()) {
        std::cout << "wp is expired (use_count: " << wp.use_count() << ")\n";
    }
}
```

⚠️ `weak_ptr` cannot be dereferenced directly—you cannot write `*wp` or `wp->`. You must first acquire a `shared_ptr` via `lock()`, and then access the object through that `shared_ptr`. This design is intentional: `weak_ptr` is a reference where "it is uncertain whether the object still exists," so direct access is too dangerous. `lock()`'s atomic check guarantees that the `shared_ptr` you acquire either points to a living object or is empty—avoiding the dangling pointer problem where "you get a pointer but the object is already deleted."

## How weak_ptr Breaks the Cycle

Returning to the previous doubly linked list example, we only need to change the `prev` member from `shared_ptr` to `weak_ptr`, and the circular reference is broken:

```cpp
#include <iostream>
#include <memory>

struct Node {
    int value;
    std::shared_ptr<Node> next;
    std::weak_ptr<Node> prev; // Changed to weak_ptr: breaks the cycle

    Node(int v) : value(v) { std::cout << "Node " << value << " created\n"; }
    ~Node() { std::cout << "Node " << value << " destroyed\n"; }
};

int main() {
    auto node1 = std::make_shared<Node>(1);
    auto node2 = std::make_shared<Node>(2);

    node1->next = node2; // node2 ref count = 2
    node2->prev = node1; // node1 ref count = 1 (weak_ptr doesn't increase count)

    // When main() returns:
    // node2 goes out of scope -> node2 ref count 2->1
    // node1 goes out of scope -> node1 ref count 1->0 -> Node 1 destroyed
    // Node 1's destruction releases next (node2) -> node2 ref count 1->0 -> Node 2 destroyed
}
```

Output:

```text
Node 1 created
Node 2 created
Node 1 destroyed
Node 2 destroyed
```

The key lies in the line `node2->prev = node1`—`weak_ptr` does not increase the strong reference count of `node1`. Therefore, when the local variable `node1` goes out of scope, `node1`'s strong reference count drops directly from 1 to 0, triggering destruction. The design philosophy of `weak_ptr` can be summed up in one sentence: **"I know you exist, but I will not stop you from leaving."**

This pattern can be extended to any data structure with "parent-child relationships" or "upstream-downstream relationships": use `shared_ptr` for the strong reference direction (holding ownership), and `weak_ptr` for the weak reference direction (observing only, not holding ownership). As long as there is no ring consisting entirely of strong references in the graph, reference counting works normally.

## weak_ptr in the Observer Pattern

The Observer pattern is one of the most important application scenarios for `weak_ptr`. In this pattern, a Subject maintains a list of observers and notifies all observers when the state changes. If the observer list stores `shared_ptr`, then as long as the Subject is alive, no observer will be destroyed—even if external code no longer needs these observers. Even worse, if observers also hold a `shared_ptr` to the Subject, a circular reference is formed.

The correct approach is: the Subject references observers with `weak_ptr` (does not extend the observers' lifecycle), and observers can choose to reference the Subject with `weak_ptr` or `shared_ptr`.

```cpp
#include <iostream>
#include <memory>
#include <vector>
#include <functional>

// Observer Interface
struct Observer {
    virtual void update(int data) = 0;
    virtual ~Observer() = default;
};

// Concrete Observer
struct ConcreteObserver : Observer {
    std::string name;
    explicit ConcreteObserver(std::string n) : name(std::move(n)) {}
    void update(int data) override {
        std::cout << name << " received: " << data << "\n";
    }
};

// Subject
struct Subject {
    std::vector<std::weak_ptr<Observer>> observers; // Use weak_ptr

    void attach(std::shared_ptr<Observer> obs) {
        observers.push_back(obs);
    }

    void notify(int data) {
        for (auto it = observers.begin(); it != observers.end(); ) {
            if (auto obs = it->lock()) { // Try to acquire strong reference
                obs->update(data);
                ++it;
            } else {
                // Observer has been destroyed, remove from list
                it = observers.erase(it);
            }
        }
    }
};

int main() {
    auto subject = std::make_shared<Subject>();
    auto obs1 = std::make_shared<ConcreteObserver>("Obs1");
    auto obs2 = std::make_shared<ConcreteObserver>("Obs2");

    subject->attach(obs1);
    subject->attach(obs2);

    subject->notify(100); // Both observers receive the notification

    obs1.reset(); // Manually release obs1
    std::cout << "Obs1 released\n";

    subject->notify(200); // Only Obs2 receives the notification; Obs1 is automatically removed
}
```

Output:

```text
Obs1 received: 100
Obs2 received: 100
Obs1 released
Obs2 received: 200
```

This pattern is very common in actual engineering. GUI frameworks (Qt's signal-slot mechanism in certain configurations), game engine event systems, and network library callback mechanisms all face similar problems—the event source should not prevent the destruction of the event consumer. `weak_ptr` provides exactly this "loosely coupled" observation semantics.

## weak_ptr in Cache Implementation

Another classic application scenario for `weak_ptr` is caching. The core semantic of a cache is: entries in the cache can be reclaimed at any time—if no one is using them, delete them to free memory. `weak_ptr` is naturally suited to express this semantics: the cache stores `weak_ptr`, and users temporarily acquire a `shared_ptr` via `lock()` when accessing.

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

class ResourceCache {
public:
    std::shared_ptr<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_.find(key);
        if (it != cache_.end()) {
            // Try to upgrade weak_ptr to shared_ptr
            if (auto sp = it->second.lock()) {
                std::cout << "[Cache Hit] " << key << "\n";
                return sp; // Resource still exists, return it
            } else {
                // Resource has been destroyed, remove stale entry
                cache_.erase(it);
            }
        }

        // Cache miss or expired, load resource
        std::cout << "[Cache Miss] Loading " << key << "...\n";
        auto sp = std::make_shared<std::string>("Resource for " + key);
        cache_[key] = sp; // Store weak_ptr
        return sp;
    }

private:
    std::unordered_map<std::string, std::weak_ptr<std::string>> cache_;
    std::mutex mutex_;
};

int main() {
    ResourceCache cache;

    {
        auto res1 = cache.get("image.png"); // Load
        std::cout << "Using: " << *res1 << "\n";
    } // res1 goes out of scope, strong reference count drops to 0, resource destroyed

    std::cout << "--- After res1 released ---\n";
    auto res2 = cache.get("image.png"); // Reload (expired)
    std::cout << "Using: " << *res2 << "\n";
}
```

Output:

```text
[Cache Miss] Loading image.png...
Using: Resource for image.png
--- After res1 released ---
[Cache Miss] Loading image.png...
Using: Resource for image.png
```

The design of this cache is very natural: the cache itself does not hold a strong reference to the resource (using `weak_ptr`), so when all users release the resource, it is automatically reclaimed. The next time it is accessed, the cache discovers the `weak_ptr` has expired and reloads the resource. No manual "reference count check" or "scheduled cleanup" is needed—the expiration mechanism of `weak_ptr` handles these tasks automatically.

## Common Misuse: Overusing weak_ptr

Although `weak_ptr` is a powerful tool for solving circular references, overusing it can actually increase code complexity and the probability of errors. I have seen some codebases replace almost all pointers with `weak_ptr` for fear of circular references—this is overcorrecting.

First is the performance issue. Every time you access an object via `weak_ptr`, you need to call `lock()`, which involves atomic operations (checking and incrementing the reference count). Frequent `lock()` calls in hot paths can bring measurable performance overhead. According to benchmarks on [Stack Overflow](https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern), accessing via `weak_ptr` is about 10-15 times slower than directly accessing `shared_ptr` (under -O2 optimization, 10 million iterations: direct access ~5ms, `lock()` access ~62ms). Although this absolute time difference might not be significant in practical applications, if called frequently in performance-sensitive code paths, the overhead accumulates.

Second is semantic ambiguity. If your code is full of `weak_ptr` everywhere, it is hard for readers to determine which objects have true ownership relationships. Ownership relationships should be clarified as much as possible during the design phase, rather than using `weak_ptr` to avoid ownership design.

My suggestion is: in most cases, use `unique_ptr` to express exclusive ownership, and use raw pointers or references for non-owning access. Only use `weak_ptr` to break cycles when shared ownership is truly needed and there is a risk of circular references. `weak_ptr` is a precision tool, not a "sprinkle everywhere" panacea.

Another common error is using `weak_ptr` to "observe" objects on the stack or objects managed by `unique_ptr`—this is impossible because `weak_ptr` can only be used in conjunction with `shared_ptr`. If you want to observe the lifecycle of a non-shared object, you need other mechanisms (such as callbacks, manual implementation of the Observer pattern, or changing the object to be managed by `shared_ptr`).

## Summary

`weak_ptr` is `shared_ptr`'s partner, solving the `shared_ptr` circular reference problem through a "weak reference" mechanism that does not participate in strong reference counting. Its three core APIs—`lock()`, `expired()`, and `use_count()`—provide safe "check but don't own" semantics.

In practical applications, `weak_ptr` is mainly used in three scenarios: breaking circular references in data structures (doubly linked lists, trees, graphs), implementing the loosely coupled notification mechanism of the Observer pattern, and building automatically reclaiming cache systems. Mastering these three patterns means mastering the core usage of `weak_ptr`.

But remember, `weak_ptr` is not a panacea. Overusing it makes code harder to understand and maintain. Good design should prioritize clarifying ownership relationships, introducing `weak_ptr` only when necessary.

In the next post, we will discuss custom deleters and intrusive reference counting—delving into how to make smart pointers manage resources that "weren't created with new."

## Reference Resources

- [cppreference: std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [cppreference: std::weak_ptr::lock](https://en.cppreference.com/w/cpp/memory/weak_ptr/lock)
- [Using weak_ptr to implement the Observer pattern](https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern)
- [C++ Smart Pointers: weak_ptr and cyclic reference](https://www.nextptr.com/tutorial/ta1382183122/using-weak_ptr-for-circular-references)
- Herb Sutter, *GotW #89: Smart Pointers*
