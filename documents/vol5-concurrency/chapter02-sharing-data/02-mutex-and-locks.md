---
title: "用互斥保护共享状态"
description: "把上一篇丢掉的计数收进锁里，讲清 std::mutex 与三种作用域锁的用法与选择，明确每把锁保护哪块数据，讨论锁的粒度，再用 thread_local 与各持一份减少共享。"
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 用互斥保护共享状态

上一篇《从错误程序认识竞争》跑过一个会少算的计数器，您还记得吗？两条线程各加 160000 次，`-O0` 下 30 轮里错了 6 轮。咱们当时约好，这一篇给它配一把锁，把丢掉的加一都收回来。

那一篇里还给咱们留了几件事：锁和它保护的数据怎么对上号，锁的范围多大算合适，减少共享的另一半内容，以及 `std::scoped_lock`。

范围怎么划？死锁这一块，本篇只把 `std::scoped_lock` 交到您手里，用它收掉上一篇里加锁顺序相反的死锁样本。锁顺序和通用的死锁预防，归讲死锁与减少共享的那一篇。检查之后动手、跨数据的不变量、并发接口怎么设计，归讲一次操作边界的那一篇。

实验都在临时目录里编译跑过（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2，16 逻辑核，2026-11-14）。除计数对照一段用 `-O0` 外，其余实验都用 `-O2`。计数对照要和上一篇的读数放在同一档优化级别上，所以咱们单独退到 `-O0`，理由在第一节交代。`std::mutex`、`std::lock_guard`、`std::unique_lock` 和 `thread_local` 是 C++11 起的设施，`std::scoped_lock` 是 C++17 起的。文中的标准行为按 eel.is 上的草案条款与 cppreference 核对过，条款都收在参考资源里。输出都是单次运行的读数，换台机器，毫秒数会动。

## 给计数器配一把锁

给计数器配锁，咱们只加两样东西：一把 `std::mutex`，还有进入临界区时构造的 `std::lock_guard`。临界区，也就是这段只让一条线程进来的代码。

```cpp
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

咱们跑出来：

```text
30 of 30 trials printed the expected 320000
```

上一篇同一份逻辑 30 轮只对了 24 轮，丢掉的数从两百多到十五万。我这次在 `-O0` 下重跑，17 轮对，落在上一篇说的 14 到 27 的浮动区间里。同一档 `-O0` 下换上锁，30 轮一次没丢。

同一份无锁代码在 `-O2` 下是 30/30，我重跑也是。优化级别一换，编译器把 160000 次读改写折成几条指令，这次 30 轮里没再出现丢数。无锁仍是 data race，不能靠 `-O2` 修。拿 `-O2` 的修复版去对 `-O0` 的无锁版，两边的优化级别不同，读数差在编译选项上。所以计数这一段统一在 `-O0` 下做对照。

`std::mutex` 在 `<mutex>` 里，C++11 起咱们就能用。它提供 `lock()`、`try_lock()`、`unlock()` 三个操作，标准给它的定位是独占所有权的非递归互斥量（[thread.mutex.class]，C++11 起）。一条线程拿着它，别的线程调 `lock()` 就阻塞，调 `try_lock()` 就立刻返回 false，直到持有者 `unlock()`。

前置条件有两条，您得对一遍：`lock()` 的前置条件是调用线程此刻不拥有这把锁，`unlock()` 的前置条件是调用线程拥有它（[thread.mutex.requirements.mutex]/6、/21，C++11 起）。同一条线程对 `std::mutex` 连着锁两次，前置条件不成立，常见结果是自己卡住自己。

需要递归加锁的场合，咱们可以在 mutex 家族里找到 `std::recursive_mutex`，它的用法计划收在 cookbook（卷五的配方选读册）的配方里，本篇不铺。

锁能修好丢数，靠的是一次 `unlock()` 与随后成功拿到这把锁的 `lock()` 之间的 synchronizes-with 关系：白话讲，前一次放锁之前写下的东西，对后一次拿锁的线程可见。它是 happens-before 的一种来源（[thread.mutex.requirements.mutex]/8、/24）。咱们用锁把读、加、写回串起来：线程 A 在临界区里写完 `counter`，走出作用域解锁，线程 B 拿到锁的时候，A 的写入已经 happens-before 它的读取。

您这里容易碰上的岔路，是以为加了锁就万事大吉。锁本身不认识数据，它只保证同一时刻只有一条线程进得来。真正让程序正确的，是所有碰 `counter` 的线程都同意走同一个入口。谁绕过去，锁管不着。后面讲锁保护哪块数据的时候，咱们会看到绕过去长什么样。

## 手动 lock 和 unlock 为什么不能用

接手别人的代码时，您最容易读到的就是两行直白调用：

```cpp
void work() {
  gate.lock();
  throw std::runtime_error("boom");
  gate.unlock();
}
```

这里我故意让异常在解锁之前抛出来。咱们让工作线程捕获它，主线程随后去拿同一把锁，跑起来是这样（`timeout 2` 收尾）：

```text
[worker] caught boom, the lock is still held
[main] asking for the same lock
exit=124
```

这段程序的输出接进管道时，`stdout` 的缓冲会跟着被杀掉的进程一起丢。您用 `stdbuf -oL` 跑，或者直接在终端里跑，才看得到那两行。

主线程停在 `[main] asking` 之后，两秒到了被 `timeout` 杀掉，`[main] got it` 始终没有出现。工作线程自己退出了，锁还挂在它名下。标准把“线程退出时仍持有互斥量”写成未定义行为（[thread.mutex.class]/5），咱们这次看到的是主线程一直等，换个实现，换次调度，也可能是别的样子。

多个返回路径更麻烦。您看，临界区里每多一个提前返回的 `return`，就多一处需要记得 `unlock()` 的地方。异常路径、提前返回、后面有人往里加一行 `throw`，都能让锁留在原地。这类错误不会在编译时冒出来，也不一定每次运行都出现。

RAII 的做法是把锁的生命周期绑给作用域。`std::lock_guard` 是 C++11 起的最轻包装：构造函数调用 `m.lock()`，析构函数调用 `m.unlock()`（[thread.lock.guard]/2、/6）。拷贝构造被删除，也没有移动接口，标准把这两点写在类声明里（同一条款）。它没有 `unlock()` 成员，您手里的锁一旦构造出来，只能等作用域结束。

同一段代码，咱们换成 `lock_guard`：

```cpp
void work() {
  std::lock_guard<std::mutex> lock(gate);
  throw std::runtime_error("boom");
}
```

```text
[worker] caught boom
[main] asking for the same lock
[main] got it
```

异常从 `work()` 里穿出去，栈展开带着 `lock` 的析构一起走，锁在异常离开作用域的路上就放掉了。主线程随后拿到锁，打印 `got it`。咱们不需要在每条返回路径上补一句解锁，作用域结束就是解锁点。

有一个写法，我得单独说，因为它看起来像用了 RAII：

```cpp
for (int i = 0; i < rounds; ++i) {
  std::lock_guard<std::mutex>{counter_mutex};
  ++counter;
}
```

花括号里构造的是一个没有名字的临时对象。语句结束，临时对象析构，锁在 `++counter` 之前就放掉了。您把上一篇的实验换成没有名字的写法，在 `-O0` 下 30 轮跑出来：

```text
trial 0: printed 313868, lost 6132
trial 1: printed 313778, lost 6222
trial 2: printed 314428, lost 5572
0 of 30 trials printed the expected 320000
```

咱们一轮都没对。GCC 16.2.1 的 libstdc++ 在锁守卫的构造函数上标了 `[[nodiscard]]`，它默认给一条 `-Wunused-result` 警告，说构造函数的返回值被忽略了。

警告是编译器帮忙，别的实现不一定给。名字得您自己给，写成 `std::lock_guard<std::mutex> lock(counter_mutex)`，变量名不能省。

C++ Core Guidelines 的 CP.20 把做法写成了规范：用 RAII，不要直接写 `lock()` 和 `unlock()`。有一个例外咱们得知道，是 `adopt_lock`，它接管的是一把已经被 `std::lock()` 或类似算法拿到的锁，只负责析构时释放。加锁那一步仍然交给库函数，不落在应用代码手里。

## unique_lock：需要挪动、提前放、陪等待

需要挪动、提前放锁、陪条件变量等待的时候，您才用得上 `std::unique_lock`（C++11 起，`<mutex>`）。`lock_guard` 够用的场合，锁留在作用域里就好。

它比 `lock_guard` 多出一个所有权状态。咱们构造它时可以照常加锁，也可以带 `std::defer_lock` 暂不加、等到需要时再 `lock()`，带 `std::try_to_lock` 试一次，或者带 `std::adopt_lock` 接管一把已经拿到的锁。它提供 `owns_lock()`，用来查询当前是否持锁，提供 `unlock()` 提前放锁，还能移动：锁的所有权可以从一个函数交到另一个函数手里，`lock_guard` 做不到这一点。

提前解锁能省多少等待，咱们量一次。代码里 holder 线程拿锁、拷一份十万个整数的快照，然后做一件耗时的事。两种做法：一直拿着锁做完，或者拷完就放、出了锁再做。主线程在 5 毫秒后去拿同一把锁，量它等了多久：

```cpp
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
```

```text
lock held through the work: waited 45 ms
unlock after the copy:     waited 0 ms
```

输出里，holder 线程一直拿着锁，主线程等了 45 毫秒，它比 holder 晚 5 毫秒起跑。咱们能看到，拷完就放，主线程几乎立刻进去了。50 毫秒的 `sleep_for` 只是替身，现实里换成排序、格式化、写文件、发网络请求都一样：这些东西不需要锁，却能让别人等着。

拷贝本身也要付代价。快照占内存，拷贝需要时间，数据很大时这笔开销不能不算。您选哪边，看两件事的相对长度：拷贝比等待便宜，就拷出来。数据大到拷不动，那就得换个设计，不能硬把锁拿在手里。

`unique_lock` 的额外状态也有一点开销，通常是一个指针加一个布尔量，咱们把它用在需要的地方就行。手里只有一段作用域，`lock_guard` 更省心，它连“忘记自己有没有持锁”这件事都不给您。

条件变量那一边，`std::condition_variable::wait` 要的正是 `std::unique_lock<std::mutex>&`，等待期间它要替咱们解锁、醒来再加锁，`lock_guard` 没有这样的能力。`std::unique_lock` 最常见的用处是提前放锁和陪条件变量等待。锁的所有权转交，留给更复杂的接口。第三单元《任务之间怎样等待和协调》已经写好，讲等待与协调的工具，入口是[《等待一个条件成立》](../chapter03-wait-and-coordinate/01-wait-for-a-condition)。

把这一段里的 `sleep_for` 换成您手里真会做的耗时活，您再量一次，等待会变成多少。

## scoped_lock：几把锁一趟拿全

手上同时有两把锁的时候，您嵌套 `lock_guard` 就会重演上一篇里加锁顺序相反的死锁。两个对象，各有一把锁和一个数值，两边都要动两个对象，加锁顺序相反：

```cpp
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
```

```text
holding one side
holding one side
exit=124
```

这段程序接管道时同样会丢掉缓冲里的输出，您用 `stdbuf -oL`，或者直接在终端里跑。

咱们看到两条线程各拿到一把，各自等对方手里那把，两秒后被杀掉。死锁的形状上一篇见过，本篇要处理它。

把它换成 `std::scoped_lock`（C++17 起），您看，只改加锁那一行：

```cpp
void move(Item& from, Item& to, long long amount) {
  std::scoped_lock lock(from.m, to.m);
  from.value -= amount;
  to.value += amount;
}
```

咱们让两条线程各跑 200000 次，一个方向减 1，另一个方向加 2：

```text
sum = 2000000, x = 1200000, y = 800000
```

两个数加起来，始终是 2000000。咱们没有看到卡住，也没有少一分钱。

咱们看 `scoped_lock` 的构造函数：接过一把以上的锁时，它调用的是 `std::lock(m...)`（[thread.lock.scoped]/2，C++17 起），析构时把拿到的锁逐个释放（同节 /6）。

标准对 `std::lock` 的要求是：它用 `lock()`、`try_lock()`、`unlock()` 的组合去拿全部参数，调用序列不会造成死锁，具体怎么排没有规定（[thread.lock.algorithm]/5）。拿不到就全放，换个次序重来，草案注解里把这类做法叫 try-and-back-off。它保证的只是算法本身，咱们别把已经握在手里的锁递进去。

手里只有一把锁的时候，`scoped_lock` 的构造函数直接调 `m.lock()`，跟 `lock_guard` 等价（[thread.lock.scoped]/2）。读代码时，咱们一眼就能分开：看到 `lock_guard` 知道这里只有一把锁，看到 `scoped_lock` 知道它可能一次拿几把。

差别在这几处，咱们把三个工具摆在一起：

| 工具 | 能拿几把 | 能提前放 | 能挪走 | 常见场合 |
|------|----------|----------|--------|----------|
| `std::lock_guard` | 一把 | 不能 | 不能 | 一段作用域里的单锁 |
| `std::unique_lock` | 一把 | 能 | 能 | 条件变量、提前解锁、锁的所有权转交 |
| `std::scoped_lock` | 一把或多把 | 不能 | 不能 | 两把以上一起拿，避免顺序死锁 |

我在表里没有列 `std::recursive_mutex` 和 `std::timed_mutex`。它们是 mutex 家族里的 recursive 和 timed 变体，计划收在 cookbook 的配方里，本篇不铺。

还有一条边界我得交代：`scoped_lock` 不能交给条件变量。`condition_variable::wait` 要的是 `unique_lock`，`scoped_lock` 没有等待期间解锁再上锁的能力。手里同时有多把锁、又要等条件，写法得回到 `unique_lock` 加 `std::lock`。

## 锁保护的是哪块数据

三个工具都到手了，咱们接下来只剩一个问题：锁该看住哪块数据？

给一个类加锁时，您第一件要回答的事是：这把锁拦的是哪几次访问？标准不会替您回答，锁也不认识任何变量。

我习惯把锁和它保护的数据放进同一个类，数据设成私有，外面只留操作：

```cpp
class Counter {
public:
  void add(int n) {
    std::lock_guard<std::mutex> lock(m_);
    value_ += n;
  }

  long long value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

private:
  mutable std::mutex m_;
  long long value_ = 0;
};
```

`add` 和 `value` 都拿同一把锁，`value_` 是私有的，调用者拿不到它的引用，也绕不过锁。`m_` 标成 `mutable`，因为 `value() const` 也要加锁。咱们把锁和数据放进同一个类，锁的生命周期跟着对象走，锁与被保护的数据同生同灭。

漏掉一处会怎样？您把 `value()` 里的锁删掉，跑一个写线程加一百万次、主线程反复读，用 GCC 的 ThreadSanitizer 编译（`-fsanitize=thread`）：

```text
WARNING: ThreadSanitizer: data race (pid=10)
  Write of size 8 at 0x…… by thread T1 (mutexes: write M0):
    #0 Counter::add(int) e6.cpp:9
  Previous read of size 8 at 0x…… by main thread:
    #0 Counter::value() const e6.cpp:11
```

TSan 报的内容是：`add` 里的写和 `value()` 里的读之间，有一条 data race。咱们能看到，写的一方拿了锁，读的一方没拿，两者之间没有 happens-before。锁只拦那些来拿它的线程，绕过去的读照样发生。

这次报告给的检查法只用一句：可能并发的访问，要么全在锁里，要么全在锁外，不能一半一半。`const` 成员、返回引用的接口、异常路径、回调，每一处都是咱们要查的“全在”检查点。

返回引用是个容易忽略的出口。您看下面 `Box` 的实现：

```cpp
class Box {
public:
  int& ref() {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }

private:
  std::mutex m_;
  int value_ = 0;
};

int main() {
  Box box;
  int& r = box.ref();
  box.set(42);
  std::printf("through the escaped reference: %d\n", r);
}
```

```text
through the escaped reference: 42
```

`ref()` 里确实加了锁，可它交出去的是一块私有数据的位置。锁在函数返回时就放了，您手里的 `r` 不归锁管。`set(42)` 改完之后，`r` 读出来就是 42。要是另一条线程在此时调用 `set`，这次读与那次写之间就是一条 data race，接口自己把锁绕开了。

咱们修的时候，把数据拷出来交出去，返回 `int` 而不是 `int&`。大的对象可以返回一份拷贝，或者一个指向快照副本的 `shared_ptr`，也可以干脆不暴露数据，只暴露操作。哪条路合适，归讲一次操作边界的那一篇讲接口设计时再说。

锁是唯一的同步来源吗？不是，join 也在建立顺序。咱们回到第一节配过锁的计数器：修好的版本里，主线程读 `counter` 那一句没有加锁，它靠 `a.join()` 和 `b.join()` 建立顺序，join 与线程结束之间，有 synchronizes-with 关系（[thread.thread.member]/4）。两个工作线程写在 join 之前的修改，join 之后的主线程都看得见。规则可以写成一句：可能并发的两次访问之间，要有 happens-before。锁是一种建立方式，join 是另一种。

您把 `Counter::value()` 里的锁删掉，用 TSan 编译一次，看它报在哪一行。

## 粒度：锁里装多少事

临界区包住哪些语句，咱们得自己定，它决定别的线程等多久。包得太多，几条线程只能排队过。包得太少，该一起完成的动作被切开，不变量半路露出来。

讲 `unique_lock` 的时候已经见过：拷完快照就把锁放了，耗时活挪到外面。判断的时候，看哪些操作必须一起完成，不必拿“尽量减少加锁”当目标。`++counter` 的读、加、写回三步必须一起完成，它们就一起留在锁里。至于格式化日志、写文件、算哈希，只要不碰被保护的数据，没必要拖进临界区，这是咱们自己定的范围。

粒度也不是越细越好，咱们别把一把锁随随便便分成两把。两个字段如果必须保持一致，余额和流水条数、头指针和长度，它们就得由同一把锁保护。拆成两把锁，读的人可能看见新的余额配旧的流水条数，不变量在两次加锁之间破了。跨数据的不变量归讲一次操作边界的那一篇。这里把必须共用一把锁的访问列出来：余额和流水条数一起读、一起写，头指针和长度一起换。

量您自己的代码的临界区时，您可以用一次实验看清代价，再决定拆不拆。四条线程各把四个计数器中的一个加两百万次，一种做法是四个计数器共用一把锁，另一种是每个计数器一把：

```cpp
using clock_type = std::chrono::steady_clock;

long long run(int lock_count) {
  const int kThreads = 4;
  const int kIters = 2000000;
  std::vector<std::mutex> locks(lock_count);
  std::vector<long long> counters(lock_count, 0);
  auto t0 = clock_type::now();
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    const int slot = (lock_count == 1) ? 0 : i;
    threads.emplace_back([&, slot] {
      for (int j = 0; j < kIters; ++j) {
        std::lock_guard<std::mutex> lock(locks[slot]);
        ++counters[slot];
      }
    });
  }
  for (auto& t : threads) t.join();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             clock_type::now() - t0).count();
}
```

```text
one lock for four counters: 216 ms
one lock per counter:       152 ms
one lock for four counters: 218 ms
one lock per counter:       151 ms
one lock for four counters: 211 ms
one lock per counter:       149 ms
```

四把锁快了三成上下。咱们看，数字只在当前机器、当前负载下成立，换台机器、换次数，比例会变。共用一把锁，四条线程只能轮流进去。每份数据一把锁，它们在四个核上各干各的。

我再提两种粒度问题。一种是锁里做了会阻塞的事：等 I/O、等另一个线程、申请一大块内存。锁被占着，别人只能干等，这不是死锁，但同样让别的线程干等，区别在没有人循环等待。另一种是锁里调用了不受咱们控制的回调，回调转一圈又回来拿这把锁，同一条线程就永远等不到。前一种靠缩短临界区，后一种靠设计，讲死锁与减少共享的那一篇还会碰到相关的设计问题。

在您自己的机器上跑一遍四把锁与一把锁的计时，数字会跟这里不一样，您试试。

## 少共享一点：thread_local 与各持一份

有些共享，咱们根本不必让它发生。计数器之所以要锁，是因为两条线程都在改同一个变量。要是每条线程各有一份，数据所有权跟着线程走，只在最后汇总一次，锁就不需要了。

C++11 给了咱们 `thread_local`：给每个线程一份独立的对象，存储持续到线程结束（[basic.stc.thread]/1）。块作用域里的变量，每个线程第一次经过声明时初始化属于自己的一份，同一线程内只初始化一次（[stmt.dcl]/3）。初始化抛异常时不算完成，下次进入声明会重试。

命名空间作用域里的非块变量，您得分开看。初始化如果不是常量初始化，动态初始化可能在线程起始函数第一条语句之前，也可能推迟到该线程首次使用之前，由实现决定（[basic.start.dynamic]/7）。线程结束时，这些对象随线程一起销毁，销毁次序与构造相反（[basic.start.term]/2、/4）。

例子里的 `local_counter` 用常量 0 初始化，不涉及这段动态初始化。咱们换成需要运行时构造的类型，才落到上面两条规则里。

把上一篇的计数器改成线程本地版本，咱们看：

```cpp
thread_local long long local_counter = 0;

void work(int rounds, long long* slot, const char* name) {
  for (int i = 0; i < rounds; ++i) ++local_counter;
  std::printf("%s: value %lld at %p\n", name, local_counter,
              static_cast<void*>(&local_counter));
  *slot = local_counter;
}
```

咱们让两条线程各加 160000 次，跑 30 轮：

```text
a: value 160000 at 0x76c6233ff6b8
b: value 160000 at 0x76c622bfe6b8
30 of 30 trials summed to 320000
main's own copy: 0
```

两个地址不一样，各是各的对象。咱们看，每一轮把两份结果相加都是 320000，没有丢。主线程自己的 `local_counter` 仍是 0，主线程从没碰过它。

我拿同一份程序用 TSan 编译，跑下来没有一条报告。它只能算证据，不算证明：TSan 没报，不等于所有可能的交错都安全，只是这次没有数据竞争可报。

咱们得看清，`thread_local` 做的事是把一份状态复制成 N 份，替不掉真正的共享。用它之前，有几件事要知道：

- 别的线程读不到您的副本，主线程也不行。
- 每个线程各有一份，初始化的开销按线程算。块作用域看第一次经过声明，命名空间作用域看上面的初始化时机。
- 线程退出时销毁，析构顺序与构造相反，析构里再去访问别的 `thread_local` 对象要小心。
- 它仍然是全局状态，只是按线程分开。代码的可读性不会因为它变好。
- 动态库里的 `thread_local` 有额外的生命周期问题，跨平台代码要单独核对。

您手里的数据如果每条线程都能各拿一份，不妨试试。事实只能有一份的时候，它帮不上忙。停止标志、余额、全局配置、缓存，这些东西每条线程各拿一份，汇总出来就不是真相。

换一条路，咱们把结果放进各自的位置，主线程最后汇总。准备一个长度 2 的 `std::vector<long long>`，两条线程分别写 `partial[0]` 和 `partial[1]`，没有任何锁：

```cpp
std::vector<long long> partial(2, 0);
const int rounds = 160000;
std::thread a([&] { for (int i = 0; i < rounds; ++i) ++partial[0]; });
std::thread b([&] { for (int i = 0; i < rounds; ++i) ++partial[1]; });
a.join();
b.join();
std::printf("total = %lld\n", partial[0] + partial[1]);
```

两个线程各写各的元素，被写的元素各自独占。容器本身仍然是共享的，标准允许不同元素并发修改（[container.requirements.dataraces]/2），`vector<bool>` 除外。join 之后主线程再相加，顺序由 join 建立。这段程序我也跑了 30 轮，每轮都加到 320000，TSan 没有报告。

两个写法放在一起看，咱们会发现它们走的是同一个思路：让每条线程拥有自己的数据，合并放到收尾那一步。独立持有、传递结果、共同修改三种方案怎么取舍，归讲死锁与减少共享的那一篇。

您什么时候不该这么办？当事实只能有一份的时候。这时候要么用锁，要么用原子操作，要么在设计上把共享挪到一处。判断的问题很短：数据非共享不可吗？答案是否，别急着上锁，想想能不能让每条线程自己拿着。

## 动笔之前，问几句

写加锁的代码之前，我习惯把这几个问题过一遍。答不上，别急着写：

1. 您真的需要共享这块数据吗？每条线程各持一份，最后汇总，行不行？
2. 您打算用哪把锁保护它？锁和数据住在同一个类里吗？数据是不是私有的？
3. 每一个可能并发的访问，您都让它经过这把锁了吗？`const` 成员、返回引用、异常路径、回调，都算。
4. 锁里待多久？您能把快照拷出来吗？耗时活能不能挪到锁外面？
5. 您一次要拿几把？两把以上用 `scoped_lock`，不要一把一把嵌套着拿。

您要是把第 1 条答成“不必共享”，后面四条经常就不用问了。第 3 条和第 4 条，是演示里最容易走岔的两处。

## 下一步

下一篇，咱们讲一次操作的边界：检查之后动手为什么漏，多个对象之间的不变量怎么保，并发接口怎么设计才能让调用者绕不过保护。本篇的 `Counter` 接口只护住了一次加减，两次操作之间的空档，还没人管。

再往后，咱们讲减少共享与避免死锁：死锁怎么发生、锁顺序怎么排、分层怎么做，还有独立持有、传递结果、共同修改三种方案的取舍。

翻页之前，请您把本篇修好的计数器跑 30 轮，再把 `lock_guard` 删掉跑 30 轮，编译选项与上面一致，对照同档 `-O0` 的读数。

## 参考资源

我写本篇的时候，推进思路借鉴了 Anthony Williams 的《C++ Concurrency in Action》，取的是讲共享数据的那几章。例子和文字都是我自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的草案文本与 cppreference。

- [std::mutex（cppreference）][cppref-mutex]，C++11 起的互斥量。
- [std::lock_guard（cppreference）][cppref-guard]，构造加锁、析构解锁的轻量守卫。
- [std::unique_lock（cppreference）][cppref-unique]，可移动、可提前解锁的锁包装。
- [std::scoped_lock（cppreference）][cppref-scoped]，C++17 起的多锁守卫。
- [std::lock（cppreference）][cppref-lock]，一次拿多把锁的算法。
- [thread_local 存储期（cppreference）][cppref-tls]，线程本地变量的声明与生命周期。
- [C++ 标准草案：Mutex requirements（thread.mutex.requirements.mutex）][draft-mutex]，锁的语义与同步关系。
- [C++ 标准草案：Class mutex（thread.mutex.class）][draft-mutex-class]，非递归互斥量的定义与所有权规定。
- [C++ 标准草案：Class template lock_guard（thread.lock.guard）][draft-guard]，构造、析构与类声明。
- [C++ 标准草案：Generic locking algorithms（thread.lock.algorithm）][draft-lock]，`std::lock` 与 `try_lock` 的保证。
- [C++ 标准草案：Class template scoped_lock（thread.lock.scoped）][draft-scoped]，构造与析构的行为。
- [C++ 标准草案：Thread storage duration（basic.stc.thread）][draft-tls]，每个线程一个对象的依据。
- [C++ 标准草案：Declaration statement（stmt.dcl）][draft-stmt-dcl]，块作用域线程存储变量的初始化。
- [C++ 标准草案：Dynamic initialization of non-block variables（basic.start.dynamic）][draft-start-dynamic]，非块变量的初始化时机。
- [C++ 标准草案：Termination（basic.start.term）][draft-start-term]，线程存储对象的销毁顺序。
- [C++ 标准草案：Container data races（container.requirements.dataraces）][draft-containers]，不同元素并发修改的保证。
- [C++ 标准草案：thread members（thread.thread.member）][draft-thread-member]，join 与线程结束的同步关系。
- [C++ Core Guidelines：CP.20][cp20]，用 RAII，不要直接调用 `lock()` 和 `unlock()`。

[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[cppref-guard]: https://en.cppreference.com/w/cpp/thread/lock_guard
[cppref-unique]: https://en.cppreference.com/w/cpp/thread/unique_lock
[cppref-scoped]: https://en.cppreference.com/w/cpp/thread/scoped_lock
[cppref-lock]: https://en.cppreference.com/w/cpp/thread/lock
[cppref-tls]: https://en.cppreference.com/w/cpp/language/storage_duration
[draft-mutex]: https://eel.is/c++draft/thread.mutex.requirements.mutex
[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-guard]: https://eel.is/c++draft/thread.lock.guard
[draft-lock]: https://eel.is/c++draft/thread.lock.algorithm
[draft-scoped]: https://eel.is/c++draft/thread.lock.scoped
[draft-tls]: https://eel.is/c++draft/basic.stc.thread
[draft-stmt-dcl]: https://eel.is/c++draft/stmt.dcl
[draft-start-dynamic]: https://eel.is/c++draft/basic.start.dynamic
[draft-start-term]: https://eel.is/c++draft/basic.start.term
[draft-containers]: https://eel.is/c++draft/container.requirements.dataraces
[draft-thread-member]: https://eel.is/c++draft/thread.thread.member
[cp20]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock
