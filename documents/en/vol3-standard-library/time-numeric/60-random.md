---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: A thorough tour of <random>—why rand() deserves retirement (small RAND_MAX,
  not reproducible across platforms, not thread-safe, no way to control distributions),
  the <random> trio of engines/distributions/devices, the correct way to seed mt19937
  with the one-liner std::random_device{}(), measured uniformity of mt19937 plus
  uniform_int_distribution, and a thread_local engine per thread to avoid races in
  multithreaded code
difficulty: intermediate
order: 60
platform: host
prerequisites:
- 'Algorithm Overview (Part 1): Non-Modifying, Modifying, and Searching — How to Pick the Right Algorithm for a Problem'
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
reading_time_minutes: 15
related:
- 'numeric: Accumulate, Fill, Inner Product, and Adjacent Difference'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'random: Why You Should Stop Using rand()'
translation:
  source: documents/vol3-standard-library/time-numeric/60-random.md
  source_hash: 9a4aa065735385dd8be0b7d7ce7c7907aba0ba7c7a650c441717ca22e9729d73
  translated_at: '2026-09-26T00:17:49+00:00'
  engine: anthropic
  token_count: 5500
---

# random: Why You Should Stop Using rand()

Nearly every C tutorial stops teaching randomness at the same line: `srand(time(NULL)); rand() % N;`. It runs, it produces results, so everyone keeps doing it that way—until the day your simulation results quietly change after you move to a different machine, your multithreaded program crashes intermittently under load testing, or you need to generate normally distributed noise and have no idea how to coax it out of `rand()`'s uniform integers between 0 and `RAND_MAX`.

C++11's answer is a brand-new header, `<random>`, which splits "generating random numbers" into three independent, freely combinable parts: **engines** (produce raw uniform unsigned integers), **distributions** (map those raw integers into the shape you actually want—uniform, normal, Bernoulli, ...), and **devices** (supply non-deterministic, truly random seeds). In this article we take the trio apart and cover it thoroughly, and along the way we settle—with hands-on measurements—exactly where `rand()` falls short and why it deserves retirement. One scope note up front: **cryptographically secure randomness is out of scope for this article**—`std::random_device` isn't designed for cryptography either; for key generation, use a dedicated cryptographic library (OpenSSL, libsodium, that kind of thing).

## The Charges Against rand(), Verified One by One

First, let's set up the target. `rand()` isn't "so bad it's unusable"—modern glibc's `rand()` is in fact an additive feedback generator, and it looks decent enough when you merely run distribution statistics on it. But it has several structural flaws, and every one of them will bite you in real engineering.

### Charge 1: RAND_MAX Is Too Small, and Varies Across Implementations

First, check `RAND_MAX` on this machine:

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

`2147483647` is `2^31 - 1`—31 bits. That's what we get on this machine (Linux, glibc), but the C standard only guarantees that `RAND_MAX` is at least `32767`—`2^15 - 1`, a **mere 15 bits**. In other words, that same line of `rand()` can produce at most 32768 distinct values on a conforming implementation. If you want to assemble a 64-bit seed out of `rand()` calls (say, to initialize a PRNG with a huge state space), you need several calls plus shifting to stitch the bits together, and successive calls are correlated on top of that—ugly to write and easy to get wrong.

Engines in `<random>` don't have this problem: `std::mt19937` produces 32-bit unsigned integers directly, `min()` is `0`, `max()` is `4294967295` (`2^32 - 1`)—the full 32 bits, in black and white in the type, consistent across platforms.

### Charge 2: Not Reproducible Across Platforms or Implementations

Run the following on two machines with different compilers/standard libraries, and the results will differ:

```cpp
std::srand(12345);
// first 5 values
```

On this machine (GCC 16.1.1 / glibc), we get:

```text
srand(12345) 前5: 383100999 858300821 357768173 455282511 133005921
```

The algorithm behind `rand()` is **implementation-defined** in the C standard—glibc uses one kind of additive feedback generator, MSVC's CRT uses a different LCG, BSD uses yet another. The same seed `12345` produces a completely different sequence under MSVC on Windows. That's fatal for "simulations, tests, game replays": you can't hand someone a seed and have them reproduce your random sequence, which means you also can't reproduce a bug driven by random numbers.

Contrast that with `std::mt19937`: it is a mathematically fully determined algorithm (Mersenne Twister, MT19937), pinned down by the standard—**the same seed produces exactly the same sequence on any conforming implementation**:

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

Paste this sequence to a colleague on Clang+libc++ or one on MSVC, and their runs match to the digit. That's what "reproducible randomness" is supposed to look like.

### Charge 3: Not Thread-Safe

`rand()` maintains one process-wide piece of internal state that every call reads and writes. Under the C standard this state is **not thread-safe**—calling `rand()` from multiple threads at once is a data race, which is undefined behavior. POSIX layers a lock on top for glibc's `rand()`, so the following "appears to work" on our Linux machine:

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
                total += std::rand() & 1;   // 0 or 1
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

It runs, and the result looks right. But don't be fooled: that's glibc catching you, not a guarantee the C standard gives you. Switch to an implementation without the lock (some minimal libc on embedded platforms, for instance) and this exact code is a data race—at best the random quality collapses, at worst it crashes. "It works on my machine" carries no weight here.

In `<random>`, every engine object **carries its own state**—spin up as many as you like; as long as threads don't share the same object, there's no race by construction. Later we'll demonstrate the standard pattern of one engine per thread via `thread_local`.

### Charge 4: No Way to Express Distributions Directly

`rand() % N` gives you exactly one thing: **uniform integers** from 0 to N-1. But what if what you want is "normally distributed noise with mean 0 and standard deviation 1"? A boolean that is true with probability 0.7? A floating-point value in the interval [0, 1)?

With `rand()` you have to build all of that yourself—a normal distribution needs the Box-Muller transform, floats need division by `RAND_MAX` plus care with precision, Bernoulli needs `rand() < p * RAND_MAX`. None of it is hard, but every piece has to be hand-implemented, hand-tested, and hand-verified, and the moment you swap in a different PRNG you start over.

`<random>` wraps all of that "turn uniform integers into a target distribution" math into **distribution objects**. Engines stay engines, distributions stay distributions—combine them freely. The next section covers this machinery in depth.

::: warning Let's also settle the old "low bits are not random" charge
Plenty of older material stresses that `rand() % N` has "highly non-random low bits with periodic patterns". **Historically, and for certain implementations, that's true**—the lowest bit of the plainest linear congruential generator (LCG) strictly alternates `0,1,0,1...` (period 2), the next-lowest bit has period 4, and so on, and taking a modulo exposes exactly those low bits. But modern glibc's `rand()` stopped being a simple LCG long ago: in our own test, `rand()%2` over 100,000 samples produced adjacent-equal values about half the time (`49945 / 100000`)—no "strict alternation" pathology. So the accurate statement is: **the low-bit quality of `rand()` depends on the concrete implementation, and the standard guarantees nothing**—you shouldn't rest your program's correctness on "the low bits of my platform's rand happen to be okay". Using `mt19937` + `uniform_int_distribution` removes that uncertainty from the start.
:::

## The `<random>` Trio: Engines, Distributions, and Devices

With the pain points covered, let's see what the right tool looks like. `<random>`'s design philosophy in one sentence: **separate "where the randomness comes from" from "what shape the randomness takes"**.

- **Engines**: do exactly one thing—spit out raw, uniformly distributed unsigned integers. `mt19937`, `minstd_rand`, `ranlux24` are all engines. An engine is a stateful object; each call to `eng()` advances the state one step and returns a value.
- **Distributions**: map the engine's raw integers into the target distribution you want. `uniform_int_distribution`, `normal_distribution`, `bernoulli_distribution`, ... A distribution is itself **stateless** (the overwhelming majority are), just a function object: `dist(eng)`.
- **Devices**: `std::random_device` reaches an external entropy source (on Linux, usually `/dev/urandom`) and produces **non-deterministic** values—its whole job is seeding engines.

The division of labor is clean: the engine decides "how long the period, how uniform, how fast"; the distribution decides "what shape these numbers get molded into"; the device decides "where to get an unpredictable starting point". Want a normal distribution? A device seeds an engine, the engine feeds the normal distribution—three steps, each independent and swappable.

### Engines: Why mt19937 Is the Default Choice

The standard library ships a whole pile of engines, but the one you'll actually use is `std::mt19937`. It is the 32-bit version of the Mersenne Twister algorithm; the 19937 in its name comes from its period—`2^19937 - 1`, an astronomically large number you will never cycle through within a program's lifetime. Its internal state is 624 32-bit words (`std::mt19937::state_size == 624`, verified on this machine). Good quality, fast, statistically well-vetted—for the overwhelming majority of scenarios, it's all you need.

Two other engine families you'll occasionally bump into:

- `std::linear_congruential_engine`: linear congruential—the old `x = a*x + c mod m` approach; `minstd_rand0` / `minstd_rand` are its preset instances. Tiny state (a single integer), fast, but mediocre quality. Only worth considering when "state must be extremely small and statistical quality requirements are low" (certain embedded constraints, say).
- `std::subtract_with_carry_engine`: a subtract-with-carry generator (lagged Fibonacci); `ranlux24` / `ranlux48` are the preset instances. Statistically strong in certain settings, but the default pick is still mt19937.

One very practical detail: constructing `mt19937` directly accepts only a single 32-bit seed, yet its state space is 624 words—`2^19937` large. Supplying only a 32-bit seed means picking from just `2^32` possible starting states, drastically shrinking the space of reproducible "starting points". If you care (long-running simulations where you'd rather not collide seeds, say), the standard library provides `std::seed_seq`: fill in multiple seed words and feed that to the engine, spreading the initial state out fully. A single seed is plenty for everyday use—just knowing this advanced option exists is enough.

### Distributions: Shaping Uniform Integers into What You Want

Distributions are where `<random>` really saves you effort. An engine spits out uniform integers over `[0, 2^32-1]`, and that is almost never what you want. Distribution objects do that mapping, and they **handle modulo bias themselves**—a trap you'd step into hand-writing `eng() % N`, but one `uniform_int_distribution` sidesteps automatically.

Let's first run a million samples through the most common distributions:

```cpp
// Standard: C++20
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

int main()
{
    std::mt19937 eng(2024);

    // Normal distribution: mean 0, standard deviation 1
    std::normal_distribution<double> norm(0.0, 1.0);
    constexpr int N = 1000000;
    double sum = 0, sum2 = 0;
    std::vector<long long> hist(10, 0);   // 10 buckets over [-5, 5), each 1.0 wide
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

    // Bernoulli: p=0.7
    std::bernoulli_distribution bern(0.7);
    long long trues = 0;
    for (int i = 0; i < N; ++i) if (bern(eng)) ++trues;
    std::printf("bernoulli(0.7) %d 样本: true 占比=%.4f\n", N, double(trues) / N);

    // Uniform real: [0, 1)
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

Want to run it yourself and check the statistics? Open the online demo below (it finishes in 0.04 seconds):

<OnlineCompilerDemo
  title="<random> distributions demo: normal / bernoulli / uniform"
  source-path="code/examples/vol3/60_random_distributions.cpp"
  description="mt19937 feeding three distributions, one million samples each: a bell-shaped histogram for normal, the true ratio for bernoulli(0.7), and uniform_real(0,1)'s mean converging on 0.5—none of which rand()%N can do"
  allow-run
/>

A few things jump out immediately. The normal distribution's histogram is a beautiful bell curve—the two middle buckets `[-1,+1)` hold 340,000-odd each, decaying symmetrically outward; the mean is `-0.0005` and the standard deviation `1.0002`, essentially the nominal `(0, 1)`. Bernoulli `0.7` shows a true ratio of `0.7008`. Uniform reals over `[0, 1)` average `0.5000`. Every one of these is a job you'd have to implement and verify yourself with `rand()`; `<random>` has already done it for you, and done it correctly.

The most crucial difference sits in `uniform_int_distribution`. You might think: isn't a uniform integer just `eng() % N`? Write it that way and you've stepped into modulo bias. The reason: the engine's range is `[0, 2^32-1]`—`2^32` values in all—and `2^32` is not necessarily divisible by your `N`. Take `N=3`: `2^32 = 4294967296 = 3 * 1431655765 + 1`, remainder 1, so bucket 0 gets one more candidate value than buckets 1 and 2, and the distribution is no longer uniform (the bias is tiny, but it is objectively there). `uniform_int_distribution` uses rejection sampling internally to discard and redraw that "excess tail", guaranteeing strictly equal probability for every bucket. On a platform where `RAND_MAX` is only 15 bits this bias would be glaring; on glibc's 31-bit `rand()` it is too small to measure—but "relying on the platform doing you a favor" is precisely one of the reasons to retire `rand()`.

We tested `rand()%3` versus `uniform_int_distribution(0,2)`, 300 million samples each, bucket hits:

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

On glibc both fall within noise (bias on the order of one part in ten thousand), confirming that this machine's `rand()` really does have tiny modulo bias. But the conclusion is not "`rand()%3` is fine"—it is "this machine happens to be fine; another platform may not be". `uniform_int_distribution` spares you from worrying about it in the first place.

## The Correct Way: A Clean Starter Template with One Core Line

Putting the above together: for "generate a uniform integer from 1 to 100", the standard, portable, bias-free way boils down to this one core line:

```cpp
// Standard: C++20
#include <cstdio>
#include <random>

int main()
{
    std::random_device rd;                       // 1. Device: a non-deterministic seed
    std::mt19937 eng(rd());                      // 2. Engine: initialized from the seed
    std::uniform_int_distribution<int> dist(1, 100);  // 3. Distribution: closed interval [1, 100]

    std::printf("rd{}() 种 mt19937 + uniform(1,100) 10 个: ");
    for (int i = 0; i < 10; ++i) std::printf("%d ", dist(eng));
    std::printf("\n");
    return 0;
}
```

```text
rd{}() 种 mt19937 + uniform(1,100) 10 个: 97 60 100 11 25 17 57 16 43 86
```

Three steps mapping to the trio: `random_device` fetches an unpredictable seed, `mt19937` initializes from it, and `uniform_int_distribution` maps the engine's output into the closed interval `[1, 100]` you asked for. Note that `uniform_int_distribution`'s range is a **closed interval** (both endpoints are reachable), unlike the half-open intervals of a whole pile of languages—write `dist(1, 100)` and you can genuinely draw a 100.

A more compact variant takes the seed straight from a temporary: `std::mt19937 eng(std::random_device{}());`. `random_device{}` constructs a device object, `()` calls it once to fetch a value, and the whole expression serves as `eng`'s constructor argument. This line shows up constantly in tutorials and in real code alike—just commit it to memory.

If you need **reproducibility** (tests, simulation replay), swap the `random_device` step for a fixed seed: `std::mt19937 eng(42);`—the sequence is locked in, identical across platforms. Reproducible or not is the only fork on this line; everything else stays the same.

::: warning random_device is not cryptographically secure
Most implementations of `std::random_device` read `/dev/urandom`, which is excellent quality—but **the standard permits it to degrade into a deterministic pseudo-random generator** (certain MinGW versions historically did exactly that, returning something in the vein of `rand()`), and it is **not cryptographically secure**. For security-sensitive scenarios—generating keys, tokens, salts—use a dedicated cryptographic library (OpenSSL's `RAND_bytes`, libsodium's `randombytes_buf`), not `<random>`.
:::

## Multithreaded Randomness: One thread_local Engine per Thread

One last high-frequency pain point. When a multithreaded program needs randomness, the cardinal sin is **multiple threads sharing the same engine object**—engines are stateful, concurrent calls are a data race, and your options are locking (poor performance) or UB.

The correct solution is one independent engine per thread, stored `thread_local`. Each thread automatically gets its own engine and its own state—no interference, no locks needed:

```cpp
// Standard: C++20
#include <atomic>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

// One independent engine per thread; thread_local keeps it thread-private
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

Four threads drawing 100,000 samples each total about 20.2 million, matching the expected `50.5 * 400000 = 20200000`. A few details here deserve a callout.

First, `thread_local std::mt19937` initialized with `random_device{}()` means **each thread grabs its own truly random seed the first time it accesses the engine**, so different threads start at different points with different sequences—no embarrassment of every thread marching down the same random sequence.

Second, the distribution object `dist` is constructed inside the lambda but outside the loop—distributions are essentially stateless, so construct once and call repeatedly; don't put the construction in the inner loop (the cost is small, but there's no point). Here `dist` is a per-thread local variable, so there's no sharing problem either.

Third, `thread_local` is not a free lunch: each thread's first access triggers engine construction (one `random_device` system call + initializing mt19937's 624 words of state), a one-time cost. So it suits "this thread draws random numbers repeatedly"; if some thread draws one or two random values and exits, thread_local isn't worth it—just construct a local engine directly.

## A Few Pitfalls You'll Actually Hit

Let's collect the spots where this journey tends to go off the road—each one verified by the tests above:

::: warning A single mt19937 seed covers only 2^32 starting points
`std::mt19937 eng(seed)` accepts only a single 32-bit seed, but its state space is `2^19937`. Single-seeding means `2^32` program instances each walk their own disjoint orbit through the `2^19937` state space—plenty for the vast majority of applications. But if you're running a huge number of independent simulations simultaneously and worry about starting-point collisions, initialize with `std::seed_seq` filled with multiple seed words to spread the starting-point space out.
:::

::: warning uniform_int_distribution uses a closed interval
The range of `uniform_int_distribution<int>(1, 100)` is `[1, 100]`, **closed at both ends**—100 is reachable. That matches Python's `random.randint` but runs opposite to a pile of half-open APIs (the flip side of `randint`; `std::uniform_real_distribution`'s half-open `[a, b)`). Confirm whether you want closed or half-open before you use it, and don't write on autopilot from another language's habits.
:::

::: warning Don't repeatedly construct distributions or engines in loops
Engine construction is expensive (mt19937 must initialize 624 words of state); distribution construction is cheap but not free. Hoist both out of loops—engines should live at function/class scope where possible, distributions constructed once as needed and reused. Writing `std::mt19937 eng(...)` inside an inner loop on a hot path is a classic beginner performance trap.
:::

::: warning Sharing an engine across threads is a data race
Engines are stateful; multiple threads calling the same engine object without a lock is UB. Either go `thread_local` with one per thread, or protect it with a mutex (which then becomes a bottleneck). Default to `thread_local`.
:::

::: warning random_device can degrade to pseudo-random
The standard allows `std::random_device` to degrade into a deterministic generator when no true entropy source exists, and it is not cryptographically secure. Seeding purposes are generally fine; for security-sensitive scenarios, switch to a cryptographic library.
:::

## Summary

`<random>`'s idea in one sentence: **decouple "where the randomness comes from" (engines), "what shape it takes" (distributions), and "where the starting point comes from" (devices)**. The key takeaways:

- The real reasons `rand()` deserves retirement are not "how bad its distribution is on modern glibc" (our measurements show it's actually decent), but **`RAND_MAX` is small and inconsistent across implementations, the implementation-defined algorithm makes sequences non-reproducible across platforms, it is thread-unsafe at the standard level, and it cannot express distributions directly**—every one of these is a structural problem that bites in real engineering.
- The trio's division of labor: engines (`mt19937` most common, period `2^19937-1`, 624-word state) emit uniform unsigned integers; distributions (`uniform_int`/`uniform_real`/`normal`/`bernoulli`, etc.) shape them into the target form; devices (`random_device`) supply non-deterministic seeds.
- The correct pattern's one core line: `std::random_device rd; std::mt19937 eng(rd()); std::uniform_int_distribution<int> dist(1, 100);` — for reproducibility, replace `rd()` with a fixed integer seed.
- `uniform_int_distribution` uses rejection sampling internally to eliminate the modulo bias of `eng() % N`, and its interval is **closed**—those two points are where hand-rolled code most often goes wrong.
- For multithreading, use `thread_local std::mt19937` with one engine per thread to avoid data races on shared state; don't construct engines repeatedly on hot paths.
- `random_device` is not cryptographically secure; keys/tokens go to a dedicated cryptographic library.

In the next article we switch topics—looking at another "transform your data" facility the standard library offers beyond `<random>`.

## References

- [cppreference: `<random>`](https://en.cppreference.com/w/cpp/numeric/random) — an overview of engines/distributions/devices and the complete catalog
- [cppreference: std::mt19937](https://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine) — the Mersenne Twister engine, `state_size`, and its period
- [cppreference: std::uniform_int_distribution](https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution) — the closed interval, and rejection sampling removing modulo bias
- [cppreference: std::random_device](https://en.cppreference.com/w/cpp/numeric/random/random_device) — the non-deterministic entropy source and notes on implementation degradation
- [cppreference: std::rand](https://en.cppreference.com/w/cpp/numeric/random/rand) — notes on `rand()`'s thread safety and cross-implementation differences
