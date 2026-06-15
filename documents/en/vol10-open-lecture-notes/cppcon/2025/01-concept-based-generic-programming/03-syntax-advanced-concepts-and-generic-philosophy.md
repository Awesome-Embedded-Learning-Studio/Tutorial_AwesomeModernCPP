---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 Talk Notes — Syntax Unification, SmartPtr Constraints, Multi-parameter
  Concepts, Generic vs. OOP, Iterative Refinement, and a First Look at C++26 Reflection
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 42
speaker: Bjarne Stroustrup
tags:
- cpp-modern
- host
- intermediate
talk_title: Concept-based Generic Programming
title: Unified Syntax, Advanced Concepts, and Generic Philosophy
translation:
  engine: anthropic
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/03-syntax-advanced-concepts-and-generic-philosophy.md
  source_hash: 5e7ecb33685e3412ee10ba326b0475db95ed9435e4114b6c37a1f7582bcac213
  token_count: 7391
  translated_at: '2026-05-26T11:05:21.113420+00:00'
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
---
# Unified Syntax: More Important Than We Think

I used to think that unified syntax was just a superficial way to "make code look prettier." But if you look back at Simula or Java, you'll find an awkward design: custom types must be created with `new`, but built-in types cannot. In Simula, you cannot even use `new` on a `int`. This leads to a fatal consequence — you can never write a truly generic container or algorithm, because the syntax itself is split in two. One half handles built-in types, the other handles custom types: two sets of code, two sets of rules.

C++ avoided this problem from the very beginning. There is no syntactic difference between `int x = 0;` and `MyString x;`, which means that when you write a `template<typename T>`, the way you create `T` is exactly the same whether it is a `int` or a `MyString`. This decision seems unremarkable, but it is the very prerequisite that makes C++ generic programming possible.

The same logic applies to resource management. If resource management is not part of type design, and you must manually `malloc`/`free` and `new`/`delete`, then your generic code can never be truly universal — because you always have to special-case "this type requires manual resource release" somewhere. RAII embeds resource management into the type's own lifecycle, which is what allows generic code to "treat all types equally." Seeing this gave me a profound realization: the significance of RAII is not just "preventing you from forgetting to release resources" — it is the type system cornerstone that makes generic programming possible.

## Locking Down the Smart Pointer's Arrow Operator with Concepts

Having understood that prerequisite, let's look at a very specific example. When I was writing a simple smart pointer, I ran into an issue: `operator->` is not something that every type should have.

Think about it — the semantics of `operator->` are "access a member through a pointer." So if my smart pointer wraps a `int`, what members does `int` have to access? Therefore, `operator->` only makes sense when `T` is a class type. Before concepts, you either provided it unconditionally (and users calling it on a `int` would get an incomprehensible template error), or you used SFINAE with a bunch of `std::enable_if` that made the code look like gibberish. Now with concepts, things become beautifully clean.

```cpp
#include <iostream>
#include <concepts>
#include <string>

// 定义一个 concept：T 必须是 class 类型（包含 struct）
template<typename T>
concept HasMembers = std::is_class_v<T>;

template<typename T>
class SmartPtr {
    T* ptr_;
public:
    explicit SmartPtr(T* p = nullptr) : ptr_(p) {}
    ~SmartPtr() { delete ptr_; }

    // 禁止拷贝，简化示例
    SmartPtr(const SmartPtr&) = delete;
    SmartPtr& operator=(const SmartPtr&) = delete;

    // operator* 对所有类型都可用
    T& operator*() const {
        return *ptr_;
    }

    // operator-> 只在 T 是 class 类型时才存在
    // 如果你试图对 SmartPtr<int> 调用 ->，编译器直接告诉你这个成员函数不存在
    // 而不是给你一堆模板实例化的天书报错
    T* operator->() const requires HasMembers<T> {
        return ptr_;
    }
};

// 测试：对 class 类型，两个操作符都可用
void test_with_class() {
    SmartPtr<std::string> sp(new std::string("hello"));
    std::cout << *sp << std::endl;       // OK，operator*
    std::cout << sp->size() << std::endl; // OK，operator->，因为 string 是 class
}

// 测试：对 int，只有 operator* 可用
void test_with_int() {
    SmartPtr<int> sp(new int(42));
    std::cout << *sp << std::endl;  // OK，operator*
    // std::cout << sp->  // 编译错误！SmartPtr<int> 没有 operator->
    // 报错信息很清晰：没有名为 'operator->' 的成员
}
```

I ran it, and `test_with_class()` works perfectly. In `test_with_int()`, if you uncomment that line `sp->`, GCC gives the error "no member named 'operator->' in 'SmartPtr<int>'" — clean and to the point. Back when using `enable_if`, the error could scroll across a full screen; now it's just one sentence. This is the experience improvement that concepts bring — not "enabling things you couldn't do before," but "doing the same things with ten times better experience."

You might ask, why not just use `operator*` and be done with it? True, if you only use `operator*`, the smart pointer's behavior is uniform across all types. But `operator->` is just too convenient when operating on object types, and it's a real shame not to use it at all. So the correct approach is not "cut it off entirely," but "precisely control when it exists." That's exactly what concepts are for.

## Copy Construction of pair: A Narrowing Pitfall in the Standard

After finishing the smart pointer, I followed the same train of thought to look at the implementation of `std::pair`. `std::pair` has a templated version of its copy constructor that looks roughly like this: you can copy-construct a `pair<C, D>` from a `pair<A, B>`, provided that `A` can convert to `C` and `B` can convert to `D`. The standard indeed specifies it this way, and it seems quite reasonable, right?

But on closer inspection, I found a problem: this conversion uses ordinary implicit conversion, meaning it allows narrowing conversion. For example, you can copy a `pair<double, double>` into a `pair<int, int>`, and the fractional part gets truncated directly without the compiler giving you even a warning. This is definitely not the behavior I want.

```cpp
#include <utility>
#include <iostream>

void test_std_pair_narrowing() {
    std::pair<double, double> src{3.14, 2.718};
    // 这行代码能编译通过！3.14 变成 3，2.718 变成 2
    // 没有任何警告，数据静默丢失
    std::pair<int, int> dst = src;
    std::cout << dst.first << ", " << dst.second << std::endl; // 输出: 3, 2
}
```

I ran it, and the output was indeed `3, 2`. The compiler (GCC 15, with `-Wall -Wextra` enabled) didn't say a word.

## Writing a Safe pair Ourselves: NonNarrowConvertible

In [Part 1](01-type-safety-and-number-concept.md), we already discussed the detection mechanism for narrowing conversions in depth. Here we use a more concise approach — leveraging the language rule that brace initialization prohibits narrowing — to implement `NonNarrowConvertible`. The idea is simple: during copy construction, use a concept to constrain the conversion process and disallow narrowing.

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>
#include <stdexcept>

// 一个 concept：A 可以非窄化地转换为 B
// 核心思路：用花括号初始化来检测，因为花括号初始化禁止 narrowing
template<typename A, typename B>
concept NonNarrowConvertible = requires(A a) {
    // 如果这行能编译通过，说明 A 到 B 不存在 narrowing
    // 因为花括号初始化会拒绝 narrowing conversion
    B{static_cast<A>(a)};
};

template<typename T1, typename T2>
class SafePair {
public:
    T1 first;
    T2 second;

    SafePair() : first{}, second{} {}
    SafePair(T1 f, T2 s) : first(f), second(s) {}

    // 核心部分：从另一个 SafePair 拷贝构造
    // 要求两个维度都是 NonNarrowConvertible
    template<typename U1, typename U2>
    requires NonNarrowConvertible<U1, T1> && NonNarrowConvertible<U2, T2>
    SafePair(const SafePair<U1, U2>& other)
        : first(static_cast<T1>(other.first))
        , second(static_cast<T2>(other.second))
    {}
};

void test_safe_pair_no_narrowing() {
    SafePair<double, double> src{3.14, 2.718};

    // 这行会编译失败！double -> int 是 narrowing
    // 错误信息会指向 NonNarrowConvertible concept 不满足
    // SafePair<int, int> dst = src;  // 取消注释会报错

    // 这个没问题，int -> double 不是 narrowing
    SafePair<int, int> src2{3, 2};
    SafePair<double, double> dst2 = src2;  // OK
    std::cout << dst2.first << ", " << dst2.second << std::endl; // 3, 2
}
```

I spent an entire evening figuring out the trick behind this `NonNarrowConvertible` concept. Its principle leverages the language rule that brace initialization prohibits narrowing: if there is a narrowing from `A` to `B`, the line `B{a}` is itself ill-formed. The `requires` expression detects this ill-formed condition and turns it into a concept failure, rather than a hard compilation error. This elevates narrowing detection from "losing data at runtime" to "being rejected outright at compile time."

However, there is a subtle pitfall worth noting: the implementation of `NonNarrowConvertible` relies on "whether brace initialization can compile successfully," rather than precisely determining "whether narrowing exists." For numeric types, these two things are equivalent, but for complex types, brace initialization might fail for other reasons (such as lacking a corresponding constructor), and the error message in such cases could be confusing. It's sufficient for the current scenario, but if we encounter more complex situations in the future, we can refine this concept.

Moreover, C++'s protection against narrowing is actually incomplete — the rule that brace initialization prohibits narrowing only applies to initialization. Assignment, function argument passing, and return values all let it through. True safety still relies on constraints at the type system level, such as using concepts to block unsafe conversion paths at compile time.

## A First Taste of C++26 Static Reflection

At this point, while the process of hand-rolling `NonNarrowConvertible` exercises our understanding of concept composition, the speaker later presented an even more concise idea: rather than defining what "narrowing" means ourselves, why not just ask the compiler directly, "Can you initialize a `T` with a value of type `S`?" This shift in thinking seems minor, but it actually solves a problem that had stumped me for a long time — our hand-rolled version wasn't accurate enough for scenarios like `char*` to `std::string`, whereas if you directly ask the compiler "can you initialize `T` with `S`," the compiler knows the answer perfectly well.

However, the speaker also honestly reminded us of something: don't confuse this special case with the general methodology it aims to illustrate. The construction technique of "combining small concepts into larger ones" that we spent so much time learning earlier is the truly reusable weapon. This initialization version works purely because "can it be initialized" happens to highly overlap with "can it be narrowed" in this specific scenario. In other scenarios like assignment or comparison, you won't be so lucky — you'll still have to build them up the hard way. The tools in your toolbox are general, but which specific scenario lets you take a shortcut is a matter of luck.

At the end of the talk, something "completely impossible five years ago" was demonstrated — Static Reflection (P2996). Before C++26, if you needed to know what members a struct has, what each member's name and type are, and what its offset in memory is, you could only solve it with macros. A single typo would silently produce wrong results, leading to debugging sessions that make you question your life choices. With C++26's static reflection, we can finally directly ask the compiler, "what does this type look like?"

```cpp
// 基于 C++26 静态反射提案 P2996 R12 编写
// 注意：截至 2026 年初，尚无主流编译器完整实现此提案，此代码供学习参考
#include <meta>
#include <string_view>
#include <print>
#include <cstddef>
#include <array>

// 成员描述符：记录一个成员的元信息
struct member_descriptor {
    std::string_view name;   // 成员名字
    std::size_t offset;      // 在对象内的偏移量
    std::size_t size;        // 该成员占的字节数
};

// 核心魔法：为任意类型生成成员描述符数组
template<typename T>
consteval auto get_layout() {
    // ^^T 是反射运算符：向编译器请求类型 T 的元信息
    // nonstatic_data_members_of 返回 std::vector<std::meta::info>
    auto members = std::meta::nonstatic_data_members_of(^^T);
    constexpr size_t N = members.size();

    std::array<member_descriptor, N> layout{};
    for (size_t i = 0; i < N; ++i) {
        layout[i] = {
            // identifier_of 获取成员名（前提是该成员有标识符）
            .name   = std::meta::identifier_of(members[i]),
            // offset_of 返回 member_offset 结构体，.bytes 取偏移字节数
            .offset = static_cast<std::size_t>(std::meta::offset_of(members[i]).bytes),
            // size_of 返回该成员占的字节数
            .size   = std::meta::size_of(members[i])
        };
    }

    return layout;
}

// 测试用的结构体
struct Player {
    int id;
    float x;
    float y;
    double health;
    char name[32];
};

int main() {
    constexpr auto xd = get_layout<Player>();

    for (const auto& m : xd) {
        std::println("成员: {:<10} 偏移: {:>3} 字节  大小: {:>3} 字节",
                     m.name, m.offset, m.size);
    }

    return 0;
}
```

My output looked roughly like this (specific offsets may vary due to alignment differences across platforms and compiler flags):

```text
成员: id         偏移:   0 字节  大小:   4 字节
成员: x          偏移:   4 字节  大小:   4 字节
成员: y          偏移:   8 字节  大小:   4 字节
成员: health     偏移:  16 字节  大小:   8 字节
成员: name       偏移:  24 字节  大小:  32 字节
```

Note that the offset of `health` is 16 instead of 12 — this is memory alignment at work. `double` requires 8-byte alignment, so the compiler inserted 4 bytes of padding after `y`. In the past, to verify this kind of thing, you had to calculate it manually or use the `offsetof` macro one by one. Now, a single line of code gives you everything.

Looking back at when we learned concepts, concepts are essentially also asking the compiler "what conditions does this type satisfy." But the questions concepts can ask are very limited — "can it do addition?", "can it be iterated?", "can it be converted?" Static reflection directly opens up all of the compiler's internal knowledge about a type: names, members, base classes, function signatures, template parameters — you take whatever you need. I used to feel that templates were dark magic, concepts made dark magic readable, and static reflection makes dark magic composable. In the future, with reflection plus concepts, we can traverse members at compile time, check whether each member satisfies specific constraints, and generate code for each one separately — clean and efficient.

That said, although C++26's static reflection was voted into the C++26 working draft (P2996) by mid-2025, as of early 2026 no mainstream compiler has a complete implementation — GCC and Clang (Bloomberg's experimental branch [clang-p2996](https://github.com/bloomberg/clang-p2996)) are both actively under development, but neither is complete yet. The code above is written based on the P2996 R12 proposal specification, provided for learning and reference only — do not expect to use it in production environments.

---

# Concepts Are Not Just "Labels for Template Parameters" — They Are More Flexible Than You Think

To be honest, for the first two years of learning concepts, I treated them as syntactic sugar for "slapping labels on template parameters." Writing a `template<std::integral T>` felt about the same as writing an if-else with SFINAE, just prettier. It wasn't until I recently revisited this topic that I realized how shallow my understanding had been — a concept is essentially a compile-time function, and since it's a function, it can accept multiple parameters, and even value parameters. This cognitive shift literally made me slap my thigh, because many constraints I previously thought "couldn't be expressed with concepts" were never language limitations at all — I just hadn't thought them through.

## Debunking a Misconception First: Concepts Are Not Limited to Constraining a Single Type Parameter

When I wrote concepts before, almost all of them looked like this:

```cpp
template<typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

One concept constraining one type, nice and proper. But think about this — if a generic function accepts two parameters of different types, is it enough to constrain each type separately? For example, given a function signature `template<typename T, typename U> void foo(T, U)`, you use `std::integral<T>` and `std::integral<U>` to constrain them respectively, but this only says "T is an integer, U is an integer." It says absolutely nothing about the relationship between T and U. Yet since they appear in the same function, there's likely some connection between them — otherwise, why put them together?

The talk mentioned a statistic: over half of all concepts accept more than one parameter. I initially thought that ratio was exaggerated, but when I went back and looked through my own project code, it was true — as long as your generic code is even slightly complex, cross-type constraint needs are everywhere.

Here's a concrete example. Suppose I'm writing a serialization library, and I need a concept to express "a value of type T can be serialized into a buffer of type U":

```cpp
template<typename T, typename Buffer>
concept SerializableTo = requires(T value, Buffer& buf) {
    // 要求 Buffer 有 write 方法，能接受 T 的序列化结果
    { buf.write(std::declval<const char*>(), std::declval<std::size_t>()) }
        -> std::same_as<std::size_t>;
    // 要求能计算出 T 序列化后的字节大小
    { serialized_size(value) } -> std::convertible_to<std::size_t>;
};

// 使用的时候，两个类型被绑在一起约束
template<typename T, typename Buffer>
    requires SerializableTo<T, Buffer>
void serialize(const T& value, Buffer& buf) {
    auto size = serialized_size(value);
    // ... 实际序列化逻辑
}
```

See? If this concept could only accept one parameter, you'd either have to split the constraints into two places (losing the information about the inter-type relationship), or use a very awkward nested syntax. But multi-parameter concepts let you directly state "what relationship must hold between T and U," so anyone reading the code knows at a glance that these two types aren't operating independently.

## What Excited Me Even More: Concepts Can Accept Value Parameters

This was something I had no idea about. I always thought that a concept's parameter list could only contain types (`typename T`) or template template parameters and the like. I had no idea it could also accept ordinary values. This means you can mix "type constraints" and "value constraints" together at compile time, and what you write looks almost identical to ordinary code.

Suppose I'm writing network-related code and need a buffer with two hard requirements: first, it must be able to hold at least k elements; second, the buffer size must be a power of two (this is very common in memory pools and ring buffers, because modulo can be replaced with bitwise AND).

```cpp
#include <concepts>
#include <cstdint>
#include <type_traits>

// 一个普通的编译期函数，判断是不是 2 的幂
// 关键：consteval 让它只能在编译期执行
consteval bool is_power_of_two(std::size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

// concept 接受一个类型参数 S 和一个值参数 k
template<typename S, std::size_t k>
concept BufferSpace = requires(S buf) {
    // S 必须有 size() 方法返回能转成 size_t 的东西
    { buf.size() } -> std::convertible_to<std::size_t>;
    // 值约束1：大小至少是 k
    requires (S::size_value >= k);
    // 值约束2：大小必须是 2 的幂
    requires is_power_of_two(S::size_value);
};
```

Then I define several buffer types to test:

```cpp
// 大小为 64 的缓冲区（64 是 2 的幂）
struct SmallBuffer {
    static constexpr std::size_t size_value = 64;
    constexpr std::size_t size() const { return size_value; }
};

// 大小为 100 的缓冲区（100 不是 2 的幂）
struct WeirdBuffer {
    static constexpr std::size_t size_value = 100;
    constexpr std::size_t size() const { return size_value; }
};

// 大小为 1024 的缓冲区（1024 是 2 的幂）
struct NetworkBuffer {
    static constexpr std::size_t size_value = 1024;
    constexpr std::size_t size() const { return size_value; }
};
```

Now let's use this concept to constrain a template function:

```cpp
template<typename S>
    requires BufferSpace<S, 128>
void process_buffer(S& buf) {
    // 到这里编译器已经保证了：
    // 1. S 有 size() 方法
    // 2. 大小 >= 128
    // 3. 大小是 2 的幂
    // 所以这里可以放心用位与做取模
    constexpr std::size_t mask = S::size_value - 1;
    // ... 实际处理逻辑
}
```

Let's run it and see how clear the error message is:

```cpp
int main() {
    SmallBuffer small;
    // process_buffer(small);  // 编译错误：size_value(64) < 128

    WeirdBuffer weird;
    // process_buffer(weird);  // 编译错误：100 不是 2 的幂

    NetworkBuffer net;
    process_buffer(net);       // 编译通过：1024 >= 128 且 1024 是 2 的幂
}
```

I tried this under GCC 15. After uncommenting the `process_buffer(small)` line, the compiler's error message directly tells you the constraint was not satisfied, and specifically points out `requires (S::size_value >= k)`. If you used `static_assert` instead of a concept, you'd have to write it inside the function body, and the error location would be inside the function — once the call stack gets deep, it becomes completely unreadable. Concepts lift the constraint to the signature, and the error points directly to the call site. This experience gap is tangible.

Looking back at why this works — a concept declaration is essentially `template<...参数...> concept Name = 布尔表达式;`, and this Boolean expression is evaluated at compile time. Since it's a template parameter list, `typename`, `int`, and `std::size_t` can all appear as parameter types. C++20 template parameters already supported non-type parameters, and concepts simply inherited this mechanism. So there's no special "concept value parameter syntax" — it's just ordinary template non-type parameters.

And the reason `is_power_of_two` can be used inside the concept's `requires` expression is that I declared it as `consteval`. `consteval` was introduced in C++20, meaning "this function must be executed at compile time and cannot be called at runtime." In a concept's constraint expression, what you need is exactly this kind of "guaranteed to complete at compile time" function, because concepts themselves are compile-time entities.

Are value-parameterized concepts actually used in real development? My own experience is: when you write library code and frameworks, you encounter them frequently. Thread pools require task queue sizes to be powers of two (for bitwise AND modulo optimization), memory allocators require block sizes to be aligned to certain values, SIMD operations require vector lengths to be multiples of 4/8/16, protocol parsers require buffers to be at least large enough to hold a complete frame — in all these scenarios, "is the type correct" and "does the value comply" are often intertwined. In the past, when I encountered this situation, I'd either `assert` at runtime, or scatter a bunch of `static_assert` inside various function bodies in the template. Now with value-parameterized concepts, you can centralize all constraints in one place and express them clearly right at the interface signature.

At this point, I finally understand why the perspective that "concepts are compile-time functions" is so important. If you treat them as "labels for template parameters," your thinking gets trapped in the box of "one concept constrains one type." But if you treat them as functions — capable of accepting multiple parameters, accepting value parameters, calling other compile-time functions, and being composed — then their expressive power is almost as strong as ordinary code, except the entire execution process happens at compile time.

---

# Determining Powers of Two: From a Small Algorithm to the Love-Hate Relationship Between Generic and Object-Oriented Programming

## A Small Algorithm That Made Me Slap My Thigh

A couple of days ago, I was working on a very basic problem: determining whether an integer is a power of two. I had always used the most naive approach — repeatedly dividing by 2 and checking the remainder, or the slightly more "advanced" method of using logarithms. But this time I saw a bitwise approach, and honestly, when I saw it, I thought it was incredibly clever because the logic is just so clean.

The idea is this: if a number is a power of two, its binary representation must have exactly one 1, with all other bits being 0. For example, 8 is `1000`, and 32 is `100000`. So you just keep right-shifting, discarding the last bit while checking whether the discarded bit is a 1. If you shift down to exactly one 1 remaining, it's a power of two; if you encounter any non-zero bit along the way, return false immediately; if you finish shifting and find all zeros, then 0 itself is not a power of two either, so also return false.

I had always thought that the bitwise check for powers of two was just that classic `n & (n - 1) == 0` one-liner, but that approach has a pitfall — it also judges 0 as true, so you need an extra `n != 0` check. The shifting approach, while a few lines longer, is completely self-consistent in its logic and doesn't need any special cases. I casually wrote a verification:

```cpp
#include <iostream>
#include <bitset>

// 用右移来判断是不是 2 的幂
// 思路：2 的幂的二进制表示有且仅有一个 1
bool is_power_of_two_shift(unsigned int n) {
    if (n == 0) return false;  // 0 不是 2 的幂

    int count = 0;
    while (n > 0) {
        // 检查最后一位是不是 1
        if (n & 1u) {
            count++;
            if (count > 1) return false;  // 超过一个 1，不是 2 的幂
        }
        n >>= 1;  // 右移，扔掉最后一位
    }
    return count == 1;
}

// 经典的 n & (n-1) 写法，注意要排除 0
bool is_power_of_two_classic(unsigned int n) {
    return n != 0 && (n & (n - 1)) == 0;
}

int main() {
    unsigned int test_values[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 31, 32, 63, 64, 127, 128, 255, 256};

    std::cout << "数值     二进制表示              移位法  经典法\n";
    std::cout << "----     ----------              ------  ------\n";
    for (unsigned int n : test_values) {
        std::cout << n << "\t" << std::bitset<8>(n) << "\t"
                  << (is_power_of_two_shift(n) ? "true " : "false")
                  << "  "
                  << (is_power_of_two_classic(n) ? "true " : "false")
                  << "\n";
    }
    return 0;
}
```

The results from both methods were completely consistent, but I find the shifting method's logic easier to follow, because its "intent" and "implementation" are perfectly aligned — it's simply counting how many 1s there are. While `n & (n - 1)` is clever, when you see it for the first time, you really have to think about why it clears the lowest set bit. That said, the classic approach is indeed better for performance, because it only needs one AND operation and one comparison, whereas the shifting method requires a loop. So in actual engineering, I'd still use the classic approach, but understanding the shifting method's logic is really helpful for building bit manipulation intuition.

## Generic Programming vs. Object-Oriented Programming: A Question I Struggled With for a Long Time

Having gone off on that small algorithm tangent, I want to discuss a bigger topic, because this content finally helped me clarify a concept that had always been fuzzy — what is the essential difference between generic programming and object-oriented programming?

When I first started learning C++ in 2022, I learned classes and inheritance first, and thought object-oriented programming was all there is to C++. Later, when I encountered templates, seeing a bunch of angle brackets and compilation errors gave me a headache, and I treated it as "dark magic" to be avoided if possible. Even later, when I started learning concepts, I gradually discovered that generic programming could do much more than I imagined, but a question always lingered in my mind: when should I use which?

Now I finally get it. The core difference comes down to one sentence: **generic programming is more flexible, and it doesn't rely on indirect function calls**.

This "not relying on indirect function calls" is crucial. Object-oriented polymorphism is implemented through virtual function tables (vtables). When you call a virtual function, the runtime must first look up the table and then jump — that's an indirect call. Generic programming, on the other hand, determines types at compile time, inlining what should be inlined and specializing what should be specialized, generating code that's as direct as hand-written code. So generic programming is faster in most cases. This isn't mysticism — it's determined by the underlying mechanism.

## My Blood-and-Tears History of Trying to Design a Container Base Class

Speaking of the limitations of object-oriented programming, I have to vent about a pitfall I fell into myself. I previously worked on a small project where I wanted to uniformly manage different types of containers, so I naturally thought: I'll define a `Container` base class, and then have `MyList` and `MyVector` inherit from it.

```cpp
// 我当时写的"理想"代码，但从来没能真正跑通
class Container {
public:
    virtual int size() const = 0;
    virtual void push_back(int value) = 0;
    virtual int& operator[](int index) = 0;
    virtual void insert(int pos, int value) = 0;
    virtual void erase(int pos) = 0;
    // ...
};
```

Looks great, right? But it fell apart as soon as I started writing. The behavior details of `insert` in `std::list` and `insert` in `std::vector` are different. `std::list` has `splice` but `std::vector` doesn't have it at all. `std::vector` has `reserve` but `std::list` has no use for it. I tried to find a "common interface" in the base class to cover the operation sets of all containers, but their operation sets are fundamentally different, and the constraints on them are different too.

I struggled for days, and in the end, I either had to design the interface to be large and comprehensive (with a bunch of methods that simply threw "not supported" exceptions in certain subclasses), or small and fragmented (leaving only a `size()`, at which point what's the point of the base class?). Later I gave up and switched to using template functions to handle containers, and discovered that things became unexpectedly simple:

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>
#include <print>

// 对 vector 的约束：需要随机访问，可以做 reserve
template <typename T>
concept RandomAccessContainer = requires(T t, typename T::value_type v, size_t n) {
    { t.size() } -> std::convertible_to<size_t>;
    { t[n] } -> std::same_as<typename T::reference>;
    { t.reserve(n) };
};

// 对 list 的约束：不需要随机访问，但需要 push_back 和 splice 能力
// 注意这里的 !RandomAccessContainer<T> —— 因为 std::vector 同时满足两个 concept，
// 不加互斥条件的话，调用 process_container(vec) 会导致重载歧义
template <typename T>
concept SequenceContainer = !RandomAccessContainer<T> && requires(T t, typename T::value_type v) {
    { t.size() } -> std::convertible_to<size_t>;
    { t.push_back(v) };
    { t.front() } -> std::same_as<typename T::reference>;
};

// 针对随机访问容器的处理
void process_container(const RandomAccessContainer auto& c) {
    std::println("处理随机访问容器，大小: {}", c.size());
    // 可以用下标访问
    if (!c.empty()) {
        std::println("  第一个元素: {}", c[0]);
    }
}

// 针对序列容器的处理
void process_container(const SequenceContainer auto& c) {
    std::println("处理序列容器，大小: {}", c.size());
    // 用 front() 访问
    if (!c.empty()) {
        std::println("  第一个元素: {}", c.front());
    }
}

int main() {
    std::vector<int> vec{1, 2, 3, 4, 5};
    std::list<int> lst{10, 20, 30};

    process_container(vec);  // 匹配 RandomAccessContainer
    process_container(lst);  // 匹配 SequenceContainer

    return 0;
}
```

See? With concepts, I can impose different requirements on different types of containers without forcing them into a unified base class. Does vector need to support `operator[]` and `reserve`? Then write a concept that requires those. Does list not need random access? Then write another concept. Each satisfies its own constraints and goes through its own function overload. Looking back, the principle is actually simple — **don't try to force everything into a fixed interface; instead, match the most appropriate processing method based on the type's own capabilities**.

## They Are Not Enemies, They Are Partners

But I must emphasize one point: don't go and completely dismiss object-oriented programming just because I said generic programming is good. Object-oriented programming has a scenario that's hard for generic programming to replace: **open type sets**. What's an open type set? It's when you're writing code and have no idea what types will be added in the future. For example, in a GUI framework's drawing system, you define a `Shape` base class with a `draw()` virtual function. Then users can write a `MyCustomShape` in their own code that inherits from `Shape`, and your framework code can handle this new type without recompilation. This "runtime extension" capability is something generic programming cannot achieve, because templates must know all types at compile time.

So my understanding is: **if you can enumerate all types (or at least know them at compile time), use generic programming for better performance and more precise expression; if you need runtime dynamic type extension, use object-oriented polymorphism**. They are complementary, not mutually exclusive.

## draw-all: The Same Problem, Both Approaches Work

To verify this understanding, I wrote a classic "draw all shapes" example, implementing it with both object-oriented and generic programming approaches:

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <concepts>
#include <print>

// ============ 面向对象的方式 ============
class Shape {
public:
    virtual ~Shape() = default;
    virtual void draw() const = 0;
};

class Circle : public Shape {
public:
    void draw() const override {
        std::println("OOP: 绘制圆形");
    }
};

class Rectangle : public Shape {
public:
    void draw() const override {
        std::println("OOP: 绘制矩形");
    }
};

// 面向对象的 draw_all：接受 Shape 指针的 range
void draw_all_oop(const std::vector<std::unique_ptr<Shape>>& shapes) {
    for (const auto& s : shapes) {
        s->draw();  // 虚函数调用，间接调用
    }
}

// ============ 泛型编程的方式 ============

// 不需要继承任何基类的图形类型
struct Triangle {
    void draw() const {
        std::println("Generic: 绘制三角形");
    }
};

struct Star {
    void draw() const {
        std::println("Generic: 绘制五角星");
    }
};

// 定义 concept：只要你有 draw 成员函数就行
template <typename T>
concept Drawable = requires(const T& t) {
    { t.draw() };
};

// 泛型的 draw_all：接受任何有 draw() 的类型的 range
template <std::ranges::range R>
    requires Drawable<std::ranges::range_value_t<R>>
void draw_all_generic(const R& items) {
    for (const auto& item : items) {
        item.draw();  // 直接调用，编译期确定，可以内联
    }
}

int main() {
    std::println("=== 面向对象方式 ===");
    std::vector<std::unique_ptr<Shape>> oop_shapes;
    oop_shapes.push_back(std::make_unique<Circle>());
    oop_shapes.push_back(std::make_unique<Rectangle>());
    draw_all_oop(oop_shapes);

    std::println("\n=== 泛型编程方式 ===");
    std::vector<Triangle> triangles{Triangle{}, Triangle{}};
    std::vector<Star> stars{Star{}};
    draw_all_generic(triangles);
    draw_all_generic(stars);

    // 关键点：泛型方式也能处理有虚函数的类型！
    std::println("\n=== 泛型方式处理 OOP 类型 ===");
    std::vector<Circle> circles{Circle{}, Circle{}};
    draw_all_generic(circles);  // 完全可以，Circle 有 draw()

    return 0;
}
```

Let's run it and see the output:

```text
=== 面向对象方式 ===
OOP: 绘制圆形
OOP: 绘制矩形

=== 泛型编程方式 ===
Generic: 绘制三角形
Generic: 绘制三角形
Generic: 绘制五角星

=== 泛型方式处理 OOP 类型 ===
OOP: 绘制圆形
OOP: 绘制圆形
```

Note the last example — `draw_all_generic` is a generic function, but it can perfectly handle `Circle`, an object-oriented type with virtual functions, because `Circle` indeed has a `draw()` method, satisfying the `Drawable` concept. In other words, **generic programming with concepts can cover everything that classic object-oriented class hierarchies can do**, while also being able to handle types that don't belong to any class hierarchy at all (like `Triangle` and `Star`, which don't inherit from any base class).

At this point, I finally got it all straightened out. I used to think templates and concepts were "advanced tricks," while virtual functions and polymorphism were the "orthodox" approach. Looking back now, generic programming's expressive power is actually stronger, and because it doesn't require indirect calls, its performance is better too. But object-oriented programming确实 has its irreplaceability when dealing with open type sets. The two are complementary — choose based on the scenario. That's the right way to approach it.

---

# Concepts Don't Need to Be Perfect on the First Try — Iterative Practice Makes Them More Precise

This statement is a good analogy — your LLM is overthinking. Before you even start, you frantically make assumptions, attempting to use computation to describe an essentially uncertain world. The result is that every time you want to write a concept, you stare at the screen for ages, thinking "am I missing some constraint condition," and end up not writing a single line of code.

Many people initially think that a concept is like a "contract" in the type system — once signed, it can't be changed, so you must enumerate all constraints when writing it. For example, if I want to constrain a "numeric type," I start agonizing: should I add `std::is_copy_constructible`? Should I add `std::is_default_constructible`? Should I add `std::is_trivially_destructible`? The more I think, the more I add, until I scare myself away.

But in reality, concepts are just like writing ordinary code — the first version is meant to "just get it working." You don't need to consider all edge cases on day one. Write down the constraints you actually need right now, and add more later when you find they're insufficient. That's perfectly fine.

## Writing a Number Concept from Scratch

For the complete implementation and in-depth discussion of `Number<T>`, please refer to [Part 1](01-type-safety-and-number-concept.md). Here I only want to show the core skeleton of this concept, using it to illustrate the philosophy of "iterative evolution":

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>

// 第一版：只约束我当前真正用到的操作
// 不加拷贝、不加移动、不加默认构造——那些我暂时不需要
template<typename T>
concept Number = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
    { a - b } -> std::convertible_to<T>;
    { a * b } -> std::convertible_to<T>;
    { a / b } -> std::convertible_to<T>;
    { -a }    -> std::convertible_to<T>;
};

// 一个只用了加减法的函数——它只需要 Number 的部分能力
template<Number T>
T compute(T x, T y) {
    return (x + y) * 2 - y;
}
```

See? This `Number` concept is missing a bunch of things: it doesn't constrain `==` and `!=`, doesn't constrain compound assignments like `+=`, doesn't constrain `<<` output — nothing. But for the `compute` function, it's already completely sufficient. If tomorrow I write a new function that needs to compare whether two numbers are equal, I can write a separate `EqualityComparable` concept to constrain that function's parameters, rather than going back and making `Number` increasingly bloated.

Suppose I later do need a more complete numeric concept. I can extend it based on the existing `Number`, rather than starting from scratch:

```cpp
// 第二版：在 Number 基础上扩展，需要比较能力的时候再加
template<typename T>
concept ComparableNumber = Number<T> && requires(T a, T b) {
    { a == b } -> std::convertible_to<bool>;
    { a != b } -> std::convertible_to<bool>;
    { a < b }  -> std::convertible_to<bool>;
    { a <= b } -> std::convertible_to<bool>;
    { a > b }  -> std::convertible_to<bool>;
    { a >= b } -> std::convertible_to<bool>;
};

template<ComparableNumber T>
T clamp(T val, T lo, T hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}
```

This "constrain what you use" approach is actually quite similar to the typeclass idea in functional programming — you define a minimal, orthogonal set of capability primitives, then compose them where needed, rather than creating a "God concept" that stuffs everything in from the start.

## The Worry of "Will It Match the Wrong Thing?"

I worried about this at first too: if my `Number` concept only checks for the presence of `+ - * /` operators, could there be some type that happens to have these operators but isn't a number at all, and then gets incorrectly matched?

The talk mentioned a classic example: `std::forward_iterator` and `std::input_iterator` are almost identical in terms of syntactic constraints. Their difference is mainly at the semantic level — a forward iterator guarantees that multiple traversals through the same iterator produce the same results, while an input iterator doesn't guarantee this. This difference cannot be expressed with pure syntactic constraints.

But let's be realistic. The probability of a type that happens to implement `+ - * /` with a return value convertible back to its own type, yet "isn't a number," is extremely low. If a type really does provide these five operators with perfectly matching signatures, then at the syntactic level it already behaves like a number. Even if its semantics are "matrix" or "polynomial," using it in a scenario where you only need addition, subtraction, multiplication, and division is fine.

Moreover, concept-constrained name lookup is much safer than unconstrained name lookup. When you use a concept to constrain a function template's parameters, the compiler only considers candidate functions that satisfy the concept during overload resolution. This is far more reliable than traditional SFINAE, which hides conditions in return types using `std::enable_if`, because concepts are explicit, named constraints. When the compiler reports an error, it directly tells you "this type does not satisfy Number," rather than giving you fifty lines of template instantiation errors.

## Complementary Relationship with OOP Hierarchical Constraints

Another point finally clicked for me: concepts provide "flat" capability constraints, while OOP class hierarchies provide "structured" hierarchical constraints. These two are not mutually exclusive — they are complementary.

For example, if you have a class hierarchy `Shape -> Circle / Rectangle`, that's structured with inheritance relationships. But you could also write a `concept Drawable = requires(T t, std::ostream& os) { { os << t } -> std::same_as<std::ostream&>; };` concept that doesn't care whether your type inherits from `Shape` — it only cares whether you can be output to a stream. A `Circle` can simultaneously satisfy "is a subclass of Shape" and "is Drawable," with these two constraints serving their respective purposes in different scenarios.

I used to think "either use OOP or use template generics, you must choose one." Looking back now, that mindset was too narrow. The tools in your toolbox aren't meant for you to pick just one.

---

# Concepts Are Not Just for Template Parameters — I Completely Overlooked This Point

To be honest, I was quite moved when I saw this part of the content. Because ever since I started learning C++ in 2022, I had a deeply rooted impression: concepts are for constraining template parameters, written inside `template <concept_name T>`, end of story. It turns out that concepts can be used completely independently of template parameters, on ordinary function parameters. This directly opened a door I hadn't even seen before.

## Let's Talk About That "Tail Wagging the Dog" Problem First

Before diving in, I want to mention a point that really resonated with me. We often fall into a backwards-thinking trap when discussing questions like "how to distinguish forward iterators from input iterators" — to distinguish these two things, we start racking our brains to invent various syntactic differences, like adding a tag to one of them, or adding a special member function, and then writing a concept to detect whether that tag exists. The entire design exists just to solve one specific problem, and it gets more and more complex.

The correct approach should actually be: first present the most elegant design for the general problem, and then if you really encounter a special case that needs distinguishing, apply a small trick as a patch. You can't put the cart before the horse.

## Starting with the Simplest Example: Concepts Constraining Ordinary Function Parameters

Let's start with a very basic example. Suppose I have a function that processes integers, and I want it to accept `short`, `int`, and `long` — the standard integer types — but not `float` or `double` — the floating-point types.

If you follow the traditional template approach, you might write it like this:

```cpp
#include <type_traits>
#include <iostream>

// 传统写法：用 std::enable_if 或者 static_assert
template <typename T>
void process_old(T val) {
    static_assert(std::is_integral_v<T>, "T must be an integral type");
    std::cout << "processing: " << val << "\n";
}

int main() {
    process_old(42);       // OK
    process_old(3.14);     // 编译失败，但错误信息又长又丑
}
```

I've written this pattern countless times before. The problem is the error message — what you see is a wad of failed `static_assert` template instantiation stacks, which looks like gibberish to beginners.

Now let's switch to the concept approach, but here's the key — **I don't have to write it as a template**:

```cpp
#include <concepts>
#include <iostream>

// 直接用 concept 约束普通函数的参数！
void process(std::integral auto val) {
    std::cout << "processing: " << val << "\n";
}

int main() {
    process(42);       // OK，int 满足 std::integral
    process(42L);      // OK，long 满足 std::integral
    // process(3.14);  // 编译错误，double 不满足 std::integral
}
```

Did you notice? There's no `template` keyword here, no `typename T` — it's just a perfectly ordinary function, except the parameter type is written as `std::integral` instead of `int`. When the compiler sees the `std::integral` concept, it automatically treats it as a constraint and checks during overload resolution whether the passed type satisfies it.

When I first saw this pattern, everything clicked — so concepts can be used like this! This is essentially generic programming's syntax moving closer to ordinary programming. When writing functions, your mindset shifts from "I need to write a template" to "I need to write a function whose parameter type is a concept." This psychological shift was very important for me.

Of course, you can also write it in template form, and the effect is equivalent:

```cpp
#include <concepts>
#include <iostream>

// 模板写法，效果一样
template <std::integral T>
void process_template(T val) {
    std::cout << "processing: " << val << "\n";
}

// 非模板写法
void process_plain(std::integral auto val) {
    std::cout << "processing: " << val << "\n";
}

int main() {
    process_template(42);
    process_plain(42);
    // 两个调用在编译器内部的处理方式几乎一样
}
```

The difference is not fundamental in most scenarios — the compiler's underlying overload resolution is the same. But the non-template approach has a psychological benefit: when you read the code, the first thing you see is an ordinary function. You don't need to first run through in your head "this is a template, what will T be deduced as." The code's intent is more straightforward.

There is one small pitfall I should warn you about, though. If you use the non-template approach, you can't use the name `T` in the function body, because you never declared a `T`. You need to use `decltype` or `auto`:

```cpp
#include <concepts>
#include <iostream>
#include <typeinfo>

void process(std::integral auto val) {
    // 这里没有 T，所以需要用 auto 或 decltype
    auto doubled = val * 2;
    std::cout << "type: " << typeid(doubled).name()
              << ", value: " << doubled << "\n";
}

int main() {
    process(42);   // 传入 int，doubled 也是 int
    process(42L);  // 传入 long，doubled 也是 long
}
```

## The Scenario That Truly Enlightened Me: Infrastructure Needs in Industrial Code

The integer example above is too simple — you might think "that's nothing special." What really made me understand the value of this feature was the industrial software scenario mentioned in the talk.

When I interned at a larger C++ project, I had a very strong impression: **production code and teaching code are completely different things**. The textbook `advance` function is just three or four lines — advance the iterator by n steps, clean and simple. But the actual project's `advance`, or similar core functions like `advance`, were stuffed with a lot of things unrelated to the core logic — logging, debug assertions, correctness checks, telemetry data collection, call chain tracing... With every infrastructure need added, the function would bloat another layer.

Let's look at an example simulating this scenario. Suppose I have a simplified `advance` that advances an iterator by 2 steps:

```cpp
#include <iostream>
#include <vector>
#include <list>

// 教科书版本：干净但不够用
template <typename Iter>
void advance_by_2(Iter& it) {
    ++it;
    ++it;
}
```

Now, returning to the feature that concepts aren't limited to template parameters. If we constrain `advance_by_2`'s parameters using concepts, written in non-template form, we actually gain an important capability: **this function's "identity" in the type system becomes clearer**. It's no longer a template open to all types, but a function with a clear interface contract. This lays the foundation for subsequently using concepts for more precise dispatch and composition.

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>

// 用 concept 约束参数，明确表达"这个函数接受随机访问迭代器"
void advance_by_2(std::random_access_iterator auto& it) {
    it += 2;  // 随机访问迭代器可以直接 +=
}

// 同名函数，接受输入迭代器（只能一步步走）
// 注意：必须排除随机访问迭代器，否则 std::random_access_iterator<T> 满足时
// 两个重载都会匹配，导致歧义
template <typename T>
    requires std::input_iterator<T> && (!std::random_access_iterator<T>)
void advance_by_2(T& it) {
    ++it;
    ++it;
}

int main() {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::list<int> lst = {1, 2, 3, 4, 5};

    auto vit = vec.begin();
    auto lit = lst.begin();

    advance_by_2(vit);  // 调用随机访问版本
    advance_by_2(lit);  // 调用输入迭代器版本

    std::cout << *vit << "\n";  // 输出 3
    std::cout << *lit << "\n";  // 输出 3
}
```

Here, the first function uses the `std::random_access_iterator auto&` shorthand syntax (a concept shorthand form allowed in C++20). The second function, because it needs to exclude random-access iterators (to avoid ambiguity from both matching simultaneously), uses the full template + `requires` syntax, adding `!std::random_access_iterator<T>` to the constraint to ensure mutual exclusivity. Two functions with the same name achieve overloading through different concept constraints — random-access iterators take the `+= 2` fast path, while ordinary input iterators take the slow path of two `++` calls. This is the more elegant overloading mechanism that concepts bring.

## A Previous Misunderstanding of Mine

Speaking of which, I must confess a previous misunderstanding. When I first learned concepts, I thought their greatest value was "making template error messages prettier." It's true that concept error messages are a hundred times better looking than `enable_if`, but if that's all you see, you're vastly underestimating concepts.

The true value of concepts lies in **the shift they bring to generic programming's way of thinking**. When writing templates before, my mindset was "I need a type parameter here, let me add a constraint." Now with concepts, my mindset has become "I need something that satisfies a certain semantic requirement here." From "type parameter" to "semantic need," this shift seems subtle, but it actually affects your entire design.

Take the `advance_by_2` example above — I didn't write "a template function that accepts a `T`," but rather "a function that accepts a random-access iterator" and "a function that accepts an input iterator." The code's intent is elevated from the implementation detail level to the semantic level.

## The Misconception About "Isolated Compilation"

Many people (including the speaker initially) believe that generic functions must be able to compile in isolation — that is, looking at only the function definition itself, without the call site's context, type checking should be completable. But later they realized this is neither what we truly need nor what concepts provide.

I had this misconception too. I felt that a good generic function should be "self-contained," able to prove on its own that its requirements on types are reasonable. But think about
