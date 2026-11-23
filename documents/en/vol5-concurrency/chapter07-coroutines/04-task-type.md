---
title: "Wrapping Results: A Teaching-Edition Task"
description: "How results, exceptions, and destruction come together in a Task type: lazy start keeps the race out the door, exceptions relay level by level along the promise, the destructor collects every frame — three paths measured against readings, with a hands-on exercise at the end stocking material for the next article's scheduler."
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
translation:
  source: documents/vol5-concurrency/chapter07-coroutines/04-task-type.md
  source_hash: 192f1b9b1eed77826794e4bee9e11c2e679c933fad02523eb411ce8e5818f4d0
  translated_at: '2026-11-22T14:09:00+00:00'
  engine: glm-5.3
  token_count: 18802
---

# Wrapping Results: A Teaching-Edition Task

We wrap the trio and the sym posture into a usable Task type — one that receives results, passes exceptions, and destroys safely.

## The Teaching-Edition Task: Results, Exceptions, Destruction

The first article's experiments featured a minimal Task — eager-start, running at creation (see exp_b in [Why Suspend and Resume Are Needed](01-why-suspend-resume)). As a solo act it was fine; this article wants it to be co_await-able by other coroutines, and eager-start exposes the fault at once. Walk the timing through: at the moment `pipeline()` is called (the uneventful pipeline coroutine in the experiment below), the coroutine body starts running immediately, while the outer awaiter only gets its chance to register the waiter's handle into the promise at `await_suspend` — one function return separates the two steps. If the child coroutine finishes its work before the registration lands, then no matter how correctly continuation is registered, nobody will come to resume the waiter — a race is buried right there.

The fix is switching to lazy start: we reverse the placement of registration and startup. `initial_suspend` returns `suspend_always`; at creation the coroutine runs not one line but hangs obediently at the door. Only after `await_suspend` registers the waiter into continuation do we hand the child coroutine's handle out and let it run. Registration necessarily precedes running, and the race has nowhere to stand. The fork between lazy-start and eager-start was covered in the second article; this article's choice now has its complete justification: for a Task that can be awaited, lazy start is the safe spelling we can choose.

Here is the Task in full — the skeleton part of exp_h_task.cpp, explained line by line below; you may also stop and read it through:

<OnlineCompilerDemo
  title="The teaching-edition Task skeleton: the three-way handover of results, exceptions, and destruction"
  source-path="code/examples/vol5/chapter07/11_task_skeleton.cpp"
  description="The skeleton comes with a minimal demo main: the trivial coroutine co_returns 42, start launches it, result fetches it back, and the frame count shows 1 frame registered before destruction."
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

Three members sit on the promise — result, failure, continuation — each carrying one leg of the handover. When `co_return 40;` executes, the compiler calls `return_value`; the value moves into result and shares life and death with the coroutine frame. After the waiter resumes, entry comes through `await_resume`, and `take_result` moves the value out of the promise — the whole journey of a result is these two steps, and the storage point is identical to the second article's Generator. The landing points of `co_return` were covered there; this article adds one discipline: in C++20, `return_value` and `return_void` are mutually exclusive, the teaching edition follows the value-carrying road, and every path of the example coroutines writes its `co_return`. The consequence of a body running to the end without one was measured in the second article — undefined behavior, with the compiler silent — and we dodge it through discipline.

The exception leg goes to failure. When the coroutine body throws an exception nobody catches, the standard catches it, calls `unhandled_exception`, and then proceeds to the final suspension point as usual — sourced from cppreference's Completion section. The teaching edition chooses the storage school (the two schools were named in the second article): `failure = std::current_exception()` stores the exception pointer into the promise, and whoever fetches the result rethrows it as it was with `rethrow_exception`. `std::exception_ptr` is a shared-ownership pointer; cppreference states plainly that it can be passed to other functions and other threads, and so the baton can travel the whole task chain. The previous article's experiment used the terminate school; this article switches to storage. You saw the two schools' difference in the second article: the terminate school flips the table on the spot, the storage school stores the exception away for the outside to fetch. Task's position is that failure is also part of the result, to be delivered into the hands of whoever fetches it.

The relay's walk deserves a slow look. The leaf coroutine boom throws an exception; its promise stores the pointer, and the final suspension point hands over as usual. The relay awaiting it wakes from `co_await`; `take_result` inside `await_resume` rethrows the exception into relay's function body; relay wrote no try, so the exception continues upward, and relay's own promise stores another pointer. Level by level it travels — every level's promise is a relay station, nobody catches, and the exception comes to rest in the root Task's promise, surfacing only when main fetches the result. You saw the same shape of delivery in Unit 4, with exceptions traveling through the shared state to `get()` (see [Running Computations with `std::async`](../chapter04-organizing-tasks/01-launch-with-async)); here only the storage point changes from shared state to promise.

Both handover points are written in the sym posture settled by the previous article; let's check them one at a time. The first sits in Task's `await_suspend`: after registering continuation, it returns the child coroutine's handle — the draft [expr.await]/(5.1.1) says it plainly, the returned handle gets resumed, a string of coroutines can relay all the way, finally returning control to the original resumer. The second sits in the Done that `final_suspend` returns — an old friend from the previous article's chain experiment: someone waiting, hand control back; nobody, and it is the root, so hand over `std::noop_coroutine()`, whose resume is a no-op ([coroutine.handle.noop]), amounting to returning the control flow to the original resumer. The reasons for decoupling stack depth from chain depth under deep chains were told there with one-million-level readings; this article does not rerun them.

The destruction leg is carried by RAII. The coroutine frame comes from the heap; `coroutine_handle` is a non-owning handle whose destructor does nothing, and the handle does not carry the blame for leaks — the second article made that clear — so Task's destructor calls `destroy`, moving frame collection from your shoulders onto the type. Copying is deleted wholesale; the peril of two handles pointing at the same frame and collecting the same memory twice was also mentioned in the second article, and deleting copies is cheap insurance. Moving stays — factory functions returning Task have use for it.

The position that truly deserves full attention is the temporary in a co_await. In `co_await middle(20)`, the Task returned by middle (the relay on the chain) is a temporary whose lifetime must cross the suspension point, and the standard arranges for it to move into the parent coroutine's frame (the same arrangement was mentioned in the previous article when discussing the awaiter's housing). When the full co_await expression ends, the temporary Task destructs, and destroy lands on the child coroutine. At that moment the child coroutine stands at the final suspension point, and one line from cppreference holds the scene exactly: a coroutine suspended at its final suspension point can only be destroyed. Destruction's precondition is that the handle refers to a suspended coroutine ([coroutine.handle.resumption]) — the child coroutine happens to satisfy it — and so the temporary's destructor lands, time after time, on safe ground.

> While working out the timing, I ran ASan back and forth several times before daring to write the temporary Task's destruction into the main text; both experiments' readings came out clean. A silent tool is not proof of correctness (Unit 6 covered TSan's capability boundaries) — it merely swept the common dangling positions on our behalf.

The root Task's destruction is the least worrisome: it lives until main's scope ends, and we watch the destructor collect the last frame. Finished, never-started, exception-throwing — all exit through the same entrance.

### Three Paths, Measured

The reasoning is done; now let all three paths produce output. The experimental coroutines are two chains. pipeline strings leaf and middle together — the uneventful chain. The chain from top through relay to boom leaves the exception uncaught at all three levels:

<OnlineCompilerDemo
  title="Three paths measured: result relay, exception relay, all frames collected"
  source-path="code/examples/vol5/chapter07/12_exp_h_task.cpp"
  description="Path one relays 41 back along the chain; path two stores the exception in three levels of promise, surfacing only at the result fetch; path three collects both the finished and the never-started frames via destruction, with the count returning to zero."
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

Let's match the readings path by path. Path one: 41 is 20 times 2 plus 1; the result departs from leaf's `co_return`, relays through two stations — middle's result and pipeline's result — and is finally taken by main's `t.result()`. The chain's three coroutine frames are then all collected: leaf's and middle's frames follow the temporary Task's destruction at the co_await, and pipeline's frame follows t's destruction. Path two: the exception never bubbled out of start — start returned normally, the leaf's error lay quietly in three levels of promise relaying along, resurfacing only at `t.result()`; catching the exception at the result fetch is part of Task's semantics. Path three: with both Tasks in the register the frame count is 2 — run finished and hangs in the terminal station, never created but never started a single line, hanging at the door. The moment the scope ends, both frames are collected by their destructors and the count returns to zero — this is the reading from the GCC at hand; an implementation that elides heap allocations might read 0. Finished and never-started exit through the same destroy. I additionally reran all three experiments with `-fsanitize=address,undefined`; no reports.

### Where the Teaching Edition Falls Short of a Production Library

It runs; we still owe the full account — saying what it is, and what it is not. The three things teaching needs, Task provides; the positions listed below are deliberately left empty, and a production library's complexity all grows here:

- Not one state check is in place. Fetch the result without start, and you get the default value. Fetch the result a second time, and you get the moved-out husk — for int the value does not even change. Call start twice, and it is undefined behavior to resume a finished coroutine. The teaching edition blocks none of the three; a production library keeps a state machine in the promise and intercepts each item.
- No synchronization across threads. The registering and reading of continuation, the writing and reading of result, are all plain variables; this article's experiments lean on single-thread or join timing. To truly hand over between two threads, you must equip the state with atomics per Unit 5's memory ordering (see [How One Thread Observes Another Thread's Modifications](../chapter05-atomic-memory-model/03-happens-before)) — at minimum an atomic state plus a happens-before.
- No cancellation path. The stop protocol used in Units 1 and 3 (stop_token and kin) has no connection to Task; wanting to stop a half-finished task, the teaching edition offers no way.
- `Task<void>` is absent. In C++20 `return_value` and `return_void` are mutually exclusive (disclosed in the second article; relaxed only at C++29), so valueless tasks need a separately written promise — the teaching edition keeps only the value-carrying form.
- `result{}`'s initialization requires T to be default-constructible; move-only types without a default constructor cannot enter, and a production library swaps in stateful storage to get around it.
- Allocation is unoptimized. One `operator new` per coroutine frame, no pooling, and no hookup to `get_return_object_on_allocation_failure` (its use was covered in the second article).
- `await_ready` is constantly false — no fast path, so even a task whose result is already ready pays one suspend-and-resume.

The old directory ch06's promise_type and awaitable gave the embryos of SimpleTask and SafeTask; libraries like Lewis Baker's cppcoro are this teaching's distant destination — cancellation, synchronization, debugging support, memory strategies stacked layer upon layer, while the skeleton remains the one written here. Memorize the skeleton, and when you read a library later you will recognize what each layer is patching.

## A Hands-On Exercise: Making Task co_await a Return Value

The skeleton is laid out above; I need you to complete this simple little task with your own hands — it is not difficult:

> - Implement a `Task<T>` that can `co_await` a return value. (To understand the resume/suspend lifecycle of `coroutine_handle`.) Then, using `Task<int>`, write a coroutine function `co_add(a, b)` that returns a+b, with the caller using `co_await` to obtain the result.

If the task above leaves you completely lost and you have no idea what I am talking about — you can read the calling code below first, then return to the skeleton above and puzzle out how to write it. ~~(How did you know I was just as lost when I first found this exercise?)~~

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

All you need to do is make the code above run, and the way to make it run is to implement `Task<T>`. Once you have it working, compare your implementation against the code below. Later we will reuse this `Task<T>` to build the scheduler with return-value support — that is the next article's main topic; first get this version running yourself.

Here is my code. `helpers.h` has its full listing in the appendix at the end of this piece, unchanged — use it with confidence. `simple_log` is the small logging utility I often use; its listing is collected in this piece's appendix.

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

Two places deserve a pause. First, the `Task<void>` specialization is written independently — the promise drops `cached_value`, `return_value` becomes `return_void`, and hands-on item 2 will use it. Second, in `examples`, `co_return;` is never omitted; the body finished, the coroutine still wraps up, and `return_void` waits precisely for this moment.

If you did not follow what happened, go over the line-by-line explanation of the skeleton above once more. If your implementation is close to mine, you can turn the page and write the scheduler in the next article.

## Your Turn

1. Add state checks to the teaching-edition Task: fetching the result without start, fetching the result twice, calling start twice — one interception each. While designing, think about the fewest states the promise must record, and where among start, result, and await_resume each check belongs.
2. The hands-on exercise's reference implementation carries a `Task<void>` specialization (the promise drops result, `return_value` becomes `return_void` — the second article covered their mutual exclusion in C++20); move it into your project and convert exp_h's path one into a valueless task that runs through.

## References

This article's Task-body material comes from the old directory ch06's promise_type and awaitable, where SimpleTask, SafeTask, and the scheduler embryo already existed; the examples and words were rewritten entirely. The hands-on section was expanded from my October 2025 blog series Understanding C++20's Revolutionary Feature — Coroutine Support; the task assignment, calling code, and reference implementation were taken in as the blog wrote them, while the scheduler and the "who resumes on which thread" sections moved to the next article, [Getting Coroutines to Queue Up: A Minimal Scheduler and Two Resumption Routes](05-task-scheduler). Both experiments were additionally rerun with `-fsanitize=address,undefined`; the sanitizers reported nothing. The approach drew on Lewis Baker's coroutine series; the examples and words are my own. Language-mechanism claims were checked against the current working draft on eel.is and cppreference (accessed 2026-11-17); clause and paragraph numbers may shift as the draft updates.

- [Await expression (current draft expr.await)][draft-await] — the three return types of await_suspend, and the note on relayed resumption via returned handles.
- [Coroutine definitions (current draft dcl.fct.def.coroutine)][draft-def] — final_suspend's noexcept requirement, and the destruction order of the coroutine state.
- [Resumption (current draft coroutine.handle.resumption)][draft-resumption] — the preconditions of resume and destroy, the implementation-defined behavior of resuming across execution agents, and the concurrent-resumption warning.
- [No-op coroutines (current draft coroutine.handle.noop)][draft-noop] — the noop handle's resume and destroy are both no-ops.
- [Coroutines (C++20) (cppreference)][cppref-coro] — the completion sequence (uncaught exception, unhandled_exception, final_suspend), a coroutine at its final suspension point being destroy-only, and the composition of the coroutine state.
- [std::exception_ptr (cppreference)][cppref-exception-ptr] — the exception pointer's shared ownership and cross-thread delivery.
- [C++ Coroutines: Understanding Symmetric Transfer — Lewis Baker][baker-sym] — the telling of the naive and sym handover postures used in the previous article; the reference for this article's Task handovers.

[cppref-coro]: https://en.cppreference.com/w/cpp/language/coroutines
[cppref-exception-ptr]: https://en.cppreference.com/w/cpp/error/exception_ptr
[draft-await]: https://eel.is/c++draft/expr.await
[draft-def]: https://eel.is/c++draft/dcl.fct.def.coroutine
[draft-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[draft-noop]: https://eel.is/c++draft/coroutine.handle.noop
[baker-sym]: https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer

The listing of the simple_log logging utility is collected in the appendix of 7.5, Getting Coroutines to Queue Up: Singletons, spawn, and Sleeping, where the complete code of helpers.h and helpers.cpp lives; it is not repeated here.
