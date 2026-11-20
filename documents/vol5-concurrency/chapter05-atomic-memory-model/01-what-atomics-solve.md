---
title: "原子操作解决到哪一步"
description: "std::atomic 认哪些类型、配哪些操作：load/store/fetch_add 的操作面，CAS 的失败与重试、weak 与 strong 的分工，单次动作与多步业务的边界，浮点与自定义类型各卡在哪一关。"
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - atomic
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20, 26]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 原子操作解决到哪一步

第五单元从这里开始。第四单元收在 promise 与 future 上，结果的传递交给了库。几个线程同时碰同一份数据时，最底层的那套工具咱们一直在用，却一直绕着它的细节走：[《从错误程序认识竞争》](../chapter02-sharing-data/01-from-wrong-programs)给咱们留了两样，丢数的计数器是 `std::atomic<int>`，饥饿演示里的手工锁是 `std::atomic_flag`，[《一次操作的边界在哪里》](../chapter02-sharing-data/03-check-then-act)的观察循环靠 `atomic<bool>` 收尾，[《按协作需求选择工具》](../chapter03-wait-and-coordinate/04-choosing-tools)量峰值的那几行 `compare_exchange_weak` 也只交代了一句，细节归第五单元。咱们现在就站在这儿。

本篇是单元开篇，我给自己划的活儿只有一件：把原子操作能担保的与不能担保的画成一条边界。类型与操作占头两节，CAS 与 `atomic_flag` 各占一节，单次动作与多步业务的分界是一节新写的，常见的几种误认承旧目录 ch03 的素材文《atomic 操作》收拢，末尾的浮点与自定义类型带（扩展）标记，跳过它不影响主线。内存序的词眼在正文里露了几次面，默认的 `memory_order_seq_cst`，`atomic_flag` 演示里的 acquire 与 release，误区节里的 relaxed，咱们都只认名字不展开，为什么读写次序还需要另一套理论，归 5.2《[一个线程怎样观察另一个线程的修改](02-happens-before)》，已经写好。

文中的程序收在 code/examples/vol5/chapter05/，可在本页直接运行（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2 内核 6.18.33.2，16 逻辑核，2026-11-16）。多数 demo 是编排好的确定场面，谁读、谁睡、谁动手，次序写死在代码里，您重跑读数也一致。涉及标准行为的断言，我按 eel.is 上的现行草案与 cppreference 核对过，链接收在参考资源。

## std::atomic 认哪些类型，配哪些操作

`std::atomic` 是一个类模板，住在 `<atomic>` 里，C++11 起。咱们把它当一套类型加一套操作来认，类型决定您拿得到哪些操作，操作决定一次动作的形状。分档看最省事。

| 类型档 | 配到的操作 | 版本 |
|---|---|---|
| 整型：每种字符类型、每个标准整数类型，含 `<cstdint>` 的 typedef | load、store、exchange、CAS，fetch_add、fetch_sub、fetch_and、fetch_or、fetch_xor，`++`、`--`、`+=` 等运算符 | C++11 起 |
| 浮点：`float`、`double`、`long double` | load、store、exchange、CAS，fetch_add、fetch_sub | C++20 起 |
| 指针 `atomic<T*>` | load、store、exchange、CAS，fetch_add、fetch_sub（按元素个数偏移） | C++11 起 |
| 可平凡复制的自定义类型（主模板） | load、store、exchange、CAS，wait、notify | C++11 起，wait 与 notify 归 C++20 |
| `atomic<std::shared_ptr<T>>`、`atomic<std::weak_ptr<T>>` | load、store、exchange、CAS，wait、notify | C++20 起 |

两处容易读岔的地方，咱们交代清楚。`atomic<bool>` 走主模板，标准在注里写明它不归整型特化管（[atomics.types.int] 的注 1），所以 bool 身上没有加法，`fetch_add` 不存在。标准还在往前长：C++26 给整型与指针添了 `fetch_max` 与 `fetch_min`，本卷停在 C++20，您知道有它们就行。

操作面上，`fetch_` 系列把读、算、写当成一次动作做，交回的是运算之前的旧值。咱们跑一段最小的：

<OnlineCompilerDemo
  title="load、exchange、fetch_add 与运算符各交回什么"
  source-path="code/examples/vol5/chapter05/01_demo_ops.cpp"
  description="在一个 atomic<int> 上依次跑 store、exchange、fetch_add、前后置 ++ 与 +=，每个动作打印交回值与当前值。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
store(10) 之后 load      : 10
exchange(3) 交回旧值     : 10，现在 3
fetch_add(5) 交回旧值    : 3，现在 8
counter++ 交回旧值       : 8，现在 9
++counter 交回新值       : 10，现在 10
+= 4 之后                : 14
```

交回旧值是刻意的安排：一次动作同时完成“读当前状态”与“改状态”，做计数、做配额都顺手。运算符形式另有一副脾气，`++` 前置交回加完的新值，后置交回旧值，跟普通 `int` 的规则一致。`+=` 走的是 fetch_add 的运算符外衣。您要拿返回值参与别的计算，就写显式的 `fetch_add`，意图看得清。

## 原子不等于无锁：is_lock_free 与 is_always_lock_free

到这里我一直说“一次动作”，还没交代完整是谁给的。多数人脑子里原子就等于 CPU 指令一条搞定，事实是，类型够小、平台有指令，编译器直接给真指令。类型宽到平台没有对应指令（比如 16 个字节），库退回去用一把内部锁把动作围起来，动作照样完整，代价是它内部真有锁。`std::atomic` 保证的是动作完整，不保证没有锁。

标准库给了两个查询口。`is_always_lock_free` 是编译期常量（C++17 起），true 表示该类型的原子操作在这个平台上永远无锁。`is_lock_free()` 是运行时查询。咱们在本机 x86-64 上跑：

<OnlineCompilerDemo
  title="is_lock_free 与 is_always_lock_free：宽类型不是无锁"
  source-path="code/examples/vol5/chapter05/02_demo_lockfree.cpp"
  description="atomic<int> 与 atomic<long long> 两个查询都是 1，16 字节的 atomic<Wide> 都是 0，它的动作落在内部锁上。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
atomic<int>        is_always_lock_free=1  is_lock_free=1
atomic<long long>  is_always_lock_free=1  is_lock_free=1
atomic<Wide>(16B)  is_always_lock_free=0  is_lock_free=0
```

`atomic<int>` 与 `atomic<long long>` 无锁，16 字节的 `Wide` 不是，两个查询都翻成 false，它的操作落在内部锁上。您写的代码要押注某段路径绝不能有锁，就把 `static_assert(std::atomic<Wide>::is_always_lock_free)` 压上去，平台不满足直接编不过，比运行时分支干净。换编译选项或者换平台，读数会变，所以拿不准就问它，别背答案。

## CAS：compare_exchange 的失败与重试

fetch 系列只覆盖加减与位运算，形状不合的读改写，比如“只比最大值”“只在没人动过时才换”，得靠 compare-and-swap，简称 CAS，咱们把它请出来。`compare_exchange_weak` 与 `compare_exchange_strong` 的效果写在 [atomics.types.operations]/23，您把 expected 的值取出来，跟原子变量的值表示比一比，相等就写入 desired、返回 true，不相等就把原子变量当时的值写回 expected、返回 false。

CAS 失败的时候，有个容易漏的细节，expected 会被覆盖。我拿 `std::atomic<int>` 摆了两个场面。一个用 expected=99 去 CAS 值为 10 的变量，返回 false，expected 被覆盖成 10。另一个是加 5 的 CAS 循环，半路埋了个人改值，失败一轮、重算一轮、成功：

<OnlineCompilerDemo
  title="CAS 失败时 expected 被覆盖，重算重试"
  source-path="code/examples/vol5/chapter05/03_demo_cas.cpp"
  description="值对不上时 CAS 返回 false 并把当前值写回 expected；往 quota 加 5 的循环半路被人改值，失败一轮后拿新值重算成功。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
expected=99 去 CAS：ok=0，v 还是 10，expected 被覆盖成 10
第 1 次尝试失败：值已经是 100（expected 也被覆盖成它），拿它重算
第 2 次尝试成功：100 + 5 -> 105
```

覆盖不是坏事，咱们把它当成 CAS 送回来的重算材料。循环重试正是把任意“读、算、写”压成一次读改写的通用办法，您读一份 expected，算出 desired，CAS 成功就完事，失败说明别人动了，expected 已经换成最新的，接着重算。第二步的加 5 走的就是它。

### weak 与 strong 差在哪

差别只有一项，weak 允许虚假失败。哪怕值相等，它也可以返回 false。标准在 [atomics.types.operations]/27 把原因写明，compare-and-exchange 在某些机器上靠 load-link/store-conditional 一对指令实现，SC 那半可能因为各种原因失败，weak 把这样的失败照实报出来，strong 则必须内部重试到成功或者值真的变了。cppreference 补的实际建议，咱们马上把它落成判断法。

落到判断法上，包在循环里的 CAS 用 weak，部分平台上它更快，虚假失败不过多转一圈。循环外一次性的判断用 strong，免得一次虚假失败把您带错分支。常见的 x86 走 CMPXCHG 指令，没有虚假失败一说，weak 与 strong 在上面跑起来几乎无差别，可代码是写给所有平台的，照判断法写不吃亏。

### CAS 只认值，不认历史：一次可复现的 ABA

CAS 比较的是值表示，值相等就放行。它不问的问题是，这个值在中间变过没有。变过去又变回来，CAS 照样成功，这就是 ABA。空口说没有分量，我把它编排成确定复现的场面，一个用槽位数组搭的栈，head 存槽位号，出栈的人读完 head、睡 150 毫秒再 CAS。睡的空当里 main 把两个节点都弹走，再把新值塞进腾出来的槽 1，head 指回 1。另一版把版本号打进 head，咱们看看下场差在哪：

<OnlineCompilerDemo
  title="CAS 只认值不认历史：可复现的 ABA 与版本号修法"
  source-path="code/examples/vol5/chapter05/04_demo_aba.cpp"
  description="前半场 head 被弄回旧槽位，出栈线程的 CAS 照样放行，300 不见；后半场版本号打进 head，CAS 当场拦下。演示用，槽位数组只靠时间窗编排，存在数据竞争。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
[naive  ] A 读完：head=1，下一个是槽 2，随后睡 150 ms
[naive  ] main: pop 收到 100
[naive  ] main: pop 收到 200
[naive  ] main: 把 300 放进槽 1，head 指回 1
[naive  ] A 的 CAS(head: 1 -> 2) 成功，它报告 pop 到了 300
[naive  ] main 清点：head=2，栈里只剩值 200。300 不见了，200 已经交过一回货
[version] A 读完：head=1（版本 0，槽 1），随后睡 150 ms
[version] main: pop 收到 100
[version] main: pop 收到 200
[version] main: 把 300 放进槽 1，head=769（版本 3，槽 1）
    A 第 1 次 CAS 失败：手里的 head=1，现在已经是 769
[version] A 最终 pop 到 300
[version] main 清点：head=1279，槽位是空标记，栈空了。300 交给 A，数目一致
```

naive 的六行是事故现场，咱们一起对一遍。A 手里的旧值是 1，CAS 只问“现在还是 1 吗”，main 恰好把 head 弄回了 1，CAS 放行，A 把 head 改成 2。后果跟着出来，A 报告 pop 到 300，可 main 明明刚把 300 压进栈。清点时栈里只剩 200，200 是 main 已经弹走交过货的节点。值对上了，事情全错了。

version 是修法，咱们给 head 升级，从 int 涨到 64 位，低 8 位装槽位号，高位装版本号，每次进出栈版本涨一。A 手里的 head 是 1（版本 0），main 三次进出之后 head 是 769（版本 3，槽 1），CAS 当场不放行，手里的 1 对不上现在的 769。A 重读、重算，把 300 正确地弹走，栈面干净。真正的无锁栈里这个套路叫带标签的指针，思路一样，把历史塞进比较的字节里。完整的无锁栈还要解决内存回收，归 cookbook 选读的无锁入门，还没成文。

## atomic_flag：标准保证无锁的那一个

`std::atomic<T>` 的操作是否无锁看平台，`std::atomic_flag` 是例外，cppreference 的原话是 it is guaranteed to be lock-free，标准保证它无锁。代价您也看得见，操作面极窄，`test_and_set` 把标志置真、交回旧值，`clear` 置假。C++20 之前连读一眼的操作都没有，`test()` 与 `wait()`、`notify_*` 都是 C++20 添的。

它的经典用场是一把手工自旋锁，[《从错误程序认识竞争》](../chapter02-sharing-data/01-from-wrong-programs)的饥饿演示里咱们已经见过它的形状（那边用的 `ATOMIC_FLAG_INIT` 写法），现在咱们把它单独跑一遍，两条线程各自加 100000 次：

<OnlineCompilerDemo
  title="atomic_flag 自旋锁：test_and_set 拿锁，clear 放锁"
  source-path="code/examples/vol5/chapter05/05_demo_flag.cpp"
  description="默认构造后 test() 读到 0，两条线程各加 100000 次，guarded 分毫不差地落在 200000。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
flag 默认构造后 test(): 0
两条线程各加 100000 次，结果 200000
```

200000 分毫不差。锁的用法、饥饿的毛病，2.1《从错误程序认识竞争》都量过了，咱们在这里补的是初始化的历史。C++11 时代，`atomic_flag` 想有确定的初值只有一条路，`std::atomic_flag f = ATOMIC_FLAG_INIT;`，别的写法落下的值未指定。C++20 起默认构造就是清除态，demo 里 `test()` 打出来的 0 就是这么来的，宏从此不必写，2.1 的旧写法照样能用。

`test_and_set` 与 `clear` 后面括号里的 acquire 与 release 是内存序的词眼，2.1 的样本也带着它们，咱们照着写就行，词眼归 5.3《选择内存序需要什么依据》专讲。

> 插曲也记一笔：C++20 一度把这个宏标成废弃，缺陷报告 LWG 3659 又把它撤回，理由是某些平台上的 C 还需要它。您在 C 与 C++ 之间搬代码时，见到它别当成错误。

自旋锁这边，拿不到锁的线程原地空转，临界区短到纳秒级它才划算，长了烧 CPU，咱们回 `std::mutex`。C++20 的 `wait` 与 `notify_*` 能把空转换成挂起，cookbook 的 atomic_wait 配方还没成文，到时候咱们接着说。

## 一次动作与一次业务，边界画在哪

前面的操作都是一次动作，fetch_add 是一次读改写，CAS 成功是一次读改写，失败是一次读。[《从错误程序认识竞争》](../chapter02-sharing-data/01-from-wrong-programs)留的现场还没收拾，`atomic<int>` 计数器跑 8 次没有一次到 200000，因为 `counter = counter.load() + 1` 是三步。原子担保每一步完整，不担保三步连成一步。2.3《一次操作的边界在哪里》讲 check-then-act 收尾的那句就是本节的题面，原子只管单次动作，复合操作要的是把动作连起来。连的办法有两种，咱们各跑一遍。

咱们把场面摆回 2.3 的货架，只剩一件货，两位买家。库存换成 `std::atomic<int>`，每一步都原子，检查是一次 load，扣减是一次 load 接一次 store。编排好的窗口里，两位买家都看到 1，都动手：

<OnlineCompilerDemo
  title="检查与动手分成两次原子操作，货照样卖超"
  source-path="code/examples/vol5/chapter05/06_demo_boundary.cpp"
  description="库存换成 atomic<int>，两位买家都看到 1 都动手，结余 -1；CAS 循环把判断和扣压抑进一次读改写，一件货只一个人拿走。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
[naive  ] buyer B 动手，结余 0
[naive  ] buyer A 动手，结余 -1
[cas    ] buyer B 动手，结余 0
[cas    ] buyer A 重试看见 0，放弃
```

结余 -1，一件货卖给两个人，跟 2.3 里那版加了锁却把临界区切开的货架死法一样。您把每一步都换成原子操作，救不了边界划错的病，检查与动手之间那道窗口，原子管不着。

修法甲藏在 demo 的后半场，咱们把业务动作压成一次读改写。判断加扣减写不成一条 fetch_add，加减没有条件，CAS 循环兜得住，读旧值，值为 0 就走人，不为零就拿 CAS 换成旧值减一，失败重读重来。buyer B 买走了一件，buyer A 的 CAS 失败，重读看见 0，放弃，结余 0，一件货一个人。

修法乙是压不成的时候，锁回场。哪些压不成？咱们数一数。不变量横跨两个对象的，2.3 的钱包，A 减 B 加要一起成立。条件能复合的，[《写一个有容量限制的任务队列》](../chapter03-wait-and-coordinate/02-bounded-queue)的队列，空、满、关闭三个条件咬在一起。还有动手前后要调别的代码、失败要回滚别处的场面。原子与锁，分工不同，动作能压成一次的归原子，压不成的归锁或者改设计。

3.4《按协作需求选择工具》量峰值的仪表，咱们现在能读懂了：

<OnlineCompilerDemo
  title="峰值仪表：只许涨不许跌的 CAS 循环"
  source-path="code/examples/vol5/chapter05/07_peak_gauge.cpp"
  description="正文那三行原样长在 task 里：inside 记当前同时在线的任务数，量到更大 now 的人把 peak 顶上去。跑完 inside 归 0，峰值 4。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

peak 只许涨不许跌，谁量到更大的 now，谁用 CAS 把它顶上去，输的人拿到最新的 peak，下一轮接着比。咱们数一遍这几行的动作，一次读改写加循环保完整，锁都不用请。

## 常见的几种误认

素材文收过一节误认，咱们把它重写成现在的判定版本。

### volatile 不是 atomic

`volatile` 拦的是编译器优化，保证每次读写真的落到内存，不保证动作完整，也不保证次序。`++volatile_counter` 仍是读、加、写三步，两条线程一起来照样丢数，而且按 2.1 的四件事判下来，volatile 变量上的冲突访问没有原子豁免，就是 data race，标准判未定义行为。volatile 的本职在硬件寄存器映射与信号处理，跟多线程无关，想让它兼职原子，兼职那半是您自己脑补的。

### std::string 造不进 atomic

编译探针只有一行，咱们拿 `std::atomic<std::string>` 造一个对象，GCC 16 的回话：

```text
In file included from probe_str.cpp:1:
/usr/include/c++/16/atomic: In instantiation of ‘struct std::atomic<std::__cxx11::basic_string<char> >’:
probe_str.cpp:3:26:   required from here
    3 | std::atomic<std::string> s;
      |                          ^
/usr/include/c++/16/atomic:215:21: error: static assertion failed: std::atomic requires a trivially copyable type
```

卡住它的是什么，咱们交代清楚：可平凡复制（trivially copyable），复制这个类型等于把字节原样搬一遍，`std::string` 管着堆内存，搬家不是搬字节。C++11 的文本把它写成一条要求，违反没有诊断。C++20 起升格成编译期检查，不满足直接编不过。可平凡复制的自定义类型能编过，操作面只有 load、store、exchange、CAS，扩展节细说。

> 咱们把比较方式交代清楚，CAS 按字节比，[atomics.types.operations] 注 4 的原话，是 memcmp 加 memcpy 的效果。它不调用您的 operator==，也不调用拷贝构造。自定义类型当原子用，相等就是字节相等。

### 默认构造拿到什么值

咱们实测一眼：

<OnlineCompilerDemo
  title="C++20 起默认构造拿到确定的零值"
  source-path="code/examples/vol5/chapter05/08_demo_init.cpp"
  description="atomic<int> 默认构造后 load 出 0，atomic_flag 默认构造后 test() 出 0，都是 C++20 才有的确定起点。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
atomic<int> 默认构造 load: 0
atomic_flag 默认构造 test: 0
```

两个都是确定的起点，这是 C++20 的行为。历史的走向容易读反，C++11 的文本里，默认构造连 T 对象都不建，读到什么没有定义。LWG 2334 把条款修正成“用 T() 值初始化”，libstdc++ 与 libc++ 到 C++20 模式才照改，cppreference 在构造函数页记了这段。所以老代码惯用带初值的构造 `std::atomic<int> c{0}`，这个习惯您接着用没有任何损失，两种写法在新标准下都得到确定的起点。

### 每个操作都原子，两个变量合起来仍没有次序

素材文里的例子值得再看一次，线程一做 `x.store(1)` 接 `y.store(2)`，线程二读 y 再读 x。两个变量各自读写完整，线程二却可能读到 y 是 2、x 是 0。默认的 seq_cst 下读不到这个场面，它要求所有 seq_cst 操作排成一条全序，x 的写必须落在 y 的读前面，把读放松到 relaxed 才可能。为什么放松、怎么判，归 5.2《一个线程怎样观察另一个线程的修改》与 5.3《选择内存序需要什么依据》，这里我只把边界立住，原子的完整是按单变量说的，两个变量之间谁在前谁在后，本篇的工具给不了答案。

## 浮点与自定义类型的原子边界（扩展）

本节带（扩展）标记，您跳过它不影响单元主线。给浮点计数器、给自定义结构上原子的朋友，边界就在下面两小节里。

### 浮点：C++20 起能原子加减，每步一次舍入

C++20 给 `atomic<float>`、`atomic<double>`、`atomic<long double>` 配了 fetch_add 与 fetch_sub，乘除没有。咱们把语义上的关键放在舍入上，每次 fetch_add 都按浮点加法算一遍，再舍入一次。demo 从 2 的 24 次方减 1 起步：

<OnlineCompilerDemo
  title="浮点原子加法：每一步都完整，合起来数字没动"
  source-path="code/examples/vol5/chapter05/09_demo_float.cpp"
  description="从 16777215 加 1 动了一步，再原子加 100 次原地不动；四条线程各加 100 万次的对照段里 float 卡住，long long 数满。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
16777215 + 1 -> 16777216（交回旧值 16777215），这一步动了
再原子加 100 次 1.0 -> 16777216，卡住了
4 条线程各加 100 万次后：float=16777216   long long=20777216
```

第一加真的动了，16777216 可以表示，结果落在它身上。后面的加全部原地，float 的尾数只有 24 位，16777217 装不下，就近舍回 16777216，每一步都完整，每一步都白加。四条线程各加 100 万次的对照段更直白，float 原地不动，long long 数到 20777216。cppreference 在 fetch_add 页补过 C++20 的两条注，咱们照录，结果超出可表示范围时，值未指定但没有未定义行为。浮点环境可能与调用线程的不同。要给浮点计数，咱们把最小单位换成整数（以厘记、以纳秒记），原子加整数，读的时候再换算，这是最省心的死板办法。

还有一个字节比较的边角，咱们拿它单独对一遍。CAS 按值表示比，在遵从 ISO/IEC 60559 的实现上，-0.0 与 +0.0 用 `==` 相等，按字节不等。同 payload 的 NaN 按字节相等，`==` 不等（[atomics.types.operations] 注 6）。拿 CAS 判浮点相等的朋友，判法得跟着字节走。

### 自定义类型：过了可平凡复制，只有四个操作

咱们拿 `Tick` 开刀，它长这样：

<OnlineCompilerDemo
  title="自定义类型过了可平凡复制，只剩四个操作"
  source-path="code/examples/vol5/chapter05/10_atomic_tick.cpp"
  description="Tick 过了可平凡复制这一关，atomic<Tick> 能构造，load、store、exchange、CAS 逐一在跑；fetch_add 编不过，探针回话见下方。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

两个字段，可平凡复制，static_assert 通过，`std::atomic<Tick>` 能构造，操作面就是前面数过的四个，load、store、exchange、CAS。想给它来一手 fetch_add，比如推进 seq，咱们把探针喂给编译器，回话在下面：

```text
probe_fetch.cpp: In function ‘int main()’:
probe_fetch.cpp:12:16: error: ‘struct std::atomic<Tick>’ has no member named ‘fetch_add’
   12 | int main() { t.fetch_add(1); return 0; }
      |                ^~~~~~~~~
```

没有 fetch_add，算术操作面只给整型特化。咱们用 CAS 循环照样能把任意读改写压成一次动作，CAS 一节的全套办法都适用。字节层面还有两个注意，标准用注给了。从不参与值表示的填充位会被忽略（C++20 起），带 padding 的结构体不容易虚假失败。union 里有的位只参与部分成员的值表示，CAS 可能永远失败（[atomics.types.operations] 注 7、注 8）。

最后两个现成的出口，咱们一并交代。`atomic<std::shared_ptr<T>>` 是 C++20 的偏特化，主模板进不去的类型库亲自认识，想原子地换一份共享指针又不想自己管引用计数，走它。`std::atomic_ref`（C++20）给普通对象临时套原子操作，cookbook 的 atomic_ref 配方还没成文，到时候咱们接着说。

## 轮到您动手的时候

动笔改代码之前，我把本篇攒下的问题列成一列，您对着自己的共享变量过一遍：

1. 您要保护的业务动作，写成内存动作是几步？每一步各自完整吗，步与步之间谁护着？
2. 它能压成一次读改写吗？您能压，就用 fetch_add 或者 CAS 循环。压不成，锁回场，或者改设计，别硬压。
3. 您循环里的 CAS 用的是 weak 吗？循环外的一次性判断换 strong 了吗？
4. 您的原子变量真的无锁吗？`is_always_lock_free` 一问便知，关键路径上用 static_assert 压住。
5. 浮点计数您换整数最小单位了吗？自定义类型的字段里有没有 padding，有没有 union？

## 下一步

本单元下一篇是 5.2《[一个线程怎样观察另一个线程的修改](02-happens-before)》，已经写好，咱们到那儿接着走。3.4 借屏障完成函数垫的那句“强发生”，到那里换成正式的 happens-before 关系。1.5《让工作有序结束》讲停止标志时那句 relaxed 连可见性都不保证，也在那里拿到判定工具。内存序的选择依据归 5.3《选择内存序需要什么依据》，还没成篇。第四单元还欠 4.4《把计算包装成可存储分发的任务》，它跟单元五互不挡路，成篇后各归各位。

翻页之前，我给您留一个动手题。把 2.1 里 `atomic<int>` 计数器那版程序翻出来，修两版，一版用 fetch_add，一版用 CAS 循环，循环里加个计数器数重试。跑 30 轮，看两版的结果是不是都齐，CAS 版的重试次数落在什么范围。

## 参考资源

本篇八个实验与两处片段的补全收在 code/examples/vol5/chapter05/，可在本页直接运行（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2 内核 6.18.33.2，16 逻辑核，2026-11-16，抽成独立文件时复验过），两个编译探针的回话照录在上文。ABA 与卖超两个 demo 的场面是编排出来的，线程次序写死，读数可复现。demo_flag 的 200000、demo_float 的整型对照与卡住的读数、demo_init 的零值、demo_lockfree 的查询结果，每次运行都一致。demo_cas 的成功行落在第 2 次尝试，由 50 毫秒与 100 毫秒两个时长定死。涉及标准行为的断言，我按 eel.is 上的现行工作草案核对，方括号里是小节名，小节名跟正式标准一致，段号可能挪。cppreference 的 atomic、atomic_flag、compare_exchange、fetch_add、构造函数、ATOMIC_FLAG_INIT 六页都抓到了全文，与草案对读无出入。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是我自己写的。

- [std::atomic（cppreference）][cppref-atomic]，特化分档、操作面与 C++17 的 is_always_lock_free。
- [std::atomic_flag（cppreference）][cppref-flag]，保证无锁的说明，C++20 的 test 与 wait/notify。
- [compare_exchange_weak 与 strong（cppreference）][cppref-cas]，虚假失败、expected 的覆盖、按值表示比较的版本变化。
- [std::atomic 的 fetch_add（cppreference）][cppref-fetchadd]，浮点特化的 C++20 注与有符号整数的回绕。
- [std::atomic 的构造函数（cppreference）][cppref-ctor]，默认构造的值初始化与 LWG 2334。
- [ATOMIC_FLAG_INIT（cppreference）][cppref-afi]，C++20 起默认构造取代它，LWG 3659 撤回废弃。
- [Class template atomic（现行草案 atomics.types.generic）][draft-generic]，可平凡复制等编译期要求，整型、浮点、指针与智能指针特化的分工，bool 特化走主模板的注。
- [Operations on atomic types（现行草案 atomics.types.operations）][draft-ops]，CAS 的效果与虚假失败注、is_lock_free 的说明、wait 与 notify。
- [Partial specializations for smart pointers（现行草案 util.smartptr.atomic）][draft-smartptr]，shared_ptr 与 weak_ptr 的原子偏特化。
- [Multi-threaded executions and data races（cppreference）][cppref-races]，data race 判定的回顾，2.1 核过的条款。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-atomic]: https://en.cppreference.com/w/cpp/atomic/atomic
[cppref-flag]: https://en.cppreference.com/w/cpp/atomic/atomic_flag
[cppref-cas]: https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
[cppref-fetchadd]: https://en.cppreference.com/w/cpp/atomic/atomic/fetch_add
[cppref-ctor]: https://en.cppreference.com/w/cpp/atomic/atomic/atomic
[cppref-afi]: https://en.cppreference.com/w/cpp/atomic/ATOMIC_FLAG_INIT
[draft-generic]: https://eel.is/c++draft/atomics.types.generic
[draft-ops]: https://eel.is/c++draft/atomics.types.operations
[draft-smartptr]: https://eel.is/c++draft/util.smartptr.atomic
[cppref-races]: https://en.cppreference.com/w/cpp/language/multithread
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
