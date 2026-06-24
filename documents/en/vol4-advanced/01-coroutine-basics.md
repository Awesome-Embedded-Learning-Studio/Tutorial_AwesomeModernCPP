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
title: 'Understanding C++20''s Revolutionary Feature: Coroutine Support (Part 1)'
description: ''
translation:
  source: documents/vol4-advanced/01-coroutine-basics.md
  source_hash: 55eb7d50ac6a17b098145298906ffc9b8da0ccd919ae11ac801313630a7c3584
  translated_at: '2026-06-24T00:52:35.121335+00:00'
  engine: anthropic
  token_count: 5515
---
# Understanding C++20's Revolutionary Feature—Coroutines Part 1

## What is a Coroutine?

First, to introduce coroutines, we must mention the function's runtime stack: when a function is called, the runtime allocates a **stack frame** for it. This stack frame stores parameters, return addresses, and local variables declared within the function—this constitutes the function's runtime environment.

The core idea of a coroutine is: **a function can suspend (suspend) halfway through execution, yielding execution (`yield`); when conditions are met, it can then resume (`resume`) and continue execution from where it left off**. This allows us to implement lightweight cooperative scheduling in user space: different tasks switch in an orderly, program-controlled manner, rather than relying on the preemptive scheduling of OS threads.

Of course, we need to clarify—based on implementation methods,

There are two implementation approaches for coroutines: **stackful coroutines** switch the entire execution stack; whereas **C++20 coroutines belong to the "stackless" paradigm**—the compiler encapsulates the local variables and state that need to be preserved at the suspension point into a **coroutine frame**. Upon suspension, this coroutine frame is saved and returned; upon resumption, the state is restored from the frame to continue execution. Because there is no need to switch OS stacks, and usually no need to frequently enter kernel mode, this approach is obviously far superior to process/thread switching in extreme concurrency scenarios.

We typically have three major reasons for using coroutines:

- **Write asynchronous code in a synchronous style**: Complex callback chains can be replaced by linear, sequential code, making the logic more intuitive and readable.
- **High concurrency, low overhead**: Compared to threads, the creation and switching cost of coroutines is lower, making them suitable for massive numbers of I/O-intensive concurrent tasks.
- **More flexible control flow expression**: Coroutines are naturally suited for implementing patterns like generators, pipelines, lazy evaluation, and asynchronous task chains.

## How Does C++ Support Coroutines?

Since this is a C++ blog, we inevitably need to discuss C++'s support for coroutines. But unfortunately, I must emphasize—the C++20 coroutine interface is quite difficult to write. I have browsed some forums and seen other developers' introductions to C++20 coroutines, and I have to admit—if we don't understand coroutines themselves, this set of interfaces is truly hard to grasp (I struggled with this for a while myself). Therefore, I strongly suggest that while reading this blog, you practice the code and print some logs. This will help you understand—what exactly C++ coroutines are doing.

To elaborate on the above, I have decided to reorganize the introduction to coroutines from `cppreference`.

> I know some friends haven't yet looked at what coroutines are in C++. You can take a look at `cppreference`'s description of this interface first. I closed it halfway through my first look to go write something else; it is really a bit hard to understand! 👉[Coroutines (C++20) - cppreference.cn - C++ Reference Manual](https://cppreference.cn/w/cpp/language/coroutines)

To summarize it all—we need to understand this content, so keep it handy for notes. Or, if you don't want to read it, you can skip to the next section and look at the examples. Just a glance will give you a rough idea of how we need to use the coroutines supported in C++20.

- We first need to know the three extended keywords provided by the compiler:

  - `co_await`: This keyword is used to suspend a coroutine until we **call a resumption mechanism to take it down!** It should be noted that—our `co_await` must be followed by an expression. This expression is often **an object supporting several C++ standard coroutine interfaces** (at least this is how I currently use it; there are many wild tricks with C++ coroutines that look really confusing, so let's put it this way for now to facilitate the understanding of beginner readers). In plain English, the thing being waited on must implement functions with given signatures, or the compiler will tell you the interface is missing!
  - `co_yield`: Used to pause execution and return a value. What does this mean? When placed in our coroutine function, it will return the value of the expression modified by `co_yield`. This value needs to be returned via a specific interface. Don't worry about the specifics yet; we will cover that later.
  - `co_return`: Used to complete execution and return a value. At this point, when we write a `co_return`, this coroutine function ends, and we prepare to destroy our coroutine structure.

- There is also a structure (**coroutine return type**) that a coroutine function needs to return. This structure is used to provide certain scheduling information to the coroutine framework. In reality, our modern C++ uses interfaces to indicate whether coroutines are supported, so we need to do is declare an object type, **it must embed `promise_type`, note this name, it cannot be changed!**

>

```cpp
  > // coroutine中
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

Next, we need to declare and implement the required interfaces within this `promise_type`. This is what we need to implement:

| Interface (Function) | Purpose | Return Type Requirement |
| ------------------- | ------- | ----------------------- |
| **1. `get_return_object()`** | **Get Return Object**: The first function executed when the coroutine is called. It is responsible for creating and returning the **return object** (like your `Generator`) that the caller (the outside world) uses to interact with the coroutine. | Must return the coroutine function's return type (or something convertible to it). |
| **2. `initial_suspend()`** | **Initial Suspend Point**: Determines whether the coroutine is **eagerly executed** or **suspended** immediately upon creation. | Must return an **Awaitable** object (such as `std::suspend_always` or `std::suspend_never`). |
| **3. `final_suspend()`** | **Final Suspend Point**: Determines whether the coroutine is **destroyed immediately** or **suspended** after execution finishes (`co_return` or end of function body). | Must return an **Awaitable** object. |
| **4. `return_void()` or `return_value(V)`** | **Return Value Handling**: Used to handle the coroutine's **final value** or **final state**. | If the coroutine function returns `void` (which is often the case for `Generator`), you must provide `return_void()`. If the coroutine uses `co_return V;` to return a value, you must provide `return_value(V)`. You must implement **one or the other**. |
| **5. `unhandled_exception()`** | **Exception Handling**: Called when an **uncaught exception** occurs inside the coroutine. | Must return `void`. |

It is also worth mentioning that if your coroutine function uses the `co_yield` keyword, you need to implement one additional function:

| Interface (Function) | Purpose | Return Type Requirement |
| ------------------- | ------- | ----------------------- |
| **`yield_value(T value)`** | **Yield Value**: Called when the coroutine executes `co_yield T;`. It is responsible for storing the yielded value and suspending the coroutine. | Must return an **Awaitable** object (typically `std::suspend_always`). |

- Another part we need to pay attention to: As you can see, we sometimes require returning `std::suspend_always` or `std::suspend_never`. Although this expresses whether we want to suspend the coroutine or not, this interface is not strictly coupled to the `promise_type`—it is actually independent of it. It also needs to satisfy an interface type, or rather, `std::suspend_always` and `std::suspend_never` describe the behavior used to guide our scheduler—we can implement our own class that satisfies the corresponding interface (`trait`) to tell our scheduler how to work—whether to suspend or not. Generally speaking, the interface that needs to be satisfied is the `Awaitable` trait. To put it more simply, if you implement these three functions, the scheduler knows what you intend to do:

| Interface (Function) | Purpose | Explanation |
| ------------------- | ------- | ----------- |
| **`await_ready()`** | **Is Ready** | **Determines if suspension is needed**. If it returns `true`, it means "already ready, no need to wait," and the coroutine will **continue execution**, skipping `await_suspend`. If it returns `false`, it means "not ready yet, need to wait," and the coroutine will call `await_suspend()` to perform the suspension operation. |
| **`await_suspend(H)`** | **Execute Suspend** | **Execute the logic to suspend the coroutine**. Called when `await_ready()` returns `false`. The parameter `H` is the handle to the current coroutine (`std::coroutine_handle<P>`). Inside this function, you can save the handle, place it into a task queue, and yield control. |
| **`await_resume()`** | **Resume Execution** | **Handle the return value after resumption**. When the coroutine is resumed (`resume`), this is the first function executed. It is responsible for returning the value the coroutine needs to use after resumption (if applicable). |

Our subsequent exercises and explanations will revolve around three compiler extension keywords, the five or six necessary **object interfaces** for the coroutine frame (five if `co_yield` is not used, excluding `yield_value`), and the three **interface functions** of the `Awaitable` object returned by the coroutine frame object interfaces that guide the corresponding behavior.

## That's Too Dry, Let's Look at an Example

To briefly explain our **coroutine workflow**, looking at the table above is not enough to clarify anything. We need to note that a function intended to use coroutines as a carrier needs to define an interface like this:

```cpp
协程返回类型 函数名称(参数列表);

```

So we can quickly draft some code:

```cpp

bool quit_flag = 0; // 这个quit_flag用来标识Main的退出，这样我们才能看到咱们的协程的工作
int main() {
 dump_time();
 std::println("Ready to involk task()");
 auto result = task(); // 接受协程接口支持的栈帧结构体
 std::println("Result here: {}", result.value());
 while (!quit_flag) // 卡在这里，演示完整的流程
  ;

 std::println("Result here: {}", result.value());

 return 0;
}

```

> `dump_time` is a function we use to print execution events. Here is the definition, and we will use it again later for printing.
>

```cpp
> void dump_time() {
>  auto now = std::chrono::system_clock::now();
>  std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
>  std::tm localTime;
> #ifdef _WIN32
>  localtime_s(&localTime, &currentTime); // Windows 平台
> #else
>  localtime_r(&currentTime, &localTime); // Linux/Unix 平台
> #endif
>  std::cout << std::put_time(&localTime,
>                             "%H:%M:%S")
>            << " :";
> }
> ```

Next, we define our coroutine return type. As noted previously, this return type must contain a nested type named `promise_type`. Here is the type (note that this type must be public, as the scheduler will directly access these interface functions). Let's first look at how we need to write this to enable the function to operate as a coroutine —

```cpp
template<typename T>
struct MyTask { // MyTask的名称是随意的
 struct promise_type {
        // promise_type不可以随
        // 在coroutine文件中已经要求了这个类型的存在

        // 返回的是咱们的协程返回类型，这个时候外界调用的协程函数返回的对象就是MyTask
        // 实际上就是保存咱们的协程相关的内容的结构体, 我们关心的一些结果就在这个返回的结构体中
        MyTask get_return_object() { ... }

        // 不挂起的版本, 返回的是 std::suspend_never, initial_suspend在上面的笔记中谈到
        // 他是用来协程栈帧首次被创建的时候, 用来告诉调度器要不要挂起的, suspend_never就是
        // 不要挂起，直接跑
        // 如果返回的是 std::suspend_always, 那就是创建完马上挂起，需要他跑起来，
        // 我们就需要手动放下，打个类比的话——Windows创建线程or进程您可以控制它到底运行不运行
        // 如果创建即挂起，那么后面我们调用resume接口就能解决这个问题, 方便起见这里不挂起
        std::suspend_never initial_suspend() { ... }

        // 这个是协程在执行完毕的时候，调度器会在对象本来应该析构的前夕，决定
        // 要不要挂起来这个协程，这里挂起是为了防止对象直接被析构干净了，我们方便检查点内容
        // 这里就先挂起，当然如果你的协程单纯的是做苦力，不保存任何其他东西，返回
        // std::suspend_never
        std::suspend_always final_suspend() noexcept { ... }

        // co_return的时候，调用的就是这个东西——说起来很简单，return的东西会立马被转发到
        // return_value里保存起来，我们后面使用的时候，就访问对应的MyTask类型保存的内容（
        // 一般而言，咱们都是扔到Task结构体中结束的）
        void return_value(T value) { ... }

        // 这个部分是如果我们直接throw了异常，编译器会把那些没有处理的异常扔到这个函数里
        // 一般我们不做任何处理，当然，如果您需要处理一部分异常，把你的实现放到这里
        void unhandled_exception() { }
    };
};

```

Below, we implement this struct—since we are actually storing an integer as the result, the code is naturally written this way. Note that we print a lot of logs here.

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

We can now implement our task function. Let's place it below and take a look.

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

We can see that `SimpleReader` is being `co_await`ed, which means `SimpleReader` must be an Awaitable object. As we mentioned earlier, an Awaitable object must satisfy three interfaces to guide the scheduler:

```cpp
struct SimpleReader {
    // await_ready是我们的co_await语句一执行，编译器立马就会转发到这个函数里来
    // false就表明，咱们的Awaitable对象没有预备好
    // 可以拿更加场景化的例子举例——IO事件没有准备，协程化的对象这里就要返回IO是否做好了
 bool await_ready() {
  dump_time();
  std::println("call await_ready, always return false");
  return false;
 }

    // 当我们调用恢复resume接口的时候，编译器立马就会转发到await_resume上，实际上我们要求返回的就是co_await的结果，task()代码中我们是int tol = co_await reader1, 所以，这里的return value就会直接返回给tol
 int await_resume() {
  dump_time();
  std::println("call await_resume, return the current value: {}", value);
  return value;
 }

    // 当我们的await_ready返回否的时候，编译器立马挂起协程，并且走处理回调await_suspend
    // 当然，编译器好心的帮助我们传递进来了协程的handle: std::coroutine_handle<>， 这个接口被
    // 用来协调 我们可以如何操作这个协程handle，笔者这里就决定扔到一个脱离主线程的子线程
    // 拿到value后直接放下协程继续执行
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

I have placed the complete code in the appendix. You can now jump to Appendix 1 to review the code and think about the program's output.

After compiling and running, we get the following log output. Let's see if your prediction was correct.

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

By comparing the notes, we can easily understand what our code is doing.

## Exercise 2: Using Coroutines to Write a Generator

Here, a "generator" primarily refers to the prepared result of a coroutine's asynchronous operation. When we need data, we request the expected content from the structure saved by the coroutine. It looks as if the coroutine magically produces what we want—hence the name "generator."

Next, let's write our own generator to sequentially output every integer within a specified range. The signature is defined as follows:

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

​ If you are stuck, let me walk you through it?

1. First, the code here features the classic `for(int queried_value : iterate_value(1, 10))` pattern. Combined with STL constraints, any such `iteratable-for-loop` requires the object being iterated to provide two interfaces: `begin` and `end`. Since this is a coroutine function, the actual return type is `Generator<int>`, as shown in the interface. This means the generator itself must satisfy the iterable interfaces `begin` and `end`.
2. The next question is—when does the object become iterable? The answer is—when the coroutine suspends, the generator becomes iterable. Making the generator iterable by suspending the coroutine seems difficult, so let's reverse the logic—what if the coroutine suspends when the generator calls `begin()`? This makes subsequent iteration easy! When we iterate to the next item, we just suspend the coroutine to produce new content. When our coroutine finishes, the generator naturally becomes non-iterable. At that point, it serves as `end()`. How does that sound?
3. We obviously need to handle the returned value. At this stage, we hold a generator, not the value we care about. The iterator's operator* can come into play here—when we dereference, we return the value we care about from the iterator. This is the very reason for the iterator abstraction, right?
4. Regarding the lifetime issue—should the coroutine be destroyed immediately after it `co_return`s? Obviously not, because the value our generator cares about is still stored in the coroutine return handle. So, let's think in reverse—when the generator ends its lifecycle, our coroutine is obviously finished. It is clearly the correct decision for the generator to destroy our coroutine.

The code is nothing new; I have placed it in the appendix.

# References

> Main reference: [Coroutines (C++20) - cppreference.cn - C Reference Manual](https://cppreference.cn/w/cpp/language/coroutines)
>
> I have watched these video tutorials, but please judge the quality yourselves. I am simply listing what I watched honestly.
>
> - [C++20 Coroutines, 99% of programmers don't fully understand! Do you want to be that 1%? This might be the best C++ coroutine video on the web_bilibili](https://www.bilibili.com/video/BV1Cz9NYFE8E/)
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
 localtime_s(&localTime, &currentTime); // Windows 平台
#else
 localtime_r(&currentTime, &localTime); // Linux/Unix 平台
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

I am ready to translate your content. However, it appears you have only provided the filename `> co2_self.cpp` and not the actual Markdown content or code.

Please paste the full text of the Markdown file or the code you would like me to translate, and I will process it according to the specified rules.

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

> We have also included some helper functions below:
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
