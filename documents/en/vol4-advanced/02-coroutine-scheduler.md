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
title: 'Understanding C++20''s Revolutionary Feature—Coroutines Part 2: Writing a
  Simple Coroutine Scheduler'
description: ''
translation:
  source: documents/vol4-advanced/02-coroutine-scheduler.md
  source_hash: b0a17f4e8df3445c2d5a65e633bc52764489842afedd519af7803653b3a3b411
  translated_at: '2026-06-16T04:02:10.283030+00:00'
  engine: anthropic
  token_count: 6737
---
# Understanding C++20's Revolutionary Feature — Coroutine Support Part 2: Writing a Simple Coroutine Scheduler

## Preface

In the previous blog post, we understood the simplest coroutine scheduling interface in C++20 (although it wasn't exactly simple). Clearly, before this blog post, our coroutines were still using a single-coroutine scheduler. Coroutines seem pretty useless. They can't do anything. But don't worry, to further unleash the power of coroutines, I need you to complete this simple little task. This task isn't difficult:

> - Implement a `Task` that can return a value. (Understand the `resume`/`suspend` lifecycle of `coroutine_handle`.) and use `co_await` to write a coroutine function `worker` that returns `a+b`, where the caller uses `co_await` to get the result.

If you are completely confused by the prompt above and don't know what I am talking about—you can read the calling code below first, then go back to my previous blog post to figure out how to write it. ~~(How did you know that I was also confused when I found this exercise?)~~

```cpp
int main() {
    auto add = [](int a, int b) -> Task<int> {
        co_return co_await worker(a, b);
    };

    auto result = add(1, 2);
    Scheduler::instance().spawn(result);

    Scheduler::instance().run();
    std::cout << "Result from coroutine: " << co_await result << std::endl;
    return 0;
}
```

All you need to do is make the code above run. The way to run it is to implement `Task<int>`. If you have done so, please refer to the code below to compare your implementation. We will reuse `Task<int>` later to complete the theme of this blog post—a scheduler with return value support.

Here is my code. `coroutine_handle` was already given in the previous blog post and has not changed, so feel free to use it.

```cpp
// task.hpp
#pragma once
#include <coroutine>
#include <optional>
#include <iostream>
#include "helpers.hpp"

template<typename T>
struct Task {
    struct promise_type {
        T value;
        std::exception_ptr exception;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(T val) {
            value = val;
        }

        void unhandled_exception() {
            exception = std::current_exception();
        }
    };

    std::coroutine_handle<promise_type> handle;
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}

    ~Task() {
        if (handle) handle.destroy();
    }

    // Simple awaiter implementation
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> awaiting_handle) {
        // In a real scheduler, we would push the awaiting_handle to the ready queue
        // For now, we just resume it immediately to demonstrate the concept
        awaiting_handle.resume();
    }
    T await_resume() { return handle.promise().value; }
};
```

If you didn't understand what happened, please continue reading the content below. If your implementation is similar, you can scroll back up and continue writing the scheduler.

## Implementing a Simplest Scheduler

We are now going to implement a simplest scheduler. Here are our requirements:

> - Write a singleton **single-threaded scheduler** (event loop) that can schedule multiple `Task`s. (It is recommended to write a singleton template for practice; besides, the basic code for Task has been completed in the previous task.)
> - Implement a `SleepAwaiter` awaiter.
> - Test if it works—write 3 coroutines running concurrently: print "A", "B", "C", alternating output.

### Step 1 — Implement a Singleton Template

I decided to implement a simple singleton template to facilitate reuse in our other projects. Regarding the discussion of the singleton pattern, although Dependency Injection (DI) is more appropriate, we will still write a `static`-based singleton template (coroutines are only available in C++20, and since C++11, the initialization of static variables has been guaranteed to be thread-safe).

> single_instance.hpp

```cpp
#ifndef SINGLE_INSTANCE_HPP
#define SINGLE_INSTANCE_HPP

template<typename T>
class SingleInstance {
protected:
    SingleInstance() = default;
    virtual ~SingleInstance() = default;

public:
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    static T& instance() {
        static T instance;
        return instance;
    }
};

#endif // SINGLE_INSTANCE_HPP
```

Obviously, we disabled any form of copying and construction. Also, for convenience in later use, we will adopt a safe virtual destructor. The constructor should be placed in the protected domain so that our singleton subclasses can access it, ensuring we syntactically avoid the creation of a second instance. In terms of usage, we just need to write:

```cpp
class MyScheduler : public SingleInstance<MyScheduler> {
    // ...
};

// Usage
auto& sched = MyScheduler::instance();
```

> Coincidentally, I have written an exploration of the singleton pattern, implemented in C++20 as well. Refer to the blog:
>
> - [CSDN: Deep Dive into C++20 Design Patterns — Creational Patterns: Singleton Pattern - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/152166469)
> - [charliechen114514.tech: Deep Dive into C++20 Design Patterns — Creational Patterns: Singleton Pattern](https://www.charliechen114514.tech/archives/chuang-zao-xing-she-ji-mo-shi-dan-li-mo-shi)

### Step 2: Preliminary Modification of Our `Task`, Letting the Scheduler Take Over Our Coroutines

Obviously—we have now decided to use a scheduler to schedule our coroutines—so any suspension operation needs to be controlled by us, rather than the returned struct deciding for itself. To this end, our initialization also needs to be suspended immediately:

```cpp
std::suspend_always initial_suspend() { return {}; }
```

This applies to both the generic implementation and the partial specialization implementation.

### Step 3: Think About the Scheduler Supported Interface

We are now ready to think about the scheduler's interface. Fortunately, our coroutines are not preemptively scheduled, so the code is very easy to write (but "easy" is unlikely). We just need to follow FIFO scheduling when there is no yielding.

First, the scheduler needs to support a `Sleep` call, which means letting the current coroutine sleep (do other coroutines if there are any; if not, it means the current thread needs to be idle, so call the `std::this_thread::sleep_for` interface).

Therefore, we need to let the scheduler know which coroutines need to sleep—the scheduler needs a container to manage who needs to sleep, and a push interface to designate a specific coroutine for sleeping.

One thing to know—for convenience, the standard library has an interface called `std::chrono::sleep_until`. So, to facilitate management and reuse of standard library interfaces, we design a `sleep_until` interface for the scheduler—it indicates that we want to sleep until a specified time point before being ready to be scheduled (again, note that coroutine scheduling is cooperative; we can only guarantee the lower bound of the sleep event).

```cpp
void sleep_until(std::coroutine_handle<> handle, std::chrono::steady_clock::time_point wake_time);
```

Additionally, we need a push interface: a `spawn` interface, used to accept the coroutine return struct returned by the coroutine function. All scheduling of this struct must be taken over by the scheduler. So, don't forget to declare the scheduler class as a friend in the Task.

```cpp
template<typename T>
void spawn(Task<T> task);
```

Finally, there is a scheduling interface—the `run` interface.

```cpp
void run();
```

It will start our coroutine scheduling. Just three!

### Step 4: Implement the Above Interfaces

#### Implement the `spawn` Interface to Host the Coroutine Return Struct Returned by the Coroutine Function

Let's start with scheduling itself. First, we need to cache the coroutine interfaces in the ready queue (note that it is not the `Task` itself; we are scheduling coroutines, not the coroutine return structs). As mentioned above, our scheduling policy is FIFO, so first-come-first-served requires us to use a queue to handle our storage.

```cpp
std::queue<std::coroutine_handle<>> ready_queue;
```

So, our `spawn` interface becomes very easy to implement—

```cpp
template<typename T>
void spawn(Task<T> task) {
    if (task.handle) {
        ready_queue.push(task.handle);
    }
}
```

#### Implement the Sleep Mechanism

Sleeping requires us to register how long we sleep, who is sleeping, and also sort by a certain priority (think about it: if there are three sleep requests for 100ms, 200ms, and 300ms, the 100ms one should obviously sleep first, then 200ms, then 300ms; otherwise, the first two will be long done). Obviously, we immediately thought of a priority queue. However, the priority queue needs to provide a comparison method to produce a min/max heap. So we need to abstract a `SleepEvent` struct—it registers that our root is the smallest sleep event. Or rather, the one closest to the current time point.

```cpp
struct SleepEvent {
    std::coroutine_handle<> handle;
    std::chrono::steady_clock::time_point wake_time;

    bool operator<(const SleepEvent& other) const {
        return wake_time > other.wake_time; // Min-heap based on wake_time
    }
};

std::priority_queue<SleepEvent> sleep_queue;
```

But we haven't implemented the user-side code yet. Users expect to be able to sleep like this:

```cpp
co_await sleep_for(std::chrono::milliseconds(100));
```

Eh, how do we say that? Seeing `co_await` should trigger a reflex to implement the awaitable interface. So—

```cpp
struct SleepAwaiter {
    std::chrono::milliseconds duration;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
        Scheduler::instance().sleep_until(handle, std::chrono::steady_clock::now() + duration);
    }
    void await_resume() {}
};
```

#### Implement Scheduling Logic

First, sleeping is only done when there is nothing to do, so the priority of implementation is obvious—prioritize processing active coroutines!

```cpp
void run() {
    while (!ready_queue.empty() || !sleep_queue.empty()) {
        // 1. Process ready coroutines
        while (!ready_queue.empty()) {
            auto handle = ready_queue.front();
            ready_queue.pop();
            if (handle && !handle.done()) {
                handle.resume();
            }
        }

        // 2. Check sleep queue
        // ...
    }
}
```

Only when we have finished executing all active code will we check if there are any guys in the sleep queue waiting to be woken up—

```cpp
auto now = std::chrono::steady_clock::now();
while (!sleep_queue.empty() && sleep_queue.top().wake_time <= now) {
    auto event = sleep_queue.top();
    sleep_queue.pop();
    if (event.handle && !event.handle.done()) {
        ready_queue.push(event.handle);
    }
}
```

Excellent. If our current time has passed the specified sleep wake-up time (i.e., `sleepys.top().sleep`), we need to send all coroutines that have passed the time point to our ready queue.

Next, if we still have coroutines that need to sleep, and no new ready queue arrives, we immediately put the current thread to sleep.

```cpp
if (!ready_queue.empty()) continue; // New tasks arrived while checking

if (!sleep_queue.empty()) {
    auto next_wake = sleep_queue.top().wake_time;
    std::this_thread::sleep_until(next_wake);
}
```

#### Continue Modifying the Task Interface

Now the task needs to push directly to the queue. We need to think about these issues. We will use the scheduler like this:

```cpp
auto task = worker(1, 2);
Scheduler::instance().spawn(task);
```

All parent coroutines will yield their own execution. Following C++20 stackless coroutine logic—we must save the coroutine handle ourselves. So it is easy to think of—`Task` itself needs to store the parent coroutine's handle, so that when our child coroutine resumes, it can resume the parent coroutine's execution and continue the code.

Maybe this is too big a jump. Let's go slowly one by one—when our parent coroutine writes the code—`co_await worker(1, 2)`, the parent coroutine must give up its own execution and wait for the result from `worker`. At this time, we recall the execution logic of our coroutine framework from the first blog post: go to `await_ready` to see if it suspends—we obviously returned no, so we need to take over the logic ourselves. So the next step of the execution flow is forwarded to `await_suspend`. This step is what we want—the parent coroutine needs to be suspended, so the child coroutine needs to be pushed!

```cpp
void await_suspend(std::coroutine_handle<> parent) {
    child_handle.promise().parent_handle = parent;
    Scheduler::instance().spawn(child_handle);
}
```

`await_suspend` sets the parent coroutine of the child coroutine to the current thread, and then puts the child coroutine into the ready queue. Nothing wrong! (Note that this code is in the child coroutine return struct).

Now, our child coroutine has been sent to the ready queue. And excitingly—it will be sent to the ready processing logic. When our scheduler executes the ready coroutine queue code, we will execute this logic—

```cpp
while (!ready_queue.empty()) {
    auto handle = ready_queue.front();
    ready_queue.pop();
    if (handle && !handle.done()) {
        handle.resume();
    }
}
```

The child coroutine is resumed here, executing the code from `worker`—the child coroutine is now suspended. When `worker` finishes execution, we still follow the process—calling `final_suspend`. Remember the `parent_coroutine` we stored? It comes into play here—the end of the child coroutine requires the parent coroutine to put down the execution code. So things become very easy:

```cpp
std::suspend_always final_suspend() noexcept {
    if (parent_handle && !parent_handle.done()) {
        Scheduler::instance().spawn(parent_handle);
    }
    return {};
}
```

Once we get here, all our code is completed. Let's compile and run it:

```text
A
B
C
A
B
C
...
```

The code works perfectly. How was the log above generated? The answer is as follows:

```cpp
Scheduler::instance().spawn(print_a(10));
Scheduler::instance().spawn(print_b(10));
Scheduler::instance().spawn(print_c(10));
Scheduler::instance().run();
```

# Appendix: Implementing the Coroutine Addition Function `worker`

To save you from flipping back and forth, I will just copy and paste the code here.

```cpp
Task<int> worker(int a, int b) {
    co_return a + b;
}
```

First, the previous blog post mentioned that any function running in a coroutine must return a **coroutine return type**. This requires you to unconditionally embed a struct `promise_type`, and requires you to implement the interface—

```cpp
struct promise_type {
    T value;
    Task get_return_object() { /* ... */ }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_value(T val) { value = val; }
    void unhandled_exception() { /* ... */ }
};
```

In this example, it is not difficult to understand that `worker` does not need to suspend upon creation, so we just need to return `std::suspend_never` in `initial_suspend`, allowing us to immediately execute the returned result on `worker`. After `a+b` is calculated, it will be sent to the `promise_type`. It is worth noting—in the previous blog post, we already discussed whose lifetime is longer between the return type and the coroutine handle itself. This is also why we choose to suspend, so that the upper-level `Task` is responsible for destroying the coroutine object, rather than it solving it itself. You won't be unfamiliar with this structure; the previous blog post has already explained what this structure is doing.

`co_await` requires waiting for `Task<int>`, so any non-empty `Task` must also implement the Awaitable interface (Note that it is not that every return struct with a `PromiseType` interface needs to implement the Awaitable interface, but rather that we need to implement the Awaitable interface when we need to `co_await` this interface. Please make sure you understand the logical relationship.)

```cpp
bool await_ready() { return false; } // Need to suspend to handle logic
void await_suspend(std::coroutine_handle<> awaiting_handle) {
    // Push the parent (awaiting_handle) back to the scheduler
    Scheduler::instance().spawn(awaiting_handle);
}
T await_resume() { return handle.promise().value; }
```

Although logically, we actually don't need a suspend interface, our result is stored in the `promise_type` of the `coroutine_handle`. At this point—we **need to take over the waiting logic, so we still need to suspend**.

> `await_ready` can actually be expressed as—we need to take over the waiting logic and do our own processing.
>
> The first blog post is at:
>
> - CSDN Link: [CSDN](https://blog.csdn.net/charlie114514191/article/details/152518557)
> - My Blog Link: [charliechen114514.tech](https://www.charliechen114514.tech/archives/li-jie-c-20de-ge-ming-te-xing----xie-cheng-zhi-chi-1)

# Appendix 2: Scheduler Code

> schedular.cpp: Main example code

```cpp
#include "scheduler.hpp"
#include <iostream>

Task<void> print_a(int count) {
    for (int i = 0; i < count; ++i) {
        std::cout << "A" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(100));
    }
}

Task<void> print_b(int count) {
    for (int i = 0; i < count; ++i) {
        std::cout << "B" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(100));
    }
}

Task<void> print_c(int count) {
    for (int i = 0; i < count; ++i) {
        std::cout << "C" << std::endl;
        co_await sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    Scheduler::instance().spawn(print_a(10));
    Scheduler::instance().spawn(print_b(10));
    Scheduler::instance().spawn(print_c(10));

    Scheduler::instance().run();

    return 0;
}
```

> schedular.hpp: Scheduler code

```cpp
#pragma once
#include <coroutine>
#include <queue>
#include <chrono>
#include <thread>
#include <algorithm>
#include "single_instance.hpp"

class Scheduler : public SingleInstance<Scheduler> {
    friend class SingleInstance<Scheduler>;
private:
    struct SleepEvent {
        std::coroutine_handle<> handle;
        std::chrono::steady_clock::time_point wake_time;

        bool operator<(const SleepEvent& other) const {
            return wake_time > other.wake_time;
        }
    };

    std::queue<std::coroutine_handle<>> ready_queue;
    std::priority_queue<SleepEvent> sleep_queue;

    Scheduler() = default;

public:
    ~Scheduler() override = default;

    template<typename T>
    void spawn(Task<T> task) {
        if (task.handle) {
            ready_queue.push(task.handle);
        }
    }

    void sleep_until(std::coroutine_handle<> handle, std::chrono::steady_clock::time_point wake_time) {
        sleep_queue.push({handle, wake_time});
    }

    void run() {
        while (!ready_queue.empty() || !sleep_queue.empty()) {
            // 1. Process ready queue
            while (!ready_queue.empty()) {
                auto handle = ready_queue.front();
                ready_queue.pop();
                if (handle && !handle.done()) {
                    handle.resume();
                }
            }

            // 2. Check sleep queue
            auto now = std::chrono::steady_clock::now();
            while (!sleep_queue.empty() && sleep_queue.top().wake_time <= now) {
                auto event = sleep_queue.top();
                sleep_queue.pop();
                if (event.handle && !event.handle.done()) {
                    ready_queue.push(event.handle);
                }
            }

            if (!ready_queue.empty()) continue;

            // 3. Sleep if nothing to do
            if (!sleep_queue.empty()) {
                std::this_thread::sleep_until(sleep_queue.top().wake_time);
            }
        }
    }
};
```

> task.hpp: Final abstraction of Task

```cpp
#pragma once
#include <coroutine>
#include <optional>
#include <iostream>
#include "helpers.hpp"

template<typename T>
struct Task {
    struct promise_type {
        T value;
        std::exception_ptr exception;
        std::coroutine_handle<> parent_handle; // Handle to the awaiting coroutine

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() { return {}; } // Changed to suspend_always
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(T val) {
            value = val;
        }

        void unhandled_exception() {
            exception = std::current_exception();
        }
    };

    std::coroutine_handle<promise_type> handle;
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}

    ~Task() {
        if (handle) handle.destroy();
    }

    // Awaiter implementation
    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> awaiting_handle) {
        // Store the parent (awaiter) in the child's promise
        handle.promise().parent_handle = awaiting_handle;
        // Schedule the child (current task)
        Scheduler::instance().spawn(*this);
    }

    T await_resume() {
        if (handle.promise().exception) {
            std::rethrow_exception(handle.promise().exception);
        }
        return handle.promise().value;
    }
};
```

The remaining `helpers.h/helpers.cpp` and `single_instance.hpp` have already been provided in the main text. I will not repeat them.
