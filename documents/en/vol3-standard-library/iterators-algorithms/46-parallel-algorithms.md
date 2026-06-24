---
chapter: 7
cpp_standard:
- 17
- 20
description: We dive deep into the four `<execution>` policies (`seq`/`par`/`par_unseq`/`unseq`),
  explaining the parallel and vectorization semantics each permits, why `reduce` requires
  associativity, and the engineering judgment behind when parallel algorithms truly
  speed things up versus when they actually slow them down—accompanied by real-world
  timing benchmarks on a local setup using GCC 16.1.1 with libstdc++/TBB, no fake
  speedup numbers.
difficulty: advanced
order: 46
platform: host
prerequisites:
- 迭代器基础与 category
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 16
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- advanced
- 容器
title: 'Parallel Algorithms: execution Policies and When They Are Actually Faster'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/46-parallel-algorithms.md
  source_hash: 2dae5b73522199fd6cf6c14f2594ef469c7024c4e0e50db7c405fc83a385f784
  translated_at: '2026-06-24T02:43:29.195444+00:00'
  engine: anthropic
  token_count: 3902
---
# Parallel Algorithms: `<execution>` Policies and When They Are Actually Faster

In previous articles on algorithms, `std::sort`, `std::accumulate`, and `std::copy` all ran on a single thread—handling one job from start to finish. However, modern machines often have dozens of cores. Shouldn't it be理所当然 that running `sort` across eight cores would be faster?

C++17 provides this mechanism: add an "execution policy" parameter to standard library algorithms to declare the degree of parallelism you allow, while the library decides how to partition and schedule the work. With a single line like `std::sort(std::execution::par, v.begin(), v.end())`, the work is theoretically spread across multiple cores. This sounds great, but there are two real-world engineering problems here, which are exactly what this article will dissect:

First, **parallel does not equal faster**. Thread creation, task partitioning, and result aggregation come with overhead that isn't free. If the data volume is too small, or if the algorithm is bottlenecked by memory bandwidth (for example, a `reduce` operation that just accumulates values), parallel execution can actually be slower. We won't just chant the slogan "parallel is good"; instead, we will use real timing data from our local machine to see exactly when it is worth adding that `par`.

Second, **parallelism changes the requirements for function objects**. In single-threaded mode, passing a lambda to `std::transform` that is commutative but not associative might work fine; but once you switch to `par`, the standard allows it to run in any order of association. If the algorithm is not associative, the results will be wrong. This article will clarify "which algorithms can use `par` and which cannot," rather than blindly stuffing `par` into every algorithm.

## Four Execution Policies: How Aggressive You Allow the Library to Be

The `<execution>` header defines four policy objects, ranging from conservative to aggressive: `seq`, `par`, `par_unseq`, plus the C++20 addition `unseq`. They are not switches to "specify which thread to use"—you can't control that finely—but rather declarations that "allow the library to schedule element access functions in more flexible ways." Only with this authorization does the library decide whether to spawn threads or vectorize.

Let's look at a minimal example that compiles successfully with all four policies (tested on local GCC 16.1.1):

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

All four strategies compile successfully. So, what exactly is the difference between them? The key lies in the **allowed overlapping behavior** between calls to the element access function. cppreference describes the semantics of the four strategies precisely, which we have summarized in the table below:

| Strategy | Multi-threaded? | Vectorized? | Relationship between calls within the same thread | Can it lock? |
|----------|-----------------|-------------|---------------------------------------------------|--------------|
| `seq` (C++17) | No | No | Indeterminately sequenced (no overlap, indeterminate order) | Yes |
| `par` (C++17) | Yes | No | Indeterminately sequenced (no overlap within the same thread) | Yes (Parallel forward progress guarantees that the thread holding the lock will be scheduled again) |
| `par_unseq` (C++17) | Yes | Yes | Unsequenced (interleaving and vectorization allowed within the same thread) | **No** (Weakly parallel progress; threads are not guaranteed to be scheduled again) |
| `unseq` (C++20) | No | Yes | Unsequenced (vectorization and interleaving allowed within a single thread) | **No** |

The easiest ways to shoot yourself in the foot are the last two rows—`par_unseq` and `unseq`. Because these strategies allow interleaving multiple calls within a single thread (unsequenced), your function object **must not call any vectorization-unsafe operations**: locking (`std::mutex::lock`), non-lock-free `std::atomic`, or even `new`/`delete` all count. cppreference provides a direct counter-example:

```cpp
int x = 0;
std::mutex m;
int a[] = {1, 2};
std::for_each(std::execution::par_unseq, std::begin(a), std::end(a), [&](int) {
    std::lock_guard<std::mutex> guard(m);   // 错误：构造里调 m.lock()，vectorization-unsafe
    ++x;
});
```

Why doesn't `par_unseq` allow locking? Because "unsequenced" means that two element accesses within the same thread can be interleaved—the instruction pipeline might jump from function A to function B and back at any time. Once this interleaving is allowed, lock/unlock operations can no longer be guaranteed to be paired, and mutex semantics collapse immediately. Therefore, the standard simply stipulates: if you use an unsequenced policy, forget about synchronization. For parallel scenarios requiring locks, the most you can use is `par` (it guarantees that calls within the same thread are not interleaved, and threads holding locks will be rescheduled).

The difference between `seq` and `par` is much more intuitive: `seq` is always single-threaded, and the library is not allowed to switch contexts; `par` allows the library to spawn threads, but multiple calls on the same thread remain "sequentially non-overlapping," so you can still use locks—this is also why `par` is the most commonly used in practice; it's fast enough and not so picky.

::: warning Don't treat policies as "specifying thread count"
None of these four policies allow you to write "give me 8 threads." Whether to parallelize and how many threads to spawn is decided by the library (in libstdc++, it's the underlying TBB); you are merely granting permission. For fine-grained control over concurrency, you need to go straight to `std::thread`/`std::async`/thread pools as discussed in Volume 5: Concurrency, rather than relying on execution policies.
:::

## How to use: Pass a policy argument to the algorithm

The usage itself is quite straightforward—almost all `<algorithm>` algorithms have an overload with an execution policy. The policy is the **first parameter**, inserted before the iterators. Several algorithms added to `<numeric>` in C++17 (`reduce`, `transform_reduce`, and the various `scan` algorithms) also have parallel versions.

```cpp
// Standard: C++20
#include <algorithm>
#include <execution>
#include <numeric>
#include <vector>

void demo(std::vector<int>& v) {
    // 排序：允许并行
    std::sort(std::execution::par, v.begin(), v.end());

    // 累加：reduce 是 accumulate 的并行友好版（要求结合律，后面详谈）
    long sum = std::reduce(std::execution::par, v.begin(), v.end(), 0L);

    // 逐元素改写
    std::transform(std::execution::par, v.begin(), v.end(), v.begin(),
                   [](int x) { return x * 2; });

    // 对每个元素执行一个操作（注意：不保证顺序）
    std::for_each(std::execution::par, v.begin(), v.end(),
                  [](int x) { /* 用 x */ });
}
```

The key takeaway is simple: **The execution policy is an additional parameter. By including it, you authorize the library to schedule the work accordingly. If you omit it (using the legacy `std::sort(beg, end)` form), it defaults to `seq`**. Therefore, the minimal change to migrate legacy code to the parallel version is to prepend `std::execution::par` to the function call.

However, just because we *can* add it doesn't mean we *should*. In this section, we get down to brass tacks—**using real-world data to see if the overhead is actually worth it**.

## Benchmarking: When Parallelism Truly Speeds Things Up, and When It Slows Them Down

All figures in this section were obtained from local testing on an AMD Ryzen 7 5800H (8 cores, 16 threads), using GCC 16.1.1. The libstdc++ parallel backend is TBB (Intel Threading Building Blocks)—we will cover this specific detail later, as it can be a real pitfall. The compilation command was consistently `g++ -std=c++20 -O2 bench.cpp -ltbb`, and each program was run twice to obtain a representative result.

### First, Large Data Volumes: `par` is Significantly Faster

We test two typical algorithms: `reduce` (pure arithmetic, memory bandwidth bound) and `sort` (compute intensive, requiring extensive comparisons and data movement). The data volume is set sufficiently large for both tests.

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

The speedup difference between the two algorithms is massive, which perfectly illustrates a core principle: **the effectiveness of parallelization depends entirely on what bottlenecks the algorithm in a single-threaded context**.

`sort` speeds up by over 5x because sorting is compute-intensive— involving massive amounts of comparisons, data movement, and random memory accesses. The CPU's computing power is the bottleneck. When we distribute the workload across 8 cores, each core can max out its compute capabilities, so the speedup ratio naturally approaches the core count (falling short of 8x here only due to the overhead of task splitting and merging).

`reduce` only speeds up by 1.5x, which seems "lackluster." The reason is that it is bottlenecked by **memory bandwidth**. The computation in `reduce` is just a single addition; a single core can perform calculations much faster than memory can supply data. The bottleneck lies in "moving 50 million `long`s from memory to the CPU." Since this step relies on a memory bus data path shared by all 8 cores, spawning more cores doesn't move data any faster. This is a classic memory-bound scenario where the potential gains from parallelization are inherently limited.

In other words: **to determine if parallelizing an algorithm is worthwhile, first ask if it is compute-bound or memory-bound in a single-threaded state**. Compute-intensive tasks (like `sort` or `transform` with heavy computation) are worthwhile, while memory bandwidth-intensive tasks (like lightweight element-wise `reduce`) have a very low ceiling. This judgment is far more critical than blindly adding `par`.

### Looking at Small Data Volumes: `par` is 60x Slower

When we reduce the data size to 1,000 elements and run the same `reduce`, let's compare the execution time of `seq` versus `par` (taking the best result out of 5 runs):

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

`par` is nearly **60 times slower**. The reason is straightforward: adding 1,000 elements takes only a few microseconds in a single thread. However, `par` must initialize the TBB scheduler, split tasks, dispatch threads, and aggregate results for this call. This **fixed overhead** alone costs more than the entire sequential computation. The smaller the data volume, the larger the proportion of fixed overhead, and the more "loss" you incur from parallelization.

Let's summarize this conclusion: **Parallel fixed overhead is not zero; there is a break-even point**. For lightweight operations like `reduce`, this point might be in the hundreds of thousands or millions; for compute-intensive operations like `sort`, the threshold is lower. In practice, don't blindly add `par`. If you aren't sure about the data volume, either measure it yourself or just don't add it—sequential algorithms are always optimal for small data.

::: warning Don't add `par` just to look "modern"
A common misconception is seeing that the standard library supports parallel versions and blindly changing every `std::sort` to `std::sort(std::execution::par, ...)`. For containers with a few hundred or thousand elements, this change will likely slow down the code by dozens of times, while needlessly occupying the thread pool. `par` is for scenarios where the **data volume is large enough to warrant parallelization**, not a decoration.
:::

## The Cost of Parallelism: Stricter Requirements on Function Objects

Parallelization offers speed, but the cost is that its requirements for function objects are much **stricter** than the sequential version. The two core requirements are: **associativity** and (for unsequenced policies) **vectorization-safety**. Algorithms that don't meet these requirements will either produce incorrect results or cause compilation/runtime errors.

### `reduce` Requires Associativity: `accumulate` Doesn't, `reduce` Does

The most typical comparison is between `std::accumulate` and `std::reduce`. Both "merge a sequence of elements into a single value" and look almost identical, but their semantic requirements differ vastly:

- `std::accumulate` is a strict **left fold**—it calculates one by one from left to right, with a fixed evaluation order. Therefore, it **does not require** the binary operation to be associative.
- `std::reduce` allows the library to calculate in **any associative order** (this is the only way to split the work across multiple cores), so it **requires** the binary operation to be associative. The default `+` satisfies this, but for custom operations, you must guarantee it yourself.

This difference shows up immediately with floating-point numbers—floating-point addition **does not satisfy associativity**: `(a+b)+c` and `a+(b+c)` can yield different results in floating-point arithmetic. Therefore, feeding the same group of floats to `accumulate`, `reduce(seq)`, and `reduce(par)` will yield three different results:

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

All three results differ. Mathematically, the "correct answer" is 10,000 (adding 0.1 one hundred thousand times), but floating-point errors cause it to deviate. The extent of this deviation depends on the order of association. `accumulate` continuously adds small decimals to an increasingly large accumulated value, causing the error to accumulate most severely (a difference of 1.4). `reduce` splits the sequence into chunks, sums within the chunks, and then merges them. Since the values within the chunks are smaller, the error is smaller, resulting in a value closer to the true value. `reduce seq` and `reduce par` also differ because the splitting methods are different.

The essence of this issue is: **floating-point addition is not associative, so mathematically, it should not be parallelized**. The standard library allows you to do this (without raising an error), but the cost is that the result differs from the sequential version, and may even differ between runs (depending on thread scheduling). If your program requires **reproducibility** in floating-point results (e.g., bit-for-bit consistency in finance or scientific computing), using `reduce(par)` is a ticking time bomb. You must either sacrifice parallelism with `accumulate` or use compensated algorithms like Kahan summation.

Conversely, integer addition, bitwise operations, and logical AND/OR naturally satisfy associativity, making `reduce` safe for parallelization. Therefore, when determining "can I parallelize this reduce?", ask **whether your binary operation satisfies associativity**—not just about the data type.

::: warning The reduce(init, op) form also requires op to be commutative with init
For the `std::reduce(first, last, init, op)` four-parameter overload, besides requiring `op` to be associative, it also requires that both `op(init, x)` and `op(x, init)` are valid and produce the same result. This means `init` and the elements must be commutative under `op`. The reason is again parallel splitting: the library might combine `init` with any arbitrary chunk. When defining a custom `op`, if `init` is a special "identity element" type, ensure `op` behaves correctly in both positions.
:::

### Exceptions under `par` result in `std::terminate`

In sequential algorithms (including the `seq` policy), if a function object throws an exception, it propagates up normally, and you can `catch` it. However, under all parallel policies—`par`, `par_unseq`, and `unseq`—if an element access function throws an uncaught exception, the standard mandates a direct call to `std::terminate`, crashing the program. We verified this with a practical test:

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
        std::cout << "caught: " << e.what() << "\n";   // 这行不会被走到
    }
    return 0;
}
```

```text
std::terminate called (exception escaped par algorithm)
```

Notice that the outer `try/catch` block did not catch the exception—the exception never propagated out, and `terminate` was called immediately, terminating the process. This is a counter-intuitive difference between parallel and sequential algorithms: **under `par`, function objects must effectively not throw exceptions**. They must either be logically exception-free or marked `noexcept` and handle errors internally. Algorithms requiring error handling paths should either remain on `seq` or use mechanisms like `std::expected` or return values that do not rely on exceptions.

Why does this happen? Imagine an exception being thrown simultaneously across eight threads. Who aggregates them? Which exception should be propagated? The standard simply dictates no propagation and immediate termination to avoid this complexity. This is why function objects for parallel algorithms should be kept as simple and `noexcept` as possible.

## An unavoidable pitfall: libstdc++'s parallel backend is TBB, so you must link it manually

All previous examples included `-ltbb` during compilation. This is not optional—it is the key to successfully linking libstdc++ parallel algorithms, and it is the wall most beginners hit first.

libstdc++'s parallel algorithms (PSTL) rely on Intel TBB for thread scheduling. Therefore, as long as your code **uses** `std::execution::par` (even if it is just `reduce(par, ...)`), compilation will succeed, but linking will fail, reporting a long list of `undefined reference to tbb::...` errors. Try compiling the minimal `par` example from the beginning without `-ltbb`:

```text
/usr/bin/ld: ... undefined reference to `tbb::detail::r1::initialize(tbb::detail::d1::task_group_context&)'
... (几十行 TBB 符号未定义)
collect2: error: ld returned 1 exit status
```

Want to see why `par` depends on TBB? If we compile (without linking) and check the assembly, the `par` version (`sum_par`) is a string of `call __gnu_parallel`/`tbb` runtime symbols, while the `seq` version (`sum_seq`) is just a normal scalar loop:

<OnlineCompilerDemo
  title="Assembly of reduce seq vs par: TBB runtime calls"
  source-path="code/examples/vol3/46_parallel_asm.cpp"
  description="Compile only to view assembly (no run, so no -ltbb needed): seq version is a scalar accumulation loop, par version is a series of call __gnu_parallel/tbb — this is the evidence at the assembly level that the 'parallel backend is TBB'"
  allow-x86-asm
/>

The solution is just one line: add `-ltbb` to the compile command (assuming the system has TBB installed, locally `libtbb.so.12`). In a CMake project, this corresponds to `find_package(TBB REQUIRED)` followed by `target_link_libraries(... TBB::tbb)`.

::: warning Linking fails without -ltbb
As long as we use `par`/`par_unseq`, libstdc++ must link against TBB. `seq` and `unseq` do not use TBB (sequential and pure vectorization don't need a thread pool), so using just these two works without `-ltbb`. However, in practice, strategies are often swapped around, so it's easiest to just statically link `-ltbb` in the project. This is why all examples in this article containing `par` use the compile command `g++ -std=c++20 -O2 xxx.cpp -ltbb`.
:::

A quick comparison: another mainstream standard library implementation, libc++ (the Clang suite), uses a different default parallel backend and does not depend on TBB; MSVC's parallel algorithms are also plug-and-play and require no extra linking. So "whether to link TBB" is a libstdc++-specific issue to watch out for when migrating code.

## C++17 Background and C++20's `unseq`

Parallel algorithms only entered the standard in C++17 (originating from the earlier Parallelism TS). Before that, parallel sorting meant hand-rolling threads or using third-party libraries (TBB, OpenMP). C++17 added execution policy overloads to over 60 algorithms in `<algorithm>` and merge/scan algorithms in `<numeric>` in one go, plus a batch of new algorithms designed for parallelism like `reduce`, `transform_reduce`, and `inclusive_scan` (the old `accumulate` doesn't require associativity and cannot be safely parallelized, hence the separate addition).

C++20 added a fourth policy, `unseq` — single-threaded vectorization (pure SIMD). The design motivation is: sometimes we don't want to start multiple threads (e.g., single-core embedded systems, or data volumes too small to justify threading overhead), but we still want the compiler to vectorize the loop and use SIMD instructions. `par_unseq` can also vectorize, but it spawns threads; `unseq` extracts "vectorization" to give us a "no threads, just SIMD" option.

However, the actual effect of `unseq` in libstdc++ is often disappointing. Running cppreference's own example (g++ -std=c++23 -O3 -ltbb) to sort 1 million elements with four policies yields: seq 165ms, unseq 163ms, par_unseq 30ms, par 27ms. See that? **`unseq` is barely faster than `seq`** — 163 vs 165, basically within the margin of error. The reason is that vectorization offers limited gains for operations like integer comparison; the SIMD channels aren't fed efficiently. `unseq` truly shines in highly regular, branch-free, compute-intensive element-wise operations (e.g., per-element math on floating-point arrays), so in practice, we must test per scenario.

So, the correct expectation for `unseq` is: **it is a hint to "request vectorization" and does not guarantee a speedup**. Just like `par`, whether it's worth it depends on measurement — don't be superstitious.

## Summary

For parallel algorithms, the easiest part to learn is "how to add policy parameters," and the easiest pitfall is "thinking adding `par` guarantees speed." Let's wrap up the key conclusions:

- The four policies, from conservative to aggressive: `seq` (single-threaded sequential), `par` (multi-threaded, no interleaving within a thread, can lock), `par_unseq` (multi-threaded + vectorization, interleaving allowed within a thread, **cannot lock**), `unseq` (C++20, single-threaded vectorization, also cannot lock).
- Because `par_unseq` and `unseq` allow interleaving within a single thread, function objects prohibit any vectorization-unsafe operations: locking, non-lock-free atomics, or even `new`/`delete`. For parallel scenarios requiring locks, stick to `par`.
- **When parallelization actually speeds things up**: Significant speedup for compute-bound algorithms (e.g., sort, 5x+ locally); limited speedup for memory-bound algorithms (e.g., reduce, 1.5x locally) due to memory bandwidth limits; for small data sizes where fixed overhead dominates, `par` can be dozens of times slower.
- **`reduce` requires associativity, `accumulate` does not**. Floating-point addition is not associative, so floating-point `reduce(par)` results differ from the sequential version and may even vary between runs — avoid this if reproducibility is required.
- In `par` and below policies, if a function object throws an exception, it calls `std::terminate` directly and won't be caught by outer `try/catch` — function objects for parallel algorithms should be effectively `noexcept`.
- **libstdc++'s parallel backend is TBB**: As long as `par`/`par_unseq` is used, `-ltbb` must be added to the compile command, or linking fails; libc++ and MSVC have no such requirement.
- `unseq` is a hint to "request vectorization" and often shows almost no speedup for operations like integer comparison — don't be superstitious.

In the next article, we'll look at the standard library from another angle — `<chrono>` and time handling, which is another facility that "looks simple but is full of pitfalls."

## References

- [cppreference: Execution policy tags](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag) — Definitions of the four policy objects `seq`/`par`/`par_unseq`/`unseq`
- [cppreference: Execution policy types](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) — Precise semantics of how each policy type schedules element access functions (indeterminately sequenced vs unsequenced, locking permissions, exception behavior)
- [cppreference: std::reduce](https://en.cppreference.com/w/cpp/numeric/reduce) — Associativity requirements of `reduce` and its relationship with `accumulate`
- [cppreference: Parallel algorithms](https://en.cppreference.com/w/cpp/algorithm) — Overview of all algorithms with execution policy overloads since C++17
- [GCC libstdc++ manual: Parallel algorithms](https://gcc.gnu.org/onlinedocs/libstdc++/manual/parallel.html) — Documentation that libstdc++ parallel backend depends on TBB
