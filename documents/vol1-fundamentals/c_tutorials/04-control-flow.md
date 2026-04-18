---
title: "控制流：让程序学会选择和重复"
description: "掌握 C 语言的条件分支、循环、switch 穿透特性与状态机模式，理解 break/continue/goto 的正确用法"
chapter: 1
order: 6
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
  - 基础
difficulty: beginner
platform: host
reading_time_minutes: 20
cpp_standard: [11]
prerequisites:
  - "位运算与求值顺序"
---

# 控制流：让程序学会选择和重复

到目前为止我们写的程序都是从第一行一路跑到最后一行。但现实世界的逻辑不是这样的——"如果温度超过阈值就开风扇"、"重复读取传感器数据直到收到停止命令"。控制流语句就是干这个的：让程序根据条件选择不同的执行路径（分支），或者反复执行某段逻辑（循环）。

这些语句看着简单，但里面藏着不少容易踩的坑。这一篇我们把 C 语言的控制流从头到尾过一遍，重点关注那些"你以为是这样但实际不是"的地方。

> **学习目标**
> 完成本章后，你将能够：
>
> - [ ] 理解 if/else 的悬垂 else 问题和解决方法
> - [ ] 掌握 switch 的穿透特性和 case 标签的限制
> - [ ] 熟练使用三种循环结构及其适用场景
> - [ ] 理解 break/continue 的行为和局限
> - [ ] 用 switch 实现一个实用的状态机

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c17`

## 第一步——条件分支：if/else

### 基本语法

`if/else` 是最基本也是最高频使用的条件分支语句。条件为真（非零）执行 `if` 分支，否则执行 `else` 分支：

```c
if (temperature > kTempHighThreshold) {
    activate_cooling();
} else if (temperature < kTempLowThreshold) {
    activate_heating();
} else {
    maintain_temperature();
}
```text

这里有个小知识：`else if` 并不是 C 语言的一个独立关键字——它实际上是 `else` 后面跟了一个新的 `if` 语句。所以上面的代码在编译器眼里就是 `else { if (...) { } else { } }` 的嵌套结构。虽然理解成"多路分支"更直观，但编译器看到的就是一棵嵌套的二叉分支树。

### 悬垂 else——一个经典的坑

看这段代码：

```c
if (a > 0)
    if (b > 0)
        result = 1;
else
    result = -1;
```text

缩进看起来 `else` 是和第一个 `if` 配对的，但实际不是。C 语言的规则是：**`else` 总是和最近的、尚未配对的 `if` 绑定**。所以这段代码实际等价于：

```c
if (a > 0) {
    if (b > 0) {
        result = 1;
    } else {
        result = -1;
    }
}
```text

如果我们的本意是让 `else` 和外层 `if` 配对，那这段代码就是错的。解决方法很简单——**永远用花括号明确界定每个分支的范围**。

> ⚠️ **踩坑预警**
> 即使分支只有一行代码，也要加花括号。这不是多打几个字符的问题，而是防止歧义和未来维护时引入 bug 的问题——你加一行代码的时候忘了补花括号，逻辑就完全变了。很多编码规范（包括 Linux 内核风格）都强制要求这一点。

### `=` vs `==`——另一个经典笔误

`if (x = 5)` 永远为真（因为赋值表达式的值是 5，非零即真），而且 `x` 被意外修改了。好的编译器遇到这种写法会发出警告，所以务必开启 `-Wall` 让编译器帮你盯着。有些程序员习惯把常量写在左边：`if (5 == x)`，这样万一写成 `if (5 = x)` 编译器会直接报错。

## 第二步——多路分支：switch 语句

当分支条件是对同一个表达式做离散值的比较时，`switch` 比 `if/else if` 链更清晰，而且编译器通常会将 `switch` 优化为跳转表（jump table），查表的时间复杂度接近 O(1)。

```c
typedef enum {
    kCmdStart  = 0x01,
    kCmdStop   = 0x02,
    kCmdPause  = 0x03,
    kCmdResume = 0x04
} Command;

void handle_command(Command cmd) {
    switch (cmd) {
        case kCmdStart:
            start_operation();
            break;
        case kCmdStop:
            stop_operation();
            break;
        case kCmdPause:
            pause_operation();
            break;
        case kCmdResume:
            resume_operation();
            break;
        default:
            handle_unknown_command();
            break;
    }
}
```text

### 穿透特性：忘了 break 就会"漏水"

每个 `case` 分支末尾的 `break` 用来跳出 `switch`。如果忘了写 `break`，执行完当前 case 的代码后不会停下来——它会"穿透"到下一个 case 继续执行。这就是所谓的 **fall-through**。

```c
switch (cmd) {
    case kCmdStart:
        start_operation();
        // 忘了 break！会穿透到 kCmdStop 的逻辑
    case kCmdStop:
        stop_operation();
        break;
}
```text

当 `cmd` 为 `kCmdStart` 时，`start_operation()` 执行完后不会停下来，而是继续执行 `stop_operation()`——一启动就停了，血压拉满。

> ⚠️ **踩坑预警**
> 但有意识地利用穿透特性可以写出很优雅的代码——把多个 case 合并到同一个处理逻辑：

```c
int days_in_month(int month, int is_leap_year) {
    switch (month) {
        case 1: case 3: case 5: case 7:
        case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            return is_leap_year ? 29 : 28;
        default:
            return -1;
    }
}
```text

如果你确实要利用穿透特性，建议加个 `/* fall through */` 注释说明意图，否则后来维护代码的人会以为这是 bug。

### case 标签的限制

`switch` 的 case 标签必须是**整数常量表达式**——编译时就能确定值的整数。这意味着不能用变量、浮点数或字符串。字面量（`42`）、`enum` 成员和 `#define` 宏都行。

养成习惯：**写 `switch` 必须写 `default`**，哪怕只是打一行日志。特别是当你的 `enum` 后来新增了成员但忘了更新 `switch` 的时候，`default` 就是你的安全网。

## 第三步——三种循环：for、while、do-while

### for 循环——已知次数的重复

`for` 循环的三段式设计把初始化、条件判断和步进操作集中到了一行里，非常适合已知迭代次数的场景：

```c
for (int i = 0; i < count; i++) {
    process_item(items[i]);
}
```text

三个部分都可以省略。如果全部省略，就得到一个无限循环——在嵌入式系统的主循环中非常常见：

```c
for (;;) {
    read_sensors();
    process_data();
    update_outputs();
}
```text

逗号运算符可以在 `for` 中同时操作多个变量：

```c
for (int i = 0, j = length - 1; i < j; i++, j--) {
    int temp = arr[i];
    arr[i] = arr[j];
    arr[j] = temp;
}
```text

### while——先检查再决定

`while` 循环先检查条件，如果一开始就是假，循环体一次也不执行。适合那种"条件满足才需要处理"的场景：

```c
while (!uart_data_available()) {
    // 空转等待——实际项目中要加超时机制
}
```text

### do-while——先干再说

`do-while` 至少执行一次循环体，然后再检查条件。适合"至少尝试一次"的逻辑：

```c
do {
    result = attempt_communication();
    retry_count++;
} while (result != kSuccess && retry_count < kMaxRetries);
```text

不管条件怎样，通信至少会尝试一次。用普通 `while` 实现同样的逻辑需要把 `attempt_communication()` 写两次，不够优雅。

来验证一下三种循环的行为差异：

```c
#include <stdio.h>

int main(void)
{
    int count = 0;

    // while：条件一开始就是假，不执行
    while (count > 0) {
        printf("while: 不会打印这行\n");
        count--;
    }

    // do-while：至少执行一次
    count = 0;
    do {
        printf("do-while: count = %d\n", count);
        count++;
    } while (count < 3);

    return 0;
}
```text

运行结果：

```text
do-while: count = 0
do-while: count = 1
do-while: count = 2
```text

很好，`while` 循环体一次都没执行，`do-while` 执行了三次。

## 第四步——break、continue 和 goto

### break——跳出最近一层

`break` 用于立即跳出当前循环或 `switch` 语句。它只影响**最内层**的循环或 `switch`，不会穿透多层嵌套：

```c
for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
        if (matrix[i][j] == target) {
            printf("Found at [%d][%d]\n", i, j);
            break;  // 只跳出内层 j 循环，外层 i 循环继续
        }
    }
}
```text

### continue——跳过本次迭代

`continue` 跳过循环体中剩余的语句，直接进入下一次迭代：

```c
for (int i = 0; i < count; i++) {
    if (data[i] == kInvalidMarker) {
        continue;  // 跳过无效数据
    }
    process_valid_data(data[i]);
}
```text

### goto——慎用但别妖魔化

`goto` 在编程界名声不太好，但在 C 语言中有一个被广泛认可的合理使用场景：**错误处理中的资源清理**。当你有一系列需要按顺序初始化的资源，任何一步失败都需要清理前面已成功的部分时，`goto` 能让代码非常清晰：

```c
int initialize_system(void) {
    if (!init_hardware()) {
        goto error_hardware;
    }
    if (!init_peripherals()) {
        goto error_peripherals;
    }
    if (!init_communication()) {
        goto error_communication;
    }
    return kSuccess;

error_communication:
    shutdown_peripherals();
error_peripherals:
    shutdown_hardware();
error_hardware:
    return kError;
}
```text

> ⚠️ **踩坑预警**
> `goto` 的使用原则：**只向后跳转（向下跳到后面的标签），且只用于错误处理或跳出嵌套**。向前跳转（跳回前面的代码形成循环）是应该坚决避免的——那是 `for`/`while` 的工作。

## 第五步——实战：用 switch 实现状态机

状态机（State Machine）是嵌入式开发中最常见的设计模式之一——通信协议解析、外设控制序列、用户界面流程，到处都是状态机的影子。`switch` 语句是实现状态机最直接的工具。

我们来实现一个简单的通信协议解析器。假设协议格式是：帧头 `0xAA` + 长度 + 负载数据 + 校验和。

```c
typedef enum {
    kStateIdle,
    kStateHeader,
    kStatePayload,
    kStateChecksum,
    kStateDone,
    kStateError
} ParseState;

typedef struct {
    ParseState state;
    unsigned char payload[64];
    unsigned char payload_len;
    unsigned char index;
} Parser;

void parser_init(Parser* p) {
    p->state = kStateIdle;
    p->payload_len = 0;
    p->index = 0;
}

ParseState parser_feed(Parser* p, unsigned char byte) {
    switch (p->state) {
        case kStateIdle:
            if (byte == 0xAA) {
                p->state = kStateHeader;
            }
            break;

        case kStateHeader:
            p->payload_len = byte;
            if (p->payload_len > 64) {
                p->state = kStateError;
            } else {
                p->index = 0;
                p->state = kStatePayload;
            }
            break;

        case kStatePayload:
            p->payload[p->index++] = byte;
            if (p->index >= p->payload_len) {
                p->state = kStateChecksum;
            }
            break;

        case kStateChecksum: {
            unsigned char calc = 0;
            for (int i = 0; i < p->payload_len; i++) {
                calc ^= p->payload[i];
            }
            p->state = (calc == byte) ? kStateDone : kStateError;
            break;
        }

        case kStateDone:
        case kStateError:
            break;
    }
    return p->state;
}
```text

来验证一下，模拟接收一帧数据：

```c
#include <stdio.h>

int main(void)
{
    Parser p;
    parser_init(&p);

    // 帧头 0xAA，长度 3，负载 {0x01, 0x02, 0x03}，校验 0x00
    unsigned char frame[] = {0xAA, 0x03, 0x01, 0x02, 0x03, 0x00};
    for (int i = 0; i < (int)sizeof(frame); i++) {
        ParseState s = parser_feed(&p, frame[i]);
        printf("Byte 0x%02X → State %d\n", frame[i], s);
        if (s == kStateDone) {
            printf("Frame OK, payload: ");
            for (int j = 0; j < p.payload_len; j++) {
                printf("0x%02X ", p.payload[j]);
            }
            printf("\n");
            break;
        } else if (s == kStateError) {
            printf("Parse error at byte %d\n", i);
            break;
        }
    }
    return 0;
}
```text

编译运行：

```bash
gcc -Wall -Wextra -std=c17 parser.c -o parser && ./parser
```text

运行结果：

```text
Byte 0xAA → State 1
Byte 0x03 → State 2
Byte 0x01 → State 2
Byte 0x02 → State 2
Byte 0x03 → State 3
Byte 0x00 → State 4
Frame OK, payload: 0x01 0x02 0x03
```text

很好，状态机正确地从 Idle 一路走到了 Done，每一步的状态转移都符合我们的预期。这种逐字节驱动的状态机模式在串口通信和网络协议解析中非常实用。

## C++ 衔接

C++ 在控制流方面做了几个重要的扩展。C++11 引入了**范围 for 循环**，让遍历容器变得非常简洁：

```cpp
int arr[] = {1, 2, 3, 4, 5};
for (int x : arr) {
    std::cout << x << " ";
}
// 不需要手动管理索引、判断边界、递增计数器
```text

C++17 引入了 `if constexpr`，它在编译期评估条件，直接把不满足条件的分支从代码中剔除。还有 `std::variant` + `std::visit`，提供了一种类型安全的方式来替代传统 `switch`——编译器会检查你是否处理了所有类型，少处理一个就直接编译报错。

## 小结

控制流是程序逻辑的骨架。`if/else` 处理条件分支，加花括号消除悬垂 else 的歧义。`switch` 适合多路分支，穿透特性需要 `break` 来阻止，`default` 别忘加。`for`/`while`/`do-while` 三种循环各有适用场景。`break` 和 `continue` 只作用于最内层。`goto` 在错误处理的资源清理场景下是合理选择。用 `switch` 实现状态机是嵌入式开发的基本功。

接下来我们要学习函数——怎么把代码组织成可复用的模块。

## 练习

### 练习 1：月份天数

用 `switch` 实现一个函数，根据月份和是否闰年返回该月的天数。要求利用穿透特性合并同天数的月份。

### 练习 2：安全的矩阵搜索

在二维矩阵中查找目标值。找到后用两种方式跳出多层循环：一种用标志变量，一种用 `goto`。

```c
typedef struct {
    int row;
    int col;
    int found;
} SearchResult;

SearchResult matrix_search(int** matrix, int rows, int cols, int target);
```text

### 练习 3：带超时的等待

实现一个带超时机制的等待函数，避免裸 `while` 等待导致的死锁：

```c
/// @brief 等待某个条件满足或超时
/// @param check 条件检查函数，返回非零表示条件满足
/// @param timeout_ms 超时时间（毫秒）
/// @return 0 表示条件满足，-1 表示超时
int wait_with_timeout(int (*check)(void), unsigned int timeout_ms);
```text

## 参考资源

- [cppreference: switch 语句](https://en.cppreference.com/w/c/language/switch)
- [cppreference: if 语句](https://en.cppreference.com/w/c/language/if)
- [cppreference: for 循环](https://en.cppreference.com/w/c/language/for)
- [cppreference: goto 语句](https://en.cppreference.com/w/c/language/goto)
