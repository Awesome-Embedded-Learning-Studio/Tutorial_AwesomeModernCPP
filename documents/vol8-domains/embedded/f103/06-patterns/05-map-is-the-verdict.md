---
title: "终审在 map：『裁剪级别连字符串都不进固件』的完整证据链"
description: "host 上验编译期裁剪有三重陷阱（空 sink 被整链删除、常量行被折叠成立即数、字面量直接变成 movabs），本文从这些失败出发走到 ARM 的权威终审：map 与 bin 双查、MinLevel=Info/Debug 的对照实验（+508 字节与零残留），顺带落成 B 契约的 try_send 推导超时"
chapter: 6
order: 5
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - 工具链
  - 寄存器
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 16
prerequisites:
  - "两行编译旗子砍掉 64% 固件：-ffunction-sections 的函数粒度回收实录"
related:
  - "零开销日志组件完整踩坑记录：从 source_location 撞墙到反汇编验收"
---

# 终审在 map：『裁剪级别连字符串都不进固件』的完整证据链

## 引言：一个宣称的三种死法

日志组件的核心宣称是：**低于 `MinLevel` 的日志，连同字符串字面量一起，不进固件**。这句话要成立，光写代码是不够的，得拿出证据。我们最初的验证思路朴素：在 host 上编译一个带标记串的测试程序，`strings` 一查便知。结果这个思路死了三次，每次死法不同——而这三次失败本身就是这篇文章最值钱的部分，因为它们展示了优化器"骗过"测量手段的三种方式。最终我们走到了 ARM 固件的 map 文件和 bin 双查，拿到了权威判决。

## 三重陷阱：host 上的测量为什么靠不住

**陷阱一：空 sink 让整条日志链蒸发。** 测试用的 sink 是个空函数，编译器正确地判定日志没有任何可观察效果，反汇编里包装函数只剩 `bx lr`。要测开销，sink 必须有 `volatile` 副作用。

**陷阱二：常量输入让整行编译期折叠。** sink 改成可观察后，用常量时钟再测——编译器把整行装配在编译期算完，`movs r2, #26` 一条指令给出 26 字节的行长。栈帧零、周期零。这是零开销的惊喜，但作为测量方法就是灾难：输入必须来自 `volatile`。

**陷阱三：字面量根本不进 `.rodata`。** 最隐蔽的一层。给被裁和保留的两个级别分别埋了 23 字节和 38 字节的标记串，`strings` 双查——结果**两个都查不到**。反汇编揭晓：x86-64 上 GCC 把字符串内容拆成 `movabs` 立即数直接写进指令流（`movabs $0x346dc5d63886594b,%rsi`），.rodata 里当然什么都没有。也就是说 host 上的 `strings` 检查既会**漏报**（该在的不在）也可能**误报**（不在不代表被裁了）。

结论：host 是开发环境，不是宣判环境。**裁剪的终审在目标机的产物上**。

## ARM 终审：map 与 bin 的双查

验证设计：示例固件里埋一条 debug 级别的标记行（`debug-marker-must-not-survive-MinLevel-Info`），分别以 `MinLevel=Info` 和 `MinLevel=Debug` 构建真实的 STM32 固件，然后：

```sh
grep -c 'must-not-survive' build/examples/06_log/log_example.map
arm-none-eabi-strings -a build/examples/06_log/log_example.bin | grep -c must-not-survive
```

判决书：

| MinLevel | text | marker in map | marker in bin |
|---|---|---|---|
| Debug | 18708 | 0 | **1**（级别编进来，+508 字节） |
| Info | 18200 | 0 | **0**（连字面量都零残留） |

三件事一次说清：**级别开就进、关就没、差价 508 字节**。顺带一个细节：map 文件里两边都是 0——map 索引的是符号和 section，字符串字面量是匿名 `.rodata` 数据，不产符号，所以 **bin 的 `strings` 才是字面量层面的正确探针**，map 用来回答"哪个函数/section 被裁了"。

运行时还有第二道兜底：Renode 自动校验脚本在字节流里断言 `[Debug` 前缀全程不出现——编译期裁剪若因构建配置意外失效，运行时校验会立刻尖叫。证据链 thus 三层：反汇编（host 开发期）、bin/map（目标产物）、流内容（运行时）。

## 顺带落成的 B 契约：try_send 与推导超时

日志的 sink 契约是"尽力而为：吞错、不 trap、不无界阻塞"。最后一条有个隐藏深坑：HAL 的 `HAL_UART_Transmit` 传 `HAL_MAX_DELAY` 时，如果 UART 时钟没开，状态标志永远不来，函数**永久挂死**——根本走不到"返回错误"那步。所以 B 契约的真保障不是丢弃返回值，而是**有界超时**。

UART 驱动因此新增了 `try_send`：与原 `send`（保持 trap 语义的驱动契约）并存，超时不是拍脑袋常数，而是从**编译期已知的波特率推导**——8N1 每字节 10 个线位，毫秒数 = 字节数 × 10000 ÷ 波特率，再翻倍留余量。这里固定常数会翻车：9600 波特下 128 字节行合法耗时约 133ms，写死 100ms 会把**活的慢链路**误杀；推导超时对任何波特率都自洽。死串路的代价从"整机挂死"变成"每行日志多等一个有界超时"——这是尽力而为契约诚实的价格。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| host 上 strings 查无字面量 | GCC 用 movabs 立即数内联字符串 | 终审放 ARM 产物的 map/bin |
| map 里查字符串常查不到 | 字面量无符号、匿名 .rodata | 字面量用 bin+strings，符号用 map |
| "数字没变"的裁剪验证 | 空输入/常量输入被折叠或 DSE | volatile 输入 + 可观察 sink |
| 慢波特率下 try_send 误判失败 | 固定超时常数 | 按波特率推导（字节×10bit÷baud×2） |

## 小结

- 编译期裁剪的宣称需要三层证据：host 反汇编（开发期直觉）、目标产物 map/bin（权威判决）、运行时流校验（兜底）；
- host 测量有三重陷阱：DSE、常量折叠、movabs 立即数——strings 在 host 上是弱证据；
- 对照实验一锤定音：`MinLevel=Info/Debug` 两次构建，marker 零残留/在场，差价 508 字节；
- "不无界阻塞"的真保障是推导超时，不是丢弃返回值——`HAL_MAX_DELAY` 在死硬件上是永久挂死。

## 参考资源

- [GNU ld 手册：Options（--gc-sections）](https://sourceware.org/binutils/docs/ld/Options.html)
- [arm-none-eabi binutils：strings / objdump](https://sourceware.org/binutils/docs/binutils/strings.html)
- [libestdx 仓库（examples/06_log 与 logger 模块）](https://github.com/Charliechen114514/libestdx)
