---
id: "204"
title: "卷五：并发编程全史 — 全部章节大纲与文章规划"
category: content
priority: P2
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["200", "201"]
blocks: []
estimated_effort: large
---

# 卷五：并发编程全史 — 全部章节大纲与文章规划

## 总览

- **卷名**：vol5-concurrency
- **难度范围**：beginner → intermediate → advanced
- **预计文章数**：35 篇核心文章 + 2 篇分布式桥接附录
- **前置知识**：卷一 + 卷二（RAII、移动语义、lambda、智能指针、错误处理）
- **C++ 标准覆盖**：C++11-23，C++26 execution/senders 只作为前沿展望
- **目录位置**：`documents/vol5-concurrency/`
- **代码位置建议**：`code/volumn_codes/vol5/concurrency/`
- **内容定位**：标准并发教程 + 工程并发课 + 异步系统入门

## 课程定位

卷五不只是讲 `std::thread`、`std::mutex`、`std::atomic` 这些 API。它要帮助读者建立完整的并发判断力：

1. **先正确性，再性能**：先理解 data race、happens-before、生命周期、取消与关闭，再谈无锁和扩展性。
2. **先锁，再无锁**：大多数工程问题先用清晰的锁、队列和任务模型解决，无锁内容强调边界和风险。
3. **先同步，再任务**：线程原语是基础，线程池、executor、actor、协程才是大型系统真正的组织方式。
4. **先本地并发，再异步系统**：先掌握单进程多线程，再理解异步 I/O、Actor、RPC 和一致性问题。

## 学习路径

### 入门路径

适合第一次系统学习 C++ 并发的读者：

- ch00：并发问题与思维方式
- ch01：线程生命周期与 RAII
- ch02：mutex、condition_variable、shared_mutex
- ch05：future、promise、jthread、停止令牌

### 工程路径

适合已经会写多线程代码，但希望写得稳的读者：

- ch02：锁粒度、死锁、条件变量谓词
- ch04：线程安全队列、缓存、并发容器
- ch05：线程池、任务模型、backpressure、graceful shutdown
- ch08：ThreadSanitizer、benchmark、死锁与性能分析

### 高阶路径

适合希望理解内存模型、无锁、协程和工业库设计的读者：

- ch03：原子操作、内存序、fence、atomic_wait
- ch04：SPSC/MPMC 队列、ABA、内存回收
- ch06：Boost.Asio、协程异步 I/O、libuv 对照
- ch07：Actor、Channel、消息传递
- 卷九：工业级并发源码专题

## 文章写作规则

每篇文章尽量保持三层结构：

1. **标准语义**：C++ 标准库提供了什么保证，不提供什么保证。
2. **实现直觉**：它可能如何映射到 OS、CPU、cache、futex、事件循环或任务队列。
3. **工程规则**：什么时候该用，什么时候不该用，如何测试，常见坑在哪里。

### 工业库案例盒

卷五保留少量工业库轻量案例，但不做完整源码深挖。每个案例盒放在相关章节末尾，控制为一个小节。

案例盒固定回答四个问题：

1. 这个库解决什么问题？
2. 它对应本章的哪些并发知识？
3. 最小使用方式或伪代码长什么样？
4. 想继续深挖应该去卷九读哪篇？

案例盒不逐行讲源码，不要求读者提前掌握整个库。

## 章节大纲

### ch00：并发思维与硬件/OS 基础

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 00-01 | 01-why-concurrency.md | 为什么需要并发 | 并发 vs 并行、吞吐量 vs 延迟、Amdahl/Gustafson 定律、任务粒度 | 分析并发收益与代价 |
| 00-02 | 02-concurrency-problems.md | 并发基本问题 | data race、race condition、死锁、活锁、饥饿、优先级反转 | 识别并发 bug |
| 00-03 | 03-cpu-cache-and-os-threads.md | CPU cache 与 OS 线程 | cache line、MESI、false sharing、上下文切换、线程调度 | false sharing 小基准 |

### ch01：线程生命周期与 RAII

- **预计篇数**：4

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 01-01 | 01-std-thread.md | std::thread 基础 | 创建、join、detach、线程 ID、硬件并发数、线程函数异常 | 多线程任务拆分 |
| 01-02 | 02-thread-arguments-and-lifetime.md | 线程参数与生命周期 | 值传递、引用传递、移动对象、悬垂引用、对象析构顺序 | 修复生命周期 bug |
| 01-03 | 03-thread-ownership-raii.md | 线程所有权与 RAII | thread guard、joining_thread、异常安全、作用域退出自动 join | 实现 RAII thread wrapper |
| 01-04 | 04-thread-local-and-once.md | thread_local 与 call_once | 线程局部存储、懒初始化、`once_flag`、Meyers singleton | 线程安全初始化 |

### ch02：锁、条件变量与同步原语

- **预计篇数**：5

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 02-01 | 01-mutex-lock.md | mutex 与 RAII 锁 | mutex、recursive_mutex、timed_mutex、lock_guard、unique_lock、scoped_lock | 线程安全临界区 |
| 02-02 | 02-deadlock-and-lock-ordering.md | 死锁与锁顺序 | 死锁四条件、锁顺序、try_lock、scoped_lock 多锁获取 | 复现并修复死锁 |
| 02-03 | 03-condition-variable.md | condition_variable | wait/notify、虚假唤醒、谓词、丢失唤醒、超时等待 | bounded queue |
| 02-04 | 04-shared-mutex.md | 读写锁与 shared_mutex | 读多写少场景、shared_lock、写者饥饿、性能边界 | 并发缓存 |
| 02-05 | 05-latch-barrier-semaphore.md | C++20 同步原语 | latch、barrier、counting_semaphore、binary_semaphore、适用场景 | 阶段同步与限流 |

### ch03：原子操作与 C++ 内存模型

- **预计篇数**：5

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 03-01 | 01-atomic-operations.md | atomic 操作 | atomic<T>、load/store、fetch_add、compare_exchange、is_lock_free | 无锁计数器 |
| 03-02 | 02-memory-ordering.md | 内存序详解 | relaxed、acquire/release、acq_rel、seq_cst、happens-before | message passing |
| 03-03 | 03-fence-and-barrier.md | fence 与屏障 | atomic_thread_fence、compiler barrier、CPU barrier、volatile 边界 | 屏障应用辨析 |
| 03-04 | 04-atomic-wait-and-ref.md | atomic_wait 与 atomic_ref | C++20 atomic_wait/notify、atomic_ref、轻量同步信号 | 原子信号量 |
| 03-05 | 05-atomic-patterns.md | 原子操作模式 | SeqLock、Double-Checked Locking、引用计数、发布订阅 | 原子模式实现 |

### ch04：并发数据结构

- **预计篇数**：4

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 04-01 | 01-thread-safe-queue.md | 线程安全队列 | mutex + condition_variable、close、timeout、stop_token、backpressure | bounded blocking queue |
| 04-02 | 02-thread-safe-containers.md | 线程安全容器设计 | 粗粒度锁、细粒度锁、分片锁、copy-on-write、接口原子性 | 并发 map/cache |
| 04-03 | 03-lock-free-basics.md | 无锁编程基础 | CAS 循环、lock-free vs wait-free、ABA、内存回收问题 | 无锁栈实验 |
| 04-04 | 04-lock-free-queues.md | SPSC 与 MPMC 队列 | ring buffer、Michael-Scott queue、生产者消费者队列、缓存友好布局 | SPSC queue 与 MPMC 认知 |

### ch05：future、任务模型与线程池

- **预计篇数**：5

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 05-01 | 01-std-async-future.md | std::async 与 future | launch policy、future.get、异常传播、deferred 陷阱 | 异步任务 |
| 05-02 | 02-promise-packaged-task.md | promise 与 packaged_task | 值传递、异常传递、shared_future、任务封装 | 值传递链 |
| 05-03 | 03-jthread-stop-token.md | jthread 与停止令牌 | 自动 join、stop_source、stop_token、stop_callback、协作式取消 | 可中断 worker |
| 05-04 | 04-thread-pool.md | 线程池设计 | worker 生命周期、任务队列、future 返回、shutdown、异常安全 | mini thread pool |
| 05-05 | 05-executor-and-task-system.md | Executor 与任务系统 | executor 思维、任务粒度、优先级、延迟任务、C++26 senders 展望 | 任务调度器设计 |

### ch06：异步 I/O 与协程

- **预计篇数**：5

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 06-01 | 01-async-io-models.md | 异步 I/O 模型 | blocking vs non-blocking、Reactor、Proactor、事件循环、回调地狱 | 模型对比 |
| 06-02 | 02-boost-asio-basics.md | Boost.Asio 基础 | io_context、executor、timer、work guard、错误处理 | 异步定时器 |
| 06-03 | 03-asio-networking.md | Asio 网络编程 | TCP/UDP 异步、buffer 管理、连接生命周期、timeout | 异步 Echo 服务 |
| 06-04 | 04-coroutine-asio.md | 协程 + Asio | co_await、awaitable、co_spawn、取消、异常传播 | 协程 HTTP 服务 |
| 06-05 | 05-libuv-comparison.md | libuv 对照案例 | event loop、handle/request、异步文件与网络、与 Asio 的设计差异 | libuv 小实验 |

### ch07：Actor、Channel 与消息传递

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 07-01 | 01-actor-and-channel-basics.md | Actor 与 Channel 基础 | 消息传递、无共享、mailbox、channel、CSP、backpressure | channel 实现 |
| 07-02 | 02-mini-actor-runtime.md | Mini Actor Runtime | actor 生命周期、消息路由、supervision、错误处理、优雅停止 | actor 聊天室/爬虫调度器 |

### ch08：并发调试、测试与性能分析

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 08-01 | 01-thread-sanitizer-and-debugging.md | ThreadSanitizer 与并发调试 | TSan、死锁复现、随机调度、日志与断言、最小复现 | 定位 data race |
| 08-02 | 02-concurrency-benchmarking.md | 并发性能分析 | Google Benchmark、perf、锁竞争、cache miss、false sharing、可扩展性曲线 | benchmark 报告 |

### ch09：分布式并发桥接附录

- **预计篇数**：2
- **定位**：帮助读者从单进程并发过渡到分布式系统，不抢占卷五主线。

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 09-01 | 01-rpc-basics.md | RPC 与服务端并发 | gRPC 概述、Protocol Buffers、同步/异步调用、线程池服务端 | gRPC 服务 |
| 09-02 | 02-consistency-and-failure.md | 一致性与失败模型 | CAP、最终一致性、重试、幂等、超时、取消、共识算法认知 | 分布式认知 |

## 工业库案例盒分配

| 所在章节 | 案例库 | 只讲什么 | 不讲什么 | 后续深挖 |
|----------|--------|----------|----------|----------|
| ch04 并发数据结构 | moodycamel concurrentqueue | MPMC queue 的使用场景、producer token、缓存友好布局动机 | 不逐行讲队列内部算法 | 卷九 `concurrency/04-moodycamel-concurrentqueue-mpmc.md` |
| ch05 任务模型与线程池 | Chromium base | `PostTask`、Sequence、TaskRunner 的设计画面 | 不拆 `ThreadPoolImpl` 源码细节 | 卷九 `concurrency/02-chromium-base-task-sequence-threadpool.md` |
| ch05 任务模型与线程池 | Folly / oneTBB | executor、future chaining、work stealing 的工程形态 | 不比较所有 API，不做源码展开 | 卷九 `concurrency/03-folly-onetbb-executor-scheduler.md` |
| ch06 异步 I/O 与协程 | Boost.Asio | `io_context`、异步 timer、TCP echo、协程接入 | 不拆 composed operation 细节 | 卷九 `concurrency/01-boost-asio-io-context-executor.md` |
| ch06 异步 I/O 与协程 | libuv | event loop 与 Asio 的模型对照 | 不做完整 libuv 源码分析 | 卷九并发专题或后续网络专题 |

## 后续源码阅读路线

| 学完卷五内容 | 推荐继续阅读 | 阅读目标 |
|--------------|--------------|----------|
| 标准同步原语、jthread、线程池 | Chromium base synchronization/task | 看大型客户端如何用 Sequence 和 TaskRunner 降低共享状态复杂度 |
| 异步 I/O、协程、取消 | Boost.Asio / libuv | 看事件循环、executor、异步操作生命周期如何组织 |
| future、executor、任务调度 | Folly / oneTBB | 看服务端基础库和任务并行库如何表达调度与组合 |
| 无锁队列、内存模型、cache line | moodycamel concurrentqueue | 看生产级 MPMC queue 如何围绕缓存、分配和 producer 组织结构 |

## 练习与项目

### 文章末尾练习

- 每篇 3-5 道，优先关注并发正确性、生命周期、安全关闭和性能验证。
- 每个核心主题至少有一个“错误版本 → 修复版本”的练习。
- 内存模型与无锁章节必须配可运行的小实验，但明确标注“教学实现，不建议直接生产使用”。

### 小实验清单

1. **data race 复现**：普通 `int` 计数器 vs `atomic<int>` vs `mutex`。
2. **deadlock 复现与修复**：相反锁顺序 vs `std::scoped_lock`。
3. **false sharing benchmark**：`alignas(64)` 前后性能对比。
4. **condition_variable 生产者消费者**：虚假唤醒与谓词写法。
5. **atomic message passing**：release/acquire 与 relaxed 对比。
6. **atomic_wait 信号**：避免忙等的轻量同步。
7. **jthread graceful shutdown**：stop_token 贯穿 worker 循环。
8. **TSan 找 bug**：故意写一个 data race 并修复。

### 实战项目

| 项目 | 覆盖知识点 | 交付物 | 验证方式 |
|------|------------|--------|----------|
| bounded blocking queue | mutex、condition_variable、close、timeout、stop_token、backpressure | 可复用队列类 + 单元测试 | 多生产者/多消费者测试，关闭语义测试 |
| mini thread pool | 任务队列、worker 生命周期、future、异常传播、shutdown | 固定线程池 + 示例程序 | 功能测试 + benchmark 任务粒度 |
| 并发日志系统 | MPSC、batch flush、后台 I/O、丢弃策略、优雅停止 | 异步 logger | 压测吞吐量与丢日志策略 |
| TTL 并发缓存 | shared_mutex、分片锁、后台清理线程 | 并发 cache | 读多写少 benchmark |
| SPSC/MPMC queue lab | ring buffer、CAS、ABA、内存序、缓存布局 | 教学队列实现 | TSan + 压测 + 与标准队列对比 |
| 协程 HTTP server | Boost.Asio、co_await、timeout、取消、连接生命周期 | 简化 HTTP 服务 | 并发连接测试 |
| mini actor runtime | mailbox、message dispatch、supervision、backpressure | Actor runtime + demo | 消息顺序与关闭测试 |

## 现有内容映射

| 现有文章 | 重写去向 | 备注 |
|----------|---------|------|
| `documents/vol5-concurrency/01-atomic.md` | ch03-01/ch03-04 | 拆成原子基础与 C++20 atomic_wait/atomic_ref |
| `documents/vol5-concurrency/02-memory-order.md` | ch03-02/ch03-03 | 强化 happens-before、fence、volatile 边界 |
| `documents/vol5-concurrency/03-lock-free-data-structures.md` | ch04-03/ch04-04 | 拆成无锁基础与队列专题 |
| `documents/vol5-concurrency/04-mutex-and-raii-guards.md` | ch02-01/ch02-02 | 加入死锁、锁顺序和工程规则 |
| `documents/vol5-concurrency/06-critical-section-protection.md` | ch02/ch03 | 融入锁、原子和临界区保护讨论 |
| `documents/vol5-concurrency/03-coroutine-echo-server.md` | ch06-03/ch06-04 | 融入 Asio 网络与协程 HTTP server |
| `documents/cpp-reference/concurrency/*` | 每章速查卡片 | 作为 API 速查交叉链接 |
| `core-embedded-cpp/ch10/*` | ch01-ch04 | 通用化重写，嵌入式特例转为补充说明 |
| `cpp-features/coroutines/03-echo-server.md` | ch06 | 融入协程 + Asio |

## 验收标准

- 卷五读者能从零开始写出安全的多线程程序，知道 `detach`、悬垂引用、虚假唤醒、data race 为什么危险。
- 工程读者能实现 bounded queue、thread pool、并发缓存、异步 logger，并能用 TSan 和 benchmark 验证。
- 高阶读者能理解 acquire/release、CAS 循环、ABA、SPSC/MPMC 队列和协程异步 I/O 的关键边界。
- 所有工业库案例盒只做轻量导览，源码细节统一引导到卷九开源项目学习。
