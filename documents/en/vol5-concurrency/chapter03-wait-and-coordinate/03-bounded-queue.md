---
title: "A Queue with Capacity: Getting It to Stand First"
description: "Land the previous article's condition variables on a real component: one condition variable per direction — empty and full — one lock guarding all state, and correctness measured under multiple producers and consumers with wasted-wakeup counts and a full-strength acceptance run."
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
translation:
  source: documents/vol5-concurrency/chapter03-wait-and-coordinate/03-bounded-queue.md
  source_hash: 4c4f902834387e09357f2bdb972ada1d5975d837feb7b6a70b53bbb34e81e0e5
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 15319
---

# A Queue with Capacity: Getting It to Stand First

Producers put items in, consumers take them out, capacity is capped, and when the two sides run at different speeds each goes to sleep and gets woken in turn. Over the next three articles we will build it, and when we are done you will hold a queue that can be closed, can apply backpressure (make producers wait when full, or bounce them back immediately), and stands firm under multiple producers and consumers. As far as the design goes, that is as far as I will put the measurements.

## Starting with a Teaching Version: Two Directions, Two Conditions

Two directions of waiting live in this queue. When it is empty, consumers wait for "not empty". When it is full, producers wait for "not full". The ones who change "not empty" are precisely the producers, and the ones who change "not full" are precisely the consumers; the two sides never mingle, so we give each its own condition variable:

<OnlineCompilerDemo
  title="A Teaching Version of the Bounded Queue: pop Waits When Empty, push Waits When Full"
  source-path="code/examples/vol5/chapter03/10_bounded_queue_teaching.cpp"
  description="The in-text teaching queue class extracted verbatim, plus a main: capacity 2 with 3 items pushed, the producer waits once for a slot, and the consumer takes all three in order. Two condition variables, one per direction."
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

Meet the five members one by one. A `std::queue` holds the items, with `deque` as the default underlying container ([queue.defn]/1); a number records the capacity; one mutex; two condition variables. The names are blunt: everyone asleep on `not_full_` is a producer, everyone asleep on `not_empty_` is a consumer. Both waits carry predicates — the two lines of defense against spurious wakeups and lost wakeups from 3.1 land right here, so we won't repeat them.

The direction of each notify is likewise nailed down. `push` changes "goods available", so it wakes only `not_empty_`. `pop` changes "slot available", so it wakes only `not_full_`. When you put one item into the queue, your fellow producers waiting for a slot are owed no notify — the sleeping consumers are. What happens when the directions get mixed, we will show with readings once we reach multi-producer, multi-consumer correctness below.

## Queue Empty, Queue Full

Each direction's behavior is one plain sentence: queue empty, `pop` sleeps; queue full, `push` sleeps. Easy to say — how long the sleep lasts belongs to the field. I wrote a small experiment: capacity 2, the consumer rests 100 ms per item, the producer pushes 4 items in a row, timing each push. Then flipped around: the queue sits empty, the consumer enters pop, the producer pushes one item only after 80 ms, and pop gets timed too. Most experiments in this article run on the header `queue.hpp`, which is the final shape assembled across these three articles; its interfaces take the stage one by one in later articles, and the variants a few experiments use are explained in the text — for now just take `push` and `pop` as working:

<OnlineCompilerDemo
  title="Queue Empty, Queue Full: One Blocking Measurement per Direction"
  source-path="code/examples/vol5/chapter03/11_queue_full_empty.cpp"
  description="Capacity 2, consumer resting 100 ms per item, producer pushing 4 items with each push timed; then flipped, timing how long pop waits for goods. The original experiment sits on the header queue.hpp; here the final-shape queue is inlined into the same file."
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

On the full side, 3 of the 4 pushes take almost no time, and 1 sleeps for 100.4 ms. Why it slept is straightforward: no slots left, the predicate fails, it queues up on `not_full_`, and only wakes once the consumer takes an item out. Why did the block land on the 4th push rather than the 3rd? Both threads run concurrently; the 1st item was taken by the consumer the instant it landed, so by the time the producer reaches the tail, the queue actually holds 2 items. The readings come from one real run; which push blocks is up to the scheduler — a re-run might block on the 3rd, or sleep on both, but "at least one push waits out one item's consumption time" holds every time.

On the empty side, pop takes 59.7 ms and gets exactly the 99 the producer pushed. The arithmetic is easy; let's walk it: the producer is scheduled to deliver at 80 ms, the consumer enters pop at 20 ms, 80 minus 20 — you wait around 60 ms, and the small drift is wakeup plus scheduling, the same order of magnitude as the latency measured in 3.1.

The capacity hides one more thing worth saying. Filling `capacity_` with 0 is legal, but `size() < 0` never holds: in the teaching version, push sleeps forever; in the formal version, it sleeps until close comes to wake it. A capacity of 0 amounts to declaring that producers may never enter — the compiler won't stop you, so checking whether the parameter makes sense is on you.

## Correctness Under Multiple Producers and Consumers

### Why Only One Lock

The queue's invariant is a whole; let's walk it piece by piece. While `size` is being changed, the queue head must not be snatched away by someone else. While emptiness is being checked, nobody can cut in line with a push. The close flag to be added later must also mesh with the item count. Give producers and consumers a lock each, and gaps open between checking and changing, while the two locks' acquisition order opens a door to deadlock. So all shared state shares one `mutex_`; whoever enters or exits queues up. The lock's job is "no fighting".

### Why Two Condition Variables

The lock keeps order; the condition variables do the waking — and waking the wrong person costs differently. 3.2 verified that `notify_one` picks without looking at conditions: which sleeper gets woken is the implementation's call. With two groups asleep on one cv, a producer's notify after changing the condition may wake another producer. The consequences come in two layers; let's take them one at a time.

The lighter layer is the wasted wakeup; just book the cost under "waking". The awakened producer finds no slots (a colleague just filled them all) and goes back to sleep. Correctness is covered by the predicate; what is wasted is one wakeup.

Wasted wakeups can be counted, so we write the counting into the wait loop. The workload: capacity 4, 2 producers pushing 4000 items each, 2 consumers taking 4000 each, taking counts as done, no processing. The counting method: after `wait` returns, look at the condition once more, and if it still fails, count one:

<OnlineCompilerDemo
  title="The Wasted-Wakeup Bill of One cv: Count One Whenever Waking Finds the Condition Still False"
  source-path="code/examples/vol5/chapter03/12_one_cv_wasted.cpp"
  description="The in-text fragment completed into a full queue: after wait returns, re-check the condition and count one if it still fails. The fragment shares one wasted counter across both sides; this version keeps separate tallies per side, same workload: capacity 4, 2×4000 items. Wasted-wakeup counts differ on every run."
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

Apart from whom they notify and whom they wait on, the one-cv queue and the two-cv queue are identical in shape. We ran each three times:

```text
一把 cv : 生产侧白跑    276 次  消费侧白跑    233 次
一把 cv : 生产侧白跑    232 次  消费侧白跑    233 次
一把 cv : 生产侧白跑    224 次  消费侧白跑    240 次
两把 cv : 生产侧白跑    176 次  消费侧白跑    148 次
两把 cv : 生产侧白跑    148 次  消费侧白跑    113 次
两把 cv : 生产侧白跑    128 次  消费侧白跑    120 次
```

Two cvs cut wasted wakeups nearly in half: per run, the totals fall from 464-509 to 248-324. They can't reach zero, because the same direction also competes: one item feeds only one consumer, and a consumer woken too late finds a colleague already took it and has to sleep again. Slots work the same: two producers fighting over one slot can both come away empty-handed. The remaining gap is the extra tuition one cv pays — read on, and the heavier layer comes next.

### When the Other Side Swallows the Notify

Drop a one-cv queue into multiple producers and consumers, and you can assemble something more painful than a wasted wakeup. Let me walk you through it in order, with capacity noted as 4. The queue is full; producers P1 and P2 both sleep on the cv; consumer C holds one item and is processing it. C finishes processing, takes one item out, `notify_one`, and wakes P1. P1 pushes a new item in and fires `notify_one` again. The only sleeper now is P2, so this notify lands on P2. P2 wakes, finds the slots gone again — P1 just refilled the queue — and goes back to sleep. At this point the queue holds 4 items, everyone asleep is a producer, and C is the only one who might come take something. If C finishes what's in hand and wraps up, nobody will ever push or pop again; the goods and P2 stand still together in the night.

Under a real workload, a deadlock-grade stalemate needs several things to line up: the notify landing exactly on an opposite-side waiter, and no pop afterwards to make up the call. So it is usually not this scary — more often it shows up as one item sleeping through an extra full round of processing, and the tail of a real project's latency curve sprouts exactly such long tails. A genuine standstill requires every one of those coincidences to line up; you'll rarely meet one in daily work, and when you do, it is hard to diagnose.

The fix lies in direction: we tighten where notifies may go. `push` wakes only `not_empty_`; a producer is not even eligible to swallow a notify, because it never sleeps on `not_empty_` in the first place. A few extra lines for two cvs buy you a timeline you can reason about. This is also how 3.2's criterion lands on queues: when conditions are separate, one condition variable per condition, each waking its own.

### The Lost-Wakeup Defense

The defense itself is still 3.1's three rules — predicate, same lock, notify after the change — and we seat them at three places in the queue. The waits in `push` and `pop` both carry predicates and must re-verify on waking. All reads and writes of shared state sit inside one `mutex_`, so the condition-changer and the condition-checker hold the same lock. Notifies come after the change: in `push`, after `queue_.push`; in `pop`, after `queue_.pop()`. Closing adds a fourth line, `notify_all`, because the condition close changes is one everyone must check — the next article, *What Happens When a Queue Won't Close: Closing and Backpressure*, covers it.

### Acceptance, by the Numbers

Correctness cannot rest on a feeling. I staged a full-strength acceptance run: 4 producers pushing 2500 items each, numbered "producer index times 2500 plus item index", unique throughout; 4 consumers taking 2500 each; capacity squeezed to 8, forcing both sides to sleep and wake constantly. The acceptance checks three things: total enqueued matches total dequeued, no item is taken twice, and the checksum of all item numbers equals the sum of 0 through 9999, which is 49995000:

<OnlineCompilerDemo
  title="Multi-Producer, Multi-Consumer Acceptance: Counts, Duplicates, Checksum"
  source-path="code/examples/vol5/chapter03/13_queue_acceptance.cpp"
  description="4 producers pushing 2500 each, 4 consumers taking 2500 each, capacity squeezed to 8: totals match, nothing is taken twice, and the checksum equals 0 through 9999 summed. queue.hpp inlined."
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

The readings are clean, and the warrant sits in the structure: every read and write of shared state happens inside the same lock, nobody can slip around it, and every wakeup is verified by a predicate. However the scheduler stirs, counts won't run high, run low, or double up — carry the same structure into your own program and the warrant travels with it. The `QueueStatus` and `pop(T&)` used in the code are interfaces that formally debut in the next article, *What Happens When a Queue Won't Close: Closing and Backpressure*; they are borrowed ahead of time here, with the reason given in passing: what we are validating is the final-shape queue, the dequeue count is fixed, and close does not need to appear yet.

## Reference Resources

The experiments originally ran on the header `queue.hpp`, whose final shape has been inlined into the example files. This time cppreference served the full text of both `std::queue` and `std::condition_variable`, and they matched the draft with no discrepancies. The progression of the bounded queue follows Anthony Williams's *C++ Concurrency in Action*; the examples and prose are my own.

- [std::condition_variable (cppreference)][cppref-cv], since C++11.
- [std::queue (cppreference)][cppref-queue], a container adaptor whose default underlying container is `deque`.
- [Class condition_variable (current draft, thread.condition.condvar)][draft-condvar], the notify effects that two direction-specific condition variables rely on.
- [Class template queue (current draft, queue.defn)][draft-queue], `push` delegates to the underlying container's `push_back`.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[cppref-queue]: https://en.cppreference.com/w/cpp/container/queue
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-queue]: https://eel.is/c++draft/queue.defn
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
