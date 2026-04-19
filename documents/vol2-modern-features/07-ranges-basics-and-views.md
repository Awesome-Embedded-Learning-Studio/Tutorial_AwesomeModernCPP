---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Ranges库基础
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Chapter 8: 类型安全'
reading_time_minutes: 16
tags:
- cpp-modern
- host
- intermediate
title: C++20 范围库基础与视图
---
# 现代嵌入式C++教程——C++20范围库基础与视图

## 引言

每次处理数组、容器数据的时候，我总觉得少点什么。用STL算法吧，那些`std::transform`、`std::filter`写起来简直是一坨——迭代器开头、迭代器结尾、临时容器、最后再贴回去，一套操作下来，代码逻辑被拆得七零八落，读起来像在啃干面包。

然后C++20带来了Ranges库，就像给你的代码装上了一台"数据处理流水线"。更重要的是，它引入了"视图"（View）这个概念——**惰性求值、零开销拷贝**，这对嵌入式开发来说简直就是量身定做的。

> 一句话总结：**Ranges让你像Unix管道一样组合操作，视图（View）让你处理数据时不产生额外拷贝，既优雅又高效。**

我们现在的目标是搞懂两件事：什么是Range，什么是View，以及它们为什么在嵌入式场景下如此有用。

------

## 从痛点说起：传统STL算法有多烦

先看看我们以前是怎么处理数据的。假设我们从传感器读了一组数据，要过滤掉异常值，然后把剩下的都乘以一个系数：

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

你看这代码有多烦：

- 每个操作都要写两遍迭代器范围
- 需要创建临时容器`filtered`来存中间结果
- 逻辑被中间变量打断，不能一眼看懂"原始数据 → 过滤 → 校准"这条链路
- 内存分配至少两次（`filtered`和`calibrated`）

在嵌入式场景下，这种临时内存分配尤其让人头疼——你确定heap有足够空间？确定不会碎片化？确定实时性不会被分配影响？

这些问题的答案，都在Ranges库里。

------

## Range是什么：简单来说就是"一对迭代器"

C++20标准库给"Range"下的定义很简单：**任何可以提供迭代器的东西**。

```cpp
std::vector<int> vec = {1, 2, 3, 4, 5};
std::array<int, 4> arr = {10, 20, 30, 40};
int native_arr[] = {100, 200, 300};

```

这些全是Range。以前我们写算法要用`vec.begin()`、`vec.end()`，现在可以直接把整个容器丢给算法：

```cpp
#include <ranges>
#include <algorithm>

// C++20之前的写法
std::sort(vec.begin(), vec.end());

// C++20的写法
std::sort(vec);  // 直接传整个容器

```

但这只是表面糖衣，真正的威力在于`<ranges>`头文件里的一整套新工具。

首先我们得区分两个概念：**Range**和**View**。

- **Range（范围）**：所有可以迭代的玩意儿，包括`vector`、`array`、原生数组
- **View（视图）**：一种特殊的Range，它不拥有数据，只是对现有数据的"某种角度的观察"，并且**惰性求值**

视图这个概念太重要了，我们用整节来聊它。

------

## 视图（View）：零开销的数据透镜

视图的本质可以用四个字概括：**懒、不拥有、可组合、O(1)拷贝**。

### 懒惰的视图

视图是"懒惰"的——你定义它的时候不会计算任何东西，只有当你真正迭代它的时候，计算才会发生：

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

### 不拥有数据

视图只是"看着"底层数据，不拥有它们：

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

### O(1)拷贝

视图的拷贝成本是常数级别的——它只存几个指针/迭代器，不会复制底层数据：

```cpp
void demo_view_copy() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    auto view1 = std::views::filter(data, [](int x) { return x > 2; });
    auto view2 = view1;  // 拷贝视图：O(1)，不复制任何元素！

    // view1和view2都指向同一个底层数据
}

```

这对嵌入式来说非常重要——你可以到处传递视图，不用担心数据拷贝的开销。

------

## 常用视图工厂函数

`<ranges>`头文件提供了一系列"视图工厂"函数，用来创建各种视图。我们挑嵌入式开发中最常用的几个来讲。

### filter：过滤数据

`std::views::filter`创建一个只满足条件的元素的视图：

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

### transform：转换每个元素

`std::views::transform`对每个元素应用一个函数：

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

### take和drop：取前N个或跳过前N个

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

在嵌入式场景里，这在处理协议头的时候特别有用：

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

### split：按分隔符切分

`std::views::split`把一个Range按分隔符切分成多个子Range：

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

解析NMEA语句（GPS数据格式）时特别好用：

```cpp
void parse_nmea(std::string_view line) {
    // NMEA格式：$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
    auto parts = std::views::split(line, ',');

    // parts[0]是"$GPGGA"，parts[1]是时间"123519"，以此类推
}

```

### iota：生成序列

`std::views::iota`生成一个递增的序列：

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

## 组合视图：开始构建管道

单个视图威力有限，但组合起来就强大了。我们可以用管道操作符`|`来把视图串联起来（这部分下一章会详细讲，这里先预热一下）：

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

这段代码读起来就像一句话："从readings中过滤出有效值，转换为电压，取前5个"。没有临时变量，没有中间容器，逻辑清晰得令人感动。

------

## 嵌入式实战：传感器数据处理流水线

让我们用一个实际的嵌入式场景来演示视图的威力。假设我们在做温度监控系统，有一组温度传感器，需要：

1. 过滤掉无效读数（<-50或>150）
2. 摄氏度转华氏度
3. 计算移动平均
4. 输出结果

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

注意这段代码的美妙之处：

- 没有`filtered_readings`、`fahrenheit_readings`这种临时容器
- 整个处理过程只遍历数据一次
- 内存开销是O(1)——视图只存几个指针

------

## 视图 vs 容器：什么时候用什么

视图虽然强大，但不是万能的。这里有个简单的决策树：

**用视图（View）的情况：**

- 只读数据，不需要修改
- 需要组合多个操作
- 想要零开销拷贝
- 数据源生命周期足够长
- 一次性遍历

**用容器（Container）的情况：**

- 需要修改数据
- 需要多次遍历同一结果
- 数据源可能被销毁
- 需要拥有数据的所有权

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

## 避坑指南

### 坑1：视图的生命周期

视图不拥有数据，所以底层数据销毁了，视图就悬垂了：

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

### 坑2：迭代后失效

某些视图只能迭代一次，或者迭代后状态改变：

```cpp
std::vector<int> data = {1, 2, 3, 4, 5};
auto filtered = data | std::views::filter([](int x) { return x > 2; });

// 第一次迭代
for (int x : filtered) { /* 输出 3, 4, 5 */ }

// 某些实现下第二次迭代可能有问题
// 虽然大多数现代实现没问题，但最好避免多次迭代同一视图

```

如果要多次迭代，考虑转成容器：

```cpp
auto filtered_vec = filtered | std::ranges::to<std::vector<int>>();
// 现在可以多次迭代filtered_vec

```

### 坑3：视图的类型

视图的类型是复杂的模板实例化产物，别试图手动写它，用`auto`：

```cpp
// ❌ 别尝试写这个类型
std::ranges::filter_view<std::ranges::transform_view<...>> view = ...;

// ✅ 用auto
auto view = data | std::views::filter(...) | std::views::transform(...);

```

### 坏消息：不是所有编译器都完全支持

C++20的Ranges是新东西，一些老编译器可能支持不完整。GCC 11+、Clang 13+、MSVC 2019+基本没问题。如果你的编译器报错一堆模板错误，先检查版本。

------

## 小结

视图（View）是C++20 Ranges库的核心概念：

- **懒惰求值**：定义时不计算，迭代时才计算
- **不拥有数据**：只是底层数据的"透镜"
- **O(1)拷贝**：到处传递视图无开销
- **可组合**：用管道操作符串联多个操作

对嵌入式开发来说，视图让我们既能写出优雅的数据处理代码，又能保持零开销的运行时性能。不用在"优雅代码"和"高效代码"之间做选择——我们全都要。

下一章，我们深入探讨管道操作符`|`的用法，以及更多Ranges的实战技巧。到时候你会看到，Unix管道的哲学是如何在C++中完美实现的。
