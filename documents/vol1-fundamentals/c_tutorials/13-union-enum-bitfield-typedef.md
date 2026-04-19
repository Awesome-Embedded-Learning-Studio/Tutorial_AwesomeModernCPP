---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 掌握联合体、枚举、位域与 typedef 的使用，理解类型双关、硬件寄存器映射等技巧，对比 C++ 的类型安全替代方案
difficulty: beginner
order: 17
platform: host
prerequisites:
- 12 结构体与内存对齐
reading_time_minutes: 15
tags:
- host
- cpp-modern
- beginner
- 入门
- 类型安全
title: 联合体、枚举、位域与 typedef
---
# 联合体、枚举、位域与 typedef

上一篇我们彻底拆了结构体的内存布局，搞清楚了编译器会在你的字段之间塞填充字节这件事。这一篇我们要看的四个语言特性——联合体（union）、枚举（enum）、位域（bit-field）和 typedef——看起来像是结构体的"配角"，但它们各自都有不可替代的用武之地。联合体让你在同一块内存上玩变戏法，枚举让你用有意义的名字代替魔法数字，位域让你按位精确控制内存布局，typedef 则让你给类型起别名、把复杂声明收拾干净。

这四个特性在嵌入式开发中几乎是形影不离的。如果你去看任何一块 MCU 的头文件（比如 STM32 的 `stm32f1xx.h`），你会发现寄存器的定义就是联合体+结构体+位域+typedef 的组合拳。搞懂它们，你才能读懂那些看上去密密麻麻的硬件抽象层代码。

> **学习目标**
>
> - 完成本章后，你将能够：
> - [ ] 理解联合体的内存共享机制和类型双关技术
> - [ ] 掌握枚举的定义、使用和局限性
> - [ ] 使用位域定义紧凑的硬件寄存器结构
> - [ ] 熟练使用 typedef 简化复杂类型声明
> - [ ] 组合运用这些特性实现 tagged union 和协议帧解析
> - [ ] 了解 C++ 中对应的类型安全替代方案

## 环境说明

本篇的所有代码在以下环境下验证通过：

- **操作系统**：Linux（Ubuntu 22.04+） / WSL2 / macOS
- **编译器**：GCC 11+（通过 `gcc --version` 确认版本）
- **编译选项**：`gcc -Wall -Wextra -std=c11`（开警告、指定 C11 标准）
- **验证方式**：所有代码可直接编译运行

## 第一步——用联合体在同一块内存上变戏法

### 搞清楚联合体的内存模型

联合体的定义语法和结构体几乎一模一样，唯一的区别是关键字从 `struct` 换成了 `union`。但它们的内存行为天差地别：结构体的每个成员各自占据独立的内存空间，而联合体的所有成员**共享同一块起始地址相同的内存**。联合体的大小等于其最大成员的大小（可能再加上一些对齐填充）。

```c
#include <stdio.h>
#include <stdint.h>

typedef union {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
} IntUnion;

int main(void) {
    printf("sizeof(IntUnion) = %zu\n", sizeof(IntUnion));  // 4
    return 0;
}
```

运行结果：

```text
sizeof(IntUnion) = 4
```

`IntUnion` 的大小是 4 字节——由最大的成员 `uint32_t` 决定。`u8`、`u16`、`u32` 三个成员的起始地址完全相同，写入其中一个就会覆盖其他的。

> ⚠️ **踩坑预警**：联合体在同一时刻只有**一个**成员是有效的。写入一个成员后再读取另一个成员，在 C 标准中属于未定义行为（除了类型双关的例外）。你必须自己记住当前哪个成员是活跃的，编译器不会帮你检查。

### 用类型双关查看浮点数的二进制表示

虽然 C 标准说"写入一个成员后读取另一个成员是未定义行为"，但有一个重要的例外：通过联合体进行类型双关在 C99 及以后是**合法的**。所谓类型双关，就是把同一块内存按不同的类型来解读：

```c
#include <stdio.h>
#include <stdint.h>

typedef union {
    float    f;
    uint32_t u;
} FloatBits;

int main(void) {
    FloatBits fb;
    fb.f = 3.14f;
    printf("float 值: %f\n", fb.f);        // 3.140000
    printf("二进制表示: 0x%08X\n", fb.u);  // 0x4048F5C3
    return 0;
}
```

运行结果：

```text
float 值: 3.140000
二进制表示: 0x4048F5C3
```

这在 C 中是完全合法的。但要注意，这在 **C++ 中是未定义行为**——C++ 标准不允许通过联合体进行类型双关。如果你在 C++ 代码中需要做类似的事情，应该使用 `memcpy`（编译器会优化掉）或者 `std::bit_cast`（C++20）。

### 组合联合体和结构体实现变体类型

联合体真正发挥威力的时刻是和结构体、枚举组合使用。单独的联合体没什么用——因为你不知道当前存的是哪个成员。但如果你加一个"标签"来记录当前类型，它就变成了一个有意义的变体类型：

```c
#include <stdio.h>
#include <stdint.h>

typedef enum {
    kValueTypeInt,
    kValueTypeFloat,
    kValueTypeString
} ValueType;

typedef struct {
    ValueType tag;
    union {
        int32_t  int_val;
        float    float_val;
        const char* str_val;
    } data;
} TaggedValue;

void print_value(const TaggedValue* v) {
    switch (v->tag) {
        case kValueTypeInt:
            printf("int: %d\n", v->data.int_val);
            break;
        case kValueTypeFloat:
            printf("float: %f\n", v->data.float_val);
            break;
        case kValueTypeString:
            printf("string: %s\n", v->data.str_val);
            break;
    }
}
```

这种"标签+联合体"的组合模式叫做 **tagged union**（标签联合体），是 C 语言中实现多态的基本手法。

## 第二步——用枚举给整数起名字

### 搞清楚枚举的本质

枚举让你定义一组命名的整数常量，语法很简单：

```c
typedef enum {
    kColorRed,
    kColorGreen,
    kColorBlue
} Color;

Color c = kColorGreen;
printf("%d\n", c);  // 1
```

枚举值默认从 0 开始递增。你可以显式指定值：

```c
typedef enum {
    kStatusOk         = 0,
    kStatusError      = 1,
    kStatusTimeout    = 2,
    kStatusBusy       = 3,
    kStatusInvalidArg = 4
} StatusCode;
```

### 注意枚举的局限性

C 语言的枚举有一个让人又爱又恨的特点：**枚举值本质上就是 int**。这意味着你可以把任意整数赋给枚举变量，编译器不会报错：

```c
Color c = 42;          // 合法！但 42 不是任何枚举值
int x = kColorRed;     // 合法！隐式转为 int
```

这种宽松在 C 语言看来是"灵活性"，但从类型安全的角度看就是灾难——编译器完全没办法帮你检查"这个值是不是合法的枚举值"。这也是 C++ 引入 `enum class` 的根本原因。

## 第三步——用位域按位分配内存

### 先看位域的基本语法

位域允许你在结构体中以**位**为单位来分配存储空间。语法是在字段名后面加冒号和位数：

```c
typedef struct {
    uint32_t enable    : 1;   // 1 位
    uint32_t mode      : 3;   // 3 位（可表示 0-7）
    uint32_t priority  : 4;   // 4 位（可表示 0-15）
    uint32_t reserved  : 24;  // 24 位保留
} ControlReg;  // 总计 32 位 = 4 字节
```

访问位域成员的方式和普通结构体完全一样：

```c
ControlReg reg = {0};
reg.enable   = 1;
reg.mode     = 5;
reg.priority = 3;
```

### 用位域映射硬件寄存器

位域在嵌入式开发中最常见的应用就是映射硬件寄存器：

```c
typedef struct {
    volatile uint32_t enable     : 1;   // bit 0: 使能
    volatile uint32_t tickint    : 1;   // bit 1: 中断使能
    volatile uint32_t clksource  : 1;   // bit 2: 时钟源选择
    volatile uint32_t reserved   : 13;  // bit 15:3 保留
    volatile uint32_t countflag  : 1;   // bit 16: 计数标志
    volatile uint32_t reserved2  : 15;  // bit 31:17 保留
} SysTickCtrl;

volatile SysTickCtrl* systick_ctrl = (volatile SysTickCtrl*)0xE000E010;
systick_ctrl->enable    = 1;
systick_ctrl->tickint   = 1;
systick_ctrl->clksource = 1;
```

### 注意位域的可移植性陷阱

位域用起来很爽，但它有一个你必须正视的代价：**可移植性差**。C 标准对位域有几个关键细节没有规定——位域的分配顺序（从低位向高位还是反过来）、对齐和填充规则，这些全部交给编译器实现。

> ⚠️ **踩坑预警**：位域用来映射硬件寄存器时，一定要用编译器提供的标准头文件（比如 STM32 的 CMSIS 头文件）作为参考。那些头文件里的寄存器结构体是经过厂商验证的，位域的分配方向和平台是一致的。自己手写位域映射硬件寄存器，在不同编译器之间很可能出问题。

### 位域 vs 手写位运算掩码

正因为位域的可移植性问题，很多嵌入式项目会完全避免使用位域，转而用手写的位运算掩码：

```c
#define CTRL_ENABLE_MASK    (1U << 0)
#define CTRL_MODE_MASK      (0x7U << 1)

volatile uint32_t* ctrl_reg = (volatile uint32_t*)0xE000E010;
*ctrl_reg |= CTRL_ENABLE_MASK;
*ctrl_reg = (*ctrl_reg & ~CTRL_MODE_MASK) | (5U << 1);
```

位运算掩码的优点是完全可移植、不依赖编译器行为，缺点是代码可读性差。实践中经常两者混用。

## 第四步——用 typedef 给类型起别名

### 先看基本用法

typedef 的核心功能很简单——给一个已有的类型创建一个新名字：

```c
typedef uint32_t Timestamp;
typedef struct { float x; float y; } Point2D;

Timestamp now = 1700000000;
Point2D origin = {0.0f, 0.0f};
```

### 简化函数指针声明

typedef 最实用的场景之一是简化函数指针的声明：

```c
// 不用 typedef：声明一个包含 8 个函数指针的数组
void (*handlers[8])(int);

// 用 typedef：清晰得多
typedef void (*EventHandler)(int);
EventHandler handlers[8];
```

### typedef 和 `#define` 的区别

typedef 创建的是一个**真正的类型别名**，由编译器处理；而 `#define` 只是预处理器的文本替换：

```c
typedef char* CharPtr;
#define CHAR_PTR char*

CharPtr a, b;    // a 和 b 都是 char*
CHAR_PTR c, d;  // 展开后是 char* c, d; — 只有 c 是 char*，d 是 char！
```

> ⚠️ **踩坑预警**：typedef 名不能用于前向声明。解决方法是先写 `typedef struct TagName TagName;` 做前向声明，然后在后面的完整定义中使用 `struct TagName { ... };`。这种写法在实现链表、树等自引用数据结构时非常常见。另外，不要过度使用 typedef——好的 typedef 应该是增加信息量的（比如 `Timestamp` 比 `uint32_t` 更有意义），而不是单纯地隐藏信息。

## C++ 衔接

### enum class：类型安全的枚举（C++11）

```cpp
enum class Color { kRed, kGreen, kBlue };
Color c = Color::kRed;       // 必须加作用域限定
int x = c;                    // 编译错误！不能隐式转 int
int y = static_cast<int>(c);  // OK，必须显式转换
```

`enum class` 还可以指定底层类型：

```cpp
enum class StatusCode : uint8_t { kOk = 0, kError = 1 };
static_assert(sizeof(StatusCode) == 1);
```

### std::variant：类型安全的联合体（C++17）

```cpp
#include <variant>
using Value = std::variant<int, float, const char*>;

Value v1 = 42;
int x = std::get<int>(v1);    // OK
// float f = std::get<float>(v1);  // 抛出 std::bad_variant_access
```

### C++ 中限制 union 的使用

如果 union 的成员拥有非平凡的构造函数、析构函数或拷贝操作（比如 `std::string`），你就必须手动管理这些成员的生命周期。所以在 C++ 中，优先用 `std::variant`。

### std::bitset：替代手动位域

```cpp
#include <bitset>
std::bitset<32> ctrl_reg(0);
ctrl_reg[0] = 1;   // enable
bool enabled = ctrl_reg[0];
```

### using 替代 typedef（C++11）

```cpp
using EventHandler = void (*)(int);  // 比 typedef 更直观
```

## 小结

这一篇我们一口气讲了四个 C 语言特性——联合体、枚举、位域和 typedef——以及它们在 C++ 中的现代替代方案。这四个特性有一个共同的主题：它们都是 C 语言在"灵活性"和"安全性"之间选择灵活性的典型案例。C++ 的改进思路非常明确：`enum class` 约束枚举，`std::variant` 自动管理联合体的活跃成员，`std::bitset` 提供可移植的位集合操作，`using` 提供更直观的别名语法。

## 练习

### 练习 1：IEEE 754 浮点数分解

用联合体实现一个工具，把一个 `float` 值分解成 IEEE 754 格式的符号位、指数和尾数，并打印出来。

```c
#include <stdio.h>
#include <stdint.h>

// TODO: 定义一个联合体，包含 float 和 uint32_t
// TODO: 实现分解函数
// void print_float_bits(float f) {
//     // 提取符号位（1位）、指数（8位）、尾数（23位）
//     // 提示：用位运算 & 和 >>
// }

int main(void) {
    // TODO: 测试几个值：0.0f, -3.14f, 1.0f, 42.0f, 0.1f
    return 0;
}
```

### 练习 2：32 位硬件控制寄存器

用位域定义一个 32 位硬件控制寄存器结构体，然后编写函数对其进行操作。

```c
#include <stdio.h>
#include <stdint.h>

// TODO: 定义 ControlRegister 位域结构体
// 位分配：
//   bit 0:     enable (1位)
//   bit 1:     interrupt_enable (1位)
//   bit 2:     dma_enable (1位)
//   bit 5:3    mode (3位)
//   bit 9:6    speed (4位)
//   bit 31:10  reserved (22位)

typedef union {
    // TODO: 位域结构体视图
    // TODO: uint32_t 整体视图
} ControlRegister;

// TODO: 实现 void print_register(ControlRegister reg)
// TODO: 实现 void set_mode(ControlRegister* reg, uint32_t mode)

int main(void) {
    ControlRegister reg = {0};
    // TODO: 测试各个操作
    return 0;
}
```

### 练习 3：简单的 tagged union

用枚举和联合体实现一个可以存储 `int`、`float` 或字符串指针的 tagged union。

```c
#include <stdio.h>
#include <stdint.h>

// TODO: 定义枚举类型标签
// TODO: 定义 tagged union 结构体
// TODO: 实现构造函数 make_int/make_float/make_string
// TODO: 实现 print_tagged_value 函数
// TODO: 实现 get_as_int/get_as_float/get_as_string 安全访问函数
//       （检查 tag 是否匹配，不匹配则打印错误信息）

int main(void) {
    // TODO: 创建三种类型的值，打印它们
    // TODO: 尝试用错误的 tag 访问，验证安全检查
    return 0;
}
```
