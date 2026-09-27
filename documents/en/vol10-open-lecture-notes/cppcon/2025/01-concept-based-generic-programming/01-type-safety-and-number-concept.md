---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 talk notes — from implicit narrowing conversions to the Number<T> wrapper, and on to safe_int and checked_span
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 45
speaker: Bjarne Stroustrup
tags:
- cpp-modern
- host
- intermediate
talk_title: Concept-based Generic Programming
title: Type Safety, Number Constraints, and Bounds Checking
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md
  source_hash: 00ee0a98a3dbb78b5cbd5d79e39c721883551fa3be832da2fd448394a4b8309d
  translated_at: '2026-09-26T15:21:15+00:00'
  engine: anthropic
  token_count: 12900
---
# Let's Begin: From Manual Checks to Implicit Guards

:::tip
One quick PS: this section is our own elaboration branching off from the CppCon talks — the links above point to the video series they published on YouTube. Readers in mainland China can use the Bilibili link instead.
:::

Generic programming in C++ dates back to 1991, when templates were introduced into the language (C++ Release 3.0). Stroustrup's primary motivation for designing templates was to replace C preprocessor macros with type-safe generic containers. In *The Design and Evolution of C++*, he wrote that macros "fail to obey scope and type rules and don't interact well with tools," while templates were designed to be "as efficient as macros" but type safe<RefLink :id="1" preview="Stroustrup, The Design and Evolution of C++, 1994, Ch.15" />.

But the story took an unexpected turn in 1994. Erwin Unruh presented a piece of legal C++ code at a C++ committee meeting that could not even compile, yet the compiler emitted a sequence of prime numbers, line by line, in its error messages<RefLink :id="2" preview="Unruh, Prime Number Computation, C++ committee meeting, 1994" />. Only then did the whole committee realize that templates had inadvertently become a Turing-complete compile-time computation system. The following year, Todd Veldhuizen published a paper systematically describing this technique and named it **Template Metaprogramming**<RefLink :id="3" preview="Veldhuizen, Using C++ Template Metaprograms, C++ Report, 1995" />. Templates thereby evolved from a "type-safe macro replacement" into an unavoidable compile-time abstraction mechanism in C++.

Template error messages easily run to hundreds of nearly unreadable lines — that alone is why many C++ developers shy away from generic programming. But as a project grows, code that avoids generics becomes so duplicated it is hard to maintain. In this article we start from the foundational motivations of generic programming and work our way to a concrete, landable type-safety problem: implicit narrowing conversions.

The environment for this article is Arch Linux on WSL with GCC 16.1.1. Here is the environment info:

```bash
❯ gcc -v
Using built-in specs.
COLLECT_GCC=gcc
COLLECT_LTO_WRAPPER=/usr/lib/gcc/x86_64-pc-linux-gnu/16.1.1/lto-wrapper
Target: x86_64-pc-linux-gnu
Configured with: /build/gcc/src/gcc/configure --enable-languages=ada,c,c++,d,fortran,go,lto,m2,objc,obj-c++,rust,cobol --enable-bootstrap --prefix=/usr --libdir=/usr/lib --libexecdir=/usr/lib --mandir=/usr/share/man --infodir=/usr/share/info --with-bugurl=https://gitlab.archlinux.org/archlinux/packaging/packages/gcc/-/issues --with-build-config=bootstrap-lto --with-linker-hash-style=gnu --with-system-zlib --enable-cet=auto --enable-checking=release --enable-clocale=gnu --enable-default-pie --enable-default-ssp --enable-gnu-indirect-function --enable-gnu-unique-object --enable-libstdcxx-backtrace --enable-link-serialization=1 --enable-linker-build-id --enable-lto --enable-multilib --enable-plugin --enable-shared --enable-threads=posix --disable-libssp --disable-libstdcxx-pch --disable-werror --disable-fixincludes
Thread model: posix
Supported LTO compression algorithms: zlib zstd
gcc version 16.1.1 20260430 (GCC)

❯ uname -a
Linux Charliechen 6.6.114.1-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Mon Dec  1 20:46:23 UTC 2025 x86_64 GNU/Linux
```


## First, Understand What Generic Programming Is Actually For

Saying generic programming makes code more generic and more abstract is only half the story. Alex Stepanov (the father of the STL) pointed out that the goal of generic programming is to express ideas "in the most general, most efficient, and most flexible way" — the key is expressing ideas, not abstraction for abstraction's sake. Mistaking the means for the end is a common trap in programming — another classic example is the abuse of design patterns.

The distinction matters. We don't design code starting from some abstract model; we start from concrete, efficient algorithms, discover what they have in common, and then extract that commonality. And the performance must not be lost, because a large part of why C++ exists lies exactly there. While hardware keeps getting stronger, our expectations of software keep ballooning, and semiconductor processes seem to have hit a bottleneck — the room for casually written code keeps shrinking.

Generic programming demands more of us: it requires insight into the reusable patterns within the abstract domain. And its floor is this — once the abstraction is done, performance must not be worse than the hand-written concrete version. Otherwise there is no point introducing generic programming at all. Writing code is, in the hierarchy of needs, the layer where work gets done; don't do extra things. If a piece of code will not be reused and is performance-sensitive, don't make it generic.

## A Few C++ Design Criteria from Alex Stepanov

Around 1994, Stepanov proposed three design criteria<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" />. First, generality: a good generic component should be able to express uses its own designers never thought of. Second, uncompromising efficiency: to write system-level code in C++, efficiency must match C; to write linear algebra, it must match Fortran. Third, statically typed interfaces: checked at compile time, not leaving errors for runtime. Later he added two very down-to-earth requirements: compile times shouldn't be long enough to go get coffee (header-only libraries admit this is hard to guarantee), and the learning curve must not be so steep that you need an MIT PhD to get started<RefLink :id="5" preview="Nygaard, cited in Stroustrup, Concept-Based Generic Programming in C++, 2025, §1" /> — as for whether C++ has lived up to that, we all know the answer in our hearts.

## Implicit Narrowing Conversions: A Classic Type-Safety Trap

With the motivation covered, let's start from a concrete problem. Introducing a concept must correspond to a real problem scenario; otherwise it is a castle in the air. Take this code:

```cpp
#include <iostream>

int main() {
    int big = 30000;
    short small = big;          // Does 30000 exceed the range of short? Not really — short is typically -32768~32767
                                // But what if it were 40000?

    short overflow = 40000;     // Compiles! But the value is already wrong

    double pi = 3.14159;
    int int_pi = pi;            // The fractional part is simply gone

    std::cout << "overflow = " << overflow << "\n";  // prints a strange negative number
    std::cout << "int_pi = " << int_pi << "\n";      // prints 3

    return 0;
}
```

This code uses pre-C++23 style, so every compiler can build it directly.

On my machine, the result is `overflow = -25536` and `int_pi = 3`. The compiler doesn't emit a single warning (unless you turn on `-Wall -Wextra`, which many projects don't). This kind of bug is particularly insidious: the code runs, the results are simply wrong, and it often stays hidden when the data volume is small — only blowing up once you're live.

Many people think "that's just C++, be careful and you'll be fine." But human carefulness is not a reliable defense for this kind of thing. Bjarne Stroustrup himself has said he wanted to fix this back in the day and didn't manage to, and the C camp wouldn't let it be touched either. So as users, can we defend ourselves?

## Modeling "Number" with C++20 Concepts

C++20 gave us a new weapon: concepts. The essence is simple — a concept is a boolean predicate evaluated at compile time: types go in, `true` or `false` comes out. Put another way: it lets the compiler understand a "concept" without us having to describe it in convoluted natural language.

The standard library already defines some basic concepts, such as `std::integral` and `std::floating_point`, which test whether a type is an integral type or a floating-point type. These are not new inventions — the first edition of K&R's C already distinguished `int` from `float`; the difference is that now we have a language-level, compile-time-queryable representation.

Let's write the simplest possible concept that captures the notion of "number":

```cpp
#include <concepts>
#include <type_traits>

// My own "number" concept: either an integral type or a floating-point type
template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

// Verify it
static_assert(number<int>, "int 应该是 number");
static_assert(number<double>, "double 应该是 number");
static_assert(number<char>, "char 也是整数类型，所以是 number");
static_assert(!number<std::string>, "string 不是 number");
```

There is a syntax detail worth spelling out: `std::integral<T>` looks like a function call, but it isn't. `std::integral` is a concept, `<T>` instantiates it with the type T, and the value of the whole expression is a compile-time bool. You cannot write `std::integral(T)` — that syntax is wrong. Just think of it as "run the integral test on T," returning true or false.

Run the code above and all four `static_assert`s pass, which means our `number` concept basically works.

## Writing Our Own Narrowing Test

Can we write a concept that decides whether assigning a value of type U to a variable of type T causes a narrowing conversion? Well — we've set out to write this article, so let's find out.

First, if T's representable range is smaller than U's, narrowing is obviously possible. For example, assigning an `int` to a `short`: `int` can represent far more values than `short`. But how do we decide "smaller range"? The C++ standard library doesn't directly hand us a "value range of a type" concept, but `<type_traits>` gives us `std::numeric_limits`, which reports the min and max of each type. And if U is a floating-point type while T is an integer type, the fractional part is definitely lost — that is narrowing too.

There is another easily overlooked case: U and T are both integers of the same size (say, both 32 bits) but with different signedness — assigning a negative number to an unsigned type will also go wrong. Writing these rules down as code:

```cpp
#include <concepts>
#include <type_traits>
#include <limits>

template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

// Whether T is "smaller than" U (can represent fewer values)
// Here we compare ranges via numeric_limits
template<typename T, typename U>
concept smaller_range =
    number<T> && number<U> &&
    (std::numeric_limits<T>::max() < std::numeric_limits<U>::max() ||
     std::numeric_limits<T>::min() > std::numeric_limits<U>::min());

// The core decision: does assigning from U to T cause narrowing?
template<typename T, typename U>
concept narrowing_assign =
    number<T> && number<U> &&
    (
        // Case 1: T's range is smaller than U's; the value might not fit
        smaller_range<T, U> ||
        // Case 2: U is floating point, T is integral; the fractional part is lost
        (std::floating_point<U> && std::integral<T>) ||
        // Case 3: U and T are the same size but differ in signedness
        (std::integral<T> && std::integral<U> &&
         std::signed_integral<U> != std::signed_integral<T>)
    );

// Test cases
static_assert(narrowing_assign<short, int>, "int -> short 应该是窄化");
static_assert(narrowing_assign<int, double>, "double -> int 应该是窄化（丢小数）");
static_assert(narrowing_assign<unsigned int, int>, "int -> unsigned int 可能窄化（负数问题）");
static_assert(!narrowing_assign<int, short>, "short -> int 不是窄化");
static_assert(!narrowing_assign<double, float>, "float -> double 不是窄化");
static_assert(!narrowing_assign<int, int>, "int -> int 不是窄化");
```

Compile and run, and all six `static_assert`s pass. We can use the last one, `!narrowing_assign<int, int>`, to sanity-check the logic: for same-type assignment, in case 1's `smaller_range<int, int>`, `max() < max()` is false and `min() > min()` is also false, so it doesn't fire; case 2 requires U to be floating point and T an integer — not satisfied; case 3 requires different signedness, and `int` and `int` are obviously the same. All three branches are false, the whole expression is false, and the negated `static_assert` passes — exactly matching our intuition that same-type assignment doesn't narrow.

One more point worth making: wherever `narrowing_assign` mixes `&&` and `||`, the parentheses are mandatory. Because `&&` binds tighter than `||`, without the parentheses `number<T> && number<U>` would only constrain the first `||` branch, and the other two branches could still be evaluated on non-number types — for the current test cases the result would happen to be correct, but semantically it would be wrong. Parenthesizing makes the three branches a single unit, jointly constrained by `number<T> && number<U>`, and only then is the logic airtight.

## More Edge Cases to Think Through

The implementation above covers most scenarios, but a few details deserve discussion. Take conversions between floating-point types: is `double` to `float` narrowing? From a precision standpoint, certainly — `double` can represent more significant digits than `float`. And in the current implementation, `smaller_range<float, double>` checks `numeric_limits<float>::max() < numeric_limits<double>::max()`, which is true, so it is correctly identified as narrowing.

Another example: `char` to `unsigned char`. The signedness of `char` is implementation-defined (signed on some platforms, unsigned on others). On a platform where `char` is signed, `signed_integral<char> != signed_integral<unsigned char>` is true, and it will be identified as narrowing. That is actually reasonable, because if the `char` is -1, assigning it to `unsigned char` turns it into 255.

Note, however, that this implementation is not yet 100% rigorous. The standard's definition of narrowing conversions (in the C++11 list-initialization rules) is finer-grained than what we've written here — for instance, it also considers whether a floating-to-integer value falls within the integer's range. But as a starting point, this concept already blocks most of the pitfalls for us. It can be refined over time.

At this point we can summarize: a concept is not some inscrutable metaprogramming trick; it is simply a mechanism for "writing constraints on types as compile-time-checkable boolean expressions." Before, when we wrote templates, constraints lived entirely in documentation and naming conventions ("please pass a random-access iterator"), the compiler ignored them, and passing the wrong thing produced a wall of gibberish. Now, with concepts, the compiler tells you immediately "the type you passed doesn't satisfy the requirements" — in an error message a human can actually read.

The next step is to put this `narrowing_assign` concept to work in real functions, as a safe assignment wrapper — that is the next section's topic. At the very least, the core idea of "expressing type constraints with concepts" is now straightened out.

---

# From Manual Checks to Implicit Guards: Baking Narrowing-Conversion Checks into the Type

In the previous section we worked out the rules for detecting narrowing conversions. Running those rules through your head every time you write code is nearly impossible — with mixed signed and unsigned types, which one is bigger, will it overflow, can the positive part be represented: just thinking about it is already dizzying. The speaker said that written out by hand it runs to about a page, and it's messy, tricky stuff.

So this section's job is: turn that page of messy logic into code that actually runs, and then hide it, so that in day-to-day coding you don't feel its existence at all.

## First, Translate the Decision Logic into Code

One intuition: to decide whether a value of type U assigned to type T causes narrowing, just do a `static_cast` and compare. But think it through and that's simply not it — with mixed signed and unsigned, the comparison itself is a trap. So we need an honest, step-by-step decision function.

The plan: do as much elimination as possible at compile time, filtering out the cases where "narrowing is absolutely impossible," leaving only the paths that genuinely need a runtime check. This is exactly what generic programming has always emphasized — work that shouldn't happen at runtime shouldn't happen at runtime.

```cpp
#include <type_traits>
#include <limits>
#include <stdexcept>

// The core decision: does assigning the value u to a variable of type T cause narrowing?
template<typename T, typename U>
constexpr bool would_narrow(U u) noexcept {
    // Layer one: cases we can rule out at compile time
    // If T can represent every value of U, then whatever u is, narrowing is impossible
    if constexpr (std::is_same_v<T, U>) {
        return false;  // Same type, obviously not
    } else if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        // Floating point to integer: almost always potentially narrowing
        // unless the floating value happens to be an integral value in range
        // We leave that for the runtime check
    } else if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<U>) {
        // Floating point to floating point: narrowing only if T's precision/range is smaller than U's
        if constexpr (std::numeric_limits<T>::digits >= std::numeric_limits<U>::digits &&
                      std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                      std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
            return false;  // T's range and precision both suffice; ruled out at compile time
        }
    } else if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
        // Integer to integer is the most intricate case; details below
        if constexpr (std::is_signed_v<T> == std::is_signed_v<U>) {
            // Same signedness is easy: compare ranges
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                          std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
                return false;
            }
        } else if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
            // signed T receiving unsigned U
            // It's enough for T's positive range to cover all of U's values
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max()) {
                return false;
            }
        } else {
            // unsigned T receiving signed U
            // If U is negative it's certainly narrowing, but u's value is unknown at compile time
            // so this case can't be ruled out at compile time; leave it for runtime
        }
    }

    // Layer two: what compile time can't rule out gets checked at runtime

    // signed -> unsigned with a negative source value: definitely narrowing
    // Note: a round-trip check won't work (int(-1) → unsigned → int(-1) is reversible under two's complement)
    if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        if (u < 0) return true;
    }

    // Do the static conversion and see whether the value changed
    T t = static_cast<T>(u);
    if (static_cast<U>(t) != u) {
        return true;  // Converted over and back, the value differs — information was lost
    }

    // Extra check for floating point to integer: even if the round trip agrees,
    // confirm it isn't the "3.0 becomes 3 and back to 3.0" coincidence — though in
    // that case no value was really lost, so the check above already suffices.
    // But the standard is stricter about floating-to-integer conversions:
    // the original value must be an integral value (no fractional part)
    if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        // If u is not an integral value, then even if static_cast happens to truncate it,
        // it is strictly speaking narrowing (the fractional part was lost)
        if (u != static_cast<U>(static_cast<long long>(u))) {
            return true;
        }
    }

    return false;
}
```

Looking back at this function, with mixed signed and unsigned the boundary between what compile time can rule out and what runtime must check genuinely takes careful thought. One easy trap: detecting narrowing purely with a round trip (convert over and back) fails for signed→unsigned conversions — because `int(-1) → unsigned(4294967295) → int(-1)` is perfectly reversible under two's complement, and the round trip detects nothing. That's why we must explicitly check "is the source value negative" before the round trip. `if constexpr` plays the key role here — branches the compiler can settle at compile time generate no code at all; there won't be a pile of useless comparison instructions.

## When Narrowing Happens: Throw an Exception

The decision logic exists; the next choice is what to do when narrowing is detected.

The speaker's approach is blunt — throw an exception. After the compile-time filtering, the probability that a narrowing conversion actually fires at runtime is extremely low. In most code the types match and are eliminated at compile time; of the remainder that needs runtime checks, the overwhelming majority won't actually overflow. Maybe one call in a million triggers it, and that is exactly the scenario exceptions are best at — handling extremely rare exceptional situations.

```cpp
template<typename T, typename U>
constexpr T narrow_convert(U u) {
    if (would_narrow<T>(u)) {
        throw std::invalid_argument("narrowing conversion detected");
    }
    return static_cast<T>(u);
}
```

That simple. You can use it directly:

```cpp
#include <iostream>

int main() {
    // Normal cases, no exception
    int a = narrow_convert<int>(42.0);        // OK, 42.0 is an integral value
    unsigned int b = narrow_convert<unsigned int>(100);  // OK

    // These throw
    try {
        char c = narrow_convert<char>(300);   // 300 exceeds the range of char
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        unsigned int d = narrow_convert<unsigned int>(-1);  // negative to unsigned
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        int e = narrow_convert<int>(3.14);  // floating point to integer with a fractional part
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    std::cout << "a = " << a << ", b = " << b << "\n";
}
```

Run it and the output is:

```text
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
a = 42, b = 100
```

Great — everything that should be blocked is blocked. But here's the problem: you can't write `narrow_convert<int>(xxx)` at every assignment. The code would get verbose, and there'd be no way to stay consistent. Relying on programmer discipline to add checks guarantees someone slips through. Some places get the check, some places forget, and the bugs hide exactly in the forgotten places.

## Baking the Check into the Type: `Number<T>`

So the real solution is to make the check implicit. Define a wrapper type `Number<T>` that performs the narrowing check automatically at construction. After that, `Number<T>` is used just like an ordinary `T`, but without worrying about narrowing — because if the construction doesn't pass, the object simply never exists.

```cpp
template<typename T>
class Number {
    T value_;

public:
    // The constructor: this is where all the magic happens
    template<typename U>
    constexpr Number(U u) : value_(narrow_convert<T>(u)) {}

    // Same-type construction needs no check, but it goes through the same
    // interface for uniformity (optimized away at compile time)
    constexpr Number(T t) : value_(t) {}

    // Implicit conversion back to T, so Number<T> can be used like T
    constexpr operator T() const noexcept { return value_; }

    // Value access
    constexpr T get() const noexcept { return value_; }
};
```

See — that's all the class is. It looks like demo code, but it genuinely works. Let's try:

```cpp
int main() {
    // These all work fine
    Number<int> x = 42;              // int -> int, no problem
    Number<int> y = 3.0;             // double -> int, 3.0 is an integral value, no problem
    Number<unsigned int> z = 100u;   // unsigned int -> unsigned int, no problem

    // Number<T> can be used as T, thanks to operator T()
    int sum = x + static_cast<int>(z);  // ordinary arithmetic
    std::cout << "x = " << x << ", y = " << y << ", z = " << z << "\n";
    std::cout << "sum = " << sum << "\n";

    // These throw at construction
    try {
        Number<char> c = 300;  // no compile error; throws at runtime
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        Number<unsigned int> bad = -1;  // negative to unsigned; throws at runtime
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }
}
```

Output:

```text
x = 42, y = 3, z = 100
sum = 142
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
```

At this point a key design insight emerges: it used to feel like template metaprogramming and the type system were two separate things, but in fact the type system itself is the best place to do checks. You don't need to remember where to check and where not to — just use `Number<T>` in place of `T`, and the check happens automatically. And thanks to the compile-time `if constexpr` branches, the paths that need no check (like same-type assignment) don't even generate decision code — zero overhead.

## But Construction Alone Isn't Enough: It Needs Arithmetic

What good is a number type that can only be constructed? It would be indistinguishable from a constant. So we need arithmetic operators on `Number<T>`. But here's a question: what should `Number<int>` plus `Number<double>` return? You can't just return an arbitrary type; there has to be a rule.

The standard library has something called `std::common_type` that does exactly this — given two types, it tells you which type their arithmetic should use. For example, `common_type_t<int, double>` is `double`, and `common_type_t<int, unsigned int>` is `unsigned int` on most platforms. Let's use it directly:

```cpp
#include <type_traits>

template<typename T>
class Number {
    T value_;

public:
    template<typename U>
    constexpr Number(U u) : value_(narrow_convert<T>(u)) {}
    constexpr Number(T t) : value_(t) {}
    constexpr operator T() const noexcept { return value_; }
    constexpr T get() const noexcept { return value_; }

    // Addition: Number<T> + Number<U> -> Number<common_type_t<T, U>>
    template<typename U>
    constexpr auto operator+(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        // value_ + other.value_ first does ordinary arithmetic (with implicit promotion),
        // then constructs Number<ResultType> from the result; that construction
        // performs the narrowing check automatically
        return Number<ResultType>(value_ + other.get());
    }

    // Subtraction, same idea
    template<typename U>
    constexpr auto operator-(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        return Number<ResultType>(value_ - other.get());
    }

    // Multiplication
    template<typename U>
    constexpr auto operator*(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        return Number<ResultType>(value_ * other.get());
    }
};
```

Let's run a slightly more involved example to verify:

```cpp
int main() {
    Number<int> a = 10;
    Number<double> b = 3.5;

    // int + double -> common_type is double
    auto result = a + b;
    std::cout << "10 + 3.5 = " << result << "\n";
    std::cout << "结果类型是 Number<double>? "
              << std::is_same_v<decltype(result), Number<double>> << "\n";

    // Mixed unsigned + int arithmetic
    Number<unsigned int> big = 3000000000u;  // 3 billion; unsigned int can represent it
    Number<int> small = 100;

    auto result2 = big + small;
    std::cout << "3000000000u + 100 = " << result2 << "\n";

    // Try the overflow scenario: adding two big numbers
    Number<unsigned int> x = 3000000000u;
    Number<unsigned int> y = 2000000000u;
    try {
        // 3000000000 + 2000000000 = 5000000000, beyond the range of unsigned int
        auto overflow = x + y;
        std::cout << "不应该到这里\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "加法溢出捕获到: " << e.what() << "\n";
    }
}
```

Output:

```text
10 + 3.5 = 13.5
结果类型是 Number<double>? 1
3000000000u + 100 = 3000000100
加法溢出捕获到: narrowing conversion detected
```

:::warning Erratum: unsigned arithmetic overflow is not caught by narrow_convert
In the output above, the last line ("加法溢出捕获到") **never appears** when you actually compile and run this. The measured result (GCC 16.1.1, C++20):

```text
Raw unsigned sum: 705032704
Would narrow? 0
No exception thrown! overflow = 705032704
```

The reason: `unsigned int + unsigned int` arithmetic in C++ **wraps around** (well-defined wrapping). The result of `3000000000u + 2000000000u` is `705032704` — a perfectly legal `unsigned int` value. Then `narrow_convert<unsigned int>(705032704u)` sees a same-type assignment, `would_narrow` returns false outright, and no exception is ever thrown.

This is a fundamental limitation of the current `Number<T>` design: `narrow_convert` can only detect **narrowing conversions at assignment**; it cannot detect **overflow of the arithmetic itself**. To detect overflow you need compiler builtins (such as `__builtin_add_overflow`) or manual checks:

```cpp
template<typename T>
constexpr T safe_add(T a, T b) {
    if constexpr (std::is_unsigned_v<T>) {
        if (a > std::numeric_limits<T>::max() - b) {
            throw std::overflow_error("unsigned addition overflow");
        }
    } else {
        // signed overflow is UB; must use __builtin_add_overflow or an equivalent
        T result;
        if (__builtin_add_overflow(a, b, &result)) {
            throw std::overflow_error("signed addition overflow");
        }
        return result;
    }
    return a + b;
}
```

Verification code: [01-06-overflow-not-caught.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/01-concept-based-generic-programming/01-06-overflow-not-caught.cpp).
:::

Look at the last overflow-catching example — we need to note that `narrow_convert` only intercepts narrowing **during type conversion**; it is powerless against overflow of same-type arithmetic itself (such as the wrapping of `unsigned int + unsigned int`). `common_type_t<unsigned int, unsigned int>` is just `unsigned int` itself, and the arithmetic result has already wrapped into a legal value before it is ever assigned to `Number<unsigned int>`. Fully defending against arithmetic overflow needs an additional mechanism (such as the compiler's built-in overflow-checking functions), which is beyond `narrow_convert`'s job description.

At this point, from manual decision rules, to a runtime checking function, to the exception-handling strategy, to the wrapper type and arithmetic — the thread finally comes together. The key is to understand all of this as one complete narrowing-defense system, not as isolated factoids.

---

# No Need to Reinvent the Wheel: Standard Library Function Objects + Eliminating Comparison Traps

To implement a suite of safe integer types, the intuition is that you must hand-write addition, subtraction, multiplication, division, and comparisons yourself — headache-inducing just to think about. But in fact the standard library has long provided function objects like `std::plus` and `std::multiplies`; each is only a few lines of code, not black magic at all. Then again, reinventing wheels is a time-honored C++ tradition.

## First, How the Operators Are Written

A common misconception: to overload `operator+` and `operator*` for your own types, you must write a pile of `friend` functions inside the class or at global scope, each handling every edge case. In reality you just reach for the standard library's function objects.

```cpp
#include <functional>

// A simplified safe_int I defined, showing only the core idea
template <typename T>
struct safe_int {
    T value;

    // Addition: just use the standard library's std::plus, one line and done
    friend safe_int operator+(const safe_int& a, const safe_int& b) {
        return safe_int{std::plus<T>{}(a.value, b.value)};
    }

    // Multiplication, same idea
    friend safe_int operator*(const safe_int& a, const safe_int& b) {
        return safe_int{std::multiplies<T>{}(a.value, b.value)};
    }
};
```

The key here: `std::plus<T>{}` is a function object. When you invoke it, if some type conversion that shouldn't happen occurs (say, signed and unsigned getting mixed), it gets stopped by the rules we set up earlier. The arithmetic logic itself needs no attention — the standard library already wrote it. We only handle "intercept" and "pass through."

## Comparisons: The Hardest-Hit Zone of Signed/Unsigned Mixing

Operator overloading itself isn't hard; comparisons are the hardest-hit zone of signed/unsigned mixing. Spending a whole afternoon debugging, only to find the bug is one wrong comparison line — it happens more often than you'd think.

Take this code:

```cpp
#include <iostream>

int main() {
    int a = -1;
    unsigned int b = 2;

    std::cout << (a < b) << "\n";  // What do you guess this prints?
}
```

Run it and the output is `0`, i.e. `false`. A negative number less than a positive number — and the result is false? Why? The answer is one of C++'s implicit conversion rules: when signed and unsigned meet in a comparison, the signed operand is converted to unsigned. So `-1` becomes an enormous number (`4294967295`), which of course is not less than 2. This rule has been there since C was born in 1972; it may have seemed harmless at the time, but over the decades nobody knows how many bugs it has buried.

As the talk put it well: this rule should have been fixed in 1972, but by the time everyone realized how bad it was, too much code in the world already depended on the behavior — it couldn't be moved. To this day we are still paying for it.

## Fixing the Comparison Trap with Our Own Hands

Since the built-in types can't be trusted, we take over comparison in our own `safe_int`. The plan is direct: if the two sides differ in type (one signed, one unsigned), do a special check first; if the types agree, just do the ordinary comparison.

```cpp
template <typename T>
struct safe_int {
    T value;
};

// Cross-type operator<: a templated free function that can compare safe_int<T> against safe_int<U>
template <typename T, typename U>
bool operator<(const safe_int<T>& a, const safe_int<U>& b) {
    if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
        // a is signed, b is unsigned
        // If a is negative, it is definitely less than any unsigned value; return true
        if (a.value < 0) {
            return true;
        }
        // Otherwise convert both sides to unsigned and compare;
        // a.value is guaranteed non-negative here, so the conversion is safe
        return static_cast<std::make_unsigned_t<T>>(a.value) < b.value;
    } else if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        // The other way around: a is unsigned, b is signed
        if (b.value < 0) {
            return false;
        }
        return a.value < static_cast<std::make_unsigned_t<U>>(b.value);
    } else {
        // Same signedness: ordinary comparison, no conversion trouble at all
        return a.value < b.value;
    }
}
```

One crucial point: `operator<` is written as a **templated free function**, not an in-class `friend`. The reason is that an in-class `friend bool operator<(const safe_int& a, const safe_int& b)` only accepts two `safe_int<T>` with the **same T**. But `safe_int<int> < safe_int<unsigned int>` is a comparison between two different template instantiations, which the in-class friend simply cannot match. Written as a free `template<typename T, typename U>` function, the compiler can correctly match this operator between `safe_int<int>` and `safe_int<unsigned int>`. `if constexpr` lets the compiler optimize away the branches not taken — zero overhead. Equality and greater-than comparisons follow the same pattern; just write them the same way.

Let's verify:

```cpp
int main() {
    safe_int<int> a{-1};
    safe_int<unsigned int> b{2};

    std::cout << (a < b) << "\n";  // prints 1 — finally correct!
    // Note: a and b are different template instantiations, safe_int<int> and
    // safe_int<unsigned int>; only a templated free operator< can match this call
}
```


## A Bigger Pit: Bounds Checks Silently Bypassed

Comparisons are fixed, but there is an even sneakier scenario. The talk used a `span` example — this pattern is extremely common in real code.

First, some background. `std::span` is essentially a "fat pointer" — a pointer to a sequence of elements plus the length of that sequence. The idea isn't new: Dennis Ritchie proposed adding boundary-carrying pointers to C (for variable-length arrays) back in the early 1990s, calling them fat pointers, but the committee rejected the idea as too costly at runtime<RefLink :id="7" preview="Ritchie, Variable-Size Arrays in C, 1990" />. Now that C++20 has finally admitted `span`, it counts as a vindication decades late — while `span` itself does no bounds checking, it provides the foundation for safer wrappers built on top.

So where's the problem? Look at this code:

```cpp
#include <span>
#include <vector>

void process(std::span<int> data) {
    // I want the first max_size - 500 elements of data
    unsigned int max_size = 50;  // meant to write 500, typo'd it into 50
    auto sub = data.subspan(0, max_size - 500);
    // What is sub now?
}
```

`max_size` is an `unsigned int` with value 50. What does `50 - 500` do under unsigned arithmetic? It underflows into an enormous number (roughly `4294967296 - 450`). Then `subspan` receives this enormous length — and `std::span::subspan` in C++20 has **no** bounds checking; it has only a precondition (violating it is undefined behavior) and throws no exception<RefLink :id="6" preview="cppreference, std::span::subspan, C++20" />. That means the enormous number is passed straight in, and the consequence is undefined behavior — it might read memory it shouldn't, it might happen not to crash, but you absolutely cannot count on `span` to stop it for you.

Because of one tiny typo, because of the built-in conversion rules, you have completely lost the protection of bounds checking. Many people think `span` is safe enough already, never imagining it gets bypassed at the layer of argument computation.

## Giving `span` Real Protection with `safe_int`

Now that we have a `safe_int` that intercepts every wrong conversion, can we also protect `span`'s size arguments? Of course.

My plan: first define a concept meaning "types that can be spanned," and then, within that concept, require the size type to be a safe integer.

```cpp
#include <concepts>
#include <span>
#include <vector>

// First our own safe_int (simplified; assume the full safe arithmetic is implemented)
template <typename T>
struct safe_int {
    T value;
    // ... all the operator overloads written earlier live here
};

// Define a concept: span-able types
// The standard library has std::contiguous_range; I extend it
template <typename T>
concept spanable = std::contiguous_range<T>;

// Now define a safe span whose size type is safe_int
template <typename T>
struct safe_span {
    T* data_;
    safe_int<std::size_t> size_;  // key: the size is a safe integer

    // Constructor from an ordinary container
    template <spanable Container>
    explicit safe_span(Container& c)
        : data_(c.data())
        , size_(safe_int<std::size_t>{c.size()})
    {}

    // A safe subspan
    safe_span subspan(std::size_t offset, safe_int<std::size_t> count) {
        // count is a safe_int; any overflowing arithmetic is intercepted at
        // construction — the "50 - 500 becomes an enormous number" scenario
        // can no longer happen
        return safe_span{data_ + offset, count};
    }

    T* data() const { return data_; }
    std::size_t size() const { return size_.value; }
};
```

The key point is that the member `size_` has type `safe_int<std::size_t>` rather than a bare `std::size_t`. That means every operation on this size — subtraction, comparison, assignment — goes through our safety checks. If someone writes `50 - 500`, `safe_int` errors at the moment of the operation, instead of letting an enormous number quietly flow into `subspan`. **We don't patch things up inside `span`'s bounds checking; we stamp out wrong values at the source — integer arithmetic itself.** Looking back, the idea is really simple: replace unsafe built-in integers with a safe wrapper type, so errors are caught the moment they happen rather than after they've propagated into some bounds check. Put differently — let the class that is actually responsible handle its corresponding errors, instead of making other components clean up after you.

---

# Adding Bounds Checking to `span`: From Manual Defense to Type Deduction

Out-of-bounds array access has always been a headache: it certainly runs fast, but once you go out of bounds the program may crash somewhere completely unrelated, leaving you staring blankly at `gdb` for half an hour. Next we'll look at a structured way to do subscript bounds checking.

## First, Pin Down What We're Building

The core requirement is really simple: I have a contiguous memory region, I know how big it is, and I want every subscript access to automatically check whether the index is out of bounds. If it is, throw an exception immediately or have the compiler block it — rather than finding out after memory has been corrupted.

Doesn't that sound like what `std::vector`'s `at()` does? The difference is that I don't want to carry a dynamically allocating vector; I may only have a bare pointer plus a length, or a native array, and I want to access it just as safely. That is exactly the point of `span` — it doesn't own the data, it merely "watches" it, and while watching it can keep an eye on the bounds for you.

## Writing a Bounds-Checked Subscript Access

Let's start from the most basic scenario. Suppose I already have something span-like that internally holds the data and the size. What I need to do now is overload `operator[]` so it performs the range check before the access.

```cpp
#include <iostream>
#include <stdexcept>
#include <span>
#include <array>

// A simple bounds-checked span wrapper
template<typename T>
class checked_span {
    T* ptr_;
    std::size_t size_;

public:
    // Initialize with a pointer and a size — this is the essence of "spanable"
    checked_span(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}

    // Checked subscript access
    T& operator[](std::size_t index) {
        if (index >= size_) {
            throw std::out_of_range("下标越界了兄弟");
        }
        return ptr_[index];
    }

    const T& operator[](std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("下标越界了兄弟");
        }
        return ptr_[index];
    }

    std::size_t size() const { return size_; }
};
```

See — the constructor takes only a pointer and a size; that is the so-called "spanable": anything that can provide a data pointer and an element count can initialize it. Then `operator[]` does one thing: if the index you hand it is greater than or equal to the size, throw immediately.

## Run It and See

```cpp
int main() {
    int data[] = {1, 2, 3, 4, 5};
    checked_span<int> s(data, 5);

    // Normal access, no problem
    std::cout << s[2] << "\n";  // prints 3

    // Out-of-bounds access throws
    try {
        std::cout << s[10] << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

Run it and the output looks like this:

```text
3
捕获到异常: 下标越界了兄弟
```

At this point you may think there's nothing special here — isn't this just what `std::vector::at()` does? Hold on; the key point comes next.

## The Negative-Index Problem — The Signed/Unsigned Pit

Here is an easily missed trap. `operator[]` takes its parameter as `std::size_t`, an unsigned integer. What happens if you pass `-10` straight in?

```cpp
// You think you're passing -10, but the compiler performs an implicit conversion
// -10 as an unsigned integer becomes an enormous positive number
// s[-10] actually turns into something like s[18446744073709551606]
```

But! If you change the parameter type to the signed `ptrdiff_t`, the compiler can stop some obvious problems for you at compile time. Or, if you use the standard `std::span` implementation, it is particular about the index type.

Let me write it differently, making the index type signed so negative values get recognized correctly:

```cpp
template<typename T>
class checked_span_v2 {
    T* ptr_;
    std::size_t size_;

public:
    checked_span_v2(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}

    // Note ptrdiff_t (signed) here instead of size_t (unsigned)
    T& operator[](std::ptrdiff_t index) {
        // Check the negative case first
        if (index < 0) {
            throw std::out_of_range("负数下标，你想干嘛");
        }
        // Then check the upper bound
        if (static_cast<std::size_t>(index) >= size_) {
            throw std::out_of_range("下标越界了兄弟");
        }
        return ptr_[index];
    }

    std::size_t size() const { return size_; }
};
```

```cpp
int main() {
    int data[] = {1, 2, 3, 4, 5};
    checked_span_v2<int> s(data, 5);

    try {
        auto val = s[-10];  // Negative indices are now correctly caught
        std::cout << val << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

Output:

```text
捕获到异常: 负数下标，你想干嘛
```

What's worth noting here: with `size_t` as the index type, an incoming negative is silently converted into an astronomical number, and then either it happens not to be out of bounds and you read garbage (even scarier), or it is out of bounds and throws — with an error message that is completely misleading. With `ptrdiff_t`, a negative is a negative, plain and clear.

What the compiler can catch, though, is only the simplest case: literal negatives. In real projects, what actually bites is usually a value computed elsewhere — some function returned -1 to signal failure, you forgot to check, and passed it straight as an index. That kind can only be caught at runtime, but with this check at least the program won't silently corrupt memory.

## Using an Element of Another `span` as the Size — A Scenario Closer to Practice

The talk mentioned a very practical example: you take some element value out of one `span` and use it as the size argument of another operation. You have no idea what that value actually is — but unless it is a reasonable positive integer, it should be stopped.

```cpp
void process_with_dynamic_size(std::span<double> params, std::span<double> data) {
    // params[0] holds the number of elements we want to process,
    // but we don't know what it actually is: maybe 5, maybe -3, maybe 100000
    double count_raw = params[0];

    // Check before converting it to an integer
    if (count_raw < 0 || count_raw != static_cast<double>(static_cast<std::size_t>(count_raw))) {
        throw std::invalid_argument("params[0] 不是合法的正整数");
    }

    std::size_t count = static_cast<std::size_t>(count_raw);
    if (count > data.size()) {
        throw std::out_of_range("请求的元素个数超过了数据范围");
    }

    // Safely process the first count elements
    double sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += data[i];
    }
    std::cout << "前 " << count << " 个元素的和: " << sum << "\n";
}
```

```cpp
int main() {
    double params_good[] = {3.0};
    double params_bad[] = {-5.0};
    double data[] = {1.0, 2.0, 3.0, 4.0, 5.0};

    // The normal case
    process_with_dynamic_size(params_good, data);

    // The exceptional case
    try {
        process_with_dynamic_size(params_bad, data);
    } catch (const std::exception& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

Output:

```text
前 3 个元素的和: 6
捕获到异常: params[0] 不是合法的正整数
```

This pattern is extremely common in real projects. You get a number from a config file, a network protocol, or user input, then use it to decide how many elements to access. Without a check, this is a perfect security hole.

## Type Deduction: Stop Repeating What the Compiler Already Knows

By now, every site has to spell out `checked_span<int>`, `checked_span<double>`, repeating the element type — even though the compiler could clearly deduce it from the initializing arguments. That is the problem CTAD (Class Template Argument Deduction, introduced in C++17) solves. Just add a deduction guide:

```cpp
template<typename T>
class checked_span_v3 {
    T* ptr_;
    std::size_t size_;

public:
    checked_span_v3(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}

    T& operator[](std::ptrdiff_t index) {
        if (index < 0 || static_cast<std::size_t>(index) >= size_) {
            throw std::out_of_range("下标越界");
        }
        return ptr_[index];
    }

    const T& operator[](std::ptrdiff_t index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= size_) {
            throw std::out_of_range("下标越界");
        }
        return ptr_[index];
    }

    std::size_t size() const { return size_; }
};

// Deduction guide: whenever a (pointer, size) pair shows up, deduce the element type
template<typename T>
checked_span_v3(T*, std::size_t) -> checked_span_v3<T>;
```

Now it reads much cleaner:

```cpp
int main() {
    int aa[100] = {};

    // Used to be written as: checked_span_v3<int> s(aa, 100);
    // Now the compiler deduces it itself: aa is an int*, so s is checked_span_v3<int>
    // This way we write a lot less code
    checked_span_v3 s(aa, 100);

    // The compiler knows perfectly well that s is checked_span_v3<int> with 100 elements
    // I don't need to repeat int and 100
    s[0] = 42;
    std::cout << s[0] << "\n";  // 42

    // Combined with a range-based for loop, standard modern C++ practice
    // So much more comfortable than the old for (int i = 0; i < 100; ++i)
    std::size_t count = 0;
    for (auto& val : aa) {
        if (val != 0) ++count;
    }
    std::cout << "非零元素个数: " << count << "\n";  // 1

    return 0;
}
```

Type deduction looks like "syntax sugar," but after writing a hundred-plus span-related pieces of code in a project you realize: writing one less `int` isn't about saving three characters — it's that when you later change `int` to `int64_t`, you change it in one place instead of hunting all over for the spots you missed.

This is a core philosophy of generic programming: don't repeat what the compiler already knows — and what you already know.

## Sub-spans and Construction from a Pointer — A More Complete Toolbox

A complete span alone isn't enough. In real development you constantly need to slice a small piece out of a big span, or construct a span from a bare pointer.

Start with construction from a pointer. Given that the point of span is safety, isn't constructing a span from a bare pointer itself Unsafe? Indeed — there is no way to check whether that pointer really points to that many elements: the compiler doesn't know, and runtime can't verify it. But the key is this: **constructing a span from a pointer is, in itself, glaringly conspicuous in front of code review and static analysis tools**. If a project's rules say "all array access must go through span," then the moment `span(ptr, n)` code appears, reviewers see it at a glance: here is an unsafe boundary that needs a close look. That is far more manageable than `ptr[i]` scattered everywhere.

```cpp
#include <span>

// Helper for constructing a span from a pointer
// Deliberately written as a function so it stands out more in code review
template<typename T>
std::span<T> make_span_from_ptr(T* ptr, std::size_t size) {
    return std::span<T>(ptr, size);
}

// A sub-span of the first n elements
template<typename T>
std::span<T> take_front(std::span<T> s, std::size_t n) {
    if (n > s.size()) {
        throw std::out_of_range("take_front: n 超过了 span 的大小");
    }
    return s.subspan(0, n);
}

// A sub-span over a given range
template<typename T>
std::span<T> take_range(std::span<T> s, std::size_t offset, std::size_t count) {
    if (offset > s.size() || count > s.size() - offset) {
        throw std::out_of_range("take_range: 范围超出");
    }
    return s.subspan(offset, count);
}
```

```cpp
int main() {
    int data[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

    // Constructing from a pointer — this line will stand out during code review
    auto full = make_span_from_ptr(data, 10);

    // Take the first 3
    auto front3 = take_front(full, 3);
    std::cout << "前3个: ";
    for (auto v : front3) std::cout << v << " ";
    std::cout << "\n";

    // Take the range from index 2 to 5 (3 elements)
    auto mid = take_range(full, 2, 3);
    std::cout << "中间3个: ";
    for (auto v : mid) std::cout << v << " ";
    std::cout << "\n";

    // Out-of-bounds test
    try {
        auto bad = take_front(full, 20);
    } catch (const std::out_of_range& e) {
        std::cout << "捕获: " << e.what() << "\n";
    }

    return 0;
}
```

Output:

```text
前3个: 10 20 30
中间3个: 30 40 50
捕获: take_front: n 超过了 span 的大小
```

Notice how I wrote the bounds check in `take_range`: `count > s.size() - offset`. I didn't use `offset + count > s.size()`, because the latter can overflow when signed and unsigned are mixed. In this particular case `offset` and `count` are both `size_t` and won't overflow, but building the habit of doing range checks with subtraction rather than addition will save you pitfalls elsewhere. This is also the talk's advice to "use numbers rather than mixing signed and unsigned."

Likewise, these helpers can get deduction guides too, so call sites don't need template arguments. It's a two-line deduction-guide affair, but the code reads completely differently — you see `take_front(full, 3)`, not `take_front<int>(full, 3)`. The compiler knows `full` is a `span<int>`, so it can deduce that the return value is also `span<int>`; you don't have to do its worrying for it.

At this point, safe access for span, type deduction, and sub-span slicing all work. The code looks quite clean — no gratuitous duplication, and everything that should be checked is checked. But we're not done — tougher scenarios come later.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup"
    title="The Design and Evolution of C++"
    publisher="Addison-Wesley"
    :year="1994"
    chapter="Chapter 15: Templates"
    url="https://www.stroustrup.com/dne.html"
  />
  <ReferenceItem
    :id="2"
    author="Erwin Unruh"
    title="Prime Number Computation"
    :year="1994"
  />
  <ReferenceItem
    :id="3"
    author="Todd Veldhuizen"
    title="Using C++ Template Metaprograms"
    publisher="C++ Report"
    :year="1995"
    chapter="Vol. 7, No. 4, pp. 36-43"
  />
  <ReferenceItem
    :id="4"
    author="Alexander Stepanov, Meng Lee"
    title="The Standard Template Library"
    publisher="HP Laboratories"
    :year="1995"
    chapter="TR95-11(R.1)"
    url="https://www.stepanovpapers.com/stl.pdf"
  />
  <ReferenceItem
    :id="5"
    author="Kristen Nygaard (cited by Bjarne Stroustrup)"
    title="If you need a PhD to use it, you have failed"
    :year="2001"
    chapter="Cited by Stroustrup in his CppCon 2025 talk Concept-Based Generic Programming in C++, §1"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::span::subspan"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/container/span/subspan"
  />
  <ReferenceItem
    :id="7"
    author="Dennis M. Ritchie"
    title="Variable-Size Arrays in C"
    :year="1990"
    url="https://www.nokia.com/bell-labs/about/dennis-m-ritchie/vararray.pdf"
  />
</ReferenceCard>

### Further Reading

- Stroustrup, B. ["A History of C++: 1979–1991"](https://www.stroustrup.com/hopl2.pdf). *HOPL-II*, 1993. — The authoritative record of C++'s early history, covering the full context of the template design decisions.
- Lourseyre, C. ["[History of C++] Templates: from C-style macros to concepts"](https://belaycpp.com/2021/10/01/history-of-c-templates-from-c-style-macros-to-concepts/). *Belay the C++*, 2021. — A high-quality digest of Chapter 15 of Stroustrup's *D&E*, tracing the full evolution from C macros to C++20 concepts.
- Stroustrup, B. *The Design and Evolution of C++*. Addison-Wesley, 1994. — The authoritative account of C++'s design decisions; Chapter 15 is devoted to the motivations and trade-offs behind templates.
