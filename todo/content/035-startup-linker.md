---
id: 035
title: "启动流程和链接脚本深度分析"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002"]
blocks: []
estimated_effort: large
---

# 启动流程和链接脚本深度分析

## 目标
深入讲解 ARM Cortex-M 微控制器的启动流程和链接脚本。从上电复位到 main 函数执行的完整过程，涵盖 startup.s 汇编启动代码、向量表结构、Reset_Handler 流程、.data 段搬运、.bss 段清零、全局构造函数调用、链接脚本（MEMORY/SECTIONS/Flash-RAM 布局）、自定义段设计，以及 readelf/objdump/size 等分析工具的使用。

## 验收标准
- [ ] startup.s 完整注释版代码和逐行解读
- [ ] 向量表结构详细文档（含 Cortex-M 异常类型）
- [ ] Reset_Handler 到 main() 的完整流程图
- [ ] 链接脚本完整解读（MEMORY + SECTIONS）
- [ ] 自定义段实现示例（DMA 对齐段、Flash 参数区）
- [ ] 全局构造函数调用机制（.init_array 段）文档
- [ ] readelf/objdump/size 工具使用教程
- [ ] 常见启动问题排查指南（HardFault 调试）
- [ ] C++ 特有启动问题（静态初始化顺序、pure virtual call）

## 实施说明
启动流程和链接脚本是嵌入式开发的基础知识，但往往被初学者忽视。本教程帮助读者建立从硬件上电到软件运行的完整认知链。

**内容结构规划：**

1. **上电启动流程概述** — ARM Cortex-M 上电行为：从向量表获取初始 MSP 和 Reset_Handler 地址。启动流程全景图：上电 -> 向量表 -> Reset_Handler -> SystemInit -> __libc_init_array -> main()。为什么需要启动代码：C 运行时环境初始化。

2. **向量表（Vector Table）** — 向量表结构：前 16 个系统异常 + N 个设备中断。STM32F103 的完整向量表映射。向量表的链接位置（Flash 起始地址 0x08000000）。VTOR 寄存器：向量表重定位（用于 Bootloader 跳转）。C++ 中的中断服务函数声明（extern "C"）。

3. **startup.s 汇编详解** — 完整的 startup.s 逐行注释。栈和堆的定义（_estack、_sidata、_sdata、_edata、_sbss、_ebss 符号）。Reset_Handler 流程：调用 SystemInit -> 复制 .data 段 -> 清零 .bss 段 -> 调用 __libc_init_array -> 调用 main。Default_Handler：无限循环的默认中断处理。弱符号（.weak）和默认处理函数的链接机制。

4. **.data 段搬运与 .bss 段清零** — 为什么 .data 需要搬运：Flash 中存储初始值，RAM 中运行。搬运实现：从 _sidata 到 _sdata 的 memcpy 等价操作。.bss 段清零：为什么需要清零（C 标准要求未初始化全局变量为零）。RWM（Read-Write Memory）初始化的完整代码。

5. **全局构造函数调用** — C++ 特有的启动需求：全局/静态对象的构造函数必须在 main 之前调用。.init_array 段：编译器将构造函数指针放入此段。__libc_init_array 函数：遍历 .init_array 并逐个调用。.preinit_array 和 .init 段的区别。问题：静态初始化顺序（SIOF）和解决方案。嵌入式中的最佳实践：避免全局构造、使用显式 init() 函数。

6. **链接脚本（Linker Script）详解** — MEMORY 命令：定义 Flash 和 RAM 区域。SECTIONS 命令：.isr_vector、.text、.rodata、.data、.bss 的布局。符号导出：PROVIDE 和全局符号。VMA vs LMA：虚拟内存地址和加载内存地址。AT() 指令：指定段的加载地址。KEEP() 命令：防止链接器优化掉看似未引用的段。

7. **自定义段** — DMA 对齐段：__attribute__((section(".dma_buffer"))) + ALIGN(4)。Flash 参数区：固定地址的配置数据段。NOINIT 段：不被启动代码清零的 RAM 区域（保持复位值）。自定义段在链接脚本中的定义。在 C++ 中使用自定义段的属性语法。

8. **二进制分析工具** — readelf：查看段头（-S）、符号表（-s）、程序头（-l）。objdump：反汇编（-d）、段内容（-s）。size：代码段/数据段/BSS 段大小。nm：符号地址查看。map 文件分析：内存分布详情、未使用符号检测。从 map 文件定位内存溢出问题。

9. **常见启动问题排查** — HardFault 常见原因：栈溢出、非法地址访问、未对齐访问。HardFault 调试技巧：查看寄存器（CFSR/HFSR/MMFAR/BFAR）。链接错误排查：undefined reference、multiple definition、region overflow。静态初始化问题的调试策略。

## 涉及文件
- documents/embedded/topics/startup-linker/index.md
- documents/embedded/topics/startup-linker/01-boot-overview.md
- documents/embedded/topics/startup-linker/02-vector-table.md
- documents/embedded/topics/startup-linker/03-startup-asm.md
- documents/embedded/topics/startup-linker/04-data-bss-init.md
- documents/embedded/topics/startup-linker/05-global-constructors.md
- documents/embedded/topics/startup-linker/06-linker-script.md
- documents/embedded/topics/startup-linker/07-custom-sections.md
- documents/embedded/topics/startup-linker/08-binary-tools.md
- documents/embedded/topics/startup-linker/09-troubleshooting.md
- codes/embedded/startup-linker/ (配套代码和链接脚本)

## 参考资料
- ARM Cortex-M3 Technical Reference Manual (DDI 0337)
- ARM Architecture Procedure Call Standard (AAPCS)
- GNU LD 链接脚本手册 (sourceware.org/binutils/docs/ld)
- 《Making Embedded Systems》(2nd Ed) — Elecia White
- STM32F103 Programming Manual (PM0056)
- GCC 内联汇编和属性文档
- Joseph Yiu 的《The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors》
