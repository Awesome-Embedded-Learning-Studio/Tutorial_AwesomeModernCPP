---
title: "线程所有权与 jthread/stop_token"
chapter: 1
order: 3
description: "从 std::thread 的 move-only 所有权出发，手写自动 join 的 JoiningThread，再换到 C++20 的 jthread 与 stop_token 三件套，把协作式取消一次讲全"
tags:
  - host
  - cpp-modern
  - intermediate
  - RAII
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
reading_time_minutes: 20
prerequisites:
  - "std::thread 基础"
  - "线程参数与生命周期陷阱"
related:
  - "mutex 与 RAII 守卫"
  - "线程池：worker 循环与优雅关闭"
---

# 线程所有权与 jthread/stop_token

咱们在 [std::thread 基础](./01-std-thread.md) 的文末说过，接下来咱们要靠 RAII 把 join 自动化，这一篇咱们就动手做它。手头的问题您已经很熟了：`std::thread` 对象析构的时候，如果线程还是 joinable 的，程序就直接死给您看，叫的就是 `std::terminate()`。正常路径上记得 join 倒是不难，难的是异常路径和多个返回分支——中间某一步一抛异常，写在函数末尾的 `t.join()` 就成了永远执行不到的代码。

[上一篇](./02-thread-arguments-and-lifetime.md) 里咱们看过了 detach 走起来是什么下场：引用一悬垂，程序就没了。这一篇咱们把另一条路修好——给线程找一个明确的拥有者，让拥有者的析构函数负责收尾。路上依次过三样东西：`std::thread` 的 move-only 所有权、一个手写的自动 join 类，最后是 C++20 的 `std::jthread` 和它的停止三件套。三件套说的就是 `stop_source`、`stop_token`、`stop_callback` 三个类。全卷讲取消的正源就落在这一篇：往后不管哪一章讲到取消，材料的出处都指着这里，线程池、协程、Actor 全都算在了内。

## thread 是一份只能移动的所有权

咱们从一个基本事实说起：`std::thread` 复制不行，移动倒是随便。cppreference 的原话说得很直白，不存在两个 `std::thread` 对象能代表同一个执行中的线程，它既不是可复制构造的，也不是可复制赋值的，可 move 是允许的。道理不难想：假如复制是允许的，两个对象都会觉得 join 同一条底层线程是自己分内的事，join 完了另一个还蒙在鼓里，语义上根本没法给出一个不吵架的定义。标准索性把所有权规定成唯一的，要交出去就只能整个儿地移交。

既然聊到了所有权，咱们就把 `std::thread` 和 `std::unique_ptr` 归进同一类：资源归一个对象独占，move 一走、源对象就两手空空了。标准库里这样的类型还有一串——`fstream`、`unique_lock`、`future`，个个都是唯一所有者的做派。RAII 的哲学到哪都一样：资源生命周期跟着拥有者走，拥有者析构了，资源也就自动清理了。

```cpp
#include <iostream>
#include <thread>

void worker()
{
    std::cout << "worker running\n";
}

int main()
{
    std::thread t1(worker);
    std::cout << "t1 joinable: " << t1.joinable() << "\n";  // 1

    std::thread t2 = std::move(t1);  // 所有权从 t1 移交给 t2
    std::cout << "t1 joinable: " << t1.joinable() << "\n";  // 0
    std::cout << "t2 joinable: " << t2.joinable() << "\n";  // 1

    t2.join();  // 此后能收尾的只有 t2
    return 0;
}
```

预期行为：move 以前 `t1` 是 joinable 的，move 之后 `t1` 变成了空壳（joinable 为假），`t2` 把线程接了过来（joinable 为真）。咱们若往 `t1` 上调 `join()`，会被拒绝——它已经不代表任何线程了。

<!-- 实验回填：上面这段的实际运行输出（三行 joinable 打印），编译命令与输出原文 -->

### 四个空壳状态

咱们在这里把一件事记下来：`std::thread` 对象在什么情况下，就成了不代表任何线程的空壳。规范列了四种：默认构造之后、被 move 之后、`detach()` 之后、`join()` 之后。被 move 这一例咱们刚刚亲手做过，四种空壳您在 [std::thread 基础](./01-std-thread.md) 的表里也都见过：join 和 detach 都是了结性质的动作，做完之后对象与底层线程再无瓜葛了。

您可别把空壳当成废状态，它恰恰是所有权流转的中转站。后面这几条路您一定见过：工厂函数返回一个线程、容器收纳一批线程、sink 函数接收一个线程，靠的都是 move 出去的一方变空、接手的一方变实。下面咱们把这几条流转路线各看一眼。

### 所有权往哪流：返回、接收、进容器

咱们看往调用方流的那一路：工厂函数把线程造出来、交还给调用方的写法，是完全合法的：

```cpp
std::thread make_worker(int id)
{
    return std::thread(background_task, id);
    // 返回局部的具名对象时也一样：
    // std::thread t(background_task, id);
    // return t;  // C++17 起走 NRVO 或 move，不会退化成拷贝
}
```

您可能会犯嘀咕：thread 不可复制，返回值怎么过得去？答案是返回语句对右值走 move，对具名的局部对象要么被 NRVO（命名返回值优化）直接在调用方的栈上构造、要么走 move，两条路都绕开了拷贝。讲法到这个深度就够用了，拷贝消除的细则不在本篇的射程里。

再看往函数里流的：咱们让参数按值收一个 `std::thread`，调用方用 `std::move` 把所有权交了进去，从这以后线程的收尾就归这个函数管了：

```cpp
void own_the_thread(std::thread t);  // sink：收下就归我管

// 调用方：
// own_the_thread(std::move(t));    // 交出去之后，t 就别再碰了
```

最后看往容器里流的：`std::thread` 是 move-only 的，而 `std::vector` 从 C++11 起就接纳 move-only 元素，所以 `std::vector<std::thread>` 也是完全合法的——ch01/01 文末那批派生线程再逐个 join 的代码，用的骨架就是它。容器这一路咱们留到压轴再展示，等 `jthread` 上了场，咱们再拿它重写一遍，写出来的味道就不一样了。

## 为什么偏偏是 terminate

所有权的问题理清楚之后，咱们回头补问一句为什么：析构一个 joinable 的线程，标准为什么选择了最刺耳的 `std::terminate()`？

标准里其实没写理由，咱们倒是可以从三个选项里读出委员会的取舍。假如析构时静默帮你 join，析构就可能无限期阻塞——析构函数被拖住了，在栈展开里就成了灾难。假如静默帮你 detach，线程就带着对局部变量的引用跑野了，而悬垂引用是未定义行为，咱们宁可要一个响亮的崩溃。不过两头都静默不得，标准就选了第三条路：直接终止，把问题拍在您脸上，逼您显式做出 join 还是 detach 的决定。这与 [std::thread 基础](./01-std-thread.md) 里讲过的设计哲学一脉相承，就是不做隐式的、可能令人惊讶的事情。

代价咱们也见识过了：每一条退出路径都得记得收尾。函数长了、异常多了，靠人肉记忆守 join 是守不住的。修法您其实早就会的——把 join 挪进析构函数，让作用域替咱们记。

## 手写一个自动 join 的线程类

### 持引用的中间形态

最直接的包装，是咱们拿一个 guard 持有 `std::thread` 的引用，析构的时候补上 join：

```cpp
class ThreadGuard {
public:
    explicit ThreadGuard(std::thread& t) : thread_(t) {}
    ~ThreadGuard()
    {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;
private:
    std::thread& thread_;  // 持引用：thread 对象必须在外面活着
};
```

倒是能用，但咱们会觉得有两个别扭的地方。头一个别扭的地方是 thread 对象必须活在 guard 外面、还得活得比 guard 长，顺序反了就是悬垂引用。另一处别扭在 join 完之后：外面那个 thread 对象还在原地，只是空了，线程归谁管这件事在代码里还是读不出来的。Williams 的《C++ Concurrency in Action》第二版里，§2.3 就讨论过线程所有权的转移，书里给出的形态是按值持有 thread 的版本。上面这个持引用的写法是教学上的中间台阶，并不是书里的原样代码。

### 按值收编：JoiningThread

咱们把引用换成按值持有，所有权就彻底清晰了。下面是完整的 `JoiningThread`，请您逐段走一遍：

```cpp
#include <thread>
#include <utility>

class JoiningThread {
public:
    JoiningThread() noexcept = default;

    // 接受任意可调用对象与参数，直接起线程
    template <typename Callable, typename... Args>
    explicit JoiningThread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func),
                  std::forward<Args>(args)...)
    {}

    // 从现成的 std::thread 接管所有权（按值收参，move 进来）
    explicit JoiningThread(std::thread t) noexcept
        : thread_(std::move(t))
    {}

    JoiningThread(JoiningThread&& other) noexcept
        : thread_(std::move(other.thread_))
    {}

    JoiningThread& operator=(JoiningThread&& other) noexcept
    {
        if (this != &other) {
            if (joinable()) {
                join();  // 在接手新线程以前，把手里的旧线程处理掉
            }
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    JoiningThread& operator=(std::thread other) noexcept
    {
        if (joinable()) {
            join();      // 同上：旧的收了尾，再来接新的
        }
        thread_ = std::move(other);
        return *this;
    }

    ~JoiningThread()
    {
        if (joinable()) {
            join();      // 全部的卖点就在这两行
        }
    }

    void join() { thread_.join(); }
    void detach() { thread_.detach(); }
    [[nodiscard]] bool joinable() const noexcept
    {
        return thread_.joinable();
    }
    std::thread& get() noexcept { return thread_; }
    const std::thread& get() const noexcept { return thread_; }

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

private:
    std::thread thread_;
};
```

它的接口跟 `std::thread` 几乎一一对应，多出来的只有析构函数里那个自动 join。用法咱们也就不用另学了：

```cpp
int main()
{
    JoiningThread t1(task, 42);             // 模板构造直接起线程
    JoiningThread t2(std::thread(task, 7)); // 从现成 thread 接管
    // 不需要写任何 join——作用域结束，两个析构函数各自收尾
    return 0;
}
```

有两个细节值得咱们走读一遍。move 赋值里的 `if (joinable()) join();` 是容易被漏掉的一步：赋值是把旧线程换掉，在接手新的以前，手里的旧线程必须有了结，否则它就成了无主线程，等着析构的时候把程序带崩。旧线程了结、新线程接手的节奏，`unique_ptr` 的赋值也是同一个套路。咱们还注意到一个细节：整个类在 C++11 下就能编译，没有任何 C++20 的成分。请您把这个类放在心上，等一下咱们拿它跟 `jthread` 对表，缺的那一项就是标准委员会后来补的东西。

### join() 自己也会抛异常

眼看要圆满了，咱们还有最后一个阴暗的角落要看：`join()` 并不承诺不抛。规范里写明了它可能抛 `std::system_error`，错误条件里列了对自己 join（死锁检测），也有对无效线程的 join，也有对不 joinable 的对象 join。正常的程序里几乎踩不到，可是系统编程的设计里容不下几乎两个字。

麻烦出在析构函数默认就是 `noexcept` 的这一点上。`JoiningThread` 的析构里若 `join()` 真抛了，异常是逃不出去了，程序也就照样栽在 `std::terminate()` 的老路上。咱们也把老兵的务实办法搬过来，给 join 外面包一层 try/catch：异常来了就地吞掉，日志里也留了一笔。

```cpp
~JoiningThread()
{
    if (joinable()) {
        try {
            join();
        }
        catch (const std::system_error& e) {
            // 析构函数不允许把异常抛出去，只能吞下并留痕
            std::fprintf(stderr, "JoiningThread: join() failed: %s\n",
                         e.what());  // 记得 #include <cstdio>
        }
    }
}
```

倒是谈不上优雅，但它是析构函数里唯一站得住的处理方式。后面咱们会看到，`std::jthread` 也没有把这个问题给解决了，它同样在析构里 join、受的约束也一模一样。RAII 收编的是“忘了 join”，收编不了“join 本身失败”。

> 咱们再补一个边角的事实：通用 scope guard 还没进标准，P0052 的提案未被采纳。接力的 P3610 目标定在 C++29，由 Beman 项目的 scope 库承载。Beman 是以 Beman Dawes 命名的标准库候选孵化地，专收瞄准标准化的库，计划提供的 `scope_exit`、`scope_fail`、`scope_success` 加上 `unique_resource` 共四件。在它落地以前，像 `JoiningThread` 这样的专用包装依然是正路。

## jthread：标准补上的自动收尾

### 一行替换

C++20 把这件事收进了标准：`std::jthread`，住的是 `<thread>` 这个头文件。名字里的 j 指 joining，自动 join 就是它的第一卖点。用法上您几乎可以闭着眼睛替换：

```cpp
#include <chrono>
#include <iostream>
#include <thread>

void worker()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "worker done\n";
}

int main()
{
    std::jthread t(worker);  // 类名一换，join 那行删掉，其余原样
    return 0;                // t 析构时自动收尾
}
```

它的行为与 `std::thread` 相同，咱们额外得到两条：析构时自动 join，内部再持有一个 `stop_source` 类型的私有成员，维护一份共享的停止状态（shared stop-state）。后面的半篇都在讲第二条，咱们把第一条的规范原文摆出来——P0660R10 对 `~jthread()` 的措辞是：`If joinable() is true, calls request_stop() and then join().` 翻成咱们的话，析构的时候会做两件事——发出停止请求、然后 join。咱们把它简化成伪代码：

```cpp
// 简化自 P0660R10 的标准措辞
~jthread()
{
    if (joinable()) {
        request_stop();  // 礼貌地问一句：能不能停了？
        join();          // 然后才等
    }
}
```

对照咱们手写的 `JoiningThread`：析构都是 join 收尾，move 是 noexcept 的、赋值的时候也都处理了旧线程。差别恰好一处——标准版在 join 以前多做了 `request_stop()`。手写版等于 jthread 减去了 request_stop，而减掉的这一项，决定了两者在长任务面前的表现。

> **提醒**：如果您照旧教程手写过只做 join 的 guard，可得留个心眼——它析构的时候只会干等。线程若在跑一个停不下来的循环、或者阻塞在长睡眠上，析构就卡在那儿了，程序的表现就是退出时挂住。`request_stop()` + `join()` 的组合才是解药，前提是您的线程函数肯配合，而这正是下面要讲的协作式的分量所在。

### 探测式的 token 注入

`jthread` 内部那个 `stop_source` 是怎么跟您的线程函数搭上线的？答案其实有点巧：构造函数会做一次探测。标准在纸面上用 INVOKE 表达式写明了这次探测：若把内部 token 塞在头一个实参的位置上、调用您的函数还能通过编译（良构这个词的原文是 well-formed），咱们就走注入路线，token 会从参数表的头一个位置传进去。不然的话就原样调用、一个参数都不多给。

```cpp
void modern_worker(std::stop_token token);  // 首参收 token：自动注入
void old_worker(int id);                    // 不收 token：原样调用

std::jthread a(modern_worker);         // token 被注入，可取消
std::jthread b(old_worker, 42);        // 退化成自动 join 线程
```

这个设计给咱们换来了完全的向后兼容：老代码一行不改，照常跑得好好的，只是少了取消的能力。参数这块走的还是 decay-copy 那一套（在构造线程里拷贝一份再交给新线程），细节 [上一篇](./02-thread-arguments-and-lifetime.md) 里已经讲全了，咱们只补一句：拷贝参数时若抛了异常，异常就抛在您调用构造的那一行，新线程连启动的机会都没有。

探测规则倒是还有一个妙用，咱们讲到组控制时会用到：函数显式收 token 但把它排在后面的形参上，注入的形式（token 在首位、实参多一个）跟签名对不上号，探测就自然落到原样调用的分支上，您自己传的 token 就畅通无阻了。三行注释的事，到时候您会看到。

咱们顺带把构造的全貌数一数：`jthread` 的构造重载恰好四个，有 noexcept 的默认构造，得到的是不 joinable、stop_possible 为假的对象，有 noexcept 的 move 构造，有接受可调用对象与参数的模板构造，还有被删除的复制构造。有个事实值得咱们多说一句：没有任何一个构造接收外部的 `stop_source`。您想共享一份停止状态的话，标准给的路只有一条——把 token 当参数自己传。这其实不是疏漏：构造不收外部的 source，取消的开关就攥在调用方手里，委员会把这个主动权留给了您的代码。

### 环境与工具链

三件套住的是 `<stop_token>` 头文件（`jthread` 本体在 `<thread>` 里），咱们拿特性测试宏 `__cpp_lib_jthread == 201911L` 探路比数编译器版本更稳：

```cpp
#if defined(__cpp_lib_jthread)
// jthread 与 stop_token 三件套可用
#endif
```

| 工具链 | 可用版本 |
|---|---|
| GCC（libstdc++） | 10 起 |
| Clang + libstdc++ | 跟随 libstdc++，10 起 |
| Clang + libc++ | LLVM 20 起正式可用（18 起藏在 `-fexperimental-library` 后面） |
| MSVC | 19.28 起（VS 2019 16.8） |

旧资料里流传的“Clang 17 就行”对不上 libc++ 的官方状态页，请您以这一版为准。版本号出自 cppreference 的编译器支持表和 libc++ 的状态页，这些数字往后还会随版本更新而挪动。API 本身的定义出自 P0660R10，jthread 与 stop_token 家族就是它定义的，别的编号您不用记。

## 三件套：stop_source、stop_token、stop_callback

现在咱们把话题转到取消本身。C++20 这套机制的正式名字叫协作式取消（cooperative cancellation），核心思想一句话就讲完了：咱们不杀线程、只递请求，退不退、何时退、退之前收拾什么，全由线程自己说了算。您可以把它想成一面旗子：有人把旗举起来，线程每圈路过的时候瞄上一眼，看见了旗子就收拾东西回家，没有人强按着它的头。

这套机制是三个类构成的，共享同一份停止状态（stop-state）——就是 `jthread` 内部维护的那块。分工咱们列在下面：

- `std::stop_source` 是写端。`request_stop()` 发出停止请求，`get_token()` 从自己派生一个 token。
- `std::stop_token` 是读端。`stop_requested()` 查停止请求发没发出，`stop_possible()` 查这里有没有可能再发出请求。cppreference 称它是停止状态的线程安全"view"——只看，不改。
- `std::stop_callback` 是注册端。构造时挂在一个 token 上，停止请求发出的瞬间执行您给的回调。

咱们手里的一个 source 可以派生任意多个 token，拿来共享的都是同一份状态。token 咱们一般不独立构造，而是从 `jthread` 或 `stop_source` 那里取——您直接手写它的场景很少。

```cpp
#include <iostream>
#include <stop_token>

int main()
{
    std::stop_source source;             // 默认构造：分配一份停止状态
    std::stop_token token = source.get_token();

    std::cout << token.stop_requested() << "\n";  // 0：还没人发出请求

    const bool first  = source.request_stop();    // true：请求由这次调用发出
    const bool second = source.request_stop();    // false：请求早发出去了

    std::cout << first << " " << second << "\n";  // 1 0
    std::cout << token.stop_requested() << "\n";  // 1：token 同步看见
    return 0;
}
```

预期的行为是：请求发出以前 token 查到 0，发出以后查到的是 1，`request_stop()` 的两次调用只有头一次返回 true。值得您记牢的性质有两个。停止请求是单向的、一经发出就不可撤回，标准没有留出反悔的接口。计次是幂等的：多次调用是安全的，但只有实际发出请求的那次返回 true，后来的调用不会再触发一次请求。可见性也有保证——同一停止状态派生出的所有 source 与 token，都看得见这次发出的请求。

<!-- 实验回填：本例实际输出（三行），以及在 request_stop 之后再派生 token 的行为验证 -->

咱们把两个边角讲清楚就翻篇。`stop_possible()` 在两种情况下为假：token 没有关联任何停止状态（默认构造的 token），或者状态还在但既没有收到请求、世上也不再有任何存活的 `stop_source`——source 都没了，请求也就永远发不出来了。这也顺带解释了一件事：`jthread` 析构以前，它 token 的 `stop_possible()` 恒为真，因为内部那个 source 一直活得好好的。另一个边角说的是分配：默认构造 `stop_source` 的时候会分配停止状态、可能抛 `std::bad_alloc`。您确实不需要停止能力的时候，可以用 `std::stop_source(std::nostopstate)` 构造一个不分配的空 source，这个构造是 noexcept 的。

## 实战：轮询、回调、一组线程

API 到手了，剩下的全是怎么把它用对。咱们按轮询、回调、组控制三个典型用法走。

### 轮询：把检查写进循环条件

最朴素的用法，咱们把停止检查写进循环条件：

```cpp
#include <chrono>
#include <iostream>
#include <thread>

void polling_worker(std::stop_token token)
{
    int iteration = 0;
    while (!token.stop_requested()) {  // 每圈查一次停止请求
        process_batch(iteration);      // 完整版见代码仓
        ++iteration;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "processed " << iteration << " batches\n";
}

int main()
{
    std::jthread t(polling_worker);    // token 自动注入
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop();                  // 发出请求：下一圈循环条件就过不去了
    return 0;                          // 析构再请求一次（幂等，无害），然后 join
}
```

适合的场景是迭代短促的计算型循环，一圈也就毫秒级的工夫，停止请求的生效几乎是即时的。您若遇上单次迭代要跑好几秒的任务，咱们就得在迭代体内再设检查点，让等待的上限变短。轮询的边界也要认清：线程若是阻塞在长睡眠或等待上，循环条件根本没机会求值——让停止请求能唤醒阻塞中的线程，靠的是 `condition_variable_any` 与 token 的集成写法，那是 [线程池](../ch05-future-task-threadpool/03-thread-pool.md) 一篇的正源内容，本篇就按住不表了。

<!-- 实验回填：本例实际输出与停止时延的大致量级（从 request_stop 到 worker 退出的间隔） -->

### 回调：停止瞬间的收尾动作

有些收尾的活儿不适合塞在循环退出之后，比如您想立刻关掉一个句柄，或者给监控系统发一条下线的通知。咱们把这件事挂到停止请求发出的瞬间，用的就是 `stop_callback`：

```cpp
#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>

void worker(std::stop_token token)
{
    int counter = 0;
    std::stop_callback cb(token, [&counter] {
        // 注意：这段代码跑在调用 request_stop 的线程上（本例是 main），
        // 不在 worker 自己的线程上
        std::cout << "callback fired, counter = " << counter << "\n";
    });

    while (!token.stop_requested()) {
        ++counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "worker exits\n";
}

int main()
{
    std::jthread t(worker);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop();  // 回调在这里同步执行完毕，这一行才返回
    return 0;
}
```

P0660R10 给 `stop_callback` 写了四条行为保证，咱们挨个过一遍。迟到的注册是头一种情形：请求已经发出以后您才来注册，回调会在注册的线程里、赶在构造函数返回以前立即执行，谁的回调都不会被漏掉。并发竞态是第二种情形：注册与并发的 `request_stop` 之间，不存在回调凭空丢失的中间态。要么由看见请求的注册方自己执行，要么由看见了注册的请求方、在 `request_stop` 返回以前替它执行，两头总有一头把活儿接下了。最违反直觉的在于执行位置：回调同步执行在调用 `request_stop` 的那个线程上，而不是工作线程——所以回调必须短小伶俐，重活、会阻塞的活放进去，卡住的就是发出停止请求的一方。回调要是以异常退了出去，咱们等来的就是 `std::terminate()`。最后一条管的是注销：`stop_callback` 的析构函数把回调注销，而且不会阻塞等待其他回调执行完。回调若恰好跑到当前线程里来了，析构同样不等——不然就成了自己等自己。咱们也没法复制它、移动它，它就老老实实地待在注册处守着。

> 规范还给咱们写了同步子句：真正发出请求的那次 `request_stop`（返回 true 的那次）与看见它的 `stop_requested` 之间、回调注册与回调执行之间，都有 synchronizes-with 的保证——谁对谁可见、何时可见，正式的名字叫 happens-before，完整的定义住在 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 那一篇里，本篇就不越界了。

<!-- 实验回填：本例实际输出顺序（回调打印与 worker exits 两行的先后），以及把回调改为打印线程 id 后的对照 -->

### 组控制：一个 source 停一组线程

在真实的系统里，咱们常见的往往是一组线程共进退：一个按钮按下去，整组就一起收工了。`stop_source` 派生多个 token 的能力正好接住这个需求。下面的写法来自 cppreference 的官方示例，咱们照着学一遍：

```cpp
#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

void worker_fun(int id, std::stop_token stoken)
{
    // token 排在形参表后面：给自动注入让路的机关就在这里——
    // 注入形式是 f(token, id, 外部token) 三个实参，与本签名对不上，
    // 探测退到"原样调用"，咱们显式传的 token 畅通无阻
    while (!stoken.stop_requested()) {
        std::cout << "worker " << id << " is working\n";
        std::this_thread::sleep_for(200ms);
    }
    std::cout << "worker " << id << " exits\n";
}

int main()
{
    std::stop_source source;    // 外部的 source，停止权握在 main 手里
    std::jthread threads[4];

    for (int i = 0; i < 4; ++i) {
        // token 作为尾参显式传入：四个线程共享同一份停止状态
        threads[i] = std::jthread(worker_fun, i + 1, source.get_token());
    }

    std::this_thread::sleep_for(1s);
    source.request_stop();  // 一次请求，四个线程一起看见
    return 0;  // 数组析构：每个 jthread 各自收尾（内部 source 的请求
               // 没人理会，等于多发一次；join 逐个照做）
}
```

咱们把这里的门道看清：token 是显式传进去的，于是每个 `jthread` 内部的那个 source 就全程闲置了，真正掌握停止权的是外部共享的 `source`。析构照样按规范走：每个 jthread 对自己的内部 source 调 `request_stop()`，这个请求没人理会、等于多发了一次、然后照常 join。该做的收尾一件不落，真正发停止请求的主动权却始终攥在 `main` 手里。

<!-- 实验回填：本例实际输出（四个 worker 的 working/exits 交错与停止时机） -->

### vector\<jthread\> 与 parallel_for_each

容器路线现在可以补上了。咱们把 ch01/01 文末那批派生线程、逐个手动 join 的骨架拿出来，元素统一换成 `jthread` 的写法：

```cpp
#include <algorithm>
#include <thread>
#include <vector>

template <typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func func,
                       unsigned thread_count)
{
    const std::size_t length = std::distance(first, last);
    if (length == 0) {
        return;
    }
    if (thread_count == 0) {
        // hardware_concurrency() 只是提示值，讲法见 ch01/01
        thread_count = std::thread::hardware_concurrency();
    }
    if (thread_count == 0) {
        // 查询也会失手（返回 0）：兜成 1，不然下面的
        // thread_count - 1 在无符号数上回绕成巨值
        thread_count = 1;
    }

    const std::size_t block_size = length / thread_count;
    std::vector<std::jthread> threads;
    threads.reserve(thread_count);  // 容量一次给足，扩容搬移的动静省了

    Iterator block_start = first;
    for (unsigned i = 0; i < thread_count - 1; ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        threads.emplace_back([block_start, block_end, &func] {
            std::for_each(block_start, block_end, func);
        });
        block_start = block_end;
    }

    std::for_each(block_start, last, func);  // 最后一块调用方自己算，
                                             // 少开一个线程
    // 函数返回，vector 析构：逐个元素析构，逐个 join，零手动收尾
}
```

咱们跟旧骨架一对比，差异全在收尾上：那个手动 join 的循环没有了。vector 的析构会逐个销毁元素，每个 `jthread` 的析构各自做一轮 request_stop，本例的 worker 不看 token，这些请求全落了空，跟着把 join 也做了，函数一返回就全都收尾了。`reserve` 顺手把扩容问题也料理了——真发生搬移也不怕，jthread 是 move-only 的、move 又带 noexcept，vector 也就搬得动了。分块的思路照旧：每线程一块、最后一块留给调用方自己算，省一个线程的开销。这里咱们还提前堵了一处隐患：`hardware_concurrency()` 查不出来的时候会返回 0，不兜住的话，`thread_count - 1` 就在无符号数上回绕出一个大得吓人的数字，循环也就跟着失控了。所以代码里兜成了 1，哪怕最后串行地跑完，收场也是干净的。

<!-- 实验回填：与串行 std::for_each 的结果对拍（正确性），以及不同 thread_count 下的耗时对比（性能数字一律现场测） -->

## 练习

### 练习 1：可选 detach 的 JoiningThread

给本文的 `JoiningThread` 加一个 `cancel_join()`：调用之后析构改为 detach 而不是 join。想一想：这个方法适合在什么前提下调用？线程已经跑完但还没 join 时调它，之后会发生什么？请您写一个小程序验证自己的判断。做完再对照 `jthread`：它没有提供任何跳过 join 收尾的写法，您觉得委员会为什么不做？

### 练习 2：给组控制装上回调

在组控制示例的基础上，请您给每个 worker 注册一个 `stop_callback`，回调里打印自己的编号，观察谁响应了停止请求。然后咱们故意在其中一个回调里 `sleep_for(1s)`，看看 `source.request_stop()` 那一行被拖了多久——回调同步执行在请求方线程的保证，落的正是这一行。思考题：四个回调的打印顺序有保证吗？

### 练习 3：parallel_accumulate

请您仿照 `parallel_for_each` 实现 `parallel_accumulate(first, last, init)`：把范围分成 N 块，每块用一个 `jthread` 求各自的部分和、最后再汇总。别忘了最后一块留给调用方自己算，还要处理 `hardware_concurrency()` 可能返回 0 的情况。咱们把结果与 `std::accumulate` 对拍。想一想：这里需要 mutex 吗？给您的提示：每个线程写的是自己的局部和，汇总等 join 了以后才做，mutex 的正源在 [mutex 与 RAII 守卫](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md) 那儿，到时候您再回来对照。

## 下一步

所有权怎么流转、join 怎么自动化、停止请求怎么发出怎么收，咱们在这一篇里都讲全了。您要是往 ch02 走，[mutex 与 RAII 守卫](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md) 会把共享数据的同步补上。往 ch05 去的话，[线程池](../ch05-future-task-threadpool/03-thread-pool.md) 会把这一章攒下的能力组装成生产级的形态。动手的路线在 [练习体系](../exercises/)：Lab 00 的 bonus 题目，正好请您用 `jthread` 把手写的自动 join 改造一遍。想看这一章在全卷的位置，您回 [卷地图](../) 瞄一眼就行。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`。

## 参考资源

- [P0660R10: Stop Token and Joining Thread](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0660r10.pdf) —— jthread 与 stop_token 家族的单一涵盖提案
- [std::jthread — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [std::stop_source — cppreference](https://en.cppreference.com/w/cpp/thread/stop_source)
- [std::stop_token — cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [std::stop_callback — cppreference](https://en.cppreference.com/w/cpp/thread/stop_callback)
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [libc++ C++20 状态页](https://libcxx.llvm.org/Status/Cxx20.html) —— P0660R10 于 LLVM 20 Complete
- [Williams, *C++ Concurrency in Action*, 2nd ed, Manning, 2019](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition) —— §2.3 线程所有权转移
