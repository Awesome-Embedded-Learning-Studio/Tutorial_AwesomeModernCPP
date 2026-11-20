---
title: "任务什么时候执行：启动策略与等待行为"
description: "async、deferred 与默认策略各把任务安排在什么时候跑，deferred 不调用就永不执行、wait_for 只会空转，async 的 future 析构要等任务做完，背后的原因落在共享状态的引用计数上。"
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 任务什么时候执行：启动策略与等待行为

上一篇 4.1《用 `std::async` 执行计算》把发起和取结果讲完了：任务交给 `std::async`，结果从 `future` 里取，异常也顺着它传回来。4.1 的演示从头到尾用的都是不带策略的默认写法，标准写的“可能在新线程上发起”，咱们也一直没展开。还剩一个疑问咱们没有碰：您把任务交出去以后，它到底什么时候开工？

答案有三种，而且标准把其中一种的选择权交给了实现。启动策略（launch policy）就是 `std::async` 的启动参数，任务什么时候开工归它管。咱们把三种策略逐一跑一遍，把 deferred 的三个陷阱用实验摆出来。随后是第一个扩展节（deferred 什么时候值得主动选），再回主线讲析构的等待行为，最后是第二个扩展节（析构为什么等）。

## async、deferred 与默认策略

`std::async` 有两个重载，咱们一个个认。带策略的重载把 `std::launch` 当第一个参数。不带策略的重载，标准规定它等价于传入 `std::launch::async | std::launch::deferred`（[futures.async]/3）。两个旗标可以按位或组合起来用，默认策略就是这么写出来的。

`std::launch::async` 的意思是：`std::async` 的调用会在新的执行线程里把函数调起来，返回值和异常存进共享状态。条款里有一句补充，请您留意：线程对象本身，也存进共享状态（[futures.async]/3.1）。眼下看不出用途，扩展节里它是答案的一半。措辞上还有一处值得细看，标准形容异步执行，原话是“像在一个由 thread 对象代表的新执行线程里”。咱们照字面读，thread 对象意味着每回都是老老实实的一条新线程，不是从线程池里捞出来的。cppreference 首句的概述是 might be a part of a thread pool，页面给实现留了线程池的可能，您翻引用页时会看见两个说法。条文按 thread 对象写，咱们照条文读。本机走哪条路，后面的源码节选会摆出来，每回都是新起的一条线程。

`std::launch::deferred` 则相反：函数和实参被存进共享状态，谁也不动它们。第一次对返回的 future 调用非计时等待函数（`wait` 和 `get` 都算）时，函数才在调用等待函数的线程里执行（[futures.async]/3.2）。您不给它这一天，它就一直存着。

两个旗标都写上的时候，标准没有规定实现选哪种，只留了一句推荐实践：并发已经用不上的时候，实现应当推迟执行，或者推迟选择（[futures.async]/3.2 的 Recommended practice）。cppreference 的 Notes 里还提到，个别实现会给默认策略添自己的位，举过 sync 和 task 两种自定义策略的例子。咱们读到这儿就记下：选哪个，是实现的自由。

咱们拿同一个任务把三种策略各跑一遍，看线程号和时刻：

<OnlineCompilerDemo
  title="同一任务三种策略各跑一遍，看线程号和时刻"
  source-path="code/examples/vol5/chapter04/06_demo_three_policies.cpp"
  description="任务睡 50 毫秒后打印自己所在的线程号与时刻；async 组换线程立刻开工，deferred 组等 get 才在 main 线程里跑，default 组在本机长得跟 async 一样。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
[async   ] main 线程是 12072240331452761199
  async    在线程 3459647975372803470 上执行，距 async 返回 50.2 ms
[async   ] main 睡了 100 ms，现在调 get
[async   ] get 返回，全程 101.6 ms

[deferred] main 线程是 12072240331452761199
[deferred] main 睡了 100 ms，现在调 get
  deferred 在线程 12072240331452761199 上执行，距 async 返回 157.8 ms
[deferred] get 返回，全程 157.8 ms

[default ] main 线程是 12072240331452761199
  default  在线程 3459647975372803470 上执行，距 async 返回 50.5 ms
[default ] main 睡了 100 ms，现在调 get
[default ] get 返回，全程 102.9 ms
```

咱们逐组读输出。任务打印的时刻发生在它自己睡完 50 毫秒之后，拿打印时刻减 50 就是它的起步延迟。async 组：任务换到了新线程，起步延迟约 0.2 毫秒，main 还在睡它就干完了活，`get` 立刻返回。deferred 组：任务和 main 同一个线程号，输出出现在 main 睡醒、调了 `get` 之后，157.8 毫秒大致是 100 毫秒的睡眠加 50 毫秒的活。default 组在本机长得跟 async 一模一样：换了线程，立刻开工。

> default 组的线程号跟 async 组完全相同，您要是多看一眼就会发问。前一条线程早已结束，系统把线程号收回去复用了，咱们看到的不是同一条线程还活着。

本机默认策略选了 async，这是 libstdc++ 的做法，不是标准的要求。选择逻辑写在咱们跑实验的电脑自带的 `/usr/include/c++/16/future` 里：

```cpp
// /usr/include/c++/16/future（节选，注释为笔者所加；try 为原文内部宏 __try 的展开式，异常宏分支省略）
if ((__policy & launch::async) == launch::async)
  {
    try
      {
        __state = std::make_shared<_As>(...);   // _As 是 _Async_state_impl，起线程
      }
    catch(const system_error& __e)
      {
        if (__e.code() != errc::resource_unavailable_try_again
            || (__policy & launch::deferred) != launch::deferred)
          throw;                                // 起不了线程，也没给 deferred 位，照常抛
      }
  }
if (!__state)
  __state = std::make_shared<_Ds>(...);         // 走到这里，退成 deferred。_Ds 是 _Deferred_state，deferred 的状态类
```

咱们把上面的源码翻译成白话。默认重载转手就把 `async | deferred` 传下去。策略重载里，只要 async 位在，它就选起线程的状态对象。起线程失败抛出 `resource_unavailable_try_again`、而且 deferred 位也在，才退成 deferred 的状态对象。cppreference 把抛错的边界写过一遍，来源标的是库工作组第 2078 号议题（LWG 2078）的决议：只有 policy 等于 `std::launch::async` 时才抛 `system_error`，组合策略会退回 deferred，不抛。标准正文写着同样的错误条件（[futures.async]/6-7）。

## deferred 的陷阱

deferred 的行为每一条都写在标准里，标准本身没有含糊的地方。会含糊的是咱们的预期，我把它分成三个陷阱，每个都配了实验。

### 陷阱一：不调用就永不执行

deferred 的任务不占任何线程。它就是共享状态里存着的函数和实参，标准在 [futures.state]/2 的注里写了，result 可以是“用来算出结果的函数”，deferred 存进去的正是它。非计时等待函数第一次被调用，它才在调用线程里执行（[futures.async]/3.2）。您不调 `get`，也不调 `wait`，它就一直存着。后果草案都替咱们写明了（[futures.async]/4 的注）：函数可能压根没被调用。

您可能想，那我析构的时候总该跑一下吧？不。deferred 没有线程可等，future 析构只是放手。咱们看实验：

<OnlineCompilerDemo
  title="deferred 不调用就永不执行，析构也不补跑"
  source-path="code/examples/vol5/chapter04/07_demo_deferred_never_runs.cpp"
  description="deferred 的 future 拿在手里 0.0 毫秒走完，面包从头到尾没进过烤箱；async 只丢弃临时 future，析构却等了 200 毫秒。本地加 -Wall 会看到 nodiscard 告警，正文析构一节专门讲它。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
[deferred] future 一直在手里，没调 get 也没调 wait
[deferred] 到作用域结尾丢弃，共 0.0 ms，上面没有 bake 的输出
  bake: 面包出炉了
[async] 丢弃临时 future，整个表达式 201.9 ms
main 到头了，bake 只烤成过一次
```

deferred 组里，bake 的输出一行都没有。future 一直在手里也好，到作用域结尾丢弃也好，程序计时 0.0 毫秒走完了全程，面包从头到尾没进过烤箱。async 组只是丢弃了临时 future，析构却等了 201.9 毫秒，bake 的输出出现在计时行之前。同一个丢弃动作，两种策略：一边任务没跑，一边您被迫等它跑完。析构为什么等，归后面的析构一节和它的扩展节细讲，这里记下 deferred 的结果就行：不等，也不补跑。您要是较真，可以对照 [futures.state]/5.3：deferred 的状态其实也凑齐了析构一节要列的三个条件（状态由 async 创建、还没就绪、放手的是最后一个引用），条文写的是“可能阻塞”，实现选不阻塞、也不补跑，一样合规。

> 有个猜测咱们替您答了：把 deferred 的 future 交给别的线程，将来谁来调 `get`，任务就在谁的线程里跑。执行位置跟着等待函数走，不跟着当初发起 async 的线程走。

### 陷阱二：wait_for 只会空转

`wait_for` 和 `wait_until` 是计时等待函数，返回值是 `std::future_status`，有 `ready`、`timeout`、`deferred` 三种。标准给它们俩的 Effects 写得很干脆：共享状态里存着 deferred 函数时，什么都不做，返回值固定是 `future_status::deferred`（[futures.unique.future]/22、/23.1，wait_until 同款在 /25、/26.1）。您调用它，它不执行任务，也不等待，立刻回来。

<OnlineCompilerDemo
  title="deferred 的 wait_for 立刻返回，不执行也不等待"
  source-path="code/examples/vol5/chapter04/08_demo_wait_for_spins.cpp"
  description="三次 wait_for(100ms) 各花 0.000 毫秒、都返回 deferred，改用 get() 任务这才执行。拿它写超时轮询会空转烧满一个核。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
第 1 次 wait_for(100ms) -> deferred 实际用时 0.000 ms
第 2 次 wait_for(100ms) -> deferred 实际用时 0.000 ms
第 3 次 wait_for(100ms) -> deferred 实际用时 0.000 ms
改用 get()，任务这才执行 -> 7
```

三次 `wait_for` 各花了 0.000 毫秒，三次都是 deferred。要是您拿 `wait_for` 写超时轮询，判断条件写成不等于 `ready` 就继续，循环就转起来了：每次立刻返回，条件永远不满足，CPU 烧满一个核，任务一步都没走。

修法是把 deferred 当成一种答案来处理。轮询里查到 deferred，说明任务还没开始跑，要么直接 `get` 让它在当前线程跑完，要么换 `launch::async` 重新发起。空转比卡死更气人，卡死的进程您一眼就能看出来，空转的进程活得好好的，只是不出活。

### 陷阱三：thread_local 与线程身份会踩空

deferred 在调用等待函数的线程里执行。调用等待函数的线程，不一定是当初发起 async 的线程，cppreference 在 deferred 一节特意写明过。咱们手里的代码要是依赖线程身份，deferred 就会把依赖落错地方。

<OnlineCompilerDemo
  title="deferred 在调用 get 的线程里跑，thread_local 踩空"
  source-path="code/examples/vol5/chapter04/09_demo_deferred_thread_local.cpp"
  description="同一个 bump：async 版在新线程从零算到 1，deferred 版在 main 的 get 里执行，看见 main 已加到 3 的实例算到 4，main 手里的 counter 跟着变 4。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
main 线程是 1278684541068337386，counter 加到 3
  bump 在线程 13407857457959151560 上，counter 算到 1
  bump 在线程 1278684541068337386 上，counter 算到 4
async 的答案 1，deferred 的答案 4
main 手里的 counter 是 4
```

同一个 `bump`，async 版跑到新线程，counter 从零起步，算到 1。deferred 版在 main 的 `get` 里执行，看见的是 main 已经加到 3 的实例，算到 4，main 手里的 counter 也跟着变成了 4。咱们想要的任务各算各的，deferred 没给。

thread_local 的实例跟着线程走，其余依赖线程身份的代码也一样。您拿 thread::id 当 map 的 key，或者拿线程号打日志，deferred 都会让任务和调用方共享同一个身份，咱们原本指望的“后台线程里才有的初始化”更是没影。它在哪个线程跑，取决于谁调了等待函数，标准是这么定的（[futures.async]/3.2）。

## deferred 的合理用法（扩展）

> 本节是扩展，您跳过它不影响主线。陷阱讲完了，剩下的内容是 deferred 什么时候值得主动选，看完再回主线讲析构。

咱们把陷阱反过来读。deferred 的每个陷阱，换个场景都是特性。不调用就不执行，懒求值要的正是这个。任务在调用线程里跑，起线程的开销也跟着省了。至于 `wait_for` 立刻返回 deferred，您正好拿它探测一个还没开始跑的任务。

我见它好用有两种场合。

一是懒求值。解析一份配置文件要几百毫秒，程序里只有某个分支才用到它。您把它包成 deferred，真走到用它的地方才 `get`，没走到就一分钱不花：

```cpp
// 示意，不是能直接编译的完整程序
std::future<Config> lazy_config =
    std::async(std::launch::deferred, load_config);

if (mode == Mode::Fancy) {
    Config cfg = lazy_config.get();   // 走到这里才真正解析
    apply(cfg);
} else {
    // 不走 Fancy 分支，load_config 一次都不会执行
}
```

二是给轻任务一个 future 的形状。任务本身两三毫秒，起线程的开销比活还贵，可您又想让调用方统一拿 future、统一 wait、统一接异常。deferred 把线程成本拿掉，接口保持不变。咱们在 4.4《把计算包装成可存储分发的任务》（还没成篇）还会碰到同款思路：把执行和结果搬运分开，前者可以换，后者不变。

有一条边界您得自己守：deferred 的函数在 `get` 的线程里跑，您不能把“它已经在后台跑了”写进任何假设。判断法很朴素。现在就要、要并发，选 `launch::async`。用到才算、算起来飞快，选 `launch::deferred`。拿不准，就显式写出您选的策略，别让默认替您决定。

## 返回 future 的析构行为

咱们回到主线。陷阱一里丢弃 deferred，future 析构 0 毫秒。换成 `launch::async`，同一个丢弃动作变成了等待。本节把等待的条件和行为讲清楚。

规则的原文在 [futures.state]/5，咱们从骨架看起。future（以及其他异步对象）析构时“释放”共享状态，一般是放手就走。例外要三个条件同时成立：状态由 `std::async` 创建，状态还没就绪，放手的是最后一个引用。三者凑齐，释放可能阻塞。[futures.async]/5 的注补了一句人话：从 async 拿到的 future 如果被移出局部作用域，它的析构可能为了等共享状态就绪而阻塞。cppreference 把另一半也写了：别的途径拿到的 future，析构从不阻塞。

<OnlineCompilerDemo
  title="丢弃式 async 逐个等待，启动完再收集才并行"
  source-path="code/examples/vol5/chapter04/10_demo_dtor_blocks.cpp"
  description="丢弃式三行各自等满 300 毫秒累计 900 多毫秒；future 存进容器全部启动完再统一 get，三个一起只要 300 毫秒。deferred 丢弃与 promise 的 future 析构都是 0.0 毫秒谁也不等。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
  A1 干完了
  A2 干完了
  A3 干完了
[丢弃式] A1 用 300.8 ms，A2 累计 601.1 ms，A3 累计 904.7 ms
  B1 干完了
  B2 干完了
  B3 干完了
[启动完再收集] 三个一起 300.6 ms
[deferred 丢弃] 0.0 ms，上面没有 C 干完的输出
[promise 的 future 析构] 0.0 ms，谁也不等
```

丢弃式三行，每行各自等满 300 毫秒，累计 904.7 毫秒，A1 到 A3 的输出按顺序排队。咱们眼睛看着是三行并排的代码，机器执行是严格串行：前一个临时 future 析构完，后一个任务才起。想让任务并行，就把 future 存进容器，全部启动完再统一 `get`，B 组三个一起只花了 300.6 毫秒。

4.1 收尾留过一句话，任务抛出的异常还会不会被任何人看见，也系在析构的等待行为上。咱们现在能答了：丢弃式 async 里任务抛了异常，析构等到任务完成，异常存进共享状态，最后一个引用放手，状态销毁，异常跟着一起没了（[futures.state]/5.1）。想让它被看见，就得有人来 `get`。该推论我从条款读出来，没有单独立实验，异常确实存进共享状态这一半，4.1 已经验过，咱们这里补的只是销毁那一步。

deferred 丢弃 0.0 毫秒，跟陷阱一的读数一致。promise 的 future 析构也是 0.0 毫秒，谁也不等。promise 是 4.3《自行传递结果：`std::promise` 与 `std::future`》的主角，这里只借它当对照。咱们对照完就能说：同样是 future 类型，出身不同，析构的性格就不同。

三个条件里最容易被忽略的是“最后一个引用”。future 不能拷贝，引用摊不开，`shared_future` 可以。咱们试一下：

<OnlineCompilerDemo
  title="等待发生在最后一个引用放手时"
  source-path="code/examples/vol5/chapter04/11_demo_last_reference.cpp"
  description="多出来的副本析构 0 毫秒，放掉一个引用 0 毫秒，最后一个引用放手时才等满 300 毫秒：等待挂在引用计数清零上。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
多出来的副本析构 0.000 ms
放掉一个引用 0.000 ms
最后一个引用 300.4 ms
```

咱们把三个读数排成一排看：副本析构 0 毫秒，放掉一个引用 0 毫秒，最后一个引用放手时等了 300.4 毫秒。等待发生在引用计数清零的瞬间，不在每一次析构。

还有一个帮手在编译器手里。libstdc++ 给 `std::async` 标了 `[[nodiscard]]`，咱们用默认选项编译一行丢弃式调用，告警自己就出来了：

```text
warning: ignoring return value of 'std::future<...> std::async(launch, _Fn&&, _Args&& ...)',
    declared with attribute 'nodiscard' [-Wunused-result]
```

告警文本我做了截断，原样贴出来会占满半屏。它提醒的正是丢弃式 async：返回值带着 nodiscard，您扔掉它，编译器会出声。看到告警，别急着拿 `(void)` 压下去，想想任务是您想等，还是您忘了等。还有一条判断从条款里直接读得出来：`get` 完以后状态已经就绪，之后再析构就落在例外条件之外，放手即走，“还没就绪”就是门槛（[futures.state]/5.3）。该推论我没有单独立实验，是从条款读出来的。

## 析构为什么会等：共享状态的引用与释放时机（扩展）

> 本节是扩展，您跳过它不影响主线。上一节把行为和条件都给了，本节回答的是为什么要这么设计，从共享状态的引用计数讲到线程对象的下落。

咱们把共享状态摊开看。标准对它的定义在 [futures.state]/1：状态信息，加一个可能还没算出来的 result，result 可以是值，也可以是异常。注里补了第三种：还可以是“用来算出结果的函数”，async 的 deferred 存的就是它。future 和 promise 都只是挂在状态上的对象，一边读，一边写。

future 析构做的事，标准写在 [futures.unique.future]/10：释放共享状态，然后销毁自己。释放的含义在 [futures.state]/5：放手，要是手里是最后一个引用，状态随之销毁。一般不阻塞，例外就是上一节的三个条件，咱们已经背过一遍了。

为什么 async 的状态销毁要等到任务完成？咱们把策略一节埋下的伏笔接上：线程对象存进共享状态（[futures.async]/3.1）。线程对象不是普通成员，它析构时有自己的脾气。joinable 的 `std::thread` 一旦析构，会调用 `std::terminate`（[thread.thread.destr]/1）。状态要是趁任务还没跑完就销毁，里面的线程对象跟着销毁，程序当场终止。所以释放最后一个引用之前，咱们必须把线程收回来，收回来的办法就是 join，join 就要等任务跑完。

libstdc++ 的源码跟上面的推理对得上，咱们对着看。`_Async_state_commonV2` 里挂着一个 `thread _M_thread`，旁边的注释直接引了草案条款，说的正是 waiting function 的等待规则：像 join 一样，等线程完成。`_Async_state_impl` 的析构只有三行：

```cpp
// /usr/include/c++/16/future（节选）
~_Async_state_impl()
{
  if (_M_thread.joinable())
    _M_thread.join();
}
```

析构上方的原注释我也念给您听，意思是：任务没跑完之前不能销毁存结果和存函数的成员，这里直接调 `join` 而不走带 `call_once` 的 `_M_join`，因为状态销毁时不会再有别的线程引用它。引用计数的实现就是 `shared_ptr`，future 持有的是 `std::shared_ptr<_State_base>`。咱们把 future 存进 vector，引用跟着 future 活着，B 组的任务因此敢一起跑。真要把引用摊成多份持有者，靠的是 `shared_future` 的拷贝，实验里副本析构 0 毫秒对应的就是它。

deferred 的状态对象里没有线程成员，咱们翻头文件就能确认：`_Deferred_state` 的数据成员就两样，存函数的和存结果的。没有线程要收，析构自然 0 毫秒。

promise 供值的一侧也值得看一眼，它给“别的 future 从不阻塞”补上最后一块。往共享状态里放结果的对象，4.1 认过标准起的名字：异步提供者（[futures.state]/4）。promise 就是提供者。提供者销毁而状态还没就绪，标准定义为弃掉（abandon）：把一个 `future_error`、错误条件 `broken_promise` 存进状态，再把状态变就绪（[futures.state]/7）。咱们跑一段：

<OnlineCompilerDemo
  title="promise 没给值就销毁，get 收到 broken_promise"
  source-path="code/examples/vol5/chapter04/12_probe_broken_promise.cpp"
  description="promise 没兑现就销毁，共享状态被弃反而变就绪，get 抛出 future_error：Broken promise。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
get 抛了 future_error：std::future_error: Broken promise（broken_promise？是）
```

弃掉不是泄漏。状态被弃的时候反而变得就绪，等着 `get` 的人立刻收到一个明确的错误，知道提供的一方没兑现就走了。引用、就绪、弃掉，三个词凑齐，析构行为就全落在咱们能解释的范围里。async 的状态里住着线程，最后一个引用放手时必须收线程，所以要等。别的状态里没有线程，放手就走，没人兑现就送一个 `broken_promise`。

## 轮到您动手的时候

写下一处 async 之前，我会把这几个问题过一遍：

1. 您的 async 调用写了策略吗？没写，您就是在赌实现此刻的选择。
2. 拿到 future 以后，有没有一条确定会走到 `get` 或 `wait` 的路径？一条都没有的任务永远不会跑。
3. 超时等待的返回值，deferred 分支处理了吗？
4. 任务里有没有 thread_local、thread::id，或者“后台线程”的假设？deferred 会把依赖落到调用等待函数的线程上。
5. 丢弃式 async 是您想要的效果吗？真想并行，就把 future 存进容器，全部启动完再统一 `get`。

## 下一步

策略、陷阱、析构，每一条都有实验和条款对上了。下一篇是 4.3《自行传递结果：`std::promise` 与 `std::future`》。提供者到台前，轮到它回答谁执行工作、谁提供结果、失败怎么传。咱们在上文借 promise 当过对照，下一篇它当主角。

## 参考资源

写本篇的时候，推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，async 与 future 合在一起讲的安排跟咱们这里最接近。例子和文字都是我自己写的，实验在本机真跑过（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2 内核 6.18.33.2，16 逻辑核，2026-11-16），输出照原样贴在正文里，计时数字每次运行都会差一点，线程号是哈希值，每回都不同。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的现行工作草案，方括号里是小节名，斜杠后面是段号，小节名跟正式标准一致，段号可能挪动。cppreference 的 `std::async` 页面抓到了全文，丢弃式 async 和“别的 future 从不阻塞”两种说法，都能跟它的 Notes 对上。libstdc++ 的实现细节读的是本机 GCC 16 自带的 `<future>`（路径 `/usr/include/c++/16/future`），没有网页可引。旧目录 ch05 的对应篇目，第一篇《std::async 与 future》，把默认策略的风险讲过一遍，本篇的实验与条款核对都是重新做的。

- [std::async（cppreference）][cppref-async]，两个重载与默认策略、策略选择的边界、丢弃式 async 的 Notes，以及缺陷决议表里的 LWG 2078。
- [LWG 2078（标准库工作组议题）][lwg-2078]，起不来线程时抛 `system_error` 的边界收窄到 policy == `launch::async` 的决议。
- [Function template async（现行草案 futures.async）][draft-async]，默认策略的等价写法、async 与 deferred 各自的 Effects、同步条款、错误条件，以及“函数可能压根没被调用”的注。
- [Shared state（现行草案 futures.state）][draft-state]，共享状态的定义、release 的三个条件、abandon 与 broken_promise。
- [Class template future（现行草案 futures.unique.future）][draft-unique]，`wait_for` 对 deferred 什么都不做的 Effects、析构与 `get` 各自怎么释放共享状态。
- [Destructor（现行草案 thread.thread.destr）][draft-threaddestr]，joinable 的线程对象析构调用 terminate。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，async 一章的推进顺序参考。

[cppref-async]: https://en.cppreference.com/w/cpp/thread/async
[lwg-2078]: https://cplusplus.github.io/LWG/issue2078
[draft-async]: https://eel.is/c++draft/futures.async
[draft-state]: https://eel.is/c++draft/futures.state
[draft-unique]: https://eel.is/c++draft/futures.unique.future
[draft-threaddestr]: https://eel.is/c++draft/thread.thread.destr
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
