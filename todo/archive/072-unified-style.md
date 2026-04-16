---
id: "072"
title: "统一内容风格：术语表、代码风格与标签体系"
category: branding
priority: P2
status: done
created: 2026-04-15
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# 统一内容风格：术语表、代码风格与标签体系

## 目标

建立项目级的统一内容标准，确保所有教程文章在术语使用、代码风格、文章模板和 frontmatter 标签体系上保持一致。具体包括：

1. **术语表**：中英文对照的技术术语标准翻译，避免同一概念在不同文章中出现不同译法
2. **代码风格指南**：与 `.claude/writting_style.md` 协调的统一代码风格
3. **文章模板更新**：更新 `.templates/` 中的模板，确保新文章遵循统一格式
4. **Frontmatter 标签体系**：建立标准化的标签分类，用于内容分类和检索

## 验收标准

- [ ] `documents/appendix/terminology.md` 术语表已创建，包含至少 100 个常用术语的中英文对照
- [ ] 术语表覆盖以下领域：C++ 语言特性、嵌入式硬件、RTOS、工具链、调试
- [ ] `.templates/` 中的文章模板已更新，反映最新的 frontmatter 规范和内容结构要求
- [ ] `.claude/writting_style.md` 中的代码风格部分已审查和补充
- [ ] Frontmatter 标签体系统一，定义以下标签分类维度：
  - `platform`: stm32f1, stm32f4, esp32, rp2040, host
  - `topic`: cpp-modern, peripheral, rtos, debugging, toolchain, architecture
  - `difficulty`: beginner, intermediate, advanced
  - `peripheral`: gpio, uart, spi, i2c, adc, timer, pwm, dma, can
- [ ] 已有文章中的标签已完成统一化迁移（移除非标标签，使用标准标签替换）
- [ ] 代码风格指南包含：命名规范（文件/变量/函数/类）、注释规范、include 顺序、格式化规则

## 实施说明

### 术语表设计

术语表按领域分组，每个条目包含：

```markdown
| 英文 | 中文 | 备注 |
|------|------|------|
| stack unwinding | 栈展开 | 异常处理相关 |
| move semantics | 移动语义 | C++11 核心特性 |
| peripheral | 外设 | 嵌入式语境 |
```

需要覆盖的领域及示例术语：

**C++ 语言特性**：RAII、SFINAE、CRTP、perfect forwarding、copy elision、zero-overhead abstraction、UB (undefined behavior)、ODR (one definition rule)...

**嵌入式硬件**：MCU、SoC、register、interrupt、DMA、PLL、AHB/APB bus、GPIO、ADC、DAC、PWM、watchdog...

**RTOS**：scheduler、context switch、priority inversion、mutex、semaphore、queue、task、tick、deadline...

**工具链**：cross-compile、toolchain、linker script、startup code、flash、debug probe、JTAG、SWD...

**调试**：breakpoint、watchpoint、trace、semihosting、ITM、ETM、logic analyzer、oscilloscope...

### Frontmatter 标签体系

```yaml
# 文章 frontmatter 标签示例
tags:
  - stm32f1          # platform
  - gpio              # peripheral
  - beginner          # difficulty
  - peripheral        # topic
```

标签使用规则：
- 每篇文章必须包含 1 个 `platform` 标签（或 `host`）
- 每篇文章必须包含 1 个 `difficulty` 标签
- 每篇文章至少包含 1 个 `topic` 标签
- `peripheral` 标签仅在有外设相关内容时添加
- 标签使用英文小写，中划线分隔

### 代码风格指南要点

```cpp
// 文件命名：snake_case，如 gpio_input.cpp
// 类命名：PascalCase，如 GpioPin
// 函数命名：snake_case，如 configure_pull_up()
// 常量命名：kPascalCase，如 kMaxRetryCount
// 宏命名：UPPER_SNAKE_CASE，如 CONFIG_UART_BAUDRATE

// Include 顺序：
// 1. 对应头文件（如 gpio.h）
// 2. C++ 标准库
// 3. 第三方库
// 4. 项目内头文件

// 注释风格：Doxygen 格式
/// @brief 配置 GPIO 引脚的上拉电阻
/// @param pin GPIO 引脚编号
/// @return 配置是否成功
bool configure_pull_up(uint8_t pin);
```

### 文章模板更新

```markdown
---
title: "文章标题"
date: YYYY-MM-DD
tags:
  - platform
  - topic
  - difficulty
difficulty: beginner|intermediate|advanced
reading_time: NN
platform: stm32f1|esp32|rp2040|host
prerequisites:
  - "前置知识1"
  - "前置知识2"
hardware:
  - "硬件需求1"
---

# 文章标题

## 前言
简要说明本文目标和学习成果。

## 正文
...

## 总结
...

## 下一步
推荐后续阅读。

## 参考资料
- 参考链接
```

### 迁移策略

1. 先创建术语表和标签体系文档
2. 编写脚本扫描现有文章的标签，列出非标标签
3. 手动审核并替换非标标签
4. 更新文章模板
5. 后续通过 CI (content-quality) 检查新文章标签合规

## 涉及文件

- `.templates/` — 文章模板目录
- `documents/appendix/terminology.md` — 术语表
- `.claude/writting_style.md` — 写作风格指南（补充代码风格部分）

## 参考资料

- [C++ Core Guidelines 命名约定](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Snaming)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [MkDocs Material 标签插件](https://squidfunk.github.io/mkdocs-material/setup/setting-up-tags/)
