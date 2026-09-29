---
title: "死锁与现场诊断"
description: "复现一个必死的 AB-BA 现场，用 gdb 三命令判读谁在等谁，再把总锁序、std::lock、try_lock 回退三条防线与层级锁一件件配齐"
chapter: 2
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
difficulty: intermediate
platform: host
reading_time_minutes: 20
prerequisites:
  - "mutex 与 RAII 守卫"
  - "数据竞争与 ThreadSanitizer 第一课"
related:
  - "thread_local：每线程一份的世界"
  - "condition_variable 与阻塞队列"
  - "同步原语工具箱"
cpp_standard:
  - 11
  - 17
  - 20
---

# 死锁与现场诊断

[mutex 那篇](./01-mutex-and-raii-guards.md)里咱们把一把锁的用法立稳了，可它也留了话：一次要拿两把锁的时候，拿的顺序不对，程序就僵住了。那篇演示 Account 划转的时候，把这团麻烦整个绕开了，`scoped_lock` 一条语句拿了两把，顺序的问题交给算法接管了。[thread_local 那篇](./02-thread-local.md)刚把每线程一份的世界走完，那是把不共享做到头的路线。本篇咱们把遮着的布掀开，看看共享的路线要付什么代价：咱们故意不接管，亲眼看一看僵住的样子，再学一学怎么把挂住的程序查个水落石出。

挂住是并发程序里体感最特别的一种死法。崩溃好歹给您留一个 core 加一行信号，挂住的时候什么都不给：CPU 静悄悄的，日志停在半截不动了，进度条也不动了，整个程序吊在半死不活的状态里。[data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)的结尾咱们让死锁亮过相、四样配方也背过，当时说好的，诊断的流程归本篇展开。咱们把死锁稳定地跑出来，用 gdb 的三命令把现场判读明白，再把防身的手段一件件配齐。

## 复现：一次必死的 AB-BA

最小的死锁现场，两把全局锁就够了。标题里的 AB-BA 说的就是它：两个线程拿锁的顺序正好相反，一个照着 A、B 的顺序拿，另一个照着 B、A 的顺序拿，名字画的就是这个对称的环。咱们直接看程序，35 行的完整代码，没有一行多余的：

```cpp
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> a(mtx_a);               // 拿 A
    std::cout << "t1: 拿到 A，伸手等 B\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> b(mtx_b);               // 等 B：B 在 t2 手里
    std::cout << "t1: 两把都到手\n";
}

void thread2()
{
    std::lock_guard<std::mutex> b(mtx_b);               // 拿 B
    std::cout << "t2: 拿到 B，伸手等 A\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> a(mtx_a);               // 等 A：A 在 t1 手里
    std::cout << "t2: 两把都到手\n";
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    std::cout << "程序正常收尾（死锁时到不了这里）\n";
    return 0;
}
```

thread1 拿了 A 再去够 B，thread2 拿了 B 再去够 A，两边各攥了一把，互等对方手里的那把。mutex 那篇移交过来的 AB-BA 形状，活生生地就摆在眼前。咱们来编译运行，这一回咱们把调试信息也带上，gdb 稍后就要靠它把源码的行号对回来：

```bash
g++ -std=c++20 -Wall -Wextra -pedantic -pthread -g -O0 deadlock.cpp -o deadlock
timeout 3 ./deadlock
echo $?
```

```text
t1: 拿到 A，伸手等 B
t2: 拿到 B，伸手等 A
124
```

两行打印之后就没了下文，`timeout` 替咱们在三秒后强行掐断，退出码 124 是它留下的记号。您把两个线程的处境摆在一起看：t1 的第二把锁在等 t2 放 B，t2 的第二把锁在等 t1 放 A，可谁的第二把都拿不到了，第一把也就跟着永远放不掉了。中间那 50 毫秒的睡是笔者加的，它把交错的窗口拉宽了，宽到肉眼可见的程度，所以这个程序一跑就死。真实工程里的窗口窄得多，其实十万次里才碰上一次交错，可机制跟这 35 行的一模一样：只要两个序列按相反的方向各拿一次，环就成了。

还有一层区别值得咱们看清。mutex 那篇里手动 `lock()`/`unlock()` 漏放的那次锁，挂的是下一个来拿锁的线程，凶手与受害者落在不同的线程里。死锁就不一样了，每个当事的线程既是受害者，也是扣着锁不放的人，这正是它难缠的地方：现场没有任何一方在做错事的样子，大家都在安安分分地等锁。既然等是合法的动作，工具就不会替咱们报警，证据得咱们自己动手来取。

## 用 gdb 看现场：三命令走一遍

证据要从挂住的进程身上取。挂住的程序不会自己停下来给咱们看，咱们得用 gdb 手动把它冻住：做法是从 gdb 里把程序跑起来，僵住了就按 Ctrl-C，中断信号会把所有的线程一起停在同一瞬间，现场也就固定下来了。咱们需要的命令只有三个，咱们一段一段地走。

> 笔者手头的机器（WSL2）上有两个环境细节，您大概率也会碰上。头一个是附加权限的问题：`ptrace_scope` 在不少发行版上的默认值，只允许调试自己的子进程，所以想附加一个已经在跑的进程（`gdb -p <pid>`）会被内核拒绝，而从 gdb 里 `run` 启动的进程就不受限，咱们干脆就从 gdb 里启动。写批处理脚本的时候可以另开一端，用 `kill -INT <pid>` 给被调试的进程发中断，效果与 Ctrl-C 的一样。另一个是调试符号的下载询问：gdb 启动的时候若逐条问要不要下载调试符号（debuginfod），拿 `set debuginfod enabled off` 一句关掉就好了，世界一下子清静了。

咱们把会话的开头录下来：

```text
$ gdb ./deadlock
(gdb) set debuginfod enabled off
(gdb) run
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".
[New Thread 0x7ffff77ff6c0 (LWP 880929)]
t1: 拿到 A，伸手等 B
[New Thread 0x7fffeffff6c0 (LWP 880930)]
t2: 拿到 B，伸手等 A
（两行之后没了动静，此时按 Ctrl-C）
Thread 1 "deadlock" received signal SIGINT, Interrupt.
0x00007ffff78a0952 in ?? () from /usr/lib/libc.so.6
```

下面的输出都是这次会话的真实记录。您自己跑的时候，十六进制地址、LWP（内核眼里的线程号，轻量级进程）编号、路径都会跟这里的不一样，判读的方法倒是不变。

### 命令一：info threads，把在场的人点一遍

```text
(gdb) info threads
  Id   Target Id                                     Frame
* 1    Thread 0x7ffff7f71780 (LWP 880926) "deadlock" 0x00007ffff78a0952 in ?? () from /usr/lib/libc.so.6
  2    Thread 0x7ffff77ff6c0 (LWP 880929) "deadlock" 0x00007ffff7894efe in ?? () from /usr/lib/libc.so.6
  3    Thread 0x7fffeffff6c0 (LWP 880930) "deadlock" 0x00007ffff7894efe in ?? () from /usr/lib/libc.so.6
```

gdb 手册对它的说明就两句话：`Display information about one or more threads. With no arguments displays information about all threads.`，以及 `An asterisk '*' to the left of the GDB thread number indicates the current thread.`——星号标在了 1 号的左边，说明 gdb 现在的注意力落在主线程上，而这个细节下一节马上就要用到。另外每行括号里的 LWP 跟 gdb 自己编的 Id 一一对应，但数值是不相干的，后面 owner 字段的判读要拿 LWP 对人，咱们在这里混个脸熟就够了。

咱们单看五行的输出，第一层的判读就有了：2 号和 3 号两个线程停在完全相同的地址 `0x7ffff7894efe` 上，而主线程停在了别的地址，两个工作线程挤在同一扇门上的形状，就这么露出来了。至于那扇门是不是锁的等待，从 Frame 列的 `??` 里是看不出来的。libc 没装调试符号的时候就会这样显示，好在咱们的判读用不上它，咱们拿完整的栈说话。

### 命令二：thread apply all bt，把每条栈都拉出来

手册的原话是 `To apply a command to all threads in descending order, type thread apply all command.`——它按 Id 的降序走，从 3 号开始逐线程地执行，`bt`（backtrace 的缩写）负责打印每条调用栈。它是三命令里的主力，咱们接着看它的输出：

```text
(gdb) thread apply all bt

Thread 3 (Thread 0x7fffeffff6c0 (LWP 880930) "deadlock"):
#0  0x00007ffff7894efe in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff789b9e4 in pthread_mutex_lock () from /usr/lib/libc.so.6
#2  0x0000555555555606 in __gthread_mutex_lock (__mutex=0x5555555591e0 <mtx_a>) at /usr/include/c++/16/x86_64-pc-linux-gnu/bits/gthr-default.h:795
#3  std::mutex::lock (this=0x5555555591e0 <mtx_a>) at /usr/include/c++/16/bits/std_mutex.h:116
#4  0x0000555555555736 in std::lock_guard<std::mutex>::lock_guard (this=0x7fffefffed00, __m=...) at /usr/include/c++/16/bits/std_mutex.h:276
#5  0x00005555555553cf in thread2 () at /tmp/ch0203/deadlock.cpp:23
#6  0x00005555555562a9 in std::__invoke_impl<void, void (*)()> (__f=@0x55555556c188: 0x55555555533a <thread2()>) at /usr/include/c++/16/bits/invoke.h:63
#7  0x000055555555626f in std::__invoke<void (*)()> (__fn=@0x55555556c188: 0x55555555533a <thread2()>) at /usr/include/c++/16/bits/invoke.h:98
#8  0x000055555555622a in std::thread::_Invoker<std::tuple<void (*)()> >::_M_invoke<0ul> (this=0x55555556c188) at /usr/include/c++/16/bits/std_thread.h:303
#9  0x00005555555561fe in std::thread::_Invoker<std::tuple<void (*)()> >::operator() (this=0x55555556c188) at /usr/include/c++/16/bits/std_thread.h:310
#10 0x00005555555561e2 in std::thread::_State_impl<std::thread::_Invoker<std::tuple<void (*)()> > >::_M_run (this=0x55555556c180) at /usr/include/c++/16/bits/std_thread.h:255
#11 0x00007ffff7cea859 in ?? () from /usr/lib/libstdc++.so.6
#12 0x00007ffff78980a2 in ?? () from /usr/lib/libc.so.6
#13 0x00007ffff792080c in ?? () from /usr/lib/libc.so.6

Thread 2 (Thread 0x7ffff77ff6c0 (LWP 880929) "deadlock"):
#0  0x00007ffff7894efe in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff789b9e4 in pthread_mutex_lock () from /usr/lib/libc.so.6
#2  0x0000555555555606 in __gthread_mutex_lock (__mutex=0x555555559220 <mtx_b>) at /usr/include/c++/16/x86_64-pc-linux-gnu/bits/gthr-default.h:795
#3  std::mutex::lock (this=0x555555559220 <mtx_b>) at /usr/include/c++/16/bits/std_mutex.h:116
#4  0x0000555555555736 in std::lock_guard<std::mutex>::lock_guard (this=0x7ffff77fed00, __m=...) at /usr/include/c++/16/bits/std_mutex.h:276
#5  0x00005555555552ae in thread1 () at /tmp/ch0203/deadlock.cpp:14
……（#6–#13 与 Thread 3 完全相同：std::__invoke_impl / _M_run 等 std::thread 的机制帧）……

Thread 1 (Thread 0x7ffff7f71780 (LWP 880926) "deadlock"):
#0  0x00007ffff78a0952 in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff7894cd9 in ?? () from /usr/lib/libc.so.6
#2  0x00007ffff7899eed in ?? () from /usr/lib/libc.so.6
#3  0x00007ffff7cea8e4 in std::thread::join() () from /usr/lib/libstdc++.so.6
#4  0x00005555555554ab in main () at /tmp/ch0203/deadlock.cpp:31
```

栈拉全了，判读的核心材料也就到手了，咱们从下往上地剥。

咱们从 main 说起。Thread 1 的栈停在 `std::thread::join()`，源码的 31 行正是 `t1.join()` 那一行。main 在等自己的孩子们收工，而孩子们死锁了，它自然就永远等不到了——所以它是受害者，受害的原因恰恰是两个孩子在互等，追它是没有意义的。判读的时候容易被星号带偏：gdb 冻住现场的时候，当时的注意力正好落在 1 号上，您可别顺着它把 main 当成当事人。

再看 Thread 3 的 #1 帧，两份栈停在了同一个库函数里，跟 `info threads` 里看到的相同地址对上了。原来那扇挤满人的门，就是 libc 的锁等待，两个工作线程都卡在了 `pthread_mutex_lock` 里。真正的好东西还藏在 #2、#3 的参数里：#2 的参数印着 `__gthread_mutex_lock (__mutex=0x5555555591e0 <mtx_a>)`，#3 的 `std::mutex::lock (this=0x5555555591e0 <mtx_a>)` 印的还是同一把。gdb 把锁的符号名直接印在了参数上，一个 `<mtx_a>` 顶得上咱们半页的猜测。Thread 2 的 #2、#3 印的则是 `<mtx_b>`。

Thread 3 的 #5 帧是 `thread2 () at deadlock.cpp:23`，正是 thread2 里去拿 `mtx_a` 的那行 `lock_guard`。Thread 2 的 #5 帧是 `thread1 () at deadlock.cpp:14`，正是 thread1 里去拿 `mtx_b` 的那行。咱们对着源码把两头补全：thread2 在 20 行已经把 `mtx_b` 拿到了手（守卫对象还活着，所以锁放不掉），随后就堵在了 23 行的 `mtx_a` 上。thread1 的情形正好是镜像：11 行拿了 `mtx_a`，14 行堵在了 `mtx_b` 上。等的是哪把锁，源码的行号说得明明白白。

至此四条边凑齐了三条半：t1 等 B、t2 等 A 的两条等待边，咱们都能从栈上直接读到。而 t1 持 A、t2 持 B 的两条持有边，是从源码的行号推出来的——守卫对象还在作用域里，锁就必然还在它的手里。最后的半条边，咱们用 gdb 把它也变成直接的证据。

### 命令三：thread 切换，到当事线程里单独问话

手册的原话是 `Make thread ID thread-id the current thread.`。咱们用 `thread 3` 把当前的线程切成 3 号，之后的 `bt`、`print`、`list` 就都只对它生效了：

```text
(gdb) thread 3
[Switching to thread 3 (Thread 0x7fffeffff6c0 (LWP 880930))]
#0  0x00007ffff7894efe in ?? () from /usr/lib/libc.so.6
(gdb) bt
#0  0x00007ffff7894efe in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff789b9e4 in pthread_mutex_lock () from /usr/lib/libc.so.6
#2  0x0000555555555606 in __gthread_mutex_lock (__mutex=0x5555555591e0 <mtx_a>) at /usr/include/c++/16/x86_64-pc-linux-gnu/bits/gthr-default.h:795
#3  std::mutex::lock (this=0x5555555591e0 <mtx_a>) at /usr/include/c++/16/bits/std_mutex.h:116
#4  0x0000555555555736 in std::lock_guard<std::mutex>::lock_guard (this=0x7fffefffed00, __m=...) at /usr/include/c++/16/bits/std_mutex.h:276
#5  0x00005555555553cf in thread2 () at /tmp/ch0203/deadlock.cpp:23
……（往下与 thread apply all bt 里 Thread 3 的栈相同）……
```

切换的用处是能顺着栈往深处走：`frame 5` 跳进 `thread2` 的帧，`list` 直接看 23 行附近的源码，`print` 查看栈上的局部变量。您在单线程里用过的问题查法（断点、单步、打印），切换之后就都照常能用了，多线程的现场于是就化成了一条条单线程的栈。

### 进阶一招：把持有关系也变成数字

`std::mutex` 在 Linux 上包的是 pthread 的互斥量，glibc 的实现里有个 `__owner` 字段，记着当前持有者的线程号。咱们拿 gdb 就能直接看穿这层结构：

```text
(gdb) print mtx_a._M_mutex.__data.__owner
$1 = 880929
(gdb) print mtx_b._M_mutex.__data.__owner
$2 = 880930
```

咱们拿两个数字回到 `info threads` 的输出里对号：880929 是 Thread 2 的 LWP，880930 是 Thread 3 的 LWP。于是每条边都有了直接的证据：Thread 3 在等的 `mtx_a`（帧上印着），持有者的编号是 880929，也就是 Thread 2 拿着的。Thread 2 在等的 `mtx_b`，持有者的编号是 880930，也就是 Thread 3 拿着的。等待的两条边、持有的两条边，闭成了一个环，谁在等谁的锁、谁扣着哪把不放，咱们全程没有一个字是猜的。

不过咱们得说清楚，`_M_mutex` 和 `__owner` 都是 libstdc++ 与 glibc 的实现细节，而标准的接口里没有它们，换了平台、换了 libc 可能就没了。咱们拿它读现场没问题，写进产品的代码就不行了。

三命令走完了，咱们把手法收拢一下，您以后可以照着办：

1. 咱们用 `info threads` 点名，找出停在相同地址上的可疑线程，星号只是 gdb 的当前位置，您别被它带偏。
2. 咱们用 `thread apply all bt` 拉全栈，抄下卡住帧上的锁符号（像 `<mtx_a>` 这样印在参数里的名字）和源码的行号。停在 `join()` 里的 main 不是当事人，咱们放过它。
3. 咱们再把环连上：等待的关系看帧，持有的关系看源码的行号或 `__owner` 字段，四条边闭成了环，死锁也就定案了。

## Coffman 四条件：挑一条下手破

现场抓完了，咱们回头找病根。[data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)的结尾背过 Coffman 四条件的配方，也就是互斥、持有并等待、不可剥夺、循环等待这四样的组合。四样凑齐了，死锁也就成立了，所以咱们只要破掉一样，死锁就成不了了。当时咱们只背了配方，而怎么把四样里的某一样破掉，就是本篇下半场的活儿。

咱们把 35 行的现场逐条对上：

- `互斥`：A、B 都是一次只让一个线程进门的锁。
- `持有并等待`：t1 拿着 A 的同时伸手等 B，等的时候它也不撒手。
- `不可剥夺`：谁也没法从 t1 的手里把 A 抢走，而别人想拿的话，只能等它的主人自己放。
- `循环等待`：t1 等 t2 手里的 B，t2 等 t1 手里的 A，等待的关系闭成了环。

咱们看四条里的取舍：互斥与不可剥夺是锁存在的意义，破掉它们就等于不用锁了（确实存在干脆不用锁的设计，而那归 [ch03 的 atomic 篇](../ch03-atomic-memory-model/02-atomics-and-happens-before.md)的篇幅去讲）。工程上的防线，几乎都打在后两条上：总锁序让等待的关系成不了环，而 `std::lock` 用一条语句拿全，僵持也就形不成了。`try_lock` 的回退则让线程压根不抱着锁去堵。而层级锁的做法更狠，它把循环等待从生产环境的挂死，直接变成开发期的异常了。

data race 那篇的结尾里，咱们还认过两张近亲的脸，这里把欠着的深讲补上。活锁的形状是大家都在动、都在让，而活儿就是没干。饥饿的形状是调度总也轮不到某个线程，它想拿锁的时候总被插队。它们跟死锁的病理不一样：死锁的时候大家全睡了，活锁的时候大家全在空转，而饥饿里有人一直被冷落。饥饿的治理方向是把公平性做进锁的语义：有的实现提供按到达次序放行的公平锁，读写锁也有不冷落写者的放行策略，而标准把选择留给了实现，工具箱那篇讲 shared_mutex 时咱们还会遇到写者被饿着的实例。防线三的手法会把活锁招出来，咱们到防线三再细说。

## 防线一：全工程统一的锁序

头一条防线不靠新的 API，咱们靠纪律：全工程给所有的锁定一把统一的次序，拿锁的时候谁都按它来。改动的幅度小得吓人，把 thread2 的两行换个个儿就行：

```cpp
// 防线一：总锁序——两个线程都按 A、B 的顺序拿
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

std::mutex mtx_a;
std::mutex mtx_b;

void worker(int id)
{
    std::lock_guard<std::mutex> a(mtx_a);   // 谁都先拿 A
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> b(mtx_b);   // 再拿 B
    std::cout << "worker " << id << ": 两把都到手\n";
}

int main()
{
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();
    std::cout << "程序正常收尾\n";
    return 0;
}
```

```text
worker 1: 两把都到手
worker 2: 两把都到手
程序正常收尾
```

为什么咱们这么一改就断根了？因为统一的次序是传递的。全工程都按 A 在 B 之前的次序拿锁，那么一个线程等另一把的时候，被等的锁在尺子上一定更靠后。所以等待的关系只能从低处指向高处，排成的链再长也成不了环。

难的地方不在规则，难的是尺子怎么定。正道的做法是按语义分层：模块有上下级的层级，概念有主从的归属，锁的次序跟着结构走，然后把定下的次序写进注释与文档，评审的人看得见。咱们还会见到一路野路子，是拿锁对象的地址比大小定次序，地址在一次运行里是不重样的，确实能自洽地跑起来，可两把锁的高低跟着内存的分配走，读代码的人看不出道理，也不敢重构了，`scoped_lock` 出场之后它的用场就越来越小了。

总锁序的软肋咱们也想得到：它是全工程的纪律，一处例外就够把环重新接上了，而例外往往藏在看不见的地方。这就引出了下一条防线要处理的问题。

## 防线二：std::lock 与 scoped_lock，一条语句拿全

mutex 那篇演示 Account 划转的时候用过 `scoped_lock`，当时咱们只说了一句它对死锁免疫，而为什么免疫的道理压着没讲，现在咱们把它补上。规范的原话分两层：头一句承诺的是 `Locks the given Lockable objects lock1, lock2, ..., lockn using a deadlock avoidance algorithm to avoid deadlock.`，只说了会用死锁避免算法，没说怎么实现的。第二句讲的才是机制：`The objects are locked by an unspecified series of calls to lock, try_lock, and unlock.`

咱们把它翻译过来，就是试探着拿的路数：实现可以一把一把地试探，哪一把一时拿不到的时候，就把已经到手的那些放掉再从头来。您品一下这个失败路径：线程任何时刻都不会处于拿着一把、堵死等另一把的状态，持有并等待的僵持也就形不成了。规范连异常都照顾到了：`If a call to lock or unlock results in an exception, unlock is called for any locked objects before rethrowing.`——拿一半炸了也会放干净，不留半把锁在手里了。

```cpp
// 防线二：scoped_lock 一次拿两把——传参顺序相反也免疫
#include <iostream>
#include <mutex>
#include <thread>

struct Account {
    long balance = 1000;
    mutable std::mutex m;
};

void transfer(Account& from, Account& to, long amount)
{
    std::scoped_lock lk(from.m, to.m);   // 一条语句，死锁避免算法接管
    from.balance -= amount;
    to.balance += amount;
}

int main()
{
    Account a;
    Account b;
    std::thread t1([&] {
        for (int i = 0; i != 20000; ++i) {
            transfer(a, b, 1);   // 传参顺序 a, b
        }
    });
    std::thread t2([&] {
        for (int i = 0; i != 20000; ++i) {
            transfer(b, a, 1);   // 传参顺序 b, a：顺序反了也不僵
        }
    });
    t1.join();
    t2.join();
    std::cout << "总额 = " << a.balance + b.balance << "（期望 2000）\n";
    return 0;
}
```

```text
总额 = 2000（期望 2000）
```

两个线程各划转了两万次，传参的顺序一顺一反，总额分毫不差地守住了。咱们看僵持形不成的道理：在拿不到第二把的时候，`std::lock` 的算法把第一把也放掉了，抱着锁僵等的僵局也就没了。

免疫的边界咱们必须交代清楚，mutex 那篇移交问题的时候强调过一句，免疫只覆盖同一条语句里的多把锁。本篇开头的死锁现场正是活生生的反例：t1 在 11 行拿了 A，中间睡了 50 毫秒，拿 B 的是另一条语句，两条 `lock_guard` 各自为政地活着，`std::lock` 想帮也帮不上了。两个函数各拿一把自己的锁再互相调用，同样在边界的外面，因为 `std::lock` 看不见函数背后的锁。Core Guidelines 的 CP.21 把推荐写成了标题：`Use std::lock() or std::scoped_lock to acquire multiple mutexes`。

## 防线三：try_lock 回退，摸不到就撒手

第三条防线把 `std::lock` 内部的试探逻辑写在明面上，咱们手工来一遍：头一把咱们照常拿，第二把改用试探的 `try_lock()`，摸不到的话，咱们就把头一把也放掉，空着手回去重试了。

```cpp
// 防线三：try_lock 回退——第二把摸不到就全放，退避重试
#include <iostream>
#include <mutex>
#include <thread>

std::mutex mtx_a;
std::mutex mtx_b;

void worker(int id, int rounds)
{
    for (int round = 0; round != rounds; ++round) {
        while (true) {
            std::unique_lock<std::mutex> a(mtx_a, std::defer_lock);
            if (!a.try_lock()) {
                std::this_thread::yield();   // 头一把就没摸到，让一让再来
                continue;
            }
            std::unique_lock<std::mutex> b(mtx_b, std::defer_lock);
            if (b.try_lock()) {
                break;   // 两把都在手：进临界区
            }
            std::this_thread::yield();   // 第二把没摸到：a 随析构放掉，空手回去
        }
        // ……临界区干活：a、b 一直看管到本轮结束……
    }
    std::cout << "worker " << id << " done\n";
}

int main()
{
    std::thread t1(worker, 1, 100000);
    std::thread t2(worker, 2, 100000);
    t1.join();
    t2.join();
    std::cout << "程序正常收尾\n";
    return 0;
}
```

```text
worker 2 done
worker 1 done
程序正常收尾
```

咱们看 `worker` 里的 while 循环：第二把的 `try_lock()` 失败了就退出本轮，`a` 也随着析构放掉了，再用 `yield()` 把处理器客气地让一让，下一轮的尝试再从 A 摸起。线程从不会在持有一把的情况下去堵等另一把，僵持也就形不成了，跟 `std::lock` 的道理殊途同归，只是这回逻辑全在咱们眼皮底下。

活锁的风险也藏在这里：当两个线程都讲礼貌的时候，您想象两边同时摸到头一把、又同时摸不到第二把，然后同时放掉、同时重试的情形，CPU 的占用烧得挺欢，而活儿一件没干。对策是让重试错开：退避的间隔加一点随机抖动，或者两个线程用不同的让步节奏。核心的思想，是别让两个讲礼貌的线程踩着同一个节拍走。

mutex 那篇讲 `timed_mutex` 的时候演示过 `try_lock_for` 的时限拿法，当时留了一句宁可放弃也不僵等的话。现在您能看全它的来龙去脉了：`try_lock` 摸不到的时候撒手，`try_lock_for` 到点没等到的也撒手，回退重试的套路也是同一招。标准库里还有个叫 `std::try_lock` 的自由函数，它能一口气试探的锁可以有多把，哪把失败了就全放掉，还会报出失败那把的下标，回退的循环可以直接拿它当积木。

## 锁内别喊来历不明的代码

mutex 那篇讲守卫的时候，咱们留过一句话：来历不明的那类代码别搁在锁内调用。现在咱们能看清这么说的道理了。您在锁里调用的回调、虚函数、函数对象，背后可能就藏着一次拿锁的动作，而那把锁的主人，说不定正等您手里这把，环就藏在调用链的深处，您的代码里一行都看不见。而更麻烦的地方在于它会长大：今天的回调不拿锁，半年后有人改了实现，环就悄悄接上了。

Core Guidelines 把它写成了 CP.22，标题的措辞非常直白：`Never call unknown code while holding a lock (e.g., a callback)`（持锁时永远别调来历不明的代码，比如一个传进来的回调）。修法的思路也直白：锁内只拷贝需要的数据，把锁放了，再交给锁外的调用去做。您把调用挪出锁外，看不见的拿锁就与您的锁碰不上面，环想接都没处接了。

还有一层要说清的：锁序纪律的前提，是您得看得见所有拿锁的路径。看不见的部分里，防线一根本没处下脚了，而 CP.22 的修法正好把看不见的部分关在门外。

## 层级锁：把违规变成当场异常

[thread_local 那篇](./02-thread-local.md)里给咱们留过一个钩子，层级锁要来借的就是每线程一份的状态，现在就轮到它出场了。层级锁的思路是把防线的纪律变成运行时的检查：咱们给每把锁标一个层级数，而线程每拿一把，就把自己当前的层级记进一份 thread_local 的状态里。拿锁的规则只有一条，新锁的层级必须比手里已有的更低，加锁的路线一路下行。越级的那个线程会当场吃到异常，死锁从生产环境的挂死里，变成了开发期的报错栈。

```cpp
// 层级锁：thread_local 记录本线程当前层级，越级上锁抛异常
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

class HierarchicalMutex {
public:
    explicit HierarchicalMutex(unsigned long level)
        : level_(level)
    {
    }

    void lock()
    {
        check_violation();
        internal_.lock();
        push_level();
    }

    void unlock()
    {
        current_level_ = previous_level_;   // 回到拿下这把锁之前的层级
        internal_.unlock();
    }

    bool try_lock()
    {
        check_violation();
        if (!internal_.try_lock()) {
            return false;
        }
        push_level();
        return true;
    }

private:
    void check_violation()
    {
        if (level_ >= current_level_) {
            throw std::logic_error("锁层级越级：当前线程已持有同级或更低层级的锁");
        }
    }

    void push_level()
    {
        previous_level_ = current_level_;
        current_level_ = level_;
    }

    std::mutex internal_;
    const unsigned long level_;
    unsigned long previous_level_ = 0;
    static thread_local unsigned long current_level_;
};

thread_local unsigned long HierarchicalMutex::current_level_
    = std::numeric_limits<unsigned long>::max();
```

咱们把三把锁按层级摆好，用法与实测的输出如下：

```cpp
HierarchicalMutex high_mutex(10000);   // 应用层
HierarchicalMutex mid_mutex(5000);     // 业务层
HierarchicalMutex low_mutex(100);      // 底层 IO

// 合法路径：高 -> 中 -> 低，一路下行
{
    std::lock_guard<HierarchicalMutex> h(high_mutex);
    std::lock_guard<HierarchicalMutex> m(mid_mutex);
    std::lock_guard<HierarchicalMutex> l(low_mutex);
    std::cout << "下行加锁：一路顺利\n";
}

// 越级路径：在低层里回头够中层，当场抛异常
try {
    std::lock_guard<HierarchicalMutex> l(low_mutex);
    std::lock_guard<HierarchicalMutex> m(mid_mutex);   // 5000 >= 100：违规
} catch (const std::logic_error& e) {
    std::cout << "抓到越级: " << e.what() << '\n';
}

// 层级状态是 thread_local 的：上一个线程的状态不会漏到别的线程
std::thread fresh([] {
    std::lock_guard<HierarchicalMutex> m(mid_mutex);   // 新线程直接拿中层：合法
    std::cout << "新线程直接拿中层：合法\n";
});
fresh.join();
```

```text
下行加锁：一路顺利
抓到越级: 锁层级越级：当前线程已持有同级或更低层级的锁
新线程直接拿中层：合法
```

输出的判读，咱们挨个来。下行加锁的一路顺利，因为 10000、5000、100 是严格递减的。在 `low_mutex` 的怀抱里回头够 `mid_mutex`，5000 不低于当前的层级 100，违规就成立了，异常也就当场抛出来了。而新线程直接拿中层是合法的，因为层级状态是 thread_local 的：fresh 线程的 `current_level_` 从 `ULONG_MAX` 起步，上一个线程的层级历史不会漏过来，这正是 [thread_local 那篇](./02-thread-local.md) 讲的每线程一份在起作用。

实现里有两个细节值得咱们多看一眼。`unlock` 恢复的是 `previous_level_` 而不是清零，也就是说解锁要按拿锁的逆序来，好在 RAII 守卫的析构天生就是逆序的，所以您用 `lock_guard` 包它就是绝配。`check_violation` 用的是大于等于：同级也算违规的，两把同层级的锁想互等也成不了。代价是每次 lock/unlock 多几次整型的读写与比较，换的是把锁序违规从线上事故提前到开发期的异常栈。

层级锁管得住的，是同一套锁序约束得到的锁。跨模块的锁，咱们还得靠总锁序与评审来兜底。而它跟总锁序正好是一对搭档：总锁序的次序写在文档里，层级锁的次序跑在代码里。

## TSan 与 Helgrind：死锁该找谁

看到这里您可能会想，TSan 不是抓并发的神器吗，而死锁归不归它管？咱们做实验说话：咱们把 deadlock.cpp 用 `-fsanitize=thread` 重编一份去跑，三秒后它就被 `timeout` 掐死了。

```text
t1: 拿到 A，伸手等 B
t2: 拿到 B，伸手等 A
```

它拿到的退出码是 124，而 TSan 从头到尾一个字都没说。您别急着给 TSan 定罪，咱们把它分成两半看。管内存竞争的那一半，[data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md) 讲过它的原理：纯 happens-before 的检测器，靠向量时钟判断两次访问之间有没有同步的关系。它盯的是内存访问的次序，而死锁的两方安安静静地各等各的，谁也没碰谁的内存，所以没有可报的违例。另一半是死锁侦测的锁序图组件，GCC 与 LLVM 的 TSan 都默认开启，由 TSAN_OPTIONS 的 `detect_deadlocks` 开关管着。ch01 的 sanitizer 对比表把 TSan 的这一半记作“锁序反转（预警）”，详讲指名记在了 ch02，指的就是它了，咱们在这里把详讲补上。

锁序图记的是已经完成的获取：线程拿着 M0、成功拿到了 M1，图里就多一条 M0 指向 M1 的边。有向图里一旦出现了环，它就报出 `lock-order-inversion (potential deadlock)` 的预警。咱们造一个单线程的反序现场，让两段获取都完整地走完，看它的锁序图会不会报预警：

```cpp
// 单线程反序拿锁，两段都完整走完：看 TSan 的锁序图会不会报预警
#include <cstdio>
#include <mutex>

std::mutex A;
std::mutex B;

int main()
{
    {
        std::lock_guard<std::mutex> a(A);
        std::lock_guard<std::mutex> b(B);   // 拿着 A 拿 B：A -> B
    }
    {
        std::lock_guard<std::mutex> b(B);
        std::lock_guard<std::mutex> a(A);   // 拿着 B 拿 A：环成了
    }
    std::printf("done\n");
    return 0;
}
```

咱们照旧带上 `-fsanitize=thread` 编译，跑出来的真实输出如下（十六进制地址、pid 每次都会变）：

```text
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock) (pid=1134751)
  Cycle in lock order graph: M0 (0x5555555580a0) => M1 (0x5555555580e0) => M0

  Mutex M1 acquired here while holding mutex M0 in main thread:
    #0 pthread_mutex_lock <null> (libtsan.so.2+0x60bb1) ……
    #4 main /tmp/ch0203/lockdep_probe.cpp:12 ……

  Mutex M0 acquired here while holding mutex M1 in main thread:
    #0 pthread_mutex_lock <null> (libtsan.so.2+0x60bb1) ……
    #4 main /tmp/ch0203/lockdep_probe.cpp:16 ……
SUMMARY: ThreadSanitizer: lock-order-inversion (potential deadlock) /tmp/ch0203/lockdep_probe.cpp:12 in main
```

省略的 #1–#3 帧是 `gthr-default.h`、`std_mutex.h` 的机制帧，跟上面 gdb 会话里见过的相同。咱们看两段栈的指向：12 行与 16 行，正是两次反序获取 `B`、`A` 的地方。环报出来了，TSan 当场就报了预警，程序自己也照常跑完了两段获取、并没有死锁，咱们拿到的退出码是 66。

可咱们的 deadlock.cpp 它一声没吭，这是怎么回事，咱们得说清楚。差别就落在“完整走过”的字面上。预警要的边，是已经完成了的获取。死锁现场里 t1 的第二把永远拿不到，B 指回 A 的边压根没有作为完成事件进过图，环也就闭不上了，预警自然也就无从谈起了。所以口径得这么收：TSan 对已经走过的反序拿锁会预警，对正在发生的真死锁会沉默。正因为 TSan 的沉默，前面 gdb 的三命令才有了不可替代的位置。

死锁的自动侦测，Valgrind 家族的 Helgrind 挑了一部分担子。手册里的定位是：`Helgrind is a Valgrind tool for detecting synchronisation errors in C, C++ and Fortran programs`。它的长处是不用重编译的，拿现成的二进制就能查，而 TSan 得用 `-fsanitize=thread` 重编整个程序，它就没有这个门槛了。手册列出的检出有三类：`Misuses of the POSIX pthreads API.`、`Potential deadlocks arising from lock ordering problems.`、`Data races -- accessing memory without adequate locking or synchronisation`。咱们拿锁序死锁来对号，它正落在第二类的范围里。

代价的说明在手册里也写得明明白白：`Performance can be very poor. Slowdowns on the order of 100:1 are not unusual.`，慢一百倍都不算稀奇的事，所以它更像事后查现场的重量级工具，日常开发咱们还是靠 TSan 常态化跑。手册里的建议还有一条：`make your application Memcheck-clean before using Helgrind`，把 Memcheck 这一关过了再来，原话还说了 `Memcheck and Helgrind are to some extent complementary`，它们本来就是互补的搭配。

笔者的本机没装 valgrind，Helgrind 对着这个死锁现场的真实报告咱们还欠着，等维护者的机器装好它再补，报告的判读以上面三类检出为纲。

<!-- 实验回填：Helgrind 真实输出占位。本机（Arch/WSL2）无 valgrind 且无免密
     sudo，无法冒烟。维护者机执行 `sudo pacman -S valgrind` 安装后运行
     `valgrind --tool=helgrind ./deadlock`，把真实报告贴到上一段之后，并据实
     修正本段的预期措辞。 -->

咱们把分工收个尾：内存访问的竞争，TSan 管日常的活。死锁的现场，gdb 的三命令管判读。不想重编译就要查锁序的时候，就轮到 Helgrind 上场了。

## 练习：轮到您动手了

判读现场的手感是练出来的，咱们备了四道，前两道练的是判读，后两道练的是动手改。

1. 您把 deadlock.cpp 原样跑挂，用本篇的三命令把现场判读一遍，四条边挨个地对上号，把您自己的会话留档。然后任选一条防线把它修活了再重跑验证。
2. 三个线程配三把锁的场面：thread1 按 A、B 的顺序拿，thread2 按 B、C 的顺序拿，thread3 按 C、A 的顺序拿。您笔判一下会不会死锁，把 `info threads` 与 `thread apply all bt` 的预期输出写下来，然后您上机对答案，看看环在栈上的样子跟您想的一样不一样。
3. 您自己实现一遍 HierarchicalMutex，验证的两件事：合法的下行不抛异常，越级的当场抛。然后您再回答一问：两个线程各按自己方便的顺序去拿两把层级锁的场合，还能死锁吗？提示：环要闭上的话，总得有某个线程回头去拿更高层级的锁，而那一把会当场炸掉。
4. 五位哲学家围坐的场景：相邻的两人共享一根筷子。朴素写法人人拿了左手边的，再伸手够右手边的那根，稳稳地死锁。您挑两条防线修它，说明各自打破的是 Coffman 的哪一条。

## 下一步

锁序的纪律立稳了，同步的另一半是等待与通知，谓词等待的正源在 [condition_variable 与阻塞队列](./04-condition-variable-and-bounded-queue.md)，咱们本篇 try_lock 回退的写法算是给它热了身。而 call_once、semaphore、latch、barrier 一并住在 [同步原语工具箱](./05-sync-primitives-toolkit.md)，ch00 讲到一半的 `优先级反转` 与 futex 的深讲，也都记在它的名下了。动手量更大的活儿，在 [exercises 的阻塞队列 Lab](../exercises/01-bounded-queue) 里等着您。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 参考资源

- [Debugging with GDB: Threads -- sourceware](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Threads)
- [std::lock -- cppreference](https://en.cppreference.com/w/cpp/thread/lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [std::try_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/try_lock)
- [CP.21: Use std::lock() or std::scoped_lock to acquire multiple mutexes -- C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp21-use-stdlock-or-stdscoped_lock-to-acquire-multiple-mutexes)
- [CP.22: Never call unknown code while holding a lock (e.g., a callback) -- C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp22-never-call-unknown-code-while-holding-a-lock-eg-a-callback)
- [Helgrind: a thread error detector -- valgrind.org](https://valgrind.org/docs/manual/hg-manual.html)
- [ThreadSanitizerCppManual -- google/sanitizers wiki](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual)
- [Coffman, Elphick, Shoshani: System Deadlocks, ACM Computing Surveys, 1971](https://doi.org/10.1145/356586.356588)
- [Williams, *C++ Concurrency in Action*, 2nd ed, Manning, 2019](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
