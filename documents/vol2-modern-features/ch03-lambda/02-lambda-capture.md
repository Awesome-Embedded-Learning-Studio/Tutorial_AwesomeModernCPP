---
title: "Lambda 捕获机制深入"
description: "值捕获、引用捕获、初始化捕获的语义与陷阱"
chapter: 3
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - lambda
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 18
prerequisites:
  - "Chapter 3: Lambda 基础"
related:
  - "泛型 Lambda 与模板 Lambda"
---

# Lambda 捕获机制深入

## 引言

上一章我们快速过了一遍 lambda 的基本语法，也简单提到了捕获列表的存在。但你可能心中一直有几个问题：值捕获到底复制了什么？引用捕获在底层是不是就是存了个指针？`[=]` 和 `[&]` 这种默认捕获有什么坑？C++14 的初始化捕获到底好在哪里？这一章我们把捕获机制从头到尾拆清楚，不光讲"怎么用"，更要讲清楚"编译器在背后做了什么"以及"哪些用法会在运行时爆炸"。

> **学习目标**
>
> - 理解值捕获和引用捕获的底层语义——闭包类型到底存了什么
> - 掌握 C++14 初始化捕获和 C++17 `*this` 捕获的用法与动机
> - 识别并避免捕获相关的常见陷阱（悬垂引用、生命周期问题）
> - 了解 lambda 对象的大小和性能影响

---

## 值捕获——复制一份到闭包对象中

值捕获的语义非常直白：在 lambda 创建的那一刻，被捕获的变量被复制一份，作为闭包类型的成员变量存储。之后外部变量的任何修改都不会影响 lambda 内部的副本。

```cpp
void demo_value_capture() {
    int threshold = 100;

    // threshold 被复制到闭包对象中
    auto is_high = [threshold](int value) {
        return value > threshold;
    };

    threshold = 200;             // 修改外部变量
    bool result = is_high(150);  // false，lambda 里的 threshold 还是 100
}
```

从编译器的角度看，上面这个 lambda 大致被翻译成这样的闭包类型：

```cpp
struct ClosureType {
    int threshold;  // 被捕获的变量变成了成员

    bool operator()(int value) const {
        return value > threshold;
    }
};

auto is_high = ClosureType{100};  // 构造时复制 threshold
```

注意那个 `const`——值捕获的成员在 `operator()` 内部默认是 `const` 的，你不能修改它们。如果你确实需要在 lambda 内部修改捕获的副本，需要加上 `mutable` 关键字：

```cpp
int counter = 0;

// 编译错误：counter 在 lambda 内是 const int
// auto bad = [counter]() { counter++; };

// 加 mutable：允许修改 lambda 内部的副本
auto make_counter = [counter]() mutable {
    return ++counter;   // 修改的是闭包对象自己的 counter，不是外部的
};

std::cout << make_counter() << "\n";  // 1
std::cout << make_counter() << "\n";  // 2
std::cout << counter << "\n";         // 0——外部的 counter 没有被碰过
```

`mutable` 的意思是告诉编译器：这个 lambda 的 `operator()` 不是 `const` 的。每次调用都可能修改闭包对象内部的状态。这也是为什么每次调用 `make_counter()` 都会递增——闭包对象自己维护了一份独立的状态。

---

## 引用捕获——存储原始变量的地址

引用捕获的语义也并不神秘：编译器在闭包类型中存储的是被捕获变量的指针（或者引用，在底层实现上基本等价）。lambda 内部对捕获变量的读写，实际上都是对原始变量的操作。

```cpp
void demo_ref_capture() {
    int sum = 0;

    auto accumulate = [&sum](int value) {
        sum += value;   // 直接修改外部的 sum
    };

    accumulate(10);
    accumulate(20);
    accumulate(30);
    // sum == 60
}
```

对应的闭包类型大致长这样：

```cpp
struct ClosureType {
    int& sum;  // 存储的是引用

    void operator()(int value) const {
        sum += value;  // 通过引用修改外部变量
    }
};
```

这里有个很有意思的细节：`operator()` 是 `const` 的，但我们却通过 `sum` 修改了外部变量。这是因为引用本身（存储的地址）是 `const` 的——你不能让引用指向另一个对象——但引用所绑定的对象的值是可以修改的。这和 `int* const ptr` 不能改指针但能改 `*ptr` 是一个道理。

引用捕获最大的优势是零拷贝——对于大型对象（比如 `std::vector`、`std::string`），引用捕获避免了不必要的复制。但最大的风险也在这里：**被引用的变量必须活得比 lambda 久**。

---

## 默认捕获——`[=]` 和 `[&]` 的隐患

当需要捕获的变量很多的时候，挨个列出来确实有点烦人。C++ 提供了两种默认捕获方式：`[=]` 表示所有用到的外部变量都按值捕获，`[&]` 表示都按引用捕获。

```cpp
void demo_default_capture() {
    int a = 1, b = 2, c = 3;

    // 全值捕获
    auto sum = [=]() { return a + b + c; };   // 6

    // 全引用捕获
    auto increment = [&]() { a++; b++; c++; };
    increment();   // a=2, b=3, c=4
}
```

你也可以在默认捕获的基础上对个别变量指定不同的方式——混合捕获：

```cpp
void demo_mixed_capture() {
    int threshold = 100;
    int count = 0;
    double factor = 1.5;

    // 默认值捕获，但 count 按引用捕获
    auto process = [=, &count](int value) {
        if (value > threshold) {
            count++;
            return static_cast<int>(value * factor);
        }
        return value;
    };
}
```

听起来很方便，但 `[=]` 和 `[&]` 有几个不太显眼的陷阱。`[=]` 默认值捕获不会捕获 `this` 指针——等等，不对，在 C++20 之前，`[=]` 实际上是可以隐式捕获 `this` 的，这导致了一个经典问题：你以为自己在值捕获成员变量的值，实际上捕获的是 `this` 指针，lambda 内部通过 `this->member` 访问的仍然是原始对象的成员。C++20 修正了这个行为，`[=]` 不再隐式捕获 `this`，需要显式写 `[=, this]` 或 `[=, *this]`。

笔者的建议是：**在生产代码中尽量显式列出你要捕获的变量名**，少用 `[=]` 和 `[&]`。显式的好处是代码审查的时候一眼就能看出 lambda 依赖了哪些外部状态，也避免了无意中捕获了不该捕获的东西。

---

## C++14 初始化捕获——lambda 拥有自己的状态

C++14 引入了初始化捕获（init capture），有时候也叫广义 lambda 捕获（generalized lambda capture）。语法是在捕获列表里写 `name = expression`，其中 `name` 是一个新的变量名，`expression` 是初始化表达式。这个变量完全属于闭包对象，和外部没有任何关系：

```cpp
void demo_init_capture() {
    int base = 10;

    // 捕获 base + 5 的结果，而不是 base 本身
    auto lam = [value = base + 5]() {
        return value * 2;   // value == 15
    };
}
```

初始化捕获最有用的场景是**移动捕获**——把只移动类型（`std::unique_ptr`、`std::thread` 等）移入闭包对象：

```cpp
#include <memory>

auto make_handler() {
    auto ptr = std::make_unique<int>(42);

    // 把 unique_ptr 移入 lambda
    return [p = std::move(ptr)]() {
        return *p;   // p 是 lambda 独占的
    };
}
```

在 C++11 里你要实现同样的效果，得手写一个仿函数类，把 `unique_ptr` 作为成员变量。C++14 的初始化捕获让这件事变得非常自然。

另一个常见的用法是用初始化捕获来替代 `mutable` 计数器，语义更清晰：

```cpp
// C++11 风格：需要 mutable
int x = 0;
auto counter_old = [x]() mutable { return ++x; };

// C++14 风格：初始化捕获，语义更明确
auto counter_new = [count = 0]() mutable { return ++count; };
```

第二个版本的好处是 `count` 完全是 lambda 自己的状态，和外部变量 `x` 没有任何关系——从名字上就能看出来这是一个独立的计数器。

---

## C++17 的 `*this` 捕获——按值捕获整个对象

在成员函数中写 lambda 的时候，如果你想捕获当前对象，传统写法是 `[this]`。但 `[this]` 捕获的是指针，如果 lambda 的生命周期比对象本身长，你就会得到一个悬垂的 `this` 指针。C++17 引入了 `[*this]`，它按值捕获整个对象——在闭包类型中存一份对象的副本：

```cpp
#include <iostream>
#include <string>
#include <functional>

class Sensor {
    std::string name_;
    int reading_ = 0;

public:
    explicit Sensor(std::string name) : name_(std::move(name)) {}

    std::function<int()> make_reader() {
        // [*this]：复制整个 Sensor 对象到闭包中
        // 即使原始 Sensor 被销毁，lambda 仍然安全
        return [*this]() mutable {
            return ++reading_;
        };
    }

    std::function<int()> make_reader_unsafe() {
        // [this]：只存指针，对象销毁后变成悬垂指针
        return [this]() {
            return ++reading_;   // 危险！
        };
    }
};

void demo_star_this() {
    std::function<int()> reader;

    {
        Sensor s("temperature");
        reader = s.make_reader();      // [*this]：安全
        // reader_unsafe = s.make_reader_unsafe();  // [this]：危险
    }
    // s 已经销毁

    std::cout << reader() << "\n";     // 安全：lambda 持有 s 的副本
    std::cout << reader() << "\n";     // 2
}
```

`[*this]` 的代价是复制整个对象。如果对象很大（包含 `std::vector`、大 `std::array` 等），这个复制开销可能不小。但对于小型配置对象、值对象来说，这个复制换来的安全性是非常值得的。

⚠️ **注意**：`[*this]` 要求当前 lambda 所在的上下文是能够解引用 `this` 的成员函数。在静态成员函数或非成员函数中不能使用 `[*this]`。

---

## 捕获陷阱——悬垂引用与生命周期

捕获机制最常见、也最让人头疼的 bug 来源就是生命周期问题。让我们看几个经典的陷阱场景。

### 返回引用捕获的 lambda

```cpp
// 经典陷阱：返回引用了局部变量的 lambda
auto make_dangling() {
    int count = 0;
    return [&count]() { return ++count; };
    // count 在函数返回后销毁，lambda 持有的是悬垂引用
}

auto bad = make_dangling();
// bad() 是未定义行为！
```

修复方式很简单——用值捕获或初始化捕获代替引用捕获：

```cpp
auto make_safe() {
    int count = 0;
    return [count]() mutable { return ++count; };    // 值捕获：安全
}

auto make_safe2() {
    return [count = 0]() mutable { return ++count; }; // 初始化捕获：更清晰
}
```

### 循环中的引用捕获

这个陷阱在异步编程和事件系统中特别常见：

```cpp
#include <vector>
#include <functional>

std::vector<std::function<void()>> handlers;

void demo_loop_trap() {
    for (int i = 0; i < 5; ++i) {
        // 错误：所有 lambda 引用同一个 i，循环结束后 i == 5
        handlers.push_back([&i]() {
            std::cout << i << " ";   // 全部输出 5
        });
    }

    handlers.clear();

    for (int i = 0; i < 5; ++i) {
        // 正确：每个 lambda 有自己的 i 副本
        handlers.push_back([i]() {
            std::cout << i << " ";   // 输出 0 1 2 3 4
        });
    }
}
```

### 捕获 `this` 的隐患

```cpp
class Device {
    std::string name_ = "sensor";

public:
    auto get_handler() {
        // 如果 Device 对象在 lambda 执行前被销毁，this 就悬垂了
        return [this]() { return name_; };
    }

    // 更安全的做法：捕获需要的成员，而不是 this
    auto get_handler_safe() {
        return [name = name_]() { return name; };
    }

    // C++17 最安全：按值捕获整个对象
    auto get_handler_safest() {
        return [*this]() { return name_; };
    }
};
```

---

## Lambda 对象的大小分析

理解了捕获机制在底层的存储方式之后，lambda 对象的大小就很好理解了——它就是所有被捕获变量的大小之和（可能加上一些对齐填充），再加上一个虚函数表指针（如果有的话，实际上标准的 lambda 没有虚函数）。我们可以用 `sizeof` 来验证：

```cpp
#include <iostream>

void demo_closure_size() {
    int a = 0;
    double b = 0.0;
    int& ref = a;

    auto no_capture = []() {};
    auto capture_int = [a]() { return a; };
    auto capture_ref = [&a]() { return a; };
    auto capture_both = [a, &b]() { return a + b; };

    std::cout << "no_capture:    " << sizeof(no_capture) << " bytes\n";
    // 通常 1 byte（空类特例）

    std::cout << "capture_int:   " << sizeof(capture_int) << " bytes\n";
    // 通常 4 bytes（一个 int）

    std::cout << "capture_ref:   " << sizeof(capture_ref) << " bytes\n";
    // 通常 8 bytes（一个指针，64 位系统）

    std::cout << "capture_both:  " << sizeof(capture_both) << " bytes\n";
    // 通常 16 bytes（int + double 引用/指针，考虑对齐）
}
```

典型的输出（64 位系统，GCC）：

```text
no_capture:    1 bytes
capture_int:   4 bytes
capture_ref:   8 bytes
capture_both:  16 bytes
```

值得注意的一点：无捕获的 lambda 大小通常是 1 字节而不是 0 字节——C++ 不允许大小为 0 的对象（否则数组里的元素地址就没法区分了）。而引用捕获存储的是指针，在 64 位系统上占 8 字节。

当你把 lambda 存入 `std::function` 时，存储空间就不只这些了——`std::function` 通常有自己的 SBO 缓冲区（32-64 字节），再加上类型擦除的管理开销。这也是为什么我们在上一章说"优先用 `auto` 存储 lambda"。

---

## 性能考量——何时内联，何时不能

Lambda 的性能特征和它的捕获方式以及存储方式密切相关。

当 lambda 以编译期已知的类型（`auto` 或模板参数）被调用时，编译器能够看到完整的闭包类型和 `operator()` 实现，可以完美内联。这时候值捕获和引用捕获的差异基本为零——即使值捕获多了一次复制，只要 lambda 不逃逸出当前作用域，编译器也能优化掉这次复制。

但如果 lambda 被存入 `std::function`，情况就不同了。`std::function` 的类型擦除引入了一层间接调用，编译器无法跨过这层间接来内联。而且如果捕获的内容超出了 `std::function` 的 SBO 缓冲区大小，还会触发堆分配。

```cpp
#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>

void benchmark_lambda_styles() {
    std::vector<int> data(1'000'000);
    int threshold = 50;

    // 风格 1：auto + 算法模板参数——完全内联
    auto start = std::chrono::high_resolution_clock::now();
    auto count1 = std::count_if(data.begin(), data.end(),
                               [threshold](int x) { return x > threshold; });
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "auto lambda: "
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
              << " us\n";

    // 风格 2：std::function——有间接调用开销
    std::function<bool(int)> pred = [threshold](int x) { return x > threshold; };
    start = std::chrono::high_resolution_clock::now();
    auto count2 = std::count_if(data.begin(), data.end(), pred);
    end = std::chrono::high_resolution_clock::now();
    std::cout << "std::function: "
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
              << " us\n";
}
```

在优化开启的情况下，`auto` 版本通常比 `std::function` 版本快 3-10 倍。当然，具体的差距取决于编译器、优化级别和 lambda 的复杂度，但趋势是一致的：**当你不需要运行时多态时，用模板或 `auto` 来传递 lambda 是最优选择。**

---

## 选择哪种捕获方式——决策指南

我们把捕获方式的选择总结成几条简单的规则：

对于小型不可变数据（`int`、`float`、简单结构体），值捕获是最安全的默认选择。它确保 lambda 不依赖外部状态，线程安全，也不会有生命周期问题。对于大型对象（`std::vector`、`std::string`），如果 lambda 内部需要读取而不修改，引用捕获加上 `const` 是零拷贝的方案；如果 lambda 需要独立持有这个对象，用初始化捕获 `name = std::move(obj)` 把它移入闭包。对于需要在 lambda 内部修改的外部变量（累加器、状态更新），引用捕获是最自然的选择，但要确保变量的生命周期足够长。

在成员函数中，如果 lambda 不逃逸出对象的生命周期，`[this]` 是方便的；如果 lambda 可能比对象活得久，用 `[*this]`（C++17）或初始化捕获需要的成员变量。在生产代码中，笔者强烈建议显式列出捕获的变量名，避免使用 `[=]` 和 `[&]`——显式代码让 code review 变得更容易，也减少了意外的捕获。

---

## 小结

Lambda 的捕获机制是理解 lambda 性能和安全性的关键。核心要点：

- 值捕获复制变量到闭包对象中，默认 `const`，`mutable` 可修改闭包内部的副本
- 引用捕获存储变量的地址/引用，零拷贝但需要保证生命周期
- C++14 初始化捕获让 lambda 可以拥有独立的状态，支持移动捕获
- C++17 `*this` 捕获按值复制整个对象，解决了 `[this]` 的悬垂指针问题
- Lambda 对象的大小等于所有被捕获变量的大小之和
- 在不需要运行时多态时，用 `auto` 或模板参数传递 lambda 性能最优

## 参考资源

- [Lambda capture - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
- [C++14 generalized lambda capture](https://en.cppreference.com/w/cpp/language/lambda#Captures)
- [C++17 capture *this](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
