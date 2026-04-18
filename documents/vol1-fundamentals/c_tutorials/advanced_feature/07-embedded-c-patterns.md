---
title: "嵌入式 C 编程模式"
description: "寄存器访问模式、volatile 正确使用、中断安全编程、外设抽象层设计与裸机开发模式"
chapter: 1
order: 107
tags:
  - host
  - cpp-modern
  - intermediate
  - 嵌入式
  - 单片机
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard: [11, 17]
prerequisites:
  - "结构体、联合体与内存对齐"
  - "函数指针与回调机制"
  - "指针进阶：多级指针、指针与 const"
---

# 嵌入式 C 编程模式

写桌面程序的时候，我们基本上不需要关心编译器会不会偷偷把一次内存读操作给优化掉、或者两段代码会不会在同一时刻踩同一块数据。但一旦你把目光投向裸机——没有操作系统、没有标准库、甚至没有 `main` 的标准入口——这些问题就全冒出来了。嵌入式 C 编程有一套自己的模式语言：寄存器用结构体映射，硬件状态必须用 `volatile` 保护，中断和主循环之间的数据交换需要精心设计的同步机制。

这篇教程我们把这些模式逐一拆开。理解这些模式对后续学 C++ 的嵌入式应用——`constexpr` 寄存器配置、零开销抽象、类型安全的硬件访问——都是必要的前置知识。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 掌握三种寄存器访问模式（位操作、结构体映射、原子访问）
> - [ ] 正确使用 `volatile` 限定符并理解其语义边界
> - [ ] 实现中断安全的数据交换模式
> - [ ] 设计分层的外设抽象层
> - [ ] 理解裸机程序的启动流程和链接脚本

## 环境说明

本文代码以 ARM Cortex-M 为目标平台，但所有概念和模式同样适用于其他架构。主机上可以用交叉编译器验证编译：

```text
平台：ARM Cortex-M3/M4（STM32F1/F4 等）
编译器：arm-none-eabi-gcc >= 10
主机验证：gcc -Wall -Wextra -std=c11（非硬件相关代码）
依赖：无
```

## 第一步——搞清楚怎么和硬件寄存器打交道

嵌入式开发最根本的操作就是读写硬件寄存器——那些映射到内存地址空间的外设控制端口。我们来看三种从原始到优雅的访问模式。

### 位操作：最原始也最灵活

外设寄存器的每一个 bit 都有独立含义。比如 GPIO 端口的模式寄存器，可能低 2 位控制模式（输入/输出/复用/模拟），接下来 2 位控制上拉下拉。先来定义一套通用的位操作宏，几乎所有嵌入式项目都有一份类似的工具头文件：

```c
// bit_ops.h — 通用位操作工具
#define BIT_SET(reg, n)       ((reg) |=  (1U << (n)))
#define BIT_CLEAR(reg, n)     ((reg) &= ~(1U << (n)))
#define BIT_TOGGLE(reg, n)    ((reg) ^=  (1U << (n)))
#define BIT_READ(reg, n)      (((reg) >> (n)) & 1U)

// 写入字段：将 reg 中 [high:low] 区间写入 val
#define FIELD_WRITE(reg, val, high, low) \
    do { \
        uint32_t mask = ~(((1U << ((high) - (low) + 1)) - 1) << (low)); \
        (reg) = ((reg) & mask) | (((val) & ((1U << ((high) - (low) + 1)) - 1)) << (low)); \
    } while (0)
```

> ⚠️ **踩坑预警**
> 宏参数里的 `reg` 如果是带副作用的表达式（比如 `*ptr++`），会被多次求值。在生产代码中更推荐用 `static inline` 函数替代，但宏版本在嵌入式代码库里太普及了，你需要能看懂。

来看这套宏怎么配置一个假想的 GPIO 端口。假设 GPIOA 基地址是 `0x40020000`，偏移 `0x00` 是模式寄存器 `MODER`，每 2 位控制一个引脚：

```c
#define GPIOA_BASE   0x40020000U
#define GPIOA_MODER  (*(volatile uint32_t*)(GPIOA_BASE + 0x00))
#define GPIOA_ODR    (*(volatile uint32_t*)(GPIOA_BASE + 0x14))

// 将 PA5 配置为输出模式（bit[11:10] = 01）
void gpioa_pin5_output_enable(void)
{
    uint32_t moder = GPIOA_MODER;
    moder &= ~(3U << 10);   // 清除 bit[11:10]
    moder |=  (1U << 10);   // 设置为 01（输出）
    GPIOA_MODER = moder;
}

void gpioa_pin5_set(void)   { BIT_SET(GPIOA_ODR, 5); }
void gpioa_pin5_clear(void) { BIT_CLEAR(GPIOA_ODR, 5); }
```

注意那个 `*(volatile uint32_t*)` 强制转换——`volatile` 告诉编译器：这个地址的值随时可能被硬件改变，每次读写都必须真正访问内存，不能缓存或优化掉。

### 结构体映射：让寄存器有名字

直接用地址偏移和位操作能干活，但可读性很差——谁能一眼看出 `*(uint32_t*)(0x40020000 + 0x14)` 是 GPIOA 的输出数据寄存器？结构体映射是更优雅的方案：

```c
typedef struct {
    volatile uint32_t MODER;    // 偏移 0x00
    volatile uint32_t OTYPER;   // 偏移 0x04
    volatile uint32_t OSPEEDR;  // 偏移 0x08
    volatile uint32_t PUPDR;    // 偏移 0x0C
    volatile uint32_t IDR;      // 偏移 0x10
    volatile uint32_t ODR;      // 偏移 0x14
    volatile uint32_t BSRR;     // 偏移 0x18
    volatile uint32_t LCKR;     // 偏移 0x1C
    volatile uint32_t AFRL;    // 偏移 0x20
    volatile uint32_t AFRH;    // 偏移 0x24
} GpioReg;

#define GPIOA  ((GpioReg*) 0x40020000U)
#define GPIOB  ((GpioReg*) 0x40020400U)
```

现在配置代码变得非常清晰：`GPIOA->MODER &= ~(3U << 10); GPIOA->MODER |= (1U << 10);`。

结构体映射有一个隐含前提：内存布局必须和硬件寄存器布局完全一致。大多数 ARM 外设寄存器是 32 位对齐排列的，和 `uint32_t` 的自然对齐完全匹配。如果遇到寄存器之间有保留空间的，得在结构体里加 `volatile uint32_t RESERVED0` 占位——Cortex-M 的 CMSIS 头文件就是这么做的。

### 原子访问：BSRR 模式

前面配置引脚用的是"读-改-写"三步。这在没有中断干扰时没问题，但如果在"读"和"写"之间来了一个中断，中断里也修改了同一个寄存器——你的"写"就会覆盖掉中断的修改。这就是经典的读-改-写竞争。

有些外设提供了原子操作寄存器来解决此问题。STM32 GPIO 的 BSRR 就是一个典型——低 16 位写 1 置位对应引脚，高 16 位写 1 清除，写 0 无影响。直接写就完事，硬件保证原子性：

```c
// 原子置位 PA5 和 PA6
GPIOA->BSRR = (1U << 5) | (1U << 6);
// 原子清除 PA7
GPIOA->BSRR = (1U << (7 + 16));
```

如果硬件没有这种原子操作寄存器，就只能靠关中断来保护临界区了。

## 第二步——搞懂 volatile 到底做了什么和没做什么

`volatile` 可能是嵌入式 C 中被误解最深的关键字。

### volatile 做了什么

`volatile` 告诉编译器：对这个对象的每次访问都必须真正执行，不能被优化掉，也不能被重排到其他 `volatile` 访问的对面。具体来说：编译器不会把 `volatile` 变量的值缓存在寄存器中、不会优化掉看似"多余"的读写、不会重排 `volatile` 操作的顺序。

```c
// 没有 volatile——编译器可能优化掉整个循环
int* flag = (int*)0x20000000;
while (*flag == 0) {
    // 编译器可能只读一次 flag，然后死循环
}

// 加上 volatile——每次循环都会重新读取
volatile int* flag = (volatile int*)0x20000000;
while (*flag == 0) {
    // 编译器每次都生成内存读取指令
}
```

### volatile 没做什么（这更重要）

`volatile` **不是**线程同步工具。它**不保证**原子性，也**不阻止** CPU 的乱序执行。`volatile` 只约束编译器，不约束 CPU——ARM Cortex-M 对普通内存访问可以重排，所以两个 `volatile` 写操作在编译器看来是保序的，但 CPU 可能以不同顺序提交到总线。如果需要严格的内存顺序，必须用 DMB/DSB 等内存屏障指令。

另外，`volatile` 不保证读-改-写操作的原子性：

```c
volatile uint32_t counter;
counter++;  // 不是原子的！读、加、写三步
```

`counter++` 实际上是"读取、加 1、写回"三步操作。如果在读和写之间发生了中断，中断里也修改了 counter，就会丢失一次更新。

> ⚠️ **踩坑预警**
> `volatile` 的合理使用场景：硬件寄存器映射、中断与主循环之间共享的简单标志。不应该用 `volatile` 的场景：线程间同步（用 mutex 或 atomic）、大块数据传输（用 DMA）、任何需要原子读-改-写的场合。

## 第三步——掌握中断安全编程

中断是嵌入式系统的核心机制——硬件事件来了就打断当前执行流，跳到 ISR 里处理。问题在于 ISR 和主循环共享同一块内存空间，如果两者同时访问同一份数据，轻则数据损坏，重则系统跑飞。

### 临界区保护

最简单粗暴但有效的方法：在访问共享数据前关中断，操作完再开中断。这里用了一个嵌套计数器来支持临界区的嵌套：

```c
static volatile uint32_t s_critical_nesting = 0;

void critical_enter(void)
{
    __disable_irq();
    s_critical_nesting++;
}

void critical_exit(void)
{
    if (s_critical_nesting > 0) {
        s_critical_nesting--;
    }
    if (s_critical_nesting == 0) {
        __enable_irq();
    }
}
```

> ⚠️ **踩坑预警**
> 关中断是有代价的：在关中断期间，所有中断都被屏蔽，系统实时性下降。临界区必须尽可能短——进去、做必要的操作、马上出来。千万不要在临界区里调用阻塞函数或做复杂计算。

### 环形缓冲区：中断安全的经典数据结构

中断和主循环之间最常见的通信模式是"生产者-消费者"——中断往里写数据，主循环往外读。环形缓冲区是标准实现，妙处在于只要"写入"和"读取"分别只在一个上下文中执行，就不需要锁：

```c
#define RING_BUFFER_SIZE 64

typedef struct {
    volatile uint32_t head;     // 写入位置（ISR 修改）
    volatile uint32_t tail;     // 读取位置（主循环修改）
    uint8_t buffer[RING_BUFFER_SIZE];
} RingBuffer;

void ring_buffer_init(RingBuffer* rb)
{
    rb->head = 0;
    rb->tail = 0;
}

// ISR 中调用：只有 ISR 修改 head
uint32_t ring_buffer_write(RingBuffer* rb, uint8_t data)
{
    uint32_t next_head = (rb->head + 1) % RING_BUFFER_SIZE;
    if (next_head == rb->tail) {
        return 0;  // 缓冲区满
    }
    rb->buffer[rb->head] = data;
    rb->head = next_head;
    return 1;
}

// 主循环中调用：只有主循环修改 tail
uint32_t ring_buffer_read(RingBuffer* rb, uint8_t* data)
{
    if (rb->head == rb->tail) {
        return 0;  // 缓冲区空
    }
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    return 1;
}
```

关键约束是：`head` 只有写入方修改，`tail` 只有读取方修改。因为双方都只读对方的指针、只写自己的指针，所以不需要互斥锁。

### 中断处理的黄金法则

对于简单的"事件发生"通知，用一个 `volatile` 标志位就够了：

```c
static volatile uint8_t s_timer_flag = 0;

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR &= ~TIM_SR_UIF;
        s_timer_flag = 1;
    }
}

// 主循环
if (s_timer_flag) {
    s_timer_flag = 0;
    handle_timer_event();  // 重活在主循环处理
}
```

ISR 只做最少的操作——清除中断标志、设置应用层标志。这是中断处理的黄金法则：**ISR 尽可能短，把重活留给主循环**。

## 第四步——设计分层的外设抽象层

嵌入式项目如果直接在业务逻辑里操作寄存器地址，代码会变成一团无法移植的意大利面条。解决之道是引入外设抽象层（PAL），把硬件细节封装在底层驱动里。

### 三层架构

合理的分层通常是这样的：最底层是寄存器定义和位操作工具（和具体芯片绑定），中间层是外设驱动（GPIO、UART、SPI 等模块），最上层是应用逻辑（完全不碰寄存器）。中间层的接口设计要和具体芯片无关：

```c
// gpio_driver.h — 硬件无关的接口
typedef enum {
    kGpioModeInput  = 0,
    kGpioModeOutput = 1,
    kGpioModeAltFunc = 2,
    kGpioModeAnalog = 3
} GpioMode;

typedef struct {
    GpioReg* port;   // 指向 GPIO 端口的寄存器结构体
    uint8_t  pin;    // 引脚号 0-15
} GpioPin;

void gpio_init(const GpioPin* gpio, GpioMode mode, GpioPull pull);
void gpio_write(const GpioPin* gpio, bool value);
bool gpio_read(const GpioPin* gpio);
void gpio_toggle(const GpioPin* gpio);
```

```c
// gpio_driver.c — 实现细节
void gpio_init(const GpioPin* gpio, GpioMode mode, GpioPull pull)
{
    uint32_t moder = gpio->port->MODER;
    moder &= ~(3U << (gpio->pin * 2));
    moder |=  ((uint32_t)mode << (gpio->pin * 2));
    gpio->port->MODER = moder;

    uint32_t pupdr = gpio->port->PUPDR;
    pupdr &= ~(3U << (gpio->pin * 2));
    pupdr |=  ((uint32_t)pull << (gpio->pin * 2));
    gpio->port->PUPDR = pupdr;
}

void gpio_write(const GpioPin* gpio, bool value)
{
    if (value) {
        gpio->port->BSRR = (1U << gpio->pin);
    } else {
        gpio->port->BSRR = (1U << (gpio->pin + 16));
    }
}
```

上层应用完全不碰寄存器：

```c
static const GpioPin kLedPin = { GPIOA, 5 };

gpio_init(&kLedPin, kGpioModeOutput, kGpioPullNone);
gpio_toggle(&kLedPin);
```

换芯片时只需要改底层寄存器定义和中间层实现，上层应用代码完全不动。`GpioPin` 结构体把"哪个端口的哪个引脚"打包成一个可传递的对象，比到处传 `(GPIOA, 5)` 裸参数清晰得多。

## 第五步——理解裸机程序的启动流程

脱离操作系统，连 `main` 都不是第一个被执行的。理解裸机程序从上电到进入 `main` 的完整流程是基本功。

### 启动代码

ARM Cortex-M 上电后的流程：CPU 从向量表读取初始栈指针（第一个 32 位字）和复位向量（第二个 32 位字，即 Reset_Handler 地址），然后跳转到 Reset_Handler。Reset_Handler 做三件事：把 `.data` 段从 Flash 拷贝到 SRAM、把 `.bss` 段清零、调用 `main`。

```c
// startup.c — 最小启动代码（ARM Cortex-M）
extern uint32_t _estack;    // 栈顶地址（链接脚本定义）
extern uint32_t _sidata;    // .data 在 Flash 中的起始
extern uint32_t _sdata;     // .data 在 SRAM 中的起始
extern uint32_t _edata;     // .data 在 SRAM 中的结束
extern uint32_t _sbss;      // .bss 起始
extern uint32_t _ebss;      // .bss 结束

int main(void);

void default_handler(void) { while (1) {} }

__attribute__((section(".isr_vector")))
void (*const g_vector_table[])(void) = {
    (void (*)(void))(&_estack),    // 初始栈指针
    Reset_Handler,                  // Reset
    NMI_Handler,                    // NMI
    HardFault_Handler,              // Hard Fault
    default_handler,                // MemManage
    default_handler,                // BusFault
    default_handler,                // UsageFault
    0, 0, 0, 0,                    // 保留
    default_handler,                // SVCall
    default_handler,                // Debug Monitor
    0,                              // 保留
    default_handler,                // PendSV
    default_handler,                // SysTick
};

void Reset_Handler(void)
{
    // 1. 把 .data 段从 Flash 复制到 SRAM
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) { *dst++ = *src++; }

    // 2. 把 .bss 段清零
    dst = &_sbss;
    while (dst < &_ebss) { *dst++ = 0; }

    // 3. 进入 main
    main();
    while (1) {}  // 裸机 main 不应该返回
}

__attribute__((weak)) void NMI_Handler(void) { default_handler(); }
__attribute__((weak)) void HardFault_Handler(void) { default_handler(); }
```

> ⚠️ **踩坑预警**
> `_estack`、`_sdata` 这些符号不是真正的变量——它们是链接脚本里定义的地址标签。在 C 代码里用 `extern` 声明后，取地址就能得到对应段的起止位置。向量表用 `__attribute__((section(".isr_vector")))` 强制放在 Flash 开头，`__attribute__((weak))` 允许用户覆盖默认的中断处理函数。

### 链接脚本

链接脚本告诉链接器程序的内存布局——Flash 从哪到哪、SRAM 从哪到哪、各段分别放哪里。关键概念是 `> RAM AT > FLASH`——`.data` 段的运行地址在 RAM，但加载地址在 Flash。上电后启动代码把它拷贝到 RAM。`.bss` 段只有起始和结束地址，启动代码直接清零。

```ld
/* link.ld — Cortex-M3 最小链接脚本 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

_stack_size = 1024;

SECTIONS
{
    .isr_vector : {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text : {
        *(.text*) *(.rodata*)
        _etext = .;
    } > FLASH

    .data : {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);

    .bss : {
        _sbss = .;
        *(.bss*) *(COMMON)
        _ebss = .;
    } > RAM
}
```

## C++ 衔接

嵌入式 C++ 有几个重要约束：异常需要栈展开运行时支持，大多数裸机项目用 `-fno-exceptions` 禁用，改用返回值表示错误；RTTI（`dynamic_cast`/`typeid`）增加代码体积，通常用 `-fno-rtti` 禁用；裸机没有操作系统堆管理器，`new`/`delete` 默认不可用，推荐全部静态分配（`std::array` 替代 `std::vector`，固定大小容器和内存池替代动态分配）。

C++ 对嵌入式代码的改进主要集中在三个方面：

| C 模式 | C++ 改进 |
|--------|----------|
| 手动确保 init/cleanup 配对 | RAII 构造/析构自动管理 |
| 宏做位操作 | `constexpr` 编译期计算配置值 |
| 运行时查寄存器配置表 | 模板在编译期固化端口/引号常量，生成和手写一样高效的代码 |
| 函数指针 + `void*` 上下文 | `std::function` 或模板回调 |

`constexpr` 在嵌入式领域特别有价值——在编译期计算寄存器配置值，运行时直接写预计算好的常量，既消除运行时计算开销，也避免运行时出错的可能。本系列后续深入 C++ 嵌入式应用时，会详细展开 `constexpr` + 模板如何实现零开销的硬件抽象层。

## 常见陷阱速查

| 陷阱 | 说明 | 解决方法 |
|------|------|----------|
| `volatile` 当线程同步用 | `volatile` 不保证原子性和内存序 | 用原子操作或关中断保护 |
| 结构体映射忘记加 padding | 编译器 padding 和硬件布局不匹配 | 查手册加 `RESERVED` 字段 |
| ISR 里做太多事情 | 中断延迟增加、系统响应变慢 | ISR 只设标志，重活在主循环处理 |
| 读-改-写竞争 | 中断在读写间隙修改了同一个寄存器 | 用原子操作寄存器（BSRR）或关中断 |
| `main` 返回 | 裸机 `main` 返回后没有 OS 接手 | 启动代码里 `main()` 后加死循环 |

## 练习

### 练习 1：通用环形缓冲区

将文中的 `uint8_t` 环形缓冲区改造为通用版本（用 `void*` + 元素大小实现）：

```c
typedef struct {
    // 你需要设计内部字段
} RingBuffer;

/// @brief 初始化环形缓冲区
void ring_buffer_init(RingBuffer* rb, void* storage,
                       size_t item_size, size_t capacity);
/// @brief 写入一个元素
uint32_t ring_buffer_write(RingBuffer* rb, const void* item);
/// @brief 读取一个元素
uint32_t ring_buffer_read(RingBuffer* rb, void* item);
/// @brief 查询当前元素数量
uint32_t ring_buffer_count(const RingBuffer* rb);
```

提示：内部用 `memcpy` 做通用字节拷贝，`head`/`tail` 改为绝对计数（`uint32_t` 不怕溢出），实际索引通过 `count % capacity` 计算。

### 练习 2：可移植的 UART 抽象层

为 UART 外设设计一套和具体芯片无关的抽象层接口。驱动内部需要两个环形缓冲区（发送和接收），`uart_write` 先写缓冲区再触发发送中断，实际逐字节发送在 ISR 中完成。

```c
typedef struct { /* 你设计 */ } UartDriver;

void uart_init(UartDriver* uart, uint32_t baud,
               uint8_t* tx_buffer, uint8_t* rx_buffer, size_t buffer_size);
size_t uart_write(UartDriver* uart, const uint8_t* data, size_t len);
size_t uart_read(UartDriver* uart, uint8_t* data, size_t len);
void uart_irq_handler(UartDriver* uart);  // 在 ISR 中调用
```

### 练习 3：链接脚本与启动代码

写一个针对 ARM Cortex-M4（256K Flash, 64K SRAM）的最小链接脚本和启动代码。要求：定义正确的 MEMORY 区域、向量表放 Flash 开头、处理 `.data` 段地址分离、清零 `.bss`、在 `main` 后加安全死循环。

## 参考资源

- [ARM Cortex-M Programming Guide](https://developer.arm.com/documentation)
- [volatile 关键字 - cppreference](https://en.cppreference.com/w/c/language/volatile)
- [GCC Linker Script 参考](https://sourceware.org/binutils/docs/ld/Scripts.html)
- [CMSIS - Cortex 微控制器软件接口标准](https://arm-software.github.io/CMSIS_5/General/html/index.html)
