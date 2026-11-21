---
title: "等一个条件：从轮询到条件变量"
description: "从 sleep 轮询的代价量起，把等待交给条件变量：醒来不等于条件成立的虚假唤醒、通知发在等待之前的丢失唤醒，各留一个现场，谓词与锁为什么缺一不可。"
chapter: 5
order: 1
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

# 等一个条件：从轮询到条件变量

## 转圈：轮询的代价

笔者搭了个最小现场，把转圈量了一遍：一条线程算 100 毫秒的活，算完把 `done` 立起来，另一条线程就等 `done`。四种等法各写成一个回调，交给同一套架子。同一份程序跑两遍，每遍四种等法各跑一次。

<OnlineCompilerDemo
  title="轮询的代价：空转、两种睡眠轮询、条件变量同场量一遍"
  source-path="code/examples/vol5/chapter03/01_polling_cost.cpp"
  description="原实验原样抽出：四种等法各量一遍 CPU 时间与见到标志的延迟，忙等的核全烧在空转上。读数每次运行会动，量级不变。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// polling_cost.cpp —— 转圈的代价：空转、两种睡眠轮询、条件变量，四种等法同场量一遍
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::atomic<int> done{0};
std::atomic<long long> finished_at{0};
std::mutex m;
std::condition_variable cv;

long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch()).count();
}

template <class Wait>
void run(const char* name, Wait wait) {
  done = 0;
  finished_at = 0;
  std::thread worker([] {
    std::this_thread::sleep_for(100ms);   // 顶替“算完”那段耗时
    finished_at = now_ns();
    { std::lock_guard<std::mutex> g(m); done = 1; }
    cv.notify_one();
  });
  std::this_thread::sleep_for(10ms);      // 让 worker 睡下，主线程再开始等
  std::clock_t c0 = std::clock();
  long long rounds = 0;
  wait(rounds);
  std::clock_t c1 = std::clock();
  long long seen = now_ns();
  worker.join();
  std::printf("%-8s rounds=%9lld  cpu=%7.2f ms  delay=%9.1f us\n", name, rounds,
              1000.0 * (c1 - c0) / CLOCKS_PER_SEC,
              (seen - finished_at.load()) / 1000.0);
}

int main() {
  for (int pass = 1; pass <= 2; ++pass) {
    std::printf("--- pass %d ---\n", pass);
    run("spin", [](long long& r) {
      while (done.load(std::memory_order_relaxed) == 0) ++r;
    });
    run("sleep1", [](long long& r) {
      while (done.load() == 0) { ++r; std::this_thread::sleep_for(1ms); }
    });
    run("sleep20", [](long long& r) {
      while (done.load() == 0) { ++r; std::this_thread::sleep_for(20ms); }
    });
    run("cv", [](long long&) {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk, [] { return done.load() == 1; });
    });
  }
  return 0;
}
```

</OnlineCompilerDemo>

咱们把两遍的读数整理成表，同一格里斜杠前是第一遍，斜杠后是第二遍：

| 等法 | 转了几圈 | 等待期间花的 CPU | 从条件成立到察觉 |
|------|---------|----------------|----------------|
| 空转 `while (done.load(std::memory_order_relaxed) == 0) ++r;` | 4.4 亿 / 4.2 亿 | 89.2 / 89.3 ms | 8.7 / 4.9 µs |
| 每 1 ms 看一次 | 83 / 83 | 0.86 / 0.94 ms | 527 / 851 µs |
| 每 20 ms 看一次 | 5 / 5 | 0.14 / 0.11 ms | 11.2 / 11.0 ms |
| 条件变量 | 0 / 0 | 0.03 / 0.05 ms | 45 / 102 µs |

每格里的两个数是同一份程序在一次运行里跑两遍出来的，不是同一遍里的两个样本。CPU 一列用 `std::clock()` 取进程的 CPU 时间，worker 在睡，时间基本都算在主线程头上。察觉一列用 `steady_clock` 取时间戳，两列不是同一个钟，别拿它们互相加减。表里四行对应代码里四个 lambda，空转一行的循环体就是表里写的同一句，连 `memory_order_relaxed` 都一样。条件变量一行的 0，是因为它从头到尾不进循环，不是没记。

空转一行察觉最快，5 到 9 微秒，代价是 89 毫秒 CPU：主线程晚起 10 毫秒，等的那九十来毫秒里，有一个核被它整段占着。条件变量花 0.03 到 0.05 毫秒 CPU，察觉几十到一百微秒。中间两行是轮询的两种调法：每 1 毫秒看一次，CPU 降下来了，察觉几百微秒，每秒还要醒一千次。每 20 毫秒看一次，CPU 几乎不花，察觉掉到十一毫秒上下。没有一个间隔能同时顾上两头，盯得紧，CPU 就白烧，松一点，您知道得就晚。

还有一笔更难算的。间隔该填多少，取决于您等的事要多久。等一个 100 毫秒的活，20 毫秒还凑合。等一个 3 毫秒的活呢？等一个从网络上回来、谁也不知道多久的事件呢？轮询要求您在条件成立之前，就替它猜一个到期时间。

## 把等待交给条件变量

条件变量管的就是等条件的活：条件还没成立的时候，咱们让线程睡下去，不占 CPU。条件成立以后，改条件的一方把它叫醒。能跑的最小版本长这样：

<OnlineCompilerDemo
  title="条件变量的最小版本：worker 改条件，waiter 等到成立"
  source-path="code/examples/vol5/chapter03/02_cv_minimal.cpp"
  description="正文片段拼成完整程序：main 安排 waiter 睡下去，50 毫秒后 worker 把 done 立起来再 notify，waiter 醒来时条件一定成立。片段部分原样保留。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// cv_minimal.cpp —— 能跑的最小版本：worker 改完条件叫一声，waiter 睡到条件成立
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool done = false;   // 由 m 保护

void worker() {
  // 干活
  {
    std::lock_guard<std::mutex> g(m);
    done = true;
  }
  cv.notify_one();
}

void waiter() {
  std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, [] { return done; });
  // 到这里 done 一定是 true，锁还在手里
}

int main() {
  std::thread t(waiter);
  std::this_thread::sleep_for(50ms);   // 让 waiter 先查条件、先睡下去
  worker();                            // 改条件，再通知
  t.join();
  std::printf("waiter 回来了，done = %d\n", done ? 1 : 0);
  return 0;
}
```

</OnlineCompilerDemo>

`std::condition_variable` 从 C++11 起就在 `<condition_variable>` 里，标准只让它接受 `unique_lock<std::mutex>`，换来更轻的实现。您手里的锁要是别的类型，就得请它的通用版本出场，那部分放在下一篇《叫谁，等多久：notify 的选择与超时》的扩展节，可以跳过。

`wait` 的第二个参数是谓词，一个返回 `true` 或 `false` 的小函数，标准文档里的 `pred` 说的就是它。它替咱们回答一句：条件成立了没有。

`wait` 做的事，标准分成三段（[thread.condition.general]/3）。方括号里是草案的小节名，斜杠后面是段号，文中都按 eel.is 上的现行工作草案引，小节名跟正式标准一致，段号可能会挪。咱们这里只把第一段说清楚：释放互斥量和进入等待是同一步，锁一放开，线程已经排在等待的队伍里，谁也插不进缝去。解除阻塞的一段留给下一节，重新拿锁的一段，等 `wait` 返回的时候再说。

咱们从通知方看整个安排。`worker` 改 `done` 得拿 `m`，`waiter` 查 `done` 也拿着 `m`。两边抢同一把锁，只可能出现两种次序。要么 `worker` 在检查之前改完，那 `waiter` 一查就看见 true，压根不用睡。要么 `waiter` 已经查过、已经睡下，那 `worker` 改完再通知，正好把它叫醒。

`wait` 返回的时候，锁已经重新拿回手里，这是第三段。带谓词的 `wait` 返回时，条件是真的，别人改不了，您可以放心读 `done` 和它旁边的数据。醒来时锁在手里、条件成立，普通睡眠给不了。

通知放在锁里还是锁外，正确性上都行。笔者习惯放锁外，被叫醒的线程醒来就能拿锁。放锁里也成，只是被叫醒的线程醒来以后还要在锁上多等一会儿。

## 虚假唤醒：醒来不等于条件成立

解除阻塞在标准里有三种来路：`notify_one`、`notify_all`，还有 spuriously，也就是没有任何通知（[thread.condition.condvar]/8.3）。原文就一个词，没加条件。实现可以这么做，咱们写代码就得按“随时会醒”准备。

笔者在自己电脑上跑了很多遍，也没弄出一次真的虚假唤醒。标准写下的许可还是得当真：原文没有“某些平台”之类的限定，实现只要愿意，就能让您醒在一个条件并不成立的时刻。

下面是个替代演示。真的虚假唤醒笔者弄不出来，就拿一次“条件没变就发的通知”顶上，让等待线程醒在一个条件不成立的时刻。两者对代码的要求一样：醒来必须重看条件。您看第一段，等待中的线程被叫醒，靠的是 `main` 发的一次通知，可它什么也没改。

<OnlineCompilerDemo
  title="裸 wait 与谓词 wait 各接一次空通知"
  source-path="code/examples/vol5/chapter03/03_bare_wait_vs_pred.cpp"
  description="一次条件没变的通知：裸 wait 的线程醒来一看条件不成立，回去接着睡；带谓词的 wait 自己重验条件。醒来那一刻各自报出 data_ready 的值。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// bare_wait_vs_pred.cpp —— 虚假唤醒的替代演示：条件没变就发的通知，两种 wait 各接一次
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool data_ready = false;

int main() {
  {
    std::thread waiter([] {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk);  // 裸 wait，醒来不查条件
      std::printf("[bare] 醒来时 data_ready = %d\n", data_ready ? 1 : 0);
    });
    std::this_thread::sleep_for(50ms);
    cv.notify_one();  // 什么也没改，只发一次通知
    waiter.join();
  }

  data_ready = false;
  {
    std::thread waiter([] {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk, [] { return data_ready; });
      std::printf("[pred] 醒来时 data_ready = %d\n", data_ready ? 1 : 0);
    });
    std::this_thread::sleep_for(50ms);
    cv.notify_one();
    std::this_thread::sleep_for(50ms);
    std::printf("[main] 第一次通知之后，等待线程还睡着\n");
    {
      std::lock_guard<std::mutex> g(m);
      data_ready = true;
    }
    cv.notify_one();
    waiter.join();
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
[bare] 醒来时 data_ready = 0
[main] 第一次通知之后，等待线程还睡着
[pred] 醒来时 data_ready = 1
```

第一段用的是裸 `wait`。它被一次什么也没改的通知叫醒，`data_ready` 还是 0，咱们照旧往下走，把“被叫醒”当成了“条件成立”。

咱们再看第二段，换成带谓词的 `wait`。又来一次通知，它醒来查一次，发现条件不成立，回去接着睡，直到条件真改了、第二次通知到了才走。

标准把带谓词的版本定义成 `while (!pred()) wait(lock);`（[thread.condition.condvar]/13）。它每次醒来都替您重查一遍，不成立就再睡。虚假唤醒落进来，也只是多了一次检查。

所以您只该写带谓词的 `wait`。想用裸的，就得在外面自己补一圈 `while`，还得保证每条醒来路径都查过。重查的活交给库，比交给每个调用点稳。

## 丢失唤醒：通知发生在等待之前

反过来的情形更难查：通知是真的，可它来的时候还没人等着。咱们把通知挪到等待之前，看看会怎样。

<OnlineCompilerDemo
  title="丢失唤醒现场：通知发在等待之前"
  source-path="code/examples/vol5/chapter03/04_lost_wakeup.cpp"
  description="通知走在等待前面就蒸发了，worker 从此睡到永远。这个演示故意不退出，点运行后靠运行器的时限收场，不会自己停，属预期行为。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// lost_wakeup.cpp —— 丢失唤醒现场：通知发在等待之前，worker 再也等不到叫醒
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool ready = false;

int main() {
  std::thread worker([] {
    std::this_thread::sleep_for(200ms);   // 起来得比通知晚
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk);                          // 裸 wait，醒来不查条件
    std::printf("worker: 醒来了，ready = %d\n", ready ? 1 : 0);
  });

  std::this_thread::sleep_for(50ms);
  {
    std::lock_guard<std::mutex> g(m);
    ready = true;
  }
  cv.notify_one();                        // 这一刻 cv 上还没有人
  std::printf("main: 通知发完了\n");
  std::fflush(stdout);

  worker.join();
  std::printf("main: 走到底了\n");
  return 0;
}
```

</OnlineCompilerDemo>

咱们让 `worker` 睡满 200 毫秒才起来，`main` 在第 50 毫秒就改掉 `ready`、发了通知，通知比 `worker` 起床早 150 毫秒。发完 `main` 就 `join`，等它。

```text
$ timeout 2 ./lost
main: 通知发完了
$ echo $?
124
```

您看到的是，“通知发完了”打出来了，`worker` 的一行再没出现，进程被 `timeout` 杀掉。`worker` 在 `cv.wait(lk)` 里睡下，没人再来叫它。

通知不排队。`notify_one` 的效果，标准写得很干脆：如果有线程在等，解除其中一条的阻塞（[thread.condition.condvar]/5）。没有线程在等，它什么也不做，也不留记录，通知就此用掉。

修法是上一节那个谓词，您整个程序只改一行：

<OnlineCompilerDemo
  title="丢失唤醒的修法：只改一行的谓词 wait"
  source-path="code/examples/vol5/chapter03/05_lost_wakeup_fixed.cpp"
  description="与上一个程序只差一行：裸 wait 换成带谓词的 wait，睡前查一遍条件，错过的通知不再致命，worker 照常把活干完退出。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// lost_wakeup_fixed.cpp —— 丢失唤醒的修法：整个程序只改一行，裸 wait 换成带谓词的 wait
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool ready = false;

int main() {
  std::thread worker([] {
    std::this_thread::sleep_for(200ms);   // 起来得比通知晚
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [] { return ready; });    // 谓词在 wait 之前就要算一遍
    std::printf("worker: 醒来了，ready = %d\n", ready ? 1 : 0);
  });

  std::this_thread::sleep_for(50ms);
  {
    std::lock_guard<std::mutex> g(m);
    ready = true;
  }
  cv.notify_one();                        // 这一刻 cv 上还没有人
  std::printf("main: 通知发完了\n");
  std::fflush(stdout);

  worker.join();
  std::printf("main: 走到底了\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
main: 通知发完了
worker: 醒来了，ready = 1
main: 走到底了
```

咱们回头看：`worker` 没白等。它进 `wait` 之前，谓词算了一遍，`ready` 已经是 true，它压根不用睡。已经发生过的变化，谓词替它记着。

谓词和锁得成对用，您少一样都会出事。假设改 `ready` 的一方不拿 `m`，代码就还有缝：等的人查完发现是 false，还没睡下，改的人已经把 `ready` 改成了 true，通知也发完了。等的人接着睡，通知又丢一次。`wait` 的“释放锁并进入等待”是原子的，可它的原子性只对也拿同一把锁的通知方生效。

## 参考资源

笔者写本篇的时候，推进顺序参照了 Anthony Williams 那本书，书名叫《C++ Concurrency in Action》，讲条件变量的一章跟咱们这里的安排最接近。例子和文字都是笔者自己写的，转圈的读数与丢失唤醒现场是笔者在本机 GCC 16.2.1 上实测的。每条行为断言都对着 eel.is 上的现行草案与 cppreference 核过；cppreference 的页面笔者抓了两回，回回只拿到导航壳，正文被截断，所以条款一律按草案引，版本按它的标注。

- [std::condition_variable（cppreference）][cppref-cv]，C++11 起。
- [General（现行草案 thread.condition.general）][draft-general]，`wait` 释放锁并进入等待的三段原子动作。
- [Class condition_variable（现行草案 thread.condition.condvar）][draft-condvar]，`or spuriously` 的原文、谓词版的等价展开、`notify_one` 的效果。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，条件变量那一章的推进顺序参考。

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[draft-general]: https://eel.is/c++draft/thread.condition.general
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
