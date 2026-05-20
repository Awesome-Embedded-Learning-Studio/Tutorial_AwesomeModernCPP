---
title: "读写锁与 shared_mutex"
description: "C++17 shared_mutex 的读多写少场景应用，分析写饥饿问题与性能边界"
chapter: 2
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
difficulty: intermediate
platform: host
reading_time_minutes: 20
cpp_standard: [17, 20]
prerequisites:
  - "condition_variable 与等待语义"
related:
  - "mutex 与 RAII 锁"
  - "线程安全容器设计"
---

# 读写锁与 shared_mutex

到目前为止，我们讨论的同步原语都是"排他式"的——一个线程拿到锁，其他所有线程都得在外面等着。但现实中有一大类场景并不是这样的：**读多写少**。配置数据、缓存、路由表、字典——这些东西绝大部分时间在被读取，偶尔才被更新。如果每次读取都要排他地获取 mutex，那多个读线程之间就被不必要地串行化了——它们完全可以并发地读取同一个数据结构，因为读操作不会修改任何状态。

读写锁（Reader-Writer Lock）就是为了解决这个问题而生的。它区分两种锁模式：**共享模式（shared / 读锁）**和**独占模式（exclusive / 写锁）**。多个线程可以同时持有读锁进行读取，但写锁要求独占访问——任何其他线程（无论读还是写）都不能同时持有锁。C++17 引入的 `std::shared_mutex` 就是标准库对读写锁的实现。

## std::shared_mutex：两种锁定模式

`std::shared_mutex` 定义在 `<shared_mutex>` 头文件中（C++17 起可用）。它提供了两套锁定接口：写锁的 `lock()` / `unlock()` / `try_lock()`（和普通 `std::mutex` 一样），以及共享锁的 `lock_shared()` / `unlock_shared()` / `try_lock_shared()`。直接调用这些原始接口当然可以，但我们不会这么做——RAII 包装器才是正确姿势，上一篇的教训不能白学。

先看一个最基本的使用场景。假设我们有一个配置字典，偶尔被更新，频繁被查询：

```cpp
#include <iostream>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <thread>
#include <vector>

class ThreadSafeConfig {
public:
    std::string get(const std::string& key) const
    {
        // 读操作：获取共享锁
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : "";
    }

    void set(const std::string& key, const std::string& value)
    {
        // 写操作：获取独占锁
        std::unique_lock<std::shared_mutex> lock(mutex_);
        data_[key] = value;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};
```

`get` 方法使用 `std::shared_lock<std::shared_mutex>` 获取共享锁。多个线程可以同时持有 `shared_lock`——它们不会互相阻塞。`set` 方法使用 `std::unique_lock<std::shared_mutex>` 获取独占锁。当任何线程持有独占锁时，其他线程（无论想要共享锁还是独占锁）都必须等待；反过来，如果有线程持有共享锁，想要获取独占锁的线程也必须等到所有共享锁释放。

注意 `mutex_` 被声明为 `mutable`——因为 `get` 是 `const` 成员函数，但它需要修改 mutex 的状态（加锁/解锁）。这是 `mutable` 的合理用法：mutex 不是对象逻辑状态的一部分，它是同步机制的一部分。

## std::shared_lock：共享模式的 RAII 包装器

`std::shared_lock` 是 `std::unique_lock` 的"共享版本"，定义在 `<shared_mutex>` 头文件中。它的接口和 `unique_lock` 高度对称——构造时获取共享锁，析构时释放共享锁，支持延迟加锁（`defer_lock`）、手动加锁解锁等操作。但它调用的是 `lock_shared()` / `unlock_shared()` 而不是 `lock()` / `unlock()`。

为什么需要单独的 `shared_lock` 而不是给 `unique_lock` 加一个参数控制模式？原因在于类型安全。如果你有一个接受 `std::unique_lock<SharedMutex>` 参数的函数，你可以确定它持有的是独占锁——编译器帮你做了保证。反过来，`std::shared_lock<SharedMutex>` 保证持有的是共享锁。两种锁模式的语义完全不同，用不同的类型来表达是最安全的做法。

一个值得了解的用法是 `shared_lock` 配合 `condition_variable_any`（上一篇提到的通用条件变量）实现"共享等待"。普通 `condition_variable` 只接受 `unique_lock`，但 `condition_variable_any` 接受任何锁类型——包括 `shared_lock`。这允许你在持有共享锁的情况下等待条件变量，某些高级模式（比如读写锁的升级协议）会用到这个能力。

## 完整模式：读取用 shared_lock，写入用 unique_lock

读写锁的标准用法可以总结为一句话：**读时 shared_lock，写时 unique_lock**。我们来看一个更完整的例子——一个简单的线程安全缓存：

```cpp
#include <shared_mutex>
#include <unordered_map>
#include <string>
#include <optional>
#include <functional>
#include <mutex>

template <typename Key, typename Value>
class ThreadSafeCache {
public:
    /// @brief 查询缓存，命中则返回值，未命中则计算并存入
    Value get_or_compute(const Key& key,
                          std::function<Value(const Key&)> compute)
    {
        // 第一步：读锁下查询
        {
            std::shared_lock<std::shared_mutex> read_lock(mutex_);
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
        }

        // 第二步：锁外计算（避免持锁期间做重活）
        Value value = compute(key);

        // 第三步：写锁下 double-check 并写入
        {
            std::unique_lock<std::shared_mutex> write_lock(mutex_);
            // double-check：另一个线程可能在我们释放读锁到获取写锁之间已经插入了
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
            cache_[key] = value;
        }

        return value;
    }

    /// @brief 清空缓存
    void clear()
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_.clear();
    }

    /// @brief 获取缓存大小
    std::size_t size() const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<Key, Value> cache_;
};
```

这段代码展示了一个非常重要的模式——**double-checked locking**。为什么要在写锁之前再读一次？因为从第一次读锁释放到获取写锁之间有一个时间窗口，其他线程可能已经插入了相同的 key。如果我们不加这个 double-check，就可能覆盖其他线程的计算结果，甚至重复计算浪费资源。

另一个值得注意的点是 `compute(key)` 在**写锁外部**执行。这是刻意的设计——计算可能很耗时，如果在持写锁期间计算，所有读线程都会被阻塞。把计算移到锁外面，只在最终写入时获取写锁，可以最大化并发度。当然，这种做法的代价是可能重复计算——多个线程可能同时对同一个 key 执行 compute。如果你的 compute 非常昂贵且需要保证唯一性，你可能需要在写锁内部执行计算，牺牲并发度换取正确性。

## 写饥饿：读写锁的阴暗面

读写锁看起来很美——读不阻塞读，写阻塞一切。但这里藏着一个隐蔽的问题：**写饥饿（writer starvation）**。想象一下这个场景：有 10 个读线程源源不断地请求共享锁，它们来来去去，每个时刻总有那么几个在读。此时一个写线程想要获取独占锁——它必须等到**所有**共享锁都被释放。但问题是，如果读线程到达的频率足够高，共享锁永远不可能全部同时释放——总有一个新的读请求在旧的读完之前就进来了。写线程就这样被"饿着"，永远等不到独占访问的机会。

C++ 标准对 `std::shared_mutex` 的调度策略**没有任何保证**——它不保证公平性，不保证写者优先，不保证读者不会饿死写者。具体的调度行为取决于标准库的实现和底层操作系统。在某些平台上（比如 Windows 的 SRWLock），实现会倾向于写者优先——当一个写者等待时，新的读者会被阻塞，直到写者完成。但在其他平台上，读者可能会持续地获得共享锁，导致写者长时间等待。

这意味着什么？如果你使用 `std::shared_mutex`，你需要意识到写饥饿的可能性，并评估它对你的应用是否构成问题。如果你的场景是"读远多于写，写的延迟不敏感"，那读写锁的收益远大于风险。但如果写的实时性很重要（比如实时控制系统中的参数更新），读写锁可能不是最好的选择——你需要一个有写者优先保证的自定义读写锁，或者干脆用普通 `std::mutex` 配合写时复制（copy-on-write）策略。

## 性能边界：什么时候读写锁反而更慢

这一节可能会让一些人意外：**读写锁不是万能的，在某些场景下它比普通 mutex 还慢**。原因在于读写锁的内部实现比 mutex 复杂得多——它需要维护读者计数、管理等待队列、处理读写之间的优先级。这些额外的管理开销意味着即使在低竞争场景下，读写锁的每次加锁/解锁操作都比 mutex 贵。

那么 crossover point 在哪里？根据一些基准测试（比如 2025 年 Google Benchmark 上的一项对比研究），在低线程数（2-4 线程）的场景下，`std::mutex` 通常比 `std::shared_mutex` 快——因为此时竞争不激烈，mutex 的简单性胜出。当线程数增加且读操作占主导（比如 8 个读线程 + 1 个写线程）时，`shared_mutex` 开始展现优势——多个读线程可以并发执行，吞吐量显著提升。线程数越多、读写比越高，读写锁的优势越明显。

还有几个因素会影响读写锁的性能表现。首先是临界区的大小——如果临界区非常短（比如只读一个 `int`），mutex 的开销和读写锁差不多，读写锁的额外管理成本反而拖了后腿。但如果临界区很长（比如遍历一个大的 map 或者做复杂的查询），读写锁允许并发读取的收益就很可观。其次是硬件缓存的影响——读写锁的读者计数器是一个共享的原子变量，多核环境下可能导致 cache line bouncing（多核处理器频繁争夺同一 cache line 的所有权），在高频读取时可能抵消并发读的收益。

实际项目中，笔者的建议是：先用 `std::mutex`，如果你有明确的"读多写少 + 高并发读取"的性能瓶颈，再考虑换成 `std::shared_mutex`。切换之前最好做个基准测试，用你真实的工作负载来对比，因为 crossover point 跟具体的数据结构、访问模式、硬件环境都有关。过早优化是万恶之源，在同步原语的选择上同样适用。

## std::shared_timed_mutex：带超时的版本

C++14 引入了 `std::shared_timed_mutex`，它是 `std::shared_mutex` 的超时版本——除了基本的共享/独占锁定之外，还支持 `try_lock_for`、`try_lock_until`、`try_lock_shared_for`、`try_lock_shared_until` 等超时操作。C++17 的 `std::shared_mutex` 去掉了超时功能，成为一个更轻量的版本。

如果你的项目还在 C++14，`shared_timed_mutex` 是唯一的选择。如果你在 C++17 及以上，且不需要超时功能，优先使用 `std::shared_mutex`——它的实现更简单，开销更低。需要超时功能的场景跟上一篇讨论的 `wait_for` / `wait_until` 类似——比如"尝试在 100ms 内获取写锁，超时就放弃本次更新"。

## 锁升级与降级：标准未直接支持的进阶操作

锁升级是指把一个共享锁"升级"为独占锁——比如我先读了数据，发现需要修改，于是在不释放锁的情况下升级为写锁。锁降级则是反过来——把独占锁降级为共享锁。这两个操作在某些数据库系统中非常常见（比如事务的锁管理），但 C++ 标准库**不直接支持**它们。

为什么？因为锁升级在多线程环境下会引发死锁。考虑这个场景：线程 A 持有共享锁并尝试升级为独占锁，线程 B 也持有共享锁并尝试升级为独占锁——双方都在等对方释放共享锁，但谁都不会先释放，死锁了。这就是所谓的"升级死锁"。

标准库的做法是要求你**先释放共享锁，再获取独占锁**。这保证了在共享和独占之间有一个"无锁"的空窗期，其他线程可以自由地获取锁。代价是你需要处理空窗期内的状态变化——这就是前面 double-checked locking 模式的用武之地。

```cpp
// 锁升级的手动实现：释放共享锁 -> 获取独占锁
void upgrade_example()
{
    // 读取阶段
    std::shared_lock<std::shared_mutex> read_lock(mutex_);
    auto data = read_something();
    read_lock.unlock();  // 必须先释放共享锁

    // 写入阶段
    std::unique_lock<std::shared_mutex> write_lock(mutex_);
    // 注意：这里的 data 可能已经过期了！
    // 需要重新读取或做 double-check
    write_something(data);
}
```

锁降级（独占 -> 共享）是安全的——从独占降级为共享不会引起死锁，因为降级只释放权限，不请求额外的权限。但标准库同样不直接支持，你需要手动释放独占锁再获取共享锁。有些平台特定的 API（比如 Windows 的 SRWLock）提供了原子的降级操作，但 POSIX `pthread_rwlock` 和 C++ 标准库都没有这个能力——POSIX 下唯一的方式是先 unlock 再 rdlock，中间存在无锁窗口。如果你的场景需要频繁的锁降级，可能需要考虑使用平台特定的 API 或者自定义的读写锁实现。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 练习

### 练习 1：线程安全缓存

实现一个模板类 `ThreadSafeCache<Key, Value>`，支持以下操作：

- `get(key)`：查询缓存，返回 `std::optional<Value>`
- `put(key, value)`：插入或更新
- `remove(key)`：删除
- `size()`：返回当前缓存大小

要求使用 `std::shared_mutex`，读操作（`get`、`size`）使用 `shared_lock`，写操作（`put`、`remove`）使用 `unique_lock`。

然后写一个测试程序：4 个读线程不断查询随机 key，1 个写线程每隔一段时间插入新数据。观察读写是否能并发进行（可以在读操作中加入微小延迟来放大并发效果）。

### 练习 2：对比 mutex 与 shared_mutex 的性能

写一个基准测试：对同一个 `std::unordered_map<int, std::string>`，分别用 `std::mutex` 和 `std::shared_mutex` 保护，然后在多线程下执行 90% 读操作 + 10% 写操作。线程数从 1 递增到 16，记录每种配置下的总耗时。

思考以下问题：

- 在你的平台上，crossover point 在哪个线程数？
- 如果把读写比从 90:10 改成 50:50，结果会怎样？
- 如果临界区非常短（只读一个 int），结果又会怎样？

### 练习 3：复现写饥饿

构造一个场景来观察写饥饿：启动 N 个读线程，每个线程循环获取共享锁、读取数据、释放锁（可以加微小延迟控制读取频率）。然后启动 1 个写线程，尝试获取独占锁来更新数据。测量写线程从请求锁到获取锁的等待时间。逐步增加读线程数量和读取频率，观察写线程的等待时间如何变化。

提示：你可能会发现，在读写比极端的情况下（比如 20 个读线程疯狂读取），写线程的等待时间会急剧增加。这就是写饥饿的直观表现。

## 参考资源

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::shared_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_lock)
- [std::shared_timed_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_timed_mutex)
- [When std::shared_mutex Outperforms std::mutex -- C++ Stories](https://www.cppstories.com/2026/shared_mutex/)
- [Understanding std::shared_mutex from C++17 -- C++ Stories](https://www.cppstories.com/2026/shared_mutex/)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 3](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
