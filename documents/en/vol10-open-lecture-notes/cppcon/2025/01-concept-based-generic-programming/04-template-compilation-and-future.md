---
title: Template Compilation Model and Future Outlook
description: CppCon 2025 talk notes — templates shouldn't be compiled in isolation,
  concepts as compile-time functions for building a type system, complementary nature
  of interface inheritance and concepts, and future ecosystem development
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
order: 4
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/04-template-compilation-and-future.md
  source_hash: fa37499a23540a3c77d21e3e7acf33586e20828216923a4d6876029ba73d29ee
  translated_at: '2026-05-26T11:06:46.187438+00:00'
  engine: anthropic
  token_count: 4467
---
# Templates Shouldn't Be Compiled in Isolation

Earlier, when we talked about using concepts to add constraints to templates, a question kept circling in my mind: if I add a precise concept constraint to the parameters of `advance` — say, requiring it to be `random_access_iterator` — wouldn't `input_iterator` be shut out? Would I have to write a bunch of overloads for different iterator categories, each advancing in a different way? Isn't that falling right back into the old trap of unstable interfaces — where every newly supported iterator type forces me to go back and modify the declaration of `advance`?

Honestly, this problem bothered me for quite a while. I used to think that with concepts, "splitting templates apart and compiling them in isolation" should be the ideal state — each template checked independently, passing on its own, then assembled together. But seeing this iterator-advancing example completely woke me up: you actually can't do that, and you shouldn't.

## A seemingly simple problem first

You might ask, what does "compiling a template in isolation" mean? The way I understand it is this: when the compiler sees a template definition, it judges whether the template is valid solely based on the concept constraints on the template signature, without looking at what operations the actual types passed in at the call site can provide.

Sounds wonderful, right? But the problem arises immediately.

Let's look at the `std::advance` function. Its job is to advance an iterator forward by n steps. For different iterator categories, the advancing method is completely different: `random_access_iterator` can directly `+= n` in one shot; whereas `input_iterator` doesn't have `+=`, so it can only `++` one by one.

I used to think that `input_iterator` not having `+=` was some kind of "defect" in the standard, or at least a limitation that should be fixed. But that's not the case — there's a perfectly good reason `input_iterator` doesn't provide `+=`. It represents the abstraction of "can only advance one step at a time, not jump." It's a feature, not a defect.

## Write an example to see for yourself

I wrote a piece of code to verify this behavior, running on my Arch Linux WSL, with GCC 16.1.1 as the compiler and `-std=c++20` enabled.

```cpp
#include <vector>
#include <list>
#include <iostream>

// 一个简化版的 advance，模拟标准库的行为
template<typename Iter>
void my_advance(Iter& it, int n) {
    // 如果迭代器支持 +=，直接跳
    if constexpr (requires(Iter i, int m) { i += m; }) {
        it += n;
    } else {
        // 否则一步一步走
        for (int i = 0; i < n; ++i) {
            ++it;
        }
    }
}

int main() {
    // vector 的迭代器是 random_access_iterator，支持 +=
    std::vector<int> vec = {10, 20, 30, 40, 50};
    auto vit = vec.begin();
    my_advance(vit, 2);
    std::cout << *vit << "\n";  // 输出 30

    // list 的迭代器是 bidirectional_iterator，不支持 +=
    std::list<int> lst = {10, 20, 30, 40, 50};
    auto lit = lst.begin();
    my_advance(lit, 2);
    std::cout << *lit << "\n";  // 输出 30

    return 0;
}
```

Run it, and the output matches expectations perfectly: two 30s. See? The same `my_advance` uses `+=` for `vector`, and uses a loop of `++` for `list`. The reason all of this works is precisely because the template doesn't check in isolation "whether you actually support `+=`" before being instantiated — it waits until it sees the concrete type, and only then makes the choice via `if constexpr`.

## What if we really compiled in isolation?

Now let's imagine: what if C++ eventually implemented isolated template compilation, with no exemption clauses?

When the compiler sees the definition of `my_advance`, it would check whether every line of code inside is valid for the type described by the constraints. If my constraint says `input_iterator`, and the definition of `input_iterator` doesn't include `+=`, the compiler would flat-out reject the line `it += n` — even though it would never be executed at runtime (because it's blocked by the `if constexpr` branch).

Then I'd have to split the code into two overloads:

```cpp
// 给 random_access_iterator 用的版本
template<std::random_access_iterator Iter>
void my_advance(Iter& it, int n) {
    it += n;
}

// 给其他迭代器用的版本
template<std::input_iterator Iter>
    requires (!std::random_access_iterator<Iter>)
void my_advance(Iter& it, int n) {
    for (int i = 0; i < n; ++i) {
        ++it;
    }
}
```

Doesn't look too bad? But think about it carefully — this is really pushing the act of "an algorithm making different choices based on type capabilities" from inside the algorithm out to the interface level. For every additional iterator category that needs special handling, I'd have to add another overload. The interface bloats, maintenance costs rise, and fundamentally, I'm repeating the same logic.

Even more critical is the performance issue. If `advance` can't use `+=` on `random_access_iterator`, and can only use a `++` loop, the complexity jumps from O(1) to O(n). When this is called inside an algorithm, if the outer loop is also O(n), the overall complexity explodes from O(n) to O(n^2). For large data volumes, that's fatal.

A large part of why the STL is efficient comes from this ability to "branch internally within a template based on type capabilities." If isolated compilation blocks this path, the STL's performance advantage would be severely diminished.

## So what are concepts actually good for?

At this point you might ask: does that mean concepts are useless? We said earlier they catch errors sooner, but now we're saying they can't do isolated checking — isn't that contradictory?

I was confused at first too, but once I thought it through, I realized it's not contradictory at all. The value of concepts lies here: when there truly is no type in the system that satisfies the constraint, the error gets caught earlier, and the error message is much clearer — it tells you "the type you passed in doesn't satisfy `input_iterator`," instead of spitting out a screenful of incomprehensible template instantiation backtraces.

But "catching errors earlier" and "compiling templates in isolation" are two different things. Templates still need to see the concrete type to make the final validity judgment; concepts just make the failure messages of that judgment readable. In this sense, templates have always been type-safe — it's just that before, when errors occurred, you couldn't understand them at all, and now you can.

## Pragmatic, not dogmatic

So what's the conclusion? At least at this stage, we shouldn't pursue isolated template compilation. If C++ does eventually implement this feature, there must be some exemption mechanism that allows patterns like `advance` — "branching internally based on type capabilities" — to remain legal. Because honestly, this pattern is everywhere in the software infrastructure we use daily.

This reminds me of when I was learning templates — I always felt "the stricter the constraints, the better," and I wanted to lock down every template parameter. But after writing a lot of code, I discovered that the essence of generic programming is precisely this: you describe a minimal requirement, then flexibly adapt to types with different capabilities inside the implementation. That's not laziness — that's pragmatism.

## Returning to the essence of generic programming

After wrestling with all of this, I looked back and re-understood what generic programming really is. It's not some mystical art — it's just programming itself, but done in the most general, most efficient, and most comfortable way possible. The "concept" here doesn't refer to the C++20 language feature, but rather your general abstraction of an idea: what is an iterator, what is a callable, what is a range.

These things weren't invented by C++. If you flip through Alexander Stepanov and Daniel E. Rose's *From Mathematics to Generic Programming*, it's full of pure mathematics — algebraic structures, axioms, theorems. If you don't like math, that book is genuinely painful to read (I admit I put it down after a few pages). But the core idea is really quite simple: find the common algebraic structures among different types, then write algorithms targeting that structure, not a specific type.

Moreover, generic programming has built-in uniform usage of types from the very beginning — how scopes are managed, how names are resolved, how objects are created and destroyed — these are just as important in generic code as in any other code. It was introduced long before C++ existed; C++ simply expressed this set of ideas through the mechanism of templates.

At this point, I finally understood why "templates shouldn't be compiled in isolation." Looking back, it's actually not complicated — it's just about not sacrificing the flexibility and performance of real-world engineering for theoretical purity. At the end of the day, we write code to solve problems, not to write papers proving purity.

---

# Concepts: Building Your Own Type System at Compile Time

Honestly, when I reached this conclusion, it hit me like a revelation. When I was learning concepts before, I always treated them as "more elegant SFINAE," thinking they were just syntactic sugar for constraining template parameters — nicer to look at than `std::enable_if`. But after a whole night of experimenting, I finally figured it out: the essence of concepts is actually **compile-time functions** — they take types and values as parameters, return a bool, and tell you whether a type satisfies a certain condition. After this cognitive shift, many things that followed suddenly clicked.

## First, get this straight: what are concepts actually doing?

I had a persistent misunderstanding — I thought concepts were describing "what a type looks like," like "it must have `begin()` and `end()`." But that's not actually the case. Concepts describe "what a generic function requires of its parameters," and they **don't care how that requirement is met**. This distinction is crucial, and I completely missed it at first.

What do I mean? For example, if you write a concept requiring "can do addition," you don't need to say "implemented via `operator+`" or "implemented via some member function" — you just say "can do addition." The compiler figures it out itself. This is completely different from the classic OOP mindset of "must inherit from a certain base class, must override a certain virtual function" — OOP prescribes top-down "how you provide it," while concepts state bottom-up "what I need."

Furthermore, concepts can accept multiple parameters, not just one type parameter. This means you can express cross-type constraints like "type A and type B can perform a certain operation with each other," which is almost impossible to express elegantly in traditional OOP.

## Write a few concepts to get a feel for it

My experiment environment is Arch Linux WSL, GCC 16.1.1, with `-std=c++20 -Wall -Wextra` added to the compile command. The code below is something I wrote myself to verify the understanding that "concepts are compile-time functions":

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>

// 最简单的 Concept：接受一个类型参数，返回 bool
template<typename T>
concept HasSize = requires(T t) {
    { t.size() } -> std::convertible_to<std::size_t>;
};

// 接受两个类型参数的 Concept：表达跨类型约束
template<typename T, typename U>
concept CanAdd = requires(T a, U b) {
    a + b;  // 只要求 a + b 是合法表达式
};

// 接受类型参数和值参数的 Concept
// sizeof 在编译期求值，所以这里不需要运行时信息
template<typename T, std::size_t N>
concept IsLargeType = (sizeof(T) >= N);

void test_single_param(HasSize auto& container) {
    std::cout << "size = " << container.size() << "\n";
}

// 双参数 concept 不能用 "CanAdd auto" 语法——那只对单参数 concept 有效
// 必须用显式的 requires 子句，把两个模板参数传进去
template<typename T, typename U>
    requires CanAdd<T, U>
void test_cross_add(T a, U b) {
    auto result = a + b;
    std::cout << "a + b = " << result << "\n";
}

int main() {
    std::string s = "hello";
    test_single_param(s);  // OK，string 有 size()

    // test_single_param(42);  // 编译错误：int 不满足 HasSize

    test_cross_add(10, 20);      // OK，int + int -> int
    test_cross_add(10, 3.14);    // OK，int + double -> double

    // test_cross_add("hello", 42);  // 编译错误：const char* + int 不合法

    static_assert(IsLargeType<double, 4>);  // sizeof(double) == 8 >= 4
    static_assert(!IsLargeType<char, 4>);   // sizeof(char) == 1 < 4
}
```

Run it and you'll see that `HasSize` accepts one type parameter, `CanAdd` accepts two type parameters, and `IsLargeType` is even more interesting — it accepts both a type and a compile-time value simultaneously. These three parameter forms can be freely combined, making the expressiveness very powerful.

There's one pitfall I stumbled on for a long time though: multi-parameter concepts can't be written directly in front of `auto` as a constraint the way single-parameter ones can (for example, `CanAdd auto a` will directly cause a compilation error, because `CanAdd` needs two template parameters but you only gave it one). Multi-parameter concepts must use an explicit `requires` clause to pass the parameters in. Single-parameter concepts don't have this limitation; writing `HasSize auto& container` feels very natural.

## Overloading with concepts: simpler than regular overloading

This is the part I was most confused about before. I used to think template overloading was a nightmare — you had to use SFINAE for partial specialization, error messages spanned three screens, and the rules were so complex they made you question your life choices. But overloading with concepts has rules that are actually **simpler** than regular function overloading.

I wrote an example to verify these three cases:

```cpp
#include <iostream>
#include <concepts>
#include <vector>
#include <list>

// 约束 A：可排序的容器
template<typename T>
concept SortableContainer = requires(T t) {
    requires std::ranges::range<T>;
    requires std::totally_ordered<typename T::value_type>;
};

// 约束 B：可排序的随机访问容器（比 A 更严格）
template<typename T>
concept RandomAccessSortable = SortableContainer<T> && requires(T t) {
    requires std::random_access_iterator<typename T::iterator>;
};

// 情况1：只有一个匹配
void process(SortableContainer auto& c) {
    std::cout << "sortable container\n";
}

// 情况2：两个都匹配，但一个是另一个的子集 -> 选最严格的
void process(RandomAccessSortable auto& c) {
    std::cout << "random access sortable container\n";
}

int main() {
    std::list<int> lst = {3, 1, 2};
    std::vector<int> vec = {3, 1, 2};

    process(lst);  // 只匹配 SortableContainer -> 输出 "sortable container"
    process(vec);  // 两个都匹配，但 RandomAccessSortable 更严格 -> 输出 "random access sortable container"
}
```

See? Just three rules, crystal clear: if only one matches, use it directly; if two match and one is a subset of the other, pick the stricter one; everything else is an error. There are none of those complex rules from regular overloading involving implicit conversion ranking and ambiguity resolution. I was stuck on this for a long time, always assuming concept overloading had some hidden pitfall, but looking back at the principles, it's really quite simple.

## The part that really lit me up: extending C++'s type system

This is where it gets truly interesting. I used to think C++'s type system was fixed — int is int, double is double, narrowing conversion is unsafe, and you just had to work around it or use `-Wnarrowing` to warn about it. But concepts let you **build your own type system at compile time**, catching things at compile time that originally required runtime checks.

Following this line of thinking, I wrote a concept for `SafeNumericConvert` to distinguish between "safe numeric conversions" and "potentially data-losing narrowing conversions" at compile time:

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>
#include <limits>
#include <stdexcept>

// 编译期判断：从 From 到 To 的转换是否可能丢失数据
// 安全条件：To 严格更宽，或者同宽且符号性相同
template<typename From, typename To>
concept SafeNumericConvert =
    std::integral<From> && std::integral<To> &&
    (sizeof(From) < sizeof(To) ||
     (sizeof(From) == sizeof(To) &&
      std::is_signed_v<From> == std::is_signed_v<To>));

// 只有安全转换才能编译通过的包装函数
template<typename To, typename From>
    requires SafeNumericConvert<From, To>
constexpr To safe_cast(From val) {
    return static_cast<To>(val);
}

// 运行时才检查的版本：处理编译期无法判断的情况
template<typename To, typename From>
    requires (std::integral<From> && std::integral<To> && !SafeNumericConvert<From, To>)
To checked_cast(From val) {
    if constexpr (std::is_signed_v<From> && std::is_unsigned_v<To>) {
        if (val < 0) throw std::overflow_error("negative to unsigned");
    } else if constexpr (std::is_unsigned_v<From> && std::is_signed_v<To>) {
        // unsigned -> signed 同 size：0 永远合法，只需检查上界
        if (val > static_cast<From>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("narrowing conversion would overflow");
        }
    } else {
        // signed -> signed 缩小 或其他情况，用公共类型做安全比较
        using Common = std::common_type_t<From, To>;
        if (static_cast<Common>(val) < static_cast<Common>(std::numeric_limits<To>::min()) ||
            static_cast<Common>(val) > static_cast<Common>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("narrowing conversion would overflow");
        }
    }
    return static_cast<To>(val);
}

int main() {
    int x = 42;
    auto y = safe_cast<long long>(x);       // OK，int -> long long 是安全的
    // auto z = safe_cast<char>(x);          // 编译错误！int -> char 可能窄化
    // auto u = safe_cast<int>(uint32_t(0)); // 编译错误！uint32_t -> int32_t 同 size 但 unsigned -> signed

    // 需要运行时检查的场景，用 checked_cast
    auto w = checked_cast<char>(x);         // 运行时检查，42 在 char 范围内，OK
    // auto q = checked_cast<char>(300);     // 运行时抛异常
}
```

See? `safe_cast` blocks narrowing conversions right at compile time — there's no need to wait until runtime to discover the problem. And `checked_cast` only introduces runtime overhead when safety can't be determined at compile time. This is what "extending the C++ type system" means — you use concepts to add a layer of your own type-safety checks on top of C++'s existing type rules.

I used to think templates were black magic — I'd get a headache just seeing angle brackets, and I didn't want to look at screen after screen of error messages. But looking back now, concepts elevate templates from "compiler internal implementation details" to "your own type system design language." You're no longer fighting the compiler — you're **designing rules**.

I finally get it now. Concepts aren't a replacement for SFINAE, and they aren't syntactic sugar for `enable_if`. They're a complete mechanism for writing functions, making judgments, selecting branches, and building type constraints at compile time. And the ultimate goal this mechanism points to is: letting you grow new type rules on top of C++'s existing type system, according to your own domain needs. Looking back, it's really not that hard — but if nobody had punctured the veil of "compile-time functions" for me, I might have kept struggling in the SFINAE quagmire for a long time.

---

# Value Parameters in Concepts: Breaking the Last Mindset

After figuring out that "concepts are compile-time functions," I suddenly thought of a question that had always puzzled me before: since a concept is essentially a constexpr variable template returning `bool`, can it accept non-type parameters? The answer is yes, and it reads very naturally.

## Starting from the basics: what is a concept, really?

I used to treat concepts as a "special type-constraint syntax," thinking they and regular functions were two completely different worlds. This misunderstanding was actually quite harmful, because it prevented me from understanding many more advanced usages.

Let's look at a very ordinary concept definition:

```cpp
#include <concepts>
#include <type_traits>

// 我以前写的 concept，长这样——只接受类型参数
template<typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

This looks very "type-specific," right? But if you translate a concept into its true form, it's actually just a constexpr variable template returning `bool`. The way the compiler internally views the code above is roughly equivalent to:

```cpp
template<typename T>
constexpr bool Addable_v = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

Since it's constexpr, since it's a template, why couldn't it accept non-type parameters? There's no reason to forbid this. The reason I couldn't figure it out before was purely because I'd seen too much of the `typename T` pattern and had formed a mindset.

## Try it out: passing values into concepts

Once I understood the relationship above, writing the code followed naturally. Let's define a concept that constrains not just the type, but also a specific numerical condition:

```cpp
#include <iostream>
#include <concepts>
#include <array>

// 这个 concept 接受一个类型参数和一个值参数
// 它表达的含义是：T 是一个整数类型，且值 v 必须大于等于 0
template<typename T, T v>
concept NonNegativeIntegral = std::integral<T> && (v >= 0);

// 用它来约束一个函数
template<typename T, T v>
    requires NonNegativeIntegral<T, v>
constexpr T safe_value() {
    return v;
}

int main() {
    // 这个没问题，int 类型，值是 42，满足 >= 0
    std::cout << safe_value<int, 42>() << "\n";

    // 这个也没问题，值是 0，边界情况
    std::cout << safe_value<int, 0>() << "\n";

    // 下面这行如果取消注释，编译会直接报错
    // 因为值 -1 不满足 v >= 0 的约束
    // std::cout << safe_value<int, -1>() << "\n";

    return 0;
}
```

Compile and run, and the output is `42` and `0`, exactly as expected. You might say, this doesn't look any different from constraining non-type template parameters? True — in simple scenarios, it has a similar effect to the `static_assert` or `requires` clauses for non-type template parameters. But the advantage of concepts is that they can be named, composed, and overloaded, which makes them completely different.

## An even more interesting usage: concept overloading with value parameters

Since concepts can carry value parameters, can I use different values to trigger different overloads? The answer is yes, and it reads very clearly:

```cpp
#include <iostream>
#include <string>

// 定义两个 concept，用不同的值来区分
template<int N>
concept IsSmall = (N <= 10);

template<int N>
concept IsLarge = (N > 10);

// 当 N 小于等于 10 时走这个实现
template<int N>
    requires IsSmall<N>
std::string describe_size() {
    return "small: " + std::to_string(N);
}

// 当 N 大于 10 时走这个实现
template<int N>
    requires IsLarge<N>
std::string describe_size() {
    return "LARGE: " + std::to_string(N);
}

int main() {
    std::cout << describe_size<3>() << "\n";   // 输出: small: 3
    std::cout << describe_size<50>() << "\n";  // 输出: LARGE: 50
    return 0;
}
```

Seeing this, I suddenly realized something. In the past, if I needed to do this kind of dispatch based on compile-time values, I most likely would have written `if constexpr`. It works, but that approach crams all branches into a single function body — once the value-based branches multiply, the function becomes long and hard to read. With concept overloading, each branch is an independent function, the logic is completely isolated, and it's much cleaner.

## constexpr vs concept: when do you use which?

Concepts can contain value logic, and constexpr functions can contain value logic — so when should you use which?

I thought about this for quite a while, and eventually I figured out a very simple criterion. Ask yourself one question: what is the result of this computation? If the result is a value, say it computes to a `7`, then it naturally should be a constexpr function. If the result is a "judgment about a type," a yes or no, then it's suited to be a concept.

Here's a very intuitive example. Suppose I want to compute the factorial of an integer at compile time:

```cpp
// 结果是值，用 constexpr 函数，天经地义
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial(5) == 120);
```

You wouldn't write factorial as a concept, because the result of factorial isn't a boolean value — it's not a constraint. Conversely, if you want to express "can this type be used for a certain kind of numerical computation," that's a concept's job:

```cpp
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template<Numeric T>
T compute(T x) {
    return x * x + 1;
}
```

So essentially, constexpr/consteval solve the problem of "computing values at compile time," while concepts solve the problem of "judging types at compile time." They're both compile-time evaluation mechanisms, and there are many similarities in their internal implementation — after all, the constraint expression of a concept itself is evaluated in a constexpr context — but their responsibility boundaries are clear.

That said, as I demonstrated earlier, once concepts carry value parameters, this boundary becomes slightly blurred. Because your concept is indeed doing some numerical computation (like `v >= 0`), it's just that the final result is reduced to a boolean value. I think this blurring is a good thing — it gives us more expressiveness, as long as you're clear in your own mind about what you're doing.

## By the way, a note on consteval and constinit

Since we mentioned constexpr, I'll briefly bring up two other keywords introduced in C++20, because they're often discussed together and I used to mix them up too.

`consteval` is called an "immediate function," meaning this function must execute at compile time — it doesn't even give you the possibility of being called at runtime. A `constexpr` function, on the other hand, "tries to execute at compile time, but if the parameters aren't compile-time constants, it's also allowed to execute at runtime." `constinit` guarantees that a variable is initialized at compile time, but doesn't require that it can't be modified afterward (unlike `const`). These three things each have their own uses, but in my actual projects so far, I still use `constexpr` the most. I've used `consteval` a few times in some deeply nested template scenarios where performance is extremely sensitive.

---

# Interface Inheritance vs Concepts: It's Not About One Replacing the Other

Someone asked a question that had also been bothering me: since C++20 has concepts, can we retire those interface classes with only pure virtual functions? Can concepts completely cover the functionality of interface inheritance?

Honestly, I had the same thought when I was learning concepts. At the time I felt, concepts are so elegant — compile-time checking, zero runtime overhead, no need to write a bunch of virtual functions and vtables, it's like a dimensional strike. But after hearing the answer, I realized I was thinking too simplistically. Bjarne Stroustrup's answer was very direct: no, concepts can't completely cover interface inheritance. And he himself uses interface inheritance far more frequently than implementation inheritance. The key distinction here is that interface inheritance defines what a class "looks like," while implementation inheritance defines how a class "does its work." The former has always been a good practice in C++, while the latter is what everyone complains about. Bjarne Stroustrup says there are two fundamentally different ways to specify an interface: one is a fixed, strictly defined interface, and the other is a flexible, open interface. You need both — they solve different problems.

I hadn't thought this distinction through clearly before. Looking back now, a fixed interface is the kind where "you must provide these five methods, the signatures must match exactly, not a single one missing." A typical example is a plugin system — the main program defines a `IPlugin` interface, and all plugins must implement it precisely. In this scenario, virtual function interface classes are actually a natural fit, because the interface itself is a "contract," clearly spelling out in black and white what you need.

Flexible interfaces are more like the domain where concepts excel. You don't need to match a specific method signature exactly; you just need to satisfy certain "constraint conditions." For example, you don't need a method called `draw`; you just need to "be passable to a function that accepts stream output." This kind of loose, capability-based constraint is indeed more naturally expressed with concepts. In other words, it's more relaxed than an is-a relationship — you just need to "be able to do it."

As for when to use which, based on my own practice, my rough judgment now is this: if your interface is meant for "people" to read — that is, another developer needs to clearly know "which methods do I need to implement" — then an interface class is clearer, because the IDE will directly prompt you about which pure virtual functions you haven't implemented. If your interface is meant for the "compiler" to read — that is, constraining in templates so that type checking errors come earlier and error messages are more readable — then concepts are more appropriate.

---

# What Comes After Concepts? — From "What Else Can the Language Add?" to "How Should We Use Them?"

The next stage isn't about making the language more perfect — it's about **writing more libraries that truly make good use of concepts**.

The speaker was very practical — the paper does list about ten "things that could potentially be done," but he believes what's really needed isn't those. What we need is to **accumulate experience in practice**, to see how concepts and other parts of the language (like constraint partial ordering, interaction with SFINAE, interplay with modules) actually perform in real large-scale codebases. This observation period could take years.

My current understanding is: language features aren't better just because they're more advanced — they need to be driven by **real problems that exist in the real world**. If nobody is actually writing libraries with concepts and encountering real pain points, then no matter how many proposals there are, they're just toys on paper.

## Another very practical question: should the standard library add more concept constraints?

Someone at the venue also asked a very down-to-earth question: the type parameter of `std::vector` currently has basically no constraints, so should we add a concept like `std::copyable` to restrict it?

I'd thought about this question before too. I've written code like this:

```cpp
#include <vector>
#include <string>

int main() {
    // 这玩意儿能编译通过，但你基本没法对它做任何有意义的事
    std::vector<std::unique_ptr<int>> v;
    // v.push_back(std::make_unique<int>(42));  // 编译错误
    // 但 vector 本身的实例化是完全合法的
}
```

I thought it was weird at the time — `unique_ptr` isn't copyable, and putting it into a vector would blow up most operations, so why doesn't the standard library block this at the declaration?

The speaker's answer helped me understand the standard library maintainers' dilemma. He said "you have to be extremely careful," because **for years people have been doing things simply because they could**. He gave an example: some people use `std::accumulate` to concatenate strings. This thing was originally designed for numeric type reduction, but because you didn't add constraints, it could compile, so people just used it that way.

Now if you suddenly add a `std::arithmetic` constraint to `std::accumulate`, all the code using `accumulate` to concatenate strings would blow up. You don't know whose code you'd break. So the standard committee faces a choice: either provide two overloads (a numeric version and a non-numeric version), or do nothing at all. Neither is a decision to be made lightly.

I ran a small experiment to verify what this "accumulate for string concatenation" is really about:

```cpp
#include <numeric>
#include <string>
#include <vector>

int main() {
    std::vector<std::string> words = {"hello", " ", "world"};

    // accumulate 的默认操作是 std::plus，对 string 来说就是 operator+
    // 初始值 "" 是 string，所以整个推导就顺着走下去了
    auto result = std::accumulate(
        words.begin(), words.end(),
        std::string("")
    );

    // result == "hello world"，确实能跑
}
```

See? This code runs, and the result is correct. But if C++20's `<numeric>` directly added a `std::integral` or `std::floating_point` constraint to `accumulate`, this code would die on the spot. This kind of "historical baggage" isn't something you can just clean up whenever you want. (There's nothing to be done about it!)

## So what is the "next stage" of concepts, really?

Looking at these two questions together, my current understanding is this:

Concepts as a language feature have already landed. C++20 gave us the standard concepts in the `<concepts>` header, the `requires` clause, the `requires` expression, constraint partial ordering — the toolbox is sufficient. **The bottleneck isn't the language; it's the ecosystem.**

What do I mean by "ecosystem"? It means:

First, the standard library itself needs to use concepts more reasonably. C++20 has already done a lot — the algorithms in `std::ranges` are almost entirely constrained with concepts for iterator types, projection types, and so on. But old relics like `std::vector` affect everything when you change them, requiring extreme caution.

Second, those of us writing application code and third-party libraries need to start using concepts in **our own interfaces**. Not writing toy examples in blog posts, but in real projects — constraining template parameters with concepts, replacing `static_assert`, and wiping out the SFINAE `std::enable_if` hell. Then accumulating experience through this process — what concept granularity is appropriate, where to place constraints for maximum clarity, how to give users the best error messages.

Third, only after enough of this practical experience has accumulated will we know "what the language is still missing." Not by sitting in a chair daydreaming right now.
