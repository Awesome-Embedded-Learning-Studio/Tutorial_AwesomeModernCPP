---
title: "卷 9 Project：mini_chrome 任务投递系统"
description: "卷 9 综合项目：做一个 mini_chrome——把 OnceCallback、WeakPtr、flat_map、NoDestructor 四件套组装成一个带命令分发与任务投递的小型服务骨架，任务分四层：核心库（L2 含 L1 热身）、自动取消与分发（L3）、质量门（L4）、无锁消息环（L5，改编自 Vyukov 经典 MPSC 算法）。"
chapter: 9
order: 5
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
reading_time_minutes: 5
prerequisites:
  - "卷 9 chrome/ 四个主题全部章节"
related:
  - "卷 9 Homework"
  - "卷 9 Lab：mini-chrome 基础库四件套"
---

# 卷 9 Project：mini_chrome 任务投递系统

## 项目定位

把卷 9 的家当全部用进一个真实的小系统：`mini_chrome`——一个模仿 Chromium `//base` 分层思路的命令行服务骨架。OnceCallback 管"任务只能跑一次"，WeakPtr 管"对象死了任务自动作废"，flat_map 管"命令分发表的缓存友好查找"，NoDestructor 管"全局配置的静态生命周期"。四层任务一层一层往上盖；卡住了看[参考实现](06-project-solutions.md)，它按层组织，可以只读您卡住的那层。

所有代码在 `/tmp/mini_chrome` 下组织成 `include/` + `src/` 的结构，单头文件 + 单驱动，`-std=c++20` 起（OnceCallback 部分用 C++23）。参考实现每个头文件都能脱离项目独立编译，您在 WSL（g++ 16.1.1 / clang++ 22.1.8）下复现即可。

## 任务分层

### 核心任务（L2）：能跑起来的回调库 {#pj-core}

**L1 热身**：先把 `mini_once_callback.hpp` 的骨架搭起来——只写声明不写实现，`OnceCallback<R(Args...)>` 偏特化、三态 `Status`、`CancelableToken` 声明，加一个空 `main.cpp`，只求 `g++ -std=c++23 -Wall -Wextra -c` 零警告通过。

实现 `mini_once_callback.hpp`：主模板 + 偏特化；`not_the_same_t` 约束的模板构造；拷贝删除、移动保留且移动后源 `is_null()`；`run()` 用 deducing this 拦左值（错误信息要给"请写 `std::move(cb).run(...)`"的人话）；`impl_run` 先取出 `func_`、置 `kConsumed`、再执行；`bind_once<Signature>` 用 capture pack expansion + `std::invoke`；`CancelableToken`（`shared_ptr` + `atomic<bool>`，release/acquire）接进 `set_token`/`is_cancelled`/`impl_run`，取消时 void 静默、非 void 抛 `std::bad_function_call`。

**验收标准**：跑通五个场景并贴输出：非 void 返回、void 返回、move-only 捕获、`bind_once` 部分绑定（含成员函数绑定）、取消的非 void 回调被 `try/catch` 捕获到 `std::bad_function_call`。

[参考实现 →](06-project-solutions.md#pj-core)

### 进阶任务（L3）：自动取消 + 命令分发 {#pj-adv}

两个组件。

第一，`mini_weak_ptr.hpp`：Flag（侵入式引用计数 + 原子失效位）→ WeakReference → WeakPtr 三层 + WeakPtrFactory；写 `Controller` 类（成员在前、factory 最后声明）和 `bind_weak_once`（成员方法 + WeakPtr receiver → `void()` 回调，执行前 `if (!receiver) return;`）。验证完整时间线：对象活着跑回调 → 析构 → 再跑同一条回调静默 no-op。

第二，`mini_flat_map.hpp`：`flat_tree` 最小版（`GetFirst`/`std::identity` 提取器、`sort_and_unique`、`lower_bound` 查找、`insert` 拒重复、`sorted_unique_t` 诚实契约）。用它搭一张**命令分发表**：把 `"inc"`、`"dec"`、`"report"` 三条命令映射到 `bind_weak_once` 包好的回调上，从标准输入循环读命令，按 `flat_map` 查表执行；命令目标是同一个 `Controller` 实例。

**验收标准**：贴出一段交互会话：`inc`、`inc`、`report`、`dec`、`report`、`quit` 的输出；说清这条链上"回调只能跑一次"和"对象死后自动作废"各由哪个组件保证。

[参考实现 →](06-project-solutions.md#pj-adv)

### 再进阶任务（L4）：把门装上 {#pj-gates}

四件事，仿照教材三个主题的测试篇（01-6 / 02-6 / 03-6），按**不变量**而不是功能列表组织测试。

1. 写一个零依赖的迷你测试框架（`mini_test.hpp`：`EXPECT_EQ` / `EXPECT_TRUE` / 失败计数），给四件套各写一组不变量用例：OnceCallback 的"移动不消费、run 才消费"、WeakPtr 的"invalidate 一次失效所有"与"was_invalidated 区分作废与主动 reset"、flat_map 的"构造后严格升序且无重复"与"insert 拒重复"、NoDestructor 的"构造一次、析构永不跑"。
2. 全部源码 `-Wall -Wextra -Wpedantic -Werror` 零警告编译。
3. 整个测试驱动 + 交互程序用 `-fsanitize=address,undefined` 构建跑一遍，**零报告**。NoDestructor 的 LSan 行为按 Lab 步骤 6 的实验结论如实记录——本卷工具链上 LSan 不误报它（保守字节扫描看穿了 `storage_`），如果您的环境真出现误报，用 suppression 文件压掉并贴出内容。
4. 用 `-fsanitize=thread` 构建跑"16 线程首调 magic statics 计数"与"多线程投递 + 消费"两个并发用例，零报告。

**验收标准**：贴出测试框架的通过计数（`passed=N failed=0`）、`-Werror` 编译命令、三种 sanitizer 的零报告确认、suppression 文件全文。

[参考实现 →](06-project-solutions.md#pj-gates)

### 终极挑战（L5）：无锁消息环 + 编译期分派 {#pj-l5}

两件挑战，全部用本卷知识完成（早期阶段 L5＝「用该阶段知识可解的最难问题」，档位口径见[练习总览](index.md)）。

1. **编译期 weak 分派**：复刻 `bind_internal.h` 的 `IsWeakReceiver` + `kIsWeakMethod` + `WeakCallReturnsVoid`（出处见 02-5 全文引用）：`bind_weak` 只有在"成员方法 + receiver 是 WeakPtr"时才选 weak 分派，weak 调用**强制 void 返回**（`static_assert`），分派体内"先 Unwrap 再判活"。验收：返回 `int` 的成员方法绑成 weak 回调编译失败（贴报错）；合法场景跑通。
2. **无锁消息环**：改编自 Dmitry Vyukov 的侵入式 MPSC 算法（1024cores.net「Intrusive MPSC node-based queue」）。任务节点内嵌在任务对象里（per-post 零堆分配），队列只许用 `acquire`/`release`/`relaxed` 原子；4 生产线程共投 10 万任务、1 消费线程 drain，**预先**给其中 10% 任务挂上已失效的取消令牌、10% 挂上已死对象的 WeakPtr（确定性做法，计数可复现）；每个任务消费时先过 WeakPtr 判活、再过取消令牌，最终计数 == 未取消任务数（80000，一个不多一个不少）；`-fsanitize=thread` 与 `-fsanitize=address,undefined` 双零报告。
3. **基准**：同负载下与"互斥锁 + deque"朴素队列对比总耗时（`-O2`），贴出两组数据，写一句您的结论——无锁换来什么、又付了什么。

**验收标准**：贴出编译失败截图（文本）、计数对账、两种 sanitizer 零报告、基准数据；对每个 `std::memory_order` 写清它在这里防的是什么（这是这层最值钱的产出）。

[参考实现 →](06-project-solutions.md#pj-l5)

## 提交物清单

项目目录（`include/`、`src/`、每层驱动与日志）+ 各层终端记录 + 200 字以内小结：说说这个项目里哪一处让您对"卷 9 的知识点是一体的"体会最深。
