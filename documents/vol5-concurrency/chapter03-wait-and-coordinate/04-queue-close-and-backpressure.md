---
title: "队列关不上会怎样：关闭与背压"
description: "close 之后 push 拒收、pop 先 drain 再报 kClosed，析构的前置条件把收场次序定死；队列满了，拒绝、丢弃、挤掉最老三条背压放在同一份负载下对着量。"
chapter: 5
order: 4
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

# 队列关不上会怎样：关闭与背压

队列站住了，收场还差两件事：不再投放之后它怎么关，装满之后新来的怎么办。

## 关闭一个队列

生产者放完收工，消费者手里没活，睡在 `pop` 里等下一件，您能想到它等到的只有永远。3.1 讲过，通知发出去没人等就蒸发，反过来也成立：睡着的人要醒来，得有人去叫。队列要能收场，咱们就欠每个等待者一个尽头。

设计是一个 `closed_` 标志，咱们让它进两个谓词，顺带改两个方法的签名：

<OnlineCompilerDemo
  title="带关闭语义的队列：close 之后 push 拒收、pop 排空收尾"
  source-path="code/examples/vol5/chapter03/14_bounded_queue_close.cpp"
  description="正文 close 版队列类原样抽出，main 把关闭后的行为各验一遍：push 收到 kClosed，pop 先把剩下的货取干净、取空之后才报 kClosed。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// bounded_queue_close.cpp —— 带关闭语义的队列类：close 之后 push 拒收，pop 先取干净再报 kClosed
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>
#include <cstdio>

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

int main() {
  BoundedQueue<int> q(4);
  q.push(1);
  q.push(2);
  q.close();   // 生产者收工，收盘

  std::printf("关闭后再 push -> %s\n",
              q.push(3) == QueueStatus::kClosed ? "kClosed" : "kOk");
  int v = 0;
  while (q.pop(v) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", v);   // drain：把剩下的货取干净
  }
  std::printf("取空且已关，再 pop -> %s，size = %zu\n",
              q.pop(v) == QueueStatus::kClosed ? "kClosed" : "kOk", q.size());
  return 0;
}
```

</OnlineCompilerDemo>

签名为什么变，值得停一下，您想想“空且已关”的时刻。教学版 `T pop()` 拿不出值来返回，返回值通道堵死。改成 `QueueStatus pop(T& out)`，值走引用出去，状态走返回值回来。`push` 同样改回 `QueueStatus`，生产者放货被拒时能收到 `kClosed`，不至于蒙在鼓里。

两个谓词的写法各有讲究，您分开看。`push` 醒来之后查 `closed_`，关了就退货，队列都关了，收下的货没人会来取。`pop` 醒来之后查的却是 `queue_.empty()`，不是 `closed_`。关闭不等于清空：生产者收工时压在队列里的活，消费者该取干净、干完，咱们叫它 drain。把“还有没有货”交给 `empty()`，关不关就不掺和取货的判断。

`close()` 的两步咱们分开看。锁内把标志立起来，锁外对两把条件变量各 `notify_all` 一次。为什么是 `notify_all`：关闭对每个等待者都是真的条件变化，睡着的消费者全该醒来看一眼，睡着的生产者全该醒去收 `kClosed`。按 3.2 的判断法，一次变化够几条线程往下走就叫几条，这里够所有人，那就叫所有人。为什么通知放锁外：3.1 说过通知不挑锁里锁外，放锁外让醒来的人少排一趟队。

还有一条跟析构有关的红线，得请您留神。标准给 `condition_variable` 的析构写了前置条件：不许还有线程堵在上面（[thread.condition.condvar]/4，附注说只要通知发生在析构之前就成）。落到队列上，次序是死的：`close()`，join 掉所有生产者与消费者，然后才轮到队列对象出作用域。等待者还睡着就把队列析构，是未定义行为。

实验把它们一次验掉，咱们安排的次序是这样。消费者比生产者早起步，有人睡在空队列上。生产者放完 200 件退出，主线程 close，消费者一直取到收到 `kClosed` 为止：

<OnlineCompilerDemo
  title="关闭实验：notify_all 把睡着的消费者叫醒去 drain"
  source-path="code/examples/vol5/chapter03/15_queue_close_experiment.cpp"
  description="三个消费者先睡在空队列上，两个生产者各放 100 件后 close：200 件一件不丢，三位消费者取干净并以 kClosed 退出。queue.hpp 已内联。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// queue_close_experiment.cpp —— 关闭实验：消费者睡在空队列上，close 的 notify_all 把它们叫醒去 drain
// 队列类是 3.3 起三篇搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <atomic>
#include <cstdio>
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

</OnlineCompilerDemo>

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

阻塞把压力顶回上游，它是默认答案。下游消费太慢，上游整批卡住，有的场景宁可丢。咱们把选项摆开。

<OnlineCompilerDemo
  title="背压的两条出路：try_push 拒绝、挤掉最老的"
  source-path="code/examples/vol5/chapter03/16_backpressure_variants.cpp"
  description="正文两个成员函数原样收进最终形状的队列类，main 验行为：满了 try_push 立即返回 false，push_replace_oldest 挤掉队头最老的一件，新来的必进。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// backpressure_variants.cpp —— 队列满了的两条出路：try_push 立即拒绝，push_replace_oldest 挤掉最老的
#include <condition_variable>
#include <cstddef>
#include <cstdio>
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

private:
  std::queue<T> queue_;
  std::size_t capacity_;
  bool closed_ = false;
  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
};

int main() {
  BoundedQueue<int> q(4);
  for (int i = 0; i < 4; ++i) q.push(i);   // 装满

  bool ok = q.try_push(99);
  std::printf("满了 try_push(99) -> %s\n", ok ? "true" : "false");

  bool evicted = false;
  q.push_replace_oldest(100, evicted);     // 挤掉队头的 0
  std::printf("满了 push_replace_oldest(100)，挤掉最老的 -> %s\n",
              evicted ? "true" : "false");

  int v = 0;
  while (q.pop(v) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", v);
  }
  return 0;
}
```

</OnlineCompilerDemo>

拒绝版 `try_push` 锁内查满，满了立即返回 `false`，接下去怎么走由您定：重试、降级、记一笔日志，都是您的自由。丢弃新来的版本不另写代码，把 `try_push` 的失败瞒住不报，就是丢弃，队列边界上两个策略是同一个动作，差别只在知情权。覆盖版 `push_replace_oldest` 反着来，满的时候挤掉队头最老的一件，新来的永远进得去。

咱们把四种策略放到同一份负载下：容量 64，单生产者连发 10000 件，单消费者每件歇 50 微秒，各跑一遍：

| 策略 | 生产端用时 | 全程 | 受理入队 | 丢失 | 实际消费 |
|------|-----------|------|---------|------|---------|
| 阻塞 | 1477.8 ms | 1486.4 ms | 10000 | 0 | 10000 |
| 拒绝 | 0.2 ms | 8.5 ms | 64 | 9936 | 64 |
| 丢弃新来的 | 0.2 ms | 8.9 ms | 64 | 9936 | 64 |
| 覆盖最老的 | 0.2 ms | 8.7 ms | 10000 | 9936 | 64 |

阻塞行是背压本来的样子，您看两个时间：生产端用时与全程几乎重合，生产者的速度被消费者压平，上游被迫按下游的节奏走。10000 件一件没丢，代价是 1.5 秒。消费每件歇 50 微秒的设定，在 WSL2 的计时精度下实际接近 150 微秒一件，10000 件正好压出这样的总长。

拒绝与丢弃两行，受理、丢失、消费三列一模一样，咱们不意外，前文说过它们在边界上本来就是同一个动作。生产端 0.2 毫秒冲完，队列只来得及收下 64 件，正好一格容量的量，其余 9936 件被挡在门外。全程 8 到 9 毫秒，大半还是消费者消化 64 件的工夫。快是真快，丢也是真丢。

覆盖行 10000 件全部受理，可其中 9936 件在队列里被后来的挤掉，活着到消费者手里的只有最新的 64 件。它保住的是新，扔掉的是旧，取舍与阻塞正好相反，您选它图的就是一个新字。

挑法随业务走，笔者给您几条对得上场景的。下游丢不起、上游减速可接受，用阻塞。丢得起、上游卡不起，用拒绝。只在乎最近的读数，用覆盖。要替调用者隐瞒，用丢弃。还有一条反向的路：背压也可以不放队列里，队列只提供阻塞版，调用者拿 `try_push` 自己拼。咱们把四种都摆出来，是让您手上有一套对照，真项目里按需取用。

## 参考资源

各实验原本跑在头文件 `queue.hpp` 上，它的最终形状已内联进示例文件。cppreference 本回抓到了 `std::condition_variable` 一页全文，与草案核对无出入。关闭与背压的推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是笔者自己写的。

- [std::condition_variable（cppreference）][cppref-cv]，C++11 起。
- [Class condition_variable（现行草案 thread.condition.condvar）][draft-condvar]，`notify_all` 的效果、构造函数可抛 `system_error`、析构的前置条件。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，有界队列的推进顺序参考。

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
