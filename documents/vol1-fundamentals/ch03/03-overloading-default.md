---
title: "重载与默认参数"
description: "掌握函数重载的规则和默认参数的用法，理解重载决议机制，避免两者的常见冲突"
chapter: 3
order: 3
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "参数传递方式"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 重载与默认参数

上一章我们搞清楚了参数传递的几种方式——值传递、指针传递、引用传递。现在问题来了：假设我们要写一个 `print` 函数，打印整数、打印浮点数、打印字符串，这三件事本质上都是"打印"，但 C 语言的规矩是每个函数必须有一个独一无二的名字。于是你就得写 `print_int()`、`print_float()`、`print_string()`——光是起名字就够让人崩溃的，调用的时候还得自己判断该用哪个。

C++ 说：同一个概念，不需要不同的名字。**函数重载**让同名函数根据参数的不同表现出不同的行为，**默认参数**则让那些"几乎每次都传一样值"的参数彻底透明。这两个特性是设计好接口的基本功，这一章我们就来把它们彻底搞清楚。

## 第一步——认识函数重载

函数重载的核心规则非常简单：多个函数可以共享同一个名字，只要它们的**参数列表**不同——参数的类型不同，或者参数的数量不同。注意，返回类型不在考量范围内——编译器不会仅凭返回类型来区分重载。这一点很多新手会搞混，觉得"返回 `int` 和返回 `double` 总该算不同的函数了吧"，还真不算，因为调用点可能完全忽略返回值，编译器在那个上下文里根本看不到返回类型。

来看最基本的例子：

```cpp
#include <cstdio>

void print(int value)
{
    std::printf("Integer: %d\n", value);
}

void print(double value)
{
    std::printf("Double: %f\n", value);
}

void print(const char* str)
{
    std::printf("String: %s\n", str);
}
```

调用的时候，编译器根据实参的类型自动选择对应的版本：

```cpp
print(42);       // 调用 print(int)
print(3.14);     // 调用 print(double)
print("Hello");  // 调用 print(const char*)
```

在 C 里要实现同样的效果，三个函数三个名字，每次调用还要自己想该用哪个。对比之下，重载在 API 设计层面的优势是显而易见的——调用者只需要记住一个名字就够了。

参数数量不同也可以构成重载。这种模式在实际工程中非常常见——外设初始化函数往往需要提供"推荐配置"和"完全自定义"两种入口：

```cpp
void init_uart(int baudrate)
{
    // 使用默认配置：8 数据位、1 停止位、无校验
}

void init_uart(int baudrate, int databits, int stopbits, char parity)
{
    // 使用自定义配置
}
```

## 第二步——理解重载决议

表面上看，调用一个重载函数只是"写个名字、传个参数"这么简单的事。但实际上，编译器在背后执行了一套非常严格的决策流程——**重载决议 (Overload Resolution)**。每当调用一个存在多个重载版本的函数时，编译器会收集所有名字匹配的候选函数，然后逐一评估：**哪一个是"最合适"的？**需要强调的是，编译器不会理解你的业务语义，它只会机械地按照语言规则打分，选出匹配度最高的版本。

在不涉及模板的情况下，编译器的判断标准可以理解为一条由强到弱的"匹配优先级链"。最顶层是**精确匹配**——实参与形参类型完全一致；如果找不到精确匹配，才会考虑**类型提升**，比如 `char` 提升为 `int`、`float` 提升为 `double`；再往后是**标准类型转换**，例如 `int` 转换为 `double`；最后才轮到用户自定义的类型转换。这个顺序非常关键——只要某一层级已经能找到可行的匹配，后面的规则就完全不会被考虑。

我们用最常见的例子来演示。假设同时定义了 `process(int)` 和 `process(double)`：

```cpp
void process(int x) { /* ... */ }
void process(double x) { /* ... */ }
```

调用 `process(5)` 时，字面量 `5` 本身就是 `int`，属于精确匹配 `process(int)`，而 `process(double)` 需要一次从 `int` 到 `double` 的转换。精确匹配对任何形式的转换都有压倒性优势，最终调用的一定是 `process(int)`。反过来，`process(5.0)` 中的 `5.0` 是 `double`，这次精确匹配发生在 `process(double)` 上。

稍微容易让人困惑的是 `process(5.0f)` 这种情况。`5.0f` 的类型是 `float`，而我们并没有 `process(float)` 的重载。此时编译器会比较两条可能的路径：`float` 提升为 `double`，以及 `float` 转换为 `int`。前者是浮点类型之间的标准提升，被认为更加自然、安全；后者则涉及截断语义，优先级更低。所以最终仍然会调用 `process(double)`。这也体现了一个事实：**重载决议不是"最少字符匹配"，而是"最合理的类型路径匹配"**。

真正让人头疼的情况，往往出现在规则无法分出高下的时候。比如同时存在 `func(int, double)` 和 `func(double, int)`，当你调用 `func(5, 5)` 时，两个候选函数的匹配成本完全一样——对于第一个版本，一个参数是精确匹配、另一个需要标准转换；对于第二个版本，情况正好对称。编译器不会试图揣测你的意图，直接判定调用存在歧义，以编译错误终止。

> ⚠️ **踩坑预警**
> 重载歧义不会总是像上面那个例子一样显而易见。当你定义了多个重载版本，且参数之间存在隐式转换关系时（比如 `int` 和 `long`、`float` 和 `double`），歧义可能在你意想不到的地方冒出来。最可靠的做法是：**在设计接口时，避免仅靠参数顺序或微妙的类型差异来区分重载**。一旦出现歧义，把类型写清楚，或者干脆用不同的函数名。

这背后反映的是 C++ 一个非常重要的设计理念：只要存在同样可行、但无法比较优劣的选择，编译器宁可拒绝编译，也不会替程序员做决定。这也是 C++ 强类型系统的底色——明确性永远高于便利性。

## 第三步——掌握默认参数

在真实工程中，函数参数并不是"越多越好"。很多时候，一个函数的参数里总会混着几类角色：核心必选参数，每次调用都不同；高频但几乎不变的配置，绝大多数场景下取固定值；以及只有极少数场景才会调整的高级选项。如果每次调用都被迫把参数一个不落地写出来，不仅代码冗长，而且会迅速掩盖真正重要的信息。

默认参数正是为了解决这个问题而存在的——**那些你已经决定好"默认行为"的参数，就干脆别让调用者操心**。

```cpp
void configure_uart(int baudrate,
                    int databits = 8,
                    int stopbits = 1,
                    char parity = 'N')
{
    // 配置 UART
}
```

最常见的调用形式只剩下真正关心的那一个参数：

```cpp
configure_uart(115200);              // 只指定波特率，其余全部默认
configure_uart(115200, 8);           // 只改数据位
configure_uart(115200, 8, 2);        // 改数据位和停止位
configure_uart(115200, 8, 2, 'E');   // 全部自定义
```

从接口设计的角度看，这是一种非常温和的向前兼容手段：你可以不断在函数右侧追加新的可选能力，而不会破坏已有代码。

默认参数的语法看似简单，但规则其实非常严格，踩坑的人不在少数。

**规则一：默认参数必须从右向左连续出现。** 编译器在处理函数调用时，只能通过"省略尾部参数"的方式来判断哪些值使用默认值。你不能跳过中间的参数——如果要给第三个参数传值，前面的所有参数都必须显式给出。所以，设计函数签名时参数的排列顺序非常重要：**把最常需要自定义的参数放在最左边，把几乎不会变的参数放在最右边**。

```cpp
// 正确：默认参数从右向左连续
void init_spi(int freq, int mode = 0, int bits = 8);

// 错误：非默认参数不能出现在默认参数后面
// void bad_init(int freq = 1000000, int mode, int bits);  // 编译错误
```

**规则二：默认参数只能被指定一次，而且应该放在声明处。** 这一点在头文件与源文件分离的工程中尤为重要。默认值是接口的一部分，而不是实现细节——如果你在 `.cpp` 里又写了一遍默认参数，编译器会认为你在试图重新定义规则，直接报错。

```cpp
// uart.h —— 声明时指定默认参数
void configure_uart(int baudrate, int databits = 8, int stopbits = 1);

// uart.cpp —— 定义时不要重复默认参数
void configure_uart(int baudrate, int databits, int stopbits)
{
    // 实现
}
```

> ⚠️ **踩坑预警**
> 在声明处写了默认值、定义处又写一遍——这个错误在新手中非常常见，而且报错信息有时候并不那么直观，定位起来还挺费劲的。记住：**默认参数写在声明里，定义里不写**。

## 第四步——重载还是默认参数，怎么选

函数重载和默认参数都能让接口更灵活，但它们的适用场景并不完全重叠。选择用哪一个，取决于你面对的具体问题。

当你需要**处理不同类型的参数**时，函数重载是唯一的选择——默认参数做不到这一点。`print(int)` 和 `print(const char*)`，参数类型完全不同，行为也不同，这只能用重载来实现。

当你需要**减少参数数量、提供默认行为**时，默认参数是更简洁的选择。`configure_uart(115200)` 和 `configure_uart(115200, 8, 2, 'E')` 做的是同一件事，只是详细程度不同，用默认参数最自然。

但最需要警惕的情况是**两者混用**。函数重载和默认参数如果设计不当，会产生非常棘手的歧义问题。看下面这个经典的反面教材：

```cpp
void process(int value)
{
    std::printf("Single: %d\n", value);
}

void process(int value, int factor = 2)
{
    std::printf("Scaled: %d\n", value * factor);
}

process(10);  // 歧义！调用第一个？还是第二个（使用默认参数）？
```

编译器在面对 `process(10)` 时发现两个版本都能匹配——第一个是精确匹配，第二个也是精确匹配（只是第二个参数用了默认值）。两边代价一模一样，编译器无法做出选择，直接报歧义错误。

> ⚠️ **踩坑预警**
> 重载和默认参数在同一接口上重叠，几乎是必定出问题的组合。笔者的建议是：对于同一个函数名，要么只用重载（多个版本参数类型不同），要么只用默认参数（一个版本部分参数有默认值），但不要两者混搭。如果你确实需要同时支持"不同类型"和"不同参数数量"，考虑把不同类型的处理逻辑封装成不同的函数名——这虽然看起来不如重载"优雅"，但至少不会产生歧义。

## 实战演练——overload.cpp

我们把前面的用法整合到一个完整的程序里，演示多个 `print` 重载、默认参数的实际应用，以及一个刻意制造的歧义错误和修复方式：

```cpp
// overload.cpp
// Platform: host
// Standard: C++17

#include <cstdint>
#include <cstdio>
#include <cstring>

// ---- 多个 print 重载 ----

void print(int value)
{
    std::printf("int:    %d\n", value);
}

void print(double value)
{
    std::printf("double: %.2f\n", value);
}

void print(const char* str)
{
    std::printf("string: %s\n", str);
}

// ---- 默认参数示例 ----

void draw_rect(int width, int height, bool fill = false,
               char brush = '#')
{
    std::printf("绘制矩形 %dx%d, fill=%s, brush='%c'\n",
                width, height,
                fill ? "true" : "false",
                brush);
}

// ---- 修复歧义：用不同的函数名替代混搭 ----

void scale_value(int value)
{
    std::printf("原始值: %d\n", value);
}

void scale_value(int value, int factor)
{
    std::printf("缩放后: %d (factor=%d)\n", value * factor, factor);
}

int main()
{
    // 演示重载
    std::printf("=== 函数重载 ===\n");
    print(42);
    print(3.14159);
    print("Hello, overloading!");

    // 演示默认参数
    std::printf("\n=== 默认参数 ===\n");
    draw_rect(10, 5);                  // fill=false, brush='#'
    draw_rect(10, 5, true);            // fill=true,  brush='#'
    draw_rect(10, 5, true, '*');       // 全部自定义

    // 演示修复后的"重载 + 不同参数数量"
    std::printf("\n=== 不同参数数量 ===\n");
    scale_value(7);
    scale_value(7, 3);

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o overload overload.cpp
./overload
```

运行结果：

```text
=== 函数重载 ===
int:    42
double: 3.14
string: Hello, overloading!

=== 默认参数 ===
绘制矩形 10x5, fill=false, brush='#'
绘制矩形 10x5, fill=true, brush='#'
绘制矩形 10x5, fill=true, brush='*'

=== 不同参数数量 ===
原始值: 7
缩放后: 21 (factor=3)
```

如果你把前面那个歧义示例中的 `process(int)` 和 `process(int, int = 2)` 同时定义，然后调用 `process(10)`，编译器会直接报错：

```text
overload.cpp:xx:xx: error: call of overloaded 'process(int)' is ambiguous
```

解决方案就是我们演示的做法——把两个版本拆成不同函数名，或者去掉其中一个重载改用默认参数（只保留一个版本），让调用点的语义不再含糊。

## 动手试试

### 练习一：max 的重载家族

写一组重载函数 `max_value`，分别接受两个 `int`、两个 `double`、两个 `const char*`（比较字典序，返回较大的那个指针）。在 `main` 里分别调用它们，打印结果。

```text
max_value(3, 7)         -> 7
max_value(2.5, 1.8)     -> 2.5
max_value("apple", "banana") -> banana
```

### 练习二：带默认参数的日志函数

写一个 `log_message` 函数，签名为 `void log_message(const char* text, const char* level = "INFO", bool show_timestamp = false)`。分别用不同的参数组合调用它，观察默认参数的行为。

### 练习三：能编译还是歧义

下面这段代码能编译通过吗？如果可以，会调用哪个 `func`？先想清楚再上机验证：

```cpp
void func(int x) { }
void func(short x) { }

int main()
{
    func('A');  // 歧义？还是能编译？
    return 0;
}
```

提示：`'A'` 的类型是 `char`。`char` → `int` 和 `char` → `short` 分别属于什么转换级别？整型提升（promotion）和整型转换（conversion）在重载决议中的优先级一样吗？

## 小结

这一章我们学习了 C++ 在函数接口设计上的两个重要工具。函数重载允许同名函数根据参数类型和数量的不同表现出不同的行为，编译器通过一套严格的重载决议规则来决定最终调用哪个版本——精确匹配优先于类型提升，类型提升优先于标准转换，当两个候选函数无法分出高下时编译器直接报歧义错误。默认参数让调用者可以省略那些"几乎总是同一个值"的尾部参数，规则是默认值只能从右向左连续出现，且只在声明处指定一次。两者各有擅长的领域——重载处理"类型不同"，默认参数处理"参数可选"——但混用时极易产生歧义，需要格外谨慎。

下一章我们来看 inline 和 constexpr 函数——当函数调用的开销本身成了问题，C++ 给了我们什么手段来消除它。
