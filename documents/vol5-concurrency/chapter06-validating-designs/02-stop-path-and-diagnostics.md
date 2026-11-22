---
title: "停得下来吗：停止路径测试与诊断三件"
description: "异常注入与停止路径把结束路径测到，GDB、Helgrind 与结构化日志三个扩展节各认领一件诊断工具。"
chapter: 5
order: 2
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

# 停得下来吗：停止路径测试与诊断三件

竞争能不能被找出来，前一篇数完了。这一篇换一类问题：停止与失败的路径测过没有，以及 TSan 之外还有哪几件诊断工具，三个扩展节各认领一件。

## 异常注入与停止路径测试

并发程序的失败，多半藏在结束的地方。线程函数里抛出去的异常没有去处，async 任务的异常躺在 future 里等人来取，等待的循环里卡着关不掉的消费者。咱们把测试一条一条铺上去，走到头的每条路都留下读数。

线程函数的异常走不出线程，咱们做个实验：

<OnlineCompilerDemo
  title="异常逃出线程函数，进程当场被 terminate 收走"
  source-path="code/examples/vol5/chapter06/05_throw_thread.cpp"
  description="线程里抛出的 runtime_error 没人接，std::terminate 收走进程，尾部的 printf 永远到不了。运行会看到 terminate called 与 what() 消息，退出码 134。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

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

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O0 -pthread throw_thread.cpp -o throw_thread   # 普通版配方，-O0 让栈好读
./throw_thread; echo "exit=$?"
```

```text
terminate called after throwing an instance of 'std::runtime_error'
  what():  job failed
exit=134
```

退出码的来历，1.3 讲过（[《第一个 `std::thread` 程序》](../chapter01-start-concurrency/03-first-std-thread)）：128 加上 SIGABRT 的编号 6，shell 拿它报告信号的死亡，咱们这里是 134。尾部的 printf 永远到不了。异常逃出线程函数的话，std::terminate 直接收走整个进程了。所以线程函数要么自己接住异常，要么把失败递出去了再收尾。

递出去的路，4.1 咱们铺过一条（[《用 `std::async` 执行计算》](../chapter04-organizing-tasks/01-launch-with-async)）：任务抛出的异常存进共享状态，future.get() 在取结果的线程里重新抛出。失败的现场不变，咱们给它换一条递送的路：

<OnlineCompilerDemo
  title="async 任务的异常，经 future.get() 在调用线程重抛"
  source-path="code/examples/vol5/chapter06/06_async_throw.cpp"
  description="任务抛出的异常存进共享状态，main 的 catch 经 get() 接到同一个异常，消息完整，进程退出码 0。对照上一件演示，这里没人被 terminate。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

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

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O0 async_throw.cpp -o async_throw   # 普通版配方，-O0 且未加 -pthread
./async_throw; echo "exit=$?"
```

```text
caught from future: task failed
exit=0
```

失败从工作线程递到了等结果的线程，进程体面地收了场。同一类测试咱们当固定剧目来排：失败真抛出来了，接异常的那一侧，拿到的消息要完整。

注入实验咱们给失败找一个真正的入口：生产者装货装到第三件的时候抛一场异常，看消费者的一侧能不能收到通知，并且干净地退出。

<OnlineCompilerDemo
  title="异常注入：生产者第三件装货抛异常，消费者收通知退出"
  source-path="code/examples/vol5/chapter06/07_inject_fail.cpp"
  description="生产者装到第三件抛异常，置失败标志并 notify_all。消费者收讫两件后收到失败通知，干净退出，没人卡在 wait 里。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

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

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O1 -pthread inject_fail.cpp -o inject_fail   # 普通版配方，-O1 折中
./inject_fail
```

```text
consumer received 2 items, then the failure notice
```

读数是笔者想看到的形状：两件收讫了，失败的通知跟着进来，程序正常地退出了，也没有谁卡在 `wait` 里了。设计的要点有两个：失败标志与队列状态，都写进了同一个谓词里，消费者的两条出路（来货、失败）在同一个 `wait` 里等。抛异常的一侧置位标志之后 `notify_all`，把所有等着的人都叫醒，漏掉了一个，就多一个挂起的线程。

停止路径咱们用 C++20 的停止协议来测。1.6 立过停止三件套（`stop_source`、`stop_token`、`stop_callback`，[《让工作有序结束》](../chapter01-start-concurrency/06-jthread-and-stopping)），3.6 讲过等待的结束方式（[《等待也需要结束路径》](../chapter03-wait-and-coordinate/06-ending-a-wait)），`condition_variable_any` 的 `wait` 能收 `stop_token`（C++20 起，见 [cppreference][cppref-cv-wait]），停止请求一到它就被从睡梦里叫醒了：

<OnlineCompilerDemo
  title="停止路径：request_stop 与 close 都能把等待叫醒"
  source-path="code/examples/vol5/chapter06/08_stop_paths.cpp"
  description="condition_variable_any 的 wait 收 stop_token：B 被 request_stop 叫醒，A 被置位 closed 加 notify_all 叫醒，两条路的退出耗时都是毫秒级读数。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

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

</OnlineCompilerDemo>

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

两条路咱们都走到了：B 被 `request_stop` 叫醒了，A 被 `close` 叫醒了，两条等待的退出都是零毫秒。这里的 close，指的是把 closed 置位、再 notify_all 的收尾动作。来货的路，前面注入实验的生产者程序已经走过，失败、停止、关闭三种到这里都触发过了。等待的结束方式里还有超时，归 3.6 的射程（[《等待也需要结束路径》](../chapter03-wait-and-coordinate/06-ending-a-wait)），本篇就不重跑了。谓词里写过的条件，咱们把到得了的都触发了一遍，量它花了几毫秒退出，超了预算就说明有等待关不掉。

## 死锁的运行时诊断：GDB 附加与线程调用栈（扩展）

本节带（扩展）的标记，跳过它不影响本篇主线。程序已经卡死的时候，咱们别猜，看现场。朴素的做法，咱们摆出来：用 `ps` 找到卡死进程的 PID，咱们拿 `gdb -p PID` 附加进去，再用 `thread apply all bt`，把全部线程的调用栈一次性地打出来。两份栈并排地放着，谁等谁的问题，您一眼就能断。

实验程序咱们搬来 2.6 里 AB-BA 反转的最小版（[《谁也不松手，怎么办》](../chapter02-sharing-data/06-deadlock-defenses)）：

<OnlineCompilerDemo
  title="AB-BA 反转的最小死锁现场"
  source-path="code/examples/vol5/chapter06/09_deadlock.cpp"
  description="演示用，故意死锁：两条线程以相反次序拿两把锁，程序卡住不退出，正好留给 GDB 附加看线程栈（命令在下方）。站内运行会一直挂到超时，本地复跑请配 timeout。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

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

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O0 -g -pthread deadlock.cpp -o deadlock   # 普通版配方，-O0 加 -g，栈好读
./deadlock &            # 卡住不退出
ps aux | grep deadlock  # 找到 PID
gdb -p <PID> -batch -ex "thread apply all bt"
```

这里得跟您如实声明：写稿环境的 ptrace 受限，沙箱不放行 ptrace 附加，gdb 报了 Operation not permitted，命令没能在笔者这里跑通。判读的方法照 GDB 手册走，您在自己的终端里跑一遍，真凭据就有了。预期的形状笔者描述给您：两个工作线程的栈顶，都停在 mutex 加锁的内核等待里（glibc 的 `__lll_lock_wait` 一类），往下一帧就是各自的 work 函数。两边的栈都停在加锁上，对照源码里两次加锁的顺序，一边按 A 后 B 的次序拿，另一边按 B 后 A 的次序拿，对上了，死锁的现场就成立了。修复的方向 2.6 给过：std::lock 一把全拿的路子，或者分层的锁序。

## Helgrind：Valgrind 的线程检测（扩展）

本节带（扩展）的标记，跳过它不影响本篇主线。现成的二进制拿过来就能查，值得您认识一下。TSan 的路要重编，Helgrind 就不用了。它是 Valgrind 工具集里的线程检测器，走动态二进制插桩的路子，咱们拿现成的二进制就能跑：

```bash
valgrind --tool=helgrind ./your_program
```

手册列了三类检查。头一类是 pthread 接口的误用，解锁没锁过的锁、销毁带锁的锁都算。第二类是锁顺序的不一致，它的内部维护一张锁顺序图，图里出现了环，就报潜在的死锁，死锁哪怕一次都没真的发生。数据竞争的判定是第三类，靠的是 happens-before 加锁集。咱们拿两条线程各加两万次的小程序实测：

<OnlineCompilerDemo
  title="Helgrind 的口粮：两条线程各加两万次的竞争"
  source-path="code/examples/vol5/chapter06/10_hg_race.cpp"
  description="演示用，存在数据竞争：普通 long long 被两条线程自增，读数 counter=40000 全对，Valgrind Helgrind 按下方命令报 possible data race。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

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

</OnlineCompilerDemo>

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

形状与 TSan 的报告同构，都是两段栈加双方当时持有的锁与位置，报告里要能对上行号，靠的是编译时带上的 -g。代价的差别就大了：它的每条指令都要过一遍翻译，慢得不是一星半点，手册自己把性能列进了待办清单，承认慢上两个数量级也不是稀罕事。自定义的同步同样要喂注解，接口在 helgrind.h 的头文件里，名字都带 ANNOTATE 的字样。选型上笔者站 TSan 一边，拿得到源码的话就用它，插桩的开销小一截。拿不到源码的话，或者想赶在死锁发生前查一查锁的顺序，那就用 Helgrind 了。

## 结构化日志：让输出可追踪（扩展）

本节带（扩展）的标记，跳过它不影响本篇主线。不过它解决的问题，您多半马上就会遇到。查并发的问题时，很多人的第一反应是抓起 printf 就上，有三件事是咱们要认下的。

数据竞争排在头一件，咱们看它：同步地使用标准流对象，标准条款写明了它不会引入数据竞争（出处见参考资源的标准草案链接），cppreference 的 cout 页写着同一件事。

字符交错排在第二件：条款的注补了一句，不同步的使用会让字符交错，一行字被别的线程撕成两截，是真实会发生的。即便您一直同步着用，一条日志分了几次 `<<` 才写完，两次写的中间，别人照样能插进来。

排在第三件，也最麻烦：裸的输出没有时间戳，线程的编号也没有，两行字谁早谁晚，咱们对不上。

最小的方案，咱们立三条。每行的日志在局部拼成完整的一串，写的时候只加一次锁。时间戳用 `steady_clock` 的纳秒数，线程的标识也带上。格式化留在锁的外面，锁里只做一次写出的动作，等的人排的是写出，排版的事不归锁管。

<OnlineCompilerDemo
  title="结构化日志：整行拼好、锁里只做一次写出"
  source-path="code/examples/vol5/chapter06/11_logger.cpp"
  description="每行日志带 steady_clock 纳秒时间戳与线程号，格式化在锁外、写出在锁内，两条线程各打三行，行行完整不撕开。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

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

</OnlineCompilerDemo>

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

提醒一句：时间戳是本机的记录，当不得标准意义上的次序证据。判断可见性，还是回 5.3 数桥。日志帮咱们把现场串起来，判定的依据仍然是桥。

## 轮到您动手的时候

带走之前，您把两个问题过一遍：

1. 谓词里的条件怎么逐个触发？空着的队列、来货、关闭、停止，一个一个造出来，量每次退出花了几毫秒。
2. Helgrind 与 TSan 怎么选？拿得到源码就用 TSan，拿不到源码或者想提前查锁顺序，就用 Helgrind。

## 参考资源

推进思路参考了 Anthony Williams《C++ Concurrency in Action》讲测试与调试的章节，借的是组织与讲法，例子和文字都是笔者自己写的，实验在 GCC 16.2.1 与 clang 23.1.3 上编译实测。工具行为的断言按下列手册与文档核对（2026-11-17 访问）。

- [Helgrind 手册（Valgrind）][hg-manual]，三类检查、锁顺序图与 ANNOTATE 注解。
- [GDB 在线手册][gdb-manual]，`thread apply all bt` 的出处。
- [condition_variable_any::wait（cppreference）][cppref-cv-wait]，带 `stop_token` 的等待（C++20）。
- [std::cout（cppreference）][cppref-cout]，并发访问标准流的安全性说明。
- [现行标准草案：Standard iostream objects][draft-iostream]，不引入数据竞争的条款与字符交错的注。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，测试与调试章节。

[hg-manual]: https://valgrind.org/docs/manual/hg-manual.html
[gdb-manual]: https://sourceware.org/gdb/onlinedocs/gdb/
[cppref-cv-wait]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[cppref-cout]: https://en.cppreference.com/w/cpp/io/cout
[draft-iostream]: https://eel.is/c++draft/iostream.objects.overview
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
