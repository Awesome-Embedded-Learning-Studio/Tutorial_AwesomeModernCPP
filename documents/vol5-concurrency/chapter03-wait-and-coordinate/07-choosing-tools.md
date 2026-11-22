---
title: "按协作需求选择工具"
description: "第三单元的收尾：latch 把等一轮计数压成一行，barrier 管反复齐步还带完成函数，信号量把并发上限写成一个数，收束到一张四类协作各归各家的对照表，外加 mutex 与条件变量的模拟版。"
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

# 按协作需求选择工具

六篇下来，咱们手里的基本件齐了，可有的协作，写成条件变量就是绕。启动时等三件初始化活干完，要一把锁、一个计数器、一个标志，通知的时序还得自己护。阶段之间反复齐步，每轮的参与人数自己记，上一轮没散的等待者还得防着混进下一轮。

C++20 为上面说的等待备了三件专门的工具：`std::latch`、`std::barrier` 和信号量，咱们这一篇逐个把它们请出场。它们各自把一类协作压成一行调用，代价是表达面变窄，复合条件与停止请求，各有各的接不了。

## std::latch：一次性倒计时

`std::latch` 住在 `<latch>` 头文件里，C++20 起提供。它内部只有一个计数器，构造时定好初值，`count_down()` 把计数减一格，减到零的瞬间，所有睡在 `wait()` 上的线程一起放行。咱们把一条边界认下来：计数减到零以后加不回去，也没法重置，对象是一次性的（[thread.latch.general]/1）。

咱们把它跟 3.1 的条件变量放在一起看，差别在等什么上。条件变量等的是谓词，谓词可以复合，可以来回变。latch 等的是计数归零，归零只发生一次。3.1 里最难缠的丢失唤醒，在它身上没有藏身处：通知发生在等待之前就是灾难，所以谓词得在 `wait` 之前算一遍。latch 干脆没有通知，`wait()` 只看计数，`count_down()` 走在 `wait()` 前面也无所谓，后者一查计数是零，当场返回（[thread.latch.class]/13）。

咱们跑一段程序，把两面都演到。第一幕是启动初始化：三件活分给三条线程，主线程等它们全部就绪。第二幕把次序倒过来，递减走在等待前面，看看 `wait` 还睡不睡：

<OnlineCompilerDemo
  title="latch：三件初始化活齐了再走"
  source-path="code/examples/vol5/chapter03/24_demo_latch.cpp"
  description="count_down 三声、wait 一等，等待耗时落在最慢那件上；末尾补一段 count_down 走在 wait 前面的情形，计数已是 0，当场返回。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_latch.cpp —— 一次性倒计时：三件初始化活，一个等待方
#include <chrono>
#include <cstdio>
#include <latch>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  std::latch ready(3);
  const char* names[3] = {"logger", "config", "network"};
  std::vector<std::thread> inits;
  for (int i = 0; i < 3; ++i) {
    inits.emplace_back([&ready, i, &names] {
      std::this_thread::sleep_for(50ms * (3 - i));   // 各自耗时 150/100/50 ms
      std::printf("[%s] 就绪\n", names[i]);
      std::fflush(stdout);
      ready.count_down();
    });
  }
  auto t0 = Clock::now();
  ready.wait();
  std::printf("main   : %5.1f ms 全部就绪，业务开始\n", ms_since(t0));
  for (auto& t : inits) t.join();

  std::latch fired(1);
  fired.count_down();   // 递减发生在等待之前
  auto t1 = Clock::now();
  fired.wait();         // 计数已经是 0，当场返回
  std::printf("main   : count_down 走在 wait 前面，wait 用时 %.3f ms\n",
              ms_since(t1));
  return 0;
}
```

</OnlineCompilerDemo>

```text
[network] 就绪
[config] 就绪
[logger] 就绪
main   : 150.2 ms 全部就绪，业务开始
main   : count_down 走在 wait 前面，wait 用时 0.000 ms
```

三件活设计成 150、100、50 毫秒，就绪行的次序由调度排，本次运行是 network 走在最前，logger 收尾。主线程等到最慢的 logger 干完才往下走，150.2 毫秒里多出来的 0.2 是唤醒加调度，跟 3.1 量过的延迟一个量级。第二幕请把镜头挪过去看：`count_down()` 在 `wait()` 之前调用，换成条件变量，通知发出去没人等就蒸发了，等待的一方得靠谓词把已经发生的事实记下来。latch 连谓词都不用，`wait` 用时 0.000 毫秒，它等的就是计数这个状态本身，状态在那里，咱们什么时候来查都算数。

`count_down(update)` 有一道前置条件，update 不许超过当前计数（[thread.latch.class]/7），值得您留神。初始化的活写四条线程，latch 却构造成 3，第四个 `count_down` 会把计数减成负数，前置条件违反，未定义行为。标准不替咱们查，编译器也不拦，构造处写的数、调用 `count_down` 的线程数，两处对得上，得靠您自己过一遍。

想“干完自己的活，再等大伙到齐”，咱们用 `arrive_and_wait()`，它等价于 `count_down(update)` 接 `wait()`（[thread.latch.class]/16）。职责分离的场合，比如干活的不等、汇总的只等，您就照 demo 里这样分开调。您要是拿 3.2 的丢失唤醒来担心——“递减发生在等待之前，会不会像通知一样丢掉？”——这里丢不了：`wait()` 是状态型等待，进 `wait()` 时先查计数，归零就立刻放行；计数只减不加，零是不可逆的终态，哪怕递减和等待分两步走，状态也不会丢。

latch 的一次性是它的定位带来的。等一轮计数只需要发生一次，接口因此极简，咱们把成员函数拢共数出五个：count_down、try_wait、wait、arrive_and_wait，再加查上限的 max。您要是拿 barrier 的成员名单来对，会发现 latch 这里没有单叫 arrive 的成员，减一格的活由 count_down 一个人干。需要反复齐步的场合，每轮新造一个 latch 也能做，参与人数一多就乱，反复齐步的活归下一节的 barrier。

## std::barrier：可重用的阶段同步

`std::barrier` 住在 `<barrier>`，C++20 起提供。构造时给参与数，这个数叫期望计数。barrier 的一生由一串 phase 组成，phase 是标准的术语，一轮齐步算一个（[thread.barrier.general]/1）。每个 phase 的流程，咱们跟着标准走一遍（[thread.barrier.class]/1）。`arrive()` 或 `arrive_and_drop()` 把本轮期望计数减一格，这是一步。计数到零之后，恰好一次，某一条到达的线程在自己的调用里执行完成步，这是第二步。完成步干两件事，跑一遍完成函数，再放行所有等待线程。完成函数是您构造 barrier 时给的那个函数，每轮全员到齐，由它跑一遍，demo 里汇轮数的 lambda 就是它。完成步跑完，期望计数重置回构造值，下一轮开始，这是第三步。

`arrive()` 返回一个 arrival_token。您想报到之后去干别的、晚点再等，就把 token 递给 `wait(token)`。这里标准有条注，咱们得留神：要是没人调 wait，完成步执不执行归实现定（[thread.barrier.class]/1.2），光报到不等，完成函数可能不跑。多数场合咱们用不上这么细的控制，`arrive_and_wait()` 等价于 `wait(arrive())`（[thread.barrier.class]/23），一行搞定。

咱们拿四条线程跑三轮，每轮的活快慢不同，完成函数负责汇轮数，顺带报出它自己在哪条线程身上执行。1.3 量交错输出的时候遇过一个麻烦：几条线程往 `cout` 里写字，一行会被人从中间插进来。demo_latch 那边每行都是一次 printf 写完的整行，worker 打完顺手 fflush，不碍事。这里一行要分好几段拼出来，咱们请 C++20 的 `std::osyncstream` 应付，它把一条线程的多段输出攒成整行再落进 `cout`，下面的输出因此行行完整：

<OnlineCompilerDemo
  title="barrier：四条线程三轮齐步"
  source-path="code/examples/vol5/chapter03/25_demo_barrier.cpp"
  description="每轮等的是最慢到场的线程，完成函数汇轮数、报出自己跑在哪条线程身上；一行分几段拼出来的输出靠 osyncstream 保持完整。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_barrier.cpp —— 可重用的阶段同步：四条线程三轮齐步，完成函数汇轮数
#include <barrier>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  constexpr int kWorkers = 4;
  int round = 0;
  std::barrier sync(kWorkers, [&round]() noexcept {
    ++round;
    std::osyncstream(std::cout)
        << "barrier  : 第 " << round << " 轮全员到齐，完成函数在线程 "
        << std::this_thread::get_id() << " 身上执行\n";
  });

  std::vector<std::thread> workers;
  for (int i = 0; i < kWorkers; ++i) {
    workers.emplace_back([&, i] {
      std::osyncstream(std::cout)
          << "worker " << i << " : 线程 " << std::this_thread::get_id()
          << " 进场\n";
      for (int p = 1; p <= 3; ++p) {
        std::this_thread::sleep_for(20ms * (p + i));   // 各人快慢不同
        sync.arrive_and_wait();
      }
    });
  }
  for (auto& t : workers) t.join();
  std::printf("main     : 三轮走完\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
worker 0 : 线程 136740459247296 进场
worker 1 : 线程 136740450854592 进场
worker 2 : 线程 136740442461888 进场
worker 3 : 线程 136740434069184 进场
barrier  : 第 1 轮全员到齐，完成函数在线程 136740434069184 身上执行
barrier  : 第 2 轮全员到齐，完成函数在线程 136740434069184 身上执行
barrier  : 第 3 轮全员到齐，完成函数在线程 136740434069184 身上执行
main     : 三轮走完
```

四条线程进场行打完，三轮的记录行按轮次出来，每轮等的是最慢到场的线程，活都干完了才放行。完成函数的线程号，值得咱们对一眼：三轮都是 136740434069184，您往进场行里对，它正是 worker 3。标准只说完成步由某条到达的线程执行，没规定是哪条（[thread.barrier.class]/1.2），本次运行落在 worker 3 身上，换一次运行可能换人，但完成函数总跑在某条参与线程身上，不额外占一条。latch 做不到，它没有集中执行的位置，想每轮汇总，得指定某条线程去干。

这个安排还带来一个咱们能借力的性质。完成函数执行时，其余参与线程全堵在屏障上，它读各线程刚写好的数据不用加锁。同步由标准给：arrive 强发生在本轮完成步开始之前（[thread.barrier.class]/14），完成步的结束强发生在被放行线程返回之前（[thread.barrier.class]/3）。强发生（strongly happens before）是内存模型的正式关系，第五单元专门讲，咱们这里用白话说：完成步里写的任何东西，放行之后读都读得到。

两处边界，咱们写代码时得让着它。交给 barrier 的函数对象要满足 `is_nothrow_invocable_v<CompletionFunction&>`（[thread.barrier.class]/5），通俗讲就是它得承诺调用不抛，demo 里 lambda 尾上的 `noexcept` 干的就是承诺不抛的活。真会抛的收尾活别塞给它，放到参与线程自己的代码里。还有，完成函数里别再碰 barrier 自己：非默认完成函数的特化，完成步进行中调用除 `wait` 以外的成员是未定义行为（[thread.barrier.class]/3 的最后一句）。

参与人数中途变动的场合，咱们把 `arrive_and_drop()` 交给退场的线程：调用它的当轮照常减一格，从下一轮起，期望计数永久少一（[thread.barrier.class]/25）。没有它，退出的人没法向屏障请假，剩下的人每轮都在等一个不会再来的名额。

## std::counting_semaphore：把上限写成一个数

信号量住在 `<semaphore>`，C++20 起提供。`std::counting_semaphore<LeastMaxValue>` 的模板参数给的是内部计数的上界，标准只要求 `max()` 不小于它（[thread.sema.cnt]/4），cppreference 特意提醒 LeastMaxValue 是“最小的上界”，`max()` 可以比它大。`acquire()` 把计数减一，计数是零就睡下，等到有人 `release()` 把计数加回来才醒（[thread.sema.cnt]/1）。“同时最多三个人进场”的需求，咱们把名额数写进构造就成。

咱们让 8 个任务抢 3 个名额，顺便把“在场人数的峰值”量出来，再补一次超时的观察。demo 写的是 `std::counting_semaphore<>`，尖括号空着，用的就是实现的默认模板实参，标准把它标成 implementation-defined，咱们不依赖它的具体值，只要它装得下 3：

<OnlineCompilerDemo
  title="计数信号量：8 个任务抢 3 个名额"
  source-path="code/examples/vol5/chapter03/26_demo_semaphore.cpp"
  description="acquire 没名额就睡，release 把名额还回去；量出同时在场的峰值不超 3，末尾补一次计数 0 上的 try_acquire_for，50 毫秒后空手而回。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_semaphore.cpp —— 计数信号量：8 个任务抢 3 个名额，外加一次超时
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <semaphore>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

int main() {
  std::counting_semaphore<> slots(3);   // 3 个名额
  std::atomic<int> inside{0};
  std::atomic<int> peak{0};

  std::vector<std::thread> tasks;
  for (int i = 0; i < 8; ++i) {
    tasks.emplace_back([&, i] {
      slots.acquire();                  // 没名额就睡，等到 release 才回
      int now = ++inside;
      int old = peak.load();
      while (now > old && !peak.compare_exchange_weak(old, now)) {}
      std::this_thread::sleep_for(100ms);
      --inside;
      slots.release();
      std::osyncstream(std::cout)
          << "task " << i << " : 活干完，名额还回去\n";
    });
  }
  for (auto& t : tasks) t.join();
  std::printf("main: 8 个任务全做完，同时在场的峰值 %d（上限 3）\n", peak.load());

  std::counting_semaphore<> none(0);
  auto t0 = Clock::now();
  bool got = none.try_acquire_for(50ms);
  double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  std::printf("main: 计数 0 上等名额，try_acquire_for 返回 %d，耗时 %.1f ms\n",
              got ? 1 : 0, ms);
  return 0;
}
```

</OnlineCompilerDemo>

```text
task 2 : 活干完，名额还回去
task 0 : 活干完，名额还回去
task 1 : 活干完，名额还回去
task 4 : 活干完，名额还回去
task 3 : 活干完，名额还回去
task 5 : 活干完，名额还回去
task 7 : 活干完，名额还回去
task 6 : 活干完，名额还回去
main: 8 个任务全做完，同时在场的峰值 3（上限 3）
main: 计数 0 上等名额，try_acquire_for 返回 0，耗时 50.2 ms
```

8 行“还回去”的次序由调度排，哪条任务第几个进场每次都会变，结构性的读数是稳定的：8 个任务全做完，同时在场的峰值是 3，上限给多少，峰值就是多少。咱们里头插的几行 `compare_exchange_weak` 只是量峰值的仪表，原子操作的细节归第五单元，您跳过它不影响看主角。

第二段是超时。咱们在计数从 0 开始的信号量上调 `try_acquire_for(50ms)`，等了 50.2 毫秒返回 0。多出来的 0.2 毫秒跟 3.2 的超时一样，来自定时器和调度的精度。返回 `false` 之后是重试还是放弃，照 3.6 的老话，得是代码里明确的一句，demo 里它当场放弃。

`try_acquire()` 是非阻塞版，计数是零立即返回 `false`。有一条得交代：它允许虚假失败（[thread.sema.cnt]/13.3），计数明明是正的也可能返回 `false`，标准注释说这是给基于 compare-and-exchange 的实现留的余地，cppreference 把它跟条件变量的虚假唤醒归成一类。裸的 `try_acquire` 拿名额得套循环，跟 3.1 的裸 `wait` 一个道理。demo 里用的 `try_acquire_for` 内部反复调 `try_acquire`（[thread.sema.cnt]/18），虚假失败它自己多试一轮就消化了，不用咱们操心。

`release` 也有前置条件：加完之后，计数不能越过 `max()`（[thread.sema.cnt]/8）。边界在 binary_semaphore 上最近，它的 LeastMaxValue 是 1，标准只保证 `max()` 不小于它。咱们手头的实现 `max()` 返回的就是模板实参本身，binary 上正是 1，攥着 1 再 `release(2)`，当场越界。

### binary_semaphore：当信号用

`std::binary_semaphore` 是 `std::counting_semaphore<1>` 的别名，cppreference 的页面把两个名字写在同一页。咱们把计数压到 0 和 1 两个值，当配额用太挤，它更多的用场是当信号：构造时给 0，`release` 一手是交货，`acquire` 一手是收货。

<OnlineCompilerDemo
  title="binary_semaphore 当信号：release 交货，acquire 收"
  source-path="code/examples/vol5/chapter03/27_demo_binary.cpp"
  description="sender 先写 payload 再 release，receiver acquire 之后读到的必是 42：release 之前的写对 acquire 之后可见，信号之外不用另加锁。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_binary.cpp —— binary_semaphore 当信号用：release 一方交货，acquire 一方收
#include <cstdio>
#include <semaphore>
#include <thread>

int main() {
  int payload = 0;
  std::binary_semaphore delivered{0};

  std::thread sender([&] {
    payload = 42;          // release 之前写的值
    delivered.release();   // 交货，计数从 0 到 1
  });
  std::thread receiver([&] {
    delivered.acquire();   // 没货就睡，拿到信号才回
    std::printf("receiver: 读到 payload = %d\n", payload);
  });
  sender.join();
  receiver.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
receiver: 读到 payload = 42
```

receiver 读到 42，这个次序有标准撑腰：release 的效果，强发生在观察到它的 `try_acquire` 之前（[thread.sema.cnt]/10），`acquire` 内部反复调 `try_acquire`（[thread.sema.cnt]/15），链路接起来，release 之前的写入对 acquire 之后的读取可见。写数据放在 `release` 之前，读数据放在 `acquire` 之后，数据就跟着信号走。同一场面搬到 3.1 的条件变量上，咱们要一把锁、一个 bool、一次通知，还得把丢失唤醒的防线交代一遍。

跟 mutex 的差别值得咱们单独说一句。mutex 有所有权，谁 lock 谁 unlock，解锁的只能是加锁的线程本身。信号量没有所有权，acquire 和 release 可以发生在不同线程（cppreference 的页面写得直白），demo_binary 正是靠它成立的：release 由 sender 调，acquire 由 receiver 调。代价咱们也要认：信号量拦不住没拿名额的人来还，护数据还得靠 mutex，它管的是几个人能同时进，不管进去的人做什么。

## 结束路径的检查，在这里再过一遍

咱们把 3.6 的检查表拿过来，对三件新工具问下来：等不到头怎么办，停止请求谁来接，人怎么退场。

等不到头怎么办？latch 与 barrier 都没有超时等待。latch 的成员里连非阻塞版都只有看一眼的 `try_wait()`（[thread.latch.class]/12，它还允许极低概率返回虚假的 `false`），`wait_for` 这个名字不存在。barrier 同样只有阻塞的等法。计数减不到零，或者有人到不了场，等待的一方就是死等：初始化线程在 `count_down` 之前崩了，主线程睡在 `ready.wait()` 上，没有谁会来叫它。防线是有的，咱们把 `count_down` 交给一个析构必调的小守卫代管，1.5《谁负责线程的生命周期》里 thread_guard 的思路换个对象再使一遍，活成不成，计数照减，等待方总能等到头。至于活成没成，另拿结果说，不能指望计数替它报信。semaphore 在超时上答得好一些：`try_acquire_for` 与 `try_acquire_until`（[thread.sema.cnt]/18）把超时给了咱们，demo 里 50.2 毫秒的读数就是它，超时的语义跟 3.2 的 `wait_for` 对齐，到期返回 `false`。

停止请求谁来接？3.6 里 `condition_variable_any` 有带 `stop_token` 的重载，三件新工具一件都没有。等待要跟着关停流程散场的场合，它们接不住，咱们得在外围另配，比如等待方收到退出通知后，不再进入本轮同步。

人怎么退场？退场的出口，前面讲 `arrive_and_drop()` 的时候已经交代：调用它的当轮照常减一格，从下一轮起期望计数永久少一。没走它，等的就是那个不会再来的名额。latch 没有对应的出口，名额构造时就定死了，负责递减的一方出事，答案落在等不到头一问里。semaphore 压根没有参与名单，它的退场问题换个名字，叫 release 还有没有人调，动手清单里替它留了一问。barrier 还多一种特有的事故：完成函数抛异常。要求是调用不抛（[thread.barrier.class]/5），要求本身在 barrier 的边界那里咱们已经对过，这里补的是后果：真抛了会怎样，标准没给去处。写不出 `noexcept` 的收尾活，别交给它。

咱们把三问对完，跟 3.6 的检查表也对一遍：假唤醒一行，三件新工具的阻塞等待天生免检，它们等的是计数与 phase 一类的状态，不是通知，醒来条件就成立，没有要重算的谓词。要留神的是另一头的非阻塞探测，`try_wait` 与 `try_acquire` 允许虚假失败，前面各自交代过，拿它们当等待用就得套循环。其余几行，答案都在刚才的正文里。回到选择本身，等待的一方明确没有退出需求时，比如启动阶段的初始化、算法内部的阶段边界，latch 与 barrier 没有超时正合适，它们把等待收窄成计数归零。等待悬在不可预期的外部事件上，网络、输入、用户的耐心，带谓词、带超时、能接停止请求的条件变量仍然是对的选择。

## 场景选择：各自适合表达什么

四类协作需求各归各家，咱们把判断依据写全，表单独拿起来也能用：

| 协作需求 | 归谁 | 判断依据 |
|---|---|---|
| 等一轮计数 | `std::latch` | 等的是计数归零，归零只发生一次，减格子的与等着的可以不是同一批人 |
| 阶段齐步 | `std::barrier` | 一批固定的人反复到齐再一起走，每轮之间还有一次集中交接的机会（完成函数） |
| 资源配额 | `std::counting_semaphore` | 上限写进构造，acquire 拿名额，release 还，拿与还可以发生在不同线程 |
| 条件等待 | mutex 加条件变量 | 谓词随便写，条件能复合、能来回变，超时与停止请求都有现成接口 |

表外的判断法，头一条是复合条件。复合条件轮不到前三者：latch 和 semaphore 只认计数，barrier 认的是人数，“队列非空或已关闭”这样的谓词它们罩不住。3.3 起的队列是现成的例子，空了等、满了等、关了要 drain，三个条件咬在一起，当时用的就是两把条件变量。拿两个信号量重写 3.3 起的队列也通，一个数货、一个数空位，可关闭语义一进来，drain 这个复合条件又得靠谓词，咱们又绕回条件变量。

选型时笔者认一条：哪一行的语义跟需求对得上，就用谁。语义对得上，3.1 和 3.6 积累的几问就只剩工具边界内的事，不用您再补防线。硬凑的搭配，比如拿 latch 冒充反复齐步，每轮重建对象、重建参与名单的活都得自己补，补进去的胶水早晚要还。

## 没有 C++20 怎么办：mutex 与条件变量模拟（扩展）

> 本节是扩展，您跳过它不影响主线。手头停在 C++11 或 C++17 的项目，等一轮计数、反复齐步、限制并发上限照样做得出来，本节用 mutex 加条件变量把三件工具模拟一遍，顺带看一眼 `std::barrier` 内部的机关长什么样。三个类本体 C++11 就能编。驱动的打印借了 C++20 的 osyncstream，时长字面量是 C++14 的。C++11 项目把打印换回 printf，再把字面量换回 std::chrono::milliseconds，就齐了。

LatchSim 咱们直接写：计数器进谓词，归零就通知。latch 本尊没有通知，模拟版是状态加通知的合体，通知丢了也不怕，谓词兜着，3.1 丢失唤醒的防线，模拟版原样照搬。

BarrierSim 难在可重用。咱们把计数归零就清零重置的写法摆出来看一眼，轮次会串：上一轮还没放行的等待者看的是同一个计数，新一轮一来就把人搅了。解法是代际计数，每个等待者在锁内记下自己属于哪一代，等待的条件写成“代数变没变”。`std::barrier` 的 phase 靠的也是同一套机关。

CountingSemaphoreSim 咱们几行就立住了：谓词就是 `counter_ > 0`，acquire 锁内等够了再减，release 锁外加回去再叫人。

三个类加一段把三个模拟各验收一遍的驱动，全文贴在下面，您在本地拼成单个文件就能复跑：

<OnlineCompilerDemo
  title="mutex + 条件变量模拟 latch、barrier、信号量"
  source-path="code/examples/vol5/chapter03/28_demo_simulation.cpp"
  description="三个工具各搭一个朴素版跑验收：倒计时归零放行、按代数轮换的屏障、8 抢 3 的名额。模拟器只为看清语义，正文拿它们对照库工具。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_simulation.cpp —— mutex + 条件变量模拟三个工具，各跑一遍验收
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

class LatchSim {
public:
  explicit LatchSim(std::ptrdiff_t count) : count_(count) {}
  void count_down(std::ptrdiff_t update = 1) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      count_ -= update;
    }
    if (count_ <= 0) cv_.notify_all();   // 归零才叫，锁外叫醒
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return count_ <= 0; });
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t count_;
};

class BarrierSim {
public:
  explicit BarrierSim(std::ptrdiff_t expected)
      : initial_(expected), count_(expected) {}
  void arrive_and_wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    std::size_t gen = generation_;       // 锁内记住自己属于哪一代
    if (--count_ == 0) {
      ++generation_;
      count_ = initial_;
      lock.unlock();
      cv_.notify_all();
    } else {
      cv_.wait(lock, [&] { return gen != generation_; });
    }
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t initial_;
  std::ptrdiff_t count_;
  std::size_t generation_ = 0;
};

class CountingSemaphoreSim {
public:
  explicit CountingSemaphoreSim(std::ptrdiff_t desired) : counter_(desired) {}
  void acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return counter_ > 0; });
    --counter_;
  }
  void release(std::ptrdiff_t update = 1) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      counter_ += update;
    }
    cv_.notify_all();
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::ptrdiff_t counter_;
};

int main() {
  {   // LatchSim：三件初始化活，一个等待方
    LatchSim ready(3);
    std::vector<std::thread> inits;
    for (int i = 0; i < 3; ++i) {
      inits.emplace_back([&ready, i] {
        std::this_thread::sleep_for(10ms * (3 - i));
        std::osyncstream(std::cout) << "[latch sim] 活 " << i << " 完成\n";
        ready.count_down();
      });
    }
    ready.wait();
    std::printf("[latch sim] 三件活齐了，往下走\n");
    for (auto& t : inits) t.join();
  }

  {   // BarrierSim：两条线程两轮齐步
    BarrierSim sync(2);
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) {
      workers.emplace_back([&sync, i] {
        for (int p = 1; p <= 2; ++p) {
          std::this_thread::sleep_for(10ms * (p + i));
          sync.arrive_and_wait();
          std::osyncstream(std::cout)
              << "[barrier sim] worker " << i << " 第 " << p << " 轮过线\n";
        }
      });
    }
    for (auto& t : workers) t.join();
    std::printf("[barrier sim] 两轮走完\n");
  }

  {   // CountingSemaphoreSim：8 抢 3，量峰值
    CountingSemaphoreSim slots(3);
    std::atomic<int> inside{0};
    std::atomic<int> peak{0};
    std::vector<std::thread> tasks;
    for (int i = 0; i < 8; ++i) {
      tasks.emplace_back([&] {
        slots.acquire();
        int now = ++inside;
        int old = peak.load();
        while (now > old && !peak.compare_exchange_weak(old, now)) {}
        std::this_thread::sleep_for(10ms);
        --inside;
        slots.release();
      });
    }
    for (auto& t : tasks) t.join();
    std::printf("[semaphore sim] 8 个任务做完，峰值 %d（上限 3）\n", peak.load());
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
[latch sim] 活 2 完成
[latch sim] 活 1 完成
[latch sim] 活 0 完成
[latch sim] 三件活齐了，往下走
[barrier sim] worker 1 第 1 轮过线
[barrier sim] worker 0 第 1 轮过线
[barrier sim] worker 1 第 2 轮过线
[barrier sim] worker 0 第 2 轮过线
[barrier sim] 两轮走完
[semaphore sim] 8 个任务做完，峰值 3（上限 3）
```

三段验收一次跑完。latch 段三件活齐了主线程才往下走。barrier 段过线行按轮成对出现，前后次序由调度排。semaphore 段峰值 3，模拟版的计数守得住上限。模拟版咱们写到一百三十行上下，比三件本尊多出来的就是锁、谓词和代际管理，C++20 把它们交给了库。

## 轮到您动手的时候

写一处协作，动笔之前笔者会把几问过一遍：

1. 等的是一件事发生一次，还是反复发生的节拍？答案在 latch 与 barrier 之间分岔。
2. 等的一方有没有结束需求？超时、停止，各自的答案落在哪一句？latch 与 barrier 接不了，您的外围得有别的安排。
3. 参与的人数从构造到结束是同一批吗？有人中途退场，退场走的是哪一行？
4. 拿信号量做配额，每一条路径上 acquire 都有配对的 release 吗？异常抛出去，名额还回来了吗？
5. 条件变量的版本要写多少行？多出来的行买到了什么，复合条件、超时，还是停止请求？

## 下一步

条件变量、有容量的队列、结束路径、三个协作工具，七篇接的是同一条线：等什么，谁来叫，等不到怎么办。

## 参考资源

推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是笔者自己写的。峰值、超时的返回值与耗时、binary 的读数属于结构性的结果，每次一致。cppreference 的 latch、barrier、counting_semaphore 三页本回都抓到了全文，与草案核对无出入，LeastMaxValue 标着 implementation-defined，barrier 完成函数的默认类型，草案只写 unspecified，cppreference 写 see below，本篇没有依赖它们的具体值。

- [std::latch（cppreference）][cppref-latch]，C++20 起，单次使用，没有超时等待。
- [std::barrier（cppreference）][cppref-barrier]，C++20 起，phase 模型与完成函数。
- [std::counting_semaphore 与 std::binary_semaphore（cppreference）][cppref-semaphore]，binary_semaphore 是 counting_semaphore 的特化别名（LeastMaxValue 为 1），try_acquire 允许虚假失败，acquire 与 release 可以在不同线程。
- [Class latch（现行草案 thread.latch）][draft-latch]，单次使用的定义、count_down 的前置条件、wait 的状态检查、arrive_and_wait 的等效展开、try_wait 的虚假 false。
- [Class template barrier（现行草案 thread.barrier）][draft-barrier]，phase 的三步、完成步的同步关系、is_nothrow_invocable_v 要求、完成步中调用其他成员的未定义行为、arrive_and_drop 的参与数调整。
- [Class template counting_semaphore（现行草案 thread.sema.cnt）][draft-sema]，acquire 与 release 的效果和前置条件、try_acquire 的虚假失败、try_acquire_for 的超时语义、release 对观察者的强发生关系。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[cppref-barrier]: https://en.cppreference.com/w/cpp/thread/barrier
[cppref-semaphore]: https://en.cppreference.com/w/cpp/thread/counting_semaphore
[draft-latch]: https://eel.is/c++draft/thread.latch
[draft-barrier]: https://eel.is/c++draft/thread.barrier
[draft-sema]: https://eel.is/c++draft/thread.sema.cnt
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
