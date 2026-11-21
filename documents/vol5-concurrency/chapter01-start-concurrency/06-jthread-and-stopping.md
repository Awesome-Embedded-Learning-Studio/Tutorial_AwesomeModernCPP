---
title: "让工作有序结束：std::jthread 与停止三件套"
description: "用 std::jthread 的析构把停止请求接进收尾路径，讲清 stop_source、stop_token、stop_callback 三件套，回看 atomic 标志轮询的短板。"
chapter: 5
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
---

# 让工作有序结束：std::jthread 与停止三件套

上一篇收尾剧透过的主角，这篇登场：`std::jthread`，析构里先请求停止、再 join。"能等"上一篇有了，C++20 补的是"能叫停"，而且"叫停"这件事本身成了一套有定义、有同步关系的协议。回调注册的竞态与一张逐条检查退出路径的清单，归下一篇。

`std::jthread` 在 `<thread>` 里，停止令牌在 `<stop_token>` 里，都是 C++20 起的设施。下面的输出都是单次运行的读数，同一个环境里重跑，毫秒数和线程编号会小幅跳动。还在 C++20 之前的代码库只能走 atomic 标志轮询的旧路，后面有一节专门讲它。

咱们把 `std::thread` 换成 `std::jthread`，删掉那行 `join()`，程序照跑：

<OnlineCompilerDemo
  title="jthread：不写 join，析构自动等"
  source-path="code/examples/vol5/chapter01/37_jthread_auto_join.cpp"
  description="std::jthread 的析构函数替您 join，一行不写也不丢线程。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// jthread:不写 join,析构自动等
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;

void worker() {
    std::this_thread::sleep_for(100ms);
    std::println("worker done");
}

int main() {
    std::jthread t(worker);
    // 不写 join，析构时自动等
}
```

</OnlineCompilerDemo>

咱们跑出来一行 `worker done`。到这一步，它跟上一篇那个聚焦析构的 `JoiningThread` 片段没有区别，都在析构里等线程结束。区别在等之前那一步。标准草案 [thread.jthread.cons] 对 `~jthread()` 的规定是，`joinable()` 为 true 时调用 `request_stop()`，然后调用 `join()`（C++20 起）。`JoiningThread` 只做后一半，jthread 做全了。

这个顺序不是文档里的一句话，跑起来看得见。咱们让工作线程一直循环到看见停止请求，主线程从头到尾不调 `request_stop()`，只让对象离开作用域：

<OnlineCompilerDemo
  title="jthread 自动请求停止"
  source-path="code/examples/vol5/chapter01/38_stop_token_basic.cpp"
  description="离开作用域时析构先请求停止再等待，worker 的循环看得见停止令牌。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// jthread 自动请求停止:离开作用域,worker 的循环看得见
#include <chrono>
#include <print>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

void worker(std::stop_token token) {
    std::stop_callback cb(
        token, [] { std::println("[callback] running on thread {}", std::this_thread::get_id()); });
    int ticks = 0;
    while (!token.stop_requested()) {
        ++ticks;
        std::this_thread::sleep_for(20ms);
    }
    std::println("[worker] saw the request after {} ticks, thread {}", ticks,
                 std::this_thread::get_id());
}

int main() {
    std::println("[main] thread {}", std::this_thread::get_id());
    {
        std::jthread t(worker);
        std::this_thread::sleep_for(150ms);
        std::println("[main] leaving the scope, no request_stop call");
    }
    std::println("[main] destructor returned");
}
```

</OnlineCompilerDemo>

咱们跑出来的五行是这样的（线程编号因机器而异）：

```text
[main] thread 125682764838784
[main] leaving the scope, no request_stop call
[callback] running on thread 125682764838784
[worker] saw the request after 8 ticks, thread 125682757662400
[main] destructor returned
```

请您把回调那一行和 `[main]` 那一行对一下编号，两个数字相同。停止请求是主线程在析构函数里发出的，不是某个后台线程替它发的。worker 的编号跟它们不同，它在自己的执行流上循环，看见 `stop_requested()` 变成 true 就退出，析构里的 join 才等得到它。要是没有 request_stop 这一步，worker 的循环条件永远不成立，析构就永远等下去，跟只做 join 的 `JoiningThread` 一样。

请求发出去，只对看得见 token 的线程有用。`std::jthread` 的构造函数试两种调用形式。把内部 token 插在函数对象后的第一个参数位能编过，就用带 token 的调用。编不过，就不带 token 调用（[thread.jthread.cons] 的 Effects，C++20 起）。咱们写一个不接受 token 的线程函数试试：

<OnlineCompilerDemo
  title="析构等待：worker 没干完就得等"
  source-path="code/examples/vol5/chapter01/39_jthread_dtor_waits.cpp"
  description="worker 睡满 300 毫秒没人请求停止，析构就等满 300 毫秒——等待是析构的底线。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// jthread 析构等待:worker 睡满 300ms,析构就等满 300ms
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
static clock_type::time_point t0;

int main() {
    t0 = clock_type::now();
    {
        std::jthread t([] {
            std::this_thread::sleep_for(300ms);
            std::println("[worker] finished its own work, nobody asked it to");
        });
        std::this_thread::sleep_for(50ms);
        std::println(
            "[main] leaving the scope at {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - t0).count());
    }
    std::println(
        "[main] destructor returned at {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - t0).count());
}
```

</OnlineCompilerDemo>

```text
[main] leaving the scope at 50 ms
[worker] finished its own work, nobody asked it to
[main] destructor returned at 301 ms
```

主线程 50 毫秒就离开了作用域，析构却到 301 毫秒才返回。中间那约 250 毫秒不是 request_stop 花的。request_stop 发出请求就返回了，join 在等 worker 睡完自己的 300 毫秒，咱们也只能陪着等。请求确实发出去了，可 worker 手里没有 token，没人看，请求等于没发。协作式停止就是这个意思，它不杀线程，只发请求。

jthread 身上还有三个入口，`request_stop()`、`get_stop_source()`、`get_stop_token()`，都是转调内部那个 `stop_source`。要用得细，比如把同一个 token 传给几个组件，就得从它们往外拿。下一节咱们看这三件套本身。

## stop_source、stop_token、stop_callback 三件套

一块大家都能看见的停止状态（stop state），由标准库管理，三件套是访问它的三种角色。`std::stop_source` 是写端，`request_stop()` 发出请求，`get_token()` 从这里派生读端。读端就是 `std::stop_token`，手里两个查询：`stop_requested()` 问请求来没来，`stop_possible()` 问自己有没有关联上停止状态。`std::stop_callback` 管通知，挂一个可调用对象上去，请求到达时执行。三个都声明在 `<stop_token>` 里，C++20 起。

先不接线程，空手把三件套的问答跑一遍：

<OnlineCompilerDemo
  title="stop_source 与 stop_token 的状态查询"
  source-path="code/examples/vol5/chapter01/40_stop_source_states.cpp"
  description="两次 request_stop 只有一次返回真；默认令牌与 nostopstate 源都停不了任何东西。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// stop_source 与 stop_token 的状态查询:两次 request_stop 只有一次为真
#include <print>
#include <stop_token>

int main() {
    std::stop_source source;
    std::stop_token token = source.get_token();

    std::println("before: source.stop_possible()={} token.stop_possible()={}",
                 source.stop_possible(), token.stop_possible());
    std::println("before: source.stop_requested()={} token.stop_requested()={}",
                 source.stop_requested(), token.stop_requested());
    std::println("request_stop() #1 -> {}", source.request_stop());
    std::println("request_stop() #2 -> {}", source.request_stop());
    std::println("after:  token.stop_requested()={}", token.stop_requested());

    std::stop_token empty;
    std::println("default token: stop_possible()={} stop_requested()={}", empty.stop_possible(),
                 empty.stop_requested());

    std::stop_source silent(std::nostopstate);
    std::println("nostopstate source: stop_possible()={} request_stop() -> {}",
                 silent.stop_possible(), silent.request_stop());
}
```

</OnlineCompilerDemo>

```text
before: source.stop_possible()=true token.stop_possible()=true
before: source.stop_requested()=false token.stop_requested()=false
request_stop() #1 -> true
request_stop() #2 -> false
after:  token.stop_requested()=true
default token: stop_possible()=false stop_requested()=false
nostopstate source: stop_possible()=false request_stop() -> false
```

输出里有几件事值得您逐行看。默认构造的 source 立刻有停止状态，`stop_possible()` 是 true。`request_stop()` 调多少次都安全，只有第一次返回 true，第二次调用不会重复触发回调。请求发出之后不撤回。默认构造的 token 是脱开的（disengaged），没有关联状态，两个查询都返回 false。用 `std::nostopstate` 构造的 source 也没有状态，`request_stop()` 返回 false。token 可以拷贝、可以比较，同一个 source 派生出来的 token 彼此相等，所以咱们拿它当参数传来传去不丢身份。

jthread 内部本来就有一个 `stop_source`，`t.request_stop()` 转调的就是它。要用内部那个，就调 `t.get_stop_source()`。您只用 jthread、不碰三件套，析构那一步照样发请求。要让同一次停止覆盖几条线程，就得自己拿一个 source 往外派 token。下面的演示为了说明三件套不依赖 jthread，另起一个 source，用三条 `std::thread` 跑一对多：

<OnlineCompilerDemo
  title="一个 source 停一组线程"
  source-path="code/examples/vol5/chapter01/41_one_source_many_threads.cpp"
  description="三条 processor 共享同一枚令牌，一次 request_stop 各自看见、各自收场。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 一个 stop_source 停一组线程:一次 request_stop,三条 processor 各自看见
#include <chrono>
#include <print>
#include <stop_token>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

void processor(std::stop_token token, int id) {
    int rounds = 0;
    while (!token.stop_requested()) {
        ++rounds;
        std::this_thread::sleep_for(50ms);
    }
    std::println("processor {} stopped after {} rounds", id, rounds);
}

int main() {
    std::stop_source source;
    std::vector<std::thread> pool;
    for (int id = 1; id <= 3; ++id) {
        pool.emplace_back(processor, source.get_token(), id);
    }
    std::this_thread::sleep_for(150ms);
    std::println("one request_stop() call");
    source.request_stop();
    for (auto& t : pool)
        t.join();
    std::println("all joined");
}
```

</OnlineCompilerDemo>

```text
one request_stop() call
processor 2 stopped after 3 rounds
processor 3 stopped after 3 rounds
processor 1 stopped after 3 rounds
all joined
```

一次请求，三条线程都在下一轮循环里看见它。停止请求是发给停止状态的，不是发给某一条线程的。一个 source 派出去的 token 都看同一份状态，谁拿着 token 谁就能看见请求。三条线程打印的次序每次都会不一样，它们各自检查的时机由调度决定，咱们在《第一个 std::thread 程序》里见过同样的交错。线程再多些，一行里的字符也可能绞在一起，`std::cout` 的那个毛病那次也见过。

光能查询还不够。有时候请求到达的那一刻，正好是关文件、断连接、唤醒一个等待的最好时机，这就要 `std::stop_callback` 上场。它注册一个可调用对象，请求到达时执行。咱们把注册时机跑一跑：

<OnlineCompilerDemo
  title="回调在哪个线程上跑"
  source-path="code/examples/vol5/chapter01/42_callback_thread.cpp"
  description="请求前注册的回调跑在发起请求的线程上，请求之后再注册的当场就地执行。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// stop_callback 在哪个线程上跑:请求前注册的回调跑在请求方,请求后注册的当场跑
#include <print>
#include <stop_token>
#include <thread>

int main() {
    std::stop_source source;
    std::stop_token token = source.get_token();
    std::println("main is thread {}", std::this_thread::get_id());

    std::println("registering a callback, before request");
    std::stop_callback cb(
        token, [] { std::println("  [callback] runs on thread {}", std::this_thread::get_id()); });
    std::println("registration returned");

    std::println("calling request_stop");
    source.request_stop();
    std::println("request_stop returned");

    std::println("registering another callback after the request");
    std::stop_callback late(
        token, [] { std::println("  [late] runs on thread {}", std::this_thread::get_id()); });
    std::println("late registration returned");
}
```

</OnlineCompilerDemo>

```text
main is thread 129462478755712
registering a callback, before request
registration returned
calling request_stop
  [callback] runs on thread 129462478755712
request_stop returned
registering another callback after the request
  [late] runs on thread 129462478755712
late registration returned
```

两次回调都跑在主线程上，跟 `main is thread` 那个编号一致。第一次是在 `request_stop()` 里面同步执行的，回调返回，`request_stop()` 才返回。第二次注册时请求已经发过了。标准规定，注册那一刻请求已经发出，回调就在构造函数里就地执行，不进回调列表（[stoptoken.concepts] 的注册条款，C++20 起）。所以它打在 “late registration returned” 前面，咱们能亲眼看到它没有等到以后才执行。您把 [callback] 和 [late] 两行放在一起看，可以得出一句实用的话：注册回调不保证以后再说，它可能当场就跑。所以，别在回调里做耗时操作，它占的是发请求的线程的时间。回调对象析构时会自动注销，注销的细则里有一段阻塞行为，是最容易写错的地方，后面讲 stop_callback 的注册时机与竞态时，咱们一起看。

## 停止协议的前身：atomic 标志轮询与它的不足（扩展）

本节带（扩展）标记，跳过它不影响本篇主线。不过现成代码里这套旧办法到处都是，认识它，您才知道停止令牌补的是哪几块短板。

C++11 起标准库有了 `std::thread`，可停止这件事没有现成设施可用。当时的常见做法是自己放一个 `std::atomic<bool>` 当停止标志，工作线程每次循环看一眼，主线程置位。咱们把它跑起来：

<OnlineCompilerDemo
  title="停止协议的前身：atomic 标志轮询"
  source-path="code/examples/vol5/chapter01/43_atomic_flag_polling.cpp"
  description="20 毫秒一问的轮询能停，但察觉有延迟——它就是停止三件套要替换的旧路。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 停止协议的前身:atomic 标志加轮询,20ms 一问
#include <atomic>
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::atomic<bool> stop_flag{false};
    auto t0 = std::chrono::steady_clock::now();

    std::thread worker([&] {
        long polls = 0;
        while (!stop_flag.load()) {
            ++polls;
            std::this_thread::sleep_for(20ms);
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
        std::println("worker noticed at {} ms after {} polls", ms, polls);
    });

    std::this_thread::sleep_for(50ms);
    stop_flag.store(true);
    std::println("flag set at 50 ms");
    worker.join();
}
```

</OnlineCompilerDemo>

```text
flag set at 50 ms
worker noticed at 60 ms after 3 polls
```

主线程在 50 毫秒置位，worker 到 60 毫秒上下、第三次轮询才看见。响应延迟由轮询间隔决定，间隔 20 毫秒，最坏就多等 20 毫秒。咱们想把延迟压下去，就把间隔调小，线程空转的时间跟着涨。咱们想省 CPU，就把间隔调大，停止就变迟钝。这个取舍是轮询形状的一部分，由间隔决定，代码写得再整齐也躲不掉。

轮询还有个更硬的问题，它叫不醒一个正在阻塞等待的线程。咱们让 worker 等在条件变量上，主线程只改标志，不 notify：

<OnlineCompilerDemo
  title="轮询错过窗口：没人通知就得睡满"
  source-path="code/examples/vol5/chapter01/44_poll_miss_window.cpp"
  description="标志在 sleep 中间置位，wait_for 没人唤醒只能睡满 500 毫秒——停止三件套补的就是这个洞。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 轮询错过窗口:标志在 sleep 中间置位,wait_for 还得睡满整个超时
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <print>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::atomic<bool> stop_flag{false};
    std::mutex m;
    std::condition_variable cv;

    std::thread worker([&] {
        std::unique_lock<std::mutex> lock(m);
        auto t0 = std::chrono::steady_clock::now();
        bool woke = cv.wait_for(lock, 500ms, [&] { return stop_flag.load(); });
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
        std::println("wait_for returned {} after {} ms", woke, ms);
    });

    std::this_thread::sleep_for(50ms);
    stop_flag.store(true);
    std::println("flag set to true at 50 ms, nobody notifies");
    worker.join();
}
```

</OnlineCompilerDemo>

```text
flag set to true at 50 ms, nobody notifies
wait_for returned true after 500 ms
```

唤醒这一头也一样，标志自己不会叫醒谁。标志在 50 毫秒就变成 true，worker 还是睡满了 500 毫秒才回来。返回的 true 是谓词的结果。所谓谓词，就是 wait 第三个参数那个返回 bool 的判断。标准对这一点写明了，返回值表示谓词是否为真，跟超时、跟被谁叫醒都没有关系。所以咱们拿返回值当“收到停止请求”的信号也不成立。您在同一个程序里补一次 `notify_all()`，等待 50 毫秒就结束了，中断等待靠的是通知，不是标志本身。

第三个短板是语义。您得自己约定几件事。谁能置这个标志？置了以后谁负责唤醒？标志活得比线程久吗？两条线程读到的值算不算同步？两个模块各有停止条件，怎么合并？有没有“停止可能”这样一个状态可以问？这些全是口头约定。每条线程、每个接手维护的人都得知道，漏一条就是偶发故障。`std::atomic<bool>` 管不到这一层。它在 relaxed 语义下，连“写标志之前写好的数据，对读到标志的线程可见”都不保证。relaxed 是最宽松的那档内存序，只保证这个变量自己的读写不乱，别的都不保证。

停止令牌把这些约定收进了标准。`request_stop()` 返回 true，与另一条线程上 `stop_requested()` 返回 true 之间有同步关系。注册回调与那次回调的执行之间也有同步关系（[stoptoken.concepts] 的同步条款，C++20 起）。您不用逐条背约定，库把它们变成了条款。没有 C++20 的场合，atomic 标志轮询照样能用。咱们只是要把上面每一条约定自己写下来，自己检查。


## 参考资源

本篇写作时借鉴了一本书，《C++ Concurrency in Action》，作者 Anthony Williams，取的是线程与协作式取消的章节推进思路。例子和文字都是笔者自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言查了 cppreference 与标准草案，条款号附在条目后面。

- [std::jthread（cppreference）][cppref-jthread]，C++20 起的自动 join 线程。
- [std::jthread::~jthread（cppreference）][cppref-jthread-dtor]，析构时请求停止再 join。
- [std::stop_source（cppreference）][cppref-source]，写端，发出停止请求并派生 token。
- [std::stop_token（cppreference）][cppref-token]，读端，查询请求与状态。
- [std::stop_callback（cppreference）][cppref-callback]，注册与注销停止回调。
- [C++ 标准草案：jthread 的构造与析构（thread.jthread.cons）][draft-jthread]，构造的两种调用形式与析构的顺序。
- [C++ 标准草案：停止令牌的概念条款（stoptoken.concepts）][draft-stop]，注册、注销、同步关系与 request_stop 的语义。

[cppref-jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[cppref-jthread-dtor]: https://en.cppreference.com/w/cpp/thread/jthread/~jthread
[cppref-source]: https://en.cppreference.com/w/cpp/thread/stop_source
[cppref-token]: https://en.cppreference.com/w/cpp/thread/stop_token
[cppref-callback]: https://en.cppreference.com/w/cpp/thread/stop_callback
[draft-jthread]: https://eel.is/c++draft/thread.jthread.cons
[draft-stop]: https://eel.is/c++draft/stoptoken.concepts
