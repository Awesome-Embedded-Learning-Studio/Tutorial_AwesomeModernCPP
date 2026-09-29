---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: 深入死锁的四个必要条件，掌握锁顺序约束、try_lock 回退与 scoped_lock 多锁获取策略
difficulty: intermediate
order: 2
platform: host
prerequisites:
- mutex 与 RAII 锁
reading_time_minutes: 18
related:
- condition_variable 与等待语义
tags:
- host
- cpp-modern
- intermediate
- mutex
title: 死锁与锁顺序
---
# 死锁与锁顺序

上一篇我们系统梳理了 mutex 家族和三个 RAII 锁守卫，掌握了从 `lock_guard` 到 `scoped_lock` 的选择策略。那一篇里我们反复提到"死锁"这个词，但并没有深入展开——因为我们先把工具准备好，再来对付这个真正的敌人。这一篇我们就来正面对抗死锁。

死锁可能是多线程编程中最令人头疼的 bug 之一。它不像 data race 那样给你一个错误的结果——它直接让你的程序卡住不动，而且卡住的条件往往高度依赖线程的调度时序。你本地跑了十万次都正常，上线后凌晨三点在客户环境挂了，拿到 dump 文件一看，两个线程各持一把锁，都在等对方释放——经典的死锁。

这篇的目标很明确：先搞清楚死锁为什么发生（四个必要条件），然后掌握 C++ 标准库提供的死锁预防工具（`std::lock()`、`std::scoped_lock`），最后学习几个工程实践中的死锁预防策略（锁顺序、层级锁、避免回调）。

## Coffman 四条件：死锁的四个必要条件

1971 年，E. G. Coffman Jr.、M. J. Elphick 和 A. Shoshani 在一篇经典论文中提出了死锁发生的四个必要条件。这四个条件必须**同时满足**，死锁才会发生——打破其中任何一个，死锁就不可能存在。理解这四个条件，是预防死锁的理论基础。

**互斥（Mutual Exclusion）**：至少有一个资源在同一时刻只能被一个线程持有。`std::mutex` 天然就是互斥的——只有获得锁的那一个线程能进入临界区，其他线程必须等待。这个条件在大多数场景下是不可打破的——如果资源可以被自由共享，就不需要锁了。

**持有并等待（Hold and Wait）**：线程持有至少一个资源，同时又在等待其他资源。一个线程锁住了 mutex A，然后去尝试锁 mutex B，B 被别人占着，于是线程在 B 上阻塞——但它手里还拿着 A 不放。这就是"持有并等待"。如果我们要求线程在获取新锁之前必须释放已持有的所有锁，这个条件就被打破了。

**不可抢占（No Preemption）**：资源不能被强制从持有者手中夺走。一个线程锁住了 mutex，其他线程没法说"你先让开，我来用用"——只能等那个线程自己 unlock。这个条件在标准 mutex 中也是不可打破的——我们不可能强制另一个线程释放锁。

**循环等待（Circular Wait）**：存在一个线程等待环——线程 1 等线程 2 持有的资源，线程 2 等线程 3 持有的资源，...，线程 N 等线程 1 持有的资源。如果资源的获取总是按照某种固定的全局顺序进行，循环等待就不可能形成——因为顺序关系是传递的，不可能构成环。

四个条件中，互斥和不可抢占通常是锁的本质决定的，不太容易打破。实际工程中的死锁预防策略主要集中在打破"持有并等待"和"循环等待"上。`std::lock()` 和 `std::scoped_lock` 打破的是持有并等待——它们要么一次性获取所有锁，要么一个都不获取。锁顺序策略打破的是循环等待——如果所有线程都按相同的顺序获取锁，等待关系就不可能构成环。

## 经典的两锁反转：AB-BA 死锁

最经典的死锁场景就是两把锁的获取顺序不一致。我们来构造一个最小复现：

```cpp
#include <mutex>
#include <thread>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);   // 先锁 A
    std::cout << "thread1: locked A, waiting for B\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 增加死锁触发概率
    std::lock_guard<std::mutex> lock_b(mtx_b);   // 再锁 B
    std::cout << "thread1: locked both\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);   // 先锁 B
    std::cout << "thread2: locked B, waiting for A\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard<std::mutex> lock_a(mtx_a);   // 再锁 A
    std::cout << "thread2: locked both\n";
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    return 0;
}
```

运行这段代码，程序大概率会卡住。如果 thread1 先拿到 `mtx_a`，thread2 先拿到 `mtx_b`，双方就陷入了循环等待——thread1 持有 A 等 B，thread2 持有 B 等 A，谁都不会放手。我们加了一个 `sleep_for` 来提高死锁的触发概率——在实际项目中，死锁可能只在特定的负载和调度时序下才出现，这也是它难以调试的原因之一。

这个例子完美对应了 Coffman 四条件：互斥（mutex 天然互斥）、持有并等待（thread1 持有 A 等 B）、不可抢占（A 和 B 都不能被强制夺走）、循环等待（thread1 等 thread2，thread2 等 thread1）。

## 锁顺序：最实用的死锁预防策略

锁顺序（Lock Ordering）是预防死锁最直接、最实用的策略，它的核心思想是打破循环等待——所有需要同时获取多个锁的代码，必须按照相同的全局顺序获取。

如果 thread1 和 thread2 都先锁 A 再锁 B，死锁就不可能发生。因为只有一个线程能先拿到 A，另一个会在 A 上阻塞，它不会持有 B——所以不存在循环等待。

### 总顺序（Total Order）

最简单的锁顺序策略是建立一个全局的总顺序——给所有 mutex 编号，任何代码在获取多个 mutex 时必须按编号从小到大获取。这就像餐厅里大家排队取餐一样——所有人都排同一条队，不会出现两个人互相挡路的情况。

```cpp
#include <mutex>
#include <thread>
#include <iostream>

// 全局约定：先锁 account_a（ID 较小），再锁 account_b（ID 较大）
std::mutex account_a_mtx;  // "编号" 1
std::mutex account_b_mtx;  // "编号" 2

void transfer_a_to_b(int amount)
{
    std::lock_guard<std::mutex> lock_a(account_a_mtx);  // 先锁 "编号" 小的
    std::lock_guard<std::mutex> lock_b(account_b_mtx);  // 再锁 "编号" 大的
    // 执行转账...
    std::cout << "Transferred " << amount << " from A to B\n";
}

void transfer_b_to_a(int amount)
{
    std::lock_guard<std::mutex> lock_a(account_a_mtx);  // 依然是先锁 "编号" 小的！
    std::lock_guard<std::mutex> lock_b(account_b_mtx);  // 再锁 "编号" 大的
    // 执行反向转账...
    std::cout << "Transferred " << amount << " from B to A\n";
}
```

注意 `transfer_b_to_a` 的逻辑虽然是"B 转到 A"，但加锁顺序依然是先 A 后 B——方向不重要，顺序才重要。

### 比较地址：当编号不可行时

在动态创建 mutex 的场景下（比如每个对象各有一把自己的锁），你没法给所有 mutex 编一个全局的编号。这时候一个常用的技巧是比较 mutex 的地址——地址低的先锁，地址高的后锁：

```cpp
#include <mutex>
#include <iostream>

class Account {
public:
    explicit Account(int balance) : balance_(balance) {}

    static void transfer(Account& from, Account& to, int amount)
    {
        // 按地址排序加锁，保证全局一致的顺序
        if (&from < &to) {
            from.mtx_.lock();
            to.mtx_.lock();
        } else {
            to.mtx_.lock();
            from.mtx_.lock();
        }

        // 用 adopt_lock 把已获取的锁交给 RAII 守卫管理
        std::lock_guard<std::mutex> lock_from(from.mtx_, std::adopt_lock);
        std::lock_guard<std::mutex> lock_to(to.mtx_, std::adopt_lock);

        from.balance_ -= amount;
        to.balance_ += amount;
    }

    int balance() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return balance_;
    }

private:
    mutable std::mutex mtx_;
    int balance_;
};

int main()
{
    Account a(1000);
    Account b(2000);

    // 两个方向的转账都不会死锁
    Account::transfer(a, b, 100);
    Account::transfer(b, a, 50);

    std::cout << "A: " << a.balance() << ", B: " << b.balance() << "\n";
    return 0;
}
```

这里有一个细节值得注意：我们先手动 `lock()` 了两个 mutex，然后用 `std::adopt_lock` 把它们交给 `lock_guard` 管理。这个模式在 C++11/14 时代是获取多锁的标准做法——先通过某种死锁避免策略（这里是比较地址）手动获取锁，再用 `adopt_lock` 保证异常安全。如果你有 C++17，直接用 `std::scoped_lock` 就行了——它内部自动处理。

## std::lock() 与 std::try_lock()：标准库的多锁工具

C++11 提供了两个用于同时获取多个锁的函数：`std::lock()` 和 `std::try_lock()`。

### std::lock()：阻塞式多锁获取

`std::lock()` 接受任意数量的 `Lockable` 对象，使用死锁避免算法一次性获取所有锁。它的保证是：要么所有锁都获取成功，要么抛出异常并释放已获取的锁。标准没有规定具体的算法实现，但主流实现都采用 `try_lock` 回退策略——反复尝试按不同顺序 `try_lock`，如果某个失败就释放已获取的锁重试。

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void safe_swap(std::vector<int>& data_a, std::vector<int>& data_b)
{
    // 先构造 defer_lock 的 unique_lock，不实际加锁
    std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
    std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);

    // std::lock 一次性安全获取所有锁
    std::lock(lock_a, lock_b);

    // 现在两把锁都已获取，可以安全操作
    data_a.swap(data_b);
}
```

这个 `defer_lock` + `std::lock()` + `unique_lock` 的组合是 C++11/14 时代获取多锁的标准模式。它的好处是 `unique_lock` 的析构函数会正确释放已获取的锁，即使中间抛出异常也能保证异常安全。

### std::try_lock()：非阻塞式多锁获取

`std::try_lock()` 是非阻塞版本——它尝试获取所有锁，如果全部成功返回 `-1`，如果某个获取失败就立刻释放已获取的锁并返回失败的索引（从 0 开始）。`std::try_lock()` 不会重试——它只做一轮尝试：

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void try_swap(bool& success)
{
    int result = std::try_lock(mtx_a, mtx_b);
    if (result == -1) {
        // 所有锁都获取成功
        std::lock_guard<std::mutex> lock_a(mtx_a, std::adopt_lock);
        std::lock_guard<std::mutex> lock_b(mtx_b, std::adopt_lock);

        // 执行操作...
        success = true;
    } else {
        // 第 result 个锁获取失败
        std::cout << "Failed to acquire lock at index " << result << "\n";
        success = false;
        // 可以做降级处理或者稍后重试
    }
}
```

`std::try_lock()` 适用于"拿不到就算了"的场景——比如你有一个备用方案，不需要非等到锁不可。它也适用于实现自定义的回退策略——比如结合指数退避的重试机制。

## std::scoped_lock（C++17）：多锁获取的最佳实践

如果你有 C++17 可用，`std::scoped_lock` 是获取多个锁的最佳选择。它把 `defer_lock` + `std::lock()` + `unique_lock` 的三步操作压缩成了一行：

```cpp
#include <mutex>
#include <iostream>
#include <vector>

std::mutex mtx_a;
std::mutex mtx_b;
std::vector<int> data_a;
std::vector<int> data_b;

void modern_safe_swap()
{
    std::scoped_lock lock(mtx_a, mtx_b);  // 一行搞定：安全获取 + RAII 管理
    data_a.swap(data_b);
}
```

`scoped_lock` 的构造函数内部调用 `std::lock()` 的死锁避免算法获取所有 mutex，析构时按相反顺序释放。它也可以接受单个 mutex——这时行为等同于 `lock_guard`，但为了代码清晰度，单锁还是推荐用 `lock_guard`。

如果你回头去看之前的"比较地址"示例，用 `scoped_lock` 重写后代码会简洁得多：

```cpp
class Account {
public:
    explicit Account(int balance) : balance_(balance) {}

    static void transfer(Account& from, Account& to, int amount)
    {
        // scoped_lock 内部自动处理死锁避免，不需要手动比较地址
        std::scoped_lock lock(from.mtx_, to.mtx_);

        from.balance_ -= amount;
        to.balance_ += amount;
    }

private:
    mutable std::mutex mtx_;
    int balance_;
};
```

注意我们甚至不需要手动比较地址了——`scoped_lock` 内部的死锁避免算法会处理。当然，如果你知道锁的全局顺序，按顺序传给 `scoped_lock` 会获得更好的性能（因为内部的 `try_lock` 回退次数更少）。但即使顺序不一致，`scoped_lock` 也不会死锁。

## try_lock 回退模式：当顺序无法建立时

有些场景下，全局锁顺序确实没法建立——比如你有一个回调系统，回调函数可能会获取任意的锁，而你没法控制回调函数的加锁顺序。这时候 `try_lock` 回退模式就是一个实用的选择。

核心思路是：先尝试获取所有需要的锁，如果失败了，释放已获取的锁，等一小段时间再重试。由于线程不会在持有一把锁的情况下去阻塞等待另一把锁，"持有并等待"条件被打破了：

```cpp
#include <mutex>
#include <thread>
#include <chrono>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void try_lock_with_backoff()
{
    while (true) {
        // 尝试获取第一把锁
        std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
        if (!lock_a.try_lock()) {
            std::this_thread::yield();
            continue;
        }

        // 持有第一把锁，尝试获取第二把
        std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);
        if (!lock_b.try_lock()) {
            // 获取第二把失败，释放第一把，回退
            lock_a.unlock();
            std::this_thread::yield();
            continue;
        }

        // 两把锁都拿到了
        break;
    }

    // 临界区...
}
```

这个模式的关键是：一旦 `try_lock` 失败，立刻释放已持有的所有锁。这意味着线程不会在持有一把锁的情况下去阻塞等待另一把锁——"持有并等待"条件被打破了。`yield()` 的作用是让出 CPU 时间片，避免忙等浪费。在实际工程中，你还可以用指数退避（exponential backoff）来减少竞争。

当然，如果你的项目能用 C++17，直接用 `scoped_lock` 就行了——它内部做的就是这件事。

## 层级锁：按角色/级别加锁

层级锁（Hierarchical Lock）是一种更结构化的锁顺序策略，核心思想是给每个 mutex 分配一个层级编号，规定线程只能从低层级向高层级获取锁——如果当前线程已经持有了层级为 N 的锁，它就不能再去获取层级低于 N 的锁。违反这个规则就是编程错误，可以在运行时检测到。

这个策略的优势在于它把锁顺序的约束显式化了——不再依赖开发者的记忆和文档，而是通过代码本身来强制执行。我们来看一个简化版的实现：

```cpp
#include <mutex>
#include <stdexcept>
#include <thread>
#include <limits>

class HierarchicalMutex {
public:
    explicit HierarchicalMutex(unsigned long level)
        : hierarchy_level_(level)
    {}

    void lock()
    {
        check_for_hierarchy_violation();
        internal_mutex_.lock();
        update_previous_level();
    }

    void unlock()
    {
        this_thread_hierarchy_level_ = previous_level_;
        internal_mutex_.unlock();
    }

    bool try_lock()
    {
        check_for_hierarchy_violation();
        if (!internal_mutex_.try_lock()) {
            return false;
        }
        update_previous_level();
        return true;
    }

private:
    void check_for_hierarchy_violation()
    {
        if (hierarchy_level_ >= this_thread_hierarchy_level_) {
            throw std::logic_error("Mutex hierarchy violated");
        }
    }

    void update_previous_level()
    {
        previous_level_ = this_thread_hierarchy_level_;
        this_thread_hierarchy_level_ = hierarchy_level_;
    }

    std::mutex internal_mutex_;
    unsigned long const hierarchy_level_;
    unsigned long previous_level_;
    static thread_local unsigned long this_thread_hierarchy_level_;
};

thread_local unsigned long HierarchicalMutex::this_thread_hierarchy_level_
    = std::numeric_limits<unsigned long>::max();
```

使用时，给不同模块的 mutex 分配不同的层级：

```cpp
HierarchicalMutex high_level_mutex(10000);    // 高层：应用层
HierarchicalMutex mid_level_mutex(5000);      // 中层：业务逻辑
HierarchicalMutex low_level_mutex(100);       // 低层：底层 IO

void high_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(high_level_mutex);
    // 允许：10000 > 5000，可以向更低层级获取
    mid_level_operation();
}

void mid_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(mid_level_mutex);
    // 允许：5000 > 100
    low_level_operation();
}

void low_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(low_level_mutex);
    // 如果在这里尝试获取 mid_level_mutex，会抛异常！
    // 因为 100 < 5000，违反了层级约束
}
```

层级锁的精妙之处在于它用 `thread_local` 变量追踪每个线程当前的锁层级，在 `lock()` 时检查是否违反了层级约束。如果违反了，直接抛出异常——这意味着你可以在开发和测试阶段就捕获到锁顺序违规，而不是等到生产环境出现死锁了才发现问题。这个策略的代价是每次 `lock()` 和 `unlock()` 都有额外的检查开销，但对于大多数应用来说这个开销完全可以接受。

## 持锁时避免回调

这是另一个容易被忽视的死锁风险来源。如果你的代码在持有锁的情况下调用了一个回调函数、虚函数、或者任何你无法控制其实现的函数，你就把锁的安全交给了别人的代码。回调函数可能会做任何事情——包括获取其他锁。

```cpp
#include <mutex>
#include <functional>
#include <iostream>

std::mutex data_mtx;

class EventSystem {
public:
    void on_data_update(std::function<void(int)> callback)
    {
        std::lock_guard<std::mutex> lock(data_mtx);  // 持锁
        int value = get_latest_value();
        callback(value);  // 危险！回调可能获取其他锁
    }

private:
    int get_latest_value() { return 42; }
};
```

如果 `callback` 内部也获取了某把锁，而那个锁的持有者又在等 `data_mtx`，死锁就形成了。更隐蔽的是，回调函数的实现可能在今天不会获取任何锁，但半年后有人改了它的实现——boom，死锁从天而降。

安全的做法是把回调调用挪到锁外面：先在锁的保护下拷贝需要的数据，然后释放锁，最后在锁外调用回调：

```cpp
class EventSystem {
public:
    void on_data_update(std::function<void(int)> callback)
    {
        int value;
        {
            std::lock_guard<std::mutex> lock(data_mtx);
            value = get_latest_value();
        }  // 锁在这里释放
        callback(value);  // 安全：不在持锁状态下调用回调
    }

private:
    int get_latest_value() { return 42; }
};
```

这个原则可以推广为一个通用规则：**在持锁期间，只操作你完全能控制的代码和数据**。任何外部接口——回调、虚函数、I/O 操作、甚至 `std::cout`——都不应该在持锁时调用。这不仅是为了防死锁，也是为了减小临界区的长度，提高并发度。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 练习

### 练习 1：复现并修复死锁

编译运行本篇开头提供的 AB-BA 死锁示例，确认程序会卡住（如果一次没卡住，多试几次）。然后用 `std::scoped_lock` 替换两个 `lock_guard`，确认程序能正常退出。再尝试用锁顺序策略（统一先锁 A 再锁 B）修复，同样确认不死锁。

### 练习 2：实现层级锁并测试

基于本篇提供的 `HierarchicalMutex` 实现，编写一个测试程序：创建三个不同层级的 mutex，在正确的层级顺序下（从高到低）获取它们，确认不抛异常；然后故意违反层级顺序（从低到高获取），确认抛出 `std::logic_error`。提示：你需要 `#include <limits>` 来获取 `std::numeric_limits`。

### 练习 3：哲学家就餐问题

经典的哲学家就餐问题：5 个哲学家围坐一桌，每人左手边有一根筷子，需要同时拿起左右两根筷子才能吃饭。朴素实现中每个哲学家先拿左筷子再拿右筷子——5 个哲学家同时拿起左手的筷子，然后都在等右手的筷子（被右边的人拿着），死锁。用本篇学到的策略（锁顺序或 `scoped_lock`）修复这个死锁。

## 参考资源

- [std::lock -- cppreference](https://en.cppreference.com/w/cpp/thread/lock)
- [std::try_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/try_lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [C++ Core Guidelines: CP.21 -- Use lock() and unlock() with care](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp21-use-lock-and-unlock-with-care)
- [C++ Core Guidelines: CP.22 -- Never call unknown code while holding a lock](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp22-never-call-unknown-code-while-holding-a-lock-cp22)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 3](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
- [System Deadlocks -- Coffman, Elphick, Shoshani (1971)](https://doi.org/10.1145/356586.356588)
