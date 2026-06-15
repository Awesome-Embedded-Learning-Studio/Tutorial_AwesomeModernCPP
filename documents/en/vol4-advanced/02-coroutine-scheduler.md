---
chapter: 10
difficulty: intermediate
order: 9
platform: host
reading_time_minutes: 25
tags:
- cpp-modern
- host
- intermediate
title: 'Understanding the Revolutionary Features of C++20 — Coroutine Support Part
  2: Writing a Simple Coroutine Scheduler'
translation:
  engine: anthropic
  source: documents/vol4-advanced/02-coroutine-scheduler.md
  source_hash: a958e4bdda10633048b6eed587a002c22173e7c2b1618a656893cf003d1e2265
  token_count: 6731
  translated_at: '2026-05-26T11:38:46.571919+00:00'
description: ''
---
# Understanding the Revolutionary Features of C++20 — Coroutine Support Part 2: Writing a Simple Coroutine Scheduler

## Preface

In the previous blog post, we understood the simplest coroutine scheduling interface in C++20 (though it was anything but simple). Obviously, before this post, our coroutines were still being scheduled using a single-coroutine scheduler. Coroutines seem pretty useless—incapable of doing much of anything. But don't worry, to further unleash the power of coroutines, I need you to complete this simple little task. It's not difficult:

> - Implement a `Task<T>` that can `co_await` a return value. (Understand the resume/suspend lifecycle of `coroutine_handle`.) Then, use `Task<int>` to write a coroutine function `co_add(a,b)` that returns a+b, and have the caller `co_await` to get the result.

If you're completely lost and have no idea what I'm talking about, you can first read the calling code below, then go back to my previous blog post and figure out how to write it. ~~(How did you know I was also completely lost when I found this exercise?)~~

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

All you need to do is make the code above run. The way to make it run is by implementing `Task<T>`. If you've done it, please compare your implementation with the code below. We will reuse `Task<T>` later to accomplish the main topic of this blog post—a scheduler with return value support.

Here is my code. `"helpers.h"` was already provided in the previous blog post without any changes, so feel free to use it as is.

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

If you didn't understand what just happened, please continue reading the content below. If your implementation is similar to mine, you can scroll back up and continue writing the scheduler.

## Implementing the Simplest Scheduler

We are now going to implement the simplest scheduler right away. Here are our requirements:

> - Write a singleton **single-threaded scheduler** (event loop) that can schedule multiple `Task`. (It's recommended to write a singleton template for fun; additionally, the basic Task code is already done from the previous task.)
> - Implement the `sleep(ms)` awaiter
> - Test if it works—write three coroutines running concurrently: printing "A", "B", "C", alternating their output.

#### Step 1 — Implementing a Singleton Template

I decided to implement a simple singleton template for easy reuse in our other projects. Regarding the discussion of the singleton pattern, although dependency injection (DI) is more appropriate, we will still write a static-based singleton template (coroutines are only available since C++20, and C++11 and above already guarantee thread-safe initialization of static variables).

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

Obviously, we disabled any form of copying and construction. Also, for convenience in later use, we adopted a safe virtual destructor. `SingleInstance()` needs to be placed under the protected scope so that our singleton subclasses can access it, ensuring we syntactically prevent the creation of a second instance. In terms of usage, we just need to write it like this:

```cpp
class Schedular : public SingleInstance<Schedular>
{
    Schedular() = default; // 还是藏起来我们的构造函数
public:
 friend class SingleInstance<Schedular>;
}

```

> Coincidentally, I've written a discussion on the singleton pattern, and the implementation is also in C++20. Refer to these blog posts:
>
> - [CSDN: Deep Dive into C++20 Design Patterns — Creational Design Patterns: Singleton Pattern - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/152166469)
> - [charliechen114514.tech: Deep Dive into C++20 Design Patterns — Creational Design Patterns: Singleton Pattern](https://www.charliechen114514.tech/archives/chuang-zao-xing-she-ji-mo-shi-dan-li-mo-shi)

#### Step 2: Preliminarily Modify Our `Task` to Give the Scheduler a Chance to Take Over Our Coroutines

Obviously—now that we've decided to use a scheduler to schedule our coroutines—any suspend operation needs to be controlled by us rather than having the returned struct make the decision on its own. To achieve this, our initialization also needs to be suspended immediately:

```cpp
  // we need suspend when first suspend
  std::suspend_always initial_suspend() {
   // simple_log_with_func_name();
   return {};
  }

```

This applies to both the generic implementation and the partial specialization implementation.

#### Step 3: Thinking About the Scheduler's Supported Interfaces

We are now ready to think about the scheduler's interfaces. Fortunately, our coroutines use cooperative scheduling, not preemptive scheduling, so the code is very easy to write (though "easy" is unlikely). We just need to follow FIFO scheduling when there is no yielding.

First, the scheduler needs to support a Sleep call, which means putting the current coroutine to sleep (if there are other coroutine tasks, do those; if not, it means the current thread needs to be idle, so we just call the `std::this_thread::sleep_*` interface).

Therefore, we need to let the scheduler know which coroutines need to sleep—the scheduler needs a container to manage who needs to sleep, and a way to push a specific coroutine that needs to sleep.

One thing to note—for convenience, the standard library provides an interface called `sleep_until`. So, for easy management and to reuse standard library interfaces, we design a `sleep_until` interface for the scheduler—it indicates that we want to sleep until a specified time point and then be ready to be scheduled (to reiterate, we need to note that coroutine scheduling is cooperative scheduling, so we can only guarantee the lower bound of the sleep event).

```cpp
void Schedular::sleep_until(std::coroutine_handle<> which, // 谁需要休眠？
                   std::chrono::steady_clock::time_point until_when);

```

Additionally, we need a push interface: the `spawn` interface, used to accept the coroutine return struct. All scheduling for this struct must be taken over by the scheduler. So, don't forget to declare the scheduler class as a friend in the Task.

```cpp
 template <typename T>
 void Schedular::spawn(Task<T>&& task); // Task只可以被移动，所以放这个接口进来

```

Finally, there is the scheduling interface—the `run` interface.

```cpp
void Schedular::run();

```

It will start our coroutine scheduling. Just three!

#### Step 4: Implementing the Above Interfaces

##### Implementing the spawn Interface to Manage the Coroutine Return Struct Returned by the Coroutine Function

Let's start with the scheduling itself. First, we need to cache the coroutine interfaces in the ready queue (note that this is not the Task itself; we are scheduling coroutines, not the coroutine return structs). As mentioned above, our scheduling strategy is FIFO, so first-come, first-served requires us to use a queue to handle our storage.

```cpp
std::queue<std::coroutine_handle<>> ready_coroutines; // 一个简单的队列即可

```

So, our `spawn` interface becomes very easy to implement—

```cpp
void Schedular::internal_spawn(std::coroutine_handle<> h) {
    // private实现，用户不应该直接随意的触碰调度队列
 ready_coroutines.push(h); // 加入调度队列
}

// spawn是一个桥接的接口，我们会取出来Task内托管的coroutine_handle协程句柄，交给我们的
// 调度器来管理
template <typename T>
inline void Schedular::spawn(Task<T>&& task) {
 internal_spawn(task.coroutine_handle);
 task.coroutine_handle = nullptr; // 让Task不再托管coroutine_handle本身
}

```

##### Implementing the Sleep Mechanism

Sleeping requires registering how long we sleep, who is sleeping, and it also needs to be sorted by a certain priority (think about it: if there are three sleep requests for 100ms, 200ms, and 300ms, the 100ms one should definitely wake up first, then 200ms, then 300ms. If it were the other way around, the first two would be long past done). Obviously, we immediately think of a priority queue. But a priority queue needs to provide a comparison method to produce a min/max heap. So we need to abstract a `SleepItem` struct—it registers that our root is the one with the smallest sleep event. Or rather, the one closest to the current time point.

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

But we haven't implemented the user-side code yet. Users expect to be able to sleep like this:

```cpp
co_await sleep(300ms);

```

Hmm, what does that mean? When we see `co_await`, we should reflexively implement the awaitable interface. So—

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

##### Implementing the Scheduling Logic

First, sleeping is only done when there's no work to do. The implementation priority is obvious—prioritize processing active coroutines!

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

If we've finished executing all active code, we then check if there are any guys waiting to be woken up in the sleep queue—

```cpp
   auto now = current(); // current返回std::chrono::steady_clock::now()
   while (!sleepys.empty() && sleepys.top().sleep <= now) {
    ready_coroutines.push(sleepys.top().coro_handle);
    sleepys.pop();
   }

```

Excellent. If our current time has passed the specified sleep wake-up time point (i.e., `sleepys.top().sleep`), we need to send all coroutines that have passed their time points into our ready queue.

Next, if we still have coroutines that need to sleep and no new ready queue arrivals, we immediately put the current thread to sleep.

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

##### Continuing to Modify the Task Interface

Now tasks need to push directly into the queue, so we need to think about these issues. When we use the scheduler, we will use it like this:

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

All parent coroutines will yield their own execution. Following the logic of C++20 stackless coroutines—we need to save the coroutine's handle ourselves. So it's easy to think of—the Task itself needs to store the parent coroutine's handle, so that when our child coroutine resumes, it can resume the parent coroutine's execution and continue the code.

That might be too big of a leap. Let's take it one step at a time—in our parent coroutine, when we write the code—`co_await worker("TaskA", 1, 2);`, the parent coroutine needs to give up its own execution and wait for the worker's result. At this time, we recall the execution logic of our coroutine framework from the first blog post: we go to `await_ready` to check whether to suspend—we obviously return no, because we want to take over the logic ourselves. So the next step of the execution flow is forwarded to `await_suspend`. This step is exactly what we want—the parent coroutine needs to be suspended, so the child coroutine needs to be pushed!

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

`coroutine_handle.promise().parent_coroutine = h;` sets the child coroutine's parent coroutine to the current thread, and then puts the child coroutine into the ready queue. Nothing wrong with that! (Note that this code is for the child coroutine return struct.)

Now, our child coroutine has been sent to the ready queue, and excitingly—it will be sent to the ready processing logic. When our scheduler executes the ready coroutine queue code, we will execute this logic—

```cpp
   while (!ready_coroutines.empty()) {
    auto front_one = ready_coroutines.front();
    ready_coroutines.pop();
    front_one.resume(); // OK, hang this on!
   }

```

The child coroutine is resumed here, executing the worker's code—the child coroutine is now suspended. When the worker finishes executing, we still follow the process—calling `final_suspend`. Remember the `parent_coroutine` we stored? This is where it comes into play—the end of the child coroutine requires the parent coroutine to yield its executing code. So things become very easy:

```cpp
  std::suspend_always final_suspend() noexcept {
   // simple_log_with_func_name();
   if (parent_coroutine) {
    simple_log("parent_coroutine will be wake up");
                 // 父协程拉起来执行代码
    Schedular::instance().internal_spawn(parent_coroutine);
   }
   return {}; // 子协程由Task结构体托管，这个逻辑不会发生改变
  }

```

Reaching this point, all our code is complete. Let's compile and run it:

```cpp
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

The code works perfectly. How was the log above generated? The answer is as follows:

```cpp

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

# Appendix: Implementing the Coroutine Addition Function `co_add`

To save you from flipping back and forth, I'll just paste a copy of the code right here.

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

```

First, as we mentioned in the previous blog post—any function running in a coroutine must return a **coroutine return type**. This requires you to unquestionably embed a struct `struct promise_type`, and you must implement the interface—

```cpp
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

```

In this example, it's not hard to understand—`co_add` doesn't need to suspend upon coroutine creation, so we just need to return `std::suspend_never` to let us immediately execute on the returned result `co_return a + b`. Once `a + b` is calculated, it will be sent into `return_value`. It's worth noting—in the previous blog post, we already discussed whose lifetime should be longer between the return type and the coroutine handle itself. This is also why we chose to suspend, so that the upper-level `Task` is responsible for destructing the coroutine object, rather than it resolving itself. You won't find this structure unfamiliar; the previous blog post already explained what this structure is actually doing.

`co_await` requires waiting for `Task<int>`, so any non-empty `Task` also needs to implement the Awaitable interface (note that it's not that every return struct with a PromiseType interface needs to implement the Awaitable interface, but rather we only need to implement the Awaitable interface when we need to `co_await` it. Please make sure you understand the logical relationship.)

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

Logically, we actually don't need the suspend interface, but our result is stored in the `promise_type` of the `coroutine_handle`. At this point—**we need to take over the waiting logic, so we still need to suspend**.

> `await_ready` can actually also be expressed as—we need to take over the waiting logic to do our own processing.
>
> The first blog post is at:
>
> - CSDN link: [CSDN](https://blog.csdn.net/charlie114514191/article/details/152518557)
> - My own blog's link: [charliechen114514.tech](https://www.charliechen114514.tech/archives/li-jie-c-20de-ge-ming-te-xing----xie-cheng-zhi-chi-1)

# Appendix 2: The Scheduler Code

> schedular.cpp: main example code

```cpp
#include "schedular.hpp"
#include <print>

using namespace std::chrono_literals;

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

int main() {
 Schedular::instance().spawn(main_task());
 Schedular::instance().run();
}

```

> schedular.hpp: scheduler code

```cpp
#pragma once
#include "single_instance.hpp"
#include <chrono>
#include <coroutine>
#include <queue>
#include <thread>

template <typename T>
class Task;
struct AwaitableSleep;

class Schedular : public SingleInstance<Schedular> {
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

 std::queue<std::coroutine_handle<>> ready_coroutines;
 std::priority_queue<SleepItem> sleepys;

private:
 Schedular() = default;
 ~Schedular() override {
  run();
 }
 friend class AwaitableSleep;

 template <typename T>
 friend class Task;

 static std::chrono::steady_clock::time_point
 current() {
  return std::chrono::steady_clock::now();
 }

 void sleep_until(std::coroutine_handle<> which,
                  std::chrono::steady_clock::time_point until_when) {
  sleepys.emplace(which, until_when);
 }

 void internal_spawn(std::coroutine_handle<> h) {
  ready_coroutines.push(h);
 }

public:
 friend class SingleInstance<Schedular>;

 template <typename T>
 void spawn(Task<T>&& task);

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
};

struct AwaitableSleep {
 AwaitableSleep(std::chrono::milliseconds how_long)
     : duration(how_long)
     , wake_time(std::chrono::steady_clock::now() + how_long) { }

 /**
  * @brief await_ready always lets the sessions sleep!
  *
  */
 bool await_ready() { return false; }
 void await_suspend(std::coroutine_handle<> h) {
  Schedular::instance().sleep_until(h, wake_time);
 }

 void await_resume() { }

private:
 std::chrono::milliseconds duration;
 std::chrono::steady_clock::time_point wake_time;
};
inline AwaitableSleep sleep(std::chrono::milliseconds s) {
 return { s };
}

#include "task.hpp"

template <typename T>
inline void Schedular::spawn(Task<T>&& task) {
 internal_spawn(task.coroutine_handle);
 task.coroutine_handle = nullptr;
}

```

> task.hpp: final Task abstraction

```cpp
#pragma once
#include "helpers.h"
#include "schedular.hpp"
#include <coroutine>
#include <utility>

template <typename T>
class Task {
public:
 friend class Schedular;
 struct promise_type;
 using coro_handle = std::coroutine_handle<promise_type>;

 Task(coro_handle h)
     : coroutine_handle(h) {
  // simple_log_with_func_name();
 }

 ~Task() {
  // simple_log_with_func_name();
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
  std::coroutine_handle<> parent_coroutine;
  Task get_return_object() {
   // simple_log_with_func_name();
   return { coro_handle::from_promise(*this) };
  }
  // we dont need suspend when first suspend
  std::suspend_always initial_suspend() {
   // simple_log_with_func_name();
   return {};
  }
  // suspend always for the Task clean ups
  std::suspend_always final_suspend() noexcept {
   // simple_log_with_func_name();
   if (parent_coroutine) {
    simple_log("parent_coroutine will be wake up");
    Schedular::instance().internal_spawn(parent_coroutine);
   }
   return {};
  }

  void return_value(T value) {
   // simple_log_with_func_name(std::format("value T {} is received!", value));
   cached_value = std::move(value);
  }

  void unhandled_exception() {
   // process notings
  }
 };

 bool await_ready() {
  // simple_log_with_func_name();
  return false; // always need suspend
 }

 void await_suspend(std::coroutine_handle<> h) {
  // simple_log_with_func_name(); // Should never be here
  simple_log("Current Routine will be suspend!");
  coroutine_handle.promise().parent_coroutine = h;
  simple_log("Child Routine will be called resume!");
  Schedular::instance().internal_spawn(coroutine_handle);
 }

 T await_resume() {
  // simple_log_with_func_name();
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
 friend class Schedular;
 struct promise_type;
 using coro_handle = std::coroutine_handle<promise_type>;

 Task(coro_handle h)
     : coroutine_handle(h) {
  // simple_log_with_func_name();
 }

 ~Task() {
  // simple_log_with_func_name();
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

 bool await_ready() {
  // simple_log_with_func_name();
  return false; // always need suspend
 }

 void await_suspend(std::coroutine_handle<> h) {
  // simple_log_with_func_name(); // Should never be here
  simple_log("Current Routine will be suspend!");
  coroutine_handle.promise().parent_coroutine = h;
  simple_log("Child Routine will be called resume!");
  Schedular::instance().internal_spawn(coroutine_handle);
 }

 void await_resume() {
  // simple_log_with_func_name();
 }

 // concept requires
 struct promise_type {
  std::coroutine_handle<> parent_coroutine;
  Task get_return_object() {
   // simple_log_with_func_name();
   return { coro_handle::from_promise(*this) };
  }
  // we need suspend when first suspend
  std::suspend_always initial_suspend() {
   // simple_log_with_func_name();
   return {};
  }
  // suspend always for the Task clean ups
  std::suspend_always final_suspend() noexcept {
   // simple_log_with_func_name();
   if (parent_coroutine) {
    Schedular::instance().internal_spawn(parent_coroutine);
   }
   return {};
  }
  void return_void() {
   // simple_log_with_func_name();
  }
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

```

The remaining `helpers.h`/`helpers.cpp` and `single_instance.hpp` have already been provided in the main text. I won't repeat them here.
