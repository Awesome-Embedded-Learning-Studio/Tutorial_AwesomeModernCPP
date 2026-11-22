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
---

# 第一个 `std::thread` 程序

喂喂喂！怎么回事啊！一下子冲进来写代码了！是的，笔者之前也强调了程序设计要的就是你动手。哈哈！之前我相信你大致懵懵懂懂的听说了并发编程设计的一些要点。You got it。

## 先让两条线程各做一件事

首先，请让我们请出来`std::println`（C++23），咱们上一个卷已经介绍过的东西，大概。来打印点东西。再使用`std::thread`（哦吼这就是我们的主角了）启动一条工作线程干活，干的就是你`finish_download`

> 当然如果您使用的库比较老（我说的是大致Release时间是2026年1月1日前的！），可能需要开启实验特性，当然如果你是新的gcc，您当然完全不需要！

好了。猜猜哪行会最后出现。

```cpp
#include <print>
#include <thread>

void finish_download() {
    std::println("Download finished"); // Wow
}

int main() {
    std::thread worker(finish_download);
    std::println("List scrolled");
    worker.join();
    std::println("Showing the completion message");
}
```

<OnlineCompilerDemo
  title="启动一条工作线程，再等它结束"
  source-path="code/examples/vol5/chapter01/00_first_thread.cpp"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

好了多点两下！我知道你肯定只点击了一下的！

感谢AI，我要求 LLM 自己拉可执行文件跑了250次（实在是懒得写脚本和统计）。他兴冲冲的向我汇报

> 咱们用 GCC 16.2.1 按 `-O2 -std=c++23 -pthread` 编译，连跑了 250 次。其中 249 次是下面这个顺序，还有一次前两行换了位置。

一次典型的输出如下：

```text
List scrolled
Download finished
Showing the completion message
```

> 对程序感到困惑嘛？没关系！咱们还是沿着 `main` 走。第一句交给工作线程一件事，让它去执行 `finish_download` 里的代码。主线程可以接着往下打印 `List scrolled`。到 `worker.join()`（C++11 起）这一句，要是工作线程还在干活，主线程就在这里等它结束。等完再往下走，才能打印完成提示。

最后那条消息为什么能留在末尾？您回头找找，是哪一行让主线程等工作线程结束了再往下走。就是 `join()`。前面咱们在纸上画了根箭头，现在您把这个先后关系写进程序里了。 [C++ 标准草案的线程等待条款](https://eel.is/c++draft/thread.thread.member)

接下来的实验也可以直接在线运行。原实验在 GCC 16.2.1、WSL2（16 逻辑核，2026-11-14）下实测过。涉及标准库的行为查过 cppreference，也标了版本。不同运行里的输出顺序可能变化，耗时则会随机器漂。

## 三种方式构造线程

`std::thread` 是 C++11 起的标准库类，定义在 `<thread>` 头文件里。它最常用的构造函数接受一个可调用对象，再加一组传给这个对象的参数。您给它什么，它就在一个新的执行流里调用什么。如果您想再往机器里追一层，可以在做完实验后读本单元的选读。 [线程在机器上是什么](./09-what-is-a-thread)

表达“可调用”的方式不止一种，咱们按从朴素到能携带状态的顺序，把三种都跑一遍。

### 第一关：函数指针

咱们给它一个普通函数，再给一组参数，线程就起来了：

```cpp
// 三种构造方式之一:函数指针
#include <print>
#include <thread>

void print_hello(int id) {
    std::println("hello from thread {}", id);
}

int main() {
    std::thread t(print_hello, 42); // 传递了函数指针，还有他需要的参数
    t.join();
}
```

<OnlineCompilerDemo
  title="三种构造方式之一：函数指针"
  source-path="code/examples/vol5/chapter01/10_hello_thread.cpp"
  description="线程函数与参数在构造时一起传入，join 等它结束。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来一行 `hello from thread 42`。`std::thread t(print_hello, 42)` 这一句做了三件事：把函数和参数打包存进线程对象的内部存储，向操作系统要一条新的执行流，新执行流拿存好的参数去调用 `print_hello(42)`。

参数是**复制进去**的，复制发生在发起线程里，发起线程此刻就是执行 `main` 的主线程，复制失败抛的异常也留在它这里。至于复制怎么发生、引用怎么传过去，是下一篇《谁负责数据的生命周期》的主题，这里把“参数被复制”这个事实接住，就够往下走了。

### 第二关：lambda

啊，还可以是lambda，这个并不新鲜。笔者有时候一瞅这个活可以直接塞进去一个lambda，那就偷个懒咯~

```cpp
// 三种构造方式之二:lambda,按引用捕获要在 join 之后才能读结果
#include <print>
#include <thread>
#include <vector>

int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};
    long long sum = 0;

    std::thread t([&data, &sum] {
        for (int v : data)
            sum += v;
    });
    t.join();
    std::println("sum = {}", sum);
}
```

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

```cpp
class Accumulator {
  public:
    explicit Accumulator(const std::vector<int>& data) : data_(data) {}

    void operator()() const {
        long long s = 0;
        for (int v : data_)
            s += v;
        std::println("functor: sum = {}", s);
    }

  private:
    const std::vector<int>& data_; // 引用成员：拷贝进线程的只是引用本身
};

int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    Accumulator acc(data);
    std::thread t(acc); // acc 被复制一份进线程
    t.join();
}
```

<OnlineCompilerDemo
  title="三种构造方式之三：函数对象"
  source-path="code/examples/vol5/chapter01/12_functor_thread.cpp"
  description="对象被复制一份进线程，引用成员复制的只是引用本身——悬垂风险留给下一篇。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来 `functor: sum = 15`。传给 `std::thread` 的对象也会被复制一份，这个事实要跟它的引用成员放在一起看：复制进线程的只是 `data_` 这个引用本身，不是 vector。只要 `data` 活得比线程久就没问题，活得不久，线程手里就攥着一条悬垂引用。这类问题的判断和修复也在下一篇。

写函数对象这一路还有个 C++ 的老语法现象等着您。假如您把最后两步合成一句：

```cpp
int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    // std::thread t(Accumulator(data)); // 这一行声明的是名叫 t 的函数,不是线程对象

    std::thread t{Accumulator(data)}; // 花括号统一初始化,才是线程对象
    t.join();
}
```

> 把圆括号版本写成编译器看到的样子，就是 `std::thread t(Accumulator data);`：`Accumulator(data)` 在这里读作一个参数声明，类型是 `Accumulator`，参数名恰好也叫 `data`，名字外面包着一层多余的括号。于是 `t` 成了函数的名字，这个函数收一个 `Accumulator`、返回一个 `std::thread`，只有声明没有定义，线程根本没有启动。

<OnlineCompilerDemo
  title="最易错解析：圆括号还是花括号"
  source-path="code/examples/vol5/chapter01/13_vexing_parse.cpp"
  description="std::thread t(Accumulator(data)) 声明的是名叫 t 的函数；错误写法留在注释里，放开后编译器警告 -Wvexing-parse，报错记录见正文。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

这就是所谓“最令人头疼的解析”（most vexing parse）：凡是长得像函数声明的东西，一律按函数声明处理（[\[dcl.ambig.res\]/1](https://eel.is/c++draft/dcl.ambig.res)）。咱们在 GCC 16.2.1 下编译了这一句，编译器给一个警告，下一行再用 `t` 就报错：

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

```cpp
// join:发起线程的一方等它收工,再继续往下走
#include <chrono>
#include <print>
#include <thread>

void slow_work() {
    std::println("worker  : starting");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("worker  : done");
}

int main() {
    std::println("main    : launching a thread");
    std::thread t(slow_work);
    std::println("main    : waiting");
    t.join();
    std::println("main    : thread finished, continuing");
}
```

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

```cpp
// detach:放开线程自己收尾,主线程先走,进程等全部线程结束才退
#include <chrono>
#include <print>
#include <thread>

void background_task() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::println("background task finished");
}

int main() {
    std::thread t(background_task);
    t.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::println("main: exiting");
}
```

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

```cpp
// 既不 join 也不 detach:线程对象析构时 std::terminate 直接收走整个进程
#include <print>
#include <thread>

void some_work() {
    std::println("working...");
}

int main() {
    std::thread t(some_work);
    // 既不 join 也不 detach
}
```

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

```cpp
using namespace std::chrono_literals; // C++14 起的时间字面量（如 30ms），靠这行 using 声明引入

void work(int id) {
    for (int step = 0; step < 4; ++step) {
        std::println("worker {} step {}", id, step);
        std::this_thread::sleep_for(30ms);
    }
}

int main() {
    std::vector<std::thread> threads;
    for (int id = 1; id <= 4; ++id) {
        threads.emplace_back(work, id);
    }
    for (auto& t : threads) {
        t.join();
    }
}
```

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

什么叫每一次运行的结果，都不一样？！是的，这就是并发编程验证的魔盒。可能你跑几百次，几千次都对，但是就会在某一次出错。或者是错乱的并不太一样。我相信如果大家有搞性能提升时的vibe coding，时常会注意到这一点。

这里笔者不卖关子了，直接说：`std::cout` 是全程序共享的一个对象，四个线程都在往它身上写。一条输出语句里连着几个 `<<`，标准并不保证它们拼成一个不可分割的整体，谁写完一小段，谁就可能被别人插进来一截。所以您看到的现象，行序会变，行内会碎，内容一条都没有少，都是同一件事的表现：多个执行流写同一个对象，中间没有同步，写出来的字节流就是交错的。

咱们又换了个写法验证“插进来”这件事本身。让三个线程各自睡满 50 毫秒后同时开写，每个片段不带换行，全部写完才打一个换行：

```cpp
using namespace std::chrono_literals;

void chatter(int id) {
    std::this_thread::sleep_for(50ms); // 让三个线程几乎同时醒来
    for (int step = 0; step < 6; ++step) {
        std::print("[w{}:{}]", id, step);
    }
    std::println("");
}

int main() {
    std::vector<std::thread> threads;
    for (int id = 1; id <= 3; ++id) {
        threads.emplace_back(chatter, id);
    }
    for (auto& t : threads) {
        t.join();
    }
}
```

<OnlineCompilerDemo
  title="交错输出：结果不确定"
  source-path="code/examples/vol5/chapter01/18_chatter_interleave.cpp"
  description="三条线程同时醒来各自连打六个标记，输出顺序每次都不同——这就是并发程序的非确定。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

按您单线程的直觉，这该是三行整齐的片段。咱们跑了上千轮，乱起来的样子像下面这样，这是其中的某一次抽奖抽出来的结果：

```text
[w[w1:0][w1:1][w1:2][w1:3][w1:4][w1:5]
2:0][w2:1][w2:2][w2:3][w2:4][w2:5]
[w3:0][w3:1][w3:2][w3:3][w3:4][w3:5]
```

专门数过一遍：四百轮里八十五轮是乱的，剩下那些轮次都是三行整齐的。咱们按行看这次捕获的样本。worker 2 的第一个片段才写出 `[w` 两个字符，worker 1 的整行连着换行插了进来，随后 worker 2 从 `2:0]` 接着写完了自己剩下的片段。三个换行倒是都待在自己内容的末尾，穿插进来的是整行内容。什么时候乱、乱成什么样，每次跑都可能不同，这就是第一篇开头那句话的现场版：并发程序里“这个变量现在是什么值”需要证明，`std::cout` 的输出只是它最容易亲眼看到的样子。

> 啥你问我怎么修，等等等等等下后面介绍锁的概念的时候说。

## 线程函数里抛异常会怎样

异常不能逃出线程函数。咱们直接跑一个违反它的程序：

```cpp
// 异常逃出线程函数:main 的 try 接不住,std::terminate 收走整个进程
#include <print>
#include <stdexcept>
#include <thread>

void unsafe_worker() {
    throw std::runtime_error("oops, something went wrong");
}

int main() {
    try {
        std::thread t(unsafe_worker);
        t.join();
    } catch (const std::exception& e) {
        std::println("caught: {}", e.what());
    }
}
```

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

```cpp
// 异常在线程函数内部接住:main 正常继续
#include <print>
#include <stdexcept>
#include <thread>

void safe_worker() {
    try {
        throw std::runtime_error("oops, something went wrong");
    } catch (const std::exception& e) {
        std::println("worker caught: {}", e.what());
    } catch (...) {
        std::println("worker caught unknown exception");
    }
}

int main() {
    std::thread t(safe_worker);
    t.join();
    std::println("main continues normally");
}
```

<OnlineCompilerDemo
  title="异常在线程内接住"
  source-path="code/examples/vol5/chapter01/21_exception_caught.cpp"
  description="try/catch 写在线程函数里，失败就地消化，main 正常继续。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

咱们跑出来两行都在，退出码 0，程序活着走完了。线程内部接住异常、至少接住一个 `catch (...)` 兜底，是手写 `std::thread` 时的基本防御。要是您想把线程里的失败如实递回主线程处理，标准库有正路，`std::exception_ptr` 可以把异常打包带走，把递回失败修成现成设施的，是 `std::async` 和 `std::promise`/`std::future`，后面讲任务组织的那几篇专门讲怎么传。

## 下一步

今天这些程序咱们全程守着手动档：起线程靠构造，收尾靠 join 或 detach，异常路径上漏一次 join 就是 terminate，屏幕乱了也只能等后面讲互斥那一篇的锁来管。接下来两篇把这些手动档换成有设计的写法：《谁负责数据的生命周期》讲参数怎么传、引用什么时候悬垂，《谁负责线程的生命周期》讲怎么用 RAII 把 join 的责任从您的记性里接过去，那个包装器就是前面说的在析构里自动等待的东西，到时候您亲手写一遍。您手里现在有了第一手的现象，今天看到的这些乱序和终止，后面讲竞争讲同步时都会回来。

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
