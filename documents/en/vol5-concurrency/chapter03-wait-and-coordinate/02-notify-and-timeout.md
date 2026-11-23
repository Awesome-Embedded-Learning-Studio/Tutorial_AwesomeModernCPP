---
title: "Whom to Wake, How Long to Wait: Choosing notify and Using Timeouts"
description: "Whom does one notify wake: how to choose between notify_one and notify_all, what happens when it wakes the wrong thread, how to read the return value of a timed wait, and finally the generalized condition_variable_any."
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter03-wait-and-coordinate/02-notify-and-timeout.md
  source_hash: 3dd7e8a93c85ca8aeaac3def026e107df9e403f151e369a6211c107529cb23ed
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 13463
---

# Whom to Wake, How Long to Wait: Choosing notify and Using Timeouts

The previous article assembled the basic parts of waiting and notifying: what to wait for, who does the waking, and why a predicate is mandatory. This one handles two finer questions: whom a given notify should wake, and how to bring a time limit along when the wait might never pay off.

## notify_one or notify_all

Threads sleep on the condition variable, but the condition itself is your own variable. The condition variable does not know who is waiting for what; all it knows is "someone is waiting, give a shout".

Put two threads waiting on two different conditions to sleep on the same `cv`, and we can watch it give itself away.

<OnlineCompilerDemo
  title="notify_one Wakes the Wrong Thread: The Notify Is Swallowed by Another Waiter"
  source-path="code/examples/vol5/chapter03/06_notify_one_wrong_thread.cpp"
  description="Two groups asleep on one cv; notify_one picks without looking at conditions: it wakes the earlier-sleeping closer, and taker misses this notify. The output shows who woke and who did not."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// notify_one_wrong_thread.cpp —— 一把 cv 睡两拨人：notify_one 叫醒的是早睡的 closer，taker 错过这趟
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool stop = false;
bool data_ready = false;
std::atomic<int> parked{0};
std::atomic<bool> taker_got{false};

int main() {
  std::thread closer([] {
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [] { return stop; });
    std::printf("closer: 收到 stop\n");
  });
  while (parked.load() < 1) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);

  std::thread taker([] {
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [] { return data_ready; });
    taker_got = true;
    std::printf("taker: 拿到数据\n");
  });
  while (parked.load() < 2) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);

  {
    std::lock_guard<std::mutex> g(m);
    data_ready = true;
  }
  cv.notify_one();

  std::this_thread::sleep_for(200ms);
  std::printf("main: 200 ms 过去，taker %s\n",
              taker_got.load() ? "已经拿到数据" : "还没醒过来");

  {
    std::lock_guard<std::mutex> g(m);
    stop = true;
  }
  cv.notify_all();
  closer.join();
  taker.join();
  std::printf("main: 收工\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
main: 200 ms 过去，taker 还没醒过来
taker: 拿到数据
closer: 收到 stop
main: 收工
```

We placed two threads on the same `cv`: `closer` waits for `stop`, `taker` waits for `data_ready`. `main` changes `data_ready` and then calls `notify_one()`. The standard only promises to "unblock one of" the waiting threads; which one gets no say from the condition. In this run the notify woke `closer`, which had gone to sleep before `taker` — the implementation picks by its own queue order — and on seeing `stop` still false, it rolled over and went back to sleep. `taker` missed the boat and was still asleep 200 ms later. On a different implementation the woken thread might be `taker`, and the output would flip.

The failure is perfectly quiet. The program does not crash, the logs report nothing; all you observe is one thread waiting forever. In this demo `taker` is eventually rescued by the closing `notify_all` — a real program has no second notify coming, and the thread would simply sit there.

The fix depends on how you organize your conditions. If the conditions were always meant to be separate, give each condition its own `condition_variable` and let each do its own waking. If there is a single condition — "the whole batch starts", say — that everyone waiting on it should proceed past, then use `notify_all`.

<OnlineCompilerDemo
  title="notify_all as the Safety Net: From One-of-Three to All Three Released"
  source-path="code/examples/vol5/chapter03/07_notify_one_then_all.cpp"
  description="Three threads wait on the same go: notify_one releases one, notify_all releases them all; both notify styles run once each, with the outputs to compare side by side."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// notify_one_then_all.cpp —— 三条线程等同一个 go：notify_one 醒 1 条，notify_all 全放行
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  std::mutex m;
  std::condition_variable cv;
  bool go = false;
  std::atomic<int> started{0};
  std::atomic<int> woken{0};
  std::vector<std::thread> ws;
  for (int i = 0; i < 3; ++i) {
    ws.emplace_back([&, i] {
      std::unique_lock<std::mutex> lk(m);
      ++started;
      cv.wait(lk, [&] { return go; });
      ++woken;
      std::printf("worker %d: 出发\n", i);
    });
  }
  while (started.load() < 3) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);
  {
    std::lock_guard<std::mutex> g(m);
    go = true;
  }
  cv.notify_one();
  std::this_thread::sleep_for(100ms);
  std::printf("main: notify_one 之后，醒来的有 %d 条\n", woken.load());
  cv.notify_all();
  for (auto& t : ws) t.join();
  std::printf("main: notify_all 之后，醒来的有 %d 条\n", woken.load());
  return 0;
}
```

</OnlineCompilerDemo>

```text
worker 0: 出发
main: notify_one 之后，醒来的有 1 条
worker 1: 出发
worker 2: 出发
main: notify_all 之后，醒来的有 3 条
```

Before we changed `go`, all three threads had gone to sleep. After `notify_one`, one wakes; after `notify_all`, all three go.

`notify_all` never loses on correctness; the cost is that all the awakened threads scramble for the same lock, and of N threads, N-1 ran for nothing. So the criterion for choosing sits with the condition: however many threads a change is meant to let proceed, that many you wake. When unsure, `notify_all` is the safe side.

`notify_one` likewise does nothing when nobody is waiting — the lost-wakeup section of the previous article, *Waiting for a Condition: From Polling to Condition Variables*, was about exactly that.

## Waits with Timeouts

Every waiting style so far can wait forever. Real programs usually want a ceiling: past a certain point without success, go do something else.

`wait_for` and `wait_until` are built for that, both since C++11. You will meet four overloads, with only two kinds of return value:

- `wait_for(lock, rel_time)` returns `std::cv_status`: you get `timeout` or `no_timeout`.
- `wait_for(lock, rel_time, pred)` returns `bool`: whether the condition came true.
- `wait_until(lock, abs_time)` returns `cv_status`, with the relative duration swapped for an absolute time point.
- `wait_until(lock, abs_time, pred)` returns `bool`.

The predicate forms give `bool`; the predicate-less forms give `cv_status`. `no_timeout` means the call returned before the deadline — possibly a notify, possibly a spurious wakeup — and whether the condition holds is still yours to check.

Let's line up six scenarios in one run:

<OnlineCompilerDemo
  title="Six wait_for Scenarios: Three with a Predicate, Three Without"
  source-path="code/examples/vol5/chapter03/08_wait_for_six_cases.cpp"
  description="Satisfied within the deadline, deadline hit unsatisfied, condition already true before the wait... six scenarios laid out in one program, with return values reporting only predicate truth."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// wait_for_six_cases.cpp —— 带超时的等待：带谓词与不带谓词各三种情形，摆在一起跑
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

const char* name_of(std::cv_status s) {
  return s == std::cv_status::timeout ? "timeout" : "no_timeout";
}

int main() {
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = true;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 200ms, [&] { return ready; });
    std::printf("A pred 本来成立   -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::thread t([&] {
      std::this_thread::sleep_for(30ms);
      { std::lock_guard<std::mutex> g(m); ready = true; }
      cv.notify_one();
    });
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 200ms, [&] { return ready; });
    double e = ms_since(t0);
    lk.unlock();
    t.join();
    std::printf("B 30 ms 被通知    -> %d, %.3f ms\n", ok, e);
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 60ms, [&] { return ready; });
    std::printf("C 60 ms 到期      -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::thread t([&] {
      std::this_thread::sleep_for(30ms);
      { std::lock_guard<std::mutex> g(m); ready = true; }
      cv.notify_one();
    });
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    std::cv_status st = cv.wait_for(lk, 200ms);
    double e = ms_since(t0);
    lk.unlock();
    t.join();
    std::printf("D 无谓词，30 ms 通知 -> %s, %.3f ms\n", name_of(st), e);
  }
  {
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    std::cv_status st = cv.wait_for(lk, 60ms);
    std::printf("E 无谓词，没人通知   -> %s, %.3f ms\n", name_of(st), ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = true;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, -1ms, [&] { return ready; });
    std::printf("F 时限已过但 pred 成立 -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
A pred 本来成立   -> 1, 0.000 ms
B 30 ms 被通知    -> 1, 30.213 ms
C 60 ms 到期      -> 0, 60.661 ms
D 无谓词，30 ms 通知 -> no_timeout, 30.289 ms
E 无谓词，没人通知   -> timeout, 60.228 ms
F 时限已过但 pred 成立 -> 1, 0.000 ms
```

A through C are the predicate versions; let's take them line by line. A's predicate already holds on entry, and it returns 1 after waiting 0.000 ms. The standard notes explicitly that no blocking occurs when the predicate is true from the start, even if the deadline has already passed (the note at [thread.condition.condvar]/37). B gets its notify at 30 ms and returns 1. C hears from nobody, the 60 ms deadline runs out, it returns 0, having actually waited 60.7 ms.

D and E carry no predicate, so you must re-check the condition yourself. D gets a notify, `no_timeout`; E runs to its deadline, `timeout`.

F sets the deadline to a past `-1ms`, the predicate holds, and you still get 1 immediately. It says the same thing as A: with the predicate true, no sleep — deadline passed or not.

Two things to keep an eye on. The extra 0.7 ms beyond 60 ms in C comes from timer and scheduling granularity — a timeout is not an alarm clock, so don't treat it as a precise cutoff. And `wait_for` returning `false` does not mean no notify will ever come; it only says none arrived within the duration you gave. Whether you retry or give up afterwards is your design decision; how exactly to wind things down continues in 3.6, *A Wait Also Needs a Way to End*.

As for how `wait_for` turns a duration into an absolute time point, the standard has a paragraph on the helper it uses, and that helper uses `steady_clock` ([thread.condition.general]/6). Relative durations are therefore immune to adjustments of the system clock. `wait_until` is another matter: what you pass in is a time point, and if it was computed on `system_clock`, changing the system clock drags it along. If you want monotonic behavior, compute that time point from `steady_clock::now()`.

## condition_variable_any: The Generalized Version (Extension)

> This section is an extension; skipping it does not affect the main line. The queue storyline starting from 3.3, *A Queue with Capacity: Getting It to Stand First*, uses `std::condition_variable`, and so does the main line of 3.6, *A Wait Also Needs a Way to End*. The `condition_variable_any` overloads taking a `stop_token` are handled separately in 3.6.

`std::condition_variable` pairs only with `unique_lock<std::mutex>` ([thread.condition.general]/1). But the lock in your hand does not necessarily look like that — the reading side might be holding `std::shared_lock<std::shared_mutex>`, for instance.

`std::condition_variable_any` has provided the generalized version since C++11 (abbreviated `cv_any` below; the object in the example is still named `cv`). Any lock that satisfies BasicLockable — that is, has `lock()` and `unlock()` — will do ([thread.condition.condvarany.general]/1).

The most typical scenario is several reader threads waiting for a configuration to be published. `std::shared_mutex` arrived only in C++17, so compile the example with C++17 or later:

<OnlineCompilerDemo
  title="condition_variable_any with shared_lock: Reader Threads Wait for a Config Release"
  source-path="code/examples/vol5/chapter03/09_cv_any_shared_lock.cpp"
  description="The generalized condition variable accepts any lockable object: three reader threads hold shared_lock while waiting for a config release, and after publication each reads its own copy."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// cv_any_shared_lock.cpp —— condition_variable_any 配 shared_lock：三条读线程等一份配置发布
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::shared_mutex rm;
std::condition_variable_any cv;
bool published = false;
std::string config;

int main() {
  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i) {
    readers.emplace_back([i] {
      std::shared_lock<std::shared_mutex> lk(rm);   // 共享锁，几个人同时读
      cv.wait(lk, [] { return published; });
      std::printf("reader %d: %s\n", i, config.c_str());
    });
  }
  std::this_thread::sleep_for(50ms);
  {
    std::unique_lock<std::shared_mutex> lk(rm);     // 写的时候独占
    config = "level = 3";
    published = true;
  }
  cv.notify_all();
  for (auto& t : readers) t.join();
  std::printf("main: 三条读线程读到的都是 level = 3\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
reader 0: level = 3
reader 1: level = 3
reader 2: level = 3
main: 三条读线程读到的都是 level = 3
```

Watch the three reader threads: each hands its `shared_lock` into `cv.wait`, which `std::condition_variable` cannot accept — only `cv_any` can take it. The writing side takes the exclusive lock, changes the config, raises `published`, and after releasing the lock calls `notify_all`; only then do the three readers wake together and read the same configuration. The print order is decided by the scheduler and differs on every run.

The cost sits right here: every `wait` must additionally take and release an internal lock. I looked through the local GCC 16 `<condition_variable>`: `condition_variable_any` carries its own `shared_ptr<mutex> _M_mutex`, created with `make_shared` at construction. On every `wait`, it takes the internal lock, releases your lock, and on waking takes yours back. `notify_one` and `notify_all` have to take it too.

From the implementation we can see the internal lock doing two jobs. First, it lets "hang the thread into the wait queue" and "fire the notify" share one lock: `wait` holds the internal lock, releases your lock, then hangs the thread into `_M_cond`'s wait queue. `notify_one` must acquire that same internal lock before it can fire. That way no notify can slip into the gap between "condition checked" and "not yet asleep". Second, it lets every lock type walk the same waiting logic.

The standard adds a note under `[thread.condition.condvarany.general]`: when `Lock` is neither a standard mutex type nor a `unique_lock` wrapper around one, the synchronization around the predicate is yours to guarantee (the note at [thread.condition.condvarany.general]/1). `shared_lock<shared_mutex>` is exactly one such case. In the example above, the predicate reads `published` while holding the shared lock and the writer holds the exclusive lock, so the synchronization is there.

C++20 added a family of `stop_token` waits to `cv_any` ([thread.condvarany.intwait]); those belong to 3.6, and we will not unfold them here.

The choosing rule is short, and following it is enough: if the lock is `unique_lock<std::mutex>`, use `condition_variable`; only when it is something else does `cv_any` get its turn.

## Your Turn to Try

Before writing any wait, I walk a few questions through:

1. What condition are you waiting on? Who changes it, and which lock does the changing side hold?
2. Is the wait the predicate form of `wait`? Are the variables the predicate reads protected by the same lock the changer holds?
3. Does the notify happen after the condition is changed? Could it fire while nobody is waiting and then vanish without a sound?
4. How many distinct conditions hang on one condition variable? Could `notify_one` wake the wrong thread?
5. Could the wait never come to an end? Should you use `wait_for` as a fallback, and what do you plan to do when the fallback returns `false`?

## Next Steps

Condition variables only solve the waking. They have no say in where a woken thread lines up next, nor in how work is divided among several threads waking at once.

Before turning the page, pick a `sleep`-based poll you once wrote and convert it to `wait` plus a predicate. As you convert, write down the variables the predicate reads and the lock guarding them. If the two do not match up, pull the condition-changing path under the same lock as well.

## Reference Resources

While writing this article I followed the progression of Anthony Williams's book, *C++ Concurrency in Action*; its chapter on condition variables tracks the arrangement here most closely. The examples and the prose are my own. I fetched the cppreference pages twice and both times got only the navigation shell with the body cut off, so clause citations follow the current working draft on eel.is, with versions as labeled there.

- [std::condition_variable (cppreference)][cppref-cv], since C++11.
- [std::condition_variable::wait_for (cppreference)][cppref-waitfor], the two return types, `cv_status` and `bool`.
- [std::condition_variable_any (cppreference)][cppref-cvany], since C++11; accepts any BasicLockable lock.
- [General (current draft, thread.condition.general)][draft-general], the three atomic steps of waiting, the total order of notifications, and the `steady_clock` used when converting a duration into a time point.
- [Class condition_variable (current draft, thread.condition.condvar)][draft-condvar], `wait`'s `or spuriously`, the predicate form's equivalent expansion, the effect of `notify_one`, and timeouts and return values.
- [Class condition_variable_any (current draft, thread.condition.condvarany)][draft-condvarany], the BasicLockable requirements and the note about predicate synchronization.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression of the condition-variable chapter.
- GCC 16.2.1's bundled `<condition_variable>` (local path `/usr/include/c++/16/condition_variable`): the internal mutex of `condition_variable_any` was read from here; there is no web page to cite.

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[cppref-waitfor]: https://en.cppreference.com/w/cpp/thread/condition_variable/wait_for
[cppref-cvany]: https://en.cppreference.com/w/cpp/thread/condition_variable_any
[draft-general]: https://eel.is/c++draft/thread.condition.general
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-condvarany]: https://eel.is/c++draft/thread.condition.condvarany
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
