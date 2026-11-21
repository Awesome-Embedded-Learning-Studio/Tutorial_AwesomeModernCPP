---
title: "丢掉的数怎么收回来？——std::mutex 与三种作用域锁"
description: "把上一篇丢掉的计数收进锁里，讲清 std::mutex 与三种作用域锁的用法与选择。"
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
---

# 丢掉的数怎么收回来？——std::mutex 与三种作用域锁

上一篇跑过一个会少算的计数器，您还记得吗？两条线程各加 160000 次，`-O0` 下 30 轮里错了 6 轮。咱们当时约好，本篇给它配一把锁，把丢掉的加一都收回来。

## 给计数器配一把锁

给计数器配锁，咱们只加两样东西：一把 `std::mutex`，还有进入临界区时构造的 `std::lock_guard`。临界区，也就是一次只让一条线程进来的代码。

<OnlineCompilerDemo
  title="给上一篇丢数的计数器配一把锁"
  source-path="code/examples/vol5/chapter02/06_demo_mutex_counter.cpp"
  description="一把 mutex 配 lock_guard：30 轮一次没丢，全部 320000。锁让读、加、写回在临界区里连成不被打断的一串。"
  run-options="-O0 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_mutex_counter.cpp —— 给上一篇丢数的计数器配一把锁
#include <cstdio>
#include <mutex>
#include <thread>

long long counter = 0;
std::mutex counter_mutex;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    std::lock_guard<std::mutex> lock(counter_mutex);
    ++counter;
  }
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  for (int t = 0; t < kTrials; ++t) {
    counter = 0;
    std::thread a(work, kRounds);
    std::thread b(work, kRounds);
    a.join();
    b.join();
    if (counter == 2LL * kRounds) ++correct;
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
```

</OnlineCompilerDemo>

咱们跑出来：

```text
30 of 30 trials printed the expected 320000
```

上一篇同一份逻辑 30 轮只对了 24 轮，丢掉的数从两百多到十五万。笔者在 `-O0` 下重跑了一回，17 轮对，落在上一篇说的 14 到 27 的浮动区间里。同一档 `-O0` 下换上锁，30 轮一次没丢。

同一份无锁代码在 `-O2` 下是 30/30，笔者重跑也是。优化级别一换，编译器把 160000 次读改写折成几条指令，重跑的 30 轮里没再出现丢数。无锁仍是 data race，不能靠 `-O2` 修。拿 `-O2` 的修复版去对 `-O0` 的无锁版，两边的优化级别不同，读数差在编译选项上。所以计数对照统一在 `-O0` 下做。

`std::mutex` 在 `<mutex>` 里，C++11 起咱们就能用。它提供 `lock()`、`try_lock()`、`unlock()` 三个操作，标准给它的定位是独占所有权的非递归互斥量（[thread.mutex.class]，C++11 起）。一条线程拿着它，别的线程调 `lock()` 就阻塞，调 `try_lock()` 就立刻返回 false，直到持有者 `unlock()`。

前置条件有两条，您得对一遍：`lock()` 的前置条件是调用线程此刻不拥有这把锁，`unlock()` 的前置条件是调用线程拥有它（[thread.mutex.requirements.mutex]/6、/21，C++11 起）。同一条线程对 `std::mutex` 连着锁两次，前置条件不成立，行为未定义，卡住只是常见表现。

需要递归加锁的场合，咱们可以在 mutex 家族里找到 `std::recursive_mutex`，它的用法计划收在 cookbook（卷五的配方选读册）的配方里，本篇不铺。

锁能修好丢数，靠的是一次 `unlock()` 与随后成功拿到这把锁的 `lock()` 之间的 synchronizes-with 关系：白话讲，前一次放锁之前写下的东西，对后一次拿锁的线程可见。它是 happens-before 的一种来源（[thread.mutex.requirements.mutex]/8、/24）。咱们用锁把读、加、写回串起来：线程 A 在临界区里写完 `counter`，走出作用域解锁，线程 B 拿到锁的时候，A 的写入已经 happens-before 它的读取。

您这里容易碰上的岔路，是以为加了锁就万事大吉。锁本身不认识数据，它只保证同一时刻只有一条线程进得来。程序要正确，得所有碰 `counter` 的线程都同意走同一个入口，谁绕过去，锁管不着。后面讲锁保护哪块数据的时候，咱们会看到绕过去长什么样。

## 手动 lock 和 unlock 为什么不能用

接手别人的代码时，您最容易读到的就是两行直白调用：

<OnlineCompilerDemo
  title="手动 lock/unlock：异常一抛，锁留在原地"
  source-path="code/examples/vol5/chapter02/07_demo_manual_lock_ub.cpp"
  description="工作线程在临界区里抛异常并捕获退出，锁还挂在它名下，主线程随后一直等（在线运行有超时收场，本地得 Ctrl-C）。线程退出时仍持有互斥量是未定义行为。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_manual_lock_ub.cpp —— 手动 lock/unlock：异常一抛，锁留在原地
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

std::mutex gate;

void work() {
  gate.lock();
  throw std::runtime_error("boom");
  gate.unlock();
}

int main() {
  std::thread t([] {
    try {
      work();
    } catch (const std::exception& e) {
      std::printf("[worker] caught %s, the lock is still held\n", e.what());
    }
  });
  t.join();

  std::printf("[main] asking for the same lock\n");
  gate.lock();   // 工作线程退出时仍持有锁：未定义行为，这里是一直等
  std::printf("[main] got it\n");
  gate.unlock();
  return 0;
}
```

</OnlineCompilerDemo>

这里笔者故意让异常在解锁之前抛出来。咱们让工作线程捕获它，主线程随后去拿同一把锁，跑起来是这样（`timeout 2` 收尾）：

```text
[worker] caught boom, the lock is still held
[main] asking for the same lock
exit=124
```

> 有趣的是，笔者的LLM指出：这个程序的输出接进管道时，`stdout` 的缓冲会跟着被杀掉的进程一起丢。您用 `stdbuf -oL` 跑，或者直接在终端里跑，才看得到 worker 和 main 的两行字。笔者不计划暂时为这个承诺负责。

主线程停在 `[main] asking` 之后，两秒到了被 `timeout` 杀掉，`[main] got it` 始终没有出现。工作线程自己退出了，锁还挂在它名下。标准把“线程退出时仍持有互斥量”写成未定义行为（[thread.mutex.class]/5），咱们看到的正是其中一种：主线程一直等。换个实现，换次调度，也可能是别的样子。

多个返回路径更麻烦。您看，临界区里每多一个提前返回的 `return`，就多一处需要记得 `unlock()` 的地方。异常路径、提前返回、后面有人往里加一行 `throw`，都能让锁留在原地。这类错误不会在编译时冒出来，也不一定每次运行都出现。

RAII 的做法是把锁的生命周期绑给作用域。`std::lock_guard` 是 C++11 起的最轻包装：构造函数调用 `m.lock()`，析构函数调用 `m.unlock()`（[thread.lock.guard]/2、/6）。拷贝构造被删除，也没有移动接口，标准把两条都写在类声明里（同一条款）。它没有 `unlock()` 成员，您手里的锁一旦构造出来，只能等作用域结束。

同一段代码，咱们换成 `lock_guard`：

<OnlineCompilerDemo
  title="换成 lock_guard：异常路上就把锁放了"
  source-path="code/examples/vol5/chapter02/08_demo_lock_guard_raii.cpp"
  description="同一个场景，锁的生命周期绑给作用域：栈展开带着析构走，锁在异常离开作用域的路上放掉，主线程随后拿到锁打印 got it。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_lock_guard_raii.cpp —— 换成 lock_guard：异常路上就把锁放了
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

std::mutex gate;

void work() {
  std::lock_guard<std::mutex> lock(gate);
  throw std::runtime_error("boom");
}

int main() {
  std::thread t([] {
    try {
      work();
    } catch (const std::exception& e) {
      std::printf("[worker] caught %s\n", e.what());
    }
  });
  t.join();

  std::printf("[main] asking for the same lock\n");
  gate.lock();   // 构造时拿锁、析构时放锁：锁在异常离开作用域的路上就放掉了
  std::printf("[main] got it\n");
  gate.unlock();
  return 0;
}
```

</OnlineCompilerDemo>

```text
[worker] caught boom
[main] asking for the same lock
[main] got it
```

异常从 `work()` 里穿出去，栈展开带着 `lock` 的析构一起走，锁在异常离开作用域的路上就放掉了。主线程随后拿到锁，打印 `got it`。咱们不需要在每条返回路径上补一句解锁，作用域结束就是解锁点。

有一个写法，笔者得单独说，因为它看起来像用了 RAII：

<OnlineCompilerDemo
  title="没有名字的 lock_guard：看起来像用了 RAII，其实没有"
  source-path="code/examples/vol5/chapter02/09_demo_unnamed_guard.cpp"
  description="临时对象语句结束即析构，锁在 ++counter 之前就放掉了：30 轮一轮都没对。本地加 -Wall 会看到 nodiscard 警告，正是正文讲的警告。"
  run-options="-O0 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_unnamed_guard.cpp —— 没有名字的 lock_guard 临时对象：锁在加一之前就放了
#include <cstdio>
#include <mutex>
#include <thread>

long long counter = 0;
std::mutex counter_mutex;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    std::lock_guard<std::mutex>{counter_mutex};   // 临时对象：语句结束即析构
    ++counter;
  }
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  for (int t = 0; t < kTrials; ++t) {
    counter = 0;
    std::thread a(work, kRounds);
    std::thread b(work, kRounds);
    a.join();
    b.join();
    if (counter == 2LL * kRounds) ++correct;
    else std::printf("trial %d: printed %lld, lost %lld\n",
                     t, counter, 2LL * kRounds - counter);
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
```

</OnlineCompilerDemo>

花括号里构造的是一个没有名字的临时对象。语句结束，临时对象析构，锁在 `++counter` 之前就放掉了。您把上一篇的实验换成没有名字的写法，在 `-O0` 下 30 轮跑出来：

```text
trial 0: printed 313868, lost 6132
trial 1: printed 313778, lost 6222
trial 2: printed 314428, lost 5572
0 of 30 trials printed the expected 320000
```

咱们一轮都没对。GCC 16.2.1 的 libstdc++ 在锁守卫的构造函数上标了 `[[nodiscard]]`，它默认给一条 `-Wunused-result` 警告，说构造函数的返回值被忽略了。

警告是编译器帮忙，别的实现不一定给。名字得您自己给，写成 `std::lock_guard<std::mutex> lock(counter_mutex)`，变量名不能省。（这个写法笔者自己也顺手写过，是那条警告把笔者救回来的。）

C++ Core Guidelines 的 CP.20 就是这么定的：用 RAII，别直接调 `lock()` 和 `unlock()`。有一个例外咱们得知道，是 `adopt_lock`，它接管的是一把已经被 `std::lock()` 或类似算法拿到的锁，只负责析构时释放。加锁那一步仍然交给库函数，不落在应用代码手里。

## 锁要挪动、要提前放、要陪等待怎么办：unique_lock

需要挪动、提前放锁、陪条件变量等待的时候，您才用得上 `std::unique_lock`（C++11 起，`<mutex>`）。`lock_guard` 够用的场合，锁留在作用域里就好。

它比 `lock_guard` 多出一个所有权状态。咱们构造它时可以照常加锁，也可以带 `std::defer_lock` 暂不加、等到需要时再 `lock()`，带 `std::try_to_lock` 试一次，或者带 `std::adopt_lock` 接管一把已经拿到的锁。它提供 `owns_lock()`，用来查询当前是否持锁，提供 `unlock()` 提前放锁，还能移动：锁的所有权可以从一个函数交到另一个函数手里，`lock_guard` 做不到。

提前解锁能省多少等待，咱们量一次。代码里 holder 线程拿锁、拷一份十万个整数的快照，然后做一件耗时的事。两种做法：一直拿着锁做完，或者拷完就放、出了锁再做。主线程在 5 毫秒后去拿同一把锁，量它等了多久：

<OnlineCompilerDemo
  title="拷完快照就放锁：主线程少等 45 毫秒"
  source-path="code/examples/vol5/chapter02/10_demo_unlock_early.cpp"
  description="holder 线程拷一份十万个整数的快照再做 50 毫秒的耗时活：一直持锁主线程等 45 毫秒，unique_lock 拷完就 unlock 主线程立刻进去。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_unlock_early.cpp —— 拷完快照就放锁：主线程少等 45 毫秒
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;
using namespace std::chrono_literals;

long long run(bool unlock_after_copy) {
  std::mutex m;
  std::vector<int> shared(100000, 1);
  long long waited = -1;

  std::thread holder([&] {
    std::unique_lock<std::mutex> lock(m);
    auto snapshot = shared;
    if (unlock_after_copy) lock.unlock();
    std::this_thread::sleep_for(50ms);   // 顶替耗时的活
    long long sink = 0;
    for (int v : snapshot) sink += v;
    if (sink < 0) std::puts("unreachable");
  });

  std::this_thread::sleep_for(5ms);
  auto t0 = clock_type::now();
  {
    std::lock_guard<std::mutex> lock(m);
    waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                 clock_type::now() - t0).count();
  }
  holder.join();
  return waited;
}

int main() {
  std::printf("lock held through the work: waited %lld ms\n", run(false));
  std::printf("unlock after the copy:     waited %lld ms\n", run(true));
  return 0;
}
```

</OnlineCompilerDemo>

```text
lock held through the work: waited 45 ms
unlock after the copy:     waited 0 ms
```

输出里，holder 线程一直拿着锁，主线程等了 45 毫秒，它比 holder 晚 5 毫秒起跑。咱们换到拷完就放的做法，主线程几乎立刻就进去了。50 毫秒的 `sleep_for` 只是替身，现实里换成排序、格式化、写文件、发网络请求都一样：这些东西不需要锁，却能让别人等着。

拷贝本身也要付代价。快照占内存，拷贝需要时间，数据很大时这笔开销不能不算。您选哪边，看两件事的相对长度：拷贝比等待便宜，就拷出来。数据大到拷不动，那就得换个设计，不能硬把锁拿在手里。

`unique_lock` 的额外状态也有一点开销，通常是一个指针加一个布尔量，咱们把它用在需要的地方就行。手里只有一段作用域，`lock_guard` 更省心，它连“忘记自己有没有持锁”这件事都不给您。

条件变量那边，`std::condition_variable::wait` 要的正是 `std::unique_lock<std::mutex>&`，等待期间它要替咱们解锁、醒来再加锁，`lock_guard` 没有这样的能力。`std::unique_lock` 最常见的用处是提前放锁和陪条件变量等待。锁的所有权转交，留给更复杂的接口。第三单元《任务之间怎样等待和协调》已经写好，讲等待与协调的工具，入口是[《等一个条件：从轮询到条件变量》](../chapter03-wait-and-coordinate/01-wait-for-a-condition)。

把示例里的 `sleep_for` 换成您手里真会做的耗时活，您再量一次，等待会变成多少。

## 几把锁一趟拿全行不行：scoped_lock

手上同时有两把锁的时候，您嵌套 `lock_guard` 就会重演上一篇里加锁顺序相反的死锁。两个对象，各有一把锁和一个数值，两边都要动两个对象，加锁顺序相反：

<OnlineCompilerDemo
  title="两把锁嵌套着拿：死锁重现"
  source-path="code/examples/vol5/chapter02/11_demo_two_locks_deadlock.cpp"
  description="两条线程各拿一边的锁再要对方那把，两行 holding one side 之后进程停住（在线运行有超时收场）。上一篇的死锁换了件外衣又来了。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_two_locks_deadlock.cpp —— 各拿一把再嵌套要对方那把：死锁重现
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Item {
  std::mutex m;
  long long value = 1000000;
};

void move(Item& from, Item& to, long long amount) {
  std::lock_guard<std::mutex> a(from.m);
  std::printf("holding one side\n");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> b(to.m);
  from.value -= amount;
  to.value += amount;
}

int main() {
  Item x, y;
  std::thread t1([&] { move(x, y, 1); });
  std::thread t2([&] { move(y, x, 1); });
  t1.join();
  t2.join();
  std::printf("sum = %lld\n", x.value + y.value);
  return 0;
}
```

</OnlineCompilerDemo>

```text
holding one side
holding one side
exit=124
```

这个程序接管道时同样会丢掉缓冲里的输出，您用 `stdbuf -oL`，或者直接在终端里跑。

咱们看到两条线程各拿到一把，各自等对方手里那把，两秒后被杀掉。

把它换成 `std::scoped_lock`（C++17 起），您看，动的只有加锁的一行：

<OnlineCompilerDemo
  title="scoped_lock 一趟拿两把：死锁不再来"
  source-path="code/examples/vol5/chapter02/12_demo_scoped_lock_transfer.cpp"
  description="只改加锁的一行：两条线程各跑 200000 次对向转账，一个方向减 1 另一个方向加 2，总和始终 2000000，一次也没卡住。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_scoped_lock_transfer.cpp —— scoped_lock 一趟拿两把：死锁不再来
#include <cstdio>
#include <mutex>
#include <thread>

struct Item {
  std::mutex m;
  long long value = 1000000;
};

void move(Item& from, Item& to, long long amount) {
  std::scoped_lock lock(from.m, to.m);
  from.value -= amount;
  to.value += amount;
}

int main() {
  Item x, y;
  const int kIters = 200000;
  std::thread t1([&] {
    for (int i = 0; i < kIters; ++i) move(x, y, 1);
  });
  std::thread t2([&] {
    for (int i = 0; i < kIters; ++i) move(y, x, 2);
  });
  t1.join();
  t2.join();
  std::printf("sum = %lld, x = %lld, y = %lld\n",
              x.value + y.value, x.value, y.value);
  return 0;
}
```

</OnlineCompilerDemo>

咱们让两条线程各跑 200000 次，一个方向减 1，另一个方向加 2：

```text
sum = 2000000, x = 1200000, y = 800000
```

两个数加起来，始终是 2000000。这个写法咱们跑了 40 轮，一轮都没卡住，也没少一分钱。

咱们看 `scoped_lock` 的构造函数：接过一把以上的锁时，它调用的是 `std::lock(m...)`（[thread.lock.scoped]/2，C++17 起），析构时把拿到的锁逐个释放（同节 /6）。

标准对 `std::lock` 的要求是：它用 `lock()`、`try_lock()`、`unlock()` 的组合去拿全部参数，调用序列不会造成死锁，具体怎么排没有规定（[thread.lock.algorithm]/5）。拿不到就全放，换个次序重来，草案注解里把这类做法叫 try-and-back-off。它保证的只是算法本身，咱们别把已经握在手里的锁递进去。

手里只有一把锁的时候，`scoped_lock` 的构造函数直接调 `m.lock()`，跟 `lock_guard` 等价（[thread.lock.scoped]/2）。读代码时，咱们一眼就能分开：看到 `lock_guard` 知道这里只有一把锁，看到 `scoped_lock` 知道它可能一次拿几把。

咱们把三个工具摆在一起，差别一眼就能对出来：

| 工具               | 能拿几把   | 能提前放 | 能挪走 | 常见场合                           |
| ------------------ | ---------- | -------- | ------ | ---------------------------------- |
| `std::lock_guard`  | 一把       | 不能     | 不能   | 一段作用域里的单锁                 |
| `std::unique_lock` | 一把       | 能       | 能     | 条件变量、提前解锁、锁的所有权转交 |
| `std::scoped_lock` | 一把或多把 | 不能     | 不能   | 两把以上一起拿，避免顺序死锁       |

笔者在表里没有列 `std::recursive_mutex` 和 `std::timed_mutex`。它们是 mutex 家族里的 recursive 和 timed 变体，同样留给 cookbook 的配方。

还有一条边界笔者得交代：`scoped_lock` 不能交给条件变量。`condition_variable::wait` 要的是 `unique_lock`，`scoped_lock` 没有等待期间解锁再上锁的能力。手里同时有多把锁、又要等条件，写法得回到 `unique_lock` 加 `std::lock`。


## 参考资源

笔者写本篇的时候，推进思路借鉴了 Anthony Williams 的《C++ Concurrency in Action》，取的是书中讲共享数据的几章。例子和文字都是笔者自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的草案文本与 cppreference。

- [std::mutex（cppreference）][cppref-mutex]，C++11 起的互斥量。
- [std::lock_guard（cppreference）][cppref-guard]，构造加锁、析构解锁的轻量守卫。
- [std::unique_lock（cppreference）][cppref-unique]，可移动、可提前解锁的锁包装。
- [std::scoped_lock（cppreference）][cppref-scoped]，C++17 起的多锁守卫。
- [std::lock（cppreference）][cppref-lock]，一次拿多把锁的算法。
- [C++ 标准草案：Mutex requirements（thread.mutex.requirements.mutex）][draft-mutex]，锁的语义与同步关系。
- [C++ 标准草案：Class mutex（thread.mutex.class）][draft-mutex-class]，非递归互斥量的定义与所有权规定。
- [C++ 标准草案：Class template lock_guard（thread.lock.guard）][draft-guard]，构造、析构与类声明。
- [C++ 标准草案：Generic locking algorithms（thread.lock.algorithm）][draft-lock]，`std::lock` 与 `try_lock` 的保证。
- [C++ 标准草案：Class template scoped_lock（thread.lock.scoped）][draft-scoped]，构造与析构的行为。
- [C++ 标准草案：thread members（thread.thread.member）][draft-thread-member]，join 与线程结束的同步关系。
- [C++ Core Guidelines：CP.20][cp20]，用 RAII，不要直接调用 `lock()` 和 `unlock()`。

[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[cppref-guard]: https://en.cppreference.com/w/cpp/thread/lock_guard
[cppref-unique]: https://en.cppreference.com/w/cpp/thread/unique_lock
[cppref-scoped]: https://en.cppreference.com/w/cpp/thread/scoped_lock
[cppref-lock]: https://en.cppreference.com/w/cpp/thread/lock
[draft-mutex]: https://eel.is/c++draft/thread.mutex.requirements.mutex
[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-guard]: https://eel.is/c++draft/thread.lock.guard
[draft-lock]: https://eel.is/c++draft/thread.lock.algorithm
[draft-scoped]: https://eel.is/c++draft/thread.lock.scoped
[draft-thread-member]: https://eel.is/c++draft/thread.thread.member
[cp20]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock
