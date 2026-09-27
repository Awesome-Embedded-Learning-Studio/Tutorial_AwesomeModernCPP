---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: Mastering <cmath>—floating-point classification with fpclassify/isnan/isinf/isnormal
  and the NaN not-equal-to-itself trap, why you should never compare floats with ==,
  the undefined behavior of abs(INT_MIN), how hypot rescues overflow, why fma's single
  rounding preserves precision, C++17 special math functions, and C++20 std::numbers
  compile-time constants
difficulty: intermediate
order: 59
platform: host
prerequisites:
- 'numeric: Accumulate, Fill, Inner Product, and Adjacent Difference'
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New
  Tricks'
reading_time_minutes: 16
related:
- 'numeric: Accumulate, Fill, Inner Product, and Adjacent Difference'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'cmath: Mathematical Functions, Floating-Point Classification, and Precision Pitfalls'
translation:
  source: documents/vol3-standard-library/time-numeric/59-cmath.md
  source_hash: 3234889ce7b39400e1e2d68f41263d093ead10d12eade75349f312aded2ad01c
  translated_at: '2026-09-26T00:18:06+00:00'
  engine: anthropic
  token_count: 7400
---
# `<cmath>`: Mathematical Functions, Floating-Point Classification, and Precision Pitfalls

By this article, we have already been through containers, iterators, algorithms, and even `<numeric>`. But one category of tools has never gotten a proper treatment—mathematical operations. We have used `sqrt` / `pow` / `sin` / `cos` for years, and they look like "free points"—yet the moment they collide with how floating-point numbers are actually represented, things stop being "call a function, get a result" altogether.

In this article we take `<cmath>` apart—but not by copying out a function table; cppreference does that more thoroughly than anyone. What we cover are the three things that will genuinely wreck you: which "exceptional states" a floating-point value can be in (`NaN` / `inf` / subnormals) and how they compare against each other; why `==` is pretty much a trap in the floating-point world, and what to use instead; and which standard library functions "look similar but behave completely differently" (the integer trap of `abs`, `hypot` rescuing you from overflow, `fma`'s single rounding that preserves precision). Along the way we touch on the C++17 special math functions and C++20's compile-time math constants `std::numbers`. We already covered the `lerp` / `midpoint` pair thoroughly in the `<numeric>` article (`lerp` actually lives in `<cmath>`; we won't repeat it here)—this article focuses on floating-point classification, the everyday functions, and precision.

## Floating-Point Classification: First Know "Which Kind" of Number You Have

Every mathematical function in `<cmath>` is built on the IEEE 754 floating-point representation. A `double` is not "a box that can hold any real number"; it is a finite set encoded in 64 bits as sign bit + exponent + mantissa. This section first sorts out the states a floating-point value can be in, because every pitfall that follows—`NaN` not equal to itself, `==` failing, subnormals losing precision—derives directly from these states.

`<cmath>` provides a set of classification functions, used together with `fpclassify`, the "main entry point". First, a piece of code that runs them all:

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    double nan = std::numeric_limits<double>::quiet_NaN();
    double inf = std::numeric_limits<double>::infinity();

    std::cout << std::boolalpha;
    std::cout << "isnan(NaN)?        " << std::isnan(nan) << '\n';
    std::cout << "isinf(inf)?        " << std::isinf(inf) << '\n';
    std::cout << "isfinite(inf)?     " << std::isfinite(inf) << '\n';
    std::cout << "isfinite(3.14)?    " << std::isfinite(3.14) << '\n';
    std::cout << "isnormal(3.14)?    " << std::isnormal(3.14) << '\n';

    // A few exceptional states from different sources
    std::cout << "sqrt(-1) is NaN:   " << std::isnan(std::sqrt(-1.0)) << '\n';
    std::cout << "0.0/0.0 is NaN:    " << std::isnan(0.0 / 0.0) << '\n';
    std::cout << "1.0/0.0 is inf:    " << std::isinf(1.0 / 0.0) << '\n';

    std::cout << "\n--- fpclassify 逐类 ---\n";
    double values[] = {3.14, inf, -inf, nan, 0.0, -0.0};
    const char* names[] = {"3.14", "+inf", "-inf", "NaN", "0.0", "-0.0"};
    for (int i = 0; i < 6; ++i) {
        int c = std::fpclassify(values[i]);
        const char* cls = "unknown";
        switch (c) {
            case FP_INFINITE:  cls = "FP_INFINITE";  break;
            case FP_NAN:       cls = "FP_NAN";       break;
            case FP_NORMAL:    cls = "FP_NORMAL";    break;
            case FP_SUBNORMAL: cls = "FP_SUBNORMAL"; break;
            case FP_ZERO:      cls = "FP_ZERO";      break;
        }
        std::cout << names[i] << " -> " << cls
                  << "  signbit=" << std::signbit(values[i]) << '\n';
    }
    return 0;
}
```

Run with `g++ -std=c++20 -O2` (local GCC 16.1.1):

```text
isnan(NaN)?        true
isinf(inf)?        true
isfinite(inf)?     false
isfinite(3.14)?    true
isnormal(3.14)?    true
sqrt(-1) is NaN:   true
0.0/0.0 is NaN:    true
1.0/0.0 is inf:    true

--- fpclassify 逐类 ---
3.14 -> FP_NORMAL  signbit=0
+inf -> FP_INFINITE  signbit=0
-inf -> FP_INFINITE  signbit=1
NaN -> FP_NAN  signbit=0
0.0 -> FP_ZERO  signbit=0
-0.0 -> FP_ZERO  signbit=1
```

Together these categories are the complete "classification" of an IEEE 754 `double`: normal numbers (`FP_NORMAL`), zero (`FP_ZERO`—note that both positive and negative zero exist), infinities (`FP_INFINITE`, also signed), `NaN` (`FP_NAN`, not-a-number), and the subnormals (`FP_SUBNORMAL`) that we are about to cover on their own. `fpclassify` is the main entry; `isnan` / `isinf` / `isfinite` / `isnormal` are its shortcut predicates—when you only need to test one category, the shortcut function is more readable; when you need to switch over every case, go with `fpclassify`.

### `NaN` Does Not Equal Itself: The Classic Trap of the Floating-Point World

Above we tested `NaN` with `std::isnan` rather than `== nan`. That is not a style preference; it is a hard requirement—because **a `NaN` compared with any value (including itself) returns `false`**. Verified in practice:

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    double nan = std::numeric_limits<double>::quiet_NaN();
    std::cout << std::boolalpha;
    std::cout << "NaN == NaN?   " << (nan == nan) << '\n';
    std::cout << "NaN != NaN?   " << (nan != nan) << '\n';
    return 0;
}
```

```text
NaN == NaN?   false
NaN != NaN?   true
```

Want to run it and watch the NaN trap spring? Open this online demo:

<OnlineCompilerDemo
  title="NaN Does Not Equal Itself: The Classic Floating-Point Comparison Trap"
  source-path="code/examples/vol3/59_cmath_nan.cpp"
  description="NaN == NaN is false while NaN != NaN is true—IEEE 754 dictates that any comparison involving NaN returns false, so the only correct way to test for NaN is std::isnan"
  allow-run
/>

`NaN == NaN` is `false`, and `NaN != NaN` is `true` instead. This is semantics fixed at the standard level by IEEE 754: `NaN` stands for "a result with no meaningful value" (`0/0`, `sqrt(-1)`, `inf - inf`), "equality" is undefined for it, so it always compares unequal. This leads straight to a writing trap—**to test for `NaN` you can only ever use `std::isnan`, never `==`**:

```cpp
double x = compute_something();
if (x == std::numeric_limits<double>::quiet_NaN()) {   // never entered!
    handle_error();
}
if (std::isnan(x)) {                                    // this is the right way
    handle_error();
}
```

Sneakier still, `NaN` "contaminates" subsequent computation—let a `NaN` take part in any arithmetic and the result is almost always `NaN`. So a `NaN` that is not intercepted at the source with `isnan` propagates down the whole expression, until you end up staring at a baffling output and tracing back for ages before finding out it came from one `sqrt(-1)` upstream.

::: warning Always test for NaN/inf with isnan/isinf, never ==
`== NaN` is always `false`—writing it is already a bug. Test for `NaN` with `std::isnan`, for infinity with `std::isinf`, and for "neither NaN nor inf" with `std::isfinite`. These three predicates exist in `<cmath>` precisely to route around "NaN does not equal itself"; there is no reason to hand-roll comparisons.
:::

### Subnormals: Why `isnormal` Returns `false`

Looking at the output above, `3.14` is `FP_NORMAL` and `0.0` is `FP_ZERO`. So what is `FP_SUBNORMAL` (the subnormals, also called denormals)? It is a gradual-precision tier IEEE 754 designed to support "positive numbers smaller than the smallest normal number"—at the cost of degraded precision in this range (fewer significant mantissa bits).

Let's grab the boundary values straight from `std::numeric_limits<double>`:

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    double smallest_normal = std::numeric_limits<double>::min();      // smallest normal
    double smallest_denorm = std::numeric_limits<double>::denorm_min(); // smallest subnormal
    double half = smallest_normal / 2.0;
    std::cout << "smallest normal      = " << smallest_normal << '\n';
    std::cout << "smallest denorm      = " << smallest_denorm << '\n';
    std::cout << "min/2 (subnormal)    = " << half << '\n';
    std::cout << "isnormal(min)?       " << std::isnormal(smallest_normal) << '\n';
    std::cout << "isnormal(min/2)?     " << std::isnormal(half) << '\n';
    std::cout << "isnormal(denorm_min)? " << std::isnormal(smallest_denorm) << '\n';
    std::cout << "fpclassify(min/2)==FP_SUBNORMAL? "
              << (std::fpclassify(half) == FP_SUBNORMAL) << '\n';
    return 0;
}
```

```text
smallest normal      = 2.22507e-308
smallest denorm      = 4.94066e-324
min/2 (subnormal)    = 1.11254e-308
isnormal(min)?       true
isnormal(min/2)?     false
isnormal(denorm_min)? false
fpclassify(min/2)==FP_SUBNORMAL? true
```

The smallest normal is `2.22507e-308`. Divide it by 2 and the result `1.11254e-308` is still representable, but it is no longer a normal number—`isnormal` returns `false`, and `fpclassify` reports `FP_SUBNORMAL`. That is what subnormals are: the value can be stored, but precision is degraded.

Why does this deserve its own section? Because some code assumes "as long as `x != 0`, it is a number that participates in computation normally"—but once `x` lands in the subnormal range, precision collapses, and accumulation can even stall in place; some platforms/compilers also enable FTZ (flush-to-zero), treating subnormals as zero outright for performance, so the same code produces different results on different machines. `std::isnormal(x)` answers exactly "is this a normal number with guaranteed precision": `0.0`, subnormals, `inf`, and `NaN` all return `false`; only `FP_NORMAL` returns `true`. For numerically sensitive decisions (say, "is this denominator too small and needs clamping to a threshold"), `isnormal` is far more dependable than a bare `x == 0`.

### `signbit`: The Sign of Negative Zero and Negative Infinity

That last one, `signbit`, looks redundant—if you want the sign, why not just `x < 0`? Most of the time you can, but there are three values `x < 0` cannot handle: positive and negative zero compare equal under `==` (`0.0 == -0.0` is `true`), and `NaN` compared against anything is `false`—including "negative `NaN`". `signbit` reads the sign bit directly, so it can distinguish the cases comparison semantics cannot reach:

```text
0.0 == -0.0?      true
signbit(0.0)?     false
signbit(-0.0)?    true
```

In practice, `1.0 / 0.0` gives `+inf` while `1.0 / -0.0` gives `-inf`—the sign of zero makes itself felt in division, which is why the standard library provides a dedicated `signbit`.

## The Everyday Functions and Their Traps: `abs` / `hypot` / `fmod`

With classification behind us, let's look at the operations you use every day. No long-winded lecturing in this part—we just pick the points where functions "look alike but behave very differently" and cover them thoroughly.

### The Integer Trap of `std::abs`

`abs` is the most basic of needs—take the absolute value. But on integers it hides a pitfall of undefined behavior. Watch the measurement:

```cpp
// Standard: C++20
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
    int imin = std::numeric_limits<int>::min();   // usually -2147483648
    auto r = std::abs(imin);
    std::cout << "INT_MIN              = " << imin << '\n';
    std::cout << "std::abs(INT_MIN)    = " << r << '\n';
    std::cout << "INT_MIN == abs?      = " << (imin == r ? "yes (overflowed)" : "no") << '\n';

    // Floating-point overloads: abs and fabs are equivalent
    double dn = -3.14;
    std::cout << "\nstd::fabs(-3.14)     = " << std::fabs(dn) << '\n';
    std::cout << "std::abs(-3.14)      = " << std::abs(dn) << '\n';

    // long long integer abs is also overloaded, and safe
    long long big = -9000000000000000000LL;
    std::cout << "std::abs(-9e18 LL)   = " << std::abs(big) << '\n';
    return 0;
}
```

```text
INT_MIN              = -2147483648
std::abs(INT_MIN)    = -2147483648
INT_MIN == abs?      = yes (overflowed)

std::fabs(-3.14)     = 3.14
std::abs(-3.14)      = 3.14
std::abs(-9e18 LL)   = 9000000000000000000
```

`std::abs(INT_MIN)` hands back `-2147483648`—a negative number. Taking an absolute value produced a negative. The cause lies in two's-complement representation: 32-bit two's complement can represent one more negative than positives (`[-2^31, 2^31-1]`), `INT_MIN` is `-2^31`, and its absolute value `2^31` simply cannot be represented in an `int`. So `abs` overflows, and the result is **undefined behavior**—on this machine's GCC 16.1.1 it happens to wrap around back to the original value, but the standard guarantees no particular result, and the optimizer may even perform transformations premised on "UB will not happen" that surprise you further.

::: warning abs(INT_MIN) is undefined behavior
The absolute value of `INT_MIN` cannot be represented in an `int` of the same type, so `std::abs(INT_MIN)` is UB. If your input might reach `INT_MIN` (say, parsing integers that can return extreme values, or using `INT_MIN` as a sentinel), either use a wider type (convert to `long long` first, then `abs`) or check explicitly before the call. Floating point has no such problem—the `std::fabs` and `std::abs(double)` overloads are both safe.
:::

One more historically common trap deserves a mention: the C-era `abs` in `<cstdlib>` only works for `int`; passing a `long` / `long long` gets silently truncated. C++'s `std::abs` has the full overload set in `<cmath>` / `<cstdlib>` (`int` / `long` / `long long` / `float` / `double` / `long double`), so **as long as you use `std::abs` rather than bare `abs`, and include the right header**, you won't run into the old C truncation trap.

### `hypot`: Naive `sqrt(x*x+y*y)` Overflows

For the hypotenuse of a right triangle, or the magnitude of a vector, the intuitive way to write it is `std::sqrt(x*x + y*y)`. It works—but it hides a numerical overflow trap: when `x` and `y` themselves have not overflowed, yet `x*x` already overflows to `inf`, the result is ruined. `std::hypot` internally uses an equivalent algorithm that avoids intermediate overflow, made precisely to rescue this case:

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>

int main()
{
    std::cout << "hypot(3,4)           = " << std::hypot(3.0, 4.0) << '\n';
    std::cout << "sqrt(3*3+4*4)        = " << std::sqrt(3.0*3.0 + 4.0*4.0) << '\n';

    double x = 1e200, y = 1e200;   // x and y are both within double's range
    std::cout << "\nnaive sqrt(x*x+y*y) = " << std::sqrt(x*x + y*y) << '\n';
    std::cout << "hypot(1e200, 1e200)  = " << std::hypot(x, y) << '\n';
    return 0;
}
```

```text
hypot(3,4)           = 5
sqrt(3*3+4*4)        = 5

naive sqrt(x*x+y*y) = inf
hypot(1e200, 1e200)  = 1.41421e+200
```

`1e200` has not overflowed, but `1e200 * 1e200 = 1e400` far exceeds the `double` maximum (about `1.8e308`); the squaring step turns directly into `inf`, the square root of `inf` is still `inf`, and the correct result `1.41421e+200` is completely lost. `hypot` got it right. Since C++17, `hypot` also has a three-argument overload `std::hypot(x, y, z)`; three-dimensional vector magnitudes work the same way.

The lesson: whenever you compute a magnitude, a Euclidean distance, or anything shaped like $\sqrt{\sum x_i^2}$, don't save effort with the naive square-sum-then-root; reach for `hypot` directly (for higher dimensions, chain `hypot` calls or use a more robust algorithm). Beyond overflow protection, it also guards against underflow (extremely small numbers squaring down to 0), so precision is steadier.

### `fmod`: Floating-Point Remainder, Sign Follows the Dividend

`std::fmod(x, y)` is the floating-point version of remainder: the result is `x - n*y`, where `n` is the quotient "truncated toward zero". It differs from `std::remainder` (C++11, remainder toward the nearest integer) in sign rules—a common point of confusion:

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>

int main()
{
    std::cout << "fmod(5.3, 2.0)       = " << std::fmod(5.3, 2.0) << '\n';
    std::cout << "fmod(-5.3, 2.0)      = " << std::fmod(-5.3, 2.0) << '\n';
    std::cout << "remainder(-5.3, 2.0) = " << std::remainder(-5.3, 2.0) << '\n';
    return 0;
}
```

```text
fmod(5.3, 2.0)       = 1.3
fmod(-5.3, 2.0)      = -1.3
remainder(-5.3, 2.0) = 0.7
```

`fmod(-5.3, 2.0)` gives `-1.3`—the result's sign follows the **dividend** `x` (because the quotient truncates toward zero to `-2`, and `-5.3 - (-2)*2 = -1.3`). Meanwhile `remainder(-5.3, 2.0)` gives `0.7`—it takes the **nearest** integer quotient `-3`, so `-5.3 - (-3)*2 = 0.7`. Both are correct, but the semantics differ: for periodic mapping (say, folding an angle into `[-pi, pi]`) you usually want `remainder`, or `fmod` plus a manual shift—mix them up and you get a result with the opposite sign.

## Precision: Don't Compare Floats with `==`, Use Epsilon; `fma` Keeps a Single Rounding

This is the section most worth remembering in the whole article. The `NaN` / subnormal material above was about "extreme states"; this section is about how **normal computation goes wrong too**—floating-point `==` is almost always wrong.

### Why `==` Fails: Accumulated Error

Floating-point numbers are finite binary fractions; most decimal fractions (`0.1`, `0.2`) are infinitely repeating in binary and get truncated into approximations inside a `double`. Every operation introduces a tiny rounding error, and once those errors accumulate, two numbers that "should be equal" compare unequal under `==`. The classic demonstration—adding `0.1` ten times:

```cpp
// Standard: C++20
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(17);
    double acc = 0.0;
    for (int i = 0; i < 10; ++i) acc += 0.1;
    std::cout << "sum of 10*0.1  = " << acc << '\n';
    std::cout << "acc == 1.0?    = " << (acc == 1.0 ? "true" : "false") << '\n';
    return 0;
}
```

```text
sum of 10*0.1  = 0.99999999999999989
acc == 1.0?    = false
```

Mathematically `0.1 * 10 == 1`, but the floating-point accumulation produces `0.99999999999999989`, and `== 1.0` says `false`. If you feed that `acc` into `if (acc == 1.0) ...`, that branch is never entered. That is why **floating-point numbers must never be compared for equality directly with `==`**.

The alternative is tolerance-based comparison, commonly known as epsilon comparison. The basic idea: if the absolute difference of the two numbers falls within some tiny threshold (absolute tolerance + relative tolerance), consider them equal:

```cpp
// Standard: C++20
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

bool nearly_equal(double a, double b, double abs_eps, double rel_eps)
{
    double diff = std::fabs(a - b);
    if (diff <= abs_eps) return true;                       // absolute tolerance: handles values near 0
    return diff <= rel_eps * std::max(std::fabs(a), std::fabs(b));  // relative tolerance: scales with magnitude
}

int main()
{
    std::cout << std::setprecision(17);
    double acc = 0.0;
    for (int i = 0; i < 10; ++i) acc += 0.1;
    std::cout << "acc == 1.0?            = " << (acc == 1.0) << '\n';
    std::cout << "nearly_equal(eps=1e-9) = "
              << nearly_equal(acc, 1.0, 1e-12, 1e-9) << '\n';
    return 0;
}
```

```text
acc == 1.0?            = false
nearly_equal(eps=1e-9) = true
```

The essentials of this `nearly_equal` approach: the **absolute tolerance** `abs_eps` specifically covers the "both numbers near 0" case (where relative tolerance stops working, because the denominator is near 0 too); the **relative tolerance** `rel_eps` scales with the numbers' magnitude, handling large values (for numbers bigger than `1e9`, the expected error is also bigger than `1e-6`). Use both together and you cover the whole range of magnitudes. What threshold to pick depends on the domain—`1e-5` is plenty in graphics, while scientific computing often wants `1e-12`. There is no silver bullet, but the step of "replace `==` with tolerance-based comparison" is almost always the right one.

### `fma`: One Fused Multiply-Add, Only One Rounding

The utterly ordinary expression `a * b + c` takes two operations and two roundings the naive way (`a*b` is computed and rounded to `double` first, then `c` is added and it rounds again). `std::fma(a, b, c)` (borrowed from C99, standard since C++11) turns this expression into a **single** fused multiply-add—the infinitely precise intermediate product is added to `c` directly, and the result is **rounded only once**.

The benefit of rounding once is precision. The cost is—as measured below—when the compiler has already contracted the naive expression into a hardware FMA instruction, `std::fma` can be **slower** (because it is forced through the standard library implementation to preserve the semantics). First, the precision difference; this one is rock solid:

```cpp
// Standard: C++20
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(17);
    // In the naive two-step form, (1/3)*3 rounds to 1.0, then minus 1 gives 0
    // fma rounds once, preserving the true approximation of (1/3)*3
    double p = 1.0 / 3.0;
    double naive = p * 3.0 + (-1.0);
    double fused = std::fma(p, 3.0, -1.0);
    std::cout << "naive (1/3)*3 - 1 = " << naive << '\n';
    std::cout << "fma   (1/3)*3 - 1 = " << fused << '\n';
    return 0;
}
```

```text
naive (1/3)*3 - 1 = 0
fma   (1/3)*3 - 1 = -5.5511151231257827e-17
```

The two results **differ**: the naive form gives `0`, `fma` gives `-5.55e-17`. Which one is "right"? That depends on how you define "right"—`1.0/3.0` is truncated inside a `double` to `0.333...` (an approximation slightly smaller than the true `1/3`); in the naive form, `*3` rounds it back to exactly `1.0` (the error happens to be "corrected away" by the second multiplication), and `-1` gives `0`. `fma`, doing no intermediate rounding, honestly reflects the fact that `0.333... * 3` is slightly less than 1, and subtracting 1 yields a tiny negative number. From the standpoint of "faithfully reflecting the true mathematical result of the intermediate approximation", `fma` is more honest; from the standpoint of "I want `(x/x)*x` to compute `x`", the naive form is actually more convenient. This is exactly the subtlety of floating-point precision—**there is no absolutely correct answer, only "I know exactly what I am asking for"**.

This example also confirms the previous section's conclusion: even an identity as "obviously true" as `(1/3)*3 == 1.0` holds in the naive form and fails in the `fma` form—yet more proof of how unreliable `==` is for floating point.

What about performance? In theory hardware FMA beats "one multiply + one add" (one instruction doing two jobs). But `std::fma` is a **library function** whose semantics must guarantee "single rounding", so the compiler cannot freely fold it into its context. Let's run two comparisons with noinline loops:

```cpp
// Standard: C++20
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>

__attribute__((noinline)) double run_naive(const double* x, const double* y,
                                           const double* z, std::size_t n)
{
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += x[i] * y[i] + z[i];
    return s;
}

__attribute__((noinline)) double run_fma(const double* x, const double* y,
                                         const double* z, std::size_t n)
{
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += std::fma(x[i], y[i], z[i]);
    return s;
}

int main()
{
    constexpr std::size_t kN = 4'000'000;
    static double x[kN], y[kN], z[kN];
    for (std::size_t i = 0; i < kN; ++i) {
        x[i] = static_cast<double>(i % 1000) * 0.001 + 0.5;
        y[i] = static_cast<double>(i %  500) * 0.002 + 0.25;
        z[i] = static_cast<double>(i %  200) * 0.003 + 0.125;
    }
    volatile double sink = 0.0;
    auto t1 = std::chrono::high_resolution_clock::now();
    double r1 = run_naive(x, y, z, kN);
    auto t2 = std::chrono::high_resolution_clock::now();
    double r2 = run_fma(x, y, z, kN);
    auto t3 = std::chrono::high_resolution_clock::now();
    sink = r1 + r2;
    std::cout << "naive a*b+c: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
              << " ms\n";
    std::cout << "fma:         "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count()
              << " ms\n";
    return 0;
}
```

Run with `g++ -std=c++20 -O2` (local GCC 16.1.1) three times in a row; the result is stable:

```text
naive a*b+c: 4 ms
fma:         13 ms
```

`fma` is instead **more than three times slower**. The reason: at `-O2` the compiler already auto-contracts `x[i] * y[i] + z[i]` into a hardware FMA instruction (`vfmadd` on x86) and vectorizes the loop along the way—one instruction does the multiply-add while collecting the SIMD bonus; the explicit `std::fma`, held to the library semantics of strictly guaranteeing "single rounding", is something the compiler dare not freely vectorize and fold, so it degenerates into per-element calls and ends up slower.

So keep a clear-eyed view of `fma`:

::: warning fma is a precision tool, not necessarily a performance tool
`std::fma`'s core value is **precision** (single rounding, avoiding lost bits in intermediate results), not speed. In the common scenario where your naive `a*b+c` has already been contracted into a hardware FMA by the compiler, the explicit `std::fma` may well be slower. You need `fma` when the naive computation shows visible precision loss (catastrophic cancellation, intermediate overflow or lost bits) and you cannot rely on the compiler's auto-contraction (`-ffp-contract=off` is on, or you need strict cross-platform reproducibility). Otherwise the naive form is accurate enough and fast enough.
:::

The most practical way to judge "is the naive form accurate enough" is exactly what we did above: run the expression you distrust and the `fma` version side by side and compare the results—if the difference is within your tolerance, keep the naive form; otherwise switch to `fma`.

## C++17 Special Math Functions: `beta` / `riemann_zeta` / Elliptic Integrals

As of C++17, `<cmath>` took in a whole batch of "special mathematical functions"—the beta function, the Riemann zeta function, assorted Laguerre/Legendre/Hermite polynomials, and all kinds of elliptic integrals. These mainly serve scientific computing and engineering (quantum mechanics, statistics, electromagnetic fields); everyday business code barely touches them. Here we only point out that they exist, and measure a couple of them so you get a firsthand impression:

```cpp
// Standard: C++17
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(17);
    std::cout << "beta(1,1)           = " << std::beta(1.0, 1.0) << '\n';      // mathematically = 1
    std::cout << "riemann_zeta(2)     = " << std::riemann_zeta(2.0) << '\n';   // = pi^2/6 ~ 1.6449
    std::cout << "comp_ellint_2(0)    = " << std::comp_ellint_2(0.0) << '\n';  // = pi/2 ~ 1.5708
    std::cout << "assoc_laguerre(2,0,1) = " << std::assoc_laguerre(2, 0, 1.0) << '\n';
    return 0;
}
```

```text
beta(1,1)           = 1
riemann_zeta(2)     = 1.6449340668482264
comp_ellint_2(0)    = 1.5707963267948968
assoc_laguerre(2,0,1) = -0.5
```

`beta(1,1)` is `1` (the beta function $B(1,1) = \Gamma(1)\Gamma(1)/\Gamma(2) = 1$), `riemann_zeta(2)` is $1.6449...$ (the famous Basel problem, $\pi^2/6$), and `comp_ellint_2(0)` is $\pi/2 \approx 1.5708$—all matching the mathematical expectations. GCC 16.1.1 supports every one of them.

One thing to flag up front: rumors keep surfacing that this batch of functions is marked "no longer part of C++" (there was a removal discussion during the C++23 cycle, and they were formally removed in the C++26 draft). But for this local toolchain, GCC 16.1.1, they remain fully available. If you are writing long-lived scientific computing code, treat them as "works now, but the implementation may need replacing later" dependencies: wrap them behind one layer instead of scattering calls, so a future migration touches one place only.

## C++20 Math Constants: `std::numbers`

The final topic. Back when you needed $\pi$, the standard practice was `const double PI = 3.141592653589793;` or `M_PI` (POSIX, not in the C++ standard—portability depends on the platform). C++20 provides proper compile-time constants: `std::numbers`:

```cpp
// Standard: C++20
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>

int main()
{
    std::cout << std::setprecision(17);
    std::cout << "std::numbers::pi    = " << std::numbers::pi << '\n';
    std::cout << "std::numbers::e     = " << std::numbers::e << '\n';
    std::cout << "std::numbers::sqrt2 = " << std::numbers::sqrt2 << '\n';
    std::cout << "std::numbers::phi   = " << std::numbers::phi << '\n';   // the golden ratio
    std::cout << "cos(pi)             = " << std::cos(std::numbers::pi) << '\n';

    // They are variable templates, usable at compile time
    constexpr double kPi = std::numbers::pi_v<double>;
    std::cout << "constexpr pi_v      = " << kPi << '\n';
    static_assert(std::numbers::pi_v<double> > 3.14, "pi > 3.14");
    return 0;
}
```

```text
std::numbers::pi    = 3.1415926535897931
std::numbers::e     = 2.7182818284590451
std::numbers::sqrt2 = 1.4142135623730951
std::numbers::phi   = 1.6180339887498949
cos(pi)             = -1
constexpr pi_v      = 3.1415926535897931
```

A whole lineup of constants is there—`std::numbers::pi` / `e` / `sqrt2` / `phi` (the golden ratio) / `ln2` / `log2e` / `egamma` (Euler's constant)—all at `double`'s full precision. Notice that `cos(pi)` comes out as exactly `-1`: the `double` value of `pi` is itself a tiny bit larger than the true $\pi$, and `cos`'s rounding happens to swallow that error, yielding a clean `-1` (which is also why the `fma` section earlier stressed that floating-point results are sometimes "accidentally exact"—don't take such coincidences as a general rule).

`std::numbers::pi` is in fact shorthand for a variable template, `std::numbers::pi_v<T>`, specialized for `T = float/double/long double` respectively. That means you can use it as a compile-time constant—drop it into `constexpr`, `static_assert`, or template arguments—something a hand-written `const double PI = 3.14` cannot do. GCC 16.1.1 supports all of it.

The old `M_PI` way is not unusable (you need `#define _USE_MATH_DEFINES` before `#include <cmath>`, and it depends on a POSIX extension), but since C++20 provides the standard facility, new code should stop using `M_PI`—portable, type-safe, usable at compile time, better on every axis.

## Integer Overflow and `<cmath>`: The Behavior Is Defined

To close out, a brief word on how "overflow" behaves inside `<cmath>`. We already saw with `hypot` that `<cmath>` functions have a fairly deterministic semantics for overflow and domain errors—unlike integer overflow, which is UB:

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>

int main()
{
    std::cout << std::boolalpha;
    std::cout << "exp(1000) isinf? " << std::isinf(std::exp(1000.0)) << '\n';  // overflow -> +inf
    std::cout << "pow(2,1024) isinf? " << std::isinf(std::pow(2.0, 1024.0)) << '\n'; // 2^1024 just barely overflows
    std::cout << "log(0) isinf?   " << std::isinf(std::log(0.0)) << '\n';     // -> -inf
    std::cout << "log(-1) isnan?  " << std::isnan(std::log(-1.0)) << '\n';    // domain error -> NaN
    std::cout << "sqrt(-1) isnan? " << std::isnan(std::sqrt(-1.0)) << '\n';   // domain error -> NaN
    return 0;
}
```

```text
exp(1000) isinf? true
pow(2,1024) isinf? true
log(0) isinf?   true
log(-1) isnan?  true
sqrt(-1) isnan? true
```

The pattern is clear:

- **Overflow** (the result exceeds the `double` maximum, about $1.8 \times 10^{308}$)—returns `±inf`. `exp(1000)` far exceeds that maximum and gives `+inf`; `pow(2, 1024)` gives `inf` as well, because $2^{1024}$ just crosses `double`'s maximum exponent (the largest normal exponent is 1023).
- **Domain error** (meaningless input, e.g. `sqrt` / `log` receiving a negative number)—returns `NaN`.
- **Pole** (e.g. `log(0)`)—returns `-inf`.

None of this is undefined behavior; it is deterministic semantics prescribed by IEEE 754 plus the C standard library (whether `errno` is additionally set depends on `<cerrno>` and the compiler's `-fmath-errno` setting; GCC does not set it by default). By contrast, overflow in integer arithmetic (`abs(INT_MIN)`, `a + b` going out of range) is the real UB. So the one-sentence summary: "inside `<cmath>`, exceptional results have a well-defined shape (`inf` / `NaN`) that `isinf` / `isnan` can detect; the truly dangerous overflows live on the integer side, to be avoided with wider types or up-front checks".

## Summary

`<cmath>` looks like a "catalog of math functions", but what really decides whether you use it correctly is your understanding of floating-point representation and precision. The key takeaways:

- **Floating-point classification**: `fpclassify` is the main entry; `isnan` / `isinf` / `isfinite` / `isnormal` are the shortcut predicates. A floating-point value has five states—normal numbers, zero (including positive and negative zero), infinities, `NaN`, and subnormals; subnormals have degraded precision, and `isnormal` tells you "does this number carry guaranteed precision".
- **`NaN` does not equal itself**: `NaN == NaN` is `false`; always test for `NaN` with `std::isnan` and for infinity with `std::isinf`, never with `==`.
- **`abs(INT_MIN)` is UB**: two's-complement asymmetry means `INT_MIN`'s absolute value cannot be represented in an `int`. Floating point has no such issue—`std::fabs` / `std::abs(double)` are safe.
- **`hypot` rescues overflow**: `sqrt(x*x+y*y)` overflows to `inf` at the intermediate squaring step; `std::hypot`'s overflow-proof algorithm delivers the correct result.
- **Don't compare floats with `==`**: accumulated error makes "should-be-equal" numbers compare unequal. Use a `nearly_equal` with absolute + relative epsilon, and pick thresholds per domain.
- **`fma` protects precision but doesn't necessarily speed things up**: `std::fma(a,b,c)` rounds once to preserve precision, but at `-O2` the naive `a*b+c` is often auto-contracted into a hardware FMA and vectorized on top, ending up faster. Reach for `fma` when precision is critical and you cannot lean on the compiler's contraction.
- **C++17 special functions / C++20 `std::numbers`**: the special math functions (`beta` / `riemann_zeta` / elliptic integrals) serve scientific computing—removed in the C++26 draft, but still supported by GCC 16.1.1; `std::numbers::pi` and friends are full-precision compile-time constants that new code should use in place of `M_PI`.
- **`<cmath>`'s overflow is defined**: overflow gives `inf`, domain errors give `NaN`, poles give `±inf`—all detectable with `isinf` / `isnan`; the genuinely dangerous UB overflows live on the integer side.

In the next article we keep wandering the standard library's math-related facilities, shifting the view from "individual functions" up to "the type level"—how `std::complex` makes complex-number arithmetic as natural as real-number arithmetic, and how its complex overloads mesh with the `<cmath>` functions.

## References

- [cppreference: Common math functions (`<cmath>`)](https://en.cppreference.com/w/cpp/numeric/math) — a function overview, plus the signatures and behavior of `fpclassify` / `isnan` / `abs` / `fma` / `hypot`
- [cppreference: Floating-point environment](https://en.cppreference.com/w/cpp/numeric/fenv) — the floating-point environment, rounding modes, `errno` and `-fmath-errno`
- [cppreference: Special mathematical functions (C++17)](https://en.cppreference.com/w/cpp/numeric/special_functions) — `beta` / `riemann_zeta` / the elliptic integrals, and more
- [cppreference: Mathematical constants `std::numbers` (C++20)](https://en.cppreference.com/w/cpp/numeric/constants) — compile-time constants like `pi` / `e` / `sqrt2`
- [IEEE 754-2019](https://standards.ieee.org/ieee/7594/) — the standard source for floating-point representation, `NaN` semantics, and subnormals
