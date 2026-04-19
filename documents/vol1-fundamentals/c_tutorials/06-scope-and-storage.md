---
chapter: 1
cpp_standard:
- 11
description: 深入理解 C 语言的作用域规则、存储类别和链接性，掌握 static 的三种用法
difficulty: beginner
order: 8
platform: host
prerequisites:
- 控制流：让程序学会选择和重复
reading_time_minutes: 24
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 作用域与存储类别
---
# 作用域与存储类别

如果你写过超过两个源文件的项目，大概率已经踩过这样的坑：两个文件里都定义了一个叫 `count` 的全局变量，编译的时候链接器一脸懵逼地告诉你 `multiple definition`。或者更隐蔽的情况——你在某个 `.c` 文件里定义了一个辅助函数，结果别的文件不小心也调用了它，后来你改了那个函数的实现，调用方毫无预警地崩了。

这些问题的根源都在于**作用域**和**存储类别**。前者决定了一个名字在程序的哪些部分可以被使用，后者决定了这个名字对应的实体在内存中活了多久、被谁看见。这两个概念交织在一起，加上 `static` 这个关键字在 C 语言里身兼数职，初学者很容易搞混。

我们今天就把这团乱麻理清楚——从最基本的作用域规则开始，一路讲到存储类别、链接性、生命周期，最后看看 `static` 的三种截然不同的用法到底是什么。理解了这些，你在多文件项目中组织代码的时候就不会再凭感觉了。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 说出 C 语言的四种作用域及其区别
> - [ ] 解释 `auto`、`static`、`extern`、`register` 的含义
> - [ ] 理解链接性（内部/外部/无）如何控制符号可见性
> - [ ] 正确使用 `static` 的三种语义
> - [ ] 用 `extern` 和 `static` 组织多文件项目的符号

## 环境说明

我们用 GCC 12+ 或 Clang 15+，在 Linux 或 WSL2 上编译。所有示例都可以用一条简单的命令编译运行：

```bash
gcc -Wall -Wextra -std=c11 -o scope_demo scope_demo.c && ./scope_demo
```

多文件项目则需要分别编译再链接，或者直接一把梭：

```bash
gcc -Wall -Wextra -std=c11 -o multi_file_demo file1.c file2.c && ./multi_file_demo
```

## 第一步——搞清楚四种作用域

C 语言标准定义了四种作用域：块作用域（block scope）、文件作用域（file scope）、函数作用域（function scope）和函数原型作用域（function prototype scope）。我们一个一个来。

### 块作用域

块作用域是最常见的——花括号 `{}` 包围的区域就是一个块，在块内声明的变量只在这个块（以及嵌套的子块）内可见。`if`、`for`、`while` 的循环体，甚至你随手写的一对花括号，都会创建新的块作用域：

```c
#include <stdio.h>

int main(void) {
    int x = 10;  // x 在整个 main 函数体中可见

    if (x > 5) {
        int y = 20;       // y 只在这个 if 块中可见
        printf("x=%d, y=%d\n", x, y);  // OK
    }

    // printf("%d\n", y);  // 错误：y 已经不可见了

    {
        // 你甚至可以凭空创造一个块
        int z = 30;  // z 只在这个匿名块中可见
        printf("z=%d\n", z);
    }

    // printf("%d\n", z);  // 错误：z 同样不可见

    return 0;
}
```

这里值得注意的一点是，内层块可以屏蔽（shadow）外层块的同名变量——内层的 `x` 会暂时"遮住"外层的 `x`，直到内层块结束：

```c
#include <stdio.h>

int main(void) {
    int value = 100;
    printf("Outer: %d\n", value);  // 100

    {
        int value = 200;  // 屏蔽外层的 value
        printf("Inner: %d\n", value);  // 200
    }

    printf("Outer again: %d\n", value);  // 100，外层的 value 没变
    return 0;
}
```

C99 之后，`for` 循环的初始化部分也可以声明变量，这个变量的作用域是整个循环（包括循环体和条件判断部分），在循环外不可见。这和 C++ 的行为一致，但如果你用的是古老的 C89 编译器（一般不太可能了），循环变量必须在循环外声明。

### 文件作用域

在所有函数之外声明的变量和函数具有文件作用域——它们从声明位置开始，一直到当前翻译单元（也就是 `.c` 文件加上它 `#include` 进来的所有内容）的末尾都可见。习惯上我们把这类变量叫"全局变量"，但实际上它们的可见性并不真的是"全局"的——是否被其他翻译单元看到，取决于链接性，后面会详细说：

```c
#include <stdio.h>

// 这两个具有文件作用域，从声明处到文件末尾可见
int kGlobalCounter = 0;
static int kInternalVar = 42;  // static 限制了链接性，但作用域仍是文件级

void increment_counter(void) {
    kGlobalCounter++;
}

int main(void) {
    increment_counter();
    printf("Counter: %d\n", kGlobalCounter);
    return 0;
}
```

### 函数作用域

这个作用域比较特殊，它**只适用于标签**（label），也就是 `goto` 跳转目标的那个带冒号的名字。标签在其所在的整个函数内都可见，不管它在哪个嵌套层次声明。说实话，因为你大概率不太会用 `goto`，所以这个作用域了解一下就行，知道有这回事就够了：

```c
#include <stdio.h>

void demo_function_scope(void) {
    goto cleanup;  // 跳到标签，标签在整个函数内可见

    {
        // 即使标签在嵌套块内声明，上面的 goto 也能找到它
        // （但这样写可读性很差，别这么干）
    }

cleanup:
    printf("Cleanup done.\n");
}
```

### 函数原型作用域

这是最小的一个作用域——函数声明（原型）中出现的参数名，只在这个声明的括号范围内有效，出了括号就不存在了。实际上编译器根本不在乎原型里的参数名（它只看类型），所以这个作用域基本可以忽略：

```c
// name 只在这个声明的括号里有效，出了括号就没了
// 实际上你完全可以不写参数名
void greet(const char* name);

// 和上面完全等价
void greet(const char*);
```

## 第二步——理解存储类别怎么管生命周期

作用域解决的是"名字在哪里可见"的问题，而存储类别解决的是"数据什么时候创建、什么时候销毁、存在哪里"的问题。C 语言定义了几个存储类别说明符：`auto`、`static`、`extern`、`register`，以及 C11 新增的 `_Thread_local`。

### auto：默认的自动存储

`auto` 是局部变量的默认存储类别——你在函数里写 `int x = 10;`，和写 `auto int x = 10;` 完全等价。因为这是默认行为，没有人会显式写 `auto`，所以你基本不会在真正的代码里看到它。它意味着这个变量在进入所在块时被创建（分配在栈上），在离开块时被销毁。

有一个容易混淆的点：C++11 把 `auto` 改成了类型推导的关键字，和 C 语言的 `auto` 毫无关系。如果你以后写 C++ 代码看到 `auto x = 10;`，那是让编译器推导 `x` 的类型为 `int`，不是什么存储类别。

### static：贯穿程序始终

`static` 是 C 语言里含义最多的关键字之一，它在不同位置出现时做的事情完全不同。我们先看它作为存储类别说明符的含义——**把变量的生命周期从自动变为静态**。

普通的局部变量每次进入函数都会重新初始化，离开函数就消失。但如果你给局部变量加上 `static`，它就只在程序启动时初始化一次（如果你没给初值，它会被初始化为零），之后即使函数返回了，这个变量也不会被销毁，下次再调用函数时还能看到上一次的值：

```c
#include <stdio.h>

void counter(void) {
    static int call_count = 0;  // 只初始化一次
    call_count++;
    printf("Called %d times\n", call_count);
}

int main(void) {
    counter();  // Called 1 times
    counter();  // Called 2 times
    counter();  // Called 3 times
    return 0;
}
```

这个 `call_count` 虽然看起来是"局部变量"，但它不存储在栈上——它存储在数据段（Data Segment）或 BSS 段中，和全局变量住在一起。唯一的区别是它的**作用域**仍然是块作用域，只有 `counter` 函数内部能访问它。

为什么要这么做？想象你在写一个模块，需要维护一些内部状态（比如缓冲区、计数器、配置信息），但你不希望外部代码直接碰这些数据。用 `static` 局部变量就能实现"数据持久化 + 访问受限"的完美组合——信息隐藏的一种朴素实现。

### extern：声明在别处定义的符号

`extern` 告诉编译器"这个变量/函数在别的地方定义了，你先别管它在哪里，链接的时候会找到的"。它的典型用法是在多文件项目中共享全局变量：

```c
// === config.c（定义） ===
#include "config.h"

int kMaxRetryCount = 3;  // 定义，分配内存
const char* kServerAddress = "192.168.1.100";
```

```c
// === config.h（声明） ===
#ifndef CONFIG_H
#define CONFIG_H

extern int kMaxRetryCount;  // 声明，不分配内存
extern const char* kServerAddress;

#endif
```

```c
// === main.c（使用） ===
#include <stdio.h>
#include "config.h"

int main(void) {
    printf("Server: %s, Retry: %d\n", kServerAddress, kMaxRetryCount);
    return 0;
}
```

这里的关键区别是：**定义**（definition）会分配内存，只能出现一次；**声明**（declaration）用 `extern` 表示"它在别处定义"，可以出现多次。头文件里放声明，源文件里放定义，这是 C 语言多文件项目的基本组织模式。

一个常见的坑是这么写：

```c
// 头文件里
extern int kValue = 42;  // 千万别这么干！
```

如果给 `extern` 声明同时赋了初值，`extern` 就被忽略了——这变成了一个定义。如果这个头文件被多个 `.c` 文件 `#include`，每个翻译单元都会生成一个 `kValue` 的定义，链接的时候就会收到 `multiple definition` 错误。

> ⚠️ **踩坑预警**
> 头文件里放 `extern int kValue = 42;` 是典型的错误写法——带初始值的 `extern` 等于定义，头文件被多次 include 就会导致链接冲突。记住：头文件里只放声明（不带初始值），定义放 `.c` 文件里。

### register：一个历史遗留的建议

`register` 是早期 C 语言用来建议编译器"把这个变量放在寄存器里"的关键字。在 1970 年代的 PDP-11 上，编译器优化能力有限，程序员手动指定 `register` 确实能提升性能。

但在现代编译器面前，这个关键字基本没用了——GCC 和 Clang 的优化器比你更清楚哪个变量该放寄存器。事实上，你写了 `register` 编译器也完全可以忽略。而且 `register` 变量不能取地址（不能对它用 `&`），因为它可能根本不在内存里——这个限制偶尔会坑到你。

了解一下就行，现代代码里不建议使用。

## 第三步——掌握链接性控制符号可见性

链接性（linkage）描述的是一个名字在不同翻译单元之间的可见性。C 语言定义了三种链接性：外部链接（external linkage）、内部链接（internal linkage）和无链接（no linkage）。

- **外部链接**的名字可以被整个程序的所有翻译单元访问。普通的全局变量和函数默认就是外部链接的——只要你在其他文件里用 `extern` 声明一下，就能用。
- **内部链接**的名字只在当前翻译单元内可见，其他文件即使 `extern` 了也找不到。给文件作用域的变量或函数加 `static` 就能把它变成内部链接。
- **无链接**的名字只在自己的作用域内有效——局部变量、函数参数、块作用域里的 `typedef` 都是无链接的。

这三者的关系可以用一个表格来总结：

| 声明位置 | 关键字 | 链接性 | 作用域 | 生命周期 |
| --- | --- | --- | --- | --- |
| 函数内 | （无） | 无 | 块 | 自动 |
| 函数内 | `static` | 无 | 块 | 静态 |
| 函数外 | （无） | 外部 | 文件 | 静态 |
| 函数外 | `static` | 内部 | 文件 | 静态 |
| 函数外 | `extern` | （取决于首次声明） | 文件 | 静态 |

这个表格值得多看几眼——注意函数外的 `static` 改变的是链接性（从外部变成内部），而不是作用域或生命周期。

我们来通过一个多文件的实际例子感受一下链接性是怎么工作的：

```c
// === logger.c ===
#include <stdio.h>

// 内部链接——只有 logger.c 内部能用
static int log_count = 0;

// 内部链接的辅助函数
static void format_prefix(const char* level) {
    printf("[%s #%d] ", level, ++log_count);
}

// 外部链接——其他文件可以调用
void log_info(const char* message) {
    format_prefix("INFO");
    printf("%s\n", message);
}

void log_error(const char* message) {
    format_prefix("ERROR");
    printf("%s\n", message);
}
```

```c
// === logger.h ===
#ifndef LOGGER_H
#define LOGGER_H

void log_info(const char* message);
void log_error(const char* message);

// 注意：log_count 和 format_prefix 不出现在头文件里
// 它们是 logger.c 的内部实现细节

#endif
```

```c
// === main.c ===
#include "logger.h"

int main(void) {
    log_info("System starting");
    log_error("Something went wrong");
    log_info("Retrying...");
    return 0;
}
```

编译运行：

```bash
gcc -Wall -Wextra -std=c11 -o logger_demo main.c logger.c && ./logger_demo
```

输出：

```text
[INFO #1] System starting
[ERROR #2] Something went wrong
[INFO #3] Retrying...
```

`logger.c` 里的 `log_count` 和 `format_prefix` 用 `static` 标记为内部链接，这意味着即使其他文件里也有一个叫 `log_count` 的全局变量，也不会产生冲突。这就是 `static` 在文件级的核心价值——**信息隐藏**，把模块的内部实现细节封装起来，只通过头文件暴露公共接口。

如果你好奇不加 `static` 会怎样——试试在两个不同的 `.c` 文件里各定义一个 `int log_count = 0;`，编译时大概率会看到链接器报 `multiple definition of 'log_count'`。这就是为什么全局变量和辅助函数如果不打算对外暴露，一定要加 `static`。

## 第四步——理清 static 的三种用法

理解了作用域和链接性，最后一个维度是**生命周期**（storage duration）——一个对象从创建到销毁的时间跨度。生命周期和 static 的用法密不可分，我们把它们放在一起讲。

> ⚠️ **踩坑预警**
> 不能返回指向局部变量的指针——函数返回后那块栈空间就被回收了，指针变成悬空指针，解引用是未定义行为。如果你需要在函数间传递数据，要么传值、要么用 `static` 局部变量、要么动态分配内存。

**自动生命周期**是最常见的：普通局部变量在进入所在块时创建，离开块时销毁。它们存储在栈上，函数每调用一次，局部变量就被创建一次，返回后就没了。这也是为什么你不能返回指向局部变量的指针——函数返回后那块栈空间就被回收了，指针变成了悬垂指针（dangling pointer），解引用它属于未定义行为。

**静态生命周期**的对象从程序启动时就存在，一直活到程序结束。这包括所有文件作用域的变量（不管有没有 `static`），以及函数内用 `static` 声明的局部变量。它们存储在数据段（有初值的）或 BSS 段（没初值的，自动初始化为零）中。

**动态生命周期**的对象通过 `malloc`/`calloc`/`realloc` 在堆上分配，由程序员手动管理——什么时候 `free`，什么时候销毁。这部分我们在后面的内存管理章节会详细讨论。

```c
#include <stdio.h>
#include <stdlib.h>

int kGlobalVar = 10;             // 静态生命周期，数据段
static int kInternalVar = 20;    // 静态生命周期，数据段，内部链接
int kUninitialized;              // 静态生命周期，BSS 段，自动为 0

void demonstrate_lifetime(void) {
    int auto_var = 30;           // 自动生命周期，栈上
    static int static_var = 40;  // 静态生命周期，数据段

    int* heap_var = malloc(sizeof(int));  // 动态生命周期，堆上
    *heap_var = 50;

    printf("auto=%d, static=%d, heap=%d\n",
           auto_var, static_var, heap_var);

    free(heap_var);  // 手动销毁
    // auto_var 在函数返回时自动销毁
    // static_var 继续活着
}
```

一个容易忽略的事实是：全局变量的初始化顺序在同一个翻译单元内是确定的（按照定义顺序），但跨翻译单元的初始化顺序是**未定义的**。对于 C 语言来说这通常不是大问题（因为全局变量一般用常量表达式初始化），但这在 C++ 中是个著名的坑——C++ 允许全局对象有构造函数，而跨文件的构造顺序是未定义的，这就是所谓的"static initialization order fiasco"。我们提前知道有这么回事就行。

既然 `static` 在不同位置含义不同，我们来做个完整的总结。

**用法一：静态局部变量**——在函数内部，`static` 让局部变量拥有静态生命周期，函数返回后变量不销毁，下次调用保留上次的值，但作用域仍然是块作用域。

**用法二：静态全局变量**——在函数外部，`static` 让全局变量变成内部链接，其他翻译单元看不到它。作用域仍然是文件作用域，生命周期仍然是静态的，唯一改变的是链接性。

**用法三：静态函数**——给函数加 `static`，和静态全局变量同理，函数变成内部链接，只在当前翻译单元可见。

注意这三种用法中，"静态局部变量"改变的是生命周期（从自动变成静态），而"静态全局变量"和"静态函数"改变的是链接性（从外部变成内部）。同一个关键字做了两件不同的事，这是 C 语言设计上的一个历史遗留问题，但用多了也就习惯了。

## C++ 衔接

C++ 在作用域和存储类别的基础上做了不少增强和改进。

最值得一提的是**命名空间（namespace）**。在 C 语言中，如果你不想让文件级的辅助符号暴露给外部，唯一的手段就是 `static`——前面我们的 `logger.c` 就是这么做的。但 C++ 引入了 `namespace`，提供了更结构化的方式来组织符号、避免命名冲突。更妙的是，C++17 引入了 **`inline` 变量**，让头文件中的常量定义不再需要 `extern` 配合源文件定义的繁琐模式：

```cpp
// C++17 的头文件——不需要配套的 .cpp 文件
#ifndef CONFIG_HPP
#define CONFIG_HPP

inline constexpr int kMaxRetryCount = 3;  // inline 允许多重定义
inline constexpr const char* kServerAddress = "192.168.1.100";

#endif
```

C++ 的 **`static` 类成员**又是另一种语义——它表示这个成员属于类本身而非类的某个实例，所有对象共享同一份。这和 C 语言的 `static` 又不是一回事了：

```cpp
class Counter {
public:
    static int count;  // 声明，所有 Counter 对象共享
    static void reset() { count = 0; }
};

int Counter::count = 0;  // 定义，在类外（C++17 可以用 inline static）
```

另外，C++ 的匿名命名空间（anonymous namespace）可以完全替代文件级 `static` 的用法，而且更彻底——匿名命名空间里的符号不仅对外部隐藏，连模板参数推导也参与不了。在 C++ 项目中，推荐用匿名命名空间代替 `static`。

最后，C++11 的 `thread_local` 提供了线程级别的存储周期——每个线程有自己独立的变量副本。这在多线程编程中非常有用，C11 也有对应的 `_Thread_local`，但支持程度和易用性都不如 C++。

## 小结

作用域、存储类别和链接性这三个概念共同构成了 C 语言中"名字管理"的完整体系。作用域决定名字在哪里可见，存储类别决定数据活多久、存在哪里，链接性决定名字能否跨文件被访问。

`static` 是这个体系中最容易混淆的关键字——在函数内部它改变生命周期，在函数外部它改变链接性。但只要记住这个区别，你就不会再搞混了。`extern` 则是多文件项目共享全局变量的工具，配合头文件的声明-源文件的定义模式来使用。

在实际项目中，养成一个习惯：**不打算对外暴露的全局变量和辅助函数，统统加 `static`**。这是 C 语言层面最实用的信息隐藏手段，能大幅减少多文件项目中的命名冲突和意外依赖。

### 关键要点

- [ ] C 有四种作用域：块、文件、函数、函数原型
- [ ] `static` 局部变量拥有静态生命周期但块作用域
- [ ] `static` 全局变量/函数拥有内部链接，其他文件不可见
- [ ] `extern` 声明一个在其他地方定义的符号
- [ ] 全局变量不加 `static` 就是外部链接，任何文件都能通过 `extern` 访问
- [ ] 内部链接的符号即使在多个文件中同名也不冲突

## 练习

### 练习 1：模块化计数器

设计一个简单的模块，头文件只暴露 `counter_increment`、`counter_get`、`counter_reset` 三个函数，内部用一个 `static` 变量维护计数。要求外部无法直接访问或修改这个计数器变量。

```c
// === counter.h ===
void counter_increment(void);
int counter_get(void);
void counter_reset(void);
```

请自行实现 `counter.c`。

### 练习 2：多文件符号可见性

创建三个文件 `a.c`、`b.c`、`main.c`。要求：

- `a.c` 定义一个外部链接的全局变量 `int kSharedValue`，初始值为 `0`
- `a.c` 定义一个内部链接的辅助函数 `static void helper_a(void)`
- `b.c` 也定义一个同名的内部链接辅助函数 `static void helper_a(void)`（不冲突！）
- `b.c` 通过 `extern` 访问 `kSharedValue` 并提供一个修改它的函数
- `main.c` 调用各模块提供的函数并验证结果

```c
// a.h —— 请自行设计
// b.h —— 请自行设计
// 各 .c 文件的实现留给你
```

### 练习 3：延迟初始化

用 `static` 局部变量实现一个 `get_config` 函数：第一次调用时执行初始化（打印 "Initializing..." 并设置默认值），后续调用直接返回已初始化的值，不再重新初始化。

```c
typedef struct {
    int max_connections;
    int timeout_ms;
    const char* server_name;
} Config;

const Config* get_config(void);
```

> 提示：`static` 局部变量只在第一次进入函数时被初始化——正好可以用来实现"只初始化一次"的语义。

## 参考资源

- [存储类别说明符 - cppreference](https://en.cppreference.com/w/c/language/storage_duration)
- [作用域 - cppreference](https://en.cppreference.com/w/c/language/scope)
- [链接性 - cppreference](https://en.cppreference.com/w/c/language/storage_duration#Linkage)
