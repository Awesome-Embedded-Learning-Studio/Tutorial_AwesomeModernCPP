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
title: "Understanding C++20's Revolutionary Feature — Coroutines, Part 2: Writing a Simple Coroutine Scheduler"
description: ''
translation:
  source: documents/vol4-advanced/02-coroutine-scheduler.md
  source_hash: b0a17f4e8df3445c2d5a65e633bc52764489842afedd519af7803653b3a3b411
  translated_at: '2026-09-26T02:53:04+00:00'
  engine: anthropic
  token_count: 8000
---
# Understanding C++20's Revolutionary Feature — Coroutines, Part 2: Writing a Simple Coroutine Scheduler

## Preface

In the previous post, we came to grips with the simplest coroutine scheduling interface C++20 offers (and even that was far from simple). Clearly, everything before this post still had our coroutines running under what amounted to a single-coroutine scheduler. Coroutines look pretty lame — they can't do anything. But don't worry: so that we can push further and truly unleash the power of coroutines, I need you to get hands-on with this simple little task. It is not difficult:

> - Implement a `Task<T>` that can be `co_await`ed for a return value. (Understand the resume/suspend lifecycle of `coroutine_handle`.) Then use `Task<int>` to write a coroutine function `co_add(a, b)` that returns a + b, with the caller using `co_await` to obtain the result.

If the exercise above leaves you completely lost and you have no idea what I am talking about — you can read the calling code below first, then go back to my previous post and puzzle over how to write it. ~~(How did you know I was just as lost when I first found this exercise?)~~

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

All you need to do is get the code above running, and the way to get it running is to implement `Task<T>`. Once you have it done, please compare your implementation against the code below. Later on we will reuse `Task<T>` to build the topic of this post — a scheduler with return-value support.

Here is my code. `"helpers.h"` was already given in the previous post and has not changed one bit, so use it with confidence.

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

If you did not understand what happened, keep reading below. If your implementation is more or less the same as mine, you can scroll back up and get on with writing the scheduler.

## Implementing the Simplest Possible Scheduler

We are about to implement the simplest possible scheduler. Here are our requirements:

> - Write a singleton **single-threaded scheduler** (an event loop) that can schedule multiple `Task`s. (Writing a singleton template makes for good practice; besides, the basic Task code was already finished in the previous task)
> - Implement a `sleep(ms)` awaiter
> - Check whether it actually works — write 3 coroutines running concurrently: they print "A", "B", "C", alternating.

#### Step 1 — Implementing a Singleton Template

I decided to implement a simple singleton template, to make reuse in our other projects convenient. On the singleton pattern: even though dependency injection (DI) would be the more appropriate choice, we will still write a `static`-based singleton template (coroutines only arrived with C++20, and C++11 onward already guarantees that static initialization is thread-safe).

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

Clearly, we have disabled every form of copying and construction, and for convenience in later use we adopt a safe virtual destructor. `SingleInstance()` goes under the protected section so our singleton subclasses can reach it, which is what rules out — right at the syntax level — creating a second instance. In use, we only need to write:

```cpp
class Schedular : public SingleInstance<Schedular>
{
    Schedular() = default; // still hiding our constructor away
public:
 friend class SingleInstance<Schedular>;
}

```

> As it happens, I have written a discussion of the singleton pattern before, also implemented in C++20. See the blog posts:
>
> - [CSDN: A Close Reading of C++20 Design Patterns — Creational Design Patterns: The Singleton Pattern - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/152166469)
> - [charliechen114514.tech: A Close Reading of C++20 Design Patterns — Creational Design Patterns: The Singleton Pattern](https://www.charliechen114514.tech/archives/chuang-zao-xing-she-ji-mo-shi-dan-li-mo-shi)

#### Step 2: A First Modification of Our `Task`, Giving the Scheduler a Chance to Take Over Our Coroutines

Clearly — we have now decided to drive our coroutines with a scheduler — which means every suspension has to be under our control rather than adjudicated by the return object itself. For that, our initialization needs to be suspended immediately:

```cpp
  // we need suspend when first suspend
  std::suspend_always initial_suspend() {
   // simple_log_with_func_name();
   return {};
  }

```

The same goes for the generic implementation and for the partial specialization.

#### Step 3: Thinking About the Interfaces the Scheduler Supports

We are now ready to think about the scheduler's interfaces. Happily, our coroutines are not preemptively scheduled, so the code is very easy to write (though "easy" may be optimistic) — all we need is to follow FIFO scheduling when nothing yields.

First, the scheduler needs to support a Sleep call — that is, letting the current coroutine go have a proper nap (if there are other coroutine tasks, it works on those; if there are none, that tells us the current thread should idle, and calling one of the `std::this_thread::sleep_*` interfaces does the job).

So we need the scheduler to know which coroutines want to nap — the scheduler needs a container managing who needs to sleep, plus a push for the specific coroutine that wants to sleep.

One thing to know — for convenience, the standard library does have an interface called `sleep_until`. So, to make management easy and to reuse a standard library interface, we design a `sleep_until` interface for our scheduler — it declares that we sleep until a specified time point, at which point we are ready to be scheduled again (and let me stress it once more: coroutine scheduling is cooperative here, so all we can guarantee is a lower bound on how long the sleep lasts).

```cpp
void Schedular::sleep_until(std::coroutine_handle<> which, // who needs to sleep?
                   std::chrono::steady_clock::time_point until_when);

```

On top of that, we need a push interface: the spawn interface, which accepts the return object a coroutine function produces. All scheduling of that object must be taken over by the scheduler. So don't forget to declare the scheduler class as a friend over at the Task.

```cpp
 template <typename T>
 void Schedular::spawn(Task<T>&& task); // Task is move-only, so that is what this interface takes

```

And finally there is one scheduling interface — the run interface

```cpp
void Schedular::run();

```

It will kick off our coroutine scheduling. Just three of them!

#### Step 4: Implementing the Interfaces Above

##### Implementing the spawn Interface, Taking Custody of the Coroutine Return Objects Returned by Coroutine Functions

We start with scheduling itself. First, we need to hold on to the coroutine handles lined up as ready (note: not the `Task` itself — we are scheduling coroutines, not coroutine return objects). As mentioned above, our scheduling policy is FIFO, so first-come-first-served calls for a queue to handle the storage.

```cpp
std::queue<std::coroutine_handle<>> ready_coroutines; // a simple queue is all we need

```

With that, our spawn interface becomes very easy to implement —

```cpp
void Schedular::internal_spawn(std::coroutine_handle<> h) {
    // a private implementation; users should not poke the scheduling queues directly
 ready_coroutines.push(h); // add it to the scheduling queue
}

// spawn is a bridging interface: we take out the coroutine_handle managed inside
// the Task and hand it over to our scheduler to manage
template <typename T>
inline void Schedular::spawn(Task<T>&& task) {
 internal_spawn(task.coroutine_handle);
 task.coroutine_handle = nullptr; // the Task no longer manages the coroutine_handle itself
}

```

##### Implementing the Sleep Mechanism

Sleeping needs to register how long we sleep and who is sleeping, and the records have to be kept sorted by some priority (think: given three sleep requests of 100 ms, 200 ms, and 300 ms, clearly the 100 ms sleeper gets priority, then the 200 ms one, then the 300 ms one — do it the other way around and the first two would be stone cold by wake-up time). The answer that springs to mind at once is a priority queue. But a priority queue needs a comparison method to produce a min/max heap. So we need to abstract a `SleepItem` struct — one that keeps our heap root at the smallest sleep time; or in other words, the one closest to the current time point.

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

But we have not implemented the user-side code yet, and users expect to be able to sleep like this:

```cpp
co_await sleep(300ms);

```

Well, how to put it? The moment you see `co_await`, your conditioned reflex should be to implement the awaitable interface. So —

```cpp
struct AwaitableSleep {
 AwaitableSleep(std::chrono::milliseconds how_long)
     : duration(how_long)
     , wake_time(std::chrono::steady_clock::now() + how_long) { }

 /**
  * @brief await_ready always lets the sessions sleep!
  *
  */
 bool await_ready() { return false; } // we always take over the rest of the flow
 void await_suspend(std::coroutine_handle<> h) {
         // do the push; later our own scheduler takes this handle out and throws it into the ready queue
  Schedular::instance().sleep_until(h, wake_time);
 }

 // do nothing
 void await_resume() { }

private:
 std::chrono::milliseconds duration; // handy for getters or debugging; kick it out if performance comes first
 std::chrono::steady_clock::time_point wake_time;
};

inline AwaitableSleep sleep(std::chrono::milliseconds s) {
 return { s };
}

```

##### Implementing the Scheduling Logic

First, sleeping is only for when there is nothing left to do — the implementation priority is plain as day: favor the active coroutines!

```cpp
 void run() {
  // if there is any corotines ready or sleepy unfinished
  while (!ready_coroutines.empty() || !sleepys.empty()) {
            // entering this branch means we do have something to do right now — napping, or pulling a coroutine up.
   while (!ready_coroutines.empty()) {
    auto front_one = ready_coroutines.front();
    ready_coroutines.pop();
    front_one.resume(); // OK, hang this on!
   }

            ...
  }
 }

```

Only once every bit of active code has finished executing do we go check whether there is anyone in the sleep queue waiting to be woken —

```cpp
   auto now = current(); // current returns std::chrono::steady_clock::now()
   while (!sleepys.empty() && sleepys.top().sleep <= now) {
    ready_coroutines.push(sleepys.top().coro_handle);
    sleepys.pop();
   }

```

Very good: if our current time has passed the designated sleep wake-up time point (that is, `sleepys.top().sleep`), we send every coroutine whose time has passed into our ready queue.

Next, if we still have coroutines that need to sleep and no new ready queue has arrived, we immediately put this very thread to sleep

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

Now Tasks need to push straight into the queue, and we need to think through a few things. We will use the scheduler like this:

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

Every parent coroutine will lay down its own execution, and per the logic of C++20 stackless coroutines — we have to save the coroutine handles ourselves. So it is easy to realize: the Task itself must store the parent coroutine's handle, so that when our child coroutine finishes, we can bring the parent coroutine's execution back and the code can go on.

That may be too big a jump, so let's take it one step at a time — when, inside our parent coroutine, we write `co_await worker("TaskA", 1, 2);`, the parent coroutine has to give up its own execution and wait for worker's result. At this point, we recall how our coroutine framework runs, from the first post: it goes through `await_ready` to check whether to suspend — and we clearly returned false, because we want to take over the logic ourselves. So the next step of the execution flow is forwarded into `await_suspend`, and that step is exactly the one we want — the parent coroutine is to be suspended, so the child coroutine is to be pushed!

```cpp
 // in the coroutine return object of the child coroutine being created
 void await_suspend(std::coroutine_handle<> h) {
  // simple_log_with_func_name(); // Should never be here
  simple_log("Current Routine will be suspend!");
  coroutine_handle.promise().parent_coroutine = h;
  simple_log("Child Routine will be called resume!");
  Schedular::instance().internal_spawn(coroutine_handle);
 }

```

`coroutine_handle.promise().parent_coroutine = h;` sets the child coroutine's parent to the currently running coroutine, and then puts the child coroutine into the ready queue. Nothing wrong with that! (Note that this code lives in the child coroutine's return object.)

Now our child coroutine has been sent into the ready queue — and the exciting part is that it lands right in the ready-handling logic. When our scheduler executes the ready-coroutine-queue code, this is the logic that runs —

```cpp
   while (!ready_coroutines.empty()) {
    auto front_one = ready_coroutines.front();
    ready_coroutines.pop();
    front_one.resume(); // OK, hang this on!
   }

```

The child coroutine is resumed here, and what runs is worker's code — until the child coroutine in turn gets suspended. When worker finishes executing, we still follow the procedure — what gets called is `final_suspend`. Remember the parent_coroutine we stored? This is where it pulls its weight — the child coroutine finishing calls for the parent coroutine to take execution back. So things become very easy:

```cpp
  std::suspend_always final_suspend() noexcept {
   // simple_log_with_func_name();
   if (parent_coroutine) {
    simple_log("parent_coroutine will be wake up");
                 // pull the parent coroutine back up to run its code
    Schedular::instance().internal_spawn(parent_coroutine);
   }
   return {}; // the child coroutine is owned by the Task struct; this logic never changes
  }

```

With that in place, all of our code is done. Let's compile and run it:

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

The code works perfectly. How was the log above produced? The answer is below:

```cpp

[charliechen@Charliechen coroutines]$ build/schedular/schedular
10:36:12 :Current Routine will be suspend! // main_task is about to be suspended
10:36:12 :Child Routine will be called resume! // worker("TaskA", 1, 2) is about to get to work
10:36:12 :Current Routine will be suspend! // worker("TaskA", 1, 2) is about to be suspended
10:36:12 :Child Routine will be called resume! // co_add is about to get to work
10:36:13 :parent_coroutine will be wake up // co_add, as the leaf coroutine, is about to end itself and pull its parent worker back up
TaskA: 1 + 2 = 3 // worker is pulled back up and runs the printing

// the logic below is analogous
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

To spare you flipping back and forth, I will simply paste a copy of the code here as well.

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

First, we already mentioned this in the previous post — any function that runs as a coroutine must return a **coroutine return type**, which — no negotiation allowed — means embedding a `struct promise_type`, and it requires you to implement the interfaces —

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

In this post's example, it is not hard to see that `co_add` does not need to suspend the moment it is created, so we can simply return `std::suspend_never` and let ourselves run straight onto the returned result, `co_return a + b`. Once `a + b` has been computed, it gets handed to `return_value`. Note that — as the previous post already discussed — we settled who outlives whom between the return type and the coroutine handle itself; that is also why we choose to suspend, letting the `Task` one level up be responsible for destroying the coroutine object instead of the coroutine settling itself. This structure should be nothing new to you: the previous post already explained what it is doing.

`co_await` wants to wait on a `Task<int>`, so any non-void `Task` must also implement the Awaitable interface (note: it is not that every return object carrying a PromiseType interface has to implement the Awaitable interface — it is that we implement the Awaitable interface precisely when we need to `co_await` such an object. Please keep that logical relationship straight.)

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

Although, logically speaking, we do not actually need the suspension, our result is stored inside the promise_type of the coroutine_handle — so at this point, **we need to take over the waiting logic, and that means suspending after all**.

> await_ready can also be read as: we need to take over the waiting logic and process it our own way
>
> The first post is at:
>
> - CSDN link: [CSDN](https://blog.csdn.net/charlie114514191/article/details/152518557)
> - Link to my own blog: [charliechen114514.tech](https://www.charliechen114514.tech/archives/li-jie-c-20de-ge-ming-te-xing----xie-cheng-zhi-chi-1)

# Appendix 2: The Scheduler's Code

> schedular.cpp: the main code of the example

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

> schedular.hpp: the scheduler code

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

> task.hpp: the final abstraction of Task

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

The remaining helpers.h/helpers.cpp and single_instance.hpp were already given in the main text. I won't repeat them.
