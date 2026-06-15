---
chapter: 17
difficulty: beginner
order: 3
platform: stm32f1
reading_time_minutes: 9
tags:
- beginner
- cpp-modern
- stm32f1
title: 第33篇：STM32 USART 外设 —— 芯片内部的串口引擎
description: ''
---
# 第33篇：STM32 USART 外设 —— 芯片内部的串口引擎

> 承接上一篇：我们搞清楚了 UART 协议的帧格式、波特率、过采样。现在该看看 STM32F103 芯片内部是怎么实现这个协议的了。

---

## USART vs UART：多出来的 S 代表什么

你可能注意到了，STM32F103 的参考手册里写的是 USART（Universal Synchronous/Asynchronous Receiver/Transmitter），比 UART 多了一个"S"——Synchronous（同步）。这意味着 STM32 的这个外设不仅能做异步 UART 通信，还能在同步模式下工作——多一根时钟线（SCLK）输出，用来给外部设备提供同步时钟。另外 USART 还支持智能卡（SmartCard）模式、IrDA（红外）模式和 LIN（局域互联网络）模式。

不过在我们的教程中，只使用异步模式（也就是标准 UART）。其他模式在特定应用场景下有用，但对理解 UART 通信的核心机制来说并非必需。所以我们虽然用的是 USART 外设，但把它当 UART 用。

STM32F103C8T6 有三个 USART 实例：USART1、USART2 和 USART3。它们的区别主要在于挂载的总线不同：

- **USART1** 挂在 APB2 总线上。APB2 是高速总线，在我们代码中运行在 64 MHz。USART1 支持的最高波特率也最高（可达 4.5 Mbps at 72 MHz）
- **USART2 和 USART3** 挂在 APB1 总线上。APB1 是低速总线，在我们代码中运行在 32 MHz。最高波特率相对较低

我们选择 USART1 的原因很简单：它挂在高频总线上，波特率灵活度更高；而且 USART1 的默认引脚（PA9/PA10）在 Blue Pill 的排针上很好找。这一点在代码中也有所体现——`UartInstance` 枚举中直接使用了 USART1 的基地址：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_config.hpp
enum class UartInstance : uintptr_t {
    Usart1 = USART1_BASE,
    Usart2 = USART2_BASE,
    Usart3 = USART3_BASE,
};
```

这里用了一个很巧妙的做法：枚举值直接存的是 USART 外设在内存映射中的基地址。`USART1_BASE` 在 STM32 头文件中定义为 `0x40013800`——这是 USART1 所有寄存器的起始地址。后面我们在 C++ 模板驱动中会看到，这个基地址可以直接 `reinterpret_cast` 成 `USART_TypeDef*` 指针来访问所有寄存器。

---

## USART 的关键寄存器

STM32F103 的 USART 外设有 7 个寄存器。我们不逐位拆解每个寄存器（那是参考手册的工作），而是聚焦在实际编程中最常用的几个标志和字段。

### SR —— 状态寄存器（Status Register）

SR 寄存器反映了 USART 当前的工作状态。最重要的几个标志位：

- **TXE（Transmit Data Register Empty）**：发送数据寄存器为空。当上一个数据从 TDR（发送数据寄存器）移入了移位寄存器正在发送时，TXE 置 1，表示"可以写入下一个数据了"。`HAL_UART_Transmit()` 内部就是轮询等待这个标志。
- **TC（Transmission Complete）**：发送完成。当移位寄存器中的数据全部发送完毕且 TDR 也是空的，TC 置 1。比 TXE 更严格——TXE 只表示"可以写下一个数据了"，TC 表示"所有数据都发完了"。
- **RXNE（Read Data Register Not Empty）**：接收数据寄存器非空。当移位寄存器把接收到的数据移入了 RDR（接收数据寄存器），RXNE 置 1，表示"有新数据可以读了"。这个标志在中断驱动的接收中扮演核心角色——当 RXNE 置 1 时，如果 RXNEIE（RXNE 中断使能）被打开，CPU 就会被中断。
- **ORE（Overrun Error）**：溢出错误。上一个数据还没读走，新的数据又到了，旧数据被覆盖。说明你的代码读取数据的速度不够快。

### DR —— 数据寄存器（Data Register）

DR 寄存器实际上由两个独立的寄存器组成——TDR（发送）和 RDR（接收），它们共享同一个地址。当你往 DR 写数据时，数据进入 TDR 触发发送；当你从 DR 读数据时，数据来自 RDR。读和写操作在硬件层面自动路由到正确的内部寄存器，你的代码只需要记住"发送就写 DR，接收就读 DR"。

### BRR —— 波特率寄存器（Baud Rate Register）

BRR 存储了分频值，USART 用它来生成正确的波特率。BRR 由两部分组成：12 位的整数部分（Mantissa）和 4 位的小数部分（Fraction）。对于 16x 过采样模式：

```text
BRR = fCK / BaudRate
```

其中 `fCK` 是 USART 挂载总线的时钟频率。USART1 在 APB2 上，所以 `fCK` = 64 MHz（我们的配置）。整数部分是 BRR 的整数位，小数部分是 BRR 的小数位乘以 16。这个计算由 HAL 库的 `HAL_UART_Init()` 内部完成，你只需要在 `UART_InitTypeDef` 中设置 `BaudRate` 字段，HAL 会自动计算 BRR。

### CR1/CR2/CR3 —— 控制寄存器

三个控制寄存器管理 USART 的工作模式：

**CR1** 是最重要的一个，包含了：

- **UE（USART Enable）**：USART 总使能开关。不置位，USART 不工作。
- **TE（Transmitter Enable）**：发送使能。
- **RE（Receiver Enable）**：接收使能。
- **RXNEIE**：RXNE 中断使能。置位后，当 RXNE = 1 时会触发中断。这就是中断驱动接收的关键开关。
- **TXEIE**：TXE 中断使能。用于中断驱动的发送。
- **M（Word Length）**：数据位长度。0 = 8 位，1 = 9 位。
- **PCE（Parity Control Enable）**：校验使能。
- **PS（Parity Selection）**：校验类型。0 = 偶校验，1 = 奇校验。

**CR2** 主要管理停止位长度（STOP 位域，00 = 1 stop bit，10 = 2 stop bits）和时钟输出配置（同步模式用）。

**CR3** 管理硬件流控制（CTSE/RTSE）、DMA 使能（DMAT/DMAR）和一些特殊模式（智能卡、IrDA、LIN）。

---

## 时钟使能

USART 外设默认是不使能的——为了省电。使用之前必须先打开对应的总线时钟。USART1 在 APB2 上，所以调用：

```c
__HAL_RCC_USART1_CLK_ENABLE();
```

USART2 和 USART3 在 APB1 上，分别调用 `__HAL_RCC_USART2_CLK_ENABLE()` 和 `__HAL_RCC_USART3_CLK_ENABLE()`。

这和 LED 教程中使能 GPIO 时钟是同一个模式：STM32 的所有外设在复位后时钟都是关闭的，你需要手动打开。HAL 库的 `__HAL_RCC_xxx_CLK_ENABLE()` 宏本质上就是往 RCC（Reset and Clock Control）寄存器的对应位写 1。

在我们的 C++ 代码中，这个时钟使能被封装在 `UartDriver` 模板的 `enable_clock()` 私有方法里，通过 `if constexpr` 在编译时选择正确的宏：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
static inline void enable_clock() {
    if constexpr (INSTANCE == UartInstance::Usart1) {
        __HAL_RCC_USART1_CLK_ENABLE();
    } else if constexpr (INSTANCE == UartInstance::Usart2) {
        __HAL_RCC_USART2_CLK_ENABLE();
    } else if constexpr (INSTANCE == UartInstance::Usart3) {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
}
```

`if constexpr` 在编译时就确定了走哪个分支——最终编译出来的代码只包含对应的那一行宏调用，没有运行时条件判断的开销。

---

## GPIO 复用功能：PA9 和 PA10 的特殊身份

在 LED 教程中，GPIO 配置为推挽输出（`GPIO_MODE_OUTPUT_PP`）或输入（`GPIO_MODE_INPUT`）。但 USART1 的 TX 引脚 PA9 需要配置为**复用功能推挽输出**（`GPIO_MODE_AF_PP`），这是一个之前没见过的模式。

为什么需要复用功能？因为 PA9 不是一个普通的 GPIO 引脚——当 USART1 的发送器使能后，USART 外设会直接控制 PA9 的电平输出，而不是由 GPIO 的 ODR（输出数据寄存器）控制。换句话说，PA9 的输出控制权从 GPIO 模块转移到了 USART 模块。`GPIO_MODE_AF_PP` 就是告诉 GPIO 控制器："这个引脚的输出由外设（AF = Alternate Function）管理，你不要管了。"

PA10 作为 USART1 的 RX 引脚，配置为输入模式带上拉（`GPIO_MODE_INPUT` + `GPIO_PULLUP`）。这和按钮教程中的输入配置一样——上拉电阻确保 RX 线在空闲时保持高电平，和 UART 协议的空闲状态一致。

我们的 `main.cpp` 中，GPIO 初始化封装在一个独立的函数里：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
static void usart1_gpio_init() noexcept {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}
```

这段代码先使能 GPIOA 时钟（PA9 和 PA10 都在 GPIOA 上），然后分别配置 PA9 为复用推挽输出、PA10 为上拉输入。`gpio.Speed = GPIO_SPEED_FREQ_HIGH` 设置 PA9 的输出翻转速率——高速模式确保在 115200 baud 下的信号边沿足够陡峭。

注意这个函数被声明为 `noexcept`。在 C++ 驱动设计中，GPIO 初始化不应该抛出异常（我们的项目本身就禁用了异常）。后面第 41 篇讲 Concepts 时你会看到，`UartGpioInitializer` Concept 会通过 `std::is_nothrow_invocable_v` 在编译时强制检查这一点。

---

## NVIC 连接预览

USART1 有自己的中断向量 `USART1_IRQn`。当 USART1 的 RXNE 标志置位（收到了新字节）且 RXNEIE 被使能时，如果 NVIC 中 USART1 的中断也被使能了，CPU 就会暂停当前任务，跳转到 `USART1_IRQHandler` 函数执行。

NVIC 的配置在 `uart_driver.hpp` 的 `enable_interrupt()` 方法中：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
void enable_interrupt() {
    if constexpr (INSTANCE == UartInstance::Usart1) {
        HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    } else if constexpr (INSTANCE == UartInstance::Usart2) {
        HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    } else if constexpr (INSTANCE == UartInstance::Usart3) {
        HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART3_IRQn);
    }
}
```

两步：设置优先级（抢占优先级 0、子优先级 0——最高优先级），然后使能 IRQ。这和按钮教程中 EXTI 中断的 NVIC 配置是同一个模式。

中断的完整工作流程——从硬件触发到字节进入环形缓冲区——会在第 36 到 38 篇详细拆解。现在你只需要知道：USART1 有自己的中断通道，配置好 NVIC 和 RXNEIE 之后，每收到一个字节就会触发一次中断。

---

## 小结

这一篇我们搞清楚了 STM32 USART 外设的硬件架构：三个 USART 实例的区别、关键寄存器（SR/DR/BRR/CR1/CR2/CR3）的作用、GPIO 复用功能引脚的配置方式，以及 NVIC 中断连接的预览。这些知识是下一篇写代码的基础——知道硬件是怎么工作的，写起代码来就知道每一步在做什么。

下一篇，我们要正式动工了。HAL 库的 UART 初始化流程、阻塞式发送、第一次在终端里看到芯片说 "Hello"——这些就是第 34 篇的内容。
