---
title: "数据竞争与 ThreadSanitizer 第一课"
chapter: 0
order: 2
description: "用一个两线程计数器亲眼看见 data race，读标准的定义与 UB 判决，再用 ThreadSanitizer 把看不见的竞争变成一份能读的报告"
tags:
  - host
  - cpp-modern
  - beginner
  - atomic
  - mutex
difficulty: beginner
platform: host
cpp_standard: [11]
reading_time_minutes: 16
prerequisites:
  - "为什么并发：一次阻塞引出的问题"
related:
  - "mutex 与 RAII 守卫"
  - "原子操作与 happens-before"
---

# 数据竞争与 ThreadSanitizer 第一课

咱们在 [为什么并发：一次阻塞引出的问题](./01-why-concurrency.md) 里把并发的收益和代价摆平了。我们还立了三条原则，头一条讲的就是**先正确性，再性能**。这一篇咱们就对付正确性里最难缠的问题：data race，中文里叫数据竞争。它难缠的地方是它看不见——程序不一定崩给您看，跑出来的结果还常常是对的，错只在某个特定的交错里发生，而那个交错未必来。

所以本篇还有第二件事：把全卷的第一件工具 ThreadSanitizer（下文简称 TSan）请出来，让您在本机上亲眼看见一次数据竞争。

路线咱们也交代清楚。先写一个很小的坏程序，把标准对 data race 的定义读一遍，弄明白标准为什么把它判成未定义行为，再看一眼修复的样子；接着 TSan 登场，走一个从编译到读报告的完整来回。本篇的代码、命令和现象，全部可以在一台普通的 Linux 机器或 WSL2 上复现，需要的只有 g++ 和几十行源码。

## 一个看起来无害的计数器

程序小，可以整篇贴出来：全局放一个 int，两个线程各给它加十万次，join 完了打印。

```cpp
#include <iostream>
#include <thread>

int counter = 0;  // 普通 int：非 atomic，也没有锁保护

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        ++counter;              // 读 -> 加 -> 写，三步
    }
}

int main()
{
    std::thread t1(increment, 100000);
    std::thread t2(increment, 100000);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << "\n";
    return 0;
}
```

您可能看不懂，没关系，抄就完事了。编译运行就是普普通通的两条命令：

```bash
g++ -O2 -pthread 01_data_race.cpp -o race
./race
```

您猜打印多少？两边的循环加起来是二十万次自增，期望的自然是 200000。笔者在 WSL2 上（GCC 16.2.1）用 -O2 编译，连着跑了十次，十次跑出来的全是 200000：一个数都没丢。

咱们的程序看着健康得很，健康到您可以放心地把它忘了。

那把优化关掉再试一把：命令一字不改，只把 -O2 换成 -O0。十万档连跑十次，居然还是十个 200000。先别收工——竞争碰不碰得上，跟机器当时的调度强相关，同一份代码换个量级就能翻脸。咱们把每边的自增从十万加到两百万，把竞争窗口撑大再跑，现场就来了。您点"动手试一试"自己跑一遍：

<OnlineCompilerDemo
  title="动手验证：两百万次自增，丢一半是什么样"
  source-path="code/examples/vol5/12_data_race_counter.cpp"
  description="两个线程各给全局 counter 自增两百万次，期望 4000000。多跑几遍：每次的数字都不同，而且几乎到不了四百万——丢掉的那一百多万次自增，就是 data race 的现场。"
  run-options="-O0 -std=c++17 -pthread"
  allow-run
/>

笔者的实测记录（WSL2 Arch Linux、内核 6.18、g++ 16.2.1、AMD Ryzen 7 9700X）：十万档上 -O2 与 -O0 各跑十次，二十个 200000，一次没丢；两百万档的 -O0 十次跑出来的是——

```text
counter = 2129414
counter = 2306182
counter = 2959094
counter = 2153043
counter = 2161568
counter = 2335754
counter = 2097602
counter = 2195795
counter = 2232904
counter = 2533209
```

期望四百万，最好的一回也只摸到二百九十万出头，最差的一回 2097602。啥？？？怎么优化关掉了反而错了呢？这就是我们从并发开始，就需要掌握的能力（至少你要有意识的让LLM往这个方向上查）。如果程序看起来表现异常，那么，麻烦您看汇编，自然会有答案。

> 毕竟汇编就是机器码的直接映射，查这个，最直接！

objdump，启动！

```text
$ objdump -d --no-show-raw-insn race | grep -A 4 '_Z9incrementi>:'
00000000000013d0 <_Z9incrementi>:
  13d0:	test   %edi,%edi
  13d2:	jle    13da <_Z9incrementi+0xa>
  13d4:	add    %edi,0x2dba(%rip)        # 4194 <counter>
  13da:	ret
```

您也可以照着跑：-d 是反汇编，--no-show-raw-insn 把机器码的字节藏掉。C++ 的函数名进了二进制会换上修饰名（mangled name），`increment(int)` 在符号表里换上的名字是 `_Z9incrementi`，grep 就按这个名字抓。地址和偏移每台机器都会不一样，指令的形状是一样的。

您看，一下子真相大白了，因为我们压根就没执行循环。。。

我们的编译器把整个 for 折叠成一条 add 指令，直接往 counter 上加 times——单线程语义下这是标准的归纳变换，天经地义。数据竞争原封不动地留在原处，只是冲突窗口从十万条指令缩成一条，碰上的概率跟着缩水，结果就几乎次次都对了。-O0 下老老实实循环两百万次的版本窗口大得多，丢得理直气壮：它的下限就是两百万整——每轮两边都读到同一个数、写回同一个数，一轮总数只涨一，两百万轮就只涨两百万。本机最差的那回 2097602，已经贴着这条地板了。

所以有件事请您放在心上：**结果对了，不等于程序就对了。** 判断对错靠标准说话，不靠某一次运行的输出。加 printf 调试也没用——打印本身就改变时序，bug 说不定就被那一行输出吓跑了。这类抓不住的 bug 有个名字：Heisenbug。

肉眼是看不住它的，运气也是靠不住的，咱们需要一个不改变程序语义的观察者。不过在拿到它之前，得先弄清楚要观察的到底是什么，所以这就去读标准。

## 标准怎么说：data race 的定义

data race 是 C++ 标准里的正式术语，不是咱们自己造的词，它的定义住在 [intro.races] 条款里：

> `The execution of a program contains a data race if it contains two potentially concurrent conflicting actions, at least one of which is not atomic, and neither happens before the other.`（一个程序的执行里，出现了两个潜在并发的冲突动作，其中至少一个不是原子的，而且谁也不 **happens-before** 谁。）紧接着的判决是：`Any such data race results in undefined behavior.`

“欸你怎么又说黑话。。。”，先别急，懂你意思，我慢慢说。

头一个是 **conflicting（冲突）**：它判的是**一对访问**，成立要同时满足两条——两个访问落在**同一个内存位置**上，且其中**至少一个是写**。组合就三种：写对写、读对写，都算冲突；只有读对读不算——两个都只是读，谁也改变不了结果。本程序里两个线程都在写 counter 的那四个字节，写对写，**冲突成立**。

第二个是 **potentially concurrent（潜在并发）**：它指的是不同线程里的动作，或者同一线程与它的信号处理函数之间的动作。咱们只关心前者——两个 increment 跑在两条线程上，要件也成立了。

第三个是 **happens-before**，对咱们最陌生，直觉版本一句话：**A happens-before B，就是标准担保 A 先发生、且 A 的效果 B 一定看得到。** 这个先后关系不是碰运气排出来的，而是由**同步动作**——锁、join、线程创建——建立的。拿本程序对号：`t1.join()` 返回之后，main 线程再去读 counter，join 担保线程里的写全部完成、对 main 可见，这一对读写次序是定的，不成数据竞争；同理，程序开头那句 `std::thread t1(increment, 100000)`，意思就是"开一条新线程，让它去跑 increment 这个函数"。在这句之前发生的写，新开的那条线程也一定看得到，也不成数据竞争。真正的数据竞争出在两个线程的 `++` 之间：那里没有任何同步动作，谁也不 happens-before 谁，次序完全看调度脸色。

> 咱们把定义折叠成一张检查单，往后每怀疑一处竞争，就拿它过一遍：**同一个内存位置吗？至少一个写吗？有 happens-before 隔开吗？** 三条全中，data race 成立。差一条没中的，那才谈得上没竞争。

拿计数器走一遍：同一个位置、两边都是写、中间没有锁也没有哪个 join 把它们隔开——三条全中，判得干脆。

happens-before 有精确的数学定义，它的正源在 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 那一篇，本篇到直觉为止。

## 为什么定成未定义行为

有朋友可能会问：标准为什么不干脆给个说法，比如带竞争的读最多拿到旧值，让咱们心里有个预期？Hans Boehm 是 C++ 内存模型的主要设计者之一，他专门写过一页文章回答这个问题，而他的论证，咱们可以用自家机器上刚发生的事原样复述。

咱们刚才看见的，就是把十万次 `++` 折叠成一条 add 的变换。这个变换在单线程语义下挑不出毛病，可它顺手把带竞争的执行结果改了个面目全非。您想，标准若给数据竞争规定任何确定的语义——哪怕温和到"最多拿到旧值"——这类变换就得逐个审查，编译器的手脚就被捆住了。标准选了另一条路：**带竞争的程序没有语义，所以编译器怎么优化都不算违约，责任全部落在写代码的人肩上。** Boehm 的立场说得很直白：`Data races among ordinary variables are a bug`。

UB 的许可范围也比"读到旧值"宽得多：同一个变量读到撕成两半的值、写被重排到看不懂的位置、一个分支整个被优化没了，全在许可范围之内。咱们用的 x86 机器上，对齐的 int 读写本身不撕，可换了平台、换了编译器，没人给您这个担保。还有人琢磨过"良性竞争"的说法，觉得有些竞争反正看起来没事——那个 -O2 下次次全对的计数器就是活例子。恰恰是这样的没事，UB 才最阴险：**它不欠您一个崩溃，等真崩的时候也不打招呼。**

这话听着冷酷，其实是对写代码的人的一种尊重：标准不糊弄您说大概没事，它把边界画清楚，把判定的工具也交给您。本篇剩下的篇幅，就是带您把工具拿到手。

::: details 无竞争的世界有秩序：DRF-SC

只要程序没有 data race，原子操作用的又都是默认内存序，它的行为就有一个统一的顺序一致语义兜底。这个保证有个名字，叫 **DRF-SC**，也就是无竞争程序的顺序一致性。它的正式讲法放在[原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 那一篇，您现在只需要认下一件事：无竞争的世界里有秩序，有竞争的世界里没有标准。

:::

## 修复长什么样

修复的完整体系——锁族、条件变量、原子、内存序——占整整两章。但 counter 的病根咱们已经看明白了：两个非原子的写之间，缺的就是次序。修法顺着病根来，要么给这对访问建立确定的次序，要么让写本身不可分割。锁的版本改动最小：

```cpp
#include <iostream>
#include <mutex>
#include <thread>

int counter = 0;
std::mutex counter_mtx;

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        std::lock_guard<std::mutex> lock(counter_mtx);
        ++counter;              // 两个线程的 ++ 从此排上了队
    }
}
// main 与打印部分原样不动
```

一把 mutex 看住了 counter 的全部访问，两个线程的 `++` 从此排上了队，数据竞争也就消失了。按定义说：锁的拿与放建立了 happens-before，前一个持有者的写，对下一个持有者是可见的。`lock_guard` 在这里当黑盒用，它替咱们管拿锁放锁、异常路径上也不漏放，RAII 的完整讲法在 [mutex 与 RAII 守卫](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)。

另一条路是把 counter 换成 `std::atomic<int>`，让写本身成为不可分割的一步：

```cpp
#include <atomic>

std::atomic<int> counter{0};

void increment_atomic(int times)
{
    for (int i = 0; i < times; ++i) {
        counter.fetch_add(1);     // 一条原子读改写，顶掉读-加-写三步
    }
}
```

`fetch_add` 用一条原子读改写顶掉了读-加-写三步，单变量的计数场景往往比锁更轻。不过它还能带一个内存序实参（`memory_order_relaxed` 就是其中一种），这一片水很深，正源在[原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 和[内存序](../ch03-atomic-memory-model/03-memory-ordering.md) 两篇，本篇一个字都不展开。挑选的口味也好记：**单变量计数用 atomic，要协同变化的变量一多，就用 mutex 把整段临界区一起看住。**

修没修好？口说无凭，让工具来验。

## TSan：把看不见的数据竞争变成报告

### 编译与运行

要启用 TSan，编译和链接都带上 `-fsanitize=thread`。g++ 的一条命令就把两头全占了：

```bash
g++ -fsanitize=thread -g -O2 -pthread 01_data_race.cpp -o race_tsan
./race_tsan
echo $?     # 报出数据竞争时，默认退出码是 66
```

几个 flag 挨个过一遍。`-fsanitize=thread` 是本体：编译器在程序里的每个内存访问前后插桩，运行时由 TSan 的运行库逐对核对。`-g` 让报告的调用栈带上源码位置，少了它，您看到的就只是一串地址。`-pthread` 次次都带上，libstdc++ 的多线程程序都需要。

最有说头的是 -O2：有些老教程叮嘱开 TSan 就别开优化，说调用栈会没法读。官方 wiki 的现行口径恰好反着写——`To get a reasonable performance add -O2`，`Use -g to get file names and line numbers in the warning messages`。笔者的本机冒烟里，-O2 加 -g 的栈解析得好好的，咱们按 wiki 来。

插桩为什么不算改变语义？因为 TSan 加的是核对，不是次序：它记录每次访问、检查配对，不插入等待、也不推迟谁的执行。所以它**看得见竞争，却治不了竞争**，治病的还是您手里的锁和原子。

跑起来是什么阵仗？TSan 把一份报告刷到了 stderr，程序照常打印 counter——数字还是那个看起来健康的 200000，进程以退出码 66 收了场。报告在前、counter 在后，是因为 stderr 不经缓冲，而 stdout 的内容要等进程退出才落下来。66 不是崩溃的信号，它代表 TSan 默认的失败退出码，搁持续集成（CI）里拿 `$?` 直接判定，一行脚本的事。笔者做过的本机冒烟：GCC 16.2.1、WSL2 内核 6.18，这命令拿来就能用，两线程十万次自增的程序一次就报出了数据竞争。

CMake 工程里用的也是同一个口径，编译、链接两头都配上：

```cmake
add_executable(race_tsan 01_data_race.cpp)
target_compile_options(race_tsan PRIVATE -fsanitize=thread -g)
target_link_options(race_tsan PRIVATE -fsanitize=thread -pthread)
```

本机（WSL2 Arch Linux、g++ 16.2.1）这份命令跑出来的报告，原文照贴：

```text
==================
WARNING: ThreadSanitizer: data race (pid=157474)
  Read of size 4 at 0x5555555581d4 by thread T2:
    #0 increment(int) /tmp/sysprog-race/01_data_race.cpp:8 (race_tsan+0x152c) (BuildId: c2829e9ff...)
    #1 void std::__invoke_impl<void, void (*)(int), int>(std::__invoke_other, void (*&&)(int), int&&) /usr/include/c++/16/bits/invoke.h:63
    #2 std::__invoke_result<void (*)(int), int>::type std::__invoke<void (*)(int), int>(void (*&&)(int), int&&) /usr/include/c++/16/bits/invoke.h:98
    #3 void std::thread::_Invoker<std::tuple<void (*)(int), int> >::_M_invoke<0ul, 1ul>(std::_Index_tuple<0ul, 1ul>) /usr/include/c++/16/bits/std_thread.h:303
    #4 std::thread::_Invoker<std::tuple<void (*)(int), int> >::operator()() /usr/include/c++/16/bits/std_thread.h:310
    #5 std::thread::_State_impl<std::thread::_Invoker<std::tuple<void (*)(int), int> > >::_M_run() /usr/include/c++/16/bits/std_thread.h:255
    #6 <null> <null> (libstdc++.so.6+0xea858)

  Previous write of size 4 at 0x5555555581d4 by thread T1:
    #0 increment(int) /tmp/sysprog-race/01_data_race.cpp:8 (race_tsan+0x153e) (BuildId: c2829e9ff...)
    #1 void std::__invoke_impl<void, void (*)(int), int>(std::__invoke_other, void (*&&)(int), int&&) /usr/include/c++/16/bits/invoke.h:63
    #2 std::__invoke_result<void (*)(int), int>::type std::__invoke<void (*)(int), int>(void (*&&)(int), int&&) /usr/include/c++/16/bits/invoke.h:98
    #3 void std::thread::_Invoker<std::tuple<void (*)(int), int> >::_M_invoke<0ul, 1ul>(std::_Index_tuple<0ul, 1ul>) /usr/include/c++/16/bits/std_thread.h:303
    #4 std::thread::_Invoker<std::tuple<void (*)(int), int> >::operator()() /usr/include/c++/16/bits/std_thread.h:310
    #5 std::thread::_State_impl<std::thread::_Invoker<std::tuple<void (*)(int), int> > >::_M_run() /usr/include/c++/16/bits/std_thread.h:255
    #6 <null> <null> (libstdc++.so.6+0xea858)

  Location is global 'counter' of size 4 at 0x5555555581d4 (race_tsan+0x41d4)

  Thread T2 (tid=157477, running) created by main thread at:
    #0 pthread_create <null> (libtsan.so.2+0x616c9)
    #1 std::thread::_M_start_thread(std::unique_ptr<std::thread::_State, std::default_delete<std::thread::_State> >, void (*)()) <null> (libstdc++.so.6+0xea961)
    #2 <null> <null> (libc.so.6+0x27780)

  Thread T1 (tid=157476, finished) created by main thread at:
    #0 pthread_create <null> (libtsan.so.2+0x616c9)
    #1 std::thread::_M_start_thread(std::unique_ptr<std::thread::_State, std::default_delete<std::thread::_State> >, void (*)()) <null> (libstdc++.so.6+0xea961)
    #2 <null> <null> (libc.so.6+0x27780)

SUMMARY: ThreadSanitizer: data race /tmp/sysprog-race/01_data_race.cpp:8 in increment(int)
==================
counter = 200000
ThreadSanitizer: reported 1 warnings
```

### 报告怎么读：三个问题

报告的结构长年都是固定的。一行 WARNING 报的是事故，冲突的双方各占一段，打头要么是 `Write of size 4` 要么是 `Read of size 4`，各配一份调用栈；一行 Location 指明内存位置；随后的两段是线程创建栈，打头都用 `created by main thread at`；末尾一行 SUMMARY 压轴。读的时候，笔者的习惯是拿三个问题过一遍。

**头一个：冲突的是哪两处访问？** 报告把双方成对摆出来，一段是肇事的 Write（或 Read），另一段是 Previous write（或 Previous read），各自都标上了文件名和行号。Previous 这个词还顺手把前后排好了：标了 Previous 的那段时间上在前。咱们这个程序里，两段行号都落在 increment 里那行 `++counter` 上，读对写的冲突，与咱们拿定义对出来的判词一模一样。

**第二个：是哪两个线程？** 每段访问都缀着所属线程的编号，报告末尾还给出每个线程的创建点调用栈，打头也是 `created by main thread at`。往下会看到 `pthread_create` 和 `std::thread` 内部的帧；您要找的 main 里那两行构造，开了优化之后未必留在栈上（冒烟那次它就被裁掉了），看到出身也就够了。这一问在线程多的时候特别有用：五个线程抢一个变量，您总得知道是哪两个搅在一起。

**第三个：两段访问之间缺了什么？** 咱们替它答：缺的是同步——没有锁的隔离、没有原子的帮忙，也没有 join 建立的次序隔开它们。数据竞争的定义反着用，就是 TSan 的判据。三个问题答完，一份报告也就读透了：**谁、在哪、中间缺什么。**

读栈还有个小窍门：咱们这次的冒烟里，头一帧干干净净地就是 increment，再往下数几帧，`invoke.h`、`std_thread.h` 的 `_M_run` 之类的帧就冒出来了。那些是 `std::thread` 内部替咱们跑函数的机制帧，不认识也不用慌，读报告时跳过去，认自己工程路径的那几帧就行。

### 修复版复跑

给坏程序读完报告，再把修复版也过一遍 TSan，一个完整的来回就走下来了：

```bash
g++ -fsanitize=thread -g -O2 -pthread 02_data_race_mutex.cpp -o race_fixed_tsan
./race_fixed_tsan
echo $?     # 0：报告干净
```

counter 老老实实打出 200000，TSan 一个字没说、退出码也从 66 回到了 0。代码仓里给本篇的四个示例各配了 CMake 目标：`01_data_race.cpp` 是坏计数器，`02_data_race_mutex.cpp` 是锁修复版，`03_check_then_act.cpp` 是后面 check-then-act 一节的向量例，`04_deadlock_reorder.cpp` 是结尾的死锁例，您拉下来就能复跑本篇的每一步。

### 边界、开销与搭档

再漂亮的算法也有工程边界，两头都交代清楚。**一头是它报的都作数**：只要整个程序都插着桩编译，TSan 报出来的每一份报告，都对应这次执行里真实发生过的一对冲突访问，它不会编故事。**另一头是没报不作数**：TSan 只看这次执行真跑到的路径，您的测试没让两个访问碰上面，它也无从说起，所以并发测试要多换线程数、多换任务粒度、多跑几轮。官方 wiki 也明说了：`There is tiny probability to miss a data race though`——影子格里存的历史有限，极端场景下的旧记录会被挤掉。合成一句：**报了必是真竞争，没报不等于没竞争。**

开销上，wiki 给典型程序的口径是 `for a typical program, memory usage may increase by 5-10x and execution time by 2-20x`，内存涨五到十倍、执行时间慢二到二十倍，量级上有数就好。TSan 的二进制是给测试和 CI 用的，咱们不拿它跑生产。

还有一条硬限制：`-fsanitize=thread` 与 `-fsanitize=address`、`-fsanitize=leak` 不能同开，编译器会直接报错，想要两套 sanitizer 就得分开构建两份二进制。ASan 管内存错，TSan 管数据竞争，它的分工表在 [线程参数与生命周期陷阱](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md) 里会正式见到。

本机的实测对比（十万档计数器，同一台 WSL2 机器）：普通版（-O2）单次运行约 2 ms、峰值内存约 12 MB；TSan 版（-fsanitize=thread -g -O2）约 19 ms、约 22 MB。程序太小，绝对数没多大参考价值，量级上的账倒是能对上：慢了约十倍、内存多占八成，都落在官方 wiki 给的 2–20 倍与 5–10 倍口径之内。

> 还有一桩环境病值得认脸：社区里有一批 WSL2 用户报告过，他们的内核在 6.6.6 及以上，部分环境里的 TSan 一启动就报 `FATAL: ThreadSanitizer: unexpected memory mapping`，连程序的边都没摸到。报告者排查出来的根因是内核的 `vm.mmap_rnd_bits` 设成了 32，与 TSan 的影子内存映射谈不拢，修复办法是把 `sudo sysctl vm.mmap_rnd_bits=28` 打上再重跑。咱们 6.18 内核的机器不受影响，您要是遇上了就回头查它，没遇上别折腾。环境病和竞争病要分清：报 `unexpected memory mapping` 是 TSan 自己没起来，报 `WARNING: ThreadSanitizer` 才是它真抓到了竞争。

::: details 它凭什么判数据竞争：向量时钟与影子内存

现行的版本叫 TSan v2，GCC 和 Clang 附带的就是它，跑的是纯 happens-before 算法。您可以想象每个线程身上挂着一座**向量时钟**，同步事件——拿锁、放锁、线程创建、join 这一类——会推进各自的时钟，在时钟之间建立起偏序；每次内存访问都被记进**影子内存**，按 8 字节一格分，每格里存的只是寥寥几条历史。两座时钟对不上偏序、访问又起了冲突，它这才报案。

向量时钟的名字听着玄，直觉却简单：每座时钟记的是自家线程看过的世界，同步事件就是两个世界的碰头对表。一旦对上了表，双方就互相看得见了；没对过表的两座时钟，彼此的访问就按无序处理——宁可多查，也不放过。

拿两个版本各走一遍就明白了。坏版本里，两个线程从出生到结束都没遇上任何同步事件，两座时钟永远对不上，于是每一对指向 counter 的冲突访问都成了数据竞争。修复版里，t1 放锁、t2 拿锁的一来一回把两座时钟对上了，t1 在锁里的写对 t2 在锁里的读也可见了，次序建立，数据竞争不再成立。**TSan 判的正是定义里那个 happens-before，一点多余的东西都没夹带。**

这里要专门纠正一个流传很广的旧说法：不少老资料说 TSan 走的是混合算法，也就是 happens-before 加上锁集分析（lockset）。那是早期版本的旧黄历，现行的 v2 用的就是纯 happens-before 加向量时钟。锁集那一路的思路是两个线程没持有共同的锁就报案，听着直观，误报概率却高得很。您查资料时留个心眼：讲算法的资料，以官方 wiki 的 Algorithm 页为准。

:::

## race condition：另一个竞争，另一种错

咱们还得把一对双胞胎请出来辨一辨：data race 和 race condition，中文里两个名字都带竞争俩字，讲混的人一片一片。

两者的分工是这样：**data race 是标准术语**，判据落在内存层面——同位置、至少一写、无 happens-before。**race condition 是工程概念**，宽得多——程序的结果取决于线程的交错顺序。两者的关系，咱们只押一个可靠的方向：**没有 data race 的时候，race condition 也还是可以存在的。**

为什么会这样？看一眼下面这种写法就懂了：每一次对共享向量的访问都端端正正地拿着锁，检查容量和真正入队却是两段分开的临界区，中间那段窗口里，世界可能已经变了。

::: details 例子：锁住了每一处访问，上限还是被突破

```cpp
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

std::vector<int> data;
std::mutex data_mtx;

void add_if_not_full(int value)
{
    {
        std::lock_guard<std::mutex> lock(data_mtx);
        if (static_cast<int>(data.size()) >= 100) {
            return;               // 检查：拿着锁
        }
    }                             // 锁放下了，窗口敞开
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    {
        std::lock_guard<std::mutex> lock(data_mtx);
        data.push_back(value);    // 操作：重新拿锁
    }
}
```

驱动代码让两个线程各推 60 个数，上限定的是 100。数据竞争还有吗？没有了，每一次对 data 的访问都在锁里，TSan 跑下来全程安静。可是检查和入队是两段分开的临界区，中间的窗口敞着：两个线程都趁着 size 是 99 通过了检查，然后各自入了队，上限就被突破了。咱们把窗口拉宽到一毫秒跑，出来的 size 是 101，超编看得明明白白；把那行睡删掉，窗口缩回几纳秒，超编就难得一见了，可代码一处没改，错还稳稳留在原地。

锁管住了内存层面的冲突，却管不住"查一眼然后动手"被拆成两截的逻辑漏洞。治本的做法是把检查与操作放进同一个临界区，让它们成为不可分割的动作，同样的讲法在 [mutex 与 RAII 守卫](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md) 那篇细看。这类错的学名叫 **check-then-act**：检查之后、动手之前，世界就变了。它在单线程里也有影子——查一眼文件存在就去打开，两步之间文件就被人删了，并发只是把窗口拉宽到了任意时刻。

:::

反方向的关系咱们把话说软：data race 往往同时也是 race condition，毕竟它的结果确实依赖交错，但这个方向不是必然成立的，判定要靠定义而不是感觉。层次上这么排：**消除 data race 是底线**，锁和原子也就够了；**消除 race condition 还得靠接口的设计**，那一层的功夫贯穿第 2 章整章。

## 死锁与它的亲戚：只认个脸

并发的问题家族里还有几位常客，本篇请他们集体亮个相，免得您以为竞争就是全部。咱们只教认脸：见过名字、知道大概症状、知道正源在哪，遇到了不慌，这也就够了。

名气最大的是**死锁**：两个线程各持一把锁，又伸手去够对方手里的那把，谁也不肯撒手，程序就永远停在了那儿。工程上最常用的破法是**统一锁的顺序**——全工程都按同一个顺序拿锁，循环等待也就无从形成；C++17 的 `std::scoped_lock` 还能一次锁多把，内部带着避免死锁的获取策略。程序真挂住了怎么救、gdb 的三个命令怎么下，正源在[死锁与现场诊断](../ch02-mutex-condition-sync/02-deadlock-and-gdb.md) 那篇。

另外两位亲戚：**活锁**是线程都在动、CPU 都在烧，就是没有一点进展——两边都客气地互相礼让，永远也让不开；**饥饿**是有些线程永远轮不到资源，别人热火朝天，它一场一场地等下来，每一场都没它的份。还有一位**优先级反转**：低任务拿到了锁、高任务等着这把锁，中任务又把低任务挤下了 CPU，结果最高的任务被两个比它低的间接卡死。1997 年的 Mars Pathfinder 在火星上反复复位，根子就是它干的。这几位的病理各不相同，药方也不同，深讲都在第 2 章的[同步原语工具箱](../ch02-mutex-condition-sync/06-sync-primitives-toolkit.md)。

::: details 死锁长什么样（配套示例 04_deadlock_reorder.cpp）

```cpp
std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> a(mtx_a);      // 拿 A
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> b(mtx_b);      // 等 B：B 在 thread2 手里
}

void thread2()
{
    std::lock_guard<std::mutex> b(mtx_b);      // 拿 B
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> a(mtx_a);      // 等 A：顺序反了过来
}
```

中间那 50 毫秒的睡是笔者加的，用来把交错的窗口拉宽，这程序基本一跑就死锁，您拿 `timeout 3 ./deadlock` 跑它，三秒后被强制掐断，这就是死锁的样子。死锁的配方，Coffman 几位作者在 1971 年就总结齐了：互斥、持有并等待、不可剥夺、循环等待，四样同时凑齐就发生，打破任何一样都能破局。

:::

## 练习

三道题分别对着笔算、判定、工具全流程三个层次，难度递进，咱们建议您全做。

### 练习 1：丢更新的笔算与实测

拿本篇的计数器当标本，笔算两个问题：结果的理论上限是多少？下限又是多少？提示是：最坏的交错为两个线程完全同步走，每一轮里两边都读到同一个值、也写回同一个值，十万轮下来总数只涨了十万。

然后用 -O0 编译裸跑十次，记下见到的最大值和最小值，跟笔算的对一对：

```bash
g++ -O0 -pthread 01_data_race.cpp -o race0
for i in $(seq 1 10); do ./race0; done
```

末了再想一层：为什么 -O2 下几乎次次 200000？文中给过答案，您拿 objdump 亲手再看一遍更扎实。

### 练习 2：给一段代码判数据竞争

下面这段代码有没有数据竞争？请您自己判一遍：有的话，按标准定义把冲突的一对访问标出来——位置在哪、谁读谁写、缺的是哪个要件。判定时只认定义，不认运行结果。

```cpp
std::atomic<bool> ready{false};
int value = 0;

void producer()
{
    value = 42;                          // (A)
    ready.store(true);                   // (B)
}

void consumer()
{
    while (!ready.load()) {              // (C)
        std::this_thread::yield();
    }
    std::cout << value << "\n";          // (D)
}
```

答案带点反直觉的意思：**没有**。默认内存序下，(B) 与读到 true 的 (C) 之间建立了 happens-before；[intro.races] 点名的同步动作包括原子操作和 mutex，默认内存序是算数的，(A) 又排在 (B) 前面，(D) 排在 (C) 后面，一环扣一环地把 (A) 和 (D) 隔开了。

> 进阶一问（等您读完第 3 章再回来做）：把 (B)(C) 都换成 `memory_order_relaxed` 再判一次，答案就翻面成"有"——relaxed 的原子操作不建立这样的次序，value 上的读写成了没看住的竞争，TSan 亲测会报。翻面的机关在哪，正是[内存序](../ch03-atomic-memory-model/03-memory-ordering.md)整篇要讲的事。

### 练习 3：TSan 全流程走一遍

您用 TSan 编译本篇的坏程序，跑出报告，从里面抄下三样东西：冲突双方的行号，两个线程的创建行，Location 指的变量。然后加上 mutex 修复，配套代码仓里的锁修复版 `02_data_race_mutex.cpp` 可以对答案，复跑确认报告干净、退出码从 66 回到 0。命令和输出都留好，从本篇起这就是咱们每次并发交付的标准动作。

再往前一步，您可以把两条构建写进 CI：普通构建跑功能用例，TSan 构建跑同一批用例，退出码非零就拦下来。工具进了流水线，才算真正站住了脚。

## 本篇收进包里的东西

- 判据三条：同一个内存位置、至少一个写、没有 happens-before 隔开，全中即数据竞争。
- 定义末尾那句狠话：任何 data race 都是未定义行为，结果对也不算数。
- TSan 三问：哪两处访问、哪两个线程、中间缺了什么同步。
- 工具的边界：报了必是真竞争，没报不等于没竞争，并发测试要广。

## 下一步

全卷的第一件工具收进包里了。还有一件小事说在前面：本卷后面的代码，都默认您会用本篇的两条构建——普通构建与 TSan 构建。工具一旦上了手，就不再专门教，咱们只用它。

到这里第 0 章就收尾了。接下来咱们跟线程正面打交道，[第 1 章 的 std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md) 会把构造、传参、收尾的讲究一篇篇讲全。至于一条线程在操作系统那边是什么、有多贵，量法记在[第 4 章开头的 OS 线程与开销](../ch04-concurrent-data-structures/00-os-threads-and-cost.md)。想马上动手的读者，[练习体系](../exercises/) 的 Lab 00 就是工具链与第一场竞态，正对着本篇的靶心。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="C++ 标准草案"
    title="[intro.races] 条款"
    url="https://eel.is/c++draft/intro.races"
    chapter="data race 的定义与 UB 判决的原文"
  />
  <ReferenceItem
    :id="2"
    author="cppreference"
    title="Multi-threaded executions and data races"
    url="https://en.cppreference.com/w/cpp/language/multithread"
    chapter="术语的展开讲法"
  />
  <ReferenceItem
    :id="3"
    author="cppreference"
    title="std::thread::join"
    url="https://en.cppreference.com/w/cpp/thread/thread/join"
    chapter="join 建立 happens-before 的正式表述"
  />
  <ReferenceItem
    :id="4"
    author="cppreference"
    title="std::atomic::fetch_add"
    url="https://en.cppreference.com/w/cpp/atomic/atomic/fetch_add"
    chapter="原子读改写"
  />
  <ReferenceItem
    :id="5"
    author="Hans Boehm"
    title="Why undefined semantics for C++ data races?"
    url="https://www.hboehm.info/c++mm/why_undef.html"
    chapter="为什么定成 UB 的完整论证，本篇为什么定成未定义行为一节的底稿"
  />
  <ReferenceItem
    :id="6"
    author="google/sanitizers wiki"
    title="ThreadSanitizerCppManual"
    url="https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual"
    chapter="-fsanitize=thread 的用法、-O2 与 -g 的口径、开销与漏报说明"
  />
  <ReferenceItem
    :id="7"
    author="google/sanitizers wiki"
    title="ThreadSanitizerAlgorithm"
    url="https://github.com/google/sanitizers/wiki/ThreadSanitizerAlgorithm"
    chapter="v2 纯 happens-before 算法，向量时钟与影子内存"
  />
  <ReferenceItem
    :id="8"
    author="GCC 手册"
    title="Instrumentation Options"
    url="https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html"
    chapter="-fsanitize 各开关的官方说明"
  />
  <ReferenceItem
    :id="9"
    author="Coffman, Elphick, Shoshani"
    title="System Deadlocks"
    :year="1971"
    url="https://doi.org/10.1145/356586.356588"
    chapter="ACM Computing Surveys 3(2)，死锁四条件的出处"
  />
  <ReferenceItem
    :id="10"
    author="Anthony Williams"
    title="C++ Concurrency in Action, 2nd ed"
    publisher="Manning"
    :year="2019"
    url="https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition"
    chapter="Manning"
  />
</ReferenceCard>
