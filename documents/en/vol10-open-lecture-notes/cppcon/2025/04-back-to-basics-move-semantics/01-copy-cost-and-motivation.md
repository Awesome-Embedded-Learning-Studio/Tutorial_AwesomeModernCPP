---
chapter: 4
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: CppCon 2025 Talk Notes — Starting from the three deep copies in `swap`,
  we implement a `MyString` class from scratch to expose the waste of copying temporary
  objects and reveal the core motivation behind move semantics.
difficulty: beginner
order: 1
platform: host
reading_time_minutes: 9
speaker: Ben Saks
tags:
- cpp-modern
- host
- beginner
talk_title: 'Back to Basics: Move Semantics'
title: 'The Cost of Copying and the Motivation for Moving: From `swap` to `MyString`'
translation:
  engine: anthropic
  source: documents/vol10-open-lecture-notes/cppcon/2025/04-back-to-basics-move-semantics/01-copy-cost-and-motivation.md
  source_hash: 56bd3dfb332458806f57c221de5b61f3c1f11c9d163355a0a288bdfb1fb01c15
  token_count: 3125
  translated_at: '2026-06-15T09:09:23.799382+00:00'
video_bilibili: https://www.bilibili.com/video/BV1X54y1P7uM
video_youtube: https://www.youtube.com/watch?v=szU5b972F7E
---
# Starting with swap: A Tale of Three Copies

:::tip
A quick note: this section is an extended discussion based on CppCon. The link above points to a video series on YouTube; users in China can watch via the Bilibili link.
:::

Copying—not moving, but strictly copying—is a very common operation in C++. The problem is that for many objects (like containers), copying is expensive in most cases. Move semantics were introduced to convert these expensive copy operations into cheap "handover" operations.

Sounds great, but what does "handover" actually mean? Let's start with an example everyone has seen—the `std::swap` function.

## C++03 swap: Three Deep Copies

If you write a generic `swap` in C++03 (before move semantics), it looks like this:

```cpp
template<typename T>
void swap(T& a, T& b) {
    T tmp = a;  // Copy a to tmp
    a = b;      // Copy b to a
    b = tmp;    // Copy tmp to b
}
```

Functionally, every line here performs a copy. But what we really want to do is move the value in `x` to `y`, and the value in `y` to `x`. For built-in types like `int`, copying and moving are the same thing—`int` has no internal structure, copying an `int` is just copying 4 bytes. But for class types that hold dynamically allocated memory (like `std::vector`, `std::string`), every copy can mean a `malloc` + `memcpy` + `free` upon destruction.

Today, we will figure out: why copying is so expensive, and how move semantics slashes this cost.

The experimental environment for this article is Arch Linux WSL, GCC 16.1.1. Here is the environment info:

```text
OS: Linux
Arch: x86_64
Kernel: 5.15.167.4-microsoft-standard-WSL2
GCC: 16.1.1
```

## Hand-rolling a MyString: Seeing the Cost of Copying

To see the problem clearly, let's write a simplified string class—`MyString`. It stores string content using a dynamically allocated character array, similar to the first string class you might write when learning C++. `std::string` is much more complex (it has SSO optimization<RefLink :id="1" preview="cppreference, std::basic_string, Notes 节" />—short strings are stored directly inside the object, avoiding heap allocation), but `MyString` is sufficient to expose the overhead of copying.

By the way, if I were writing this code today, I would use `std::unique_ptr` to manage that dynamic array. But `std::unique_ptr` already implements move semantics, so using it would prevent us from demonstrating "what happens without move semantics." So I am intentionally using raw pointers. Similarly, I omitted `explicit` and `noexcept` qualifiers to keep the slides from getting too cluttered.

### Basic Structure: Construction and Destruction

```cpp
class MyString {
    char* data_;
    size_t size_;

public:
    // Constructor from C-string
    MyString(const char* str) {
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

Creating a `MyString` for `"hello"` results in a memory layout where `size_` holds 5, and `data_` points to a 6-byte block allocated on the heap (5 characters + the terminating `\0`). Upon destruction, `delete[]` releases this memory. Very straightforward.

### Copy Constructor: The Necessity of Deep Copy

Now the problem arises: if I want to create `b` from `a`—an independent string with the same value—can I just copy these two data members?

```cpp
MyString b = a;  // Can we just copy data_ and size_?
```

No. Because if `b`'s `data_` points to the same memory as `a`'s, then when both `a` and `b` go out of scope and destruct, they will both execute `delete[]` on the same memory. This is double delete—undefined behavior<RefLink :id="2" preview="C++ Standard, [expr.delete] — 对同一指针执行两次 delete 是 UB" />.

Therefore, the copy constructor must perform a **deep copy**—allocate memory exclusive to the new object and copy the content over:

```cpp
// Copy Constructor
MyString(const MyString& other) {
    size_ = other.size_;
    data_ = new char[size_ + 1];      // Heap allocation
    memcpy(data_, other.data_, size_ + 1); // Memory copy
}
```

This is correct, but the cost is: one `new` (heap allocation) + one `memcpy`. For short strings, the overhead of heap allocation far outweighs the cost of copying the characters themselves.

### Copy Assignment Operator: Overwriting an Existing Object

Copy construction and copy assignment are easily confused because they both use the `=` sign. The distinction is simple: **check if the target object exists before the assignment**. If it already exists (like `b` in `a = b`), it is assignment; if a new object is being created (like `MyString b = a`), it is construction.

Assignment implementation requires one extra step compared to construction—cleaning up the old value first:

```cpp
// Copy Assignment Operator
MyString& operator=(const MyString& other) {
    if (this != &other) {             // Self-assignment check
        delete[] data_;               // 1. Release old memory
        size_ = other.size_;
        data_ = new char[size_ + 1];  // 2. Allocate new memory
        memcpy(data_, other.data_, size_ + 1); // 3. Copy content
    }
    return *this;
}
```

Note that we `delete[]` the old array first, then `new` the new array. If we did `new` first then `delete[]`, and if `new` threw an exception, the old array would be lost and the new allocation would have failed, leaving the object in an unrecoverable state. Here we are temporarily ignoring exception safety (production code should use the copy-and-swap idiom<RefLink :id="3" preview="Wikipedia, Copy-and-swap idiom" />) to focus on the core logic.

### operator+: The Waste of Copying Temporary Objects

Now `MyString` has complete copy operations. But if I only implement copying, this type effectively **has no move semantics**—any attempt to "move" it will fall back to a copy. Let's look at a typical scenario—string concatenation:

```cpp
// Concatenation: returns a new MyString
MyString operator+(const MyString& a, const MyString& b) {
    MyString result;                  // Default construct
    // ... (omitted implementation: allocate size_ + b.size_, copy both) ...
    return result;
}
```

Wait—there is a problem here. `result` is constructed using the default constructor (calling the first constructor), which is fine in itself. But the problem lies in the **caller**:

```cpp
MyString a = "Hello";
MyString b = "World";
MyString c = a + b;  // c is copy-constructed from the temporary result
```

`a + b` returns a temporary `MyString` object (which internally already has a block of heap memory storing `"HelloWorld"`). Then `c` is created via copy construction from it—meaning a new block of memory must be allocated, content copied over, and then the temporary object releases its own memory upon destruction.

What we are doing is: **copying a piece of data that already exists and is exactly what we want, and then destroying the original**. If that isn't waste, what is?

## Let the Experiment Speak: How Expensive is Copying?

Saying "waste" isn't intuitive enough. Let's run a simple benchmark to compare the performance difference of string concatenation with and without move semantics.

```cpp
#include <iostream>
#include <chrono>

// ... (MyString definition) ...

int main() {
    using namespace std::chrono;
    auto start = high_resolution_clock::now();

    for (int i = 0; i < 100000; ++i) {
        MyString a = "Hello ";
        MyString b = "World";
        MyString c = a + b;  // Hot path
    }

    auto end = high_resolution_clock::now();
    std::cout << "Time: " << duration_cast<milliseconds>(end - start).count() << "ms\n";
}
```

Compile and run:

```text
# Without move semantics (copying)
Time: 38ms

# With move semantics (std::string)
Time: 9ms
```

You see—with move semantics, the number of copies is 0; everything turns into move operations. Each move just steals a pointer (one pointer assignment + one `nullptr` set), rather than allocating new memory + copying content. In 100,000 concatenations, this is a difference of 38ms vs 9ms—**more than a 4x speedup**. And this gap scales rapidly with string length and iteration count.

## The Intuition Behind Move Semantics: Why Not Just Hand Over?

Going back to the `a + b` example. `a + b` produces a temporary object that holds a block of heap memory containing `"HelloWorld"`. This temporary object is about to be destroyed—its lifecycle ends at the conclusion of this statement. Since it's going to die anyway, why don't we just "hand over" its memory to `c`?

This is the core intuition of move semantics: **temporary objects are going to be destroyed anyway, so we might as well steal their resources before they die**. Specifically:

1. `c` directly takes over the temporary object's `data_` pointer (one pointer assignment).
2. The temporary object's `data_` is set to `nullptr` (to prevent `delete[]` upon destruction).
3. When the temporary object destructs, `delete[]` does nothing.

The whole process involves no `malloc`, no `memcpy`, and no extra memory allocation. One pointer assignment + one `nullptr` set, done.

## std::string's SSO: Why isn't Moving Always Necessary?

At this point, you might ask: modern `std::string` has SSO (Small String Optimization), so short strings don't allocate heap memory at all. Does move semantics still matter for it?

Good question. SSO means that if a string is short enough (the threshold in libstdc++ is about 15 characters<RefLink :id="4" preview="GCC libstdc++ source, basic_string.h, _S_local_capacity" />), data is stored directly inside the object, and no heap allocation occurs. For these short strings, the cost of moving and copying is indeed similar—both involve copying a dozen or so bytes.

But once a string exceeds the SSO threshold, `std::string` falls back to heap allocation, and the advantage of move semantics is fully realized—one pointer swap vs one `malloc` + `memcpy`. Furthermore, even for short strings, move semantics allows the compiler to omit unnecessary copies in more scenarios.

For a complete analysis of SSO, we previously discussed this in detail in vol3's [Deep Dive into string: SSO, COW, and resize_and_overwrite](../../../../vol3-standard-library/04-string-memory-deep-dive.md), so we won't expand on it here.

## What We've Figured Out So Far

Starting from the three deep copies of `std::swap`, we hand-rolled a `MyString` class to see the source of copying overhead (heap allocation + memory copy), and used experiments to prove that move semantics can bring more than a 4x performance boost. The core intuition is simple: **temporary objects are going to die anyway, so steal their resources before they do**.

But "stealing" requires language-level support—we need a mechanism to distinguish between "this thing will persist" (lvalue) and "this thing is about to die" (rvalue), so the compiler knows when it is safe to steal. This is the content of the next article—lvalues, rvalues, and the reference system. If you are interested in the move semantics series in vol2, check out [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md), which has a more systematic explanation.

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
