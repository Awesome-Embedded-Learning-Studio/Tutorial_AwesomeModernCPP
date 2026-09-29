---
title: "mutex 与 RAII 守卫"
description: "从手动 lock/unlock 的漏锁路径出发，把放锁的活交给作用域，再配齐 lock_guard、unique_lock、scoped_lock 三件守卫与 osyncstream"
chapter: 2
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
  - RAII守卫
difficulty: intermediate
platform: host
reading_time_minutes: 20
prerequisites:
  - "数据竞争与 ThreadSanitizer 第一课"
  - "线程所有权与 jthread/stop_token"
related:
  - "condition_variable 与阻塞队列"
  - "死锁与现场诊断"
  - "thread_local"
cpp_standard:
  - 11
  - 17
  - 20
---

# mutex 与 RAII 守卫

ch01 里咱们养出来的线程，各自干着各自的活、谁也不碰谁的数据，收尾时 join 一下就两清了。其实真正的并发程序没这么客气：多个线程总要读写同一批数据，而 [data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md) 已经验过，两行看似无害的 `++counter` 挤在一起，结果就是坏的。当时的修法您还记得：counter 外面套的那把 `std::lock_guard`，让两个线程的自增从此排上了队。锁为什么能修好它呢，那篇用 happens-before 的直觉讲过一遍。而 lock_guard 本身，咱们当时当黑盒用，答应过到 ch02 把它的全貌补齐。

卷首立的三条原则里，`先锁，再无锁` 说的就是脚下这一段路：把锁用对、用稳，把共享数据都看住了。无锁的事，等 ch04 手里有了测量的手段再谈。本篇就是锁的第一站，咱们把最小的一把锁 `std::mutex` 拿起来看看它怎么用、怎么漏，再看看怎么让作用域替咱们把漏堵上。

挑选的口径 ch00/02 也给过：单变量的计数用 `std::atomic` 往往更轻。要协同变化的变量一多，咱们就把整段代码一起看住、用 mutex。本篇的对象就是后者——一段必须“至多一个线程在场”的代码。

## 临界区：把“至多一个”要回来

`std::mutex` 的接口小得一行就能数完：`lock()`、`try_lock()`、`unlock()`，可它的语义是并发世界的地基。咱们调 `lock()` 拿锁，拿不到的话线程就阻塞排队。而 `try_lock()` 是另一种性子：摸一把就走，拿不到的话立刻返回 `false`。剩下的 `unlock()` 就简单了：放锁，把排队的机会让给下一个线程。

咱们还得记牢两条约束。头一条约束是锁归拿它的线程所有：别的线程跑来 `unlock()`，吃到的同样是未定义行为。第二条是 `std::mutex` 不认重复的 `lock()`：持有期间再去锁它的话，吃到的还是未定义行为。真要递归的话，标准库备了 `recursive_mutex`，可它的适用面窄得很——多数时候它一出现，就说明咱们的设计绕了弯。

被 `lock()` 与 `unlock()` 夹住的那段代码，就是临界区（critical section）——至多一个线程在场的地界，共享数据任凭您读改。咱们从最手动的写法起步，看看它怎么漏的：

```cpp
std::mutex early_ret_m;

int leak_by_early_return(bool something_wrong)
{
    early_ret_m.lock();
    if (something_wrong) {
        return -1;      // 写在下面的 unlock，永远执行不到
    }
    // ……干活……
    early_ret_m.unlock();
    return 0;
}
```

提前 `return` 就是头一条漏路的来源。第二条更绝的漏路藏在异常里：临界区里抛了异常的话，`throw` 之后的代码就不再执行了，手动配平在这里天生残疾——您没法在所有路径上都记得放锁，因为路径的数目本身就数不清。

您光说漏了不算数，咱们得取证。怎么证明锁漏了？最直接的办法，就是咱们眼看着下一个线程永远卡在 `lock()` 上，可它的代价是整个程序得陪着挂死。`try_lock()` 能回答锁是不是空闲的，但它得从别的线程发起：咱们自己拿着锁的时候再对它 `try_lock()`，踩到的还是未定义行为。所以咱们取证得借第二个线程的手，探完了再回来看结果：

```cpp
// 用另一个线程探一把锁是否仍被占用。
// 谁拿的锁谁放，所以探针线程自己在 lambda 里配平。
bool probe_locked(std::mutex& m)
{
    bool got = false;
    std::jthread probe([&] {
        got = m.try_lock();
        if (got) {
            m.unlock();
        }
    });
    probe.join();   // 必须等探针干完活再读 got，不然读到的是初始值
    return !got;    // true = 锁仍被原线程攥着
}
```

咱们把两条漏路各跑了一遍，本机的判决如下（GCC 16.2.1，完整程序在篇尾的代码仓）：

```text
提前 return 后锁仍被持有: 1
异常被接住: 临界区里出了事
异常路径后锁仍被持有: 1
```

漏掉的锁，后果不止“少放了一次”这么轻：下一个碰它的线程会永远阻塞，程序也就此挂死了。带着锁的 mutex 走到析构、又是未定义行为。手动 `lock()`/`unlock()` 的问题，记性是补不上的，它出在结构上——每一条提前退出的路径、每一处可能的异常，都是一条漏锁的路。Core Guidelines 把话挑明了，CP.20 的标题就是 `Use RAII, never plain lock()/unlock()`。RAII 是 Resource Acquisition Is Initialization（资源获取即初始化）的缩写，名字直说的机制很朴素：资源在拿到手的那一刻，就绑在一个对象的身上，对象的寿命有多久，它替您看管的资源就活多久。下面咱们就看看它怎么把结构性的漏法整体消掉。

### 锁保护的是什么：不变量

咱们动手以前还有一层认识要建立：锁真正锁住的东西，咱们叫它不变量（invariant）——一句关于共享数据的陈述，任何时候它都应当是真的。拿 Account 结构做例子：两个对象各自的余额，在任何观察者眼里、总和都应当是守恒的。可从一边扣、往另一边加是两步的动作，中间要是被打断了的话，守恒就不成立了。咱们把两步圈进同一个临界区，锁保护的正是总和守恒本身。

不变量的视角，还能帮咱们检查锁配没配齐。一个不变量对应一把锁：凡是能碰到这批数据的路径，都得拿同一把锁——少一条路径的话，守卫就白站岗了。队列类的 push 拿了锁、某个直通成员函数却没拿锁，咱们照样拦不住数据从侧面漏出去。[data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md) 里 check-then-act 的坏例子，治法也是同一副药：咱们把检查与操作圈进同一个临界区、配上同一把锁，竞争的窗口就没了。配套的纪律也有两条。您记头一条纪律：锁内不做慢活，打印、发包之类的活咱们都往外挪、临界区也收得越小越好。第二条：来历不明的代码别在锁内调用，它可能回头再拿锁、僵住以后的形状归 [死锁与 gdb](03-deadlock-and-gdb.md) 分析。

## lock_guard：放锁的活交给作用域

咱们还是从计数器修起，ch00/02 那次修过的家伙，这回咱们把镜头拉近看清楚：

```cpp
long counter = 0;
std::mutex counter_m;

void bump()
{
    std::lock_guard<std::mutex> lk(counter_m);
    ++counter;   // 异常也好、提前 return 也好，都不用惦记放锁
}
```

`lock_guard` 干的活，一句话就能讲完：构造时替咱们拿锁，析构时再替咱们放锁，中间不给您任何插手的机会。您不能移动它、也没法手动 `unlock()`，除了构造再没有别的入口。它的功能少得像缺点，其实是设计：能做错的口子，它全替您堵上了。异常真来的话，栈回溯会经过 `lk` 的析构，锁照放——ch00/02 里那句“异常路径上也不漏放”的承诺，干活的正是它。

写法上还有一处省心的地方：C++17 起类模板实参推导（CTAD）上岗了，咱们连模板参数都能省去，写 `std::lock_guard lk(counter_m);` 就够了，类型是构造参数推出来的。本文后面的 `std::scoped_lock lk(from.m, to.m);` 靠的也是它，后文的 `unique_lock` 也吃这一套，写起来也就不再像模板时代那么啰嗦了。

本机的验证跑了两组：四线程各十万次自增，结果收敛到了期望值。异常抛过之后咱们再探一次，锁已经不在了：

```text
4 线程 x 100000 次自增, 结果 = 400000（期望 400000）
异常被接住: 守卫还在，异常先走
异常过后锁已被释放: 1
```

黑盒拆到这儿了，ch00/02 当黑盒用的 lock_guard 就算讲全了。下面咱们看一种写法，它能把上面的好处一笔勾销。

### 无名守卫：两种写法，两种下场

忘写守卫的变量名，是 RAII 锁最经典的误用。它的两种形态在现代工具链上的下场完全不同，咱们分开来看。

咱们从头一种括号版看起：

```cpp
std::lock_guard<std::mutex>(mtx);   // 想锁 mtx，实际上什么都没锁
```

咱们拿它去编译，根本过不了这关。GCC 16 眼里的这一行，是“声明一个名叫 `mtx` 的 `lock_guard` 变量”，而它正是最阴险的解析（most vexing parse）的近亲。麻烦的根源，是函数风格的写法与变量声明起了冲突。编译器的原话给您放在下面（GCC 16.2.1 节选）：

```text
warning: unnecessary parentheses in declaration of 'mtx' [-Wparentheses]
    8 |     std::lock_guard<std::mutex>(mtx);
      |                                ^~~~~
error: no matching function for call to 'std::lock_guard<std::mutex>::lock_guard()'
    8 |     std::lock_guard<std::mutex>(mtx);
      |                                    ^
```

声明既然成立了，走的就得是默认构造，而 `lock_guard` 偏偏没有默认构造函数，错误在这儿收了场。旧教材爱拿它吓唬咱们，说编译器一声不吭——在现代工具链上这话过时了：括号版连编译都过不了、谈不上潜伏，错误被拦在了编译期。

花括号版才是咱们要找的、真正能跑起来的误用：

```cpp
std::lock_guard<std::mutex>{mtx};   // 编译通过，锁也真的拿了——又立刻放了
```

花括号排除了声明式解析，它成了一个临时对象：构造时锁上、语句末尾析构，锁当场就归还了。下一行咱们再去碰受 mtx 保护的数据，保护已经不在了。咱们还是用 `try_lock()` 取证，照旧借第二个线程的手：探针线程一摸就摸到了。归仓的演示把这一段放在 `-DSHOW_UNNAMED_GUARD_TRAP` 开关下，跑出来的输出是：

```text
下一行用另一个线程探锁，free = 1（1 = 锁确实白拿了）
```

好在工具链在这里也替您把着关。libstdc++ 打 GCC 13 起就把守卫的构造函数标上了 `[[nodiscard]]`，您开 `-Wall` 就能看到警告：

```text
warning: ignoring return value of 'std::lock_guard<_Mutex>::lock_guard(mutex_type&)
[with _Mutex = std::mutex; mutex_type = std::mutex]', declared with attribute 'nodiscard' [-Wunused-result]
    9 |     std::lock_guard<std::mutex>{mtx};
      |                                    ^
```

您忽略警告硬跑的话，程序照样跑得起来、锁照样白拿了，所以那道警告不是摆设。要记的就一句：守卫对象必须有个名字——名字就是锁的寿命。

## unique_lock：能解开，能拿回，能带走

`lock_guard` 把口子全堵上了，代价是它默认了“作用域就是临界区”。可临界区经常比作用域小：锁内改完共享数据的活之后，后面的格式化、打印、发网络包，就都不必占着锁了。有时候咱们还得把锁整个交出去、让别人保管一阵。`unique_lock` 把这些口子有选择地开了回来，而且开得仍然是安全的。

构造的方式有三种、全靠 tag 区分，咱们一种一种过：

```cpp
std::mutex m;

{
    std::unique_lock<std::mutex> a(m, std::defer_lock);  // 只包装，不锁
    // a.owns_lock() == false
    a.lock();
    // a.owns_lock() == true
}

{
    m.lock();  // 手动拿锁
    std::unique_lock<std::mutex> b(m, std::adopt_lock);  // 接管看管责任
    // b.owns_lock() == true，析构时替咱们放
}
```

`defer_lock` 的意思是构造时不锁：锁不锁、什么时候锁的主动权，都由您说了算，`owns_lock()` 会随时报出您手里有没有锁。`adopt_lock` 承认的是已经拿在手里的锁——手动 `lock()` 拿到的那一把：您把看管责任交给它，析构时它替您放。`std::lock` 的老式搭配就靠它收尾，用法放在后面的 scoped_lock 一节里。还有 `try_to_lock` 的玩法：摸一把，拿不到的话就不纠缠，`owns_lock()` 会告诉您结果——结果拿不到的话，您就别碰受保护的数据、走别的路。拿不到锁的场景要演示的话，咱们得请第二个线程出场，本机的输出就在下面：等主线程把锁攥住了，子线程的 `try_to_lock` 拿到 = 0。

```text
defer_lock 构造后 owns_lock = 0
lock() 之后 owns_lock = 1
adopt_lock 构造后 owns_lock = 1（析构时替我们放）
主线程持锁时, 子线程 try_to_lock 拿到 = 0
```

要是您想要的是“限时等待、等不到就撒手”，标准库也备了路：咱们把 `std::mutex` 换成 `std::timed_mutex`，`unique_lock` 就多出 `try_lock_for()` 与 `try_lock_until()` 两个带时限的拿法。拿不到的话就超时返回，决策权回到您手里：

```cpp
std::timed_mutex tm;
std::unique_lock<std::timed_mutex> lk(tm, std::defer_lock);
if (lk.try_lock_for(std::chrono::milliseconds(50))) {
    // 50 毫秒内拿到了锁，干活
} else {
    // 超时了：走降级路径，别死等
}
```

时限拿法用的场合不算多，可在“宁可放弃也不能僵住”的设计里，它走的是正路。僵住之后的诊断与治理，咱们还是交给 [死锁与 gdb](03-deadlock-and-gdb.md)。

咱们提前放锁、需要时再拿回来，这是 `unique_lock` 的看家本领：

```cpp
std::unique_lock<std::mutex> lk(m);
++shared;              // 临界区：改共享数据
lk.unlock();           // 提前放：后面的活不碰共享状态，没必要占着锁
log_and_send(shared);  // 耗时的活在锁外
lk.lock();             // 需要时再拿回来
--shared;
```

它也是三件守卫里唯一可移动的：咱们能把锁的看管权作为返回值交出去，也能存进容器、传给别的函数。

```cpp
std::unique_lock<std::mutex> keeper = std::move(lk);
// keeper.owns_lock() == true，lk 变成空壳：owns_lock() == false
```

咱们看本机的输出：

```text
提前 unlock 后 owns_lock = 0
拿回来后 owns_lock = 1, snapshot = 1
移动后原对象 owns_lock = 0, 新对象 owns_lock = 1
```

与 `unlock()` 容易混的还有一个 `release()`，值得咱们单独立在这儿：它放弃看管权、返回 mutex 的指针，但锁是不放的——锁还锁着，看管的责任从此与它无关，析构也不会再替您放了。`unlock()` 动的是锁的状态，`release()` 动的是看管权的归属，是两件不同的事。真实的用场不算多，但把两者分清了，您对“所有权在谁手上”的判断就不会含糊。

“能解开、又能原样拿回来”的本领，`lock_guard` 给不了，它压根不给您暴露 `unlock()` 的机会。您也不用急着找用场：有一样东西要求“把您的锁交给我，我睡醒了再还您”，说的就是 `condition_variable`——[它的正源](04-condition-variable-and-bounded-queue.md) 开篇头一节就会来借这手本领。

## ThreadSafeQueue：push 讲明白，pop 当黑盒

把本篇的家伙拼成一件常用件：线程安全队列。它也是后面线程池与 Actor 邮箱的地基，咱们在这儿把它立起来——push 用本篇的守卫讲明白、pop 当黑盒用。

```cpp
template <typename T>
class ThreadSafeQueue {
public:
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(value));
        }                  // 临界区到花括号为止，锁在此归还
        cv_.notify_one();  // 通知放在锁外
    }

    T pop();   // 黑盒：队列空的时候它能"等"，不忙转

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> q_;
};
```

push 的骨架您已经全认识了。内层的一对花括号把临界区收得只剩 `push_back` 一步，上节“临界区比作用域小”的技巧、用在类成员上就是这个样子。还有一处值得说的设计：value 按值传入，可能的拷贝留在了调用方，锁内只做一次 `std::move` 入队、临界区里干的活越少越好。`notify_one()` 咱们挪在锁外，理由与 pop 的实现绑在一起，收编的时候咱们一并讲清。

pop 的签名留在上面，实现整段进了黑盒。您在队列空的时候调 pop，看到的行为是这样：它不返回、不忙转、不烧 CPU，安静地等到有人 push。那它凭什么等得起来？用的正是 `unique_lock` 那手“交出去、拿回来”。完整实现与它的每一个为什么，咱们交给 [condition_variable 与阻塞队列](04-condition-variable-and-bounded-queue.md) 整篇收编。本机的演示里消费者比生产者早跑了 100 毫秒、全程没忙转：

```text
消费者已在 pop 里等了 100ms（没有忙转）
消费者取完 1..8, sum = 36（期望 36）
```

## 一次拿两把：scoped_lock 与 std::lock

划转要在同一时刻看到两个对象的余额，两把锁咱们都得拿到手。演示用的 Account 形状很简单：一份余额配一把自己的锁。

```cpp
struct Account {
    long balance = 1000;
    std::mutex m;
};
```

线程甲按 A、B 的顺序拿锁，线程乙按 B、A 的顺序拿——各持一把、互等另一把，谁也不再往前走了，这就是死锁最经典的形状。诊断与全景是 [死锁与 gdb](03-deadlock-and-gdb.md) 一篇的主线，咱们这里只把工具领走。

咱们领走的头一件工具是 `std::lock`：一条语句把两把锁一起拿，内部走的是死锁避免的算法，哪一把一时拿不到的时候，就把已经到手的都放掉重来，异常路径上它也把已锁的都放干净了才重抛。算法凭什么免疫、免疫的边界在哪，[死锁与 gdb](03-deadlock-and-gdb.md) 一篇会拿 AB-BA 现场拆给咱们看。

咱们在 C++17 拿到的 `scoped_lock` 把 `std::lock` 包成了 RAII：

```cpp
void transfer(Account& from, Account& to, long amount)
{
    std::scoped_lock lk(from.m, to.m);   // 一次拿两把，析构全放
    from.balance -= amount;
    to.balance += amount;
}
```

标准的定位原文是 `The class scoped_lock is a mutex wrapper that provides a convenient RAII-style mechanism for owning zero or more mutexes for the duration of a scoped block.`，多把锁的场合标准说的是 `deadlock avoidance algorithm is used as if by std::lock`——与 `std::lock` 同一套算法。析构时它把名下的所有锁逐一放掉、一句析构收全部。`zero or more` 里的零是有真实含义的：不给锁的构造同样合法，咱们偶尔还能在泛型代码里拿空守卫顶一个位置。特性宏 `__cpp_lib_scoped_lock` 的取值等于 `201703L`，咱们可以拿它探这件东西到没到。cppreference 还有一句偏好的建议，您能用 `scoped_lock` 的话就别裸调 `std::lock`，它的原文是 `std::scoped_lock offers a RAII wrapper for this function, and is generally preferred to a naked call to std::lock.`

C++17 以前的老写法如今咱们仍用得上，靠的正是前文 unique_lock 一节的 `adopt_lock`：

```cpp
void transfer_std_lock(Account& from, Account& to, long amount)
{
    std::lock(from.m, to.m);                                  // 先抓两把
    std::lock_guard<std::mutex> lk1(from.m, std::adopt_lock); // 再接管
    std::lock_guard<std::mutex> lk2(to.m, std::adopt_lock);
    from.balance -= amount;
    to.balance += amount;
}
```

三行样板是它的代价：`std::lock` 抓锁、两个 `adopt_lock` 守卫接管。咱们读老代码库的时候认得出它就行，新代码里一条 `scoped_lock` 就顶掉了全部。

本机的实验里，咱们让两个线程用两种写法对转了两万次、拿锁的顺序刻意相反，总额是纹丝不动的，a 与 b 各自守住了 1000。

## 三件守卫怎么选

三件守卫的能力摆在一张表里，差异您一眼就能见底：

| 能力 | lock_guard | unique_lock | scoped_lock |
|---|---|---|---|
| 构造即锁 | 是 | 可延迟（defer_lock） | 是 |
| 提前 unlock | 不能 | 能，还能再 lock | 不能 |
| 移动所有权 | 不能 | 能 | 不能 |
| 一次拿多把 | 不能 | 不能 | 能 |
| 典型场合 | 默认选择 | 交出去保管、缩临界区 | 两把以上的锁 |

选型的口径给您三句话：默认的是 `lock_guard`、越简单越难用错。锁要交出去保管、或临界区比作用域小的话，就该换 `unique_lock` 了。一次要拿两把以上的锁，就轮到 `scoped_lock` 了。

`unique_lock` 的自由是有代价的：它得多记一份“现在手里有没有锁”的状态，对象也比 `lock_guard` 大了一圈。多数临界区咱们用不上这样的自由，也就不必带上这层状态了。

选完了守卫，咱们还得看一眼持锁的时间。锁内干的事越多，别的线程排队等的时间就越长，所以慢活外挪、临界区收窄的纪律，比守卫的挑选更要紧。它们俩咱们在不变量一节立过、这里再提一遍。

## 输出交错：一行一个临界区

咱们让多个线程往同一个 `std::cout` 打印，您会看到行断成半截。机制说穿了不复杂：`std::cout << "writer " << id << " round " << r << '\n';` 虽是一条语句，执行起来是五次 `operator<<` 的调用，交错的最小单位是“一次调用”、不是“一条语句”。别的线程完全可以从您写到一半的行中间插进来，半行半行地断。

咱们演示用的程序小得很：四个写线程、各打三轮。

```cpp
std::vector<std::jthread> writers;
for (int id = 0; id != 4; ++id) {
    writers.emplace_back([id] {
        for (int r = 0; r != 3; ++r) {
            std::cout << "writer " << id << " round " << r << '\n';
        }
    });
}
for (auto& t : writers) {
    t.join();
}
```

<!-- 实验回填：四线程各三轮裸 cout 的真实交错输出（本机已复现，逐次不同，回填时贴一次现场抓拍） -->

C++20 的 `<syncstream>` 给了咱们标准的做法：`std::osyncstream`。用法一行就够了：

```cpp
std::osyncstream(std::cout) << "writer " << id << " round " << r << '\n';
```

思路正是 mutex 的思路搬到流上：整行输出攒进 `osyncstream` 自己的缓冲，语句结束时一次性原子地转交给 cout。标准对转交的描述是 `Atomically transfers the associated output of *this to the stream buffer`，还保证输出 `appears in the output stream as a contiguous sequence of characters`——作为连续的字符序列出现、整段交付、不被人从中间插进来。转交要是失败的话它会 `setstate(ios_base::badbit)`，您也不会看到错误无声地溜走。它的来路是提案 P0053R7，特性宏 `__cpp_lib_syncbuf` 的取值是 `201803L`。

临时对象为什么够用？它的寿命恰好是整条语句、语句一结束就析构，析构时把还没转交的缓冲交出去。所以上面那行的写法、一行恰好就是一次原子交付。要攒的不止一行时，咱们给对象起名字、攒完手动调 `emit()`。`get_wrapped()` 则能拿到被包裹的流缓冲指针，需要直接检查 cout 的场合用得上。还有一条边界要交代清楚：osyncstream 保的是一行之内不被插入，行与行之间的次序它不管——谁攒完了谁走，本机的演示里行序就与线程编号对不上、完全正常。

咱们起名字的写法长这个样子：

```cpp
{
    std::osyncstream out(std::cout);   // 有名字：跨语句攒
    out << "writer " << id << " 开始干活\n";
    log_to_file();                     // 别处的输出，插不进上面那行
    out << "writer " << id << " 干完收工\n";
}   // 析构时，两行一起原子转交
```

<!-- 实验回填：同一程序换成 osyncstream 的对照输出（行序仍随调度变化，行内不再断裂） -->

所以 osyncstream 并没有引入什么新锁，它把“临界区”的思想直接搬到输出流上：咱们把一行当成一段临界区，攒齐了再交。有了它，为打印单配一把 mutex 的老办法也就可以退休了。

## 练习：轮到您动手了

三道题分别对着守卫的用法、输出的交错与队列的落地三个层次、难度递进，咱们建议您全做。

### 练习 1：三种拿法各验一遍

咱们把同一段读-改-写（读 `shared`、加一、写回）各包一遍 `unique_lock`，用的三个 tag 分别是 `defer_lock`、`try_to_lock`、`adopt_lock`，咱们再用 `owns_lock()` 在关键步骤后各打印一次。再往深想一步：`adopt_lock` 版的写法里，锁最初是谁拿的？那一下 `lock()` 与 `unique_lock` 有什么关系？

### 练习 2：修好交错，再故意拆掉

四线程打印的程序，咱们用 `osyncstream` 修到整行。然后咱们另写一小段：让 `std::scoped_lock{mtx};` 单独成一句，紧接着访问受 mtx 保护的数据，用另一个线程的 `try_lock()` 取证“锁白拿了”。`-Wall` 下 `[[nodiscard]]` 警告的原文抄下来，对照本文无名守卫一节的两种形态，请您说一说括号版与花括号版各自死在哪一步。

### 练习 3：把队列立起来

照本文的规格咱们把 `ThreadSafeQueue` 写出来：push 由您自己实现（`lock_guard` 版），pop 您一时写不出来很正常——它等的东西超出锁的范围：等的是条件，它的正源在 cv（condition_variable）那一篇。您可以让 pop 暂时借用队列内部的等法，调用侧咱们只当黑盒用。咱们让生产者睡 100 毫秒再 push，观察消费者在 pop 里安静地等。您学完 cv 篇回来补全实现，就是这道题的下半场。

## 下一步

本篇的成果，章内就有它的去处：`unique_lock` 那手“交出去、拿回来”，咱们 [condition_variable 与阻塞队列](04-condition-variable-and-bounded-queue.md) 那篇开篇就会接手，ThreadSafeQueue 的黑盒也在那儿收编。锁拿多了的话会僵住，现场的抓法与三条防线归 [死锁与 gdb](03-deadlock-and-gdb.md)。每线程各存一份的写法，您看 [thread_local](02-thread-local.md)。动手路线推荐 [exercises 的阻塞队列 Lab](../exercises/01-bounded-queue)，题目是有界队列、并发缓存与同步原语的组合。迷路了就回 [卷地图](../) 看一眼位置。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 参考资源

- [std::mutex — cppreference](https://en.cppreference.com/w/cpp/thread/mutex)
- [std::lock_guard — cppreference](https://en.cppreference.com/w/cpp/thread/lock_guard)
- [std::unique_lock — cppreference](https://en.cppreference.com/w/cpp/thread/unique_lock)
- [std::scoped_lock — cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [std::lock — cppreference](https://en.cppreference.com/w/cpp/thread/lock)
- [std::osyncstream — cppreference](https://en.cppreference.com/w/cpp/io/osyncstream)
- [CppCoreGuidelines CP.20: Use RAII, never plain lock()/unlock()](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [CppCoreGuidelines CP.21: Use std::lock() or std::scoped_lock to acquire multiple mutexes](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp21-use-stdlock-or-stdscoped_lock-to-acquire-multiple-mutexes)
- [P0053R7: C++ Synchronized Buffered Ostream](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0053r7.pdf)
- [Williams, *C++ Concurrency in Action*, 2nd ed, Manning, 2019](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition) —— 第 3 章共享数据与互斥量
