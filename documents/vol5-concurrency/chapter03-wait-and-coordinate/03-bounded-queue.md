---
title: "有容量的队列：先让它站得住"
description: "把上一篇的条件变量落到真实组件上：空与满两个方向各配一把条件变量，一把锁护住全部状态，多生产者多消费者下用白跑计数与全编制验收量它的正确性。"
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 有容量的队列：先让它站得住

生产者往里放，消费者往外取，容量有上限，两边快慢不一致时各自睡下、各自被叫醒。接下来三篇咱们把它搭出来，搭完您手里会有一个能关闭、能背压（满了就让生产者等着，或者立即退回）、在多生产者多消费者下站得住的队列。设计走到哪儿，笔者就把读数摆到哪儿。

## 教学版起步：两个方向的条件

队列里住着两个方向的等待。空了，消费者等的是“不空”。满了，生产者等的是“不满”。改“不空”的正好是生产者，改“不满”的正好是消费者，两边互不掺和，咱们就让它们各占一把条件变量：

<OnlineCompilerDemo
  title="教学版有界队列：空了 pop 等，满了 push 等"
  source-path="code/examples/vol5/chapter03/10_bounded_queue_teaching.cpp"
  description="正文的教学版队列类原样抽出，补了一个 main：容量 2 放 3 件，生产者等一回位置，消费者把三件按序取走。两把条件变量各管一个方向。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// bounded_queue_teaching.cpp —— 教学版有容量队列：空了消费者等，满了生产者等，各配一把条件变量
#include <condition_variable>
#include <mutex>
#include <queue>
#include <cstddef>
#include <utility>
#include <cstdio>
#include <thread>

template <typename T>
class BoundedQueue {
public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  void push(T value) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
    queue_.push(std::move(value));
    not_empty_.notify_one();
  }

  T pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty(); });
    T value = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return value;
  }

private:
  std::queue<T> queue_;
  std::size_t capacity_;
  std::mutex mutex_;
  std::condition_variable not_full_;   // 等有位的，是生产者
  std::condition_variable not_empty_;  // 等有货的，是消费者
};

int main() {
  BoundedQueue<int> q(2);   // 容量 2，放 3 件，生产者会等一回位置

  std::thread consumer([&q] {
    for (int i = 0; i < 3; ++i) {
      std::printf("pop -> %d\n", q.pop());
    }
  });
  for (int i = 0; i < 3; ++i) {
    q.push(i);
  }
  consumer.join();
  return 0;
}
```

</OnlineCompilerDemo>

成员五个，您一个一个认。`std::queue` 存货，默认底层容器是 `deque`（[queue.defn]/1），容量记一个数，互斥量一把，条件变量两把。名字起得直白：睡在 `not_full_` 上的全是生产者，睡在 `not_empty_` 上的全是消费者。两处等待都带谓词，3.1 讲过的虚假唤醒与丢失唤醒两道防线就在这里落地，不再重复。

通知的方向也定死了。`push` 改的是“有货”，叫的只有 `not_empty_`。`pop` 改的是“有位”，叫的只有 `not_full_`。您放一件货进队列，等位置的同行不欠一声通知，睡着的消费者才欠。方向要是混了会怎样，往下到多生产者多消费者的正确性，咱们拿读数给您看。

## 队列空了、队列满了

两个方向的行为就是两句大白话：队列空着，`pop` 睡。队列满了，`push` 睡。话好说，睡多久归现场。笔者写了个小实验：容量 2，消费者每件歇 100 毫秒，生产者连放 4 件，每一回 push 计一次时。然后倒过来，队列空着，消费者进 pop，生产者 80 毫秒后才放一件，pop 也计一次时。本篇多数实验跑在头文件 `queue.hpp` 上，它是这三篇搭完的最终形状，接口在后面几篇才逐一登场，个别实验用的变体在正文交代，您在这里只当 `push`、`pop` 已经能用就行：

<OnlineCompilerDemo
  title="队列空了、满了：两个方向的阻塞各量一次"
  source-path="code/examples/vol5/chapter03/11_queue_full_empty.cpp"
  description="容量 2、消费者每件歇 100 毫秒，生产者连放 4 件，每回 push 计时；再倒过来量 pop 等货的时长。原实验基于头文件 queue.hpp，这里把最终形状的队列内联进同一个文件。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// queue_full_empty.cpp —— 队列空了 pop 睡、队列满了 push 睡：容量 2，消费每件歇 100 ms
// 队列类是 3.3 起三篇搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <chrono>
#include <cstdio>
#include <thread>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

enum class QueueStatus {
  kOk,
  kClosed,
};

template <typename T>
class BoundedQueue {
public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  QueueStatus push(T value) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
    if (closed_) return QueueStatus::kClosed;   // 关了，货不收
    queue_.push(std::move(value));
    not_empty_.notify_one();
    return QueueStatus::kOk;
  }

  QueueStatus pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return QueueStatus::kClosed;  // 关了且取空了
    out = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return QueueStatus::kOk;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    not_full_.notify_all();
    not_empty_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

private:
  std::queue<T> queue_;
  std::size_t capacity_;
  bool closed_ = false;
  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
};

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  BoundedQueue<int> q(2);

  std::printf("--- 满的方向：容量 2，消费每件歇 100 ms ---\n");
  std::thread consumer([&q] {
    for (int i = 0; i < 4; ++i) {
      int v = 0;
      q.pop(v);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));  // 让消费者进 pop 等着
  for (int i = 0; i < 4; ++i) {
    auto t0 = Clock::now();
    q.push(i);
    std::printf("push #%d 用时 %6.1f ms\n", i, ms_since(t0));
  }
  consumer.join();

  std::printf("--- 空的方向：队列空着，生产者 80 ms 后才来一件 ---\n");
  std::thread producer([&q] {
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    q.push(99);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto t0 = Clock::now();
  int v = 0;
  q.pop(v);
  std::printf("pop 用时 %6.1f ms，拿到 %d\n", ms_since(t0), v);
  producer.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
--- 满的方向：容量 2，消费每件歇 100 ms ---
push #0 用时    0.0 ms
push #1 用时    0.0 ms
push #2 用时    0.0 ms
push #3 用时  100.4 ms
--- 空的方向：队列空着，生产者 80 ms 后才来一件 ---
pop 用时   59.7 ms，拿到 99
```

满的方向，4 次 push 里 3 次几乎不花时间，1 次睡了 100.4 毫秒。睡下去的原因直截了当：位置没了，谓词不成立，它在 `not_full_` 上排队，直到消费者取出一件才醒。为什么落在第 4 次而不是第 3 次？两条线同时在跑，第 1 件落进队列的一瞬间就被消费者取走了，等生产者放到末尾，队列里实际压着 2 件。读数来自一次真实运行，阻塞落在哪一回由调度定，您复跑可能落在第 3 次，也可能两回都睡，但“至少有一回 push 等满一件消费的耗时”每次都在。

空的方向，pop 用时 59.7 毫秒，拿到的正是生产者放的 99。数很好算，咱们一起过：生产者被安排在 80 毫秒后才放货，消费者在 20 毫秒时进 pop，80 减 20，您等到 60 毫秒上下，差出来的浮动是唤醒加调度，跟 3.1 量过的延迟一个量级。

容量还藏着一句要说的。`capacity_` 填 0 合法，可 `size() < 0` 永远不成立，教学版里 push 会一直睡下去，正式版里要睡到 close 才有人叫。填 0 等于宣布生产者永远进不来，编译器不拦，参数对不对得上得靠您自己过一眼。

## 多生产者多消费者的正确性

### 锁为什么只有一把

队列的不变量是整体的，咱们一处一处过。改 `size` 的同时，队头不能被人取走。查空的同时，不能有人插队放货。后面还要加的关闭标志，也得跟件数咬合。要是给生产者与消费者各配一把锁，查与改之间就漏缝，两把锁的获取顺序还给死锁留了门。所以全部共享状态共用一把 `mutex_`，谁进谁出都排队。锁负责“别打架”。

### 条件变量为什么要有两把

锁管秩序，条件变量管叫人，叫错人的代价不一样。3.2 验过 `notify_one` 挑人不看条件，叫醒哪一位归实现定。一把 cv 上睡两拨人，生产者改完条件叫一声，被叫醒的可能是另一个生产者。后果有两层，咱们一层层看。

轻的一层是白跑，您把代价记在唤醒上就好。醒来的生产者一看位置没了（刚被同行塞满），回去接着睡。正确性有谓词兜着，浪费的是一次唤醒。

白跑能数出来，咱们把它写进等待循环。负载是这样的：容量 4，2 个生产者各放 4000 件，2 个消费者各取 4000 件，取到就算完，不加工。数法就是 `wait` 返回之后再看一遍条件，还不成立就计一次：

<OnlineCompilerDemo
  title="一把 cv 的白跑账：醒来条件还不成立就计一次"
  source-path="code/examples/vol5/chapter03/12_one_cv_wasted.cpp"
  description="正文片段补成完整队列：wait 返回再看一遍条件，还不成立就计一次。片段里生产消费共用一个 wasted 计数，这里分成两侧各数各的，负载照旧：容量 4、2×4000 件。白跑次数每次都不同。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// one_cv_wasted.cpp —— 一把条件变量的队列：醒了条件还不成立，白跑一趟能数出来
// 负载与 3.2 量白跑的实验同款：容量 4，2 个生产者各放 4000 件，2 个消费者各取 4000 件
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// 一把 cv 的队列：生产者与消费者睡在同一把 cv 上，notify_one 不看条件挑人，
// 醒来重看条件还不成立就白跑一趟，两侧各自计数
class OneCvQueue {
public:
  explicit OneCvQueue(std::size_t capacity) : capacity_(capacity) {}

  void push(int v) {
    std::unique_lock<std::mutex> lk(m);
    while (q.size() >= capacity_) {
      cv.wait(lk);
      if (q.size() >= capacity_) ++wasted_push;   // 醒了，位置还没空出来，白跑一趟
    }
    q.push(v);
    cv.notify_one();
  }

  int pop() {
    std::unique_lock<std::mutex> lk(m);
    while (q.empty()) {
      cv.wait(lk);
      if (q.empty()) ++wasted_pop;   // 醒了，条件还不成立，白跑一趟
    }
    int v = q.front();
    q.pop();
    cv.notify_one();
    return v;
  }

  // 次数都在锁内累加，join 完所有线程再读
  long long wasted_pushes() const { return wasted_push; }
  long long wasted_pops() const { return wasted_pop; }

private:
  std::queue<int> q;
  std::size_t capacity_;
  std::mutex m;
  std::condition_variable cv;
  long long wasted_push = 0;   // 生产侧白跑
  long long wasted_pop = 0;    // 消费侧白跑
};

int main() {
  OneCvQueue q(4);
  constexpr int kProducers = 2;
  constexpr int kConsumers = 2;
  constexpr int kPerSide = 4000;

  std::vector<std::thread> threads;
  for (int i = 0; i < kConsumers; ++i) {
    threads.emplace_back([&q] {
      for (int k = 0; k < kPerSide; ++k) q.pop();
    });
  }
  for (int i = 0; i < kProducers; ++i) {
    threads.emplace_back([&q, i] {
      for (int k = 0; k < kPerSide; ++k) q.push(i * kPerSide + k);
    });
  }
  for (auto& t : threads) t.join();

  std::printf("一把 cv : 生产侧白跑 %6lld 次  消费侧白跑 %6lld 次\n",
              q.wasted_pushes(), q.wasted_pops());
  return 0;
}
```

</OnlineCompilerDemo>

一把 cv 的队列与两把 cv 的队列，除了通知对象与等待对象不同，形状完全一样。咱们让它们各跑三遍：

```text
一把 cv : 生产侧白跑    276 次  消费侧白跑    233 次
一把 cv : 生产侧白跑    232 次  消费侧白跑    233 次
一把 cv : 生产侧白跑    224 次  消费侧白跑    240 次
两把 cv : 生产侧白跑    176 次  消费侧白跑    148 次
两把 cv : 生产侧白跑    148 次  消费侧白跑    113 次
两把 cv : 生产侧白跑    128 次  消费侧白跑    120 次
```

两把 cv 把白跑砍掉接近一半，合计每遍从 464 到 509 次落到 248 到 324 次。砍不到零，因为同向也会抢：一件货只够一个消费者拿，被叫醒的消费者来晚了，货已被同行取走，只好接着睡。空位同理，两个生产者抢一个格子也会空手而归。剩下的差额，就是一把 cv 额外交的学费，您接着往下看，重的一层在后头。

### 通知被对面收走的时候

把一把 cv 的队列放进多生产者多消费者，能凑出比白跑更疼的场面。笔者按次序推给您看，容量记 4。队列满着，P1、P2 两个生产者都睡在 cv 上，消费者 C 手里拿着一件在加工。C 加工完，取出一件，`notify_one`，叫醒了 P1。P1 放进一件新的，又 `notify_one`。此刻睡着的只剩 P2，通知就落在 P2 头上。P2 醒来一看，位置又没了，P1 刚把队列塞满，它回去接着睡。到这里，队列里躺着 4 件货，睡着的全是生产者，C 是唯一可能来取的人。要是 C 处理完手里的事就收工，再没有谁会放货或取货，货与 P2 一起停在夜里。

真实负载里，死局要凑齐好几件事：通知恰好落在对面的等待者身上，之后再没有 pop 来补一声。所以它平时不这么吓人，更多表现成一件货多睡了一整轮加工的时长，真实项目的延迟曲线末端就会冒出这样的长尾。真要停摆，得把刚才的凑巧一样样凑齐，您日常多半碰不上，碰上了也不好查。

修法在方向上，咱们把通知的去向收拢。`push` 只叫 `not_empty_`，生产者想收走通知都没有资格，它根本不在 `not_empty_` 上睡。两把 cv 多写几行，换回来的是时序说得清。这也是 3.2 的判断法在队列上的落法：条件分开，就一个条件配一把条件变量，各叫各的。

### 丢失唤醒的防线

防线的本体还是 3.1 的三条：谓词、同一把锁、改完条件再通知，咱们把它们落到队列的三处位置上。`push` 与 `pop` 的等待都带谓词，醒来必验。共享状态的读写全在一把 `mutex_` 里，改条件与查条件的人拿的是同一把锁。通知排在条件改完之后，`push` 里排在 `queue_.push` 后面，`pop` 里排在 `queue_.pop()` 后面。关闭还要添第四道，`notify_all`，因为关闭改的条件够所有人检查，下一篇《队列关不上会怎样：关闭与背压》专门讲。

### 数出来的验收

正确性不能靠感觉。笔者搭了一场全编制的验收：4 个生产者各放 2500 件，编号按“生产者序号乘 2500 加件号”排，全程唯一。4 个消费者各取 2500 件，容量压到 8，逼两边频繁地睡与醒。验收查三样：总入队与总出队对得上，没有谁被取到两次，全部件号加起来的校验和等于 0 加到 9999 的总和 49995000：

<OnlineCompilerDemo
  title="多生产者多消费者验收：件数、重复、校验和"
  source-path="code/examples/vol5/chapter03/13_queue_acceptance.cpp"
  description="4 个生产者各放 2500 件、4 个消费者各取 2500 件、容量压到 8：总入队出队对得上、没有谁被取到两次、校验和等于 0 加到 9999。queue.hpp 已内联。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// queue_acceptance.cpp —— 多生产者多消费者的验收：件数对得上、没有重复、校验和一致
// 队列类是 3.3 起三篇搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

enum class QueueStatus {
  kOk,
  kClosed,
};

template <typename T>
class BoundedQueue {
public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  QueueStatus push(T value) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
    if (closed_) return QueueStatus::kClosed;   // 关了，货不收
    queue_.push(std::move(value));
    not_empty_.notify_one();
    return QueueStatus::kOk;
  }

  QueueStatus pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return QueueStatus::kClosed;  // 关了且取空了
    out = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return QueueStatus::kOk;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    not_full_.notify_all();
    not_empty_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

private:
  std::queue<T> queue_;
  std::size_t capacity_;
  bool closed_ = false;
  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
};

constexpr int kProducers = 4;
constexpr int kConsumers = 4;
constexpr int kPerProducer = 2500;
constexpr int kTotal = kProducers * kPerProducer;

int main() {
  BoundedQueue<int> q(8);
  std::atomic<long long> pushed{0};
  std::atomic<long long> popped{0};
  std::atomic<long long> sum{0};
  std::atomic<long long> duplicates{0};
  std::unique_ptr<std::atomic<uint8_t>[]> seen(new std::atomic<uint8_t>[kTotal]);
  for (int i = 0; i < kTotal; ++i) seen[i].store(0, std::memory_order_relaxed);

  std::vector<std::thread> threads;
  for (int i = 0; i < kConsumers; ++i) {
    threads.emplace_back([&, i] {
      for (int k = 0; k < kTotal / kConsumers; ++k) {
        int v = -1;
        if (q.pop(v) != QueueStatus::kOk) {
          std::printf("consumer %d: 提前收到 kClosed!\n", i);
          return;
        }
        if (seen[v].exchange(1, std::memory_order_relaxed)) {
          duplicates.fetch_add(1, std::memory_order_relaxed);
        }
        sum.fetch_add(v, std::memory_order_relaxed);
        popped.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (int i = 0; i < kProducers; ++i) {
    threads.emplace_back([&, i] {
      for (int k = 0; k < kPerProducer; ++k) {
        int v = i * kPerProducer + k;  // 0..9999，每件恰好出现一次
        if (q.push(v) == QueueStatus::kOk) {
          pushed.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& t : threads) t.join();

  long long expect_sum = static_cast<long long>(kTotal - 1) * kTotal / 2;
  std::printf("总入队 %lld  总出队 %lld\n", pushed.load(), popped.load());
  std::printf("重复取到 %lld 件  校验和 %lld（应为 %lld，%s）\n",
              duplicates.load(), sum.load(), expect_sum,
              sum.load() == expect_sum ? "一致" : "不一致");
  std::printf("收尾时 size = %zu\n", q.size());
  return 0;
}
```

</OnlineCompilerDemo>

```text
总入队 10000  总出队 10000
重复取到 0 件  校验和 49995000（应为 49995000，一致）
收尾时 size = 0
```

读数干净，凭据在结构里：所有共享状态的读写都在同一把锁内完成，谁也绕不过去，每一次醒来都有谓词验着。调度怎么搅，件数不会多、不会少、也不会重，您把同款结构搬到自己的程序里，凭据跟着走。代码里用到的 `QueueStatus` 与 `pop(T&)` 是下一篇《队列关不上会怎样：关闭与背压》才正式登场的接口，这里提前借来，理由顺嘴交代：咱们要验的就是最终形状的队列，而且出队件数固定，close 还不需要出场。

## 参考资源

各实验原本跑在头文件 `queue.hpp` 上，它的最终形状已内联进示例文件。cppreference 本回抓到了 `std::queue` 与 `std::condition_variable` 两页全文，与草案核对无出入。有界队列的推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是笔者自己写的。

- [std::condition_variable（cppreference）][cppref-cv]，C++11 起。
- [std::queue（cppreference）][cppref-queue]，容器适配器，默认底层容器是 `deque`。
- [Class condition_variable（现行草案 thread.condition.condvar）][draft-condvar]，两把条件变量各管一个方向所依赖的通知效果。
- [Class template queue（现行草案 queue.defn）][draft-queue]，`push` 委托给底层容器的 `push_back`。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[cppref-queue]: https://en.cppreference.com/w/cpp/container/queue
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-queue]: https://eel.is/c++draft/queue.defn
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
