---
title: "卷五课后练习（Homework）"
description: "卷五并发编程的课后练习：ch00~ch09 每章 2 题（基础+进阶），另加 2 道跨章综合与 1 道 L5 挑战（有界 MPMC 无锁队列，改编自 Dmitry Vyukov 算法）。难度覆盖 L1~L5，题目都做了变式处理，参考答案独立成文件、逐步解答附知识点链接，全部代码在 WSL（g++ 16 / clang++ 22，C++20）真编译真跑，并发题 TSan 实测。"
chapter: 5
order: 10
tags:
  - host
  - cpp-modern
  - intermediate
  - 并发
  - atomic
  - mutex
difficulty: intermediate
platform: host
reading_time_minutes: 25
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷五课后练习（Homework）

## 引言

这里的题按章组织，每章两道（基础 + 进阶），最后是两道跨章综合和一道 L5 挑战。每题标注难度档位（L1~L5，见[卷五练习体系](index.md)）和涉及章节。题目都是「变式」——换场景、换推理方向，照抄教材例题抄不出答案；每道题都要真编译真跑，把输出贴下来才算完。

答案在独立的[参考答案](11-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一章做完再看答案。所有代码用 `g++ -std=c++20 -Wall -Wextra -pthread` 起步；并发题的最终验收一律过 TSan（`-fsanitize=thread -g`，这是**编译期**选项，不是运行参数）。

## 5.0 并发思维与基础

### 5.0-A {#hw-5-0-a}

难度 **L1** · 涉及[为什么需要并发](../ch00-concurrency-fundamentals/01-why-concurrency.md)、[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)

一个程序有 80% 的计算量可以并行、20% 必须串行。①用 Amdahl 定律手算 4 核、8 核、无穷核的理论加速比。②写一个程序：计算 1+2+…+2×10⁹，先单线程跑一遍，再分成 8 块交给 8 个 `std::thread` 跑一遍（每线程算自己的局部和，主线程汇总），打印 `std::thread::hardware_concurrency()`、两次耗时与实测加速比。③实测为什么到不了理论值（至少说两条原因）？提示：编译器会把「1 加到 N」直接折叠成 $\frac{N(N+1)}{2}$ 闭式解，想真跑循环就把上限从命令行读进来。

[参考答案 →](11-homework-solutions.md#hw-5-0-a)

### 5.0-B {#hw-5-0-b}

难度 **L2** · 涉及[CPU cache 与 OS 线程](../ch00-concurrency-fundamentals/03-cpu-cache-and-os-threads.md)

两个线程各自对**自己的** `std::atomic<int64_t>` 做 `fetch_add(1, relaxed)` 一亿次——两个计数器挨在一起（同缓存行）和用 `alignas(64)` 分开（各占一行），哪个慢？先预测再实测（各跑 3 轮），贴出真实耗时与比值。两个问题：①为什么「atomic 自增」也逃不过伪共享？②`alignas(64)` 的 64 是从哪来的，换成 `alignas(32)` 行不行？

[参考答案 →](11-homework-solutions.md#hw-5-0-b)

## 5.1 线程生命周期与 RAII

### 5.1-A {#hw-5-1-a}

难度 **L1** · 涉及[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)、[线程参数与生命周期](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md)

写一个程序，分三个模式（用 `argv[1]` 切换）。模式 `id`：创建一个线程，打印 join 前 `joinable()` 与 `get_id()`，join 后再次打印，并与 `std::thread::id{}` 比较。模式 `terminate`：创建线程后**既不 join 也不 detach**，直接走出作用域，贴出真实输出与退出码。模式 `except`：让线程函数抛出异常且不捕获，join 后观察，贴出输出与退出码。解释两种崩溃为什么都指向 `std::terminate`、以及主线程的 try-catch 为什么接不住线程里的异常。

[参考答案 →](11-homework-solutions.md#hw-5-1-a)

### 5.1-B {#hw-5-1-b}

难度 **L3** · 涉及[线程参数与生命周期](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md)、[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)

三道实验。①写 `void update_value(int& x) { x = 42; }`，用 `std::thread t(update_value, value);` 直接传 `int`，贴出编译错误并解释它源于哪条规则（decay-copy）；改用 `std::ref` 后验证修改可见。②写一个「detach + 引用捕获栈上局部变量」的函数，循环调用 200 次，普通构建跑一遍（如实记录你看到了什么），再分别用 TSan 和 ASan 构建跑一遍，贴出两份报告，说清它们各自点名的对象。③为什么普通构建下它常常「看起来没事」？这正是哪一类 bug 的特征？

[参考答案 →](11-homework-solutions.md#hw-5-1-b)

## 5.2 互斥量、条件变量与同步原语

### 5.2-A {#hw-5-2-a}

难度 **L2** · 涉及[mutex 与 RAII 锁](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)

四道小题。①写一个 `bad_push`：循环里写 `std::lock_guard<std::mutex>{mtx};`（花括号临时对象），8 线程各自增共享计数 10 万次，打印最终值对照期望值，贴出编译器的警告与 TSan 报告。②改成具名 `lock_guard` 验证计数精确。③用 `std::recursive_mutex` 写一个递归函数打印深度 3→0。④用 `std::timed_mutex` 验证：空闲锁 `try_lock_for(100ms)` 立即成功；另一线程持锁 300ms 时，`try_lock_for(100ms)` 超时并测出实际等待时间。

[参考答案 →](11-homework-solutions.md#hw-5-2-a)

### 5.2-B {#hw-5-2-b}

难度 **L3** · 涉及[condition_variable 与等待语义](../ch02-mutex-condition-sync/03-condition-variable.md)

复现丢失唤醒。消费者线程**故意先睡 200ms** 再进入 `cv.wait(lock)`（不带谓词），生产者 50ms 时置 `ready=true` 并 `notify_one()`。①跑程序，用 `timeout 5` 兜底，贴出真实输出与退出码——为什么消费者永远醒不来？②把 `wait` 换成带谓词版本，重新跑，解释谓词为什么能同时防住丢失唤醒与虚假唤醒。③这个场景下生产者置位与 notify 之间隔不隔锁有区别吗？`notify_one` 和 `notify_all` 这里怎么选？

[参考答案 →](11-homework-solutions.md#hw-5-2-b)

## 5.3 原子操作与内存模型

### 5.3-A {#hw-5-3-a}

难度 **L2** · 涉及[并发基本问题](../ch00-concurrency-fundamentals/02-concurrency-problems.md)、[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)

①8 线程各对**非原子** `int` 自增 100 万次，普通构建连跑 5 次、再换 24 线程跑 3 次，如实贴出每次结果——本机很可能一次都不丢，这不是「没问题」，是运气。②用 TSan 构建跑一遍，贴出报告与这次的最终值，解释为什么 TSan 版本反而丢了数。③写三种修复（`fetch_add(relaxed)`、CAS 循环、mutex），各验证精确，贴出 CAS 重试次数；再写两行代码验证 `fetch_add` **返回旧值**而非新值。

[参考答案 →](11-homework-solutions.md#hw-5-3-a)

### 5.3-B {#hw-5-3-b}

难度 **L4** · 涉及[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)、[fence 与编译器屏障](../ch03-atomic-memory-model/03-fence-and-barrier.md)、[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)

两道题。①写消息传递：生产者写非原子 `g_data=42` 后 `ready.store(true, relaxed)`，消费者自旋 `ready.load(relaxed)` 后打印 `g_data`。用 TSan 跑贴报告——为什么 data 上出了 race？把两端改成 release/acquire，TSan 应干净，贴输出。②写 Dekker 式重排实验（改编自 Preshing《Memory Reordering Caught in the Act》的结构）：两个 worker 反复 `x.store(1, mo); r = y.load(mo);`，主线程每轮复位并统计 `r1==0 && r2==0` 的次数，分别用 `relaxed` 与 `seq_cst` 跑 30 万轮，如实贴出 (r1,r2) 分布。**如实报告**：x86 上很可能观察不到 00（为什么？）；seq_cst 下 00 为什么是**被禁止**的（用 happens-before 论证）？如果你的机器观察到非零的 00，那是 x86 store-load 重排的实锤，请保留数据。

[参考答案 →](11-homework-solutions.md#hw-5-3-b)

## 5.4 并发数据结构

### 5.4-A {#hw-5-4-a}

难度 **L3** · 涉及[mutex 与 RAII 锁](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)、[线程安全队列](../ch04-concurrent-data-structures/01-thread-safe-queue.md)、[并发基本问题](../ch00-concurrency-fundamentals/02-concurrency-problems.md)（race condition 与 data race 之分）

实现一个线程安全栈：`push` / `pop`（返回 `std::optional<T>`，空栈返 `std::nullopt`）/ `top`。①用 `std::latch` 编排两个消费者：栈里只有一个元素 42，两个线程都先 `top()`（latch 汇合）再各 `pop()` 一次——贴出四个返回值，解释「两个人都看到 42、但只有一个人能取走」说明 `top`+`pop` 两步操作之间发生了什么，为什么 `pop` 返回 `optional` 的单一调用就没有这个问题。②4 生产者各压入 1 万个互不相同的整数、4 消费者并发取完，用 `std::set` 收集验证不丢不重，TSan 干净。

[参考答案 →](11-homework-solutions.md#hw-5-4-a)

### 5.4-B {#hw-5-4-b}

难度 **L4** · 涉及[无锁编程基础](../ch04-concurrent-data-structures/03-lock-free-basics.md)、[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)

实现 Treiber 无锁栈：`head_` 是 `std::atomic<Node*>`，push 用 CAS 循环挂新节点，pop 先摘节点再搬数据；加一个原子计数器统计 CAS 失败重试次数。①4 生产者 × 5 万 + 4 消费者验证不丢不重，贴出重试次数。②与 `mutex + std::stack` 版本各跑 3 轮吞吐对比——**如实贴出**你的结果（本机无锁版很可能会输，说清为什么：低争用下 futex 快速路径 + 每 push 一次 `new` 的分配开销）。③用 TSan 跑初版（pop 的 CAS **失败序**写 `relaxed`），大概率会报 race——定位它并解释：CAS 失败后 `old` 被更新为当前头节点，接着读 `old->next`，这次失败路径上的 load 没有与写 `next` 的线程建立 happens-before；把失败序改成 `acquire` 后 TSan 干净。④这道题为什么可以「不 delete 弹出的节点」？真实工程里这一步叫什么、有哪几种解法？

[参考答案 →](11-homework-solutions.md#hw-5-4-b)

## 5.5 future、任务与线程池

### 5.5-A {#hw-5-5-a}

难度 **L2** · 涉及[std::async 与 future](../ch05-future-task-threadpool/01-std-async-and-future.md)

写一个打印自己线程 ID、睡 300ms 后返回 `id*10` 的任务。①分别用 `std::launch::async` 与 `std::launch::deferred` 提交，贴出线程 ID 对比，证明 deferred 版是在调用 `get()` 的线程上**同步**执行的。②连续写三行 `std::async(std::launch::async, task, i);` 但**不保存返回值**，测总耗时；再把三个 future 存进 vector 后统一 `get()`，测总耗时。贴出两个数字并解释差距的来源（临时 future 析构的行为）。③不指定策略的 `std::async` 有什么隐患？（参考 Effective Modern C++ Item 36）

[参考答案 →](11-homework-solutions.md#hw-5-5-a)

### 5.5-B {#hw-5-5-b}

难度 **L3** · 涉及[promise 与 packaged_task](../ch05-future-task-threadpool/02-promise-and-packaged-task.md)

三件事。①用 `promise`/`future` 搭一条三线程处理链：A 产出 21 → B 翻倍 → C 打印最终值。②让一个线程函数抛 `std::invalid_argument`，用 `set_exception(std::current_exception())` 存入 promise，主线程 `get()` 时捕获并打印 `what()`——贴出异常跨线程传播的证据。③用 `std::shared_future` 做「发令枪」：4 个线程阻塞在同一个 `shared_future::get()` 上，主线程睡 300ms 后 `set_value(42)`，贴出 4 个 runner 全部苏醒的输出；说清 `shared_future` 和 `future` 在「谁可以读」上的本质区别。

[参考答案 →](11-homework-solutions.md#hw-5-5-b)

## 5.6 异步 I/O 与协程

### 5.6-A {#hw-5-6-a}

难度 **L3** · 涉及[C++20 协程基础](../ch06-async-io-coroutine/02-coroutine-basics.md)、[promise_type 与 awaitable](../ch06-async-io-coroutine/03-promise-type-and-awaitable.md)

手写一个 `Generator<int>`（懒启动 `initial_suspend`=suspend_always、`final_suspend`=suspend_always、`yield_value` 存当前值）。①写 `squares(n)` 协程产出前 n 个平方数，用 `next()`/`value()` 循环打印前 8 个。②给 promise 加 `std::exception_ptr`，`unhandled_exception()` 存异常、`next()` 里 `rethrow_exception`；写一个产出到第 7 个数就 `throw` 的协程，主循环 try-catch 捕获并打印。③解释：协程的局部变量为什么能在挂起点之间安全存活（它们住在哪）？`final_suspend` 为什么必须返回 `suspend_always`？

[参考答案 →](11-homework-solutions.md#hw-5-6-a)

### 5.6-B {#hw-5-6-b}

难度 **L4** · 涉及[promise_type 与 awaitable](../ch06-async-io-coroutine/03-promise-type-and-awaitable.md)、[C++20 协程基础](../ch06-async-io-coroutine/02-coroutine-basics.md)

写一个 `ChainTask`：`await_suspend` 里存下 caller 并**返回自己的 handle**（对称转移），`final_suspend` 返回一个能把控制权转回 caller 的 awaitable。写 `chain(depth, counter)` 递归 co_await 自己，构造一条 100 万层的链。①`-O2` 构建跑一遍贴结果；②`-O0` 构建跑同一深度贴结果——两者差异的根源是什么？③为什么教材说「尾调用优化是 QoI 而非标准保证」？在你的编译器上验证这个说法。

[参考答案 →](11-homework-solutions.md#hw-5-6-b)

## 5.7 Actor 与 Channel

### 5.7-A {#hw-5-7-a}

难度 **L2** · 涉及[Actor 模型与消息传递](../ch07-actor-channel/01-actor-model.md)、[condition_variable 与等待语义](../ch02-mutex-condition-sync/03-condition-variable.md)

用最简 mailbox（`mutex + condition_variable + queue`，带 `close()`）实现两个「actor」线程的乒乓球：线程 A 往 mailbox_ab 发 `i`（1..10000）并等待回复，线程 B 收到后回 $i\times 2$，A 累加所有回复；A 发完哨兵后 `close()` 让 B 退出。①贴出总轮数与回复总和（应为 100010000）。②TSan 跑一遍贴结果。③回答：这里的「消息」是值语义还是共享语义？在真实 Actor 框架里大消息应该怎么传（`shared_ptr` 的权衡是什么）？

[参考答案 →](11-homework-solutions.md#hw-5-7-a)

### 5.7-B {#hw-5-7-b}

难度 **L3** · 涉及[Actor 模型与消息传递](../ch07-actor-channel/01-actor-model.md)（variant + visit 模式匹配）

定义 `using Message = std::variant<Inc, Query, Stop>;`（`Inc` 带 `int delta`）。①用 overload 模式写 `std::visit`，**故意漏掉 `Stop` 的处理器**，贴出编译错误——它和 `switch` 漏 case 的警告有什么本质区别？②补全三个处理器，处理消息序列 `{Inc{5}, Query{}, Inc{3}, Stop{}}`，维护一个计数器，贴出状态迁移输出。③如果加一个 `[](auto&&)` 通配兜底，编译器还会帮你查漏吗？通配兜底的代价是什么？

[参考答案 →](11-homework-solutions.md#hw-5-7-b)

## 5.8 调试、测试与性能

### 5.8-A {#hw-5-8-a}

难度 **L2** · 涉及[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)

实现一个线程安全 logger：先在线程本地 `std::ostringstream` 拼好「纳秒时间戳 + 线程 ID + 级别 + 消息」整行，再加锁一次性输出。①4 线程各打 5 行日志，贴出真实输出——为什么每行都完整不交错？②为什么要在**锁外**拼字符串？③说出至少两个 `std::cout` 直接多线程输出的具体问题。

[参考答案 →](11-homework-solutions.md#hw-5-8-a)

### 5.8-B {#hw-5-8-b}

难度 **L3** · 涉及[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)、[死锁与锁顺序](../ch02-mutex-condition-sync/02-deadlock-and-lock-ordering.md)

两个独立场景。①计数器类 `increment()` 里裸 `++count_`：普通构建跑（如实贴结果——本机很可能「恰好对」），TSan 构建跑贴报告，改 `fetch_add(relaxed)` 后 TSan 干净——总结「复现 → 分类 → 选工具 → 修 → 回归」五步里你实际走了哪几步。②写 AB-BA 死锁：两个线程各按相反顺序锁两把 mutex、中间 `sleep(100ms)` 制造窗口，`timeout 3` 跑贴输出与退出码；改用 `std::scoped_lock` 后正常完成。死锁为什么不是 TSan 的主战场？它靠什么工具或手段定位？

[参考答案 →](11-homework-solutions.md#hw-5-8-b)

## 5.9 分布式桥接

### 5.9-A {#hw-5-9-a}

难度 **L2** · 涉及[从单机并发到分布式](../ch09-distributed-bridge/01-from-concurrent-to-distributed.md)

用两个「节点」结构体 + 一个全局 `partitioned` 开关模拟网络分区。①CP 路径：分区期间 `cp_write` 拒绝写入并打印原因，分区恢复后接受；②AP 路径：分区期间 S1 接受写入 200、S2 读到的仍是旧值 100；网络恢复后 `heal()` 把 S1 同步到 S2，再读得 200。贴出决策日志。③分别说出一个 CP 系统和一个 AP 系统的真实例子，并说明「分布式锁」与「单机 mutex」在保证强度上的根本区别（一个词概括）。

[参考答案 →](11-homework-solutions.md#hw-5-9-a)

### 5.9-B {#hw-5-9-b}

难度 **L4** · 涉及[从单机并发到分布式](../ch09-distributed-bridge/01-from-concurrent-to-distributed.md)、[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)

模拟 Kleppmann 的 fencing token 论证。锁服务：`try_acquire()` 检查租约（100ms）是否过期，过期则版本号 +1 并发放新租约；共享资源提供两个写入接口：`write_no_fencing(v, version)`（不看版本，直接覆盖）和 `write_fenced(v, version)`（版本小于当前已记录版本就拒绝）。①worker A 拿锁后干活 300ms（租约中途过期），B 拿到新版本锁并写入 100，A 干完用旧版本写 999——无 fencing 时资源终值是多少、谁该对这笔账负责？有 fencing 时 A 的写入被拒绝、B 的 100 保住。贴出两组输出。②说出 Redis 分布式锁「超时 + GC/调度停顿」为什么会破坏互斥，fencing token 是如何在**资源侧**兜底的。

[参考答案 →](11-homework-solutions.md#hw-5-9-b)

## 5.C 跨章综合与挑战

### 5.C-1 {#hw-5-c-1}

难度 **L3** · 涉及[为什么需要并发](../ch00-concurrency-fundamentals/01-why-concurrency.md)、[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)、[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)

并行计算 π（中点矩形法：4/(1+x²) 在 [0,1] 积分）。①单线程、8 线程「每线程局部和 + 主线程汇总」、8 线程「全局 `atomic<double>` CAS 累加」三种实现，4×10⁸ 步，贴出三个 π 值与耗时。②三种结果精度差多少？为什么？③「局部和 + 汇总」和「CAS 累加」谁更快？这印证了哪种模式的威力（每线程局部状态消除竞争）？④两个原子浮点注意点（教材里讲过）各是什么？

[参考答案 →](11-homework-solutions.md#hw-5-c-1)

### 5.C-2 {#hw-5-c-2}

难度 **L4** · 涉及[并发基本问题](../ch00-concurrency-fundamentals/02-concurrency-problems.md)、[mutex 与 RAII 锁](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)、[并发性能测试与基准](../ch08-debug-testing-perf/02-concurrency-benchmarks.md)、[线程参数与生命周期](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md)

对 2000 万个 [0,15] 随机整数做 16 桶直方图。①单线程、8 线程「每线程局部 16 桶 + 汇总」、8 线程「每次更新持一把全局 mutex」三种实现，验证结果完全一致，贴出三个耗时——mutex 版会慢两个数量级，说说为什么（锁竞争 + 锁粒度）。②三种实现的正确性依据分别是什么（哪个根本不需要同步）？③用 TSan 跑贴结果。**额外彩蛋**：如果你的 mutex 版 worker 用 `[&]` 捕获了循环变量 `w`，TSan 会精准点名——试试复现这个「引用捕获循环变量」的坑，再改成值捕获。

[参考答案 →](11-homework-solutions.md#hw-5-c-2)

### 5.C-3 {#hw-5-c-3}

难度 **L5** · 涉及[SPSC 与 MPMC 队列](../ch04-concurrent-data-structures/04-lock-free-queues.md)、[无锁编程基础](../ch04-concurrent-data-structures/03-lock-free-basics.md)、[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)

挑战题（改编自 **Dmitry Vyukov 的 Bounded MPMC queue**（1024cores.net），按竞赛挑战强化；卷五档位口径见[卷五练习体系](index.md)）。实现一个有界多生产者多消费者无锁队列：`Capacity` 为 2 的幂，每格一个 `sequence` 号 + 数据，`enqueue_pos_`/`dequeue_pos_` 是 `alignas(64)` 的原子游标，`enqueue` 用 CAS 抢占 `pos` 后写数据再 `release` 发布 `pos+1`，`dequeue` 对称，判满判空全靠 `sequence` 与游标的差值符号。验收：①4 生产者 × 10 万 + 4 消费者，元素不丢不重（用「总和 = $\frac{kTotal×(kTotal-1)}{2}$」强校验，这个校验能同时抓住丢和重）；②TSan 与 UBSan 构建零报告；③口头解释：为什么 `dequeue` 的判空条件是 `dif < 0` 而 `enqueue` 判满也是 `dif < 0`（两个「0」的语义差别）；④它的「有界」相比教材 Michael-Scott 无界队列省掉了什么（提示：内存回收）。

[参考答案 →](11-homework-solutions.md#hw-5-c-3)
