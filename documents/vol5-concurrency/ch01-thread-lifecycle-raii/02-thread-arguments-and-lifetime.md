---
title: "线程参数与生命周期陷阱"
chapter: 1
order: 2
description: "看清 decay-copy 的按值本质与 ref、move、成员函数入口的传参正路，再看 detach 引发的悬垂引用现场，ASan 亲手抓一次现行"
tags:
  - host
  - cpp-modern
  - intermediate
  - 内存管理
  - lambda
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 20
prerequisites:
  - "std::thread 基础"
related:
  - "线程所有权与 jthread/stop_token"
  - "数据竞争与 ThreadSanitizer 第一课"
---

# 线程参数与生命周期陷阱

[上一篇](./01-std-thread.md) 里咱们把 `std::thread` 的启动、join、detach 操练了一遍，文末还留了一个念想：detach 出去的线程到底能闯多大的祸。动身之前咱们得补一块地基：写 `std::thread t(f, x)` 的时候，那个 `x` 是怎么到新线程手里的？是别名还是副本？为什么有的参数要包一层 `std::ref`，`unique_ptr` 传进去编译器又为什么直接拒绝？这些疑问的背后站着同一个机制，它的名字叫 decay-copy（退化拷贝）。

## 参数去哪了：每个参数都被拷了一份

咱们从一个最直白的愿望开始：主线程里有一个 `int` 型的 `value`，咱们想让线程函数把它改成 42。这段代码在您眼里毫无毛病：

```cpp
#include <iostream>
#include <thread>

void update_value(int& x)
{
    x = 42;
}

int main()
{
    int value = 0;
    std::thread t(update_value, value);  // 编译不过
    t.join();
    std::cout << value << "\n";
    return 0;
}
```

咱们直接编译，GCC 16.2.1 的报错一句话就说到了点子上：

```text
error: static assertion failed: std::thread arguments must be
invocable after conversion to rvalues
```

报错的位置在标准库头文件里，随错误一起摆出来的还有实例化之后的类型 `_Invoker<std::tuple<void (*)(int&), int>>`。咱们把尖括号里的内容读出来，decay 之后的实参类型就在其中：`int&` 已经变成了 `int`。

标准的措辞能解答一切疑问。线程构造函数对新线程行为的定义是：新线程执行 `INVOKE(decay-copy(std::forward<F>(f)), decay-copy(std::forward<Args>(args))...)`。INVOKE 说的就是调用这一步，它是标准里的正式写法。C++23 起标准把措辞换成了 `std::invoke(auto(...), auto(...))`，教学上的叫法不变，咱们还是统一叫它 decay-copy。decay 的规则您在函数模板参数推导那里见过：引用被剥掉了，顶层的 `const` 与 `volatile` 被丢掉了，数组退化成了指针，函数退化成了函数指针。拿上面的例子对号入座：实参 `value` 的类型是 `int`，decay 之后得到的还是 `int`，线程内部存的是一份 `int` 副本。新线程里做调用的时候，咱们面对的是一个右值 `int`，而右值是绑不上 `int&` 形参的，`invocable after conversion to rvalues` 的静态断言就是在这儿拦下的。

cppreference 的 Notes 里还有一句大白话，值得咱们原文记下：`The arguments to the thread function are moved or copied by value.` 翻过来的意思就是，线程函数的参数要么被移动、要么被按值拷贝，唯独不会悄悄地变成引用。

### 拷贝发生在哪：当前线程，构造那一行

decay-copy 不是到了新线程才做的，它发生在您调用构造函数的当前线程。cppreference 的原话说，auto 产生的值在当前线程实体化（materialized），因此求值与拷贝/移动参数时抛出的任何异常，全都落在了当前线程的手上，新线程连启动的机会都没有。

这一设计上的实惠，咱们举个例子就能懂：假设某个参数类型的拷贝构造会抛异常，您把构造调用整个包在 try/catch 里，异常就能就地接住了。假如拷贝被推迟到了新线程里才做，异常就成了线程函数里的异常，而异常一旦逃出线程函数，咱们等来的是 `std::terminate()`。把拷贝放在了启动之前，等于把一类失败从不可收拾挪到了可收拾。

decay-copy 的设计动机也在这里露出全貌：让每个线程默认拥有自己的参数副本，默认是不共享的。共享是并发 bug 的温床，标准库选了默认隔离的路，想共享的人必须显式地写出来。怎么写？咱们下一节马上看。

## 引用参数：要共享，就写 std::ref

咱们真想让线程改到主线程里的变量，路倒是有一条：用 `std::ref` 把引用包起来。

```cpp
#include <functional>
#include <iostream>
#include <string>
#include <thread>

void append_suffix(std::string& str, const std::string& suffix)
{
    str += suffix;
}

int main()
{
    std::string message = "Hello";
    std::string suffix = " World";

    std::thread t(append_suffix, std::ref(message), std::cref(suffix));
    t.join();
    std::cout << message << "\n";  // Hello World
    return 0;
}
```

cppreference 的 Notes 对此有一句直接的指示：`If a reference argument needs to be passed to the thread function, it has to be wrapped (e.g., with std::ref or std::cref).` 咱们把机关找出来了，它就藏在 `reference_wrapper` 的身上：`std::ref(message)` 造出的是一个值语义的小对象，decay 剥不掉它的外壳，拷贝它拷的只是包装器，而 INVOKE 调用时又把它解包回了 `std::string&`。于是副本安全地到了新线程，解包之后指向的还是 `main` 里的 `message`，线程里改到的才是原件。

可天下没有白给的共享。`std::ref` 打破隔离默认的那一刻起，被引用对象的生命周期就成了您自己的责任：而线程还在跑，`message` 却提前死掉了怎么办？上面的例子靠的是 `join()` 兜底，join 还没返回的时候，`message` 一定还活得好好的。

> 目前为止，还犯不着用这样的方式非要绕过去，除非您完全清晰的可控对象生命周期！而且，几乎没有场景我们真的需要拷贝引用，大家基本上都是传递各种语义的指针的！

## move-only 参数：把所有权移进线程

天底下的类型并非个个都拷得动，`std::unique_ptr` 天生只肯 move：拷贝构造被删掉了。咱们把它交给线程的时候，写法上差的只是一个 `std::move`，编译器就会把您直接拒绝：

```cpp
#include <iostream>
#include <memory>
#include <thread>

void process_data(std::unique_ptr<int> data)
{
    std::cout << *data << "\n";
}

int main()
{
    auto p = std::make_unique<int>(42);
    std::thread t(process_data, p);  // 编译不过：拷贝被删除
    t.join();
    return 0;
}
```

GCC 报错的核心一行是 `no matching function for call to 'std::tuple<...>::tuple(...)'`——线程内部要拿一个 tuple 把函数指针和参数 decay-copy 存起来，`unique_ptr` 的拷贝被删了，您又只给了个左值，tuple 的构造函数找不出一个能对上的重载。修法是把所有权显式地交出去：

```cpp
std::thread t(process_data, std::move(p));  // OK：move 进线程
```

咱们补上 `std::move` 之后，decay-copy 走的便是移动构造，`p` 变成了 moved-from 状态（对 `unique_ptr` 而言等于空指针），本线程从这一行起就别再碰它了。

move-only 参数的默认与 decay-copy 是同一个思路：线程成了数据唯一的拥有者，再没有别人能同时碰到它了，data race 也就没有了落脚的地方。您拥有、您释放、不与人共享，这正是并发代码里最干净的关系。咱们的话题再往前挪一步：`std::thread` 对象自己也是 move-only 的公民，它的所有权怎么流转、怎么交给容器和 RAII 看管，正是 [下一篇](./03-thread-ownership-and-jthread.md) 的正题。

## 成员函数与函数对象做入口

线程的入口不一定非得是自由函数。lambda、重载了 `operator()` 的函数对象、成员函数指针，全都在 INVOKE 的管辖范围里。咱们来看成员函数的一种写法：

```cpp
#include <iostream>
#include <thread>

struct Greeter {
    void greet(const std::string& name)
    {
        std::cout << "greeter says: " << name << "\n";
    }
};

int main()
{
    Greeter g;
    std::thread t(&Greeter::greet, &g, "hello member fn");
    t.join();
    return 0;
}
```

头一个实参给的是成员函数指针，第二个实参给的是对象指针，cppreference 的官方示例 `std::thread t5(&foo::bar, &f)` 用的正是同样的形状，咱们看穿之后也就不觉得神秘了。

可有一件事您得盯紧：INVOKE 管的是怎么调用，decay-copy 管的是怎么存放，而 `&g` 也是一个参数——它是个裸指针，decay-copy 拷的是指针的值，而拷不走对象本身。函数对象同理：您把一个 functor 传给线程，线程跑的是它的拷贝，cppreference 官方例里线程内改 `b.n`、外面的 `b.n` 纹丝不动，这就是 decay-copy 在起作用了。可 functor 里要是捕获了引用或 `this`，拷出来的副本里存的照样是这些引用，悬垂的风险原样跟着进了线程。

真悬垂起来是什么样？咱们得看 detach 用错的例子才知道。

## detach 的下场：悬垂引用的现场

终于到了 [上一篇](./01-std-thread.md) 埋下的伏笔。咱们把最典型的错误写法摆出来，它短得像一个无害的小函数：

```cpp
#include <chrono>
#include <iostream>
#include <thread>

void faulty_function()
{
    // 一个看起来无害的局部变量
    int local_value = 42;

    std::thread t([&local_value] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Value: " << local_value << "\n";  // local_value 已死
    });
    t.detach();
}

int main()
{
    faulty_function();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "main done\n";
    return 0;
}
```

时间线走一遍您就看清死因了。等到 `faulty_function` 返回了，栈帧就拆除了，`local_value` 也就地销毁了。可 detach 出去的线程还活得好好的，而它在 100 毫秒后醒来，拿着 `[&local_value]` 存下的地址去读一块已经归还的栈内存。这便是咱们念叨的悬垂引用（dangling reference）：引用倒是还在，而它指向的对象已经没了。

更阴险的是，它在您机器上多半能跑。咱们在 `main` 里睡了 200 毫秒，线程 100 毫秒醒来的时候，那块已经归还的栈内存多半还没被重新用起来，读出来的就还是 42。露馅要等时机：系统负载一高、线程醒晚了，`main` 后续的调用早把那片栈翻过一遍，读出来的就成了垃圾的值。咱们运气再差一点，等来的就是当场崩溃。咱们在测试环境跑一万遍都可能太平，上线之后却挑客户环境的凌晨三点崩一次，并发 bug 的常见走向大多是这样。而症状又如此地不确定，咱们靠肉眼盯代码是守不住的，咱们得请工具。

### 请 ASan 抓一次现行

[data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md) 结尾交代过：AddressSanitizer（下文简称 ASan）管的是内存错，TSan 管的是 race，它的分工表在这一篇里正式见面，本篇下文就请 ASan 再抓一次悬垂引用给您看。现在就轮到这一步了，编译的时候只要多加一个旗标：

```bash
g++ -std=c++20 -fsanitize=address -g -pthread dangling.cpp -o dangling_asan
./dangling_asan
echo "exit=$?"
```

这里的 `dangling.cpp` 就是上面那段 `faulty_function`，您要是想找它，仓库里的对应文件是 `02_thread_arguments_and_lifetime.cpp`。

ASan 的报告长篇大论，可它的结构就只有三层，咱们拿三行就能读懂一份：

笔者实测：WSL2 Arch Linux、内核 6.18、g++ 16.2.1、AMD Ryzen 7 9700X。真实报告原文如下（地址与 pid 每次运行都会变，这份是其中一次）：

```text
$ ./dangling_asan
=================================================================
==207444==ERROR: AddressSanitizer: stack-use-after-return on address 0x7176073f01b0 at pc 0x562d25a68b39 bp 0x7176065feb60 sp 0x7176065feb50
READ of size 4 at 0x7176073f01b0 thread T1
    #0 0x562d25a68b38 in operator() /tmp/vol5-exp/ch01b/dangling.cpp:293
    #1 0x562d25a6dc95 in __invoke_impl<void, (anonymous namespace)::faulty_function()::<lambda()> > /usr/include/c++/16/bits/invoke.h:63
    #2 0x562d25a6d957 in __invoke<(anonymous namespace)::faulty_function()::<lambda()> > /usr/include/c++/16/bits/invoke.h:98
    #3 0x562d25a6d6d1 in _M_invoke<0> /usr/include/c++/16/bits/std_thread.h:303
    #4 0x562d25a6d55d in operator() /usr/include/c++/16/bits/std_thread.h:310
    #5 0x562d25a6d40d in _M_run /usr/include/c++/16/bits/std_thread.h:255
    #6 0x757609aea858  (/usr/lib/libstdc++.so.6+0xea858) (BuildId: 5b8d3de442de987b24d0e3679533068f2ae62497)
    #7 0x757609e61858  (/usr/lib/libasan.so.8+0x61858) (BuildId: b8a4241051a1621937fdc46e867ba7ecb56d96ea)
    #8 0x7576096980a1  (/usr/lib/libc.so.6+0x980a1) (BuildId: 503200d7fda94a5dc6058d7e0694e5d1dcb2e372)
    #9 0x75760972080b  (/usr/lib/libc.so.6+0x12080b) (BuildId: 503200d7fda94a5dc6058d7e0694e5d1dcb2e372)

Address 0x7176073f01b0 is located in stack of thread T0 at offset 48 in frame
    #0 0x562d25a68bce in faulty_function /tmp/vol5-exp/ch01b/dangling.cpp:287

  This frame has 3 object(s):
    [48, 52) 'local_value' (line 289) <== Memory access at offset 48 is inside this variable
    [64, 72) 't' (line 291)
    [96, 104) '<unknown>'
HINT: this may be a false positive if your program uses some custom stack unwind mechanism, swapcontext or vfork
      (longjmp and C++ exceptions *are* supported)
SUMMARY: AddressSanitizer: stack-use-after-return /tmp/vol5-exp/ch01b/dangling.cpp:293 in operator()
Shadow bytes around the buggy address:
  0x7176073eff00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7176073eff80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7176073f0000: f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f3 f3 f3 f3
  0x7176073f0080: f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f3 f3 f3 f3
  0x7176073f0100: f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f3 f3 f3 f3
=>0x7176073f0180: f5 f5 f5 f5 f5 f5[f5]f5 f5 f5 f5 f5 f5 f5 f5 f5
  0x7176073f0200: f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f3 f3 f3 f3
  0x7176073f0280: f1 f1 f1 f1 f1 f1 f8 f2 f8 f2 f2 f2 00 00 f3 f3
  0x7176073f0300: f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f3 f3 f3 f3
  0x7176073f0380: f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f3 f3 f3 f3
  0x7176073f0400: f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f5 f3 f3 f3 f3
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
Thread T1 created by T0 here:
    #0 0x757609f226f4 in pthread_create (/usr/lib/libasan.so.8+0x1226f4) (BuildId: b8a4241051a1621937fdc46e867ba7ecb56d96ea)
    #1 0x757609aea961 in std::thread::_M_start_thread(std::unique_ptr<std::thread::_State, std::default_delete<std::thread::_State> >, void (*)()) (/usr/lib/libstdc++.so.6+0xea961) (BuildId: 5b8d3de442de987b24d0e3679533068f2ae62497)
    #2 0x562d25a6af4a in thread<(anonymous namespace)::faulty_function()::<lambda()> > /usr/include/c++/16/bits/std_thread.h:175
    #3 0x562d25a68cd4 in faulty_function /tmp/vol5-exp/ch01b/dangling.cpp:294
    #4 0x562d25a697e3 in main /tmp/vol5-exp/ch01b/dangling.cpp:352
    #5 0x757609627780  (/usr/lib/libc.so.6+0x27780) (BuildId: 503200d7fda94a5dc6058d7e0694e5d1dcb2e372)
    #6 0x7576096278b8 in __libc_start_main (/usr/lib/libc.so.6+0x278b8) (BuildId: 503200d7fda94a5dc6058d7e0694e5d1dcb2e372)
    #7 0x562d25a65304 in _start (/tmp/vol5-exp/ch01b/dangling_asan+0x9304) (BuildId: 4083dec8575473efa26614d638805f9aeeb908d3)

==207444==ABORTING
exit=1
```


三行各自的职责都很分明，咱们一行行看。ERROR 行报的是错的种类：stack-use-after-return，栈上的对象在函数返回之后又被访问。第二段 `This frame has 3 object(s)` 直接点出了事的栈帧里住着谁，`[48, 52) 'local_value' (line 289)` 说的就是那块栈内存的地址区间，外加它的名字和它原来住在哪一行（行号对到仓库源文件 `02_thread_arguments_and_lifetime.cpp` 上就是 289）。第三段 created by 记下了线程的出生地：T1 是 T0 在 `_M_start_thread` 里创建的，创建时的调用栈就在报告里。哪个对象出了错、错在哪次访问、线程又是谁创建的，一份报告全交代了。进程在头一个错误处就停了，退出码给的是 1，ASan 的文档对此有一句原话：`AddressSanitizer exits on the first detected error. This is by design.`

有两个名字值得咱们当场分清。您可能在别处见过 use-after-scope（对象的作用域结束了还被访问），而它跟 use-after-return 是一对兄弟，本例里的函数已经返回，所以报告写的是后者。GCC 的 `-fsanitize=address` 默认把两类都管上了，咱们不用背旗标细节，认得报告里的名字就行。

### 分工表：谁管什么

TSan 的课咱们上过了，ASan 也见了真身，咱们把两件工具合在一张表里，各自的辖区就清楚了：

| 工具  | 编译旗标                   | 管什么                                                                                          | 不管什么                                                 |
| ----- | -------------------------- | ----------------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| ASan  | `-fsanitize=address -g`    | 内存错：越界、use-after-free、use-after-return、use-after-scope、double-free、泄漏（LSan 一体） | data race、逻辑错                                        |
| TSan  | `-fsanitize=thread -g -O2` | data race、锁序反转（预警，第 2 章 详讲）                                                       | 内存错（悬垂它不吭声）、已成真的死锁（不报，程序只挂住） |
| UBSan | `-fsanitize=undefined`     | 未定义行为：溢出、空指针解引用一类                                                              | race、堆悬垂                                             |

UBSan 咱们一句带过，本卷的排错主力是前两位。

组合上有一条硬约束您得记牢：ASan 与 TSan 是不能同时开的。GCC 文档写的是 `-fsanitize=address` `cannot be combined with -fsanitize=thread or -fsanitize=hwaddress`，TSan 一侧的原话是对称的，连 `-fsanitize=leak` 也一并列在了禁区里。编译器在编译期就把您拦下，连一份二进制都不给您。[data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md) 提过两件工具不能同开的限制，不过您翻过去也找不到报错的原文，那边只述了个大概。您想要两套工具，咱们就得分别构建出两份二进制，这里咱们只提醒、不展开。

真正有记忆点的读法，是咱们把同一份代码分别用两套工具各跑一遍：悬垂的代码在 ASan 底下吼声震天，您刚刚看过，而它跑到 TSan 底下就一声不吭了，因为悬垂不在它的辖区里。反过来咱们再看看，[data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md) 的计数器例子在 TSan 底下吼，在 ASan 底下倒是静悄悄的。工具没吭声的时候，不代表您的代码没问题：您的程序过了 TSan 不代表没有悬垂，过了 ASan 也不代表您就没有 race。

笔者实测：WSL2 Arch Linux、内核 6.18、g++ 16.2.1、AMD Ryzen 7 9700X。同一份悬垂代码换成 TSan 镜头（`g++ -std=c++20 -fsanitize=thread -g -O2 -pthread dangling.cpp -o dangling_tsan && ./dangling_tsan dangling`），它一声不吭：

```text
$ ./dangling_tsan dangling
Value: 42
main done
$ echo "exit=$?"
exit=0
```

反过来，[第 0 章的 race 例子](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)（仓库文件 `01_data_race.cpp`）换成 ASan 镜头跑，同样一声不吭，退出码 0，只把错误的计数器结果原样吐出来：

```text
$ g++ -std=c++20 -fsanitize=address -g -pthread race.cpp -o race_asan && ./race_asan
counter = 168256
$ echo "exit=$?"
exit=0
```

（`counter` 的具体值每次运行都不一样，这不是笔误，正是 race 本身。）两份安静加一份吼声，分工表的辖区划分就这么落了地。

开销的代价也得报给您。ASan 文档的口径是 `Typical slowdown introduced by AddressSanitizer is 2x.`，内存的占用还要涨得更多。所以它并不适合常驻在生产环境里，不过它在测试与排错里，用两倍上下的慢换一份逐字节的明察，这个代价咱们可以接受。真在乎数字，笔者在 ch00 的 race 例子上各跑三遍做了个小对拍：普通版（`-O2`）单次约 2.5 到 3.0 毫秒，ASan 版约 7.4 到 7.9 毫秒，慢了 2.7 倍上下，跟文档的口径对得上。

## this 也会悬垂：对象没能活过线程

面向对象版的事故就藏在后台任务类里。咱们在成员函数里起线程、让 lambda 捕获 `this`，是最常见的写法，而事故偏偏藏得最深：

```cpp
#include <chrono>
#include <iostream>
#include <thread>

class BackgroundWorker {
public:
    void start()
    {
        running_ = true;
        std::thread t([this] {
            while (running_) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
        t.detach();
    }

    void stop() { running_ = false; }

private:
    bool running_ = false;
};

int main()
{
    {
        BackgroundWorker worker;
        worker.start();
    }  // worker 在这里析构，线程还在跑
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
```

咱们看时间线：`worker` 离开了作用域，成员 `running_` 跟着一起没了。detach 出去的线程还在循环里读 `running_`，它通过捕获的 `this` 摸进一块已经拆除的内存。病根与上一个悬垂的例子是同一个：引用（这里是 `this` 指针）指向的对象，活不过使用它的线程。

咱们再看一处新语法。老代码里常见 `[=]` 一网打尽的捕获写法，C++20 起它隐式捕获 `this` 的用法被废弃了（P0806R2 提的案），GCC 的警告写得明白：

```text
warning: implicit capture of 'this' via '[=]' is deprecated in
C++20 [-Wdeprecated]
note: add explicit 'this' or '*this' capture
```

替换写法有两种：`[=, this]` 捕获的还是指针，语义跟从前的完全一样，也照样藏不住悬垂的风险。`[=, *this]` 捕获的是整个对象的副本，线程里改的是副本成员，动不到外部的原件。您品一品 `*this` 的本质：又是拷贝隔离生命周期，decay-copy 的思路在捕获列表里又应验了一次。

### 看似修好的坏例：atomic 修不了生命周期

上面的坏例身上其实叠着两处病。头一处的病在生命周期，另一处您在 [data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md) 已经给过名字：`running_` 是主线程与工作线程都在碰的普通 bool，而读写之间没有任何同步，妥妥的 data race 现行。于是一个非常顺手的修复出现了，咱们把 `running_` 换成 `std::atomic<bool>`，咱们只需加一个头文件、改一个类型：

```cpp
#include <atomic>

private:
    std::atomic<bool> running_ = false;
```

race 这一项确实修好了。atomic 管的就是可见性与撕裂：`stop()` 写入的 false，工作线程下一次读的时候一定能看见，也不会读到被撕成两半的值。可该崩的程序照样崩，咱们一步步看一遍死因：析构的时候 `stop()` 把 false 写进 `running_`，紧接着成员 `running_` 自己也被销毁了，而它也是对象的一部分。而工作线程下一圈循环再来读它，读到的就是一块已死内存上的原子量。atomic 给的保证是 `这个对象上的操作怎么交错都正确`，而它从头到尾没有承诺过 `这个对象活着`。对象的本体已经悬垂，而操作再原子，换来的也是未定义行为。

所以这样的修复修的是症状，而病根在生命周期。真正的出路只有两条：让被引用的数据活过线程，或者干脆别 detach、老老实实地 join。至于 atomic 真正的领地，也就是可见性的建立、内存序的配对，咱们留到 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 正式开讲，本篇咱们不越界。

### [&] 批量捕获的隐患

咱们快看下一个例子：它就是头一个例子的放大版。批量起线程的循环里，一个 `[&]` 把 `input`、`output` 连同别的局部变量全按引用收进了闭包：

```cpp
void parallel_square_wrong(const std::vector<int>& input,
                           std::vector<int>& output)
{
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < input.size(); ++i) {
        threads.emplace_back([&, i] {  // input、output 是引用捕获
            output[i] = input[i] * input[i];
        });
    }
    for (auto& t : threads) {
        t.join();
    }
}
```

眼下这个版本恰好是能活的：函数返回前所有线程都被 join 了，被引用的 `input`、`output` 一定还活着。它的脆弱之处在于，安全全押在了末尾那几行 join 上。谁要是顺手把 join 改成了 detach，理由多半是觉得结果不用等了，整个函数立刻就变成了批量悬垂。而 `[&]` 还有一处暗伤：它捕获的是所有局部变量，今天您检视过捕获列表没有问题，明天哪位同事往函数里加了一个局部变量，它自动也被引用捕获了，审查的时候就多了一块看不到的地方。所以咱们写多线程代码的时候，要么把捕获列表写成显式的（`[&input, &output, i]`），要么干脆走参数传递的路子，出了事咱们才有处可查。

## 修复的思路：让数据活过线程

上面几处翻车的病根是同一个，咱们把修法归成三类，一类一类地看。

### 修法一：值捕获，让线程自带副本

咱们最干净的解法是让每个线程自带干粮，而这正好是 decay-copy 的默认：

```cpp
void safe_version()
{
    std::string message = "Hello from parent";

    std::thread t([message] {  // 值捕获：闭包里是 message 的副本
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Thread sees: " << message << "\n";
    });
    t.detach();  // 随便 detach，线程拿的是自己的副本
}
```

咱们把 `[&message]` 换成 `[message]`，lambda 闭包里存的是副本，线程构造的时候又把闭包整个拷走，detach 之后外面的生死也就与它无关了。拷贝的开销在小对象、小字符串上可以忽略。大对象怎么办？卷首立过的头一条原则在这儿正好用上：`先正确性，再性能`。拷贝换来的隔离是最容易审计的正确性，等正确性站稳了，再回头来谈省不省拷贝的事，省的路子就在下一个修法里。

### 修法二：shared_ptr 延长生命周期

在数据拷不动、又确实要共享的场合，咱们把生命周期交给引用计数。咱们还是拿 BackgroundWorker 开刀，把它的成员从 `atomic<bool>` 换成 `shared_ptr<atomic<bool>>`：

```cpp
#include <memory>

class BackgroundWorker {
public:
    BackgroundWorker()
        : running_(std::make_shared<std::atomic<bool>>(false)) {}

    void start()
    {
        running_->store(true);
        auto running = running_;  // 拷一份 shared_ptr，计数 +1
        std::thread t([running] {  // 闭包按值持有 shared_ptr
            while (running->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            std::cout << "worker exiting cleanly\n";
        });
        t.detach();
    }

    void stop() { running_->store(false); }

private:
    std::shared_ptr<std::atomic<bool>> running_;
};
```

咱们把收场走一遍：析构的时候 `stop()` 写入 false，`running_` 析构、计数从 2 降到了 1，可堆上的 `atomic<bool>` 还活得好好的，因为线程闭包里的 `shared_ptr` 还攥着最后一个引用。线程一旦看见了 false，就退出了循环，闭包也跟着销毁了，计数也就归零了，`atomic<bool>` 到了现在才被释放。数据活过了线程，一切都在按部就班地进行。

有一句注脚咱们必须配在这里。`shared_ptr` 控制块里的引用计数，增减是原子的，多个线程各持一份拷贝、各自析构的时候，计数就不会乱了，cppreference 的注脚大意就是控制块线程安全。不过这样的安全只罩得住计数，而罩不住被指对象的内容：两个线程各拿一份 `shared_ptr<vector<int>>`、并发去读写同一个 vector 的时候，那照样就是 data race 了。生命周期的事情归 `shared_ptr` 管，内容的同步还得咱们自己想办法，[第 3 章](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 那边接着跟您细说。

### 修法三：不 detach，join 收尾

前两个修法都在延长数据的命，第三个修法换了个方向：让线程的寿命别超过对象。咱们让 BackgroundWorker 把线程对象收成成员，析构的时候做两件事：写入停止的标志，然后安心地等线程结束：

```cpp
class BackgroundWorker {
public:
    void start()
    {
        running_ = true;
        thread_ = std::thread([this] {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            std::cout << "worker exiting cleanly\n";
        });
    }

    ~BackgroundWorker()
    {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();  // join 返回，线程一定已结束
        }
    }

private:
    std::atomic<bool> running_ = false;  // 这里用 atomic 是为可见性，见上文
    std::thread thread_;
};
```

join 返回的那一刻，线程函数已经执行完毕了，成员的销毁排在了它的后面，`this` 从头到尾没有悬垂的机会。您会发现 `[this]` 在这里从隐患变成了合理写法：捕获方式倒是一个字没改，变的是背后的生命周期关系。这里的 `running_` 用了 atomic，图的是停止标志的可见性，它管不了的事，上文“看似修好的坏例”一节刚刚讲过。

三类修法之外笔者还有一句大实话。Core Guidelines 的 CP.26 把话挑明了，标题写的就是 Don't `detach()` a thread。而实践中，真的不在乎线程什么时候结束的场景远比想象中少，多数 detach 的真实动机是没想清楚怎么收尾。而 CP.24 又从另一个角度敲了敲边鼓：把 thread 想成一个全局的容器，它的生命周期问题就得按全局对象的严格标准来对待。真到了必须 detach 的场合，咱们就从三类修法里挑一条，把数据的生命续上。

而手动在析构里写 `if (joinable()) join()` 这件事本身，正是 RAII 能替咱们自动化掉的活。怎么自动化、C++20 的 `std::jthread` 又把这件事做到了什么程度，咱们留给 [下一篇](./03-thread-ownership-and-jthread.md) 专门讲——生命周期问题的出路，是给线程找一个明确的拥有者。

## 练习：轮到您动手了

### 练习 1：ref 修正与 ASan 抓现行

把本篇开头的 `update_value` 程序补完整：用 `std::ref` 让线程真的改到 `value`，join 之后您再打印验证。然后您再故意犯一次错，您把 `faulty_function` 原样复制一份、用 `-fsanitize=address -g` 编译运行，对着报告咱们找三样东西：错误的种类，出错的栈对象的名字与行号，线程创建时的调用栈。您把报告原文存下来，跟您读文章时的预期对一遍。

### 练习 2：[=] 弃用与两种修法

请您写一个小类，在它的成员函数里起线程，lambda 用 `[=]` 隐式地捕获 `this`，编译并记下 GCC 的弃用警告。然后您分别换成 `[=, this]` 与 `[=, *this]` 各编译一遍：前者的警告消失了，可 `[=, this]` 用的仍是指针捕获，后者连对象都拷进了闭包。请您在线程里改一个成员变量的值，join 之后您再打印原件，验证 `*this` 版本的改动动不到外面。

### 练习 3：这段代码悬没悬垂

下面这段代码是作者声称修复完毕的版本，咱们来挑挑毛病：

```cpp
class Sensor {
public:
    void start()
    {
        std::thread t([this] {
            while (!stop_requested_.load()) {
                read_and_log();
            }
        });
        t.detach();
    }

    ~Sensor() { stop_requested_.store(true); }

private:
    void read_and_log() { /* ... */ }
    std::atomic<bool> stop_requested_ = false;
};
```

请您判断：等对象析构了之后，工作线程对 `stop_requested_` 与 `read_and_log()` 的访问是什么行为？atomic 有没有把问题修掉？真正的修复有哪些选项？判断的依据请您用一句话说出来，它就落在 `可见性` 与 `生命周期` 两个词的差别里。

## 下一步

本篇把参数的机制与生命周期的病根都过了一遍。往下走咱们有三个方向：[线程所有权与 jthread/stop_token](./03-thread-ownership-and-jthread.md) 而它就在隔壁等着您，把给线程找拥有者的正路走到头，顺手废掉手动的 join。您可以回头翻 [data race 与 TSan](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)，把两套工具的对照记牢。等您走到 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md)，atomic 与内存序的旧事会跟本篇的坏例正式对上。动手量更大的活儿在 [练习体系](../exercises/) 里，那边的 bonus 会请您用 `jthread` 把手写的收尾改造一遍。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    title="std::thread constructor"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/thread/thread/thread"
  />
  <ReferenceItem
    :id="2"
    title="std::ref, std::cref"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/utility/functional/ref"
  />
  <ReferenceItem
    :id="3"
    title="AddressSanitizer"
    author="Clang Compiler 文档"
    url="https://clang.llvm.org/docs/AddressSanitizer.html"
  />
  <ReferenceItem
    :id="4"
    title="GCC Instrumentation Options"
    url="https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html"
    chapter="sanitizer 互斥条款的出处"
  />
  <ReferenceItem
    :id="5"
    title="P0806R2: Deprecate implicit capture of this via [=]"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0806r2.html"
    author="WG21"
    :year="2018"
  />
  <ReferenceItem
    :id="6"
    title="C++ Core Guidelines CP.24: Think of a thread as a global container"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp24-think-of-a-thread-as-a-global-container"
  />
  <ReferenceItem
    :id="7"
    title="C++ Core Guidelines CP.26: Don't detach() a thread"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp26-dont-detach-a-thread"
  />
  <ReferenceItem
    :id="8"
    title="Williams, *C++ Concurrency in Action*, 2nd ed, Manning, 2019"
    :year="2019"
    url="https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition"
  />
</ReferenceCard>
