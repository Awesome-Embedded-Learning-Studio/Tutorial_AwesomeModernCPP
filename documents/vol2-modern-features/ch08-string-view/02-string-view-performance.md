---
title: "string_view 性能分析"
description: "基准测试 string_view 替代 const string& 的性能收益"
chapter: 8
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 8: string_view 内部原理"
related:
  - "string_view 陷阱与最佳实践"
---

# string_view 性能分析

上一篇我们深入了 `string_view` 的内部原理，知道了它是"指针 + 长度"的非拥有视图。这一篇我们用数据说话——`string_view` 到底比 `const std::string&` 快多少？在什么场景下收益最大？有没有反而更慢的情况？

笔者为了写这篇文章，着实跑了不少 benchmark。说实话，有些结果跟笔者的直觉是一致的（substr 确实快很多），有些则出乎意料（在某些 ABI 下，按值传 `string_view` 并不总是比 `const string&` 快）。我们一个一个来看。

> **学习目标**
>
> - 完成本章后，你将能够：
> - [ ] 理解 `string_view` 在 substr、传参等场景下的性能差异
> - [ ] 掌握用 `<chrono>` 编写微基准测试的方法
> - [ ] 了解 `string_view` 在嵌入式命令解析中的实战应用

## 环境说明

我们今天所有基准测试的环境如下：Linux 6.x（x86_64），GCC 13.2，编译选项 `-std=c++17 -O2 -march=native`。测试机器是一台普通的 x86 开发板。所有时间测量使用 `std::chrono::high_resolution_clock`，每个测试用例循环执行足够多次以减少误差。

## substr：O(1) vs O(n) 的天壤之别

`string_view` 性能优势最直观的体现就是 `substr` 操作。上一篇我们已经从原理上分析过了：`string_view::substr` 只是指针偏移和长度截断，`std::string::substr` 需要堆分配加字符拷贝。现在我们用数据来验证。

先写一个简单的基准测试框架：

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

然后分别测试 `std::string::substr` 和 `string_view::substr` 的性能。测试方法是：给定一个 10000 字符的长字符串，对其执行 100000 次 substr 操作，每次取一个 50 字符的子串，起始位置随机。

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

笔者跑出来的结果：

```text
std::string::substr:   38.7 ms (sink=5000000)
string_view::substr:    0.4 ms (sink=5000000)
```

将近 100 倍的差距。原因很简单：`std::string::substr` 做了 100000 次堆分配和字符拷贝（每次 50 字节），而 `string_view::substr` 只做了 100000 次指针加法和长度调整。这个差距在字符串更长、调用更频繁的场景下会更加明显。

当然，这个测试是刻意构造的极端场景。在实际项目中，如果你只偶尔做一次 substr，这点差距你可能根本感知不到。但如果你在写一个解析器，需要频繁地对输入字符串做分割、提取、跳过等操作，那 `string_view` 的优势就会非常突出。

## 函数参数：string_view vs const string&

这是大家最关心的场景：把函数参数从 `const std::string&` 改成 `std::string_view`，到底能快多少？

我们从原理上先分析。当函数签名是 `const std::string&` 时，如果调用者传入的是一个 `const char*`（比如字符串字面量或 C API 返回的字符串），编译器需要先隐式构造一个临时的 `std::string`，然后传引用进去。这个临时构造涉及 `strlen` 计算长度加上可能的堆分配。函数返回后，临时对象析构，堆内存释放。

而当函数签名是 `std::string_view` 时，无论传入的是 `std::string`、`const char*` 还是字符串字面量，都只是构造一个 16 字节的 view 对象。从 `const char*` 构造时，还需要一次 `strlen`，但不需要分配堆内存。从 `std::string` 构造时，连 `strlen` 都不需要，直接取 `data()` 和 `size()`。

我们写一个基准测试来验证。测试场景：一个函数接收字符串参数并做简单的处理（计算字符出现次数），分别用两种签名，然后分别传入 `std::string` 和 `const char*` 调用。

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

笔者跑出来的结果：

```text
const string& + string arg:  12.3 ms
const string& + char* arg:   95.7 ms   ← 慢了 8 倍！
string_view   + string arg:  12.1 ms
string_view   + char* arg:   35.2 ms   ← 快了 3 倍
```

关键数据在第二行和第四行的对比上。当调用者传入 `const char*` 时，`const string&` 版本因为要隐式构造 100 万个临时 `std::string`，耗时暴增到 95ms。而 `string_view` 版本虽然也需要对 `const char*` 做一次 `strlen`，但不需要堆分配，所以只用了 35ms。至于传入 `std::string` 的情况，两者的性能基本持平——`const string&` 是直接传引用，`string_view` 是构造一个 16 字节的 view，都是几个时钟周期的事，差异在噪声范围内。

这个测试告诉我们一个很实际的结论：如果你的函数可能被 `const char*`、字符串字面量或 `std::string` 混合调用，用 `string_view` 做参数类型是更优的选择。如果你的函数只接收 `std::string`，两者差别不大。

## 减少临时 string 分配

除了显式的函数调用，`string_view` 还能帮助我们减少隐式的临时 `std::string` 分配。一个典型的场景是字符串比较：

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

`string_view` 与字符串字面量的比较运算符（`==`）不需要构造任何临时对象——它直接逐字符比较。而 `const std::string&` 与字符串字面量比较时，字面量会被隐式转换为临时 `std::string`（虽然有些编译器会优化掉这个转换，但标准并不保证）。

另外一个常见的"临时 string"来源是函数返回值。考虑这个模式：

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

⚠️ 第二个版本有一个前提条件：`get_env_var` 返回的指针必须是长期有效的。在环境变量的场景下，这个前提通常是成立的（环境变量在进程生命周期内不会消失）。但如果 C API 返回的是一个内部静态缓冲区（比如 `inet_ntoa`），下次调用就会覆盖，那用 `string_view` 就有风险了。再次强调：使用 `string_view` 之前，必须确认底层数据的生命周期。

## 避免不必要的 string 构造

有时候我们明明只需要读取字符串数据，却不小心触发了 `std::string` 的构造。看一个实际例子——字符串哈希表查找：

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

严格来说，C++17 的 `std::unordered_map` 还不支持 heterogeneous lookup（这是 C++20 加的 `std::unordered_map::find(K)` 重载），所以 `old_map.find(key)` 中 `const char*` 仍然会被隐式构造为 `std::string`。但在 C++20 中，你可以为 `unordered_map` 启用 `is_transparent` 特性，让查找完全跳过临时构造。`string_view` 在这个场景下是关键的一环。

## 嵌入式实战：命令解析与协议处理

在嵌入式开发中，`string_view` 的"零分配"特性非常有价值。MCU 的 RAM 通常只有几十 KB 到几百 KB，堆空间极其有限，频繁的 `std::string` 分配不仅慢，还可能导致内存碎片化，最终让系统崩溃。

我们来看一个实际的串口协议解析场景。假设我们的嵌入式设备通过串口接收 JSON-RPC 风格的命令，格式为 `{"method":"xxx","params":"yyy"}`。我们需要提取 method 和 params 字段。

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

这个解析器完全不需要堆分配——所有操作都在栈上的 `string_view` 对象之间完成。`uart_buf` 是一个静态数组，`string_view` 只是"看"了它一眼。在 RAM 只有 20KB 的 STM32F103 上，这种零分配的字符串处理方式意味着你可以放心使用，不用担心内存不够或者碎片化。

当然，这个 JSON 解析器是玩具级的——它不处理转义、嵌套、数组等复杂情况。但它展示了 `string_view` 在资源受限环境下的核心价值：用最小的代价提供字符串操作能力。如果你需要一个完整的 JSON 解析器，可以考虑 ArduinoJson 等库，它们内部也大量使用了类似 `string_view` 的非拥有引用技术。

## 小结

这一篇我们用基准测试数据验证了 `string_view` 的性能优势。核心结论如下：`substr` 操作是 `string_view` 最大的性能杀手锏，O(1) vs O(n) 的差距在频繁调用时会放大到上百倍。在函数参数场景下，`string_view` 对 `const char*` 调用者有明显优势，但对 `std::string` 调用者差异不大。减少临时 `std::string` 构造是 `string_view` 的另一个重要收益。在嵌入式场景中，`string_view` 的零分配特性使它成为资源受限环境下字符串处理的首选方案。

不过，性能不是一切。下一篇我们要讨论 `string_view` 的陷阱——悬垂引用、null 终止、隐式转换等问题，这些问题如果忽视了，再高的性能也弥补不了崩溃的代价。

## 参考资源

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [C++ Stories: Performance of string_view vs string](https://www.cppstories.com/2018/07/string-view-perf/)
- [StackOverflow: How exactly is string_view faster than const string&?](https://stackoverflow.com/questions/40127965/how-exactly-is-stdstring-view-faster-than-const-stdstring)
- [cppreference: std::chrono](https://en.cppreference.com/w/cpp/chrono)
