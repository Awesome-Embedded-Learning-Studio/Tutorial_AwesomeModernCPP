---
id: 042
title: "STM32F1 工程准备教程（工具链 + freestanding + 启动文件 + BSP）"
category: content
priority: P1
status: pending
created: 2026-05-22
assignee: charliechen
depends_on:
  - "architecture/002"
blocks:
  - "012"
  - "013"
  - "014"
  - "015"
  - "016"
  - "017"
  - "018"
  - "019"
estimated_effort: large
---

# STM32F1 工程准备教程

## 目标
编写 STM32F1 系列教程的工程准备章节，作为 012-021 外设教程的前置基础。覆盖四大主题：(1) 工具链安装与交叉编译环境搭建（arm-none-eabi-gcc、CMake、VS Code 配置）；(2) freestanding 策略（裸机环境下的 C++ 子集选择、禁止项与推荐项）；(3) 启动文件与链接脚本（startup.s、向量表、Reset_Handler、Flash/RAM 布局）；(4) 最小板级抽象（board/ 目录职责、Blue Pill 资源分配、LED/按键/串口命名规范）。

## 验收标准
- [ ] 1.1 工具链与构建系统完成
- [ ] 1.2 freestanding 策略完成
- [ ] 1.3 启动文件与链接脚本完成
- [ ] 1.4 最小 BSP 与板级抽象完成
- [ ] 读者能一键编译并生成可下载固件（.elf / .bin / .hex）
- [ ] 读者能解释 `main()` 之前的启动流程
- [ ] 读者能区分 freestanding 与 hosted 环境的差异
- [ ] 板级资源定义集中管理，上层不直接关心 pin 号细节
- [ ] 所有代码在 STM32F103C8T6（Blue Pill）上验证通过

## 实施说明

### 1.1 工具链与构建系统

**目标：** 让读者完成交叉编译环境的基本搭建，理解"为什么需要独立工具链和构建系统"。

- [ ] arm-none-eabi-gcc 的作用与安装
- [ ] binutils 工具链：objdump、size、readelf 的用途
- [ ] CMake 交叉编译配置（toolchain file 编写）
- [ ] VS Code 任务配置（build/flash 任务）和调试配置（Cortex-Debug 插件）
- [ ] 编译产物解读：`.elf`、`.bin`、`.hex` 的区别与用途
- [ ] 烧录与下载方式（ST-Link / OpenOCD / serial bootloader）

**C++23 关联：**
- 建议开启 `-std=c++23`
- 建议关闭异常、RTTI（`-fno-exceptions -fno-rtti`），按需讨论
- 编译选项优化等级选择（`-Og` 调试 / `-Os` 发布）

**验收：**
- 工程成功编译
- 输出文件可被下载器识别
- 板子能进入最小可运行状态

### 1.2 freestanding 策略

**目标：** 让读者明确在裸机环境下哪些 C++ 用法优先，哪些应当谨慎。

- [ ] freestanding 与 hosted 的区别（标准库头文件分类：freestanding vs hosted）
- [ ] 为什么裸机教程不建议一开始就依赖完整标准库生态
- [ ] 固定容量容器优先（`std::array`、`std::span` 替代 `std::vector`）
- [ ] 运行时分配最小化（禁用/限制 `new/delete`）
- [ ] 错误处理优先返回值 / `std::expected`（替代异常）
- [ ] 输出策略：不依赖 `iostream`（自定义轻量日志）

**输出规范建议：**
- 主路径不依赖 `new/delete`
- 主路径不依赖异常
- 主路径不依赖 RTTI
- 主路径不依赖 iostream

**C++23 关联：**
- `std::expected` — 统一错误处理
- `std::span` — 安全的缓冲区传递
- `std::array` — 固定容量容器
- `std::byte` — 类型安全的原始字节
- `std::to_underlying` — 枚举底层值转换
- `constexpr` / `consteval` / `if consteval` — 编译期策略

**验收：**
- 读者能够明确哪些库在教程中"默认可用"，哪些是"可选增强"
- 读者理解为什么这样设计更适合 STM32F103

### 1.3 启动文件与链接脚本

**目标：** 让读者知道程序为什么能从 Flash 跑起来，以及启动阶段发生了什么。

- [ ] 启动文件 `startup_stm32f103xb.s` 分析
- [ ] 向量表（Vector Table）结构：栈指针 + 异常/中断入口
- [ ] `Reset_Handler` 流程：`SystemInit` → `.data` 搬运 → `.bss` 清零 → `main`
- [ ] 数据段 `.data`：从 Flash 搬运到 RAM
- [ ] BSS 段 `.bss`：初始化为零
- [ ] 链接脚本中的 Flash / RAM 布局（`MEMORY` 命令、`SECTIONS` 命令）
- [ ] 栈与堆的概念（栈大小配置、堆的取舍）

**C++23 关联：**
- `constexpr` 定义内存布局常量（Flash 大小、RAM 大小、栈大小）
- 无直接语言特性为主，重点是"编译期配置"思想

**验收：**
- 读者能解释 `main()` 之前的流程
- 读者知道为什么 `.data` 需要搬运、`.bss` 需要清零
- 读者知道链接脚本为什么不是可有可无

### 1.4 最小 BSP 与板级抽象

**目标：** 建立"板级差异收口"的基本架构，让上层应用不直接关心引脚细节。

- [ ] `board/` 目录职责与文件组织
- [ ] Blue Pill (STM32F103C8T6) 的板级资源分配（LED、按键、串口、调试口）
- [ ] LED / 按键 / 串口 / 调试口的命名规范
- [ ] 板级初始化入口（`board_init()` 的职责）
- [ ] 引脚配置集中管理策略（`board_pins.hpp` 示例）

**C++23 关联：**
- `enum class` 用于硬件资源枚举（`enum class Port : char { A, B, C }`）
- `std::to_underlying` 用于枚举索引和表驱动结构
- `[[nodiscard]]` 标记初始化结果函数

**验收：**
- 板级资源定义集中在一个文件
- 上层应用不直接写 `GPIOA, GPIO_PIN_5` 等裸常量
- 切换板子（如 Blue Pill → 最小系统板）只需修改 board 文件

### 附：C++23 特性章节引入规划

以下是建议在 STM32F1 教程中逐步引入的 C++23 特性：

| 特性 | 优先级 | 首次引入 | 主要用途 |
|------|--------|----------|----------|
| `std::expected` | 优先 | 1.2 | 统一错误处理 |
| `std::span` | 优先 | 1.2 | 安全缓冲区 |
| `std::array` | 优先 | 1.2 | 固定容器 |
| `std::byte` | 优先 | 1.2 | 类型安全字节 |
| `std::to_underlying` | 优先 | 1.2 | 枚举转换 |
| `constexpr` | 优先 | 1.3 | 编译期常量 |
| `consteval` | 优先 | 时钟树 | 编译期强制计算 |
| `if consteval` | 优先 | 时钟树 | 编译期分支 |
| `enum class` | 优先 | GPIO | 强类型枚举 |
| `[[nodiscard]]` | 优先 | GPIO | 忽略返回值警告 |
| `std::byteswap` | 进阶 | SPI | 字节序处理 |
| `deducing this` | 进阶 | 事件驱动 | 简化 CRTP |
| 静态 lambda | 进阶 | 驱动封装 | ISR 无捕获回调 |

### 附：freestanding 使用约束速查

**默认推荐：** 固定容量容器、静态存储期对象、编译期配置、无捕获回调、显式错误返回

**默认谨慎：** `new/delete`、`exception`、`RTTI`、`iostream`、过度依赖动态分配的容器与算法

**可选扩展：** 某些 STL 算法（`<algorithm>`）、轻量 `<utility>` 能力（`std::move`、`std::forward`）、某些工具库

前提是不破坏教程的"可移植、可解释、可教学"目标。

## 涉及文件
- documents/vol8-domains/embedded/stm32f1/ch00-engineering-prep/index.md
- documents/vol8-domains/embedded/stm32f1/ch00-engineering-prep/01-toolchain-build.md
- documents/vol8-domains/embedded/stm32f1/ch00-engineering-prep/02-freestanding-strategy.md
- documents/vol8-domains/embedded/stm32f1/ch00-engineering-prep/03-startup-linker.md
- documents/vol8-domains/embedded/stm32f1/ch00-engineering-prep/04-board-bsp.md

## 参考资料
- ARM Cortex-M3 Technical Reference Manual
- STM32F103 Reference Manual (RM0008)
- GCC ARM Embedded 工具链文档
- C++23 Freestanding 提案（P2338）
- ISO C++ Freestanding 章节（[compliance])
