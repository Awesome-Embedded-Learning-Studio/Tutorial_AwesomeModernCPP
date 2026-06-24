---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'Here is the translation following the provided rules and style guide:


  A Deep Dive into `<random>`: Why `rand()` Should Be Retired (limitations include
  small `RAND_MAX`, non-reproducible cross-platform results, thread safety issues,
  and lack of distribution control). We explore the trio of engines, distributions,
  and devices in `<random>`, demonstrate the correct way to seed `mt19937` with `std::random_device{}()`,
  verify the uniformity of `mt19937` and `uniform_int_distribution` through testing,
  and use `thread_local` engines to avoid contention in multi-threaded environments.'
difficulty: intermediate
order: 60
platform: host
prerequisites:
- 算法总览（上）：非修改式、修改式与查找
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 15
related:
- numeric 算法：累加、前缀和与 midpoint
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'Random: Why You Should Stop Using rand()'
translation:
  source: documents/vol3-standard-library/time-numeric/60-random.md
  source_hash: 9a4aa065735385dd8be0b7d7ce7c7907aba0ba7c7a650c441717ca22e9729d73
  translated_at: '2026-06-24T04:28:33.308489+00:00'
  engine: anthropic
  token_count: 3517
---
# random: Why You Should Stop Using rand()

Almost every C tutorial stops at `srand(time(NULL)); rand() % N;` when it comes to random numbers. It runs, it spits out results, so everyone keeps using it—until the day your simulation results silently change after switching machines, your multi-threaded program crashes under stress testing, or you need to generate normally distributed noise but have no idea how to derive it from the uniform integers between 0 and `RAND_MAX` provided by `rand()`.

The standard answer provided by C++11 is a brand new header `<random>`. It breaks down "generating random numbers" into three independent, freely composable components: **engines** (which generate raw uniform unsigned integers), **distributions** (which map those raw integers into the shape you actually want—uniform, normal, Bernoulli, etc.), and **devices** (which provide non-deterministic, true random seeds). In this post, we will thoroughly dissect this trio, and use concrete benchmarks to clarify exactly where `rand()` falls short and why it should be retired. A quick scope limitation: **cryptographically secure random numbers are not discussed in this article**—`std::random_device` is not designed for cryptography; for key generation, please use dedicated cryptographic libraries (like OpenSSL or libsodium).

## The Indictments of rand(), Tested One by One

Let's set up the target first. `rand()` isn't so terrible that it's unusable; modern glibc's implementation is actually a linear congruential generator (additive feedback generator), and it looks okay for simple distribution statistics. However, it has several structural flaws, each of which will bite you in real-world engineering.

### Charge 1: RAND_MAX is Too Small, and Implementation-Dependent

First, let's look at the local `RAND_MAX`:

```cpp
// Standard: C++20
#include <cstdio>
#include <cstdlib>

int main()
{
    std::printf("RAND_MAX = %d\n", RAND_MAX);
    return 0;
}
```

```text
RAND_MAX = 2147483647
```

`2147483647`, which is $2^{31} - 1$, provides 31 bits. This is the case on the local machine (Linux, glibc), but the C standard only guarantees that `RAND_MAX` is at least `32767`—that is, $2^{15} - 1$, **which is only 15 bits**. This means that the same line of `rand()` can produce at most 32,768 distinct values on a standard-compliant implementation. If you want to piece together a 64-bit seed from a single `rand()` (for example, to initialize a PRNG with a huge state space), you must call it several times and shift the results together. Furthermore, there are correlations between calls, making the code ugly and error-prone.

Engines in `<random>` do not suffer from this flaw: `std::mt19937` directly generates 32-bit unsigned integers, where `min()` is `0` and `max()` is `4294967295` ($2^{32} - 1$). This is a full 32 bits, explicitly defined by the type, and consistent across platforms.

### Flaw 2: Non-reproducible across platforms and implementations

Running the following code on two different machines with different compilers or standard libraries will yield different results:

```cpp
std::srand(12345);
// 前 5 个值
```

Here is the output on our local machine (GCC 16.1.1 / glibc):

```text
srand(12345) 前5: 383100999 858300821 357768173 455282511 133005921
```

The algorithm used by `rand()` in the C standard is **implementation-defined**—glibc uses an additive feedback generator, MSVC's CRT uses a different LCG, and BSD uses yet another. With the same seed `12345`, running on Windows with MSVC yields a completely different sequence. This is fatal for "simulation, testing, and replaying matches": you cannot simply give a seed to someone else to reproduce your random sequence, nor can you reproduce a bug driven by random numbers.

In contrast, `std::mt19937` is a mathematically deterministic algorithm (Mersenne Twister, MT19937) strictly defined by the standard. **The same seed produces the exact same sequence on any compliant implementation**:

```cpp
// Standard: C++20
#include <cstdio>
#include <random>

int main()
{
    std::printf("mt19937(12345) 前5: ");
    std::mt19937 eng(12345);
    for (int i = 0; i < 5; ++i) std::printf("%u ", eng());
    std::printf("\n");
    return 0;
}
```

```text
mt19937(12345) 前5: 3992670690 3823185381 1358822685 561383553 789925284
```

Paste this snippet to a colleague using Clang with libc++, or to one using MSVC, and the results will be identical. This is exactly what "reproducible randomness" should look like.

### Charge Three: Thread Safety

`rand()` maintains an internal state at the process level, reading from and writing to it on every call. The C standard does not guarantee that this state is **thread-safe**—calling `rand()` concurrently from multiple threads constitutes a data race and results in undefined behavior. POSIX adds a lock to glibc's `rand()`, so the following code "appears to work" on a local Linux machine:

```cpp
// Standard: C++20
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main()
{
    std::atomic<int> total{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([&]() {
            for (int i = 0; i < 100000; ++i) {
                total += std::rand() & 1;   // 0 或 1
            }
        });
    }
    for (auto& th : ts) th.join();
    std::printf("4 线程各取 100000 次奇偶，1 的总数 = %d\n", total.load());
    return 0;
}
```

```text
4 线程各取 100000 次奇偶，1 的总数 = 199624
```

It runs, and the result looks correct. But don't be fooled: this is glibc covering for you, not a guarantee from the C standard. Switch to a non-locking implementation (like a stripped-down libc on some embedded platforms), and this code becomes a data race immediately. At best, the quality of your random numbers collapses; at worst, the program crashes. "It works on my machine" doesn't hold water here.

Every engine object in `<random>` **carries its own state**. Create as many as you need. As long as threads don't share the same object, there is naturally no contention. Later, we will demonstrate the standard idiom of using `thread_local` to give every thread its own engine.

### Charge 4: No direct way to express "distributions"

`rand() % N` only gives you a **uniform integer** from 0 to N-1. But what if you want "normally distributed noise with a mean of 0 and a standard deviation of 1"? What about "a boolean value that is true with 0.7 probability"? Or "a floating-point number in the [0, 1) range"?

With `rand()`, you have to write it yourself—normal distributions need the Box-Muller transform, floating-point numbers require dividing by `RAND_MAX` while watching out for precision loss, and Bernoulli trials need `rand() < p * RAND_MAX`. None of these are hard, but each requires you to implement, test, and validate it yourself. Plus, if you switch PRNGs, you have to do it all over again.

`<random>` encapsulates all the mathematics of "turning a uniform integer into a target distribution" into **distribution objects**. Engines are engines, distributions are distributions, and they can be combined freely. In the next section, we will break down this mechanism completely.

::: warning Clarifying the old charge of "low bits aren't random"
Many older resources emphasize that `rand() % N` has "highly non-random low bits with periodic patterns." This **was historically true for certain implementations**—the most naive Linear Congruential Generator (LCG) has a lowest bit that strictly alternates `0,1,0,1...` (period 2), the next bit has a period of 4, and so on. Taking the modulus exposes these low bits. However, modern glibc's `rand()` hasn't been a simple LCG for a long time. In our tests, `rand()%2` showed adjacent equal values about half the time in 100,000 samples (`49945 / 100000`), so it doesn't suffer from "strict alternation." A more accurate statement is: **`rand()`'s low bit quality depends on the implementation and is not guaranteed by the standard**. You shouldn't base your program's correctness on "the low bits of `rand` on my platform happen to be okay." Using `mt19937` + `uniform_int_distribution` eliminates this uncertainty from the start.
:::

## The `<random>` Trio: Engine, Distribution, and Device

Now that we've covered the pain points, let's look at the right tools. The design philosophy of `<random>` can be summed up in one sentence: **Separate "where the randomness comes from" from "what shape the randomness has."**

- **Engine**: Responsible only for spitting out raw, uniformly distributed unsigned integers. `mt19937`, `minstd_rand`, and `ranlux24` are all engines. It is a stateful object; each call to `eng()` advances the state and returns a value.
- **Distribution**: Maps the raw integers from the engine into the target distribution you want. `uniform_int_distribution`, `normal_distribution`, `bernoulli_distribution`... It is **stateless** (mostly), acting simply as a function object: `dist(eng)`.
- **Device**: `std::random_device`, which accesses an external entropy source (usually `/dev/urandom` on Linux) to spit out **non-deterministic** values, specifically used to seed the engine.

The division of labor is clean: the engine determines "how long the period is, how uniform, and how fast," the distribution decides "what shape these numbers are molded into," and the device decides "where to get an unpredictable starting point." If you want a normal distribution, the flow is "device seeds an engine, engine feeds a normal distribution"—three independent, replaceable steps.

### Engine: Why mt19937 is the default choice

The standard library provides a bunch of engines, but the most common one is `std::mt19937`. It is the 32-bit version of the Mersenne Twister algorithm. The 19937 in its name comes from its period length—`2^19937 - 1`. This is an astronomical number; you will never exhaust the period during your program's lifetime. Its internal state is 624 32-bit words (`std::mt19937::state_size == 624` on my machine). It offers good quality, high speed, and statistically verified properties, making it sufficient for the vast majority of scenarios.

Two other engine families you might occasionally encounter:

- `std::linear_congruential_engine`: Linear congruential generators, the old `x = a*x + c mod m` method. `minstd_rand0` / `minstd_rand` are predefined instances. Small state (one integer), fast, but average quality. Only consider these for scenarios where "state must be tiny" and statistical quality requirements are low (e.g., certain embedded constraints).
- `std::subtract_with_carry_engine`: Subtract-with-carry generators (Lagged Fibonacci). `ranlux24` / `ranlux48` are predefined instances. They have good statistical properties in some cases, but `mt19937` is still the default choice.

A practical detail: `mt19937`'s constructor takes a single 32-bit seed, but its state space is 624 words wide with a period of `2^19937`. Feeding it just a 32-bit seed means you are only picking from `2^32` possible starting states, drastically compressing the reproducible "starting points." If you care about this (e.g., running long simulations and worried about seed collisions), the standard library provides `std::seed_seq`. You can feed multiple seed words into it to fill the engine's initial state. A single seed is usually enough for daily use, but it's good to know this advanced option exists.

### Distribution: Molding uniform integers into the shape you need

Distributions are where `<random>` really saves you headaches. The engine spits out uniform integers in `[0, 2^32-1]`, but that is almost never what you want. Distribution objects handle this mapping and, crucially, **handle modulo bias correctly**—a pitfall you might hit with hand-written `eng() % N` but which `uniform_int_distribution` avoids automatically.

Let's look at the most common distributions, running one million samples each:

```cpp
// Standard: C++20
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

int main()
{
    std::mt19937 eng(2024);

    // 正态分布: 均值 0, 标准差 1
    std::normal_distribution<double> norm(0.0, 1.0);
    constexpr int N = 1000000;
    double sum = 0, sum2 = 0;
    std::vector<long long> hist(10, 0);   // [-5, 5) 分 10 桶，每桶宽 1.0
    for (int i = 0; i < N; ++i) {
        double x = norm(eng);
        sum += x; sum2 += x * x;
        int b = int(x + 5.0);
        if (b >= 0 && b < 10) ++hist[b];
    }
    double mean = sum / N;
    double stddev = std::sqrt(sum2 / N - mean * mean);
    std::printf("normal(0,1) %d 样本: 均值=%.4f 标准差=%.4f\n", N, mean, stddev);
    std::printf("直方图(每桶宽1.0, [-5,5)):\n");
    for (int i = 0; i < 10; ++i) {
        std::printf("  [%+.0f,%+.0f) %lld\n", i - 5.0, i - 4.0, hist[i]);
    }

    // 伯努利: p=0.7
    std::bernoulli_distribution bern(0.7);
    long long trues = 0;
    for (int i = 0; i < N; ++i) if (bern(eng)) ++trues;
    std::printf("bernoulli(0.7) %d 样本: true 占比=%.4f\n", N, double(trues) / N);

    // 均匀实数: [0, 1)
    std::uniform_real_distribution<double> ureal(0.0, 1.0);
    double usum = 0;
    for (int i = 0; i < N; ++i) usum += ureal(eng);
    std::printf("uniform_real(0,1) %d 样本: 均值=%.4f (期望 0.5)\n", N, usum / N);

    return 0;
}
```

```text
normal(0,1) 1000000 样本: 均值=-0.0005 标准差=1.0002
直方图(每桶宽1.0, [-5,5)):
  [-5,-4) 29
  [-4,-3) 1346
  [-3,-2) 21417
  [-2,-1) 136208
  [-1,+0) 340726
  [+0,+1) 341356
  [+1,+2) 136132
  [+2,+3) 21484
  [+3,+4) 1269
  [+4,+5) 33
bernoulli(0.7) 1000000 样本: true 占比=0.7008
uniform_real(0,1) 1000000 样本: 均值=0.5000 (期望 0.5)
```

Want to see the distribution statistics in action? Check out this online demo (completes in 0.04 seconds):

<OnlineCompilerDemo
  title="<random> Distribution Demo: normal / bernoulli / uniform"
  source-path="code/examples/vol3/60_random_distributions.cpp"
  description="Feeding mt19937 into three distributions with one million samples each: normal shows a bell-shaped histogram, bernoulli(0.7) checks the true ratio, and uniform_real(0,1) checks if the mean approaches 0.5—things rand()%N can't do"
  allow-run
/>

Several things are immediately obvious. The normal distribution histogram is a beautiful bell curve; the two middle buckets `[-1, +1)` each have around 340,000 hits, decaying symmetrically outwards. The mean is `-0.0005` and the standard deviation is `1.0002`, almost exactly the nominal `(0, 1)`. The true ratio for Bernoulli `0.7` is `0.7008`. The mean of the uniform real numbers `[0, 1)` is `0.5000`. These are all tasks you would have to implement and verify yourself with `rand()`, but `<random>` handles them for you, and correctly.

The most critical difference lies in `uniform_int_distribution`. You might think: isn't a uniform integer just `eng() % N`? If you actually write that, you fall into the modulo bias trap. The reason is: the value range produced by the engine is `[0, 2^32-1]`, totaling `2^32` values, and `2^32` is not necessarily divisible by your `N`. For example, if `N=3`, `2^32 = 4294967296 = 3 * 1431655765 + 1`. There is a remainder of 1, meaning bucket 0 gets one more candidate value than buckets 1 and 2, so the distribution is no longer uniform (the bias is small, but objectively exists). `uniform_int_distribution` internally uses rejection sampling to discard this "extra tail" and redraw, guaranteeing strictly equal probability for every bucket. On platforms where `RAND_MAX` is only 15 bits, this bias is quite noticeable; on glibc's 31-bit `rand()`, it is too small to detect—but "relying on platform charity" is one of the reasons `rand()` needs to be retired.

We tested `rand()%3` versus `uniform_int_distribution(0,2)` with 300 million samples each. Here are the bucket hits:

```text
rand()%3 取 300000000 样本:
  bucket 0: 100009515
  bucket 1: 99993723
  bucket 2: 99996762
mt19937+uniform_int(0,2) 取 300000000 样本:
  bucket 0: 99999397
  bucket 1: 99992920
  bucket 2: 100007683
```

On glibc, both are within the noise margin (on the order of one ten-thousandth), indicating that the modulo bias of the native `rand()` is indeed small. However, the conclusion is not that "`rand()%3` is fine," but rather that "it happens to be fine on this machine, but might not be on another platform"—`uniform_int_distribution` saves you from worrying about this from the start.

## Correct Approach: A Clean One-Liner Template

Putting it all together, the standard, portable, and unbiased way to "generate a uniform integer from 1 to 100" boils down to this single core line:

```cpp
// Standard: C++20
#include <cstdio>
#include <random>

int main()
{
    std::random_device rd;                       // 1. 设备：非确定种子
    std::mt19937 eng(rd());                      // 2. 引擎：用种子初始化
    std::uniform_int_distribution<int> dist(1, 100);  // 3. 分布：[1, 100] 闭区间

    std::printf("rd{}() 种 mt19937 + uniform(1,100) 10 个: ");
    for (int i = 0; i < 10; ++i) std::printf("%d ", dist(eng));
    std::printf("\n");
    return 0;
}
```

```text
rd{}() 种 mt19937 + uniform(1,100) 10 个: 97 60 100 11 25 17 57 16 43 86
```

These three steps correspond to the standard trio: use `random_device` to obtain an unpredictable seed, initialize `mt19937` with it, and use `uniform_int_distribution` to map the engine's output to your desired closed interval `[1, 100]`. Note that the interval for `uniform_int_distribution` is a **closed interval** (both endpoints are inclusive), which differs from the half-open intervals found in many other languages. Writing `dist(1, 100)` means you can actually roll a 100.

A more compact approach is to use a temporary object to get the seed directly: `std::mt19937 eng(std::random_device{}());`. Here, `random_device{}` constructs a device object, `()` invokes it once to fetch a value, and the entire expression serves as the constructor argument for `eng`. This one-liner is extremely common in both tutorials and production code, so just memorize it.

If you need **reproducibility** (for testing or simulation replay), replace the `random_device` step with a fixed seed: `std::mt19937 eng(42);`. This locks the sequence, ensuring it is consistent across platforms. Whether or not you need reproducibility is the only branching point in this setup; everything else remains the same.

::: warning random_device is not cryptographically secure
Most implementations of `std::random_device` read from `/dev/urandom` and are of high quality, but the **standard allows it to degenerate into a deterministic pseudo-random number generator** (historically, some versions of MinGW did exactly this, returning something akin to `rand()`). Furthermore, it is **not cryptographically secure**. For security-sensitive scenarios like generating keys, tokens, or salts, use a dedicated cryptography library (such as OpenSSL's `RAND_bytes` or libsodium's `randombytes_buf`), not `<random>`.
:::

## Multi-threaded Randomness: One thread_local Engine per Thread

The final frequent pain point. When generating random numbers in a multi-threaded program, the cardinal sin is **sharing a single engine object across multiple threads**. Engines have state; concurrent calls result in data races, leading to either poor performance (due to locking) or undefined behavior (UB).

The correct solution is to give each thread its own independent engine, stored using `thread_local`. Each thread automatically possesses its own engine and state upon entry, isolated from others without requiring locks:

```cpp
thread_local std::mt19937 eng(std::random_device{}());
```

```cpp
// Standard: C++20
#include <atomic>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

// 每个线程一个独立引擎，thread_local 保证线程私有
thread_local std::mt19937 tl_eng{std::random_device{}()};

int main()
{
    std::atomic<int> total{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([&]() {
            std::uniform_int_distribution<int> dist(1, 100);
            for (int i = 0; i < 100000; ++i) total += dist(tl_eng);
        });
    }
    for (auto& th : ts) th.join();
    std::printf("4 线程各取 100000 次 uniform(1,100), 总和=%d\n", total.load());
    std::printf("期望均值约 50.5 * 400000 = %d\n", (int)(50.5 * 400000));
    return 0;
}
```

```text
4 线程各取 100000 次 uniform(1,100), 总和=20196410
期望均值约 50.5 * 400000 = 20200000
```

Four threads each drawing 100,000 times yields a total of approximately 20.2 million, matching the expected `50.5 * 400000 = 20200000`. There are a few details worth highlighting here.

First, `thread_local std::mt19937` is initialized with `random_device{}()`. This means **each thread fetches a true random seed the first time it accesses the generator**. Consequently, different threads start from different points and produce different sequences, avoiding the awkward situation where "all threads walk the same random sequence."

Second, the distribution object `dist` is constructed inside the lambda but outside the loop. Distributions are basically stateless, so we construct it once and call it repeatedly. Do not write it inside the inner loop to construct it on every iteration (although the overhead is small, it is unnecessary). Here, `dist` is a local variable for each thread, so there are no sharing issues.

Third, `thread_local` is not a free lunch: the first access by each thread triggers the construction of the engine (including one system call for `random_device` and initialization of the 624-byte state of mt19937), which incurs a one-time overhead. Therefore, it is suitable for scenarios where "this thread will repeatedly fetch random numbers." If a thread only fetches a random number once or twice before exiting, using `thread_local` is not cost-effective; simply constructing a local engine is sufficient.

## Common Real-World Pitfalls

Let's consolidate the places where things easily go wrong; every point here has been verified through the practical tests above:

::: warning mt19937 Single Seed Covers Only 2^32 Starting Points
`std::mt19937 eng(seed)` accepts only one 32-bit seed, but its state space is `2^19937`. Using a single seed means that `2^32` program instances each walk a disjoint trajectory in the `2^19937` state space. This is sufficient for most applications, but if you need to run massive independent simulations simultaneously and are concerned about starting point collisions, use `std::seed_seq` to fill multiple seed words for initialization to fully cover the starting space.
:::

::: warning uniform_int_distribution is a Closed Interval
The range of `uniform_int_distribution<int>(1, 100)` is `[1, 100]`, **closed on both ends**; 100 can be drawn. This is the same as Python's `random.randint`, but opposite to many half-open interval APIs (the opposite of `randint`, and `std::uniform_real_distribution`'s half-open `[a, b)`). Before using it, confirm clearly whether you need a closed or half-open interval, and don't write code based on habits from other languages.
:::

::: warning Don't Repeatedly Construct Distributions or Engines in Loops
Engine construction is expensive (mt19937 needs to initialize 624 bytes of state), and distribution construction, while cheap, is not zero-cost. Move them outside the loop; try to move the engine to the lifecycle of the function or class, and construct the distribution once for repeated use as needed. Writing `std::mt19937 eng(...)` inside the inner loop of a hot path is a common performance pitfall for newcomers.
:::

::: warning Sharing an Engine Across Threads is a Data Race
Engines have state. Calling the same engine object from multiple threads without a lock is undefined behavior (UB). Either use `thread_local` for one engine per thread, or protect it with a mutex (which becomes a bottleneck). Default to `thread_local`.
:::

::: warning random_device May Degrade to Pseudo-Random
The standard allows `std::random_device` to degrade to a deterministic generator when no true entropy source is available, and it is not cryptographically secure. It is generally fine for seeding purposes, but for security-sensitive scenarios, use a dedicated cryptography library.
:::

## Summary

The philosophy of `<random>` in one sentence: **Decouple "where randomness comes from" (engine), "what shape the randomness takes" (distribution), and "where the starting point comes from" (device).** Let's recap the key conclusions:

- The real reason `rand()` should be retired isn't "how bad the distribution is on modern glibc" (tests show it's actually passable), but rather **`RAND_MAX` is small and inconsistent across implementations, the algorithm is implementation-defined causing non-reproducibility across platforms, it is not thread-safe at the standard level, and it cannot directly express distributions**—each of these is a structural issue that bites in real engineering.
- Division of labor for the trio: the engine (`mt19937` is most common, period `2^19937-1`, state 624 bytes) spits out uniform unsigned integers; the distribution (`uniform_int`/`uniform_real`/`normal`/`bernoulli`, etc.) shapes it into the target form; the device (`random_device`) provides a non-deterministic seed.
- Correct core line: `std::random_device rd; std::mt19937 eng(rd()); std::uniform_int_distribution<int> dist(1, 100);` — swap `rd()` for a fixed integer seed if reproducibility is needed.
- `uniform_int_distribution` uses rejection sampling internally to eliminate the modulo bias of `eng() % N`, and the interval is **closed**—these are the two points most prone to error in manual implementations.
- Use `thread_local std::mt19937` for one engine per thread in multithreading to avoid data races from shared state; do not repeatedly construct engines in hot paths.
- `random_device` is not cryptographically secure; use dedicated cryptography libraries for keys/tokens.

In the next post, we will switch topics and look at another set of standard library facilities for "transforming data," outside of `<random>`.

## References

- [cppreference: `<random>`](https://en.cppreference.com/w/cpp/numeric/random) — Overview and complete directory of engines/distributions/devices
- [cppreference: std::mt19937](https://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine) — Mersenne Twister engine, `state_size`, and period
- [cppreference: std::uniform_int_distribution](https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution) — Closed interval and rejection sampling to eliminate modulo bias
- [cppreference: std::random_device](https://en.cppreference.com/w/cpp/numeric/random/random_device) — Non-deterministic entropy source and implementation degradation notes
- [cppreference: std::rand](https://en.cppreference.com/w/cpp/numeric/random/rand) — Thread safety and cross-implementation differences of `rand()`
