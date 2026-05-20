---
id: 018
title: "STM32F1 Flash 存储与看门狗教程（参数持久化 + 故障恢复）"
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

# STM32F1 Flash 存储与看门狗教程

## 目标
编写两部分教程：(1) Flash 存储教程覆盖 Flash 内存结构、页擦除、半字写入、参数区设计、校验与版本号管理；(2) 看门狗教程覆盖独立看门狗（IWDG）和窗口看门狗（WWDG）、喂狗机制、故障恢复策略、超时复位演示。使用 C++23 特性（std::span、std::expected、std::array、std::byteswap）进行类型安全的封装。教程遵循 9 节模板结构。

## 验收标准
- [ ] 教程遵循 9 节模板结构，Flash 存储和看门狗各占独立章节但组织在同一文档
- [ ] Flash 存储部分：
  - [ ] 详解 STM32F1 Flash 扇区结构（低密度/中密度/高密度/互联型的不同布局）
  - [ ] 页擦除流程（检查 BUSY/LOCK -> 解锁 -> 擦除 -> 等待完成 -> 上锁）
  - [ ] 半字（16 位）编程流程（解锁 -> 写入 -> 等待完成 -> 上锁）
  - [ ] 参数区设计方案：魔数校验 + 版本号 + 参数数据 + CRC/校验和
  - [ ] 提供 Flash 参数读写的完整封装（支持任意结构体的序列化与反序列化）
- [ ] 看门狗部分：
  - [ ] IWDG（独立看门狗）：LSI 时钟源（约 40kHz）、预分频器、重装载值、超时计算公式
  - [ ] WWDG（窗口看门狗）：APB1 时钟源、窗口值、计数器递减、提前唤醒中断（EWI）
  - [ ] IWDG vs WWDG 的对比与选型指南
  - [ ] 喂狗策略：主循环喂狗 vs 任务监控喂狗
  - [ ] 故障恢复演示：故意死循环触发看门狗复位，验证系统恢复
- [ ] C++23 封装：`FlashStorage<Params>` 模板类 + `Watchdog` 类
- [ ] 常见坑涵盖：Flash 写入时不能执行同 Flash 中的代码、擦除次数寿命、看门狗一旦启动无法停止
- [ ] 练习题包含：参数保存系统（断电恢复）、双区备份 Flash 写入、看门狗 + 故障日志
- [ ] 所有代码在 STM32F103C8T6 上验证

## 实施说明
### 9 节模板详细规划

**第 1 节：目标**
- 理解 STM32F1 Flash 存储结构与编程方法
- 掌握 IWDG/WWDG 看门狗的原理与使用
- 能用 C++23 封装安全的 Flash 参数存储和看门狗驱动

**第 2 节：硬件原理**
- Flash 存储结构（STM32F103C8T6，中密度产品）：
  - 主 Flash：64KB，分为 64 页，每页 1KB
  - 页 0-3：存放用户代码（通常 4KB 足够简单程序）
  - 信息块：System Memory（Bootloader，2KB）+ Option Bytes（16 字节）
  - Flash 编程粒度：半字（16 位），不能按字节写入
  - 擦除粒度：整页（1KB），擦除后所有位为 1（0xFFFF）
  - 写入只能将 1 变为 0，不能将 0 变为 1（必须先擦除再写入）
  - 擦写寿命：典型 10000 次擦写周期
- 看门狗原理：
  - IWDG：独立于主时钟（LSI ~40kHz），一旦启动无法停止，适合安全关键应用
    - 超时 = (Prescaler / LSI_FREQ) * Reload
    - 超时范围：约 0.1ms ~ 26214.4ms
  - WWDG：使用 APB1 时钟，有窗口限制（计数器值必须在窗口范围内才能喂狗）
    - 适合检测软件时序异常（任务执行太快或太慢都能检测）
    - 早期唤醒中断（EWI）：计数器到达 0x40 时触发，最后喂狗机会

**第 3 节：HAL 接口**
- Flash 编程 API：
  - `HAL_FLASH_Unlock` / `HAL_FLASH_Lock`：解锁/上锁 Flash
  - `HAL_FLASHEx_Erase`：页擦除（`FLASH_EraseInitTypeDef`：TypeErase=BANK/PAGES、PageAddress、NbPages）
  - `HAL_FLASH_Program`：编程（Type=FLASH_TYPEPROGRAM_HALFWORD，Address，Data）
  - `HAL_FLASH_WaitForLastOperation`：等待操作完成
  - `HAL_FLASHEx_OBGetConfig` / `HAL_FLASHEx_OBProgram`：Option Bytes 配置
- IWDG API：
  - `HAL_IWDG_Init`：初始化（`IWDG_HandleTypeDef`：Prescaler、Reload）
  - `HAL_IWDG_Refresh`：喂狗（重装载计数器）
- WWDG API：
  - `HAL_WWDG_Init`：初始化（`WWDG_HandleTypeDef`：Prescaler、Window、Counter）
  - `HAL_WWDG_Refresh`：喂狗（重装载计数器）
  - `HAL_WWDG_EarlyWakeupCallback`：早期唤醒回调

**第 4 节：最小 Demo**
- Demo 1：Flash 页擦除 + 半字写入 + 读回验证
- Demo 2：Flash 参数区实现（写入结构体到 Flash 最后一页，重启后读取恢复）
- Demo 3：IWDG 基本使用（正常喂狗保持运行）
- Demo 4：IWDG 超时复位演示（故意不喂狗，观察系统复位）
- Demo 5：WWDG 窗口喂狗演示（在窗口内喂狗正常，窗口外喂狗复位）
- Demo 6：综合 Demo：参数保存系统 + 看门狗保护（上电从 Flash 恢复参数，看门狗监控主循环）

**第 5 节：C++23 封装**
- `FlashStorage` 模板类：
  ```cpp
  template<typename T>
  class FlashStorage {
  public:
      constexpr FlashStorage(uint32_t page_address);
      auto save(const T& params) -> std::expected<void, FlashError>;
      auto load() -> std::expected<T, FlashError>;
      auto is_valid() const -> bool;
      auto erase() -> std::expected<void, FlashError>;
  private:
      static constexpr uint32_t MAGIC = 0xA5A5'C8C8;
      struct StorageHeader {
          uint32_t magic;
          uint16_t version;
          uint16_t checksum;  // CRC16 或 Fletcher-16
      };
      auto calculate_checksum(std::span<const std::byte> data) -> uint16_t;
  };
  ```
- `Watchdog` 类：
  ```cpp
  enum class WatchdogType { Independent, Window };

  class Watchdog {
  public:
      static auto create_iwdg(uint32_t timeout_ms) -> std::expected<Watchdog, WdgError>;
      static auto create_wwdg(uint32_t window_min_ms, uint32_t window_max_ms) -> std::expected<Watchdog, WdgError>;
      auto feed() -> void;
      auto get_type() const -> WatchdogType;
  };
  ```
- 使用 `std::byteswap` 处理 Flash 数据的字节序（跨平台兼容）
- 使用 `std::array<std::byte, sizeof(T)>` 进行结构体序列化

**第 6 节：常见坑**
- Flash 编程期间不能从同一 Flash Bank 执行代码（阻塞 CPU，严重时导致 HardFault）
  - 解决方案：将 Flash 操作函数放到 RAM 中执行（`__attribute__((section(".ram_code")))`）
- 擦除次数有限（10000 次），避免频繁擦写（使用磨损均衡或日志结构）
- Flash 写入必须半字对齐（地址必须是偶数）
- IWDG 一旦启动无法停止（即使进入低功耗模式也会继续计数）
- WWDG 只有 7 位计数器（0x40-0x7F），窗口限制严格
- LSI 时钟精度低（30-60kHz 范围），IWDG 超时计算可能有较大误差
- Flash Option Bytes 错误配置可能导致芯片锁死（需要 ST-Link 解锁）

**第 7 节：练习题**
- 基础：实现一个简单的配置保存系统（上电加载，按键修改后保存到 Flash）
- 进阶：实现双区备份 Flash 写入（A/B 区交替写入，启动时选择有效区，提高可靠性）
- 挑战：实现简单的磨损均衡（维护写入指针，每次写入递增位置，区满后再擦除）

**第 8 节：可复用代码**
- `flash_storage.hpp` / `flash_storage.cpp`：FlashStorage<T> 模板类
- `watchdog.hpp` / `watchdog.cpp`：Watchdog 类
- `checksum.hpp`：CRC16 / Fletcher-16 校验算法
- `flash_params_example.hpp`：参数结构体示例与使用方法

**第 9 节：小结**
- Flash 存储与 EEPROM 的区别（按页擦除 vs 按字节、寿命、速度）
- IWDG vs WWDG 选型速查表
- 安全关键系统中看门狗的最佳实践

## 涉及文件
- documents/embedded/platforms/stm32f1/10-flash/index.md
- documents/embedded/platforms/stm32f1/11-watchdog/index.md
- code/platforms/stm32f1/10-flash/basic_write_read/
- code/platforms/stm32f1/10-flash/param_save_load/
- code/platforms/stm32f1/10-flash/cpp23_driver/
- code/platforms/stm32f1/10-flash/cpp23_driver/flash_storage.hpp
- code/platforms/stm32f1/10-flash/cpp23_driver/flash_storage.cpp
- code/platforms/stm32f1/11-watchdog/iwdg_demo/
- code/platforms/stm32f1/11-watchdog/wwdg_demo/
- code/platforms/stm32f1/11-watchdog/cpp23_driver/
- code/platforms/stm32f1/11-watchdog/cpp23_driver/watchdog.hpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md sections 8.1-8.2（Flash + 看门狗草稿内容）
- STM32F1 Reference Manual (RM0008) Chapter 3: Flash / Chapter 18: IWDG / Chapter 19: WWDG
- AN2606: STM32 microcontroller system memory boot mode
- C++23 std::span, std::expected, std::array, std::byteswap 标准
