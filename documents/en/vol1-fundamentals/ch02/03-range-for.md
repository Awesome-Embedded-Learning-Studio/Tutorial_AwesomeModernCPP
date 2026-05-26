---
title: range-for loop
description: Master the range-for loop introduced in C++11, and iterate over arrays
  and containers in the most concise way.
chapter: 2
order: 3
difficulty: beginner
reading_time_minutes: 10
platform: host
prerequisites:
- 循环语句
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch02/03-range-for.md
  source_hash: 5ae7af74c4fa44a5ebd49d2cf24dc9d2f2fa544d4602bd9f591bf50080165813
  translated_at: '2026-05-26T10:47:26.587859+00:00'
  engine: anthropic
  token_count: 1667
---
# The range-for Loop

When writing traditional for loops to iterate over arrays, we always have to do one thing—manage that index variable. `for (int i = 0; i < n; ++i)`, we've written this line countless times, but we've also gotten it wrong countless times: writing `<` as `<=` causing out-of-bounds access, forgetting to increment `i` causing an infinite loop, changing the array length but forgetting to update the loop condition... Frankly, bugs introduced by simple typos are the most frustrating because they aren't logic errors—they're just sloppy manual work.

C++11 gives us an elegant solution: the **range-for loop**. Its core idea is simple—stop making the programmer manage indices, and just tell the compiler "iterate through every element in this collection." In this chapter, we'll thoroughly understand how to use range-for.

## Step One — Understanding the Basic Syntax of range-for

The syntax of range-for looks like this:

```cpp
for (类型 变量名 : 集合) {
    // 使用变量
}
```

Let's compare the two approaches with a simple example. Suppose we have an array and want to print each element:

```cpp
#include <iostream>

int main()
{
    int scores[] = {90, 85, 78, 92, 88};

    // 传统 for 循环
    for (int i = 0; i < 5; ++i) {
        std::cout << scores[i] << " ";
    }
    std::cout << std::endl;

    // range-for 循环
    for (int score : scores) {
        std::cout << score << " ";
    }
    std::cout << std::endl;

    return 0;
}
```

Output:

```text
90 85 78 92 88
90 85 78 92 88
```

The output of both versions is exactly the same, but the range-for version eliminates the index variable `i`, the array length `5`, and the `scores[i]` subscript access—in other words, it removes all the places where a typo could cause a bug. The compiler handles all the calculations for you. range-for isn't picky; it supports C-style arrays, `std::array`, `std::vector`, `std::string`, and brace-enclosed initializer lists—basically anything you can "traverse from beginning to end."

## Step Two — Three Ways to Use auto

The `auto` keyword saves us the trouble of writing types manually, but in a range-for there are three forms with distinctly different behaviors. Understanding them is an important piece of the puzzle for grasping C++ value semantics versus reference semantics.

**By value** `for (auto x : arr)` copies the element to `x` on each iteration. Modifying `x` does not affect the original collection. For small types like `int`, this is fine, but iterating over large objects wastes performance.

**By reference** `for (auto& x : arr)` makes `x` a reference to the original element, avoiding copy overhead and allowing direct modification of the original element.

**By const reference** `for (const auto& x : arr)` is a read-only reference that avoids copying while preventing accidental modification. This is the best practice for iterating over large objects and the recommended default choice in generic code.

Let's use a brief example to feel the differences between the three:

```cpp
int nums[] = {1, 2, 3};

// 按值：改副本，原数组不变
for (auto x : nums) { x *= 2; }
// nums 仍是 {1, 2, 3}

// 按引用：直接改原数组
for (auto& x : nums) { x *= 2; }
// nums 变成 {2, 4, 6}

// const 引用：只读遍历，编译器会阻止修改
for (const auto& x : nums) {
    std::cout << x << " ";  // 2 4 6
}
```

> ⚠️ **Watch Out**
> Never use `for (auto x : arr)` when you need to modify elements, otherwise you're only modifying a copy and the original array remains untouched. The hallmark of this bug is "compiles fine, no runtime errors, but wrong results"—making it one of the hardest to track down. If you need to modify elements inside the loop, you must use `auto&`. This is a reference, which we covered in the previous chapter.

## Step Three — The Pitfall of range-for with C-Style Arrays

range-for natively supports C-style arrays, but there is an important limitation: when an array is passed as a function parameter, it decays into a pointer, and range-for stops working.

```cpp
void print_array(int arr[])  // arr 在这里其实是指针
{
    // 编译错误！编译器不知道 arr 指向多少个元素
    // for (int x : arr) { ... }
}
```

The reason is that range-for needs to know the beginning and end of the collection. Once the array decays into a pointer, the compiler loses the "number of elements" information and cannot determine where the end is.

> ⚠️ **Watch Out**
> range-for cannot be used with bare pointers. If you receive a `int*` plus a length `size_t n`, you have to use a traditional for loop. Later, when we learn about `std::span` (C++20), there will be a more elegant solution.

We recommend using `std::array` instead of C-style arrays—it has the same performance as C arrays but provides standard `begin()`/`end()` interfaces, working seamlessly with range-for:

```cpp
std::array<int, 5> scores = {90, 85, 78, 92, 88};
for (const auto& s : scores) {
    std::cout << s << " ";
}
```

## Step Four — Iterating Over Strings with range-for

`std::string` can also be iterated over with range-for, yielding a single character on each iteration. For example, counting vowels:

```cpp
std::string text = "Hello C++ World";
int vowel_count = 0;
for (char c : text) {
    char lower = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    if (lower == 'a' || lower == 'e' || lower == 'i'
        || lower == 'o' || lower == 'u') {
        ++vowel_count;
    }
}
std::cout << "元音字母个数: " << vowel_count << std::endl;
// 输出: 元音字母个数: 3
```

Using the reference version, we can also modify the string in place, such as converting to uppercase:

```cpp
for (auto& c : text) {
    c = static_cast<char>(
        std::toupper(static_cast<unsigned char>(c)));
}
```

The `static_cast<unsigned char>` here isn't redundant. The parameter of `std::toupper` is `int`, and in C++, `char` might be signed—passing a negative character value directly is undefined behavior (UB). Converting to `unsigned char` first before promoting to `int` is the standard practice when working with character functions.

> ⚠️ **Watch Out**
> Calling `std::toupper` directly on a `char` without first converting it to `unsigned char` produces undefined behavior (UB) when encountering extended ASCII or Chinese characters. The compiler won't warn you, but the results may be completely wrong. Make it a habit to always perform this conversion before calling character functions.

## Looking Ahead to C++17: Structured Bindings

Structured bindings introduced in C++17 work beautifully with range-for. While a full explanation will have to wait for the container chapters, we can take a quick peek:

```cpp
// C++17：遍历键值对容器时直接拆开 key 和 value
// for (const auto& [key, value] : my_map) {
//     std::cout << key << " -> " << value << std::endl;
// }
```

The `[key, value]` inside the brackets "destructures" an object containing multiple fields into independent variables, which is much more intuitive than manually writing `pair.first` and `pair.second`. Don't worry if you don't fully understand it yet—just know this capability exists.

## Under the Hood — What range-for Actually Does

Why can range-for work with both arrays and completely different types like `std::vector` and `std::string`? The answer is simple: the compiler translates range-for into an equivalent traditional loop.

```cpp
// for (auto x : coll) 大致等价于：
{
    auto&& __range = coll;
    for (auto __it = __range.begin(); __it != __range.end(); ++__it) {
        auto x = *__it;
        // 循环体
    }
}
```

What the compiler does is call `begin()` to get the beginning, call `end()` to get the end, and then step through one by one. For C-style arrays, the compiler knows the length and uses the pointer to the first element plus the length to serve as the start and end positions. This means any type that provides `begin()` and `end()` can use range-for—which also explains why `std::array` is more convenient to use than C-style arrays.

## Hands-On Practice — range_for.cpp

Let's integrate the usages we've covered into a complete program, demonstrating summation, counting, and in-place modification:

```cpp
// range_for.cpp
// Platform: host
// Standard: C++17

#include <array>
#include <cctype>
#include <iostream>
#include <string>

int main()
{
    // 求和
    std::array<int, 6> data = {3, 7, 1, 9, 4, 6};
    int sum = 0;
    for (const auto& x : data) {
        sum += x;
    }
    std::cout << "总和: " << sum << std::endl;

    // 计数
    int target = 6;
    int count = 0;
    for (const auto& x : data) {
        if (x == target) { ++count; }
    }
    std::cout << "值 " << target << " 出现了 " << count
              << " 次" << std::endl;

    // 原地修改：每个元素翻倍
    std::array<int, 6> doubled = data;
    for (auto& x : doubled) { x *= 2; }
    std::cout << "翻倍后: ";
    for (const auto& x : doubled) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // 字符串转大写
    std::string message = "range-for is elegant";
    for (auto& c : message) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    std::cout << "转大写: " << message << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o range_for range_for.cpp
./range_for
```

Output:

```text
总和: 30
值 6 出现了 1 次
翻倍后: 6 14 2 18 8 12
转大写: RANGE-FOR IS ELEGANT
```

## Run Online

Run the range-for comprehensive example online to observe summation, counting, in-place modification, and string operations:

<OnlineCompilerDemo
  title="range-for Comprehensive Demo: Summation, Counting, Modification, Strings"
  source-path="code/examples/vol1/07_range_for.cpp"
  description="Run online and observe four typical usages of range-for. Try modifying the array contents or the target value."
  allow-run
/>

## Try It Yourself

### Exercise 1: Find the Maximum Value

Given a `std::array<int, 8>`, use range-for to find the maximum value and print it. Hint: declare `max_val` initialized to the first element, then iterate and compare.

```text
数组: 12 3 45 7 23 56 8 19
最大值: 56
```

### Exercise 2: Count Vowels

Use range-for to count the number of vowels (a/e/i/o/u, case-insensitive) in a `std::string`.

```text
字符串: "Beautiful C++"
元音个数: 5
```

### Exercise 3: In-Place Modification

Use the reference version of range-for to take the absolute value of all negative numbers in an array.

```text
修改前: 3 -7 1 -9 4 -6
修改后: 3 7 1 9 4 6
```

## Summary

In this chapter, starting from the pain points of traditional for loops, we learned about range-for, a C++11 syntactic sugar. `for (类型 变量 : 集合)` lets the compiler take over index management, so we no longer need to manually write boundary conditions. When paired with `auto`, we need to distinguish three forms: `auto` makes a value copy, `auto&` makes a modifiable reference, and `const auto&` makes a read-only reference. range-for cannot be used with bare pointers because pointers lose the element count information. Under the hood, it's simply a wrapper around `begin()` and `end()`, and any type providing these two interfaces can use it.

With this, we have covered the entire control flow section of Chapter two. if/else branching, switch multi-way selection, the three classic loops, plus range-for—combined, these tools are sufficient to handle the vast majority of execution flows in a program. In the next chapter, we'll enter the world of functions—encapsulating repetitive code to make the program structure much clearer.
