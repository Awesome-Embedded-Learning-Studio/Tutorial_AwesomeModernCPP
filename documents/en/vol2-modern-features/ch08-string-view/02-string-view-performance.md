# string_view Performance Analysis

In the previous article, we dove into the internals of `string_view`, learning that it is a non-owning view consisting of a pointer and a length. In this article, we let the data speak—exactly how much faster is `string_view` than `const std::string&`? In which scenarios do the benefits peak? Are there cases where it is actually slower?

To write this article, the author ran quite a few benchmarks. To be honest, some results aligned with intuition (`substr` is indeed much faster), while others were surprising (under certain ABIs, passing `string_view` by value is not always faster than `const string&`). Let's examine them one by one.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Understand the performance differences of `string_view` in scenarios like `substr` and parameter passing
> - [ ] Master the method of writing micro-benchmarks using `<chrono>`
> - [ ] Learn about the practical application of `string_view` in embedded command parsing

## Environment Setup

The environment for all benchmarks today is as follows: Linux 6.x (x86_64), GCC 13.2, compiler flag `-std=c++17 -O2 -march=native`. The test machine is a standard x86 development board. All time measurements use `std::chrono::high_resolution_clock`, and each test case loops enough times to minimize error.

## substr: The World of Difference Between O(1) and O(n)

The most intuitive demonstration of `string_view`'s performance advantage is the `substr` operation. We analyzed this from a theoretical perspective in the previous article: `string_view::substr` is just a pointer offset and length truncation, whereas `std::string::substr` requires heap allocation plus character copying. Now let's verify this with data.

First, we write a simple benchmarking framework:

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

Then we test the performance of `std::string::substr` and `string_view::substr` respectively. The test method is: given a long string of 10,000 characters, we perform 100,000 `substr` operations on it, each time extracting a 50-character substring from a random starting position.

```cpp
#include <random>

constexpr int kStringLength = 10000;
constexpr int kSubstrLen = 50;
constexpr int kIterations = 100000;

// 生成随机字符串
std::string make_long_string(int len) {
    std::string s(len, 'a');
    for (int i = 0; i < len; ++i) {
        s[i] = static_cast<char>('a' + (i % 26));
    }
    return s;
}

void bench_string_substr(const std::string& s) {
    Timer t;
    volatile std::size_t sink = 0;  // 防止优化掉
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

The results the author got:

```text
std::string::substr:   38.7 ms (sink=5000000)
string_view::substr:    0.4 ms (sink=5000000)
```

A gap of nearly 100 times. The reason is simple: `std::string::substr` performed 100,000 heap allocations and character copies (50 bytes each time), while `string_view::substr` only performed 100,000 pointer additions and length adjustments. This gap becomes even more pronounced in scenarios with longer strings and more frequent calls.

Of course, this test is an intentionally constructed extreme scenario. In real projects, if you only occasionally perform a `substr` operation, you might not even notice the difference. However, if you are writing a parser that frequently splits, extracts, and skips parts of an input string, the advantage of `string_view` becomes very prominent.

## Function Parameters: string_view vs const string&

This is the scenario everyone cares about most: how much faster is it to change a function parameter from `const std::string&` to `std::string_view`?

Let's analyze this from a theoretical perspective first. When the function signature is `const std::string&`, if the caller passes in a `const char*` (such as a string literal or a string returned by a C API), the compiler needs to implicitly construct a temporary `std::string` first, and then pass a reference to it. This temporary construction involves a `strlen` to calculate the length, plus potential heap allocation. After the function returns, the temporary object is destructed, and the heap memory is freed.

When the function signature is `std::string_view`, regardless of whether a `std::string`, `const char*`, or string literal is passed in, it only constructs a 16-byte view object. When constructing from a `const char*`, a `strlen` (an O(n) traversal) is still needed, but no heap allocation is required. When constructing from a `std::string`, not even a `strlen` is needed—it directly takes the `data()` and `size()`.

Let's write a benchmark to verify this. Test scenario: a function receives a string parameter and performs simple processing (counting character occurrences), using both signatures, and then calling it with `std::string` and `const char*` respectively.

```cpp
#include <cctype>

// 版本一：const string& 参数
int count_digits_v1(const std::string& s) {
    int count = 0;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ++count;
        }
    }
    return count;
}

// 版本二：string_view 参数
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

    // 测试 1：传 std::string 给 const string& 参数
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v1(str_data);
        }
        std::cout << "const string& + string arg: "
                  << t.elapsed_ms() << " ms\n";
    }

    // 测试 2：传 const char* 给 const string& 参数（需要临时构造）
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v1(c_data);  // 隐式构造临时 string
        }
        std::cout << "const string& + char* arg:  "
                  << t.elapsed_ms() << " ms\n";
    }

    // 测试 3：传 std::string 给 string_view 参数
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v2(str_data);
        }
        std::cout << "string_view   + string arg: "
                  << t.elapsed_ms() << " ms\n";
    }

    // 测试 4：传 const char* 给 string_view 参数
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

The results the author got:

```text
const string& + string arg:  12.3 ms
const string& + char* arg:   95.7 ms   ← 慢了 8 倍！
string_view   + string arg:  12.1 ms
string_view   + char* arg:   35.2 ms   ← 快了 3 倍
```

The key comparison is between the second and fourth rows. When the caller passes a `const char*`, the `const string&` version takes a huge jump to 95ms because it has to implicitly construct one million temporary `std::string` objects. The `string_view` version, although it also needs to perform a `strlen` on the `const char*`, does not require heap allocation, so it only takes 35ms. As for passing a `std::string`, the performance of both is basically tied—`const string&` passes a reference directly, and `string_view` constructs a 16-byte view; both are a matter of a few clock cycles, and the difference is within the noise margin.

This test tells us a very practical conclusion: if your function might be called with a mix of `const char*`, string literals, or `std::string`, using `string_view` as the parameter type is the better choice. If your function only receives `std::string`, there is little difference between the two.

## Reducing Temporary string Allocations

Besides explicit function calls, `string_view` can also help us reduce implicit temporary `std::string` allocations. A typical scenario is string comparison:

```cpp
// 旧写法：每次比较都可能构造临时 string
bool is_http_method(const std::string& method) {
    return method == "GET" || method == "POST" || method == "PUT"
        || method == "DELETE" || method == "PATCH";
}

// 新写法：零分配比较
bool is_http_method_sv(std::string_view method) {
    return method == "GET" || method == "POST" || method == "PUT"
        || method == "DELETE" || method == "PATCH";
}
```

The comparison operator between `string_view` and a string literal (`==`) constructs a lightweight `string_view` temporary object (16 bytes, no heap allocation), and then compares character by character. When `const std::string&` is compared with a string literal, the literal is implicitly converted to a temporary `std::string` (which may involve heap allocation; although some compilers optimize away this conversion, the standard does not guarantee it).

Another common source of "temporary strings" is function return values. Consider this pattern:

```cpp
// 返回 const char* 的 C API
const char* get_env_var(const char* name);

// 包装函数：旧版返回 string
std::string get_env_string(const char* name) {
    const char* val = get_env_var(name);
    return val ? std::string(val) : std::string("");
}

// 包装函数：新版返回 string_view
std::string_view get_env_view(const char* name) {
    const char* val = get_env_var(name);
    return val ? std::string_view(val) : std::string_view();
}
```

⚠️ The second version has a prerequisite: the pointer returned by `get_env_var` must be valid long-term. In the context of environment variables, this prerequisite usually holds true (environment variables do not disappear during the process's lifetime). But if the C API returns an internal static buffer (like `inet_ntoa`), the next call will overwrite it, and using `string_view` becomes risky. To emphasize again: before using `string_view`, you must confirm the lifetime of the underlying data.

## Avoiding Unnecessary string Construction

Sometimes we clearly only need to read string data, but we accidentally trigger the construction of a `std::string`. Let's look at a practical example—string hash table lookup:

```cpp
#include <unordered_map>
#include <string_view>

// 旧写法：查找时需要构造 string
std::unordered_map<std::string, int> old_map;
old_map["apple"] = 1;
old_map["banana"] = 2;

int lookup_old(const char* key) {
    auto it = old_map.find(key);  // 隐式构造临时 string
    return (it != old_map.end()) ? it->second : -1;
}

// 新写法：使用 transparent comparator，查找时零构造
// C++20 的 unordered_map 支持 heterogeneous lookup
// C++17 的 map/set 支持，unordered_map 要等 C++20
// 不过我们可以用 string_view 做键来演示类似的思路
std::unordered_map<std::string_view, int> sv_map;
// 注意：sv_map 的键指向的外部数据必须活得比 map 长

int lookup_sv(std::string_view key) {
    auto it = sv_map.find(key);
    return (it != sv_map.end()) ? it->second : -1;
}
```

Strictly speaking, C++17's `std::unordered_map` does not yet support heterogeneous lookup (this was added in C++20 via the `std::unordered_map::find(K)` overload), so the `const char*` in `old_map.find(key)` will still be implicitly constructed as a `std::string`. However, in C++20, you can enable the `is_transparent` feature for `unordered_map`, allowing the lookup to completely skip temporary construction. `string_view` is a crucial piece of the puzzle in this scenario.

## Embedded in Practice: Command Parsing and Protocol Handling

In embedded development, the "zero-allocation" characteristic of `string_view` is extremely valuable. An MCU's RAM is typically only a few dozen to a few hundred KB, and heap space is extremely limited. Frequent `std::string` allocations are not only slow but can also lead to memory fragmentation, ultimately crashing the system.

Let's look at a practical serial protocol parsing scenario. Suppose our embedded device receives JSON-RPC-style commands over a serial port, in the format `{"method":"xxx","params":"yyy"}`. We need to extract the `method` and `params` fields.

```cpp
#include <string_view>
#include <cstring>

// 模拟串口接收缓冲区
constexpr int kBufSize = 256;
static char uart_buf[kBufSize];
static int uart_len = 0;

/// @brief 在缓冲区中查找 JSON 字段的值
/// @param json JSON 字符串视图
/// @param key 要查找的键
/// @return 值的 string_view，找不到返回空 view
std::string_view find_json_field(std::string_view json,
                                  std::string_view key) {
    // 构造搜索模式："key":"
    // 这里用最简单的线性搜索，生产代码应该用真正的 JSON 解析器
    auto key_pattern = key;
    auto pos = json.find(key_pattern);
    if (pos == std::string_view::npos) {
        return {};
    }
    // 跳过 key 和 ":" 部分
    auto rest = json.substr(pos + key_pattern.size());
    // 跳过空白和冒号
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == ':'
           || rest.front() == '"')) {
        rest.remove_prefix(1);
    }
    // 找值的结束引号
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

This parser requires absolutely no heap allocation—all operations are completed among `string_view` objects on the stack. `uart_buf` is a static array, and `string_view` merely "takes a look" at it. On an STM32F103 with only 20KB of RAM, this zero-allocation string processing approach means you can use it with confidence, without worrying about running out of memory or fragmentation.

Of course, this JSON parser is toy-level—it doesn't handle complex cases like escaping, nesting, or arrays. But it demonstrates the core value of `string_view` in resource-constrained environments: providing string manipulation capabilities at the minimal cost. If you need a complete JSON parser, you can consider libraries like ArduinoJson, which also make extensive use of non-owning reference techniques similar to `string_view` internally.

## Summary

In this article, we used benchmark data to verify the performance advantages of `string_view`. The core conclusions are as follows: the `substr` operation is `string_view`'s biggest performance trump card, and the O(1) vs O(n) gap amplifies to over a hundred times with frequent calls. In the function parameter scenario, `string_view` has a clear advantage for `const char*` callers, but makes little difference for `std::string` callers. Reducing temporary `std::string` construction is another important benefit of `string_view`. In embedded scenarios, the zero-allocation characteristic of `string_view` makes it the preferred solution for string processing in resource-constrained environments.

However, performance isn't everything. In the next article, we will discuss the pitfalls of `string_view`—dangling references, null termination, implicit conversions, and other issues. If these problems are ignored, no amount of performance can make up for the cost of a crash.

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [C++ Stories: Performance of string_view vs string](https://www.cppstories.com/2018/07/string-view-perf/)
- [StackOverflow: How exactly is string_view faster than const string&?](https://stackoverflow.com/questions/40127965/how-exactly-is-stdstring-view-faster-than-const-stdstring)
- [cppreference: std::chrono](https://en.cppreference.com/w/cpp/chrono)
