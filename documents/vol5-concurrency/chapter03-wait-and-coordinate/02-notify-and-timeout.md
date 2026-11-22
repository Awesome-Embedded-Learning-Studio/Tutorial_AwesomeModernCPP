---
title: "叫谁，等多久：notify 的选择与超时"
description: "一次通知发出去叫谁：notify_one 与 notify_all 怎么挑、叫错人会怎样，带超时的等待怎么读返回值，最后看通用版本 condition_variable_any。"
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

# 叫谁，等多久：notify 的选择与超时

等待和通知的基本件上一篇都齐了：等什么、谁来叫、为什么必须配谓词。这一篇管两件更细的事：一次通知发出去叫谁，以及等不到的时候怎么带一条时间的底线。

## notify_one 还是 notify_all

线程睡在条件变量上，可条件是您自己的变量。条件变量不知道谁在等什么，它只知道“有人等着，叫一声”。

两条线程等两种条件、睡在同一个 `cv` 上，咱们就能看见它怎么露馅。

<OnlineCompilerDemo
  title="notify_one 叫错人：通知被同向等待者收走"
  source-path="code/examples/vol5/chapter03/06_notify_one_wrong_thread.cpp"
  description="一把 cv 上睡两拨人，notify_one 挑人不看条件：叫醒的是早睡的 closer，taker 错过这趟通知。输出里能看见谁醒了、谁没醒。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// notify_one_wrong_thread.cpp —— 一把 cv 睡两拨人：notify_one 叫醒的是早睡的 closer，taker 错过这趟
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool stop = false;
bool data_ready = false;
std::atomic<int> parked{0};
std::atomic<bool> taker_got{false};

int main() {
  std::thread closer([] {
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [] { return stop; });
    std::printf("closer: 收到 stop\n");
  });
  while (parked.load() < 1) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);

  std::thread taker([] {
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [] { return data_ready; });
    taker_got = true;
    std::printf("taker: 拿到数据\n");
  });
  while (parked.load() < 2) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);

  {
    std::lock_guard<std::mutex> g(m);
    data_ready = true;
  }
  cv.notify_one();

  std::this_thread::sleep_for(200ms);
  std::printf("main: 200 ms 过去，taker %s\n",
              taker_got.load() ? "已经拿到数据" : "还没醒过来");

  {
    std::lock_guard<std::mutex> g(m);
    stop = true;
  }
  cv.notify_all();
  closer.join();
  taker.join();
  std::printf("main: 收工\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
main: 200 ms 过去，taker 还没醒过来
taker: 拿到数据
closer: 收到 stop
main: 收工
```

咱们把两条线程放在同一个 `cv` 上：`closer` 等 `stop`，`taker` 等 `data_ready`。`main` 改的是 `data_ready`，然后 `notify_one()`。标准只保证“解除其中一条的阻塞”，哪一条不看条件。本次运行叫醒的是比 `taker` 早睡下的 `closer`，实现按自己的队列顺序挑，它一看 `stop` 还是 false，回去接着睡。`taker` 错过这趟，200 毫秒过去还在睡。换一个实现，被叫醒的可能是 `taker`，输出就反过来了。

错法安静得很。程序不崩，日志不报，您只会看见一条线程一直等下去。这个演示里 `taker` 后来被收尾的 `notify_all` 救回来了，真实的程序里没有这第二下通知，它就等在那儿。

修法要看您怎么组织条件。条件本来就该分开，那就一个条件配一个 `condition_variable`，各叫各的。条件只有一个，比如“整批开始”，所有等它的人都该往下走，那就 `notify_all`。

<OnlineCompilerDemo
  title="notify_all 兜底：三缺一改成三全放"
  source-path="code/examples/vol5/chapter03/07_notify_one_then_all.cpp"
  description="三条线程等同一个 go：notify_one 只放行一条，notify_all 全部放行，两种通知方式各跑一遍，输出对照着看。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// notify_one_then_all.cpp —— 三条线程等同一个 go：notify_one 醒 1 条，notify_all 全放行
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  std::mutex m;
  std::condition_variable cv;
  bool go = false;
  std::atomic<int> started{0};
  std::atomic<int> woken{0};
  std::vector<std::thread> ws;
  for (int i = 0; i < 3; ++i) {
    ws.emplace_back([&, i] {
      std::unique_lock<std::mutex> lk(m);
      ++started;
      cv.wait(lk, [&] { return go; });
      ++woken;
      std::printf("worker %d: 出发\n", i);
    });
  }
  while (started.load() < 3) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);
  {
    std::lock_guard<std::mutex> g(m);
    go = true;
  }
  cv.notify_one();
  std::this_thread::sleep_for(100ms);
  std::printf("main: notify_one 之后，醒来的有 %d 条\n", woken.load());
  cv.notify_all();
  for (auto& t : ws) t.join();
  std::printf("main: notify_all 之后，醒来的有 %d 条\n", woken.load());
  return 0;
}
```

</OnlineCompilerDemo>

```text
worker 0: 出发
main: notify_one 之后，醒来的有 1 条
worker 1: 出发
worker 2: 出发
main: notify_all 之后，醒来的有 3 条
```

咱们改 `go` 之前，3 条线程都睡下了。`notify_one` 之后醒 1 条，`notify_all` 之后 3 条全走。

`notify_all` 在正确性上从不吃亏，代价是醒来的线程去抢同一把锁，N 条里 N-1 条白跑。所以咱们挑哪个，判断法在条件那边：一次变化够几条线程往下走，就叫几条。拿不准的时候，`notify_all` 是安全的一边。

`notify_one` 在没人等的时候同样什么也不做，上一篇《等一个条件：从轮询到条件变量》里丢失唤醒一节讲的就是它。

## 带超时的等待

前面几种等法都可能永远等下去。真实程序里，咱们常要一个上限：到某个时刻还没成，就得去干别的。

`wait_for` 和 `wait_until` 就是干它的，都从 C++11 起。咱们会碰到四个重载，返回值只有两种：

- `wait_for(lock, rel_time)` 返回 `std::cv_status`，您会拿到 `timeout` 或 `no_timeout`。
- `wait_for(lock, rel_time, pred)` 返回 `bool`，条件成没成。
- `wait_until(lock, abs_time)` 返回 `cv_status`，把相对时长换成绝对时间点。
- `wait_until(lock, abs_time, pred)` 返回 `bool`。

带谓词的给 `bool`，不带谓词的给 `cv_status`。`no_timeout` 的意思是期限还没到就返回了，可能是通知，也可能是一次虚假唤醒，条件成没成还得您自己查。

咱们把六种情形摆在一起跑：

<OnlineCompilerDemo
  title="wait_for 六种情形：带谓词与不带谓词各三种"
  source-path="code/examples/vol5/chapter03/08_wait_for_six_cases.cpp"
  description="时限内等到、时限到头没等到、等待前条件已成立……六种情形摆在一个程序里跑，返回值只报谓词真假。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// wait_for_six_cases.cpp —— 带超时的等待：带谓词与不带谓词各三种情形，摆在一起跑
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

const char* name_of(std::cv_status s) {
  return s == std::cv_status::timeout ? "timeout" : "no_timeout";
}

int main() {
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = true;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 200ms, [&] { return ready; });
    std::printf("A pred 本来成立   -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::thread t([&] {
      std::this_thread::sleep_for(30ms);
      { std::lock_guard<std::mutex> g(m); ready = true; }
      cv.notify_one();
    });
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 200ms, [&] { return ready; });
    double e = ms_since(t0);
    lk.unlock();
    t.join();
    std::printf("B 30 ms 被通知    -> %d, %.3f ms\n", ok, e);
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 60ms, [&] { return ready; });
    std::printf("C 60 ms 到期      -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::thread t([&] {
      std::this_thread::sleep_for(30ms);
      { std::lock_guard<std::mutex> g(m); ready = true; }
      cv.notify_one();
    });
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    std::cv_status st = cv.wait_for(lk, 200ms);
    double e = ms_since(t0);
    lk.unlock();
    t.join();
    std::printf("D 无谓词，30 ms 通知 -> %s, %.3f ms\n", name_of(st), e);
  }
  {
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    std::cv_status st = cv.wait_for(lk, 60ms);
    std::printf("E 无谓词，没人通知   -> %s, %.3f ms\n", name_of(st), ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = true;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, -1ms, [&] { return ready; });
    std::printf("F 时限已过但 pred 成立 -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
A pred 本来成立   -> 1, 0.000 ms
B 30 ms 被通知    -> 1, 30.213 ms
C 60 ms 到期      -> 0, 60.661 ms
D 无谓词，30 ms 通知 -> no_timeout, 30.289 ms
E 无谓词，没人通知   -> timeout, 60.228 ms
F 时限已过但 pred 成立 -> 1, 0.000 ms
```

A 到 C 是带谓词的版本，咱们逐行看。A 的谓词在进来之前就成立，返回 1，等 0.000 毫秒。标准专门注过，谓词一开始就是 true 时不阻塞，哪怕时限已经过去（[thread.condition.condvar]/37 的注）。B 在 30 毫秒收到通知，返回 1。C 没人通知，60 毫秒的期限到头，返回 0，实际等了 60.7 毫秒。

D 和 E 不带谓词，您得自己再查一遍条件。D 收到通知，`no_timeout`，E 到期，`timeout`。

F 把时限设成过去的 `-1ms`，谓词成立，您照样立刻拿到 1。它和 A 说的是同一件事：谓词成立就不睡，时限过没过都不睡。

两处要留意。C 比 60 毫秒多出来的 0.7 毫秒来自定时器和调度的精度，超时不是闹钟，别拿它当精确的截止。还有，`wait_for` 返回 `false` 不等于以后不会有通知，它只说您给的时长里没等到。之后是重试还是放弃，得您自己设计，具体怎么收场，3.6《等待也需要结束路径》接着谈。

`wait_for` 怎么把时长变成绝对时间，标准里有一段说明用的辅助函数，它用的是 `steady_clock`（[thread.condition.general]/6）。相对时长因此不受系统时间调整影响。`wait_until` 就不一定，您传进去的是时间点，用 `system_clock` 算的话，系统时间一改它跟着改。要单调，就拿 `steady_clock::now()` 算那个时间点。

## condition_variable_any：通用版本（扩展）

> 本节是扩展，您跳过它不影响主线。3.3《有容量的队列：先让它站得住》起的队列主线用 `std::condition_variable`，3.6《等待也需要结束路径》的主线也在它上面。`condition_variable_any` 带 `stop_token` 的重载归 3.6 单独处理。

`std::condition_variable` 只能配 `unique_lock<std::mutex>`（[thread.condition.general]/1）。可您手里的锁不一定长这样，比如读的一方拿的是 `std::shared_lock<std::shared_mutex>`。

`std::condition_variable_any` 从 C++11 起提供通用版（下文简称 `cv_any`，例子里的对象名还叫 `cv`）。您手里的锁只要满足 BasicLockable，也就是有 `lock()` 和 `unlock()`，就能用（[thread.condition.condvarany.general]/1）。

最典型的场景是几条读线程等一份配置发布。`std::shared_mutex` 从 C++17 才有，您得用 C++17 以上的标准编译例子：

<OnlineCompilerDemo
  title="condition_variable_any 配 shared_lock：读线程等配置发布"
  source-path="code/examples/vol5/chapter03/09_cv_any_shared_lock.cpp"
  description="通用版条件变量接受任意可锁对象，三条读线程拿 shared_lock 等一份配置发布，发布之后各自读自己的那份。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// cv_any_shared_lock.cpp —— condition_variable_any 配 shared_lock：三条读线程等一份配置发布
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::shared_mutex rm;
std::condition_variable_any cv;
bool published = false;
std::string config;

int main() {
  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i) {
    readers.emplace_back([i] {
      std::shared_lock<std::shared_mutex> lk(rm);   // 共享锁，几个人同时读
      cv.wait(lk, [] { return published; });
      std::printf("reader %d: %s\n", i, config.c_str());
    });
  }
  std::this_thread::sleep_for(50ms);
  {
    std::unique_lock<std::shared_mutex> lk(rm);     // 写的时候独占
    config = "level = 3";
    published = true;
  }
  cv.notify_all();
  for (auto& t : readers) t.join();
  std::printf("main: 三条读线程读到的都是 level = 3\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
reader 0: level = 3
reader 1: level = 3
reader 2: level = 3
main: 三条读线程读到的都是 level = 3
```

咱们看三条读线程：各自把 `shared_lock` 递进 `cv.wait`，`std::condition_variable` 收不下，只能交给 `cv_any`。写的一方把独占锁拿住，改配置、把 `published` 立起来，放锁以后 `notify_all`，三条读线程这才一起醒来，读到同一份配置。打印的次序由调度定，每次都不同。

每次 `wait` 要多拿多放一把内部锁，代价就在这里。笔者翻了本机 GCC 16 的 `<condition_variable>`：`condition_variable_any` 自己带一把 `shared_ptr<mutex> _M_mutex`，构造时就 `make_shared` 出来。每次 `wait`，它拿住内部锁，再放掉您手里的锁，醒来以后反过来拿。`notify_one` 和 `notify_all` 也要拿它。

咱们从实现里能看出，内部锁有两个用处。一是让“把线程挂进等待队列”和“发通知”这两步共用同一把内部锁：`wait` 拿住内部锁，放掉您手里的锁，再把线程挂进 `_M_cond` 的等待队列。`notify_one` 也得拿到同一把内部锁才发得出去。这么一来，通知插不进“查完条件、还没睡下”的缝里。二是让任意锁类型走同一套等待逻辑。

标准在 `[thread.condition.condvarany.general]` 底下还有一条注：`Lock` 不是标准互斥量类型、也不是标准互斥量的 `unique_lock` 包装时，谓词相关的同步要您自己保证（[thread.condition.condvarany.general]/1 的注）。`shared_lock<shared_mutex>` 正是一例。上面例子里的代码，谓词读 `published` 时拿着共享锁，写的一方拿着独占锁，同步是有的。

C++20 给 `cv_any` 加了一组带 `stop_token` 的等待（[thread.condvarany.intwait]），归 3.6，咱们这里不展开。

挑法很短，咱们照着走就够：锁是 `unique_lock<std::mutex>`，用 `condition_variable`，锁是别的，才轮到 `cv_any`。

## 轮到您动手的时候

写一处等待之前，笔者会把几样问题过一遍：

1. 等的条件是什么？谁改它，改的一方拿的是哪把锁？
2. 等待用的是带谓词的 `wait` 吗？谓词读的变量，和改它的人拿的是同一把锁吗？
3. 通知发生在条件改完之后吗？会不会在没人等的时候发出去，然后无声无息地消失？
4. 一个条件变量上挂了几种条件？`notify_one` 有没有可能叫错人？
5. 等待有没有可能等不到头？要不要用 `wait_for` 兜个底，兜底返回 `false` 之后您打算做什么？

## 下一步

条件变量只解决“叫醒”。它不管醒来以后排到哪儿，也不管几条线程同时醒来怎么分活。

翻页之前，请您挑一段自己写过的 `sleep` 轮询，把它换成 `wait` 加谓词。换的时候把谓词读的变量、护着它的锁一并写下来。两样对不上，就把改条件的路径也拉进同一把锁。

## 参考资源

笔者写本篇的时候，推进顺序参照了 Anthony Williams 那本书，书名叫《C++ Concurrency in Action》，讲条件变量的一章跟咱们这里的安排最接近。例子和文字都是笔者自己写的。cppreference 的页面笔者抓了两回，回回只拿到导航壳，正文被截断，所以条款一律按 eel.is 上的现行草案引，版本按它的标注。

- [std::condition_variable（cppreference）][cppref-cv]，C++11 起。
- [std::condition_variable::wait_for（cppreference）][cppref-waitfor]，`cv_status` 与 `bool` 两种返回值。
- [std::condition_variable_any（cppreference）][cppref-cvany]，C++11 起，接受任意 BasicLockable 锁。
- [General（现行草案 thread.condition.general）][draft-general]，等待的三段原子动作、通知的总序，以及把时长换算成时间点时用的 `steady_clock`。
- [Class condition_variable（现行草案 thread.condition.condvar）][draft-condvar]，`wait` 的 `or spuriously`、谓词版的等价展开、`notify_one` 的效果、超时与返回值。
- [Class condition_variable_any（现行草案 thread.condition.condvarany）][draft-condvarany]，BasicLockable 的要求与谓词同步那条注。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，条件变量那一章的推进顺序参考。
- GCC 16.2.1 自带的 `<condition_variable>`（本机路径 `/usr/include/c++/16/condition_variable`）：`condition_variable_any` 的内部互斥量是从这里读的，没有网页可引。

[cppref-cv]: https://en.cppreference.com/w/cpp/thread/condition_variable
[cppref-waitfor]: https://en.cppreference.com/w/cpp/thread/condition_variable/wait_for
[cppref-cvany]: https://en.cppreference.com/w/cpp/thread/condition_variable_any
[draft-general]: https://eel.is/c++draft/thread.condition.general
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-condvarany]: https://eel.is/c++draft/thread.condition.condvarany
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
