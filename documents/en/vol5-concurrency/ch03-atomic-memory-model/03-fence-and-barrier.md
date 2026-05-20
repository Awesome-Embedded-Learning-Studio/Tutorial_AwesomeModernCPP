---
title: '`fence` and Compiler Barriers'
description: Principles of `atomic_thread_fence`, compiler barriers, and CPU barriers,
  and analyzing the boundaries and common misuses of `volatile`
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
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- 内存序详解
related:
- atomic_wait 与 atomic_ref
- 原子操作模式
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/03-fence-and-barrier.md
  source_hash: f8513d92b724cbb63b4b0d04d040faa5214329c954a6bf36912f18db8c6d938c
  translated_at: '2026-05-20T04:39:11.152265+00:00'
  engine: anthropic
  token_count: 3691
---
# Fences and Compiler Barriers

In the previous article, we spent a lot of time breaking down the six levels of `memory_order`—from `relaxed` to `seq_cst`. Each step was about drawing a line between "what the compiler/CPU can reorder" and "what guarantees we need." But did you notice that all the synchronization we discussed was "bound" to a specific atomic operation? `store(..., memory_order_release)` and `load(..., memory_order_acquire)` always appeared in pairs, with release bound to a write and acquire bound to a read.

Here is the question: what if we only want to control reordering behavior without performing a store or load on any specific atomic variable? In other words, can we extract the "no reordering" constraint on its own, decoupling it from atomic operations?

This is where `std::atomic_thread_fence` comes in—a memory barrier independent of atomic operations. It tells the compiler and CPU: "Don't mess with the order of memory operations before and after this point." Add to this its more low-key sibling `std::atomic_signal_fence` (which only prevents compiler reordering, not CPU reordering), along with lower-level inline assembly barriers and platform-specific instructions, and we have the complete toolbox for controlling memory order in C++.

In this article, we will go through these tools from top to bottom: the semantics of the standard library fence, the difference between compiler barriers and CPU barriers, the underlying barrier instructions on x86 and ARM, and finally, we will clear up a pitfall that almost every C++ programmer falls into—what `volatile` actually has to do with thread safety (spoiler: nothing).

## std::atomic_thread_fence: The Standalone Memory Barrier

`std::atomic_thread_fence` is defined in the `<atomic>` header, with the following signature:

```cpp
extern "C" void atomic_thread_fence(std::memory_order order) noexcept;
```

What it does maps one-to-one with the `memory_order` semantics we discussed in the previous article, except it isn't associated with any specific atomic operation. Passing `memory_order_release` gives you a release fence, passing `memory_order_acquire` gives you an acquire fence, `acq_rel` provides both, and `seq_cst` is the strongest all-directional barrier. If you pass `memory_order_relaxed`, the fence does nothing—no ordering constraints.

So how exactly does a fence establish a synchronization relationship? cppreference outlines three patterns, which we will break down one by one.

### Fence-Atomic Synchronization

The first pattern: a release fence in Thread A paired with a plain atomic store synchronizes with an acquire load in Thread B. The conditions are: the fence in Thread A is sequenced-before the store, and the load in Thread B reads the value written by the store in Thread A. At this point, all non-atomic and relaxed atomic writes before the fence in Thread A happen-before all reads after the load in Thread B.

This sounds a bit convoluted, so let's illustrate it with some code:

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

Notice the difference between this code and the "publish-subscribe" pattern from the previous article. In the previous article, we wrote `flag.store(1, std::memory_order_release)`, binding the release semantics to the store operation. Here, the store itself is `relaxed`, and the release constraint is provided by a standalone fence. The two approaches are semantically equivalent—both ultimately establish the same happens-before relationship. So why use a fence? We will see a scenario shortly where a fence cannot be replaced by a regular atomic operation.

### Atomic-Fence Synchronization

The second pattern is the reverse of the first: Thread A uses a plain release store, and Thread B uses a standalone acquire fence. The condition is that Thread B has an atomic load sequenced-before the fence, and this load reads the value written by Thread A's store.

A typical use case for this pattern is "mailbox scanning": we have multiple mailboxes (each identified by an atomic flag), and the reader needs to scan all of them but only needs to synchronize with the one that contains its data. If we use an acquire load to read every mailbox flag, we incur unnecessary barrier overhead even when the flag isn't ours. A better approach is to scan with relaxed loads, and after discovering that a mailbox we care about has data, perform a single acquire fence for just that one mailbox:

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

The key insight here is that the acquire fence is only executed when we confirm we need to synchronize. The preceding 31 relaxed loads introduce no barriers, resulting in minimal performance cost. This is the flexibility of fences compared to "ordered atomic operations"—it allows us to separate "decision-making" from "synchronization," only applying the barrier after confirming that synchronization is needed.

### Fence-Fence Synchronization

The third pattern uses fences on both ends. Thread A uses a release fence + relaxed store, and Thread B uses a relaxed load + acquire fence. The conditions are that Thread A's fence is sequenced-before the store, Thread B's load reads the value written by the store, and the load is sequenced-before the fence.

A good use case for this pattern is "batch publishing": after Thread A prepares a set of data, it uses a single release fence to publish multiple relaxed stores at once. The corresponding consumer uses a single acquire fence to read multiple relaxed loads. Compared to setting release/acquire on every individual atomic operation, a single fence covering multiple operations is obviously more efficient:

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

This pattern is common in lock-free data structures—when you need to publish multiple fields simultaneously but don't want each field to carry a release store, a single release fence + multiple relaxed stores is the more elegant choice.

### Comparing Fences and Atomic Operations: When to Use Fences

At this point, we can summarize the advantages and disadvantages of fences compared to "ordered atomic operations." The advantage of fences lies in their flexibility: a single fence can cover multiple atomic operations, synchronization can be delayed until it is truly needed, and unnecessary barrier overhead can be avoided. The disadvantage is readability and error-proneness—the semantics of fences are harder to reason about than ordered atomic operations, because the sequenced-before relationship between the fence and specific atomic operations must be guaranteed by the programmer; the compiler won't check it for you.

My recommendation is: in most scenarios, prefer ordered atomic operations (like `store(..., release)` + `load(..., acquire)`). Only consider using fences as a replacement when you have confirmed the code is performance-sensitive and can benefit from the flexibility of fences. Remember, fences are not a "more advanced" approach; they are a "more manual" approach—manual means greater freedom, but it also means it's easier to make mistakes.

## std::atomic_signal_fence: Intra-Thread Signal Fence

`std::atomic_signal_fence` is a relatively low-key member of the fence family, with the following signature:

```cpp
extern "C" void atomic_signal_fence(std::memory_order order) noexcept;
```

Its purpose is very specific: to establish memory ordering constraints between normal code and a signal handler **within the same thread**. The difference between it and `atomic_thread_fence` is that it **does not issue any CPU barrier instructions**—it only prevents the compiler from reordering instructions. In other words, `atomic_signal_fence` is a pure compiler barrier.

Why only manage the compiler and not the CPU? Because the signal handler runs on the same CPU core as the interrupted thread, sharing the same cache and register state. The memory order seen by the CPU is inherently consistent (there are no cache coherency issues within a single core), so the only thing that can go wrong is compiler reordering—the compiler might move a store that the signal handler needs to see to after the signal handler reads, or move a load that the signal handler writes to before the signal handler writes. `atomic_signal_fence` exists to prevent this kind of compiler-level "trying to help but causing harm."

A typical use case is using `SIGINT` or a custom signal in asynchronous I/O to notify the main thread that data is ready:

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

It must be strongly emphasized that `atomic_signal_fence` is only applicable to signal handler scenarios and cannot be used for inter-thread synchronization. If you use it between two different threads, it will not issue any CPU barrier instructions, and it provides absolutely no memory visibility guarantees on weakly-ordered architectures (like ARM). For inter-thread synchronization, use `atomic_thread_fence`.

## Compiler Barriers vs. CPU Barriers

Having understood the difference between `atomic_thread_fence` and `atomic_signal_fence`, we can more clearly see that the concept of a "barrier" actually operates at two levels: compiler barriers and CPU barriers. The former prevents the compiler from reordering instructions during compilation, while the latter prevents the CPU from out-of-order execution at runtime. Both are indispensable—with only a compiler barrier, the CPU might still execute out of order; with only a CPU barrier, the compiler may have already scrambled the order when generating code.

### Compiler Barriers: asm volatile("" ::: "memory")

In GCC and Clang, the most low-level compiler barrier is inline assembly:

```cpp
asm volatile("" ::: "memory");
```

The meaning of this inline assembly is: generate no instructions (the `""` is an empty assembly template), but tell the compiler three things—this operation is volatile (it cannot be optimized away or merged with other statements), it may modify memory (the `"memory"` clobber), so the compiler must assume that all memory accesses before and after this "operation" might be affected, and therefore cannot reorder across this point.

This is essentially the underlying implementation of `std::atomic_signal_fence(memory_order_seq_cst)` on most platforms. The C++ standard does not mandate a specific implementation for `atomic_signal_fence`, but on GCC/Clang it is typically a compiler-level barrier that generates no CPU instructions.

### CPU Barriers: Architecture-Specific Instructions

Compiler barriers manage the compiler's code generation, but the CPU itself might also perform out-of-order execution at runtime. To prevent CPU reordering, hardware-level barrier instructions are needed. Different architectures have different instruction sets; let's look at the two most common ones.

#### x86/x86-64: mfence, sfence, lfence

The x86 memory model is TSO (Total Store Ordering), which is already quite strong—stores cannot be reordered before other stores, loads cannot be reordered before other loads, and stores cannot be reordered after earlier loads. The only allowed reordering is store-load: when a store is followed by a load, the CPU might execute the load before the store. Therefore, on x86, `acquire` and `release` semantics are almost guaranteed by hardware automatically, requiring no additional barrier instructions.

`mfence` is the all-directional barrier on x86—it prevents all types of reordering, including store-load. `sfence` is a store barrier (all store operations before sfence must complete before any store operations after sfence), and `lfence` is a load barrier. In practice, on x86, `atomic_thread_fence` generates no CPU instructions for any level other than `seq_cst`—because TSO is already strong enough. For a `seq_cst` fence, GCC typically doesn't generate `mfence` directly, but instead generates `lock orq $0, (%rsp)` (an atomic OR operation with a value of 0 on the top of the stack); the `LOCK` prefix of this instruction is itself an all-directional barrier, which is faster than `mfence` on certain microarchitectures and is completely equivalent in effect.

It is worth mentioning that `lfence + sfence` is not equivalent to `mfence`. The former prevents load-load and store-store reordering, but cannot prevent store-load reordering—and store-load happens to be the only reordering allowed on x86. So when an all-directional barrier is needed, `mfence` must be used.

#### ARM: dmb, dsb, isb

ARM is a weakly-ordered architecture that allows almost all types of reordering (store-store, load-load, store-load, and load-store can all be reordered), so on ARM, memory barriers are a daily reality in concurrent programming—they are not a nice-to-have, but a necessity.

ARM provides three barrier instructions. `DMB` (Data Memory Barrier) ensures that all data memory accesses before it complete before any data memory accesses after it begin. `DSB` (Data Synchronization Barrier) is stronger than DMB—it not only guarantees ordering but also ensures that all memory accesses truly reach their destination before the DSB completes. `ISB` (Instruction Synchronization Barrier) flushes the pipeline, ensuring that all instructions before it complete before subsequent instructions are fetched—typically used after modifying system registers (such as switching page tables).

DMB also has option suffixes: `DMB ST` is a store-only barrier, `DMB LD` is a load-only barrier, and `DMB ISH` is an all-directional barrier for the inner shareable domain (the most common use case among multiple cores). When `std::atomic_thread_fence(memory_order_release)` is called in C++ code, the compiler typically generates a `DMB ISH` instruction on ARM. For `memory_order_acquire`, GCC and Clang generate the lighter-weight `DMB ISHLD` instruction, which only applies a barrier to load operations.

Regarding these CPU barrier instructions, we usually don't need to use them directly—the standard library's `atomic_thread_fence` and ordered atomic operations have already encapsulated them for us. But understanding the underlying mechanisms helps us make better performance decisions: on x86, the extra overhead of `seq_cst` is a single `mfence`; on ARM, every `acquire`/`release` is a `DMB`, which is much more expensive.

## volatile Is Not a Thread Safety Mechanism

We have finally arrived at the "pitfall avoidance" section of this article. `volatile` is arguably one of the most misunderstood keywords in C++—many developers think it guarantees visibility and ordering just like Java's `volatile`, but C++'s `volatile` is nothing of the sort.

### What volatile Actually Does

The C++ standard's specification for `volatile` is that reads and writes to a volatile glvalue are "observable behavior," and the compiler cannot optimize away or merge these reads and writes. In other words, every time the code accesses `volatile int x`, the compiler must faithfully generate the corresponding load/store instructions—it cannot cache it in a register, it cannot merge two reads into one, and it cannot optimize it away.

The original intent of this semantics was for hardware register access—for example, a memory-mapped UART data register where each read might return a different value (newly arrived data), and the compiler absolutely must not optimize it into "read once and cache in a register." Another classic scenario is `setjmp`/`longjmp`—after a jump, we need to ensure the value of the `volatile` variable is up to date.

### What volatile Does Not Do

`volatile` does not guarantee atomicity. The increment operation `++x` on a `volatile int` is still a three-step read-modify-write in a multithreaded context, and it can be interrupted by other threads in the middle. It also does not guarantee memory order—the compiler will not insert any barriers for `volatile` accesses, and CPU out-of-order execution is completely unaffected. It further does not guarantee how the cache coherency protocol is involved—although volatile variables do physically reside in main memory, this has nothing to do with happens-before.

In short, `volatile` solves the problem of "compiler, don't be too clever," whereas thread safety needs to solve "compiler and CPU, don't be too clever, and the operations must be atomic"—these are completely different levels of concern.

### A Classic volatile Misuse

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

This code will most likely "work fine" on x86—because x86's TSO model is strong enough, and `volatile` prevents the compiler from caching `ready` in a register (so the consumer's loop reads from memory every time). But on ARM or PowerPC, this code might fail completely: the CPU's store buffer might cause `ready = true` to be invisible to the consumer, or the writes to `data = 42` and `ready = true` might be reordered by the CPU.

The correct approach is to use `std::atomic`:

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

### Correct Use Cases for volatile

`volatile` is not completely useless; its use cases just have nothing to do with multithreading. It is suitable for communication between a signal handler and the main thread (which is also explicitly permitted by the POSIX standard), protecting variables in `setjmp`/`longjmp`, and memory-mapped access to hardware registers. In these scenarios, the "other party" involved is either a signal handler on the same CPU core (which doesn't involve cache coherency issues) or a hardware device (via MMIO).

If you do need to synchronize between a signal handler and the main thread, the standard library provides `std::atomic_signal_fence`, which we covered earlier—it is designed specifically for this scenario, its semantics are much clearer than `volatile`, and when used in conjunction with `std::atomic`, it provides complete synchronization guarantees.

## Comparing volatile and std::atomic

Finally, let's do a clean comparison. `volatile` tells the compiler "don't optimize accesses to this variable," but it provides no atomicity, no memory ordering, and no inter-thread visibility. `std::atomic` tells the compiler and CPU "accesses to this variable must be atomic, and memory ordering can be specified," providing complete inter-thread synchronization guarantees. The two solve completely different problems and cannot replace each other.

One noteworthy exception: MSVC historically added acquire/release semantics to `volatile` variables—a volatile read has acquire semantics, and a volatile write has release semantics. This is a non-standard extension that takes effect under the `/volatile:ms` compiler flag (and is the default on ARM). GCC and Clang do not provide this guarantee. If your code relies on MSVC's volatile semantics for thread safety, it will not be portable to other compilers. The standards committee explicitly rejected standardizing MSVC's behavior because it would restrict the compiler's optimization capabilities.

## Exercises

### Exercise 1: Fence Placement Analysis

Is the fence usage in the following code correct? If `thread_b` is not -1 in both `arr[0]` and `arr[1]`, can `data[v0]` and `data[v1]` be safely read? If only one fence could be kept (either the release fence or the acquire fence), which one's removal would break correctness?

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

Hint: Review the three conditions for fence-fence synchronization—there exists an atomic object M, Thread A has a write X to M and a release fence sequenced-before X, and Thread B has a read Y from M and Y is sequenced-before the acquire fence.

### Exercise 2: volatile Diagnosis

Analyze the possible behavioral differences of the following code on x86 and ARM. Explain why `volatile` "appears to work" on x86 but might fail on ARM. If you were to replace it with `std::atomic`, what is the minimal change required?

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

### Exercise 3: Compiler Barriers vs. CPU Barriers

Determine whether the following statements are correct, and explain your reasoning:

1. `std::atomic_signal_fence(memory_order_release)` generates CPU barrier instructions.
2. On x86, `std::atomic_thread_fence(memory_order_acquire)` does not need to generate any CPU instructions.
3. `asm volatile("" ::: "memory")` can prevent CPU out-of-order execution.

> 💡 The complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## References

- [std::atomic_thread_fence -- cppreference](https://en.cppreference.com/cpp/atomic/atomic_thread_fence)
- [std::atomic_signal_fence -- cppreference](https://en.cppreference.com/cpp/atomic/atomic_signal_fence)
- [DMB, DSB, and ISB -- Arm Developer](https://developer.arm.com/documentation/dui0489/e/arm-and-thumb-instructions/miscellaneous-instructions/dmb--dsb--and-isb)
- [MFENCE -- x86 Instruction Reference](https://www.felixcloutier.com/x86/mfence)
- [Fences as Memory Barriers -- Modernes C++](https://www.modernescpp.com/index.php/fences-as-memory-barriers/)
- [C++ Standard Draft [atomics.fences] -- eel.is](https://eel.is/c++draft/atomics.fences)
