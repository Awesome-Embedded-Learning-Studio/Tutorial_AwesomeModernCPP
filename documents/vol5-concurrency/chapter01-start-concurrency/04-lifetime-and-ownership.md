---
title: "谁负责数据的生命周期"
description: "看清 std::thread 参数的 decay-copy 与 move-only 传法，认识悬垂引用的两种修复路径。"
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
---

# 谁负责数据的生命周期

OI，开单要兑现，对吧，在上一篇结尾咱们许诺的事，这一篇兑现两件：参数复制怎么发生，引用什么时候悬垂。join 的责任怎么从您的记性里接出去，留给下一篇。

## 参数怎么传：decay-copy 与 move-only 类型

### 复制发生在发起线程

上一篇留过一个尾巴：`std::thread t(print_hello, 42)` 这一句里“参数被复制”，复制到底怎么发生的。咱们把 cppreference 在 [`std::thread` 的构造页面][cppref-ctor] 的规定翻出来看（C++11 起），新线程开始执行的是这样一个调用：

```text
INVOKE(decay-copy(std::forward<F>(f)),
       decay-copy(std::forward<Args>(args))...)
```

INVOKE 是调用的英文，说白了意思就是发起一个线程准备跑东西。跑啥呢？跑的东西又需要什么呢？自然需要的就是函数，和函数执行所需要的参数。这样看，我们解释到的位置是：`INVOKE(f, args)`，std::forward并不奇怪，表达的是维持左右值的转发，维度需要注意一下的是`decay-copy`。

decay-copy是退化复制的意思，是这里的正式名目。它的意思用白话说就是：每个实参都按函数模板传递参数的路数处理一遍，**引用剥掉，顶层的 const 和 volatile 丢掉，数组退成指针，函数退成函数指针**。

好，我知道有小朋友问了，尽管0人问我。喂我说这标准库的人都是吃干饭的？有病啊套一层这个，嫌C++太麻烦了嘛？

并不是的，我们要始终回扣到之前我们提到的，现在开始，我们的代码不再按照同步的方式运作了，您使用的这些变量，您自己也不知道什么时候才会使用。换而言之：“**我们的变量的生命周期，要不不是按照decay-copy的方式传递的话，我们没办法保证自己程序的运行安全**”，我们的`arg` 所在的函数随时可能返回，变量随时可能销毁，而新线程要到真正跑起来才用得上参数，中间隔着一段没人担保的时间。所以**默认动作是当场复制一份，存进线程对象的内部存储，新线程从头到尾只用自己的副本**。默认按值是安全默认，您不想复制、想共享，就得显式写出 `std::ref`，下一小节讲。

复制解释了，退化呢？因为有两类东西“按值复制”这件事本身做不到。数组没有值语义，您没法把一个数组按值传给任何函数，也没法用一个数组去初始化另一个（咱们试过，编译器当场拒绝），函数类型连对象都算不上，能存下来的是函数指针，能复制的最接近替代就是退化出来的指针。引用则必须先剥掉，不然存进内部存储的就是引用本身，发起线程的对象一销毁，悬垂跟着进来，这份复制等于白做。

<OnlineCompilerDemo
  title="decay-copy：传进线程的是副本"
  source-path="code/examples/vol5/chapter01/22_decay_copy.cpp"
  description="拷贝构造在线程一侧执行、副本地址各不相同；指针传的只是地址，数组不会被复制。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// decay-copy:传给线程的对象是副本,指针和引用传的是地址本身
#include <cstddef>
#include <print>
#include <thread>

class Tracker {
  public:
    explicit Tracker(int v) : value_(v) {}
    Tracker(const Tracker& other) : value_(other.value_) {
        std::println("  copy ctor on thread {}, copy lives at {}", std::this_thread::get_id(),
                     (const void*)this);
    }
    int value() const { return value_; }

  private:
    int value_;
};

void work(Tracker t) {
    std::println("  worker got its own object at {}, value {}", (const void*)&t, t.value());
}

void read_array(int* data, std::size_t n) {
    std::println("  worker sees an int* holding {} elements", n);
    long long s = 0;
    for (std::size_t i = 0; i < n; ++i)
        s += data[i];
    std::println("  sum {}", s);
}

int main() {
    Tracker original(7);
    std::println("main thread id {}", std::this_thread::get_id());
    std::println("original lives at {}", (const void*)&original);
    std::thread t(work, original);
    t.join();

    int arr[5] = {1, 2, 3, 4, 5};
    std::thread t2(read_array, arr, 5);
    t2.join();
}
```

</OnlineCompilerDemo>

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
>

```cpp
#include <print>
#include <thread>

void update_value(int& x) {
    x = 42;
}

int main() {
    int value = 0;
    // std::thread t(update_value, value);  // 编译不过
    std::thread t(update_value, std::ref(value));
    t.join();
    std::println("value = {}", value);
}
```

</OnlineCompilerDemo>

```text
error: static assertion failed: std::thread arguments must be invocable after conversion to rvalues
```

`update_value` 想要 `int&`，退化复制之后线程手里只有一份 `int` 的右值，右值绑定不上非 const 引用（报错的是 libstdc++ 16.2.1 的 static_assert，其余几行报错都是它带出来的连锁）。这个编译错误是标准库在执行“默认按值”的立场：共享是危险动作，您想共享，就得在代码里写出共享两个字，让审查代码的人一眼看见。写出来的工具是 `std::ref` 和 `std::cref`（都是 C++11 起，定义在 `<functional>`）：它们把引用包成一个可以复制的 reference_wrapper（引用包装器），线程调用函数时把包装解掉，函数收到的还是对原始对象的引用。

<OnlineCompilerDemo
  title="std::ref 与 std::cref：真的引用"
  source-path="code/examples/vol5/chapter01/24_ref_wrap.cpp"
  description="用引用包装器之后，worker 摸到的就是 main 的同一个对象，地址一致、修改可见。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// std::ref 与 std::cref:让线程真的引用主线程的对象
#include <functional>
#include <print>
#include <string>
#include <thread>

void append_suffix(std::string& str, const std::string& suffix) {
    std::println("  worker touches str at {}", (void*)&str);
    str += suffix;
}

int main() {
    std::string message = "Hello";
    std::string suffix = " World";
    std::println("main's message lives at {}", (void*)&message);
    std::thread t(append_suffix, std::ref(message), std::cref(suffix));
    t.join();
    std::println("message = {}", message);
}
```

</OnlineCompilerDemo>

咱们跑出来 `worker touches str at` 和 `main's message lives at` 两行地址完全相同，`message` 也真的变成了 `Hello World`，线程改的就是 main 里那个对象。这个例子安全，因为 `join()` 在场，线程用 `message` 的时候它还活着。要是把 join 换成 detach，线程对 `message` 的引用什么时候过期就没有人管了，下一节解剖的悬垂引用正是这类事。

### move-only 类型：所有权跟着线程走

并不是什么类型都拷得动。您在卷二讲智能指针的部分熟悉过的 `std::unique_ptr` 就是一个，这类只可移动的类型（拷贝构造被删除，只支持 move 的类型），move 进线程才是正路：

<OnlineCompilerDemo
  title="move-only：unique_ptr 进线程"
  source-path="code/examples/vol5/chapter01/25_move_unique.cpp"
  description="所有权随 std::move 交进线程，main 侧变空指针，各归各的活。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// move-only 类型:unique_ptr 的所有权随 std::move 交进线程
#include <cstddef>
#include <memory>
#include <print>
#include <thread>

void process_data(std::unique_ptr<int[]> data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i)
        data[i] *= 2;
    std::println("  worker: data[0] = {}", data[0]);
}

int main() {
    constexpr std::size_t kSize = 5;
    auto data = std::make_unique<int[]>(kSize);
    for (std::size_t i = 0; i < kSize; ++i)
        data[i] = static_cast<int>(i + 1);
    std::thread t(process_data, std::move(data), kSize);
    std::println("main: data after move is {}", data ? "not null" : "null");
    t.join();
}
```

</OnlineCompilerDemo>

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
>

```cpp
// 悬垂引用:按引用捕获活不过本函数的局部变量,线程读到的是旧栈上的垃圾
#include <chrono>
#include <cstring>
#include <print>
#include <thread>

void burn_stack() { // 往栈上灌别的数据，看能不能把旧值覆盖出来
    volatile char area[16384];
    std::memset(const_cast<char*>(area), '#', sizeof(area));
}

void spawn() {
    int local_value = 42;
    std::thread t([&local_value] { // 按引用捕获一个活不过本函数的局部变量
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::println("thread reads: {}", local_value);
    });
    t.detach();
}

int main() {
    spawn();
    burn_stack();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("main exits");
}
```

</OnlineCompilerDemo>

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
>

```cpp
// 捕获 this 指针后销毁对象:未定义行为,读到垃圾或崩溃都是它的样子
#include <chrono>
#include <print>
#include <thread>

class Worker {
  public:
    void start() {
        t_ = std::thread([this] { // 捕获的是指针，不是对象
            for (int i = 0; i < 8; ++i) {
                std::println("tick {}, sees running_ = {}", i, running_);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        t_.detach();
    }

  private:
    bool running_ = true;
    std::thread t_;
};

int main() {
    Worker* w = new Worker;
    w->start();
    delete w; // 对象立刻销毁，线程还在跑
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    std::println("main exits");
}
```

</OnlineCompilerDemo>

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
>

```cpp
// 值捕获修复悬垂:线程拿自己的副本,父函数先走也不怕
#include <chrono>
#include <print>
#include <string>
#include <thread>

void safe_version() {
    std::string message = "Hello from parent";
    std::thread t([message] { // 值捕获：线程拿自己的副本
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::println("thread sees: {}", message);
    });
    t.detach();
}

int main() {
    safe_version();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("main exits");
}
```

</OnlineCompilerDemo>

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
>

```cpp
// shared_ptr 加 atomic 标志:对象销毁后线程仍能安全收场
#include <atomic>
#include <chrono>
#include <memory>
#include <print>
#include <thread>

class BackgroundWorker {
  public:
    BackgroundWorker() : running_(std::make_shared<std::atomic<bool>>(true)) {}

    void start() {
        auto running = running_; // 复制 shared_ptr，引用计数加一
        std::thread t([running] {
            while (running->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            std::println("worker exits cleanly");
        });
        t.detach();
    }

    void stop() { running_->store(false); }
    ~BackgroundWorker() { stop(); }

  private:
    std::shared_ptr<std::atomic<bool>> running_;
};

int main() {
    {
        BackgroundWorker worker;
        worker.start();
    }
    std::println("worker object already destroyed");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::println("main exits");
}
```

</OnlineCompilerDemo>

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

语法怎么变，背后的原则只有一条：被引用的对象必须活到线程用完它为止。两条修复路径是您手工维持这句话成立的办法，把这份维持交给析构函数自动做，是下一篇《谁负责线程的生命周期》的主题。

## 下一步

这一篇把数据这一侧收拢了：要么复制进线程（默认的退化复制一直在做这件事），要么用 `shared_ptr` 把生命周期延到最后一份共享指针消失。您手里现在有两段修复代码，都能搬进您自己的程序里跑。数据安顿好了，线程自己还悬着：join 写在函数末尾，异常一来就被跳过，谁来兜底。把 join 的责任从您的记性里接出去、交给析构函数，是下一篇《谁负责线程的生命周期》的主题，thread_guard 与手写的 JoiningThread 在那里等您。

## 参考资源

本篇写作时对照借鉴了 Anthony Williams 的《C++ Concurrency in Action》（Manning 出的第二版），例子与文字都是我自己写的。涉及 C++ 行为的断言，均按文中标注编译实测或查证 cppreference，std::thread、ref/cref、shared_ptr 自 C++11 起可用，make_unique 是 C++14，[=,*this] 是 C++17，`[=]` 隐式捕获 this 的废弃是 C++20。悬垂引用的输出按真实运行记录，未定义行为的运行结果在您的机器上可能不同。

- [std::thread::thread（cppreference）][cppref-ctor]，构造语义，退化复制在发起线程求值的规定。
- [std::ref 与 std::cref（cppreference）][cppref-ref]，reference_wrapper 的生成与拆包。
- [std::shared_ptr（cppreference）][cppref-shrptr]，并发调用与引用计数的保证范围。
- [P0806: Deprecate Implicit Capture of this][p0806]，C++20 废弃 `[=]` 隐式捕获 this 的提案。

[cppref-ctor]: https://en.cppreference.com/w/cpp/thread/thread/thread
[cppref-ref]: https://en.cppreference.com/w/cpp/utility/functional/ref
[cppref-shrptr]: https://en.cppreference.com/w/cpp/memory/shared_ptr
[p0806]: https://www.open-std.org/jtc1/SC22/wg21/docs/papers/2017/p0806r0.html
