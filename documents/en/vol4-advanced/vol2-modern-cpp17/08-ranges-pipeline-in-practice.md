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
reading_time_minutes: 12
tags:
- cpp-modern
- host
- intermediate
title: Pipes and Ranges in Practice
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/08-ranges-pipeline-in-practice.md
  source_hash: 6a01545ffd4070e56e1741366b3e70fd01468089844173ecc6adf889bf61e3a8
  translated_at: '2026-06-16T06:18:00.304140+00:00'
  engine: anthropic
  token_count: 3136
---
# Modern Embedded C++ Tutorial — Pipeline Operations and Ranges in Practice

## Introduction

In the previous chapter, we explored the concept of **views**, but if we only use them in isolation, we haven't fully unleashed their power. The real magic happens when we chain views together—much like Unix pipelines, where the output of one operation immediately becomes the input for the next.

Honestly, the first time I wrote code using the pipe operator `|`, I felt like I was writing some high-level scripting language rather than C++. The code reads like an English sentence, and the logic is so clear it almost feels unfamiliar. But what is even better is that behind this "script-like" syntax lies fully zero-overhead compile-time optimization.

> TL;DR: **The pipe operator `|` allows you to compose data processing operations like building blocks. It is both readable and efficient, making it one of C++20's most elegant features.**

In this chapter, we focus on practice—how to use Ranges and pipelines in embedded projects to write code that is both elegant and efficient.

------

## The Pipe Operator: The Unix Philosophy in C++

The philosophy of Unix pipelines is: **combine small programs to accomplish large tasks**. `cat data | grep pattern | sort | head -n 10`—each program does one thing, but chained together, their power is limitless.

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

The pipe operator `|` is overloaded here. The left side is a range, and the right side is a view adaptor, returning a new view. The key point is: **no data copying occurs throughout the entire process**. Instead, a "processing chain" is constructed, and data only flows through this chain when you iterate over the result.

Let's start with a simple example and gradually build a complex data processing pipeline.

------

## Basic Pipeline: Filter-Transform-Collect

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

- The logic flows from top to bottom, like telling a story.
- There are no temporary variables to store intermediate results.
- The compiler optimizes the entire pipeline into a single pass.

------

## Real-World Scenario 1: Multi-Stage ADC Data Processing

In embedded systems, ADC data usually requires multiple processing stages. Let's design a complete ADC processing pipeline:

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

- Each processing stage has a single responsibility, making it easy to test.
- Adding a new processing step only requires adding one line to the pipeline.
- We can comment out any step at any time to facilitate debugging.

------

## Practical Scenario 2: Protocol Parsing and Data Extraction

In embedded communication, we often need to extract data from a byte stream. Ranges make this task exceptionally simple:

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

`std::views::chunk` is a highly practical view adapter that groups elements into sets of N, making it ideal for handling protocol data.

------

## Practical Scenario 3: Event Queue Processing

In event-driven embedded systems, we frequently need to handle various types of events. We can use Ranges to elegantly implement event classification and handling:

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

## Custom View Adapters: Making Your Types Pipe-Friendly

Sometimes, we want our custom types to participate in pipe operations. C++20 allows us to define custom view adapters (Range Adaptor Objects), but this involves some template metaprogramming.

The good news is that for most embedded scenarios, we can use a simpler approach: make the custom range support iteration, and then we can directly plug it into the pipeline:

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

## Common Composition Patterns

Based on practical project experience, I have summarized several particularly useful pipeline composition patterns:

### Pattern 1: Data Cleaning Pipeline

```cpp
auto clean_data = raw_data
    | std::views::filter(is_valid)      // 去除无效值
    | std::views::transform(clamp)       // 限制范围
    | std::views::transform(calibrate);  // 校准

```

### Mode 2: Sliding Window

```cpp
auto windowed = data
    | std::views::slide(window_size)     // 滑动窗口（C++23）
    | std::views::transform(compute_avg);

```

Here is how we can implement a sliding window effect in C++20:

```cpp
template<std::ranges::input_range R>
auto sliding_window(R&& r, size_t n) {
    return std::views::iota(size_t{0}, std::ranges::size(r) - n + 1)
        | std::views::transform([r, n](size_t i) {
            return r | std::views::drop(i) | std::views::take(n);
        });
}

```

### Mode 3: Zip Operation (Iterating Over Two Sequences Simultaneously)

```cpp
std::vector<float> values = {1.1f, 2.2f, 3.3f};
std::vector<int> ids = {10, 20, 30};

// 同时遍历两个序列（需要自定义zip视图或等C++23）
// C++23: auto zipped = std::views::zip(values, ids);

```

In the C++20 era, we can use `std::views::zip` (provided by some libraries) or implement a simple zip ourselves:

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

## Performance Verification: Is it Really Zero Overhead?

Let's verify the performance of the Ranges pipeline. I wrote a test snippet:

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

At `-O2` or higher optimization levels, modern compilers will completely inline the lambda expressions within the pipeline and eliminate unnecessary intermediate steps. The resulting assembly code is highly efficient, potentially even faster than a hand-written loop—because the compiler sees the complete processing logic, it can perform better vectorization optimizations.

------

## Common Pitfalls

### Pitfall 1: Do not iterate over the same pipeline multiple times

Some view adapters generate "consuming" views, where multiple iterations may yield different results:

```cpp
auto data = std::views::iota(0, 5);

// 如果内部有状态（比如生成随机数）
// 多次迭代结果可能不同

// 解决方案：如果需要多次使用，转成容器
auto vec = std::vector<int>(data.begin(), data.end());

```

### Pitfall 2: Watch out for object lifetimes

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

### Pitfall 3: Compiler error messages can be verbose

Ranges involve a large number of templates, so compiler error messages can span dozens of lines. When you encounter issues:

- First, check if the lambda's return type matches.
- Confirm that the Range's `value_type` meets expectations.
- Use `std::ranges::range_reference_t<R>` to inspect the reference type.

### Pitfall 4: Incomplete compiler support

If you encounter strange compilation errors, first verify your compiler version:

- GCC 11+
- Clang 13+
- MSVC 2019 v16.10+

------

## Compiler Support and Alternatives

If your compiler does not fully support C++20 Ranges, or if you want some additional features, consider:

1. **range-v3 library**: This is the reference implementation of Ranges, written by Eric Niebler; C++20 Ranges is based on it. It can be used with C++14/17.

```cpp
#include <range/v3/all.hpp>

using namespace ranges;  // 提供类似C++20的接口

```

1. **nano-range**: A lightweight Ranges implementation suitable for embedded systems.

However, to be honest, in 2024, mainstream embedded compilers (GCC 11+, Clang 13+) have fairly good support for C++20 Ranges. If your project can upgrade the compiler, we strongly recommend using the standard library implementation directly.

------

## Summary

The combination of the pipe operator `|` and the Ranges library is one of the most elegant features in modern C++:

- **Readability**: Data processing flows are clear at a glance.
- **Composability**: We can combine operations like building blocks.
- **Zero overhead**: After compiler optimization, efficiency is on par with traditional code.
- **Type safety**: The compiler checks all type matching at compile time.

For embedded developers, Ranges finally allows us to write data processing code that is both elegant and efficient—no need to choose between "readability" and "performance." This toolset is particularly suitable for common embedded scenarios like sensor data processing, protocol parsing, and event handling.

Once we get used to thinking in terms of pipelines, we will find that many data processing tasks that used to seem troublesome can now be handled in just a few lines of code. This is the effect that good language features should achieve—making code resemble our thought process, rather than forcing us to adapt to the language's limitations.

In the next chapter, we will continue to explore the application of functional programming in C++ and see how to use tools like `std::expected` to build more robust error handling mechanisms.
