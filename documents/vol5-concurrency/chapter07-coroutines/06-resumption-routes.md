---
title: "两条恢复路线：把 Task 接上调度器"
description: "教材版的 Loop 与 Yield、博客调度器的统一恢复摆在一起，看清对称交接与非对称恢复各适合什么，最后回到那个问题：恢复协程的到底是哪条线程。"
chapter: 5
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 两条恢复路线：把 Task 接上调度器

旧文的调度器雏形是就绪队列加 run 循环，住在旧目录的《promise_type 与 awaitable》里。咱们把它重写并接上 Task。分工摆出来就三句话。Task 管一段工作的一生，从创建到销毁。Loop 管排队，决定下一个跑谁。Yield 管让出，协程自己把位置让出来排到队尾。把工作的一生与执行的位置分开，是调度器能成立的前提，也是单元七想交给您的组织方式。

咱们让两段流水线排队跑，pipeline 串起 fetch 与 process 两站，每站干完一段活就让出一轮。Task 沿用上一篇《把结果包起来》的教学版 Task，Loop、Yield 与三个协程是新增的：

<OnlineCompilerDemo
  title="最小调度器：就绪队列加单线程循环"
  source-path="code/examples/vol5/chapter07/13_exp_i_loop.cpp"
  description="两段流水线排队跑，Yield 让出的协程排回队尾，链上的完工交接抄近路，一条线程把六个协程全伺候完。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_i_loop.cpp —— 最小调度器：就绪队列加单线程循环，两段流水线排队跑
// Task 与 exp_h_task.cpp 相同，已拼在本文件顶部
#include <coroutine>
#include <cstdio>
#include <deque>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

int g_frames = 0;   // 协程帧的在册计数，教学演示销毁时机用，非线程安全

template <typename T>
class Task {
public:
  struct promise_type {
    T result{};
    std::exception_ptr failure{};
    std::coroutine_handle<> continuation{};   // 等我做完的上一级协程，根 Task 手里没有

    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }   // 懒启动：创建时不跑
    void return_value(T value) { result = std::move(value); }
    void unhandled_exception() { failure = std::current_exception(); }

    // 最终挂起点的交接，sym 姿势：有人等就交回去，没人是根，交还最初的恢复者
    auto final_suspend() noexcept {
      struct Done {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<promise_type> me) noexcept {
          auto c = me.promise().continuation;
          if (!c) return std::noop_coroutine();
          return c;
        }
        void await_resume() noexcept {}
      };
      return Done{};
    }

    // 帧计数，7.2 frame_alloc 的同款手法
    void* operator new(std::size_t size) {
      ++g_frames;
      return ::operator new(size);
    }
    void operator delete(void* ptr, std::size_t size) {
      --g_frames;
      ::operator delete(ptr, size);
    }
  };

  explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
  ~Task() { if (h_) h_.destroy(); }   // RAII：帧的收回交给析构函数
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& o) noexcept : h_(o.h_) { o.h_ = {}; }
  Task& operator=(Task&& o) noexcept {
    if (this != &o) {
      if (h_) h_.destroy();
      h_ = o.h_;
      o.h_ = {};
    }
    return *this;
  }

  // 三件套：Task 自己就是 awaiter，co_await 一个 Task 就是在等它做完
  bool await_ready() noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiter) noexcept {
    h_.promise().continuation = waiter;   // 登记在前
    return h_;                            // 启动在后，sym 姿势把子协程跑起来
  }
  T await_resume() { return take_result(); }

  void start() { h_.resume(); }   // 根 Task 的启动口：谁调它，协程体就在谁的线程上跑
  T result() { return take_result(); }
  std::coroutine_handle<promise_type> handle() const { return h_; }   // 交给调度器用

private:
  T take_result() {
    if (h_.promise().failure)
      std::rethrow_exception(h_.promise().failure);
    return std::move(h_.promise().result);
  }

  std::coroutine_handle<promise_type> h_;
};

class Loop {
public:
  void schedule(std::coroutine_handle<> h) { ready_.push_back(h); }
  void run() {
    while (!ready_.empty()) {
      auto h = ready_.front();
      ready_.pop_front();
      h.resume();   // 谁调 run，队列里的协程就在谁的线程上跑
    }
  }

private:
  std::deque<std::coroutine_handle<>> ready_;
};

// 让出：挂起，把自己放回队尾，轮到排在后面的协程
struct Yield {
  explicit Yield(Loop& loop) : loop_(loop) {}
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) { loop_.schedule(h); }
  void await_resume() noexcept {}

private:
  Loop& loop_;
};

Task<std::string> fetch(Loop& loop, int id) {
  std::printf("  fetch(%d): 开始取数\n", id);
  co_await Yield{loop};
  std::printf("  fetch(%d): 取到了\n", id);
  co_return "数据" + std::to_string(id);
}

Task<std::string> process(Loop& loop, std::string data, int id) {
  std::printf("  process(%d): 拿到 %s，开始加工\n", id, data.c_str());
  co_await Yield{loop};
  std::printf("  process(%d): 加工完成\n", id);
  co_return data + "(已加工)";
}

Task<std::string> pipeline(Loop& loop, int id) {
  std::string data = co_await fetch(loop, id);
  std::string out = co_await process(loop, data, id);
  co_return out;
}

int main() {
  Loop loop;
  Task<std::string> a = pipeline(loop, 1);
  Task<std::string> b = pipeline(loop, 2);
  loop.schedule(a.handle());
  loop.schedule(b.handle());
  std::printf("== 循环开跑 ==\n");
  loop.run();
  std::printf("== 队列空了 ==\n");
  std::printf("a = %s\n", a.result().c_str());
  std::printf("b = %s\n", b.result().c_str());
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_i_loop.cpp -o exp_i_loop
./exp_i_loop
```

```text
== 循环开跑 ==
  fetch(1): 开始取数
  fetch(2): 开始取数
  fetch(1): 取到了
  process(1): 拿到 数据1，开始加工
  fetch(2): 取到了
  process(2): 拿到 数据2，开始加工
  process(1): 加工完成
  process(2): 加工完成
== 队列空了 ==
a = 数据1(已加工)
b = 数据2(已加工)
```

输出的次序咱们逐行对。循环起手弹出 a，pipeline(1) 跑进 fetch(1)，打印开始取数，随即被 Yield 挂起排到队尾，控制权回到 run。接着弹出 b，fetch(2) 同样让出一轮。您看第三轮，弹出的是 fetch(1)，它报完取到了，完工交接没有走队列，Done 把 pipeline(1) 直接叫醒，pipeline(1) 落地就调 process(1)，process(1) 打印拿到数据又让出一轮回队列，整段过程都发生在 run 的单次 resume 里面。再看第五行与第六行的次序，process(1) 拿到了数据，fetch(2) 才报取到了，这正是 sym 交接的痕迹，链上的交接抄近路，队列里排的只有让过身的协程。

剩下几行咱们跟着队列往下看，process(1) 与 process(2) 陆续完工，各自的完工交接同样抄近路把 pipeline 叫醒，pipeline 是根，Done 交出 noop，控制流回到 run，队列空了循环收场。a 与 b 的结果在循环外取得，全程只有 run 一个恢复者，一条线程把两段流水线的六个协程全伺候完了。

咱们把它跟单元一选读里的内核调度摆在一起比（见《[线程在机器上是什么](./../chapter01-start-concurrency/09-what-is-a-thread)》），内核的时间用完了就换人，不管程序愿不愿意。Loop 是纯协作式，协程不让出就独占到死，写调度器的人与写协程的人得共同守住让出的纪律。跟旧文的调度器比，Task 把手动 destroy 的活接了过去，调度器只管排队，谁也不惦记销毁。真要把等待做成异步，登记的去向《co_await 怎样参与等待》的 Gate 一节交代过，事件循环与 Echo-Server 有了正主，本单元 07、08 两篇选读就是它们。

## 两条恢复路线

把上一节与博客调度器的读数摆在一起，两条恢复路线就现了形。博客调度器那边，A、B、C 三个协程交替打印，恢复者从头到尾只有一个：run 循环。协程挂起时把句柄登记进就绪队列或者睡眠堆，控制权交回 run，下一个恢复谁、什么时候恢复，全归调度器持筹。教材版这边，Yield 让出走的也是队列，可链上的完工交接不走——Done 把 continuation 直接叫醒，pipeline 落地就调 process，整段发生在 run 的单次 resume 里面。

一条是非对称恢复：调度器统一持筹，协程挂起就是交还控制权，恢复的时机与顺序全在调度器手里。好处是调试好做，断点打在 run 里，每一次恢复都看得见；调度策略要改，改调度器一处就够。代价是每次交接都要回一趟队列，链路深的时候，一环完工要绕调度器一圈才轮到下一环。

另一条是对称交接：协程挂起时不回调度器，直接把控制权递给登记好的等待者，编译器把这个"递"优化成一次栈上的跳转。好处是深链不吃栈、不绕队列，上一篇 Task 链上的完工交接走的就是它。代价是恢复的路径散进了各个挂起点，断点只打在 run 里看不见全貌。

两条路线没有高下，各有各的场面。要集中的调度策略与可观测性，用非对称的统一恢复；要链路的短平快，用对称交接。教材版的 Task 两样都给您看了：await_suspend 返回句柄的那一刻，走的就是对称交接；Loop 与调度器，是非对称恢复的最小样板。

## 协程与线程：谁在哪个线程上恢复

单元一的老问题，咱们在本篇收。第二篇引过标准的命名，调用恢复成员函数的求值动作叫 resumer（[dcl.fct.def.coroutine]/8），协程体不自带线程，谁调 resume，它就接着跑在谁的调用栈上。第一篇的一幕量过同一段函数体换线程的读数，咱们把立场落到 Task 上：Task 自己不带线程，start 谁调、Loop 谁跑，整条链就在谁的栈上活着。链上的交接全按 sym 姿势写，交接不换线程，它只是把控制权递给下一级，脚下还是同一条线程。

三幕实验把立场量给您看。幕一 main 自己 start，幕二 start 交给一条 driver 线程，幕三任务在 main 创建、循环却搬上另一条 driver 跑：

<OnlineCompilerDemo
  title="三幕实验换恢复入口：main 自己 start、driver 线程 start、循环搬上 driver"
  source-path="code/examples/vol5/chapter07/14_exp_j_threads.cpp"
  description="三幕实验换恢复入口：main 自己 start、driver 线程 start、循环搬上 driver，root 前后半与 leaf 的线程标识跟着恢复入口走。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// 源码清单见 code/examples/vol5/chapter07/14_exp_j_threads.cpp，与本篇演示一致
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_j_threads.cpp -o exp_j_threads
./exp_j_threads
```

```text
main 在线程 130680546568064

  root 前半      在线程 130680546568064

  leaf(10)       在线程 130680546568064

  root 后半 v=11 在线程 130680546568064

幕一 result=11

  root 前半      在线程 130680539313856

  leaf(20)       在线程 130680539313856

  root 后半 v=21 在线程 130680539313856

幕二 result=21

  root 前半      在线程 130680539313856

  leaf(30)       在线程 130680539313856

  root 后半 v=31 在线程 130680539313856

幕三 result=31
```

三幕的读数对得整整齐齐。幕一，root 前半、leaf、root 后半全打 main 的线程标识，恢复者只有 main。幕二，start 只写了一处，整条链的三个打印全换了 driver 的标识，main 创建的 Task 在别的线程上跑完了，它的一生跟着恢复入口走。幕三换了个恢复入口，loop.run 搬上 driver，root 的三个打印跟着落过去。您再留意一处细节，幕二与幕三的 driver 标识数值一样，那是系统把刚收回的线程标号复用了，判断归属别看数值本身，看它不等于 main。

跨线程使用有一条安全边界要交代。幕二里 main 把 Task 交给 driver，靠 thread 的构造与 join 建立 happens-before，main 在 join 之后读 result 才有保障。这对同步关系单元五讲过（见《[一个线程怎样观察另一个线程的修改](./../chapter05-atomic-memory-model/03-happens-before)》）。教学版自己不加锁，两条线程并发 resume 同一条协程是数据竞争，第一篇引过 [coroutine.handle.resumption] 的警告。要用 Task 在线程之间交接，同步得您自己补，位置在教学版差距清单里挂着。

单元一给世界定的秩序是工作长在线程身上，一条线程一段栈，等待把线程占住，一千个等待要一千条线程预订约 8 GB 地址空间（第一篇的读数）。单元七把两件事分了家：工作是 Task，Task 是所有物，它有帧、有结果、有异常、有析构。线程是恢复的执行者，一次 resume 服务一段继续，服务完就去干别的。单元一的 join 与 detach 管线程的命（见《[第一个 std::thread 程序](./../chapter01-start-concurrency/03-first-std-thread)》），本篇的析构管工作的帧，收场都落在析构函数上，管的对象换了。单元七还配了一个小作品，方向是解释协程、线程与调度的关系，素材咱们在本篇备齐了，练习单独设计。

## 轮到您动手的时候

- 把 exp_i 的就绪队列换成两条线程共享的队列（加锁），两边的 run 同时往外弹协程，观察打印交错的变化，想想 continuation 的登记与完工的交接各自需要什么同步。
- 给 Loop 补一个 run_one，一次只弹一个就绪协程，配合定时器的思路想想事件循环还缺什么。完整的答案，本单元 08 号选读《三块积木拼出 Echo-Server》里有现成的实现。

## 参考资源

本篇的调度器讲解由笔者 2025 年 10 月的博客《理解 C++20 的革命特性——协程支持 2：编写简单的协程调度器》扩写而来，单例模板与睡眠 awaiter 的讲法照博客原文平移。素材的另一路来自旧目录 ch06 的《promise_type 与 awaitable》，SimpleTask、SafeTask 与调度器在旧文里已有雏形，例子与文字全部重写。三个实验另拿 -fsanitize=address,undefined 各复跑一遍，消毒器没有报告。语言机制的断言按 eel.is 上的现行工作草案与 cppreference 核对（2026-11-17 访问），条款号与段号可能随草案更新挪动。

- [Await expression（现行草案 expr.await）](https://eel.is/c++draft/expr.await)，await_suspend 的三种返回类型、句柄返回值的接力恢复注。
- [Coroutine definitions（现行草案 dcl.fct.def.coroutine）](https://eel.is/c++draft/dcl.fct.def.coroutine)，resumer 的命名、final_suspend 的 noexcept 要求、协程状态的销毁次序。
- [Resumption（现行草案 coroutine.handle.resumption）](https://eel.is/c++draft/coroutine.handle.resumption)，resume 与 destroy 的前置条件、跨执行代理恢复的实现定义行为、并发恢复的警告。
- [No-op coroutines（现行草案 coroutine.handle.noop）](https://eel.is/c++draft/coroutine.handle.noop)，noop 句柄的 resume 与 destroy 都是空操作。
- [Coroutines (C++20)（cppreference）](https://en.cppreference.com/w/cpp/language/coroutines)，完成的时序（未接住异常、unhandled_exception、final_suspend）、挂在最终挂起点的协程只能被销毁、协程状态的组成。
- [std::exception_ptr（cppreference）](https://en.cppreference.com/w/cpp/error/exception_ptr)，异常指针的共享所有权与跨线程传递。
- [C++ Coroutines: Understanding Symmetric Transfer — Lewis Baker](https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer)，naive 与 sym 两种交接姿势的讲法，本篇 Task 的交接姿势与"两条恢复路线"一节的参照。
