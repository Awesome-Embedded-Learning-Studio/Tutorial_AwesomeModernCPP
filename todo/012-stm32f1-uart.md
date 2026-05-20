---
id: 012
title: "STM32F1 UART 完整教程（阻塞/中断收发 + C++23 封装）"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on:
  - "architecture/002"
blocks: []
estimated_effort: large
---

# STM32F1 UART 完整教程

## 目标
编写 STM32F1 UART 外设的完整教程，覆盖 UART 协议基础（波特率、起始位、数据位、校验位、停止位）、HAL 库阻塞式收发、中断式收发、串口 Echo 实验、串口日志输出。使用 C++23 特性（std::span、std::expected、std::byte）进行面向对象的驱动封装。教程遵循 9 节模板结构。

## 验收标准
- [ ] 教程遵循 9 节模板：目标 -> 硬件原理 -> HAL 接口 -> 最小 Demo -> C++23 封装 -> 常见坑 -> 练习题 -> 可复用代码 -> 小结
- [ ] 硬件原理部分详细解释 UART 协议帧格式（波特率计算、起始位检测、数据位顺序、校验位算法、停止位数量）
- [ ] HAL 接口部分覆盖：`HAL_UART_Transmit`、`HAL_UART_Receive`、`HAL_UART_Transmit_IT`、`HAL_UART_Receive_IT`、`HAL_UART_TxCpltCallback`、`HAL_UART_RxCpltCallback`
- [ ] 最小 Demo 包含：阻塞式发送 "Hello" 字符串、阻塞式接收并回显、中断式 Echo 全双工
- [ ] C++23 封装包含：`UartDriver` 类（使用 std::span<const std::byte> 作为发送参数、std::expected<size_t, UartError> 作为返回值、std::byte 替代 uint8_t）
- [ ] 常见坑涵盖：中断重入问题、printf 重定向实现、缓冲区溢出、波特率误差计算
- [ ] 练习题包含：带超时的接收、环形缓冲区中断接收、命令解析器框架
- [ ] 可复用代码提供完整的头文件和源文件，可直接复制使用
- [ ] 所有代码在 STM32F103C8T6（Blue Pill）上验证通过

## 实施说明
### 9 节模板详细规划

**第 1 节：目标**
- 明确学习目标：理解 UART 协议、掌握 HAL 收发 API、能用 C++23 封装 UART 驱动

**第 2 节：硬件原理**
- UART 协议帧格式图示：起始位（1 bit）+ 数据位（7/8/9 bit）+ 校验位（None/Odd/Even）+ 停止位（1/1.5/2 bit）
- 波特率与时钟的关系：`BRR = fCK / (16 * BaudRate)` 的计算过程
- STM32F1 的 USART 外设概览：USART1（APB2, 72MHz）、USART2/3（APB1, 36MHz）
- GPIO 复用功能配置：TX 推挽复用输出、RX 浮空输入/上拉输入
- 硬件连接图：USB-TTL 转换器（CH340/CP2102）与 Blue Pill 的连接

**第 3 节：HAL 接口**
- UART 初始化结构体 `UART_HandleTypeDef` 详解（Init 成员的每个字段）
- 阻塞 API：`HAL_UART_Transmit(huart, pData, Size, Timeout)` / `HAL_UART_Receive`
- 中断 API：`HAL_UART_Transmit_IT` / `HAL_UART_Receive_IT`
- 回调函数：`HAL_UART_TxCpltCallback` / `HAL_UART_RxCpltCallback` 的弱函数覆盖
- 中断使能：`__HAL_UART_ENABLE_IT` 与 NVIC 配置
- CubeMX 配置步骤截图说明

**第 4 节：最小 Demo**
- Demo 1：阻塞式发送 "Hello UART!\r\n"
- Demo 2：阻塞式接收 1 字节并回显（串口 Echo）
- Demo 3：中断式接收 + 发送，实现非阻塞 Echo
- Demo 4：printf 重定向到 UART（重写 `_write` 或 `fputc`）

**第 5 节：C++23 封装**
- 错误类型定义：`enum class UartError { Timeout, BufferOverflow, NotInitialized, HardwareFault }`
- 核心接口设计：
  ```cpp
  auto send(std::span<const std::byte> data, uint32_t timeout_ms) -> std::expected<size_t, UartError>;
  auto receive(std::span<std::byte> buffer, uint32_t timeout_ms) -> std::expected<size_t, UartError>;
  auto send_it(std::span<const std::byte> data) -> std::expected<void, UartError>;
  auto receive_it(std::span<std::byte> buffer) -> std::expected<void, UartError>;
  ```
- 中断回调的 C++ 友元/静态函数桥接方案
- 使用 `std::span` 避免裸指针 + 长度的 C 风格接口

**第 6 节：常见坑**
- `HAL_UART_Receive_IT` 单次中断只接收指定长度，需要在中断回调中重新启用
- `printf` 需要 `-u _printf_float` 链接选项才能打印浮点数
- 中断中不能调用阻塞式 API
- UART 过冲错误（Overrun）的处理与清除
- 波特率误差超过 3% 时通信不稳定

**第 7 节：练习题**
- 基础：实现带超时的 `receive_line()` 函数，读取到 `\n` 为止
- 进阶：实现环形缓冲区 + 中断接收的生产者-消费者模式
- 挑战：实现简单的命令行解析器（支持 "LED ON"、"LED OFF"、"HELP" 命令）

**第 8 节：可复用代码**
- `uart_driver.hpp`：完整的 UartDriver 类头文件
- `uart_driver.cpp`：实现文件
- `printf_redirect.cpp`：printf 重定向到 UART 的实现
- `ring_buffer.hpp`：通用的环形缓冲区模板

**第 9 节：小结**
- 回顾 UART 协议要点
- 阻塞 vs 中断的选择策略
- C++23 封装的优势：类型安全、错误处理、零成本抽象

## 涉及文件
- documents/embedded/platforms/stm32f1/05-uart/index.md
- code/platforms/stm32f1/05-uart/blocking_send/
- code/platforms/stm32f1/05-uart/blocking_echo/
- code/platforms/stm32f1/05-uart/it_echo/
- code/platforms/stm32f1/05-uart/it_ring_buffer/
- code/platforms/stm32f1/05-uart/cpp23_driver/
- code/platforms/stm32f1/05-uart/cpp23_driver/uart_driver.hpp
- code/platforms/stm32f1/05-uart/cpp23_driver/uart_driver.cpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md section 6.1（UART 草稿内容）
- STM32F1 Reference Manual (RM0008) Chapter 27: USART
- STM32CubeF1 HAL Driver UART documentation
- C++23 std::span, std::expected, std::byte 标准
