---
chapter: 10
difficulty: intermediate
order: 8
platform: host
reading_time_minutes: 23
tags:
- cpp-modern
- host
- intermediate
title: Understanding C++20's Revolutionary Feature — Coroutines, Part 1
description: ''
translation:
  source: documents/vol4-advanced/01-coroutine-basics.md
  source_hash: 55eb7d50ac6a17b098145298906ffc9b8da0ccd919ae11ac801313630a7c3584
  translated_at: '2026-09-26T02:59:44+00:00'
  engine: anthropic
  token_count: 7000
---
# Understanding C++20's Revolutionary Feature — Coroutines, Part 1

## What Is a Coroutine

To work our way up to coroutines, we can't avoid mentioning the runtime stack of functions: when a function is called, the runtime allocates a **stack frame** for it, and that frame holds the arguments, the return address, and the local variables declared inside the function — this is the function's runtime environment.

The core idea of a coroutine is: **a function can suspend partway through its execution and yield control (`yield`); when the conditions are met, it resumes (`resume`) and continues from where it left off**. This lets us implement lightweight cooperative scheduling in user space: different tasks switch in an orderly fashion under the program's control, rather than relying on the preemptive scheduling of OS threads.

Of course, one thing we should make clear — by implementation approach,

coroutines come in two flavors: **stackful coroutines** switch a complete execution stack; **C++20's coroutines belong to the "stackless" paradigm** — the compiler packages the local variables and state that must survive a suspension point into a **coroutine frame**. On suspension, that frame is saved and control returns; on resumption, the state is restored from the frame and execution continues. Since no OS stack gets switched, and we usually don't need frequent trips into kernel mode either, for extreme concurrency scenarios this thing beats process/thread switching by a huge margin.

We usually reach for coroutines for three big reasons:

- **Writing asynchronous code in a synchronous style**: tangled callback chains can be replaced by linear, sequential code — the logic becomes more intuitive and easier to read.
- **High concurrency, low overhead**: compared with threads, creating and switching coroutines is far cheaper, which makes them a good fit for large numbers of I/O-intensive concurrent tasks.
- **More flexible control-flow expression**: coroutines are a natural fit for generators, pipelines, lazy evaluation, asynchronous task chains, and similar patterns.

## What C++'s Coroutine Support Looks Like

This is a C++ blog, so a discussion of C++'s coroutine support is unavoidable. But unfortunately, I must stress — the C++20 coroutine interface is genuinely hard to write. I have browsed quite a few forums and seen other people's introductions to C++20 coroutines, and I have to admit — if we don't understand coroutines to begin with, this set of interfaces is truly hard to grasp (I struggled with it for a good while myself). So my strong suggestion: as you read this blog, practice the code and print some logs. It helps you understand what C++ coroutines are actually doing.

To expand on the above, I decided to reorganize `cppreference`'s introduction to coroutines.

> I know some friends haven't yet looked at what coroutines are in C++. You can go read `cppreference`'s account of this interface first — my first time through, I closed it halfway and went off to write something else; it really is a bit hard to digest! 👉[Coroutines (C++20) - cppreference.cn - C++ Reference Manual](https://cppreference.cn/w/cpp/language/coroutines)

Boiled down — this is the content we need to understand, so keep it handy as a note. Or, if you'd rather not read it, skip to the next section and glance at the example, and one skim will tell you roughly how to use the coroutines C++20 supports.

- The three extension keywords the compiler provides are the first thing to know:

  - `co_await`: this keyword suspends the coroutine until we **call the resumption mechanism to set it back down!** One thing to note — our `co_await` must be followed by an expression. That expression is typically **an object supporting several of the coroutine interfaces C++ prescribes** (that is at least how I use them today; C++ coroutine experts have cooked up all sorts of fancy tricks, and they look genuinely baffling, so let's just put it this way to keep things digestible for beginners). In plain words: the thing being awaited must implement functions with the given signatures — if it doesn't, the compiler will tell you the interface is missing!
  - `co_yield`: pauses execution and produces a value. What does that mean? Sitting inside our coroutine function, it hands out the value of the expression that `co_yield` marks, and that value has to be given back through an interface. Don't rush for the specifics — we'll get to them later.
  - `co_return`: completes execution and returns a value. The moment we write a `co_return`, this coroutine function is finished, and we get ready to destroy our coroutine structure.

- The other part is a struct that the coroutine function has to return (the **coroutine return type**). This struct is used to give the coroutine framework some scheduling information. In practice, our modern C++ uses interfaces to say whether coroutines are supported, so what we need to do is declare an object type, **and it must nest a `promise_type` inside — note that exact name, it cannot change!**

  > ```cpp
  > // in <coroutine>
  > #if __cpp_concepts
  >     requires requires { typename _Result::promise_type; }
  >     struct __coroutine_traits_impl<_Result, void>
  > #else
  >     struct __coroutine_traits_impl<_Result,
  >        __void_t<typename _Result::promise_type>>
  > #endif
  >     {
  >       using promise_type = typename _Result::promise_type;
  >     };
  > ```

  The next step is to declare and implement the interfaces that must exist inside this `promise_type`. This is what we need to implement —

  | Interface (Function) | Purpose | Return Type Requirement |
  | -------------------- | ------- | ----------------------- |
  | **1. `get_return_object()`** | **Get the return object**: the first function executed when the coroutine function is called. It is responsible for creating and returning the **return object** (such as your `Generator`) that the caller (the outside world) uses to drive the coroutine. | Must return the coroutine function's return type (or a type convertible to it). |
  | **2. `initial_suspend()`** | **Initial suspend point**: decides whether the coroutine **runs immediately** upon creation or is **suspended**. | Must return an **Awaitable** object (such as `std::suspend_always` or `std::suspend_never`). |
  | **3. `final_suspend()`** | **Final suspend point**: decides whether the coroutine is **destroyed immediately** or **suspended** after execution finishes (`co_return` or the end of the function body). | Must return an **Awaitable** object. |
  | **4. `return_void()` or `return_value(V)`** | **Return-value handling**: used to handle the coroutine's **final value** or **final state**. | If the coroutine function returns `void` (as `Generator` often does), you must provide `return_void()`. If the coroutine returns a value via `co_return V;`, you must provide `return_value(V)`. You provide **one or the other**. |
  | **5. `unhandled_exception()`** | **Exception handling**: called when an **uncaught exception** occurs inside the coroutine. | Must return `void`. |

  Also worth mentioning: if your coroutine function uses the `co_yield` keyword, there is one more function you need to sort out —

  | Interface (Function) | Purpose | Return Type Requirement |
  | -------------------- | ------- | ----------------------- |
  | **`yield_value(T value)`** | **Produce a value**: called when the coroutine executes `co_yield T;`. It is responsible for storing the produced value and suspending the coroutine. | Must return an **Awaitable** object (usually `std::suspend_always`). |

- One more part deserves attention — as you can see, we sometimes require returning `std::suspend_always` or `std::suspend_never`. That expresses whether we want to suspend the coroutine at all, but this interface is not necessarily coupled with `promise_type` — it is independent of our `promise_type`, in fact. It too has an interface type to satisfy; or rather, `std::suspend_always` and `std::suspend_never` describe what steers our scheduler's behavior — we can implement our own class that satisfies the corresponding interface (a `trait`) to tell the scheduler how to work: suspend, or don't. Generally speaking, the interface to satisfy is the `Awaitable` trait; or, to put it more simply, once you implement these three functions, the scheduler knows what you want:

  | Interface (Function) | Purpose | Explanation |
  | -------------------- | ------- | ----------- |
  | **`await_ready()`** | **Is it ready** | **Decides whether a suspension is needed**. If it returns `true`, it means "already ready, no need to wait": the coroutine will **continue executing** and skip `await_suspend`. If it returns `false`, it means "not ready yet, must wait": the coroutine will call `await_suspend()` to perform the suspension. |
  | **`await_suspend(H)`** | **Perform the suspension** | **Runs the logic that suspends the coroutine**. Called when `await_ready()` returns `false`. The parameter `H` is a handle to the current coroutine (`std::coroutine_handle<P>`). Inside this function, you can save the handle, put it into a task queue, and hand over control. |
  | **`await_resume()`** | **Resume execution** | **Handles the return value after resumption**. When the coroutine is woken up (`resume`), this is the first function to execute. It is responsible for returning the value the coroutine needs to use after resuming (if one is needed). |

The exercises and explanations that follow really revolve around nothing more than the three compiler extension keywords, the six mandatory **object interfaces** of the coroutine frame (five if `co_yield` isn't used, since `yield_value` drops out), and the three **interface functions** of the `Awaitable` objects returned by those coroutine-frame object interfaces to steer the corresponding behavior.

## Too Dry — Let's Have an Example

To give a quick demonstration of **how our coroutine works**, what sits above is nowhere near enough to prove anything. What we need to note is that a function intending to use a coroutine as its vehicle needs an interface defined like this:

```cpp
coroutine-return-type function-name(parameter-list);

```

So we can quickly sketch some draft code:

```cpp

bool quit_flag = 0; // quit_flag marks main's exit; that's how we get to watch our coroutine at work
int main() {
 dump_time();
 std::println("Ready to involk task()");
 auto result = task(); // receives the frame struct the coroutine interface supports
 std::println("Result here: {}", result.value());
 while (!quit_flag) // spin here to demonstrate the full flow
  ;

 std::println("Result here: {}", result.value());

 return 0;
}

```

> `dump_time` is the function I use to print execution timestamps; here is its definition, and we will use it again later when printing.
>
> ```cpp
> void dump_time() {
>  auto now = std::chrono::system_clock::now();
>  std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
>  std::tm localTime;
> #ifdef _WIN32
>  localtime_s(&localTime, &currentTime); // Windows platform
> #else
>  localtime_r(&currentTime, &localTime); // Linux/Unix platform
> #endif
>  std::cout << std::put_time(&localTime,
>                             "%H:%M:%S")
>            << " :";
> }
> ```

Next comes defining our coroutine return type. Note that the notes above already established that our coroutine return type must contain a nested type named `promise_type`. Here is the type (mind you, this type must be public — the scheduler accesses these interface functions directly). Let's first look at how we should write it so the function can operate on coroutines —

```cpp
template<typename T>
struct MyTask { // the name MyTask is arbitrary
 struct promise_type {
        // promise_type must not be renamed
        // arbitrarily; the <coroutine> header already requires this type to exist

        // this returns our coroutine return type; now the object the outside world gets from calling the coroutine function is a MyTask
        // it is in effect the struct that holds our coroutine-related content, and the results we care about live in this returned struct
        MyTask get_return_object() { ... }

        // the non-suspending version; returns std::suspend_never. initial_suspend, as the notes above covered,
        // is consulted when the coroutine frame is first created, to tell the scheduler whether to suspend — suspend_never means
        // don't suspend, just run
        // if it returned std::suspend_always, the coroutine would suspend the moment creation finishes; to get it running,
        // we would have to set it down manually. By analogy — when Windows creates a thread or a process, you control whether it runs
        // if it starts suspended, calling the resume interface later solves the problem. For convenience, we don't suspend here
        std::suspend_never initial_suspend() { ... }

        // this one runs when the coroutine finishes: on the eve of where the object would ordinarily be destroyed, the scheduler decides
        // whether to suspend the coroutine. Suspending here keeps the object from being destroyed outright, so we can inspect things
        // we suspend for now; of course, if your coroutine is pure grunt work that stores nothing else, return
        // std::suspend_never
        std::suspend_always final_suspend() noexcept { ... }

        // this is what gets called at co_return time — simply put, whatever you return is immediately forwarded into
        // return_value and stored there; later, when we use it, we read the content kept in the MyTask object (
        // generally speaking, we finish by handing it off to the Task struct)
        void return_value(T value) { ... }

        // this part: if we throw an exception outright, the compiler tosses any unhandled exception into this function
        // usually we do nothing here; of course, if you need to handle some exceptions, put your implementation here
        void unhandled_exception() { }
    };
};

```

Below, we implement this struct for real — what it actually keeps around is an `int` as the result, so the code is naturally written this way. Worth noting — much of what we are doing here is printing logs.

```cpp
struct Task {
 struct promise_type {
  promise_type()
      : __value(std::make_shared<int>()) {
   dump_time();
   std::println("Task::promise_type::promise_type is involked!");
  }
  Task get_return_object() {
   dump_time();
   std::println("Task::promise_type::get_return_object is involked!");
   return Task { __value };
  }
  std::suspend_never initial_suspend() {
   dump_time();
   std::println("Task::promise_type::initial_suspend is involked!");
   return {};
  }
  std::suspend_always final_suspend() noexcept {
   // even though we returns the std::suspend_always
   // the co-ro will dashed after the quit flags are set as 1
   // main will quit, and you wont see the program stuck
   dump_time();
   std::println("Task::promise_type::final_suspend is involked!");
   return {};
  }
  void return_value(int value) {
   dump_time();
   std::println("Task::promise_type::return_value is involked!");
   *__value = value;
   /**
    *  Warning: dont write codes like that in
    * production env, this is unsafe
    */
   quit_flag = 1; // OK, main can quit then
  }
  void unhandled_exception() { }

 private:
  std::shared_ptr<int> __value;
 };

 Task(std::shared_ptr<int> v)
     : __value(v) {
  dump_time();
  std::println("Task is created!");
 }

 int value() const { return *__value; }

private:
 std::shared_ptr<int> __value;
};

```

Our `task` function can now be implemented; let's put it below and take a look.

```cpp
Task task() {
 SimpleReader reader1;
 dump_time();
 std::println("CoAwait the reader1");
 int tol = co_await reader1;
 std::println("tol: {}", tol);

 SimpleReader reader2;
 dump_time();
 std::println("CoAwait the reader2");
 tol += co_await reader2;
 std::println("tol: {}", tol);

 SimpleReader reader3;
 dump_time();
 std::println("CoAwait the reader3");
 tol += co_await reader3;
 std::println("tol: {}", tol);

 dump_time();
 std::println("Ready to co_return");

 co_return tol;
}

```

We can see that `SimpleReader` is being `co_await`ed, so `SimpleReader` must be an Awaitable object. We already said that an Awaitable object must satisfy three interfaces to steer the scheduler's work:

```cpp
struct SimpleReader {
    // the moment our co_await statement executes, the compiler forwards straight into this function
    // false means our Awaitable object is not ready
    // to paint a more concrete scenario — the I/O event hasn't arrived yet, and the coroutine-flavored object reports here whether the I/O is done
 bool await_ready() {
  dump_time();
  std::println("call await_ready, always return false");
  return false;
 }

    // when we invoke the resume interface, the compiler immediately forwards to await_resume; what we require it to return is the result of the co_await. In the task() code we wrote int tol = co_await reader1, so the value returned here lands directly in tol
 int await_resume() {
  dump_time();
  std::println("call await_resume, return the current value: {}", value);
  return value;
 }

    // when await_ready returns no, the compiler suspends the coroutine at once and runs the handling callback await_suspend
    // kindly, the compiler passes in the coroutine's handle: std::coroutine_handle<>; this interface is
    // used to coordinate how we may operate that coroutine handle. Here I decide to toss it onto a thread detached from the main one,
    // grab the value, then set the coroutine back down to continue
 void await_suspend(std::coroutine_handle<> handle) {
  dump_time();
  std::println("call await_suspend, creating a detached thread");
  std::thread worker([this, handle]() {
   std::this_thread::sleep_for(1s);
   value = 1;
   handle.resume(); // resume the await, will later involk await_resume
  });

  worker.detach();
 }

private:
 int value { 0 };
};

```

I've put the complete code in the appendix. You can jump to Appendix 1 now to view the code and think about what the program outputs.

After compiling and running, we get the log output below. See if you got it right?

```cpp

19:24:06 :Ready to involk task()
19:24:06 :Task::promise_type::promise_type is involked!
19:24:06 :Task::promise_type::get_return_object is involked!
19:24:06 :Task is created!
19:24:06 :Task::promise_type::initial_suspend is involked!
19:24:06 :CoAwait the reader1
19:24:06 :call await_ready, always return false
19:24:06 :call await_suspend, creating a detached thread
Result here: 0
19:24:07 :call await_resume, return the current value: 1
tol: 1
19:24:07 :CoAwait the reader2
19:24:07 :call await_ready, always return false
19:24:07 :call await_suspend, creating a detached thread
19:24:08 :call await_resume, return the current value: 1
tol: 2
19:24:08 :CoAwait the reader3
19:24:08 :call await_ready, always return false
19:24:08 :call await_suspend, creating a detached thread
19:24:09 :call await_resume, return the current value: 1
tol: 3
19:24:09 :Ready to co_return
19:24:09 :Task::promise_type::return_value is involked!
19:24:09 :Task::promise_type::final_suspend is involked!
Result here: 3

```

Check it against the notes, and it is easy to work out what happened in our code.

## Exercise 2: Writing a Generator with Coroutines

The generator here mostly illustrates a coroutine asynchronously preparing results: when we need them, we go to the struct the coroutine keeps and demand the content we expect — it looks as if the coroutine conjured exactly what we wanted, and that is where the name "generator" comes from.

Next, let's write our own generator that loops over every integer between a given lower and upper bound. The signature is agreed as follows:

```cpp
Generator<int> iterate_value(int start, int end) {
 // implement codes here
}

int main() {
 simple_log("Ready to start the range loop");

 for (int queried_value : iterate_value(1, 10)) {
  std::println("get the iterative value: {}", queried_value);
 }

 simple_log("the range loop Finished!");
}

```

#### Some Thoughts

If you're well and truly out of ideas, care to hear me out?

1. First, this exercise features the classic `for(int queried_value : iterate_value(1, 10))` style of code. Going by the STL's requirements, any such `iteratable-for-loop` demands that the iterated object provide two interfaces: `begin` and `end`. Since ours is a coroutine function, what it actually returns — as the interface you saw indicates — is a `Generator<int>`, which means the generator itself must satisfy the two iterable interfaces, `begin` and `end`.
2. The next question — when does the object become iterable? The answer: the coroutine is set down to run, and the generator becomes iterable. Arranging "the coroutine resumes, therefore the generator is iterable" is too hard, so why not think in reverse — the coroutine is set down and gets to work when the generator calls `begin()`. Then the rest of the iteration is easy too! Each time we advance to the next element, we set the coroutine down again and it produces fresh content. Once our coroutine finishes working, the generator is naturally no longer iterable! At that point it acts as `end()`. How does that sound?
3. The value that comes back obviously needs handling on our side — what we hold at that point is the generator, not the value we care about — and here the iterator's `operator*` clearly gets to shine: when we dereference, the value we care about is returned out of the iterator. That's exactly why the iterator abstraction exists, isn't it?
4. The lifetime question — should the coroutine be destroyed the moment it `co_return`s? Clearly not, because the value our generator cares about is still stored in the coroutine return type's handle. So think in reverse — when the generator reaches the end of its lifecycle, our coroutine is obviously finished working too, and having the generator destroy our coroutine is plainly the right decision.

The code holds nothing new; I've already put it in the appendix.

# References

> Main reference: [Coroutines (C++20) - cppreference.cn - C++ Reference Manual](https://cppreference.cn/w/cpp/language/coroutines)
>
> I have watched these video tutorials, but judge their quality for yourselves — I'm only honestly listing what I watched
>
> - [C++20 Coroutines: 99% of Programmers Don't Fully Understand Them! Will You Be That 1%? This Might Be the Best C++ Coroutine Video on the Internet_bilibili](https://www.bilibili.com/video/BV1Cz9NYFE8E/)
> - [C++20 Coroutine Tutorial_bilibili](https://www.bilibili.com/video/BV1JN411y7Bx)

# Appendix

> co1.cpp

```cpp
#include <coroutine>
#include <iomanip>
#include <iostream>
#include <memory>
#include <print>
#include <thread>
using namespace std::chrono_literals;

void dump_time() {
 auto now = std::chrono::system_clock::now();
 std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
 std::tm localTime;
#ifdef _WIN32
 localtime_s(&localTime, &currentTime); // Windows platform
#else
 localtime_r(&currentTime, &localTime); // Linux/Unix platform
#endif

 std::cout << std::put_time(&localTime,
                            "%H:%M:%S")
           << " :";
}

struct SimpleReader {
 bool await_ready() {
  dump_time();
  std::println("call await_ready, always return false");
  return false;
 }

 int await_resume() {
  dump_time();
  std::println("call await_resume, return the current value: {}", value);
  return value;
 }

 void await_suspend(std::coroutine_handle<> handle) {
  dump_time();
  std::println("call await_suspend, creating a detached thread");
  std::thread worker([this, handle]() {
   std::this_thread::sleep_for(1s);
   value = 1;
   handle.resume(); // resume the await
  });

  worker.detach();
 }

private:
 int value { 0 };
};

bool quit_flag = 0;

struct Task {
 struct promise_type {
  promise_type()
      : __value(std::make_shared<int>()) {
   dump_time();
   std::println("Task::promise_type::promise_type is involked!");
  }
  Task get_return_object() {
   dump_time();
   std::println("Task::promise_type::get_return_object is involked!");
   return Task { __value };
  }
  std::suspend_never initial_suspend() {
   dump_time();
   std::println("Task::promise_type::initial_suspend is involked!");
   return {};
  }
  std::suspend_always final_suspend() noexcept {
   // even though we returns the std::suspend_always
   // the co-ro will dashed after the quit flags are set as 1
   // main will quit, and you wont see the program stuck
   dump_time();
   std::println("Task::promise_type::final_suspend is involked!");
   return {};
  }
  void return_value(int value) {
   dump_time();
   std::println("Task::promise_type::return_value is involked!");
   *__value = value;
   /**
    *  Warning: dont write codes like that in
    * production env, this is unsafe
    */
   quit_flag = 1; // OK, main can quit then
  }
  void unhandled_exception() { }

 private:
  std::shared_ptr<int> __value;
 };

 Task(std::shared_ptr<int> v)
     : __value(v) {
  dump_time();
  std::println("Task is created!");
 }

 int value() const { return *__value; }

private:
 std::shared_ptr<int> __value;
};

Task task() {
 SimpleReader reader1;
 dump_time();
 std::println("CoAwait the reader1");
 int tol = co_await reader1;
 std::println("tol: {}", tol);

 SimpleReader reader2;
 dump_time();
 std::println("CoAwait the reader2");
 tol += co_await reader2;
 std::println("tol: {}", tol);

 SimpleReader reader3;
 dump_time();
 std::println("CoAwait the reader3");
 tol += co_await reader3;
 std::println("tol: {}", tol);

 dump_time();
 std::println("Ready to co_return");

 co_return tol;
}

int main() {
 dump_time();
 std::println("Ready to involk task()");
 auto result = task();
 std::println("Result here: {}", result.value());
 while (!quit_flag)
  ;

 std::println("Result here: {}", result.value());

 return 0;
}

```

> co2_self.cpp

```cpp
#include "helpers.h"
#include <coroutine>
#include <format>
#include <print>

/**
 * @brief   class Generator will be the coroutine return handles
 *          We have said that we need to inplace a promise_type
 *          for coroutine schedular to co-operate the task
 */
template <typename T>
class Generator {
public:
 // to simplied the code, lets take it easy
 // make a new type coro_handle
 struct promise_type;
 using coro_handle = std::coroutine_handle<promise_type>;

 /**
  * @brief Construct a new Generator object
  *
  * @param h
  */
 Generator(coro_handle h)
     : handle(h) {
  simple_log_with_func_name();
 }

 ~Generator() {
  if (handle)
   // we return std::suspend_always
   // so we need to clean up everything here
   handle.destroy();
 }

 class Iterator {
 public:
  Iterator(coro_handle h)
      : handle(h) {
  }

  bool operator!=(const Iterator& other) const {
   return handle // happens in end()
       && !handle.done(); // or the coroutine is shutdown
  }

  Iterator& operator++() {
   if (handle) {
    handle.resume(); // resume util next co_yield!
   }
   return *this;
  }

  T operator*() const {
   if (!handle || !handle.promise()._value) {
    throw std::runtime_error("Dereferencing invalid iterator");
   }
   return handle.promise()._value;
  }

 private:
  coro_handle handle;
 };

 Iterator begin() {
  if (handle) {
   // resume as the initial suspend
   // hang up the co-routine
   handle.resume();
  }
  return Iterator { handle };
 }

 Iterator end() {
  // to manual trigger the != sessions
  return Iterator { nullptr };
 }

 // Must be name promise_type, we need to implement following
 // interfaces:
 struct promise_type {
  promise_type() {
   simple_log_with_func_name();
  } // nothing special for the promise_type

  Generator get_return_object() noexcept {
   simple_log_with_func_name();
   // Create the Generator for outlayer caller
   return { coro_handle::from_promise(*this) };
  }

  // We need to suspend as we need to let them work
  // until the Iterator access the value
  std::suspend_always initial_suspend() {
   simple_log_with_func_name();
   return {};
  }

  // suspend the co-routine up
  std::suspend_always final_suspend() noexcept {
   simple_log_with_func_name();
   return {};
  }

  // when involk co_yield, these functions work
  std::suspend_always yield_value(T value) {
   simple_log_with_func_name(
       std::format("yield_value with {}", value));
   _value = std::move(value); // move the value
   return {}; // suspend the session
  }

  // dont handle the exception
  void unhandled_exception() { }

  // internal value
  T _value {};
 };

private:
 coro_handle handle;
};

Generator<int> iterate_value(int start, int end) {
 for (int i = start; i < end; i++) {
  // every time, what we involk
  co_yield i;
 }
}

int main() {
 simple_log("Ready to start the range loop");

 for (int queried_value : iterate_value(1, 10)) {
  // explain the code if you are not familiar with
  // STL iterations, for any FOR LOOP with iteratable objects
  // which requires the begin() and end() interfaces
  // we get the call as followings
  // 1.   call Generator<int>::begin() -> Iterator to get the initial iterators
  //      at this case, begin() will resume the co-routine which is suspend initially
  // 2.   co_yield i will call yield_value and stores i into _value,
  //      which later will be placed in hereby queried_value, as operator* is called, we will get the
  //      result stores in the promise_type
  // 3.   then we continue as it is not the end (func iterate_value dont reach co_return implicitly)
  // 4.   so, we will call operator++, which will call co_yield again, we shell return the next value
  // 5.   goto step 2 again
  // 6.   util the end, we will reach co_return, as i == end, then the
  //      co-routines are suspend, as the Iterator::end() == current_iterator, with coroutine invalid already!
  // 7.   so, loop will quit
  std::println("get the iterative value: {}", queried_value);
 }

 simple_log("the range loop Finished!");
}

```

> There are also a few helper functions, which I've put below:
>
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
 localtime_s(&localTime, &currentTime); // Windows platform
#else
 localtime_r(&currentTime, &localTime); // Linux/Unix platform
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
