---
title: "原子操作模式"
description: "SeqLock、Double-Checked Locking、引用计数与发布-订阅等经典原子模式的正确实现"
chapter: 3
order: 5
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - 无锁
difficulty: advanced
platform: host
reading_time_minutes: 25
cpp_standard: [11, 14, 17, 20]
prerequisites:
  - "fence 与编译器屏障"
  - "atomic_wait 与 atomic_ref"
related:
  - "无锁编程基础"
---

# 原子操作模式

> 📖 **应用场景**：这一篇的原子模式在嵌入式里有个高频落地——ISR 和主循环之间无锁共享变量。如果你在写单片机固件，配着 [卷八·中断安全编程](../../vol8-domains/embedded/05-interrupt-safe-coding.md)看会更通透。

到这一篇为止，我们已经把 `std::atomic` 的操作集、六种内存序、fence 和屏障、`wait/notify` 和 `atomic_ref` 全部拆解完了。但这些工具单独拿出来，只是在回答"怎么做"的问题——怎么做一个原子加法、怎么发一个 release store、怎么等一个值变化。真正的工程实践需要的是模式：面对一个具体的并发问题，应该选用哪些原子操作，以什么样的内存序组合起来，才能既正确又高效地解决问题。

这一篇我们集中讨论几个最经典的原子操作模式。这些模式不是凭空发明的——它们来自 Linux 内核、数据库引擎、高性能网络框架等真实系统中反复验证过的方案。我们会拆解每个模式的"为什么"：为什么这样设计、为什么内存序不能更弱、为什么某个看似无害的改动会引入 bug。

我们要覆盖的模式包括：SeqLock（序列锁定）、Double-Checked Locking（双重检查锁定）、引用计数、发布-订阅标志、无锁最大/最小追踪、停止标志以及自旋锁。每个模式都配有完整的代码和逐步的语义分析。

## SeqLock：读取器不被阻塞的序列锁定

### 模式动机

读者写者问题有一个经典的解法是读写锁（reader-writer lock），但它的代价很高——即使只有读取操作，也需要 `lock_shared()` / `unlock_shared()` 的完整流程，涉及到原子操作甚至系统调用。在很多场景下，读取频率远远高于写入频率（比如传感器数据的采集-读取、系统时间的获取），我们希望读取操作尽可能轻量——最好完全无锁。

SeqLock 就是为此设计的。它的核心思想是：用一把自旋锁保护写入方（同一时刻只有一个写入者），但完全不阻塞读取方——读取者通过检查一个序列号来判断自己读到的数据是否一致。如果读取过程中序列号发生了变化（说明有写入者修改了数据），读取者只需要重试即可。

### 实现

```cpp
#include <atomic>
#include <thread>
#include <iostream>

class SeqLock {
public:
    SeqLock() : sequence_(0) {}

    /// 写入者：获取写入权限
    void lock_write()
    {
        unsigned seq = sequence_.load(std::memory_order_relaxed);
        // 如果序列号是奇数，说明已经有写入者在工作
        if ((seq & 1u) != 0) {
            // 多写入者场景需要自旋等待或用额外的 mutex
            // 这里假设只有一个写入者
            return;
        }
        // 序列号加 1，变成奇数——标记"正在写入"
        sequence_.store(seq + 1, std::memory_order_release);
    }

    /// 写入者：释放写入权限
    void unlock_write()
    {
        unsigned seq = sequence_.load(std::memory_order_relaxed);
        // 序列号再加 1，变回偶数——标记"写入完成"
        sequence_.store(seq + 1, std::memory_order_release);
    }

    /// 读取者：在稳定状态下读取数据
    /// 返回读取开始时的序列号；调用者需要在读取后验证序列号是否变化
    unsigned read_begin() const
    {
        unsigned seq;
        for (;;) {
            seq = sequence_.load(std::memory_order_acquire);
            if ((seq & 1u) == 0) {
                // 偶数：没有写入者正在工作
                break;
            }
            // 奇数：有写入者正在工作，自旋等待
            // 实际实现中可以用 pause/yield 减少功耗
        }
        return seq;
    }

    /// 读取者：验证读取期间是否有写入发生
    /// 如果返回 true，说明读取是有效的
    bool read_validate(unsigned seq_before) const
    {
        unsigned seq_after = sequence_.load(std::memory_order_acquire);
        return (seq_after == seq_before) && ((seq_after & 1u) == 0);
    }

private:
    std::atomic<unsigned> sequence_;
};
```

我们来拆解这个设计的核心机制。

序列号的奇偶性是关键。偶数表示"当前没有写入者在工作，数据处于一致状态"；奇数表示"有写入者正在修改数据，当前可能不一致"。写入者在开始时把序列号从偶数变成奇数，完成后再变成偶数——每次成功的写入让序列号增加 2。

读取者的策略是"读前检查 + 读后验证"：先读取序列号，确认它是偶数（没有写入者），然后读取实际数据，最后再读取序列号。如果前后两次序列号相同且都是偶数，说明读取过程中没有写入者介入，数据是一致的。如果不同（或者变成了奇数），说明读取过程中有写入发生，数据可能不一致——读取者直接丢弃这次结果，重试。

`unlock_write()` 中的 `memory_order_release` 和 `read_begin()` / `read_validate()` 中的 `memory_order_acquire` 建立了 happens-before 关系：写入者对实际数据的所有修改，在 `sequence_` 变回偶数之前完成（release 保证之前的写入不会被重排到 store 之后）；读取者在 `sequence_` 变成偶数之后才看到数据（acquire 保证之后的读取不会被重排到 load 之前）。这样，读取者读到的数据一定是写入者完全写入后的版本。

### 使用示例

```cpp
struct SensorData {
    double temperature;
    double humidity;
    double pressure;
};

SensorData g_sensor_data;
SeqLock g_seq_lock;

// 写入者线程（通常是传感器采集线程）
void writer_thread()
{
    for (int i = 0; i < 100; ++i) {
        g_seq_lock.lock_write();

        g_sensor_data.temperature = 20.0 + i * 0.1;
        g_sensor_data.humidity = 50.0 + i * 0.2;
        g_sensor_data.pressure = 1013.25 + i * 0.01;

        g_seq_lock.unlock_write();
    }
}

// 读取者线程（可以有多个）
void reader_thread(int id)
{
    for (int i = 0; i < 100; ++i) {
        SensorData local;
        unsigned seq;

        do {
            seq = g_seq_lock.read_begin();
            local = g_sensor_data;  // 拷贝数据
        } while (!g_seq_lock.read_validate(seq));

        // 现在可以安全地使用 local——它是一个一致的快照
        std::cout << "Reader " << id << ": temp=" << local.temperature
                  << " humidity=" << local.humidity
                  << " pressure=" << local.pressure << "\n";
    }
}
```

注意读取者把数据拷贝到 `local` 变量中再验证。这是一个关键细节——如果不拷贝就直接使用，验证失败时数据已经"脏"了，没法用也没法重来。SeqLock 的读取者必须准备好随时丢弃读取结果，所以读取的数据要么是只读的（用完即弃），要么拷贝出来再使用。

### SeqLock 的适用边界

SeqLock 有几个限制需要清楚认识。首先，它假设最多只有一个写入者——如果需要多个写入者，必须在外面套一层 mutex。其次，读取的数据类型必须是 trivially copyable 的——如果数据包含指针或复杂对象，拷贝过程中遇到的部分修改状态可能导致未定义行为。第三，如果写入非常频繁，读取者可能反复重试，性能反而比读写锁差——SeqLock 适合"写入少、读取多"的场景。Linux 内核的 `seqlock_t` 就是这个模式的经典实现，用于时间获取（`do_gettimeofday`）等场景。

## Double-Checked Locking：C++11 起终于正确了

### 模式动机与历史包袱

Double-Checked Locking Pattern（DCLP）可能是多线程编程中被讨论得最多的模式之一——不是因为它是最好的模式，而是因为它在 C++11 之前根本无法正确实现。Scott Meyers 和 Andrei Alexandrescu 在 2004 年的论文 "C++ and the Perils of Double-Checked Locking" 中详细分析了它为什么在旧标准下会失败。核心原因有两点：编译器可以对内存操作做重排（写入对象的字段可能被重排到发布指针之后），以及 CPU 本身也可能做重排（在 x86 上相对受限，在 ARM/PowerPC 上非常激进）。

C++11 引入的正式内存模型和 `std::atomic` 终于让 DCLP 有了可移植的正确实现。

### 正确的 DCLP 实现

```cpp
#include <atomic>
#include <mutex>
#include <iostream>

class Singleton {
public:
    static Singleton& instance()
    {
        Singleton* ptr = instance_.load(std::memory_order_acquire);
        if (ptr == nullptr) {
            std::lock_guard<std::mutex> lock(mutex_);
            ptr = instance_.load(std::memory_order_relaxed);
            if (ptr == nullptr) {
                ptr = new Singleton();
                instance_.store(ptr, std::memory_order_release);
            }
        }
        return *ptr;
    }

    void do_something()
    {
        std::cout << "Singleton::do_something()\n";
    }

private:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    static std::atomic<Singleton*> instance_;
    static std::mutex mutex_;
};

std::atomic<Singleton*> Singleton::instance_{nullptr};
std::mutex Singleton::mutex_;
```

我们拆解这个实现中每一层检查的作用。

第一次检查 `instance_.load(acquire)` 是在锁外进行的——如果实例已经创建好了（绝大多数调用都走这条路径），直接返回指针，不需要加锁。`memory_order_acquire` 保证了后续通过这个指针访问的 `Singleton` 对象的成员，一定能看到构造函数中初始化的值。这就是为什么这个 load 不能用 `relaxed`——`relaxed` 不建立 happens-before 关系，我们可能看到一个分配了内存但还没构造完的对象。

第二次检查 `instance_.load(relaxed)` 在锁内进行——这个时候已经持有 mutex，不可能有其他线程同时在创建实例，所以 `relaxed` 就够了。如果你觉得 `relaxed` 看着不放心，换成 `acquire` 也不会有正确性问题，只是理论上多了一个不必要的屏障。

`instance_.store(ptr, release)` 中的 `release` 语义是关键：它保证 `new Singleton()`（包括构造函数中所有的初始化操作）在 store 之前完成。结合第一次检查中的 `acquire` load，就建立了一个完整的 release-acquire 同步对：构造函数的所有写入 happens-before store，store happens-before 另一个线程的 acquire load，acquire load happens-before 该线程对 Singleton 成员的访问。链路完整，没有缝隙。

### 为什么不直接用 Meyers' Singleton

C++11 保证函数内的 `static` 局部变量的初始化是线程安全的。所以最简单的单例模式其实是：

```cpp
class Singleton {
public:
    static Singleton& instance()
    {
        static Singleton inst;
        return inst;
    }
private:
    Singleton() = default;
};
```

这段代码完全正确，且编译器通常会内部用 `std::call_once` 或等价的原子操作来实现。那 DCLP 还有什么用？

首先，DCLP 的思想不局限于单例——任何"检查-加锁-再检查-初始化"的模式都可以用这个思路。比如延迟初始化一个大对象、按需分配线程局部存储、延迟加载配置文件等。其次，在某些极端性能场景下，DCLP 的第一次检查比 `static` 局部变量生成的代码更轻量——后者通常需要检查一个隐藏的 `std::once_flag`，而这个 flag 的实现可能比单个 `atomic load` 更重。

## 引用计数：shared_ptr 的原子基础

### 引用计数的原子要求

引用计数是另一个随处可见的原子模式。`std::shared_ptr` 的控制块里就包含一个引用计数和一个弱引用计数，它们都是原子变量。我们来看一个简化版的引用计数指针，理解它需要哪些原子操作：

```cpp
#include <atomic>
#include <iostream>

template<typename T>
class IntrusivePtr {
public:
    IntrusivePtr() : ptr_(nullptr) {}

    explicit IntrusivePtr(T* ptr) : ptr_(ptr)
    {
        if (ptr_) {
            ptr_->add_ref();
        }
    }

    IntrusivePtr(const IntrusivePtr& other) : ptr_(other.ptr_)
    {
        if (ptr_) {
            ptr_->add_ref();
        }
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }

    IntrusivePtr& operator=(const IntrusivePtr& other)
    {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            if (ptr_) {
                ptr_->add_ref();
            }
        }
        return *this;
    }

    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept
    {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    ~IntrusivePtr()
    {
        release();
    }

    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    T* get() const { return ptr_; }

private:
    void release()
    {
        if (ptr_ && ptr_->release_ref()) {
            delete ptr_;
        }
        ptr_ = nullptr;
    }

    T* ptr_;
};

/// 基类：提供侵入式引用计数
class RefCounted {
public:
    RefCounted() : ref_count_(1) {}
    virtual ~RefCounted() = default;

    void add_ref()
    {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    /// 返回 true 表示引用计数归零，应该销毁对象
    bool release_ref()
    {
        // acquire 保证在引用计数归零后，能看到所有之前 add_ref 的线程
        // 对对象的全部修改——确保析构时对象状态一致
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

private:
    std::atomic<int> ref_count_;
};
```

引用计数的原子操作有两个关键点。`add_ref()` 使用 `memory_order_relaxed`——增加引用计数不需要跟其他操作同步，我们只关心计数本身的原子性。即使线程 A 的 `add_ref` 和线程 B 的 `release_ref` 发生竞争，`fetch_add` 和 `fetch_sub` 本身是原子的，不会导致计数错误。

`release_ref()` 使用 `memory_order_acq_rel` 则是一个更精细的选择。`acquire` 语义保证当引用计数归零时，当前线程能看到所有其他线程在此之前对对象的修改（因为每个 `add_ref` 之后的对象访问都隐含了一个"持有引用"的关系）。`release` 语义保证在析构对象之前，所有当前线程对对象的访问都已经完成。这两个方向一起确保了析构的安全性——析构函数看到的是一个完全一致的对象状态，而不会有其他线程还在访问对象。

## 发布-订阅标志：relaxed 计数器 + acquire-release 标志

### 模式描述

这是一个非常实用的组合模式：一个 `relaxed` 的原子计数器用于统计（不需要精确的同步），加上一个 `acquire-release` 的原子标志用于通知。典型的场景是任务队列——工作线程从队列中取任务执行，每完成一个任务就把计数器加 1，全部完成后设置标志通知主线程。

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

std::atomic<int> tasks_completed{0};
std::atomic<bool> all_done{false};

void worker(int num_tasks)
{
    for (int i = 0; i < num_tasks; ++i) {
        // 模拟任务处理
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        tasks_completed.fetch_add(1, std::memory_order_relaxed);
    }
}

int main()
{
    constexpr int kNumWorkers = 4;
    constexpr int kTasksPerWorker = 25;
    constexpr int kTotalTasks = kNumWorkers * kTasksPerWorker;

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumWorkers; ++i) {
        threads.emplace_back(worker, kTasksPerWorker);
    }

    // 主线程等待所有任务完成
    while (!all_done.load(std::memory_order_acquire)) {
        std::cout << "Progress: " << tasks_completed.load(std::memory_order_relaxed)
                  << "/" << kTotalTasks << "\n";
        if (tasks_completed.load(std::memory_order_relaxed) >= kTotalTasks) {
            all_done.store(true, std::memory_order_release);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (auto& t : threads) {
        t.join();
    }
    std::cout << "All " << kTotalTasks << " tasks completed!\n";
    return 0;
}
```

这个模式的关键在于分离关注点。`tasks_completed` 只用于显示进度——它不需要精确同步，所以用 `memory_order_relaxed` 就够了。即使主线程偶尔读到"旧"的计数（少 1 或 2），对用户体验没有影响。`all_done` 才是真正的同步点——它用 `acquire-release` 保证当主线程看到 `all_done == true` 时，所有工作线程对共享数据的修改都已经可见。

这种"宽松统计 + 严格同步"的组合在工程中非常常见。再举一个例子：网络服务器用一个 relaxed 计数器记录已处理的请求数（偶尔丢一两个更新无所谓），用一个 acquire-release 标志通知关闭信号（必须保证所有请求处理完毕后才能关）。

## 无锁最大/最小追踪：CAS 循环

### 模式描述

维护一个全局的最大值或最小值，在多线程环境中无锁地更新——这是一个经典的 CAS（compare-and-swap）使用模式。比如一个网络服务器想追踪最慢的请求延迟，或者一个传感器系统想记录极端温度。

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <random>
#include <iostream>
#include <cmath>

class MaxTracker {
public:
    explicit MaxTracker(double initial)
        : max_value_(initial)
    {}

    /// 如果新值大于当前最大值，更新最大值
    void update(double candidate)
    {
        double current = max_value_.load(std::memory_order_relaxed);
        while (candidate > current) {
            if (max_value_.compare_exchange_weak(
                    current, candidate,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;  // CAS 成功，更新完成
            }
            // CAS 失败，current 被自动更新为当前值，继续循环
        }
    }

    double get() const
    {
        return max_value_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<double> max_value_;
};

int main()
{
    MaxTracker tracker(0.0);
    constexpr int kNumThreads = 4;
    constexpr int kUpdatesPerThread = 100000;

    auto worker = [&](int seed) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> dist(0.0, 100.0);
        for (int i = 0; i < kUpdatesPerThread; ++i) {
            tracker.update(dist(rng));
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back(worker, i + 42);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "Max value tracked: " << tracker.get() << "\n";
    return 0;
}
```

CAS 循环是这个模式的核心。我们先 load 当前最大值，如果候选值不大于当前值，什么都不做直接返回。如果候选值更大，尝试用 CAS 把当前值替换成候选值。CAS 可能失败——因为其他线程可能在我们 load 和 CAS 之间已经更新了最大值。失败后 `compare_exchange_weak` 会把 `current` 更新为最新的值，我们重新比较，决定是否需要再次尝试。

这里用 `compare_exchange_weak` 而不是 `strong` 是一个常见的优化——在循环中，`weak` 版本偶尔的伪失败（spurious failure）只是多循环一次，但它在某些平台（特别是 ARM、PowerPC 等 LL/SC 架构）上比 `strong` 更高效。

内存序全部用 `relaxed`——因为我们只关心单个变量（最大值）本身的正确性，不需要跟其他变量建立同步关系。如果最大值追踪只是用于统计或监控，不需要严格的 happens-before 保证。

不过要注意，`std::atomic<double>` 的 CAS 操作在大多数平台上不是 lock-free 的——因为 `double` 是 64 位，而某些 32 位平台的 CAS 只能处理 32 位。如果你的目标是 32 位嵌入式平台，这个模式可能不如预期那样高效。在 64 位平台上，64 位的 CAS 通常是 lock-free 的。

## 停止标志：atomic<bool> 的正确用法

### 基本模式

停止标志可能是最简单的原子模式了——一个后台线程定期检查标志，主线程设置标志后等待线程退出。看起来简单，但细节上还是有值得讨论的地方：

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

std::atomic<bool> should_stop{false};

void background_task()
{
    int count = 0;
    while (!should_stop.load(std::memory_order_acquire)) {
        // 做一些工作
        ++count;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Task stopped after " << count << " iterations\n";
}

int main()
{
    std::thread t(background_task);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    should_stop.store(true, std::memory_order_release);
    t.join();
    std::cout << "Main: thread joined\n";
    return 0;
}
```

这里用 `memory_order_acquire` 和 `memory_order_release` 而不是 `relaxed`，原因值得说明。如果后台线程在检查停止标志之后还读取了一些共享数据（比如在 `sleep_for` 之后要读取最新的配置），那 `acquire` 保证了它能看到设置标志的线程在此之前对共享数据的所有修改。同理，`release` 保证了主线程在设置标志之前的所有写入（比如更新配置）对后台线程可见。

如果你的停止标志纯粹是一个布尔信号——后台线程不需要读取任何其他共享数据——那 `relaxed` 也是安全的。但养成用 `acquire/release` 的习惯没什么坏处，性能差异可以忽略不计（x86 上 load 不管用什么内存序都是普通读取，ARM 上的 acquire load 也只是一条 `ldar` 指令）。

### 结合 atomic_wait 实现低延迟停止

上一篇我们介绍了 `std::atomic::wait/notify`，这里可以把停止标志升级为"等待式停止"——后台线程不是轮询标志，而是在标志上阻塞等待：

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

std::atomic<bool> should_stop{false};

void waiting_task()
{
    int count = 0;
    while (!should_stop.load(std::memory_order_acquire)) {
        ++count;
        std::cout << "Working... iteration " << count << "\n";

        // 等待 100ms 或被 notify 唤醒
        should_stop.wait(false, std::memory_order_acquire);
    }
    std::cout << "Task stopped after " << count << " iterations\n";
}

int main()
{
    std::thread t(waiting_task);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    should_stop.store(true, std::memory_order_release);
    should_stop.notify_one();

    t.join();
    std::cout << "Main: thread joined\n";
    return 0;
}
```

这个版本中，`wait(false)` 在 `should_stop` 仍然是 `false` 时阻塞，完全不消耗 CPU。当主线程 `store(true) + notify_one()` 后，后台线程立即被唤醒并退出。但有一个问题：`wait` 是没有超时的——如果后台线程在两次 `wait` 之间需要定期做一些工作（比如每 100ms 检查一次传感器），那纯 `wait` 就不太合适了。这种情况下，结合 `sleep_for` + `notify` 的混合方案更实际：大部分时间用 `sleep_for` 做周期性工作，用 `notify` 在需要立即停止时唤醒线程。

## 自旋锁：教学实现与适用场景

### 基本实现

自旋锁是最简单的互斥原语——获取失败的线程不阻塞，而是在一个紧凑的循环中反复尝试。它通常不适合生产环境（后面会解释为什么），但作为教学工具非常合适——因为它用最少的代码展示了 `atomic_flag` 的用法和 lock-free 同步的基本原理。

```cpp
#include <atomic>
#include <thread>
#include <iostream>

class SpinLock {
public:
    SpinLock() : locked_(false) {}

    void lock()
    {
        while (locked_.exchange(true, std::memory_order_acquire)) {
            // exchange 返回旧值：如果是 true，说明锁已经被占用，继续自旋
            // 如果是 false，说明我们成功获取了锁
        }
    }

    void unlock()
    {
        locked_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> locked_;
};

int main()
{
    SpinLock spinlock;
    int counter = 0;

    auto work = [&](int times) {
        for (int i = 0; i < times; ++i) {
            spinlock.lock();
            ++counter;
            spinlock.unlock();
        }
    };

    std::thread t1(work, 1000000);
    std::thread t2(work, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter << "\n";  // 2000000
    return 0;
}
```

`lock()` 中的 `exchange(true, acquire)` 是一个巧妙的操作：它原子地把 `locked_` 设为 `true`，同时返回设置之前的值。如果旧值是 `false`，说明锁之前没被占用，我们成功获取了锁。如果旧值是 `true`，说明锁已经被别人占了，我们继续循环。`acquire` 语义保证了获取锁之后的操作不会被重排到 `exchange` 之前——其他线程释放锁之前的修改对当前线程可见。

`unlock()` 中的 `release` 语义保证了临界区内的所有写入在释放锁之前完成——下一个获取锁的线程能看到这些修改。

### 为什么自旋锁通常不适合生产环境

自旋锁最大的问题是它在等待期间占用 CPU。如果临界区很短（几条指令），自旋等待的开销可能比 mutex 的上下文切换开销更低。但如果临界区稍长，或者有多个线程在竞争同一把锁，自旋锁会导致 CPU 时间被大量浪费在"空转"上。更糟糕的是，在单核系统上自旋锁完全没有意义——线程在自旋期间占着 CPU，持有锁的线程根本没机会运行去释放锁，死锁。

在实际项目中，优先使用 `std::mutex` 或 `std::shared_mutex`。只有在以下条件同时满足时才考虑自旋锁：临界区极短（不超过几十条指令）、竞争不激烈、运行在多核系统上。Linux 内核在中期内核抢占（preemptible kernel）中大量使用自旋锁——但内核有特殊的调度保证（关抢占），用户态没有这个条件。

### 用 atomic_flag 的更优版本

上面的 `SpinLock` 用 `std::atomic<bool>` 实现，但更规范的做法是用 `std::atomic_flag`——它是标准保证 lock-free 的唯一原子类型（`std::atomic<bool>` 理论上可能不是 lock-free 的）：

```cpp
class SpinLockFlag {
public:
    SpinLockFlag() { flag_.clear(); }

    void lock()
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // test_and_set 原子地设置 flag 为 true 并返回旧值
        }
    }

    void unlock()
    {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
```

`test_and_set` 和 `clear` 是 `atomic_flag` 的两个核心操作——前者原子地把 flag 设为 `true` 并返回旧值，后者原子地把 flag 设为 `false`。这个版本在语义上跟 `atomic<bool>` 版本完全等价，但保证了 lock-free。

## 模式选择的决策指南

了解了这么多模式，实际编码时怎么选？我们可以按照临界区的特征来决策。

如果临界区只是一个简单的变量读取或更新——比如一个计数器、一个标志、一个最大值——直接用 `std::atomic` 的 RMW 操作（`fetch_add`、CAS 等）就够了。不需要 mutex，也不需要自旋锁。这是最轻量的选择，性能最好。内存序的选择取决于是否需要跟其他变量同步：如果不需要，`relaxed` 即可；如果需要，用 `acquire/release`。

如果临界区包含多个变量的协同修改——比如向一个 map 中插入元素同时更新计数器——那 `std::atomic` 不够用了（除非你能把多个变量打包成一个用 CAS 更新的结构体），老老实实用 `std::mutex`。mutex 虽然有上下文切换的开销，但它保证正确性，且在竞争不激烈时开销很低（Linux 的 `futex` 在无竞争时完全在用户空间完成）。

如果读取频率远高于写入频率，且数据是 trivially copyable 的——SeqLock 是一个好选择。它让读取者完全无锁，代价只是偶尔重试。Linux 内核在很多高频读取场景下使用它。

如果需要延迟初始化或"检查-加锁-再检查"的模式——DCLP 在 C++11 起是正确的。但如果只是单例，优先用 Meyers' Singleton（`static` 局部变量），它更简单也更不容易出错。

如果需要等待某个条件满足——用 `std::atomic::wait/notify` 替代忙等或 condition_variable。它在 Linux 上用 futex，延迟比 condition_variable 低一个数量级，且不需要额外的 mutex。

## 小结

这一篇我们把 ch03 学到的所有工具——`std::atomic` 的操作集、内存序、fence、`wait/notify`、`atomic_ref`——综合运用到了七个经典的并发模式中。

SeqLock 通过序列号的奇偶性让读取者无锁地检测写入干扰，适合"读多写少、数据 trivially copyable"的场景。Double-Checked Locking 在 C++11 的内存模型下终于有了正确的可移植实现——核心是 `std::atomic<T*>` 的 `acquire` load 和 `release` store。引用计数模式展示了 `relaxed` 的 `fetch_add` 和 `acq_rel` 的 `fetch_sub` 的组合——前者只关心原子性，后者还要保证析构时的可见性。发布-订阅标志把宽松的计数统计和严格的同步通知分离——各取所需，互不拖累。无锁最大/最小追踪用 CAS 循环实现了无锁的"比较并更新"。停止标志是最简单的原子模式，但结合 `wait/notify` 后也能实现低延迟的停止信号。自旋锁是教学的经典，生产环境应谨慎使用。

这些模式不是孤立的——它们经常组合使用。一个 SeqLock 内部可能用自旋锁保护写入者；一个 DCLP 内部用了 acquire-release 同步对；一个引用计数指针的析构可能触发一个发布-订阅通知。理解每个模式的核心思想，然后在具体场景中灵活组合，才是真正的目标。

下一篇我们离开 ch03 的原子世界，进入新的话题。但在那之前，建议把本篇的练习做一做——特别是 SeqLock 和 DCLP 的实现，它们是面试中的高频考点，也是检验你是否真正理解了内存序的试金石。

## 练习

### 练习 1：实现 SeqLock

基于上面的 `SeqLock` 类，写一个完整的程序：一个写入者线程以 10ms 的间隔更新一个包含三个 `double` 字段的结构体，四个读取者线程各自以 1ms 的间隔读取并打印数据。运行一段时间后，观察读取者是否总能获得一致的数据（三个字段的值来自同一次写入）。如果数据出现不一致（比如温度是第 5 次写入的值，但湿度是第 6 次的），检查你的 `read_begin` / `read_validate` 是否正确使用。

### 练习 2：实现 DCLP 单例

用 DCLP 模式实现一个线程安全的配置管理器。要求：

1. 使用 `std::atomic<ConfigManager*>` + `std::mutex` 的经典 DCLP 结构
2. 在 `instance()` 中正确使用 `memory_order_acquire` 和 `memory_order_release`
3. 写一个多线程测试：8 个线程同时调用 `ConfigManager::instance()`，验证所有线程拿到的是同一个实例

额外挑战：对比你的 DCLP 实现和 Meyers' Singleton（`static` 局部变量）实现的性能差异。用 `std::chrono` 测量两种实现在 100 万次 `instance()` 调用下的耗时。

### 练习 3：无锁最小值追踪器

实现一个 `MinTracker` 类，用 CAS 循环追踪一个 `double` 类型的最小值。然后用 4 个线程各自生成随机数并调用 `update()`，最后验证 `get()` 返回的确实是所有线程生成的数中的最小值。

提示：你需要注意浮点数的原子操作在你当前平台上是否是 lock-free 的。用 `std::atomic<double>::is_lock_free()` 检查。如果不是 lock-free，性能可能不如预期。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch03-atomic-memory-model/`。

## 参考资源

- [Double-Checked Locking is Fixed In C++11 — Jeff Preshing](https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11)
- [C++ and the Perils of Double-Checked Locking — Scott Meyers, Andrei Alexandrescu](https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf)
- [Seqlock — Wikipedia](https://en.wikipedia.org/wiki/Seqlock)
- [Linux Kernel seqlock.h — source code](https://github.com/torvalds/linux/blob/master/include/linux/seqlock.h)
- [std::atomic_flag — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [std::shared_ptr thread safety — cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [Preshing on Programming: Atomic vs. Non-Atomic Operations](https://preshing.com/20130618/atomic-vs-non-atomic-operations/)
