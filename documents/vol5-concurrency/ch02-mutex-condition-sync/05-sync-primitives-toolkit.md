---
title: "同步原语工具箱"
description: "把 call_once、latch、barrier、counting_semaphore、shared_mutex 五件工具配齐，兑现 futex 与优先级反转的深讲，再收一张容器锁策略速查表"
chapter: 2
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
difficulty: intermediate
platform: host
reading_time_minutes: 22
prerequisites:
  - "mutex 与 RAII 守卫"
  - "condition_variable 与阻塞队列"
related:
  - "死锁与现场诊断"
  - "condition_variable 与阻塞队列"
cpp_standard:
  - 11
  - 17
  - 20
---

# 同步原语工具箱

五件工具各进了哪个标准，咱们不用背版本对照表，问编译器就好：每件都配了特性测试宏，咱们拿十行的探针一问，答案就都有了。本篇就从这个验货的动作开场，把工具箱里剩下的家伙一件一件地配齐。

[死锁那篇](./03-deadlock-and-gdb.md)的结尾记下了一串名字：call_once、semaphore、latch、barrier，您也许还记得 `优先级反转` 与 futex（全称 fast userspace mutex）两个悬案，全都记在了本篇的名下。本章走到这里也正好到了收尾的位置，咱们把这些零碎一次清点完，连同前面几篇攒下的深讲也一次讲完。

清单其实不长。初始化只许成功一次的 `call_once` 与块级 `static`，有等全到齐的 `latch`，有管资源池与跨线程信号的 `counting_semaphore`，也有读多写少场合的 `shared_mutex`。barrier 咱们单独留了一节，一轮一轮汇合的活儿归它。五件工具的岁数横跨三个标准版本，`call_once` 的资历最老，C++11 里就有了，`shared_mutex` 是 C++17 进来的，`latch`、`barrier`、`semaphore` 随 C++20 一起落了地。岁数差了九年，可它们回答的都是同一类问题：线程之间除了互斥与谓词的等待，还有哪些配合方式值得标准库替咱们出手。

## 五件套点名，拿特性宏问编译器

C++20 的三件新工具出自同一份提案 P1135R6（提案编号 1135 的第六版修订稿，C++20 同步库的落地文本），咱们要探的就是它们配的特性测试宏，宏名都拿 `__cpp_lib_` 打的头。探针十行也就够了：

```cpp
// 特性宏探针：宏在，工具就在
#include <iostream>
#include <version>   // 特性测试宏的正源头

int main()
{
    std::cout << "__cpp_lib_semaphore   = " << __cpp_lib_semaphore << '\n'
              << "__cpp_lib_latch       = " << __cpp_lib_latch << '\n'
              << "__cpp_lib_barrier     = " << __cpp_lib_barrier << '\n'
              << "__cpp_lib_atomic_wait = " << __cpp_lib_atomic_wait << '\n';
}
```

```bash
g++ -std=c++20 -Wall -Wextra -pedantic -pthread ftm_probe.cpp -o ftm_probe && ./ftm_probe
```

```text
__cpp_lib_semaphore   = 201907
__cpp_lib_latch       = 201907
__cpp_lib_barrier     = 201907
__cpp_lib_atomic_wait = 201907
```

四个宏在笔者本机的 GCC 16.2.1 上全报 201907，这个数是组件随 C++20 定稿的年月号（2019 年 7 月）。您把编译档切到 C++17 再编一次，得到的是另一份答案：

```text
ftm_probe.cpp:7:48: error: '__cpp_lib_semaphore' was not declared in this scope
ftm_probe.cpp:8:48: error: '__cpp_lib_latch' was not declared in this scope
```

宏在 C++17 的档位下没有定义，探针也就编不过了，报错本身就是咱们要的答案。咱们在工程代码里的用法是拿 `#if defined(__cpp_lib_semaphore)` 把新工具包起来，老编译器上自动走别的路。

有一处弯路笔者替您试过了：这些宏的正源头是 `<version>`，而 `<latch>`、`<barrier>`、`<semaphore>` 这些组件头只是拿着宏做一圈开关，宏的定义不在它们手里。您想在引入组件头之前探路，包含 `<version>` 一个就够了，全部 `__cpp_lib_` 宏的户口都落在那里。

第四个宏 `__cpp_lib_atomic_wait` 管的不是本篇的哪一件，它是 `atomic` 的 `wait`/`notify` 一家的门牌。那一层与本篇的 semaphore 关系密切，咱们讲完 semaphore 的实现再回头看它，正源在 [ch03 的等待与模式篇](../ch03-atomic-memory-model/04-fence-wait-and-patterns.md)。

## call_once：初始化只许成功一次

[thread_local 那篇](./02-thread-local.md)讲每线程一份的世界时，`call_once` 只在结尾点了个名，完整用法压到了本篇。它的活儿一句话说得清：一段初始化的逻辑，全进程只许真正地执行一次，第一个到的线程干，其余的线程等它干完。接口是成对的搭档，干活的 `std::call_once` 配记状态的 `std::once_flag`，flag 记着初始化的进度，咱们把函数和参数传给它。

用法本身三行就写完了，值得专门花笔墨的是它的语义边界：失败的尝试算不算数。咱们看一段会失败的初始化，依赖的服务没起来，初始化就只好抛异常了：

```cpp
// call_once 的重试语义：异常不算成功，下一个进来的接着试
int init_attempts = 0;
std::once_flag config_once;

void flaky_init()
{
    ++init_attempts;
    std::cout << "初始化第 " << init_attempts << " 次尝试\n";
    if (init_attempts < 3) {
        throw std::runtime_error("依赖还没就绪");
    }
    std::cout << "初始化成功\n";
}
```

咱们故意让前两次尝试都抛异常，到了第三次才成功。主线程一轮一轮地调 `call_once`，每轮接住的异常都打出来，之后四个后到的线程再各调一次：

```cpp
for (int round = 0; round != 3; ++round) {
    try {
        std::call_once(config_once, flaky_init);
        std::cout << "第 " << round + 1 << " 轮：call_once 顺利返回\n";
    } catch (const std::exception& e) {
        std::cout << "第 " << round + 1 << " 轮：异常传出 call_once（" << e.what() << "）\n";
    }
}
std::vector<std::jthread> latecomers;   // 后到的线程：直接通过
for (int id = 0; id != 4; ++id) {
    latecomers.emplace_back([] { std::call_once(config_once, flaky_init); });
}
```

```text
初始化第 1 次尝试
第 1 轮：异常传出 call_once（依赖还没就绪）
初始化第 2 次尝试
第 2 轮：异常传出 call_once（依赖还没就绪）
初始化第 3 次尝试
初始化成功
第 3 轮：call_once 顺利返回
后到线程全部通过，总尝试次数 = 3（期望 3）
```

输出的要点全在异常的去向里。`call_once` 没有替咱们吞掉异常，初始化函数抛了，异常会原样地传给调用方，可 flag 的状态纹丝不动：这一次的尝试作废，下一个调进来的线程就重新执行初始化函数。所以总尝试次数是 3 而不是 1，前两次都是失败之后的一轮重试。第三次成功落了地，flag 的状态才算点亮，之后不管咱们再调多少次，`flaky_init` 都不再执行了，大家直接通过。

咱们在工程上正需要这样的语义：初始化依赖数据库、依赖配置服务的场合，失败成了常态，重试也就成了本分。

### 块级 static：编译器替您 call_once

同样的活儿，C++11 起还有一条更省事的路：函数里的块级 `static` 局部变量。规范 [stmt.dcl]/4 的保证译过来是：控制流并发进入一条正在初始化的声明时，并发的执行应当等待初始化完成。落到咱们看得见的行为上：第一个到的线程执行构造，其他的线程原地等，构造完成之前谁也拿不到它的引用。

```cpp
struct Heavy {
    Heavy() { std::cout << "Heavy 构造（只应出现一次）\n"; }
    static Heavy& instance()
    {
        static Heavy h;   // C++11 起：并发进入只有一个线程执行初始化
        return h;
    }
    int value = 42;
};
```

咱们让四个线程同时调 `Heavy::instance()`，构造只出现了一次，四个线程全都读到了 42：

```text
Heavy 构造（只应出现一次）
线程读到 value = 42
线程读到 value = 42
线程读到 value = 42
线程读到 value = 42
构造次数 = 1（期望 1）
```

咱们口说无凭，拿符号表验货就是了，给编好的二进制跑一遍 `nm`：

```bash
nm toolkit | grep "cxa_guard"
```

```text
                 U __cxa_guard_abort@CXXABI_1.3
                 U __cxa_guard_acquire@CXXABI_1.3
                 U __cxa_guard_release@CXXABI_1.3
```

三个未解析的外部符号是编译器替块级 `static` 生成的护栏，协议定义在 Itanium C++ ABI（C++ 的应用二进制接口规范）的 guard 一节里：acquire 问的是初始化做没做，做过了直接走，没做过的就由当前线程来做，其余的线程在 acquire 里睡等。异常的路径也有交代，初始化抛了就调 abort 把护栏复位，让下一个进来的还有重试的机会——跟 `call_once` 的重试语义一一对上。护栏给每个块级 `static` 都配了一份，咱们在符号表里还能看到一行本地符号 `u guard variable for Heavy::instance()::h`，它就是 `instance()` 里那个 `h` 的护栏本体。

两条路怎么选？咱们看初始化的形状。要重试、要传参数、初始化逻辑散在多处共用一个 flag 的场合，`call_once` 的管法显式也可控。单例入口唯一的场合，块级 `static` 三行就完事了，连 flag 都不用咱们自己管。

老一点的教材还会教您双检查锁定，DCLP 是 double-checked locking pattern 的缩写，您在门外查一眼，拿了锁在锁里再查一眼，为的是跳过每次进入都拿锁的开销。C++11 之前它是出名的易错区，内存序没有正式的定义，各家编译器各干各的，写对了也靠运气。它如今的正确写法是什么、什么时候还有存在的价值，得等 happens-before 有了正式定义才说得清，咱们把这件事记到 [ch03 的原子篇](../ch03-atomic-memory-model/02-atomics-and-happens-before.md)。本篇只留一句给您：单例的场合，块级 `static` 早已把事情办好了，您别再手写。

## latch：一次性的倒计数

`std::latch` 是一个用完即弃的倒计数器。咱们构造的时候给一个正数，每个线程干完了自己的活就 `count_down()` 报到，计数归零的瞬间，所有等在 `wait()` 上的线程一起放行。接口的四个成员各管一摊：`count_down` 管的是报到，带参数的话一次减多个也行。`wait` 管的是等归零。`try_wait` 探的是归没归零。`arrive_and_wait` 是报到加等待的合写。实现能扛的上限由 `latch::max()` 报给咱们。

最典型的用法是开工前的集合，咱们让四个工人各写各的 `data[i]`，写完了就报到，main 等全员到齐了再读：

```cpp
// latch：main 等四个工人全到齐
constexpr int n = 4;
std::latch ready(n);
std::vector<int> data(n, -1);
std::vector<std::jthread> workers;
for (int i = 0; i != n; ++i) {
    workers.emplace_back([i, &ready, &data] {
        data[i] = i * i;        // 各自的准备活
        ready.count_down();     // 报到：计数减一，不等待
    });
}
ready.wait();                   // main 在这里等计数归零
```

```text
开工前 try_wait = 0（计数未归零）
归零后 try_wait = 1，data = [0 1 4 9]
```

输出第一行是在工人出门之前打的，第二行是 join 之后打的，咱们判读两句话：归零之前 `try_wait` 报的是 0，归零之后就报 1 了。latch 是不可复位的，一旦归零了就永远归零，`wait()` 也就立刻返回了。main 能过这一关靠的是计数本身，咱们没写过一行轮询，条件变量更是没碰过的。

反着用的话也成立：咱们拿 1 初始化的 latch 当发令门，工人们全员 `ready.wait()` 等的就是发令，main 准备好了 `count_down()`，四个人就能同时起步了。两个 latch 的一来一回，就是两段式开工的套路（发令门加集合门）。生命周期上只有一条要注意的：所有线程用完之前 latch 是不能析构的。约束在标准条文里没有写成析构函数前置条件的形状，cppreference 在 Notes 里叮嘱的依据是数据竞争的规则：析构与还在跑的 wait 交叠，就是一场 data race 了。

咱们看它跟 barrier 差在哪儿？差的其实就是复用。latch 放行一次就到头了，等第二轮汇合的线程得换一个新 latch。多轮的活儿要么每轮造一个，要么就得请出 barrier 了。

## barrier：多轮汇合与完成函数

`std::barrier` 把倒计数做成了一轮一轮的循环：全队到齐、放行、计数自动地复位，下一轮接着来的还是同一个 barrier。它比 latch 多了一件本事，也是本节的主角——完成函数（completion function）。构造时传进去的那段代码，在全队到齐之后、放行之前的那段空档里执行，恰好是咱们替全队做聚合的位置：

```cpp
// barrier：完成函数在全队放行之前替全队聚合
constexpr int n = 4;
constexpr int rounds = 2;
std::array<int, n> partial{};
int round_total = 0;
long grand_total = 0;
std::barrier sync_point(n, [&]() noexcept {
    round_total = 0;
    for (int x : partial) {
        round_total += x;
    }
    grand_total += round_total;
});
std::vector<std::jthread> workers;
for (int id = 0; id != n; ++id) {
    workers.emplace_back([&, id] {
        for (int r = 0; r != rounds; ++r) {
            partial[id] = (id + 1) * (r + 1);    // 本轮贡献
            sync_point.arrive_and_wait();        // 到齐后完成函数先跑，然后全队放行
            std::osyncstream(std::cout)
                << "第 " << r + 1 << " 轮：worker " << id
                << " 看到 round_total = " << round_total << '\n';
        }
    });
}
```

```text
第 1 轮：worker 0 看到 round_total = 10
第 1 轮：worker 1 看到 round_total = 10
第 1 轮：worker 3 看到 round_total = 10
第 1 轮：worker 2 看到 round_total = 10
第 2 轮：worker 0 看到 round_total = 20
第 2 轮：worker 1 看到 round_total = 20
第 2 轮：worker 3 看到 round_total = 20
第 2 轮：worker 2 看到 round_total = 20
两轮合计 grand_total = 30（期望 30）
```

每行的次序是随机的，醒得早的 worker 打得早，数值倒是定的。第 1 轮四个人的贡献是 1、2、3、4，聚合出了 10。第 2 轮翻倍成了 20。两轮的累计是 30。咱们没给 `round_total` 加过一把锁，聚合那段凭什么安全？靠的是规范写明的次序保证，原文的话只有一句：完成步骤的结束，strongly happens-before 所有被它放行的调用返回。咱们译过来对着看：worker 写完了 `partial[id]` 才报到，完成函数执行的时候本轮的写都落了地。完成函数写完了 `round_total`，放行之后的 worker 才醒来读它。两个方向的次序都有规范背书，聚合的一整段一把锁都不用。strongly 一词的分量，等 [ch03 内存序篇](../ch03-atomic-memory-model/03-memory-ordering.md)把 happens-before 家族摆全了，咱们再回来品。

完成函数有个值得专门对待的约束：不许抛异常。规范的模板参数表写着，`CompletionFunction` 得满足 `is_nothrow_invocable_v<CompletionFunction&>` 为真的要求。笔者在 GCC 16.2.1 上做了个探针，完成函数故意省了 noexcept：

```cpp
std::barrier b(1, []() { });   // 没写 noexcept
```

探针编译通过了，连警告的影子都没有。条文跟实现有了出入，咱们翻实现头 `/usr/include/c++/16/barrier` 找原因。第 254 行的约束只有半条：

```cpp
static_assert(is_invocable_v<_CompletionF&>);
```

只查了可调用，noexcept 的那半没落地。咱们再看第 188 行，完成函数是在哪儿被调起来的：

```cpp
void _M_invoke_completion() noexcept { _M_completion(); }
```

它被裹在一个 noexcept 的成员函数里执行。两层合起来的实际效果是：编译器放行了会抛的完成函数，可它真抛出来的那一刻，noexcept 的壳把异常按 std::terminate 处理，程序当场就倒地了。所以咱们的应对很干脆：完成函数一律由咱们自己写上 noexcept。聚合用的累加、复位、排序本来就抛不了异常，这样的约束在日常代码里不构成负担。

条文这头其实也没闲着。P2588R3（barrier 完成保证的放宽）作为缺陷报告（DR）回溯修订了 C++20 的条文，动机写在了 DR 表里：完成步骤原来的强保证挡了硬件加速的路。barrier 的特性宏为此登记了新值 202302L，本机 GCC 16.2.1 报的仍是 201907，新的保证没有声明齐。头文件行号是 GCC 16.2.1 的，换了编译器就会挪，您自己查的时候以 grep 为准，判据是不变的：咱们还是要把它写成 noexcept。

咱们对两个进阶的成员也过一眼。`arrive()` 干的是把报到与等待一分为二，返回的是一张 `arrival_token`：

```cpp
auto tok = sync_point.arrive();   // 报到，但不当场等
// ……（收尾的杂活，不需要全队齐活儿）……
sync_point.wait(std::move(tok));  // 杂活干完再汇合
```

报到与等待之间的空档，咱们正好拿它塞私活。`arrive_and_drop()` 干的则是退出报名：本轮汇合之后这个席位就不再计入了，下一轮的到齐人数少一个，逐轮淘汰的计算正好用它。

## semaphore：没有主人的计数器

`std::counting_semaphore` 就是一个带阻塞的计数器。`acquire()` 做的是把计数减一，减不动了就去睡等。`release()` 做的是把计数加一，加了之后唤醒等的人。它跟 mutex 的分界在所有权：mutex 讲究谁拿谁还，别的线程替它解锁就是未定义行为。而 semaphore 没有主人，release 与 acquire 可以落在不同的线程身上，咱们让甲线程发、乙线程收完全合法，甚至一次操作都不做、纯粹地发个信号也行。

模板参数 `LeastMaxValue` 是计数上限的最小承诺：`counting_semaphore<2>` 保证至少能数到 2，实现给得更多也是合规的。咱们不写模板参数的时候，默认取的就是实现的上限。这个上限也不是白设的，`release` 的前置条件写得很硬：加完了之后计数不得超过上限，越界了就是未定义行为。`std::binary_semaphore` 是标准里的原样别名，`using binary_semaphore = counting_semaphore<1>;`，管的就是 0 和 1 两个状态。

### 资源池：2 个座位 4 个用户

咱们让计数器干最自然的活儿：资源池限流。2 个数据库连接、4 个想用的线程，连接数就是信号量的初值：

```cpp
// semaphore：2 个座位，4 个用户，限流由计数本身承担
constexpr int permits = 2;
constexpr int users = 4;
std::counting_semaphore<permits> seats{permits};
std::atomic<int> in_use{0};    // 只做在场人数的观测，原子的正式规则归 ch03
std::atomic<int> peak{0};

std::vector<std::jthread> holders;
for (int id = 0; id != users; ++id) {
    holders.emplace_back([id, &seats, &in_use, &peak] {
        seats.acquire();               // 没空位就阻塞，不烧 CPU
        int now = in_use.fetch_add(1) + 1;
        int seen = peak.load();
        while (now > seen && !peak.compare_exchange_weak(seen, now)) {
        }
        std::osyncstream(std::cout) << "用户 " << id << " 入座，在场 " << now << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        in_use.fetch_sub(1);
        seats.release();               // 离开设卡：空位 +1，等的人被唤醒
        std::osyncstream(std::cout) << "用户 " << id << " 离开\n";
    });
}
```

```text
用户 0 入座，在场 1
用户 1 入座，在场 2
用户 0 离开
用户 1 离开
用户 3 入座，在场 1
用户 2 入座，在场 2
用户 3 离开
用户 2 离开
同时在库峰值 = 2（上限 2）
```

入座离开的次序每次都不同，唯一恒定的是最后那行：同时在场的峰值正好压在了上限 2 上，第 3 个用户想进来的时候只能在 `acquire()` 里睡，直到有人 `release()` 了为止。峰值不用咱们肉眼数，CAS 循环记下了最大值，完整版连同入座的打印都在归仓代码里。

### 信号：release 与 acquire 隔着一条线程

咱们把初值设成 0，semaphore 就成了一个纯信号：计数没有存量，release 管的就是发，acquire 管的就是收。这也正是它没有主人的价值所在：

```cpp
// binary_semaphore 初值 0：release 的发信号，acquire 的收信号
std::binary_semaphore signal{0};
std::jthread producer([&signal] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    signal.release();          // 发信号的是这个线程
});
signal.acquire();              // 收信号的是 main：跨线程合法
```

```text
main 收到了信号
try_acquire_for(50ms) 拿到 = 0，实际等了 50ms
```

第二行是紧接着再试的一次：信号已被收走，`try_acquire_for(50ms)` 等满了 50 毫秒空手而归，实际等待的时长拿 `steady_clock` 量出来正好 50ms。等不起的场合，咱们就靠带时限的成员（还有 `try_acquire_until` 的到点版）。

### try_acquire 可能空手而归

`try_acquire` 的语义有一条容易被忽略的让步，咱们把它单独讲：它允许虚假失败，计数明明是大于零的，它返回 `false` 也是合法的。规范的条文把这层授权写得明明白白，实现照办而已。libstdc++ 把原因写在头文件的注释里（`bits/semaphore_base.h` 第 256 行起），注释的原文一句：

```text
Fastest implementation of this function is just _M_do_try_acquire
but that can fail under contention even when _M_count > 0.
```

咱们译过来：最快的实现就是试一把原子交换，可竞争激烈的时候哪怕计数大于零也会失手，所以实现干脆拿 `try_acquire_for(0ns)` 顶替，小重试几次就认输了。由此咱们有个实用的判断：`try_*` 家族是快探针，拿来决定去留是可以的，拿来断言资源的状态就不行了。

### 它底下不是 sem_t，咱们当面验证

老一点的并发教材常讲，C++ 的信号量是 POSIX `sem_t` 外面的一层包装，本卷旧稿也这么写过。咱们当面验证，在 libstdc++ 的头文件树里搜 POSIX 的痕迹：

```bash
grep -rln "semaphore.h" /usr/include/c++/16/
grep -rln "sem_post" /usr/include/c++/16/
```

两条命令在笔者机器上的输出都是空的。主流实现走的路跟 POSIX 压根不沾边，咱们顺着头文件把实现走读一遍。计数本体的位置在 `bits/semaphore_base.h`，装的就是一个原子整数，acquire 是原子交换的循环。release 的原文不到十行：

```cpp
    _M_release(ptrdiff_t __update) noexcept
    {
      auto __old = __atomic_impl::fetch_add(&_M_counter, __update,
					    memory_order::release);
      if (__old == 0 && __update > 0)
	__atomic_notify_address(&_M_counter, true, true);
      return __old;
    }
```

咱们判读就看一行：`fetch_add` 把计数加了上去，只有旧值是 0（这次加法让计数从无到有）的时候才唤醒等待者。计数本来富余时的 release 连唤醒的开销都省了，一次原子加完就走了。binary 的优化也有实物，同文件末尾挑实现的代码拿模板布尔切了两套，`__platform_semaphore_impl<(_Max <= 1)>`，上限为 1 的走更省的分支。规范的语义条目对此点过头：实现可以对 binary 版做更高效的特化。

那等的人睡在哪儿？咱们跟着 `__atomic_notify_address` 追进 `bits/atomic_wait.h`，看 Linux 分支开头的几行（第 59 行起）：

```cpp
#if defined _GLIBCXX_HAVE_LINUX_FUTEX
  namespace __detail
  {
    // Use futex syscall on int objects.
    using __platform_wait_t = int;
    inline constexpr size_t __platform_wait_alignment = 4;
  }
```

futex 到场了。这个名字在 [ch00 的 OS 线程篇](../ch00-concurrency-fundamentals/03-os-threads-and-cost.md)登记过：无竞争的拿放全在用户态的原子指令上办完，真要睡等了才进内核，futex 是 fast userspace mutex 的缩写。欠了一整章的深讲，咱们现在把它兑现成实物：本篇的 semaphore 睡在它上面，前面几篇的 mutex 与 condition_variable 也睡在它上面，ch03 要讲的 atomic wait 还睡在它上面。Linux 的 libstdc++ 里，线程睡等压的就是这一块底。Windows 那边对应的是 WaitOnAddress 一组 API，思路是一样的：盯着一个地址上的值等它变，宽度是 1/2/4/8 字节的四种。

### 跟条件变量的分界

咱们回看 [阻塞队列那篇](./04-condition-variable-and-bounded-queue.md)，cv 等的是谓词：条件本身可以随便多复杂，得配合 mutex 与 while 的循环判断。而 semaphore 等的是计数，判断的条件只有一个数，换来的好处是不用外配 mutex、不用写谓词、连唤醒都能按跃迁省略。规范的语义条目也给了定性的一句：拿 0 初始化的信号量做跨线程通知，常常比 cv 的表现更好。定性咱们就说到这里，数字要测了才算数，怎么测归 [ch04 的 perf 篇](../ch04-concurrent-data-structures/02-lock-free-stack-and-perf.md)。

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

```text
读者 1 进入，在读 1 人
读者 0 进入，在读 2 人
两读者在场时独占档 try_lock 拿到 = 0（期望 0）
读者全撤后独占档 try_lock 拿到 = 1，protected_value = 8（期望 8）
```

读者进入的两行打印与在读人数的计数在归仓完整版里，计数用的原子只做观测。判读看后两行：读档在场时独占档的 `try_lock` 直接失败，读者撤光之后独占档才进得去门，改动落地。

编排值得咱们说一句。两个读者与 main 用一个空完成函数的 barrier 编成两轮：第 1 轮三方到齐的瞬间，两个读者都持着读档、main 的探针出手，打到稳定的 0。第 2 轮咱们把读者放走，撤光了之后再探，打到的就是 1。咱们没有这套编排的时候，探针可能赶在读者进门之前出手，也可能等读者撤完才出手，演示就成了碰运气。咱们顺带也看到了 barrier 的日常用法：完成函数空着，它就是一个纯粹的节拍器。

用法上还有三件实情要跟您交代。

共享档是有成本的。实现得让读者互相知道彼此在场，多半是内部锁加计数的组合，拿放读档本身是有开销的，读临界区太短的话 shared_mutex 反而不如 mutex 划算。判据是读侧干不干重活：解析、聚合、查大表够重的，才值得咱们换档。

没有原子的升降档。咱们读着读着想写的时候，标准没有从读档直接升到独占档的操作，只能放了再拿。可放的瞬间别人可能插进来改了数据，等咱们重新拿到独占档，此前读到的状态得重新验证。

写者的饥饿问题没有标准答案。读者源源不断的场合，写者的独占档可能一直进不去，防写者饥饿的策略留给了实现，公平性咱们不能当作承诺写进设计。

时间线上还有个冷知识：带计时的 `shared_timed_mutex`（`try_lock_for` 这类带时限的成员）是 C++14 进来的，不带计时的 `shared_mutex` 反而晚到 C++17。选型的顺序咱们维持不变：默认 mutex 配守卫，等 [ch04 的 perf 篇](../ch04-concurrent-data-structures/02-lock-free-stack-and-perf.md)量出读侧争用真是瓶颈，咱们再考虑换档或者上分片。

## `优先级反转`：标准库管不到的一段

`优先级反转` 的机制在 [data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)讲 Mars Pathfinder 时铺过：低任务拿着锁，高任务等的就是这把锁，中任务又把低任务挤下了 CPU，最高的任务被两个低于它的间接卡死。咱们清点工具箱到这里，正好交代标准库在这一段的位置：管不到。std 的 mutex 家族不分任务的轻重，谁急谁不急与拿锁的次序无关，标准里也没有带 `优先级继承` 的锁。对策咱们只能去标准之外找：持锁要短，持锁的期间不睡不让，真要强实时的场合，换 pthread 的 `PTHREAD_PRIO_INHERIT` 属性，或 RTOS（实时操作系统）提供的 `优先级继承` 互斥量。那是嵌入式实时专题的活儿，本卷咱们不展开。

## 容器加锁：一张速查表加一件实物

共享容器怎么上锁，工程上反复出现的路数拢共四条。咱们收成一张表，按粒度从粗到细地排：

| 策略 | 做法 | 要害 |
|---|---|---|
| 一把大锁 | 整个容器配一把 mutex | 最简单也最稳，正确性上从不出岔子，吞吐封顶在锁上 |
| 每桶一锁 | 哈希表每个桶一把锁 | 并行度高，rehash 时桶要搬家，锁跟桶走还是跟表走得设计清楚 |
| 分片锁 | 固定片数，key 映射到片 | 扩容不动锁，热点 key 没救，同一个 key 永远落在同一片 |
| 写时复制 | 整表做快照，`atomic<shared_ptr>` 一换 | 读者零锁，写侧复制贵，内存序的门道归 ch03 |

一把大锁起步不是丢人的事，`先正确性，再性能` 的原则在容器上同样成立：没量出争用之前（量法归 [ch04 的 perf 篇](../ch04-concurrent-data-structures/02-lock-free-stack-and-perf.md)），粗锁常常就已经够用了。真要往细里走的时候，分片锁是工程上最常落地的一条，咱们把它做成实物：

```cpp
// 分片锁：N 把 shared_mutex 摊开竞争，写走独占档，读走读档
template <std::size_t N>
class ShardedCounter {
public:
    void add(std::size_t key, long delta)
    {
        Shard& s = shards_[key % N];    // key 映射到固定分片
        std::lock_guard<std::shared_mutex> lk(s.m);
        s.value += delta;
    }

    long total() const
    {
        long sum = 0;
        for (const Shard& s : shards_) {
            std::shared_lock<std::shared_mutex> lk(s.m);   // 逐片读，不挡别片的写
            sum += s.value;
        }
        return sum;
    }

private:
    struct Shard {
        mutable std::shared_mutex m;    // total() 是 const，锁要 mutable
        long value = 0;
    };
    std::array<Shard, N> shards_{};
};
```

咱们让八个线程各做一万次 `add`，key 的取值撒在 32 个数上、落到 8 个分片里：

```text
分片计数总数 = 80000（期望 80000）
```

三个设计点咱们挨个交代。片数是固定的，跟容器的容量脱了钩，扩容的时候锁一件不动，这正是它比每桶一锁省心的地方。写侧走独占档、`total()` 走的读档，同一片内的读写就是上一节 shared_mutex 那套配合。`mutable` 不是摆样子的装饰：`total()` 是 const 成员函数，锁对象在 const 的世界里也得能动，mutable 就是给这个开的口子。片数给多少是实测的题，练习 4 您自己量，经验起点是并发线程数的同量级。

写时复制诱惑最大的地方在读者零锁，可它的成本摆在明处，每写一次的代价都是复制一份整表，正确性又全系在 `atomic<shared_ptr>` 的内存序上，咱们要讲清楚，只能等 ch03 了。还有一条边界要划清：分片是加锁路线内部的精细化，与无锁走的是两条路。咱们 ch02 讲到这里为止，无锁的那一套（CAS 循环、内存回收）从 ch04 讲起。

## 背压三策：丢、挤、阻塞

咱们回看 [阻塞队列那篇](./04-condition-variable-and-bounded-queue.md) 留下的题：队列满了怎么办，丢、挤、阻塞怎么选。丢是满了就扔：扔新来的（那篇的 push_or_drop 就是它），或踢掉最老的给滑动窗口腾位子，日志、指标这类丢一条不心疼的数据用它，接收的线程绝不被吊住。挤的思路是有界加拒绝：push 失败了就老老实实返回 false，把满了的压力显式交回生产者，重试还是降级的决定权由它自己拿着。阻塞的配法是有界加等待：生产者在条件变量上睡到有空位才醒，吞吐让位于稳定的内存占用，那篇正文实现的就是它。

三策怎么选，咱们看的次序是业务在前：数据能不能丢、生产者能不能慢下来、内存给得起多少，三个问题答完了，选型自己就站出来了。真正要躲开的只有一个组合：无界队列假装什么都能装，装到 OOM（内存耗尽）才算到了头。

## 选型速查表

本章的工具到本篇配齐了，咱们选型的时候按场景对号入座：

| 您要的是 | 拿这件 | 备注 |
|---|---|---|
| 临界区互斥 | mutex 配守卫 | 正源在 [mutex 与 RAII 守卫](./01-mutex-and-raii-guards.md) |
| 一条语句拿多把锁 | scoped_lock | 免疫的边界在 [死锁与现场诊断](./03-deadlock-and-gdb.md) |
| 复杂条件的等待 | condition_variable | 谓词配 while，正源在 [阻塞队列](./04-condition-variable-and-bounded-queue.md) |
| 初始化只跑一次 | call_once 或块级 static | 会失败要重试的用 call_once |
| 全到齐放行一次 | latch | 一次性，归零即弃 |
| 多轮汇合带聚合 | barrier | 完成函数写 noexcept |
| 资源池限流 | counting_semaphore | 计数即许可，没有主人 |
| 单事件跨线程信号 | binary_semaphore | 0 初始化，发收两端随便换线程 |
| 读多写少 | shared_mutex 或分片锁 | 读侧够重才划算 |

## 练习：轮到您动手了

1. barrier 的 noexcept 之问。您把本篇 barrier 演示里完成函数的 noexcept 去掉，编译一遍记录结果，再去翻您编译器的 barrier 头，把 `static_assert` 那行抄下来。条文要求的是 `is_nothrow_invocable`，您的实现查的是哪半条？然后把 latch 那节的集合演示改用 barrier 实现（完成函数留空），再答一问：要改成两轮集合，barrier 版动哪一行就够了，latch 版得多造什么？
2. 资源池加时限。给 semaphore 资源池的用户配 `try_acquire_for` 的限时入座：100ms 拿不到就放弃，统计放弃的次数。再答一问：`try_acquire` 在计数大于零时也可能空手而归，语义为什么授权它这样？提示在本篇 try_acquire 那节，libstdc++ 的注释原文摆在那里。
3. 源码走读。在您的 libstdc++ 头文件树里跑本篇那两条 grep，验证 `sem_t` 的缺席。然后翻开 `bits/semaphore_base.h`，把 `_M_release` 的唤醒条件抄下来，答一问：旧值非零的 release 不唤醒任何人，信号为什么不会丢？
4. ShardedMap。把 ShardedCounter 扩成分片 map（每片一个 `unordered_map` 配一把 `shared_mutex`），实现 put、get、size。实测 1、4、16、64 片的吞吐，观察片数翻倍时吞吐跟不跟。再答一问：不跟的原因里，哪一条与本篇讲的锁开销有关，哪一条要等 ch03 的 cache 行才能解释清楚？

## 下一步

锁与等待的工具配齐了，卷内下一站换地基：[ch03 的原子篇](../ch03-atomic-memory-model/02-atomics-and-happens-before.md)接管 happens-before 的正式定义，本篇押着的 DCLP 那桩悬案，在那边有了正式判据。semaphore 底下的那层 atomic wait，正源在 [ch03 的等待与模式篇](../ch03-atomic-memory-model/04-fence-wait-and-patterns.md)。咱们回头看任务侧的线索，线程池的正源在 [ch05 的线程池篇](../ch05-future-task-threadpool/03-thread-pool.md)，它的开工门闩就是 latch 的主场。动手量更大的活儿，在 [exercises 的阻塞队列 Lab](../exercises/01-bounded-queue) 里候着，全卷的走向见 [卷地图](../)。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 参考资源

- [P1135R6: The C++20 Synchronization Library -- open-std.org](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1135r6.html)
- [P2588R3: Relaxed guarantees for phase completion -- wg21.link](https://wg21.link/p2588r3)
- [std::latch -- cppreference](https://en.cppreference.com/w/cpp/thread/latch)
- [std::barrier -- cppreference](https://en.cppreference.com/w/cpp/thread/barrier)
- [std::counting_semaphore -- cppreference](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::call_once -- cppreference](https://en.cppreference.com/w/cpp/thread/call_once)
- [Storage duration（块级 static 的并发保证）-- cppreference](https://en.cppreference.com/w/cpp/language/storage_duration)
- [Itanium C++ ABI（guard 协议）-- Linux Foundation](https://refspecs.linuxfoundation.org/cxxabi-1.86.html)
- [futex(7) -- Linux man-pages (man7)](https://man7.org/linux/man-pages/man7/futex.7.html)
- [Williams, *C++ Concurrency in Action*, 2nd ed, Manning, 2019](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
