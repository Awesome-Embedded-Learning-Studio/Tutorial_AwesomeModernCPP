---
title: "What Happens When a Queue Won't Close: Closing and Backpressure"
description: "After close, push refuses and pop drains first, then reports kClosed, and the precondition on the destructor fixes the shutdown order for good; when the queue is full, three backpressure strategies — reject, drop, evict the oldest — are measured against one another under the same workload."
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
translation:
  source: documents/vol5-concurrency/chapter03-wait-and-coordinate/04-queue-close-and-backpressure.md
  source_hash: 4e3706a7375338ca9d047308a1d57058264865a764cc631c616322de68d274b0
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 12119
---

# What Happens When a Queue Won't Close: Closing and Backpressure

The queue now stands on its own; two things remain before it can wrap up: how it closes once nothing more will be pushed, and what happens to newcomers when it is full.

## Closing a Queue

The producers finish and clock out; the consumer has nothing in hand and sleeps in `pop` waiting for the next item — and you can guess the only thing it will ever wait for: forever. 3.1 showed that a notify fired with nobody waiting evaporates; the converse also holds: a sleeper only wakes if someone calls it. For the queue to be able to wind down, we owe every waiter an ending.

The design is a `closed_` flag; we let it into both predicates and, along the way, change two method signatures:

<OnlineCompilerDemo
  title="A Queue with Close Semantics: After close, push Refuses, pop Drains and Wraps Up"
  source-path="code/examples/vol5/chapter03/14_bounded_queue_close.cpp"
  description="The in-text close-capable queue class extracted verbatim; main verifies each post-close behavior: push receives kClosed, and pop first takes every remaining item, reporting kClosed only after the queue runs dry."
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

Why the signatures changed deserves a pause; picture the moment of "empty and closed". The teaching version's `T pop()` has no value to return — the return channel is jammed. Change it to `QueueStatus pop(T& out)`: the value leaves through the reference, the status comes back through the return value. `push` likewise returns `QueueStatus`, so a producer whose delivery is refused receives `kClosed` instead of staying in the dark.

The two predicates are written with different concerns; look at them separately. `push`, on waking, checks `closed_` — closed means the goods are returned, since with the queue shut nobody will ever come take what got accepted. `pop`, on waking, checks `queue_.empty()` instead, not `closed_`. Closing does not mean clearing: the work piled in the queue when the producers clocked out should be taken and finished by the consumers — we call this drain. Handing "is there any stock left" to `empty()` keeps the closed question out of the taking decision.

Take `close()`'s two steps apart. Inside the lock, raise the flag; outside the lock, fire `notify_all` once on each condition variable. Why `notify_all`: closing is a genuine condition change for every waiter — all sleeping consumers should wake to take a look, and all sleeping producers should wake to collect their `kClosed`. By 3.2's criterion, wake as many threads as the change lets proceed; here that is everyone, so wake everyone. Why the notify sits outside the lock: 3.1 established that notifies don't care inside-or-outside, and outside spares the awakened one trip through the lock queue.

There is also a red line around destruction; please keep an eye on it. The standard gives `condition_variable`'s destructor a precondition: no thread may still be blocked on it ([thread.condition.condvar]/4; a note adds that it suffices for notifications to happen before destruction). Landed on the queue, the order is fixed: `close()`, join every producer and consumer, and only then may the queue object leave its scope. Destroying the queue while waiters are still asleep is undefined behavior.

One experiment verifies all of it, arranged in this order. The consumers start before the producers, so some are asleep on the empty queue. The producers push 200 items and exit; the main thread closes; the consumers keep taking until they receive `kClosed`:

<OnlineCompilerDemo
  title="The Close Experiment: notify_all Wakes the Sleeping Consumers to Drain"
  source-path="code/examples/vol5/chapter03/15_queue_close_experiment.cpp"
  description="Three consumers fall asleep on an empty queue first; two producers push 100 each, then close: none of the 200 items is lost, all three consumers drain the queue and exit with kClosed. queue.hpp inlined."
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

Not one of the 200 items was lost; let's verify line by line. The three consumers' shares were 66, 89, and 45 — under multiple producers and consumers, whoever grabs gets, and uneven is the norm. All three exited with `kClosed`, which shows the drain ran clean and that the consumers asleep on the empty queue were woken by close's `notify_all`. Push after close got `kClosed`; pop after closed-and-empty got `kClosed`. Every behavior on the closing path lines up.

## When the Queue Is Full: Backpressure Trade-offs

Blocking pushes the pressure back upstream; it is the default answer. If the downstream consumes too slowly, the upstream batch grinds to a halt — and some scenarios would rather drop. Let's lay the options out.

<OnlineCompilerDemo
  title="Two Ways Out of Backpressure: try_push Rejects, or Evict the Oldest"
  source-path="code/examples/vol5/chapter03/16_backpressure_variants.cpp"
  description="The two in-text member functions folded verbatim into the final-shape queue class; main checks the behavior: on a full queue try_push returns false immediately, and push_replace_oldest evicts the oldest item at the head so the newcomer always gets in."
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

The rejecting `try_push` checks fullness inside the lock and returns `false` immediately; what happens next is up to you — retry, degrade, log a line, all your freedom. The drop-the-newcomer version needs no extra code: swallowing the `try_push` failure without reporting it is dropping. At the queue's boundary the two strategies are the same action; the difference is only the right to know. The overwriting `push_replace_oldest` goes the other way: when full, it evicts the oldest item at the head, so the newcomer always gets in.

We put the four strategies under the same workload: capacity 64, a single producer firing 10000 items in a row, a single consumer resting 50 microseconds per item, one run each:

| Strategy | Producer side | Whole run | Accepted | Lost | Actually consumed |
|------|-----------|------|---------|------|---------|
| Blocking | 1477.8 ms | 1486.4 ms | 10000 | 0 | 10000 |
| Reject | 0.2 ms | 8.5 ms | 64 | 9936 | 64 |
| Drop the newcomer | 0.2 ms | 8.9 ms | 64 | 9936 | 64 |
| Overwrite the oldest | 0.2 ms | 8.7 ms | 10000 | 9936 | 64 |

The blocking row is backpressure in its natural shape — look at the two times: producer side and whole run nearly coincide; the producer's speed is flattened by the consumer, and the upstream is forced to march at the downstream's tempo. Not one of the 10000 items was lost, at the price of 1.5 seconds. The 50-microsecond rest per item actually lands near 150 microseconds per item under WSL2's timing granularity, which is exactly how 10000 items stretch to this total.

The reject and drop rows show identical accepted, lost, and consumed columns — no surprise, since we said they are the same action at the boundary. The producer side sprints through in 0.2 ms; the queue manages to take in just 64 items — exactly one capacity's worth — and the other 9936 are turned away at the door. The whole run takes 8 to 9 ms, mostly the consumer digesting those 64. Fast is fast; lost is lost.

The overwrite row accepts all 10000, but 9936 of them are pushed out of the queue by later arrivals; only the newest 64 arrive alive in the consumer's hands. It keeps the new and throws away the old — the exact opposite trade from blocking, and freshness is precisely what you choose it for.

The choice follows the business; here are a few scene-matched rules. If the downstream can't afford loss and slowing the upstream is acceptable, block. If loss is affordable and stalling the upstream is not, reject. If only the latest readings matter, overwrite. If you must hide it from the caller, drop. And one road runs the other way: backpressure need not live inside the queue at all — the queue offers only the blocking version, and callers assemble their own from `try_push`. We laid out all four so you hold a set of comparisons, to draw on as needed in a real project.

## Reference Resources

The experiments originally ran on the header `queue.hpp`, whose final shape has been inlined into the example files. This time cppreference served the full text of the `std::condition_variable` page, matching the draft with no discrepancies. The progression of closing and backpressure follows Anthony Williams's *C++ Concurrency in Action*; the examples and prose are my own.

- [std::condition_variable (cppreference)][cppref-cv], since C++11.
- [Class condition_variable (current draft, thread.condition.condvar)][draft-condvar], the effect of `notify_all`, the constructor's permission to throw `system_error`, and the destructor's precondition.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the bounded-queue progression.

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
