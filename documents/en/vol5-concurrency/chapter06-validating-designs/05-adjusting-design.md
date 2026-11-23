---
title: "How to Adjust the Design"
description: "Measuring and fixing cache lines and false sharing, the reading shapes of lock contention, over-fine tasking, and over-sharing, plus the measure-first-change-later adjustment order — Unit 6 comes together here."
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter06-validating-designs/05-adjusting-design.md
  source_hash: 9879642f34ee2efe5cb3826665398df2046d51698725967bcb0277df005d5bb9
  translated_at: '2026-11-22T13:49:00+00:00'
  engine: glm-5.3
  token_count: 17470
---

# How to Adjust the Design

Multithreaded readings are all readings of single runs; I list the raw readings of several rounds as they came, without picking the best-looking one.

## Cache Lines: Where False Sharing Comes From

The CPU deals with memory not by the byte but by the line. Memory is carved into equal-length small blocks, each block one cache line — 64 bytes on common desktop and server processors. Read one byte in a line, and the whole line enters the cache together. Write one byte in a line, and if someone else writes the same line too, trouble follows. The line width varies by implementation. My machine's answer sits in the command below: 64. Since C++17 the standard also provides the constant `std::hardware_destructive_interference_size`, living in `<new>`, whose semantics are the minimum spacing needed to avoid false sharing (the current draft gives it the clause name `[hardware.interference]`); the value is implementation-defined: Apple's arm64 core line width is 128 bytes, and Clang adjusted the constant to 128 for Apple targets in July 2026 (the commit message is in the references; the original words are sysctl reports 128), while GCC gives 256 on AArch64, the conservative value from ARM's documentation. One constant, a different value per target — which is why it does not belong inside a binary interface shared across compilers.

```bash
cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size
```

```text
64
```

When Section 5.3, [How One Thread Observes Another Thread's Modifications](../chapter05-atomic-memory-model/03-happens-before), covered cache coherence, we met this temperament: to write a line, a core must acquire exclusive ownership, and copies held by other cores are invalidated on the spot. Two cores each writing their own variable — as long as the variables live on the same line, exclusive ownership keeps changing hands between the two cores, and neither gets to write in peace. The variables' logical relationship has not changed at all, yet the reading collapses.

Let's turn the mechanism into readings with an experiment. Two counters, each written by its own thread; nowhere in the source can you find a single share. The Packed version packs two `long long`s into one struct, their addresses only 8 bytes apart, landing on the same line. The Slot version wraps each counter in `alignas(64)`, the two addresses exactly 64 bytes apart — one line each. We also set up a serial comparison, one thread incrementing both counters in turn, so we know where single-core speed sits. The counters are declared `volatile long long` — do not misread this, it is not a synchronization tool; Section 5.5, [A Practice Ground for the Criteria: Stop Flags, Reference Counting, and DCLP](../chapter05-atomic-memory-model/05-memory-order-patterns), drew volatile's boundary. Here it blocks the compiler: without it, `-O2` folds twenty million increments into one addition, and line contention never gets a chance to happen. Along the way, one new change to disclose: writing `++` on a volatile object counts as deprecated since C++20, and GCC 16.2.1 reminded me on the spot:

```text
warning: ‘++’ expression of ‘volatile’-qualified type is deprecated [-Wvolatile]
```

So the experiment writes `v = v + 1`, splitting the read and the write apart, each counted on its own. For the gate we use two doors, `ready` and `go`: `ready` counts worker threads reporting in, `go` is pressed once by the main thread to let them go, and the stopwatch starts at the moment of release (why two doors — the adjustment-order section has a case study to confess).

<OnlineCompilerDemo
  title="False sharing measured: squeezing onto one line vs. one line each"
  source-path="code/examples/vol5/chapter06/16_exp_g_falsesharing.cpp"
  description="Two counters each written by its own thread: the Packed version's two addresses differ by 8 bytes, the Slot version by 64; serial, packed, and slot groups each list three rounds of raw readings (if this machine schedules the two threads onto the same core, the gap shrinks — mechanism in the body text)."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_g_falsesharing.cpp —— 各写各的两个计数器，挤一条行对一人一条行
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <new>
#include <thread>
#include <vector>

template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

constexpr long long kPerThread = 20000000LL;   // 每线程两千万次加一

struct alignas(64) Packed { volatile long long a = 0; volatile long long b = 0; };
struct alignas(64) Slot   { volatile long long v = 0; };

template <class F, class G>
double run_two(F&& body1, G&& body2) {
  using clock = std::chrono::steady_clock;
  std::latch ready{2};
  std::latch go{1};
  std::thread t1([&] { ready.count_down(); go.wait(); body1(); });
  std::thread t2([&] { ready.count_down(); go.wait(); body2(); });
  ready.wait();
  auto t0 = clock::now();
  go.count_down();
  t1.join();
  t2.join();
  auto t1c = clock::now();
  return std::chrono::duration<double, std::milli>(t1c - t0).count();
}

int main(int argc, char** argv) {
  std::printf("sizeof(Packed)=%zu sizeof(Slot)=%zu\n", sizeof(Packed), sizeof(Slot));
  Packed paddr{};
  std::array<Slot, 2> saddr{};
  std::printf("packed a->b delta=%lld  slot0->slot1 delta=%lld\n",
      (long long)((char*)&paddr.b - (char*)&paddr.a),
      (long long)((char*)&saddr[1].v - (char*)&saddr[0].v));
  bool do_serial = true, do_packed = true, do_slot = true;
  int rounds = 3;
  if (argc > 1) {   // 单发模式留给 perf c2c 一类的工具
    do_serial = do_packed = do_slot = false;
    if (argv[1][0] == 's' && argv[1][1] == 'e') do_serial = true;
    else if (argv[1][0] == 'p') do_packed = true;
    else if (argv[1][0] == 's') do_slot = true;
    if (argc > 2) rounds = std::atoi(argv[2]);
  }
  if (do_serial) {
    for (int r = 0; r < rounds; ++r) {
      Packed packed{};
      auto t0 = std::chrono::steady_clock::now();
      for (long long i = 0; i < kPerThread; ++i) packed.a = packed.a + 1;
      for (long long i = 0; i < kPerThread; ++i) packed.b = packed.b + 1;
      auto t1 = std::chrono::steady_clock::now();
      std::printf("serial round=%d %.2fms\n", r,
                  std::chrono::duration<double, std::milli>(t1 - t0).count());
      do_not_optimize(packed.a); do_not_optimize(packed.b);
    }
  }
  if (do_packed) {
    for (int r = 0; r < rounds; ++r) {
      Packed packed{};
      double ms = run_two([&] { for (long long i = 0; i < kPerThread; ++i) packed.a = packed.a + 1; },
                          [&] { for (long long i = 0; i < kPerThread; ++i) packed.b = packed.b + 1; });
      std::printf("packed round=%d %.2fms\n", r, ms);
      do_not_optimize(packed.a); do_not_optimize(packed.b);
    }
  }
  if (do_slot) {
    for (int r = 0; r < rounds; ++r) {
      std::array<Slot, 2> slots{};
      double ms = run_two([&] { for (long long i = 0; i < kPerThread; ++i) slots[0].v = slots[0].v + 1; },
                          [&] { for (long long i = 0; i < kPerThread; ++i) slots[1].v = slots[1].v + 1; });
      std::printf("slot   round=%d %.2fms\n", r, ms);
      do_not_optimize(slots[0].v); do_not_optimize(slots[1].v);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_g_falsesharing.cpp -o exp_g_falsesharing
./exp_g_falsesharing
```

```text
sizeof(Packed)=64 sizeof(Slot)=64
packed a->b delta=8  slot0->slot1 delta=64
serial round=0 7.62ms
serial round=1 7.63ms
serial round=2 7.69ms
packed round=0 53.84ms
packed round=1 58.62ms
packed round=2 49.16ms
slot   round=0 4.49ms
slot   round=1 3.72ms
slot   round=2 3.77ms
```

Look at the readings. Serial sits around 7.6 milliseconds per round — one core finished forty million increments. Packed's three rounds run 49 to 59 milliseconds, six or seven times slower than serial: two cores working slower than one, slapping each other in the face. Slot's three rounds sit at 3.7 to 4.5 milliseconds, almost exactly half of serial — the two cores are truly running in parallel. Packed versus slot: a gap of 12 to 16 times. Both versions occupy 64 bytes of memory; the only difference is how far apart the two counters sit.

Put the names side by side and they are easy to tell apart. Section 6.4's contended version was eight threads fighting over the same `std::atomic` — eight threads really writing the same variable: that is true sharing. This article's packed writes different variables; the sharing is false, but the cost is real — the name false sharing says exactly that the sharing is false. The two versions' reading shapes are the same: 6.4 measured 295 times the cycles with only 18% more instructions, and this article measures a dozen-plus times slower with not one more addition — the time all goes into the line changing hands back and forth.

The fix is plain: separate the concurrently written variables on the hot path — one line each. Padding can do the job too, but how many bytes to pad is yours to calculate. One more layer you should know: a hand-padded struct's own alignment requirement is still 8; where the line boundary falls is not up to us, who it neighbors inside a line is not up to us, the size still has to be hand-computed, and one added field throws the layout off. So we simply raise the alignment requirement to 64 with `alignas`, and the boundary lands steadily:

<OnlineCompilerDemo
  title="alignas(64) vs. hand-padded bytes: both sizes right, alignments different"
  source-path="code/examples/vol5/chapter06/17_slot_vs_padded.cpp"
  description="Slot and Padded both have sizeof 64, but alignof is 64 for one and 8 for the other: hand-padding reaches the size, yet where the line boundary falls is still not up to us."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// slot_vs_padded.cpp —— alignas(64) 与手工垫字节：尺寸都对，对齐不同
#include <cstdio>

struct alignas(64) Slot   { long long v = 0; };      // 一人一条行
struct Padded             { long long v = 0; char pad[56]; };   // 手工垫满，也能用

int main() {
  std::printf("sizeof(Slot)=%zu   alignof(Slot)=%zu\n", sizeof(Slot), alignof(Slot));
  std::printf("sizeof(Padded)=%zu alignof(Padded)=%zu\n", sizeof(Padded), alignof(Padded));
  return 0;
}
```

</OnlineCompilerDemo>

Do not hand-write 64 for the alignment value; use the standard constant. Both compilers on this machine give 64 (GCC 16.2.1 and clang 23.1.3, verified 2026-11-17, with the feature macro `__cpp_lib_hardware_interference_size` at 201703L). Clang initially did not implement it (LLVM issue 60174 records that history); it works now, I re-verified, and the change is honestly recorded here.

<OnlineCompilerDemo
  title="The local answer from the standard constant hardware_destructive_interference_size"
  source-path="code/examples/vol5/chapter06/18_interference_size.cpp"
  description="The minimum anti-false-sharing spacing since C++17; both local g++ and clang++ report 64. If an implementation does not provide the constant, it prints constant unavailable."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <cstddef>
#include <cstdio>
#include <new>

int main() {
#ifdef __cpp_lib_hardware_interference_size
  std::printf("%zu\n", std::hardware_destructive_interference_size);
#else
  std::printf("constant unavailable\n");
#endif
}
```

</OnlineCompilerDemo>

A word on the space cost too: `alignas(64)` lets one 8-byte counter occupy an entire line, with the remaining 56 bytes of the line sitting empty. Empty, yet still taken, because a neighbor would slap back. Fifty-six bytes for a dozen-plus-fold reading — worth it on the hot path, but do not waste it on cold data.

For the 4x speedup reported in Section 6.3, this article conveniently handles two candidate explanations. The first is the task-claiming index: it truly is a cache line fought over by eight threads, but with the chunk count fixed at 800, the whole experiment sees only eight hundred `fetch_add` calls — nowhere near a leading role in a two-hundred-million-iteration job; the arithmetic is right there. The second is the scheduler placing two of the eight threads onto the same physical core's hyperthread siblings; what is tested here is merely one concrete arrangement by the scheduler. Check the machine and we know how siblings pair up:

```bash
cat /sys/devices/system/cpu/cpu0/topology/thread_siblings_list
```

```text
0-1
```

On this machine siblings come in pairs, 0-1, 2-3: even numbers are physical cores, odd numbers their siblings. Let's pin once, one thread per physical core. The program is 6.3's dynamic-claiming summation (chunk count likewise fixed at 800), fitted into the same ready-and-go gate as this article, compiled as `exp_i_pinsum`:

```bash
g++ -std=c++20 -O2 -pthread exp_i_pinsum.cpp -o exp_i_pinsum
./exp_i_pinsum                                 # 没绑的跑一遍
taskset -c 0,2,4,6,8,10,12,14 ./exp_i_pinsum   # 再绑到物理核上跑一遍
```

```text
-- unpinned --
round=0 16.26ms total=599999994
round=1 15.94ms total=599999994
round=2 15.61ms total=599999994
-- pinned --
round=0 15.79ms total=599999994
round=1 16.92ms total=599999994
round=2 17.92ms total=599999994
```

The unpinned and pinned labels on the two blocks of readings are mine; the program itself only reports round. The unpinned three rounds sit at 15.6 to 16.3 milliseconds, the pinned three at 15.8 to 17.9 — no lift, and slightly worse within the jitter. The reading did not move, so on my machine one candidate explanation is crossed off. The remaining list still hangs: frequency sharing, and the parts of scheduling the pinning comparison did not cover — I did not verify those (Section 6.3 disclosed that I left frequency scaling on). Cross-session readings compare order of magnitude only: in Section 6.4's granularity experiment at the 800-chunk tier, the same job ran 17 to 19 milliseconds in another session; put those next to this article's numbers and just look at the magnitude.

## Three Diseases, and What Their Readings Look Like

Lock contention, over-fine tasking, and over-sharing are the names this tutorial's Volume 5 gives to three classes of reading shapes. Names aside, the readings have to be your own. Each of the three diseases has its own shape across wall clock, per-synchronization cost, and cache counters; recognize the shape, and you know where to move.

### Lock Contention: The Waiting Grows on the Lock

We fix the total: four hundred thousand increments, independent of thread count, all behind the same `std::mutex`, every increment entering the lock. Thread counts 1, 2, 4, 8, three rounds each:

<OnlineCompilerDemo
  title="Lock contention: fixed total, more threads, slower"
  source-path="code/examples/vol5/chapter06/19_exp_h_fixedtotal.cpp"
  description="All four hundred thousand increments go through the same lock; thread counts 1 to 8, three rounds each: total time climbs from 4ms to around 17ms, and a single lock entry rises from 11 to 44 nanoseconds."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_h_fixedtotal.cpp —— 总量固定四十万次，每次加一都进同一把锁
#include <chrono>
#include <cstdio>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

long long counter = 0;
std::mutex mu;

constexpr long long kTotal = 400000LL;

void op_lock(int threads) {
  const long long n = kTotal / threads;
  for (long long i = 0; i < n; ++i) {
    std::lock_guard<std::mutex> lk(mu);
    ++counter;
  }
}

template <class F>
double run_gated(int threads, F&& op) {
  using clock = std::chrono::steady_clock;
  std::latch ready{threads};
  std::latch go{1};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&] { ready.count_down(); go.wait(); op(threads); });
  ready.wait();
  auto t0 = clock::now();
  go.count_down();
  for (auto& t : pool) t.join();
  auto t1 = clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
  for (int nt : {1, 2, 4, 8}) {
    for (int r = 0; r < 3; ++r) {
      counter = 0;
      double ms = run_gated(nt, op_lock);
      std::printf("threads=%d round=%d %.2fms per_op=%.0fns counter=%lld\n",
                  nt, r, ms, ms * 1e6 / kTotal, counter);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
threads=1 round=0 3.31ms per_op=8ns counter=400000
threads=1 round=1 3.28ms per_op=8ns counter=400000
threads=1 round=2 3.25ms per_op=8ns counter=400000
threads=2 round=0 6.28ms per_op=16ns counter=400000
threads=2 round=1 6.62ms per_op=17ns counter=400000
threads=2 round=2 6.64ms per_op=17ns counter=400000
threads=4 round=0 8.48ms per_op=21ns counter=400000
threads=4 round=1 8.94ms per_op=22ns counter=400000
threads=4 round=2 9.93ms per_op=25ns counter=400000
threads=8 round=0 10.15ms per_op=25ns counter=400000
threads=8 round=1 10.20ms per_op=26ns counter=400000
threads=8 round=2 10.27ms per_op=26ns counter=400000
```

Look at the readings. The total never moved, yet the time climbed from 3.3 to 10.2 milliseconds — adding people made it three times slower. The per-entry price of the lock rose from 8 to 26 nanoseconds: eight threads drove one lock to three times its price. A lock strings everyone into a single queue; the more people, the longer the tail waits.

For comparison, rearrange the same total: each thread does its additions on a local variable and enters the lock once at the end to merge. The change is a few lines:

<OnlineCompilerDemo
  title="Merging outside the lock: same total, a gap of dozens of times"
  source-path="code/examples/vol5/chapter06/20_exp_h_merge.cpp"
  description="Per-thread local accumulation with one lock entry at the end to merge, same 1-to-8 thread sweep: readings fall back to fractions of a millisecond, two orders of magnitude away from the ~10ms of locking every time."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_h_merge.cpp —— 同一份总量，每线程局部累加、收工进一次锁合并
#include <chrono>
#include <cstdio>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

long long counter = 0;
std::mutex mu;

constexpr long long kTotal = 400000LL;

void op_merge(int threads) {
  const long long n = kTotal / threads;
  long long local = 0;
  for (long long i = 0; i < n; ++i) ++local;
  std::lock_guard<std::mutex> lk(mu);
  counter += local;
}

template <class F>
double run_gated(int threads, F&& op) {
  using clock = std::chrono::steady_clock;
  std::latch ready{threads};
  std::latch go{1};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&] { ready.count_down(); go.wait(); op(threads); });
  ready.wait();
  auto t0 = clock::now();
  go.count_down();
  for (auto& t : pool) t.join();
  auto t1 = clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
  for (int nt : {1, 2, 4, 8}) {
    for (int r = 0; r < 3; ++r) {
      counter = 0;
      double ms = run_gated(nt, op_merge);
      std::printf("threads=%d round=%d %.2fms counter=%lld\n", nt, r, ms, counter);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
threads=1 round=0 0.09ms counter=400000
threads=2 round=0 0.09ms counter=400000
threads=4 round=0 0.14ms counter=400000
threads=8 round=0 0.15ms counter=400000
threads=8 round=1 0.20ms counter=400000
threads=8 round=2 0.17ms counter=400000
```

Put the two groups of readings together. Same lock, same total: locking every time with eight threads takes around 10 milliseconds, merging outside the lock takes only 0.15 to 0.20 — a gap of fifty to sixty-plus times. What fills the gap is waiting. With the work outside the lock and the lock only doing the merge, waiting has nowhere to grow.

For how to recognize it, here is a list of things I have done:

1. Add threads and watch the wall clock: with the total fixed, if adding threads makes the reading rise instead of fall, the first suspect is a lock.
2. Compute the price of one synchronization: total time divided by total synchronization count. Section 6.3 said an uncontended lock-and-unlock is on the order of tens of nanoseconds; a reading clearly above that and rising with thread count means contention is present.
3. Move the work outside the lock: change one place, re-measure — whatever the reading drops by is what the waiting was worth.

We used the same way of reading in earlier units: Section 2.3 measured one lock against four, 216 milliseconds versus 152 ([How Wide Should the Lock Stretch: Protection Scope, Granularity, and Sharing Less](../chapter02-sharing-data/03-lock-scope)). Section 6.4's contended version with its 108-fold gap is the extreme shape. This article's ten milliseconds versus 0.2 is the household version.

### Over-Fine Tasking: Coordination Takes Over the House

We already saw the readings in 6.4, where the chunk count was the variable: a two-hundred-million-iteration summation job, 8 threads, chunks stepping from 800 to twenty million. Readings and conditions are in the granularity section of [Benchmark Tools and Three Variables](04-benchmark-and-variables). Three rounds at 800 chunks: 17.3 to 19.3 milliseconds; at eighty thousand chunks still around 18; at twenty million chunks a jump to 257.6 to 262.0 — fourteen times slower. Each chunk holds only 10 additions, yet each chunk must claim a task once, and under contention one claiming `fetch_add` costs more than the work itself: coordination takes over the house.

The recognizable reading shape has two segments: a flat one and a steep one. Sweep the chunk count (or task size) from coarse to fine; where it stays flat, coordination is still a rounding error, and where it steepens is the crossing point — the criterion belongs to 6.4. Write down the numbers around the steep point, and set your granularity around it from then on. The crossing point moves with the job; there is no universal chunk count, and only measuring tells.

### Over-Sharing: Queuing on the Cache Line

You saw the symptoms in 6.4's perf stat table: only 18% more instructions, 295 times the cycles, IPC falling from 4.5 to 0.018, cache misses jumping from 23.3% to 70.9%. That is true sharing. False sharing wears the same reading shape — only, no sharing can be found in the source; this article's packed is the very picture, logically each writing its own, and the readings queue up all the same.

Recognition starts from the data flow: who writes, who reads, how often. When more than one thread writes, ask whether each can write its own and merge at the end. Merge is the answer at the lock, slot the answer at the cache line — two roads, one idea.

perf stat identifies that contention exists. To identify the specific cache line, the manual says it is `perf c2c`'s job: it ranks the cache lines with the highest HITM in the report's first table — HITM counts hits on lines another core modified while this core was still reading them (the manual is in the references). I must disclose honestly: the command did not run through on my machine; perf reported no PMU supports the memory events. The PMU is the processor's counting unit for cycles and cache events, and WSL2 does not give it the memory-latency events.

```bash
perf c2c record -o c2c.data -- ./exp_g_falsesharing packed 2
```

```text
failed: no PMU supports the memory events
```

The environment cannot provide it; following Section 6.2's treatment of GDB, I note this honestly and invent no readings. Run the method on your own Linux — the high-HITM lines in the report are the suspects.

## The Order of Adjustment: Measure First, Change Later

The order has three steps. Step one, measure out the bottleneck: give the suspect sections comparisons, write the conditions down in full, and archive the raw readings. Step two, treat the diagnosed disease: the readings identify the ailment, and you change exactly one place at a time. Step three, re-measure to confirm: measure again under the same conditions, comparing against the raw readings from before the change — never against how fast it should be in your memory.

Let's walk this article's packed through the whole course. The measured reading: packed is a dozen-plus times slower than the one-line-each slot version, and six or seven times slower than serial. The diagnosed disease: not one share in the source, yet readings like true sharing — suspicion falls on data layout; check the addresses, and the two counters sit 8 bytes apart, same line. The one change: wrap the counters in `alignas(64)` — the edit is one type declaration. Re-measurement lands at 3.7 to 4.5 milliseconds; the dozen-plus-fold is reclaimed. The lock was never touched, the thread count never touched, because the readings did not point at them.

One more item in the order is easy to miss: when readings misbehave, the first suspect is the measurement code. This article's harness gave me a taste of that. In the handshake experiment I wrote the gate as `std::latch go{2}` yet gave it three hands to count down: the two worker threads each count once, and the main thread counts once more. Most of the time the main thread's count-down takes the first step and some worker thread's takes the second, so when the late worker thread arrives to count again, the counter has already passed zero; what `wait` sees is negative, not zero, and it waits forever. A forty-million-increment job — wrap-up should be a matter of milliseconds — yet join did not come back for ten-some minutes. The fix is the two doors you saw in the source above: `ready` counts the roll call, `go` handles the release, each hand on its own door. The hang and the re-measurement were two separate experiments. The hung run did the full forty-million job; after fixing the gate, a fresh handshake of two hundred thousand increments measured the gate and handshake overhead alone, reading 0.29 milliseconds. When the harness is wrong, every adjustment afterward is surgery on fake readings — so auditing the harness comes before all the other steps.

Intuition often shouts the wrong direction. A program a dozen-plus times slower — intuition shouts add locks, swap in a lock-free structure, while the readings point at the cache line. With the direction twisted, all the effort is wasted. Section 6.4 left a line behind — which one to change, checking cache lines or touching locks, the readings decide — and it lands here as the three steps above.

## Your Turn

1. Pick out the most expensive stretch of your program, pair it with a serial comparison, measure at least three rounds with the same compile options, and write the raw readings plus machine, core count, and date into the record — copy Section 6.3's approach wholesale.
2. Sweep thread count and chunk count each; note where the readings bend — the bend is the crossing point of coordination cost.
3. When readings misbehave, pull out perf stat's cycles, IPC, and cache misses (event list in Section 6.4) and tell waiting apart from memory access.
4. Let the readings name the disease, change one place at a time, re-measure against the table, and roll the change back if the reading does not move.
5. Audit the harness itself: run your eyes over the gate's counts, the timing boundary, and the anti-optimization functions — do not let the measurement code take readings on your behalf.

This article takes the readings over and turns them into cache-line layout, lock scope, and task granularity. The other half of the waiting problem belongs to Unit 7's coroutines: while a thread waits, the whole core sits idle; coroutines reorganize things and give the waiting thread back.

## References

The approach drew on the performance-and-debugging chapters of Williams' C++ Concurrency in Action and McKenney's perfbook — what I borrowed is the organization and the way of explaining; the examples and the words are my own, and the readings were measured on the machine described in the text. Claims about cache lines and the interference constant were checked against cppreference and the LLVM commit, accessed 2026-11-17.

- [The alignas specifier (cppreference)][cppref-alignas] — since C++11; the source of this article's fix.
- [hardware_destructive_interference_size (cppreference)][cppref-interference] — the sources for C++17, `<new>`, the implementation-defined value, and the 201703L feature macro.
- [Clang adopts the 128-byte destructive interference size for Apple AArch64][llvm-apple-128] — LLVM commit 9c8485a2, merged 2026-07-10; the source for Apple's 128-byte line width and GCC's 256 on AArch64.
- [LLVM issue 60174][llvm-60174] — the Clang support tracker once cited by the old CPU Caches and OS Threads article; verified working on the local clang 23.1.3.
- [perf-c2c man page (man7)][perf-c2c] — the sources for the HITM ranking table and per-architecture events; verified 2026-11-17.
- [perf tutorial (perf wiki)][perf-wiki] — perf stat usage; cited in Section 6.4 and carried over here.
- [std::latch (cppreference)][cppref-latch] — the C++20 gate; the harness's ready and go use it.
- [std::atomic (cppreference)][cppref-atomic] — the source for `fetch_add` and the memory-order parameter.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams] — the performance and debugging chapters.
- [Is Parallel Programming Hard, And, If So, What Can You Do About It? (perfbook)][perfbook] — Paul E. McKenney's free online book, kernel.org mirror.

[cppref-alignas]: https://en.cppreference.com/w/cpp/language/alignas
[cppref-interference]: https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size
[llvm-apple-128]: https://llvm.googlesource.com/llvm-project/+/9c8485a2c216160bfc5d64a71cbbd298776618bd
[llvm-60174]: https://github.com/llvm/llvm-project/issues/60174
[perf-c2c]: https://man7.org/linux/man-pages/man1/perf-c2c.1.html
[perf-wiki]: https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat
[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[cppref-atomic]: https://en.cppreference.com/w/cpp/atomic/atomic
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
[perfbook]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
