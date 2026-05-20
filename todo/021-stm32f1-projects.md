---
id: 021
title: "STM32F1 综合项目集合（串口 CLI + 模拟采集器 + 参数保存 + 低功耗唤醒）"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on:
  - "012"
  - "013"
  - "014"
  - "015"
  - "016"
  - "017"
  - "018"
  - "019"
  - "020"
blocks: []
estimated_effort: epic
---

# STM32F1 综合项目集合

## 目标
编写四个综合项目，整合前面所有外设教程（012-020）的知识点，作为 STM32F1 系列的收尾实战项目。项目包括：(1) 串口命令行 CLI（命令输入/解析、帮助系统、LED 控制、参数调整、状态读取）；(2) 模拟量采集器（ADC 多通道采样、周期定时采集、数据打印、简单滤波算法）；(3) 参数保存系统（Flash 参数持久化、断电恢复、版本兼容性）；(4) 低功耗唤醒演示（按键唤醒、Stop/Standby 休眠进入、状态恢复）。每个项目都应体现事件驱动架构和 C++23 驱动封装模式。

## 验收标准
- [ ] 项目一：串口命令行 CLI
  - [ ] 支持 UART 命令输入（中断接收 + 环形缓冲区 + 行解析）
  - [ ] 支持 `help` 命令列出所有可用命令
  - [ ] 支持 `led on` / `led off` / `led blink <freq>` 控制 LED
  - [ ] 支持 `pwm set <channel> <duty>` 调整 PWM 占空比
  - [ ] 支持 `adc read <channel>` 读取 ADC 值
  - [ ] 支持 `status` 命令读取系统状态（运行时间、ADC 值、LED 状态等）
  - [ ] 支持 `save` / `load` 保存/加载参数到 Flash
  - [ ] 命令解析器可扩展（注册新命令的接口设计）
  - [ ] 提供命令自动补全和命令历史（可选）
- [ ] 项目二：模拟量采集器
  - [ ] ADC + DMA 多通道连续采集（至少 2 个通道）
  - [ ] 定时器触发采样（可配置采样率，如 10Hz/100Hz/1kHz）
  - [ ] 滑动平均滤波 + 中值滤波可选
  - [ ] 数据通过 UART 以 CSV 格式输出（时间戳, 通道1, 通道2, ...）
  - [ ] 电压/温度等物理量转换
  - [ ] 过采样 + 降采样提高精度（可选）
- [ ] 项目三：参数保存系统
  - [ ] 定义参数结构体（LED 状态、PWM 占空比、ADC 校准值、采样率等）
  - [ ] Flash 参数区设计：魔数 + 版本号 + 参数 + CRC 校验
  - [ ] 上电自动加载参数，参数无效时使用默认值
  - [ ] `save` 命令触发保存，`load` 命令触发加载
  - [ ] 参数版本兼容：新增字段时旧版本参数仍可使用（默认值填充）
  - [ ] 看门狗保护 Flash 写入过程
- [ ] 项目四：低功耗唤醒演示
  - [ ] 按键触发进入 Stop/Standby 模式
  - [ ] Stop 模式：RTC 闹钟定时唤醒 + 按键唤醒
  - [ ] Standby 模式：WKUP 引脚唤醒 + RTC 闹钟唤醒
  - [ ] 进入休眠前保存状态到备份寄存器
  - [ ] 唤醒后从备份寄存器恢复状态并继续运行
  - [ ] 功耗测量与优化报告
- [ ] 每个项目都有完整的 README 和代码注释
- [ ] 每个项目都有独立的 CubeMX 配置文件（.ioc）
- [ ] 所有代码在 STM32F103C8T6 上验证通过

## 实施说明
### 项目一：串口命令行 CLI

**架构设计**：
- 使用事件驱动架构（020 教程的模式）
- UART 中断接收 -> 行缓冲区 -> 行完成事件 -> 命令解析 -> 命令执行
- 命令注册表：`std::array<CommandEntry, MAX_COMMANDS>`，每项包含命令名、帮助文本、处理函数

**核心组件**：
```
UartRx (中断) -> LineBuffer -> CommandParser -> CommandDispatcher
                                                       |
                                              +--------+--------+
                                              |        |        |
                                           LedCmd   PwmCmd   AdcCmd ...
```

**命令框架**：
```cpp
using CommandHandler = auto(std::span<std::string_view> args) -> void;

struct CommandEntry {
    std::string_view name;
    std::string_view help;
    CommandHandler* handler;
};

class CommandLine {
    std::array<char, 128> line_buffer_{};
    size_t pos_{0};
    std::array<CommandEntry, 16> commands_{};
    size_t cmd_count_{0};
public:
    auto register_command(std::string_view name, std::string_view help, CommandHandler* handler) -> void;
    auto feed(char c) -> void;  // 每收到一个字符调用
    auto execute() -> void;     // 收到 \n 时解析并执行
};
```

### 项目二：模拟量采集器

**架构设计**：
- ADC + DMA 循环采集 -> 半传输/完成中断 -> 滤波处理 -> UART 输出
- 定时器控制采样率（TIM 触发 ADC）
- 主循环中处理已采集的数据块

**核心流程**：
```
TIM (定时触发) -> ADC (转换) -> DMA (搬运到缓冲区) -> 中断通知
                                                              |
                                                    主循环读取缓冲区
                                                         |
                                                    滤波处理
                                                         |
                                                    UART 输出 (DMA)
```

### 项目三：参数保存系统

**参数结构体设计**：
```cpp
struct SystemParams {
    static constexpr uint32_t MAGIC = 0xA5C8'0100;  // 版本 1.0
    uint32_t magic;
    uint16_t version;
    uint16_t checksum;

    // 用户参数
    bool led_on;
    uint8_t pwm_duty;       // 0-100
    uint8_t led_blink_freq; // Hz
    uint16_t adc_sample_rate; // Hz
    float adc_cal_offset;
    uint32_t uptime_seconds;
};
```

**版本兼容策略**：
- 每次修改参数结构体时递增 version
- 加载时检查 version：版本匹配直接使用，版本较低用默认值填充新字段，版本过高拒绝加载

### 项目四：低功耗唤醒演示

**状态保存与恢复**：
```cpp
struct PowerState {
    uint32_t magic;         // 验证备份域数据有效性
    SystemState state;      // 当前系统状态
    uint32_t uptime;        // 运行时间（秒）
    uint16_t wakeup_count;  // 唤醒次数
};
// 保存到备份寄存器（BKP_DR1-DR10，共 20 字节）
```

**演示流程**：
1. 正常运行：LED 闪烁 + UART 交互
2. 收到 `sleep` 命令或长按按键 -> 保存状态 -> 进入 Stop
3. RTC 闹钟或按键唤醒 -> 恢复时钟 -> 恢复状态 -> 继续运行
4. 收到 `standby` 命令 -> 保存状态 -> 进入 Standby
5. WKUP 引脚唤醒 -> 系统复位 -> 检测到 Standby 恢复 -> 加载状态 -> 继续

## 涉及文件
- documents/embedded/platforms/stm32f1/17-projects/index.md
- documents/embedded/platforms/stm32f1/17-projects/01-cli/README.md
- documents/embedded/platforms/stm32f1/17-projects/02-adc-collector/README.md
- documents/embedded/platforms/stm32f1/17-projects/03-param-save/README.md
- documents/embedded/platforms/stm32f1/17-projects/04-low-power/README.md
- code/platforms/stm32f1/17-projects/01-cli/
- code/platforms/stm32f1/17-projects/01-cli/Core/
- code/platforms/stm32f1/17-projects/01-cli/app/command_line.hpp
- code/platforms/stm32f1/17-projects/01-cli/app/command_line.cpp
- code/platforms/stm32f1/17-projects/01-cli/app/commands.hpp
- code/platforms/stm32f1/17-projects/02-adc-collector/
- code/platforms/stm32f1/17-projects/02-adc-collector/app/adc_collector.hpp
- code/platforms/stm32f1/17-projects/02-adc-collector/app/filters.hpp
- code/platforms/stm32f1/17-projects/03-param-save/
- code/platforms/stm32f1/17-projects/03-param-save/app/flash_params.hpp
- code/platforms/stm32f1/17-projects/03-param-save/app/flash_params.cpp
- code/platforms/stm32f1/17-projects/04-low-power/
- code/platforms/stm32f1/17-projects/04-low-power/app/power_state.hpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md section 10（综合项目草稿内容）
- 教程 012-020 的所有可复用代码模块
- STM32CubeF1 HAL Driver 全部相关文档
- C++23 标准全套特性参考
