---
title: "为什么需要暂停与恢复"
description: "一千个并发连接把等待压在线程身上的代价实测，回调、future 链、协程三种模型对同一道题的答法，以及协程把程序的写法从线程身上换开来的读数证据。"
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

# 为什么需要暂停与恢复

单元六收尾的时候，我留过一句方向：单元三的等待压在条件变量与线程上，线程等待的时候整条核闲着，协程换一种组织方式，等待的时候把线程还回去（收尾在《[怎样调整设计](../chapter06-validating-designs/03-adjusting-design)》，预告也挂在那里）。现在轮到兑现。第七单元讲的就是协程，本篇是开场，只回答一个问题：为什么需要可以暂停的工作。机制的部分，协程状态、`promise_type`、`co_await` 的协议，往后三篇接手，这里不抢。

您把前六个单元用过的工具在脑子里过一遍，`std::thread`、互斥量、条件变量、future、原子操作，全都长在线程身上。线程是它们共同的执行单位，等待也压在线程身上：线程睡在 `wait` 里，它就被眼下的等待占住，别的活插不进来。单元七请进来的协程换了个思路：工作可以自己停住，停住的时候把线程还回去，事件到了再从停住的位置接着跑。单元名里的“可以暂停的工作”，说的就是它。

本篇按三步走。场景摆前面，一千个并发连接，把等待压在线程身上的代价量给您看。接着是三种模型对同一道题的答法，回调、future 链、协程，各付各的代价。收尾回答一个容易被误解的问题：协程改变的是程序组织，不是线程数量，咱们拿读数作证。

两个小实验放在临时目录里编译跑过（`/tmp/concurrency-u7/`，GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2 内核 6.18.33.2，16 逻辑核，2026-11-17）。读数都是单次运行的读数，换台机器数字会漂，量级和判断不会。协程语言机制的断言，我按 cppreference 与现行草案核对过（2026-11-17 访问），出处收在参考资源里。

## 一千个并发连接的场景

场景是老朋友，单元一讲什么时候值得并发时露过面（[《什么时候值得引入并发》](../chapter01-start-concurrency/01-when-concurrency)）。咱们搭一台服务器，同时伺候一千个客户端连接，每个连接的日子差不多：请求进来，读一段，算一段，回一段，道别。读与回都是 I/O，一次网络读取等几毫秒到几百毫秒是常事，等的时候手里没有任何计算可做。一千个连接同时站在等待里，是很平常的下午。

最直觉的方案您在单元一就备齐了零件：来一个连接，开一条线程，`std::thread` 一包，让它从头伺候到尾。写起来直白，每个连接的执行现场就是一条线程的栈，逻辑顺着往下走。代价咱们量：

```cpp
// exp_a_threads.cpp —— 一千条"一连接一线程"的线程：量地址空间、物理内存与线程数
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

// 从 /proc/self/status 抄一行出来，VmSize 与 VmRSS 单位是 kB
static bool status_line(const char* prefix, char* out, size_t n) {
  FILE* f = std::fopen("/proc/self/status", "r");
  if (!f) return false;
  bool found = false;
  char line[256];
  while (std::fgets(line, sizeof line, f)) {
    if (std::strncmp(line, prefix, std::strlen(prefix)) == 0) {
      std::snprintf(out, n, "%s", line);
      found = true;
      break;
    }
  }
  std::fclose(f);
  return found;
}

static void report(const char* tag) {
  char line[256];
  if (status_line("Threads:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmSize:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmRSS:", line, sizeof line))
    std::printf("%s %s", tag, line);
}

int main() {
  constexpr int kN = 1000;
  std::printf("== before ==\n");
  report("");

  std::atomic<int> started{0};
  std::vector<std::thread> pool;
  pool.reserve(kN);
  for (int i = 0; i < kN; ++i) {
    pool.emplace_back([&started] {
      ++started;
      // 睡着等事件，模拟一条正在等 I/O 的连接线程
      std::this_thread::sleep_for(std::chrono::seconds(3));
    });
  }
  while (started.load() < kN)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

  std::printf("== %d threads parked ==\n", kN);
  report("");

  for (auto& t : pool) t.join();
  return 0;
}
```

```bash
g++ -std=c++20 -O2 -pthread exp_a_threads.cpp -o exp_a_threads
./exp_a_threads
```

```text
== before ==
 Threads:	1
 VmSize:	    7300 kB
 VmRSS:	    3944 kB
== 1000 threads parked ==
 Threads:	1001
 VmSize:	 8203564 kB
 VmRSS:	   12488 kB
```

咱们看读数，把两行字段对上号：VmSize 一行量地址空间，VmRSS 一行量物理内存，单位都是 kB。一千条线程把地址空间从 7,300 kB 抬到 8,203,564 kB，涨出约 8,196,264 kB，摊到每条线程 8,196 kB 上下。来源是每条线程自己的栈：Linux 的 glibc 给线程的默认栈大小跟着 RLIMIT_STACK 走，我机器上 `ulimit -s` 报 8192 kB，正好对上，多出来的小截是线程自己的管理结构。

物理内存的路数，咱们分两半说。VmRSS 只从 3,944 kB 涨到 12,488 kB，摊到每条睡着的线程不到 9 kB。8 MiB 是预订，不是占用：线程睡着等 I/O，栈深处它没碰过，物理页就没分下去。地址空间一侧也有真实开销：内核要为每段映射保存管理记录，地址空间摆多大，要记的虚存区就跟着有多少。页表与物理页跟实际触碰走，线程真跑起来、栈真被碰到，它们才跟上。所以“一千条线程吃掉 8 GB 内存”要修正成：一千条线程预订了约 8 GB 地址空间，映射的管理记录随之变多，物理内存与页表取决于它们实际走到多深。您想想，光是预订，地址空间就到了 8 GB 的量级！

调度的一侧您在 3.1 就认识：睡着的等待不烧 CPU，内核把线程停在原地（《[等待一个条件成立](../chapter03-wait-and-coordinate/01-wait-for-a-condition)》）。贵的地方在别处。一千个栈的预订摆着，事件到达时谁叫醒谁、谁上谁下，切换与缓存的开销跟着线程数走，等待一多、又一齐醒，切换的次数就上去了。

等待本身不贵，贵的是承载等待的线程。您把它从线程身上摘下来，才有出路。摘下来的工作放在哪里，就是三种模型各自的答案。

## 三种模型，同一道题的三种答法

题目只有一句：流程走到一半要等 I/O，等的时候能不能不占着一条线程。咱们把三种模型的取舍各压缩成一段，通史的细节住在旧目录（卷五改版前的旧章节，迁移完成后会撤下）的一篇通史《[异步编程演进：从回调地狱到协程](../ch06-async-io-coroutine/01-async-programming-evolution)》里，本篇例子全部重写，取舍两边一致。

### 回调：把“接下来做什么”交出去

思路直白：发起异步操作时递进去一个函数，操作完成后系统替您调它。写起来快，代价跟着就来。顺序散落在嵌套的回调里，读、算、回三步分成三层嵌套的函数，层数跟着流程长，五步十步之后右边的缩进能跑出屏幕。错误处理散在各层，每层自己查自己的一段，集中式的 `try`/`catch` 没有落脚处。闭包捕获的对象生命周期也险，回调真正执行的时候，被捕获的变量可能已经死了，悬垂与释放后使用在该模型里是常客。

回调本身几乎零开销，一个函数指针加一份捕获就是全部，高性能库里它一直活着。代价不在机器，在读代码与维护的人，咱们自己也在其中。

### future 链：单元四的工具到这里不够用

`std::async` 配 future，咱们在单元四用过：一段工作扔出去，future 收结果（[《用 `std::async` 执行计算》](../chapter04-organizing-tasks/01-launch-with-async)）。放到该场景里，取结果的办法是 `get()`，它把调用线程睡到结果就绪为止。等待又压回线程身上，只是位置从 `read` 挪到了 `get()`，线程照样被占住。

真正想要的是另一样东西：结果就绪之后自动接下一步。标准的 `std::future` 给不了，成员函数清单就是 `get`、`wait`、`wait_for`、`wait_until` 一类，`then` 不在其中（cppreference，2026-11-17 访问）。带 `then` 的续接长在并发技术规范（Concurrency TS，标准化的草案轨道）的 `std::experimental::future` 身上，该 TS 没有并进国际标准。还有一份随链增长的开销：future 背后的共享状态由实现分配，多数实现做一次堆分配，链长一步就多一份。异常倒是能沿着共享状态传到 `get()` 再重新抛出，该机制您在单元四用过。

### 协程：顺序写法回来了

写法回到顺序：函数体中间写 `co_await`，等的事情没就绪，函数从那里停住，把控制权交还，就绪之后从停住的位置接着跑。读、算、回三步一行挨一行，跟同步代码一个长相，挂起与恢复是编译器和您一起埋进去的。

C++20 把协程带进语言，三个关键字咱们挨个认：`co_await` 挂起等事情，`co_yield` 产出一个值挂起，`co_return` 收尾返回。函数体里出现任意一个，该函数就是协程（现行草案 [dcl.fct.def.coroutine] 第 1 段，cppreference 同述）。`<coroutine>` 头文件给支持接口，`suspend_always` 与 `suspend_never` 就住在里面（草案 [support.coroutine] 一节）。机制有它自己的复杂，咱们也别瞒：`promise_type`、句柄、awaiter 一套零件，编译器在背后做大变换，错误信息可能晦涩。

零件的讲解，咱们交给《[一个最小协程怎样成立](02-minimal-coroutine)》与《`co_await` 怎样参与等待》（还没成篇），本篇只立取舍。

三种模型咱们摆成一排看：

| 模型 | 顺序写在哪里 | 等待的时候 | 主要代价 |
|------|------------|-----------|----------|
| 回调 | 散落在嵌套的回调里 | 不占线程 | 嵌套深、错误处理散、闭包生命周期险 |
| future 链 | 标准 future 串不成链，`get()` 一路睡 | 占着调用线程 | 等待压回线程，共享状态随链多一份 |
| 协程 | 顺序写法，`co_await` 挂起 | 不占线程 | 机制复杂，学习成本摆在前面 |

立场我也给：服务器的场景里我选协程的方向，因为只有它同时做到两件事，顺序读得下去，等待不占线程。回调与 future 链没有死，回调活在性能敏感的最底层，future 适合一次性扔出去的计算活，单元四咱们拿它算过平方与除法这样的独立小值。

## 协程改变的是程序组织，不是线程数量

“可以暂停的工作”落到程序上长什么样，咱们跑一段看。程序做两幕。一幕：一个协程干一件“要等”的活，干到一半挂起，由另一条线程把它恢复。二幕：一千个协程全部挂起，全程只有主线程，量内存。`Task` 类型的零件本篇不当主角，《[一个最小协程怎样成立](02-minimal-coroutine)》逐个讲，您只要认得 `co_await` 的挂起与 `resume()` 的恢复：

```cpp
// exp_b_suspend.cpp —— 协程的暂停与恢复：谁恢复它，它就在谁的线程上跑
// 二幕：一千个挂起的协程，一条线程，量地址空间与物理内存
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::string tid() {
  std::ostringstream oss;
  oss << std::this_thread::get_id();
  return oss.str();
}

// 从 /proc/self/status 抄一行出来，VmSize 与 VmRSS 单位是 kB
static bool status_line(const char* prefix, char* out, size_t n) {
  FILE* f = std::fopen("/proc/self/status", "r");
  if (!f) return false;
  bool found = false;
  char line[256];
  while (std::fgets(line, sizeof line, f)) {
    if (std::strncmp(line, prefix, std::strlen(prefix)) == 0) {
      std::snprintf(out, n, "%s", line);
      found = true;
      break;
    }
  }
  std::fclose(f);
  return found;
}

static void report(const char* tag) {
  char line[256];
  if (status_line("Threads:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmSize:", line, sizeof line))
    std::printf("%s %s", tag, line);
  if (status_line("VmRSS:", line, sizeof line))
    std::printf("%s %s", tag, line);
}

// awaiter：挂起，把句柄登记进列表，等别人来恢复
struct Park {
  static std::vector<std::coroutine_handle<>> parked;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) { parked.push_back(h); }
  void await_resume() {}
};
std::vector<std::coroutine_handle<>> Park::parked;

// 最小任务类型。零件各自干什么归 7.2，本篇只要它撑起 co_await
struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& o) noexcept : h(o.h) { o.h = {}; }
  Task& operator=(Task&& o) noexcept {
    if (this != &o) {
      if (h) h.destroy();
      h = o.h;
      o.h = {};
    }
    return *this;
  }
  ~Task() { if (h) h.destroy(); }
};

// 处理一件"要等"的活：stage 1，挂起等事件，恢复后 stage 2
// done 非空时是安静模式，只计数不打印
Task worker(int id, std::atomic<int>* done) {
  if (done == nullptr)
    std::printf("  worker %d stage1 on thread %s, suspend\n", id, tid().c_str());
  co_await Park{};
  if (done != nullptr)
    ++*done;
  else
    std::printf("  worker %d stage2 on thread %s, finished\n", id, tid().c_str());
}

int main() {
  { // 一幕：同一个协程体分两段跑，谁恢复它，它就在谁的线程上继续
    Task t = worker(0, nullptr);
    std::printf("main: worker suspended, parked=%zu\n", Park::parked.size());
    std::thread driver([] { Park::parked.back().resume(); });
    driver.join();
    std::printf("main: worker finished\n");
  }
  Park::parked.clear();

  { // 二幕：一千个挂起的协程，全程只有主线程
    constexpr int kN = 1000;
    std::atomic<int> done{0};
    std::printf("== before %d workers ==\n", kN);
    report("");
    std::vector<Task> tasks;
    tasks.reserve(kN);
    for (int i = 0; i < kN; ++i)
      tasks.emplace_back(worker(i, &done));
    while (static_cast<int>(Park::parked.size()) < kN)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::printf("== %d coroutines suspended ==\n", kN);
    report("");
    for (auto& h : Park::parked) h.resume();
    std::printf("== resumed, done=%d ==\n", done.load());
  }
  return 0;
}
```

```bash
g++ -std=c++20 -O2 -pthread exp_b_suspend.cpp -o exp_b_suspend
./exp_b_suspend
```

```text
  worker 0 stage1 on thread 130143623620480, suspend
main: worker suspended, parked=1
  worker 0 stage2 on thread 130143615973056, finished
main: worker finished
== before 1000 workers ==
 Threads:	1
 VmSize:	   81040 kB
 VmRSS:	    4236 kB
== 1000 coroutines suspended ==
 Threads:	1
 VmSize:	   81040 kB
 VmRSS:	    4316 kB
== resumed, done=1000 ==
```

一幕的读数，咱们逐行看。stage1 打印的线程标识是主线程的，挂起之后主线程干自己的事去了，恢复它的是 driver 线程，stage2 打印的线程标识跟着换了。同一段函数体，两段执行，中间没有任何 CPU 替它空转。

发起恢复的求值，草案给它起了名字叫 resumer（[dcl.fct.def.coroutine] 第 8 段），协程体没有自己的线程，谁恢复它，它就在谁的线程上继续。cppreference 的协程页给过一个同形状的例子，把句柄递给一条新线程去恢复，跟咱们的一幕是一个思路。咱们头一回并排看两行线程标识的时候愣了一下，同一段函数体，前半截与后半截不住在同一条线程上！

跨线程恢复标准点过名：两边都是 `std::thread`、`std::jthread` 或者主线程时，行为有兜底。换成别的执行代理，行为就是实现定义（[coroutine.handle.resumption] 的说明）。注里还写着一条警告，两条线程并发恢复一条协程可能构成数据竞争，句柄交出去之前，您得确认协程真的停稳了。

挂起期间现场放在哪里？咱们问得直白，答案就是协程状态（coroutine state，也常叫协程帧）。cppreference 给过一份清单：参数的副本在里面，生命周期跨越挂起点的局部变量与临时量也在里面，执行停在哪个位置也记着，出处是 [dcl.fct.def.coroutine] 第 9 段与第 13 段。它多半靠一次动态分配安家。豁免也有：实现要是能证明协程的生命周期不逃出调用者，分配就能省下来（cppreference，Dynamic allocation 一节）。普通函数做不到：一返回，栈帧就没了，想停只能把整条线程停住。单元三的等待压在线程上，根子就在这里。

二幕是对照的另一边。一千个协程全部挂起，线程数从头到尾是 1，物理内存涨了 80 kB，折到每个协程帧 80 B 上下（GCC 该实现的读数，帧大小随局部变量与实现变），VmSize 没动。咱们把它跟 exp_a 摆在一起看：一千条线程，约 8 GB 的地址空间预订，1001 条调度对象。一千个挂起的协程，零条新增线程，80 kB 的物理页。您想想，单看一帧 80 B，一条线程的栈预订却是 8 MiB，差着五个数量级！单元名的半句在读数里落了地：等待不再按线程计价。

便宜有边界，咱们认下。协程省的是等待占住的线程，计算还是要真线程干，事件到达时得有几条线程把挂起的协程挨个恢复起来。谁来恢复、什么时候恢复，标准不管，语言机制给到协程状态与句柄为止，调度归您自己的程序组织，事件循环或者线程池都能当恢复的执行者。事件循环与 echo server 不进正文，选读的正式入口还在迁移路上。

> 旧目录里有现成的两篇，想提前看的话您移步过去就是。头一篇，[《异步 I/O 与事件循环》][ch06-io]，讲事件循环的骨架。后一篇，[《协程 echo server》][ch06-echo]，讲的是拿协程写回声服务器。

跟 3.3 的对照，咱们也在这里收个尾：挂起之后没人恢复、没人销毁的协程，跟睡死在 `wait` 上没人叫的线程是亲戚。等待需要结束路径的问题，协程一样要答，功课在《[等待也需要结束路径](../chapter03-wait-and-coordinate/03-ending-a-wait)》里做过一遍。落点在《[一个最小协程怎样成立](02-minimal-coroutine)》与《把协程组织成可完成的任务》，后者还没成篇。

## 轮到您动手的时候

1. 把两个实验在您的机器上各跑一遍，VmSize 与 VmRSS 的涨幅分开记，注意 `ulimit -s` 的值，栈的默认大小随实现变。
2. 把 exp_b 的 `kN` 提到十万再跑，看看物理内存的涨幅折到每个协程是多少字节，跟一千个时的读数对一对。
3. 把一幕里恢复协程的 driver 换成主线程自己，stage1 与 stage2 的线程标识还一样吗，为什么。
4. 拿协程的眼睛把 3.1 的等待重看一遍：线程睡在 `wait` 里的时间，挂起的协程把相应的时间还给了谁，还回来的线程又能干什么。

## 下一步

本篇立了需求，还没有给零件。下一篇《[一个最小协程怎样成立](02-minimal-coroutine)》接手机制：编译器把函数变成状态机，协程状态里装着什么，`promise_type` 与句柄怎么把两边接起来，收尾拿一个 Generator 把全链走通，咱们从 6.3 结尾预告过的“协程状态机与 `co_await`”那里接上。

## 参考资源

场景与三种模型的取舍，咱们压缩自旧目录的《[异步编程演进：从回调地狱到协程](../ch06-async-io-coroutine/01-async-programming-evolution)》，例子全部重写，文字是新的。协程语言机制的断言，我按下列出处核对过（2026-11-17 访问）。线程栈默认值、地址空间与物理内存的读数以本机实测为准，字段的出处见 proc_pid_status(5) 手册，proc(5) 后来分了册，status 字段住进了该分册。

- [协程（C++20），cppreference][cppref-coroutines]，三关键字判定、协程状态的组成、动态分配与省略、跨线程恢复例子的出处。
- [std::future，cppreference][cppref-future]，成员函数清单，`then` 不在标准 `std::future` 里的出处。
- [std::experimental::future::then，cppreference][cppref-then]，Concurrency TS 续接的出处，该 TS 未并入国际标准。
- [现行草案：Coroutine definitions][draft-coroutine]，[dcl.fct.def.coroutine]（9.6.4），协程判定在第 1 段，resumer 在第 8 段，协程状态在第 9 段，参数副本在第 13 段。现行草案里语言侧协程条款住在该名下，库侧支持接口住在 [support.coroutine]（17.13），旧称呼 [cpp.coroutine] 在现行草案里已经不是条款名。
- [现行草案：协程支持接口][draft-support]，[support.coroutine]（17.13），`suspend_always` 与 `suspend_never` 的住处。
- [现行草案：句柄恢复][draft-handle-resumption]，[coroutine.handle.resumption]（17.13.4.6），跨线程恢复的执行代理条件与并发恢复警告的出处。
- [proc_pid_status(5) 手册][man-proc]，`/proc/self/status` 里 VmSize、VmRSS、Threads 字段的出处（proc(5) 分册后的新家）。

[cppref-coroutines]: https://en.cppreference.com/w/cpp/language/coroutines
[cppref-future]: https://en.cppreference.com/w/cpp/thread/future
[cppref-then]: https://en.cppreference.com/w/cpp/experimental/future/then
[draft-coroutine]: https://eel.is/c++draft/dcl.fct.def.coroutine
[draft-support]: https://eel.is/c++draft/support.coroutine
[draft-handle-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[man-proc]: https://man7.org/linux/man-pages/man5/proc_pid_status.5.html
[ch06-io]: ../ch06-async-io-coroutine/04-async-io-and-event-loop
[ch06-echo]: ../ch06-async-io-coroutine/05-coroutine-echo-server
