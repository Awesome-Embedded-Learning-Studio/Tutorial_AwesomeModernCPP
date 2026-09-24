---
title: "两行编译旗子砍掉 64% 固件：-ffunction-sections 的函数粒度回收实录"
description: "给 logger 接构建系统时顺手补上 -ffunction-sections -fdata-sections，五个既有示例固件全部瘦身 37%–64%——本文给全数据表、链接器视角的机制解释、toolchain 文件改了不生效的缓存坑，以及『先记基线再动手』的方法论"
chapter: 6
order: 4
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - 工具链
  - 链接器
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 14
prerequisites:
  - "零开销日志组件完整踩坑记录：从 source_location 撞墙到反汇编验收"
related:
  - "日志组件的栈账实测：函数局部的 LineBuffer 到底会不会打穿栈"
---

# 两行编译旗子砍掉 64% 固件：-ffunction-sections 的函数粒度回收实录

## 引言：一个反直觉的起点

libestdx 的链接脚本里一直挂着 `-Wl,--gc-sections`——"链接器垃圾回收"看起来早就开了。可当我们给日志模块接构建、顺手把各示例固件的体积记成基线时，数字很平静：blinky 稳定在 5500 字节。问题在于，`--gc-sections` 开着却几乎无事可做，因为**它的回收粒度是 section，而默认布局下整个 `.o` 文件的代码挤在一个 `.text` section 里**——只要这个对象文件里有任何一个函数被引用，全部函数一起进固件。STM32 的 HAL 驱动文件动辄几十个函数，真正用到的可能只有三五个，剩下的就这么陪着进了 flash。

让 `--gc-sections` 真正开始干活，只需要两行编译旗子：

```cmake
set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb -ffunction-sections -fdata-sections")
```

## 机制：把"房间"隔成"格子"

`-ffunction-sections` 让编译器把**每个函数**放进自己的 section（`-fdata-sections` 对数据同理）。链接器做垃圾回收时，从入口点和 KEEP 标记的 section 出发沿引用链可达性分析，**函数粒度**地丢弃不可达的格子。GNU `ld` 手册和各家工具链文档对这套配合的描述是一致的（[GNU ld 文档](https://sourceware.org/binutils/docs/ld/Options.html)，[Microchip 的 function-sections 说明](https://onlinedocs.microchip.com/guides/content.xhtml?AID=1669576)）；两个工程注意点也记录在案：向量表等关键 section 需要 `KEEP()` 或 `SHF_GNU_RETAIN` 保护（[AdaCore 文档](https://docs.adacore.com/gnat_ugx_docs/views/gnat_ugx/building_executable_programs_with_gnat.html#the-order-of-sections-and-allocation-of-variables)，[MaskRay 的分析](https://maskray.me/blog/2021-01-31-metadata-sections-comdat-and-shf-link-order)），以及被回收 section 的调试信息残留问题（[LLVM 论坛讨论](https://discourse.llvm.org/t/lld-how-to-get-rid-of-debug-info-of-sections-deleted-by-gc-sections/13983)）。

## 实测：五个固件的瘦身账

动手前先记基线——这是本文的第一个方法论点：**改构建旗子之前，把每个产物的 `arm-none-eabi-size` 存档**，改完对比。没有基线的"优化"是玄学。

| 固件 | 基线 (text) | 加旗子后 | 变化 |
|---|---|---|---|
| blinky | 5500 | 3468 | **−37%** |
| gpio_example | 5500 | 3468 | −37% |
| led_example | 5520 | 3476 | −37% |
| button_example | 5552 | 3484 | −37% |
| uart_example | 15268 | 5476 | **−64%** |

blinky 只用 `HAL_Init`/`HAL_Delay`，但链接进来的 `stm32f1xx_hal.c` 等对象文件里的其余 HAL 函数全被拖进来——现在它们整格被回收。uart_example 最惨烈也最受益：UART 相关 HAL、RCC 细分路径里一半的函数根本没被走到，**15268 → 5476，砍掉近 10KB**。

## 坑：toolchain 文件改了，为什么数字一个没变

第一次改完 `MCU_FLAGS` 重新 `cmake --build`，五个固件的体积和基线**逐字节相同**——旗子根本没生效。原因是 CMake toolchain 文件里的 `CMAKE_C_FLAGS_INIT` 系列**只在 build 目录首次配置时写入缓存**，之后对 toolchain 文件的修改不会传播到已有的 `CMAKE_C_FLAGS`。验证方法很直接：查 `build/CMakeCache.txt` 里的 `CMAKE_C_FLAGS:STRING`，看到的还是旧值。解法是删掉 build 目录重新配置。这个坑的教训值得写进任何项目的构建文档：**改 toolchain 文件 = 重新配置，不是增量构建**。

顺手再补一发 `-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.`（用 `$<$<COMPILE_LANGUAGE:C,CXX>:...>` 生成器表达式挡住汇编器）：`source_location` 和 `__FILE__` 默认会把构建机的绝对路径烤进固件，每条日志点位都在为 flash 付费；prefix-map 让它们变成相对路径，也是可复现构建的前提。

## 回归：旗子不能只看体积

回收粒度变细之后，必须确认没把**该活着的** section 一起收走。我们跑了两层回归：全量重新构建五个示例（编译/链接零告警零失败），加上 Renode 自动校验 `check_uart_renode`——串口控制台的欢迎语、命令响应、行编辑、退格全部字节级核对通过。体积优化永远要带着功能回归一起交账。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 加了旗子体积纹丝不动 | toolchain `*_FLAGS_INIT` 只在首次配置进缓存 | 删 build 目录重配，或手动清 `CMAKE_*_FLAGS` 缓存 |
| 固件瘦身后起不来/ HardFault | 向量表等关键 section 被回收 | 链接脚本 `KEEP()` 或 `SHF_GNU_RETAIN` |
| 调试器跳行错乱 | 被回收 section 的调试信息残留 | 重新生成调试符号；发布构建剥离 |
| `-ffile-prefix-map` 对 `.S` 报警 | 汇编器不认识该旗子 | 生成器表达式限定 C/CXX |

## 小结

- `--gc-sections` 单独挂着基本是摆设，`-ffunction-sections -fdata-sections` 才把回收粒度降到函数级；
- 五个示例实测瘦身 37%–64%，uart 示例砍掉近 10KB——HAL 大对象文件是最大受益者；
- toolchain 文件的修改必须重新配置 build 目录才生效，"数字没变"先查缓存；
- 改旗子前记基线、改完后跑功能回归，两件小事把"优化"变成工程。

这批数据同时是日志模块的启动资金：下一篇文章里，logger 自己的固件也要站在这个被裁干净的基线上量体积。

## 参考资源

- [GNU ld 手册：Options（--gc-sections）](https://sourceware.org/binutils/docs/ld/Options.html)
- [Microchip：Function-sections Option](https://onlinedocs.microchip.com/guides/content.xhtml?AID=1669576)
- [MaskRay：Metadata sections, COMDAT and SHF_LINK_ORDER](https://maskray.me/blog/2021-01-31-metadata-sections-comdat-and-shf-link-order)
- [LLVM Discourse：被 GC 的 section 与调试信息](https://discourse.llvm.org/t/lld-how-to-get-rid-of-debug-info-of-sections-deleted-by-gc-sections/13983)
- [libestdx 仓库](https://github.com/Charliechen114514/libestdx)
