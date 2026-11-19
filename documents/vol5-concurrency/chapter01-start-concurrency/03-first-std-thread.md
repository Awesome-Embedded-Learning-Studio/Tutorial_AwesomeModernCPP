---
title: "第一个 std::thread 程序"
description: "动手写第一个多线程程序：三种方式构造 std::thread，用 join 或 detach 给线程收尾，用实验看清交错输出与结果不确定，查一查机器有几核，认一认线程的标识，弄明白线程函数抛异常为什么直接终止程序。"
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20, 23]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 第一个 `std::thread` 程序

前两篇都在为这一篇做准备。第一篇《什么时候值得引入并发》立了判断，单线程不够用才值得引入并发。上一篇《线程在机器上是什么》下到了系统层，讲了 `std::thread` 经 pthread 落到内核的对应关系，还有调度怎么排。这一篇轮到您自己的程序动手。咱们从最短的一个 `std::thread` 对象开始，把启动、等待、收尾各走一遍，再让几个线程同时往屏幕上写，看看您写单线程代码时从未见过的两种现象：两段输出绞在一起，同一份程序每次跑结果还不一样。

文中的实验全部收在本页的在线示例里，您点开就能直接运行；我们本地实测的配方是 GCC 16.2.1、`g++ -std=c++20 -O2 -pthread`、WSL2（16 逻辑核，2026-11-14），涉及标准库行为的断言也查过 cppreference 并标了版本。输出类实验，您在机器上跑出来的样子每次都会变，这是实验本身要教给您的第一课，耗时类的数字则会随机器漂。

## 三种方式构造线程

`std::thread` 是 C++11 起的标准库类，定义在 `<thread>` 头文件里。它最常用的构造函数接受一个可调用对象，再加一组传给这个对象的参数。您给它什么，它就在一个新的执行流里调用什么。至于新的执行流底下怎么落到内核，上一篇讲过对应关系，本篇只在用到的地方碰一下。

表达“可调用”的方式不止一种，咱们按从朴素到能携带状态的顺序，把三种都跑一遍。

### 最朴素的起点：函数指针

咱们给它一个普通函数，再给一组参数，线程就起来了：

<OnlineCompilerDemo
  title="三种构造方式之一：函数指针"
  source-path="code/examples/vol5/chapter01/10_hello_thread.cpp"
  description="线程函数与参数在构造时一起传入，join 等它结束。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来一行 `hello from thread 42`。`std::thread t(print_hello, 42)` 这一句做了三件事：把函数和参数打包存进线程对象的内部存储，向操作系统要一条新的执行流，新执行流拿存好的参数去调用 `print_hello(42)`。参数是复制进去的，复制发生在发起线程里，发起线程此刻就是执行 `main` 的主线程，复制失败抛的异常也留在它这里。至于复制怎么发生、引用怎么传过去，是下一篇《谁负责线程和数据的生命周期》的主题，这里把“参数被复制”这个事实接住，就够往下走了。

### 日常主力：lambda

实际工程里我写线程，绝大多数时候用的是 lambda，因为要做的事就在调用点，不用专门声明一个函数：

<OnlineCompilerDemo
  title="三种构造方式之二：lambda"
  source-path="code/examples/vol5/chapter01/11_lambda_sum.cpp"
  description="按引用捕获的求和在线程里跑，join 之后主线程才读结果。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来 `sum = 15`。这段代码是安全的，但安全的原因值得您看清：`sum += v` 只有一个线程在写，主线程在 `t.join()` 之后才去读 `sum`。cppreference 在 [`std::thread::join`][cppref-join] 的页面写明（C++11 起），线程的完成与 join 的成功返回之间有同步关系，所以主线程读到的值不会是半成品。假如把 join 去掉，或者把读的位置挪到 join 之前，这段程序就变成后面讲竞争那一篇要解剖的数据竞争。

`[&data, &sum]` 按引用捕获也有另一面：您捕进去的是引用，引用活得比对象久，线程手里就是一条悬垂引用，两条修复路径咱们留在下一篇讲。

### 携带状态：函数对象

咱们给的第三样东西是一个重载了 `operator()` 的对象，任务复杂、要携带自己的状态时用得上：

<OnlineCompilerDemo
  title="三种构造方式之三：函数对象"
  source-path="code/examples/vol5/chapter01/12_functor_thread.cpp"
  description="对象被复制一份进线程，引用成员复制的只是引用本身——悬垂风险留给下一篇。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来 `functor: sum = 15`。传给 `std::thread` 的对象也会被复制一份，这个事实要跟它的引用成员放在一起看：复制进线程的只是 `data_` 这个引用本身，不是 vector。只要 `data` 活得比线程久就没问题，活得不久，线程手里就攥着一条悬垂引用。这类问题的判断和修复也在下一篇。

写函数对象这一路还有个 C++ 的老语法现象等着您。假如您把最后两步合成一句：

<OnlineCompilerDemo
  title="最易错解析：圆括号还是花括号"
  source-path="code/examples/vol5/chapter01/13_vexing_parse.cpp"
  description="std::thread t(Accumulator(data)) 声明的是名叫 t 的函数；错误写法留在注释里，放开后编译器警告 -Wvexing-parse，报错记录见正文。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

这一句在编译器眼里不是“构造一个线程对象”，而是“声明一个名叫 `t` 的函数”。这是所谓“最令人头疼的解析”（most vexing parse）：凡是长得像函数声明的东西，一律按函数声明处理。咱们在 GCC 16.2.1 下编译了这一句，编译器给一个警告，下一行再用 `t` 就报错：

```text
warning: parentheses were disambiguated as a function declaration [-Wvexing-parse]
note:   replace parentheses with braces to declare a variable
error: request for member 'join' in 't',
       which is of non-class type 'std::thread(Accumulator)'
```

编译器的提示已经把出路写出来了，把圆括号换成花括号，写成 `std::thread t{Accumulator(data)}`。另外两条路是构造一个具名对象再传入（咱们上面的写法），或者干脆改用 lambda。新版 GCC 会替您把这行挑出来，但别的编译器未必，习惯上还是自己避开。

三种方式到这里咱们都跑通了。往后本卷的例子以 lambda 为主，函数对象在需要携带较多状态时再出场。

## 启动、等待、结束：`join` 与 `detach`

线程对象构造完成的那一刻，新执行流已经交给操作系统了，接下来它什么时候被调度到、跑多快，都不由您控制。您能控制的是一件事：这个线程收尾时您怎么交代。`std::thread` 给了两条路，`join()` 和 `detach()`（都是 C++11 起），咱们一条一条走。

### join：等它收工

`join()` 是阻塞调用，当前线程停在原地，等目标线程跑完才继续往下走。咱们看一个带时间线的例子：

<OnlineCompilerDemo
  title="join：等它收工"
  source-path="code/examples/vol5/chapter01/14_join_wait.cpp"
  description="发起线程的一方在 join 处等待，工作线程收尾后主线程才继续。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑了三次，输出都是同一个顺序：

```text
main    : launching a thread
main    : waiting
worker  : starting
worker  : done
main    : thread finished, continuing
```

有两层顺序要分开看。`waiting` 出现在 `starting` 前面，说明主线程把线程放出去之后没有停，接着往下打印，worker 那边才被调度到。这两行都发生在构造完成之后，中间没有任何同步点，两者的次序由调度决定，咱们三次跑出来碰巧一样，换个机器或换个负载就可能变。而 `done` 与 `continuing` 的次序是标准保证的：cppreference 在 join 页面写明，线程的完成与 join 的成功返回同步，所以 `continuing` 永远在 `done` 后面。哪些次序是碰巧的、哪些是标准担保的，把两层分开看的习惯，咱们从这一轮时间线实验起就立起来。

### detach：放手不管

`detach()` 走另一条路，它把执行流从线程对象上分离出去，线程继续在后台跑，对象从此不再关联任何线程（cppreference 的说法是分离后 `*this` 不再拥有任何线程，见 [detach 的 cppreference 页面][cppref-detach]，C++11 起）。咱们看这个例子：

<OnlineCompilerDemo
  title="detach：放开线程自己收尾"
  source-path="code/examples/vol5/chapter01/15_detach.cpp"
  description="detach 之后线程自生自灭，主线程先走，进程等全部线程结束才退。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

后台任务要 500 毫秒，主线程只睡 200 毫秒就退出。咱们跑了三次，三次都只看到一行 `main: exiting`，`background task finished` 从来没有出现。原因在进程这一层：主线程从 `main` 返回，整个进程就结束了，还没跑完的线程跟着一起被收掉，那句输出永远不会有机会打出来。这正是 detach 的分量，您交出去之后就没有任何办法再等到它，它什么时候跑完、跑没跑完，程序退了都不知道。

什么时候真用 detach？我的经验是应用代码里很少见。它适合“结果没人等、自己会收拾自己”的任务，但大多数看似这样的任务，比如后台日志，实际都需要程序退出前确认数据写完了，那就得留一条显式的关闭路径，而不是撒手。怎么把收尾做成有协议的停止，第一单元最后一篇《让工作有序结束》专门讲，那里您还会见到 C++20 的 `std::jthread`。

### 两条路都不走：std::terminate

join 和 detach 您可以都不调用，代价标准早就定好了。咱们把前面每个例子里都有的那行收尾拿掉，构造完直接走出 `main`，看看会发生什么：

<OnlineCompilerDemo
  title="既不 join 也不 detach：进程被收走"
  source-path="code/examples/vol5/chapter01/16_no_join_terminate.cpp"
  description="线程对象析构时 std::terminate 直接收走整个进程，本示例预期以崩溃收场。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们编译运行了一次，屏幕上连 `working...` 都没有，标准错误里打出一行 libstdc++（GCC 附带的 C++ 标准库）的报错，进程退出码是 134。这个数字的来历是进程被 SIGABRT 信号终止，128 加上 SIGABRT 的编号 6，正好是 134：

```text
terminate called without an active exception
```

主线程走完 `main` 就析构 `t`，而一个还关联着线程的 `std::thread` 对象析构时，标准的行为是调用 `std::terminate()`（C++11 起），cppreference 有正式记载，见它的[析构页面][cppref-dtor]。咱们这次跑，terminate 赶在了 worker 执行到输出之前，所以连 `working...` 那一行都没出现，换成您的机器，它出现没出现都可能。默认构造的、被移动过的、已经 join 过的、已经 detach 过的对象，析构都是安全的，cppreference 在同一页里列了全部四种情形。

标准为什么这么狠？假如析构隐式地替您 join，对象离开作用域就可能卡住整个线程，析构函数里的意外阻塞比崩溃还难查。假如析构隐式地替您 detach，线程可能在对象销毁后继续访问已经不存在的东西，那连崩溃都算善终。标准干脆不替您选，逼您在每一条路径上自己表态。这个表态包括异常路径：函数中途抛异常，栈沿着调用栈逐层退出并沿途析构局部对象（这个过程叫栈展开）时，同样会析构到没 join 过的线程对象。把 join 的责任交给一个在析构里自动等待的包装器，是解决这件事的成熟做法，下一篇动手写。

## 第一手实验：交错输出、结果不确定、机器有几核

构造、等待、收尾都见过了，现在做本篇真正想让您做的事：亲眼看到乱。三个实验都不难，跑一遍胜过十段解释。

### 四个线程一起往屏幕上写

咱们来最直觉的版本，四个线程各打四行日志，主线程等它们全部收工：

<OnlineCompilerDemo
  title="四条线程各走各的进度"
  source-path="code/examples/vol5/chapter01/17_interleaved_workers.cpp"
  description="四条线程各走四步、每步 30 毫秒，执行流彼此独立推进，行的交错每次不同。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑了三次。三次都乱了，乱法还不一样，这是第一次的输出：

```text
worker 1 step 0
worker 2 step 0
worker 3 step 0
worker 4 step 0
worker 2 step 1
worker 3 step 1
worker 4 step 1
worker 1 step 1
worker worker 4 step 2
2 step 2
worker 3 step 2
worker 1 step 2
worker worker 42worker  step 3 step 3 step 33


worker 1 step 3
```

头四行还挺整齐，每行也完整。往中间看，`worker worker 4 step 2` 和 `2 step 2` 这两行是 worker 2 和 worker 4 绞在了一起：worker 2 刚写出行首的 `worker` 和它后面的空格，worker 4 的整行连着自己的换行插了进来，随后 worker 2 接着写完剩下的 `2 step 2`。底下那行碎得更狠，三个线程熔进了同一个物理行，行尾的三个换行连排在融合行末尾，您看到的两个空行就是这么来的。另两次跑出来是另一种乱法，行与行完整，但顺序不同，worker 1 不一定还是头一个打完。您数一下行数，十六行一条都不少，内容都对，就是顺序和形状全变了。

还有一个观察要如实交给您。咱们一开始跑的是不带 `sleep_for` 的快版本，同样的四个线程，在咱们自己的 16 核机器上六次里有五次输出一模一样，看上去“挺稳定”。那不是线程们懂事，是每个任务太短，第一个线程跑完了第二个才被调度进来。任务越短、机器越快，乱越藏得深，但“藏起来”不等于“不存在”，快版本那次不一样的输出就是它在探头。实验里加 30 毫秒的睡眠，就是把每个任务拉长，让重叠必然发生，您在自己机器上复现时也照这个手法写。

### 每次运行都不一样

乱的根源值得咱们说清。`std::cout` 是全程序共享的一个对象，四个线程都在往它身上写。一条输出语句里连着几个 `<<`，标准并不保证它们拼成一个不可分割的整体，谁写完一小段，谁就可能被别人插进来一截。所以您看到的现象，行序会变，行内会碎，内容一条都没有少，都是同一件事的表现：多个执行流写同一个对象，中间没有同步，写出来的字节流就是交错的。

咱们又换了个写法验证“插进来”这件事本身。让三个线程各自睡满 50 毫秒后同时开写，每个片段不带换行，全部写完才打一个换行：

<OnlineCompilerDemo
  title="交错输出：结果不确定"
  source-path="code/examples/vol5/chapter01/18_chatter_interleave.cpp"
  description="三条线程同时醒来各自连打六个标记，输出顺序每次都不同——这就是并发程序的非确定。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

按您单线程的直觉，这该是三行整齐的片段。咱们跑了上千轮，乱起来的样子像下面这样，这是其中一次：

```text
[w[w1:0][w1:1][w1:2][w1:3][w1:4][w1:5]
2:0][w2:1][w2:2][w2:3][w2:4][w2:5]
[w3:0][w3:1][w3:2][w3:3][w3:4][w3:5]
```

专门数过一遍：四百轮里八十五轮是乱的，剩下那些轮次都是三行整齐的。咱们按行看这次捕获的样本。worker 2 的第一个片段才写出 `[w` 两个字符，worker 1 的整行连着换行插了进来，随后 worker 2 从 `2:0]` 接着写完了自己剩下的片段。三个换行倒是都待在自己内容的末尾，穿插进来的是整行内容。什么时候乱、乱成什么样，每次跑都可能不同，这就是第一篇开头那句话的现场版：并发程序里“这个变量现在是什么值”需要证明，`std::cout` 的输出只是它最容易亲眼看到的样子。

把输出修回人能读的样子，办法是把一段完整输出用锁包起来，一次只放一个线程进去写。锁（`std::mutex` 这类工具）是后面讲互斥那一篇的主题，这里知道症结在哪就够。您以后写多线程日志时看到的每一行乱码，根源都是咱们今天做过的实验。

### 机器有几核

乱见过了，咱们顺手把机器的家底查一下。`std::thread::hardware_concurrency()` 是个静态成员函数（C++11 起），返回实现支持的并发线程数：

<OnlineCompilerDemo
  title="机器有几核"
  source-path="code/examples/vol5/chapter01/01_hardware_concurrency.cpp"
  description="hardware_concurrency 返回实现支持的并发线程数，只是提示，算不出来返回 0。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来是 16，跟命令行的 `nproc` 一致，数的就是咱们本机逻辑核。cppreference 在 [hardware_concurrency 的页面][cppref-hw] 写着，这个值只当提示用，算不出来时返回 0。它最常见的一个用途是给第一篇里把求和拆给多核的那类并行决定开多少个线程，用之前值得检查一下是不是 0，也别把它当精确承诺，容器（Docker 这类限制核数的运行环境）里的程序拿到什么值，要看环境分到几个核。

## 线程是谁：标识与查询（扩展）

本节带（扩展）标记，跳过它不影响本篇主线。不过它很短，而且您调多线程的 bug 时大概率会用上，建议读完。

每个线程有一个自己的标识，类型是 `std::thread::id`（C++11 起）。从线程对象身上拿叫 `get_id()`，线程在自己内部拿自己的，叫 `std::this_thread::get_id()`。咱们把两条路都走了一遍：

<OnlineCompilerDemo
  title="线程标识：get_id 的报告与归零"
  source-path="code/examples/vol5/chapter01/19_thread_id.cpp"
  description="主线程与工作线程的 id 各自可见，join 之后线程对象给出的是非执行线程的 id。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们这次跑出来：

```text
from main   : worker id is 132837468534464
from main   : main id is 132837476218752
inside worker: my id is 132837468534464
after join  : get_id gives thread::id of a non-executing thread
```

第一行和第三行对上了：主线程手里 `t.get_id()` 拿到的编号，跟 worker 内部自报的编号是同一个，线程只有一条身份。第二行是主线程自己的编号，它没有对照对象，放在这儿只是给您看个参照。第四行，join 之后再问 `t.get_id()`，对象已经不关联任何线程，返回的是不指向任何执行的默认值，咱们机器上的 libstdc++ 把它打印成一句话。

> 这个类型还有两条用法上的事要交代。id 的具体样式是各实现自己定的，咱们这里是数字，别的编译器可能是十六进制地址，所以拿它打日志可以，拿它做逻辑判断不行。它支持比较，也能当哈希表的键，给每条线程配一份独立资源时用得上。

标准库还留了一个通向底层的口子：`native_handle()`（C++11 起）。它返回实现定义的原生线程句柄，Linux 上就是 pthread 句柄，拿它可以做标准库没覆盖的事，比如设优先级、绑 CPU 亲和性。这些平台相关的话题归上一篇讲 pthread 的部分，本篇不走进去，您知道有这么个口子就行。

## 线程函数里抛异常会怎样

本篇还有一条必须遵守的规则：异常不能逃出线程函数。咱们直接跑一个违反它的程序：

<OnlineCompilerDemo
  title="异常逃出线程函数：接不住"
  source-path="code/examples/vol5/chapter01/20_exception_escapes.cpp"
  description="main 的 try 接不住工作线程抛出的异常，std::terminate 收走整个进程，本示例预期崩溃。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

主线程的 `try` 在这儿，`catch` 也在这儿，可咱们跑出来的是退出码 134，标准错误里两行：

```text
terminate called after throwing an instance of 'std::runtime_error'
  what():  oops, something went wrong
```

`catch` 一行都没执行。每个线程有自己独立的调用栈，异常处理只在当前线程的栈里展开，栈展开就是前面说的那个逐层退出、沿途析构的过程，worker 抛出的异常在 worker 的栈里找不到任何能接住它的 `catch`，就触发 `std::terminate()`。主线程的 `try` 跟它隔着一整条线程，够不着。咱们把同样的程序改成在线程内部把异常处理掉，就能正常收场：

<OnlineCompilerDemo
  title="异常在线程内接住"
  source-path="code/examples/vol5/chapter01/21_exception_caught.cpp"
  description="try/catch 写在线程函数里，失败就地消化，main 正常继续。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来两行都在，退出码 0，程序活着走完了。线程内部接住异常、至少接住一个 `catch (...)` 兜底，是手写 `std::thread` 时的基本防御。要是您想把线程里的失败如实递回主线程处理，标准库有正路，`std::exception_ptr` 可以把异常打包带走，把递回失败修成现成设施的，是 `std::async` 和 `std::promise`/`std::future`，后面讲任务组织的那几篇专门讲怎么传。

## 下一步

今天这些程序咱们全程守着手动档：起线程靠构造，收尾靠 join 或 detach，异常路径上漏一次 join 就是 terminate，屏幕乱了也只能等后面讲互斥那一篇的锁来管。下一篇《谁负责线程和数据的生命周期》把这些手动档换成有设计的写法，参数怎么传，引用什么时候悬垂，还有怎么用 RAII 把 join 的责任从您的记性里接过去，那个包装器就是前面说的在析构里自动等待的东西，到时候您亲手写一遍。您手里现在有了第一手的现象，今天看到的这些乱序和终止，后面讲竞争讲同步时都会回来。

## 参考资源

本篇写作时对照借鉴了 Anthony Williams 的《C++ Concurrency in Action》，借的是章节推进思路。例子与文字都是我自己写的。涉及 C++ 行为的断言，均按文中标注编译实测或查证 cppreference，文中涉及的线程设施均自 C++11 起可用。

- [std::thread（cppreference）][cppref-thread]，成员一览，含 `get_id` 与 `native_handle`。
- [std::thread::thread（cppreference）][cppref-ctor]，构造函数语义，参数的复制发生在发起线程。
- [std::thread::join（cppreference）][cppref-join]，阻塞语义与同步保证。
- [std::thread::detach（cppreference）][cppref-detach]，分离语义。
- [std::thread::~thread（cppreference）][cppref-dtor]，joinable 状态下析构调用 `std::terminate` 的规定。
- [std::thread::hardware_concurrency（cppreference）][cppref-hw]，提示值语义与返回 0 的情形。
- [std::thread::native_handle（cppreference）][cppref-native]，实现定义的原生句柄。

[cppref-thread]: https://en.cppreference.com/w/cpp/thread/thread
[cppref-ctor]: https://en.cppreference.com/w/cpp/thread/thread/thread
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[cppref-detach]: https://en.cppreference.com/w/cpp/thread/thread/detach
[cppref-dtor]: https://en.cppreference.com/w/cpp/thread/thread/~thread
[cppref-hw]: https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency
[cppref-native]: https://en.cppreference.com/w/cpp/thread/thread/native_handle
