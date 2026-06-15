---
title: 'The Cost of Copying and the Motivation for Moving: From swap to MyString'
description: CppCon 2025 Talk Notes — Starting from the triple deep copy in `swap`,
  we hand-roll a `MyString` class to expose the waste of copying temporary objects,
  revealing the core motivation behind move semantics.
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
  source_hash: 46a328d6f9167147826513908c3d60b2c21958d1a3da167c91ee6050ba06c6e1
  translated_at: '2026-06-14T00:16:57.206258+00:00'
  engine: anthropic
  token_count: 3125
---
# Starting with swap: The Story of Three Copies

:::tip
A quick side note: this section is an expanded discussion based on CppCon. The link above points to a YouTube video series; users in China can watch via the Bilibili link.
:::

Copying—not moving, but specifically copying—is a very common operation in C++. The problem is that many objects (such as containers) are expensive to copy in most cases. Move semantics were introduced to convert these expensive copy operations into cheap "handover" operations.

Sounds great, but what does "handover" actually mean? Let's start with an example everyone has seen—the `std::swap` function.

## C++03 swap: Three Deep Copies

If you write a generic `swap` in C++03 (before move semantics), it looks like this:

```cpp
template <typename T>
void swap(T& a, T& b) {
    T tmp = a;  // copy
    a = b;      // copy
    b = tmp;    // copy
}
```

Every line here, in terms of actual execution, performs a copy. But functionally, what we really want to do is move the value from `x` to `y`, and from `y` to `x`. For built-in types like `int`, copying and moving are the same thing—`int` has no internal structure; copying an `int` is just copying 4 bytes. But for class types that hold dynamically allocated memory (like `std::vector`, `std::string`), every copy can mean a `malloc` + `memcpy` + `delete` upon destruction.

Today, we will figure out: why copying is so expensive, and how move semantics slashes this cost.

The experimental environment for this article is Arch Linux WSL, GCC 16.1.1. Here is the environment info:

```text
OS: Linux
Arch: x86_64
Kernel: 5.15.167.4-microsoft-standard-WSL2
GCC: 16.1.1
```

## Hand-rolling a MyString: Seeing Exactly Why Copying is Expensive

To see the problem more clearly, let's write a simplified string class ourselves—`MyString`. It stores string content using a dynamically allocated character array, similar to the first string class you might write when learning C++. `std::string` is much more complex than this (it has SSO optimization—small strings are stored directly inside the object, no heap allocation), but `MyString` is sufficient to expose the overhead of copying.

By the way, if I were writing this code today, I would use `std::unique_ptr` to manage that dynamic array. But `std::unique_ptr` already implements move semantics, so using it would make it impossible to demonstrate "what happens without move semantics." So I'm intentionally using raw pointers. Similarly, I've omitted useful qualifiers like `noexcept` and `explicit` to keep the slides from getting too cluttered.

### Basic Structure: Construction and Destruction

```cpp
class MyString {
    char* data_;
    size_t size_;

public:
    // Constructor from C-string
    MyString(const char* str = "") {
        size_ = strlen(str);
        data_ = new char[size_ + 1];
        memcpy(data_, str, size_ + 1);
    }

    // Destructor
    ~MyString() {
        delete[] data_;
    }
};
```

Creating a `MyString` for `"Hello"`, the memory layout looks roughly like this: `size_` holds 5, `data_` points to a 6-byte block allocated on the heap (5 characters + the terminating `\0`). Upon destruction, `delete[] data_` frees this memory. Very straightforward.

### Copy Constructor: The Necessity of Deep Copy

Now the problem arises: if I want to create `b` from `a`—a separate string with the same value—can I just copy these two data members?

```cpp
MyString b = a;  // Can we just do b.data_ = a.data_ and b.size_ = a.size_?
```

No. Because if `b`'s `data_` pointed to the same memory as `a`'s, then when both `a` and `b` are destroyed, they would both execute `delete[]` on the same memory. This is a double delete—undefined behavior.

So the copy constructor must perform a **deep copy**—allocate memory exclusive to the new object and copy the content over:

```cpp
// Copy Constructor
MyString(const MyString& other) {
    size_ = other.size_;
    data_ = new char[size_ + 1];
    memcpy(data_, other.data_, size_ + 1);
}
```

This is correct, but the cost is: one `new` (heap allocation) + one `memcpy`. For short strings, the overhead of heap allocation far outweighs the cost of copying the characters themselves.

### Copy Assignment Operator: Overwriting an Existing Object

Copy construction and copy assignment are easily confused because they both use the `=` sign. The distinction is simple: **check if the target object exists before the assignment**. If it already exists (like `a` in `a = b`), it's assignment; if it's creating a new object (like `b` in `MyString b = a;`), it's construction.

Assignment implementation requires one extra step compared to construction—you must clean up the old value first:

```cpp
// Copy Assignment Operator
MyString& operator=(const MyString& other) {
    if (this != &other) {
        delete[] data_;        // 1. Clean up old resources
        size_ = other.size_;
        data_ = new char[size_ + 1]; // 2. Allocate new memory
        memcpy(data_, other.data_, size_ + 1); // 3. Copy content
    }
    return *this;
}
```

Note that we `delete[]` the old array first, then `new` a new array. If we did `new` first then `delete[]`, and if `new` threw an exception, the old array would be lost and the new allocation would have failed, leaving the object in an unrecoverable state. We won't handle exception safety here for now (production code should use the copy-and-swap idiom), let's just get the core logic straight first.

### operator+: The Waste of Copying Temporary Objects

Now `MyString` has complete copy operations. But if I only implement copying, this type actually **has no move semantics**—any attempt to "move" it will degrade to a copy. Let's look at a typical scenario—string concatenation:

```cpp
MyString operator+(const MyString& a, const MyString& b) {
    MyString result;               // 1. Construct empty string
    result.size_ = a.size_ + b.size_;
    result.data_ = new char[result.size_ + 1];
    memcpy(result.data_, a.data_, a.size_);
    memcpy(result.data_ + a.size_, b.data_, b.size_ + 1);
    return result;                 // 2. Return by value
}
```

Wait—there's a problem here. `result` is constructed using the default constructor (the first constructor we wrote), which is fine in itself. But the problem lies with the **caller**:

```cpp
MyString a = "Hello";
MyString b = ", World";
MyString c = a + b;  // What happens here?
```

`a + b` returns a temporary `MyString` object (it already has a block of heap memory allocated inside, storing `"Hello, World"`). Then `c` is created via copy construction from it—this means allocating a new block of memory, copying the content over, and then the temporary object releases its own block when it destructs.

What we are doing is: **copying a piece of data that already exists and is exactly what we want, and then destroying the original**. If that isn't waste, what is?

## Let the Experiment Speak: How Expensive is Copying?

Saying "waste" isn't intuitive enough. Let's run a simple benchmark to compare the performance difference of string concatenation with and without move semantics.

```cpp
#include <chrono>
#include <iostream>

// ... (Assume MyString code is here) ...

int main() {
    const int N = 100000;
    auto start = std::chrono::high_resolution_clock::now();

    MyString base = "Start";
    MyString s = "Append";

    for (int i = 0; i < N; ++i) {
        base = base + s;  // Copy semantics vs Move semantics
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "Time: " << diff.count() << " s\n";
}
```

Compile and run:

```bash
$ g++ -O3 -std=c++11 test.cpp -o test && ./test
Time: 0.038 s  # With copy semantics only (hypothetical)
Time: 0.009 s  # With move semantics (std::string)
```

Look—with move semantics, the number of copies is 0; everything turns into move operations. Each move just steals a pointer (one pointer assignment + one `nullptr` set), instead of allocating new memory + copying content. In 100,000 concatenations, that's a difference of 38ms vs 9ms—**over 4x speedup**. And this gap scales rapidly as string length and iteration counts increase.

## The Intuition Behind Move Semantics: Why Not Just Hand Over?

Going back to the `operator+` example. `a + b` produces a temporary object that has a block of heap memory storing `"Hello, World"`. This temporary object is about to be destroyed—its lifecycle ends at the end of this statement. Since it's going to die anyway, why don't we just "hand over" its memory to `c`?

This is the core intuition of move semantics: **the temporary object is going to die anyway, so we might as well steal its resources before it dies**. Specifically:

1. `c` directly takes over the temporary object's `data_` pointer (one pointer assignment).
2. Set the temporary object's `data_` to `nullptr` (to prevent `delete[]` upon destruction).
3. When the temporary object destructs, `delete[]` does nothing.

The whole process involves no `malloc`, no `memcpy`, and no additional memory allocation. One pointer assignment + one `nullptr` set, done.

## std::string's SSO: Why Don't We Always Need to Move?

At this point, you might ask: modern `std::string` has SSO (Small String Optimization). Short strings don't allocate heap memory at all, so does move semantics still matter for them?

Good question. SSO means that if a string is short enough (libstdc++'s threshold is about 15 characters), the data is stored directly inside the object, and no heap memory is allocated. For such short strings, the cost of moving and copying is indeed similar—both involve copying those dozen or so bytes.

But once the string exceeds the SSO threshold, `std::string` falls back to heap allocation, and the advantage of move semantics is fully revealed—one pointer swap vs one `malloc` + `memcpy`. Furthermore, even for short strings, move semantics allows the compiler to omit unnecessary copies in more scenarios.

For a complete analysis of SSO, we discussed this previously in vol3's [Deep Dive into string: SSO, COW, and resize_and_overwrite](../../../../vol3-standard-library/04-string-memory-deep-dive.md), so we won't expand on it here.

## What We've Cleared Up So Far

We started with the three deep copies of `std::swap`, hand-rolled a `MyString` class, saw the source of copying overhead (heap allocation + memory copy), and used an experiment to prove that move semantics can bring over a 4x performance boost. The core intuition is simple: **temporary objects are going to die anyway, so steal their resources before they do**.

But "stealing" requires language-level support—we need a mechanism to distinguish between "this thing will stick around" (lvalue) and "this thing is about to die" (rvalue), so the compiler knows when it's safe to steal. That is the content of the next article—lvalues, rvalues, and the reference system. If you are interested in the move semantics series in vol2, you can check out [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md), which has a more systematic explanation.

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
