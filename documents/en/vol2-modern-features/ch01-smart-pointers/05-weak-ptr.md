---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master weak_ptr's weak-reference mechanism and solve shared_ptr's circular reference problem
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
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
  source: documents/vol2-modern-features/ch01-smart-pointers/05-weak-ptr.md
  source_hash: 66e5193847198bc51fdf4a5dbc7a16b0b0f62c6853b8371b22defe9c284eecae
  translated_at: '2026-09-27T04:57:20+00:00'
  engine: anthropic
  token_count: 6500
---
# weak_ptr and Circular References: Breaking the Ownership Deadlock

Last time we talked about `shared_ptr` — shared ownership through reference counting. `shared_ptr` looks wonderful: the moment the last holder walks away, the object destroys itself. The reality, though, is that this "automatic destruction" has one deadly enemy: **circular references**. When two objects each hold a `shared_ptr` to the other, their reference counts never reach zero — the two "caretakers" each assume the other still holds the key, neither dares to lock the door, and the result is a memory leak.

`std::weak_ptr` was born to solve exactly this problem. It is an observer pointer that "doesn't participate in reference counting" — you can use it to see whether the object is still alive, and if it is, temporarily obtain a `shared_ptr` to access it, but the `weak_ptr` itself never extends the object's lifetime<RefLink :id="1" preview="cppreference std::weak_ptr — non-owning observer of shared_ptr-managed objects" />.

## Demonstrating the Circular Reference Problem

Before diving into `weak_ptr`, let's feel the circular-reference problem firsthand. The classic example is a doubly linked list: each node holds a `shared_ptr` to the next node, and — since the list is doubly linked — also a `shared_ptr` to the previous node. Every node is then kept referenced by its neighbors' `shared_ptr`s, forming a loop, and the reference count never falls back to zero.

```cpp
#include <memory>
#include <iostream>
#include <string>

struct Node {
    std::string name;
    std::shared_ptr<Node> next;
    std::shared_ptr<Node> prev;  // the shared_ptr here is what creates the circular reference

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
    // a goes out of scope, A's reference count: 2 → 1 (B->prev still holds A)
    // b goes out of scope, B's reference count: 2 → 1 (A->next still holds B)
    // Result: A and B both sit at a reference count of 1 that never reaches zero — memory leak!
}
```

Run this code and you'll find that the `~Node()` destructor output **never appears** — neither `~Node("A") 析构` nor `~Node("B") 析构` gets printed. The two nodes each hold a `shared_ptr` to the other, forming a "deadlock loop" in which neither is ever released. That is a memory leak caused by a circular reference.

This deadlock loop comes as an animation — you can play it, pause it, or single-step through it with the step buttons to watch how the two `shared_ptr`s interlock and how a `weak_ptr` breaks the loop open:

<Anim id="weak-ptr-cycle" />

This kind of problem is not rare in real projects. In the observer pattern, the subject holds `shared_ptr`s to its observers while the observers hold `shared_ptr`s to the subject; in tree structures, the parent holds `shared_ptr`s to its children while the children hold `shared_ptr`s to the parent; in graph structures, any two adjacent nodes may reference each other. As soon as a loop forms, `shared_ptr`'s reference-counting mechanism stops working<RefLink :id="2" preview="nextptr — Using weak_ptr for circular references, 2020" />.

## The weak_ptr API: lock(), expired(), use_count()

`weak_ptr` is `shared_ptr`'s partner — it points at an object managed by a `shared_ptr` without incrementing the strong reference count. You can think of it as a "visitor pass": the pass lets you go see whether the object is still around, but it cannot stop the object from being destroyed.

`weak_ptr` provides three core APIs:

`lock()` is the most important of the three. It tries to obtain a `shared_ptr` pointing at the object. If the object still exists (strong reference count > 0), it returns a valid `shared_ptr`; if the object has already been destroyed (strong reference count = 0), it returns an empty `shared_ptr` (that is, `nullptr`). `lock()` is thread-safe — in a multi-threaded environment, several threads can call `lock()` at the same time, and the standard guarantees that the returned `shared_ptr` either points at a valid object or is empty. You can never land in the dangling situation of "got the pointer, but the object was already deleted"<RefLink :id="3" preview="cppreference std::weak_ptr::lock — atomic check-and-increment" />.

`expired()` returns a bool telling whether the object has already been destroyed (that is, whether the strong reference count is 0). In practice, though, the usual recommendation is to call `lock()` directly instead of checking `expired()` first and calling `lock()` afterwards — in a multi-threaded environment, between the moment `expired()` returns `false` and the moment you call `lock()`, another thread may already have destroyed the object, and that is a race condition. `lock()` folds "check whether the object exists" and "increment the reference count" into a single operation, which sidesteps the problem.

`use_count()` returns the number of `shared_ptr`s currently pointing at the object (the strong reference count). Like `expired()`, its return value may already be stale by the time you use it, so it is generally reserved for debugging and logging.

```cpp
#include <memory>
#include <iostream>

void weak_ptr_api_demo() {
    std::weak_ptr<int> weak;

    {
        auto shared = std::make_shared<int>(42);
        weak = shared;  // weak doesn't increment the reference count

        std::cout << "use_count: " << weak.use_count() << "\n";  // 1
        std::cout << "expired: " << weak.expired() << "\n";      // 0 (false)

        // Obtain a shared_ptr through lock()
        if (auto locked = weak.lock()) {
            std::cout << "value: " << *locked << "\n";  // 42
            std::cout << "use_count after lock: "
                      << weak.use_count() << "\n";  // 2
        }
        // locked goes out of scope, the reference count drops back to 1
    }

    // shared has been destroyed
    std::cout << "expired after scope: " << weak.expired() << "\n";  // 1 (true)

    // lock() returns an empty shared_ptr
    auto locked = weak.lock();
    std::cout << "locked is nullptr: " << (locked == nullptr) << "\n";  // 1 (true)
}
```

A `weak_ptr` cannot be dereferenced directly — you cannot write `*weak` or `weak->member`. You must first obtain a `shared_ptr` through `lock()` and then reach the object through that `shared_ptr`. The design is deliberate: a `weak_ptr` is a reference of the "not sure whether the object still exists" kind, and accessing it directly is far too dangerous. `lock()`'s atomic check guarantees that the `shared_ptr` you obtain either points at a living object or is empty — no "got the pointer but the object was already deleted" dangling-pointer problem.

## How weak_ptr Breaks the Cycle

Back in the doubly linked list example, all we have to do is change `prev` from `shared_ptr` to `weak_ptr`, and the circular reference is broken:

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
    // a goes out of scope, A's strong reference count: 1 → 0, A is destroyed
    //   destroying A destroys A->next, so B's strong reference count: 2 → 1
    // b goes out of scope, B's strong reference count: 1 → 0, B is destroyed
    // Every node is correctly released!
}
```

The demo program is right below — click "Try it yourself" to run it directly:

<OnlineCompilerDemo
  title="Try It Yourself: weak_ptr Breaks the Circular Reference"
  source-path="code/examples/vol2/27_weak_ptr_circular_fix.cpp"
  description="Verify online how weak_ptr breaks the circular reference: once prev becomes a weak_ptr, both nodes A and B are correctly destroyed when the function ends."
  run-options="-std=c++17"
  allow-run
/>

The key is the line `b->prev = a` — the `weak_ptr` does not increment `a`'s strong reference count. So when the local variable `a` goes out of scope, `a`'s strong reference count drops straight from 1 to 0 and the destructor fires. `weak_ptr`'s design philosophy compresses into one sentence: **"I know you exist, but I won't stop you from leaving"**.

This pattern generalizes to any data structure with a "parent-child" or "upstream-downstream" relationship: use `shared_ptr` along the strong-reference direction (holding ownership), and `weak_ptr` along the weak-reference direction (observing only, holding no ownership). As long as the graph contains no cycle made purely of strong references, reference counting works as it should.

## weak_ptr in the Observer Pattern

The observer pattern is one of `weak_ptr`'s most important application scenarios. In this pattern, the subject maintains a list of observers and notifies them all when its state changes. If that list stores `shared_ptr<Observer>`, then for as long as the subject lives, none of the observers can be destroyed — even if the outside world no longer needs them. Worse, if the observers also hold a `shared_ptr` back to the subject, a circular reference forms<RefLink :id="4" preview="Stack Overflow — Using weak_ptr to implement the Observer pattern" />.

The correct approach: the subject references its observers through `weak_ptr` (without extending their lifetimes), and each observer chooses between `shared_ptr` and `weak_ptr` for referencing the subject.

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
        // Clean out observers that have already been destroyed
        listeners_.erase(
            std::remove_if(listeners_.begin(), listeners_.end(),
                [](const std::weak_ptr<EventListener>& w) {
                    return w.expired();
                }),
            listeners_.end()
        );

        // Notify all the living observers
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
    // L1 and L2 have both gone out of scope
    // but the EventBus holds weak_ptrs, so it cannot stop them from being destroyed

    bus.publish("第二条消息");
    // No observer receives it — they have already been destroyed
}
```

The demo program is right below — click "Try it yourself" to run it directly:

<OnlineCompilerDemo
  title="Try It Yourself: weak_ptr Observers"
  source-path="code/examples/vol2/28_weak_ptr_observer.cpp"
  description="Verify the observer pattern online: the EventBus stores observers as weak_ptrs and never blocks their destruction — the second publish produces no output, because the observers have all been destroyed."
  run-options="-std=c++17"
  allow-run
/>

This pattern is extremely common in real engineering. GUI frameworks (Qt's signal-slot mechanism under certain configurations), game engines' event systems, and network libraries' callback mechanisms all face the same problem — an event source should not prevent its consumers from being destroyed. `weak_ptr` provides exactly this "loosely coupled" observation semantics.

## weak_ptr in a Cache Implementation

Another classic scenario for `weak_ptr` is caching. The core semantic of a cache is that entries can be reclaimed at any moment — if nobody is using one, drop it to free memory. `weak_ptr` is a natural fit for expressing this: the cache stores `weak_ptr`s, and users go through `lock()` to temporarily obtain a `shared_ptr` when they fetch.

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
        // Try the cache first
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
        cache_[key] = resource;  // store a weak_ptr
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

        // r1 and r2 go out of scope
    }

    std::cout << "资源已无人使用\n";
    std::cout << "缓存中的条目数: " << cache.size() << "\n";  // 0 (the weak_ptr has expired)

    auto r3 = cache.get("texture/player.png");  // needs to be reloaded
}
```

The demo program is right below — click "Try it yourself" to run it directly:

<OnlineCompilerDemo
  title="Try It Yourself: A weak_ptr Cache"
  source-path="code/examples/vol2/29_weak_ptr_cache.cpp"
  description="Verify the weak_ptr cache online: once every user lets go, the resource is released automatically (the entry count drops to zero), and the next access reloads it."
  run-options="-std=c++17"
  allow-run
/>

The design of this cache is very natural: the cache itself holds no strong reference to the resource (it uses `weak_ptr`), so once every user has released the resource, the resource is reclaimed automatically. On the next access, the cache finds the `weak_ptr` expired and loads the resource again. No manual "reference-count checking", no "periodic cleanup" — `weak_ptr`'s expiration mechanism does all of that by itself.

## A Common Misuse: Overusing weak_ptr

`weak_ptr` is a sharp tool for breaking circular references, but overusing it raises code complexity and the chance of mistakes. We have seen codebases where nearly every pointer was swapped for a `weak_ptr` for fear of circular references — that is overcorrection.

The first problem is performance. Every access through a `weak_ptr` requires calling `lock()`, which involves an atomic operation (checking the reference count and incrementing it). Frequent `lock()` calls on a hot path bring measurable overhead. In our measurements, `weak_ptr::lock()` is about 30x slower than accessing through a `shared_ptr` directly (`lock()` has to perform an atomic operation to grab the reference count; over 10 million iterations, direct access takes about 2 ms while `lock()` takes about 68 ms, GCC 16.1.1 -O2). In a real application this absolute difference may not look large, but called frequently on a performance-sensitive path, the overhead accumulates.

The second problem is blurred semantics. If `weak_ptr` is scattered all over your code, readers can hardly tell which objects stand in a genuine ownership relationship. Ownership should be sorted out at design time wherever possible, not dodged by papering over it with `weak_ptr`.

Our advice: in most cases, express exclusive ownership with `unique_ptr`, and express non-owning access with raw pointers or references. Only when shared ownership is genuinely needed and a circular-reference risk exists should `weak_ptr` step in to break the cycle<RefLink :id="5" preview="Herb Sutter, GotW #89 Solution: Smart Pointers, 2013" />. `weak_ptr` is a precision tool, not a "sprinkle it everywhere" cure-all.

Another common mistake is using a `weak_ptr` to "observe" an object on the stack or an object managed by a `unique_ptr` — that cannot be done, because `weak_ptr` only works together with `shared_ptr`. If you want to observe the lifetime of a non-shared object, you need a different mechanism (callback functions, a hand-rolled observer pattern, or moving the object under `shared_ptr` management).

Next time we will talk about custom deleters and intrusive reference counting — how to make smart pointers manage resources that did not come from `new`.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::weak_ptr"
    url="https://en.cppreference.com/w/cpp/memory/weak_ptr"
  />
  <ReferenceItem
    :id="2"
    author="nextptr"
    title="Using weak_ptr for Circular References"
    :year="2020"
    url="https://www.nextptr.com/tutorial/ta1382183122/using-weak_ptr-for-circular-references"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::weak_ptr::lock"
    url="https://en.cppreference.com/w/cpp/memory/weak_ptr/lock"
  />
  <ReferenceItem
    :id="4"
    title="Using weak_ptr to Implement the Observer Pattern"
    publisher="Stack Overflow"
    url="https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern"
  />
  <ReferenceItem
    :id="5"
    author="Herb Sutter"
    title="GotW #89 Solution: Smart Pointers"
    publisher="herbsutter.com"
    :year="2013"
    url="https://herbsutter.com/2013/05/29/gotw-89-solution-smart-pointers/"
  />
</ReferenceCard>
