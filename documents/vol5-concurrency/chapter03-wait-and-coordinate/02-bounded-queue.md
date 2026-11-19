---
title: "写一个有容量限制的任务队列"
description: "把 3.1《等待一个条件成立》的条件变量落到一个真实组件上：两个方向的条件怎样住进同一个对象，多生产者多消费者凭什么站得住，队列怎么关、满了怎么办、异常从哪里漏。"
chapter: 5
order: 2
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

# 写一个有容量限制的任务队列

3.1《等待一个条件成立》的结尾咱们留过话：条件变量学会了，得落到一个真的组件上才算数，落点选了队列。生产者往里放，消费者往外取，容量有上限，两边快慢不一致时各自睡下、各自被叫醒。这一篇咱们把它搭出来，搭完您手里会有一个能关闭、能背压（满了就让生产者等着，或者立即退回）、在多生产者多消费者下站得住的队列。设计走到哪儿，我就把读数摆到哪儿，六组实验全部在本机编译实测过，怎么复现写在参考资源里。

## 教学版起步：两个方向的条件

队列里住着两个方向的等待。空了，消费者等的是“不空”。满了，生产者等的是“不满”。改“不空”的正好是生产者，改“不满”的正好是消费者，两边互不掺和，咱们就让它们各占一把条件变量：

```cpp
#include <condition_variable>
#include <mutex>
#include <queue>
#include <cstddef>
#include <utility>

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
```

成员五个，您一个一个认。`std::queue` 存货，默认底层容器是 `deque`（[queue.defn]/1），容量记一个数，互斥量一把，条件变量两把。名字起得直白：睡在 `not_full_` 上的全是生产者，睡在 `not_empty_` 上的全是消费者。两处等待都带谓词，3.1 讲过的虚假唤醒与丢失唤醒两道防线就在这里落地，不再重复。

通知的方向也定死了。`push` 改的是“有货”，叫的只有 `not_empty_`。`pop` 改的是“有位”，叫的只有 `not_full_`。您放一件货进队列，等位置的同行不欠一声通知，睡着的消费者才欠。方向要是混了会怎样，往下到多生产者多消费者的正确性，咱们拿读数给您看。

## 队列空了、队列满了

两个方向的行为就是两句大白话：队列空着，`pop` 睡。队列满了，`push` 睡。话好说，睡多久归现场。我写了个小实验：容量 2，消费者每件歇 100 毫秒，生产者连放 4 件，每一回 push 计一次时。然后倒过来，队列空着，消费者进 pop，生产者 80 毫秒后才放一件，pop 也计一次时。本篇多数实验跑在头文件 `queue.hpp` 上，它是全文搭完的最终形状，接口在后面几节才逐一登场，个别实验用的变体在正文交代，您在这里只当 `push`、`pop` 已经能用就行：

```cpp
#include <chrono>
#include <cstdio>
#include <thread>
#include "queue.hpp"

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

```text
--- 满的方向：容量 2，消费每件歇 100 ms ---
push #0 用时    0.0 ms
push #1 用时    0.0 ms
push #2 用时    0.0 ms
push #3 用时  100.4 ms
--- 空的方向：队列空着，生产者 80 ms 后才来一件 ---
pop 用时   59.7 ms，拿到 99
```

满的方向，4 次 push 里 3 次几乎不花时间，1 次睡了 100.4 毫秒。睡下去的原因直截了当：位置没了，谓词不成立，它在 `not_full_` 上排队，直到消费者取出一件才醒。为什么落在第 4 次而不是第 3 次？两条线同时在跑，第 1 件落进队列的那一瞬间就被消费者取走了，等生产者放到末尾，队列里实际压着 2 件。读数来自一次真实运行，阻塞落在哪一回由调度定，您复跑可能落在第 3 次，也可能两回都睡，但“至少有一回 push 等满一件消费的耗时”这件事每次都在。

空的方向，pop 用时 59.7 毫秒，拿到的正是生产者放的 99。数很好算，咱们一起过：生产者被安排在 80 毫秒后才放货，消费者在 20 毫秒时进 pop，80 减 20，您等到 60 毫秒上下，差出来的那点浮动是唤醒加调度，跟 3.1 量过的那批延迟一个量级。

容量还藏着一句要说的。`capacity_` 填 0 合法，可 `size() < 0` 永远不成立，教学版里 push 会一直睡下去，正式版里要睡到 close 才有人叫。填 0 等于宣布生产者永远进不来，编译器不拦，参数对不对得上得靠您自己过一眼。

## 多生产者多消费者的正确性

### 锁为什么只有一把

队列的不变量是整体的，咱们一处一处过。改 `size` 的同时，队头不能被人取走。查空的同时，不能有人插队放货。后面还要加的关闭标志，也得跟件数咬合。要是给生产者与消费者各配一把锁，查与改之间就漏缝，两把锁的获取顺序还给死锁留了门。所以全部共享状态共用一把 `mutex_`，谁进谁出都排队。锁负责“别打架”。

### 条件变量为什么要有两把

锁管秩序，条件变量管叫人，叫错人的代价不一样。3.1 验过 `notify_one` 挑人不看条件，叫醒哪一位归实现定。一把 cv 上睡两拨人，生产者改完条件叫一声，被叫醒的可能是另一个生产者。后果有两层，咱们一层层看。

轻的一层是白跑，您把代价记在唤醒上就好。醒来的生产者一看位置没了（刚被同行塞满），回去接着睡。正确性有谓词兜着，浪费的是一次唤醒。

白跑能数出来，咱们把它写进等待循环。负载是这样的：容量 4，2 个生产者各放 4000 件，2 个消费者各取 4000 件，取到就算完，不加工。数法就是 `wait` 返回之后再看一遍条件，还不成立就计一次：

```cpp
int pop() {
  std::unique_lock<std::mutex> lk(m);
  while (q.empty()) {
    cv.wait(lk);
    if (q.empty()) ++wasted;   // 醒了，条件还不成立，白跑一趟
  }
  int v = q.front();
  q.pop();
  cv.notify_one();
  return v;
}
```

一把 cv 的队列与两把 cv 的队列，除了通知对象与等待对象不同，形状完全一样。咱们让它们各跑三遍：

```text
一把 cv : 生产侧白跑    276 次  消费侧白跑    233 次
一把 cv : 生产侧白跑    232 次  消费侧白跑    233 次
一把 cv : 生产侧白跑    224 次  消费侧白跑    240 次
两把 cv : 生产侧白跑    176 次  消费侧白跑    148 次
两把 cv : 生产侧白跑    148 次  消费侧白跑    113 次
两把 cv : 生产侧白跑    128 次  消费侧白跑    120 次
```

两把 cv 把白跑砍掉接近一半，合计每遍从 464 到 509 次落到 248 到 324 次。砍不到零，因为同向也会抢：一件货只够一个消费者拿，被叫醒的消费者来晚了，货已被同行取走，只好接着睡。空位同理，两个生产者抢一个格子也会空手而归。剩下的差额，就是一把 cv 额外交的学费，您接着往下看，重的那层在后头。

### 通知被对面收走的时候

把一把 cv 的队列放进多生产者多消费者，能凑出比白跑更疼的场面。我按次序推给您看，容量记 4。队列满着，P1、P2 两个生产者都睡在 cv 上，消费者 C 手里拿着一件在加工。C 加工完，取出一件，`notify_one`，叫醒了 P1。P1 放进一件新的，又 `notify_one`。此刻睡着的只剩 P2，这声通知就落在 P2 头上。P2 醒来一看，位置又没了，P1 刚把队列塞满，它回去接着睡。到这一步，队列里躺着 4 件货，睡着的全是生产者，C 是唯一可能来取的人。要是 C 处理完手里的事就收工，再没有谁会放货或取货，货与 P2 一起停在夜里。

真实负载里，死局要凑齐好几件事：通知恰好落在对面的等待者身上，之后再没有 pop 来补一声。所以它平时不这么吓人，更多表现成一件货多睡了一整轮加工的时长，真实项目的延迟曲线末端就会冒出这样的长尾。真要停摆，得把刚才的凑巧一样样凑齐，您日常多半碰不上，碰上了也不好查。

修法在方向上，咱们把通知的去向收拢。`push` 只叫 `not_empty_`，生产者想收走这声通知都没有资格，它根本不在 `not_empty_` 上睡。两把 cv 多写几行，换回来的是时序说得清。这也是 3.1 那句判断法在队列上的落法：条件分开，就一个条件配一把条件变量，各叫各的。

### 丢失唤醒的防线

防线的本体还是 3.1 的三条：谓词、同一把锁、改完条件再通知，咱们把它们落到队列的三处位置上。`push` 与 `pop` 的等待都带谓词，醒来必验。共享状态的读写全在一把 `mutex_` 里，改条件与查条件的人拿的是同一把锁。通知排在条件改完之后，`push` 里排在 `queue_.push` 后面，`pop` 里排在 `queue_.pop()` 后面。关闭还要添第四道，`notify_all`，因为关闭改的条件够所有人检查，下一节专门讲。

### 数出来的验收

正确性不能靠感觉。我搭了一场全编制的验收：4 个生产者各放 2500 件，编号按“生产者序号乘 2500 加件号”排，全程唯一。4 个消费者各取 2500 件，容量压到 8，逼两边频繁地睡与醒。验收查三样：总入队与总出队对得上，没有谁被取到两次，全部件号加起来的校验和等于 0 加到 9999 的总和 49995000：

```cpp
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>
#include "queue.hpp"

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

```text
总入队 10000  总出队 10000
重复取到 0 件  校验和 49995000（应为 49995000，一致）
收尾时 size = 0
```

读数干净，凭据在结构里：所有共享状态的读写都在同一把锁内完成，谁也绕不过去，每一次醒来都有谓词验着。调度怎么搅，件数不会多、不会少、也不会重，您把这套结构搬到自己的程序里，凭据跟着走。这段代码里用到的 `QueueStatus` 与 `pop(T&)` 是下一节才正式登场的接口，这里提前借来，理由顺嘴交代：咱们要验的就是最终形状的队列，而且出队件数固定，close 还不需要出场。

## 关闭一个队列

生产者放完收工，消费者手里没活，睡在 `pop` 里等下一件，您能想到它等到的只有永远。3.1 讲过，通知发出去没人等就蒸发，反过来也成立：睡着的人要醒来，得有人去叫。队列要能收场，咱们就欠每个等待者一个尽头。

设计是一个 `closed_` 标志，咱们让它进两个谓词，顺带改两个方法的签名：

```cpp
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
```

签名为什么变，值得停一下，您想想“空且已关”的时刻。教学版 `T pop()` 拿不出值来返回，返回值通道堵死。改成 `QueueStatus pop(T& out)`，值走引用出去，状态走返回值回来。`push` 同样改回 `QueueStatus`，生产者放货被拒时能收到 `kClosed`，不至于蒙在鼓里。

两个谓词的写法各有讲究，您分开看。`push` 醒来之后查 `closed_`，关了就退货，队列都关了，收下的货没人会来取。`pop` 醒来之后查的却是 `queue_.empty()`，不是 `closed_`。关闭不等于清空：生产者收工时压在队列里的活，消费者该取干净、干完，咱们叫它 drain。把“还有没有货”交给 `empty()`，关不关就不掺和取货的判断。

`close()` 的两步咱们分开看。锁内把标志立起来，锁外对两把条件变量各 `notify_all` 一次。为什么是 `notify_all`：关闭对每个等待者都是真的条件变化，睡着的消费者全该醒来看一眼，睡着的生产者全该醒去收 `kClosed`。按 3.1 的判断法，一次变化够几条线程往下走就叫几条，这里够所有人，那就叫所有人。为什么通知放锁外：3.1 说过通知不挑锁里锁外，放锁外让醒来的人少排一趟队。

还有一条跟析构有关的红线，得请您留神。标准给 `condition_variable` 的析构写了前置条件：不许还有线程堵在上面（[thread.condition.condvar]/4，附注说只要通知发生在析构之前就成）。落到队列上，次序是死的：`close()`，join 掉所有生产者与消费者，然后才轮到队列对象出作用域。等待者还睡着就把队列析构，是未定义行为。

实验把这些行为一次验掉，咱们安排的次序是这样。消费者比生产者早起步，有人睡在空队列上。生产者放完 200 件退出，主线程 close，消费者一直取到收到 `kClosed` 为止：

```cpp
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
#include "queue.hpp"

constexpr int kPerProducer = 100;
constexpr int kProducers = 2;

int main() {
  BoundedQueue<int> q(4);
  std::atomic<int> total{0};

  std::vector<std::thread> consumers;
  std::vector<int> consumed(3, 0);
  std::vector<int> exit_way(3, -1);
  for (int i = 0; i < 3; ++i) {
    consumers.emplace_back([&, i] {
      while (true) {
        int v = 0;
        QueueStatus st = q.pop(v);
        if (st == QueueStatus::kClosed) {
          exit_way[i] = 0;
          break;
        }
        ++consumed[i];
        total.fetch_add(1);
      }
    });
  }

  std::vector<std::thread> producers;
  for (int i = 0; i < kProducers; ++i) {
    producers.emplace_back([&] {
      for (int k = 0; k < kPerProducer; ++k) q.push(k);
    });
  }
  for (auto& t : producers) t.join();
  q.close();
  for (auto& t : consumers) t.join();

  std::printf("总入队 %d  总出队 %d（应为 %d）\n", kPerProducer * kProducers,
              total.load(), kPerProducer * kProducers);
  for (int i = 0; i < 3; ++i) {
    std::printf("consumer %d: 消费 %3d 件，以 %s 退出\n", i, consumed[i],
                exit_way[i] == 0 ? "kClosed" : "???");
  }
  std::printf("关停后 size = %zu\n", q.size());

  QueueStatus st = q.push(1);
  std::printf("关闭后再 push -> %s\n",
              st == QueueStatus::kClosed ? "kClosed" : "kOk");
  int v = 0;
  st = q.pop(v);
  std::printf("关闭且空了再 pop -> %s\n",
              st == QueueStatus::kClosed ? "kClosed" : "kOk");
  return 0;
}
```

```text
总入队 200  总出队 200（应为 200）
consumer 0: 消费  66 件，以 kClosed 退出
consumer 1: 消费  89 件，以 kClosed 退出
consumer 2: 消费  45 件，以 kClosed 退出
关停后 size = 0
关闭后再 push -> kClosed
关闭且空了再 pop -> kClosed
```

200 件一件没丢，咱们逐行核对。三个消费者的分配是 66、89、45，多生产者多消费者的常态就是抢到多少算多少，不均匀才是正常。三位都以 `kClosed` 退出，说明 drain 干净了，也说明睡在空队列上的消费者被 close 的 `notify_all` 叫醒了。关闭后再 push 收到 `kClosed`，空且关之后再 pop 收到 `kClosed`，关闭路径上的行为都对上了。

## 队列满了怎么办：背压的取舍

阻塞把压力顶回上游，这是默认答案，不是唯一答案。下游消费太慢，上游整批卡住，有的场景宁可丢。咱们把选项摆开。

```cpp
bool try_push(T value) {   // 拒绝：满了立即返回，决定权交给调用者
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || queue_.size() >= capacity_) return false;
    queue_.push(std::move(value));
  }
  not_empty_.notify_one();
  return true;
}

bool push_replace_oldest(T value, bool& evicted) {  // 覆盖：挤掉最老的，新来的必进
  evicted = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return false;
    if (queue_.size() >= capacity_) {
      queue_.pop();          // 挤掉队头最老的一件
      evicted = true;
    }
    queue_.push(std::move(value));
  }
  not_empty_.notify_one();
  return true;
}
```

拒绝版 `try_push` 锁内查满，满了立即返回 `false`，接下去怎么走由您定：重试、降级、记一笔日志，都是您的自由。丢弃新来的版本不另写代码，把 `try_push` 的失败瞒住不报，就是丢弃，队列边界上两个策略是同一个动作，差别只在知情权。覆盖版 `push_replace_oldest` 反着来，满的时候挤掉队头最老的一件，新来的永远进得去。

咱们把四种策略放到同一份负载下：容量 64，单生产者连发 10000 件，单消费者每件歇 50 微秒，各跑一遍：

| 策略 | 生产端用时 | 全程 | 受理入队 | 丢失 | 实际消费 |
|------|-----------|------|---------|------|---------|
| 阻塞 | 1477.8 ms | 1486.4 ms | 10000 | 0 | 10000 |
| 拒绝 | 0.2 ms | 8.5 ms | 64 | 9936 | 64 |
| 丢弃新来的 | 0.2 ms | 8.9 ms | 64 | 9936 | 64 |
| 覆盖最老的 | 0.2 ms | 8.7 ms | 10000 | 9936 | 64 |

阻塞行是背压本来的样子，您看两个时间：生产端用时与全程几乎重合，生产者的速度被消费者压平，上游被迫按下游的节奏走。10000 件一件没丢，代价是 1.5 秒。消费每件歇 50 微秒的设定，在 WSL2 的计时精度下实际接近 150 微秒一件，10000 件正好压出这样的总长。

拒绝与丢弃两行，受理、丢失、消费三列一模一样，咱们不意外，前文说过它们在边界上本来就是同一个动作。生产端 0.2 毫秒冲完，队列只来得及收下 64 件，正好一格容量的量，其余 9936 件被挡在门外。全程 8 到 9 毫秒，大半还是消费者在消化那 64 件。快是真快，丢也是真丢。

覆盖行 10000 件全部受理，可其中 9936 件在队列里被后来的挤掉，活着到消费者手里的只有最新的 64 件。它保住的是新，扔掉的是旧，取舍与阻塞正好相反，您选它图的就是一个新字。

挑法随业务走，我给您几条对得上场景的。下游丢不起、上游减速可接受，用阻塞。丢得起、上游卡不起，用拒绝。只在乎最近的读数，用覆盖。要替调用者隐瞒，用丢弃。还有一条反向的路：背压也可以不放队列里，队列只提供阻塞版，调用者拿 `try_push` 自己拼。咱们把四种都摆出来，是让您手上有一套对照，真项目里按需取用。

## 异常安全

异常安全咱们查三处：构造，搬进搬出的每一步，析构。锁的释放是全场兜底，也一并说。

构造。咱们顺着声明顺序过成员。`std::queue` 首次分配可能抛 `bad_alloc`。`condition_variable` 的构造函数按标准可以抛 `system_error`（[thread.condition.condvar]/2，比如资源不够）。语言的规则在这里兜底：初始化列表里哪个成员没构造完，已经构造好的成员按相反的顺序析构。半成品漏不出去，已构造的部分也不会泄漏，这层您不用操心。

`push` 的路。按值收参，调用者传左值时，拷贝发生在进函数体之前。我造了一个拷贝构造从第 3 次起必抛的类型，看看队列被波及了没有：

```cpp
// 拷贝构造从第 3 次起必抛，移动操作不抛
struct FlakyCopy {
  int v;
  inline static int copies = 0;
  explicit FlakyCopy(int x) : v(x) {}
  FlakyCopy(FlakyCopy&&) noexcept = default;
  FlakyCopy& operator=(FlakyCopy&&) noexcept = default;
  FlakyCopy(const FlakyCopy& o) : v(o.v) {
    if (++copies >= 3) throw std::runtime_error("copy #3 爆了");
  }
};
```

驱动它的调用序列不长，您照着摆一遍就能对上输出：

```cpp
BoundedQueue<FlakyCopy> q(8);
FlakyCopy a{1}, b{2}, c{3};
q.push(a);            // 第 1 次拷贝，花在进参数上
q.push(b);            // 第 2 次
try {
  q.push(c);          // 第 3 次，抛在进函数体之前
} catch (const std::runtime_error& e) {
  std::printf("push(c) 抛出：%s\n", e.what());
}
q.push(FlakyCopy{4}); // 右值：一路移动，不碰拷贝构造
```

前两件以左值 push，各花掉一次拷贝，花在进参数的动作上。第 3 件的拷贝在进函数体之前抛出，咱们看输出：

```text
push(c) 抛出：copy #3 爆了
队列里 2 件（应为 2）
再 push 一件右值，队列里 3 件
pop -> 1
pop -> 2
pop -> 4
收尾 size = 0
```

队列原地不动，尺寸还是 2，之后右值 push（一路移动，不碰拷贝构造）照常进队，三件货按次序取干净。异常从哪里进来，队列就停在哪儿，随后照常干活，这就是咱们要的效果。

进了函数体之后，承诺来自标准，咱们逐句念。`queue::push` 委托给底层容器的 `push_back`（[queue.defn]/1），而容器要求里写着，往尾巴放元素的函数抛了异常就当没发生（[container.reqmts]/66.2、[deque.modifiers]/3）。队列保持原样，锁由 `unique_lock` 的析构放掉，被跳过的 `notify_one` 不欠任何人，条件没变，本来就没有谁该被叫醒。承诺有一个例外：T 只能移动、而移动构造会抛的时候，效果未指明（[deque.modifiers]/3 的最后一句）。这道闸在下面用 `static_assert` 落地。

`pop` 的路更险。`out = std::move(queue_.front())` 把货从队头搬出来，之后才 `queue_.pop()`。移动构造要是抛了呢？元素还躺在队列里，调用者没拿到，队列也没删，下次 pop 遇上的还是它。我用一个从第 2 次移动起必抛的类型跑了实证。为了让这道闸不拦路，我另搭了一个副本装它，起名 NaiveQueue，形状就是第 1 节的教学版，`pop` 还是按值返回的旧签名：

```cpp
// 移动构造从第 2 次起必抛
struct Fragile {
  int v;
  inline static int moves = 0;
  explicit Fragile(int x) : v(x) {}
  Fragile(const Fragile&) = delete;
  Fragile(Fragile&& o) : v(o.v) {
    if (++moves >= 2) throw std::runtime_error("第 2 次移动起，一搬就爆");
  }
};

NaiveQueue<Fragile> nq(4);
nq.push(Fragile{7});
try {
  Fragile got = nq.pop();  // 从队列往外搬的那次移动
  std::printf("pop -> %d\n", got.v);
} catch (const std::runtime_error& e) {
  std::printf("pop 抛出：%s\n", e.what());
}
```

push 能成功，因为 C++17 的省略规则让入参少走一次移动，进容器的是第 1 次。从队列往外搬是第 2 次，咱们看它撑不撑得住：

```text
push 成功，队列里 1 件
pop 抛出：第 2 次移动起，一搬就爆
队列里 1 件（元素还卡着）
再 pop 又抛：第 2 次移动起，一搬就爆
队列里 1 件
```

那 1 件永远取不出来，每回 pop 都在同一处摔倒。标准对这类局面不给承诺，咱们自己给：把 T 的移动构造与移动赋值都要求不抛，写进 `static_assert`，编译期就把不合格的类型拦下：

```cpp
template <typename T>
class BoundedQueue {
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "T must be nothrow move constructible");
  static_assert(std::is_nothrow_move_assignable_v<T>,
                "T must be nothrow move assignable");
  // ...
};
```

第二道拦的是另一条缝：您复用 `out` 连续取件时，赋值要是抛了，元素同样卡在队头。咱们拿一个能移动但不承诺不抛的类型，起名 BrokenMove，单独喂给编译器一个最小翻译单元，f3_static_fail.cpp，内容只有类型定义加一行实例化：

```text
queue.hpp: In instantiation of ‘class BoundedQueue<BrokenMove>’:
f3_static_fail.cpp:11:29:   required from here
queue.hpp:16:22: error: static assertion failed: T must be nothrow move constructible
   16 |   static_assert(std::is_nothrow_move_constructible_v<T>,
      |                 ~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

拦下它的是第一道闸，连移动构造都不承诺不抛，还没轮到第二道出场。拦截发生在编译期。`int`、`std::string`、`std::unique_ptr` 这些常用类型都过得去。真有类型过不了闸，您别怪队列苛刻，它自己的移动操作确实还欠着不抛的承诺。

析构。队列对象销毁时，剩下的元素逐个析构。栈展开进行到一半时，析构函数没有抛异常的退路：再抛新的，`std::terminate` 直接接手（[except.terminate]/1.4）。所以您给 T 的纪律只有一条，析构不抛。

锁的释放是全场兜底。每个方法里的锁都由 RAII 对象持有，正常走释放，异常走也释放。`wait` 内部锁带出去、带回来的交接是标准保证的原子动作，3.1 讲过。异常路径上真正要您操心的，只有两处搬运。

## 轮到您动手的时候

动笔写队列，或者接手别人的队列，我会把这些问题过一遍：

1. 两个方向的谓词各是什么？改条件的人拿的锁，与等条件的人拿的锁，是同一把吗？
2. 您的队列打算给几种背压？被丢掉的每一件，调用者知情吗，知情之后能做什么？
3. `close()` 之后谁等消费者退完？队列对象出作用域之前，还有没有人睡在它的条件变量上？
4. T 的移动构造、移动赋值、析构，三样敢不敢都不抛？有一样不敢，队列会在哪一步漏？
5. 消费者加工一件要多久？容量按消费一件的耗时乘上您能接受的等待来估，比拍脑袋靠谱。

## 下一步

超时等待没有进本篇。超时等待的通用做法与停止令牌归 3.3《等待也需要结束路径》，`condition_variable_any` 带 `std::stop_token` 的那组重载也在那里。迁移前旧 ch04 那篇[《线程安全队列》](../ch04-concurrent-data-structures/01-thread-safe-queue)，接着旧 ch02 那篇[《condition_variable 与等待语义》](../ch02-mutex-condition-sync/03-condition-variable)的教学版 `BoundedQueue` 写过关闭、超时与背压，您可以对照着读。差别在推进的依据，本篇每一步都带读数。

单元收尾的阶段作品，是一个能关闭、能结束等待的生产者—消费者程序，还没动笔，定在 3.4《按协作需求选择工具》讲完 latch、barrier 与信号量之后。本篇交到您手里的队列，正好是它的地基。

## 参考资源

本篇的六组实验都在同一台电脑上编译实测（WSL2，16 逻辑核），GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-16。源码连同头文件 `queue.hpp` 一起放在 /tmp/concurrency-u3/ 下，临时目录系统会清，正文里的代码块就是全部内容，把片段拼回队列类、补上 `cstdio` 与 `stdexcept` 这类头文件即可复跑。并发读数每次运行都会动，正文引用的数字是当天那一遍，量级比具体值可靠，您复跑时对量级、不对尾数。条件变量与容器的条款一律按 eel.is 上的现行工作草案引，小节名与正式标准一致，段号可能挪动。cppreference 本回抓到了 `std::queue` 与 `std::condition_variable` 两页全文，与草案核对无出入。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是我自己写的。

- [std::condition_variable（cppreference）][cppref-cv]，C++11 起。
- [std::queue（cppreference）][cppref-queue]，容器适配器，默认底层容器是 `deque`。
- [Class condition_variable（现行草案 thread.condition.condvar）][draft-condvar]，`notify_one` 与 `notify_all` 的效果、解除阻塞的来路、谓词版的等价展开、构造函数可抛 `system_error`、析构的前置条件。
- [Class template queue（现行草案 queue.defn）][draft-queue]，`push` 委托给底层容器的 `push_back`。
- [Modifiers（现行草案 deque.modifiers）][draft-deque-modifiers]，尾端插入的异常承诺，与“只能移动的 T 移动构造抛出时效果未指明”的例外。
- [Container requirements（现行草案 container.reqmts）][draft-creq]，`push_back` 类操作抛了无效果、`pop` 类操作不抛的总要求。
- [The std::terminate function（现行草案 except.terminate）][draft-terminate]，栈展开中析构抛出即终止。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[cppref-queue]: https://en.cppreference.com/w/cpp/container/queue
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-queue]: https://eel.is/c++draft/queue.defn
[draft-deque-modifiers]: https://eel.is/c++draft/deque.modifiers
[draft-creq]: https://eel.is/c++draft/container.reqmts
[draft-terminate]: https://eel.is/c++draft/except.terminate
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
