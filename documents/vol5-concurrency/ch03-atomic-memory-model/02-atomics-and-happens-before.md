---
title: "原子操作与 happens-before"
chapter: 3
order: 2
description: "atomic 的操作集与 CAS 的两个版本，sequenced-before、synchronizes-with、happens-before 的公理族谱与 DRF-SC 的正式陈述，末了把前两章押着的四桩悬案挨个判掉"
tags:
  - host
  - cpp-modern
  - intermediate
  - atomic
  - 无锁
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
reading_time_minutes: 22
prerequisites:
  - "缓存与重排：机器真的会乱序"
  - "数据竞争与 ThreadSanitizer 第一课"
related:
  - "缓存与重排：机器真的会乱序"
  - "内存序"
---

# 原子操作与 happens-before

[上一篇](./01-cache-and-reordering.md)咱们亲眼看了机器的三种自由，收尾把话停在了公理门口：判次序靠平台直觉是靠不住的，咱们得给什么时候谁能看见谁立一套大家都要遵守的规则。立规则的活归本篇，规则的名字您已经见过：happens-before。法立完咱们还要回头办案，前两章一路押着的四桩悬案，本篇也把它们挨个判掉了。

办案少不了物证，咱们分两批备齐。头一批是 `std::atomic` 的操作集：load、store、fetch 系、exchange、compare_exchange，外加 atomic_flag 与 lock-free 的查询。公理挂在嘴边的“同步动作”，指的就是这些操作与 mutex 一类的东西，咱们把它们认全了，后面的条款才对得上号。第二批是 happens-before 的族谱：sequenced-before、synchronizes-with、happens-before、strongly happens-before，四层关系是一层压一层的，越往后的管得越远。族谱立好了，DRF-SC 的正式陈述就水到渠成，第 0 章折叠框立下的无竞争程序的顺序一致性，上一篇收尾又点过它的名，现在也该转正了。

路线咱们也交代清楚。咱们把操作集摆开，您看到的每个操作都带着一段小代码，自旋锁作为 atomic_flag 的正主戏收尾。CAS 的部分要单独占一节，因为它肩负的东西最多：weak 与 strong 的分野、CAS 循环的姿势，全都记在了它名下。然后咱们立法，立完拿公理回头验一遍本篇开头写的自旋锁。末了是四桩判决，外加三道留给您的练习。

本篇的实测数字全部带着复现命令。实测都在笔者自己的 WSL2 Arch Linux（内核 6.18、g++ 16.2.1、AMD Ryzen 7 9700X）上跑出来，完整的代码放在 `code/volumn_codes/vol5/ch03-atomic-memory-model/`，正文里咱们只贴关键的片段，读法的约定在[卷首](../)立着，咱们照它来。

## atomic<T> 的操作集

[第 0 章](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)咱们用 `std::atomic<int>` 修过坏掉的计数器，`fetch_add` 顶掉了读、加、写三步，当时咱们只浅尝辄止。第 2 章一路用的都是拿锁、改数据、放锁的整套家伙，它们倒是安全、直观，可临界区小到只有一个变量的时候，这套流程就显得重了。atomic 针对的正是小粒度的场合：不加锁，靠原子指令保住单次访问的不可分割。本节咱们把它的家底全部摆开。

能装进 `atomic<T>` 的类型有讲究。整型与指针是全配的，fetch 系的算术与位运算都有。别的类型走的是通用路子，门槛咱们叫它平凡可拷贝（trivially copyable），白话讲的就是：复制它等于把字节原样抄了一份。`std::string` 这样自己管堆内存的不算，所以 `atomic<std::string>` 直接编译不过。通用路子给的操作有 load、store、exchange、compare_exchange 两种，fetch 系是缺的，因为标准不知道您的类型该怎么“加”。

构造的事咱们单独说清。`atomic<T> a{T{42}}` 带初值的构造从来都行，它对 T 没有默认构造的要求，代码仓里的 `atomic_default_ctor_verify.cpp` 用删掉默认构造的类型验证过，跑出来的就是 42、100、200 三行。默认构造的那句 `atomic<T> a;` 在 C++20 起会值初始化，也就是给了一个零值。更早的标准里它不初始化，您立刻去读就是未定义行为。补丁打在了 C++20（标准委员会库工作组的问题单 LWG 2334），三大标准库都是那时才改齐的，您读老代码要留意。

咱们把操作集列成一张速查表。衡量的维度是操作属于读、写还是读改写（read-modify-write，咱们下文简称 RMW：一整次原子动作里完成读、算、写，中间不留外人可见的状态）：

| 操作 | 类别 | 返回什么 |
| --- | --- | --- |
| `load()` / `store()` | 读 / 写 | 读到的值 / 无 |
| `exchange()` | 读改写 | 旧值 |
| `fetch_add()` 等 | 读改写 | 旧值 |
| `++x` / `x += n` | 读改写 | 新值 |
| `compare_exchange_*` | 读改写 | 成败布尔，失败时附带最新值 |

### load、store 与 exchange：读写的根基

`load` 与 `store` 是最基础的一对，所有的原子读写最后都落到它们身上，外加一个可选的内存序实参。不指定的时候全用默认的 `memory_order_seq_cst`，它是最安全的一档，代价咱们留给[内存序](./03-memory-ordering.md)细算。

```cpp
std::atomic<int> value{0};

value.store(42);                                   // 默认内存序：seq_cst
value.store(100, std::memory_order_relaxed);       // 显式给序
int x = value.load();                              // 默认内存序
int y = value.load(std::memory_order_relaxed);

int z = value;        // 隐式转换，等价于 load()
value = 200;          // 等价于 store(200)
```

隐式转换的手脚，笔者劝您少用。`int z = value;` 看着像普通变量的拷贝，背后其实是一次原子读，混进了复杂表达式里，读代码的人就分不清哪句在碰共享状态。显式地写 `load()` 要多打几个字，意图倒是一眼可见，多打的字花得值。

`exchange` 是无条件的换入：新值写进去，旧值交回给您。它省掉了比较、来者就换。典型的去处是状态交接，比如把设备状态从 Idle 拧到 Busy 的时候，`state.exchange(Busy)` 交回的旧值告诉您刚才是不是真的空闲，旧值不对的时候就回滚。咱们在“换入新值、旧值我也要”的场合用它，比写 CAS 循环的写法简洁。

### fetch 系：读改写，把旧值交回给您

整型与指针配有一整套的 fetch 操作：`fetch_add`、`fetch_sub`，整型独占的是 `fetch_and`、`fetch_or`、`fetch_xor`，指针的加减按元素大小步进。咱们说过，它们的共同身份是 RMW，设计上有个统一的脾气：返回**修改前的旧值**。

```cpp
std::atomic<int> counter{0};
int old1 = counter.fetch_add(5);   // counter 变 5，old1 是 0
int old2 = counter.fetch_add(3);   // counter 变 8，old2 是 5
```

返回旧值的安排并不亏。您拿到了旧值，就同时做成了两件事：读当前状态、改状态，而两件事之间没有任何缝隙让别人插进来。无锁算法处处吃的正是这个便利。

运算符的重载也在，脾气恰好相反：`++x` 与 `x += n` 返回的是**新值**，`x++` 返回的是旧值、行为等同 `fetch_add(1)`。您要是把返回值用在表达式里，咱们就得把一新一旧分清楚，`++x` 等同的是 `x.fetch_add(1) + 1`。只计数不用返回值的场合，咱们用哪个都行。咱们把两种返回各看一眼：

```cpp
std::atomic<int> x{10};

int new_val = ++x;   // x 变 11，new_val 是 11：前置返回新值
int old_val = x++;   // x 变 12，old_val 也是 11：后置返回旧值
x += 5;              // x 变 17，复合赋值同样返回新值
```

### is_lock_free：原子不等于无锁

到这里咱们一直说 atomic 不加锁，这话其实得打折。无锁的事得看类型宽度与目标平台：硬件没有对应宽度的原子指令的时候，库就退回内部锁去实现了，一个对象一把的、全程序一张锁表的都有。标准给了两个查询：`is_lock_free()` 在运行时问当前的对象，`is_always_lock_free` 是编译期的常量（C++17 起），它担保该类型在目标平台的所有实例都无锁。

```cpp
static_assert(std::atomic<int>::is_always_lock_free,
              "int 必须无锁，不然这个平台咱不伺候");
```

运行时那一档的用法咱们也看一眼：

```cpp
std::atomic<int> ai;
std::atomic<long long> all;

std::cout << "atomic<int>: "
          << (ai.is_lock_free() ? "lock-free" : "uses lock") << "\n";
std::cout << "atomic<long long>: "
          << (all.is_lock_free() ? "lock-free" : "uses lock") << "\n";
```

笔者在 WSL2 上跑 `-O2 -std=c++20`，x86-64 的 64 位世界里两个都是 `lock-free`：

```text
atomic<int>: lock-free
atomic<long long>: lock-free
```

您把上面的片段存进 `/tmp/lfq.cpp`，命令咱们在仓库根目录跑：

```bash
g++ -std=c++20 -O2 -o /tmp/lfq /tmp/lfq.cpp
/tmp/lfq
```

工程上笔者更信 `static_assert` 这一档。运行时查询意味着同一份代码在不同实例上可能走不同的路径，测试就难做了。编译期断言把问题拦在构建那一步：要么全平台无锁，要么编译不过，没有灰色地带。嵌入式上的事更要命：Cortex-M0/M3 这类 32 位核上，`atomic<int>` 几乎总是无锁的，`atomic<int64_t>` 却可能是带锁的，而中断服务函数（ISR）里不能阻塞，一把锁就把系统挂住了。您真要在 ISR 里用 atomic，`is_always_lock_free` 的断言是必上的保险。

### 浮点原子：能加，顺序不保证

浮点在 C++20 起也进了 `fetch_add`、`fetch_sub` 的辖区，咱们一段话带过它的两个特殊处。头一处是硬件的：多数 CPU 没有原子浮点加法指令，x86 的 `lock xadd` 只管整数，浮点的实现常常退化成 CAS 循环，所以别指望它比整数快。另一处是数学的：浮点加法不满足结合律，`(a+b)+c` 与 `a+(b+c)` 是可以不相等的，多线程各加各的，到达的顺序不同，加出来的结果也就不同，而顺序是不定的。您要的是可复现的结果，咱们就锁住整段，或者改用整数定点的写法。这套单变量原子性够用、要协同就另想办法的口味，贯穿了整个 atomic。

### atomic_flag 与自旋锁：把原语拼成一把锁

`atomic_flag` 是标准里唯一白纸黑字担保**永远无锁**的原子类型，在所有平台上都无一例外地无锁。它的状态只有两个，操作是两个半：`test_and_set()` 原子地设成 true 并交回旧值，把状态设回 false 的活儿归 `clear()`，C++20 又添了只读的 `test()`。因为它有无锁的担保，咱们构建最底层的同步原语时，它是唯一不问平台的起点。

咱们拿它拼一把自旋锁，这是 atomic_flag 的经典岗位。示例 54 的完整实现贴在下面：

```cpp
class Spinlock {
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;

public:
    void lock() {
        // test_and_test_and_set：先只读地看一眼，看着像空的才去抢，
        // 省得每个等待者都往缓存行上写，把持锁人的核打脏
        while (flag_.test(std::memory_order_relaxed)) {}
        while (flag_.test_and_set(std::memory_order_acquire)) {
            while (flag_.test(std::memory_order_relaxed)) {}
        }
    }
    void unlock() { flag_.clear(std::memory_order_release); }
};
```

咱们把 `lock()` 的两层结构读明白。外层的 `test(relaxed)` 只读地打量锁位，看着像空的才进内层去抢。为什么不直接全员 `test_and_set`？因为 `test_and_set` 是写操作，每个等待者都往同一个缓存行上写的话，持锁人所在的核就会被不断打失效，[上一篇的 MESI](./01-cache-and-reordering.md) 里那套写要独占的机制，反过来就成了等待者对持锁人的骚扰。只读的 `test` 在共享态就能完成，而它不抢缓存行。真去抢的那一下用 `acquire`，放锁的 `clear` 用 `release`，词面上的分量咱们到公理部分马上给它们立起来。

咱们开四条线程、各十万次自增，普通的 `long long` 计数器全靠这把锁看住：

<OnlineCompilerDemo
  title="动手验证：atomic_flag 自旋锁看住普通计数器"
  source-path="code/examples/vol5/54_atomic_flag_spinlock.cpp"
  description="四线程各 10 万次自增，临界区里是普通变量的 ++，安全性全靠 test_and_set 的 acquire 与 clear 的 release 两侧。counter 与 expected 两行都是 400000。临界区里的普通变量为什么不出竞争，判据在本篇后半的公理里。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者在同一台 WSL2 上的实测（`-O2 -std=c++20`）：

```text
counter: 400000
expected: 400000
```

counter 是个普普通通的变量，它其实没沾一点 atomic，凭什么安全？这个问题咱们押到公理部分回来验，它正好是整套族谱的最小应用题。

分寸咱们也交代一句。自旋锁是教学件的身份，生产环境您别拿它替换 `std::mutex`：等待者在空转烧 CPU。单核的机器上它甚至能把自己饿死，自旋的人占着核，持锁的人反而没机会跑来放锁。而 OS 的 mutex 在无竞争时走的是用户态的快路径（Linux 上叫 futex），开销在用户态就完了。它适合的窗口很窄：临界区只有几条指令、确定跑在多核、且您真的量出了 mutex 的开销，三个条件都齐了再考虑它。

### 原子性不传染：两件常被想岔的事

头一件：单次访问是原子的，不代表多次访问合起来也是原子的。咱们看一个最小的反例：

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// 线程 1
x.store(1);
y.store(2);

// 线程 2
int a = y.load();   // 可能读到 2
int b = x.load();   // 可能同时读到 0
```

咱们看结果：四次访问各自都是原子的，线程 2 仍然可能拿到的就是 `a == 2` 且 `b == 0`。atomic 其实没有失灵，它本来就只担保单次访问的原子性，从没担保过多个变量之间的次序，[上一篇](./01-cache-and-reordering.md)的 store buffer 现场就是这个结局的实物来源。要跨变量的次序，咱们就得靠内存序建立同步关系，正源在[内存序](./03-memory-ordering.md)的正文里。

第二件是上一篇记下的旧话：拿 `volatile` 当线程同步，是流传很久的老错误，正解咱们记在本篇。正解其实是一句泼冷水的话：您别用它。`volatile` 进的是单线程可观察行为的清单，它担保了访问不被合并、也不被省略，但是它既不原子，也不建立任何跨线程的次序。一个线程做 volatile 的写、另一个做 volatile 的读，落在了同一个位置上，标准给出的判罚照旧是 data race、未定义行为。`volatile` 的真正辖区是内存映射寄存器与信号处理一类的单线程场景。Windows 上 MSVC 的老扩展让 volatile 读写自带 acquire/release 语义，那不过是平台方言，出了那个编译器就不作数，咱们写可移植代码不认它。

## CAS：期望一个旧值，换上一个新值

compare-and-swap 的中文是比较并交换，咱们下文叫它 CAS，它是无锁世界的头号原语，几乎全部的无锁数据结构都立在它上面。它的一句话语义是：咱们记得这个位置放着某个旧值，如果咱们没记错，它就换成了新值，再告诉咱们成没成。

### 接口与语义：expected 会被覆盖

```cpp
bool compare_exchange_weak(T& expected, T desired,
                           std::memory_order success = std::memory_order_seq_cst,
                           std::memory_order failure = std::memory_order_seq_cst);
// strong 版的签名完全相同
```

咱们拿三行代码把成败两路都走出一遍：

```cpp
std::atomic<int> value{10};

int expected = 10;
bool ok = value.compare_exchange_strong(expected, 20);
// ok 是 true：value 变 20，expected 保持 10 不动

expected = 10;   // 重设期望值再试一次
ok = value.compare_exchange_strong(expected, 30);
// ok 是 false：value 还是 20，expected 被改写成 20
```

执行逻辑咱们挨步读。当前值等于 `expected` 的时候：换入 `desired`，返回的是 true。不等于的时候：把**当前值写进 expected**，返回的是 false。第二个行为是最容易被漏看的，`expected` 是按引用传入的，失败时它就被覆盖掉了，您后面还要用原来的旧值，您就得提前备份。不过这不算缺陷，它是顺水推舟的设计：失败的时候您最想要的恰恰是它现在的实际值，CAS 顺手带了回来。

内存序实参有两个：`success` 给成功的路径，`failure` 给失败的路径。失败时您只是在读，所以 `failure` 那一侧带不得 release 的语义，比 `success` 更强也是不允许的。怎么配对、什么时候值得省，答案都在[内存序](./03-memory-ordering.md)的整篇里，本篇的示例全用默认序或 relaxed，咱们把机制看真切了再谈序。

### weak 与 strong：虚假失败的分野

两个版本共用的是同一套语义，分野说的是 weak 的**虚假失败**（spurious failure），当前值明明等于 `expected` 的时候，它却还是可能返回 false 的。标准为它开这个口子的原因，是有些架构的 CAS 不由一条指令组成，它靠的是一对指令办事：load-linked 与 store-conditional，咱们下称 LL/SC。LL 读的时候会在地址上挂一个监视标记，SC 只在标记没被人碰过的前提下才允许写。同一缓存行被别的核碰了、中断来了、调度把线程切走了，SC 的执行都可能失败，而失败只说明监视被打断了，并不代表值变了。ARM、POWER 都是这么干的。机器是这样的，strong 版本通常就是 weak 外面再包一层直到真失败的循环。

x86 不在这类机器的行列里。它的 CAS 是硬件一条指令 `cmpxchg`，要么成、要么败，败也有败的道理，“看着相等却报失败”的戏码在这里不存在。所以在 x86 上，weak 与 strong 应当生成同样的机器码。咱们不信空口无凭这一套，照旧到汇编里找它的证据。完整的工程放在 `code/volumn_codes/vol5/ch03-atomic-memory-model/atomic_cas_weak_strong_asm.cpp`，命令咱们在仓库根目录跑：

```bash
g++ -std=c++20 -O2 -S \
  -o /tmp/atomic_cas_weak_strong_asm.s \
  code/volumn_codes/vol5/ch03-atomic-memory-model/atomic_cas_weak_strong_asm.cpp
grep -A8 "test_weak\|test_strong" /tmp/atomic_cas_weak_strong_asm.s
```

笔者机上跑出来的结果，指令是逐条照贴的，标签行里 `#` 后面的说明是咱们加的：

```text
_Z9test_weakRii:                 # compare_exchange_weak
    movl    (%rdi), %eax
    lock cmpxchgl    %esi, x(%rip)
    je      .L1
    movl    %eax, (%rdi)
.L1:
    sete    %al
    ret

_Z11test_strongRii:              # compare_exchange_strong
    movl    (%rdi), %eax
    lock cmpxchgl    %esi, x(%rip)
    je      .L4
    movl    %eax, (%rdi)
.L4:
    sete    %al
    ret
```

两个函数的指令逐条相同，只有跳转标签的名字不一样。咱们顺带把每条指令的差事读掉。`movl (%rdi), %eax` 把 expected 装进了 eax。`lock cmpxchgl` 比较内存里的 x 与 eax：相等就换入 esi 并置标志，不等的话就把实际值送回 eax。`je` 按标志跳过失败的路径。失败路径里的 `movl %eax, (%rdi)` 把实际值写回 expected，标准说的失败时 expected 被覆盖，在机器码里对应的就是它。末了 `sete` 把成败装进返回值。`lock` 前缀担保整条指令独占它碰的缓存行，全机同一时刻能动它的只有一个核，x86 上原子性的硬件来源就是它。

挑选的口径咱们一句话说完：CAS 外面包着循环的时候用 weak，虚假失败只是多转一圈的事，LL/SC 的机器上还省一层。一次定成败、不在循环里的 CAS 用 strong，不然一次虚假失败就把您的逻辑带偏了。x86 上两者是一样的价，按咱们刚才的口径写，咱们的代码搬到 ARM 也不吃亏。

### CAS 循环：被人抢了就带着新值重试

CAS 单发能做的事不多，真正的威力在循环里。姿势有三步：读一份旧值、算出对应的新值、拿旧值去换新值。换成了就收工，被人抢了，CAS 会把别人刚写入的最新值塞回 expected，咱们带着它回到条件再判一遍。示例 53 的注释把姿势写成了三句话：期望旧值、想换新值，被人抢了就带着对方的值重试。

```cpp
std::atomic<int> g_max{0};

void update(int candidate) {
    int observed = g_max.load(std::memory_order_relaxed);
    if (candidate > observed) {
        while (candidate > observed &&
               !g_max.compare_exchange_weak(
                      observed, candidate, std::memory_order_relaxed)) {
            // 走到这里说明有人抢先，observed 已被填成对方的值，回到条件再比
        }
    }
}
```

::: details 跟一次 CAS 失败的完整过程

咱们让线程 A 与 B 同时追最大值，当前的 `g_max` 是 900，按步走一遍坏结局其实不坏的交错：

1. A 读到 observed = 900，摇出了候选值 950。
2. B 也读到 observed = 900，摇出了更大的候选值 980。
3. B 的 CAS 落地在前面：g_max 从 900 换到 980，B 成功退出。
4. A 的 CAS 随后到位：它带的期望还是 900，可 g_max 已经是 980。比较失败，CAS 把 980 写进 A 的 observed，返回 false。
5. A 回到循环条件：950 > 980 不成立，A 退出循环，什么也没写。

两个线程都体面地退了场：B 发布了更大的值，A 认下了别人的成果。咱们没有轮询、也没有加锁，丢掉的数一个也没有。

:::

循环的出口有两个。CAS 成了，最大值就换成了候选值。失败之后 observed 被抬到不低于 candidate 的值，`candidate > observed` 的比较不再成立，咱们发现自己已经不是最大，于是退出了。两个出口都是正确的，漏掉一个大数的事不会发生。

这里还有一条给 CAS 循环兜底的标准条款，值得咱们单独记下：原子 RMW 操作读到的一定是修改全序里的最新值。

> 术语卡咱们补一张，名字咱们叫它**修改全序**（modification order）。同一个原子对象上的全部修改，排成一条属于它自己的全序，全体线程认同的是同一条。每个原子对象都各有自己的一条，这一层的全序彼此不干涉。RMW 被要求在整条全序的队尾读，所以 CAS 失败后 expected 里装的就是此刻的现值，旧快照是进不来的。少了这一重担保，重试就可能在过期的快照上空转。

咱们做八线程各二十万次、值域 0 到 1000000 的完整实测（`mt19937` 种子 1 到 8，每个线程守着自己的种子，抽出的序列完全确定）：

<OnlineCompilerDemo
  title="动手验证：CAS 循环追踪全局最大值"
  source-path="code/examples/vol5/53_cas_max_tracker.cpp"
  description="八个线程各自摇 20 万个 0 到 1000000 的随机数，谁的数大过全局最大值，就用 compare_exchange_weak 的循环发布上去。libstdc++ 下 final max 恒为 1000000：mt19937 的序列固定，CAS 循环担保任何一个大数都不会被漏掉。uniform_int_distribution 的映射是实现定义的，换了标准库可能见到 99999x 一类的值。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者在同一台 WSL2 上的实测：

```text
final max: 1000000
```

一百六十万次抽样抽不中 1000000 的概率本来就有两成上下，种子 1 到 8 在 libstdc++ 的映射下恰好都抽中了，所以笔者复跑多遍输出恒定。您要是换了种子见到 99999x 一类的值，那是概率的事，不是循环的错，它只担保不漏掉任何比当前值大的数。另一个细节也值得咱们记下：全程 relaxed。最大值追踪要的只是单变量自身的原子性，不背任何同步的职责，relaxed 恰好就够用了，这正是[第 0 章](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)说过的单变量计数用 atomic 的完整版。

## happens-before：给次序立法

物证齐了，咱们开始立公理。地基是一个词：求值（evaluation）。标准把程序的执行看成一颗颗求值，一次写、一次读、一次函数调用的动作，咱们都把它叫求值。四层关系全是在求值与求值之间画的箭头，咱们一层层立，每层咱们配一张卡。

### sequenced-before：单线程的执行序

求值 A 排在同一条线程里求值 B 的前面，这一对就是 sequenced-before 的关系。咱们读代码时的上一句、下一句，大体上说的就是它。大体两个字留了余地，因为求值顺序自有暗礁（函数实参谁算谁后这类未指明的缝隙），那归语言规则的细目管，本篇只要单线程的顺序骨架。

> 术语卡咱们立族谱的头一张：**sequenced-before**。它管的是单线程内的执行序，也出不了线程：两条线程各自的 sequenced-before 放在一起，互相之间什么也不担保。您在 [第 2 章 thread_local 那篇](../ch02-mutex-condition-sync/04-thread-local.md)已经按字面用过它：同线程内一个对象的构造 sequenced before 另一个，后者的析构就排在前者的析构之前。

它管不到的地方，正是并发事故的案发现场。两条线程对同一个普通变量做着又写又读的事，而谁也不 sequenced-before 谁，光靠它咱们什么也判不了。跨线程的边，就得靠下一层了。

### synchronizes-with：跨线程的发布与接收

synchronizes-with 是跨线程的同步关系，它的定义权在库手里，标准原话的意思是：某些库调用与别的线程上的另一些库调用之间，存在这样的边。主角咱们选一对：

> 术语卡咱们立族谱的第二张：**synchronizes-with**。线程 A 对原子对象 M 做了一次 release 的写，线程 B 从 M 做了一次 acquire 的读、并且读到了 A 写的那个值，A 的写就 synchronizes-with B 的读。名字咱们读作同步于：A 发布的货，B 真的收到了。

读到那个值的前提咱们要抠字眼。B 读到更新的值也行，读到更旧的值边就不成立了。具体的边咱们列开，您会认出好几位老熟人：

- 原子对：release 的写，与读到它的 acquire 的读（含读改写操作）。
- mutex：同一把锁的放与拿。标准的讲法是，同一把 mutex 上的全部操作排成一条全序，每次加锁读到的是上一次解锁写的值。
- 线程边界：`std::thread` 构造调用完成，synchronizes-with 新线程函数的开头。线程函数完成，synchronizes-with 对应 `join()` 的返回。[第 0 章](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)咱们用直觉认过 join 的边，现在它有名字了。
- C++20 的停止令牌：返回 true 的 `request_stop`，与随后看见 true 的 `stop_requested`，以及回调的注册与回调的执行。这是第 1 章押下的案子，判决在后面。

您回头看刚才的清单，会发现一个共同点：每条边都是一次明确的交接，写方把东西放下、读方确认拿到了。mutex 的拿与放、thread 的生与 join、release 与 acquire，长的都是同一个形状。公理不过问感情，它只认交接的动作。

### happens-before：两边接起来，传递下去

sequenced-before 管的是线程内，synchronizes-with 管的是线程间，happens-before 把两边接成了一张网。C++20 的定义咱们分开写：要认的边一共三种。头一种是 sequenced-before 的边。第二种是 synchronizes-with 的边。第三种边说的是接力：存在一个中间的 X，让 A 排到 X 的前面，而 X 又排到 B 的前面，边就这样一节一节接了下去，传递性说的就是它。（定义里还有 consume 一档的依赖边，本卷就不碰它了：主流的编译器都把 consume 按 acquire 处理，标准自己都劝您别用）

> 术语卡咱们立族谱的第三张：**happens-before**。它由 sequenced-before 与 synchronizes-with 两类边接龙而来，边一节节传了下去，谁排在前面、谁的效果看得见，判据全落在了它身上。data race 三问的第三问，问的就是它肯不肯出面隔开。

效果咱们拿发布-使用的老形状走一遍。咱们让线程 A 写 data、release 发布标志，而线程 B acquire 读到标志、再用 data：

```text
(A) data = 42 ── sequenced-before ──> (B) flag.store(1, release)
                                                │
                                                │ synchronizes-with
                                                v
(D) use(data) <── sequenced-before ── (C) 读到 1
```

咱们把四步接起来：(A) happens-before (D)。它的含义就是第 0 章那句话的正式版：A 的效果对 D 一定可见。标准的可见性条款还补了一个条件：A 与 D 之间不许有别人对 data 的新写插队，满足了它，D 读到的就是 42。编译器与硬件的换序自由，被公理链一段段收走了：任何把 (A) 挪到 (B) 之后、或把 (D) 挪到 (C) 之前的变换，都是会破坏合同的，实现是不许干的。

[第 0 章](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)那道 (A)(B)(C)(D) 的判定题，现在能写全链条了：默认内存序的 store 与读到 true 的 load 之间是 synchronizes-with，两头再各接一段 sequenced-before 的边，接起来 (A) 就 happens-before (D) 了，data race 也就不成立了。练习 1 请您亲手把链条写一遍，再判它的 relaxed 版本。

::: details 把第 0 章那条链写成公理的样子

咱们用四层关系的语言，把第 0 章判定题的链条逐段标出来：

1. (A) 写 value 排在 (B) 发布标志之前，同一条线程里，这段是 sequenced-before。
2. (B) 的默认序 store 与 (C) 读到 true 的默认序 load 之间，读到的正是写下的值，这段是 synchronizes-with。
3. (C) 排在 (D) 用 value 之前，又是 sequenced-before。
4. 三段接力，(A) happens-before (D)，读写之间有了隔断，data race 的三问凑不齐。

您对照第 0 章的直觉版判定读一遍，字句变了，判的是同一件事。标准担保的始终是那两句：A 排在前面发生，A 的效果 D 一定看得到。

:::

### strongly happens-before：更严的一条边

族谱的最后还剩一层。C++20 添了两个新词：simply happens-before 与 strongly happens-before，动因是 consume 与混合内存序把水搅浑了，咱们总得有一条谁也不挑毛病的边。simply happens-before 就是去掉 consume 的 happens-before，strongly 的要求再严一格。演进的事咱们也交代半句：C++26 的 P3475 已经把 simply 一档并回了 happens-before，咱们下面按 C++20 的口径，把 strongly 认的边逐条摆开：

> 术语卡咱们立族谱的最后一张，压轴的术语是 **strongly happens-before**。它认的是四条边。头一条是线程内的 sequenced-before。第二条的要求高一些：synchronizes-with 的两端，都得是 seq_cst 的原子操作。第三条咱们叫它夹心边：A sequenced-before X，X 与 Y 之间隔着一段 simply happens-before 的关系，而 Y 又 sequenced-before B，整串就都算数了。第四条是这些边的传递组合。标准的注脚说，A 对 B 建立了 strongly happens-before 的话，A 在所有场合看起来都排在 B 的前面。

它的岗位不在 data race 的判罚（那用普通的 happens-before），而在给 seq_cst 的总顺序立约束：若 A strongly happens-before B，A 就得排在总顺序 S 里 B 的前面。S 是什么、咱们怎么用它读程序，展开的活儿归[内存序](./03-memory-ordering.md)。四条边里咱们用得最多的是第三条：mutex 的放锁与拿锁、release 与 acquire 的配对，两头的读写各自接一段线程内的 sequenced-before，而同步边夹在中间，靠的正是第三条。所以咱们只要按部就班地用 mutex 加默认序、或者老老实实配 release/acquire，strongly 与普通 happens-before 判下来的读写次序，两边是一致的。您认得名字、知道它是更严的一条边，就够本篇用了。

### 拿公理验一遍自旋锁

押了一节的题，咱们现在开箱。示例 54 临界区里的 counter 是个普通变量，它的读写为什么不是 data race？咱们把链条摆出来。上一个持锁人对 counter 的写 W，排在他自己的 `clear(release)` 之前，所以 W sequenced-before clear。赢家那次 `test_and_set(acquire)` 读到了 false，而 false 正是 clear 写下去的值，所以 clear 对 test_and_set 这一对，synchronizes-with 的关系成立。赢家拿到锁之后的读写 R，而它排在 test_and_set 之后，所以 test_and_set sequenced-before R。咱们把三段接起来，W 就 happens-before R 了。

data race 三问的第三问，就这样答上了：同一个内存位置上，写也在、happens-before 的隔开也在，三问也就凑不齐了，竞争的判定自然不成立。锁的本质在公理层面就一句话：上一段临界区的全部效果，对下一段临界区是可见的。咱们用的 `std::mutex`、自旋锁、`unique_lock`，全是同一条链的不同包装，mutex 的边甚至就是清单里的第二条。您以后怀疑哪把锁灵不灵，拿三段一接就知道了。

## DRF-SC：无竞争的世界有秩序

族谱立完了，第 0 章押下的总担保可以转正。标准在 data race 条款的注脚里写的就是它的正式陈述，咱们译过来：

> 程序若只用 mutex 与默认的 `memory_order_seq_cst` 操作挡住所有数据竞争，而且不使用其他的同步操作，它的行为就如同所有线程的操作被简单地交错展开，每一次取值都取自交错序里它的上一次写入。它就是顺序一致性的正式定义，这个担保也只罩住无竞争的程序，您把这个前提记下。

名字咱们分开读：DRF-SC 说的就是 data-race-free 加 sequential consistency，无竞争的程序享受顺序一致性。无竞争的世界里有秩序，有竞争的世界里没有标准，第 0 章咱们留下的那句话，正式的版本一字不差。

咱们拿它回头验上一篇的 litmus。seq_cst 阶段的所有访存都是默认序的原子操作，整场程序也是无竞争的，于是整场执行有一条全体共见的总顺序，[上一篇](./01-cache-and-reordering.md)那套四条边的反证在总序里原样成立，`(0,0)` 的结局一个都出不来，恒为 0 的两行输出就是公理在背书。relaxed 阶段把担保摘了，`(0,0)` 就冒了头，比例还当不了常数。咱们再往深一层问，默认序凭什么立得住总顺序？因为 seq_cst 除了 release/acquire 的效果，还额外担保所有的 seq_cst 操作排进同一条全序 S。它在 x86 上的代价（排空 store buffer 的指令）与在 ARM 上的代价，是[内存序](./03-memory-ordering.md)的正源。

您再想想前两章写的程序：锁住、读写普通变量、放锁。它们全在 DRF-SC 的辖区里，跑起来与您直觉里的交错执行一模一样。写锁的代码可以按直觉理解，这样的安稳是本篇的公理挣来的。

## 旧案重审：拿公理判四桩悬案

法条立齐了，物证也进了卷，咱们开庭。四桩案子都是前两章押下的，每桩咱们引原悬念，再拿本篇的公理判。

### 双检查锁定的正确终形

[同步原语工具箱](../ch02-mutex-condition-sync/06-sync-primitives-toolkit.md)那篇记下过：DCLP 是 double-checked locking pattern 的缩写，门外查一眼、拿了锁门里再查一眼，为的是跳过每次都拿锁的开销。C++11 之前它是出名的易错区，内存序没有正式的定义，写对了也靠运气，正确写法得等 happens-before 立了法才说得清。现在说得清了。为什么旧标准修不好它？Meyers 与 Alexandrescu 在 2004 年的论文里把各路写法挨个验过一遍：声明可变的指针、双层锁、volatile 上加锁，全都是堵不住的。病根咱们现在已经认得：没有正式的内存模型，就没有一条跨线程的可见性承诺，编译器与 CPU 的重排谁也管不住。C++11 把公理立了起来，`atomic` 与内存序进了标准，这桩公案才算结了案。

示例 55 的手写版咱们整段看：

```cpp
class ManualDclp {
    inline static std::atomic<ManualDclp*> instance_{nullptr};
    inline static std::mutex ctor_mutex_;

    ManualDclp() { std::cout << "ManualDclp constructed once\n"; }

public:
    static ManualDclp* get() {
        // 第一道检查：多数调用走到这里就返回，不碰锁。
        // acquire 与门里的 release store 配对：看见非空指针，就看见了完整的构造
        ManualDclp* p = instance_.load(std::memory_order_acquire);
        if (p == nullptr) {
            std::lock_guard<std::mutex> guard(ctor_mutex_);
            // 第二道检查：拿锁后再看一眼，先抢到锁的那个线程已经把活干完了
            p = instance_.load(std::memory_order_relaxed);
            if (p == nullptr) {
                p = new ManualDclp();
                // release 发布：构造的写先落地，指针后可见
                instance_.store(p, std::memory_order_release);
            }
        }
        return p;
    }
};
```

四步咱们挨个判。门外快查用 acquire：它与门内的 release store 配对，读到非空指针的线程就和构造它的线程建立了 synchronizes-with，构造函数的全部写 happens-before 您对成员的访问，您拿到的指针背后是一个完整的对象。这也就是它不能用 relaxed 的原因：relaxed 建立不了所说的边，您可能看见指针、读到半成品。门内二查用 relaxed 就够：mutex 的边已经把抢到锁的线程干完的活送过来了，再多花一道 acquire 就是白花了。release 发布担保 `new ManualDclp()` 的全部写排在指针可见之前，少了它，[上一篇](./01-cache-and-reordering.md)的两种自由随时能把构造与发布分开，x86 上 StoreStore 是硬件白送的，写出来的程序常常碰巧不出事，不过合同不认它，这正是上一篇说过的、在每个平台上分别看起来对的病历，如今有了 DCLP 版。

咱们开八线程同时抢初始化，做手写版与块级 static（Meyers 单例）的对照。场景与预期是这样的：

<OnlineCompilerDemo
  title="动手验证：手写 DCLP 与块级 static 的八线程抢初始化"
  source-path="code/examples/vol5/55_dclp_final_form.cpp"
  description="八个线程同时抢两种单例：手写 DCLP（门外 acquire 快查 + 锁内二查 + release 发布）与块级 static。两行 constructed once 各出现一次，两个 single address 都是 yes。两行构造消息的顺序也不是碰运气，判据在正文。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者在同一台 WSL2 上的实测：

```text
ManualDclp constructed once
MeyersSingleton constructed once
manual dclp single address: yes
meyers singleton single address: yes
```

四行输出咱们挨行读。构造各只发生了一次，八线程拿到的都是同一个地址，说明两版都对了。头两行的顺序也不是碰运气：任何线程要走到 Meyers 的构造，都得排在自己那次 `ManualDclp::get()` 的后面，而那次调用的背后是构造完成才有的 release 发布，本篇立的公理连输出的顺序都在背书。

那手写版还有没有活路？工具箱那篇留了一句单例的场合别再手写，现在咱们能说清为什么。块级 static 的担保由标准给出：并发下初始化恰好一次，迟到的线程等它完成。cppreference 的注脚还写明，实现通常就生成 DCLP 的变体。快路径的样子，笔者拿 `-O2 -S` 看过示例 55 的生成代码：初始化做完了之后，Meyers 单例的入口只剩 guard 字节（初始化守卫）的一次普通读，比您手写 atomic 指针的 acquire 读还轻一档。单例的场合用块级 static，任意的函数要恰好一次就用 `call_once`，两样[工具箱](../ch02-mutex-condition-sync/06-sync-primitives-toolkit.md)都教过了。手写 DCLP 的价值在形状而不在单例：延迟初始化一个大对象、按需加载一份配置，凡是检查、加锁、再检查、干活的场合，配方咱们已经写过：指针换成 atomic、门外用 acquire、门内用 release，四步的每一步都有公理背书。

### shared_ptr 的引用计数为什么安全

[第 1 章](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md)留过一则注脚：控制块里的引用计数增减是原子的，这样的安全只罩得住计数，罩不住被指对象的内容，细节的判决就在下面。咱们分两问判。

计数为什么乱不了？因为增减都是同一个原子对象上的 RMW。咱们让两条线程同时减，修改全序也会让两次减各就各位地落了队，谁的一次也不会丢。这与内存序是无关的，给出它的是原子性本身，relaxed 的 `fetch_add` 也一样有。

内存序的配方为什么是增用 relaxed、减用 acq_rel？cppreference 在 memory_order 页写得很直白，大意是：引用计数的增加是 relaxed 的典型用场，它要的只是原子性，次序是不要的。而减计数需要与析构之间的 acquire-release 同步。咱们拿公理把后半句读懂。每个持有者对对象的全部访问，都排在他自己的减计数之前，这就是减法 release 那一侧给的边。减到零的那次带 acquire，它顺着修改全序接到了前面每一次减法，于是前面所有持有者的访问全部 happens-before 析构，析构线程看到的对象是完整的。两句话合起来：计数归零的那一刻，世界上持引用的人已经没有了，此前每个人的痕迹也都可见，删干净、看完整的两件事，它一次就办齐了。真实的 `shared_ptr` 控制块，各家实现小异大同：有的直接用 `acq_rel` 的 `fetch_sub`，有的用 release 减法加一道 acquire 围栏，围栏一类的路数是[等待与模式篇](./04-fence-wait-and-patterns.md)的活。

罩不住内容的那半句，判词倒是简单。咱们让两个线程各持一份拷贝、并发读写同一个 `vector`，计数倒是安好无损，vector 上的访问照样是 data race。计数担保的是生命周期，内容上谁也没替您同步，这正是第 1 章那则注脚的公理版。

### cv 的谓词变量为什么必须持锁改

[condition_variable 那篇](../ch02-mutex-condition-sync/05-condition-variable-and-bounded-queue.md)引过 cppreference 的规范：哪怕共享变量是 atomic 的，也必须在持有 mutex 的情况下修改，才能把修改正确地发布给等待的线程。当时只要您认下规范，判据咱们留给本篇。案子的谜面在等待协议的时序。

`wait(lock)` 干的是一件事的两半：原子地放锁并入睡。原子两个字是公理级的担保：锁放出去的那一刻，等待者已经挂在这个 cv 的等待队列上，之后的任何 notify 都看得见它。危险恰恰出在查谓词与挂上队列之间的缝隙，咱们把丢唤醒的坏结局按步摆开：

::: details 不持锁改谓词，一次丢唤醒的时间线

咱们让等待者 W 与通知者 N 赛跑，而 N 不拿锁，坏结局咱们按步走一遍：

1. W 持锁查谓词，得到 false，于是决定去睡。
2. W 还没挂上等待队列。此刻锁还在 W 手里，可 N 根本不需要锁。
3. N 把谓词改成 true，调 notify。队列上空无一人，notify 扑了个空。
4. W 挂上队列，放锁，入睡。
5. W 查过条件了，没有人再叫它。它要等到一次虚假唤醒，或一次与 N 无关的 notify，才有机会重查谓词。

一次唤醒就这么丢了。丢唤醒的窗口只有两步宽，可高并发的程序里咱们每秒走上亿步，窄的窗口照样是常客。

:::

持锁改谓词为什么治它？因为谓词的改与查被同一把 mutex 串了起来，交错的情形只剩两种。咱们看头一种交错：N 改在 W 查之前，W 查到的就是 true，干脆不睡了。咱们再看另一种：W 查在了前头，N 要改的话就得等锁，而锁在 W 手里要一直握到 `wait` 原子地放锁入睡那一刻，W 挂上队列的事排在放锁之前，N 这边拿到锁、改完、notify 的时候，队列上一定是有 W 的。两种交错都是安全的，丢唤醒的那个缝隙已经不存在了。

判词的公理版咱们也写出来。W 的挂队动作 sequenced-before 放锁，放锁 synchronizes-with N 的拿锁，N 的拿锁 sequenced-before notify，接起来 W 的注册 happens-before N 的通知，所以 notify 必然看得见等待者。谓词的可见性走另一段链：N 持锁改谓词、放锁，被唤醒的 W 会在 `wait` 返回前重新拿锁，于是谓词的改 happens-before 复查，W 看到的一定是改完以后的值。您单给谓词变量换成 atomic，换来的只是单次改写的不可分割，上面的两条链一条都接不起来，丢唤醒的窗口就这样敞着。锁在这里管的从来不只是互斥，它管的是等待协议的时序本身。

### request_stop 与 stop_requested 的同步边

[thread 所有权那篇](../ch01-thread-lifecycle-raii/03-thread-ownership-and-jthread.md)引过规范的同步子句：真正发出请求的那次 `request_stop`（返回 true 的那次）与看见它的 `stop_requested` 之间、回调注册与回调执行之间，都有 synchronizes-with 的保证，正源写的正是本篇。原文咱们现在给全：返回 true 的 `request_stop`，与随后在关联令牌上看见 true 的 `stop_requested` 之间，synchronizes-with 就成立了。回调的注册，synchronizes-with 回调的执行。

拿公理翻译成人话：请求线程里排在 `request_stop` 之前的全部写，happens-before 的是工作线程里，排在看见 true 的 `stop_requested` 之后的全部读。这对您意味着一件实惠的事：停止信号周围可以带数据。您在主线程写好一份普通结构体，咱们不给它 atomic，写完了才调 `request_stop`。等 `stop_requested()` 变了 true，工作线程去读它的时候，中间的公理边把 data race 隔掉，可见性也一并担保了。`jthread` 析构里的那次 `request_stop` 同理，析构前的准备对收到停止信号的工作线程可见。

半句边界咱们也要说清：可见性的边是单向的。工作线程自己的写，对 `request_stop` 之后的请求线程没有担保，反方向想传东西的话，咱们就用 join，它给的边是线程完成 synchronizes-with join 返回，或者用别的同步动作也行。停止令牌给您的是一根从请求方指向停止方的单向边，方向您别记反。

## 练习

三道题咱们照旧排梯度：写链条、改现场、自己动手。建议您全做。

### 练习 1：把第 0 章的判定链写全

咱们回到那道 (A)(B)(C)(D) 的老题，四个字母分别是 `value = 42`、`ready.store(true)`、循环里读到 true 的 `ready.load()`、`std::cout << value`。您拿本篇的四层关系把链条写全：哪几步是 sequenced-before，哪一步是 synchronizes-with（B 与 C 之间成立的前提是什么），末了给出 A happens-before D 的完整接力。然后判第 0 章留过的进阶版：把 (B)(C) 都换成 `memory_order_relaxed` 再判一次。预期的答案：链条断在 B 与 C 之间，relaxed 建立不了 synchronizes-with，(A) 与 (D) 之间也就接不上了，value 上的读写成了没人隔开的竞争，TSan 也会报的。您判完就跑一遍第 0 章的 TSan 流程对答案。

### 练习 2：把自旋锁的内存序改松再判

您把示例 54 另存一份到 `/tmp/spin_relaxed.cpp`，咱们改三处内存序：读用的 `test`、抢锁的 `test_and_set`、放锁的 `clear`，三处全换成 relaxed 的档位。锁还在转着的样子，问题其实出在合同。两遍实验请您做：

```bash
g++ -std=c++20 -O2 -pthread -o /tmp/spin_relaxed /tmp/spin_relaxed.cpp
/tmp/spin_relaxed
g++ -std=c++20 -O1 -g -fsanitize=thread -pthread \
  -o /tmp/spin_relaxed_tsan /tmp/spin_relaxed.cpp
/tmp/spin_relaxed_tsan
```

预期的对照是这样的。普通构建里 counter 照样给的是 400000，因为 x86 把 relaxed 需要的次序白送了，上一篇 TSO 的对比表里，StoreStore、LoadStore 的两栏都写着不换。TSan 构建里报告把 counter 上的竞争指了出来，Location 就是全局的 counter。请您解释这对矛盾：机器给的与合同给的为什么不一样，再想想这把锁搬到 ARM 一类的弱序机器上会漏成什么样。两句话的口径：普通构建碰巧全对，合同上它已经是未定义行为了，换了机器或换了编译器版本，它就未必还是对的。

### 练习 3：最小值追踪器

咱们把示例 53 改造成最小值追踪器：初值换成值域上界（比如 `std::numeric_limits<int>::max()`），比较的方向反过来，CAS 循环的条件跟着换向。四个线程、种子您自定，跑出最终的最小值并核对。两问请您答。头一问：fetch 系里为什么没有现成的 fetch_min 可用？眼下（C++20 及更早）标准只给了算术与位运算的 fetch，max、min 这类比较式的 RMW 只能靠 CAS 循环手写，`atomic::fetch_max` 与 `fetch_min` 是 C++26 才写进标准的。第二问：这个循环里用 weak 还是 strong？您拿本篇的口径答，再用汇编验证一下您手头的机器上两者是不是一个价。

## 本篇收进包里的东西

- atomic<T> 的操作集：整型与指针全配 fetch 系，别的平凡可拷贝类型走 load、store、exchange、CAS 的通用路子。fetch 返回旧值，运算符返回新值，CAS 失败时 expected 被覆盖成最新值。
- weak 允许虚假失败，源头是 LL/SC 架构的监视被打断。x86 的 lock cmpxchg 没有这回事，两版机器码逐指令相同（本机实测）。循环里用 weak，单发用 strong。
- 原子不等于无锁：is_lock_free 在运行时问对象，is_always_lock_free 在编译期断言，ISR 里用 atomic 务必上断言。atomic_flag 是唯一标准担保永远无锁的类型，自旋锁是它的教学正主。
- 原子性不传染：单次访问原子，多次访问之间照样可以交错。volatile 更是连原子都不给，线程同步两头都不沾。
- 四层关系：sequenced-before 管单线程，synchronizes-with 管跨线程交接，happens-before 接力传递，strongly happens-before 是更严的边，给 seq_cst 总顺序立约束。
- DRF-SC：无竞争加默认序，程序享受顺序一致性，上一篇 (0,0) 恒为零的判据由此转正。
- 四桩判决：DCLP 四步各有公理背书，单例场合块级 static 包办。引用计数增用 relaxed、减用 acq_rel，罩得住生命周期罩不住内容。cv 谓词必须持锁改，不然丢唤醒。request_stop 给一根从请求方指向停止方的单向可见性边。

## 下一步

公理立完了，配方的事还没发。六种内存序各自担保什么、配对怎么配、在 x86 与 ARM 上各花什么指令代价，发配方的活儿归[内存序](./03-memory-ordering.md)。fence 这套手动挡与 atomic wait 的等待模式，咱们留给[等待与模式篇](./04-fence-wait-and-patterns.md)。动手量更大的活儿在[练习体系](../exercises/)候着，全卷的走向见[卷地图](../)。机器那一路的现场，[缓存与重排](./01-cache-and-reordering.md)随时欢迎您回去复看。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch03-atomic-memory-model/`。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="C++ 标准草案"
    title="[intro.races] 数据竞争与求值次序条款"
    url="https://eel.is/c++draft/intro.races"
    chapter="四层关系、修改全序与 data race 判决的原文，DRF-SC 的正式陈述在条款注脚"
  />
  <ReferenceItem
    :id="2"
    author="cppreference"
    title="std::memory_order"
    url="https://en.cppreference.com/w/cpp/atomic/memory_order"
    chapter="关系族谱的展开讲法，引用计数增 relaxed 减 acquire-release 的那句注"
  />
  <ReferenceItem
    :id="3"
    author="cppreference"
    title="std::atomic::atomic"
    url="https://en.cppreference.com/w/cpp/atomic/atomic/atomic"
    chapter="操作集、平凡可拷贝要求、LWG 2334 默认构造值初始化"
  />
  <ReferenceItem
    :id="4"
    author="cppreference"
    title="std::atomic::compare_exchange_weak / strong"
    url="https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange"
    chapter="虚假失败与 weak/strong 的选型口径"
  />
  <ReferenceItem
    :id="5"
    author="cppreference"
    title="std::atomic_flag"
    url="https://en.cppreference.com/w/cpp/atomic/atomic_flag"
    chapter="永远无锁的担保，test() 自 C++20"
  />
  <ReferenceItem
    :id="6"
    author="C++ 标准草案"
    title="[thread.stoptoken] 停止令牌条款"
    url="https://eel.is/c++draft/thread.stoptoken"
    chapter="request_stop 与 stop_requested、回调注册与执行的同步子句"
  />
  <ReferenceItem
    :id="7"
    author="Scott Meyers, Andrei Alexandrescu"
    title="C++ and the Perils of Double-Checked Locking"
    :year="2004"
    url="https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf"
    chapter="DCLP 在旧标准下为什么修不好，本篇判决的历史卷宗"
  />
  <ReferenceItem
    :id="8"
    author="Jeff Preshing"
    title="Double-Checked Locking is Fixed in C++11"
    :year="2013"
    url="https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/"
    chapter="acquire 快查与 release 发布那条链的展开讲法"
  />
  <ReferenceItem
    :id="9"
    author="Anthony Williams"
    title="C++ Concurrency in Action, 2nd ed"
    publisher="Manning"
    :year="2019"
    url="https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition"
    chapter="第 5 章内存模型，与本篇公理族谱同源的展开"
  />
</ReferenceCard>
