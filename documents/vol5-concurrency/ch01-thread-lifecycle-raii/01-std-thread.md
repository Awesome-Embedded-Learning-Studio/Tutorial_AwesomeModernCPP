---
title: "std::thread 基础"
chapter: 1
order: 1
description: "亲手开起全卷第一条执行流：构造的三种入口、join 与 detach 的分岔、joinable 的四种空壳、get_id 与 hardware_concurrency，文末拼出 parallel_for_each 骨架"
tags:
  - host
  - cpp-modern
  - beginner
  - 异步编程
difficulty: beginner
platform: host
cpp_standard: [11]
reading_time_minutes: 18
prerequisites:
  - "OS 线程与开销"
related:
  - "线程参数与生命周期陷阱"
  - "线程所有权与 jthread/stop_token"
---

# std::thread 基础

[上一篇](../ch00-concurrency-fundamentals/03-os-threads-and-cost.md)咱们把 `std::thread` 当把手用了两回：开篇的示意算一回，掐表基准里真用的算一回。创建的价钱量过了，欠下的债都记在了本篇头上。今天咱们就来亲手开一条线程，再把它体面地送走。

工具还是 ch00 用过的那套：本机的 g++（GCC 16.2.1，跑在 20 核的 WSL2 里），编译的时候带上 `-Wall -Wextra -pedantic -pthread`。全卷的代码都在这套环境里编译验证过，您跟着敲就行。

本篇从最少的写法讲起，一路讲到一个压轴的 `parallel_for_each`：数据分了块、派生一批线程、再逐个 join。往后它还要被咱们翻修一遍，咱们把它搭结实点。

## 开一条线程：最少的写法

咱们从最朴素的形态开始：一个普通的函数，就是线程的入口。

```cpp
#include <iostream>
#include <thread>

void print_hello(int id)
{
    std::cout << "Hello from thread " << id << "\n";
}

int main()
{
    std::thread t(print_hello, 42);
    t.join();
    return 0;
}
```

两行的新东西，咱们一行一行看。`std::thread t(print_hello, 42)` 构造了一条线程：头一个参数是它的入口，后面的参数会原样传给它。`t.join()` 等它把活儿干完了才返回。咱们编译运行，屏幕上会多出一行来自新线程的问候，那就是另一条执行流在跟咱们打招呼。

构造函数在幕后做的事，咱们挑要紧的说。它做的头一件事，是把函数和实参各拷了一份、存进对象里。跟着它去求底层的线程创建，等内核把线程派了出来，新线程就在自己的栈上、用拷来的副本把函数调用起来。

第二件事咱们机器上有实证可查。libstdc++ 的 `std::thread` 启动路径最终落在 glibc 的 `pthread_create` 上，符号表里的记录看得清清楚楚：

```text
$ nm -D /usr/lib64/libstdc++.so.6 | grep pthread_create
                 U pthread_create@GLIBC_2.34
```

咱们写的每一条 `std::thread`，在 Linux 加 libstdc++ 的组合上，最终都是 `pthread_create` 生出来的。这句限定咱们要挂牢：MSVC 走的是另一套线程 API，平台断言不写成全平台的。[上一篇](../ch00-concurrency-fundamentals/03-os-threads-and-cost.md)里 clone 与 EAGAIN 的故事，接的正是这里。

构造的失败也有形可循：资源不够的时候，构造函数抛出的就是 `std::system_error`，错误条件里可能就有您认识的 `resource_unavailable_try_again`，正是 EAGAIN 的错码名。[上一篇](../ch00-concurrency-fundamentals/03-os-threads-and-cost.md)里咱们看过的线程数上限，顶到头上的时候就是在这一行遇上的。

还有一层保证值得咱们现在就记下：构造函数的返回在前，新线程的开跑在后，所以咱们在构造以前写好的数据，线程函数一睁眼就看见了。咱们在 [data race 那一篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)里说过“线程创建也是 happens-before 的来源”，说的就是这件事。正式的定义住在 ch03，眼下咱们只用口语版。

### lambda：把活儿写在调用点

入口的三种形态里，工程上最常用的是 lambda：咱们把活儿写在调用点，读代码的人不用满文件找函数：

```cpp
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    std::vector<int> data = {1, 2, 3, 4, 5};
    long sum = 0;

    std::thread t([&data, &sum] {
        for (int v : data) {
            sum += v;
        }
    });

    t.join();
    std::cout << "sum = " << sum << "\n";
    return 0;
}
```

上面的例子是能跑的，但您闻到味道了吗：lambda 按引用捕获了 `data` 和 `sum`，用它们的却是另一条执行流。眼下 main 会老老实实地 join，两边的寿命对得上，所以相安无事。可只要线程的寿命比它们长，引用就悬在了半空。这个味道咱们记下，[下一篇](./02-thread-arguments-and-lifetime.md)整篇处理的都是它。

### 函数对象：带状态的入口

第三种形态是重载了 `operator()` 的类，咱们叫它函数对象，它的长处是能揣着状态进门：

```cpp
class Accumulator {
public:
    Accumulator(const std::vector<int>& data, int& result)
        : data_(data), result_(result) {}

    void operator()() const
    {
        int local_sum = 0;
        for (int v : data_) {
            local_sum += v;
        }
        result_ = local_sum;
    }

private:
    const std::vector<int>& data_;
    int& result_;
};

int main()
{
    std::vector<int> data(10000, 1);
    int result = 0;

    Accumulator acc(data, result);
    std::thread t(acc);

    t.join();
    std::cout << "result = " << result << "\n";
    return 0;
}
```

咱们本地编译运行，输出的结果是 `result = 10000`。这里有个细节值得您多看一眼：递给线程的是 acc，线程里跑的却是 acc 的拷贝。函数对象的待遇跟实参一样：会被拷了一份、带进线程。细节咱们[下一篇](./02-thread-arguments-and-lifetime.md)讲全，本篇您只要认下这个事实。还请把这个能跑的版本记牢，后文有个长得几乎一样的亲戚，它的下场完全不同。

## join 与 detach：收场的分岔

线程开了出去，对象 t 的身后就牵着一条执行流，它迟早要有个了结。了结的路只有 join 与 detach 两条，逼着您亲口选一条。

### join：等它跑完

```cpp
#include <chrono>
#include <iostream>
#include <thread>

void slow_work()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

int main()
{
    std::thread t(slow_work);

    std::cout << "main: waiting\n";
    t.join();
    std::cout << "main: joined, worker is done\n";
    return 0;
}
```

join 的语义一句话：谁调用了它、谁就被挂起，直到 `t` 身后的线程把代码执行完毕。它还给了一层顺序保证：线程的活儿全干完在前，join 的返回在后，所以 join 之后，子线程写下的任何结果，咱们放心理用。咱们在 [data race 那一篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)里说过，join 就是 happens-before 的来源之一，落的就是这一层。

<!-- 实验回填：本例运行输出三行的先后顺序 -->

还有一个容易被忽略的事实，值得您记下：线程的代码跑完了、还没被 join，它仍然算是活动的线程，它的 join 或 detach 也还没有做。要判断的正是这一层，靠的就是 `joinable()`，咱们马上说它。

### detach：放它单飞

```cpp
#include <chrono>
#include <iostream>
#include <thread>

void background_cleanup()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "cleanup: done\n";
}

int main()
{
    std::thread t(background_cleanup);
    t.detach();

    std::cout << "main: detached, leaving now\n";
    return 0;
}
```

detach 的语义也一句话：把线程从对象的手里放走，咱们从此撒手不管。放走之后对象成了空壳，join 却再也够不着它了，线程接下来的路得自己走。本例里 main 转身就返回了，进程多半等不到 `cleanup` 的那行输出。从 main 返回触发的是与 `std::exit` 同一套收尾：静态存储期的对象析构、atexit 的处理器跑完，然后进程也就终止了。进程终止的时候，还在跑的后台线程直接消亡，它们的局部对象不会析构。

<!-- 实验回填：本例运行输出（时序敏感，多跑几次看差异） -->

detach 最要命的地方在于它手里攥着的引用：等主线程的局部变量没了，而它还在后面用。这个现场长什么样，[下一篇](./02-thread-arguments-and-lifetime.md)会整篇演给您看，本篇咱们只把事实立住：放出去的线程，生死您就管不着了。Core Guidelines 的态度也直白，CP.26 的标题就叫 Don't `detach()` a thread。

### 什么都不做，会怎样

除了上面的两条路，咱们还有第三种选法：什么都不做，join 与 detach 咱们一个都不给，任由 `t` 走完它的一生。

```cpp
#include <thread>

void some_work() {}

int main()
{
    std::thread t(some_work);
    // 既没有 join()，也没有 detach()
    return 0;
}
```

下场的名字叫 `std::terminate`，程序当场就死了。咱们本地跑一遍：输出的是一行 `terminate called without an active exception`，退出码落在了 134，也就是咱们熟悉的 SIGABRT。

<!-- 实验回填：terminate 输出原文与退出码 -->

判据咱们说准确：析构的时候，对象若仍然处于 joinable 的状态，terminate 就跟着落地了。这是 `std::thread` 一以贯之的设计哲学——不做隐式的、可能令人惊讶的事情。join 还是 detach 的选择，标准不给默认的答案，含糊是不行的。至于标准为什么偏偏挑了当场崩溃这一手，咱们到 [jthread 那一篇](./03-thread-ownership-and-jthread.md)再回头说它。

### joinable：对象的实与空

`joinable()` 回答的问题只有一个：这个对象的身后现在有没有牵着线程。四种空壳的状态，咱们拿一张小表记全：

| 状态 | `joinable()` |
|------|--------------|
| 默认构造的 `std::thread` | false |
| 被 move 走之后 | false |
| `join()` 之后 | false |
| `detach()` 之后 | false |

move 那一行咱们只登记不深谈，所有权是 [jthread 那一篇](./03-thread-ownership-and-jthread.md)的主菜。眼下您把判据记牢就够了：terminate 看的是它，往后写收尾代码的时候，问的也是它。

## get_id 与 this_thread：认出每条线程

每条线程都有自己的身份证 `std::thread::id`。咱们在对象这边调用 `t.get_id()`，拿到的是它身后的线程 id。线程要看自己的，用的就是 `std::this_thread::get_id()`：

```cpp
#include <iostream>
#include <thread>

void worker()
{
    std::cout << "worker 自己的 id: "
              << std::this_thread::get_id() << "\n";
}

int main()
{
    std::thread t(worker);
    std::cout << "main 的 id: " << std::this_thread::get_id() << "\n";
    std::cout << "t 的 id:     " << t.get_id() << "\n";

    t.join();
    std::cout << "join 之后 t 的 id: " << t.get_id() << "\n";
    return 0;
}
```

咱们本机的 libstdc++ 把 id 打印成一串纯数字（pthread 底层值的十进制），join 之后那一行打出来的却是字面文本 `thread::id of a non-executing thread`。join 完成的那一瞬间，t 成了空壳，这个 id 也就失效了。

<!-- 实验回填：本例实际输出四行（数字部分每次运行都不同） -->

打印成什么样，标准里没有格式的规定，您别拿格式当逻辑依据，MSVC 那边打印的就是十六进制的模样。标准保证的是另一些东西：id 的拷贝、相等比较、全序关系，标准都给了保证，还专门给 `std::hash` 配了特化，明说它的设计用途就是当关联容器的键。所以咱们拿 `unordered_map<std::thread::id, T>` 给每条线程记档案，写起来也就名正言顺了。还有个小知识：线程跑完了之后，它的 id 值可能被后来新建的线程复用，您做日志排查的时候心里记下这件事，也就够了。

> 咱们顺带登记一个话题：`native_handle()` 给您平台的原生句柄，在 Linux 上拿到的是 `pthread_t`。设调度策略、绑核、起线程名的活儿，都得从它的手里走。不过这些活多数要特权，比如您去调 `SCHED_RR` 那一套，普通用户一调就是 EPERM 的下场。这个话题咱们本篇不展开，您知道有它就行。

## 机器里有多少个核：hardware_concurrency

咱们想分块并行的时候，头一个要回答的问题就是开几条线程。`std::thread` 给了一个静态成员：

```cpp
#include <iostream>
#include <thread>

int main()
{
    std::cout << std::thread::hardware_concurrency() << "\n";
    return 0;
}
```

本机打印的是 20，跟咱们 20 核的机器对得上。它的标准口径，咱们按事实摆开。咱们看签名：它是 `static unsigned int hardware_concurrency()`，返回的是 unsigned int，而不是 `size_t`。它返回的是“实现支持的并发线程数”，而且咱们只能把它当成提示，标准的原话是 The value should be considered only a hint，您不能把它当真，实现在超线程、资源配额这些事上各有各的算法。而最要紧的事实是：值查不出来或算不出来的时候，它返回的就是 0。这个 0 说的不是核数，它是不知道的意思，怎么兜底是调用者的责任。文末的骨架正好踩到这件事，咱们到时候把话说清。

它最常见的用法就是定并行度：线程池要开几条工人、数据要切几块，咱们都从它问起。问出来的数您也别直接照单全收，正式的工程里总得自己再校一遍。

> 好奇它在咱们的机器上怎么实现的话，咱们可以扒一眼 libstdc++：它调的是 glibc 的 `get_nprocs()`，反汇编里只有四条指令：调用的结果放进 `eax`，把 `edx` 清成了零，`test` 查 `eax` 的符号位，负的话就用一条 `cmovs` 把 `eax` 换成 0。标准说的“查不出来返回 0”，在实现层确实有一条兜底的指令。另外 `get_nprocs` 数的是调度亲和掩码里的 CPU：容器把 cpuset 限住了它会跟着变，cfs 的配额它就不认了。版本号这类事以您机器上的 glibc 为准，咱们记机制就好。

## 长得像函数声明的线程：最费解解析

前文函数对象一节的 Accumulator 带着两个参数，编译和运行都顺利地过了关。还记得咱们预告过的那个亲戚吗？现在咱们把构造那一行改一下：括号里什么都不给。

```cpp
std::thread t(Accumulator());   // 您以为在定义线程，其实在声明函数
t.join();
```

GCC 16.2.1 的原话，咱们一字不改地抄给您：

```text
mvp_true.cpp:26:18: warning: parentheses were disambiguated as a function declaration [-Wvexing-parse]
mvp_true.cpp:27:7: error: request for member 'join' in 't', which is of non-class type 'std::thread(Accumulator (*)())'
```

warning 附带的 note 也值得您看：编译器教您把括号换成花括号，完整的原文收在仓库代码里。

出了什么事？咱们得搬出 C++ 语法里的一条规则：一句话既能解析成对象定义、又能解析成函数声明的时候，编译器挑的就是函数声明。而 `Accumulator()` 恰好读得成一个合法的形参，它的读法是“返回 `Accumulator` 的无参函数”。而这样的形参会被调整成函数的指针。于是整句话就成了一个函数声明：t 收的就是这样一个函数指针，返回的也是一个 `std::thread`。t 成了函数声明，自然就不是对象了，`join()` 自然也就无处可调了。有意思的是，报错里压根不提默认构造的事：解析这步排在重载决议的前面，编译器根本没走到构造 Accumulator 的那一步。

这里咱们得替带参数的版本澄清一下，因为网上有不少资料把它们混成了一锅。网上把它们当成反例的 `std::thread t(Accumulator(data, result));`，其实是合法的。能翻车的那句话，括号里必须恰好拼得出一个合法的形参声明：空括号的 `Accumulator()` 拼得出形参，读作无参的函数指针，而 `Accumulator(data, result)` 的括号里装着两个名字，怎么读都拼不成一个合法的形参，编译器就只好老老实实地把它当对象定义。前文函数对象一节咱们本地编译运行过它，输出的 `result = 10000` 就是它编译运行都过了的实证。

修法是现成的，GCC 的 note 也是同一个意思：咱们初始化对象的时候统一用花括号。

```cpp
std::thread t{Accumulator(data, result)};   // 花括号初始化，读不出函数声明
```

花括号初始化走的是另一套语法，从头到尾的每一步都读不出函数声明。还有一层值得您看：要是您写 `std::thread t{Accumulator()};`，歧义确实没了，编译器却会诚实地告诉您 `no matching function for call to 'Accumulator::Accumulator()'`。咱们这个类本来就没有默认构造，空括号那版就算解析对了也造不出对象。这也正是花括号的价值：出了错，错在它真正该错的地方。

## 综合运用：parallel_for_each

学过的用法都齐了，咱们把它们拼成一个能用的 `parallel_for_each`：把一段数据切成了块、派生一批线程、每条线程啃一块，最后的一块留给调用方自己，也省下了一条线程的开销。

咱们动手之前有个头一号的问题：thread 对象拿什么存？`std::thread` 没法拷贝，移动倒是行，而 `vector` 从 C++11 起恰好接纳 move-only 的元素。`emplace_back` 干脆在容器里就地构造，连一次拷贝的功夫都没有。这件事的合法性深处是所有权语义，咱们到 [jthread 那一篇](./03-thread-ownership-and-jthread.md)再挖，本篇咱们直接用起来。

```cpp
#include <algorithm>
#include <cstddef>
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
        thread_count = std::thread::hardware_concurrency();
    }

    const std::size_t block_size = length / thread_count;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    Iterator block_start = first;
    for (unsigned i = 0; i < thread_count - 1; ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        threads.emplace_back([block_start, block_end, &func] {
            std::for_each(block_start, block_end, func);
        });
        block_start = block_end;
    }

    std::for_each(block_start, last, func);  // 最后一块，调用方自己啃

    for (std::thread& t : threads) {
        t.join();                            // 一串手动 join，一个都不能少
    }
}
```

咱们走读一遍。length 是 0 的话就直接返回，空数据就不折腾了。thread_count 传 0 的意思是您看着办，骨架转而去问 `hardware_concurrency()` 的意见。`reserve` 会把容量一次给足了，省去中途扩容的搬移。循环的次数是 `thread_count - 1`，最后的一块由调用方自己算，省下一条线程的创建开销，调用方也跟第一批的结果贴得更近。末尾的一串手动 join，一个都少不了。

咱们接着看它的用法：把 1000 个数交给它翻倍，再跟串行的版本对拍。

```cpp
#include <iostream>

int main()
{
    std::vector<int> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int>(i);
    }

    parallel_for_each(data.begin(), data.end(),
                      [](int& v) { v *= 2; },
                      0);

    long sum = 0;
    for (int v : data) {
        sum += v;
    }
    std::cout << "sum = " << sum << "\n";  // 期望 999000
    return 0;
}
```

碰每个元素的线程自始至终是同一条，块与块的边界是分开的，天然就没有了 race。join 又把顺序隔开了，所以咱们回到 main 里再读 `data`，就理直气壮了。跟串行的 `std::for_each` 各跑几遍对拍，结果分毫不差，咱们才算过关。

<!-- 实验回填：本例运行输出（sum 的值）与串行对拍结果 -->

### 出了异常怎么办：两条线都通向 terminate

这套骨架要是出了异常，事情会怎么走？咱们把两条线分开看。两条路最后到的都是同一个 terminate，走的是完全不同的路径，偏偏老被人混成一件事。

咱们看头一条线：某条线程的函数里抛了异常。异常是跑不过线程边界的，主线程的 try-catch 接不到它。它在子线程自己的栈上逃逸，接不住它的正是子线程自己，于是 `std::terminate` 就在子线程里落了地，整个进程当场就死了，死因记在子线程的名下，主线程连栈展开的影子都没见着。另一条线在主线程自己身上：它抛了异常（比如哪一步分配失败）。栈展开会替咱们把 `threads` 析构掉，元素们还处于 joinable 的状态，terminate 就在主线程这边落了地。

所以咱们得把两桩事分开记。“析构时仍 joinable 就 terminate”看的是对象的状态，跟子线程里发生了什么无关。“异常从线程函数里逃逸”看的是子线程的栈，主线程怎么收尾影响不到它。想把子线程的异常体面地接回来，得靠 ch05 的 future 那一套，那是往后的故事，本篇咱们只把事实立住。

### 骨架没兜的一处隐患

咱们把话说明：`hardware_concurrency()` 查不出来的时候返回 0，咱们这套骨架没有兜它。它真返回 0 的话，`length / thread_count` 就成了整数除以 0，直接落进未定义行为的范围，就算侥幸地没炸，`thread_count - 1` 也会在无符号的数上回绕出一个天文数字，循环也就当场失控了。这个隐患咱们记下了，[jthread 那一篇](./03-thread-ownership-and-jthread.md)拿 jthread 重写骨架的时候，兜底会跟自动的 join 一起来，您正好对照着看两版的差异。眼下您要在正式的工程里用它，请您自己把 0 兜成 1。

### 手动 join 的脆弱，与 RAII 的接手

在正常的路径上，记得在函数的末尾调 join 并不难，难的是异常路径和好几个返回分支。中间的某一步一抛异常，写在末尾的 join 就成了永远执行不到的代码，terminate 就接管了一切。修法的方向是现成的：把 join 挪进析构函数，让作用域替咱们盯着这件事。这是 RAII 的活。

咱们往下的路线也这么定：[下一篇](./02-thread-arguments-and-lifetime.md)把参数与生命周期的事实攒齐，detach 翻车的现场就在那边。再往后的那一篇，咱们就用 RAII 把 join 自动化。Core Guidelines 给的意象很准，CP.23 的说法是，把一条会 join 的 thread 想成一个 scoped container，也就是作用域开始时启动、结束时收尾的容器。

## 练习

### 练习 1：id 的前后

把 get_id 一节的例子跑起来，在 join 的前后各打印一次 `t.get_id()`。解释两个问题：为什么 join 之后打出来的是 `thread::id of a non-executing thread`？为什么同一段代码每次运行，数字部分都不一样？做完请您再想一层：两张 id 相等意味着什么，标准又是拿什么保证能比的？

### 练习 2：构造形态判断

三种写法摆在您的面前：`std::thread t(Accumulator(data, result));`、`std::thread t(Accumulator());`、`std::thread t{Accumulator()};`。哪个编译不过、各自卡在了哪一步？请把编不过的报错原文亲手敲出来，跟本篇抄录的对一对。有一行的报错说的不是解析的事，您找到它，解析在前、重载决议在后这件事也就看透了。

### 练习 3：给骨架埋雷

把骨架里的 join 循环整个删掉，程序会怎样、什么时候发生？再把一半的线程留下不 join，又会怎么样？您把 terminate 发生的时机说清楚了，这一关就算过了。提示：vector 析构的时候，会挨个问元素的 `joinable()`。

## 下一步

本篇咱们把 `std::thread` 的骨架立了起来：怎么开、怎么等、怎么放、什么都不做的下场，还有 get_id 与核数的问法，外加一个手动 join 的 `parallel_for_each`。留白也记了两笔：函数对象与实参是怎么被拷进线程的（decay-copy 的细节），detach 攥着引用跑远了会翻成什么车。咱们[下一篇](./02-thread-arguments-and-lifetime.md)就把这两件事讲全：参数按值拷贝的机制、引用要 `std::ref`、move-only 要 `std::move`，还有 ASan 怎么把悬垂引用抓个现行。回来的时候，咱们就用 RAII 把 join 自动化，[jthread](./03-thread-ownership-and-jthread.md) 的登场也就安排上了。

想看这一章在全卷的位置，您回 [卷地图](../) 瞄一眼就行。动手的路线在 [练习体系](../exercises/)：Lab 00 的线程生命周期练习，正好让您把本篇的手动 join 用上。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`。

## 参考资源

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread) —— 类总览与成员索引
- [std::thread::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread/thread) —— decay-copy 构造语义与构造失败的 `std::system_error`
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join) —— 跑完在前、join 返回在后的同步保证
- [std::thread::joinable — cppreference](https://en.cppreference.com/w/cpp/thread/thread/joinable) —— 跑完没 join 仍算活动线程的判据原文
- [std::thread::id — cppreference](https://en.cppreference.com/w/cpp/thread/thread/id) —— 关联容器键的设计用途与 `std::hash` 特化
- [std::thread::hardware_concurrency — cppreference](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency) —— hint 语义与查不出来返回 0
- [Core Guidelines CP.23](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp23-think-of-a-joining-thread-as-a-scoped-container) —— 把会 join 的线程当 scoped container
- [Core Guidelines CP.26](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp26-dont-detach-a-thread) —— 别 detach 线程
- [Williams, *C++ Concurrency in Action*, 2nd ed, Manning, 2019](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition) —— §2.1 线程的基本管理


