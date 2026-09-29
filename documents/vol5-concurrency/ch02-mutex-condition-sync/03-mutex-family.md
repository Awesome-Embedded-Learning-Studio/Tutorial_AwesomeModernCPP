---
title: "mutex 家族：标准库里的其他锁"
description: "认全 mutex 的亲戚们：recursive_mutex 为什么多数时候是设计绕了弯的信号、timed_mutex 的时限拿法归位、shared_mutex 的读档与独占档，末尾给一张按场景取用的选型表"
chapter: 2
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 14
prerequisites:
  - "mutex 与 RAII 守卫"
  - "死锁与现场诊断"
related:
  - "同步原语工具箱"
  - "thread_local：每线程一份的世界"
---

# mutex 家族：标准库里的其他锁

[mutex 那篇](./01-mutex-and-raii-guards.md)咱们把 `std::mutex` 配 `lock_guard` 的主力用法立稳了，[死锁那篇](./02-deadlock-and-gdb.md)又把拿多把锁的病治了。可 `<mutex>` 与 `<shared_mutex>` 头文件里的家当不止这一件：`recursive_mutex`、`timed_mutex`、`shared_mutex`……新手的常见事故就出在这里——遇到"锁拿不进去"的时候，顺手把 `mutex` 换成 `recursive_mutex`，编译过了、跑起来了，事故就埋下了。本篇把这些亲戚逐个认脸：各自的机制是什么、什么场景是正路、什么场景是病兆。

先把家族的合照摆出来：

| 类型 | 进标准的版本 | 多出来的本领 | 一句话定位 |
| --- | --- | --- | --- |
| `std::mutex` | C++11 | —— | 默认选择，配守卫 |
| `std::recursive_mutex` | C++11 | 同一线程可重复 `lock()` | 多数时候是设计绕了弯的信号 |
| `std::timed_mutex` | C++11 | `try_lock_for()` / `try_lock_until()` | 宁可放弃也不僵住的场合 |
| `std::recursive_timed_mutex` | C++11 | 上面两样的叠加 | 两样坑叠在一起，见得少 |
| `std::shared_mutex` | C++17 | `lock_shared()` 读档 | 读多写少的场合分档 |
| `std::shared_timed_mutex` | C++14 | 读档加时限 | 比 `shared_mutex` 还早一版，冷知识后文说 |

## recursive_mutex：能递归不等于用对了

`std::mutex` 有条铁律您记得：持有期间再去 `lock()` 同一把锁，未定义行为。`recursive_mutex` 把这条放宽了——同一线程重复拿，不但不是 UB，还进得去，内部拿着计数，拿几层就得放几层。

它看着像救命稻草，咱们看它救的是什么。典型现场：公开成员函数 `f()` 拿了锁干活，干着干着调了另一个成员函数 `g()`，`g()` 自己也拿同一把锁。用 `std::mutex` 的话这一步直接踩进 UB；换 `recursive_mutex`，程序"能跑了"。您点"动手试一试"，看递归版和重构版的对照：

<OnlineCompilerDemo
  title="动手验证：recursive_mutex 放行重复拿锁，重构版一把锁只拿一次"
  source-path="code/examples/vol5/50_recursive_mutex_smell.cpp"
  description="递归版三层调用层层拿同一把 recursive_mutex，都能进临界区；重构版把「假设调用方已持锁」的步骤拆成私有函数，一把锁只拿一次。两边结果相同：递归版是症状被压住，重构版是病根被拿掉。"
  run-options="-O2 -std=c++17 -pthread"
  allow-run
/>

咱们把话说透：**recursive_mutex 没有解决任何正确性问题，它只是让程序不崩了。** 三条账挨个算。

头一条，它出现的原因本身就是病兆。同一线程第二次拿同一把锁，意味着有两个函数都以为自己独占着这批数据——外层的 `f` 拿锁时数据可能正改到一半，内层的 `g` 一进来读到的是中间态。锁的本意是护住不变量（[mutex 那篇](./01-mutex-and-raii-guards.md)讲过），可 `f` 与 `g` 各自对不变量的假设对不上，recursive_mutex 把这个裂缝糊住了，没焊上。

第二条，别的线程一点没多得到。递归计数是线程私有的：您自己三层进出随便，另一条线程照样被挡在门外等您全放完。互斥的粒度没有变小，保护没有变多，得到的只有一个"不用重构"的许可。

第三条，调试更难看。gdb 里 `std::mutex` 的持有者是一个 tid（[死锁那篇](./02-deadlock-and-gdb.md)的进阶招数看的就是它），recursive_mutex 得连着计数一起看，"谁拿着"的答案从一个人变成了一串。更别说它照样死锁：您跟别的线程交叉拿锁，递归性一点救不了。

正路是重构，思路就一条：**拿锁的活在公开入口做一次，内部的步骤拆成"假设调用方已持锁"的私有函数。** 演示里的 `step_locked` 就是它——名字带个 `_locked` 后缀把约定写在脸上，注释一句"不拿锁：调用方已经拿了"。递归改循环、或者把递归里需要锁的部分提到锁外，多数"必须递归拿锁"的场景拆完都会消失。真有按层级组织锁的需求，正主是 [死锁那篇](./02-deadlock-and-gdb.md)的层级锁：它管的是多把锁的次序，靠 thread_local 记状态，跟 recursive_mutex 的"同一把锁拿多层"是两码事。

什么时候它算正当？几乎只剩一种：**接手没法改的接口**。第三方回调把一个已经持锁的上下文递回给您，您调它的公开接口又必须拿锁——改不了别人的签名，recursive_mutex 是最不坏的止血。前提是您写注释说明白为什么。

## timed_mutex：时限拿法归位

`timed_mutex` 的本领在 [mutex 那篇](./01-mutex-and-raii-guards.md)已经用过一遍：换上它，`unique_lock` 就多出 `try_lock_for()` 与 `try_lock_until()`，50 毫秒内拿不到就超时返回，决策权回到您手里，走降级路径、别死等。它常与 [死锁那篇](./02-deadlock-and-gdb.md)的 `try_lock` 回退防线搭档：回退是"摸一把就走"，时限是"等一会儿再说"，两档按业务等得起多久来挑。

用法那边有完整代码，这里只补两条实情。一，超时返回 `false` 之后**别立刻重试**——自旋着反复 `try_lock_for` 就把降级路径变成了忙等，退避的间隔要配上。二，家族表里的 `recursive_timed_mutex` 是 recursive 与 timed 的叠加，两样的坑它都占，正路场景比 recursive_mutex 还窄，您见到它的时候，多半该先审设计。

## shared_mutex：读档与独占档

mutex 守门的方式是一次一个人。可读数据的场合，几个读者同时看根本不打架，一次只放一个进来就纯属浪费了。`std::shared_mutex` 为读多写少的场合开了两种档位：共享档（读档）允许任意多的读者同场，独占档（写档）还是一次一个人的老做法，独占档在场时读者也得等。接口与 mutex 的形状同构，多出的半套是 `lock_shared`/`unlock_shared`，RAII 的配法是读档用 `std::shared_lock`，独占档照旧用 `lock_guard` 或 `unique_lock` 就可以了。

咱们做个探针实验：两个读者拿着读档在场，main 试独占档的门：

```cpp
// shared_mutex：两读者在场时，独占档拿不进门
constexpr int n = 2;
std::shared_mutex m;
int protected_value = 7;
std::barrier phase(n + 1, []() noexcept {});   // 编排用：两读者加 main

std::vector<std::jthread> readers;
for (int id = 0; id != n; ++id) {
    readers.emplace_back([&, id] {
        std::shared_lock lk(m);                 // 读档：多人可同时在场
        phase.arrive_and_wait();                // 第 1 轮：两个读者与 main 都到位
        phase.arrive_and_wait();                // 第 2 轮：等 main 探完独占档再撤
    });                                         // 作用域结束：shared_lock 放锁
}
phase.arrive_and_wait();                        // 此刻两个读者都持着读档
bool got = m.try_lock();
if (got) {
    m.unlock();
}
std::cout << "两读者在场时独占档 try_lock 拿到 = " << got << "（期望 0）\n";
phase.arrive_and_wait();                        // 放读者们出去，读档全撤
for (auto& t : readers) {
    t.join();
}
bool got2 = m.try_lock();                       // 读者全撤：独占档进门
if (got2) {
    ++protected_value;
    m.unlock();
}
std::cout << "读者全撤后独占档 try_lock 拿到 = " << got2
          << "，protected_value = " << protected_value << "（期望 8）\n";
```

<OnlineCompilerDemo
  title="动手验证：两读者同场，独占档进不了门"
  source-path="code/examples/vol5/46_shared_mutex_try_lock.cpp"
  description="两行「读者进入」的先后与在读人数的计数顺序稳定（barrier 编排），判读看后两行：读档在场时 try_lock = 0，读者全撤后 = 1 且 protected_value 变 8。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

读者进入的两行打印与在读人数的计数在归仓完整版里，计数用的原子只做观测。判读看后两行：读档在场时独占档的 `try_lock` 直接失败，读者撤光之后独占档才进得去门，改动落地。

编排值得咱们说一句。两个读者与 main 用一个空完成函数的 barrier 编成两轮：第 1 轮三方到齐的瞬间，两个读者都持着读档、main 的探针出手，打到稳定的 0。第 2 轮咱们把读者放走，撤光了之后再探，打到的就是 1。咱们没有这套编排的时候，探针可能赶在读者进门之前出手，也可能等读者撤完才出手，演示就成了碰运气。咱们顺带也看到了 barrier 的日常用法：完成函数空着，它就是一个纯粹的节拍器。

用法上还有三件实情要跟您交代。

共享档是有成本的。实现得让读者互相知道彼此在场，多半是内部锁加计数的组合，拿放读档本身是有开销的，读临界区太短的话 shared_mutex 反而不如 mutex 划算。判据是读侧干不干重活：解析、聚合、查大表够重的，才值得咱们换档。

没有原子的升降档。咱们读着读着想写的时候，标准没有从读档直接升到独占档的操作，只能放了再拿。可放的瞬间别人可能插进来改了数据，等咱们重新拿到独占档，此前读到的状态得重新验证。

写者的饥饿问题没有标准答案。读者源源不断的场合，写者的独占档可能一直进不去，防写者饥饿的策略留给了实现，公平性咱们不能当作承诺写进设计。

时间线上还有个冷知识：带计时的 `shared_timed_mutex`（`try_lock_for` 这类带时限的成员）是 C++14 进来的，不带计时的 `shared_mutex` 反而晚到 C++17。选型的顺序咱们维持不变：默认 mutex 配守卫，等 [第 4 章 的 perf 篇](../ch04-concurrent-data-structures/02-lock-free-stack-and-perf.md)量出读侧争用真是瓶颈，咱们再考虑换档或者上分片。


## 什么时候换锁：一张选型表

默认永远是 `std::mutex` 配守卫，这话本篇开头说过、结尾再说一遍。换锁的理由必须是量出来的，不是感觉出来的：

| 您的处境 | 先想什么 | 再考虑换 |
| --- | --- | --- |
| 同线程二次拿锁 | 拆"假设已持锁"的私有函数 | `recursive_mutex` 仅限改不了的接口 |
| 等锁等不起 | 降级路径、退避重试 | `timed_mutex` |
| 读多写少、读侧干重活 | 先量争用（[第 4 章 perf 篇](../ch04-concurrent-data-structures/02-lock-free-stack-and-perf.md)） | `shared_mutex`，读临界区太短反而更亏 |
| 按层级组织多把锁 | [层级锁](./02-deadlock-and-gdb.md) | 不是 recursive_mutex 的事 |

锁家族认全了，还有一类数据压根不用抢：天生每线程一份的，咱们下一篇 [thread_local](./04-thread-local.md) 说它。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference"
    title="std::recursive_mutex"
    url="https://en.cppreference.com/w/cpp/thread/recursive_mutex"
    chapter="重复加锁语义与持有计数"
  />
  <ReferenceItem
    :id="2"
    author="cppreference"
    title="std::timed_mutex"
    url="https://en.cppreference.com/w/cpp/thread/timed_mutex"
    chapter="try_lock_for / try_lock_until 的超时语义"
  />
  <ReferenceItem
    :id="3"
    author="cppreference"
    title="std::shared_mutex"
    url="https://en.cppreference.com/w/cpp/thread/shared_mutex"
    chapter="读档与独占档的阻塞规则"
  />
  <ReferenceItem
    :id="4"
    author="Core Guidelines"
    title="CP.21: Use std::lock() or std::scoped_lock to acquire multiple mutexes"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp21-use-stdlock-or-stdscoped_lock-to-acquire-multiple-mutexes"
  />
  <ReferenceItem
    :id="5"
    author="Anthony Williams"
    title="C++ Concurrency in Action, 2nd ed"
    :year="2019"
    publisher="Manning"
    url="https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition"
    chapter="§13 与 recursive_mutex 的设计讨论"
  />
</ReferenceCard>
