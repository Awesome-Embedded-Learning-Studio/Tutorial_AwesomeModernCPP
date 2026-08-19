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
reading_time_minutes: 10
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
```

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
```

运行结果：

```text
15
5
12
```

函数名在大多数上下文中会隐式转换为函数指针，就像数组名退化为指向首元素的指针一样，所以 `op_ptr = add` 不需要取地址符。调用时 `op_ptr(10, 5)` 和 `(*op_ptr)(10, 5)` 完全等价——C 标准说函数指针会被自动解引用。

### 用 typedef 让声明可读

函数指针的声明语法不太友好，一旦类型复杂起来或者需要多处使用，满屏幕的 `int (*)(int, int)` 实在是折磨人。`typedef` 就是我们的救星——它不创造新类型，只是给现有类型起个别名：

```c
// 给"接受两个int、返回int的函数指针"起个别名
typedef int (*BinaryOp)(int, int);

// 现在声明变量就像普通类型一样自然
BinaryOp op = add;
printf("%d\n", op(3, 4));  // 7
```

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
```

运行结果：

```text
20 + 4 = 24
20 - 4 = 16
20 * 4 = 80
20 / 4 = 5
```

这种"操作表"的模式在嵌入式固件里很常见——比如你有一组串口命令，每个命令对应一个处理函数，把这些函数指针按命令 ID 编入数组，收到命令后直接 `handlers[cmd_id](args)` 一行搞定分发。

> ⚠️ **踩坑预警**：使用函数指针数组做分发时，一定要检查索引是否越界。如果 `cmd_id` 超出数组范围，访问到的要么是垃圾地址，要么是 NULL——直接调用就是段错误（segmentation fault）。

## 第三步——掌握回调函数模式

函数指针真正大放异彩的地方是**回调**（callback）。回调的核心思想很简单：我把一个函数的地址传给你，你在合适的时机替我调用它。用通俗的话说就是"回头再调"——调用者不直接执行某段逻辑，而是把这段逻辑"注册"到被调用者那里，由被调用者在需要的时候回头触发。

### 从 qsort 看回调

C 标准库的 `qsort` 函数是回调模式最经典的教材级案例：

```c
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));
```

前三个参数分别是数组首地址、元素个数和每个元素的大小。最后一个参数是一个比较函数指针——`qsort` 内部在排序过程中需要比较两个元素的大小关系时，会调用这个函数。

```c
#include <stdio.h>
#include <stdlib.h>

int compare_asc(const void* a, const void* b)
{
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
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
```

运行结果：

```text
3 7 12 23 42 55 89
```

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
```

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

**难度：进阶** · 用函数指针做比较策略

参照 `qsort` 的接口设计，实现一个自己的通用插入排序函数，并用它分别对 `int` 数组（升序和降序）和一个字符串数组（按字典序）进行排序：

```c
void insertion_sort(void* base, size_t nmemb, size_t size,
                    int (*compar)(const void*, const void*));
```

::: details 参考答案

这里我们沿用 `qsort` 的比较器约定：返回负数表示左侧元素应排在右侧之前，返回 0
表示两者等价，返回正数表示左侧元素应排在右侧之后。插入排序本身只认这个约定，至于
最终是升序、降序还是字符串字典序，全部交给回调决定。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int compare_int_ascending(const void* a, const void* b)
{
    const int ia = *(const int*)a;
    const int ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
}

int compare_int_descending(const void* a, const void* b)
{
    const int ia = *(const int*)a;
    const int ib = *(const int*)b;
    return (ib > ia) - (ib < ia);
}

int compare_cstrings(const void* a, const void* b)
{
    const char* const lhs = *(const char* const*)a;
    const char* const rhs = *(const char* const*)b;
    return strcmp(lhs, rhs);
}

void insertion_sort(void* base, size_t nmemb, size_t size,
                    int (*compar)(const void*, const void*))
{
    if (base == NULL || compar == NULL || nmemb < 2 || size == 0) {
        return;
    }

    // unsigned char* 可以逐字节移动，才能让同一套算法处理任意元素类型。
    unsigned char* data = (unsigned char*)base;
    unsigned char* current = malloc(size);
    if (current == NULL) {
        return;
    }

    for (size_t i = 1; i < nmemb; ++i) {
        size_t j = i;
        memcpy(current, data + i * size, size);

        while (j > 0 && compar(data + (j - 1) * size, current) > 0) {
            --j;
        }

        if (j != i) {
            // 源区间和目标区间重叠，因此这里必须使用 memmove。
            memmove(data + (j + 1) * size, data + j * size, (i - j) * size);
            memcpy(data + j * size, current, size);
        }
    }

    free(current);
    current=NULL;
}

int main(void)
{
    int ascending_numbers[] = {5, 2, 9, 1, 5, 6};
    int descending_numbers[] = {5, 2, 9, 1, 5, 6};
    const char* words[] = {"pear", "apple", "orange", "banana", "grape"};
    const size_t number_count = sizeof(ascending_numbers) / sizeof(ascending_numbers[0]);
    const size_t word_count = sizeof(words) / sizeof(words[0]);

    insertion_sort(ascending_numbers, number_count, sizeof(ascending_numbers[0]),
                   compare_int_ascending);
    insertion_sort(descending_numbers, number_count, sizeof(descending_numbers[0]),
                   compare_int_descending);
    insertion_sort(words, word_count, sizeof(words[0]), compare_cstrings);

    printf("int 升序：");
    for (size_t i = 0; i < number_count; ++i) {
        printf("%d ", ascending_numbers[i]);
    }

    printf("\nint 降序：");
    for (size_t i = 0; i < number_count; ++i) {
        printf("%d ", descending_numbers[i]);
    }

    printf("\n字符串字典序：");
    for (size_t i = 0; i < word_count; ++i) {
        printf("%s ", words[i]);
    }
    putchar('\n');

    return 0;
}
```

运行结果：

```text
int 升序：1 2 5 5 6 9
int 降序：9 6 5 5 2 1
字符串字典序：apple banana grape orange pear
```

这里和前面的 `qsort` 示例一样，没有写成 `ia - ib`，因为两个相距很远的 `int` 相减
可能发生有符号整数溢出。用 `(ia > ib) - (ia < ib)` 只会得到 `-1`、`0` 或 `1`，同样
满足比较器约定，却不会埋下这个坑。另一个工程上的取舍是：题目给定的接口返回 `void`，
所以临时缓冲区分配失败时只能保持原数组不变并直接返回；如果这是正式库接口，我们通常
会返回状态码，把失败明确交给调用者处理。

:::

### 练习 2：带最大次数的重试

**难度：进阶** · 用函数指针做条件回调

实现一个 `retry_until`：反复调用 `check` 函数指针，直到它返回非零（成功）或达到最大尝试次数。

```c
/// @brief 反复调用 check，直到成功或达到最大尝试次数
/// @param check 条件函数，返回非零表示成功
/// @param max_attempts 最大尝试次数
/// @return 成功时返回第几次尝试（从 1 开始）；全部失败返回 -1
int retry_until(int (*check)(void), int max_attempts);
```

提示：`check` 可以这么模拟一个"第三次才就绪的外设"：

```c
int device_ready(void) {
    static int tried = 0;       // 第 06 章讲过的 static 局部变量，这里正好用上
    return ++tried >= 3;
}
```

想一想：这种把"判断条件"做成函数指针传进来的写法，和本篇的 `qsort` 比较器、事件分发有什么共同点？

::: details 参考答案

先把 `retry_until` 跑起来。这里我们同时准备两个回调：`device_ready` 在第 3 次检查时
成功，`always_fail` 则始终失败，正好把“提前成功”和“达到上限”两条退出路径都走一遍。

```c
#include <stddef.h>
#include <stdio.h>

int retry_until(int (*check)(void), int max_attempts)
{
    if (check == NULL || max_attempts <= 0) {
        return -1;
    }

    int attempt = 0;
    while (attempt < max_attempts) {
        ++attempt;
        if (check() != 0) {
            return attempt;
        }
    }

    return -1;
}

int device_ready(void)
{
    static int tried = 0;
    return ++tried >= 3;
}

int always_fail(void)
{
    return 0;
}

int main(void)
{
    const int ready_attempt = retry_until(device_ready, 5);
    const int failed_attempt = retry_until(always_fail, 2);

    printf("device_ready：第 %d 次检查成功\n", ready_attempt);
    printf("always_fail：%d\n", failed_attempt);
    return 0;
}
```

运行结果：

```text
device_ready：第 3 次检查成功
always_fail：-1
```

它们的共同点是：**框架负责流程，回调负责策略**。`qsort` 决定何时比较元素，但把
“怎样算大小”交给比较器；`retry_until` 决定最多检查几次，但把“怎样算成功”交给
`check`；事件分发器决定何时响应事件，但把“收到事件后做什么”交给处理函数。三者都
不需要知道回调内部的具体实现，只需要约定好函数签名和返回值含义，这就是把算法框架和
可替换行为解耦。

> ⚠️ **踩坑预警**：示例里的 `device_ready` 用 `static` 局部变量模拟外设状态，它的值
> 不会在 `retry_until` 返回后自动复位。如果再次用同一个回调测试，它会在第 1 次检查时
> 直接成功。正式项目通常会通过 `void* context` 传入可独立管理的状态，避免把测试状态
> 藏在函数内部。

:::

### 练习 3：简单的命令行计算器

**难度：进阶** · 用函数指针数组做表驱动分发

使用函数指针数组实现一个命令行计算器，支持加减乘除和取模运算，通过用户输入的操作符选择对应的函数。

```c
typedef int (*BinaryOp)(int, int);
// 请自行设计映射表和主循环
```

```c
#include <limits.h>
#include <stddef.h>
#include <stdio.h>

typedef int (*BinaryOp)(int, int);

typedef struct
{
    char symbol;
    BinaryOp function;
} Operation;

static int add(int left, int right)
{
    return left + right;
}

static int subtract(int left, int right)
{
    return left - right;
}

static int multiply(int left, int right)
{
    return left * right;
}

static int divide(int left, int right)
{
    return left / right;
}

static int modulo(int left, int right)
{
    return left % right;
}

static const Operation operations[] = {
    {'+', add},
    {'-', subtract},
    {'*', multiply},
    {'/', divide},
    {'%', modulo},
};

//根据符号查找对应的运算函数
static const Operation *find_operation(char symbol)
{
    const size_t operation_count =
        sizeof(operations) / sizeof(operations[0]);
    size_t i;

    for (i = 0; i < operation_count; ++i)
    {
        if (operations[i].symbol == symbol)
        {
            return &operations[i];
        }
    }

    return NULL;
}

int main(void)
{
    char line[128];

    puts("Integer calculator: +  -  *  /  %");
    puts("Enter an expression such as 12 + 3, or q to quit.");

    for (;;)
    {
        const Operation *operation;
        int left;
        int right;
        int result;
        char symbol;
        char trailing;

        printf("> ");
        //因为 "> " 没有换行。立即刷新可以保证用户在等待输入前看到提示符
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            putchar('\n');
            break;
        }

        if (sscanf(line, " %c", &symbol) == 1 &&
            (symbol == 'q' || symbol == 'Q'))
        {
            break;
        }

        if (sscanf(line, " %d %c %d %c", &left, &symbol, &right,
                   &trailing) != 3)
        {
            puts("Invalid input. Use: integer operator integer");
            continue;
        }

        operation = find_operation(symbol);
        if (operation == NULL)
        {
            printf("Unknown operator: %c\n", symbol);
            continue;
        }

        if ((symbol == '/' || symbol == '%') && right == 0)
        {
            puts("Error: division by zero is not allowed.");
            continue;
        }

        if ((symbol == '/' || symbol == '%') && left == INT_MIN && right == -1)
        {
            puts("Error: result is outside the range of int.");
            continue;
        }

        result = operation->function(left, right);
        printf("= %d\n", result);
    }

    return 0;
}

```


### 练习 4：事件分发系统扩展（挑战·可选）

**难度：挑战** · 可选，需要设计回调容器，新手可跳过

基于本篇的数组版事件分发系统，扩展成支持同一个事件类型（event type）按不同事件名称（event name）注册多个回调，并支持按 `type + name` 注销回调。提示：不必上链表，可以用**二维函数指针数组**作为回调容器；重复注册同一组 `type + name` 时替换原回调，注销时清空对应槽位。

::: details 参考答案

event.h

```c
#pragma once
#include <stdint.h>
typedef enum {
    ERR_OK = 0,//成功
    ERR_NULL = -1,//空指针
    ERR_INVALID_ARGUMENT = -2,//无效参数
    ERR_FULL = -3,//队列已满
    ERR_NOT_FOUND = -4//未找到
} err_t;
typedef struct {
  void (*fn)(void *arg);
  void *arg;
}Callback_t;

enum EventType
{
    EVENT_TYPE_1 = 0,
    EVENT_TYPE_2,
    EVENT_TYPE_3,
    EVENT_TYPE_Num,
   
};
enum EventName
{
    EVENT_NAME_1 = 0,
    EVENT_NAME_2,
    EVENT_NAME_3,
    EVENT_NAME_Num,
};
```

event.c

```c
#include "event.h"

Callback_t callback_list[EVENT_TYPE_Num][EVENT_NAME_Num] = {0};

//回调注册
void register_callback(enum EventType type, enum EventName name, void (*fn)(void *arg), void *arg)
{
    if (type >= EVENT_TYPE_Num || name >= EVENT_NAME_Num)
    {
        return;
    }

    callback_list[type][name].fn = fn;
    callback_list[type][name].arg = arg;
}
//运行回调
void run_callback(enum EventType type, enum EventName name)
{
    if (type >= EVENT_TYPE_Num || name >= EVENT_NAME_Num)
    {
        return;
    }

    Callback_t *callback = &callback_list[type][name];
    if (callback->fn != NULL)
    {
        callback->fn(callback->arg);
    }
}
//注销回调
void unregister_callback(enum EventType type, enum EventName name)
{
    if (type >= EVENT_TYPE_Num || name >= EVENT_NAME_Num)
    {
        return;
    }

    callback_list[type][name].fn = NULL;
    callback_list[type][name].arg = NULL;
}
```

main.c

```c
#include <stdio.h>

#include "event.h"

/* 回调接口由 event.c 实现。 */
void register_callback(enum EventType type, enum EventName name,
                       void (*fn)(void *arg), void *arg);
void run_callback(enum EventType type, enum EventName name);

static void on_event_name_1(void *arg)
{
    const char *message = (const char *)arg;

    printf("EVENT_NAME_1 callback: %s\n", message);
}

static void on_event_name_2(void *arg)
{
    const char *message = (const char *)arg;

    printf("EVENT_NAME_2 callback: %s\n", message);
}

static void on_event_name_1_replaced(void *arg)
{
    const char *message = (const char *)arg;

    printf("EVENT_NAME_1 replacement callback: %s\n", message);
}

static void callback_demo(void)
{
    const enum EventType type = EVENT_TYPE_1;

    puts("Callback demo (the same type uses different callbacks):");

    /* 同一个事件类型可以通过不同事件名称绑定不同回调。 */
    register_callback(type, EVENT_NAME_1, on_event_name_1,
                      "registered for the first event name");
    register_callback(type, EVENT_NAME_2, on_event_name_2,
                      "registered for the second event name");

    printf("run_callback(EVENT_TYPE_1, EVENT_NAME_1) -> ");
    run_callback(type, EVENT_NAME_1);
    printf("run_callback(EVENT_TYPE_1, EVENT_NAME_2) -> ");
    run_callback(type, EVENT_NAME_2);

    /* 再次注册相同的类型和名称会替换该槽位中的回调。 */
    register_callback(type, EVENT_NAME_1, on_event_name_1_replaced,
                      "the original callback was replaced");
    printf("after re-registering EVENT_NAME_1 -> ");
    run_callback(type, EVENT_NAME_1);
}

int main(void)
{
    callback_demo();
    return 0;
}

```

这里的二维数组把 `type` 和 `name` 共同当作回调的键。同一个 `type` 下，`EVENT_NAME_1` 和 `EVENT_NAME_2` 对应不同槽位，互不影响；再次注册完全相同的 `type + name`，则会替换该槽位原来的回调。

想一想：注销 `EVENT_TYPE_1 + EVENT_NAME_1` 后，为什么 `EVENT_TYPE_1 + EVENT_NAME_2` 仍然可以正常分发？

::: details 参考答案

答案是这两个回调位于二维数组的不同槽位：前者对应 `callback_list[EVENT_TYPE_1][EVENT_NAME_1]`，后者对应 `callback_list[EVENT_TYPE_1][EVENT_NAME_2]`。`unregister_callback` 只会把指定槽位中的 `fn` 和 `arg` 清空，不会修改同一 `type` 下其他 `name` 对应的槽位，所以 `EVENT_NAME_2` 的回调仍然可以正常分发。

:::

## 参考资源

- [函数指针声明 - cppreference](https://en.cppreference.com/w/c/language/pointer)
- [qsort - cppreference](https://en.cppreference.com/w/c/algorithm/qsort)
- [std::function - cppreference](https://en.cppreference.com/w/cpp/utility/functional/function)
- [Lambda 表达式 - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
