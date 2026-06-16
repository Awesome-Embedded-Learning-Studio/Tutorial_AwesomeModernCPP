---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the range-for loop introduced in C++11 to iterate over arrays
  and containers in the most concise way.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 循环语句
reading_time_minutes: 9
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Range-based for loop
translation:
  source: documents/vol1-fundamentals/ch02/03-range-for.md
  source_hash: 399e2ba0a566a4cb892c681cc1605e6bc02cbe7382406f1c67a5b5c6645a8fb4
  translated_at: '2026-06-16T03:41:44.949015+00:00'
  engine: anthropic
  token_count: 1663
---
# Range-based for Loops

When writing traditional for loops to iterate over arrays, we always have to do one thing—manage that index variable. Honestly, we've all written this line countless times, and we've all gotten it wrong countless times: writing `i < n` as `i <= n` causing an out-of-bounds access, forgetting `i++` causing an infinite loop, or changing the array length but forgetting to update the loop condition... Frankly, bugs introduced by these slips are the most frustrating because they aren't logic errors; they are purely a failure of manual bookkeeping.

C++11 offers an elegant solution: the **range-based for loop**. The core idea is simple—stop making the programmer manage the index. Just tell the compiler, "iterate over every element in this collection." In this chapter, we will thoroughly master the usage of range-based for loops.

## Step One — Understanding Basic Syntax

The syntax for a range-based for loop looks like this:

```cpp
for (element_declaration : collection) {
    // loop body
}
```

Let's compare this with a simple example. Suppose we have an array and want to print every element:

```cpp
#include <cstdio>

int main() {
    int arr[] = {1, 2, 3, 4, 5};

    // Traditional for loop
    for (int i = 0; i < 5; ++i) {
        printf("%d ", arr[i]);
    }
    printf("\n");

    // Range-based for loop
    for (int x : arr) {
        printf("%d ", x);
    }
    printf("\n");

    return 0;
}
```

Output:

```text
1 2 3 4 5
1 2 3 4 5
```

The output is identical, but the range-based for version eliminates the index variable `i`, the array length `5`, and the `arr[i` indexing access—meaning it removes all the places where a slip-up could occur. The compiler handles all the calculations for you. The range-based for loop isn't picky; it supports C-style arrays, `std::vector`, `std::list`, `std::map`, brace-enclosed initializer lists—basically anything you can "traverse from beginning to end."

## Step Two — Three Ways to Use `auto`

The `auto` keyword saves us the trouble of writing out types, but in a range-based for loop, there are three forms with drastically different behaviors. Understanding them is a crucial piece of the puzzle for grasping C++ value semantics versus reference semantics.

**By value** `auto x`: Each iteration copies the element to `x`. Modifying `x` does not affect the original collection. For small types like `int`, this is fine, but it wastes performance when iterating over large objects.

**By reference** `auto& x`: Makes `x` a reference to the original element. There is no copying overhead, and we can modify the original element directly.

**By const reference** `const auto& x`: This is a read-only reference. It avoids copying and prevents accidental modification. It is the best practice for traversing large objects and the recommended default choice in generic code.

Let's use a brief example to see the difference between the three:

```cpp
#include <iostream>
#include <vector>

int main() {
    std::vector<int> nums = {1, 2, 3};

    // 1. By value: Copy, modification doesn't affect original
    for (auto x : nums) {
        x = 10;
    }
    // nums is still {1, 2, 3}

    // 2. By reference: No copy, modification affects original
    for (auto& x : nums) {
        x *= 2;
    }
    // nums becomes {2, 4, 6}

    // 3. By const reference: Read-only, efficient for large objects
    for (const auto& x : nums) {
        std::cout << x << " ";
    }
    // Output: 2 4 6
}
```

> ⚠️ **Warning**
> Never use `auto x` when you need to modify elements; otherwise, you are only modifying a copy, and the original array remains untouched. Bugs of this nature—"compiles successfully, runs without error, but produces incorrect results"—are among the hardest to track down. If you need to modify elements in the loop, you must use `auto& x`. This refers to references, which we covered in the previous chapter.

## Step Three — The Trap with C-Style Arrays

The range-based for loop natively supports C-style arrays, but there is a significant limitation: when an array is passed as a function parameter, it decays into a pointer, causing the range-based for loop to fail.

```cpp
// ❌ Error: range-based for loop needs an array, not a pointer
void print_array(int arr[]) {  // equivalent to int* arr
    for (int x : arr) {        // Compiler error here
        printf("%d ", x);
    }
}
```

The reason is that the range-based for loop needs to know the start and end of the collection. Once the array decays into a pointer, the compiler loses the "number of elements" information and cannot determine where the end is.

> ⚠️ **Warning**
> The range-based for loop cannot be used with raw pointers. If you are given a `T*` pointer and a length `n`, you must use a traditional for loop. Later, when we learn about `std::span` (C++20), there will be a more elegant solution.

We recommend using `std::array` instead of C-style arrays. It has the same performance as C arrays but provides standard `begin()`/`end()` interfaces, working seamlessly with range-based for loops:

```cpp
#include <array>
#include <iostream>

void print_array(const std::array<int, 5>& arr) {
    for (int x : arr) {  // ✅ Works perfectly
        std::cout << x << " ";
    }
}
```

## Step Four — Iterating Over Strings

`std::string` can also be traversed with a range-based for loop, yielding one character per iteration. For example, counting vowels:

```cpp
#include <iostream>
#include <string>

int main() {
    std::string text = "Hello World";
    int vowel_count = 0;

    for (char ch : text) {
        if (ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u' ||
            ch == 'A' || ch == 'E' || ch == 'I' || ch == 'O' || ch == 'U') {
            ++vowel_count;
        }
    }

    std::cout << "Vowels: " << vowel_count << std::endl;
    return 0;
}
```

Using the reference version allows for in-place modification of the string, such as converting to uppercase:

```cpp
#include <cctype>
#include <iostream>
#include <string>

int main() {
    std::string str = "hello";

    for (char& ch : str) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    std::cout << str << std::endl; // Output: HELLO
    return 0;
}
```

Here, `static_cast<unsigned char>` is not redundant. `std::toupper`'s parameter is `int`, and `char` in C++ can be signed—passing a negative character value directly is undefined behavior. Casting to `unsigned char` first and then promoting to `int` is the standard way to handle character functions.

> ⚠️ **Warning**
> Calling `std::toupper` directly on a `char` without first casting to `unsigned char` can produce undefined behavior when encountering extended ASCII or Chinese characters. The compiler won't warn you, but the results might be completely wrong. Make it a habit to always perform this conversion before calling character functions.

## C++17 Preview: Structured Bindings

C++17 introduced structured bindings, which work excellently with range-based for loops. While a full explanation waits for the container chapters, let's take a quick look:

```cpp
#include <iostream>
#include <map>

int main() {
    std::map<int, std::string> items = {
        {1, "One"},
        {2, "Two"}
    };

    // C++17 structured binding
    for (const auto& [key, value] : items) {
        std::cout << key << ": " << value << std::endl;
    }
    // Output:
    // 1: One
    // 2: Two
}
```

The `[key, value]` inside the brackets "deconstructs" an object containing multiple fields into independent variables, which is much more intuitive than manually writing `it->first` and `it->second`. Don't worry if you don't fully understand it yet; just know this capability exists.

## Under the Hood — What Range-Based For Actually Does

Why can the range-based for loop work for arrays, `std::vector`, and `std::map`, which are completely different types? The answer is simple: the compiler translates a range-based for loop into an equivalent traditional loop.

```cpp
// Compiler transforms this:
for (auto x : collection) {
    // body
}

// Into roughly this (conceptually):
auto&& __range = collection;
for (auto __begin = __range.begin(), __end = __range.end();
     __begin != __end; ++__begin) {
    auto x = *__begin;
    // body
}
```

The compiler's job is to call `begin()` to get the start and `end()` to get the finish, then step through one by one. For C-style arrays, the compiler knows the length and uses the pointer to the first element plus the length to act as start and stop positions. This means any type that provides `begin()` and `end()` can use a range-based for loop—this also explains why `std::array` is more convenient to use than C-style arrays.

## Practice — range_for.cpp

Let's integrate the previous usage into a complete program, demonstrating summation, counting, and in-place modification:

```cpp
#include <array>
#include <cctype>
#include <iostream>
#include <string>

int main() {
    // 1. Summation
    std::array<int, 5> nums = {1, 2, 3, 4, 5};
    int sum = 0;
    for (int x : nums) {
        sum += x;
    }
    std::cout << "Sum: " << sum << std::endl;

    // 2. Counting
    std::string text = "Embedded C++";
    int count = 0;
    for (char ch : text) {
        if (ch == 'e' || ch == 'E') {
            ++count;
        }
    }
    std::cout << "Count of 'e': " << count << std::endl;

    // 3. In-place modification
    for (int& x : nums) {
        x *= 2; // Double each element
    }
    std::cout << "Modified array: ";
    for (int x : nums) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 range_for.cpp -o range_for
./range_for
```

Output:

```text
Sum: 15
Count of 'e': 3
Modified array: 2 4 6 8 10
```

## Run Online

Run the comprehensive range-for example online to observe summation, counting, in-place modification, and string operations:

<OnlineCompilerDemo
  title="Range-for Comprehensive Drill: Sum, Count, Modify, Strings"
  source-path="code/examples/vol1/07_range_for.cpp"
  description="Run online and observe four typical usages of range-for. Try modifying the array content or the target value."
  allow-run
/>

## Try It Yourself

### Exercise 1: Find the Maximum

Given a `std::array<int, 5>`, use a range-based for loop to find the maximum value and print it. Hint: Declare a variable `max_val` initialized to the first element, then iterate and compare.

```cpp
// Write your code here
```

### Exercise 2: Count Vowels

Use a range-based for loop to count the number of vowels (a/e/i/o/u, case-insensitive) in a `std::string`.

```cpp
// Write your code here
```

### Exercise 3: In-Place Modification

Use the reference version of the range-based for loop to take the absolute value of all negative numbers in an array.

```cpp
// Write your code here
```

## Summary

In this chapter, starting from the pain points of traditional for loops, we learned about the range-based for loop, a C++11 syntactic sugar. The range-based for loop lets the compiler take over index management, so we no longer need to write boundary conditions manually. When paired with `auto`, we must distinguish between three forms: `auto x` for value copying, `auto& x` for modifiable references, and `const auto& x` for read-only references. The range-based for loop cannot be used with raw pointers because pointers lose the information about the number of elements. Mechanically, it is just a wrapper for `begin()` and `end()`, and any type providing these two interfaces can use it.

With this, we have finished covering control flow in Chapter 2. `if`/`else` branches, `switch` multi-way selection, the three classic loops, and the range-based for loop—combined, these tools are sufficient for programs to handle the vast majority of execution flows. In the next chapter, we enter the world of functions—encapsulating repetitive code to make the program structure clearer.
