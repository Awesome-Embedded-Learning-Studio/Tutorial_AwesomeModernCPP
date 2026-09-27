---
chapter: 8
cpp_standard:
- 17
description: Benchmarking the performance gains of replacing const string& with string_view
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
  source_hash: e9f06f7fda85107abb457fff7e4e04281975064a991a39908e6022ae5cc2b05a
  translated_at: '2026-09-27T05:12:46+00:00'
  engine: anthropic
  token_count: 5000
---
# string_view Performance Analysis: Exactly How Much Faster—Let the Data Speak

In the previous article we dug into the internals of `string_view` and learned that it is a non-owning "pointer + length" view. This time we let the data do the talking—how much faster is `string_view` than `const std::string&`? Which scenarios benefit the most? And are there cases where it actually ends up slower?

To write this article, I ran quite a few benchmarks. Honestly, some results matched my intuition (substr really is much faster), while others caught me off guard (under certain ABIs, passing `string_view` by value is not always faster than `const string&`). Let's walk through them one by one.

All of today's benchmarks ran in the following environment: Linux 6.x (x86_64), GCC 13.2, compiled with `-std=c++17 -O2 -march=native`. The test machine is an ordinary x86 dev board. All timings use `std::chrono::high_resolution_clock`, and every test case loops enough times to keep measurement error down.

## substr: The Night-and-Day Difference Between O(1) and O(n)

The most visible payoff of `string_view`'s performance advantage is the `substr` operation. We already analyzed this from first principles in the previous article: `string_view::substr` is just a pointer offset plus a length adjustment, while `std::string::substr` needs a heap allocation plus a character copy. Now let's verify it with data.

First, a simple benchmark harness:

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

Then we benchmark `std::string::substr` and `string_view::substr` separately. The setup: given a long string of 10,000 characters, perform 100,000 substr operations on it, each taking a 50-character substring at a random starting position.

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
    volatile std::size_t sink = 0;  // prevent the optimizer from removing the work
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

The benchmark is right below (the Timer class and both test functions live in the same source file)—click "Try It Yourself" to run it directly. Absolute numbers vary from machine to machine; the order-of-magnitude gap is what matters:

<OnlineCompilerDemo
  title="Hands-On Measurement: The Heap-Allocation Cost of substr"
  source-path="code/examples/vol2/43_substr_benchmark.cpp"
  description="Compare the two substr flavors online. One run on my machine: std::string::substr 38.7 ms vs string_view::substr 0.4 ms (sink is 5000000 in both)—nearly a hundredfold gap."
  run-options="-O2 -std=c++17"
  allow-run
/>

Nearly a 100x gap (measured on my machine; your ratio will differ, but the direction won't). The reason is simple: `std::string::substr` performed 100,000 heap allocations and character copies (50 bytes each), while `string_view::substr` performed 100,000 pointer additions and length adjustments. The gap grows even wider when the strings are longer and the calls more frequent.

Of course, this test is a deliberately constructed extreme case. In a real project, if you only do a substr once in a while, you may never notice the difference. But if you are writing a parser that constantly splits, extracts, and skips over the input string, `string_view`'s advantage becomes very pronounced.

## Function Parameters: string_view vs const string&

This is the scenario everyone cares about most: switching a function parameter from `const std::string&` to `std::string_view`—how much speed does that actually buy?

Let's analyze the theory first. When the signature is `const std::string&` and the caller passes a `const char*` (say, a string literal or a string returned from a C API), the compiler must first implicitly construct a temporary `std::string`, then pass a reference to it. That temporary construction involves a `strlen` to compute the length plus a possible heap allocation. When the function returns, the temporary is destroyed and the heap memory is released.

When the signature is `std::string_view`, whether the argument is a `std::string`, a `const char*`, or a string literal, all that gets constructed is a 16-byte view object. Constructing from a `const char*` still needs one `strlen` (an O(n) walk), but no heap allocation. Constructing from a `std::string` doesn't even need the `strlen`—it just grabs `data()` and `size()`.

Let's write a benchmark to verify. The scenario: a function takes a string parameter and does some light processing (counting character occurrences), with both signatures, then gets called with a `std::string` and with a `const char*` in turn.

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

    // Test 2: pass const char* to a const string& parameter (requires temporary construction)
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

The benchmark is right below—click "Try It Yourself" to run it. The four numbers from one run on my machine (row 2 is about 8x slower than row 1, and row 4 is about 3x faster than row 2):

<OnlineCompilerDemo
  title="Hands-On Measurement: The Hidden Cost of a Parameter Signature"
  source-path="code/examples/vol2/44_param_passing_benchmark.cpp"
  description="Measure all four argument-passing combinations online. One run on my machine: const string& + char* is the slowest (95.7 ms—a million temporary strings implicitly constructed), string_view + char* takes 35.2 ms, and with a string argument the two are neck and neck (about 12 ms)."
  run-options="-O2 -std=c++17"
  allow-run
/>

Here is the passing mechanism of the three signatures together with the four numbers above, in one diagram:

![String passing cost comparison: by value, const reference, and string_view](./02-sv-passing-cost.drawio)

The key data sits in the contrast between rows 2 and 4. When the caller passes a `const char*`, the `const string&` version has to implicitly construct a million temporary `std::string` objects, ballooning the time to 95 ms. The `string_view` version also needs one `strlen` per `const char*`, but no heap allocation, so it took only 35 ms. As for passing a `std::string`, the two perform essentially the same—`const string&` passes the reference directly, `string_view` constructs a 16-byte view; both are a matter of a few clock cycles, and the difference is within noise.

The practical takeaway from this test: if your function might be called with a mix of `const char*`, string literals, and `std::string`, `string_view` is the better parameter type. If your function only ever receives `std::string`, there is little difference between the two.

## Cutting Down on Temporary string Allocations

Beyond explicit function calls, `string_view` also helps us reduce implicit temporary `std::string` allocations. A classic scenario is string comparison:

```cpp
// Old way: every comparison may construct a temporary string
bool is_http_method(const std::string& method) {
    return method == "GET" || method == "POST" || method == "PUT"
        || method == "DELETE" || method == "PATCH";
}

// New way: zero-allocation comparison
bool is_http_method_sv(std::string_view method) {
    return method == "GET" || method == "POST" || method == "PUT"
        || method == "DELETE" || method == "PATCH";
}
```

The comparison operator (`==`) between a `string_view` and a string literal constructs a lightweight temporary `string_view` (16 bytes, no heap allocation) and then compares character by character. When a `const std::string&` is compared against a string literal, the literal gets implicitly converted to a temporary `std::string` (which may involve a heap allocation—some compilers optimize this conversion away, but the standard doesn't guarantee it).

Another common source of "temporary strings" is function return values. Consider this pattern:

```cpp
// A C API returning const char*
const char* get_env_var(const char* name);

// Wrapper: the old version returns string
std::string get_env_string(const char* name) {
    const char* val = get_env_var(name);
    return val ? std::string(val) : std::string("");
}

// Wrapper: the new version returns string_view
std::string_view get_env_view(const char* name) {
    const char* val = get_env_var(name);
    return val ? std::string_view(val) : std::string_view();
}
```

The second version carries a precondition: the pointer returned by `get_env_var` must stay valid for the long term. For environment variables this usually holds (environment variables don't disappear over the process's lifetime). But if the C API returns a pointer into an internal static buffer (`inet_ntoa`, for example) that the next call overwrites, `string_view` becomes risky. To hammer the point home once more: before using `string_view`, you must confirm the lifetime of the underlying data.

## Avoiding Unnecessary string Construction

Sometimes all we want is to read string data, yet we accidentally trigger a `std::string` construction anyway. Take a real example—hash-table string lookup:

```cpp
#include <unordered_map>
#include <string_view>

// Old way: lookup needs to construct a string
std::unordered_map<std::string, int> old_map;
old_map["apple"] = 1;
old_map["banana"] = 2;

int lookup_old(const char* key) {
    auto it = old_map.find(key);  // implicitly constructs a temporary string
    return (it != old_map.end()) ? it->second : -1;
}

// New way: use a transparent comparator, zero construction at lookup time
// C++20 unordered_map supports heterogeneous lookup
// C++17 map/set support it; unordered_map has to wait for C++20
// For now we can use string_view as the key to demonstrate the same idea
std::unordered_map<std::string_view, int> sv_map;
// Note: the external data that sv_map's keys point to must outlive the map

int lookup_sv(std::string_view key) {
    auto it = sv_map.find(key);
    return (it != sv_map.end()) ? it->second : -1;
}
```

Strictly speaking, C++17 `std::unordered_map` doesn't support heterogeneous lookup yet (that arrived in C++20 via the `std::unordered_map::find(K)` overload), so in `old_map.find(key)` the `const char*` still gets implicitly constructed into a `std::string`. In C++20, though, you can enable the `is_transparent` machinery for `unordered_map` and let lookups skip the temporary construction entirely. `string_view` is a key link in that chain.

## Embedded in Practice: Command Parsing and Protocol Handling

In embedded development, `string_view`'s "zero allocation" property is extremely valuable. An MCU typically has only tens to a few hundred KB of RAM, and heap space is severely limited; frequent `std::string` allocations are not just slow—they can also fragment memory, eventually crashing the system.

Let's look at a real UART protocol-parsing scenario. Suppose our embedded device receives JSON-RPC-style commands over the serial port, in the format `{"method":"xxx","params":"yyy"}`. We need to extract the method and params fields.

```cpp
#include <string_view>
#include <cstring>

// Simulated UART receive buffer
constexpr int kBufSize = 256;
static char uart_buf[kBufSize];
static int uart_len = 0;

/// @brief Find the value of a JSON field in the buffer
/// @param json the JSON string view
/// @param key the key to look for
/// @return a string_view of the value; an empty view if not found
std::string_view find_json_field(std::string_view json,
                                  std::string_view key) {
    // Build the search pattern: "key":
    // This uses the simplest possible linear search; production code should use a real JSON parser
    auto key_pattern = key;
    auto pos = json.find(key_pattern);
    if (pos == std::string_view::npos) {
        return {};
    }
    // Skip past the key and the ":" part
    auto rest = json.substr(pos + key_pattern.size());
    // Skip whitespace and the colon
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

This parser needs no heap allocation at all—every operation happens between `string_view` objects on the stack. `uart_buf` is a static array; the `string_view` merely "takes a look" at it. On an STM32F103 with only 20 KB of RAM, this zero-allocation string handling means you can use it with confidence, without worrying about running out of memory or fragmenting the heap.

Of course, this JSON parser is toy-grade—it handles none of the messy parts like escapes, nesting, or arrays. But it demonstrates `string_view`'s core value in a resource-constrained environment: string manipulation capability at minimal cost. If you need a full JSON parser, consider libraries like ArduinoJson, which also lean heavily on non-owning reference techniques similar to `string_view` internally.

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [C++ Stories: Performance of string_view vs string](https://www.cppstories.com/2018/07/string-view-perf/)
- [StackOverflow: How exactly is string_view faster than const string&?](https://stackoverflow.com/questions/40127965/how-exactly-is-stdstring-view-faster-than-const-stdstring)
- [cppreference: std::chrono](https://en.cppreference.com/w/cpp/chrono)
