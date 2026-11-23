---
title: "A Wait Also Needs a Way to End"
description: "Each of the four ways a wait can end gets its own observable scene — close, timeout, stop, and failure — then the stop_token overloads of condition_variable_any and the hand-built stop_callback bridge, closing with a checklist for wait-ending paths."
chapter: 5
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter03-wait-and-coordinate/06-ending-a-wait.md
  source_hash: f2075d2053e8f15b86d987ed3ae8ecd93dbf35e101a22f87998c057d17f03f73
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 16259
---

# A Wait Also Needs a Way to End

*What Happens When a Queue Won't Close: Closing and Backpressure* closed the queue: once the closing move happens, producers stop pushing and consumers finish the work in hand. The queue winds down cleanly — but what about a wait that no close can shut and no goods can satisfy? Asleep in `wait`, it sleeps until called. This article answers exactly one question: every wait you write must have an ending path you can articulate.

## When It Never Comes, How Many Ways Out

The successful end needs no design. Goods arrive, the predicate holds, the wait ends on its own — nobody has to do anything extra. The end that needs designing is the other one: the goods never come. The countable endings number four, and in order we call them close, timeout, stop, and failure. Below, the same scene runs four times, one ending per run, and after each run you should be able to answer "why did the wait come apart".

The scene is a minimal goods bay we set up: one lock, one condition variable, one empty queue, and one thread waiting for the first item. Each of the four endings runs once, every run starting from the waiting thread asleep:

<OnlineCompilerDemo
  title="Four Endings: Close, Timeout, Stop, Failure — Each Leaves Its Scene"
  source-path="code/examples/vol5/chapter03/20_exits.cpp"
  description="One wait, four acts: closed raised, the 120 ms deadline hit, a stop request at 60 ms, failed throwing in the towel. Each act reports when it returned and what flipped the predicate true; the millisecond figures shift slightly per run."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exits.cpp —— 同一处等待，四种结束方式，各留一个可观察的现场
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

struct Bay {
  std::mutex m;
  std::condition_variable_any cv;   // 通用版：C 一幕要接停止请求
  std::queue<int> q;
  bool closed = false;
  bool failed = false;
  std::atomic<int> parked{0};
};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  { // A 关闭：没人再来放货，收盘的一方把 closed 立起来
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bay.cv.wait(lk, [&] { return !bay.q.empty() || bay.closed; });
      std::printf("A 关闭 : %5.1f ms 返回，队列空着，谓词翻正靠的是 closed\n",
                  ms_since(t0));
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    { std::lock_guard<std::mutex> g(bay.m); bay.closed = true; }
    bay.cv.notify_all();
    waiter.join();
  }

  { // B 超时：时限 120 ms，谁也没来
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bool got = bay.cv.wait_for(lk, 120ms,
                                 [&] { return !bay.q.empty() || bay.closed; });
      std::printf("B 超时 : %5.1f ms 返回，返回值 %d，时限内没等到货\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    waiter.join();
  }

  { // C 停止：停止请求在 60 ms 到，等待当场结束
    Bay bay;
    std::stop_source source;
    auto token = source.get_token();
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bool got = bay.cv.wait(lk, token, [&] { return !bay.q.empty(); });
      std::printf("C 停止 : %5.1f ms 返回，返回值 %d，谓词没成立\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main   : %5.1f ms 发出停止请求\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    source.request_stop();
    waiter.join();
  }

  { // D 失败：上游干砸了，把 failed 记下来再散场
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bay.cv.wait(lk, [&] { return !bay.q.empty() || bay.failed; });
      std::printf("D 失败 : %5.1f ms 返回，货没等到，谓词翻正靠的是 failed\n",
                  ms_since(t0));
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(70ms);
    { std::lock_guard<std::mutex> g(bay.m); bay.failed = true; }
    bay.cv.notify_all();
    waiter.join();
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
A 关闭 :  61.4 ms 返回，队列空着，谓词翻正靠的是 closed
B 超时 : 122.3 ms 返回，返回值 0，时限内没等到货
main   :  61.9 ms 发出停止请求
C 停止 :  64.2 ms 返回，返回值 0，谓词没成立
D 失败 :  72.2 ms 返回，货没等到，谓词翻正靠的是 failed
```

Take A, close. The wait returns at 61.4 ms; the queue was empty from start to finish, and the predicate came true by way of `closed`. The criterion has two halves. The closing move must do two things at once — raise `closed` inside the lock and fire the notify outside it; miss either and the wait cannot break up. After the breakup you must also be able to say why it broke up: the flag is up, the queue is empty, so it was "closed, goods never came" — no goods were lost, there never were any. How to wind down with goods still left in the queue belongs to the close semantics of *What Happens When a Queue Won't Close: Closing and Backpressure*; this article doesn't touch it.

B is timeout. It returns at 122.3 ms, 2-odd ms past the 120 deadline; the spare change comes from timer and scheduling granularity — the same spare change we saw in 3.2, since a timeout is not an alarm clock. The criterion doesn't look at the instant of return but at what follows: with `false` in hand, whether you retry or give up must be one explicit line in the code. If retry, from where does the deadline restart. If give up, what gets reported upward. Where the deadline itself comes from must also be answerable — passed by the caller, read from config, or a hard-coded number — so that half a year later, when someone asks why it is 120, somebody can catch the question. The final question in 3.2's checklist, "what do you plan to do after the fallback returns `false`", gets its answer here.

C is stop: the ending time follows neither the queue nor the deadline, but the request. The request line prints at 61.9 ms, the wait returns at 64.2 ms, a gap of 2-odd ms — of which two full milliseconds are the fixed yielding pause after printing; shave that off, and the wake-up itself takes less than half a millisecond. The return value is `false`, meaning what it always meant in 3.2: the predicate did not come true. It does not report who woke it; we met that once in *Ending Work in an Orderly Way*, verified there with `wait_for`, and we meet it again here. C can receive the request thanks to the `condition_variable_any` overload taking a `stop_token`, which the next section covers.

D is failure — the one most easily forgotten of the four. The upstream botched it, the work cannot be done; that must be recorded somewhere the predicate can read, or the side that gave up has already left while the waiting side is still waiting. Who claims the failure must also be named — I wrote it as the upstream side that throws in the towel, played in the scene by `main`; a dedicated cleanup side would do too, but somebody has to do it. When real exceptions are involved, catch them in the thread entry's `catch` and lower them onto the flag. The proper road for propagating failure upward is exception delivery through `std::promise` and `std::future` — the promising side packs the exception, the future side takes it — covered in Unit 4's 4.4, *Passing Results Yourself: `std::promise` and `std::future`*. Here we use the plainest flag bit: enough, and visible.

## A Stop Request Inside the Wait: The stop_token Overloads

The `stop_token` overloads exist only on `condition_variable_any` — `condition_variable` has none — since C++20, under [thread.condvarany.intwait]. There are three: `wait`, `wait_until`, and `wait_for`, all with a predicate, all returning `bool`. You will not find a predicate-less version; the standard simply doesn't provide one — the predicate is mandatory.

The skeleton of the wait is spelled out in equivalent code by the standard ([thread.condvarany.intwait]/2): for this call, register the condition variable with the stop request's notification relationship, then `while (!stoken.stop_requested()) { if (pred()) return true; wait(lock); } return pred();`. The return value reports predicate truth and nothing else, from start to finish (/1 and the note after it). If the request was already standing before the wait began, the loop is never entered and the predicate's result is handed to you on the spot. The timed versions add a note: predicate true from the start, request already standing, deadline already past — none of the three blocks (the note at /8). The arrangement is the same idea as the predicate `wait_for` from 3.2, with one more wire attached: the sources that can wake it now include the stop request.

I ran two acts in one program: the consumer becomes a `std::jthread`, whose destructor requests stop itself before `join`; the wait takes the token, and the request wakes it on cue. Act one, the queue is empty. Act two, goods are there but the notify is deliberately withheld, letting the stop request play the waker. `parked` is still the parked-count from 3.2 — `main` only moves once the waiter is properly hung:

<OnlineCompilerDemo
  title="stop_token Inside the Wait: Act One Leaves Empty-Handed, Act Two Still Delivers"
  source-path="code/examples/vol5/chapter03/21_stop_wake.cpp"
  description="condition_variable_any's wait with stop_token: the jthread requests stop at destruction. The empty-queue act is woken by the stop request and returns false; in the act with goods present the notify is deliberately withheld, yet the stop request still gets the goods delivered."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// stop_wake.cpp —— stop_token 进等待：第一幕空手走，第二幕货照交
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::mutex m;
std::condition_variable_any cv;
std::queue<int> q;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();

  { // 第一幕：队列空着，叫醒它的是停止请求
    std::jthread consumer([&](std::stop_token st) {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool got = cv.wait(lk, st, [] { return !q.empty(); });
      std::printf("consumer: %5.1f ms 离开等待，返回值 %d（队列是空的）\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main    : %5.1f ms 请求停止\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    consumer.request_stop();
  }   // jthread 析构：停止已经请求过，这里只剩 join

  while (!q.empty()) q.pop();
  parked = 0;
  auto t1 = Clock::now();

  { // 第二幕：货到了，通知故意不发，停止请求负责叫醒
    std::jthread consumer([&](std::stop_token st) {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool got = cv.wait(lk, st, [] { return !q.empty(); });
      if (got) {
        int v = q.front();
        q.pop();
        std::printf("consumer: %5.1f ms 拿到 %d，此刻 stop_requested = %d\n",
                    ms_since(t1), v, st.stop_requested() ? 1 : 0);
      } else {
        std::printf("consumer: %5.1f ms 空手离开，返回值 false\n", ms_since(t1));
      }
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    { std::lock_guard<std::mutex> g(m); q.push(42); }  // 故意不 notify
    std::this_thread::sleep_for(60ms);
    std::printf("main    : %5.1f ms 请求停止\n", ms_since(t1));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    consumer.request_stop();
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
main    :  61.6 ms 请求停止
consumer:  63.8 ms 离开等待，返回值 0（队列是空的）
main    :  61.4 ms 请求停止
consumer:  63.6 ms 拿到 42，此刻 stop_requested = 1
```

Act one is the shape we've already seen: `false`, leaving empty-handed, with roughly two milliseconds between the request line and the wait's return — mostly the fixed yielding pause after printing. The interesting part of act two is the return value: `true`. The predicate holds, the goods are delivered, even with the stop request already standing — the output shows `stop_requested = 1`. The return value's meaning never changed: it is the predicate's truth, not "did we stop". The library keeps the two things cleanly apart: who woke you, and whether what you waited for came true — each in its own box. When the stop request passes by, carrying off work that was already finished is no loss.

Where the crack is, and how the library blocks it, I read out of the local GCC 16.2.1 `<condition_variable>` (`/usr/include/c++/16/condition_variable`). The callback it registers on your behalf has a body of exactly one statement: `notify_all()`. And `notify_all` must take its own internal lock; the waiting side holds that same internal lock while re-checking the stop request, and only hangs itself up after the check. For the callback to fire its notify, the lock must come free — meaning the little window of "checked, not yet asleep" has already passed. No notify can drop into the crack, so the timing is not yours to worry about.

When all you have is `condition_variable`, we build the bridge ourselves. *Ending Work in an Orderly Way* left a line — "the details of waiting and notifying belong to the later unit on waiting" — and this is the place it meant. The build is three sentences: register a `stop_callback` whose body calls `notify_all`. Let the predicate take `token.stop_requested()` into account. Register before taking the lock and calling `wait`, because the "two ends" are two timings. If the request is already standing when you register, the callback runs immediately on the registering thread ([stoptoken.concepts]); it then wants the same `m` you may already be holding through `lk`, and one thread re-entering a non-recursive mutex deadlocks on the spot. Register before locking, and the callback gets an uncontended lock — a notify into the void is harmless, since the waiter's predicate, once it takes the lock, sees `stop_requested()` already standing and never sleeps at all. The other timing is a request arriving mid-wait: the callback runs on the requester's thread and plugs the crack by taking the same lock — the very scenario the next paragraph walks through.

<OnlineCompilerDemo
  title="Only condition_variable in Hand: Bridging with stop_callback"
  source-path="code/examples/vol5/chapter03/22_bridge.cpp"
  description="Registration before the lock: when the stop request arrives, the callback first takes the same lock to plug the gap between checking and falling asleep, then notify_all; the waiter reads stop_requested and flips its condition true."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// bridge.cpp —— 手头只有 condition_variable 时，用 stop_callback 搭桥
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::mutex m;
std::condition_variable cv;
bool closed = false;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();
  std::stop_source source;
  auto token = source.get_token();

  std::thread waiter([&] {
    // 桥：注册赶在拿锁之前，两头都罩得住
    // 注册时已立：回调当场在注册线程执行，此时 m 还空着，不会自己卡自己
    // 等待中到达：回调跑在请求方线程，拿同一把锁挡缝
    std::stop_callback cb(token, [&] {
      { std::lock_guard<std::mutex> g(m); }   // 拿同一把锁，挡住检查与入睡之间的缝
      cv.notify_all();
    });
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [&] { return closed || token.stop_requested(); });
    std::printf("waiter: %5.1f ms 离开等待，closed = %d\n",
                ms_since(t0), closed ? 1 : 0);
  });

  while (parked.load() < 1) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(60ms);
  std::printf("main  : %5.1f ms 请求停止\n", ms_since(t0));
  std::fflush(stdout);
  std::this_thread::sleep_for(2ms);
  source.request_stop();
  waiter.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
main  :  61.3 ms 请求停止
waiter:  63.6 ms 离开等待，closed = 0
```

What we walk through now is the request arriving mid-wait, with the callback running on the requester's thread. Taking the lock inside the callback looks redundant, but it is where the whole bridge carries its load. Run the no-lock version through your head: the predicate finishes checking inside the lock and returns `false`; then `wait` fuses releasing the lock with hanging up — one atomic step. If the callback doesn't take the lock, it might squeeze exactly into the "checked, not yet asleep" gap and run its `notify_all` to nobody; the notify lands in the void, the wait sleeps on, and nobody ever calls again. With the callback taking the same lock, it is mutually exclusive with the predicate check: if it can get the lock, that little window has already passed, and the notify lands after the fall-asleep. I could not build a stable reproduction of this crack on my machine, but you can verify the reasoning yourself; the library's implementation blocks it with its internal lock (previous paragraph), and copying that costs you nothing. Keep the callback itself short — it runs on the body of whoever calls `request_stop()`, as *Ending Work in an Orderly Way* taught: a little longer, and you slow down their shutdown.

*Thread-Safe Queue* already built this bridge into `pop`. It gave `pop` a version accepting a `stop_token`, internally the token-carrying overload: a `false` return exits the loop, reporting "nothing taken". It keeps `close()` at the same time, and the article says so explicitly: `close` governs producers no longer pushing, `request_stop` governs consumers not waiting themselves to death on an empty queue — fire both, each governs its own end. We can now read its predicate fluently: `!empty() || closed` — the waiter only agrees to disband upon seeing `closed`, exactly the pattern of A at the start.

Timeout and stop can also be packed into one call; let's run them on the same stage. The `stop_token` version of `wait_until` takes a time point plus a token, and the predicate is a single one — write the business condition itself, don't stuff `stop_requested()` into it; use that afterwards, when `false` comes back, to tell who did the waking:

<OnlineCompilerDemo
  title="Timeout and Stop Packed into One wait_until"
  source-path="code/examples/vol5/chapter03/23_deadline.cpp"
  description="Deadline 300 ms, stop request at 60 ms: wait_until with the token returns early with false, and stop_requested afterwards tells whether it was woken by the stop or by the clock."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// deadline.cpp —— 超时和停止拼进同一次调用
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::mutex m;
std::condition_variable_any cv;
bool done = false;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();
  std::stop_source source;
  auto token = source.get_token();
  auto deadline = Clock::now() + 300ms;   // 时限从调用方来

  {
    std::jthread waiter([&] {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool ok = cv.wait_until(lk, token, deadline, [&] { return done; });
      if (ok) {
        std::printf("waiter: %5.1f ms 返回，done 成了\n", ms_since(t0));
      } else if (token.stop_requested()) {
        std::printf("waiter: %5.1f ms 返回，返回值 false，叫醒它的是停止请求\n",
                    ms_since(t0));
      } else {
        std::printf("waiter: %5.1f ms 返回，返回值 false，时限到头\n", ms_since(t0));
      }
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main  : %5.1f ms 请求停止（时限其实还没到）\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    source.request_stop();
  }   // jthread 析构：停止已请求过，这里只剩 join
  return 0;
}
```

</OnlineCompilerDemo>

```text
main  :  61.4 ms 请求停止（时限其实还没到）
waiter:  63.7 ms 返回，返回值 false，叫醒它的是停止请求
```

The deadline was 300 ms; the request line printed at 61 ms and the wait returned at 64 ms, a gap of 2-odd ms still dominated by the fixed yielding pause after printing — clearly the deadline never held it. After `false` comes back, ask `token.stop_requested()`: if it was the stop, file the "gave up" under the shutdown flow; only a spent deadline goes under timeout — that way the two failures stay separable in the logs.

## A Checklist: Can the Wait End as Designed

The four ways out walked through, they collapse into one table. The checklist in *Ending Work in an Orderly Way* governs a whole thread's exit path; this table governs each individual wait — both are worth walking through before work starts. Whenever I finish writing a wait, I hold it up against these rows:

| Check | How to judge | Typical defect |
|--------|--------|----------|
| Does every `wait` have an exit path | With the predicate false, what event can flip it true? No answer means a dead wait | Predicate is only `!q.empty()`, nobody raises a flag, nobody notifies |
| Where does the timeout come from | Passed at the call site, from config, or hard-coded? After `false`, which line retries or gives up | Deadline buried in a deep call, nothing done after `false`, straight into the next loop round |
| Who sends the stop request | Who holds the `stop_source`? A `jthread` destructor sends one; do `close` (the queue end) and `request_stop` each govern their own end, clearly said | Shutdown only closes without stopping: the wait can't disband. Only stops without closing: nothing blocks new pushes |
| After a spurious wakeup, does the condition still hold | Is the first thing on waking to re-evaluate the predicate? Predicate `wait` and the `stop_token` overloads do it for you; a bare `wait` needs your own `while` | Waking and taking goods without checking the condition |
| Is the un-waitable event recorded | Can "never coming" be seen by the predicate? If the upstream dies, who raises the flag | Producer crashes, consumer sleeps on an empty queue until someone kills the process |

Hold the table up against A through D at the start — every row answers: A's predicate carries `closed`, and the closer raises the flag inside the lock and notifies outside it. B's deadline is written at the call site, and after `false` it prints its surrender. C's `stop_source` sits in `main`'s hand, with the token inside the wait. D's `failed` has a claimant. Spurious wakeups are covered all along by the predicate waits. Whichever row you cannot answer, your code still owes one explicit sentence — write it before handing the code over.

## Next Steps

Before turning the page, dig out the predicate of every wait you own and walk it against the table. The rows you cannot answer are exactly where the one sentence this article prepared for you should land.

## Reference Resources

The progression of this article once again follows the queue and cooperative-cancellation chapters of *C++ Concurrency in Action* (Anthony Williams, Manning); the examples and prose are my own. The outputs shown are single-run readings; I re-ran once, with B landing at 120.6 ms and everything else within a millisecond or two. The source for `condition_variable_any`'s internals is the local header file; there is no web page to cite. The cppreference pages count as of the versions they label.

- [std::condition_variable_any (cppreference)][cvany], the generalized version since C++11; C++20 added the `stop_token` waits.
- [std::condition_variable_any::wait (cppreference)][cvany-wait], the three overloads and the `stop_token` version's equivalent code.
- [Interruptible waits (current draft, thread.condvarany.intwait)][draft-intwait], registration semantics, the equivalent expansion, and the return value reporting only predicate truth.
- [Class condition_variable (current draft, thread.condition.condvar)][draft-condvar], the return values of `wait_for` and the predicate expansion — the basis of scene B.
- [std::stop_token (cppreference)][token], the reading side: querying the request and its state.
- [std::stop_callback (cppreference)][callback], registration timing and the run-immediately rule.
- [std::jthread (cppreference)][jthread], requests stop then joins in the destructor.
- [C++ standard draft: stop token concepts clause (stoptoken.concepts)][draft-stop], synchronization of registration and callback execution.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.
- GCC 16.2.1's bundled `<condition_variable>` (local path `/usr/include/c++/16/condition_variable`): the internal lock and callback shape of `condition_variable_any` were read from here.

[cvany]: https://en.cppreference.com/w/cpp/thread/condition_variable_any
[cvany-wait]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[draft-intwait]: https://eel.is/c++draft/thread.condvarany.intwait
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[token]: https://en.cppreference.com/w/cpp/thread/stop_token
[callback]: https://en.cppreference.com/w/cpp/thread/stop_callback
[jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[draft-stop]: https://eel.is/c++draft/stoptoken.concepts
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
