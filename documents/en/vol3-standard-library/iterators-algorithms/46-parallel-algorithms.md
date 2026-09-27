---
chapter: 7
cpp_standard:
- 17
- 20
description: 'A close look at the four <execution> policies (seq/par/par_unseq/unseq) and the parallelism and vectorization semantics each permits, why reduce demands associativity, and the engineering judgment of when parallel algorithms truly get faster versus when they get slower instead — with real timings measured on this machine under GCC 16.1.1 + libstdc++/TBB, no fabricated speedup numbers'
difficulty: advanced
order: 46
platform: host
prerequisites:
- 'Iterator Basics and Categories: The Glue Between Containers and Algorithms'
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New Tricks'
reading_time_minutes: 16
related:
- 'Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- advanced
- 容器
title: 'Parallel Algorithms: execution Policies and When They Actually Get Faster'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/46-parallel-algorithms.md
  source_hash: 63d96d55feb218a2d87b0708ea60cc1b106a3bec7fa5fab3921da8b78e642c8c
  translated_at: '2026-09-26T01:47:47+00:00'
  engine: anthropic
  token_count: 4200
---
# Parallel Algorithms: `<execution>` Policies and When They Actually Get Faster

In the earlier algorithm articles, `std::sort`, `std::accumulate`, and `std::copy` all ran on a single thread — one chunk of work from head to tail. But modern machines casually ship with a dozen or twenty cores; isn't it self-evident that letting `sort` sort on eight cores at once should be faster?

C++17 delivered exactly this mechanism: standard library algorithms accept one extra "execution policy" parameter declaring how much parallelism you permit, and the library decides how to split and schedule the work. One line — `std::sort(std::execution::par, v.begin(), v.end())` — in theory spreads the job across cores. Sounds lovely, but two real engineering questions hide here, and they are exactly what this article will take apart:

First, **parallel does not mean faster**. Thread creation, task splitting, and result merging are not free; when the data volume is too small, or the algorithm itself is strangled by memory bandwidth (say a `reduce` hammering away at additions), parallel can actually be slower. We will not chant the "parallel is good" slogan — instead we will pull up real timing data from this machine and see clearly when that `par` is actually worth adding.

Second, **parallelism changes what the algorithm demands of the function object**. In single-threaded `std::transform`, passing a lambda whose calls can be exchanged even though the operation does not satisfy the commutative law is harmless; the moment you switch to `par`, the standard allows it to run in an arbitrary order of combination, so an algorithm that does not satisfy associativity produces wrong results. This article will spell out which algorithms can take `par` and which cannot, instead of blindly shoving `par` into every algorithm.

## The Four Execution Policies: How Aggressive You Let the Library Get

The `<execution>` header defines four policy objects: from conservative to aggressive they are `seq`, `par`, and `par_unseq`, plus `unseq`, newly added in C++20. They are not "use thread number N" switches — you cannot get control that fine-grained — they are declarations of how freely the library may schedule the element access functions. Only with that authorization in hand does the library go decide whether to spin up threads, whether to vectorize.

First a minimal example — all four policies compile (on this machine's GCC 16.1.1):

```cpp
// Standard: C++20
#include <algorithm>
#include <execution>
#include <iostream>
#include <vector>

int main() {
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};

    std::sort(std::execution::seq, v.begin(), v.end());
    std::sort(std::execution::par, v.begin(), v.end());
    std::sort(std::execution::par_unseq, v.begin(), v.end());
    std::sort(std::execution::unseq, v.begin(), v.end());
    std::cout << "all four policies compiled and ran\n";
    return 0;
}
```

```text
all four policies compiled and ran
```

All four compile. So where do they actually differ? The crux is what kind of overlap is permitted between calls of the element access function. cppreference states the semantics of the four policies precisely; we distill them into this table:

| Policy | Multi-threaded? | Vectorized? | Relationship between calls in the same thread | Can you lock? |
|------|---------|---------|--------------------------|---------|
| `seq` (C++17) | No | No | indeterminately sequenced (no overlap, order unspecified) | Yes |
| `par` (C++17) | Yes | No | indeterminately sequenced (no overlap within a thread) | Yes (parallel forward progress guarantees a thread holding a lock will be scheduled again) |
| `par_unseq` (C++17) | Yes | Yes | unsequenced (calls may interleave within a thread; vectorizable) | **No** (weakly parallel progress — a thread is not guaranteed to be scheduled again) |
| `unseq` (C++20) | No | Yes | unsequenced (vectorized and interleavable within a single thread) | **No** |

The last two rows are the easiest to crash on — because `par_unseq` and `unseq` allow multiple calls to interleave within a single thread (unsequenced), your function object **must not call anything vectorization-unsafe**: locking (`std::mutex::lock`), non-lock-free `std::atomic` operations, even `new`/`delete` all count. cppreference gives a direct counterexample:

```cpp
int x = 0;
std::mutex m;
int a[] = {1, 2};
std::for_each(std::execution::par_unseq, std::begin(a), std::end(a), [&](int) {
    std::lock_guard<std::mutex> guard(m);   // wrong: the constructor calls m.lock(), which is vectorization-unsafe
    ++x;
});
```

Why does `par_unseq` forbid locking? Because "unsequenced" means two element accesses inside the same thread may interleave — the instruction pipeline can jump from function A to function B and back at any moment. Once such interleaving is allowed, lock/unlock can no longer be guaranteed to pair up, and mutex semantics collapse on the spot. So the standard simply rules: with an unsequenced policy, forget about synchronization. Parallel scenarios that need locking should go at most to `par` (it guarantees calls within a thread do not interleave, and that a thread holding a lock will be scheduled again).

As for the difference between `seq` and `par`, that is far more intuitive: `seq` is always single-threaded and the library may not split the work; `par` lets the library open threads, but multiple calls on the same thread remain sequenced and non-overlapping, so you can still lock — which is also why `par` is the most used policy in practice: fast enough, and not so picky about what you feed it.

::: warning Don't treat policies as a thread count
None of these four policies lets you write "open 8 threads for me". Whether to parallelize, and how many threads to open, is decided by the library (under libstdc++, that means TBB underneath) — you are only granting authorization. For fine-grained control over concurrency, go straight to the `std::thread`/`std::async`/thread pools covered in the vol5 concurrency volume, not to execution policies.
:::

## How to Use It: Slip the Algorithm a Policy Argument

The usage itself is painless — almost every `<algorithm>` algorithm has an overload taking an execution policy, and the policy is the **first argument**, inserted before the iterators. The C++17 newcomers in `<numeric>` (`reduce`, `transform_reduce`, the various scans) have parallel versions too.

```cpp
// Standard: C++20
#include <algorithm>
#include <execution>
#include <numeric>
#include <vector>

void demo(std::vector<int>& v) {
    // sort: parallelism allowed
    std::sort(std::execution::par, v.begin(), v.end());

    // sum: reduce is the parallel-friendly version of accumulate (requires associativity, more on that later)
    long sum = std::reduce(std::execution::par, v.begin(), v.end(), 0L);

    // rewrite element by element
    std::transform(std::execution::par, v.begin(), v.end(), v.begin(),
                   [](int x) { return x * 2; });

    // run one operation per element (note: order not guaranteed)
    std::for_each(std::execution::par, v.begin(), v.end(),
                  [](int x) { /* use x */ });
}
```

The key point fits in one sentence: **the policy is an extra argument — pass it and you authorize the library to schedule accordingly; omit it (the old `std::sort(beg, end)` form) and you get the equivalent of `seq`**. So migrating old code to the parallel version takes a minimal change: stuff a `std::execution::par` at the very front of the call.

But "can add" does not mean "should add". The next section is where this article gets most rigorous — **we pull real data to see whether adding it actually pays off**.

## Measured: When Parallel Truly Speeds Up, and When It Slows Down Instead

Every number in this section was run on this machine: AMD Ryzen 7 5800H (8 cores, 16 threads), GCC 16.1.1, and libstdc++ with TBB as the parallel backend (more on that later — it is a genuine trap). The compile command was uniformly `g++ -std=c++20 -O2 bench.cpp -ltbb`; each program was run twice and one representative run taken.

### Large Data First: par Really Is Markedly Faster

We test with two representative algorithms — `reduce` (pure arithmetic, limited by memory bandwidth) and `sort` (compute-intensive, lots of comparisons and data movement). Data volumes are turned up high enough.

```cpp
// Standard: C++20
#include <algorithm>
#include <chrono>
#include <execution>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
    const std::size_t kReduceN = 50'000'000;
    const std::size_t kSortN = 5'000'000;

    std::vector<long> v(kReduceN);
    for (std::size_t i = 0; i < kReduceN; ++i) v[i] = static_cast<long>(i % 1000);

    auto t0 = Clock::now();
    long s_seq = std::reduce(std::execution::seq, v.begin(), v.end(), 0L);
    double dt_seq = ms_since(t0);

    auto t1 = Clock::now();
    long s_par = std::reduce(std::execution::par, v.begin(), v.end(), 0L);
    double dt_par = ms_since(t1);

    std::cout << "=== reduce N=" << kReduceN << " ===\n";
    std::cout << "seq: " << dt_seq << " ms\n";
    std::cout << "par: " << dt_par << " ms  (speedup " << (dt_seq / dt_par) << "x)\n\n";

    std::mt19937 rng(42);
    std::vector<int> a(kSortN), b(kSortN);
    for (std::size_t i = 0; i < kSortN; ++i) {
        int x = static_cast<int>(rng());
        a[i] = x; b[i] = x;
    }

    auto t2 = Clock::now();
    std::sort(std::execution::seq, a.begin(), a.end());
    double dt_sort_seq = ms_since(t2);

    auto t3 = Clock::now();
    std::sort(std::execution::par, b.begin(), b.end());
    double dt_sort_par = ms_since(t3);

    std::cout << "=== sort N=" << kSortN << " ===\n";
    std::cout << "seq: " << dt_sort_seq << " ms\n";
    std::cout << "par: " << dt_sort_par << " ms  (speedup " << (dt_sort_seq / dt_sort_par) << "x)\n";
    return 0;
}
```

```text
=== reduce N=50000000 ===
seq: 24.3248 ms
par: 16.2351 ms  (speedup 1.49829x)

=== sort N=5000000 ===
seq: 341.455 ms
par: 62.773 ms  (speedup 5.43952x)
```

The speedup gap between the two algorithms is huge, and that illustrates one core principle: **how well parallel acceleration works depends on what the algorithm is bottlenecked by when single-threaded**.

`sort` speeds up more than 5x because sorting is compute-intensive — masses of comparisons, moves, and random memory access, with raw CPU horsepower as the bottleneck. Split the work across 8 cores and every core can run its horsepower at full tilt, so the speedup naturally approaches the core count (it lands under 8 here because task splitting and merging still cost something).

`reduce` speeds up only 1.5x, which looks like neither here nor there, and the reason is that it is choked by **memory bandwidth**. A reduce does one addition per element — a single core computes faster than memory can feed it long before you run out of cores. The bottleneck is the step that hauls 50 million longs from memory to the CPU, and that data path runs over the same memory bus shared by all 8 cores, so opening more cores cannot move more data. This is the classic memory-bound scenario: the payoff parallelism can wring out is inherently limited.

Put differently: **to judge whether an algorithm parallelizes profitably, first ask whether it is compute-bound or memory-bound when single-threaded**. Compute-intensive cases (sort, or transform paired with heavy computation) pay off; memory-bandwidth-heavy cases (lightweight element-wise reduction like reduce) hit a very low ceiling. This judgment matters far more than blindly adding `par`.

### Small Data Next: par Is 60 Times Slower Instead

Shrink the data to 1000 elements and compare the timing of `seq` versus `par` for the same `reduce` (best of 5 runs):

```cpp
// Standard: C++20
#include <chrono>
#include <execution>
#include <iostream>
#include <numeric>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
    const std::size_t kSmallN = 1000;
    std::vector<int> v(kSmallN, 1);
    double best_seq = 1e9, best_par = 1e9;
    for (int i = 0; i < 5; ++i) {
        auto t0 = Clock::now();
        volatile long s1 = std::reduce(std::execution::seq, v.begin(), v.end(), 0L);
        (void)s1;
        best_seq = std::min(best_seq, ms_since(t0));
        auto t1 = Clock::now();
        volatile long s2 = std::reduce(std::execution::par, v.begin(), v.end(), 0L);
        (void)s2;
        best_par = std::min(best_par, ms_since(t1));
    }
    std::cout << "seq: " << best_seq << " ms\n";
    std::cout << "par: " << best_par << " ms\n";
    std::cout << "par/seq ratio: " << (best_par / best_seq) << "  (>1 means par slower)\n";
    return 0;
}
```

```text
seq: 0.00014 ms
par: 0.008376 ms
par/seq ratio: 59.8286  (>1 means par slower)
```

`par` is nearly **60 times slower**. The reason could not be plainer: adding 1000 elements takes a single thread a few microseconds; but `par` has to start up the TBB scheduler for this one call, split tasks, dispatch threads, and gather results — and that **fixed overhead** by itself costs more than the entire sequential computation. The smaller the data volume, the bigger the fixed overhead's share, and the more parallelism loses.

To bottle this conclusion up: **the fixed overhead of parallelism is not zero; a break-even point exists**. For a lightweight operation like reduce, that point may sit somewhere around a hundred thousand or a million elements; for a compute-intensive operation like sort, the point is lower. In practice, do not add `par` brainlessly — for data volumes you are unsure about, either measure yourself or simply don't add it: the sequential algorithm is always optimal on small data.

::: warning Don't add par just to look modern
A common pitfall: seeing that the standard library supports parallel versions, people brainlessly rewrite every `std::sort` into `std::sort(std::execution::par, ...)`. For containers of a few hundred or a few thousand elements, that change most likely slows the code down by tens of times, while squatting on the thread pool for nothing. `par` is for scenarios where the data volume is large enough to be worth parallelizing — it is not a decoration.
:::

## The Price of Parallelism: What Algorithms Demand from Function Objects Changes

Parallelism buys speed, and the price is that its demands on function objects are far **stricter** than the sequential versions. Two core requirements: **associativity**, and (for the unsequenced policies) **vectorization safety**. Algorithms that fail either produce wrong results, or run into compile/run problems outright.

### reduce Demands Associativity: accumulate Doesn't, reduce Does

The most classic contrast is `std::accumulate` versus `std::reduce`. Both "collapse a sequence of elements into one value" and look nearly identical, but their semantic requirements are worlds apart:

- `std::accumulate` is a strict **left fold** — computed one by one from left to right, with the order of combination fixed. So it **does not require** the binary operation to be associative.
- `std::reduce` allows the library to compute in **an arbitrary order of combination** (that is what lets it split the work onto multiple cores, each computing its own share), so it **does require** the binary operation to be associative; the default `+` qualifies, but for a custom operation you have to guarantee it yourself.

This difference shows up immediately on floating-point — floating-point addition **does not satisfy associativity**: `(a+b)+c` and `a+(b+c)` can differ under floating point. Feed the same bunch of floats to `accumulate`, `reduce(seq)`, and `reduce(par)`, and the three results will differ:

```cpp
// Standard: C++20
#include <execution>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    std::vector<float> v;
    for (int i = 0; i < 100000; ++i) v.push_back(0.1f);

    float acc     = std::accumulate(v.begin(), v.end(), 0.0f);
    float red_seq = std::reduce(std::execution::seq, v.begin(), v.end(), 0.0f);
    float red_par = std::reduce(std::execution::par, v.begin(), v.end(), 0.0f);

    std::cout.precision(12);
    std::cout << "accumulate (left fold): " << acc << "\n";
    std::cout << "reduce seq           : " << red_seq << "\n";
    std::cout << "reduce par           : " << red_par << "\n";
    return 0;
}
```

```text
accumulate (left fold): 9998.55664062
reduce seq           : 10000.3525391
reduce par           : 10000.3349609
```

All three results differ. Mathematically the "correct answer" is 10000 (0.1 added a hundred thousand times), but floating-point error pulls it off course, and by how much depends on the order of combination — `accumulate` keeps adding small numbers onto an ever-growing accumulator, so error piles up hardest (off by 1.4); `reduce` splits the sequence into chunks, sums within each chunk, then merges, and since the values inside chunks stay small the error stays small, landing it closer to the true value instead. `reduce seq` and `reduce par` also differ, because the splitting differs.

The essence of the matter: **floating-point addition does not satisfy associativity, so mathematically it should not be parallelized at all**. The standard library lets you do it anyway (no error reported), at the price of results that differ from the sequential version — and can even differ from run to run (depending on thread scheduling). If your program demands **reproducibility** of floating-point results (finance and scientific computing often need bit-for-bit agreement), `reduce(par)` is a landmine. Either sacrifice parallelism with `accumulate`, or reach for a compensated algorithm such as Kahan summation.

Conversely, integer addition, bitwise operations, and logical and/or are all naturally associative, so `reduce` parallelizes safely. When judging "can my reduce go par", first ask **whether your binary operation satisfies associativity** — not what the data type is.

::: warning The reduce(init, op) form additionally requires op to commute with init
The four-argument overload `std::reduce(first, last, init, op)` requires more than `op` being associative: both `op(init, x)` and `op(x, init)` must be valid and give consistent results — in other words, init and the elements must be commutative under op. The reason is parallel splitting again: the library may combine init with any chunk. When you customize op and init is some special "identity element" type, make sure op behaves correctly in both argument positions.
:::

### Under par, an Exception Means std::terminate

In sequential algorithms (including the `seq` policy), an exception thrown from the function object propagates up normally and you can `catch` it. But under all parallel policies — `par`, `par_unseq`, `unseq` — the moment the element access function throws an uncaught exception, the standard mandates a direct call to `std::terminate`, and the program dies. We verified this live:

```cpp
// Standard: C++20
#include <algorithm>
#include <csignal>
#include <execution>
#include <iostream>
#include <stdexcept>
#include <vector>

void on_term() {
    std::cout << "std::terminate called (exception escaped par algorithm)" << std::endl;
    std::_Exit(1);
}

int main() {
    std::set_terminate(on_term);
    std::vector<int> v(10, 1);
    std::size_t i = 0;
    try {
        std::for_each(std::execution::par, v.begin(), v.end(), [&i](int& x) {
            if (i++ == 3) throw std::runtime_error("boom");
            x = 2;
        });
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";   // this line is never reached
    }
    return 0;
}
```

```text
std::terminate called (exception escaped par algorithm)
```

Look closely: the outer `try/catch` never received the exception — the exception never propagated out at all; `terminate` was called directly and the process exited. This is a deeply unintuitive gap between parallel and sequential algorithms: **under `par`, the function object must be effectively non-throwing** — either guarantee the logic cannot throw, or mark it `noexcept` and digest errors internally. Algorithms that need an error-handling path should either stay on `seq`, or use a channel that does not rely on exceptions, such as `std::expected`/return values.

Why is it this way? Picture exceptions flying from eight threads at once: who gathers them? Which exception should propagate out? The standard simply rules no propagation, straight termination, cutting the complexity away in one stroke. That is also why function objects for parallel algorithms should stay as simple and `noexcept` as possible.

## A Trap You Cannot Dodge: libstdc++'s Parallel Backend Is TBB and Must Be Linked Manually

Every earlier example was compiled with `-ltbb`. That is not an optional extra — it is the key to whether libstdc++ parallel algorithms link at all, and the wall beginners run into most easily.

libstdc++'s parallel algorithms (the PSTL) rely on Intel TBB underneath for thread scheduling. So the moment your code **uses** `std::execution::par` (even just `reduce(par, ...)`), compilation passes, but linking fails with a long string of `undefined reference to tbb::...`. Compile that minimal par example from the top without `-ltbb`:

```text
/usr/bin/ld: ... undefined reference to `tbb::detail::r1::initialize(tbb::detail::d1::task_group_context&)'
... (dozens of lines of undefined TBB symbols)
collect2: error: ld returned 1 exit status
```

Want to see why `par` depends on TBB? Compile only (no linking) and look at the assembly — the `par` version (`sum_par`) is a string of `call __gnu_parallel`/`tbb` runtime symbols, while the `seq` version (`sum_seq`) is just a plain scalar loop:

<OnlineCompilerDemo
  title="Assembly of reduce seq vs par: TBB runtime calls"
  source-path="code/examples/vol3/46_parallel_asm.cpp"
  description="Compile only and read the assembly (no running, so no -ltbb needed): the seq version is a scalar accumulation loop, the par version a string of call __gnu_parallel/tbb — this is the assembly-level evidence that the parallel backend is TBB"
  allow-x86-asm
/>

The fix is one line: add `-ltbb` to the compile command (TBB must be installed on the system; this machine has `libtbb.so.12`). In a CMake project the equivalent is `find_package(TBB REQUIRED)` followed by `target_link_libraries(... TBB::tbb)`.

::: warning Without -ltbb, linking fails
As long as you use `par`/`par_unseq`, libstdc++ must link TBB. `seq` and `unseq` do not go through TBB (sequential and purely vectorized execution need no thread pool), so using only these two links fine without `-ltbb`. But in practice policies get swapped around a lot, and pinning `-ltbb` in the project is the least hassle. That is also why every `par`-bearing example in this article uses the compile command `g++ -std=c++20 -O2 xxx.cpp -ltbb`.
:::

One comparison to add: libc++ (the Clang stack), the other major standard library implementation, has a different default parallel backend and does not depend on TBB; MSVC's parallel algorithms work out of the box with no extra linking. So "whether to link TBB" is a libstdc++-specific problem — watch out for it when porting code.

## The C++17 Backdrop and C++20's unseq

Parallel algorithms entered the standard with C++17 (descended from the earlier Parallelism TS). Before that, parallel sorting meant hand-rolling threads or going through a third-party library (TBB, OpenMP). C++17 in one sweep added execution-policy overloads to more than 60 `<algorithm>` algorithms and the reduce/scan algorithms of `<numeric>`, plus a batch of new algorithms born for parallelism — `reduce`, `transform_reduce`, `inclusive_scan` and friends (the old `accumulate` does not require associativity and cannot be safely parallelized, hence the fresh start).

C++20 then added the fourth policy `unseq` — single-threaded vectorization (pure SIMD). Its design motivation: some scenarios do not want multiple threads (an embedded single-core chip, say, or data too small for threads to pay off), but still want the compiler to vectorize the loop and use SIMD instructions. `par_unseq` can vectorize too, but it also opens threads; `unseq` splits "vectorization" out on its own, giving you a "no threads, SIMD only" option.

But `unseq`'s actual effect on libstdc++ often disappoints. cppreference's own example (g++ -std=c++23 -O3 -ltbb) sorting 1 million elements reports, for the four policies: seq 165ms, unseq 163ms, par_unseq 30ms, par 27ms. See it clearly? **`unseq` is barely faster than `seq`** — 163 vs 165, essentially within error. The reason, again, is that vectorization buys limited gains for operations like integer comparisons; the SIMD lanes go underfed. Where `unseq` can genuinely pull ahead is highly regular, branch-free, compute-intensive element-wise operations (say element-wise math over float arrays) — in practice you have to measure per scenario to know.

So the right expectation for `unseq` is: **it is a "request vectorization" hint, not a guarantee of speedup**. As with `par`, measure whether it pays — no blind faith.

## Summary

In the parallel algorithms game, the easiest part to learn is "how to add the policy argument", and the easiest way to get burned is "assuming `par` is automatically faster". Let us gather the key conclusions:

- The four policies from conservative to aggressive: `seq` (single-threaded sequential), `par` (may multi-thread; no interleaving within a thread; locking allowed), `par_unseq` (may multi-thread plus vectorize; calls may interleave within a thread; **no locking**), `unseq` (C++20; single-threaded vectorization; likewise no locking).
- Because `par_unseq` and `unseq` allow calls to interleave within a single thread, function objects must not perform any vectorization-unsafe operation: locking, non-lock-free atomics, even `new`/`delete`. Parallel scenarios that need locking should use `par` at most.
- **When parallel truly gets faster**: with compute-bound algorithms (like sort) the speedup is significant (5x+ on this machine); with memory-bound ones (like reduce) memory bandwidth caps the gain (1.5x on this machine); and when the data volume is so small that fixed overhead dominates, `par` is tens of times slower instead.
- **reduce requires associativity, accumulate does not**. Floating-point addition does not satisfy associativity, so a float `reduce(par)` differs from the sequential version and even from run to run — forbidden wherever reproducibility is required.
- A function object that throws under `par` and the policies beyond it triggers `std::terminate` directly, never caught by the outer `try/catch` — function objects for parallel algorithms should be effectively `noexcept`.
- **libstdc++'s parallel backend is TBB**: as soon as you use `par`/`par_unseq`, the compile command needs `-ltbb`, or linking fails; libc++ and MSVC have no such requirement.
- `unseq` is a "request vectorization" hint; for operations like integer comparisons it often buys almost no speedup — no blind faith.

In the next article we look at the standard library from another angle — `<chrono>` and time handling, yet another facility that looks simple and hides plenty of traps.

## References

- [cppreference: Execution policy tags](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag) — definitions of the four policy objects `seq`/`par`/`par_unseq`/`unseq`
- [cppreference: Execution policy types](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) — the precise semantics of how each policy type may schedule element access functions (indeterminately sequenced vs unsequenced, whether locking is allowed, exception behavior)
- [cppreference: std::reduce](https://en.cppreference.com/w/cpp/numeric/reduce) — reduce's associativity requirement and its relation to accumulate
- [cppreference: Parallel algorithms](https://en.cppreference.com/w/cpp/algorithm) — overview of all algorithms with execution-policy overloads since C++17
- [GCC libstdc++ manual: Parallel algorithms](https://gcc.gnu.org/onlinedocs/libstdc++/manual/parallel.html) — notes that libstdc++'s parallel backend depends on TBB
