---
title: "When a Move Throws Halfway, Who Pays: Exception Safety of the Queue"
description: "Exceptions thrown halfway from three places — construction, the copy in push, the move in pop — with what remains inside and outside the queue measured scene by scene, ending at two compile-time gates and the discipline of non-throwing destructors."
chapter: 5
order: 5
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
  source: documents/vol5-concurrency/chapter03-wait-and-coordinate/05-queue-exception-safety.md
  source_hash: cd49ce47112a08fd812f4b5fa627fd4829f6cb19549df1e3685aa31c5cb317cb
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 14384
---

# When a Move Throws Halfway, Who Pays: Exception Safety of the Queue

The queue can close and it can push back, but the trickiest class of scenes remains: an exception thrown halfway through construction or a move, and the question of what belongs to whom, inside and outside the queue.

## Exception Safety

For exception safety we inspect three places: construction, every step of moving in and out, and destruction. Lock release is the net under the whole scene; we cover it as well.

Construction. Walk the members in declaration order. `std::queue`'s first allocation may throw `bad_alloc`. `condition_variable`'s constructor may throw `system_error` per the standard ([thread.condition.condvar]/2 — insufficient resources, for instance). The language's rules catch this case: whichever member in the initializer list fails to finish constructing, the already-constructed members are destroyed in reverse order. No half-built object can leak out, and the finished parts won't leak either — nothing for you to worry about.

The path through `push`. The parameter is taken by value, so when the caller passes an lvalue, the copy happens before the function body is entered. I built a type whose copy constructor throws without fail from the 3rd call on, to see whether the queue gets so much as scratched:

<OnlineCompilerDemo
  title="FlakyCopy, Whose Copies Always Throw: The Type Definition Lives in the Full Experiment File"
  source-path="code/examples/vol5/chapter03/17_flaky_copy_push.cpp"
  description="The type definition: copy constructor throws from the 3rd call on, moves never throw. Shared with the next demo in the same file flaky_copy_push.cpp — queue and driver are all in there; here we first meet the type itself."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// flaky_copy_push.cpp —— 拷贝构造从第 3 次起必抛的类型：第 3 件左值抛在进 push 函数体之前
// 队列类是 3.3 起三篇搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stdexcept>
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

int main() {
  BoundedQueue<FlakyCopy> q(8);
  FlakyCopy a{1}, b{2}, c{3};
  q.push(a);            // 第 1 次拷贝，花在进参数上
  q.push(b);            // 第 2 次
  try {
    q.push(c);          // 第 3 次，抛在进函数体之前
  } catch (const std::runtime_error& e) {
    std::printf("push(c) 抛出：%s\n", e.what());
  }
  std::printf("队列里 %zu 件（应为 2）\n", q.size());
  q.push(FlakyCopy{4}); // 右值：一路移动，不碰拷贝构造
  std::printf("再 push 一件右值，队列里 %zu 件\n", q.size());

  q.close();   // 收工，让下面的排空循环能取完退出
  FlakyCopy out{0};
  while (q.pop(out) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", out.v);
  }
  std::printf("收尾 size = %zu\n", q.size());
  return 0;
}
```

</OnlineCompilerDemo>

The call sequence driving it is short; lay it out once and the output will match:

<OnlineCompilerDemo
  title="push(c) Throws Before the Function Body: The Queue Doesn't Budge"
  source-path="code/examples/vol5/chapter03/17_flaky_copy_push.cpp"
  description="The first two lvalues each spend one copy; the third item's copy throws before entering push: the queue stops at 2 items, a later rvalue still enters normally, and all three are taken in order. FlakyCopy and the queue live in the same file."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// flaky_copy_push.cpp —— 拷贝构造从第 3 次起必抛的类型：第 3 件左值抛在进 push 函数体之前
// 队列类是 3.3 起三篇搭完的最终形状，原实验放在头文件 queue.hpp 里，这里一并内联
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stdexcept>
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

int main() {
  BoundedQueue<FlakyCopy> q(8);
  FlakyCopy a{1}, b{2}, c{3};
  q.push(a);            // 第 1 次拷贝，花在进参数上
  q.push(b);            // 第 2 次
  try {
    q.push(c);          // 第 3 次，抛在进函数体之前
  } catch (const std::runtime_error& e) {
    std::printf("push(c) 抛出：%s\n", e.what());
  }
  std::printf("队列里 %zu 件（应为 2）\n", q.size());
  q.push(FlakyCopy{4}); // 右值：一路移动，不碰拷贝构造
  std::printf("再 push 一件右值，队列里 %zu 件\n", q.size());

  q.close();   // 收工，让下面的排空循环能取完退出
  FlakyCopy out{0};
  while (q.pop(out) == QueueStatus::kOk) {
    std::printf("pop -> %d\n", out.v);
  }
  std::printf("收尾 size = %zu\n", q.size());
  return 0;
}
```

</OnlineCompilerDemo>

The first two items are pushed as lvalues, each spending one copy on the pass into the parameter. The third item's copy throws before the function body is entered; look at the output:

```text
push(c) 抛出：copy #3 爆了
队列里 2 件（应为 2）
再 push 一件右值，队列里 3 件
pop -> 1
pop -> 2
pop -> 4
收尾 size = 0
```

The queue doesn't budge: size still 2; the later rvalue push (moves all the way, never touching the copy constructor) enters as usual, and the three items are taken out clean, in order. Wherever the exception came in, the queue stops right there and carries on working afterwards.

Inside the function body, the promises come from the standard; let's read them clause by clause. `queue::push` delegates to the underlying container's `push_back` ([queue.defn]/1), and the container requirements say that if a function appending an element at the tail throws, it is as if nothing happened ([container.reqmts]/66.2, [deque.modifiers]/3). The queue stays as it was, the lock is released by `unique_lock`'s destructor, and the skipped `notify_one` owes nobody anything — the condition did not change, so nobody was owed a waking in the first place. The promise carries one exception: when T is move-only and its move constructor can throw, the effect is unspecified (the last sentence of [deque.modifiers]/3). How do we fence off that exception? Below, with a `static_assert`.

The path through `pop` is more dangerous. `out = std::move(queue_.front())` carries the goods out of the queue head, and only afterwards does `queue_.pop()` run. What if the move constructor throws? The element still lies in the queue, the caller never received it, and the queue never deleted it — the next pop meets the very same element. I ran an empirical test with a type whose move construction throws without fail from the 2nd time on. To keep the gate from blocking the way, I built a separate copy to hold it, named NaiveQueue, shaped exactly like the teaching version at the start of *A Queue with Capacity: Getting It to Stand First*, with `pop` still on the old return-by-value signature:

<OnlineCompilerDemo
  title="Fragile, Whose Moves Always Throw: The Element Sticks at the Head Forever"
  source-path="code/examples/vol5/chapter03/18_fragile_move_pop.cpp"
  description="Enqueueing skips one move thanks to elision; the move carrying the element out of the head always throws: the element sticks in the queue, and every pop trips over the same spot. It rides in a teaching-shaped NaiveQueue whose pop still returns by value."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// fragile_move_pop.cpp —— 移动构造从第 2 次起必抛的类型：元素永远卡在队头，每回 pop 同一处摔倒
// NaiveQueue 是 3.3 第 1 节教学版的副本，pop 按值返回的旧签名，专门装这个类型
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <utility>

template <typename T>
class NaiveQueue {
public:
  explicit NaiveQueue(std::size_t capacity) : capacity_(capacity) {}

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

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

private:
  std::queue<T> queue_;
  std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
};

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

int main() {
  NaiveQueue<Fragile> nq(4);
  nq.push(Fragile{7});
  std::printf("push 成功，队列里 %zu 件\n", nq.size());
  try {
    Fragile got = nq.pop();  // 从队列往外搬的那次移动
    std::printf("pop -> %d\n", got.v);
  } catch (const std::runtime_error& e) {
    std::printf("pop 抛出：%s\n", e.what());
  }
  std::printf("队列里 %zu 件（元素还卡着）\n", nq.size());
  try {
    Fragile got = nq.pop();  // 再来一回，还是那件，还是同一次摔倒
    std::printf("pop -> %d\n", got.v);
  } catch (const std::runtime_error& e) {
    std::printf("再 pop 又抛：%s\n", e.what());
  }
  std::printf("队列里 %zu 件\n", nq.size());
  return 0;
}
```

</OnlineCompilerDemo>

The push succeeds because C++17's elision rules spare the parameter one move — entering the container is move #1. Carrying it out of the queue is move #2; let's see whether it survives:

```text
push 成功，队列里 1 件
pop 抛出：第 2 次移动起，一搬就爆
队列里 1 件（元素还卡着）
再 pop 又抛：第 2 次移动起，一搬就爆
队列里 1 件
```

The one remaining item can never be taken out; every pop trips over the same spot. The standard makes no promise here, so we make our own: require T's move constructor and move assignment both to be non-throwing, write it into a `static_assert`, and stop unqualified types at compile time:

<OnlineCompilerDemo
  title="Two static_assert Gates: Throwing Moves Are Stopped at Compile Time"
  source-path="code/examples/vol5/chapter03/19_queue_static_assert.cpp"
  description="The two in-text static_asserts folded into the full queue class: move construction and move assignment must both promise not to throw. int passes the gates and runs; BrokenMove's failed compilation is preserved in the output block in the text."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// queue_static_assert.cpp —— 两道编译期的闸：移动构造、移动赋值会抛的类型，实例化时就拦下
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>

enum class QueueStatus {
  kOk,
  kClosed,
};

template <typename T>
class BoundedQueue {
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "T must be nothrow move constructible");
  static_assert(std::is_nothrow_move_assignable_v<T>,
                "T must be nothrow move assignable");

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
  BoundedQueue<int> q(4);   // int 移动构造、移动赋值都不抛，两道闸都过得去
  q.push(7);
  int v = 0;
  q.pop(v);
  std::printf("int 过闸：push 7，pop -> %d\n", v);
  return 0;
}
```

</OnlineCompilerDemo>

The second gate fences a different crack: when you reuse `out` to take items in a loop, a throwing assignment leaves the element stuck at the head just the same. Take a type that can move but promises nothing about not throwing, name it BrokenMove, and feed the compiler a minimal translation unit, f3_static_fail.cpp, containing nothing but the type definition and one line of instantiation:

```text
queue.hpp: In instantiation of ‘class BoundedQueue<BrokenMove>’:
f3_static_fail.cpp:11:29:   required from here
queue.hpp:16:22: error: static assertion failed: T must be nothrow move constructible
   16 |   static_assert(std::is_nothrow_move_constructible_v<T>,
      |                 ~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

What stopped it was the first gate — it cannot even promise a non-throwing move constructor, so the second gate never got its turn. The interception happens at compile time. Everyday types like `int`, `std::string`, and `std::unique_ptr` all pass. If some type of yours truly cannot pass the gate, don't blame the queue for being harsh — its own move operations genuinely still owe the no-throw promise.

Destruction. When the queue object is destroyed, the remaining elements are destroyed one by one. Halfway through stack unwinding, a destructor has no exit that throws: throw a new exception, and `std::terminate` takes over directly ([except.terminate]/1.4). So the discipline you impose on T is a single line: destructors don't throw.

Lock release is the net under the whole scene. In every method the lock is held by an RAII object: released on the normal path, released on the exception path too. The handover inside `wait` — carrying the lock out and bringing it back — is an atomic action guaranteed by the standard, as covered in 3.1. On the exception path, the only things that genuinely need your attention are the two moves.

## Your Turn to Try

Before writing a queue, or taking over someone else's, I walk a few questions through:

1. What is each direction's predicate? Do the condition-changer and the condition-waiter hold the same lock?
2. How many backpressure modes will your queue offer? For every dropped item, is the caller informed — and what can they do once informed?
3. After `close()`, who waits for the consumers to finish exiting? Before the queue object leaves its scope, is anyone still asleep on its condition variables?
4. Do you dare make T's move construction, move assignment, and destruction all non-throwing? If one of the three dares not, at which step will the queue leak?
5. How long does a consumer take per item? Estimating capacity as per-item processing time times the wait you can accept beats a wild guess.

## Reference Resources

The experiments originally ran on the header `queue.hpp`, whose final shape has been inlined into the example files. This time cppreference served the full text of both `std::queue` and `std::condition_variable`, matching the draft with no discrepancies. The progression follows Anthony Williams's *C++ Concurrency in Action*; the examples and prose are my own.

- [std::condition_variable (cppreference)][cppref-cv], since C++11.
- [std::queue (cppreference)][cppref-queue], a container adaptor whose default underlying container is `deque`.
- [Class condition_variable (current draft, thread.condition.condvar)][draft-condvar], the effects of `notify_one` and `notify_all`, the causes of unblocking, the predicate form's equivalent expansion, the constructor's permission to throw `system_error`, and the destructor's precondition.
- [Class template queue (current draft, queue.defn)][draft-queue], `push` delegates to the underlying container's `push_back`.
- [Modifiers (current draft, deque.modifiers)][draft-deque-modifiers], the exception promise for tail insertion, and the exception that the effect is unspecified when a move-only T's move constructor throws.
- [Container requirements (current draft, container.reqmts)][draft-creq], the general requirements that `push_back`-style operations have no effect on throw and `pop`-style operations never throw.
- [The std::terminate function (current draft, except.terminate)][draft-terminate], a destructor throwing during unwinding means termination.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[cppref-queue]: https://en.cppreference.com/w/cpp/container/queue
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-queue]: https://eel.is/c++draft/queue.defn
[draft-deque-modifiers]: https://eel.is/c++draft/deque.modifiers
[draft-creq]: https://eel.is/c++draft/container.reqmts
[draft-terminate]: https://eel.is/c++draft/except.terminate
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
