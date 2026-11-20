---
title: "谁负责线程和数据的生命周期"
description: "看清 std::thread 参数的 decay-copy 与 move-only 传法，认识悬垂引用的两种修复路径，用 thread_guard 与 joining_thread 把 join 的责任交给析构函数。"
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 谁负责线程和数据的生命周期

上一篇《第一个 std::thread 程序》结尾咱们许诺的事，这一篇兑现：参数复制怎么发生，引用什么时候悬垂，join 的责任怎么从您的记性里接出去。三件事背后立着同一个问题，写多线程程序的人迟早要在代码评审里被问到：线程起来之后，谁负责它和它手里数据的生命周期？落到每一条执行流上，要回答的是它的数据是谁给的，这些数据活到什么时候，收尾由谁兜底。上一篇您已经见过不收尾的下场，一个还关联着线程（joinable）的线程对象析构时整个程序直接终止。这一篇把收尾写进析构函数，让它不再靠您记得调用。

文中全部示例都收在本页的在线示例里，您点开就能直接运行；我们本地实测的配方是 GCC 16.2.1、`g++ -std=c++20 -O2 -pthread`、WSL2（2026-11-14），涉及标准库行为的断言都查过 cppreference，版本按段标注，没有逐条标注的见参考资源。悬垂引用这类未定义行为的实验，跑出来的样子跟机器、调度和优化档都有关系，咱们把真实输出原样放在您面前，包括那些看起来毫无异常的运行，那恰好是这类问题最阴险的部分。

## 参数怎么传：decay-copy 与 move-only 类型

### 复制发生在发起线程

上一篇留过一个尾巴：`std::thread t(print_hello, 42)` 这一句里“参数被复制”，复制到底怎么发生的。咱们把 cppreference 在 [`std::thread` 的构造页面][cppref-ctor] 的规定翻出来看（C++11 起），新线程开始执行的是这样一个调用：

```text
INVOKE(decay-copy(std::forward<F>(f)),
       decay-copy(std::forward<Args>(args))...)
```

decay-copy，退化复制，是这里的正式名目。它的意思用白话说就是：每个实参都按函数模板传递参数的路数处理一遍，引用剥掉，顶层的 const 和 volatile 丢掉，数组退成指针，函数退成函数指针。同一页还有一句要紧的话：这些复制发生在当前线程，也就是您发起线程的那一侧，复制或者移动参数时抛出的异常都留在发起线程，新线程根本不会因为复制失败而启动。C++23 起标准把这里的写法换成按 auto 的规则生成参数副本，名字变了，规定没变，参数照旧在发起线程里定下来，异常照旧留在发起线程，这里只是措辞说明，本篇示例照旧按 C++20 编译。

空口无凭，咱们让拷贝构造函数自己报一下它在哪个线程干活、拷出来的对象放在哪：

<OnlineCompilerDemo
  title="decay-copy：传进线程的是副本"
  source-path="code/examples/vol5/chapter01/22_decay_copy.cpp"
  description="拷贝构造在线程一侧执行、副本地址各不相同；指针传的只是地址，数组不会被复制。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来的输出如下，线程编号是咱们机器上的值，您那里会不同，地址也会变：

```text
main thread id 127628787791744
original lives at 0x7fff145e0bb4
  copy ctor on thread 127628787791744, copy lives at 0x5bfa3b942038
  copy ctor on thread 127628780500672, copy lives at 0x7413e41fedd4
  worker got its own object at 0x7413e41fedd4, value 7
  worker sees an int* holding 5 elements
  sum 15
```

第一次拷贝的线程编号跟 main 一模一样，这就是“复制发生在发起线程”的现场证据，decay-copy 把 `original` 复制了一份存进线程对象的内部存储。第二次拷贝发生在新线程上，新线程拿存好的副本去调用 `work`，而 `work` 的参数按值收，于是又拷贝了一回，`worker got its own object` 报的地址就是函数参数里的拷贝。您数一下地址，原对象、内部存储、函数参数，三处各不相同，线程手里拿到的永远是自己的副本。

数组那一半输出另有话说，它对应 `read_array`：`arr` 在退化复制里变成了 `int*`，长度信息当场丢失，所以咱们得多带一个 `n` 进函数签名。引用呢？非 const 引用绑定不上，马上就能见到。const 引用能编译，但它指的是线程内部的副本。想共享得写出 `std::ref`，下一小节讲。

### 想共享，得显式写出 ref

假如线程函数就是想要一个引用，直接传会发生什么，咱们编译了一回：

<OnlineCompilerDemo
  title="引用直接传参：编译不过"
  source-path="code/examples/vol5/chapter01/23_ref_compile_error.cpp"
  description="decay-copy 后的副本绑不回非常量引用，编译器直接拒绝；错误写法留在注释里，报错见正文，正确写法用 std::ref。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
error: static assertion failed: std::thread arguments must be invocable after
conversion to rvalues
```

`update_value` 想要 `int&`，退化复制之后线程手里只有一份 `int` 的右值，右值绑定不上非 const 引用（报错的是 libstdc++ 16.2.1 的 static_assert，其余几行报错都是它带出来的连锁）。这个编译错误是标准库在执行“默认按值”的立场：共享是危险动作，您想共享，就得在代码里写出共享两个字，让审查代码的人一眼看见。写出来的工具是 `std::ref` 和 `std::cref`（都是 C++11 起，定义在 `<functional>`）：它们把引用包成一个可以复制的 reference_wrapper（引用包装器），线程调用函数时把包装解掉，函数收到的还是对原始对象的引用。

<OnlineCompilerDemo
  title="std::ref 与 std::cref：真的引用"
  source-path="code/examples/vol5/chapter01/24_ref_wrap.cpp"
  description="用引用包装器之后，worker 摸到的就是 main 的同一个对象，地址一致、修改可见。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来 `worker touches str at` 和 `main's message lives at` 两行地址完全相同，`message` 也真的变成了 `Hello World`，线程改的就是 main 里那个对象。这个例子安全，因为 `join()` 在场，线程用 `message` 的时候它还活着。要是把 join 换成 detach，线程对 `message` 的引用什么时候过期就没有人管了，下一节解剖的悬垂引用正是这类事。

### move-only 类型：所有权跟着线程走

并不是什么类型都拷得动。您在卷二讲智能指针的部分熟悉过的 `std::unique_ptr` 就是一个，这类只可移动的类型（拷贝构造被删除，只支持 move 的类型），move 进线程才是正路：

<OnlineCompilerDemo
  title="move-only：unique_ptr 进线程"
  source-path="code/examples/vol5/chapter01/25_move_unique.cpp"
  description="所有权随 std::move 交进线程，main 侧变空指针，各归各的活。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来 `main: data after move is null`，`std::move(data)` 把整块内存的所有权转进了线程的内部存储，main 手里的指针当场变空，worker 那边正常改完数据。线程函数 `process_data` 独占转进线程的整块数据，用完不用交接，函数返回时 `unique_ptr` 自动释放。在这段代码里，线程是这块数据的唯一持有者，没有第二个执行流读写它。

## 悬垂引用：两种修复路径

悬垂引用说的是一件具体的事：引用还攥在线程手里，它指着的对象已经没了。上面讲引用共享时换掉 join 的那半截正是高发区。咱们把两种现场都跑给您看，再给两条修复路径。

### 现场一：detach 之后的局部变量

<OnlineCompilerDemo
  title="悬垂引用：读旧栈上的垃圾"
  source-path="code/examples/vol5/chapter01/26_dangling_ref.cpp"
  description="按引用捕获活不过函数的局部变量，函数返回后栈被覆盖，线程读到什么全看运气——这也可能看起来毫无异常。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

`spawn` 里的 `local_value` 是栈上的局部变量，lambda 按引用捕获它，线程被 detach 带走了。`spawn` 一返回，`local_value` 就销毁了，而分离出去的线程还要在 150 毫秒后读它。main 那侧咱们安排了 `burn_stack`，故意往栈上灌一万六千字节的别的数据。

咱们跑了十回。-O2 那五回全部打出 `thread reads: 42`。换 -O0（关掉优化）再跑五回，五回全部打出 `thread reads: 589505315`。这个数有来历，它是十六进制的 0x23232323，正好是 `burn_stack` 往栈上灌的井号（# 的 ASCII 编码是 0x23）四个字节拼成的整数，线程读到的是别人写进这块内存的数据。同一份代码，优化档一换，读到的就从原值变成了别人的数据，背后是两档下函数栈帧的落点不同，咱们没有去核对反汇编，机制点到为止。这个结果要如实交给您：它不代表 -O2 那五回安全。对象销毁之后再读它，标准不提供任何保证，读到原值、读到垃圾、把进程带崩，全都在许可范围里。我跑完的感受是，这一回实验看到了悬垂读取换优化档就换数据，没看到它当场崩溃，真要把崩溃逼出来，得换个调用深度或者换台机器再试。

### 现场二：lambda 捕获的 this 指针

您将来在类里起线程时，最常见的形态是成员函数里起线程、lambda 捕获 `this`。捕获 `this` 捕的是指针，对象活多久，这个指针才算活多久。

<OnlineCompilerDemo
  title="捕获 this 后销毁对象：未定义行为"
  source-path="code/examples/vol5/chapter01/27_this_capture_ub.cpp"
  description="线程攥着对象指针跑，对象先走一步。读到垃圾、看似正常或崩溃都是它的样子。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

这个程序是故意写错的，咱们跑它属于观察错误的形态。`start` 之后立刻 `delete w`，线程手里只剩一个指向已销毁对象的 `this`。咱们跑了三回，每回八行输出里 `running_` 的读数是这样的：一回是 114，一回是 39，一回是 82，同一回内部稳定，跨回不同。`running_` 的真值是 `true`，正常读出来应该是 1，三个数字没一个是 1，线程读到的是 `delete` 之后被分配器另作他用的内存。至于谁写的 114、谁写的 39，不知道也不需要知道，反正那已经不是您的对象了。

就算您在析构函数里把标志改成 false（这是这类代码常见的自我安慰），也拦不住时间窗：detach 之后没有任何机制等线程退出，标志改完的那一刻，线程可能正睡在半路，醒来照样去读一块不属于任何人的内存。

还有一类同根的形态，上一篇《第一个 std::thread 程序》的函数对象您已经见过：`Accumulator` 整个被复制进线程，它里面的 `data_` 是引用成员，复制进线程的只是引用本身。判断法和 lambda 的引用捕获是同一条，被指的 vector 必须活到线程用完它，两条修复路径对它同样适用。怎么选？咱们从便宜的一条走起。

### 修复路径一：把数据复制进线程

第一条修复路径两个现场都能修，咱们拿现场一来演示，它最省事：把引用换成值，线程拿自己的副本。

<OnlineCompilerDemo
  title="值捕获：线程拿自己的副本"
  source-path="code/examples/vol5/chapter01/28_value_capture_safe.cpp"
  description="同样的场景改成值捕获，父函数先返回也不怕，线程打印完整如初。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

`[message]` 值捕获，lambda 自己持有一份副本，`std::thread` 构造时又把整个闭包对象退化复制进内部存储。线程读的从头到尾都是自己的数据，外面的 `message` 销毁与它无关，detach 在这里就是安全的。咱们跑了三回，三回都是 `thread sees: Hello from parent`。

值捕获就是拷贝，`int`、指针这类小对象无所谓，一个几百兆的 `vector` 就要掂量了。咱们拷贝不起的时候，就走第二条路。

### 修复路径二：用 shared_ptr 延长生命周期

第二条修复路径主修现场二，针对的情形是对象和线程各活各的、又必须共享同一份数据。数据拷不动，或者您掂量完觉得拷不起，`std::shared_ptr`（共享所有权的智能指针，内部用引用计数决定对象什么时候销毁）是现成的折中：只要还有一份 `shared_ptr` 指着对象，对象就不销毁。

<OnlineCompilerDemo
  title="shared_ptr 加 atomic 标志：安全收场"
  source-path="code/examples/vol5/chapter01/29_shared_stop_flag.cpp"
  description="对象销毁后线程靠共享的停止标志干净退出，引用计数替您管好了生命周期。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们那一回跑出来的三行长这样：

```text
worker object already destroyed
worker exits cleanly
main exits
```

咱们读的是其中一次运行的记录，这段代码有两条可能的时间线，各自的输出都合法。worker 第一次读标志可能发生在析构之前：它读到 true，进入 200 毫秒睡眠，析构把标志置假，线程醒来读到 false，退出循环，打出 `worker exits cleanly`。要是线程在 stop 之后才第一次读标志，它立即退出，打出的是同一行。哪条时间线在跑、三行输出按什么次序落，都没有保证，咱们就按看到的记录来读。shared_ptr 保证 `atomic<bool>` 对象活到线程最后一次读它，对象的生命周期跟着最后一份共享指针走，而不是跟着拥有它的那个类走。

cppreference 在 [shared_ptr 的页面][cppref-shrptr] 写明：不同的 `shared_ptr` 对象可以并发调用成员函数，哪怕它们共享同一对象的所有权，实现说明里引用计数用原子操作维护。shared_ptr 只保证对象不会被过早销毁，它管的是所有权和控制块（记录引用计数的那块内部数据），被管理对象内部的并发访问仍然要您自己安排。这个例子选 `atomic<bool>` 当共享对象，就是为了让标志自己站得住，换成 `shared_ptr<std::vector<int>>` 再让两个线程同时 `push_back`，该乱还是乱，那是下一单元讲数据竞争的那几篇的话题。

### 捕获的语法与它的年代

两条修复路径之外，lambda 的捕获写法本身还有几句要说。`[&]` 一刀切把所有局部变量都按引用捕进来，包括您没打算共享的那些，函数一长，审查的人就分不清哪条引用是故意留的。显式写出捕获列表，要哪个写哪个，意图和风险都摆在明面上。

捕获整个对象还有一条专用语法，`[=, *this]`，咱们把 `*this` 按值捕获，线程拿到整个对象的副本。

> 它 C++17 才进标准。咱们在 GCC 16.2.1 下试过：`-std=c++17` 和 `-std=c++20` 直接编译通过，`-std=c++14` 下 GCC 把它当扩展接受，给一条 `-Wc++17-extensions` 警告，加 `-pedantic-errors` 才真的编译不过。

另一条是废弃警告。C++20 起，用 `[=]` 隐式捕获 `this` 被废弃了（提案 P0806，链接收在参考资源），咱们拿一小段代码实测，`-std=c++20` 加 `-Wdeprecated` 时 GCC 16.2.1 给出：

```text
warning: implicit capture of ‘this’ via ‘[=]’ is deprecated in C++20 [-Wdeprecated]
    4 |         return [=] { return n; };
note: add explicit ‘this’ or ‘*this’ capture
```

语法怎么变，背后的原则只有一条：被引用的对象必须活到线程用完它为止。两条修复路径是您手工维持这句话成立的办法，下一节的 RAII 把这件事交给析构函数自动做。

## 所有权与 RAII：thread_guard 与异常安全

RAII（资源获取即初始化）您在卷二讲 RAII 的部分用过，一句话重申：把资源的收尾写进析构函数，对象离开作用域时自动执行，正常返回和异常退出都不漏。咱们下面就把线程当作资源来管。

### 线程对象自己也是资源

管线程之前，要看清 `std::thread` 对象本身是什么：它是底层线程的唯一所有者，而这个所有权只能 move，不能拷贝。咱们试着拷贝了一回：

<OnlineCompilerDemo
  title="std::thread 不可拷贝"
  source-path="code/examples/vol5/chapter01/30_thread_not_copyable.cpp"
  description="拷贝构造被删除，编译不过；错误写法留在注释里，所有权只能 std::move。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
error: use of deleted function ‘std::thread::thread(const std::thread&)’
```

拷贝构造被删除是刻意设计，咱们把理由说清：一条操作系统线程同一时刻只能被一个对象管理，允许拷贝就会出现两个对象抢着 join 同一条线程的混乱局面。move 是唯一的所有权转移方式，move 之后的源对象不再关联任何线程：

<OnlineCompilerDemo
  title="joinable 与 move：转移后源为空"
  source-path="code/examples/vol5/chapter01/31_joinable_move.cpp"
  description="move 之后再问 joinable，源线程已是 false，新对象接管执行流。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来 `t1 joinable: 1`，move 之后 `t1 joinable 0`、`t2 joinable 1`。这套唯一所有者加 move 的形状，和 `std::unique_ptr` 相似，差别在收尾：`unique_ptr` 赋值时释放旧资源，`std::thread` 在目标还 joinable 时直接 terminate。标准库里的 `std::fstream`、`std::future` 也是这套唯一所有者的思路。

### 异常路径上的 join：thread_guard

上一篇咱们见过没 join 的 terminate，也见过线程函数里抛异常的 terminate，但主线程自己抛异常、把写在后面的 join 跳过的例子还没有。函数中途抛异常，写在后面的 `join()` 永远执行不到，栈展开（异常沿调用栈逐层退出、沿途析构局部对象的过程）析构到还没 join 的线程对象，程序照样终止。

解法是上一篇说的那类包装器，这里给持有引用、不拥有线程的版本（默认在析构里 join）。书里管它叫 thread_guard，咱们代码里写成 ThreadGuard，是同一个东西的两种拼法。Anthony Williams 的《C++ Concurrency in Action》给过这个经典形态，咱们照它的推进思路写了自己的版本，连同验证程序一起放在这里：

<OnlineCompilerDemo
  title="ThreadGuard：异常也收得了场"
  source-path="code/examples/vol5/chapter01/32_thread_guard.cpp"
  description="栈展开时守卫对象的析构函数替您 join，异常穿过也不丢线程。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来三行，次序值得您读一遍：

```text
  worker finished its work
caught: boom
main continues normally
```

`risky()` 抛出异常，`run_with_guard` 的栈开始展开，`guard` 的析构函数被调用，join 阻塞了 150 毫秒等 worker 干完活，然后异常才被 main 的 catch 接住。要是把 guard 拿掉，这个程序会以 terminate 收场，worker 的输出都没有机会打出来。您靠着 guard，把异常路径上的 join 写进了析构函数，它不再靠您记得调用，作用域退出时析构函数自己执行。

这个版本是个半成品，咱们把两个弱点摆出来。guard 持有的是引用，线程的所有权还在外面的 `t` 手里，`t` 和 `guard` 谁管收尾，代码里没有答案。被引用的 thread 对象必须比 guard 活得久，它要是早一步销毁（堆上 delete、成员销毁顺序之类），还 joinable 的话，thread 自己的析构当场 terminate，轮不到 guard 析构。已经 join 过或 detach 过的话，guard 的析构去读的就是已经销毁的对象，未定义行为。这一版证明析构函数接得住 join，所有权还没着落，下一节的包装器补的就是这一块。

### join 自己也会抛

join 自己就万无一失了吗？它本身可能抛异常。cppreference 在 [join 的页面][cppref-join] 写明，出错时抛 `std::system_error`（装着错误码的异常，`code()` 取出错误码，`message()` 给出文字），并列出三种条件：`joinable()` 为假（`invalid_argument`）、线程不存在（`no_such_process`）、线程 join 自己（`resource_deadlock_would_occur`）。咱们把其中两种实测了：

<OnlineCompilerDemo
  title="join 的错误路径：system_error"
  source-path="code/examples/vol5/chapter01/33_join_throws.cpp"
  description="join 自己抛 resource_deadlock_would_occur，重复 join 抛 invalid_argument，都能接住。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
  second join threw: Invalid argument
  self join threw: Resource deadlock avoided
main continues
```

第二条消息是 EDEADLK 这个 errno 的字面译文（资源死锁已避免），线程 join 自己被实现当场拦下。麻烦在于咱们把 join 写进析构时，析构不该往外抛异常，析构执行中再有异常逸出就是 terminate，所以务实的写法是在析构里把 join 包进 try，失败记一笔，不往外抛。下一节包装器的析构函数就是这么写的，`thread_` 是它手里那个 `std::thread`：

<OnlineCompilerDemo
  title="JoiningThread 的析构：聚焦"
  source-path="code/examples/vol5/chapter01/34_joining_thread_dtor.cpp"
  description="joinable 才 join，失败也不让异常逃出析构函数——自动 join 的全部要点在这几行。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

### 裸 thread 的 move 赋值是 terminate

所有权转移还有一处意外在赋值上。cppreference 在 [operator= 的页面][cppref-opassign] 写明（C++11 起）：如果赋值目标还关联着一条线程（`joinable() == true`），就调用 `std::terminate()`。咱们实测：

<OnlineCompilerDemo
  title="move 赋值给已关联的线程：terminate"
  source-path="code/examples/vol5/chapter01/35_move_assign_terminate.cpp"
  description="目标对象还 joinable 就被赋值，std::terminate 直接收场，本示例预期崩溃。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们编译运行，标准错误一行，退出码 134（进程被 SIGABRT 终止，上一篇咱们算过 128 加 6 这笔数）：

```text
terminate called without an active exception
```

裸 `std::thread` 的所有权交接到这里露全了面：move 构造畅通，move 赋值给一个还没收尾的对象就直接终止程序。我在这件事上选包装器的路：接新的那一步，把手里旧的 join 完再 move，下一节的包装器就写这一行。它的约定说全：join 失败时记录一笔，然后 terminate，这是这套写法的边界。

## joining_thread：接管所有权的手写方案（扩展）

本节带（扩展）标记，跳过它不影响本篇主线。读法按上一篇的约定：您照行为要求自己写一版，再回来对照，写了，下一篇的 `std::jthread` 对您就是旧相识。名字这里对齐一次：书里管这类包装器叫 joining_thread，C++ Core Guidelines 的 CP.25 建议优先用这类自动 join 的包装器，GSL（Guidelines Support Library）的 README 注明它尚未实现，所以咱们手写一版 JoiningThread，同一个思路。

设计一句话就能说完：让包装器直接拥有线程，析构时自动 join，move 赋值时把手里旧的收掉再接新的。完整实现如下，咱们把每个决定都写了注释：

<OnlineCompilerDemo
  title="JoiningThread：接管所有权的完整写法"
  source-path="code/examples/vol5/chapter01/36_joining_thread.cpp"
  description="构造接管、析构自动 join、move 赋值先收旧的，容器里放一排也各归各的场。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

move 赋值里那句把旧的 join 完再接手，对着的正是裸 `std::thread` 在同一位置的 terminate，标准选了响的，包装器选了顺的，哪边合理，看您程序自己的退出策略。join 自己也会抛，道理前面讲过了。析构和 move 赋值里的两个 try 落实的正是它，join 抛了就记一笔，绝不让异常从 noexcept 函数里逸出。错误记下来之后，失败路径的结局仍是 terminate：move 赋值下一步一执行，目标 `thread_` 还 joinable，赋值就 terminate。析构体走完，成员 `thread_` 还 joinable，成员析构也 terminate。两条路径咱们都实测过，以 134 收场。五回重跑走的是正常路径，失败边界没人踩到，但它在约定里。模板构造函数把参数原样转发，用法上没有多出接口，行为上多了两件：析构自动 join，move 赋值收旧的再接新的。咱们跑了五回，下面是其中一回的完整输出：

```text
-- phase 1: two threads, nobody calls join by hand
task 1 done
task 2 done
-- phase 2: reassignment joins the old thread first
task 4 done
task 3 done
-- phase 3: a vector of JoiningThread
task 5 done
task 6 done
task 8 done
task 7 done
main exits, every thread was joined by destructors
```

Phase 1 没有人手动 join，咱们看到两个任务都被析构收掉。两行打印的次序五回里三回 task 1 在前、两回 task 2 在前，两个任务几乎同时收工，打印发生在各自的线程里，谁在前头由调度定。

Phase 2 的机制是咱们写死在代码里的：`t = JoiningThread(task, 4)` 这一句，右边的临时对象在赋值发生前就构造完，task 4 的线程已经启动，赋值动作把旧的 task 3 join 完才接管 task 4。五回里两回 task 3 在前、三回 task 4 在前，打印次序没有保证。能担保的是正常路径上包装器的代码把旧的 join 完才接管，对照的裸 `std::thread` 在同一位置直接 terminate。

Phase 3 是 `std::vector<JoiningThread>`，`reserve` 预留好空间之后逐个 `emplace_back`，作用域结束时每个元素析构时各自 join，四条线程五回全部收掉，没有落下任何一个。四行完成的次序五回里出现了四种，vector 元素的析构次序标准没有规定，咱们也不押它，可靠的是每个元素析构都会 join。

> Phase 3 还贡献过一行碎输出，`task task 65 done` 连着半行 done，两行打印绞在了一起。`std::cout` 的竞争上一篇您已经见识过。

咱们往标准库看一眼。C++20 的 `std::jthread` 就是这套设计收编后的正式版本：构造、move、析构的特殊成员形状与咱们的 `JoiningThread` 一致，析构自动 join，move 赋值收掉旧的再接新的，另外多出一套协作式停止的协议（`stop_source`、`stop_token`、`stop_callback`）。咱们下一篇专门讲多出来的那一套。

## 下一步

这一篇把生命周期问题的答案落在了数据与线程两侧。数据这一侧，要么复制进线程（默认的退化复制一直在做这件事），要么用 `shared_ptr` 把生命周期延到最后一份共享指针消失。线程这一侧，所有权只能 move，join 写进析构函数，不再靠您记得调用。您手里现在有可检验的东西：一个 ThreadGuard、一个 JoiningThread，外加两段修复代码，都能搬进您自己的程序里跑。还差一类角色没安排：循环一直跑到被叫停的 worker，`JoiningThread` 析构时能等它，却没法通知它该收了，等一个不知道要跑多久的循环，析构就变成无限期阻塞。让工作有机会被通知结束，是下一篇《让工作有序结束》的主题，`std::jthread` 与协作式停止协议在那里等着您。

## 参考资源

本篇写作时对照借鉴了 Anthony Williams 的《C++ Concurrency in Action》（Manning 出的第二版），thread_guard 的推进思路借自它。joining_thread 也是照它的路子写出来的，例子与文字都是我自己写的。涉及 C++ 行为的断言，均按文中标注编译实测或查证 cppreference，std::thread、ref/cref、shared_ptr 自 C++11 起可用，make_unique 是 C++14，[=,*this] 是 C++17，jthread 与停止协议是 C++20。悬垂引用的输出按真实运行记录，未定义行为的运行结果在您的机器上可能不同。

- [std::thread::thread（cppreference）][cppref-ctor]，构造语义，退化复制在发起线程求值的规定。
- [std::thread::~thread（cppreference）][cppref-dtor]，joinable 状态下析构调用 terminate 的规定。
- [std::thread::join（cppreference）][cppref-join]，同步保证与 system_error 的三种错误条件。
- [std::thread::operator=（cppreference）][cppref-opassign]，joinable 状态下 move 赋值调用 terminate 的规定。
- [std::thread::detach（cppreference）][cppref-detach]，分离语义。
- [std::ref 与 std::cref（cppreference）][cppref-ref]，reference_wrapper 的生成与拆包。
- [std::shared_ptr（cppreference）][cppref-shrptr]，并发调用与引用计数的保证范围。
- [std::jthread（cppreference）][cppref-jthread]，C++20 的自动 join 线程与停止协议。
- [P0806: Deprecate Implicit Capture of this][p0806]，C++20 废弃 `[=]` 隐式捕获 this 的提案。
- [C++ Core Guidelines CP.24][cp24]与[CP.25][cp25]，把线程当作全局容器看待、优先 gsl::joining_thread 的两条工程建议，与咱们手写的 JoiningThread 是同一个思路（GSL 的 README 注明该类型尚未实现）。

[cppref-ctor]: https://en.cppreference.com/w/cpp/thread/thread/thread
[cppref-dtor]: https://en.cppreference.com/w/cpp/thread/thread/~thread
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[cppref-opassign]: https://en.cppreference.com/w/cpp/thread/thread/operator%3D
[cppref-detach]: https://en.cppreference.com/w/cpp/thread/thread/detach
[cppref-ref]: https://en.cppreference.com/w/cpp/utility/functional/ref
[cppref-shrptr]: https://en.cppreference.com/w/cpp/memory/shared_ptr
[cppref-jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[p0806]: https://www.open-std.org/jtc1/SC22/wg21/docs/papers/2017/p0806r0.html
[cp24]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp24-think-of-a-thread-as-a-global-container
[cp25]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp25-prefer-gsljoining_thread-over-stdthread
