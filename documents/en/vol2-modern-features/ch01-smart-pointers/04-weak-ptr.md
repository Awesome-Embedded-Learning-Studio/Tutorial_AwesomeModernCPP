---
title: 'weak_ptr and circular references: breaking the ownership deadlock'
description: Master the weak reference mechanism of weak_ptr to solve the circular
  reference problem of shared_ptr
chapter: 1
order: 4
tags:
- host
- cpp-modern
- intermediate
- weak_ptr
- 智能指针
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 15
prerequisites:
- 'Chapter 1: shared_ptr 详解'
related:
- 自定义删除器
---
# weak pointer and Circular References: Breaking the Ownership Deadlock

In the previous article, we discussed `shared_ptr`—shared ownership implemented via reference counting. `shared_ptr` seems wonderful: as long as the last holder goes out of scope, the object is automatically destroyed. But in reality, this "automatic destruction" has a fatal enemy: **circular references**. When two objects hold each other's `shared_ptr`, their reference counts never reach zero—two "managers" each assume the other still holds the key, and neither dares to close the door. The result is a memory leak.

`std::weak_ptr` was born to solve this problem. It is an observer pointer that "does not participate in reference counting"—you can use it to check whether an object is still alive, and if it is, temporarily obtain a `shared_ptr` to access it. However, it does not extend the object's lifetime itself.

## Demonstrating the Circular Reference Problem

Before diving into `weak_ptr`, let's intuitively experience the circular reference problem. The classic example is a doubly linked list: each node holds a `shared_ptr` to the next node, and in a doubly linked list, it also holds a `shared_ptr` to the previous node. This way, each node is referenced by the `shared_ptr` of its adjacent nodes, forming a ring—the reference count never reaches zero.

```cpp
#include <memory>
#include <iostream>
#include <string>

struct Node {
    std::string name;
    std::shared_ptr<Node> next;
    std::shared_ptr<Node> prev;  // 这里的 shared_ptr 导致循环引用

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

    a->next = b;  // A → B（B 的引用计数: 1 → 2）
    b->prev = a;  // B → A（A 的引用计数: 1 → 2）

    std::cout << "准备离开函数...\n";
    // 函数结束时：
    // a 离开作用域，A 的引用计数: 2 → 1（B->prev 仍然持有 A）
    // b 离开作用域，B 的引用计数: 2 → 1（A->next 仍然持有 B）
    // 结果：A 和 B 的引用计数都是 1，永远不会归零——内存泄漏！
}
```

If you run this code, you will find that the destructor output for `~Node()` **never appears**—neither `Node("A") 析构` nor `~Node("B") 析构` gets printed. The two nodes hold each other's `shared_ptr`, forming a "deadlock ring," and neither gets released. This is a memory leak caused by circular references.

This problem is not rare in real-world engineering. In the Observer pattern, the Subject holds the observers' `shared_ptr`, and the observers also hold the Subject's `shared_ptr`; in tree structures, parent nodes hold their children's `shared_ptr`, and child nodes also hold their parent's `shared_ptr`; in graph structures, any two adjacent nodes might reference each other. As long as a ring is formed, the `shared_ptr` reference counting mechanism breaks down.

## weak pointer API: lock(), expired(), use_count()

`weak_ptr` is the partner of `shared_ptr`—it points to the object managed by `shared_ptr` but does not increase the strong reference count. You can think of it as a "visitor pass": you can use the pass to check if the object is still there, but you cannot use the pass to prevent the object from being destroyed.

`weak_ptr` provides three core APIs:

`lock()` is the most important method. It attempts to obtain a `shared_ptr` pointing to the object. If the object still exists (strong reference count > 0), it returns a valid `shared_ptr`; if the object has already been destroyed (strong reference count = 0), it returns an empty `shared_ptr` (i.e., `nullptr`). `lock()` is thread-safe—in a multithreaded environment, multiple threads can call `lock()` simultaneously, and the standard guarantees that the returned `shared_ptr` either points to a valid object or is empty, avoiding the dangling scenario of "obtaining a pointer but the object has already been deleted." See `test_weak_ptr_atomicity.cpp` for verification code.

`expired()` returns a bool value indicating whether the object has already been destroyed (i.e., whether the strong reference count is 0). However, in practice, we generally recommend using `lock()` directly rather than checking `expired()` first and then calling `lock()`—because in a multithreaded environment, between the moment `expired()` returns `false` and the call to `lock()`, the object might have already been destroyed by another thread, leading to a race condition. `lock()` atomically completes both the "check if the object exists" and "increment the reference count" operations, avoiding this problem. See the race condition test in `test_weak_ptr_atomicity.cpp` for verification code.

`use_count()` returns the current number of `shared_ptr` pointing to the object (i.e., the strong reference count). Like `expired()`, the return value might already be stale by the time you use it, so it is generally only used for debugging and logging.

```cpp
#include <memory>
#include <iostream>

void weak_ptr_api_demo() {
    std::weak_ptr<int> weak;

    {
        auto shared = std::make_shared<int>(42);
        weak = shared;  // weak 不增加引用计数

        std::cout << "use_count: " << weak.use_count() << "\n";  // 1
        std::cout << "expired: " << weak.expired() << "\n";      // 0 (false)

        // 通过 lock() 获取 shared_ptr
        if (auto locked = weak.lock()) {
            std::cout << "value: " << *locked << "\n";  // 42
            std::cout << "use_count after lock: "
                      << weak.use_count() << "\n";  // 2
        }
        // locked 离开作用域，引用计数回到 1
    }

    // shared 已经被销毁
    std::cout << "expired after scope: " << weak.expired() << "\n";  // 1 (true)

    // lock() 返回空的 shared_ptr
    auto locked = weak.lock();
    std::cout << "locked is nullptr: " << (locked == nullptr) << "\n";  // 1 (true)
}
```

⚠️ `weak_ptr` cannot be dereferenced directly—you cannot write `*weak` or `weak->member`. You must first obtain a `shared_ptr` via `lock()`, and then access the object through `shared_ptr`. This design is intentional: `weak_ptr` is a reference that "does not guarantee the object still exists," so direct access is too dangerous. The atomic check in `lock()` guarantees that the `shared_ptr` you obtain either points to a living object or is empty—there will be no dangling pointer problem where "you obtain a pointer but the object has already been deleted."

## How weak pointer Breaks the Cycle

Returning to the previous doubly linked list example, we only need to change the `prev` from `shared_ptr` to `weak_ptr`, and the circular reference is broken:

```cpp
struct NodeFixed {
    std::string name;
    std::shared_ptr<NodeFixed> next;
    std::weak_ptr<NodeFixed> prev;  // 改为 weak_ptr

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

    a->next = b;  // A → B（B 的强引用计数: 1 → 2）
    b->prev = a;  // B ⇢ A（弱引用，A 的强引用计数不变，仍然是 1）

    std::cout << "准备离开函数...\n";
    // 函数结束时：
    // a 离开作用域，A 的强引用计数: 1 → 0，A 被销毁
    //   A 的析构会销毁 A->next，B 的强引用计数: 2 → 1
    // b 离开作用域，B 的强引用计数: 1 → 0，B 被销毁
    // 所有节点都被正确释放！
}
```

Run result:

```text
Node(A) 构造
Node(B) 构造
准备离开函数...
~Node(A) 析构
~Node(B) 析构
```

The key lies in the line `b->prev = a`—`weak_ptr` does not increase the strong reference count of `a`. Therefore, when the local variable `a` goes out of scope, the strong reference count of `a` drops directly from 1 to 0, triggering the destructor. The design philosophy of `weak_ptr` can be summed up in one sentence: **"I know you exist, but I will not stop you from leaving."**

This pattern can be generalized to any data structure with "parent-child" or "upstream-downstream" relationships: use `shared_ptr` for the strong reference direction (holding ownership), and use `weak_ptr` for the weak reference direction (observing only, not holding ownership). As long as there is no ring consisting entirely of strong references in the graph, reference counting can work properly.

## weak pointer in the Observer Pattern

The Observer pattern is one of the most important application scenarios for `weak_ptr`. In this pattern, the Subject maintains a list of observers and notifies all observers when the state changes. If the observer list stores `shared_ptr<Observer>`, then as long as the Subject is alive, none of the observers will be destroyed—even if the outside world no longer needs these observers. What's worse, if the observers in turn also hold a `shared_ptr` to the Subject, a circular reference is formed.

The correct approach is: the Subject uses `weak_ptr` to reference the observers (not extending the observers' lifetimes), and the observers can choose to use either `shared_ptr` or `weak_ptr` to reference the Subject.

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
        listeners_.push_back(listener);  // 存储 weak_ptr
    }

    void publish(const std::string& msg) {
        // 清理已销毁的观察者
        listeners_.erase(
            std::remove_if(listeners_.begin(), listeners_.end(),
                [](const std::weak_ptr<EventListener>& w) {
                    return w.expired();
                }),
            listeners_.end()
        );

        // 通知所有存活的观察者
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
        // L1 和 L2 都能收到

        std::cout << "--- L2 离开作用域 ---\n";
    }
    // L1 和 L2 都离开了作用域
    // 但 EventBus 用的是 weak_ptr，所以不会阻止它们被销毁

    bus.publish("第二条消息");
    // 没有观察者能收到——它们已经被销毁了
}
```

Run result:

```text
Listener(L1) 创建
Listener(L2) 创建
[L1] 收到事件: 第一条消息
[L2] 收到事件: 第一条消息
--- L2 离开作用域 ---
~Listener(L2) 销毁
~Listener(L1) 销毁
```

This pattern is very common in real-world engineering. GUI frameworks (Qt's signal-slot mechanism under certain configurations), game engine event systems, and network library callback mechanisms all face similar problems—an event source should not prevent the destruction of an event consumer. `weak_ptr` exactly provides this "loosely coupled" observation semantics.

## weak pointer in Cache Implementations

Another classic application scenario for `weak_ptr` is caching. The core semantic of a cache is: entries in the cache can be reclaimed at any time—if no one is using an entry, delete it to free memory. `weak_ptr` is naturally suited to express this semantic: the cache stores `weak_ptr`, and when a user retrieves an entry, they temporarily obtain a `shared_ptr` via `lock()`.

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
        // 先尝试从缓存获取
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            if (auto cached = it->second.lock()) {
                std::cout << "缓存命中: " << key << "\n";
                return cached;
            }
            // weak_ptr 已过期，从缓存中移除
            cache_.erase(it);
        }

        // 缓存未命中，加载资源
        auto resource = std::make_shared<ExpensiveResource>(key);
        cache_[key] = resource;  // 存储 weak_ptr
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
        auto r1 = cache.get("texture/player.png");  // 缓存未命中，加载
        auto r2 = cache.get("texture/player.png");  // 缓存命中

        std::cout << "缓存中的条目数: " << cache.size() << "\n";  // 1

        // r1 和 r2 离开作用域
    }

    std::cout << "资源已无人使用\n";
    std::cout << "缓存中的条目数: " << cache.size() << "\n";  // 0（weak_ptr 已过期）

    auto r3 = cache.get("texture/player.png");  // 需要重新加载
}
```

Run result:

```text
加载资源: texture/player.png
缓存命中: texture/player.png
缓存中的条目数: 1
释放资源: texture/player.png
资源已无人使用
缓存中的条目数: 0
加载资源: texture/player.png
```

This cache design is very natural: the cache itself does not hold a strong reference to the resource (using `weak_ptr`), so when all users release the resource, it is automatically reclaimed. The next time it is accessed, the cache will find that the `weak_ptr` has expired and reload the resource. There is no need for manual "reference count checks" or "timed cleanups"—the expiration mechanism of `weak_ptr` automatically handles these tasks.

## Common Misuse: Overusing weak pointer

Although `weak_ptr` is a powerful tool for solving circular references, overusing it actually increases code complexity and the probability of errors. I have seen some codebases that replace almost all pointers with `weak_ptr`, terrified of circular references—this is actually overcorrecting.

First is the performance issue. Every time you access an object through a `weak_ptr`, you need to call `lock()`, which involves atomic operations (checking and incrementing the reference count). Frequently calling `lock()` on a hot path brings measurable performance overhead. According to benchmarks from `test_weak_ptr_performance.cpp`, accessing via `weak_ptr::lock()` is about 10 to 15 times slower than directly accessing a `shared_ptr` (under -O2 optimization, 10 million iterations: direct access takes about 5ms, lock() access takes about 62ms). Although this absolute time difference might not seem large in practical applications, if it is frequently called in performance-sensitive code paths, the overhead accumulates.

Second is semantic ambiguity. If your code is full of `weak_ptr` everywhere, readers will have a hard time determining which objects have true ownership relationships. Ownership relationships should be clarified as much as possible during the design phase, rather than using `weak_ptr` to dodge ownership design.

My recommendation is: in most cases, use `unique_ptr` to express exclusive ownership, and use raw pointers or references to express non-owning access. Only use `weak_ptr` to break cycles when you genuinely need shared ownership and there is a risk of circular references. `weak_ptr` is a precise tool, not a "sprinkle everywhere" panacea.

Another common mistake is trying to use `weak_ptr` to "observe" stack objects or objects managed by `unique_ptr`—this is impossible, because `weak_ptr` can only be used in conjunction with `shared_ptr`. If you want to observe the lifetime of a non-shared object, you need to use other mechanisms (such as callback functions, a manual implementation of the Observer pattern, or changing the object to be managed by `shared_ptr`).

## Summary

`weak_ptr` is the partner of `shared_ptr`. Through a "weak reference" mechanism that does not participate in strong reference counting, it solves the circular reference problem of `shared_ptr`. Its three core APIs—`lock()`, `expired()`, and `use_count()`—provide safe "observe but don't own" semantics.

In practical applications, `weak_ptr` is mainly used in three scenarios: breaking circular references in data structures (doubly linked lists, trees, graphs), implementing the loosely coupled notification mechanism of the Observer pattern, and building auto-reclaiming cache systems. Mastering these three patterns means mastering the core usage of `weak_ptr`.

But remember, `weak_ptr` is not a panacea. Overusing it makes code harder to understand and maintain. Good design should prioritize clarifying ownership relationships, introducing `weak_ptr` only when necessary.

In the next article, we will discuss custom deleters and intrusive reference counts—exploring in depth how to make smart pointers manage resources that "weren't created with new."

## Reference Resources

- [cppreference: std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [cppreference: std::weak_ptr::lock](https://en.cppreference.com/w/cpp/memory/weak_ptr/lock)
- [Using weak_ptr to implement the Observer pattern](https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern)
- [C++ Smart Pointers: weak_ptr and cyclic reference](https://www.nextptr.com/tutorial/ta1382183122/using-weak_ptr-for-circular-references)
- Herb Sutter, *GotW #89: Smart Pointers*
