---
chapter: 1
cpp_standard:
- 11
description: 掌握结构体定义、内存对齐与填充规则、柔性数组成员及 offsetof 验证
difficulty: beginner
order: 16
platform: host
prerequisites:
- restrict、不完整类型与结构体指针
reading_time_minutes: 24
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 结构体与内存对齐
---
# 结构体与内存对齐

如果你写 C 写到现在，只用过基本类型——int、float、char 这些——那大概率是因为你还没遇到过需要把一组相关数据打包在一起传递的场景。一旦你开始写稍微像样的程序，比如一个传感器数据包、一个配置表、一个通信协议帧，你会发现光靠散装变量根本没法管理。结构体（struct）就是 C 语言给出的答案：它让我们把不同类型的数据揉成一个整体，然后当做一个值来传递、存储和操作。

但结构体远不止"打包数据"这么简单。当我们把结构体放进内存的那一刻，编译器会在幕后做一件你可能从没想过的事——内存对齐（alignment）。它会在你的字段之间偷偷塞进一些填充字节（padding），让每个字段都落在处理器"喜欢"的地址上。如果你不知道这件事的存在，有一天你在设计二进制协议帧、做 DMA 传输、或者手写序列化代码的时候，大概率会被那些幽灵字节搞得怀疑人生。

所以这一篇，我们不仅要学会怎么定义和使用结构体，还要彻底搞清楚结构体在内存里的真实模样。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 熟练定义、初始化和操作结构体及其指针
> - [ ] 理解内存对齐的原理和填充字节的分布规则
> - [ ] 使用 `_Alignas`、`alignof` 和 `offsetof` 进行对齐控制和验证
> - [ ] 掌握指定初始化器和柔性数组成员的使用
> - [ ] 了解结构体到 C++ class 的演进关系

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c17`

## 第一步——掌握结构体的定义与基本操作

### 定义一个结构体

在 C 语言里定义一个结构体，用的是 `struct` 关键字加上一对花括号：

```c
struct SensorReading {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint8_t status;
};
```text

注意结尾那个分号——忘了它是新手最常见的编译错误之一，而且报错信息通常指向下一行，让你一头雾水。`struct SensorReading` 现在就是一个类型名了，但每次都写 `struct SensorReading` 确实有点啰嗦，所以我们通常会搭配 `typedef` 来简化：

```c
typedef struct {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint8_t status;
} SensorReading;
```text

这样我们就可以直接写 `SensorReading reading;` 来声明变量了，清爽很多。两种写法在功能上是等价的，区别只在于类型名的使用方式：前者需要带着 `struct` 前缀，后者不需要。在实际项目中，`typedef` 的用法更为普遍，尤其是在嵌入式开发里——你去看任何一个 MCU 厂商的 SDK，满眼都是 `typedef struct` 的身影。

### 初始化与赋值

结构体有几种初始化方式，我们从最基础的开始。第一种是顺序初始化——按字段定义的顺序依次给出值：

```c
SensorReading r1 = {1700000000, 23.5f, 60.0f, 1};
```text

这种方式能跑，但可读性不太好——你必须记住每个位置对应什么字段，一旦结构体定义调整了顺序，所有初始化代码都得跟着改。C99 给了我们一个更好的方案：**指定初始化器**（designated initializer），它可以按名字初始化任意字段：

```c
SensorReading r2 = {
    .timestamp = 1700000000,
    .temperature = 23.5f,
    .humidity = 60.0f,
    .status = 1
};

// 不需要按定义顺序，也可以只初始化部分字段
SensorReading r3 = {
    .humidity = 45.0f,
    .status = 0
    // timestamp 和 temperature 自动初始化为 0
};
```text

指定初始化器的好处非常明显：代码自文档化，不依赖字段顺序，未指定的字段自动清零。说实话，在现代 C 代码中，只要你用的编译器支持 C99（基本上都支持），就应该优先使用指定初始化器。

结构体的赋值和初始化是两回事。初始化发生在声明时，赋值发生在声明之后。C 语言允许同类型的结构体之间直接赋值，这是逐字节复制的：

```c
SensorReading r4;
r4 = r2;  // 把 r2 的所有字段复制到 r4
```text

但要注意，C 语言的结构体赋值是**浅拷贝**——如果结构体里有指针成员，赋值后两个结构体的指针字段会指向同一块内存。这在处理包含动态分配内存的结构体时是一个经典的坑。

### 结构体指针与箭头运算符

当结构体比较大，或者我们需要在函数中修改调用者的结构体时，传递指针是唯一合理的做法。这里就会遇到 `.` 和 `->` 的区别：

```c
SensorReading reading = {
    .timestamp = 1700000000,
    .temperature = 25.0f,
    .humidity = 50.0f,
    .status = 1
};

// 通过变量名直接访问——用点号
reading.temperature = 26.0f;

// 通过指针访问——用箭头
SensorReading* ptr = &reading;
ptr->humidity = 55.0f;
// 等价于 (*ptr).humidity = 55.0f
```text

`->` 运算符就是 `(*ptr).` 的语法糖，没有什么神秘的。但这个语法糖实在太常用以至于你根本不会去写 `(*ptr).`——在 C 语言里，只要函数参数里有结构体指针，你几乎必定在用 `->`。

在函数参数中传递结构体指针而非结构体本身，不仅能避免昂贵的拷贝开销，还允许函数修改调用者的数据。如果你不想让函数修改数据，加上 `const` 就行了：

```c
/// @brief 打印传感器读数（只读访问）
void print_reading(const SensorReading* r) {
    printf("T=%.1fC H=%.1f%% status=%u\n",
           r->temperature, r->humidity, r->status);
}

/// @brief 更新传感器状态（可修改）
void update_status(SensorReading* r, uint8_t new_status) {
    r->status = new_status;
}
```text

这种 `const SensorReading*` 和 `SensorReading*` 的区分，在 C++ 里会被继承到 `const` 成员函数和引用语义中，形成更完整的"只读 vs 可变"接口设计。

## 第二步——理解内存对齐和填充字节

接下来我们要进入这篇教程最核心也最容易让人迷惑的部分了。先来看一个问题：下面这个结构体占多少字节？

```c
typedef struct {
    uint8_t  a;   // 1 字节
    uint32_t b;   // 4 字节
    uint8_t  c;   // 1 字节
} WeirdLayout;
```text

直觉上，1 + 4 + 1 = 6 字节，对吧？但实际上，在大多数 32 位和 64 位平台上，`sizeof(WeirdLayout)` 是 **12 字节**。那多出来的 6 个字节去哪了？答案是它们被编译器当作**填充字节**（padding）塞进了结构体里。

### 为什么需要对齐

处理器访问内存的时候，并不是一个字节一个字节地去读的。大多数架构的 CPU 更喜欢按照 2、4、8 字节的边界来访问数据——这就是所谓的**对齐**（alignment）。一个 `uint32_t` 如果放在 4 的倍数地址上，CPU 可以一次读出来；但如果它跨了一个 4 字节边界（比如放在地址 3），CPU 可能需要分两次读取再拼起来，性能上会打折扣。有些架构甚至更极端——直接抛出硬件异常（比如 ARM 在某些模式下访问未对齐的地址会触发 fault）。

所以编译器为了性能和正确性，会在结构体成员之间插入填充字节，确保每个成员都落在它自然对齐的地址上。

### 对齐与填充的规则

对齐规则其实就两条，但理解起来需要一点耐心。第一条：**每个成员的起始地址必须是该成员对齐要求的整数倍**。`uint8_t` 的对齐要求是 1（任意地址都行），`uint16_t` 是 2，`uint32_t` 是 4，`double` 和 `uint64_t` 是 8，以此类推——基本类型的对齐要求通常等于它的大小。第二条：**结构体本身的大小必须是其最大对齐要求的整数倍**——这是为了在结构体数组中，每个元素都能满足对齐要求。

现在我们回到 `WeirdLayout` 的例子，逐字节画出来看看：

```text
偏移  0  1  2  3  4  5  6  7  8  9  10  11
     [a ][pad pad pad][b         ][c ][pad pad pad]
      ^              ^           ^
      |              |           b: 偏移 4（4 的倍数，满足）
      |              填充 3 字节让 b 对齐到 4
      a: 偏移 0（1 的倍数，满足）
```text

`a` 在偏移 0，占 1 字节。`b` 的对齐要求是 4，但下一个可用偏移是 1，不是 4 的倍数，所以编译器填入 3 个字节的 padding，让 `b` 从偏移 4 开始。`c` 在偏移 8，对齐要求是 1，没问题。最后，结构体的最大对齐要求是 4（来自 `uint32_t b`），所以总大小必须是 4 的倍数——当前是 9，要填充到 12。

这就是为什么明明只有 6 字节的数据，实际占了 12 字节——50% 的空间都浪费在了填充上。

### 调整字段顺序以减少填充

解决这个问题的方法出奇地简单：**把对齐要求大的字段放前面，小的放后面**。我们把 `WeirdLayout` 的字段重新排列一下：

```c
typedef struct {
    uint32_t b;   // 4 字节，偏移 0
    uint8_t  a;   // 1 字节，偏移 4
    uint8_t  c;   // 1 字节，偏移 5
    // 填充 2 字节（偏移 6-7），使总大小为 4 的倍数
} BetterLayout;
```text

现在 `sizeof(BetterLayout)` 是 **8 字节**——比之前的 12 节省了三分之一。`b` 在偏移 0（天然对齐），`a` 和 `c` 紧挨着排在后面，最后只需要 2 字节的尾部填充。这个技巧在实际工程中非常有用，尤其是在内存受限的嵌入式系统上——养成按对齐要求从大到小排列字段的习惯是值得的。

### 用 offsetof 验证偏移

C 标准库提供了 `offsetof` 宏（定义在 `<stddef.h>` 中），它可以精确地告诉你某个字段在结构体中的偏移量。我们在调试对齐问题、设计二进制协议时，经常会用到它：

```c
#include <stddef.h>
#include <stdio.h>

printf("offset of a: %zu\n", offsetof(WeirdLayout, a));  // 0
printf("offset of b: %zu\n", offsetof(WeirdLayout, b));  // 4
printf("offset of c: %zu\n", offsetof(WeirdLayout, c));  // 8
printf("total size: %zu\n", sizeof(WeirdLayout));         // 12
```text

养成写完结构体就用 `offsetof` 打印一遍的习惯，特别是在设计通信协议帧的时候——你会发现有些字段的偏移跟你预想的不一样，而这通常意味着对齐问题。

## C11 的对齐控制：_Alignas 与 alignof

C99 时代，如果你需要手动控制对齐，只能依赖编译器扩展——GCC 的 `__attribute__((aligned(n)))`、MSVC 的 `__declspec(align(n))` 之类的。C11 终于把这个能力标准化了，提供了 `_Alignas` 和 `_Alignof` 关键字，以及更友好的宏别名 `alignas` 和 `alignof`（定义在 `<stdalign.h>` 中）。

### alignof：查询对齐要求

`alignof` 可以查询任何类型的对齐要求：

```c
#include <stdalign.h>
#include <stdio.h>

printf("alignof(uint8_t)  = %zu\n", alignof(uint8_t));   // 1
printf("alignof(uint32_t) = %zu\n", alignof(uint32_t));  // 4
printf("alignof(double)   = %zu\n", alignof(double));    // 通常 8
printf("alignof(WeirdLayout) = %zu\n", alignof(WeirdLayout)); // 4
```text

结构体的对齐要求等于其成员中最大的对齐要求。`WeirdLayout` 里有 `uint32_t`，所以整体对齐要求是 4。

### alignas：强制对齐

`alignas` 可以用来强制一个变量或结构体成员按指定的对齐边界分配。这在嵌入式开发中非常有用——比如 DMA 传输通常要求缓冲区起始地址是 4 字节甚至 32 字节对齐的：

```c
#include <stdalign.h>

// 强制 DMA 缓冲区 32 字节对齐
alignas(32) uint8_t dma_buffer[256];

// 在结构体中强制某个字段的对齐
typedef struct {
    uint8_t header;
    alignas(4) uint32_t payload;  // 即使前面有 header，也保证 payload 4 字节对齐
} AlignedFrame;
```text

`alignas` 的参数必须是 2 的幂，且不能小于类型的自然对齐要求。如果你写 `alignas(2)` 给一个 `uint32_t`，编译器会忽略它或者报错——因为 `uint32_t` 本身就需要 4 字节对齐，你不可能把它降到 2。

## 指定初始化器详解

前面我们简单提到了指定初始化器，这里再深入看一下它的完整能力。指定初始化器是 C99 引入的特性，它允许你在初始化结构体、联合体和数组时，用 `.成员名 = 值` 的语法来指定要初始化哪些字段。

除了前面展示的基本用法，它还有一些值得注意的细节。比如你可以混合使用顺序初始化和指定初始化器：

```c
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t flags;
} Point3D;

Point3D p1 = {
    10, 20,        // x=10, y=20（顺序初始化）
    .flags = 0xFF  // 指定初始化 flags
    // z 自动为 0
};
```text

在数组中也可以使用指定初始化器：

```c
// 稀疏初始化——只初始化需要的下标
uint8_t lookup[256] = {
    ['A'] = 1,
    ['B'] = 2,
    ['C'] = 3,
    // 其余全部为 0
};
```text

这种写法在做 ASCII 字符映射表、命令分发表的时候特别方便，比起手写 256 个元素的初始化列表要清晰得多。未指定的元素会被自动初始化为零（和全局变量一样）。

## 第三步——了解柔性数组成员

柔性数组成员（Flexible Array Member，简称 FAM）是 C99 引入的一个特性，它允许在结构体的末尾放置一个大小未指定的数组。听起来有点奇怪，但它的用途非常实际——当你需要一个结构体带有一段"可变长度的尾随数据"时，FAM 是最干净的做法。

```c
typedef struct {
    uint16_t length;
    uint8_t  type;
    uint8_t  data[];  // 柔性数组成员，不占结构体大小
} Packet;
```text

`data[]` 是一个不完整类型的数组——它在结构体中不占用空间（`sizeof(Packet)` 不会包含 `data` 的大小），但它告诉编译器"这个结构体的末尾可能跟着一段连续内存"。使用时，我们需要手动分配足够的内存来容纳结构体本身加上数据：

```c
#include <stdlib.h>
#include <string.h>

/// @brief 创建一个指定长度的数据包
Packet* create_packet(uint8_t type, const uint8_t* payload, uint16_t len) {
    // 分配：结构体大小 + 数据长度
    Packet* pkt = malloc(sizeof(Packet) + len);
    if (pkt == NULL) {
        return NULL;
    }
    pkt->type = type;
    pkt->length = len;
    memcpy(pkt->data, payload, len);
    return pkt;
}

// 使用
uint8_t payload[] = {0x01, 0x02, 0x03};
Packet* pkt = create_packet(0x42, payload, sizeof(payload));
// 访问 pkt->data[0], pkt->data[1], pkt->data[2]
free(pkt);
```text

柔性数组成员在通信协议、变长消息处理、数据包解析中用得非常多。在 C 的早期年代，人们用一种叫做"struct hack"的技巧来实现类似功能——在结构体末尾放一个长度为 1（或 0）的数组，然后多分配一些空间。但那是未定义行为，C99 的 FAM 才是标准做法。

有一点需要注意：含柔性数组成员的结构体不能按值传递或复制——因为 `sizeof` 不知道尾部数据有多大。你只能通过指针来操作它们。

## 结构体数组

结构体和数组组合在一起是非常常见的数据组织方式。比如一张配置表、一组传感器读数、一个消息队列，本质上都是结构体数组：

```c
typedef struct {
    uint8_t  id;
    uint16_t timeout_ms;
    uint8_t  retry_count;
    uint8_t  priority;
} TaskConfig;

// 初始化一个结构体数组
TaskConfig config_table[] = {
    {.id = 1, .timeout_ms = 100, .retry_count = 3, .priority = 2},
    {.id = 2, .timeout_ms = 200, .retry_count = 5, .priority = 1},
    {.id = 3, .timeout_ms = 50,  .retry_count = 1, .priority = 3},
};

// 获取数组元素个数
size_t task_count = sizeof(config_table) / sizeof(config_table[0]);
```text

遍历结构体数组的方式和普通数组一样，可以用下标也可以用指针：

```c
/// @brief 按优先级查找最高优先级任务的 ID
uint8_t find_highest_priority(const TaskConfig* tasks, size_t count) {
    uint8_t max_priority = 0;
    uint8_t result_id = 0;

    for (size_t i = 0; i < count; i++) {
        if (tasks[i].priority > max_priority) {
            max_priority = tasks[i].priority;
            result_id = tasks[i].id;
        }
    }
    return result_id;
}
```text

结构体数组在内存中的布局是紧密排列的——每个元素的大小为 `sizeof(TaskConfig)`（包含填充），第 i 个元素的地址就是 `base + i * sizeof(TaskConfig)`。这也是为什么结构体末尾需要填充的原因——如果不填充，数组中第二个元素的字段就可能不对齐。

## `__attribute__((packed))`：取消填充

有些场景下我们确实需要结构体没有任何填充——最典型的就是二进制通信协议。MCU 通过 UART/SPI/I2C 收到的数据是紧凑排列的字节流，如果结构体有填充，你直接强转指针去解读就会读到错误的值。GCC 和 Clang 提供了 `__attribute__((packed))` 来取消填充：

```c
typedef struct __attribute__((packed)) {
    uint8_t  header;
    uint16_t length;
    uint8_t  command;
    uint32_t parameter;
} PackedFrame;
```text

加了这个属性后，`sizeof(PackedFrame)` 就是纯粹的 1 + 2 + 1 + 4 = 8 字节，没有任何填充。但要注意代价——访问未对齐的字段在某些架构上会导致性能下降甚至硬件异常。所以 `packed` 应该只在你确实需要紧凑布局的时候使用，而不是到处乱加。ARM Cortex-M 系列在大多数情况下能处理未对齐访问（有性能损失），但有些老架构（比如 ARM7TDMI）会直接 fault。

一个更安全的做法是：**通信层用 packed 结构体解析原始字节，然后立即转换成对齐的内部结构体来使用**。解析和业务逻辑分离，各取所需。

## C++ 衔接

### struct 到 class 的演进

在 C 语言里，`struct` 只能包含数据成员——没有成员函数，没有访问控制，没有继承。C++ 保留了 `struct` 关键字，但赋予了它和 `class` 几乎相同的能力。唯一的区别在于默认访问权限：`struct` 的成员默认是 `public` 的，`class` 的成员默认是 `private` 的。除此之外，C++ 的 `struct` 可以有构造函数、析构函数、成员函数、继承、虚函数——什么都能做。

```cpp
// C++ 中的 struct——可以有成员函数
struct SensorReading {
    uint32_t timestamp;
    float temperature;
    float humidity;

    // 成员函数
    bool is_overheating() const {
        return temperature > 85.0f;
    }

    void print() const {
        printf("T=%.1fC H=%.1f%%\n", temperature, humidity);
    }
};
```text

所以你在 C++ 代码里看到 `struct`，不要以为它跟 C 语言的结构体一样——它就是一个默认 public 的 class。

### POD 类型与 trivially copyable

C++ 对"和 C 语言兼容的简单结构体"有一个专门的概念：POD 类型（Plain Old Data）。简单来说，如果一个结构体没有虚函数、没有非平凡的构造/析构函数、所有成员都是 POD 类型，那它本身就是 POD。POD 类型可以用 `memcpy` 安全地复制、可以用 `memset` 清零、可以安全地做二进制序列化和反序列化——因为它的内存布局和 C 语言完全一致。

C++11 之后，POD 的概念被细化成了几个更精确的类型特征：`is_trivially_copyable`、`is_standard_layout` 等。理解这些概念在跨语言交互（C/C++ 混编）、二进制序列化、共享内存通信中非常重要。

### std::aligned_storage

C++ 标准库提供了 `std::aligned_storage`（C++11 起，C++23 起被 `alignas` 替代），它是一个类型特性工具，用于手动控制一块原始内存的对齐。在实现类型擦除容器、内存池、placement new 等高级场景中会用到：

```cpp
#include <type_traits>

// 分配一块 64 字节对齐的原始内存
alignas(64) std::byte storage[sizeof(MyStruct)];

// 或者使用 std::aligned_storage（C++23 前的做法）
using AlignedStorage = std::aligned_storage_t<sizeof(MyStruct), alignof(MyStruct)>;
```text

这些概念在后续的 C++ 章节中会详细讨论。这里只需要知道：C 语言中对齐控制的思路，在 C++ 中被更系统化、更安全地实现了。

## 小结

我们在这篇教程里把结构体从"怎么用"到"内存里长什么样"彻底拆了一遍。结构体是 C 语言中最核心的复合类型，理解它的内存布局——尤其是对齐和填充——是写出高效、正确、可移植代码的基础。

### 关键要点

- [ ] 结构体用 `typedef struct { ... } Name;` 定义，搭配指针用 `->` 访问成员
- [ ] C99 指定初始化器 `.field = value` 比顺序初始化更安全、更可读
- [ ] 编译器会在成员间和结构体尾部插入填充字节，确保每个成员对齐
- [ ] 按对齐要求从大到小排列字段可以减少填充，节省内存
- [ ] `offsetof` 宏可以精确验证字段的偏移量
- [ ] C11 的 `alignas`/`alignof` 提供了标准化的对齐控制能力
- [ ] 柔性数组成员用于变长尾部数据，必须通过指针和动态分配使用
- [ ] `__attribute__((packed))` 取消填充，用于二进制协议解析，但有性能和可移植性代价
- [ ] C++ 的 `struct` 是默认 public 的 `class`，POD 类型保持与 C 兼容的内存布局

## 练习

### 练习：设计一个手动对齐控制的通信协议帧

请设计一个用于嵌入式设备通信的二进制协议帧结构。要求如下：

1. 帧头包含 1 字节的起始标志 `0xAA`、1 字节的帧类型、2 字节的载荷长度、4 字节的时间戳
2. 载荷部分为变长数据（使用柔性数组成员）
3. 帧尾包含 2 字节的 CRC16 校验
4. 使用 `_Alignas` 确保时间戳字段 4 字节对齐
5. 使用 `__attribute__((packed))` 确保帧结构紧凑（适合直接强转解析字节流）
6. 编写一个函数，使用 `offsetof` 打印每个字段的偏移量来验证布局

```c
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// TODO: 定义 Frame 结构体
// typedef struct __attribute__((packed)) {
//     ...
// } Frame;

// TODO: 实现 print_frame_layout() 函数
// 使用 offsetof 打印每个字段的偏移量

// TODO: 实现 create_frame() 函数
// 分配内存并填充帧数据（含柔性数组成员）

int main(void) {
    print_frame_layout();

    // TODO: 创建一个测试帧并验证偏移
    return 0;
}
```text

提示：在 packed 结构体中使用 `alignas` 需要注意——packed 会取消自动填充，但 `alignas` 可以强制某个字段的对齐。思考一下：在 packed 结构体中，如果帧头到时间戳之间恰好不是 4 的倍数偏移，你该怎么处理？

## 参考资源

- [C struct - cppreference](https://en.cppreference.com/w/c/language/struct)
- [C11 alignas/alignof - cppreference](https://en.cppreference.com/w/c/language/alignment)
- [offsetof - cppreference](https://en.cppreference.com/w/c/types/offsetof)
- [Flexible array members - cppreference](https://en.cppreference.com/w/c/language/struct)
