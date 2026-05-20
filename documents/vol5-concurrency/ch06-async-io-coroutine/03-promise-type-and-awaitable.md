---
title: "promise_type 与 awaitable"
description: "掌握 C++20 协程的两大定制扩展点——promise_type 控制协程行为，awaitable 控制挂起与恢复"
chapter: 6
order: 3
tags:
  - host
  - cpp-modern
  - advanced
  - coroutine
  - 异步编程
difficulty: advanced
platform: host
reading_time_minutes: 30
cpp_standard: [20]
prerequisites:
  - "C++20 协程基础"
related:
  - "异步 I/O 与事件循环"
  - "协程 Echo Server 实战"
---

# promise_type 与 awaitable

上一篇我们看到了 C++20 协程的基本语法——`co_await`、`co_yield`、`co_return` 长什么样，编译器帮我们生成了一个什么样的状态机。但说实话，光会用那几个关键字只是皮毛。C++20 协程真正的威力——或者说真正的"坑"——在于它把几乎所有的行为决策都交给了两个定制点：`promise_type` 和 `awaitable`（更准确地说是 awaiter）。这让协程变成了一个"框架"而不是一个"功能"：语言标准只规定了编译器在什么时候调用什么方法，至于这些方法怎么实现，完全由你决定。

这种设计哲学的好处是极致的灵活性——你可以用协程来实现生成器、异步任务、惰性求值、协作式调度，甚至是状态机。坏处是，C++20 标准库几乎没有提供任何现成的协程类型（`std::generator` 要到 C++23 才来），所以你不得不自己从零搭建整个基础设施。这篇和下一篇我们要做的，就是把这两个定制点拆透，让你读完之后能自己写出可用的协程框架。

## 环境说明

本篇所有代码在以下环境中测试通过：

- **操作系统**：Linux (WSL2, kernel 6.6+)
- **编译器**：GCC 13+ 或 Clang 17+（两者对 C++20 协程的支持已经相当完善）
- **编译选项**：`-std=c++20 -fcoroutines`（GCC 可能需要 `-fcoroutines`，Clang 通常默认支持）
- **平台**：本篇所有内容都是平台无关的纯 C++20，不涉及操作系统特定 API（下一篇才会引入 epoll）

## promise_type 的全貌

如果你读过上篇，你应该还记得：每当编译器遇到一个包含 `co_await`、`co_yield` 或 `co_return` 的函数，它就会把那个函数变成一个协程。而协程的"行为"——它的返回值怎么构造、启动时是否挂起、结束时做什么——全部由一个叫做 `promise_type` 的嵌套类型来控制。

这个 `promise_type` 不是什么神秘的东西，它就是协程返回类型的一个嵌套类（或者通过 `std::coroutine_traits` 指定的类型）。编译器在协程的"协程帧"（coroutine frame）里为你构造一个 `promise_type` 对象，然后在协程生命周期的各个节点上调用这个对象的方法。

我们现在要做的是，沿着协程的生命周期走一遍，把 `promise_type` 的每个钩子都拆开来看。

### 生命周期总览

一个协程从被调用到最终销毁，大致经历几个阶段。首先，编译器在堆上分配一块内存来保存协程状态——局部变量、挂起点、promise 对象等等——这就是所谓的"协程帧"（coroutine frame）。你可以通过 `promise_type` 的 `operator new` 来自定义分配策略，不过大多数情况下默认的堆分配就够了。协程帧分配好之后，编译器在里面构造一个 `promise_type` 实例，紧接着调用 `get_return_object()`，这个方法的返回值就是协程函数返回给调用者的那个对象——通常它会拿到协程的 handle 并把它包进返回类型里。

接下来，协程体在执行第一条语句之前，会先调用 `initial_suspend()`，它返回一个 awaitable，决定协程是"立刻开始执行"还是"先挂起来"。之后就是你的代码真正运行的时间了，在这个过程中可能发生 `co_await`（挂起）、`co_yield`（产出值并挂起）、`co_return`（返回并结束）。当 `co_return` 执行时会触发 `return_value()` 或 `return_void()`——有返回值就调前者，没有就调后者。协程体执行完毕（或异常退出）后，`final_suspend() noexcept` 被调用，它决定协程结束时是否挂起。如果 `final_suspend` 返回 `suspend_never`，协程帧会自动被销毁；如果返回 `suspend_always`，协程帧保持挂起状态，直到有人手动调用 `handle.destroy()`。如果执行过程中抛出了未捕获的异常，`unhandled_exception()` 会被调用，然后直接跳到 `final_suspend`。

下面是一个最简单的 `promise_type` 实现，包含了所有必要的钩子：

```cpp
#include <coroutine>
#include <cstdio>

/// 一个最简单的协程返回类型——不做任何有用的事情，
/// 只是完整展示 promise_type 的所有钩子
struct SimpleTask {
    struct promise_type {
        // ① 构造返回给调用者的对象
        SimpleTask get_return_object()
        {
            // 把协程 handle 包装进返回对象
            return SimpleTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        // ② 协程启动前——这里选择不挂起，立即开始执行
        std::suspend_never initial_suspend() { return {}; }

        // ③ 协程结束时——挂起来，防止协程帧被自动销毁
        //    注意：noexcept 是必须的
        std::suspend_always final_suspend() noexcept { return {}; }

        // ④ co_return 没有返回值时调用
        void return_void() {}

        // ⑤ 异常处理
        void unhandled_exception()
        {
            // 最简单的做法：直接 rethrow
            // 也可以把异常存起来，等后面再抛
            throw;
        }
    };

    // 协程句柄——持有对协程帧的引用
    std::coroutine_handle<promise_type> handle;
};

// 一个使用 SimpleTask 的协程函数
SimpleTask hello_coroutine()
{
    std::puts("你好，协程世界！");
    co_return; // 触发 return_void()
}

int main()
{
    auto task = hello_coroutine();   // 此时协程已经执行完毕（因为 initial_suspend 返回 suspend_never）
    task.handle.destroy();           // 必须手动销毁（因为 final_suspend 返回 suspend_always）
    return 0;
}
```

你会发现，这个例子虽然简单，但已经把 `promise_type` 的核心职责都涵盖了。接下来我们逐个钩子展开。

### get_return_object()：创建返回对象

这个钩子在协程帧分配好、promise 对象构造好之后立即被调用。它的返回值就是协程函数返回给调用者的对象。这里面有一个关键的细节：在 `get_return_object()` 执行的时候，协程体还没有开始执行，但协程帧已经存在了。所以你可以通过 `std::coroutine_handle<promise_type>::from_promise(*this)` 拿到协程的 handle，把它塞进返回对象里，这样调用者就能通过这个 handle 来控制协程的执行。

这个设计其实是协程和调用者之间的"握手"：协程说"我准备好了，这是我的 handle"，调用者拿到 handle 后可以选择立刻 `resume()`，也可以先存起来等以后再恢复。

### initial_suspend()：启动时挂起决策

这个钩子决定协程在执行第一条语句之前是否挂起。它返回一个 awaitable 对象，通常的选择就两种：`std::suspend_never`（不挂起，立即执行协程体）和 `std::suspend_always`（挂起，等调用者手动 `resume()`）。

什么时候该用 `suspend_never`，什么时候该用 `suspend_always`？这取决于你的使用场景。如果你希望协程"启动就跑"（fire-and-forget 风格），用 `suspend_never`。如果你希望协程是"惰性求值"的（lazy evaluation），调用者需要显式启动它，用 `suspend_always`。后者在实现生成器（generator）时非常常见——你创建一个生成器，直到你第一次调用 `begin()` 或 `next()` 时才开始执行协程体。

### final_suspend() noexcept：结束时的关键决策

这个钩子可能是整个 `promise_type` 里最容易出错的地方。

`final_suspend` 在协程体执行完毕（通过 `co_return` 正常返回，或者 `unhandled_exception` 处理完异常）之后被调用。它同样返回一个 awaitable，决定协程是否在结束时挂起。

关键问题是：为什么大多数实现选择返回 `suspend_always`？

> ⚠️ **如果你返回 `suspend_never`，协程帧会在 `final_suspend` 返回后立即被销毁。这意味着此时任何对协程 handle 的操作都是悬垂的（dangling）——你的程序随时可能崩溃。**
>
> 返回 `suspend_always` 让协程在结束状态挂起，协程帧保持有效，调用者可以安全地检查协程状态、获取返回值，然后手动调用 `handle.destroy()` 来清理。这是一种更安全的"手动生命周期管理"模式。

另外，`noexcept` 不是可选的——标准规定 `final_suspend` 必须是 `noexcept` 的。原因也很直白：如果 `final_suspend` 的 awaitable 操作抛出了异常，协程已经执行完了，这时候该把异常抛给谁？没有合理的接收方，所以标准直接在编译期禁止了这种可能性。

### return_value() / return_void()：co_return 的处理

当协程执行 `co_return expr;` 时，`promise_type::return_value(expr)` 被调用。如果 `co_return;` 没有返回值（或者协程体末尾隐式 `co_return`），则 `promise_type::return_void()` 被调用。

注意，`return_value` 和 `return_void` 在使用上的选择取决于你的协程设计：如果你的协程总是通过 `co_return expr;` 返回一个值，定义 `return_value()`；如果你的协程通过 `co_return;` 退出（或者执行到函数末尾隐式返回），定义 `return_void()`。技术上你可以同时定义两者——`co_return;` 会调用 `return_void()`，`co_return expr;` 会调用 `return_value(expr)`——但在实践中，一个设计良好的协程类型通常只使用其中一种，避免调用者混淆。

一个典型的 `return_value` 实现会把值存到 promise 对象里，等后续通过 handle 去取：

```cpp
struct TaskWithValue {
    struct promise_type {
        int kResultValue; // 存储返回值

        TaskWithValue get_return_object()
        {
            return TaskWithValue{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_return value; 时调用
        void return_value(int value) { kResultValue = value; }

        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;

    int get_result() const { return handle.promise().kResultValue; }
};

TaskWithValue compute_something()
{
    co_return 42;
}
```

### yield_value()：co_yield 的处理

`co_yield expr;` 实际上等价于 `co_await promise.yield_value(expr);`。也就是说，`yield_value` 的返回值必须是一个 awaitable。最常见的做法是返回 `std::suspend_always`，表示每次 yield 之后都挂起协程，把控制权交还给调用者。

`yield_value` 在实现生成器时是核心。每次调用者从生成器取一个值，生成器就执行到下一个 `co_yield`，产出值后挂起，等待下一次取值。

```cpp
#include <coroutine>
#include <cstdio>

// 一个简单的整数生成器
struct IntGenerator {
    struct promise_type {
        int kCurrentValue; // 当前产出的值

        IntGenerator get_return_object()
        {
            return IntGenerator{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield value; → 产出值并挂起
        std::suspend_always yield_value(int value)
        {
            kCurrentValue = value;
            return {}; // 返回 suspend_always，挂起协程
        }

        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;

    // 获取当前值
    int current_value() const { return handle.promise().kCurrentValue; }

    // 推进到下一个值，返回 false 表示生成器结束
    bool next()
    {
        handle.resume();
        return !handle.done();
    }

    ~IntGenerator()
    {
        if (handle) {
            handle.destroy();
        }
    }
};

// 使用生成器产出斐波那契数列
IntGenerator fibonacci()
{
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        int kTemp = a + b;
        a = b;
        b = kTemp;
    }
}

int main()
{
    auto gen = fibonacci();
    for (int i = 0; i < 10 && gen.next(); ++i) {
        std::printf("%d ", gen.current_value());
    }
    std::puts("");
    // 输出: 0 1 1 2 3 5 8 13 21 34
    return 0;
}
```

这个生成器虽然简单，但它展示了 `yield_value` 的核心用法：每次 `co_yield` 产出值并挂起，调用者通过 `resume()` 推进到下一个值。这就是 Python 里 `yield` 关键字背后的机制，只不过 C++ 需要你自己搭框架。

### unhandled_exception()：异常的最后一道防线

如果协程体内抛出了异常且没有被捕获，`unhandled_exception()` 就会被调用。你可以在这个钩子里做几件事：

最简单的做法是什么都不做（`std::terminate()` 的隐式调用），或者直接 `throw;` 把异常重新抛出到调用者。但这两种做法都比较粗暴。更精细的做法是把 `std::current_exception()` 存到 promise 对象里，等调用者通过 handle 取结果的时候再 `std::rethrow_exception()`。这样异常的传播就变成了"按需"的，而不是"立即炸掉"的。

```cpp
struct SafeTask {
    struct promise_type {
        std::exception_ptr kException;

        SafeTask get_return_object()
        {
            return SafeTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}

        void unhandled_exception()
        {
            // 捕获异常，存起来稍后处理
            kException = std::current_exception();
        }
    };

    std::coroutine_handle<promise_type> handle;

    void rethrow_if_failed()
    {
        if (handle.promise().kException) {
            std::rethrow_exception(handle.promise().kException);
        }
    }

    ~SafeTask()
    {
        if (handle) {
            handle.destroy();
        }
    }
};
```

很好，到这里我们已经把 `promise_type` 的所有主要钩子都过了一遍。现在回头看，你会发现 `promise_type` 本质上就是一个"协程行为控制器"：它控制协程怎么启动、怎么结束、怎么处理返回值和异常。而协程体里的 `co_await`——也就是"挂起与恢复"——则由另一套机制来控制，这就是接下来要讲的 awaiter/awaitable 协议。

## awaiter/awaitable 协议

如果说 `promise_type` 控制的是协程的"宏观生命周期"，那 awaiter/awaitable 控制的就是"微观挂起与恢复"。每次你在协程里写 `co_await expr;`，编译器就会在 `expr` 上执行一套固定的协议：先问"准备好了没"，再问"挂起后做什么"，最后问"恢复后给我什么结果"。

### co_await expr 的展开过程

让我们一步步看编译器在处理 `co_await expr;` 时到底做了什么。

首先，编译器需要从 `expr` 得到一个 awaiter 对象，这个过程分两步。

第一步是得到 awaitable。如果 `promise_type` 定义了 `await_transform` 成员函数，编译器会先调用 `promise.await_transform(expr)` 得到一个中间结果，这个中间结果就是 awaitable。如果没有 `await_transform`，那原始的 `expr` 本身就是 awaitable。（注意，`initial_suspend`、`final_suspend` 和 `yield_value` 产生的表达式会跳过 `await_transform`，直接作为 awaitable 使用。）

第二步是从 awaitable 得到 awaiter。编译器会对 `operator co_await` 进行重载决议（overload resolution），成员函数 `awaitable.operator co_await()` 和非成员函数 `operator co_await(awaitable)` 一起参与候选——不是"先找成员再找 ADL"的顺序查找，而是一次统一的重载决议。如果恰好有一个最佳匹配，就用它的返回值作为 awaiter；如果完全找不到 `operator co_await`，那 awaitable 本身就被当作 awaiter——前提是它有 `await_ready`、`await_suspend`、`await_resume` 这三个方法；如果重载决议有歧义（比如成员和非成员都能匹配），程序直接 ill-formed，编译器会报错。

拿到 awaiter 之后，编译器执行以下步骤：

```cpp
if (!awaiter.await_ready()) {
    // 情况 A：需要挂起
    // 保存当前协程状态，然后：
    awaiter.await_suspend(handle);
    // 此时协程已经挂起，控制权返回给调用者/恢复者
}
// 情况 B：不需要挂起（await_ready 返回 true），或恢复时：
auto result = awaiter.await_resume();
// result 就是 co_await 表达式的值
```

你会发现，这三个方法构成了一个精确的"查询-挂起-恢复"协议：

**`await_ready()`** 返回 `bool`。如果返回 `true`，表示"不需要挂起，我已经准备好了"，直接跳到 `await_resume()`。如果返回 `false`，表示"我还没准备好，需要挂起"。这个方法是一个快速路径优化——如果你知道操作已经完成了（比如缓存了结果），直接返回 `true` 就能避免挂起/恢复的开销。

**`await_suspend(handle)`** 在确认需要挂起之后被调用，接收当前协程的 `std::coroutine_handle`。这是整个协议里最灵活的部分——它有三种合法的返回类型。返回 `void` 时协程无条件挂起，控制权返回给调用者或恢复者；返回 `bool` 时，`true` 表示挂起，`false` 表示不挂起（直接 resume），给你一个在最后一刻改变主意的机会；返回 `std::coroutine_handle<>` 时，这就是所谓的 symmetric transfer（对称转移）——协程挂起后不返回给调用者，而是直接恢复返回的那个 handle 对应的协程，这个机制在协程链式调用时非常重要，可以避免栈溢出。后面我们会专门用一小节来拆解这三种形式。

还有一点容易被忽略：如果 `await_suspend` 抛出了异常，协程会被自动恢复，然后异常立刻被重新抛出到协程体内。也就是说，异常不会跑到调用者那里，而是留在协程内部——你可以在协程体里用 `try/catch` 捕获它，或者让它冒泡到 `unhandled_exception()`。

> ⚠️ **`await_ready()` 和 `await_suspend()` 的 bool 语义是反的！** `await_ready()` 返回 `true` 表示"不挂起"，`await_suspend()` 返回 `true` 表示"挂起"。这个设计让很多人在第一次接触时绕了进去。你可以这样记忆：`await_ready` 问的是"准备好了吗"，准备好了当然不用挂起；`await_suspend` 问的是"要不要挂起"，`true` 就是"挂起吧"。

**`await_resume()`** 在协程恢复执行时调用（或者 `await_ready()` 返回 `true` 时立即调用）。它的返回值就是整个 `co_await` 表达式的值。如果你不需要返回任何值，返回 `void` 就行。

### 一个异步计时器 awaiter

说了这么多理论，接下来我们用一个具体的例子把这些东西串起来。我们要实现一个 `SleepAwaiter`——一个让协程"休眠"指定毫秒数的 awaiter。

当然，真正的异步休眠需要事件循环和定时器的配合，这里我们先用一个简化的同步版本来展示 awaiter 的完整结构：

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <thread>

/// 异步休眠 awaiter（同步阻塞版本，仅作演示）
struct SleepAwaiter {
    int kMilliSeconds; // 休眠时长

    explicit SleepAwaiter(int ms) : kMilliSeconds(ms) {}

    // ① 永远需要挂起——因为我们确实需要等待
    bool await_ready() const noexcept { return false; }

    // ② 挂起时执行休眠
    //    返回 void = 无条件挂起，控制权返回调用者
    void await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        // 在实际的事件循环里，这里应该是"注册定时器，把 handle 存起来"
        // 这里简化为直接 sleep，然后恢复
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kMilliSeconds)
        );
        // 休眠结束后立刻恢复协程
        handle.resume();
    }

    // ③ 恢复时不返回任何值
    void await_resume() const noexcept {}
};

/// 期望让 co_await 可以直接接受一个整数（毫秒）
/// 看起来很直觉——但这个写法实际上有问题，见下文分析
SleepAwaiter operator co_await(int ms)
{
    return SleepAwaiter(ms);
}
```

等等，上面这个写法有一个问题：`operator co_await(int ms)` 是一个自由函数，ADL 查找时需要考虑命名空间。对于内置类型 `int`，ADL 不起作用——`int` 没有关联的命名空间。所以更正确的做法是通过 `promise_type` 的 `await_transform` 来拦截：

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <thread>

/// 异步休眠 awaiter
struct SleepAwaiter {
    int kMilliSeconds;

    explicit SleepAwaiter(int ms) : kMilliSeconds(ms) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kMilliSeconds)
        );
        handle.resume();
    }

    void await_resume() const noexcept {}
};

/// 协程任务类型
struct TimerTask {
    struct promise_type {
        TimerTask get_return_object()
        {
            return TimerTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { throw; }

        // ④ await_transform：拦截 co_await 表达式
        //    当你写 co_await 100; 时，编译器会调用这个方法
        SleepAwaiter await_transform(int ms)
        {
            return SleepAwaiter(ms);
        }
    };

    std::coroutine_handle<promise_type> handle;

    ~TimerTask()
    {
        if (handle) {
            handle.destroy();
        }
    }
};

// 使用示例
TimerTask countdown()
{
    for (int i = 5; i > 0; --i) {
        std::printf("倒计时: %d\n", i);
        co_await 1000; // 等待 1 秒（通过 await_transform 转换为 SleepAwaiter）
    }
    std::puts("发射！");
}

int main()
{
    auto task = countdown(); // 协程立即开始执行（initial_suspend 返回 suspend_never）
    // 协程已经执行完毕，因为 SleepAwaiter 在 await_suspend 中同步恢复了自己
    return 0;
}
```

在这个例子中，`await_transform` 扮演了一个"中间人"的角色——它把 `int` 转换成 `SleepAwaiter`。这种模式在实际项目里非常常见：你可以在 `await_transform` 里做类型检查、日志记录、取消检查等等。

### await_suspend 的三种返回形式

接下来问题来了：`await_suspend` 为什么要有三种返回形式？这不是让事情变得更复杂吗？

其实每种形式都有它的用武之地。让我们逐一拆解。

**返回 `void`** 是最简单的——协程挂起，控制权返回给调用者或最近一次 `resume()` 的发起者。这适用于"把挂起这件事完全交给外部管理"的场景，比如把 handle 存到一个队列里，等事件循环稍后来恢复。

**返回 `bool`** 给你一个在挂起和不挂起之间做最后决定的机会。比如你检查了一下，发现 I/O 操作其实已经完成了，就返回 `false` 让协程继续执行，避免无谓的挂起/恢复开销。

**返回 `std::coroutine_handle<>`** 是最强大但也最容易出问题的形式。这就是所谓的 symmetric transfer。当你的 `await_suspend` 返回一个 handle 时，编译器会挂起当前协程，然后**立刻**恢复返回的那个 handle 对应的协程——不会返回到调用者。标准的设计意图是让编译器可以做尾调用优化，从而不增加调用栈深度——主流编译器（GCC、Clang、MSVC）在较高优化级别下确实会这么做。但严格来说，尾调用优化是"实现质量"而非"标准保证"：GCC 和 Clang 都曾有过 symmetric transfer 仍导致栈溢出的 bug（GCC #100897、LLVM #42853）。在实践中，这个机制能可靠地避免栈溢出，但不要在 `-O0` 下依赖它。

来看一个展示 symmetric transfer 的例子：

```cpp
#include <coroutine>
#include <cstdio>

/// 一个简单的任务类型，支持链式执行
struct ChainTask {
    struct promise_type {
        // 存储调用者协程的 handle，等自己结束后恢复它
        std::coroutine_handle<> kCaller;

        ChainTask get_return_object()
        {
            return ChainTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() { return {}; }

        // 结束时通过 symmetric transfer 恢复调用者
        std::coroutine_handle<> final_suspend() noexcept
        {
            return kCaller; // 如果 kCaller 为空，行为是未定义的
        }

        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;

    /// 当 co_await 一个 ChainTask 时，挂起当前协程，启动被 await 的协程
    bool await_ready() noexcept { return false; }

    // symmetric transfer：挂起自己，恢复对方
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> caller) noexcept
    {
        // 存住调用者，等自己结束后恢复它
        handle.promise().kCaller = caller;
        // 返回自己的 handle——调度器会直接恢复这个协程
        return handle;
    }

    void await_resume() noexcept {}
};
```

> ⚠️ **symmetric transfer 是避免协程栈溢出的关键机制。** 如果你的协程 A 调用协程 B，B 调用 C，C 调用 D……每层都是"挂起 A → resume B → 挂起 B → resume C"，如果不用 symmetric transfer，调用栈会越来越深。而 symmetric transfer 让编译器有机会通过尾调用优化来避免栈增长——在协程链比较长的场景下（比如 deep recursive coroutine chains），这一点至关重要。需要注意，尾调用优化是"实现质量"而非"标准保证"，在低优化级别下仍可能出现栈溢出。

## operator co_await 与 ADL

前面我们讲了编译器怎么通过 `operator co_await` 的重载决议从 awaitable 拿到 awaiter。这里有一个实际工程中经常遇到的问题：如果你拿到的是一个第三方库里的类型，你没法修改它的源码，那怎么给它加 `operator co_await`？

答案是利用 ADL（Argument-Dependent Lookup）。重载决议在查找 `operator co_await` 的候选函数时，除了在 awaitable 所属类的作用域里找成员函数，还会通过 ADL 在 awaitable 类型的关联命名空间里查找自由函数。这就给了我们一个不修改原始类型就能扩展其 await 能力的后门。来看一个具体的例子：

```cpp
namespace third_party {
    // 你无法修改的第三方类型
    struct Future {
        // ... 内部实现
    };
}

// 在 third_party 命名空间里添加 operator co_await
// ADL 会找到这个重载
namespace third_party {
    struct FutureAwaiter {
        third_party::Future& kFuture;

        bool await_ready();
        void await_suspend(std::coroutine_handle<> handle);
        int await_resume();
    };

    FutureAwaiter operator co_await(third_party::Future& f)
    {
        return FutureAwaiter{f};
    }
}

// 现在你可以这样写：
third_party::Future fut;
auto result = co_await fut; // ADL 找到 operator co_await
```

这就是 ADL 的威力——你不需要修改原始类型，只需要在它的命名空间里提供一个自由函数 `operator co_await` 重载就行了。当然，如果你能修改类型本身，直接给它加一个成员 `operator co_await()` 更简单。不过要注意一点：如果同一个类型同时存在成员和非成员的 `operator co_await`，而且都能匹配，重载决议会产生歧义，编译器会直接报错。所以不要在同一类型上两种方式都提供。

## 从 awaitable 到调度器

到目前为止，我们所有的 awaiter 都是在 `await_suspend` 里做一些"立即"的事情——要么同步阻塞，要么立刻恢复。但在真正的异步框架里，`await_suspend` 做的事情通常是把协程 handle 提交给某个调度器（事件循环、线程池等），然后让调度器在合适的时机恢复协程。

这就是 awaitable 和调度器之间的桥梁：**`await_suspend` 是调度器集成的关键点**。当协程挂起时，`await_suspend` 拿到了协程的 handle，它可以把这个 handle 存到任何地方——一个队列、一个定时器列表、一个 epoll 事件的数据字段里——然后让调度器稍后来 `resume()` 它。

接下来我们来看一个最小的调度器框架，它展示了一个完整的"协程 + 调度器"是怎么运转的。

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <deque>
#include <functional>

/// 最小调度器——维护一个就绪队列，循环执行
class Scheduler {
public:
    static Scheduler& instance()
    {
        static Scheduler kScheduler;
        return kScheduler;
    }

    /// 把协程 handle 放入就绪队列
    void schedule(std::coroutine_handle<> handle)
    {
        kReadyQueue.push_back(handle);
    }

    /// 运行调度循环，直到队列为空
    void run()
    {
        while (!kReadyQueue.empty()) {
            auto handle = kReadyQueue.front();
            kReadyQueue.pop_front();
            handle.resume();
        }
    }

private:
    std::deque<std::coroutine_handle<>> kReadyQueue;
};

/// 调度器友好的任务类型
struct ScheduledTask {
    struct promise_type {
        ScheduledTask get_return_object()
        {
            return ScheduledTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        // 惰性启动：协程创建时不执行，等调度器来调度
        std::suspend_always initial_suspend() { return {}; }

        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;
};

/// 让出一个时间片——挂起自己，把自己重新放回就绪队列
struct YieldAwaiter {
    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // 核心：把当前协程放回就绪队列，让其他协程先跑
        Scheduler::instance().schedule(handle);
    }

    void await_resume() noexcept {}
};

/// 异步休眠——挂起自己，设定时间后重新入队
/// （这里用 sleep 模拟定时器，实际应该用 epoll + timerfd）
struct AsyncSleepAwaiter {
    int kMilliSeconds;

    explicit AsyncSleepAwaiter(int ms) : kMilliSeconds(ms) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // 在真实调度器里，这里应该注册一个定时器
        // 简化版本：开一个线程来模拟异步定时器
        std::thread([handle, this]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kMilliSeconds)
            );
            // 定时器到期后，把协程放回就绪队列
            Scheduler::instance().schedule(handle);
        }).detach();
    }

    void await_resume() noexcept {}
};

/// 协程函数——交替执行
ScheduledTask producer()
{
    for (int i = 0; i < 3; ++i) {
        std::printf("  [producer] 生产第 %d 个消息\n", i + 1);
        co_await YieldAwaiter{}; // 让出执行权
    }
    std::puts("  [producer] 完成！");
}

ScheduledTask consumer()
{
    for (int i = 0; i < 3; ++i) {
        std::printf("  [consumer] 消费第 %d 个消息\n", i + 1);
        co_await YieldAwaiter{}; // 让出执行权
    }
    std::puts("  [consumer] 完成！");
}

int main()
{
    auto& sched = Scheduler::instance();

    // 创建两个协程（此时都不会执行，因为 initial_suspend 返回 suspend_always）
    auto prod = producer();
    auto cons = consumer();

    // 把它们都放进就绪队列
    sched.schedule(prod.handle);
    sched.schedule(cons.handle);

    std::puts("=== 调度器开始运行 ===");

    // 启动调度循环
    // 两个协程会交替执行：
    // [producer] 生产第 1 个消息 → yield
    // [consumer] 消费第 1 个消息 → yield
    // [producer] 生产第 2 个消息 → yield
    // [consumer] 消费第 2 个消息 → yield
    // [producer] 生产第 3 个消息 → yield
    // [consumer] 消费第 3 个消息 → yield
    // [producer] 完成！
    // [consumer] 完成！
    sched.run();

    std::puts("=== 调度器运行结束 ===");

    // 清理
    prod.handle.destroy();
    cons.handle.destroy();

    return 0;
}
```

这个调度器虽然简陋，但它展示了协程调度的核心模型。`YieldAwaiter` 展示了一个最基本的协作式调度：协程主动让出执行权，把自己放回就绪队列，让其他协程运行。`AsyncSleepAwaiter` 则展示了一个异步定时器的基本模式：挂起协程，设定一个定时器（这里用线程模拟），定时器到期后把协程放回就绪队列。

真正的坑在后面——当我们要把这个调度器和 I/O 多路复用（epoll）结合起来的时候，事情会变得更加复杂，但基本模型是不变的：**awaiter 的 `await_suspend` 负责把协程 handle 提交给调度器，调度器在合适时机 `resume()` 协程**。

## 我们的位置

这篇我们拆解了 C++20 协程的两大定制扩展点。`promise_type` 控制协程的宏观生命周期——怎么创建返回对象、启动时挂不挂起、结束时做什么、返回值和异常怎么处理。awaiter/awaitable 协议控制协程的微观挂起与恢复——`await_ready` 问"准备好了没"，`await_suspend` 在挂起时做操作，`await_resume` 在恢复时拿结果。`await_suspend` 的三种返回形式（void / bool / coroutine_handle）提供了从简单挂起到 symmetric transfer 的渐进式灵活性。最后我们看到，`await_suspend` 是调度器集成的关键点——它把协程 handle 提交给调度器，让调度器决定何时恢复协程。

但到目前为止，我们的调度器还只是一个"就绪队列 + 顺序执行"的玩具。真正的异步 I/O 需要和操作系统的 I/O 多路复用机制接通。下一篇我们要做的事情是：把协程和 epoll（Linux 的 I/O 多路复用）结合起来，构建一个能处理真实网络 I/O 的事件循环。那才是协程真正发光的地方。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch06-async-io-coroutine/`。

## 参考资源

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — C++20 协程的权威参考，包含完整的语言规范
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — 从零搭建协程异步框架的实战文章
- [My Tutorial and Take on C++20 Coroutines — David Mazieres (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html) — 斯坦福教授的协程教程，讲解深入且实用
- [C++ Coroutines: Defining the co_await operator — Raymond Chen (Microsoft)](https://devblogs.microsoft.com/oldnewthing/20191218-00/?p=103221) — 解释 `operator co_await` 的成员函数与自由函数重载、重载决议规则
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/) — 实用指南，包含 `await_ready` 和 `await_suspend` bool 语义差异的提醒
- [C++20 Coroutines — Complete Guide — Simon Toth (ITNEXT)](https://itnext.io/c-20-coroutines-complete-guide-7c3fc08db89d) — 全面介绍协程机制的综合指南
