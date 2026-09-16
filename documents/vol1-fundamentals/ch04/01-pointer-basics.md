---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 从零开始理解指针：取地址、解引用、指针类型与空指针，掌握 C++ 内存访问的核心机制
difficulty: beginner
order: 1
platform: host
prerequisites:
- inline 与 constexpr 函数
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 指针基础
---
# 指针是什么：一个变量，装着另一个变量的地址

指针大概是 C++ 里名声最响、也最容易劝退新手的特性了。如果您之前接触过 Python 或 Java，很可能习惯了"变量就是对象本身"的思维：变量里存的就是数据，拿来用就好。但 C++ 不一样，它给了咱们直接操作内存地址的能力，而指针就是这个能力的入口。

很多朋友一听到"指针"两个字就开始紧张。但实际上**指针就是一个存储内存地址的变量，仅此而已。** 每个变量都占着内存里的某个位置，这个位置有一个编号（地址），指针就是用来记录和操作这些编号的。咱们把取地址、解引用、指针类型、空指针这几个基础拿下，后面的指针算术、数组、动态内存管理才有地基。

## 先搞懂"地址"：每个变量在内存里的位置

咱们可以把内存看作一排按字节编号的存储单元，每个变量占用其中若干连续的字节；变量的地址，就是它占用的这段空间的起始编号。用 `&`（取地址）运算符可以获取一个变量的地址：

```cpp
// address_demo.cpp
#include <iostream>

int main()
{
    int x = 42;
    std::cout << "x 的值:   " << x << std::endl;
    std::cout << "x 的地址: " << &x << std::endl;
    return 0;
}
```

```bash
g++ -std=c++17 -Wall -Wextra -o address_demo address_demo.cpp && ./address_demo
```

输出大概是：

```text
x 的值:   42
x 的地址: 0x7ffd4a3b2c5c
```

这样读这个内容哈。X是多少呢？42！X在哪里呢？第0x7ffd4a3b2c5c字节处！

`0x` 开头的十六进制数字就是 `x` 在内存中的地址。每次运行地址可能不同，但有一点确定：**每个变量都有唯一的地址，`&` 就是获取它的运算符**。咱们多声明几个变量并打印地址，会发现相邻 `int` 的地址相差 4，正好是一个 `int` 占的字节数。

## 指针变量，就是我们常说的指针

既然地址就是一个数字，那咱们自然可以用变量来存储它。这就是**指针**：一个存储内存地址的变量。

```cpp
int x = 42;
int* p = &x;   // p 存储 x 的地址
```

声明中的 `*` 表示"这是一个指针"，`int*` 读作"指向 int 的指针"。`p` 本身是个变量、占自己的内存，它存的值是 `&x` 这个地址；咱们顺着这个地址去取，取到的就是 `x` 的值 42。

咱们来验证指针和原始变量的关系：

```cpp
int x = 42;
int* p = &x;

std::cout << "x 的值:   " << x << std::endl;   // 42
std::cout << "&x 的值:  " << &x << std::endl;   // 0x7ffd...
std::cout << "p 的值:   " << p << std::endl;     // 和 &x 一样
std::cout << "&p 的值:  " << &p << std::endl;    // 不同的地址
```

咱们看到 `p` 的值和 `&x` 完全一样——它确实存了 `x` 的地址。而 `p` 自己也有地址（`&p`），因为指针本身也是变量，也需要占内存。

`int* p1, p2;` 的结果是 `p1` 是 `int*` 而 `p2` 是 `int`，`*` 只修饰紧跟它的变量。咱们想声明两个指针必须写 `int *p1, *p2;`。最佳实践是一行只声明一个指针。

## 知道地了，拿到东西！这就是解引用

`*` 在声明里表示"这是指针"，在表达式里表示"顺着这个地址去拿数据"，上下文不同，含义不同。咱们通过 `*p` 可以读取甚至修改指针指向的变量：

```cpp
int x = 42;
int* p = &x;

std::cout << *p << std::endl;  // 42，读取
*p = 100;                       // 通过指针修改 x
std::cout << x << std::endl;   // 100
```

咱们没有直接写 `x = 100`，而是通过指针间接修改了 `x`。这就是指针的核心能力：**间接访问**。`&`（取地址）和 `*`（解引用）是一对互逆操作：`*&x` 就是 `x`，`&*p` 就是 `p`。

## 指针类型——为什么 `int*` 和 `double*` 不是一回事

地址确实只是一个数字，但类型信息会告诉编译器"这个地址上存的是什么类型的数据"：读取时操作多少字节、怎么解释二进制内容——这也是咱们接下来要看的东西。

```cpp
// pointer_types.cpp
#include <iostream>

int main()
{
    int    i = 42;
    double d = 3.14;
    char   c = 'A';

    std::cout << "*(&i) = " << *(&i) << std::endl;  // 42
    std::cout << "*(&d) = " << *(&d) << std::endl;  // 3.14
    std::cout << "*(&c) = " << *(&c) << std::endl;  // A

    std::cout << "sizeof(int*):    " << sizeof(int*) << std::endl;    // 8
    std::cout << "sizeof(double*): " << sizeof(double*) << std::endl; // 8
    std::cout << "sizeof(char*):   " << sizeof(char*) << std::endl;   // 8
    return 0;
}
```

咱们拿到两个结论：不同类型的指针解引用后得到的值类型不同，因为编译器根据指针类型来解释二进制数据；但不管指向什么类型，指针本身在 64 位系统上都是 8 字节，地址就是地址，记录一个编号而已。

`int* p = &d;`（把 `double` 的地址赋给 `int*`）会直接编译错误，这是编译器在保护咱们。咱们要是用 C 风格强转绕过，写 `int* p = (int*)&d;`，那 `*p` 读出来就是一团毫无意义的数字。

## 空指针：什么也不指向

有时候咱们需要一个指针但暂时不知道该指向哪里，或者函数查找失败时需要返回"没找到"的信号。这就需要**空指针**：明确表示"什么也不指向"的指针。C++98 和 C 里咱们用的是 `NULL`——翻过 `stdlib.h` 的朋友知道，C 里它就是 `(void*)0`，C++98 里则是整数 `0`，两种形态都留了歧义的口子。C++11 引入的 `nullptr` 是现代 C++ 中表示空指针的唯一正确方式：

```cpp
int* p = nullptr;  // 不指向任何有效地址

if (p != nullptr) { // 也有朋友喜欢if(p)，这个是习惯，笔者只有在非常需要强调兄弟们这不是空指针的时候这样写。
    std::cout << *p << std::endl;
} else {
    std::cout << "p 是空指针，不能解引用" << std::endl;
}
```

解引用空指针是**未定义行为**（Undefined Behavior）。程序可能直接崩溃（Segmentation Fault），可能输出垃圾，也可能看起来"正常"但数据已被破坏。语法完全合法，编译器不会帮咱们拦，所以养成习惯：**解引用之前先判空**。

老代码里可能看到 `NULL` 或 `0`，但 `nullptr` 有一个关键优势：它的类型是 `std::nullptr_t`，不会和整数混淆，在函数重载时不会导致错误匹配。咱们一律用 `nullptr`，把 `NULL` 留给历史。

## 指针与 const——温故知新

前面章节咱们学过 `const` 和指针的三种组合，这里快速回顾：

`const int* p`：指向常量的指针，咱们不能通过 `p` 改数据，但可以改指向。

```cpp
int x = 10, y = 20;
const int* p = &x;
// *p = 100;  // 编译错误
p = &y;       // 没问题
```

`int* const p`：常量指针，咱们不能改它的指向，但可以改数据。

```cpp
int x = 10;
int* const p = &x;
*p = 100;      // 没问题
// p = &y;     // 编译错误
```

`const int* const p`：双重 const，都不能改。咱们阅读时从右往左读，`const int* const p` 读作"p 是一个 const 指针，指向 const int"。

## 那些常见的坑

指针的强大伴随着危险。下面几个陷阱初学者几乎必踩，咱们提前认识，能省下大量调试时间。具体的，咱们的[《崩溃实验室》](/crash-lab/)这个栏目，会带着大家好好玩耍的（斜眼笑。

| 坑 | 怎么发生的 | 有多糟 | 怎么防 |
| --- | --- | --- | --- |
| **未初始化的指针** | 声明指针但不赋值，里面就是垃圾地址 | 解引用是未定义行为，甚至可能比空指针更糟：空指针至少会立即崩溃，垃圾地址可能指向有效区域，数据被悄悄篡改 | 声明指针时立即初始化，咱们哪怕暂时不知道指向哪里，也先赋 `nullptr` |
| **返回局部变量的地址** | 局部变量分配在栈上，函数返回后栈空间被回收 | 调用者拿到**悬空指针**（dangling pointer）：地址还在，数据已不可靠 | 别返回局部变量的地址；编译器加 `-Wall` 会给出 `warning: address of local variable 'local' returned`，务必认真对待 |
| **重复释放与使用后释放** | 通过 `new` 分配的内存没有被 `delete` 恰好一次 | 释放两次（double free）或释放后继续使用（use after free）都是严重的未定义行为 | 牢记"恰好一次"的配对原则；细节属于动态内存管理的范畴，咱们后面会详细讲 |

上面三个坑有一个共同的根源：指针给了咱们直接操作内存的能力，但编译器无法在所有场景下帮咱们检查使用是否正确。所以指针相关的问题往往在运行时才暴露，而且症状可能很不稳定（有时候跑得好好的，换个编译选项就崩了）。养成良好的指针使用习惯，比出了问题再排查高效得多。

## 综合实战——pointers.cpp

咱们把所有内容串在一起：

```cpp
// pointers.cpp —— 指针基础操作综合演示
#include <iostream>

/// @brief 通过指针交换两个变量的值
void swap_by_pointer(int* a, int* b)
{
    if (a == nullptr || b == nullptr) {
        return;
    }
    int temp = *a;
    *a = *b;
    *b = temp;
}

/// @brief 安全打印指针指向的值
void safe_print(const char* label, const int* p)
{
    std::cout << label;
    if (p != nullptr) {
        std::cout << *p << " (地址: " << p << ")" << std::endl;
    }
    else {
        std::cout << "(空指针)" << std::endl;
    }
}

int main()
{
    // 取地址与解引用
    int x = 42;
    int* p = &x;
    std::cout << "=== 取地址与解引用 ===" << std::endl;
    std::cout << "x = " << x << ", &x = " << &x << std::endl;
    std::cout << "p = " << p << ", *p = " << *p << std::endl;

    // 通过指针修改
    *p = 100;
    std::cout << "\n=== *p = 100 后 ===" << std::endl;
    std::cout << "x = " << x << std::endl;

    // 指针 swap
    int a = 10, b = 20;
    std::cout << "\n=== swap ===" << std::endl;
    std::cout << "交换前: a=" << a << ", b=" << b << std::endl;
    swap_by_pointer(&a, &b);
    std::cout << "交换后: a=" << a << ", b=" << b << std::endl;

    // 空指针检查
    std::cout << "\n=== 空指针 ===" << std::endl;
    int value = 99;
    safe_print("有效指针: ", &value);
    safe_print("空指针:   ", static_cast<int*>(nullptr));

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o pointers pointers.cpp && ./pointers
```

预期输出：

```text
=== 取地址与解引用 ===
x = 42, &x = 0x7ffd4a3b2c5c
p = 0x7ffd4a3b2c5c, *p = 42

=== *p = 100 后 ===
x = 100

=== swap ===
交换前: a=10, b=20
交换后: a=20, b=10

=== 空指针 ===
有效指针: 99 (地址: 0x7ffd4a3b2c4c)
空指针:   (空指针)
```

地址每次运行可能不同，但 `p` 的值始终和 `&x` 一致，swap 后值互换了，空指针被正确处理。建议您拷到本地编译运行，自己观察地址变化。

## 在线运行

您也可以在线运行指针基础综合示例，观察取地址、解引用、指针 swap 和空指针检查：

<OnlineCompilerDemo
  title="指针基础综合演练：取地址、解引用、swap、空指针"
  source-path="code/examples/vol1/10_pointer_basics.cpp"
  description="在线运行并观察指针的基本操作。试着修改指针指向的值，观察原变量的变化。"
  allow-run
/>

## 动手试试

### 练习一：手写 swap 并观察地址

请您声明两个 `int` 变量 `a` 和 `b`，打印值和地址，通过指针交换值后再次打印。值变了，地址变了吗？为什么？

::: details 参考答案

```cpp
#include <iostream>

void swap(int* p1, int* p2)
{
    int temp = *p1;
    *p1 = *p2;
    *p2 = temp;
}

int main()
{
    int a = 10;
    int b = 20;
    int* p1 = &a;
    int* p2 = &b;

    std::cout << "========== 交换前 ==========" << std::endl;
    std::cout << "变量a的值: " << a << std::endl;
    std::cout << "变量a的地址: " << p1 << std::endl;
    std::cout << "变量b的值: " << b << std::endl;
    std::cout << "变量b的地址: " << p2 << std::endl;

    swap(p1, p2);

    std::cout << "\n========== 交换后 ==========" << std::endl;
    std::cout << "变量a的值: " << a << std::endl;
    std::cout << "变量a的地址: " << p1 << std::endl;
    std::cout << "变量b的值: " << b << std::endl;
    std::cout << "变量b的地址: " << p2 << std::endl;

    return 0;
}
```

编译运行:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

运行结果:

```text
========== 交换前 ==========
变量a的值: 10
变量a的地址: 0x7ffe15dc91b0
变量b的值: 20
变量b的地址: 0x7ffe15dc91b4

========== 交换后 ==========
变量a的值: 20
变量a的地址: 0x7ffe15dc91b0
变量b的值: 10
变量b的地址: 0x7ffe15dc91b4
```

**关键观察：值变了，地址没变**

咱们仔细对比输出会发现：`a` 的值从 10 变成了 20，但地址始终是 `0x7ffe15dc91b0`；`b` 的值从 20 变成了 10，地址也始终是 `0x7ffe15dc91b4`。

**为什么地址不会变？**

咱们看 `int a = 10;`：它在栈上分配空间时，操作系统和编译器协作为 `a` 分配了一块 4 字节的内存（假设 `int` 是 4 字节），这块内存的起始位置就是 `a` 的地址。这个地址在变量的整个生命周期内都不会改变。

咱们再看 `swap`：它通过指针交换的是**变量的内容**（值），变量在内存中的位置从头到尾没动——`0x7ffe15dc91b0` 这段内存在 `a` 的整个生命周期里都属于 `a`，只是里面存的值从 10 换成了 20。

这也是指针强大之处的体现：通过地址，咱们可以跨越函数边界去修改原始变量的内容，而不需要拷贝整个变量或改变它的内存布局。

:::

### 练习二：追踪指针的值

请您先不运行，在纸上追踪结果，再编译验证：

```cpp
#include <iostream>

int main()
{
    int x = 10, y = 20;
    int* p = &x;
    int* q = &y;
    *p = *q;   // 把 q 指向的值赋给 p 指向的位置
    p = q;     // 让 p 和 q 指向同一个地方
    *p = 30;
    std::cout << "x = " << x << std::endl;
    std::cout << "y = " << y << std::endl;
    std::cout << "*p = " << *p << std::endl;
    std::cout << "*q = " << *q << std::endl;
    return 0;
}
```

很多朋友第一次做时会栽在 `*p = *q` 和 `p = q` 的区别上——前者是赋值数据，后者是改变指向，咱们值得在这儿多花一分钟。

::: details 参考答案

编译运行:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

运行结果:

```text
x = 20
y = 30
*p = 30
*q = 30
```

:::

### 练习三：修复空指针 bug

下面代码有两个指针相关的 bug，请您找出并修复：

```cpp
#include <iostream>

int* create_value()
{
    int val = 42;
    return &val;
}

int main()
{
    int* p;  // bug 1
    std::cout << *p << std::endl;

    int* q = create_value();  // bug 2
    std::cout << *q << std::endl;

    return 0;
}
```

::: details 参考答案

```cpp
#include <iostream>

int *create_value()
{
    static int val = 42;
    return &val;
}

int main()
{
    int *p = nullptr; // bug 1
    int value = 10;
    p = &value;
    std::cout << "*p的值是: " << *p << std::endl;

    int *q = create_value(); // bug 2
    std::cout << "*q的值是: " << *q << std::endl;

    return 0;
}
```

编译运行:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

运行结果:

```text
*p的值是: 10
*q的值是: 42
```

:::
