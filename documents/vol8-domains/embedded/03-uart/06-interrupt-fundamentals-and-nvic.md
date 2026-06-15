---
chapter: 17
difficulty: intermediate
order: 6
platform: stm32f1
reading_time_minutes: 9
tags:
- cpp-modern
- intermediate
- stm32f1
title: 第36篇：中断基础与 NVIC —— 让硬件主动通知 CPU
description: ''
---
# 第36篇：中断基础与 NVIC —— 让硬件主动通知 CPU

> 上一篇我们发现了阻塞式接收的致命缺陷。这一篇开始搭建解决方案：先搞清楚 Cortex-M3 的中断机制是怎么工作的。

---

## 从轮询到中断：范式的转换

在按钮教程的最后一篇（第 30 篇）中，我们简单介绍过 EXTI 外部中断。那篇文章讲的是"引脚电平变化触发中断"的场景。现在我们需要把中断的理解提升一个层次——因为 UART 中断驱动接收比 EXTI 按钮检测复杂得多，涉及 ISR 和主循环之间的数据传递、缓冲区管理、回调链路等。

先回顾一下两种编程范式的本质区别。

**轮询**：CPU 主动检查外设状态。"有数据了吗？没有。有数据了吗？没有。有数据了吗？有了！"CPU 在忙等——虽然可以通过做其他事来填补等待时间（就像我们的按钮状态机那样），但检查本身需要 CPU 时间。

**中断**：外设在需要 CPU 关注时主动发信号。"我有数据了，请处理。"CPU 在收到信号之前可以专心做其他事。信号到来时，硬件自动暂停当前任务，跳转到预设的处理函数，处理完毕后返回。

打个比方。轮询就像你每隔 5 分钟去看一次邮箱——不管有没有信你都得跑一趟。中断就像邮递员按门铃——没信的时候你可以安心在家做别的事，信来了门铃会响。

---

## Cortex-M3 的中断硬件

STM32F103 使用的是 ARM Cortex-M3 内核，它的中断系统由两部分组成：NVIC（嵌套向量中断控制器）和向量表。

### NVIC

NVIC 是 Cortex-M3 内核内置的中断控制器，负责管理所有中断源的优先级、使能和挂起状态。STM32F103 有 60 个可屏蔽中断通道（加上 16 个 Cortex-M3 内核异常），每个通道都有独立的中断向量。

NVIC 的关键特性：

- **嵌套**：高优先级中断可以打断低优先级中断。如果 USART1 中断正在处理中，一个更高优先级的中断（比如 SysTick）可以抢占它。处理完高优先级中断后，返回继续处理 USART1 中断。
- **向量**：每个中断源都有自己的入口函数（中断服务函数，ISR）。中断触发时，硬件自动跳转到对应的 ISR，不需要软件判断"是哪个中断源触发的"。
- **自动上下文保存/恢复**：中断触发时，CPU 自动把当前寄存器状态（r0-r3、r12、LR、PC、xPSR）压入栈。ISR 返回时自动弹出。你不需要手写保存/恢复寄存器的代码。

### 向量表

向量表是一个函数指针数组，存储在 Flash 的起始位置（默认地址 0x00000000）。每个中断源在表中占据一个固定位置——表中的第 N 个条目对应第 N 个中断源的 ISR 地址。当第 N 号中断触发时，CPU 从表中读取第 N 个条目的地址，然后跳转过去执行。

USART1 的中断号是 `USART1_IRQn`（值为 37）。向量表中第 37 个位置存储的就是 `USART1_IRQHandler` 函数的地址。这个函数名不是随意的——它必须和向量表中的位置严格对应。链接器根据函数名把它放到正确的位置。

---

## USART1 中断的工作原理

现在我们把中断的通用机制具体到 USART1 的场景。

### 触发条件：RXNE 标志

上一篇讲过 SR 寄存器中的 RXNE（Read Data Register Not Empty）标志。当 USART1 的接收移位寄存器把一个完整的字节移入 RDR 时，RXNE 自动置 1。这就是中断的触发条件。

但 RXNE 置 1 不等于中断会触发。还需要两个条件同时满足：

1. **RXNEIE = 1**：CR1 寄存器中的 RXNE 中断使能位。这个位由软件设置，表示"当 RXNE 置 1 时请触发中断"。
2. **NVIC 中 USART1 IRQ 使能**：NVIC 中对应的 USART1_IRQn 中断通道必须被使能。这通过 `HAL_NVIC_EnableIRQ(USART1_IRQn)` 完成。

三个条件（RXNE 置 1 + RXNEIE 使能 + NVIC 使能）同时满足时，CPU 才会跳转到 `USART1_IRQHandler`。

### HAL_UART_Receive_IT 做了什么

HAL 库提供了一个便捷的函数来设置中断接收：

```c
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);
```

这个函数内部做了三件事：

1. 把 `pData` 指针和 `Size` 存到 `huart` 结构体中（HAL 内部会用它们来跟踪接收进度）
2. 设置 RXNEIE 位（使能接收中断）
3. 返回 `HAL_OK`

注意：这个函数不会阻塞。它只是"设置好了接收条件"，然后立即返回。实际的接收发生在中断触发后——当新字节到达时，ISR 自动被调用，ISR 内部的 HAL 代码把字节存到 `pData` 指向的缓冲区，递减剩余计数，当接收完 `Size` 个字节后调用 `HAL_UART_RxCpltCallback()` 回调。

### 单字节接收策略

我们的代码使用了一个关键的策略：每次只接收 1 个字节。

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/system/uart_irq.cpp
std::byte rx_byte{};

void restart_receive() {
    [[maybe_unused]] auto r =
        Manager::driver().receive_it(std::span<std::byte, 1>{&rx_byte, 1});
}
```

`HAL_UART_Receive_IT(&huart, &rx_byte, 1)` 意思是："请设置接收 1 个字节的中断。收到 1 个字节后告诉我。"

收到 1 个字节后，HAL 调用 `HAL_UART_RxCpltCallback()`。在回调中，我们把这 1 个字节存进环形缓冲区，然后立即调用 `restart_receive()` 再设置一次单字节接收。这样循环往复，就实现了连续的、不丢失字节的接收流：

```text
restart_receive()
  → 等待字节...
  → 字节到达，ISR 触发
  → HAL_UART_IRQHandler()
  → HAL_UART_RxCpltCallback()
    → push(rx_byte) 到环形缓冲区
    → restart_receive()
      → 等待下一个字节...
      → （循环）
```

为什么不用"一次接收 N 个字节"？因为 UART 是字节流协议——你不知道发送方什么时候发完、发多少字节。如果设"一次接收 10 个字节"，那收到 3 个字节后对方停了，你的接收就卡在那了。单字节策略最灵活——每收到一个字节就处理一个，不会有任何"等待凑齐"的问题。

---

## extern "C" ISR 桥接

我们的项目是一个 C++ 项目，但 ISR 函数名（如 `USART1_IRQHandler`）必须用 C 链接来定义。原因是向量表中存的是 C 符号名——链接器根据未修饰的函数名来填充向量表。如果 C++ 编译器对 `USART1_IRQHandler` 做了 name mangling，链接器就找不到正确的函数了。

所以 ISR 定义必须放在 `extern "C"` 块中：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/system/uart_irq.cpp
extern "C" {

void USART1_IRQHandler(void) {
    HAL_UART_IRQHandler(Manager::handle());
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart->Instance == USART1) {
        rx_ring.push(rx_byte);
        restart_receive();
    }
}

} // extern "C"
```

`extern "C"` 保证了这两个函数在符号表中以原名出现，链接器能正确地把它们放入向量表。函数内部的代码依然是 C++——你可以调用 C++ 函数、使用 C++ 类型、访问 C++ 命名空间中的成员。`extern "C"` 只影响链接规则，不影响编译规则。

这种"C 链接 + C++ 实现"的模式在嵌入式 C++ 项目中非常常见。任何需要被 C 接口调用的函数（ISR、回调、系统调用如 `_write()`）都需要 `extern "C"` 包装。

---

## NVIC 优先级配置

在我们的代码中，NVIC 配置封装在 `enable_interrupt()` 方法中：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
void enable_interrupt() {
    if constexpr (INSTANCE == UartInstance::Usart1) {
        HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
    // ...
}
```

`HAL_NVIC_SetPriority(USART1_IRQn, 0, 0)` 的两个参数分别是抢占优先级和子优先级。设为 (0, 0) 表示最高优先级——USART1 中断可以打断任何其他中断（除了 NMI 等不可屏蔽异常）。

在简单的项目中（只有 USART 中断和 SysTick），优先级设最高没问题。在复杂的项目中，如果多个中断源竞争 CPU 时间，你需要仔细规划优先级。一般原则是：对实时性要求最高的中断优先级最高。UART 接收（不及时处理可能丢数据）通常比 LED 控制（晚几毫秒人眼也看不出来）优先级更高。

---

## 中断处理的黄金法则

在深入具体的 ISR 实现之前，先记住一条嵌入式开发的黄金法则：

> **ISR 必须尽可能短。**

ISR 执行期间，同级和低级中断被屏蔽。如果你的 ISR 执行时间太长（比如在 ISR 里做复杂的计算、调用 `printf()`、等待超时），其他中断可能被延迟响应甚至丢失。对于 USART 接收来说，如果 ISR 处理上一个字节的时候下一个字节已经到了，而 RXNE 还没被清零，就会触发 ORE（Overrun Error）——上一个字节丢失了。

我们的 ISR 实现遵循了"短 ISR"原则：`USART1_IRQHandler` 委托给 HAL，HAL 清除中断标志、读取数据、调用回调，回调中只做两件事——把字节 push 到环形缓冲区（O(1) 操作），然后 restart 下一轮接收。整个流程在几个微秒内完成，远小于 115200 baud 下一个字节的传输时间（87 微秒）。

---

## 小结

这一篇搭建了中断驱动接收的理论基础：Cortex-M3 的 NVIC 和向量表机制、USART1 RXNE 中断的触发条件、`HAL_UART_Receive_IT()` 的工作方式、单字节接收策略、`extern "C"` 桥接模式，以及 ISR 必须尽可能短的原则。

但还有一块关键拼图没解决：ISR 收到字节后怎么传给主循环？直接用全局变量？用数组？下一篇我们要设计一个专门为 ISR-to-main 通信优化的数据结构——无锁环形缓冲区。
