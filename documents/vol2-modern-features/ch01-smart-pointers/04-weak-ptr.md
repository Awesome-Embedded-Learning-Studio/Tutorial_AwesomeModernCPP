---
title: "weak_ptr 与循环引用：打破所有权的死锁"
description: "掌握 weak_ptr 的弱引用机制，解决 shared_ptr 的循环引用问题"
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
cpp_standard: [11, 14, 17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 1: shared_ptr 详解"
related:
  - "自定义删除器"
---

# weak_ptr 与循环引用：打破所有权的死锁

上一篇我们聊了 `shared_ptr`——通过引用计数实现共享所有权。`shared_ptr` 看起来很美好：只要最后一个持有者离开，对象就自动销毁。但现实是，这个"自动销毁"有一个致命的敌人：**循环引用**。当两个对象互相持有对方的 `shared_ptr` 时，它们的引用计数永远不会归零——两个"管家"互相以为对方还持有钥匙，谁也不敢关门，结果就是内存泄漏。

`std::weak_ptr` 就是为解决这个问题而生的。它是一种"不参与引用计数"的观察者指针——你可以通过它查看对象是否还活着，如果活着就临时获取一个 `shared_ptr` 来访问，但它本身不会延长对象的生命周期。

## 循环引用问题演示

在深入 `weak_ptr` 之前，我们先来直观地感受一下循环引用的问题。经典的例子是双向链表：每个节点持有下一个节点的 `shared_ptr`，如果是双向链表，还持有上一个节点的 `shared_ptr`。这样一来，每个节点都被相邻节点的 `shared_ptr` 引用着，形成了一个环——引用计数永远不归零。

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

运行这段代码你会发现：`~Node()` 的析构输出**永远不会出现**——`Node("A") 析构` 和 `~Node("B") 析构` 都没有打印。两个节点互相持有对方的 `shared_ptr`，形成了一个"死锁环"，谁都不会被释放。这就是循环引用导致的内存泄漏。

这种问题在实际工程中并不罕见。观察者模式中，主题（Subject）持有观察者的 `shared_ptr`，观察者也持有主题的 `shared_ptr`；树形结构中，父节点持有子节点的 `shared_ptr`，子节点也持有父节点的 `shared_ptr`；图结构中，任意两个相邻节点都可能互相引用。只要形成了环，`shared_ptr` 的引用计数机制就失灵了。

## weak_ptr 的 API：lock()、expired()、use_count()

`weak_ptr` 是 `shared_ptr` 的搭档——它指向 `shared_ptr` 管理的对象，但不增加强引用计数。你可以把它理解为一张"参观券"：你可以凭券去看看对象还在不在，但不能凭券阻止对象被销毁。

`weak_ptr` 提供三个核心 API：

`lock()` 是最重要的方法。它尝试获取一个指向对象的 `shared_ptr`。如果对象仍然存在（强引用计数 > 0），返回一个有效的 `shared_ptr`；如果对象已经被销毁（强引用计数 = 0），返回一个空的 `shared_ptr`（即 `nullptr`）。这个操作是原子性的——在 `lock()` 返回 `shared_ptr` 之后，对象的引用计数至少是 1，所以在你使用这个 `shared_ptr` 期间对象不会被销毁。

`expired()` 返回一个 bool 值，表示对象是否已经被销毁（即强引用计数是否为 0）。不过在实际使用中，通常推荐直接用 `lock()` 而不是先检查 `expired()` 再 `lock()`——因为在多线程环境下，`expired()` 返回 `false` 之后到调用 `lock()` 之间，对象可能已经被另一个线程销毁了。`lock()` 本身就已经包含了检查 + 获取的原子操作。

`use_count()` 返回当前指向对象的 `shared_ptr` 数量（即强引用计数）。和 `expired()` 一样，返回值在你使用时可能已经过时了，所以一般只用于调试和日志。

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

⚠️ `weak_ptr` 不能直接解引用——你无法写 `*weak` 或 `weak->member`。必须先通过 `lock()` 获取 `shared_ptr`，然后通过 `shared_ptr` 访问对象。这个设计是故意的：`weak_ptr` 是一种"不确定对象是否还存在"的引用，直接访问太危险了。`lock()` 的原子检查保证了你获取到的 `shared_ptr` 要么指向一个活着的对象，要么是空的——不会出现"获取到指针但对象已经被删"的悬垂指针问题。

## weak_ptr 打破循环的原理

回到之前的双向链表示例，我们只需要把 `prev` 从 `shared_ptr` 改成 `weak_ptr`，循环引用就被打破了：

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

运行结果：

```text
Node(A) 构造
Node(B) 构造
准备离开函数...
~Node(A) 析构
~Node(B) 析构
```

关键在于 `b->prev = a` 这一行——`weak_ptr` 不增加 `a` 的强引用计数。所以当局部变量 `a` 离开作用域时，`a` 的强引用计数从 1 直接降到 0，触发析构。`weak_ptr` 的设计哲学可以归纳为一句话：**"我知道你的存在，但我不会阻止你离开"**。

这个模式可以推广到任何有"父子关系"或"上下游关系"的数据结构：强引用方向用 `shared_ptr`（持有所有权），弱引用方向用 `weak_ptr`（仅观察，不持有所有权）。只要图中不存在全是强引用的环，引用计数就能正常工作。

## 观察者模式中的 weak_ptr

观察者模式是 `weak_ptr` 最重要的应用场景之一。在这个模式中，主题（Subject）维护一个观察者列表，当状态变化时通知所有观察者。如果观察者列表存储的是 `shared_ptr<Observer>`，那么只要主题还活着，所有观察者都不会被销毁——即使外部已经不再需要这些观察者了。更糟糕的是，如果观察者反过来也持有主题的 `shared_ptr`，就会形成循环引用。

正确的做法是：主题用 `weak_ptr` 引用观察者（不延长观察者的生命周期），观察者可以选择用 `shared_ptr` 或 `weak_ptr` 引用主题。

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

运行结果：

```text
Listener(L1) 创建
Listener(L2) 创建
[L1] 收到事件: 第一条消息
[L2] 收到事件: 第一条消息
--- L2 离开作用域 ---
~Listener(L2) 销毁
~Listener(L1) 销毁
```

这个模式在实际工程中非常常见。GUI 框架（Qt 的信号槽机制在某些配置下）、游戏引擎的事件系统、网络库的回调机制，都会面临类似的问题——事件源不应该阻止事件消费者的销毁。`weak_ptr` 正好提供了这种"松耦合"的观察语义。

## 缓存实现中的 weak_ptr

另一个经典的 `weak_ptr` 应用场景是缓存。缓存的核心语义是：缓存中的条目可以被随时回收——如果没有人使用它，就把它删掉以释放内存。`weak_ptr` 天然适合表达这种语义：缓存存储 `weak_ptr`，使用者获取时通过 `lock()` 临时获取 `shared_ptr`。

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

运行结果：

```text
加载资源: texture/player.png
缓存命中: texture/player.png
缓存中的条目数: 1
释放资源: texture/player.png
资源已无人使用
缓存中的条目数: 0
加载资源: texture/player.png
```

这个缓存的设计非常自然：缓存本身不持有资源的强引用（用 `weak_ptr`），所以当所有使用者都释放了资源后，资源会自动被回收。下次再访问时，缓存会发现 `weak_ptr` 已过期，重新加载资源。不需要手动的"引用计数检查"或"定时清理"——`weak_ptr` 的过期机制自动完成了这些工作。

## 常见误用：过度使用 weak_ptr

虽然 `weak_ptr` 是解决循环引用的利器，但过度使用它反而会增加代码的复杂性和出错概率。笔者见过一些代码库几乎把所有指针都换成 `weak_ptr`，生怕出现循环引用——这其实是矫枉过正。

首先是性能问题。每次通过 `weak_ptr` 访问对象都需要调用 `lock()`，这涉及一次原子操作（检查引用计数并递增）。在热路径中频繁 `lock()` 会带来可测量的性能开销。

其次是语义模糊。如果你的代码中到处都是 `weak_ptr`，读者很难判断哪些对象之间有真正的所有权关系。所有权关系应该尽量在设计阶段就理清楚，而不是用 `weak_ptr` 来回避所有权设计。

笔者的建议是：在大多数情况下，用 `unique_ptr` 表达独占所有权，用裸指针或引用表达非拥有访问。只在确实需要共享所有权且存在循环引用风险时，才用 `weak_ptr` 打破循环。`weak_ptr` 是一种精确的工具，而不是一种"到处撒一把"的万能药。

还有一种常见的错误是用 `weak_ptr` 来"观察"栈上的对象或由 `unique_ptr` 管理的对象——这不可能做到，因为 `weak_ptr` 只能与 `shared_ptr` 配合使用。如果你想观察非共享对象的生命周期，需要用其他机制（比如回调函数、观察者模式的手动实现、或者把对象改为 `shared_ptr` 管理）。

## 小结

`weak_ptr` 是 `shared_ptr` 的搭档，通过不参与强引用计数的"弱引用"机制，解决了 `shared_ptr` 的循环引用问题。它的三个核心 API——`lock()`、`expired()`、`use_count()`——提供了安全的"查看但不拥有"的语义。

在实际应用中，`weak_ptr` 主要用于三种场景：打破数据结构中的循环引用（双向链表、树、图）、实现观察者模式的松耦合通知机制、以及构建自动回收的缓存系统。掌握这三种模式，就掌握了 `weak_ptr` 的核心用法。

但记住，`weak_ptr` 不是万能药。过度使用会让代码更难理解和维护。好的设计应该优先理清所有权关系，只在必要时引入 `weak_ptr`。

下一篇我们将讨论自定义删除器和侵入式引用计数——深入探讨如何让智能指针管理那些"不是 new 出来的"资源。

## 参考资源

- [cppreference: std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [cppreference: std::weak_ptr::lock](https://en.cppreference.com/w/cpp/memory/weak_ptr/lock)
- [Using weak_ptr to implement the Observer pattern](https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern)
- [C++ Smart Pointers: weak_ptr and cyclic reference](https://www.nextptr.com/tutorial/ta1382183122/using-weak_ptr-for-circular-references)
- Herb Sutter, *GotW #89: Smart Pointers*
