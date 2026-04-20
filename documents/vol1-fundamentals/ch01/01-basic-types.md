---
title: "基本数据类型"
description: "掌握 C++ 的整数、浮点、字符和布尔类型，理解类型大小、取值范围与平台差异"
chapter: 1
order: 1
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "第一个 C++ 程序"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 基本数据类型

上一章我们写出了第一个 C++ 程序，用 `int` 声明了整数变量，用 `std::cin` 和 `std::cout` 完成了输入输出。你可能当时就在想：`int` 到底能存多大的数？小数怎么办？文字怎么表示？这些问题非常好，因为它们直指 C++ 类型系统的核心——我们这一章就来彻底搞清楚 C++ 给我们提供了哪些基本数据类型，每种类型能存什么、存多少、边界在哪里。

哦，你可能会说——搞这个干啥，有病？兄弟们，理解数据类型不仅仅是为了应付考试或者面试题，它是写出正确程序的基础。不知道 `int` 的上限，你就可能在一个看似正常的循环里突然溢出；不了解浮点数的精度陷阱，你的金融计算可能在悄无声息地吞掉一分钱；搞不清楚 `char` 的符号问题，你的网络协议可能在跨平台的时候莫名其妙地出错。所以我们现在花时间把这些东西夯实，后面会省下大量的排错时间。尽管你说——少放屁，你还说准我之后的事情了。嗯，这个我之前也是这样的，直到我自己手撸代码真被int狠狠艹了一段发现这块应该是unsigned long long才对的时候老实了。真得学兄弟们。

## 整数家族——C++ 给了我们多少种选择

C++ 的整数类型乍一看挺多的，但其实有一个清晰的规律。按照"从小到大"排列，最基本的整数类型有 `short`、`int`、`long`、`long long` 四种，每种都可以加上 `unsigned` 前缀变成无符号版本。C++ 标准对它们只规定了最小范围——比如 `int` 至少是 16 位——但在今天主流的 64 位平台上，`int` 通常是 32 位，`long long` 是 64 位。这里有一个很容易混淆的地方：`long` 在 Linux 64 位系统上是 64 位，但在 Windows 64 位系统上只有 32 位。没错，同一段代码，换个操作系统，`sizeof(long)` 就不一样了。这就是为什么我们需要后面会讲到的固定宽度类型。

让我们用代码把这些类型的大小看清楚。先写一个简单的程序：

```cpp
// integer-type-sizes.cpp
// 打印 C++ 基本整数类型在当前平台上的大小

#include <iostream>

int main()
{
    std::cout << "=== 整数类型大小（字节） ===" << std::endl;
    std::cout << "short:          " << sizeof(short) << std::endl;
    std::cout << "int:            " << sizeof(int) << std::endl;
    std::cout << "long:           " << sizeof(long) << std::endl;
    std::cout << "long long:      " << sizeof(long long) << std::endl;
    std::cout << std::endl;

    std::cout << "=== 对应的无符号版本 ===" << std::endl;
    std::cout << "unsigned short: " << sizeof(unsigned short) << std::endl;
    std::cout << "unsigned int:   " << sizeof(unsigned int) << std::endl;
    std::cout << "unsigned long:  " << sizeof(unsigned long) << std::endl;
    std::cout << "unsigned long long: " << sizeof(unsigned long long)
              << std::endl;

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -o integer-type-sizes integer-type-sizes.cpp
./integer-type-sizes
```

在典型的 64 位 Linux 系统上，输出大概是：

```text
=== 整数类型大小（字节） ===
short:          2
int:            4
long:           8
long long:      8

=== 对应的无符号版本 ===
unsigned short: 2
unsigned int:   4
unsigned long:  8
unsigned long long: 8
```

如果你在 Windows 上跑同样的代码，`long` 那一行会显示 4 而不是 8。（可能，我印象里会不一样，但是我完全不了解MSVC的小差异，这一点要是说错了，请大佬们速速批评！）这就是平台差异，也是很多跨平台 bug 的温床。

> ⚠️ **踩坑预警**：`sizeof` 返回的类型是 `std::size_t`，这是一个无符号整数类型。如果你在表达式里混用 `std::size_t` 和有符号整数（比如 `int`），编译器可能会发出"有符号/无符号比较"的警告。这种警告不要无视，因为它确实可能引发逻辑错误——后面我们讲到类型转换的时候会详细解释。

## 固定宽度类型——跨平台的定心丸

既然 `long` 的大小随平台变化，那我们写跨平台代码、解析二进制文件格式、操作网络协议的时候，怎么确保一个整数恰好是 32 位？答案是 `<cstdint>` 头文件提供的**固定宽度类型**。

这些类型名字很直白：`int8_t` 是恰好 8 位的有符号整数，`uint32_t` 是恰好 32 位的无符号整数，以此类推。在你的平台不支持某种宽度的情况下（比如某些嵌入式平台没有 64 位整数），对应的类型就不会存在——编译直接报错，这比运行时出 bug 好得多。

```cpp
#include <cstdint>
#include <iostream>

int main()
{
    std::cout << "=== 固定宽度类型大小（字节） ===" << std::endl;
    std::cout << "int8_t:   " << sizeof(int8_t) << std::endl;
    std::cout << "int16_t:  " << sizeof(int16_t) << std::endl;
    std::cout << "int32_t:  " << sizeof(int32_t) << std::endl;
    std::cout << "int64_t:  " << sizeof(int64_t) << std::endl;
    std::cout << std::endl;
    std::cout << "uint8_t:  " << sizeof(uint8_t) << std::endl;
    std::cout << "uint16_t: " << sizeof(uint16_t) << std::endl;
    std::cout << "uint32_t: " << sizeof(uint32_t) << std::endl;
    std::cout << "uint64_t: " << sizeof(uint64_t) << std::endl;

    return 0;
}
```

输出：

```text
=== 固定宽度类型大小（字节） ===
int8_t:   1
int16_t:  2
int32_t:  4
int64_t:  8

uint8_t:  1
uint16_t: 2
uint32_t: 4
uint64_t: 8
```

不管你在 Linux、Windows 还是 macOS 上跑，结果都一样。是的。我没有讨论咱们这是32位还是64位。这就是固定宽度类型的魅力——它消除了平台差异带来的不确定性。在嵌入式开发中，我们几乎总是用 `uint8_t`、`uint32_t` 这些类型来操作寄存器，而不是用 `int` 或 `unsigned long`，因为寄存器的宽度是固定的，和编译器所在平台没关系。

## 类型的极限——std::numeric_limits

知道了一种类型占多少字节，下一个问题自然是：它到底能存多大的数？C++ 提供了一个非常优雅的工具来回答这个问题——`<limits>` 头文件里的 `std::numeric_limits` 模板。

```cpp
#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    std::cout << "=== int32_t 的范围 ===" << std::endl;
    std::cout << "最小值: " << std::numeric_limits<int32_t>::min()
              << std::endl;
    std::cout << "最大值: " << std::numeric_limits<int32_t>::max()
              << std::endl;
    std::cout << std::endl;

    std::cout << "=== uint32_t 的范围 ===" << std::endl;
    std::cout << "最小值: " << std::numeric_limits<uint32_t>::min()
              << std::endl;
    std::cout << "最大值: " << std::numeric_limits<uint32_t>::max()
              << std::endl;

    return 0;
}
```

输出：

```text
=== int32_t 的范围 ===
最小值: -2147483648
最大值: 2147483647

=== uint32_t 的范围 ===
最小值: 0
最大值: 4294967295
```

`int32_t` 的最大值是 2147483647，也就是大约 21 亿——这个数字在做累加运算的时候其实很容易溢出。`uint32_t` 的最大值翻了一倍到大约 42 亿，看起来大了很多，但在处理大文件偏移量或者高精度时间戳的时候依然不够用。所以如果你需要存超过 21 亿的数，请用 `int64_t`。

> ⚠️ **踩坑预警**：整数溢出在 C++ 里是**未定义行为**（unsigned 类型除外，它会回绕）。也就是说，如果你让一个 `int` 加上某个值导致超出了最大值，编译器可以做任何事情——生成错误的计算结果、优化掉你的溢出检查代码、甚至让程序崩溃。永远不要假设"溢出了就取模"，那是 `unsigned` 才有的保证。

## 浮点数——精确与近似的博弈

整数只能存完整的数值，一旦涉及小数就需要浮点类型了。C++ 提供了三种浮点类型：`float`（单精度，通常 4 字节）、`double`（双精度，通常 8 字节）和 `long double`（扩展精度，大小因平台而异，在 x86-64 Linux 上通常是 16 字节）。

`float` 大约能提供 7 位有效数字，`double` 大约 15 位。这个差异在实际编程中非常关键——如果你在做科学计算或者金融相关的运算，7 位精度很可能不够用，这时候应该直接上 `double`。

但浮点数有一个根本性的问题：它用二进制来表示十进制的小数，所以很多看起来"整整齐齐"的十进制小数，在二进制里是无限循环的。这导致浮点运算天生就是近似的。来看一个经典的例子：

```cpp
#include <iomanip>
#include <iostream>

int main()
{
    float a = 0.1f;
    float b = 0.2f;
    float c = a + b;

    // 用高精度输出，看清楚浮点数的真面目
    std::cout << std::setprecision(20);
    std::cout << "0.1f  = " << a << std::endl;
    std::cout << "0.2f  = " << b << std::endl;
    std::cout << "a + b = " << c << std::endl;
    std::cout << "0.3f  = " << 0.3f << std::endl;
    std::cout << std::endl;

    // 比较结果
    if (c == 0.3f) {
        std::cout << "a + b == 0.3f (相等)" << std::endl;
    }
    else {
        std::cout << "a + b != 0.3f (不相等!)" << std::endl;
        std::cout << "差值: " << (c - 0.3f) << std::endl;
    }

    return 0;
}
```

输出：

```text
0.1f  = 0.10000000149011611938
0.2f  = 0.20000000298023223877
a + b = 0.30000001192092895508
0.3f  = 0.30000001192092895508

a + b == 0.3f (相等)
```

有趣——在这个特定例子里它们碰巧相等了（因为误差方向一致）。但如果我们换成 `double`，结果可能就不同了。这个例子真正要说明的是：浮点数在内存里的表示和你在代码里写的字面值并不完全一致。所以**永远不要用 `==` 比较两个浮点数**。正确的做法是判断它们的差值是否在一个足够小的范围内：

```cpp
bool is_approximately_equal(double x, double y, double epsilon)
{
    // epsilon 通常取 1e-9 或更小，具体看你的精度需求
    return (x - y) < epsilon && (y - x) < epsilon;
}
```

`long double` 的情况比较特殊——不同平台上它的大小和精度差异很大。在 x86-64 Linux 上它通常是 80 位扩展精度（实际占 16 字节因为有对齐填充），而在某些 ARM 平台上它可能和 `double` 完全一样。所以除非你清楚自己的目标平台提供了什么，否则不要过度依赖 `long double`。

## 字符类型——不只是一个字母

字符类型可能是 C++ 基本类型里最容易让人迷惑的，因为它处在整数和文本的交界处。最基础的 `char` 恰好占 1 个字节（8 位），它既可以存一个 ASCII 字符，也可以当一个小范围的整数来用。但事情到这里还没完——`char`、`signed char`、`unsigned char` 在 C++ 里是**三种不同的类型**。普通 `char` 到底是有符号还是无符号，由编译器决定。GCC 默认 `char` 是有符号的，但在 ARM 平台上通常是无符号的。

```cpp
#include <iostream>

int main()
{
    char c = 'A';
    signed char sc = -1;
    unsigned char uc = 255;

    std::cout << "char 'A' 的整数值: " << static_cast<int>(c) << std::endl;
    std::cout << "signed char -1 的整数值: " << static_cast<int>(sc)
              << std::endl;
    std::cout << "unsigned char 255 的整数值: " << static_cast<int>(uc)
              << std::endl;

    return 0;
}
```

输出：

```text
char 'A' 的整数值: 65
signed char -1 的整数值: -1
unsigned char 255 的整数值: 255
```

你可能注意到了，我在输出的时候用了 `static_cast<int>(c)` 而不是直接 `std::cout << c`。这是因为 `std::cout` 看到 `char` 类型会直接输出字符而不是数字——如果我们直接输出 `sc`，终端可能会显示一个乱码字符。

除了经典的 `char`，C++ 还有几个为 Unicode 设计的字符类型。`wchar_t` 是"宽字符"，在 Windows 上是 2 字节（UTF-16），在 Linux 上是 4 字节（UTF-32），所以它也不跨平台。C++11 引入了 `char16_t`（2 字节，对应 UTF-16）和 `char32_t`（4 字节，对应 UTF-32），C++20 又加了 `char8_t`（1 字节，对应 UTF-8）。对于我们这个阶段的教程，知道它们存在就好，等到后面处理字符串的时候再来深入。

## 布尔类型——真与假，没有灰色地带

`bool` 是 C++ 里最简单的类型，只有两个值：`true` 和 `false`。它占多少内存呢？通常是 1 个字节，虽然理论上只需要 1 个 bit 就够了——但现代 CPU 的最小寻址单位是字节，所以 `sizeof(bool)` 在主流平台上都是 1。

`bool` 和整数之间有一套隐式转换规则：零值转换为 `false`，任何非零值转换为 `true`。反过来，`false` 转换为 `0`，`true` 转换为 `1`。这套规则看起来简单，但藏着一些容易掉进去的坑。

> ⚠️ **踩坑预警**：不要写出类似 `if (x = 5)` 这样的代码。这里 `=` 是赋值而不是比较，`x` 被赋值为 5，然后 5 被隐式转换为 `true`，所以这个 `if` 永远为真。编译器加上 `-Wall` 会给出警告，所以再一次强调——编译警告不是摆设，认真对待每一条。

另一个值得注意的地方是 `bool` 到 `int` 的转换在数学运算中的行为：

```cpp
#include <iostream>

int main()
{
    bool flag = true;
    int count = flag + flag + flag;

    std::cout << "true + true + true = " << count << std::endl;
    std::cout << "sizeof(bool) = " << sizeof(bool) << std::endl;

    return 0;
}
```

输出：

```text
true + true + true = 3
sizeof(bool) = 1
```

`true` 参与算术运算时被当作 `1`，`false` 被当作 `0`。这有时候可以用来做简洁的计数——比如统计一组布尔条件中有多少个成立——但如果你发现自己在写这种"聪明"的代码，先停下来想一想：是不是有更清晰的写法？代码的可读性通常比简洁性更重要。

## sizeof 揭秘——类型到底占了多少内存

到目前为止我们一直在用 `sizeof`，但还没正式介绍过它。`sizeof` 是 C++ 的一个运算符（不是函数），它在**编译期**就能计算出类型或变量所占的字节数。这意味着它没有任何运行时开销——编译器直接把结果当成常量嵌入到代码里。

```cpp
#include <iostream>

int main()
{
    std::cout << "=== 基本类型 sizeof 汇总 ===" << std::endl;
    std::cout << "bool:          " << sizeof(bool) << " 字节" << std::endl;
    std::cout << "char:          " << sizeof(char) << " 字节" << std::endl;
    std::cout << "short:         " << sizeof(short) << " 字节" << std::endl;
    std::cout << "int:           " << sizeof(int) << " 字节" << std::endl;
    std::cout << "long:          " << sizeof(long) << " 字节" << std::endl;
    std::cout << "long long:     " << sizeof(long long) << " 字节" << std::endl;
    std::cout << "float:         " << sizeof(float) << " 字节" << std::endl;
    std::cout << "double:        " << sizeof(double) << " 字节" << std::endl;
    std::cout << "long double:   " << sizeof(long double) << " 字节"
              << std::endl;

    return 0;
}
```

64 位 Linux 上的典型输出：

```text
=== 基本类型 sizeof 汇总 ===
bool:          1 字节
char:          1 字节
short:         2 字节
int:           4 字节
long:          8 字节
long long:     8 字节
float:         4 字节
double:        8 字节
long double:   16 字节
```

记住这些数字——当然不用死记硬背，随时可以写个小程序测一下。咱们是学程序设计。这就是一个需求——验证我们的类型的大小。不背诵它，而是思考如何完成它！真正需要刻在脑子里的是这个认识：类型的大小不是随心所欲的，它直接影响程序的内存布局和性能。在嵌入式系统上，SRAM 可能只有几十 KB，这时候 `int` 和 `int8_t` 的选择就不是风格偏好的问题了，而是省不省得下的问题。

## 选型的智慧——什么时候用什么类型

讲了这么多类型，到底该怎么选？这里有几条实战经验，不一定覆盖所有场景，但至少能帮你做出八九不离十的判断。

一般用途的整数，用 `int`。它是编译器最"喜欢"的类型——运算速度通常最快，代码生成也最优化。循环变量、数组索引、简单的计数器，统统用 `int` 就好。只有当你确认数据范围会超过 `int` 的极限（大约正负 21 亿），或者需要处理无符号值时，才考虑换用 `long long` 或 `unsigned`。

大小必须确定的场景，用 `<cstdint>` 里的固定宽度类型。解析二进制文件、网络通信协议、操作硬件寄存器、序列化数据结构——凡是"第 N 个字节到第 M 个字节必须是一个多长的整数"这种需求，都应该用 `int32_t`、`uint16_t` 这些类型。不要假设 `int` 一定是 32 位的，虽然今天几乎所有平台都如此，但标准并没有这么保证。

浮点运算用 `double`，除非你有明确的理由选 `float`。`double` 的精度是 `float` 的两倍多，现代 CPU 上两者的运算速度几乎没有差别（都有硬件 FPU 支持）。只有在存储空间极其紧张的场景下——比如嵌入式设备上需要存大量测量数据——才值得牺牲精度去换 `float` 的 4 字节。至于 `long double`，除非你在做极高精度的科学计算，否则基本用不到。

布尔逻辑用 `bool`，不要拿 `int` 当布尔值用。C 语言时代确实有"零是假，非零是真"的习惯（当然，现在C23也有正儿八经的bool了，不知道的朋友去试试！），但在 C++ 里我们有正经的 `bool` 类型，用它能让代码意图更清晰，也能让编译器做更好的类型检查。

## 动手试试

### 练习一：完整的大小和范围报告

写一个程序，打印出所有基本整数类型（`short`、`int`、`long`、`long long` 及其 `unsigned` 版本，加上 `int8_t`、`int16_t`、`int32_t`、`int64_t` 及其 `unsigned` 版本）的 `sizeof` 和通过 `std::numeric_limits` 获取的最小值、最大值。格式化输出，让结果一目了然。

### 练习二：预测 sizeof 的结果

在看答案之前，先预测一下以下表达式在你的平台上的结果，然后写程序验证：`sizeof('A')`、`sizeof(true)`、`sizeof(3.14)`、`sizeof(3.14f)`、`sizeof(3.14L)`。想一想，为什么 `sizeof('A')` 的结果不是 1？提示：C++ 里字符字面量的类型是 `int` 而不是 `char`（这一点和 C 语言不同）。

### 练习三：体验浮点精度陷阱

写一个程序，用 `float` 变量从 0 开始，每次加 0.1，加 10 次，然后判断结果是否等于 1.0。再用 `double` 做同样的事情。观察两者的行为差异，并用 `std::setprecision` 打印出每一步累加后的精确值。

## 小结

这一章我们把 C++ 的基本数据类型从头到尾过了一遍。整数类型有 `short`、`int`、`long`、`long long` 和它们的无符号版本，大小随平台变化；固定宽度类型 `<cstdint>` 解决了跨平台一致性的问题。浮点类型有 `float`、`double`、`long double`，精度逐级递增，但要时刻牢记浮点数是近似表示，不能用 `==` 直接比较。字符类型处在整数和文本的交界处，`char`、`signed char`、`unsigned char` 是三种独立类型。布尔类型虽然简单，但隐式转换规则容易制造隐蔽的 bug。`sizeof` 运算符在编译期计算类型大小，`std::numeric_limits` 提供类型的取值范围。

下一章我们要看看这些类型之间是怎么互相转换的——隐式转换什么时候安全、什么时候危险，`static_cast` 和其他几种转型到底该怎么用。类型转换是 C++ 类型系统里最容易出问题的地方之一，搞清楚了它，我们写代码的时候会安心很多。
