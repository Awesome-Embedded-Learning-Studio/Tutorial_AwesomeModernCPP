---
title: "卷五 Lab 实验参考"
description: "卷五 Lab（原子模式实验台）的实验参考：六步加 L5 挑战的逐步解答，每步标注知识点链接，所有输出在 WSL Arch（g++ 16.1.1，C++20）真实运行得到；TSan 对 SeqLock 的误报、对 fence 的不支持等「意外结果」如实呈现并解释。"
chapter: 5
order: 13
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - memory_order
  - 无锁
difficulty: advanced
platform: host
reading_time_minutes: 11
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷五 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1，C++20）真实运行得到。建议卡住时先看「思路」逐步对照。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。

## 步骤 1：量内存序的开销 {#lab-1}

**思路**：把内存序做成模板参数，同一段循环跑两种序。x86 的 TSO 模型下 acquire/release/seq_cst 的 store 几乎一样贵，relaxed 只省掉了理论上的排序约束，差距应在噪声内。

1. `measure<Mo>(4, 5'000'000)` 用 4 线程各 500 万次 `fetch_add(1, Mo)`。→ 知识点：[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)「fetch_add、fetch_sub 与位运算」一节
2. 本机 3 轮：relaxed 172/166/174 ms，seq_cst 169/171/167 ms，比值 0.96~1.03——**没有显著差异**，这正是 x86 的预期：`fetch_add` 无论如何都是一条 `lock xadd`（带全屏障），relaxed 与 seq_cst 生成同一条指令。这条结论在 ARM 上不成立（seq_cst 要多一道 DMB），所以「内存序优化」是弱内存序架构上的游戏。→ 知识点：[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)「memory_order_seq_cst」一节（代价论述）

**验证输出**：

```text
$ g++ -std=c++20 -O2 -pthread lab1.cpp -o lab1 && ./lab1
round 1: relaxed=172 ms, seq_cst=169 ms, ratio=0.982558
round 2: relaxed=166 ms, seq_cst=171 ms, ratio=1.03012
round 3: relaxed=174 ms, seq_cst=167 ms, ratio=0.95977
```

## 步骤 2：atomic_wait 的语义边界 {#lab-2}

**思路**：`wait(old)` 阻塞的条件是「当前值 == old」；通知不排队，值变了就不会白等。

1. `notify_all`：4 个等待者全部醒来（`woken=4`）。→ 知识点：[atomic_wait 与 atomic_ref](../ch03-atomic-memory-model/04-atomic-wait-and-ref.md)「wait/notify：原子变量的自带 condition variable」一节
2. `notify_one` + 200ms：`woken=1`——只醒了一个（如实记录：伪唤醒理论上允许多醒，本机这次没有）。→ 知识点：同上「notify 的保证与局限」一节
3. `wait(0)` 作用在值为 7 的变量上：立即返回，实测 0µs。参数是「期望的旧值」而非「目标值」，因为内部语义是「值若等于 old 才阻塞」——这天然避免了先检查再等待之间的 TOCTOU 竞态（值在检查后、阻塞前被改，则比较失败直接返回，不会错过通知）。→ 知识点：同上「wait 的值语义：一种容易误解的设计」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 -pthread lab2.cpp -o lab2 && ./lab2
  waiter 0 woke, signal=1
  waiter 1 woke, signal=1
  waiter 3 woke, signal=1
  waiter 2 woke, signal=1
notify_all: woken=4
  waiter 0 woke, signal=1
notify_one + 200ms: woken=1
  waiter 3 woke, signal=1
  waiter 1 woke, signal=1
  waiter 2 woke, signal=1
after cleanup notify_all: woken=4
```

## 步骤 3：atomic_ref 的正确用法与红线 {#lab-3}

**思路**：`atomic_ref` 是对已有对象的「原子视图」，对象本身不用改类型；但视图存续期间，所有访问都必须走视图。

1. 正确用法：4×100 万次 `fetch_add` 得 4000000 整——普通 `int` 数组也能无锁累加。→ 知识点：[atomic_wait 与 atomic_ref](../ch03-atomic-memory-model/04-atomic-wait-and-ref.md)「std::atomic_ref\<T>」一节
2. 红线实验：一半 `fetch_add`、一半裸写混用，普通构建最终值 2323399（期望 400 万，丢了 168 万）；TSan 报 2 个 race：一条是「Atomic write（fetch_add）」vs 另一线程的裸写，另一条是裸写 vs 裸写——TSan 甚至能把「atomic 侧」和「非 atomic 侧」分开点名。→ 知识点：同上「限制与约束」一节（第二条：存在 atomic_ref 期间对象只能经 atomic_ref 访问）

**验证输出**：

```text
$ ./lab3
g_counters[0] = 4000000 (expected 4000000)
g_counters[1] = 2323399 (expected 4000000)

$ ./lab3_tsan
WARNING: ThreadSanitizer: data race (pid=329)
  Atomic write of size 4 at 0x5555555581dc by thread T6:
    #0 std::__atomic_ref<...>::fetch_add(int, std::memory_order) const
    #1 mixed_use() /tmp/.../lab3.cpp:23
  Previous write of size 4 at 0x5555555581dc by thread T5:
    #0 mixed_use() /tmp/.../lab3.cpp:25     ← 裸写 ++g_counters[1]
SUMMARY: ThreadSanitizer: data race /tmp/.../lab3.cpp:23 in mixed_use()
ThreadSanitizer: reported 2 warnings
```

## 步骤 4：SeqLock——读多写少的终极形态 {#lab-4}

**思路**：写者靠「偶数→奇数→偶数」的序号变化框住写入区间，读者「读前确认偶数、读后确认序号未变」。实现见步骤 4 参考代码（与教材 ch03-05 的 SeqLock 同构）。

1. 普通构建：`reads=400001 retries=4426 violations=0`——4 万次读里 4426 次撞上写入被迫重试，**撕裂为 0**。→ 知识点：[原子操作模式](../ch03-atomic-memory-model/05-atomic-patterns.md)「SeqLock：读取器不被阻塞的序列锁定」一节
2. TSan 构建：**报 2 个 race**，位置都是读者读 `g_data` vs 写者写 `g_data`。**如实解释**：TSan 的 happens-before 模型认 mutex/标准原子对，但不理解「序号验证」这类自定义协议——它看到的就是无保护的读写。你的实现仍然正确：写者的两次 `release` store 把数据写入框住，读者 `read_begin` 的 `acquire` load 若读到写者 `unlock_write` 发布的偶数序号，就与那次 release store 构成 synchronizes-with，数据写入 happens-before 读者读取；`read_validate` 再确认期间没有新一轮写入。生产环境要让 TSan 闭嘴，用 `__tsan_acquire/__tsan_release` 注解告诉它你的协议。→ 知识点：[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「TSan 的局限」一节；[无锁编程基础](../ch04-concurrent-data-structures/03-lock-free-basics.md)「何时使用无锁——何时不使用」一节（正文观点：TSan 对无锁代码的误报率也不低）

**验证输出**：

```text
$ ./lab4
reads=400001 retries=4426 violations=0

$ ./lab4_tsan
WARNING: ThreadSanitizer: data race (pid=360)
  Read of size 4 at 0x555555559238 by thread T2:
    #0 operator() /tmp/.../lab4.cpp:72        ← 读者读快照
  Previous write of size 4 at 0x555555559238 by thread T1:
    #0 operator() /tmp/.../lab4.cpp:59        ← 写者写 g_data
SUMMARY: ThreadSanitizer: data race /tmp/.../lab4.cpp:72 in operator()
ThreadSanitizer: reported 2 warnings
```

## 步骤 5：引用计数的 acq_rel 选择 {#lab-5}

**思路**：`add_ref` 只关心计数原子性，relaxed 够；`release` 减到 0 的线程要**看到**此前所有使用者对对象的操作完成（acquire 半边），同时它的析构要在所有此前操作之后（release 半边）——acq_rel 一个都不能少。

1. 4 线程 × 50 万对 add/release，join 后主线程释放最后一个引用：`destroyed=true`，析构恰好一次。→ 知识点：[原子操作模式](../ch03-atomic-memory-model/05-atomic-patterns.md)「引用计数：shared_ptr 的原子基础」一节
2. TSan 构建干净（release/acq_rel 与 add/relaxed 都是标准原子操作，TSan 全部认可）。→ 知识点：[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)「memory_order_acq_rel」一节

**验证输出**：

```text
$ ./lab5
released, destroyed=true

$ ./lab5_tsan
released, destroyed=true
```

## 步骤 6：fence 版消息传递 {#lab-6}

**思路**：release fence 把「之前的写入」和「之后的 relaxed store」缝在一起，acquire fence 镜像之——fence 版与「带序的 store/load」版语义等价，只是工具支持不同。

1. 普通构建 4 轮全部正确（payload=100/200/300/400）。→ 知识点：[fence 与编译器屏障](../ch03-atomic-memory-model/03-fence-and-barrier.md)「fence-atomic 同步」一节
2. TSan 构建：编译期先出两条警告 `atomic_thread_fence is not supported with '-fsanitize=thread' [-Wtsan]`；运行时报 race 指向 `g_payload`。**如实报告**：这是 GCC TSan 的已知限制——它不把 fence 建模为同步原语。用带 release/acquire 的原子版本（本卷 5.3-B 题的 `mo_fixed` 已实测）跑同样的消息传递，TSan 干净。结论：语义等价，工具不认 fence——想要 TSan 帮你盯住这类代码，用带序的原子操作写法。→ 知识点：同上「fence 与原子操作的比较：何时用 fence」一节（劣势正是可读性与工具支持）

**验证输出**：

```text
$ g++ -std=c++20 -O2 -pthread lab6.cpp -o lab6 && ./lab6
round 1: consumer saw payload=100
round 2: consumer saw payload=200
round 3: consumer saw payload=300
round 4: consumer saw payload=400

$ g++ -std=c++20 -O1 -g -fsanitize=thread -pthread lab6.cpp -o lab6_tsan
lab6.cpp:12:29: warning: 'atomic_thread_fence' is not supported
  with '-fsanitize=thread' [-Wtsan]
lab6.cpp:21:29: warning: 'atomic_thread_fence' is not supported
  with '-fsanitize=thread' [-Wtsan]

$ ./lab6_tsan
WARNING: ThreadSanitizer: data race (pid=413)
  Read of size 4 at 0x555555558218 by main thread:
    #0 consumer_fence() /tmp/.../lab6.cpp:22
  Previous write of size 4 at 0x555555558218 by thread T1:
    #0 producer_fence(int) /tmp/.../lab6.cpp:11
SUMMARY: ThreadSanitizer: data race /tmp/.../lab6.cpp:22 in consumer_fence()
round 1: consumer saw payload=100
...
ThreadSanitizer: reported 1 warnings
```

## 附加挑战（L5）：Peterson 软件锁 {#lab-l5}

**思路**：Peterson 的正确性依赖「置位自己想进 → 让出优先权 → 检查对方」三步的**顺序可见性**，这只有 seq_cst 保证；relaxed 版在 x86 上靠 TSO 与编译器不重排「侥幸接近正确」，在弱内存序机器上必错。

1. seq_cst 版：`counter=1000000`，TSan 零报告——TSan 完全理解 seq_cst 原子构成的锁协议。→ 知识点：[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)「memory_order_seq_cst」一节；[原子操作模式](../ch03-atomic-memory-model/05-atomic-patterns.md)「自旋锁」一节（软件锁的原理同源）
2. relaxed 版普通构建：`counter=999997`——丢了 3 次更新（如实记录：本机 x86 上它「差点全对」）；TSan 构建：报 2 个 race 指向临界区里的 `++counter`，随后**死锁**，`timeout` 退出码 124。TSan 扰动时序后锁协议彻底失效——这正是弱内存序软件锁的「不确定行为」：普通跑像没事，工具一碰就现形。→ 知识点：[并发基本问题](../ch00-concurrency-fundamentals/02-concurrency-problems.md)「把问题分类：我们的路线图」一节文末的原则句（先正确性，再性能）；[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「并发 bug 的四大门派」一节（Heisenbug 的出处在此节）
3. 为什么：seq_cst 给所有原子操作一个全局全序，Peterson 的「意图-让路-检查」三步在全序里保持相对顺序；relaxed 允许任意重排，x86 的 TSO 恰好只允许 store-load 重排、且编译器碰巧没动它们，所以「大部分时候对」——但这不是保证，ARM 上 store-store 重排足以撕碎协议。对照步骤 1：内存序的「省」在 x86 上本来就省不出性能，代价却是把正确性交给运气——不划算。→ 知识点：[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)「为什么要重排」一节

**验证输出**：

```text
$ ./labl5
seq_cst: counter=1000000 expected=1000000
relaxed: counter=999997 expected=1000000

$ ./labl5_tsan     # seq_cst 部分先跑完（干净），relaxed 部分报 race 后死锁
WARNING: ThreadSanitizer: data race (pid=438)
  Read of size 4 at 0x7fffffffdde4 by thread T4:
    #0 run<PetersonLockRelaxed>(...) /tmp/.../labl5.cpp:68
  Previous write of size 4 at 0x7fffffffdde4 by thread T3:
    #0 run<PetersonLockRelaxed>(...) /tmp/.../labl5.cpp:61
SUMMARY: ThreadSanitizer: data race /tmp/.../labl5.cpp:68 in ...
exit=124        ← relaxed 版在 TSan 下死锁，timeout 兜底

$ ./labl5_seq_tsan    # 只跑 seq_cst 版的 TSan 构建
seq_cst peterson: counter=1000000 expected=1000000
```
