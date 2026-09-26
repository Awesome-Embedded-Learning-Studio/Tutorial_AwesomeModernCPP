---
chapter: 1
cpp_standard:
- 11
description: Master C's floating-point types and their precision issues, character storage
  and encoding, the const qualifier, and implicit type conversion rules, and understand
  the motivation behind C++'s type-safety design.
difficulty: beginner
order: 3
platform: host
prerequisites:
- Data Type Basics: Integers and Memory
reading_time_minutes: 12
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: Floating Point, Characters, const, and Type Conversion
translation:
  source: documents/vol1-fundamentals/c_tutorials/02B-float-char-const-cast.md
  source_hash: 4907067b5596523b6c6076c3b1e33ab35d40bb677bf2ca0558c040ad3c7068e7
  translated_at: '2026-09-25T12:42:00+00:00'
  engine: anthropic
  token_count: 4500
---
# Floating Point, Characters, const, and Type Conversion

In the previous article we took the integer family apart from the inside out—integer hierarchy, signed versus unsigned, fixed-width types, and sizeof. But the programming world doesn't run on integers alone: product prices need decimals, text on a screen needs characters, a declared variable sometimes needs protection from careless modification, and when data of different types gets mixed together in an operation, what does the compiler actually do with it? These are exactly the things we are going to chew through one piece at a time today.

To be honest, some of the material in this article—implicit type conversion in particular—looks pretty convoluted on first contact. But don't worry: these very "pitfalls" are precisely what motivated C++ to strengthen the type system. Once you understand "what goes wrong easily" in C, learning "how C++ solves these problems" later on will fall into place naturally.

## Step 1 — How Are Decimals Stored? The World of Floating-Point Precision

### The Floating-Point Trio

C provides three floating-point types, listed here in order of increasing precision:

| Type | Typical Width | Significant Digits | Literal Syntax |
|------|---------|---------|-----------|
| `float` | 32 bits (single precision) | about 7 digits | `3.14f` |
| `double` | 64 bits (double precision) | about 15 digits | `3.14` (default) |
| `long double` | 80 or 128 bits | platform-dependent | `3.14L` |

`double` is the default floating-point type—when you write `3.14`, the compiler treats it as a `double`. If you want to use `float`, remember to add the `f` suffix; for `long double`, add the `L` suffix.

```c
float f = 3.14f;            // the f suffix means float
double d = 3.14159265359;    // a plain literal is double by default
long double ld = 3.14L;      // the L suffix means long double
```

### Floating-Point Numbers Are Imprecise—and That's Not a Bug

To understand floating-point numbers, first accept one thing: **they are approximations, not exact values**. It feels counterintuitive—why shouldn't the `0.1` you store equal `0.1`? The answer hides in the binary structure floating-point numbers use internally.

Let's lay the groundwork with integers. An `int` has only 32 binary bits, so the numbers it can represent have both an upper and a lower bound—go past them and it overflows. Yet mathematics has infinitely many integers; the computer merely takes a finite slice of them. Floating-point numbers face the very same problem: there are infinitely many real numbers, many with infinitely many decimal places, and a `float` still has only 32 bits. What can it possibly use to cover such an enormous range? The same trick you use when working things out on paper—scientific notation.

Decimal scientific notation writes `0.0123` as `1.23 × 10⁻²`: a significand multiplied by a power of 10. Computers do exactly the same thing, only with base 2: `value = (±1) × significand × 2^exponent`. The 32 bits of a `float` are carved up along those three parts:

| 1 sign bit | 8 exponent bits | 23 significand bits |
|---|---|---|
| Positive or negative | How large and how small the numbers can be (range) | How many significant digits there are (precision) |

The exponent bits govern magnitude, the significand bits govern fine detail, and both are finite. The exponent lets you go as large as `10³⁸` and as small as `10⁻⁴⁵`; but with only 23 significand bits, you get roughly 7 significant decimal digits—anything more simply doesn't fit.

The real trouble lives in those 23 significand slots. Written in binary, the decimal `0.1` is `0.0001100110011...`—an infinitely repeating binary fraction. There are only 23 slots, an infinite repetition can't fit, so it must be truncated—what gets stored stopped being `0.1` long ago. The little program below prints the 32 binary bits of `0.1f` exactly as they are stored; click "Try it yourself" and it runs right away:

<OnlineCompilerDemo
  title="What 0.1 Actually Looks Like as a Float"
  source-path="code/examples/vol1/c_float_representation.c"
  description="Prints the 32 binary bits of 0.1f (sign | exponent | significand), then shows why 0.1 + 0.2 does not equal 0.3 under double."
  allow-run
  run-compiler="cg132"
  run-options="-O2 -std=c17"
/>

The key lines of the output look like this:

```text
0.1f 存进 float 的 32 个二进制位 (符号 | 指数 | 尾数):
  0 01111011 10011001100110011001101

用不同精度打印它，都不是 0.1：
  9 位精度 : 0.100000001
  20 位精度: 0.10000000149011611938

double: 0.1 + 0.2 == 0.3 ? no
  a + b = 0.30000000000000004441
  c     = 0.29999999999999998890
```

Look at the first line: the sign bit is `0` (positive), the exponent is `01111011`, and the significand is `10011001100110011001101`—that run of `1001 1001 1001...` is what the infinite repetition looks like after being cut off at 23 bits, and the trailing `1` is left over from rounding up. That's why `0.1f` prints as `0.10000000149011611938`, not `0.1`.

The `double` part is even more intuitive: `0.1 + 0.2` works out to `0.30000000000000004441`, while `0.3` as stored is `0.29999999999999998890`. Compare two numbers that were never equal in the first place with `==`, and of course they don't come out equal. The reason is no mystery: a significand of finite bits cannot hold an infinitely repeating fraction—this is simply inevitable.

::: warning
Never compare floating-point numbers with `==`. `0.1 + 0.2 != 0.3` is the norm in floating-point arithmetic, not a bug. Testing approximate equality with an epsilon is the correct solution.
:::

One related trap: under `float`, `0.1f + 0.2f` happens to equal `0.3f`. Don't conclude from this that `float` is more "accurate"—it's just that 23 significand bits are so coarse that the error gets rounded away along the way. Switch to the higher-precision `double`, and the error has nowhere to hide. Always use an approximate comparison like the following for equality tests:

```c
#include <math.h>

/// @brief Check whether two floats are approximately equal
/// @param a The first floating-point number
/// @param b The second floating-point number
/// @return 1 if approximately equal, 0 if not
int float_equal(float a, float b)
{
    return fabsf(a - b) < 1e-6f;
}
```

One more detail: when you write `float f = 0.1;`, the `0.1` is first processed as a `double` and then truncated to `float`, which introduces an extra precision discrepancy. Once you have settled on `float`, make a habit of adding the `f` suffix.

### Floating Point on Embedded Systems

Be extra careful with floating-point arithmetic on embedded systems. Many microcontrollers have no hardware floating-point unit (FPU), so floating-point operations run in software emulation and perform about an order of magnitude worse than integer operations. Even with an FPU, `double` arithmetic is usually considerably slower than `float`. So in embedded development, if a problem can be solved with integers, don't use floating point.

## Step 2 — Characters Are Just Small Integers

### The Dual Identity of char

C has no dedicated "character type". The name `char` invites misunderstanding—in reality it is simply "the smallest addressable unit of storage", exactly one byte (1 byte) in size. It's just that by convention we use it to store the ASCII code of a character—and an ASCII code is itself an integer in the range 0\~127.

```c
char ch = 'A';
printf("%c\n", ch);   // printed as a character: A
printf("%d\n", ch);   // printed as an integer: 65
```

The ASCII code of `'A'` is 65. So `'A' + 1` evaluates to 66, which corresponds to the character `'B'`. This "characters are integers" property is especially handy for case conversion:

```c
char lower = 'a';
char upper = lower - 32;    // 'a' is ASCII 97; subtract 32 and you get 65 = 'A'
char upper2 = lower - ('a' - 'A');  // a more readable way to write it
```

Let's verify:

```bash
gcc -Wall -Wextra -std=c17 char_demo.c -o char_demo && ./char_demo
```

The result:

```text
A
65
```

### The Type of a Character Literal—C and C++ Differ

Here is a subtle incompatibility between C and C++: in C, the character literal `'A'` has type `int` (4 bytes), but in C++ its type is `char` (1 byte).

```c
printf("%zu\n", sizeof('A'));  // C: prints 4, C++: prints 1
```

This difference doesn't affect the code you write in the vast majority of cases, but if you move from C to C++ later on, remember this so the sizeof result doesn't catch you off guard.

### The World of Encoding—ASCII Is Only the Starting Point

ASCII uses 7 bits (0\~127) to represent English letters, digits, and common symbols. But the world doesn't speak only English—Chinese, Japanese, and emoji can't be represented in ASCII. The C standard later extended its support to multibyte characters and wide characters:

```c
#include <wchar.h>

wchar_t wc = L'中';        // wide character; size is implementation-defined
char* mb = "你好";          // multibyte characters (UTF-8 encoded)
```

The problem with `wchar_t` is that its size is not consistent—2 bytes on Windows, 4 bytes on Linux. This is also why many modern projects simply handle all text with UTF-8-encoded `char` arrays. Encoding is a huge topic; we'll only touch on it here—knowing it exists is enough for now.

## Step 3 — Putting a Lock on Variables: const

### Basic Usage of const

`const` is a type qualifier that tells the compiler "this variable's value should not be modified". You can think of it as putting a lock on the variable—once the lock is on, any attempt to modify the variable gets stopped at compile time.

```c
const int kMaxSize = 256;        // a constant; it cannot be modified
const double kPi = 3.14159265;

// kMaxSize = 100;  // compile error! cannot modify a const variable
```

Note my wording here is "should not" rather than "cannot"—technically you can force your way past `const` through a pointer and modify the data, but that's undefined behavior and purely asking for trouble.

### The Clever Use of const in Function Parameters

The most common use of `const` is in function parameters, declaring "this function will not modify the data you pass in":

```c
/// @brief Compute the length of a string
/// @param str A string that cannot be modified
/// @return The length of the string
size_t my_strlen(const char* str);

/// @brief Write data into a buffer
/// @param buf A modifiable buffer
/// @param len The buffer length (the function does not modify len)
void fill_buffer(char* buf, const size_t len);
```

`const char* str` means "the characters str points to cannot be modified", but str itself may point somewhere else. `const size_t len` means "the value of len will not be changed inside the function". These `const`s are not just for the compiler to read—they are for the people reading the code too; the function signature itself communicates intent.

`const int* p` and `int* const p` are different things. The former means "the pointed-to value cannot be modified"; the latter means "the pointer itself cannot be modified". We will expand on this distinction in the pointers article; for now, just know it exists.

### const in Embedded Development

In embedded development, `const` has a very practical benefit—the compiler can place `const` data in Flash/ROM instead of RAM. For microcontrollers where every byte of RAM counts, this is an important optimization. Take the sine table in a lookup-table approach:

```c
const uint8_t sine_table[256] = {128, 131, 134, /* ... */};
```

With `const` on this array, the compiler can put it into Flash, leaving the precious RAM untouched.

## Step 4 — When Different Types Meet: Implicit Conversion

This section is the most confusing part of the entire article. No rush—we'll take it one step at a time.

### Integer Promotion—Small Types Get an Automatic Upgrade

In any arithmetic operation, `char` and `short` are first automatically promoted to `int`, and only then take part in the operation. This is a historically inherited design—the arithmetic units of early CPUs only supported operations at `int` width, so the compiler does this conversion for you automatically.

```c
uint8_t a = 200;
uint8_t b = 100;
uint8_t c = a + b;  // 200 + 100 = 300, truncated to 44
// but the type of a + b itself is int (300), not uint8_t
```

Here the result of `a + b` is the `int` value 300, and it gets truncated to 44 when assigned to a `uint8_t`. Integer promotion guarantees that operations on small types won't overflow in intermediate steps, but truncation can still happen when assigning back to a small type.

### Usual Arithmetic Conversions—What About Two Different Types

When two operands of different types take part in an operation, the compiler converts them to a "common type" following a set of rules. The full rule set looks fairly complicated, but we only need to remember the single most trap-prone one: **when a signed value and an unsigned value operate together, the signed one gets converted to unsigned**.

```c
int i = -1;
unsigned int u = 10;
if (i < u) {
    // you'd think -1 < 10 is true?
    // wrong! i is converted to unsigned int and becomes UINT_MAX (a huge positive number)
    // so UINT_MAX < 10 is false
    printf("这行不会打印\n");
}
```

When a signed number is compared against an unsigned one, the signed number is implicitly converted to unsigned. In C, `-1 < 10u` evaluates to false. This kind of bug is particularly insidious because the compiler may not warn you at all. It is especially common in mixed comparisons involving `size_t` (unsigned) and `int` (signed).

Our advice is simple: **avoid mixing signed and unsigned wherever possible**. If you absolutely must mix them, write the cast explicitly and make your intent clear:

```c
int count = -1;
size_t len = 5;
if (count < (int)len) {  // explicit cast; the intent is clear
    // ...
}
```

### Explicit Type Conversion

Explicit conversion in C is the C-style cast: `(type)value`. It is simple and brutal—it can convert anything, and it performs no checks whatsoever:

```c
double pi = 3.14159;
int i = (int)pi;              // truncated to 3
unsigned int u = (unsigned int)-1;  // becomes UINT_MAX
```

The problem with the C-style cast is that it is too "universal"—`const` can be cast away, pointer types can be converted at will, and assumptions about data layout get no verification at all. This is exactly why C++ introduced the named cast operators (`static_cast`, `const_cast`, `reinterpret_cast`, `dynamic_cast`), making the intent of each conversion obvious at a glance.

## Bridging to C++

C++ did a great deal of safety hardening on the type system, and many of its improvements take direct aim at C's pain points:

- `{}` initialization forbids narrowing conversions (mentioned in the previous article)
- The named cast operators make the intent of type conversions more explicit
- `constexpr` builds on `const` to guarantee compile-time evaluation
- `char16_t`, `char32_t`, and `char8_t` address the type-safety problems of encodings
- `std::numeric_limits<T>::epsilon()` provides a more precise floating-point comparison tool than a hand-written epsilon

The motivation for every one of these improvements comes from the "pitfalls" we discussed today. Once you understand "what goes wrong easily" in C, learning "how C++ solves these problems" will feel completely natural.

## Exercises

### Exercise 1: Floating-Point Precision Detective

**Difficulty: Basic** · Testing floating-point equality with epsilon

Predict the output of the following code, then compile and run it to verify your prediction:

```c
#include <stdio.h>

int main(void)
{
    double a_double = 0.1;
    double b_double = 0.2;
    double c_double = 0.3;
    float  a_float  = 0.1f;
    float  b_float  = 0.2f;
    float  c_float  = 0.3f;
    float  e_float  = 0.3f;
    float  f_float  = 0.4f;
    float  g_float  = 0.7f;

    printf("a_double + b_double == c_double? %s\n", (a_double + b_double == c_double) ? "yes" : "no");
    printf("a_float + b_float   == c_float? %s\n", (a_float + b_float == c_float) ? "yes" : "no");
    printf("e_float + f_float   == g_float? %s\n", (e_float + f_float == g_float) ? "yes" : "no");
    printf("a_double + b_double   = %.20f\n", 0.1 + 0.2);
    printf("a_float + b_float     = %.20f\n", 0.1f + 0.2f);
    printf("e_float + f_float     = %.20f\n", 0.3f + 0.4f);
    printf("c_double        = %.20f\n", c_double);
    printf("c_float         = %.20f\n", c_float);
    printf("g_float         = %.20f\n", g_float);
    return 0;
}
```

```text
a_double + b_double   == c_double? no
a_float + b_float     == c_float? yes
e_float + f_float     == g_float? no
a_double + b_double   = 0.30000000000000004441 //0.1 + 0.2
a_float + b_float     = 0.30000001192092895508 //0.1f + 0.2f
e_float + f_float     = 0.70000004768371582031 //0.3f + 0.4f
c_double              = 0.29999999999999998890 //0.3
c_float               = 0.30000001192092895508 //0.3f
g_float               = 0.69999998807907104492 //0.7f
```

Modify the code to use epsilon comparison and get the correct results.

::: details Reference answer

Replace `==` with a check that "the absolute difference is smaller than a very small threshold (epsilon)":

```c
#include <math.h>
#include <float.h>

// DBL_EPSILON for double
int double_equal(double a, double b) {
    return fabs(a - b) < DBL_EPSILON;
}

// FLT_EPSILON for float
int float_equal(float a, float b) {
    return fabsf(a - b) < FLT_EPSILON;
}
```

After the replacement, the difference between `0.1 + 0.2` and `0.3` is about `5.5e-17`, smaller than `DBL_EPSILON` (about `2.2e-16`), so `double_equal(0.1 + 0.2, 0.3)` returns true. One thing to watch: an absolute-epsilon comparison stops working when the magnitudes involved are large; in real engineering work, relative-error comparison is the more robust choice, but let's stick with the beginner version here.

:::

### Exercise 2: The Implicit Conversion Trap

**Difficulty: Basic** · The pitfall of mixing signed values with size_t

The following code hides a bug. Find it and explain why:

```c
int values[] = {1, 2, 3, 4, 5};
int target = -1;

// the bug is in the line below
if (target < sizeof(values) / sizeof(values[0])) {
    printf("target is in range\n");
}
```

Hint: what type does `sizeof` return?

::: details Reference answer

```c
int values[] = {1, 2, 3, 4, 5};
int target = -1;

// the bug is in the line below
if (target < (int)sizeof(values) / (int)sizeof(values[0])) {
    printf("target is in range\n");
}
```

Or

```c
int values[] = {1, 2, 3, 4, 5};
int target = -1;

// the bug is in the line below
if (target < (int)(sizeof(values) / sizeof(values[0]))) {
    printf("target is in range\n");
}
```

:::

### Exercise 3: const in Practice

**Difficulty: Basic** · Telling what const protects and how parameters should be passed

Read the following code and answer four questions:

```c
// sum promises not to modify what data points to
int sum(const int* data, size_t n);   // (1) what contract does const on the parameter create?

void f(void) {
    const int limit = 100;            // (2) what does const on a local variable accomplish?
    int arr[3] = {1, 2, 3};
    limit = 200;                      // (3) will this line compile?
    sum(arr, 3);                      // (4) is passing int* to a const int* parameter legal?
}
```

Now think in reverse: if `sum`'s parameter were `int*` (no const), and the caller passed a `const int carr[3]`, what would happen? Why?

::: details Reference answer

(1) const is a read-only contract: it tells callers and the compiler that `sum` will not modify the array contents through `data`.

(2) With const, `limit` becomes a read-only variable that can no longer be assigned to, and the compiler can also optimize on that basis.

(3) It won't compile. `limit` is read-only, so assigning to it fails outright.

(4) Legal. Passing `int*` to a `const int*` parameter "tightens" things (from writable to read-only), a safe implicit conversion.

In reverse: passing `const int*` to an `int*` parameter discards the const protection, and the compiler will warn or even error out; if you insist on doing it, you need an explicit cast, but that's dangerous—the function might modify data it was never supposed to touch.

:::

## References

- [cppreference: implicit conversions in C](https://en.cppreference.com/w/c/language/conversion)
- [What Every Programmer Should Know About Floating-Point Arithmetic](https://floating-point-gui.de/)
- [IEEE 754 floating-point standard](https://en.wikipedia.org/wiki/IEEE_754)
