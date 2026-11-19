---
title: "一个最小协程怎样成立"
description: "三个关键字怎样把函数变成可暂停的对象，协程帧里住什么、怎样分配、什么时候销毁，promise_type 与协程句柄各管什么，最后拼出一个能跑通的最小生成器。"
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 一个最小协程怎样成立

上一篇《[为什么需要暂停与恢复](01-why-suspend-resume)》回答了为什么要让工作停下来再接着跑。本篇要跟您一起把机制本体摆出来：三个关键字怎么把一个普通函数变成可以暂停的对象，暂停之后状态住在哪，谁来恢复它，又是谁负责收回它。这些是第七单元后面所有内容的底盘，`co_await` 的等待协议、任务类型的组织，全都站在这套机制上。

素材从旧目录 ch06 的《C++20 协程基础》与《promise_type 与 awaitable》来，代码全部重写，行为断言我按现行工作草案重核了一遍。本篇的三个实验我都放进了临时目录 /tmp/concurrency-u7/ 里编译运行（WSL2，16 逻辑核，GCC 16.2.1 20260810，`g++ -std=c++20 -O2 -pthread`，2026-11-17），正文里的代码块就是全部内容，拼成单个 .cpp 文件即可复跑。

## 三个关键字，把函数变成协程

`co_await`、`co_yield`、`co_return`，C++20 加了三个以 co_ 打头的关键字。编译器认协程的依据就一条：您在函数体里写下它们中的任何一个，该函数就按协程处理（[dcl.fct.def.coroutine]/1）。不用加修饰符，不用注册，关键字本身就是身份声明。反过来，函数体里一个都没有，哪怕返回类型的名字长得再像 Task，它也只是普通函数。

分工上，`co_await` 管“等一等”：走到它，协程评估要不要挂起，挂起就把控制权交出去，恢复之后从它手里接回一个值。等待对象怎么定制归下一篇《`co_await` 怎样参与等待》，挂起体就是最简单的一类 awaiter，协议也在那边讲。咱们本篇只用标准库送的两个现成挂起体，`std::suspend_always` 与 `std::suspend_never`：前者的 `await_ready()` 永远返回 false，意思是每次都挂起。后者返回 true，意思是从不挂起（[coroutine.trivial.awaitables]）。`co_yield` 管“产出一个值再挂起”，生成器全靠它。`co_return` 管收尾，协程的结束由它完成。

`co_yield` 在标准里有确定的展开：`co_yield e` 等价于 `co_await promise.yield_value(e)`（[expr.yield]/1）。promise 对象从哪里来，promise_type 一节交代。您眼下只需要看见形状：产值的动作被翻译成一次挂起等待，promise 借 `yield_value` 把值收进自己身上，然后返回挂起体。

`co_return` 有两种形态。`co_return;` 调 `promise.return_void()`。`co_return expr;` 看 expr 的类型，非 void 调 `promise.return_value(expr)`。void 类型照样走 `return_void()`，出处是 cppreference 的 coroutines 页。函数体走到底没有写 `co_return`，等价于末尾补了一个 `co_return;`。有一条松动的地方要向您交代清楚：promise 缺了 `return_void`，编译器不报错，函数体走到底按现行草案是未定义行为（[stmt.return.coroutine]/3）。本机实测，默认选项和 -Wall -Wextra 都没有一条警告，走到底的协程在咱们机器上照常挂进最终挂起点，程序没崩。标准把它定为未定义行为，后果随实现走，换一个编译器可能就是另一副面孔，别把一次没崩当成没事。显式写了 `co_return;` 才会被编译器拦下，报 no member named `return_void`。按 C++20 的规则，promise_type 里 `return_void` 与 `return_value` 不能同时声明，两者都声明程序不合法，标准到 C++29 才把互斥放开（P3950R1），咱们本篇按 C++20 走。

还有几道限制，咱们写代码时绕不开。协程里不能写普通的 `return` 语句。constexpr 函数、consteval 函数、构造函数、析构函数、变参函数和 `main`，都不能是协程。返回类型也不能是 auto，cppreference 把它收在 Restrictions 一节。变参的限制标准写得具体：参数表以不带名的省略号收尾的协程，程序不合法（[dcl.fct.def.coroutine]/1）。参数包展开的可变模板不受影响，标准自己举了 `g2(Args&&...)` 的例子。

## 编译器把函数体换成了一台状态机

您写下的函数体，编译器并不会照单全收。标准把协程的行为写成一份替换体。您声明的函数体，会被装进编译器搭好的一段骨架里（[dcl.fct.def.coroutine]/5）。骨架长这样，伪代码照标准的结构摆：

```text
{
  promise 构造（promise 若有接收协程参数的构造函数，实参用参数拷贝）
  try {
    co_await promise.initial_suspend();   // 初始挂起点
    您写的函数体
  } catch (...) {
    if (!initial-await-resume-called) throw;   // 开工前抛的，原样交回调用者
    promise.unhandled_exception();
  }
final-suspend:
  co_await promise.final_suspend();       // 最终挂起点
}
```

骨架里有两个您没写的挂起点。`initial_suspend` 站在函数体之前，它决定协程是懒启动还是急启动：返回 `suspend_always`，调用者拿到返回对象时函数体一行没跑，得 resume 才开工。返回 `suspend_never`，协程一路跑到头一个真正的挂起点才停下，上一篇《[为什么需要暂停与恢复](01-why-suspend-resume)》的 Task 选的就是急启动，一千个协程创建后立刻跑到 Park 挂起。`final_suspend` 站在函数体之后，`co_return` 执行完、局部变量按构造的逆序销毁完，协程来到它面前。

Park 身上的 `await_ready`、`await_suspend`、`await_resume` 三件套，协议的细节归下一篇《`co_await` 怎样参与等待》，您到那里再拆。

`get_return_object` 的时序也定死了：它的调用排在 `initial_suspend` 之前，至多一次（[dcl.fct.def.coroutine]/7）。所以您手里拿到返回对象的时候，协程体必然还没开工，哪怕 promise 配的是急启动。返回对象里通常住着一个句柄，句柄怎么造出来，promise_type 一节细讲。

替换体的链条，咱们拿一个会打印的协程真跑一遍。Trace 的协程体只有两行打印，promise 的 `initial_suspend` 与 `final_suspend` 都是 `suspend_always`，两个挂起点的入口都插了打印，析构函数调 destroy 时也打印一声：

<OnlineCompilerDemo
  title="懒启动与终点站：协程从生到死的每一步"
  source-path="code/examples/vol5/chapter07/03_lazy_start.cpp"
  description="demo() 被调用时函数体一行没跑，第一次 resume 才开工，co_return 之后挂在最终挂起点等 destroy，帧的收回也有打印。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

咱们看实跑输出：

```text
[main ] 调用 demo()
  [协程] initial_suspend 挂起，控制权交回调用者
[main ] 拿到返回对象，函数体一行没跑，done=0
[main ] 第一次 resume
  [协程] 函数体第一行
  [协程] 函数体第二行
  [协程] final_suspend 挂起，等 destroy
[main ] 回到 main，done=1
  [main ] Trace 析构，调 destroy 收回协程帧
[main ] Trace 析构完，程序结束
```

输出跟替换体逐行对得上，咱们对着读。`demo()` 被调用的当口，帧分配好，promise 构造好，`get_return_object` 把带着句柄的 Trace 交出来，`initial_suspend` 把协程挡在函数体门口，控制权回到 main。此刻 `done()` 是 0，因为它只在最终挂起点返回 true。第一次 resume 之后，函数体两行打印依次出来，`co_return` 走完 `return_void`，局部变量销毁，`final_suspend` 把协程挂进终点站，done 翻成 1，Trace 析构时 destroy 把帧收回。协程从生到死，每一步都看得见。

## 协程帧：状态住在一块堆内存里

协程暂停之后还能恢复，靠的是它的状态完整地活着，前提摆在这里，咱们得认下来。标准把它叫协程状态（coroutine state），业界叫得更多的是协程帧（coroutine frame），两个名字说的是同一个东西：一块装着协程全部身家的存储（[dcl.fct.def.coroutine]/9）。cppreference 给它列了四样内容：promise 对象，全部参数的拷贝，表示当前挂起点的数据（恢复时靠它知道从哪里继续，销毁时靠它知道哪些局部变量在挂起点活着），以及生命周期跨越挂起点的局部变量与临时对象。

咱们在 4.1《[用 `std::async` 执行计算](../chapter04-organizing-tasks/01-launch-with-async)》见过一位亲戚：future 背后有一段共享状态，值与异常都住在里面，异步提供者往里放，future 从里头取。协程帧是同一思路的另一个住法，状态不住在调用栈上，住在一个对象里，对象的命比函数调用长。区别也看得清楚：共享状态中转结果，协程帧存放的是协程自己的身家，参数、局部变量、挂起点全在里面。

分配走的是 `operator new`。标准把查找顺序也写死了，咱们跟着读：promise_type 的作用域里有 `operator new` 就用它，实参除了大小还能带上协程参数（[dcl.fct.def.coroutine]/9.1）。作用域里没有，再去全局找一份普通的非数组 `operator new`。释放用 `operator delete`，同样从 promise 作用域找起，单参版本与带大小版本都找得到时，带大小的版本优先（/12）。promise 想自定义分配策略，定义一份成员 `operator new` 就够，后面的 frame_alloc 实验里就是这么干的。

分配失败的默认出路是抛 `std::bad_alloc`。您在 promise_type 里若声明了静态成员 `get_return_object_on_allocation_failure`，语言就换一副面孔：分配改用不抛的 nothrow 形式，失败时不抛异常，而是调它拿一个返回对象直接交回调用者（[dcl.fct.def.coroutine]/10）。标准在协程定义条款讲分配失败的位置，给了一个完整的生成器示例，用的正是它。

cppreference 还交代了一层：堆上的分配有可能被实现直接省掉。条件有两个，协程状态的生命周期严格嵌套在调用者的生命周期之内，大小在调用点可以算出来。满足的话，帧可以住进调用者的栈帧里，`operator new` 一次都不调。省掉堆分配的优化有名字，叫堆分配省略（heap allocation elision，业界常缩写成 HALO）。我拿 frame_alloc 数了数，读数在下面。

<OnlineCompilerDemo
  title="数协程帧的分配与释放"
  source-path="code/examples/vol5/chapter07/04_frame_alloc.cpp"
  description="promise 作用域里的 operator new 与 delete 各计一次数，嵌套场景 3 次、逃逸场景再加 1 次，-O0 与 -O2 读数相同。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

咱们在 -O2 与 -O0 各跑一遍，读数相同：

```text
嵌套场景之后：operator new 3 次，operator delete 3 次
逃逸场景取到 2 个值，随后：operator new 4 次，operator delete 4 次
```

逃逸场景好理解：生成器活着离开了 `make_escape`，协程状态的命比调用者长，嵌套条件不成立，堆分配躲不开。嵌套场景的三次，按 cppreference 给的条件本可以省掉，我手头的 GCC 16.2.1 没有省，优化档位开与不开读数一样。读数只说我手头的编译器，换一家实现、换一版编译器，读数可能就不一样，您拿自己机器跑一遍，就知道自己站在哪边。

参数进帧有它自己的规则，咱们跟着标准过一遍。每个协程参数都要在替换体开头拷出一份（[dcl.fct.def.coroutine]/13）：按值参数拷的是值，挪进帧里，协程恢复的时候用的是帧里的拷贝。引用参数拷的是引用本身，帧里只记一个名字，被指的对象归调用者的世界管。拷贝的初始化排在 promise 构造之前，销毁排在 promise 析构之后（/13.5、/13.6）。

悬垂的风险，您可以拿 `S{0}.f()` 想一遍：`S{0}` 是个临时对象，语句一结束就没了，成员函数发起的协程在帧里只记了个引用，等您 resume 的时候，它指着一块已经还给世界的内存。cppreference 的 coroutines 页给了一组同形状的悬垂示例（bad1 正是这么写的），标准在 /13 的注里专门提醒过。引用参数让协程活得比对象久，是协程里最常见的悬垂来源。参数要跨挂起点用，按值传是咱们能给自己上的最便宜的一道保险。

## 帧的销毁时机：危险都在这里

销毁的入口有两个。控制流穿过协程末尾，也就是 `final_suspend` 也不挂、一路走出函数时，协程状态自动销毁。另一个入口是句柄上的 `destroy`（[dcl.fct.def.coroutine]/11）。咱们拿最常见的懒启动写法，`initial_suspend` 与 `final_suspend` 都返回 `suspend_always`，对一遍时序：`co_return` 之后协程挂在最终挂起点，控制流没有走出协程，帧还完整地活着，promise、参数拷贝、局部变量全在，等的就是 destroy。上面 lazy_start 的输出里，done 翻成 1 之后帧还收得到，就是挂在终点站的帧。

`final_suspend` 若返回 `suspend_never`，替换体里的 `co_await promise.final_suspend()` 不挂起，控制流直接穿过末尾，协程状态当场自动销毁。看起来省了一步 destroy，代价是句柄悬垂：外部若还攥着 `coroutine_handle`，它指向的内存已经还给了分配器，再 resume 再 destroy 都是未定义行为。所以教学用与库用的类型几乎都让 `final_suspend` 挂起，把销毁的决定权留给拿句柄的人。您马上会看到，Generator 也做了同一个选择。

挂在最终挂起点的协程，能做的只剩销毁。resume 的前置条件写得很明白：句柄指向一个挂起的协程，而且不在最终挂起点（[coroutine.handle.resumption]/2），拿 `done()` 为 true 的句柄去 resume，是未定义行为。destroy 的前置条件同样写着指向挂起的协程（/4）。`done()` 在这里充当仪表，咱们写恢复循环，就是循环推进到 done 翻 true 为止的形状，Generator 一节会见到。

反方向的错误是忘了销毁。协程帧从堆上来，咱们不调 destroy、控制流又走不出末尾（final_suspend 挂着），帧就一直在堆上住着。`coroutine_handle` 是非拥有的，析构时它什么都不做，泄漏的责任句柄不背。所以句柄几乎总要包进一层 RAII，析构函数里调 destroy，Generator 的析构函数正是这么写的。

destroy 随后的清理活，cppreference 的 Completion 一节给出了定序：promise 对象析构，参数拷贝析构，`operator delete` 把帧的内存还回去。咱们把次序跟参数一节的规则对一遍：参数拷贝活得比 promise 长，销毁时也走在 promise 后面。

## promise_type 与协程句柄

promise_type 不是凭空来的，它由协程的返回类型决定：`std::coroutine_traits<R, Args...>::promise_type`，R 是返回类型，Args 是参数表（[dcl.fct.def.coroutine]/3）。写成普通函数时，取的就是返回类型的嵌套 promise_type。5.1《[原子操作解决到哪一步](../chapter05-atomic-memory-model/01-what-atomics-solve)》里咱们认过一套同型的关系：`std::atomic` 是一套类型加一套操作，类型决定您拿得到哪些操作。协程这边，返回类型决定 promise_type，promise_type 决定协程在各个节点上的行为，返回类型就是协程的身份声明。`Generator<int>` 与 `Task<int>` 挂同样的关键字，行为可以完全不同，分岔全在 promise_type 里。

编译器会调的行为钩子拢共六个，return_void 与 return_value 二选一，咱们照调用次序走一遍（分配与释放函数也住在 promise 作用域里，协程帧一节讲过它们的查找顺序）：

1. `get_return_object()`：构造返回对象，调用者拿到的就是它，至多一次（[dcl.fct.def.coroutine]/7）。
2. `initial_suspend()`：开工前是否挂起，懒启动与急启动的分岔口。
3. `yield_value(e)`：`co_yield` 的落点，收下值并决定是否挂起（[expr.yield]/1）。
4. `return_void()` 或 `return_value(v)`：`co_return` 的落点，二选一，C++20 里两者都声明程序不合法。
5. `unhandled_exception()`：函数体抛出异常没有接住时被调用，是异常的出路。
6. `final_suspend()`：终点站，不得 potentially-throwing，通俗讲就是它得承诺不会抛异常，签名要带上 `noexcept`（[dcl.fct.def.coroutine]/15）。

`unhandled_exception` 的去处有两派。terminate 派直接 `std::terminate()`，简单粗暴，适合异常压根不该出现的场合。存储派把 `std::current_exception()` 存进 promise 的 `exception_ptr` 成员，等外部来取，下面 gen_min.cpp 里的 Generator 用的就是这一派，您马上会在逐行讲的时候见到它。还可以在 `unhandled_exception` 里 `throw;`，把异常重新抛出去，标准对此有明确交代：unhandled_exception 再抛异常时，协程视为挂在最终挂起点，异常传播给调用者或恢复者（[dcl.fct.def.coroutine]/14）。

promise 这个名字容易让人想到第四单元的 `std::promise`。cppreference 在协程页写了一句排除说明，promise objects 与 `std::promise` 没有任何关系。`std::promise` 是 `<future>` 里往共享状态放结果的异步提供者。协程的 promise_type 是编译器调用的行为定制点。两者除了名字没有任何亲缘。咱们在正文里写 promise_type，单写 promise 时都指协程帧里的 promise 对象。

协程句柄 `std::coroutine_handle<Promise>` 是从外部操作协程的入口，cppreference 给它的定位是非拥有句柄：它指向协程状态，不拥有它。常用的成员不多。`resume()`（还有个等价的 `operator()`）让挂起的协程继续跑。`done()` 报告协程是否挂在最终挂起点。`destroy()` 销毁协程状态。`promise()` 拿到帧内 promise 对象的引用。静态的 `from_promise(promise)` 反向从 promise 引用造出句柄，您在 `get_return_object` 里写的那句 `from_promise(*this)` 正是这么用的。

标准给恢复动作起了名字，调用恢复成员函数的求值动作叫 resumer（[dcl.fct.def.coroutine]/8）。协程不自带线程，谁调 resume，协程体就接着跑在谁的调用栈上。3.4《[按协作需求选择工具](../chapter03-wait-and-coordinate/04-choosing-tools)》讲 barrier 完成函数的时候咱们认过一回事：完成函数跑在某条到达线程身上，不额外占一条线程。协程的恢复是同一门亲戚。跨线程恢复有一条边界值得交代。在不挂起它的执行代理上 resume 或 destroy，行为由实现定义，除非参与的都是 `std::thread`、`std::jthread` 实例，或者执行 main 的线程（[coroutine.handle.resumption]/1）。谁恢复谁负责，7.4《把协程组织成可完成的任务》讲任务类型的时候还要回来。

## 第一个完整例子：Generator

机制都齐了，咱们把它们拼成一个真的组件。目标是做出如下生成器：协程用 `co_yield` 产出整数序列，调用方 `next()` 一次取一个，值从 promise 身上读，用完析构函数收帧。上一篇说的能暂停的工作，落到代码上长成的就是它。

<OnlineCompilerDemo
  title="最小生成器：co_yield 产出平方数"
  source-path="code/examples/vol5/chapter07/05_gen_min.cpp"
  description="squares(5) 创建时函数体一行没跑，next() 一次产出一个平方数，五个取完后 done 翻 true，析构函数收帧。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

咱们看实跑输出：

```text
创建 squares(5)，此刻函数体一行没跑
拿到 1
拿到 4
拿到 9
拿到 16
拿到 25
生成器耗尽，destroy 交给析构函数
```

咱们逐行走一遍。promise_type 的头两个成员是 `current_value` 与 `failure`，它们住在 promise 身上，跟着帧同生共死：`current_value` 存 `co_yield` 交来的值，`failure` 存异常指针。

您在 `get_return_object()` 里写下的 `from_promise(*this)` 从 promise 引用造出句柄，包进 Generator 交回调用者。协程帧在协程被调用时就存在了，Generator 只是把句柄接过来。

`initial_suspend()` 返回 `suspend_always`，咱们选了懒启动：创建时不跑函数体，调用方 `next()` 才开工。生成器几乎都选懒启动，因为值要按需产出，创建时一把跑完就违背了它的本意。

`yield_value(T value)` 是 `co_yield i * i` 的落点。值存进 `current_value`，返回 `suspend_always` 挂起，控制权交回 `next()`。您读值，走的是 `value()` 里的 `handle_.promise().current_value`，从句柄摸到 promise，从 promise 摸到值。

`return_void()` 对应 `squares` 函数体走到底的隐式 `co_return;`，promise 只需要空的 `return_void`。`unhandled_exception()` 这里咱们选了存储派：协程体里抛出的异常被 `std::current_exception()` 收进 `failure`，`next()` 在 resume 之后检查它，有异常就 `rethrow_exception` 原样抛给调用方。协程里的失败由此走到调用方手里，类型不变，消息原样，跟 4.1 里异步任务异常经共享状态传递是同一个思路，只是存放点从共享状态换成了 promise。

`final_suspend()` 带着 noexcept 返回 `suspend_always`，终点站挂着，帧等析构函数收。Generator 类本身是句柄的 RAII 包装：析构函数调 destroy，拷贝整个删掉。删拷贝的原因在句柄的定位里写着，它是非拥有的，两份拷贝指向同一帧，析构时会对同一块内存 destroy 两次。移动语义对生成器有用（把生成器从工厂函数里搬出去），最小版咱们省了，您动手时可以照 RAII 的常规做法补上。

输出那边，咱们看着五个平方数依次出来，随后 `next()` 里 `done()` 翻成 true，循环收束，gen 析构收帧。`squares(5)` 想产出多少个值由协程自己的循环决定，调用方要几个取几个，不想要了析构函数随时收场。上一篇讲的暂停与恢复，落到代码上就是三个环节的配合：`co_yield` 挂起，句柄恢复，析构销毁。

## 轮到您动手的时候

1. 把 `squares` 换成您自己的序列，斐波那契、读文件的行都行，跑通后把 limit 换成一个大数，体会值按需产出的意思。
2. 把 `initial_suspend` 的返回值换成 `suspend_never`，再跑一遍，看看创建时函数体直接跑到头一个 `co_yield`，对照 lazy_start 的输出。
3. 拿 frame_alloc 的计数器去数您自己的生成器，-O0 与 -O2 各跑一遍，看看您手头的编译器省不省堆分配。
4. 删掉 Generator 析构函数里的 destroy，用 valgrind 或 AddressSanitizer 跑一遍，亲眼看看泄漏从哪里冒出来。

## 下一步

咱们本篇用的挂起体只有 `suspend_always` 与 `suspend_never` 两件现成货，等待真实的对象（I/O、定时器、另一个协程的结果）得自己写 awaiter，`await_ready`、`await_suspend`、`await_resume` 的协议归下一篇《`co_await` 怎样参与等待》。再往后，《把协程组织成可完成的任务》把本篇的机制包成能接结果、能传异常、能安全销毁的 Task 类型，谁在哪个线程上恢复的问题也在那里落地。本篇的 Generator 是它们共同的底。后面两篇都还没写出来。

翻页之前，请您把动手清单里的第 2 条真的跑一遍，急启动的生成器在创建时就已经产出头一个值，把它观察清楚了，下一篇的 awaiter 才有对照的基准。

## 参考资源

本篇的三个实验初验在开头交代的临时目录与编译选项下完成，frame_alloc 额外在 -O0 下跑了一遍做对照，现在收在 code/examples/vol5/chapter07/，可在本页直接运行，抽成独立文件时复验过。return_void 缺失、显式拦截与双声明的验证出自修订用的另一段探针，单独验的，没有进正文，也不计入三个实验的计数，读数随实现与选项走。行为断言按 eel.is 上的现行工作草案核对，条款号与段号可能随草案更新挪动。cppreference 的 coroutines 页抓到了全文，与草案核对无出入。旧目录 ch06 的《C++20 协程基础》与《promise_type 与 awaitable》是本篇的素材，例子与文字全部重写。推进思路参照了 Lewis Baker 的协程系列，例子与文字都是我自己写的。

- [Coroutines (C++20)（cppreference）][cppref-coro]，协程定义、限制、协程状态的四样内容、参数拷贝与悬垂示例、分配的省略条件、promise_type 的推导、co_return 语义的出处。
- [Coroutine definitions（现行草案 dcl.fct.def.coroutine）][draft-def]，替换体的结构、get_return_object 的时序、分配与释放函数的查找规则、get_return_object_on_allocation_failure、参数拷贝规则、final_suspend 的 noexcept 要求。
- [Yielding a value（现行草案 expr.yield）][draft-yield]，co_yield 等价于 co_await promise.yield_value(e)。
- [Resumption（现行草案 coroutine.handle.resumption）][draft-resumption]，resume 与 destroy 的前置条件、跨执行代理恢复的实现定义行为。
- [Trivial awaitables（现行草案 coroutine.trivial.awaitables）][draft-trivial]，suspend_always 与 suspend_never 的 await_ready 语义。
- [Coroutine 支持库头文件的 synopsis（现行草案 coroutine.syn）][draft-syn]，协程库支持的结构，coroutine_handle 与两个挂起体都在这里。
- [The co_return statement（现行草案 stmt.return.coroutine）][draft-coret]，走到底与 return_void 重载决议的未定义行为出处。
- [P3950R1: return_value & return_void Are Not Mutually Exclusive][p3950]，“标准到 C++29 才把互斥放开”的前瞻断言出处。
- [std::coroutine_handle（cppreference）][cppref-handle]，非拥有句柄的成员清单，resume、done、destroy、promise、from_promise。
- [Understanding the Compiler Transform — Lewis Baker][baker-transform]，编译器变换的讲解，本篇推进思路的参照，例子自己写。

[cppref-coro]: https://en.cppreference.com/w/cpp/language/coroutines
[draft-def]: https://eel.is/c++draft/dcl.fct.def.coroutine
[draft-yield]: https://eel.is/c++draft/expr.yield
[draft-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[draft-trivial]: https://eel.is/c++draft/coroutine.trivial.awaitables
[draft-syn]: https://eel.is/c++draft/coroutine.syn
[draft-coret]: https://eel.is/c++draft/stmt.return.coroutine
[p3950]: https://open-std.org/jtc1/sc22/wg21/docs/papers/2026/p3950r1.pdf
[cppref-handle]: https://en.cppreference.com/w/cpp/coroutine/coroutine_handle
[baker-transform]: https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform
