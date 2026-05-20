---
title: Type Safety, Number Constraints, and Bounds Checking
description: CppCon 2025 talk notes — from implicit narrowing conversions to the `Number<T>`
  wrapper type, then to `safe_int` and `checked_span`
conference: cppcon
conference_year: 2025
talk_title: Concept-based Generic Programming
speaker: Bjarne Stroustrup
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 20
- 23
chapter: 1
order: 1
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md
  source_hash: 707b662969a6dfc9b5be3d5075758f4a2e72ec44b6ac4d8a7e81ec88dd260f19
  translated_at: '2026-05-20T04:33:21.308852+00:00'
  engine: anthropic
  token_count: 8899
---
# From Manual Checks to Implicit Guards

:::tip
As a side note, this section is an extended discussion based on a CppCon talk. The link above points to their video series on YouTube; users in China can watch via the Bilibili link.
:::

Generic programming in C++ traces back to 1991 when templates were introduced to the language (C++ Release 3.0). Stroustrup's primary motivation for designing templates was to replace C preprocessor macros with type-safe generic containers. In *The Design and Evolution of C++*, he wrote that macros "fail to obey scope and type rules and don't interact well with tools," whereas templates were designed to be "as efficient as macros" but type-safe<RefLink :id="1" preview="Stroustrup, The Design and Evolution of C++, 1994, Ch.15" />.

But the story took an unexpected turn in 1994. At a C++ committee meeting, Erwin Unruh presented a perfectly legal C++ program that wouldn't even compile—yet the compiler's error messages output a sequence of prime numbers line by line<RefLink :id="2" preview="Unruh, Prime Number Computation, C++ 委员会会议, 1994" />. The entire committee suddenly realized that templates had inadvertently formed a Turing-complete compile-time computation system. The following year, Todd Veldhuizen published a paper systematically describing this technique and named it **Template Metaprogramming**<RefLink :id="3" preview="Veldhuizen, Using C++ Template Metaprograms, C++ Report, 1995" />. Templates thus evolved from a "type-safe macro replacement" into an indispensable compile-time abstraction mechanism in C++.

Template error messages often span hundreds of lines and are notoriously unreadable—this is why many C++ developers shy away from generic programming. But as project scale grows, code without generics becomes so repetitive that it's unmaintainable. In this article, we start from the foundational motivation of generic programming and work our way to a concrete, actionable type safety issue: implicit narrowing conversions.

The experimental environment for this article is Arch Linux WSL with GCC 16.1.1. Here is the environment info:

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


## First, let's clarify what generic programming is really trying to achieve

Saying that generic programming makes code more generic and abstract only tells half the story. Alex Stepanov (the father of the STL) pointed out that the goal of generic programming is "to express ideas in the most generic, most efficient, and most flexible way"—the key is expressing ideas, not abstracting for abstraction's sake. Treating the means as the end is a common pitfall in programming—another typical example being the overuse of design patterns.

This distinction matters. We don't start from some abstract model to design our code; instead, we start from concrete, efficient algorithms, discover the commonalities within them, and then extract those commonalities. And we can't sacrifice performance, because a large part of C++'s reason for existing lies right there. As hardware gets faster, our expectations for software are growing just as rapidly, while semiconductor processes seem to have hit a bottleneck. The room for writing careless code is shrinking.

Generic programming demands more from us: it requires us to discern reusable patterns within abstract domains. And its bottom line is this—after abstraction, performance must not degrade compared to hand-written concrete versions. Otherwise, there's no point in introducing generic programming. Writing code itself belongs to the "getting work done" layer of the needs hierarchy; we don't do extra things. If a particular piece of code won't be reused and is performance-sensitive, don't introduce generics there.

## Alex Stepanov's design criteria for C++

Around 1994, Stepanov proposed three design criteria<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" />: first, generality—a good generic component should be able to express use cases that even its designer hadn't thought of; second, uncompromising efficiency—system-level code written in C++ should match C's performance, and linear algebra code should match Fortran's; third, statically typed interfaces—catch errors at compile time, don't leave them for runtime. Later he added two very down-to-earth requirements: compile times shouldn't be so long that you go grab a coffee (header-only libraries make this hard to guarantee), and the learning curve shouldn't be so steep that you need an MIT PhD to get started<RefLink :id="5" preview="Nygaard, cited in Stroustrup, Concept-Based Generic Programming in C++, 2025, §1" />—as for whether C++ has actually achieved this, we all know the answer.

## Implicit narrowing conversions: a classic type safety trap

With the motivation out of the way, let's start with a concrete problem. The introduction of any concept must have a corresponding problem scenario, otherwise it's just a castle in the air. Look at this code:

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

This code uses pre-C++23 syntax to ensure it compiles directly on all compilers.

On my machine, the result is `overflow = -25536`, `int_pi = 3`. The compiler doesn't emit a single warning (unless you enable `-Wall -Wextra`, but many projects don't). This kind of bug is particularly insidious: the code runs, but the results are wrong, and it often doesn't surface with small data volumes—only blowing up after deployment.

Many people think "that's just how C++ is, just be careful." But relying on human carefulness for this kind of thing is unreliable. Bjarne Stroustrup himself has said that he wanted to fix this problem back in the day but couldn't, and the C camp wouldn't allow changes either. So as users, can we defend against it ourselves?

## Modeling "numbers" with C++20 concepts

C++20 gives us a new weapon: concepts. Their essence is simple—a concept is a compile-time evaluated Boolean predicate that takes a type as input and outputs true or false. In other words: it lets the compiler understand a "concept" without us having to describe it in complex natural language.

The standard library already defines some basic concepts, such as `std::integral` and `std::floating_point`, which determine whether a type is an integer type or a floating-point type. These aren't new inventions—the first edition of K&R C was already distinguishing between int and float, except now we have a language-level, compile-time queryable representation.

Let's first write the simplest concept to express the idea of a "number":

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

There's a syntax detail worth explaining here: `std::integral<T>` looks like a function call, but it isn't. `std::integral` is a concept, `<T>` instantiates it with type T, and the value of the entire expression is a compile-time bool. You can't write `std::integral(T)`—that syntax is wrong. Just understand it as "run the integral test on T," returning true or false.

Run the code above, and all four `static_assert` assertions pass, showing that our `number` concept basically works.

## Writing a narrowing check ourselves

Can we write a concept that determines "when assigning a value of type U to type T, will a narrowing conversion occur"? Since we're writing this article, let's give it a shot.

First, if T's representable range is smaller than U's, narrowing is obviously possible. For example, assigning `int` to `short`—`int` can represent far more values than `short`. But how do we determine "smaller range"? The C++ standard library doesn't directly give us a "range of a type" concept, but `<type_traits>` has `std::numeric_limits`, where we can look up the min and max of various types. If U is floating-point and T is an integer, the fractional part will definitely be lost, which is also narrowing.

There's another easily overlooked case: U and T are both integers of the same size (say both 32-bit), but one is signed and the other is unsigned—then assigning a negative number to an unsigned type will also cause problems. Let's write these rules as code:

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

Compile and run it, and all six `static_assert` assertions pass. We can use the last `!narrowing_assign<int, int>` to verify the logic: for same-type assignment, in case 1's `smaller_range<int, int>`, `max() < max()` is false and `min() > min()` is also false, so it doesn't trigger; case 2 requires U to be floating-point and T to be an integer, which isn't satisfied; case 3 requires different signedness, and `int` and `int` are obviously the same. All three branches are false, the overall result is false, and after negation `static_assert` passes—this perfectly matches our intuition that "same-type assignment doesn't narrow."

One more thing worth mentioning: where `&&` and `||` are mixed in `narrowing_assign`, parentheses are mandatory. Because `&&` has higher precedence than `||`, without parentheses, `number<T> && number<U>` would only constrain the first `||` branch, and the latter two branches might still be evaluated for non-number types—although the results happen to be correct for the current test cases, the semantics would be wrong. Adding parentheses makes the three branches a single unit, then uniformly constrained by `number<T> && number<U>`, making the logic rigorous.

## Some edge cases to think through

The implementation above covers most scenarios, but there are some details worth discussing. For example, conversions between floating-point types: does `double` to `float` count as narrowing? From a precision standpoint, of course it does, because `double` can represent more significant digits than `float`. But in the current implementation, `smaller_range<float, double>` would evaluate `numeric_limits<float>::max() < numeric_limits<double>::max()` as true, so it would be correctly identified as narrowing.

Another case is `char` to `unsigned char`. The signedness of `char` is implementation-defined (signed on some platforms, unsigned on others). If `char` is signed on your platform, then `signed_integral<char> != signed_integral<unsigned char>` is true, and it would be identified as narrowing. This is actually reasonable, because if `char` is -1, assigning it to `unsigned char` would become 255.

Note, however, that this implementation isn't 100% rigorous. The standard's definition of narrowing conversion (in C++11's list initialization rules) is more nuanced than what's written here—for instance, it also considers whether a floating-point-to-integer value falls within the integer's range. But as a starting point, this concept can already help us avoid most pitfalls. We can refine it gradually over time.

At this point, we can summarize one thing: concepts aren't some mysterious metaprogramming technique—they're simply a mechanism for "writing type constraints as compile-time checkable Boolean expressions." In the past, when writing templates, constraints relied entirely on documentation and naming conventions (like "please pass a random-access iterator"), and the compiler didn't enforce them. Pass the wrong type, and you'd get pages of incomprehensible errors. Now with concepts, the compiler can tell you immediately "the type you passed doesn't satisfy the requirements," and the error messages are actually human-readable.

The next step is to apply this `narrowing_assign` concept in actual functions to create a safe assignment wrapper—that's the content of the next section. At the very least, the core idea of "using concepts to express type constraints" is now clear.

---

# From Manual Checks to Implicit Guards: Baking Narrowing Conversion Checks into Types

In the previous section, we figured out the rules for determining narrowing conversions. If you had to run through those rules in your head every time you write code, it would be practically impossible—when signed and unsigned are mixed, which one is bigger, will it overflow, can the positive part be represented... just thinking about these is enough to make your head spin. The speaker mentioned that writing this out manually takes about a page of code, and it's messy and tricky.

So what this section needs to do is: turn that page of messy logic into actually runnable code, and then hide it away so that when you write code day to day, you don't even notice its existence.

## First, translate the checking logic into code

One intuition is: to determine whether assigning a value from type U to type T will cause narrowing, just use a `static_cast` and compare. But think carefully—that's not how it works at all. When signed and unsigned are mixed, the comparison itself has traps. So we need an honest, step-by-step function.

The idea is: do as much filtering as possible at compile time, directly eliminating those cases where narrowing "absolutely cannot happen," leaving only the paths that truly need runtime checks. This is exactly what generic programming has always emphasized—don't do at runtime what doesn't need to be done at runtime.

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

Looking back at this function after writing it, when signed and unsigned are mixed, how much can be eliminated at compile time versus what must be checked at runtime—that boundary really does require careful thought. There's an easy trap to fall into: simply using round-trip (convert there and back) to detect narrowing fails for signed→unsigned conversions—because `int(-1) → unsigned(4294967295) → int(-1)` is perfectly reversible under two's complement, so round-trip detection won't catch it. That's why you must explicitly check "is the source value negative" before the round-trip. `if constexpr` plays a key role here—branches that can be determined at compile time won't generate any code at all, so there won't be a bunch of useless comparison instructions.

## What to do when narrowing occurs? Throw an exception

With the checking logic in place, the next decision is: how do we handle it when narrowing is detected?

The speaker's approach is very direct—throw an exception. After compile-time filtering, the probability of narrowing actually triggering at runtime is extremely low. In most code, types match and get eliminated at compile time; among the remaining cases that need runtime checks, the vast majority won't actually overflow. It might trigger once in a million calls—this is exactly the scenario where exceptions excel: handling extremely rare exceptional situations.

```cpp
template<typename T, typename U>
constexpr T narrow_convert(U u) {
    if (would_narrow<T>(u)) {
        throw std::invalid_argument("narrowing conversion detected");
    }
    return static_cast<T>(u);
}
```

It's that simple. You can use it directly:

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

Run it and see the output:

```text
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
a = 42, b = 100
```

Great, everything that should be caught is caught. But here's the problem—you can't possibly write `narrow_convert<int>(xxx)` at every assignment site. The code would become verbose, and there's no way to maintain consistency. Relying on programmers to diligently add checks will inevitably lead to missed cases. Some places will have them, some will be forgotten, and bugs will hide in those forgotten places.

## Baking the check into a type: Number<T>

So the real solution is—make the checking implicit. Define a wrapper type `Number<T>` that automatically performs narrowing checks upon construction. After that, use `Number<T>` just like an ordinary `T`, without worrying about narrowing issues, because if construction can't pass, the object simply won't exist.

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

You see, the class itself is just this much code. It looks like demo code, but it actually works. Let's try it:

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

Output:

```text
x = 42, y = 3, z = 100
sum = 142
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
```

At this point, a key design philosophy becomes clear: we used to think of template metaprogramming and the type system as two separate things, but in reality, the type system itself is the best place to perform checks. You don't need to remember where to check and where not to check—just use `Number<T>` instead of `T`, and the check happens automatically. And because of the compile-time `if constexpr` branches, paths that don't need checking (like same-type assignment) won't even generate judgment code—zero overhead.

## But construction alone isn't enough; we need arithmetic

If a numeric type can only be constructed but can't do arithmetic, how is it different from a constant? So we need to add arithmetic operators to `Number<T>`. But there's a question: what should `Number<int>` plus `Number<double>` return? You can't just return some arbitrary type; there needs to be a rule.

There's something in the standard library called `std::common_type` that does exactly this—given two types, it tells you what type to use for their arithmetic result. For example, `common_type_t<int, double>` is `double`, and `common_type_t<int, unsigned int>` is `unsigned int` on most platforms. Let's just use it:

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

Let's run a slightly more complex example to verify:

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

Output:

```text
10 + 3.5 = 13.5
结果类型是 Number<double>? 1
3000000000u + 100 = 3000000100
加法溢出捕获到: narrowing conversion detected
```

:::warning Original text error correction: unsigned arithmetic overflow is not detected by narrow_convert
In the output above, the last line "addition overflow caught" will **not appear** in actual compilation and execution. Actual test results (GCC 16.1.1, C++20):

```text
Raw unsigned sum: 705032704
Would narrow? 0
No exception thrown! overflow = 705032704
```

The reason is: arithmetic on `unsigned int + unsigned int` in C++ is **wrapping** (well-defined wrapping), and the result of `3000000000u + 2000000000u` is `705032704`—a legal `unsigned int` value. Subsequently, `narrow_convert<unsigned int>(705032704u)` detects a same-type assignment, and `would_narrow` simply returns false, so the exception is never thrown.

This is a fundamental limitation of the current design of `Number<T>`: `narrow_convert` can only detect **narrowing conversions during assignment**, not **overflow of the arithmetic operation itself**. To detect overflow, you need to use compiler built-in functions (such as `__builtin_add_overflow`) or manual checks:

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

See `code/volumn_codes/vol10/cppcon/2025/01-concept-based-generic-programming/01-06-overflow-not-caught.cpp` for verification code.
:::

Looking at the last overflow detection example—we need to note that `narrow_convert` can only intercept **narrowing during type conversions**. It's powerless against overflow of same-type arithmetic operations (like the wrapping of `unsigned int + unsigned int`). `common_type_t<unsigned int, unsigned int>` is `unsigned int` itself, and the operation result has already wrapped into a legal value before being assigned to `Number<unsigned int>`. For complete arithmetic overflow defense, additional mechanisms are needed (like compiler built-in overflow checking functions), which falls outside the scope of `narrow_convert`'s responsibilities.

At this point, from manual checking rules, to runtime checking functions, to exception handling strategies, to wrapper types and arithmetic operations—this thread is finally tied together. The key is to understand these things as a complete narrowing defense system, not as isolated knowledge points.

---

# No Need to Reinvent the Wheel: Standard Library Function Objects + Eliminating Comparison Traps

To implement a safe integer type, the intuitive approach is to hand-write all the addition, subtraction, multiplication, division, and comparison operators—just thinking about it is exhausting. But in reality, the standard library has long provided function objects like `std::plus` and `std::multiplies`, each just a few lines of code, nothing like black magic. Of course, reinventing the wheel is a traditional C++ art form.

## First, let's see how to write the operators

A common misconception is that to overload `operator+` and `operator*` for a custom type, you need to write a bunch of `friend` functions either inside the class or globally, with each function handling various edge cases. But actually, you just need to use the standard library's function objects.

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

You'll notice the key point here: `std::plus<T>{}` is a function object, and when you call it, if an improper type conversion occurs (like mixing signed and unsigned), it gets intercepted by the rules we set up earlier. The operation logic itself doesn't need our attention—the standard library has already written it; we just handle "intercepting" and "letting through."

## Comparison operations: the worst offender for signed/unsigned mixing

Operator overloading itself isn't hard, but comparison operations are the real disaster zone for signed/unsigned mixing. Spending a whole afternoon debugging only to find that a single comparison was written wrong—this isn't uncommon.

Look at this code:

```cpp
#include <iostream>

int main() {
    int a = -1;
    unsigned int b = 2;

    std::cout << (a < b) << "\n";  // 你猜输出什么？
}
```

Run it, and the output is `0`, which is `false`. A negative number is less than a positive number, yet the result is false? Why? The answer lies in one of C++'s implicit conversion rules—when signed and unsigned are mixed in a comparison, the signed value is converted to unsigned. So `-1` becomes a huge number (`4294967295`), which of course isn't less than 2. This rule has existed since C was born in 1972; at the time it probably seemed fine, but over the decades it has buried who knows how many bugs.

As the speaker put it well: this rule should have been fixed in 1972, but by the time everyone realized how bad it was, there was already too much code in the world depending on this behavior—it couldn't be changed. To this day, we're still suffering for it.

## Fixing this comparison trap ourselves

Since built-in types aren't reliable, let's take over comparison operations in our safe_int. The approach is straightforward: if the two sides have different types (one signed, one unsigned), do special handling first; if the types are the same, just do a normal comparison.

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

There's a key point here: `operator<` is written as a **templated free function** rather than a class-internal `friend`. The reason is that a class-internal `friend bool operator<(const safe_int& a, const safe_int& b)` only accepts two `safe_int<T>` instances with the **same T**. But `safe_int<int> < safe_int<unsigned int>` is a comparison between two different template instances, and a class-internal friend simply can't match it. By writing it as a `template<typename T, typename U>` free function, the compiler can correctly match this operator between `safe_int<int>` and `safe_int<unsigned int>`. `if constexpr` lets the compiler optimize away branches that aren't taken—zero overhead. Equality comparison and greater-than comparison follow the same approach; just write them the same way.

Let's verify:

```cpp
int main() {
    safe_int<int> a{-1};
    safe_int<unsigned int> b{2};

    std::cout << (a < b) << "\n";  // 输出 1，终于正确了！
    // 注意：a 和 b 是不同模板实例 safe_int<int> 和 safe_int<unsigned int>，
    // 只有模板化的自由 operator< 才能匹配这个调用
}
```


## A bigger trap: range checks silently bypassed

Comparison operations are fixed, but there's an even more hidden scenario. The speaker gave an example with span—this pattern is extremely common in real code.

First, some background. `std::span` is essentially a "fat pointer"—a pointer to an element sequence plus the length of the sequence. This idea isn't new; Dennis Ritchie proposed adding boundary-carrying pointers to C as early as the early 1990s (for variable-length arrays), calling them fat pointers at the time, but the committee felt the runtime overhead was too large and didn't adopt them<RefLink :id="7" preview="Ritchie, Variable-Size Arrays in C, 1990" />. Now C++20 has finally added span, a vindication decades overdue—although span itself doesn't do bounds checking, it provides a foundation for upper-level safety wrappers.

So where's the problem? Look at this code:

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

`max_size` is `unsigned int`, with a value of 50. What happens with `50 - 500` under unsigned arithmetic? Underflow—it becomes a huge number (around `4294967296 - 450`). Then `subspan` receives this huge length—and `std::span::subspan` in C++20 does **not** have bounds checking; it only has a precondition (violating it is undefined behavior) and won't throw exceptions<RefLink :id="6" preview="cppreference, std::span::subspan, C++20" />. This means that huge number gets passed straight in, and the consequence is undefined behavior—it might read memory it shouldn't, it might happen not to crash, but you absolutely cannot rely on span to catch it for you.

All because of a tiny typo, all because of built-in type conversion rules, you completely lose the protection of range checking. Many people think span is safe enough, never expecting it to be bypassed at the parameter calculation layer.

## Using safe_int to give span real protection

Now that we have a safe_int that can intercept all erroneous conversions, can we make span's size parameters protected too? Of course we can.

My approach is: first define a concept representing "types that can be spanned," and then require within this concept that the size type must be a safe integer.

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

The key point is that the member variable `size_` has type `safe_int<std::size_t>` rather than a bare `std::size_t`. This means any operation on this size—subtraction, comparison, assignment—will go through our safety checks. If someone writes `50 - 500`, safe_int will report an error the moment the operation happens, rather than letting a huge number quietly flow into subspan. **We don't need to patch things up in span's bounds checking; we need to eliminate erroneous values from the source—integer arithmetic itself.** Looking back, the approach is actually quite simple: replace unsafe built-in integers with safe wrapper types, letting errors be caught the moment they occur, rather than waiting for them to propagate to some bounds check before being discovered. In other words—let the class that should truly be responsible handle the corresponding errors, rather than relying on other components as a safety net.

---

# Adding Bounds Checking to Span: From Manual Defense to Type Deduction

Array out-of-bounds access has always been a headache: it's fast when it runs, but once it goes out of bounds, the program might crash in some completely unrelated place, and then you stare at gdb for half an hour. Next, let's look at a structured approach to bounds checking for subscript access.

## First, let's clarify what we're trying to do

The core need is actually quite simple: I have a contiguous memory region, I know how big it is, and I want to automatically check whether a subscript is out of bounds every time I use it to access the region. If it's out of bounds, throw an exception immediately or get blocked by the compiler, rather than waiting until memory is corrupted before I notice.

Doesn't this sound like what `std::vector`'s `at()` does? But the difference is, I don't want to bear the cost of a dynamically allocated vector—I might just have a raw pointer plus a length, or a native array, and I want to access it in the same safe way. That's the whole point of span—it doesn't own the data, it just "views" the data, but while viewing, it can watch the boundaries for you.

## Writing a checked subscript access

Let's start with the most basic scenario. Suppose I already have something of span type that internally holds data and size. What I need to do now is overload `operator[]` so that it performs a range check before executing the access.

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

You see, the constructor here only accepts a pointer and a size—this is what we call "spanable"—anything that can provide a data pointer and element count can be used to initialize it. Then `operator[]` does one thing: if the index you give is greater than or equal to size, throw an exception directly.

## Run it and see the effect

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

When you run it, the output looks like this:

```text
3
捕获到异常: 下标越界了兄弟
```

At this point you might think, there's nothing special about this—doesn't `std::vector::at()` already do this? Don't rush, the key points are coming up.

## The negative subscript problem—the signed/unsigned trap

There's an easily overlooked trap here. `operator[]` accepts a parameter of type `std::size_t`, which is an unsigned integer. If you directly pass a `-10` in, what happens?

```cpp
// 你以为你在传 -10，其实编译器会做隐式转换
// -10 作为无符号整数会变成一个巨大的正数
// s[-10] 实际上变成了 s[18446744073709551606] 之类的鬼东西
```

But! If you change the parameter type to a signed `ptrdiff_t`, the compiler can help you catch some obvious problems at compile time. Or rather, if you use `std::span`'s standard implementation, it has specific requirements for the subscript type.

Let me rewrite it, changing the subscript type to signed so that negative numbers can be correctly identified:

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

Output:

```text
捕获到异常: 负数下标，你想干嘛
```

What's worth noting here is that when using `size_t` as the subscript type, a negative number passed in gets implicitly converted to an astronomical figure, and then either it happens not to go out of bounds and reads garbage data (which is scarier), or it goes out of bounds and throws an exception but with a completely misleading error message. After changing to `ptrdiff_t`, a negative number is just a negative number—clear and unambiguous.

However, the compiler can only catch the simplest cases like literal negative numbers. In real engineering, the problems that actually occur are often values calculated elsewhere—some function returns a -1 to indicate failure, you forget to check it and use it directly as a subscript. This can only be caught at runtime, but at least with this check, the program won't silently corrupt memory.

## Using another span's element as a size—a more realistic scenario

The speaker mentioned a very practical example: you use an element value from one span as a size parameter for another operation. You don't actually know what that value is, but unless it's a reasonable positive integer, it should be intercepted.

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

Output:

```text
前 3 个元素的和: 6
捕获到异常: params[0] 不是合法的正整数
```

This pattern is especially common in real projects. You get a number from a config file, a network protocol, or user input, and then use it to decide how many elements to access. Without checking, this is a perfect security vulnerability.

## Type deduction: stop repeating what the compiler already knows

At this point, every time we have to write `checked_span<int>` and `checked_span<double>`, repeating the element type, even though the compiler can deduce it from the initialization arguments. This is exactly the problem that C++17's CTAD (Class Template Argument Deduction) was introduced to solve. Just add a deduction guide:

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

Now writing it is much cleaner:

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

Type deduction might seem like "syntactic sugar," but after writing hundreds of span-related lines in a project, you'll find that omitting one `int` isn't about saving three characters—it's about when you later change `int` to `int64_t`, you only need to change it in one place, instead of searching everywhere for where you forgot to update it.

This is a core philosophy of generic programming: don't repeat what the compiler already knows and what you already know.

## Sub-spans and construction from pointers—a more complete toolbox

Having just one complete span isn't enough. In real development, you often need to slice a small piece from a large span, or construct a span from a raw pointer.

First, the scenario of constructing from a pointer. Since the whole point of span is safety, isn't constructing a span from a raw pointer inherently unsafe? There's indeed no way to verify whether that pointer really points to that many elements—the compiler doesn't know, and runtime can't verify it either. But the key insight is: **constructing a span from a pointer itself will look extremely conspicuous during code review and to static analysis tools**. If a project's standards require "all array access must go through span," then as soon as someone writes `span(ptr, n)`, the reviewer can spot it at a glance: here is an unsafe boundary that needs close attention. This is much easier to manage than raw `ptr[i]` scattered everywhere.

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

Output:

```text
前3个: 10 20 30 
中间3个: 30 40 50 
捕获: take_front: n 超过了 span 的大小
```

Note how I write the bounds check in `take_range`: `count > s.size() - offset`. I didn't use `offset + count > s.size()` here because the latter could overflow when signed and unsigned are mixed. Although in this scenario both `offset` and `count` are `size_t` and won't overflow, developing the habit of using subtraction rather than addition for range checks will save you from pitfalls elsewhere. This also aligns with the speaker's idea of "using numbers rather than mixing signed and unsigned."

Similarly, these helper functions can have deduction guides added so that call sites don't need to write template parameters. It's just two lines of deduction guides, but the code reads completely differently—you see `take_front(full, 3)`, not `take_front<int>(full, 3)`. The compiler knows `full` is `span<int>`, so it can deduce that the return value is also `span<int>`; you don't need to worry about it for the compiler.

At this point, we've covered span's basic safe access, type deduction, and sub-span slicing. The code looks quite clean, with no unnecessary repetition, and checks are in place wherever needed. But we're not done yet—there are more complex scenarios ahead.

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

- Stroustrup, B. ["A History of C++: 1979–1991"](https://www.stroustrup.com/hopl2.pdf). *HOPL-II*, 1993. — The authoritative record of C++'s early history, covering the full context of template design decisions.
- Lourseyre, C. ["[History of C++] Templates: from C-style macros to concepts"](https://belaycpp.com/2021/10/01/history-of-c-templates-from-c-style-macros-to-concepts/). *Belay the C++*, 2021. — An excellent secondary synthesis of Chapter 15 of Stroustrup's *D&E*, tracing the complete evolution from C macros to C++20 concepts.
- Stroustrup, B. *The Design and Evolution of C++*. Addison-Wesley, 1994. — The authoritative interpretation of C++ language design decisions, with Chapter 15 specifically discussing the motivation and trade-offs behind templates.
