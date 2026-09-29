---
title: "数据竞争与 ThreadSanitizer 第一课"
chapter: 0
order: 2
description: "用一个两线程计数器亲眼看见 data race，逐字读标准的定义与 UB 判决，再用 ThreadSanitizer 把看不见的竞争变成一份能读的报告"
tags:
  - host
  - cpp-modern
  - beginner
  - atomic
  - mutex
difficulty: beginner
platform: host
cpp_standard: [11]
reading_time_minutes: 22
prerequisites:
  - "为什么并发：世界观与卷地图"
related:
  - "mutex 与 RAII 守卫"
  - "原子操作与 happens-before"
---

# 数据竞争与 ThreadSanitizer 第一课

咱们在 [为什么并发：世界观与卷地图](./01-why-concurrency.md) 里把并发的收益和代价摆平了，卷首还立了三条原则，头一条讲的就是 `先正确性，再性能`。这一篇咱们就对付正确性里最难缠的问题：咱们叫它 data race，中文里叫的是数据竞争。它难缠的地方，是它看不见——程序不一定崩给您看，跑出来的结果还常常是对的，错只在某个特定的交错里发生，而那个交错未必来。

所以本篇还有第二件事：把全卷的第一件工具 ThreadSanitizer（下文简称 TSan）请出来，让您在本机上亲眼看见一次 race。

路线咱们也交代清楚。咱们会写一个很小的坏程序，把标准对 data race 的定义逐字读一遍，弄明白标准为什么把它判成未定义行为，再看一眼修复的样子。接着 TSan 就登场了，走一个从编译到读报告的完整来回。死锁的那一类问题，咱们在结尾让他们亮个相就收，深讲各有专门的篇目。

本篇的代码、命令和现象，全部可以在一台普通的 Linux 机器或 WSL2 上复现，需要的只有 g++ 和几十行源码。您跟着敲一遍，比光看强了不少——race，咱们眼见为实。

## 一个看起来无害的计数器

咱们的程序小，可以整篇地贴出来：全局放一个 int，您让两个线程各给它加十万次，join 完了打印。笔者知道不少从单片机转过来的朋友习惯直接丢一个全局变量上去，咱们这回就这么写，看看会发生什么。

```cpp
#include <iostream>
#include <thread>

int counter = 0;  // 普通 int：非 atomic，也没有锁保护

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        ++counter;              // 读 -> 加 -> 写，三步
    }
}

int main()
{
    std::thread t1(increment, 100000);
    std::thread t2(increment, 100000);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << "\n";
    return 0;
}
```

咱们来编译运行，普普通通的两条命令：

```bash
g++ -O2 -pthread 01_data_race.cpp -o race
./race
```

您猜打印多少？两边的循环加起来是二十万次自增，咱们期望的自然是 200000。笔者在 20 核的 WSL2 上（GCC 16.2.1）用 -O2 编译，连着跑了五次，五次跑出来的全是 200000：一个数都没丢。

咱们的程序看着健康得很，健康到您可以放心地把它忘了。

咱们把优化关掉再试一把：命令一字不改，只把 -O2 换成了 -O0。数字立刻就垮了：五次跑出 116007、102861、100000、162707、100000，其中两回是不多不少的 100000 整——跟期望的二十万比，您跑得最好的一回也只摸到十六万出头。

<!-- 实验回填：-O2 与 -O0 两版各跑十次的完整输出记录（注明机器与 GCC 版本） -->

同一份源代码：咱们只换了优化等级，结果就从 200000 掉到了 100000 上下。您要是归因成 -O0 暴露了 bug、-O2 运气好，方向只对了一部分。

真正值得停下来看的是：-O2 生成的代码里，这个满身是 race 的程序为什么看起来毫发无伤？咱们拿 objdump 看一眼 increment 的函数体，答案就摆在咱们眼前：

```text
$ objdump -d --no-show-raw-insn race | grep -A 4 '_Z9incrementi>:'
00000000000013d0 <_Z9incrementi>:
  13d0:	test   %edi,%edi
  13d2:	jle    13da <_Z9incrementi+0xa>
  13d4:	add    %edi,0x2dba(%rip)        # 4194 <counter>
  13da:	ret
```

您也可以照着跑：咱们用 objdump -d 反汇编，--no-show-raw-insn 把机器码的字节藏掉。C++ 的函数名进了二进制会换上修饰名（mangled name），increment(int) 在符号表里换上的名字是 _Z9incrementi，咱们 grep 的时候就按这个名字抓。地址和偏移每台机器都会不一样，指令的形状是一样的。

咱们要找的十万次循环，没了。编译器把整个 for 折叠成了一条 add 指令：直接往 counter 上加 100000，edi 这个寄存器里存的就是 times。

为什么说它天经地义？您想，在单线程的语义下，这个变换天衣无缝：加一百次是加了 100，加一万次是加了 10000，归纳后的结果就是加 times，编译器做它是天经地义的。可两个线程各做一次非原子的加法，race 原封不动地留在原处，只是冲突窗口从十万条指令缩成了一条指令，碰上的概率跟着缩水，结果就几乎次次都对了。

咱们反过来看，-O0 下老老实实循环十万次的版本，它的窗口大得很，您看它丢更新，丢得理直气壮——那两回的 100000，是两边完全同步走出来的极限值，每一轮里两个人都读到了同一个数，也写回了同一个数，十万轮下来总数只涨了十万。

这里有一件事请您放在心上：结果对了，也不等于程序就对了。咱们判断对错，靠的是标准说话，不靠某一次运行的输出。

咱们加 printf 调试也没用——打印本身就改变时序，bug 说不定就被您那一行输出吓跑了，老江湖给这类抓不住的 bug 起过一个名字：Heisenbug。

肉眼是看不住它的，运气也是靠不住的，咱们需要一个不改变程序语义的观察者。不过在拿到它之前，咱们得弄清楚要观察的到底是什么，所以这就去读标准。

## 标准怎么说：data race 的定义

咱们在读定义之前，把手里的疑问列清楚：race 到底指的是什么？标准凭什么一句话就判了 UB？判了 UB，咱们写代码的人该怎么办？咱们带着问题去读，条款也就不枯燥了。

data race 是 C++ 标准里的正式术语，不是咱们自己造的词，它的定义住在 [intro.races] 条款里。

咱们把原文分成两截抄在这里，每一截都值得您逐字读：`The execution of a program contains a data race if it contains two potentially concurrent conflicting actions, at least one of which is not atomic, and neither happens before the other`（一个程序的执行里，出现了两个潜在并发的冲突动作，其中至少一个不是原子的，而且谁也不 happens-before 谁）。定义的原文还拖着半句例外，咱们抄的时候把它折掉了：那是给信号处理函数留的窄门，本篇的舞台是线程，您用不上。另一截是紧跟在后面的判决：“Any such data race results in undefined behavior”（任何这样的 data race，招来的都是未定义行为）。

一句话里压着的术语有三个，咱们拿上面的计数器当标本，一个一个地对号。

头一个要看的词是 conflicting（冲突）。cppreference 展开的判据是：两个表达式访问的是同一个内存位置，而且至少有一个是写。咱们程序里两个线程都在写 counter 的四个字节：写对写，冲突就成立了。读对写、写对写的情况也都算，只有两个纯读不算数——两边都只看而不动手、谁也碍不着谁。

第二个要看的词是 potentially concurrent（潜在并发），它指的是不同线程里的动作，或者同一线程与它的信号处理函数之间的动作。咱们只关心前者：咱们的两个 increment 就跑在两条线程上，要件也成立了。

第三个词 happens-before 对咱们最陌生，咱们只取一个直觉的版本：A happens-before B，粗略地说就是 A 的效果对 B 可见，而且 A 排在 B 前面，次序是定死的、也不许翻案。这样定下的次序由同步动作建立。

咱们看 join：它就是一个常见的来源，t1.join() 返回了以后，main 线程再去读它的 counter，这一读与线程里的写就被隔开了，也就不成 race 了。线程创建也是来源：构造 std::thread 之前发生的写，对新线程也是可见的。

咱们程序真正的 race 出在两个线程的 ++ 之间，那里没有任何的同步动作，所以谁也不等谁。happens-before 有精确的数学定义，它的正源在 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 那一篇，咱们本篇到直觉为止，不往深里去了。

> 咱们把定义折叠成一张检查单，往后咱们每怀疑一处 race，就拿它过一遍：同一个内存位置吗？至少一个写吗？有 happens-before 隔开吗？三条全中了，data race 也就成立了。差一条没中的，那才谈得上没 race 了。

拿咱们的计数器走一遍：counter 用的是同一个位置，两边的访问都是写，中间没有锁的隔离，也没有哪个 join 隔开了它们。三条全中了，race 也就成立了，咱们判得干脆。

咱们把三个要件对完号，剩下的就是那句判决：“Any such data race results in undefined behavior”（任何这样的 data race，招来的都是未定义行为）。

咱们平时嘴里的结果不确定，在标准这里已经算客气话——UB 的真实待遇，咱们下一节看。

## 为什么定成未定义行为

有朋友可能会问：标准为什么不干脆给个说法，比如 racy 的读最多拿到旧值，让咱们心里有个预期？

Hans Boehm 是 C++ 内存模型的主要设计者之一，他专门写过一页的文章，回答的正是这个问题。他的论证，咱们可以用自家机器上刚发生的事原样复述一遍。

咱们刚才看见的，就是把十万次 ++ 折叠成一条 add 的变换。这个变换在单线程的语义下挑不出任何毛病：把一串加法合成一次加法，算出来的结果分毫不差。可它顺手把 racy 执行的结果改了个面目全非。

您想，标准若给 data race 规定任何确定的语义，哪怕温和到咱们熟悉的最多拿到旧值，这类变换就得逐个地审查了，编译器的手脚就被捆住了。标准选了另一条路：racy 的程序没有语义，所以编译器怎么优化都不算违约，责任全部落在写代码的人肩上。

咱们再看 Boehm 在那页文章里的立场，他的话说得很直白：“Data races among ordinary variables are a bug”，翻成中文的意思是：普通变量上的 data race 就是 bug。

UB 的许可范围也比读到旧值宽得多：同一个变量读到撕成两半的值、写被重排到看不懂的位置、一个分支整个被优化没了，这些全都在许可的范围之内。咱们用的 x86 机器上，对齐的 int 读写本身不撕，可换了平台、换了编译器，没人给您这个担保。

还有人琢磨过良性 race 的说法，觉得咱们有些 race 反正看起来没事，那个 -O2 下次次全对的计数器就是活例子。恰恰是这样的没事，UB 才是最阴险的：它不欠您一个崩溃，等真崩的时候也不打招呼。

这话听着是冷酷了点，其实是对写代码的人的一种尊重：标准不糊弄您说大概没事，它把边界给您画清楚，把判定的工具也交给您。本篇剩下的篇幅，就是带您把工具拿到手。

顺着刚才的线索，还有一块基石值得咱们认个脸熟：只要咱们的程序没有 data race，原子操作用的又都是默认内存序，它的行为就有一个统一的顺序一致语义兜底。

这样一个保证是有名字的，它的大名叫 DRF-SC，也就是无 race 程序的顺序一致性。它的正式讲法和来龙去脉，咱们放到 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 那一篇里讲，您现在只需要认下一件事：无 race 的世界里有秩序，有 race 的世界里没有标准。

## 修复长什么样

修复的完整体系——锁族、条件变量、原子、内存序——占整整两章。咱们读到这里，counter 的病根咱们已经看明白了：两个非原子的写之间，缺的就是次序。修法也就顺着病根来：要么给这对访问建立确定的次序，要么让写本身不可分割。锁的版本改动最小，咱们看一眼：

```cpp
#include <iostream>
#include <mutex>
#include <thread>

int counter = 0;
std::mutex counter_mtx;

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        std::lock_guard<std::mutex> lock(counter_mtx);
        ++counter;              // 两个线程的 ++ 从此排上了队
    }
}
// main 与打印部分原样不动
```

一把 mutex 看住了 counter 的全部访问，两个线程的 ++ 从此排上了队，race 也就消失了。咱们按定义说：锁的拿与放建立了 happens-before，前一个持有者的写，对下一个持有者是可见的。

lock_guard 在这里咱们当黑盒用，它替咱们管拿锁放锁、异常路径上也不漏放，RAII 的完整讲法在 [mutex 与 RAII 守卫](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md) 里，那是 ch02 的正源。

咱们另一条路走的是把 counter 换成 std::atomic<int>，让写本身成了不可分割的一步：

```cpp
#include <atomic>

std::atomic<int> counter{0};

void increment_atomic(int times)
{
    for (int i = 0; i < times; ++i) {
        counter.fetch_add(1);     // 一条原子读改写，顶掉读-加-写三步
    }
}
```

fetch_add 用一条原子读改写顶掉了读-加-写三步，单变量的计数场景往往比锁更轻。不过它还能带一个内存序实参，memory_order_relaxed 就是其中的一种，这一片咱们只能说水很深，它们的正源在 [原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 和 [内存序](../ch03-atomic-memory-model/03-memory-ordering.md) 两篇，咱们本篇一个字都不展开。

挑选的口味咱们也好给：单变量计数用 atomic，要协同变化的变量一多，咱们就用 mutex，把整段的临界区一起看住。

修没修好？口说无凭，咱们让工具来验。

## TSan：把看不见的 race 变成报告

接下来咱们按实战的顺序，把 TSan 从名词变成您手里的东西。

### 编译与运行

咱们要启用 TSan，官方 wiki 一句话就说完了，咱们编译和链接都带上 -fsanitize=thread。g++ 的一条命令就把两头全占了：

```bash
g++ -fsanitize=thread -g -O2 -pthread 01_data_race.cpp -o race_tsan
./race_tsan
echo $?     # 报 data race 时，默认退出码是 66
```

咱们把这几个 flag 挨个过一遍。-fsanitize=thread 是本体：编译器在程序里的每个内存访问前后插桩，运行时由 TSan 的运行库逐对核对。

-g 让报告的调用栈带上源码位置，少了它，您看到的就只是一串地址。-pthread 咱们也次次都带上，libstdc++ 的多线程程序都需要。

最有说头的是 -O2：有些老教程叮嘱开 TSan 就别开优化，说调用栈会没法读了。wiki 的现行口径恰好反着写：“To get a reasonable performance add -O2”（想要像样的性能就加 -O2），“Use -g to get file names and line numbers in the warning messages”（加了 -g 之后，警告里就带上了文件名和行号）。笔者的本机冒烟里，-O2 加 -g 的栈解析得好好的，咱们按 wiki 来。

插桩为什么不算改变语义？TSan 加的是核对，而加的不是次序：它记录每次访问、检查配对，不插入等待、也不推迟谁的执行。所以它看得见 race，却治不了 race，治病的还是您手里的锁和原子。

咱们跑起来看看是什么阵仗？程序照常打印 counter，数字还是那个看起来健康的值，紧接着 TSan 把一份报告刷到了 stderr，进程以退出码 66 收了场。

66 不是崩溃的信号，它代表的是 TSan 默认的失败退出码，咱们搁持续集成（CI）里拿 `$?` 直接判定，一行脚本的事。

笔者做过的本机冒烟：GCC 16.2.1、WSL2 内核 6.18，这命令您拿来就能用，两线程十万次自增的程序一次就报出了 race，退出码也落在了 66 上。

CMake 工程里用的也是同一个口径，咱们把编译、链接两头都配上：

```cmake
add_executable(race_tsan 01_data_race.cpp)
target_compile_options(race_tsan PRIVATE -fsanitize=thread -g)
target_link_options(race_tsan PRIVATE -fsanitize=thread -pthread)
```

<!-- 实验回填：TSan 报告原文（WARNING、冲突双方调用栈、Location、线程创建栈、SUMMARY） -->

### 报告怎么读：三个问题

报告的原文，咱们等实验回填时再见真容，结构的部分现在就可以交底，因为它长年都是固定的。

一行 WARNING 报的是事故，冲突的双方各占一段，打头的要么是 Write of size 4、要么是 Read of size 4，各配了一份调用栈，一行 Location 指明的是内存位置，随后的两段是线程创建栈，它们打头用的都是 created by main thread at，末尾的一行 SUMMARY 压轴。咱们读报告的时候，笔者的习惯是拿三个问题过一遍。

头一个问题：冲突的是哪两处访问？咱们看报告把双方成对摆出来，一段是肇事的 Write（或 Read），而另一段是 Previous write（或 Previous read），各自都标上了文件名和行号。

Previous 这个词还顺手把两段的前后排好了：标了 Previous 的那段，在时间上落在了前面。咱们这个程序里，两段的行号都落在 increment 里那行 ++counter 上，写对写的冲突，与咱们拿定义对出来的判词一模一样。

第二个问题：是哪两个线程？每段访问都缀着所属线程的编号，咱们叫它们 T1、T2，报告末尾还给出每个线程的创建点调用栈。创建栈打头用的也是 created by main thread at，说的就是线程的出身。往下咱们看到的，是 pthread_create 和 std::thread 内部的帧，您要找的 main 里那两行 std::thread 构造，咱们把优化开起来之后，它就未必留在栈上了，冒烟那次它就被裁掉了，看到出身也就够了。这一问在线程多的时候特别有用：五个线程抢一个变量，您总得知道是哪两个搅在一起。

第三个问题：两段访问之间缺了什么？咱们替它答：缺的是同步——没有锁的隔离、没有原子的帮忙，也没有 join 建立的次序隔开它们。data race 的定义反着用，就是 TSan 的判据。三个问题答完了，一份报告也就读透了：谁、在哪、中间缺什么。

读栈的时候咱们还有个小窍门。咱们这次的冒烟里，头一帧干干净净地就是 increment，再往下数了几帧，invoke.h、std_thread.h 的 _M_run 之类的帧就冒出来了。

那些是 std::thread 内部替咱们跑函数的机制帧，咱们不认识它们也不用慌，读报告时跳了过去，认自己工程路径的那几帧就行。窗口小、lambda 套得深的时候，机制帧也就跟着多了，这个窍门咱们往后用得上。

### 修复版复跑

咱们给坏程序读完了报告，咱们再把修复版也过一遍 TSan，把一个完整的来回走下来：

```bash
g++ -fsanitize=thread -g -O2 -pthread 02_data_race_mutex.cpp -o race_fixed_tsan
./race_fixed_tsan
echo $?     # 0：报告干净
```

counter 老老实实地打出了 200000，TSan 一个字没说、退出码也从 66 回到了 0，这一来一回就算走完了。代码仓里给本篇的四个示例各配了 CMake 目标：`01_data_race.cpp` 对应的是坏计数器，`02_data_race_mutex.cpp` 对应的是锁修复版，`03_check_then_act.cpp` 对应的是后面 check-then-act 一节的向量例，`04_deadlock_reorder.cpp` 对应的是结尾的死锁例，您拉下来就能复跑本篇的每一步。

### 它凭什么判 race

咱们再多问一层：TSan 凭什么下这个判断，判据又是从哪来的？现行的版本叫 TSan v2，GCC 和 Clang 附带的就是它，它跑的是纯 happens-before 算法。

咱们可以想象，每个线程的身上都挂着一座向量时钟，同步事件——拿锁、放锁、线程创建、join 这一类——会推进各自的时钟，而在时钟之间建立起偏序。每次的内存访问都被记进影子内存，咱们按 8 字节一格分，每格里存的只是寥寥的几条历史。两座时钟对不上偏序的时候，访问又起了冲突，它这才报了案。

向量时钟的名字听着玄，直觉却是简单的：每座时钟记的是自家线程看过的世界，同步事件就是两个世界的碰头对表。一旦对上了表，双方就互相看得见了，没对过表的两座时钟，彼此的访问就按无序处理——咱们宁可多查、也不放过。

拿咱们的两个版本各走一遍，您就看明白它在干什么了。坏的那个版本里，两个线程从出生到结束都没遇上任何的同步事件，所以两座时钟永远对不上，于是每一对指向 counter 的冲突访问都成了 race。修复的版本里，t1 放锁、t2 拿锁的一来一回把两座时钟对上了——t1 在锁里的写，对 t2 在锁里的读也是可见的，次序建立了，race 也不再成立了。TSan 判的正是定义里那个 happens-before，一点多余的东西都没夹带。

这里笔者要专门纠正一个流传很广的旧说法，咱们会读到不少老资料说，TSan 走的是混合算法，也就是 happens-before 加上了锁集分析（lockset）。那其实是早期版本的旧黄历了，而现行的 v2 不搞混合，用的就是纯 happens-before 加向量时钟。

锁集那一路的思路，是两个线程没持有共同的锁就报案，听着倒是直观，误报的概率却高得很，它是更早一代工具的路线。您查资料的时候留个心眼：讲算法的资料，以官方 wiki 的 Algorithm 页为准。

您看到这儿，happens-before 这个词应该已经眼熟了，它就是标准定义里的那个词。TSan 的向量时钟，就是 happens-before 理论落在机器上的实现：标准用数学语言写下的次序关系，TSan 用时钟一格一格地算出来。这两者怎样严丝合缝地对上，[原子操作与 happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) 那篇里有专门的一页讲它，咱们到 ch03 再会。

### 边界、开销与搭档

再漂亮的算法，工程上的边界也是有的，咱们把两头都交代清楚。

一头是它报的都作数：只要整个程序都插着桩编译，TSan 报出来的每一份报告，都对应这次执行里真实发生过的一对冲突访问，它是不会编故事的。另一头是没报不作数：TSan 只看这次执行真跑到的路径，您的测试没让两个访问碰上面，它也就无从说起了，所以您做并发测试要多换线程数、多换任务粒度、多跑上几轮。

算法本身也留了活口，官方 wiki 也明说了：“There is tiny probability to miss a data race though”，影子格里存的历史有限，极端场景下的旧记录会被挤掉。咱们合成一句：报了必是真 race，没报可就不等于没 race 了。

开销与搭档的部分，咱们也顺路交代。wiki 给典型程序的口径是一整句：`for a typical program, memory usage may increase by 5-10x and execution time by 2-20x`，内存涨的是五到十倍，执行时间慢的是二到二十倍，量级上咱们心里有数就好。

TSan 的二进制是给测试和 CI 用的，咱们不拿它跑生产，本机的实测数字留给实验回填。

还有一条硬的限制：-fsanitize=thread 与 -fsanitize=address、-fsanitize=leak 不能同开，编译器就直接报了错，您想要两套 sanitizer，就得分开构建两份二进制了。ASan 管的是内存错，TSan 管的是 race，它的分工表在 [线程参数与生命周期陷阱](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md) 里会正式见到，那边还要请 ASan 抓一次悬垂引用给您看。

<!-- 实验回填：本机 TSan 版与普通版的运行耗时与内存占用对比 -->

> 还有一桩环境病值得咱们认脸：社区里有一批 WSL2 用户报告过这个病，他们的内核在 6.6.6 及以上，部分环境里的 TSan 一启动就报 “FATAL: ThreadSanitizer: unexpected memory mapping”，连程序的边都没摸到。报告者排查出来的根因是内核的 vm.mmap_rnd_bits 设成了 32，与 TSan 的影子内存映射谈不拢，修复的办法是把 `sudo sysctl vm.mmap_rnd_bits=28` 打上再重跑。咱们 6.18 内核的机器不受影响，冒烟一路畅通——您要是遇上了，回头查它就好了，没遇上就别折腾了。

环境病和 race 病咱们要分清：报 unexpected memory mapping 的时候，那其实是 TSan 自己没起来，它连程序的边都没摸到。报 WARNING: ThreadSanitizer 的时候，才是它真正抓到了 race 的信号。症状是不同的，药也是不同的。

## race condition：另一个竞争，另一种错

咱们还得把一对双胞胎请出来辨一辨：data race 和 race condition，中文里的两个名字都带竞争俩字，讲混的人一片一片。

咱们把两者摆开：data race 是标准的术语，判据落在内存的层面：同位置、至少一写、无 happens-before。race condition 则宽得多了，它是个工程概念：程序的结果取决于线程的交错顺序。

两者的关系，咱们只押一个可靠的方向——没有 data race 的时候，race condition 也还是可以存在的。

咱们看一个 TSan 抓不到的错，您就知道这话不是空谈。咱们这次写一个带容量上限的向量，满了就不往里加，每一次访问都端端正正地拿着锁：

```cpp
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

std::vector<int> data;
std::mutex data_mtx;

void add_if_not_full(int value)
{
    {
        std::lock_guard<std::mutex> lock(data_mtx);
        if (static_cast<int>(data.size()) >= 100) {
            return;               // 检查：拿着锁
        }
    }                             // 锁放下了，窗口敞开
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    {
        std::lock_guard<std::mutex> lock(data_mtx);
        data.push_back(value);    // 操作：重新拿锁
    }
}
```

跑它的驱动代码（harness）咱们也一起给出，您让两个线程各推 60 个数，上限定的是 100：

```cpp
int main()
{
    std::thread t1([] {
        for (int i = 0; i < 60; ++i) {
            add_if_not_full(i);
        }
    });
    std::thread t2([] {
        for (int i = 0; i < 60; ++i) {
            add_if_not_full(1000 + i);
        }
    });
    t1.join();
    t2.join();
    std::cout << "final size = " << data.size() << "\n";
    return 0;
}
```

data race 还有吗？没有了，咱们每一次对 data 的访问都在锁里，TSan 跑下来全程都是安静的。

可是咱们的检查和入队，是两段分开的临界区，中间的窗口敞着：两个线程都趁着 size 是 99 通过了检查，然后就各自入了队，上限就被突破了。咱们把窗口拉宽到一毫秒跑，出来的 size 是 101，咱们把超编看得明明白白。咱们把那行睡删掉了之后，窗口缩回到了几纳秒，超编就难得一见了，可咱们的代码一处都没改，错还稳稳地留在原地。

锁管住了内存层面的冲突，却管不住查一眼然后干被拆成两截的逻辑漏洞。治本的做法是把检查与操作放进同一个临界区，让它们成为不可分割的动作，同样的讲法在 [mutex 与 RAII 守卫](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md) 那篇，咱们到 ch02 细看。

这类错的学名叫 check-then-act：检查之后、动手之前，世界就变了。它在单线程里也有自己的影子，咱们查一眼文件存在就去打开，两步之间文件就被人删了，并发只是把窗口拉宽到了任意时刻。它的名字听着像新朋友，事情您早见过了。

反方向的关系，咱们把话说软：data race 往往同时也是 race condition，毕竟它的结果确实依赖交错，但这个方向不是必然成立的，判定要靠的是定义而不是感觉。

层次上咱们这么排：消除 data race 是底线，锁和原子也就够了。而消除 race condition 还得靠接口的设计，那一层的功夫贯穿 ch02 整章。

## 死锁与它的亲戚：只认个脸

并发的问题家族里还有几位常客，咱们本篇请他们集体亮个相，免得您以为竞争就是全部。咱们本节不教诊断，只教您认脸：见过名字、知道大概症状、知道正源在哪，遇到了不慌，这也就够了。名气最大的是死锁：两个线程各持一把锁，又伸手去够对方手里的那把、谁也不肯撒手，程序就永远停在了那儿。

```cpp
std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> a(mtx_a);      // 拿 A
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> b(mtx_b);      // 等 B：B 在 thread2 手里
}

void thread2()
{
    std::lock_guard<std::mutex> b(mtx_b);      // 拿 B
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> a(mtx_a);      // 等 A：顺序反了过来
}
```

中间那 50 毫秒的睡是笔者加的，用来把交错的窗口拉宽、这程序基本一跑就死锁，您拿 timeout 3 ./deadlock 跑它，三秒后被强制掐断了，这就是死锁的样子。

死锁的配方，Coffman 几位作者在 1971 年就总结齐了，咱们背下四样就行：互斥、持有并等待、不可剥夺、循环等待。四样同时凑齐了，死锁跟着就发生了，咱们打破任何一样都能破局。

咱们工程上最常用的破法是统一锁的顺序：全工程都按同一个顺序拿锁，循环等待也就无从形成了。C++17 的 std::scoped_lock 还能一次锁多把，内部带着避免死锁的获取策略。程序真挂住了怎么救、gdb 的三个命令怎么下，它们的正源在 [死锁与现场诊断](../ch02-mutex-condition-sync/03-deadlock-and-gdb.md) 那篇，那篇会把诊断流程走全的。

咱们也给死锁的两位亲戚认个脸。活锁的情形是线程都在动、CPU 都在烧，就是没有一点的进展——两边都客气地互相礼让，永远也让不开了。饥饿的情形是有些线程永远轮不到资源，别人的那边热火朝天，它一场一场地等下来，每一场都没它的份。

他们的名字起得很直白：活锁的活字，说的是线程活着。饥饿的饿字，说的是任务干等。病理是不同的，药方也是不同的，咱们把深讲放在 ch02 的 [死锁与现场诊断](../ch02-mutex-condition-sync/03-deadlock-and-gdb.md) 一篇。

还有一位 `优先级反转`：低任务拿到了锁、高任务等着这把锁，中任务又把低任务挤下了 CPU——结果最高的任务，被两个比它低的间接卡死了。1997 年的 Mars Pathfinder 在火星上反复复位，地面排查了半天，根子就是它干的。它要调度器配合才有解的，咱们在通用平台上少见它，咱们到 ch02 的 [同步原语工具箱](../ch02-mutex-condition-sync/05-sync-primitives-toolkit.md) 再细说。

## 练习

三道题分别对着笔算、判定、工具全流程的三个层次，难度是递进的，咱们建议您全做。

### 练习 1：丢更新的笔算与实测

拿本篇的计数器当标本，咱们笔算两个问题：结果的理论上限是多少？下限又是多少？给您的提示是：最坏的交错为两个线程完全同步走，每一轮里两边都读到了同一个值、也写回了同一个值，十万轮下来总数只涨了十万。

然后您用 -O0 编译裸跑十次，记下见到的最大值和最小值，跟笔算的对一对，十次的循环用一条 for 就够了：

```bash
g++ -O0 -pthread 01_data_race.cpp -o race0
for i in $(seq 1 10); do ./race0; done
```

末了再想一层：为什么 -O2 下几乎次次 200000？文中给过答案，您拿 objdump 亲手再看一遍更扎实。

### 练习 2：给一段代码判 race

下面这段代码有没有 data race？请您自己判一遍：有的话，按标准定义把冲突的一对访问标出来，位置在哪、谁读谁写、缺的是哪个要件。咱们判定时只认的是定义，不认的是运行结果。

```cpp
std::atomic<bool> ready{false};
int value = 0;

void producer()
{
    value = 42;                          // (A)
    ready.store(true);                   // (B)
}

void consumer()
{
    while (!ready.load()) {              // (C)
        std::this_thread::yield();
    }
    std::cout << value << "\n";          // (D)
}
```

答案带点反直觉的意思：没有。咱们在默认内存序下看，(B) 与读到 true 的 (C) 之间，建立了 happens-before。咱们在讲定义的时候说过，次序是由同步动作建立的，而 [intro.races] 点名的同步动作，包括了原子操作和 mutex，默认内存序是算数的，relaxed 是不算数的。(A) 又排在了 (B) 前面，(D) 也排在了 (C) 后面，一环扣一环地把 (A) 和 (D) 隔开了。

可您把 (B)(C) 都换成 memory_order_relaxed 再判一次，答案就翻面成有了——relaxed 的原子操作不建立这样的次序，value 上的读写成了没看住的 race，TSan 笔者亲测是会报的。翻面的机关在哪，正是 [内存序](../ch03-atomic-memory-model/03-memory-ordering.md) 整篇要讲的事。

### 练习 3：TSan 全流程走一遍

您用 TSan 编译本篇的坏程序，跑出了报告，咱们再从里面抄下三样东西：冲突双方的行号，两个线程的创建行，Location 指的变量。然后您加上 mutex 修复，配套代码仓里的锁修复版 `02_data_race_mutex.cpp` 可以对答案，复跑确认报告干净、退出码也从 66 回到了 0。

命令和输出都留好了，从本篇起这就是咱们每次并发交付的标准动作。

您再往前一步，可以把这样两条构建写进 CI：普通构建跑的是功能用例，TSan 构建跑的是同一批用例，退出码非零就把它拦了下来。工具进了流水线，才算真正站住了脚。

## 本篇收进包里的东西

- 判据三条：同一个内存位置、至少一个写、没有 happens-before 隔开，全中即 data race。
- 定义末尾那句狠话：任何 data race 都是未定义行为，结果对也不算数。
- TSan 三问：哪两处访问、哪两个线程、中间缺了什么同步。
- 工具的边界：报了必是真 race，没报不等于没 race，并发测试要广。

## 下一步

全卷的第一件工具，咱们收进包里了。

还有一件小事咱们说在前面：本卷后面的代码，都默认您会用本篇的两条构建——普通构建与 TSan 构建。工具一旦上了手，就不再专门教了，咱们只用它。

下一篇 [OS 线程与开销](./03-os-threads-and-cost.md) 咱们换个视角，去看一条线程在操作系统那边是什么、有多贵，perf stat 也会在那里露面的。想马上动手的读者，[练习体系](../exercises/) 的 Lab 00 就是工具链与第一场竞态，正对着本篇的靶心。您迷路了就回 [卷地图](../) 看一眼位置。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch00-concurrency-fundamentals/`。

## 参考资源

- [intro.races 条款 —— C++ 标准草案（eel.is）](https://eel.is/c++draft/intro.races)
- [Multi-threaded executions and data races —— cppreference](https://en.cppreference.com/w/cpp/language/multithread)
- [std::thread::join —— cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [std::atomic::fetch_add —— cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/fetch_add)
- [Why undefined semantics for C++ data races? —— Hans Boehm](https://www.hboehm.info/c++mm/why_undef.html)
- [ThreadSanitizerCppManual —— google/sanitizers wiki](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual)
- [ThreadSanitizerAlgorithm —— google/sanitizers wiki](https://github.com/google/sanitizers/wiki/ThreadSanitizerAlgorithm)
- [Instrumentation Options —— GCC 手册](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)
- [Coffman, Elphick, Shoshani, System Deadlocks, ACM Computing Surveys 3(2), 1971](https://doi.org/10.1145/356586.356588)
- [Williams, C++ Concurrency in Action, 2nd ed, Manning, 2019](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
