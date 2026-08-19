---
title: "卷五课后练习参考答案（Homework）"
description: "卷五课后练习的逐题详细解答：每道题给出解题思路、逐步解答（每步标注知识点链接）与真实验证输出。所有命令与输出在 WSL Arch（g++ 16.1.1，C++20）真实运行得到，数据竞争类题目贴真实 TSan 报告，无锁与原子题目跑 UBSan，凡与教材表述不一致之处如实说明。"
chapter: 5
order: 11
tags:
  - host
  - cpp-modern
  - intermediate
  - 并发
  - atomic
  - mutex
difficulty: intermediate
platform: host
reading_time_minutes: 45
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷五课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1，C++20）真实运行得到。data race 类题目的普通构建输出「只是这台机器这一次的选择」，换时序、换编译器、换插桩都可能不同——这正是每道题要你体会的东西。TSan 报告均为真实截取。

## 5.0-A {#hw-5-0-a}

**难度 L1** · 题面见 [homework](10-homework.md#hw-5-0-a)

**思路**：Amdahl 定律把加速比写死为串行比例的倒数上限；实测部分用「每线程局部和 + 汇总」的模式，并把上限从命令行读入以阻止编译器把循环折叠成闭式解。

1. 手算：$S(N) = \frac{1}{(1-f) + f/N}$，$f=0.8$：$S(4)=\frac{1}{0.2+0.2}=2.5$，$S(8)=\frac{1}{0.2+0.1}\approx 3.33$，$S(\infty)=\frac{1}{0.2}=5$。→ 知识点：[为什么需要并发](../ch00-concurrency-fundamentals/01-why-concurrency.md)「两个定律：Amdahl 与 Gustafson」一节
2. 代码：单线程 `sum_range(1, limit)`；并行版 8 个线程各自算 $[limit\times w/8+1, limit\times (w+1)/8]$ 写入 `partial[w]`，主线程汇总。**坑就地讲**：`limit` 必须从 `argv` 读入——写死成 constexpr 的话，GCC 16 会把 `sum_range` 的循环直接替换成 $\frac{n(n+1)}{2}$（级数求和优化），你测到的将是 0ms 和 `-nan` 加速比，这个坑笔者实测踩过。另外 1+…+2×10⁹ = 2.0×10¹⁸，刚好不溢出 `int64_t`（上限 9.2×10¹⁸），换成平方和就会溢出——求和前先估量级。→ 知识点：[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)「基本模式：派生线程，作用域退出时 join」一节
3. 实测到不了理论值的两条原因：①并行计时把 8 次线程创建/销毁算进去了，而 Amdahl 假设这部分为零；②本机 `hardware_concurrency` 是 24，8 线程只用了 1/3 的核，$S(8)=3.33$ 是按「只受串行比例限制」算的，实际还要受内存带宽与线程数约束。→ 知识点：[为什么需要并发](../ch00-concurrency-fundamentals/01-why-concurrency.md)「任务粒度：太细和太粗都不行」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 -pthread hw5_0_a.cpp -o hw5_0_a
$ ./hw5_0_a 2000000000
hardware_concurrency = 24, limit = 2000000000
serial   = 2000000001000000000
parallel = 2000000001000000000
serial 耗时:   420.496 ms
parallel 耗时: 66.089 ms
实测加速比:    6.36257
```

两版求和相等，实测加速比 6.36。理论 $S(8)\approx 3.33$ 是按 f=0.8 算的——本题负载其实是 100% 可并行，所以实测超过了那个数；这也说明 Amdahl 的用途是**给定串行比例估上限**，而不是预测任何程序的具体加速比。

## 5.0-B {#hw-5-0-b}

**难度 L2** · 题面见 [homework](10-homework.md#hw-5-0-b)

**思路**：两个 atomic 挨着放在同一 64 字节缓存行里，各核心写自己的计数器也要先把对方的缓存行抢过来（RFO），MESI 协议的乒乓开销随线程数放大。

1. 预测：挨着（同缓存行）慢；实测 3 轮，比值 4.55~4.59。→ 知识点：[CPU cache 与 OS 线程](../ch00-concurrency-fundamentals/03-cpu-cache-and-os-threads.md)「False sharing：看不见的性能杀手」一节
2. 为什么 atomic 也逃不过：`fetch_add` 保证**原子性**，但两个计数器物理上共享一条缓存行；原子操作本身不改变缓存行的归属，每次写都要 RFO 抢行。→ 知识点：同上（MESI 协议与 RFO 请求）
3. `alignas(64)` 的 64 是本机缓存行大小（`lscpu` 的 `coherency_line_size`）；`alignas(32)` 在 64 字节行的机器上**不行**——两个成员仍可能落在同一行。→ 知识点：同上「消除 false sharing：alignas 与缓存行填充」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 -pthread hw5_0_b.cpp -o hw5_0_b
$ ./hw5_0_b
round 1: close=1750 ms, apart=381 ms, ratio=4.59318
round 2: close=1769 ms, apart=388 ms, ratio=4.55928
round 3: close=1739 ms, apart=380 ms, ratio=4.57632
```

## 5.1-A {#hw-5-1-a}

**难度 L1** · 题面见 [homework](10-homework.md#hw-5-1-a)

**思路**：`std::thread` 析构时若仍 `joinable()`，标准规定直接 `std::terminate`；线程函数里逃逸的异常同理——两个「崩溃」同根同源。

1. `id` 模式：join 前 `joinable=1` 且有 id；join 后 `joinable=0`、`get_id() == std::thread::id{}` 为 1。→ 知识点：[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)「线程的标识与查询」一节
2. `terminate` 模式：退出码 134（SIGABRT），输出 `terminate called without an active exception`——注意线程甚至没来得及打印，析构就炸了。→ 知识点：同上「不 join 也不 detach 的后果：std::terminate」一节
3. `except` 模式：`terminate called after throwing ... boom from worker`，同样 134。主线程 try-catch 接不住：每个线程有独立调用栈，异常处理只在**当前线程**的栈上展开。→ 知识点：同上「线程函数中的异常」一节

**验证输出**：

```text
$ ./hw5_1_a id
before join: joinable=1, id=125945270761152
main thread id = 125945278822272
worker thread id = 125945270761152
after join: joinable=0, id=thread::id of a non-executing thread
id == std::thread::id{}? 1

$ timeout 10 ./hw5_1_a terminate; echo exit=$?
terminate called without an active exception
exit=134

$ timeout 10 ./hw5_1_a except; echo exit=$?
terminate called after throwing an instance of 'std::runtime_error'
  what():  boom from worker
exit=134
```

## 5.1-B {#hw-5-1-b}

**难度 L3** · 题面见 [homework](10-homework.md#hw-5-1-b)

**思路**：①decay-copy 把参数剥成 `int` 右值，绑不上 `int&` 形参；②悬垂引用普通构建下不必然炸——这正是「Heisenbug」；TSan/ASan 各有各的抓法。

1. 编译错误（g++ 16）：`static assertion failed: std::thread arguments must be invocable after conversion to rvalues`——`std::thread` 构造时对参数做 decay-copy，`int&` 退化成了 `int`。`std::ref` 用引用包装器保住引用，修改可见。→ 知识点：[线程参数与生命周期](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md)「decay-copy：所有参数都是按值传递的」一节
2. 悬垂实验：200 次 detach + 50ms 后写已出栈的 `local`。普通构建输出里能看到 `std::cout` 行被交错打断（`worker sees local=worker sees local=100100...`），但值几乎都是 100——栈位还没被覆盖。TSan 报 data race 指向 `local`（两个 worker 线程互写同一栈地址）；ASan 报 `stack-use-after-return`，直接点名 `'local' (line 7)` 对象。→ 知识点：[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「ThreadSanitizer」一节
3. 「看起来没事」是因为那块栈内存恰好还没被复用——这正是 data race / 悬垂引用这类**时序依赖型 bug**（Heisenbug）的特征：加打印、换负载、换插桩，行为全变。→ 知识点：同上「并发 bug 的四大门派」一节

**验证输出**（节选）：

```text
$ g++ -std=c++20 -pthread hw5_1_b_ref_bad.cpp -o bad
hw5_1_b_ref_bad.cpp:11:38: error: static assertion failed:
  std::thread arguments must be invocable after conversion to rvalues
   11 |     std::thread t(update_value, value);
...
$ ./hw5_1_b_ref_ok
value after thread = 42
other after lambda copy = 7

$ ./hw5_1_b_dangle_plain        # 普通构建：输出行交错，但值「看起来都对」
worker sees local=worker sees local=100100worker sees local=

100
...
$ ./hw5_1_b_dangle_tsan
WARNING: ThreadSanitizer: data race (pid=786)
  Write of size 4 at 0x7fffffffddc4 by thread T1:
    #0 operator() /tmp/.../hw5_1_b_dangle.cpp:10
  Previous write of size 4 at 0x7fffffffddc4 by main thread:
    #0 spawn_detached() /tmp/.../hw5_1_b_dangle.cpp:7
SUMMARY: ThreadSanitizer: data race /tmp/.../hw5_1_b_dangle.cpp:10 in operator()

$ ./hw5_1_b_dangle_asan
ERROR: AddressSanitizer: stack-use-after-return on address ...
WRITE of size 4 at ... thread T4
  This frame has 5 object(s):
    [48, 52) 'local' (line 7) <== Memory access at offset 48 is inside this variable
SUMMARY: AddressSanitizer: stack-use-after-return ... in operator()
```

## 5.2-A {#hw-5-2-a}

**难度 L2** · 题面见 [homework](10-homework.md#hw-5-2-a)

**思路**：未命名临时对象在语句结尾就析构，锁等于没加；递归锁靠计数放行同线程重入；带超时锁把「等到什么时候为止」说清楚。

1. `bad_push`：8×100000 只得到 778510。**坑就地讲**：g++ 16 会先给一个 `-Wunused-result` 警告（libstdc++ 给 `lock_guard` 构造函数标了 `[[nodiscard]]`）——这是编译器在救你，别无视它；另外如果你写的是 `std::lock_guard<std::mutex>(g_mtx);`（圆括号）会触发 most vexing parse（声明了一个名叫 `g_mtx` 的局部变量），连编译都过不去，所以本答案用花括号临时对象。TSan 对裸 `++g_counter` 报了 2 个 race。→ 知识点：[mutex 与 RAII 锁](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)「std::lock_guard」一节
2. 具名 `lock_guard` 版精确得到 800000。→ 知识点：同上
3. `recursive_mutex` 递归打印 3→0：每次进入计数 +1，逐层析构减到 0 才释放。→ 知识点：同上「std::recursive_mutex」一节
4. `timed_mutex`：空闲时 `try_lock_for(100ms)` 成功；被持有时超时，实测等待 100ms。→ 知识点：同上「std::timed_mutex」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 -pthread hw5_2_a.cpp -o hw5_2_a
hw5_2_a.cpp:13:42: warning: ignoring return value of
  'std::lock_guard<_Mutex>::lock_guard(mutex_type&)', declared with
  attribute 'nodiscard' [-Wunused-result]
   13 |         std::lock_guard<std::mutex>{g_mtx};
$ ./hw5_2_a
bad_push:   expected=800000 actual=778510
good_push:  expected=800000 actual=800000
depth = 3
depth = 2
depth = 1
depth = 0
try_lock_for(100ms) on free mutex: success
try_lock_for(100ms) while held: timeout after 100 ms

$ ./hw5_2_a_bad_tsan     # 只跑 bad_push 的 TSan 构建
WARNING: ThreadSanitizer: data race (pid=337)
  Write of size 4 at 0x555555558220 by thread T3:
    #0 bad_push(int) /tmp/.../hw5_2_a_bad_only.cpp:13
  Previous write of size 4 at 0x555555558220 by thread T1:
    #0 bad_push(int) /tmp/.../hw5_2_a_bad_only.cpp:13
SUMMARY: ThreadSanitizer: data race /tmp/.../hw5_2_a_bad_only.cpp:13 in bad_push(int)
expected=160000 actual=89250
ThreadSanitizer: reported 2 warnings
```

## 5.2-B {#hw-5-2-b}

**难度 L3** · 题面见 [homework](10-homework.md#hw-5-2-b)

**思路**：条件变量不存通知——先通知后等待就是丢。谓词 wait 把「检查条件」变成等待的一部分，两边都防。

1. 坏版本：消费者 200ms 后才进裸 `wait`，生产者 50ms 就通知完了——通知没有任何接收方，消费者永久阻塞，`timeout 5` 后退出码 124。→ 知识点：[condition_variable 与等待语义](../ch02-mutex-condition-sync/03-condition-variable.md)「丢失唤醒：先通知后等待的灾难」一节
2. 谓词版正常退出：`wait(lock, []{ return ready; })` 醒来后**重新检查** `ready` 本身，通知丢了也没关系。虚假唤醒同理——平白醒一次，谓词不过就继续等。→ 知识点：同上「虚假唤醒：为什么 wait 必须配合谓词使用」一节
3. 置位与 notify 之间是否持锁在正确性上没有区别（条件由同一把 mutex 保护才是关键）；`notify_one` 与 `notify_all` 的选择取决于「一次唤醒是否恰好只满足一个等待者」——本场景只有一个消费者，`notify_one` 够。→ 知识点：同上「notify_all 与 notify_one 的选择策略」一节

**验证输出**：

```text
$ timeout 5 ./hw5_2_b_bad; echo exit=$?
producer set ready and notified
consumer entered wait (no predicate)
exit=124

$ timeout 5 ./hw5_2_b_fixed; echo exit=$?
producer set ready and notified
consumer entered predicate wait
consumer woke up, ready=1
consumer joined
exit=0
```

## 5.3-A {#hw-5-3-a}

**难度 L2** · 题面见 [homework](10-homework.md#hw-5-3-a)

**思路**：data race 是 UB，UB 的表现没有义务「每次都错」；TSan 靠插桩追踪 happens-before，它改变时序本身也是它「抓到现行」的一部分。

1. 普通构建连跑 5 次（8 线程）与 3 次（24 线程），本机全部恰好等于期望值——如实贴出。这不是「没有 race」，是竞争窗口没被调度命中；换一台机器、换一次负载就可能丢。→ 知识点：[并发基本问题](../ch00-concurrency-fundamentals/02-concurrency-problems.md)「data race：C++ 标准规定的未定义行为」一节
2. TSan 构建：报 2 个 race（读-写、写-写都在 `++g_counter` 那行），且本次实际值 4000000——插桩改变了时序，竞争窗口被命中，丢了一半。**如实说明**：TSan 版的「丢了数」不是 TSan 的结论，是它扰动时序后的副产物；它给你的真正结论是那份 race 报告。→ 知识点：[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「ThreadSanitizer」一节
3. 三种修复全部精确；CAS 版重试 815134 次（TSan 构建下 7356608 次）。`fetch_add(5)` 打印 `old=10 new=15`——返回的是**修改前**的旧值。→ 知识点：[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)「fetch_add、fetch_sub 与位运算」一节

**验证输出**：

```text
$ for i in 1 2 3 4 5; do ./hw5_3_a_race; done
expected=8000000 actual=8000000
expected=8000000 actual=8000000
expected=8000000 actual=8000000
expected=8000000 actual=8000000
expected=8000000 actual=8000000

$ for i in 1 2 3; do timeout 60 ./hw5_3_a_race 24; done
expected=24000000 actual=24000000
expected=24000000 actual=24000000
expected=24000000 actual=24000000

$ ./hw5_3_a_race_tsan
WARNING: ThreadSanitizer: data race (pid=437)
  Write of size 4 at 0x5555555581d4 by thread T1:
    #0 increment(int) /tmp/.../hw5_3_a_race.cpp:9
  Previous read of size 4 at 0x5555555581d4 by thread T2:
    #0 increment(int) /tmp/.../hw5_3_a_race.cpp:9
SUMMARY: ThreadSanitizer: data race /tmp/.../hw5_3_a_race.cpp:9 in increment(int)
expected=8000000 actual=4000000
ThreadSanitizer: reported 2 warnings

$ ./hw5_3_a_fixed
fetch_add(5): old=10 new=15
atomic:   expected=2000000 actual=2000000
cas:      expected=2000000 actual=2000000 retries=815134
mutex:    expected=2000000 actual=2000000

$ ./hw5_3_a_fixed_tsan
fetch_add(5): old=10 new=15
atomic:   expected=2000000 actual=2000000
cas:      expected=2000000 actual=2000000 retries=7356608
mutex:    expected=2000000 actual=2000000
```

## 5.3-B {#hw-5-3-b}

**难度 L4** · 题面见 [homework](10-homework.md#hw-5-3-b)

**思路**：①relaxed 不建立 happens-before，标志与数据之间没有同步关系；②Dekker 实验是 x86 上**唯一能观察到**的硬件重排（store-load），但能不能命中窗口取决于调度——如实报告。

1. relaxed 消息传递：TSan 在 `g_data` 上报 race（生产者写 vs 消费者读），尽管打印值「恰好是 42」——x86 的 TSO 恰好保住了 store-store 顺序，这是硬件碰巧，不是 C++ 标准给您的保证。改 release/acquire 后，store 与 load 组成 synchronizes-with，TSan 干净。→ 知识点：[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)「memory_order_acquire 与 memory_order_release」一节
2. Dekker 实验（信号量同步起跑、随机延迟，结构同 Preshing 原文）：本机 relaxed 30 万轮分布 `00=0 01=291838 10=7989 11=173`——**00 一次都没出现**。原因有二：x86 的 TSO 只允许 store-load 重排（x、y 落同一缓存行时，后发起的 store 经 RFO 会先看见先发起的 store 已提交的值，两个「同时读 0」的窗口需要两个 store 都还压在 store buffer 里）；且本机信号量唤醒的两个线程起跑有偏差，窗口更难命中。Preshing 在其博客上报告过旧机器上可稳定观察到 00（那是**他的数据**，不是本机复现）。如实结论：x86 上这个实验可能拿不到 00，弱内存序架构（ARM）才是它的主场——但这**不改变** C++ 层面的结论：relaxed 没有排序保证，第一部分 TSan 报告已经证明。seq_cst 版 `00=0` 是**保证**：假设 r1==0 且 r2==0，则线程1 的 load 发生在线程2 的 store 之前、线程2 的 load 发生在线程1 的 store 之前，而各自程序序里 store 又在 load 之前，四条边构成环，与 seq_cst 的全序矛盾——反证成立，00 不可能。→ 知识点：同上「memory_order_seq_cst」一节（「为什么 x86 上 acquire/release 几乎免费」的硬件背景就在这一节：x86 的 TSO 模型本身就很强）；[fence 与编译器屏障](../ch03-atomic-memory-model/03-fence-and-barrier.md)「CPU 屏障：架构相关指令」一节

**验证输出**：

```text
$ ./mo_relaxed_tsan
WARNING: ThreadSanitizer: data race (pid=607)
  Read of size 4 at 0x5555555581d8 by thread T2:
    #0 consumer() /tmp/.../mo_relaxed.cpp:18
  Previous write of size 4 at 0x5555555581d8 by thread T1:
    #0 producer() /tmp/.../mo_relaxed.cpp:10
SUMMARY: ThreadSanitizer: data race /tmp/.../mo_relaxed.cpp:18 in consumer()
consumer sees data = 42
ThreadSanitizer: reported 1 warnings

$ ./mo_fixed_tsan
consumer sees data = 42

$ ./dekker_preshing relaxed 300000
mode=relaxed, rounds=300000
  (r1,r2): 00=0 01=291838 10=7989 11=173

$ ./dekker_preshing seq_cst 300000
mode=seq_cst, rounds=300000
  (r1,r2): 00=0 01=293772 10=6217 11=11
```

## 5.4-A {#hw-5-4-a}

**难度 L3** · 题面见 [homework](10-homework.md#hw-5-4-a)

**思路**：`top()` 和 `pop()` 是两次加锁，中间窗口里别人可以插进来——这是 race condition，即使每一步都线程安全；把「查看并取走」合成一个返回 `optional` 的原子操作，窗口消失。

1. latch 编排后：`top_a=42 top_b=42 pop_a=-1 pop_b=42`——两个消费者都看到栈顶 42，但只有一个能取走，另一个 pop 得 nullopt。若它俩按「先 top 再 pop」编程（比如 top 判空后 pop），第二个消费者就会拿着过期的 42 干活。→ 知识点：[并发基本问题](../ch00-concurrency-fundamentals/02-concurrency-problems.md)「race condition：逻辑层面的竞态」一节
2. 4 生产者 4 消费者共 40000 个互异整数，`std::set` 收集后大小 40000，不丢不重，TSan 干净。→ 知识点：[mutex 与 RAII 锁](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)（锁内完成整个复合操作）；[线程安全队列](../ch04-concurrent-data-structures/01-thread-safe-queue.md)「多生产者多消费者的正确性」一节（MPMC 的正确性靠同一把锁）

**验证输出**：

```text
$ ./hw5_4_a
top_a=42 top_b=42 pop_a=-1 pop_b=42
collected=40000 expected=40000

$ ./hw5_4_a_tsan
top_a=42 top_b=42 pop_a=-1 pop_b=42
collected=40000 expected=40000
```

## 5.4-B {#hw-5-4-b}

**难度 L4** · 题面见 [homework](10-homework.md#hw-5-4-b)

**思路**：Treiber 栈的全部同步都在一个 `head_` 指针上；「无锁」是进度保证不是性能保证——低争用下它常常输给 mutex，这是预期内结果。

1. 正确性：`collected=200000`，CAS 重试 110150 次。→ 知识点：[无锁编程基础](../ch04-concurrent-data-structures/03-lock-free-basics.md)「经典无锁栈」一节
2. 吞吐对比（4 生产者 4 消费者，各 3 轮）：lockfree 16.7/22.6/15.2 ms，mutex 12.7/12.2/12.4 ms——**无锁版反而慢约 40%**。原因如实说：本场景竞争不激烈，mutex 走 futex 快速路径几乎无开销，而无锁版每次 push 一次 `new`（分配器锁）+ 至少一次 CAS；高争用、低延迟敏感场景才是无锁的主场。→ 知识点：同上「何时使用无锁——何时不使用」一节（「lock-free 不意味着更快」）
3. TSan 初版（pop 的 CAS 失败序为 `relaxed`）报 3 个 race，位置都在 `Node* next = old->next;`。根因：CAS 失败时 `old` 被一个 **relaxed** 读更新成当前头节点，随后解引用 `old->next`——这次读没有与写 `next` 的线程建立 happens-before。把失败序改成 `acquire` 后 TSan 干净（`got=80000 sum=3199960000`，即 0..79999 的和）。**如实说明**：这是**真实 data race、不是 TSan 误报**——C++ 抽象机里失败路径确实缺 acquire，TSan 报得对。教材 ch04-03 示例 pop 代码的失败序同样写 `relaxed`、同样踩此坑。初版在 x86 上恰好跑得对只是硬件碰巧；修掉它既是让 TSan 闭嘴、也是把正确性从「硬件碰巧」升级为「标准保证」。（教材正文「TSan 对无锁代码的误报率也不低」出自 ch04-03「不适合使用无锁的场景」小节，说的是无锁代码的团队维护成本这类一般现象，本题不是它的实例。）→ 知识点：同上「ABA 问题」与内存回收的讨论；[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「TSan 的局限」一节
4. 不 delete 弹掉的节点：因为可能有线程已经读出 `old_head` 还没执行 CAS，你一删它就 use-after-free。这一步的工程解法统称**安全内存回收**：hazard pointer、epoch-based reclamation、引用计数。→ 知识点：同上「内存回收：无锁编程里最难的问题」一节

**验证输出**（节选）：

```text
$ ./hw5_4_b
collected=200000 expected=200000 cas_retries=110150
round 1: lockfree=16.712 ms, mutex=12.728 ms, ratio=0.761608
round 2: lockfree=22.579 ms, mutex=12.245 ms, ratio=0.542318
round 3: lockfree=15.168 ms, mutex=12.41 ms, ratio=0.81817

$ ./hw5_4_b_tsan      # 初版（失败序 relaxed）
WARNING: ThreadSanitizer: data race (pid=687)
  Read of size 8 at 0x72040000d5c8 by thread T6:
    #0 LockFreeStack<int>::pop() /tmp/.../hw5_4_b.cpp:34
  Previous write of size 8 at 0x72040000d5c8 by thread T4:
    #0 operator new(unsigned long)
    #1 LockFreeStack<int>::push(int const&) /tmp/.../hw5_4_b.cpp:16
SUMMARY: ThreadSanitizer: data race /tmp/.../hw5_4_b.cpp:34 in LockFreeStack<int>::pop()
ThreadSanitizer: reported 3 warnings

$ ./hw5_4_b_tsan_clean    # 失败序改为 acquire
got=80000 sum=3199960000
```

## 5.5-A {#hw-5-5-a}

**难度 L2** · 题面见 [homework](10-homework.md#hw-5-5-a)

**思路**：deferred 的任务在 `get()` 的调用线程上同步执行；`std::async` 返回的 future 析构会阻塞到任务完成，丢掉返回值 = 变串行。

1. async 版任务跑在独立线程（id 与 main 不同）；deferred 版任务 id 与 main **相同**——`get()` 时才执行。→ 知识点：[std::async 与 future](../ch05-future-task-threadpool/01-std-async-and-future.md)「两种启动策略」一节
2. 三行不保存返回值的 async：总耗时 **900ms**（严格串行，每个临时 future 在语句结尾析构并阻塞到任务完成）；存进 vector 再统一 `get()`：**300ms**（三个任务并行）。→ 知识点：同上「std::async 返回的 future 的析构行为」一节
3. 默认策略 `async|deferred` 下实现有权自行选择——可能在高负载时突然全部 deferred，你的「并行」悄悄变串行；`wait_for` 对 deferred 任务还会直接返回 `deferred` 而非 `timeout`，轮询循环会变死循环。要真异步就显式 `std::launch::async`。→ 知识点：同上「deferred 策略的陷阱」一节

**验证输出**（注意两处细节都是真实发生的：主线程的 `result =` 前缀与任务线程的打印在 stdout 上**互相插入**——两个线程各管各的 `<<`；deferred 版的任务线程 ID 与 main 相同）：

```text
$ ./hw5_5_a
main thread id = 138749809158016
--- std::launch::async ---
  result =   task 1 runs on thread 138749801395904
10
--- std::launch::deferred ---
  [main] f2 created, task not started yet
  result =   task 2 runs on thread 138749809158016     ← 与 main 相同：get() 时同步执行
20
temp futures (3 个): elapsed=900 ms
  task 10 runs on thread 138749801395904
  task 11 runs on thread 138749801395904
  task 12 runs on thread 138749801395904
stored futures (3 个): elapsed=300 ms
```

## 5.5-B {#hw-5-5-b}

**难度 L3** · 题面见 [homework](10-homework.md#hw-5-5-b)

**思路**：promise 是 future 的写端、一次设值；`set_exception` + `current_exception` 让异常对象完整穿越线程边界；`shared_future` 可拷贝、可反复 `get()`。

1. A→B→C 链：`[C] final = 42`。→ 知识点：[promise 与 packaged_task](../ch05-future-task-threadpool/02-promise-and-packaged-task.md)「std::promise\<T>」一节
2. `failing_stage` 里 catch 后用 `set_exception(std::current_exception())`，主线程 `get()` 重新抛出，`[main] caught: negative input`——异常类型与消息完整穿越线程。→ 知识点：同上「set_value()、set_exception() 和 get_future()」一节
3. 发令枪：主线程睡 300ms 后 `set_value(42)`，4 个 runner 全部苏醒并读到 42。`shared_future` 的 `get()` 返回 `const T&`、可并发多次调用；`future` 只能移动、`get()` 一次消耗。→ 知识点：同上「std::shared_future\<T>」一节

**验证输出**：

```text
$ ./hw5_5_b
[C] final = 42
[main] caught: negative input
[main] firing the starting gun
  runner 3 starts with value 42
  runner 1 starts with value 42
  runner 0 starts with value 42
  runner 2 starts with value 42
```

## 5.6-A {#hw-5-6-a}

**难度 L3** · 题面见 [homework](10-homework.md#hw-5-6-a)

**思路**：generator 是协程最经典的形态：`co_yield` 存值挂起、`next()` 恢复推进、异常存进 `exception_ptr` 按需重抛。

1. `squares(8)` 输出 1 4 9 16 25 36 49 64。→ 知识点：[C++20 协程基础](../ch06-async-io-coroutine/02-coroutine-basics.md)「从零实现一个 Generator」一节
2. `risky(15)` 产出 0..6 后抛出 `boom at 7`，主循环 catch 到。→ 知识点：同上「第二步：加入异常处理」一节
3. 局部变量在协程帧（堆）里——跨挂起点的变量被编译器搬进帧，挂起后依然有效；`final_suspend` 必须 `suspend_always`：若返回 `suspend_never`，协程帧在 `final_suspend` 返回后立刻自毁，外部持有的 handle 全变悬垂。→ 知识点：同上「协程的生命周期」一节；[promise_type 与 awaitable](../ch06-async-io-coroutine/03-promise-type-and-awaitable.md)「final_suspend() noexcept」一节

**验证输出**：

```text
$ ./hw5_6_a
squares(8): 1 4 9 16 25 36 49 64
risky(15): 0 1 2 3 4 5 6
caught exception: boom at 7

$ ./hw5_6_a_asan     # ASan+UBSan 构建
squares(8): 1 4 9 16 25 36 49 64
risky(15): 0 1 2 3 4 5 6
caught exception: boom at 7
```

## 5.6-B {#hw-5-6-b}

**难度 L4** · 题面见 [homework](10-homework.md#hw-5-6-b)

**思路**：对称转移把「挂起 A → 恢复 B」从函数调用变成控制流转移，让编译器有机会做尾调用优化——但那是 QoI。

1. `-O2`、深度 100 万：`executed=1000001 done=1`，正常完成——GCC 16 在 `-O2` 下确实把对称转移做成了尾调用，栈深不随链长增长。→ 知识点：[promise_type 与 awaitable](../ch06-async-io-coroutine/03-promise-type-and-awaitable.md)「await_suspend 的三种返回形式」一节
2. `-O0`、同一深度：**Segmentation fault（exit 139）**——栈溢出。没有尾调用优化时，每层「挂起-恢复」都在 C++ 调用栈上堆栈帧。→ 知识点：同上「symmetric transfer 是避免协程栈溢出的关键机制」的警告框
3. 「QoI 而非标准保证」：标准只规定 await_suspend 返回 handle 时发生对称转移，不要求编译器必须尾调用；GCC/Clang 都曾有过 symmetric transfer 仍导致栈溢出的 bug。本题 -O2 与 -O0 的对比就是这句话的直接验证。→ 知识点：同上

**验证输出**：

```text
$ g++ -std=c++20 -O2 -pthread hw5_6_b.cpp -o hw5_6_b_o2
$ ./hw5_6_b_o2 1000000
depth=1000000, executed=1000001, done=1

$ g++ -std=c++20 -O0 -pthread hw5_6_b.cpp -o hw5_6_b_o0
$ ./hw5_6_b_o0 1000000; echo exit=$?
Segmentation fault
exit=139
```

## 5.7-A {#hw-5-7-a}

**难度 L2** · 题面见 [homework](10-homework.md#hw-5-7-a)

**思路**：每个 actor 一个 mailbox + 一个线程，状态全部私有，线程之间只靠消息交互——「不共享内存」的最小形态。

1. 10000 轮乒乓，回复总和 $100010000 = 2×(1+…+10000)$，与 A 端累加一致。→ 知识点：[Actor 模型与消息传递](../ch07-actor-channel/01-actor-model.md)「用 C++ 实现简易 Actor」一节
2. TSan 干净——mailbox 用 mutex+cv 保护，没有共享可变状态。→ 知识点：[condition_variable 与等待语义](../ch02-mutex-condition-sync/03-condition-variable.md)（谓词 wait 的队列模板）
3. 这里消息是**值语义**（int 拷贝）。真实框架里大消息传 `shared_ptr` 以省拷贝——但那是「又绕回共享状态」的折中：接收方必须把它当独占数据用，否则 race 就又回来了。→ 知识点：[Actor 模型与消息传递](../ch07-actor-channel/01-actor-model.md)「Actor 模型的优势与局限」一节

**验证输出**：

```text
$ ./hw5_7_a
rounds=10000 total=100010000

$ ./hw5_7_a_tsan
rounds=10000 total=100010000
```

## 5.7-B {#hw-5-7-b}

**难度 L3** · 题面见 [homework](10-homework.md#hw-5-7-b)

**思路**：`std::visit` 要求 visitor 覆盖 variant 的**所有**备选类型——漏一个就是 hard error，不是 warning。

1. 漏掉 `Stop` 时 g++ 16 报 `no matching function for call to '__invoke(..., Stop&)'` 外加一串 `no type named 'type' in 'struct std::__invoke_result<...>'`——编译器逐一尝试给 `Stop&` 找处理器失败。`switch` 漏 case 只是 warning，`visit` 漏类型直接编译失败。→ 知识点：[Actor 模型与消息传递](../ch07-actor-channel/01-actor-model.md)「消息的模式匹配」一节
2. 补全后状态迁移：`Inc(5) -> counter=5`、`Query -> counter=5`、`Inc(3) -> counter=8`、`Stop`。→ 知识点：同上
3. `[](auto&&)` 通配兜底会吞掉所有未匹配类型，编译器不再帮你查漏——它方便，但代价是「悄悄忽略新消息类型」。→ 知识点：同上（通配处理器是双刃剑）

**验证输出**：

```text
$ g++ -std=c++20 -pthread hw5_7_b_incomplete.cpp -o inc
/usr/include/c++/16/variant:1071:31: error: no matching function for call to
  '__invoke(overload<...>, Stop&)'
...（缺 Stop 处理器，编译失败）

$ ./hw5_7_b
Inc(5) -> counter=5
Query -> counter=5
Inc(3) -> counter=8
Stop
```

## 5.8-A {#hw-5-8-a}

**难度 L2** · 题面见 [homework](10-homework.md#hw-5-8-a)

**思路**：日志的「行完整性」靠一把输出锁；性能靠锁外拼串；`std::cout` 的多线程问题在于字节级交错与无法归属。

1. 4 线程 × 5 行输出每行完整——每行在锁内一次 `<<` 完成。→ 知识点：[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「结构化日志」一节
2. 锁外拼 `ostringstream` 让持锁时间只剩一次输出调用，格式化（可能是重活）不排队。→ 知识点：同上（该节的实现要点）
3. `std::cout` 直出的两个问题：多个 `<<` 之间被其他线程插入导致**字节级交错**（第 5.1-B 题的输出就是活例），以及没有时间戳/线程 ID 时日志**无法归属**。→ 知识点：同上「让 printf 靠谱一点」一节

**验证输出**（节选）：

```text
$ ./hw5_8_a
[959636297085][140090384840384][INFO] thread 0 line 0
[959636344473][140090368054976][INFO] thread 2 line 0
[959636368456][140090359662272][INFO] thread 3 line 0
[959636435464][140090368054976][INFO] thread 2 line 1
...
[959636454853][140090376447680][INFO] thread 1 line 4
```

## 5.8-B {#hw-5-8-b}

**难度 L3** · 题面见 [homework](10-homework.md#hw-5-8-b)

**思路**：①数据竞争走 TSan 五步法；②死锁靠时序窗口复现、靠 scoped_lock 根治，TSan 不管死锁因为它抓的是「无同步的冲突访问」。

1. 计数器 race：普通构建本机得到 400000 整（又没丢，如实贴）；TSan 构建报 2 个 race 且本次实际 327577；改 `fetch_add(relaxed)` 后 TSan 干净。走完的步骤：复现（TSan 构建稳定命中）→ 分类（数据竞争）→ 工具（TSan）→ 修复 → 回归（TSan 零报告）。→ 知识点：[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「系统性诊断流程」一节
2. AB-BA 死锁：两个线程都打印出「锁住了第一把」后卡死，`timeout 3` 退出码 124；`std::scoped_lock` 版一次拿两把锁，正常完成。死锁不是 TSan 的主战场——它的判定基于 happens-before 与锁集，锁序环它不建模；死锁靠 Helgrind 的锁序图、GDB 附加看 `thread apply all bt`（两线程都卡 `__lll_lock_wait`）等手段定位。→ 知识点：[死锁与锁顺序](../ch02-mutex-condition-sync/02-deadlock-and-lock-ordering.md)「经典的两锁反转」一节；[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「死锁的运行时诊断」一节

**验证输出**：

```text
$ ./tsan_flow
expected=400000 actual=400000

$ ./tsan_flow_tsan
WARNING: ThreadSanitizer: data race (pid=736)
  Read of size 4 at 0x7fffffffddfc by thread T2:
    #0 Counter::increment() /tmp/.../tsan_flow.cpp:9
  Previous write of size 4 at 0x7fffffffddfc by thread T1:
    #0 Counter::increment() /tmp/.../tsan_flow.cpp:9
SUMMARY: ThreadSanitizer: data race /tmp/.../tsan_flow.cpp:9 in Counter::increment()
expected=400000 actual=327577
ThreadSanitizer: reported 2 warnings

$ ./tsan_fixed && ./tsan_fixed_tsan
expected=400000 actual=400000
expected=400000 actual=400000

$ timeout 3 ./deadlock_bad; echo exit=$?
task1: locked A, want B
task2: locked B, want A
exit=124

$ ./deadlock_fixed
task1: locked both via scoped_lock
task2: locked both via scoped_lock
done
```

## 5.9-A {#hw-5-9-a}

**难度 L2** · 题面见 [homework](10-homework.md#hw-5-9-a)

**思路**：分区期间「一致性」和「可用性」只能二选一，这是 CAP 的工程直觉；模拟程序把两条路都演一遍。

1. CP：分区中写入被拒（`cannot reach quorum`），恢复后接受。AP：分区中 S1 接受写 200，S2 读旧值 100；`heal()` 同步后读 200。→ 知识点：[从单机并发到分布式](../ch09-distributed-bridge/01-from-concurrent-to-distributed.md)「CAP 定理的工程直觉」一节
2. CP 例子：ZooKeeper/etcd；AP 例子：Cassandra。分布式锁与单机 mutex 的根本区别一个词：**概率**——分布式锁只能提供「大多数情况下」的互斥，进程暂停、时钟漂移都可能打破它；mutex 是确定性的。→ 知识点：同上「分布式锁的本质困境」一节

**验证输出**：

```text
$ ./hw5_9_a
--- CP 路径：分区期间拒绝写入 ---
  [CP] write(42) REJECTED: cannot reach quorum
  [CP] write(42) accepted
--- AP 路径：分区期间接受写入但读到旧值 ---
  [AP] S1 accepted write(200)
  [AP] S2 read during partition: 100
  [AP] S2 read during partition: 100
  [heal] network repaired, S2 synced to S1, S2.data=200
  [AP] S2 read after heal: 200
```

## 5.9-B {#hw-5-9-b}

**难度 L4** · 题面见 [homework](10-homework.md#hw-5-9-b)

**思路**：租约过期的旧持有者仍可能回来写——锁侧无法阻止，只有**资源侧**拿着版本号才能拒绝；这就是 fencing token。

1. 无 fencing：A 干完活写 999，资源终值 999，B 的 100 被覆盖——「错账」的责任在于锁服务允许了过期持有者，而资源没有防御手段。有 fencing：`write_fenced(999, v1)` 因 `v1 < v2` 被拒，终值 100。→ 知识点：[从单机并发到分布式](../ch09-distributed-bridge/01-from-concurrent-to-distributed.md)「分布式锁的本质困境」一节
2. Redis 锁破坏互斥的机制：持有者做长时间 GC 或调度停顿，租约在它「没死只是暂停」期间过期，第二个客户端拿到锁，两个「持有者」同时操作共享资源。fencing token 的兜底在资源侧：每次获取锁都给一个单调递增的版本号，写入时资源只接受**不小于已记录版本**的写入，过期持有者的旧版本写入被拒——即使锁侧完全失守，资源也不会被写坏。→ 知识点：同上（Kleppmann 对 Redlock 的反驳与 fencing token）

**验证输出**：

```text
$ ./hw5_9_b
--- 无 fencing：过期的旧持有者覆盖了新写入 ---
worker A acquired lock, version=1
worker B acquired lock, version=2
worker B wrote 100
worker A (stale) wrote 999
resource = 999  (期望 B 的 100，实际被 A 的 999 覆盖)
--- 有 fencing：过期写入被拒绝 ---
worker A acquired lock, version=3
worker B acquired lock, version=4
worker B wrote 100: accepted
worker A (stale) wrote 999: rejected
resource = 100  (B 的 100 保住了)
```

## 5.C-1 {#hw-5-c-1}

**难度 L3** · 题面见 [homework](10-homework.md#hw-5-c-1)

**思路**：数值积分是「可分块、无依赖」的经典可并行负载；对比三种累加策略，正好验证「每线程局部状态」消除竞争的威力。

1. 三种实现 4×10⁸ 步：serial 653ms、local 113ms（5.78×）、CAS-atomic 125ms（5.22×），π 值三者都是 3.14159265359。既然 C++20 已为浮点特化提供 `fetch_add`（第③问展开），答案顺手补一个 fetch_add 版对照：结果与 CAS 版一致、耗时在噪声内（本机 g++ 三轮 65~84ms，见验证输出末段）。→ 知识点：[为什么需要并发](../ch00-concurrency-fundamentals/01-why-concurrency.md)（可并行负载 + Amdahl）；[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)（分块模式）
2. 精度差异：三者输出到 12 位完全相同——每线程都在做 5×10⁷ 次的双精度累加，舍入误差都被平均掉；真要看差异得算到 10⁻¹⁶ 量级。→ 知识点：[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)「浮点原子操作的注意事项」一节
3. local 比 CAS-atomic 略快（113 vs 125ms）：CAS 版每个线程只在**最后**做一次 CAS 累加，理论上差距不应大——本机差异主要来自 CAS 版的额外负载波动；对照实验里 CAS 与 fetch_add 更是在噪声内（65~84ms）。真正惨的是「每次迭代都 CAS」的写法（答案没有这么干）：全局热点会让所有核排队抢同一缓存行。**坑就地讲**：C++20（P0020）起 `std::atomic<double>` 明确提供 `fetch_add`/`fetch_sub`（教材 ch03-01 正是这么写的），本机 g++ 16 / clang++ 22 `-std=c++20` 实测编译运行正常——第①问已贴对照输出。但 `fetch_add` 不是魔法：多数平台没有硬件原子浮点加法，它内部仍退化为 CAS 循环；且浮点加法不结合，累加结果依赖执行顺序、不可复现，逐位可能与 local 版有末位差异——想绕开这两点，才需要手写 CAS。本机 12 位内无差，如实报告。→ 知识点：同上
4. 两个浮点原子注意点：①多数 CPU 没有硬件原子浮点加法，`fetch_add` 会退化成 CAS 循环；②浮点加法不结合，结果依赖调度顺序，不可复现。→ 知识点：同上

**验证输出**：

```text
$ ./hw5_c_1
constexpr double pi = 3.141592653589793...
steps = 400000000
serial pi   = 3.14159265359 (653 ms)
parallel pi = 3.14159265359 (113 ms)
atomic pi   = 3.14159265359 (125 ms)
speedup(local)  = 5.77876
speedup(atomic) = 5.224

$ # 顺带对照：C++20 浮点特化 fetch_add 版（同一程序 g++ 16 跑三轮，另用 clang++ 22 跑一轮）
$ g++ -std=c++20 -O2 -pthread hw5_c_1_compare.cpp -o hw5_c_1_compare
$ ./hw5_c_1_compare
steps = 400000000, threads = 8
local     pi = 3.141592653590 (77 ms)
cas       pi = 3.141592653590 (65 ms)
fetch_add pi = 3.141592653590 (72 ms)

$ ./hw5_c_1_compare
steps = 400000000, threads = 8
local     pi = 3.141592653590 (68 ms)
cas       pi = 3.141592653590 (77 ms)
fetch_add pi = 3.141592653590 (74 ms)

$ ./hw5_c_1_compare
steps = 400000000, threads = 8
local     pi = 3.141592653590 (84 ms)
cas       pi = 3.141592653590 (72 ms)
fetch_add pi = 3.141592653590 (69 ms)

$ clang++ -std=c++20 -O2 -pthread hw5_c_1_compare.cpp -o hw5_c_1_compare_clang
$ ./hw5_c_1_compare_clang
steps = 400000000, threads = 8
local     pi = 3.141592653590 (65 ms)
cas       pi = 3.141592653590 (70 ms)
fetch_add pi = 3.141592653590 (68 ms)
```

## 5.C-2 {#hw-5-c-2}

**难度 L4** · 题面见 [homework](10-homework.md#hw-5-c-2)

**思路**：直方图三种策略的正确性依据完全不同：局部桶根本无共享、串行就是基准、全局锁靠互斥——性能差距则是锁粒度的直接体现。

1. 三种实现结果完全一致（16 桶逐一相等），耗时 serial 8ms、local(8) 9ms、mutex(8) 1016ms——mutex 版慢 100 倍以上：2000 万次加解锁，每次 `++bins[...]` 的锁开销远大于一次数组自增，且 8 个核在抢同一把锁 + 同一批 16 个桶的缓存行。→ 知识点：[mutex 与 RAII 锁](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)（锁竞争成本）；[并发性能测试与基准](../ch08-debug-testing-perf/02-concurrency-benchmarks.md)「实战：对比不同同步方案」一节
2. 正确性依据：local 版每线程写**自己的** 16 桶（无共享、无需同步），汇总在主线程 join 之后做（join 建立 happens-before）；mutex 版靠锁保证互斥；serial 是基准。→ 知识点：[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)（join 的 happens-before）
3. TSan 干净。**彩蛋复现**：初版 mutex 版 worker 写成 `[&]` 捕获循环变量 `w`，TSan 立刻点名（`Write of size 4 ... by main thread` vs worker 的读，位置正是 `for (int i = w; ...)`）——`w` 是主线程在循环里持续修改的变量，引用捕获 = 数据竞争。改成显式值捕获后干净。→ 知识点：[线程参数与生命周期](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md)「场景 3：引用捕获的 Lambda 陷阱」一节

**验证输出**：

```text
$ ./hw5_c_2
results identical: true
serial: 8 ms
local(8): 9 ms
mutex(8): 1016 ms
bin counts: 1251357 1248510 1250039 1249214 1249808 1248757 1249550 1251115
            1250145 1249604 1250087 1251537 1250444 1250167 1248712 1250954

$ ./hw5_c_2_tsan
results identical: true
serial: 37 ms
local(8): 48 ms
mutex(8): 20132 ms

$ # 彩蛋：mutex 版用 [&] 捕获 w 时，TSan 的真实报告
WARNING: ThreadSanitizer: data race (pid=1058)
  Write of size 4 at 0x7fffffffc82c by main thread:
    #0 parallel_mutex_histogram(...) /tmp/.../hw5_c_2.cpp:54
  Previous read of size 4 at 0x7fffffffc82c by thread T9:
    #0 operator() /tmp/.../hw5_c_2.cpp:56
SUMMARY: ThreadSanitizer: data race /tmp/.../hw5_c_2.cpp:54
```

## 5.C-3 {#hw-5-c-3}

**难度 L5** · 题面见 [homework](10-homework.md#hw-5-c-3)

**思路**：Vyukov 队列把「谁能写哪个格子」编码进每个格子的 `sequence` 号——生产者用 CAS 抢 `enqueue_pos_` 预留槽位，写完把 `sequence` 从 `pos` 发布成 `pos+1`；消费者看 `sequence == dequeue_pos_+1` 才取数，取完发布成 `pos+Capacity+1` 释放槽位。全程无锁、无内存回收问题（数组固定）。

1. 完整实现见下（核心 40 行）。4 生产者 × 10 万 + 4 消费者：`got=400000 sum=79999800000`，与 $\frac{kTotal×(kTotal-1)}{2}$ 完全一致——总和校验同时抓住「丢」（和变小）与「重」（和变大……若重复且不丢则和不变，所以还要靠 `got==kTotal` 兜底，两个断言一起用）。→ 知识点：[SPSC 与 MPMC 队列](../ch04-concurrent-data-structures/04-lock-free-queues.md)「MPSC 队列：多生产者的挑战」一节（sequence 技巧是 MPSC 的推广）；[无锁编程基础](../ch04-concurrent-data-structures/03-lock-free-basics.md)「CAS 循环」一节
2. TSan 与 UBSan 构建均零报告（`-fsanitize=thread -g` 是**编译期**选项，运行不加任何旗标）。→ 知识点：[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)（acquire/release 配对让 TSan 认可这段同步）
3. 两个「0」的差别：`enqueue` 判满时 `dif = seq - pos < 0`，意味着该格子的 `sequence` 还停留在**上一圈**（消费者没释放）；`dequeue` 判空时 `dif = seq - (pos+1) < 0`，意味着 `sequence` 还等于 `pos`（生产者没发布）——`dif == 0` 对 enqueue 是「槽位待我写入」、对 dequeue 是「数据已就绪」，差的 1 正是「发布 +1」的协议。→ 知识点：同上（推导参考 MPSC 的 sequence 协议）
4. 「有界」省掉了内存回收：格子固定、永不 delete，就没有 ABA/悬垂节点的回收问题；Michael-Scott 无界队列每次 enqueue 都要 `new`，弹掉的哨兵节点必须用 hazard pointer / epoch 之类方案回收——有界性是这笔复杂度账的交换物。→ 知识点：[无锁编程基础](../ch04-concurrent-data-structures/03-lock-free-basics.md)「内存回收：无锁编程里最难的问题」一节

**参考实现**（核心部分）：

```cpp
template <typename T, std::size_t Capacity>
class VyukovMpmcQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    VyukovMpmcQueue()
    {
        for (std::size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool enqueue(const T& value)
    {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell* cell = &buffer_[pos & kMask];
            std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            std::intptr_t dif = static_cast<std::intptr_t>(seq)
                - static_cast<std::intptr_t>(pos);
            if (dif == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                    pos, pos + 1, std::memory_order_relaxed)) {
                    cell->data = value;
                    cell->sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false;  // 队列满
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    std::optional<T> dequeue()
    {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell* cell = &buffer_[pos & kMask];
            std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            std::intptr_t dif = static_cast<std::intptr_t>(seq)
                - static_cast<std::intptr_t>(pos + 1);
            if (dif == 0) {
                if (dequeue_pos_.compare_exchange_weak(
                    pos, pos + 1, std::memory_order_relaxed)) {
                    T value = std::move(cell->data);
                    cell->sequence.store(
                        pos + kMask + 1, std::memory_order_release);
                    return value;
                }
            } else if (dif < 0) {
                return std::nullopt;  // 队列空
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct Cell {
        std::atomic<std::size_t> sequence;
        T data;
    };

    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> enqueue_pos_{0};
    alignas(64) std::atomic<std::size_t> dequeue_pos_{0};
    alignas(64) std::array<Cell, Capacity> buffer_;
};
```

**验证输出**：

```text
$ g++ -std=c++20 -O2 -pthread hw5_c_3.cpp -o hw5_c_3
$ ./hw5_c_3
got=400000 expected=400000 sum=79999800000 expected_sum=79999800000

$ g++ -std=c++20 -O1 -g -fsanitize=thread -pthread hw5_c_3.cpp -o hw5_c_3_tsan
$ ./hw5_c_3_tsan
got=400000 expected=400000 sum=79999800000 expected_sum=79999800000

$ g++ -std=c++20 -O1 -g -fsanitize=undefined -pthread hw5_c_3.cpp -o hw5_c_3_ubsan
$ ./hw5_c_3_ubsan
got=400000 expected=400000 sum=79999800000 expected_sum=79999800000
```
