---
title: "thread_local：每线程一份的世界"
description: "把共享变量变成每线程一份：初始化的翻译单元粒度、析构的逆序时点、三种声明位置，以及什么时候不该用它"
chapter: 2
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 22
prerequisites:
  - "mutex 与 RAII 守卫"
  - "数据竞争与 ThreadSanitizer 第一课"
  - "线程参数与生命周期陷阱"
related:
  - "死锁与现场诊断"
  - "同步原语工具箱"
  - "原子操作与 happens-before"
cpp_standard: [11, 20]
---

多线程的世界里有个现象，您天天在读它、却未必留意过：`errno`。POSIX（一套可移植操作系统的接口标准）对它的要求是每个线程里各有一份，现代 glibc（Linux 通用的 C 运行库）把 `errno` 展开成对 `__errno_location()` 的调用，函数返回的地址来自线程局部存储，所以两个线程各自读到的错误码互不覆盖。glibc 的内存分配器同样给不同线程准备不同的 arena（分配器划给各线程的私有堆区），目的也是一样的。多线程程序之所以没有在头一步就乱掉，靠的就是这套机制在底层的支撑。

让这套机制成立的语言说明符，就是本篇的主角 `thread_local`，名字把语义都说明白了：带上它的变量在每个线程里各有自己的实例，构造是各自的，析构也是各自的，线程之间互相看不见对方的实例。[mutex 篇](./01-mutex-and-raii-guards.md)里咱们用锁把共享数据围了起来，可是数据只是被围了起来，每个线程的访问依然指向同一个对象，咱们还有一条更彻底的路对付竞争：让数据不再共享。

它并不承担锁的职责，这一点咱们得摆在开头说清楚。锁解决的问题是很多人同时够一份数据，`thread_local` 做的事情是把一份数据变成很多份，从根上取消了共享，所以两件工具处在不同的方向上，谁也替代不了谁的职责。本篇咱们要看清的对象相应有三块：构造的时机、析构的时点、说明符能写的位置，收尾咱们再回答不该用它的时候怎么判断。

## 每线程一份，到底意味着什么

咱们要写的全部代价，只是名字前面多写的一个说明符：

```cpp
thread_local int counter = 0;
```

咱们写下这行之后，`counter` 就不再是进程里唯一的对象了，每个线程手里都有自己的一个。t1 里的 `++counter` 改的是 t1 的实例，t2 改的是 t2 的，连地址都是不同的。咱们把最朴素的演示记作场景 1：起两个工作线程各自数三下，把 main 里的值也打出来对比。

```cpp
thread_local int counter = 0;   // 常量初始化，构造上没有任何动作

void bump(const char* who)
{
    for (int i = 0; i < 3; ++i) {
        ++counter;
        std::printf("%s: counter=%d，地址 %p\n", who, counter, (void*)&counter);
    }
}

std::thread t1(bump, "t1");
std::thread t2(bump, "t2");
t1.join();
t2.join();
```

咱们把程序跑起来看输出，地址的值在每次运行、每台机器上都不同，要看的不是数值本身：

<OnlineCompilerDemo
  title="动手验证：thread_local 让每个线程数自己的数"
  source-path="code/examples/vol5/30_thread_local_counter.cpp"
  description="t1 与 t2 各自数到 3，main 的 counter 始终是 0。地址的十六进制数值每次运行都不同，看的是两点：同一线程三次的地址不变，两个线程的地址互不相同。"
  run-options="-O2 -std=c++17 -pthread"
  allow-run
/>

咱们把输出并排读一遍：三个执行流拿到了三段不同的地址，main 里的 `counter` 自始至终都是 0。t1 与 t2 求值的是同一个名字，拿到的却是不同的对象，这就是线程存储期（thread storage duration）的含义，名字只有一个、实例的生死跟着各自的线程走。

它的位置咱们放在一起看。同样写在函数外的 `int`，不带说明符的时候具有静态存储期，进程里存在的实例只有一个，所有线程共享的就是它。带上 `thread_local` 之后换成了线程存储期，实例跟着线程各配了一份。函数体内的自动变量虽然也待在各自的线程栈上，可它的生存期只到函数返回为止，`thread_local` 的对象却能活到线程退出。您要是想要一份生存期覆盖整个线程、又不与别的线程共享的变量，语言里对应的就是这里。

有一件事咱们得说在前面：`thread_local` 并没有带来任何同步。每线程一份消掉的只是同名访问的共享，您要是把 `&counter` 这样的地址交给别的线程，指针指向的仍是发出方线程的实例，跨线程通过它进行的读写照样构成 data race，对方线程退出的时候析构也会跟着发生，指针也就悬垂了。[data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)里咱们建起来的诊断手段，放到这里的场景同样适用。

## 只碰一个变量，三个构造函数一起跑

入了门，咱们直接看一个能让老手也愣住的现象，它就是场景 2 的主角。下面的三个变量都装上了构造与析构的留痕，main 与 worker 各自只碰它们当中的一个：

```cpp
struct Tag {
    const char* name;
    explicit Tag(const char* n) : name(n) { std::printf("    ctor %s\n", n); }
    ~Tag() { std::printf("    dtor %s\n", name); }
};

thread_local Tag probe_a{"a"};
thread_local Tag probe_b{"b"};
thread_local Tag probe_c{"c"};

void scenario_init_granularity()
{
    std::printf("main 只碰 probe_c：\n");
    (void)probe_c;                  // 只碰这一个

    std::thread worker([] {
        std::printf("worker 只碰 probe_a：\n");
        (void)probe_a;              // 也只碰这一个
    });
    worker.join();
    std::printf("join 已返回\n");
}
```

按照不少资料的讲法，`thread_local` 变量在每个线程里只构造第一次用到的对象，没有用到的对象不会有任何动静。照这样的预期，main 只碰了 `probe_c`，另外的两个探针就不该出场。可咱们实际跑出来的输出是：

<OnlineCompilerDemo
  title="动手验证：只碰一个探针，三个构造函数一起跑"
  source-path="code/examples/vol5/31_tls_init_granularity.cpp"
  description="main 只碰 probe_c，ctor a、b、c 却一起出场；worker 同样如此，且它的三个 dtor 都跑在 join 返回之前。join 已返回之后还有 main 线程自己的 dtor c、b、a 三行，那是进程收尾时 main 的三个探针在析构。"
  run-options="-O2 -std=c++17 -pthread"
  allow-run
/>

main 只碰了 `probe_c`，三个构造函数却一起跑了。worker 只碰了 `probe_a`，结局也是一样的。请您再留意 join 的位置，worker 的三个析构函数在它返回之前就全部跑完了。构造怎么一起跑、析构什么时候来，咱们顺着这两件事往下看。

## 构造的粒度是翻译单元，不是单个变量

咱们把标准请出来。[basic.start.dynamic] 第 7 段的规则自 C++11 起一致，译文是：一个非块级、非 inline 的线程存储期变量，它的动态初始化要么排在线程初始函数的第一条语句之前，要么推迟到后面的时点，选择的权力由实现定义。在被推迟的情况下，线程 t 对它的初始化排在某个时点之前：t 第一次非初始化地 odr-use 到任何一个非 inline 的线程存储期变量，而那个变量与它同处一个翻译单元、并且本身就是需要动态初始化的。译文里的 odr-use 指名字被真正用上，读它的值，取它的地址，用引用去绑它的名字，光有声明是不算的。

这段译文里有两个关键的限定，咱们把它们挑出来。头一个限定说的是推迟与否由实现来决定，GCC 在 x86-64 Linux 上选择的就是推迟。第二个限定更值得咱们留意：触发点认的是同一个翻译单元里任何一个需要动态初始化的线程存储期变量，认的范围不局限于变量本身。也就是说在 `probe_c` 被 odr-use 的那一刻，与它同处一个翻译单元的 `probe_a` 和 `probe_b` 的推迟初始化会一起结算，构造的粒度是整个翻译单元。场景 1 里的 `counter` 是常量初始化，不需要任何动态的初始化，所以碰它不会触发探针的构造，条文中需要动态初始化的限定说的就是这一层。

汇编层面的证据咱们也抓了一份，对演示程序跑 `nm -C` 的结果如下：

```text
000000000000186f T TLS init function for probe_a
000000000000186f T TLS init function for probe_b
000000000000186f T TLS init function for probe_c
0000000000001bb9 t TLS wrapper function for probe_a
0000000000001b9f t TLS wrapper function for probe_c
0000000000000028 b __tls_guard
```

三个 TLS init function（TLS 就是 thread local storage 的缩写，说的还是线程局部存储）落在了同一个地址 `0x186f` 上，那就是整个翻译单元共享的 `__tls_init` 函数，配上一个字节的 `__tls_guard` 做一次性开关。咱们把它反汇编，开头的几行长这样：

```text
186f: movzbl %fs:0xfffffffffffffff8,%eax    ; 读本线程的 guard
187c: xor    $0x1,%eax
187f: test   %al,%al
1881: je     196e                           ; 初始化过，直接出去
1887: movb   $0x1,%fs:0xfffffffffffffff8    ; 置位 guard
18ac: call   Tag::Tag(char const*)          ; 构造 probe_a
18d5: call   __cxa_thread_atexit@plt        ; 注册 probe_a 的析构
```

（地址与符号名随编译器的版本浮动，行为是一致的。）`__tls_init` 做的第一件事是检查 guard，guard 没有立起来的线程会把它立起来，随后按声明的顺序构造 a、b、c 并逐个调用 `__cxa_thread_atexit`，把析构函数挂到线程退出的列表上。TLS wrapper 是按变量生成的，`probe_a` 与 `probe_c` 里的每一个都有自己的包装函数，`probe_b` 它从头到尾没有被咱们 odr-use 过一次，连属于它的包装函数都没有生成，可它的构造照样跑了。标准条文、运行输出与汇编符号的三层证据，说的都是同一件事。

咱们再把 `movzbl %fs:0xfffffffffffffff8` 里的 `%fs` 说明一下：x86-64 Linux 上它指向当前线程的控制块，线程局部存储就挂在以它为基址的内存里，所以 guard 与探针的地址都带负的段内偏移。您留意到这个事实就够了：每线程一份的寻址，硬件与运行时是一起配合好的。

本仓库旧版教程在这一段的说法，原文咱们摘录如下：

```text
如果一个 thread_local 变量从来没有被某个线程访问过，那个线程就不会为它分配内存或执行初始化。
```

对照上面的三层证据，咱们可以看出这句话按单变量去读是不成立的：哪怕变量本身没有被用到，同一个翻译单元里其他变量的使用也会把整体的初始化触发。旧的说法流传得很广，它给人的直觉会让您低估一个重型构造函数在真实工程里的触发面。

工程上的推论有三条，咱们逐条摊开：

- 命名空间作用域上，别指望靠不用某个变量来省构造的开销。翻译单元内任何一个变量的第一次使用，都会让全部推迟的初始化一起结算，重型构造函数躲不开。
- 想要按变量懒构造，把声明写进函数体。块作用域走的是另一套语义，构造发生在控制流第一次经过声明的地方，本文后半有实测。
- 跨翻译单元的初始化顺序没有任何保证。一个 `thread_local` 对象的构造函数若引用另一个翻译单元里的 `thread_local` 对象，就可能读到未初始化的状态。

### 构造抛异常：条文与实测

命名空间作用域里剩下的最后一个边角，是构造函数抛出的异常。[basic.start.dynamic] 第 8 段的原文很干脆，非块级变量的初始化若以异常退出，标准给的后果是调用 `std::terminate`。笔者在 GCC 16.2.1 上实测到的行为存在出入：推迟的初始化发生在线程第一次 odr-use 的现场，异常从那里抛了出来，外层的 `try`/`catch` 能接得住，进程也活了下来。放开 try 让异常直接逃逸的情况下，进程会以 134 的退出码终止。条文与实测的出入笔者如实摆在这里，谁对谁错的裁决咱们不做，给您的建议只有一条：给 `thread_local` 对象写不抛异常的构造函数，两种解释的分歧就与您无关了。

## 析构：逆序、时点，还有 std::exit

析构的规则，咱们接着场景 2 的输出往下读。worker 线程里构造的顺序是 a、b、c，析构的顺序是 c、b、a，main 返回之后 main 线程的三个对象同样按 c、b、a 走。[basic.start.term] 第 4 段给出了排序的规则，译文是：同线程内若一个对象的构造 sequenced before 另一个，则后者的析构完成排在前者的析构开始之前。sequenced before 说的就是单线程里的执行次序，咱们在函数体里读代码时的“上一句、下一句”就是它。跨线程场合的排序关系，正式的定义在 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md)，这里咱们按字面理解就够用了。

咱们再看顺序的覆盖面：它是不分声明位置的。块作用域里声明的 `Flaky`（它的完整演示是后面的场景 3）比三个 `probe` 构造得晚，程序退出的时候它的析构排在三个 `probe` 的析构之前出场，最终输出里 `dtor：成功构造过` 的一行排在 `dtor c` 的上面。无论声明在命名空间作用域还是块作用域的对象，构造与析构走的都是同一个逆序。

时点的规定来自 [basic.start.term] 第 2 段，译文是：线程内所有构造完成的线程存储期对象，随线程从初始函数的返回而析构，也随该线程对 `std::exit` 的调用而析构。这些析构排在该线程任何线程存储期对象的存储释放之前，也排在任何静态存储期对象的析构之前，标准的说法是 strongly happens before。前一半咱们在输出里已经看到了现场：worker 的三个析构跑在线程函数返回之后、线程存储释放之前，join 返回之前就已经全部结束了。

第 2 段还藏着一个容易被忽略的推论：谁调用了 `std::exit`，被析构的就只有调用方线程的对象。咱们要是此刻还有别的线程没跑完，它们的线程存储期对象就没人负责析构了，进程退出的瞬间，操作系统直接回收了内存，析构函数连一次执行的机会都没有。咱们做个实验验证这个推论：

```cpp
thread_local Tag worker_tag{"worker_tag"};   // Tag 与场景 2 的相同

int main()
{
    std::thread worker([] {
        (void)worker_tag;   // 触发本线程的构造
        std::this_thread::sleep_for(std::chrono::seconds(5));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::printf("main: 还没等 worker 醒，直接 std::exit\n");
    std::exit(0);
}
```

咱们拿到的输出到第二行就结束了：

<OnlineCompilerDemo
  title="动手验证：std::exit 时 worker 的 thread_local 不析构"
  source-path="code/examples/vol5/32_exit_skips_worker_tls_dtor.cpp"
  description="只有 ctor worker_tag 与 main 的告别两行，找不到 dtor worker_tag：worker 的对象构造了，析构无声缺席。进程约 0.2 秒就退出，不会等 worker 睡满 5 秒。"
  run-options="-O2 -std=c++17 -pthread"
  allow-run
/>

输出里找不到 `dtor worker_tag` 的行：worker 的对象构造了，析构却无声无息地缺席了。把刷新日志缓冲、回写文件一类的事情放进 `thread_local` 析构的代码，在别的线程更早调用 `exit` 的情况下会无声地丢数据。想在长命的后台线程上依赖析构的副作用，请您三思。

咱们再看析构阶段的一个未定义行为：一个对象的析构函数里访问同线程已经析构的另一个线程存储期对象，就是在对象的生存期之外使用它，标准给的行为是未定义。逆序的规则保证了构造早的析构晚，所以被引用的对象若构造得更晚，反而排在更早的位置被析构，风险是真实存在的。析构函数里只做自己份内的清理，别去碰同线程里其他的线程存储期对象。

## 声明能写在哪：三种位置，一处隐含

说明符能出现的位置有哪些，cppreference 的存储类说明符表格给了完整的清单，咱们按使用的频率重排：

| 位置 | 能否声明 thread_local | 备注 |
|------|------|------|
| 命名空间作用域 | 可以 | 最常见，名字全局可见，实例每线程一份 |
| 类的静态数据成员 | 可以 | 类内声明，类外定义时 `static` 不再写，`thread_local` 要跟着写 |
| 函数体内（块作用域） | 可以 | 隐含 `static`，本线程内从此每次调用共享，跨线程隔离 |
| 函数形参 | 不行 | 形参本来就各自占用调用线程的栈帧 |
| 非静态数据成员 | 不行 | 成员属于对象实例，谈不上属于哪个线程 |

类内静态成员的写法有个小细节，咱们看实测过的例子：

```cpp
struct Widget {
    static thread_local int calls;    // 类内声明
};
thread_local int Widget::calls = 0;   // 类外定义：static 不重复，thread_local 要写
```

两个线程各自把 `Widget::calls` 加到了 1，地址也是互不相同的，与场景 1 的表现是一致的。块作用域对应的格子里写的隐含 `static`，是单凭阅读代码最容易漏掉的信息。您在函数体里写下 `thread_local Flaky engine;`，得到的是每线程一份的静态生存期对象，本线程内从此每次调用共享的是它，跨线程的视角下又各自隔离。它的初始化也换了一套语义：构造发生在控制流第一次经过声明的地方，而不是命名空间作用域里跟随第一次 odr-use 的整体结算。

组合的写法也交代一下：一个声明里最多只有一个存储类说明符，`thread_local` 是唯一能与 `static` 或 `extern` 组合的例外。块作用域里写 `thread_local static` 合法但 `static` 是冗余的，命名空间作用域上单独出现的 `thread_local` 隐含 `static`，与 `extern` 组合的时候就不隐含了，出处是 CWG（C++ 标准的核心工作组）的议题 1648 修订。下面几行咱们全部编译验证过：

```cpp
thread_local static int a = 1;        // 命名空间：static 显式写出，合法
extern thread_local int b;            // 声明：extern 组合，不隐含 static
thread_local int b = 2;               // 定义

int probe_block()
{
    thread_local static int c = 3;    // 块作用域：static 冗余但合法
    thread_local int d = 4;           // 块作用域：隐含 static
    return a + b + c + d;
}
```

这套语义配上会抛异常的构造函数，还有重试的行为，咱们直接看场景 3 的代码：

```cpp
int g_attempts = 0;

struct Flaky {
    Flaky()
    {
        ++g_attempts;
        std::printf("    第 %d 次尝试构造\n", g_attempts);
        if (g_attempts < 3) {
            throw std::runtime_error("还没准备好");
        }
    }
    ~Flaky() { std::printf("    dtor：成功构造过，退出线程时析构\n"); }
};

int next_id()
{
    try {
        thread_local Flaky engine;    // 每线程一份，首次经过声明时构造
    } catch (const std::exception& e) {
        std::printf("    捕获：%s\n", e.what());
        return -1;
    }
    return g_attempts;
}
```

咱们连续调用三次 `next_id`，再看程序的收尾部分，输出的内容是：

<OnlineCompilerDemo
  title="动手验证：块作用域 thread_local 构造失败后重试"
  source-path="code/examples/vol5/33_block_tls_flaky_retry.cpp"
  description="前两次尝试构造抛异常、调用返回 -1，第三次构造成功后第 3 次调用返回 3；main 返回之后 Flaky 的 dtor 先出场，排在三个探针的 dtor c、b、a 之前——块作用域对象构造得更晚，析构反而更早。"
  run-options="-O2 -std=c++17 -pthread"
  allow-run
/>

咱们把三次调用读一遍：构造抛了异常的对象不算初始化完成，控制流下一次经过声明的位置会再试一次，第三次成功了之后，析构的注册才算完成。咱们换一个线程来跑，语义原样地成立，每个线程各走各的第一次。块作用域因此是真正的按变量懒构造，想要省下确定用不到的构造开销，合适的位置就在这里。每线程的随机数引擎是这里的常客：

```cpp
double next_score()
{
    // 每线程一份引擎，构造发生在各线程第一次经过声明
    thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<double> dist{0.0, 1.0};
    return dist(gen);
}
```

## 什么时候不该用 thread_local

机制看完了，咱们要面对的是真正的选型问题。默认的答案应当是显式传参：数据从实参或 lambda 的捕获流进函数，谁读了什么、又从哪里来，咱们一眼就能查清。[线程参数与生命周期](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md)里咱们看过 decay-copy 的按值隔离，那就是显式路线的底色。`thread_local` 不该成为默认的选项，它合理的位置是没有自然参数位、又不值得层层加参的隐式上下文。

真正合适的场景，咱们按常见程度列：

- 日志与追踪的上下文：请求 ID、模块名一类标记，库函数深处直接读取，省掉每一层函数都传一遍的样板。
- 每线程的资源持有者：随机数引擎、内存池、格式化缓冲区，各自构造各自用，共享版本要么加锁要么竞争。
- 递归深度计数与重入保护：进入时加一，退出时减一，天然每线程一份。
- 平台与运行时的每线程状态：`errno` 是原型，您自己写的库一般不该再发明新的。

咱们用一个最小的日志场景把两条路线都写出来，头一个是显式传参的版本：

```cpp
struct LogCtx {
    const char* request_id;
};

void log_line(const LogCtx& ctx, const char* msg);   // 每个调用点都看得见 ctx

void handle(const LogCtx& ctx)
{
    log_line(ctx, "handling");
}
```

咱们再看 `thread_local` 的版本：

```cpp
struct LogCtx {
    const char* request_id;
};

thread_local LogCtx current_ctx{};   // 隐式上下文：入口设置一次，深处直接读

void log_line(const char* msg);      // 签名里没有 ctx

void handle()
{
    log_line("handling");            // 谁在读 current_ctx？这里看不出来
}
```

两个版本咱们都能跑通，区别在读代码的人需要知道多少。显式版本的调用链上，数据流是可见的，单测直接把构造好的 `LogCtx` 传进去就能跑。隐式版本省掉了层层传参的样板，可 `handle` 的行为多了一个看不见的输入，单测必须提前布置好全局的状态。您工程里的选择标准就在这里：需要调用方感知的上下文走参数的路线，纯粹属于内部实现的上下文，才值得咱们把 `thread_local` 请出来。

它的一个正当场景您已经见过了：[死锁与 gdb](./02-deadlock-and-gdb.md) 的层级锁，拿它记录每个线程当前持到哪一级了。按锁的等级组织获取顺序来预防死锁，它需要知道当前线程持有了哪些锁，这样的集合天然是每线程一份的，`thread_local` 咱们正好用它承接，等级的制定、方案的落地，咱们留到那一篇里完整展开。

## 练习：轮到您动手了

1. 把场景 2 的三个探针拆进三个 `.cpp` 文件分别编译链接，观察每个线程的构造行为会发生怎样的变化。有条件的话换 clang 编译同一份单文件程序再跑，比较两个编译器对推迟初始化的实现选择，把 `nm -C` 的输出放在一起对照。
2. 给 `Tag` 加一个记录构造序号的成员，在两个线程里验证同线程内构造与析构的逆序关系，再验证块作用域对象与命名空间作用域对象之间的相对顺序，预期它与 [basic.start.term] 第 4 段的排序一致。
3. 写一个每层函数直接读全局 `thread_local` 日志标记的版本，再把它改造成显式参数传递，给两个版本各写一个不启动任何线程的单测，对比哪个版本不需要提前布置环境，体会数据流可见性的差别。

## 下一步

每线程一份的状态，正当的去处咱们已经见过一处：[死锁与 gdb](./02-deadlock-and-gdb.md) 的层级锁，拿它记录每个线程当前持到哪一级了。同步的另一半是等待与通知，谓词等待的纪律，在 [condition_variable 与阻塞队列](./05-condition-variable-and-bounded-queue.md)里立了起来。线程入口只需要跑一次的初始化需求，对应的工具是 `std::call_once` 与 `std::once_flag`，完整的用法在 [同步原语工具箱](./06-sync-primitives-toolkit.md)。线程之间要传递的东西一旦超出标记与计数，内存序的正式规则，就在 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 里等着您。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    title="Storage class specifiers"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/language/storage_duration"
  />
  <ReferenceItem
    :id="2"
    title="Initialization"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/language/initialization"
  />
  <ReferenceItem
    :id="3"
    title="basic.start.dynamic"
    author="C++ Working Draft（eel.is 镜像）"
    url="https://eel.is/c++draft/basic.start.dynamic"
  />
  <ReferenceItem
    :id="4"
    title="basic.start.term"
    author="C++ Working Draft（eel.is 镜像）"
    url="https://eel.is/c++draft/basic.start.term"
  />
  <ReferenceItem
    :id="5"
    title="TLS"
    author="GCC Wiki"
    url="https://gcc.gnu.org/wiki/TLS"
  />
  <ReferenceItem
    :id="6"
    title="Williams, *C++ Concurrency in Action*, 2nd ed, Manning, 2019"
    :year="2019"
    url="https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition"
  />
</ReferenceCard>
