---
id: 016
title: "STM32F1 ADC 教程（模拟采集 + 单次/连续/扫描模式 + C++23 封装）"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on:
  - "architecture/002"
blocks: []
estimated_effort: medium
---

# STM32F1 ADC 教程

## 目标
编写 STM32F1 ADC（模数转换器）外设的完整教程，覆盖模拟输入基础、12 位分辨率、采样时间配置、单次转换模式、连续转换模式、通道扫描模式、ADC 校准。产出电位器采样和电压读取的实际应用。使用 C++23 特性（std::expected、std::span、constexpr）进行驱动封装。教程遵循 9 节模板结构。

## 验收标准
- [ ] 教程遵循 9 节模板：目标 -> 硬件原理 -> HAL 接口 -> 最小 Demo -> C++23 封装 -> 常见坑 -> 练习题 -> 可复用代码 -> 小结
- [ ] 硬件原理部分包含 ADC 转换原理（逐次逼近型 SAR）、分辨率与量化误差、采样时间与输入阻抗的关系
- [ ] HAL 接口覆盖：`HAL_ADC_Start`、`HAL_ADC_PollForConversion`、`HAL_ADC_Start_IT`、`HAL_ADC_Start_DMA`、`HAL_ADCEx_Calibration_Start`
- [ ] 最小 Demo 包含：单通道电位器采样、多通道扫描（电位器+温度传感器）、ADC 中断模式、ADC+DMA 连续采集
- [ ] C++23 封装：`AdcChannel` 类和 `AdcController` 类，使用 constexpr 配置通道映射
- [ ] 常见坑涵盖：采样时间不足导致数据不准、GPIO 未配置为模拟输入、ADC 校准时机、温度传感器内部通道特殊处理
- [ ] 练习题包含：电压表、多通道数据记录器、简单滤波（滑动平均）
- [ ] 所有代码在 STM32F103C8T6 + 电位器模块上验证

## 实施说明
### 9 节模板详细规划

**第 1 节：目标**
- 理解 ADC 转换原理与 STM32F1 ADC 外设特性
- 掌握单次/连续/扫描三种转换模式的使用场景
- 能用 C++23 封装 ADC 驱动并实现模拟量采集

**第 2 节：硬件原理**
- ADC 基础概念：
  - 逐次逼近型（SAR）ADC 工作原理
  - 12 位分辨率：0-4095 对应 0-VREF
  - 量化误差：+/- 1 LSB
  - 参考电压 VREF+（通常 3.3V）
- STM32F1 ADC 特性：
  - ADC1 和 ADC2（10 路外部通道 + 内部温度传感器 + 内部参考电压）
  - ADC3（部分型号支持，8 路外部通道）
  - 通道分组：规则组（最多 16 通道）和注入组（最多 4 通道）
  - 采样时间可配置（1.5/7.5/13.5/28.5/41.5/55.5/71.5/239.5 周期）
  - 转换时间 = 采样时间 + 12.5 周期
- GPIO 配置：模拟输入模式（`GPIO_MODE_ANALOG`）
- 硬件连接：电位器中间引脚接 PA0（ADC1_CH0），两端接 VCC 和 GND
- 内部通道：温度传感器（CH16）、VREFINT（CH17）

**第 3 节：HAL 接口**
- `ADC_HandleTypeDef` 初始化结构体详解：
  - `ClockPrescaler`：ADC 时钟分频（PCLK2 的 2/4/6/8 分频，最大 14MHz）
  - `Resolution`：12 位
  - `ScanConvMode`：扫描模式（多通道时启用）
  - `ContinuousConvMode`：连续转换（一次触发后持续转换）
  - `DiscontinuousConvMode`：间断模式
  - `NbrOfConversion`：规则组转换数量
  - `ExternalTrigConv`：外部触发源选择（软件触发 / 定时器触发）
  - `DataAlign`：数据对齐（右对齐）
- 核心 API：
  - `HAL_ADC_Init`：初始化 ADC
  - `HAL_ADCEx_Calibration_Start`：ADC 校准（应在初始化后、启动前调用）
  - `HAL_ADC_Start` / `HAL_ADC_Stop`：启动/停止 ADC
  - `HAL_ADC_PollForConversion`：轮询等待转换完成
  - `HAL_ADC_GetValue`：读取转换结果
  - `HAL_ADC_Start_IT` / `HAL_ADC_ConvCpltCallback`：中断模式
  - `HAL_ADC_Start_DMA` / `HAL_ADC_ConvHalfCpltCallback` / `HAL_ADC_ConvCpltCallback`：DMA 模式
- 通道配置：`ADC_ChannelConfTypeDef`（Channel、Rank、SamplingTime）

**第 4 节：最小 Demo**
- Demo 1：单通道电位器采样（软件触发 + 轮询 + 电压计算）
  - 电压 = ADC_VALUE * VREF / 4095
  - 通过 UART 打印电压值
- Demo 2：内部温度传感器读取
  - 读取 CH16，根据数据手册公式计算温度：`T = (V25 - Vsense) / Avg_Slope + 25`
- Demo 3：多通道扫描模式（电位器 + 温度传感器，DMA 传输）
- Demo 4：连续转换 + DMA 循环缓冲，实时采集

**第 5 节：C++23 封装**
- `AdcChannel` 配置结构：
  ```cpp
  struct AdcChannelConfig {
      uint32_t channel;
      uint32_t rank;
      uint32_t sampling_time;
      constexpr AdcChannelConfig(uint32_t ch, uint32_t r, uint32_t st)
          : channel(ch), rank(r), sampling_time(st) {}
  };
  ```
- `AdcController` 类：
  ```cpp
  class AdcController {
  public:
      constexpr AdcController(ADC_TypeDef* instance) : instance_(instance) {}
      auto initialize() -> std::expected<void, AdcError>;
      auto calibrate() -> std::expected<void, AdcError>;
      auto read_single(uint32_t channel) -> std::expected<uint16_t, AdcError>;
      auto read_voltage(uint32_t channel, float vref = 3.3f) -> std::expected<float, AdcError>;
      auto start_continuous(std::span<uint16_t> buffer) -> std::expected<void, AdcError>;
      auto stop() -> void;
  };
  ```
- `constexpr` 通道配置表，编译期确定采样参数
- `std::expected` 处理校准失败、转换超时等错误

**第 6 节：常见坑**
- 采样时间不足：高源阻抗信号需要更长采样时间（建议 >= 28.5 周期）
- GPIO 必须配置为模拟模式，否则数字输入施密特触发器会干扰
- ADC 校准必须在第一次转换前执行，且 ADC 必须处于禁用状态
- 温度传感器精度较低（+/- 1.5°C），仅适合粗略监测
- DMA 循环模式下缓冲区对齐问题
- 连续转换模式关闭时必须先等待当前转换完成
- 多通道扫描时 Rank 顺序决定转换顺序

**第 7 节：练习题**
- 基础：实现一个简单的电压表，通过 UART 输出电位器电压值（保留 2 位小数）
- 进阶：实现多通道数据记录器（ADC + DMA + 定时器触发 + 环形缓冲区）
- 挑战：实现滑动平均滤波器 + 中值滤波器，对比滤波效果

**第 8 节：可复用代码**
- `adc_controller.hpp` / `adc_controller.cpp`：AdcController 完整实现
- `adc_filters.hpp`：滑动平均滤波器 + 中值滤波器模板
- `voltage_calculator.hpp`：电压转换工具函数

**第 9 节：小结**
- ADC 三种模式的选型指南：单次（事件触发）、连续（持续监控）、扫描（多通道）
- 采样时间与精度的权衡
- C++23 constexpr 配置的优势

## 涉及文件
- documents/embedded/platforms/stm32f1/09-adc/index.md
- code/platforms/stm32f1/09-adc/potentiometer_poll/
- code/platforms/stm32f1/09-adc/temp_sensor/
- code/platforms/stm32f1/09-adc/multi_channel_dma/
- code/platforms/stm32f1/09-adc/cpp23_driver/
- code/platforms/stm32f1/09-adc/cpp23_driver/adc_controller.hpp
- code/platforms/stm32f1/09-adc/cpp23_driver/adc_controller.cpp
- code/platforms/stm32f1/09-adc/cpp23_driver/adc_filters.hpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md section 7.1（ADC 草稿内容）
- STM32F1 Reference Manual (RM0008) Chapter 11: ADC
- STM32CubeF1 HAL Driver ADC documentation
- C++23 std::expected, std::span, constexpr 标准
