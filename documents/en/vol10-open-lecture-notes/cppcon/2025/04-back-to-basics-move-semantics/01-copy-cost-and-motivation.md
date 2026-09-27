---
chapter: 4
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: CppCon 2025 talk notes — starting from swap's three deep copies, hand-rolling a MyString class, exposing the copy waste of temporary objects, and arriving at the core motivation for move semantics
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
  translated_at: '2026-09-26T15:55:30+00:00'
  engine: anthropic
  token_count: 6000
---
# Starting with `swap`: A Tale of Three Copies

:::tip
A quick aside: this part is our own further riff on a CppCon talk. The links above point to the video series on YouTube; readers in mainland China can watch via the Bilibili link instead.
:::

Copying—and here we mean copying specifically, not moving—is an extremely common operation in C++. The problem is that many objects (containers, for example) are expensive to copy in most cases. Move semantics was introduced to turn these expensive copy operations into cheap "handoffs."

Sounds lovely, but what does a "handoff" actually mean? Let's start from an example everyone has seen—the `swap` function.

## C++03 `swap`: Three Deep Copies

If you wrote a generic `swap` in C++03 (before move semantics existed), it looked like this:

```cpp
template<typename T>
void swap(T& x, T& y)
{
    T temp(x);    // 1st copy: copy x's value into temp
    x = y;        // 2nd copy: copy y's value into x
    y = temp;     // 3rd copy: copy temp's value into y
}
```

Every single line here, in terms of the operations actually performed, is doing a copy. Functionally, though, what we really want is to move the value in `x` into `y`, and the value in `y` into `x`. For built-in types like `int`, copying and moving are the same thing—an `int` has no internal structure, and copying one is just duplicating 4 bytes. But for class types that hold dynamically allocated memory (say `std::string` or `std::vector`), every copy can mean a `malloc` + `memcpy`, plus a `free` when the object is destroyed.

What we're going to nail down today is this: why copying is so expensive, and how move semantics cuts that cost down.

The environment for this article's experiments is Arch Linux WSL, GCC 16.1.1. Here is the environment info:

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

## Hand-Rolling a `MyString`: Where the Copy Cost Actually Comes From

To see the problem more clearly, let's write our own simplified string class—`MyString`. It stores the string contents in a dynamically allocated char array, much like the first string class you probably wrote while learning C++. `std::string` is far more complicated than this (it has the SSO optimization<RefLink :id="1" preview="cppreference, std::basic_string, Notes section" />—short strings are stored directly inside the object, with no heap allocation), but `MyString` is enough for us to see exactly what copying costs.

By the way, if I were writing this code today, I would manage that dynamic array with `std::unique_ptr<char[]>`. But `unique_ptr` already implements move semantics, and using it would make it impossible to demonstrate "what happens when there is no move semantics." So I'm deliberately using a raw pointer. Along the same lines, I've also left out useful qualifiers such as `constexpr` and `[[nodiscard]]`, to keep the slides from getting too cluttered.

### Basic Structure: Construction and Destruction

```cpp
#include <cstring>
#include <utility>

class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // Constructor: allocate just enough memory
    MyString(const char* s)
        : stored_length_(std::strlen(s))
        , actual_str_(new char[stored_length_ + 1])
    {
        std::memcpy(actual_str_, s, stored_length_ + 1);
    }

    // Destructor: release the dynamic array
    ~MyString()
    {
        delete[] actual_str_;
    }

    // Forbid copying and moving (for now)
    MyString(const MyString&) = delete;
    MyString& operator=(const MyString&) = delete;

    // Access the contents
    const char* c_str() const { return actual_str_; }
    std::size_t size() const { return stored_length_; }
};
```

Create a `"hello"` string and the memory layout looks roughly like this: `stored_length_` holds 5, and `actual_str_` points to a 6-byte block allocated on the heap (5 characters plus the terminating `'\0'`). When the object is destroyed, `delete[] actual_str_` releases that block. Perfectly straightforward.

### The Copy Constructor: Why a Deep Copy Is Necessary

Now the question arises: if I want to create `s2` from `s1`—an independent string with the same value—can I just copy those two data members?

```cpp
// Danger! A shallow copy leads to double delete
MyString s1("hello");
MyString s2(s1);  // if we only copied stored_length_ and the actual_str_ pointer...
```

No. If `s2`'s `actual_str_` ended up pointing at the same block of memory, then both `s1` and `s2` would execute `delete[]` on that same memory when destroyed. That is a double delete—undefined behavior<RefLink :id="2" preview="C++ Standard, [expr.delete] — deleting the same pointer twice is UB" />.

So the copy constructor must perform a **deep copy**—allocate memory that belongs exclusively to the new object, then copy the contents over:

```cpp
// Copy constructor: deep copy
MyString(const MyString& other)
    : stored_length_(other.stored_length_)
    , actual_str_(new char[other.stored_length_ + 1])
{
    std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
}
```

This is correct, but the price is one `new` (a heap allocation) plus one `memcpy`. For short strings, the overhead of the heap allocation far outweighs the cost of copying the characters themselves.

### The Copy Assignment Operator: Overwriting an Object That Already Exists

Copy construction and copy assignment are easy to confuse, because both can be written with the `=` sign. The distinction is simple: **look at whether the target object already exists before the assignment**. If it does (like `s1` in `s1 = s2;`), it is assignment; if a new object is being created (like in `MyString s2(s1);`), it is construction.

Implementing assignment takes one extra step compared to construction—the old value has to be cleaned up first:

```cpp
// Copy assignment operator
MyString& operator=(const MyString& other)
{
    if (this != &other) {
        delete[] actual_str_;  // clean up the old value
        stored_length_ = other.stored_length_;
        actual_str_ = new char[stored_length_ + 1];
        std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
    }
    return *this;
}
```

Note that we `delete[]` the old array first and only then `new` the new one. If we did it the other way around—`new` first, `delete[]` after—and `new` happened to throw, the old array would already be lost while the new one never got allocated, leaving the object in an unrecoverable state. We won't deal with exception safety here (production code should use the copy-and-swap idiom<RefLink :id="3" preview="Wikipedia, Copy-and-swap idiom" />); let's get the core logic straight first.

### `operator+`: The Copy Waste of Temporary Objects

`MyString` now has a complete set of copy operations. But if copying is all we implement, this type effectively **has no move semantics**—any attempt to "move" it degrades into a copy. Consider the most classic scenario: string concatenation.

```cpp
// Concatenate two strings
MyString operator+(const MyString& lhs, const MyString& rhs)
{
    std::size_t new_len = lhs.size() + rhs.size();
    char* buf = new char[new_len + 1];
    std::memcpy(buf, lhs.c_str(), lhs.size());
    std::memcpy(buf + lhs.size(), rhs.c_str(), rhs.size() + 1);

    MyString result(buf);  // construct result from buf
    delete[] buf;          // clean up the temporary buffer
    return result;         // return result
}
```

Wait—there's a problem here. `result` is constructed from a `const char*` (invoking the first constructor), which is fine in itself. The problem lies with the **caller**:

```cpp
MyString s1("ABC");
MyString s2("DEF");
MyString s3 = s1 + s2;  // expected to get "ABCDEF"
```

`s1 + s2` returns a temporary `MyString` object (which already owns an allocated block of heap memory holding `"ABCDEF"`). Then `s3` is created from it via the copy constructor—which means allocating another block of memory, copying the contents into it, and then releasing the temporary's own block when it is destroyed.

What we are doing is: **taking a block of data that already exists and is exactly what we want, copying it, and then destroying the original**. If that's not waste, what is?

## Let the Experiment Speak: Just How Expensive Copying Is

Simply calling it "waste" isn't very concrete. Let's run a simple benchmark comparing the performance of string concatenation with and without move semantics.

```cpp
#include <iostream>
#include <cstring>
#include <chrono>

// ===== The version without move =====
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

// ===== The version with move =====
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

    // Copy constructor
    MyStringWithMove(const MyStringWithMove& o)
        : len_(o.len_)
        , str_(new char[o.len_ + 1])
    {
        std::memcpy(str_, o.str_, len_ + 1);
        ++copy_count;
    }

    // Move constructor!
    MyStringWithMove(MyStringWithMove&& o) noexcept
        : len_(o.len_)
        , str_(o.str_)       // steal the pointer outright
    {
        o.str_ = nullptr;     // prevent delete[] when the source is destroyed
        o.len_ = 0;
        ++move_count;
    }

    // Copy assignment: must deep copy. Never use = default here — for a class
    // holding a raw pointer, = default shallow-copies the pointer member by member, and the two objects double delete at destruction.
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

    // Move assignment: steal the pointer, null out the source
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

    // Benchmark the no-move version
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

    // Benchmark the with-move version
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

And there it is—with move semantics, the copy count is 0; everything became move operations. Each move just steals a pointer (one pointer assignment plus one write of `nullptr`) instead of allocating fresh memory and copying the contents into it. Across 100,000 concatenations, that is the difference between 38 ms and 9 ms—**a speedup of more than 4x**. And the gap widens rapidly as strings get longer and iteration counts grow.

## The Intuition Behind Move Semantics: Why Not Just Hand It Over

Back to that `s3 = s1 + s2` example. `s1 + s2` produces a temporary object whose internal heap memory holds `"ABCDEF"`. That temporary is about to be destroyed—its lifetime ends when this statement finishes. Since it is dying anyway, why not just "hand over" its memory to `s3`?

This is the core intuition of move semantics: **a temporary object is going to be destroyed anyway, so we might as well steal its resources before it dies**. Concretely:

1. `s3` directly takes over the temporary's `actual_str_` pointer (one pointer assignment)
2. We set the temporary's `actual_str_` to `nullptr` (to prevent `delete[]` during destruction)
3. When the temporary object is destroyed, `delete[] nullptr` does nothing

The whole process involves no `new`, no `memcpy`, and no extra memory allocation. One pointer assignment plus one write of `nullptr`, done.

## `std::string` and SSO: Why Moving Isn't Always Necessary

At this point you might ask: modern `std::string` has SSO (Small String Optimization), so short strings never allocate heap memory at all—does move semantics still mean anything for it?

Good question. SSO means that if a string is short enough (libstdc++'s threshold is roughly 15 characters<RefLink :id="4" preview="GCC libstdc++ source, basic_string.h, _S_local_capacity" />), the data is stored directly inside the object and nothing is allocated on the heap. For such short strings, moving and copying really do cost about the same—either way, you're copying those dozen-odd bytes.

But once a string exceeds the SSO threshold, `std::string` falls back to heap allocation, and that's where the advantage of move semantics shows up in full—one pointer swap versus a `malloc` + `memcpy`. And even for short strings, move semantics lets the compiler skip unnecessary copies in more situations.

For a complete analysis of SSO, we covered it in detail back in vol3's [Deep Dive into string: SSO, COW, and resize_and_overwrite](../../../../vol3-standard-library/containers/04-string-memory-deep-dive.md), so we won't expand on it here.

## What We've Figured Out So Far

We started from `swap`'s three deep copies, hand-rolled a `MyString` class, saw exactly where the cost of copying comes from (heap allocation plus memory copying), and then proved with an experiment that move semantics can deliver more than a 4x speedup. The core intuition is just as simple: **a temporary is going to die anyway, so we might as well steal its resources before it does**.

But "stealing" needs support at the language level—we need a mechanism to distinguish "this thing will keep on existing" (an lvalue) from "this thing is about to die" (an rvalue), so that the compiler knows when stealing is safe. That is the subject of the next article—lvalues, rvalues, and the reference system. If you're interested in the move semantics series in vol2, you can also start with [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md), which walks through the material more systematically.

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
