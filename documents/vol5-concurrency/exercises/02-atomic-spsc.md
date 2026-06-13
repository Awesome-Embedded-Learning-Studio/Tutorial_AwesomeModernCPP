---
title: "Lab 2: Atomic Metrics and SPSC Ring Buffer"
description: "通过原子计数器和单生产者单消费者环形缓冲区，掌握 atomic、memory_order、false sharing 和基准测试方法论"
chapter: 10
order: 2
difficulty: intermediate
tags:
  - host
  - cpp-modern
  - atomic
  - memory_order
  - intermediate
cpp_standard: [17, 20]
reading_time_minutes: 16
prerequisites:
  - "卷五 ch03: 原子操作与内存模型"
  - "Lab 0: Thread Lifecycle Lab"
---

# Lab 2: Atomic Metrics and SPSC Ring Buffer

## 目标

Lab 1 全程在用 mutex 和 condition_variable——加锁、等待、唤醒，逻辑虽然清晰但开销不小。每次加锁/解锁涉及内核态的系统调用（futex），在极高频率的场景下（比如每秒百万级的消息传递），这个开销是不可接受的。这个 Lab 我们进入另一个世界：用 atomic 操作和 memory order 来实现无锁的数据交换。

我们先实现一组原子指标组件——计数器、最大值追踪器、停止标志——它们在后续 Lab 的性能监控中会被反复用到。然后实现一个固定容量的 SPSC（Single-Producer Single-Consumer）环形缓冲区，用 acquire-release 语义保证数据可见性，用 cache line padding 消除 false sharing。最后跟 Lab 1 的 mutex 队列做基准测试对比，用数据说明两种方案各自的适用场景。

## 前置知识

在开始之前，确保你已经读完以下章节：

- **ch03-01**：atomic 操作 — `atomic<T>`、`load`/`store`/`fetch_add`、is_lock_free
- **ch03-02**：内存序详解 — relaxed、acquire-release、seq_cst 的语义和开销
- **ch03-03**：memory_order_fence 与屏障 — 显式 fence 的使用场景
- **ch03-04**：atomic wait 与引用语义 — `wait`/`notify_one`/`notify_all`
- **ch03-05**：原子操作模式 — 常见的 atomic 使用模式

这个 Lab 不依赖 Lab 1 的组件，但建议先完成 Lab 1 以便理解 mutex 方案的基准对比。

## 环境准备

与 Lab 1 相同。此外，性能测试部分建议在 Linux 上运行（需要 `perf stat` 支持）。WSL2 用户可以直接使用 perf。

关闭 CPU 频率动态调节可以提高 benchmark 的稳定性（需要 sudo）：

```bash
sudo cpupower frequency-set -g performance
```

## 最终接口

### `AtomicCounter` — 原子计数器（Milestone 1）

成员变量：内部持有 `std::atomic<std::size_t>`。

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `AtomicCounter(size_t initial = 0)` | 设置初始值 | MS1 |
| increment | `void increment()` | 原子递增（`relaxed`） | MS1 |
| decrement | `void decrement()` | 原子递减 | MS1 |
| get | `size_t get() const` | 读取当前值 | MS1 |
| exchange | `size_t exchange(size_t new_val)` | 原子替换并返回旧值 | MS1 |

### `AtomicMaxTracker` — 原子最大值追踪器（Milestone 1）

成员变量：内部持有 `std::atomic<std::size_t>`。

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `AtomicMaxTracker(size_t initial = 0)` | 设置初始最大值 | MS1 |
| update | `void update(size_t value)` | CAS 循环更新最大值 | MS1 |
| get | `size_t get() const` | 读取当前最大值 | MS1 |

### `StopFlag` — 停止标志（Milestone 1）

成员变量：内部持有 `std::atomic<bool>`。

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| request_stop | `void request_stop()` | 设置停止标志（`release`） | MS1 |
| is_stop_requested | `bool is_stop_requested() const` | 检查是否停止（`acquire`） | MS1 |

### `SpscRingBuffer<T, N>` — SPSC 环形缓冲区（Milestone 2–4）

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::array<T, N>` | `buffer_` | 固定容量存储（编译期确定） |
| `alignas(64) atomic<size_t>` | `head_` | 消费者读取位置（MS4 加 cache line padding） |
| `alignas(64) atomic<size_t>` | `tail_` | 生产者写入位置（MS4 加 cache line padding） |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `SpscRingBuffer()` | 初始化 head/tail 为 0 | MS2 |
| try_push | `bool try_push(T item)` | 非阻塞写入，满则返回 false | MS2 |
| try_pop | `std::optional<T> try_pop()` | 非阻塞读取，空则返回 nullopt | MS2 |
| empty | `bool empty() const` | 缓冲区是否为空 | MS2 |
| full | `bool full() const` | 缓冲区是否已满 | MS2 |

## Milestone 1: 原子指标组件

### 目标

实现 `AtomicCounter`、`AtomicMaxTracker` 和 `StopFlag` 三个组件。重点在于为每个操作选择合适的 memory order——不是所有操作都需要默认的 `seq_cst`。

### 为什么

这三个组件是后续所有 Lab 的基础设施工具。线程池需要 `AtomicCounter` 来统计已完成的任务数，echo server 需要 `AtomicMaxTracker` 来追踪最大并发连接数，所有 Lab 都需要 `StopFlag` 来实现优雅停止。先把它们实现正确，后面就不用反复纠结 memory order 的选择了。

### 实现指引

`AtomicCounter` 的 `increment` 用 `fetch_add(1, std::memory_order_relaxed)` 就够了——我们只关心计数的准确性，不需要跟其他变量建立同步关系。`get` 用 `load(std::memory_order_relaxed)` 同理。这是因为 relaxed atomic 保证原子性（不会出现半写的值），但不保证跟其他操作的顺序——对于纯粹的计数来说，这正好是我们想要的。

`AtomicMaxTracker` 稍微复杂一点。`update` 需要一个 CAS 循环：读取当前最大值，如果新值更大就尝试替换，如果被其他线程抢先了就重试。这里用 `compare_exchange_weak` 就好——CAS 循环本身就处理了失败重试，所以 weak 版本的虚假失败不是问题。

```cpp
void update(size_t value) {
    size_t current = max_.load(relaxed);
    while (value > current) {
        if (max_.compare_exchange_weak(current, value,
                relaxed, relaxed)) {
            break;
        }
    }
}
```

`StopFlag` 是最简单的——一个 `atomic<bool>`，`request_stop` 用 `store(true, release)`，`is_stop_requested` 用 `load(acquire)`。这里的 acquire-release 对是有意义的：`request_stop` 之前的所有写操作（比如清理资源、设置状态）对调用 `is_stop_requested` 并看到 `true` 的线程可见。

### 验证

```cpp
TEST_CASE("Milestone 1: AtomicCounter under contention",
          "[lab2][milestone1]")
{
    AtomicCounter counter;
    const int kThreads = 8;
    const int kIncrements = 100000;

    std::vector<JoiningThread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < kIncrements; ++j) {
                counter.increment();
            }
        });
    }

    REQUIRE(counter.get() ==
            kThreads * kIncrements);
}

TEST_CASE("Milestone 1: AtomicMaxTracker tracks global max",
          "[lab2][milestone1]")
{
    AtomicMaxTracker tracker(0);

    std::vector<JoiningThread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&tracker, i]() {
            tracker.update(i * 10 + 5);
        });
    }

    // 最大值应该是 75 (7*10+5)
    REQUIRE(tracker.get() == 75);
}

TEST_CASE("Milestone 1: StopFlag signals stop",
          "[lab2][milestone1]")
{
    StopFlag flag;
    REQUIRE_FALSE(flag.is_stop_requested());

    flag.request_stop();
    REQUIRE(flag.is_stop_requested());
}
```

## Milestone 2: SPSC 环形缓冲区基础

### 目标

实现 `SpscRingBuffer<T, N>` 的 `try_push` 和 `try_pop`。固定容量 N，编译期确定，不支持阻塞——满了就返回 false，空了就返回 nullopt。这个 milestone 先不纠结 memory order，全部用默认的 `seq_cst`。

### 为什么

SPSC 是最简单的无锁数据结构——只有一个生产者、一个消费者，不需要考虑多线程同时修改同一个位置的问题。生产者只写 `tail_`，消费者只写 `head_`，两者通过读取对方的索引来判断缓冲区状态。这种"各自只写自己那一块"的设计是无锁编程的核心模式——消除写竞争。

### 实现指引

环形缓冲区的核心是两个索引：`head_`（消费者读取位置）和 `tail_`（生产者写入位置）。`try_push` 检查 `tail_ - head_ < N`（不满），然后写入 `buffer_[tail_ % N]`，最后递增 `tail_`。`try_pop` 检查 `head_ < tail_`（不空），读取 `buffer_[head_ % N]`，递增 `head_`。

伪代码：

```cpp

bool try_push(T item) {
    size_t tail = tail_.load(seq_cst);
    size_t head = head_.load(seq_cst);

    if (tail - head >= N) return false;  // 满了

    buffer_[tail % N] = std::move(item);
    tail_.store(tail + 1, seq_cst);
    return true;
}

optional<T> try_pop() {
    size_t head = head_.load(seq_cst);
    size_t tail = tail_.load(seq_cst);

    if (head >= tail) return nullopt;  // 空了

    T item = std::move(buffer_[head % N]);
    head_.store(head + 1, seq_cst);
    return item;
}

```

踩坑预警：索引溢出。如果 `head_` 和 `tail_` 持续递增，最终会溢出 `size_t`。在 64 位系统上这不是实际问题（2^64 次操作需要几十亿年），但如果你把类型改成了 `uint32_t` 就要小心了——溢出后 `tail - head` 的计算结果会出错。

### 验证

```cpp
TEST_CASE("Milestone 2: SPSC transfers sequential integers",
          "[lab2][milestone2]")
{
    SpscRingBuffer<int, 16> buf;
    const int kItems = 100000;

    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!buf.try_push(i)) {
                // 自旋等待
            }
        }
    });

    std::vector<int> consumed;
    int expected = 1;
    while (expected <= kItems) {
        auto val = buf.try_pop();
        if (val) {
            REQUIRE(*val == expected);
            ++expected;
        }
    }

    REQUIRE(expected == kItems + 1);
}

TEST_CASE("Milestone 2: full and empty states",
          "[lab2][milestone2]")
{
    SpscRingBuffer<int, 4> buf;

    REQUIRE(buf.empty());
    REQUIRE_FALSE(buf.full());

    REQUIRE(buf.try_push(1));
    REQUIRE(buf.try_push(2));
    REQUIRE(buf.try_push(3));
    REQUIRE(buf.try_push(4));
    REQUIRE(buf.full());

    REQUIRE_FALSE(buf.try_push(5));  // 满了

    REQUIRE(buf.try_pop() == 1);
    REQUIRE_FALSE(buf.full());  // 有空间了
    REQUIRE(buf.try_push(5));   // 现在可以了
}
```

## Milestone 3: acquire-release 优化

### 目标

把 Milestone 2 中全部使用 `seq_cst` 的 memory order 替换为更轻量的 acquire-release 语义。理解哪些 load/store 可以用 `relaxed`，哪些必须用 acquire/release。

### 为什么

`seq_cst` 是最强的 memory order——它保证所有线程看到的操作顺序是一致的，但这需要额外的同步指令（x86 上的 `MFENCE` 或 `LOCK` 前缀）。在 SPSC 场景中，我们不需要全局一致性——只需要保证生产者写入的数据对消费者可见。这正是 acquire-release 语义做的事情：生产者 `store(release)` 之前的所有写操作，对消费者 `load(acquire)` 之后可见。

### 实现指引

关键分析：`try_push` 中，写入 `buffer_[tail % N]` 必须在 `tail_.store(tail + 1, release)` 之前完成——这样消费者看到新的 `tail_` 时，`buffer_` 的内容已经就绪了。`try_pop` 中，读取 `buffer_[head % N]` 必须在 `head_.store(head + 1, release)` 之后——这样生产者看到新的 `head_` 时，`buffer_` 的内容已经被取走了，可以安全覆盖。

具体替换策略：

- `try_push` 中读取 `head_` 可以用 `relaxed`——生产者不关心消费者的精确位置，只关心"还有没有空间"，稍有延迟没关系
- `try_push` 中写入 `tail_` 必须用 `release`——保证 buffer 写入在 tail 更新之前完成
- `try_pop` 中读取 `tail_` 可以用 `relaxed`——同上
- `try_pop` 中写入 `head_` 必须用 `release`——保证 buffer 读取在 head 更新之前完成

踩坑预警：如果你错误地把 `tail_` 的 store 改成了 `relaxed`，消费者可能会看到一个尚未写入完成的数据。这种 bug 在开发时几乎不可能复现（因为 x86 的强内存模型天然保证了 store-store 顺序），但在 ARM 架构上会暴露。

### 验证

```cpp
TEST_CASE("Milestone 3: acquire-release SPSC correctness",
          "[lab2][milestone3]")
{
    // 跟 Milestone 2 一样的测试，但跑在 acquire-release 版本上
    SpscRingBuffer<int, 64> buf;
    const int kItems = 500000;

    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!buf.try_push(i)) {}
        }
    });

    int expected = 1;
    while (expected <= kItems) {
        auto val = buf.try_pop();
        if (val) {
            REQUIRE(*val == expected);
            ++expected;
        }
    }
}
```

## Milestone 4: cache line padding 与 false sharing 消除

### 目标

在 `SpscRingBuffer` 中加入 cache line padding，确保 `head_` 和 `tail_` 不共享同一个 cache line。对比 padding 前后的性能数据。

### 为什么

ch00-03 讲过 false sharing：两个原子变量如果恰好在同一个 cache line（通常 64 字节）上，一个线程修改变量 A 会让另一个线程的变量 B 所在的 cache line 失效，即使 B 根本没被修改。在 SPSC 场景中，`head_` 和 `tail_` 被不同线程高频修改——如果它们在同一个 cache line 上，每次修改都会导致对方的 cache miss，性能可能下降数倍。

### 实现指引

解决方案是在 `head_` 和 `tail_` 之间插入 padding，强制它们位于不同的 cache line。C++11 提供了 `alignas` 说明符：

```cpp

alignas(64) atomic<size_t> head_{0};
// 64 字节对齐，确保 head_ 独占一个 cache line

char padding_[64 - sizeof(atomic<size_t>)];
// 填充剩余空间（如果需要）

alignas(64) atomic<size_t> tail_{0};
// tail_ 也独占一个 cache line

```

更简洁的做法是直接用 `alignas(64)` 放在类成员声明上，编译器会自动插入 padding。在实际测试中，你应该看到 false sharing 消除后吞吐量的提升——尤其在 ARM 架构上差异会非常明显。

这个 milestone 的验证主要是性能对比。用 Catch2 的 `BENCHMARK` 宏（或者手动计时）测量同样数量的 push/pop 操作在 padding 前后的耗时。具体的数字取决于你的硬件，但你应该至少观察到量级上的差异。

### 验证

```cpp
TEST_CASE("Milestone 4: padded SPSC maintains correctness",
          "[lab2][milestone4]")
{
    SpscRingBuffer<int, 64> buf;
    const int kItems = 100000;

    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            while (!buf.try_push(i)) {}
        }
    });

    int expected = 1;
    while (expected <= kItems) {
        auto val = buf.try_pop();
        if (val) {
            REQUIRE(*val == expected);
            ++expected;
        }
    }
}

TEST_CASE("Milestone 4: benchmark padded vs unpadded",
          "[lab2][milestone4]")
{
    // 性能对比测试——不需要 REQUIRE，只需观察输出
    const int kItems = 1000000;
    const int kRounds = 10;

    // 测量当前（padded）版本
    auto padded_time = benchmark_spsc<SpscRingBuffer<int, 256>>(
        kItems, kRounds);

    // 你可以额外实现一个 UnpaddedSpscRingBuffer 来对比
    // auto unpadded_time = benchmark_spsc<UnpaddedSpscRingBuffer<int, 256>>(
    //     kItems, kRounds);

    // 报告结果（不做 REQUIRE，因为性能数字因环境而异）
    std::cout << "Padded SPSC: " << padded_time << " us\n";
}
```

## Milestone 5: 与 mutex 队列的基准测试对比

### 目标

用统一的 benchmark 方法论对比 `SpscRingBuffer`（无锁）和 `BoundedBlockingQueue`（mutex）在 SPSC 场景下的吞吐量。

### 为什么

很多朋友看到"无锁"两个字就觉得一定更快，但事实并非如此简单。在低竞争场景下，mutex 的开销其实不大（x86 上 futex 在无竞争时只是一条原子指令）；在高频单线程场景下，atomic 的 busy-wait 可能比 mutex 的 sleep-wait 消耗更多 CPU。只有用数据说话，才能搞清楚"更快"到底是在什么条件下成立的。

### 实现指引

按统一的 benchmark 方法论来测（后续 Lab 共用这套准则）：

1. **测量目标**——明确测的是吞吐量（ops/s）、延迟还是扩展性，一次只测一个。
2. **热身**——先跑 5 轮不计入，让缓存与分支预测进入稳态。
3. **多轮采集**——正式至少 10 轮，取**中位数**（不要只取平均或单次）。
4. **固定 CPU 亲和性**——用 `taskset` 或 `pthread_setaffinity_np` 把线程钉在固定核心，避免 OS 迁移核心引入噪声；区分物理核与超线程逻辑核。
5. **两组数据规模**——一组数据量在 L3 缓存内、一组超出 L3，观察缓存效应。
6. **防止结果被优化掉**——用 `benchmark::DoNotOptimize` 或写入 `volatile`，确保计算不被编译器消除；预分配内存，避免分配器锁干扰。
7. **报告格式**——测试环境、参数、结果、结论与边界（5% 以内的差异通常不显著，关注量级差异）。

伪代码：

```cpp
auto benchmark = [&](auto& queue, int items) -> double {
    // 热身
    for (int i = 0; i < 3; ++i) {
        run_spsc_benchmark(queue, items);
    }

    // 正式采集
    vector<double> samples;
    for (int i = 0; i < 10; ++i) {
        auto start = steady_clock::now();
        run_spsc_benchmark(queue, items);
        auto elapsed = steady_clock::now() - start;
        samples.push_back(elapsed in microseconds);
    }

    sort(samples);
    return samples[samples.size() / 2];  // 中位数
};
```

你的报告应该包含：CPU 型号和核数、编译器和优化级别、数据规模、中位数延迟、以及你对结论边界的说明——"这个结论只适用于 SPSC 场景，在 MPMC 场景下不成立"。

### 验证

这个 milestone 的验证不是传统的 `REQUIRE`，而是性能数据的合理性检查。你需要确认：

- 无锁版本在 SPSC 场景下确实比 mutex 版本快（通常快 2-10 倍）
- 性能差异随数据规模的变化趋势是合理的
- 你能解释为什么在某些条件下 mutex 版本可能反而更快（比如竞争极低时 mutex 的开销几乎为零）

## 自查清单

- [ ] `AtomicCounter` 使用 `relaxed` order，`StopFlag` 使用 acquire-release 对
- [ ] `AtomicMaxTracker` 的 CAS 循环正确处理并发更新
- [ ] SPSC 的数据传输无丢失、无重复、顺序正确
- [ ] acquire-release 替换 seq_cst 后测试仍然通过
- [ ] cache line padding 后 `head_` 和 `tail_` 不在同一 cache line
- [ ] 基准测试遵循统一方法论（热身、多次采集、取中位数）
- [ ] 能解释 relaxed vs acquire-release vs seq_cst 的性能差异
- [ ] 能解释 false sharing 的原理和 padding 的消除方式
- [ ] 能说明无锁方案在什么条件下优于 mutex 方案，什么条件下不一定
- [ ] TSan 下所有测试无 data race 报告
