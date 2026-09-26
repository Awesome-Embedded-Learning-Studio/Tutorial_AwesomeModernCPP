---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the range-for loop introduced in C++11 and iterate over arrays
  and containers the concise way.
difficulty: beginner
order: 3
platform: host
prerequisites:
- Loop Statements
reading_time_minutes: 11
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Range-based for Loops
translation:
  source: documents/vol1-fundamentals/ch02/03-range-for.md
  source_hash: c2f964febfb905c68c3be84bcbdc96cd99c1ea66445f6dcf49ae0f84071cd035
  translated_at: '2026-09-25T10:10:40+00:00'
  engine: anthropic
  token_count: 1800
---
# Range-based for: No More Fat-Fingered Index Bugs

When we write a traditional for loop to walk an array, we're stuck babysitting that index variable. We've all written `for (int i = 0; i < n; ++i)` countless times — and gotten it wrong countless times: `<` typed as `<=` and we read out of bounds, `i` left unincremented and the loop never ends, the array's length changed but the loop condition forgotten. What makes these bugs so annoying is that the logic is perfectly fine — it all falls apart on pure finger-trouble busywork.

The **range-based for loop** from C++11 exists precisely for this: we don't manage the index at all, we just tell the compiler "run me through every element in this collection."

## Basic Syntax

The range-for syntax looks like this:

```cpp
for (type variable_name : collection) {
    // use the variable
}
```

Let's compare with the simplest possible example. Say we have an array and want to print every element:

```cpp
#include <iostream>

int main()
{
    int scores[] = {90, 85, 78, 92, 88};

    // Traditional for loop
    for (int i = 0; i < 5; ++i) {
        std::cout << scores[i] << " ";
    }
    std::cout << std::endl;

    // Range-based for loop
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

Both forms print exactly the same thing, but the range-for version drops the index variable `i`, drops the array length `5`, and drops the `scores[i]` subscript access.

With those gone, the opportunities for finger-trouble go with them — the compiler works out the start and end positions entirely for us. And it iterates over plenty of things: C-style arrays, `std::array`, `std::vector`, `std::string`, brace-enclosed initializer lists — any collection that can be walked from start to finish is supported.

## A Trap with C-Style Arrays: Decaying into a Pointer

range-for natively supports C-style arrays, but there's one limitation we should know ahead of time: when an array is passed as a function parameter it decays into a pointer, and at that point range-for stops working.

```cpp
void print_array(int arr[])  // arr is actually a pointer here
{
    // Compile error! The compiler doesn't know how many elements arr points to
    // for (int x : arr) { ... }
}
```

The reason is that range-for needs to know where the collection starts and ends. Once the array has decayed into a pointer, all the compiler holds is a single address — the "number of elements" information is lost, so it has no way to know where the end is. And there's nothing we can do to help.

range-for cannot be used on raw pointers. If all we hold is an `int*` plus a length `size_t n`, then we have to fall back to the traditional for loop; once we get to `std::span` (C++20), this problem has a much better answer.

We recommend `std::array` as the replacement for C-style arrays: same performance as a C array, plus it comes with the standard `begin()`/`end()` interface, so range-for just works:

```cpp
std::array<int, 5> scores = {90, 85, 78, 92, 88};
for (int s : scores) {
    std::cout << s << " ";
}
```

## Three Ways to Use `auto`: by Value, by Reference, by const Reference

`auto` saves us the trouble of writing out types by hand. In range-for it comes in three forms with drastically different behavior, and this article sorts them all out.

For **access by value**, write `for (auto x : arr)`: each iteration copies the element into `x`. If we modify `x` in the loop body, we're modifying the copy — the original collection doesn't budge. For small types like `int` it doesn't matter, but iterating over big objects this way means paying for one pointless extra copy.

To modify the original elements, use **access by reference**, `for (auto& x : arr)`: `x` is a reference to the original element, there's no copying overhead, and modifying it modifies the real thing. If we only want to read without changing anything, use **access by const reference**, `for (const auto& x : arr)`: a read-only reference — the copy is skipped, and any accidental modification attempt gets blocked by the compiler on the spot. This is the first choice when iterating over large objects, and the recommended default in generic code.

Let's feel out the differences between the three with a short example!

```cpp
int nums[] = {1, 2, 3};

// By value: modifies the copy, the original array stays unchanged
for (auto x : nums) { x *= 2; }
// nums is still {1, 2, 3}

// By reference: modifies the original array directly
for (auto& x : nums) { x *= 2; }
// nums becomes {2, 4, 6}

// const reference: read-only traversal, the compiler blocks modification
for (const auto& x : nums) {
    std::cout << x << " ";  // 2 4 6
    // x *= 2; // <- Not allowed! clangd will paint that line red for you!
}
```

Whatever you do, never use `for (auto x : arr)` when you need to modify elements: you'd only be modifying a copy while the original array stays untouched. The signature of this bug: it compiles, it runs without a single complaint, and the results are wrong — the hardest kind to hunt down. To modify elements, use `auto&`. The `&` is the marker of a reference; references get their formal treatment in Chapter 4, so for now just remember the usage: `auto&` hands you the original.

## Iterating Over Strings

`std::string` can be traversed with range-for too, handing us one character per iteration. For example, counting the vowels in a piece of text:

```cpp
std::string text = "Hello C++ World";
int vowel_count = 0;
for (char c : text) {
    char lower = (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
    if (lower == 'a' || lower == 'e' || lower == 'i'
        || lower == 'o' || lower == 'u') {
        ++vowel_count;
    }
}
std::cout << "元音字母个数: " << vowel_count << std::endl;
// Output: 元音字母个数: 3
```

The reference version also lets us modify a string in place — uppercasing it, for instance:

```cpp
for (auto& c : text) {
    c = static_cast<char>(
        std::toupper(static_cast<unsigned char>(c)));
}
```

The `static_cast<unsigned char>` here is not busywork. `std::toupper` takes an `int` parameter, and `char` in C++ may be signed — feed a negative value straight in and that's undefined behavior; when extended ASCII or Chinese characters show up the results can be completely wrong, and the compiler won't warn us. So cast to `unsigned char` first and then let it promote to `int`: this is the standard idiom when calling character functions, so make it a habit.

## A C++17 Sneak Peek: Structured Bindings

Structured bindings, introduced in C++17, pair beautifully with range-for. The full treatment waits until the containers chapter — for now let's just get familiar with the sight of it:

```cpp
// C++17: unpack key and value directly while iterating a key-value container
for (const auto& [key, value] : my_map) {
    std::cout << key << " -> " << value << std::endl;
}
```

The `[key, value]` in brackets "destructures" an object with multiple fields into independent variables — far more intuitive than hand-writing `pair.first` and `pair.second`. It's fine if you can't read it yet; just knowing the capability exists is enough for now.

## Behind the Scenes — What range-for Actually Does

Why does range-for work on arrays and equally well on completely different types like `std::vector` and `std::string`? The answer is blunt: the compiler translates range-for into an equivalent traditional loop. Let's look at that translation.

```cpp
// for (auto x : coll) is roughly equivalent to:
{
    auto&& __range = coll;
    for (auto __it = __range.begin(); __it != __range.end(); ++__it) {
        auto x = *__it;
        // loop body
    }
}
```

Look at what the compiler does: it calls `begin()` to get the start, calls `end()` to get the finish, and then walks from one to the other step by step. For C-style arrays, the compiler knows the length itself and uses the pointer to the first element plus that length as the start and end positions. Which also means: any type that provides `begin()` and `end()` can use range-for — and that's exactly why `std::array` is nicer to work with than a C-style array.

## Hands-On Practice — range_for.cpp

Let's roll the previous usages into one complete program, demonstrating summation, counting, and in-place modification:

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
    // Sum
    std::array<int, 6> data = {3, 7, 1, 9, 4, 6};
    int sum = 0;
    for (const auto& x : data) {
        sum += x;
    }
    std::cout << "总和: " << sum << std::endl;

    // Count
    int target = 6;
    int count = 0;
    for (const auto& x : data) {
        if (x == target) { ++count; }
    }
    std::cout << "值 " << target << " 出现了 " << count
              << " 次" << std::endl;

    // In-place modification: double every element
    std::array<int, 6> doubled = data;
    for (auto& x : doubled) { x *= 2; }
    std::cout << "翻倍后: ";
    for (const auto& x : doubled) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // Uppercase the string
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

## Run It Online

You can also run this comprehensive example online and watch the summation, counting, in-place modification, and string operations:

<OnlineCompilerDemo
  title="range-for Comprehensive Drill: Sum, Count, Modify, Strings"
  source-path="code/examples/vol1/07_range_for.cpp"
  description="Run it online and watch four typical range-for patterns in action. Try changing the array contents or the target value."
  allow-run
/>

## Try It Yourself

### Exercise 1: Find the Maximum

Given a `std::array<int, 8>`, use range-for to find the maximum and print it. Hint: declare `max_val` initialized to the first element, then iterate and compare.

```text
数组: 12 3 45 7 23 56 8 19
最大值: 56
```

::: details Reference Solution

```cpp
#include <iostream>
#include <array>

int main()
{
    std::array<int, 8> value = {12, 3, 45, 7, 23, 56, 8, 19};
    int max_val = value[0];
    std::cout << "数组: ";
    for (const auto& x : value)
    {
        if (max_val < x)
        {
            max_val = x;
        }
        std::cout << x << " ";
    }

    std::cout << std::endl
              << "最大值: " << max_val << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
数组: 12 3 45 7 23 56 8 19
最大值: 56
```

:::

### Exercise 2: Count the Vowels

Use range-for to count the vowels (a/e/i/o/u, case-insensitive) in a `std::string`.

```text
字符串: "Beautiful C++"
元音个数: 5
```

::: details Reference Solution

```cpp
#include <iostream>
#include <string>

int main()
{
    std::string text = "Beautiful C++";
    int vowel_count = 0;
    for (char c : text)
    {
        char lower = (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
        if (lower == 'a' || lower == 'e' || lower == 'i' || lower == 'o' || lower == 'u')
        {
            ++vowel_count;
        }
    }
    std::cout << "元音个数: " << vowel_count << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
元音个数: 5
```

:::

### Exercise 3: In-Place Modification

Use the reference version of range-for to take the absolute value of every negative number in an array.

```text
修改前: 3 -7 1 -9 4 -6
修改后: 3 7 1 9 4 6
```

::: details Reference Solution

```cpp
#include <iostream>
#include <array>

int main()
{
    std::array<int, 6> value = {3, -7, 1, -9, 4, -6};
    std::cout << "修改前: ";
    for (auto& x : value)
    {
        std::cout << x << " ";
        if (x < 0)
        {
            x *= -1;
        }
    }
    std::cout << std::endl
              << "修改后: ";
    for (const auto& x : value)
    {
        std::cout << x << " ";
    }
    std::cout << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
修改前: 3 -7 1 -9 4 -6
修改后: 3 7 1 9 4 6
```

:::
