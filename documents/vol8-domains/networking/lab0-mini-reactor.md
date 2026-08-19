---
title: "Lab 0:mini Reactor echo server——从 epoll 到一个能扛并发的事件循环"
description: 在前四篇的 socket/epoll/Reactor 地基上,动手实现一个最小的 Reactor(epoll 事件循环 + handler 注册表),做成能扛并发的 echo server。分 4 个 milestone,每个只引入一个工程问题,验收是对抗性的:多并发不崩、ET 大 burst 不丢数据、stop 不挂死——通不过这些就不是"能跑",是"看着能跑"
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 8
prerequisites:
  - "传统 socket 编程:服务器五步与 TCP 建链"
  - "epoll:Linux I/O 多路复用"
  - "Reactor 模式"
tags:
  - host
  - cpp-modern
  - advanced
  - 网络编程
  - 异步编程
---

# Lab 0:mini Reactor echo server——从 epoll 到一个能扛并发的事件循环

> 这是一个动手 Lab,不是讲概念的教程。前四篇(00→03)我们把 socket、epoll、Reactor 模式都讲过了,这一篇轮到你自己把它们拼成一个能跑的东西。配套工程脚手架在 `code/volumn_codes/vol8-labs/lab0-mini-reactor/`。

## 目标

实现一个最小的 **Reactor**——在 epoll 之上的"事件循环 + handler 注册表",并用它搭一个能同时伺候大量连接的 echo server。整个 Lab 拆成 4 个 milestone,每个 milestone 只引入**一个新的工程问题**:

- **MS1** 事件循环本身怎么转起来、单连接怎么 echo;
- **MS2** 多个并发连接同时来,怎么不出错;
- **MS3** ET 模式下大 burst,怎么不丢数据;
- **MS4** 怎么优雅关闭、不挂死。

重点是:**每个 milestone 的验收都是对抗性的**——不是"echo 能跑",而是"echo 在并发/大 burst/带载关闭下也不崩、不丢、不挂"。网络代码最大的谎就是"看着能跑",这个 Lab 就是要把那些"看着能跑"的实现,在验收里打成不合格。

## 前置知识

- [00 传统 socket 编程](./00-traditional-socket-basics.md)——服务器五步、RAII `UniqueFd`。
- [01 现代 socket 封装](./01-modern-socket-wrapping.md)——`std::expected`、每连接一线程的 C10K 代价(这个 Lab 正是它的解药)。
- [02 epoll](./02-epoll-io-multiplexing.md)——兴趣表/就绪队列、ET vs LT、循环读到 EAGAIN。
- [03 Reactor 模式](./03-reactor-pattern.md)——POSA2 四角色,这个 Lab 实现的就是 Initiation Dispatcher。

## 工程脚手架

`code/volumn_codes/vol8-labs/lab0-mini-reactor/` 给你一套可构建的工程:

```text
include/net/reactor.hpp     # Reactor 的接口(你要实现的)
include/net/unique_fd.hpp   # 复用 01 的 RAII fd
src/reactor.cpp             # ★参考实现(答案)——你的任务是在这里对照接口重写一遍
tests/lab0_tests.cpp        # MS1-4 的 Catch2 对抗性验收
CMakeLists.txt              # Catch2(FetchContent)+ 普通测试 + TSan 测试两套目标
```

**怎么干**:读 `reactor.hpp` 那个接口,在 `src/reactor.cpp` 里自己写实现,然后 `cmake --build`、跑测试,4 个 milestone 的测试逐个变绿。`src/reactor.cpp` 里现在那份是**参考答案**——给你对照思路用的,真要做 Lab 就先把它清空、只留接口,自己从头写。这叫 dogfooding:接口和测试是我给你的"题目",实现是你交的"作业"。

## 最终接口

你要实现的 `Reactor` 类(完整声明在 `reactor.hpp`):

| 成员 | 语义 | 哪个 MS 用到 |
|---|---|---|
| `add(fd, events, handler)` | 注册一个 fd,声明关心的事件(`EPOLLIN`/`EPOLLOUT`/`EPOLLET`...),就绪时调 `handler` | MS1 |
| `modify(fd, events)` | 改已注册 fd 的事件(比如切 LT→ET、加 `EPOLLOUT`) | MS3 |
| `remove(fd)` | 注销 fd(从兴趣表移除 + 删 handler) | MS1(EOF 时) |
| `run()` | 跑事件循环,阻塞直到 `stop()` | MS1 |
| `stop()` | 请求停止(从别的线程/信号 handler 调;要能唤醒阻塞中的 `epoll_wait`) | MS4 |

设计要点:**所有 handler 都在 `run()` 的线程上同步执行**(单线程 Reactor)。这是它"免锁"的根基——同一时刻只有一个 handler 在跑,共享状态不会被并发改。多线程安全只靠 `stop()`(它得能唤醒阻塞中的 `epoll_wait`,这就需要个 eventfd 或 self-pipe 来"戳"一下)。

## Milestone 1:事件循环转起来,echo 一条连接

**目标**:实现 `Reactor` 的核心——`epoll` 实例 + `add`/`run`,注册一个监听 fd,accept 出一条连接、echo 它。

**为什么**:这是整个 Lab 的地基。`run()` 必须是个能阻塞等待、有事件就分发的循环;`add` 必须能把 fd 和 handler 绑在一起存好。这一步通了,后面三个 milestone 都是在这之上加东西。

**实现指引**:

- `epoll_create1(0)` 建 epoll 实例;`add` 里 `epoll_ctl(EPOLL_CTL_ADD)` + 把 handler 存进一个 `unordered_map<int, Handler>`。
- `run()` 是个 `while` 循环,里面 `epoll_wait` 阻塞等事件,拿到事件后按 `fd` 查 map、调对应 handler。
- handler 里 `accept` 出连接后,再 `add` 一个连接 handler(负责 echo)。连接 handler 读到 `0`(EOF)时要 `remove` 自己 + `close`。
- ⚠️ **`run()` 调 handler 前先拷一份**(`Handler h = it->second; h(events);`):连接 handler 在 EOF 时会 `remove` 自己,这会从 map 里擦除**正在执行**的这个 `std::function`,直接调 `it->second(...)` 就是 use-after-free——TSan 一抓一个准。这是 reactor 的经典自删除坑,参考实现 `src/reactor.cpp` 里就是这么处理的(这个 Lab 自己就踩过,所以 MS2 的 TSan 验收不是摆设)。

**验证**(`tests/lab0_tests.cpp` 的 MS1 用例):启动 reactor(放独立线程),客户端连上来发 `"hello-ms1"`,断言读回的 echo 字节数等于发送的。再连第二个,断言也通——**顺序多连接都要对**。

## Milestone 2:并发客户端,全对(且 TSan 干净)

**目标**:16 个客户端**同时**连、同时发,16 条 echo 全对。

**为什么**:MS1 只测了顺序连接。并发一起来,如果你的 handler 注册表、或 per-连接状态有问题(比如 handler 捕获的 fd 错了、或 map 并发改),就会露馅。单线程 Reactor 设计上不该有 data race——所以这一步的验收外加一道 **TSan**:有 race 就红。

**实现指引**:MS1 的实现如果"所有 handler 都在 loop 线程跑、map 只在 loop 线程改",MS2 自然就过了。**别在 handler 里 `std::thread`**——那就退化回 01 的每连接一线程了,而且会和 loop 线程抢 map,TSan 立刻报 race。

**验证**:MS2 用例开 16 个客户端线程并发 echo,断言全部成功;**TSan 版测试**(`lab0_tests_tsan`)跑同一份用例,断言无 race 报告。这一步真正抓的是"看着并发能跑、其实有隐藏 race"的实现——TSan 就是来揭穿它的。

## Milestone 3(对抗性):ET 模式 + 大 burst,一字节都不能少

**目标**:连接注册成 `EPOLLET | EPOLLIN`,客户端一次性发 100KB,断言 echo 回来**正好 100KB**。

**为什么**:这是整个 Lab 的"别被测试骗了"名场面,直接对应 [02 篇那个"ET-read-once 丢 87KB"的坑](./02-epoll-io-multiplexing.md)。ET 只在"有新数据到达"这个边沿通知一次;你的 handler 如果只 `read` 一次,剩下的数据就卡在 socket 缓冲区里,ET 再也不通知——测试用小消息(4KB)根本测不出来,非得上 100KB 的大 burst 才能把这个 bug 揪出来。

**实现指引**:

- `add` 时给连接 `EPOLLIN | EPOLLET`;连接 fd 必须 `O_NONBLOCK`。
- handler 收到事件后,**必须 `for(;;)` 循环 `read`,直到返回 `-1` 且 `errno == EAGAIN`** 才结束这次处理——把缓冲区彻底读空。
- 读到的每段循环 `write` 回去(write 也可能短写/`EAGAIN`)。

**验证**:MS3 用例发 100KB、读 echo(带 3s 超时),`REQUIRE(got == 100000)`。**一字节都不能少**——这就是对抗性验收。漏了循环读、或忘了非阻塞,这个数字就到不了 10 万。

## Milestone 4:优雅关闭,`stop()` 不挂死

**目标**:从别的线程调 `stop()`,断言 `run()` 在 2 秒内返回(不挂)。

**为什么**:`run()` 阻塞在 `epoll_wait(-1)` 上(永久等待)。如果 `stop()` 只是设个 `stop_` 标志,`epoll_wait` 根本不知道——它会继续阻塞,`run()` 永远不返回,你的 join 就挂死了。这是 reactor 类最容易踩的关闭坑(旧笔记里就有"accept 阻塞导致 join 挂起"的真 bug)。

**实现指引**:

- 建一个 `eventfd`(或 self-pipe),`add` 进 epoll。
- `stop()` 里:设 `stop_` 标志 + 往 eventfd 写一字节——这一写会立刻唤醒阻塞中的 `epoll_wait`。
- `run()` 醒来发现 eventfd 可读(或检查 `stop_`),退出循环。

**验证**:MS4 用例把 `run()` 放进 `std::async`,先连一个客户端(模拟"带载"),再 `stop()`,用 `future::wait_for(2s)` 断言状态是 `ready`——**2 秒内必须返回**。挂死就红。

## 性能测试(选做)

Lab 跑通后,可以和 `code/volumn_codes/vol8/networking/01-modern-socket/`(01 的每连接一线程 server)做个对照:同样开 2000 个空闲连接,看你的 reactor server 的 `VmSize`/`Threads` 涨多少。预期:Threads 应该基本不涨(就 loop 那一个线程),`VmSize` 远不到 01 的 24GB——这就是"少量线程服务大量连接"的实证。数据自己跑、自己贴,别照抄。

## 扩展练习(bonus,非主线)

- **定时器**:用 `timerfd` 注册进 reactor,实现一个 `call_after(duration, fn)`。提示:timerfd 也是个 fd,read 它就清掉定时。
- **EPOLLONESHOT**:连接注册 `EPOLLONESHOT`,处理后要 `modify` 重新挂上——理解它和普通 ET 的区别(为什么多线程 reactor 需要 oneshot)。
- **多线程 reactor**:开 N 个 worker 线程跑同一个 `io_context`,用 `strand` 保证同一连接的 handler 不并发——这就摸到 Boost.Asio 的门槛了。

## 自查清单

- [ ] MS1:单连接 + 顺序多连接 echo 全对?
- [ ] MS2:16 并发 echo 全对,且 TSan 版**无 race**?
- [ ] MS3:ET + 100KB burst,echo **正好 100000** 字节?(循环读到 EAGAIN、fd 非阻塞)
- [ ] MS4:`stop()` 后 `run()` 2 秒内返回?(eventfd 唤醒)
- [ ] handler 全在 loop 线程跑,没在 handler 里 spawn 线程?
- [ ] 连接 EOF 时 `remove` + `close` 了?没漏 fd?

## 参考资源

- [man 2 epoll_create1](https://man7.org/linux/man-pages/man2/epoll_create1.2.html) / [epoll_ctl](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html) / [epoll_wait](https://man7.org/linux/man-pages/man2/epoll_wait.2.html)
- [man 2 eventfd](https://man7.org/linux/man-pages/man2/eventfd.2.html) —— MS4 用来唤醒阻塞中的 `epoll_wait`
- [Catch2](https://github.com/catchorg/Catch2) —— 本 Lab 的测试框架
- [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) —— MS2 抓隐藏 data race 的工具
- [Reactor 模式(本系列 03)](./03-reactor-pattern.md) —— 本 Lab 实现的设计模式
- [epoll(本系列 02)](./02-epoll-io-multiplexing.md) —— MS3 的 ET 坑在那里有完整复现
