---
title: "Lambda 表达式基础"
description: "Lambda基本语法"
chapter: 9
order: 1
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
reading_time_minutes: 15
prerequisites:
  - "Chapter 8: 类型安全"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# Lambda表达式基础

## 引言

我们在写一个排序函数，需要自定义比较规则。传统方式是：写一个单独的比较函数，或者写一个仿函数类。这两种方式都有点烦人——函数分离在别处，仿函数类又要写一堆样板代码。

然后，C++11带来了Lambda表达式，就像给你的代码送来了一台"一次性函数生成器"。

> 一句话总结：**Lambda是匿名函数，可以就地定义和使用，特别适合作为回调函数和算法参数。**

------

## Lambda的基本语法

Lambda表达式的语法看起来有点吓人，但拆解后其实很清晰：

```cpp
[capture](parameters) -> return_type { body }

```

各部分说明：

- `capture`：捕获列表（后面章节详细讲）
- `parameters`：参数列表（和普通函数一样）
- `return_type`：返回类型（可以省略，编译器自动推导）
- `body`：函数体

最简单的Lambda：

```cpp
[]() { }              // 最简单：什么都不做
[]() { return 42; }   // 简单返回
[](int x) { return x * 2; }  // 带参数

```

实际使用：

```cpp
auto add = [](int a, int b) { return a + b; };
int result = add(3, 4);  // result = 7

```

<details>
<summary>查看完整可编译示例</summary>

```cpp
--8<-- "code/examples/chapter09/01_lambda_basics/basic_syntax.cpp"

```

</details>

------

## 类型推导：自动识别返回类型

只要你的函数体只有一条`return`语句，编译器就能自动推导返回类型：

```cpp
// 返回int，自动推导
auto square = [](int x) { return x * 2; };

// 返回double，自动推导
auto divide = [](int a, int b) { return static_cast<double>(a) / b; };

```

如果有多条语句，或者逻辑复杂，可以用`->`显式指定：

```cpp
auto complex = [](int x) -> int {
    if (x > 0) {
        return x * 2;
    } else {
        return x;
    }
};

```

<details>
<summary>查看完整可编译示例</summary>

```cpp
--8<-- "code/examples/chapter09/01_lambda_basics/type_deduction.cpp"

```

</details>

------

## 作为算法参数：Lambda最常用的场景

Lambda表达式最经典的用法是作为STL算法的参数。这在嵌入式开发中同样有用，比如处理数据数组：

```cpp
#include <algorithm>
#include <vector>

void process_sensor_data() {
    std::vector<int> readings = {12, 45, 23, 67, 34, 89, 56};

    // 找出第一个超过阈值的读数
    auto it = std::find_if(readings.begin(), readings.end(),
                          [](int value) { return value > 50; });

    // 统计有多少个异常值（>80）
    int count = std::count_if(readings.begin(), readings.end(),
                             [](int value) { return value > 80; });

    // 对所有值进行处理
    std::transform(readings.begin(), readings.end(), readings.begin(),
                  [](int value) { return value * 2; });
}

```

这比传统写函数干净太多了——逻辑就在使用的地方，不用跳来跳去。

<details>
<summary>查看完整可编译示例</summary>

```cpp
--8<-- "code/examples/chapter09/01_lambda_basics/algorithm_args.cpp"

```

</details>

------

## 捕获外部变量：让Lambda"看见"外面

虽然下一章会详细讲捕获，但这里先简单介绍基础概念。

默认情况下，Lambda不能访问外部变量：

```cpp
int threshold = 50;
// ❌ 编译错误：threshold不可访问
auto lambda = [](int value) { return value > threshold; };

```

需要通过捕获列表"引入"外部变量：

```cpp
int threshold = 50;

// 值捕获：复制一份threshold
auto by_value = [threshold](int value) { return value > threshold; };

// 引用捕获：使用外部的threshold
auto by_ref = [&threshold](int value) { return value > threshold; };

// 捕获所有变量（值捕获）
auto capture_all = [=](int value) { return value > threshold; };

// 捕获所有变量（引用捕获）
auto capture_all_ref = [&](int value) { return value > threshold; };

```

嵌入式场景示例：

```cpp
void configure_pwm(uint32_t base_addr, int frequency) {
    // Lambda捕获base_addr和frequency
    auto set_duty = [base_addr, frequency](int percent) {
        uint32_t period = 1000000 / frequency;  // 微秒
        uint32_t duty = period * percent / 100;
        *reinterpret_cast<volatile uint32_t*>(base_addr + 0x04) = duty;
    };

    set_duty(25);   // 25%占空比
    set_duty(50);   // 50%占空比
    set_duty(75);   // 75%占空比
}

```

<details>
<summary>查看完整可编译示例</summary>

```cpp
--8<-- "code/examples/chapter09/01_lambda_basics/capture_basics.cpp"

```

</details>

------

## Lambda的类型：它到底是什么？

Lambda表达式的类型是**唯一的、未命名的类类型**。你不能直接声明Lambda类型，但可以用`auto`或者`std::function`：

```cpp
auto lambda1 = []() { };
// lambda1的类型是编译器生成的某个唯一类型

// 用std::function存储（有运行时开销）
std::function<void()> lambda2 = []() { };

// 用模板参数传递
template<typename Func>
void call_func(Func f) {
    f();
}
call_func([]() { /* ... */ });

```

**嵌入式提示**：`std::function`有动态分配开销，优先使用`auto`或模板。

------

## 实战示例：事件处理器

假设你在做一个简单的GPIO事件处理系统：

```cpp
#include <cstdint>
#include <functional>

class GPIOManager {
public:
    using EventHandler = std::function<void(uint32_t timestamp)>;

    // 注册上升沿事件处理
    void on_rising_edge(int pin, EventHandler handler) {
        rising_handlers[pin] = handler;
    }

    // 模拟硬件中断触发
    void simulate_interrupt(int pin, uint32_t timestamp) {
        auto it = rising_handlers.find(pin);
        if (it != rising_handlers.end() && it->second) {
            it->second(timestamp);  // 调用注册的Lambda
        }
    }

private:
    std::array<EventHandler, 16> rising_handlers;  // 假设16个引脚
};

// 使用示例
void setup_gpio_system() {
    GPIOManager gpio;

    int debounce_counter = 0;
    uint32_t last_press_time = 0;

    // 注册按键处理Lambda
    gpio.on_rising_edge([&](uint32_t timestamp) {
        // 防抖逻辑
        if (timestamp - last_press_time > 50) {  // 50ms防抖
            debounce_counter++;
            last_press_time = timestamp;
            // 触发业务逻辑...
        }
    });

    // 注册LED控制Lambda
    gpio.on_rising_edge([](uint32_t timestamp) {
        // 翻转LED
        static bool led_state = false;
        led_state = !led_state;
        // GPIO_Write(LED_PIN, led_state);
    }, 5);  // 引脚5
}

```

<details>
<summary>查看完整可编译示例</summary>

```cpp
--8<-- "code/examples/chapter09/01_lambda_basics/event_handler.cpp"

```

</details>

------

## 实战示例：配置生成器

Lambda表达式可以用来创建"迷你配置语言"：

```cpp
struct TimerConfig {
    uint32_t prescaler = 0;
    uint32_t period = 0;
    bool auto_reload = false;
};

TimerConfig make_timer_config(std::function<void(TimerConfig&)> builder) {
    TimerConfig config;
    builder(config);
    return config;
}

// 使用：创建一个1kHz定时器配置
auto timer1_cfg = make_timer_config([](TimerConfig& c) {
    c.prescaler = 7200 - 1;     // 72MHz / 7200 = 10kHz
    c.period = 10 - 1;          // 10kHz / 10 = 1kHz
    c.auto_reload = true;
});

// 使用：创建一个PWM定时器配置
auto pwm_cfg = make_timer_config([](TimerConfig& c) {
    c.prescaler = 1 - 1;        // 不分频
    c.period = 1000 - 1;        // PWM周期
    c.auto_reload = true;
});

```

这种写法让配置代码非常清晰，所有参数都在一个地方集中管理。

<details>
<summary>查看完整可编译示例</summary>

```cpp
--8<-- "code/examples/chapter09/01_lambda_basics/config_builder.cpp"

```

</details>

------

## 泛型Lambda（C++14）

C++14让Lambda支持`auto`参数，变成模板函数：

```cpp
// 泛型Lambda：可以接受任何可加类型
auto add = [](auto a, auto b) { return a + b; };

int x = add(3, 4);           // int
double y = add(3.5, 2.5);    // double

```

这在嵌入式开发中处理寄存器操作时特别有用：

```cpp
auto write_reg = [](auto addr, auto value) {
    *reinterpret_cast<volatile decltype(value)*>(addr) = value;
};

write_reg(0x40000000, uint32_t(0x12345678));
write_reg(0x50000000, uint16_t(0xABCD));

```

<details>
<summary>查看完整可编译示例</summary>

```cpp
--8<-- "code/examples/chapter09/01_lambda_basics/generic_lambda.cpp"

```

</details>

------

## 注意事项与最佳实践

### 1. 复杂度控制

如果Lambda超过5行，考虑把它命名或者提取成函数：

```cpp
// ❌ 太长了，难以阅读
std::vector<int> result = std::accumulate(/* ... */, [](int acc, int x) {
    // ... 20行复杂逻辑 ...
});

// ✅ 提取成命名函数
int transform_element(int acc, int x);
std::vector<int> result = std::accumulate(/* ... */, transform_element);

```

### 2. 注意捕获的生命周期

引用捕获的变量必须保证在Lambda执行时仍然有效：

```cpp
// ❌ 危险：返回后局部变量销毁
auto get_lambda() {
    int local = 42;
    return [&local]() { return local; };  // 悬垂引用！
}

// ✅ 安全：值捕获
auto get_lambda_safe() {
    int local = 42;
    return [local]() { return local; };  // 复制了一份
}

```

### 3. 嵌入式环境中避免动态分配

避免用`std::function`存储Lambda，使用`auto`或模板：

```cpp
// ❌ 可能动态分配
std::function<void(int)> f = [](int x) { /* ... */ };

// ✅ 零开销
auto f = [](int x) { /* ... */ };

```

------

## 小结

Lambda表达式是现代C++的重要特性：

- **就地定义**：代码在使用的地方，逻辑清晰
- **匿名函数**：不需要额外命名，减少命名污染
- **捕获机制**：灵活控制外部变量访问
- **算法友好**：与STL算法配合使用，威力强大

在下一章，我们将深入了解Lambda的捕获机制及其对性能的影响。
