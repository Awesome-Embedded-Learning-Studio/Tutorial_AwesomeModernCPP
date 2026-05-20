---
title: "C++20 协程基础"
description: "深入 C++20 协程的语法、状态机模型与生命周期管理，理解 co_await/co_yield/co_return 的编译器变换"
chapter: 6
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
  - 异步编程
difficulty: intermediate
platform: host
reading_time_minutes: 30
cpp_standard: [20]
prerequisites:
  - "异步编程演进：从回调地狱到协程"
related:
  - "promise_type 与 awaitable"
  - "异步 I/O 与事件循环"
---

# C++20 协程基础

上一篇我们看到协程让异步代码看起来像同步代码——线性流程，没有嵌套，没有回调金字塔。那篇的重点是"为什么需要协程"和"协程长什么样"，我们只是展示了最终效果，但没解释它背后到底发生了什么。这一篇我们要把协程从里到外拆清楚：编译器对协程函数做了什么变换？协程帧里存了什么？`coroutine_handle` 是怎么管理协程生命周期的？这些问题的答案构成了理解 C++20 协程的基础。

先说一句大实话：C++20 协程的学习曲线是比较陡的。它不是一个"学了 `co_await` 就能用"的特性——你需要理解 promise_type、coroutine_handle、awaitable、awaiter 这些概念之间的协作关系，才能真正写出正确的协程代码。但好消息是，这些概念之间的关系是固定的，一旦你理解了这套模型，所有协程代码都是同一个模式的变体。我们这一篇的目标就是把这个模型讲透。

## 环境

本篇所有代码在 GCC 12+、Clang 15+、MSVC 19.34+ 上编译通过，这三个编译器都提供了完整的 C++20 协程支持。平台方面没有特殊依赖，Linux、macOS、Windows 都可以跑——我们只用到纯标准库。编译选项方面，`-std=c++20` 是必须的；GCC 12 之前的版本可能还需要额外加一个 `-fcoroutines` 标志，但 GCC 12+ 已经默认启用了。有一点要提前说明：本篇会大量使用 `<coroutine>` 头文件，它是 C++20 协程的库支持部分，提供 `std::coroutine_handle`、`std::suspend_always`、`std::suspend_never` 这些基础设施。

## 三个关键字：co_await、co_yield、co_return

C++20 为协程引入了三个关键字，它们各有分工，但有一个共同效果：只要函数体内出现了这三个关键字中的任何一个，编译器就会把那个函数当作协程来处理。不需要额外的声明、属性或修饰符——关键字本身就是信号。

`co_await` 是最核心的一个。它出现在你需要"等一等"的地方——挂起当前协程，让出执行权，等某个异步操作完成后恢复执行。`co_await expr` 的语义是：把 `expr` 当作一个可等待对象（awaitable），通过它来判断是否需要挂起、如何挂起、以及恢复后返回什么值。我们来看一个最简单的例子：

```cpp
#include <coroutine>
#include <iostream>

// 最简协程返回类型
struct SimpleTask
{
    struct promise_type
    {
        SimpleTask get_return_object()
        {
            return SimpleTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
};

// 一个简单的协程
SimpleTask demo_coroutine()
{
    std::cout << "第一步：协程开始执行\n";

    // co_await std::suspend_always{} 挂起协程
    co_await std::suspend_always{};

    std::cout << "第二步：协程恢复后继续执行\n";

    co_await std::suspend_always{};

    std::cout << "第三步：协程再次恢复\n";
}

int main()
{
    std::cout << "主线程: 启动协程\n";

    // 调用协程函数，返回 SimpleTask
    SimpleTask task = demo_coroutine();

    // 因为 initial_suspend 返回 suspend_never，
    // 协程会立刻执行到第一个 co_await
    std::cout << "主线程: 协程已挂起，手动恢复\n";
    task.handle.resume();

    std::cout << "主线程: 再次恢复\n";
    task.handle.resume();

    std::cout << "主线程: 协程执行完毕\n";
    task.handle.destroy();
    return 0;
}
```

运行输出是这样的：

```text
主线程: 启动协程
第一步：协程开始执行
主线程: 协程已挂起，手动恢复
第二步：协程恢复后继续执行
主线程: 再次恢复
第三步：协程再次恢复
主线程: 协程执行完毕
```

你会发现，`demo_coroutine()` 被调用后，它并不会一口气执行完毕——每遇到一个 `co_await std::suspend_always{}`，协程就挂起，控制权回到 `main()`。当我们调用 `task.handle.resume()` 时，协程从上次的挂起点继续往下执行。`std::suspend_always` 是标准库提供的一个最简 awaitable，它的 `await_ready()` 总是返回 `false`，意思是"永远需要挂起"。对应地，`std::suspend_never` 的 `await_ready()` 总是返回 `true`，意思是"永远不挂起"。

`co_yield expr` 用于产出一个值并挂起协程。它等价于 `co_await promise.yield_value(expr)`。`co_yield` 是构建生成器（generator）的基础——每次产出一个值，协程挂起，消费者取走值后再恢复。我们后面会从零实现一个 generator。

`co_return` 用于结束协程。它有两种形态：`co_return;`（无返回值）和 `co_return expr;`（带返回值）。前者等价于调用 `promise.return_void()`，后者如果 `expr` 的类型非 void 则等价于 `promise.return_value(expr)`，如果 `expr` 的类型是 void 也调用 `promise.return_void()`。`co_return` 不同于普通 `return`——普通 `return` 语句不能出现在协程中，协程必须用 `co_return` 来结束（或者让函数体自然结束，编译器会在末尾隐式插入一个 `co_return;`）。

> ⚠️ 注意，`co_return` 和普通 `return` 不能混用。一个函数如果包含了 `co_await`、`co_yield` 或 `co_return` 中的任何一个，它就是协程，此时函数体内的普通 `return` 语句是非法的——编译器会直接报错。反过来，如果函数体内没有任何 `co_*` 关键字，即使返回类型里定义了 `promise_type`，它也只是普通函数。

## 编译器做了什么——协程状态机

这是理解 C++20 协程的核心。当你写下一个协程函数时，编译器并不会像处理普通函数那样直接生成一段线性代码。它会把整个协程函数**变换成一个状态机**——每个 `co_await`（包括初始挂起点和最终挂起点）都是一个状态，协程每次恢复时根据当前状态跳到对应的代码位置继续执行。

我们用一个简化的例子来跟踪这个变换过程。假设你写了这样一个协程：

```cpp
SimpleTask example(int x)
{
    int a = x + 1;
    co_await std::suspend_always{};
    int b = a + 2;
    co_await std::suspend_always{};
    co_return;
}
```

编译器大致会把它变换成类似这样的伪代码（简化了很多细节，但核心逻辑是对的）：

```text
1. 分配协程帧（coroutine frame）
2. 把参数 x 拷贝到协程帧里
3. 在协程帧里构造 promise_type 对象
4. 调用 promise.get_return_object() 拿到返回值
5. co_await promise.initial_suspend()
6. 进入状态机:

   状态 0:（初始状态）
     a = x + 1
     保存当前挂起点为"状态 1"
     co_await std::suspend_always{}
     → 挂起，返回到调用者

   状态 1:（从第一个 co_await 恢复）
     b = a + 2
     保存当前挂起点为"状态 2"
     co_await std::suspend_always{}
     → 挂起，返回到调用者

   状态 2:（从第二个 co_await 恢复）
     调用 promise.return_void()
     销毁局部变量 b, a
     co_await promise.final_suspend()
     → 最终挂起
```

我们来逐行理解这个变换。

**第一步，分配协程帧。** 协程帧是一块堆内存（通常情况），用来存储协程恢复执行所需的所有数据。它包含以下几个部分：函数参数的副本（因为协程可能比调用者的栈活得更久，所以参数必须拷贝到帧里，避免悬垂引用）、局部变量（生命周期跨越挂起点的那些局部变量——如果一个局部变量在 `co_await` 之前创建、之后还要使用，它就必须存在协程帧里）、promise 对象（协程状态的一部分）、以及当前的挂起点索引（让恢复时知道该跳到哪个状态）。

> ⚠️ 只有生命周期跨越挂起点的局部变量才会被存入协程帧。如果某个局部变量在两次挂起之间就完成了生命周期，编译器可以把它优化到寄存器或普通栈上。这个优化是由编译器自行决定的。

**第二步，拷贝参数。** 所有按值传递的参数都会被移动或拷贝到协程帧里。按引用传递的参数只保存引用本身——这意味着如果你传了一个局部变量的引用给协程，而那个局部变量在协程恢复前就失效了，你就会得到一个悬垂引用。这是 C++20 协程的一个经典坑：**协程按引用捕获参数是危险的**，因为你无法保证引用的对象在协程恢复时还活着。

**第三步，构造 promise 对象。** `promise_type` 是协程的"内省接口"——编译器在协程执行的各个关键节点都会调用 promise 的方法。它不是一个普通的概念，而是编译器通过 `std::coroutine_traits` 从协程的返回类型推导出来的。如果你的返回类型是 `Task`，那编译器会去找 `Task::promise_type`。

**第四步，调用 `get_return_object()`。** 这个方法的返回值就是协程函数返回给调用者的那个对象（我们例子里的 `SimpleTask`）。这个调用发生在协程函数体开始执行之前——也就是说，调用者拿到返回值时，协程函数体的第一行代码还没执行。

**第五步，调用 `initial_suspend()`。** 这个方法决定协程在函数体开始执行之前是否挂起。如果返回 `std::suspend_always`（懒启动），协程在执行第一行代码之前就挂起，调用者必须手动 `resume()` 才能让它开始干活。如果返回 `std::suspend_never`（急启动），协程会立刻开始执行函数体，直到遇到第一个 `co_await`。

**第六步，执行函数体并处理挂起点。** 协程执行函数体，遇到 `co_await` 时，先调用 awaitable 的 `await_ready()`。如果返回 `true`，不需要挂起，直接继续。如果返回 `false`，保存当前状态（挂起点索引、活跃的局部变量），调用 `await_suspend(handle)`，然后挂起——控制权交还给调用者或恢复者。当协程被 `resume()` 时，从保存的挂起点恢复，调用 `await_resume()` 拿到 `co_await` 表达式的返回值，然后继续往下执行。

**最终状态：`final_suspend()`。** 当协程执行到 `co_return`（或函数体结束），它会调用 `promise.return_void()` 或 `promise.return_value()`，销毁所有活跃的局部变量，然后调用 `promise.final_suspend()` 并 `co_await` 其结果。这个 `final_suspend` 是协程的"终点站"——如果它返回 `std::suspend_always`，协程在最终状态挂起，等待外部通过 `coroutine_handle::destroy()` 销毁协程帧。如果它返回 `std::suspend_never`，协程帧会自动销毁——但你必须确保此时没有任何人还持有这个协程的 `coroutine_handle`，否则就是 use-after-free。

## coroutine_handle：协程帧的句柄

`std::coroutine_handle<>`（或其特化版本 `std::coroutine_handle<Promise>`）是协程帧的非拥有句柄。你可以把它理解为一个"裸指针"——它指向协程帧，但不管协程帧的生死。

最常用的操作是 `resume()`，它恢复协程执行，从上次的挂起点继续。但有一个前提条件：协程必须还没有到达最终挂起状态。如果 `done()` 已经返回 `true` 了，再调 `resume()` 就是未定义行为——在某些编译器上可能恰好不崩，换个优化级别可能直接段错误。`destroy()` 则是销毁协程帧：它会依次调用 promise 的析构函数、参数的析构函数，然后释放协程帧的内存。`done()` 用来检查协程是否已经到达最终挂起点——也就是说，函数体是否已经执行完毕并处于 `final_suspend` 状态。还有一个静态方法 `from_promise(promise)`，它可以从 promise 对象的引用反推出对应的 `coroutine_handle`，这在 promise_type 的方法里非常常用，因为你经常需要在 promise 的方法里拿到自己的 handle 来传递给外部。

我们用一个完整的示例来展示 `coroutine_handle` 的基本操作：

```cpp
#include <coroutine>
#include <iostream>

struct Resumable
{
    struct promise_type
    {
        Resumable get_return_object()
        {
            return Resumable{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        // 懒启动：协程创建后立刻挂起，不执行函数体
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;

    // RAII: 析构时自动销毁协程帧
    ~Resumable()
    {
        if (handle) {
            handle.destroy();
        }
    }
};

Resumable countdown(int from)
{
    while (from > 0) {
        std::cout << "  countdown: " << from << "\n";
        --from;
        co_await std::suspend_always{};   // 每次循环后挂起
    }
    std::cout << "  countdown: 发射!\n";
}

int main()
{
    std::cout << "创建协程...\n";
    Resumable task = countdown(5);

    // 因为 initial_suspend 返回 suspend_always，
    // 协程还没开始执行

    std::cout << "开始恢复协程:\n";
    while (!task.handle.done()) {
        task.handle.resume();
        if (!task.handle.done()) {
            std::cout << "  (协程已挂起，可以干别的事)\n";
        }
    }

    std::cout << "协程已完成\n";
    // Resumable 的析构函数会调用 handle.destroy()
    return 0;
}
```

运行输出：

```text
创建协程...
开始恢复协程:
  countdown: 5
  (协程已挂起，可以干别的事)
  countdown: 4
  (协程已挂起，可以干别的事)
  countdown: 3
  (协程已挂起，可以干别的事)
  countdown: 2
  (协程已挂起，可以干别的事)
  countdown: 1
  countdown: 发射!
协程已完成
```

你看，协程每次循环到 `co_await std::suspend_always{}` 就挂起，回到 `main()`。`main()` 可以检查 `done()` 来判断协程是否完成，然后决定是继续 `resume()` 还是去做别的事情。这就是协程和普通函数的根本区别：普通函数要么在执行，要么已经返回；协程可以"暂停"——挂起后不是消失了，而是状态完整地保存在协程帧里，随时可以恢复。

这里有一个非常重要的细节：`coroutine_handle` 是非拥有的。它不会在析构时自动销毁协程帧。如果你拿到了一个 `coroutine_handle` 但从来没有调用 `destroy()`，协程帧就会泄漏——那块堆内存永远不会被释放。所以你几乎总是要把 `coroutine_handle` 包装在一个 RAII 类里（就像我们上面的 `Resumable`），让析构函数自动处理清理工作。

> ⚠️ `coroutine_handle` 的 `resume()` 和 `destroy()` 都不应该在协程已经 `done()` 之后调用。对已完成的协程调 `resume()` 是未定义行为——在你的代码里可能恰好"不崩溃"，但在另一个编译器或优化级别下可能直接段错误。

## 协程的生命周期

协程的生命周期从它被调用那一刻开始，到它的协程帧被销毁那一刻结束。我们来完整走一遍这个过程。

**创建阶段**。当你调用一个协程函数时，编译器生成的代码会先分配协程帧，然后拷贝参数、构造 promise、调用 `get_return_object()`。此时协程函数体还没开始执行——调用者已经拿到了返回对象（里面包含 `coroutine_handle`），但协程的"真正执行"还要等 `initial_suspend()` 的结果。

**执行阶段**。如果 `initial_suspend()` 返回 `suspend_never`，协程会立刻开始执行函数体，直到遇到第一个真正的 `co_await`（`await_ready()` 返回 `false` 的那个）。如果返回 `suspend_always`，协程在函数体开始之前就挂起，等待外部调 `resume()`。在执行过程中，每次遇到 `co_await` 且需要挂起时，协程保存当前状态然后返回控制权给调用者或恢复者。

**结束阶段**。当协程执行到 `co_return`（或函数体末尾，前提是 promise 有 `return_void()`），它调用 `promise.return_void()` 或 `promise.return_value()`，销毁局部变量，然后调用 `promise.final_suspend()`。这是一个关键设计点：**`final_suspend()` 应该返回 `std::suspend_always`**。

为什么 `final_suspend` 要返回 `suspend_always`？因为如果它返回 `suspend_never`，协程帧会在 `final_suspend` 返回后立刻被自动销毁——此时协程函数体已经结束，局部变量已经销毁，但外部可能还持有 `coroutine_handle`。如果外部不知道协程已经自动销毁了，再去调 `resume()` 或 `destroy()` 就是 use-after-free。返回 `suspend_always` 让协程在最终状态挂起，协程帧还活着，外部可以通过 `done()` 检测到完成，然后安全地调 `destroy()` 销毁协程帧。

> ⚠️ 悬挂问题（dangling coroutine）是协程最常见的 bug 之一。典型场景：你返回了一个包含 `coroutine_handle` 的对象，但调用者拿到后没有妥善管理它的生命周期——要么忘记调 `destroy()` 导致内存泄漏，要么在协程帧已经被销毁后继续使用 `coroutine_handle`。最佳实践是始终用 RAII 包装 `coroutine_handle`，不要让它裸露在 API 边界之外。

## 从零实现一个 Generator

很好，现在我们对协程的基本机制有了理解。接下来我们要做一件非常实用的事情：从零实现一个可以用 `co_yield` 产出整数值的 generator。这个实现会涉及到 `promise_type`、`coroutine_handle`、`co_yield` 的完整配合，是理解 C++20 协程的绝佳练习。

我们分三步来构建这个 generator。先搭骨架——让 generator 能用 `co_yield` 产出值、外部能迭代取值。然后加异常处理——让协程里的异常能正确传播到外部。最后加 RAII——确保协程帧在 generator 析构时被正确销毁。

### 第一步：骨架——能产出、能取值

```cpp
#include <coroutine>
#include <iostream>
#include <memory>

template<typename T>
class Generator
{
public:
    // ---- promise_type：编译器通过它定制协程行为 ----
    struct promise_type
    {
        T current_value;    // 存储 co_yield 产出的值

        Generator get_return_object()
        {
            // 从 promise 创建 coroutine_handle，包装成 Generator 返回
            return Generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // 初始挂起：协程创建后立刻挂起（懒启动）
        // 调用者需要手动 resume 才开始产出值
        std::suspend_always initial_suspend() { return {}; }

        // 最终挂起：协程结束后挂起，等待外部 destroy()
        // 不能返回 suspend_never，否则协程帧会自动销毁
        // 外部的 handle 就变成悬垂指针了
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield expr 等价于 co_await promise.yield_value(expr)
        // 我们把值存到 current_value 里，然后挂起
        std::suspend_always yield_value(T value)
        {
            current_value = value;
            return {};   // 返回 suspend_always，挂起协程
        }

        // 协程没有 co_return 或 co_return; 时调用
        void return_void() {}

        // 未处理的异常——先简单 terminate
        void unhandled_exception() { std::terminate(); }
    };

    // ---- 迭代器接口 ----

    // 恢复协程，移动到下一个 yield 点
    bool next()
    {
        handle_.resume();
        return !handle_.done();
    }

    // 获取当前 yield 的值
    T value() const
    {
        return handle_.promise().current_value;
    }

    // ---- 构造/析构/移动 ----

    explicit Generator(std::coroutine_handle<promise_type> handle)
        : handle_(handle)
    {
    }

    ~Generator()
    {
        if (handle_) {
            handle_.destroy();
        }
    }

    // 禁止拷贝——coroutine_handle 不能共享所有权
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    // 允许移动
    Generator(Generator&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;    // 防止 other 析构时 destroy
    }

    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

private:
    std::coroutine_handle<promise_type> handle_;
};
```

我们先来梳理这段代码的逻辑。`promise_type` 是编译器和协程之间的桥梁。当编译器看到你的协程函数返回 `Generator<int>` 时，它会去找 `Generator<int>::promise_type`，然后在协程执行的各个关键节点调用 promise 的方法。

`get_return_object()` 是第一个被调用的——它创建 `coroutine_handle` 并包装成 `Generator` 返回给调用者。`initial_suspend()` 返回 `suspend_always`，这意味着协程在执行函数体之前就挂起了——调用者拿到 Generator 后，必须调 `next()`（内部调 `resume()`）才会开始产出值。这是 generator 的标准设计：**懒启动**，因为 generator 的消费者可能只需要前几个值，没必要在创建时就产出所有值。

`yield_value(T value)` 是 `co_yield` 背后的实际操作。当协程执行到 `co_yield 42` 时，编译器把它变换成 `co_await promise.yield_value(42)`。我们的实现把值存到 `current_value` 里，然后返回 `suspend_always`——协程挂起，控制权回到 `next()` 的调用者。调用者通过 `value()` 读取 `current_value`，然后再次调 `next()` 继续产出下一个值。

`final_suspend()` 返回 `suspend_always`，这一点我们前面解释过了——协程结束后保持挂起状态，等待外部的 Generator 析构函数调 `destroy()`。

Generator 本身是 `coroutine_handle` 的 RAII 包装。析构函数调 `destroy()` 销毁协程帧，移动构造/赋值通过 `nullptr` 标记来防止双重销毁，拷贝被禁止因为 `coroutine_handle` 不支持共享所有权。

现在我们来用它产出斐波那契数列：

```cpp
Generator<int> fibonacci()
{
    int a = 0, b = 1;
    while (true) {
        co_yield a;         // 产出当前值，挂起
        int temp = a + b;
        a = b;
        b = temp;
    }
    // 这个协程永远不会 co_return——无限序列
}

int main()
{
    auto gen = fibonacci();

    std::cout << "斐波那契数列前 15 项:\n";
    for (int i = 0; i < 15 && gen.next(); ++i) {
        std::cout << "  fib(" << i << ") = " << gen.value() << "\n";
    }

    // gen 析构时自动 destroy 协程帧
    return 0;
}
```

运行输出：

```text
斐波那契数列前 15 项:
  fib(0) = 0
  fib(1) = 1
  fib(2) = 1
  fib(3) = 2
  fib(4) = 3
  fib(5) = 5
  fib(6) = 8
  fib(7) = 13
  fib(8) = 21
  fib(9) = 34
  fib(10) = 55
  fib(11) = 89
  fib(12) = 144
  fib(13) = 233
  fib(14) = 377
```

你会发现 `fibonacci()` 函数看起来就是一个普通的生成斐波那契数列的循环——唯一的区别是 `co_yield` 替代了 `return` 或 `push_back`。但这个函数不会一口气跑完：每次 `co_yield` 产出一个值后就挂起，等 `next()` 调用时才继续循环。这就是惰性求值（lazy evaluation）——值按需产出，不需要预先计算和存储所有结果。对于无限序列或大数据集，这个特性非常有价值。

### 第二步：加入异常处理

上面的 generator 有一个问题：如果协程函数体里抛了异常怎么办？目前我们的 `unhandled_exception()` 直接 `std::terminate()`，这太粗暴了。更好的做法是把异常捕获并存储起来，等到外部调 `next()` 或 `value()` 时重新抛出：

```cpp
template<typename T>
class SafeGenerator
{
public:
    struct promise_type
    {
        T current_value;
        std::exception_ptr exception;   // 存储异常

        SafeGenerator get_return_object()
        {
            return SafeGenerator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T value)
        {
            current_value = value;
            return {};
        }

        void return_void() {}

        // 捕获异常，存到 exception_ptr 里
        void unhandled_exception()
        {
            exception = std::current_exception();
        }
    };

    bool next()
    {
        handle_.resume();

        // resume 之后检查是否有异常
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }

        return !handle_.done();
    }

    T value() const
    {
        return handle_.promise().current_value;
    }

    explicit SafeGenerator(std::coroutine_handle<promise_type> handle)
        : handle_(handle)
    {
    }

    ~SafeGenerator()
    {
        if (handle_) {
            handle_.destroy();
        }
    }

    SafeGenerator(const SafeGenerator&) = delete;
    SafeGenerator& operator=(const SafeGenerator&) = delete;

    SafeGenerator(SafeGenerator&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    SafeGenerator& operator=(SafeGenerator&& other) noexcept
    {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

private:
    std::coroutine_handle<promise_type> handle_;
};
```

`unhandled_exception()` 现在把异常捕获到 `std::exception_ptr` 里。`next()` 在 `resume()` 之后检查 `exception`——如果有异常，就通过 `std::rethrow_exception` 重新抛出。这样外部代码就可以用 `try/catch` 来处理协程里的异常了：

```cpp
#include <iostream>
#include <stdexcept>
#include <string>

SafeGenerator<int> risky_range(int max)
{
    for (int i = 0; i < max; ++i) {
        if (i == 7) {
            throw std::runtime_error("7 是不吉利的数字!");
        }
        co_yield i;
    }
}

int main()
{
    auto gen = risky_range(15);

    try {
        while (gen.next()) {
            std::cout << "  值: " << gen.value() << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "  捕获异常: " << e.what() << "\n";
    }

    return 0;
}
```

运行输出：

```text
  值: 0
  值: 1
  值: 2
  值: 3
  值: 4
  值: 5
  值: 6
  捕获异常: 7 是不吉利的数字!
```

异常从协程内部传播到了外部的 `catch` 块——跟同步代码的异常行为完全一致。这就是协程的优雅之处：异步代码不仅写起来像同步代码，连错误处理都跟同步代码一样。

### 第三步：支持 range-for 循环

真正的 generator 应该支持 range-for 循环。这需要我们提供一个迭代器类型和 `begin()`/`end()` 方法。我们把这个加到 `SafeGenerator` 里：

```cpp
// 在 SafeGenerator 类里添加：

class Iterator
{
public:
    // 必须提供 iterator_category、value_type 等类型别名
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    Iterator() : generator_(nullptr) {}

    explicit Iterator(SafeGenerator* gen) : generator_(gen)
    {
        // 初始时先前进到第一个值
        advance();
    }

    T operator*() const
    {
        return generator_->value();
    }

    Iterator& operator++()
    {
        advance();
        return *this;
    }

    void operator++(int) { advance(); }

    bool operator==(const Iterator& other) const
    {
        return generator_ == other.generator_;
    }

    bool operator!=(const Iterator& other) const
    {
        return !(*this == other);
    }

private:
    SafeGenerator* generator_;
    bool exhausted_ = false;

    void advance()
    {
        if (!generator_->next()) {
            exhausted_ = true;
            generator_ = nullptr;   // 迭代结束，变成 end()
        }
    }
};

Iterator begin()
{
    return Iterator(this);
}

Iterator end()
{
    return Iterator();
}
```

现在你可以用 range-for 来遍历 generator 了：

```cpp
SafeGenerator<int> squares(int n)
{
    for (int i = 1; i <= n; ++i) {
        co_yield i * i;
    }
}

int main()
{
    std::cout << "前 8 个完全平方数:\n";
    for (int val : squares(8)) {
        std::cout << "  " << val << "\n";
    }
    return 0;
}
```

运行输出：

```text
前 8 个完全平方数:
  1
  4
  9
  16
  25
  36
  49
  64
```

`for (int val : squares(8))` 展开后，等价于调用 `begin()` 拿到迭代器，每次循环调 `operator++()`（内部调 `next()`），用 `operator*()` 取值，直到 `operator==()` 返回 `true`（协程完成，迭代器变成 end）。整件事跟遍历一个 `std::vector` 的写法完全一样，但底层是惰性求值的协程。

> ⚠️ 这个迭代器是**单次使用**的（input iterator）——迭代一遍之后不能回头。这是因为 `coroutine_handle` 只能前进不能后退。如果你需要多次遍历，就得重新创建一个 generator。这也意味着 `Iterator` 不满足 ForwardIterator 的要求——不要对它做 `std::sort` 之类的需要多次遍历的操作。

## 我们的位置

这一篇我们把 C++20 协程的内部机制拆了个七七八八。三个关键字——`co_await` 挂起等待、`co_yield` 产出值并挂起、`co_return` 返回并结束——它们的出现让编译器知道这个函数是协程，并触发一系列变换。编译器把协程函数变换成一个状态机：分配协程帧存储参数、局部变量和 promise 对象，每个 `co_await` 是一个状态切换点，协程挂起时保存当前状态、恢复时跳到对应位置继续执行。`coroutine_handle` 是协程帧的非拥有句柄，提供 `resume()`、`destroy()`、`done()` 操作——它不管协程帧的生死，所以你必须用 RAII 包装它。`final_suspend()` 应该返回 `suspend_always`，这样协程结束后保持挂起状态，外部可以安全地检测 `done()` 并调用 `destroy()`。我们从零实现了一个完整的 generator，逐步加入了异常处理和 range-for 支持——这个实现涵盖了 promise_type、coroutine_handle、co_yield 的完整协作。

但到目前为止，我们用的 awaitable 都是标准库提供的 `std::suspend_always` 和 `std::suspend_never`，或者自己写的简单结构体。真正的异步编程需要更灵活的 awaitable——比如等待一个 I/O 操作完成、等待一个定时器超时、等待另一个协程的结果。这就涉及到 awaitable/awaiter 的定制机制：`await_ready()`、`await_suspend()`、`await_resume()` 三个方法各自的语义和返回值类型，以及 `await_suspend` 返回 `bool`、`void` 或 `coroutine_handle` 时的不同行为。这些内容我们会在下一篇展开——那是协程从"理解机制"到"实际使用"的关键一步。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch06-async-io-coroutine/`。

## 参考资源

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines)
- [Coroutine support library — cppreference](https://en.cppreference.com/w/cpp/coroutine)
- [Understanding the Compiler Transform — Lewis Baker](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)
- [C++ Coroutines: Understanding operator co_await — Lewis Baker](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)
- [Coroutine Theory — Lewis Baker](https://lewissbaker.github.io/2017/09/25/coroutine-theory)
- [My tutorial and take on C++20 coroutines — Dima Korolev (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
- [C++20's Coroutines for Beginners — Andreas Fertig (CppCon 2022)](https://www.youtube.com/watch?v=8sEe-4tig_A)
- [Coroutine changes for C++20 and beyond (WG21 P1745R0)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1745r0.pdf)
