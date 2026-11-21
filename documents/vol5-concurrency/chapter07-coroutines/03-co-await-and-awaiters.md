---
title: "`co_await` 怎样参与等待"
description: "co_await 表达式按什么协议展开，awaiter 三件套各答哪一问，一扇带值的门怎样写，operator co_await 与 await_transform 在哪里进场，对称转移又怎样把栈深从链深上解下来。"
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# `co_await` 怎样参与等待

本篇只围着 `co_await` 转：您写下它，编译器替您问了一串问题，问谁、问什么、答案怎样改写控制流，咱们一件件看。

四个实验的代码块就是全部内容，拼成单个 .cpp 文件即可复跑。两个探针只求报错信息，报错原文已经贴在正文里，没有可运行的输出。

## awaiter 与 awaitable 协议

`co_await expr`，咱们拿一句话读它：走到它，编译器从 expr 手里弄到一个对象，让它来回答三个问题。准备好了吗？没准备好，挂起之后干什么？恢复的时候，带什么回来？三个问题对应三个成员函数，`await_ready`、`await_suspend`、`await_resume`，这就是三件套。

术语也在这里对齐。能出现在 `co_await` 后面的类型，叫 **awaitable**。真正带着三件套接手展开的，叫 **awaiter**。一个类型可以两者兼任，Park、标准库的挂起体、咱们等会儿要写的 Gate，都兼着两个身份。

标准的说法比一句话读法细。展开一条 await 表达式，草案 [expr.await] 列了一组编号步骤，咱们按顺序走一遍（条目号对齐 2026-11-17 访问的现行草案）：

1. 操作数照常求值。promise 的类型里要是找得到 `await_transform`，expr 得过它的手，出来的才算数。初始挂起点、最终挂起点与 `co_yield` 产生的等待表达式不经过它，promise 拦不住语言自己安排的三次挂起（[expr.await]/(3.2)）。
2. 找 `operator co_await`。成员版本与非成员版本一起进一场重载决议，选不出唯一的一个，程序不合法。可行的候选一个都没有，操作数自己就当 awaiter（[expr.await]/(3.3)，候选的组成规则在 [over.match.oper]/3 与 /7）。
3. 求值 `await_ready()`，语境转换成 bool（[expr.await]/(3.6)）。返回 true，直接跳到第 6 环，什么也不挂。
4. 返回 false：协程视为已挂起，然后求值 `await_suspend(h)`，h 是指回本协程的句柄（[expr.await]/(5.1)）。挂起发生在 await_suspend 进场之前，您在它里面看见的协程，已经是挂好的。
5. `await_suspend` 的返回类型只有三样合法：void、bool、`coroutine_handle<Z>`（[expr.await]/(3.7)）。返回 void，控制流交回本协程的调用者或恢复者。返回 bool，求值结果为 false 时协程当场恢复。返回句柄，接着求值它的 `.resume()`，草案在注 (5.1.1) 里写明，一串协程可以一路接力恢复下去，最终把控制流还给最初的调用者或恢复者。要是 await_suspend 中途抛了异常，异常被接住，协程被恢复，异常不当场抛给调用者或恢复者，而是在协程体内重新抛出（[expr.await]/(5.1)）。
6. 恢复时求值 `await_resume()`，第 3 环 ready 为 true 的话当场就求值，它的返回值就是整个 `co_await` 表达式的值（[expr.await]/(5.2)），表达式的类型与值类别跟它一致（(4)）。

零件摆齐了，回头把第一篇的 Park 请出来，现在您可以逐行读它：

<OnlineCompilerDemo
  title="Park 的三件套：挂起登记，等别人来恢复"
  source-path="code/examples/vol5/chapter07/06_park_awaiter.cpp"
  description="片段补全成可运行文件：worker 跑到 co_await Park{} 挂起登记，main 数完句柄把它恢复，worker 从挂起点接着跑完。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// park_awaiter.cpp —— Park：三件套的最小实现，挂起登记，等别人来恢复
#include <coroutine>
#include <cstdio>
#include <exception>
#include <vector>

struct Park {
  static std::vector<std::coroutine_handle<>> parked;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) { parked.push_back(h); }
  void await_resume() {}
};
std::vector<std::coroutine_handle<>> Park::parked;

// 最小任务类型，撑起 co_await（急启动，7.1 的同款）
struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task worker() {
  std::printf("  worker: 跑到 co_await Park{}，挂起登记\n");
  co_await Park{};
  std::printf("  worker: 被恢复，从挂起点接着跑\n");
}

int main() {
  Task t = worker();
  std::printf("main: 登记了 %zu 个句柄\n", Park::parked.size());
  Park::parked.back().resume();
  std::printf("main: worker 完工，程序结束\n");
  return 0;
}
```

</OnlineCompilerDemo>

三行成员，正好是三问的答案，咱们挨个对。`await_ready` 永远返回 false，Park 不知道事情什么时候来，干脆每次都挂。`await_suspend` 把句柄登记进列表，登记完什么都不做，控制流交回恢复者，协程就在列表里睡着。`await_resume` 空空如也，Park 等的只是“轮到我了”，再没有别的要带。第一篇的一幕里恢复它的是 driver 线程，谁恢复、什么时候恢复，Park 一概不管。void 返回类型的用法就在这里：交接完全交给外面。

标准库的挂起体也在同一份协议上，上一篇全篇在用它们：`suspend_always` 的 `await_ready` 永远是 false，`suspend_never` 的永远是 true，两个的 `await_suspend` 与 `await_resume` 都什么也不做（[coroutine.trivial.awaitables]）。您现在可以自己看出一件事来，它们就是三件套的最小实现，跟 Park 是同类东西。

有个容易读岔的地方，咱们停下来交代。`await_ready` 返回 true，意思是“别挂，继续”。`await_suspend` 返回 true，意思却是“挂”。两个函数都在回答挂不挂，答案的方向是反的。笔者头一回读到 bool 版本的 await_suspend 就在这里愣了一下，后来找到顺口的记法：ready 问的是“你好了吗”，好了自然不用挂。suspend 问的是“挂吗”，true 才挂。

bool 返回类型也不是怪口味，它有正当的活干。咱们看一个典型的场景：await_suspend 里打算登记句柄，登记的动作有可能跟完成事件赛跑，另一条线程恰好在这当口把事情办完了，返回 false，协程当场继续，一场无谓的挂起与恢复全省下来。Lewis Baker 的 operator co_await 一篇给过一个手工复位事件的完整实现，bool 版本正是为这里的竞态窗口准备的。

## 实现一个最小可等待对象

协议读熟了，咱们亲手写一个会等东西的对象。目标定得小：一扇带值的门。`co_await gate` 的协程，门里没值就挂起登记，值来了被叫醒，醒来手里拿着值。门里提前有值，走快路径，根本不挂。

<OnlineCompilerDemo
  title="一扇带值的门：最小可等待对象 Gate"
  source-path="code/examples/vol5/chapter07/07_exp_c_gate.cpp"
  description="幕一值后到，consumer 挂起登记，set(42) 把它叫醒并把值从 await_resume 带回，幕二值提前在门里，await_ready 走快路径根本不挂。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_c_gate.cpp —— 最小可等待对象：一扇带值的门
#include <coroutine>
#include <cstdio>
#include <exception>
#include <optional>

// 门：还没有值时，co_await 它的协程挂起登记，有值了，恢复时把值带回来
struct Gate {
  std::optional<int> value;            // 门里的值
  std::coroutine_handle<> waiter{};    // 登记在门口的协程，至多一位

  bool await_ready() { return value.has_value(); }  // 值已经在，不必挂起
  void await_suspend(std::coroutine_handle<> h) { waiter = h; }
  int await_resume() { return *value; }             // 恢复时把门里的值带回去

  void set(int v) {  // 送值进门，顺路叫醒登记的人
    value = v;
    if (waiter) {
      auto w = waiter;
      waiter = {};
      w.resume();
    }
  }
};

struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task consumer(Gate& gate) {
  std::printf("  consumer: 来到门口，准备等一个值\n");
  int v = co_await gate;
  std::printf("  consumer: 恢复了，从 await_resume 拿到 %d\n", v);
}

int main() {
  { // 幕一：值后到，协程挂起登记，set 把它叫醒
    Gate gate;
    Task t = consumer(gate);
    std::printf("main: consumer 登记了吗？waiter=%s\n", gate.waiter ? "有" : "没有");
    std::printf("main: 送 42 进门\n");
    gate.set(42);
    std::printf("main: 回到 main，一幕结束\n");
  }
  { // 幕二：值提前在门里，await_ready 走快路径，协程根本不挂起
    Gate gate;
    gate.set(7);
    std::printf("main: 幕二开场，7 已经在门里\n");
    Task t = consumer(gate);
    std::printf("main: 幕二结束，waiter=%s\n", gate.waiter ? "有" : "没有");
  }
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_c_gate.cpp -o exp_c_gate
./exp_c_gate
```

```text
  consumer: 来到门口，准备等一个值
main: consumer 登记了吗？waiter=有
main: 送 42 进门
  consumer: 恢复了，从 await_resume 拿到 42
main: 回到 main，一幕结束
main: 幕二开场，7 已经在门里
  consumer: 来到门口，准备等一个值
  consumer: 恢复了，从 await_resume 拿到 7
main: 幕二结束，waiter=没有
```

逐行对读，两幕的顺序都在意料之中，又都值得看一眼。幕一，consumer 跑到 `co_await gate`，门里是空的，`await_ready` 返回 false，协程挂起，`await_suspend` 把句柄登记进 waiter。控制流回到 main，consumer 创建时就开工，跑到头一个挂起点才停，急启动的行为您在上一篇动手清单第 2 条观察过，这里正好对上。所以紧跟着打印的“登记了吗”，答案是“有”。set(42) 把值放进门里，顺手把登记的句柄捞出来恢复，consumer 从 `co_await` 的位置接着跑，`await_resume` 把 `*value` 带回来，42 就落在 v 身上。您看打印的顺序，consumer 的第二行夹在“送 42 进门”与“回到 main”中间，恢复就发生在 set 的函数体里。

幕二，7 提前进了门。consumer 走到 `co_await` 时 `await_ready` 看见值已经在，返回 true，第 4、5 两环整个跳过，协程一行没挂，直接从 `await_resume` 拿值。您看 waiter，到最后都是空的，consumer 从头到尾没被登记过。

咱们停一步，看看门本身还交代了什么。Gate 没写 `operator co_await`，`co_await gate` 照样成立，因为协议第 2 环找不到 operator co_await 时，操作数自己就是 awaiter，而 Gate 恰好三件套齐全。真异步的版本跟它只差登记的去向：`await_suspend` 里不该把句柄存在自己成员身上，而该交给定时器、事件循环或者完成队列，让它们在合适的时候来恢复。谁负责恢复执行，`await_suspend` 写下登记的当口就定了。机制本身不管恢复的时机，管的是交接的通道。

> 还有个小细节值得留意，awaiter 的住处。`co_await gate` 里 gate 是 main 的局部变量，住处不是问题。要是您写 `co_await Gate{}`，awaiter 是个临时量，而它的生命周期要跨挂起点，标准安排它搬进协程帧（上一篇讲过，跨挂起点的局部变量与临时对象都住帧里）。写库的人常反过来借它：把每次操作的中间状态放进 awaiter，让协程帧替它出地方，一次分配，反复使用。

## operator co_await 与 ADL 定制点（扩展）

本节带（扩展）的标记，跳过它，本篇主线照样完整。想知道第三方类型怎么配等待、`co_await 250` 为什么能编过，还请您留下来。

协议第 2 环还没细看，这里补上。`operator co_await` 是等待的定制点，落位由您挑：

- 写成成员函数，`a.operator co_await()`，住在类型自己的身体里。您写任务类型时多半选它，任务配上成员版本的 operator co_await，返回自己的 awaiter。
- 写成非成员函数，`operator co_await(a)`。候选不挑出身，普通查找找得到的，加上 ADL（参数相关查找，argument-dependent lookup）顺着参数的命名空间找来的，跟成员版本一起进同一场重载决议（[over.match.oper]/3 与 /7）。咱们看中它的地方，在改不了源码的类型身上：别人的类型动不了，定制点可以住进它所在的命名空间，顺着它的来路照样能被找到。
- 什么都不写，操作数自己带三件套。Gate 与 Park 用的正是它，您在上一节已经见过。

咱们拿一个装不进定制点的类型试非成员版。场景是假的，机制是真的：vendor 命名空间里有个排队的 Ticket，假装它是别人的库，源码动不了。给它配等待的办法，是回到它的命名空间里放一个自由的 operator co_await，返回一个带三件套的 awaiter：

<OnlineCompilerDemo
  title="给改不了的第三方类型配 operator co_await"
  source-path="code/examples/vol5/chapter07/08_exp_e_adl.cpp"
  description="vendor 命名空间里的 Ticket 动不了源码，自由的 operator co_await 顺着 ADL 被找到，TicketAwaiter 接手三件套，排队号 7 从 await_resume 带回来。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_e_adl.cpp —— 给改不了的第三方类型配 operator co_await
#include <coroutine>
#include <cstdio>
#include <exception>

namespace vendor {  // 假装这是别人的库，源码动不了
  struct Ticket {
    int number;
    explicit Ticket(int n) : number(n) {}
  };

  struct TicketAwaiter {
    Ticket t;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> h) {
      std::printf("  [awaiter] 协程挂好了，真异步在这里交给调度器\n");
      h.resume();   // 演示从简：登记完当场恢复
    }
    int await_resume() { return t.number; }
  };

  // 定制点住进 vendor 命名空间，ADL 顺着 Ticket 的来路就找到它
  TicketAwaiter operator co_await(Ticket t) { return TicketAwaiter{t}; }
}

struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };
  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task caller() {
  std::printf("  caller: 写下 co_await vendor::Ticket(7)\n");
  int n = co_await vendor::Ticket(7);
  std::printf("  caller: await_resume 把排队号 %d 带了回来\n", n);
}

int main() {
  Task t = caller();
  std::printf("main: 回到 main\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
  caller: 写下 co_await vendor::Ticket(7)
  [awaiter] 协程挂好了，真异步在这里交给调度器
  caller: await_resume 把排队号 7 带了回来
main: 回到 main
```

`co_await vendor::Ticket(7)` 的走法您现在能背出来了。Ticket 自己没有三件套，编译器顺着它的命名空间找到自由函数，TicketAwaiter 接手，三件套照协议跑完全程，`await_resume` 把排队号 7 带回来。定制的全部内容，就落在“从 Ticket 造出一个 awaiter”上，三问还是三问。

两个边角笔者也探过，都拿探针验的。成员与非成员同时在场，两边都匹配，重载决议直接判歧义，程序不合法，GCC 的报错是 `ambiguous overload for ‘operator co_await’`。另一个是对基本类型的等待，`co_await 1000` 编不过：int 既没有类作用域放成员版本，也没有关联命名空间放非成员版本，操作数自己又没有三件套，GCC 的报错是 `awaitable type ‘int’ is not a structure`。想让 `co_await 250` 成立，落点回到协议第 1 环的 await_transform，promise 定义它，int 过它的手，变成一个 awaiter。

<OnlineCompilerDemo
  title="await_transform：让 co_await 一个 int 有了落点"
  source-path="code/examples/vol5/chapter07/09_exp_g_transform.cpp"
  description="promise 定义了 await_transform，co_await 250 被翻译成 SleepAwaiter，倒计时三轮后点火。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_g_transform.cpp —— promise 配了 await_transform，co_await 一个 int 就有了落点
#include <coroutine>
#include <cstdio>
#include <exception>

struct SleepAwaiter {
  int ms;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) {
    std::printf("  [awaiter] 该等 %d 毫秒，真异步在这里交给定时器\n", ms);
    h.resume();   // 演示从简：登记完当场恢复
  }
  void await_resume() {}
};

struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
    SleepAwaiter await_transform(int ms) { return SleepAwaiter{ms}; }
  };
  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task countdown() {
  for (int i = 3; i > 0; --i) {
    std::printf("  倒计时 %d\n", i);
    co_await 250;
  }
  std::printf("  点火\n");
}

int main() {
  Task t = countdown();
  std::printf("main: 回到 main\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
  倒计时 3
  [awaiter] 该等 250 毫秒，真异步在这里交给定时器
  倒计时 2
  [awaiter] 该等 250 毫秒，真异步在这里交给定时器
  倒计时 1
  [awaiter] 该等 250 毫秒，真异步在这里交给定时器
  点火
main: 回到 main
```

await_transform 还有另一面，咱们接着看。promise 一旦定义了它，所有普通 `co_await` 都得过它的手，它就成了协程类型的门卫：想禁掉某些类型、统一加日志、插入取消检查，钩子都下在这里。初始与最终挂起点不归它管，promise 给自己挑 `initial_suspend`、`final_suspend` 的挂起体时绕开它，草案在 [expr.await]/(3.2) 写明了跳过的范围。

## 对称转移：恢复执行时的控制权交接（扩展）

本节也带（扩展）的标记，不看它，主线仍完整。只是它装着本篇的写法立场，还有对下一篇《[把结果包起来：教学版 Task](04-task-type)》的预告，跳过它，到下一篇会缺一环交接的铺垫，要不要留，您掂量。

到 Gate 为止，恢复都发生在别人手里。还有一类等待，咱们把它请出来：恢复就发生在 await_suspend 自己身上，协程 A 等协程 B，A 挂起之后，await_suspend 当场把 B 跑起来。任务类型的链式等待全是同一个形状。链子短，一点事没有。链子一深，栈上的麻烦就压不住了。

咱们把麻烦造出来量一遍。写一条链：chain(N) 等 chain(N-1)，一路等到 chain(0)。交接的姿势做成开关，一份代码带两种形状：

<OnlineCompilerDemo
  title="深链协程：naive 与 sym 两种交接姿势"
  source-path="code/examples/vol5/chapter07/10_exp_d_chain.cpp"
  description="程序要两个命令行参数（naive 或 sym 加链深），页面直接运行不带参数时打印用法，文章里的五组读数是带参数跑出来的。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_d_chain.cpp —— 深链协程：两种控制权交接姿势，量栈的读数
// 用法：./exp_d_chain <naive|sym> <链深>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

static bool g_sym = false;   // sym：await_suspend 返回句柄；naive：当场 resume

struct ChainTask {
  struct promise_type {
    std::coroutine_handle<> continuation{};   // 等我做完的上一级协程，根协程没有

    ChainTask get_return_object() {
      return ChainTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }

    // 完成时的交接：有人等就把控制权交回去，没人是根，就交还给最初的恢复者
    auto final_suspend() noexcept {
      struct Done {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<promise_type> me) noexcept {
          auto c = me.promise().continuation;
          if (!c) return std::noop_coroutine();
          if (g_sym) return c;
          c.resume();                // naive：上一级在栈的更深处接着跑
          return std::noop_coroutine();
        }
        void await_resume() noexcept {}
      };
      return Done{};
    }
  };

  std::coroutine_handle<promise_type> h;
  explicit ChainTask(std::coroutine_handle<promise_type> hh) : h(hh) {}
  ChainTask(const ChainTask&) = delete;
  ChainTask& operator=(const ChainTask&) = delete;
  ~ChainTask() { if (h) h.destroy(); }

  bool await_ready() noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiter) noexcept {
    h.promise().continuation = waiter;
    if (g_sym) return h;   // sym：挂起自己，把句柄交出去，让编译器接手
    h.resume();            // naive：当场把子协程跑完，才回到这里
    return std::noop_coroutine();
  }
  void await_resume() noexcept {}
};

ChainTask chain(int depth) {
  if (depth % 100000 == 0) {
    std::printf("    到达深度 %d\n", depth);
    std::fflush(stdout);
  }
  if (depth > 0) {
    co_await chain(depth - 1);
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("用法: %s <naive|sym> <链深>\n", argv[0]);
    return 1;
  }
  g_sym = (std::strcmp(argv[1], "sym") == 0);
  int depth = std::atoi(argv[2]);
  std::printf("== %s 模式，链深 %d ==\n", g_sym ? "sym" : "naive", depth);
  ChainTask root = chain(depth);
  root.h.resume();
  std::printf("== 根协程完工，控制权回到 main ==\n");
  return 0;
}
```

</OnlineCompilerDemo>

三处值得咱们慢看。第一处是 awaiter 的 `await_suspend`，它有两个分支，naive 分支当场 `h.resume()`，把子协程跑完才回到这里。sym 分支把句柄交出去就返回。第二处是 promise 的 `final_suspend`，它返回的 Done 也是一个 awaiter：协程完工挂进终点站时，Done 把控制权交回等待它的上一级，有人等就交接，没人是根协程，就交 `std::noop_coroutine()`。noop 句柄的 resume 是个空操作（[coroutine.handle.noop]），等于把控制流还给最初的恢复者。第三处是 ChainTask 自己带三件套，没写 operator co_await，`co_await chain(depth - 1)` 走的是“操作数自己就是 awaiter”的路。

咱们跑五组读数，-O2 三组，-O0 两组对照：

```bash
g++ -std=c++20 -O2 -pthread exp_d_chain.cpp -o exp_d_chain
./exp_d_chain naive 100000
./exp_d_chain naive 1000000
./exp_d_chain sym 1000000

g++ -std=c++20 -O0 -pthread exp_d_chain.cpp -o exp_d_o0
./exp_d_o0 naive 100000
./exp_d_o0 sym 1000000
```

```text
== naive 模式，链深 100000 ==
    到达深度 100000
    到达深度 0
== 根协程完工，控制权回到 main ==

== naive 模式，链深 1000000 ==
    到达深度 1000000
    到达深度 900000
    到达深度 800000
```

-O2 的 naive 一百万层，走到 80 万层的里程碑之后，只剩一声 Segmentation fault，退出码 139。sym 的同一组数字，咱们接着看：

```text
== sym 模式，链深 1000000 ==
    到达深度 1000000
    到达深度 900000
    到达深度 800000
    到达深度 700000
    到达深度 600000
    到达深度 500000
    到达深度 400000
    到达深度 300000
    到达深度 200000
    到达深度 100000
    到达深度 0
== 根协程完工，控制权回到 main ==
```

读数咱们一组组过。-O2 的 naive 十万层，跑通了。读数得如实交代：GCC 16.2.1 在 -O2 下把嵌套的恢复压得极扁，从后面的崩点反推，每层只摊到十来个字节。小场面它救得住。-O2 的 naive 一百万层没撑住，8 MiB 的栈在 70 万到 80 万层之间耗尽。同一个程序换 sym 姿势，一百万层全程走完，栈深与链深不相干。

-O0 的两组，咱们拿它把编译器的角色照得更亮：

```text
== naive 模式，链深 100000 ==
    到达深度 100000
```

-O0 的 naive 十万层，咱们看着它走进去就没再报过里程碑，段错误退出。sym 呢，也没能全身而退：

```text
== sym 模式，链深 1000000 ==
    到达深度 1000000
    到达深度 900000
```

sym 一百万层卡在 90 万层里程碑后面，同样是段错误，退出码 139。优化一关，尾调用没做成，交接照样一层层压栈。咱们拿读数跟 GCC 一个还开着的案子对一对（bug 100897，2026-04 还在讨论）：对称转移靠的是间接尾调用，部分调用约定给不了相应的支持，-O0、sanitizer 两种情况尤其容易踩空。

机制这边咱们说回来。普通 resume 是一次普通函数调用，调用者的帧留在栈上等它回来，naive 的 await_suspend 正是这么用的：A 的恢复框架调 await_suspend，await_suspend 调 B 的 resume，B 的函数体、B 的 await_suspend，一层压一层，谁的调用都没返回，帧就都还占着栈。sym 姿势下，编译器看到的形状是“resume 一个新协程，然后立刻返回本函数”，恰好是尾调用的形状，本层帧退出，跳转过去，栈深不动。标准自己没有把“尾调用”写成语言义务，(5.1.1) 的注只说接力可以一串接下去，怎么做到不涨栈，靠编译器把交接识别成尾调用。Lewis Baker 的对称转移一篇把尾调用的成立条件列得清楚：调用约定一致、返回类型相同、没有还没析构的栈对象、调用不在 try/catch 里，协程的形状恰好都满足。不过条件满足不等于承诺兑现，笔者机器上的读数就是例证，-O2 接住了，-O0 没接住，换一家编译器、换一套 ABI 还有别的面孔。

所以笔者把写法定下来了：做任务类型，await_suspend 与最终挂起点的交接都按 sym 姿势写。形状对了，常见优化档位下编译器接得住，栈深与链深解耦就立住了。剩下的变数，-O0、sanitizer、特殊 ABI，属于实现质量的一侧，写库的人心里要有数，深链场景拿自己的工具链跑一遍，比背条款踏实。

## 轮到您动手的时候

1. 把 Gate 改成能登记多位客人的版本，waiter 换成列表，set 时按登记顺序逐个恢复。恢复的顺序该谁说了算，set 的调用者吗，还是登记的顺序本身。
2. 只保留 exp_d 的 naive 分支，链深从十万往上翻倍地加，找出您手头编译器与优化档位下 naive 的生死线，拿 `ulimit -s` 的值折算每层摊到多少字节，跟笔者 80 万层上下的读数对一对。
3. 给 TicketAwaiter 的 await_suspend 改成返回 bool，Ticket 已经处理完时返回 false，观察协程不挂起直接拿到值。顺手把 await_ready 也改成查同一件事，两道快路径并存，想想它们各防的是什么。
4. 把 Gate 的值换成 `std::coroutine_handle<>`，让 `co_await gate` 等的是另一条协程完工。写完拿 exp_d 的链子验证它。

## 下一步

三件套与两种交接姿势都齐了，可咱们手里的 Gate 还欠着东西：取不到结果，传不了异常，销毁要自己惦记。翻页前，请您把动手清单的第 2 条真的跑一遍。naive 的生死线随编译器、优化档位与栈配置浮动，笔者机器上是 80 万层上下，您的机器可能是 5 万，也可能是 200 万。把它量出来，对称转移的价值就不需要笔者再形容了。

## 参考资源

本篇的素材来自旧目录 ch06 的《[promise_type 与 awaitable](../ch06-async-io-coroutine/03-promise-type-and-awaitable)》，awaiter 协议、operator co_await 与 await_transform 在那里已有一版讲解，咱们把例子全部重写，行为断言按现行草案重核。推进思路参照 Lewis Baker 协程系列的两篇，operator co_await 与对称转移，例子与文字都是笔者自己写的。语言机制的断言按 eel.is 上的现行工作草案核对（2026-11-17 访问），条款号与段号可能随草案更新挪动。

- [Await expression（现行草案 expr.await）][draft-await]，三问的展开步骤、await_suspend 的三种返回类型、抛异常时协程恢复并立即重抛、表达式类型与 await_resume 一致、接力恢复的注。
- [Operators in expressions（现行草案 over.match.oper）][draft-oper]，operator co_await 的成员候选、普通查找与 ADL 的非成员候选怎样合成一场重载决议。
- [Trivial awaitables（现行草案 coroutine.trivial.awaitables）][draft-trivial]，suspend_always 与 suspend_never 的三件套定义。
- [Resumption（现行草案 coroutine.handle.resumption）][draft-resumption]，resume 与 destroy 的前置条件、跨执行代理恢复的实现定义行为。
- [No-op coroutines（现行草案 coroutine.handle.noop）][draft-noop]，noop 句柄的 resume 无操作、done 恒为 false。
- [C++ Coroutines: Understanding operator co_await — Lewis Baker][baker-coawait]，awaitable 与 awaiter 术语的来历、bool 返回类型的竞态用法、手工复位事件与协程帧借内存的讲法参照。
- [C++ Coroutines: Understanding Symmetric Transfer — Lewis Baker][baker-sym]，栈溢出问题的形状、尾调用的成立条件、P0913R0 与 noop_coroutine 的来历。
- [GCC bug 100897][gcc-100897]，对称转移在部分选项与 ABI 下没有做成尾调用的在案记录，状态与讨论时间以页面为准。

[draft-await]: https://eel.is/c++draft/expr.await
[draft-oper]: https://eel.is/c++draft/over.match.oper
[draft-trivial]: https://eel.is/c++draft/coroutine.trivial.awaitables
[draft-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[draft-noop]: https://eel.is/c++draft/coroutine.handle.noop
[baker-coawait]: https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await
[baker-sym]: https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer
[gcc-100897]: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100897
