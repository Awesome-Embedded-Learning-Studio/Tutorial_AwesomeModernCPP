---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Ranges Library Basics
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Chapter 8: 类型安全'
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
title: C++20 Range Library Basics and Views
translation:
  engine: anthropic
  source: documents/vol4-advanced/vol2-modern-cpp17/07-ranges-basics-and-views.md
  source_hash: f8bc594ee9cbcf6b907f60feb2f458d2d3412646523f21596a4f50246d4a1cbb
  token_count: 2581
  translated_at: '2026-05-26T11:40:13.208481+00:00'
---
# Modern Embedded C++ Tutorial — C++20 Ranges Library Basics and Views

## Introduction

Every time we process arrays or container data, I always feel like something is missing. If we use STL algorithms, those `std::transform` and `std::copy_if` calls are an absolute pain to write — iterator begin, iterator end, temporary container, and finally pasting it back. After all that, the code logic gets fragmented, and it reads like chewing on dry toast.

Then C++20 brought the Ranges library, like installing a "data processing pipeline" in your code. More importantly, it introduced the concept of a "view" — **lazy evaluation, zero-overhead copying** — which is practically tailor-made for embedded development.

> To sum it up in one sentence: **Ranges let us compose operations like Unix pipes, and views let us process data without extra copies — both elegant and efficient.**

Our goal right now is to understand two things: what a Range is, what a View is, and why they are so useful in embedded scenarios.

------

## Starting from the Pain Point: How Annoying Traditional STL Algorithms Are

Let's look at how we used to process data. Suppose we read a set of data from a sensor, need to filter out anomalous values, and then multiply the rest by a coefficient:

```cpp
#include <vector>
#include <algorithm>

void process_sensor_readings() {
    // 原始数据
    std::vector<int> readings = {120, 45, 230, 67, 340, 89, 56, 180};

    // 第一步：过滤掉小于50或大于300的异常值
    std::vector<int> filtered;
    std::copy_if(readings.begin(), readings.end(),
                 std::back_inserter(filtered),
                 [](int v) { return v >= 50 && v <= 300; });

    // 第二步：对过滤后的数据进行校准（乘以系数）
    std::vector<int> calibrated;
    std::transform(filtered.begin(), filtered.end(),
                   std::back_inserter(calibrated),
                   [](int v) { return v * 2; });

    // calibrated 现在是 {240, 90, 460, 134, 178, 112, 360}
}

```

Look at how annoying this code is:

- Every operation requires writing the iterator range twice
- We need to create a temporary container `filtered` to store intermediate results
- The logic is interrupted by intermediate variables, making it impossible to see the "raw data → filter → calibrate" pipeline at a glance
- Memory allocation happens at least twice (`filtered` and `calibrated`)

In embedded scenarios, this kind of temporary memory allocation is especially headache-inducing — are we sure the heap has enough space? Are we sure it won't fragment? Are we sure real-time performance won't be affected by allocation?

The answers to all these questions lie in the Ranges library.

------

## What is a Range: Simply Put, It's "A Pair of Iterators"

The definition of "Range" in the C++20 standard library is simple: **anything that can provide an iterator**.

```cpp
std::vector<int> vec = {1, 2, 3, 4, 5};
std::array<int, 4> arr = {10, 20, 30, 40};
int native_arr[] = {100, 200, 300};

```

These are all Ranges. Previously, we had to use `.begin()` and `.end()` when writing algorithms, but now we can throw the entire container directly into the algorithm:

```cpp
#include <ranges>
#include <algorithm>

// C++20之前的写法
std::sort(vec.begin(), vec.end());

// C++20的写法
std::sort(vec);  // 直接传整个容器

```

But this is just surface-level syntactic sugar; the real power lies in a whole new set of tools in the `<ranges>` header file.

First, we need to distinguish between two concepts: **Range** and **View**.

- **Range**: Anything that can be iterated over, including `std::vector`, `std::array`, and native arrays
- **View**: A special kind of Range that does not own data, but merely provides "a certain perspective on existing data," and performs **lazy evaluation**

The concept of a view is so important that we will dedicate an entire section to it.

------

## Views: Zero-Overhead Data Lenses

The essence of a view can be summarized in four words: **lazy, non-owning, composable, O(1) copy**.

### Lazy Views

Views are "lazy" — nothing is computed when you define them; computation only happens when you actually iterate over them:

```cpp
#include <ranges>
#include <vector>
#include <iostream>

void demo_lazy_view() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    // 创建一个过滤视图：只保留大于2的元素
    auto filtered = std::views::filter(data, [](int x) { return x > 2; });

    // 到这里为止，什么都没发生！没有新容器被创建

    // 只有当你迭代的时候，过滤逻辑才会执行
    for (int x : filtered) {
        std::cout << x << ' ';  // 输出：3 4 5
    }
}

```

### Non-owning Data

Views merely "look at" the underlying data without owning it:

```cpp
void demo_view_ownership() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    auto view = std::views::filter(data, [](int x) { return x > 2; });

    // 修改底层数据
    data[0] = 100;

    // 视图反映的是底层数据的变化
    for (int x : view) {
        std::cout << x << ' ';  // 输出：3 4 5（100被过滤掉了）
    }
}

```

### O(1) Copy

The copy cost of a view is constant-level — it only stores a few pointers/iterators and does not copy the underlying data:

```cpp
void demo_view_copy() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    auto view1 = std::views::filter(data, [](int x) { return x > 2; });
    auto view2 = view1;  // 拷贝视图：O(1)，不复制任何元素！

    // view1和view2都指向同一个底层数据
}

```

This is extremely important for embedded development — we can pass views around everywhere without worrying about the overhead of data copying.

------

## Common View Factory Functions

The `<views>` header provides a series of "view factory" functions for creating various views. Let's cover the ones most commonly used in embedded development.

### filter: Filtering Data

`std::views::filter` creates a view containing only elements that satisfy a condition:

```cpp
#include <ranges>
#include <vector>

void filter_example() {
    std::vector<int> readings = {120, 45, 230, 67, 340, 89, 56, 180};

    // 创建过滤视图：只保留50到300之间的读数
    auto valid_readings = std::views::filter(
        readings,
        [](int v) { return v >= 50 && v <= 300; }
    );

    // 迭代视图
    for (int v : valid_readings) {
        // v会是：120, 230, 67, 89, 56, 180（45和340被过滤）
        process_reading(v);
    }

    // 原始readings没有被修改，也没有创建新vector
}

```

### transform: Transforming Each Element

`std::views::transform` applies a function to each element:

```cpp
void transform_example() {
    std::vector<int> raw_values = {100, 150, 200, 250};

    // 创建转换视图：将ADC原始值转换为电压
    auto voltages = std::views::transform(
        raw_values,
        [](int adc) { return adc * 3.3f / 4095; }  // 12位ADC，3.3V参考
    );

    for (float v : voltages) {
        // v会是转换后的电压值
    }
}

```

### take and drop: Taking the First N or Skipping the First N

```cpp
void take_drop_example() {
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // 取前5个元素
    auto first_five = std::views::take(data, 5);  // {1, 2, 3, 4, 5}

    // 跳过前3个元素，然后取剩下的
    auto after_skip = std::views::drop(data, 3);  // {4, 5, 6, 7, 8, 9, 10}

    // 组合使用：跳过前2个，再取4个
    auto middle = std::views::take(std::views::drop(data, 2), 4);  // {3, 4, 5, 6}
}

```

In embedded scenarios, this is particularly useful when dealing with protocol headers:

```cpp
void parse_packet(std::span<const uint8_t> packet) {
    // 跳过2字节头部，取接下来的数据部分
    auto payload = std::views::drop(packet, 2);

    // 再取最后4字节作为CRC（假设CRC在末尾）
    size_t payload_size = packet.size() - 2 - 4;
    auto data = std::views::take(payload, payload_size);

    // 处理data...
}

```

### split: Splitting by a Delimiter

`std::views::split` splits a Range into multiple sub-Ranges based on a delimiter:

```cpp
#include <ranges>
#include <string>
#include <iostream>

void split_example() {
    std::string data = "sensor1=25,sensor2=30,sensor3=28";

    // 按逗号切分
    auto fields = std::views::split(data, ',');

    for (auto field : fields) {
        // field是一个子Range，不是string
        // 可以把它转成string_view使用
        std::string_view field_sv(field.begin(), field.end());
        // field_sv依次是："sensor1=25", "sensor2=30", "sensor3=28"
    }
}

```

It's especially handy when parsing NMEA sentences (GPS data format):

```cpp
void parse_nmea(std::string_view line) {
    // NMEA格式：$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
    auto parts = std::views::split(line, ',');

    // parts[0]是"$GPGGA"，parts[1]是时间"123519"，以此类推
}

```

### iota: Generating Sequences

`std::views::iota` generates an incrementing sequence:

```cpp
void iota_example() {
    // 生成0到9的序列
    auto numbers = std::views::iota(0, 10);  // [0, 10)

    for (int n : numbers) {
        // 0, 1, 2, ..., 9
    }

    // 生成ADC通道编号序列
    auto adc_channels = std::views::iota(0, 16);  // 通道0-15
    for (int ch : adc_channels) {
        adc_read(ch);
    }
}

```

------

## Composing Views: Starting to Build Pipelines

A single view has limited power, but composing them together makes them formidable. We can use the pipe operator `|` to chain views together (we will cover this in detail in the next chapter, but let's warm up here):

```cpp
void composition_example() {
    std::vector<int> readings = {120, 45, 230, 67, 340, 89, 56, 180};

    // 过滤异常值，然后转换为电压，最后取前5个
    auto result = readings
        | std::views::filter([](int v) { return v >= 50 && v <= 300; })
        | std::views::transform([](int v) { return v * 3.3f / 4095; })
        | std::views::take(5);

    for (float v : result) {
        // v是处理后的前5个有效读数的电压值
    }
}

```

This code reads like a single sentence: "From readings, filter out valid values, convert to voltage, and take the first five." No temporary variables, no intermediate containers — the logic is so clear it's moving.

------

## Embedded in Action: Sensor Data Processing Pipeline

Let's use a real embedded scenario to demonstrate the power of views. Suppose we are building a temperature monitoring system with a set of temperature sensors, and we need to:

1. Filter out invalid readings (< -50 or > 150)
2. Convert Celsius to Fahrenheit
3. Calculate a moving average
4. Output the result

```cpp
#include <ranges>
#include <vector>
#include <iostream>
#include <numeric>

class TemperatureMonitor {
public:
    void add_reading(int celsius) {
        readings_.push_back(celsius);

        // 保持最近100个读数
        if (readings_.size() > 100) {
            readings_.erase(readings_.begin());
        }
    }

    void process_and_report() {
        // 构建处理流水线
        auto processed = readings_
            | std::views::filter([](int t) {
                return t >= -50 && t <= 150;  // 过滤无效值
            })
            | std::views::transform([](int t) {
                return t * 9.0f / 5.0f + 32.0f;  // 摄氏度转华氏度
            });

        // 计算平均值
        float sum = 0.0f;
        int count = 0;
        for (float f : processed) {
            sum += f;
            count++;
        }

        if (count > 0) {
            float avg = sum / count;
            report_temperature(avg);
        }
    }

private:
    std::vector<int> readings_;

    void report_temperature(float fahrenheit) {
        // 实际项目中这里会通过串口输出或显示
        std::cout << "Average temp: " << fahrenheit << " F\n";
    }
};

```

Notice the beauty of this code:

- No temporary containers like `std::vector` or `std::array`
- The entire processing pipeline traverses the data only once
- The memory overhead is O(1) — views only store a few pointers

------

## Views vs. Containers: When to Use Which

Views are powerful, but they are not a silver bullet. Here is a simple decision tree:

**When to use a View:**

- Read-only data, no modification needed
- Need to compose multiple operations
- Want zero-overhead copying
- The data source has a sufficiently long lifetime
- One-time iteration

**When to use a Container:**

- Need to modify data
- Need to iterate over the same result multiple times
- The data source might be destroyed
- Need to own the data

```cpp
// 场景1：用视图——一次性转换输出
void report_filtered(const std::vector<int>& data) {
    auto filtered = data | std::views::filter([](int x) { return x > 0; });
    for (int x : filtered) { output(x); }
    // filtered用完就丢，不需要保留
}

// 场景2：用容器——需要缓存结果多次使用
std::vector<int> get_valid_values(const std::vector<int>& data) {
    std::vector<int> result;
    for (int x : data | std::views::filter([](int x) { return x > 0; })) {
        result.push_back(x);
    }
    return result;  // 返回拥有的容器
}

```

------

## Pitfall Guide

### Pitfall 1: View Lifetime

Views do not own data, so if the underlying data is destroyed, the view becomes dangling:

```cpp
// ❌ 危险：返回指向局部变量的视图
auto get_bad_view() {
    std::vector<int> local = {1, 2, 3};
    return std::views::filter(local, [](int x) { return x > 1; });
    // local被销毁，返回的视图悬垂！
}

// ✅ 正确：确保底层数据生命周期足够长
class DataHolder {
    std::vector<int> data_ = {1, 2, 3};
public:
    auto get_view() {
        return std::views::filter(data_, [](int x) { return x > 1; });
        // data_与对象同生命周期，安全
    }
};

```

### Pitfall 2: Invalidation After Iteration

Some views can only be iterated once, or their state changes after iteration:

```cpp
std::vector<int> data = {1, 2, 3, 4, 5};
auto filtered = data | std::views::filter([](int x) { return x > 2; });

// 第一次迭代
for (int x : filtered) { /* 输出 3, 4, 5 */ }

// 某些实现下第二次迭代可能有问题
// 虽然大多数现代实现没问题，但最好避免多次迭代同一视图

```

If we need to iterate multiple times, consider converting to a container:

```cpp
auto filtered_vec = filtered | std::ranges::to<std::vector<int>>();
// 现在可以多次迭代filtered_vec

```

### Pitfall 3: View Types

The type of a view is a complex template instantiation product; don't try to write it manually, use `auto`:

```cpp
// ❌ 别尝试写这个类型
std::ranges::filter_view<std::ranges::transform_view<...>> view = ...;

// ✅ 用auto
auto view = data | std::views::filter(...) | std::views::transform(...);

```

### Bad News: Not All Compilers Fully Support It

C++20 Ranges are new, and some older compilers might have incomplete support. GCC 11+, Clang 13+, and MSVC 2019+ are generally fine. If your compiler spits out a bunch of template errors, check the version first.

------

## Summary

Views are the core concept of the C++20 Ranges library:

- **Lazy evaluation**: No computation at definition time, only at iteration time
- **Non-owning data**: Merely a "lens" over the underlying data
- **O(1) copy**: Passing views around everywhere has zero overhead
- **Composable**: Chaining multiple operations with the pipe operator

For embedded development, views let us write elegant data processing code while maintaining zero-overhead runtime performance. We no longer have to choose between "elegant code" and "efficient code" — we can have both.

In the next chapter, we will dive into the usage of the pipe operator `|`, along with more practical Ranges techniques. By then, you will see how the philosophy of Unix pipes is perfectly realized in C++.
