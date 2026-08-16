---
title: "卷四 Project：schedule-lite——概念约束的编译期/运行期任务调度器"
description: "卷四综合项目：做一个命令驱动的任务调度器——三路比较与指定初始化器建任务、concept 约束的调度策略、命令模式撤销与 RAII 观察者、sanitizer 质量门，最后用模板元编程实现编译期优先级序并与运行期调度对账。任务分四层，难度 L1~L5。"
chapter: 4
order: 5
tags:
  - host
  - advanced
  - cpp-modern
  - 模板元编程
  - concepts
  - 策略模式
  - 命令模式
  - 观察者模式
  - 回调机制
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 4
prerequisites: []
related: []
---

# 卷四 Project：schedule-lite——概念约束的编译期/运行期任务调度器

## 项目定位

把本卷的家当全部用进一个真实的小程序：`schedule-lite`——一个命令驱动的任务调度器。三路比较与指定初始化器建任务、定长队列存任务、concept 约束的调度策略、命令模式做撤销、RAII 订阅令牌的观察者发完成事件、sanitizer 和警告门守着质量，最后用模板元编程在**编译期**排出优先级序、再和运行期调度结果对账。任务分四层，一层一层往上盖；卡住了看[参考实现](./06-project-solutions.md)，它按层组织，可以只读你卡住的那层。

所有代码用 `g++ -std=c++20 -Wall -Wextra -Werror` 起步，质量门阶段再加 `-fsanitize=address,undefined`。

## 任务分层

### 核心任务（L2）：能跑起来的调度器 {#pj-core}

**L1 热身**：定义 `struct Task { int priority; int deadline; int id; std::string name; };`——注意**成员声明顺序**，补 `auto operator<=>(const Task&) const = default;` 和 `bool operator==(...) const = default;`。用**指定初始化器**建两个任务并打印 `t1 < t2`、`t1 == t1`（想一想：default `<=>` 按什么顺序比？指定初始化器的 designator 顺序有什么硬规矩——本卷作业 4.16-A 纠正过教材那处）。

核心：实现 `TaskQueue`（`std::array<Task, 16>` + `size_`，`add` 满了抛异常、`at(i)`、裸指针 `begin()/end()`、`pop_last()` 供撤销用）和命令循环：`add <name> <priority> <deadline> <id>`、`list`（对齐表格）、`run`（按优先级调度：排序后按序打印 `run [name] id=.. priority=.. deadline=..`）、`quit`。

**验收标准**：`-Wall -Wextra -Werror` 零警告编译通过；add 三个任务后 `list` 输出对齐、`run` 按 priority 升序执行。贴出编译命令和一次会话的完整输出。

[参考实现 →](./06-project-solutions.md#pj-core)

### 进阶任务（L3）：concept 约束的调度策略 {#pj-advanced}

把「按什么顺序调度」抽成策略：`PriorityPolicy`（`before(a,b) = a < b`）和 `DeadlinePolicy`（`before(a,b) = a.deadline < b.deadline`），写一个 `SchedulePolicy` concept（要求 `Policy::before(a, b)` 可调用且返回可转 bool），`template <SchedulePolicy Policy> std::vector<Task> schedule(const TaskQueue&)` 用 `std::stable_sort` 排序返回。加 `rundl` 命令（DeadlinePolicy 调度）。

**验收标准**：贴出 `run` 与 `rundl` 对同一批任务的两种顺序；说清 `static_assert(!SchedulePolicy<int>)` 为什么成立；一句话说明 concept 在这里替调用者挡了什么错误。

[参考实现 →](./06-project-solutions.md#pj-advanced)

### 再进阶任务（L4）：命令撤销 + 观察者 + 质量门 {#pj-expert}

三件事。①命令模式：`AddTaskCommand{TaskQueue&, Task}` 的 `execute` 调 `add`、`undo` 调 `pop_last`；`UndoStack::execute` 先执行再压栈，`undo` 弹栈顶反操作。加 `undo` 命令。②观察者：`EventSource` 用「id + `std::function` 回调」+ `std::mutex`，订阅返回一个 **RAII 订阅令牌**（析构自动退订），`emit` 走 **snapshot**（锁内拷贝回调、锁外遍历）。`run` 每执行一个任务就 `emit(TaskDoneEvent{name})`，`main` 里订阅一个打印 `[done] name` 的 logger。③质量门：`-Wall -Wextra -Werror` 零警告；`-fsanitize=address,undefined` 构建跑一遍完整会话**零报告**。

**验收标准**：贴出含 `undo` 的会话输出（撤销最近一次 add、`list` 立刻反映）、`[done]` 通知、以及 sanitizer 会话的尾部输出（零报告）。

[参考实现 →](./06-project-solutions.md#pj-expert)

### 终极挑战（L5）：编译期优先级序与运行期对账 {#pj-l5}

三件挑战，全部用本卷知识完成（typelist 技术源自 Andrei Alexandrescu《Modern C++ Design》第 3 章与 Boost.MPL 思路；L5 档位口径见[练习总览](../exercises/index.md)）。①**编译期优先级序**：定义 `CCons<V, T>`（NTTP 值 + 类型尾巴），用模板元编程实现 `MinVal`（列表最小值）与 `SortAsc`（**按值升序**的选择排序——注意是排序「值」，不是作业 4.C-3 的按 `sizeof` 排序）。②用 `static_assert` 把 `Priorities = CCons<2, CCons<9, CCons<1, CCons<5, CCons<9, Nil>>>>>` 的排序结果钉死：头五个依次 1、2、5、9、9。③**对账**：运行期用同样的优先级序列建五个任务、走 `schedule<PriorityPolicy>`，把运行期顺序和编译期顺序并排打印——两者必须一致。说清「编译期算好的序」与「运行期排出的序」为什么能对上（比较规则是否同一套）。

**验收标准**：贴出「compile-time order: 1 2 5 9 9」与「runtime order: 1 2 5 9 9」并排的输出；说清 `SortAsc` 是选择排序、它的编译期复杂度是多少。

[参考实现 →](./06-project-solutions.md#pj-l5)

## 提交物清单

项目目录（`pj.cpp` + `session.txt` 会话输入）+ 各层终端记录 + 200 字以内小结：说说这个项目里哪一处让你对「本卷的知识点是一体的」体会最深。
