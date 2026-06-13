---
title: Type Safety, Number Constraints, and Bounds Checking
description: CppCon 2025 Talk Notes — From implicit narrowing conversions to Number<T>
  wrappers, and then to safe_int and checked_span
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
  source_hash: 1aad64ff7c3d5c3b94fb383a5778e0a13f491a51c687b3e2836c07f1ad7a9ceb
  translated_at: '2026-06-13T11:45:25.430298+00:00'
  engine: anthropic
  token_count: 8929
---
# From Manual Checks to Implicit Guards

:::tip
A quick note: this section is an expansion based on CppCon talks. The links above point to their video series on YouTube; users in China can watch via the Bilibili links.
:::

Generic programming in C++ dates back to 1991 when templates were introduced to the language (C++ Release 3.0). Stroustrup's primary motivation for designing templates was to replace C preprocessor macros to implement type-safe generic containers. In *The Design and Evolution of C++*, he wrote that macros "fail to obey scope and type rules and don't interact well with tools," whereas templates were designed to be "as efficient as macros" but type safe<RefLink :id="1" preview="Stroustrup, The Design and Evolution of C++, 1994, Ch.15" />.

But the story took an unexpected turn in 1994. Erwin Unruh presented a legal C++ program at a C++ committee meeting that wouldn't even compile, yet the compiler output a sequence of prime numbers line by line in the error messages<RefLink :id="2" preview="Unruh, Prime Number Computation, C++ 委员会会议, 1994" />. The entire committee then realized that templates had inadvertently constituted a Turing-complete compile-time computation system. The following year, Todd Veldhuizen published a paper systematically describing this technique, naming it **Template Metaprogramming**<RefLink :id="3" preview="Veldhuizen, Using C++ Template Metaprograms, C++ Report, 1995" />. Thus, templates evolved from a "type-safe macro replacement" to an indispensable compile-time abstraction mechanism in C++.

Template error messages often span hundreds of lines and are notoriously unreadable—this is why many C++ developers shy away from generic programming. However, as project scale grows, code without generics becomes too repetitive to maintain. In this article, we start from the basic motivations of generic programming and arrive at a concrete, actionable type safety issue—implicit narrowing conversion.

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


## First, let's clarify what generic programming aims to do

The effect of generic programming is to make code more general and more abstract—this is only half right. Alex Stepanov (father of the STL) pointed out that the goal of generic programming is to "express ideas in the most general, most efficient, and most flexible way." The key is expressing ideas, not abstraction for abstraction's sake. Treating means as ends is a common pitfall in programming—another typical example is the abuse of design patterns.

This distinction is important. We don't design code starting from an abstract model; instead, we start from concrete, efficient algorithms, discover commonalities, and then extract them. Moreover, performance cannot be sacrificed, as a significant part of C++'s existence relies on this. As hardware gets stronger, our expectations for software expand rapidly, yet semiconductor processes seem to have hit a bottleneck, leaving less and less room for sloppy code.

Generic programming demands more from us: it requires us to perceive reusable patterns in abstract domains. Its bottom line is—after abstraction, performance must not be worse than a hand-written concrete version. Otherwise, there is no point in introducing generic programming. Writing code itself belongs to the "getting the job done" layer of the需求 hierarchy; do not do extra work. If a certain part won't be reused and is sensitive to performance, don't introduce generics.

## Alex Stepanov's C++ Design Standards

Around 1994, Stepanov proposed three design standards<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" />: first is generality, where good generic components should express usages even the designer hadn't thought of; second is uncompromised efficiency, where writing system-level code in C++ should match C, and writing linear algebra should match Fortran; third is statically typed interfaces, where checks happen at compile time, not leaving errors to runtime. Later, he added two very practical requirements: compile time shouldn't be so long that one goes for coffee (header-only libraries find this hard to guarantee), and the learning curve shouldn't be so steep that it requires a MIT PhD to get started<RefLink :id="5" preview="Nygaard, cited in Stroustrup, Concept-Based Generic Programming in C++, 2025, §1" />—as for whether C++ achieved this, we all have our own thoughts.

## Implicit Narrowing Conversion: A Classic Type Safety Trap

With the motivation covered, let's start with a specific problem. The introduction of a concept must have a corresponding problem scenario, otherwise it's a castle in the air. Look at this code:

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

This code uses C++23 syntax to ensure all compilers can compile it directly.

On my machine, the result is `overflow = -25536`, `int_pi = 3`. The compiler doesn't give a single warning (unless you enable `-Wall -Wextra`, but many projects don't). This kind of bug is particularly insidious: the code runs, but the result is wrong, and it often doesn't reveal itself with small data sets, only surfacing after deployment.

Many people think "this is just a C++ feature, just be careful." But relying on human diligence is unreliable. Bjarne Stroustrup himself said he wanted to solve this problem back then but couldn't, and the C language camp wouldn't budge. So as users, can we defend against it ourselves?

## Using C++20 Concepts to Model "Numbers"

C++20 gives us a new weapon: concepts. Its essence is simple—a concept is a compile-time evaluated boolean predicate, taking a type as input and outputting true or false. Put another way: it lets the compiler understand a "concept" without us needing to describe it in complex natural language.

The standard library already defines some basic concepts, such as `std::integral` and `std::floating_point`, which judge whether a type is an integer type or a floating-point type. These aren't new inventions; the first edition of K&R C distinguished int and float, but now we have a language-level, compile-time queryable representation.

Let's first write a simple concept to express the idea of a "number":

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

There is a syntactic detail worth explaining here: `std::integral<T>` looks like a function call, but it isn't. `std::integral` is a concept, `<T>` instantiates it with type T, and the value of the entire expression is a compile-time bool. You cannot write `std::integral(T)`, that syntax is wrong. Just understand it as "perform the integral test on T", returning true or false.

Running the code above, all four `static_assert` assertions pass, indicating our `number` concept basically works.

## Writing a narrowing Judgment by Hand

Can we write a concept to judge "when assigning a value of type U to type T, will a narrowing conversion occur"? Since I'm writing this article.

First, if T's representable range is smaller than U's, narrowing is obviously possible. For example, assigning `int` to `short`, `int` can represent many more values than `short`. But how do we judge "smaller range"? The C++ standard library doesn't directly give us a concept like "range of a type", but `<type_traits>` has `std::numeric_limits`, which can query the min and max of various types. If U is floating-point and T is an integer, the fractional part will definitely be lost, which is also narrowing.

There is another easily overlooked situation: U and T are both integers, the size is the same (e.g., both 32-bit), but one is signed and the other is unsigned. Assigning a negative number to an unsigned type will cause problems. Writing these rules into code:

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

Compile and run, all six `static_assert` assertions pass. We can use the last `!narrowing_assign<int, int>` to verify the logic: assigning the same type, in case 1, `smaller_range<int, int>` `max() < max()` is false, `min() > min()` is also false, so it doesn't trigger; case 2 requires U to be floating and T to be integer, which isn't satisfied; case 3 requires different signedness, `int` and `int` are obviously the same. All three branches are false, the whole thing is false, and after negation `static_assert` passes—this matches our intuition that "same type assignment isn't narrowing".

Another point worth mentioning: where `&&` and `||` are mixed in `narrowing_assign`, parentheses must be added. Because `&&` has higher precedence than `||`, without parentheses, `number<T> && number<U>` would only constrain the first `||` branch, and the latter two branches might be evaluated on non-number types—although the result happens to be correct for current test cases, semantically it's wrong. Adding parentheses makes the three branches a whole, then uniformly constrained by `number<T> && number<U>`, making the logic rigorous.

## Some Edge Cases Need to Be Clear

The implementation above covers most scenarios, but there are details worth mentioning. For example, conversion between floating-point numbers: `double` to `float`, does it count as narrowing? From a precision perspective, of course, because `double` can represent more significant digits than `float`. But in the current implementation, `smaller_range<float, double>` will judge `numeric_limits<float>::max() < numeric_limits<double>::max()`, which is true, so it will be correctly identified as narrowing.

Another example is `char` to `unsigned char`. The signedness of `char` is implementation-defined (signed on some platforms, unsigned on others). If `char` is signed on the platform, then `signed_integral<char> != signed_integral<unsigned char>` is true, and it will be identified as narrowing. This is actually reasonable, because if `char` is -1, assigning it to `unsigned char` becomes 255.

However, note that this implementation isn't 100% rigorous. The standard's definition of narrowing conversion (in the C++11 list initialization rules) is more detailed than what's written here, for example, considering whether the value is within the integer range when converting from floating-point to integer. But as a starting point, this concept can already block most pitfalls. We can improve it gradually.

At this point, we can summarize one thing: concepts aren't some profound metaprogramming trick, they are just a mechanism to "write constraints on types as compile-time checkable boolean expressions". Previously, writing templates meant relying on documentation and naming conventions (e.g., "please pass a random access iterator") for constraints, the compiler didn't care, and if you passed the wrong thing, you got a pile of gibberish. Now with concepts, the compiler can tell you "the type you passed doesn't meet the requirements" immediately, and the error message is human-readable.

The next step is to apply this `narrowing_assign` concept to actual functions to make a safe assignment wrapper—that's the content of the next section. At least the core idea of "using concepts to express type constraints" is sorted out here.

---

# From Manual Checks to Implicit Guards: Stuffing Narrowing Checks into Types

In the previous section, we figured out the rules for judging narrowing conversion. It's almost impossible to run those rules through your head every time you write code—when signed and unsigned are mixed, which one is bigger, will it overflow, can the positive part be represented, just thinking about these is dizzying. The speaker said writing this out by hand takes about a page of paper, and it's messy and tricky.

So the task for this section is: turn that page of messy logic into real running code, and then hide it so you don't feel its existence when writing code normally.

## First, Translate the Judgment Logic into Code

An intuition is: to judge whether assigning a value from type U to type T will cause narrowing, just use a `static_cast` and compare. But thinking carefully, it's not that simple at all—when signed and unsigned are mixed, the comparison itself has traps. So we need an honest, step-by-step function.

The idea is: do as much exclusion work as possible at compile time, filtering out those situations where "narrowing absolutely cannot happen", leaving only the paths that truly need runtime checks. This is actually what generic programming emphasizes—don't do work at runtime that shouldn't be done.

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

Looking back at this function, the boundary between how much can be excluded at compile time and how much must be checked at runtime when signed and unsigned are mixed really needs careful thought. There's a pitfall easy to step into: simply using round-trip (convert there and back) to detect narrowing fails during signed→unsigned conversion—because `int(-1) → unsigned(4294967295) → int(-1)` is completely reversible in two's complement, round-trip can't detect it. So you must explicitly check "is the source value negative" before the round-trip. `if constexpr` plays a key role here—branches determined at compile time won't generate code at all, so there won't be a bunch of useless comparison instructions.

## What to do when narrowing happens? Throw an Exception

With the judgment logic, the next decision is: how to handle it after detecting narrowing?

The speaker's solution is very direct—throw an exception. After compile-time filtering, the probability of narrowing actually triggering at runtime is extremely low. In most code, types match, and they are excluded at compile time; for those remaining that need runtime checks, the vast majority won't actually overflow. Maybe it triggers once in a million calls, which is exactly the scenario exceptions excel at—handling extremely rare exceptional situations.

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

Great, everything that should be blocked was blocked. But the problem arises—you can't write `narrow_convert<int>(xxx)` at every assignment location. The code becomes verbose, and it's completely impossible to maintain consistency. Relying on programmers to consciously add checks will inevitably result in漏网之鱼. Some places have them, some are forgotten, and bugs hide in those forgotten places.

## Stuffing the Check into the Type: Number<T>

So the real solution is—make the check implicit. Define a wrapper type `Number<T>` that automatically performs narrowing checks when constructed. After that, this `Number<T>` is used just like a normal `T`, but you don't worry about narrowing problems, because if the construction doesn't pass, this object doesn't exist at all.

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

You see, this class itself has just this much stuff. It looks like demo code, but it really works. Let's try:

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

At this point, a key design idea emerges: we used to think template metaprogramming and type systems were different things, but in fact, the type system itself is the best place to do checks. No need to remember where to check and where not to, just use `Number<T>` instead of `T`, and the check happens automatically. And because of the compile-time `if constexpr` branch, paths that don't need checking (like same-type assignment) won't even generate judgment code—zero overhead.

## But Being Able to Construct Isn't Enough, It Needs Arithmetic

If a numeric type can only be constructed but not calculated, what's the difference between it and a constant? So we need to add arithmetic operators to `Number<T>`. But there's a problem here: `Number<int>` plus `Number<double>` should return what? You can't just return a type, you need rules.

There's a thing in the standard library called `std::common_type`, which does exactly this—given two types, telling you what type to use when doing arithmetic operations on them. For example, `common_type_t<int, double>` is `double`, `common_type_t<int, unsigned int>` is `unsigned int` on most platforms. We use it directly:

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

:::warning Original Text Error Correction: unsigned arithmetic overflow won't be detected by narrow_convert
In the output above, the last line "addition overflow caught" will **not appear** in actual compilation and running. Actual test result (GCC 16.1.1, C++20):

```text
Raw unsigned sum: 705032704
Would narrow? 0
No exception thrown! overflow = 705032704
```

The reason is: arithmetic operations of `unsigned int + unsigned int` in C++ are **wrapping** (well-defined wrapping), the result of `3000000000u + 2000000000u` is `705032704`—a legal `unsigned int` value. Subsequently, `narrow_convert<unsigned int>(705032704u)` detects same-type assignment, `would_narrow` directly returns false, and the exception isn't thrown at all.

This is a fundamental limitation of the current `Number<T>` design: `narrow_convert` can only detect **narrowing conversions during assignment**, not **overflow of the arithmetic operation itself**. To detect overflow, you need to use compiler built-ins (like `__builtin_add_overflow`) or manual checks:

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

See verification code in [01-06-overflow-not-caught.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/01-concept-based-generic-programming/01-06-overflow-not-caught.cpp).
:::

Looking at the last overflow capture example—we need to note that `narrow_convert` can only intercept narrowing **during type conversion**, it is powerless against overflow of the same-type arithmetic operation itself (like the wrapping of `unsigned int + unsigned int`). `common_type_t<unsigned int, unsigned int>` is just `unsigned int` itself, the operation result has already wrapped into a legal value before being assigned to `Number<unsigned int>`. To fully defend against arithmetic overflow, additional mechanisms are needed (like compiler built-in overflow check functions), which is outside the scope of `narrow_convert`.

At this point, from manual judgment rules, to runtime check functions, to exception handling strategies, to wrapper types and arithmetic operations, this line is finally connected. The key is to understand these things as a complete narrowing defense system, not isolated knowledge points.

---

# Don't Reinvent the Wheel: Standard Library Function Objects + Eliminating Comparison Traps

To implement a safe integer type, intuitively you have to write addition, subtraction, multiplication, division, and comparison operations all by hand—just thinking about it is a headache. But actually, the standard library has long prepared `std::plus`, `std::multiplies` and other function objects, each just a few lines of code, not black magic at all. Of course, reinventing the wheel counts as a traditional C++ art form.

## First, See How to Write Operators

A common misconception is: to overload `operator+`, `operator*` for a custom type, you have to write a bunch of `friend` functions inside or outside the class, handling various boundary conditions in each function. But actually, you just need to use the function objects from the standard library.

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

You will find the key here is: `std::plus<T>{}` is a function object, and when you call it, if an inappropriate type conversion happens (like mixing signed and unsigned), it will be blocked by the rules we set up earlier. The operation logic itself doesn't need worry, the standard library has already written it, we just handle "intercept" and "let pass".

## Comparison Operations: The Heavy Disaster Area for Signed/Unsigned Mixing

Operator overloading itself isn't hard, but comparison operations are the real heavy disaster area for signed/unsigned mixing. Debugging a bug for a whole afternoon, only to find it was a wrong comparison line—this isn't uncommon.

Look at this code:

```cpp
#include <iostream>

int main() {
    int a = -1;
    unsigned int b = 2;

    std::cout << (a < b) << "\n";  // 你猜输出什么？
}
```

Run it, the output is `0`, which is `false`. Negative less than positive, but the result is actually false? Why? The answer is that C++'s implicit conversion rules have a rule—when signed and unsigned are mixed for comparison, the signed number is converted to an unsigned number. So `-1` becomes a huge number (`4294967295`), of course it's not less than 2. This rule has existed since C was born in 1972, maybe it seemed fine at the time, but over decades who knows how many bugs it buried.

The speaker said it well: this rule should have been corrected in 1972, but by the time everyone realized how bad it was, there was too much code in the world relying on this behavior, and it couldn't be changed. To this day, we are still suffering for it.

## Fixing the Comparison Trap by Hand

Since built-in types aren't reliable, let's take over comparison operations in our safe_int. The idea is straightforward: if the types on both sides are different (one signed, one unsigned), do a special judgment first; if types are the same, go directly to normal comparison.

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

Here is a key point: `operator<` is written as a **templated free function** rather than a class member `friend`. The reason is that the class member `friend bool operator<(const safe_int& a, const safe_int& b)` only accepts two `safe_int<T>` with the **same T**. And `safe_int<int> < safe_int<unsigned int>` is a comparison between two different template instances, the class friend can't match it at all. After writing it as a `template<typename T, typename U>` free function, the compiler can correctly match this operator between `safe_int<int>` and `safe_int<unsigned int>`. `if constexpr` lets the compiler optimize away branches it doesn't take, zero overhead. Equality comparison, greater-than comparison follow the same idea, just write them accordingly.

Verify:

```cpp
int main() {
    safe_int<int> a{-1};
    safe_int<unsigned int> b{2};

    std::cout << (a < b) << "\n";  // 输出 1，终于正确了！
    // 注意：a 和 b 是不同模板实例 safe_int<int> 和 safe_int<unsigned int>，
    // 只有模板化的自由 operator< 才能匹配这个调用
}
```


## A Bigger Pit: Range Checks Silently Bypassed

Comparison operations are fixed, but there is a more hidden scenario. The speaker gave a span example—this pattern is very common in actual code.

First, background. `std::span` is essentially a "fat pointer"—a pointer to a sequence of elements plus the length of the sequence. This idea isn't new, Dennis Ritchie proposed adding boundary-carrying pointers to C (for variable-length arrays) as early as the early 1990s, calling them fat pointers, but the committee felt the runtime overhead was too high and didn't adopt it<RefLink :id="7" preview="Ritchie, Variable-Size Arrays in C, 1990" />. Now C++20 finally added span,算是 a vindication decades late—although span itself doesn't do boundary checks, it provides the foundation for upper-level safety wrappers.

Where is the problem? Look at this code:

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

`max_size` is `unsigned int`, the value is 50. What happens when `50 - 500` is calculated under unsigned arithmetic? Underflow, becoming a huge number (around `4294967296 - 450`). Then `subspan` gets this huge length—and `std::span::subspan` in C++20 **has no** boundary check, it only has a precondition (violation is undefined behavior), it won't throw an exception<RefLink :id="6" preview="cppreference, std::span::subspan, C++20" />. This means that huge number is passed directly in, the consequence is undefined behavior—it might read memory it shouldn't, might not crash, but you can't rely on span to stop it.

Just because of a small slip, just because of built-in type conversion rules, you completely lose the protection of range checks. Many people think span is safe enough,没想到 it was bypassed at the parameter calculation layer.

## Using safe_int to Give Span Real Protection

Now we have a safe_int that can intercept all wrong conversions, can we make span's size parameter protected too? Of course.

My idea is: first define a concept representing "types that can be spanned", then require in this concept that the size type must be a safe integer.

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

The key point is that the member variable `size_` is of type `safe_int<std::size_t>` rather than a bare `std::size_t`. This means any operation on this size—subtraction, comparison, assignment—will go through our safety check. If someone writes `50 - 500`, safe_int will report an error at the moment of operation, rather than letting a huge number quietly flow into subspan. **We don't need to remedy this in span's boundary check, we need to eliminate the generation of wrong values at the source—the integer operation itself.** Looking back, the idea is actually simple: replace unsafe built-in integers with safe wrapper types, so errors are caught the moment they happen, not waiting for them to propagate to some boundary check. In other words—let the class truly responsible for handling handle the corresponding error, rather than letting other components cover for you.

---

# Adding Boundary Checks to Span: From Manual Defense to Type Deduction

The problem of array out-of-bounds has always been a headache: it runs fast, but once it goes out of bounds, the program might crash in a completely unrelated place, and then you stare at gdb for half an hour. Next, let's look at a structured index out-of-bounds checking method.

## First, Clarify What We Want to Do

The core requirement is actually very simple: I have a contiguous memory area, I know how big it is, I want to automatically check if the index is out of bounds every time I access it with an index. If it's out of bounds, throw an exception immediately or be blocked by the compiler, rather than waiting for me to discover it after memory is corrupted.

Doesn't this sound like what `std::vector`'s `at()` does? But the difference is, I don't want to bear the overhead of a dynamically allocated vector, I might just have a raw pointer plus a length, or a native array, and I want to access it in the same safe way. This is the meaning of span—it doesn't own the data, it just "looks" at the data, but when looking, it can help you watch the boundaries.

## Write a Checked Index Access by Hand

Let's start with the most basic scenario. Suppose I already have a span-like thing, it holds data and size internally. What I need to do now is overload `operator[]` to make it check the range before executing the access.

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

You see, the constructor here only accepts a pointer and a size, this is so-called "spanable"—anything that can provide a data pointer and element count can be used to initialize it. Then inside `operator[]`, one thing is done: if the index you give is greater than or equal to size, throw an exception directly.

## Run It and See the Effect

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

Running it outputs this:

```text
3
捕获到异常: 下标越界了兄弟
```

At this point, you might think, this isn't special, `std::vector::at()` is just like this. Don't worry, the key point is later.

## The Problem of Negative Indices—The Signed/Unsigned Pit

There is an easily overlooked trap here. `operator[]` accepts a parameter of type `std::size_t`, which is an unsigned integer. If you pass a `-10` directly, what happens?

```cpp
// 你以为你在传 -10，其实编译器会做隐式转换
// -10 作为无符号整数会变成一个巨大的正数
// s[-10] 实际上变成了 s[18446744073709551606] 之类的鬼东西
```

But! If you change the parameter type to signed `ptrdiff_t`, the compiler can help you block some obvious problems at compile time. Or, if you use the standard implementation of `std::span`, it has specific requirements for the index type.

Let me change the writing to make the index type signed, so negative numbers can be correctly identified:

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

Here it's worth noting that when using `size_t` as the index type, a negative number passed in is directly implicitly converted to an astronomical number, then either it luckily doesn't go out of bounds and reads garbage data (scarier), or it goes out of bounds and throws an exception but the error message is completely misleading. After changing to `ptrdiff_t`, a negative number is just a negative number, clear and simple.

However, the compiler can only block the simplest cases like literal negative numbers. In actual engineering, the real problems are often values calculated elsewhere—some function returns a -1 to indicate failure, someone forgets to check and uses it as an index. This can only be caught at runtime, but at least with this check, the program won't silently corrupt memory.

## Using Another Span's Element as Size—A More Realistic Scenario

The speaker mentioned a very practical example: you use a value from one span as the size parameter for another operation. You don't actually know what that value is, but unless it's a reasonable positive integer, it should be blocked.

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

This kind of writing is particularly common in real projects. You get a number from a config file, network protocol, user input, and then use it to decide how many elements to access. Without checking, this is a perfect security vulnerability.

## Type Deduction: Don't Repeat What the Compiler Already Knows

At this point, every time you have to write `checked_span<int>`, `checked_span<double>` repeating the element type, while the compiler can obviously deduce it from the initialization parameters. This is the problem that C++17's CTAD (Class Template Argument Deduction) aims to solve. Just add a deduction guide:

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

Type deduction seems like "syntactic sugar", but after writing hundreds of span-related codes in a project, you'll find that writing one less `int` isn't about saving three characters, but when you change `int` to `int64_t` later, you only need to change one place,而不是 looking all over the world for where you missed writing.

This is a core philosophy of generic programming: don't repeat what the compiler already knows and you already know.

## Subspan and Construction from Pointers—A More Complete Toolbox

Just a complete span isn't enough. In actual development, you often need to cut a small piece from a large span, or construct a span from a raw pointer.

First, the scenario of constructing from a pointer. Since the meaning of span is safety, isn't constructing a span from a raw pointer inherently Unsafe? Indeed, there's no way to check whether that pointer really points to that many elements—the compiler doesn't know, and runtime can't verify it either. But the key is: **constructing a span from a pointer itself will appear extremely abrupt in code reviews and static analysis tools**. If a project standard requires "all array access must go through span", then writing `span(ptr, n)` code, the reviewer can see at a glance: here is an unsafe boundary, need to watch closely. This is much easier to manage than having `ptr[i]` everywhere.

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

Note the way I wrote the boundary check in `take_range`: `count > s.size() - offset`. I didn't use `offset + count > s.size()` here because the latter might overflow when signed and unsigned are mixed. Although in this scenario `offset` and `count` are both `size_t` and won't overflow, developing the habit of using subtraction rather than addition for range checks can save you from pitfalls elsewhere. This is also the idea mentioned in the speech of "using numbers rather than mixing signed and unsigned".

Similarly, these helper functions can also add deduction guides, so the call site doesn't need to write template parameters. Two lines of deduction guides, but the code reads completely differently—you see `take_front(full, 3)`, not `take_front<int>(full, 3)`. The compiler knows `full` is `span<int>`, it can deduce the return value is also `span<int>`, you don't need to worry for it.

At this point, span's basic safe access, type deduction, and subspan slicing are all figured out. The code looks quite clean, no redundant repetition, and checks where needed. But things aren't over—there are even more complex scenarios later.

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
    chapter="Stroustrup cited in CppCon 2025 talk Concept-Based Generic Programming in C++, §1"
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

- Stroustrup, B. ["A History of C++: 1979–1991"](https://www.stroustrup.com/hopl2.pdf). *HOPL-II*, 1993. — Authoritative record of the early history of the C++ language, covering the full context of template design decisions.
- Lourseyre, C. ["[History of C++] Templates: from C-style macros to concepts"](https://belaycpp.com/2021/10/01/history-of-c-templates-from-c-style-macros-to-concepts/). *Belay the C++*, 2021. — High-quality secondary整理 of Stroustrup's *D&E* Chapter 15,梳理ing the complete evolution from C macros to C++20 concepts.
- Stroustrup, B. *The Design and Evolution of C++. Addison-Wesley*, 1994. — Authoritative interpretation of C++ language design decisions, Chapter 15 specifically discusses the design motivation and trade-offs of templates.
