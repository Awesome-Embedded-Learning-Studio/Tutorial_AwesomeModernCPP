---
title: "Fences and Compiler Barriers"
description: "The principles of atomic_thread_fence, compiler barriers, and CPU barriers, and a close look at the boundaries and common misuses of volatile"
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
  - "A Deep Dive into Memory Ordering"
related:
  - "atomic_wait and atomic_ref"
  - "Atomic Operation Modes"
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/03-fence-and-barrier.md
  source_hash: f8513d92b724cbb63b4b0d04d040faa5214329c954a6bf36912f18db8c6d938c
  translated_at: '2026-09-26T07:13:44+00:00'
  engine: anthropic
  token_count: 4800
---

# Fences and Compiler Barriers

In the previous article we spent a lot of space dissecting the six levels of `memory_order`—from `relaxed` to `seq_cst`—and every step was about drawing a line between "what the compiler/CPU may reorder" and "what guarantees we need." But did you notice that all of the synchronization so far has been "bound" to some atomic operation? `store(..., memory_order_release)` and `load(..., memory_order_acquire)` show up in pairs: release is bound to the write, acquire is bound to the read.

Here is the question: what if we only want to control reordering behavior, without doing a store or a load on any specific atomic variable? In other words, can we pull the "no reordering" constraint out on its own and decouple it from atomic operations?

That is exactly where `std::atomic_thread_fence` comes in—a memory barrier independent of any atomic operation. It tells the compiler and the CPU: "don't mess with the order of the memory operations before and after this point." Add in its quieter sibling `std::atomic_signal_fence` (which only constrains compiler reordering, not CPU reordering), plus lower-level inline-assembly barriers and platform-specific instructions, and you have the complete toolbox C++ gives us for controlling memory order.

In this article we walk through these tools from top to bottom: the semantics of the standard library fences, the difference between compiler barriers and CPU barriers, the low-level barrier instructions on x86 and ARM, and finally we clear up a pitfall that almost every C++ programmer has stepped in—what `volatile` actually has to do with thread safety (spoiler: nothing).

## std::atomic_thread_fence: The Standalone Memory Barrier

`std::atomic_thread_fence` is defined in the `<atomic>` header, with the signature:

```cpp
extern "C" void atomic_thread_fence(std::memory_order order) noexcept;
```

What it does corresponds one-to-one with the `memory_order` semantics we covered in the previous article—except that it is not attached to any specific atomic operation. Pass `memory_order_release` and you get a release fence; pass `memory_order_acquire` and you get an acquire fence; `acq_rel` provides both; and `seq_cst` is the strongest, an all-around barrier. If you pass `memory_order_relaxed`, the fence does nothing at all—no ordering constraints.

So how does a fence actually establish synchronization? cppreference lays out three patterns, and we will take them apart one by one.

### Fence-Atomic Synchronization

The first pattern: a release fence in thread A, paired with a plain atomic store, synchronizes with an acquire load in thread B. The conditions: in thread A the fence is sequenced-before the store, and thread B's load reads the value written by thread A's store. When that holds, all non-atomic and relaxed atomic writes before the fence in thread A happen-before all reads after the load in thread B.

That sounds a bit convoluted, so let's make it concrete with code:

```cpp
#include <atomic>
#include <string>

std::atomic<int> flag{0};
int payload = 0;

void producer()
{
    payload = 42;  // non-atomic write
    // release fence: guarantees the write above won't be reordered after any store below
    std::atomic_thread_fence(std::memory_order_release);
    flag.store(1, std::memory_order_relaxed);  // a relaxed store is enough
}

void consumer()
{
    // wait for flag to become 1
    while (flag.load(std::memory_order_relaxed) != 1) {
        // spin
    }
    // acquire fence: guarantees the reads below won't be reordered before any load above
    std::atomic_thread_fence(std::memory_order_acquire);
    // guaranteed to see payload == 42
    int local = payload;
}
```

Notice what changed compared with the "publish-subscribe" pattern from the previous article. There we wrote `flag.store(1, std::memory_order_release)`, binding the release semantics to the store itself. Here the store is `relaxed`, and the release constraint comes from a standalone fence. The two forms are semantically equivalent—both end up establishing the same happens-before relationship. So why use a fence? Shortly we will see a scenario where a fence cannot be replaced by ordinary atomic operations.

### Atomic-Fence Synchronization

The second pattern is the first one flipped: thread A uses a plain release store, and thread B uses a standalone acquire fence. The condition is that thread B has an atomic load sequenced-before the fence, and that load reads the value written by thread A's store.

A typical application of this pattern is "mailbox scanning": we have multiple mailboxes (each identified by an atomic flag), and the reader has to scan all of them but only needs to synchronize with the one that holds data for it. Reading every mailbox flag with an acquire load would introduce unnecessary barrier overhead even for flags that are not ours. The better approach is to scan with relaxed loads, and once we discover that a mailbox we care about has data, issue a single acquire fence for that one mailbox:

```cpp
#include <atomic>
#include <string>

constexpr int kNumMailboxes = 32;

std::atomic<int> mailbox_receiver[kNumMailboxes];
std::string mailbox_data[kNumMailboxes];

// writer thread for mailbox i
void write_mailbox(int i, int receiver_id, const std::string& msg)
{
    mailbox_data[i] = msg;
    std::atomic_store_explicit(&mailbox_receiver[i],
                               receiver_id,
                               std::memory_order_release);
}

// reader thread: scan all mailboxes, synchronizing only with the one holding our data
void read_my_mail(int my_id)
{
    for (int i = 0; i < kNumMailboxes; ++i) {
        if (std::atomic_load_explicit(&mailbox_receiver[i],
                                       std::memory_order_relaxed) == my_id) {
            // insert the acquire fence only on a match
            std::atomic_thread_fence(std::memory_order_acquire);
            // it is now safe to read mailbox_data[i]
            process(mailbox_data[i]);
        }
    }
}
```

The key insight here: the acquire fence executes only once we have confirmed that we need to synchronize. The preceding 31 relaxed loads introduce no barrier at all, so the performance cost is minimal. That is the flexibility fences have over "ordered atomic operations"—they let you separate the decision from the synchronization, and apply the barrier only after confirming that synchronization is needed.

### Fence-Fence Synchronization

The third pattern uses fences on both ends. Thread A uses a release fence + relaxed store, and thread B uses a relaxed load + acquire fence. The conditions: thread A's fence is sequenced-before its store, thread B's load reads the value of that store, and the load is sequenced-before the fence.

This pattern fits "batch publishing": after thread A prepares a batch of data, it publishes several relaxed stores at once under a single release fence. The consumer on the other side reads several relaxed loads under a single acquire fence. Compared with setting release/acquire on every individual atomic operation, one fence covering multiple operations is clearly more efficient:

```cpp
#include <atomic>
#include <string>

std::atomic<int> arr[3] = {-1, -1, -1};
std::string data[1000];  // non-atomic data

// thread A: compute and publish three values in a batch
void thread_a(int v0, int v1, int v2)
{
    data[v0] = compute(v0);
    data[v1] = compute(v1);
    data[v2] = compute(v2);

    // one release fence covers the three relaxed stores below
    std::atomic_thread_fence(std::memory_order_release);
    std::atomic_store_explicit(&arr[0], v0, std::memory_order_relaxed);
    std::atomic_store_explicit(&arr[1], v1, std::memory_order_relaxed);
    std::atomic_store_explicit(&arr[2], v2, std::memory_order_relaxed);
}

// thread B: read and use the published data
void thread_b()
{
    int v0 = std::atomic_load_explicit(&arr[0], std::memory_order_relaxed);
    int v1 = std::atomic_load_explicit(&arr[1], std::memory_order_relaxed);
    int v2 = std::atomic_load_explicit(&arr[2], std::memory_order_relaxed);

    // one acquire fence covers the three relaxed loads above
    std::atomic_thread_fence(std::memory_order_acquire);

    if (v0 != -1) { process(data[v0]); }
    if (v1 != -1) { process(data[v1]); }
    if (v2 != -1) { process(data[v2]); }
}
```

This pattern is common in lock-free data structures—when you need to publish several fields at once but don't want each one to carry a release store, a single release fence plus multiple relaxed stores is the more elegant choice.

### Fences vs. Atomic Operations: When to Use a Fence

At this point we can summarize the strengths and weaknesses of fences compared with "ordered atomic operations." The strength of a fence is flexibility: one fence can cover multiple atomic operations, synchronization can be deferred until it is genuinely needed, and unnecessary barrier overhead can be avoided. The weakness is readability and error-proneness—fences are harder to reason about than ordered atomic operations, because the sequenced-before relationship between the fence and the specific atomic operations must be guaranteed by you, the programmer; the compiler will not check it for you.

Our recommendation: in most scenarios, prefer ordered atomic operations (for example `store(..., release)` + `load(..., acquire)`), and only consider replacing them with fences once you have confirmed the code is performance-sensitive and can benefit from the fence's flexibility. Remember, a fence is not the "more advanced" way to write things—it is the "more manual" way, and manual means more freedom, but also more room for mistakes.

## std::atomic_signal_fence: The Intra-Thread Signal Fence

`std::atomic_signal_fence` is the relatively quiet member of the fence family, with the signature:

```cpp
extern "C" void atomic_signal_fence(std::memory_order order) noexcept;
```

Its role is very specific: establish memory ordering constraints between ordinary code and a signal handler **within the same thread**. The difference from `atomic_thread_fence` is that it **does not emit any CPU barrier instruction**—it only prevents the compiler from reordering instructions. In other words, `atomic_signal_fence` is a pure compiler barrier.

Why does it only manage the compiler and not the CPU? Because the signal handler runs on the same CPU core as the thread it interrupts, sharing the same cache and register state. The memory order the CPU sees is already consistent (a single core has no cache coherence problem); the only thing that can go wrong is compiler reordering—the compiler might move a store that the signal handler needs to see to after the signal handler's read, or move a load of data the signal handler writes to before the signal handler's write. `atomic_signal_fence` exists to stop this compiler-level "good intention doing harm."

A typical use case is asynchronous I/O using `SIGINT` or a custom signal to notify the main thread that data is ready:

```cpp
#include <atomic>
#include <csignal>
#include <cstdio>

std::atomic<bool> signal_ready{false};
int signal_data = 0;

void handler(int signo)
{
    // write the data in the signal handler; the release fence keeps the compiler from reordering
    std::atomic_signal_fence(std::memory_order_release);
    signal_ready.store(true, std::memory_order_relaxed);
}

void setup_signal_handler()
{
    // prepare the data
    signal_data = 42;

    // release fence: ensures the write to signal_data is not moved below this point by the compiler
    std::atomic_signal_fence(std::memory_order_release);
    signal_ready.store(true, std::memory_order_relaxed);

    std::signal(SIGUSR1, handler);
}
```

One thing that bears emphasizing: `atomic_signal_fence` only applies to signal handler scenarios and must not be used for inter-thread synchronization. If you use it between two different threads, it emits no CPU barrier instruction whatsoever and provides no memory visibility guarantees on weakly ordered architectures such as ARM. For inter-thread synchronization, use `atomic_thread_fence`.

## Compiler Barriers and CPU Barriers

Having understood the difference between `atomic_thread_fence` and `atomic_signal_fence`, we can see more clearly that the concept of a "barrier" really spans two layers: the compiler barrier and the CPU barrier. The former prevents the compiler from reordering instructions at compile time; the latter prevents the CPU from executing them out of order at run time. Both are indispensable—with only a compiler barrier, the CPU may still execute out of order; with only a CPU barrier, the compiler has already scrambled the order while generating the code.

### Compiler Barriers: asm volatile("" ::: "memory")

On GCC and Clang, the most low-level compiler barrier is inline assembly:

```cpp
asm volatile("" ::: "memory");
```

This line of inline assembly means: generate no instructions (`""` is an empty assembly template), but tell the compiler three things—this operation is volatile (it cannot be optimized away or merged with other statements), and it may modify memory (the `"memory"` clobber), so the compiler must assume that all memory accesses before and after this "operation" may be affected by it, and therefore cannot reorder anything across this point.

This is effectively the underlying implementation of `std::atomic_signal_fence(memory_order_seq_cst)` on most platforms. The C++ standard does not mandate how `atomic_signal_fence` is implemented, but on GCC/Clang it is typically a compiler-level barrier that generates no CPU instructions.

### CPU Barriers: Architecture-Specific Instructions

Compiler barriers govern the compiler's code generation, but the CPU itself may also execute instructions out of order at run time. Stopping CPU reordering requires hardware-level barrier instructions. Different architectures have different instruction sets; let's look at the two most common ones.

#### x86/x86-64: mfence, sfence, lfence

The x86 memory model is TSO (Total Store Ordering), which is already quite strong—stores are not reordered before other stores, loads are not reordered before other loads, and a store is not reordered after a load that precedes it. The only reordering allowed is store-load: when a store is followed by a load, the CPU may execute the load first and the store later. So on x86, `acquire` and `release` semantics are essentially guaranteed by the hardware, with no extra barrier instructions needed.

`mfence` is x86's all-around barrier—it prevents every type of reordering, including store-load. `sfence` is a store barrier (all stores before an sfence must complete before any store after it), and `lfence` is a load barrier. In practice, on x86, `atomic_thread_fence` generates no CPU instruction for any level except `seq_cst`—because TSO is already strong enough. For a `seq_cst` fence, GCC typically does not emit `mfence` directly; instead it emits `lock orq $0, (%rsp)` (an atomic OR of 0 against the top of the stack). The `LOCK` prefix on that instruction is itself an all-around barrier, faster than `mfence` on some microarchitectures and completely equivalent in effect.

Worth mentioning: `lfence + sfence` is not equivalent to `mfence`. The former prevents load-load and store-store reordering, but cannot prevent store-load reordering—and store-load happens to be the only reordering x86 allows. So when you need an all-around barrier, you must use `mfence`.

#### ARM: dmb, dsb, isb

ARM is a weakly ordered architecture that allows almost every type of reordering (store-store, load-load, store-load, and load-store can all be reordered), so on ARM, memory barriers are part of the daily routine of concurrent programming—not a nice-to-have, but a necessity.

ARM provides three barrier instructions. `DMB` (Data Memory Barrier) ensures that all data memory accesses before it complete before any data memory access after it begins to execute. `DSB` (Data Synchronization Barrier) is stronger than DMB—it guarantees not only ordering but also that all memory accesses have truly reached their destination before the DSB completes. `ISB` (Instruction Synchronization Barrier) flushes the pipeline, guaranteeing that subsequent instructions are not fetched until all instructions before it complete—it is typically used after modifying system registers (such as switching page tables).

DMB also takes option suffixes: `DMB ST` is a store-only barrier, `DMB LD` is a load-only barrier, and `DMB ISH` is an all-around barrier over the inner shareable domain (the most common case for communication between cores). When C++ code calls `std::atomic_thread_fence(memory_order_release)`, on ARM the compiler typically generates a `DMB ISH` instruction. For `memory_order_acquire`, GCC and Clang generate the lighter `DMB ISHLD` instruction, which applies the barrier only to load operations.

We usually do not need to use these CPU barrier instructions directly—the standard library's `atomic_thread_fence` and ordered atomic operations have already wrapped them for us. But understanding the underlying mechanism helps us make better performance decisions: on x86, the extra cost of `seq_cst` is one `mfence`; on ARM, every `acquire`/`release` costs a `DMB`, which is much more expensive.

## volatile Is Not a Thread Safety Mechanism

We have finally reached this article's pitfall-avoidance section. `volatile` is probably one of the most misunderstood keywords in C++—many developers assume it guarantees visibility and ordering the way Java's `volatile` does, but C++'s `volatile` is nothing of the sort.

### What volatile Actually Does

The C++ standard's rule for `volatile` is: reads and writes to volatile glvalues are "observable behavior," and the compiler may not optimize these reads and writes away or merge them. In other words, whenever the code declares `volatile int x` and reads or writes it, the compiler must faithfully generate the corresponding load/store instructions—it cannot cache the value in a register, cannot merge two reads into one, and cannot optimize the access away.

The original intent of this semantics was hardware register access—for example, a memory-mapped UART data register where every read may return a different value (newly arrived data); the compiler absolutely must not optimize it into "read once and cache in a register." Another classic scenario is `setjmp`/`longjmp`—after the jump, the values of `volatile` variables must be up to date.

### What volatile Does Not Do

`volatile` does not guarantee atomicity. The increment `++x` on a `volatile int` is still a three-step read-modify-write in a multithreaded program, and other threads can interleave in the middle of it. Nor does it guarantee memory order—the compiler inserts no barrier for `volatile` accesses, and the CPU's out-of-order execution is completely unaffected. It certainly does not guarantee how the cache coherence protocol is involved—volatile variables do end up in main memory, but that has nothing to do with happens-before.

In short, `volatile` solves the problem of "compiler, don't get too clever," while thread safety has to solve "compiler and CPU, neither of you gets to be clever, and the operations must be atomic"—these are two completely different levels of concern.

### A Classic volatile Misuse

```cpp
#include <thread>
#include <iostream>

volatile bool ready = false;
int data = 0;

void producer()
{
    data = 42;
    ready = true;  // volatile write, but visibility to other threads is not guaranteed
}

void consumer()
{
    while (!ready) {
        // spin: may never see ready become true
    }
    std::cout << data << "\n";  // may print 0 instead of 42
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

This code will most likely "work correctly" on x86—because x86's TSO model is strong enough, and because `volatile` prevents the compiler from caching `ready` in a register (so the consumer's loop does re-read from memory every iteration). But on ARM or PowerPC, this code may fail completely: the CPU's store buffer may leave `ready = true` invisible to the consumer, or the CPU may reorder the writes `data = 42` and `ready = true`.

The correct version uses `std::atomic`:

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
        // spin: acquire semantics guarantee we see every write made before the release
    }
    std::cout << data << "\n";  // guaranteed to print 42
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

### Correct Uses of volatile

`volatile` is not useless—it is just that its use cases have nothing to do with multithreading. It suits communication between a signal handler and the main thread (also explicitly permitted by the POSIX standard), protecting variables across `setjmp`/`longjmp`, and memory-mapped access to hardware registers. In these scenarios, the "other party" involved is either a signal handler on the same CPU core (no cache coherence involved) or a hardware device (via MMIO).

If you do need to synchronize between a signal handler and the main thread, the standard library provides `std::atomic_signal_fence`, which we covered earlier—it was designed for exactly this scenario, its semantics are far clearer than `volatile`'s, and combined with `std::atomic` it provides complete synchronization guarantees.

## Comparing volatile and std::atomic

Finally, a crisp comparison. `volatile` tells the compiler "don't optimize accesses to this variable," but it says nothing about atomicity, memory order, or visibility across threads. `std::atomic` tells the compiler and the CPU "accesses to this variable must be atomic, and I can specify a memory order," and it provides complete inter-thread synchronization guarantees. The two solve entirely different problems and cannot substitute for each other.

One exception worth mentioning: MSVC historically added acquire/release semantics to `volatile` variables—a volatile read has acquire semantics, and a volatile write has release semantics. This is a non-standard extension, in effect under the `/volatile:ms` compiler option (and the default on ARM). GCC and Clang provide no such guarantee. If your code relies on MSVC's volatile semantics for thread safety, it will not port to other compilers. The standards committee explicitly refused to standardize MSVC's behavior, because doing so would restrict the compiler's ability to optimize.

## Exercises

### Exercise 1: Fence Placement Analysis

Is the fence usage in the code below correct? If in `thread_b` both `arr[0]` and `arr[1]` read as something other than -1, is it safe to read `data[v0]` and `data[v1]`? If only one fence could be kept (either the release fence or the acquire fence), which one's removal would break correctness?

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

Hint: recall the three conditions for fence-fence synchronization—there exists an atomic object M; in thread A there is a write X to M with the release fence sequenced-before X; and in thread B there is a read Y of M with Y sequenced-before the acquire fence.

### Exercise 2: Diagnosing volatile

Analyze the possible behavioral differences of the code below on x86 versus ARM. Explain why `volatile` "appears to work" on x86 but may fail on ARM. If you were to replace it with `std::atomic`, what is the minimal change?

```cpp
volatile int flag = 0;
int value = 0;

// thread 1
void writer()
{
    value = 100;
    flag = 1;
}

// thread 2
void reader()
{
    while (flag == 0) {}
    printf("value = %d\n", value);
}
```

### Exercise 3: Compiler Barriers vs. CPU Barriers

Determine whether each of the following statements is correct, and explain your reasoning:

1. `std::atomic_signal_fence(memory_order_release)` generates a CPU barrier instruction.
2. On x86, `std::atomic_thread_fence(memory_order_acquire)` does not need to generate any CPU instruction.
3. `asm volatile("" ::: "memory")` can prevent the CPU's out-of-order execution.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); go to `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## References

- [std::atomic_thread_fence -- cppreference](https://en.cppreference.com/cpp/atomic/atomic_thread_fence)
- [std::atomic_signal_fence -- cppreference](https://en.cppreference.com/cpp/atomic/atomic_signal_fence)
- [DMB, DSB, and ISB -- Arm Developer](https://developer.arm.com/documentation/dui0489/e/arm-and-thumb-instructions/miscellaneous-instructions/dmb--dsb--and-isb)
- [MFENCE -- x86 Instruction Reference](https://www.felixcloutier.com/x86/mfence)
- [Fences as Memory Barriers -- Modernes C++](https://www.modernescpp.com/index.php/fences-as-memory-barriers/)
- [C++ Standard Draft [atomics.fences] -- eel.is](https://eel.is/c++draft/atomics.fences)
