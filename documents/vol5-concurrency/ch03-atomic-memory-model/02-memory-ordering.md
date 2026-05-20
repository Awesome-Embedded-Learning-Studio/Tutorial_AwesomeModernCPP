---
title: "内存序详解"
description: "从编译器重排到 CPU 重排，逐个拆解六种 memory_order 与 happens-before 关系"
chapter: 3
order: 2
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - memory_order
difficulty: advanced
platform: host
reading_time_minutes: 25
cpp_standard: [11, 14, 17, 20]
prerequisites:
  - "atomic 操作"
related:
  - "fence 与编译器屏障"
  - "原子操作模式"
---

# 内存序详解

上一篇我们完整地拆解了 `std::atomic<T>` 的操作集——load、store、fetch_add、compare_exchange，一口气用下来默认参数就能跑。但你有没有注意到，几乎每个原子操作都有一个可选的 `std::memory_order` 参数？很多人（包括当年的笔者）都是直接无视它，反正默认值能工作嘛。

事情在简单场景下确实如此。但一旦你开始用原子变量做线程间的同步——一个线程写数据、另一个线程读数据——各种灵异现象就冒出来了：明明先写的数据，对方线程就是看不到；两个线程观察到的操作顺序完全对不上。问题不在原子操作本身，而在于**编译器和 CPU 都在背着你重排指令**，而内存序（memory order）就是你控制这种重排的工具。

这一篇我们把六种 `memory_order` 逐个拆开，搞清楚每种序保证什么、不保证什么，以及什么时候该用哪一种。

## 为什么要重排：编译器优化与 CPU 优化

在深入六种内存序之前，我们必须先理解一个基本事实：你写的代码顺序，和 CPU 实际执行的顺序，可能不是一回事。这不是 bug，而是性能优化的必然结果。

编译器在优化阶段会做指令重排。编译器看到两段互不依赖的代码时，可能把它们交换顺序——比如对两个不同变量的写入，编译器觉得谁先谁后不影响单线程语义，就可能调换。考虑下面这个经典的例子：

```cpp
int data = 0;
bool ready = false;

// 线程 1
data = 42;         // 步骤 A
ready = true;      // 步骤 B

// 线程 2
if (ready) {       // 步骤 C
    use(data);     // 步骤 D
}
```

在单线程视角下，A 和 B 的顺序无关紧要（`data` 和 `ready` 之间没有数据依赖）。编译器完全可能把 B 排到 A 前面。但在多线程视角下，这就意味着线程 2 可能看到 `ready == true` 但 `data` 还是 0——它以为数据准备好了，其实没有。

CPU 层面同样存在乱序执行。现代 CPU 都是超标量、深度流水线的设计，为了填满流水线、减少停顿，硬件会动态调整指令的执行顺序。x86 有很强的内存模型（TSO，Total Store Ordering），只允许 store-load 重排；ARM、PowerPC 的内存模型弱得多，允许 store-store、load-load、store-load、load-store 全部重排。同样的代码在 x86 上跑正常，在 ARM 上可能就出问题——这就是为什么 C++ 标准要定义一套平台无关的内存模型。

总结一下：编译器重排是为了指令调度和寄存器分配的效率，CPU 重排是为了流水线吞吐率。两者都是对单线程语义"透明"的——在单线程程序里，不管怎么重排，最终结果都不变（as-if 规则）。但多线程程序依赖的不仅仅是最终结果，还有操作之间的**可见性顺序**，而重排恰恰破坏了这种顺序。

## 六种内存序一览

C++ 定义了六种内存序，定义在 `std::memory_order` 枚举中。从弱到强排列如下。其中 `memory_order_consume` 在 C++17 中被标注为"建议不使用"，C++26 正式弃用，实际中主流编译器都把它当成 `memory_order_acquire` 处理，我们会在后面简单提及但不深入讨论。

- `memory_order_relaxed`：只保证原子性，不提供任何排序约束。
- `memory_order_consume`：数据依赖排序（已弃用，用 acquire 代替）。
- `memory_order_acquire`：用于 load 操作，保证后续的读写不能重排到此 load 之前。
- `memory_order_release`：用于 store 操作，保证之前的读写不能重排到此 store 之后。
- `memory_order_acq_rel`：用于 read-modify-write 操作，同时具有 acquire 和 release 语义。
- `memory_order_seq_cst`：默认值，最强保证，所有 seq_cst 操作存在一个全局一致的总顺序。

接下来我们逐个展开。

## memory_order_relaxed：只保原子性

`memory_order_relaxed` 是最轻量的内存序。它保证操作本身是原子的——不会有撕裂读（torn read）或撕裂写（torn write），不同线程不会看到中间状态。但它**不保证任何操作间的排序**，也就是说，编译器和 CPU 可以自由地把 relaxed 操作和前后的其他操作重排。

一个典型场景是纯计数器。你只关心计数器的最终值，不关心计数操作和其他操作之间的相对顺序：

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

std::atomic<int> request_count{0};
std::atomic<int> error_count{0};

void handle_request()
{
    request_count.fetch_add(1, std::memory_order_relaxed);
    // ... 处理请求 ...
}

void log_error()
{
    error_count.fetch_add(1, std::memory_order_relaxed);
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([]() {
            for (int j = 0; j < 100000; ++j) {
                handle_request();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "Total requests: " << request_count.load(
                     std::memory_order_relaxed) << "\n";
    // 输出：Total requests: 400000
    return 0;
}
```

relaxed 的危险在于：你不能用它来做线程间同步。很多新手会犯这样的错误——用 relaxed 的 store/load 组合来做"数据准备好了"的标志：

```cpp
// 危险示例：用 relaxed 做同步
std::atomic<bool> data_ready{false};
int data = 0;

// 线程 1：生产者
data = 42;
data_ready.store(true, std::memory_order_relaxed);

// 线程 2：消费者
if (data_ready.load(std::memory_order_relaxed)) {
    // data 可能还是 0！
    use(data);
}
```

为什么不对？因为 `memory_order_relaxed` 不阻止重排。编译器或 CPU 可能把 `data_ready.store(true)` 重排到 `data = 42` 之前。从线程 2 的视角看，`data_ready` 变成 true 了，但 `data` 还是旧值。要用标志位做同步，必须用 acquire-release——这正是下一节的内容。

## memory_order_acquire 与 memory_order_release：同步的黄金搭档

acquire 和 release 是最常用的一对内存序，它们合在一起构成了线程间同步的基本机制。理解这一对是理解整个内存模型的关键。

### release：写入时的"发布"语义

`memory_order_release` 用于 store 操作。它保证：**在这个 store 之前的所有读写操作（不管是原子的还是非原子的），都不会被重排到这个 store 之后**。可以把它理解为一个"发布"动作——在这个 store 之前的所有准备工作都完成了，现在正式发布出去。

```cpp
int data = 0;
std::atomic<bool> ready{false};

// 线程 1：生产者
data = 42;                                  // 准备数据
ready.store(true, std::memory_order_release); // 发布：保证 data 先写入
```

release 的 store 就像是一封已经封好口的信——信里的内容（之前的所有写入）在封口之前就已经写好了，不会有任何内容在封口之后才塞进去。

### acquire：读取时的"订阅"语义

`memory_order_acquire` 用于 load 操作。它保证：**在这个 load 之后的所有读写操作，都不会被重排到这个 load 之前**。更重要的是，如果一个线程用 acquire 读取到了另一个线程用 release 写入的值，那么写入线程在 release 之前做的所有写入，对读取线程都可见。

```cpp
// 线程 2：消费者
if (ready.load(std::memory_order_acquire)) {  // 订阅
    // 一定能看到 data == 42
    use(data);
}
```

acquire 的 load 就像是拆信封——你只有在拆开封口之后才能读信。拆封之后你看到的内容，一定是寄信人在封口之前写进去的。

### synchronizes-with 与 happens-before

现在我们可以引入 C++ 内存模型中最核心的关系了。当线程 A 执行了一次 release store，线程 B 执行了一次 acquire load 并且读到了线程 A 写入的值时，我们就说线程 A 的 store **synchronizes-with** 线程 B 的 load。

synchronizes-with 关系会建立 **happens-before** 关系：线程 A 在 release store 之前执行的所有操作，都 happens-before 线程 B 在 acquire load 之后执行的所有操作。happens-before 的含义是：前面的操作对后面的操作**可见**。

这个链条可以进一步延伸。如果一个操作 A happens-before 操作 B，操作 B happens-before 操作 C，那么 A 也 happens-before C——这是传递性。在多线程环境中，这种传递性通过 **inter-thread-happens-before** 关系来建立，它把同一个线程内的 sequenced-before 关系（程序顺序）和跨线程的 synchronizes-with 关系串联起来，形成完整的"可见性链"。

回到我们的例子：`data = 42` sequenced-before `ready.store(true, release)`（同一线程内），`ready.store(true, release)` synchronizes-with `ready.load(acquire)` == true（跨线程），`ready.load(acquire)` sequenced-before `use(data)`（同一线程内）。通过传递性，`data = 42` happens-before `use(data)`——所以 `use(data)` 一定能看到 `data == 42`。

### message passing 模式

acquire-release 最经典的应用就是 message passing 模式：一个线程准备数据，然后通过一个原子标志通知另一个线程"数据准备好了"。

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <string>

struct Message {
    int id;
    std::string content;
};

Message msg;
std::atomic<bool> ready{false};

void producer()
{
    msg.id = 1;
    msg.content = "Hello from producer";
    // release：保证上面的赋值在 store 之前完成
    ready.store(true, std::memory_order_release);
}

void consumer()
{
    // 自旋等待，直到看到 ready == true
    while (!ready.load(std::memory_order_acquire)) {
        // 在实际代码中可以加 yield 或 sleep 避免纯自旋
    }
    // 此时一定能看到完整的 msg
    std::cout << "Received message #" << msg.id
              << ": " << msg.content << "\n";
}

int main()
{
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

注意 `msg` 本身不是原子变量——它是一个普通的 `Message` 对象。但 acquire-release 建立的 happens-before 关系保证了 `consumer` 在读到 `ready == true` 之后，一定能看到 `producer` 写入的完整 `msg`。这就是内存序的威力：通过同步一个原子变量，间接同步了它周围的所有非原子数据。

## memory_order_acq_rel：读-改-写操作的双向保证

`memory_order_acq_rel` 用于 read-modify-write（RMW）操作——比如 `fetch_add`、`exchange`、`compare_exchange`。这类操作同时涉及读取和写入，所以它同时具有 acquire 和 release 的语义：acquire 保证这个 RMW 之后的操作不会被重排到它之前，release 保证这个 RMW 之前的操作不会被重排到它之后。

```cpp
std::atomic<int> counter{0};

// acq_rel：同时具有 acquire 和 release 语义
int old = counter.fetch_add(1, std::memory_order_acq_rel);
```

什么时候需要 `acq_rel`？最典型的场景是引用计数。`fetch_sub` 减到 0 时需要销毁对象——acquire 保证你能看到对象完整的构造结果，release 保证之前的所有使用都发生在减引用之前：

```cpp
class RefCounted {
public:
    void add_ref()
    {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release()
    {
        // acq_rel：减引用同时保证可见性
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // 最后一个引用被释放，安全销毁
            delete this;
        }
    }

protected:
    virtual ~RefCounted() = default;

private:
    std::atomic<int> ref_count_{1};
};
```

## memory_order_seq_cst：默认的全局总顺序

`memory_order_seq_cst`（sequentially consistent）是所有原子操作的默认内存序，也是最强的保证。它在 acquire-release 的基础上增加了一个额外约束：**所有 `seq_cst` 操作之间存在一个全局一致的总顺序（single total order）**——所有线程看到的 `seq_cst` 操作的执行顺序是相同的。

这意味着什么？考虑一个涉及多个原子变量的场景：

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// 线程 1
x.store(1, std::memory_order_seq_cst);

// 线程 2
y.store(1, std::memory_order_seq_cst);

// 线程 3
int r1 = x.load(std::memory_order_seq_cst);
int r2 = y.load(std::memory_order_seq_cst);

// 线程 4
int r3 = y.load(std::memory_order_seq_cst);
int r4 = x.load(std::memory_order_seq_cst);
```

如果用的是 `seq_cst`，那么不可能出现"线程 3 看到 `r1 == 1, r2 == 0`（x 先变）同时线程 4 看到 `r3 == 1, r4 == 0`（y 先变）"的情况。因为 `seq_cst` 保证了所有线程对 x 和 y 的修改顺序达成一致——要么全局认为 x 先变，要么全局认为 y 先变。

如果换成 `acquire-release`，这个一致性就不保证了。acquire-release 只在配对的 load/store 之间建立 synchronizes-with 关系，但不会对不同原子变量之间的顺序做全局约束。在需要多个原子变量协同的场景下，`seq_cst` 是最安全的选择。

代价是什么呢？在 x86 上代价很小——x86 的 TSO 模型本身就很强，`seq_cst` 的 store 只需要一条 `MFENCE` 或 `LOCK XCHG` 指令。但在 ARM、PowerPC 这种弱内存模型的架构上，`seq_cst` 需要完整的内存屏障（ARMv8 的 `DMB ISH`，PowerPC 的 `sync`），性能开销可能是 `relaxed` 的 3 到 6 倍。

一个实用的原则：**先用 `seq_cst`，能跑且性能满意就别动它**。只有当你有明确的性能瓶颈、且通过 profiling 确认原子操作是瓶颈所在时，才考虑降级到 acquire-release 甚至 relaxed。过早优化内存序是并发编程中隐蔽的错误来源。

## memory_order_consume：已被 C++26 弃用的依赖序

`memory_order_consume` 原本的设计意图是比 `acquire` 更轻量：它只保证依赖于这个 load 值的操作不会被重排到此 load 之前，而不依赖这个值的操作不受约束。这在发布指针的场景下理论上比 `acquire` 更高效——你只需要保证通过指针访问的数据是正确的，不需要同步所有其他内存操作。

但现实中，没有主流编译器真正实现了 consume 的精确语义。编译器要做依赖链追踪是非常困难的，所以 GCC 和 Clang 都把 `consume` 提升为 `acquire` 处理。C++17 将 `consume` 标记为"建议不使用"，实践中直接用 `acquire` 即可。

## 每种序何时使用：实用指南

到这里我们已经逐个拆解了所有内存序。下面这个实用的决策流程可以帮助你在实际编码中做出选择。

**纯计数器、统计量、指标**：用 `memory_order_relaxed`。你只关心最终数值的准确性，不关心它和其他操作之间的顺序。

**一个线程写数据、另一个线程读数据**（message passing 模式）：写端用 `memory_order_release`，读端用 `memory_order_acquire`。这是最常见也最需要掌握的模式。

**引用计数、信号量等 RMW 操作**：用 `memory_order_acq_rel`。减引用到 0 时需要销毁对象，必须同时看到完整对象状态（acquire）并确保之前的所有访问已完成（release）。

**多个原子变量需要协同**：用 `memory_order_seq_cst`。如果你不确定该用什么，也先用 `seq_cst`。

**绝对不要用 `memory_order_consume`**：用 `acquire` 代替。

一个更简洁的经验法则是：当你能在代码里明确指出"这里需要 synchronizes-with 那里"的时候，用 acquire-release；当你需要"所有线程对所有原子操作达成一致的顺序"的时候，用 seq_cst；当你不需要任何同步只关心原子性本身的时候，用 relaxed。

## 练习

### 练习 1：消息传递实验

编写一个程序，验证 acquire-release 同步的正确性。创建两个线程：生产者线程写入一个非原子变量 `int payload`，然后用 release 语义 store 一个 `std::atomic<bool> ready`；消费者线程用 acquire 语义 load `ready`，读到 true 后读取 `payload`。确认消费者总是能看到正确的 payload 值。

然后，把两端的内存序都改成 `memory_order_relaxed`，在高并发下反复运行。你是否能观察到 payload 读到旧值的情况？（提示：在 x86 上很难复现，因为 x86 的硬件模型比 relaxed 更强。你可以尝试在 ARM 设备或使用 ThreadSanitizer 来增加复现概率。）

```cpp
#include <atomic>
#include <thread>
#include <iostream>

int payload = 0;
std::atomic<bool> ready{false};

void producer()
{
    payload = 42;
    ready.store(true, std::memory_order_release);
}

void consumer()
{
    while (!ready.load(std::memory_order_acquire)) {}
    std::cout << "payload = " << payload << "\n";
}

int main()
{
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

### 练习 2：relaxed 与 acquire-release 的行为对比

编写一个程序，使用两个原子变量 `x` 和 `y`（均初始化为 0）。线程 1 对 x 和 y 分别 store 1；线程 2 读取 y 和 x（先读 y 再读 x）。使用两种配置运行：

1. 所有操作使用 `memory_order_relaxed`。
2. 所有操作使用 `memory_order_seq_cst`。

在循环中反复执行（比如 100 万次），统计线程 2 看到 `y == 1 && x == 0` 的次数。理论上，在 relaxed 模式下这种情况可能出现（因为两个 store 之间没有排序约束），而在 seq_cst 模式下不应该出现。注意：x86 上很难观察到差异，这个实验更适合在弱内存模型架构上运行。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch03-atomic-memory-model/`。

## 参考资源

- [std::memory_order -- cppreference](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [C++ Standard Draft [intro.multithread] -- eel.is](https://eel.is/c++draft/intro.multithread)
- [C++ Concurrency in Action, 2nd Edition -- Anthony Williams, Chapter 5](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Herb Sutter: atomic Weapons -- CppCon 2012](https://www.youtube.com/watch?v=A8e5OjAVHEA)
- [Memory Ordering in Modern Microprocessors -- Paul E. McKenney](https://www.linuxjournal.com/content/memory-ordering-modern-microprocessors-part-i)
