---
title: "Why Suspend and Resume Are Needed"
description: "Measured costs of a thousand concurrent connections pressing their waits onto threads, how callbacks, future chains, and coroutines each answer the same problem, and reading-level evidence that coroutines detach the program's shape from the thread."
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter07-coroutines/01-why-suspend-resume.md
  source_hash: e8557a6365fd4dbfbca4f62be7470c0e30f7be8f0c820cc8021a5fe95500872b
  translated_at: '2026-11-22T13:54:30+00:00'
  engine: glm-5.3
  token_count: 13128
---

# Why Suspend and Resume Are Needed

This article answers exactly one question: why work that can pause is needed.

Run through in your head the tools the first six units used — `std::thread`, mutexes, condition variables, futures, atomics: all of them grow on threads. The thread is their common unit of execution, and waiting presses down on the thread too: a thread asleep in `wait` is occupied by the wait at hand, and no other work can squeeze in. Coroutines take a different angle: the work can stop itself; while stopped, it hands the thread back, and when the event arrives it continues from where it stopped.

## The Scenario: One Thousand Concurrent Connections

The scenario is an old friend; it showed its face in Unit 1's discussion of when concurrency is worth it ([When Is Concurrency Worth Introducing](../chapter01-start-concurrency/08-when-concurrency)). We set up a server serving a thousand client connections at once, each connection living a similar day: request arrives, read a stretch, compute a stretch, reply with a stretch, say goodbye. Reading and replying are both I/O; one network read waiting a few to a few hundred milliseconds is routine, and during the wait there is no computation in hand at all. A thousand connections all standing in their waits at once — a perfectly ordinary afternoon.

The most intuitive design's parts were ready back in Unit 1: a connection arrives, open a thread, wrap it in `std::thread`, and let it serve from start to finish. Straightforward to write; each connection's execution context is a thread's stack, and the logic runs straight down. The cost, we measure:

<OnlineCompilerDemo
  title="One connection, one thread: the address space and physical memory of a thousand threads"
  source-path="code/examples/vol5/chapter07/01_exp_a_threads.cpp"
  description="A thousand threads each sleep 3 seconds to mimic waiting on I/O; /proc/self/status reports 1001 threads, VmSize climbing to the ~8 GB range, with physical memory rising only a small stretch."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_a_threads.cpp —— 一千条"一连接一线程"的线程：量地址空间、物理内存与线程数
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

// 从 /proc/self/status 抄一行出来，VmSize 与 VmRSS 单位是 kB
static bool status_line(const char* prefix, char* out, size_t n) {
  FILE* f = std::fopen("/proc/self/status", "r");
  if (!f) return false;
  bool found = false;
  char line[256];
  while (std::fgets(line, sizeof line, f)) {
    if (std::strncmp(line, prefix, std::strlen(prefix)) == 0) {
      std::snprintf(out, n, "%s", line);
      found = true;
      break;
    }
  }
  std::fclose(f);
  return found;
}

static void report(const char* tag) {
  char line[256];
  if (status_line("Threads:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmSize:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmRSS:", line, sizeof line))
    std::printf("%s %s", tag, line);
}

int main() {
  constexpr int kN = 1000;
  std::printf("== before ==\n");
  report("");

  std::atomic<int> started{0};
  std::vector<std::thread> pool;
  pool.reserve(kN);
  for (int i = 0; i < kN; ++i) {
    pool.emplace_back([&started] {
      ++started;
      // 睡着等事件，模拟一条正在等 I/O 的连接线程
      std::this_thread::sleep_for(std::chrono::seconds(3));
    });
  }
  while (started.load() < kN)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

  std::printf("== %d threads parked ==\n", kN);
  report("");

  for (auto& t : pool) t.join();
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_a_threads.cpp -o exp_a_threads
./exp_a_threads
```

```text
== before ==
 Threads:	1
 VmSize:	    7300 kB
 VmRSS:	    3944 kB
== 1000 threads parked ==
 Threads:	1001
 VmSize:	 8203564 kB
 VmRSS:	   12488 kB
```

Look at the readings and match up the two field lines: VmSize measures address space, VmRSS measures physical memory, both in kB. A thousand threads lift the address space from 7,300 kB to 8,203,564 kB, a rise of about 8,196,264 kB — around 8,196 kB per thread. The source is each thread's own stack: on Linux, glibc sizes a thread's default stack after RLIMIT_STACK; on my machine `ulimit -s` reports 8192 kB, which matches exactly, and the small extra slice is the thread's own management structures.

The physical-memory side we tell in two halves. VmRSS rises only from 3,944 kB to 12,488 kB — under 9 kB per sleeping thread. The 8 MiB is a reservation, not consumption: the thread sleeps waiting on I/O, never touches the depths of its stack, and physical pages are never handed down. The address-space side carries real cost too: the kernel keeps management records for every mapping, and the larger the address space laid out, the more virtual memory areas there are to record. Page tables and physical pages follow actual touches — only when a thread truly runs and its stack is truly touched do they catch up. So "a thousand threads eat 8 GB of memory" needs correcting into: a thousand threads reserve roughly 8 GB of address space, the mappings' management records grow accordingly, and physical memory and page tables depend on how deep they actually walk. Think about it — the reservation alone puts the address space at the 8 GB order of magnitude!

The scheduling side you met back in Unit 3: a sleeping wait burns no CPU; the kernel parks the thread where it is ([Waiting for a Condition to Hold](../chapter03-wait-and-coordinate/01-wait-for-a-condition)). The expensive part is elsewhere. With a thousand stack reservations sitting there, who wakes whom when events arrive, who goes on and who comes off — switching and cache overhead scale with thread count, and once waits multiply and all wake together, the number of switches climbs.

Waiting itself is not expensive; what is expensive is the thread carrying the wait. Only by taking the wait off the thread is there a way out. Where the taken-off work lives is each of the three models' own answer.

## Three Models, Three Answers to One Problem

The problem is one sentence: the flow reaches its halfway point and must wait on I/O — can the wait happen without occupying a thread? We compress each model's trade-offs into one paragraph; this article does not lay out the general history of the evolution. Walk the three roads one by one, then lay them side by side in a table.

### Callbacks: Handing Out "What Comes Next"

The idea is plain: when initiating an asynchronous operation you pass in a function, and the system calls it for you when the operation completes. Quick to write; the costs follow right behind. Sequence scatters across nested callbacks — read, compute, reply split into three levels of nested functions, the depth growing with the flow, and after five or ten steps the indentation on the right runs off the screen. Error handling scatters across the levels, each level checking its own stretch, with nowhere for a centralized `try`/`catch` to stand. The lifetime of objects captured by closures is treacherous too: by the time the callback actually executes, a captured variable may already be dead — dangling references and use-after-free are regulars in this model.

The callback itself is nearly zero-cost — a function pointer plus a capture is the whole of it — and it stays alive inside high-performance libraries. The cost is not in the machine but in the people reading and maintaining the code, ourselves included.

### Future Chains: Unit 4's Tools Run Short Here

We used `std::async` with futures in Unit 4: a stretch of work is thrown out, and the future collects the result ([Running Computations with `std::async`](../chapter04-organizing-tasks/01-launch-with-async)). Put it on this server of ours, and the way to fetch the result is `get()`, which puts the calling thread to sleep until the result is ready. The wait presses back onto the thread — only the location moved from `read` to `get()`; the thread is occupied just the same.

What we really want is something else: automatically continuing to the next step once the result is ready. The standard `std::future` cannot deliver; its member-function list is `get`, `wait`, `wait_for`, `wait_until` and kin, with no `then` among them (cppreference, accessed 2026-11-17). The `then` continuation lives on `std::experimental::future` from the Concurrency TS (a draft track toward standardization), and that TS was never merged into the International Standard. There is also a cost that grows with the chain: the shared state behind a future is allocated by the implementation, most implementations with one heap allocation, and each link in the chain adds one more. Exceptions, to be fair, do travel through the shared state to `get()` and rethrow there — a mechanism you used in Unit 4.

### Coroutines: Sequential Writing Comes Back

The writing returns to sequential: put a `co_await` in the middle of the function body; if the awaited thing is not ready, the function stops right there, hands the control back, and once ready continues from the stopping point. Read, compute, reply sit line after line, looking exactly like synchronous code, with suspension and resumption buried in by the compiler together with you.

C++20 brought coroutines into the language; the three keywords, one by one: `co_await` suspends to wait for something, `co_yield` produces a value and suspends, `co_return` wraps up and returns. Any one of them appearing in a function body makes that function a coroutine (current draft [dcl.fct.def.coroutine] paragraph 1; cppreference says the same). The `<coroutine>` header supplies the support interface, where `suspend_always` and `suspend_never` live (draft, [support.coroutine]). The mechanism has complexities of its own, and we will not hide them: `promise_type`, handles, awaiters — a whole set of parts, with the compiler doing a large transformation behind your back, and error messages that can be obscure.

Explaining the parts we leave to [How a Minimal Coroutine Comes Together](02-minimal-coroutine) and to How `co_await` Joins the Wait; this article only sets the trade-offs.

Lay the three models in a row:

| Model | Where the sequence lives | While waiting | Main cost |
|------|------------|-----------|----------|
| Callbacks | Scattered across nested callbacks | No thread held | Deep nesting, scattered error handling, risky closure lifetimes |
| Future chains | Standard futures cannot chain; `get()` sleeps the whole way | Holds the calling thread | The wait presses back onto the thread; one more shared state per link |
| Coroutines | Sequential writing, `co_await` suspends | No thread held | Complex machinery, learning cost up front |

My position too: in the server scenario I choose coroutines, because only they do two things at once — the sequence stays readable, and the wait holds no thread. Callbacks and future chains are not dead: callbacks live in the performance-sensitive lowest layers, and futures suit one-off computational jobs thrown out the door; in Unit 4 we used them to compute small independent values like squares and divisions.

## Coroutines Change How the Program Is Organized, Not the Thread Count

What does "pausable work" look like once it lands in a program? Let's run a stretch and see. The program plays two acts. In the first, one coroutine does a job that "must wait", suspends halfway through, and another thread resumes it. In the second, a thousand coroutines are all suspended with only the main thread throughout, and we measure memory. The `Task` type's parts are not this article's protagonist — [How a Minimal Coroutine Comes Together](02-minimal-coroutine) covers them one by one; you only need to recognize `co_await`'s suspension and `resume()`'s resumption:

<OnlineCompilerDemo
  title="Coroutine suspension and resumption: a thousand suspended coroutines hold just one thread"
  source-path="code/examples/vol5/chapter07/02_exp_b_suspend.cpp"
  description="In act one the same coroutine body runs in two stretches, the thread id changing with the resumer; in act two a thousand coroutines are all suspended, the thread count stays 1, and physical memory rises only a few dozen kB."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_b_suspend.cpp —— 协程的暂停与恢复：谁恢复它，它就在谁的线程上跑
// 二幕：一千个挂起的协程，一条线程，量地址空间与物理内存
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::string tid() {
  std::ostringstream oss;
  oss << std::this_thread::get_id();
  return oss.str();
}

// 从 /proc/self/status 抄一行出来，VmSize 与 VmRSS 单位是 kB
static bool status_line(const char* prefix, char* out, size_t n) {
  FILE* f = std::fopen("/proc/self/status", "r");
  if (!f) return false;
  bool found = false;
  char line[256];
  while (std::fgets(line, sizeof line, f)) {
    if (std::strncmp(line, prefix, std::strlen(prefix)) == 0) {
      std::snprintf(out, n, "%s", line);
      found = true;
      break;
    }
  }
  std::fclose(f);
  return found;
}

static void report(const char* tag) {
  char line[256];
  if (status_line("Threads:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmSize:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmRSS:", line, sizeof line))
    std::printf("%s %s", tag, line);
}

// awaiter：挂起，把句柄登记进列表，等别人来恢复
struct Park {
  static std::vector<std::coroutine_handle<>> parked;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) { parked.push_back(h); }
  void await_resume() {}
};
std::vector<std::coroutine_handle<>> Park::parked;

// 最小任务类型。零件各自干什么归 7.2，本篇只要它撑起 co_await
struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& o) noexcept : h(o.h) { o.h = {}; }
  Task& operator=(Task&& o) noexcept {
    if (this != &o) {
      if (h) h.destroy();
      h = o.h;
      o.h = {};
    }
    return *this;
  }
  ~Task() { if (h) h.destroy(); }
};

// 处理一件"要等"的活：stage 1，挂起等事件，恢复后 stage 2
// done 非空时是安静模式，只计数不打印
Task worker(int id, std::atomic<int>* done) {
  if (done == nullptr)
    std::printf("  worker %d stage1 on thread %s, suspend\n", id, tid().c_str());
  co_await Park{};
  if (done != nullptr)
    ++*done;
  else
    std::printf("  worker %d stage2 on thread %s, finished\n", id, tid().c_str());
}

int main() {
  { // 一幕：同一个协程体分两段跑，谁恢复它，它就在谁的线程上继续
    Task t = worker(0, nullptr);
    std::printf("main: worker suspended, parked=%zu\n", Park::parked.size());
    std::thread driver([] { Park::parked.back().resume(); });
    driver.join();
    std::printf("main: worker finished\n");
  }
  Park::parked.clear();

  { // 二幕：一千个挂起的协程，全程只有主线程
    constexpr int kN = 1000;
    std::atomic<int> done{0};
    std::printf("== before %d workers ==\n", kN);
    report("");
    std::vector<Task> tasks;
    tasks.reserve(kN);
    for (int i = 0; i < kN; ++i)
      tasks.emplace_back(worker(i, &done));
    while (static_cast<int>(Park::parked.size()) < kN)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::printf("== %d coroutines suspended ==\n", kN);
    report("");
    for (auto& h : Park::parked) h.resume();
    std::printf("== resumed, done=%d ==\n", done.load());
  }
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_b_suspend.cpp -o exp_b_suspend
./exp_b_suspend
```

```text
  worker 0 stage1 on thread 130143623620480, suspend
main: worker suspended, parked=1
  worker 0 stage2 on thread 130143615973056, finished
main: worker finished
== before 1000 workers ==
 Threads:	1
 VmSize:	   81040 kB
 VmRSS:	    4236 kB
== 1000 coroutines suspended ==
 Threads:	1
 VmSize:	   81040 kB
 VmRSS:	    4316 kB
== resumed, done=1000 ==
```

Act one's readings, line by line. The thread id printed at stage1 belongs to the main thread; after suspension the main thread went off to its own business, the driver thread resumed it, and the thread id printed at stage2 changed accordingly. One function body, two stretches of execution, with no CPU idling on its behalf in between.

The evaluation that initiates the resumption — the draft names it the resumer ([dcl.fct.def.coroutine] paragraph 8). A coroutine body owns no thread; whoever resumes it, on that thread it continues. cppreference's coroutine page once gave an example of the same shape, handing the handle to a new thread for resumption — the same idea as our act one. The first time we looked at two thread ids side by side, we paused for a beat: one function body, whose first half and second half do not live on the same thread!

Cross-thread resumption is one the standard has named: when both sides are `std::thread`, `std::jthread`, or the main thread, the behavior has a guaranteed footing. Swap in another execution agent and the behavior becomes implementation-defined (the note under [coroutine.handle.resumption]). The note also carries a warning: two threads concurrently resuming one coroutine can constitute a data race — before handing the handle out, you must confirm the coroutine has truly come to a stop.

Where does the context live during suspension? Asked plainly, the answer is the **coroutine state** (also often called the coroutine frame). cppreference once gave a list: copies of the parameters live in it, local variables and temporaries whose lifetimes cross the suspension point live in it, and where the execution stopped is recorded too — sources are [dcl.fct.def.coroutine] paragraphs 9 and 13. It mostly settles into a home through one dynamic allocation. There is an exemption as well: if the implementation can prove the coroutine's lifetime does not escape the caller, the allocation can be elided (cppreference, the Dynamic allocation section). Ordinary functions cannot do this: once they return, the stack frame is gone, and the only way to pause is to stop the entire thread. Unit 3's waits pressing on threads have their root right here.

Act two is the comparison's other side. A thousand coroutines all suspended, thread count 1 from start to finish, physical memory up 80 kB — about 80 B per coroutine frame (a reading from this GCC implementation; frame size varies with locals and implementation) — and VmSize did not move. Put it next to exp_a: a thousand threads, roughly 8 GB of address-space reservation, 1001 scheduling objects; a thousand suspended coroutines, zero new threads, 80 kB of physical pages. Look — one frame at 80 B, while one thread's stack reservation is 8 MiB: five orders of magnitude apart! Half a sentence of the unit's title lands in the readings: waiting is no longer priced by the thread.

The cheapness has a boundary, and we accept it. What coroutines save is the thread occupied by waiting; computation still has to be done by real threads, and when events arrive, a few threads must be there to resume the suspended coroutines one by one. Who resumes, and when — the standard does not govern; the language mechanism goes as far as the coroutine state and the handle, and scheduling belongs to your own program's organization. An event loop or a thread pool can both serve as the resuming executor. The event loop and the Echo-Server now have their rightful owners: this unit's articles 07 and 08, the two selected readings.

> The rightful owners are in this very unit: [Selected Reading: Wiring IO into the Scheduler — Epoll and IOManager](07-echo-io-manager) and [Selected Reading: Three Blocks Assembling an Echo-Server](08-echo-server), where the scheduler takes on epoll to serve an echo server.

A coroutine suspended and never resumed, never destroyed, is a close relative of a thread asleep forever in `wait` with no one to wake it; we close the comparison here. The problem of waits needing an exit path — coroutines must answer it too; the homework was done once in [Waits Need an Exit Path Too](../chapter03-wait-and-coordinate/06-ending-a-wait). This article has established the need and handed out no parts yet.

## Your Turn

1. Run both experiments once each on your machine; record the VmSize and VmRSS rises separately, and mind the value of `ulimit -s` — the default stack size varies by implementation.
2. Raise exp_b's `kN` to one hundred thousand and run again; see how many bytes the physical-memory rise folds down to per coroutine, and compare against the thousand-coroutine reading.
3. Replace the driver that resumes the coroutine in act one with the main thread itself — are the stage1 and stage2 thread ids still different? Why?
4. Re-read Unit 3's waits with coroutine eyes: the time a thread spent asleep in `wait` — to whom does the suspended coroutine return the corresponding time, and what can the returned thread do?

## References

The scenario and the three models' trade-offs were distilled by this article itself. Claims about the coroutine language mechanism were checked by the author against the following sources (accessed 2026-11-17). Thread stack defaults and the address-space and physical-memory readings are anchored to local measurement; the source of the fields is the proc_pid_status(5) manual — proc(5) was later split into volumes, and the status fields moved into that volume.

- [Coroutines (C++20), cppreference][cppref-coroutines] — the sources for the three-keyword test, the composition of the coroutine state, dynamic allocation and its elision, and the cross-thread resumption example.
- [std::future, cppreference][cppref-future] — the member-function list, and the source showing `then` is not in the standard `std::future`.
- [std::experimental::future::then, cppreference][cppref-then] — the source of the Concurrency TS continuation; that TS was not merged into the International Standard.
- [Current draft: Coroutine definitions][draft-coroutine] — [dcl.fct.def.coroutine] (9.6.4): the coroutine test in paragraph 1, resumer in paragraph 8, coroutine state in paragraph 9, parameter copies in paragraph 13. In the current draft the language-side coroutine clauses live under this name, the library-side support interface lives in [support.coroutine] (17.13), and the old name [cpp.coroutine] is no longer a clause name in the current draft.
- [Current draft: coroutine support interface][draft-support] — [support.coroutine] (17.13), where `suspend_always` and `suspend_never` live.
- [Current draft: handle resumption][draft-handle-resumption] — [coroutine.handle.resumption] (17.13.4.6), the source for the execution-agent conditions on cross-thread resumption and the concurrent-resumption warning.
- [proc_pid_status(5) manual][man-proc] — the source of the VmSize, VmRSS, and Threads fields in `/proc/self/status` (their new home after proc(5) split into volumes).

[cppref-coroutines]: https://en.cppreference.com/w/cpp/language/coroutines
[cppref-future]: https://en.cppreference.com/w/cpp/thread/future
[cppref-then]: https://en.cppreference.com/w/cpp/experimental/future/then
[draft-coroutine]: https://eel.is/c++draft/dcl.fct.def.coroutine
[draft-support]: https://eel.is/c++draft/support.coroutine
[draft-handle-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[man-proc]: https://man7.org/linux/man-pages/man5/proc_pid_status.5.html
