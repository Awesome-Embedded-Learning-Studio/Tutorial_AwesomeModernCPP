---
title: "理解C++20的革命特性——协程支持1"
description: ""
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
platform: host
---
# 理解C++20的革命特性——协程支持1

## 什么是协程？

​ 首先，引出协程，我们跑不了提到函数的运行时栈：调用一个函数时，运行时会为该函数分配一个**栈帧**，在栈帧中保存参数、返回地址以及函数中声明的局部变量——这就是函数的运行时环境。

​ 协程的核心思想是：**函数可以在执行到一半时挂起（suspend），把执行权让出`(yield)`；当条件满足时再恢复（`resume`）并从原处继续执行。**这使得我们可以在用户态实现轻量级的协作式调度：不同任务按程序控制有序切换，而不是依赖操作系统线程的抢占式调度。

​ 当然，我们需要说明是——按照实现方式，

​ 协程有两类实现思路：**有栈协程（stackful）**会切换完整的执行栈；而**C++20 的协程属于"无栈（`stackless`）"范式**——编译器会把在挂起点需要保留的局部变量和状态封装到一个 **协程帧（coroutine frame）**中。挂起时保存该协程帧并返回，恢复时从帧里恢复状态继续执行。因为不需要切换操作系统栈，也通常不需要频繁进入内核态，对于极端的并发场景，这玩意显然比进程/线程的切换要强的太多太多。

我们使用协程通常有三大理由：

- **把异步代码写成同步风格**：复杂的回调链可以被线性、顺序的代码替代，逻辑更直观、易读。
- **高并发、低开销**：相比线程，协程的创建与切换代价更低，适合大量 I/O 密集型并发任务。
- **更灵活的控制流表达**：协程天生适合实现生成器、流水线、惰性计算与异步任务链等模式。

## C++的协程支持是如何的？

​ 我们这里是C++的博客，避免不了讨论C++的协程支持。但很遗憾，笔者必须强调的是——C++20的协程接口是在比较难写。笔者逛了些论坛，包括看到其他同志对C++20的协程的介绍，不得不承认——这一套接口如果我们不理解协程，实在是难以理解（为此我挣扎了好一会）。因此，笔者非常建议各位看此博客的时候，多练习代码，打一打日志。这样有助于你理解——C++的协程到底在做什么。

​ 为了展开说明上面的内容，笔者决定重新整理一下`cppreference`对于协程的介绍

> 我知道有一些朋友还没看什么是C++中的协程，您可以自行先看看`cppreference`对这个接口的讲述，笔者第一次看到一半就关掉写别的去了，实在有些难懂！👉[协程 (C++20) - cppreference.cn - C++参考手册](https://cppreference.cn/w/cpp/language/coroutines)

​ 整理下来——我们需要了解这些内容，您记在手头做一个笔记。或者你不想看的话，可以跳到下一部分看一下例子，你扫一眼就知道大概我们需要如何使用C++20支持的协程了

- 编译器提供的三个扩展关键字需要我们先知道：

  - `co_await`：这个关键字用于把协程挂起来，直到我们**调用了恢复机制把它放下来！**，需要说明的是——咱们的`co_await`的后面需要跟一个表达式。这个表达式往往是**一个支持若干C++约定协程接口的对象**（至少笔者目前这样使用，各位C++协程大跌花招很多，看着实在费解难懂，所以索性先这样说，便于初级读者的理解）。人话就是，等待的东西中要实现给定签名的函数，不实现编译器就会告诉你接口缺失！
  - `co_yield`：用于暂停执行并返回一个值。啥意思呢，这个时候放在咱们的协程函数里，他就会返回co_yield修饰的表达式子的值，这个值需要利用一个接口返回回去。具体怎么用别着急，我们后面会讲
  - `co_return`：用于完成执行并返回一个值，这个时候我们写下一个`co_return`，这个协程函数就结束了，准备销毁我们的协程结构体。

- 还有一部分是一个协程函数需要返回的一个结构体（**协程返回类型**），这个结构体被用来给协程框架提供一定的调度信息。实际上，咱们的现代C++都是使用的接口来表示能不能支持协程，所以，我们需要做的是声明一个对象类型，**他必须内嵌`promise_type`,注意就是这个名称，变不了！**

  > ```cpp
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

  下一步，就是声明和实现这个`promise_type`中必须要存在的接口，这就是我们要实现的——

  | 接口 (函数)                                 | 作用                                                         | 返回类型要求                                                 |
  | ------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
  | **1. `get_return_object()`**                | **获取返回对象**：协程函数被调用时第一个执行的函数。它负责创建并返回调用者（外部世界）用于操作协程的**返回对象**（如您的 `Generator`）。 | 必须返回协程函数的返回类型（或可转换为该类型）。             |
  | **2. `initial_suspend()`**                  | **初始暂停点**：决定协程在创建时是**立即执行**还是**暂停**。 | 必须返回一个 **Awaitable** 对象（如 `std::suspend_always` 或 `std::suspend_never`）。 |
  | **3. `final_suspend()`**                    | **最终暂停点**：决定协程在执行完毕（`co_return` 或函数体结束）后是**立即销毁**还是**暂停**。 | 必须返回一个 **Awaitable** 对象。                            |
  | **4. `return_void()` 或 `return_value(V)`** | **返回值处理**：用于处理协程的**终结值**或**终结状态**。     | 如果协程函数返回 `void` (例如 `Generator` 经常如此)，必须提供 `return_void()`。如果协程使用 `co_return V;` 返回一个值，则必须提供 `return_value(V)`。两者**二选一**。 |
  | **5. `unhandled_exception()`**              | **异常处理**：当协程内部发生**未捕获的异常**时被调用。       | 必须返回 `void`。                                            |

  当然，还要值得一提的是，如果你的协程函数中，用到了`co_yield`关键字，你还需要额外的搞定一个函数

  | 接口 (函数)                | 作用                                                         | 返回类型要求                                                 |
  | -------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
  | **`yield_value(T value)`** | **产出值**：当协程执行到 `co_yield T;` 时被调用。它负责存储产出的值并暂停协程。 | 必须返回一个 **Awaitable** 对象（通常是 `std::suspend_always`）。 |

- 当然，还有一个部分是我们需要注意的——您可以看到，咱们有时候要求返回`std::suspend_always`或者是`std::suspend_never`，这个虽然表达的是我们到底要不要挂起来协程，但是这个接口并不是一定要跟`promise_type`耦合的——它独立于咱们的`promise_type`，实际上。他也需要满足一个接口类型，或者说，`std::suspend_always`或者是`std::suspend_never`描述的是指导咱们的调度器的行为用的——我们可以自己实现一个满足对应接口（`trait`）的类来告诉我们调度器如何工作——是挂起还是不挂起。一般而言，需要满足的接口是`Awaitable trait`的，或者更加简单的说，你把这三个函数实现了，调度器就知道你要干啥了：

  | 接口 (函数)            | 作用           | 解释                                                         |
  | ---------------------- | -------------- | ------------------------------------------------------------ |
  | **`await_ready()`**    | **是否准备好** | **判断是否需要暂停**。如果返回 `true`，表示"已经准备好，无需等待"，协程将**继续执行**，跳过 `await_suspend`。如果返回 `false`，表示"尚未准备好，需要等待"，协程将调用 `await_suspend()` 进行暂停操作。 |
  | **`await_suspend(H)`** | **执行暂停**   | **执行挂起协程的逻辑**。当 `await_ready()` 返回 `false` 时被调用。参数 `H` 是当前协程的句柄 (`std::coroutine_handle<P>`)。在这个函数内部，你可以保存句柄，将其放入任务队列，并交出控制权。 |
  | **`await_resume()`**   | **恢复执行**   | **处理恢复后的返回值**。当协程被唤醒 (`resume`) 后，这是第一个执行的函数。它负责返回协程在恢复后需要使用的值（如果需要）。 |

  我们后面的练习，讲解，实际上就紧紧围绕着三个编译器的扩展关键字，6个必要的协程帧**对象接口**（不用`co_yield`就是5个，不包含`yield_value`了）和3个部分协程帧对象接口返回的指导对应的行为的`Awaitable`对象的**接口函数**

## 太干了，来一个例子

​ 为了短暂的说明我们的**协程的工作流程**，光看上面的例子，是不足以说明任何事情的。我们需要注意的是，一个打算使用协程来作为载体的函数，需要这样定义一个接口：

```cpp
协程返回类型 函数名称(参数列表);

```

​ 所以我们可以快速的起一个草稿代码：

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

> dump_time是笔者用来打印执行事件的函数，这里给出定义，我们后面打印的时候还会用到。
>
> ```cpp
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

​ 下一步，就是定义咱们的协程返回类型，需要注意的是，上面的笔记已经说明了咱们的协程返回类型要存在内嵌的指定类型`promise_type`，这里给出类型（注意的是，这个类型必须是public的，调度器会直接访问这些接口函数），我们先来看看我们要咋写，才能让函数支持在协程上运作——

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

​ 下面笔者把这个结构体实现了——实际上存留的是一个int作为结果，自然也就这样编写代码。值得注意的是——这里笔者很多内容都是在打印日志。

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

​ 我们现在的task函数可以准备实现了，可以放到下面来看看。

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

​ 我们可以看到`SimpleReader`被`co_await`了，所以SimpleReader必须是一个Awaitable对象。我们早在之前就提到了Awaitable对象必须满足三个接口来指导调度器工作：

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

​ 整个代码笔者放到附录了。您现在可以跳到附录一查看代码，思考一下程序的输出。

​ 编译执行后，得到下面的日志结果。看看您想的对不对？

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

​ 您对照笔记，很容易就搞清楚我们的代码发生了什么。

## 练习2：利用协程来编写生成器（Generator）

​ 这里的生成器，更多的是说明协程异步的准备结果，当我们需要的时候，我们找协程保存的结构体中索要我们期待的内容，看起来就像是协程变出来我们想要的东西——生成器因此而得名。

​ 下面，我们来编写自己的生成器，来循环输出指定上下界的每一个整数。签名约定如下：

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

#### 一些思考

​ 如果你实在没有思路，听我说说？

1. 首先，笔者这里的题目出现了经典的`for(int queried_value : iterate_value(1, 10))`样式的代码，结合STL的约束标准，任何这样的`iteratable-for-loop`要求被迭代的对象提供两个接口：`begin`和`end`，由于我们这个是协程函数，实际上返回的，如您看到接口所示的是——`Generator<int>`，说明生成器自身要满足可迭代的两个接口`begin`和`end`
2. 下一个问题——什么时候对象变得可迭代呢？答案是——协程放下，生成器可迭代。协程放下让生成器可迭代太难了，要不要反过来思考——生成器调用`begin()`的时候协程放下可以运作？这样后面的迭代也好办！迭代到下一个的时候咱们就放下协程产生新的内容。当我们的协程运作结束，生成器自然也就不可迭代了！这个时候就作为`end()`，怎么样？
3. 返回回来的值，显然需要我们做处理，这个时候我们拿到的是生成器，而不是我们关心的值——迭代器的操作符*显然就可以发力了——在我们做解引用的时候，就把我们关心的值从迭代器中返回出去——这也是迭代器抽象存在的理由，对不对？
4. 生命周期的问题——协程要不要co_return了立马被销毁呢？显然不可以，因为我们的生成器关心的值还存储在协程返回类型句柄中，那我们就倒过来想——生成器结束了它的声明周期，咱们的协程显然也就运作完毕，由生成器销毁咱们的协程，显然才是正确的决策。

代码没有什么新鲜的，笔者已经放到附录中了。

# 参考

> 主要的参考：[协程 (C++20) - cppreference.cn - C++参考手册](https://cppreference.cn/w/cpp/language/coroutines)
>
> 这些视频教程笔者看了，但是质量各位自行评判，笔者只是诚实的枚举我看了什么
>
> - [C++20 协程，99% 的程序员都没完全搞懂！你要做那 1% 吗？ 这可能是全网C++协程讲的最好的视频_bilibili](https://www.bilibili.com/video/BV1Cz9NYFE8E/)
> - [C++20协程教程_bilibili](https://www.bilibili.com/video/BV1JN411y7Bx)

# 附录

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

> 还有一些辅助函数，笔者也放到下面去：
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
