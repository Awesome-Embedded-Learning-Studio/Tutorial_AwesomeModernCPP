---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'A thorough tour of the `<numeric>` family—why `accumulate` truncates `double`
  to `int`, what lets `reduce` run in parallel and why it demands associativity, how
  `partial_sum` differs from the `scan` variants, plus C++17 number theory `gcd`/`lcm`,
  how C++20 `midpoint` rescues `(a+b)/2` from overflow, and why `lerp` is not in `<numeric>`'
difficulty: intermediate
order: 44
platform: host
prerequisites:
- 'Iterator Basics and Categories: The Glue Between Containers and Algorithms'
- 'Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation'
reading_time_minutes: 14
related:
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New Tricks'
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'numeric: Accumulate, Fill, Inner Product, and Adjacent Difference'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/44-numeric-algorithms.md
  source_hash: 5edb72a701167af00103aebed715ba87c0e69b6af0a20f4b2116594d9bc02c3c
  translated_at: '2026-09-26T01:50:41+00:00'
  engine: anthropic
  token_count: 9000
---
# `<numeric>`: Accumulate, Fill, Inner Product, and Adjacent Difference

In the previous few articles we walked through containers and iterators, and said quite a bit on the algorithm side too. But the standard library's algorithms actually live in two headers: the well-known `<algorithm>` holds the crowd that "does things to the elements themselves" — `find` / `sort` / `copy` and friends; and then there is a far more low-key `<numeric>`, dedicated to "computing a pile of numbers down to one number" or "turning a pile of numbers into another pile of numbers" — accumulation, inner products, prefix sums, and filling in running indices all live here.

This article takes the `<numeric>` family apart. They all look like simple chores that "a for loop could write", but each one hides at least one design decision worth digging into: why `accumulate`'s return type silently truncates `double`, what gives `reduce` the nerve to run in parallel, how the C++17 pile of `scan` algorithms splits the prefix-sum family into finer distinctions, and why C++20's `midpoint` rescues `(a+b)/2` from overflow. Once you have these points strung together, you genuinely know how to use this family of algorithms instead of handwriting a loop every single time.

## `accumulate`: Summation, and Its Most Treacherous Return Type

The most basic one. `std::accumulate(first, last, init)` means "starting from `init`, fold each element of the range in, one after another"; the default operation is `+`. Summing a `vector`:

```cpp
// Standard: C++20
#include <iostream>
#include <numeric>
#include <vector>

int main()
{
    std::vector<double> v{1.5, 2.5, 3.5, 4.5};   // mathematical sum = 12.0
    std::cout << "accumulate(v, 0):    " << std::accumulate(v.begin(), v.end(), 0) << '\n';
    std::cout << "accumulate(v, 0.0):  " << std::accumulate(v.begin(), v.end(), 0.0) << '\n';
    return 0;
}
```

Run with `g++ -std=c++20 -O2` (local GCC 16.1.1), it prints:

```text
accumulate(v, 0):    10
accumulate(v, 0.0):  12
```

Want to run it yourself and watch the truncation happen? Open this live example:

<OnlineCompilerDemo
  title="accumulate's truncation trap: the initial value's type decides the return type"
  source-path="code/examples/vol3/44_accumulate_truncation.cpp"
  description="The same double vector (mathematical sum 12.0): accumulate(v, 0) returns 10 (int truncation), accumulate(v, 0.0) returns 12 — the return type follows the initial value"
  allow-run
/>

The only difference is the initial value: one passes `0` (an `int`), the other `0.0` (a `double`), and the results come out `10` versus `12`. This is exactly `accumulate`'s most treacherous corner, and it is precisely the defect that C++23's `std::fold` (the star of the next article) set out to fix.

### Why the truncation happens: the return type equals the initial value's type

Look at `accumulate`'s signature and it becomes clear:

```cpp
T accumulate(InputIt first, InputIt last, T init);
```

The return type `T` is not "the type of the range's elements" — it is "the type of the initial value `init`". Internally the accumulation is roughly `acc = acc + *it`, and `acc` begins life as `init`, so its type is locked in from the start. Pass `0`, and the whole accumulation happens in `int` — every `double` element is first implicitly converted to `int` (chopping off the fraction), then added. `1.5 + 2.5 + 3.5 + 4.5` becomes `1 + 2 + 3 + 4 = 10`, the fractional parts silently discarded, and the compiler does not say one word of warning.

Change the initial value to `0.0` and `T` becomes `double`; the accumulation runs in `double` from start to finish, and the result is finally correct. So when summing a floating-point sequence with `accumulate`, **the initial value must carry a decimal point** — a trap that a single line of code can plant, yet whose correctness is hard to see by eye. Integer sequences are immune, but the moment the element type is "wider" than the initial value's type (say, `long long` elements with an `int` initial value), the same truncation strikes.

::: warning accumulate's return type = the initial value's type, not the element type
Always pass `0.0` for floating-point sums, and `0LL` for big-integer sums. Passing the wrong type is not an error — it just hands you a wrong result that "looks about right". C++23's `std::fold_left` switched to "deducing the accumulator type from the element type", eliminating this trap at the root — we expand on that in the next article.
:::

`accumulate`'s fourth parameter swaps out the accumulation operation. Pass in `std::multiplies<>{}` and accumulation turns into multiplication; pass a lambda, and any custom "merge" you like is possible. Note that the operation here is expected to be left-associative (`acc = op(acc, *it)`, with the initial value at the far left), which has consequences for order-sensitive operations (floating-point addition, for instance) — a point we will run straight into again when we get to `reduce`.

## `iota`: Filling with Successively Incremented Values

A tool that looks unremarkable but saves real typing. `std::iota(first, last, value)` starts from `value` and fills in `value, value+1, value+2, ...` one after another. The classic use is generating a run of sequence numbers:

```cpp
std::vector<int> ids(6);
std::iota(ids.begin(), ids.end(), 0);   // 0 1 2 3 4 5
```

The name `iota` comes from that index-generating operator in the programming language APL (the Greek letter ι) — it is not an "I-O-T-A" acronym, and knowing that makes it much harder to misremember. It is often used to put "running numbers" on a set of elements — shuffling indices, tagging a candidate set:

```cpp
// Standard: C++20
#include <array>
#include <iostream>
#include <numeric>
#include <vector>

template <class T>
void print(const char* label, const T& v)
{
    std::cout << label;
    for (auto x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> ids(6);
    std::iota(ids.begin(), ids.end(), 0);     // 0 1 2 3 4 5
    print("iota(0): ", ids);

    std::vector<int> ids5(6);
    std::iota(ids5.begin(), ids5.end(), 100); // 100 101 102 ...
    print("iota(100): ", ids5);
    return 0;
}
```

Running it:

```text
iota(0): 0 1 2 3 4 5
iota(100): 100 101 102 103 104 105
```

What `iota` does is actually equivalent to "`for (i, v) { *i = val++; }`" — but once you recognize the name, the intent reads at a glance when scanning code ("this stretch generates sequence numbers"), far clearer than a bare loop.

## `inner_product`: The Inner Product of Two Sequences

`std::inner_product(first1, last1, first2, init)` computes the inner product of two sequences: multiply the elements at corresponding positions pairwise, then accumulate them onto `init`. Mathematically, it is `init + Σ a[i] * b[i]`:

```cpp
std::vector<int> a{1, 2, 3, 4};
std::vector<int> b{2, 3, 4, 5};
std::cout << std::inner_product(a.begin(), a.end(), b.begin(), 0);
// = 1*2 + 2*3 + 3*4 + 4*5 = 40
```

It shares `accumulate`'s return-type-equals-initial-value-type trap (`init` decides the type), and it likewise accepts two extra callable parameters that customize the "multiply" and the "add": `inner_product(first1, last1, first2, init, op1, op2)` internally does `acc = op1(acc, op2(*it1, *it2))`.

This doubly customized form is not common, but occasionally it yields a remarkably concise expression. Say you want to decide "do these two boolean sequences have true at the same positions" — set both `op1` and `op2` to logical AND and the initial value to `1` (true); the moment any position fails to be true in both, the result collapses to zero:

```cpp
std::vector<int> flags1{1, 1, 0, 1};
std::vector<int> flags2{1, 0, 1, 1};
auto all_both = std::inner_product(flags1.begin(), flags1.end(), flags2.begin(), 1,
    [](int x, int y){ return x && y; },   // op1: accumulated AND
    [](int x, int y){ return x && y; });  // op2: pairwise AND
// result 0 (second position 1&&0=0, accumulated value drops to zero)
```

One caveat worth stating: since C++17, `inner_product` is no longer recommended for new code — C++17 delivered the more general `std::transform_reduce`, which both runs multi-threaded and accepts an execution policy. That said, `inner_product` still reads the most plainly in simple single-threaded scenes, and old code is full of it, so knowing it remains necessary.

## The Prefix-Sum Family: `partial_sum` / `adjacent_difference` / `inclusive_scan` / `exclusive_scan`

`<numeric>` contains a family of algorithms dedicated to "turning one sequence into another", with the prefix sum at its core. The old interface (C++11) has two of them; the new interface (C++17) splits out two more, the `scan` pair, separating the two prefix-sum semantics cleanly. Let's run them all at once and see the differences:

```cpp
// Standard: C++20
#include <iostream>
#include <numeric>
#include <vector>

template <class T>
void print(const char* label, const T& v)
{
    std::cout << label;
    for (auto x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{1, 2, 3, 4, 5};
    std::vector<int> out(v.size());

    std::partial_sum(v.begin(), v.end(), out.begin());
    print("partial_sum      : ", out);   // includes current: 1 3 6 10 15

    std::adjacent_difference(v.begin(), v.end(), out.begin());
    print("adjacent_diff    : ", out);   // 1 (2-1) (3-2) (4-3) (5-4) = 1 1 1 1 1

    std::inclusive_scan(v.begin(), v.end(), out.begin(), std::plus<>{}, 0);
    print("inclusive_scan(0): ", out);   // includes current, same as partial_sum

    std::inclusive_scan(v.begin(), v.end(), out.begin());
    print("inclusive_scan   : ", out);

    std::exclusive_scan(v.begin(), v.end(), out.begin(), 0);
    print("exclusive_scan(0): ", out);   // excludes current: 0 1 3 6 10
    return 0;
}
```

Running it:

```text
partial_sum      : 1 3 6 10 15
adjacent_diff    : 1 1 1 1 1
exclusive_scan(0): 0 1 3 6 10
inclusive_scan(0): 1 3 6 10 15
inclusive_scan   : 1 3 6 10 15
```

`partial_sum` is the textbook prefix sum — the output at position `i` is `v[0] + v[1] + ... + v[i]`, **including the current position**. `adjacent_difference` is its inverse: the output at position `i` is `v[i] - v[i-1]` (the first element is kept as-is), which is why `1 2 3 4 5` above comes out all `1` (each number is 1 more than the one before it). The two form a pair: `partial_sum` first, then `adjacent_difference`, and you recover the original sequence.

C++17's `inclusive_scan` / `exclusive_scan` make the distinction between the two prefix-sum semantics explicit:

- `inclusive_scan` — **includes** the current position, matching `partial_sum`'s semantics;
- `exclusive_scan` — **excludes** the current position: the output at position `i` is `v[0] + ... + v[i-1]`, and the first position receives the given initial value.

Above, `exclusive_scan(0)` produced `0 1 3 6 10` — the first position is simply handed the initial value `0`, and only the positions after it hold "the sum of all preceding elements". This "exclude the current element" prefix sum is especially common in sweep-line and pipeline-style algorithms; you used to have to hand-write a loop or shift by one position, and now a single `exclusive_scan` settles it.

The `scan` series' real value over the old `partial_sum` is twofold: the semantics are cleanly separated (include / exclude), and **it can be parallelized just like `reduce`** — they all support being handed an execution policy (`std::execution::par`), so prefix sums, once a computation that had to be strictly serial, now have a parallel implementation in the standard library. We will expand on this right away in the `reduce` section.

## `reduce`: The Parallelizable `accumulate`

`std::reduce` (C++17) looks like it does the same job as `accumulate` — folding a range into a total:

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::cout << std::reduce(v.begin(), v.end(), 0);   // 15
```

But it differs from `accumulate` in two essential ways.

**First, it can run in parallel.** `reduce` supports being passed an execution policy, so the standard library can cut the range into segments, hand them to multiple threads to compute separately, and merge at the end. `accumulate`, by contrast, is strictly serial from left to right — it must guarantee the "left side first, then the right" order, so it cannot be parallelized. That is why C++17 added `reduce`: on large data volumes, single-threaded `accumulate` cannot keep many cores fed.

**Second, the operation must be associative.** This is the direct price of "can be parallel". `accumulate`'s merge is `acc = acc + *it`, fixed left to right, so even if the operation itself is order-sensitive (floating-point addition, say), what it computes is still "consistent by definition". `reduce` must split and then merge, and the order in which elements associate during the merge is arbitrary — `(a+b)+c+d` might become `a+(b+c)+d`, or an even stranger chunking. Only if the operation is associative (`op(a, op(b,c)) == op(op(a,b), c)`) does every association order arrive at the same result.

Floating-point addition, as luck would have it, is not associative. Let's take an order-sensitive floating-point sequence, let `accumulate` and `reduce` each have a run at it, and see how far apart they actually land:

```cpp
// Standard: C++20
#include <iostream>
#include <numeric>
#include <vector>

int main()
{
    // one million 0.1f plus one 1e7f: different float accumulation orders, results drift
    std::vector<float> f;
    for (int i = 0; i < 1000000; ++i) f.push_back(0.1f);
    f.push_back(1e7f);

    float acc = 0.0f;
    for (auto x : f) acc += x;                              // sequential accumulation
    auto red = std::reduce(f.begin(), f.end(), 0.0f);       // any association order allowed

    std::cout.precision(15);
    std::cout << "accumulate float : " << acc << '\n';
    std::cout << "reduce float     : " << red << '\n';
    std::cout << "数学期望(约)    : " << (1000000 * 0.1 + 1e7) << '\n';
    return 0;
}
```

Running it:

```text
accumulate float : 10100958
reduce float     : 10099760
数学期望(约)    : 1.01e+07
```

Neither one equals the expected `10100000` exactly — that is inherent to floating-point accumulation. But the key point is that the two **disagree**: `10100958` vs `10099760`, more than a thousand apart. `accumulate`'s result is the strictly left-associated one; `reduce` merged in whatever chunked order GCC's implementation chose. Neither side is "wrong" — the non-associativity of floating-point addition simply makes the result depend on the order.

That is `reduce`'s implicit demand on the operation: **integer addition, multiplication, bitwise AND/OR/XOR, logical AND/OR, `max`/`min` — associative operations like these can be parallelized freely and the result stays consistent; order-sensitive ones like floating-point addition may drift once parallelized**. Using `reduce` for floating-point sums is broadly acceptable (the error stays within floating-point precision), but if you depend on "reproducing one exact value", you have to go back to serial `accumulate`.

::: warning reduce / scan require the operation to be associative
As soon as a parallel execution policy is on (or you use `reduce` at all), do not expect left-associative ordering to survive. Integer and unsigned operations are fine; floating-point results drift with the association order. If you need strict ordering, use `accumulate`.
:::

A side note on something we have not expanded yet: `reduce` is currently **not in the C++20 `std::ranges` namespace** — `ranges::reduce` does not exist. The reason is that the design for ranges-ifying the parallel algorithms (the execution-policy family) still has open points, so the committee did not ship them together with C++20. We will pick this thread back up in the next article when we cover the `fold` family, because `fold` is exactly the ranges-ified "serial fold" — in a sense, `reduce`'s ranges counterpart.

## C++17 Number Theory: `gcd` and `lcm`

From this section on, we are into the "small but practical" number-theory and geometry tools of `<numeric>`. First up, the greatest common divisor and least common multiple added in C++17:

```cpp
std::gcd(54, 24)   // 6
std::lcm(4, 6)     // 12
std::gcd(17, 13)   // 1 (coprime)
```

`gcd` / `lcm` are function templates that work for integer types, using an efficient Euclidean algorithm inside. No more hand-writing Euclid or reaching for Boost. A few boundary values are worth remembering (all verified by running them):

- `gcd(0, 0) = 0` and `gcd(0, 12) = 12` — `gcd(0, n)` is simply `|n|`;
- `lcm(0, x) = 0` — the moment either argument is 0, the least common multiple is 0 (loosely speaking, every number is a "multiple" of 0, but the convention is to return 0).

::: warning lcm(0, x) = 0, it does not throw
Mathematically `lcm(0, x)` is a little ambiguous; the standard library picks `0`. If you are writing code that involves fraction simplification or period alignment, do not assume `lcm` always returns a positive number — pass in 0 and you get 0 back.
:::

## C++20: `midpoint` Rescues `(a+b)/2`, `lerp` Does Linear Interpolation

C++20 delivered two tools that look plain but exist specifically to fix real bugs.

### `midpoint`: Computing the Midpoint Safely

In scenes like binary search and range halving, taking the midpoint of two numbers is a high-frequency operation. The intuitive spelling is `(a + b) / 2` — but that line **overflows** when `a` and `b` both approach the type's upper limit. Two `int64` values on the order of 9e18, for instance, add up past `int64`'s maximum (about 9.22e18); signed integer overflow is undefined behavior, and the result can be a negative number. Let's put this trap to the test for real:

```cpp
// Standard: C++20
#include <cstdint>
#include <iostream>
#include <numeric>

int main()
{
    std::int64_t big1 = 7'000'000'000'000'000'000LL;   // 7e18
    std::int64_t big2 = 9'000'000'000'000'000'000LL;   // 9e18
    auto naive = (big1 + big2) / 2;                      // overflow!
    auto safe  = std::midpoint(big1, big2);
    std::cout << "naive (big1+big2)/2 = " << naive << " (溢出!)\n";
    std::cout << "midpoint(big1,big2) = " << safe << " (正确)\n";
    return 0;
}
```

Running it:

```text
naive (big1+big2)/2 = -1223372036854775808 (溢出!)
midpoint(big1,big2) = 8000000000000000000 (正确)
```

`(big1+big2)/2` handed back `-1223372036854775808` — a ridiculous negative number, exactly the typical wreckage after an overflow crash. The correct midpoint should be `8000000000000000000` (8e18), and `std::midpoint` computed it right.

Inside, `midpoint` uses an equivalent algorithm that does not overflow (roughly `a + (b - a) / 2`, with sign and parity handled properly); it never passes through the `a + b` step, so it cannot overflow. This is a classic case of C++20 promoting a small operation "everyone writes wrong" into a standard facility — stop writing `(a+b)/2` yourself; when you write binary search, divide-and-conquer, or range halving, go straight to `std::midpoint`.

`midpoint` also has an overload that takes the midpoint of two **pointers**:

```cpp
int arr[]{10, 20, 30, 40, 50};
auto mid = std::midpoint(arr, arr + 4);
std::cout << "midpoint(arr, arr+4) -> arr[" << (mid - arr) << "] = " << *mid << '\n';
// output: midpoint(arr, arr+4) -> arr[2] = 30
```

The pointer version handles odd and even lengths correctly (length 4 takes offset 2; length 5 rounds down to offset 2 as well), which makes it steadier than a hand-written `(lo + hi) / 2` when implementing binary search or chunking. Pointer midpoints come with an extra benefit: they avoid `lo + hi`, the illegal act of "adding two pointers" (in C++, pointers may only be subtracted, never added), so on syntax grounds alone `midpoint` is already cleaner than a hand-rolled loop.

### `lerp`: Linear Interpolation (Note That It Is Not in `<numeric>`)

`std::lerp(a, b, t)` computes the linear interpolation `a + t * (b - a)`: `t=0` returns `a`, `t=1` returns `b`, and `t=0.5` is the midpoint. Animation, gradients, in-game interpolation — all of it goes through this one:

```cpp
std::lerp(0.0, 100.0, 0.25)   // 25
std::lerp(0.0, 100.0, 1.0)    // 100
std::lerp(0.0, 100.0, 2.0)    // 200 (extrapolation works; t is not limited to [0,1])
```

It looks perfectly ordinary, but it carries a few guarantees a hand-written `a + t*(b-a)` cannot get: `t=0` returns `a` exactly, `t=1` returns `b` exactly (the hand-written version may hand you `99.9999...` thanks to floating-point error), and its behavior for infinities and NaN is well-defined. In numerical and graphics code, these are guarantees that genuinely matter.

::: warning lerp is in `<cmath>`, not in `<numeric>`
This is a header-file trap that is easy to stumble into: `gcd` / `lcm` / `midpoint` are all in `<numeric>`, but `std::lerp` insists on living in **`<cmath>`**. Including only `<numeric>` and then using `std::lerp` fails to compile outright, with `'lerp' is not a member of 'std'`. We tested it — you must additionally `#include <cmath>`.
:::

## A Few Traps That Are Genuinely Easy to Hit

Let's gather in one place the spots where this family tends to crash when in use — every one of them verified by the runs above:

::: warning The initial value of accumulate / inner_product decides the return type
Always pass `0.0` for floating-point sums, and `0LL` for big-integer sums. Passing `0` (an `int`) over a `double` sequence truncates every element to `int` first, silently drops the fractional part, and draws no compiler warning. This is `accumulate`'s most classic and best-hidden trap.

::: warning reduce / scan require the operation to be associative when parallelized
Once an execution policy is on (or you rely on `reduce`'s associative semantics), the association order is arbitrary. Integers and bitwise operations are fine; floating-point addition drifts with the order (measured: `10100958` vs `10099760`). If you need strict left-association, use `accumulate` / `partial_sum`.

::: warning (a+b)/2 overflows on large integers; use midpoint
Taking midpoints in binary search and range halving, `a + b` overflows. Measured: `(7e18 + 9e18) / 2` hands back `-1223372036854775808`. From C++20 on, always use `std::midpoint` — it works for integers and pointers alike.

::: warning std::lerp is in `<cmath>`, not in `<numeric>`
`gcd` / `lcm` / `midpoint` are in `<numeric>`, but `lerp` is in `<cmath>`. Including only `<numeric>` and using `lerp` will not compile — remember to add `<cmath>`.

## Summary

The `<numeric>` family all look like small tools "a for loop could write", but each one hides at least one design decision worth knowing. Let's collect the key conclusions:

- When `accumulate` sums, the return type equals the initial value's type; a floating-point sequence must be passed `0.0`, or the result truncates to integer (a trap only C++23's `fold` fixes — covered in the next article).
- `iota` fills with successively incremented values, the standard way to generate a sequence of running numbers; `inner_product` computes the inner product of two sequences — an old single-threaded interface, and new code can consider `transform_reduce`.
- The prefix-sum family: `partial_sum` (includes the current element) and `adjacent_difference` (differencing, `partial_sum`'s inverse); C++17's `inclusive_scan` (includes current) / `exclusive_scan` (excludes current) split the semantics apart, and they parallelize too.
- `reduce` is the parallelizable `accumulate`, at the price of requiring the operation to be associative; floating-point addition is not, so parallel results drift. It is not ranges-ified yet — the next article expands on why when it covers `fold`.
- C++17 number theory: `gcd` / `lcm` (mind `lcm(0, x) = 0`); C++20's `midpoint` rescues `(a+b)/2` from overflow and works on pointers too; `lerp` does linear interpolation, but it lives in `<cmath>`, not in `<numeric>`.

In the next article we formally step into the C++23 `fold` family — seeing how it fixes `accumulate`'s return-type defect at the root, and what its relation to `reduce` and to ranges-style folding actually is.

## References

- [cppreference: `<numeric>`](https://en.cppreference.com/w/cpp/numeric) — an overview of the whole family
- [cppreference: std::accumulate](https://en.cppreference.com/w/cpp/algorithm/accumulate) — the formal word that the return type equals the initial value's type (`T init`)
- [cppreference: std::reduce (C++17)](https://en.cppreference.com/w/cpp/algorithm/reduce) — parallel semantics and the "operation must be associative" requirement
- [cppreference: std::exclusive_scan / inclusive_scan (C++17)](https://en.cppreference.com/w/cpp/algorithm/exclusive_scan) — the two prefix-sum semantics: including / excluding the current position
- [cppreference: std::midpoint (C++20)](https://en.cppreference.com/w/cpp/numeric/midpoint) — the overflow-free midpoint, with integer and pointer overloads
- [cppreference: std::lerp (C++20)](https://en.cppreference.com/w/cpp/numeric/lerp) — linear interpolation; note that it is defined in `<cmath>`
- [cppreference: std::gcd / std::lcm (C++17)](https://en.cppreference.com/w/cpp/numeric/gcd) — the number-theory tools and the `lcm(0,x)=0` convention
