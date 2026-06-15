---
title: "mutex 与 RAII 锁"
description: "系统梳理 mutex 家族与 RAII 锁守卫，从 lock_guard 到 scoped_lock 的演进与最佳实践"
chapter: 2
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
  - RAII守卫
difficulty: intermediate
platform: host
reading_time_minutes: 22
cpp_standard: [11, 14, 17, 20]
prerequisites:
  - "线程所有权与 RAII"
related:
  - "死锁与锁顺序"
  - "condition_variable 与等待语义"
---

# mutex 与 RAII 锁

上一篇我们聊了线程所有权与 RAII，掌握了 `std::thread` 的生命周期管理和基于作用域的资源控制思路。现在问题来了：有了线程，线程之间怎么安全地共享数据？我们在并发基本问题那一篇里已经见过 data race 的威力了——两个线程同时写一个 `int`，结果就能从 2000000 跑成 1345687。解决 data race 最通用的手段就是互斥量（mutex），而 C++ 标准库为我们准备了一整个 mutex 家族和配套的 RAII 锁守卫。

这一篇我们要做的事情很明确：先把 mutex 家族的四个成员——`std::mutex`、`std::recursive_mutex`、`std::timed_mutex`、`std::recursive_timed_mutex`——逐个过一遍，搞清楚各自解决什么问题；然后再系统梳理三个 RAII 锁守卫——`lock_guard`、`unique_lock`、`scoped_lock`——它们是真正应该出现在我们日常代码里的工具。整个过程中我们会反复强调一条原则：绝对不要手动调用 `lock()` 和 `unlock()`。

## std::mutex：最基础的互斥量

`std::mutex` 是 C++11 引入的标准互斥量，定义在 `<mutex>` 头文件中。它只提供三个操作：`lock()`、`unlock()` 和 `try_lock()`。

`lock()` 是阻塞调用——如果 mutex 已经被其他线程持有，当前线程会阻塞等待，直到获取到锁为止。`unlock()` 释放锁。`try_lock()` 是非阻塞版本——尝试获取锁，成功返回 `true`，失败返回 `false`，不会等待。这三个操作就是 mutex 的全部接口，简单到令人发指。

先别急着觉得简单就没坑。看看下面这段"手工作坊"式的代码：

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx;
int shared_counter = 0;

void bad_increment()
{
    mtx.lock();              // 手动加锁
    shared_counter++;
    // 如果这里抛出异常... unlock 永远不会执行
    mtx.unlock();            // 手动解锁
}
```

这段代码在正常路径下能工作，但它有几个致命的隐患。如果 `shared_counter++` 和 `mtx.unlock()` 之间有任何异常抛出（当然，`int` 自增不会抛异常，但把 `int` 换成复杂类型，或者中间穿插了其他可能抛异常的操作呢？），`unlock()` 就永远不会被执行。锁不释放，其他等待这把锁的线程全部阻塞——这不是死锁，但效果差不多，而且更难排查，因为程序没有卡死在某个明显的循环等待上，而是"莫名其妙"地停住了。

更糟糕的情况是多个 return 路径。如果你的临界区中间有三四个 `if-return` 分支，每个分支前面都得写 `mtx.unlock()`，漏一个就是 bug。在大型代码库里，这种"手动配对 lock/unlock"的模式几乎不可能保证正确性。

还有一个经典的坑：同一把锁被同一个线程加两次。`std::mutex` 不允许同一线程重复加锁——如果你在持有锁的情况下调用 `lock()`，结果是未定义行为（大多数实现会直接死锁）。这在函数调用链复杂的时候很容易不知不觉地踩上去：

```cpp
std::mutex mtx;

void function_a()
{
    mtx.lock();
    function_b();    // function_b 内部也锁了同一把 mutex
    mtx.unlock();
}

void function_b()
{
    mtx.lock();      // 死锁！同一线程对 std::mutex 重复加锁
    // ...
    mtx.unlock();
}
```

所以结论很清楚：`std::mutex` 的直接接口不应该出现在应用代码中。它的设计初衷是作为 RAII 封装的底层基石，而不是让你天天 `lock()`/`unlock()` 的。

## std::recursive_mutex：允许同线程重复加锁

`std::recursive_mutex` 解决了上面提到的"同线程重复加锁"问题。它内部维护一个锁计数器——同一线程第一次 `lock()` 计数器变为 1，第二次变为 2，依此类推；每次 `unlock()` 计数器减 1，减到 0 时才真正释放锁。

```cpp
#include <mutex>
#include <iostream>

std::recursive_mutex rmtx;

void recursive_function(int depth)
{
    std::lock_guard<std::recursive_mutex> lock(rmtx);
    std::cout << "depth = " << depth << "\n";
    if (depth > 0) {
        recursive_function(depth - 1);  // 递归调用，再次加锁
    }
}

int main()
{
    recursive_function(5);
    return 0;
}
```

这段代码完全合法——`recursive_mutex` 允许同一线程多次加锁，每次递归调用都会增加计数器，每次 return 都会触发 `lock_guard` 的析构减少计数器，直到最外层函数返回时锁才真正释放。

但是，`recursive_mutex` 通常是一种设计气味的信号。如果你需要递归锁，大概率是因为你的接口设计把"需要在锁保护下调用的函数"和"不需要锁的内部实现"混在了一起。更好的做法是把"在锁保护下的操作"提取成一个不加锁的内部函数，让外层接口负责加锁。递归锁是一根拐杖，能帮你走，但最好别依赖它。

## std::timed_mutex：带超时的互斥量

`std::timed_mutex` 在 `std::mutex` 的基础上增加了两个带超时的加锁操作：`try_lock_for()` 和 `try_lock_until()`。

`try_lock_for()` 接受一个时间段（`std::chrono::duration`），在指定时间内反复尝试获取锁，超时返回 `false`。`try_lock_until()` 接受一个绝对时间点（`std::chrono::time_point`），在指定时刻之前尝试获取锁，超时返回 `false`。两者的区别类似于"等最多 100 毫秒"和"等到下午 3 点"。

```cpp
#include <mutex>
#include <chrono>
#include <iostream>

std::timed_mutex tmtx;

void try_with_timeout()
{
    if (tmtx.try_lock_for(std::chrono::milliseconds(100))) {
        // 成功获取锁
        std::cout << "Lock acquired within 100ms\n";
        // ... 临界区操作 ...
        tmtx.unlock();
    } else {
        // 超时，锁获取失败
        std::cout << "Failed to acquire lock within 100ms\n";
        // 可以做降级处理、记录日志、或者稍后重试
    }
}
```

`std::recursive_timed_mutex` 是递归锁和超时锁的结合体——同一线程可以多次加锁，同时支持 `try_lock_for()` 和 `try_lock_until()`。实际工程中使用频率很低，知道有这么个东西就行。

这里要提醒一点：带超时的锁在某些平台上开销更大，因为它需要和系统时钟交互。如果你的场景不需要超时能力，用普通的 `std::mutex` 就够了。不要因为"万一能用上"就默认选 `timed_mutex`。

## std::lock_guard：最简单的 RAII 包装器

终于到了我们真正该用的工具。`std::lock_guard` 是 C++11 引入的最轻量的 RAII 锁守卫——构造时调用 `lock()`，析构时调用 `unlock()`，就这样。它不接受 `defer_lock`，没有 `unlock()` 方法，不支持移动——什么额外能力都没有，但正是这种极简设计保证了你用不出错。

```cpp
#include <mutex>
#include <iostream>
#include <vector>

std::mutex mtx;
std::vector<int> shared_data;

void safe_push(int value)
{
    std::lock_guard<std::mutex> lock(mtx);  // 构造时自动 lock
    shared_data.push_back(value);
    // 无论正常返回、异常抛出、还是 early return，析构时都会 unlock
}
```

注意一个新手常犯的错误——忘记给 `lock_guard` 变量命名：

```cpp
void bad_push(int value)
{
    std::lock_guard<std::mutex>(mtx);  // 临时对象！立刻析构！
    shared_data.push_back(value);      // 没有锁保护
}

void good_push(int value)
{
    std::lock_guard<std::mutex> lock(mtx);  // lock 有名字，生命周期是整个作用域
    shared_data.push_back(value);
}
```

无名的临时对象在语句结束时立刻析构——锁刚加上就释放了，等于没加。编译器通常不会对这种情况发出警告，所以一定要记住给锁对象起名字。

`lock_guard` 有一个不太常用但值得了解的构造选项：`std::adopt_lock`。它告诉 `lock_guard`："锁已经被当前线程持有了，你只管在析构时释放，不要再 lock"。这个选项主要用于配合 `std::lock()` 函数——先通过 `std::lock()` 同时获取多个锁，再用 `adopt_lock` 把它们交给 `lock_guard` 管理。我们会在下一篇讲死锁预防时看到具体用法。

## std::unique_lock：灵活但不沉重的瑞士军刀

如果 `lock_guard` 是一把可靠的螺丝刀，`std::unique_lock` 就是一把瑞士军刀。它在 `lock_guard` 的基础上增加了几个关键能力：延迟锁定、手动解锁、锁所有权转移，以及与条件变量的配合。当然，多出来的能力也意味着多出来的状态——`unique_lock` 内部需要额外存储"是否持有锁"的标记，开销比 `lock_guard` 稍微大一点，但在绝大多数场景下这个差异可以忽略不计。

### 基本用法：和 lock_guard 一样简单

```cpp
#include <mutex>

std::mutex mtx;

void basic_unique_lock()
{
    std::unique_lock<std::mutex> lock(mtx);  // 构造时加锁，析构时解锁
    // 临界区...
}
```

最基本的用法跟 `lock_guard` 完全一样，构造即加锁，析构即解锁。

### 延迟锁定：defer_lock

`std::defer_lock` 告诉 `unique_lock` 在构造时不要加锁，稍后由我们来决定什么时候加。这在"条件性加锁"的场景下很有用——不是所有代码路径都需要锁，但你希望在需要锁的路径上享受 RAII 的保护：

```cpp
#include <mutex>

std::mutex mtx;
bool needs_sync = true;  // 假设由外部条件决定

void conditional_lock()
{
    std::unique_lock<std::mutex> lock(mtx, std::defer_lock);  // 构造时不加锁

    if (needs_sync) {
        lock.lock();  // 按需加锁
    }

    // ... 无论加没加锁，析构时都能正确处理
}
```

`defer_lock` 更常见的用途是配合 `std::lock()` 实现多锁的安全获取——先构造两个 `defer_lock` 的 `unique_lock`，再用 `std::lock()` 同时锁定它们。这个模式在下一篇会详细展开。

### 提前解锁：减小临界区

`unique_lock` 允许你在作用域结束之前手动调用 `unlock()`——这在需要缩小临界区的场景下很有价值。锁持有时间越短，其他线程的等待时间就越短，并发度就越高：

```cpp
#include <mutex>
#include <vector>
#include <fstream>

std::mutex mtx;
std::vector<int> shared_data;

void process_and_save()
{
    std::unique_lock<std::mutex> lock(mtx);

    // 在锁的保护下拷贝数据
    auto snapshot = shared_data;

    lock.unlock();  // 临界区结束，提前解锁

    // 在锁外做耗时操作——不会阻塞其他线程
    for (auto& v : snapshot) {
        v *= 2;
    }

    // 保存到文件也是锁外的操作
    std::ofstream ofs("output.txt");
    for (int v : snapshot) {
        ofs << v << "\n";
    }
}
```

这个例子展示了一个重要的模式：在锁的保护下快速完成必要的数据拷贝，然后立刻释放锁，后续的处理在锁外进行。`lock_guard` 做不到提前解锁——它的设计哲学就是"锁的生命周期等于作用域的生命周期"，没有任何例外。

### 与条件变量配合

这是 `unique_lock` 最不可替代的场景。`std::condition_variable` 的 `wait()` 系列函数要求传入 `std::unique_lock<std::mutex>`，不能用 `lock_guard`。原因在于条件变量的工作机制：线程在等待时必须先释放锁（让其他线程能进入临界区修改条件），被唤醒时又要重新获取锁。`unique_lock` 提供的"解锁-再加锁"能力正是条件变量所需要的。

```cpp
#include <mutex>
#include <condition_variable>
#include <queue>
#include <iostream>

template<typename T>
class ThreadSafeQueue {
public:
    void push(const T& value)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(value);
        }
        cv_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mtx_);  // 必须用 unique_lock
        cv_.wait(lock, [this] { return !queue_.empty(); });
        // wait 内部：条件不满足 -> unlock -> 等待 -> 被唤醒 -> re-lock -> 检查条件

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};
```

如果你尝试把 `pop()` 里的 `unique_lock` 换成 `lock_guard`，编译都过不了——`condition_variable::wait()` 的签名要求的就是 `unique_lock`。

### 锁所有权转移

`unique_lock` 支持移动语义，可以在函数之间传递锁的所有权。这在某些架构设计中很有用——比如一个函数负责获取锁并做一些初始化工作，然后把锁的所有权转移给调用者，由调用者负责后续的临界区操作和最终的解锁：

```cpp
#include <mutex>

std::mutex mtx;

std::unique_lock<std::mutex> acquire_and_initialize()
{
    std::unique_lock<std::mutex> lock(mtx);
    // 做一些需要锁保护的初始化工作
    prepare_shared_state();
    return lock;  // NRVO 或移动返回，锁的所有权转移给调用者
}

void use_lock()
{
    std::unique_lock<std::mutex> lock = acquire_and_initialize();
    // lock 持有锁，可以在临界区操作
    modify_shared_state();
    // lock 离开作用域时自动解锁
}
```

注意 `lock_guard` 不支持移动——它的拷贝构造和移动构造都是删除的。如果你需要转移锁的所有权，`unique_lock` 是唯一选择。

## std::scoped_lock：C++17 的多锁死锁预防

`std::scoped_lock` 是 C++17 引入的 RAII 锁守卫，专门为多锁场景设计。它的构造函数可以接受任意数量的 mutex（当然也接受单个 mutex），内部使用 `std::lock()` 提供的死锁避免算法来一次性获取所有锁，析构时按相反顺序释放。

这个特性解决了一个非常现实的问题。假设有两个线程需要同时操作两个被不同 mutex 保护的数据结构，最朴素的做法是嵌套使用 `lock_guard`：

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);  // 先锁 A
    std::cout << "thread1: locked A\n";
    std::lock_guard<std::mutex> lock_b(mtx_b);  // 再锁 B
    std::cout << "thread1: locked both\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);  // 先锁 B
    std::cout << "thread2: locked B\n";
    std::lock_guard<std::mutex> lock_a(mtx_a);  // 再锁 A
    std::cout << "thread2: locked both\n";
}
```

如果 thread1 拿到 `mtx_a` 的同时 thread2 拿到 `mtx_b`，双方就卡住了——经典的 AB-BA 死锁。`scoped_lock` 用一行代码解决：

```cpp
void safe_thread()
{
    std::scoped_lock lock(mtx_a, mtx_b);  // 一次性安全获取两把锁
    // 临界区...
}
```

`scoped_lock` 内部的死锁避免算法基于 `try_lock` 回退策略：尝试按某种顺序获取所有锁，如果某个 `try_lock` 失败，就释放已经获取的锁，换个顺序重试。这个算法打破了死锁四个必要条件中的"持有并等待"——如果获取失败，已持有的锁会被释放，不存在"持有一把等另一把"的局面。

`scoped_lock` 也能用于单个 mutex 的情况，这时它等价于 `lock_guard`。但为了代码意图的清晰性，单锁场景还是推荐用 `lock_guard`——看到 `lock_guard` 就知道只有一个锁，看到 `scoped_lock` 就知道可能涉及多锁，这对阅读代码的人来说是有价值的信息。

## lock_guard vs unique_lock vs scoped_lock：选择指南

我们把三个 RAII 锁守卫的核心差异放在一起比较，帮助你在实际开发中快速做出选择。

`lock_guard` 的设计哲学是"简单即美"。它不可复制、不可移动、不能提前解锁、不能延迟加锁——这些"限制"恰恰是它的优势，因为限制越多，用出错的空间就越小。90% 的日常场景下 `lock_guard` 就够了：进入函数、构造 `lock_guard`、操作共享数据、函数返回、`lock_guard` 析构释放锁。整个流程一条直线，没有分叉。

`unique_lock` 适合那 10% 需要额外灵活性的场景。最典型的是配合条件变量——这是 `unique_lock` 不可替代的核心场景。其次是"先拷贝数据，再提前解锁"的模式——把耗时操作挪到锁外面做，减少锁的持有时间。还有延迟加锁和锁所有权转移，这些在更复杂的架构设计中会用到。

`scoped_lock` 的核心价值是多锁获取的死锁预防。只要你的代码需要同时持有两把或更多锁，就应该用 `scoped_lock`。如果项目已经采用了 C++17，单锁场景用 `scoped_lock` 也完全没问题——但团队约定上，区分 `lock_guard`（单锁）和 `scoped_lock`（多锁）有助于代码的可读性和可维护性。

## 工程原则：绝对不要手动调用 lock()/unlock()

我们花了一整篇文章讨论 mutex 家族和 RAII 锁守卫，最后要强调的核心原则只有一条：绝对不要在应用代码中直接调用 `mutex.lock()` 和 `mutex.unlock()`。原因我们在前面已经反复看到了——手动管理 lock/unlock 在异常路径、多 return 路径、嵌套调用等场景下几乎不可能保证正确性，而 RAII 锁守卫通过将锁的生命周期绑定到作用域，从根本上消除了这一整类 bug。

这条原则在 C++ Core Guidelines 中被明确记录为 CP.20："Use RAII, never plain `lock()`/`unlock()`"。唯一的例外是 `adopt_lock`——它接受一个已经被锁住的 mutex，只负责在析构时解锁。但即使在这种情况下，加锁的动作也应该是通过 `std::lock()` 或者其他安全机制完成的，而不是手动调用 `mutex.lock()`。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 在线运行

在线体验 lock_guard、unique_lock + condition_variable 和 scoped_lock 三种 RAII 锁守卫：

<OnlineCompilerDemo
  title="mutex 与 RAII 锁"
  source-path="code/examples/vol5/10_mutex_raii.cpp"
  description="体验 lock_guard 计数、unique_lock+CV 生产消费队列和 scoped_lock 多锁安全交换"
  allow-run
/>

## 练习

### 练习 1：为 stack 实现线程安全包装器

给定一个 `std::stack<int>`，用 `std::mutex` 和 `std::lock_guard` 为它实现一个线程安全的包装器。要求提供 `push()`、`pop()`（返回 `std::optional<int>`，空栈时返回 `std::nullopt`）、`top()`（同样返回 `optional`）和 `empty()` 四个接口。提示：注意 `pop()` 和 `top()` 不能返回引用——因为在解锁之后调用者再去访问引用就无效了。

### 练习 2：比较 lock_guard 和 unique_lock 的性能

编写一个简单的基准测试：用 4 个线程各递增一个共享计数器 1000000 次，分别用 `lock_guard` 和 `unique_lock` 保护。对比两者的运行时间——你会发现差异通常在噪声范围内，但在极端场景下 `unique_lock` 的额外状态维护可能体现为可测量的开销。思考：在什么条件下这个差异会变得显著？

### 练习 3：用 scoped_lock 安全地交换两个被保护的数据

假设有两个 `std::vector<int>`，各自被一个 `std::mutex` 保护。编写一个 `swap_contents()` 函数，用 `std::scoped_lock` 同时获取两把锁，然后交换两个 vector 的内容。验证在多线程环境下反复调用这个函数不会死锁。

## 参考资源

- [std::mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/mutex)
- [std::recursive_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/recursive_mutex)
- [std::timed_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/timed_mutex)
- [std::lock_guard -- cppreference](https://en.cppreference.com/w/cpp/thread/lock_guard)
- [std::unique_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/unique_lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [C++ Core Guidelines: CP.20 -- Use RAII, never plain lock()/unlock()](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
