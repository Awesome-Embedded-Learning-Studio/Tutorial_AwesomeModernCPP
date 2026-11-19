---
title: "等待一个条件成立"
description: "从 sleep 轮询的代价量起，把等待交给条件变量：谓词为什么必须带、通知怎么会丢、notify_one 与 notify_all 怎么挑、带超时的等待怎么读返回值，最后看通用版本 condition_variable_any。"
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

# 等待一个条件成立

《减少共享与避免死锁》收尾时数过的那几处等待，这一篇接着往下走。条件变量咱们其实借过两回：《线程在机器上是什么》里那个乒乓程序拿它量来回唤醒的开销，《让工作有序结束》借它的通用版接停止请求。两回都只当现成工具用，语义一直没讲，这一篇补上。

## 转圈：轮询的代价

我搭了个最小现场，把转圈量了一遍：一条线程算 100 毫秒的活，算完把 `done` 立起来，另一条线程就等 `done`。四种等法各写成一个回调，交给同一套架子。同一份程序跑两遍，每遍四种等法各跑一次。

```cpp
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

咱们把两遍的读数整理成表，同一格里斜杠前是第一遍，斜杠后是第二遍：

| 等法 | 转了几圈 | 等待期间花的 CPU | 从条件成立到察觉 |
|------|---------|----------------|----------------|
| 空转 `while (done.load(std::memory_order_relaxed) == 0) ++r;` | 4.4 亿 / 4.2 亿 | 89.2 / 89.3 ms | 8.7 / 4.9 µs |
| 每 1 ms 看一次 | 83 / 83 | 0.86 / 0.94 ms | 527 / 851 µs |
| 每 20 ms 看一次 | 5 / 5 | 0.14 / 0.11 ms | 11.2 / 11.0 ms |
| 条件变量 | 0 / 0 | 0.03 / 0.05 ms | 45 / 102 µs |

读表前，有几句咱们得说清。每格里的两个数是同一份程序在一次运行里跑两遍出来的，不是同一遍里的两个样本。CPU 那列用 `std::clock()` 取进程的 CPU 时间，worker 在睡，这点时间基本都算在主线程头上。察觉那列用 `steady_clock` 取时间戳，两列不是同一个钟，别拿它们互相加减。表里四行对应代码里四个 lambda，空转那行的循环体就是表里写的那句，连 `memory_order_relaxed` 都一样。条件变量那行的 0 是因为这类等法从头到尾不进循环，不是没记。表里的数在 GCC 16.2.1、WSL2、16 逻辑核上跑出来，两遍之间也会差。

空转那行察觉最快，5 到 9 微秒，代价是 89 毫秒 CPU：主线程晚起 10 毫秒，等的那九十来毫秒里，有一个核被它整段占着。条件变量花 0.03 到 0.05 毫秒 CPU，察觉几十到一百微秒。中间两行是轮询的两种调法：每 1 毫秒看一次，CPU 降下来了，察觉几百微秒，每秒还要醒一千次。每 20 毫秒看一次，CPU 几乎不花，察觉掉到十一毫秒上下。没有一个间隔能同时顾上两头，盯得紧，CPU 就白烧，松一点，您知道得就晚。

还有一笔更难算的。间隔该填多少，取决于您等的那件事要多久。等一个 100 毫秒的活，20 毫秒还凑合。等一个 3 毫秒的活呢？等一个从网络上回来、谁也不知道多久的事件呢？轮询要求您在条件成立之前，就替它猜一个到期时间。

## 把等待交给条件变量

条件变量管的就是这类等待：条件还没成立的时候，咱们让线程睡下去，不占 CPU。条件成立以后，改条件的那一方把它叫醒。能跑的最小版本长这样：

```cpp
#include <condition_variable>
#include <mutex>

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
```

`std::condition_variable` 从 C++11 起就在 `<condition_variable>` 里，标准只让它接受 `unique_lock<std::mutex>`，换来更轻的实现。您手里的锁要是别的类型，就得请它的通用版本出场，那部分放在本篇末尾的扩展节，可以跳过。

`wait` 的第二个参数是谓词，一个返回 `true` 或 `false` 的小函数，标准文档里的 `pred` 说的就是它。它替咱们回答一句：条件成立了没有。

`wait` 做的事，标准分成三段（[thread.condition.general]/3）。方括号里是草案的小节名，斜杠后面是段号，文中都按 eel.is 上的现行工作草案引，小节名跟正式标准一致，段号可能会挪。咱们这里只把第一段说清楚：释放互斥量和进入等待是同一步，锁放开的那一瞬，线程已经排在等待的队伍里，别人插不进那道缝。解除阻塞那一段留给下一节，重新拿锁那一段，等 `wait` 返回的时候再说。

咱们从通知方看这套安排。`worker` 改 `done` 得拿 `m`，`waiter` 查 `done` 也拿着 `m`。两边抢同一把锁，只可能出现两种次序。要么 `worker` 在检查之前改完，那 `waiter` 一查就看见 true，压根不用睡。要么 `waiter` 已经查过、已经睡下，那 `worker` 改完再通知，正好把它叫醒。

`wait` 返回的时候，锁已经重新拿回手里，这是第三段。带谓词的 `wait` 返回时，条件是真的，别人改不了，您可以放心读 `done` 和它旁边那些数据。这一点让它和普通睡眠分了家。

通知放在锁里还是锁外，正确性上都行。我习惯放锁外，被叫醒的线程醒来就能拿锁。放锁里也成，只是被叫醒的线程醒来以后还要在锁上多等一会儿。

## 虚假唤醒：醒来不等于条件成立

解除阻塞在标准里有三种来路：`notify_one`、`notify_all`，还有 spuriously，也就是没有任何通知（[thread.condition.condvar]/8.3）。原文就一个词，没加条件。实现可以这么做，咱们写代码就得按“随时会醒”准备。

我在自己电脑上跑了很多遍，也没弄出一次真的虚假唤醒。标准里那句许可还是得当真：原文没有“某些平台”之类的限定，实现只要愿意，就能让您醒在一个条件并不成立的时刻。

下面这段是个替代演示。真的虚假唤醒我弄不出来，就拿一次“条件没变就发的通知”顶上，让等待线程醒在一个条件不成立的时刻。两者对代码的要求一样：醒来必须重看条件。您看第一段，等待中的线程被叫醒，靠的是 `main` 发的那次通知，可那次通知什么也没改。

```cpp
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

```text
[bare] 醒来时 data_ready = 0
[main] 第一次通知之后，等待线程还睡着
[pred] 醒来时 data_ready = 1
```

第一段用的是裸 `wait`。它被那次什么也没改的通知叫醒，`data_ready` 还是 0，咱们照旧往下走，把“被叫醒”当成了“条件成立”。

咱们再看第二段，换成带谓词的 `wait`。又来一次通知，它醒来查一次，发现条件不成立，回去接着睡，直到条件真改了、第二次通知到了才走。

标准把带谓词这一版定义成 `while (!pred()) wait(lock);`（[thread.condition.condvar]/13）。它每次醒来都替您重查一遍，不成立就再睡。虚假唤醒落进这段代码，只是多了一次检查。

所以您只该写带谓词的 `wait`。想用裸的，就得在外面自己补一圈 `while`，还得保证每条醒来路径都查过。这件事交给库，比交给每个调用点稳。

## 丢失唤醒：通知发生在等待之前

反过来的情形更难查：通知是真的，可它来的时候还没人等着。咱们把通知挪到等待之前，看看会怎样。

```cpp
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

咱们让 `worker` 睡满 200 毫秒才起来，`main` 在第 50 毫秒就改掉 `ready`、发了通知，通知比 `worker` 起床早 150 毫秒。发完 `main` 就 `join`，等它。

```text
$ timeout 2 ./lost
main: 通知发完了
$ echo $?
124
```

您看到的是，“通知发完了”打出来了，`worker` 那一行再没出现，进程被 `timeout` 杀掉。`worker` 在 `cv.wait(lk)` 里睡下，没人再来叫它。

通知不排队，这一点咱们得认下来。`notify_one` 的效果，标准写得很干脆：如果有线程在等，解除其中一条的阻塞（[thread.condition.condvar]/5）。没有线程在等，它什么也不做，也不留记录。那次通知已经用掉了。

修法是上一节那个谓词，您整个程序只改一行：

```cpp
    cv.wait(lk, [] { return ready; });    // 谓词在 wait 之前就要算一遍
```

```text
main: 通知发完了
worker: 醒来了，ready = 1
main: 走到底了
```

咱们回头看：`worker` 这次没白等。它进 `wait` 之前，谓词算了一遍，`ready` 已经是 true，它压根不用睡。已经发生过的变化，谓词替它记着。

谓词和锁得成对用，您少一样都会出事。假设改 `ready` 的一方不拿 `m`，这段代码就还有缝：等的人查完发现是 false，还没睡下，改的人已经把 `ready` 改成了 true，通知也发完了。等的人接着睡，通知又丢一次。`wait` 的“释放锁并进入等待”是原子的，可它的原子性只对也拿同一把锁的通知方生效。

## notify_one 还是 notify_all

线程睡在条件变量上，可条件是您自己的变量。条件变量不知道谁在等什么，它只知道“有人等着，叫一声”。

两条线程等两种条件、睡在同一个 `cv` 上，咱们就能看见这件事露出来。

```cpp
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

```text
main: 200 ms 过去，taker 还没醒过来
taker: 拿到数据
closer: 收到 stop
main: 收工
```

咱们把两条线程放在同一个 `cv` 上：`closer` 等 `stop`，`taker` 等 `data_ready`。`main` 改的是 `data_ready`，然后 `notify_one()`。标准只保证“解除其中一条的阻塞”，哪一条不看条件。这一跑，叫醒的是比 `taker` 早睡下的 `closer`，这次实现按自己的队列顺序挑，它一看 `stop` 还是 false，回去接着睡。`taker` 错过这趟，200 毫秒过去还在睡。换一个实现，被叫醒的可能是 `taker`，这段输出就反过来了。

这类错法很安静。程序不崩，日志不报，您只会看见一条线程一直等下去。这个演示里 `taker` 后来被收尾的 `notify_all` 救回来了，真实的程序里没有这第二下通知，它就等在那儿。

修法要看您怎么组织条件。条件本来就该分开，那就一个条件配一个 `condition_variable`，各叫各的。条件只有一个，比如“整批开始”，所有等它的人都该往下走，那就 `notify_all`。

```cpp
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

```text
worker 0: 出发
main: notify_one 之后，醒来的有 1 条
worker 1: 出发
worker 2: 出发
main: notify_all 之后，醒来的有 3 条
```

咱们改 `go` 之前，3 条线程都睡下了。`notify_one` 之后醒 1 条，`notify_all` 之后 3 条全走。

`notify_all` 在正确性上从不吃亏，代价是醒来的线程去抢同一把锁，N 条里 N-1 条白跑。所以咱们挑哪个，判断法在条件那边：一次变化够几条线程往下走，就叫几条。拿不准的时候，`notify_all` 是安全的那一边。

`notify_one` 在没人等的时候同样什么也不做，咱们在讲丢失唤醒的地方见过的就是这类通知。

## 带超时的等待

前面几种等法都可能永远等下去。真实程序里，咱们常要一个上限：到某个时刻还没成，就得去干别的。

`wait_for` 和 `wait_until` 干这件事，都从 C++11 起。咱们会碰到四个重载，返回值只有两种：

- `wait_for(lock, rel_time)` 返回 `std::cv_status`，您会拿到 `timeout` 或 `no_timeout`。
- `wait_for(lock, rel_time, pred)` 返回 `bool`，条件成没成。
- `wait_until(lock, abs_time)` 返回 `cv_status`，把相对时长换成绝对时间点。
- `wait_until(lock, abs_time, pred)` 返回 `bool`。

带谓词的给 `bool`，不带谓词的给 `cv_status`。`no_timeout` 的意思是期限还没到就返回了，可能是通知，也可能是一次虚假唤醒，条件成没成还得您自己查。

咱们把六种情形摆在一起跑：

```cpp
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

```text
A pred 本来成立   -> 1, 0.000 ms
B 30 ms 被通知    -> 1, 30.213 ms
C 60 ms 到期      -> 0, 60.661 ms
D 无谓词，30 ms 通知 -> no_timeout, 30.289 ms
E 无谓词，没人通知   -> timeout, 60.228 ms
F 时限已过但 pred 成立 -> 1, 0.000 ms
```

A 到 C 是带谓词那一版，咱们逐行看。A 的谓词在进来之前就成立，返回 1，等 0.000 毫秒。标准专门注过，谓词一开始就是 true 时不阻塞，哪怕时限已经过去（[thread.condition.condvar]/37 的注）。B 在 30 毫秒收到通知，返回 1。C 没人通知，60 毫秒的期限到头，返回 0，实际等了 60.7 毫秒。

D 和 E 不带谓词，您得自己再查一遍条件。D 收到通知，`no_timeout`，E 到期，`timeout`。

F 把时限设成过去的 `-1ms`，谓词成立，您照样立刻拿到 1。它和 A 说的是同一件事：谓词成立就不睡，时限过没过都不睡。

两处要留意。C 比 60 毫秒多出来的 0.7 毫秒来自定时器和调度的精度，超时不是闹钟，别拿它当精确的截止。还有，`wait_for` 返回 `false` 不等于以后不会有通知，它只说您给的时长里没等到。之后是重试还是放弃，得您自己设计，那件事归 3.3《等待也需要结束路径》。

`wait_for` 怎么把时长变成绝对时间，标准里有一段说明用的辅助函数，它用的是 `steady_clock`（[thread.condition.general]/6）。相对时长因此不受系统时间调整影响。`wait_until` 就不一定，您传进去的是时间点，用 `system_clock` 算的话，系统时间一改它跟着改。要单调，就拿 `steady_clock::now()` 算那个时间点。

## condition_variable_any：通用版本（扩展）

> 本节是扩展，您跳过它不影响主线。3.2《写一个有容量限制的任务队列》的队列主线用 `std::condition_variable`，3.3《等待也需要结束路径》的主线也在它上面。`condition_variable_any` 带 `stop_token` 的那组重载归 3.3 单独处理。

`std::condition_variable` 只能配 `unique_lock<std::mutex>`（[thread.condition.general]/1）。可您手里的锁不一定长这样，比如读的一方拿的是 `std::shared_lock<std::shared_mutex>`。

`std::condition_variable_any` 从 C++11 起提供通用版（下文简称 `cv_any`，例子里的对象名还叫 `cv`）。您手里的锁只要满足 BasicLockable，也就是有 `lock()` 和 `unlock()`，就能用（[thread.condition.condvarany.general]/1）。

最典型的场景是几条读线程等一份配置发布。`std::shared_mutex` 从 C++17 才有，您得用 C++17 以上的标准编译这段例子：

```cpp
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

```text
reader 0: level = 3
reader 1: level = 3
reader 2: level = 3
main: 三条读线程读到的都是 level = 3
```

咱们看三条读线程：各自把 `shared_lock` 递进 `cv.wait`，`std::condition_variable` 收不下这类锁，只能交给 `cv_any`。写的那一方把独占锁拿住，改配置、把 `published` 立起来，放锁以后 `notify_all`，三条读线程这才一起醒来，读到同一份配置。打印的次序由调度定，每次都不同。

每次 `wait` 要多拿多放一把内部锁，代价就在这里。我翻了本机 GCC 16 的 `<condition_variable>`：`condition_variable_any` 自己带一把 `shared_ptr<mutex> _M_mutex`，构造时就 `make_shared` 出来。每次 `wait`，它拿这把内部锁，再放掉您那把，醒来以后反过来拿。`notify_one` 和 `notify_all` 也要拿它。

咱们从那段实现里能看出，这把内部锁有两个用处。一是让“把线程挂进等待队列”和“发通知”这两步共用同一把内部锁：`wait` 拿住内部锁，放掉您那把，再把线程挂进 `_M_cond` 的等待队列。`notify_one` 也得拿到同一把内部锁才发得出去。这么一来，通知插不进“查完条件、还没睡下”那道缝。二是让任意锁类型走同一套等待逻辑。

标准在 `[thread.condition.condvarany.general]` 底下还有一条注：`Lock` 不是标准互斥量类型、也不是标准互斥量的 `unique_lock` 包装时，谓词相关的同步要您自己保证（[thread.condition.condvarany.general]/1 的注）。`shared_lock<shared_mutex>` 就属于这一类。上面那段代码里，谓词读 `published` 时拿着共享锁，写的那一方拿着独占锁，同步是有的。

C++20 给 `cv_any` 加了一组带 `stop_token` 的等待（[thread.condvarany.intwait]），那组归 3.3，咱们这里不展开。

挑法很短，咱们照着走就够：锁是 `unique_lock<std::mutex>`，用 `condition_variable`，锁是别的，才轮到 `cv_any`。

## 轮到您动手的时候

写一处等待之前，我会把这几个问题过一遍：

1. 等的条件是什么？谁改它，改的那一侧拿的是哪把锁？
2. 等待用的是带谓词的 `wait` 吗？谓词读的变量，和改它的人拿的是同一把锁吗？
3. 通知发生在条件改完之后吗？会不会在没人等的时候发出去，然后无声无息地消失？
4. 一个条件变量上挂了几种条件？`notify_one` 有没有可能叫错人？
5. 这次等待有没有可能等不到头？要不要用 `wait_for` 兜个底，兜底返回 `false` 之后您打算做什么？

## 下一步

条件变量只解决“叫醒”。它不管醒来以后排到哪儿，也不管几条线程同时醒来怎么分活。下一篇 3.2《写一个有容量限制的任务队列》要拿条件变量搭一个有上限的队列，咱们在那儿会碰上两个方向的条件：队列空了，消费者等，队列满了，生产者等。两个方向的条件住在一个对象里，`notify` 的选择和丢失唤醒的防线都得跟着队列的形状走。

翻页之前，请您挑一段自己写过的 `sleep` 轮询，把它换成 `wait` 加谓词。换的时候把谓词读的变量、护着它的那把锁一并写下来。两样对不上，就把改条件的路径也拉进同一把锁。

## 参考资源

我写本篇的时候，推进顺序参照了 Anthony Williams 那本书，书名叫《C++ Concurrency in Action》，讲条件变量的一章跟咱们这里的安排最接近。例子和文字都是我自己写的，实验在本机 GCC 16.2.1 上编译实测（`g++ -std=c++20 -O2 -pthread`，2026-11-15），机器写在轮询表旁边。表里的读数都是真跑出来的，同一行两遍之间也会差，换台机器、换个负载，毫秒和微秒都会动。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的现行草案。cppreference 的页面我抓了两回，回回只拿到导航壳，正文被截断，所以条款一律按现行草案引，版本按它的标注。

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
