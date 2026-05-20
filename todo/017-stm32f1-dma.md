---
id: 017
title: "STM32F1 DMA 教程（外设-内存搬运 + 循环模式 + C++23 封装）"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on:
  - "architecture/002"
  - "012"
blocks: []
estimated_effort: large
---

# STM32F1 DMA 教程

## 目标
编写 STM32F1 DMA（直接内存访问）外设的完整教程，覆盖 DMA 职责与工作原理、外设到内存传输、内存到外设传输、传输完成中断、半传输中断、循环模式（Circular）、双缓冲（Double Buffer）技术。产出 DMA 驱动的 UART 发送/接收、ADC+DMA 连续采集、DMA 循环缓冲等实际应用。使用 C++23 特性（std::span、std::array、std::expected）进行类型安全的封装。教程遵循 9 节模板结构。

## 验收标准
- [ ] 教程遵循 9 节模板：目标 -> 硬件原理 -> HAL 接口 -> 最小 Demo -> C++23 封装 -> 常见坑 -> 练习题 -> 可复用代码 -> 小结
- [ ] 硬件原理部分包含 DMA 架构图（总线矩阵、DMA 通道与外设请求映射）
- [ ] 解释 DMA1（7 通道）和 DMA2（5 通道，仅大容量产品）的通道-外设映射表
- [ ] HAL 接口覆盖：`HAL_DMA_Start`、`HAL_DMA_Start_IT`、`HAL_UART_Transmit_DMA`、`HAL_UART_Receive_DMA`、`HAL_ADC_Start_DMA`
- [ ] 最小 Demo 包含：DMA 内存拷贝、UART DMA 发送/接收、ADC+DMA 多通道采集、循环缓冲区
- [ ] C++23 封装：`DmaChannel` 类 + 循环缓冲区模板，使用 `std::span` 标记缓冲区半区
- [ ] 常见坑涵盖：缓存一致性（D-Cache 对 DMA 的影响，虽然 F1 无 D-Cache 但作为知识补充）、对齐要求、传输完成判断
- [ ] 练习题包含：UART DMA 双缓冲接收、ADC+DMA+环形缓冲+数据处理流水线
- [ ] 依赖 UART 教程（012）的内容，在 UART DMA 部分引用已有 UART 驱动

## 实施说明
### 9 节模板详细规划

**第 1 节：目标**
- 理解 DMA 的工作原理与总线架构
- 掌握 DMA 三种传输方向（外设到内存、内存到外设、内存到内存）
- 能用 C++23 封装 DMA 驱动并实现高效的零 CPU 占用数据搬运

**第 2 节：硬件原理**
- DMA 架构：
  - AHB 总线矩阵：Cortex-M3、DMA1、DMA2、SRAM、Flash、APB1、APB2 的连接关系
  - DMA 作为总线主设备，独立于 CPU 访问内存和外设
  - DMA 传输不占用 CPU 时间，CPU 可同时执行其他任务
- DMA1 通道映射表（STM32F103）：
  - Channel 1: ADC1
  - Channel 2: SPI1_RX / USART3_TX / TIM1_CH1
  - Channel 3: SPI1_TX / USART3_RX / TIM1_CH2
  - Channel 4: SPI2/I2C2_RX / USART1_TX / I2C2_TX / TIM1_CH4/TRIG/COM
  - Channel 5: SPI2/I2C2_TX / USART1_RX / I2C2_RX / TIM1_UP
  - Channel 6: USART2_RX / I2C1_TX / TIM1_CH3
  - Channel 7: USART2_TX / I2C1_RX
- DMA2 通道映射（大容量产品）
- 核心概念：
  - 传输方向：PeriphToMemory / MemoryToPeriph / MemoryToMemory
  - 循环模式（Circular）：传输完成后自动重新开始，适合连续数据流
  - 普通模式（Normal）：传输完成一次后停止
  - 数据宽度：Byte / HalfWord / Word
  - 地址递增：源/目的地址是否递增
  - 优先级：Low / Medium / High / VeryHigh
- 半传输中断（HT）和传输完成中断（TC）的区别与双缓冲应用

**第 3 节：HAL 接口**
- `DMA_HandleTypeDef` 详解（通常不由用户直接配置，由外设句柄内部管理）
- 直接 DMA API：
  - `HAL_DMA_Start(hdma, SrcAddress, DstAddress, DataLength)`
  - `HAL_DMA_Start_IT(hdma, SrcAddress, DstAddress, DataLength)`
  - `HAL_DMA_Abort` / `HAL_DMA_Abort_IT`
- 外设组合 API（更常用）：
  - UART+DMA：`HAL_UART_Transmit_DMA`、`HAL_UART_Receive_DMA`
  - ADC+DMA：`HAL_ADC_Start_DMA`
  - SPI+DMA：`HAL_SPI_TransmitReceive_DMA`
- DMA 回调：
  - `HAL_UART_TxCpltCallback` / `HAL_UART_RxCpltCallback`
  - `HAL_UART_RxHalfCpltCallback`（半传输完成回调）
  - `HAL_ADC_ConvCpltCallback` / `HAL_ADC_ConvHalfCpltCallback`
- CubeMX DMA 配置步骤：在外设配置中添加 DMA 通道，设置方向/模式/宽度/优先级

**第 4 节：最小 Demo**
- Demo 1：DMA 内存到内存拷贝（验证 DMA 基本工作）
- Demo 2：UART DMA 发送（发送大量数据时 CPU 不阻塞）
- Demo 3：UART DMA 循环接收 + 半传输/完成中断实现双缓冲
  ```cpp
  // 半传输完成：前半缓冲区已填满，可以处理前半数据
  // 传输完成：后半缓冲区已填满，可以处理后半数据
  // DMA 自动重新开始，实现无缝连续接收
  ```
- Demo 4：ADC + DMA 循环采集多通道数据
- Demo 5：ADC + DMA + 双缓冲处理流水线（采集同时进行滤波计算）

**第 5 节：C++23 封装**
- `DmaBuffer` 模板类：安全管理的 DMA 缓冲区
  ```cpp
  template<size_t N>
  class DmaBuffer {
      std::array<std::byte, N> buffer_{};
  public:
      constexpr auto data() -> std::byte* { return buffer_.data(); }
      constexpr auto size() const -> size_t { return N; }
      auto first_half() -> std::span<std::byte> { return {buffer_.data(), N / 2}; }
      auto second_half() -> std::span<std::byte> { return {buffer_.data() + N / 2, N / 2}; }
      auto all() -> std::span<std::byte> { return {buffer_.data(), N}; }
  };
  ```
- `CircularDmaReceiver` 类：UART DMA 循环接收管理器
  ```cpp
  class CircularDmaReceiver {
  public:
      auto start(UART_HandleTypeDef* huart, std::span<std::byte> buffer) -> std::expected<void, DmaError>;
      auto on_half_complete() -> std::span<std::byte>;  // 返回前半缓冲区
      auto on_complete() -> std::span<std::byte>;        // 返回后半缓冲区
      auto get_pending_data() -> std::span<std::byte>;   // 计算未读数据
  };
  ```
- 使用 `std::span` 标记缓冲区的不同区域，避免拷贝
- 使用 `std::array` 确保缓冲区大小编译期确定

**第 6 节：常见坑**
- DMA 缓冲区必须位于 SRAM 中（不能是栈上的局部变量，函数返回后地址无效）
- DMA 传输期间不能修改源/目标缓冲区（一致性保证）
- UART DMA 接收中断回调中的空闲中断（IDLE）处理：接收不定长数据
- 半传输中断和完成中断的竞争条件
- DMA 通道冲突：同一通道同一时间只能服务一个外设请求
- 循环模式下 `HAL_UART_DMAStop` 和 `HAL_UART_Receive_DMA` 的正确调用顺序
- DMA 传输大小限制（最大 65535）

**第 7 节：练习题**
- 基础：实现 UART DMA 发送函数，在发送完成回调中设置标志位
- 进阶：实现 UART DMA 循环接收 + IDLE 线空闲检测，支持不定长数据帧
- 挑战：实现完整的 ADC + DMA + 数据处理流水线（采集 -> 滤波 -> UART 输出，全 DMA 驱动）

**第 8 节：可复用代码**
- `dma_buffer.hpp`：DmaBuffer 模板类
- `circular_dma_receiver.hpp` / `circular_dma_receiver.cpp`：UART DMA 循环接收
- `dma_helpers.hpp`：DMA 状态查询、剩余传输量查询等工具函数

**第 9 节：小结**
- DMA 核心价值：释放 CPU、提高吞吐、降低延迟
- Normal vs Circular 模式的选型指南
- 双缓冲模式的实现模式总结

## 涉及文件
- documents/embedded/platforms/stm32f1/06-dma/index.md
- code/platforms/stm32f1/06-dma/mem_to_mem/
- code/platforms/stm32f1/06-dma/uart_dma_send/
- code/platforms/stm32f1/06-dma/uart_dma_circular_rx/
- code/platforms/stm32f1/06-dma/adc_dma_multi/
- code/platforms/stm32f1/06-dma/cpp23_driver/
- code/platforms/stm32f1/06-dma/cpp23_driver/dma_buffer.hpp
- code/platforms/stm32f1/06-dma/cpp23_driver/circular_dma_receiver.hpp
- code/platforms/stm32f1/06-dma/cpp23_driver/circular_dma_receiver.cpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md section 6.2（DMA 草稿内容）
- STM32F1 Reference Manual (RM0008) Chapter 9: DMA
- STM32CubeF1 HAL Driver DMA documentation
- AN4031: Using the STM32F2 and STM32F4 DMA controller（通用 DMA 应用笔记）
- C++23 std::span, std::array, std::expected 标准
