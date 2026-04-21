---
title: "constexpr 基础：编译期求值的艺术"
description: "从 constexpr 变量到 constexpr 函数，掌握编译期计算的核心机制与标准演进"
chapter: 2
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - constexpr
  - 编译期计算
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 18
prerequisites:
  - "Chapter 0: 移动构造与移动赋值"
related:
  - "constexpr 构造函数与字面类型"
  - "编译期计算实战"
---

# constexpr 基础：编译期求值的艺术

## 引言

笔者最早接触 `constexpr` 的时候，内心其实是有些不以为然的——这不就是一个"更激进的 `const`"吗？编译器优化那么强，写不写 `constexpr` 有什么区别？直到有一次在嵌入式项目里，启动阶段的 CRC 校验表在运行时通过循环生成，吃掉了好几百个时钟周期，我才意识到：**有些计算压根就不应该出现在运行时**。

`constexpr` 解决的核心问题不是"快不快"，而是"还需不需要算"。当你在代码里写下 `constexpr int kBufferSize = 256;`，你是在告诉编译器：这个值在编译阶段就已经确定了，把它直接写进二进制文件里就行。运行时连一条指令都不需要花。这比任何运行时优化都要彻底。

在这一章里，我们从头开始搞清楚 `constexpr` 的来龙去脉：它是什么，它不是什么，各个 C++ 标准版本放宽了哪些限制，以及怎么用它来写出更安全、更快的代码。

## 第一步——搞清楚 constexpr 变量

### 编译期常量 vs const

很多人把 `const` 和 `constexpr` 混为一谈，这是一个需要尽早纠正的误解。`const` 的语义是"这个变量在初始化之后不能被修改"，但它的初始值完全可以在运行时计算。而 `constexpr` 的语义更强：它要求变量的初始值必须能在编译期确定。

```cpp
// const：运行时常量，初始值可以来自运行时
int get_runtime_value();
const int kSize = get_runtime_value();     // OK，kSize 是 const 但不是编译期常量

// constexpr：编译期常量，初始值必须能在编译期算出来
constexpr int kBufferSize = 256;           // OK，256 是字面量
constexpr int kMask = kBufferSize - 1;     // OK，由编译期常量计算而来

// constexpr int kBad = get_runtime_value(); // 编译错误！初始值不是常量表达式
```

`kSize` 是一个 `const` 变量，编译器不允许你修改它，但它的值是在运行时才确定的。这意味着你不能用它来声明数组大小（C 风格数组在 C++ 中需要编译期常量作为长度），也不能用它做非类型模板参数。而 `kBufferSize` 就没有这些限制——因为它在编译期就有确定的值。

这里有一个容易踩的坑：在全局作用域或命名空间作用域中，`const` 整型变量在 C++ 中默认具有内部链接性（跟 `static` 一样），而 `constexpr` 变量也具有内部链接性。但如果你的 `const` 变量恰好用了一个编译期就能算出来的值来初始化，编译器可能会把它当作常量表达式来用——这是编译器的扩展行为，不是标准保证的。所以如果你需要一个编译期常量，就明确写 `constexpr`，不要指望编译器替你做决定。

### constexpr 变量的要求

要让一个变量声明为 `constexpr`，需要满足以下条件：它必须是字面类型（literal type），必须立即初始化，且初始表达式必须是一个常量表达式。字面类型这个概念我们会在下一章详细展开，现在只需要知道标量类型（`int`、`float`、指针等）、引用类型，以及带有 `constexpr` 构造函数的类类型都算字面类型就够了。

## 第二步——constexpr 函数：双面间谍

`constexpr` 函数是 `constexpr` 最有意思的部分。说它是"双面间谍"是因为它可以在两种场景下工作：当它的参数全是编译期常量且上下文要求编译期求值时，它会在编译期执行；否则它就像普通函数一样在运行时执行。

### 基本形态

```cpp
constexpr int square(int x)
{
    return x * x;
}

// 编译期求值：参数是字面量，上下文是 constexpr 变量初始化
constexpr int kResult = square(8);  // 编译器直接把 kResult 替换为 64

// 运行时求值：参数来自运行时
int runtime_input = 42;
int result = square(runtime_input);  // 普通函数调用，在运行时执行
```

你看，同一个函数，两种命运。这其实是 `constexpr` 函数设计的精髓：你写一份代码，编译器根据上下文决定在什么时候执行它。这种"上下文自适应"的特性让 `constexpr` 函数比单纯的编译期工具（比如模板元编程）灵活得多。

### static_assert 与 constexpr 的黄金搭档

`static_assert` 是编译期断言，它的第一个参数必须是一个常量表达式。这就天然地和 `constexpr` 函数形成了配合——你可以用 `static_assert` 来验证 `constexpr` 函数在编译期的行为。

```cpp
constexpr int factorial(int n)
{
    return n <= 1 ? 1 : n * factorial(n - 1);
}

static_assert(factorial(0) == 1, "factorial(0) should be 1");
static_assert(factorial(1) == 1, "factorial(1) should be 1");
static_assert(factorial(5) == 120, "factorial(5) should be 120");
static_assert(factorial(10) == 3628800, "factorial(10) should be 3628800");
```

如果你在 `factorial` 的实现中写了一个 bug（比如把 `n <= 1` 误写成了 `n < 1`），`static_assert` 会在编译期立刻炸掉，告诉你哪里出了问题。这种"在编译期就抓住错误"的能力，在大型项目中是非常有价值的。而且这种测试零成本——它们不会生成任何运行时代码。

## 第三步——标准的演进：从束手束脚到放开手脚

`constexpr` 在不同 C++ 标准中的能力差异非常大。理解这些差异对于写出可移植且正确的 `constexpr` 代码至关重要。

### C++11：极其严格的限制

C++11 引入了 `constexpr`，但限制极其严格。`constexpr` 函数的函数体只能包含唯一的一条 `return` 语句（外加 `static_assert`、`using` 声明等不产生代码的语句）。这意味着你不能写循环，不能声明局部变量，不能写 `if-else`——一切逻辑都必须压缩成一个三元运算符表达式或者递归调用。

```cpp
// C++11 风格：只能用递归和三元运算符
constexpr int fibonacci_cxx11(int n)
{
    return n <= 1 ? n : fibonacci_cxx11(n - 1) + fibonacci_cxx11(n - 2);
}
```

这段代码看起来简洁，但有一个隐含的问题：递归深度。编译器对 `constexpr` 求值的递归深度有默认限制（通常是 512 或 1024 层，取决于编译器实现）。如果你计算 `fibonacci(50)` 这种规模的值，虽然递归展开的调用树很大，但由于结果被缓存为常量表达式，通常不会触发限制。但如果你手写了一个线性递归（比如每次减 1 递归到 0），当参数很大时就会超过限制。

### C++14：大幅放宽

C++14 是 `constexpr` 真正变得实用的转折点。函数体中可以使用局部变量、`if-else` 语句、`for`/`while` 循环了。唯一仍然不允许的是 `goto`、`label` 语句、以及非字面类型的局部变量。

```cpp
// C++14 风格：自然得多的写法
constexpr int factorial_cxx14(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial_cxx14(6) == 720);
```

这下终于不用把所有逻辑塞进递归里了。对于嵌入式开发者来说，这意味着你可以用更自然的方式实现 CRC 计算、查表生成等逻辑，而不是绞尽脑汁地用模板元编程或者递归来绕过限制。

另一个重要变化是 `constexpr` 成员函数不再隐式 `const` 了。在 C++11 中，`constexpr` 成员函数会被隐式加上 `const` 限定符，这意味着它不能修改任何成员变量。C++14 取消了这个限制，使得 `constexpr` 成员函数可以修改成员（在编译期上下文中），这让编译期对象的行为更加灵活。

### C++17：更多实用特性

C++17 进一步扩展了 `constexpr` 的能力。`constexpr` lambda 表达式被正式支持（GCC/Clang 之前也有扩展支持），`if constexpr` 也成了标配。此外，标准库中越来越多的函数被标记为 `constexpr`：`std::array`、`std::tuple` 的各种操作、`std::min`/`std::max` 等。

```cpp
// C++17：constexpr lambda
constexpr auto add = [](int a, int b) constexpr { return a + b; };
static_assert(add(3, 4) == 7);

// C++17：constexpr std::array
#include <array>
constexpr std::array<int, 5> kArr = {1, 2, 3, 4, 5};
static_assert(kArr.size() == 5);
static_assert(kArr[2] == 3);
```

我们用一个表格来总结三个标准的关键差异：

| 能力 | C++11 | C++14 | C++17 |
|------|-------|-------|-------|
| 局部变量 | 仅 `return` | 允许 | 允许 |
| 循环 (`for`/`while`) | 禁止 | 允许 | 允许 |
| `if-else` 语句 | 禁止（只能用三元运算符） | 允许 | 允许 |
| 成员函数修改成员 | 禁止（隐式 `const`） | 允许 | 允许 |
| Lambda | 不支持 | 部分支持 | 正式支持 |
| 标准库 constexpr | 极少 | 增多 | 大量增加 |

## 第四步——constexpr vs 模板：何时用哪个

`constexpr` 和模板元编程（template metaprogramming）都能实现编译期计算，但它们的定位截然不同。模板元编程是图灵完备的，理论上可以在编译期做任何计算；但它写起来痛苦、读起来更痛苦、编译错误信息像天书。`constexpr` 则是"够用就好"的方案——它能覆盖绝大多数编译期计算需求，写起来和普通函数几乎一样。

```cpp
// 模板元编程版本：计算阶乘（C++98 风格）
template <int N>
struct Factorial {
    static constexpr int value = N * Factorial<N - 1>::value;
};
template <>
struct Factorial<0> {
    static constexpr int value = 1;
};
static_assert(Factorial<5>::value == 120);

// constexpr 版本：清晰得多
constexpr int factorial(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}
static_assert(factorial(5) == 120);
```

从笔者的经验来看，原则很简单：能用 `constexpr` 函数解决的，就不要上模板元编程。模板元编程适合那些需要在类型层面做计算的场景（比如根据类型选择不同的实现策略），而 `constexpr` 适合在值层面做编译期计算。两者经常配合使用——模板做类型层面的分派，`constexpr` 函数做具体的值计算。

## 第五步——实战示例

### 编译期斐波那契与阶乘

前面我们已经展示过这两个经典例子了。现在让我们来点更实用的——用 `constexpr` 函数生成一个编译期的查找表。

### 编译期 CRC-32 查找表

CRC 校验在通信协议和存储系统中无处不在。传统的做法是在运行时用循环生成 CRC 查表，或者用 Python 之类的工具生成表再 `#include` 进来。有了 `constexpr`，我们可以让编译器替我们生成这张表。

```cpp
#include <array>
#include <cstdint>

constexpr std::array<std::uint32_t, 256> make_crc32_table()
{
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;

    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ kPolynomial;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

// 编译期生成完整的 CRC-32 查找表
constexpr auto kCrc32Table = make_crc32_table();

// 运行时使用：只需要做查表操作
constexpr std::uint32_t crc32_compute(const std::uint8_t* data, std::size_t len)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ kCrc32Table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFu;
}
```

`kCrc32Table` 在编译期就已经完整生成，最终被直接写入目标文件的只读数据段（`.rodata`）。运行时不需要任何初始化代码，直接拿来用就行。这种模式的优雅之处在于：表生成逻辑和表使用逻辑在同一个源文件中，不需要额外的代码生成工具或构建步骤。

### 编译期 vs 运行期性能对比

为了直观感受 `constexpr` 的威力，我们来看一个简单的对比实验。

```cpp
#include <chrono>
#include <iostream>

// 运行时版本的 CRC 表生成
std::array<std::uint32_t, 256> make_crc32_table_runtime()
{
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;
    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ kPolynomial;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

int main()
{
    // 运行时生成
    auto start = std::chrono::high_resolution_clock::now();
    auto runtime_table = make_crc32_table_runtime();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Runtime generation: "
              << std::chrono::duration<double, std::micro>(end - start).count()
              << " us\n";

    // constexpr 版本：直接使用 kCrc32Table，耗时为 0
    std::cout << "CRC table first entry: " << kCrc32Table[0] << "\n";
    std::cout << "Runtime table first entry: " << runtime_table[0] << "\n";

    return 0;
}
```

运行结果大致如下（具体数值取决于硬件）：

```text
Runtime generation: 2.5 us
CRC table first entry: 0
Runtime table first entry: 0
```

虽然 2.5 微秒在 PC 上不算什么，但考虑到在嵌入式系统中可能需要更快、更确定的启动时间，这个差异就有了实际意义。更关键的是，`constexpr` 版本在编译期就完成了校验——如果表的生成逻辑有 bug，`static_assert` 会在编译期就报错，而不是等设备上电后才发现。

### 编译期数学查表

另一个常见场景是三角函数查表。在信号处理和电机控制中，经常需要快速获取 `sin`/`cos` 值。直接调用 `std::sin` 在嵌入式上可能太慢（特别是没有 FPU 的 MCU），查表是经典优化手段。

```cpp
#include <array>
#include <cmath>

template <std::size_t N>
constexpr std::array<float, N> make_sin_table()
{
    std::array<float, N> table{};
    for (std::size_t i = 0; i < N; ++i) {
        // 将 [0, N-1] 映射到 [0, 2π)
        constexpr double kPi = 3.14159265358979323846;
        double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(N);
        // 注意：C++26 之前 std::sin 不保证是 constexpr
        // 在不支持 constexpr std::sin 的编译器上，可以用泰勒展开近似
        double x = angle;
        double sin_val = x - x*x*x/6.0 + x*x*x*x*x/120.0;
        table[i] = static_cast<float>(sin_val);
    }
    return table;
}

constexpr auto kSinTable256 = make_sin_table<256>();

// 快速查表获取 sin 值（输入为 0-255 的索引）
inline float fast_sin(std::size_t index)
{
    return kSinTable256[index & 0xFF];
}
```

这里有一个值得注意的细节：C++ 标准并不保证 `std::sin` 是 `constexpr` 函数。直到 C++26 才有提案让它正式成为 `constexpr`。所以在 C++17 及之前，你需要自己用泰勒展开或其他近似方法来实现编译期的三角函数计算。不过这不影响最终效果——编译出来的查表数据是精确的。

## 常见陷阱与踩坑记录

### constexpr 不是"强制编译期求值"

这是最容易犯的错误。`constexpr` 函数只是"可以"在编译期求值，不是"必须"。如果你把一个 `constexpr` 函数的返回值赋给一个普通变量（不是 `constexpr` 变量），编译器完全可能在运行时才调用它。如果你真的需要强制编译期求值，那就用 `constexpr` 变量来接收返回值，或者在 C++20 中使用 `consteval`（我们会在后面的章节详细介绍）。

### 编译器的递归深度限制

即使是 C++14 的迭代版本，`constexpr` 函数内部仍然可能触发编译器的求值步数限制。各个编译器的默认限制不同：GCC 默认是 `constexpr` 步数限制 33554432（`-fconstexpr-ops-limit`），Clang 默认是 1048576 步，MSSTG 也有类似限制。如果你在编译期做大量计算（比如生成一张非常大的查表），可能会触发编译器的内部限制，表现为编译失败。

遇到这种情况，可以通过编译器选项提高限制（比如 GCC 的 `-fconstexpr-ops-limit=100000000`），或者考虑把大表的生成拆分成更小的片段。

### constexpr 函数中的未定义行为

`constexpr` 函数在编译期求值时，如果触发了未定义行为（UB），编译器会直接报错——这其实是件好事。比如数组越界、有符号整数溢出、除以零等，在运行时可能悄悄产生错误结果，但在 `constexpr` 求值时会被编译器拦截。

```cpp
constexpr int bad_divide(int a, int b)
{
    return a / b;  // 如果 b == 0，编译期求值时直接编译错误
}

// constexpr int kBoom = bad_divide(10, 0);  // 编译错误：除以零
```

这个特性让 `constexpr` 成为一种"安全网"——你在编译期能算出来的东西，编译器会帮你检查合法性。

## 小结

到这里，我们已经把 `constexpr` 的基础机制梳理了一遍。总结几个关键点：

`constexpr` 变量是真正的编译期常量，而 `const` 只保证"不可修改"。`constexpr` 函数是一种双模式函数，编译器根据上下文决定它在编译期还是运行期执行。从 C++11 到 C++17，`constexpr` 的限制逐步放宽，从只能写单一 `return` 语句到支持循环、局部变量和 lambda。`static_assert` 是 `constexpr` 的天然搭档，让编译期测试成为可能。能用 `constexpr` 函数解决的问题就不要上模板元编程——代码更清晰、错误信息更友好。

下一章我们会深入到 `constexpr` 构造函数和字面类型，看看如何让自定义类型也参与编译期计算。

## 参考资源

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Feature-test macro `__cpp_constexpr`](https://en.cppreference.com/w/cpp/feature_test)
