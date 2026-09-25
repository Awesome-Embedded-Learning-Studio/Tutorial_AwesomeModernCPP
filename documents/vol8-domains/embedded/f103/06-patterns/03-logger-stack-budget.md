---
title: "日志组件的栈账实测：函数局部的 LineBuffer 到底会不会打穿栈"
description: "emit 里那个栈上的 LineBuffer<128> 被灵魂三问：为什么不是对象级持有？会不会打穿栈？——三个测量教训（空 sink 被整链删除、常量输入被编译期折叠、显式实例化撞编译器 ICE）、两种测量方法互相验证出 376 字节瞬态帧、帧只随嵌套深度不随调用点数量累加的性质，以及从链接脚本到 RTOS 线程栈的完整预算链路"
chapter: 6
order: 3
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - 嵌入式
  - 实战
  - 内存管理
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 16
prerequisites:
  - "零开销日志组件完整踩坑记录：从 source_location 撞墙到反汇编验收"
related:
  - "从重载集到 Formatter 特化：一次嵌入式日志分发机制的选型讨论"
---

# 日志组件的栈账实测：函数局部的 LineBuffer 到底会不会打穿栈

## 引言：一个栈上数组的灵魂三问

写 `Logger::emit` 的核心就一行——在栈上开一个行组装器，装配，写出去：

```cpp
LineBuffer<LINE_BYTES> line;
// ...装配各段...
Sink::write(line.finish(kTruncateMark, kLineEnd));
```

就这一行，被连问了三个问题：**为什么是函数局部、不是对象级持有？** 128 字节在栈上，**会不会打穿？** 打穿了怎么办？这三个问题恰好是嵌入式工程师对"零开销"宣称的合理怀疑——你说零开销，那栈呢？这篇文章把三个测量教训、两份互相验证的实测数据、和一整条栈预算链路摆出来。先剧透结论：函数局部是对的，376 字节是瞬态的，真正会打穿的场景将来只有一个——RTOS 定长线程栈。

## 为什么是函数局部：三个理由，一个比一个硬

**第一，"对象级持有"在这个库里不存在。** libestdx 的词汇是全静态接口：`Uart`、`Gpio`、`LED`、`Formatter` 全是没有实例的类型，`Logger` 也不例外。不存在 `logger` 对象，就没有"对象的成员"这回事。

**第二，共享 buffer 是重入炸弹。** 就算退一步，做一个 `static inline` 的共享行缓冲——主循环、回调、将来 RTOS 的线程、乃至 ISR，任何两个上下文同时打日志，就会互相撕行：A 装配到一半，B 把游标推走了，两行都是坏的。栈局部让每次调用有自己的行缓冲，天然并发安全，一分钱同步代码都不用写。

**第三，生命周期刚好卡准。** 我们拍板过的 B 契约下 `Sink::write` 是同步完成的——buffer 从第一条 `append` 到最后一次写出，始终活在 `emit` 的栈帧里，不逃逸、不悬垂。同步契约不只是错误语义，它还是内存安全的前提。

## 测量篇：三个教训，别被编译器骗

真去测之前，我们先撞了三个坑，每个都值得单独记录。

**教训一：空 sink 会让整条日志链蒸发。** 第一版测试用了一个什么都不做的 sink（`write` 是空函数），反汇编一看，包装函数只剩一条 `bx lr`——整条装配链被死代码消除干净了。这不是 bug，是优化器正确地发现"日志没有可观察效果"。教训：**测日志的开销，sink 必须有可观察副作用**（比如把行长写进一个 `volatile` 全局）。

**教训二：常量输入会让整行被编译期折叠。** 把 sink 修成可观察之后，用常量时钟（`now_ms` 返回 42）和常量参数再测——编译器把**整行装配在编译期算完了**：

```text
00000000 <_Z7call_itv>:
   0:	221a      	movs	r2, #26      @ 行长 26 是编译期常量!
   2:	4b01      	ldr	r3, [pc, #4]
   4:	701a      	strb	r2, [r3, #0] @ g_sink_probe = 26
   6:	4770      	bx	lr
```

`movs r2, #26`——一条指令给出整行的长度，栈帧零字节，运行周期约等于无。这是零开销故事的又一个实锤：**编译期已知的日志行，装配本身被完全折叠**。但作为测量方法这就是灾难：要测真实栈账，时钟和参数必须来自 `volatile`，逼优化器保留装配链。

**教训三：显式实例化撞编译器 ICE。** 想在 map 里强制实例化模板成员，arm-none-eabi-gcc 16.2 直接内部编译器错误（internal compiler error）。绕法：`__attribute__((noinline))` 的自由函数包装调用，包装体内联了成员的真实优化体，`objdump` 包装函数即可。

## 实测账：376 字节，两种方法互相验证

输入全部换成 `volatile` 之后，反汇编给出答案：

```text
   4:	e92d 47f0 	stmdb	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}   @ 8 个寄存器 = 32B
   a:	b0d6      	sub	sp, #344                               @ 344B 帧空间
```

再用 GCC 的 `-fstack-usage` 交叉验证（它为每个函数生成 `.su` 文件报告静态栈用量）：

```text
$ cat emit_stack.su
emit_stack.cpp:40:32:void call_it()	376	static
```

两条独立路径给出同一个数：**376 字节**（`LINE_BYTES=128` 时；344 里除了缓冲本体，还有全内联后的临时量与调用 ABI 暂存——`sub sp` 的数不等于模板参数，这是全内联的真实代价形态）。

这 376 字节的关键性质是**瞬态**：帧只在 `emit` 执行期间存在。它不随日志调用点的数量累加——一百个调用点共用的是"同一时刻最多一帧"的性质；它只随**嵌套深度**累加——而嵌套打日志本身就是要避免的反模式（尤其不要在 sink 内部再打日志，那是递归）。

## 预算链路：栈从哪来，够不够

最后一段账：这 376 字节从哪个预算里出？

**裸机场景：几乎无限。** 本仓库的链接脚本写的是 `_estack = ORIGIN(RAM) + LENGTH(RAM)`——栈顶钉在 RAM 顶端，栈空间就是"全部剩余 RAM"（BluePill 上约 20KB 减去 data/bss）。主循环里打日志，同一时刻一帧 376B，是零头。顺带提醒：很多 Cube 工程的启动文件默认 `Stack_Size EQU 0x400`（1KB 定长），那种配置下 376B 就要掂量了——两种栈模型的差异本身就是个常见盲区。

**RTOS 场景：真约束在这里。** 每个线程的栈是定长的，日志深度 × LINE_BYTES 要进每个可能打日志的线程的预算。那时的旋钮是把 `LINE_BYTES` 从 128 调到 64（内容区 61 字节，头部最长形态约 28 字节，还剩三十多字节载荷，够用），并把"每活跃日志调用 `LINE_BYTES` + 临时量的栈"写进文档。

**工具箱**（静态与动态结合是共识做法）：GCC 自带的 [`-fstack-usage` 与 `-fcallgraph-info=su`](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html) 可以算出全调用图的最坏栈深（[AdaCore 的原始论文](https://www.adacore.com/readings/gem-120-compile-time-stack-usage-analysis)和 [Embedded Artistry 的实践指南](https://embeddedartistry.com/blog/2020/08/17/three-gcc-flags-for-analyzing-memory-usage/)是好入口；注意间接调用、中断和 LTO 会打破静态调用图）；运行时侧有 [puncover](https://github.com/HBehrens/puncover) 这类二进制分析工具和 [Memfault 介绍的高水位涂色法](https://interrupt.memfault.com/blog/measuring-stack-usage-the-hard-way)；FreeRTOS 用户则有 `uxTaskGetStackHighWaterMark()`——但记住它只反映**实际走过**的路径，跑不到的分支不算数。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 测出来的栈/周期是零，完美得可疑 | sink 无可观察效果，整链被 DSE | sink 写 `volatile` 副作用 |
| 测出来一条 `movs #26`，没有栈帧 | 常量输入，整行编译期折叠 | 时钟/参数走 `volatile` |
| `internal compiler error` | arm-gcc 16.2 对类模板成员显式实例化的 bug | `noinline` 自由函数包装 |
| `sub sp` 的数比 LINE_BYTES 大不少 | 全内联后临时量与 ABI 暂存都在同一帧 | 正常形态，按实测数做预算 |
| Cube 模板工程栈只有 1KB | `Stack_Size EQU 0x400` 定长模型 | 调大或改全 RAM 栈；日志栈账进预算 |

## 小结

- 函数局部的行缓冲是正确性选择：全静态词汇里没有"对象级"，共享 buffer 是重入炸弹，B 契约让生命周期刚好卡准；
- 测开销前先防三骗：空 sink 的 DSE、常量输入的编译期折叠、显式实例化的 ICE；
- `LINE_BYTES=128` 的实测账是 376 字节瞬态帧（反汇编与 `-fstack-usage` 双验证），只随嵌套深度不随调用点数量累加；
- 裸机全 RAM 栈模型下无忧；RTOS 定长线程栈才是真约束，`LINE_BYTES` 是旋钮，静态 +fstack-usage 与动态高水位结合是预算的正路；
- 常量日志行被完全折叠成 `movs #26`——零开销的最后一块拼图。

代码在 [libestdx/logger/](https://github.com/Charliechen114514/libestdx/tree/main/include/libestdx/logger)，`emit` 的实现按本文结论落笔。

## 参考资源

- [Compile-time Stack Usage Analysis (AdaCore/GCC -fstack-usage 原始介绍)](https://www.adacore.com/readings/gem-120-compile-time-stack-usage-analysis)
- [Three GCC Flags for Analyzing Memory Usage — Embedded Artistry](https://embeddedartistry.com/blog/2020/08/17/three-gcc-flags-for-analyzing-memory-usage/)
- [GCC Developer Options（-fstack-usage / -fcallgraph-info 官方文档）](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html)
- [Measuring Stack Usage the Hard Way — Memfault Interrupt](https://interrupt.memfault.com/blog/measuring-stack-usage-the-hard-way)
- [puncover — bytewise stack/code analysis](https://github.com/HBehrens/puncover)
- [libestdx 仓库](https://github.com/Charliechen114514/libestdx)
