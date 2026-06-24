---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 'Mastering <cmath>: Floating-point classification (fpclassify/isnan/isinf/isnormal),
  the NaN "not equal to self" pitfall, why you shouldn''t use == for floats, the undefined
  behavior of abs(INT_MIN), how hypot prevents overflow, why fma''s single rounding
  guarantees precision, C++17 special math functions, and C++20 std::numbers compile-time
  constants'
difficulty: intermediate
order: 59
platform: host
prerequisites:
- <numeric>：累加、填充、内积与相邻差
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 16
related:
- <numeric>：累加、填充、内积与相邻差
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'cmath: Mathematical Functions, Floating-Point Classification, and Precision Pitfalls'
translation:
  source: documents/vol3-standard-library/time-numeric/59-cmath.md
  source_hash: e17a74eed616db7571401dbe5900676dea417aeae3d6954fe7934c657b2ea846
  translated_at: '2026-06-24T04:07:38.227373+00:00'
  engine: anthropic
  token_count: 5684
---
# `<cmath>`: Mathematical Functions, Floating-Point Classification, and Precision Pitfalls

By this point, we have covered containers, iterators, algorithms, and even `<numeric>`. However, one category of tools remains to be properly discussed—mathematical operations. We have used functions like `sqrt`, `pow`, `sin`, and `cos` for years. They might look like "easy points," but once they collide with the actual representation of floating-point numbers, things are far from simple as "just call a function and get the result."

In this article, we dissect `<cmath>`, but not by simply copying a function list—cppreference does that better than anyone. We discuss three things that can actually cause you to crash and burn: what "exceptional states" a floating-point number can have (`NaN`, `inf`, subnormals), how they compare with each other; why `==` is almost a trap in the floating-point world and what to use instead; and which "look-alike but behave completely differently" functions the standard library provides (the integer trap of `abs`, `hypot` saving you from overflow, and `fma` preserving precision via single rounding). We will also touch upon C++17's special math functions and C++20's compile-time math constants `std::numbers`. We covered `lerp` and `midpoint` thoroughly in the `<numeric>` article (note that `lerp` actually lives in `<cmath>`, so we won't repeat that here). This article focuses on floating-point classification, common functions, and precision.

## Floating-Point Classification: First, Know "What Kind" of Number You Have

All mathematical functions in `<cmath>` are built upon the IEEE 754 floating-point representation. A `double` is not a "box that can hold any real number"; it is a finite collection of 64 bits, encoded as a sign bit, an exponent, and a mantissa. In this section, we first clarify the various states a floating-point number can be in, because all the pitfalls later—`NaN` not equaling itself, `==` failing, subnormals losing precision—are direct derivatives of these states.

`<cmath>` provides a set of classification functions, used in conjunction with `fpclassify`, the "main entry point." Let's start with some code that runs through all of them:

```cpp
#include <cmath>
#include <iostream>
#include <limits>

void print_classification(double x) {
    int class_type = std::fpclassify(x);
    switch (class_type) {
        case FP_INFINITE:
            std::cout << x << " is Infinite\n";
            break;
        case FP_NAN:
            std::cout << x << " is NaN\n";
            break;
        case FP_NORMAL:
            std::cout << x << " is Normal\n";
            break;
        case FP_SUBNORMAL:
            std::cout << x << " is Subnormal\n";
            break;
        case FP_ZERO:
            std::cout << x << " is Zero\n";
            break;
    }
}

int main() {
    double inf = std::numeric_limits<double>::infinity();
    double nan = std::numeric_limits<double>::quiet_NaN();
    double max = std::numeric_limits<double>::max();
    double min = std::numeric_limits<double>::min();
    double zero = 0.0;
    double subnormal = std::numeric_limits<double>::min() / 2.0;

    print_classification(inf);       // Infinite
    print_classification(nan);        // NaN
    print_classification(max);        // Normal
    print_classification(min);        // Normal (smallest normal)
    print_classification(zero);       // Zero
    print_classification(subnormal);  // Subnormal
}
```

### The Five Categories

IEEE 754 defines five mutually exclusive categories for floating-point values:

| Category | Macro Constant | Description |
| :--- | :--- | :--- |
| **Zero** | `FP_ZERO` | The value is `0.0` or `-0.0`. Yes, signed zero exists. |
| **Denormal (Subnormal)** | `FP_SUBNORMAL` | The exponent field is all zeros, but the fraction is non-zero. These numbers fill the gap between zero and the smallest normal number, offering gradual underflow but with reduced precision (loss of significant bits). |
| **Normal** | `FP_NORMAL` | The "standard" numbers. The exponent is not all zeros or all ones. They have full precision. |
| **Infinite** | `FP_INFINITE` | Result of division by zero or overflow. Can be positive or negative (`+inf`, `-inf`). |
| **NaN** | `FP_NAN` | "Not a Number". Result of invalid operations like `0.0 / 0.0` or `sqrt(-1)`. |

### Convenience Macros

Instead of memorizing the return values of `fpclassify`, `<cmath>` provides convenience macros that return `bool`:

```cpp
std::isfinite(x);    // Returns true if x is Normal, Subnormal, or Zero
std::isinf(x);       // Returns true if x is Infinite
std::isnan(x);       // Returns true if x is NaN
```

**Note:** `std::isfinite` is the most robust check for "is this a usable number?" before performing calculations.

### The "Trap" of Comparisons

Once you understand the categories, the behavior of comparison operators makes sense (but remains dangerous):

1. **NaN is unordered:** Any comparison involving `NaN` returns `false`.
    - `NaN == NaN` is **false**.
    - `NaN < 1.0` is **false**.
    - `NaN > 1.0` is **false**.
    - This is why `x == x` is a common (though not necessarily the fastest) idiom to check for NaN.

2. **Signed Zero:** `+0.0` and `-0.0` compare equal (`+0.0 == -0.0` is `true`), but they behave differently in some operations (e.g., `1.0 / +0.0` is `+inf`, `1.0 / -0.0` is `-inf`).

3. **Infinity:** Comparisons work as expected in the extended real number line (e.g., `-inf < 1000 < +inf`).

## Why `==` is a Trap and What to Use Instead

Due to rounding errors, two mathematically identical values might end up with different bit representations. For example, `0.1 + 0.2` is not exactly `0.3` in binary floating point.

### The Problem with Direct Equality

```cpp
double a = 0.1 + 0.2;
// a is likely 0.30000000000000004
if (a == 0.3) {
    // This block is rarely executed.
}
```

### Solution 1: Epsilon Comparison (Absolute Tolerance)

For numbers close to zero, we check if the absolute difference is within a small margin (epsilon).

```cpp
bool approx_equal_abs(double a, double b, double epsilon) {
    return std::abs(a - b) <= epsilon;
}
```

### Solution 2: Relative Tolerance (ULP-based)

For larger numbers, an absolute tolerance fails (e.g., comparing 1,000,000 vs 1,000,001). We need a tolerance relative to the magnitude of the numbers.

```cpp
#include <cmath>
#include <limits>

bool approx_equal_rel(double a, double b, double max_rel_diff = std::numeric_limits<double>::epsilon()) {
    double diff = std::abs(a - b);
    a = std::abs(a);
    b = std::abs(b);
    double largest = (b > a) ? b : a;

    return diff <= largest * max_rel_diff;
}
```

### Solution 3: `std::isunordered` and `std::isequal`

C++ provides specific helpers for handling the edge cases:

- `std::isunordered(x, y)`: Returns `true` if either `x` or `y` is NaN. Useful to check if a comparison is even valid.
- `std::isequal(x, y)`: (C++11) Unlike `==`, this distinguishes between signed zeros. `+0.0` and `-0.0` are **not** equal according to `isequal`.

## "Look-Alike" Functions with Different Behaviors

The standard library often provides multiple versions of similar mathematical operations. Choosing the wrong one can lead to overflow, underflow, or precision loss.

### 1. `std::abs` vs. `std::fabs` vs. Integer `abs`

- **The Trap:** In C++, `abs` is overloaded in `<cstdlib>` for integers and `<cmath>` for floating-point numbers. However, if you include `<cstdlib>` but not `<cmath>`, or if the argument type is ambiguous, you might accidentally call the integer version.
- **The Danger:** Calling `abs(integer_min)` causes undefined behavior (overflow) because the absolute value of the minimum integer cannot be represented in a signed integer type.
- **The Fix:** Use `std::abs` from `<cmath>` for floating-point types, or explicitly use `std::fabs` to ensure you are working with doubles.

```cpp
#include <cmath>
// Safe for doubles
double val = -100.0;
double safe = std::abs(val);

// Dangerous if passed INT_MIN
#include <cstdlib>
// int dangerous = std::abs(INT_MIN); // UB!
```

### 2. `std::hypot` (The Overflow Savior)

Calculating the Euclidean distance $\sqrt{x^2 + y^2}$ is risky. If $x$ or $y$ is large, $x^2$ might overflow to `inf` before the square root is applied, even if the final result fits in a `double`.

- **Naive approach:** `std::sqrt(x*x + y*y)` -> Risk of overflow/underflow.
- **Safe approach:** `std::hypot(x, y)` -> Internally handles scaling to prevent intermediate overflow/underflow.

```cpp
double x = 1e200;
double y = 1e200;
// naive: sqrt(1e400) -> Inf
// hypot: ~1.414e200
double dist = std::hypot(x, y);
```

### 3. `std::fma` (Fused Multiply-Add)

`fma(x, y, z)` computes $x \times y + z$.

- **The Benefit:** It performs the multiplication and addition as a single operation with **only one rounding** at the end.
- **Why it matters:** In `x * y + z`, the compiler rounds the result of `x * y`, then adds `z`, and rounds again. This double rounding introduces error. `fma` is more accurate and is often hardware-accelerated (single instruction on many FPUs).

```cpp
// High precision calculation
double result = std::fma(a, b, c);
```

## C++17 Special Math Functions & C++20 Constants

### C++17 Special Math Functions

C++17 standardized many special functions often used in scientific computing. These include:

- `std::beta` (Beta function)
- `std::tgamma` (Gamma function)
- `std::expint` (Exponential integral)
- `std::hermite` (Hermite polynomials)
- `std::comp_ellint_1` (Complete elliptic integrals)

These are useful when you need more than just `sin` and `cos`.

### C++20 `std::numbers`

C++20 introduced the `<numbers>` header, providing compile-time mathematical constants. No more hardcoding `3.14159...` or `2.718...` with limited precision.

```cpp
#include <numbers>

// std::numbers::pi is a double constant
double circumference = 2 * std::numbers::pi * radius;

// Also available: e, ln2, ln10, sqrt2, sqrt3, inv_sqrt3 (phi), etc.
```

## Summary

- **Classify First:** Use `std::fpclassify`, `std::isfinite`, and `std::isnan` to handle edge cases (`NaN`, `inf`) before doing math.
- **Avoid `==`:** Use epsilon-based or relative tolerance comparisons for floating-point equality.
- **Choose the Right Tool:** Use `std::hypot` to avoid overflow in distance calculations, and `std::fma` for higher precision in multiply-add operations.
- **Modern Constants:** Use `std::numbers` (C++20) for precision and readability.

Understanding these details transforms `<cmath>` from a list of "simple" functions into a robust toolkit for reliable numerical computing.

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

    // 几种来源各异的异常态
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

Here are the results obtained by running `g++ -std=c++20 -O2` (native GCC 16.1.1):

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

These categories combined represent the complete classification of IEEE 754 `double`: normal numbers (`FP_NORMAL`), zero (`FP_ZERO`, note that there are both positive and negative zeros), infinity (`FP_INFINITE`, also positive and negative), `NaN` (`FP_NAN`, Not a Number), and subnormal numbers (`FP_SUBNORMAL`), which we will discuss separately next. `fpclassify` is the general entry point, while `isnan` / `isinf` / `isfinite` / `isnormal` are convenience predicates—use the convenience functions for checking a specific category as they are more intuitive, but use `fpclassify` when you need to switch over all possible cases.

### `NaN` is not equal to itself: The classic floating-point pitfall

In the previous example, we used `std::isnan` to check for `NaN`, rather than `== nan`. This is not a stylistic preference, but a hard requirement—because **`NaN` compares as `false` against any value (including itself)**. Verification:

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

Want to see the NaN pitfall in action? Check out this online demo:

<OnlineCompilerDemo
  title="NaN is not equal to itself: A classic floating-point comparison pitfall"
  source-path="code/examples/vol3/59_cmath_nan.cpp"
  description="NaN == NaN is false, while NaN != NaN is true—IEEE 754 dictates that comparisons with NaN always return false, so use std::isnan to check for NaN"
  allow-run
/>

`NaN == NaN` evaluates to `false`, while `NaN != NaN` evaluates to `true`. This behavior is mandated by the IEEE 754 standard: `NaN` represents a "meaningless computation result" (such as `0/0`, `sqrt(-1)`, or `inf - inf`). Since "equality" is undefined for it, all comparisons are treated as unequal. This leads to a common coding trap—**to check for `NaN`, we must always use `std::isnan`, never `==`**:

```cpp
double x = compute_something();
if (x == std::numeric_limits<double>::quiet_NaN()) {   // 永远进不来！
    handle_error();
}
if (std::isnan(x)) {                                    // 这才对
    handle_error();
}
```

Even more insidiously, `NaN` "poisons" subsequent calculations—any arithmetic involving `NaN` almost always results in `NaN`. Therefore, if a `NaN` isn't caught at the source using `isnan`, it will propagate down the entire expression chain. You end up with a baffling output, and after a long investigation, you discover it originated from an upstream `sqrt(-1)`.

::: warning Always use isnan/isinf to check for NaN/inf, never ==
`== NaN` is always `false`, so writing it is a bug. Use `std::isnan` to check for `NaN`, `std::isinf` for infinity, and `std::isfinite` to check if a value is neither `NaN` nor infinity. These three predicates are provided in `<cmath>` specifically to handle the "NaN is not equal to itself" rule, so there is no reason to write comparisons manually.
:::

### Subnormal Numbers: Why `isnormal` Returns `false`

Looking at the output above, `3.14` is `FP_NORMAL`, and `0.0` is `FP_ZERO`. So, what is `FP_SUBNORMAL` (subnormal number, also known as denormal or denormalized number)? It is a range of gradual precision defined by IEEE 754 to support "positive numbers smaller than the smallest normal number"—the tradeoff is that precision decreases in this range (the number of significant bits in the mantissa is reduced).

We can directly use `std::numeric_limits<double>` to retrieve the boundary values and see:

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    double smallest_normal = std::numeric_limits<double>::min();      // 最小正常数
    double smallest_denorm = std::numeric_limits<double>::denorm_min(); // 最小次正规数
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

The minimum normal number is `2.22507e-308`. Dividing it by two yields `1.11254e-308`, which is still representable but is no longer a normal number—`isnormal` returns `false`, and `fpclassify` classifies it as `FP_SUBNORMAL`. This is a subnormal number: the value is preserved, but the precision is degraded.

Why is this worth discussing separately? Because some code assumes that "if `x != 0`, it is a number that can participate in calculations normally." However, once `x` falls into the subnormal range, precision collapses, and accumulation might even stagnate. Furthermore, some platforms or compilers enable FTZ (flush-to-zero) to treat subnormal numbers as zero for performance, causing the same code to produce inconsistent results across different machines. `std::isnormal(x)` is used to determine "whether this number is a normal number with guaranteed precision"—`0.0`, subnormal numbers, `inf`, and `NaN` all return `false`, while only `FP_NORMAL` returns `true`. When making numerically sensitive judgments (such as "is the denominator too small, should it be clamped to a threshold?"), `isnormal` is much more reliable than simply checking `x == 0`.

### `signbit`: The sign bit of negative zero and negative infinity

That final `signbit` might look redundant—can't we just use `x < 0` to check the sign? That works most of the time, but there are three values `x < 0` cannot handle: positive zero and negative zero are equal using `==` (`0.0 == -0.0` is `true`), and `NaN` compares as `false` with anything, including "negative `NaN`". `signbit` reads the sign bit directly, allowing us to distinguish these cases where "comparison semantics fail us":

```text
0.0 == -0.0?      true
signbit(0.0)?     false
signbit(-0.0)?    true
```

Actually, `1.0 / 0.0` yields `+inf`, while `1.0 / -0.0` yields `-inf`—the sign of zero manifests in division, which is why the standard library provides a dedicated `signbit` function.

## Common Functions and Their Pitfalls: `abs` / `hypot` / `fmod`

Now that we have covered the categories, let us look at the operations used most frequently in daily practice. This section will not be an exhaustive lecture; instead, we will focus on thoroughly explaining a few specific points where "functions look similar, but behave very differently."

### The Integer Trap of `std::abs`

`abs` represents the most fundamental requirement—obtaining the absolute value. However, it hides a pitfall of undefined behavior (UB) when used with integers. Let us look at a practical test:

```cpp
// Standard: C++20
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
    int imin = std::numeric_limits<int>::min();   // 通常是 -2147483648
    auto r = std::abs(imin);
    std::cout << "INT_MIN              = " << imin << '\n';
    std::cout << "std::abs(INT_MIN)    = " << r << '\n';
    std::cout << "INT_MIN == abs?      = " << (imin == r ? "yes (overflowed)" : "no") << '\n';

    // 浮点重载：abs 和 fabs 等价
    double dn = -3.14;
    std::cout << "\nstd::fabs(-3.14)     = " << std::fabs(dn) << '\n';
    std::cout << "std::abs(-3.14)      = " << std::abs(dn) << '\n';

    // long long 整数 abs 也有重载，安全
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

`std::abs(INT_MIN)` returns `-2147483648`, a negative number—taking the absolute value yielded a negative number. The reason lies in two's complement representation: a 32-bit two's complement integer can represent one more negative value than positive values (`[-2^31, 2^31-1]`). `INT_MIN` is `-2^31`, and its absolute value `2^31` cannot be represented in an `int`. Consequently, `abs` overflows, resulting in **undefined behavior**. On this local machine, GCC 16.1.1 happens to wrap around to the original value, but the standard guarantees no specific result. The optimizer might even perform surprising transformations based on the assumption that "UB won't happen".

::: warning abs(INT_MIN) is undefined behavior
The absolute value of `INT_MIN` cannot be represented by the same type `int`, so `std::abs(INT_MIN)` is UB. If your input might hit `INT_MIN` (for example, parsing integers that might return extremely small values, or using `INT_MIN` as a sentinel value), either use a wider type (cast to `long long` before calling `abs`) or explicitly check for it before calling. Floating-point types don't have this issue—`std::fabs` and the `std::abs(double)` overload are safe.
:::

By the way, here is another historical pitfall that is easy to stumble into: the C-era `abs` in `<cstdlib>` only works for `int`. Passing a `long` or `long long` will be silently truncated. C++'s `std::abs` in `<cmath>` / `<cstdlib>` provides a complete set of overloads (`int`, `long`, `long long`, `float`, `double`, `long double`). Therefore, **as long as you use `std::abs` instead of the bare `abs` and include the correct header, you won't hit the old C truncation trap.**

### `hypot`: Naive `sqrt(x*x+y*y)` can overflow

To calculate the hypotenuse of a right triangle or the magnitude of a vector, the intuitive approach is `std::sqrt(x*x + y*y)`. This works, but it hides a numerical overflow trap. When `x` and `y` themselves haven't overflowed, but `x*x` already overflows to `inf`, the result is ruined. `std::hypot` uses an internal equivalent algorithm that avoids intermediate overflow, designed specifically to rescue this situation:

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>

int main()
{
    std::cout << "hypot(3,4)           = " << std::hypot(3.0, 4.0) << '\n';
    std::cout << "sqrt(3*3+4*4)        = " << std::sqrt(3.0*3.0 + 4.0*4.0) << '\n';

    double x = 1e200, y = 1e200;   // x、y 都在 double 范围内
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

`1e200` does not overflow, but `1e200 * 1e200 = 1e400` far exceeds the upper limit of `double` (approximately `1.8e308`). The squaring step immediately results in `inf`, and taking the square root of that still yields `inf`, completely losing the correct result `1.41421e+200`. `hypot` calculates this correctly. Since C++17, `hypot` also has a three-argument overload, `std::hypot(x, y, z)`, which calculates the magnitude of a three-dimensional vector using the same principle.

The lesson: whenever we need to calculate magnitude, Euclidean distance, or $\sqrt{\sum x_i^2}$, let's not write the naive square-and-sum approach for convenience. We should use `hypot` directly (for higher dimensions, use chained `hypot` calls or more robust algorithms). Besides preventing overflow, it also protects against underflow (where extremely small numbers squared become zero), providing more stable precision.

### `fmod`: Floating-point remainder with the sign matching the dividend

`std::fmod(x, y)` is the floating-point version of the remainder operation. The result is `x - n*y`, where `n` is the quotient "truncated toward zero". It differs in sign rules from `std::remainder` (C++11, which rounds to the nearest integer), which is a common point of confusion:

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

`fmod(-5.3, 2.0)` yields `-1.3`—the sign of the result follows the **dividend** `x` (because the quotient is truncated toward zero, `-2`, so `-5.3 - (-2)*2 = -1.3`). In contrast, `remainder(-5.3, 2.0)` yields `0.7`—it takes the **nearest** integer quotient, `-3`, so `-5.3 - (-3)*2 = 0.7`. Both are correct, but the semantics differ: for periodic mapping (e.g., normalizing angles to `[-pi, pi]`), we usually want `remainder` or manual shifting after `fmod`. Mixing them up will yield results with inverted signs.

## Precision: Don't use `==` for floating-point comparison; use epsilon; `fma` guarantees single rounding

This is the most important section to remember in the entire article. While the previous discussion on `NaN` and subnormals covered "edge cases," this section covers issues that arise in **normal calculations**—floating-point `==` is almost always wrong.

### Why `==` fails: Accumulated error

Floating-point numbers are finite binary fractions. Most decimal fractions (like `0.1` and `0.2`) are infinite recurring fractions in binary and are stored as truncated approximations in `double`. Every operation introduces a tiny rounding error, and these errors accumulate so that two numbers that "should be equal" are judged unequal by `==`. The classic demonstration—adding `0.1` ten times:

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

Mathematically, `0.1 * 10 == 1`, but the result of floating-point accumulation is `0.99999999999999989`, so `== 1.0` evaluates to `false`. If we use this `acc` in `if (acc == 1.0) ...`, that branch will never be reached. This is why **we should never compare floating-point numbers for equality using `==` directly**.

The alternative is a tolerance-based comparison, commonly known as an epsilon comparison. The basic idea is that two numbers are considered equal if the absolute value of their difference falls within a very small threshold (absolute tolerance + relative tolerance):

```cpp
// Standard: C++20
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

bool nearly_equal(double a, double b, double abs_eps, double rel_eps)
{
    double diff = std::fabs(a - b);
    if (diff <= abs_eps) return true;                       // 绝对容差：处理接近 0 的情况
    return diff <= rel_eps * std::max(std::fabs(a), std::fabs(b));  // 相对容差：按数量级缩放
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

The key points of the `nearly_equal` approach: the **absolute tolerance** `abs_eps` specifically covers the case where "both numbers are close to zero" (where relative tolerance fails because the denominator is also near zero); the **relative tolerance** `rel_eps` scales with the magnitude of the numbers, handling large values (for numbers larger than `1e9`, the expected error is also larger than `1e-6`). Using both tolerances together covers the full range of magnitudes. The specific threshold values depend on the context—`1e-5` is often sufficient for graphics, while scientific computing frequently requires `1e-12`. There is no silver bullet, but replacing `==` with a tolerance-based comparison is almost always the right move.

### `fma`: One Fused Multiply-Add, Only One Rounding

The expression `a * b + c` is extremely common. The naive approach requires two operations and two rounding steps (first calculating `a*b` and rounding to `double`, then adding `c` and rounding again). `std::fma(a, b, c)` (borrowed from C in C++99 and standard since C++11) implements this expression as a **single** fused multiply-add (FMA) operation—the intermediate infinite-precision product is added directly to `c`, and finally, **rounded only once**.

The benefit of rounding only once is precision. The cost is—as shown in the benchmarks below—when the compiler has already contracted the naive expression into a hardware FMA instruction, `std::fma` might be **slower** (because it forces the use of the standard library implementation to guarantee semantics). Let's first look at the difference in precision; this difference is definitive:

```cpp
// Standard: C++20
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(17);
    // (1/3)*3 在朴素两步里被舍入成 1.0，再减 1 得 0
    // fma 单次舍入，保留了 (1/3)*3 的真实近似值
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

The results are **different**: the naive approach yields `0`, while `fma` gives `-5.55e-17`. Which one is "right"? It depends on how you define "right." Since `1.0/3.0` is truncated to `0.333...` in `double` (an approximation slightly smaller than the real `1/3`), the naive approach's `*3` rounds it back to exactly `1.0` (the error happens to be "fixed" by the second multiplication), and subtracting `1` yields `0`. `fma`, however, performs no intermediate rounding; it faithfully reflects the fact that "`0.333... * 3` is slightly less than 1," resulting in a very small negative number after subtracting 1. From the perspective of "faithfully reflecting the true mathematical result of intermediate approximations," `fma` is more honest. From the perspective of "I want `(x/x)*x` to calculate `x`," the naive approach is actually more convenient. This is exactly the subtlety of floating-point precision—**there is no absolutely correct answer, only "I know what I want."**

This example also confirms the conclusion from the previous section: even for an equation that "obviously holds" like `(1/3)*3 == 1.0`, the naive approach holds true while the `fma` approach does not—once again proving how unreliable `==` is for floating-point numbers.

What about performance? Theoretically, hardware FMA is faster than "one multiply + one add" (one instruction does two jobs). However, `std::fma` is a **library function**. Semantically, it must guarantee "single rounding," so the compiler cannot simply fold it with the surrounding context. Let's run a comparison using two loops with `noinline`:

```cpp
#include <cmath>
#include <cstdio>

// Prevent compiler optimizations like loop vectorization
// or inlining to ensure we measure the actual instruction cost.
volatile double input; // volatile to prevent read elimination
double result_naive;
double result_fma;

// Naive version: separate multiply and add
__attribute__((noinline)) void naive_calc(double x, int n) {
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        // Represents: (x * 0.1) + x
        sum = (x * 0.1) + x;
    }
    result_naive = sum;
}

// FMA version: fused multiply-add
__attribute__((noinline)) void fma_calc(double x, int n) {
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        // Represents: x * 0.1 + x
        sum = std::fma(x, 0.1, x);
    }
    result_fma = sum;
}

int main() {
    input = 1.0;
    const int N = 100000000;

    naive_calc(input, N);
    fma_calc(input, N);

    std::printf("Naive result: %f\n", result_naive);
    std::printf("FMA result:   %f\n", result_fma);

    return 0;
}
```

**Expected Outcome:**
On architectures that support FMA (like modern x86 with FMA or ARM with VFPv4), the `fma_calc` function should execute significantly faster because the compiler maps `std::fma` to a single hardware instruction (e.g., `vfmadd` or `vfmsub`). The `naive_calc` function typically requires two instructions (a multiply followed by an add), plus an intermediate rounding step.

**Note:** You must enable compiler optimizations (e.g., `-O2` or `-O3`) and ensure the target architecture supports FMA (e.g., `-march=native` on GCC/Clang) to see the performance difference. Without optimizations, function call overhead might dominate the execution time.

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

Running three consecutive times with `g++ -std=c++20 -O2` (native GCC 16.1.1) yields stable results:

```text
naive a*b+c: 4 ms
fma:         13 ms
```

`fma` is actually **more than three times slower**. The reason is that under `-O2`, the compiler automatically contracts `x[i] * y[i] + z[i]` into the hardware FMA instruction (`vfmadd` on x86). It also conveniently applies loop vectorization, completing the multiply-add operation in a single instruction while reaping the benefits of SIMD. However, explicit `std::fma` calls must strictly adhere to the library semantics of "single rounding," so the compiler cannot freely vectorize or fuse the operations. This degrades into element-by-element calls, resulting in slower performance.

Therefore, we need a clear understanding of `fma`:

::: warning fma is a precision tool, not necessarily a performance tool
The core value of `std::fma` lies in **precision** (single rounding to avoid loss of significance in intermediate results), not speed. In common scenarios where your naive `a*b+c` is already contracted into hardware FMA by the compiler, explicit `std::fma` might actually be slower. You need `fma` when: naive calculation produces visible precision loss (catastrophic cancellation, or overflow/underflow of intermediate values), and you cannot rely on the compiler's automatic contraction (e.g., `-ffp-contract=off` is enabled, or strict cross-platform reproducibility is required). Otherwise, the naive approach is both accurate enough and fast enough.
:::

The most practical way to determine if "naive is accurate enough" is to run both the expression you suspect and the `fma` version as shown above, and compare the difference in results. If the difference is within your tolerance range, stick with the naive approach; otherwise, switch to `fma`.

## C++17 Special Math Functions: `beta` / `riemann_zeta` / Elliptic Integrals

With C++17, `<cmath>` incorporated a large number of "special mathematical functions"—the beta function, the Riemann zeta function, various Laguerre/Legendre/Hermite polynomials, and various elliptic integrals. These are primarily intended for scientific computing and engineering (quantum mechanics, statistics, and electromagnetic fields), and are rarely used in typical business logic code. We will just mention their existence here and test two of them to give you an intuitive impression:

```cpp
// Standard: C++17
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(17);
    std::cout << "beta(1,1)           = " << std::beta(1.0, 1.0) << '\n';      // 数学上 = 1
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

`beta(1, 1)` evaluates to `1` (the beta function $B(1,1) = \Gamma(1)\Gamma(1)/\Gamma(2) = 1$), `riemann_zeta(2)` is $1.6449...$ (the famous Basel problem $\pi^2/6$), and `comp_ellint_2(0)` is $\pi/2 \approx 1.5708$—all match mathematical expectations. GCC 16.1.1 supports them fully.

A quick heads-up: rumors occasionally surface that this batch of functions is "no longer part of C++" (there were removal discussions during C++23, and they were officially removed in the C++26 draft). However, for the GCC 16.1.1 native toolchain, they remain fully available. If we are writing long-term scientific computing code, we recommend treating them as dependencies that "work now, but might need replacement later." We should wrap them in a layer rather than scattering them throughout the code; this way, if we need to migrate later, we only need to change one place.

## C++20 Mathematical Constants: `std::numbers`

The final section. Previously, to use $\pi$, the standard approach was `const double PI = 3.141592653589793;` or `M_PI` (from POSIX, not part of the C++ standard, and platform-dependent for portability). C++20 provides proper compile-time constants—`std::numbers`:

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
    std::cout << "std::numbers::phi   = " << std::numbers::phi << '\n';   // 黄金比例
    std::cout << "cos(pi)             = " << std::cos(std::numbers::pi) << '\n';

    // 它们是变量模板，能编译期用
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

`std::numbers::pi`, `e`, `sqrt2`, `phi` (the golden ratio), `ln2`, `log2e`, `egamma` (Euler's constant), and a host of other constants are all available, matching the full precision of `double`. Note that `cos(pi)` evaluates to exactly `-1`. This is because the `double` value of `pi` itself is slightly larger than the real mathematical constant $\pi$, and the rounding in `cos` happens to cancel out this error, yielding a clean `-1` (this is also why the previous section on `fma` emphasized that floating-point results are sometimes "unexpectedly accurate"—we shouldn't treat such coincidences as a universal rule).

`std::numbers::pi` is actually a shorthand for the variable template `std::numbers::pi_v<T>`, which is specialized for `T = float`, `double`, and `long double`. This means we can use it as a compile-time constant—in `constexpr`, `static_assert`, and as template parameters—which is something a handwritten `const double PI = 3.14` cannot achieve. GCC 16.1.1 offers full support.

Old-style approaches like `M_PI` aren't unusable (they require `#define _USE_MATH_DEFINES` before `#include <cmath>` and rely on POSIX extensions), but since C++20 provides standard facilities, new code should no longer use `M_PI`. The standard version is better in every way: portable, type-safe, and available at compile time.

## Integer Overflow and `<cmath>`: Behavior is "Well-Defined"

Finally, let's briefly wrap up the topic of "overflow" within the context of `<cmath>`. As we saw earlier with `hypot`, `<cmath>` functions have relatively well-defined semantics for overflow and domain errors, unlike integer overflow which is undefined behavior (UB):

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>

int main()
{
    std::cout << std::boolalpha;
    std::cout << "exp(1000) isinf? " << std::isinf(std::exp(1000.0)) << '\n';  // 上溢 -> +inf
    std::cout << "pow(2,1024) isinf? " << std::isinf(std::pow(2.0, 1024.0)) << '\n'; // 2^1024 刚好溢出
    std::cout << "log(0) isinf?   " << std::isinf(std::log(0.0)) << '\n';     // -> -inf
    std::cout << "log(-1) isnan?  " << std::isnan(std::log(-1.0)) << '\n';    // 定义域错 -> NaN
    std::cout << "sqrt(-1) isnan? " << std::isnan(std::sqrt(-1.0)) << '\n';   // 定义域错 -> NaN
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

- **Overflow** (result exceeds the upper limit of `double`, approximately $1.8 \times 10^{308}$) — returns `±inf`. `exp(1000)` far exceeds this limit, yielding `+inf`; `pow(2, 1024)` yields `inf` because $2^{1024}$ just crosses the maximum exponent for `double` (the maximum normal exponent is 1023).
- **Domain error** (input is meaningless, such as `sqrt` / `log` receiving a negative number) — returns `NaN`.
- **Pole** (e.g., `log(0)`) — returns `-inf`.

These are not undefined behavior, but rather defined semantics specified by IEEE 754 and the C standard library (whether `errno` is set depends on `<cerrno>` and the compiler's `-fmath-errno` setting; GCC defaults to not setting it). In contrast, integer arithmetic overflow (like `abs(INT_MIN)` or `a + b` going out of bounds) is true UB. To summarize in one sentence: "In `<cmath>`, exceptional results have defined forms (`inf` / `NaN`) and can be detected with `isinf` / `isnan`; the truly dangerous overflows are on the integer side and must be avoided using wider types or pre-checks."

## Recap

`<cmath>` may look like a "compendium of math functions," but what truly determines whether you use them correctly is your understanding of floating-point representation and precision. Let's recap the key conclusions:

- **Floating-point classification**: `fpclassify` is the main entry point, while `isnan` / `isinf` / `isfinite` / `isnormal` are convenient predicates. Floating-point numbers have five states: normal numbers, zero (including positive and negative zero), infinity, `NaN`, and subnormal numbers. Subnormal numbers have reduced precision, and `isnormal` helps you determine if "this number's precision is guaranteed."
- **`NaN` is not equal to itself**: `NaN == NaN` is `false`. Always use `std::isnan` to check for `NaN` and `std::isinf` for infinity; never use `==`.
- **`abs(INT_MIN)` is UB**: Asymmetry in two's complement means the absolute value of `INT_MIN` cannot be represented in an `int`. Floating-point doesn't have this issue; `std::fabs` / `std::abs(double)` are safe.
- **`hypot` prevents overflow**: `sqrt(x*x+y*y)` can overflow to `inf` during the intermediate squaring step, while `std::hypot` uses an overflow-resistant algorithm to provide the correct result.
- **Don't use `==` for floating-point comparison**: Accumulated errors can make numbers that "should be equal" evaluate as unequal. Use an `nearly_equal` comparison with absolute and relative epsilons, adjusting the threshold based on the scenario.
- **`fma` guarantees precision but isn't always faster**: `std::fma(a,b,c)` guarantees precision with a single rounding, but under `-O2`, the naive `a*b+c` is often automatically contracted into hardware FMA and vectorized, making it faster. Use `fma` when precision is critical and compiler contraction cannot be relied upon.
- **C++17 special functions / C++20 `std::numbers`**: Special mathematical functions (`beta` / `riemann_zeta` / elliptic integrals) target scientific computing; while removed in the C++26 draft, GCC 16.1.1 still supports them. `std::numbers::pi` and others are compile-time, full-precision constants; new code should use them instead of `M_PI`.
- **Overflow in `<cmath>` is well-defined**: Overflow yields `inf`, domain errors yield `NaN`, and poles yield `±inf`, all detectable with `isinf` / `isnan`. The truly dangerous UB overflows are on the integer side.

In the next article, we will continue exploring standard library math facilities, shifting our perspective from "standalone functions" to the "type level" — how `std::complex` makes complex arithmetic as natural as real arithmetic, and how its overloads interact with `<cmath>` functions.

## References

- [cppreference: Common math functions (`<cmath>`)](https://en.cppreference.com/w/cpp/numeric/math) — Function overview, signatures, and behavior of `fpclassify` / `isnan` / `abs` / `fma` / `hypot`.
- [cppreference: Floating-point environment](https://en.cppreference.com/w/cpp/numeric/fenv) — Floating-point environment, rounding modes, `errno`, and `-fmath-errno`.
- [cppreference: Special mathematical functions (C++17)](https://en.cppreference.com/w/cpp/numeric/special_functions) — `beta` / `riemann_zeta` / elliptic integrals, etc.
- [cppreference: Mathematical constants `std::numbers` (C++20)](https://en.cppreference.com/w/cpp/numeric/constants) — Compile-time constants like `pi` / `e` / `sqrt2`.
- [IEEE 754-2019](https://standards.ieee.org/ieee/7594/) — The standard source for floating-point representation, `NaN` semantics, and subnormal numbers.
