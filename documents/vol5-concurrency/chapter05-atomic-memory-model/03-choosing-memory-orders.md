---
title: “选择内存序需要什么依据”
description: “从 seq_cst 的全体次序到 relaxed 的只保原子，中间的 release 与 acquire、acq_rel 与被弃用的 consume 逐档交代保什么、不保什么、各花多少，判据落成数桥的三问。”
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20, 26]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 选择内存序需要什么依据

5.2《[一个线程怎样观察另一个线程的修改](02-happens-before)》收尾的时候，桌面上摆着咱们一起量出来的两份读数：relaxed 档每一千万轮里命中几万次，seq_cst 档整整一千万轮零次。中间的档位当时留了口。acquire 与 release 您都碰过了。[《从错误程序认识竞争》](../chapter02-sharing-data/01-from-wrong-programs)的手工自旋锁里有它，[《原子操作解决到哪一步》](01-what-atomics-solve)的 `atomic_flag` 演示里有它，5.2 的 litmus 代码里也有它。acq_rel 还是个没见过面的新词，本篇正式介绍。参数到底怎么挑、凭什么挑，归本篇交代，咱们一档一档把它说清。

咱们把立场摆在前面：给判据，不给背答案。六档参数背下来，换一个场景照样发懵。判据只有一座，就是 5.2 立起来的那座桥。您要检查的读写之间需要桥，内存序的任务就是把桥的两端与桥上的货安排好，挑哪一档，看桥要连什么。

欠着的几笔，咱们在本篇一起清。5.1 讲 `atomic_flag` 的时候留了话，`test_and_set` 与 `clear` 后面的 acquire 与 release 归本篇专讲。5.2 的 litmus 架子借了 release 与 acquire 的握手，也留了话，正式讲在这篇。5.2 结尾那句“隔着的几档内存序各保什么、各花多少”，本篇拿指令对照来回答。1.5《[让工作有序结束](../chapter01-start-concurrency/05-jthread-and-stopping)》讲停止标志时提过 relaxed 连可见性都不保证，5.2 数出它桥一节不搭，修法也在本篇给。

照例交代实验。文中的程序收在 code/examples/vol5/chapter05/，可在本页直接运行（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2 内核 6.18.33.2，16 逻辑核，2026-11-16）。litmus 的三档我各跑了两遍，篇末动手题里给 litmus 加的那一档，我也预跑过，读数都写进正文，多线程读数都是单次运行的读数。涉及标准行为的断言，我按 eel.is 上的现行工作草案与 cppreference 核对，条款名都带着，段号随草案版本会挪。

## 从顺序一致性出发

六档里最贵也最好推理的是默认档，咱们从它出发。seq_cst 让读带上 acquire、写带上 release、读改写两头都占，在这三样之上还叠了一条：全部 seq_cst 操作排成一条全体的次序，所有线程看到的是同一条（[atomics.order] 第 4 段）。白话就是，全局的 seq_cst 动作排在一条线上，谁在谁前面，人人看法一致。

全序的用处，咱们拿 5.1 留下的一个最短例子看：线程一做 `x.store(1)` 接着 `y.store(2)`，线程二读 y 再读 x。把读放松到 relaxed，线程二可能读到 y 是 2、x 是 0，两个变量各自完整，合起来却对不上。换成 seq_cst，挡住它的是总序。5.1 只立了定性，5.2 的 litmus 把它量了出来，一千万轮零命中。

草案在注里补了一句提醒，我原样转述（[atomics.order] 第 7 段的注）：seq_cst 只对没有 data race、并且全部原子操作都用 seq_cst 的程序保证顺序一致，掺进任何更弱的用法，保证就作废，除非格外小心。我把它读成这样：默认档推理省力，是因为它替您把所有原子变量的次序一起管了。您一降档，管住的那部分就得自己接管。5.2 讲强发生时留给本篇的那句，门槛就设在桥两端的 seq_cst 上，看过总序再看下一节的配对桥，门槛为什么立在那儿就清楚了。

什么时候留在默认档？我的判据有两条，占一条就留着。您手里的几个原子变量互相咬合、谁也离不开谁的次序，这时候 seq_cst 是标准里唯一给全序的档。或者您拿着代码数不出桥、说不清哪一对读写要接起来，留在默认档是给自己留退路，保住对，再谈省。

## release 与 acquire：配对的地方搭桥

中间的档位，咱们从 release 与 acquire 讲起。release 用在写上：它前面的读写，不许越过它挪到后面。acquire 用在读上：它后面的读写，不许提到它前面。读的一侧拿到 release 写下的值时，两边 synchronizes-with，桥从配对处接通（[atomics.order] 第 2 段），写端在 release 之前的全部操作，对读端在 acquire 之后的操作可见。

这对词眼您已经见过很多回。5.2 的 litmus 架子，放行与应答用的就是它们，当时我说架子借了原子操作搭桥，表里最后一行就是它们，为什么借还欠着：握手只需要两个线程之间的动作对接上，全局总序在架子上是白花的钱。配对处搭桥的档正好够用，这就是 acquire 与 release 的位置，只在需要桥的地方付桥的钱。

光说不够，咱们量一把。我把 5.2 的 litmus 原架搬来，动作对单独装进一个函数，内存序做成编译期三选一，relaxed 一档，release 配 acquire 一档，seq_cst 一档。

<OnlineCompilerDemo
  title="litmus 三档：relaxed、release 配 acquire、seq_cst"
  source-path="code/examples/vol5/chapter05/13_litmus_acqrel.cpp"
  description="内存序编译期三选一，默认 -DORDER_MODE=0 是 relaxed 档，命中数万次；换成 1 或 2 重编（命令与读数见下方）。"
  run-options="-O2 -std=c++20 -pthread -DORDER_MODE=0"
  allow-run
/>

读数摆在下面，咱们三档各跑了两遍，都是单次运行的读数：

```text
relaxed: 89258 of 10000000 rounds saw r1=0 and r2=0
release/acquire: 60782 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0

relaxed: 63698 of 10000000 rounds saw r1=0 and r2=0
release/acquire: 70389 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0
```

咱们把读数并排一摆，中间档与全松档同量级，每一千万轮命中六到九万次，全序档照旧零次。机器码把原因摆得很直，三档的 probe 函数并排看（objdump 输出，地址列截掉）：

```text
relaxed:         movl $0x1,(%rdi)     ← 写
                 mov  (%rsi),%eax     ← 读

release/acquire: movl $0x1,(%rdi)     ← 写，与 relaxed 一条不差
                 mov  (%rsi),%eax     ← 读

seq_cst:         mov  $0x1,%eax
                 xchg %eax,(%rdi)     ← 写换成 xchg，当场生效
                 mov  (%rsi),%eax     ← 读
```

咱们看指令。release 与 acquire 在 x86 上编出来的与 relaxed 完全相同，编译器只多拦了一类编译期的重排。litmus 测的场面是 x 与 y 两个变量互相咬合，两边同时读到旧值需要的正是全局次序，acquire 与 release 不设全局次序，读数自然落在全松档的量级。您要零命中，就得付 `xchg` 的钱。

5.1 的欠单咱们也在这里清。`atomic_flag` 的 `test_and_set` 带 acquire、`clear` 带 release，2.1 的手工自旋锁用的正是它：拿锁的人，锁住之后的读写不许提到 `test_and_set` 前面。放锁的人，临界区里的写压在 `clear` 前面。桥的形状与 5.2 数过的 `std::mutex` 一模一样，只是零件从锁换成原子标志。2.1 与 5.1 代码里的几处 acquire 与 release，加上 5.2 同步关系表里的最后一行，判法咱们已经熟了：桥的两端是不是它们，读的一侧拿到写的一侧的值没有。

还有一个细节值得立住。读的一侧拿到的不一定是那次 release 写本身，排它后面、任何人做的读改写也算数，标准管它叫释放序列（release sequence，[atomics.order] 第 2 段原文写的就是“从 A 头部的释放序列里取值”）。释放序列的概念，咱们就在本节立住，往后 cookbook 选读区的《无锁入门》会反复用到它，那里还没成文。

## acq_rel：读改写的两头

咱们把读改写交代清（read-modify-write，一次动作里读旧值、算新值、写回去），它同时有一读一写，两头都能接序。acq_rel 让它两头都占：作为读，它是 acquire，接住上游的桥。作为写，它是 release，开下游的桥。cppreference 的常量表把两句话并排写着，标准原文在 [atomics.order] 第 1 段，acquire 与 release 的清单里都点着它。

判据还是数桥，只是要数两头。您写下一个 `fetch_sub` 的时候问两件事：它的读在等谁的发布，它的写要发布给谁。两头都接链，用 acq_rel。只接一头，挑 acquire 或者 release。两头都不接，relaxed。引用计数的减法是两头都接的样板，下面模式节展开。

成本给个底：x86 上读改写不管什么内存序都编成同一条 `lock xadd`，指令对照在后面的表里。差距要到弱内存模型的机器上才显出来，那边的读数本篇没法给，我不装作测过。

## relaxed：只保原子性的场面

relaxed 的承诺清单最短，值钱的也正在这里，咱们一条一条看。头一件，动作完整，别的线程看不见半成品。再一件，同一个变量的修改，全体线程按同一条次序看。标准管它叫修改次序，四条一致性要求对全部原子操作生效，relaxed 也在受益的名单里。它不给的是桥，一节都不搭。

咱们拿计数器量一遍它的底：

<OnlineCompilerDemo
  title="relaxed 计数器：只保原子性也分毫不差"
  source-path="code/examples/vol5/chapter05/14_counter_relaxed.cpp"
  description="四条线程各加 100 万次，relaxed 读数 4000000；数值本身就是目的，旁边没有货要过桥，这就是它的用场。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
4 条线程各加 100 万次，relaxed 读数 4000000
```

四条线程各加一百万次，分毫不差，relaxed 的底咱们量完了。它适合的场面也清楚了：数值本身是目的，它旁边没有别人等着看的货。统计计数器、只报进度的任务数、引用计数的增，都落在这里。cppreference 在 relaxed 的小节里举过例子，说典型用法是递增计数器，比如 `std::shared_ptr` 的引用计数，同页紧接着提醒，减引用要 acquire-release 来配合析构，待遇确实不一样，判据在下面讲引用计数的时候数。

1.5 讲停止标志时提过的 relaxed，5.2 数出桥一节不搭，咱们把边界在这里立清楚：标志本身用 relaxed 不会碎，动作照样完整。碎的是它旁边该一起可见的数据，看到停止的人往往还要读配置，配置不在桥上，看到的就可能是旧值。修法是加桥，咱们在下面停止标志一节给，本篇的 demo 就按修法写，relaxed 的对照组留给篇末动手题。

## consume 的现状：被弃用的依赖序（扩展）

本节带（扩展）标记，跳过它不影响主线。您只要带走一件事：别写 `memory_order_consume`。

consume 当初的算盘比 acquire 精细，咱们把它摆开看。acquire 是全量订阅，读出来之后的全部读写不许提前。consume 只想约束依赖这次读出的值的那些操作。指针发布是它设想的主场：读者拿到指针，顺着指针访问的数据受排序保护，与指针无关的访问不受约束，理论上一分冤枉钱都不花。

现实没有配合，咱们直说。要兑现依赖序，编译器得沿整个函数追踪值的流向，变量一换名、值一进寄存器，依赖链就断在半路，链断了排序就得放大，不然就出错。没有主流编译器实现它的精确语义，实际都按 acquire 的强度来跑 consume 的名字。标准委员会的处置提案编号 P3475R2，标题里的动词很直白，defang and deprecate，把语义拔掉，再标成废弃。

现状我按日子记录，一个来源一句。cppreference 的条目表我 2026-11-16 抓过，它给 `memory_order_consume` 标了 deprecated in C++26。配套的 `kill_dependency` 同批废弃。“携带依赖”与“依赖排序在前”两个正式定义，同一个表里都标了 until C++26。eel.is 的现行工作草案我也抓了，[atomics.order] 的枚举只剩五个值，relaxed、acquire、release、acq_rel、seq_cst，consume 已经不在里面。libstdc++ 在 2026 年 5 月合入了对应的弃用补丁。往后哪个版本彻底删掉名字，我还没查到定论，您读到的时候值得再核一眼。

判据一句话：您想表达“读到指针再用它”，acquire 就是现在的写法，语义只强不弱，在所有编译器上行为一致。代码库里要是还留着 consume，换成 acquire。

## 常见模式：判据的练习场

停止标志与引用计数，您在前面单元都碰过它们的成品，双重检查锁定是唯一的新面孔，本篇把它带进门。判据三问挨个上：桥的两端是谁，桥上过什么货，操作站在链条的哪一段。

### 停止标志：release 置、acquire 读

1.5 讲协作式停止之前，轮询标志是人人写过的版本。工作线程反复看标志，主线程置标志，多数时候标志旁边还有一起更新的配置。5.2 数出 relaxed 版桥一节不搭，咱们把修法写成 demo，relaxed 的对照留给篇末动手题：

<OnlineCompilerDemo
  title="停止标志：release 置、acquire 读，配置跟着过桥"
  source-path="code/examples/vol5/chapter05/15_stop_flag.cpp"
  description="main 先改 batch_size 再用 release 置标志，worker 用 acquire 看到停止后读配置，读到的是 64 不是旧值 8。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
worker 看到停止后读到的 batch_size=64
```

桥咱们一节节数。batch_size 的写 sequenced-before stop_flag 的 release 写，这是程序顺序。worker 的 acquire 读拿到 true，synchronizes-with 接通。配置的读排在 acquire 之后。三节连成一条，64 进来，8 没有再出现。

什么时候 relaxed 真的够？标志旁边没有任何共享数据的时候。停止信号本身就是全部内容，看到即执行，没有货要跟着过桥，relaxed 的动作完整就够了。判断您自己来：标志的读之后，还要读谁？一个名字都数不出来，relaxed 放心用。数得出来，就给写端 release、读端 acquire。在本机上，acquire 的读与 relaxed 的读编成同一条 mov，加桥不添指令。换到弱内存模型的机器上，acquire 要付屏障的钱，那是买正确性的钱。

### 引用计数：增 relaxed、减 acq_rel

引用计数是 relaxed 与 acq_rel 同框的样板。完整的侵入式指针，旧目录 ch03 的素材文《原子操作模式》有全本，这里咱们只留两个成员函数，内存序才是主角：

<OnlineCompilerDemo
  title="引用计数：增用 relaxed，减用 acq_rel"
  source-path="code/examples/vol5/chapter05/16_refcount_core.cpp"
  description="正文那两个成员函数原样在 Counted 里，main 补成可运行：refs 从 1 加到 3，再逐个放手，减到零的那一次 release_ref 返回 true。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

判据咱们两头数。减法那一头：减到零的人要销毁对象，销毁要看见别的线程对对象的全部使用，所以减法的读得是 acquire，接住上游。同时每个使用过对象的人，自己的访问得压在减法之前，所以减法的写得是 release，向下游发布。两头都接链，acq_rel。加法那一头：增引用的人只关心自己拿到引用这件事成立，动作完整就够，relaxed。增与减的待遇不一样，这句概括是咱们自己下的，上面两个成员函数就是它的落点。

### 双重检查锁定：C++11 起才谈得上正确

假设您手上有一个初始化很贵的对象，配置要读文件，连接要握手，多数调用其实只想拿现成的。每次访问都加锁当然对，可锁的开销全摊在快路径上，九成九的调用都在替百分之一的初始化买单。检查、加锁、再检查，把锁往后挪，这就是双重检查锁定（double-checked locking pattern，简称 DCLP）的形状。它在 C++11 之前没法正确实现，Scott Meyers 与 Andrei Alexandrescu 2004 年的分析文章把旧标准的难处写全了，C++11 的内存模型给它补上了地基。咱们看能用的写法：

<OnlineCompilerDemo
  title="双重检查锁定：外层 acquire，锁内 relaxed，发布 release"
  source-path="code/examples/vol5/chapter05/17_dclp.cpp"
  description="八条线程各取 10 万次，指针一致、字段齐全；漏写发布后带回指针那一行的翻车现场见下方 AddressSanitizer 的回话。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
8 条线程各取 10 万次：指针一致=1，id=1 value=42
```

读数我跑了两遍，都一样。内存序的分工咱们摆一遍：外层检查用 acquire，读到非空就要保证构造完成对它可见，不然后面读字段就是读构造一半的对象。锁内复查用 relaxed，拿锁的动作已经替它搭好桥（5.2 数过的锁的同步关系），relaxed 只需要一个完整的读。发布用 release，构造的全部写入压在它之前。三问各就各位。

读数来得不容易，我把翻车的现场留给您。翻车那一下，我盯着段错误愣了半天。第一版 demo 少写一行，锁内构造完、发布完，忘了把指针带回局部变量，抢到锁的线程拿着 nullptr 返回，程序当场就崩。AddressSanitizer 的报告两行就把位置点了出来：

```text
==43==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
    #0 ... in operator() /tmp/concurrency-u5/dclp.cpp:33
```

空指针解引用，33 行正是线程函数里取字段的地方。机器帮我抓住了，补上 `p = raw_instance` 一行，读数就齐。

我把它讲出来，是因为它正好量出了两种错误的边界。指针忘带回，本机抓得住，读数当场爆炸。内存序的错本机抓不住：您把实现里的 acquire 与 release 全换成 relaxed，x86 上大概率照旧全对，TSO 替您扛着写与读的次序。正确性的依据不是读数，是标准语义：release 发布让构造完成对 acquire 读可见，换掉桥，构造与使用之间没有 happens-before，按 [intro.races] 第 17 段就是 data race，未定义行为。弱内存模型的机器上 relaxed 版才真会读到构造一半的对象。那边的机器我没测过，本篇读数不覆盖，这句话我自己记着。

Meyers 单例在旁边等着收尾。块级 static 的初始化，标准替咱们管了并发（[stmt.dcl] 第 3 段：并发进入声明时，并发执行要等待初始化完成）：

<OnlineCompilerDemo
  title="块级 static：并发初始化由标准担保"
  source-path="code/examples/vol5/chapter05/18_meyers_singleton.cpp"
  description="config() 两次调用取到同一个对象，字段齐全；能写成块级 static 就不必手搓 DCLP，Config 沿用 dclp.cpp 的定义。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

您能写成块级 static 的，就别手搓 DCLP。DCLP 的价值在形状本身，检查、加锁、再检查，延迟初始化任何资源都用得上，单例只是它最有名的雇主。

## 发布—订阅标志：relaxed 计数配 acquire-release（扩展）

本节带（扩展）标记，跳过它不影响主线。大伙想看进度，可以。可见性这一层，得由完成标志来扛，您 acquire 到 true，货才算真的到手。分工摆出来，就是 relaxed 与 acquire-release 同框的另一处：

<OnlineCompilerDemo
  title="发布订阅：relaxed 计数只报进度，可见性归标志"
  source-path="code/examples/vol5/chapter05/19_pubsub.cpp"
  description="done 的 release 与 acquire 把三份普通货物送过桥，消费者看到计数 3、货物 100、101、102；桥接通后读进度连 relaxed 都够。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
消费者看到的计数=3，货物=100,101,102
```

分工的判据咱们过一遍。finished 的三次加法用 relaxed，读它的人少看见一两次更新没有损害，数字只是进度。done 的两端用 release 与 acquire，三份货物是普通变量，没有桥它们到不了对面。桥接通之后还有一个白送的推论，咱们单独拎出来。consumer 再读 finished，连 acquire 都不用，relaxed 就够。依据是桥的形状。写端 release 之前的全部写，对读端 acquire 之后的读全部可见。cppreference 讲 release 与 acquire 的小节里，原话就是这么写的。

写岔的形状我也摆在旁边，您对照着认：只用一个 relaxed 标志报完成。本机跑，大概率照样全对，TSO 帮忙。标准语义里没有桥，货物就可能在对面开工时还没齐。这类错偏要到弱内存模型的机器上才肯现形，又是那句，本篇读数不覆盖。

## fence 与 volatile 的边界

到这里，排序都绑在具体的原子操作上，release 长在 store 身上，acquire 长在 load 身上。fence（屏障，让编译器与 CPU 在一个位置上停住、前后的内存操作不许跨越）把排序从操作上挪下来，单独放在代码的一个位置。`std::atomic_thread_fence` 接一个内存序参数，C++11 起。本节是选讲内容，咱们只取判据要用的部分，跳过它不影响主线。承的出处交代清楚，旧目录 ch03 的素材文《fence 与编译器屏障》里 fence-atomic 同步与 volatile 辨析两块，剩下的深度归选读。

咱们只讲两端都挂 fence 的接法。cppreference 在 `atomic_thread_fence` 的页上列了三种接法，fence 对原子、原子对 fence、fence 对 fence，demo 用的是 fence 对 fence。条件咱们数全：写端把 release fence 排在一个原子写之前，原子写用什么内存序都行。读端用 relaxed 读拿到值，再把 acquire fence 排在读的后面。两端凑齐，桥就接通，写端 fence 之前的写对读端 fence 之后的读可见。

<OnlineCompilerDemo
  title="fence 对 fence：排序约束从原子操作挪到 fence 上"
  source-path="code/examples/vol5/chapter05/20_fence_demo.cpp"
  description="发布端挂 release fence、订阅端挂 acquire fence，flag 的读写都降成 relaxed，消费者照样读到 payload=42。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
消费者读到的 payload=42
```

指令的形状与 release 配 acquire 同构，排序的钱从 store 与 load 两头挪到了两条 fence 上，flag 的读写都降成了 relaxed。fence 本身的指令，咱们也抓了一份，本机 GCC 16 编出来：

```text
fence_release:  ret                      ← 函数体只剩返回，零指令
fence_acquire:  ret                      ← 同上
fence_seqcst:   lock orq $0x0,(%rsp)     ← 一条带 LOCK 前缀的指令
```

cppreference 在 Notes 里写明，x86 上 `atomic_thread_fence` 除 seq_cst 外一条 CPU 指令都不发，只影响编译期的代码移动，与咱们抓到的指令对得上。教科书画 seq_cst 档，常画成一条 mfence，本机 GCC 16 实际编出来的却是一条带 LOCK 前缀的 or。

本机上看不出差别，我如实交代。fence 值钱的地方不在省指令，在把付钱的时间点推到运行时确认之后。cppreference 的页上有一个信箱扫描的例子，用的是原子对 fence 的另一种接法，32 个信箱逐个 relaxed 读，只有读到属于自己的格子，才插 acquire fence，没命中的格子不花同步的钱。带内存序的原子操作做不到，acquire 长在读上，读了一次钱就付了。

判据我这么摆：默认写带内存序的原子操作，显式，桥好数。fence 留给同步位置确实要与操作分开的场面，用它的代价是数桥变绕，fence 与操作谁在前谁在后、读到底拿到哪次写，三个条件少一条就断，走查的时候容易漏。

volatile 的边界 5.1 判过一半，咱们在这里收拢。volatile 承诺的只有一件事：每次访问真的发生，编译器不合并不删。它的本职在硬件寄存器映射与信号处理。它不保证动作完整，不安排次序，不搭桥，跨线程的标志与计数用它，按 [intro.races] 第 17 段就是 data race。MSVC 历史上给 volatile 添了 acquire 与 release 语义，非标准扩展，代码要跨编译器就别依赖。跨线程的场面您现在有整套工具：动作完整找 atomic，次序找内存序，volatile 请回它的寄存器与信号处理。

## 挑档的判据，摆在一起

标题的问题该正面回答了。判据三问，每问一个完整句，您对着自己的代码逐个过：

1. 您要检查的读写之间，桥的两端连得上吗？写端落在 release（或者放锁），读端落在 acquire（或者拿锁），读端拿到写端写下的值，三节连成一条，acquire 与 release 就够，不用更贵的档。
2. 桥要护的是一次交接，还是几个原子变量互相咬合的次序？一次交接归 acquire 与 release，互相咬合归 seq_cst。5.2 的 litmus 与本篇中间档的读数，分界线就画在这里。
3. 这个操作站在链条的哪一段？不接链只递增，relaxed。读改写两头都接链，acq_rel。一时数不清，回到 seq_cst。

机器成本咱们也量了，本机 x86-64、GCC 16.2.1、-O2 的 objdump 读数摆成一张表：

| 动作 | relaxed | release / acquire | seq_cst |
|---|---|---|---|
| store | mov | mov | xchg |
| load | mov | mov | mov |
| 读改写 | lock xadd | lock xadd | lock xadd |
| fence | 无指令 | 无指令 | lock orq |

读改写一列含 acq_rel。弱内存模型的机器上，release、acquire 与 seq_cst 要付屏障的钱，本篇没有那边的机器，我给不出读数。

成本表底下还有一层不对称，咱们摆在明处。数不出桥的时候留在 seq_cst，多花几条指令。数错了桥，代价就是正确性，读数再干净也救不回来。判据偏向保守，是代价不对称逼的。

## 轮到您动手的时候

动笔改代码之前，我把本篇攒下的问题列成一列，您对着自己的并发代码过一遍：

1. 您要检查的读写之间，数一遍桥，把两端写在注释里。写端 release、读端 acquire、读到写下的值，三节缺一节就要补。
2. 桥要护的是一次交接，还是几个原子变量的咬合？前者 acquire 与 release，后者留在 seq_cst。
3. 您的读改写两头各接在哪条链上？两头都要才配 acq_rel。
4. 您的计数器旁边还有没有别人要读的数据？没有，relaxed 才放得下心。
5. 代码库里还有 `memory_order_consume` 吗？换成 acquire。
6. 单例能写成块级 static 吗？能，就不必手搓 DCLP。

动手题两个。题目一是给 litmus 加一档，写端保持 release，读端降到 relaxed。我预跑过两遍，命中 109083 与 108575，量级与中间档几乎一样。您跑完想想为什么读数几乎不动：acquire 端缺席，桥本来就没搭起来，机器也没有多付钱。读数不变不等于桥还在。题目二把 stop_flag 的内存序全部换成 relaxed，跑一遍，读数照样是 64，然后数桥，数完您自己判断该不该合进去。

## 下一步

单元五到这里收束，咱们把三篇连成一条线：5.1 画原子操作的能力边界，5.2 给出桥的语言，本篇把挑档的判据落到每一对读写上。您现在回头看 [《从错误程序认识竞争》](../chapter02-sharing-data/01-from-wrong-programs)的 atomic_flag、5.1 的 `atomic_flag` 自旋锁演示、5.2 的 litmus 与同步关系表，代码里的 acquire 与 release，都能按判据数一遍了。

下一步是第六单元，怎么判断并发设计是否有效。判据给了，剩的问题是您怎么验证自己数对了桥：6.1《怎样寻找并发错误》讲重复测试与 ThreadSanitizer，本篇反复出现的那句“本机看不出”，要在那里拿到工具。6.2《怎样衡量并发带来的收益》立串行基线，6.3《怎样调整设计》讲从测量回到设计。三节都还没成篇。翻页之前，把本篇动手题里的两档跑一遍，读数您自己核。

## 参考资源

本篇的实验收在 code/examples/vol5/chapter05/，可在本页直接运行（GCC 16.2.1 20260810，`g++ -std=c++20 -O2 -pthread`，WSL2 内核 6.18.33.2，16 逻辑核，2026-11-16，抽成独立文件时复验过）。litmus 三档我各跑了两遍，更早一轮预跑的同为三档（relaxed、release 配 acquire、seq_cst），读数量级相同，seq_cst 全部零命中。篇末动手题的那一档，我也预跑了两遍。counter_relaxed 的 4000000、fence_demo 的 42、stop_flag 的 64、pubsub 的 3 与 100、101、102，每次运行读数一致。DCLP 的两遍读数一致，翻车现场由 AddressSanitizer 定位。指令对照来自 objdump，地址列截掉。litmus 的架子与 5.2 相同，放行哨兵、应答只增不减、重置压在放行之前、绑核 0 与 8 号逻辑核。

涉及标准行为的断言，我按 eel.is 的现行工作草案与 cppreference 核对，草案是 2026-11-16 抓的。[atomics.order] 核了枚举、release 与 acquire 的同步关系、释放序列、seq_cst 总序与掺弱作废的注，现行草案的枚举里没有 consume。[stmt.dcl] 核了块级 static 并发初始化的等待条款。[intro.races] 的 data race 判定 5.2 已核，本篇复用。fence 的三种同步模式、x86 不发指令的 Notes 与信箱扫描的例子，按 cppreference 的 `atomic_thread_fence` 页核对。consume 的弃用状态核对过两处。一处是 cppreference 条目表的 C++26 标记，另一处是 libstdc++ 的合入记录。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，取的是单元排布与例子设计的思路。DCLP 的写法参考了两份材料，Jeff Preshing 的文章是一份，Meyers 与 Alexandrescu 2004 年的分析是另一份。借的是结构与讲法，例子和文字都是我自己写的。

- [std::memory_order（cppreference）][cppref-mo]，六档的常量表、release-acquire 小节、consume 的 C++26 弃用标记、relaxed 的典型用法。
- [现行标准草案：Order and consistency（atomics.order）][draft-atomics-order]，五档枚举、同步关系、释放序列、seq_cst 总序与掺弱作废的注。
- [现行标准草案：Declaration statement（stmt.dcl）][draft-stmt-dcl]，块级 static 并发初始化的等待条款。
- [现行标准草案：Data races（intro.races）][draft-intro-races]，data race 判定，5.2 已核，本篇复用。
- [std::atomic_thread_fence（cppreference）][cppref-fence]，三种同步模式、x86 不发指令的 Notes、信箱扫描的例子。
- [libstdc++ 弃用 memory_order_consume 的补丁（GCC 邮件列表）][gcc-consume]，2026 年 5 月合入。
- [Double-Checked Locking is Fixed In C++11（Jeff Preshing）][preshing-dclp]，DCLP 的写法参考。
- [C++ and the Perils of Double-Checked Locking（Scott Meyers, Andrei Alexandrescu）][meyers-dclp]，旧标准下 DCLP 为什么失败的分析。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-mo]: https://en.cppreference.com/w/cpp/atomic/memory_order
[draft-atomics-order]: https://eel.is/c++draft/atomics.order
[draft-stmt-dcl]: https://eel.is/c++draft/stmt.dcl
[draft-intro-races]: https://eel.is/c++draft/intro.races
[cppref-fence]: https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence
[gcc-consume]: https://gcc.gnu.org/pipermail/gcc-patches/2026-May/718127.html
[preshing-dclp]: https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11
[meyers-dclp]: https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
