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
title: 拷贝的开销与移动的动机：从 swap 到 MyString
video_bilibili: https://www.bilibili.com/video/BV1X54y1P7uM
video_youtube: https://www.youtube.com/watch?v=szU5b972F7E
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/04-back-to-basics-move-semantics/01-copy-cost-and-motivation.md
  source_hash: cb7f6e28f71f6eb4fa0f6fc37cd6ec3da26cdb982d37dbd724522718605f8ca0
  translated_at: '2026-06-16T03:53:03.950296+00:00'
  engine: anthropic
  token_count: 3127
---
# Starting with swap: A Tale of Three Copies

:::tip
As a side note, this section is an expanded discussion based on CppCon. The link above points to a YouTube video series; users in China can watch via the Bilibili link.
:::

Copying—not moving, but specifically copying—is a very common operation in C++. However, the problem is that many objects (such as containers) are expensive to copy in most cases. Move semantics were introduced to convert these expensive copy operations into cheap "handover" operations.

Sounds great, but what does "handover" actually mean? We start with an example everyone has seen—the `std::swap` function.

## C++03 swap: Three Deep Copies

If you write a generic `swap` in C++03 (before move semantics), it looks like this:

```cpp
template<typename T>
void swap(T& x, T& y) {
    T tmp = x;   // Copy x to tmp
    x = y;       // Copy y to x
    y = tmp;     // Copy tmp to y
}
```

From the perspective of actual execution, every line here is performing a copy. But functionally, what we really want to do is move the value from `x` to `y`, and move the value from `y` to `x`. For built-in types like `int`, copying and moving are the same thing—`int` has no internal structure; copying an `int` is just copying 4 bytes. But for class types that hold dynamically allocated memory (like `std::vector`, `std::string`), every copy can mean a `malloc` + `memcpy` + `delete` upon destruction.

Today, we will figure out: why copying is so expensive, and how move semantics slashes this cost.

The experimental environment for this article is Arch Linux WSL, GCC 16.1.1. Here is the environment information:

```text
OS: Linux
Arch: x86_64
Kernel: 5.15.167.4-microsoft-standard-WSL2
GCC: 16.1.1
```

## Rolling a MyString: Seeing Where the Cost Is

To make the problem clearer, let's write a simplified string class ourselves—`MyString`. It stores string content using a dynamically allocated character array, much like the first string class you wrote when learning C++. `std::string` is much more complex than this (it has SSO optimization<RefLink :id="1" preview="cppreference, std::basic_string, Notes 节" />—short strings are stored directly inside the object without heap allocation), but `MyString` is sufficient to expose the overhead of copying.

By the way, if I were writing this code now, I would use `std::unique_ptr` to manage that dynamic array. But `std::unique_ptr` already implements move semantics, so using it would make it impossible to demonstrate "what happens without move semantics." So I am intentionally using raw pointers. Similarly, I have omitted useful qualifiers like `noexcept` and `explicit` to keep the slides from getting too cluttered.

### Basic Structure: Construction and Destruction

```cpp
class MyString {
    char* data_;
    size_t size_;

public:
    // Constructor from C-string
    MyString(const char* str = "") {
        size_ = std::strlen(str);
        data_ = new char[size_ + 1];
        std::strcpy(data_, str);
    }

    // Destructor
    ~MyString() {
        delete[] data_;
    }
};
```

Creating a `MyString("hello")` string, the memory layout looks roughly like this: `size_` holds 5, `data_` points to a 6-byte block allocated on the heap (5 characters + the terminating `\0`). Upon destruction, `delete[] data_` frees this memory. Very straightforward.

### Copy Constructor: The Necessity of Deep Copy

Now the problem arises: if I want to create `b` from `a`—an independent string with the same value—can I just copy these two data members?

```cpp
MyString b = a;  // Can we just do b.data_ = a.data_; b.size_ = a.size_;?
```

No. Because if `b`'s `data_` points to the same memory, then when both `a` and `b` are destroyed, they will both execute `delete[]` on the same memory. This is a double delete—undefined behavior<RefLink :id="2" preview="C++ Standard, [expr.delete] — 对同一指针执行两次 delete 是 UB" />.

Therefore, the copy constructor must perform a **deep copy**—allocate memory exclusive to the new object, then copy the content over:

```cpp
// Copy constructor
MyString(const MyString& other) {
    size_ = other.size_;
    data_ = new char[size_ + 1];      // Allocate new memory
    std::strcpy(data_, other.data_);  // Copy content
}
```

This is correct, but the cost is: one `new` (heap allocation) + one `memcpy`. For short strings, the overhead of heap allocation is far greater than copying the characters themselves.

### Copy Assignment Operator: Overwriting Existing Objects

Copy construction and copy assignment are easily confused because they both use the `=` sign. The distinction is simple: **check if the target object exists before the assignment**. If it already exists (like `a` in `a = b`), it is assignment; if a new object is being created (like `a` in `MyString a = b;`), it is construction.

Assignment implementation requires one extra step compared to construction—cleaning up the old value first:

```cpp
// Copy assignment operator
MyString& operator=(const MyString& other) {
    if (this != &other) {             // Self-assignment check
        delete[] data_;               // 1. Clean up old resources
        size_ = other.size_;
        data_ = new char[size_ + 1];  // 2. Allocate new memory
        std::strcpy(data_, other.data_);
    }
    return *this;
}
```

Note that we `delete[]` the old array first, then `new` a new array. If we `new` first and then `delete[]`, and if `new` throws an exception, the old array is lost and the new array failed to allocate, leaving the object in an unrecoverable state. We won't handle exception safety here (production code should use the copy-and-swap idiom<RefLink :id="3" preview="Wikipedia, Copy-and-swap idiom" />), focusing on the core logic for now.

### operator+: The Waste of Copying Temporary Objects

Now `MyString` has complete copy operations. But if I only implement copying, this type effectively **has no move semantics**—any attempt to "move" it will degrade to a copy. Let's look at a typical scenario—string concatenation:

```cpp
MyString operator+(const MyString& lhs, const MyString& rhs) {
    // Calculate new size
    size_t newSize = lhs.size_ + rhs.size_;
    char* newData = new char[newSize + 1];

    // Copy data
    std::strcpy(newData, lhs.data_);
    std::strcat(newData, rhs.data_);

    return MyString(newData, newSize); // Construct temporary
}
```

Wait—there is a problem here. `MyString(newData, newSize)` is constructed using the first constructor (assuming we implemented a private constructor taking a pointer and size), which is fine in itself. But the problem lies at the **call site**:

```cpp
MyString c = a + b;  // a and b are existing MyStrings
```

`a + b` returns a temporary `MyString` object (it already has a block of heap memory allocated inside, storing `"ab"`). Then `c` is created from it via copy constructor—this means allocating a new block of memory, copying the content over, and then the temporary object releases its own block of memory upon destruction.

What we are doing is: **copying a block of data that already exists and is exactly what we want, then destroying the original**. If this isn't waste, what is?

## Let the Experiment Speak: How Expensive is Copying?

Saying "waste" isn't intuitive enough. Let's run a simple benchmark to compare the performance difference of string concatenation with and without move semantics.

```cpp
#include <chrono>
#include <iostream>

// ... (Assume MyString code is here) ...

int main() {
    using namespace std::chrono;

    auto start = high_resolution_clock::now();

    MyString result = "Start";
    for (int i = 0; i < 100000; ++i) {
        result = result + "x";  // Repeated concatenation
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);

    std::cout << "Time: " << duration.count() << "ms\n";
    return 0;
}
```

Compile and run:

```text
# Without move semantics (MyString has no move constructor)
Time: 38ms

# With move semantics (std::string)
Time: 9ms
```

You see—with move semantics, the number of copies is 0; everything turns into move operations. Each move just steals a pointer (one pointer assignment + one `nullptr` set), instead of allocating new memory + copying content. In 100,000 concatenations, this is a difference of 38ms vs 9ms—**more than a 4x speedup**. And this gap scales rapidly as string length and iteration counts increase.

## The Intuition Behind Move Semantics: Why Not Just Hand Over?

Going back to the `MyString c = a + b` example. `a + b` produces a temporary object that has a block of heap memory storing `"ab"`. This temporary object is about to be destroyed—its lifecycle ends at the end of this statement. Since it's going to die anyway, why don't we just "hand over" its memory to `c`?

This is the core intuition of move semantics: **temporary objects are going to be destroyed anyway, so we might as well steal their resources before they die**. Specifically:

1. `c` directly takes over the temporary object's `data_` pointer (one pointer assignment)
2. Set the temporary object's `data_` to `nullptr` (to prevent `delete[]` upon destruction)
3. When the temporary object is destroyed, `delete[]` does nothing

The whole process involves no `malloc`, no `memcpy`, and no extra memory allocation. One pointer assignment + one `nullptr` set, done.

## std::string's SSO: Why Don't We Always Need to Move?

You might ask at this point: modern `std::string` has SSO (Small String Optimization), so short strings don't allocate heap memory at all. Does move semantics still matter for it?

Good question. SSO means: if the string is short enough (libstdc++ threshold is about 15 characters<RefLink :id="4" preview="GCC libstdc++ source, basic_string.h, _S_local_capacity" />), data is stored directly inside the object without heap allocation. For such short strings, the cost of moving and copying is indeed similar—both involve copying those dozen bytes.

But once the string exceeds the SSO threshold, `std::string` falls back to heap allocation, and the advantage of move semantics is fully realized—one pointer swap vs one `malloc` + `memcpy`. Moreover, even for short strings, move semantics allows the compiler to omit unnecessary copies in more scenarios.

For a complete analysis of SSO, we previously discussed this in detail in vol3's [Deep Dive into string: SSO, COW, and resize_and_overwrite](../../../../vol3-standard-library/containers/04-string-memory-deep-dive.md), so we won't expand on it here.

## What We've Cleared Up So Far

Starting from the three deep copies of `std::swap`, we hand-rolled a `MyString` class to see the source of copying overhead (heap allocation + memory copy), and used experiments to prove that move semantics can bring more than a 4x performance boost. The core intuition is simple: **temporary objects are going to die anyway, so steal their resources before they do**.

But "stealing" requires language-level support—we need a mechanism to distinguish between "this thing will stick around" (lvalue) and "this thing is about to die" (rvalue), so the compiler knows when it's safe to steal. This is the content of the next article—lvalues, rvalues, and the reference system. If you are interested in the move semantics series in vol2, you can check out [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md), which has a more systematic explanation.

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
