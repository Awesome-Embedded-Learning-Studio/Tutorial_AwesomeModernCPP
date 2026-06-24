---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: Mastering the `<numeric>` algorithm family—why `accumulate` truncates
  `double` to `int`, how `reduce` enables parallelism and why it requires associativity,
  the difference between the `partial_sum` family and `scan`, C++17 number theory
  `gcd`/`lcm`, how C++20 `midpoint` prevents overflow in `(a+b)/2`, and why `lerp`
  isn't in `<numeric>`.
difficulty: intermediate
order: 44
platform: host
prerequisites:
- 迭代器基础与 category
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 14
related:
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'numeric: Accumulate, Fill, Inner Product, and Adjacent Difference'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/44-numeric-algorithms.md
  source_hash: 5edb72a701167af00103aebed715ba87c0e69b6af0a20f4b2116594d9bc02c3c
  translated_at: '2026-06-24T04:27:22.897892+00:00'
  engine: anthropic
  token_count: 3519
---
# `<numeric>`: Accumulation, Fill, Inner Product, and Adjacent Difference

In previous posts, we covered containers and iterators, and we touched on many algorithms. However, the standard library algorithms are actually split across two headers: the well-known `<algorithm>` contains utilities like `find`, `sort`, and `copy` that manipulate elements directly. Then there is the much more low-profile `<numeric>`, which specializes in "reducing a pile of numbers to a single number" or "transforming a pile of numbers into another pile"—summation, inner product, prefix sums, and filling sequences all live here.

In this post, we will break down the `<numeric>` family. These look like simple tasks that "a for loop could handle," but each hides at least one design decision worth exploring in depth: Why does `accumulate`'s return type silently truncate `double`? How does `reduce` dare to be parallel? How did C++17 split the prefix sum family into those `scan` variants? And how does C++20's `midpoint` save `(a+b)/2` from overflow? Connecting these dots ensures you truly master this family of algorithms, rather than handwriting loops every time.

## `accumulate`: Accumulation, and its Pitfall of a Return Type

The most basic one. `std::accumulate(first, last, init)` simply means "starting from `init`, sequentially add each element in the range." The default operation is `+`. Here is how we sum a `vector`:

```cpp
// Standard: C++20
#include <iostream>
#include <numeric>
#include <vector>

int main()
{
    std::vector<double> v{1.5, 2.5, 3.5, 4.5};   // 数学和 = 12.0
    std::cout << "accumulate(v, 0):    " << std::accumulate(v.begin(), v.end(), 0) << '\n';
    std::cout << "accumulate(v, 0.0):  " << std::accumulate(v.begin(), v.end(), 0.0) << '\n';
    return 0;
}
```

Here are the results obtained by running `g++ -std=c++20 -O2` (native GCC 16.1.1):

```text
accumulate(v, 0):    10
accumulate(v, 0.0):  12
```

Want to run it and see the truncation yourself? Check out this online demo:

<OnlineCompilerDemo
  title="The Truncation Trap of accumulate: Initial Value Type Determines Return Type"
  source-path="code/examples/vol3/44_accumulate_truncation.cpp"
  description="Same double vector (math sum is 12.0): accumulate(v, 0) returns 10 (int truncation), accumulate(v, 0.0) returns 12 — the return type is determined by the initial value"
  allow-run
/>

The only difference is the initial value: one is `0` (`int`), the other is `0.0` (`double`). The results are `10` and `12`, respectively. This is the biggest pitfall of `accumulate`, and it is precisely the defect that C++23's `std::fold` (covered in the next article) was designed to fix.

### Why truncation occurs: Return type = Initial value type

Looking at the signature of `accumulate` makes this clear:

```cpp
T accumulate(InputIt first, InputIt last, T init);
```

The return type `T` is not "the type of the elements in the range", but rather "the type of the initial value `init`". The internal accumulation is roughly `acc = acc + *it`, and since `acc` starts as `init`, its type is fixed. Therefore, if we pass `0`, the entire accumulation happens within `int`—each `double` element is implicitly converted to an `int` (truncating the decimal) before being added. `1.5 + 2.5 + 3.5 + 4.5` becomes `1 + 2 + 3 + 4 = 10`; the fractional parts are silently discarded, and the compiler doesn't issue a single warning.

If we change the initial value to `0.0`, `T` becomes `double`, the accumulation happens entirely in `double`, and the result is correct. So, when using `accumulate` to sum a floating-point sequence, **the initial value must include a decimal point**—this is a pitfall that can be buried in a single line of code but is very difficult to spot. Integer sequences don't have this problem, but if the element type is "wider" than the initial value type (e.g., `long long` elements with an `int` initial value), truncation will still occur.

::: warning accumulate's return type = initial value type, not element type
For floating-point sums, be sure to pass `0.0`; for large integer sums, pass `0LL`. Passing the wrong type won't cause an error, it will just give you a "looks about right" incorrect result. C++23's `std::fold_left` changes this to "deduce the accumulator type from the element type", eliminating this pitfall at the root—we'll cover this in the next article.
:::

The fourth parameter of `accumulate` can swap out the accumulation operation. Passing `std::multiplies<>{}` turns summation into multiplication; passing a lambda allows for any custom "fold" operation. Note that the operation here must satisfy "left-associative" semantics (`acc = op(acc, *it)`, with the initial value on the far left), which affects order-sensitive operations (like floating-point addition)—we will encounter this again when we discuss `reduce`.

## `iota`: Filling with Incrementing Values

A tool that looks unassuming, but saves a lot of effort when writing code. `std::iota(first, last, value)` starts from `value` and fills in `value, value+1, value+2, ...` sequentially. The most classic use case is generating a set of indices:

```cpp
std::vector<int> ids(6);
std::iota(ids.begin(), ids.end(), 0);   // 0 1 2 3 4 5
```

The name `iota` comes from the operator in the APL programming language that generates sequential indices (the Greek letter ι), not the acronym "I-O-T-A". Knowing this makes it harder to confuse. It is commonly used to assign sequential indices to a group of elements, for example, when shuffling indices or tagging a candidate set:

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

Here is the output:

```text
iota(0): 0 1 2 3 4 5
iota(100): 100 101 102 103 104 105
```

The work done by `iota` is essentially equivalent to "`for (i, v) { *i = val++; }`". However, once we recognize the name, the intent is immediately clear when reading the code—"this section is generating sequential indices"—which is much clearer than a raw loop.

## `inner_product`: Inner Product of Two Sequences

`std::inner_product(first1, last1, first2, init)` calculates the inner product of two sequences: it multiplies elements at corresponding positions and accumulates the result into `init`. Mathematically, this is `init + Σ a[i] * b[i]`:

```cpp
std::vector<int> a{1, 2, 3, 4};
std::vector<int> b{2, 3, 4, 5};
std::cout << std::inner_product(a.begin(), a.end(), b.begin(), 0);
// = 1*2 + 2*3 + 3*4 + 4*5 = 40
```

It shares the same pitfall as `accumulate`, where the return type is determined by the type of the initial value (`init`). Similarly, it accepts two extra callable arguments to customize the "multiply" and "add" operations: `inner_product(first1, last1, first2, init, op1, op2)`. Internally, it performs `acc = op1(acc, op2(*it1, *it2))`.

This double-customization form is not commonly used, but occasionally allows for very concise expressions. For example, if we want to check "whether two boolean sequences are true at the same corresponding positions" — we set both `op1` and `op2` to logical AND, and the initial value to `1` (true). If any position is not simultaneously true, the result becomes zero:

```cpp
std::vector<int> flags1{1, 1, 0, 1};
std::vector<int> flags2{1, 0, 1, 1};
auto all_both = std::inner_product(flags1.begin(), flags1.end(), flags2.begin(), 1,
    [](int x, int y){ return x && y; },   // op1: 累计「与」
    [](int x, int y){ return x && y; });  // op2: 逐位「与」
// 结果 0（第二位 1&&0=0，累计归零）
```

One note: `inner_product` has been deprecated since C++17 for new code—C++17 provides the more generic `std::transform_reduce`, which supports multithreading parallelism and execution policies. However, `inner_product` remains the most straightforward to read in simple, single-threaded scenarios, and it is ubiquitous in legacy code, so understanding it is still necessary.

## Prefix Sum Family: `partial_sum` / `adjacent_difference` / `inclusive_scan` / `exclusive_scan`

`<numeric>` contains a family of algorithms dedicated to "transforming one sequence into another," with the core concept being the prefix sum. The legacy interface (C++11) provides two algorithms, while the new interface (C++17) introduces two `scan` variants, clearly distinguishing between the two semantics of prefix sums. Let's run through them all at once to see the differences:

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
    print("partial_sum      : ", out);   // 含当前: 1 3 6 10 15

    std::adjacent_difference(v.begin(), v.end(), out.begin());
    print("adjacent_diff    : ", out);   // 1 (2-1) (3-2) (4-3) (5-4) = 1 1 1 1 1

    std::inclusive_scan(v.begin(), v.end(), out.begin(), std::plus<>{}, 0);
    print("inclusive_scan(0): ", out);   // 含当前, 同 partial_sum

    std::inclusive_scan(v.begin(), v.end(), out.begin());
    print("inclusive_scan   : ", out);

    std::exclusive_scan(v.begin(), v.end(), out.begin(), 0);
    print("exclusive_scan(0): ", out);   // 不含当前: 0 1 3 6 10
    return 0;
}
```

**Output:**

```text
```

```text
partial_sum      : 1 3 6 10 15
adjacent_diff    : 1 1 1 1 1
exclusive_scan(0): 0 1 3 6 10
inclusive_scan(0): 1 3 6 10 15
inclusive_scan   : 1 3 6 10 15
```

`partial_sum` is the textbook definition of a prefix sum—the output at position `i` is `v[0] + v[1] + ... + v[i]`, **including the current position**. `adjacent_difference` is its inverse operation: the output at position `i` is `v[i] - v[i-1]` (the first element is preserved as-is). Therefore, applying it to the sequence `1 2 3 4 5` above yields all `1`s (each number is one greater than the previous one). These two form a pair: applying `adjacent_difference` after `partial_sum` restores the original sequence.

C++17's `inclusive_scan` and `exclusive_scan` explicitly distinguish between the two semantics of prefix sums:

- `inclusive_scan` — **Includes** the current position, consistent with `partial_sum` semantics.
- `exclusive_scan` — **Excludes** the current position; the output at position `i` is `v[0] + ... + v[i-1]`, and the first position is given the initial value.

The `exclusive_scan(0)` example above yields `0 1 3 6 10`—the first position is directly assigned the initial value `0`, while subsequent positions represent the "sum of all preceding elements". This "exclusive" prefix sum is particularly common in algorithms like scanlines or pipelines, where we previously had to write manual loops or shift by one position; now, a single `exclusive_scan` handles it.

The true value of the `scan` family over the legacy `partial_sum` lies first in clear semantics (inclusive/exclusive), and second in the fact that **they can be parallelized like `reduce`**—they all support passing an execution policy (e.g., `std::execution::par`). Calculations like prefix sums, which used to be strictly serial, now have a parallel implementation in the standard library. We will expand on this in the upcoming section on `reduce`.

## `reduce`: A Parallel Version of `accumulate`

`std::reduce` (C++17) appears to do the same thing as `accumulate`—it accumulates a range:

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::cout << std::reduce(v.begin(), v.end(), 0);   // 15
```

However, there are two fundamental differences between it and `accumulate`.

**First, it can be parallelized.** `reduce` accepts an execution policy, allowing the standard library to split the range into segments, distribute them across multiple threads for independent computation, and finally merge the results. In contrast, `accumulate` is strictly sequential from left to right—it must guarantee the order of "left first, then right," making parallelization impossible. This is precisely why C++17 introduced `reduce`: the single-threaded `accumulate` cannot fully utilize multi-core processors when dealing with large datasets.

**Second, the operation must be associative.** This is the direct cost of being "able to be parallelized." The merging in `accumulate` follows `acc = acc + *it`, which is fixed from left to right. Therefore, even if the operation itself is order-sensitive (like floating-point addition), it remains "consistent with the definition." Since `reduce` splits and then merges, the grouping order of elements during merging is arbitrary—`(a+b)+c+d` might become `a+(b+c)+d`, or even more exotic chunking. Only if the operation satisfies associativity (`op(a, op(b,c)) == op(op(a,b), c)`), will arbitrary grouping orders yield the same result.

Floating-point addition, unfortunately, is not associative. Let's take an order-sensitive floating-point sequence and run it through both `accumulate` and `reduce` to see the actual difference:

```cpp
// Standard: C++20
#include <iostream>
#include <numeric>
#include <vector>

int main()
{
    // 一百万个 0.1f 加一个 1e7f: 浮点累加顺序不同, 结果会有出入
    std::vector<float> f;
    for (int i = 0; i < 1000000; ++i) f.push_back(0.1f);
    f.push_back(1e7f);

    float acc = 0.0f;
    for (auto x : f) acc += x;                              // 顺序累加
    auto red = std::reduce(f.begin(), f.end(), 0.0f);       // 允许任意结合顺序

    std::cout.precision(15);
    std::cout << "accumulate float : " << acc << '\n';
    std::cout << "reduce float     : " << red << '\n';
    std::cout << "数学期望(约)    : " << (1000000 * 0.1 + 1e7) << '\n';
    return 0;
}
```

Here is the output:

```text
accumulate float : 10100958
reduce float     : 10099760
数学期望(约)    : 1.01e+07
```

Neither result is exactly equal to the expected `10100000`—this is an inherent issue with floating-point accumulation. However, the key point is that the two results are **not equal** to each other: `10100958` vs `10099760`, a difference of over a thousand. `accumulate` is strictly left-associative, whereas `reduce` merges in the chunked order implemented by GCC. Neither side is "wrong"; it is simply that the non-associativity of floating-point addition makes the result order-dependent.

This implies a hidden requirement of `reduce`: **operations like integer addition, multiplication, bitwise AND/OR/XOR, logical AND/OR, and `max`/`min` satisfy associativity, so parallel execution yields consistent results. For order-sensitive operations like floating-point addition, results may drift when parallelized.** Using `reduce` for floating-point summation is generally acceptable (the error is within floating-point precision limits), but if you rely on "exactly reproducing a specific value," you must revert to the serial `accumulate`.

::: warning reduce / scan requires associative operations
As soon as you enable a parallel execution policy (or use the default `reduce`), do not expect it to maintain a left-associative order. Integer and unsigned operations are fine; floating-point results will drift based on association order. Use `accumulate` if strict ordering is required.
:::

By the way, here is a quick note on something we haven't expanded on yet: `reduce` is currently **not in the C++20 `std::ranges` namespace**—`ranges::reduce` does not exist. This is because the design for parallelizing algorithms (the family with execution policies) within ranges still has unresolved details, so the standards committee did not include it in C++20. We will discuss this further in the next article when we cover the `fold` family, as `fold` is the "serializable fold" adapted for ranges and is, in a sense, the ranges counterpart to `reduce`.

## C++17 Number Theory: `gcd` and `lcm`

Starting from this section, we cover some "small yet practical" number theory and geometry tools found in `<numeric>`. First up are the greatest common divisor and least common multiple, introduced in C++17:

```cpp
std::gcd(54, 24)   // 6
std::lcm(4, 6)     // 12
std::gcd(17, 13)   // 1（互素）
```

`gcd` / `lcm` are template functions that work for all integer types, using an efficient implementation of the Euclidean algorithm internally. You no longer need to write the Euclidean algorithm by hand or dig through Boost. Here are a few edge cases worth noting (all verified):

- `gcd(0, 0) = 0`, `gcd(0, 12) = 12` — `gcd(0, n)` is simply `|n|`.
- `lcm(0, x) = 0` — if either argument is 0, the least common multiple is 0 (any number is a "multiple" of 0, but the standard specifies 0).

::: warning lcm(0, x) = 0, not an exception
Mathematically, `lcm(0, x)` is somewhat ambiguous, but the standard library returns `0`. If you are writing code involving fraction reduction or period alignment, do not assume `lcm` will always return a positive number; passing 0 yields 0.
:::

## C++20: `midpoint` Saves `(a+b)/2`, `lerp` Does Linear Interpolation

C++20 provides two tools that may look plain, but are specifically designed to fix real-world bugs.

### `midpoint`: Safe Midpoint Calculation

In scenarios like binary search or splitting intervals, calculating the midpoint of two numbers is a frequent operation. The intuitive approach is `(a + b) / 2` — however, this **overflows** when `a` and `b` are both close to the type's limit. For example, if two `int64` values are around 9e18, their sum exceeds the maximum value of `int64` (approx 9.22e18). Signed integer overflow is undefined behavior, and the result might be a negative number. Let's verify this pitfall directly:

```cpp
// Standard: C++20
#include <cstdint>
#include <iostream>
#include <numeric>

int main()
{
    std::int64_t big1 = 7'000'000'000'000'000'000LL;   // 7e18
    std::int64_t big2 = 9'000'000'000'000'000'000LL;   // 9e18
    auto naive = (big1 + big2) / 2;                      // 溢出!
    auto safe  = std::midpoint(big1, big2);
    std::cout << "naive (big1+big2)/2 = " << naive << " (溢出!)\n";
    std::cout << "midpoint(big1,big2) = " << safe << " (正确)\n";
    return 0;
}
```

Here is the output:

```text
```

```text
naive (big1+big2)/2 = -1223372036854775808 (溢出!)
midpoint(big1,big2) = 8000000000000000000 (正确)
```

`(big1+big2)/2` yields `-1223372036854775808`—an absurd negative number, which is a classic symptom of an overflow failure. The correct midpoint should be `8000000000000000000` (8e18), and `std::midpoint` calculates it correctly.

`midpoint` uses an internal algorithm that avoids overflow (roughly `a + (b - a) / 2`, while carefully handling signs and odd/even cases). It never performs the `a + b` step, so overflow is impossible. This is a classic example in C++20 of elevating a "trivial operation everyone gets wrong" into a standard facility—stop writing `(a+b)/2` manually. When implementing binary search, divide-and-conquer, or interval halving, just use `std::midpoint`.

`midpoint` also has an overload that can calculate the midpoint of two **pointers**:

```cpp
int arr[]{10, 20, 30, 40, 50};
auto mid = std::midpoint(arr, arr + 4);
std::cout << "midpoint(arr, arr+4) -> arr[" << (mid - arr) << "] = " << *mid << '\n';
// 输出: midpoint(arr, arr+4) -> arr[2] = 30
```

The pointer-based version correctly handles both even and odd lengths (for a length of four, it takes an offset of two; for a length of five, it "rounds down" to an offset of two). It is more robust than a handwritten `(lo + hi) / 2` when implementing binary search or partitioning logic. The pointer midpoint has an additional benefit: it avoids the illegal operation of "adding two pointers" (in C++, pointers can only be subtracted, not added). Therefore, purely from a syntax perspective, `midpoint` is cleaner than a handwritten loop.

### `lerp`: Linear Interpolation (Note that it is not in `<numeric>`)

`std::lerp(a, b, t)` calculates the linear interpolation `a + t * (b - a)`. It returns `a` when `t=0`, returns `b` when `t=1`, and returns the midpoint when `t=0.5`. It is used universally for interpolation in animations, gradients, and games:

```cpp
std::lerp(0.0, 100.0, 0.25)   // 25
std::lerp(0.0, 100.0, 1.0)    // 100
std::lerp(0.0, 100.0, 2.0)    // 200（可外插，t 不限 [0,1]）
```

It might look unremarkable, but it offers guarantees that a hand-written `a + t*(b-a)` cannot: it returns exactly `a` when `t=0` and exactly `b` when `t=1` (the manual version might result in `99.9999...` due to floating-point errors), and it has well-defined behavior for infinities and NaNs. These are meaningful guarantees for numerical and graphical code.

::: warning lerp is in `<cmath>`, not `<numeric>`
This is a common header file pitfall: `gcd`, `lcm`, and `midpoint` are all in `<numeric>`, but `std::lerp` is specifically in **`<cmath>`**. If you only `#include <numeric>` and try to use `std::lerp`, compilation will fail with `'lerp' is not a member of 'std'`. We've verified this—you must explicitly `#include <cmath>`.
:::

## Common Pitfalls

Let's summarize the common issues encountered when using this family of algorithms. Each has been verified through testing:

::: warning accumulate / inner_product: Initial value determines return type
For sums of floating-point numbers, pass `0.0`. For sums of large integers, pass `0LL`. If you pass `0` (an `int`) to accumulate a sequence of `double`, each element will be truncated to an `int` before accumulation, silently discarding the fractional part without a compiler warning. This is the most classic and insidious pitfall of `accumulate`.

::: warning reduce / scan: Operations must be associative when parallel
When using an execution policy (or relying on `reduce`'s associative semantics), the order of combination is arbitrary. Integer and bitwise operations are safe, but floating-point addition results will drift depending on the order (observed `10100958` vs `10099760`). If strict left-associativity is required, use `accumulate` or `partial_sum`.

::: warning (a+b)/2 overflows for large integers, use midpoint
When calculating midpoints for binary search or interval splitting, `a + b` can overflow. Testing `(7e18 + 9e18) / 2` yields `-1223372036854775808`. Starting with C++20, always use `std::midpoint`, which works for both integers and pointers.

::: warning std::lerp is in `<cmath>`, not `<numeric>`
`gcd`, `lcm`, and `midpoint` are in `<numeric>`, but `lerp` is in `<cmath>`. Including only `<numeric>` will cause a compilation error when using `lerp`; remember to include `<cmath>`.

## Summary

The `<numeric>` library looks like a collection of "for-loop" utilities, but each hides at least one design decision worth knowing. Here are the key takeaways:

- In `accumulate`, the return type equals the initial value type. For floating-point sequences, pass `0.0`, or the values will be truncated to integers (this pitfall is finally fixed in C++23's `fold`, covered in the next article).
- `iota` fills with incrementing values and is the standard way to generate index sequences. `inner_product` computes the inner product of two sequences; it's a single-threaded legacy interface, so consider `transform_reduce` for new code.
- Prefix sum family: `partial_sum` (includes current), `adjacent_difference` (difference, inverse of `partial_sum`). C++17's `inclusive_scan` (includes current) / `exclusive_scan` (excludes current) clarify semantics and add parallel support.
- `reduce` is a parallel version of `accumulate`, requiring the operation to be associative. Floating-point addition is not associative, so parallel results will drift. It is not yet ranges-aware; we'll discuss why when covering `fold` in the next article.
- C++17 number theory `gcd` / `lcm` (note `lcm(0, x) = 0`); C++20 `midpoint` fixes the overflow in `(a+b)/2` and works on pointers; `lerp` performs linear interpolation but resides in `<cmath>`, not `<numeric>`.

In the next article, we dive into C++23's `fold` family to see how it fixes `accumulate`'s return type defect from the root, and how it relates to `reduce` and ranges folding.

## References

- [cppreference: `<numeric>`](https://en.cppreference.com/w/cpp/numeric) — Overview of the entire algorithm family
- [cppreference: std::accumulate](https://en.cppreference.com/w/cpp/algorithm/accumulate) — Official specification for return type = initial value type (`T init`)
- [cppreference: std::reduce (C++17)](https://en.cppreference.com/w/cpp/algorithm/reduce) — Parallel semantics and the "operation must be associative" requirement
- [cppreference: std::exclusive_scan / inclusive_scan (C++17)](https://en.cppreference.com/w/cpp/algorithm/exclusive_scan) — Two prefix sum semantics: excluding vs. including current position
- [cppreference: std::midpoint (C++20)](https://en.cppreference.com/w/cpp/numeric/midpoint) — Overflow-free midpoint, integer and pointer overloads
- [cppreference: std::lerp (C++20)](https://en.cppreference.com/w/cpp/numeric/lerp) — Linear interpolation, note it is defined in `<cmath>`
- [cppreference: std::gcd / std::lcm (C++17)](https://en.cppreference.com/w/cpp/numeric/gcd) — Number theory tools and the `lcm(0,x)=0` convention
