---
title: "Getting Coroutines to Queue Up: Singletons, spawn, and Sleeping"
description: "The next stop for Task is a queueing home: how a singleton template, a FIFO ready queue, spawn, a sleep awaiter, and a priority queue assemble a minimal scheduler, with the teaching-edition Loop and Yield placed alongside for comparison — what each of the two resumption routes governs, and which thread resumption lands on, is collected here too."
chapter: 5
order: 5
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
  source: documents/vol5-concurrency/chapter07-coroutines/05-task-scheduler.md
  source_hash: 3332fc9041b2ce5feda4c796b2289385a8f5476c647794708063ac74ffc08969
  translated_at: '2026-11-22T14:13:30+00:00'
  engine: glm-5.3
  token_count: 13152
---

# Getting Coroutines to Queue Up: A Minimal Scheduler and Two Resumption Routes

The previous article gathered results, exceptions, and destruction into a usable Task — but up to that point, coroutines were still each fighting alone, one chain finishing and clocking out. Coroutines look pretty useless: they can't do anything. But don't worry — so that we can push the power of coroutines further, we build Task a queueing home. The scheduler is Task's next stop, and this article builds that home.

## The Previous Article's Hands-On Exercise: How co_add Landed

The previous article's hands-on exercise was making Task able to `co_await` a return value; the complete reference implementation is collected in the hands-on section of [Wrapping Results: A Teaching-Edition Task](04-task-type). We will not copy the code over again here — instead, its fine points get explained one by one.

First, as already mentioned last time — any function that runs as a coroutine must return a **coroutine return type**, and this requires, no negotiation allowed, a nested `struct promise_type`, plus implementing every required interface; the reference implementation leaves none of them out. Pick out the return-value-related ones to look at:

In this article's example, what is easy to understand is that `co_add` does not need to suspend upon creation, so returning `std::suspend_never` is enough — we go straight to executing the returned result `co_return a + b`; once `a + b` is computed, it is delivered into `return_value`. One thing to note — last article already discussed which lives longer, the return type or the coroutine handle itself, and that is exactly why we choose to suspend at the end: the higher-level `Task` then takes responsibility for destroying the coroutine object, instead of it settling its own affairs. This structure will not feel unfamiliar; the previous article already explained what it is doing.

`co_await` wants to await a `Task<int>`, so any non-void `Task` must also implement the Awaitable interface (note: this does not mean every return struct carrying a PromiseType interface must implement Awaitable — it means Awaitable is needed only when we need to co_await that interface; please keep the logic straight). The trio looks like this in the reference implementation:

```cpp
	bool await_ready() {
		simple_log_with_func_name();
		return false; // always need suspend
	}

	void await_suspend(std::coroutine_handle<> h) {
		simple_log_with_func_name(); // Should never be here
		h.resume(); // resume these always, call await_resume then
	}

	T await_resume() {
		simple_log_with_func_name();
		return coroutine_handle.promise().cached_value;
	}
```

Logically, we do not actually need the suspend interface — but our result is stored in the promise_type held by the coroutine_handle, and at this point **we need to take over the waiting logic, so it still must suspend**.

> await_ready can actually also be read as: we need to take over the waiting logic and do our own handling

## Implementing the Simplest Possible Scheduler

We are about to implement the simplest possible scheduler right now. Here are the requirements:

> - Write a singleton **single-threaded scheduler** (event loop) that can schedule multiple `Task`s. (Suggested: write a singleton template for fun; besides, Task's basic code was already finished by the previous article's hands-on exercise.)
> - Implement a `sleep(ms)` awaiter
> - Check that it works — write 3 coroutines running concurrently: printing "A", "B", "C", alternating.

### Step One — Implement a Singleton Template

I decided to implement a simple singleton template, convenient for reuse in our other projects. On the discussion of the singleton pattern: even though dependency injection (DI) would be more appropriate, we still write a static-based singleton template (coroutines only arrived with C++20, and C++11 onward already guarantees that static initialization is thread-safe).

> single_instance.hpp

```cpp
#pragma once

template <typename SingleInstanceType>
class SingleInstance {
public:
	static SingleInstanceType& instance() {
		static SingleInstanceType instance;
		return instance;
	}

protected:
	SingleInstance() = default;
	virtual ~SingleInstance() = default;

private:
	SingleInstance(const SingleInstance&) = delete;
	SingleInstance& operator=(const SingleInstance&) = delete;
	SingleInstance(SingleInstance&&) = delete;
	SingleInstance& operator=(SingleInstance&&) = delete;
};
```

Obviously, we disabled every form of copy and construction, and for convenience in later use we adopt a safe virtual destructor. `SingleInstance()` goes under protected access — our singleton subclasses need to reach it, and this guarantees we syntactically dodge the creation of a second instance. In use, we merely write:

```cpp
class Schedular : public SingleInstance<Schedular>
{
    Schedular() = default; // 还是藏起来我们的构造函数
public:
	friend class SingleInstance<Schedular>;
}
```

> As it happens, I have written a discussion of the singleton pattern, with the implementation also in C++20; see the blogs:
>
> - [CSDN: A Close Reading of C++20 Design Patterns — Creational Patterns: The Singleton (CSDN blog)](https://blog.csdn.net/charlie114514191/article/details/152166469)
> - [charliechen114514.tech: A Close Reading of C++20 Design Patterns — Creational Patterns: The Singleton](https://www.charliechen114514.tech/archives/chuang-zao-xing-she-ji-mo-shi-dan-li-mo-shi)

### Step Two: A First Pass of Modifying Our `Task`, Giving the Scheduler a Chance to Take Over Our Coroutines

Obviously — now that we have decided to schedule our coroutines with a scheduler — every suspension must be under our control rather than decided by the return struct itself; for that, our initialization must also suspend immediately (in the previous article's hands-on reference implementation, `initial_suspend` returned `std::suspend_never` — eager-start, running at creation. Handed to a scheduler, it must be the other way around: registration must precede running, the reasoning from 7.4; the teaching-edition Task has been `suspend_always` from the start):

```cpp
		// we need suspend when first suspend
		std::suspend_always initial_suspend() {
			// simple_log_with_func_name();
			return {};
		}
```

The same holds for both the primary template and the partial specialization.

### Step Three: Thinking Through the Scheduler's Interface

Now we prepare to think through the scheduler's interface. Fortunately our coroutines are not preemptively scheduled, so the code is very easy to write (though "easy" may be optimistic) — just follow FIFO scheduling when nothing yields.

First, the scheduler must support a Sleep call — putting the current coroutine to sleep (if other coroutine tasks exist, do those; if not, we declare the current thread idle, and calling the `std::this_thread::sleep_*` interface is fine).

So we need the scheduler to know which coroutines need their nap — the scheduler needs a container managing who sleeps, and a way to push a specific coroutine that wants to sleep.

One thing to know — for convenience, the standard library does have an interface called `sleep_until`, so for easy management and reuse of the standard library interface, we design the scheduler's sleep_until interface — it says we sleep until the specified time point and are then ready to be scheduled (once more: coroutine scheduling is cooperative; we can only guarantee a lower bound on the sleep time).

```cpp
void Schedular::sleep_until(std::coroutine_handle<> which, // 谁需要休眠？
	                 	std::chrono::steady_clock::time_point until_when);
```

Additionally, we need a push interface: the spawn interface, used to accept the struct returned by a coroutine function. All scheduling of that struct must be taken over by the scheduler. So don't forget to declare the scheduler class as a friend in Task.

```cpp
	template <typename T>
	void Schedular::spawn(Task<T>&& task); // Task只可以被移动，所以放这个接口进来
```

Finally there is one scheduling interface — the run interface:

```cpp
void Schedular::run();
```

It will start our coroutine scheduling. Just three!

### Step Four: Implementing the Interfaces Above

#### Implementing the spawn Interface: Taking Over the Coroutine Return Structs

We start with scheduling itself. First, we need to buffer the coroutine handles on the ready list (note: not the Task itself — we are scheduling coroutines, not coroutine return structs). The scheduling policy mentioned above is FIFO, so first-come-first-served calls for a queue to handle the storage.

```cpp
std::queue<std::coroutine_handle<>> ready_coroutines; // 一个简单的队列即可
```

So our spawn interface becomes very easy to implement —

```cpp
void Schedular::internal_spawn(std::coroutine_handle<> h) {
    // private实现，用户不应该直接随意的触碰调度队列
	ready_coroutines.push(h);	// 加入调度队列
}

// spawn是一个桥接的接口，我们会取出来Task内托管的coroutine_handle协程句柄，交给我们的
// 调度器来管理
template <typename T>
inline void Schedular::spawn(Task<T>&& task) {
	internal_spawn(task.coroutine_handle);
	task.coroutine_handle = nullptr; // 让Task不再托管coroutine_handle本身
}
```

#### Implementing the Sleep Mechanism

Sleeping needs to register how long we sleep and who sleeps, and it also needs sorting by priority (think: given three sleep requests of 100ms, 200ms, and 300ms, obviously the 100ms sleeper must go first, then 200ms, then 300ms — the other way around, and the first two would long since have missed their wake-up). Clearly a priority queue comes to mind at once. But a priority queue needs a comparison method to produce a min/max heap. So we need to abstract a `SleepItem` struct — registering our root as the sleep event that is smallest, or in other words, nearest to the current time point.

```cpp
	struct SleepItem {
		SleepItem(std::coroutine_handle<> h,
		          std::chrono::steady_clock::time_point tp)
		    : coro_handle(h)
		    , sleep(tp) {
		}
		std::chrono::steady_clock::time_point sleep;
		std::coroutine_handle<> coro_handle;
		bool operator<(const SleepItem& other) const {
			return sleep > other.sleep;
		}
	};

	std::priority_queue<SleepItem> sleepys;
```

But we have not written the user-side code yet; users expect to sleep like this:

```cpp
co_await sleep(300ms);
```

Well, how to put it? Seeing co_await, your reflex must be to implement the awaitable interface. So —

```cpp
struct AwaitableSleep {
	AwaitableSleep(std::chrono::milliseconds how_long)
	    : duration(how_long)
	    , wake_time(std::chrono::steady_clock::now() + how_long) { }

	/**
	 * @brief await_ready always lets the sessions sleep!
	 *
	 */
	bool await_ready() { return false; } // 总是我们接管剩下的流程
	void await_suspend(std::coroutine_handle<> h) {
         // 执行推送，然后后面我们自己的调度器会取出来这个句柄扔到就绪队列中
		Schedular::instance().sleep_until(h, wake_time);
	}

	// 什么都不做
	void await_resume() { }

private:
	std::chrono::milliseconds duration; // 方便获取接口或者调试，性能优先下可以踢掉这个
	std::chrono::steady_clock::time_point wake_time;
};

inline AwaitableSleep sleep(std::chrono::milliseconds s) {
	return { s };
}
```

#### Implementing the Scheduling Logic

First, sleeping is what you do only when there is no work; the implementation priority is obvious — active coroutines first!

```cpp
	void run() {
		// if there is any corotines ready or sleepy unfinished
		while (!ready_coroutines.empty() || !sleepys.empty()) {
            // 进来这个逻辑，就表明我们现在是有事情做的——不管是睡大觉还是拉起一个协程。
			while (!ready_coroutines.empty()) {
				auto front_one = ready_coroutines.front();
				ready_coroutines.pop();
				front_one.resume(); // OK, hang this on!
			}

            ...
		}
	}
```

Only when all our active code has finished executing do we go check whether the sleep queue holds anyone waiting to be woken —

```cpp
			auto now = current(); // current返回std::chrono::steady_clock::now()
			while (!sleepys.empty() && sleepys.top().sleep <= now) {
				ready_coroutines.push(sleepys.top().coro_handle);
				sleepys.pop();
			}
```

Very good — if our current time has passed the designated wake-up point (that is, sleepys.top().sleep), we send every coroutine whose time has passed into our ready queue.

Next step: if coroutines still need to sleep and no new ready-queue entries have arrived, we immediately put this thread to sleep:

```cpp
	void run() {
		// if there is any corotines ready or sleepy unfinished
		while (!ready_coroutines.empty() || !sleepys.empty()) {
			while (!ready_coroutines.empty()) {
				auto front_one = ready_coroutines.front();
				ready_coroutines.pop();
				front_one.resume(); // OK, hang this on!
			}

			auto now = current();
			while (!sleepys.empty() && sleepys.top().sleep <= now) {
				ready_coroutines.push(sleepys.top().coro_handle);
				sleepys.pop();
			}

			if (ready_coroutines.empty() && !sleepys.empty()) {
				// OK, we can sleep
				std::this_thread::sleep_until(sleepys.top().sleep);
			}
		}
	}
```

#### Continuing to Modify Task's Interface

Now tasks need to push directly into the queue, and we need to think these questions through. We would use the scheduler like this:

```cpp
Task<int> co_add(int a, int b) {
	co_await sleep(300ms);
	co_return a + b;
}

Task<void> worker(const char* name, int a, int b) {
	int result = co_await co_add(a, b);
	std::println("{}: {} + {} = {}", name, a, b, result);
}

Task<void> main_task() {
	co_await worker("TaskA", 1, 2);
	co_await worker("TaskB", 3, 4);
	co_await worker("TaskC", 5, 6);
}
```

Every parent coroutine sets down its own running; per the logic of C++20 stackless coroutines — we must save the coroutine's handle ourselves. So it is easy to think of: Task itself must store the parent coroutine's handle, so that when the child coroutine finishes we can resume the parent's running and the code can continue.

That may be too big a jump; let's take it one at a time — when, in our parent coroutine, we write the line `co_await worker("TaskA", 1, 2);`, the parent coroutine must give up its own running and wait for worker's result. At this point, recall the coroutine framework's run logic covered in [How `co_await` Joins the Wait](03-co-await-and-awaiters): go through `await_ready` to check whether to suspend — we obviously returned false, wanting to take over the logic ourselves. So the next step's execution flow is forwarded into `await_suspend`, and this step is exactly what we want — the parent coroutine must be suspended, so the child coroutine must be pushed!

```cpp
	// 在创建的子协程的协程返回体中
	void await_suspend(std::coroutine_handle<> h) {
		// simple_log_with_func_name(); // Should never be here
		simple_log("Current Routine will be suspend!");
		coroutine_handle.promise().parent_coroutine = h;
		simple_log("Child Routine will be called resume!");
		Schedular::instance().internal_spawn(coroutine_handle);
	}
```

`coroutine_handle.promise().parent_coroutine = h;` sets the child coroutine's parent coroutine to the current one, then puts the child coroutine into the ready queue. Nothing wrong with that! (Note this code is the child coroutine's return struct.)

Now our child coroutine has been delivered into the ready queue, and excitingly — it will be delivered into the ready-handling logic; when our scheduler runs the ready-coroutine-queue code, we execute this logic —

```cpp
			while (!ready_coroutines.empty()) {
				auto front_one = ready_coroutines.front();
				ready_coroutines.pop();
				front_one.resume(); // OK, hang this on!
			}
```

The child coroutine gets resumed here, executing worker's code — the child coroutine then suspends. When worker finishes executing, we still follow the procedure — what gets called is `final_suspend`. Remember the parent_coroutine we stored? This is where it flexes — the child coroutine's ending requires the parent coroutine to set down its executing code, so things become very easy:

```cpp
		std::suspend_always final_suspend() noexcept {
			// simple_log_with_func_name();
			if (parent_coroutine) {
				simple_log("parent_coroutine will be wake up");
                 // 父协程拉起来执行代码
				Schedular::instance().internal_spawn(parent_coroutine);
			}
			return {};	// 子协程由Task结构体托管，这个逻辑不会发生改变
		}
```

Reaching this point, all our code is complete. Let's compile and run:

```text
[charliechen@Charliechen coroutines]$ build/schedular/schedular
10:36:12 :Current Routine will be suspend!
10:36:12 :Child Routine will be called resume!
10:36:12 :Current Routine will be suspend!
10:36:12 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskA: 1 + 2 = 3
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskB: 3 + 4 = 7
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskC: 5 + 6 = 11
```

The code works perfectly.

How was the log above produced? The answer:

```text
[charliechen@Charliechen coroutines]$ build/schedular/schedular
10:36:12 :Current Routine will be suspend! // main_task准备被挂起
10:36:12 :Child Routine will be called resume! // worker("TaskA", 1, 2);准备干活
10:36:12 :Current Routine will be suspend! // worker("TaskA", 1, 2)准备被挂起
10:36:12 :Child Routine will be called resume! // co_add准备干活
10:36:13 :parent_coroutine will be wake up // co_add作为叶子协程，准备结束自己，拉起父协程worker干活
TaskA: 1 + 2 = 3 // worker被拉起，执行打印逻辑

// 如下的逻辑是类似的
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskB: 3 + 4 = 7
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskC: 5 + 6 = 11
```

## Appendix: The helpers.h and helpers.cpp Listings

The two small logging tools used in the scheduler code — their listings are collected here exactly as they are in my repository. `simple_log` prints one timestamped log line; `simple_log_with_func_name` reports the function name along the way.

> helpers.h

```cpp
#pragma once
#include <source_location>
#include <string>
void simple_log(const std::string& v, bool request_dump_time = true);

void simple_log_with_func_name(
    const std::string& other = "",
    const std::string& func_name
    = std::source_location::current().function_name(),
    bool request_dump_time = true);
```

> helpers.cpp

```cpp
#include "helpers.h"
#include <chrono>
#include <format>
#include <iomanip>
#include <iostream>
#include <print>

namespace {
void dump_time() {
	auto now = std::chrono::system_clock::now();
	std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::tm localTime;
#ifdef _WIN32
	localtime_s(&localTime, &currentTime); // Windows 平台
#else
	localtime_r(&currentTime, &localTime); // Linux/Unix 平台
#endif

	std::cout << std::put_time(&localTime,
	                           "%H:%M:%S")
	          << " :";
}
}
void simple_log(const std::string& v, bool request_dump_time) {
	if (request_dump_time) {
		dump_time();
	}
	// logings
	std::println("{}", v);
}

void simple_log_with_func_name(
    const std::string& other,
    const std::string& func_name,
    bool request_dump_time) {

	simple_log(std::format(
	               "function: {} is involked, {}", func_name, other),
	           request_dump_time);
}
```
