---
title: “怎样寻找并发错误”
description: “重复测试给出轮数与命中率，ThreadSanitizer 给出报告，Thread Safety Analysis 把检查搬进编译期，异常注入与停止路径把结束路径测到，三个扩展节补 GDB、Helgrind 与结构化日志。”
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

# 怎样寻找并发错误

前五个单元教的是怎么写对：锁怎么配、等待怎么落、桥怎么数。第五单元收尾的时候，happens-before 在咱们手里已经成了一座桥，一座可以逐节检查的桥（[《一个线程怎样观察另一个线程的修改》](../chapter05-atomic-memory-model/02-happens-before)）。可桥要人一条一条地去数，代码一大、线程一多的时候，数漏了一节，错就藏进去了。到了第六单元，咱们换一个问题：怎么证明自己写对了。本篇的任务是找错。量收益的任务落在下一篇 [《怎样衡量并发带来的收益》](02-measuring-benefits)手里，它已经成篇。从测量回到设计的收尾是 6.3，入口在 [《怎样调整设计》](03-adjusting-design)。

5.3 收尾留过一句话（见 [《选择内存序需要什么依据》](../chapter05-atomic-memory-model/03-choosing-memory-orders)），说本机看不出的事，工具替您把证据拿到手。兑现的事，本篇就把工具一个个请出来。手段我按三类铺开：一类是重复压的办法，咱们把同一份程序按轮数往机器上送，看偶发的现形落在哪个轮次。另一类是工具查的办法，让插桩与静态检查，替咱们扫跑不到、看不出的地方。异常注入与停止路径的测试是第三类，它压的对象不一样，压的是程序结束的那些小路。三个扩展节留给了运行时的诊断，GDB 附加、Helgrind、结构化日志就是里面的三件。

文中的程序都放在临时目录里（`/tmp/concurrency-u6/`），编译跑过（GCC 16.2.1，普通版配方 `g++ -std=c++20 -O2 -pthread`，插桩版配方 `g++ -std=c++20 -O1 -g -fsanitize=thread -pthread`，WSL2 内核 6.18.33.2，16 逻辑核，2026-11-17）。每个用 gcc 编的实验，我都标了用的是哪一组配方。Thread Safety Analysis 一节，用 clang 23.1.3 实测，GCC 对它的态度我也当场试给您看。写稿环境的 ptrace 受限，GDB 附加没能在我这里跑通，扩展节里如实注明，没有编造输出。涉及工具行为的断言，按 clang 手册、GCC 手册与 Valgrind 手册核对过，链接收在参考资源里。输出都是单次运行的读数，换台机器，数字会动。

## 重复测试：把偶发换成读数

单线程程序里，一次失败就能断言有问题。并发程序倒过来：一次全对说明不了什么，因为调度器交出来的只是众多交错里的一个样本。2.1 里有一份现成的读数（[《从错误程序认识竞争》](../chapter02-sharing-data/01-from-wrong-programs)）：没上锁的计数器编成 `-O2` 后 30 轮全对，同一份源码拿 TSan 一查，data race 照报。30 轮全对，竞争照在。偶然成功当不得正确性依据，咱们拿实验把道理变成能摸的读数。

咱们做一个更细的实验。两条线程，各把一个普通 `int` 加一次，一轮下来 `counter` 应当恰好等于 2。tight 档把读写挤在一起，wide 档把动作摊开：读进局部变量、睡 50 微秒、再写回。窗口宽度不同，命中机会就不同，读数您往下看。

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

wide 档的读数出来，我心里就有底了：窗口撑开，偶发真的会现形。两遍分别丢了 2979 轮与 2993 轮，几乎轮轮有失。tight 档我跑了三遍，三万轮里咱们没见它露过一次面，摊到普通测试的 30 轮上，抓到它的机会约等于零。可抓不到不等于没有，TSan 一节咱们拿同一类程序再验一遍。

压多少轮的取舍，我按预算与观测来定，不存在一个统一的数字。单轮的成本越低，买得下的轮数就越多。咱们拿窗口撑开的版本估出单轮命中的量级，再反推普通版本要多少轮才可能见一次失败。每轮的结果都要记下来，记下哪一轮、丢了多少，失败真的来了，咱们才查得回去。持续集成里可以放一个循环的用例，同一份程序咱们连跑几百上千轮，只要错了一轮，构建就算挂了。

压力设计本身也有讲究，我把做过的几件事摆成清单：

1. 线程数变着跑：2 条、4 条、核数顶格，各自给调度器不同的交错空间。
2. 窗口试着撑开，像 wide 档一样在读写之间垫一段延迟，偶发立刻变成常态。
3. 负载也值得一变，空载与满载的读数会差出一截。
4. 判定落在不变量上：总数守恒、没有重复、没有遗漏，错没错当场见分晓。
5. 复现条件原样记下：线程数、迭代数、延迟量、机器与负载，下次才好把失败请回来。

## ThreadSanitizer：把跑到的竞争报出来

TSan 的原理我一句话摆出来：编译时在每个内存访问处插桩，运行时库给每次访问记下线程与同步事件。两次访问碰在同一地址，它就拿 happens-before 去数桥，数不出桥、又有一方是写，报告就来了。判定语言与 5.2 同源，所以报告读起来不该陌生。GCC 与 Clang 都带它，选项是 `-fsanitize=thread`，咱们在 2.1 与它初见（[《从错误程序认识竞争》](../chapter02-sharing-data/01-from-wrong-programs)）。

实验程序咱们安排四条线程，每条把一个普通的 `long long` 加十万次：

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

```bash
g++ -std=c++20 -O1 -g -fsanitize=thread -pthread tsan_race.cpp -o tsan_race   # 插桩版配方
./tsan_race
```

编译选项我交代一下：`-g` 带来报告里的行号，`-O1` 是手册建议的起步优化级别，`-pthread` 管线程库的链接。

咱们看标准输出，程序自己报了一行：

```text
counter=400000 (expected 400000)
```

读数全对，我盯着看了两遍才敢信。可标准错误流里，报告躺着（我删了标准库内部的帧与一长串 BuildId，留下主干）：

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

读数全对，报告照给。插桩改变了时序，四条线程恰好挤成串行，一次更新都没丢。可竞争本身成立，TSan 不管读数好不好看。编译纪律还有一条：`-fsanitize=thread` 不能与 `-fsanitize=address` 同开（GCC 手册写明不能组合），两个检测器要各自编一版分别跑。ASan 管内存越界与悬垂访问，咱们在 2.3 用过它（[《一次操作的边界在哪里》](../chapter02-sharing-data/03-check-then-act)），两版测试各司其职。

## TSan 没报告，不等于没有错误

TSan 只看跑到的路径，这点咱们在 2.1 就见识过：它报的，是执行里真发生过的竞争。执行里真实发生过的竞争，理论上没有漏报的余地，不过工程实现为控内存，给访问历史设了上限（`TSAN_OPTIONS` 里的 `history_size`），极端场景下报告可能拿不全调用栈，压在历史上限底下的竞争，报告也可能整个不来。所以您拿它当强信号就好，别当证明。

2.1 里 `-O2` 的读数是反面的好例子，您还记得：30 轮全对它照样报，因为竞争真的发生了。反过来，测试覆盖不到的分支里藏着竞争，它就一声不吭了。

再看逻辑层。工具认的是内存级的 data race，逻辑上的漏洞它不判。咱们拿全原子的 check-then-act 试它一遍：

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

您看读数，一万轮里双赢的 9632 轮往上，两名竞争者都以为自己抢到了席位。检查与写入是两个分开的动作，中间的窗口原子操作堵不上。同样的洞咱们在 2.3 见过（[《一次操作的边界在哪里》](../chapter02-sharing-data/03-check-then-act)）。要堵它的话，得换 `compare_exchange` 一类的单步判定，CAS 的功课归 5.1（[《原子操作解决到哪一步》](../chapter05-atomic-memory-model/01-what-atomics-solve)）。

标准原语 TSan 全都认得了，mutex、atomic、条件变量都是它的熟人。自旋锁、事件计数这些咱们自己手搓的同步，它就不认了，得咱们用注解接口把同步关系喂给它，喂不上的话，误报与漏报就都来了。没有插桩的第三方库同样别信，clang 手册的 Limitations 写明：全部的代码都要带 `-fsanitize=thread` 重编，不然就有漏报，误报也是一样的。

开销的事咱们也得如实交代，clang 手册给的典型数字，是慢 5 到 15 倍、内存多 5 到 10 倍。它的位置在测试与持续集成里，生产环境里咱们不放它。

## 编译期防线：Thread Safety Analysis

TSan 要等错误真的跑出来才开口。有一类错能在编译期拦下：忘了拿锁就去摸数据。Clang 的 Thread Safety Analysis 是语言扩展，不是 C++ 标准，官方文档开头写明 it is a C++ language extension，完全是编译期的静态检查，零运行时开销。您给数据声明它归哪把锁管，编译器逐个函数查访问。

工具链的边界当场试给您看。GCC 16.2.1 对 `-Wthread-safety` 的回答是：

```text
g++: error: unrecognized command-line option ‘-Wthread-safety’
```

同一段代码用 g++ 编，全部属性报 `attribute directive ignored`，检查等于没开。MSVC 不在本篇实测范围内，我不替它说话。要用它，编译器得是 Clang（本机 clang 23.1.3 实测通过），搭构建之前值得确认。

属性拼写还有一处小的边界，我拿本机的 clang 23.1.3 试过。`[[clang::capability("mutex")]]` 的拼写 clang 认。`[[clang::guarded_by(mu_)]]` 的拼写它就不认了，警告写的是 unknown attribute ignored，2026-11-17 实测。得用 `__attribute__` 的原形来写，本篇的示例统一用它。Google 的 mutex.h 把这些属性包成 `GUARDED_BY` 一类宏，工程里更常见的正是宏写法，宏展开的样子就是原形，您对照着看就行。

示例的一组最小注解，咱们摆出来：

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
```

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

`broken_read` 没拿锁就读 `total_`，警告落在函数体里。`peek_locked` 自己声明了 `requires_capability`，内部访问合法，警告挪到了调用点，谁不持锁调它，谁挨警告。`add` 走的是注解过的 RAII，`TicketLock` 构造拿锁、析构放锁，干干净净。不加 `-Wthread-safety`，检查就不跑：同一份代码一声不吭，这也是我实测过的。

它的边界咱们同样认下。分析只查注解过的地方，没声明的数据一概不管，声明的锁与真锁对不上，它也不知道。所以它防的是忘了锁，防不了锁错了对象。它与 TSan 是互补的一对，您分开看就好：一个查声明的纪律，一个查跑出的事实。

## 异常注入与停止路径测试

并发程序的失败，多半藏在结束的地方。线程函数里抛出去的异常没有去处，async 任务的异常躺在 future 里等人来取，等待的循环里卡着关不掉的消费者。咱们把测试一条一条铺到这些小路上，走到头的每条路都留下读数。

线程函数的异常走不出线程，咱们做个实验：

```cpp
#include <cstdio>
#include <stdexcept>
#include <thread>

int main() {
  std::thread t([] { throw std::runtime_error("job failed"); });
  t.join();
  std::printf("never reached\n");
  return 0;
}
```

```bash
g++ -std=c++20 -O0 -pthread throw_thread.cpp -o throw_thread   # 普通版配方，-O0 让栈好读
./throw_thread; echo "exit=$?"
```

```text
terminate called after throwing an instance of 'std::runtime_error'
  what():  job failed
exit=134
```

退出码的来历，1.3 里算过这笔数（[《第一个 `std::thread` 程序》](../chapter01-start-concurrency/03-first-std-thread)）：128 加上 SIGABRT 的编号 6，shell 拿它报告信号的死亡，咱们这里是 134。尾部的 printf 永远到不了。异常逃出线程函数的话，std::terminate 直接收走整个进程了。所以线程函数要么自己接住异常，要么把失败递出去了再收尾。

递出去的路，4.1 咱们铺过一条（[《用 `std::async` 执行计算》](../chapter04-organizing-tasks/01-launch-with-async)）：任务抛出的异常存进共享状态，future.get() 在取结果的线程里重新抛出。失败的现场不变，咱们给它换一条递送的路：

```cpp
#include <cstdio>
#include <future>
#include <stdexcept>

int main() {
  auto f = std::async(std::launch::async,
                      []() -> int { throw std::runtime_error("task failed"); });
  try {
    f.get();
  } catch (const std::runtime_error& e) {
    std::printf("caught from future: %s\n", e.what());
  }
  return 0;
}
```

```bash
g++ -std=c++20 -O0 async_throw.cpp -o async_throw   # 普通版配方，-O0 且未加 -pthread
./async_throw; echo "exit=$?"
```

```text
caught from future: task failed
exit=0
```

失败从工作线程递到了等结果的线程，进程体面地收了场。这项测试咱们当固定剧目来排：失败真抛出来了，接异常的那一侧，拿到的消息要完整。

注入实验咱们给失败找一个真正的入口：生产者装货装到第三件的时候抛一场异常，看消费者的一侧能不能收到通知，并且干净地退出。

```cpp
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

std::deque<int> items;
std::mutex m;
std::condition_variable cv;
bool closed = false;
bool failed = false;

int main() {
  std::thread producer([] {
    try {
      for (int i = 1; i <= 5; ++i) {
        if (i == 3) throw std::runtime_error("device busy while packing item 3");
        {
          std::lock_guard lk(m);
          items.push_back(i);
        }
        cv.notify_one();
      }
    } catch (...) {
      std::lock_guard lk(m);
      failed = true;
      cv.notify_all();
    }
  });
  std::thread consumer([] {
    int got = 0;
    for (;;) {
      std::unique_lock lk(m);
      cv.wait(lk, [] { return !items.empty() || failed; });
      if (!items.empty()) {
        items.pop_front();
        ++got;
        continue;
      }
      std::printf("consumer received %d items, then the failure notice\n", got);
      break;
    }
  });
  producer.join();
  consumer.join();
  return 0;
}
```

```bash
g++ -std=c++20 -O1 -pthread inject_fail.cpp -o inject_fail   # 普通版配方，-O1 折中
./inject_fail
```

```text
consumer received 2 items, then the failure notice
```

读数是我想看到的形状：两件收讫了，失败的通知跟着进来，程序正常地退出了，也没有谁卡在 `wait` 里了。设计的要点有两个：失败标志与队列状态，都写进了同一个谓词里，消费者的两条出路（来货、失败）在同一个 `wait` 里等。抛异常的一侧置位标志之后 `notify_all`，把所有等着的人都叫醒，漏掉了一个，就多一个挂起的线程。

停止路径咱们用 C++20 的停止协议来测。1.5 立过停止三件套（`stop_source`、`stop_token`、`stop_callback`，[《让工作有序结束》](../chapter01-start-concurrency/05-jthread-and-stopping)），3.3 讲过等待的结束方式（[《等待也需要结束路径》](../chapter03-wait-and-coordinate/03-ending-a-wait)），`condition_variable_any` 的 `wait` 能收 `stop_token`（C++20 起，见 [cppreference][cppref-cv-wait]），停止请求一到它就被从睡梦里叫醒了：

```cpp
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

std::deque<int> items;
std::mutex m;
std::condition_variable_any cv_any;
bool closed = false;

static std::string exit_reason(bool stop_seen) {
  if (stop_seen) return "stop requested";
  if (closed) return "closed";
  return "items left";
}

int main() {
  std::jthread b([](std::stop_token st) {
    std::unique_lock lk(m);
    cv_any.wait(lk, st, [&] { return !items.empty() || closed; });
    std::printf("consumer B exits on: %s\n",
                exit_reason(st.stop_requested()).c_str());
  });
  std::jthread a([](std::stop_token st) {
    std::unique_lock lk(m);
    cv_any.wait(lk, st, [&] { return !items.empty() || closed; });
    std::printf("consumer A exits on: %s\n",
                exit_reason(st.stop_requested()).c_str());
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto t0 = std::chrono::steady_clock::now();
  b.request_stop();
  b.join();
  auto ms_b = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  std::printf("request_stop() to B exit: %lld ms\n", (long long)ms_b);

  auto t1 = std::chrono::steady_clock::now();
  {
    std::lock_guard lk(m);
    closed = true;
  }
  cv_any.notify_all();
  a.join();
  auto ms_a = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t1).count();
  std::printf("close() to A exit: %lld ms\n", (long long)ms_a);
  return 0;
}
```

```bash
g++ -std=c++20 -O1 -g -pthread stop_paths.cpp -o stop_paths   # 普通版配方，-O1 折中，-g 让栈好定位
./stop_paths
```

```text
consumer B exits on: stop requested
request_stop() to B exit: 0 ms
consumer A exits on: closed
close() to A exit: 0 ms
```

两条路咱们都走到了：B 被 `request_stop` 叫醒了，A 被 `close` 叫醒了，两条等待的退出都是零毫秒。这里的 close，指的是把 closed 置位、再 notify_all 的收尾动作。来货的路，前面注入实验的生产者程序已经走过，失败、停止、关闭三种到这里都触发过了。等待的结束方式里还有超时，归 3.3 的射程（[《等待也需要结束路径》](../chapter03-wait-and-coordinate/03-ending-a-wait)），本篇就不重跑了。谓词里写过的条件，咱们把到得了的都触发了一遍，量它花了几毫秒退出，超了预算就说明有等待关不掉。

## 死锁的运行时诊断：GDB 附加与线程调用栈（扩展）

本节带（扩展）的标记，跳过它不影响本篇主线。程序已经卡死的时候，咱们别猜，看现场。朴素的做法，咱们摆出来：用 `ps` 找到卡死进程的 PID，咱们拿 `gdb -p PID` 附加进去，再用 `thread apply all bt`，把全部线程的调用栈一次性地打出来。两份栈并排地放着，谁等谁的问题，您一眼就能断。

实验程序咱们搬来 2.4 里 AB-BA 反转的最小版（[《减少共享与避免死锁》](../chapter02-sharing-data/04-sharing-less-and-deadlock)）：

```cpp
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

std::mutex ma;
std::mutex mb;

void work_a() {
  std::lock_guard<std::mutex> la(ma);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::lock_guard<std::mutex> lb(mb);   // A 后 B 的次序
  std::printf("a got both\n");
}

void work_b() {
  std::lock_guard<std::mutex> lb(mb);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::lock_guard<std::mutex> la(ma);   // B 后 A 的次序
  std::printf("b got both\n");
}

int main() {
  std::thread a(work_a);
  std::thread b(work_b);
  a.join();
  b.join();
  return 0;
}
```

```bash
g++ -std=c++20 -O0 -g -pthread deadlock.cpp -o deadlock   # 普通版配方，-O0 加 -g，栈好读
./deadlock &            # 卡住不退出
ps aux | grep deadlock  # 找到 PID
gdb -p <PID> -batch -ex "thread apply all bt"
```

这里得跟您如实声明：写稿环境的 ptrace 受限，沙箱不放行 ptrace 附加，gdb 报了 Operation not permitted，命令没能在我这里跑通。判读的方法照 GDB 手册走，您在自己的终端里跑一遍，真凭据就有了。预期的形状我描述给您：两个工作线程的栈顶，都停在 mutex 加锁的内核等待里（glibc 的 `__lll_lock_wait` 一类），往下一帧就是各自的 work 函数。两边的栈都停在加锁上，对照源码里两次加锁的顺序，一边按 A 后 B 的次序拿，另一边按 B 后 A 的次序拿，对上了，死锁的现场就成立了。修复的方向 2.4 给过：std::lock 一把全拿的路子，或者分层的锁序。

## Helgrind：Valgrind 的线程检测（扩展）

本节带（扩展）的标记，跳过它不影响本篇主线。现成的二进制拿过来就能查，值得您认识一下。TSan 的路要重编，Helgrind 就不用了。它是 Valgrind 工具集里的线程检测器，走动态二进制插桩的路子，咱们拿现成的二进制就能跑：

```bash
valgrind --tool=helgrind ./your_program
```

手册列了三类检查。头一类是 pthread 接口的误用，解锁没锁过的锁、销毁带锁的锁都算。第二类是锁顺序的不一致，它的内部维护一张锁顺序图，图里出现了环，就报潜在的死锁，死锁哪怕一次都没真的发生。数据竞争的判定是第三类，靠的是 happens-before 加锁集。咱们拿两条线程各加两万次的小程序实测：

```cpp
#include <cstdio>
#include <thread>

long long counter = 0;

void work() {
  for (int i = 0; i < 20000; ++i) ++counter;
}

int main() {
  std::thread a(work);
  std::thread b(work);
  a.join();
  b.join();
  std::printf("counter=%lld (expected 40000)\n", counter);
  return 0;
}
```

```bash
g++ -std=c++20 -O1 -g -pthread hg_race.cpp -o hg_race   # 普通版配方，-O1 折中，-g 对行号
valgrind --tool=helgrind ./hg_race
```

本机的 Valgrind 3.25.1，咱们看报告节选：

```text
==86131== Possible data race during read of size 8 at 0x4004068 by thread #3
==86131== Locks held: none
==86131==    at 0x40011C0: work() (hg_race.cpp:7)
==86131==    by 0x40013CC: _M_invoke<0> (std_thread.h:303)
...
==86131== This conflicts with a previous write of size 8 by thread #2
==86131== Locks held: none
==86131==    at 0x40011EC: work() (hg_race.cpp:7)
```

形状与 TSan 的报告同构，都是两段栈加双方当时持有的锁与位置，报告里要能对上行号，靠的是编译时带上的 -g。代价的差别就大了：它的每条指令都要过一遍翻译，慢得不是一星半点，手册自己把性能列进了待办清单，承认慢上两个数量级也不是稀罕事。自定义的同步同样要喂注解，接口在 helgrind.h 的头文件里，名字都带 ANNOTATE 的字样。选型上我站 TSan 一边，拿得到源码的话就用它，插桩的开销小一截。拿不到源码的话，或者想赶在死锁发生前查一查锁的顺序，那就用 Helgrind 了。

## 结构化日志：让输出可追踪（扩展）

本节带（扩展）的标记，跳过它不影响本篇主线。不过它解决的问题，您多半马上就会遇到。查并发的问题时，很多人的第一反应是抓起 printf 就上，有三件事是咱们要认下的。

数据竞争排在头一件，咱们看它：同步地使用标准流对象，标准条款写明了它不会引入数据竞争（出处见参考资源的标准草案链接），cppreference 的 cout 页写着同一件事。

字符交错排在第二件：条款的注补了一句，不同步的使用会让字符交错，一行字被别的线程撕成两截，是真实会发生的。即便您一直同步着用，一条日志分了几次 `<<` 才写完，两次写的中间，别人照样能插进来。

排在第三件，也最麻烦：裸的输出没有时间戳，线程的编号也没有，两行字谁早谁晚，咱们对不上。

最小的方案，咱们立三条。每行的日志在局部拼成完整的一串，写的时候只加一次锁。时间戳用 `steady_clock` 的纳秒数，线程的标识也带上。格式化留在锁的外面，锁里只做一次写出的动作，等的人排的是写出，排版的事不归锁管。

```cpp
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

std::mutex log_mu;

static long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

static void log_line(const std::string& event) {
  std::ostringstream line;
  line << '[' << std::setw(18) << now_ns() << " ns] [thread "
       << std::this_thread::get_id() << "] " << event << '\n';
  std::string s = line.str();
  std::lock_guard<std::mutex> lk(log_mu);
  std::fwrite(s.data(), 1, s.size(), stdout);
}

int main() {
  std::thread t1([] {
    log_line("t1 begin");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t1 halfway");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t1 done");
  });
  std::thread t2([] {
    log_line("t2 begin");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t2 halfway");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t2 done");
  });
  t1.join();
  t2.join();
  log_line("main: both workers joined");
  return 0;
}
```

```bash
g++ -std=c++20 -O2 -pthread logger.cpp -o logger   # 普通版配方
./logger
```

```text
[     6448665790006 ns] [thread 127378770622144] t1 begin
[     6448665823209 ns] [thread 127378762229440] t2 begin
[     6448670917121 ns] [thread 127378762229440] t2 halfway
[     6448670922732 ns] [thread 127378770622144] t1 halfway
[     6448676026944 ns] [thread 127378762229440] t2 done
[     6448676026954 ns] [thread 127378770622144] t1 done
[     6448676109375 ns] [thread 127378778429312] main: both workers joined
```

您看读数，里面的故事不少：t2 的 halfway 印在 6448670917121，t1 的 halfway 印在 6448670922732，中间差了约 5600 纳秒的间隔，两条线程各差半拍地错开跑。每行都是完整的，被撕开的一行也没有。日志要写进文件的时候同理，整行整块地写出，别让两条线程共用一个不加锁的流。量大了再考虑 C++20 的 `std::osyncstream`，本篇就不展开它了。

提醒一句：时间戳是本机的记录，不是标准的顺序语言。判断可见性，还是回 5.2 数桥。日志帮咱们把现场串起来，判定的依据仍然是桥。

## 轮到您动手的时候

带走之前，您把五个问题过一遍：

1. 轮数的预算怎么定？单轮的成本越低，买得下的轮数就越多，拿窗口撑开的版本估出命中的量级，再反推普通版本要多少轮。
2. TSan 的报告读哪几处？头一行定性，中间两段栈是冲突的双方，Location 与 SUMMARY 给出位置，退出码 66 留给持续集成判挂。
3. Thread Safety Analysis 防什么、不防什么？防的是忘了锁，防不了锁错了对象，而且只查注解过的地方。
4. 谓词里的条件怎么逐个触发？空着的队列、来货、关闭、停止，一个一个造出来，量每次退出花了几毫秒。
5. Helgrind 与 TSan 怎么选？拿得到源码就用 TSan，拿不到源码或者想提前查锁顺序，就用 Helgrind。

## 下一步

找错的手段，咱们铺到这里了，收个尾：

- 轮数与命中率，归重复压管。
- 竞争的报告归 TSan 管，声明的纪律归 Thread Safety Analysis 管。
- 结束的小路，归异常注入与停止路径的测试管。
- 时间线，归扩展节里的结构化日志管。

下一篇 [《怎样衡量并发带来的收益》](02-measuring-benefits)已经在等您，它把同一套较真搬到性能上，串行的基线、测量的条件、任务的粒度，都写进了它的清单。

翻页之前留一个动手：把 stress 程序编两版，普通版跑 30000 轮，TSan 版跑一遍就够。三万轮零命中与一条 data race 报告摆在一起，读数您自己核。

## 参考资源

推进思路参考了 Anthony Williams《C++ Concurrency in Action》讲测试与调试的章节，借的是组织与讲法，例子和文字都是我自己写的，实验在 GCC 16.2.1 与 clang 23.1.3 上编译实测。工具行为的断言按下列手册与文档核对（2026-11-17 访问）。

- [GCC 的插桩选项（含 `-fsanitize=thread`）][gcc-instrument]，两个检测器不能组合的出处。
- [ThreadSanitizer（clang 手册）][clang-tsan]，用法、开销数字与默认退出码 66。
- [Thread Safety Analysis（clang 手册）][clang-tsa]，属性总表与 mutex.h 的宏写法。
- [ThreadSanitizer C++ 手册（Google sanitizers wiki）][tsan-wiki]，报告格式。
- [Helgrind 手册（Valgrind）][hg-manual]，三类检查、锁顺序图与 ANNOTATE 注解。
- [GDB 在线手册][gdb-manual]，`thread apply all bt` 的出处。
- [condition_variable_any::wait（cppreference）][cppref-cv-wait]，带 `stop_token` 的等待（C++20）。
- [std::cout（cppreference）][cppref-cout]，并发访问标准流的安全性说明。
- [现行标准草案：Standard iostream objects][draft-iostream]，不引入数据竞争的条款与字符交错的注。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，测试与调试章节。

[gcc-instrument]: https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
[clang-tsan]: https://clang.llvm.org/docs/ThreadSanitizer.html
[clang-tsa]: https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
[tsan-wiki]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
[hg-manual]: https://valgrind.org/docs/manual/hg-manual.html
[gdb-manual]: https://sourceware.org/gdb/onlinedocs/gdb/
[cppref-cv-wait]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[cppref-cout]: https://en.cppreference.com/w/cpp/io/cout
[draft-iostream]: https://eel.is/c++draft/iostream.objects.overview
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
