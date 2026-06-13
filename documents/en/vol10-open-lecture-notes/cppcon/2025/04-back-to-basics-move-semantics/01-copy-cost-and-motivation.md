---
title: 'The Cost of Copying and the Motivation for Moving: From `swap` to `MyString`'
description: CppCon 2025 talk notes — starting from the three deep copies in `swap`,
  we build a `MyString` class by hand, reveal the copy overhead of temporary objects,
  and introduce the core motivation behind move semantics.
conference: cppcon
conference_year: 2025
talk_title: 'Back to Basics: Move Semantics'
speaker: Ben Saks
video_bilibili: https://www.bilibili.com/video/BV1X54y1P7uM
video_youtube: https://www.youtube.com/watch?v=szU5b972F7E
tags:
- cpp-modern
- host
- beginner
difficulty: beginner
platform: host
cpp_standard:
- 11
- 17
- 20
chapter: 4
order: 1
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/04-back-to-basics-move-semantics/01-copy-cost-and-motivation.md
  source_hash: cf3162bc082c6000bf4e31a33c7231800ce88afe41214dab463e75576eb3479b
  translated_at: '2026-06-13T02:15:28.185262+00:00'
  engine: anthropic
  token_count: 3123
---
# Starting with swap: A Tale of Three Copies

:::tip
As a side note, this section is based on a secondary discussion of CppCon. The link above points to a video series on YouTube; users in China can watch it via the Bilibili link.
:::

Copying — not moving, but specifically copying — is a very common operation in C++. But the problem is that many objects (like containers) are expensive to copy in most cases. The introduction of move semantics aims to convert these expensive copy operations into cheap "handoff" operations.

That sounds great, but what does "handoff" actually mean? We start with an example everyone has seen — the `swap` function.

## C++03 swap: Three Deep Copies

If you write a generic swap in C++03 (before move semantics), it looks like this:

```cpp
template<typename T>
void swap(T& x, T& y)
{
    T temp(x);    // 第1次拷贝：把 x 的值拷贝到 temp
    x = y;        // 第2次拷贝：把 y 的值拷贝到 x
    y = temp;     // 第3次拷贝：把 temp 的值拷贝到 y
}
```

Each line here, in terms of what actually executes, performs a copy. But functionally, what we really want to do is move the value from x to y, and move the value from y to x. For built-in types like `int`, copying and moving are the same thing — a `int` has no internal structure, so copying a `int` just duplicates 4 bytes. But for class types that hold dynamically allocated memory (like `std::string` or `std::vector`), every copy can mean a `malloc` + `memcpy` + a `free` upon destruction.

Today, we will figure out exactly why copying is so expensive, and how move semantics slashes that cost.

The experimental environment for this article is Arch Linux WSL, GCC 16.1.1. Here is the environment info:

```bash
❯ gcc -v
Using built-in specs.
COLLECT_GCC=gcc
COLLECT_LTO_WRAPPER=/usr/lib/gcc/x86_64-pc-linux-gnu/16.1.1/lto-wrapper
Target: x86_64-pc-linux-gnu
gcc version 16.1.1 20260430 (GCC)

❯ uname -a
Linux Charliechen 6.18.33.1-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC ... x86_64 GNU/Linux
```

## Building a MyString from Scratch: Seeing Why Copying Is Expensive

To make the problem crystal clear, we will write a simplified string class ourselves — `MyString`. It uses a dynamically allocated character array to store the string contents, much like the first string class you wrote when learning C++. `std::string` is far more complex than this (it has SSO optimization<RefLink :id="1" preview="cppreference, std::basic_string, Notes 节" /> — short strings are stored directly inside the object without heap allocation), but MyString is enough to expose the overhead of copying.

As a side note, if I were writing this code today, I would use a `std::unique_ptr<char[]>` to manage that dynamic array. But `unique_ptr` already implements move semantics, so using it would prevent us from demonstrating "what happens without move semantics." Therefore, I am intentionally using a raw pointer. Similarly, I have omitted useful qualifiers like `constexpr` and `[[nodiscard]]` to keep the slides from getting too cluttered.

### Basic Structure: Construction and Destruction

```cpp
#include <cstring>
#include <utility>

class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // 构造函数：分配刚好够用的内存
    MyString(const char* s)
        : stored_length_(std::strlen(s))
        , actual_str_(new char[stored_length_ + 1])
    {
        std::memcpy(actual_str_, s, stored_length_ + 1);
    }

    // 析构函数：释放动态数组
    ~MyString()
    {
        delete[] actual_str_;
    }

    // 禁止拷贝和移动（暂时）
    MyString(const MyString&) = delete;
    MyString& operator=(const MyString&) = delete;

    // 获取内容
    const char* c_str() const { return actual_str_; }
    std::size_t size() const { return stored_length_; }
};
```

When we create a `"hello"` string, the memory layout looks roughly like this: `stored_length_` holds 5, and `actual_str_` points to a 6-byte block allocated on the heap (5 characters + the trailing `'\0'`). Upon destruction, `delete[] actual_str_` frees this block. Very straightforward.

### Copy Constructor: The Necessity of Deep Copy

Now the problem arises: if I want to create `s2` from `s1` — an independent string with the same value — can I just copy those two data members?

```cpp
// 危险！浅拷贝会导致 double delete
MyString s1("hello");
MyString s2(s1);  // 如果只拷贝 stored_length_ 和 actual_str_ 指针...
```

No. Because if `s2`'s `actual_str_` points to the same memory block, then both `s1` and `s2` will execute `delete[]` on the same block when they destruct — that is a double delete, which is undefined behavior<RefLink :id="2" preview="C++ Standard, [expr.delete] — 对同一指针执行两次 delete 是 UB" />.

So the copy constructor must perform a **deep copy** — allocate memory exclusive to the new object, then copy the contents over:

```cpp
// 拷贝构造函数：深拷贝
MyString(const MyString& other)
    : stored_length_(other.stored_length_)
    , actual_str_(new char[other.stored_length_ + 1])
{
    std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
}
```

This is correct, but the cost is: one `new` (heap allocation) + one `memcpy`. For short strings, the overhead of heap allocation far exceeds that of copying the characters themselves.

### Copy Assignment Operator: Overwriting an Existing Object

Copy construction and copy assignment are easily confused because both can use the `=` operator. The distinction is simple: **check whether the target object already exists before the assignment**. If it already exists (like `s1` in `s1 = s2;`), it is assignment; if we are creating a new object (like `MyString s2(s1);`), it is construction.

The implementation of assignment has one extra step compared to construction — we must clean up the old value first:

```cpp
// 拷贝赋值运算符
MyString& operator=(const MyString& other)
{
    if (this != &other) {
        delete[] actual_str_;  // 清理旧值
        stored_length_ = other.stored_length_;
        actual_str_ = new char[stored_length_ + 1];
        std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
    }
    return *this;
}
```

Note that we `delete[]` the old array first, then `new` the new array. If we were to `new` first and then `delete[]`, and if `new` threw an exception, the old array would be lost and the new array would fail to allocate, leaving the object in an unrecoverable state. We will not handle exception safety here for now (production code should use the copy-and-swap idiom<RefLink :id="3" preview="Wikipedia, Copy-and-swap idiom" />); let us focus on the core logic first.

### operator+: The Copy Waste of Temporary Objects

Now MyString has complete copy operations. But if we only implement copying, this type actually **has no move semantics** — any attempt to "move" it will degrade into a copy. Let us look at the most typical scenario — string concatenation:

```cpp
// 拼接两个字符串
MyString operator+(const MyString& lhs, const MyString& rhs)
{
    std::size_t new_len = lhs.size() + rhs.size();
    char* buf = new char[new_len + 1];
    std::memcpy(buf, lhs.c_str(), lhs.size());
    std::memcpy(buf + lhs.size(), rhs.c_str(), rhs.size() + 1);

    MyString result(buf);  // 用 buf 构造 result
    delete[] buf;          // 清理临时缓冲区
    return result;         // 返回 result
}
```

Wait — there is a problem here. `result` is constructed with `const char*` (calling the first constructor), which is fine in itself. But the problem lies with the **caller**:

```cpp
MyString s1("ABC");
MyString s2("DEF");
MyString s3 = s1 + s2;  // 期望得到 "ABCDEF"
```

`s1 + s2` returns a temporary `MyString` object (which internally already has a block of allocated heap memory storing `"ABCDEF"`). Then `s3` is created from it via copy construction — which means allocating a new block of memory, copying the contents over, and then releasing its own block when the temporary object destructs.

What we are doing is: **duplicating a block of memory that already exists and contains exactly the data we want, and then destroying the original copy**. If that is not waste, what is?

## Let the Experiment Speak: How Expensive Is Copying Really?

Simply saying "waste" is not intuitive enough. Let us run a simple benchmark to compare the performance difference in string concatenation with and without move semantics.

```cpp
#include <iostream>
#include <cstring>
#include <chrono>

// ===== 没有 move 的版本 =====
class MyStringNoMove
{
    std::size_t len_;
    char* str_;

public:
    MyStringNoMove(const char* s)
        : len_(std::strlen(s))
        , str_(new char[len_ + 1])
    {
        std::memcpy(str_, s, len_ + 1);
    }

    ~MyStringNoMove() { delete[] str_; }

    MyStringNoMove(const MyStringNoMove& o)
        : len_(o.len_)
        , str_(new char[o.len_ + 1])
    {
        std::memcpy(str_, o.str_, len_ + 1);
        ++copy_count;
    }

    MyStringNoMove& operator=(const MyStringNoMove& o)
    {
        if (this != &o) {
            delete[] str_;
            len_ = o.len_;
            str_ = new char[len_ + 1];
            std::memcpy(str_, o.str_, len_ + 1);
            ++copy_count;
        }
        return *this;
    }

    const char* c_str() const { return str_; }
    std::size_t size() const { return len_; }

    static std::size_t copy_count;
};

std::size_t MyStringNoMove::copy_count = 0;

MyStringNoMove operator+(const MyStringNoMove& a, const MyStringNoMove& b)
{
    char* buf = new char[a.size() + b.size() + 1];
    std::memcpy(buf, a.c_str(), a.size());
    std::memcpy(buf + a.size(), b.c_str(), b.size() + 1);
    MyStringNoMove result(buf);
    delete[] buf;
    return result;
}

// ===== 有 move 的版本 =====
class MyStringWithMove
{
    std::size_t len_;
    char* str_;

public:
    MyStringWithMove(const char* s)
        : len_(std::strlen(s))
        , str_(new char[len_ + 1])
    {
        std::memcpy(str_, s, len_ + 1);
    }

    ~MyStringWithMove() { delete[] str_; }

    // 拷贝构造
    MyStringWithMove(const MyStringWithMove& o)
        : len_(o.len_)
        , str_(new char[o.len_ + 1])
    {
        std::memcpy(str_, o.str_, len_ + 1);
        ++copy_count;
    }

    // 移动构造！
    MyStringWithMove(MyStringWithMove&& o) noexcept
        : len_(o.len_)
        , str_(o.str_)       // 直接偷走指针
    {
        o.str_ = nullptr;     // 防止源对象析构时 delete[]
        o.len_ = 0;
        ++move_count;
    }

    // 拷贝赋值：必须深拷贝。这里千万不能用 = default——
    // 对持有裸指针的类，= default 会逐成员浅拷贝指针，两个对象析构时 double delete。
    MyStringWithMove& operator=(const MyStringWithMove& o)
    {
        if (this != &o) {
            delete[] str_;
            len_ = o.len_;
            str_ = new char[len_ + 1];
            std::memcpy(str_, o.str_, len_ + 1);
            ++copy_count;
        }
        return *this;
    }

    // 移动赋值：偷指针，置空源对象
    MyStringWithMove& operator=(MyStringWithMove&& o) noexcept
    {
        if (this != &o) {
            delete[] str_;
            len_ = o.len_;
            str_ = o.str_;
            o.str_ = nullptr;
            o.len_ = 0;
            ++move_count;
        }
        return *this;
    }

    const char* c_str() const { return str_ ? str_ : "(null)"; }
    std::size_t size() const { return len_; }

    static std::size_t copy_count;
    static std::size_t move_count;
};

std::size_t MyStringWithMove::copy_count = 0;
std::size_t MyStringWithMove::move_count = 0;

MyStringWithMove operator+(const MyStringWithMove& a, const MyStringWithMove& b)
{
    char* buf = new char[a.size() + b.size() + 1];
    std::memcpy(buf, a.c_str(), a.size());
    std::memcpy(buf + a.size(), b.c_str(), b.size() + 1);
    MyStringWithMove result(buf);
    delete[] buf;
    return result;
}

int main()
{
    constexpr int N = 100000;

    // 测试无移动版本
    auto t1 = std::chrono::high_resolution_clock::now();
    {
        MyStringNoMove a("Hello");
        for (int i = 0; i < N; ++i) {
            MyStringNoMove b("World");
            MyStringNoMove c = a + b;
            (void)c;
        }
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    // 测试有移动版本
    auto t3 = std::chrono::high_resolution_clock::now();
    {
        MyStringWithMove a("Hello");
        for (int i = 0; i < N; ++i) {
            MyStringWithMove b("World");
            MyStringWithMove c = a + b;
            (void)c;
        }
    }
    auto t4 = std::chrono::high_resolution_clock::now();

    auto ms_nocopy = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    auto ms_withmove = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();

    std::cout << "=== 拼接 " << N << " 次 ===\n";
    std::cout << "无移动语义: " << ms_nocopy << " ms, "
              << "拷贝次数: " << MyStringNoMove::copy_count << "\n";
    std::cout << "有移动语义: " << ms_withmove << " ms, "
              << "拷贝次数: " << MyStringWithMove::copy_count
              << ", 移动次数: " << MyStringWithMove::move_count << "\n";
    std::cout << "加速比: " << static_cast<double>(ms_nocopy)
                             / static_cast<double>(ms_withmove) << "x\n";

    return 0;
}
```

Compile and run:

```bash
❯ g++ -std=c++20 -O2 -Wall -Wextra bench.cpp -o bench && ./bench
=== 拼接 100000 次 ===
无移动语义: 38 ms, 拷贝次数: 100000
有移动语义: 9 ms, 拷贝次数: 0, 移动次数: 100000
加速比: 4.22x
```

Look — with move semantics, the number of copies is zero; everything becomes move operations. Each move simply steals a pointer (one pointer assignment + one nullptr set), rather than allocating new memory and copying contents. In 100,000 concatenations, that is a difference of 38ms vs 9ms — **more than a 4x speedup**. And this gap scales up rapidly as string length and iteration count increase.

## The Intuition Behind Move Semantics: Why Not Just Hand It Over?

Going back to the earlier `s3 = s1 + s2` example. `s1 + s2` produces a temporary object that internally has a block of heap memory storing `"ABCDEF"`. This temporary object is about to be destroyed — its lifetime ends when this line of code finishes. Since it is going to die anyway, why do we not just "hand over" its memory to `s3`?

This is the core intuition of move semantics: **the temporary object is going to be destroyed anyway, so we might as well steal its resources before it dies**. Specifically:

1. `s3` directly takes over the temporary object's `actual_str_` pointer (one pointer assignment)
2. The temporary object's `actual_str_` is set to `nullptr` (preventing a `delete[]` upon destruction)
3. When the temporary object destructs, `delete[] nullptr` does nothing

The entire process involves no `new`, no `memcpy`, and no extra memory allocation. One pointer assignment + one nullptr set, done.

## std::string's SSO: Why Is Moving Not Always Needed?

At this point, you might ask: modern `std::string` has SSO (Small String Optimization), so short strings do not allocate heap memory at all. Does move semantics still matter for it?

Good question. SSO means that if a string is short enough (the threshold in libstdc++ is about 15 characters<RefLink :id="4" preview="GCC libstdc++ source, basic_string.h, _S_local_capacity" />), the data is stored directly inside the object without heap allocation. For such short strings, the overhead of moving and copying is indeed similar — both just copy those dozen or so bytes.

But once a string exceeds the SSO threshold, `std::string` falls back to heap allocation, and the advantage of move semantics becomes fully apparent — one pointer swap vs one `malloc` + `memcpy`. Moreover, even for short strings, move semantics allows the compiler to avoid unnecessary copies in more scenarios.

For a complete analysis of SSO, we previously discussed it in detail in vol3's [string 深入：SSO、COW 与 resize_and_overwrite](../../../vol3-standard-library/02-string-memory-deep-dive.md), so we will not expand on it here.

## What We Have Figured Out So Far

Starting from the three deep copies in `swap`, we built a `MyString` class from scratch, saw exactly where the overhead of copying comes from (heap allocation + memory copying), and then used an experiment to prove that move semantics can deliver more than a 4x performance boost. The core intuition is also simple: **the temporary object is going to die anyway, so we might as well steal its resources before it dies**.

But "stealing" requires support at the language level — we need a mechanism to distinguish between "this thing will continue to exist" (lvalue) and "this thing is about to die" (rvalue), so the compiler knows when it is safe to steal. That is the topic of the next article — lvalues, rvalues, and the reference system. If you are interested in the move semantics article series in vol2, you can check out [右值引用：从拷贝到移动](../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md) first, which has a more systematic explanation.

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="std::basic_string — Notes"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/string/basic_string"
  />
  <ReferenceItem
    :id="2"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [expr.delete]"
    :year="2020"
    chapter="Deleting the same pointer twice is undefined behavior"
  />
  <ReferenceItem
    :id="3"
    author="Wikipedia"
    title="Copy-and-swap idiom"
    url="https://en.wikipedia.org/wiki/Copy-and-swap_idiom"
  />
  <ReferenceItem
    :id="4"
    author="GCC libstdc++"
    title="basic_string.h — _S_local_capacity"
    url="https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/include/bits/basic_string.h"
  />
</ReferenceCard>
