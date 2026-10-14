---
title: "volatile Doesn't Stop Data Races: Lost Updates Up Close and the Line Drawn by the lock Prefix"
chapter: 7
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
description: "Two threads concurrently increment a volatile int: six runs at 100,000 iterations per thread all come out right, eight runs at one million all lose updates; then three source statements fuse back into a single addl, a compiler barrier truly opens the window, and finally the lock prefix of std::atomic marks the machine-code line between forbidding the compiler and forbidding races"
reading_time_minutes: 9
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/07-what-volatile-guarantees/02-volatile-race-and-lock-prefix.md
  source_hash: a3fff743bd5d444514841966f4f5801bddfdb437ae3e561b607d0d46c0cd9445
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# volatile Doesn't Stop Data Races: Lost Updates Up Close and the Line Drawn by the lock Prefix

The previous piece ended on a list of bans: merging, elimination, and reordering. Within a single thread volatile forbids the compiler from doing all three, and those experiments never left one function, one thread. This piece steps outside the single thread: two threads share one volatile variable and even increment it concurrently. Does volatile still hold? Let's keep checking, against the race scene and the disassembly. The environment is the same one as [the previous piece](01-volatile-bans-in-one-thread.md), so follow it when you rerun anything.

## The Turn: So Does volatile Work with Multiple Threads?

At this point a very widespread piece of folklore is waiting for us: two threads share a variable, one writing while the other reads, and as long as the variable is qualified with volatile, each side sees the other's fresh value right away, right? Push the question one step further: when both threads increment it, can volatile keep the count from leaking? Let's just run the experiment, with the ground rules stated up front: the program contains no synchronization of any kind. Two threads reading and writing the same volatile int concurrently with no synchronization between them is, by the C++ standard's book, a data race. A data race means two threads access the same object, at least one of the accesses is a write, and no synchronization separates them. The standard classifies it as undefined behavior, and undefined behavior means the standard no longer promises any boundary on the program's behavior. The output below only records what actually happened on my machine. Switch machines, switch the load, and what you see could easily differ.

```cpp
// v4_race.cpp: a shared volatile variable incremented concurrently by two threads, a data race (undefined behavior) demo
// This program demonstrates UB; it only records what actually happened on my machine
#include <cstdio>
#include <thread>

volatile int counter = 0;

int main() {
    constexpr int kIters = 100000;  // increments per thread

    std::thread t1([] { for (int i = 0; i != kIters; ++i) ++counter; });
    std::thread t2([] { for (int i = 0; i != kIters; ++i) ++counter; });
    t1.join();
    t2.join();

    std::printf("volatile counter = %d (2*%d = %d if no update were lost)\n",
                counter, kIters, 2 * kIters);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 -pthread v4_race.cpp -o v4_race
```

Two warnings appeared during the compile. We never turned on `-Wall`; GCC 16 reported them by default:

```text
v4_race.cpp: In lambda function:
v4_race.cpp:11:61: warning: '++' expression of 'volatile'-qualified type is deprecated [-Wvolatile]
   11 |     std::thread t1([] { for (int i = 0; i != kIters; ++i) ++counter; });
      |                                                             ^~~~~~~
v4_race.cpp: In lambda function:
v4_race.cpp:12:61: warning: '++' expression of 'volatile'-qualified type is deprecated [-Wvolatile]
   12 |     std::thread t2([] { for (int i = 0; i != kIters; ++i) ++counter; });
      |                                                             ^~~~~~~
```

Where that warning comes from gets a dedicated look later. For now just note that it stems from a C++20 deprecation. We ran the program 6 times in a row, and the output is pasted verbatim:

```text
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
```

All six runs printed 200000, and the number of lost updates is zero. If you take this result as proof that volatile protects the count, this piece was written in vain. Let's read the disassembly for the cause. The thread body in it looks like this (`objdump -d --no-show-raw-insn v4_race`):

```asm
0000000000001370 <...6_M_runEv>:
    1370:	mov    $0x186a0,%eax
    1375:	data16 cs nopw 0x0(%rax,%rax,1)
    1380:	addl   $0x1,0x2cdd(%rip)        # 4064 <counter>
    1387:	sub    $0x1,%eax
    138a:	jne    1380 <...6_M_runEv+0x10>
    138c:	ret
```

Time to meet the term read-modify-write: read the old value once, modify it, write it back. The three steps taken together are one read-modify-write, and an increment is the most typical one there is. Let's also identify the two opening instructions: `mov $0x186a0,%eax` loads the loop count, and 0x186a0 is exactly 100000 written in hex; `data16 cs nopw` is alignment padding, a no-op. The real player is the addl in the middle. In the assembly, `++counter` was compiled into a single `addl $0x1, counter(%rip)`: one instruction does the read, the add, and the write in a single stroke, and the read-modify-write window shrinks to a few cycles (a cycle is a clock tick, the CPU's beat). For an update to be lost, the two threads' addl instructions have to land on the same few cycles, which is unlikely to begin with. Stack on top the serialization that comes free whenever a cache line (the smallest unit of CPU cache management, commonly 64 bytes) shuttles back and forth between two cores, and at the 100,000 scale, none of my six runs ran into it.

But let's draw the boundary cleanly: a lone addl carries no lock prefix, so it still does not qualify as an atomic instruction. A small window is not a closed window. Six runs without an observed lost update do not make the program correct. Undefined behavior never promised that the disaster would certainly come on stage; it won't even say when. That is exactly what makes it so nasty: a passing test gives you no guarantee at all. Words only carry us so far, so let's scale the load up tenfold, to 1,000,000 increments per thread. One constant changes in the code, `kIters = 1000000`, and the compile warnings are the same as before. Here is the output of all eight runs:

```text
volatile counter = 1681797 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1046051 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1492306 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1609693 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1895598 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1159715 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1029614 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1086008 (2*1000000 = 2000000 if no update were lost)
```

All eight runs lost updates. The hardest fall was 1029614, nearly half of 2000000 gone, close to throwing away one entire thread's full workload. The best run, 1895598, still lost about 5%. How much disappears is random in every run, because whether anything is lost depends on how the two executions interleave in time, and the timing is reshuffled on every run. The same code sits right in front of us: at 100,000 per thread it went six for six, at 1,000,000 it went eight for eight lost. Only with the two results side by side have you read them completely. What a race costs depends on the window and the timing: scale the load up and the probability piles up until the scene can no longer hide. If you have seen a multithreaded volatile counter run in a project for years without incident, it enjoyed exactly the luck of our six runs.

A word on reproducing this, also up front: every number pasted in this piece is an on-the-spot record from my machine. Run v4_race and v4_race_1m on your own machine (they differ by the single constant kIters); your load and scheduling timing differ from ours, and your results may differ too. And the judgment that not blowing up does not mean correct never depended on any particular run's numbers: at the 100,000 scale this machine went six for six correct, at the 1,000,000 scale it went eight for eight lost. Same code, different load, different outcome, which is precisely the road we just walked.

## Split into Three Statements, Still Fused into One Instruction

Reading this far you may already have invented an escape: ++ squeezes its read-modify-write into one instruction, and that is why the window is so narrow. So split the read, the arithmetic, and the write into three statements, and the window opens, no? The compiler couldn't glue them back together, could it? GCC 16's measured answer: it could. Let's also pick up the word fusing here, because the rest of this piece leans on it: when the compiler assembles two independent memory accesses into the same instruction, we call it fusing. Here is the source we will look at:

```cpp
// v4_race_rmw.cpp: read, add, write split into three statements, watching whether GCC still fuses them
#include <cstdio>
#include <thread>

volatile int counter = 0;

void bump() {
    int v = counter;   // volatile read
    v = v + 1;         // ordinary arithmetic
    counter = v;       // volatile write
}

int main() {
    constexpr int kIters = 100000;

    std::thread t1([] { for (int i = 0; i != kIters; ++i) bump(); });
    std::thread t2([] { for (int i = 0; i != kIters; ++i) bump(); });
    t1.join();
    t2.join();

    std::printf("volatile counter = %d (2*%d = %d if no update were lost)\n",
                counter, kIters, 2 * kIters);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 -pthread v4_race_rmw.cpp -o v4_race_rmw
```

`bump`'s disassembly: two instructions in all.

```asm
0000000000001410 <_Z4bumpv>:
    1410:	addl   $0x1,0x2c4d(%rip)        # 4064 <counter>
    1417:	ret
```

We ran it 6 times, and every run printed 200000:

```text
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
```

The three source statements we wrote came out as a single instruction in machine code. Between the volatile read and the volatile write stood only an ordinary addition, and GCC 16 fused them into a lone addl all the same. From the abstract machine's point of view (the fictional machine model the standard reasons with, introduced back in 01), the instruction carried out one read and one write: the number of accesses did not shrink, the order stayed as written, and semantically it is fully compliant. But at the machine level the window between read and write no longer exists, and six correct runs make this version behave exactly like the 100,000-increment one. Let's state the judgment precisely: splitting at the statement level is not splitting at the instruction level. The window you count in the source only counts once you know how many instructions the compiler packed it into. volatile keeps its grip on the number of accesses and their order; the one thing it cannot stop is those accesses being packed into the same instruction.

## A Compiler Barrier Truly Opens the Window

To open the window for real, what we call on is the compiler barrier. One plain sentence for the word barrier: a wall that binds only the compiler, occupies not one byte of machine code, yet forbids the compiler from merging memory accesses on either side of it or moving them across it. We will use GCC's inline assembly form `asm volatile ("" : "+r"(v))`: an empty piece of inline assembly whose `"+r"(v)` constraint binds the variable v into its input-output list. The compiler has to assume that the value v holds from the read on this side of the wall and the value written back on the other side know nothing about each other, so it dares not merge the accesses at either end. Amusingly, even this wall is held up by volatile: asm volatile declares that the inline assembly has side effects and must not be deleted. Notice the asymmetry, it cannot reach the CPU at all, yet wherever the job is reining in the compiler, it works everywhere. In this group of experiments the barrier is only the means of observation; what it blocks is the compiler merging the two accesses of the thing under observation, and the thing under observation is, from start to finish, that volatile int counter:

```cpp
// v4_race_split.cpp: a compiler barrier between the read and the write, observing the shape of the race once the window is open
#include <cstdio>
#include <thread>

volatile int counter = 0;

void bump() {
    int v = counter;                  // volatile read
    asm volatile ("" : "+r"(v));      // compiler barrier: keeps the two accesses from fusing (means of observation, not the thing observed)
    counter = v + 1;                  // volatile write
}

int main() {
    constexpr int kIters = 1000000;

    std::thread t1([] { for (int i = 0; i != kIters; ++i) bump(); });
    std::thread t2([] { for (int i = 0; i != kIters; ++i) bump(); });
    t1.join();
    t2.join();

    std::printf("volatile counter = %d (2*%d = %d if no update were lost)\n",
                counter, kIters, 2 * kIters);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 -pthread v4_race_split.cpp -o v4_race_split
```

```asm
0000000000001420 <_Z4bumpv>:
    1420:	mov    0x2c3e(%rip),%eax        # 4064 <counter>
    1426:	add    $0x1,%eax
    1429:	mov    %eax,0x2c35(%rip)        # 4064 <counter>
    142f:	ret
```

This time `bump` is a genuine three instructions: load, add one, store back. The barrier itself costs not a byte, exactly the property we just stated. The load is 1,000,000 per thread, and here is the full output of all six runs:

```text
volatile counter = 1181492 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1186724 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1499203 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1364355 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1067062 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1229806 (2*1000000 = 2000000 if no update were lost)
```

With the window open, lost updates appear at scale, and the six runs lost between 25% and 47%. What sits between the read and the write now is an add. Both threads can read the same old value, each then adds one, and each writes back: two 1s land in the same spot, and the net effect is still just one. That load, add, store-back shape of assembly is the minimal scene of a read-modify-write race. One more thing to note here: this barrier stops only the compiler. At the CPU layer it does not spend half a byte of attention; the fence that actually plants a wall in the instruction stream is a different animal, and the next section points you to where it lives.

## All It Takes Is a lock Prefix

The scenario stays as it was. We swap the counter from `volatile int` to `std::atomic<int>` and keep the 100,000-increment-per-thread load and the loop shape untouched:

```cpp
// v5_atomic.cpp: same scenario, std::atomic instead, the count should now always be correct
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<int> counter{0};

int main() {
    constexpr int kIters = 100000;

    std::thread t1([] { for (int i = 0; i != kIters; ++i) ++counter; });
    std::thread t2([] { for (int i = 0; i != kIters; ++i) ++counter; });
    t1.join();
    t2.join();

    std::printf("atomic counter = %d (expected %d)\n",
                counter.load(), 2 * kIters);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 -pthread v5_atomic.cpp -o v5_atomic
```

All eight runs returned `atomic counter = 200000 (expected 200000)`, without a single line deviating, so we will not fill the page with eight identical lines. The real protagonist is the thread body's disassembly:

```asm
00000000000013f0 <...6_M_runEv>:
    13f0:	mov    $0x186a0,%eax
    13f5:	data16 cs nopw 0x0(%rax,%rax,1)
    1400:	lock addl $0x1,0x2c5c(%rip)        # 4064 <counter>
    1408:	lock addl $0x1,0x2c54(%rip)        # 4064 <counter>
    1410:	sub    $0x2,%eax
    1413:	jne    1400 <...6_M_runEv+0x10>
    1415:	ret
```

One plain sentence for lock: it is an x86 instruction prefix, written at the very front of an instruction, and its job is to demand exclusive execution from the CPU. While the instruction runs, this core holds the cache line it touches, no other core can squeeze in, and the whole read-modify-write becomes indivisible at the hardware level. GCC also unrolled the loop by 2: each pass of the loop body issues two `lock addl` instructions (their offsets differ by 8, which is simply each instruction's 8-byte length showing through), and the closing `sub $0x2,%eax` and `jne` let two increments through per pass, matching the by-2 unrolling. Each thread ends up with exactly 100,000 increments, not one short, and the unrolled scheduling breaks the atomicity of no single increment.

Put the increment instruction from the volatile version's thread body next to the one from the atomic version, and you have the hardest-hitting comparison of the whole piece:

| Version | Increment instruction in the thread body |
|---|---|
| volatile int, ++counter | `addl $0x1,0x2cdd(%rip)` |
| std::atomic&lt;int&gt;, ++counter | `lock addl $0x1,0x2c5c(%rip)` |

The instruction bodies are identical, the operand shapes match, and the only difference is the word lock in front. At the source level the compiler cannot tell which side will lose updates; what it hands to both sides is legal code. The difference lands entirely at the machine layer, and the job of backstopping goes to the hardware. volatile's addl against atomic's lock addl: that one word is the line between forbidding the compiler and forbidding races, projected into machine code. Thread in the earlier barrier and the whole picture closes: volatile and the compiler barrier both address the compiler, and the machine code spends not one extra byte on either, while the lock prefix addresses the CPU, and what it says is written right into the instruction.

lock is where the atomic story opens, not where it ends. Atomic operations also carry memory ordering (memory_order) parameters, governing whether ordinary reads and writes around the atomic access may be rearranged by the CPU, and on that dimension volatile has no counterpart at all. That ground belongs to vol5's concurrency volume, and we will not tread on it: [Atomic Operations](../../../vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md) and [A Deep Dive into Memory Ordering](../../../vol5-concurrency/ch03-atomic-memory-model/02-memory-ordering.md) give both a full measured treatment, and the difference between the compiler barrier from the previous section and a genuine CPU barrier is laid out in [Fences and Compiler Barriers](../../../vol5-concurrency/ch03-atomic-memory-model/03-fence-and-barrier.md), in the same chapter. As for which promises volatile actually makes in writing, the next piece goes to the standard's own text.
