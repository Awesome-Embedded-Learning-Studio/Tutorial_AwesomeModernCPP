---
title: "fence 与编译器屏障"
description: "atomic_thread_fence、编译器屏障与 CPU 屏障的原理，辨析 volatile 的边界与常见误用"
chapter: 3
order: 3
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - memory_order
difficulty: advanced
platform: host
reading_time_minutes: 20
cpp_standard: [11, 14, 17, 20]
prerequisites:
  - "内存序详解"
related:
  - "atomic_wait 与 atomic_ref"
  - "原子操作模式"
---

# fence 与编译器屏障

上一篇我们花了大量篇幅拆解 `memory_order` 的六种级别——从 `relaxed` 到 `seq_cst`，每一步都是在"编译器/CPU 可以怎么重排"和"我们需要什么保证"之间画线。但你有没有注意到，前面所有的同步都是"绑"在某个原子操作上的？`store(..., memory_order_release)` 和 `load(..., memory_order_acquire)` 成对出现，release 绑定写入，acquire 绑定读取。

问题来了：如果我们只想控制重排行为，不想在任何具体的原子变量上做 store 或 load 呢？换句话说，能不能把"禁止重排"这个约束单独拎出来，跟原子操作解耦？

这就是 `std::atomic_thread_fence` 的用武之地——一个独立于原子操作的内存屏障。它告诉编译器和 CPU："在这个点之前和之后的内存操作，别乱动它们的顺序。" 再加上一个更低调的兄弟 `std::atomic_signal_fence`（只管编译器重排，不管 CPU 重排），以及更底层的内联汇编屏障和平台相关指令，就构成了我们在 C++ 中控制内存顺序的全部工具箱。

这一篇我们把这些工具从上到下捋一遍：标准库 fence 的语义、编译器屏障与 CPU 屏障的区别、x86 和 ARM 的底层屏障指令，最后还要澄清一个几乎每个 C++ 程序员都踩过的坑——`volatile` 跟线程安全到底有什么关系（剧透：没有关系）。

## std::atomic_thread_fence：独立的内存屏障

`std::atomic_thread_fence` 定义在 `<atomic>` 头文件中，签名为：

```cpp
extern "C" void atomic_thread_fence(std::memory_order order) noexcept;
```

它做的事情跟我们上一篇讲的 `memory_order` 语义是一一对应的，只不过它不关联任何具体的原子操作。传入 `memory_order_release` 就是一个 release fence，传入 `memory_order_acquire` 就是一个 acquire fence，`acq_rel` 两者兼备，`seq_cst` 则是最强的全向屏障。如果你传 `memory_order_relaxed`，这个 fence 就什么也不做——没有排序约束。

那么 fence 到底是怎么建立同步关系的？cppreference 给出了三种模式，我们逐一拆解。

### fence-atomic 同步

第一种模式：线程 A 中的 release fence 配合一个普通的原子 store，与线程 B 中的 acquire load 建立同步。条件是：线程 A 中 fence 排在 store 之前（sequenced-before），而线程 B 的 load 读到了线程 A 的 store 写入的值。此时，线程 A 中 fence 之前的所有非原子和 relaxed 原子写入，都对线程 B 中 load 之后的所有读取 happens-before。

这听起来有点绕，我们用一段代码来说明：

```cpp
#include <atomic>
#include <string>

std::atomic<int> flag{0};
int payload = 0;

void producer()
{
    payload = 42;  // 非原子写入
    // release fence：保证上面的写入不会被重排到下面任何 store 之后
    std::atomic_thread_fence(std::memory_order_release);
    flag.store(1, std::memory_order_relaxed);  // relaxed store 即可
}

void consumer()
{
    // 等待 flag 变为 1
    while (flag.load(std::memory_order_relaxed) != 1) {
        // spin
    }
    // acquire fence：保证下面的读取不会被重排到上面任何 load 之前
    std::atomic_thread_fence(std::memory_order_acquire);
    // 一定能看到 payload == 42
    int local = payload;
}
```

注意看这段代码和上一篇的"发布-订阅"模式有什么区别。在上一篇中，我们写的是 `flag.store(1, std::memory_order_release)`，把 release 语义绑定在 store 操作上。而这里，store 本身是 `relaxed` 的，release 的约束由独立的 fence 提供。两种写法在语义上是等价的——最终建立的都是同一个 happens-before 关系。那为什么要用 fence？等下我们会看到一个 fence 无法被普通 atomic 操作替代的场景。

### atomic-fence 同步

第二种模式是第一种的反转：线程 A 用普通的 release store，线程 B 用独立的 acquire fence。条件是线程 B 中有一个 atomic load 排在 fence 之前，并且这个 load 读到了线程 A 的 store 写入的值。

这种模式的一个典型应用场景是"邮箱扫描"：我们有多个邮箱（每个邮箱由一个原子标志位标识），读者需要扫描所有邮箱，但只需要跟其中写入了自己的数据的那个邮箱建立同步。如果用带 acquire 的 load 去读每一个邮箱标志位，那即使标志位不是自己的，也会引入不必要的屏障开销。更好的做法是用 relaxed load 扫描，发现自己关心的邮箱有数据之后，只对那一个邮箱做一次 acquire fence：

```cpp
#include <atomic>
#include <string>

constexpr int kNumMailboxes = 32;

std::atomic<int> mailbox_receiver[kNumMailboxes];
std::string mailbox_data[kNumMailboxes];

// 写入线程 i
void write_mailbox(int i, int receiver_id, const std::string& msg)
{
    mailbox_data[i] = msg;
    std::atomic_store_explicit(&mailbox_receiver[i],
                               receiver_id,
                               std::memory_order_release);
}

// 读取线程：扫描所有邮箱，只跟包含自己数据的邮箱同步
void read_my_mail(int my_id)
{
    for (int i = 0; i < kNumMailboxes; ++i) {
        if (std::atomic_load_explicit(&mailbox_receiver[i],
                                       std::memory_order_relaxed) == my_id) {
            // 只在匹配时插入 acquire fence
            std::atomic_thread_fence(std::memory_order_acquire);
            // 现在能安全地读取 mailbox_data[i]
            process(mailbox_data[i]);
        }
    }
}
```

这里的关键洞察是：acquire fence 只在我们确认需要同步的时候才执行。前面的 31 次 relaxed load 不引入任何屏障，性能代价极低。这就是 fence 相比"带排序的原子操作"的灵活性——它可以把"决策"和"同步"分开，在确认需要同步之后再施加屏障。

### fence-fence 同步

第三种模式是两端都用 fence。线程 A 用 release fence + relaxed store，线程 B 用 relaxed load + acquire fence。条件是线程 A 的 fence 排在 store 之前，线程 B 的 load 读到了 store 的值，且 load 排在 fence 之前。

这种模式的适用场景是"批量发布"：线程 A 准备好一组数据后，用一次 release fence 同时发布多个 relaxed store。对应的消费者用一次 acquire fence 读取多个 relaxed load。比起对每个原子操作都设置 release/acquire，一次 fence 覆盖多个操作显然更高效：

```cpp
#include <atomic>
#include <string>

std::atomic<int> arr[3] = {-1, -1, -1};
std::string data[1000];  // 非原子数据

// 线程 A：计算并批量发布三个值
void thread_a(int v0, int v1, int v2)
{
    data[v0] = compute(v0);
    data[v1] = compute(v1);
    data[v2] = compute(v2);

    // 一次 release fence 覆盖后续三个 relaxed store
    std::atomic_thread_fence(std::memory_order_release);
    std::atomic_store_explicit(&arr[0], v0, std::memory_order_relaxed);
    std::atomic_store_explicit(&arr[1], v1, std::memory_order_relaxed);
    std::atomic_store_explicit(&arr[2], v2, std::memory_order_relaxed);
}

// 线程 B：读取并使用已发布的数据
void thread_b()
{
    int v0 = std::atomic_load_explicit(&arr[0], std::memory_order_relaxed);
    int v1 = std::atomic_load_explicit(&arr[1], std::memory_order_relaxed);
    int v2 = std::atomic_load_explicit(&arr[2], std::memory_order_relaxed);

    // 一次 acquire fence 覆盖前面三个 relaxed load
    std::atomic_thread_fence(std::memory_order_acquire);

    if (v0 != -1) { process(data[v0]); }
    if (v1 != -1) { process(data[v1]); }
    if (v2 != -1) { process(data[v2]); }
}
```

这种模式在无锁数据结构里很常见——当你需要同时发布多个字段，但不希望每个字段都带一个 release store 时，一次 release fence + 多个 relaxed store 是更优雅的选择。

### fence 与原子操作的比较：何时用 fence

到这里我们可以总结一下 fence 相比"带排序的原子操作"的优势和劣势了。fence 的优势在于灵活性：一次 fence 可以覆盖多个原子操作，可以把同步延迟到真正需要的时候才施加，可以避免不必要的屏障开销。劣势在于可读性和易错性——fence 的语义比带排序的原子操作更难推理，因为它跟具体原子操作之间的 sequenced-before 关系必须被程序员自己保证，编译器不会帮你检查。

笔者的建议是：在大多数场景下，优先使用带排序的原子操作（比如 `store(..., release)` + `load(..., acquire)`），只有在确认性能敏感且能从 fence 的灵活性中获益时，才考虑用 fence 替代。记住，fence 不是"更高级"的写法，它是一种"更手动"的写法——手动意味着更大的自由度，也意味着更容易出错。

## std::atomic_signal_fence：线程内的信号屏障

`std::atomic_signal_fence` 是 fence 家族中相对低调的一个，签名为：

```cpp
extern "C" void atomic_signal_fence(std::memory_order order) noexcept;
```

它的定位非常明确：在**同一个线程**的普通代码和信号处理器（signal handler）之间建立内存排序约束。它跟 `atomic_thread_fence` 的区别在于，它**不发出任何 CPU 屏障指令**——只阻止编译器的指令重排。换句话说，`atomic_signal_fence` 是一个纯粹的编译器屏障。

为什么只管编译器不管 CPU？因为信号处理器跟被中断的线程跑在同一个 CPU 核心上，共享同一套 cache 和寄存器状态。CPU 看到的内存顺序本来就是一致的（同一个核心不存在 cache 一致性问题），唯一可能出问题的是编译器的重排——编译器可能把信号处理器需要看到的 store 挪到信号处理器读取之后，或者把信号处理器写入的 load 挪到信号处理器写入之前。`atomic_signal_fence` 就是阻止这种编译器层面的"好心办坏事"。

一个典型的用例是异步 I/O 中用 `SIGINT` 或自定义信号通知主线程数据已就绪：

```cpp
#include <atomic>
#include <csignal>
#include <cstdio>

std::atomic<bool> signal_ready{false};
int signal_data = 0;

void handler(int signo)
{
    // 信号处理器中写入数据，用 release fence 确保编译器不重排
    std::atomic_signal_fence(std::memory_order_release);
    signal_ready.store(true, std::memory_order_relaxed);
}

void setup_signal_handler()
{
    // 准备数据
    signal_data = 42;

    // release fence：确保 signal_data 的写入不被编译器重排到后面
    std::atomic_signal_fence(std::memory_order_release);
    signal_ready.store(true, std::memory_order_relaxed);

    std::signal(SIGUSR1, handler);
}
```

需要特别强调的是：`atomic_signal_fence` 只适用于信号处理器场景，不能用于线程间同步。如果你把它用在两个不同线程之间，它不会发出任何 CPU 屏障指令，在弱序架构（如 ARM）上完全无法保证内存可见性。线程间同步请用 `atomic_thread_fence`。

## 编译器屏障与 CPU 屏障

理解了 `atomic_thread_fence` 和 `atomic_signal_fence` 的区别之后，我们可以更清晰地看到"屏障"这个概念其实分两个层次：编译器屏障和 CPU 屏障。前者阻止编译器在编译阶段对指令进行重排，后者阻止 CPU 在运行阶段对指令进行乱序执行。两者缺一不可——只有编译器屏障的话，CPU 可能还是会乱序执行；只有 CPU 屏障的话，编译器在生成代码时就已经把顺序打乱了。

### 编译器屏障：asm volatile("" ::: "memory")

在 GCC 和 Clang 中，最底层的编译器屏障是内联汇编：

```cpp
asm volatile("" ::: "memory");
```

这行内联汇编的含义是：不生成任何指令（`""` 是空的汇编模板），但告诉编译器三件事——这个操作是 volatile 的（不能被优化掉或跟其他语句合并），它可能修改内存（`"memory"` clobber），所以编译器必须假设在这之前和之后的所有内存访问都可能被这个"操作"影响，从而不能跨过这个点进行重排。

这实际上就是 `std::atomic_signal_fence(memory_order_seq_cst)` 在大多数平台上的底层实现。C++ 标准没有规定 `atomic_signal_fence` 的具体实现方式，但在 GCC/Clang 上它通常就是编译器级别的屏障，不生成任何 CPU 指令。

### CPU 屏障：架构相关指令

编译器屏障管的是编译器的代码生成，但 CPU 本身也可能在运行时做乱序执行。要阻止 CPU 的重排，需要硬件级别的屏障指令。不同架构有不同的指令集，我们来看看最常见的两种。

#### x86/x86-64：mfence、sfence、lfence

x86 的内存模型是 TSO（Total Store Ordering），本身已经相当强——store 不会被重排到其他 store 之前，load 不会被重排到其他 load 之前，store 也不会被重排到它之前的 load 之后。唯一允许的重排是 store-load：一个 store 后面跟一个 load，CPU 可能先执行 load 再执行 store。所以在 x86 上，`acquire` 和 `release` 语义几乎由硬件自动保证，不需要额外的屏障指令。

`mfence` 是 x86 的全向屏障——它阻止所有类型的重排，包括 store-load。`sfence` 是 store 屏障（所有 store 操作在 sfence 之前的必须在 sfence 之后的之前完成），`lfence` 是 load 屏障。实际上，在 x86 上，`atomic_thread_fence` 除了 `seq_cst` 级别外，其他级别不生成任何 CPU 指令——因为 TSO 已经足够强了。对于 `seq_cst` fence，GCC 通常不会直接生成 `mfence`，而是生成 `lock orq $0, (%rsp)`（对栈顶做一个值为 0 的原子 OR 操作），这条指令的 `LOCK` 前缀本身就是全向屏障，在某些微架构上比 `mfence` 更快，效果完全等价。

值得一提的是，`lfence + sfence` 并不等价于 `mfence`。前者阻止了 load-load 和 store-store 的重排，但无法阻止 store-load 重排——而 store-load 恰好是 x86 上唯一被允许的重排。所以当需要全向屏障时，必须用 `mfence`。

#### ARM：dmb、dsb、isb

ARM 是弱序架构，允许几乎所有类型的重排（store-store、load-load、store-load、load-store 都可能被重排），所以在 ARM 上，内存屏障是并发编程的日常——不是锦上添花，而是刚需。

ARM 提供三种屏障指令。`DMB`（Data Memory Barrier）确保在它之前的所有数据内存访问完成后，才开始执行它之后的数据内存访问。`DSB`（Data Synchronization Barrier）比 DMB 更强——它不仅保证排序，还保证所有内存访问在 DSB 完成之前真正到达目标。`ISB`（Instruction Synchronization Barrier）则刷新流水线，保证它之前的所有指令完成后才开始取后续指令——通常在修改系统寄存器（如切换页表）之后使用。

DMB 还有选项后缀：`DMB ST` 只管 store 屏障，`DMB LD` 只管 load 屏障，`DMB ISH` 是 inner shareable 域的全向屏障（多核之间最常见的使用场景）。当 C++ 代码中调用 `std::atomic_thread_fence(memory_order_release)` 时，在 ARM 上编译器通常会生成 `DMB ISH` 指令。而对于 `memory_order_acquire`，GCC 和 Clang 会生成更轻量的 `DMB ISHLD` 指令，只对 load 操作施加屏障。

关于这些 CPU 屏障指令，我们通常不需要直接使用——标准库的 `atomic_thread_fence` 和带排序的原子操作已经帮我们封装好了。但理解底层机制有助于我们做出更好的性能决策：在 x86 上，`seq_cst` 的额外开销是一次 `mfence`；在 ARM 上，每次 `acquire`/`release` 都是一次 `DMB`，开销大得多。

## volatile 不是线程安全机制

终于到了这一篇"避坑"的部分。`volatile` 可能是 C++ 中被误解最深的关键字之一——很多开发者以为它跟 Java 的 `volatile` 一样能保证可见性和有序性，但 C++ 的 `volatile` 完全不是这么回事。

### volatile 到底做了什么

C++ 标准对 `volatile` 的规定是：对 volatile glvalue 的读写是"可观察行为"（observable behavior），编译器不能优化掉或合并这些读写。换句话说，每次代码写了 `volatile int x` 并对其进行读写，编译器必须忠实地生成对应的 load/store 指令，不能把它缓存在寄存器里，不能把两次读合并成一次，不能把它优化掉。

这个语义的初衷是硬件寄存器访问——比如一个内存映射的 UART 数据寄存器，每次读取都可能返回不同的值（新到的数据），编译器绝对不能把它优化成"读一次缓存在寄存器里"。另一个经典场景是 `setjmp`/`longjmp`——跳转后需要确保 `volatile` 变量的值是最新的。

### volatile 没做什么

`volatile` 不保证原子性。`volatile int` 的自增操作 `++x` 在多线程中仍然是 read-modify-write 三步，中间可以被其他线程打断。它也不保证内存序——编译器不会对 `volatile` 访问插入任何屏障，CPU 的乱序执行也完全不受影响。它更不保证 cache 一致性协议的参与方式——虽然实际上 volatile 变量确实存在于主存中，但这跟 happens-before 没有任何关系。

简而言之，`volatile` 解决的是"编译器别自作聪明"的问题，而线程安全需要解决的是"编译器和 CPU 都别自作聪明，而且操作还得是原子的"——这完全是两个层面的事情。

### 一个经典的 volatile 误用

```cpp
#include <thread>
#include <iostream>

volatile bool ready = false;
int data = 0;

void producer()
{
    data = 42;
    ready = true;  // volatile 写入，但不保证对其他线程可见
}

void consumer()
{
    while (!ready) {
        // 自旋：可能永远不会看到 ready 变为 true
    }
    std::cout << data << "\n";  // 可能输出 0 而非 42
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

这段代码在 x86 上大概率能"正常工作"——因为 x86 的 TSO 模型足够强，而且 `volatile` 阻止了编译器把 `ready` 缓存在寄存器中（所以 consumer 的循环能每次从内存读）。但在 ARM 或 PowerPC 上，这段代码可能完全失败：CPU 的 store buffer 可能导致 `ready = true` 对 consumer 不可见，或者 `data = 42` 和 `ready = true` 的写入被 CPU 重排。

正确的写法是使用 `std::atomic`：

```cpp
#include <thread>
#include <iostream>
#include <atomic>

std::atomic<bool> ready{false};
int data = 0;

void producer()
{
    data = 42;
    ready.store(true, std::memory_order_release);
}

void consumer()
{
    while (!ready.load(std::memory_order_acquire)) {
        // 自旋：acquire 语义保证能看到 release 之前的所有写入
    }
    std::cout << data << "\n";  // 保证输出 42
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

### volatile 的正确使用场景

`volatile` 并不是一无是处，只是它的使用场景跟多线程无关。它适合处理信号处理器（signal handler）与主线程之间的通信（这也是 POSIX 标准明确允许的）、`setjmp`/`longjmp` 中的变量保护、以及硬件寄存器的内存映射访问。在这些场景中，涉及的"另一方"要么是同一个 CPU 核心上的信号处理器（不涉及 cache 一致性问题），要么是硬件设备（通过 MMIO）。

如果你确实需要在信号处理器和主线程之间同步，标准库提供了 `std::atomic_signal_fence`，前面已经讲过——它是专门为这个场景设计的，语义比 `volatile` 清晰得多，而且跟 `std::atomic` 配合使用时能提供完整的同步保证。

## volatile 与 std::atomic 的比较

最后我们来做一个干脆利落的对比。`volatile` 告诉编译器"别优化这个变量的访问"，但不管原子性、不管内存序、不管线程间的可见性。`std::atomic` 告诉编译器和 CPU"这个变量的访问必须是原子的，并且可以指定内存序"，它提供完整的线程间同步保证。两者解决的是完全不同的问题，不能互相替代。

有一个值得一提的例外：MSVC 在历史上为 `volatile` 变量添加了 acquire/release 语义——volatile read 具有 acquire 语义，volatile write 具有 release 语义。这是非标准扩展，在 `/volatile:ms` 编译选项下生效（在 ARM 上是默认的）。GCC 和 Clang 不提供这种保证。如果你的代码依赖了 MSVC 的 volatile 语义来保证线程安全，那它无法移植到其他编译器。标准委员会明确拒绝把 MSVC 的行为标准化，因为这会限制编译器的优化能力。

## 练习

### 练习 1：fence 放置分析

下面这段代码的 fence 使用是正确的吗？如果 `thread_b` 在 `arr[0]` 和 `arr[1]` 都不是 -1 时，能否安全地读取 `data[v0]` 和 `data[v1]`？如果只能有一个 fence 被保留（要么 release fence，要么 acquire fence），去掉哪一个会破坏正确性？

```cpp
std::atomic<int> arr[2] = {-1, -1};
std::string data[1000];

void thread_a(int v0, int v1)
{
    data[v0] = compute(v0);
    data[v1] = compute(v1);
    std::atomic_thread_fence(std::memory_order_release);
    arr[0].store(v0, std::memory_order_relaxed);
    arr[1].store(v1, std::memory_order_relaxed);
}

void thread_b()
{
    int v0 = arr[0].load(std::memory_order_relaxed);
    int v1 = arr[1].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (v0 != -1) { process(data[v0]); }
    if (v1 != -1) { process(data[v1]); }
}
```

提示：回顾 fence-fence 同步的三个条件——存在一个原子对象 M、线程 A 中有对 M 的写 X 且 release fence 在 X 之前、线程 B 中有对 M 的读 Y 且 Y 在 acquire fence 之前。

### 练习 2：volatile 诊断

分析下面这段代码在 x86 和 ARM 上的可能行为差异。解释为什么 `volatile` 在 x86 上"看起来能工作"，但在 ARM 上可能失败。如果要用 `std::atomic` 替换，最小的改动是什么？

```cpp
volatile int flag = 0;
int value = 0;

// 线程 1
void writer()
{
    value = 100;
    flag = 1;
}

// 线程 2
void reader()
{
    while (flag == 0) {}
    printf("value = %d\n", value);
}
```

### 练习 3：编译器屏障 vs CPU 屏障

判断以下说法是否正确，并说明理由：

1. `std::atomic_signal_fence(memory_order_release)` 会生成 CPU 屏障指令。
2. 在 x86 上，`std::atomic_thread_fence(memory_order_acquire)` 不需要生成任何 CPU 指令。
3. `asm volatile("" ::: "memory")` 能阻止 CPU 的乱序执行。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch03-atomic-memory-model/`。

## 参考资源

- [std::atomic_thread_fence -- cppreference](https://en.cppreference.com/cpp/atomic/atomic_thread_fence)
- [std::atomic_signal_fence -- cppreference](https://en.cppreference.com/cpp/atomic/atomic_signal_fence)
- [DMB, DSB, and ISB -- Arm Developer](https://developer.arm.com/documentation/dui0489/e/arm-and-thumb-instructions/miscellaneous-instructions/dmb--dsb--and-isb)
- [MFENCE -- x86 Instruction Reference](https://www.felixcloutier.com/x86/mfence)
- [Fences as Memory Barriers -- Modernes C++](https://www.modernescpp.com/index.php/fences-as-memory-barriers/)
- [C++ Standard Draft [atomics.fences] -- eel.is](https://eel.is/c++draft/atomics.fences)
