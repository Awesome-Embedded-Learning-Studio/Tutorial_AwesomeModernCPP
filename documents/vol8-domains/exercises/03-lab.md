---
title: "卷八 Lab：从串口帧到网络控制台——主机上的嵌入式数据链路"
description: "卷八动手实验：六个步骤把寄存器位运算、UART 8N1 帧编解码、环形缓冲区、7 状态消抖状态机、命令处理器与 epoll 网络控制台串成一条主机可验证的数据链路，最后附一道 TSan 验收的无锁 SPSC 环形缓冲区 L5 挑战。全程不需要任何硬件，WSL + g++ 即可完成。"
chapter: 8
order: 3
tags:
  - host
  - intermediate
  - cpp-modern
  - 嵌入式
  - 网络编程
  - 状态机
  - 循环缓冲区
  - 寄存器
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 8
prerequisites: []
related: []
---

# 卷八 Lab：从串口帧到网络控制台——主机上的嵌入式数据链路

## 实验目标

本卷的嵌入式与网络两个子领域讲的是同一条故事的两半：芯片内部，一个字节从寄存器出发，编成 UART 帧、穿过中断与环形缓冲区、被命令处理器消费；网络侧，一个字节从 socket 出发，穿过 epoll 事件循环到达你的处理逻辑。这个 Lab 把两半接成一条**主机上可完整验证的数据链路**——寄存器 → 8N1 帧 → 环形缓冲区 → 消抖状态机（旁路）→ 命令处理器 → TCP 回环控制台。

不需要任何硬件：寄存器用普通变量、时钟用虚拟毫秒、串口用函数调用、网络用 `127.0.0.1` 回环。所有实验在 `/tmp` 下独立目录做。每步有验收标准；卡住先回题面每步标注的章节链接读教材，再不行看[实验参考](./04-lab-solutions.md)。建议先做 Homework 8.1-C——本 Lab 步骤 3 的环形缓冲区直接用它验证过的正确实现方案。

## 步骤 1：寄存器位运算工具箱 {#lab-1}

难度 **L1** · 涉及[类型安全的寄存器访问](../embedded/02-type-safe-register-access.md)、[第 11 篇：HAL_GPIO_WritePin 与 TogglePin](../embedded/01-led/06-hal-gpio-output.md)

**目标**：把「读-改-写」的位运算六件套练成肌肉记忆——置位、清位、翻转、字段提取、字段写入、字段读回。

1. 定义一个模拟的 32 位外设寄存器快照 `uint32_t reg = 0x00004500u`。
2. 按顺序完成并逐行打印十六进制：提取 offset 8 处的 4 位字段；置位 bit13；清位 bit10；翻转 bit11 两次（应回到原值）；把 offset 14 处的 2 位字段写入 `0b10`；再读回该字段。
3. 全程掩码用 `constexpr` 常量表达（`(1u << 13)`、`0xF`、`0x3u << 14` 这类），不要写魔法数字。

**验收标准**：贴出全部输出；说清「先移位再掩码」提取字段、以及「先清位再或」写入字段，各自先后的理由。

[实验参考 →](./04-lab-solutions.md#lab-1)

## 步骤 2：UART 8N1 帧编解码 {#lab-2}

难度 **L2** · 涉及[第 32 篇：UART 协议详解](../embedded/03-uart/02-uart-protocol-basics.md)

**目标**：把 8N1 帧从「协议描述」变成能跑的函数——编码器把字节摊平成 10 个 bit，解码器在 16x 过采样流里找 bit 中心、做多数表决、校验起止位。

1. 写 `encode_8n1(byte)`：起始位 0、8 个数据位 LSB 在前、停止位 1，返回 `std::array<uint8_t, 10>`。编码 `0xA5` 并打印帧。
2. 模拟接收端：把帧展开成 162 个采样点（起始边沿**迟到 2 个 tick** 模拟时钟偏差），解码器在每个 bit 中心取 3 个采样点多数表决，校验起始位为 0、停止位为 1，不合法报 framing error。
3. 顺手算吞吐：115200 baud 下 1 bit、1 个 8N1 帧各占多少微秒？有效字节速率是多少？打印出来对照第 32 篇的数字。
4. 把停止位中心附近的采样点改 0，验证解码器报错。

**验收标准**：贴出帧、解码结果与吞吐三行数字；说清「16x 过采样为什么能容忍 2 个 tick 的时钟偏差」和「为什么多数表决只改 1 个采样点打不坏解码」。

[实验参考 →](./04-lab-solutions.md#lab-2)

## 步骤 3：环形缓冲区 {#lab-3}

难度 **L3** · 涉及[第 37 篇：无锁环形缓冲区](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)、[循环缓冲区](../embedded/03-circular-buffer.md)

**目标**：实现一个容量恰为 N-1 的 SPSC 环形缓冲区，并用两批数据实测环绕路径。

1. 模板类 `CircularBuffer<N>`：`static_assert` N 为 2 的幂；head/tail 用**单调递增计数器**，只在数组下标处 `& (N-1)`（提示：如果你做过 Homework 8.1-C，直接复用修复版方案）；满时拒绝并计数 `dropped_`。
2. 批次 1：push 20 个字节（值 0..19）不 pop——容量 15 的缓冲区应接受 15、丢 5。
3. 批次 2：pop 8 个，再 push 8 个（值 100..107，走环绕路径），最后全部 drain，打印字节序与 checksum（drain 值的总和）。

**验收标准**：贴出三阶段输出；说明「满时丢字节 + 计数」这个策略为什么适合 ISR 生产者的场景（对照第 37 篇「N=128 够不够」的算账）。

[实验参考 →](./04-lab-solutions.md#lab-3)

## 步骤 4：7 状态消抖状态机 {#lab-4}

难度 **L3** · 涉及[第 25 篇：7 状态消抖状态机](../embedded/02-button/07-debounce-state-machine.md)

**目标**：把第 25 篇的 7 状态状态机在主机上完整实现一遍（虚拟时钟），并用三个场景验证核心路径、启动锁定路径与假信号路径。

1. 实现 `Button::poll(sample, now_ms)`：状态 `BootSync → Idle → DebouncingPress → Pressed → DebouncingRelease → Idle` 的核心路径 + `BootPressed`/`BootReleaseDebouncing` 启动路径，消抖窗口 20ms，事件回调 `std::variant<Pressed, Released>`。
2. 场景 A（正常按下+释放，各带几次反弹）：断言恰好 1 次 Pressed、1 次 Released，事件发生在稳定确认时刻。
3. 场景 B（上电时按钮已按住，随后释放）：断言**零事件**——boot-lock 静默解锁。
4. 场景 C（按下但消抖窗口内弹回）：断言**零事件**。

**验收标准**：贴出三个场景的输出；说清 DebouncingPress 里三个判断的顺序为什么不能换。

[实验参考 →](./04-lab-solutions.md#lab-4)

## 步骤 5：命令处理器 {#lab-5}

难度 **L4** · 涉及[第 42 篇：命令处理器与完整代码走读](../embedded/03-uart/12-command-processor-and-main-walkthrough.md)

**目标**：实现字节级行拼装 + `std::string_view` 零拷贝分发的命令处理器，驱动一个模拟 LED。

1. `CommandShell::feed(char)` 逐字节喂入（模拟主循环从环形缓冲区 pop 一个处理一个）：容忍 `\r\n`、空行忽略、超长行丢弃尾部、固定 `std::array<char, 32>` 行缓冲。
2. 命令：`LED ON`/`LED OFF`（驱动模拟 LED 状态）、`STATUS`（打印 led 状态）、`HELP`、未知命令报错。**先 NUL 终止再分发**（`line_[len_] = '\0'`），否则 `%s` 会读到上一行的残留——这是本 Lab 真实踩过的坑。
3. 用一个会话脚本验证：`HELP / LED ON / STATUS / LED OFF / STATUS / TURBO / 空行`。

**验收标准**：贴出会话完整输出；说清 `string_view` 在这里零拷贝的含义（对比构造 `std::string` 的路径）。

[实验参考 →](./04-lab-solutions.md#lab-5)

## 步骤 6：网络控制台 {#lab-6}

难度 **L4** · 涉及[00 · 传统 socket 编程](../networking/00-traditional-socket-basics.md)、[01 · 现代 socket 封装](../networking/01-modern-socket-wrapping.md)、[02 · epoll：Linux I/O 多路复用](../networking/02-epoll-io-multiplexing.md)、[03 · Reactor 模式](../networking/03-reactor-pattern.md)

**目标**：把命令处理器挂到 TCP 回环上——单线程 epoll 事件循环同时伺候监听 fd 与连接 fd，RAII 管 fd、`SIGTERM` 优雅退出。

1. 服务器（C++20）：`UniqueFd`（move-only RAII）、`signal(SIGPIPE, SIG_IGN)`、`SO_REUSEADDR`、监听 fd 与连接 fd 全 `O_NONBLOCK`、`epoll_create1` + `EPOLLIN`（LT 模式）、`epoll_wait` 带 200ms 超时以便检查停止标志。连接事件：读到数据原样回写（写要处理短写与 EINTR），读到 0 注销并关闭；对端断开计数。
2. 客户端：连 `127.0.0.1`，先发一条 `HELP\r\n` 验证回显一致，再一次性发 **64KB** 大 burst，读回并断言「逐字节一致 + 恰好 64KB」——大 burst 是网络代码的对抗性验收。
3. 运行顺序：后台起服务器 → 跑客户端 → `kill -TERM` 服务器 → 观察优雅退出日志。

**验收标准**：贴出服务器启动、客户端两条回显、断开与优雅退出的完整日志；64KB 一字节不能少。

[实验参考 →](./04-lab-solutions.md#lab-6)

## 附加挑战（L5）：无锁 SPSC 环形缓冲区，TSan 验收 {#lab-l5}

**目标**：把步骤 3 的单线程环形缓冲区升级成跨线程的无锁 SPSC（受 Dmitry Vyukov 的 intrusive MPSC 队列（1024cores.net）启发、按竞赛挑战级强化改编；L5＝「用本卷知识可解的最难问题」，档位口径见[练习总览](../exercises/index.md)）。

1. head/tail 换成 `alignas(64)` 的 `std::atomic<size_t>`（独占 cache line）；`push`：relaxed 读 head、acquire 读 tail、release 写 head；`pop` 对称。容量 N-1。
2. 生产者线程 push 1..5000000，消费者 pop 并逐项校验**严格递增**，最后校验 checksum $\frac{n(n+1)}{2}$。
3. 普通 `-O2` 与 `-fsanitize=thread` 构建都要跑通，**TSan 零报告是通过条件**。

**验收标准**：贴出两份构建的输出；说清 push 里「acquire 读 tail / release 写 head」各自保护了什么，全换成 relaxed 会怎样。

[实验参考 →](./04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下全部源码、每步终端记录（`stepN.log`）、以及 200 字以内的小结——用你自己的话说清「从寄存器到网络控制台」这条链路上，哪个环节最容易「看着能跑、其实丢数据」，你在这套 Lab 里怎么抓住它的。
