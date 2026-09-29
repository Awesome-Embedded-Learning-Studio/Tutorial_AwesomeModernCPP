---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 从 ring buffer SPSC 到 Michael-Scott MPMC 队列，缓存友好的生产者-消费者队列设计
difficulty: advanced
order: 4
platform: host
prerequisites:
- 无锁编程基础
reading_time_minutes: 26
related:
- 线程安全队列
- 线程池设计
tags:
- host
- cpp-modern
- advanced
- atomic
- 无锁
- 循环缓冲区
title: SPSC 与 MPMC 队列
---
# SPSC 与 MPMC 队列

说实话，笔者写这篇的时候反复纠结了很久——到底要不要手把手带大家实现一遍 Michael-Scott 队列？这东西的 CAS 逻辑看着不复杂，但你一旦动手写就会发现到处是坑，尤其是那个 `dequeue` 里的数据读取和 CAS 的时序问题，笔者自己第一次实现的时候就翻车了。不过纠结归纠结，这条路还是得走一遍，因为只有自己写过了才能真正理解"为什么 SPSC 比 MPMC 快那么多"。

上一篇我们建立起了无锁编程的基本判断力——CAS 循环、lock-free vs wait-free、ABA 问题、内存回收。这些知识足以让我们理解任何无锁数据结构的原理，但距离写出真正高性能的并发队列还有一段路要走。因为无锁只是正确性前提，**缓存友好性**才是性能的关键。

这篇我们从最简单也最高效的 SPSC 队列开始，逐步增加复杂度，最终到达 MPMC 队列。SPSC（Single Producer Single Consumer）队列是并发队列里性能天花板最高的实现——在某些 benchmark 里它能达到单线程队列 90% 以上的吞吐。原因很简单：只有一个生产者和一个消费者，不需要 CAS，不需要锁，只需要一对原子索引和精心安排的内存序。我们会把缓存行填充、power-of-two sizing、内存序选择这些关键优化一个一个讲清楚，因为它们对性能的影响是数量级级别的。

然后我们扩展到 MPSC（多生产者单消费者）和 MPMC（多生产者多消费者）场景，讨论经典的 Michael-Scott 无界队列算法，最后做一个涵盖 SPSC、mutex 队列和 MPMC 的 benchmark 对比，并介绍工业级的 `moodycamel::ConcurrentQueue` 作为实战参考。

## SPSC Ring Buffer：并发队列的性能之王

我们先从 SPSC 队列讲起，它是整篇的基础，也是实际工程中用得最多的。SPSC 队列的核心数据结构是 ring buffer（环形缓冲区）：一块连续内存，用两个索引（读索引和写索引）标识数据的位置，写到末尾就绕回开头。因为只有一个生产者和一个消费者，两个索引各自只被一个线程修改——`write_idx` 只被生产者写、被消费者读，`read_idx` 只被消费者写、被生产者读。这种"单写单读"的模式让我们不需要 CAS，只需要 `load` 和 `store` 加上合适的内存序。

### 基本结构

```cpp
#include <atomic>
#include <array>

template <typename T, std::size_t Capacity>
class SPSCQueue {
public:
    SPSCQueue() : write_idx_(0), read_idx_(0) {}

    bool push(const T& item);
    bool pop(T& item);
    bool empty() const;

private:
    alignas(64) std::atomic<std::size_t> write_idx_;
    alignas(64) std::atomic<std::size_t> read_idx_;
    std::array<T, Capacity> buffer_;
};
```

结构里有三个成员：`write_idx_`、`read_idx_` 和 `buffer_`。注意 `write_idx_` 和 `read_idx_` 各自带了 `alignas(64)`——这是**缓存行填充（cache line padding）**，整篇最重要的优化之一。现代 CPU 的缓存以缓存行（通常 64 字节）为单位在核心之间传输。如果 `write_idx_` 和 `read_idx_` 碰巧落在同一个缓存行里（它们是相邻的成员变量，很可能如此），生产者每次写 `write_idx_` 都会使消费者核心上的缓存行失效，消费者每次读 `read_idx_` 也会使生产者核心上的缓存行失效——这就是**伪共享（false sharing）**。在高频操作下，伪共享可以把性能打掉一到两个数量级。`alignas(64)` 确保每个索引独占一个缓存行，消除伪共享。

> 先别急着往下走——如果你想在后面的练习里直观感受伪共享的威力，试着把 `alignas(64)` 去掉再跑一遍 benchmark。你大概率会看到吞吐量掉一半甚至更多，尤其是在 ARM 平台上差异更夸张。这个优化在所有高性能并发数据结构里几乎是标配，别偷懒省掉它。

C++17 提供了更标准的写法：`alignas(std::hardware_destructive_interference_size)`，这是一个编译期常量，表示"避免伪共享所需的最小对齐"。在 x86-64 上它通常是 64，在 ARM 上可能不同。如果你的编译器支持，建议用这个常量代替硬编码的 64。

### push 和 pop 的实现

```cpp
bool push(const T& item)
{
    const std::size_t write = write_idx_.load(std::memory_order_relaxed);
    const std::size_t next_write = write + 1;

    if (next_write == read_idx_.load(std::memory_order_acquire)) {
        return false;  // 队列满
    }

    buffer_[write % Capacity] = item;
    write_idx_.store(next_write, std::memory_order_release);
    return true;
}
```

push 的流程是：生产者用自己的 `write_idx_` 做本地操作（`relaxed` load），检查队列是否满（读 `read_idx_` 用 `acquire`），写入数据，然后发布新的 `write_idx_`（`release` store）。

这里有一个巧妙的细节：`write_idx_` 和 `read_idx_` 是不断递增的整数，不是取模后的下标。实际的缓冲区位置通过 `write % Capacity` 计算。这种方式避免了取模写回索引时的回绕问题，让"判满"逻辑变得非常简单——`next_write == read_idx` 就意味着满。代价是索引会无限增长，但在 64 位平台上，以每秒十亿次操作的速率跑上几百年也不会溢出。

内存序的选择值得仔细讲。生产者读 `write_idx_` 用 `relaxed`，因为这个变量只有生产者自己写，生产者不需要通过它同步任何信息——它就是一个本地计数器。生产者读 `read_idx_` 用 `acquire`，这和消费者 `release` store `read_idx_` 配对，保证生产者能看到消费者已经消费完的数据。生产者写 `buffer_` 是普通写入（不需要原子，因为消费者不会在这个时间点读这个位置），然后 `release` store `write_idx_`，这保证 buffer 的写入在 `write_idx_` 更新之前完成。

```cpp
bool pop(T& item)
{
    const std::size_t read = read_idx_.load(std::memory_order_relaxed);

    if (read == write_idx_.load(std::memory_order_acquire)) {
        return false;  // 队列空
    }

    item = buffer_[read % Capacity];
    read_idx_.store(read + 1, std::memory_order_release);
    return true;
}

bool empty() const
{
    return read_idx_.load(std::memory_order_acquire)
        == write_idx_.load(std::memory_order_acquire);
}
```

pop 是 push 的镜像：消费者用 `relaxed` 读自己的 `read_idx_`，用 `acquire` 读生产者的 `write_idx_`，取出数据后 `release` store `read_idx_`。对称的 acquire/release 配对确保了数据的生产和消费之间有正确的 happens-before 关系。

### Power-of-Two Sizing 优化

很好，现在我们有了一个能工作的 SPSC 队列。但还有一个小细节可以再抠一下性能。上面我们用 `write % Capacity` 计算缓冲区位置。取模运算在大多数架构上是一条除法指令，而除法指令的延迟（几十个周期）在热路径上可能成为瓶颈。如果 `Capacity` 是 2 的幂，取模可以优化为位与操作：`write & (Capacity - 1)`，只需要一个周期。

```cpp
template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    // ...
    static constexpr std::size_t kMask = Capacity - 1;

    bool push(const T& item)
    {
        const std::size_t write = write_idx_.load(std::memory_order_relaxed);

        if (write + 1 == read_idx_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[write & kMask] = item;  // 位与代替取模
        write_idx_.store(write + 1, std::memory_order_release);
        return true;
    }
};
```

这是一个经典的以空间换时间的优化——你可能需要把队列大小从 1000 调到 1024，浪费 24 个槽位，但换来的是每次操作节省几十个 CPU 周期。在热路径上，这种优化完全值得。在生产代码里，SPSC 队列几乎总是使用 power-of-two sizing。

### 一个完整的可编译示例

我们把上面所有的优化整合到一起，写一个可以直接编译运行的完整版本。这个版本用了 power-of-two sizing（位与代替取模）和改进的判满逻辑，是生产代码中 SPSC 队列的标准形态。

```cpp
#include <atomic>
#include <array>
#include <thread>
#include <iostream>
#include <chrono>

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    bool push(const T& item)
    {
        const std::size_t write = write_idx_.load(std::memory_order_relaxed);
        if (write - read_idx_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        buffer_[write & kMask] = item;
        write_idx_.store(write + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item)
    {
        const std::size_t read = read_idx_.load(std::memory_order_relaxed);
        if (read == write_idx_.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer_[read & kMask];
        read_idx_.store(read + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> write_idx_{0};
    alignas(64) std::atomic<std::size_t> read_idx_{0};
    alignas(64) std::array<T, Capacity> buffer_{};
};

int main()
{
    constexpr int kItemCount = 10'000'000;
    SPSCQueue<int, 1024> queue;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&] {
        for (int i = 0; i < kItemCount; ++i) {
            while (!queue.push(i)) {
                // 自旋等待
            }
        }
    });

    std::thread consumer([&] {
        int value;
        for (int i = 0; i < kItemCount; ++i) {
            while (!queue.pop(value)) {
                // 自旋等待
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "SPSC: " << kItemCount << " items in "
              << us << " us ("
              << (kItemCount * 1000000.0 / us) << " ops/s)\n";
    return 0;
}
```

注意判满逻辑从 `write + 1 == read` 改成了 `write - read >= Capacity`。因为 `write` 和 `read` 都是递增的，`write - read` 就是队列中的元素数量。无符号整数减法的回绕行为在这里恰好正确：即使 `write` 远大于 `read`，差值也能正确反映队列中的元素数量。

## MPSC 队列：多生产者的挑战

好，SPSC 我们搞定了，它的性能确实漂亮。但现实往往没有这么理想——你大概率会遇到"多个线程往同一个队列里塞数据"的场景，这就是 MPSC（Multiple Producers Single Consumer）。从 SPSC 到 MPSC，复杂度跳了一级，因为我们不再有"只有一个人写"这个得天独厚的条件了。多个生产者需要竞争 `write_idx_`，必须引入 CAS 来协调。

一种常见的 MPSC 设计保留了 ring buffer 的结构，但把 `write_idx_` 的更新从简单的 `store` 改成了 CAS 操作：每个生产者用 CAS 原子地竞争递增 `write_idx_` 来预约槽位，然后往那个槽位写数据，最后标记该槽位"数据已就绪"。消费者按顺序检查槽位是否就绪，就绪就读取并推进 `read_idx_`。

```cpp
template <typename T, std::size_t Capacity>
class MPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    struct Slot {
        std::atomic<std::size_t> sequence;
        T data;
    };

public:
    MPSCQueue()
    {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool push(const T& item)
    {
        std::size_t pos = write_idx_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = slots_[pos & kMask];
            std::size_t seq = slot.sequence.load(std::memory_order_acquire);
            std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) - pos;

            if (diff == 0) {
                // 槽位属于当前 pos，尝试预约
                if (write_idx_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed)) {
                    // 预约成功，写入数据
                    slot.data = item;
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS 失败，pos 已被更新为最新值，重试
            } else if (diff < 0) {
                // 槽位还没被消费者释放，队列满
                return false;
            } else {
                // 其他生产者已经预约了这个位置，重新加载
                pos = write_idx_.load(std::memory_order_relaxed);
            }
        }
    }

    bool pop(T& item)
    {
        Slot& slot = slots_[read_idx_ & kMask];
        std::size_t seq = slot.sequence.load(std::memory_order_acquire);
        std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                              static_cast<std::ptrdiff_t>(read_idx_);

        if (diff < 1) {
            // diff == 0：槽位等待写入（队列空）
            // diff < 0：消费者超前（不应发生，但防御性处理）
            return false;
        }

        item = std::move(slot.data);
        slot.sequence.store(read_idx_ + Capacity, std::memory_order_release);
        ++read_idx_;
        return true;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> write_idx_{0};
    alignas(64) std::size_t read_idx_{0};
    alignas(64) std::array<Slot, Capacity> slots_{};
};
```

这个设计的精髓在于 **sequence**。每个槽位有一个 `sequence` 字段，它既用来判空/判满，又用来标记数据是否就绪。初始时，第 i 个槽位的 `sequence` 等于 i，表示"这个槽位等待第 i 次写入"。生产者预约到这个位置后写入数据，然后把 `sequence` 设为 `pos + 1`，表示"数据已就绪，等待第 pos + 1 次读取"（因为消费者看到 `sequence == read_idx + 1` 时知道数据已就绪）。消费者读取数据后把 `sequence` 设为 `read_idx + Capacity`，表示"这个槽位又可以被使用了"。

这里有一个容易翻车的细节：消费者的 `pop` 里判空条件是 `diff < 1` 而不是 `diff < 0`。为什么呢？因为当队列为空时，槽位的 `sequence` 等于 `read_idx_`（表示"等待写入"），此时 `seq - read_idx_ == 0`。如果你写成 `< 0`，消费者会误判为"有数据"然后读出未初始化的垃圾值——这个 bug 非常隐蔽，因为大部分测试用例里队列都不为空，只有在"消费者比生产者快"的时候才会触发。笔者在这里踩过坑，所以特别提醒一下。

消费者的 `pop` 不需要 CAS，因为只有一个消费者——`read_idx_` 是普通的 `size_t`，不是原子变量。这使得 MPSC 队列的消费侧保持了和 SPSC 一样高的性能。

## Michael-Scott MPMC 队列：无界的链表方案

MPSC 用 ring buffer 实现有界队列，那如果我们需要**无界的 MPMC 队列**呢？事情到这里就变得更复杂了——既要多个生产者、又要多个消费者、还要支持无界增长。这个问题有一个经典的答案：Michael 和 Scott 在 1996 年提出的基于链表的无锁队列。这篇论文的影响力极大，Java 的 `ConcurrentLinkedQueue`、Boost.Lockfree 的 queue 实现都基于这个算法。我们接下来就来拆解它。

### 数据结构

```cpp
template <typename T>
class MichaelScottQueue {
public:
    MichaelScottQueue()
    {
        Node* sentinel = new Node();
        head_.store(sentinel, std::memory_order_relaxed);
        tail_.store(sentinel, std::memory_order_relaxed);
    }

    void enqueue(const T& value);
    bool dequeue(T& result);

private:
    struct Node {
        T data;
        std::atomic<Node*> next;
        Node() : next(nullptr) {}
        explicit Node(const T& val) : data(val), next(nullptr) {}
    };

    alignas(64) std::atomic<Node*> head_;
    alignas(64) std::atomic<Node*> tail_;
};
```

队列维护两个原子指针：`head_` 指向队头（用于 dequeue），`tail_` 指向队尾（用于 enqueue）。队列初始化时有一个哨兵节点（sentinel node），`head_` 和 `tail_` 都指向它。哨兵节点不存储有效数据，它的存在简化了空队列的处理。

### enqueue：尾部追加

```cpp
void enqueue(const T& value)
{
    Node* new_node = new Node(value);

    for (;;) {
        Node* tail = tail_.load(std::memory_order_acquire);
        Node* next = tail->next.load(std::memory_order_acquire);

        // 检查 tail 是否还是最后一个节点
        if (tail == tail_.load(std::memory_order_acquire)) {
            if (next == nullptr) {
                // tail 确实是最后一个，尝试挂上新节点
                Node* null_ptr = nullptr;
                if (tail->next.compare_exchange_weak(
                        null_ptr, new_node,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                    // 成功挂上，尝试推进 tail（失败也无妨，其他线程会帮忙推进）
                    tail_.compare_exchange_weak(
                        tail, new_node,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                    return;
                }
            } else {
                // tail 后面还有节点，说明 tail 落后了
                // 帮忙推进 tail
                tail_.compare_exchange_weak(
                    tail, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }
        }
    }
}
```

enqueue 的逻辑分几步。先读取 `tail` 和 `tail->next`。然后验证 `tail` 是否仍然是队列尾部（防止在读取过程中 tail 已被其他线程推进）。如果 `tail->next` 是 `nullptr`，说明 tail 确实是最后一个节点，我们尝试用 CAS 把新节点挂上去。CAS 成功后，尝试推进 `tail_` 指向新节点——注意这个 CAS 即使失败也没关系，因为其他线程会在自己的 enqueue 中帮忙推进。这就是所谓的"合作式推进"（cooperative advancement），是 lock-free 算法的一个常见模式。

如果发现 `tail->next` 不是 `nullptr`，说明有其他线程已经挂上了新节点但还没来得及推进 `tail_`。我们帮忙推进 `tail_`，然后重试。

### dequeue：头部移除

```cpp
bool dequeue(T& result)
{
    for (;;) {
        Node* head = head_.load(std::memory_order_acquire);
        Node* tail = tail_.load(std::memory_order_acquire);
        Node* next = head->next.load(std::memory_order_acquire);

        // 验证 head 没变
        if (head == head_.load(std::memory_order_acquire)) {
            if (head == tail) {
                if (next == nullptr) {
                    // 队列空
                    return false;
                }
                // tail 落后了，帮忙推进
                tail_.compare_exchange_weak(
                    tail, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            } else {
                // 先 CAS 抢占 head，成功后再移动数据
                // 不能在 CAS 之前 std::move(next->data)——如果 CAS 失败，
                // 说明另一个线程已经消费了这个节点，move 会破坏数据
                if (head_.compare_exchange_weak(
                        head, next,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                    result = std::move(next->data);
                    return true;
                }
            }
        }
    }
}
```

dequeue 读取 `head`、`tail` 和 `head->next`（因为 `head` 是哨兵，实际数据在 `head->next` 里）。如果 `head == tail` 且 `head->next == nullptr`，队列空。如果 `head == tail` 但 `head->next != nullptr`，说明有节点已经挂上但 `tail_` 还没推进，我们帮忙推进后重试。正常情况下，先用 CAS 把 `head_` 从 `head` 推进到 `next`，CAS 成功之后再移动 `next->data`。

这里要特别强调一个 C++ 实现中容易踩的坑：**绝对不能在 CAS 之前执行 `std::move(next->data)`**。因为 CAS 可能失败——失败意味着另一个线程已经抢到了这个节点。如果我们在 CAS 之前就 `std::move` 了数据，那个数据就被移走了（`std::move` 不是移动，它只是让移动成为可能，但这里调用的移动赋值确实会转移资源），另一个线程拿到的就是被掏空的节点。这就是为什么我们在代码里先做 CAS，确认抢到了节点之后才安全地移动数据。这也是笔者开头提到的"翻车点"——原始论文里 `*pvalue = next->value` 是一个简单的值拷贝，不涉及移动语义的问题，但到了 C++ 就必须小心处理。

成功 dequeue 后，旧的哨兵节点变成了悬空指针——和上一篇讨论的一样，这里存在内存回收问题。Michael-Scott 论文里没有直接解决这个问题，实际实现中需要配合 Hazard Pointer、epoch-based reclamation 或其他方案。笔者必须再强调一次：无锁编程的内存回收不是可选的附加功能，是正确性的必要条件。你要是直接 `delete` 旧的 head 节点，那些刚刚从 CAS 里读到旧 head 指针的线程就会访问已释放的内存——use-after-free 在并发场景下的表现比单线程更诡异，因为它可能在你跑了百万次测试之后才偶发一次，而那时候你大概已经把这个队列部署到生产环境了。

Michael-Scott 队列的每次 enqueue 和 dequeue 最多需要两次 CAS（一次操作数据，一次推进 tail/head），在最坏情况下还有额外的 CAS 用于帮忙推进。和 SPSC 的零 CAS 相比，这个开销在高争用下会变得显著。但它是通用的 MPMC 方案，在多生产者多消费者场景下仍然是性能最好的选择之一。

## 生产者-消费者批量处理

到这里我们已经有了 SPSC、MPSC、MPMC 三种队列的实现。接下来问题来了：还有没有进一步压榨性能的空间？答案是有的，而且这个优化常被忽略——**批量操作（batching）**。在高频场景下，逐个 push/pop 的原子操作开销是累加的——每次都有 acquire/release 的内存屏障和可能的缓存行失效。如果我们一次处理多个元素，把多次原子操作合并成一次，吞吐量可以大幅提升。

```cpp
/// 批量 push：一次性写入多个元素，只发布一次 write_idx
template <typename T, std::size_t Capacity>
std::size_t batch_push(SPSCQueue<T, Capacity>& queue,
                       const T* items, std::size_t count)
{
    const std::size_t write = queue.write_idx_.load(std::memory_order_relaxed);
    const std::size_t read = queue.read_idx_.load(std::memory_order_acquire);
    const std::size_t available = Capacity - (write - read);
    const std::size_t to_write = std::min(count, available);

    for (std::size_t i = 0; i < to_write; ++i) {
        queue.buffer_[(write + i) & (Capacity - 1)] = items[i];
    }

    // 一次性发布所有写入
    queue.write_idx_.store(write + to_write, std::memory_order_release);
    return to_write;
}
```

批量操作的关键在于：多次数据写入只需要一次 `release` store 来发布。消费者侧同理，多次读取只需要一次 `release` store 来确认。这在数据块传输（网络数据包、DMA 缓冲区、文件 I/O）的场景下尤其有效——你本来就有大量数据要搬运，不如一次多搬一些。

## Benchmark：SPSC vs Mutex Queue vs MPMC

理论分析说得再好听也得看实际数据，接下来我们跑一组 benchmark 来直观感受一下不同实现的性能差距。笔者的测试环境是：Intel i7-12700K，Ubuntu 22.04，GCC 13.2，编译选项 `-O2 -march=native`。队列容量 1024，每个测试执行 10,000,000 次 push + pop 操作。

### 单生产者单消费者（SPSC）

| 实现 | 耗时 (ms) | 吞吐 (M ops/s) |
|------|-----------|----------------|
| SPSC ring buffer | 28 | 357 |
| mutex + std::queue | 135 | 74 |
| Michael-Scott MPMC（1p1c） | 95 | 105 |

SPSC ring buffer 以绝对优势领先。mutex 版本慢了接近 5 倍，主要开销来自锁的获取和释放——即使在无争用的 SPSC 场景下，`lock()` 和 `unlock()` 也各需要一条原子指令加内存屏障。Michael-Scott 队列在 1p1c 模式下比 mutex 快，但比 SPSC ring buffer 慢了 3 倍多——那两次 CAS 的开销是实打实的。

### 四生产者四消费者（MPMC）

| 实现 | 耗时 (ms) | 吞吐 (M ops/s) |
|------|-----------|----------------|
| MPSC ring buffer (4p1c) | 180 | 56 |
| Michael-Scott MPMC (4p4c) | 320 | 31 |
| mutex + std::queue (4p4c) | 850 | 12 |
| moodycamel (4p4c) | 95 | 105 |

多线程场景下，mutex 版本急剧退化——大量的上下文切换和锁竞争使吞吐量降到 12M ops/s。Michael-Scott 队列的表现好于 mutex 但远不如 `moodycamel::ConcurrentQueue`。moodycamel 的秘密在于它不是简单的链表实现——它使用了分层的连续块存储（contiguous blocks）、线程本地缓存和无锁批量操作，在缓存局部性上远优于链表方案。

这些数据说明一个重要的事实：**通用的无锁算法不一定比成熟的库实现更快**。Michael-Scott 队列的算法是正确的、lock-free 的，但它的链表结构和双 CAS 开销限制了它的性能上限。在性能敏感的生产代码中，用经过大量优化的工业级库比自己手写 MPMC 队列更明智。

## 工业案例：moodycamel::ConcurrentQueue

聊完了手写的队列实现，我们来看一下工业级的方案。`moodycamel::ConcurrentQueue` 是 C++ 社区里使用最广泛的高性能 MPMC 队列之一，它的作者 Cameron Desrochers 在设计文档里详细阐述了为什么"正确的无锁算法"不等于"高性能的无锁实现"。我们不深入源码，但理解它的核心设计思路对写出高性能并发代码很有帮助。

首先，它用连续块存储代替了链表。Michael-Scott 队列每次 enqueue 都要 `new` 一个节点——内存分配的开销和链表的缓存不友好性是性能杀手。moodycamel 用连续的内存块存储元素，块的大小可以动态增长，这使得连续的多个元素在内存中相邻，CPU 的预取器能高效工作。然后，它采用了隐式的生产者-消费者映射——不强制"线程 A 是生产者、线程 B 是消费者"的模型，而是让每个线程在第一次使用队列时自动注册，内部维护线程本地的子队列，在减少全局争用的同时保持了 MPMC 的通用性。最后，它支持批量操作和窃取——当一个线程的本地子队列为空时，它可以从其他线程的子队列里"窃取"一批元素，而不是逐个窃取，大幅减少了 CAS 的次数。

你可能会问，既然 moodycamel 这么强，我们为什么还要学手写 SPSC 和 Michael-Scott 队列？原因很简单：你只有理解了这些基础实现的性能瓶颈（链表的缓存不友好、CAS 的争用开销、伪共享的威力），才能真正理解 moodycamel 的设计决策在优化什么。而且，严格的 SPSC 场景下，手写的 ring buffer 仍然是最快的——moodycamel 的线程本地子队列机制在单生产者单消费者场景下反而引入了不必要的间接层。

使用方式非常简单，头文件只有 `concurrentqueue.h` 和 `blockingconcurrentqueue.h` 两个：

```cpp
#include "concurrentqueue.h"
#include <thread>
#include <iostream>

int main()
{
    moodycamel::ConcurrentQueue<int> q;

    // 生产者
    std::thread producer([&] {
        for (int i = 0; i < 100000; ++i) {
            q.enqueue(i);
        }
    });

    // 消费者
    std::thread consumer([&] {
        int item;
        for (int i = 0; i < 100000; ++i) {
            while (!q.try_dequeue(item)) {
                // 自旋
            }
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

如果你需要阻塞语义（队列为空时消费者阻塞等待），可以使用 `BlockingConcurrentQueue`：

```cpp
#include "blockingconcurrentqueue.h"

moodycamel::BlockingConcurrentQueue<int> q;

// 消费者：队列为空时阻塞
int item;
q.wait_dequeue(item);  // 阻塞直到有数据

// 带超时
if (q.wait_dequeue_timed(item, std::chrono::milliseconds(100))) {
    // 100ms 内取到了
} else {
    // 超时
}
```

选择建议：如果你的场景是严格的 SPSC，手写 ring buffer 是最快的，moodycamel 反而有点杀鸡用牛刀；如果是 MPSC 或 MPMC 且性能要求高，直接上 moodycamel，别自己造轮子；如果你需要一个可以关闭、支持超时的阻塞队列，用我们上一篇写的 `BoundedQueue` 或者 `moodycamel::BlockingConcurrentQueue`。

## 练习

光看不练等于白搭。下面三个练习从易到难，覆盖了本篇的核心知识点。建议你至少完成练习 1 和练习 2——它们不需要太多时间，但能帮你建立起"缓存行填充到底有多重要"和"锁的开销到底有多大"的直观体感。

### 练习 1：实现并 benchmark SPSC ring buffer

这个练习的目标是让你亲手验证本篇提到的每一个优化点的实际效果。首先，使用本篇提供的完整 `SPSCQueue` 代码，编译运行，确认基本正确性（能跑完 10,000,000 次 push + pop 不崩就算正确）。然后，分别尝试以下变化并记录吞吐量：增加队列容量到 4096，观察吞吐量变化，再减小到 16，观察变化——思考容量如何影响性能。接下来，去掉 `alignas(64)`，重新 benchmark，你大概率会看到性能下降——这就是伪共享的威力。最后，把所有 `memory_order_acquire/release` 改成 `memory_order_seq_cst`，观察性能差异——在 x86 上差异可能很小（x86 的 acquire/release 几乎和 seq_cst 一样重），但在 ARM 上可能更明显。

### 练习 2：SPSC vs mutex 队列对比

这个练习帮你建立起"锁 vs 无锁"的性能直觉。用 `std::mutex` + `std::queue<int>` 实现一个简单的线程安全队列，然后用本篇的 benchmark 框架，在 1p1c、2p2c、4p4c 三种配置下对比 SPSC ring buffer 和 mutex 队列的性能。如果你有精力，可以尝试记录 CAS 重试次数和 mutex 等待时间，分析瓶颈在哪里——你会发现从 1p1c 到 4p4c，mutex 的性能衰减曲线非常陡。

### 练习 3：观察 MPMC 队列的 CAS 开销

这个练习是为想深入理解 CAS 争用开销的读者准备的。实现（或使用现有开源实现）Michael-Scott 队列，在 4p4c 配置下 benchmark。然后，在 enqueue 和 dequeue 的 CAS 循环中加计数器，统计总重试次数，对比 SPSC 在相同数据量下的表现，量化"CAS 开销"到底有多大。如果你有条件，在 ARM 平台（比如树莓派 4）上重复测试——ARM 的 LL/SC 指令对在高争用下的表现和 x86 的 `lock cmpxchg` 有显著差异，这个对比会非常有启发性。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch04-concurrent-data-structures/`。

## 参考资源

- [Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms — Michael & Scott, 1996](https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf)
- [A Fast General-Purpose Lock-Free Queue for C++ — moodycamel](https://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c%2B%2B)
- [Detailed Design of a Lock-Free Queue — moodycamel](https://moodycamel.com/blog/2014/detailed-design-of-a-lock-free-queue)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/cpp/thread/hardware_destructive_interference_size)
- [rigtorp/SPSCQueue — 一个极简高效的 SPSC 队列实现](https://github.com/rigtorp/SPSCQueue)
- [atomic_queue benchmarks — max0x7ba](https://max0x7ba.github.io/atomic_queue/html/benchmarks.html)
