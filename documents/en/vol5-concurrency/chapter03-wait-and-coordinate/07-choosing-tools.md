---
title: "Choosing Tools by What the Coordination Needs"
description: "The close of Unit 3: latch compresses waiting on a counted round into one line, barrier handles repeated lockstep with a completion function, a semaphore writes the concurrency cap as a single number — collected into a table mapping the four kinds of coordination to their homes, plus mutex-and-condition-variable simulations."
chapter: 5
order: 7
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
  source: documents/vol5-concurrency/chapter03-wait-and-coordinate/07-choosing-tools.md
  source_hash: fbb87cef482f574578ef70367cb91496f04f01b696a4acff2ba3dfeb915350d2
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 20393
---

# Choosing Tools by What the Coordination Needs

Six articles in, the basic parts are all in hand — yet some kinds of coordination come out roundabout when written as condition variables. Waiting at startup for three initialization jobs to finish takes a lock, a counter, a flag, and hand-protected notify timing. Repeated lockstep between phases means tracking each round's head count yourself and guarding against last round's undismissed waiters leaking into the next.

C++20 stocks three purpose-built tools for the waiting described above: `std::latch`, `std::barrier`, and semaphores; this article brings each on stage in turn. Each compresses one kind of coordination into a single call — at the price of a narrower expressive surface: compound conditions and stop requests, each tool has the ones it cannot take.

## std::latch: A One-Shot Countdown

`std::latch` lives in the `<latch>` header, provided since C++20. Inside it is nothing but a counter: the initial value is fixed at construction, `count_down()` decrements it by one notch, and the instant it reaches zero, every thread asleep in `wait()` is released together. Let's acknowledge one boundary up front: once the count reaches zero it can be neither added back to nor reset — the object is single-use ([thread.latch.general]/1).

Set it beside 3.1's condition variable and the difference lies in what is being waited on. A condition variable waits on a predicate, and predicates can be compound and can flip back and forth. A latch waits on the count reaching zero, and zero happens exactly once. The lost wakeup, the most stubborn troublemaker in 3.1, has nowhere to hide here: there, a notify ahead of the wait is a disaster, which is why the predicate must be evaluated once before `wait`. A latch has no notify at all — `wait()` only looks at the count — so `count_down()` running before `wait()` is harmless: the latter checks, finds zero, and returns on the spot ([thread.latch.class]/13).

Let's run one program playing both sides. Act one is startup initialization: three jobs go to three threads, and the main thread waits for all of them to be ready. Act two flips the order — the decrement ahead of the wait — to see whether `wait` sleeps at all:

<OnlineCompilerDemo
  title="latch: Three Initialization Jobs All In Before Moving"
  source-path="code/examples/vol5/chapter03/24_demo_latch.cpp"
  description="Three count_downs and one wait; the wait's cost lands on the slowest job. The tail adds the case of count_down running before wait: the count is already 0, and it returns on the spot."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_latch.cpp —— 一次性倒计时：三件初始化活，一个等待方
#include <chrono>
#include <cstdio>
#include <latch>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  std::latch ready(3);
  const char* names[3] = {"logger", "config", "network"};
  std::vector<std::thread> inits;
  for (int i = 0; i < 3; ++i) {
    inits.emplace_back([&ready, i, &names] {
      std::this_thread::sleep_for(50ms * (3 - i));   // 各自耗时 150/100/50 ms
      std::printf("[%s] 就绪\n", names[i]);
      std::fflush(stdout);
      ready.count_down();
    });
  }
  auto t0 = Clock::now();
  ready.wait();
  std::printf("main   : %5.1f ms 全部就绪，业务开始\n", ms_since(t0));
  for (auto& t : inits) t.join();

  std::latch fired(1);
  fired.count_down();   // 递减发生在等待之前
  auto t1 = Clock::now();
  fired.wait();         // 计数已经是 0，当场返回
  std::printf("main   : count_down 走在 wait 前面，wait 用时 %.3f ms\n",
              ms_since(t1));
  return 0;
}
```

</OnlineCompilerDemo>

```text
[network] 就绪
[config] 就绪
[logger] 就绪
main   : 150.2 ms 全部就绪，业务开始
main   : count_down 走在 wait 前面，wait 用时 0.000 ms
```

The three jobs are designed as 150, 100, and 50 ms; the scheduler arranges the ready lines — in this run network came first and logger closed. The main thread only moves once the slowest job, logger, is done, and the extra 0.2 ms in 150.2 is wakeup plus scheduling, the same order of magnitude as the latency measured in 3.1. For act two, move the camera over: `count_down()` is called before `wait()`. With a condition variable, that notify would fire with nobody waiting and evaporate, forcing the waiting side to rely on a predicate to remember what already happened. A latch needs not even a predicate — `wait` takes 0.000 ms, because what it waits on is the count, a state; the state is there, and whenever we come to check, it counts.

`count_down(update)` carries a precondition: update must not exceed the current count ([thread.latch.class]/7) — worth your attention. Write four initialization threads but construct the latch with 3, and the fourth `count_down` pushes the count negative: precondition violated, undefined behavior. The standard won't check for us, and the compiler won't stop it; making the number at construction match the number of threads calling `count_down` is a pass you make yourself.

For "finish my own job, then wait for everyone to arrive", use `arrive_and_wait()`, equivalent to `count_down(update)` followed by `wait()` ([thread.latch.class]/16). Where duties are separate — the workers don't wait, the aggregator only waits — call them separately as in the demo. If you're worrying with 3.2's lost wakeup in mind — "the decrement happens before the wait, could it be lost like a notify?" — it cannot be lost here: `wait()` is a state-based wait that checks the count on entry and releases immediately at zero. The count only goes down, and zero is an irreversible terminal state; even with decrement and wait as two separate steps, the state cannot be lost.

The latch's single-use nature comes from its position. Waiting for one counted round needs to happen once, so the interface is minimal — counting the member functions, we get five in all: count_down, try_wait, wait, arrive_and_wait, plus max for the ceiling. Hold it against barrier's member list and you'll notice latch has no member named just arrive; the notch-removing job is done by count_down alone. For repeated lockstep you could build a fresh latch every round, but with more than a few participants it turns messy — repeated lockstep belongs to the next section's barrier.

## std::barrier: Reusable Phase Synchronization

`std::barrier` lives in `<barrier>`, provided since C++20. Construction takes the participant count, a number called the expected count. A barrier's life is a series of phases — phase is the standard's term, one round of lockstep per phase ([thread.barrier.general]/1). Let's walk one phase's flow with the standard ([thread.barrier.class]/1). `arrive()` or `arrive_and_drop()` decrements this round's expected count by one notch — that's step one. After the count reaches zero, exactly once, one of the arriving threads executes the completion step within its own call — that's step two. The completion step does two things: run the completion function once, then release all waiting threads. The completion function is the one you gave at construction; each round, when everyone has arrived, it runs once — the round-counting lambda in the demo is exactly it. With the completion step done, the expected count resets to the constructed value and the next round begins — that's step three.

`arrive()` returns an arrival_token. If you want to check in, go do something else, and wait later, hand the token to `wait(token)`. Here the standard has a note we must heed: if nobody calls wait, whether the completion step executes is implementation-defined ([thread.barrier.class]/1.2) — arrive without waiting, and the completion function may never run. Most scenes don't need such fine control; `arrive_and_wait()` is equivalent to `wait(arrive())` ([thread.barrier.class]/23), one line and done.

We run four threads for three rounds, each round's work at a different pace, with the completion function counting rounds and reporting which thread it ran on. Back when 1.3 measured interleaved output we met a nuisance: several threads writing into `cout` can have a line spliced open mid-way by another. In demo_latch every line is a whole line from a single printf, with the worker fflush-ing right after, so it doesn't bite. Here one line is assembled from several segments, so we bring in C++20's `std::osyncstream`: it gathers one thread's output segments into a whole line before dropping them into `cout`, which is why the output below has every line intact:

<OnlineCompilerDemo
  title="barrier: Four Threads, Three Rounds of Lockstep"
  source-path="code/examples/vol5/chapter03/25_demo_barrier.cpp"
  description="Each round waits for the slowest arrival; the completion function counts rounds and reports which thread it ran on. Output lines assembled from several segments stay whole thanks to osyncstream."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_barrier.cpp —— 可重用的阶段同步：四条线程三轮齐步，完成函数汇轮数
#include <barrier>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  constexpr int kWorkers = 4;
  int round = 0;
  std::barrier sync(kWorkers, [&round]() noexcept {
    ++round;
    std::osyncstream(std::cout)
        << "barrier  : 第 " << round << " 轮全员到齐，完成函数在线程 "
        << std::this_thread::get_id() << " 身上执行\n";
  });

  std::vector<std::thread> workers;
  for (int i = 0; i < kWorkers; ++i) {
    workers.emplace_back([&, i] {
      std::osyncstream(std::cout)
          << "worker " << i << " : 线程 " << std::this_thread::get_id()
          << " 进场\n";
      for (int p = 1; p <= 3; ++p) {
        std::this_thread::sleep_for(20ms * (p + i));   // 各人快慢不同
        sync.arrive_and_wait();
      }
    });
  }
  for (auto& t : workers) t.join();
  std::printf("main     : 三轮走完\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
worker 0 : 线程 136740459247296 进场
worker 1 : 线程 136740450854592 进场
worker 2 : 线程 136740442461888 进场
worker 3 : 线程 136740434069184 进场
barrier  : 第 1 轮全员到齐，完成函数在线程 136740434069184 身上执行
barrier  : 第 2 轮全员到齐，完成函数在线程 136740434069184 身上执行
barrier  : 第 3 轮全员到齐，完成函数在线程 136740434069184 身上执行
main     : 三轮走完
```

Once the four entry lines print, the three rounds' record lines come out round by round; each round waits for the slowest arrival, releasing everyone only when all work is done. The completion function's thread id deserves a glance: all three rounds show 136740434069184 — match it against the entry lines and it is worker 3. The standard only says the completion step runs on one of the arriving threads, not which one ([thread.barrier.class]/1.2); this run landed on worker 3, another run may pick someone else, but the completion function always runs on some participating thread, never occupying an extra one. A latch can't do that — it has no centralized execution point; to tally each round you'd have to appoint a thread for the job.

This arrangement also yields a property we can lean on. While the completion function runs, every other participant is parked on the barrier, so it can read the data those threads just wrote without locking. The synchronization comes from the standard: arrive strongly happens before the current phase's completion step begins ([thread.barrier.class]/14), and the end of the completion step strongly happens before the released threads return ([thread.barrier.class]/3). Strongly happens before is a formal memory-model relation — Unit 5 covers it in full; in plain words here: anything written in the completion step is readable after release.

Two boundaries to yield to when writing code. The function object handed to the barrier must satisfy `is_nothrow_invocable_v<CompletionFunction&>` ([thread.barrier.class]/5) — plainly, it must promise the invocation won't throw; the `noexcept` on the demo lambda's tail does exactly that promising. Don't stuff genuinely-throwing finishing work into it; put that in the participants' own code. Also, don't touch the barrier itself from inside the completion function: for specializations with a non-default completion function, calling any member other than `wait` while the completion step is in progress is undefined behavior (the last sentence of [thread.barrier.class]/3).

When participation changes mid-flight, hand `arrive_and_drop()` to the departing thread: in the round it calls, the count still drops one notch as usual; from the next round on, the expected count is permanently one fewer ([thread.barrier.class]/25). Without it, a leaver has no way to excuse themselves from the barrier, and the rest wait every round for a seat that will never be filled again.

## std::counting_semaphore: Writing the Cap as a Number

Semaphores live in `<semaphore>`, provided since C++20. The template parameter of `std::counting_semaphore<LeastMaxValue>` gives an upper bound for the internal count; the standard only requires `max()` to be no smaller ([thread.sema.cnt]/4), and cppreference goes out of its way to note that LeastMaxValue is a "least maximum value" — `max()` may exceed it. `acquire()` decrements the count, sleeping when it is zero until someone's `release()` adds it back ([thread.sema.cnt]/1). For an "at most three inside at a time" requirement, just write the seat count into the constructor.

We let 8 tasks fight over 3 seats, measure the peak number inside along the way, and add one timeout observation. The demo writes `std::counting_semaphore<>` with empty angle brackets, using the implementation's default template argument — the standard marks it implementation-defined, and we don't depend on its concrete value, only on it being able to hold 3:

<OnlineCompilerDemo
  title="Counting Semaphore: 8 Tasks Fighting over 3 Seats"
  source-path="code/examples/vol5/chapter03/26_demo_semaphore.cpp"
  description="acquire sleeps when no seat is free; release gives the seat back. The measured peak of simultaneous occupancy stays within 3, and the tail adds a try_acquire_for on a count of 0, coming back empty-handed after 50 ms."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_semaphore.cpp —— 计数信号量：8 个任务抢 3 个名额，外加一次超时
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <semaphore>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

int main() {
  std::counting_semaphore<> slots(3);   // 3 个名额
  std::atomic<int> inside{0};
  std::atomic<int> peak{0};

  std::vector<std::thread> tasks;
  for (int i = 0; i < 8; ++i) {
    tasks.emplace_back([&, i] {
      slots.acquire();                  // 没名额就睡，等到 release 才回
      int now = ++inside;
      int old = peak.load();
      while (now > old && !peak.compare_exchange_weak(old, now)) {}
      std::this_thread::sleep_for(100ms);
      --inside;
      slots.release();
      std::osyncstream(std::cout)
          << "task " << i << " : 活干完，名额还回去\n";
    });
  }
  for (auto& t : tasks) t.join();
  std::printf("main: 8 个任务全做完，同时在场的峰值 %d（上限 3）\n", peak.load());

  std::counting_semaphore<> none(0);
  auto t0 = Clock::now();
  bool got = none.try_acquire_for(50ms);
  double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  std::printf("main: 计数 0 上等名额，try_acquire_for 返回 %d，耗时 %.1f ms\n",
              got ? 1 : 0, ms);
  return 0;
}
```

</OnlineCompilerDemo>

```text
task 2 : 活干完，名额还回去
task 0 : 活干完，名额还回去
task 1 : 活干完，名额还回去
task 4 : 活干完，名额还回去
task 3 : 活干完，名额还回去
task 5 : 活干完，名额还回去
task 7 : 活干完，名额还回去
task 6 : 活干完，名额还回去
main: 8 个任务全做完，同时在场的峰值 3（上限 3）
main: 计数 0 上等名额，try_acquire_for 返回 0，耗时 50.2 ms
```

The scheduler arranges the eight "gave it back" lines — which task enters nth changes every run — while the structural readings hold steady: all 8 tasks done, peak simultaneous occupancy 3; whatever the cap is, that's the peak. The few `compare_exchange_weak` lines we tucked inside are only the peak-measuring gauge; atomic-operation details belong to Unit 5, and skipping them costs you nothing of the protagonist.

The second segment is the timeout. On a semaphore whose count starts at 0, we call `try_acquire_for(50ms)`; it waits 50.2 ms and returns 0. The extra 0.2 ms, like 3.2's timeouts, comes from timer and scheduling granularity. What happens after `false` — retry or give up — must, per 3.6's standing advice, be one explicit line in the code; in the demo it gives up on the spot.

`try_acquire()` is the non-blocking version, returning `false` immediately when the count is zero. One thing to state: it is allowed to fail spuriously ([thread.sema.cnt]/13.3) — the count may plainly be positive and it still returns `false`; the standard's note says this leaves room for compare-and-exchange-based implementations, and cppreference files it alongside the condition variable's spurious wakeups. Taking a seat with bare `try_acquire` requires a loop, same reasoning as 3.1's bare `wait`. The `try_acquire_for` used in the demo internally calls `try_acquire` repeatedly ([thread.sema.cnt]/18), digesting spurious failures with one more attempt, none of it ours to worry about.

`release` has a precondition too: after the addition, the count must not cross `max()` ([thread.sema.cnt]/8). The boundary sits closest on binary_semaphore, whose LeastMaxValue is 1 while the standard only guarantees `max()` to be no smaller. On the implementation at hand, `max()` returns exactly the template argument — on binary, 1 — so holding 1 and then calling `release(2)` crosses the line on the spot.

### binary_semaphore: Used as a Signal

`std::binary_semaphore` is an alias of `std::counting_semaphore<1>`; cppreference keeps both names on one page. Squeezing the count to the two values 0 and 1 makes cramped quota usage — its more common role is as a signal: construct with 0, `release` is the delivering hand, `acquire` the receiving one.

<OnlineCompilerDemo
  title="binary_semaphore as a Signal: release Delivers, acquire Receives"
  source-path="code/examples/vol5/chapter03/27_demo_binary.cpp"
  description="sender writes payload then releases; whatever receiver reads after acquire is guaranteed to be 42: writes before release are visible after acquire, and no extra lock is needed beyond the signal."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_binary.cpp —— binary_semaphore 当信号用：release 一方交货，acquire 一方收
#include <cstdio>
#include <semaphore>
#include <thread>

int main() {
  int payload = 0;
  std::binary_semaphore delivered{0};

  std::thread sender([&] {
    payload = 42;          // release 之前写的值
    delivered.release();   // 交货，计数从 0 到 1
  });
  std::thread receiver([&] {
    delivered.acquire();   // 没货就睡，拿到信号才回
    std::printf("receiver: 读到 payload = %d\n", payload);
  });
  sender.join();
  receiver.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
receiver: 读到 payload = 42
```

The receiver reads 42, and the standard backs this ordering: the effects of release strongly happen before any `try_acquire` that observes it ([thread.sema.cnt]/10), and `acquire` internally calls `try_acquire` repeatedly ([thread.sema.cnt]/15); chain them together, and writes before release are visible to reads after acquire. Put the data-writing before `release` and the data-reading after `acquire`, and the data travels with the signal. The same scene on 3.1's condition variables would take a lock, a bool, and a notify — plus a recitation of the lost-wakeup defense.

The difference from a mutex deserves its own sentence. A mutex has ownership: whoever locks unlocks, and only the locking thread itself can release. A semaphore has no ownership — acquire and release may happen on different threads (cppreference states it bluntly) — and demo_binary stands precisely on that: release is called by sender, acquire by receiver. The price we also acknowledge: a semaphore cannot stop someone who never took a seat from giving one back; guarding data still falls to the mutex. It governs how many may enter at once, not what they do inside.

## The Ending-Path Checks, Walked Once More Here

Take 3.6's checklist over and aim it at the three new tools: what if the wait never ends, who receives the stop request, and how do people leave.

What if the wait never ends? Neither latch nor barrier has a timed wait. Among latch's members, the only non-blocking offering is the glance-taking `try_wait()` ([thread.latch.class]/12 — which is even allowed to return a spurious `false` with tiny probability); the name `wait_for` simply doesn't exist. Barrier likewise offers only blocking waits. If the count never reaches zero, or someone never arrives, the waiting side dead-waits: an initialization thread crashing before its `count_down` leaves the main thread asleep in `ready.wait()` with nobody coming to call it. A defense does exist: hand `count_down` to a small guard whose destructor always runs — the thread_guard idea from 1.5, *Who Is Responsible for the Thread's Lifecycle*, reused on a different object. Whether the work succeeded or not, the count still drops, and the waiter always reaches the end of its wait. As for whether the work succeeded, settle that with results elsewhere; don't expect the count to carry the news. The semaphore answers the timeout question better: `try_acquire_for` and `try_acquire_until` ([thread.sema.cnt]/18) give us the timeout — the demo's 50.2 ms reading is exactly it — with semantics aligned to 3.2's `wait_for`: at the deadline, return `false`.

Who receives the stop request? In 3.6, `condition_variable_any` had the `stop_token` overloads; of the three new tools, none has one. Where a wait must disband along with the shutdown flow, they can't receive it, and we must wire it up outside — for instance, the waiting side, on receiving the exit notice, stops entering the current synchronization round.

How do people leave? The exit was covered when we discussed `arrive_and_drop()`: the calling round still drops one notch, and from the next round the expected count is permanently one fewer. Skip it, and what you wait for is a seat that will never be filled again. Latch has no corresponding exit — its seats are fixed at construction — and if the side responsible for decrementing fails, the answer falls under the never-ending-wait question. Semaphore has no participant list at all; its leaving question goes by another name, "will anybody still call release", and the hands-on checklist below reserves a question for it. Barrier also has an accident all its own: a throwing completion function. The requirement is a non-throwing invocation ([thread.barrier.class]/5) — the requirement itself was checked at barrier's boundaries; what's added here is the consequence: what actually happens on a throw, the standard gives no destination. Finishing work that cannot be written `noexcept` should not be handed to it.

With the three questions answered, run them against 3.6's checklist once more: the spurious-wakeup row — the three tools' blocking waits are exempt by birth, since they wait on states like counts and phases, not notifications; on waking, the condition already holds, with no predicate to recompute. What needs care is the other end, the non-blocking probes: `try_wait` and `try_acquire` allow spurious failure, each covered above, and used as waits they need loops. The remaining rows have their answers in the text just written. Back to choosing: when the waiting side clearly has no exit requirement — startup-phase initialization, phase boundaries inside an algorithm — latch and barrier's lack of timeout fits exactly, narrowing the wait to a count reaching zero. When the wait hangs on unpredictable external events — the network, input, the user's patience — the condition variable with predicate, timeout, and stop-request support remains the right choice.

## Choosing by Scene: What Each Is Fit to Express

The four kinds of coordination each go home; here are the full criteria, and the table works standalone:

| Coordination need | Goes to | Criterion |
|---|---|---|
| Waiting for one counted round | `std::latch` | What's awaited is the count hitting zero; zero happens once; the decrementers and the waiters need not be the same people |
| Phase lockstep | `std::barrier` | A fixed group repeatedly arrives together then moves together, with one centralized handover per round (the completion function) |
| Resource quota | `std::counting_semaphore` | The cap goes into the constructor; acquire takes a seat, release gives it back; taking and giving may happen on different threads |
| Waiting on a condition | mutex plus condition variable | Predicates are free to write — compound, changeable — and timeout and stop requests have ready-made interfaces |

Beyond the table, the first criterion is compound conditions. Compound conditions are out of reach for the first three: latch and semaphore recognize only counts, barrier recognizes head counts, and a predicate like "queue not empty or closed" is beyond them. The queue from 3.3 onward is the ready example — wait on empty, wait on full, drain on close — three conditions meshed together, which is exactly why two condition variables were used. Rewriting that queue with two semaphores also works — one counting goods, one counting free slots — but the moment close semantics enter, the compound condition drain again needs a predicate, and we circle back to condition variables.

In choosing, I hold to one rule: use whichever row's semantics match the need. When semantics match, the questions accumulated in 3.1 and 3.6 reduce to matters inside the tool's boundary, with no extra defenses for you to build. Forced pairings — a latch impersonating repeated lockstep — leave you rebuilding the object and the participant list every round yourself, and the glue you add always comes due.

## No C++20? Simulating with Mutex and Condition Variables (Extension)

> This section is an extension; skipping it does not affect the main line. Projects parked on C++11 or C++17 can still wait out a counted round, lockstep repeatedly, and cap concurrency — this section simulates all three tools with mutex plus condition variable, and takes a peek at the machinery inside `std::barrier`. The three class bodies compile under C++11 alone. The driver's printing borrows C++20's osyncstream, and the duration literals are C++14. A C++11 project swaps the printing back to printf and the literals back to std::chrono::milliseconds, and it's complete.

LatchSim we write directly: the counter goes into the predicate, and zero triggers a notify. The latch proper has no notify; the simulation is a fusion of state plus notification — a lost notify is harmless because the predicate catches it, and 3.1's lost-wakeup defense carries over to the simulation unchanged.

BarrierSim is hard in the reuse. Look once at the reset-on-zero approach — reset the count when it hits zero — and rounds bleed into each other: waiters from the previous, unreleased round are watching the same count, and the new round stirs them in. The solution is a generation count: every waiter, inside the lock, records which generation it belongs to, and the waiting condition reads "has the generation changed". `std::barrier`'s phase runs on exactly the same machinery.

CountingSemaphoreSim stands up in a few lines: the predicate is just `counter_ > 0`; acquire waits inside the lock and then decrements; release adds back outside the lock and calls.

The three classes plus a driver that acceptance-tests each simulation — the whole listing is below; assemble it into a single file locally and you can re-run it:

<OnlineCompilerDemo
  title="mutex + Condition Variables Simulating latch, barrier, semaphore"
  source-path="code/examples/vol5/chapter03/28_demo_simulation.cpp"
  description="A plain version of each tool, acceptance-run: a countdown that releases at zero, a barrier that rotates by generation, and 8 tasks over 3 seats. The simulators exist to make the semantics visible; the text uses them to contrast with the library tools."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_simulation.cpp —— mutex + 条件变量模拟三个工具，各跑一遍验收
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

class LatchSim {
public:
  explicit LatchSim(std::ptrdiff_t count) : count_(count) {}
  void count_down(std::ptrdiff_t update = 1) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      count_ -= update;
    }
    if (count_ <= 0) cv_.notify_all();   // 归零才叫，锁外叫醒
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return count_ <= 0; });
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t count_;
};

class BarrierSim {
public:
  explicit BarrierSim(std::ptrdiff_t expected)
      : initial_(expected), count_(expected) {}
  void arrive_and_wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    std::size_t gen = generation_;       // 锁内记住自己属于哪一代
    if (--count_ == 0) {
      ++generation_;
      count_ = initial_;
      lock.unlock();
      cv_.notify_all();
    } else {
      cv_.wait(lock, [&] { return gen != generation_; });
    }
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t initial_;
  std::ptrdiff_t count_;
  std::size_t generation_ = 0;
};

class CountingSemaphoreSim {
public:
  explicit CountingSemaphoreSim(std::ptrdiff_t desired) : counter_(desired) {}
  void acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return counter_ > 0; });
    --counter_;
  }
  void release(std::ptrdiff_t update = 1) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      counter_ += update;
    }
    cv_.notify_all();
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t counter_;
};

int main() {
  {   // LatchSim：三件初始化活，一个等待方
    LatchSim ready(3);
    std::vector<std::thread> inits;
    for (int i = 0; i < 3; ++i) {
      inits.emplace_back([&ready, i] {
        std::this_thread::sleep_for(10ms * (3 - i));
        std::osyncstream(std::cout) << "[latch sim] 活 " << i << " 完成\n";
        ready.count_down();
      });
    }
    ready.wait();
    std::printf("[latch sim] 三件活齐了，往下走\n");
    for (auto& t : inits) t.join();
  }

  {   // BarrierSim：两条线程两轮齐步
    BarrierSim sync(2);
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) {
      workers.emplace_back([&sync, i] {
        for (int p = 1; p <= 2; ++p) {
          std::this_thread::sleep_for(10ms * (p + i));
          sync.arrive_and_wait();
          std::osyncstream(std::cout)
              << "[barrier sim] worker " << i << " 第 " << p << " 轮过线\n";
        }
      });
    }
    for (auto& t : workers) t.join();
    std::printf("[barrier sim] 两轮走完\n");
  }

  {   // CountingSemaphoreSim：8 抢 3，量峰值
    CountingSemaphoreSim slots(3);
    std::atomic<int> inside{0};
    std::atomic<int> peak{0};
    std::vector<std::thread> tasks;
    for (int i = 0; i < 8; ++i) {
      tasks.emplace_back([&] {
        slots.acquire();
        int now = ++inside;
        int old = peak.load();
        while (now > old && !peak.compare_exchange_weak(old, now)) {}
        std::this_thread::sleep_for(10ms);
        --inside;
        slots.release();
      });
    }
    for (auto& t : tasks) t.join();
    std::printf("[semaphore sim] 8 个任务做完，峰值 %d（上限 3）\n", peak.load());
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
[latch sim] 活 2 完成
[latch sim] 活 1 完成
[latch sim] 活 0 完成
[latch sim] 三件活齐了，往下走
[barrier sim] worker 1 第 1 轮过线
[barrier sim] worker 0 第 1 轮过线
[barrier sim] worker 1 第 2 轮过线
[barrier sim] worker 0 第 2 轮过线
[barrier sim] 两轮走完
[semaphore sim] 8 个任务做完，峰值 3（上限 3）
```

All three acceptance segments ran in one go. The latch segment moves on only when all three jobs are in. The barrier segment's crossing lines come in pairs per round, order within a pair up to the scheduler. The semaphore segment peaks at 3 — the simulation's count holds the cap. The simulations run us about a hundred and thirty lines; the extra over the three real tools is exactly the locks, predicates, and generation management that C++20 handed to the library.

## Your Turn to Try

Before writing a piece of coordination, I walk a few questions through:

1. Is what you're waiting for a one-time event, or a recurring beat? The answer forks between latch and barrier.
2. Does the waiting side need an ending? For timeout and stop, which line does each answer land on? Latch and barrier can't take them, so your surroundings need other arrangements.
3. Is the participant set the same from construction to the end? If someone leaves mid-way, which line do they exit through?
4. Using a semaphore as quota: does every path pair its acquire with a release? If an exception flies out, did the seat come back?
5. How many lines would the condition-variable version take? What do the extra lines buy — compound conditions, timeout, or stop requests?

## Next Steps

Condition variables, the capacity-bounded queue, ending paths, three coordination tools — the seven articles string one thread: what to wait for, who does the calling, what to do when it never comes.

## Reference Resources

The progression follows Anthony Williams's *C++ Concurrency in Action*; the examples and prose are my own. The peak, the timeout's return value and duration, and the binary reading are structural results, identical on every run. This time cppreference served the full text of the latch, barrier, and counting_semaphore pages, matching the draft with no discrepancies; LeastMaxValue is marked implementation-defined, and the default type of barrier's completion function is written only as unspecified in the draft and see below on cppreference — this article depends on neither concrete value.

- [std::latch (cppreference)][cppref-latch], since C++20, single-use, no timed wait.
- [std::barrier (cppreference)][cppref-barrier], since C++20, the phase model and the completion function.
- [std::counting_semaphore and std::binary_semaphore (cppreference)][cppref-semaphore], binary_semaphore is a specializing alias of counting_semaphore (LeastMaxValue of 1); try_acquire may fail spuriously; acquire and release may sit on different threads.
- [Class latch (current draft, thread.latch)][draft-latch], the single-use definition, count_down's precondition, wait's state check, arrive_and_wait's equivalent expansion, and try_wait's spurious false.
- [Class template barrier (current draft, thread.barrier)][draft-barrier], the three steps of a phase, the completion step's synchronization relations, the is_nothrow_invocable_v requirement, undefined behavior for calling other members mid-completion-step, and arrive_and_drop's participant adjustment.
- [Class template counting_semaphore (current draft, thread.sema.cnt)][draft-sema], the effects and preconditions of acquire and release, try_acquire's spurious failure, try_acquire_for's timeout semantics, and release's strongly-happens-before relation to observers.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[cppref-barrier]: https://en.cppreference.com/w/cpp/thread/barrier
[cppref-semaphore]: https://en.cppreference.com/w/cpp/thread/counting_semaphore
[draft-latch]: https://eel.is/c++draft/thread.latch
[draft-barrier]: https://eel.is/c++draft/thread.barrier
[draft-sema]: https://eel.is/c++draft/thread.sema.cnt
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
