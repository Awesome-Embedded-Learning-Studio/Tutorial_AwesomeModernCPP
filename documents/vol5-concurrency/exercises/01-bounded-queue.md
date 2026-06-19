---
title: "Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives"
description: "实现固定容量阻塞队列、关闭语义、超时、分片锁缓存，训练 mutex/condition_variable/C++20 同步原语"
chapter: 10
order: 1
tags:
  - host
  - cpp-modern
  - mutex
  - intermediate
difficulty: intermediate
platform: host
reading_time_minutes: 20
cpp_standard: [17, 20]
prerequisites:
  - "卷五 ch02: 互斥量、条件变量与同步原语"
  - "Lab 0: Thread Lifecycle Lab"
related:
  - "mutex 与 RAII 守卫"
  - "condition_variable"
  - "latch/barrier/semaphore"
---

# Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives

> 本 Lab 配套可运行工程在 [`code/volumn_codes/vol5-labs/templates/lab1_bounded_queue/`](../../../code/volumn_codes/vol5-labs/templates/lab1_bounded_queue/)。动手工作量约 **8–12 小时**（`reading_time_minutes` 是纯阅读分钟数，不是动手时间）。

## 目标

Lab 0 跑通了多线程骨架——创建线程、RAII 包装、参数安全传递。但那些线程都是"各干各的"，主线程只是等它们结束。真实并发系统不是这样：线程之间要协作，生产者往队列塞数据、消费者取，队列满了要背压，队列关了要优雅退出。

这个 Lab 的核心产物是三件东西：

1. **`BoundedBlockingQueue<T>`**——带关闭语义的固定容量阻塞队列（MS1-4 演进）。**Lab 3 的 ThreadPool 会直接复用它做任务队列**，所以接口要一次定稳。
2. **`ConcurrentCache<K, V>`**——分片锁并发缓存（MS5），练"粗粒度锁 vs 细粒度锁"的权衡。
3. **C++20 同步原语实践**（MS6）——用 `std::latch` / `barrier` / `counting_semaphore` 实现三种经典并发模式。

完成后，你应该对 mutex + condition_variable 的组合拳有肌肉记忆，能正确处理**谓词等待、虚假唤醒、丢失唤醒、关闭唤醒**这四种等待场景，并理解锁粒度的性能权衡。

## 前置知识

- **ch02-01** mutex 与 RAII 守卫 — `std::mutex`、`lock_guard`、`unique_lock`
- **ch02-03** condition_variable — 谓词等待、虚假唤醒、`notify_one` vs `notify_all`
- **ch02-05** latch / barrier / semaphore — C++20 同步原语
- **Lab 0** — `JoiningThread`（本 Lab 的测试和示例里会用到）

## 工程脚手架（先把这个跑起来）

每个 Lab 在 [vol5-labs/](../../../code/volumn_codes/vol5-labs/) 下有两份：**`templates/lab1_bounded_queue/`** 是空实现骨架（你拷贝去做），**`examples/lab1_bounded_queue/`** 是参考实现（卡住可对照，别先抄）。两份都是 standalone 工程。你要做的是 templates 那份：

```text
templates/lab1_bounded_queue/
├── CMakeLists.txt       # standalone: FetchContent Catch2 + INTERFACE 库 + test
├── include/lab1/        ← 你在这里补全实现
│   ├── bounded_blocking_queue.h   #   MS1-4
│   ├── concurrent_cache.h         #   MS5
│   └── sync_practice.h            #   MS6
└── test/                # 教程提供的测试（不用改）
    └── test_milestone1.cpp … test_milestone6.cpp
```

**注意 lab1 是 C++20**（不是 lab0 的 C++17），因为 MS6 要用 `std::latch/barrier/counting_semaphore`。

```bash
cd code/volumn_codes/vol5-labs/templates/lab1_bounded_queue
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # Debug 默认开 ThreadSanitizer
cmake --build build
```

**预期：构建停在链接阶段，报 `undefined reference to lab1::BoundedBlockingQueue<...>::push(...)`** —— 这是故意的，`include/lab1/*.h` 只有声明没有实现。按 milestone 顺序补全，对应测试从红变绿。

## 最终接口

动手前看清目标形状（和 `include/lab1/` 的头文件完全一致）。

### `BoundedBlockingQueue<T>` — MS1-4 演进（接口不变，内部逐步补全）

| 方法 | 签名 | Milestone |
|------|------|-----------|
| 构造 | `explicit BoundedBlockingQueue(std::size_t capacity)` | MS1 |
| 阻塞 push | `void push(T value)` — 满则等；close 后抛 `std::runtime_error` | MS1/MS2 |
| 阻塞 pop | `std::optional<T> pop()` — 空则等；close 且空返 `nullopt` | MS1/MS2 |
| 关闭 | `void close()` — 唤醒所有阻塞者 | MS2 |
| 查关闭 | `bool is_closed() const noexcept` | MS2 |
| 超时 push | `bool try_push_for(T, std::chrono::nanoseconds)` — 成功 true；超时或 close 返 false | MS3 |
| 超时 pop | `std::optional<T> try_pop_for(std::chrono::nanoseconds)` | MS3 |
| 近似大小 | `std::size_t size() const noexcept` | MS4 |

### `ConcurrentCache<K, V, Hash>` — MS5（分片锁）

| 方法 | 签名 |
|------|------|
| 构造 | `explicit ConcurrentCache(std::size_t shard_count = 16)` |
| 查 | `std::optional<V> get(const K&) const` |
| 写 | `void put(K key, V value)` |
| 删 | `bool erase(const K&)` |
| 大小 | `std::size_t size() const noexcept` |

### `sync_practice` — MS6（三个自由函数，各用一种 C++20 原语）

| 函数 | 用的原语 | 为什么是它 |
|------|----------|-----------|
| `fork_join_sum(n, task)` | `std::latch` | 一次性"等 N 个任务全完成"（countdown 到 0） |
| `two_phase_sum(n, val)` | `std::barrier` | 多阶段、阶段间同步（可复用） |
| `measure_max_concurrency(n, max)` | `std::counting_semaphore` | "允许 N 个并发"的计数信号量 |

接下来按 milestone 拆解。

## Milestone 1: 阻塞 push / pop

### 目标

实现 `push`（队列满时阻塞等空间）和 `pop`（队列空时阻塞等数据）。先把"多线程通过队列传递数据"这件事跑通，close 和超时是后面的事。

### 为什么先做这一步

这是 mutex + condition_variable 组合拳的最基本形态。后面所有 milestone（关闭唤醒、超时等待）都在这个结构上加分支，所以这一步的骨架要搭对。

### 实现指引

核心是一个固定容量的环形缓冲（或直接用 `std::queue<T>`）+ 一把 `mutex` + 两个 `condition_variable`（`not_full_` 给生产者、`not_empty_` 给消费者）。两个设计点：

**第一，等待必须用谓词（predicate），不能裸 `wait()`。** 生产者要"等到有空位"：

```cpp
std::unique_lock lock(m_);
not_full_.wait(lock, [this] { return queue_.size() < capacity_; });  // ← 谓词
queue_.push(std::move(value));
not_empty_.notify_one();
```

那个 lambda 谓词是命根子。如果写成 `not_full_.wait(lock)`（无谓词），就会吃**虚假唤醒**和**丢失唤醒**的亏：操作系统允许 `wait` 莫名其妙返回（spurious wakeup），或者 notify 发在你进 wait 之前（lost wakeup）——两种情况你都会在"其实没空位"时往下走，塞爆队列。谓词 wait 在返回后**重新检查条件**，把这两种坑都堵死。

**第二，`notify_one` 还是 `notify_all`？** MS1 单生产者单消费者场景 `notify_one` 就够（只唤醒一个等待者）。但到了 MS2 的 close，必须 `notify_all`（要唤醒所有阻塞的消费者让它们退出）。现在用 `notify_one`，MS2 再改。

> **踩坑预警**：永远不要在持有锁的时候 `notify`——不是说错，而是"先 unlock 再 notify"能避免"被唤醒的线程立刻又抢锁失败、继续睡"的无谓上下文切换。但 predicate wait 的标准写法（`wait` 返回时仍持锁，函数返回时析构 lock 释放）已经隐含了正确顺序，别画蛇添足手动 unlock 再 notify 又重新 lock。

### 验证

> **别被测试骗了**：`test_milestone1` 测的是"能放能取、FIFO、阻塞行为、多生产者不丢不重"——**全是行为，不查你用没用 predicate wait**。你完全可以用裸 `wait()`（无谓词）蒙混过测试（碰巧没触发虚假唤醒）。但那是定时炸弹：高并发或特定调度下必炸。**真正的验收标准：`push`/`pop` 的等待都是谓词 wait（`cv.wait(lock, predicate)`），没有裸 `wait()`。** 用 TSan 跑 MS4 的压力测试，裸 wait 迟早会以 data race 或越界暴露。

[`test/test_milestone1.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab1_bounded_queue/test/test_milestone1.cpp) 覆盖四个场景：单 push/pop、FIFO、pop 阻塞直到 push、多生产者并发不丢不重。

## Milestone 2: close 语义

### 目标

实现 `close()`：唤醒所有正在阻塞的 push/pop；close 之后 push 抛异常、pop 取完剩余元素后返 `nullopt`。

### 为什么

MS1 的队列没法"结束"——消费者 `while (auto v = q.pop())` 会永远等下去。真实的生产者-消费者必须有"生产完毕"的信号，消费者据此退出。`close()` 就是这个信号：它让 `pop` 在队列耗尽后返回 `nullopt`，消费者循环自然结束。

### 实现指引

`close` 里要：加锁、置 `closed_ = true`、**`notify_all()` 两个 cv**（唤醒所有阻塞的生产者和消费者）。然后 push 和 pop 的谓词都要加上 `closed_` 条件：

```cpp
// push: 既等"有空位"，也要在 close 时立刻失败（抛）
not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
if (closed_) throw std::runtime_error("push on closed queue");
// ...

// pop: 既等"有数据"，close 后队列空了也要立刻返回 nullopt
not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
if (queue_.empty()) return std::nullopt;   // 一定是 closed_ && 空
// ...
```

**关键：`close` 必须 `notify_all`**（不是 `notify_one`）。因为可能有多个消费者阻塞在 `pop`，你要把它们全唤醒——`notify_one` 只唤醒一个，剩下的永远卡着（死锁）。

> **踩坑预警**：close 后 pop 的语义是"取完剩余 → nullopt"，不是"立刻 nullopt"。队列里 close 之前已塞入的元素，消费者必须还能取走。所以 pop 谓词是 `!queue_.empty() || closed_`——先满足取数据，空了才看 closed。

### 验证

> **别被测试骗了**：`test_milestone2` 测 close 后 push 抛、pop 取完返 nullopt、close 唤醒消费者退出。但它**不验证 close 是不是真的唤醒了所有阻塞者**——如果你手滑写了 `notify_one`，测试里只有一个消费者，照样过。**真正的验收标准：`close()` 里对两个 cv 都是 `notify_all()`。** 多消费者场景在 MS4 压力测试里会暴露 notify_one 的死锁。

## Milestone 3: 超时 try_push_for / try_pop_for

### 目标

给 push/pop 加超时版本：等不到就放弃，返回失败（push 返 false、pop 返 nullopt），不抛、不死等。

### 为什么

阻塞版可能永远等下去——这是死锁的温床（比如生产者和消费者互相等对方）。超时版给一个"放弃并继续"的出口，在真实系统里用于探活、降级、避免永久阻塞。

### 实现指引

用 `condition_variable::wait_for(lock, timeout, predicate)`。和 MS1 一样**必须有谓词**——`wait_for` 也会虚假唤醒，没谓词你会在超时前就误返回。返回值表示"是否因谓词满足而返回"（true）还是"超时"（false）：

```cpp
bool try_push_for(T value, std::chrono::nanoseconds timeout) {
    std::unique_lock lock(m_);
    bool ok = not_full_.wait_for(lock, timeout,
        [this] { return queue_.size() < capacity_ || closed_; });
    if (!ok || closed_) return false;   // 超时或已关闭
    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

注意 `wait_for` 返回 false 不代表 close——只代表超时。所以之后还要单独判 `closed_`。

### 验证

> **别被测试骗了**：`test_milestone3` 测超时返回值和时间，**不查你 wait_for 有没有带谓词**。裸 `wait_for(lock, timeout)`（无谓词）碰上虚假唤醒会提前返回、时间不到就退出，但测试的时间断言有 10ms 容差，可能蒙混过。**真正的验收标准：`wait_for` 带谓词，返回后用返回值 + `closed_` 双重判断。**

## Milestone 4: 背压与并发压力

### 目标

用小容量队列跑真实的 MPMC（多生产者多消费者）压力，验证容量限制生效、不丢不重、TSan 干净。

### 为什么

前面三个 milestone 是"单点正确"，MS4 是"系统正确"——多个生产者和消费者真正并发时，你的锁、cv、谓词会不会在压力下露馅。这是 BoundedBlockingQueue"能用"的硬门槛。

### 实现指引

基本不用再加新代码——MS1-3 的实现对就够。这一步的重点是**理解容量限制**：`capacity_` 是硬上限，生产者在队列满时**必须阻塞**（背压），而不是无限扩容。如果你的 `push` 不阻塞、改成动态扩容，那就不是"有界"队列了——测试的 `size()` 断言和 MS4 的背压语义都会失效。

跑测试前想清楚多消费者退出的时序：生产者全部 push 完 → 主线程 `close()` → 消费者的 `while (auto v = q.pop())` 取完剩余后收到 `nullopt` 退出。这个链路靠的是 MS2 的 `notify_all` + nullopt 语义，MS1 单消费者时测不出来。

### 验证

> **别被测试骗了**：`test_milestone4` 的 MPMC 压力测"不丢不重 + size 跟踪"。如果你偷偷把队列改成无界（push 永不阻塞），不丢不重照样成立、测试照过——但你失去了背压能力，Lab 3 的 ThreadPool 用它时会被 OOM。**真正的验收标准：队列大小永远 ≤ capacity（`push` 在满时真的阻塞），靠 MS2 的 close 让多消费者退出。** TSan 下这个压力测试必须零 race。

## Milestone 5: ConcurrentCache（分片锁）

### 目标

实现分片锁并发缓存：key 按哈希分到 `shard_count` 个 shard，每个 shard 一把独立 mutex，不同 shard 可并行。

### 为什么

这是"粗粒度锁 vs 细粒度锁"的经典权衡。朴素做法是整个缓存一把锁——所有线程串行访问，吞吐被锁竞争卡死。分片后，不同 key 落在不同 shard，读写可以真正并行，吞吐随 shard 数线性提升（直到碰到别的瓶颈）。

### 实现指引

内部是 `std::vector<Shard>`，每个 Shard 持有自己的 `mutex` + `unordered_map`。定位 shard：`shard_idx = hash(key) % shard_count`（shard_count 取 2 的幂时可以用 `& (shard_count - 1)` 位运算，更快）。`shard_count` 建议 16（够分散，开销可控）。

```cpp
template <typename K, typename V, typename Hash = std::hash<K>>
class ConcurrentCache {
    struct Shard {
        mutable std::mutex m;
        std::unordered_map<K, V> map;
    };
    std::vector<Shard> shards_;
    Hash hash_{};
    // get/put/erase: hash(key) % shards_.size() 定位 shard, 只锁那一个
};
```

> **关于 `mutable`**：`get` 是 `const` 方法（逻辑上不改变缓存），但它要加锁（锁是 mutex，加锁改变 mutex 状态）。所以 Shard 的 mutex 是 `mutable`——在 const 方法里也能改。这是 const + 并发的标准写法，不是偷懒。

### 验证

> **别被测试骗了**：`test_milestone5` 测并发 put 不丢、get 正确、size 对。**但它不查你是不是真分片**——你用"全局一把锁"也能过所有测试（结果一样对）。区别只在吞吐：单锁在高并发下慢得多。**真正的验收标准：内部是多个 shard，每个 shard 独立 mutex，不同 key 的访问锁不同的 shard。** 你可以自己写个 micro-benchmark（单锁 vs 分片）对比吞吐，体会差异——这才是 MS5 的点。

## Milestone 6: C++20 同步原语实践

### 目标

用 `std::latch` / `std::barrier` / `std::counting_semaphore` 各实现一个经典并发模式（`fork_join_sum` / `two_phase_sum` / `measure_max_concurrency`）。

### 为什么

ch02-05 讲了这三个原语的概念，但"知道"和"会挑"差很远。这个 milestone 的核心不是写多少代码，而是**判断"这个场景该用哪个原语"**——三个函数各对应一种典型场景，做的时候想清楚为什么是它。

### 实现指引

**`fork_join_sum`（latch）**：派发 N 个任务到线程，主线程要等全部完成。`std::latch` 初始化为 N，每个任务完成时 `count_down()`，主线程 `wait()`。为什么是 latch 不是 barrier？因为这是一次性的"等 N 个全完成"（countdown 到 0），而 barrier 是可复用的阶段同步。

**`two_phase_sum`（barrier）**：多个 worker 各自做 phase 1（写自己的贡献），barrier 同步（全部完成 phase 1 才进 phase 2），再汇总。`std::barrier` 可以指定 completion 函数（最后一个到达的线程执行），适合"阶段间汇总"。为什么不是 latch？因为可能有多轮阶段（barrier 可复用），且要在阶段点做事。

**`measure_max_concurrency`（semaphore）**：N 个线程都想进临界区，但最多 `max_concurrent` 个能进。`std::counting_semaphore<max>` 初始化为 max，每个线程 `acquire()` 进、`release()` 出，用 atomic 记录在区内的峰值。为什么是 semaphore？因为这是"允许 N 个并发"的典型场景，latch/barrier 都不对。

> **踩坑预警**：`counting_semaphore` 的模板参数是最大值，构造参数是初始值。`std::counting_semaphore<4>` + 构造 `(4)` 表示初始 4 个许可、上限 4。`measure_max_concurrency` 里观测峰值要用 `atomic` 的 compare_exchange，别用普通 `int++`（多线程写同变量是 data race）。

### 验证

> **别被测试骗了**：`test_milestone6` 测三个函数的返回值（fork_join_sum 的和、two_phase_sum 的积、max_concurrency 的上限）。**但不查你是不是真的用了对应原语**——你完全可以用 mutex 手搓出同样结果（比如 fork_join 用 mutex + atomic counter 模拟 latch）。**真正的验收标准：三个函数分别真的用 `std::latch` / `std::barrier` / `std::counting_semaphore`**，不是手搓等价物。体会"标准库给了你趁手的工具，别再造轮子"。

## 自查清单

提交前逐项确认：

- [ ] MS1 测试通过——push/pop、FIFO、阻塞行为、多生产者不丢不重
- [ ] MS2 测试通过——close 后 push 抛、pop 取完返 nullopt、close 唤醒消费者
- [ ] MS3 测试通过——超时返回值和时间正确
- [ ] MS4 测试通过——MPMC 压力不丢不重、size 跟踪容量
- [ ] MS5 测试通过——并发 put 不丢、get 正确、size 对
- [ ] MS6 测试通过——三个同步原语函数结果正确
- [ ] **MS1 真验收**：`push`/`pop` 的等待都是谓词 wait（`cv.wait(lock, predicate)`），没有裸 `wait()`
- [ ] **MS2 真验收**：`close()` 里对两个 cv 都是 `notify_all()`（不是 `notify_one`）
- [ ] **MS4 真验收**：队列大小永远 ≤ capacity（背压生效），靠 close 让多消费者退出
- [ ] **MS5 真验收**：内部是多个 shard 各持独立 mutex（不是全局一把锁）
- [ ] **MS6 真验收**：三个函数分别真用了 `std::latch` / `std::barrier` / `std::counting_semaphore`
- [ ] **全部测试在 TSan 下无 data race 报告**（Debug 构建直接跑）
- [ ] 能解释 predicate wait 为什么能同时防虚假唤醒和丢失唤醒
- [ ] 能解释 close 后 pop "取完剩余再 nullopt" 的语义（而不是立刻 nullopt）
- [ ] 能解释分片锁相比单锁的吞吐优势，以及 shard_count 的取舍

## 扩展（bonus）

- 给 `BoundedBlockingQueue` 加 `try_push`/`try_pop`（非阻塞版，立刻返回成功/失败）
- 用 `std::shared_mutex` 给 `ConcurrentCache` 做读写锁版本（读多写少时比分片互斥锁更优），对比吞吐
- 实现 `measure_max_concurrency` 的"严格 == max"验证（需要足够多 caller + 同步启动）

## 参考资源

- [`std::condition_variable` — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [`std::condition_variable::wait` 的谓词重载 — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable/wait)
- [`std::latch` / `std::barrier` / `std::counting_semaphore — cppreference`](https://en.cppreference.com/w/cpp/thread)
- [ThreadSanitizer — Clang 文档](https://clang.llvm.org/docs/ThreadSanitizer.html)
