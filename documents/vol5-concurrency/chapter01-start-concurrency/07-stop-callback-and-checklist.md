---
title: "注册竞态与退出检查表：停止协议的细节"
description: "stop_callback 注册与请求互抢时回调可能就地执行，销毁后的回调另有说法，最后给一张逐条检查退出路径的清单。"
chapter: 5
order: 7
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 注册竞态与退出检查表：停止协议的细节

三件套的件数上一篇数完了，这一篇管两件细账：回调的注册时机与竞态，以及开线程之前值得逐条过的退出检查表。

## stop_callback 的注册时机与竞态

注册回调有两条路径，标准写得很清楚。注册那一刻 `stop_requested()` 还是 false，回调进列表，请求到来时执行。注册那一刻它已经是 true，回调立即在注册线程上执行，不进列表。上一篇《让工作有序结束》讲三件套时，最后注册的那个回调（代码里叫 late）走的就是后一条。

注册和请求抢起来会怎样？回调对象注销之后请求才到又会怎样？咱们把 A、B、C 三种情况放进一个程序里跑：

<OnlineCompilerDemo
  title="stop_callback 的注册竞态"
  source-path="code/examples/vol5/chapter01/45_registration_race.cpp"
  description="注册与请求互抢时回调可能就地执行；在线沙箱限线程数，收窄到 50 轮，正文读数来自本地 2000 轮实测。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
A: 2000 rounds, 1999 invocations
B: live callback at request -> 2000/2000
C: request after deregistration -> 0/2000
```

咱们看输出，注意这是其中一次运行的读数。A 是注册与请求互抢，回调对象活到工作函数返回前。B 是回调对象活到请求之后。C 是回调对象在请求之前注销。B 说明规则的一头，回调对象活着，请求一到它就执行。C 说明另一头，请求来晚一步，回调对象已经没了，一次都不跑。

A 的数字是抢出来的，跟着调度走。反复跑，A 落在 1997 到 2000 之间，负载更重时还会更低，所以咱们把它当观察，不当证明。只要回调对象没被提前注销，注册与请求谁快谁慢，只决定那一次执行发生在哪里。请求早于注册，回调在构造时执行一次。注册早于请求、对象又没注销，请求到达时执行一次。注销早于请求，零次。把 A、B、C 放在一起，三种结局就齐了。

同步关系在这里有个实际用处。注册与那次回调的执行之间有同步，所以注册之前写好的数据，回调里看得见。`request_stop()` 返回 true 与 `stop_requested()` 返回 true 之间同样有同步，所以请求之前写好的数据，看见请求的线程也看得见。原子标志在 relaxed 语义下只保证这个 bool 自己的读写，别处的数据要可见，得靠内存序约定。默认的 seq_cst 读写本身带同步。停止令牌这边不用咱们操心，可见性跟着协议走。

销毁的细则要单独看。`std::stop_callback` 析构时注销自己。注销那一刻回调正在另一条线程上执行，析构就阻塞到它返回（[stoptoken.concepts] 的注销条款，C++20 起）。如果回调就在当前线程上执行，也就是回调里销毁自己的回调对象，析构不阻塞。咱们搭一个场景看第一种情形：回调正在另一条线程上执行，回调对象在 worker 手里析构。

<OnlineCompilerDemo
  title="回调里的活儿阻塞 request_stop"
  source-path="code/examples/vol5/chapter01/46_callback_blocks_request.cpp"
  description="回调睡 200 毫秒，request_stop 就等 200 毫秒才返回——回调跑在请求方的线程上。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
[main] request_stop at 20 ms
  [callback] starts at 20 ms
[worker] leaving the inner scope at 21 ms
  [callback] ends at 222 ms
[main] request_stop returned at 222 ms
[worker] past the destructor at 222 ms
```

worker 在 21 毫秒离开内层作用域，析构那个回调对象。咱们看它打印 “past the destructor” 的时刻，却是 222 毫秒。中间那 200 毫秒，主线程正在回调里睡。worker 的析构等到回调返回才继续往下走，主线程的 `request_stop()` 也一起被占满了 200 毫秒。两头都被这次回调拴住，因为回调跑在主线程上，回调对象却死在工作线程上。

咱们能带走两句话。回调要短，它跑在 `request_stop()` 的调用者身上，长一点就把对方的关停流程拖慢，还可能连带拖住别的线程销毁回调对象。回调对象放在工作线程循环外面，一次注册管整段工作。放进循环里反复注册注销，每次都要动共享的停止状态，纯属白花时间。

阻塞中的等待，标准里有一处现成支持。`std::condition_variable_any` 的 wait 有一组带 `std::stop_token` 的重载。它比 `condition_variable` 多出来的能力，一是 wait 能配任意锁类型，二是带 stop_token 的重载只有它有。这次调用期间，它把自己登记到停止请求的通知关系上，请求到达时等待立即返回（[thread.condvarany.intwait]，C++20 起）。咱们用它做一个能停的任务队列：

<OnlineCompilerDemo
  title="TaskQueue：等待收停止令牌"
  source-path="code/examples/vol5/chapter01/47_taskqueue_stop_cv.cpp"
  description="condition_variable_any 的带 token 重载，停止请求一到，睡在队列上的 worker 立刻醒。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
[worker] processed 0
[worker] processed 1
[worker] processed 2
[main] requesting stop
[main] request_stop returned
[worker] wait returned false, stopping
```

请求到达，worker 的 wait 立刻返回 false，线程醒来打印退出。没有超时，没有轮询，也不用自己写 notify。这里的 false 仍然只说明谓词为假，队列空了。是不是收到了停止请求，要看外层循环条件。您别把返回值当成停止标记。wait 这组函数的返回值只报告谓词真假，不报告被谁叫醒，这一点跟前面 `wait_for` 那次观察是同一个道理。

这组重载只有 `condition_variable_any` 有，`condition_variable` 没有。拿 `std::condition_variable` 去写这段等待，会得到一条找不到匹配函数的报错，咱们在 GCC 16.2.1 上看到的头几行是这样的：

```text
error: no matching function for call to
'std::condition_variable::wait(std::unique_lock<std::mutex>&,
 std::stop_token&, main()::<lambda()>)'
note: candidate 1: 'template<class _Predicate> void
 std::condition_variable::wait(std::unique_lock<std::mutex>&, _Predicate)'
note: candidate expects 2 arguments, 3 provided
```

手里只有 `condition_variable` 的场合，咱们自己注册一个 `stop_callback` 去叫醒等待也能做到。回调负责叫醒，谓词把 `std::stop_token` 的 `stop_requested()` 算进去，让等待返回。`condition_variable::wait` 没有返回值，等待返回之后，循环条件再看 token。等待与通知的细节归后面讲等待的单元。阻塞等待要能被停止请求叫醒，才用得上它。没有它，等待只能靠超时或外部事件结束。

## 逐条检查退出路径

第一单元问题链的最后一项是让工作有序结束，工具到这里齐了，还差一张检查表。下面每一条都是我写并发程序时问过自己的问题，您开新线程时照着过一遍，比事后查死锁便宜。

1. 退出条件写在哪一行？循环条件里有没有 `std::stop_token` 的 `stop_requested()`，循环体内部有没有检查点？一次迭代要跑十秒的循环，只在循环头检查，请求来了您还得等十秒。
2. 阻塞中的等待靠什么醒来？`std::condition_variable_any` 有带 `std::stop_token` 的重载。阻塞在文件描述符或网络上的读没有，标准库不替您管，您得靠关闭描述符、超时或者非阻塞设计自己安排。
3. 停止时队列里剩下的东西怎么办？丢掉、做完、交回调用者，您选一种，代码里得有一句明确的话。
4. 谁等谁，顺序是什么？jthread 的析构会等。成员的声明顺序决定析构顺序，外层对象析构时，它管的线程必须已经能退出。
5. 异常路径走不走同一条退出路线？worker 里抛出的异常逃不出去，这一点《第一个 std::thread 程序》验证过，停止清理时再抛就是 terminate。
6. 回调里做的事有多长？它跑在 `request_stop()` 的调用者线程上，长一点就拖慢对方的关停。
7. 停止之后对象还能用吗？请求单向不可撤回，再往队列里提交任务（比如前面 `TaskQueue` 的 `submit`）没人接，接口上您要么拒绝，要么说清楚。

第 3 条值得跑一遍，因为很多人以为停止请求会自动清空队列。咱们拿 10 个任务试：

<OnlineCompilerDemo
  title="停止时清点剩余任务"
  source-path="code/examples/vol5/chapter01/48_taskqueue_drain_or_drop.cpp"
  description="stop_requested 之后清点队列里还剩多少任务，丢弃并报告——收场路径要自己交代。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
[worker] ran task 0
[worker] ran task 1
[main] request_stop
[worker] stop requested, 8 tasks left in the queue, dropping them
```

主线程 60 毫秒时请求停止，worker 做完手里那个任务才发现循环条件不成立，此时队列里还剩 8 个。代码里那句 `q_ = {}` 就是我做的决定：丢掉。换成做完再退，把清空动作写在 break 之后逐个执行。换成交回调用者，就把队列移出去。咱们三种都行，写清楚就行。不写，就是让下一个维护的人猜。

咱们把本篇和前几篇的工具摆在一起对照，差别在看谁负责收尾：

| 工具 | 析构时做什么 | 停止请求 | 适用场合 |
|------|--------------|----------|----------|
| `std::thread` | joinable 就调用 `std::terminate()` | 没有 | 收尾完全由您控制，一处不漏 |
| 手写 `JoiningThread`（见《谁负责线程的生命周期》） | 只 join | 没有 | C++11 到 C++17，任务会自己结束 |
| `std::jthread` | 调用 `request_stop()`，然后 join | 有，析构里自动发 | C++20，任务要能被叫停 |
| stop_source 加 `std::thread` | 手动 `request_stop()`，手动 join | 有，自己发 | 一对多，或发请求的那一端（stop_source）要跨组件共享 |

表里最后两行经常搭配着用。jthread 管单条线程的收尾，stop_source 管一组线程的统一停止。您选哪一行，看任务需不需要被叫停。

## 下一步

第一单元到这里收尾。我自己的习惯是在开线程之前把退出路径想一遍，想不通就别开。

接下来是讲竞争的单元，从错误的程序入手。停止协议管的是“什么时候停”，管不了“两条线程同时读写同一份数据”。咱们在《第一个 std::thread 程序》的实验里见过输出绞在一起，那还只是看得见的乱。下一单元看的是看不见的乱，以及它为什么每次跑都可能给您不同的答案。

## 参考资源

本篇写作时借鉴了一本书，《C++ Concurrency in Action》，作者 Anthony Williams，取的是线程与协作式取消的章节推进思路。例子和文字都是我自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言查了 cppreference 与标准草案，条款号附在条目后面。

- [std::jthread（cppreference）][cppref-jthread]，C++20 起的自动 join 线程。
- [std::jthread::~jthread（cppreference）][cppref-jthread-dtor]，析构时请求停止再 join。
- [std::stop_source（cppreference）][cppref-source]，写端，发出停止请求并派生 token。
- [std::stop_token（cppreference）][cppref-token]，读端，查询请求与状态。
- [std::stop_callback（cppreference）][cppref-callback]，注册与注销停止回调。
- [std::condition_variable_any::wait（cppreference）][cppref-cvany]，带 stop_token 的可中断等待。
- [C++ 标准草案：jthread 的构造与析构（thread.jthread.cons）][draft-jthread]，构造的两种调用形式与析构的顺序。
- [C++ 标准草案：停止令牌的概念条款（stoptoken.concepts）][draft-stop]，注册、注销、同步关系与 request_stop 的语义。
- [C++ 标准草案：可中断等待（thread.condvarany.intwait）][draft-cvany]，等待期间登记停止通知的规定。

[cppref-jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[cppref-jthread-dtor]: https://en.cppreference.com/w/cpp/thread/jthread/~jthread
[cppref-source]: https://en.cppreference.com/w/cpp/thread/stop_source
[cppref-token]: https://en.cppreference.com/w/cpp/thread/stop_token
[cppref-callback]: https://en.cppreference.com/w/cpp/thread/stop_callback
[cppref-cvany]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[draft-jthread]: https://eel.is/c++draft/thread.jthread.cons
[draft-stop]: https://eel.is/c++draft/stoptoken.concepts
[draft-cvany]: https://eel.is/c++draft/thread.condvarany.intwait
