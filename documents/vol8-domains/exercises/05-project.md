---
title: "卷八 Project：MiniDevice——主机版智能设备模拟器"
description: "卷八综合项目：在主机上造一个迷你嵌入式设备——LED 有效电平、按钮消抖、环形缓冲区、命令 shell 为核（L2），8N1 帧层与 Tensor 传感器为进阶（L3），epoll 网络控制台、并发对抗验收、ASan/UBSan 门与弱引用观察者为质量层（L4），双线程无锁流水线 TSan 验收为终极（L5）。全程不需要硬件。"
chapter: 8
order: 5
tags:
  - host
  - advanced
  - cpp-modern
  - 嵌入式
  - 网络编程
  - 状态机
  - 无锁
  - 并发
  - 模板
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 4
prerequisites: []
related: []
---

# 卷八 Project：MiniDevice——主机版智能设备模拟器

## 项目定位

把本卷七个子领域的家当装进一台**主机上能跑的设备模拟器** `minidevice`：一颗 LED（有效电平抽象）、一个按钮（消抖）、一个环形缓冲区、一个命令 shell，进阶后加 8N1 帧层与 Tensor 传感器，再挂到 epoll 网络控制台上，最后用双线程无锁流水线把它和网络层焊在一起。任务分四层，一层一层往上盖；卡住了看[参考实现](./06-project-solutions.md)，它按层组织，可以只读你卡住的那层。

不需要任何硬件。这台「设备」的行为在主机上全部可验证：LED 是 bool、按钮是虚拟时钟驱动的采样序列、串口是函数调用、网络是 `127.0.0.1` 回环。真实 STM32 上的 HAL 调用在本项目里一律不出现——把「外设」与「业务」分层这件事本身就是本卷要练的核心能力。

## 任务分层

### 核心任务（L2）：能跑起来的设备 {#pj-core}

**L1 热身**：先把 `device.hpp` 的骨架搭起来——`Led`（含 `ActiveLevel` 枚举）、`Button`、`run_command` 的**声明**加一个能编译的 `main` 桩，用 `g++ -std=c++20 -Wall -Wextra -fsyntax-only` 做到零警告。不实现逻辑，先立契约。

实现一台命令驱动的设备：`Led` 用 `ActiveLevel::Low/High` 表达硬件电路的有效电平，对外只暴露 `on()/off()/toggle()` 与逻辑状态，`pin_level_for(logical)` 换算成引脚真实电平；`Button` 用虚拟时钟做非阻塞消抖（`poll(sample, now_ms)`，消抖窗口 20ms，稳定跳变返回 true）；`Device` 内置 `CircularBuffer<64>`（2 的幂 + `static_assert`，head/tail 单调计数器）与字节级行拼装器。命令集：`HELP`、`LED ON`、`LED OFF`、`LED TOGGLE`、`BTN`（喂一段「按下-抖动-释放」采样序列）、`STATUS`（打印 led/btn 状态与引脚电平）、`QUIT`。`main` 从 stdin 逐行喂给 `Device::feed`。

**验收标准**：骨架编译零警告；贴出一次完整会话输出（HELP → LED ON → STATUS → BTN → STATUS → LED TOGGLE → STATUS → LED OFF → QUIT）。命令解析用 `std::string_view`，行缓冲固定大小、不 new。

[参考实现 →](./06-project-solutions.md#pj-core)

### 进阶任务（L3）：帧层与传感器 {#pj-adv}

加两条「外设」：①**8N1 帧层**——`encode_8n1(byte)` 与 `decode_8n1(bits)`（校验起止位，报 framing error），命令 `FRAME <hex>` 打印一字节的帧、`DEFRAME <10bits>` 解码。②**Tensor 传感器模块**——`Tensor<1, 3>`（std::array 存储、行主序），命令 `SENSE t h l` 存三个读数、`SHOW` 打印扁平存储与最大元素。

**验收标准**：贴出会话输出（`FRAME 41`、`FRAME A5`、`DEFRAME 0100000101`、一条非法帧、`SENSE 25.5 60 420`、`SHOW`）；`SHOW` 的最大值要直接遍历 `storage()` 行主序数组得到。

[参考实现 →](./06-project-solutions.md#pj-adv)

### 再进阶任务（L4）：网络控制台、对抗验收与弱引用观察者 {#pj-l4}

三件事。①**网络控制台**：把 `Device` 挂到 TCP 回环——单线程 epoll（LT）+ `UniqueFd`（move-only RAII）+ `SIGPIPE` 忽略 + `SIGTERM` 优雅退出，每个连接一个独立 Device 状态。②**对抗验收**：8 个客户端线程**同时**连、各发 8KB burst，全部逐字节回显一致——一字节都不能少。③**弱引用观察者**：用 Chrome-like `WeakPtr`（引用计数控制块 + `WeakPtrFactory`）给设备装一个「看门狗」：设备销毁后定时回调触发，`is_valid()` 安全返回 false、静默无动作——ASan/UBSan 构建下**零报告**。整个核心会话在 `-fsanitize=address,undefined` 下再跑一遍也零报告。

**验收标准**：贴出服务器日志、8 客户端全部 OK、看门狗三行输出（存活时动作 / 销毁后静默 ×2）、sanitizer 构建零报告。

[参考实现 →](./06-project-solutions.md#pj-l4)

### 终极挑战（L5）：双线程无锁流水线，TSan 验收 {#pj-l5}

把「设备线程」和「网络线程」用一条无锁 SPSC 环形缓冲区接起来（受 Dmitry Vyukov 的 intrusive MPSC 队列（1024cores.net）启发、按竞赛挑战级强化改编；L5＝「用本卷知识可解的最难问题」，档位口径见[练习总览](../exercises/index.md)）：设备线程作为生产者 push 100 万个帧序号（满时自旋），网络线程作为消费者 pop 并逐项校验**严格递增**。head/tail 用 `alignas(64)` 的 `std::atomic<size_t>`，push 用 acquire 读对方 + release 写自己，容量 N-1。普通 `-O2` 与 `-fsanitize=thread` 构建都要跑通，**TSan 零报告是通过条件**。

**验收标准**：贴出两份构建的输出（100 万帧全部投递、顺序严格递增）；一句话说清 acquire/release 在这条流水线里各自保护了什么。

[参考实现 →](./06-project-solutions.md#pj-l5)

## 提交物清单

项目目录（`device.hpp`、各层 `main`/`net_server`/`net_client`/`watchdog`/`pipeline` 源文件 + 会话脚本）+ 各层终端记录 + 200 字以内小结：说说这台「主机上的设备」里，哪一层让你对「硬件与业务分层、主机先行验证」体会最深。
