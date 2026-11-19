---
title: "等待也需要结束路径"
description: "四种结束方式各配一个可观察的现场：关闭、超时、停止、失败，讲清 condition_variable_any 的 stop_token 重载与 stop_callback 手动搭桥，收在一张等待结束路径检查表上。"
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 等待也需要结束路径

上一篇《写一个有容量限制的任务队列》把队列关上了：收盘这个动作一发生，生产者不再投放，消费者把手头的活做完。队列这一头收得干净，可队列关不上、也等不来货的那些等待呢——睡在 `wait` 上，没人叫就一直睡。这一篇咱们只回答一个问题：您写下的每一处等待，都得有一条说得清的结束路径。

3.1《等待一个条件成立》收尾留了两条线，咱们在这里一起接上：`wait_for` 返回 `false` 之后是重试还是放弃，当时说归这一篇。`condition_variable_any` 带 `stop_token` 的那组重载，当时也说归这一篇。3.1 还说过这一篇的主线仍在 `condition_variable` 上，这话照旧算数，只是接停止请求的那组重载长在通用版身上，开头的现场因此拿的就是 `condition_variable_any`，不接停止请求的等待照旧用 `condition_variable`。再往前数，《让工作有序结束》给整条线程做过一遍退出路径检查，这一篇把同样的问题压到更小的单位，一处处具体的 `wait`。靠的无非是标志位、时限和停止令牌，能单独用，也能拼在一起。

## 等不到的时候，出路有几条

成功那一头不用设计。货到了，谓词成立，等待自己就结束，没有人需要额外做什么。要设计的是另一头：货一直不来。收场数得出来的有四种，咱们按顺序叫它们关闭、超时、停止、失败。下面同一个现场跑四遍，每遍换一种结束方式，您看完每一遍，都得能答出“它为什么散”。

现场是咱们搭的一个最小货台：一把锁、一个条件变量、一条空队列，一条线程等第一件货。四种结束方式各跑一遍，起点都是等待线程睡下：

```cpp
// exits.cpp —— 同一处等待，四种结束方式，各留一个可观察的现场
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

struct Bay {
  std::mutex m;
  std::condition_variable_any cv;   // 通用版：C 一幕要接停止请求
  std::queue<int> q;
  bool closed = false;
  bool failed = false;
  std::atomic<int> parked{0};
};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  { // A 关闭：没人再来放货，收盘的一方把 closed 立起来
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bay.cv.wait(lk, [&] { return !bay.q.empty() || bay.closed; });
      std::printf("A 关闭 : %5.1f ms 返回，队列空着，谓词翻正靠的是 closed\n",
                  ms_since(t0));
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    { std::lock_guard<std::mutex> g(bay.m); bay.closed = true; }
    bay.cv.notify_all();
    waiter.join();
  }

  { // B 超时：时限 120 ms，谁也没来
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bool got = bay.cv.wait_for(lk, 120ms,
                                 [&] { return !bay.q.empty() || bay.closed; });
      std::printf("B 超时 : %5.1f ms 返回，返回值 %d，时限内没等到货\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    waiter.join();
  }

  { // C 停止：停止请求在 60 ms 到，等待当场结束
    Bay bay;
    std::stop_source source;
    auto token = source.get_token();
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bool got = bay.cv.wait(lk, token, [&] { return !bay.q.empty(); });
      std::printf("C 停止 : %5.1f ms 返回，返回值 %d，谓词没成立\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main   : %5.1f ms 发出停止请求\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    source.request_stop();
    waiter.join();
  }

  { // D 失败：上游干砸了，把 failed 记下来再散场
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bay.cv.wait(lk, [&] { return !bay.q.empty() || bay.failed; });
      std::printf("D 失败 : %5.1f ms 返回，货没等到，谓词翻正靠的是 failed\n",
                  ms_since(t0));
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(70ms);
    { std::lock_guard<std::mutex> g(bay.m); bay.failed = true; }
    bay.cv.notify_all();
    waiter.join();
  }
  return 0;
}
```

```text
A 关闭 :  61.4 ms 返回，队列空着，谓词翻正靠的是 closed
B 超时 : 122.3 ms 返回，返回值 0，时限内没等到货
main   :  61.9 ms 发出停止请求
C 停止 :  64.2 ms 返回，返回值 0，谓词没成立
D 失败 :  72.2 ms 返回，货没等到，谓词翻正靠的是 failed
```

咱们看 A，关闭。等待在 61.4 毫秒返回，队列从头到尾是空的，谓词成立靠的是 `closed`。判据有两半。收盘这个动作得同时做两件事，`closed` 在锁里立起来，通知放在锁外发，少一件等待就散不了。散场之后您还得答得出为什么散：标志立着、队列空着，所以是“关了，没等到货”，货没丢，本来就没有。队列里剩着货时该怎么散，归上一篇的关闭语义管，这一篇不碰它。

B 是超时。122.3 毫秒返回，比 120 的时限多出 2 毫秒出头，零头来自定时器和调度的精度，3.1 里也见过这样的零头，超时不是闹钟。判据不在返回那一刻，在返回之后：`false` 到手，您打算重试还是放弃，得是代码里明确的一句。重试的话，时限从哪里重新算。放弃的话，往上报什么。时限本身从哪来也要答得出，调用方给的、配置里的、还是写死的数，半年后有人问起它为什么是 120，得有人接得住。3.1 检查清单里的最后一问，“兜底返回 `false` 之后您打算做什么”，答案就在这里。

C 是停止，结束时间不跟队列走，也不跟时限走，跟着请求走。61.9 毫秒打出请求那一行，64.2 毫秒等待返回，中间两毫秒出头，其中两毫秒是打印后让路的固定停顿，刨掉它，唤醒本身的耗时连半毫秒都不到。返回值是 `false`，含义照 3.1 的老话：谓词没成立。它不报被谁叫醒，这件事咱们在《让工作有序结束》碰过一回，那里拿 `wait_for` 验过，这里再碰一次。C 这一幕能接住请求，靠的是 `condition_variable_any` 带 `stop_token` 的重载，下一节专门讲。

D 是失败，四条里最容易被漏掉的就是它。上游干砸了、做不成了，这件事得有地方记录，谓词得读得到它，不然认输的一方已经走了，等待的一方还在等。谁来认领也得指定，我这版是上游认输的一方，现场里由 `main` 顶替，把 `failed` 立起来，换成专门收尸的一方也行，得有人。真有异常的场合，把它接在线程入口的 `catch` 里再落到标志上。失败往上传的正路是 `std::promise` 与 `std::future` 的异常传递，承诺一侧把异常装起来、未来一侧取走，第四单元 4.3《自行传递结果：`std::promise` 与 `std::future`》讲，咱们这里用最朴素的标志位，够用，也看得见。

## 停止请求进等待：stop_token 那组重载

这组重载只有 `condition_variable_any` 有，`condition_variable` 没有，C++20 起，条款在 [thread.condvarany.intwait]。一共三个，`wait`、`wait_until`、`wait_for`，全都带着谓词，全都返回 `bool`。您找不到不配谓词的版本，标准就没提供这一档，写这组等待时谓词是必选项。

等待的骨架，标准用一段等效代码写明（[thread.condvarany.intwait]/2）：为这一次调用，把这个条件变量登记到停止请求的通知关系上，然后 `while (!stoken.stop_requested()) { if (pred()) return true; wait(lock); } return pred();`。返回值从头到尾只报谓词真假（/1 与其后的注）。请求在等待开始之前就立着的话，循环一次都不进，当场把谓词结果交给您。带时限的版本还有一条注，谓词一开始成立、请求已立、时限已过，三种情形都不阻塞（/8 的注）。这些安排跟 3.1 讲过的谓词版 `wait_for` 是同一个思路，等于多接了一根线：叫醒它的来源，多了停止请求。

我把两幕放进一个程序里跑，consumer 换成 `std::jthread`，析构时它会自己请求停止再 `join`，等待接了 token，这次请求正好把它叫醒。第一幕队列空着。第二幕有货，通知故意不发，让停止请求来当叫醒的那一方。`parked` 还是 3.1 里那个停车计数，等的人挂稳之后 `main` 才动手：

```cpp
// stop_wake.cpp —— stop_token 进等待：第一幕空手走，第二幕货照交
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::mutex m;
std::condition_variable_any cv;
std::queue<int> q;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();

  { // 第一幕：队列空着，叫醒它的是停止请求
    std::jthread consumer([&](std::stop_token st) {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool got = cv.wait(lk, st, [] { return !q.empty(); });
      std::printf("consumer: %5.1f ms 离开等待，返回值 %d（队列是空的）\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main    : %5.1f ms 请求停止\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    consumer.request_stop();
  }   // jthread 析构：停止已经请求过，这里只剩 join

  while (!q.empty()) q.pop();
  parked = 0;
  auto t1 = Clock::now();

  { // 第二幕：货到了，通知故意不发，停止请求负责叫醒
    std::jthread consumer([&](std::stop_token st) {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool got = cv.wait(lk, st, [] { return !q.empty(); });
      if (got) {
        int v = q.front();
        q.pop();
        std::printf("consumer: %5.1f ms 拿到 %d，此刻 stop_requested = %d\n",
                    ms_since(t1), v, st.stop_requested() ? 1 : 0);
      } else {
        std::printf("consumer: %5.1f ms 空手离开，返回值 false\n", ms_since(t1));
      }
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    { std::lock_guard<std::mutex> g(m); q.push(42); }  // 故意不 notify
    std::this_thread::sleep_for(60ms);
    std::printf("main    : %5.1f ms 请求停止\n", ms_since(t1));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    consumer.request_stop();
  }
  return 0;
}
```

```text
main    :  61.6 ms 请求停止
consumer:  63.8 ms 离开等待，返回值 0（队列是空的）
main    :  61.4 ms 请求停止
consumer:  63.6 ms 拿到 42，此刻 stop_requested = 1
```

第一幕是咱们已经见过的样子：`false`，空手离开，打印请求到等待返回相隔两毫秒上下，大头还是那两毫秒的固定停顿。第二幕有意思的地方在返回值：`true`。谓词成立，货照交，哪怕停止请求已经立着，输出里 `stop_requested = 1`。返回值的意思从头到尾没变过，它是谓词的真假，不是“有没有停”。库把两件事分得很清：谁把您叫醒，您等的东西成没成立，各归各。停止请求路过的时候，顺手把已经到位的活带走，不算亏。

缝在哪里，库又怎么挡的，是我翻本机 GCC 16.2.1 的 `<condition_variable>` 读出来的（`/usr/include/c++/16/condition_variable`）。它替您注册的那个回调，函数体就是一句 `notify_all()`。而 `notify_all` 要拿它自己的内部锁，等待这一头拿着同一把内部锁，把停止请求再查一遍，查完才挂起。回调想发通知，得等这把锁空出来，也就是等“查完、还没睡下”那一段过去。通知落不进缝里，这组重载就不用您操心时序。

手头只有 `condition_variable` 的场合，桥咱们自己搭。《让工作有序结束》留过一句“等待与通知的细节归后面讲等待的单元”，说的就是这里。搭法三句话：注册一个 `stop_callback`，回调里 `notify_all`。谓词把 `token.stop_requested()` 算进去。注册赶在拿锁与 `wait` 之前，因为“两头”是两种时序。注册那一刻请求已经立着，回调当场就在注册线程执行（[stoptoken.concepts]），这时它要拿同一把 m，而您手里可能已经锁着 lk，同一条线程再进一次非递归的 mutex，就地僵住。注册放在拿锁前面，这一路回调拿到的是把空锁，通知落空也无妨，waiter 拿锁之后谓词看见 `stop_requested()` 已经立着，压根不睡。另一路是请求在等待中到达，回调跑在请求方线程上，靠拿同一把锁挡缝，下一段推演的就是它。

```cpp
// bridge.cpp —— 手头只有 condition_variable 时，用 stop_callback 搭桥
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::mutex m;
std::condition_variable cv;
bool closed = false;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();
  std::stop_source source;
  auto token = source.get_token();

  std::thread waiter([&] {
    // 桥：注册赶在拿锁之前，两头都罩得住
    // 注册时已立：回调当场在注册线程执行，此时 m 还空着，不会自己卡自己
    // 等待中到达：回调跑在请求方线程，拿同一把锁挡缝
    std::stop_callback cb(token, [&] {
      { std::lock_guard<std::mutex> g(m); }   // 拿同一把锁，挡住检查与入睡之间的缝
      cv.notify_all();
    });
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [&] { return closed || token.stop_requested(); });
    std::printf("waiter: %5.1f ms 离开等待，closed = %d\n",
                ms_since(t0), closed ? 1 : 0);
  });

  while (parked.load() < 1) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(60ms);
  std::printf("main  : %5.1f ms 请求停止\n", ms_since(t0));
  std::fflush(stdout);
  std::this_thread::sleep_for(2ms);
  source.request_stop();
  waiter.join();
  return 0;
}
```

```text
main  :  61.3 ms 请求停止
waiter:  63.6 ms 离开等待，closed = 0
```

这一段推的是请求在等待中到达的一路，回调跑在请求方的线程上。回调里那句拿锁，看着多余，其实是整个桥承重的地方。咱们把不拿锁的情形推一遍：谓词在锁内查完，返回 `false`，接着 `wait` 做放锁加挂起这一步，原子。回调要是不拿锁，它可能恰好插在“查完、还没睡下”的缝里跑完 `notify_all`，这次通知扑空，等待接着睡，没人再叫它。回调拿了同一把锁，跟谓词检查互斥，它能拿到锁，说明那一小段已经过去，通知就落在了睡下之后。这个缝我在机器上构造不出稳定的复现，推演您自己也能验，库的实现拿内部锁挡它（上一段），照着做不吃亏。回调本身要短，它跑在 `request_stop()` 调用者的身上，《让工作有序结束》讲过，长一点就拖慢对方的关停。

《线程安全队列》已经把这套用进过 `pop`。它给 `pop` 加了一个收 `stop_token` 的版本，内部就是这组重载：返回 `false` 就退出循环，报“没拿到”。它同时保留 `close()`，文章里专门交代过：`close` 管生产者不再投放，`request_stop` 管消费者不在空队列上等死，两个都发，各管一头。咱们现在能把它的谓词读懂了：`!empty() || closed`，等待的人看见 `closed` 才肯散，正是 A 那一幕的写法。

超时和停止还能拼进同一次调用，咱们让它们同场跑一回。`wait_until` 的 `stop_token` 版收一个时间点加一个 token，谓词只有一条，写业务条件本身，别把 `stop_requested()` 塞进去，返回 `false` 之后再用它分辨是谁叫醒的：

```cpp
// deadline.cpp —— 超时和停止拼进同一次调用
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::mutex m;
std::condition_variable_any cv;
bool done = false;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();
  std::stop_source source;
  auto token = source.get_token();
  auto deadline = Clock::now() + 300ms;   // 时限从调用方来

  {
    std::jthread waiter([&] {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool ok = cv.wait_until(lk, token, deadline, [&] { return done; });
      if (ok) {
        std::printf("waiter: %5.1f ms 返回，done 成了\n", ms_since(t0));
      } else if (token.stop_requested()) {
        std::printf("waiter: %5.1f ms 返回，返回值 false，叫醒它的是停止请求\n",
                    ms_since(t0));
      } else {
        std::printf("waiter: %5.1f ms 返回，返回值 false，时限到头\n", ms_since(t0));
      }
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main  : %5.1f ms 请求停止（时限其实还没到）\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    source.request_stop();
  }   // jthread 析构：停止已请求过，这里只剩 join
  return 0;
}
```

```text
main  :  61.4 ms 请求停止（时限其实还没到）
waiter:  63.7 ms 返回，返回值 false，叫醒它的是停止请求
```

时限给的是 300 毫秒，请求那一行 61 毫秒打出，等待 64 毫秒就返回了，中间两毫秒出头，还是打印后让路的固定停顿占大头，咱们看得出时限没绑住它。返回 `false` 之后问一句 `token.stop_requested()`，是停止就把“放弃”归给关停流程，时限到头才归给超时，这样两种失败在日志里分得开。

## 一张检查表：等待能不能按设计结束

四种出路过完，收束成一张表。《让工作有序结束》的检查表管整条线程的退出路径，这里的表管一处处等待，两张开工前都值得过一遍。我写完一处等待，就拿这几行对它：

| 检查项 | 怎么判 | 典型病灶 |
|--------|--------|----------|
| 每处 `wait` 有没有退出路径 | 谓词为假时，什么事件能把它翻正？答不出来，它就是死等 | 谓词只有 `!q.empty()`，没人立标志，也没人 notify |
| 超时值从哪来 | 调用点传参、配置，还是写死？`false` 之后重试还是放弃，是哪一句 | 时限埋在深处的调用里，`false` 之后什么都不做，接着进下一轮循环 |
| 停止请求谁发 | 谁握 `stop_source`？`jthread` 析构会发一次，`close`（关队列那头）与 `request_stop` 各管哪头，说清了吗 | 关停只 close 不 stop，等待散不了。只 stop 不 close，投放没人拦 |
| 假唤醒后条件还成不成立 | 醒来第一件事是重算谓词吗？带谓词的 `wait` 与 `stop_token` 重载替您做，裸 `wait` 得自己补 `while` | 醒来不查条件直接取货 |
| 等不到的事有没有记录 | “永远等不到”能不能被谓词看见？上游死了谁负责把标志立起来 | 生产者崩了，消费者在空队列上睡到有人杀进程 |

咱们把表拿去对开头的 A 到 D，一行行都答得出：A 的谓词带 `closed`，收盘的人在锁里立标志、锁外发通知。B 的时限写在调用点，`false` 之后打印放弃。C 的 `stop_source` 在 `main` 手里，token 进了等待。D 的 `failed` 有认领人。假唤醒一路由带谓词的等待兜住。您哪一行答不出，那段代码就还欠一句明确的话，补上再交。

## 下一步

第三单元收在 3.4《按协作需求选择工具》上，`latch`、`barrier`、信号量。它们是更高一层的等待，等的方式变了，结束路径的问题一个没变，到那里咱们把同样的几问再问一遍。单元的收尾作品是一个能够关闭、能够结束等待的生产者—消费者程序，单独设计，还没成文，发布后补进单元导航。

翻页之前，请您把手头每一处等待的谓词翻出来，对着表过一遍。答不出的行，就是这一篇替您准备的那句话该落下去的地方。

## 参考资源

本篇的推进顺序照旧参照《C++ Concurrency in Action》（Anthony Williams，Manning）里队列与协作取消的章节安排，例子和文字都是我自己写的。实验在本机 GCC 16.2.1 上编译实测（`g++ -std=c++20 -O2 -pthread`，WSL2，16 逻辑核，2026-11-16），四份源码在临时目录 `/tmp/concurrency-u3/` 里编译运行，没有进仓库。文中的输出都是单次运行的读数，毫秒那一列重跑会差，我重跑过一遍，B 落在 120.6 毫秒，其余各处差别在一两毫秒之内。标准行为的断言按 eel.is 上的现行工作草案核对，条款附在正文与条目后面。`condition_variable_any` 内部实现那一段，出处是本机头文件，没有网页可引。cppreference 的页面以它标注的版本为准。

- [std::condition_variable_any（cppreference）][cvany]，C++11 起的通用版本，C++20 加了 `stop_token` 等待。
- [std::condition_variable_any::wait（cppreference）][cvany-wait]，三个重载与 `stop_token` 版的等效代码。
- [Interruptible waits（现行草案 thread.condvarany.intwait）][draft-intwait]，登记语义、等效展开、返回值只报谓词真假。
- [Class condition_variable（现行草案 thread.condition.condvar）][draft-condvar]，`wait_for` 的返回值与谓词版展开，B 那一幕的依据。
- [std::stop_token（cppreference）][token]，读端，查询请求与状态。
- [std::stop_callback（cppreference）][callback]，注册时机与当场执行的规则。
- [std::jthread（cppreference）][jthread]，析构里请求停止再 join。
- [C++ 标准草案：停止令牌概念条款（stoptoken.concepts）][draft-stop]，注册与回调执行的同步。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。
- GCC 16.2.1 自带的 `<condition_variable>`（本机路径 `/usr/include/c++/16/condition_variable`）：`condition_variable_any` 的内部锁与回调写法从这里读出。

[cvany]: https://en.cppreference.com/w/cpp/thread/condition_variable_any
[cvany-wait]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[draft-intwait]: https://eel.is/c++draft/thread.condvarany.intwait
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[token]: https://en.cppreference.com/w/cpp/thread/stop_token
[callback]: https://en.cppreference.com/w/cpp/thread/stop_callback
[jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[draft-stop]: https://eel.is/c++draft/stoptoken.concepts
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
