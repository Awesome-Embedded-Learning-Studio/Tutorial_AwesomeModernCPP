---
title: "Lambda 基础：匿名函数的优雅表达"
description: "从语法要素到 STL 配合，掌握 C++ lambda 表达式的核心用法"
chapter: 3
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - lambda
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 0: 移动构造与移动赋值"
related:
  - "Lambda 捕获机制深入"
  - "std::function 与类型擦除"
---

# Lambda 基础：匿名函数的优雅表达

## 引言

笔者在写排序逻辑的时候，一直觉得 C 的函数指针和 C++98 的仿函数都有点别扭。函数指针要么写在全局作用域里污染命名空间，要么得用 `static` 成员函数配合 `void*` 上下文传来传去；仿函数倒是可以把状态封装在类里，但为了一个两行的比较逻辑去定义一个完整的类，多少有点杀鸡用牛刀(哇, 最OOP的一集)。C++11 带来了 lambda 表达式，本质上就是一个可以在使用处就地定义的匿名函数对象——不用跳到文件头部去声明，不用给编译器生成额外的符号，逻辑就写在调用点旁边，读代码的人一眼就能看明白。

> **学习目标**
>
> - 理解 lambda 表达式的语法要素和编译器背后的闭包类型
> - 掌握 lambda 与 STL 算法的配合使用
> - 了解值捕获和引用捕获的基本语义
> - 知道什么时候该用 `auto`，什么时候该用 `std::function`

---

## Lambda 的语法拆解

Lambda 表达式的完整语法看起来有点唬人，但拆开来看每一部分都很直觉：

```cpp
[capture](parameters) -> return_type { body }
```

`capture` 是捕获列表，决定了 lambda 怎么访问外层作用域的变量；`parameters` 和普通函数的参数列表完全一致；`-> return_type` 是尾置返回类型，在 C++11 中需要满足特定条件才能省略让编译器推导（详见下节）；`body` 就是函数体。我们从一个最简单的 lambda 开始逐步加料：

```cpp
// 什么都不做的 lambda,纯摆烂的
auto do_nothing = []() {};

// 简单返回一个值
auto forty_two = []() { return 42; };

// 带参数
auto double_it = [](int x) { return x * 2; };

// 实际使用：像普通函数一样调用
int result = double_it(21);  // result == 42
```

你会发现 lambda 用 `auto` 来接收——这是因为每个 lambda 表达式都会生成一个独一无二的、没有名字的类类型（所谓的闭包类型，closure type），你没办法直接写出这个类型的名字。`auto` 在这里就是最自然的选择。

---

## 返回类型推导

C++11 的 lambda 返回类型推导规则相对严格：只有当 lambda 体满足以下条件时，编译器才能自动推导返回类型：

1. 函数体只有一条 `return` 语句，或者
2. 所有 `return` 语句返回的表达式推导出相同类型

满足这些条件时，可以省略 `-> return_type`：

```cpp
// 自动推导为 int
auto square = [](int x) { return x * x; };

// 自动推导为 double（因为有 static_cast<double>）
auto divide = [](int a, int b) -> double {
    return static_cast<double>(a) / b;
};
```

如果函数体比较复杂，比如有多个分支各自返回不同的路径，编译器可能无法推导，或者推导的结果和你预期不一致。这时候显式指定返回类型是最稳妥的做法：

```cpp
auto classify = [](int x) -> int {
    if (x > 0) {
        return x * 2;
    } else if (x < 0) {
        return -x;
    }
    return 0;   // 如果没有这条，某些编译器可能报警告
};
```

笔者的建议是：简单 lambda 省略返回类型，复杂 lambda 显式写出来。省略之后代码更紧凑，但前提是别让读代码的人猜半天返回值到底是什么类型。

---

## 作为 STL 算法参数——lambda 的主战场

Lambda 表达式最常见的场景，是作为 STL 算法的谓词或操作函数。以前你要么传一个全局函数指针，要么写一个仿函数类，现在直接在调用处写 lambda 就行了，逻辑一目了然：

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

void process_data() {
    std::vector<int> readings = {12, 45, 23, 67, 34, 89, 56};

    // 找出第一个超过阈值的读数
    auto it = std::find_if(readings.begin(), readings.end(),
                          [](int value) { return value > 50; });

    // 统计有多少个异常值
    int anomaly_count = std::count_if(readings.begin(), readings.end(),
                                     [](int value) { return value > 80; });
    std::cout << "Anomalies: " << anomaly_count << "\n";

    // 原地翻倍
    std::transform(readings.begin(), readings.end(), readings.begin(),
                  [](int value) { return value * 2; });

    // 自定义排序：降序
    std::sort(readings.begin(), readings.end(),
             [](int a, int b) { return a > b; });
}
```

以前你得把 `is_above_threshold()` 定义在别的地方，读代码的时候要跳来跳去找定义。现在 lambda 就写在算法调用旁边，扫一眼就知道这个谓词在干什么。

---

## 捕获外部变量——让 lambda "看见"外面

默认情况下，lambda 不能访问外层作用域的任何变量。这是有意为之的设计：lambda 要的是一个干净的沙箱，不会意外地触碰外部状态。当你确实需要访问外部变量时，就通过捕获列表显式声明：

```cpp
int threshold = 50;

// 编译错误：threshold 不在 lambda 的作用域内
// auto check = [](int value) { return value > threshold; };

// 值捕获：复制一份 threshold 到闭包对象中
auto by_value = [threshold](int value) { return value > threshold; };

// 引用捕获：直接引用外部的 threshold
auto by_ref = [&threshold](int value) { return value > threshold; };
```

值捕获会在 lambda 创建的那一刻复制变量，之后外部的修改不会影响 lambda 内部的副本；引用捕获则让 lambda 直接操作原始变量。两种方式各有适用场景，也有各自的坑——我们在下一章会专门展开讨论。这里只需要记住一点：**当你只读不写的时候，值捕获是最安全的默认选择。**

常用的默认捕获写法也有两种：`[=]` 表示值捕获所有用到的外部变量，`[&]` 表示引用捕获所有用到的外部变量。用起来很方便，但在生产代码中笔者建议尽量显式列出要捕获的变量名，避免无意间捕获了不该捕获的东西。

```cpp
int a = 1, b = 2, c = 3;

// 全值捕获
auto sum_all = [=]() { return a + b + c; };  // 6

// 全引用捕获——可以修改外部变量
auto increment_all = [&]() { a++; b++; c++; };
increment_all();  // a=2, b=3, c=4

// 混合捕获：a 值捕获，b 引用捕获
auto mixed = [a, &b]() { return a + b; };
```

---

## Lambda 的类型——闭包类型揭秘

前面提过，每个 lambda 表达式都会产生一个唯一的、匿名的类类型（闭包类型）。这个类类型有一个 `operator()` 成员函数，参数和返回值就是你在 lambda 里写的那些。标准只规定了行为，具体实现由编译器决定。概念上，你可以把 lambda 理解为编译器生成了类似这样的类：

```cpp
// 你写的 lambda
auto greet = [](const std::string& name) -> std::string {
    return "Hello, " + name;
};

// 编译器概念上生成的类（简化版）
struct /* 编译器生成的唯一名字 */ {
    std::string operator()(const std::string& name) const {
        return "Hello, " + name;
    }
};
auto greet = /* 上面那个类的实例 */{};
```

实际实现中，编译器会根据 lambda 的捕获列表添加相应的数据成员，根据 `mutable` 关键字决定 `operator()` 是否为 const。类型名称的生成方式由各编译器自行决定（比如 GCC 用 `_Z...` 编码，Clang 用 `$_0...` 等），跨编译器也不保证一致。

这就是为什么你没法直接写出 lambda 的类型名——这个名字是编译器内部生成的，不同编译器、不同编译单元的名字都不一样。所以存放 lambda 的时候要么用 `auto`（编译期类型已知），要么用 `std::function`（运行时类型擦除，有额外开销）。

用模板参数传递 lambda 是零开销抽象的常见做法——编译器能看到完整的 lambda 类型，有机会进行内联优化：

```cpp
template<typename Func>
void call_func(Func f) {
    f();
}

call_func([]() { /* ... */ });  // 类型对编译器可见，可能内联
```

这里的关键是"可能"：是否真的内联取决于编译器的优化策略、lambda 的复杂程度、编译选项等因素。但相比 `std::function` 的运行时间接调用，模板参数至少给了编译器优化的机会。

> **关于 `std::function` 的开销**：`std::function` 内部使用了类型擦除和小对象优化（Small Buffer Optimization, SBO）。在 libstdc++ 中，一个 `std::function` 对象通常占据 32 字节（64位系统），即使存储的 lambda 只需要 1 字节。调用时多一层虚函数风格的间接跳转，可能阻止内联。如果不需要运行时多态，优先用 `auto` 或模板参数。我们在第四章《类型擦除与 std::function》会深入展开。

---

## 实战：事件处理系统

让我们用 lambda 来搭建一个简单的事件处理系统，这在实际项目中是很常见的需求——注册回调、触发回调，回调可能来自不同的模块，各有各的上下文：

```cpp
#include <cstdint>
#include <functional>
#include <array>
#include <iostream>

class EventDispatcher {
public:
    using Handler = std::function<void(uint32_t)>;

    void on_event(int id, Handler handler) {
        if (id >= 0 && id < static_cast<int>(handlers_.size())) {
            handlers_[id] = std::move(handler);
        }
    }

    void trigger(int id, uint32_t timestamp) {
        if (id >= 0 && id < static_cast<int>(handlers_.size()) && handlers_[id]) {
            handlers_[id](timestamp);
        }
    }

private:
    std::array<Handler, 8> handlers_;
};

// 使用示例
void setup_system() {
    EventDispatcher dispatcher;
    int press_count = 0;
    uint32_t last_press_time = 0;

    // 注册按键回调：引用捕获 press_count 和 last_press_time
    dispatcher.on_event(0, [&](uint32_t timestamp) {
        if (timestamp - last_press_time > 50) {   // 50ms 防抖
            press_count++;
            last_press_time = timestamp;
            std::cout << "Press #" << press_count
                      << " at " << timestamp << "ms\n";
        }
    });

    // 注册超时回调：值捕获 threshold
    uint32_t threshold = 1000;
    dispatcher.on_event(1, [threshold](uint32_t timestamp) {
        if (timestamp > threshold) {
            std::cout << "Timeout at " << timestamp << "ms\n";
        }
    });

    // 模拟事件触发
    dispatcher.trigger(0, 100);
    dispatcher.trigger(0, 160);   // 距上次 60ms，通过防抖
    dispatcher.trigger(0, 180);   // 距上次 20ms，被防抖过滤
    dispatcher.trigger(1, 1200);
}
```

运行结果：

```text
Press #1 at 100ms
Press #2 at 160ms
Timeout at 1200ms
```

可以看到 lambda 作为回调非常自然——捕获列表把需要的上下文变量引进来，函数体写业务逻辑，注册的时候传进去就行了。比起 C 风格的 `void (*callback)(void* user_data)` 配合 `void*` 强转，类型安全和可读性都好太多了。

---

## C++14 的泛型 lambda

C++14 给 lambda 带来了一个很实用的增强：参数类型可以用 `auto`。这让 lambda 变成了一个模板函数对象——编译器会为不同的参数类型各自生成一份 `operator()` 的实例：

```cpp
// 泛型 lambda：可以接受任何支持 operator+ 的类型
auto add = [](auto a, auto b) { return a + b; };

int xi = add(3, 4);              // int operator+(int, int)
double xd = add(3.5, 2.5);       // double operator+(double, double)
std::string xs = add(std::string("hello"), std::string(" world"));
```

编译器在背后生成的闭包类型大致长这样：

```cpp
struct GenericClosure {
    template<typename T1, typename T2>
    auto operator()(T1 a, T2 b) const {
        return a + b;
    }
};
```

泛型 lambda 在写通用算法和工具函数的时候特别好用，不需要在 lambda 外面再套一层模板函数了。这块我们会在第三章《泛型 Lambda 与模板 Lambda》里深入探讨。

---

## 注意事项与踩坑预警

### Lambda 体不要太长

Lambda 的优势在于就地定义、逻辑紧凑。如果一个 lambda 超过 5-7 行，就该考虑把它提取成一个命名函数或者仿函数了。超过这个长度的 lambda 反而会降低可读性——读代码的人要在算法调用的参数列表里滚动好几屏，这就违背了"逻辑在使用处"的初衷。

### 引用捕获的生命周期陷阱

这是 lambda 最常见的 bug 来源之一：引用捕获的变量在 lambda 执行时已经销毁了。典型的场景是在函数内创建 lambda 并返回它：

```cpp
// 危险！返回的 lambda 引用了局部变量 local
auto make_bad_lambda() {
    int local = 42;
    return [&local]() { return local; };   // local 在函数返回后销毁
}

// 安全：值捕获
auto make_safe_lambda() {
    int local = 42;
    return [local]() { return local; };    // lambda 持有副本
}
```

引用捕获本身没有错，但你必须保证被引用的对象活得比 lambda 久。在事件系统、异步回调这类场景中，这个约束特别容易被忽视。

### 优先用 `auto` 而非 `std::function` 存储 lambda

除非你需要运行时多态（比如把不同类型的回调放进同一个容器），否则不要用 `std::function` 来存储 lambda。`auto` 直接持有闭包类型，类型大小等于捕获的数据成员大小（无捕获的 lambda 通常只有 1 字节），给编译器提供了内联优化的机会；`std::function` 做了类型擦除，固定开销（32-64 字节），调用时多一层间接跳转。

```cpp
// 编译期类型已知，大小=1字节（无捕获），可能内联
auto f = [](int x) { return x * 2; };

// 类型擦除，大小=32字节（libstdc++），运行时间接调用
std::function<int(int)> g = [](int x) { return x * 2; };
```

这个差异在性能关键路径上可能很重要，但也要避免过早优化：如果代码不是热点路径，`std::function` 的便利性可能更重要。

---

## 小结

Lambda 表达式是现代 C++ 中最实用的特性之一。它把"在使用处定义函数"这件事件的成本降到了最低——不需要额外的命名、不需要类定义、不需要分离声明和实现。核心要点回顾：

- Lambda 的语法是 `[capture](params) -> ret { body }`，大部分时候可以省略返回类型
- Lambda 的类型是编译器生成的唯一闭包类型，用 `auto` 存储最自然
- Lambda 最大的用武之地是作为 STL 算法的谓词和操作参数
- 值捕获复制变量、引用捕获引用变量，各有各的安全边界
- C++14 的 `auto` 参数让 lambda 变成了模板函数对象

下一章我们深入讨论 lambda 的捕获机制——值捕获和引用捕获在底层到底发生了什么，C++14 的初始化捕获解决了什么问题，以及那些让你凌晨两点还在 debug 的捕获陷阱。

## 参考资源

- [Lambda expressions (C++11) - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++14 generic lambdas - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Generic_lambdas)
