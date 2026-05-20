---
id: 013
title: "STM32F1 SPI 完整教程（主机通信 + 寄存器读写 + C++23 封装）"
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

# STM32F1 SPI 完整教程

## 目标
编写 STM32F1 SPI 外设的完整教程，覆盖 SPI 协议基础（SCK/MOSI/MISO/NSS 四线、主机/从机模式、CPOL/CPHA 四种模式）、HAL SPI 收发 API、SPI 寄存器读写操作、设备探测与识别。使用 C++23 特性（std::span、std::expected、std::byteswap）进行类型安全的驱动封装。教程遵循 9 节模板结构。

## 验收标准
- [ ] 教程遵循 9 节模板：目标 -> 硬件原理 -> HAL 接口 -> 最小 Demo -> C++23 封装 -> 常见坑 -> 练习题 -> 可复用代码 -> 小结
- [ ] 硬件原理部分包含 SPI 四线通信时序图（CPOL=0/1, CPHA=0/1 的四种模式波形图）
- [ ] 解释 NSS 管理方式：硬件 NSS vs 软件 NSS（GPIO 手动拉低拉高）
- [ ] HAL 接口覆盖：`HAL_SPI_Transmit`、`HAL_SPI_Receive`、`HAL_SPI_TransmitReceive`、中断/DMA 版本
- [ ] 最小 Demo 包含：SPI Loopback 测试、W25Q32 Flash 读取 JEDEC ID、SPI 设备寄存器读写
- [ ] C++23 封装：`SpiDriver` 类 + `SpiDevice` 抽象（CS 管理 + 寄存器读写），使用 `std::byteswap` 处理字节序
- [ ] 常见坑涵盖：NSS 时序问题、时钟极性/相位不匹配、字节序问题、FIFO 溢出
- [ ] 练习题包含：W25Q32 Flash 读写、MPU6050 SPI 读取、多从机管理
- [ ] 所有代码在 STM32F103C8T6 + W25Q32 Flash 模块上验证

## 实施说明
### 9 节模板详细规划

**第 1 节：目标**
- 理解 SPI 协议四线通信原理与时序
- 掌握 STM32F1 SPI 外设的 HAL 驱动 API
- 能用 C++23 封装通用的 SPI 主机驱动和设备抽象层

**第 2 节：硬件原理**
- SPI 四线详解：SCK（时钟）、MOSI（主出从入）、MISO（主入从出）、NSS（片选）
- 四种 SPI 模式（Mode 0-3）的 CPOL/CPHA 组合与时序波形图
  - Mode 0: CPOL=0, CPHA=0（最常用，空闲低电平，第一个边沿采样）
  - Mode 1: CPOL=0, CPHA=1
  - Mode 2: CPOL=1, CPHA=0
  - Mode 3: CPOL=1, CPHA=1（空闲高电平，第二个边沿采样）
- 数据传输方向：全双工、半双工、单线
- STM32F1 SPI 外设：SPI1（APB2, 36MHz max）、SPI2/SPI3（APB1, 18MHz max）
- GPIO 配置：SCK/MOSI/NSS 复用推挽输出、MISO 浮空输入/上拉输入
- 硬件连接图：Blue Pill + W25Q32 SPI Flash 模块接线图

**第 3 节：HAL 接口**
- `SPI_HandleTypeDef` 初始化结构体详解（Mode、Direction、DataSize、CLKPolarity、CLKPhase、NSS、BaudRatePrescaler、FirstBit）
- BaudRatePrescaler 计算公式：SPI_CLK = APB_CLK / Prescaler
- 阻塞 API：`HAL_SPI_Transmit`、`HAL_SPI_Receive`、`HAL_SPI_TransmitReceive`
- 中断 API：`HAL_SPI_Transmit_IT`、`HAL_SPI_Receive_IT`、`HAL_SPI_TransmitReceive_IT`
- DMA API：`HAL_SPI_Transmit_DMA`、`HAL_SPI_Receive_DMA`、`HAL_SPI_TransmitReceive_DMA`
- 回调函数：`HAL_SPI_TxCpltCallback`、`HAL_SPI_RxCpltCallback`、`HAL_SPI_TxRxCpltCallback`
- NSS 软件管理：`HAL_GPIO_WritePin` 手动控制 CS 引脚

**第 4 节：最小 Demo**
- Demo 1：SPI 自环回测试（MOSI-MISO 短接，发送并验证接收数据）
- Demo 2：W25Q32 Flash 读取 JEDEC ID（命令 0x9F，读取 3 字节制造商+设备 ID）
- Demo 3：SPI 设备寄存器读取（写 1 字节寄存器地址，读 1 字节数据）
- Demo 4：W25Q32 Flash 写入使能 + 页写入 + 读取验证

**第 5 节：C++23 封装**
- `SpiDriver` 类：底层 SPI 外设封装
  ```cpp
  class SpiDriver {
  public:
      auto transfer(std::span<const std::byte> tx, std::span<std::byte> rx, uint32_t timeout_ms) -> std::expected<size_t, SpiError>;
      auto write(std::span<const std::byte> data, uint32_t timeout_ms) -> std::expected<size_t, SpiError>;
      auto read(std::span<std::byte> buffer, uint32_t timeout_ms) -> std::expected<size_t, SpiError>;
  };
  ```
- `SpiDevice` 抽象类：管理 CS 片选 + 寄存器读写协议
  ```cpp
  class SpiDevice {
  public:
      auto write_register(uint8_t reg, std::span<const std::byte> data) -> std::expected<void, SpiError>;
      auto read_register(uint8_t reg, std::span<std::byte> buffer) -> std::expected<void, SpiError>;
  protected:
      CsGuard cs_guard_;  // RAII 片选管理
  };
  ```
- 使用 `std::byteswap` 处理 16/32 位寄存器的字节序转换
- RAII 片选管理：构造时拉低 CS，析构时拉高 CS

**第 6 节：常见坑**
- SPI 时钟速度过快导致通信不稳定（注意 APB 总线分频限制）
- CPOL/CPHA 配置与从设备要求不匹配（必须查阅从设备数据手册）
- CS 管理时序：传输完成前不能释放 CS（HAL 函数返回前需等待 BSY 标志清零）
- 接收时必须同时发送 dummy 字节（全双工特性）
- W25Q32 写入前必须执行 Write Enable 命令
- SPI Flash 页写入跨页边界问题（256 字节对齐）

**第 7 节：练习题**
- 基础：读取 W25Q32 的 UID 并通过 UART 打印
- 进阶：实现 SPI Flash 的扇区擦除 + 写入 + 读取 + 校验完整流程
- 挑战：实现一个通用的 SPI 设备探测工具，自动扫描并识别总线上的设备

**第 8 节：可复用代码**
- `spi_driver.hpp` / `spi_driver.cpp`：SpiDriver 完整实现
- `spi_device.hpp`：SpiDevice 抽象基类
- `cs_guard.hpp`：RAII 片选管理器
- `w25q32.hpp` / `w25q32.cpp`：W25Q32 Flash 驱动示例

**第 9 节：小结**
- SPI 协议核心概念回顾
- 四种模式的选型指南
- C++23 封装的设计模式总结

## 涉及文件
- documents/embedded/platforms/stm32f1/07-spi/index.md
- code/platforms/stm32f1/07-spi/loopback_test/
- code/platforms/stm32f1/07-spi/w25q32_id/
- code/platforms/stm32f1/07-spi/w25q32_read_write/
- code/platforms/stm32f1/07-spi/cpp23_driver/
- code/platforms/stm32f1/07-spi/cpp23_driver/spi_driver.hpp
- code/platforms/stm32f1/07-spi/cpp23_driver/spi_driver.cpp
- code/platforms/stm32f1/07-spi/cpp23_driver/spi_device.hpp
- code/platforms/stm32f1/07-spi/cpp23_driver/w25q32.hpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md section 6.3（SPI 草稿内容）
- STM32F1 Reference Manual (RM0008) Chapter 25: SPI
- W25Q32 datasheet
- C++23 std::span, std::expected, std::byteswap 标准
