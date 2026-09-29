---
title: "atomic_wait 与 atomic_ref"
description: "C++20 新增的等待/通知机制与原子引用，构建比忙等更轻量的同步原语"
chapter: 3
order: 4
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
difficulty: advanced
platform: host
reading_time_minutes: 20
cpp_standard: [20]
prerequisites:
  - "内存序详解"
related:
  - "fence 与编译器屏障"
  - "原子操作模式"
---

# atomic_wait 与 atomic_ref

前几篇我们一直在聊 `std::atomic` 的操作集、内存序、fence——这些都是"怎么安全地读写共享变量"的工具。但有一个场景我们一直没有涉及：线程 A 修改了某个原子变量，线程 B 需要等这个变化发生才能继续。在 C++20 之前，我们只有两条路可以走——要么用忙等（spin loop，反复 `load()` 检查），要么引入重量级的 mutex + condition_variable。前者浪费 CPU，后者即便在无竞争的情况下也有微秒级的上下文切换开销。

C++20 给出了第三条路：`std::atomic<T>::wait()`、`notify_one()` 和 `notify_all()`。这三个成员函数让原子变量本身具备了"等待/通知"能力——线程可以直接在原子变量上阻塞，直到其他线程修改了值并发出通知。在 Linux 上它底层用 futex 实现，在 Windows 上用 WaitOnAddress，延迟比 condition_variable 低一个数量级。

这一篇我们先完整拆解 `wait/notify` 的语义和底层机制，然后看 C++20 同时引入的另一个利器 `std::atomic_ref<T>`——它让你对已有的非原子变量施加原子操作，而不需要改变变量的类型。最后我们用这两个工具一起搭一个二进制信号量，看看它们在实践中怎么配合。

## wait/notify：原子变量的"自带 condition variable"

### 基本接口

`std::atomic<T>` 在 C++20 中新增了三个成员函数：

```cpp
// 阻塞当前线程，直到 notify_one/notify_all 被调用，
// 且当前值与 old_value 不同（伪唤醒也会返回）
void wait(T old_value,
          std::memory_order order = std::memory_order_seq_cst) const noexcept;

// 唤醒一个在 *this 上等待的线程（如果有）
void notify_one() noexcept;

// 唤醒所有在 *this 上等待的线程
void notify_all() noexcept;
```

我们先看一个最简单的使用场景——主线程等待工作线程完成初始化：

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> ready{false};

void worker()
{
    std::cout << "Worker: initializing...\n";
    // ... 做一些初始化工作 ...
    std::cout << "Worker: ready!\n";
    ready.store(true, std::memory_order_release);
    ready.notify_one();  // 通知等待的线程
}

int main()
{
    std::thread t(worker);

    // 在 ready 上等待，直到它不再是 false
    ready.wait(false, std::memory_order_acquire);
    std::cout << "Main: worker is ready, continuing\n";

    t.join();
    return 0;
}
```

这段代码的流程非常直观：主线程调用 `ready.wait(false)`，意思是"如果 `ready` 当前值是 `false`，就阻塞；如果已经是 `true`，立即返回"。工作线程完成初始化后，先 `store(true, release)`，再调用 `notify_one()` 唤醒主线程。

### wait 的值语义：一种容易误解的设计

`wait()` 的参数 `old_value` 是一个"我期望的旧值"，而不是"我等待的目标值"。这个设计经常让初学者困惑——明明我在等它变成 `true`，为什么要传 `false`？

原因是 `wait()` 的内部逻辑是这样的：首先原子地 `load()` 当前值，然后拿它跟 `old_value` 比较。如果相等，线程就进入阻塞状态；如果不等，直接返回。这个设计有一个关键优势：**避免了 TOCTOU（Time of Check to Time of Use）竞态**。试想如果接口设计成 `wait_until_equals(target)`——线程先 load 一下发现不等于 target，准备阻塞——但就在这个间隙里，另一个线程把值改成了 target。线程还是去阻塞了，结果没人会再通知它，死锁。

用 `old_value` 的设计天然避免了这个问题。`wait()` 内部把"比较值"和"决定阻塞"合并成一个原子操作——如果在 load 和阻塞之间值发生了变化，线程不会真的阻塞，而是立即发现值已经不等了，直接返回。这是一种 lock-free 的"比较-等待"模式，跟 Linux futex 的 `FUTEX_WAIT` 系统调用的语义完全一致——这不是巧合，因为 C++ 的 `wait()` 就是直接映射到 futex 的。

另外还需要注意，`wait()` 允许伪唤醒（spurious wakeup）——即使没有人调用 `notify`，`wait()` 也可能返回。所以 `wait()` 通常要放在一个循环中使用：

```cpp
while (flag.load(std::memory_order_acquire) == false) {
    flag.wait(false, std::memory_order_acquire);
}
```

但在很多场景下，如果值已经变了，`wait()` 本身就会立即返回（因为值不等于 `old_value`），所以这个循环通常只执行一次。伪唤醒虽然理论上存在，但在主流实现中极少发生。不过标准既然允许，我们就得遵守。

### notify 的保证与局限

`notify_one()` 唤醒一个在同一个原子变量上等待的线程（如果有多个，选择哪个是不确定的）。`notify_all()` 唤醒所有等待的线程。它们的语义跟 `condition_variable` 的 `notify_one/notify_all` 非常类似。

一个关键的保证是：如果在 `notify` 调用之前，另一个线程已经进入了 `wait`（已经开始阻塞），那这次 `notify` 一定会唤醒它（或它之中的一个）。但如果线程 A 正在执行 `notify`，而线程 B 还没来得及调用 `wait`——线程 B 不会错过这次通知，因为它会先 load 值，发现已经变了，直接返回而不阻塞。这也是值语义设计的威力：只要值变了，`wait` 就不会白等。

有一个容易忽视的细节：`notify_one()` 和 `notify_all()` 不需要跟 `wait()` 在同一个线程中配对使用。一个线程可以 `store + notify`，另一个线程可以 `wait + load`，完全解耦。但要确保 `store` 和 `notify` 的顺序正确——先 store 再 notify，否则等待线程可能被唤醒后还看到旧值（虽然它会再循环等一遍，但效率就差了）。

## 底层实现：从 futex 到 WaitOnAddress

了解底层实现有助于我们建立正确的性能预期。`wait/notify` 不是魔法——它们在不同平台上映射到不同的操作系统原语。

### Linux：futex

在 Linux 上，`std::atomic<T>::wait()` 最终调用 `futex()` 系统调用。futex 是 "Fast Userspace muTEX" 的缩写，但它的功能远不止 mutex——它本质上是一个"在用户空间地址上等待"的内核接口。`futex(addr, FUTEX_WAIT, expected_val, ...)` 的工作方式是：原子地比较 `*addr` 和 `expected_val`，如果相等就把当前线程挂起。`futex(addr, FUTEX_WAKE, 1, ...)` 唤醒一个在 `addr` 上等待的线程。

这跟 `std::atomic::wait(old_value)` 的语义几乎完全一致。标准库的实现通常会在内部维护一个等待表（waiter table），把原子变量的地址映射到 futex 的等待队列上。这个表的细节决定了 `notify` 的效率——如果表太大，哈希冲突少但内存占用高；如果表太小，不同原子变量可能共享同一个槽位，导致 `notify` 误唤醒不相关的线程。libstdc++ 的实现使用了固定大小的哈希表，根据地址映射到槽位。

futex 的阻塞/唤醒延迟大约在微秒量级——比 mutex + condition_variable 的组合快，但比纯用户空间的自旋要慢。所以 `wait/notify` 的最佳使用场景是：等待时间不会太短（不值得忙等），也不会太长（不值得用重量级的同步原语）。

### Windows：WaitOnAddress

在 Windows 上，对应的原语是 `WaitOnAddress`、`WakeByAddressSingle` 和 `WakeByAddressAll`，它们的语义跟 futex 几乎完全对称：`WaitOnAddress(addr, &expected, sizeof(T), INFINITE)` 在 `*addr == expected` 时阻塞，`WakeByAddressSingle(addr)` 唤醒一个等待者，`WakeByAddressAll(addr)` 唤醒所有。

### 回退方案

在不支持 futex 或 WaitOnAddress的平台上（比如某些嵌入式 RTOS），标准库会回退到 `std::mutex` + `std::condition_variable` 的实现。这意味着 `wait/notify` 在这些平台上并不比 condition_variable 更高效，但至少接口是统一的。

## 无需忙等的标志同步

有了 `wait/notify`，我们可以写出既不浪费 CPU 又不引入 mutex 开销的同步代码。一个经典的模式是"停止标志"——一个后台线程在循环中检查标志，主线程设置标志后通知它退出：

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

std::atomic<bool> stop_flag{false};

void background_worker()
{
    int iteration = 0;
    while (!stop_flag.load(std::memory_order_acquire)) {
        // 做一些周期性工作
        std::cout << "Working... iteration " << ++iteration << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "Worker: received stop signal, exiting\n";
}

int main()
{
    std::thread t(background_worker);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Main: sending stop signal\n";
    stop_flag.store(true, std::memory_order_release);
    stop_flag.notify_one();

    t.join();
    std::cout << "Main: worker joined\n";
    return 0;
}
```

这个例子中 `notify_one()` 其实不是必须的——工作线程每隔 500ms 会检查一次标志，最终总会发现它变了。但如果我们把 `sleep_for` 去掉，让工作线程真正忙等（比如高频执行短任务），那 `notify_one()` 就至关重要了——它能立即唤醒工作线程，减少退出的延迟。

更典型的用法是让等待方直接 `wait`，而不是轮询：

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> signal{0};

void waiter()
{
    std::cout << "Waiter: waiting for signal\n";
    signal.wait(0, std::memory_order_acquire);
    std::cout << "Waiter: signal value = " << signal.load() << "\n";
}

void notifier()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Notifier: sending signal\n";
    signal.store(42, std::memory_order_release);
    signal.notify_one();
}

int main()
{
    std::thread t1(waiter);
    std::thread t2(notifier);
    t1.join();
    t2.join();
    return 0;
}
```

在这个版本中，`waiter` 线程在 `signal.wait(0)` 处直接阻塞，完全不消耗 CPU。当 `notifier` 把值改成 42 并调用 `notify_one()` 时，`waiter` 立即被唤醒。这比忙等省电，比 condition_variable 省代码。

## std::atomic_ref<T>：给非原子变量"套上"原子操作

### 为什么需要 atomic_ref

`std::atomic<T>` 要求你在声明变量的时候就决定它是不是原子的——一旦声明为 `std::atomic<int>`，所有访问路径就都是原子的。但实际工程中有很多场景不允许这么做。最典型的是：你需要对一个已有数组中的元素做原子操作，但数组的类型已经定死了（可能是第三方库定义的，也可能需要兼容 C 接口），不能改成 `std::atomic<int>[]`。

C++20 引入的 `std::atomic_ref<T>` 就是解决这个问题的。它让你在不改变原始变量类型的前提下，对其施加原子操作。你可以把它理解成一个"原子视图"——它不拥有数据，只是提供了一种原子访问的视角。

### 基本用法

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    int value = 0;  // 普通 int，不是 std::atomic<int>

    // 创建 atomic_ref，指向 value
    std::atomic_ref<int> ref(value);

    auto increment = [&ref]() {
        for (int i = 0; i < 1000000; ++i) {
            ref.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::thread t1(increment);
    std::thread t2(increment);

    t1.join();
    t2.join();

    std::cout << "value = " << value << "\n";  // 稳定输出 2000000
    return 0;
}
```

注意，`value` 的类型是普通的 `int`，但通过 `std::atomic_ref<int>` 访问时，`fetch_add` 是原子的——没有 data race。`t1` 和 `t2` 同时对 `value` 做百万次自增，最终结果稳定在 2000000。

### 操作集：跟 std::atomic<T> 几乎一致

`std::atomic_ref<T>` 的接口跟 `std::atomic<T>` 几乎完全一样——`load()`、`store()`、`exchange()`、`compare_exchange_weak/strong()`、`fetch_add()`（对整型和指针）等全部支持。内存序参数也完全相同。唯一的区别是：`atomic_ref` 不拥有数据，它只是一个指向已有变量的引用。

### 限制与约束

`std::atomic_ref` 的设计带来了几个必须遵守的约束，违反这些约束会导致未定义行为。

第一条也是最关键的一条：**被引用的对象的生命周期必须超过所有 `atomic_ref` 实例的生命周期**。这跟普通引用的生命周期规则一样——如果对象已经销毁，你还在通过 `atomic_ref` 访问它，那就是典型的悬垂引用。在实际代码中，这意味着你不能让 `atomic_ref` 活得比它引用的变量更久。

第二条：**一旦对某个对象创建了 `atomic_ref`，在所有 `atomic_ref` 销毁之前，该对象只能通过 `atomic_ref`（或 `std::atomic`）来访问**。换句话说，不能混用原子和非原子的访问路径。如果线程 A 通过 `atomic_ref` 做 `fetch_add`，线程 B 直接读写原始变量——这是 data race，是未定义行为。这个约束的逻辑很清晰：`atomic_ref` 需要知道所有对变量的访问都走原子路径，否则无法保证一致性。

第三条：**同一个对象上的所有 `atomic_ref` 实例必须使用相同的对齐要求**。`std::atomic_ref<T>` 有一个静态成员 `required_alignment`，表示对齐的最小要求。如果某些平台上原子操作需要特殊对齐（比如 ARM 上 64 位原子操作需要 8 字节对齐），那么所有引用同一对象的 `atomic_ref` 都必须遵守这个对齐。

第四条：`std::atomic_ref` 可以复制构造——复制会产生一个引用同一对象的新 `atomic_ref` 实例，它也可以从被引用对象直接构造。所有引用同一对象的 `atomic_ref` 实例共享原子操作的保证，不会因为"多了一份引用"就破坏一致性。

### 典型使用场景：数组元素的原子访问

`atomic_ref` 最常见的场景是对数组或容器中的元素做原子操作。比如一个全局的统计数组，多个线程各自更新自己负责的计数器：

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

// 全局统计数组，类型是普通 int——可能需要被 C 代码或序列化库直接访问
constexpr int kNumCounters = 4;
int counters[kNumCounters] = {};

void update_counter(int index, int times)
{
    // 在函数内部创建 atomic_ref，安全地对 counters[index] 做原子自增
    std::atomic_ref<int> ref(counters[index]);
    for (int i = 0; i < times; ++i) {
        ref.fetch_add(1, std::memory_order_relaxed);
    }
}

int main()
{
    constexpr int kIncrementsPerThread = 1000000;
    std::vector<std::thread> threads;
    threads.reserve(kNumCounters);

    for (int i = 0; i < kNumCounters; ++i) {
        threads.emplace_back(update_counter, i, kIncrementsPerThread);
    }

    for (auto& t : threads) {
        t.join();
    }

    for (int i = 0; i < kNumCounters; ++i) {
        std::cout << "counter[" << i << "] = " << counters[i] << "\n";
    }
    return 0;
}
```

每个线程只访问自己负责的计数器——通过 `atomic_ref` 做原子操作。由于不同计数器之间没有依赖关系，用 `memory_order_relaxed` 就够了。注意 `atomic_ref` 的创建和销毁都在 `update_counter` 函数内部——它的生命周期严格短于 `counters` 数组，满足了生命周期约束。

## 实战：基于 atomic_wait 的二进制信号量

现在我们把 `wait/notify` 和 `atomic_ref` 的知识用起来，实现一个完整的二进制信号量。二进制信号量的语义是：初始值为 0（或 1），`acquire()` 将值从 1 减到 0（如果值为 0 就等待），`release()` 将值从 0 增到 1（如果值为 1 就什么都不做）。C++20 已经提供了 `std::binary_semaphore`，但自己实现一遍有助于理解 `wait/notify` 的工作方式。

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

class BinarySemaphore {
public:
    explicit BinarySemaphore(bool initial = false)
        : flag_(initial)
    {}

    void release()
    {
        // 先把 flag 设为 true，再通知等待者
        // 如果已经是 true，什么都不做
        bool expected = false;
        if (flag_.compare_exchange_strong(
                expected, true,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            flag_.notify_one();
        }
    }

    void acquire()
    {
        // 快速路径：如果已经是 true，直接 CAS 成功
        bool expected = true;
        if (flag_.compare_exchange_strong(
                expected, false,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return;
        }

        // 慢速路径：flag 是 false，需要等待
        // wait(false) 表示"如果值是 false 就阻塞"
        while (flag_.load(std::memory_order_acquire) == false) {
            flag_.wait(false, std::memory_order_acquire);
        }

        // 被唤醒后，尝试获取（可能有多个等待者竞争）
        bool exp = true;
        while (!flag_.compare_exchange_weak(
                   exp, false,
                   std::memory_order_acquire,
                   std::memory_order_relaxed)) {
            exp = true;
            flag_.wait(false, std::memory_order_acquire);
        }
    }

private:
    std::atomic<bool> flag_;
};
```

我们来逐块分析这段代码。

`release()` 的逻辑很简单：用 CAS 把 `flag_` 从 `false` 改成 `true`。如果 `flag_` 已经是 `true`（信号量已经 release 了），CAS 失败，我们什么都不做——这正是二进制信号量的语义。CAS 成功后调用 `notify_one()` 唤醒一个等待的 `acquire()`。注意 store 用的是 `memory_order_release`——这保证了 `release()` 之前的所有写入对被唤醒的线程可见。

`acquire()` 分成两个阶段。快速路径用 CAS 把 `flag_` 从 `true` 改成 `false`，如果当前值确实是 `true`，直接成功返回——没有阻塞，没有系统调用，纯粹的用户空间操作。如果 `flag_` 是 `false`（信号量没被 release），就进入慢速路径：在 `wait(false)` 上阻塞，直到其他线程调用 `release()` 把它变成 `true` 并通知。被唤醒后还需要再次 CAS——因为可能有多个线程同时被唤醒（`notify_all` 场景，或者伪唤醒），只有一个能成功获取。

接下来我们用这个信号量做一个简单的生产者-消费者测试：

```cpp
int main()
{
    constexpr int kNumItems = 10;
    BinarySemaphore sem_produced(false);  // 生产者完成后通知消费者
    BinarySemaphore sem_consumed(true);   // 消费者完成后通知生产者

    int shared_data = 0;

    std::thread producer([&]() {
        for (int i = 1; i <= kNumItems; ++i) {
            sem_consumed.acquire();   // 等待消费者消费完上一个
            shared_data = i;
            std::cout << "Produced: " << i << "\n";
            sem_produced.release();   // 通知消费者有新数据
        }
    });

    std::thread consumer([&]() {
        for (int i = 1; i <= kNumItems; ++i) {
            sem_produced.acquire();   // 等待生产者生产
            std::cout << "Consumed: " << shared_data << "\n";
            sem_consumed.release();   // 通知生产者可以继续
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

这个测试中，生产者和消费者交替执行——生产者写入数据后 `release(sem_produced)`，消费者 `acquire(sem_produced)` 后读取并 `release(sem_consumed)`，形成一个交替的生产-消费循环。`sem_consumed` 初始值为 `true`（通过构造函数传入 `true`），表示一开始生产者可以立即开始——不需要等待消费者先消费。

## 与 std::binary_semaphore 的对比

C++20 提供了标准的 `std::binary_semaphore`（定义在 `<semaphore>` 头文件中），它的功能跟我们上面实现的几乎一样。那什么情况下应该用标准库的，什么情况下需要自己实现？

标准库的 `std::binary_semaphore` 的优点是接口简洁——`acquire()` 和 `release()` 就够了，不需要关心内部实现。它也是基于 futex/WaitOnAddress 的，性能不会有差异。如果你的需求就是一个简单的信号量，直接用标准库版本就好。

但 `std::atomic::wait/notify` 给了你更多的灵活性。信号量只是它能实现的同步原语之一——你还可以用它实现事件（event）、一次性门闩（latch）、甚至简化版的 condition variable。当你的同步逻辑不刚好匹配信号量的语义时，直接用 `wait/notify` 比把信号量硬塞进去更自然。另外，`wait/notify` 直接作用在原子变量上，不需要额外的同步对象，在某些内存受限的场景（比如嵌入式系统）中可能更合适。

## 小结

这一篇我们完整拆解了 C++20 的两个原子新工具。`std::atomic<T>::wait()/notify_one()/notify_all()` 让原子变量具备了"等待/通知"能力——线程可以直接在原子变量上阻塞，值改变时被唤醒。它的值语义设计（传入"期望的旧值"而非"等待的目标值"）天然避免了 TOCTOU 竞态，底层在 Linux 上映射到 futex，在 Windows 上映射到 WaitOnAddress，延迟比 condition_variable 低一个数量级。

`std::atomic_ref<T>` 则解决了"对已有非原子变量做原子操作"这个长期痛点。它的接口跟 `std::atomic<T>` 几乎一致，但引入了严格的生命周期约束：引用必须活得比对象短，且存在 `atomic_ref` 期间不允许非原子的访问路径。

最后我们用 `wait/notify` 实现了一个完整的二进制信号量，看到了快速路径（CAS 直接成功）和慢速路径（阻塞等待 + 被唤醒后竞争）的完整处理流程。这个模式在后续的原子操作模式篇中还会反复出现。

下一篇我们进入"原子操作模式"——SeqLock、Double-Checked Locking、引用计数、发布-订阅标志等经典模式，综合运用我们在 ch03 学到的所有工具。

## 练习

### 练习 1：多等待者通知

编写一个程序：创建 4 个等待线程，每个线程在同一个 `std::atomic<int>` 上调用 `wait(0)`。主线程休眠 1 秒后，把值改为 1 并调用 `notify_all()`。观察 4 个线程是否都被唤醒。然后改成 `notify_one()`，观察有几个线程被唤醒。注意：由于伪唤醒的可能性，即使 `notify_one()` 也可能唤醒多个线程——但你大概率会观察到只唤醒了一个。

### 练习 2：atomic_ref 与数组

创建一个 `std::vector<int>` 包含 8 个元素。启动 4 个线程，每个线程通过 `std::atomic_ref<int>` 对 vector 中两个不同的元素各做 100 万次自增。结束后验证每个元素的值是否正确。注意选择不重叠的元素索引以避免竞争。

### 练习 3：改进二进制信号量

我们实现的 `BinarySemaphore::acquire()` 在慢速路径中有一个潜在问题：被 `notify_one()` 唤醒后，如果 CAS 失败（另一个等待者先拿到了），线程会再次进入 `wait`。但在重新 `wait` 之前，需要先确认值确实回到了 `false`——否则可能永远阻塞。分析一下当前实现是否正确处理了这个场景，如果不正确，指出问题并修复。

提示：考虑这个时序——线程 A 和线程 B 同时被唤醒，A 先 CAS 成功（`true -> false`），B 的 CAS 失败（值已经是 `false` 了）。此时 B 应该怎么做？`flag_` 的值是 `false`，B 调用 `wait(false)`，而此时没有线程会再调用 `release()`——B 会永远阻塞吗？

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch03-atomic-memory-model/`。

## 参考资源

- [std::atomic::wait — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/wait)
- [std::atomic_ref — cppreference](https://en.cppreference.com/cpp/atomic/atomic_ref)
- [Implementing C++20 atomic waiting in libstdc++ — Red Hat Developer](https://developers.redhat.com/articles/2022/12/06/implementing-c20-atomic-waiting-libstdc)
- [ogiroux/atomic_wait — Sample Implementation (GitHub)](https://github.com/ogiroux/atomic_wait)
- [Synchronization with Atomics in C++20 — Modernes C++](https://www.modernescpp.com/index.php/synchronization-with-atomics-in-c-20/)
- [Atomic References with C++20 — Modernes C++](https://www.modernescpp.com/index.php/atomic-ref/)
