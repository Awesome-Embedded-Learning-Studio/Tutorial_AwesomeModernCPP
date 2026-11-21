---
title: "线程在机器上是什么"
description: "从 std::thread 下到内核线程，看清 1:1 模型与线程的真实开销，看 Linux 的 pthread、clone 与 futex 怎么配合，比较三种线程模型，再看调度怎么排、跑多久。"
chapter: 5
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20, 23]
---

# 线程在机器上是什么

> 这一篇，是笔者自己给那些希望初步了解一下 `std::thread` 黑盒后到底是什么的朋友写的一些文章！

咱们已经写过 `std::thread`，也画过线程轮流用 CPU 的图。那一行代码在 Linux 上究竟做了什么？这篇选读陪您往下追，追到底您会看到一件有点反直觉的事：内核里其实没有"线程"这个特殊物种。系统调用层面咱们用 strace（跟踪进程发出的每个系统调用的工具）观察过，涉及 Linux 行为的断言查证过对应的手册页。

## 从 `std::thread` 到内核线程：1:1 模型

从最小的一段代码追起：

<OnlineCompilerDemo
  title="最小的 std::thread 程序"
  source-path="code/examples/vol5/chapter01/05_minimal_thread.cpp"
  description="构造即启动：声明 std::thread 的那一行执行完，系统里多了一条执行流，join 等它结束。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 最小的 std::thread 程序:构造即启动,join 等待
#include <print>
#include <thread>

void work() {
    std::println("working");
}

int main() {
    std::thread t(work); // 这一行执行完，系统里多了一条执行流
    t.join();
}
```

</OnlineCompilerDemo>

构造即启动。`std::thread` 的构造函数（C++11 起）拿到工作函数之后，不是登记了再排队，而是当场创建一条新的执行流，新线程立刻开始跑 `work`。空口无凭，咱们用 strace 跟了一趟 `std::thread t(work);`，看它触发了什么（输出有删节，地址因机器而异，长行折了行）：

```text
clone3({flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|
        CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID,
        exit_signal=0, stack=0x7ddac21ff000, stack_size=0x7fff40, ...})
```

构造函数触发的是一次 clone3 系统调用，它是 clone 的新式接口，也是 Linux 创建执行流的原语。flags 里每一项都在声明新执行流跟老执行流共享什么，比如 `CLONE_VM` 共享虚拟地址空间、`CLONE_FILES` 共享打开文件的描述符表，这些标志下一节挑要紧的几个看。换句话说，您写下 `std::thread t(work);`，内核里就多了一条可以独立调度的执行流。

操作系统的教科书说法是：线程是 CPU 调度的基本单位，进程是资源分配的基本单位。这句话咱们用代码核实过，让主线程和它创建的两条线程各自报告自己属于哪个进程、自己的线程编号是多少：

<OnlineCompilerDemo
  title="同进程不同 tid：getpid 与 gettid"
  source-path="code/examples/vol5/chapter01/06_pid_and_tid.cpp"
  description="主线程与两条工作线程报告各自的进程号与线程编号——进程号相同、tid 各异，线程是 CPU 调度的基本单位。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 线程与进程编号:同一个进程里的多条执行流,tid 各不相同
#include <print>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

static long my_tid() {
    return syscall(SYS_gettid);
}

int main() {
    std::println("main    : getpid = {}, tid = {}", getpid(), my_tid());
    std::thread t1([] { std::println("worker-1: getpid = {}, tid = {}", getpid(), my_tid()); });
    std::thread t2([] { std::println("worker-2: getpid = {}, tid = {}", getpid(), my_tid()); });
    t1.join();
    t2.join();
}
```

</OnlineCompilerDemo>

```text
main    : getpid = 46, tid = 46
worker-1: getpid = 46, tid = 47
worker-2: getpid = 46, tid = 48
```

咱们把三行输出摆在一起看。三条执行流的 `getpid` 相同，`tid` 各不相同。`getpid` 回答的是它属于哪个进程，三条线程给出同一个答案，所以它们是同一个进程里的成员。`tid` 是内核发给每条执行流的编号，这个编号各不相同，说明内核眼里它们是三条可以分别调度的执行流。共享的部分让线程之间通信便宜（同一个地址空间，读写同一批变量不用任何跨进程的手段），独占的部分让它们可以各自推进。

这个映射关系叫 1:1 模型：您写的每一条 `std::thread`，都对应内核里一条真实的调度实体，中间没有别的层。这件事的直接后果是，线程的每一分开销都是内核级别的真实开销。咱们把开销分成两笔看。

第一笔是栈。每条线程要有自己的栈，函数调用、局部变量都放在里面，不能跟别的线程共用。栈有多大，咱们查的而不是猜的：

<OnlineCompilerDemo
  title="每条线程的栈有多大"
  source-path="code/examples/vol5/chapter01/07_thread_stack_size.cpp"
  description="用 pthread_getattr_np 查主线程与工作线程的栈大小，这个数是查出来的，不是猜的。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 每条线程的栈有多大:pthread_getattr_np 查实际栈大小
#include <cstddef>
#include <print>
#include <pthread.h>
#include <thread>

static void report(const char* who) {
    pthread_attr_t attr;
    void* addr = nullptr;
    size_t size = 0;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &addr, &size);
    std::println("{} stack size = {} KB", who, size / 1024);
    pthread_attr_destroy(&attr);
}

int main() {
    report("main  ");
    std::thread t([] { report("worker"); });
    t.join();
}
```

</OnlineCompilerDemo>

```text
main   stack size = 8184 KB
worker stack size = 8192 KB
```

新线程的栈是 8192 KB，也就是 8 MB，正好等于咱们机器 `ulimit -s` 的值。这是因为 pthreads 的手册页写明：只要栈大小的软限制（`RLIMIT_STACK`）不是 unlimited，它就决定新线程的默认栈大小。所以“Linux 默认线程栈 8 MB”这句话只对默认配置成立，您拿 `ulimit -s` 对一下自己机器的数就知道了。

输出里 main 报的 8184 KB 是进程自己的栈，来历不同，咱们看一眼就好，不用管它。还有一个细节要交代准确：8 MB 是预留的虚拟地址空间，物理内存按实际用到的页增长，一条刚出生的线程并没有真的占掉 8 MB 内存。但这个预留本身就是约束，开一万条线程会怎样，咱们摆一下数：光栈的预留就是 80 GB 的地址空间，加上内核要为每条线程维护的调度信息，量级告诉您 `std::thread` 不是拿来撒的。

第二笔在切换上。内核的调度器在多条执行流之间来回切换，让每条都推进一点。切换分两部分。直接的部分是把当前线程的寄存器状态保存起来，把下一条线程的状态恢复回来。间接的部分往往更大，新线程的数据大概率不在现在这个核心的缓存里，地址翻译的缓存（TLB）里装的也是上一条线程的映射，接手的核心要把工作集重新装进缓存。经验上，一次切换的直接加间接开销在几微秒到几十微秒，这个数字咱们没有单独实测，属经验量级。咱们实测的是一件相邻的事：两个线程通过条件变量互相通知，各自睡下又被唤醒，来回了十万次。每次往返的耗时咱们拿条件变量与互斥锁写了个乒乓程序来量：

<OnlineCompilerDemo
  title="条件变量乒乓：一次唤醒往返多贵"
  source-path="code/examples/vol5/chapter01/08_cv_pingpong.cpp"
  description="两个线程用互斥锁加条件变量互相通知，十万次往返计时，量出唤醒与切换的开销。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 条件变量乒乓:两个线程互相通知十万次,量一次唤醒往返的开销
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <print>
#include <thread>

int main() {
    std::mutex m;
    std::condition_variable cv;
    int turn = 0;
    constexpr long kRounds = 100000;
    std::thread worker([&] {
        std::unique_lock<std::mutex> lk(m);
        for (long i = 0; i < kRounds; ++i) {
            cv.wait(lk, [&] { return turn == 1; });
            turn = 0;
            cv.notify_one();
        }
    });
    auto start = std::chrono::steady_clock::now();
    {
        std::unique_lock<std::mutex> lk(m);
        for (long i = 0; i < kRounds; ++i) {
            turn = 1;
            cv.notify_one();
            cv.wait(lk, [&] { return turn == 0; });
        }
    }
    auto end = std::chrono::steady_clock::now();
    worker.join();
    const long us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::println("rounds = {}, total = {} us, per round trip = {} us", kRounds, us,
                 static_cast<double>(us) / kRounds);
}
```

</OnlineCompilerDemo>

```text
rounds = 100000, total = 5634019 us, per round trip = 56.3402 us
rounds = 100000, total = 5641756 us, per round trip = 56.4176 us
rounds = 100000, total = 5764721 us, per round trip = 57.6472 us
```

咱们跑了三遍，都在 56 到 58 微秒（WSL2 虚拟化环境，裸机多半更低）。一次往返里装着两次睡眠与唤醒、两次线程切换，还有锁自身的开销，所以它给的是切换成本的一个偏高的口径。咱们按几十微秒一次来估，切换是真实的钱。我们曾经讲过：任务分得太碎，收益就可能被开销吃掉。钱花在哪，花在这里。

## Linux 的实现：pthread、clone 与 futex

`std::thread` 底下那层，在 Linux 上是 pthread。pthread 不是内核里的东西，它是 POSIX 标准规定的一组线程接口，`pthread_create`、`pthread_join` 这些函数在哪儿都能用，各家系统自己实现。Linux 的现代实现叫 NPTL（Native POSIX Thread Library），glibc 2.3.2 起可用，依赖 Linux 2.6 内核提供的特性，之后成了默认。您可以用 `getconf GNU_LIBPTHREAD_VERSION` 查自己机器用的是哪一套，咱们机器上答的是 NPTL 2.44。手册页把这一实现概括为 1:1（每条线程映射到一条内核调度实体），并且写明它建立在 clone 这个系统调用上。

clone 是 Linux 最底层的创建执行流的原语。`fork()` 创建一个全新的进程，父子什么都不共享。clone 让调用者逐项指定共享哪些。地址空间、文件描述符表、信号处置、文件系统信息（当前目录、umask 这些），都可以单列进去。有一个说法值得转过来：内核内部其实没有“线程”这个特殊物种，它调度的是一条条任务，进程与线程的差别只是共享了多少。共享清单开得越多，新执行流越像线程。一项都不共享，它就是一个传统进程。pthread_create 要的是大部分共享的那一档，咱们在上一节 strace 里看到的那个 flags 串就是清单本身：`CLONE_VM` 共享地址空间，`CLONE_FILES` 共享文件描述符表，`CLONE_SIGHAND` 共享信号处置，`CLONE_THREAD` 把新执行流放进同一个线程组，让三条线程共用同一个进程编号。清单末尾的 `CLONE_CHILD_CLEARTID` 平时不起眼，值得单独看一眼，它跟线程的结束有关，讲到 futex 的时候咱们回头把它接上。

创建追完了，接着追同步。互斥锁有个天然的难题：锁没有被争抢的时候，加锁解锁要不要跑一趟内核？Linux 的答案是 futex（fast userspace mutex，快速用户态互斥锁），思路一句话：没有竞争，全程留在用户态，几条原子指令完事。真有竞争，才进内核，让内核把等锁的线程挂起，等持有者释放时再把它唤醒。手册页 futex(7) 的原话翻译过来是“无竞争时整个操作都在用户空间完成”，内核只在有竞争的时候才出场。

咱们把两条路径都实测了一遍。第一趟，主线程独自对一把没人争的 mutex 加锁解锁一百万次，用 `strace -c` 数系统调用（`strace -c` 输出截取）：

```text
  8.88    0.000027          27         1           futex
100.00    0.000304           4        65         1 total
```

全程 65 个系统调用，futex 只有 1 次，还是程序启动时运行库自己产生的。一百万次加锁解锁，零系统调用，快速路径名不虚传。第二趟咱们安排真争抢：一条线程拿住锁睡 100 毫秒，另一条 10 毫秒后伸手要锁。这一伸手就出了系统调用（输出有删节，地址因机器而异）：

```text
[pid 66] futex(0x60dc3522b0a0, FUTEX_WAIT_PRIVATE, 2, NULL <unfinished ...>
[pid 67] futex(0x60dc3522b0a0, FUTEX_WAKE_PRIVATE, 1 <unfinished ...>
```

`FUTEX_WAIT` 是等锁的线程请内核把自己挂起，`FUTEX_WAKE` 是释放锁的线程请内核唤醒等待者。名字里的 `_PRIVATE` 后缀表示这把锁只在进程内部用，不跨进程共享。往后翻手册和源码，咱们还会反复遇到 `FUTEX_WAIT` 和 `FUTEX_WAKE`。`std::mutex` 在 Linux 上就是这套机制包出来的，所以“无竞争的锁便宜，竞争激烈的锁贵”不是比喻，是两条代码路径的区别：前者一条系统调用都没有，后者每一次等待与唤醒都要进内核。

第二趟实验的完整输出里还有一条 `FUTEX_WAIT_BITSET`，刚才删节时按下了没讲，现在可以把它讲完。它作用的地址落在等锁那个地址之外，是新线程栈顶附近记录线程编号的那个地址，也就是上一节 clone 清单里 `CLONE_CHILD_CLEARTID` 指的那个地址。clone(2) 的手册页写明：新线程退出时，内核把这个地址清零，并对它做一次 futex 唤醒。`pthread_join` 等线程结束，等的正是这个事件。咱们写 `t.join()`（C++11 起）的时候，等的就是这个内核级别的信号。创建靠 clone，结束靠 futex，一条线程的一生在 Linux 上的两头都看到了。

## 线程模型对比：1:1、M:N、N:1（扩展）

本节带（扩展）标记：认识了三种映射关系，后面遇到线程池和协程时，您能认出它们在解决哪类问题。跳过它，本篇主线和第一单元的后续内容都不受影响。

前面两节咱们看的都是 1:1，一条用户侧的线程对应一条内核调度实体，这是 C++ 程序员默认拿到的模型。把视角拉高，用户侧的执行流和内核的执行流之间还有另外两种接法。

N:1 是一个极端：多条用户线程全部挂在一个内核线程上。创建与切换完全在用户态完成，不进内核，轻快便宜。但代价是结构性的，任何一条用户线程做了阻塞操作，底下的内核线程就被卡住，全部用户线程一起停摆。而且内核眼里只有一条执行流，多个核心帮不上忙，真正的并行无从谈起。早期的一些绿色线程（green threads）实现就是按 N:1 做的，咱们把它当作了解历史就够了。

M:N 试图两头都占：M 条用户线程映射到 N 条内核线程上，M 通常远大于 N。调度器运行在用户态，把用户线程分配到内核线程上执行，用户线程保持轻量，内核线程的数量又足以铺开到多个核心。Go 的 goroutine 是这个模型的代表，它的初始栈只有几 KB，由 Go 运行时的调度器安排到少量操作系统线程上。代价是运行时自身的复杂度：抢占、系统调用的包装、两级栈的管理，都要运行时自己处理，这些恰好是内核本来替 1:1 模型做的事情。咱们在 C++ 里一般不碰这个复杂度，除非自己做运行时。

C++ 的位置在 1:1。`std::thread` 在主流平台的标准实现里都落在操作系统的原生线程接口上（Linux 语境咱们已经实测过，直接到 clone），所以前面两节的开销分析对它逐条成立。当任务数量远超核数、每个任务又很轻时，一条任务一条线程就不经济了，现成的路有两条。线程池用固定数量的工作线程加任务队列，把线程创建的成本摊销掉。线程池的教学级完整版本，我计划写进 cookbook 的选读区，写好后在参考资源补链接。协程把“可以暂停的工作”作为语言机制，本卷后面讲协程的单元会专门展开。两者本质上都是在 1:1 之上构建的调度策略，把“接下来跑哪条”的决定从内核拿回来一部分，由库或者您自己掌控。《什么时候值得引入并发》讲过“您需要的是异步 I/O”的情形，协程正是其中一条路。

## 调度：怎么排，跑多久

最后一站是操作系统的日常：机器上几千条线程，十几个核心，怎么排，每条跑多久。咱们写并发程序时看不到这个决定的过程，但它决定了程序表现出来的全部时间性质，值得认识一下它的脾气。

现代操作系统可以抢占正在运行的线程，把 CPU 交给另一条线程。具体什么时候换人，由调度策略决定。咱们在《第一个 std::thread 程序》里见过几条线程的输出交错在一起，回头再看，您现在有了更具体的解释。

时间片这个词您会常看到，教科书讲抢占式时用它称呼“分给一条线程的那一小段时间”。Linux 的普通调度器没有固定的时间片，咱们把它的实际做法说清楚。平时创建的线程全部默认属于 SCHED_OTHER 策略（内核源码里叫 SCHED_NORMAL），实现这个策略的调度器从 2.6.23 起是 CFS（完全公平调度器），思路是给每条线程记录一个虚拟运行时间，虚拟运行时间落后的一条下一个上，让同优先级的线程公平地分到 CPU。内核 6.6 起换成了 EEVDF，公平的目标没变，选下一条线程时再按虚拟截止时间排序，对响应敏感的任务可以申请更短的时间片。CFS 和 EEVDF 两个名字，您知道到这个程度就够，SCHED_OTHER 的语义在两个实现之间是稳定的，咱们写的程序不需要区分底下是谁。

您想给同一批普通线程排个次序，靠的是 nice 值。范围是 -20 到 +19，默认 0，数值越低越优先。手册里有一个有用的具体数字：现在的实现里，nice 每差一档，调度器偏爱高优先级一方的程度差 1.25 倍，一个档位就是一个系数，几十档的差距累积起来非常可观。还有一点容易想错：POSIX 标准说 nice 是进程属性，Linux 的实现是每线程一个，同一个进程里的线程可以有不同的 nice 值。您要调，用 `nice` 命令或者 `setpriority()` 系统调用。

最后一块是亲和性（CPU affinity），指的是线程跟核心之间的绑定关系。默认情况下调度器可以把线程在核心之间搬来搬去，一条线程在核心 A 上跑了一段时间，接下来可能在核心 B 上。搬家有代价，新核心的缓存和 TLB 都要重新装，正是 1:1 模型一节讲切换开销时间接的那部分。如果您知道某条线程的工作集大、缓存命中要紧，可以用 `sched_setaffinity()` 把它跟固定的核心绑住。这是 Linux 特有的接口，管的是每线程的属性，C++ 标准库里没有对应物，好在 `std::thread::native_handle()`（C++11 起）能拿到底层的 pthread_t，POSIX 的接口从那儿接着用。咱们实测了一遍，给刚创建的线程绑核心：

<OnlineCompilerDemo
  title="sched_setaffinity 绑核"
  source-path="code/examples/vol5/chapter01/09_cpu_affinity.cpp"
  description="经 native_handle 拿到 pthread_t，用 POSIX 接口把线程绑到固定核心。在线沙箱仅 2 核，示例绑 cpu 0，本地机器可改绑其他核心。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// sched_setaffinity 绑核:经 native_handle 拿 pthread_t,用 POSIX 接口绑固定核心
// 在线沙箱只有 2 个逻辑核,这里绑 cpu 0;本地机器可改绑其他核心
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <print>
#include <pthread.h>
#include <sched.h>
#include <thread>

int main() {
    std::thread t([] {
        for (int i = 0; i < 3; ++i)
            std::println("worker on cpu {}", sched_getcpu());
    });
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    pthread_t native = t.native_handle();
    int rc = pthread_setaffinity_np(native, sizeof(set), &set);
    std::println("pin worker to cpu 0 via native_handle, rc = {}", rc);
    t.join();
}
```

</OnlineCompilerDemo>

```text
pin worker to cpu 5 via native_handle, rc = 0
worker on cpu 5
worker on cpu 5
worker on cpu 5
```

`rc` 为 0 说明绑定设置成功，线程三次查看自己所在的核心，都答 cpu 5，`native_handle` 拿到的句柄确实可以直通 POSIX 接口。绑核是一种性能手段，不是默认动作，大多数程序让调度器自由安排就好。咱们能带走两句话。线程在核心间是可以搬家的，搬家有代价。绑核的手段存在，接口是平台相关的，标准库不替您管这件事。

## 下一步

咱们看过调度器怎么给线程安排时间了。回到 C++ 程序里，两条线程换一种次序执行，结果也得对。下一单元从一个算错的计数器入手，看看同一份数据被两条线程碰到时，会出什么问题。 [同一个计数器，两条线程为什么算不对？](../chapter02-sharing-data/01-from-wrong-programs)

## 参考资源

本篇写作时对照借鉴了 Anthony Williams 的《C++ Concurrency in Action》第二版，取的是其中关于线程的章节推进思路。例子与文字都是我自己写的。涉及 C++ 行为的断言，按文中标注编译实测或查证 cppreference。涉及 Linux 行为的断言，查证手册页。实验的编译器版本、标准选项与日期都在文中标明。

- [std::thread（cppreference）][cppref-thread]，C++11 起的线程接口，构造即启动。
- [std::thread::join（cppreference）][cppref-join]，等待线程完成及其同步保证。
- [std::thread::native_handle（cppreference）][cppref-native]，取底层线程句柄（C++11 起）。
- [clone(2) — Linux manual page][man-clone]，创建执行流的原语，各共享标志的含义，以及线程库如何使用它。
- [futex(7) — Linux manual page][man-futex]，快速用户态锁定的两条路径：无竞争全在用户态，竞争才进内核。
- [pthreads(7) — Linux manual page][man-pthreads]，POSIX 线程的 Linux 实现，NPTL 的来历，线程共享与独占属性的清单，默认栈大小跟 RLIMIT_STACK 的关系。
- [sched(7) — Linux manual page][man-sched]，调度策略、nice 值的语义与每线程属性、CPU 亲和性。
- [EEVDF Scheduler — kernel.org documentation][kernel-eevdf]，内核 6.6 起公平调度器从 CFS 更替为 EEVDF 的说明。
- [The Native POSIX Thread Library for Linux — Ulrich Drepper, Ingo Molnar][nptl-design]，NPTL 的设计文档。

[cppref-thread]: https://en.cppreference.com/w/cpp/thread/thread
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[cppref-native]: https://en.cppreference.com/w/cpp/thread/thread/native_handle
[man-clone]: https://man7.org/linux/man-pages/man2/clone.2.html
[man-futex]: https://man7.org/linux/man-pages/man7/futex.7.html
[man-pthreads]: https://man7.org/linux/man-pages/man7/pthreads.7.html
[man-sched]: https://man7.org/linux/man-pages/man7/sched.7.html
[kernel-eevdf]: https://docs.kernel.org/scheduler/sched-eevdf.html
[nptl-design]: https://www.akkadia.org/drepper/nptl-design.pdf
