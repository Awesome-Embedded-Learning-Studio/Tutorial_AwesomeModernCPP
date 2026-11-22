---
title: "锁盖多大才算够：保护范围、粒度与少共享"
description: "三种锁挂上以后算更细的账：一把锁看住哪块数据，锁里装多少事算合适，再用 thread_local 与各持一份减少共享。"
chapter: 5
order: 3
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

# 锁盖多大才算够：保护范围、粒度与少共享

三种锁挂上了，工具不缺了。接下来是把更细的账算清：一把锁看住哪块数据，锁里装多少事算合适，以及能不能干脆每条线程各拿一份、让锁提前下班。

## 锁保护的是哪块数据

三个工具都到手了，咱们手上还剩一个问题：锁该看住哪块数据？

给一个类加锁时，您第一件要回答的事是：这把锁拦的是哪几次访问？标准不会替您回答，锁也不认识任何变量。

笔者习惯把锁和它保护的数据放进同一个类，数据设成私有，外面只留操作：

<OnlineCompilerDemo
  title="锁和数据住进同一个类，外面只留操作"
  source-path="code/examples/vol5/chapter02/13_demo_counter_class.cpp"
  description="Counter 把 mutex 和 value_ 藏成私有成员，公开的 add 与 value 各自加锁：想绕过锁碰数据，得穿过访问控制。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_counter_class.cpp —— 锁和数据住进同一个类，外面只留操作
#include <cstdio>
#include <mutex>

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

int main() {
  Counter c;
  for (int i = 0; i < 1000; ++i) c.add(3);
  std::printf("after 1000 adds of 3: %lld\n", c.value());
  return 0;
}
```

</OnlineCompilerDemo>

`add` 和 `value` 都拿同一把锁，`value_` 是私有的，调用者拿不到它的引用，也绕不过锁。`m_` 标成 `mutable`，因为 `value() const` 也要加锁。咱们把锁和数据放进同一个类，锁的生命周期跟着对象走。

漏掉一处会怎样？您把 `value()` 里的锁删掉，跑一个写线程加一百万次、主线程反复读。这个删锁变体入库在 code/examples/vol5/chapter02/13b_demo_counter_class_tsan.cpp，和 13 号文件并排放着，您可以自己编译对着跑。这一处编译用 `-O1` 加 `-g`，再配 `-fsanitize=thread`，比其余实验低一档，`-g` 是为了让报告里留得住文件名和行号。下面是其中一回的报告，模块偏移、BuildId 和调用栈的深层帧笔者删了，只留两边的关键几行：

```text
WARNING: ThreadSanitizer: data race (pid=9)
  Write of size 8 at 0x7fffffffd3f8 by thread T1 (mutexes: write M0):
    #0 Counter::add(int) /tmp/13b_demo_counter_class_tsan.cpp:10
  Previous read of size 8 at 0x7fffffffd3f8 by main thread:
    #0 Counter::value() const /tmp/13b_demo_counter_class_tsan.cpp:14
    #1 main /tmp/13b_demo_counter_class_tsan.cpp:31
  Location is stack of main thread.

SUMMARY: ThreadSanitizer: data race /tmp/13b_demo_counter_class_tsan.cpp:10 in Counter::add(int)
```

TSan 报的内容是：`add` 里的写和 `value()` 里的读之间，有一条 data race。咱们看报告，写的一方拿着锁，括号里的 M0 就是 `add` 进的那把锁；读的一方没拿，两者之间没有 happens-before。锁只拦那些来拿它的线程，绕过去的读照样发生。这个实验笔者前后跑了四回，四回都报。pid 和地址每回都换，两边谁排在前头也换：四回里“写在前、读在后”和“读在前、写在后”各出过两回，排后面的那边冠一个 Previous，SUMMARY 跟着排在前头的那边走。

实验做到这里，检查法其实就一句：可能并发的访问，要么全在锁里，要么全在锁外，不能一半一半。`const` 成员、返回引用的接口、异常路径、回调，每一处都是咱们要查的“全在”检查点。

返回引用是个容易忽略的出口。您看下面 `Box` 的实现：

<OnlineCompilerDemo
  title="ref() 加了锁，交出去的引用不归锁管"
  source-path="code/examples/vol5/chapter02/14_demo_escaped_reference.cpp"
  description="拿到的引用在锁外飘着：set(42) 落锁里，读引用不落。另一条线程此刻调 set，一读一写之间就是一条 data race，接口自己把锁绕开了。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_escaped_reference.cpp —— ref() 加了锁，交出去的引用不归锁管
#include <cstdio>
#include <mutex>

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

</OnlineCompilerDemo>

```text
through the escaped reference: 42
```

`ref()` 里确实加了锁，可它交出去的是一块私有数据的位置。锁在函数返回时就放了，您手里的 `r` 不归锁管。`set(42)` 改完之后，`r` 读出来就是 42。要是另一条线程在此时调用 `set`，一读一写之间就是一条 data race，接口自己把锁绕开了。（这种交引用的接口笔者自己也写过，写的时候没觉得有毛病，是讲一遍锁才看出来它绕开了。）

咱们修的时候，把数据拷出来交出去，返回 `int` 而不是 `int&`。大的对象可以返回一份拷贝，或者一个指向快照副本的 `shared_ptr`，也可以干脆不暴露数据，只暴露操作。哪条路合适，留到讲一次操作边界的一篇谈接口设计时再说。

锁是唯一的同步来源吗？不是，join 也在建立顺序。咱们回到第一节配过锁的计数器：修好的版本里，主线程读 `counter` 那一句没有加锁，它靠 `a.join()` 和 `b.join()` 建立顺序，join 与线程结束之间，有 synchronizes-with 关系（[thread.thread.member]/4）。两个工作线程写在 join 之前的修改，join 之后的主线程都看得见。规则可以写成一句：可能并发的两次访问之间，要有 happens-before。锁是一种建立方式，join 是另一种。

您把 `Counter::value()` 里的锁删掉，用 TSan 编译一次，看它报在哪一行。

## 锁里该装多少事：粒度

临界区包住哪些语句，咱们得自己定，它决定别的线程等多久。包得太多，几条线程只能排队过。包得太少，该一起完成的动作被切开，不变量半路露出来。

讲 `unique_lock` 的时候已经见过：拷完快照就把锁放了，耗时活挪到外面。判断的时候，看哪些操作必须一起完成，不必拿“尽量减少加锁”当目标。`++counter` 的读、加、写回三步必须一起完成，它们就一起留在锁里。至于格式化日志、写文件、算哈希，只要不碰被保护的数据，没必要拖进临界区，这是咱们自己定的范围。

粒度也不是越细越好，咱们别把一把锁随随便便分成两把。两个字段如果必须保持一致，余额和流水条数、头指针和长度，它们就得由同一把锁保护。拆成两把锁，读的人可能看见新的余额配旧的流水条数，不变量在两次加锁之间破了。跨数据的不变量，留给讲一次操作边界的一篇。这里把必须共用一把锁的访问列出来：余额和流水条数一起读、一起写，头指针和长度一起换。

量您自己的代码的临界区时，您可以用一次实验看清代价，再决定拆不拆。四条线程各把四个计数器中的一个加两百万次，一种做法是四个计数器共用一把锁，另一种是每个计数器一把：

<OnlineCompilerDemo
  title="四把锁比一把锁快多少"
  source-path="code/examples/vol5/chapter02/15_demo_lock_granularity.cpp"
  description="四条线程各把四个计数器中的一个加两百万次：共用一把锁轮流进去，每计数器一把锁四个核各干各的，快了三成上下。数字只在当前机器当前负载下成立。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_lock_granularity.cpp —— 四把锁比一把锁快多少：四线程各加两百万次
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

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

int main() {
  for (int i = 0; i < 3; ++i) {
    std::printf("one lock for four counters: %lld ms\n", run(1));
    std::printf("one lock per counter:       %lld ms\n", run(4));
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
one lock for four counters: 216 ms
one lock per counter:       152 ms
one lock for four counters: 218 ms
one lock per counter:       151 ms
one lock for four counters: 211 ms
one lock per counter:       149 ms
```

四把锁快了三成上下。您别忘了，数字只在当前机器、当前负载下成立，换台机器、换次数，比例会变。共用一把锁，四条线程只能轮流进去。每份数据一把锁，它们在四个核上各干各的。

粒度上还有两种毛病，笔者一并说了。一种是锁里做了会阻塞的事：等 I/O、等另一个线程、申请一大块内存。锁被占着，别人只能停在那儿等。这不算死锁（区别在没有人循环等待），可拖住别人的效果是一样的。另一种是锁里调用了不受咱们控制的回调，回调转一圈又回来拿这把锁，同一条线程就永远等不到。前一种靠缩短临界区，后一种靠设计，讲死锁与减少共享的一篇还会碰到相关的设计问题。（回调进锁这种事，笔者是在别人的代码里碰上的，找了好一阵才定位到，打那以后看见临界区里有回调就先问一句它出去以后去哪儿。）

在您自己的机器上跑一遍四把锁与一把锁的计时，数字会跟这里不一样，您试试。

## 能不能让每条线程各拿一份：thread_local 与各持一份

有些共享，咱们根本不必让它发生。计数器之所以要锁，是因为两条线程都在改同一个变量。要是每条线程各有一份，数据所有权跟着线程走，只在最后汇总一次，锁就不需要了。

把上一篇的计数器改成线程本地版本，咱们看：

<OnlineCompilerDemo
  title="thread_local：每条线程一份计数，最后汇总，锁退场"
  source-path="code/examples/vol5/chapter02/16_demo_thread_local_counter.cpp"
  description="两条线程各加 160000 次跑 30 轮，两个地址不一样、每轮相加都是 320000、主线程自己的副本仍是 0。数据所有权跟着线程走，锁不需要了。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_thread_local_counter.cpp —— 每条线程一份计数，最后汇总，锁退场
#include <cstdio>
#include <thread>

thread_local long long local_counter = 0;

void work(int rounds, long long* slot, const char* name) {
  for (int i = 0; i < rounds; ++i) ++local_counter;
  std::printf("%s: value %lld at %p\n", name, local_counter,
              static_cast<void*>(&local_counter));
  *slot = local_counter;
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  long long a_sum = 0, b_sum = 0;
  for (int t = 0; t < kTrials; ++t) {
    long long a = 0, b = 0;
    std::thread ta(work, kRounds, &a, "a");
    std::thread tb(work, kRounds, &b, "b");
    ta.join();
    tb.join();
    if (a + b == 2LL * kRounds) ++correct;
    a_sum = a;
    b_sum = b;
  }
  (void)a_sum;
  (void)b_sum;
  std::printf("%d of %d trials summed to %d\n", correct, kTrials, 2 * kRounds);
  std::printf("main's own copy: %lld\n", local_counter);
  return 0;
}
```

</OnlineCompilerDemo>

咱们让两条线程各加 160000 次，跑 30 轮：

```text
a: value 160000 at 0x76c6233ff6b8
b: value 160000 at 0x76c622bfe6b8
30 of 30 trials summed to 320000
main's own copy: 0
```

两个地址不一样，各是各的对象。咱们看，每一轮把两份结果相加都是 320000，没有丢。主线程自己的 `local_counter` 仍是 0，主线程从没碰过它。

笔者拿同一份程序用 TSan 编译，跑下来没有一条报告。它只能算证据，不算证明：TSan 没报，不等于所有可能的交错都安全，只是跑过的几轮里没有数据竞争可报。

现象到手了，回头补两句标准的话。C++11 给了咱们 `thread_local`：给每个线程一份独立的对象，存储持续到线程结束（[basic.stc.thread]/1）。块作用域里的变量，每个线程第一次经过声明时初始化属于自己的一份，同一线程内只初始化一次（[stmt.dcl]/3）。初始化抛异常时不算完成，下次进入声明会重试。

命名空间作用域里的非块变量，您得分开看。初始化如果不是常量初始化，动态初始化可能在线程起始函数第一条语句之前，也可能推迟到该线程头一回用它之前，由实现决定（[basic.start.dynamic]/7）。

例子里的 `local_counter` 用常量 0 初始化，不涉及上面说的动态初始化。咱们换成需要运行时构造的类型，才落到刚才两条规则里。

咱们得看清，`thread_local` 做的事是把一份状态复制成 N 份，替不掉真正的共享。用它之前，有几件事要知道：

- 别的线程读不到您的副本，主线程也不行。
- 每个线程各有一份，初始化的开销按线程算。块作用域看第一次经过声明，命名空间作用域看上面的初始化时机。
- 线程退出时销毁，析构顺序与构造相反，析构里再去访问别的 `thread_local` 对象要小心。
- 它仍然是全局状态，只是按线程分开。代码的可读性不会因为它变好。
- 动态库里的 `thread_local` 有额外的生命周期问题，跨平台代码要单独核对。

您手里的数据如果每条线程都能各拿一份，不妨试试。事实只能有一份的时候，它帮不上忙。停止标志、余额、全局配置、缓存，这些东西每条线程各拿一份，汇总出来就不是真相。

换一条路，咱们把结果放进各自的位置，主线程最后汇总。准备一个长度 2 的 `std::vector<long long>`，两条线程分别写 `partial[0]` 和 `partial[1]`，没有任何锁：

<OnlineCompilerDemo
  title="各写各的元素，join 之后汇总，全程无锁"
  source-path="code/examples/vol5/chapter02/17_demo_partial_slots.cpp"
  description="两条线程分别写 partial[0] 和 partial[1]，被写的元素各自独占，join 建立顺序后再相加：total = 320000，30 轮全对。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_partial_slots.cpp —— 各写各的元素，join 之后汇总，全程无锁
#include <cstdio>
#include <thread>
#include <vector>

int main() {
  std::vector<long long> partial(2, 0);
  const int rounds = 160000;
  std::thread a([&] { for (int i = 0; i < rounds; ++i) ++partial[0]; });
  std::thread b([&] { for (int i = 0; i < rounds; ++i) ++partial[1]; });
  a.join();
  b.join();
  std::printf("total = %lld\n", partial[0] + partial[1]);
  return 0;
}
```

</OnlineCompilerDemo>

两个线程各写各的元素，被写的元素各自独占。容器本身仍然是共享的，标准允许不同元素并发修改（[container.requirements.dataraces]/2），`vector<bool>` 除外。join 之后主线程再相加，顺序由 join 建立。这个程序笔者也跑了 30 轮，每轮都加到 320000，TSan 没有报告。

两个写法放在一起看，咱们会发现它们走的是同一个思路：让每条线程拥有自己的数据，合并放到收尾那一步。独立持有、传递结果、共同修改三种方案怎么取舍，归讲死锁与减少共享的一篇。

您什么时候不该这么办？当事实只能有一份的时候。这时候要么用锁，要么用原子操作，要么在设计上把共享挪到一处。判断问一句就够：数据非共享不可吗？答案是否，别急着上锁，想想能不能让每条线程自己拿着。

## 动笔之前，问几句

写加锁的代码之前，笔者习惯把几个问题过一遍。答不上，别急着写：

1. 您真的需要共享同一份数据吗？每条线程各持一份，最后汇总，行不行？
2. 您打算用哪把锁保护它？锁和数据住在同一个类里吗？数据是不是私有的？
3. 每一个可能并发的访问，您都让它经过这把锁了吗？`const` 成员、返回引用、异常路径、回调，都算。
4. 锁里待多久？您能把快照拷出来吗？耗时活能不能挪到锁外面？
5. 您一次要拿几把？两把以上用 `scoped_lock`，不要一把一把嵌套着拿。

您要是把第 1 条答成“不必共享”，后面四条经常就不用问了。第 3 条和第 4 条，是演示里最容易走岔的两处。

## 锁配好了，然后呢

本篇的 `Counter` 接口只护住了一次加减，两次操作之间的空档，还没人管。

翻页之前，请您把本篇修好的计数器跑 30 轮，再把 `lock_guard` 删掉跑 30 轮，编译选项与上面一致，对照同档 `-O0` 的读数。


## 参考资源

笔者写本篇的时候，推进思路借鉴了 Anthony Williams 的《C++ Concurrency in Action》，取的是书中讲共享数据的几章。例子和文字都是笔者自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的草案文本与 cppreference。

- [thread_local 存储期（cppreference）][cppref-tls]，线程本地变量的声明与生命周期。
- [C++ 标准草案：Thread storage duration（basic.stc.thread）][draft-tls]，每个线程一个对象的依据。
- [C++ 标准草案：Declaration statement（stmt.dcl）][draft-stmt-dcl]，块作用域线程存储变量的初始化。
- [C++ 标准草案：Dynamic initialization of non-block variables（basic.start.dynamic）][draft-start-dynamic]，非块变量的初始化时机。
- [C++ 标准草案：Container data races（container.requirements.dataraces）][draft-containers]，不同元素并发修改的保证。

[cppref-tls]: https://en.cppreference.com/w/cpp/language/storage_duration
[draft-tls]: https://eel.is/c++draft/basic.stc.thread
[draft-stmt-dcl]: https://eel.is/c++draft/stmt.dcl
[draft-start-dynamic]: https://eel.is/c++draft/basic.start.dynamic
[draft-containers]: https://eel.is/c++draft/container.requirements.dataraces
