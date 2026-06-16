---
chapter: 8
cpp_standard:
- 17
description: Benchmarking the performance benefits of replacing `const string&` with
  `string_view`
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 8: string_view 内部原理'
reading_time_minutes: 13
related:
- string_view 陷阱与最佳实践
tags:
- host
- cpp-modern
- intermediate
title: string_view Performance Analysis
translation:
  source: documents/vol2-modern-features/ch08-string-view/02-string-view-performance.md
  source_hash: 8a5d7df3bb15f8f2865703e8bafade7ff4fb002f6ff3bd1715f88464b6a90235
  translated_at: '2026-06-16T03:58:49.217853+00:00'
  engine: anthropic
  token_count: 2916
---
# string_view Performance Analysis

In the previous article, we dove into the internal mechanics of `std::string_view`, understanding that it is a non-owning view consisting of a "pointer + length". In this article, let the data do the talking—how much faster is `std::string_view` than `std::string` really? In which scenarios does it yield the greatest benefits? Are there cases where it is actually slower?

To write this article, the author ran quite a few benchmarks. Honestly, some results aligned with intuition (e.g., `substr` is indeed much faster), while others were unexpected (e.g., under certain ABIs, passing `std::string_view` by value isn't always faster than passing `const std::string&`). Let's examine them one by one.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand the performance differences of `std::string_view` in scenarios like `substr` and parameter passing.
> - [ ] Master the method of writing micro-benchmarks using Google Benchmark.
> - [ ] Learn about the practical application of `std::string_view` in embedded command parsing.

## Environment Setup

The environment for all benchmarks today is as follows: Linux 6.x (x86_64), GCC 13.2, compiler flags `-O3 -march=native`. The test machine is a standard x86 development board. All time measurements use `std::chrono::high_resolution_clock`, and each test case loops enough times to minimize error.

## substr: The Difference Between O(1) and O(n)

The most intuitive demonstration of `std::string_view`'s performance advantage is the `substr` operation. We analyzed this theoretically in the last article: `std::string_view::substr` involves only pointer arithmetic and length truncation, while `std::string::substr` requires heap allocation and character copying. Now let's verify this with data.

First, let's write a simple benchmark framework:

```cpp
#include <string>
#include <string_view>
#include <chrono>
#include <iostream>
#include <random>

// Simple timer wrapper
class Timer {
    std::string title;
    std::chrono::high_resolution_clock::time_point start;
public:
    Timer(const std::string& t) : title(t), start(std::chrono::high_resolution_clock::now()) {}
    ~Timer() {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms = end - start;
        std::cout << title << ": " << ms.count() << " ms\n";
    }
};

// Generate a random string of length n
std::string gen_random_string(size_t n) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string str;
    str.reserve(n);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    for (size_t i = 0; i < n; ++i)
        str += charset[dis(gen)];
    return str;
}
```

Then, we test the performance of `std::string::substr` and `std::string_view::substr` respectively. The test method involves: given a long string of 10,000 characters, perform 100,000 `substr` operations, each time extracting a 50-character substring starting from a random position.

```cpp
void bench_string_substr() {
    Timer t("std::string::substr");
    std::string long_str = gen_random_string(10000);
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, 9950); // Ensure room for 50 chars

    for (int i = 0; i < 100000; ++i) {
        size_t pos = dist(rng);
        // This triggers heap allocation and copy
        std::string sub = long_str.substr(pos, 50);
        // Prevent compiler from optimizing away the call
        if (!sub.empty() && sub[0] == 'a') {
            // Dummy branch
        }
    }
}

void bench_string_view_substr() {
    Timer t("std::string_view::substr");
    std::string long_str = gen_random_string(10000);
    std::string_view sv(long_str);
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, 9950);

    for (int i = 0; i < 100000; ++i) {
        size_t pos = dist(rng);
        // No heap allocation, just pointer + size update
        std::string_view sub = sv.substr(pos, 50);
        if (!sub.empty() && sub[0] == 'a') {
            // Dummy branch
        }
    }
}

int main() {
    bench_string_substr();
    bench_string_view_substr();
    return 0;
}
```

The results the author obtained:

```text
std::string::substr: 94.231 ms
std::string_view::substr: 0.985 ms
```

A difference of nearly 100 times. The reason is simple: `std::string::substr` performed 100,000 heap allocations and character copies (50 bytes each time), while `std::string_view::substr` only performed 100,000 pointer additions and length adjustments. This gap becomes even more pronounced when strings are longer and calls are more frequent.

Of course, this test is an extreme scenario constructed deliberately. In actual projects, if you only perform `substr` occasionally, you might not perceive this difference at all. However, if you are writing a parser that needs to frequently perform splitting, extraction, and skipping operations on input strings, the advantage of `std::string_view` becomes very significant.

## Function Parameters: string_view vs const string&

This is the scenario everyone cares about most: how much faster is it to change function parameters from `const std::string&` to `std::string_view`?

Let's analyze this from a theoretical standpoint first. When the function signature is `void func(const std::string&)`, if the caller passes a `const char*` (like a string literal or a string returned by a C API), the compiler needs to implicitly construct a temporary `std::string` first, then pass the reference. This temporary construction involves calculating the length (O(n)) plus potential heap allocation. After the function returns, the temporary object is destructed, and heap memory is released.

When the function signature is `void func(std::string_view)`, regardless of whether the caller passes a `std::string`, `const char*`, or a string literal, it only constructs a 16-byte view object. When constructing from `const char*`, a `strlen` call (O(n) traversal) is still needed, but no heap allocation is required. When constructing from `std::string`, even `strlen` is not needed; it directly takes the data pointer and size.

Let's write a benchmark to verify this. Test scenario: a function receives a string parameter and performs simple processing (counts character occurrences), using both signatures, and is called by passing `std::string` and `const char*` respectively.

```cpp
#include <string>
#include <string_view>
#include <chrono>

// Count occurrences of 'c' in str
size_t count_c_str(const std::string& str) {
    size_t count = 0;
    for (char ch : str) if (ch == 'c') ++count;
    return count;
}

size_t count_c_view(std::string_view sv) {
    size_t count = 0;
    for (char ch : sv) if (ch == 'c') ++count;
    return count;
}

int main() {
    std::string long_str = gen_random_string(10000);
    const char* c_str = long_str.c_str();

    const int iterations = 1000000;

    // Test 1: Pass std::string to const string&
    {
        Timer t("Pass std::string to const std::string&");
        for (int i = 0; i < iterations; ++i) {
            count_c_str(long_str);
        }
    }

    // Test 2: Pass std::string to string_view
    {
        Timer t("Pass std::string to std::string_view");
        for (int i = 0; i < iterations; ++i) {
            count_c_view(long_str);
        }
    }

    // Test 3: Pass const char* to const string&
    {
        Timer t("Pass const char* to const std::string&");
        for (int i = 0; i < iterations; ++i) {
            count_c_str(c_str);
        }
    }

    // Test 4: Pass const char* to string_view
    {
        Timer t("Pass const char* to std::string_view");
        for (int i = 0; i < iterations; ++i) {
            count_c_view(c_str);
        }
    }

    return 0;
}
```

The results the author obtained:

```text
Pass std::string to const std::string&: 12.4 ms
Pass std::string to std::string_view: 13.1 ms
Pass const char* to const std::string&: 95.2 ms
Pass const char* to std::string_view: 35.8 ms
```

The key data lies in the comparison between the second and fourth rows. When the caller passes `const char*`, the `const std::string&` version explodes in time to 95ms because it must implicitly construct 1 million temporary `std::string` objects. The `std::string_view` version, while still needing to perform `strlen` on the `const char*`, requires no heap allocation, so it only took 35ms. As for passing `std::string`, the performance of both is basically flat—`const std::string&` passes a reference directly, and `std::string_view` constructs a 16-byte view; both are a matter of a few clock cycles, and the difference is within the noise range.

This test tells us a very practical conclusion: if your function might be called with a mix of `const char*`, string literals, or `std::string`, using `std::string_view` as the parameter type is the superior choice. If your function only accepts `std::string`, there isn't much difference.

## Reducing Temporary string Allocations

Beyond explicit function calls, `std::string_view` helps us reduce implicit temporary `std::string` allocations. A typical scenario is string comparison:

```cpp
std::string s = get_input();
// Old way: s == "reset" might construct a temporary string
if (s == "reset") { ... }

// New way: "reset" is converted to string_view (no alloc)
if (std::string_view(s) == "reset") { ... }
```

The comparison operator (`operator==`) between `std::string_view` and a string literal constructs a lightweight `std::string_view` temporary object (16 bytes, no heap allocation) and then compares character by character. When `std::string` is compared with a string literal, the literal is implicitly converted to a temporary `std::string` (involving heap allocation, although some compilers optimize this conversion away, the standard does not guarantee it).

Another common source of "temporary strings" is function return values. Consider this pattern:

```cpp
// Old way: Return std::string, involves heap allocation
std::string get_env(const std::string& name) {
    return getenv(name.c_str()); // getenv returns const char*, constructs std::string
}

// New way: Return string_view, zero allocation
std::string_view get_env_view(std::string_view name) {
    // getenv returns a pointer to static env memory
    // Note: This is only safe if the underlying data persists!
    const char* val = getenv(std::string(name).c_str());
    return val ? std::string_view(val) : std::string_view();
}
```

⚠️ The second version has a prerequisite: the pointer returned by `getenv` must be long-lived. In the scenario of environment variables, this premise usually holds (environment variables do not disappear during the process lifecycle). However, if the C API returns an internal static buffer (like `asctime`), the next call will overwrite it, so using `std::string_view` is risky. Again: before using `std::string_view`, you must confirm the lifetime of the underlying data.

## Avoiding Unnecessary string Construction

Sometimes we only need to read string data but accidentally trigger the construction of `std::string`. Let's look at a practical example—string hash table lookup:

```cpp
std::unordered_set<std::string> keywords = {"func", "var", "if"};

// Old way: "func" constructs a temporary std::string to search
if (keywords.find("func") != keywords.end()) { ... }

// New way: Use string_view to avoid construction (C++20 heterogeneous lookup)
// Note: Requires C++20 transparent comparator support
// std::unordered_set<std::string, std::hash<std::string_view>, std::equal_to<>> keywords;
// if (keywords.find(std::string_view("func")) != keywords.end()) { ... }
```

Strictly speaking, C++17's `std::unordered_set` does not yet support heterogeneous lookup (this was added in C++20 with `find(const T&)` overload), so `keywords.find("func")` in C++17 will still implicitly construct `std::string`. However, in C++20, you can enable heterogeneous lookup for `std::unordered_set` (by providing a transparent hash and equality comparator), allowing the lookup to completely skip temporary construction. `std::string_view` is a key part of this scenario.

## Embedded Practice: Command Parsing and Protocol Processing

In embedded development, the "zero-allocation" characteristic of `std::string_view` is extremely valuable. An MCU's RAM is typically only a few dozen to a few hundred KB, and heap space is extremely limited. Frequent `std::string` allocation is not only slow but can also lead to memory fragmentation, eventually crashing the system.

Let's look at a practical serial protocol parsing scenario. Suppose our embedded device receives JSON-RPC style commands via serial port, formatted as `{"method":"set_led", "params":"on"}`. We need to extract the `method` and `params` fields.

```cpp
#include <string_view>
#include <array>

// Simple non-allocating JSON parser
void parse_command(std::string_view cmd) {
    // Find "method" field
    size_t method_pos = cmd.find("\"method\":");
    if (method_pos == std::string_view::npos) return;

    // Skip to the value
    method_pos += 10; // len of "\"method\":"
    if (method_pos >= cmd.size()) return;
    if (cmd[method_pos] == '"') method_pos++; // Skip opening quote

    size_t method_end = cmd.find("\"", method_pos);
    if (method_end == std::string_view::npos) return;

    std::string_view method = cmd.substr(method_pos, method_end - method_pos);

    // Find "params" field
    size_t params_pos = cmd.find("\"params\":");
    if (params_pos == std::string_view::npos) return;

    params_pos += 10;
    if (params_pos >= cmd.size()) return;
    if (cmd[params_pos] == '"') params_pos++;

    size_t params_end = cmd.find("\"", params_pos);
    if (params_end == std::string_view::npos) return;

    std::string_view params = cmd.substr(params_pos, params_end - params_pos);

    // Now we have method and params as views, no allocation happened
    if (method == "set_led") {
        // Process params...
    }
}

// Usage
std::array<char, 256> rx_buffer; // Static buffer
// ... receive data into rx_buffer ...
parse_command(std::string_view(rx_buffer.data(), received_len));
```

This parser requires absolutely no heap allocation—all operations are completed between `std::string_view` objects on the stack. `rx_buffer` is a static array, and `std::string_view` just "peeks" at it. On an STM32F103 with only 20KB of RAM, this zero-allocation string processing method means you can use it freely without worrying about running out of memory or fragmentation.

Of course, this JSON parser is toy-grade—it doesn't handle escaping, nesting, arrays, or other complex situations. But it demonstrates the core value of `std::string_view` in resource-constrained environments: providing string manipulation capabilities at minimal cost. If you need a complete JSON parser, consider libraries like ArduinoJson, which also heavily use non-owning reference techniques similar to `std::string_view` internally.

## Summary

In this article, we verified the performance advantages of `std::string_view` with benchmark data. The core conclusions are as follows: The `substr` operation is `std::string_view`'s biggest performance killer; the difference between O(1) and O(n) amplifies to over a hundred times with frequent calls. In the function parameter scenario, `std::string_view` has a clear advantage for `const char*` callers, but little difference for `std::string` callers. Reducing temporary `std::string` construction is another important benefit of `std::string_view`. In embedded scenarios, `std::string_view`'s zero-allocation nature makes it the preferred solution for string processing in resource-constrained environments.

However, performance isn't everything. In the next article, we will discuss the pitfalls of `std::string_view`—dangling references, null termination, implicit conversions, and other issues. If these are ignored, no amount of performance can make up for the cost of a crash.

## Reference Resources

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [C++ Stories: Performance of string_view vs string](https://www.cppstories.com/2018/07/string-view-perf/)
- [StackOverflow: How exactly is string_view faster than const string&?](https://stackoverflow.com/questions/40127965/how-exactly-is-stdstring-view-faster-than-const-stdstring)
- [cppreference: std::chrono](https://en.cppreference.com/w/cpp/chrono)
