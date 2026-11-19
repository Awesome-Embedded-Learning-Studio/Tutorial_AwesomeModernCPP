---
title: "把协程组织成可完成的任务"
description: "教学版 Task 怎样把结果、异常、销毁备齐，最小调度器怎样让几段协程排队跑起来，恢复发生在哪条线程由谁说了算，第七单元在收尾交代。"
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 把协程组织成可完成的任务

上一篇《[`co_await` 怎样参与等待](03-co-await-and-awaiters)》结尾留过话：三件套与交接姿势都齐了，可手里的 Gate 还欠着东西，取不到结果，传不了异常，销毁要自己惦记。本篇兑现留话，咱们把三件套与 sym 姿势包成一个能用的 Task 类型，能接结果，能传异常，能安全销毁，7.3 定下的交接姿势原样落进代码。把几段协程串起来的最小调度示例也落在本篇。

单元七也在本篇收官。7.1《[为什么需要暂停与恢复](01-why-suspend-resume)》立了为什么，7.2《[一个最小协程怎样成立](02-minimal-coroutine)》把函数变成协程，7.3 交出等待的协议，本篇把三篇的零件装配成一件能跑的工具。装配完咱们还回头把单元一埋下的老问题接住：工作不占线程了，恢复发生在哪条线程上，由谁说了算。

素材从旧目录 ch06 的《[promise_type 与 awaitable](../ch06-async-io-coroutine/03-promise-type-and-awaitable)》来，SimpleTask、SafeTask 与调度器的雏形在旧文里已有，本篇的 Task 与调度示例全部重写，逐行补齐理由。三个实验我都放进了临时目录 /tmp/concurrency-u7/ 里编译运行（WSL2，16 逻辑核，GCC 16.2.1 20260810，`g++ -std=c++20 -O2 -pthread`，2026-11-17），三份都另拿 `-fsanitize=address,undefined` 复跑过一遍，没有报告。exp_i 与 exp_j 复用本篇第一节的 Task，拼接位置写在文件头注释里，编译命令里的文件名与各块头注释一致。语言机制的断言我按 cppreference 与现行工作草案核对过（2026-11-17 访问），出处收在参考资源里。

## 教学版 Task：结果、异常、销毁

7.1 的实验里出现过一版最小 Task，急启动，创建即跑（见《[为什么需要暂停与恢复](01-why-suspend-resume)》的 exp_b）。它当独角戏没问题，本篇要让它能被别的协程 co_await，急启动马上露出毛病。您跟着时序走一遍：调用 `pipeline()`（下面实验里无事的流水线协程）的当口，协程体立刻开跑，外面的 awaiter 要到 `await_suspend` 才有机会把等待者的句柄登记进 promise，两步之间隔着一次函数返回。子协程要是抢在登记之前跑完收工，continuation 登记得再对也没有人来恢复等待者，一场竞态就此埋下。

解法是换懒启动，咱们把登记与启动的顺序反过来安放：`initial_suspend` 返回 `suspend_always`，协程创建时一行不跑，乖乖挂在门口。等到 `await_suspend` 把等待者登记进 continuation，再把子协程的句柄交出去，让它开跑。登记必定赶在运行前面，竞态就没有落脚的地方。懒启动与急启动的分岔口 7.2 讲过，本篇的选择到这里有了完整的理由：能被等待的 Task，懒启动是咱们能选的安全写法。

下面是 Task 的全貌，exp_h_task.cpp 的骨架部分，逐行讲在后面，您也可以停下来通读一遍：

```cpp
// exp_h_task.cpp —— 教学版 Task<int>：结果怎么回、异常怎么传、协程帧怎么收回
#include <coroutine>
#include <cstdio>
#include <exception>
#include <new>
#include <stdexcept>
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
```

promise 身上三个成员，result、failure、continuation，各扛一路交接。`co_return 40;` 执行时编译器调 `return_value`，值搬进 result，跟着协程帧同生共死。等待者恢复后从 `await_resume` 进来，`take_result` 把值从 promise 搬出去，结果的全程就这两步，存放点与 7.2 的 Generator 一模一样。`co_return` 的落点 7.2 已经交代过，本篇补一条纪律：C++20 里 `return_value` 与 `return_void` 互斥，教学版按带值的一路走，示例协程的每条路径都写了 `co_return`。函数体走到底没写的后果 7.2 量过，未定义行为，编译器一声不吭，咱们在纪律里躲开它。

异常的一路交给 failure。协程体抛出异常没人接住时，标准把异常接住，调 `unhandled_exception`，随后照常走到最终挂起点，出处是 cppreference 的 Completion 一节。教学版选的是存储派，两派的名字 7.2 起过：`failure = std::current_exception()`，把异常指针存进 promise，取结果的人用 `rethrow_exception` 原样重抛。`std::exception_ptr` 是共享所有权的指针，cppreference 明说它可以递给别的函数、别的线程，接力棒由此能沿任务链一路传下去。7.3 的实验用的是 terminate 派，本篇换成存储派。您在 7.2 见过两派的分别，terminate 派当场掀桌，存储派把异常存起来等外部来取。Task 的立场是失败也属于结果，要送到取结果的人手里。

接力的走法值得您慢慢看。叶子协程 boom 抛出异常，它的 promise 存下指针，最终挂起点照常交接。等待它的 relay 从 `co_await` 醒来，`await_resume` 里的 `take_result` 把异常重新抛进 relay 的函数体，relay 没写 try，异常继续向上，relay 自己的 promise 又存下一枚指针。一级一级传上去，每一级的 promise 都是接力站，谁都不接，异常就停在根 Task 的 promise 里，等 main 取结果时再浮出来。同型的传递您在单元四见过，异常经共享状态走到 `get()`（见《[用 `std::async` 执行计算](../chapter04-organizing-tasks/01-launch-with-async)》），这里只是存放点从共享状态换成了 promise。

交接的两处都按 7.3 定下的 sym 姿势写，咱们一处一处对。第一处在 Task 的 `await_suspend`：登记完 continuation，返回子协程的句柄，草案 [expr.await]/(5.1.1) 说得明白，返回的句柄会被 resume，一串协程可以一路接力下去，最终把控制流还给最初的恢复者。第二处在 `final_suspend` 返回的 Done，它是 7.3 链式实验的老朋友：有人等就把控制权交回去，没人是根，就交 `std::noop_coroutine()`，noop 句柄的 resume 是空操作（[coroutine.handle.noop]），等于把控制流还给最初的恢复者。深链之下栈深与链深解耦的理由，7.3 用一百万层的读数给您讲过了，本篇不再重跑。

销毁的一路由 RAII 扛。协程帧从堆上来，`coroutine_handle` 是非拥有句柄，析构时它什么都不做，泄漏的责任句柄不背，7.2 讲得很清楚，所以 Task 的析构函数调 `destroy`，把收帧从您肩上挪到类型身上。拷贝整个删掉，两份句柄指向同一帧、析构时对同一块内存收两次的险情 7.2 提过，删拷贝是便宜的保险。移动保留，工厂函数返回 Task 时用得上。

真正值得打起精神的位置在 co_await 的临时量上。`co_await middle(20)` 里，middle（链上的中转）返回的 Task 是个临时对象，生命周期要跨挂起点，标准安排它搬进父协程的帧里住（7.3 讲 awaiter 住处时提过同款安排）。等 co_await 完整表达式结束，临时 Task 析构，destroy 落在子协程身上。此刻子协程站在最终挂起点，cppreference 有一句话正好压住场面，挂在最终挂起点的协程只能被销毁。销毁的前提是句柄指向挂起的协程（[coroutine.handle.resumption]），子协程恰好满足，咱们把临时量的析构就这样一次次落在安全的位置上。

> 我推演时序时拿 ASan 来回跑了几遍，才敢把临时 Task 的析构写进正文，三份实验的读数都干净。工具没说话不等于证明无错（单元六交代过 TSan 的能力边界），它只是替咱们把常见的悬垂位置过了一遍。

根 Task 的销毁最省心，它活到 main 的作用域结束，咱们看着析构函数收走最后一块帧。跑完的、没跑的、抛过异常的，收场都是同一个入口。

### 三条路径实测

道理讲完了，咱们让三条路径都跑出输出。实验协程是两条链。pipeline 串起 leaf 与 middle，是无事的链。top 经 relay 到 boom 的链上，三级都不接异常：

```cpp
Task<int> leaf(int x) {
  co_return x * 2;
}

Task<int> middle(int x) {
  int v = co_await leaf(x);
  co_return v + 1;
}

Task<int> pipeline() {
  int v = co_await middle(20);
  co_return v;
}

Task<int> boom(int x) {
  if (x == 13) throw std::runtime_error("叶子协程抛出的运行时错误");
  co_return x;
}

Task<int> relay(int x) {
  int v = co_await boom(x);   // boom 的异常在 co_await 这里重新浮出，relay 没接，存进自己的 promise
  co_return v;
}

Task<int> top(int x) {
  int v = co_await relay(x);  // relay 的异常在 co_await 这里重新浮出，top 也没接
  co_return v;
}

int main() {
  { // 路径一：结果沿链传回
    Task<int> t = pipeline();
    t.start();
    std::printf("路径一 结果: %d\n", t.result());
  }
  { // 路径二：异常逐级接力，在取结果的地方重新抛出
    Task<int> t = top(13);
    t.start();   // start 正常返回，异常不当场冒出来
    try {
      (void)t.result();
    } catch (const std::exception& e) {
      std::printf("路径二 异常: %s\n", e.what());
    }
  }
  { // 路径三：跑完的帧与没跑的帧，都由析构收回
    Task<int> run = pipeline();
    run.start();
    (void)run.result();
    Task<int> never = pipeline();   // 创建了，没 start，析构直接收帧
    std::printf("路径三 两个 Task 还在册的帧数: %d\n", g_frames);
  }
  std::printf("路径三 全部析构后在册帧数: %d\n", g_frames);
  return 0;
}
```

```bash
g++ -std=c++20 -O2 -pthread exp_h_task.cpp -o exp_h_task
./exp_h_task
```

```text
路径一 结果: 41
路径二 异常: 叶子协程抛出的运行时错误
路径三 两个 Task 还在册的帧数: 2
路径三 全部析构后在册帧数: 0
```

三条路径咱们逐条对读数。路径一，41 等于 20 乘 2 加 1，结果从叶子的 `co_return` 出发，经 middle 的 result、pipeline 的 result 两站接力，最后被 main 的 `t.result()` 取走。链上三个协程帧随后全部收回，leaf 与 middle 的帧跟着 co_await 处的临时 Task 析构走，pipeline 的帧跟着 t 的析构走。路径二，异常并没有从 start 冒出来，start 正常返回，叶子抛出的错误安安静静躺在三级 promise 里接力，直到 `t.result()` 才重新浮出，异常在取结果的地方接住，这是 Task 语义的一部分。路径三，两个 Task 在册时帧数是 2，run 跑完了挂在终点站，never 创建之后一行没跑，挂在门口。作用域一结束，两块帧都被析构收回，计数归零，这是手头 GCC 的读数，实现若省掉堆分配，计数可能是 0。跑完的与没跑的，收场走同一个 destroy。

### 教学版与生产库差在哪

跑通了，咱们还要把话说全，说清它是什么，也说清它不是什么。教学要的三件事，Task 备齐了，下面列出的位置是故意留空的，生产库的复杂度全长在这里：

- 状态检查一道没设。没 start 就取结果，拿到的是默认值。结果取第二次，拿到的是搬空后的残值，对 int 连值都不变。start 调两次，是对完工协程 resume 的未定义行为。三种教学版都拦不住，生产库要在 promise 里记一套状态机，逐项拦截。
- 跨线程不加同步。continuation 的登记与读取、result 的写入与读取都是普通变量，本篇的实验靠单线程或者 join 的时序兜着。真要在两条线程之间交接，得按单元五的内存序给状态配原子操作（见《[一个线程怎样观察另一个线程的修改](../chapter05-atomic-memory-model/02-happens-before)》），至少一道原子状态加 happens-before。
- 取消没有通路。单元一、单元三用过的停止协议（stop_token 一类）与 Task 之间没有任何连接，任务做了一半要停，教学版给不出办法。
- `Task<void>` 缺席。C++20 里 `return_value` 与 `return_void` 互斥（7.2 交代过，C++29 才放开），支持无值任务得单独写一个 promise，教学版只留了带值的一型。
- `result{}` 的初始化要求 T 能默认构造，move-only 又没有默认构造的类型进不来，生产库会换带状态的存储绕开。
- 分配没有优化。每个协程帧一次 `operator new`，没有池化，也没有接 `get_return_object_on_allocation_failure`（7.2 讲过它的用法）。
- `await_ready` 恒为 false，没走快路径，结果已经就绪的任务也要付一次挂起与恢复的开销。

旧目录 ch06 的《promise_type 与 awaitable》给过 SimpleTask 与 SafeTask 的雏形，Lewis Baker 的 cppcoro 一类库是本篇教学的远房终点，取消、同步、调试支持、内存策略层层叠加，骨架还是本篇写的底子。您把骨架记牢，往后读库时认得每一层在补什么。

## 最小调度示例

旧文的调度器雏形是就绪队列加 run 循环，住在旧目录的《[promise_type 与 awaitable](../ch06-async-io-coroutine/03-promise-type-and-awaitable)》里。咱们把它重写并接上 Task。分工摆出来就三句话。Task 管一段工作的一生，从创建到销毁。Loop 管排队，决定下一个跑谁。Yield 管让出，协程自己把位置让出来排到队尾。把工作的一生与执行的位置分开，是调度器能成立的前提，也是单元七想交给您的组织方式。

咱们让两段流水线排队跑，pipeline 串起 fetch 与 process 两站，每站干完一段活就让出一轮。Task 沿用本篇第一节的代码，Loop、Yield 与三个协程是新增的：

```cpp
// exp_i_loop.cpp —— 最小调度器：就绪队列加单线程循环，两段流水线排队跑
// Task 与 exp_h_task.cpp 相同，拼接在本文件顶部即可编译
#include <coroutine>
#include <cstdio>
#include <deque>
#include <exception>
#include <string>
#include <utility>

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

咱们把它跟 1.2 的内核调度摆在一起比（见《[线程在机器上是什么](../chapter01-start-concurrency/02-what-is-a-thread)》），内核的时间用完了就换人，不管程序愿不愿意。Loop 是纯协作式，协程不让出就独占到死，写调度器的人与写协程的人得共同守住让出的纪律。跟旧文的调度器比，Task 把手动 destroy 的活接了过去，调度器只管排队，谁也不惦记销毁。真要把等待做成异步，登记的去向 7.3 的 Gate 一节交代过，定时器与事件循环不进正文，旧目录里有完整的两篇，动手清单里给了入口。

## 协程与线程：谁在哪个线程上恢复

单元一的老问题，咱们在本篇收。7.2 引过标准的命名，调用恢复成员函数的求值动作叫 resumer（[dcl.fct.def.coroutine]/8），协程体不自带线程，谁调 resume，它就接着跑在谁的调用栈上。7.1 的一幕量过同一段函数体换线程的读数，咱们把立场落到 Task 上：Task 自己不带线程，start 谁调、Loop 谁跑，整条链就在谁的栈上活着。链上的交接全按 sym 姿势写，交接不换线程，它只是把控制权递给下一级，脚下还是同一条线程。

三幕实验把立场量给您看。幕一 main 自己 start，幕二 start 交给一条 driver 线程，幕三任务在 main 创建、循环却搬上另一条 driver 跑：

```cpp
// exp_j_threads.cpp —— 谁在哪个线程上恢复：谁调 resume，协程体就在谁的线程上接着跑
// Task 与 exp_h_task.cpp 相同（handle() 访问器用上了），Loop 与 exp_i_loop.cpp 相同
#include <coroutine>
#include <cstdio>
#include <deque>
#include <exception>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

class Loop {
public:
  void schedule(std::coroutine_handle<> h) { ready_.push_back(h); }
  void run() {
    while (!ready_.empty()) {
      auto h = ready_.front();
      ready_.pop_front();
      h.resume();
    }
  }

private:
  std::deque<std::coroutine_handle<>> ready_;
};

static std::string tid() {
  std::ostringstream oss;
  oss << std::this_thread::get_id();
  return oss.str();
}

Task<int> leaf(int x) {
  std::printf("  leaf(%d)       在线程 %s\n", x, tid().c_str());
  co_return x + 1;
}

Task<int> root(int x) {
  std::printf("  root 前半      在线程 %s\n", tid().c_str());
  int v = co_await leaf(x);
  std::printf("  root 后半 v=%d 在线程 %s\n", v, tid().c_str());
  co_return v;
}

int main() {
  std::printf("main 在线程 %s\n", tid().c_str());
  { // 幕一：main 自己启动
    Task<int> t = root(10);
    t.start();
    std::printf("幕一 result=%d\n", t.result());
  }
  { // 幕二：start 交给 driver 线程
    Task<int> t = root(20);
    std::thread driver([&t] { t.start(); });
    driver.join();
    std::printf("幕二 result=%d\n", t.result());
  }
  { // 幕三：任务在 main 创建，恢复发生在跑循环的 driver 线程上
    Loop loop;
    Task<int> t = root(30);
    loop.schedule(t.handle());
    std::thread driver([&loop] { loop.run(); });
    driver.join();
    std::printf("幕三 result=%d\n", t.result());
  }
  return 0;
}
```

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

跨线程使用有一条安全边界要交代。幕二里 main 把 Task 交给 driver，靠 thread 的构造与 join 建立 happens-before，main 在 join 之后读 result 才有保障。这对同步关系单元五 5.2 讲过（见《[一个线程怎样观察另一个线程的修改](../chapter05-atomic-memory-model/02-happens-before)》）。教学版自己不加锁，两条线程并发 resume 同一条协程是数据竞争，7.1 引过 [coroutine.handle.resumption] 的警告。要用 Task 在线程之间交接，同步得您自己补，位置在教学版差距清单里挂着。

单元一给世界定的秩序是工作长在线程身上，一条线程一段栈，等待把线程占住，一千个等待要一千条线程预订约 8 GB 地址空间（7.1 的读数）。单元七把两件事分了家：工作是 Task，Task 是所有物，它有帧、有结果、有异常、有析构。线程是恢复的执行者，一次 resume 服务一段继续，服务完就去干别的。1.3 的 join 与 detach 管线程的命（见《[第一个 `std::thread` 程序](../chapter01-start-concurrency/03-first-std-thread)》），本篇的析构管工作的帧，收场都落在析构函数上，管的对象换了。单元七还配了一个小作品，方向是解释协程、线程与调度的关系，素材咱们在本篇备齐了，练习单独设计。

## 轮到您动手的时候

1. 给教学版 Task 补状态检查：没 start 就取结果、结果取两次、start 调两次，各加一道拦截。设计时想想 promise 里最少要记哪几个状态，检查放在 start、result、await_resume 的哪一处。
2. 把 exp_i 的就绪队列换成两条线程共享的队列（加锁），两边的 run 同时往外弹协程，观察打印交错的变化，想想 continuation 的登记与完工的交接各自需要什么同步。
3. 写一个 `Task<void>`：promise 里去掉 result，把 `return_value` 换成 `return_void`（7.2 讲过 C++20 里两者互斥），把 exp_h 的路径一改成无值任务跑通。
4. 给 Loop 补一个 `run_one`，一次只弹一个就绪协程，配合定时器的思路想想事件循环还缺什么。完整的答案旧目录里有现成的两篇，[《异步 I/O 与事件循环》][ch06-io]与[《协程 echo server》][ch06-echo]。

## 下一步

单元七到这里四篇齐了：为什么、机制、协议、工具，协程从三个关键字走到能组织任务的 Task。下一站是单元八的结课作品，一个小型任务处理程序：任务怎么分、数据归谁、怎么协作，设计归您自己拿主意，验证与设计说明也由您提交，另留一个协程综合扩展的选做。题目与行为要求（8.1 到 8.4）还没写出来，到时候咱们在那里见。

## 参考资源

本篇的素材来自旧目录 ch06 的《[promise_type 与 awaitable](../ch06-async-io-coroutine/03-promise-type-and-awaitable)》，SimpleTask、SafeTask 与调度器在旧文里已有雏形，例子与文字全部重写。三个实验都在开头交代的临时目录里编译运行，另拿 `-fsanitize=address,undefined` 各复跑一遍，没有报告。exp_i 与 exp_j 复用本篇第一节的 Task，拼接位置写在文件头注释里，正文代码块加上本篇第一节的 Task 就是完整可编译的文件。推进思路参照 Lewis Baker 的协程系列，例子与文字都是我自己写的。语言机制的断言按 eel.is 上的现行工作草案与 cppreference 核对（2026-11-17 访问），条款号与段号可能随草案更新挪动。

- [Await expression（现行草案 expr.await）][draft-await]，await_suspend 的三种返回类型、句柄返回值的接力恢复注。
- [Coroutine definitions（现行草案 dcl.fct.def.coroutine）][draft-def]，resumer 的命名、final_suspend 的 noexcept 要求、协程状态的销毁次序。
- [Resumption（现行草案 coroutine.handle.resumption）][draft-resumption]，resume 与 destroy 的前置条件、跨执行代理恢复的实现定义行为、并发恢复的警告。
- [No-op coroutines（现行草案 coroutine.handle.noop）][draft-noop]，noop 句柄的 resume 与 destroy 都是空操作。
- [Coroutines (C++20)（cppreference）][cppref-coro]，完成的时序（未接住异常、unhandled_exception、final_suspend）、挂在最终挂起点的协程只能被销毁、协程状态的组成。
- [std::exception_ptr（cppreference）][cppref-exception-ptr]，异常指针的共享所有权与跨线程传递。
- [C++ Coroutines: Understanding Symmetric Transfer — Lewis Baker][baker-sym]，7.3 用过的 naive 与 sym 两种交接姿势的讲法，本篇 Task 的交接姿势参照。

[cppref-coro]: https://en.cppreference.com/w/cpp/language/coroutines
[cppref-exception-ptr]: https://en.cppreference.com/w/cpp/error/exception_ptr
[draft-await]: https://eel.is/c++draft/expr.await
[draft-def]: https://eel.is/c++draft/dcl.fct.def.coroutine
[draft-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[draft-noop]: https://eel.is/c++draft/coroutine.handle.noop
[baker-sym]: https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer
[ch06-io]: ../ch06-async-io-coroutine/04-async-io-and-event-loop
[ch06-echo]: ../ch06-async-io-coroutine/05-coroutine-echo-server
