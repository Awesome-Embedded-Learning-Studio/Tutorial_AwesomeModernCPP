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
reading_time_minutes: 50
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
```

注意结尾那个分号——忘了它是新手最常见的编译错误之一，而且报错信息通常指向下一行，让你一头雾水。`struct SensorReading` 现在就是一个类型名了，但每次都写 `struct SensorReading` 确实有点啰嗦，所以我们通常会搭配 `typedef` 来简化：

```c
typedef struct {
    uint32_t timestamp;
    float temperature;
    float humidity;
    uint8_t status;
} SensorReading;
```

这样我们就可以直接写 `SensorReading reading;` 来声明变量了，清爽很多。两种写法在功能上是等价的，区别只在于类型名的使用方式：前者需要带着 `struct` 前缀，后者不需要。在实际项目中，`typedef` 的用法更为普遍，尤其是在嵌入式开发里——你去看任何一个 MCU 厂商的 SDK，满眼都是 `typedef struct` 的身影。

### 初始化与赋值

结构体有几种初始化方式，我们从最基础的开始。第一种是顺序初始化——按字段定义的顺序依次给出值：

```c
SensorReading r1 = {1700000000, 23.5f, 60.0f, 1};
```

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
```

指定初始化器的好处非常明显：代码自文档化，不依赖字段顺序，未指定的字段自动清零。说实话，在现代 C 代码中，只要你用的编译器支持 C99（基本上都支持），就应该优先使用指定初始化器。

结构体的赋值和初始化是两回事。初始化发生在声明时，赋值发生在声明之后。C 语言允许同类型的结构体之间直接赋值，赋值后各个成员的值与源对象对应成员相同：

```c
SensorReading r4;
r4 = r2;  // 把 r2 的所有字段复制到 r4
```

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
```

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
```

这种 `const SensorReading*` 和 `SensorReading*` 的区分，在 C++ 里会被继承到 `const` 成员函数和引用语义中，形成更完整的"只读 vs 可变"接口设计。

## 第二步——理解内存对齐和填充字节

接下来我们要进入这篇教程最核心也最容易让人迷惑的部分了。先来看一个问题：下面这个结构体占多少字节？

```c
typedef struct {
    uint8_t  a;   // 1 字节
    uint32_t b;   // 4 字节
    uint8_t  c;   // 1 字节
} WeirdLayout;
```

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
```

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
```

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
```

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
```

在没有额外对齐说明时，大多数 ABI 会让结构体的对齐要求不低于其成员中最严格的一项。`WeirdLayout` 里有 `uint32_t`，因此当前实验环境中整体对齐要求是 4；需要跨平台确认时，仍应以 `alignof(WeirdLayout)` 的结果为准。

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
```

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
```

在数组中也可以使用指定初始化器：

```c
// 稀疏初始化——只初始化需要的下标
uint8_t lookup[256] = {
    ['A'] = 1,
    ['B'] = 2,
    ['C'] = 3,
    // 其余全部为 0
};
```

这种写法在做 ASCII 字符映射表、命令分发表的时候特别方便，比起手写 256 个元素的初始化列表要清晰得多。未指定的元素会被自动初始化为零（和全局变量一样）。

## 第三步——了解柔性数组成员

柔性数组成员（Flexible Array Member，简称 FAM）是 C99 引入的一个特性，它允许在结构体的末尾放置一个大小未指定的数组。听起来有点奇怪，但它的用途非常实际——当你需要一个结构体带有一段"可变长度的尾随数据"时，FAM 是最干净的做法。

```c
typedef struct {
    uint16_t length;
    uint8_t  type;
    uint8_t  data[];  // 柔性数组成员，不占结构体大小
} Packet;
```

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
```

柔性数组成员在通信协议、变长消息处理、数据包解析中用得非常多。在 C 的早期年代，人们用一种叫做"struct hack"的技巧来实现类似功能——在结构体末尾放一个长度为 1（或 0）的数组，然后多分配一些空间。但那是未定义行为，C99 的 FAM 才是标准做法。

有一点需要注意：含柔性数组成员的结构体**可以**按值传递、赋值或返回，但这些操作只复制固定成员，不会复制尾随数据。协议帧和变长消息通常仍应通过指针传递，或者显式分配目标缓冲区并复制完整字节范围；否则很容易误以为 payload 也一起被复制了。

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
```

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
```

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
```

加了这个属性后，`sizeof(PackedFrame)` 就是纯粹的 1 + 2 + 1 + 4 = 8 字节，没有任何填充。但要注意代价——访问未对齐的字段在某些架构上会导致性能下降甚至硬件异常。所以 `packed` 应该只在你确实需要紧凑布局的时候使用，而不是到处乱加。ARM Cortex-M 系列在大多数情况下能处理未对齐访问（有性能损失），但有些老架构（比如 ARM7TDMI）会直接 fault。

`packed` 只约束布局，既不处理大端、小端，也不会让任意 `uint8_t` 接收缓冲区都能安全地强转为结构体指针。更稳妥的做法是：通信层按字节读取，或先 `memcpy` 到对齐良好的临时对象后再逐字段解码；随后转换成自然对齐的内部结构体供业务代码使用。解析和业务逻辑分离，各取所需。

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
```

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
```

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

### 练习 1：对齐预测与验证

**难度：基础** · 先手算 sizeof 和 offsetof，再写程序验证

假设 `int` 对齐为 4、`sizeof(int) == 4`，先手算下面三个结构体里每个字段的 `offsetof` 和整个结构体的 `sizeof`，再写一段程序用 `offsetof` 和 `sizeof` 打印出来对一对：

```c
#include <stddef.h>
#include <stdio.h>

typedef struct {
    char  a;
    int   b;
    char  c;
} StructA;

typedef struct {
    int   b;
    char  a;
    char  c;
} StructB;

typedef struct {
    char  a;
    char  c;
    int   b;
} StructC;
```

想一下：三个结构体字段完全相同、只是顺序不同，`sizeof` 为什么不一样？哪个最省空间？

::: details 参考答案

先把三个结构体补成完整程序，用 `offsetof` 和 `sizeof` 验证手算结果：

```c
#include <stddef.h>
#include <stdio.h>

typedef struct {
    char a;
    int b;
    char c;
} StructA;

typedef struct {
    int b;
    char a;
    char c;
} StructB;

typedef struct {
    char a;
    char c;
    int b;
} StructC;

int main(void)
{
    printf("StructA: a=%zu, b=%zu, c=%zu, sizeof=%zu\n",
           offsetof(StructA, a), offsetof(StructA, b),
           offsetof(StructA, c), sizeof(StructA));
    printf("StructB: a=%zu, b=%zu, c=%zu, sizeof=%zu\n",
           offsetof(StructB, a), offsetof(StructB, b),
           offsetof(StructB, c), sizeof(StructB));
    printf("StructC: a=%zu, b=%zu, c=%zu, sizeof=%zu\n",
           offsetof(StructC, a), offsetof(StructC, b),
           offsetof(StructC, c), sizeof(StructC));
    return 0;
}
```

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic alignment_verify.c -o alignment_verify && ./alignment_verify
```

在 `int` 为 4 字节、按 4 字节对齐的测试环境中，输出如下：

```text
StructA: a=0, b=4, c=8, sizeof=12
StructB: a=4, b=0, c=5, sizeof=8
StructC: a=0, b=4, c=1, sizeof=8
```

手算结果（`int` 4 字节对齐）：

| 结构体 | a 偏移 | b 偏移 | c 偏移 | sizeof |
|--------|--------|--------|--------|--------|
| StructA | 0 | 4 | 8 | 12 |
| StructB | 4 | 0 | 5 | 8 |
| StructC | 0 | 4 | 1 | 8 |

StructA 里 `a` 在 0、`b` 要 4 字节对齐所以前面补 3 字节填到偏移 4、`c` 在 8、尾部再补到 12。把大对齐的字段（`int`）和小字段（`char`）各自集中放，能减少中间填充。StructB 和 StructC 都是 8 字节，比 StructA 的 12 字节省。

:::

### 练习 2：packed 与显式对齐对比

**难度：进阶** · 对比默认对齐、packed、_Alignas 三种布局

对同一组字段，分别用默认对齐、`__attribute__((packed))`、`_Alignas` 三种方式定义结构体，打印 `sizeof` 和各字段偏移，看三种布局有什么不同：

```c
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    uint8_t  type;
    uint32_t value;
} FrameNormal;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint32_t value;
} FramePacked;

typedef struct {
    uint8_t  type;
    _Alignas(4) uint32_t value;
} FrameAligned;
```

想一想：`FramePacked` 最省空间，但为什么在有些 CPU 上访问 `value` 字段反而更慢、甚至直接崩？什么场景该用 packed，什么场景该用显式对齐？

::: details 参考答案

下面的程序一次把三种布局打印出来。`__attribute__((packed))` 是 GCC/Clang 扩展，所以这里使用 GCC 编译：

```c
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t type;
    uint32_t value;
} FrameNormal;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t value;
} FramePacked;

typedef struct {
    uint8_t type;
    _Alignas(4) uint32_t value;
} FrameAligned;

int main(void)
{
    printf("FrameNormal: type=%zu, value=%zu, sizeof=%zu\n",
           offsetof(FrameNormal, type), offsetof(FrameNormal, value),
           sizeof(FrameNormal));
    printf("FramePacked: type=%zu, value=%zu, sizeof=%zu\n",
           offsetof(FramePacked, type), offsetof(FramePacked, value),
           sizeof(FramePacked));
    printf("FrameAligned: type=%zu, value=%zu, sizeof=%zu\n",
           offsetof(FrameAligned, type), offsetof(FrameAligned, value),
           sizeof(FrameAligned));
    return 0;
}
```

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic packed_compare.c -o packed_compare && ./packed_compare
```

运行结果：

```text
FrameNormal: type=0, value=4, sizeof=8
FramePacked: type=0, value=1, sizeof=5
FrameAligned: type=0, value=4, sizeof=8
```

| 结构体 | `type` 偏移 | `value` 偏移 | `sizeof` |
|---|---:|---:|---:|
| `FrameNormal` | 0 | 4 | 8 |
| `FramePacked` | 0 | 1 | 5 |
| `FrameAligned` | 0 | 4 | 8 |

`FramePacked` 的问题就在 `value`：它从偏移 1 开始，不能保证落在 4 字节边界上。对 x86 这类通常支持未对齐访问的 CPU，处理器往往还能把值读出来，但可能需要额外的访存操作；如果数据刚好跨过缓存行，代价会更明显。换到对齐要求严格的 CPU，编译器要么把一次读取拆成多次字节读取来绕开问题，要么生成的未对齐读取会触发硬件异常。尤其不要把 `&frame.value` 当作普通的 `uint32_t*` 传给别的接口，这个指针的对齐条件已经不可靠了。

所以 `packed` 不是一个"省空间就加上"的开关。它适合必须逐字节匹配外部格式的地方，例如通信协议帧、文件头或 Flash 中的固定记录；而且它只解决布局，不会自动处理大端、小端。工程上更稳妥的做法是：收发层按字节读取或用 `packed` 描述外部格式，完成长度和字节序检查后，立刻复制、解码到正常对齐的内部结构体，再交给业务代码使用。热路径数据、结构体数组、原子变量，以及 DMA 缓冲区等对访问效率或硬件约束敏感的对象，都不该为了省几个字节贸然打包。

`_Alignas` 则是反过来的工具：它是在 C11 中显式保证对象或成员满足较高的对齐要求。在这个例子里，`uint32_t` 本来就自然按 4 字节对齐，所以 `FrameAligned` 和 `FrameNormal` 的布局相同；它的价值在于把约束写在代码里，例如 DMA 缓冲区需要 32 字节对齐，或某个成员必须从指定边界开始。记住这一条就够了：外部字节格式优先考虑 `packed`，内存中的日常数据优先保持自然对齐；两者之间用一次明确的解析和转换隔开，后面的代码就不用一直背着对齐风险跑。

:::

### 练习 3：通信协议帧设计（挑战·可选）

**难度：挑战** · 可选，需要了解 CRC 与字节序，新手可跳过

设计一个用于嵌入式设备通信的二进制协议帧结构：帧头（起始符、帧类型、载荷长度、时间戳）、变长载荷（柔性数组成员）、帧尾校验。

- 用 `offsetof` 打印每个字段偏移，验证布局符合预期
- 帧尾的校验字段可以先预留 2 字节占位、写一句 `// TODO: 填 CRC16`，不必现在就实现 CRC 算法
- 想一下：不同字节序（大端、小端）的设备通信时，多字节字段（比如时间戳）该怎么处理？

提示：本篇讲过柔性数组成员、`_Alignas`、`__attribute__((packed))`、`offsetof`，这些都是你的工具。CRC 算法和字节序转换属于通信专题，这里只要求你意识到这两个问题、留好占位，不要求完整实现。

::: details 参考答案

题面只要求预留校验字段，下面给出一份完整的 CRC8/CRC16 与协议帧实现，供学有余力的读者对照。完整程序分为三个文件：

**crc_ref.h**

```c
#ifndef CRC_REF_H
#define CRC_REF_H

#include <stdint.h>
#include <stdbool.h>


void Append_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength);

bool Verify_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength);


uint8_t Get_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength,
                           uint8_t ucCRC8);


void Append_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength);


bool Verify_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength);


uint16_t Get_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength,
                             uint16_t wCRC);

#endif


```

**crc_ref.c**

```c

#include "crc_ref.h"

#include <stddef.h>

/* CRC 查表和接口命名改编自 RoboMaster 官方例程；为便于对照保留原命名。 */


//查表法CRC

//CRC8 使用的初始值。
static const uint8_t k_crc8_init = 0xffu;

// CRC8 预计算状态转移表。
// 每轮使用 current_crc ^ next_byte 作为索引，表项就是折叠该字节后的下一 CRC 状态。
static const uint8_t k_crc8_tab[256] = {
    0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 0xc2, 0x9c, 0x7e, 0x20,
    0xa3, 0xfd, 0x1f, 0x41, 0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e,
    0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc, 0x23, 0x7d, 0x9f, 0xc1,
    0x42, 0x1c, 0xfe, 0xa0, 0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62,
    0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 0x7c, 0x22, 0xc0, 0x9e,
    0x1d, 0x43, 0xa1, 0xff, 0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5,
    0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07, 0xdb, 0x85, 0x67, 0x39,
    0xba, 0xe4, 0x06, 0x58, 0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a,
    0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 0xa7, 0xf9, 0x1b, 0x45,
    0xc6, 0x98, 0x7a, 0x24, 0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b,
    0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9, 0x8c, 0xd2, 0x30, 0x6e,
    0xed, 0xb3, 0x51, 0x0f, 0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd,
    0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92, 0xd3, 0x8d, 0x6f, 0x31,
    0xb2, 0xec, 0x0e, 0x50, 0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c,
    0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee, 0x32, 0x6c, 0x8e, 0xd0,
    0x53, 0x0d, 0xef, 0xb1, 0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73,
    0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 0x08, 0x56, 0xb4, 0xea,
    0x69, 0x37, 0xd5, 0x8b, 0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4,
    0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16, 0xe9, 0xb7, 0x55, 0x0b,
    0x88, 0xd6, 0x34, 0x6a, 0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8,
    0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7, 0xb6, 0xe8, 0x0a, 0x54,
    0xd7, 0x89, 0x6b, 0x35,
};

//系统整帧 CRC16 使用的初始值。
static const uint16_t k_crc16_init = 0xffffu;

// CRC16 预计算状态转移表。
static const uint16_t k_crc16_tab[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 0x8c48,
    0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7, 0x1081, 0x0108,
    0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e, 0x9cc9, 0x8d40, 0xbfdb,
    0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876, 0x2102, 0x308b, 0x0210, 0x1399,
    0x6726, 0x76af, 0x4434, 0x55bd, 0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e,
    0xfae7, 0xc87c, 0xd9f5, 0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e,
    0x54b5, 0x453c, 0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd,
    0xc974, 0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3, 0x5285,
    0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a, 0xdecd, 0xcf44,
    0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72, 0x6306, 0x728f, 0x4014,
    0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9, 0xef4e, 0xfec7, 0xcc5c, 0xddd5,
    0xa96a, 0xb8e3, 0x8a78, 0x9bf1, 0x7387, 0x620e, 0x5095, 0x411c, 0x35a3,
    0x242a, 0x16b1, 0x0738, 0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862,
    0x9af9, 0x8b70, 0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e,
    0xf0b7, 0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036, 0x18c1,
    0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e, 0xa50a, 0xb483,
    0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5, 0x2942, 0x38cb, 0x0a50,
    0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd, 0xb58b, 0xa402, 0x9699, 0x8710,
    0xf3af, 0xe226, 0xd0bd, 0xc134, 0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7,
    0x6e6e, 0x5cf5, 0x4d7c, 0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1,
    0xa33a, 0xb2b3, 0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72,
    0x3efb, 0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a, 0xe70e,
    0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1, 0x6b46, 0x7acf,
    0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9, 0xf78f, 0xe606, 0xd49d,
    0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330, 0x7bc7, 0x6a4e, 0x58d5, 0x495c,
    0x3de3, 0x2c6a, 0x1ef1, 0x0f78,
};

//计算一段连续字节的 CRC8。
uint8_t Get_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength,
                           uint8_t ucCRC8)
{
  uint8_t ucIndex = 0u;

  // 每轮消耗 1 字节，并把该字节折叠进当前 CRC 状态。
  while (dwLength-- != 0u)
  {
    ucIndex = ucCRC8 ^ (*pchMessage++);
    ucCRC8 = k_crc8_tab[ucIndex];
  }
  return ucCRC8;
}

//校验缓冲区末尾已保存的 CRC8。
bool Verify_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength)
{
  uint8_t expected = 0u;

  // 系统帧头至少要包含有效字段和 1 字节 CRC8。
  if ((pchMessage == NULL) || (dwLength <= 2u))
  {
    return false;
  }

  //计算窗口排除末尾已经保存的 CRC8 字节。
  expected = Get_CRC8_Check_Sum(pchMessage, (uint16_t)(dwLength - 1u),
                                k_crc8_init);
  return expected == pchMessage[dwLength - 1u];
}

//把重新计算得到的 CRC8 写入缓冲区末尾。
void Append_CRC8_Check_Sum(uint8_t *pchMessage, uint16_t dwLength)
{
  // 不向空指针或长度不足的缓冲区写入。
  if ((pchMessage == NULL) || (dwLength <= 2u))
  {
    return;
  }

  //校验字节本身不参与本轮 CRC8 计算。
  pchMessage[dwLength - 1u] =
      Get_CRC8_Check_Sum(pchMessage, (uint16_t)(dwLength - 1u), k_crc8_init);
}

//计算一段连续字节的 CRC16。
uint16_t Get_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength,
                             uint16_t wCRC)
{
  uint8_t chData = 0u;

  if (pchMessage == NULL)
  {
    return 0xffffu;
  }

  // 将每个输入字节折叠到 CRC 低位，再通过查表推进到下一状态。
  while (dwLength-- != 0u)
  {
    chData = *pchMessage++;
    wCRC = (uint16_t)((wCRC >> 8u) ^
                      k_crc16_tab[(uint8_t)(wCRC ^ chData)]);
  }
  return wCRC;
}

//校验完整帧末尾 2 字节保存的 CRC16。

bool Verify_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength)
{
  uint16_t expected = 0u;

  //完整帧至少要包含受保护数据和 2 字节 CRC16。
  if ((pchMessage == NULL) || (dwLength <= 2u))
  {
    return false;
  }

  //只重算受保护的载荷/帧头区域，末尾 CRC16 不参与计算。
  expected = Get_CRC16_Check_Sum(pchMessage, dwLength - 2u, k_crc16_init);
  return (((expected & 0x00ffu) == pchMessage[dwLength - 2u]) &&
          (((expected >> 8u) & 0x00ffu) == pchMessage[dwLength - 1u]));
}

//把 CRC16 追加到完整帧缓冲区末尾 2 字节。
void Append_CRC16_Check_Sum(uint8_t *pchMessage, uint32_t dwLength)
{
  uint16_t wCRC = 0u;

  //不向空指针或长度不足的帧缓冲区写入。
  if ((pchMessage == NULL) || (dwLength <= 2u))
  {
    return;
  }

  //小端协议：低字节在前，高字节在后。
  wCRC = Get_CRC16_Check_Sum(pchMessage, dwLength - 2u, k_crc16_init);
  pchMessage[dwLength - 2u] = (uint8_t)(wCRC & 0x00ffu);
  pchMessage[dwLength - 1u] = (uint8_t)((wCRC >> 8u) & 0x00ffu);
}


```

**protocol_frame.c**

```c
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc_ref.h"

#define FRAME_START_BYTE 0xA5u /* 固定帧头，用于接收端寻找一帧的开始。 */
#define FRAME_CRC16_SIZE 2u    /* CRC16 占两个线上字节。 */

/*
 * 线上帧格式（所有偏移量均相对于 protocol_frame_t 起始地址）：
 *
 *   偏移 0       start              帧头，1 字节
 *   偏移 1       type               所属分类，1 字节
 *   偏移 2..3    payload_length     payload 长度，2 字节，小端序
 *   偏移 4..7    timestamp          时间戳，4 字节，小端序
 *   偏移 8       header_crc8        帧头 CRC8，1 字节
 *   偏移 9..     payload            可变长度的业务数据
 *   最后 2 字节  crc16              整帧 CRC16，低字节在前
 *
 * payload 是协议中的可变长度区域。柔性数组成员必须是结构体的最后一个成员，
 * 因此 CRC16 尾部通过同一次分配的额外空间放在 payload 后面。多字节字段使用
 * 字节数组保存，再显式按协议字节序读写，避免结构体填充和主机字节序影响线上格式。
 */
typedef struct {
    uint8_t start;
    uint8_t type;
    uint8_t payload_length[2];
    uint8_t timestamp[4];
    uint8_t header_crc8;
    uint8_t payload[];
} protocol_frame_t;

enum {
    FRAME_START_OFFSET = offsetof(protocol_frame_t, start), // 帧头偏移
    FRAME_TYPE_OFFSET = offsetof(protocol_frame_t, type), // 帧类型偏移
    FRAME_PAYLOAD_LENGTH_OFFSET = offsetof(protocol_frame_t, payload_length), // 帧负载长度偏移
    FRAME_TIMESTAMP_OFFSET = offsetof(protocol_frame_t, timestamp), // 时间戳偏移
    FRAME_HEADER_CRC8_OFFSET = offsetof(protocol_frame_t, header_crc8), // 帧头 CRC8 偏移
    FRAME_PAYLOAD_OFFSET = offsetof(protocol_frame_t, payload), // 可变长度数据的偏移
    FRAME_HEADER_SIZE = offsetof(protocol_frame_t, payload) // 固定线上帧头长度
};

/*
 * 按指定字节序把 16 位整数写入线上缓冲区。
 *
 * big_endian == true：大端序，高字节在前；
 * big_endian == false：小端序，低字节在前。
 * C 语言没有可省略的默认参数，因此 false 是调用者应使用的默认选择，
 * 必须在调用处显式传入。当前协议的调用点统一传入 false，使用小端序。
 */
static void write_u16(uint8_t *destination, uint16_t value,
                         bool big_endian) {
    if (big_endian) {
        destination[0] = (uint8_t)(value >> 8u);
        destination[1] = (uint8_t)value;
    } else {
        destination[0] = (uint8_t)value;
        destination[1] = (uint8_t)(value >> 8u);
    }
}

/* 按 big_endian 选择的字节序把 32 位整数写入线上缓冲区。 */
static void write_u32(uint8_t *destination, uint32_t value,
                         bool big_endian) {
    if (big_endian) {
        destination[0] = (uint8_t)(value >> 24u);
        destination[1] = (uint8_t)(value >> 16u);
        destination[2] = (uint8_t)(value >> 8u);
        destination[3] = (uint8_t)value;
    } else {
        destination[0] = (uint8_t)value;
        destination[1] = (uint8_t)(value >> 8u);
        destination[2] = (uint8_t)(value >> 16u);
        destination[3] = (uint8_t)(value >> 24u);
    }
}

/* 按 big_endian 选择的字节序从线上缓冲区读取 16 位整数。 */
static uint16_t read_u16(const uint8_t *source, bool big_endian) {
    if (big_endian) {
        return (uint16_t)(((uint16_t)source[0] << 8u) |
                          (uint16_t)source[1]);
    }

    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8u));
}

/* 按 big_endian 选择的字节序从线上缓冲区读取 32 位整数。 */
static uint32_t read_u32(const uint8_t *source, bool big_endian) {
    if (big_endian) {
        return ((uint32_t)source[0] << 24u) |
               ((uint32_t)source[1] << 16u) |
               ((uint32_t)source[2] << 8u) |
               (uint32_t)source[3];
    }

    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8u) |
           ((uint32_t)source[2] << 16u) |
           ((uint32_t)source[3] << 24u);
}

/*
 * 组装一帧完整的线上数据。
 *
 * 函数先检查长度和指针，再分配空间；之后按协议序列化字段、复制 payload，
 * 最后计算 CRC8 和 CRC16。这样 CRC 覆盖的是最终真正要发送的字节。
 */
static bool data_process(uint8_t type, uint32_t timestamp,
                        const uint8_t *payload, size_t payload_size,
                        protocol_frame_t **frame_out,
                        uint32_t *frame_size_out) {
    size_t calculated_frame_size; // 计算的帧大小
    protocol_frame_t *frame; // 分配的帧缓冲区

    /* 输出参数无效时不能写回结果。 */
    if ((frame_out == NULL) || (frame_size_out == NULL)) {
        return false;
    }

    *frame_out = NULL;
    *frame_size_out = 0u;

    /* payload_length 只有 16 位，超出后强制转换会发生截断。 */
    if (payload_size > UINT16_MAX) {
        fputs("Payload is too large for the 16-bit length field.\n", stderr);
        return false;
    }
    /* 只有空 payload 才允许 payload 指针为 NULL。 */
    if ((payload == NULL) && (payload_size != 0u)) {
        fputs("Payload pointer is NULL for a non-empty payload.\n", stderr);
        return false;
    }
    /* 先检查加法，再计算总长度，防止 size_t 溢出。 */
    if (payload_size > SIZE_MAX - sizeof(protocol_frame_t) - FRAME_CRC16_SIZE) {
        fputs("Frame size calculation overflowed size_t.\n", stderr);
        return false;
    }

    calculated_frame_size =
        sizeof(protocol_frame_t) + payload_size + FRAME_CRC16_SIZE;
    /* payload_size 已限制为 uint16_t，故完整帧最大为 65546 字节，可安全转为 uint32_t。 */

    frame = calloc(1u, calculated_frame_size);
    if (frame == NULL) {
        fputs("Failed to allocate protocol frame.\n", stderr);
        return false;
    }

    /* 写入固定字段：多字节普通字段统一使用小端序。 */
    frame->start = FRAME_START_BYTE;
    frame->type = type;
    /* 协议规定 payload_length 使用小端序，因此这里明确传入 false。 */
    write_u16(frame->payload_length, (uint16_t)payload_size, false);
    /* 协议规定 timestamp 使用小端序，因此这里明确传入 false。 */
    write_u32(frame->timestamp, timestamp, false);
    frame->header_crc8 = 0u;

    /* payload 是可变长度区域，紧接在固定帧头之后。 */
    if (payload_size != 0u) {
        memcpy(frame->payload, payload, payload_size);
    }

    /* CRC8 覆盖前 8 个帧头字节，并把结果写入偏移 8。 */
    Append_CRC8_Check_Sum((uint8_t *)frame, (uint16_t)FRAME_HEADER_SIZE);
    /* CRC16 计算覆盖帧头和 payload；计算结果写入帧尾预留的两个 CRC16 字节。 */
    Append_CRC16_Check_Sum((uint8_t *)frame, (uint32_t)calculated_frame_size);

    *frame_out = frame;
    *frame_size_out = (uint32_t)calculated_frame_size;
    return true;
}

static void print_layout(void) {
    printf("start offset:          %zu\n", (size_t)FRAME_START_OFFSET);
    printf("type offset:           %zu\n", (size_t)FRAME_TYPE_OFFSET);
    printf("payload_length offset: %zu\n", (size_t)FRAME_PAYLOAD_LENGTH_OFFSET);
    printf("timestamp offset:      %zu\n", (size_t)FRAME_TIMESTAMP_OFFSET);
    printf("header_crc8 offset:    %zu\n", (size_t)FRAME_HEADER_CRC8_OFFSET);
    printf("payload offset:        %zu\n", (size_t)FRAME_PAYLOAD_OFFSET);
}

int main(void) {
    uint8_t crc_test_vector[] = "123456789";
    /* 示例 payload；实际项目中可替换为待发送的业务数据。 */
    const uint8_t sample_payload[] = {0x10u, 0x20u, 0x30u, 0x40u};
    const size_t payload_size = sizeof(sample_payload);
    protocol_frame_t *frame = NULL;
    uint32_t frame_size = 0u;
    const uint8_t *crc16;

    /* type=0x01 表示一个示例业务分类，时间戳固定为 0x12345678。 */
    if (data_process(0x01u, 0x12345678u, sample_payload,
                     payload_size, &frame, &frame_size) == false) {
        return EXIT_FAILURE;
    }

    /* CRC16 位于 payload 后面，因此它的起始偏移随 payload 长度变化。 */
    crc16 = &frame->payload[payload_size];

    /* 固定测试向量锁定当前 CRC 参数，防止查找表或初值被误改。 */
    if ((Get_CRC8_Check_Sum(crc_test_vector, 9u, 0xffu) != 0x0bu) ||
        (Get_CRC16_Check_Sum(crc_test_vector, 9u, 0xffffu) != 0x6f91u)) {
        fputs("CRC test vector check failed.\n", stderr);
        free(frame);
        return EXIT_FAILURE;
    }

    print_layout();
    printf("CRC test vector:       passed (CRC-8=0x0B, CRC-16=0x6F91)\n");
    printf("crc16 offset:          %u\n",
           (unsigned int)(crc16 - (const uint8_t *)frame));
    printf("total frame size:      %u bytes\n", (unsigned int)frame_size);
    /* 通过解码函数读取线上字段，验证字节序与数值均正确。 */
    printf("payload length:        %u\n",
           (unsigned int)read_u16(frame->payload_length, false));
    printf("timestamp:             0x%08X\n",
           (unsigned int)read_u32(frame->timestamp, false));
    printf("timestamp wire bytes:  %02X %02X %02X %02X\n",
           (unsigned int)frame->timestamp[0],
           (unsigned int)frame->timestamp[1],
           (unsigned int)frame->timestamp[2],
           (unsigned int)frame->timestamp[3]);
    printf("crc8 check:            %s\n",
           Verify_CRC8_Check_Sum((uint8_t *)frame,
                                 (uint16_t)FRAME_HEADER_SIZE)
               ? "passed"
               : "failed");
    printf("crc16 check:           %s\n",
           Verify_CRC16_Check_Sum((uint8_t *)frame, frame_size)
               ? "passed"
               : "failed");

    free(frame);
    return EXIT_SUCCESS;
}


```

把三个文件放在同一目录后编译运行：

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic protocol_frame.c crc_ref.c -o protocol_frame && ./protocol_frame
```

这组测试数据的输出如下：

```text
start offset:          0
type offset:           1
payload_length offset: 2
timestamp offset:      4
header_crc8 offset:    8
payload offset:        9
CRC test vector:       passed (CRC-8=0x0B, CRC-16=0x6F91)
crc16 offset:          13
total frame size:      15 bytes
payload length:        4
timestamp:             0x12345678
timestamp wire bytes:  78 56 34 12
crc8 check:            passed
crc16 check:           passed
```

这里没有把 `uint16_t` 或 `uint32_t` 直接塞进线上结构体，而是把多字节字段保存成字节数组，再由 `write_u16`、`write_u32` 明确序列化；当前协议调用它们时传入 `false`，固定使用小端序。这样结构体填充和主机字节序都不会偷偷改变协议格式，接收端也能用对应的读取函数还原数值。

固定线上帧头长度取 `offsetof(protocol_frame_t, payload)`，而不是把 `sizeof(protocol_frame_t)` 当成协议长度。柔性数组成员本身不计入 `sizeof`，但标准允许结构体在末尾保留额外填充；用成员偏移计算线上头长，才能准确找到 payload 真正开始的位置。分配内存时仍使用 `sizeof(protocol_frame_t)`，这样连同可能的尾部填充也有足够空间；CRC16 则放在同一次分配中、紧跟 payload 的两个额外字节里。

> ⚠️ **踩坑预警**
> CRC 不只是一个“CRC16”名字：多项式、初值、输入/输出反射、最终异或值和线上字节序共同决定结果。本例的 CRC-8 使用多项式 `0x31`、初值 `0xFF`、反射输入/输出、最终异或值 `0x00`；CRC-16 使用多项式 `0x1021`、初值 `0xFFFF`、反射输入/输出、最终异或值 `0x0000`，并按小端写入帧尾。`"123456789"` 的固定测试结果分别为 `0x0B` 和 `0x6F91`，用于防止示例内部参数被误改；接入真实设备时，仍必须同协议文档或设备给出的测试帧逐项核对，不能只看位数相同就直接复用。

:::

## 参考资源

- [C struct - cppreference](https://en.cppreference.com/w/c/language/struct)
- [C11 alignas/alignof - cppreference](https://en.cppreference.com/w/c/language/_Alignas)
- [offsetof - cppreference](https://en.cppreference.com/w/c/types/offsetof)
- [Flexible array members - cppreference](https://en.cppreference.com/w/c/language/struct)
