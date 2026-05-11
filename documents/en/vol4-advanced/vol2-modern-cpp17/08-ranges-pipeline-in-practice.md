---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Ranges Pipeline in Practice
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'Chapter 8: 类型安全'
reading_time_minutes: 19
tags:
- cpp-modern
- host
- intermediate
title: Piping and Ranges in Practice
---
# Modern Embedded C++ Tutorial — Pipeline Operations and Ranges in Practice

## Introduction

In the previous chapter, we explored the concept of views, but if you only use them in isolation, you are not unlocking their full potential. The real magic happens when you chain views together—just like Unix pipes, where the output of one operation becomes the input of the next.

Honestly, the first time I wrote code using the pipe operator `|`, I felt like I was writing some high-level scripting language, not C++. The code reads like an English sentence, with a clarity that almost feels unusual. But what is even better is that behind this "script-like" syntax lies completely zero-overhead compile-time optimization.

> In a nutshell: **The pipe operator `|` lets you compose data processing operations like building blocks, offering both readability and efficiency. It is one of the most elegant features in C++20.**

In this chapter, we focus on practical application—how to use Ranges and pipelines to write elegant and efficient code in embedded projects.

------

## The Pipe Operator: The Unix Philosophy in C++

The Unix pipe philosophy is: **combine small programs to accomplish big tasks**. `ls | grep ".cpp" | wc -l`—each program does one thing, but chained together, they are incredibly powerful.

C++20 brings this philosophy into the language:

```cpp
// 传统写法：嵌套、内联、难以阅读
auto result = std::views::transform(
    std::views::filter(
        data,
        predicate1
    ),
    function2
);

// 管道写法：像句子一样自然
auto result = data
    | std::views::filter(predicate1)
    | std::views::transform(function2);

```

The pipe operator `|` is overloaded here. The left side is a range, and the right side is a view adaptor, which returns a new view. The key point is: **no data is copied during the entire process**. It simply constructs a "processing chain," and data only flows through this chain when you iterate over the result.

Let us start with a simple example and gradually build complex data processing pipelines.

------

## Basic Pipelines: Filter-Transform-Collect

The most common combination is the "filter → transform → collect" trio. Suppose we are processing a set of sensor readings:

```cpp
#include <ranges>
#include <vector>
#include <iostream>

struct SensorReading {
    int sensor_id;
    int raw_value;
    bool valid;
};

std::vector<SensorReading> get_readings() {
    return {
        {1, 120, true},
        {2, 45, false},   // 无效
        {3, 230, true},
        {4, 67, true},
        {5, 340, false},  // 超量程
        {6, 89, true}
    };
}

void process_sensors() {
    auto readings = get_readings();

    // 构建管道：过滤有效读数 → 提取raw_value → 转换为电压
    auto voltages = readings
        | std::views::filter([](const SensorReading& r) { return r.valid; })
        | std::views::transform([](const SensorReading& r) { return r.raw_value; })
        | std::views::transform([](int raw) { return raw * 3.3f / 4095; });

    std::cout << "Valid voltages:\n";
    for (float v : voltages) {
        std::cout << "  " << v << " V\n";
    }
}

```

```cpp

Valid voltages:
  0.0966133 V
  0.185425 V
  0.0540171 V
  0.0717957 V

```

The beauty of this code:

- The logic flows from top to bottom, like telling a story
- No temporary variables store intermediate results
- The compiler optimizes the entire pipeline into a single pass

------

## Practical Scenario 1: Multi-stage ADC Data Processing

In embedded systems, ADC (Analog-to-Digital Converter) data usually requires multiple processing stages. Let us design a complete ADC processing pipeline:

```cpp
#include <ranges>
#include <vector>
#include <array>
#include <cmath>

class ADCProcessor {
public:
    // 添加ADC原始读数
    void add_sample(uint16_t raw) {
        samples_.push_back(raw);
        keep_recent(100);  // 只保留最近100个样本
    }

    // 处理并返回结果
    std::vector<float> process() {
        // 构建完整处理管道
        auto pipeline = samples_
            | std::views::filter([](uint16_t v) {
                // 阶段1：过滤掉明显无效的值
                return v >= 100 && v <= 4000;
            })
            | std::views::transform([](uint16_t v) {
                // 阶段2：转换为电压
                return v * 3.3f / 4095.0f;
            })
            | std::views::transform([](float voltage) {
                // 阶段3：应用校准曲线（二阶多项式）
                return 1.001f * voltage + 0.0002f * voltage * voltage;
            });

        // 转换为vector返回
        return std::vector<float>(pipeline.begin(), pipeline.end());
    }

    // 获取滤波后的当前值
    std::optional<float> get_filtered_value() {
        if (samples_.empty()) return std::nullopt;

        // 计算移动平均
        auto pipeline = samples_
            | std::views::filter([](uint16_t v) {
                return v >= 100 && v <= 4000;
            })
            | std::views::transform([](uint16_t v) {
                return v * 3.3f / 4095.0f;
            });

        float sum = 0.0f;
        size_t count = 0;
        for (float v : pipeline) {
            sum += v;
            count++;
        }

        return count > 0 ? std::optional<float>(sum / count) : std::nullopt;
    }

private:
    std::vector<uint16_t> samples_;

    void keep_recent(size_t n) {
        if (samples_.size() > n) {
            samples_.erase(samples_.begin(), samples_.end() - n);
        }
    }
};

```

This example demonstrates several advantages of pipelines:

- Each processing stage has a single responsibility, making it easy to test
- Adding a new processing step only requires appending one more line to the pipeline
- You can comment out any step at any time for debugging

------

## Practical Scenario 2: Protocol Parsing and Data Extraction

In embedded communication, we often need to extract data from a byte stream. Ranges make this kind of work exceptionally simple:

```cpp
#include <ranges>
#include <vector>
#include <cstdint>
#include <iostream>

// 假设我们接收到了一串16位数据（大端序）
std::vector<uint8_t> receive_spi_data() {
    return {0x01, 0x00, 0x00, 0x64, 0x00, 0x02, 0xFF, 0xFF};
    // 解析为：0x0100, 0x0064, 0x0002, 0xFFFF
}

void parse_spi_packet() {
    auto data = receive_spi_data();

    // 步骤1：按2字节分组
    auto chunks = data | std::views::chunk(2);

    // 步骤2：将每组合并为16位值
    auto words = chunks | std::views::transform([](auto chunk) {
        uint16_t high = chunk[0];
        uint16_t low = chunk[1];
        return (high << 8) | low;
    });

    // 步骤3：过滤掉填充值（假设0xFFFF是填充）
    auto valid_words = words | std::views::filter([](uint16_t w) {
        return w != 0xFFFF;
    });

    // 输出结果
    for (uint16_t w : valid_words) {
        std::cout << "Word: 0x" << std::hex << w << std::dec << '\n';
    }
}

```

```cpp

Word: 0x100
Word: 0x64
Word: 0x2

```

`chunk` is a very practical view adaptor that groups N elements together, making it perfect for handling protocol data.

------

## Practical Scenario 3: Event Queue Processing

In event-driven embedded systems, we often need to handle various types of events. We can use Ranges to elegantly implement event classification and processing:

```cpp
#include <ranges>
#include <vector>
#include <variant>
#include <iostream>

enum class EventType { Timer, GPIO, UART, ADC };

struct Event {
    EventType type;
    uint32_t timestamp;
    std::variant<int, bool, char> data;  // 简化版事件数据
};

class EventManager {
public:
    void add_event(Event e) {
        events_.push_back(e);
    }

    // 处理所有GPIO事件
    void process_gpio_events() {
        auto gpio_events = events_
            | std::views::filter([](const Event& e) {
                return e.type == EventType::GPIO;
            });

        for (const auto& e : gpio_events) {
            handle_gpio(e);
        }

        // 处理完后移除
        std::erase_if(events_, [](const Event& e) {
            return e.type == EventType::GPIO;
        });
    }

    // 获取最近N个事件的时间戳
    std::vector<uint32_t> get_recent_timestamps(size_t n) {
        auto recent = events_
            | std::views::reverse  // 从新到旧
            | std::views::take(n)
            | std::views::transform([](const Event& e) {
                return e.timestamp;
            });

        return std::vector<uint32_t>(recent.begin(), recent.end());
    }

private:
    std::vector<Event> events_;

    void handle_gpio(const Event& e) {
        std::cout << "GPIO event at " << e.timestamp << '\n';
    }
};

```

------

## Custom View Adaptors: Making Your Types Pipe-Friendly

Sometimes you want your own types to participate in pipeline operations. C++20 allows you to define custom view adaptors (Range Adaptor Objects), but this involves some template metaprogramming.

The good news is that for most embedded scenarios, you can use a simpler approach: make your custom range support iteration, and it can plug directly into pipelines:

```cpp
#include <ranges>
#include <iterator>

// 简单的环形缓冲区
template<typename T, size_t N>
class RingBuffer {
public:
    void push(T value) {
        data_[head_] = value;
        head_ = (head_ + 1) % N;
        if (size_ < N) size_++;
    }

    // 让它成为Range：提供begin/end
    auto begin() { return Iterator(this, 0); }
    auto end() { return Iterator(this, size_); }

private:
    std::array<T, N> data_;
    size_t head_ = 0;
    size_t size_ = 0;

    // 简单的迭代器实现
    struct Iterator {
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = ptrdiff_t;

        RingBuffer* buf;
        size_t idx;

        Iterator(RingBuffer* b, size_t i) : buf(b), idx(i) {}

        T& operator*() {
            size_t pos = (buf->head_ - buf->size_ + idx) % N;
            return buf->data_[pos];
        }

        Iterator& operator++() {
            ++idx;
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return idx != other.idx;
        }
    };
};

// 使用：RingBuffer可以直接接入管道
void demo_ring_buffer_pipeline() {
    RingBuffer<int, 10> buffer;

    for (int i = 0; i < 8; ++i) {
        buffer.push(i);
    }

    // 直接用管道处理环形缓冲区
    auto result = buffer
        | std::views::filter([](int x) { return x % 2 == 0; })
        | std::views::transform([](int x) { return x * 2; });

    for (int x : result) {
        std::cout << x << ' ';  // 输出：0 4 8 12
    }
}

```

------

## Common Composition Patterns

Based on real-world project experience, I have summarized several particularly useful pipeline composition patterns:

### Pattern 1: Data Cleaning Pipeline

```cpp
auto clean_data = raw_data
    | std::views::filter(is_valid)      // 去除无效值
    | std::views::transform(clamp)       // 限制范围
    | std::views::transform(calibrate);  // 校准

```

### Pattern 2: Sliding Window

```cpp
auto windowed = data
    | std::views::slide(window_size)     // 滑动窗口（C++23）
    | std::views::transform(compute_avg);

```

For C++20, we can achieve a sliding window effect like this:

```cpp
template<std::ranges::input_range R>
auto sliding_window(R&& r, size_t n) {
    return std::views::iota(size_t{0}, std::ranges::size(r) - n + 1)
        | std::views::transform([r, n](size_t i) {
            return r | std::views::drop(i) | std::views::take(n);
        });
}

```

### Pattern 3: Zip Operation (Iterating Two Sequences Simultaneously)

```cpp
std::vector<float> values = {1.1f, 2.2f, 3.3f};
std::vector<int> ids = {10, 20, 30};

// 同时遍历两个序列（需要自定义zip视图或等C++23）
// C++23: auto zipped = std::views::zip(values, ids);

```

In the C++20 era, we can use `zip` (provided by certain libraries) or implement a simple zip ourselves:

```cpp
template<typename R1, typename R2>
auto zip_simple(R1&& r1, R2&& r2) {
    return std::views::iota(size_t{0}, std::min(std::ranges::size(r1), std::ranges::size(r2)))
        | std::views::transform([&r1, &r2](size_t i) {
            return std::pair{r1[i], r2[i]};
        });
}

```

------

## Performance Verification: Is It Really Zero-Overhead?

Let us verify the performance of Ranges pipelines. I wrote a test snippet:

```cpp
#include <ranges>
#include <vector>
#include <algorithm>
#include <chrono>

// 传统写法
std::vector<int> traditional(const std::vector<int>& input) {
    std::vector<int> temp1;
    std::copy_if(input.begin(), input.end(), std::back_inserter(temp1),
                 [](int x) { return x > 50; });

    std::vector<int> temp2;
    std::transform(temp1.begin(), temp1.end(), std::back_inserter(temp2),
                   [](int x) { return x * 2; });

    return temp2;
}

// Ranges管道写法
std::vector<int> with_ranges(const std::vector<int>& input) {
    auto pipeline = input
        | std::views::filter([](int x) { return x > 50; })
        | std::views::transform([](int x) { return x * 2; });

    return std::vector<int>(pipeline.begin(), pipeline.end());
}

// 性能测试
void benchmark() {
    std::vector<int> data(1000000);
    for (int i = 0; i < 1000000; ++i) data[i] = i;

    auto t1 = std::chrono::high_resolution_clock::now();
    auto r1 = traditional(data);
    auto t2 = std::chrono::high_resolution_clock::now();

    auto t3 = std::chrono::high_resolution_clock::now();
    auto r2 = with_ranges(data);
    auto t4 = std::chrono::high_resolution_clock::now();

    auto time1 = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1);
    auto time2 = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3);

    // 在-O2优化下，两者性能接近，ranges甚至可能更快
    // 因为编译器能更好地优化整个管道
}

```

At `-O2` or higher optimization levels, modern compilers will fully inline the lambdas in the pipeline and eliminate unnecessary intermediate steps. The resulting assembly code is highly efficient, and might even be faster than a hand-written loop—because the compiler can see the complete processing logic and perform better vectorization optimizations.

------

## Pitfall Guide

### Pitfall 1: Do Not Iterate the Same Pipeline Multiple Times

Certain view adaptors produce "consuming" views, and iterating them multiple times may yield different results:

```cpp
auto data = std::views::iota(0, 5);

// 如果内部有状态（比如生成随机数）
// 多次迭代结果可能不同

// 解决方案：如果需要多次使用，转成容器
auto vec = std::vector<int>(data.begin(), data.end());

```

### Pitfall 2: Watch Out for Reference Lifetimes

```cpp
// ❌ 危险
auto get_pipeline() {
    std::vector<int> local = {1, 2, 3};
    return local | std::views::filter([](int x) { return x > 1; });
    // local被销毁，返回的管道悬垂
}

// ✅ 正确：传数据进来
template<std::ranges::input_range R>
auto make_pipeline(R&& r) {
    return r | std::views::filter([](int x) { return x > 1; });
}

```

### Pitfall 3: Compiler Error Messages Can Be Verbose

Ranges involve a lot of templates, and compiler error messages can span dozens of lines. When you run into issues:

- First, check if the lambda's return type matches
- Confirm that the range's `value_type` is as expected
- Use `std::ranges::range_reference_t` to check reference types

### Pitfall 4: Incomplete Support in Certain Compilers

If you encounter strange compilation errors, first verify your compiler version:

- GCC 11+
- Clang 13+
- MSVC 2019 v16.10+

------

## Compiler Support and Alternatives

If your compiler does not fully support C++20 Ranges, or if you want some extra features, consider the following:

1. **range-v3 library**: This is the reference implementation of Ranges, written by Eric Niebler. C++20 Ranges is based on it. It can be used with C++14/17.

```cpp
#include <range/v3/all.hpp>

using namespace ranges;  // 提供类似C++20的接口

```

1. **nano-range**: A lightweight Ranges implementation, suitable for embedded systems.

But honestly, in 2024, mainstream embedded compilers (GCC 11+, Clang 13+) have fairly solid support for C++20 Ranges. If your project can upgrade its compiler, we strongly recommend using the standard library implementation directly.

------

## Summary

The combination of the pipe operator `|` and the Ranges library is one of the most elegant features in modern C++:

- **Readability**: The data processing flow is clear at a glance
- **Composability**: Compose operations like building blocks
- **Zero-overhead**: After compiler optimization, the efficiency is on par with traditional code
- **Type safety**: All type matching is checked at compile time

For embedded developers, Ranges finally allow us to write data processing code that is both elegant and efficient—without having to choose between "readability" and "performance." This toolset is particularly well-suited for common embedded scenarios like sensor data processing, protocol parsing, and event handling.

Once you get used to thinking in pipelines, you will find that many data processing tasks that used to feel cumbersome can now be accomplished in just a few lines of code. This is exactly what a good language feature should achieve—making the code look more like your thought process, rather than forcing you to adapt to the language's limitations.

In the next chapter, we will continue exploring the application of functional programming in C++, looking at how to build more robust error handling mechanisms using tools like `std::expected`.
