---
title: "手写 task<T>：惰性任务与对称转移"
chapter: 6
order: 2
description: "从 vol4 留下的急切 Task 复盘起笔，手写惰性 Task<T>：值、异常、续体三条通道一次接通，再看对称转移的栈真相与工程替代"
tags:
  - host
  - cpp-modern
  - advanced
  - coroutine
  - 异步编程
difficulty: advanced
platform: host
cpp_standard: [20]
reading_time_minutes: 26
prerequisites:
  - "C++20 协程基础"
related:
  - "协程取消"
  - "事件循环与定时器"
---

# 手写 task<T>：惰性任务与对称转移

[上一篇](./01-coroutine-basics.md) 里咱们把三个关键字和 promise 的六个钩子走查了一遍，协程机制的地基算是打好了。[线程池那篇](../ch05-future-task-threadpool/03-thread-pool.md) 的文末也预告过：协程要真正干活，缺的是一个像样的任务类型。这一篇咱们就在地基上盖房子，要亲手写的家伙是 `Task<T>`：协程函数的返回值、协程帧的生命周期，从此有了明确的归属对象。值要从 `co_await` 的手里交回来，异常也得有自己的路可走，任务跑完了之后，等它的人还能接着往下执行。

它在全卷的分量不轻，算得上六大正源里的一席，[下一篇](./03-coroutine-cancellation.md) 的取消、[事件循环](./04-event-loop-and-timers.md) 的调度，咱们都要在它上面接着搭。咱们把它写结实了，后面的文章直接链接过来，只写各自的差异。

说来咱们也不是头一回写 Task。vol4 讲协程的时候，笔者写过一版，倒是能跑，留下的毛病却实打实。咱们把它请出来过一遍，您对新房子的砖从哪砌起也就清楚了。

## vol4 那版 Task 留下了什么

当时的演示长这样：每个 SimpleReader 的 `await_suspend` 都会开一个 detached 线程，线程睡了一秒、把值填成 1，再用 `handle.resume()` 把协程拉了回来。咱们看 main 这边：拿到了 Task 对象就打印了一句结果，接着守着全局的 `quit_flag` 忙等：

```cpp
int main() {
    auto result = task();   // 协程当场开跑，跑到第一个 co_await 挂起，控制流退回这里
    std::println("Result here: {}", result.value());   // 打出 0！
    while (!quit_flag)
        ;   // 忙等协程完工
    std::println("Result here: {}", result.value());   // 这回才是 3
}
```

您看，打印出来的头一行是 `Result here: 0`。协程里明明写了 `co_await`，main 却赶在它完工前头把 0 印了出来。这个 0 就是第一处毛病的现场：**没有任何人真的在等**。那版 Task 的 `initial_suspend` 返回的是 `suspend_never`，协程一被调用就跑了起来，跑到头一个 `co_await` 处就挂起了，控制流原路退回了 main，打印自然抢在了恢复前头。急切（eager）启动的 Task 就是这个性格：创建即执行，落在它手里的 `co_await` 更像空转，等的人没等成，被等的反倒提前跑完了。

咱们再看第二处毛病，它出在值的来路上。协程里的 `int tol = co_await reader1;` 接到了 1，可 main 手里的 `result.value()` 读的是另一个地方：promise 里塞了一个 `shared_ptr<int>`，写入的一头是 `return_value`，读取的一头是 main，两头再靠 `quit_flag` 的旗子对表。值没有从 `co_await` 的表达式走回来，走的是共享状态的侧门。演示是够用了，组合却组合不起来：两个协程想互相等对方的结果，靠旗子对表是凑不出来的。

第三处毛病算是最小的，也最要命：空着身子的 `unhandled_exception() {}`。协程体里真抛了异常，也会被这个空实现悄无声息地吞掉，咱们站在调用方的位置上，是连一声都听不见的。

于是这一篇的活就定下来了：写一个惰性（lazy）的 `Task<T>`，它的创建不等于执行，等 `co_await` 来了才启动。值与异常都从 `co_await` 的通道交回来，任务结束的时候，控制权交还给等它的执行流。咱们一块砖一块砖地砌，砌完了拿一条三层调用链验收。

## 编译环境：规范一层，实测一层

到了动笔编译的时候，咱们得把编译器的口径对齐。规范层的事实来自 GCC 官方的语言状态页和 cppreference 的支持表：GCC 的协程语言支持自 GCC 10 就有，GCC 10 上您得多写一面 `-fcoroutines` 旗子。到了 GCC 11，`-std=c++20` 自动就把协程带上了，旗子就不用写了。Clang 这边走的 `-std=c++20` 就是正路，旧教程里流传的 `-fcoroutines-ts` 是协程进标准前的旧名字，Clang 17 已经把它拆了。MSVC 那边咱们记 19.28（VS 2019 16.8）就行，比它早的 19.0 只算部分支持。您嫌版本号记不住的话，特性测试宏是更稳的选择：编译器一侧看 `__cpp_impl_coroutine`，库的一侧看 `__cpp_lib_coroutine`，两个宏的取值都认 `201902L`。

实测层是咱们自己的环境声明：本卷代码究竟在哪台机器、哪个编译器上真跑过，这件事等实验回填的时候落在那儿。顺带纠正一句流传甚广的说法：协程并不需要 GCC 13。您拿 GCC 11 配上 `-std=c++20` 去编译本篇的代码，这一关是过得去的。把某一台机器的实测环境当成规范门槛，就是版本号谣言的标准配方。

<!-- 实验回填：本卷协程代码的实测环境声明（编译器版本、平台、编译命令与通过情况） -->

## promise 骨架：三个新成员

[上一篇](./01-coroutine-basics.md) 里咱们把六个钩子的职责走查了一遍，这里直接看 `Task<T>` 的 promise，它比教学用的 SimpleTask 多了什么。多出来的三个成员，一人认领了一处旧毛病：

```cpp
template <typename T>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        T value{};              // 值住进 promise，不再走侧门
        std::exception_ptr exception;   // 异常也住进 promise
        std::coroutine_handle<> continuation = std::noop_coroutine();

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_value(T v) { value = std::move(v); }
        void unhandled_exception() noexcept { exception = std::current_exception(); }
    };
    // Task 本体见下文
};
```

`value` 和 `exception` 的意思直白：协程的产出不再走侧门，一律都存进了 promise，等取的人上门。另一张新面孔的名字叫 `continuation`，中文的译名叫续体（continuation），指的是这个任务结束后、下一个该接着跑的执行流。具体到咱们这儿，它存的是那个正在 `co_await` 咱们这个任务、等它结果的父协程句柄。类型用了擦除过的 `coroutine_handle<>`，因为父协程的 promise 类型是什么，咱们不知道，其实也不需要知道。初始值给的是 `std::noop_coroutine()`，一个恢复后立刻返回的空转句柄，它的用场等讲到根任务的时候您就看到了。

`get_return_object` 的执行时机值得单独看一眼：它在协程体开跑前就被调用，而且必须如此。协程帧连同 promise 的存活期可能撑不到 `resume()` 返回的那一刻，返回对象的构造就得赶在销毁发生前完成，交到调用者的手里。函数体里的那行 `from_promise(*this)`，走的就是从 promise 反取句柄的路子。还请您留意，能这么干的只有带 Promise 类型参数的主模板，擦除形态的 `coroutine_handle<>` 上是没有它的，所以咱们在这里写的句柄类型是具体的 `handle_type`。

### initial_suspend：把创建即执行改掉

咱们把 `initial_suspend` 从 vol4 的 `suspend_never` 换成 `suspend_always`。协程被调用之后就停在了起跑线上，而函数体一个字都没执行，控制权直接还给了调用者。什么时候跑？被 `co_await` 的时候。这一改的幅度看着小，性质全变了：Task 变成了一份创建出来但还没启动的执行计划，启动的时机由拿到它的代码说了算。上一篇的生成器靠的正是同一手，咱们不调 `begin()`，协程就一动不动地待着。至于协程帧本体的堆分配，编译器在能证明生命周期不出逃的时候，可以把它叠进调用者的栈帧（HALO：heap allocation elision，省略堆分配的优化），那篇已经讲过了，这里就不重复了。

### return_value：值进 promise

写下 `co_return tol;` 的那一刻，触发的是 `return_value(tol)`，值就搬进了 promise 存着。咱们还该记下一个边角：在协程体一路执行到底、一个 `co_return` 都没写的情况下，语言对它的处理等同于一句 `co_return;`，要走 `return_void()` 的路。咱们的 promise 只提供了 `return_value`，真把函数体写成了那样，按 CWG 2556 的追溯裁决它是非良构。CWG 是标准委员会下属的核心工作组，管的就是语言层的缺陷报告。规范层的非良构，不等于编译器真的会给诊断。笔者在 GCC 16.2.1 下试过：它一声不吭地编过去了，也照常跑完了，而 `return_value` 从头到尾没人调用，值就这么缺失了，其实比拒收更隐蔽。带值的 `Task<T>` 用 `return_value`，不带值的 `Task<void>` 用 `return_void`，所以 `void` 得另开一个走 `return_void` 的特化，本篇的演示马上就要用到它。咱们把完整实现收在配套代码里，免得它淹了正文的主线。

### final_suspend：noexcept 是编译期门槛

协程体跑完了、`return_value` 收完尾，下一个被 co_await 的就是 `final_suspend`。咱们不返回 `suspend_always`，而是返回一个自制的 `FinalAwaiter`。道理留到对称转移的部分再讲，到时候您就全看清了，这里要办的是另一件事：`noexcept`。

规范对 `final_suspend` 的要求是必须声明为 `noexcept`，少了它，程序就成了非良构，编译期直接就拒收了。道理您想一下就通了：协程体都执行完了，在 `final_suspend` 里再抛出异常的话，抛给谁？已经没有合理的接收方了。标准库自己的协程类型（C++23 的 `std::generator` 就是）全都把它声明成 `noexcept`。笔者把它删掉编了一遍，GCC 的报错原文如下：

```text
error: the expression 'BadTask::promise_type::final_suspend' is required to be non-throwing
note: must be declared with 'noexcept(true)'
```

报错的措辞里连补救方法都给了：必须声明成 `noexcept(true)`。还有一个对照您可以留意：`initial_suspend` 并没有那样的硬性要求，不过咱们的实现同样声明了 `noexcept`，启动阶段的异常语义另有它的讲究，留到异常的部分再讲。

### FinalAwaiter：完工之后把控制权交出去

`final_suspend` 的返回值是要被 co_await 的，所以它得是个像模像样的 awaiter，咱们得把 `await_ready`、`await_suspend`、`await_resume` 三个函数配齐。笔者给它起的名字叫 `FinalAwaiter`：

```cpp
struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template <typename Promise>
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<Promise> h) noexcept {
        return h.promise().continuation;   // 把控制权交给等我的人
    }
    void await_resume() const noexcept {}
};
```

`await_suspend` 干的事只有一件：把存在 promise 里的续体句柄交出去。具体怎么个交法，等到对称转移的部分就是正题。这里咱们还得交代清楚一件事：根任务是没有人等它的。根任务的 `continuation` 会一直是初始值，咱们给的初始值是 `std::noop_coroutine()`，P0913 提案专门为兜底场景造的小件，典型的实现就一条 `ret` 指令，恢复它的效果等于原地返回。咱们把空句柄交出去恢复才是未定义行为，初始值给了 noop，交出去的就永远是一个能安全恢复的句柄。

### Task 本体：一份只能移动的所有权

咱们再往外看 promise 之外，Task 的本体管的是协程帧的所有权：

```cpp
    explicit Task(handle_type h) noexcept : handle_(h) {}
    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;   // move 置空，防二次销毁
    }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) { handle_.destroy(); }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~Task() {
        if (handle_) { handle_.destroy(); }
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
```

这套写法您在 [线程所有权那篇](../ch01-thread-lifecycle-raii/03-thread-ownership-and-jthread.md) 见过同款：资源归一个对象独占，一旦 move 走了，源对象就两手空空了。Task 管的资源是协程帧，析构函数里的一声 `destroy()`，promise、参数副本、帧本体就都跟着销毁了。`destroy()` 的规范前置条件是协程必须处于挂起状态，在别的状态下调用它就是未定义的行为。咱们手上的 Task 恰好天然满足：惰性启动保证了它只有三种状态，没启动的时候停在初始挂起点，跑起来了会挂在某个 co_await 上，跑完了就停在最终挂起点。只要咱们还没销毁它，它总归是挂着的。

这里有一处时序值得咱们当场对一遍。`co_await co_add(1, 2);` 语句结束的时候，右值的临时 Task 析构，那一刻子协程已经停在了最终挂起点：值存好了，父协程也恢复了，此刻的帧还挂着，`destroy()` 是合法的。父协程的 Task 活得更久，由最外层的拥有者负责收尾，本篇里是咱们等会儿写的 `sync_wait`，换成 [事件循环那篇](./04-event-loop-and-timers.md) 的调度循环也一样。帧的销毁由拥有者负责，这件事咱们在代码上一眼就能读出来。

## Task 自己当 awaiter

promise 侧齐了，现在咱们接通另一半。`co_await co_add(1, 2);` 里被等的对象是一个 Task。上一篇讲过编译器找 awaiter 的顺序：Task 没有提供自己的 `operator co_await`，它的身上直接长着 `await_ready`、`await_suspend`、`await_resume` 三个函数，那它自己就是 awaiter 了。三个函数咱们挨个看：

```cpp
    bool await_ready() const noexcept {
        return !handle_ || handle_.done();
    }
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().continuation = awaiting;   // 续体登记
        return handle_;
    }
    T await_resume() {
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
        return std::move(handle_.promise().value);
    }
```

咱们得对 `done()` 多说两句。`done()` 问的是协程有没有停在最终挂起点，它的前置条件同样是句柄得指着挂起中的协程。惰性 Task 刚造好的时候停在初始挂起点，`done()` 的答案是 false，照常走挂起的路线，这一路是没有问题的。那什么时候会是 true？父协程去等一个已经完工的任务的时候。真到了那一步，咱们可不能对停在最终挂起点的协程调 `resume()`，那就是明晃晃的未定义行为，cppreference 的页面写得明明白白。短路把路封死了：已经完工的任务不再走挂起，而是直接从 `await_resume` 取值。还有一个对照您可以记下：`final_suspend` 返回 `suspend_never` 的协程永远到不了最终挂起点，`done()` 对它的答案永远是 false。上一篇的生成器判产完，靠的正是反过来用 `suspend_always` 让 `done()` 变 true。

咱们写的 `await_suspend` 只有短短的两行，而且两行里都有戏。编译器把父协程的句柄作为参数递进来，咱们的头一个动作是登记：把它存进自己那个活在协程帧里的 promise。为什么存 promise 不存 Task？因为此刻眼前的 Task 是个右值临时对象，语句一结束它就析构了，父句柄放在它身上就等于放在了流沙上。promise 能活到任务真正销毁的那一刻，就可靠得多了。第二个动作是返回自己的句柄 `handle_`，这是给编译器的暗号：当前协程挂起了之后，控制权不还给 resume 的调用者，直接转给返回的句柄。这个动作的大名叫对称转移（symmetric transfer，`await_suspend` 返回句柄触发的控制权交接），咱们把它留到下半场专讲。

咱们给 `await_resume` 排了两班岗，异常这一班排在值的前面：有 `exception_ptr` 的话就交给 `std::rethrow_exception` 原样重抛，没有的话就把值搬走，交给赋值号的左边。咱们把 `int r = co_await co_add(1, 2);` 的完整旅程凑齐了：父协程挂起了，子协程被启动了，值进了 promise，控制权转移了回来，最后由 `await_resume` 交了货。vol4 留下的第二处毛病，就在这一行被治好了。

### 三层链验收

咱们直接上代码，拿一条三层链做验收：`main_task` 等的是 `worker`，`worker` 等的是 `co_add`：

```cpp
Task<int> co_add(int a, int b) {
    log("  co_add: running");
    co_return a + b;
}

Task<void> worker(const char* name, int a, int b) {
    log("  worker: enter");
    int result = co_await co_add(a, b);
    std::printf("  worker: %s: %d + %d = %d\n", name, a, b, result);
    co_return;
}

Task<void> main_task() {
    co_await worker("TaskA", 1, 2);
    co_await worker("TaskB", 3, 4);
    co_await worker("TaskC", 5, 6);
}
```

咱们还差一个口子：启动 `main_task` 的 main 并不是协程，那谁来干这个活？咱们写个最小的驱动函数：

```cpp
template <typename T>
T sync_wait(Task<T> task) {
    task.start();              // 里面就一句 handle_.resume()
    return task.await_resume();
}
```

咱们敢只 resume 一次，是因为链上所有的等待都是 Task：一层层的启动与回传走的全是对称转移，中途不会有任何的控制权丢回给 main。根任务一启动就会一路跑到停在自己的最终挂起点，它此时的 `continuation` 还是 noop，干净地返回。等咱们在 [事件循环那篇](./04-event-loop-and-timers.md) 换上真调度器，变的只是驱动的写法，Task 的本体一行都不用改。

咱们把输出在脑子里过一遍：`main_task` 一启动，就一头扎进了 TaskA 的 co_await。worker 打印了 enter，co_add 也打印了 running，worker 随后打印了算式，三层调用到这里就走完了。TaskB、TaskC 也照同样的顺序各走一遍，最后跑完的是 `main_task`。全程没有任何一行输出会抢在别人的前头，惰性 Task 的执行顺序从代码上一眼就能读出来，这个性质等会儿的练习要用。

<!-- 实验回填：三层链 demo 的实际运行输出（编译命令 + 完整日志原文） -->

## 对称转移：恢复别人的正确姿势

### 朴素 resume 链：栈是怎么涨上去的

在对称转移的正课开讲之前，咱们得看看不用它会怎样。最直觉的写法是：挂起父协程之后，顺手把子协程的执行 resume 起来，既然你要跑，我这就叫你跑：

```cpp
// 反面教材：awaiter 里直接 resume
std::coroutine_handle<> await_suspend(
    std::coroutine_handle<> awaiting) {
    handle_.promise().continuation = awaiting;
    handle_.resume();          // 栈上直接叫醒子协程
    return std::noop_coroutine();
}
```

完工的方向同理，FinalAwaiter 的 `await_suspend` 里改成调 `continuation.resume()`，而不是返回它。栈会怎么长，咱们画给眼睛看：

```text
sync_wait()  的栈帧
└─ resume(main_task)
   └─ main_task 的栈帧：co_await worker
      └─ resume(worker)          ← awaiter 里直接调
         └─ worker 的栈帧：co_await co_add
            └─ resume(co_add)    ← 又是 awaiter 里直接调
               └─ co_add 的栈帧
```

咱们看往下走的方向：每等一层就多一层调用。回来的方向更糟：每个完工的协程在自己 `await_suspend` 的栈帧里把父协程叫醒，父协程续跑了、也完工了，又在自己那层的栈帧里叫醒更上一级，栈的高度跟着链的深度一起涨。Lewis Baker 在他的对称转移专文里给的例子更狠：两个协程在同一个循环里互相等，A 的 resume 里调 B 的 resume，B 的又调 A 的，一个永远不结束的循环就能把栈吃到爆，下场跟无限递归的一模一样。

咱们别小看这个增长。异步代码里链条的深度常常不由人控制：请求协程等解析的结果，解析的那头等读缓冲，读缓冲的下面还等着 socket，用户随手一包就多了一层。深链之下咱们离爆栈有多远，看的只是栈还剩多少。

<!-- 实验回填：深链实验（朴素 resume 版 vs 对称转移版，-O0/-O2 两档，各深度档位的栈深/崩溃点，perf 或 /proc 数据原文） -->

### 语言给的答案：返回句柄

P0913R0 给 `await_suspend` 开了第三种返回：句柄。语义是这样的：当前协程挂起后，返回的句柄被恢复执行，控制权从当前的 co_await 直接交接到那个协程。您在这里看到的是交接，而不是函数调用。提案在纸面上提的要求是：这样的接连恢复不许有层数上限。落到机器码上怎么兑现，Baker 的原话说得很硬：无论开没开优化，编译器都把每一次转移做成了一次尾调用（tail call）。为什么敢这么保证？因为尾调用的几个条件在这里天然就齐了：两边的调用约定相同，转移函数返回的是 void，调用之后也没有非平凡的析构要跑。协程挂起的时候不退出任何作用域，本来就没有扫尾的工作挡在返回路上。

到这里咱们把三种返回收进一张表。它比看上去的更像个调度接口：

| `await_suspend` 返回 | 语义 | 什么场合用 |
|---|---|---|
| `void` | 协程保持挂起，控制权还给 resume 的调用者 | 把句柄交给外部保管，比如入队、挂上定时器 |
| `bool` | true 同 void。false 是反悔，立即恢复当前协程 | 挂不挂起可以临场再定 |
| `coroutine_handle` | 挂起当前协程，直接恢复返回的句柄 | 需要立刻恢复另一个协程 |

bool 那一行的反直觉，上一篇里咱们提过，这里再念一遍口诀防手滑：`await_ready` 的 true 是好了别挂，`await_suspend` 的 true 是挂，两个 true 说的是相反的事。Baker 给的选型经验也直白：要恢复另一个协程，就选句柄的返回形式。

### 现实的两个实现缺陷

笔者得把现实里的暗礁摆出来，不然您拿着对称转移上生产，可能在最想不到的地方翻船。头一块在 GCC 的地盘：Bugzilla 上挂着 PR c++/100897，标题说的就是对称转移没能阻止 C++20 协程的栈溢出。从提交的那天到现在，咱们没找到任何已在某个版本修复的官方确认，社区里不少项目以它为由绕开了对称转移，ROS 2 的执行器设计讨论就在其列。第二块在 Clang：LLVM 的 issue #42853 记录了 AArch64 上 `-O0` 构建时，对称转移发的是普通调用而不是尾跳转，同样深度的链直接爆栈。同一个 issue 附了两套架构的汇编对照，x86-64 上哪怕 `-O0` 发的也是尾跳转。咱们由此能看明白：尾调用的保证写在提案与设计里，两家的实现都出过漏子。哪里漏了、哪里修了，咱们得看版本、看架构、看优化级别。

于是咱们手里有两条务实的路。语言层的路就是本篇的 FinalAwaiter 加句柄返回，它的设计意图正、代码也短，中小深度的链在主流平台上没有麻烦。工程层的路是调度器蹦床：谁都不许在自己的 awaiter 里直接 resume 别人，而是统一把句柄丢进就绪队列，让顶层的调度循环做唯一的 resume。栈深是恒定的，不赌编译器的尾调用质量。

### 调度器蹦床：把 resume 收归一处

咱们在 vol4 的调度器那篇用过一套接线，promise 的骨架和本篇的 Task 一模一样，都是存一个父句柄、完工的时候唤醒，差别全在唤醒的动作上：

```cpp
// vol4 调度器的接线：resume 一律收归调度循环
struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template <typename Promise>   // 擦除形态调不了 promise()，得收带类型的句柄
    void await_suspend(std::coroutine_handle<Promise> h) noexcept {
        // 不恢复任何人，把父句柄塞进就绪队列就走
        ready_queue.push(h.promise().parent_coroutine);
    }
    void await_resume() const noexcept {}
};
```

咱们要看的就一行：`ready_queue.push(...)` 只把句柄塞进就绪队列，连一次像样的 `resume()` 都不做，控制权当场还给了调度循环。子协程完工的时候也一样，把父句柄塞进队列就完事了。于是每一趟的路线都是闭合的：`resume` 的起点在调度循环，协程跑完自己的一小段，控制权交还给了循环，下一趟还是从同一处出发的。蹦床的名字就是这么来的，每次的弹跳都从同一块板起跳。调度器本身的实现是 [事件循环那篇](./04-event-loop-and-timers.md) 的主场，咱们在这里只认下这个接口的形状。

三种续体的方案摆成一张表，您选型的时候心里有底：

| 方案 | 机制 | 栈行为 | 短板 |
|---|---|---|---|
| 朴素 resume 链 | awaiter 里直接 `resume()` | 随链深增长，深链爆栈 | 深链下正确性过不了关 |
| 对称转移 | `await_suspend` 返回句柄，编译器尾调用交接 | 设计上恒定 | 赌实现质量（GCC PR 100897、LLVM #42853） |
| 调度器蹦床 | 句柄一律入队，调度循环独占 resume | 恒定 | 多一层调度，路径变长 |

本篇的 Task 走的是对称转移。[事件循环那篇](./04-event-loop-and-timers.md) 会把蹦床的那一半补上，到时候您把 Task 接进真调度器，两条线共用的 promise 骨架是一样的，换的只是接线。

## 异常通道：存进 promise，在 await_resume 重抛

咱们现在来治第三处毛病。协程体里抛了异常又没人接，编译器会把它送进 `unhandled_exception()` 的门里，走的正是协程版的事故出口。咱们的实现就一行：`exception = std::current_exception();`，把当前的异常对象装进 `exception_ptr`。这个类型是标准库搬运异常的集装箱：值语义、可空、可拷贝，专门为把异常从抛出的现场运到别处再抛而设计。存好了之后，协程会照常地走 `final_suspend`，把控制权照常地交给续体，一路静悄悄的。

父协程的那一头，`await_resume` 的头一班岗就是查集装箱里有没有货：有货的话就交给 `std::rethrow_exception` 原样重抛。于是异常就出现在了父协程体里，正好落在那里的 try/catch 里，观感跟普通函数调用的一模一样。cppreference 协程页的官方 Generator 示例用的正是这一存一抛，Baker 的 promise 专文也把它列为典型做法。咱们拿一个最小例子过一遍水：

```cpp
Task<int> failing() {
    log("  failing: about to throw");
    throw std::runtime_error("boom from child coroutine");
    co_return 0;   // 不会执行到，只为让返回类型是 Task<int>
}

Task<void> exception_parent() {
    log("exception_parent: enter");
    try {
        int v = co_await failing();
        std::printf("exception_parent: got %d (unexpected)\n", v);
    } catch (const std::runtime_error& e) {
        std::printf("exception_parent: caught '%s'\n", e.what());
    }
    log("exception_parent: continue after catch");
    co_return;
}
```

咱们预期的行为是这样的：`failing` 一进门就抛了，异常进了 `unhandled_exception`、也存进了 promise。控制权交回给了 `exception_parent`，`await_resume` 重抛了它，catch 接住了、打印了，协程正常地收尾。异常就这样穿过了协程边界，中间连一点失控的动静都没有。

<!-- 实验回填：异常通道 demo 的实际运行输出（编译命令 + 完整日志原文） -->

> 有个细节咱们顺路记下：协程体的 try/catch 大罩子罩不到 `initial_suspend`。它是在罩子外头执行的，那里抛出的异常不会进 `unhandled_exception()`，而是直接传给协程的调用者。Baker 的忠告更直白：`initial_suspend`、`final_suspend`、`unhandled_exception` 三处，咱们一处都不许它抛东西。要求也有软硬之分：`final_suspend` 的 `noexcept` 是规范的硬性要求，少了它程序就是非良构。`initial_suspend` 和 `unhandled_exception` 的不抛，靠的则是实现自律。咱们的实现给三处都声明了 `noexcept`，编译器替咱们守着。

## 勘误：旧卷的 ChainTask 其实编不过

写到了这里，笔者得向旧卷（卷五的上一版）的读者认个错。旧卷在这个位置给出的 ChainTask 示例，`final_suspend` 直接返回了裸句柄：

```cpp
// 错的，别抄：final_suspend 返回裸句柄
struct ChainTask::promise_type {
    std::coroutine_handle<> kCaller;

    std::suspend_always initial_suspend() { return {}; }
    std::coroutine_handle<> final_suspend() noexcept {
        return kCaller;   // 编不过：句柄不是 awaiter
    }
    // ...
};
```

这段代码是非良构的。原因咱们现在看得清清楚楚：`final_suspend` 的返回值要被编译器 co_await，而 `coroutine_handle` 只是个句柄，够不上 awaiter 的资格。它的身上没有 `await_ready`、`await_suspend`、`await_resume` 三个函数，也没有能把它变成 awaiter 的 `operator co_await`。笔者把旧卷代码原样抄下来编了一遍，GCC 的报错如下：

```text
error: no member named 'await_ready' in 'std::__n4861::coroutine_handle<void>'
```

报错指的位置还有点迷惑性：它指在协程函数的收尾花括号上，而不是 `final_suspend` 那一行，因为这次 co_await 发生在编译器生成的收尾代码里，离您手写的代码隔着一层。报错的措辞也直白：编译器想对这个返回值调 `await_ready`，句柄身上偏偏没有这个成员。正确的写法就是本篇的 FinalAwaiter：结构体把 `await_ready`、`await_suspend`、`await_resume` 三个函数都备齐了，`await_suspend` 负责的活就是把续体句柄交出去。手里存着旧卷代码的读者，照着勘误的部分改过来就能编过。咱们的勘误到这儿就算补完了，新读者直接照本篇的写法来就好。

笔者把旧卷自注的另一句也翻出来了，它倒是说对了：kCaller 为空的话，行为就是未定义的。解法其实也已经就位：把 `continuation` 初始化成 `noop_coroutine()`，根任务也有了体面的落点。

## 下一步

`Task<T>` 的骨架到这里就齐了。任务是惰性启动的，创建的时候并不执行。值、异常、续体三条通道都接通了：值与异常都存进了 promise，控制权的交接走的是续体。帧的所有权归 move-only 的 Task 管，由持有它的代码负责销毁。咱们还欠它两样东西。取消的这一桩 [下一篇](./03-coroutine-cancellation.md) 专门讲：协程跑到一半怎么礼貌地劝退，stop_token 那套怎么接进 promise。另一桩是真调度：`sync_wait` 这样一次到底的驱动只够教学用，事件循环、定时器的内容都在 [事件循环与定时器](./04-event-loop-and-timers.md)。想动手的，推荐去 [协程调度器 Lab](../exercises/04-coroutine-scheduler) 拿本篇的 Task 换上蹦床的接线练一遍。全卷的走向见 [卷地图](../)。

> 💡 咱们把本篇的完整可编译代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch06-async-io-coroutine/`。里面还有 `Task<void>` 特化、`sync_wait` 驱动、三层链与异常通道的演示，编译命令就写在头部的注释里，咱们照着敲就行。

## 练习：日志预测

练习的玩法从这一篇起换成日志预测：代码给您，日志也给您，可能还挖掉了几行。您把输出顺序在心里过一遍，再对照日志做逐行的归因。代码还没跑起来的时候，您就能预判协程的执行顺序，手感就是从这样的练习里练出来的。

### 练习 1：vol4 那个 0 是怎么来的

咱们看 vol4 当时的程序骨架。`task()` 里连着三个 SimpleReader 的 `co_await`，SimpleReader 的 `await_suspend` 开了 detached 线程，睡了一秒，置了值，再恢复协程的执行。Task 的 `initial_suspend` 返回 `suspend_never`，值是从 `shared_ptr` 侧门走的，完工的消息靠的是 `quit_flag` 传递。

```cpp
int main() {
    auto result = task();   // 打印前协程已经开跑
    std::println("Result here: {}", result.value());
    while (!quit_flag)
        ;
    std::println("Result here: {}", result.value());
}
```

日志是 vol4 机器上当时的真实输出，笔者挖掉了两行，请您补齐，并说明每一行的来历。尤其值得说清楚的是，main 的打印为什么会插在协程的日志中间。读日志的时候留意格式：带时刻前缀的行出自协程框架的 simple_log，光秃秃不带前缀的行（Result 与 tol 两类）出自 main 和协程函数体自己的 println，格式本身就是归因的线索。再给您一句提示：两处空白里有一行不是协程打出来的，那一行出现的时机不归协程管，得看 main 抢跑的节奏。

```text
19:24:06 :Ready to involk task()
19:24:06 :Task::promise_type::promise_type is involked!
19:24:06 :Task::promise_type::get_return_object is involked!
19:24:06 :Task is created!
19:24:06 :Task::promise_type::initial_suspend is involked!
19:24:06 :CoAwait the reader1
19:24:06 :call await_ready, always return false
19:24:06 :call await_suspend, creating a detached thread
____________________①____________________
19:24:07 :call await_resume, return the current value: 1
____________________②____________________
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

补完之后请您再答一问：把 `while (!quit_flag);` 删掉，程序会打出什么？退出的时候，三个 detached 线程和协程帧各是什么下场？

### 练习 2：蹦床接线下的唤醒链

咱们把调度器蹦床的接线，套回正文写过的三层链：`main_task` 依次 `co_await worker("TaskA", 1, 2)` 到 `worker("TaskC", 5, 6)`，`worker` 里面等的都是 `co_add`。vol4 那版调度器的 `await_suspend` 做的头一件事是记父句柄、再入队子协程，`final_suspend` 做的是把父句柄入队。下面是 vol4 机器上当时的真实日志，TaskA 的一段挖掉了两行，TaskB 是完整的，TaskC 的收尾同构。请您补齐：

```text
10:36:12 :Current Routine will be suspend!
10:36:12 :Child Routine will be called resume!
10:36:12 :Current Routine will be suspend!
10:36:12 :Child Routine will be called resume!
____________________①____________________
____________________②____________________
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskB: 3 + 4 = 7
（TaskC 一组同构，最后打出 TaskC: 5 + 6 = 11）
```

补齐了之后，咱们再对 TaskA 的五条调度日志（算式那行不算）逐条归因：每一条是从哪个函数里打出来的，是 `await_suspend` 的手笔，还是 `final_suspend` 的手笔。有个容易看走眼的地方提醒您：打 `Child Routine will be called resume!` 的时候，resume 其实还没发生，它打在入队动作的前头，真正的恢复发生在调度循环里。加分题也备好了：同样的三层链，换成本篇正文的对称转移接线，您觉得日志会变短还是变长？哪些行会整组消失？

### 练习 3：把 initial_suspend 改回 suspend_never（思考题）

咱们既要想清楚，又要动手改一改：把本篇 Task 的 `initial_suspend` 改成 `suspend_never`，别的地方都不动，程序会坏在哪一步？给您三条线索：惰性没了之后，`sync_wait` 里的 `start()` 会去 resume 一个停在什么状态的协程。链路中间的 `co_await`，在 `await_ready` 的 `done()` 短路下会走什么路线。还有正文里反复写到的那道 UB 边界。笔者动手验证过，它在笔者的机器上跑不完就倒下了，ASan 报的是跳进空地址。您验证完还可以再想一层：程序倒下的那一刻，中间层 worker（居中传递、自己不产值的包装层）打印出来的算式居然还是对的，为什么？

## 参考资源

- Lewis Baker, *Understanding Symmetric Transfer*, Asymmetric Transfer 博客, 2020-05-11 —— 尾调用保证、朴素 resume 的栈增长与互相等待的死循环，正文多处转述自它：<https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer>
- P0913R0 *Add symmetric coroutine control transfer* —— `await_suspend` 返回句柄、恢复层数不设上限与 `noop_coroutine` 的提案出处：<https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0913r0.html>
- Lewis Baker, *Understanding the promise type*, 2018-09-05 —— promise 侧设计的正源，续体存 promise 的理由：<https://lewissbaker.github.io/2018/09/05/understanding-the-promise-type>
- cppreference: Coroutines —— 协程执行全流程、`await_suspend` 三种返回语义、Generator 官方示例（存异常、重抛）与特性测试宏取值：<https://en.cppreference.com/w/cpp/language/coroutines>
- GCC Bugzilla PR c++/100897 *Symmetric transfer does not prevent stack-overflow for C++20 coroutines*：<https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100897>
- LLVM issue #42853 *Coroutine symmetric transfer tail call optimization not working on AArch64*：<https://github.com/llvm/llvm-project/issues/42853>
- GCC 官方 C++20 语言支持状态页（协程一行，含 `-fcoroutines` 与版本口径）：<https://gcc.gnu.org/projects/cxx-status.html>
- CWG 2556 *Fallthrough to co_return*（掉出协程体等价 `co_return;` 的追溯裁决）：<https://cplusplus.github.io/CWG/issues/2556.html>
