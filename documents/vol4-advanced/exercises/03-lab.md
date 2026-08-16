---
title: "卷四 Lab：编译期加工厂——从模板配方到协程惰性序列"
description: "卷四动手实验：把模板配方、偏特化 type_traits、concepts 四成分、CRTP 汇编、协程惰性序列串成一条「编译期 vs 运行期」的解剖线——五个步骤从四种模板实体一路做到协程生成器，最后附一道只用编译期手段做 typelist 去重求和的 L5 挑战（源自 Modern C++ Design 思路）。每步有验收标准，参考答案附全部真实输出。"
chapter: 4
order: 3
tags:
  - host
  - advanced
  - cpp-modern
  - 模板
  - 模板元编程
  - concepts
  - CRTP
  - coroutine
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 5
prerequisites: []
related: []
---

# 卷四 Lab：编译期加工厂——从模板配方到协程惰性序列

## 实验目标

本卷的知识像两条流水线：一条在**编译期**（模板配方、偏特化、concepts、CRTP），一条在**运行期**（协程惰性计算）。这个 Lab 把它们拧成一条解剖线：你拿着「实例化地址」「偏特化模式」「requires 四成分」「objdump」「协程句柄」这几把刀，把「同一份模板怎么变成两份不同的函数」「一个概念怎么在编译期判真伪」「一条装饰链怎么被内联成零」「一个无限序列怎么只算前五个」这些问题一个个解剖开。做完你会对「把计算推到编译期」和「把计算推迟到用时」这两件事都有肌肉记忆。

所有实验在 `/tmp` 下独立目录做（比如 `/tmp/cpp-v4-lab`）。每步有验收标准；卡住先回题面每步标注的章节链接读教材，再不行看[实验参考](./04-lab-solutions.md)。

## 步骤 1：模板配方与四种实体 {#lab-1}

难度 **L1** · 涉及[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)

**目标**：把四种模板实体各亮一遍，并用「函数地址」证明实例化是「各抄一份」。

1. 写函数模板 `scale`（×2）、类模板 `Box`、变量模板 `golden<T>`（黄金比例 1.618...）、别名模板 `Vec<T> = std::vector<T>`，各用一个实例打印输出。
2. 取 `&scale<int>` 和 `&scale<double>` 两个函数指针，转成 `void*` 比较并打印地址。

**验收标准**：贴出输出；一句话说清「两个地址不同」证明的是什么（实例化是编译期照配方各抄一份，两份是**不同的函数**）。

[实验参考 →](./04-lab-solutions.md#lab-1)

## 步骤 2：手写 type_traits {#lab-2}

难度 **L2** · 涉及[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)

**目标**：用「主模板默认 false + 偏特化命中 true」的套路手写三个 trait。

1. 手写 `IsPointer`（`T*` 偏特化）、`IsSame`（`IsSame<A, A>` 偏特化）、`RemoveRef`（`T&`/`T&&` 两个偏特化）。
2. 写六条 `static_assert`（含 `IsPointer<int**>`、`RemoveRef<const int&&>::type` 是 `const int` 这类刁钻案例），运行期再打印五个查询结果。

**验收标准**：贴出输出；说清「一个 `T*` 偏特化为什么同时匹配 `int*` 和 `int**`」。

[实验参考 →](./04-lab-solutions.md#lab-2)

## 步骤 3：concepts 四成分与报错对比 {#lab-3}

难度 **L3** · 涉及[Requires 表达式深度解析](../vol3-metaprogramming-cpp20-23/03-requires-expressions.md)、[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)

**目标**：写一个四种成分齐全的概念，并亲手对比 enable_if 与 concept 的报错。

1. 写 `Serializable` 概念，四种成分一次用全：简单要求（`t.serialize();`）、类型要求（`typename T::id_type;`）、复合要求（`{ t.id() } -> std::convertible_to<int>;`）、嵌套要求（`requires sizeof(typename T::id_type) <= 8;`）。
2. 用 `EventA`（`id_type = int`）和 `EventB`（`id_type = long double`，16 字节 > 8）验证正反例，打印 `sizeof(long double)` 佐证。
3. 报错对比：写只收算术类型的 `process`，①`std::enable_if_t` 版、②`Arithmetic` concept 版，都拿 `std::string` 调，各自编译，把两份真实报错的关键行都贴下来。

**验收标准**：贴出三份输出；能指出 `EventB` 卡在哪一条成分、以及两份报错「讲的是谁」的差别。

[实验参考 →](./04-lab-solutions.md#lab-3)

## 步骤 4：CRTP 的汇编实证 {#lab-4}

难度 **L4** · 涉及[CRTP](../vol1-basics-cpp11-14/09-crtp.md)

**目标**：用 objdump 亲手看到「静态多态零开销」和「虚函数分派开销」的差距。

1. 写 CRTP 版 `use_crtp()`（返回 42）和虚函数版 `use_virtual(BaseV&)`，各用 `g++ -std=c++20 -O2 -c` 编译。
2. `objdump -d` 把两个函数的反汇编都贴出来，逐行数指令条数与内存访问次数。

**验收标准**：贴出两段汇编；说清 CRTP 版的两条指令是什么、虚函数版的两次内存解引用各在解什么。

[实验参考 →](./04-lab-solutions.md#lab-4)

## 步骤 5：协程惰性序列 {#lab-5}

难度 **L4** · 涉及[协程基础](../01-coroutine-basics.md)、[迭代器模式](../vol4-generics-patterns/18-iterator.md)

**目标**：手写一个 `Generator<T>`，让「无限序列」只在你 `next()` 的时候才往前走。

1. 手写 `Generator<T>` 的 `promise_type`（`initial_suspend`/`final_suspend` 都返回 `suspend_always`），对外用 `next()`/`value()` 接口。
2. 写一个**无限**计数器协程 `counter()`（`while (true) co_yield i++;`），只取前 5 个打印；再写一个无限斐波那契 `fibs()`，取前 10 个打印。
3. 思考题：为什么「无限」协程不会死循环——`co_yield` 那一刻发生了什么？

**验收标准**：贴出两组输出；说清 `initial_suspend` 挂起与「你不调 next 它一行不跑」之间的因果。

[实验参考 →](./04-lab-solutions.md#lab-5)

## 附加挑战（L5）：编译期 typelist 去重与求和 {#lab-l5}

**目标**：**只用编译期手段**（typelist 技术源自 Andrei Alexandrescu《Modern C++ Design》第 3 章与 Boost.MPL 思路；L5 档位口径见[练习总览](../exercises/index.md)）实现类型列表的去重与求和。

1. 定义 `Nil` 与 `Cons<H, T>`，实现 `Contains`（`is_same` 逐个比对）、`UniqueImpl`（累加器 + 条件插入）、`Reverse`（尾递归）、`Unique`（去重且保持原顺序）、`SumSizes`（`sizeof` 求和）、`Length`。
2. 用 `L = Cons<char, Cons<int, Cons<char, Cons<int, Cons<double, Nil>>>>>` 验证：`static_assert` 断言长度 5、去重后 3、去重结果头三个依次 `char/int/double`、去重前后 `SumSizes` 分别是 18 和 13。
3. 运行期打印去重前后的 `sizeof` 序列。

**验收标准**：贴出输出；说清 `UniqueImpl` 的累加器为什么是反序的、`Reverse` 为什么必须存在；并说明 `SumSizes<L>` 的 18 是怎么在编译期算出来的（逐项展开 `sizeof(char) + sizeof(int) + ...`）。

[实验参考 →](./04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下全部源码、每步终端记录（`stepN.log`）、以及 200 字以内的小结——用你自己的话说清「同一份模板、不同实例化就是不同实体」这件事你在哪一步看得最真切（步骤 1 的地址，还是步骤 4 的汇编）。
