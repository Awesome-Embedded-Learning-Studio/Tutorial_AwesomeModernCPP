---
title: "浮点、字符、const 与类型转换"
description: "掌握 C 语言的浮点类型与精度问题、字符存储与编码、const 限定符和隐式类型转换规则，理解 C++ 类型安全设计的动机"
chapter: 1
order: 3
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
  - 基础
difficulty: beginner
platform: host
reading_time_minutes: 15
cpp_standard: [11]
prerequisites:
  - "数据类型基础：整数与内存"
---

# 浮点、字符、const 与类型转换

上一篇里我们把整数家族从里到外拆了一遍——整型层级、有符号无符号、固定宽度类型和 sizeof。但程序世界里不只有整数：商品价格需要小数，屏幕上的文字需要字符，变量声明后有时候需要保护它不被乱改，不同类型的数据混在一起运算时编译器到底怎么处理。这些就是我们今天要一块一块啃的内容。

说实话，这篇里有些东西——特别是隐式类型转换——初看会觉得很绕。但别担心，这些"坑"恰恰是 C++ 加强类型系统的动机。理解了 C 里面"什么容易出问题"，后面学 C++ 的"怎么解决这些问题"就会顺理成章。

> **学习目标**
> 完成本章后，你将能够：
>
> - [ ] 理解浮点类型的精度特征，避免浮点比较的常见错误
> - [ ] 认识字符类型的本质——它就是小整数
> - [ ] 正确使用 `const` 限定符保护数据
> - [ ] 理解隐式类型转换的规则，避免有符号/无符号混用的陷阱

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c17`

## 第一步——小数怎么存？浮点数的精度世界

### 浮点三兄弟

C 语言提供了三种浮点类型，按精度从小到大排列：

| 类型 | 典型位数 | 有效数字 | 字面量写法 |
|------|---------|---------|-----------|
| `float` | 32 位（单精度） | 约 7 位 | `3.14f` |
| `double` | 64 位（双精度） | 约 15 位 | `3.14`（默认） |
| `long double` | 80 或 128 位 | 平台相关 | `3.14L` |

`double` 是默认的浮点类型——你写 `3.14` 的时候，编译器就把它当作 `double` 处理。如果要用 `float`，记得加 `f` 后缀；要用 `long double`，加 `L` 后缀。

```c
float f = 3.14f;            // 后缀 f 表示 float
double d = 3.14159265359;    // 默认就是 double
long double ld = 3.14L;      // 后缀 L 表示 long double
```

### 浮点数是不精确的——这不是 bug

这是理解浮点数最重要的一个认知：**浮点数是近似值，不是精确值**。原因在于计算机用有限的二进制位来表示十进制小数，就像你用有限的小数位来表示 1/3 一样——永远只能近似。

```c
#include <stdio.h>

int main(void)
{
    float a = 0.1f;
    float b = 0.2f;
    if (a + b == 0.3f) {
        printf("equal\n");
    } else {
        printf("not equal: %.9f\n", a + b);
    }
    return 0;
}
```

来验证一下，编译运行：

```bash
gcc -Wall -Wextra -std=c17 float_demo.c -o float_demo && ./float_demo
```

运行结果：

```text
not equal: 0.300000012
```

看到了吧——`0.1 + 0.2` 在浮点运算中并不等于 `0.3`。这不是编译器的 bug，是 IEEE 754 浮点标准的固有特性。所以，**永远不要用 `==` 比较浮点数**。正确的做法是用一个小的 epsilon 值来判断"近似相等"：

```c
#include <math.h>

/// @brief 判断两个 float 是否近似相等
/// @param a 第一个浮点数
/// @param b 第二个浮点数
/// @return 1 表示近似相等，0 表示不相等
int float_equal(float a, float b)
{
    return fabsf(a - b) < 1e-6f;
}
```

> ⚠️ **踩坑预警**
> 永远不要用 `==` 比较浮点数。`0.1 + 0.2 != 0.3` 在浮点运算中是常态，不是 bug。用 epsilon 判断近似相等才是正确做法。

还有一个细节：写 `float f = 0.1;` 的时候，`0.1` 先被当作 `double` 处理，然后截断为 `float`——这可能会引入额外的精度差异。如果确定要用 `float`，养成加 `f` 后缀的习惯。

### 嵌入式里的浮点

在嵌入式系统上使用浮点运算要格外谨慎。很多微控制器没有硬件浮点单元（FPU），浮点运算靠软件模拟，性能会比整数运算差一个数量级。即使有 FPU，`double` 的运算速度通常也比 `float` 慢不少。所以嵌入式开发中，能用整数解决的问题就别用浮点。

## 第二步——字符就是小整数

### char 的双重身份

C 语言里没有专门的"字符类型"。`char` 这个名字容易让人误解，实际上它就是"最小的可寻址存储单元"，大小恰好是一个字节（1 字节）。只不过我们习惯上用它来存字符的 ASCII 码——而 ASCII 码本身就是 0\~127 的整数。

```c
char ch = 'A';
printf("%c\n", ch);   // 作为字符打印：A
printf("%d\n", ch);   // 作为整数打印：65
```

`'A'` 的 ASCII 码是 65。所以 `'A' + 1` 的结果是 66，对应字符 `'B'`。这个"字符就是整数"的特性在做大小写转换的时候特别方便：

```c
char lower = 'a';
char upper = lower - 32;    // 'a' 的 ASCII 是 97，减 32 得 65 = 'A'
char upper2 = lower - ('a' - 'A');  // 更可读的写法
```

来验证一下：

```bash
gcc -Wall -Wextra -std=c17 char_demo.c -o char_demo && ./char_demo
```

运行结果：

```text
A
65
```

### 字符字面量的类型——C 和 C++ 不一样

这里有一个 C 和 C++ 之间微妙的不兼容区别：在 C 中，字符字面量 `'A'` 的类型是 `int`（占 4 字节），但在 C++ 中它的类型是 `char`（占 1 字节）。

```c
printf("%zu\n", sizeof('A'));  // C: 输出 4，C++: 输出 1
```

这个区别在绝大多数情况下不影响你写代码，但如果你以后从 C 切换到 C++，记住有这回事，免得被 sizeof 的结果吓一跳。

### 编码的世界——ASCII 只是起点

ASCII 用 7 位（0\~127）表示英文字母、数字和常用符号。但世界上不只有英文——中文、日文、emoji 都没法用 ASCII 表示。C 标准后来扩展了对多字节字符和宽字符的支持：

```c
#include <wchar.h>

wchar_t wc = L'中';        // 宽字符，大小由实现定义
char* mb = "你好";          // 多字节字符（UTF-8 编码）
```

`wchar_t` 的问题是它的大小不一致——Windows 上 2 字节，Linux 上 4 字节。这也是为什么很多现代项目直接用 UTF-8 编码的 `char` 数组来处理所有文本。编码是一个很大的话题，这里我们点到为止，知道有这么回事就行。

## 第三步——给变量加把锁：const

### const 的基本用法

`const` 是一个类型限定符，告诉编译器"这个变量的值不应该被修改"。你可以把它理解为给变量加了一把锁——加了锁之后，任何试图修改这个变量的操作都会在编译期被拦下来。

```c
const int kMaxSize = 256;        // 常量，不能修改
const double kPi = 3.14159265;

// kMaxSize = 100;  // 编译错误！不能修改 const 变量
```

这里注意我的用词是"不应该"而不是"不能"——技术上你可以通过指针强制绕过 `const` 修改数据，但那是未定义行为，纯属自找麻烦。

### const 在函数参数中的妙用

`const` 最常见的用途是在函数参数中声明"这个函数不会修改传入的数据"：

```c
/// @brief 计算字符串长度
/// @param str 不可修改的字符串
/// @return 字符串长度
size_t my_strlen(const char* str);

/// @brief 在缓冲区中写入数据
/// @param buf 可修改的缓冲区
/// @param len 缓冲区长度（函数不会修改 len）
void fill_buffer(char* buf, const size_t len);
```

`const char* str` 意思是"str 指向的字符不可修改"，但 str 本身可以指向别的地方。`const size_t len` 意思是"len 的值在函数内部不会被改变"。这些 `const` 不仅是给编译器看的，也是给读代码的人看的——函数签名本身就在传达意图。

> ⚠️ **踩坑预警**
> `const int* p` 和 `int* const p` 是不同的东西。前者表示"指向的值不能改"，后者表示"指针本身不能改"。这个区别在指针那一篇我们会展开讲，目前先知道有这么回事。

### 嵌入式中的 const

在嵌入式开发中，`const` 有一个很实际的好处——编译器可以把 `const` 数据放到 Flash/ROM 而不是 RAM 里。对于 RAM 寸土寸金的微控制器来说，这是很重要的优化。比如查表法中的正弦表：

```c
const uint8_t sine_table[256] = {128, 131, 134, /* ... */};
```

这个数组加了 `const` 之后，编译器就可以把它放进 Flash，不占用宝贵的 RAM。

## 第四步——当不同类型碰在一起：隐式转换

这一节是整篇里最容易让人困惑的部分。先别急，我们一步一步来。

### 整型提升——小类型自动"升级"

在任何算术运算中，`char` 和 `short` 都会先被自动提升为 `int`，然后再参与运算。这是历史遗留的设计——早期 CPU 的运算单元只支持 `int` 宽度的操作，所以编译器自动帮你做了这个转换。

```c
uint8_t a = 200;
uint8_t b = 100;
uint8_t c = a + b;  // 200 + 100 = 300，截断为 44
// 但 a + b 本身的类型是 int（300），不是 uint8_t
```

这里 `a + b` 的结果是 `int` 类型的 300，然后赋值给 `uint8_t` 的时候被截断为 44。整型提升保证了小类型的运算不会在中间步骤溢出，但赋值回小类型的时候仍然可能截断。

### 常用算术转换——两个不同类型怎么办

当两个不同类型的操作数进行运算时，编译器会按一套规则把它们转换成"公共类型"。这套规则看起来挺复杂的，但我们只需要记住最容易踩坑的那一条：**有符号数和无符号数一起运算时，有符号数会被转成无符号数**。

```c
int i = -1;
unsigned int u = 10;
if (i < u) {
    // 你以为 -1 < 10 是 true？
    // 错！i 被转成 unsigned int，变成 UINT_MAX（一个巨大的正数）
    // 所以 UINT_MAX < 10 是 false
    printf("这行不会打印\n");
}
```

> ⚠️ **踩坑预警**
> 有符号数和无符号数比较的时候，有符号数会被隐式转成无符号数。`-1 < 10u` 在 C 中的结果是 false。这种 bug 特别阴险，因为编译器可能根本不警告你。涉及 `size_t`（无符号）和 `int`（有符号）的混合比较中尤其常见。

我们的建议很简单：**尽量避免有符号和无符号的混用**。如果一定要混用，显式写类型转换，把意图表达清楚：

```c
int count = -1;
size_t len = 5;
if (count < (int)len) {  // 显式转换，意图清楚
    // ...
}
```

### 显式类型转换

C 语言的显式转换就是 C 风格的 cast：`(type)value`。它简单粗暴，什么都能转，而且不做任何检查：

```c
double pi = 3.14159;
int i = (int)pi;              // 截断为 3
unsigned int u = (unsigned int)-1;  // 变成 UINT_MAX
```

C 风格 cast 的问题在于太"万能"了——`const` 可以被 cast 掉、指针类型可以随意转换、数据布局的假设完全没有验证。这也是为什么 C++ 引入了命名的 cast 操作符（`static_cast`、`const_cast`、`reinterpret_cast`、`dynamic_cast`），让每一种转换的意图都一目了然。

## C++ 衔接

C++ 在类型系统上做了大量的安全加固，很多改进直接瞄准了 C 的痛点：

- `{}` 初始化禁止窄化转换（上一篇已经提过）
- 命名的 cast 操作符让类型转换的意图更明确
- `constexpr` 在 `const` 的基础上保证编译期求值
- `char16_t`、`char32_t`、`char8_t` 解决了编码的类型安全问题
- `std::numeric_limits<T>::epsilon()` 提供了比手写 epsilon 更精确的浮点比较工具

这些改进的动机全都来自我们今天讨论的这些"坑"。理解了 C 中"什么容易出问题"，学 C++ 的"怎么解决这些问题"就会非常自然。

## 小结

我们来梳理一下这篇的核心要点。浮点数是近似值，`0.1 + 0.2 != 0.3` 是 IEEE 754 的固有特性，比较浮点数要用 epsilon 而不是 `==`。`char` 本质上就是小整数，它的符号性取决于平台。`const` 给变量加了一把编译期保护的锁，在嵌入式场景中还能帮助编译器把数据放进 Flash。隐式类型转换——特别是有符号和无符号混用——是 bug 的高发地带，混用时务必显式写 cast。

到这里我们已经把 C 语言数据类型的基础打好了。接下来我们要进入运算符的世界，看看这些数据怎么进行各种运算。

## 练习

### 练习 1：浮点精度侦探

预测以下代码的输出，然后编译运行验证你的预测：

```c
#include <stdio.h>

int main(void)
{
    float a = 0.1f;
    float b = 0.2f;
    float c = 0.3f;

    printf("a + b == c? %s\n", (a + b == c) ? "yes" : "no");
    printf("a + b     = %.20f\n", a + b);
    printf("c         = %.20f\n", c);
    return 0;
}
```

修改代码使用 epsilon 比较来得到正确的结果。

### 练习 2：隐式转换陷阱

下面这段代码有一个隐藏的 bug，找出它并解释原因：

```c
int values[] = {1, 2, 3, 4, 5};
int target = -1;

// bug 就在下面这行
if (target < sizeof(values) / sizeof(values[0])) {
    printf("target is in range\n");
}
```

提示：`sizeof` 返回的是什么类型？

### 练习 3：const 实战

写一个函数，接收一个字符串，统计其中某个字符出现的次数。函数签名中正确使用 `const`：

```c
/// @brief 统计字符 ch 在字符串 str 中出现的次数
/// @param str 不可修改的字符串
/// @param ch 要查找的字符
/// @return 出现次数
size_t count_char(const char* str, char ch);
```

## 参考资源

- [cppreference: C 语言隐式转换](https://en.cppreference.com/w/c/language/conversion)
- [What Every Programmer Should Know About Floating-Point Arithmetic](https://floating-point-gui.de/)
- [IEEE 754 浮点标准](https://en.wikipedia.org/wiki/IEEE_754)
