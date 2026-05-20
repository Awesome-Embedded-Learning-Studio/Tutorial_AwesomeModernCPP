---
id: 015
title: "STM32F1 定时器类外设教程（TIM + PWM + 输入捕获 + 编码器模式）"
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

# STM32F1 定时器类外设教程

## 目标
编写 STM32F1 定时器类外设的完整教程，覆盖四大主题：基础定时器 TIM（计数器、预分频器、自动重装载寄存器、更新中断）、PWM 输出（占空比、频率、通道、极性、LED 呼吸灯、蜂鸣器驱动）、输入捕获（周期测量、频率测量、脉宽测量）、编码器模式（A/B 相正交信号、速度与位移计算）。教程遵循 9 节模板结构。

## 验收标准
- [ ] 教程遵循 9 节模板结构，四个主题各自独立但相互关联
- [ ] 基础 TIM 部分：计数器/预分频/ARR 的时钟频率计算公式详解，更新中断配置与回调
- [ ] PWM 部分：占空比/频率计算、通道选择、极性配置、LED 呼吸灯渐变效果、蜂鸣器音调驱动
- [ ] 输入捕获部分：周期测量、频率测量、脉宽测量、输入滤波/边沿检测
- [ ] 编码器模式部分：A/B 相正交解码、方向检测、4 倍频计数、速度与位移计算
- [ ] 每个主题包含 CubeMX 配置步骤和 HAL 代码示例
- [ ] C++23 封装包含至少：PwmChannel 类、InputCapture 类、Encoder 类
- [ ] 常见坑涵盖：定时器时钟源选择（内部/外部）、预分频值与 ARR 的关系、PWM 分辨率与频率的权衡
- [ ] 练习题包含：音乐播放器（蜂鸣器 PWM + 定时器切换音符）、RPM 测量计、电机速度闭环控制框架
- [ ] 所有代码在 STM32F103C8T6 上验证

## 实施说明
### 教程结构（9 节模板 × 四大主题）

**第 1 节：目标**
- 理解 STM32F1 定时器架构：高级定时器（TIM1）、通用定时器（TIM2-4）、基本定时器（TIM6-7）
- 掌握定时器四类应用：基础计时、PWM 输出、输入捕获、编码器接口
- 能用 C++23 封装定时器相关驱动

**第 2 节：硬件原理**
- STM32F1 定时器分类与特性对比表：
  - 高级 TIM1：6 通道、互补输出、死区、刹车输入
  - 通用 TIM2-4：4 通道、PWM/输入捕获/编码器
  - 基本 TIM6-7：仅计数+更新中断，常用于 DAC 触发
- 时钟树与定时器时钟源：TIM1 来自 APB2（72MHz），TIM2-4 来自 APB1（36MHz，但 APB1 预分频不为 1 时定时器时钟自动 x2）
- 核心寄存器：
  - PSC（预分频器）：`TIM_CLK / (PSC + 1)` 得到计数频率
  - ARR（自动重装载）：计数器从 0 计数到 ARR 后溢出
  - CCR（捕获/比较寄存器）：PWM 比较值 / 输入捕获值
  - CNT（计数器）：当前计数值
- PWM 原理：向上计数模式下，CNT < CCR 时输出高电平，CNT >= CCR 时输出低电平
- 输入捕获原理：检测到边沿时将 CNT 值锁存到 CCR
- 编码器模式原理：A/B 相正交信号的四倍频计数

**第 3 节：HAL 接口**
- 基础定时器：
  - `HAL_TIM_Base_Init`、`HAL_TIM_Base_Start_IT`
  - `HAL_TIM_PeriodElapsedCallback`
- PWM 输出：
  - `HAL_TIM_PWM_Init`、`HAL_TIM_PWM_Start`、`__HAL_TIM_SET_COMPARE`
  - 通道配置：`sConfigOC.OCMode = TIM_OCMODE_PWM1`、`Pulse`、`OCPolarity`
- 输入捕获：
  - `HAL_TIM_IC_Init`、`HAL_TIM_IC_Start_IT`
  - `HAL_TIM_IC_CaptureCallback`
  - 通道配置：`sConfigIC.ICPolarity`、`ICSelection`、`ICPrescaler`、`ICFilter`
- 编码器模式：
  - `HAL_TIM_Encoder_Init`、`HAL_TIM_Encoder_Start`
  - `__HAL_TIM_GET_COUNTER` 读取编码器计数值
  - TIM_ENCODERMODE_TI12（四倍频）vs TI1/TI2（二倍频）

**第 4 节：最小 Demo**
- Demo 1（基础 TIM）：1 秒定时中断，LED 翻转（SysTick 替代方案）
- Demo 2（PWM）：LED 呼吸灯效果（CCR 从 0 渐变到 ARR 再渐变回 0）
- Demo 3（PWM 蜂鸣器）：播放简单旋律（不同频率对应不同音符）
- Demo 4（输入捕获）：测量外部方波信号频率（两次上升沿的 CCR 差值 / 定时器频率）
- Demo 5（编码器）：连接旋转编码器，读取旋转方向和位移量

**第 5 节：C++23 封装**
- `Timer` 基类：基础定时器封装
  ```cpp
  class Timer {
  public:
      constexpr Timer(TIM_TypeDef* instance, uint32_t prescaler, uint32_t period);
      auto start() -> std::expected<void, TimerError>;
      auto stop() -> std::expected<void, TimerError>;
      void set_period(uint32_t period);
      void set_prescaler(uint32_t psc);
  };
  ```
- `PwmChannel` 类：
  ```cpp
  class PwmChannel {
  public:
      constexpr PwmChannel(TIM_TypeDef* timer, uint32_t channel);
      auto start() -> std::expected<void, TimerError>;
      auto set_duty_cycle(float percent) -> void;  // 0.0 - 100.0
      auto set_frequency(uint32_t freq_hz) -> void;
      auto set_pulse(uint32_t pulse) -> void;
  };
  ```
- `InputCapture` 类：
  ```cpp
  class InputCapture {
  public:
      auto start() -> std::expected<void, TimerError>;
      auto get_frequency() const -> std::expected<float, TimerError>;
      auto get_duty_cycle() const -> std::expected<float, TimerError>;
  };
  ```
- `Encoder` 类：
  ```cpp
  class Encoder {
  public:
      auto start() -> std::expected<void, TimerError>;
      auto get_count() const -> int32_t;
      auto get_speed() const -> float;  // RPM
      void reset();
  };
  ```
- 使用 `constexpr` 构造函数实现编译期定时器参数配置
- 使用 `std::expected` 统一错误处理

**第 6 节：常见坑**
- 定时器时钟频率计算错误：APB1 预分频不为 1 时定时器时钟 = APB1 时钟 x2
- PWM 频率与分辨率的权衡：ARR 越大分辨率越高但频率越低
- `__HAL_TIM_SET_COMPARE` 可以在运行时动态修改占空比，无需重新初始化
- 输入捕获溢出处理：信号周期大于 ARR 时需处理溢出
- 编码器模式计数方向与物理旋转方向的对应关系
- 定时器中断优先级：避免在高优先级中断中执行耗时操作

**第 7 节：练习题**
- 基础：使用 TIM6 实现软件 PWM（控制 LED 亮度，不使用硬件 PWM）
- 进阶：实现蜂鸣器音乐播放器（音符频率表 + 定时器切换 + 节拍控制）
- 进阶：使用输入捕获实现一个简单的 RPM 测量计（带 LCD/UART 显示）
- 挑战：使用编码器 + PWM 实现简单的电机速度闭环控制（PID 框架）

**第 8 节：可复用代码**
- `timer.hpp` / `timer.cpp`：Timer 基类
- `pwm_channel.hpp` / `pwm_channel.cpp`：PwmChannel 类
- `input_capture.hpp` / `input_capture.cpp`：InputCapture 类
- `encoder.hpp` / `encoder.cpp`：Encoder 类
- `buzzer.hpp`：蜂鸣器音符频率定义与播放接口

**第 9 节：小结**
- 定时器四大模式的核心概念回顾
- 时钟频率与参数计算速查表
- C++23 constexpr 配置的优势

## 涉及文件
- documents/embedded/platforms/stm32f1/04-timer/index.md
- code/platforms/stm32f1/04-timer/basic_tim_interrupt/
- code/platforms/stm32f1/04-timer/pwm_breathing_led/
- code/platforms/stm32f1/04-timer/pwm_buzzer_melody/
- code/platforms/stm32f1/04-timer/input_capture_freq/
- code/platforms/stm32f1/04-timer/encoder_read/
- code/platforms/stm32f1/04-timer/cpp23_driver/
- code/platforms/stm32f1/04-timer/cpp23_driver/timer.hpp
- code/platforms/stm32f1/04-timer/cpp23_driver/pwm_channel.hpp
- code/platforms/stm32f1/04-timer/cpp23_driver/input_capture.hpp
- code/platforms/stm32f1/04-timer/cpp23_driver/encoder.hpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md sections 5.1-5.4（定时器草稿内容）
- STM32F1 Reference Manual (RM0008) Chapter 14-16: TIM1, TIM2-5, TIM6-7
- STM32CubeF1 HAL Driver TIM documentation
- C++23 std::expected, constexpr 标准
