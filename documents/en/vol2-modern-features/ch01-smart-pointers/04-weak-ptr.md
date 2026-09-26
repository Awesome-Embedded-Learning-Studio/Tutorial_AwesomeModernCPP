---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master the weak reference mechanism of weak_ptr and solve shared_ptr's
  circular reference problem
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
reading_time_minutes: 14
related:
- 'Custom Deleters and Intrusive Reference Counting'
tags:
- host
- cpp-modern
- intermediate
- weak_ptr
- 智能指针
title: 'weak_ptr and Circular References: Breaking the Ownership Deadlock'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/04-weak-ptr.md
  source_hash: 3dd6ad5cfbdb0fd5a06d64de25b02d965c817f7c7ae5c4f10b70a06a18c30a8a
  translated_at: '2026-09-25T14:42:14+00:00'
  engine: anthropic
  token_count: 6900
---
# weak_ptr and Circular References: Breaking the Ownership Deadlock

Last time we talked about `shared_ptr` — shared ownership through reference counting. `shared_ptr` looks lovely: as soon as the last owner leaves, the object destroys itself. But in reality, this "automatic destruction" has one mortal enemy: **circular references**. When two objects each hold a `shared_ptr` to the other, their reference counts never drop back to zero — the two "butlers" each assume the other still holds the key, and neither dares to lock the door. The result is a memory leak.

`std::weak_ptr` was born to solve exactly this problem. It is an observer pointer that "does not participate in reference counting" — you can use it to see whether the object is still alive, and if so, temporarily obtain a `shared_ptr` to access it, but it never extends the object's lifetime by itself.

## Demonstrating the Circular Reference Problem

Before diving into `weak_ptr`, let's first get an intuitive feel for the circular reference problem. The classic example is a linked list: each node holds a `shared_ptr` to the next node, and in a doubly linked list, also to the previous node. Now every node is referenced by its neighbors' `shared_ptr`s, forming a ring — the reference counts never reach zero.

```cpp
#include <memory>
#include <iostream>
#include <string>

struct Node {
    std::string name;
    std::shared_ptr<Node> next;
    std::shared_ptr<Node> prev;  // the shared_ptr here causes the circular reference

    explicit Node(const std::string& n) : name(n) {
        std::cout << "Node(" << name << ") 构造\n";
    }
    ~Node() {
        std::cout << "~Node(" << name << ") 析构\n";
    }
};

void circular_reference_bug() {
    auto a = std::make_shared<Node>("A");
    auto b = std::make_shared<Node>("B");

    a->next = b;  // A → B (B's reference count: 1 → 2)
    b->prev = a;  // B → A (A's reference count: 1 → 2)

    std::cout << "准备离开函数...\n";
    // When the function ends:
    // a leaves scope, A's reference count: 2 → 1 (B->prev still holds A)
    // b leaves scope, B's reference count: 2 → 1 (A->next still holds B)
    // Result: A and B both end with a reference count of 1 that never reaches zero — a memory leak!
}
```

Run this code and you will find that the destructor output of `~Node()` **never appears** — neither `Node("A") 析构` nor `~Node("B") 析构` is ever printed. The two nodes hold `shared_ptr`s to each other, forming a "deadlock ring" where neither ever gets released. That is the memory leak caused by circular references.

This deadlock ring has been animated: you can play it, pause it, or single-step through it to watch how the two `shared_ptr`s interlock and how a `weak_ptr` breaks the ring open:

<Anim id="weak-ptr-cycle" />

This kind of problem is far from rare in real projects. In the observer pattern, the subject holds `shared_ptr`s to its observers while the observers hold `shared_ptr`s back to the subject; in tree structures, the parent holds `shared_ptr`s to the children while the children hold `shared_ptr`s to the parent; in graphs, any two adjacent nodes may reference each other. Once a ring forms, `shared_ptr`'s reference counting mechanism stops working.

## The weak_ptr API: lock(), expired(), use_count()

`weak_ptr` is `shared_ptr`'s partner — it points to the object managed by a `shared_ptr` without incrementing the strong reference count. Think of it as a "visitor pass": you can use it to go see whether the object is still there, but you cannot use it to stop the object from being destroyed.

`weak_ptr` provides three core APIs:

`lock()` is the most important method. It attempts to obtain a `shared_ptr` pointing to the object. If the object still exists (strong reference count > 0), it returns a valid `shared_ptr`; if the object has already been destroyed (strong reference count = 0), it returns an empty `shared_ptr` (that is, `nullptr`). `lock()` is thread-safe — in a multi-threaded environment, multiple threads can call `lock()` simultaneously, and the standard guarantees that the returned `shared_ptr` either points to a valid object or is empty. There is no dangling scenario of "got the pointer, but the object was already deleted".

`expired()` returns a bool indicating whether the object has already been destroyed (that is, whether the strong reference count is 0). In practice, though, it is generally recommended to just call `lock()` directly rather than checking `expired()` first and calling `lock()` afterward — in a multi-threaded environment, between the moment `expired()` returns `false` and the moment you call `lock()`, another thread may already have destroyed the object, which is a race condition. `lock()` performs "check whether the object exists" and "increment the reference count" as a single operation, avoiding this problem.

`use_count()` returns the number of `shared_ptr`s currently pointing at the object (that is, the strong reference count). Like `expired()`, its return value may already be stale by the time you use it, so it is generally only good for debugging and logging.

```cpp
#include <memory>
#include <iostream>

void weak_ptr_api_demo() {
    std::weak_ptr<int> weak;

    {
        auto shared = std::make_shared<int>(42);
        weak = shared;  // weak does not increment the reference count

        std::cout << "use_count: " << weak.use_count() << "\n";  // 1
        std::cout << "expired: " << weak.expired() << "\n";      // 0 (false)

        // Obtain a shared_ptr via lock()
        if (auto locked = weak.lock()) {
            std::cout << "value: " << *locked << "\n";  // 42
            std::cout << "use_count after lock: "
                      << weak.use_count() << "\n";  // 2
        }
        // locked leaves scope, the reference count drops back to 1
    }

    // shared has been destroyed
    std::cout << "expired after scope: " << weak.expired() << "\n";  // 1 (true)

    // lock() returns an empty shared_ptr
    auto locked = weak.lock();
    std::cout << "locked is nullptr: " << (locked == nullptr) << "\n";  // 1 (true)
}
```

A `weak_ptr` cannot be dereferenced directly — you cannot write `*weak` or `weak->member`. You must first obtain a `shared_ptr` through `lock()`, and then access the object through that `shared_ptr`. This design is deliberate: a `weak_ptr` is a reference that "is not sure whether the object still exists", and accessing it directly would be far too dangerous. `lock()`'s atomic check guarantees that the `shared_ptr` you obtain either points to a living object or is empty — the dangling-pointer scenario of "got the pointer, but the object was already deleted" cannot happen.

## How weak_ptr Breaks the Cycle

Back to the doubly linked list example: we only need to change `prev` from `shared_ptr` to `weak_ptr`, and the circular reference is broken:

```cpp
struct NodeFixed {
    std::string name;
    std::shared_ptr<NodeFixed> next;
    std::weak_ptr<NodeFixed> prev;  // changed to weak_ptr

    explicit NodeFixed(const std::string& n) : name(n) {
        std::cout << "Node(" << name << ") 构造\n";
    }
    ~NodeFixed() {
        std::cout << "~Node(" << name << ") 析构\n";
    }
};

void fixed_circular_reference() {
    auto a = std::make_shared<NodeFixed>("A");
    auto b = std::make_shared<NodeFixed>("B");

    a->next = b;  // A → B (B's strong reference count: 1 → 2)
    b->prev = a;  // B ⇢ A (a weak reference; A's strong reference count stays at 1)

    std::cout << "准备离开函数...\n";
    // When the function ends:
    // a leaves scope, A's strong reference count: 1 → 0, A is destroyed
    //   Destroying A destroys A->next, so B's strong reference count: 2 → 1
    // b leaves scope, B's strong reference count: 1 → 0, B is destroyed
    // Every node is correctly released!
}
```

Output:

```text
Node(A) 构造
Node(B) 构造
准备离开函数...
~Node(A) 析构
~Node(B) 析构
```

The key is the line `b->prev = a` — the `weak_ptr` does not increment `a`'s strong reference count. So when the local variable `a` leaves its scope, `a`'s strong reference count drops from 1 straight to 0, triggering the destructor. The design philosophy of `weak_ptr` can be summed up in one sentence: **"I know you exist, but I will not stop you from leaving"**.

This pattern generalizes to any data structure with a "parent-child" or "upstream-downstream" relationship: use `shared_ptr` for the strong-reference direction (holding ownership), and `weak_ptr` for the weak-reference direction (observing only, not holding ownership). As long as the graph contains no ring made entirely of strong references, reference counting works just fine.

## weak_ptr in the Observer Pattern

The observer pattern is one of the most important use cases for `weak_ptr`. In this pattern, the subject maintains a list of observers and notifies them all when the state changes. If that observer list stores `shared_ptr<Observer>`, then as long as the subject is alive, none of the observers can be destroyed — even when the outside world no longer needs them. Worse still, if the observers also hold `shared_ptr`s back to the subject, a circular reference forms.

The correct approach: the subject references its observers with `weak_ptr`s (without extending their lifetimes), while each observer may reference the subject with either a `shared_ptr` or a `weak_ptr`.

```cpp
#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>

class EventListener {
public:
    virtual ~EventListener() = default;
    virtual void on_event(const std::string& msg) = 0;
};

class ConsoleListener : public EventListener {
public:
    explicit ConsoleListener(const std::string& name) : name_(name) {
        std::cout << "Listener(" << name_ << ") 创建\n";
    }
    ~ConsoleListener() override {
        std::cout << "~Listener(" << name_ << ") 销毁\n";
    }
    void on_event(const std::string& msg) override {
        std::cout << "[" << name_ << "] 收到事件: " << msg << "\n";
    }
private:
    std::string name_;
};

class EventBus {
public:
    void subscribe(std::shared_ptr<EventListener> listener) {
        listeners_.push_back(listener);  // stored as a weak_ptr
    }

    void publish(const std::string& msg) {
        // Clean up destroyed observers
        listeners_.erase(
            std::remove_if(listeners_.begin(), listeners_.end(),
                [](const std::weak_ptr<EventListener>& w) {
                    return w.expired();
                }),
            listeners_.end()
        );

        // Notify all live observers
        for (const auto& weak : listeners_) {
            if (auto listener = weak.lock()) {
                listener->on_event(msg);
            }
        }
    }

private:
    std::vector<std::weak_ptr<EventListener>> listeners_;
};

void observer_demo() {
    EventBus bus;

    {
        auto l1 = std::make_shared<ConsoleListener>("L1");
        auto l2 = std::make_shared<ConsoleListener>("L2");

        bus.subscribe(l1);
        bus.subscribe(l2);

        bus.publish("第一条消息");
        // Both L1 and L2 receive it

        std::cout << "--- L2 离开作用域 ---\n";
    }
    // L1 and L2 have both left scope
    // but the EventBus holds weak_ptrs, so it cannot keep them alive

    bus.publish("第二条消息");
    // No observer receives it — they are already destroyed
}
```

Output:

```text
Listener(L1) 创建
Listener(L2) 创建
[L1] 收到事件: 第一条消息
[L2] 收到事件: 第一条消息
--- L2 离开作用域 ---
~Listener(L2) 销毁
~Listener(L1) 销毁
```

This pattern is extremely common in real projects. GUI frameworks (Qt's signal-slot mechanism under certain configurations), game engines' event systems, and networking libraries' callback mechanisms all face a similar issue — an event source should not prevent the destruction of its event consumers. `weak_ptr` provides exactly this kind of "loosely coupled" observation semantics.

## weak_ptr in a Cache Implementation

Another classic use case for `weak_ptr` is caching. The core semantic of a cache: entries may be reclaimed at any time — if nobody is using one, drop it to free memory. `weak_ptr` is a natural fit for expressing this: the cache stores `weak_ptr`s, and users obtain a temporary `shared_ptr` through `lock()` when they need an entry.

```cpp
#include <memory>
#include <unordered_map>
#include <string>
#include <iostream>

class ExpensiveResource {
public:
    explicit ExpensiveResource(const std::string& key)
        : key_(key)
    {
        std::cout << "加载资源: " << key_ << "\n";
    }
    ~ExpensiveResource() {
        std::cout << "释放资源: " << key_ << "\n";
    }
    const std::string& key() const { return key_; }
private:
    std::string key_;
};

class ResourceCache {
public:
    std::shared_ptr<ExpensiveResource> get(const std::string& key) {
        // First try to fetch it from the cache
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            if (auto cached = it->second.lock()) {
                std::cout << "缓存命中: " << key << "\n";
                return cached;
            }
            // The weak_ptr has expired; remove it from the cache
            cache_.erase(it);
        }

        // Cache miss; load the resource
        auto resource = std::make_shared<ExpensiveResource>(key);
        cache_[key] = resource;  // stored as a weak_ptr
        return resource;
    }

    void cleanup() {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.expired()) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t size() const {
        size_t count = 0;
        for (const auto& [k, v] : cache_) {
            if (!v.expired()) ++count;
        }
        return count;
    }

private:
    std::unordered_map<std::string, std::weak_ptr<ExpensiveResource>> cache_;
};

void cache_demo() {
    ResourceCache cache;

    {
        auto r1 = cache.get("texture/player.png");  // cache miss, loads it
        auto r2 = cache.get("texture/player.png");  // cache hit

        std::cout << "缓存中的条目数: " << cache.size() << "\n";  // 1

        // r1 and r2 leave scope
    }

    std::cout << "资源已无人使用\n";
    std::cout << "缓存中的条目数: " << cache.size() << "\n";  // 0 (the weak_ptr has expired)

    auto r3 = cache.get("texture/player.png");  // needs a reload
}
```

Output:

```text
加载资源: texture/player.png
缓存命中: texture/player.png
缓存中的条目数: 1
释放资源: texture/player.png
资源已无人使用
缓存中的条目数: 0
加载资源: texture/player.png
```

This cache design is very natural: the cache itself holds no strong reference to the resource (it uses `weak_ptr`), so once all users have released the resource, it is reclaimed automatically. On the next access, the cache finds the `weak_ptr` expired and loads the resource again. No manual "reference count checks" or "scheduled cleanup" needed — `weak_ptr`'s expiration mechanism handles all of that automatically.

## A Common Misuse: Overusing weak_ptr

Although `weak_ptr` is a powerful tool for breaking circular references, overusing it actually increases code complexity and the chance of mistakes. We have seen codebases that replace nearly every pointer with a `weak_ptr` out of fear of circular references — that is overcorrection.

First, performance. Every access to an object through a `weak_ptr` requires calling `lock()`, which involves an atomic operation (checking the reference count and incrementing it). Frequent `lock()` calls on a hot path bring measurable overhead. In measured tests, `weak_ptr::lock()` is about 30x slower than accessing a `shared_ptr` directly (`lock()` has to grab the reference count with an atomic operation; over 10 million iterations, direct access takes about 2 ms versus about 68 ms for `lock()`, GCC 16.1.1 -O2). While the absolute difference may not be large in a real application, if the call is made frequently on a performance-sensitive code path, the overhead accumulates.

Second, muddied semantics. If `weak_ptr`s are scattered everywhere in your code, readers can hardly tell which objects stand in a genuine ownership relationship. Ownership relationships should be worked out as early as the design stage, not dodged by using `weak_ptr` to avoid ownership design.

Our recommendation: in most cases, express exclusive ownership with `unique_ptr`, and non-owning access with raw pointers or references. Only when shared ownership is genuinely needed and a circular reference risk exists should you use `weak_ptr` to break the cycle. `weak_ptr` is a precision tool, not a cure-all to scatter everywhere.

Another common mistake is trying to use a `weak_ptr` to "observe" an object on the stack or an object managed by a `unique_ptr` — that is impossible, because `weak_ptr` only works together with `shared_ptr`. If you want to observe the lifetime of a non-shared object, you need some other mechanism (for example, callback functions, a hand-rolled observer pattern, or switching the object to `shared_ptr` management).

The next article covers custom deleters and intrusive reference counting — how to make smart pointers manage resources that did not come from `new`.

## References

- [cppreference: std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [cppreference: std::weak_ptr::lock](https://en.cppreference.com/w/cpp/memory/weak_ptr/lock)
- [Using weak_ptr to implement the Observer pattern](https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern)
- [C++ Smart Pointers: weak_ptr and cyclic reference](https://www.nextptr.com/tutorial/ta1382183122/using-weak_ptr-for-circular-references)
- Herb Sutter, *GotW #89: Smart Pointers*
