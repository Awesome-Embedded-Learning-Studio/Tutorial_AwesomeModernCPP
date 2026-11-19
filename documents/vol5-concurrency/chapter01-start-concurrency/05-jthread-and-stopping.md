---
title: "让工作有序结束：std::jthread 与协作式停止"
description: "用 std::jthread 的析构把停止请求接进收尾路径，讲清 stop_source、stop_token、stop_callback 三件套，回看 atomic 标志轮询的短板，处理回调注册与销毁的竞态，最后给出一张退出路径检查表。"
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 让工作有序结束：`std::jthread` 与协作式停止

这个单元走到现在，咱们把线程启动起来了，也看清了它在机器上的位置。《第一个 std::thread 程序》结尾的那个麻烦还在：忘记 join，程序直接 terminate。上一篇《谁负责线程和数据的生命周期》把 join 的责任交给了析构函数，可它结尾又添了一个不肯停的 worker，包装器能等它，却没法通知它该收了。

C++20 补上了后半截。`std::jthread` 在析构里请求停止，然后 join，而“请求停止”本身变成了一套有定义、有同步关系的协议。本篇是第一单元的收尾，目标就一句：咱们写下的每一条执行流，都有一条说得清的退出路径。

实验都收在本页的在线示例里，您点开就能直接运行；我们本地实测的配方是 GCC 16.2.1、`g++ -std=c++20 -O2 -pthread`、WSL2（16 逻辑核，2026-11-14）。`std::jthread` 在 `<thread>` 里，停止令牌在 `<stop_token>` 里，两者都是 C++20 起的设施。下面的输出都是单次运行的读数，同一个环境里重跑，毫秒数和线程编号也会小幅跳动。没有 C++20 的环境只能走 atomic 标志轮询的旧路，后面有一节专门讲它。文中的标准行为按 cppreference 与标准草案条目核对过，条款收在参考资源里。

## 自动 join 的线程

咱们把 `std::thread` 换成 `std::jthread`，删掉那行 `join()`，程序照跑：

<OnlineCompilerDemo
  title="jthread：不写 join，析构自动等"
  source-path="code/examples/vol5/chapter01/37_jthread_auto_join.cpp"
  description="std::jthread 的析构函数替您 join，一行不写也不丢线程。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来一行 `worker done`。到这一步，它跟上一篇扩展节里那个在析构里 join 的包装类 `JoiningThread` 没有区别，都在析构里等线程结束。区别在等之前那一步。标准草案 [thread.jthread.cons] 对 `~jthread()` 的规定是，`joinable()` 为 true 时调用 `request_stop()`，然后调用 `join()`（C++20 起）。`JoiningThread` 只做后一半，jthread 做全了。

这个顺序不是文档里的一句话，跑起来看得见。咱们让工作线程一直循环到看见停止请求，主线程从头到尾不调 `request_stop()`，只让对象离开作用域：

<OnlineCompilerDemo
  title="jthread 自动请求停止"
  source-path="code/examples/vol5/chapter01/38_stop_token_basic.cpp"
  description="离开作用域时析构先请求停止再等待，worker 的循环看得见停止令牌。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来的五行是这样的（线程编号因机器而异）：

```text
[main] thread 125682764838784
[main] leaving the scope, no request_stop call
[callback] running on thread 125682764838784
[worker] saw the request after 8 ticks, thread 125682757662400
[main] destructor returned
```

请您把回调那一行和 `[main]` 那一行对一下编号，两个数字相同。停止请求是主线程在析构函数里发出的，不是某个后台线程替它发的。worker 的编号跟它们不同，它在自己的执行流上循环，看见 `stop_requested()` 变成 true 就退出，析构里的 join 才等得到它。要是没有 request_stop 这一步，worker 的循环条件永远不成立，析构就永远等下去，跟只做 join 的 `JoiningThread` 一样。

请求发出去，只对看得见 token 的线程有用。`std::jthread` 的构造函数试两种调用形式。把内部 token 插在函数对象后的第一个参数位能编过，就用带 token 的调用。编不过，就不带 token 调用（[thread.jthread.cons] 的 Effects，C++20 起）。咱们写一个不接受 token 的线程函数试试：

<OnlineCompilerDemo
  title="析构等待：worker 没干完就得等"
  source-path="code/examples/vol5/chapter01/39_jthread_dtor_waits.cpp"
  description="worker 睡满 300 毫秒没人请求停止，析构就等满 300 毫秒——等待是析构的底线。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
[main] leaving the scope at 50 ms
[worker] finished its own work, nobody asked it to
[main] destructor returned at 301 ms
```

主线程 50 毫秒就离开了作用域，析构却到 301 毫秒才返回。中间那约 250 毫秒不是 request_stop 花的。request_stop 发出请求就返回了，join 在等 worker 睡完自己的 300 毫秒，咱们也只能陪着等。请求确实发出去了，可 worker 手里没有 token，没人看，请求等于没发。协作式停止就是这个意思，它不杀线程，只发请求。

jthread 身上还有三个入口，`request_stop()`、`get_stop_source()`、`get_stop_token()`，都是转调内部那个 `stop_source`。要用得细，比如把同一个 token 传给几个组件，就得从它们往外拿。下一节咱们看这三件套本身。

## stop_source、stop_token、stop_callback 三件套

停止状态（stop state）由标准库管理。三件套是访问它的三种角色，咱们看它们各自做什么。`std::stop_source` 是写端，`request_stop()` 发出请求，`get_token()` 派生出读端。`std::stop_token` 是读端，`stop_requested()` 问请求来没来，`stop_possible()` 问它有没有关联的停止状态。`std::stop_callback` 是通知口，注册一个可调用对象，请求到达时执行。三者都是 C++20 起的设施，声明在 `<stop_token>` 头文件里。

咱们把基本行为跑一遍：

<OnlineCompilerDemo
  title="stop_source 与 stop_token 的状态查询"
  source-path="code/examples/vol5/chapter01/40_stop_source_states.cpp"
  description="两次 request_stop 只有一次返回真；默认令牌与 nostopstate 源都停不了任何东西。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
before: source.stop_possible()=true token.stop_possible()=true
before: source.stop_requested()=false token.stop_requested()=false
request_stop() #1 -> true
request_stop() #2 -> false
after:  token.stop_requested()=true
default token: stop_possible()=false stop_requested()=false
nostopstate source: stop_possible()=false request_stop() -> false
```

输出里有几件事值得您逐行看。默认构造的 source 立刻有停止状态，`stop_possible()` 是 true。`request_stop()` 调多少次都安全，只有第一次返回 true，第二次调用不会重复触发回调。请求发出之后不撤回。默认构造的 token 是脱开的（disengaged），没有关联状态，两个查询都返回 false。用 `std::nostopstate` 构造的 source 也没有状态，`request_stop()` 返回 false。token 可以拷贝、可以比较，同一个 source 派生出来的 token 彼此相等，所以咱们拿它当参数传来传去不丢身份。

jthread 内部本来就有一个 `stop_source`，`t.request_stop()` 转调的就是它。要用内部那个，就调 `t.get_stop_source()`。您只用 jthread、不碰三件套，析构那一步照样发请求。要让同一次停止覆盖几条线程，就得自己拿一个 source 往外派 token。下面的演示为了说明三件套不依赖 jthread，另起一个 source，用三条 `std::thread` 跑一对多：

<OnlineCompilerDemo
  title="一个 source 停一组线程"
  source-path="code/examples/vol5/chapter01/41_one_source_many_threads.cpp"
  description="三条 processor 共享同一枚令牌，一次 request_stop 各自看见、各自收场。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
one request_stop() call
processor 2 stopped after 3 rounds
processor 3 stopped after 3 rounds
processor 1 stopped after 3 rounds
all joined
```

一次请求，三条线程都在下一轮循环里看见它。停止请求是发给停止状态的，不是发给某一条线程的。一个 source 派出去的 token 都看同一份状态，谁拿着 token 谁就能看见请求。三条线程打印的次序每次都会不一样，它们各自检查的时机由调度决定，咱们在《第一个 std::thread 程序》里见过同样的交错。线程再多些，一行里的字符也可能绞在一起，`std::cout` 的那个毛病那次也见过。

光能查询还不够。有时候请求到达的那一刻，正好是关文件、断连接、唤醒一个等待的最好时机，这就要 `std::stop_callback` 上场。它注册一个可调用对象，请求到达时执行。咱们把注册时机跑一跑：

<OnlineCompilerDemo
  title="回调在哪个线程上跑"
  source-path="code/examples/vol5/chapter01/42_callback_thread.cpp"
  description="请求前注册的回调跑在发起请求的线程上，请求之后再注册的当场就地执行。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
main is thread 129462478755712
registering a callback, before request
registration returned
calling request_stop
  [callback] runs on thread 129462478755712
request_stop returned
registering another callback after the request
  [late] runs on thread 129462478755712
late registration returned
```

两次回调都跑在主线程上，跟 `main is thread` 那个编号一致。第一次是在 `request_stop()` 里面同步执行的，回调返回，`request_stop()` 才返回。第二次注册时请求已经发过了。标准规定，注册那一刻请求已经发出，回调就在构造函数里就地执行，不进回调列表（[stoptoken.concepts] 的注册条款，C++20 起）。所以它打在 “late registration returned” 前面，咱们能亲眼看到它没有等到以后才执行。您把 [callback] 和 [late] 两行放在一起看，可以得出一句实用的话：注册回调不保证以后再说，它可能当场就跑。所以，别在回调里做耗时操作，它占的是发请求的线程的时间。回调对象析构时会自动注销，注销的细则里有一段阻塞行为，是最容易写错的地方，后面讲 stop_callback 的注册时机与竞态时，咱们一起看。

## 停止协议的前身：atomic 标志轮询与它的不足（扩展）

本节带（扩展）标记，跳过它不影响本篇主线。不过现成代码里这套旧办法到处都是，认识它，您才知道停止令牌补的是哪几块短板。

C++11 起标准库有了 `std::thread`，可停止这件事没有现成设施可用。当时的常见做法是自己放一个 `std::atomic<bool>` 当停止标志，工作线程每次循环看一眼，主线程置位。咱们把它跑起来：

<OnlineCompilerDemo
  title="停止协议的前身：atomic 标志轮询"
  source-path="code/examples/vol5/chapter01/43_atomic_flag_polling.cpp"
  description="20 毫秒一问的轮询能停，但察觉有延迟——它就是停止三件套要替换的旧路。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
flag set at 50 ms
worker noticed at 60 ms after 3 polls
```

主线程在 50 毫秒置位，worker 到 60 毫秒上下、第三次轮询才看见。响应延迟由轮询间隔决定，间隔 20 毫秒，最坏就多等 20 毫秒。咱们想把延迟压下去，就把间隔调小，线程空转的时间跟着涨。咱们想省 CPU，就把间隔调大，停止就变迟钝。这个取舍是轮询形状的一部分，由间隔决定，代码写得再整齐也躲不掉。

轮询还有个更硬的问题，它叫不醒一个正在阻塞等待的线程。咱们让 worker 等在条件变量上，主线程只改标志，不 notify：

<OnlineCompilerDemo
  title="轮询错过窗口：没人通知就得睡满"
  source-path="code/examples/vol5/chapter01/44_poll_miss_window.cpp"
  description="标志在 sleep 中间置位，wait_for 没人唤醒只能睡满 500 毫秒——停止三件套补的就是这个洞。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
flag set to true at 50 ms, nobody notifies
wait_for returned true after 500 ms
```

唤醒这一头也一样，标志自己不会叫醒谁。标志在 50 毫秒就变成 true，worker 还是睡满了 500 毫秒才回来。返回的 true 是谓词的结果。所谓谓词，就是 wait 第三个参数那个返回 bool 的判断。标准对这一点写明了，返回值表示谓词是否为真，跟超时、跟被谁叫醒都没有关系。所以咱们拿返回值当“收到停止请求”的信号也不成立。您在同一个程序里补一次 `notify_all()`，等待 50 毫秒就结束了，中断等待靠的是通知，不是标志本身。

第三个短板是语义。您得自己约定几件事。谁能置这个标志？置了以后谁负责唤醒？标志活得比线程久吗？两条线程读到的值算不算同步？两个模块各有停止条件，怎么合并？有没有“停止可能”这样一个状态可以问？这些全是口头约定。每条线程、每个接手维护的人都得知道，漏一条就是偶发故障。`std::atomic<bool>` 管不到这一层。它在 relaxed 语义下，连“写标志之前写好的数据，对读到标志的线程可见”都不保证。relaxed 是最宽松的那档内存序，只保证这个变量自己的读写不乱，别的都不保证。

停止令牌把这些约定收进了标准。`request_stop()` 返回 true，与另一条线程上 `stop_requested()` 返回 true 之间有同步关系。注册回调与那次回调的执行之间也有同步关系（[stoptoken.concepts] 的同步条款，C++20 起）。您不用逐条背约定，库把它们变成了条款。没有 C++20 的场合，atomic 标志轮询照样能用。咱们只是要把上面每一条约定自己写下来，自己检查。

## stop_callback 的注册时机与竞态

注册回调有两条路径，标准写得很清楚。注册那一刻 `stop_requested()` 还是 false，回调进列表，请求到来时执行。注册那一刻它已经是 true，回调立即在注册线程上执行，不进列表。咱们前面讲三件套时，最后注册的那个回调（代码里叫 late）走的就是后一条。

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
| 手写 `JoiningThread`（见上一篇） | 只 join | 没有 | C++11 到 C++17，任务会自己结束 |
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
