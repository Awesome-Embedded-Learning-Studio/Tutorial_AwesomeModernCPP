---
title: "inline 与 constexpr 函数"
description: "理解 inline 的真正含义和 constexpr 函数的编译期计算能力，为现代 C++ 的零开销抽象打下基础"
chapter: 3
order: 4
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "重载与默认参数"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# inline 与 constexpr 函数

我们到现在已经写过不少函数了。每次调用一个函数，程序要做的事情其实不少——保存当前执行位置、分配栈帧、跳转到函数体、执行完再跳回来、销毁栈帧、恢复现场。对于几十行的大函数来说这点开销不算什么，但如果是一个只做 `return x * x` 的小函数，调用开销可能比函数本身的计算还要大。能不能把这些"薄得像纸一样"的函数调用直接展开，省掉所有跳转和栈帧的代价？这就是 `inline` 和 `constexpr` 要解决的问题。

## inline——被误解最深的关键字

### inline 不是"强制内联"

很多人把 `inline` 理解成"建议编译器把这个函数展开到调用处"。这个理解在过去不算错，但在现代 C++ 中它已经严重偏离了 `inline` 的真正用途。事实是：**编译器完全有权无视你写的 `inline` 关键字**。现代编译器有非常成熟的内联启发式算法，会根据函数体大小、调用频率等因素自动决定是否内联。反过来，就算你不写 `inline`，编译器也完全可能把一个简短的函数内联展开。

那 `inline` 到底是干什么的？答案和 ODR 有关。

### inline 的真正含义——ODR 豁免权

C++ 有一条铁律叫 **ODR（One Definition Rule，单定义规则）**：一个函数在整个程序中只能有一个定义。如果你把函数定义写在头文件里，然后这个头文件被两个 `.cpp` 文件 include，链接器就会看到两个一模一样的定义，直接报重定义错误。

`inline` 关键字可以打破这条规则。被标记为 `inline` 的函数允许在多个翻译单元中存在相同的定义，只要所有定义完全一致——链接器会自动合并它们，只保留一份。所以 `inline` 的本质不是"请展开这个函数"，而是"允许这个函数在头文件里定义，被多次 include 也不会炸"。

```cpp
// math_utils.h
#pragma once

// 有了 inline：多个 .cpp include 此头文件不会重定义
inline int square(int x)
{
    return x * x;
}
```

> **踩坑预警**：`inline` 函数的定义必须出现在头文件中。如果你在头文件里只写了 `inline` 声明，而把定义放在 `.cpp` 文件里，其他翻译单元调用时找不到函数体，链接器要么报错，要么只能按普通函数调用。**定义必须和声明一起出现在头文件中**。
>
> C++17 引入了 `inline` 变量。和 `inline` 函数一样，它允许在头文件中定义全局变量，被多次 include 不会违反 ODR。比如 `inline static const int kMaxSize = 100;` 在头文件中就可以这样写。

在 C++17 之后，`inline` 作为关键字的存在感越来越弱了。`constexpr` 函数默认就是 `inline` 的，类体内定义的成员函数默认也是 `inline` 的，模板函数同样默认是 `inline` 的。真正需要手动写 `inline` 的场景，几乎只剩下"在头文件中定义一个非模板、非 constexpr 的自由函数"这一种。但理解它的真正含义，依然是理解 C++ 编译链接模型的重要一步。

## constexpr 函数——编译期能算就算

如果说 `inline` 解决的是"允许多次定义"，那 `constexpr` 解决的就是一个更本质的问题：**能不能让函数在编译期就算完，直接把结果"焊"到二进制文件里？**

### constexpr 的基本含义

`constexpr` 是 C++11 引入的关键字，用于声明"可能在编译期求值"的函数或变量。一个 `constexpr` 函数在被调用时，如果所有参数都是编译期已知的常量，函数的求值会发生在编译阶段，结果直接变成一个常量。如果参数中包含运行时才能确定的值，函数就退化为普通的运行时调用。这种"编译期能算就算，不行就运行时算"的双模特性，是 `constexpr` 最强大的地方。

```cpp
constexpr int square(int x)
{
    return x * x;
}

int main()
{
    constexpr int kResult = square(5);  // 编译期求值，kResult = 25

    int x = 0;
    std::cin >> x;
    int runtime_result = square(x);    // 运行时求值，退化为普通调用

    return 0;
}
```

> **踩坑预警**：`constexpr` 不等于 `const`。`constexpr` 表示"编译期常量"（值在编译时确定），`const` 表示"运行时不可修改"（值在运行时确定但不能改变）。如果你需要编译期确定的值，用 `constexpr` 而不是 `const`。

### constexpr 的演进——一代比一代强

`constexpr` 函数能做什么、不能做什么，每个 C++ 标准都在放宽限制。在 C++11 中，`constexpr` 函数体只能包含一条 `return` 语句，不能有局部变量、循环或 `if/else`，写阶乘只能靠递归加三元运算符：

```cpp
// C++11：只能用 return + 三元运算符
constexpr int factorial(int n)
{
    return (n <= 1) ? 1 : n * factorial(n - 1);
}
```

C++14 大幅放宽了限制——函数体里可以有局部变量、`if/else`、`for/while` 循环，写法终于正常了。C++17 进一步允许 `constexpr` lambda 和 `if constexpr`，C++20 更是放开了几乎所有限制——甚至可以在 `constexpr` 函数中使用 `std::vector`、`std::string` 和动态内存分配。到 C++23，`constexpr` 中甚至可以抛出异常并用 `try/catch` 捕获。这个趋势非常明确：**C++ 正在让尽可能多的逻辑能在编译期执行**。

## 编译期计算的实际案例

### 编译期 Fibonacci 与 static_assert

`static_assert` 是编译期断言——条件不满足编译直接失败。用它来验证 `constexpr` 函数的结果，既能确保逻辑正确，又能倒逼编译器真的在编译期完成了计算。

```cpp
constexpr int fib(int n)
{
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}

static_assert(fib(0) == 0);
static_assert(fib(1) == 1);
static_assert(fib(10) == 55);
```

不过递归版 Fibonacci 的时间复杂度是 O(2^n)，编译器在编译期执行它时也要承受这个指数级代价。在编译期计算中应该尽量用迭代来控制复杂度。

### 用 constexpr 做模板参数计算

模板的非类型参数必须是编译期常量，`constexpr` 函数的返回值正好满足这个条件：

```cpp
constexpr int bytes_from_bits(int bits)
{
    return (bits + 7) / 8;  // bit 数换算成 byte 数
}

// 模板参数需要编译期常量，constexpr 函数完美适配
std::array<uint8_t, bytes_from_bits(32)> buffer{};
```

这种用法在嵌入式开发中尤其常见——寄存器宽度、缓冲区大小、DMA 传输长度这些编译期就能确定的值，用 `constexpr` 函数计算后直接喂给模板，既保证类型安全，又没有任何运行时开销。

### 编译期查找表

嵌入式开发中经常需要预计算的查找表。传统做法是手写一个数组，改参数时还要手动重新算。用 `constexpr` 可以让编译器帮你生成：

```cpp
/// @brief 编译期生成 CRC8 查找表
constexpr std::array<uint8_t, 256> make_crc8_table()
{
    std::array<uint8_t, 256> table{};
    constexpr uint8_t kPoly = 0x07;
    for (int i = 0; i < 256; ++i) {
        uint8_t byte = static_cast<uint8_t>(i);
        for (int bit = 0; bit < 8; ++bit) {
            byte = (byte & 0x80) ? (byte << 1) ^ kPoly : (byte << 1);
        }
        table[i] = byte;
    }
    return table;
}

// 编译期生成，运行时零开销
constexpr auto kCrc8Table = make_crc8_table();
```

整个查找表在编译阶段就已经生成完毕，直接嵌入到二进制文件的 `.rodata` 段。运行时访问它和访问一个手写的 `const` 数组没有任何区别。

## consteval 和 constinit——更严格的控制（C++20）

C++20 在 `constexpr` 基础上引入了两个新关键字，目前只需要知道它们存在即可。

`consteval` 声明的函数**必须**在编译期求值——用运行时的值调用它，编译器直接报错。这和 `constexpr` 的"能编译就编译、不行就运行时"完全不同：

```cpp
consteval int power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; ++i) { result *= base; }
    return result;
}

constexpr int kVal = power(2, 10);  // OK：编译期求值，kVal = 1024
// int x; std::cin >> x;
// int y = power(x, 3);             // 编译错误：x 不是编译期常量
```

`constinit` 作用于变量声明，保证变量在编译期完成初始化但不要求它成为 `const`。这解决了"静态初始化顺序陷阱"——不同翻译单元中的全局变量初始化顺序是不确定的，可能导致未定义行为。`constinit` 确保初始化发生在编译期，直接绕开这个问题。

## 什么时候该用 constexpr

一条简单的判断标准：**如果一个函数是纯函数（pure function）——给定相同输入永远返回相同输出、并且没有副作用——那它就是 `constexpr` 的候选者。** 数学函数（平方、绝对值、最大公约数）、查找表生成（正弦表、CRC 表）、配置值计算（寄存器地址、缓冲区大小）、类型特征判断（`std::size()`、`std::extent_v`）——这些不依赖运行时状态的纯计算，都应该尽量交给编译器。编译器比 CPU 更有耐心，而且只算一次。

## 实战演练——inline_constexpr.cpp

现在把这一章的内容整合到一个完整程序里，演示 `constexpr` 在编译期和运行时的不同行为。

```cpp
#include <array>
#include <cstdint>
#include <cstdio>

/// @brief 编译期平方计算
constexpr int square(int x)
{
    return x * x;
}

/// @brief 编译期阶乘（迭代版，C++14 风格）
constexpr int factorial(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

/// @brief 编译期整数幂
constexpr int power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; ++i) {
        result *= base;
    }
    return result;
}

/// @brief 编译期生成 CRC8 查找表
constexpr std::array<uint8_t, 256> make_crc8_table()
{
    std::array<uint8_t, 256> table{};
    constexpr uint8_t kPoly = 0x07;
    for (int i = 0; i < 256; ++i) {
        uint8_t byte = static_cast<uint8_t>(i);
        for (int bit = 0; bit < 8; ++bit) {
            byte = (byte & 0x80) ? (byte << 1) ^ kPoly : (byte << 1);
        }
        table[i] = byte;
    }
    return table;
}

// 编译期验证
static_assert(square(5) == 25, "square(5) should be 25");
static_assert(square(-3) == 9, "square(-3) should be 9");
static_assert(factorial(5) == 120, "5! should be 120");
static_assert(factorial(10) == 3628800, "10! should be 3628800");
static_assert(power(2, 10) == 1024, "2^10 should be 1024");

// 编译期生成查找表
constexpr auto kCrc8Table = make_crc8_table();

/// @brief 用查找表计算 CRC8
uint8_t compute_crc8(const uint8_t* data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc = kCrc8Table[crc ^ data[i]];
    }
    return crc;
}

int main()
{
    printf("=== 编译期计算结果 ===\n");
    printf("square(5)     = %d\n", square(5));
    printf("factorial(10) = %d\n", factorial(10));
    printf("power(2, 16)  = %d\n", power(2, 16));
    printf("CRC8 table[0] = 0x%02X\n", kCrc8Table[0]);
    printf("CRC8 table[1] = 0x%02X\n", kCrc8Table[1]);

    printf("\n=== 运行时 CRC8 计算 ===\n");
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t crc = compute_crc8(test_data, sizeof(test_data));
    printf("CRC8 of {01 02 03 04 05} = 0x%02X\n", crc);

    printf("\n=== 编译期 vs 运行时 ===\n");
    constexpr int kCompileTime = square(7);
    int runtime_input = 7;
    int runtime_result = square(runtime_input);
    printf("constexpr square(7) = %d\n", kCompileTime);
    printf("runtime  square(7)  = %d\n", runtime_result);
    printf("结果一致: %s\n",
           kCompileTime == runtime_result ? "是" : "否");

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o inline_constexpr inline_constexpr.cpp
./inline_constexpr
```

预期输出：

```text
=== 编译期计算结果 ===
square(5)     = 25
factorial(10) = 3628800
power(2, 16)  = 65536
CRC8 table[0] = 0x00
CRC8 table[1] = 0x07

=== 运行时 CRC8 计算 ===
CRC8 of {01 02 03 04 05} = 0xBC

=== 编译期 vs 运行时 ===
constexpr square(7) = 49
runtime  square(7)  = 49
结果一致: 是
```

`static_assert` 在编译阶段就已经验证了所有计算结果的正确性——如果某个函数的实现有 bug，编译根本不会通过。`kCrc8Table` 是一个 256 字节的查找表，完全在编译期生成并嵌入二进制文件，运行时访问它没有任何初始化开销。`square(7)` 在编译期和运行时给出了相同的结果，这正是 `constexpr` "一份代码两种模式"的精髓。

> **踩坑预警**：`constexpr` 函数中如果包含浮点运算，编译期求值和运行时求值的结果可能存在微小差异——不同编译器、不同平台的浮点精度不完全一致。对于整数运算这不是问题，但如果你在 `constexpr` 函数中使用了浮点算法，最好用 `static_assert` 锁定期望结果。

## 动手试试

### 练习一：constexpr 最大公约数

写一个 `constexpr int gcd(int a, int b)` 函数，使用欧几里得算法（辗转相除法）计算两个正整数的最大公约数。用 `static_assert` 验证 `gcd(12, 8) == 4`、`gcd(100, 75) == 25`。

### 练习二：编译期斐波那契查找表

写一个 `constexpr` 函数生成一个包含 30 个元素的 `std::array<uint32_t, 30>`，其中第 i 个元素是第 i 个 Fibonacci 数。用 `static_assert` 验证 `table[10] == 55`、`table[20] == 6765`。注意用迭代而不是递归，避免指数级编译时间。

### 练习三：constexpr popcount

写一个 `constexpr int count_bits(int n)` 函数，返回整数 `n` 的二进制表示中有多少个 1。用 `static_assert` 验证 `count_bits(0) == 0`、`count_bits(7) == 3`、`count_bits(255) == 8`。提示：每次 `n &= (n - 1)` 会消除最低位的 1（Brian Kernighan 技巧）。

## 小结

这一章我们拆解了两个和函数执行方式密切相关的关键字。`inline` 的真正含义不是"强制内联"，而是 ODR 豁免——允许同一个函数定义出现在多个翻译单元中。`constexpr` 则是现代 C++ 编译期计算的基石——标记为 `constexpr` 的函数在参数全部为编译期常量时会自动在编译期求值，否则退化为普通运行时调用。C++14 放宽了函数体限制，C++20 引入了 `consteval` 和 `constinit`，整个趋势就是让尽可能多的计算在编译期完成。

掌握 `constexpr` 的核心收益在于：**把运行时的工作量转移给编译器**。查找表生成、配置参数计算、类型特征判断——这些不依赖运行时状态的纯计算，都应该尽量交给编译器。

下一章我们学习指针与引用——C++ 中让无数新手崩溃、又让老手欲罢不能的东西。别紧张，有了前面这些基础，指针和引用的图景会比想象中清晰得多。
