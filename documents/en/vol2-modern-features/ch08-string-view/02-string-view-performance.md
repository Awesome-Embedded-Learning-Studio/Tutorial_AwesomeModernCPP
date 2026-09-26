---
chapter: 8
cpp_standard:
- 17
description: Benchmarking the performance gains of replacing const string& with
  string_view
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 8: string_view Internals: A Non-Owning String View'
reading_time_minutes: 13
related:
- string_view Pitfalls and Best Practices
tags:
- host
- cpp-modern
- intermediate
title: string_view Performance Analysis
translation:
  source: documents/vol2-modern-features/ch08-string-view/02-string-view-performance.md
  source_hash: ae9ab072ab585447c07a77d6e1ba33bdb8fc023b97c49b68e2aae577c205fccc
  translated_at: '2026-09-25T16:17:40+00:00'
  engine: anthropic
  token_count: 6800
---
# string_view Performance Analysis: How Much Faster Is It Really? Let the Data Speak

In the previous article we dug into the internals of `string_view` and learned that it is a non-owning view built from "pointer + length". This time we let the data do the talking — exactly how much faster is `string_view` than `const std::string&`? Where are the gains largest? Can it ever actually be slower?

I ran quite a few benchmarks to write this article. Honestly, some results matched my intuition (`substr` really is much faster), and some caught me off guard (under certain ABIs, passing `string_view` by value is not always faster than `const string&`). Let's take them one by one.

Here is the environment for all of today's benchmarks: Linux 6.x (x86_64), GCC 13.2, compile flags `-std=c++17 -O2 -march=native`. The test machine is an ordinary x86 development board. All timing uses `std::chrono::high_resolution_clock`, and each test case is looped enough times to keep measurement error small.

## substr: O(1) vs O(n), a World of Difference

The most visible showcase of `string_view`'s performance advantage is the `substr` operation. We already analyzed the theory in the previous article: `string_view::substr` is just a pointer offset plus a length truncation, while `std::string::substr` needs a heap allocation plus a character copy. Now let's verify it with data.

First, a simple benchmark framework:

```cpp
#include <string>
#include <string_view>
#include <chrono>
#include <iostream>
#include <vector>

class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};
```

Then we benchmark `std::string::substr` and `string_view::substr` separately. The method: given a long string of 10000 characters, run 100000 substr operations on it, each taking a 50-character substring from a random starting position.

```cpp
#include <random>

constexpr int kStringLength = 10000;
constexpr int kSubstrLen = 50;
constexpr int kIterations = 100000;

// Generate a random string
std::string make_long_string(int len) {
    std::string s(len, 'a');
    for (int i = 0; i < len; ++i) {
        s[i] = static_cast<char>('a' + (i % 26));
    }
    return s;
}

void bench_string_substr(const std::string& s) {
    Timer t;
    volatile std::size_t sink = 0;  // prevent this from being optimized away
    for (int i = 0; i < kIterations; ++i) {
        auto sub = s.substr(i % (s.size() - kSubstrLen), kSubstrLen);
        sink += sub.size();
    }
    std::cout << "std::string::substr:   "
              << t.elapsed_ms() << " ms (sink=" << sink << ")\n";
}

void bench_string_view_substr(std::string_view sv) {
    Timer t;
    volatile std::size_t sink = 0;
    for (int i = 0; i < kIterations; ++i) {
        auto sub = sv.substr(i % (sv.size() - kSubstrLen), kSubstrLen);
        sink += sub.size();
    }
    std::cout << "string_view::substr:   "
              << t.elapsed_ms() << " ms (sink=" << sink << ")\n";
}

int main() {
    auto long_str = make_long_string(kStringLength);
    bench_string_substr(long_str);
    bench_string_view_substr(long_str);
    return 0;
}
```

The results I got:

```text
std::string::substr:   38.7 ms (sink=5000000)
string_view::substr:    0.4 ms (sink=5000000)
```

A gap of nearly 100x. The reason is simple: `std::string::substr` performed 100000 heap allocations and character copies (50 bytes each), while `string_view::substr` performed 100000 pointer additions and length adjustments. The gap grows even wider with longer strings and more frequent calls.

Of course, this test is a deliberately constructed extreme case. In a real project, if you only do a substr once in a while, you will never notice this difference. But if you are writing a parser that constantly splits, extracts, and skips over the input string, the `string_view` advantage becomes very pronounced.

## Function Parameters: string_view vs const string&

This is the scenario everyone cares about most: changing a function parameter from `const std::string&` to `std::string_view` — how much faster does that actually make it?

Let's analyze the theory first. When the signature is `const std::string&`, if the caller passes a `const char*` (say, a string literal or a string returned by a C API), the compiler has to implicitly construct a temporary `std::string` first, then pass the reference in. That temporary construction involves a `strlen` to compute the length plus a possible heap allocation. After the function returns, the temporary is destroyed and the heap memory is released.

When the signature is `std::string_view`, whether the argument is a `std::string`, a `const char*`, or a string literal, all that gets constructed is a 16-byte view object. Constructing from a `const char*` still requires one `strlen` (an O(n) walk), but no heap allocation. Constructing from a `std::string` skips even the `strlen` — it just takes `data()` and `size()` directly.

Let's write a benchmark to verify. Test scenario: a function takes a string parameter and does some simple processing with it (counting character occurrences), written with both signatures, then called by passing in a `std::string` and a `const char*` respectively.

```cpp
#include <cctype>

// Version 1: const string& parameter
int count_digits_v1(const std::string& s) {
    int count = 0;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ++count;
        }
    }
    return count;
}

// Version 2: string_view parameter
int count_digits_v2(std::string_view sv) {
    int count = 0;
    for (char c : sv) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ++count;
        }
    }
    return count;
}

void bench_param_passing() {
    constexpr int kCalls = 1000000;
    std::string str_data = "abc123def456ghi789jkl012mno345";
    const char* c_data = "abc123def456ghi789jkl012mno345";

    // Test 1: pass std::string to a const string& parameter
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v1(str_data);
        }
        std::cout << "const string& + string arg: "
                  << t.elapsed_ms() << " ms\n";
    }

    // Test 2: pass const char* to a const string& parameter (requires constructing a temporary)
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v1(c_data);  // implicitly constructs a temporary string
        }
        std::cout << "const string& + char* arg:  "
                  << t.elapsed_ms() << " ms\n";
    }

    // Test 3: pass std::string to a string_view parameter
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v2(str_data);
        }
        std::cout << "string_view   + string arg: "
                  << t.elapsed_ms() << " ms\n";
    }

    // Test 4: pass const char* to a string_view parameter
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v2(c_data);
        }
        std::cout << "string_view   + char* arg:  "
                  << t.elapsed_ms() << " ms\n";
    }
}
```

The results I got:

```text
const string& + string arg:  12.3 ms
const string& + char* arg:   95.7 ms   ← 8x slower!
string_view   + string arg:  12.1 ms
string_view   + char* arg:   35.2 ms   ← 3x faster
```

Let's put the parameter-passing mechanics of the three signatures and the four numbers above into one diagram:

![String passing cost comparison: by value, const reference, and string_view](./02-sv-passing-cost.drawio)

The key data sits in the contrast between the second and fourth rows. When the caller passes a `const char*`, the `const string&` version balloons to 95 ms because it implicitly constructs a million temporary `std::string` objects. The `string_view` version still needs one `strlen` on the `const char*`, but with no heap allocation it only took 35 ms. As for passing in a `std::string`, the two are basically on par — `const string&` passes the reference directly, `string_view` constructs a 16-byte view; both are a matter of a few clock cycles, and the difference is within noise.

This test hands us a very practical conclusion: if your function might be called with a mix of `const char*`, string literals, and `std::string`, `string_view` is the better parameter type. If your function only ever receives `std::string`, there is little difference between the two.

## Reducing Temporary string Allocations

Beyond explicit function calls, `string_view` also helps us cut down on implicit temporary `std::string` allocations. A typical scenario is string comparison:

```cpp
// Old style: every comparison may construct a temporary string
bool is_http_method(const std::string& method) {
    return method == "GET" || method == "POST" || method == "PUT"
        || method == "DELETE" || method == "PATCH";
}

// New style: zero-allocation comparison
bool is_http_method_sv(std::string_view method) {
    return method == "GET" || method == "POST" || method == "PUT"
        || method == "DELETE" || method == "PATCH";
}
```

The comparison operator (`==`) between `string_view` and a string literal constructs a lightweight temporary `string_view` object (16 bytes, no heap allocation) and then compares character by character. When a `const std::string&` is compared against a string literal, the literal is implicitly converted to a temporary `std::string` (which may involve a heap allocation — some compilers optimize that conversion away, but the standard does not guarantee it).

Another common source of "temporary strings" is function return values. Consider this pattern:

```cpp
// A C API that returns const char*
const char* get_env_var(const char* name);

// Wrapper function: the old version returns string
std::string get_env_string(const char* name) {
    const char* val = get_env_var(name);
    return val ? std::string(val) : std::string("");
}

// Wrapper function: the new version returns string_view
std::string_view get_env_view(const char* name) {
    const char* val = get_env_var(name);
    return val ? std::string_view(val) : std::string_view();
}
```

The second version comes with a precondition: the pointer returned by `get_env_var` must remain valid for the long term. In the environment-variable scenario this usually holds (environment variables do not disappear over the process's lifetime). But if the C API returns an internal static buffer (`inet_ntoa`, for example) that gets overwritten on the next call, then `string_view` is risky. Once again: before using `string_view`, you must confirm the lifetime of the underlying data.

## Avoiding Unnecessary string Construction

Sometimes all we want is to read string data, yet we accidentally trigger a `std::string` construction. Take a practical example — string hash table lookup:

```cpp
#include <unordered_map>
#include <string_view>

// Old style: lookup requires constructing a string
std::unordered_map<std::string, int> old_map;
old_map["apple"] = 1;
old_map["banana"] = 2;

int lookup_old(const char* key) {
    auto it = old_map.find(key);  // implicitly constructs a temporary string
    return (it != old_map.end()) ? it->second : -1;
}

// New style: use a transparent comparator for zero-construction lookup
// C++20's unordered_map supports heterogeneous lookup
// C++17's map/set support it; unordered_map has to wait for C++20
// Here we use string_view as the key to demonstrate a similar idea
std::unordered_map<std::string_view, int> sv_map;
// Note: the external data that sv_map's keys point to must outlive the map

int lookup_sv(std::string_view key) {
    auto it = sv_map.find(key);
    return (it != sv_map.end()) ? it->second : -1;
}
```

Strictly speaking, C++17's `std::unordered_map` does not support heterogeneous lookup yet (that was added in C++20 as the `std::unordered_map::find(K)` overload), so in `old_map.find(key)` the `const char*` still gets implicitly constructed into a `std::string`. But in C++20, you can enable the `is_transparent` feature on an `unordered_map` so that lookup skips the temporary construction entirely. `string_view` is a key link in that chain.

## Embedded in Practice: Command Parsing and Protocol Handling

In embedded development, `string_view`'s "zero-allocation" property is extremely valuable. An MCU's RAM is usually only a few dozen KB to a few hundred KB, and heap space is severely limited; frequent `std::string` allocation is not only slow, it can also fragment memory and eventually crash the system.

Let's look at a real serial-protocol parsing scenario. Suppose our embedded device receives JSON-RPC style commands over the serial port, in the format `{"method":"xxx","params":"yyy"}`. We need to extract the method and params fields.

```cpp
#include <string_view>
#include <cstring>

// Simulated UART receive buffer
constexpr int kBufSize = 256;
static char uart_buf[kBufSize];
static int uart_len = 0;

/// @brief Find the value of a JSON field in the buffer
/// @param json the JSON string view
/// @param key the key to search for
/// @return the value as a string_view, or an empty view if not found
std::string_view find_json_field(std::string_view json,
                                  std::string_view key) {
    // Build the search pattern: "key":"
    // This uses the simplest linear search; production code should use a real JSON parser
    auto key_pattern = key;
    auto pos = json.find(key_pattern);
    if (pos == std::string_view::npos) {
        return {};
    }
    // Skip past the key and the ":" part
    auto rest = json.substr(pos + key_pattern.size());
    // Skip whitespace and colons
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == ':'
           || rest.front() == '"')) {
        rest.remove_prefix(1);
    }
    // Find the value's closing quote
    auto end = rest.find('"');
    if (end == std::string_view::npos) {
        return rest;
    }
    return rest.substr(0, end);
}

void process_uart_command() {
    std::string_view input(uart_buf, static_cast<std::size_t>(uart_len));

    auto method = find_json_field(input, "method");
    auto params = find_json_field(input, "params");

    if (method == "led_set") {
        int brightness = 0;
        for (char c : params) {
            if (c >= '0' && c <= '9') {
                brightness = brightness * 10 + (c - '0');
            }
        }
        hal_pwm_set_duty(brightness);
    } else if (method == "reboot") {
        hal_system_reset();
    }
}
```

This parser needs no heap allocation at all — every operation happens between `string_view` objects on the stack. `uart_buf` is a static array, and the `string_view` merely "glances" at it. On an STM32F103 with only 20KB of RAM, this zero-allocation way of handling strings means you can use it with confidence, without worrying about running out of memory or fragmenting it.

Of course, this JSON parser is toy-grade — it does not handle escaping, nesting, arrays, or other complex cases. But it shows the core value of `string_view` in resource-constrained environments: string manipulation capability at minimal cost. If you need a complete JSON parser, consider libraries such as ArduinoJson, which also make heavy internal use of `string_view`-like non-owning reference techniques.

## Reference Resources

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [C++ Stories: Performance of string_view vs string](https://www.cppstories.com/2018/07/string-view-perf/)
- [StackOverflow: How exactly is string_view faster than const string&?](https://stackoverflow.com/questions/40127965/how-exactly-is-stdstring-view-faster-than-const-stdstring)
- [cppreference: std::chrono](https://en.cppreference.com/w/cpp/chrono)
