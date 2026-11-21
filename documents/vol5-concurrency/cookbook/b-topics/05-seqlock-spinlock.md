---
title: "SeqLock 与自旋锁"
description: "两个以读取方为中心的原子模式：SeqLock 用序列号奇偶校验让读者完全无锁，自旋锁用最少的代码展示 atomic_flag 与 acquire/release 的配合，外加各自的适用边界。"
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
difficulty: advanced
platform: host
cpp_standard: [11]
---

# SeqLock 与自旋锁

本篇是旧卷《原子操作模式》的分篇。素材原本一篇打包了七个模式，正文迁移时做了拆分：停止标志、引用计数与双重检查锁定归第五单元的[《内存序的四档：从顺序一致性到 relaxed》](../../chapter05-atomic-memory-model/04-choosing-memory-orders)，发布-订阅标志也在那里（扩展节），无锁最大值追踪不单独留篇，CAS 循环的重试写法在[《原子操作解决到哪一步》](../../chapter05-atomic-memory-model/01-what-atomics-solve)讲过。选读区留下 SeqLock 与自旋锁两样，它们超出必修线的判断法很直接：不看它们，第五单元的主线（原子操作、内存序判据）仍然完整，剩下的是读多写少与极短临界区两类专门场景的专用设计，咱们把它们收进选读区。

读它之前，您需要 happens-before 与 acquire/release 的判据（[《一个线程怎样观察另一个线程的修改》](../../chapter05-atomic-memory-model/03-happens-before)与[《内存序的四档：从顺序一致性到 relaxed》](../../chapter05-atomic-memory-model/04-choosing-memory-orders)），CAS 与 `atomic_flag` 的操作语义在[《原子操作解决到哪一步》](../../chapter05-atomic-memory-model/01-what-atomics-solve)。

平移来路：机制讲解与代码原样保留，练习只迁了 SeqLock 一道，另加一道自旋锁计时题，我改动的只有编号、frontmatter、指向正文的交叉引用，外加判死词表清理。素材没有声称实测读数，涉及的内核行为（`seqlock_t`）是引用 Linux 内核的公开事实，出处收在参考资源。

## SeqLock：读取器不被阻塞的序列锁定

### 模式动机

读者写者问题有一个经典的解法是读写锁（reader-writer lock），但它的代价很高——即使只有读取操作，也需要 `lock_shared()` / `unlock_shared()` 的完整流程，涉及到原子操作甚至系统调用。在很多场景下，读取频率远远高于写入频率（比如传感器数据的采集-读取、系统时间的获取），咱们希望读取操作尽可能轻量——最好完全无锁。

SeqLock 就是为此设计的。咱们把它的核心思想摆出来：用一把自旋锁保护写入方（同一时刻只有一个写入者），但完全不阻塞读取方，读取者通过检查一个序列号，判断自己读到的数据是否一致。如果读取过程中序列号发生了变化（说明有写入者修改了数据），读取者只需要重试即可。

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

咱们来拆解这个设计的核心机制。

序列号的奇偶性是要害，咱们把它记牢。偶数表示“当前没有写入者在工作，数据处于一致状态”，奇数表示“有写入者正在修改数据，当前可能不一致”。写入者在开始时把序列号从偶数变成奇数，完成后再变成偶数——每次成功的写入让序列号增加 2。

读取者的策略是“读前检查 + 读后验证”，咱们跟着走一遍：读取序列号，确认它是偶数（没有写入者），然后读取实际数据，最后再读取序列号。如果前后两次序列号相同且都是偶数，说明读取过程中没有写入者介入，数据是一致的。如果不同（或者变成了奇数），说明读取过程中有写入发生，数据可能不一致——读取者直接丢弃这次结果，重试。

`unlock_write()` 中的 `memory_order_release` 和 `read_begin()` / `read_validate()` 中的 `memory_order_acquire` 建立了 happens-before 关系：写入者对实际数据的所有修改，在 `sequence_` 变回偶数之前完成，release 保证之前的写入不会被重排到 store 之后。读取者在 `sequence_` 变成偶数之后才看到数据，acquire 保证之后的读取不会被重排到 load 之前。这样读取者读到的数据，一定是写入者完全写入后的版本，咱们可以放心用。

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

咱们注意，读取者把数据拷贝到 `local` 变量中再验证。这是一个要害细节，如果不拷贝就直接使用，验证失败时数据已经“脏”了，没法用也没法重来。SeqLock 的读取者必须准备好随时丢弃读取结果，所以读取的数据要么是只读的（用完即弃），要么拷贝出来再使用。

### SeqLock 的适用边界

SeqLock 有几个限制需要咱们清楚认识。它假设最多只有一个写入者，需要多个写入者时，必须在外面再套一层 mutex。读取的数据类型必须是 trivially copyable 的，数据里要是有指针或复杂对象，拷贝过程中遇到部分修改的状态就可能导致未定义行为。写入非常频繁的时候，读取者可能反复重试，性能反而比读写锁差——SeqLock 适合“写入少、读取多”的场景。Linux 内核的 `seqlock_t` 就是这个模式的经典实现，用于时间获取（`do_gettimeofday`）等场景。

## 自旋锁：教学实现与适用场景

### 基本实现

自旋锁是最简单的互斥原语——获取失败的线程不阻塞，而是在一个紧凑的循环中反复尝试。它通常不适合生产环境（后面咱们会解释为什么），但作为教学工具非常合适，因为它用最少的代码，展示了 `atomic_flag` 的用法和同步的基本原理。

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

`lock()` 中的 `exchange(true, acquire)` 是一个巧妙的操作：它原子地把 `locked_` 设为 `true`，同时返回设置之前的值。如果旧值是 `false`，说明锁之前没被占用，咱们成功获取了锁。如果旧值是 `true`，说明锁已经被别人占了，咱们继续循环。`acquire` 语义保证了获取锁之后的操作不会被重排到 `exchange` 之前，其他线程释放锁之前的修改对当前线程可见。

咱们再看 `unlock()`：其中的 `release` 语义保证了临界区内的所有写入在释放锁之前完成，下一个获取锁的线程能看到这些修改。

### 为什么自旋锁通常不适合生产环境

自旋锁最大的问题是它在等待期间占用 CPU。咱们分情况看：如果临界区很短（几条指令），自旋等待的开销可能比 mutex 的上下文切换开销更低。但如果临界区稍长，或者有多个线程在竞争同一把锁，自旋锁会导致 CPU 时间被大量浪费在“空转”上。更糟糕的是，在单核系统上自旋锁完全没有意义，线程在自旋期间占着 CPU，持有锁的线程根本没机会运行去释放锁，死锁就来了。

在实际项目中，优先使用 `std::mutex` 或 `std::shared_mutex`。您只有在几个条件同时满足时才考虑自旋锁：临界区极短（不超过几十条指令），竞争不激烈，运行在多核系统上。Linux 内核在中期内核抢占（preemptible kernel）中大量使用自旋锁，但内核有特殊的调度保证（关抢占），用户态没有这个条件。

### 用 atomic_flag 的更优版本

上面的 `SpinLock` 用 `std::atomic<bool>` 实现，但更规范的做法是用 `std::atomic_flag`，咱们换一版：它是标准保证 lock-free 的唯一原子类型（`std::atomic<bool>` 理论上可能不是 lock-free 的）：

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

`test_and_set` 和 `clear` 是 `atomic_flag` 的两个核心操作——前者原子地把 flag 设为 `true` 并返回旧值，后者原子地把 flag 设为 `false`。这个版本在语义上跟 `atomic<bool>` 版本完全等价，但保证了 lock-free。顺带交代一个版本差异：C++11 里 `atomic_flag` 默认构造出来的状态是未指定的，必须用 `ATOMIC_FLAG_INIT` 初始化，咱们构造函数里的 `clear()` 是双保险。C++20 起 `ATOMIC_FLAG_INIT` 不再必需，标准保留它只为兼容 C，默认构造等价于 clear——您升到 C++20 之后可以直接写 `std::atomic_flag flag_{};`。

## 什么时候轮到它们

两个模式放在一起看，咱们把选择的判据落得具体一些。

如果临界区只是一个简单的变量读取或更新（一个计数器、一个标志、一个最大值），直接用 `std::atomic` 的读改写操作（`fetch_add`、CAS 等）就够了，不需要 mutex，也不需要自旋锁。这是最轻量的选择。内存序怎么挑，咱们在[《内存序的四档：从顺序一致性到 relaxed》](../../chapter05-atomic-memory-model/04-choosing-memory-orders)备了整套判据。

如果临界区包含多个变量的协同修改（比如向一个 map 中插入元素，同时更新计数器），那 `std::atomic` 就不够用了，除非您能把多个变量打包成一个用 CAS 更新的结构体，否则老老实实用 `std::mutex`。mutex 虽然有上下文切换的开销，但它保证正确性，且在竞争不激烈时开销很低，Linux 的 futex 在无竞争时完全在用户空间完成。

如果读取频率远高于写入频率，且数据是 trivially copyable 的，SeqLock 是一个好选择，它让读取者完全无锁，代价只是偶尔重试，Linux 内核在很多高频读取场景下使用它。而自旋锁的位置在 mutex 与 atomic 之间：临界区短到 mutex 的上下文切换都嫌贵，又长到单条原子指令包不住的时候，它才有出场机会。日常工程里，咱们建议您量出锁竞争真是瓶颈，再考虑它（[《怎样调整设计》](../../chapter06-validating-designs/05-adjusting-design)）。

## 动手题

请您基于上面的 `SeqLock` 类写一个完整的程序：一个写入者线程以 10ms 的间隔更新一个包含三个 `double` 字段的结构体，四个读取者线程各自以 1ms 的间隔读取并打印数据。运行一段时间后，观察读取者是否总能获得一致的数据（三个字段的值来自同一次写入）。如果数据出现不一致（比如温度是第 5 次写入的值，但湿度是第 6 次的），检查您的 `read_begin` / `read_validate` 是否正确使用。

再把 `SpinLock` 与 `std::mutex` 放上同一张考卷：让 4 个线程各自对同一个计数器自增一百万次，分别用两种锁保护，记录总耗时与 CPU 占用。把临界区从一条自增换成十次自增再加一次 `sleep_for(1ms)`，再跑一遍——您会亲眼看到自旋锁的优势区间有多窄。

> 部分示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) 的 `code/volumn_codes/vol5/ch03-atomic-memory-model/`，其余以正文代码块为准，您可以对照着取用。

## 参考资源

- [Seqlock — Wikipedia](https://en.wikipedia.org/wiki/Seqlock)
- [Linux Kernel seqlock.h — source code](https://github.com/torvalds/linux/blob/master/include/linux/seqlock.h)
- [std::atomic_flag — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [Preshing on Programming: Atomic vs. Non-Atomic Operations](https://preshing.com/20130618/atomic-vs-non-atomic-operations/)
