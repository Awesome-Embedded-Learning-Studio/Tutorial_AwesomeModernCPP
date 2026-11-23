---
title: "Same Counter, Two Threads — Why Does It Compute Wrong?"
description: "Open with a small program where two threads compute the same counter wrong, distinguish data race from race condition, explain why accidental success cannot serve as evidence of correctness, then lay out deadlock, starvation, and priority inversion as a panorama of the problems."
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
prev:
  text: "Letting Work End in an Orderly Way"
  link: /vol5-concurrency/chapter01-start-concurrency/06-jthread-and-stopping
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter02-sharing-data/01-from-wrong-programs.md
  source_hash: 63304da49fe2c04292c2a476be5e148ef68e2bd4d57fedca435eba8a186a9294
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 14834
---
# Same Counter, Two Threads — Why Does It Compute Wrong?

From here on, we will not build anything flashy, brothers — just one thing: please run, with your own hands, a program that computes wrong, and then sort the "errors" we discover into categories.

## Firsthand Experiments: Lost Counts and Occasional Failures

Let's start from the plainest mistake. Two threads each increment a global variable 160000 times; when both finish, the main thread reads the total, expecting 320000.

<OnlineCompilerDemo
  title="Two threads each increment 160000 times — how many get lost in 30 trials"
  source-path="code/examples/vol5/chapter02/01_demo_lost_updates.cpp"
  description="Expecting 320000, a few of the 30 trials always miss, with losses seen from two hundred-odd to over a hundred thousand. A demonstration of data race undefined behavior: change the optimization level or the machine, and the readings change."
  run-options="-O0 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_lost_updates.cpp —— 两条线程各加 160000 次，30 轮里丢多少
#include <cstdio>
#include <thread>

long long counter = 0;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) ++counter;
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  for (int t = 0; t < kTrials; ++t) {
    counter = 0;
    std::thread a(work, kRounds);
    std::thread b(work, kRounds);
    a.join();
    b.join();
    if (counter == 2LL * kRounds) {
      ++correct;
    } else {
      std::printf("trial %2d: printed %lld, lost %lld\n",
                  t, counter, 2LL * kRounds - counter);
    }
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
```

</OnlineCompilerDemo>

OK, a principle gets established right here: one run's result says nothing. We run many times and observe the distribution of the outputs.

For instance, have the program run 30 trials, zeroing `counter` each round and starting the two threads afresh. Compiled at `-O0`, ours came out like this:

```text
trial  0: printed 161267, lost 158733
trial  4: printed 315296, lost 4704
trial  5: printed 313422, lost 6578
trial 13: printed 319718, lost 282
trial 14: printed 315173, lost 4827
trial 29: printed 299905, lost 20095
24 of 30 trials printed the expected 320000
```

Of 30 trials, 24 right, 6 wrong. In those wrong 6, we have seen losses ranging from two hundred-odd to over a hundred thousand. How random! Run it on another machine and the results differ completely again — what a black box!

Why do counts get lost? `++counter` is not one step on the machine. The mouth says "add one"; the machine does three things: read `counter` from memory into a register, add one to the register, write the result back to memory. Under `-O0`, the loop body of `work(int)` spends one instruction each on the read, the add, and the write (as seen by turning the compiled binary back into assembly with `objdump -d`; this author shortened the address columns and comments, and dropped the few instructions for stack frames and loop control):

```text
mov    0x2e98(%rip),%rax        # counter
add    $0x1,%rax
mov    %rax,0x2e8d(%rip)        # counter
```

`%rax` is a general-purpose register; you can think of it as a small temporary cell. Each of the two threads executes its own read, add, write, and the steps can interleave freely. Watch one common bad ordering: thread a executes its read and gets 5; before it can write its result back, thread b executes the same read and also gets 5. Then each does its `add` to 6, and each writes back 6. Two increments, and the count rose by one — one increment vanished just like that.

How common is "both sides read the old value"? We look at the window. From the first `mov` reading out the old value to the last `mov` writing the new value back, the few steps in between are the window. The bigger the window, the more overlapping round trips, and the more lost increments. The trial that lost 158733 (trial 0) amounts to the two threads running the bulk of their loops entirely on top of each other. Under `-O0` this window opens once per loop iteration — 160000 iterations times two threads, and an overlap always comes.

This author compiled the same source at `-O2` for 30 trials; here are the results for you:

```text
30 of 30 trials printed the expected 320000
```

Not one wrong line. Look at the machine code this time: under `-O2`, `work(int)` changed its face (output of `objdump -d`; this author dropped the address columns, the symbol table, and the two instructions at the entry that test whether `rounds` is positive):

```text
mov    0x2c8d(%rip),%rdx        # counter
lea    -0x1(%rdi),%eax
lea    0x1(%rdx,%rax,1),%rax
mov    %rax,0x2c7e(%rip)        # counter
ret
```

Count them yourself. Setting aside the trailing `ret`, the first four lines are four instructions: a `mov` reads `counter` once, two `lea`s compute and add `rounds`, and a `mov` writes it back. `lea` is nominally for computing addresses; compilers often use it for a free addition on the way. `%rdi` holds the first incoming parameter, that is, `rounds`. The loop is gone: what used to be 160000 rounds of "read once, add one, write back" folded into this single read and write. The window's shape changed with it — no longer a small window opened per iteration, 160000 times, but one big window for the whole function, opened once.

By what right does the compiler rewrite like this? Two cases, kept apart: in race-free code this is an equivalent transformation — looping 160000 single increments and one addition of 160000 give the same result. In racy code, it turned "read 160000 times" into "read only once," and whatever the other thread wrote in between gets overwritten wholesale.

The standard permits the compiler to do this because, in the standard's eyes, the program we wrote is already **undefined behavior**. That is — er — it's not exactly "wrong," you see; it's that nobody guarantees this gray zone is correct. The standard says neither right nor wrong.

## data race: Undefined Behavior, As Specified

The data race — Chinese-language material usually calls it 数据竞争 ("data competition"). It has a formal definition, written into the standard when C++11 introduced the memory model; the clause number has shifted across draft versions and is now [intro.races]. The standard writes four things into the definition; let's check them one by one: two conflicting actions on the same piece of memory, the two actions potentially concurrent, at least one of them not atomic, and neither one happening-before the other.

Let's swap the four terms into plain speech one by one. Conflicting actions: one writes, the other reads or also writes — including beginning or ending an object's lifetime on the same storage. Potentially concurrent says the two actions are executed by different threads (signal handling adds one corner case this article doesn't touch). An atomic operation is an action done in one step, never letting another thread glimpse a half-finished product; the details belong to the articles on atomics and the memory model. **happens-before** is the relation the standard uses to derive "who is before whom" — join, mutexes, and atomics can all establish it.

Read one more sentence from the standard, the one about consequences: any such data race results in undefined behavior (paragraph 17 of [intro.races] in the current draft; there since C++11).

Undefined behavior is a word easily read too lightly. It does not mean "the result is random." It means the standard no longer makes any demand of this program, and the compiler may assume no other thread is touching this memory. The `-O2` machine code you counted instruction by instruction in the last section is exactly what the compiler produced under the assumption that nobody else is moving this memory.

A program computing wrong numbers is the phenomenon you observe: it has probabilities, a distribution, possibly different every time. Undefined behavior is different — it is the standard's verdict, fixed and unambiguous.

One more easily confused clause, [res.on.data.races], governs the standard library itself. In essence: standard library functions must not directly or indirectly access objects accessible by other threads, unless the objects come in through parameters (including `this`). The clause is your guarantee that `std::vector`, `std::string`, and `std::cout` won't secretly touch data you never handed them. It grants your code no exemption.

Brought down to your own code, the judgment is short — ask four things: are the two actions executed by different threads; is it the same piece of memory being touched (one of them a write, or beginning/ending an object's lifetime); is either one non-atomic; and is there a happens-before relation between the two. Match all four, and the program is already outside the standard's jurisdiction.

## race condition: An Ordering Problem at the Logic Level

This author built a program with not a single data race in it. The shared variable is a `std::atomic<int>` (since C++11); every read and every write is individually whole, and the standard will not judge it undefined behavior.

<OnlineCompilerDemo
  title="No data race, and the count still goes missing: read-modify-write on an atomic"
  source-path="code/examples/vol5/chapter02/02_demo_race_condition.cpp"
  description="Every read and write is individually atomic, but the read, add, write-back trio can still be interrupted: expecting 200000, every run falls short. The standard does not judge it undefined behavior; the flaw is in the ordering, at the design level."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_race_condition.cpp —— 没有 data race，照样丢计数：原子上的读改写
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<int> counter{0};

void work() {
  for (int i = 0; i < 100000; ++i) {
    counter = counter.load() + 1;
  }
}

int main() {
  std::thread a(work);
  std::thread b(work);
  a.join();
  b.join();
  std::printf("%d\n", counter.load());
}
```

</OnlineCompilerDemo>

We ran it 8 times; the results:

```text
147112
164959
183999
178921
152737
140398
144015
125283
```

We expected 200000 and never once reached it. Every read and write is atomic — why still short? Because `counter = counter.load() + 1` is three actions: read once, add one, write back. Atomicity guarantees only that each action is individually whole, not that the three strung together go uninterrupted. Both threads read the old value, each adds one, each writes back — and one increment is lost. Exactly the same loss pattern as the non-atomic version before; the difference lies only in the verdict: here the standard offers no opinion, and every step of the program is defined.

race condition is not a term of the standard. You won't find it in the working draft, and it has no formal definition. It speaks of the design level: the program's correctness depends on the order in which two flows of execution interleave; change the order, and you get a different result.

The loss pattern has a ready-made name: **read-modify-write** — read once, compute once, write back, three steps used as one. It has a close cousin called **check-then-act**: check first, act after. See where the flaw sits: the check saw the old state, and by the time the action runs, the state has changed. The code above that dropped 200000 to 147112 was walking exactly the read-modify-write road.

Look into the real world and race conditions often hide deeper. E-commerce stock deduction: "checked, still in stock" and "deduct" run as two operations, and in between someone else buys one. The filesystem's "create if absent": two threads check together, and create together. Such programs may contain not one data race — all shared data atomic, perhaps no C++ objects shared at all — and still get the wrong result.

data race governs the language-level verdict: exact definition, fixed consequence — hit it, and it is undefined behavior. race condition governs the design-level flaw: the result depends on execution order, and the order is not arranged by you.

A program can be free of data races yet full of race conditions — the atomic counter program we ran above is one. The other way around, a program with a data race is necessarily unreliable, because the standard has already washed its hands of it.

## Accidental Success Is Not Evidence of Correctness

The `-O0`-compiled version above always had a batch of mismatches across 30 trials. Running one round, seeing 320000, and concluding the program is fine is the easiest misjudgment to make — change the scale and rerun, and the numbers change at once.

When the error surfaces is decided by several things: the iteration scale, your machine, the optimization level. This author took the same program and changed only the count, compiling all four tiers at `-O0`, 30 trials each: at 50000 per side, 30 of 30 correct; at 100000, 28 correct; at 160000, 24 correct (this author ran the 160000 tier several extra times, landing between 14 and 27); at 200000, only 2 remained. One variable only — the count. Switch computers, and cache, core count, and scheduling all differ, and the numbers shift along.

The optimization level works the same way. The same source at `-O2` came out 30 of 30. Had you compiled it `-O2` from the start, you might never see an error at all — you'd even think the code quite solid.

So how do you judge whether a piece of concurrent code is correct? Testing gives no affirmative answer; it can only falsify.

This author recompiled the same source with `-fsanitize=thread` (GCC's ThreadSanitizer, TSan from here on) (`-O1 -g`; without `-g` the report carries only addresses and function names, no file names or line numbers), ran it once, and what it reported was this:

```text
WARNING: ThreadSanitizer: data race (pid=8)
  Read of size 8 at 0x…… by thread T2:
    #0 work(int) 01_demo_lost_updates.cpp:8
  Previous write of size 8 at 0x…… by thread T1:
    #0 work(int) 01_demo_lost_updates.cpp:8
  Location is global 'counter' of size 8 at 0x……
```

Same source: the `-O2` binary was 30 of 30 correct, yet TSan still reported a data race — a race in the standard's sense. How the tool instruments and where its abilities end belong to the articles on debugging, testing, and performance; here this author borrows it to make one point: running right and being correct are two different things.

Then what do you lean on? On the standard's clauses, to see how it judges the shared access in front of you; on synchronization relationships, to see whether the reads and writes are joined by happens-before. Tools and tests are another road: look at what they reported and which runs they took.

This author's own practice is to ask, before writing code: who owns this data, who reads it, who writes it. That is the gate before writing a concurrency demo, not a way to sign off on a concurrency library. If you can't answer, don't expect tests to catch you. One test run counts as one sample, and with few samples it cannot even see the error.

## A Panorama of Race Problems: Deadlock, Starvation

The previous sections' experiments all landed on wrong results. Another class of problems exists: the program cannot move at all, or only some of its threads cannot. We lay out two common ones, each with a minimal sample: in deadlock, two threads each hold a lock while each waits for the lock in the other's hand. In starvation, the observed thread waits forever while the other threads keep getting the lock. Starvation is only recognized here; its fixes are not laid out in this article.

### Deadlock: Each Holds One, Waiting for the Other to Let Go

In plain words: we let two threads each take a lock, then each go demand the lock in the other's hand; neither lets go, so neither can move.

The sample uses `std::lock_guard`; all you need to know is that it takes the lock at construction and releases it at destruction. The details unfold in the next article on mutexes; here we only watch where they stop the threads.

<OnlineCompilerDemo
  title="Deadlock: each holds a lock, waiting for the other to let go"
  source-path="code/examples/vol5/chapter02/03_demo_deadlock.cpp"
  description="After printing the two [one]/[two] holds lines, the process stops moving (online running has a timeout that wraps up for you; locally you will need Ctrl-C). Once deadlock forms it is stable — every time, no probabilities."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_deadlock.cpp —— 各持一把锁，等对方松手：跑起来会停在两行
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m1, m2;

void route_one() {
  std::lock_guard<std::mutex> a(m1);
  std::puts("[one] holds m1");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> b(m2);
  std::puts("[one] got m2 as well");
}

void route_two() {
  std::lock_guard<std::mutex> b(m2);
  std::puts("[two] holds m2");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> a(m1);
  std::puts("[two] got m1 as well");
}

int main() {
  std::thread t1(route_one);
  std::thread t2(route_two);
  t1.join();
  t2.join();
  std::puts("both finished");
}
```

</OnlineCompilerDemo>

We got two lines, and then nothing more:

```text
[one] holds m1
[two] holds m2
```

The process stopped moving. This author ran it under `timeout 2`: after two seconds the process was killed, exit code 124, and the `both finished` line never printed. When you redirect output to a file or pipe it, the `stdout` buffer dies with the killed process; run it directly in a terminal, or add `stdbuf -oL`, to keep the two lines.

Once deadlock forms it is stable — every time, with no probabilities involved. After both sides take their first lock, whichever goes for the second meets the same result: it cannot have it. The two threads sit blocked on the kernel's wait queues while the CPU idles. In your process manager you see a process using almost no CPU that nonetheless never ends.

Fixed acquisition order belongs to the article on deadlock and reducing sharing; taking several locks in one go (with `std::scoped_lock`, say) belongs to the next article. Here you only need to recognize the shape: a thread holding one lock while waiting for another.

### Starvation: The System Advances, Someone Never Gets a Turn

Starvation is another matter. The whole system is moving forward; it is just that one particular thread never gets the resource. The sample uses a `std::atomic_flag` as a hand-made lock; the parenthesized arguments after `test_and_set` and `clear` are memory-order parameters. The details of atomic flags and memory order belong to the articles on atomics and the memory model — no obstacle to reading the numbers.

<OnlineCompilerDemo
  title="Starvation: the lock doesn't queue — fastest hand wins"
  source-path="code/examples/vol5/chapter02/05_demo_starvation.cpp"
  description="Eight threads grab the lock over a million times in 200 milliseconds while the observed thread gets only fourteen to fifty thousand turns, its worst single wait running several milliseconds. The standard requires only mutual exclusion of a mutex; it never promised a queue."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_starvation.cpp —— 锁不排队：八条线程抢 200 毫秒，观察者排不上
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::atomic_flag busy = ATOMIC_FLAG_INIT;

void lock() { while (busy.test_and_set(std::memory_order_acquire)) {} }
void unlock() { busy.clear(std::memory_order_release); }

int main() {
  std::atomic<long> hog_turns{0};
  std::atomic<long> victim_turns{0};
  std::atomic<bool> stop{false};

  std::vector<std::thread> hogs;
  for (int i = 0; i < 8; ++i) {
    hogs.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        lock();
        hog_turns.fetch_add(1, std::memory_order_relaxed);
        unlock();
      }
    });
  }
  std::this_thread::sleep_for(50ms);

  auto deadline = std::chrono::steady_clock::now() + 200ms;
  const long hog_before = hog_turns.load();   // 窗口开头的快照
  long worst_us = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    auto t0 = std::chrono::steady_clock::now();
    lock();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (us > worst_us) worst_us = us;
    victim_turns.fetch_add(1, std::memory_order_relaxed);
    unlock();
  }
  const long hog_during = hog_turns.load() - hog_before;

  stop.store(true);
  for (auto& t : hogs) t.join();
  std::printf("in 200 ms: 8 threads took the lock %ld times, the other %ld times\n",
              hog_during, victim_turns.load());
  std::printf("its longest single wait: %ld us\n", worst_us);
}
```

</OnlineCompilerDemo>

Three runs:

```text
in 200 ms: 8 threads took the lock 1397111 times, the other 34867 times
its longest single wait: 11133 us
in 200 ms: 8 threads took the lock 1338603 times, the other 14041 times
its longest single wait: 4880 us
in 200 ms: 8 threads took the lock 1422153 times, the other 49968 times
its longest single wait: 17082 us
```

The eight threads took the lock 1.34 to 1.42 million times in 200 milliseconds, while the thread we wanted to observe got only 14 to 50 thousand turns, its worst single wait 5 to 17 milliseconds. The critical section — the few lines between taking the lock and releasing it — holds only a few instructions; normal waiting is nanosecond-scale, and the machine is far from busy. The gap comes from the lock not queueing: fastest hand wins.

This author did not see total starvation this time, and that must be said clearly: the observed thread still got in over ten thousand times in 200 milliseconds. The sample offers readings; it does not prove that on some particular machine the thread would never get a turn at all.

Flip through the C++ standard and you will find it specifies only mutual-exclusion semantics; fairness (queue order) is not among the requirements, nor is implementing a queue forbidden (clause [thread.mutex.requirements]). So don't blame any particular lock for being badly made — the standard never promised a queue in the first place.

The difference between the two problems, laid side by side:

| Problem | What the threads are doing | The system as a whole |
| ---- | -------------------------- | -------------------- |
| Deadlock | Each holds one lock, blocked waiting for the other | Stopped |
| Starvation | Waiting forever while other threads keep grabbing it | Advancing, but this thread never gets a turn |

## Priority Inversion (Extended)

This section carries the (Extended) marker; skipping it does not affect the main line. But when you later meet real-time systems, or reports like "the high-priority task is stuck," this section helps you recognize what the wait relationships look like: a high-priority task stuck on a lock held by a low-priority task, which is itself being shouldered aside by medium-priority work.

Priority inversion needs three roles; this author calls them high, medium, and low by priority. The low-priority thread holds a lock and is working. The high-priority thread arrives, wants the same lock, and can only wait for the low one to release it. So far, still normal — the low one will release soon. The trouble arrives when a medium-priority thread wakes at this moment: it doesn't need that lock, but it outranks the low-priority thread, so it shoulders the low one aside and occupies the CPU. The result: the high-priority thread's wait stretches from "until the low-priority thread finishes its little remaining work" to "until the medium-priority thread finishes all of its work" — and how long medium's work runs has no upper bound.

That is the inversion. Comb through the wait relationships: the high-priority thread is actually waiting on a medium-priority thread, and the two of them share no data at all.

Search the whole standard library and you will find no interface for setting thread priority — `std::thread` carries no such member. To touch priority you must go through `native_handle()` to the platform interface, which swaps the standard library's thread object for the native platform's raw handle; A First `std::thread` Program's reference resources link the comparison page. So priority inversion is not a C++-standard matter; it belongs to scheduling policy.

The fix has a name: **priority inheritance**. While the high-priority thread waits for the lock, the holder's priority is temporarily raised to the waiter's level, so the medium-priority thread can no longer shoulder it aside; once the lock is released, the priority reverts. POSIX made it a mutex attribute — `pthread_mutexattr_setprotocol` with `PTHREAD_PRIO_INHERIT` — findable in your machine's `<pthread.h>`.

This author offers you a true story: in 1997 the Mars Pathfinder lander kept resetting itself; NASA Jet Propulsion Laboratory's (JPL) post-mortem attributed the cause to priority inversion, and it later became a textbook standard case.

This section has no experiment, and the reason is that for priority inheritance to truly take effect, the threads must run under a real-time scheduling policy (such as `SCHED_FIFO`), which requires privileges — this author has not run that here. What is described above is the structure.

## Next Steps

Unit 2 moves on to the concrete fixes. The lost counts from read-modify-write and the check-then-act compound operations get their fixes in the articles ahead. Starvation is only recognized here; the fixes are not pursued.

If you truly go hunting a fix for starvation, the direction is introducing fairness. A read-write lock can switch to writer priority so readers stop flooding in while the writer never gets a turn. A task queue can distribute round-robin, handing tasks out one by one in turn, or apply priority aging (the longer a task waits, the higher its priority climbs) so old tasks don't linger at the back. Locks themselves have fair variants like the ticket lock — take a number, then hold the lock; first come, first served. But this fairness is bought with throughput: the more conservative the scheduling, the slower the whole thing runs — keep the trade-off in mind. These topics are scattered through the optional reading on read-write locks and concurrent data structures, not expanded in the main text; if you want a look first, the cookbook's [Thread-Safe Container Strategies](../cookbook/b-topics/01-thread-safe-containers) is the place.

The next article covers mutexes and handles this article's undercounted counter: we gather shared access into locks, make explicit what each lock protects, and then discuss lock granularity. The practice of splitting check from act — "checked and there, gone at deduction" — gets its own article one further on. Preventing deadlock by lock ordering belongs to What If Nobody Lets Go; the trade-offs among independent ownership, passing results, and joint modification belong to Don't Share If You Don't Have To.

Before you turn the page, this author would like to ask one thing: run this article's 30-trial experiment on your own machine, both `-O0` and `-O2`, see how far apart the two sides land, then run the `-O0` build several more times.

## Reference Resources

This article's progression drew on Anthony Williams' C++ Concurrency in Action, taking the chapters on sharing data. The examples and prose are this author's own; the experiments were compiled and run on GCC 16.2.1. Claims about standard behavior were checked against the draft text on eel.is and cppreference per the clauses noted in the text.

- [Multi-threaded executions and data races (cppreference)][cppref-multithread] — the definition of a data race, happens-before, and synchronizing operations.
- [Current working draft: Data races (intro.races, eel.is)][draft-intro-races] — the original definition text, plus the sentence "any such data race results in undefined behavior"; paragraph numbers follow the current draft.
- [C++ working draft: Data race avoidance (res.on.data.races)][draft-lib-races] — the standard library implementation's obligations.
- [std::mutex (cppreference)][cppref-mutex] — the mutex requirements page ([thread.mutex.requirements]); it specifies only mutual-exclusion semantics, nothing about queue order.
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm][boehm] — why the standard chose undefined behavior over a defined semantics.
- [GCC instrumentation options (including `-fsanitize=thread`)][tsan] — the option documentation.
- [ThreadSanitizer (Google Sanitizers wiki)][tsan-wiki] — report format and detection principles.
- [Priority inversion — Wikipedia][wiki-priority] — the formulation of priority inversion and its common fixes.
- [What Really Happened on Mars — Glenn Reeves (JPL)][reeves] — the post-mortem of the Pathfinder incident.

[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[draft-intro-races]: https://eel.is/c++draft/intro.races
[draft-lib-races]: https://eel.is/c++draft/res.on.data.races
[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[boehm]: https://www.hboehm.info/c++mm/why_undef.html
[tsan]: https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
[tsan-wiki]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
[wiki-priority]: https://en.wikipedia.org/wiki/Priority_inversion
[reeves]: https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html
