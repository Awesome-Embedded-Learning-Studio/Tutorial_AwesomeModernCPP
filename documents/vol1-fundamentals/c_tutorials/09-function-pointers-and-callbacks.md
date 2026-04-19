---
chapter: 1
cpp_standard:
- 11
description: 掌握函数指针的声明与使用，理解回调函数模式在事件驱动编程中的应用，对比 C++ 的 lambda 和 std::function
difficulty: beginner
order: 13
platform: host
prerequisites:
- 07A 指针基础与核心用法
- 07B 指针、数组与 const
- 08A 多级指针与函数参数
reading_time_minutes: 13
tags:
- host
- cpp-modern
- beginner
- 入门
title: 函数指针与回调模式
---
# 函数指针与回调模式

如果说指针是 C 语言最强大的特性，那函数指针就是指针世界里最容易让人血压拉满的一环。不过说真的，一旦你把它搞明白了，就会发现它是 C 语言中少有的几种能让你写出"灵活到不像 C"的代码的机制——回调、事件驱动、策略模式，这些听起来像是高级语言才有的东西，在 C 里全靠函数指针撑起一片天。

我们在前面的教程里已经系统梳理了指针的各种用法，这篇就专门来啃函数指针这块硬骨头。先从声明和基本用法入手，然后过渡到函数指针数组、回调模式，最后看看 C++ 在这个方向上做了哪些令人舒适的改进。

> **学习目标**
>
> - 完成本章后，你将能够：
> - [ ] 理解函数指针的声明语法并正确使用
> - [ ] 用 typedef 简化复杂的函数指针类型
> - [ ] 实现类似 qsort 的回调排序接口
> - [ ] 构建简单的事件分发系统
> - [ ] 了解 C++ 中 std::function、lambda 和函数对象的对应关系

## 环境说明

本篇的所有代码在以下环境下验证通过：

- **操作系统**：Linux（Ubuntu 22.04+） / WSL2 / macOS
- **编译器**：GCC 11+（通过 `gcc --version` 确认版本）
- **编译选项**：`gcc -Wall -Wextra -std=c11`（开警告、指定 C11 标准）
- **验证方式**：所有代码可直接编译运行

## 第一步——把函数当数据用

在 C 语言里，函数编译后就是一段机器指令，驻留在内存的代码段里。既然在内存里，那它就有地址——函数名本身（不带调用括号的时候）就是一个指向这个地址的指针。我们可以把这个地址存下来，在需要的时候通过它调用函数。

### 先学会声明函数指针

函数指针的声明语法是 C 语言里公认的"反人类"设计之一，我们先硬着头皮看一下：

```c
// 假设有一个函数：int add(int a, int b)
// 它的函数指针类型声明如下：
int (*op_ptr)(int, int);
```text

拆解一下这行声明：`op_ptr` 是一个指针（因为 `*op_ptr` 被括号括起来了），它指向一个接受两个 `int` 参数、返回 `int` 的函数。那个括号不能省——如果写成 `int *op_ptr(int, int)`，编译器会理解为"一个名为 `op_ptr` 的函数，它返回 `int*`"，这完全不是一回事。

> ⚠️ **踩坑预警**：声明函数指针时，`(*op_ptr)` 外面的括号**绝对不能省**。省掉就变成了返回指针的函数声明，编译器不会报错，但行为完全不同。这是新手最容易犯的错误之一。

拿到指针之后，赋值和调用就自然了：

```c
#include <stdio.h>

int add(int a, int b)
{
    return a + b;
}

int subtract(int a, int b)
{
    return a - b;
}

int main(void)
{
    int (*op_ptr)(int, int) = add;     // 函数名就是地址，不需要 &
    printf("%d\n", op_ptr(10, 5));      // 15

    op_ptr = subtract;                  // 指向另一个函数
    printf("%d\n", op_ptr(10, 5));      // 5

    // 通过指针调用也可以显式解引用，两种写法等价
    printf("%d\n", (*op_ptr)(20, 8));   // 12
    return 0;
}
```text

运行结果：

```text
15
5
12
```text

函数名在大多数上下文中会隐式转换为函数指针，就像数组名退化为指向首元素的指针一样，所以 `op_ptr = add` 不需要取地址符。调用时 `op_ptr(10, 5)` 和 `(*op_ptr)(10, 5)` 完全等价——C 标准说函数指针会被自动解引用。

### 用 typedef 让声明可读

函数指针的声明语法不太友好，一旦类型复杂起来或者需要多处使用，满屏幕的 `int (*)(int, int)` 实在是折磨人。`typedef` 就是我们的救星——它不创造新类型，只是给现有类型起个别名：

```c
// 给"接受两个int、返回int的函数指针"起个别名
typedef int (*BinaryOp)(int, int);

// 现在声明变量就像普通类型一样自然
BinaryOp op = add;
printf("%d\n", op(3, 4));  // 7
```text

强烈建议在项目中遇到函数指针就用 typedef 管理起来。特别是在回调接口的 API 设计中，typedef 既简化了函数签名的书写，也让头文件的自文档性好了不少。

## 第二步——用函数指针数组做批量调度

函数指针能做的不仅是保存一个函数地址——把多个函数指针塞进数组里，就可以用索引来选择调用哪个函数。这种模式在命令分发、状态机跳转表等场景中非常实用：

```c
#include <stdio.h>

typedef int (*BinaryOp)(int, int);

int add(int a, int b)      { return a + b; }
int subtract(int a, int b) { return a - b; }
int multiply(int a, int b) { return a * b; }
int divide(int a, int b)   { return b != 0 ? a / b : 0; }

int main(void)
{
    BinaryOp operations[] = { add, subtract, multiply, divide };
    const char* op_names[] = { "+", "-", "*", "/" };

    int x = 20, y = 4;
    for (int i = 0; i < 4; i++) {
        printf("%d %s %d = %d\n", x, op_names[i], y, operations[i](x, y));
    }
    return 0;
}
```text

运行结果：

```text
20 + 4 = 24
20 - 4 = 16
20 * 4 = 80
20 / 4 = 5
```text

这种"操作表"的模式在嵌入式固件里很常见——比如你有一组串口命令，每个命令对应一个处理函数，把这些函数指针按命令 ID 编入数组，收到命令后直接 `handlers[cmd_id](args)` 一行搞定分发。

> ⚠️ **踩坑预警**：使用函数指针数组做分发时，一定要检查索引是否越界。如果 `cmd_id` 超出数组范围，访问到的要么是垃圾地址，要么是 NULL——直接调用就是段错误（segmentation fault）。

## 第三步——掌握回调函数模式

函数指针真正大放异彩的地方是**回调**（callback）。回调的核心思想很简单：我把一个函数的地址传给你，你在合适的时机替我调用它。用通俗的话说就是"回头再调"——调用者不直接执行某段逻辑，而是把这段逻辑"注册"到被调用者那里，由被调用者在需要的时候回头触发。

### 从 qsort 看回调

C 标准库的 `qsort` 函数是回调模式最经典的教材级案例：

```c
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));
```text

前三个参数分别是数组首地址、元素个数和每个元素的大小。最后一个参数是一个比较函数指针——`qsort` 内部在排序过程中需要比较两个元素的大小关系时，会调用这个函数。

```c
#include <stdio.h>
#include <stdlib.h>

int compare_asc(const void* a, const void* b)
{
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return ia - ib;
}

int main(void)
{
    int numbers[] = { 42, 12, 7, 89, 23, 55, 3 };
    size_t count = sizeof(numbers) / sizeof(numbers[0]);

    qsort(numbers, count, sizeof(int), compare_asc);
    for (size_t i = 0; i < count; i++) {
        printf("%d ", numbers[i]);
    }
    printf("\n");
    return 0;
}
```text

运行结果：

```text
3 7 12 23 42 55 89
```text

排序逻辑本身（`qsort` 的实现）完全没有变，我们只是换了一个比较函数，排序结果就完全不同了。这就是回调的威力——**算法和策略解耦**。

> ⚠️ **踩坑预警**：`qsort` 的比较函数接收的是 `const void*`，返回值遵循"左小于右返回负数，相等返回 0，左大于右返回正数"的约定。如果你把比较逻辑写反了，排序结果就是乱序——而且不会有任何编译期提示。

## 第四步——搭一个事件分发系统

我们把前面学的函数指针、typedef、函数指针数组组合起来，搭一个简单的事件分发系统：

```c
#include <stdio.h>

typedef enum {
    kEventButtonPress,
    kEventTimerTick,
    kEventDataReceived,
    kEventCount
} EventType;

typedef void (*EventHandler)(EventType event, void* context);

typedef struct {
    EventHandler handlers[kEventCount];
    void* contexts[kEventCount];
} EventDispatcher;

void dispatcher_init(EventDispatcher* dispatcher)
{
    for (int i = 0; i < kEventCount; i++) {
        dispatcher->handlers[i] = NULL;
        dispatcher->contexts[i] = NULL;
    }
}

void dispatcher_register(EventDispatcher* dispatcher,
                          EventType event,
                          EventHandler handler,
                          void* context)
{
    if (event >= 0 && event < kEventCount) {
        dispatcher->handlers[event] = handler;
        dispatcher->contexts[event] = context;
    }
}

void dispatcher_dispatch(EventDispatcher* dispatcher, EventType event)
{
    if (event >= 0 && event < kEventCount) {
        EventHandler handler = dispatcher->handlers[event];
        if (handler != NULL) {
            handler(event, dispatcher->contexts[event]);
        }
    }
}
```text

这就是一个最小可行的事件系统了。`void* context` 是这里的"万能胶"——回调函数需要什么额外的状态信息，调用者就通过 `context` 指针传进去。这种设计在嵌入式 SDK 里随处可见，比如 STM32 HAL 库里的回调注册接口，本质上就是这套模式。

## C++ 衔接

C++ 在这个方向上做了多层次的改进，从最基础的函数对象到现代的 lambda 和 `std::function`。

**函数对象（Functor）**：给类重载 `operator()`，使其实例可以像函数一样调用。和 C 的函数指针相比，函数对象最大的优势是它可以携带状态。

**Lambda 表达式**（C++11）：在调用点就地定义的匿名函数对象，支持捕获外部变量（闭包）。这在 C 的函数指针世界里是做不到的。

**std::function**（C++11）：通用的、类型安全的函数包装器，可以持有函数指针、函数对象、lambda 等任何可调用目标。统一了所有可调用对象的接口。

**模板策略模式**：在编译期就把策略确定下来，零运行时开销，但增加了编译时间。

从 C 的函数指针到 C++ 的 lambda 和 `std::function`，核心思想是一脉相承的——把"行为"参数化。C 用函数指针做到了最基础的版本，C++ 在此基础上加了类型安全、闭包和统一的可调用对象接口。

## 小结

函数指针是 C 语言中实现回调和策略模式的核心机制。声明语法确实不够友好，但用 `typedef` 管理起来之后实用性很强。函数指针数组实现了表驱动的分发逻辑，回调模式通过 `qsort` 这个经典案例我们已经看得非常清楚了——算法框架和具体策略通过函数指针解耦。事件分发系统则是回调在事件驱动编程中的直接应用。

### 关键要点

- [ ] 函数名在大多数上下文中隐式转换为函数指针
- [ ] 声明语法中括号不能省：`int (*p)(int)` 而非 `int *p(int)`
- [ ] `typedef` 是管理复杂函数指针类型的最佳实践
- [ ] 函数指针数组可以实现表驱动的命令/状态分发
- [ ] 回调的核心是"算法不变、策略可替换"
- [ ] `void*` 提供泛型但牺牲类型安全，C++ 的模板和 `std::function` 解决了这个问题

## 练习

### 练习 1：通用排序接口

参照 `qsort` 的接口设计，实现一个自己的通用插入排序函数，并用它分别对 `int` 数组（升序和降序）和一个字符串数组（按字典序）进行排序：

```c
void insertion_sort(void* base, size_t nmemb, size_t size,
                    int (*compar)(const void*, const void*));
```text

### 练习 2：事件分发系统扩展

基于本篇的事件分发系统，支持同一个事件注册多个回调（回调链）并支持注销回调。思考：如果回调链中某个 handler 执行时修改了链表结构，会发生什么？

### 练习 3：简单的命令行计算器

使用函数指针数组实现一个命令行计算器，支持加减乘除和取模运算，通过用户输入的操作符选择对应的函数。

```c
typedef int (*BinaryOp)(int, int);
// 请自行设计映射表和主循环
```text

## 参考资源

- [函数指针声明 - cppreference](https://en.cppreference.com/w/c/language/pointer)
- [qsort - cppreference](https://en.cppreference.com/w/c/algorithm/qsort)
- [std::function - cppreference](https://en.cppreference.com/w/cpp/utility/functional/function)
- [Lambda 表达式 - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
