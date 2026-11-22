---
title: "谁负责线程的生命周期"
description: "看清 std::thread 的 move-only 所有权与 join 的三条失败路径，用 thread_guard 把收尾写进析构函数。"
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
---

# 谁负责线程的生命周期

好了！您别真忘记我们要做什么了！我们现在要处理的是：join 写在函数末尾，异常一来就被跳过，谁来负责兜底嘞？

答案揭晓：交给析构函数。

## 所有权与 RAII：thread_guard 与异常安全

整篇要用的家伙就一件，您在卷二用过的 RAII（资源获取即初始化）：把资源的收尾写进析构函数，对象离开作用域自动执行，异常路过也不漏。

### 线程对象自己也是资源

动手管它之前，先答一个问题：`std::thread t(work);` 这一句之后，`t` 自己是个什么东西？答案：一份资源，背后是操作系统的一条线程。而这份资源只认一个所有者——只能 move，不能拷贝。咱们上手拷了一回：

<OnlineCompilerDemo
  title="std::thread 不可拷贝"
  source-path="code/examples/vol5/chapter01/30_thread_not_copyable.cpp"
  description="拷贝构造被删除，编译不过；错误写法留在注释里，所有权只能 std::move。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// std::thread 不可拷贝:拷贝构造被删除,编译不过
// 错误写法留在注释里,放开后报错见正文;所有权只能 move
#include <chrono>
#include <print>
#include <thread>

void work() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

int main() {
    std::thread a(work);
    // std::thread b = a;  // 编译不过
    std::thread b = std::move(a); // 所有权转移
    b.join();
    std::println("a joinable after move: {}", a.joinable());
}
```

</OnlineCompilerDemo>

```text
error: use of deleted function ‘std::thread::thread(const std::thread&)’
```

拷贝构造被删除是刻意设计，因为一条操作系统线程**同一时刻只能被一个对象管理**，允许拷贝就会出现两个对象抢着 join 同一条线程的混乱局面。

所以线程没法拷，所有权只能 move。move 完了，源对象跟线程算什么关系？看 joinable：

<OnlineCompilerDemo
  title="joinable 与 move：转移后源为空"
  source-path="code/examples/vol5/chapter01/31_joinable_move.cpp"
  description="move 之后再问 joinable，源线程已是 false，新对象接管执行流。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// joinable 与 move:转移之后,源线程不再关联执行流
#include <print>
#include <thread>

void work() {}

int main() {
    std::thread t1(work);
    std::println("t1 joinable: {}", t1.joinable());
    std::thread t2 = std::move(t1);
    std::println("after move: t1 joinable {}, t2 joinable {}", t1.joinable(), t2.joinable());
    t2.join();
}
```

</OnlineCompilerDemo>

跑出来 `t1 joinable: 1`，move 之后 `t1 joinable 0`、`t2 joinable 1`——源对象彻底交了权。这套形状您不陌生，`std::unique_ptr` 就是它，卷二的老朋友。差别在收尾：`unique_ptr` 赋值时释放旧资源，`std::thread` 在目标还 joinable 时直接 terminate（这个暴脾气下面专门有一节）。标准库里的 `std::fstream`、`std::future` 也都是这一家的。

### 异常路径上的 join：thread_guard

第一处：异常跳过 join。《第一个 std::thread 程序》里咱们见过两种 terminate——没 join 的一种，线程函数里抛异常的一种。第三种还没见过：主线程自己抛异常，把写在后面的 `join()` 跳过去。您想想那个画面：join 写在函数最后一行，中间某一行炸了，栈展开（异常沿调用栈逐层退出、沿途析构局部对象的过程）走到那个还没 join 的线程对象跟前——照样 terminate。

解法《第一个 std::thread 程序》的结尾就预告过：那个在析构里自动等待的包装器。这里给持有引用、不拥有线程的版本（默认在析构里 join）。书里管它叫 thread_guard，咱们代码里写成 ThreadGuard，同一个东西的两种拼法。Anthony Williams 的《C++ Concurrency in Action》给过这个经典形态，咱们照它的推进思路写了自己的版本，连同验证程序一起放在这里：

<OnlineCompilerDemo
  title="ThreadGuard：异常也收得了场"
  source-path="code/examples/vol5/chapter01/32_thread_guard.cpp"
  description="栈展开时守卫对象的析构函数替您 join，异常穿过也不丢线程。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// ThreadGuard:栈展开时析构函数替您 join,异常也收得了场
#include <chrono>
#include <print>
#include <stdexcept>
#include <thread>

class ThreadGuard {
  public:
    enum class Action { kJoin, kDetach };
    explicit ThreadGuard(std::thread& t, Action action = Action::kJoin)
        : thread_(t), action_(action) {}
    ~ThreadGuard() {
        if (thread_.joinable()) {
            if (action_ == Action::kJoin)
                thread_.join();
            else
                thread_.detach();
        }
    }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

  private:
    std::thread& thread_; // 持有引用，不拥有线程
    Action action_;
};

void risky() {
    throw std::runtime_error("boom");
}

void run_with_guard() {
    std::thread t([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::println("  worker finished its work");
    });
    ThreadGuard guard(t);
    risky(); // 抛出，栈展开时 guard 的析构函数 join 线程
}

int main() {
    try {
        run_with_guard();
    } catch (const std::exception& e) {
        std::println("caught: {}", e.what());
    }
    std::println("main continues normally");
}
```

</OnlineCompilerDemo>

跑出来三行，次序有点意思，您先自己读一遍再往下看：

```text
  worker finished its work
caught: boom
main continues normally
```

`risky()` 抛出异常，`run_with_guard` 的栈开始展开，`guard` 的析构函数被调用，join 阻塞了 150 毫秒等 worker 干完活，然后异常才被 main 的 catch 接住。要是把 guard 拿掉，这个程序会以 terminate 收场，worker 的输出都没有机会打出来。您靠着 guard，把异常路径上的 join 写进了析构函数，它不再靠您记得调用，作用域退出时析构函数自己执行。

这个版本是个半成品，两个弱点摆在明面上。第一，guard 持有的是引用，线程的所有权还在外面的 `t` 手里，`t` 和 `guard` 谁管收尾，代码里没有答案。第二，被引用的 thread 对象必须比 guard 活得久，它要是早一步销毁（堆上 delete、成员销毁顺序之类），还 joinable 的话，thread 自己的析构当场 terminate，轮不到 guard 析构；已经 join 过或 detach 过的话，guard 的析构去读的就是已经销毁的对象，未定义行为。这一版只证明了一件事：析构函数接得住 join。所有权归谁，它没答，这个悬念先挂着。

### join 自己也会抛

第二处：join 自己会抛。您可能觉得，join 不就是等吗，等还能等出事？还真能。cppreference 在 [join 的页面][cppref-join] 写明，出错时抛 `std::system_error`（装着错误码的异常，`code()` 取出错误码，`message()` 给出文字）。它列了三种条件，`joinable()` 为假、线程不存在、线程 join 自己。最后这个最扎眼，条件名叫 `resource_deadlock_would_occur`。三种里咱们挑两种实测：重复 join 一种，join 自己一种：

<OnlineCompilerDemo
  title="join 的错误路径：system_error"
  source-path="code/examples/vol5/chapter01/33_join_throws.cpp"
  description="join 自己抛 resource_deadlock_would_occur，重复 join 抛 invalid_argument，都能接住。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// join 的错误路径:join 自己、重复 join,各抛一个 system_error
#include <chrono>
#include <print>
#include <system_error>
#include <thread>

std::thread* g_self = nullptr;

void self_joiner() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    try {
        g_self->join(); // 线程 join 自己
    } catch (const std::system_error& e) {
        std::println("  self join threw: {}", e.code().message());
    }
}

void work() {}

int main() {
    std::thread t(work);
    t.join();
    try {
        t.join(); // 已经 join 过，joinable() 为假
    } catch (const std::system_error& e) {
        std::println("  second join threw: {}", e.code().message());
    }
    std::thread s(self_joiner);
    g_self = &s;
    s.join();
    std::println("main continues");
}
```

</OnlineCompilerDemo>

```text
  second join threw: Invalid argument
  self join threw: Resource deadlock avoided
main continues
```

第二行那个 `Resource deadlock avoided` 是 EDEADLK 这个 errno 的直译（资源死锁已避免），念着别扭，意思就是线程 join 自己，被实现当场拦下了。麻烦跟着就到：咱们刚把 join 写进析构，而析构函数不该往外抛异常——析构执行中再有一个异常逸出就是 terminate。所以务实的写法是把 join 包进 try，失败记一笔，不往外抛。真写出来长这样，`thread_` 是包装器手里那个 `std::thread`：

> 哦别急着抄，更加近代版本的C++20已经把jthread做进标准库了。下一篇我们就来介绍这个东西！
> 看这个更多是理解发生了什么。

<OnlineCompilerDemo
  title="JoiningThread 的析构：聚焦"
  source-path="code/examples/vol5/chapter01/34_joining_thread_dtor.cpp"
  description="joinable 才 join，失败也不让异常逃出析构函数——自动 join 的全部要点在这几行。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// JoiningThread 的析构:focus 在析构函数上——joinable 才 join,失败不逃出析构
#include <chrono>
#include <print>
#include <system_error>
#include <thread>
#include <utility>

class JoiningThread {
  public:
    explicit JoiningThread(std::thread t) noexcept : thread_(std::move(t)) {}

    ~JoiningThread() {
        if (thread_.joinable()) { // 如果是可以被join的
            try {
                thread_.join();
            } catch (const std::system_error& e) {
                std::println("join failed in destructor: {}", e.what());
            }
        }
    }

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

  private:
    std::thread thread_;
};

int main() {
    std::println("entering scope");
    {
        std::thread raw([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::println("  worker done");
        });
        JoiningThread t(std::move(raw));
        std::println("scope body, nobody calls join by hand");
    } // 析构在这里替我们 join
    std::println("scope left, worker already joined");
}
```

</OnlineCompilerDemo>

### 裸 thread 的 move 赋值是 terminate

第三处：move 赋值。前头 move 构造一路畅通，赋值这里埋着一发大的：目标对象还关联着线程（`joinable() == true`）的时候被 move 赋值，标准直接规定调 `std::terminate()`（[operator= 的页面][cppref-opassign]，C++11 起）。听着夸张，咱们实测，您盯着退出码：

<OnlineCompilerDemo
  title="move 赋值给已关联的线程：terminate"
  source-path="code/examples/vol5/chapter01/35_move_assign_terminate.cpp"
  description="目标对象还 joinable 就被赋值，std::terminate 直接收场，本示例预期崩溃。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// move 赋值给已关联线程的对象:std::terminate 直接收场,本示例预期崩溃
#include <chrono>
#include <print>
#include <thread>

void slow_work() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

int main() {
    std::println("starting two threads, then move-assign over a joinable one");
    std::thread t1(slow_work);
    std::thread t2(slow_work);
    t2 = std::move(t1); // t2 还关联着线程
}
```

</OnlineCompilerDemo>

编译运行，标准错误就一行，退出码 134。兄弟们老熟人了，《第一个 std::thread 程序》里咱们算过 128 加 6 这笔数，SIGABRT：

```text
terminate called without an active exception
```

裸 `std::thread` 到这里露全了面：move 构造畅通，move 赋值给还没收尾的对象就当场终止。三处会出事的地方走完了，回头看是一条线上的三段：异常跳过 join，join 自己会抛，赋值 terminate，全都是"收尾这件事押在您的记性上"押出来的。出路也早就指过了：交给一个接管所有权的包装器，析构里收尾，赋值前先收旧的。C++20 干脆把这条路铺进了标准库，`std::jthread` 的析构先请求停止再 join，move 赋值先收旧的再接新的。它就是下一篇的主角。

## 参考资源

本篇写作时对照借鉴了 Anthony Williams 的《C++ Concurrency in Action》（Manning 出的第二版），thread_guard 的推进思路借自它，那个聚焦析构的 JoiningThread 片段也是照它的路子写的，例子与文字都是我自己写的。涉及 C++ 行为的断言，均按文中标注编译实测或查证 cppreference，thread 与 thread_guard 相关设施自 C++11 起可用，jthread 与停止协议是 C++20。

- [std::thread::~thread（cppreference）][cppref-dtor]，joinable 状态下析构调用 terminate 的规定。
- [std::thread::join（cppreference）][cppref-join]，同步保证与 system_error 的三种错误条件。
- [std::thread::operator=（cppreference）][cppref-opassign]，joinable 状态下 move 赋值调用 terminate 的规定。
- [std::thread::detach（cppreference）][cppref-detach]，分离语义。
- [std::jthread（cppreference）][cppref-jthread]，C++20 的自动 join 线程与停止协议。
- [C++ Core Guidelines CP.24][cp24]与[CP.25][cp25]，把线程当作全局容器看待、优先 gsl::joining_thread 的两条工程建议，与咱们手写的 JoiningThread 是同一个思路（GSL 的 README 注明该类型尚未实现）。

[cppref-dtor]: https://en.cppreference.com/w/cpp/thread/thread/~thread
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[cppref-opassign]: https://en.cppreference.com/w/cpp/thread/thread/operator%3D
[cppref-detach]: https://en.cppreference.com/w/cpp/thread/thread/detach
[cppref-jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[cp24]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp24-think-of-a-thread-as-a-global-container
[cp25]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp25-prefer-gsljoining_thread-over-stdthread
