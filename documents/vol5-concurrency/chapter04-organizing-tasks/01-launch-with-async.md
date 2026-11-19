---
title: "用 std::async 执行计算"
description: "把发起计算、稍后取结果交给库：async 发起任务，future 的 get 堵到结果就绪，值与异常都从它手里出来，get 过一次 future 就空，多人要结果走 shared_future。"
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 用 std::async 执行计算

第三单元收在《按协作需求选择工具》上，收尾留了一句话：下一单元从管理线程走向组织任务，`std::async` 与 `std::future` 让“执行一件事、稍后取结果”成为库里的现成调用，咱们在那儿见。咱们到了。前三单元您亲手干的事不少：线程的生灭归您管，共享的数据靠您手里的锁护着，每一处等待的结束路径也由您安排。干活的线程把结果送回来，桥倒是一直归咱们自己搭，共享变量配锁也好，标志位配通知也好，3.3《等待也需要结束路径》失败那一幕里那个 `failed` 标志，是演示里 `main` 替上游立起来的。

这一篇起，桥可以交给库了。`std::async` 把计算发起出去，`std::future` 把结果接回来，值是这样，失败也是这样。3.3 当时留了话：失败往上传的正路是 promise 与 future 的异常传递，归这一单元。正路有一半在这里就修好了，async 发起的任务抛出异常，库自动把它装进共享状态，`get` 在您自己的线程里原样重抛，类型不变，您照常按具体类型接。剩下那一半，亲手装异常的 `std::promise::set_exception`，归 4.3《自行传递结果：`std::promise` 与 `std::future`》。还有一件事得交代在开头：这一篇所有演示都用不带策略的默认写法，任务到底什么时候真正开始跑，归 4.2《任务什么时候执行：启动策略与等待行为》，那一篇专门谈。

## 把计算交出去：async 与 future

咱们从最小的形状看起。`std::async` 住在 `<future>` 头文件里，C++11 起（cppreference 的标注），是个函数模板：给它一个可调用对象和一组参数，它回一个 `std::future<R>`，R 是任务的返回值类型（[futures.async]/5）。标准给它定位的话就一句：提供一种机制，把一个函数可能在新线程上发起，函数的结果放进一个 future 对象，future 与它共用一段共享状态（[futures.async]/1）。“可能”两个字是标准自己写的，实现有权不让它真开线程，这层意思留给 4.2 展开。

共享状态是这套机制的中转站，标准专门给它立了条款。定义朴素：一段状态信息，加一份结果，结果可以是值，也可以是异常（[futures.state]/1）。状态什么时候算就绪也写得清楚：里面存着一份能取的值或者异常，才算就绪，ready 是标准给它的名字（[futures.state]/8）。咱们前三个单元自己搭的桥，桥面上跑的是共享变量加通知，这里全换成了库管的共享状态，谁往里放、谁能取，条款各管各的。

> 这里认两个词。往共享状态里放结果的对象，标准叫异步提供者，async、promise、packaged_task，都在这一侧（[futures.state]/4）。future 与 shared_future 这类读结果的，叫异步返回对象（[futures.state]/3）。您后文看到“这一头、那一头”，说的就是它们。

咱们把代码摆出来看。任务还是那个顶替真实计算的老办法：睡 300 毫秒，返回一个平方数。咱们让 `main` 发起以后不闲着，自己干五段 10 毫秒的小活，然后来取结果：

<OnlineCompilerDemo
  title="发起、自己干一小段活、get 把结果等回来"
  source-path="code/examples/vol5/chapter04/01_demo_get.cpp"
  description="async 发起 square(21)，main 干五段 10 毫秒的小活，get 一直等到 300 毫秒的结果就绪才返回。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
main  :   0.1 ms 计算发起，future 在手
main  :  51.3 ms 自己的活干完，来取结果
main  : 301.2 ms get 返回，square(21) = 441
main  : get 之后 valid() = 0
```

`get` 干的事，标准写成两步：等共享状态就绪，然后把里面存的值取出来（[futures.unique.future]/16）。取值捎带一步，释放共享状态，`valid()` 随之变成 `false`（/16.2 与 /17）。咱们从标准的描述里拿到一条保底：`get` 返回的时候，结果一定在手，活没干完它就一直堵着。301.2 毫秒那行就是保底的样子，`main` 的五段小活只花了 51 毫秒，剩下的时间是 `get` 替您等的。顺带交代返回值的形状：它按移动给出（/18.1），`int` 这类小东西您感觉不出差别，换成一个大的 `std::string`，值就是从共享状态里搬出来的，不复制第二份。

还有一句得说在明处：从上面的输出里，您看不出活是在 `main` 干自己的活的同时跑的，还是 `get` 调用时才开始跑的。读数答不了这个问题，两种安排都落在 300 毫秒上下。演示没有失误，读不出才正常：默认写法把选择留给了实现，4.2 给答案。只想等、不取值的话，`wait()` 也在（[futures.unique.future]/21），这一篇用不上它，您知道有它就行。

## 异常也走同一趟车

3.3 失败那一幕立过两个条件：上游干砸了得有地方记录，等待的一方得读得到。标志位是咱们当时自己动手的答案。`async` 把记录这半边接了过去：任务执行中抛出的异常，标准写明存进共享状态，与值待在同一个地方，名字叫 exceptional result（[futures.async]/3.1，显式选延迟策略时同样如此，/3.2）。`future` 这一头的 `get` 声明了自己抛什么。它抛的，是存进共享状态的那个异常（[futures.unique.future]/19）。两头一对，失败有了自己的路：任务里抛，`get` 到您这边重抛，重抛的就是存下的异常本身，类型不变，消息原样。

> future 与 shared_future，都备了引用与 void 两个特化（[futures.unique.future]/5、[futures.shared.future]/5）。差别只在 `get` 的返回：引用特化交还存下的引用，void 特化什么都不返回，等待与异常的语义照旧。咱们走主模板的主线，两个特化等真碰上了，到时再认。

失败的那一头也得有人接。咱们让 `divide` 在除数为零时抛 `invalid_argument`，`main` 这头照常 `try` 加 `catch`：

<OnlineCompilerDemo
  title="任务里抛的异常，get 在调用线程重抛"
  source-path="code/examples/vol5/chapter04/02_demo_exception.cpp"
  description="divide 在除数为零时抛 invalid_argument，main 的 catch 经 get 接到的是同一个异常，消息原样；除数正常时值照常从 get 出来。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
main: 捕获 invalid_argument，what() = 除数为零，除法做不下去
main: divide(6, 3) = 2
```

输出第一行就是 `get` 那一行抛的：`divide` 里 `throw` 的 `invalid_argument` 跑进了 `main` 的 `catch`，消息原封没动。咱们没有写一行跨线程搬错误码的代码，桥是库搭的。第二行是对照，活成了，值照常从 `get` 出来。3.3 那一幕要靠 `failed` 标志和谓词才散得了场，这里 `try` 加 `catch` 就把失败接住了。

异常的出口只有 get，另一个方向咱们也得看清：没人调 `get`，它就停在共享状态里，不打印，不终止程序，最后随共享状态一起销毁。3.3 立的两个条件，库替咱们包下了记录的一半，“读得到”还是您的事，您得真的来取。至于把 future 整个丢开不管的写法，析构那边还牵着一段特殊的等待，它决定您不留 future 的时候，程序会不会替您把任务等完，也决定任务抛出的异常还会不会被任何人看见，剩下的归 4.2。

3.3 留话的后一半，“承诺一侧把异常装起来、未来一侧取走”，装的那一半咱们这篇没碰。亲手装异常的入口是 `std::promise` 的 `set_exception`，主场在 4.3。

## 一次性消耗：get 过一次，future 就空了

future 这个名字容易让人生出能反复读的念头。标准的类定义开头就把念头拦下了。它写明：future 是一种返回对象，不与其他异步返回对象共享自己的共享状态（[futures.unique.future]/1）。类声明里，拷贝构造和拷贝赋值的位置写着 `= delete`，您想复制一份，编译器当场拒绝。一段共享状态，归一个 future 独占。

独占由 `get` 兑现。`get` 的效果除了等与取，还有释放共享状态（[futures.unique.future]/16），后置条件写着 `valid() == false`（/17）。您取过一次，这个 future 就与共享状态断了关联，再调 `get`、再调 `wait`，标准不给任何行为。它把话写成前置条件：`valid()` 为 `false` 的 future，除了析构、移动赋值、`share` 与 `valid` 本身，调任何成员都是未定义行为（/3）。

未定义行为演示不出来，能演示的是咱们手头的机器碰上它时干了什么。标准在同一处留了推荐做法：实现应当查出这类调用，抛一个 `std::future_error`，错误条件是 `no_state`（/3 的 Recommended practice）。我把探针跑了：

<OnlineCompilerDemo
  title="get 过一次的 future，再 get 一次会怎样"
  source-path="code/examples/vol5/chapter04/03_probe_double_get.cpp"
  description="第二回 get 在标准层面是未定义行为；本机的 libstdc++ 按标准的推荐做法抛 future_error，消息是 No associated state。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
第一回 get = 42，valid() = 0
第二回 get 抛了异常：std::future_error: No associated state
```

本机的 libstdc++ 照着推荐做法做了，抛出 `std::future_error`，消息就是输出里印着的 No associated state。它是实现的善意，不是标准的承诺，换一份实现，它也可以直接把进程带走。咱们代码里能靠的只有前置条件本身，还有随身带着的问询工具：`valid()` 返回 true 当且仅当对象还关联着共享状态（/20），拿不准就问它，问出来是 `false` 就收手。future 还只移动：移动构造把共享状态转给新对象，原来的 `valid()` 变 `false`（/8、/9）。想把结果交给别人，用移动，标准没给复制的路。

咱们把对照的两种设计摆在一起看。可拷贝的 future 要让几个人各持一份结果，就得给共享状态配引用计数，谁来都加一，走时都减一，维护的活跟着每一次调用走。独占的 future 不用背这个包袱：谁取走了结果永远只有一个答案，`get` 因此敢把值按移动交出来，敢在取完就释放共享状态。多处都要读的场合，库备了另一件工具，下一节请它出场。

## 几个人都要结果：shared_future

场面咱们常碰：一份结果，几条线程都要。future 独占，复制不过去，硬把同一个 future 移动给几个人，前一个拿到手的已经空了。库给的答案是 `std::shared_future`，同样从 C++11 起。它的类定义与 future 只差一个词：多了一个“可以”，它可以与其他异步返回对象共享自己的共享状态（[futures.shared.future]/1）。拷贝构造是正经成员，拷出来的每一份都指向同一段共享状态（/8）。

咱们要换过去，路有两条。`future::share()` 把手里的共享状态交出去，返回一个 `shared_future`，自己变成无效（[futures.unique.future]/13、/14）。`shared_future` 也有从 future 右值转换构造的入口（[futures.shared.future]/10），`std::shared_future<std::string> sf = std::async(load_config);` 这样的写法直接成立，我用探针验过：

<OnlineCompilerDemo
  title="shared_future 直接从 async 的返回值构造"
  source-path="code/examples/vol5/chapter04/04_probe_shared_from_async.cpp"
  description="不经 share()，async 返回的 future 右值直接转换构造出 shared_future，同一份拷贝连取两回都拿得到结果。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

三条线程分食一份配置的现场，咱们直接看。`main` 发起读取，`share` 出去，三条 reader 各拿一份拷贝，各自 `get`：

<OnlineCompilerDemo
  title="一份配置，三条线程都要：shared_future"
  source-path="code/examples/vol5/chapter04/05_demo_shared.cpp"
  description="share 之后原 future 的 valid() 变 0，三条 reader 各拿 shared_future 的拷贝各自 get，末尾同一份拷贝连取两回。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
main: share 之后，future 的 valid() = 0
reader 1: port = 8080
reader 0: port = 8080
reader 2: port = 8080
main: 第一回 get 到 port = 8080
main: 第二回 get 到 port = 8080
```

`share` 之后原 future 立刻空了，`valid()` 是 0，结果没有丢，它已经归 `shared_future` 管。三条 reader 各拿各的拷贝，`get` 在谁的拷贝上调用，谁就等就绪，150 毫秒的活干完，三行读数一起出来，次序由调度排，我重跑一遍，读到的是 0、1、2。末尾两行是同一份拷贝连取两回，`shared_future` 不还手。换成 future，这个场面就走不通：取过一次就空了，第二个人没得取。

咱们把 `shared_future::get` 与 future 的差别归拢一下，两处，标准都写明了。它等就绪、取值，但不动共享状态，取完还能再取（/19）。`get` 交回来的是一份 const 引用，指着共享状态里存的那个值，值仍留在原处，这与 future 交值本身不同（/20.1）。异常照旧从它重抛（/21）。标准补的注说得明白：共享状态销毁以后再经由引用访问，未定义行为，别把引用存进活得比 `shared_future` 自己久的地方（/20.1 的注）。您要长期持有，拿到引用就拷一份存自己的变量里。

几条线程同时读行不行？cppreference 的一句话答了：每条线程各用自己的拷贝去访问同一共享状态，是安全的。标准的注把边界也划了出来：`shared_future` 的成员函数彼此不同步，但都与共享状态同步（/2 的注）。经由 `get` 返回的引用并发访问其中的值，要么只读，要么您自己再补同步（[futures.state]/11 的注）。照着做就行：一人一份拷贝，读结果。要写，另请 mutex，那是第二单元的地盘。

## 轮到您动手的时候

动笔之前，我把这一篇攒下的问题摆成一列，您对着自己的代码过一遍：

1. 从 async 发起、到 get 返回，中间这段空档您的程序在干什么？get 是会堵的，堵多久由活的长短说了算，调用点等得起吗？
2. 每一个 get 的调用点，异常有人接吗？接住之后报给谁，报得清楚吗？
3. 对同一个 future，get 走了几回？循环与分支里反复取的地方，第二回走的是哪条路？拿不准的，valid() 问过吗？
4. 一份结果几个人要，用的是 shared_future 还是各自 async？各自发起的写法把同样的活干了几遍，值得吗？
5. 从 shared_future::get 拿回来的引用，有没有被存进活得比它久的地方？

## 下一步

单元的第二篇是 4.2《任务什么时候执行：启动策略与等待行为》。这一篇留了两条线给它：默认策略下，实现什么时候选 async、什么时候选 deferred，`wait_for` 的 `deferred` 返回值怎么读。还有 async 返回的 future 的析构，它可能让您的程序在原地等着，上面说的“任务抛出的异常还会不会被任何人看见”，也系在析构的等待行为上。两件事凑齐，“发起一个任务”这件事才算被咱们看全。

翻页之前，请您把 3.3 里那个锁、标志、通知、join 齐全的等待程序翻出一份，试着改写成 async 加 get，waiter 等的货改成一份 future 的结果。改完数一数删掉的行：锁、标志、通知、join，哪几样删得掉，哪几样换成了库搭的桥，您数完就知道了。

## 参考资源

五段代码收在 `code/examples/vol5/chapter04/`，可在本页直接运行。编译器是 GCC 16.2.1，命令 `g++ -std=c++20 -O2 -pthread`，另加 `-Wall -Wextra` 无警告，机器是 WSL2，16 逻辑核，初验日期 2026-11-16，抽成独立文件时复验过。正文挂出其中四段，`shared_future` 直接从 async 返回值构造那句，是第五段探针单独验的，也一并收在示例里。demo_get 里 get 返回的读数落在 300 毫秒上下，我重跑过一遍，读到 300.5，零头是唤醒与调度。reader 三行的次序由调度排，两遍分别读到 1、0、2 与 0、1、2。结构性的结果每次一致，它们是 valid 的翻转、异常的类型与消息，还有 `shared_future` 的多路取值。二次 get 在标准层面是未定义行为，本机的 libstdc++ 按推荐做法抛 `std::future_error`，别的实现可以另作选择。条款一律按 eel.is 上的现行工作草案核对，小节名跟正式标准一致，段号可能挪。cppreference 的 async 与 shared_future 两页本回抓到了全文，与草案核对无出入。版本标注按它写的来，async、future、shared_future 都是 C++11。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是我自己写的。

- [std::async（cppreference）][cppref-async]，C++11 起，默认写法等效 async 与 deferred 的组合。
- [std::future（cppreference）][cppref-future]，C++11 起，只移动。
- [std::future::get（cppreference）][cppref-get]，等就绪、交出值或重抛异常，调用后 valid 为 false。
- [std::shared_future（cppreference）][cppref-shared]，C++11 起，可拷贝，多线程各用自己的拷贝访问同一共享状态是安全的。
- [Function template async（现行草案 futures.async）][draft-async]，默认策略的定义、异常存入共享状态、发起与完成的同步关系。
- [Class template future（现行草案 futures.unique.future）][draft-future]，get 的效果与后置条件、valid 为 false 时调其他成员的未定义行为与推荐做法、share 的语义。
- [Class template shared_future（现行草案 futures.shared.future）][draft-shared]，get 返回 const 引用与它的生存期注、从 future 转换构造、成员同步的注。
- [Shared state（现行草案 futures.state）][draft-state]，共享状态与 ready 的定义、异步提供者与异步返回对象、结果存入与等待返回之间的同步关系、并发访问同一结果的注。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-async]: https://en.cppreference.com/w/cpp/thread/async
[cppref-future]: https://en.cppreference.com/w/cpp/thread/future
[cppref-get]: https://en.cppreference.com/w/cpp/thread/future/get
[cppref-shared]: https://en.cppreference.com/w/cpp/thread/shared_future
[draft-async]: https://eel.is/c++draft/futures.async
[draft-future]: https://eel.is/c++draft/futures.unique.future
[draft-shared]: https://eel.is/c++draft/futures.shared.future
[draft-state]: https://eel.is/c++draft/futures.state
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
