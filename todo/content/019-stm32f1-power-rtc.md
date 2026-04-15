---
id: 019
title: "STM32F1 低功耗模式与 RTC/Backup 教程（休眠唤醒 + 断电保持）"
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

# STM32F1 低功耗模式与 RTC/Backup 教程

## 目标
编写两部分教程：(1) 低功耗模式教程覆盖 Sleep（CPU 停止、外设运行）、Stop（CPU 停止、高速时钟停止、SRAM 保持）、Standby（CPU 停止、SRAM 丢失、仅备份域保持）三种模式，以及唤醒来源与进入/退出流程；(2) RTC 与 Backup 教程覆盖实时时钟配置、备份寄存器、断电保持（VBAT）、时间戳管理、状态恢复机制。使用 C++23 特性（constexpr、std::expected、std::array）进行封装。教程遵循 9 节模板结构。

## 验收标准
- [ ] 教程遵循 9 节模板结构，低功耗模式和 RTC/Backup 各占独立章节
- [ ] 低功耗模式部分：
  - [ ] 三种模式对比表（Sleep/Stop/Standby 的功耗、保持区域、唤醒来源、唤醒时间）
  - [ ] 每种模式的进入流程（WFI/WFE 指令、条件配置）
  - [ ] 唤醒来源配置：中断（EXTI）、RTC 闹钟、WKUP 引脚
  - [ ] 电流测量方法与实际功耗数据
- [ ] RTC/Backup 部分：
  - [ ] RTC 时钟源选择：LSE（32.768kHz 外部晶振）、LSI（~40kHz 内部）、HSE/128
  - [ ] RTC 计数器与预分频器配置（Unix 时间戳方案 vs BCD 方案）
  - [ ] 备份寄存器（BKP）读写：VBAT 域保持，主电源断电后数据不丢失
  - [ ] RTC 闹钟中断与唤醒配置
- [ ] C++23 封装：`PowerManager` 类 + `RtcDriver` 类 + `BackupRegisters` 类
- [ ] 常见坑涵盖：Standby 模式 SRAM 丢失、VBAT 未连接导致备份域复位、RTC LSE 起振失败
- [ ] 练习题包含：定时唤醒采集系统、低功耗传感器节点、RTC 日历时钟 + UART 输出
- [ ] 所有代码在 STM32F103C8T6 上验证（需要 VBAT 引脚接 3V 纽扣电池测试备份域）

## 实施说明
### 9 节模板详细规划

**第 1 节：目标**
- 理解 STM32F1 三种低功耗模式的特点与使用场景
- 掌握 RTC 和备份域的工作原理与配置方法
- 能用 C++23 封装低功耗管理和 RTC 驱动

**第 2 节：硬件原理**
- 低功耗模式对比：
  | 特性 | Sleep | Stop | Standby |
  | CPU | 停止 | 停止 | 停止 |
  | 高速时钟 | 运行 | 停止 | 停止 |
  | SRAM | 保持 | 保持 | 丢失 |
  | 寄存器 | 保持 | 保持 | 丢失（除备份域） |
  | 功耗（典型） | ~2mA | ~20uA | ~2uA |
  | 唤醒时间 | 即时 | ~us（HSI 启动） | ~ms（HSE/LSE 启动） |
  | 唤醒源 | 任何中断 | EXTI 中断 | WKUP 引脚、RTC 闹钟 |
- 备份域（Backup Domain）：
  - 由 VBAT 引脚供电（主电源断电时由纽扣电池保持）
  - 包含：RTC 计数器、RTC 预分频器、备份寄存器（BKP_DR1-DR10，共 10 个 16 位寄存器）
  - 备份域访问需要先使能 PWR 和 BKP 时钟，再解除备份域写保护
- RTC 架构：
  - 时钟源：LSE（32.768kHz，最精确）、LSI（~40kHz，精度差）、HSE/128
  - 预分频器：将时钟源分频到 1Hz（异步预分频 + 同步预分频）
  - 32 位计数器：可配置为 Unix 时间戳（秒数）
  - 闹钟寄存器：匹配计数器值触发闹钟中断
- 硬件连接：VBAT 引脚接 CR1220/CR2032 纽扣电池（3V）

**第 3 节：HAL 接口**
- 低功耗 API：
  - `HAL_PWR_EnterSLEEPMode`：进入 Sleep 模式（WFI 或 WFE）
  - `HAL_PWR_EnterSTOPMode`：进入 Stop 模式（配置调压器 LowPower 或 Main）
  - `HAL_PWR_EnterSTANDBYMode`：进入 Standby 模式
  - `HAL_PWR_EnableWakeUpPin`：使能 WKUP 引脚唤醒（Standby 模式）
  - `HAL_PWR_DisableWakeUpPin`
- PWR 相关：
  - `__HAL_RCC_PWR_CLK_ENABLE`：使能 PWR 时钟
  - `HAL_PWR_EnableBkUpAccess`：允许访问备份域
- RTC API：
  - `HAL_RTC_Init`：初始化 RTC（`RTC_HandleTypeDef`：HourFormat、AsynchPrediv、SynchPrediv、OutPut）
  - `HAL_RTC_SetTime` / `HAL_RTC_GetTime`：设置/获取时间
  - `HAL_RTC_SetDate` / `HAL_RTC_GetDate`：设置/获取日期
  - `HAL_RTC_SetAlarm_IT` / `HAL_RTC_AlarmIRQHandler`：闹钟中断
  - `HAL_RTC_SetAlarm`：设置闹钟
- BKP API：
  - `HAL_PWR_EnableBkUpAccess`：解锁备份域
  - `__HAL_RCC_BKP_CLK_ENABLE`：使能 BKP 时钟
  - `HAL_RTCEx_BKUPWrite` / `HAL_RTCEx_BKUPRead`：写入/读取备份寄存器

**第 4 节：最小 Demo**
- Demo 1：Sleep 模式（WFI 等待外部中断唤醒，LED 翻转指示）
- Demo 2：Stop 模式（EXTI 按键唤醒，唤醒后恢复 HSI 时钟并继续运行）
- Demo 3：Standby 模式（WKUP 引脚唤醒，检测复位标志判断是否从 Standby 恢复）
- Demo 4：RTC 日历时钟（设置当前时间，每秒通过 UART 输出时间戳）
- Demo 5：RTC 闹钟唤醒（Stop 模式 + RTC 闹钟，定时唤醒执行任务后再次休眠）
- Demo 6：备份寄存器断电保持（写入数据到 BKP，断电再上电后读取验证）

**第 5 节：C++23 封装**
- `PowerManager` 类：
  ```cpp
  enum class PowerMode { Sleep, Stop, Standby };

  class PowerManager {
  public:
      static auto enter_sleep(uint32_t wakeup_exti_line) -> void;
      static auto enter_stop(bool low_power_regulator = true) -> void;
      static auto enter_standby(uint32_t wakeup_pins = PWR_WAKEUP_PIN1) -> void;
      static auto is_standby_wakeup() -> bool;  // 检查是否从 Standby 恢复
      static auto clear_wakeup_flag() -> void;
      static auto configure_exti_wakeup(uint32_t pin, uint32_t trigger) -> std::expected<void, PowerError>;
  };
  ```
- `RtcDriver` 类：
  ```cpp
  struct DateTime {
      uint16_t year; uint8_t month; uint8_t day;
      uint8_t hour; uint8_t minute; uint8_t second;
      constexpr auto to_timestamp() const -> uint32_t;
      static constexpr auto from_timestamp(uint32_t ts) -> DateTime;
  };

  class RtcDriver {
  public:
      auto initialize(RtcClockSource source) -> std::expected<void, RtcError>;
      auto set_time(const DateTime& dt) -> std::expected<void, RtcError>;
      auto get_time() -> std::expected<DateTime, RtcError>;
      auto set_alarm(const DateTime& alarm) -> std::expected<void, RtcError>;
  };
  ```
- `BackupRegisters` 类：
  ```cpp
  class BackupRegisters {
  public:
      static auto write(uint32_t reg, uint16_t value) -> void;
      static auto read(uint32_t reg) -> uint16_t;
      static constexpr size_t count = 10;  // DR1-DR10
  };
  ```
- `DateTime` 使用 `constexpr` 方法实现编译期时间转换

**第 6 节：常见坑**
- Standby 模式唤醒等效于复位（SRAM 丢失，程序从头开始），需通过复位标志和备份寄存器判断
- VBAT 未连接时每次主电源上电都会导致备份域复位
- LSE 晶振起振可能失败（PCB 布局、负载电容不匹配），需要超时检测并回退到 LSI
- Stop 模式唤醒后时钟切换到 HSI（8MHz），需要手动重新配置 PLL 到 72MHz
- RTC 读取时间后必须再读取日期寄存器才能解锁影子寄存器（HAL 的特殊要求）
- 备份域写保护需要关闭（`HAL_PWR_EnableBkUpAccess`），操作完可恢复
- Stop 模式的调压器模式选择：Main 系统功耗略高但唤醒快，LowPower 功耗低但唤醒慢

**第 7 节：练习题**
- 基础：实现定时唤醒采集系统（Stop + RTC 闹钟，每 10 秒唤醒采集 ADC，通过 UART 输出）
- 进阶：实现低功耗传感器节点（采集 -> 发送 -> 休眠 循环，统计平均功耗）
- 挑战：实现 RTC 日历时钟 + 断电恢复（Standby 前保存状态到备份寄存器，恢复后继续运行）

**第 8 节：可复用代码**
- `power_manager.hpp` / `power_manager.cpp`：PowerManager 类
- `rtc_driver.hpp` / `rtc_driver.cpp`：RtcDriver + DateTime
- `backup_registers.hpp`：BackupRegisters 类
- `datetime.hpp`：constexpr 时间戳转换工具

**第 9 节：小结**
- 三种低功耗模式选型速查表
- RTC 时钟源选型指南
- 电池供电系统的功耗优化策略

## 涉及文件
- documents/embedded/platforms/stm32f1/12-power/index.md
- documents/embedded/platforms/stm32f1/13-rtc/index.md
- code/platforms/stm32f1/12-power/sleep_demo/
- code/platforms/stm32f1/12-power/stop_demo/
- code/platforms/stm32f1/12-power/standby_demo/
- code/platforms/stm32f1/12-power/cpp23_driver/
- code/platforms/stm32f1/12-power/cpp23_driver/power_manager.hpp
- code/platforms/stm32f1/13-rtc/calendar_demo/
- code/platforms/stm32f1/13-rtc/alarm_wakeup/
- code/platforms/stm32f1/13-rtc/backup_demo/
- code/platforms/stm32f1/13-rtc/cpp23_driver/
- code/platforms/stm32f1/13-rtc/cpp23_driver/rtc_driver.hpp
- code/platforms/stm32f1/13-rtc/cpp23_driver/datetime.hpp
- code/platforms/stm32f1/13-rtc/cpp23_driver/backup_registers.hpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md sections 8.3-8.4（低功耗 + RTC 草稿内容）
- STM32F1 Reference Manual (RM0008) Chapter 4: Power Control (PWR) / Chapter 20: Backup Registers (BKP) / Chapter 21: Real-Time Clock (RTC)
- AN2629: STM32F10x RTC and backup registers
- C++23 constexpr, std::expected, std::array 标准
