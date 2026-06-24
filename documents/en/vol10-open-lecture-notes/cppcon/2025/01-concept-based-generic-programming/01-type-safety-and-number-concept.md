---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 Talk Notes — From implicit narrowing conversions to `Number<T>`
  wrapper types, then to `safe_int` and `checked_span`
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
  translated_at: '2026-06-24T00:32:20.617611+00:00'
  engine: anthropic
  token_count: 8926
---
# Let's Talk About Manual Checks to Implicit Guards

:::tip
A quick note: this section is an expansion based on CppCon. The links above point to their video series on YouTube; users in China can watch via the Bilibili links.
:::

Generic programming in C++ dates back to 1991 when templates were introduced to the language (C++ Release 3.0). Stroustrup's primary motivation for designing templates was to replace C preprocessor macros with type-safe generic containers. In *The Design and Evolution of C++*, he wrote that macros "fail to obey scope and type rules and don't interact well with tools," whereas templates were designed to be "as efficient as macros" but type safe<RefLink :id="1" preview="Stroustrup, The Design and Evolution of C++, 1994, Ch.15" />.

But the story took an unexpected turn in 1994. Erwin Unruh presented a piece of legal C++ code at a C++ committee meeting that wouldn't even compile, yet the compiler output a sequence of prime numbers line by line in the error messages<RefLink :id="2" preview="Unruh, Prime Number Computation, C++ committee meeting, 1994" />. The committee realized that templates had inadvertently constituted a Turing-complete compile-time computation system. The following year, Todd Veldhuizen published a paper systematically describing this technique and named it **Template Metaprogramming**<RefLink :id="3" preview="Veldhuizen, Using C++ Template Metaprograms, C++ Report, 1995" />. Templates thus evolved from a "type-safe macro replacement" to an indispensable compile-time abstraction mechanism in C++.

Template error messages often span hundreds of lines and are notoriously difficult to read—this is why many C++ developers shy away from generic programming. However, as project scale grows, the code duplication without generics becomes too high to maintain. In this article, we start from the basic motivations of generic programming and work towards a concrete, actionable type safety issue—implicit narrowing conversions.

The experimental environment for this article is Arch Linux WSL, GCC 16.1.1. Here is the environment information:

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

## Understanding the True Goal of Generic Programming

Generic programming makes code more generic and abstract—this is only half the story. Alex Stepanov (the father of the STL) pointed out that the goal of generic programming is "to express ideas in the most general, efficient, and flexible way possible." The key is expressing ideas, not abstraction for the sake of abstraction. Treating the means as the end is a common pitfall in programming—another typical example is the abuse of design patterns.

This distinction is crucial. We do not design code starting from an abstract model; instead, we start from concrete, efficient algorithms, discover commonalities, and then extract them. Furthermore, performance cannot be sacrificed, as a significant part of C++'s raison d'être lies here. As hardware becomes more powerful, our expectations for software are rapidly expanding. However, semiconductor processes seem to have hit a bottleneck, leaving less and less room for inefficient code.

Generic programming demands more from us: it requires us to identify reusable patterns within abstract domains. Its bottom line is that after abstraction, performance must not be inferior to a hand-written concrete version. Otherwise, there is no point in introducing generic programming. Writing code itself is about getting the job done; we do not do unnecessary work. If a piece of code will not be reused and is performance-sensitive, do not introduce generics.

## Alex Stepanov's Design Standards for C++

Around 1994, Stepanov proposed three design standards<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" />. First is **generality**: a good generic component should express use cases that even the designer hadn't thought of. Second is **uncompromising efficiency**: when writing system-level code in C++, efficiency must be on par with C, and when writing linear algebra, it must compete with Fortran. Third is **statically typed interfaces**: checks should happen at compile time, not leaving errors for runtime. Later, he added two very practical requirements: compilation time shouldn't be so long that one could go for a coffee break (header-only libraries make this hard to guarantee), and the learning curve shouldn't be so steep that it requires a PhD from MIT to get started<RefLink :id="5" preview="Nygaard, cited in Stroustrup, Concept-Based Generic Programming in C++, 2025, §1" />—as for whether C++ has achieved this, we all have our own opinions.

## Implicit Narrowing Conversion: A Classic Type Safety Trap

With the motivation out of the way, let's start with a specific problem. The introduction of a concept must have a corresponding problem scenario; otherwise, it is just a castle in the air. Consider this code:

```cpp
#include <iostream>

int main() {
    int big = 30000;
    short small = big;          // 30000 超出了 short 的范围吗？其实没有，short 一般是 -32768~32767
                                // 但如果是 40000 呢？

    short overflow = 40000;     // 编译通过！但值已经错了

    double pi = 3.14159;
    int int_pi = pi;            // 小数部分直接丢了

    std::cout << "overflow = " << overflow << "\n";  // 输出一个奇怪的负数
    std::cout << "int_pi = " << int_pi << "\n";      // 输出 3

    return 0;
}
```

This code follows a pre-C++23 style to ensure it compiles directly on all compilers.

On my machine, the result is `overflow = -25536` and `int_pi = 3`. The compiler doesn't emit a single warning (unless you enable `-Wall -Wextra`, which many projects don't). This kind of bug is particularly insidious: the code runs, but the results are wrong. It often stays hidden with small datasets and only surfaces after deployment.

Many people think, "that's just how C++ is, just be careful." But relying on human vigilance is unreliable. Bjarne Stroustrup himself has mentioned that he wanted to solve this problem back in the day but couldn't, and the C camp wouldn't allow changes. So, as users, can we defend against this ourselves?

## Using C++20 Concepts to Model "Numbers"

C++20 gives us a new weapon: concepts. Their essence is simple—a concept is a compile-time evaluated Boolean predicate. It takes a type as input and outputs true or false. In other words, it allows the compiler to understand a "concept" without us needing to describe it using complex natural language.

The standard library already defines some basic concepts, such as `std::integral` and `std::floating_point`, which determine whether a type is an integer type or a floating-point type. These aren't new inventions; the first edition of K&R C distinguished between int and float. The difference is that we now have a language-level, compile-time queryable representation.

Let's write a simple concept first to express the idea of a "number":

```cpp
#include <concepts>
#include <type_traits>

// 我自己的 "number" concept：要么是整数，要么是浮点数
template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

// 验证一下
static_assert(number<int>, "int 应该是 number");
static_assert(number<double>, "double 应该是 number");
static_assert(number<char>, "char 也是整数类型，所以是 number");
static_assert(!number<std::string>, "string 不是 number");
```

Here is a syntax detail worth explaining: `std::integral<T>` looks like a function call, but it isn't. `std::integral` is a concept, and `<T>` instantiates it with type `T`. The value of the entire expression is a compile-time `bool`. You cannot write `std::integral(T)`, as that syntax is incorrect. You can simply understand it as "perform the `integral` test on `T`," which returns `true` or `false`.

If we run the code above, all four `static_assert` checks pass, which indicates that our `number` concept works as expected.

## Writing a narrowing check

Can we write a concept to determine whether assigning a value of type `U` to type `T` constitutes a narrowing conversion? Since we are writing this article, let's give it a try.

First, if the representable range of `T` is smaller than that of `U`, narrowing is obviously possible. For example, assigning an `int` to a `short` is risky because `int` can represent many more values than `short`. But how do we determine if the range is "smaller"? The C++ standard library does not provide a concept for "value range" directly, but `<type_traits>` provides `std::numeric_limits`, which allows us to query the minimum and maximum values of various types. If `U` is a floating-point type and `T` is an integer type, the fractional part will inevitably be lost, which is also narrowing.

There is another easily overlooked scenario: `U` and `T` are both integers of the same size (e.g., both are 32-bit), but one is signed and the other is unsigned. Assigning a negative number to an unsigned type will cause issues. Let's translate these rules into code:

```cpp
#include <concepts>
#include <type_traits>
#include <limits>

template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

// 判断 T 是否"比 U 小"（能表示的值更少）
// 这里用 numeric_limits 的范围来比较
template<typename T, typename U>
concept smaller_range =
    number<T> && number<U> &&
    (std::numeric_limits<T>::max() < std::numeric_limits<U>::max() ||
     std::numeric_limits<T>::min() > std::numeric_limits<U>::min());

// 核心判断：从 U 到 T 的赋值是否会发生窄化
template<typename T, typename U>
concept narrowing_assign =
    number<T> && number<U> &&
    (
        // 情况1：T 的范围比 U 小，可能放不下
        smaller_range<T, U> ||
        // 情况2：U 是浮点数，T 是整数，小数部分会丢
        (std::floating_point<U> && std::integral<T>) ||
        // 情况3：U 和 T 大小相同但有符号性不同
        (std::integral<T> && std::integral<U> &&
         std::signed_integral<U> != std::signed_integral<T>)
    );

// 测试用例
static_assert(narrowing_assign<short, int>, "int -> short 应该是窄化");
static_assert(narrowing_assign<int, double>, "double -> int 应该是窄化（丢小数）");
static_assert(narrowing_assign<unsigned int, int>, "int -> unsigned int 可能窄化（负数问题）");
static_assert(!narrowing_assign<int, short>, "short -> int 不是窄化");
static_assert(!narrowing_assign<double, float>, "float -> double 不是窄化");
static_assert(!narrowing_assign<int, int>, "int -> int 不是窄化");
```

Let's compile and run it; all six `static_assert` checks pass. We can verify the logic using the last case, `!narrowing_assign<int, int>`. When assigning the same type, condition 1, `smaller_range<int, int>`, evaluates `max() < max()` to false and `min() > min()` to false, so it doesn't trigger. Condition 2 requires U to be floating point and T to be an integer, which isn't met. Condition 3 requires different signedness, but `int` and `int` are obviously the same. With all three branches false, the overall result is false, and the negation causes the `static_assert` to pass—this aligns perfectly with our intuition that "assignment between the same types is not narrowing."

Another point worth mentioning: we must add parentheses where `&&` and `||` are mixed in `narrowing_assign`. Since `&&` has higher precedence than `||`, without parentheses, `number<T> && number<U>` would only constrain the first `||` branch. The latter two branches might be evaluated for non-number types—although the result happens to be correct for the current test cases, the semantics are wrong. Adding parentheses makes the three branches a single unit, uniformly constrained by `number<T> && number<U>`, which ensures the logic is rigorous.

## Considering Some Edge Cases

The implementation above covers most scenarios, but some details are worth discussing. For example, conversions between floating-point numbers: is `double` to `float` narrowing? From a precision perspective, yes, because `double` can represent more significant digits than `float`. In the current implementation, `smaller_range<float, double>` evaluates `numeric_limits<float>::max() < numeric_limits<double>::max()` to true, so it is correctly identified as narrowing.

Consider the case of `char` to `unsigned char`. The signedness of `char` is implementation-defined (signed on some platforms, unsigned on others). If `char` is signed on the platform, `signed_integral<char> != signed_integral<unsigned char>` is true, and it will be identified as narrowing. This is actually reasonable, because if `char` is -1, assigning it to `unsigned char` results in 255.

However, note that this implementation is not yet 100% rigorous. The standard's definition of narrowing conversion (in the C++11 list initialization rules) is more detailed than what is written here, for instance, considering whether the value is within the integer range when converting from floating-point to integer. But as a starting point, this concept is already sufficient to block most pitfalls. We can refine it gradually later.

At this point, we can summarize one thing: concepts are not some profound, unfathomable metaprogramming technique; they are simply a mechanism to "express type constraints as boolean expressions checkable at compile time." Previously, when writing templates, constraints relied entirely on documentation and naming conventions (e.g., "please pass a random access iterator"). The compiler didn't check this, and passing the wrong type resulted in cryptic error dumps. Now, with concepts, the compiler can tell you immediately "the type you passed doesn't meet the requirements," and the error message is human-readable.

The next step is to apply this `narrowing_assign` concept to actual functions to create a safe assignment wrapper—that's the content of the next section. At the very least, the core idea of "using concepts to express type constraints" is now clear.

---

# From Manual Judgment to Implicit Guarding: Embedding Narrowing Conversion Checks into Types

In the previous section, we figured out the rules for determining narrowing conversions. If we had to mentally run through those rules every time we wrote code, it would be almost impossible—when mixing signed and unsigned types, figuring out which is larger, whether overflow will occur, and if the positive range can be represented is enough to make one dizzy. The speaker mentioned that writing this out by hand takes about a page of paper, and it's messy and tricky.

So, the task for this section is to turn that page of messy logic into working code, and then hide it so you don't even feel its existence when writing code normally.

## Translating the Judgment Logic into Code

One intuition is: to judge whether assigning a value from type U to type T will cause narrowing, just use a `static_cast` and compare. But if you think about it carefully, that's not right at all—when mixing signed and unsigned types, the comparison itself has traps. Therefore, we need an honest, step-by-step function.

The idea is to do as much elimination as possible at compile time, filtering out situations where "narrowing absolutely cannot happen," leaving only the paths that truly need runtime checks. This is actually what generic programming has always emphasized—don't do work at runtime that shouldn't be done there.

```cpp
#include <type_traits>
#include <limits>
#include <stdexcept>

// 核心判断：值 u 赋值给 T 类型的变量，会不会发生 narrowing？
template<typename T, typename U>
constexpr bool would_narrow(U u) noexcept {
    // 第一层：编译期就能排除的情况
    // 如果 T 能表示 U 的所有值，那不管 u 是什么，都不可能 narrowing
    if constexpr (std::is_same_v<T, U>) {
        return false;  // 同类型，废话
    } else if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        // 浮点转整数，几乎总是可能 narrowing 的
        // 除非这个浮点数恰好是个整数值且在范围内
        // 这个我们放运行时判断
    } else if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<U>) {
        // 浮点转浮点，只有当 T 的精度/范围小于 U 时才可能 narrowing
        if constexpr (std::numeric_limits<T>::digits >= std::numeric_limits<U>::digits &&
                      std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                      std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
            return false;  // T 的范围和精度都够，编译期直接排除
        }
    } else if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
        // 整数转整数的情况最复杂，下面细说
        if constexpr (std::is_signed_v<T> == std::is_signed_v<U>) {
            // 同号比较简单：看范围
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                          std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
                return false;
            }
        } else if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
            // signed T 接收 unsigned U
            // T 的正数范围能覆盖 U 的全部值就行
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max()) {
                return false;
            }
        } else {
            // unsigned T 接收 signed U
            // U 是负数的话肯定 narrowing，但编译期不知道 u 的值
            // 所以这种情况不能在编译期排除，得放运行时
        }
    }

    // 第二层：编译期排不掉的，运行时判断

    // signed -> unsigned 且源值为负数：一定是 narrowing
    // 注意：不能用 round-trip 检测（int(-1) → unsigned → int(-1) 在补码上是可逆的）
    if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        if (u < 0) return true;
    }

    // 先做静态转换，看值有没有变
    T t = static_cast<T>(u);
    if (static_cast<U>(t) != u) {
        return true;  // 转过去再转回来，值不一样，信息丢了
    }

    // 浮点转整数的额外检查：即使转回来一样，也要确认不是那种
    // "3.0 转成 3 再转回 3.0"的巧合——不过这种情况下值确实没丢，
    // 所以其实上面的检查已经够了。但标准对浮点转整数有更严格的要求：
    // 原值必须是整数值（没有小数部分）
    if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        // 如果 u 不是整数值，即使 static_cast 恰好截断了，
        // 严格来说也是 narrowing（信息丢失了小数部分）
        if (u != static_cast<U>(static_cast<long long>(u))) {
            return true;
        }
    }

    return false;
}
```

Looking back at this function, when mixing signed and unsigned types, we need to carefully consider the boundary between what the compiler can eliminate at compile time and what must be checked at runtime. There is a subtle pitfall here: simply using a round-trip check (converting back and forth) fails to detect narrowing during signed-to-unsigned conversion. This is because `int(-1) → unsigned(4294967295) → int(-1)` is perfectly reversible under two's complement, so the round-trip check won't catch it. Therefore, we must explicitly check "is the source value negative?" before the round-trip. `if constexpr` plays a key role here—branches determined at compile time generate no code, avoiding a bunch of useless comparison instructions.

## What to do when narrowing occurs? Throw an exception

Now that we have the validation logic, the next decision is: how do we handle narrowing once detected?

The speaker's approach is straightforward—throw an exception. After compile-time filtering, the probability of narrowing actually triggering at runtime is extremely low. In most code, types match and are eliminated during compilation; for the remaining cases requiring runtime checks, the vast majority won't actually overflow. It might trigger only once in a million calls, which is exactly the scenario where exceptions shine—handling extremely rare exceptional cases.

```cpp
template<typename T, typename U>
constexpr T narrow_convert(U u) {
    if (would_narrow<T>(u)) {
        throw std::invalid_argument("narrowing conversion detected");
    }
    return static_cast<T>(u);
}
```

It's that simple. We can use it directly:

```cpp
#include <iostream>

int main() {
    // 正常情况，不会抛异常
    int a = narrow_convert<int>(42.0);        // OK，42.0 是整数值
    unsigned int b = narrow_convert<unsigned int>(100);  // OK

    // 这些会抛异常
    try {
        char c = narrow_convert<char>(300);   // 300 超出 char 范围
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        unsigned int d = narrow_convert<unsigned int>(-1);  // 负数转 unsigned
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        int e = narrow_convert<int>(3.14);  // 浮点转整数，有小数部分
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    std::cout << "a = " << a << ", b = " << b << "\n";
}
```

Let's run it and check the output:

```text
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
a = 42, b = 100
```

Excellent, we have successfully intercepted the problematic cases. However, a problem arises—we cannot write `narrow_convert<int>(xxx)` at every assignment location. The code would become verbose, and maintaining consistency would be impossible. Relying on programmers to manually add checks will inevitably lead to oversights. Some places will have them, others will be forgotten, and bugs will hide in those forgotten spots.

## Putting the Check into the Type: `Number<T>`

Therefore, the real solution is to make the check implicit. We define a wrapper type, `Number<T>`, that automatically performs narrowing checks upon construction. From then on, we use `Number<T>` just like a regular `T`, but we do not need to worry about narrowing issues, because if the construction check fails, the object simply will not exist.

```cpp
template<typename T>
class Number {
    T value_;

public:
    // 构造函数：这里是所有魔法发生的地方
    template<typename U>
    constexpr Number(U u) : value_(narrow_convert<T>(u)) {}

    // 同类型构造不需要检查，但为了统一接口也走一遍（编译期会优化掉）
    constexpr Number(T t) : value_(t) {}

    // 隐式转换回 T，让 Number<T> 能像 T 一样用
    constexpr operator T() const noexcept { return value_; }

    // 取值
    constexpr T get() const noexcept { return value_; }
};
```

Look, this class is quite minimal. It might look like demo code, but it actually works. Let's try it out:

```cpp
int main() {
    // 这些都能正常工作
    Number<int> x = 42;              // int -> int，没问题
    Number<int> y = 3.0;             // double -> int，3.0 是整数值，没问题
    Number<unsigned int> z = 100u;   // unsigned int -> unsigned int，没问题

    // Number<T> 可以当 T 用，因为有了 operator T()
    int sum = x + static_cast<int>(z);  // 正常运算
    std::cout << "x = " << x << ", y = " << y << ", z = " << z << "\n";
    std::cout << "sum = " << sum << "\n";

    // 这些会在构造时抛异常
    try {
        Number<char> c = 300;  // 编译不报错，运行时抛异常
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        Number<unsigned int> bad = -1;  // 负数转 unsigned，运行时抛异常
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }
}
```

It appears you have provided the instruction prompt but not the actual Chinese Markdown content to be translated.

Please paste the **Chinese Markdown text** you would like me to translate below. I will process it according to the rules provided (preserving code blocks, translating terminology like NVIC/RAII/lambda expressions, and maintaining the Markdown structure).

```text
x = 42, y = 3, z = 100
sum = 142
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
```

At this point, a key design philosophy becomes clear: we used to think of template metaprogramming and type systems as separate things, but the type system itself is actually the best place to perform checks. We don't need to remember where to check or where not to check; simply by using `Number<T>` instead of `T`, the checking happens automatically. Furthermore, thanks to compile-time `if constexpr` branching, code isn't even generated for paths that don't need checking (such as assignments between the same types), resulting in zero overhead.

## But construction alone isn't enough; we need arithmetic

If a numeric type can only be constructed but not calculated, how is it different from a constant? Therefore, we need to add arithmetic operators to `Number<T>`. However, a problem arises: what should the result of adding `Number<int>` and `Number<double>` be? We can't just return any arbitrary type; we need a rule.

The standard library provides a utility called `std::common_type`, designed specifically for this purpose—given two types, it tells you which type to use for the result of arithmetic operations. For example, `common_type_t<int, double>` is `double`, and `common_type_t<int, unsigned int>` is `unsigned int` on most platforms. We'll use it directly:

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

    // 加法：Number<T> + Number<U> -> Number<common_type_t<T, U>>
    template<typename U>
    constexpr auto operator+(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        // value_ + other.value_ 先做普通算术（会隐式提升），
        // 然后用结果构造 Number<ResultType>，构造时自动做 narrowing 检查
        return Number<ResultType>(value_ + other.get());
    }

    // 减法，同理
    template<typename U>
    constexpr auto operator-(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        return Number<ResultType>(value_ - other.get());
    }

    // 乘法
    template<typename U>
    constexpr auto operator*(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        return Number<ResultType>(value_ * other.get());
    }
};
```

Let's run a slightly more complex example to verify this:

```cpp
int main() {
    Number<int> a = 10;
    Number<double> b = 3.5;

    // int + double -> common_type 是 double
    auto result = a + b;
    std::cout << "10 + 3.5 = " << result << "\n";
    std::cout << "结果类型是 Number<double>? "
              << std::is_same_v<decltype(result), Number<double>> << "\n";

    // unsigned + int 的混合运算
    Number<unsigned int> big = 3000000000u;  // 30亿，unsigned int 能表示
    Number<int> small = 100;

    auto result2 = big + small;
    std::cout << "3000000000u + 100 = " << result2 << "\n";

    // 试试溢出场景：两个大数相加
    Number<unsigned int> x = 3000000000u;
    Number<unsigned int> y = 2000000000u;
    try {
        // 3000000000 + 2000000000 = 5000000000，超出 unsigned int 范围
        auto overflow = x + y;
        std::cout << "不应该到这里\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "加法溢出捕获到: " << e.what() << "\n";
    }
}
```

It appears that the source text to be translated was not included in your message. The prompt ended with "输出：" (Output:), but no content followed.

Please provide the Chinese Markdown text you would like me to translate. Once you provide it, I will translate it following all the specified rules, including:

- Translating naturally and idiomatically.
- Preserving code blocks and technical syntax.
- Using the specific terminology provided (e.g., `interrupt service routine (ISR)`, `RAII`, `constexpr`).
- Handling frontmatter, tables, and links appropriately.

I am ready to assist as soon as you share the content.

```text
10 + 3.5 = 13.5
结果类型是 Number<double>? 1
3000000000u + 100 = 3000000100
加法溢出捕获到: narrowing conversion detected
```

:::warning Correction: Unsigned arithmetic overflow is not detected by `narrow_convert`
In the output above, the last line "Addition overflow caught" **will not appear** in actual compilation and execution. Actual test results (GCC 16.1.1, C++20):

```text
Raw unsigned sum: 705032704
Would narrow? 0
No exception thrown! overflow = 705032704
```

The reason is that arithmetic operations on `unsigned int + unsigned int` in C++ **wrap around** (well-defined wrapping). The result of `3000000000u + 2000000000u` is `705032704`—a valid `unsigned int` value. Subsequently, `narrow_convert<unsigned int>(705032704u)` detects an assignment of the same type, so `would_narrow` simply returns false, and the exception is never thrown.

This is a fundamental limitation of the current `Number<T>` design: `narrow_convert` can only detect **narrowing conversions during assignment**, not **overflow in arithmetic operations themselves**. To detect overflow, we need to use compiler built-ins (like `__builtin_add_overflow`) or perform manual checks:

```cpp
template<typename T>
constexpr T safe_add(T a, T b) {
    if constexpr (std::is_unsigned_v<T>) {
        if (a > std::numeric_limits<T>::max() - b) {
            throw std::overflow_error("unsigned addition overflow");
        }
    } else {
        // signed overflow is UB, 必须用 __builtin_add_overflow 或类似机制
        T result;
        if (__builtin_add_overflow(a, b, &result)) {
            throw std::overflow_error("signed addition overflow");
        }
        return result;
    }
    return a + b;
}
```

See the verification code in [01-06-overflow-not-caught.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/01-concept-based-generic-programming/01-06-overflow-not-caught.cpp).
:::

Looking at the last example of overflow catching, we need to note that `narrow_convert` can only intercept narrowing **during type conversion**. It is powerless against overflow inherent to arithmetic operations of the same type (such as the wraparound of `unsigned int + unsigned int`). Since `common_type_t<unsigned int, unsigned int>` is just `unsigned int` itself, the calculation result has already wrapped around to a valid value before being assigned to `Number<unsigned int>`. To fully defend against arithmetic overflow, we need additional mechanisms (such as compiler built-in overflow checking functions), which goes beyond the scope of `narrow_convert`'s responsibilities.

At this point, the thread is finally connected: from manual rule checking, to runtime check functions, to exception handling strategies, and finally to wrapper types and arithmetic operations. The key is to understand these elements as a complete narrowing defense system, rather than as isolated knowledge points.

---

# No Need to Reinvent the Wheel: Library Function Objects + Eliminating Comparison Traps

To implement a set of safe integer types, intuition suggests that we must manually write out every addition, subtraction, multiplication, division, and comparison operator. Just thinking about it is overwhelming. However, the standard library has long prepared function objects like `std::plus` and `std::multiplies`. Each is only a few lines of code and certainly not black magic. Of course, reinventing the wheel is a traditional C++ pastime.

## Let's First Look at How Operators Are Written

A common misconception is that to overload `operator+` or `operator*` for custom types, we must write a bunch of `friend` functions inside the class or globally, handling various edge cases in each function. In reality, we only need to utilize the function objects from the standard library.

```cpp
#include <functional>

// 我定义了一个简化版的 safe_int，只展示核心思路
template <typename T>
struct safe_int {
    T value;

    // 加法：直接用标准库的 std::plus，一行搞定
    friend safe_int operator+(const safe_int& a, const safe_int& b) {
        return safe_int{std::plus<T>{}(a.value, b.value)};
    }

    // 乘法：同理
    friend safe_int operator*(const safe_int& a, const safe_int& b) {
        return safe_int{std::multiplies<T>{}(a.value, b.value)};
    }
};
```

You will notice that the key here is that `std::plus<T>{}` is a function object. When we invoke it, if an unintended type conversion occurs (for example, mixing signed and unsigned types), it will be intercepted by the rules we established previously. We don't need to worry about the calculation logic itself, as the standard library has already implemented it; we simply handle the "interception" and "pass-through."

## Comparison Operations: The Danger Zone for Signed/Unsigned Mixing

Overloading operators itself isn't difficult, but comparison operations are where the real danger lies when mixing signed and unsigned types. Spending an entire afternoon debugging a bug, only to find out it was caused by a single incorrect comparison, is not an uncommon occurrence.

Let's look at this code:

```cpp
#include <iostream>

int main() {
    int a = -1;
    unsigned int b = 2;

    std::cout << (a < b) << "\n";  // 你猜输出什么？
}
```

Run it, and the output is `0`, which means `false`. A negative number is less than a positive number, yet the result is false? Why? The answer lies in one of C++'s implicit conversion rules: when signed and unsigned numbers are mixed in a comparison, the signed value is converted to an unsigned value. Consequently, `-1` becomes a massive number (`4294967295`), which is naturally not less than 2. This rule has existed since the inception of C in 1972; it might have seemed fine at the time, but over the decades, it has likely been the source of countless bugs.

As the presentation aptly put it: this rule should have been fixed back in 1972, but by the time everyone realized how terrible it was, there was already too much code in the world relying on this behavior. It became impossible to change, and we are still suffering the consequences today.

## Fixing the Comparison Trap Ourselves

Since built-in types are unreliable, let's intercept the comparison operators in our `safe_int`. The approach is straightforward: if the types on both sides differ (one signed, one unsigned), we perform a special check first; if the types are the same, we proceed with the normal comparison.

```cpp
template <typename T>
struct safe_int {
    T value;
};

// 跨类型的 operator<：模板化的自由函数，能处理 safe_int<T> 和 safe_int<U> 的比较
template <typename T, typename U>
bool operator<(const safe_int<T>& a, const safe_int<U>& b) {
    if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
        // a 是有符号，b 是无符号
        // 如果 a 小于零，那它一定小于任何无符号数，直接返回 true
        if (a.value < 0) {
            return true;
        }
        // 否则两边都转成无符号再比，此时 a.value 一定是非负的，转换安全
        return static_cast<std::make_unsigned_t<T>>(a.value) < b.value;
    } else if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        // 反过来，a 是无符号，b 是有符号
        if (b.value < 0) {
            return false;
        }
        return a.value < static_cast<std::make_unsigned_t<U>>(b.value);
    } else {
        // 类型一致，正常比较，没有任何转换问题
        return a.value < b.value;
    }
}
```

Here is a key point: we implemented `operator<` as a **templated free function** rather than a `friend` function inside the class. This is because a `friend bool operator<(const safe_int& a, const safe_int& b)` defined inside the class only accepts two `safe_int<T>` instances with the **same T**. However, comparing `safe_int<int>` with `safe_int<unsigned int>` involves two different template instances, so the in-class friend function cannot match it. By writing it as a free function `template<typename T, typename U>`, the compiler can correctly match this operator between `safe_int<int>` and `safe_int<unsigned int>`. The `if constexpr` allows the compiler to optimize away the branch that isn't taken, resulting in zero overhead. Equality and greater-than comparisons follow the same logic, so we can simply implement them similarly.

Let's verify this:

```cpp
int main() {
    safe_int<int> a{-1};
    safe_int<unsigned int> b{2};

    std::cout << (a < b) << "\n";  // 输出 1，终于正确了！
    // 注意：a 和 b 是不同模板实例 safe_int<int> 和 safe_int<unsigned int>，
    // 只有模板化的自由 operator< 才能匹配这个调用
}
```

## A Bigger Pitfall: Silently Bypassed Bounds Checks

We fixed the comparison operators, but there is an even more subtle scenario. The presentation gave an example using `span`—a pattern that is extremely common in production code.

First, some background. `std::span` is essentially a "fat pointer"—a pointer to a sequence of elements combined with the length of that sequence. This concept isn't new; Dennis Ritchie proposed adding pointers carrying boundary information to C (for variable-length arrays) in the early 1990s, calling them "fat pointers," but the committee rejected the idea due to runtime overhead concerns <RefLink :id="7" preview="Ritchie, Variable-Size Arrays in C, 1990" />. Now that C++20 has finally introduced `span`, it serves as a long-overdue validation—while `span` itself doesn't perform bounds checking, it provides the foundation for higher-level safety wrappers.

So, where is the problem? Look at this code:

```cpp
#include <span>
#include <vector>

void process(std::span<int> data) {
    // 我想取 data 的前 max_size - 500 个元素
    unsigned int max_size = 50;  // 本来想写 500，手误写成了 50
    auto sub = data.subspan(0, max_size - 500);
    // sub 现在是什么？
}
```

`max_size` is an `unsigned int` with a value of 50. What happens when we calculate `50 - 500` using unsigned arithmetic? Underflow occurs, resulting in a massive number (roughly `4294967296 - 450`). Then `subspan` receives this massive length—and `std::span::subspan` in C++20 **does not** perform bounds checking. It only has a precondition (violating it results in undefined behavior) and will not throw an exception <RefLink :id="6" preview="cppreference, std::span::subspan, C++20" />. This means that massive number is passed right in, leading to undefined behavior—it might read memory it shouldn't, it might not crash, but you certainly cannot rely on `span` to catch it for you.

Just because of a small typo, and simply because of the conversion rules of built-in types, you completely lose the protection of range checking. Many people think `span` is safe enough, only to find that it is easily bypassed at the parameter calculation level.

## Adding real protection to `span` with `safe_int`

Now that we have `safe_int`, which can intercept all incorrect conversions, can we also protect the size parameters of `span`? Of course we can.

My approach is: first, define a concept representing "types that can be used by `span`," and then require within that concept that the size type must be a safe integer.

```cpp
#include <concepts>
#include <span>
#include <vector>

// 先定义我们自己的 safe_int（简化版，假设已经实现了完整的安全运算）
template <typename T>
struct safe_int {
    T value;
    // ... 之前写的所有运算符重载都在这里
};

// 定义一个概念：可 span 的类型
// 标准库里有 std::contiguous_range，我基于它扩展
template <typename T>
concept spanable = std::contiguous_range<T>;

// 现在定义一个安全 span，尺寸类型用 safe_int
template <typename T>
struct safe_span {
    T* data_;
    safe_int<std::size_t> size_;  // 关键：尺寸是安全整数

    // 构造函数，从普通容器构造
    template <spanable Container>
    explicit safe_span(Container& c)
        : data_(c.data())
        , size_(safe_int<std::size_t>{c.size()})
    {}

    // 安全的 subspan
    safe_span subspan(std::size_t offset, safe_int<std::size_t> count) {
        // count 是 safe_int，任何溢出运算都会在构造阶段就被拦住
        // 不可能再出现 "50 - 500 变成巨大数" 的情况
        return safe_span{data_ + offset, count};
    }

    T* data() const { return data_; }
    std::size_t size() const { return size_.value; }
};
```

The key point is that the member variable `size_` is of type `safe_int<std::size_t>` rather than a bare `std::size_t`. This means that any operation on this size—subtraction, comparison, or assignment—undergoes our safety checks. If someone writes `50 - 500`, `safe_int` will report the error the moment the operation occurs, rather than allowing a huge number to silently slip into the `subspan`. **We do not need to patch things up in the span's boundary checks; we need to eliminate the generation of erroneous values at the source—the integer arithmetic itself.** Looking back, the idea is actually quite simple: replace unsafe built-in integers with a safe wrapper type so that errors are caught the moment they occur, rather than waiting for them to propagate to some boundary check. In other words—let the class actually responsible for the value handle the corresponding errors, rather than relying on other components to provide a safety net.

---

# Adding Bounds Checking to span: From Manual Defense to Type Deduction

Array out-of-bounds access is a persistent headache: it runs fast, but once you exceed the bounds, the program might crash in a completely unrelated place, leaving you staring at `gdb` for half an hour. Next, we will look at a structured approach to bounds checking for subscripts.

## Clarifying Our Goal

The core requirement is quite simple: I have a contiguous memory region, I know its size, and I want to automatically check if a subscript is out of bounds every time I access it. If it is out of bounds, I want to throw an exception immediately or have the compiler block it, rather than waiting until memory is corrupted to discover the issue.

This sounds exactly like what `std::vector`'s `at()` does, right? But the difference is that I don't want the overhead of a dynamically allocated vector. I might simply have a raw pointer plus a length, or a native array, and I want to access it with the same level of safety. This is where `span` comes in—it doesn't own the data, it just "views" the data, but it can watch the boundaries for you while it's looking.

## Implementing Checked Subscript Access

Let's start with the most basic scenario. Assume we already have a `span`-like type that holds data and a size internally. What we need to do now is overload `operator[]` to perform a range check before executing the access.

```cpp
#include <iostream>
#include <stdexcept>
#include <span>
#include <array>

// 一个简单的带边界检查的 span 包装
template<typename T>
class checked_span {
    T* ptr_;
    std::size_t size_;

public:
    // 用指针和大小初始化——这就是"spanable"的本质
    checked_span(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}

    // 带检查的下标访问
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

You see, the constructor here only accepts a pointer and a size. This is what we call "spanable"—anything that can provide a data pointer and an element count can be used to initialize it. Then, inside `operator[]`, we do one thing: if the index you provide is greater than or equal to the size, we throw an exception directly.

## Let's Run It and See

```cpp
int main() {
    int data[] = {1, 2, 3, 4, 5};
    checked_span<int> s(data, 5);

    // 正常访问，没问题
    std::cout << s[2] << "\n";  // 输出 3

    // 越界访问，抛异常
    try {
        std::cout << s[10] << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

The output looks like this when we run it:

```text
3
捕获到异常: 下标越界了兄弟
```

At this point, you might think this is nothing special—isn't this just what `std::vector::at()` does? Not so fast; the key point comes next.

## The Problem with Negative Indices—The Signed/Unsigned Pitfall

Here is a trap that is easy to overlook. The parameter type accepted by `operator[]` is `std::size_t`, which is an unsigned integer. What happens if we pass a `-10` directly?

```cpp
// 你以为你在传 -10，其实编译器会做隐式转换
// -10 作为无符号整数会变成一个巨大的正数
// s[-10] 实际上变成了 s[18446744073709551606] 之类的鬼东西
```

However! If we change the parameter type to the signed `ptrdiff_t`, the compiler can catch obvious issues at compile time. Alternatively, if we use the standard implementation of `std::span`, it has specific requirements for the index type.

Let's rewrite this by changing the index type to signed, so that negative numbers are correctly identified:

```cpp
template<typename T>
class checked_span_v2 {
    T* ptr_;
    std::size_t size_;

public:
    checked_span_v2(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}

    // 注意这里用 ptrdiff_t（有符号）而不是 size_t（无符号）
    T& operator[](std::ptrdiff_t index) {
        // 先检查负数
        if (index < 0) {
            throw std::out_of_range("负数下标，你想干嘛");
        }
        // 再检查上界
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
        auto val = s[-10];  // 现在能正确捕获负数下标了
        std::cout << val << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

Please provide the Chinese Markdown content you would like me to translate. You have sent the instruction prompt, but the source text is missing.

Once you paste the content, I will translate it following your specified rules, ensuring technical accuracy and natural English flow.

```text
捕获到异常: 负数下标，你想干嘛
```

It is worth noting that when using `size_t` as the index type, passing in a negative number results in an implicit conversion to an astronomically large number. This either leads to an out-of-bounds read that retrieves garbage data (which is even scarier), or an exception is thrown with a completely misleading error message. By switching to `ptrdiff_t`, a negative number remains a negative number, making the issue crystal clear.

However, the compiler can only catch the simplest cases, such as literal negative numbers. In real-world engineering, the real problems often come from values calculated elsewhere—for example, a function returns `-1` to indicate failure, but we forget to check the result and use it directly as an index. This can only be caught at runtime, but with this check in place, the program won't silently corrupt memory.

## Using an element from another span as a size—a more realistic scenario

The presentation mentions a very practical example: using a value from one span as a size parameter for another operation. We don't actually know what that specific value is, but unless it is a reasonable positive integer, it should be blocked.

```cpp
void process_with_dynamic_size(std::span<double> params, std::span<double> data) {
    // params[0] 里存的是我们想要处理的元素个数
    // 但我们不知道它到底是多少，可能是 5，可能是 -3，可能是 100000
    double count_raw = params[0];

    // 把它转成整数之前，先做检查
    if (count_raw < 0 || count_raw != static_cast<double>(static_cast<std::size_t>(count_raw))) {
        throw std::invalid_argument("params[0] 不是合法的正整数");
    }

    std::size_t count = static_cast<std::size_t>(count_raw);
    if (count > data.size()) {
        throw std::out_of_range("请求的元素个数超过了数据范围");
    }

    // 安全地处理前 count 个元素
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

    // 正常情况
    process_with_dynamic_size(params_good, data);

    // 异常情况
    try {
        process_with_dynamic_size(params_bad, data);
    } catch (const std::exception& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

It looks like you haven't provided the Chinese Markdown content yet. Please paste the text you would like me to translate below.

Once you provide the content, I will translate it following your specific guidelines for modern C++ and embedded systems terminology.

```text
前 3 个元素的和: 6
捕获到异常: params[0] 不是合法的正整数
```

This pattern is extremely common in real-world projects. We receive a number from a configuration file, a network protocol, or user input, and then use it to determine how many elements to access. Without proper bounds checking, this becomes a perfect security vulnerability.

## Type Deduction: Stop Repeating What the Compiler Already Knows

At this point, we have to write `checked_span<int>` or `checked_span<double>` every time, repeating the element type even though the compiler can already infer it from the initialization arguments. This is exactly what C++17's CTAD (Class Template Argument Deduction) was designed to solve. We simply need to add a deduction guide:

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

// 推导指引：只要看到 (指针, 大小) 的组合，自动推导元素类型
template<typename T>
checked_span_v3(T*, std::size_t) -> checked_span_v3<T>;
```

Now, the code is much cleaner:

```cpp
int main() {
    int aa[100] = {};

    // 以前要这么写：checked_span_v3<int> s(aa, 100);
    // 现在编译器自己推断，aa 是 int*，所以 s 就是 checked_span_v3<int>
    // 这样，我们可以少写很多代码
    checked_span_v3 s(aa, 100);

    // 编译器非常清楚 s 是 checked_span_v3<int>，有 100 个元素
    // 我不需要再重复一遍 int 和 100
    s[0] = 42;
    std::cout << s[0] << "\n";  // 42

    // 配合 range-based for 循环，现代 C++ 的常规操作
    // 比以前写 for (int i = 0; i < 100; ++i) 舒服太多了
    std::size_t count = 0;
    for (auto& val : aa) {
        if (val != 0) ++count;
    }
    std::cout << "非零元素个数: " << count << "\n";  // 1

    return 0;
}
```

Type deduction may seem like "syntactic sugar," but after writing hundreds of lines of `span`-related code in a project, you'll realize that omitting an `int` isn't just about saving three characters. It means that when you eventually change `int` to `int64_t`, you only need to modify the definition in one place, instead of hunting down every instance where you might have missed it.

This represents a core philosophy of generic programming: don't repeat what the compiler already knows, and what you already know.

## Sub-spans and Construction from Pointers—A More Complete Toolbox

Having just a complete `span` isn't enough. In real-world development, we often need to slice a smaller `span` from a larger one, or construct a `span` from a raw pointer.

Let's first look at the scenario of constructing from a pointer. Since the purpose of `span` is safety, isn't constructing a `span` from a raw pointer inherently unsafe? Indeed, there is no way to verify whether that pointer actually points to that many elements—the compiler doesn't know, and runtime cannot validate it. However, the key point is: **constructing a `span` from a pointer stands out conspicuously during code reviews and in static analysis tools**. If a project's coding standard mandates that "all array access must go through `span`," then as soon as someone writes code like `span(ptr, n)`, the reviewer can spot it immediately: here is an unsafe boundary that requires scrutiny. This is much easier to manage than having `ptr[i]` scattered everywhere.

```cpp
#include <span>

// 从指针构造 span 的辅助函数
// 故意写成函数形式，让它在代码审查中更显眼
template<typename T>
std::span<T> make_span_from_ptr(T* ptr, std::size_t size) {
    return std::span<T>(ptr, size);
}

// 取前 n 个元素的子 span
template<typename T>
std::span<T> take_front(std::span<T> s, std::size_t n) {
    if (n > s.size()) {
        throw std::out_of_range("take_front: n 超过了 span 的大小");
    }
    return s.subspan(0, n);
}

// 取某个范围内的子 span
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

    // 从指针构造——代码审查时这行会特别显眼
    auto full = make_span_from_ptr(data, 10);

    // 取前 3 个
    auto front3 = take_front(full, 3);
    std::cout << "前3个: ";
    for (auto v : front3) std::cout << v << " ";
    std::cout << "\n";

    // 取下标 2 到 5 的范围（3 个元素）
    auto mid = take_range(full, 2, 3);
    std::cout << "中间3个: ";
    for (auto v : mid) std::cout << v << " ";
    std::cout << "\n";

    // 越界测试
    try {
        auto bad = take_front(full, 20);
    } catch (const std::out_of_range& e) {
        std::cout << "捕获: " << e.what() << "\n";
    }

    return 0;
}
```

Please provide the Chinese Markdown content you would like me to translate. You have included the instructions and the context, but the actual text to be translated is missing (indicated by "输出：" which likely means "Output:" or "Content to translate:").

Once you paste the Markdown content, I will translate it following your specified rules, ensuring technical accuracy and natural English flow.

```text
前3个: 10 20 30
中间3个: 30 40 50
捕获: take_front: n 超过了 span 的大小
```

Pay attention to how I perform the bounds check in `take_range`: `count > s.size() - offset`. I didn't use `offset + count > s.size()` because the latter can overflow when mixing signed and unsigned integers. Although `offset` and `count` are both `size_t` in this scenario and won't overflow, cultivating the habit of using subtraction instead of addition for range checks will save you trouble elsewhere. This aligns with the "use numbers, don't mix signed/unsigned" philosophy mentioned in the talk.

Similarly, we can add deduction guides to these helper functions, so callers don't need to write template parameters. It's just two lines of deduction guides, but the code reads completely differently—you see `take_front(full, 3)`, not `take_front<int>(full, 3)`. The compiler knows `full` is a `span<int>`, so it can deduce that the return value is also a `span<int>`. You don't need to worry about it for the compiler.

At this point, we have covered span's basic safe access, type deduction, and sub-span slicing. The code looks quite clean, without unnecessary repetition, and all necessary checks are in place. But we aren't done yet—there are more complex scenarios ahead.

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
    chapter="Cited by Stroustrup in CppCon 2025 talk Concept-Based Generic Programming in C++, §1"
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

- Stroustrup, B. ["A History of C++: 1979–1991"](https://www.stroustrup.com/hopl2.pdf). *HOPL-II*, 1993. — The authoritative record of the early history of the C++ language, covering the full context of template design decisions.
- Lourseyre, C. ["[History of C++] Templates: from C-style macros to concepts"](https://belaycpp.com/2021/10/01/history-of-c-templates-from-c-style-macros-to-concepts/). *Belay the C++*, 2021. — A high-quality secondary synthesis of Stroustrup's *D&E* Chapter 15, tracing the complete evolution from C macros to C++20 concepts.
- Stroustrup, B. *The Design and Evolution of C++. Addison-Wesley*, 1994. — The authoritative interpretation of C++ language design decisions; Chapter 15 specifically discusses the design motivation and trade-offs of templates.
