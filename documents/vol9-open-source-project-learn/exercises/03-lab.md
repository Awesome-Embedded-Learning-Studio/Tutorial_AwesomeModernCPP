---
title: "卷 9 Lab：mini-chrome 基础库四件套"
description: "卷 9 动手实验：把 OnceCallback、WeakPtr、flat_map、NoDestructor 四件套拧成一条贯穿线——六个步骤从工具链侦察一路做到 LSan/TSan 验收，每一步产出可编译可跑的小组件，最后附一道侵入式零分配无锁队列的 L5 挑战（改编自 Vyukov 经典 MPSC 算法）。"
chapter: 9
order: 3
tags:
  - host
  - intermediate
  - cpp-modern
  - 回调机制
  - weak_ptr
  - map
  - 内存管理
difficulty: intermediate
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 8
prerequisites:
  - "卷 9 chrome/ 四个主题全部章节"
related:
  - "卷 9 Homework"
  - "卷 9 Project：mini_chrome 任务投递系统"
---

# 卷 9 Lab：mini-chrome 基础库四件套

## 实验目标

本卷四篇文章各讲一个 Chromium `//base` 组件，这个 Lab 把它们拧成一条贯穿线：您从空目录起步，一步步攒出一个 `mini_base` 头文件库——OnceCallback、WeakPtr、flat_map、NoDestructor 各就各位，最后一步用 sanitizer 给整条线验货，附加挑战再上一段无锁。做完您手上就有一套"自己的 Chromium 风格 base"，同时对"读真实工程代码 → 复刻核心机制"这条本卷主线会有肌肉记忆。

所有实验在 `/tmp` 下独立目录做，每步一个源码文件。每步有验收标准；卡住先回题面每步标注的章节链接读教材，再不行看[实验参考](04-lab-solutions.md)。工具链：WSL Arch，g++ 16.1.1 / clang++ 22.1.8。

## 步骤 1：工具链与函数类型侦察 {#lab-1}

难度 **L1** · 涉及[OnceCallback 实战（一）：动机与接口设计](../chrome/01_once_callback/full/01-1-once-callback-motivation-and-api-design.md)、[OnceCallback 前置知识（一）：函数类型与模板偏特化](../chrome/01_once_callback/full/pre-01-once-callback-function-type-and-specialization.md)

**目标**：确认本卷要吃的 C++23 特性齐了，并把"函数类型"这个被忽略的概念摸一遍。

1. 写一个 `env_check.cpp`：`static_assert(__cpp_lib_move_only_function >= 202110L)`，加一个 deducing this 的最小类（`void test(this auto&& self) {}`），能编译过就算工具链过关。
2. 再写 `FuncTraits`（主模板不定义 + 偏特化拆 `R(Args...)`），打印 `int(int, int, int)` 的 arity 和 `void()` 的 arity。

**验收标准**：贴出编译命令与运行输出；一句话说清 `int(int,int)` 在模板参数位置为什么不是"函数声明残骸"。

[实验参考 →](04-lab-solutions.md#lab-1)

## 步骤 2：OnceCallback 核心骨架 {#lab-2}

难度 **L2** · 涉及[once_callback 设计指南（二）：逐步实现](../chrome/01_once_callback/hands_on/02-once-callback-implementation.md)

**目标**：把 OnceCallback 的最小骨架立起来，这是后面所有步骤的地基。

1. 单文件实现：主模板 + 偏特化、三态 `Status`、`not_the_same_t` 约束、拷贝删除移动保留、`run()` 用 deducing this 拦左值、`impl_run` 先取出再执行。
2. 顺带补一个 `bind_once<Signature>`（C++20 capture pack expansion + `std::invoke` + mutable lambda）。
3. 跑四种场景：非 void 返回、void 返回、move-only 捕获、`bind_once` 部分绑定。

**验收标准**：贴出四次运行输出；`-Wall -Wextra -Wpedantic` 零警告。

[实验参考 →](04-lab-solutions.md#lab-2)

## 步骤 3：取消令牌 {#lab-3}

难度 **L2** · 涉及[OnceCallback 实战（四）：取消令牌设计](../chrome/01_once_callback/full/01-4-once-callback-cancellation-token.md)

**目标**：给上一步的回调挂上轻量取消机制。

1. 实现 `CancelableToken`：`shared_ptr` 包 `struct Flag { atomic<bool> valid; }`，`invalidate` 用 release、`is_valid` 用 acquire。
2. 接进 OnceCallback：`set_token` + `is_cancelled()` 查两道关 + `impl_run` 执行前查令牌。取消命中时：void 回调静默 return、非 void 抛 `std::bad_function_call`。
3. 跑三个场景：令牌有效正常执行；void 回调取消后静默不执行；非 void 回调取消后抛异常（用 `try/catch` 捕获并打印异常类型）。

**验收标准**：贴出三次运行输出；说清"取消的 void 回调执行了吗、取消的非 void 回调返回了什么"。

[实验参考 →](04-lab-solutions.md#lab-3)

## 步骤 4：WeakPtr 三层 + 回调集成 {#lab-4}

难度 **L3** · 涉及[WeakPtr 实战（二）：核心骨架与控制块](../chrome/02_weak_ptr/full/02-2-weak-ptr-core-skeleton-and-control-block.md)、[WeakPtr 实战（三）：WeakPtrFactory 与「最后成员」惯用法](../chrome/02_weak_ptr/full/02-3-weak-ptr-factory-and-last-member.md)、[WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环](../chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md)

**目标**：把 01-4 手搓令牌的工业正解做出来，并接进回调系统。

1. 实现 Flag（侵入式引用计数 + 原子失效位）→ WeakReference → WeakPtr 三层，加 WeakPtrFactory（`get_weak_ptr`、`invalidate_weak_ptrs`、析构自动失效）。
2. 写一个 `Controller`：成员在前、`WeakPtrFactory` 最后声明；再写 `bind_weak_once` 把成员方法 + WeakPtr 绑成 void() 回调，执行前 `if (!receiver) return;`。
3. 跑一条完整时间线：对象活着跑回调 → 出作用域析构 → 再跑同一条回调 → 静默 no-op。

**验收标准**：贴出完整输出（应只有一次"got"）；说出 factory 放最后、析构逆序、自动失效三者的因果链。

[实验参考 →](04-lab-solutions.md#lab-4)

## 步骤 5：flat_map 骨架与诚实契约 {#lab-5}

难度 **L3** · 涉及[flat_map 实战（二）：flat_tree 核心骨架](../chrome/03_flat_map/full/03-2-flat-map-flattree-skeleton.md)、[flat_map 实战（四）：sorted_unique 构造优化](../chrome/03_flat_map/full/03-4-flat-map-sorted-unique-construction.md)

**目标**：把 flat_tree 的最小版立起来，摸一遍 sorted_unique 的诚实契约。

1. 实现 `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>` 最小版：`GetFirst`/`std::identity` 提取器、`sort_and_unique`、`lower_bound` 查找、`insert` 拒绝重复。
2. 加 `sorted_unique_t` tag dispatch：普通构造排序去重、sorted_unique 构造只做 debug 断言。
3. 验证：乱序数据普通构造后升序去重；有序数据 sorted_unique 接管；乱序数据塞 sorted_unique 在 debug 下 abort（贴退出码），`-DNDEBUG` 下则"活"下来。

**验收标准**：贴出三组输出；一句话总结"诚实契约"两头各是谁的义务。

[实验参考 →](04-lab-solutions.md#lab-5)

## 步骤 6：NoDestructor + 三把 sanitizer 验货 {#lab-6}

难度 **L4** · 涉及[NoDestructor 实战（二）：核心实现](../chrome/04_no_destructor/full/04-2-no-destructor-core-impl.md)、[NoDestructor 实战（四）：LSan 泄漏权衡与 reachability hack](../chrome/04_no_destructor/full/04-4-no-destructor-lsan-and-leak.md)

**目标**：把静态生命周期那块补上，然后让 sanitizer 给前面五步的资产集体验一次货。

1. 实现 `NoDestructor` 最小版（placement new + `= default` 析构 + 两条 static_assert），用一个构造/析构都打印的 `Noisy` 验证"构造一次、析构永不跑"。
2. 把一张 `flat_map` 配置表装进 `NoDestructor`（函数局部静态），从 `main` 里查两回，验证第二次不重新构造（打印构造计数）。
3. 验货：①把步骤 4 的完整时间线用 `-fsanitize=address,undefined` 跑一遍，零报告；②LSan 实验——写一个"真泄漏对照组"（`new` 之后指针置空，比如 `int* p = new int[100]; p[0]=1; p=nullptr;`），和 NoDestructor 版配置表放进同一个程序，用 `-fsanitize=address -g -O0` 加 `ASAN_OPTIONS=detect_leaks=1` 跑一遍，**如实贴出** LSan 报了谁、没报谁。教材 04-4 **旧版**曾说 LSan 会把 NoDestructor 误报成泄漏（那是 crbug 年代老版本 LSan 的行为，现行教材已按保守字节扫描的口径修订）；您在新工具链上大概率看到它只报真泄漏、放过 NoDestructor——结合 LSan 的保守字节扫描（它扫的是内存里的指针值，不是 C++ 类型）解释为什么 `char storage_` 里的指针还是被看见了，并体会"工具行为随版本变化"对读旧文档意味着什么。两个实测踩坑请一并记录：本工具链 LSan 的 `detect_leaks` 默认就是 1——不写 `ASAN_OPTIONS` 也照样报真泄漏（命令里写上无害且自解释，值得写）；真正必须的是 `-O0`——`-O1`/`-O2` 下编译器可能把您的对照组分配直接优化掉（那实验就白做了）；③把 `GetSharedCounter`（magic statics 首调计数）用 `-fsanitize=thread` 多线程跑一遍，零报告。

**验收标准**：贴出三种 sanitizer 的输出；说清"真泄漏被抓、NoDestructor 没被误报"（或您实测到的任何其他结果）背后的可达性分析机制，以及为什么按旧版机制描述 `storage_` 的 `char` 类型本会让可达链断开、现代 LSan 的保守扫描又为什么让它断不了。

[实验参考 →](04-lab-solutions.md#lab-6)

## 附加挑战（L5）：侵入式零分配 MPSC 队列 {#lab-l5}

**目标**：把步骤 3 的取消令牌和步骤 2 的回调塞进一个**不许用锁**、且**每次投递零堆分配**的队列——改编自 Dmitry Vyukov 的侵入式 MPSC 算法（1024cores.net「Intrusive MPSC node-based queue」），按本卷机制强化：节点内嵌进任务对象、任务必须是 move-only 回调、执行前查取消令牌（早期阶段 L5＝「用该阶段知识可解的最难问题」，档位口径见[练习总览](index.md)）。

1. 定义任务节点：节点结构体自带 `std::atomic<Node*> next`，任务对象持有节点并允许被"摘取"（队列只移动节点，不动任务本身，所以 per-post 不 new）。
2. 实现 `push` / `pop_all`：只用 `acquire` / `release` / `relaxed` 三种内存序，每个原子都说得出理由。
3. 压测：4 生产线程共投 10 万任务（每个对 `atomic<int>` 自增），1 消费线程 drain，中途 invalidate 掉一批；验收：计数 == 未取消任务数，`-fsanitize=thread` 零报告。
4. 基准：同一负载下，与"互斥锁 + deque"的朴素队列比投递+执行总耗时，贴出两组数据与您的结论。

**验收标准**：贴出计数对账、TSan 零报告、基准对比；写一段不超过 100 字的注释说明 push 路径上哪个内存序在防什么。

[实验参考 →](04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下全部源码（`mini_base` 各头文件 + 每步 `stepN.cpp`）、每步终端记录（`stepN.log`）、以及 200 字以内的小结——用你自己的话说清"卷 9 的四件套在您手里怎么从教材代码变成自己的 mini_base"，哪一步您觉得最难、难在哪。
