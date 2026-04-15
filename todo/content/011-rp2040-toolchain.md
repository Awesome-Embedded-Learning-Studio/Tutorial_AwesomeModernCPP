---
id: 011
title: "RP2040 开发环境搭建教程（Pico SDK + CMake + ARM 工具链 + Wokwi 仿真）"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# RP2040 开发环境搭建教程

## 目标
编写 Raspberry Pi Pico / RP2040 平台的开发环境搭建教程，涵盖 Pico SDK 安装、ARM GCC 交叉编译工具链配置、CMake 构建系统、UF2 烧录方式，以及 Wokwi 在线仿真器的使用。读者应能从零搭建本地开发环境并完成 LED Blink 程序，同时掌握 Wokwi 在线仿真用于快速原型验证。

## 验收标准
- [ ] 教程覆盖 ARM GCC 工具链（arm-none-eabi-gcc）的安装与验证
- [ ] Pico SDK 的克隆与 CMake 构建配置（包括 pico-sdk 子模块初始化）
- [ ] 第一个 LED Blink 项目的创建、编译、UF2 烧录完整流程
- [ ] CMakeLists.txt 详解：`pico_sdk_init()`、`target_link_libraries`、pico_enable_stdio_usb/uart
- [ ] VSCode 配置：CMake Tools 扩展 + Cortex-Debug 扩展 + OpenOCD 调试
- [ ] Wokwi 在线仿真器集成：项目配置（wokwi.toml）、diagram.json、VSCode Wokwi 插件
- [ ] 常见问题排错：SDK 路径、子模块缺失、串口驱动、BOOTSEL 模式说明
- [ ] 提供 C++ 版本的 LED Blink 示例代码

## 实施说明
教程应按以下结构组织：

1. **环境要求**：列出操作系统、CMake 版本（>=3.13）、ARM工具链、Python、Git 等前置条件
2. **工具链安装**：
   - Linux: `sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential`
   - macOS: Homebrew 安装 `arm-none-eabi-gcc`
   - Windows: ARM GNU Toolchain 安装包 + MinGW
3. **Pico SDK 安装**：`git clone --recursive https://github.com/raspberrypi/pico-sdk`，环境变量 `PICO_SDK_PATH` 设置
4. **第一个项目**：从 pico-examples 的 blink 示例入手，解析项目结构（.cpp、CMakeLists.txt、pico_sdk_import.cmake）
5. **编译与烧录**：
   - `cmake -B build` + `cmake --build build` 生成 `.uf2` 文件
   - BOOTSEL 模式拖拽烧录的详细步骤
   - picotool 命令行烧录方式
6. **串口输出**：stdio 通过 USB 或 UART 的配置方法，`pico_enable_stdio_usb()` 和 `pico_enable_stdio_uart()`
7. **Wokwi 仿真器**：
   - 在线版使用：直接在 wokwi.com 创建 RP2040 项目
   - VSCode 集成：安装 Wokwi for VSCode 扩展，配置 `wokwi.toml` 和 `diagram.json`
   - 仿真 vs 实机的差异与注意事项
8. **调试配置**：OpenOCD + GDB 的 SWD 调试配置（使用 Raspberry Pi Debug Probe 或 picoprobe）
9. **常见问题**：UF2 不出现、串口不输出、SDK 版本兼容性

## 涉及文件
- documents/embedded/platforms/rp2040/00-toolchain/index.md
- documents/embedded/platforms/rp2040/00-toolchain/code/blink/
- documents/embedded/platforms/rp2040/00-toolchain/code/blink/blink.cpp
- documents/embedded/platforms/rp2040/00-toolchain/code/blink/CMakeLists.txt

## 参考资料
- [Raspberry Pi Pico 官方入门指南](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)
- [Pico SDK 文档](https://www.raspberrypi.com/documentation/microcontrollers/)
- [Wokwi 文档](https://docs.wokwi.com/)
- [Wokwi VSCode 扩展](https://docs.wokwi.com/vscode/getting-started)
- [ARM GNU Toolchain](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain)
