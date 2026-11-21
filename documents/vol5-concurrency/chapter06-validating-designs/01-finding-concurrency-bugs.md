---
title: "竞争找出来：重复测试与 TSan"
description: "重复测试给出轮数与命中率，ThreadSanitizer 把跑到的竞争报出来，Thread Safety Analysis 把检查搬进编译期。"
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 竞争找出来：重复测试与 TSan

## 重复测试：把偶发换成读数

单线程程序里，一次失败就能断言有问题。并发程序倒过来：一次全对说明不了什么，因为调度器交出来的只是众多交错里的一个样本。2.1 里有一份现成的读数（[《同一个计数器，两条线程为什么算不对？》](../chapter02-sharing-data/01-from-wrong-programs)）：没上锁的计数器编成 `-O2` 后 30 轮全对，同一份源码拿 TSan 一查，data race 照报。30 轮全对，竞争照在。偶然成功当不得正确性依据，咱们拿实验把道理变成能摸的读数。

咱们做一个更细的实验。两条线程，各把一个普通 `int` 加一次，一轮下来 `counter` 应当恰好等于 2。tight 档把读写挤在一起，wide 档把动作摊开：读进局部变量、睡 50 微秒、再写回。窗口宽度不同，命中机会就不同，读数您往下看。

<OnlineCompilerDemo
  title="重复压测：tight 档 30000 轮不丢，wide 档几乎轮轮丢"
  source-path="code/examples/vol5/chapter06/01_stress.cpp"
  description="演示用，存在数据竞争：两条线程读写同一个普通 int。tight 档三万轮可能一次不丢，wide 档（读、睡 50 微秒、写）三千轮几乎轮轮丢增量。程序要带轮数参数，站内直接运行只打印用法，本地按下方命令跑。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int counter = 0;   // 故意不用 std::atomic，本实验就是要它出错

static void bump_once(bool wide) {
  if (wide) {
    int local = counter;                                   // 读
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    counter = local + 1;                                   // 写
  } else {
    ++counter;                                             // 读写挤在一条指令附近
  }
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: %s rounds [wide]\n", argv[0]); return 1; }
  const int rounds = std::atoi(argv[1]);
  const bool wide = argc > 2;
  int bad = 0;
  for (int r = 0; r < rounds; ++r) {
    counter = 0;
    std::thread a([&] { bump_once(wide); });
    std::thread b([&] { bump_once(wide); });
    a.join();
    b.join();
    if (counter != 2) ++bad;
  }
  std::printf("%s: %d of %d rounds lost an increment\n",
              wide ? "wide" : "tight", bad, rounds);
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread stress.cpp -o stress   # 普通版配方
./stress 30000          # tight 档，读写挤在一起
./stress 3000 wide      # wide 档，读、睡 50 微秒、写
```

```text
tight: 0 of 30000 rounds lost an increment
tight: 0 of 30000 rounds lost an increment
tight: 0 of 30000 rounds lost an increment

wide: 2979 of 3000 rounds lost an increment
wide: 2993 of 3000 rounds lost an increment
```

wide 档的读数出来，笔者心里就有底了：窗口撑开，偶发真的会现形。两遍分别丢了 2979 轮与 2993 轮，几乎轮轮有失。tight 档笔者跑了三遍，三万轮里咱们没见它露过一次面，摊到普通测试的 30 轮上，抓到它的机会约等于零。可抓不到不等于没有，TSan 一节咱们拿同一类程序再验一遍。

压多少轮的取舍，笔者按预算与观测来定，不存在一个统一的数字。单轮的成本越低，买得下的轮数就越多。咱们拿窗口撑开的版本估出单轮命中的量级，再反推普通版本要多少轮才可能见一次失败。每轮的结果都要记下来，记下哪一轮、丢了多少，失败真的来了，咱们才查得回去。持续集成里可以放一个循环的用例，同一份程序咱们连跑几百上千轮，只要错了一轮，构建就算挂了。

压力设计本身也有讲究，笔者把做过的几件事摆成清单：

1. 线程数变着跑：2 条、4 条、核数顶格，各自给调度器不同的交错空间。
2. 窗口试着撑开，像 wide 档一样在读写之间垫一段延迟，偶发立刻变成常态。
3. 负载也值得一变，空载与满载的读数会差出一截。
4. 判定落在不变量上：总数守恒、没有重复、没有遗漏，错没错当场见分晓。
5. 复现条件原样记下：线程数、迭代数、延迟量、机器与负载，下次才好把失败请回来。

## ThreadSanitizer：把跑到的竞争报出来

TSan 的原理笔者一句话摆出来：编译时在每个内存访问处插桩，运行时库给每次访问记下线程与同步事件。两次访问碰在同一地址，它就拿 happens-before 去数桥，数不出桥、又有一方是写，报告就来了。判定语言与 5.3 同源，所以报告读起来不该陌生。GCC 与 Clang 都带它，选项是 `-fsanitize=thread`，咱们在 2.1 与它初见（[《同一个计数器，两条线程为什么算不对？》](../chapter02-sharing-data/01-from-wrong-programs)）。

实验程序咱们安排四条线程，每条把一个普通的 `long long` 加十万次：

<OnlineCompilerDemo
  title="读数全对，ThreadSanitizer 照样报 data race"
  source-path="code/examples/vol5/chapter06/02_tsan_race.cpp"
  description="演示用，存在数据竞争：四条线程各十万次自增同一个普通 long long。标准输出 counter=400000 全对，竞争报告在标准错误流。本地复跑按下方 TSan 配方编译即可看到报告。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <cstdio>
#include <thread>

long long counter = 0;

void work() {
  for (int i = 0; i < 100000; ++i) ++counter;
}

int main() {
  std::thread t[4];
  for (auto& x : t) x = std::thread(work);
  for (auto& x : t) x.join();
  std::printf("counter=%lld (expected 400000)\n", counter);
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O1 -g -fsanitize=thread -pthread tsan_race.cpp -o tsan_race   # 插桩版配方
./tsan_race
```

编译选项笔者交代一下：`-g` 带来报告里的行号，`-O1` 是手册建议的起步优化级别，`-pthread` 管线程库的链接。

咱们看标准输出，程序自己报了一行：

```text
counter=400000 (expected 400000)
```

读数全对，笔者盯着看了两遍才敢信。可标准错误流里，报告躺着（笔者删了标准库内部的帧与一长串 BuildId，留下主干）：

```text
==================
WARNING: ThreadSanitizer: data race (pid=17)
  Read of size 8 at 0x56b396dfd098 by thread T2:
    #0 work() /tmp/concurrency-u6/tsan_race.cpp:7 (tsan_race+0x1236)

  Previous write of size 8 at 0x56b396dfd098 by thread T1:
    #0 work() /tmp/concurrency-u6/tsan_race.cpp:7 (tsan_race+0x1267)

  Location is global 'counter' of size 8 at 0x56b396dfd098

  Thread T2 (tid=20, running) created by main thread at:
    #0 pthread_create <null> (libtsan.so.2+0x616c9)
    #1 std::thread::_M_start_thread(...) <null> (libstdc++.so.6+0xea961)

  Thread T1 (tid=19, finished) created by main thread at:
    #0 pthread_create <null> (libtsan.so.2+0x616c9)
    #1 std::thread::_M_start_thread(...) <null> (libstdc++.so.6+0xea961)

SUMMARY: ThreadSanitizer: data race /tmp/concurrency-u6/tsan_race.cpp:7 in work()
==================
```

报告怎么读，咱们逐行看。头一行 WARNING 给事件定性，data race，就是 2.1 判过的未定义行为源头。中间两段栈是冲突的双方：T2 的读与 T1 的写，都落在第 7 行的 `++counter` 上。Location 告诉您地址对应全局变量 `counter`，SUMMARY 一行收拢位置与函数。程序退出码是 66，手册写明的默认值，持续集成里靠它判挂。

读数全对，报告照给。插桩改变了时序，四条线程恰好挤成串行，一次更新都没丢。可竞争本身成立，TSan 不管读数好不好看。编译纪律还有一条：`-fsanitize=thread` 不能与 `-fsanitize=address` 同开（GCC 手册写明不能组合），两个检测器要各自编一版分别跑。ASan 管内存越界与悬垂访问，咱们在 2.5 用过它（[《边界交给了调用者》](../chapter02-sharing-data/05-interface-leaks)），两版测试各司其职。

## TSan 没报告，不等于没有错误

TSan 只看跑到的路径，咱们在 2.1 就见识过：它报的，是执行里真发生过的竞争。执行里真实发生过的竞争，理论上没有漏报的余地，不过工程实现为控内存，给访问历史设了上限（`TSAN_OPTIONS` 里的 `history_size`），极端场景下报告可能拿不全调用栈，压在历史上限底下的竞争，报告也可能整个不来。所以您拿它当强信号就好，别当证明。

测试覆盖不到的分支里藏着竞争，它就一声不吭了。

再看逻辑层。工具认的是内存级的 data race，逻辑上的漏洞它不判。咱们拿全原子的 check-then-act 试它一遍：

<OnlineCompilerDemo
  title="全原子的 check-then-act：TSan 不报，逻辑照错"
  source-path="code/examples/vol5/chapter06/03_logical_race.cpp"
  description="每一步都是原子操作，TSan 复跑警告为零。一万轮里仍有几十轮双赢，检查与写入之间的窗口，原子操作堵不上。本地复跑加 -fsanitize=thread。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<bool> go{false};
std::atomic<bool> reserved{false};
std::atomic<int> winners{0};

void contender() {
  while (!go.load(std::memory_order_relaxed)) {}
  if (!reserved.load(std::memory_order_relaxed)) {
    reserved.store(true, std::memory_order_relaxed);
    winners.fetch_add(1, std::memory_order_relaxed);
  }
}

int main() {
  const int kRounds = 10000;
  int double_wins = 0;
  for (int r = 0; r < kRounds; ++r) {
    reserved.store(false);
    winners.store(0);
    go.store(false);
    std::thread a(contender);
    std::thread b(contender);
    go.store(true);
    a.join();
    b.join();
    if (winners.load() == 2) ++double_wins;
  }
  std::printf("double wins: %d of %d rounds\n", double_wins, kRounds);
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O1 -g -fsanitize=thread -pthread logical_race.cpp -o logical_race   # 插桩版配方
./logical_race
```

两条线程等同一声放行，各自查 `reserved`、置位、计数，每一步都是原子操作，内存级咱们挑不出一处 data race。咱们用 TSan 编译，跑了三遍，三遍的警告都是零：

```text
double wins: 9632 of 10000 rounds
double wins: 9666 of 10000 rounds
double wins: 9675 of 10000 rounds
```

您看读数，一万轮里双赢的 9632 轮往上，两名竞争者都以为自己抢到了席位。检查与写入是两个分开的动作，中间的窗口原子操作堵不上。同样的洞咱们在 2.4 见过（[《一次操作的边界在哪里》](../chapter02-sharing-data/04-check-then-act)）。要堵它的话，得换 `compare_exchange` 一类的单步判定，CAS 的功课归 5.1（[《原子操作解决到哪一步》](../chapter05-atomic-memory-model/01-what-atomics-solve)）。

标准原语 TSan 全都认得了，mutex、atomic、条件变量都是它的熟人。自旋锁、事件计数等咱们自己手搓的同步，它就不认了，得咱们用注解接口把同步关系喂给它，喂不上的话，误报与漏报就都来了。没有插桩的第三方库同样别信，clang 手册的 Limitations 写明：全部的代码都要带 `-fsanitize=thread` 重编，不然就有漏报，误报也是一样的。

开销的事咱们也得如实交代，clang 手册给的典型数字，是慢 5 到 15 倍、内存多 5 到 10 倍。它的位置在测试与持续集成里，生产环境里咱们不放它。

## 编译期防线：Thread Safety Analysis

TSan 要等错误真的跑出来才开口。有一类错能在编译期拦下：忘了拿锁就去摸数据。Clang 的 Thread Safety Analysis 是 Clang 自家的语言扩展，官方文档开头写明 it is a C++ language extension，完全是编译期的静态检查，零运行时开销。您给数据声明它归哪把锁管，编译器逐个函数查访问。

工具链的边界当场试给您看。GCC 16.2.1 对 `-Wthread-safety` 的回答是：

```text
g++: error: unrecognized command-line option ‘-Wthread-safety’
```

同一段代码用 g++ 编，全部属性报 `attribute directive ignored`，检查等于没开。MSVC 不在本篇实测范围内，笔者不替它说话。要用它，编译器得是 Clang（本机 clang 23.1.3 实测通过），搭构建之前值得确认。

属性拼写还有一处小的边界，笔者拿本机的 clang 23.1.3 试过。`[[clang::capability("mutex")]]` 的拼写 clang 认。`[[clang::guarded_by(mu_)]]` 的拼写它就不认了，警告写的是 unknown attribute ignored，2026-11-17 实测。得用 `__attribute__` 的原形来写，本篇的示例统一用它。Google 的 mutex.h 把上面的属性包成 `GUARDED_BY` 一类宏，工程里更常见的正是宏写法，宏展开的样子就是原形，您对照着看就行。

示例的一组最小注解，咱们摆出来：

<OnlineCompilerDemo
  title="Thread Safety Analysis：忘了拿锁，编译期就拦"
  source-path="code/examples/vol5/chapter06/04_tsa_demo.cpp"
  description="clang 扩展：broken_read 没拿锁读 total_、无锁调 peek_locked，clang++ -Wthread-safety 各报一条警告（命令在下方，行号对得上）。g++ 不认这些属性，站内运行只看得到属性被忽略的编译告警。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <mutex>

class __attribute__((capability("mutex"))) TicketMutex {
 public:
  void lock() __attribute__((acquire_capability())) { m_.lock(); }
  void unlock() __attribute__((release_capability())) { m_.unlock(); }
 private:
  std::mutex m_;
};

class __attribute__((scoped_lockable)) TicketLock {
 public:
  explicit TicketLock(TicketMutex& mu) __attribute__((acquire_capability(mu)))
      : mu_(&mu) { mu_->lock(); }
  ~TicketLock() __attribute__((release_capability())) { mu_->unlock(); }
  TicketLock(const TicketLock&) = delete;
  TicketLock& operator=(const TicketLock&) = delete;
 private:
  TicketMutex* mu_;
};

class Ledger {
 public:
  void add(int n) {
    TicketLock lk(mu_);
    total_ += n;
  }
  int broken_read() {           // 忘了拿锁，等编译器拦
    return total_;
  }
  int peek_locked() __attribute__((requires_capability(mu_))) {
    return total_;
  }
 private:
  TicketMutex mu_;
  int total_ __attribute__((guarded_by(mu_))) = 0;
};

int use_bad() {
  Ledger led;
  led.add(1);
  return led.broken_read() + led.peek_locked();
}

int main() {
  return use_bad();
}
```

</OnlineCompilerDemo>

```bash
# clang 23.1.3
clang++ -std=c++20 -Wthread-safety -fsyntax-only tsa_demo.cpp
```

两条警告的原文，咱们原样摆出来：

```text
tsa_demo.cpp:29:12: warning: reading variable 'total_' requires holding mutex 'mu_' [-Wthread-safety-analysis]
   29 |     return total_;
      |            ^

tsa_demo.cpp:42:34: warning: calling function 'peek_locked' requires holding mutex 'led.mu_' exclusively [-Wthread-safety-analysis]
   42 |   return led.broken_read() + led.peek_locked();
      |                                  ^
```

`broken_read` 没拿锁就读 `total_`，警告落在函数体里。`peek_locked` 自己声明了 `requires_capability`，内部访问合法，警告挪到了调用点，谁不持锁调它，谁挨警告。`add` 走的是注解过的 RAII，`TicketLock` 构造拿锁、析构放锁，干干净净。不加 `-Wthread-safety`，检查就不跑：同一份代码一声不吭，这也是笔者实测过的。

它的边界咱们同样认下。分析只查注解过的地方，没声明的数据一概不管，声明的锁与真锁对不上，它也不知道。所以它防的是忘了锁，防不了锁错了对象。它与 TSan 是互补的一对，您分开看就好：一个查声明的纪律，一个查跑出的事实。

## 轮到您动手的时候

带走之前，您把三个问题过一遍：

1. 轮数的预算怎么定？单轮的成本越低，买得下的轮数就越多，拿窗口撑开的版本估出命中的量级，再反推普通版本要多少轮。
2. TSan 的报告读哪几处？头一行定性，中间两段栈是冲突的双方，Location 与 SUMMARY 给出位置，退出码 66 留给持续集成判挂。
3. Thread Safety Analysis 防什么、不防什么？防的是忘了锁，防不了锁错了对象，而且只查注解过的地方。

再留一个动手：把 stress 程序编两版，普通版跑 30000 轮，TSan 版跑一遍就够。三万轮零命中与一条 data race 报告摆在一起，读数您自己核。

## 参考资源

推进思路参考了 Anthony Williams《C++ Concurrency in Action》讲测试与调试的章节，借的是组织与讲法，例子和文字都是笔者自己写的，实验在 GCC 16.2.1 与 clang 23.1.3 上编译实测。工具行为的断言按下列手册与文档核对（2026-11-17 访问）。

- [GCC 的插桩选项（含 `-fsanitize=thread`）][gcc-instrument]，两个检测器不能组合的出处。
- [ThreadSanitizer（clang 手册）][clang-tsan]，用法、开销数字与默认退出码 66。
- [Thread Safety Analysis（clang 手册）][clang-tsa]，属性总表与 mutex.h 的宏写法。
- [ThreadSanitizer C++ 手册（Google sanitizers wiki）][tsan-wiki]，报告格式。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，测试与调试章节。

[gcc-instrument]: https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
[clang-tsan]: https://clang.llvm.org/docs/ThreadSanitizer.html
[clang-tsa]: https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
[tsan-wiki]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
