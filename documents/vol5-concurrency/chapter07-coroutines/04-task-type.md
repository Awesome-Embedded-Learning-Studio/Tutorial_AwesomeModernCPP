---
title: "把结果包起来：教学版 Task"
description: "结果、异常、销毁怎么收进一个 Task 类型：懒启动把竞态挡在门口，异常沿 promise 一级级接力，析构把帧全数收回，三条路径实测对读数，篇末一道亲手题给下一篇的调度器备料。"
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

# 把结果包起来：教学版 Task

咱们把三件套与 sym 姿势包成一个能用的 Task 类型，能接结果，能传异常，能安全销毁。

## 教学版 Task：结果、异常、销毁

第一篇的实验里出现过一版最小 Task，急启动，创建即跑（见《[为什么需要暂停与恢复](01-why-suspend-resume)》的 exp_b）。它当独角戏没问题，本篇要让它能被别的协程 co_await，急启动马上露出毛病。您跟着时序走一遍：调用 `pipeline()`（下面实验里无事的流水线协程）的当口，协程体立刻开跑，外面的 awaiter 要到 `await_suspend` 才有机会把等待者的句柄登记进 promise，两步之间隔着一次函数返回。子协程要是抢在登记之前跑完收工，continuation 登记得再对也没有人来恢复等待者，一场竞态就此埋下。

解法是换懒启动，咱们把登记与启动的顺序反过来安放：`initial_suspend` 返回 `suspend_always`，协程创建时一行不跑，乖乖挂在门口。等到 `await_suspend` 把等待者登记进 continuation，再把子协程的句柄交出去，让它开跑。登记必定赶在运行前面，竞态就没有落脚的地方。懒启动与急启动的分岔口第二篇讲过，本篇的选择到这里有了完整的理由：能被等待的 Task，懒启动是咱们能选的安全写法。

下面是 Task 的全貌，exp_h_task.cpp 的骨架部分，逐行讲在后面，您也可以停下来通读一遍：

<OnlineCompilerDemo
  title="教学版 Task 的骨架：结果、异常、销毁三路交接"
  source-path="code/examples/vol5/chapter07/11_task_skeleton.cpp"
  description="骨架配了最小演示 main，trivial 协程 co_return 42，start 启动、result 取回，帧计数显示析构前在册 1 帧。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_h_task.cpp 的骨架 —— 教学版 Task<int>：结果怎么回、异常怎么传、协程帧怎么收回
// 配了最小演示 main 让骨架独立可跑，三条路径的完整实验在 exp_h_task.cpp
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

// 最小演示：带值协程跑一轮，结果回传，析构收帧
Task<int> trivial() {
  co_return 42;
}

int main() {
  Task<int> t = trivial();
  t.start();
  std::printf("结果: %d\n", t.result());
  std::printf("析构前在册帧数: %d\n", g_frames);
  return 0;
}
```

</OnlineCompilerDemo>

promise 身上三个成员，result、failure、continuation，各扛一路交接。`co_return 40;` 执行时编译器调 `return_value`，值搬进 result，跟着协程帧同生共死。等待者恢复后从 `await_resume` 进来，`take_result` 把值从 promise 搬出去，结果的全程就这两步，存放点与第二篇的 Generator 一模一样。`co_return` 的落点那里已经交代过，本篇补一条纪律：C++20 里 `return_value` 与 `return_void` 互斥，教学版按带值的一路走，示例协程的每条路径都写了 `co_return`。函数体走到底没写的后果在第二篇量过，未定义行为，编译器一声不吭，咱们在纪律里躲开它。

异常的一路交给 failure。协程体抛出异常没人接住时，标准把异常接住，调 `unhandled_exception`，随后照常走到最终挂起点，出处是 cppreference 的 Completion 一节。教学版选的是存储派，两派的名字第二篇起过：`failure = std::current_exception()`，把异常指针存进 promise，取结果的人用 `rethrow_exception` 原样重抛。`std::exception_ptr` 是共享所有权的指针，cppreference 明说它可以递给别的函数、别的线程，接力棒由此能沿任务链一路传下去。上一篇的实验用的是 terminate 派，本篇换成存储派。您在第二篇见过两派的分别，terminate 派当场掀桌，存储派把异常存起来等外部来取。Task 的立场是失败也属于结果，要送到取结果的人手里。

接力的走法值得您慢慢看。叶子协程 boom 抛出异常，它的 promise 存下指针，最终挂起点照常交接。等待它的 relay 从 `co_await` 醒来，`await_resume` 里的 `take_result` 把异常重新抛进 relay 的函数体，relay 没写 try，异常继续向上，relay 自己的 promise 又存下一枚指针。一级一级传上去，每一级的 promise 都是接力站，谁都不接，异常就停在根 Task 的 promise 里，等 main 取结果时再浮出来。同型的传递您在单元四见过，异常经共享状态走到 `get()`（见《[用 `std::async` 执行计算](../chapter04-organizing-tasks/01-launch-with-async)》），这里只是存放点从共享状态换成了 promise。

交接的两处都按上一篇定下的 sym 姿势写，咱们一处一处对。第一处在 Task 的 `await_suspend`：登记完 continuation，返回子协程的句柄，草案 [expr.await]/(5.1.1) 说得明白，返回的句柄会被 resume，一串协程可以一路接力下去，最终把控制权还给最初的恢复者。第二处在 `final_suspend` 返回的 Done，它是上一篇链式实验的老朋友：有人等就把控制权交回去，没人是根，就交 `std::noop_coroutine()`，noop 句柄的 resume 是空操作（[coroutine.handle.noop]），等于把控制流还给最初的恢复者。深链之下栈深与链深解耦的理由，那里用一百万层的读数给您讲过了，本篇不再重跑。

销毁的一路由 RAII 扛。协程帧从堆上来，`coroutine_handle` 是非拥有句柄，析构时它什么都不做，泄漏的责任句柄不背，第二篇讲得很清楚，所以 Task 的析构函数调 `destroy`，把收帧从您肩上挪到类型身上。拷贝整个删掉，两份句柄指向同一帧、析构时对同一块内存收两次的险情也在第二篇提过，删拷贝是便宜的保险。移动保留，工厂函数返回 Task 时用得上。

真正值得打起精神的位置在 co_await 的临时量上。`co_await middle(20)` 里，middle（链上的中转）返回的 Task 是个临时对象，生命周期要跨挂起点，标准安排它搬进父协程的帧里住（上一篇讲 awaiter 住处时提过同款安排）。等 co_await 完整表达式结束，临时 Task 析构，destroy 落在子协程身上。此刻子协程站在最终挂起点，cppreference 有一句话正好压住场面：挂在最终挂起点的协程只能被销毁。销毁的前提是句柄指向挂起的协程（[coroutine.handle.resumption]），子协程恰好满足，咱们把临时量的析构就这样一次次落在安全的位置上。

> 笔者推演时序时拿 ASan 来回跑了几遍，才敢把临时 Task 的析构写进正文，两份实验的读数都干净。工具没说话不等于证明无错（单元六交代过 TSan 的能力边界），它只是替咱们把常见的悬垂位置过了一遍。

根 Task 的销毁最省心，它活到 main 的作用域结束，咱们看着析构函数收走最后一块帧。跑完的、没跑的、抛过异常的，收场都是同一个入口。

### 三条路径实测

道理讲完了，咱们让三条路径都跑出输出。实验协程是两条链。pipeline 串起 leaf 与 middle，是无事的链。top 经 relay 到 boom 的链上，三级都不接异常：

<OnlineCompilerDemo
  title="三条路径实测：结果回传、异常接力、帧全数收回"
  source-path="code/examples/vol5/chapter07/12_exp_h_task.cpp"
  description="路径一 41 沿链传回，路径二异常存在三级 promise 里接力、取结果时才浮出，路径三跑完的与没跑的帧都由析构收回，计数归零。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

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

// 以下为三条路径实测的实验协程与 main
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

</OnlineCompilerDemo>

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

三条路径咱们逐条对读数。路径一，41 等于 20 乘 2 加 1，结果从叶子的 `co_return` 出发，经 middle 的 result、pipeline 的 result 两站接力，最后被 main 的 `t.result()` 取走。链上三个协程帧随后全部收回，leaf 与 middle 的帧跟着 co_await 处的临时 Task 析构走，pipeline 的帧跟着 t 的析构走。路径二，异常并没有从 start 冒出来，start 正常返回，叶子抛出的错误安安静静躺在三级 promise 里接力，直到 `t.result()` 才重新浮出，异常在取结果的地方接住，这是 Task 语义的一部分。路径三，两个 Task 在册时帧数是 2，run 跑完了挂在终点站，never 创建之后一行没跑，挂在门口。作用域一结束，两块帧都被析构收回，计数归零，这是手头 GCC 的读数，实现若省掉堆分配，计数可能是 0。跑完的与没跑的，收场走同一个 destroy。三个实验笔者都另拿 `-fsanitize=address,undefined` 复跑过一遍，没有报告。

### 教学版与生产库差在哪

跑通了，咱们还要把话说全，说清它是什么，也说清它不是什么。教学要的三件事，Task 备齐了，下面列出的位置是故意留空的，生产库的复杂度全长在这里：

- 状态检查一道没设。没 start 就取结果，拿到的是默认值。结果取第二次，拿到的是搬空后的残值，对 int 连值都不变。start 调两次，是对完工协程 resume 的未定义行为。三种教学版都拦不住，生产库要在 promise 里记一套状态机，逐项拦截。
- 跨线程不加同步。continuation 的登记与读取、result 的写入与读取都是普通变量，本篇的实验靠单线程或者 join 的时序兜着。真要在两条线程之间交接，得按单元五的内存序给状态配原子操作（见《[一个线程怎样观察另一个线程的修改](../chapter05-atomic-memory-model/03-happens-before)》），至少一道原子状态加 happens-before。
- 取消没有通路。单元一、单元三用过的停止协议（stop_token 一类）与 Task 之间没有任何连接，任务做了一半要停，教学版给不出办法。
- `Task<void>` 缺席。C++20 里 `return_value` 与 `return_void` 互斥（第二篇交代过，C++29 才放开），支持无值任务得单独写一个 promise，教学版只留了带值的一型。
- `result{}` 的初始化要求 T 能默认构造，move-only 又没有默认构造的类型进不来，生产库会换带状态的存储绕开。
- 分配没有优化。每个协程帧一次 `operator new`，没有池化，也没有接 `get_return_object_on_allocation_failure`（第二篇讲过它的用法）。
- `await_ready` 恒为 false，没走快路径，结果已经就绪的任务也要付一次挂起与恢复的开销。

旧目录 ch06 的《promise_type 与 awaitable》给过 SimpleTask 与 SafeTask 的雏形，Lewis Baker 的 cppcoro 一类库是本篇教学的远房终点，取消、同步、调试支持、内存策略层层叠加，骨架还是本篇写的底子。您把骨架记牢，往后读库时认得每一层在补什么。

## 亲手题：让 Task 能 co_await 一个返回值

骨架摆在上面了，笔者需要你动手完成这个简单的小任务，这个小任务并不困难：

> - 实现一个 `Task<T>`，可以 `co_await` 一个返回值。（理解 `coroutine_handle` 的 resume/suspend 生命周期。），并利用`Task<int>`写一个协程函数 `co_add(a,b)`，返回 a+b，调用方 `co_await` 获取结果。

如果你看上面的题目一头雾水，觉得不知道笔者在说什么的话——您可以先阅读下面的调用代码，然后返回上文的骨架琢磨一下该怎么写。~~（ 你怎么知道笔者找到这个练习的时候自己也是一头雾水的）~~

```cpp
Task<int> co_add(int a, int b) {
	simple_log_with_func_name(
	    std::format("Get a: {} and b: {}, "
	                "expected a + b = {}",
	                a, b, a + b));
	co_return a + b;
}

Task<void> examples(int a, int b) {
	simple_log("About to call co_add");
	int result = co_await co_add(a, b);
	simple_log(std::format("Get the result: {}", result));
	co_return;
}

int main() {
	simple_log_with_func_name();
	examples(1, 2);
	simple_log("Done!");
}
```

你所需要做的，就是上面的代码跑起来，跑起来的办法就是实现`Task<T>`。如果您做好了，请您参考下面的代码对比一下实现。咱们后面会复用这里的`Task<T>`来完成带有返回值支持的调度器，那是下一篇的正题，您先把这一版自己跑通。

这里是笔者的代码。`helpers.h`已经在篇末附录里给全了清单，没有任何变化，放心使用。`simple_log`是笔者常用的日志小工具，清单收在本篇附录。

```cpp
#include "helpers.h"
#include <coroutine>
#include <format>

template <typename T>
class Task {
public:
	struct promise_type;
	using coro_handle = std::coroutine_handle<promise_type>;

	Task(coro_handle h)
	    : coroutine_handle(h) {
		simple_log_with_func_name();
	}

	~Task() {
		simple_log_with_func_name();
		if (coroutine_handle) {
			coroutine_handle.destroy();
		}
	}

	Task(Task&& o)
	    : coroutine_handle(o.coroutine_handle) {
		o.coroutine_handle = nullptr;
	}

	Task& operator=(Task&& o) {
		coroutine_handle = std::move(o.coroutine_handle);
		o.coroutine_handle = nullptr;
		return *this;
	}

	// concept requires
	struct promise_type {
		T cached_value;
		Task get_return_object() {
			simple_log_with_func_name();
			return { coro_handle::from_promise(*this) };
		}
		// we dont need suspend when first suspend
		std::suspend_never initial_suspend() {
			simple_log_with_func_name();
			return {};
		}
		// suspend always for the Task clean ups
		std::suspend_always final_suspend() noexcept {
			simple_log_with_func_name();
			return {};
		}

		void return_value(T value) {
			simple_log_with_func_name(std::format("value T {} is received!", value));
			cached_value = std::move(value);
		}

		void unhandled_exception() {
			// process notings
		}
	};

	bool await_ready() {
		simple_log_with_func_name();
		return false; // always need suspend
	}

	void await_suspend(std::coroutine_handle<> h) {
		simple_log_with_func_name(); // Should never be here
		h.resume(); // resume these always
	}

	T await_resume() {
		simple_log_with_func_name();
		return coroutine_handle.promise().cached_value;
	}

private:
	coro_handle coroutine_handle;

private:
	Task(const Task&) = delete;
	Task& operator=(const Task&) = delete;
};

template <>
class Task<void> {
public:
	struct promise_type;
	using coro_handle = std::coroutine_handle<promise_type>;

	Task(coro_handle h)
	    : coroutine_handle(h) {
		simple_log_with_func_name();
	}

	~Task() {
		simple_log_with_func_name();
		if (coroutine_handle) {
			coroutine_handle.destroy();
		}
	}

	Task(Task&& o)
	    : coroutine_handle(o.coroutine_handle) {
		o.coroutine_handle = nullptr;
	}

	Task& operator=(Task&& o) {
		coroutine_handle = std::move(o.coroutine_handle);
		o.coroutine_handle = nullptr;
		return *this;
	}

	// concept requires
	struct promise_type {
		Task get_return_object() {
			simple_log_with_func_name();
			return { coro_handle::from_promise(*this) };
		}
		// we dont need suspend when first suspend
		std::suspend_never initial_suspend() {
			simple_log_with_func_name();
			return {};
		}
		// suspend always for the Task clean ups
		std::suspend_always final_suspend() noexcept {
			simple_log_with_func_name();
			return {};
		}
		void return_void() { simple_log_with_func_name(); }
		void unhandled_exception() {
			// process notings
		}
	};

private:
	coro_handle coroutine_handle;

private:
	Task(const Task&) = delete;
	Task& operator=(const Task&) = delete;
};

Task<int> co_add(int a, int b) {
	simple_log_with_func_name(
	    std::format("Get a: {} and b: {}, "
	                "expected a + b = {}",
	                a, b, a + b));
	co_return a + b;
}

Task<void> examples(int a, int b) {
	simple_log("About to call co_add");
	int result = co_await co_add(a, b);
	simple_log(std::format("Get the result: {}", result));
	co_return;
}

int main() {
	simple_log_with_func_name();
	examples(1, 2);
	simple_log("Done!");
}
```

值得您停一步的地方有两处。第一处，`Task<void>` 的特化独立写了一份，promise 里去掉了 `cached_value`，`return_value` 换成 `return_void`，动手清单的第 2 条会用到它。第二处，`examples` 里 `co_return;` 一行不落，函数体走完协程照样收尾，`return_void` 等的就是这一刻。

如果你没有看懂发生了什么，请把上文骨架的逐行讲解再对一遍。如果实现跟你的差不多，您就可以翻到下一篇编写调度器了。

## 轮到您动手的时候

1. 给教学版 Task 补状态检查：没 start 就取结果、结果取两次、start 调两次，各加一道拦截。设计时想想 promise 里最少要记哪几个状态，检查放在 start、result、await_resume 的哪一处。
2. 亲手题的参考实现里带着一版 `Task<void>` 特化（promise 去掉 result，`return_value` 换成 `return_void`，第二篇讲过 C++20 里两者互斥），把它挪进您的工程，把 exp_h 的路径一改成无值任务跑通。

## 参考资源

本篇的 Task 本体素材来自旧目录 ch06 的《promise_type 与 awaitable》，SimpleTask、SafeTask 与调度器雏形在旧文里已有，例子与文字全部重写。亲手题一节由笔者 2025 年 10 月的博客《理解 C++20 的革命特性——协程支持》系列扩写而来，派活、调用代码与参考实现照博客原文收编，调度器与"谁在哪个线程上恢复"两节挪去了下一篇《[让协程排队跑起来：最小调度器与两条恢复路线](05-task-scheduler)》。两个实验另拿 `-fsanitize=address,undefined` 各复跑一遍，消毒器没有报告。推进思路参照 Lewis Baker 的协程系列，例子与文字都是笔者自己写的。语言机制的断言按 eel.is 上的现行工作草案与 cppreference 核对（2026-11-17 访问），条款号与段号可能随草案更新挪动。

- [Await expression（现行草案 expr.await）][draft-await]，await_suspend 的三种返回类型、句柄返回值的接力恢复注。
- [Coroutine definitions（现行草案 dcl.fct.def.coroutine）][draft-def]，final_suspend 的 noexcept 要求、协程状态的销毁次序。
- [Resumption（现行草案 coroutine.handle.resumption）][draft-resumption]，resume 与 destroy 的前置条件、跨执行代理恢复的实现定义行为、并发恢复的警告。
- [No-op coroutines（现行草案 coroutine.handle.noop）][draft-noop]，noop 句柄的 resume 与 destroy 都是空操作。
- [Coroutines (C++20)（cppreference）][cppref-coro]，完成的时序（未接住异常、unhandled_exception、final_suspend）、挂在最终挂起点的协程只能被销毁、协程状态的组成。
- [std::exception_ptr（cppreference）][cppref-exception-ptr]，异常指针的共享所有权与跨线程传递。
- [C++ Coroutines: Understanding Symmetric Transfer — Lewis Baker][baker-sym]，上一篇用过的 naive 与 sym 两种交接姿势的讲法，本篇 Task 的交接姿势参照。

[cppref-coro]: https://en.cppreference.com/w/cpp/language/coroutines
[cppref-exception-ptr]: https://en.cppreference.com/w/cpp/error/exception_ptr
[draft-await]: https://eel.is/c++draft/expr.await
[draft-def]: https://eel.is/c++draft/dcl.fct.def.coroutine
[draft-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[draft-noop]: https://eel.is/c++draft/coroutine.handle.noop
[baker-sym]: https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer

simple_log 这套日志小工具的清单收在 7.5《让协程排队跑起来：单例、spawn 与睡眠》的附录，那里有 helpers.h 与 helpers.cpp 的完整代码，这里不再重复。
