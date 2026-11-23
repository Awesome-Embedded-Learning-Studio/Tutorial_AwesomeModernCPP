---
title: "Two Resumption Routes: Wiring Task into a Scheduler"
description: "The teaching edition's Loop and Yield set beside the blog scheduler's unified resumption, to see what symmetric handover and asymmetric resumption each suit — ending back at that question: which thread actually resumes the coroutine."
chapter: 5
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter07-coroutines/06-resumption-routes.md
  source_hash: 76d92989992ec974af51983af3488f228218f594ea4e6199715ccc2a60fd2861
  translated_at: '2026-11-22T14:18:30+00:00'
  engine: glm-5.3
  token_count: 10506
---

# Two Resumption Routes: Wiring Task into a Scheduler

The old article's scheduler embryo was a ready queue plus a run loop, living in the old directory's promise_type and awaitable. We rewrite it and wire it to Task. The division of labor, laid out in three sentences: Task governs a job's lifetime, from creation to destruction. Loop governs queueing, deciding who runs next. Yield governs yielding — the coroutine itself steps aside and rejoins at the tail of the queue. Separating a job's lifetime from its position of execution is the precondition for a scheduler to stand, and it is also the way of organizing that Unit 7 wants to hand you.

We let two pipeline lines queue up and run: pipeline strings together the two stations fetch and process, each station yielding one round after finishing a stretch of work. Task carries over the teaching-edition Task from the previous article, Wrapping Results; Loop, Yield, and the three coroutines are new:

<OnlineCompilerDemo
  title="A minimal scheduler: ready queue plus a single-threaded loop"
  source-path="code/examples/vol5/chapter07/13_exp_i_loop.cpp"
  description="Two pipelines queue up and run; a Yielding coroutine rejoins at the tail, completion handovers on the chain take the shortcut, and one thread serves all six coroutines."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_i_loop.cpp —— 最小调度器：就绪队列加单线程循环，两段流水线排队跑
// Task 与 exp_h_task.cpp 相同，已拼在本文件顶部
#include <coroutine>
#include <cstdio>
#include <deque>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

int g_frames = 0;   // 协程帧的在册计数，教学演示销毁时机用，非线程安全

template <typename T>
class Task {
public:
  struct promise_type {
    T result{};
    std::exception_ptr failure{};
    std::coroutine_handle<> continuation{};   // 等我做完的上一级协程，根 Task 手里没有

    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }   // 懒启动：创建时不跑
    void return_value(T value) { result = std::move(value); }
    void unhandled_exception() { failure = std::current_exception(); }

    // 最终挂起点的交接，sym 姿势：有人等就交回去，没人是根，交还最初的恢复者
    auto final_suspend() noexcept {
      struct Done {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<promise_type> me) noexcept {
          auto c = me.promise().continuation;
          if (!c) return std::noop_coroutine();
          return c;
        }
        void await_resume() noexcept {}
      };
      return Done{};
    }

    // 帧计数，7.2 frame_alloc 的同款手法
    void* operator new(std::size_t size) {
      ++g_frames;
      return ::operator new(size);
    }
    void operator delete(void* ptr, std::size_t size) {
      --g_frames;
      ::operator delete(ptr, size);
    }
  };

  explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
  ~Task() { if (h_) h_.destroy(); }   // RAII：帧的收回交给析构函数
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& o) noexcept : h_(o.h_) { o.h_ = {}; }
  Task& operator=(Task&& o) noexcept {
    if (this != &o) {
      if (h_) h_.destroy();
      h_ = o.h_;
      o.h_ = {};
    }
    return *this;
  }

  // 三件套：Task 自己就是 awaiter，co_await 一个 Task 就是在等它做完
  bool await_ready() noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiter) noexcept {
    h_.promise().continuation = waiter;   // 登记在前
    return h_;                            // 启动在后，sym 姿势把子协程跑起来
  }
  T await_resume() { return take_result(); }

  void start() { h_.resume(); }   // 根 Task 的启动口：谁调它，协程体就在谁的线程上跑
  T result() { return take_result(); }
  std::coroutine_handle<promise_type> handle() const { return h_; }   // 交给调度器用

private:
  T take_result() {
    if (h_.promise().failure)
      std::rethrow_exception(h_.promise().failure);
    return std::move(h_.promise().result);
  }

  std::coroutine_handle<promise_type> h_;
};

class Loop {
public:
  void schedule(std::coroutine_handle<> h) { ready_.push_back(h); }
  void run() {
    while (!ready_.empty()) {
      auto h = ready_.front();
      ready_.pop_front();
      h.resume();   // 谁调 run，队列里的协程就在谁的线程上跑
    }
  }

private:
  std::deque<std::coroutine_handle<>> ready_;
};

// 让出：挂起，把自己放回队尾，轮到排在后面的协程
struct Yield {
  explicit Yield(Loop& loop) : loop_(loop) {}
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) { loop_.schedule(h); }
  void await_resume() noexcept {}

private:
  Loop& loop_;
};

Task<std::string> fetch(Loop& loop, int id) {
  std::printf("  fetch(%d): 开始取数\n", id);
  co_await Yield{loop};
  std::printf("  fetch(%d): 取到了\n", id);
  co_return "数据" + std::to_string(id);
}

Task<std::string> process(Loop& loop, std::string data, int id) {
  std::printf("  process(%d): 拿到 %s，开始加工\n", id, data.c_str());
  co_await Yield{loop};
  std::printf("  process(%d): 加工完成\n", id);
  co_return data + "(已加工)";
}

Task<std::string> pipeline(Loop& loop, int id) {
  std::string data = co_await fetch(loop, id);
  std::string out = co_await process(loop, data, id);
  co_return out;
}

int main() {
  Loop loop;
  Task<std::string> a = pipeline(loop, 1);
  Task<std::string> b = pipeline(loop, 2);
  loop.schedule(a.handle());
  loop.schedule(b.handle());
  std::printf("== 循环开跑 ==\n");
  loop.run();
  std::printf("== 队列空了 ==\n");
  std::printf("a = %s\n", a.result().c_str());
  std::printf("b = %s\n", b.result().c_str());
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_i_loop.cpp -o exp_i_loop
./exp_i_loop
```

```text
== 循环开跑 ==
  fetch(1): 开始取数
  fetch(2): 开始取数
  fetch(1): 取到了
  process(1): 拿到 数据1，开始加工
  fetch(2): 取到了
  process(2): 拿到 数据2，开始加工
  process(1): 加工完成
  process(2): 加工完成
== 队列空了 ==
a = 数据1(已加工)
b = 数据2(已加工)
```

Let's match the output's order line by line. The loop starts by popping a; pipeline(1) runs into fetch(1), prints "start fetching", is promptly suspended by Yield and sent to the queue's tail, and control returns to run. Next pops b, and fetch(2) likewise yields a round. Look at the third round: what pops is fetch(1); after it reports "fetched", the completion handover does not go through the queue — Done wakes pipeline(1) directly, pipeline(1) lands and immediately calls process(1), and process(1) prints that it has the data and yields another round back to the queue — the whole stretch happens inside run's single resume. Look again at the order of lines five and six: process(1) already has its data before fetch(2) reports "fetched" — precisely the trace of sym handover; handovers on the chain take the shortcut, and the only coroutines queued are those that yielded.

For the remaining lines we follow the queue downward: process(1) and process(2) finish one after the other, each completion handover likewise taking the shortcut to wake its pipeline; pipeline is the root, Done hands out noop, control returns to run, and the loop closes when the queue empties. a's and b's results are fetched outside the loop; throughout there is only one resumer, run, and one thread served all six coroutines of the two pipeline lines.

Set it beside the kernel scheduling in Unit 1's selected reading (see [What a Thread Is on the Machine](./../chapter01-start-concurrency/09-what-is-a-thread)): the kernel swaps you out when your time is up, regardless of the program's wishes. Loop is purely cooperative — a coroutine that never yields monopolizes until death — so the scheduler's writer and the coroutine's writer must jointly keep the discipline of yielding. Compared with the old article's scheduler, Task has taken over the manual-destroy work: the scheduler only queues, and nobody worries about destruction. To truly make waiting asynchronous, where the registration goes was disclosed in the Gate section of How `co_await` Joins the Wait; the event loop and the Echo-Server have their rightful owners — this unit's articles 07 and 08, the two selected readings.

## Two Resumption Routes

Put the previous section's readings next to the blog scheduler's, and the two resumption routes take shape. On the blog scheduler's side, the three coroutines A, B, C print alternately with exactly one resumer from start to finish: the run loop. When a coroutine suspends, it registers its handle into the ready queue or the sleep heap and hands control back to run; who is resumed next, and when, is entirely the scheduler's chip to hold. On the teaching edition's side, Yield's letting-go also goes through the queue, but the completion handover on the chain does not — Done wakes the continuation directly, pipeline lands and immediately calls process, the whole stretch happening inside run's single resume.

One route is asymmetric resumption: the scheduler holds the chips uniformly; a coroutine suspending is handing control back, and the timing and order of resumption are entirely in the scheduler's hands. The benefit is easy debugging — put a breakpoint in run and every resumption is visible; when the scheduling policy must change, changing the scheduler in one place suffices. The cost is that every handover takes a trip back to the queue; when the chain is deep, one link's completion must detour around the scheduler before the next link gets its turn.

The other route is symmetric handover: a suspending coroutine does not return to the scheduler but passes control directly to the registered waiter, and the compiler optimizes this "pass" into one jump on the stack. The benefit is that deep chains eat no stack and detour through no queue — the completion handovers on the previous article's Task chain took exactly this route. The cost is that resumption paths scatter into the individual suspension points; a breakpoint placed only in run never sees the whole picture.

The two routes have no high or low; each has its scene. Want centralized scheduling policy and observability — use asymmetric unified resumption; want the chain short and quick — use symmetric handover. The teaching-edition Task showed you both: the moment await_suspend returns a handle, it is taking the symmetric handover; Loop and the scheduler are the minimal template of asymmetric resumption.

## Coroutines and Threads: Who Resumes on Which Thread

Unit 1's old question is settled in this article. The second article quoted the standard's naming: the evaluation that calls a resumption member function is the resumer ([dcl.fct.def.coroutine]/8); a coroutine body carries no thread of its own — whoever calls resume, it continues on that one's call stack. The first article's act measured the same function body switching threads; here we set the position down on Task: Task itself carries no thread — whoever calls start, whoever runs the Loop, the whole chain lives on that one's stack. The handovers on the chain are all written in the sym posture; a handover does not switch threads — it merely passes control to the next level, with the same thread still underfoot.

A three-act experiment measures the position for you. Act one: main calls start itself. Act two: start is handed to a driver thread. Act three: the tasks are created in main while the loop is moved onto another driver to run:

<OnlineCompilerDemo
  title="Three acts, three resumption entrances: main starts itself, a driver thread starts, the loop moves onto a driver"
  source-path="code/examples/vol5/chapter07/14_exp_j_threads.cpp"
  description="Three acts swap the resumption entrance: main starts itself, a driver thread starts, the loop moves onto a driver — and the thread ids of root's two halves and of leaf follow the resumption entrance."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// 源码清单见 code/examples/vol5/chapter07/14_exp_j_threads.cpp，与本篇演示一致
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_j_threads.cpp -o exp_j_threads
./exp_j_threads
```

```text
main 在线程 130680546568064

  root 前半      在线程 130680546568064

  leaf(10)       在线程 130680546568064

  root 后半 v=11 在线程 130680546568064

幕一 result=11

  root 前半      在线程 130680539313856

  leaf(20)       在线程 130680539313856

  root 后半 v=21 在线程 130680539313856

幕二 result=21

  root 前半      在线程 130680539313856

  leaf(30)       在线程 130680539313856

  root 后半 v=31 在线程 130680539313856

幕三 result=31
```

The three acts' readings line up perfectly. Act one: root's first half, leaf, and root's second half all print main's thread id — the only resumer is main. Act two: start was written in exactly one place, yet all three prints along the chain switched to driver's id; the Task created in main finished running on another thread — its lifetime follows the resumption entrance. Act three swaps the entrance again: loop.run moved onto a driver, and root's three prints followed it over. One more detail to note: the driver ids in acts two and three have the same value — the system reused the number of a just-reclaimed thread; to judge ownership, do not look at the value itself, look at whether it differs from main.

One safety boundary for cross-thread use must be stated. In act two, main hands the Task to driver, with happens-before established by the thread's construction and join; only after the join does main's read of result carry a guarantee. Unit 5 covered this pair of synchronization relations (see [How One Thread Observes Another Thread's Modifications](./../chapter05-atomic-memory-model/03-happens-before)). The teaching edition itself adds no locks; two threads concurrently resuming one coroutine is a data race — the first article quoted the [coroutine.handle.resumption] warning. To hand Tasks between threads, you must supply the synchronization yourself; the position hangs on the teaching edition's gap list.

The order Unit 1 set for the world was work growing on threads: one thread, one stack; waiting occupies the thread, and a thousand waits demand a thousand threads reserving roughly 8 GB of address space (the first article's readings). Unit 7 split the two apart: work is a Task, and a Task is a possession — it has a frame, a result, an exception, a destructor. The thread is the executor of resumption: one resume serves one continuation, and when served it goes off to do something else. Unit 1's join and detach govern the thread's life (see [The First `std::thread` Program](./../chapter01-start-concurrency/03-first-std-thread)); this article's destructor governs the work's frame — the final accounting still lands on a destructor, but the object governed has changed. Unit 7 also comes with a small project, aimed at explaining the relations among coroutines, threads, and scheduling; the material is gathered in this article, with the exercise designed separately.

## Your Turn

- Swap exp_i's ready queue for one shared by two threads (with a lock), both sides' run popping coroutines concurrently; watch how the interleaving of prints changes, and think about what synchronization the continuation registration and the completion handover each need.
- Add a run_one to Loop that pops exactly one ready coroutine at a time, and think, together with the timer idea, about what the event loop still lacks. The complete answer has a ready implementation in this unit's selected reading 08, Three Blocks Assembling an Echo-Server.

## References

This article's scheduler telling was expanded from my October 2025 blog, Understanding C++20's Revolutionary Feature — Coroutine Support 2: Writing a Simple Coroutine Scheduler; the singleton template and sleep awaiter tellings were carried over verbatim from the blog. The other stream of material comes from the old directory ch06's promise_type and awaitable, where SimpleTask, SafeTask, and the scheduler already had their embryos; the examples and words were rewritten entirely. The three experiments were additionally rerun with -fsanitize=address,undefined; the sanitizers reported nothing. Language-mechanism claims were checked against the current working draft on eel.is and cppreference (accessed 2026-11-17); clause and paragraph numbers may shift as the draft updates.

- [Await expression (current draft expr.await)](https://eel.is/c++draft/expr.await) — the three return types of await_suspend, and the note on relayed resumption via returned handles.
- [Coroutine definitions (current draft dcl.fct.def.coroutine)](https://eel.is/c++draft/dcl.fct.def.coroutine) — the naming of resumer, final_suspend's noexcept requirement, and the destruction order of the coroutine state.
- [Resumption (current draft coroutine.handle.resumption)](https://eel.is/c++draft/coroutine.handle.resumption) — the preconditions of resume and destroy, the implementation-defined behavior of resuming across execution agents, and the concurrent-resumption warning.
- [No-op coroutines (current draft coroutine.handle.noop)](https://eel.is/c++draft/coroutine.handle.noop) — the noop handle's resume and destroy are both no-ops.
- [Coroutines (C++20) (cppreference)](https://en.cppreference.com/w/cpp/language/coroutines) — the completion sequence (uncaught exception, unhandled_exception, final_suspend), a coroutine at its final suspension point being destroy-only, and the composition of the coroutine state.
- [std::exception_ptr (cppreference)](https://en.cppreference.com/w/cpp/error/exception_ptr) — the exception pointer's shared ownership and cross-thread delivery.
- [C++ Coroutines: Understanding Symmetric Transfer — Lewis Baker](https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer) — the telling of the naive and sym handover postures; the reference for this article's Task handovers and for the Two Resumption Routes section.
