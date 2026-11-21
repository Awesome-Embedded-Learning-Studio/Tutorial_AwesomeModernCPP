---
title: "自行传递结果：`std::promise` 与 `std::future`"
description: "promise 把供值端交到您手里：get_future 结成一对，set_value 供值、set_exception 装异常并在 get 处原样重抛，broken_promise 收掉没兑现的场，多级接力与 shared_future 广播画出结果的路线。"
chapter: 5
order: 3
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

# 自行传递结果：`std::promise` 与 `std::future`

与 async 的对照一句话就立得住：async 替您管着两端，您发起任务以后只管等。promise 把供值端交到您手里，什么时候给值、给什么、失败了装什么，都由您安排。执行端反而更自由了，起线程也行，不起线程也行，下面的实验会演示到。

## 谁执行工作，谁提供结果

分工从名字认起。往共享状态里放结果的对象，标准叫异步提供者，4.1 认过，async、promise、packaged_task 都在提供者一列，[futures.state]/4 的例句直接写着 promises 和 tasks。future 与 shared_future 叫异步返回对象，管读。promise 与 future 经 `get_future` 结成一对，两端挂同一段共享状态。前三个单元里您亲手搭的桥就是垫底的形状，只是当时锁、标志、通知全得您自己来，如今垫底的活库干了。

咱们把最小的一对搭起来。main 建好 promise，`get_future` 拿到 future 留在自己手里，promise 用 `std::move` 移交给干活的线程，线程干完活调 `set_value` 把结果供进去，main 的 `get` 等到值，返回：

<OnlineCompilerDemo
  title="promise 供值，future 取值：两端都在咱们手里"
  source-path="code/examples/vol5/chapter04/13_demo_pair.cpp"
  description="worker 线程数完偶数调 set_value 供进 promise，main 的 get 等到值返回，后半段 promise 不移交线程，当场供、当场取，全程没有第二条线程。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_pair.cpp —— promise 供值，future 取值：两端都在咱们手里
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

int count_even(const std::vector<int>& xs) {
  int n = 0;
  for (int x : xs) {
    if (x % 2 == 0) ++n;
  }
  return n;
}

void worker(std::promise<int> out) {
  std::vector<int> xs{7, 2, 9, 4, 6, 1, 8, 3};
  out.set_value(count_even(xs));   // 干完活，把结果供进 promise
}

int main() {
  std::promise<int> p;
  std::future<int> f = p.get_future();   // 供值端移动给线程，future 留在 main
  std::thread t(worker, std::move(p));

  int n = f.get();
  std::printf("main: get 到 %d 个偶数，valid() = %d\n", n, f.valid() ? 1 : 0);
  t.join();

  // 供值端不开线程也能用：当场供、当场取
  std::promise<int> here;
  std::future<int> fh = here.get_future();
  here.set_value(42);
  std::printf("main: 当场供的值 %d，全程没动过一条线程\n", fh.get());
  return 0;
}
```

</OnlineCompilerDemo>

```text
main: get 到 4 个偶数，valid() = 0
main: 当场供的值 42，全程没动过一条线程
```

咱们对着输出读分工。执行工作的，是您亲手起的线程，第一单元的手艺原样在用，起线程、join 线程都还是您的事。提供结果的是线程函数手里攥着的 promise，数完的偶数个数由它装进共享状态。等待取值的活在 main 手里的 future 上，`get` 堵到就绪，返回之后 `valid()` 翻成 0，4.1 讲过的一次性消耗在这里照旧生效。三份活分给三个角色，谁干什么，代码里一眼看得清。

供值端不绑线程，值得单独立一段。demo 的后半截，promise 没有移交给任何人，main 当场 `set_value`、当场 `get`，全程没有第二条线程。您无需想得复杂，其实，promise 与 future 就是一段共享状态的两端，供值与取值是两头各自的能力，谁执行工作反而是外挂的安排。async 的写法里，两端加上执行全由库一家包办。promise 的写法里，供值端交到您手里，执行端您想怎么排就怎么排。

两端的额度都有标准看着。`get_future` 一回，`set_value` 一回，超了都会抛 `future_error`。笔者把探针跑了：

<OnlineCompilerDemo
  title="get_future 与 set_value 都只有一回的额度"
  source-path="code/examples/vol5/chapter04/14_probe_rules.cpp"
  description="第二回 get_future 抛 future_already_retrieved，第二回 set_value 抛 promise_already_satisfied，libstdc++ 的两条消息都把事由说清了。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// probe_rules.cpp —— get_future 与 set_value 都只有一回的额度
#include <cstdio>
#include <future>

int main() {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  (void)f;
  try {
    std::future<int> again = p.get_future();   // 第二回
    (void)again;
  } catch (const std::future_error& e) {
    std::printf("第二回 get_future 抛 future_error：%s（future_already_retrieved？%s）\n",
                e.what(),
                e.code() == std::future_errc::future_already_retrieved ? "是" : "否");
  }

  p.set_value(1);
  try {
    p.set_value(2);                            // 第二回
  } catch (const std::future_error& e) {
    std::printf("第二回 set_value 抛 future_error：%s（promise_already_satisfied？%s）\n",
                e.what(),
                e.code() == std::future_errc::promise_already_satisfied ? "是" : "否");
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
第二回 get_future 抛 future_error：std::future_error: Future already retrieved（future_already_retrieved？是）
第二回 set_value 抛 future_error：std::future_error: Promise already satisfied（promise_already_satisfied？是）
```

条款写得明白，咱们逐条对一遍。`get_future` 返回一个 future，与 promise 共用同一段共享状态（[futures.promise]/13）。同一份状态上已经取过一回再调，它就抛 `future_error`，错误条件是 `future_already_retrieved`（/14、/15.1）。`set_value` 把值原子地存进共享状态、把状态变就绪（/16），状态里已经存着值或异常再装，错误条件 `promise_already_satisfied`（/18.1）。本机的 libstdc++ 两处都照着推荐做法抛了，消息还把事由说清了，输出里印着。额度对称得很：一端供一回，一端取一回，两端合起来恰好走完一段结果的路。顺手补一条结对的自由：`get_future` 与 `set_value` 彼此不构成数据竞争（[futures.promise]/12），取 future 的时刻早一点晚一点都行。

promise 也是只移动的类型。拷贝构造在类定义里标着 `= delete`（[futures.promise] 的类定义），移动构造把共享状态的归属转过来（/5），移出去的一方手里没有共享状态（/6）。笔者头一回把它传线程的时候忘写 `std::move`，编译器当场把拷贝构造已删除摆到眼前，想含糊都含糊不了。空着手的 promise 再去 `set_value`，抛的 `future_error` 错误条件是 `no_state`（/18.2）。权力随移动交接，跟 4.1 里 future 的脾气一样：供值的权力整个在一个人手里，“供过没有”才有一个干净的答案。

## 失败的传递路径

3.6 立过的两个条件，您还记得吧：上游干砸了，得有地方记录。等待的一方，得读得到。当时的答案全是手工的，`failed` 标志配谓词，`main` 替认输的一方立标志。库的正路从记录一侧接手，入口是 `set_exception`：异常对象从工作线程一路带进共享状态，在 `get` 的调用点原样重抛。

路径咱们走一遍。工作线程里 `throw` 出异常，自己的 `catch (...)` 把它接住，`std::current_exception()` 把手里的异常装成一根 `exception_ptr`，交给 `set_exception`，条款写的效果是原子地存进共享状态、把状态变就绪（[futures.promise]/20）。等待一方的 `get` 等到就绪，把存下的异常重抛，抛的就是存进去的异常本身（[futures.unique.future]/19，4.1 核对过的条款，这里接着用）。类型、消息，全程不变，标准保的就是原样。

咱们接着拿除法当靶子，异常消息沿用 4.1 的演示，好让两篇的输出能对上：

<OnlineCompilerDemo
  title="set_exception：失败从工作线程走到 get 手里"
  source-path="code/examples/vol5/chapter04/15_demo_exception.cpp"
  description="坏除法在 worker 里抛的 invalid_argument 经 current_exception 装进共享状态，在 get 的调用点原样重抛，消息一字不差，好除法照常供值。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_exception.cpp —— set_exception：失败从工作线程走到 get 手里
#include <cstdio>
#include <exception>
#include <future>
#include <stdexcept>
#include <thread>

void divide(std::promise<int> out, int a, int b) {
  try {
    if (b == 0) {
      throw std::invalid_argument("除数为零，除法做不下去");
    }
    out.set_value(a / b);
  } catch (...) {
    out.set_exception(std::current_exception());   // 手里的异常原样装进共享状态
  }
}

void run(const char* tag, int a, int b) {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  std::thread t(divide, std::move(p), a, b);
  try {
    std::printf("%s: %d / %d = %d\n", tag, a, b, f.get());
  } catch (const std::invalid_argument& e) {
    std::printf("%s: 捕获 invalid_argument，what() = %s\n", tag, e.what());
  }
  t.join();
}

int main() {
  run("坏除法", 6, 0);
  run("好除法", 6, 3);
  return 0;
}
```

</OnlineCompilerDemo>

```text
坏除法: 捕获 invalid_argument，what() = 除数为零，除法做不下去
好除法: 6 / 3 = 2
```

两行输出都出在 `get` 的调用点。坏除法，`divide` 里 `throw` 的 `invalid_argument` 跑进了 main 的 `catch`，类型对得上，消息一字不差，路是 4.1 里 async 走过的同一条，只是供值的手从库换成了您。好除法照常，`set_value` 供的值从 `get` 出来。咱们手写的就两行：`catch (...)` 一行，`set_exception(std::current_exception())` 一行。3.6 里锁、标志、谓词、通知一整套手工桥，到这里退役。

有几处边界请您留意。装进 `set_exception` 的指针不能是空的，条款把它写成前置条件：p 非 null（[futures.promise]/19），`current_exception()` 在 `catch` 里给的正好是真指针，照着上面的姿势写就不会落空。值与异常共享同一个额度：状态里已经装了异常再调 `set_value`，或者反过来，都是 `promise_already_satisfied`，/18.1 的措辞是“值或异常”，探针里验过 `set_value` 的款，`set_exception` 的款同一条判罚写在 /22.1。还有一个方向咱们得看清：装了异常没人取，它就停在共享状态里，随状态销毁。4.1 对 async 版说过：记录发生在库一侧，读永远在您一侧。promise 版照旧成立。

兑现不了的场，库也安排了收尾。提供的一方销毁时状态还没就绪，标准管它叫**弃掉**（abandon）：把一根 `future_error`、错误条件 `broken_promise` 存进状态，再把状态变就绪（[futures.state]/7）。等待一方的 `get` 立刻收到一句明确的错误，不会悬着等一个永远不会来的人。后果 4.2 的扩展节演示过，`get` 抛出 `future_error`、错误条件比对是 `broken_promise`，读数都在 4.2 的正文里，咱们不重演。失败路径上它有实职：干活的一方散场之前忘了供值，等待的一方收到的是一句能定位的抱怨。

> `set_value_at_thread_exit` 与 `set_exception_at_thread_exit` 是两位变体，装结果的动作当场做，状态就绪的时刻拖到线程退出、线程本地的对象析构完之后（[futures.promise]/23、/27，[futures.state]/10）。清理顺序有讲究的场合它们才出场，主线用不上，您认识一下就行。

## 结果传递链的组织模式

供值端到了手，结果的路线图归您画。

### 多级交接

链的形状咱们直接摆出来。A 阶段供一组数据，B 阶段一手接 A 的 future、一手供 C 的 promise，C 阶段收尾打印：

<OnlineCompilerDemo
  title="结果传递链：A 供数据，B 接上算总和，C 收尾打印"
  source-path="code/examples/vol5/chapter04/16_demo_chain.cpp"
  description="三条线程两个共享状态：B 的 get 天生等 A，C 的 get 天生等 B，main 只管起齐 join 齐，锁与通知一概没有出场。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_chain.cpp —— 结果传递链：A 供数据，B 接上算总和，C 收尾打印
#include <cstdio>
#include <future>
#include <numeric>
#include <thread>
#include <vector>

void stage_a(std::promise<std::vector<int>> out) {
  out.set_value(std::vector<int>{3, 1, 4, 1, 5, 9, 2, 6});
}

void stage_b(std::future<std::vector<int>> in, std::promise<int> out) {
  std::vector<int> xs = in.get();   // 一手接上游的 future
  out.set_value(std::accumulate(xs.begin(), xs.end(), 0));   // 一手供下游的 promise
}

void stage_c(std::future<int> in) {
  std::printf("stage C: 收到总和 %d\n", in.get());
}

int main() {
  std::promise<std::vector<int>> ab;
  std::future<std::vector<int>> f_ab = ab.get_future();
  std::promise<int> bc;
  std::future<int> f_bc = bc.get_future();

  std::thread ta(stage_a, std::move(ab));
  std::thread tb(stage_b, std::move(f_ab), std::move(bc));
  std::thread tc(stage_c, std::move(f_bc));
  ta.join();
  tb.join();
  tc.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
stage C: 收到总和 31
```

中间的 B 是链上的枢纽，参数表里写得很清楚：`std::future<std::vector<int>>` 进，`std::promise<int>` 出。`get` 等 A 的货，算完总和装进自己的 promise。等待的顺序不用您排，B 的 `get` 天生就在等 A，C 的 `get` 天生就在等 B，`main` 只管把三条线程起齐、join 齐。第三单元的锁、谓词、通知一概没有出场，同步的保底在条款里：往共享状态存结果的操作，与共享状态上等待函数的成功返回，彼此同步（[futures.state]/9）。A 存的值 B 看得见，B 存的值 C 看得见，管事的正是 4.1 里认过的共享状态。等会儿，整条链能不能按序交接，就取决于它了！

失败也沿链走。B 的 `get` 要是收到 A 装进来的异常，您有两条路可选：就地接住处理，或者原样往下游装，`catch (...)` 加 `set_exception(std::current_exception())` 的姿势原样搬。走哪条由链的设计定，唯一不能省的是等待一方的 `catch`，异常走到谁手里，谁就得接得住。

级数一多，手工链的成本就显出来了。每一级一对 promise 加 future，装值装异常全手写，您会想念能替您包下杂活的壳子。`std::packaged_task` 干的就是它，本篇不展开，4.4 见。

### 一处供值，多处取用

shared_future 的取值规则 4.1 交代过：可拷贝，`get` 交 const 引用，取完还在，异常照重抛。供值的一端当时是 async 的任务，现在换成您手里的 promise，场面立刻完整：一处供值，多处取用，供值的时刻由您定。

<OnlineCompilerDemo
  title="一处供值，多处取用：promise 配 shared_future"
  source-path="code/examples/vol5/chapter04/17_demo_broadcast.cpp"
  description="三条 worker 各攥一份 shared_future 拷贝去 get，promise 留在 main 手里，set_value 一落三个等的人一起醒。worker 三行的次序由调度排。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_broadcast.cpp —— 一处供值，多处取用：promise 配 shared_future
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  std::promise<std::string> ready;
  std::shared_future<std::string> config = ready.get_future().share();

  std::vector<std::thread> pool;
  for (int i = 0; i < 3; ++i) {
    pool.emplace_back([config, i] {            // 每条线程一份自己的拷贝
      const std::string& line = config.get();  // 没就绪就等，就绪了一起醒
      std::printf("worker %d: 读到 %s\n", i, line.c_str());
    });
  }

  std::this_thread::sleep_for(150ms);   // 顶替一段准备工作
  ready.set_value("pool_size = 4");     // 供值端在 main 手里，什么时候给由咱们定
  for (auto& t : pool) t.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
worker 0: 读到 pool_size = 4
worker 2: 读到 pool_size = 4
worker 1: 读到 pool_size = 4
```

main 起好三条 worker，各自攥一份 shared_future 的拷贝去 `get`，没就绪就等。promise 留在 main 手里，睡 150 毫秒顶替一段准备活，`set_value` 一落，三个等的人一起醒，各自拿到 `pool_size = 4`。worker 三行的次序由调度排，笔者重跑了三遍，读到 0、1、2 与 0、2、1 两种排法，三行永远齐全，它们都堵到 `set_value` 放行才出来。

把 4.1 的版本摆在一起，差别就落在供值端上。async 版本里，结果什么时候就绪由任务的活长短说了算，`main` 只能等。promise 版本里，供值端攥在您手里，什么时候给、给什么，您安排。`promise<void>` 更干脆，`set_value()` 一个参数都不收，纯发令枪，主模板与 void 特化只在 `set_value` 与 `set_value_at_thread_exit` 的参数上有差别（[futures.promise]/2）。配置就绪、初始化完成、开工号令，一次性的广播场面它都顺手。反复供值的通道别拿它凑，额度用完就废，反复的活归条件变量与队列，第三单元在等您。

### 与前两篇的边界收拢

单元走到这里，执行方式的地盘划清了。`std::thread` 管线程本身，起、等、停都在第一单元的手艺里。async 管两端，发起即执行，供值与传异常都由库接管，什么时候跑归 4.2 的策略。promise 只管供值端，把结果装进共享状态的权力整个交给您，执行端随您安排。三种工具各有各的管区，混在一段代码里也不打架，因为各自的接口只管各自的一端。

packaged_task 的去留也交代清楚。它站在 promise 的肩膀上，把您手写的 `try`、`catch`、`set_value`、`set_exception` 接成自动的，任务由此变成能存进容器、能传给别人的对象，线程池的底座就是它。它是 4.4《把计算包装成可存储分发的任务》的主角，还没成篇。旧目录 ch05 的素材文把它跟 promise 挤在同一篇里，迁移之后各归各的节。线程池写进 cookbook 选读，还没成文，不进正文。所以本篇的主线收在 promise 与 future 上，一端供、一端取，格局到这里就齐了。

析构的性格也归拢回出身。4.2 的实验里，promise 的 future 析构 0.0 毫秒，谁也不等，当时的伏笔在本篇兑现：promise 建出来的共享状态里没有线程对象要收，放手即走，只在没人兑现时留下 `broken_promise` 一句交代。async 的状态里住着线程，得等任务跑完再收，promise 的状态里只有结果，不等。两种出身您如今都见过了。

## 轮到您动手的时候

动笔之前，笔者把本篇攒下的问题摆成一列，您对着自己的代码过一遍：

1. 供值端在谁手里？干活的一方散场之前，`set_value` 或者 `set_exception` 一定有人调吗？没人调的话，等待一方收到的是 `broken_promise`，您打算把它当错误报出去，还是当流程正常的一站？
2. 装异常的姿势对吗？`catch (...)` 加 `std::current_exception()`，接的一方 `catch` 的类型具体到能对上 `what()` 吗？
3. `get_future` 与 `set_value` 的额度都只有一回，代码里有没有可能走到第二回的路径？分支与循环里尤其要查。
4. 传递链的每一级，上游的异常是就地处理还是往下游装？链走到头，总得有人接住，接的人找到了吗？
5. 一处供多处取的场面，您用的是 shared_future 还是各发一份 async？后一种把同样的活干了几遍，值得吗？

3.6《[等待也需要结束路径](../chapter03-wait-and-coordinate/06-ending-a-wait)》里靠 `failed` 标志散场的等待程序，请您翻出一份，把失败的腿改走 promise：上游接住异常，`set_exception` 装进共享状态，等待的一方改 `try` 加 `catch`。改完对照着数一数：标志、锁、谓词，哪几样退了场，异常的类型与消息有没有原样走到接的人手里。数完您就知道，3.6 的留话，正路修到哪一步了。

## 参考资源

写本篇的时候，推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，promise 与 packaged_task 相邻成章的安排给本篇的取舍做了参照，packaged_task 划去 4.4 的边界则是既定的分工。例子与文字都是笔者自己写的，编译时另加了 `-Wall -Wextra`，无警告。探针里 libstdc++ 的两条 `future_error` 消息是本机实现给的，别的实现可以措辞不同，错误条件的比对才是标准保的。demo_broadcast 里 worker 三行的次序由调度排，笔者累计跑了十三遍，正文引的头三遍读到 0、1、2 与 0、2、1 两种排法，修订时补跑的十遍全部读到 0、1、2，三行都堵到 `set_value` 放行才出来，worker 0 十三遍都在最前，仅为本机观察。文中条款的引用格式是方括号里放小节名，斜杠后面放段号，小节名跟正式标准一致，段号可能挪。cppreference 的 promise 与 future_errc 两页抓到了全文，与草案对读无出入。shared_future 的组合条款按草案 [futures.shared.future] 核对，4.1 核过的取值规则没有重核。`future::get` 重抛异常的条款沿用 4.1 的核对。旧目录 ch05 的《promise 与 packaged_task》是本篇的素材底子，例子、实验与条款核对都是重新做的，packaged_task 的部分留给 4.4。

- [std::promise（cppreference）][cppref-promise]，C++11 起，只移动，供值端，存值与等待返回之间的同步关系。
- [std::future_errc（cppreference）][cppref-errc]，四个错误码：broken_promise、future_already_retrieved、promise_already_satisfied、no_state。
- [Class template promise（现行草案 futures.promise）][draft-promise]，类定义与只移动、get_future 的一次性额度、set_value 与 set_exception 的效果与错误条件、at_thread_exit 两个变体。
- [Shared state（现行草案 futures.state）][draft-state]，异步提供者的定义、abandon 与 broken_promise、结果存入与等待返回之间的同步。
- [Class template shared_future（现行草案 futures.shared.future）][draft-shared]，拷贝共享同一段状态、从 future 转换构造、get 交 const 引用与它的生存期注。
- [Class template future（现行草案 futures.unique.future）][draft-unique]，get 重抛异常的条款，4.1 核对，本篇沿用。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-promise]: https://en.cppreference.com/w/cpp/thread/promise
[cppref-errc]: https://en.cppreference.com/w/cpp/thread/future_errc
[draft-promise]: https://eel.is/c++draft/futures.promise
[draft-state]: https://eel.is/c++draft/futures.state
[draft-shared]: https://eel.is/c++draft/futures.shared.future
[draft-unique]: https://eel.is/c++draft/futures.unique.future
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
