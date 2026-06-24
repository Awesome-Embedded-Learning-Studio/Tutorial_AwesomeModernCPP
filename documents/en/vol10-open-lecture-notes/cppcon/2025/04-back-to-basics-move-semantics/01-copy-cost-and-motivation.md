---
chapter: 4
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: CppCon 2025 演讲笔记 —— 从 swap 的三次深拷贝出发，手搓 MyString 类，揭示临时对象的拷贝浪费，引出移动语义的核心动机
difficulty: beginner
order: 1
platform: host
reading_time_minutes: 13
speaker: Ben Saks
tags:
- cpp-modern
- host
- beginner
talk_title: 'Back to Basics: Move Semantics'
title: 'The Cost of Copying and the Motivation for Moving: From `swap` to `MyString`'
video_bilibili: https://www.bilibili.com/video/BV1X54y1P7uM
video_youtube: https://www.youtube.com/watch?v=szU5b972F7E
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/04-back-to-basics-move-semantics/01-copy-cost-and-motivation.md
  source_hash: afa45d02f798f955df5a78a39cbf30d5bd13c055fb024f717f04397b94bb4bc5
  translated_at: '2026-06-24T00:33:27.169368+00:00'
  engine: anthropic
  token_count: 3130
---
# Starting with `swap`: A Tale of Three Copies

:::tip
A quick note: this section is inspired by a CppCon talk. The link above points to a YouTube video series; users in China can watch via the Bilibili link.
:::

Copying—not moving, but specifically copying—is a very common operation in C++. However, the problem is that many objects (such as containers) are expensive to copy in most cases. Move semantics were introduced to convert these expensive copy operations into cheap "handovers."

Sounds great, but what exactly does a "handover" mean? Let's start with an example everyone has seen—the `swap` function.

## C++03 `swap`: Three Deep Copies

If you write a generic `swap` in C++03 (before move semantics), it looks like this:

```cpp
template<typename T>
void swap(T& x, T& y)
{
    T temp(x);    // 第1次拷贝：把 x 的值拷贝到 temp
    x = y;        // 第2次拷贝：把 y 的值拷贝到 x
    y = temp;     // 第3次拷贝：把 temp 的值拷贝到 y
}
```

From an operational standpoint, every line here performs a copy. However, functionally, what we really want to do is move the value from `x` to `y`, and from `y` to `x`. For built-in types like `int`, copying and moving are effectively the same thing—an `int` has no internal structure, so copying it just means duplicating four bytes. But for class types that hold dynamically allocated memory (like `std::string` or `std::vector`), every copy can imply a `malloc` + `memcpy` + `free` upon destruction.

Today, we will clarify why copying is so expensive, and how move semantics reduces this cost.

The experimental environment for this article is Arch Linux WSL, GCC 16.1.1. Here is the environment information:

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

## Hand-Rolling a MyString: Where Exactly is the Cost of Copying?

To make the problem crystal clear, let's implement a simplified string class ourselves—`MyString`. It stores string content using a dynamically allocated character array, much like the first string class you might have written when learning C++. `std::string` is far more complex (it features SSO optimization<RefLink :id="1" preview="cppreference, std::basic_string, Notes 节" />—where small strings are stored directly within the object, avoiding heap allocation), but `MyString` is sufficient to expose the overhead of copying.

By the way, if I were writing this code today, I would use `std::unique_ptr<char[]>` to manage that dynamic array. However, `unique_ptr` already implements move semantics, so using it would prevent us from demonstrating "what happens without move semantics." Therefore, I am intentionally using raw pointers. Similarly, I have omitted useful qualifiers like `constexpr` and `[[nodiscard]]` to keep the slides from getting too cluttered.

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

Creating a `"hello"` string results in a memory layout roughly like this: `stored_length_` holds 5, and `actual_str_` points to a block of 6 bytes allocated on the heap (5 characters plus the terminating `'\0'`). Upon destruction, `delete[] actual_str_` releases this memory. Very straightforward.

### Copy Constructor: The Necessity of Deep Copy

Now the question arises: if we want to create `s2` from `s1`—an independent string with the same value—can we simply copy these two data members?

```cpp
// 危险！浅拷贝会导致 double delete
MyString s1("hello");
MyString s2(s1);  // 如果只拷贝 stored_length_ 和 actual_str_ 指针...
```

No. If `s2`'s `actual_str_` pointed to the same memory block, both `s1` and `s2` would execute `delete[]` on that same memory upon destruction. This is double delete—undefined behavior<RefLink :id="2" preview="C++ Standard, [expr.delete] — deleting the same pointer twice is UB" />.

Therefore, the copy constructor must perform a **deep copy**—allocate dedicated memory for the new object and copy the contents over:

```cpp
// 拷贝构造函数：深拷贝
MyString(const MyString& other)
    : stored_length_(other.stored_length_)
    , actual_str_(new char[other.stored_length_ + 1])
{
    std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
}
```

This approach is correct, but it comes at a cost: one `new` (heap allocation) plus one `memcpy`. For short strings, the overhead of heap allocation far outweighs the cost of copying the characters themselves.

### Copy Assignment Operator: Overwriting an Existing Object

Copy constructors and copy assignment operators are easily confused because they both involve the `=` operator. The distinction is simple: **check if the target object already exists before the operation**. If it exists (like `s1` in `s1 = s2;`), it is assignment; if we are creating a new object (like in `MyString s2(s1);`), it is construction.

Implementing assignment involves one extra step compared to construction—we must clean up the old value first:

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

Note that we `delete[]` the old array before we `new` the new array. If we were to `new` first and `delete[]` later, and if `new` were to throw an exception, the old array would be lost and the new array would fail to allocate, leaving the object in an unrecoverable state. We will temporarily ignore exception safety here (production code should use the copy-and-swap idiom<RefLink :id="3" preview="Wikipedia, Copy-and-swap idiom" />), focusing on the core logic for now.

### operator+: Copy overhead of temporary objects

MyString now has complete copy operations. However, if we only implement copying, this type actually **lacks move semantics**—any attempt to "move" it will fall back to a copy. Let's look at a typical scenario: string concatenation:

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

Wait—there is an issue here. `result` is constructed with a `const char*` (invoking the first constructor), which is fine in itself. However, the problem lies with the **caller**:

```cpp
MyString s1("ABC");
MyString s2("DEF");
MyString s3 = s1 + s2;  // 期望得到 "ABCDEF"
```

`s1 + s2` returns a temporary `MyString` object (which already has a block of heap memory allocated inside, storing `"ABCDEF"`). Then, `s3` is created via the copy constructor—this means allocating a new block of memory, copying the contents over, and finally, releasing the temporary object's memory when it destructs.

What we are doing is: **copying a piece of existing data that is exactly what we want, and then destroying the original**. How is this not a waste?

## Let the experiment speak: How expensive is copying?

Simply saying "wasteful" isn't intuitive enough. Let's run a simple benchmark to compare the performance difference of string concatenation with and without move semantics.

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

Look at this—with move semantics, the number of copies is 0; everything turns into move operations. Each move simply steals a pointer (one pointer assignment + one `nullptr` set), rather than allocating new memory and copying content. In 100,000 concatenations, this is the difference between 38ms and 9ms—**more than a 4x speedup**. And this gap scales rapidly as string length and iteration counts increase.

## The Intuition Behind Move Semantics: Why Not Just Hand Over?

Let's return to the `s3 = s1 + s2` example. `s1 + s2` produces a temporary object that holds a block of heap memory storing `"ABCDEF"`. This temporary object is about to be destroyed—its lifetime ends at the conclusion of this statement. Since it's going to die anyway, why don't we just "hand over" its memory to `s3`?

This is the core intuition of move semantics: **temporary objects are going to be destroyed anyway, so we might as well steal their resources before they die**. Specifically:

1. `s3` directly takes over the temporary object's `actual_str_` pointer (one pointer assignment).
2. We set the temporary object's `actual_str_` to `nullptr` (to prevent `delete[]` during destruction).
3. When the temporary object is destructed, `delete[] nullptr` does nothing.

The whole process involves no `new`, no `memcpy`, and no extra memory allocation. One pointer assignment + one `nullptr` set, and we are done.

## std::string's SSO: Why Isn't Moving Always Necessary?

You might ask at this point: modern `std::string` has SSO (Small String Optimization), so short strings don't allocate heap memory at all. Does move semantics still matter for them?

Good question. SSO means that if a string is short enough (the libstdc++ threshold is about 15 characters<RefLink :id="4" preview="GCC libstdc++ source, basic_string.h, _S_local_capacity" />), the data is stored directly inside the object, and no heap memory is allocated. For such short strings, the overhead of moving and copying is indeed similar—both involve copying those few bytes.

However, once a string exceeds the SSO threshold, `std::string` falls back to heap allocation, and the advantage of move semantics is fully realized—a pointer swap versus a `malloc` + `memcpy`. Moreover, even for short strings, move semantics allows the compiler to omit unnecessary copies in more scenarios.

For a complete analysis of SSO, we previously discussed this in detail in vol3's [Deep Dive into string: SSO, COW, and resize_and_overwrite](../../../../vol3-standard-library/containers/04-string-memory-deep-dive.md), so we won't expand on it here.

## What We've Learned So Far

Starting from the three deep copies in `swap`, we hand-rolled a `MyString` class to visualize the source of copy overhead (heap allocation + memory copying), and used experiments to prove that move semantics can yield more than a 4x performance boost. The core intuition is simple: **temporary objects are going to die anyway, so we might as well steal their resources before they do**.

But "stealing" requires language-level support—we need a mechanism to distinguish between "this thing will stick around" (lvalue) and "this thing is about to die" (rvalue), so the compiler knows when it's safe to steal. This is the topic of the next article—lvalues, rvalues, and the reference system. If you are interested in the move semantics series in vol2, you can check out [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md), which provides a more systematic explanation.

<ReferenceCard title="References">
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
