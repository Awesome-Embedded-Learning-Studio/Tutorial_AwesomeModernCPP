---
title: "Waiting for a Condition: From Polling to Condition Variables"
description: "Start by measuring the cost of sleep-polling, then hand the waiting over to condition variables: a spurious wakeup demo where waking does not mean the condition holds, a lost wakeup where the notify fires before the wait, and why the predicate and the lock are inseparable."
chapter: 5
order: 1
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
  source: documents/vol5-concurrency/chapter03-wait-and-coordinate/01-wait-for-a-condition.md
  source_hash: 4584cc6b4a6b2bc02b9191033f020ec819821528c5df47d26e7ec0cb2fd5d368
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 11473
---

# Waiting for a Condition: From Polling to Condition Variables

## Spinning: The Cost of Polling

I set up a minimal testbed and measured what spinning actually costs: one thread does 100 milliseconds of work, then raises `done`; the other thread waits for `done`. Each of the four waiting strategies is written as a callback handed to the same harness. The program runs twice, and each pass exercises all four strategies once.

<OnlineCompilerDemo
  title="The Cost of Polling: Busy-Waiting, Two Sleep-Polling Intervals, and a Condition Variable, Measured Side by Side"
  source-path="code/examples/vol5/chapter03/01_polling_cost.cpp"
  description="The original experiment extracted verbatim: each waiting strategy is measured for CPU time and for the latency from flag to observation, and the busy-wait core burns itself entirely on spinning. Readings move from run to run, but the orders of magnitude hold."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// polling_cost.cpp —— 转圈的代价：空转、两种睡眠轮询、条件变量，四种等法同场量一遍
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::atomic<int> done{0};
std::atomic<long long> finished_at{0};
std::mutex m;
std::condition_variable cv;

long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch()).count();
}

template <class Wait>
void run(const char* name, Wait wait) {
  done = 0;
  finished_at = 0;
  std::thread worker([] {
    std::this_thread::sleep_for(100ms);   // 顶替“算完”那段耗时
    finished_at = now_ns();
    { std::lock_guard<std::mutex> g(m); done = 1; }
    cv.notify_one();
  });
  std::this_thread::sleep_for(10ms);      // 让 worker 睡下，主线程再开始等
  std::clock_t c0 = std::clock();
  long long rounds = 0;
  wait(rounds);
  std::clock_t c1 = std::clock();
  long long seen = now_ns();
  worker.join();
  std::printf("%-8s rounds=%9lld  cpu=%7.2f ms  delay=%9.1f us\n", name, rounds,
              1000.0 * (c1 - c0) / CLOCKS_PER_SEC,
              (seen - finished_at.load()) / 1000.0);
}

int main() {
  for (int pass = 1; pass <= 2; ++pass) {
    std::printf("--- pass %d ---\n", pass);
    run("spin", [](long long& r) {
      while (done.load(std::memory_order_relaxed) == 0) ++r;
    });
    run("sleep1", [](long long& r) {
      while (done.load() == 0) { ++r; std::this_thread::sleep_for(1ms); }
    });
    run("sleep20", [](long long& r) {
      while (done.load() == 0) { ++r; std::this_thread::sleep_for(20ms); }
    });
    run("cv", [](long long&) {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk, [] { return done.load() == 1; });
    });
  }
  return 0;
}
```

</OnlineCompilerDemo>

Here are both passes' readings in one table; within each cell, the number before the slash is the first pass and the number after it is the second:

| Waiting strategy | Rounds | CPU spent waiting | From condition to notice |
|------|---------|----------------|----------------|
| Busy-wait `while (done.load(std::memory_order_relaxed) == 0) ++r;` | 440M / 420M | 89.2 / 89.3 ms | 8.7 / 4.9 µs |
| Check every 1 ms | 83 / 83 | 0.86 / 0.94 ms | 527 / 851 µs |
| Check every 20 ms | 5 / 5 | 0.14 / 0.11 ms | 11.2 / 11.0 ms |
| Condition variable | 0 / 0 | 0.03 / 0.05 ms | 45 / 102 µs |

The two numbers in each cell come from the same program run twice within one execution, not from two samples inside a single pass. The CPU column reads the process's CPU time with `std::clock()`; the worker is asleep, so nearly all of that time is charged to the main thread. The notice column timestamps with `steady_clock` — the two columns use different clocks, so don't add or subtract across them. The four rows correspond to the four lambdas in the code; the busy-wait row's loop body is exactly the line quoted in the table, down to the `memory_order_relaxed`. The condition-variable row's 0 is there because that strategy never enters a loop in the first place, not because it went unrecorded.

Busy-waiting notices fastest, in 5 to 9 microseconds, at the cost of 89 ms of CPU: the main thread starts 10 ms late, and for the roughly ninety milliseconds it spends waiting, one core is occupied by it the whole way. The condition variable spends 0.03 to 0.05 ms of CPU and notices within tens to a hundred microseconds. The two middle rows are polling tuned two ways: checking every 1 ms brings CPU down and notice time to a few hundred microseconds, but the thread still wakes a thousand times per second. Checking every 20 ms spends almost no CPU, but notice time slides to around eleven milliseconds. No single interval covers both ends: watch tightly and the CPU burns for nothing; loosen the interval and you find out late.

There is another cost that is harder to tally. What interval to fill in depends on how long the thing you are waiting on will take. For 100 ms of work, 20 ms is tolerable. What about 3 ms of work? What about an event coming back from the network, taking nobody knows how long? Polling asks you to guess a deadline on its behalf before the condition has even come true.

## Handing the Wait to a Condition Variable

Condition variables exist for exactly this job of waiting on a condition: while the condition does not hold, we let the thread sleep and spend no CPU. Once the condition comes true, the side that changed it wakes the sleeper. Here is the smallest runnable version:

<OnlineCompilerDemo
  title="A Minimal Condition Variable: worker Changes the Condition, waiter Waits Until It Holds"
  source-path="code/examples/vol5/chapter03/02_cv_minimal.cpp"
  description="The in-text fragments assembled into a complete program: main lets waiter settle into its sleep, after 50 ms worker raises done and notifies, and when waiter wakes the condition is guaranteed to hold. The fragment sections are kept verbatim."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// cv_minimal.cpp —— 能跑的最小版本：worker 改完条件叫一声，waiter 睡到条件成立
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool done = false;   // 由 m 保护

void worker() {
  // 干活
  {
    std::lock_guard<std::mutex> g(m);
    done = true;
  }
  cv.notify_one();
}

void waiter() {
  std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, [] { return done; });
  // 到这里 done 一定是 true，锁还在手里
}

int main() {
  std::thread t(waiter);
  std::this_thread::sleep_for(50ms);   // 让 waiter 先查条件、先睡下去
  worker();                            // 改条件，再通知
  t.join();
  std::printf("waiter 回来了，done = %d\n", done ? 1 : 0);
  return 0;
}
```

</OnlineCompilerDemo>

`std::condition_variable` has lived in `<condition_variable>` since C++11. The standard lets it accept only `unique_lock<std::mutex>`, in exchange for a lighter implementation. If the lock in your hand is of some other type, you need its generalized sibling; that part sits in the extension section of the next article, *Whom to Wake, How Long to Wait: Choosing notify and Using Timeouts*, and can be skipped.

The second argument to `wait` is a predicate — a small function returning `true` or `false`; the `pred` in the standard's wording refers to exactly this. It answers one question for us: does the condition hold yet?

The standard splits what `wait` does into three steps ([thread.condition.general]/3). The brackets hold the draft's clause name and, after the slash, the paragraph number; throughout this series, citations follow the current working draft on eel.is — the clause names match the official standard, while paragraph numbers may drift. Here we only need the first step spelled out: releasing the mutex and entering the wait are one and the same step. The moment the lock is let go, the thread is already queued among the waiters, and nobody can wedge anything in between. The unblocking step belongs to a later section; the re-acquiring step waits until `wait` returns.

Look at the arrangement from the notifying side. `worker` must hold `m` to modify `done`, and `waiter` holds `m` while checking `done`. Both sides contend for the same lock, so only two orderings can occur. Either `worker` finishes its change before the check, in which case `waiter` sees true the moment it looks and never sleeps at all. Or `waiter` has already checked and already gone to sleep, in which case `worker` changes the condition and then notifies, waking it right on cue.

When `wait` returns, the lock is back in your hand — that is the third step. When the predicate form of `wait` returns, the condition is true and nobody can change it behind your back, so you can safely read `done` and the data beside it. Waking with the lock in hand and the condition holding is something an ordinary sleep cannot give you.

Whether the notify sits inside the lock or outside it, correctness is unaffected. I habitually put it outside, so the awakened thread can grab the lock as soon as it wakes. Inside works too; the awakened thread just has to wait a little longer on the lock after waking.

## Spurious Wakeups: Waking Does Not Mean the Condition Holds

Unblocking has three possible causes in the standard: `notify_one`, `notify_all`, and spuriously — that is, with no notification at all ([thread.condition.condvar]/8.3). The original text is a single word, with no condition attached. An implementation is allowed to do this, so our code has to be prepared to wake at any moment.

I ran this many times on my own machine and never produced one genuine spurious wakeup. The license the standard writes down still has to be taken seriously: the text carries no qualifier such as "on some platforms", so any implementation that wants to can wake you at a moment when the condition does not hold.

What follows is a stand-in demonstration. Since I cannot produce a real spurious wakeup, a notification fired with the condition unchanged stands in, waking the waiting thread at a moment when the condition does not hold. Both demand the same thing of the code: on waking, the condition must be re-examined. Watch the first half: the waiting thread is woken by a notify sent from `main`, yet `main` changed nothing.

<OnlineCompilerDemo
  title="A Bare wait and a Predicate wait Each Catch a Hollow Notify"
  source-path="code/examples/vol5/chapter03/03_bare_wait_vs_pred.cpp"
  description="One notify with the condition unchanged: the bare-wait thread wakes, finds the condition false, and marches on; the predicate wait rechecks the condition on its own. Each reports the value of data_ready at the instant it wakes."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// bare_wait_vs_pred.cpp —— 虚假唤醒的替代演示：条件没变就发的通知，两种 wait 各接一次
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool data_ready = false;

int main() {
  {
    std::thread waiter([] {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk);  // 裸 wait，醒来不查条件
      std::printf("[bare] 醒来时 data_ready = %d\n", data_ready ? 1 : 0);
    });
    std::this_thread::sleep_for(50ms);
    cv.notify_one();  // 什么也没改，只发一次通知
    waiter.join();
  }

  data_ready = false;
  {
    std::thread waiter([] {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk, [] { return data_ready; });
      std::printf("[pred] 醒来时 data_ready = %d\n", data_ready ? 1 : 0);
    });
    std::this_thread::sleep_for(50ms);
    cv.notify_one();
    std::this_thread::sleep_for(50ms);
    std::printf("[main] 第一次通知之后，等待线程还睡着\n");
    {
      std::lock_guard<std::mutex> g(m);
      data_ready = true;
    }
    cv.notify_one();
    waiter.join();
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
[bare] 醒来时 data_ready = 0
[main] 第一次通知之后，等待线程还睡着
[pred] 醒来时 data_ready = 1
```

The first half uses a bare `wait`. A notify that changed nothing wakes it, `data_ready` is still 0, and the code carries on regardless, taking "woken up" for "condition holds".

Now the second half, with the predicate form of `wait`. Another notify arrives; it wakes, checks once, finds the condition false, and goes back to sleep — it proceeds only once the condition has truly changed and the second notify arrives.

The standard defines the predicate form as `while (!pred()) wait(lock);` ([thread.condition.condvar]/13). It rechecks the condition for you on every wake and sleeps again if it fails. A spurious wakeup landing in the middle costs nothing more than one extra check.

So the only `wait` you should write is the one with a predicate. If you insist on the bare form, you must wrap your own `while` around it and guarantee that every wakeup path performs the check. Leaving the recheck to the library is steadier than leaving it to every call site.

## Lost Wakeups: The Notify That Fires Before the Wait

The reverse situation is harder to hunt down: the notify is real, but nobody is waiting when it arrives. Let's move the notify ahead of the wait and see what happens.

<OnlineCompilerDemo
  title="A Lost Wakeup in the Act: The Notify Fired Before the Wait"
  source-path="code/examples/vol5/chapter03/04_lost_wakeup.cpp"
  description="The notify arrives before the wait and simply evaporates; worker sleeps on forever. This demo deliberately never exits: after you hit run, the runner's time limit ends it — it will not stop on its own, and that is expected."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// lost_wakeup.cpp —— 丢失唤醒现场：通知发在等待之前，worker 再也等不到叫醒
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool ready = false;

int main() {
  std::thread worker([] {
    std::this_thread::sleep_for(200ms);   // 起来得比通知晚
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk);                          // 裸 wait，醒来不查条件
    std::printf("worker: 醒来了，ready = %d\n", ready ? 1 : 0);
  });

  std::this_thread::sleep_for(50ms);
  {
    std::lock_guard<std::mutex> g(m);
    ready = true;
  }
  cv.notify_one();                        // 这一刻 cv 上还没有人
  std::printf("main: 通知发完了\n");
  std::fflush(stdout);

  worker.join();
  std::printf("main: 走到底了\n");
  return 0;
}
```

</OnlineCompilerDemo>

We let `worker` sleep a full 200 ms before getting up, while `main` changes `ready` and fires the notify at the 50 ms mark — the notify beats `worker`'s wake-up by 150 ms. Having fired it, `main` calls `join` and waits.

```text
$ timeout 2 ./lost
main: 通知发完了
$ echo $?
124
```

What you see is this: "notification sent" prints, `worker`'s line never appears, and the process is killed by `timeout`. `worker` went to sleep inside `cv.wait(lk)`, and nobody ever comes to wake it.

Notifications do not queue up. The standard states the effect of `notify_one` bluntly: if any threads are waiting, unblock one of them ([thread.condition.condvar]/5). If no thread is waiting, it does nothing and leaves no record — the notify is spent then and there.

The fix is the predicate from the previous section, and you change exactly one line of the program:

<OnlineCompilerDemo
  title="Fixing the Lost Wakeup: A One-Line Predicate wait"
  source-path="code/examples/vol5/chapter03/05_lost_wakeup_fixed.cpp"
  description="Differs from the previous program by one line: the bare wait becomes a predicate wait, the condition is checked before sleeping, a missed notify is no longer fatal, and worker finishes its work and exits as usual."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// lost_wakeup_fixed.cpp —— 丢失唤醒的修法：整个程序只改一行，裸 wait 换成带谓词的 wait
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool ready = false;

int main() {
  std::thread worker([] {
    std::this_thread::sleep_for(200ms);   // 起来得比通知晚
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [] { return ready; });    // 谓词在 wait 之前就要算一遍
    std::printf("worker: 醒来了，ready = %d\n", ready ? 1 : 0);
  });

  std::this_thread::sleep_for(50ms);
  {
    std::lock_guard<std::mutex> g(m);
    ready = true;
  }
  cv.notify_one();                        // 这一刻 cv 上还没有人
  std::printf("main: 通知发完了\n");
  std::fflush(stdout);

  worker.join();
  std::printf("main: 走到底了\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
main: 通知发完了
worker: 醒来了，ready = 1
main: 走到底了
```

Look back at what happened: `worker` did not wait in vain. Before entering `wait`, the predicate was evaluated once; `ready` was already true, so it never needed to sleep at all. Changes that already happened, the predicate remembers on its behalf.

Predicate and lock must be used as a pair; drop either one and things break. Suppose the side that changes `ready` does not take `m` — a crack stays open in the code: the waiter checks, finds false, and before it can settle down to sleep, the changer has already set `ready` to true and fired the notify. The waiter then sleeps, and the notify is lost all over again. `wait`'s "release the lock and enter the wait" is atomic, but its atomicity binds only against notifiers that take the same lock.

## Reference Resources

While writing this article I followed the progression of Anthony Williams's book, *C++ Concurrency in Action*; its chapter on condition variables tracks the arrangement here most closely. The examples and the prose are my own; the spinning measurements and the lost-wakeup demonstration were run on my machine with GCC 16.2.1. Every behavioral claim was checked against the current working draft on eel.is and against cppreference; I fetched the cppreference pages twice and both times got only the navigation shell with the body cut off, so clause citations follow the draft throughout, with versions as labeled there.

- [std::condition_variable (cppreference)][cppref-cv], since C++11.
- [General (current draft, thread.condition.general)][draft-general], the three atomic steps of `wait`: release the lock and enter the wait.
- [Class condition_variable (current draft, thread.condition.condvar)][draft-condvar], the original `or spuriously` wording, the predicate form's equivalent expansion, and the effect of `notify_one`.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression of the condition-variable chapter.

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[draft-general]: https://eel.is/c++draft/thread.condition.general
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
