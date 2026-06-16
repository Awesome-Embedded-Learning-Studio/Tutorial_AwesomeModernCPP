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
  source_hash: 5293686037b44a163d2a5b150cc0772a1d1ffac4964a14adabcee65d9143f3cf
  translated_at: '2026-06-16T06:17:44.275019+00:00'
  engine: anthropic
  token_count: 5515
---
# Understanding C++20's Revolutionary Feature — Coroutine Support (Part 1)

## What is a Coroutine?

​ First, to introduce coroutines, we must mention the function's runtime stack: when a function is called, the runtime allocates a **stack frame** for it. This stack frame stores parameters, return addresses, and local variables declared within the function—this constitutes the function's runtime environment.

​ The core idea of a coroutine is: **a function can suspend (suspend) halfway through execution, yielding execution (`yield`); when conditions are met, it can then resume (`resume`) and continue execution from exactly where it left off.** This allows us to implement lightweight cooperative scheduling in user space: different tasks switch in an orderly, program-controlled manner, rather than relying on the preemptive scheduling of OS threads.

​ Of course, we need to clarify—based on implementation methods—

​ There are two implementation approaches for coroutines: **stackful coroutines** switch the entire execution stack; whereas **C++20 coroutines belong to the "stackless" paradigm**—the compiler encapsulates local variables and state required at the suspension point into a **coroutine frame**. Upon suspension, this coroutine frame is saved and returned; upon resumption, the state is restored from the frame to continue execution. Because there is no need to switch OS stacks, and usually no need to frequently enter kernel mode, for extreme concurrency scenarios, this is obviously vastly superior to process/thread switching.

We typically use coroutines for three major reasons:

- **Writing asynchronous code in a synchronous style**: Complex callback chains can be replaced by linear, sequential code, making logic more intuitive and readable.
- **High concurrency, low overhead**: Compared to threads, the creation and switching cost of coroutines is lower, making them suitable for massive I/O-intensive concurrent tasks.
- **More flexible control flow expression**: Coroutines are naturally suited for implementing patterns like generators, pipelines, lazy evaluation, and asynchronous task chains.

## How Does C++ Support Coroutines?

​ Since this is a C++ blog, we inevitably need to discuss C++ coroutine support. But unfortunately, I must emphasize—the C++20 coroutine interface is quite difficult to write. I have browsed some forums and seen other developers' introductions to C++20 coroutines, and I have to admit—if we don't understand coroutines, this set of interfaces is truly hard to grasp (I struggled with this for a while myself). Therefore, I strongly suggest that while reading this blog, you practice with the code and print some logs. This will help you understand—what exactly C++ coroutines are doing.

​ To elaborate on the above, I have decided to reorganize the introduction to coroutines on `cppreference`.

> I know some friends haven't seen what coroutines in C++ are yet. You can take a look at `cppreference`'s description of this interface first. I closed it halfway through my first read to go write something else; it is really a bit hard to understand! 👉[Coroutines (C++20) - cppreference.cn - C++ Reference Manual](https://cppreference.cn/w/cpp/language/coroutines)

​ To summarize—we need to understand this content, so keep this as a note. Or, if you don't want to read it, you can skip to the next section to look at the examples. Just a glance will give you a rough idea of how we need to use C++20-supported coroutines.

- We first need to know the three extended keywords provided by the compiler:

  - `co_await`: This keyword is used to suspend a coroutine until we **call a resumption mechanism to take it down!** It should be noted that—our `co_await` must be followed by an expression. This expression is often **an object supporting several C++ coroutine interface conventions** (at least that is how I use it currently; C++ coroutines have many tricks, which are really confusing and hard to understand, so let's just say this for the benefit of beginners). In plain English, the thing being waited on needs to implement functions with a given signature, or the compiler will tell you the interface is missing!
  - `co_yield`: Used to suspend execution and return a value. What does this mean? When placed in our coroutine function, it will return the value of the expression modified by `co_yield`. This value needs to be returned via a specific interface. Don't worry about the specific usage yet; we will cover it later.
  - `co_return`: Used to complete execution and return a value. At this point, when we write a `co_return`, this coroutine function ends, and we prepare to destroy our coroutine structure.

- There is also a structure (**coroutine return type**) that a coroutine function needs to return. This structure is used to provide certain scheduling information to the coroutine framework. In fact, in our modern C++, interfaces are used to indicate whether coroutines can be supported, so we need to do is declare an object type, **it must embed `promise_type`, note that this is the name, it cannot be changed!**

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

Next, we need to declare and implement the required interfaces within this `promise_type`. Here is what we need to implement:

| Interface (Function)                     | Purpose                                                                                      | Return Type Requirement                                                                                      |
| ---------------------------------------- | -------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| **1. `get_return_object()`**             | **Get Return Object**: The first function executed when the coroutine is invoked. It creates and returns the **return object** (e.g., your `Generator`) that the caller (the external world) uses to interact with the coroutine. | Must return the coroutine function's return type (or something convertible to it).                          |
| **2. `initial_suspend()`**               | **Initial Suspend Point**: Determines whether the coroutine **executes immediately** or **pauses** upon creation. | Must return an **Awaitable** object (such as `std::suspend_always` or `std::suspend_never`).                 |
| **3. `final_suspend()`**                 | **Final Suspend Point**: Determines whether the coroutine is **destroyed immediately** or **suspended** after execution finishes (`co_return` or end of function body). | Must return an **Awaitable** object.                                                                        |
| **4. `return_void()` or `return_value(V)`** | **Return Value Handling**: Used to handle the coroutine's **final value** or **final state**. | If the coroutine function returns `void` (common for `Generator`), `return_void()` must be provided. If the coroutine uses `co_return V;` to return a value, `return_value(V)` must be provided. Choose **one** of the two. |
| **5. `unhandled_exception()`**           | **Exception Handling**: Called when an **uncaught exception** occurs inside the coroutine.   | Must return `void`.                                                                                          |

Additionally, it is worth mentioning that if your coroutine function uses the `co_yield` keyword, you need to implement one extra function:

| Interface (Function)          | Purpose                                                                                      | Return Type Requirement                                                                                      |
| ----------------------------- | -------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| **`yield_value(T value)`**    | **Yield Value**: Called when the coroutine executes `co_yield T;`. It stores the yielded value and suspends the coroutine. | Must return an **Awaitable** object (typically `std::suspend_always`).                                       |

- Another part we need to pay attention to is this: you can see that we sometimes require returning `std::suspend_always` or `std::suspend_never`. Although this expresses whether we want to suspend the coroutine, this interface is not strictly coupled to `promise_type`—it is actually independent of our `promise_type`. It also needs to satisfy an interface type, or rather, `std::suspend_always` and `std::suspend_never` describe the behavior used to guide our scheduler—we can implement a class ourselves that satisfies the corresponding interface (`trait`) to tell our scheduler how to work—whether to suspend or not. Generally speaking, the interface to be satisfied is that of the **Awaitable trait**. To put it more simply, if you implement these three functions, the scheduler will know what you intend to do:

| Interface (Function)      | Purpose           | Explanation                                                                                                                                  |
| ------------------------- | ------------------ | -------------------------------------------------------------------------------------------------------------------------------------------- |
| **`await_ready()`**       | **Is Ready**      | **Determines if suspension is needed**. If returning `true`, it means "already prepared, no need to wait," and the coroutine will **continue executing**, skipping `await_suspend`. If returning `false`, it means "not yet prepared, need to wait," and the coroutine will call `await_suspend()` to perform the suspension operation. |
| **`await_suspend(H)`**    | **Execute Suspend** | **Execute the logic to suspend the coroutine**. Called when `await_ready()` returns `false`. The parameter `H` is the current coroutine's handle (`std::coroutine_handle<P>`). Inside this function, you can save the handle, place it into a task queue, and yield control. |
| **`await_resume()`**      | **Resume Execution** | **Handle the return value after resumption**. When the coroutine is woken up (`resume`), this is the first function executed. It is responsible for returning the value the coroutine needs to use after resumption (if applicable). |

Our subsequent exercises and explanations will actually revolve closely around three compiler extension keywords, the six necessary coroutine frame **object interfaces** (five if you don't use `co_yield`, excluding `yield_value`), and the three **interface functions** of the `Awaitable` object returned by parts of the coroutine frame object that guide the corresponding behavior.

## This is Too Dry, Let's Look at an Example

To briefly explain our **coroutine workflow**, looking at the previous examples alone is not enough to illustrate anything clearly. We need to note that a function intended to use coroutines as a carrier needs to define an interface like this:

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

> `dump_time` is a function the author uses to print execution events. Here is the definition, which we will use again later for printing.
>
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

Next, we define our coroutine return type. As noted in the previous section, our coroutine return type must contain the nested type `promise_type`. Here is the type definition (note that this type must be public, as the scheduler will directly access these interface functions). Let's first examine how to write this so that our functions can support coroutine operations—

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

Below, we implement this struct—since we actually store an integer as the result, we naturally write the code this way. Note that much of the code here involves logging.

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

We can now implement the task function. Let's place it below and take a look.

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

We can see that `SimpleReader` is being `co_await`ed, which means `SimpleReader` must be an Awaitable object. As we mentioned earlier, an Awaitable object must implement three interfaces to guide the scheduler:

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

I have placed the full code in the appendix. You can now jump to Appendix 1 to review the code and think about the program's output.

After compiling and executing, we get the following log output. Let's see if your prediction was correct.

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

By comparing with the notes, we can easily understand what is happening in our code.

## Exercise 2: Using Coroutines to Write a Generator

Here, the term "generator" primarily refers to the prepared result of a coroutine's asynchronous operation. When we need data, we request the expected content from the structure saved by the coroutine. It looks as if the coroutine produces the value we want on demand—hence the name "generator."

Next, we will write our own generator to sequentially output every integer within a specified range. The signature is defined as follows:

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

​ If you are really stuck, how about we walk through this together?

1. First, the code here features the classic `for(int queried_value : iterate_value(1, 10))` pattern. Given the constraints of the STL, any such range-based `for` loop requires the iterated object to provide two interfaces: `begin` and `end`. Since this is a coroutine function, it actually returns a `Generator<int>` (as shown in the interface). This means the generator itself must satisfy the iterable interface requirements by providing `begin` and `end`.

2. The next question is—when does the object become iterable? The answer is—when the coroutine suspends, the generator becomes iterable. It's tricky to make the generator iterable only when the coroutine suspends, so let's reverse the logic—what if the coroutine suspends when we call `begin()` on the generator? This makes the subsequent iteration easy to handle! When we iterate to the next item, we just resume the coroutine to generate new content. When our coroutine finishes execution, the generator naturally becomes no longer iterable. At that point, it serves as our `end()`. How does that sound?

3. We clearly need to handle the returned value. At this stage, we hold a generator, not the value we care about. The iterator's dereference operator (`operator*`) is the perfect tool for this. When we dereference the iterator, we extract and return the value we care about. This is, after all, the rationale behind the iterator abstraction, right?

4. Regarding lifecycles—should the coroutine be destroyed immediately after it `co_return`s? Obviously not, because the values our generator cares about are still stored within the coroutine return object's handle. So, let's think inversely—when the generator reaches the end of its lifecycle, our coroutine has obviously finished running. Therefore, having the generator destroy our coroutine is clearly the correct decision.

The code itself isn't anything new; I have placed it in the appendix below.

# References

> Main reference: [Coroutines (C++20) - cppreference.cn - C++ Reference Manual](https://cppreference.cn/w/cpp/language/coroutines)
>
> I have watched these video tutorials, but please judge their quality yourselves. I am simply listing what I watched honestly.
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

```cpp
// co2_self.cpp
```

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
