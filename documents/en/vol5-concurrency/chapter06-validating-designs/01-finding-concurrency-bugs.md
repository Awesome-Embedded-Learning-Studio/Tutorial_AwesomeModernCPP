---
title: "Catching the Races: Repeated Testing and TSan"
description: "Repeated testing gives you round counts and hit rates, ThreadSanitizer reports the races that actually execute, and Thread Safety Analysis moves the checking into compile time."
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
  source: documents/vol5-concurrency/chapter06-validating-designs/01-finding-concurrency-bugs.md
  source_hash: aa68442f69c15b67d05b4ba6c6d80ee8c790748865062f19708c54f1d321a2bb
  translated_at: '2026-11-22T13:34:30+00:00'
  engine: glm-5.3
  token_count: 12035
---

# Catching the Races: Repeated Testing and TSan

## Repeated Testing: Turning the Sporadic into a Reading

In a single-threaded program, one failure is enough to assert that something is wrong. Concurrent programs invert that: one all-green run proves nothing, because the scheduler hands you only one sample out of the vast space of interleavings. Section 2.1 already produced this reading ([One Counter, Two Threads: Why Does the Count Come Out Wrong?](../chapter02-sharing-data/01-from-wrong-programs)): an unlocked counter compiled at `-O2` came out correct for 30 straight rounds, yet the same source checked with TSan still reported a data race. Thirty all-green rounds, and the race is still there. Accidental success is not evidence of correctness — we turn that principle into readings you can touch, with experiments.

Let's run a finer-grained experiment. Two threads each increment a plain `int` once, so after one round `counter` should equal exactly 2. The tight variant squeezes the read and write together; the wide variant spreads the actions apart: read into a local variable, sleep 50 microseconds, write back. Different window widths mean different hit probabilities — the readings are below.

<OnlineCompilerDemo
  title="Repeated stress test: the tight variant can run 30000 rounds without a loss, the wide variant loses almost every round"
  source-path="code/examples/vol5/chapter06/01_stress.cpp"
  description="Demo purposes only; it contains a data race: two threads read and write the same plain int. The tight variant may go thirty thousand rounds without losing one, while the wide variant (read, sleep 50 microseconds, write) loses an increment in almost every one of three thousand rounds. The program takes a round-count argument; running it on-site only prints the usage line — run it locally with the commands below."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int counter = 0;   // 故意不用 std::atomic，本实验就是要它出错

static void bump_once(bool wide) {
  if (wide) {
    int local = counter;                                   // 读
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    counter = local + 1;                                   // 写
  } else {
    ++counter;                                             // 读写挤在一条指令附近
  }
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: %s rounds [wide]\n", argv[0]); return 1; }
  const int rounds = std::atoi(argv[1]);
  const bool wide = argc > 2;
  int bad = 0;
  for (int r = 0; r < rounds; ++r) {
    counter = 0;
    std::thread a([&] { bump_once(wide); });
    std::thread b([&] { bump_once(wide); });
    a.join();
    b.join();
    if (counter != 2) ++bad;
  }
  std::printf("%s: %d of %d rounds lost an increment\n",
              wide ? "wide" : "tight", bad, rounds);
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread stress.cpp -o stress   # 普通版配方
./stress 30000          # tight 档，读写挤在一起
./stress 3000 wide      # wide 档，读、睡 50 微秒、写
```

```text
tight: 0 of 30000 rounds lost an increment
tight: 0 of 30000 rounds lost an increment
tight: 0 of 30000 rounds lost an increment

wide: 2979 of 3000 rounds lost an increment
wide: 2993 of 3000 rounds lost an increment
```

Once the wide-variant readings came out, I felt confident: stretch the window open, and the sporadic failure really does surface. The two runs lost 2979 and 2993 rounds respectively — a loss in nearly every round. I ran the tight variant three times, and across thirty thousand rounds we never saw it once; spread over the 30 rounds of an ordinary test, the chance of catching it is roughly zero. But failing to catch it does not mean it is absent — in the TSan section we verify that again with the same kind of program.

How many rounds to run is a budget-and-observation call; no universal number exists. The cheaper each round, the more rounds you can afford. We use the window-stretched version to estimate the order of magnitude of the per-round hit rate, then work backwards to how many rounds the ordinary version would need before a failure might show up once. Record the result of every round — which round, how much was lost — so that when a failure does arrive, we can trace our way back. Continuous integration can host a looping test case: the same program run hundreds or thousands of rounds in a row, and a single failed round fails the build.

Stress-test design itself deserves care. Here is a list of things I have done:

1. Vary the thread count: 2, 4, all the cores — each hands the scheduler a different space of interleavings.
2. Try stretching the window: pad a delay between read and write the way the wide variant does, and the sporadic becomes routine immediately.
3. Varying the load is worthwhile too; idle and fully loaded machines give noticeably different readings.
4. Let the verdict land on invariants: totals conserved, no duplicates, no omissions — right or wrong is settled on the spot.
5. Record reproduction conditions verbatim: thread count, iteration count, delay amount, machine and load — so the failure can be invited back next time.

## ThreadSanitizer: Reporting the Races That Actually Execute

TSan's mechanism in one sentence: at compile time it instruments every memory access, and at run time the runtime library records the thread and the synchronization events for each access. When two accesses land on the same address, it counts happens-before paths between them; when no path can be counted and one side is a write, the report fires. The judging language is the same one as in Section 5.3, so the reports should read familiar. Both GCC and Clang ship it; the option is `-fsanitize=thread`, and we first met it in Section 2.1 ([One Counter, Two Threads: Why Does the Count Come Out Wrong?](../chapter02-sharing-data/01-from-wrong-programs)).

Our experimental program arranges four threads, each incrementing a plain `long long` one hundred thousand times:

<OnlineCompilerDemo
  title="The count comes out fully correct, and ThreadSanitizer still reports a data race"
  source-path="code/examples/vol5/chapter06/02_tsan_race.cpp"
  description="Demo purposes only; it contains a data race: four threads each increment the same plain long long one hundred thousand times. Standard output shows counter=400000, fully correct, while the race report goes to standard error. To reproduce locally, compile with the TSan recipe below and the report appears."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <cstdio>
#include <thread>

long long counter = 0;

void work() {
  for (int i = 0; i < 100000; ++i) ++counter;
}

int main() {
  std::thread t[4];
  for (auto& x : t) x = std::thread(work);
  for (auto& x : t) x.join();
  std::printf("counter=%lld (expected 400000)\n", counter);
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O1 -g -fsanitize=thread -pthread tsan_race.cpp -o tsan_race   # 插桩版配方
./tsan_race
```

A word on the compile options: `-g` brings line numbers into the report, `-O1` is the starting optimization level the manual recommends, and `-pthread` handles linking the threading library.

Look at standard output first; the program reports one line itself:

```text
counter=400000 (expected 400000)
```

The count is fully correct — I stared at it twice before believing it. But over on standard error, the report is lying there (I removed the frames internal to the standard library and a long string of BuildId, keeping the backbone):

```text
==================
WARNING: ThreadSanitizer: data race (pid=17)
  Read of size 8 at 0x56b396dfd098 by thread T2:
    #0 work() /tmp/concurrency-u6/tsan_race.cpp:7 (tsan_race+0x1236)

  Previous write of size 8 at 0x56b396dfd098 by thread T1:
    #0 work() /tmp/concurrency-u6/tsan_race.cpp:7 (tsan_race+0x1267)

  Location is global 'counter' of size 8 at 0x56b396dfd098

  Thread T2 (tid=20, running) created by main thread at:
    #0 pthread_create <null> (libtsan.so.2+0x616c9)
    #1 std::thread::_M_start_thread(...) <null> (libstdc++.so.6+0xea961)

  Thread T1 (tid=19, finished) created by main thread at:
    #0 pthread_create <null> (libtsan.so.2+0x616c9)
    #1 std::thread::_M_start_thread(...) <null> (libstdc++.so.6+0xea961)

SUMMARY: ThreadSanitizer: data race /tmp/concurrency-u6/tsan_race.cpp:7 in work()
==================
```

How to read the report, line by line. The first WARNING line classifies the event — data race, the source of undefined behavior we convicted in Section 2.1. The two stack sections in the middle are the conflicting parties: T2's read and T1's write, both landing on the `++counter` at line 7. Location tells you the address corresponds to the global variable `counter`, and the SUMMARY line gathers up the location and the function. The program's exit code is 66, the default value documented in the manual — continuous integration relies on it to flag the failure.

The count was fully correct, and the report came anyway. Instrumentation shifted the timing: the four threads happened to squeeze into a serial order and lost not a single update. But the race itself stands — TSan does not care how pretty the readings look. One more compilation discipline: `-fsanitize=thread` cannot be combined with `-fsanitize=address` (the GCC manual states plainly that they cannot be combined); build a separate binary for each detector and run them apart. ASan covers out-of-bounds and dangling accesses; we used it in Section 2.5 ([Boundaries Handed to the Caller](../chapter02-sharing-data/05-interface-leaks)), and the two test builds each do their own job.

## No TSan Report Does Not Mean No Bug

TSan only looks at the paths that actually execute, as we saw in Section 2.1: what it reports are races that truly happened during execution. In theory there is no room for missing a race that really occurred, but the engineering implementation caps the access history to control memory (`history_size` in `TSAN_OPTIONS`); in extreme scenarios the report may come without a complete call stack, and a race buried under the history cap may produce no report at all. So treat it as a strong signal, not as proof.

If a race hides in a branch your tests never reach, it stays silent.

Now the logic layer. What the tool recognizes is the memory-level data race; holes in logic it does not judge. Let's test it with an all-atomic check-then-act:

<OnlineCompilerDemo
  title="An all-atomic check-then-act: TSan reports nothing, the logic is still wrong"
  source-path="code/examples/vol5/chapter06/03_logical_race.cpp"
  description="Every step is an atomic operation, and TSan reruns with zero warnings. Yet dozens of rounds in ten thousand still end in double wins — the window between the check and the write is one that atomic operations cannot plug. To reproduce locally, add -fsanitize=thread."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<bool> go{false};
std::atomic<bool> reserved{false};
std::atomic<int> winners{0};

void contender() {
  while (!go.load(std::memory_order_relaxed)) {}
  if (!reserved.load(std::memory_order_relaxed)) {
    reserved.store(true, std::memory_order_relaxed);
    winners.fetch_add(1, std::memory_order_relaxed);
  }
}

int main() {
  const int kRounds = 10000;
  int double_wins = 0;
  for (int r = 0; r < kRounds; ++r) {
    reserved.store(false);
    winners.store(0);
    go.store(false);
    std::thread a(contender);
    std::thread b(contender);
    go.store(true);
    a.join();
    b.join();
    if (winners.load() == 2) ++double_wins;
  }
  std::printf("double wins: %d of %d rounds\n", double_wins, kRounds);
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O1 -g -fsanitize=thread -pthread logical_race.cpp -o logical_race   # 插桩版配方
./logical_race
```

Two threads wait for the same go signal, then each checks `reserved`, sets it, and counts — every step is an atomic operation, and at the memory level we cannot point to a single data race. We compiled with TSan and ran it three times; all three runs produced zero warnings:

```text
double wins: 9632 of 10000 rounds
double wins: 9666 of 10000 rounds
double wins: 9675 of 10000 rounds
```

Look at the readings: 9632 rounds out of ten thousand or more end in double wins — both contenders believe they grabbed the seat. The check and the write are two separate actions, and the window between them is one that atomic operations cannot plug. We met the same hole in Section 2.4 ([Where Does One Operation End?](../chapter02-sharing-data/04-check-then-act)). To plug it, you have to switch to a single-step decision such as `compare_exchange`; the CAS homework belongs to Section 5.1 ([How Far Do Atomic Operations Get You?](../chapter05-atomic-memory-model/01-what-atomics-solve)).

TSan recognizes all the standard primitives — mutex, atomic, and condition variables are old acquaintances of its. Spinlocks, event counts, and other synchronization we build by hand it does not recognize; we have to feed it the synchronization relationships through the annotation interface, and where we cannot feed them, false positives and false negatives both arrive. Third-party libraries without instrumentation deserve the same distrust — the Limitations section of the clang manual states it plainly: all of the code must be recompiled with `-fsanitize=thread`, otherwise there will be missed reports, and false reports likewise.

We owe you honesty on overhead too: the typical figures given in the clang manual are 5 to 15 times slower and 5 to 10 times more memory. Its place is in testing and continuous integration; we do not put it in production.

## A Compile-Time Line of Defense: Thread Safety Analysis

TSan speaks only when the error actually runs. One category of errors can be intercepted at compile time: touching data without taking the lock. Clang's Thread Safety Analysis is a Clang-native language extension — the official documentation states up front, it is a C++ language extension — an entirely compile-time static check with zero runtime overhead. You declare which lock governs a piece of data, and the compiler checks access function by function.

Let me show you the toolchain boundary on the spot. GCC 16.2.1's answer to `-Wthread-safety` is:

```text
g++: error: unrecognized command-line option ‘-Wthread-safety’
```

Compile the same code with g++ and every attribute reports `attribute directive ignored` — the check is effectively off. MSVC is outside what this article tested, so I will not speak for it. To use it, the compiler must be Clang (verified locally on clang 23.1.3); worth confirming before you wire it into the build.

There is one more small boundary in attribute spelling, which I tested on the local clang 23.1.3. Clang accepts the `[[clang::capability("mutex")]]` spelling. The `[[clang::guarded_by(mu_)]]` spelling it does not accept — the warning says unknown attribute ignored, verified 2026-11-17. You have to write them in the original `__attribute__` form, which this article's examples use throughout. Google's mutex.h wraps the attributes above into macros like `GUARDED_BY`; the macro spelling is what you usually meet in real projects, and what the macros expand to is exactly the original form, so you can cross-check against it.

Here is the minimal set of annotations from the example:

<OnlineCompilerDemo
  title="Thread Safety Analysis: forgetting the lock gets caught at compile time"
  source-path="code/examples/vol5/chapter06/04_tsa_demo.cpp"
  description="A clang extension: broken_read reads total_ without holding the lock and peek_locked is called without it; clang++ -Wthread-safety reports one warning for each (command below, line numbers line up). g++ does not recognize these attributes, so running on-site shows only the compile warnings about ignored attributes."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <mutex>

class __attribute__((capability("mutex"))) TicketMutex {
 public:
  void lock() __attribute__((acquire_capability())) { m_.lock(); }
  void unlock() __attribute__((release_capability())) { m_.unlock(); }
 private:
  std::mutex m_;
};

class __attribute__((scoped_lockable)) TicketLock {
 public:
  explicit TicketLock(TicketMutex& mu) __attribute__((acquire_capability(mu)))
      : mu_(&mu) { mu_->lock(); }
  ~TicketLock() __attribute__((release_capability())) { mu_->unlock(); }
  TicketLock(const TicketLock&) = delete;
  TicketLock& operator=(const TicketLock&) = delete;
 private:
  TicketMutex* mu_;
};

class Ledger {
 public:
  void add(int n) {
    TicketLock lk(mu_);
    total_ += n;
  }
  int broken_read() {           // 忘了拿锁，等编译器拦
    return total_;
  }
  int peek_locked() __attribute__((requires_capability(mu_))) {
    return total_;
  }
 private:
  TicketMutex mu_;
  int total_ __attribute__((guarded_by(mu_))) = 0;
};

int use_bad() {
  Ledger led;
  led.add(1);
  return led.broken_read() + led.peek_locked();
}

int main() {
  return use_bad();
}
```

</OnlineCompilerDemo>

```bash
# clang 23.1.3
clang++ -std=c++20 -Wthread-safety -fsyntax-only tsa_demo.cpp
```

The two warnings, verbatim:

```text
tsa_demo.cpp:29:12: warning: reading variable 'total_' requires holding mutex 'mu_' [-Wthread-safety-analysis]
   29 |     return total_;
      |            ^

tsa_demo.cpp:42:34: warning: calling function 'peek_locked' requires holding mutex 'led.mu_' exclusively [-Wthread-safety-analysis]
   42 |   return led.broken_read() + led.peek_locked();
      |                                  ^
```

`broken_read` reads `total_` without taking the lock, and the warning lands inside the function body. `peek_locked` declares `requires_capability` itself, so its internal access is legal and the warning moves to the call site: whoever calls it without holding the lock gets the warning. `add` goes through the annotated RAII — `TicketLock` takes the lock in its constructor and releases it in its destructor, clean and tidy. Without `-Wthread-safety`, the check does not run: the very same code says nothing at all — this too I verified locally.

We accept its boundaries just the same. The analysis checks only annotated places — undeclared data is ignored entirely, and if the declared lock does not match the real lock, it has no idea. So it protects against forgetting the lock, not against locking the wrong object. It and TSan are a complementary pair; keep them separate in your mind: one checks the discipline you declared, the other checks the facts that ran.

## Your Turn

Before you take this away, work through three questions:

1. How do you budget the round count? The cheaper each round, the more rounds you can afford; use the window-stretched version to estimate the order of magnitude of the hit rate, then work backwards to how many rounds the ordinary version needs.
2. Which parts of a TSan report do you read? The first line classifies, the two middle stacks are the conflicting parties, Location and SUMMARY give the positions, and exit code 66 is left for continuous integration to fail on.
3. What does Thread Safety Analysis protect against, and what does it not? Forgetting the lock, yes; locking the wrong object, no — and it only checks annotated places.

One hands-on exercise to leave with: build the stress program twice, run the ordinary version for 30000 rounds, and one run of the TSan version is enough. Put thirty thousand zero-hit rounds and one data race report side by side, and check the readings yourself.

## References

The overall approach drew on the testing-and-debugging chapters of Anthony Williams' C++ Concurrency in Action — what I borrowed is the organization and the way of explaining; the examples and the words are my own, and the experiments were compiled and verified on GCC 16.2.1 and clang 23.1.3. Claims about tool behavior were checked against the following manuals and documents (accessed 2026-11-17).

- [GCC instrumentation options (including `-fsanitize=thread`)][gcc-instrument] — the source for why the two detectors cannot be combined.
- [ThreadSanitizer (clang manual)][clang-tsan] — usage, overhead figures, and the default exit code 66.
- [Thread Safety Analysis (clang manual)][clang-tsa] — the full attribute table and the mutex.h macro spellings.
- [ThreadSanitizer C++ manual (Google sanitizers wiki)][tsan-wiki] — report format.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams] — the testing and debugging chapters.

[gcc-instrument]: https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
[clang-tsan]: https://clang.llvm.org/docs/ThreadSanitizer.html
[clang-tsa]: https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
[tsan-wiki]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
