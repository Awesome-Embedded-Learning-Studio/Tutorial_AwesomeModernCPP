---
title: "Data Races and ThreadSanitizer, Lesson One"
chapter: 0
order: 2
description: "Watch a two-thread counter expose a data race with your own eyes, read the standard's definition and its UB verdict, then use ThreadSanitizer to turn an invisible race into a report you can read"
tags:
  - host
  - cpp-modern
  - beginner
  - atomic
  - mutex
difficulty: beginner
platform: host
cpp_standard: [11]
reading_time_minutes: 16
prerequisites:
  - "Why Concurrency: A Blocked Main Loop"
related:
  - "mutex and RAII Locks"
  - "Atomic Operations and happens-before"
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/02-data-race-and-tsan.md
  source_hash: 081057a172e61653905f4cc1f03c8ff3bde7f9a73e8ef5b3580e6a8e360459e5
  translated_at: '2026-09-29T14:40:17+00:00'
  engine: anthropic
  token_count: 11000
---

# Data Races and ThreadSanitizer, Lesson One

In [Why Concurrency: A Blocked Main Loop](./01-why-concurrency.md) we squared away the benefits and the costs of concurrency, and the volume opener also laid down three principles, the first of which is `correctness first, performance second`. In this article we take on the nastiest problem inside correctness: the data race. What makes it nasty is that it is invisible—the program does not necessarily crash on you, the result it produces is often right, and the error happens only in one particular interleaving, one that may never show up.

So this article has a second job as well: to bring out the volume's first tool, ThreadSanitizer (TSan from here on), and let you watch a race with your own eyes on your own machine.

Let's lay out the route too. First we write a very small bad program, read through the standard's definition of a data race, work out why the standard hands it a verdict of undefined behavior, and take a quick look at what a fix looks like. Then TSan takes the stage, and we walk a complete round trip from compiling to reading the report. Every piece of code, every command, and every symptom in this article reproduces on an ordinary Linux machine or on WSL2; all you need is g++ and a few dozen lines of source. Typing it through yourself beats just reading it by a wide margin—with a race, seeing is believing.

## A Counter That Looks Harmless

The program is small enough to paste whole: one global int, two threads each adding to it a hundred thousand times, and a print after the joins.

```cpp
#include <iostream>
#include <thread>

int counter = 0;  // plain int: not atomic, and no lock protecting it

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        ++counter;              // read -> add -> write, three steps
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

Compiling and running it is just two ordinary commands:

```bash
g++ -O2 -pthread 01_data_race.cpp -o race
./race
```

What do you guess it prints? The two loops add up to two hundred thousand increments, so the natural expectation is 200000. The author compiled with -O2 on a 20-core WSL2 box (GCC 16.2.1) and ran it five times in a row; all five runs printed 200000—not a single count was lost.

Our program looks perfectly healthy—so healthy that you can safely forget about it.

Now turn the optimizer off and try again: the command is unchanged word for word, only -O2 swapped for -O0, and the numbers collapse at once—five runs produced 116007, 102861, 100000, 162707, 100000, and two of those were exactly 100000 on the nose. Against the expected two hundred thousand, even your best run barely scraped past one hundred sixty thousand.

<!-- Experiment backfill: the complete output record for ten runs of each of the -O2 and -O0 versions (noting the machine and the GCC version) -->

Same source file, only the optimization level changed, and the result falls from 200000 to around 100000. If you attribute that to -O0 exposing the bug and -O2 getting lucky, your direction is only partly right. What really deserves a pause is this: in the code -O2 generates, why does this program riddled with races look completely unscathed? Take a look at the body of increment with objdump and the answer is right there:

```text
$ objdump -d --no-show-raw-insn race | grep -A 4 '_Z9incrementi>:'
00000000000013d0 <_Z9incrementi>:
  13d0:	test   %edi,%edi
  13d2:	jle    13da <_Z9incrementi+0xa>
  13d4:	add    %edi,0x2dba(%rip)        # 4194 <counter>
  13da:	ret
```

You can run it yourself: -d is disassemble, and --no-show-raw-insn hides the machine-code bytes. C++ function names get mangled when they enter the binary; `increment(int)` becomes `_Z9incrementi` in the symbol table, and grep grabs it by that name. Addresses and offsets differ from machine to machine, but the shape of the instructions is the same.

The hundred-thousand-iteration loop we were looking for is gone. The compiler folded the entire for loop into a single add instruction: it adds 100000 straight to counter, and edi holds times. Why is that beyond reproach? Under single-threaded semantics the transformation is seamless—adding a hundred times is adding 100, adding ten thousand times is adding 10000, and by induction it is adding times. But when two threads each perform one non-atomic add, the race stays exactly where it was; only the conflict window shrinks from a hundred thousand instructions to one, the odds of hitting it shrink along with it, and the result comes out right almost every time.

Look the other way, though: under -O0 the version that dutifully loops a hundred thousand times has an enormous window, and it loses updates with a clear conscience—those two runs of 100000 are the extreme value the two threads produce when they march in perfect lockstep: in every round both of them read the same number and write back the same number, so across a hundred thousand rounds the total only climbs by a hundred thousand.

One thing we want you to take to heart here: **getting the right result does not mean the program is right.** We judge right and wrong by what the standard says, not by the output of one particular run. Adding printf debugging doesn't help either—the print itself changes the timing, and the bug may well be scared off by that one line of output. Veterans have a name for this kind of bug that slips through your fingers: a Heisenbug.

The naked eye cannot keep watch on it, and luck is not something to lean on, so we need an observer that does not change the program's semantics. Before we can get our hands on one, though, we have to be clear about exactly what it is we want to observe—so let's go read the standard.

## What the Standard Says: The Definition of a Data Race

data race is a formal term in the C++ standard, not a word we invented, and its definition lives in the [intro.races] clause:

> `The execution of a program contains a data race if it contains two potentially concurrent conflicting actions, at least one of which is not atomic, and neither happens before the other.` (An execution of the program contains a data race when two potentially concurrent conflicting actions appear, at least one of them is not atomic, and neither happens before the other.) The verdict that follows immediately: `Any such data race results in undefined behavior.`

That one sentence packs in three terms, so let's use the counter above as our specimen and check them off one by one.

The first is **conflicting**: the two expressions access the same memory location, and at least one of them is a write. In the program both threads write to counter's four bytes—write against write—so the conflict holds. Read against write and write against write both count; only two pure reads don't—if both sides are just looking and never touching, neither gets in the other's way.

The second is **potentially concurrent**: it refers to actions in different threads, or to actions between a thread and its signal handler. We only care about the former—the two increments run on two threads, so that requirement holds too.

The third is **happens-before**, the one most foreign to us, so let's take an intuitive version first: A happens-before B, roughly speaking, means A's effects are visible to B and A comes before B—the order is fixed and cannot be overturned. Such orders are established by **synchronization actions**. Look at join: after `t1.join()` returns, the main thread reads its counter, and that read is separated from the writes in the thread, so there is no race. Thread creation is a source too: writes that happen before the `std::thread` is constructed are also visible to the new thread. The program's real race is between the two threads' `++`, and there no synchronization action exists at all—neither side waits for the other.

> Let's fold the definition into a checklist, and run every suspected race through it from now on: **the same memory location? at least one write? separated by happens-before?** Hit all three and the data race holds. Miss any one of them and only then can we talk about there being no race.

Walk the counter through it: same location, writes on both sides, and nothing in between—no lock, no join—to separate them. All three hold, and the verdict is clean.

happens-before has a precise mathematical definition, whose authoritative source is the article [Atomic Operations and happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md); this article stops at intuition.

## Why It Is Classified as Undefined Behavior

Some readers may ask: why doesn't the standard simply give us a rule—say that a racy read at worst gets a stale value—so that we have something to expect? Hans Boehm is one of the principal designers of the C++ memory model, and he wrote a whole page answering exactly this question; his argument can be retold verbatim using what just happened on our own machine.

What we just saw was the transformation folding a hundred thousand `++` into a single add. Under single-threaded semantics that transformation cannot be faulted, yet in passing it changes the result of a racy execution beyond recognition. Think about it: if the standard prescribed any definite semantics for a data race—even something as mild as "at worst you get a stale value"—every transformation of this kind would have to be audited one by one, and the compiler's hands would be tied. The standard took the other road: **a racy program has no semantics, so the compiler cannot break its contract no matter how it optimizes, and the entire responsibility lands on the person writing the code.** Boehm's position is blunt: `Data races among ordinary variables are a bug`.

The license UB grants is also far broader than "you read a stale value": reading a value torn in half from the same variable, writes reordered into an unrecognizable position, an entire branch optimized away—all of it is within the license. On the x86 machines we use, aligned int reads and writes do not tear by themselves, but change the platform or change the compiler and nobody gives you that guarantee. Some people have also toyed with the notion of a "benign race," feeling that certain races look harmless anyway—that -O2 counter that comes out right every single time is a living example. It is precisely that harmlessness that makes UB so insidious: **it does not owe you a crash, and when one does come it will not announce itself.**

That sounds cold, but it is actually a form of respect for the person writing the code: the standard won't fob you off with "it's probably fine"; it draws the boundary clearly and hands you the tool for making the judgment too. The rest of this article is about putting that tool in your hands.

::: details A World Without Races Has Order: DRF-SC

As long as a program has no data race, and its atomic operations all use the default memory order, its behavior has a uniform sequentially consistent semantics to fall back on. That guarantee has a name: **DRF-SC**, that is, sequential consistency for data-race-free programs. Its formal statement lives in the article [Atomic Operations and happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md); for now you only need to accept one thing: the world without races has order, and the world with races has no standard.

:::

## What the Fix Looks Like

The complete system of fixes—the lock family, condition variables, atomics, memory orders—takes two whole chapters. But we already understand the root cause of counter's illness: what is missing between two non-atomic writes is precisely an order. The fix follows the root cause: either establish a definite order for that pair of accesses, or make the write itself indivisible. The lock version changes the least:

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
        ++counter;              // the two threads' ++ now queue up
    }
}
// main and the printing part are unchanged
```

One mutex guards every access to counter, the two threads' `++` now queue up, and the race disappears. Put in terms of the definition: acquiring and releasing the lock establishes happens-before, so the previous holder's writes are visible to the next holder. We use `lock_guard` here as a black box—it manages acquiring and releasing the lock for us, and it doesn't miss a release on the exception path either; the full story of RAII is in [mutex and RAII Locks](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md).

The other road is to replace counter with `std::atomic<int>`, making the write itself an indivisible step:

```cpp
#include <atomic>

std::atomic<int> counter{0};

void increment_atomic(int times)
{
    for (int i = 0; i < times; ++i) {
        counter.fetch_add(1);     // one atomic read-modify-write replaces the read-add-write three steps
    }
}
```

`fetch_add` replaces the three steps of read-add-write with a single atomic read-modify-write, and for a single-variable counting scenario it is often lighter than a lock. It can also take a memory-order argument, though (`memory_order_relaxed` is one of them), and that water runs deep: the authoritative sources are [Atomic Operations and happens-before](../ch03-atomic-memory-model/02-atomics-and-happens-before.md) and [Memory Ordering](../ch03-atomic-memory-model/03-memory-ordering.md), and this article does not open the topic for a single word. The rule of thumb for choosing is easy to remember too: **for a single-variable counter use atomic; once several variables have to change together, use a mutex to guard the whole critical section at once.**

Is it fixed or not? Words prove nothing—let the tool verify it.

## TSan: Turning an Invisible Race into a Report

### Compiling and Running

To enable TSan, put `-fsanitize=thread` on both the compile and the link. One g++ command covers both ends:

```bash
g++ -fsanitize=thread -g -O2 -pthread 01_data_race.cpp -o race_tsan
./race_tsan
echo $?     # when it reports a data race, the default exit code is 66
```

Let's walk the flags one by one. `-fsanitize=thread` is the main body: the compiler instruments around every memory access in the program, and the TSan runtime checks them pairwise. `-g` makes the report's call stacks carry source locations; without it, all you see is a string of addresses. `-pthread` goes on every time; libstdc++ programs that use threads all need it.

The most interesting one is -O2: some older tutorials warn you not to optimize when TSan is on, saying the call stacks become unreadable. The official wiki's current wording says exactly the opposite—`To get a reasonable performance add -O2`, `Use -g to get file names and line numbers in the warning messages`. In the author's smoke test on this machine, the -O2 plus -g stack resolved perfectly well, so we follow the wiki.

Why doesn't instrumentation count as changing semantics? Because what TSan adds is checking, not ordering: it records every access and checks the pairs; it inserts no waits, and it postpones nobody's execution. So it **can see a race but cannot cure one**; the cure is still the locks and atomics in your hands.

What does it look like when you run it? The program prints counter as usual, the number is still that healthy-looking value, and right after that TSan flushes a report to stderr, and the process exits with code 66. 66 is not a signal of a crash; it stands for TSan's default failure exit code, and in continuous integration (CI) you can decide on it straight from `$?`—a one-line script job. The author's smoke test on this machine: GCC 16.2.1, WSL2 kernel 6.18; the command worked as-is, and the two-thread, hundred-thousand-increment program reported a race on the very first run.

CMake projects use the same wording, with both the compile and the link sides configured:

```cmake
add_executable(race_tsan 01_data_race.cpp)
target_compile_options(race_tsan PRIVATE -fsanitize=thread -g)
target_link_options(race_tsan PRIVATE -fsanitize=thread -pthread)
```

<!-- Experiment backfill: the TSan report verbatim (WARNING, the call stacks of both conflicting sides, Location, the thread-creation stacks, SUMMARY) -->

### How to Read the Report: Three Questions

The report's structure has been fixed for years. One WARNING line reports the incident; the two conflicting sides each get a section, headed by either `Write of size 4` or `Read of size 4`, each with its own call stack; one Location line names the memory location; the next two sections are the thread-creation stacks, each headed `created by main thread at`; and a final SUMMARY line closes it out. When reading, the author's habit is to run through three questions.

**First: which two accesses conflict?** The report lays the two sides out in pairs—one section is the offending Write (or Read), the other is the Previous write (or Previous read), each tagged with a file name and line number. The word Previous also puts them in order for you: the one marked Previous comes first in time. In our program both line numbers land on that `++counter` line inside increment—a write-against-write conflict, exactly the verdict we reached by checking it against the definition.

**Second: which two threads are they?** Each access section is tagged with the id of the thread it belongs to, and at the end the report gives the creation-point call stack for each thread, likewise headed `created by main thread at`. Further down you will see frames inside `pthread_create` and `std::thread`; the two construction lines in main that you are looking for may not survive on the stack once optimization is on (in that smoke test they had been pruned), but seeing where the thread came from is enough. This question is especially useful when there are many threads: when five threads fight over one variable, you need to know which two are tangled up together.

**Third: what is missing between the two accesses?** We answer on its behalf: synchronization is missing—no lock isolating them, no atomic helping out, and no join establishing an order that separates them. Turning the definition of a data race around backward gives you TSan's criterion. Answer all three questions and the report is read through: **who, where, and what is missing in between.**

There is a small trick to reading the stack as well: in our smoke test the top frame was cleanly increment, and a few frames further down frames like `_M_run` in `invoke.h` and `std_thread.h` start showing up. Those are the mechanism frames `std::thread` uses to run your function on your behalf; not knowing them is no reason to panic—skip them when reading the report, and just recognize the frames that carry your own project's paths.

### Re-running the Fixed Version

Once you have read the report for the bad program, run the fixed version through TSan as well, and one complete round trip is done:

```bash
g++ -fsanitize=thread -g -O2 -pthread 02_data_race_mutex.cpp -o race_fixed_tsan
./race_fixed_tsan
echo $?     # 0: a clean report
```

counter dutifully prints 200000, TSan says not a word, and the exit code comes back from 66 to 0. The code repository gives this article's four examples a CMake target each: `01_data_race.cpp` is the bad counter, `02_data_race_mutex.cpp` is the mutex-fixed version, `03_check_then_act.cpp` is the vector example from the check-then-act section later on, and `04_deadlock_reorder.cpp` is the deadlock example at the end; pull it down and you can re-run every step of this article.

### Limits, Overhead, and Companions

Even the prettiest algorithm has engineering limits, and both ends deserve to be stated clearly. **One end is that whatever it reports counts**: as long as the whole program is compiled with instrumentation, every report TSan produces corresponds to a pair of conflicting accesses that really happened in that execution; it does not make things up. **The other end is that not reporting proves nothing**: TSan only sees the paths this execution really took, so if your test never lets the two accesses meet, it has nothing to say—which is why concurrency tests should vary the thread count, vary the task granularity, and run many rounds. The official wiki says it plainly too: `There is tiny probability to miss a data race though`—the history kept in the shadow cells is finite, and old records get squeezed out in extreme scenarios. Put it in one sentence: **a report means a real race; no report does not mean no race.**

On overhead, the wiki's wording for a typical program is `for a typical program, memory usage may increase by 5-10x and execution time by 2-20x`—memory up five to ten times, execution time two to twenty times slower; having the order of magnitude in mind is enough. TSan binaries are for tests and CI; we don't run them in production.

There is one more hard limit: `-fsanitize=thread` cannot be enabled together with `-fsanitize=address` or `-fsanitize=leak`; the compiler errors out on the spot, so wanting both sanitizers means building two separate binaries. ASan handles memory errors, TSan handles races, and we will meet its division of labor formally in [Thread Arguments and Lifetime Traps](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md).

<!-- Experiment backfill: the run-time and memory-footprint comparison between the TSan build and the ordinary build on this machine -->

> There is one more environmental ailment worth recognizing on sight: a batch of WSL2 users in the community have reported that on their kernels at 6.6.6 and above, in some environments TSan dies the moment it starts with `FATAL: ThreadSanitizer: unexpected memory mapping`, without so much as touching the program. What the reporters traced the root cause to is the kernel's `vm.mmap_rnd_bits` being set to 32, which fails to come to terms with TSan's shadow-memory mapping; the fix is to run `sudo sysctl vm.mmap_rnd_bits=28` and re-run. Our 6.18-kernel machine is unaffected, so if you run into it, go back and check that; if you don't, don't go fiddling. Environmental ailments and race ailments must be kept apart: `unexpected memory mapping` means TSan itself failed to start, whereas `WARNING: ThreadSanitizer` means it really caught a race.

::: details How It Decides There Is a Race: Vector Clocks and Shadow Memory

The current version is called TSan v2, which is what ships with GCC and Clang, and it runs a pure happens-before algorithm. You can imagine a **vector clock** hanging on each thread: synchronization events—locking, unlocking, thread creation, join, and the like—advance the respective clocks and establish a partial order between them; every memory access is recorded into **shadow memory**, divided into 8-byte cells, each holding only a scant few entries of history. When two clocks disagree on the partial order and the accesses also conflict, only then does it file a report.

The name vector clock sounds esoteric, but the intuition is simple: each clock records the world its own thread has seen, and a synchronization event is two worlds meeting to sync watches. Once the watches are synced, the two sides can see each other; two clocks that never synced treat each other's accesses as unordered—better to check too much than to let one slip.

Walk both versions through it and it becomes clear. In the bad version, the two threads never meet any synchronization event from birth to death, the two clocks never sync, and so every pair of conflicting accesses pointing at counter becomes a race. In the fixed version, the back-and-forth of t1 releasing the lock and t2 acquiring it syncs the two clocks, t1's write inside the lock becomes visible to t2's read inside the lock, order is established, and the race no longer holds. **What TSan judges is exactly the happens-before from the definition, with nothing extra smuggled in.**

Here we should specifically correct a widespread old claim: plenty of older material says TSan runs a hybrid algorithm, that is, happens-before plus lockset analysis. That is old news from an early version; the current v2 uses pure happens-before plus vector clocks. The lockset line of thinking reports a race whenever two threads don't hold a common lock—intuitive to hear, but with a very high false-positive rate. Keep an eye out when you look things up: for material about the algorithm, take the official wiki's Algorithm page as authoritative.

:::

## Race Condition: Another Kind of Race, Another Kind of Bug

We also need to bring out a pair of twins and tell them apart: data race and race condition—in Chinese both names carry the same word for "race," and people mix them up in droves.

The division of labor between the two goes like this: **data race is a standard term**, and its criterion sits at the memory level—same location, at least one write, no happens-before. **race condition is an engineering concept**, far broader—the program's result depends on the order in which the threads interleave. As for how the two relate, we will commit to only one reliable direction: **even with no data race, a race condition can still exist.**

Why is that? One look at the following pattern explains it: every access to the shared vector properly holds the lock, but checking the capacity and actually enqueueing are two separate critical sections, and in the window between them the world may already have changed.

::: details Example: Every Access Locked, and the Limit Is Still Breached

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
            return;               // check: holding the lock
        }
    }                             // lock released, the window stands open
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    {
        std::lock_guard<std::mutex> lock(data_mtx);
        data.push_back(value);    // act: acquire the lock again
    }
}
```

The driver code has two threads push 60 numbers each, with the limit set at 100. Is there still a data race? No—every access to data is inside the lock, and TSan runs through it in complete silence. But the check and the enqueue are two separate critical sections, and the window between them stands open: both threads saw size as 99 and passed the check, then each enqueued, and the limit was breached. Widen the window to one millisecond and run it, and the size that comes out is 101—the overrun is plain to see; delete that sleep line and the window shrinks back to a few nanoseconds and the overrun becomes a rare sight, but not a line of code has changed, and the bug sits exactly where it was.

The lock controls conflicts at the memory level, but it cannot control a logic hole that splits "take a look then act" into two halves. The fundamental cure is to put the check and the operation into the same critical section, making them one indivisible action; the same account appears in more detail in [mutex and RAII Locks](../ch02-mutex-condition-sync/01-mutex-and-raii-guards.md). This kind of bug has a proper name: **check-then-act**—after the check, before the act, the world changes. It casts a shadow in single-threaded code too: check that a file exists and then go open it, and between the two steps the file gets deleted by someone; concurrency merely widens the window to any instant at all.

:::

As for the relationship in the other direction, let's put it softly: a data race is often also a race condition—its result does depend on interleaving, after all—but that direction does not necessarily hold, and the verdict must rest on the definition rather than on a feeling. Laid out in layers, the order goes: **eliminating data races is the baseline**, and locks and atomics are enough for that; **eliminating race conditions still depends on interface design**, and that layer of craft runs through the whole of Chapter 2.

## Deadlock and Its Relatives: Just the Faces

There are a few other regulars in the concurrency problem family, and this article brings them out for a group appearance, so that you don't think races are the whole story. We will only teach you to recognize their faces: you have seen the name, you know roughly the symptoms, you know where the authoritative source is, and you don't panic when you run into them—that is enough.

The most famous is **deadlock**: two threads each hold one lock and reach out for the one in the other's hand, neither willing to let go, and the program sits there forever. The most common engineering remedy is **a uniform lock order**—the whole project acquires locks in the same order, and circular waiting has no way to form; C++17's `std::scoped_lock` can even lock several at once, carrying an acquisition strategy inside that avoids deadlock. How to rescue a program that really hangs, and how to issue gdb's three commands, is covered at the source in [Deadlock and Live Diagnosis](../ch02-mutex-condition-sync/03-deadlock-and-gdb.md).

Two more relatives: **livelock** is when the threads are all moving and the CPU is all burning, yet nothing makes any progress—both sides politely defer to each other and can never step aside; **starvation** is when some threads never get their turn at a resource while everyone else is going full tilt, waiting round after round with no share in any of them. And one more, **priority inversion**: a low-priority task takes the lock, a high-priority task waits for that lock, and a medium-priority task shoves the low one off the CPU, so the highest task ends up indirectly blocked by two tasks below it. Mars Pathfinder reset over and over on Mars in 1997, and this was the root cause. Their pathologies differ and so do the prescriptions; the deep treatment is all in Chapter 2's [Synchronization Primitives Toolkit](../ch02-mutex-condition-sync/05-sync-primitives-toolkit.md).

::: details What Deadlock Looks Like (Companion Example 04_deadlock_reorder.cpp)

```cpp
std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> a(mtx_a);      // take A
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> b(mtx_b);      // wait for B: B is in thread2's hands
}

void thread2()
{
    std::lock_guard<std::mutex> b(mtx_b);      // take B
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> a(mtx_a);      // wait for A: the order is reversed
}
```

The 50-millisecond sleep in the middle is something the author added to widen the interleaving window; this program deadlocks almost on the first run, and if you run it with `timeout 3 ./deadlock`, it gets cut off by force after three seconds—that is what deadlock looks like. The recipe for deadlock was written down completely by Coffman and his co-authors back in 1971: mutual exclusion, hold and wait, no preemption, circular wait; all four together and it happens, and breaking any one of them breaks the deadlock.

:::

## Exercises

The three problems target three levels—hand computation, judgment, and the full tool workflow—with increasing difficulty, and we suggest you do all of them.

### Exercise 1: Hand-Computing and Measuring Lost Updates

Take this article's counter as your specimen and work out two questions by hand: what is the theoretical upper bound on the result? And what is the lower bound? Here is a hint: the worst interleaving has the two threads marching in perfect lockstep, in every round both of them reading the same value and writing back the same value, so across a hundred thousand rounds the total only climbs by a hundred thousand.

Then compile with -O0 and run it bare ten times, note the largest and smallest values you see, and check them against your hand computation:

```bash
g++ -O0 -pthread 01_data_race.cpp -o race0
for i in $(seq 1 10); do ./race0; done
```

Finally, think one layer further: why does -O2 come out at 200000 almost every time? The article gave you the answer, and looking at it again yourself with objdump makes it stick.

### Exercise 2: Judge a Piece of Code for a Race

Does the following code have a data race? Judge it yourself: if it does, mark out the conflicting pair of accesses according to the standard's definition—where the location is, who reads and who writes, and which requirement is missing. Judge by the definition only, not by the result of a run.

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

The answer is a little counterintuitive: **no.** Under the default memory order, a happens-before is established between (B) and (C) when (C) reads true; the synchronization actions [intro.races] names include atomic operations and mutexes, the default memory order counts, and (A) is ordered before (B) while (D) is ordered after (C), so link by link they separate (A) from (D).

> A follow-up question (come back to it after you have read Chapter 3): replace both (B) and (C) with `memory_order_relaxed` and judge again, and the answer flips to "yes"—relaxed atomic operations do not establish such an order, the reads and writes on value become an unguarded race, and TSan confirms it in testing. Where the flip happens is exactly what the whole of [Memory Ordering](../ch03-atomic-memory-model/03-memory-ordering.md) is about.

### Exercise 3: Walk the Whole TSan Workflow

Compile this article's bad program with TSan, produce a report, and copy three things out of it: the line numbers of the two conflicting sides, the creation lines of the two threads, and the variable that Location points at. Then add a mutex fix—the lock-fixed version `02_data_race_mutex.cpp` in the companion code repository will let you check your answer—and re-run to confirm the report is clean and the exit code goes from 66 back to 0. Keep the commands and the output; from this article on, this is our standard move for every concurrency deliverable.

One step further, and you can write two builds into CI: the ordinary build runs the functional cases, the TSan build runs the same batch of cases, and a non-zero exit code stops the line. Only once the tool is in the pipeline has it really taken hold.

## What This Article Packs Away

- Three criteria: the same memory location, at least one write, and no happens-before separating them—all three and it is a data race.
- The harsh sentence at the end of the definition: any data race is undefined behavior, and a right answer still doesn't count.
- TSan's three questions: which two accesses, which two threads, and what synchronization is missing in between.
- The tool's limits: a report means a real race, no report does not mean no race, and concurrency tests must be broad.

## Next Steps

The volume's first tool is in the bag. One small thing to say up front as well: the rest of this volume's code assumes you know this article's two builds—the ordinary build and the TSan build. Once a tool is in your hands we don't teach it separately anymore; we just use it.

And with that, Chapter 0 wraps up. Next we deal with threads head-on; [Chapter 1's std::thread Basics](../ch01-thread-lifecycle-raii/01-std-thread.md) covers the finer points of construction, argument passing, and wrapping up, article by article. As for what a thread is on the operating system's side and how expensive it is, the measuring method is recorded in [OS Threads and Their Cost at the head of Chapter 4](../ch04-concurrent-data-structures/00-os-threads-and-cost.md). For readers who want to get their hands dirty right away, Lab 00 in [the exercise system](../exercises/) is the toolchain and the first race, aimed squarely at this article's target.

> 💡 We put the complete example code in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); you can visit `code/volumn_codes/vol5/ch00-concurrency-fundamentals/`.

## Reference Resources

- [intro.races clause — C++ standard draft (eel.is)](https://eel.is/c++draft/intro.races)
- [Multi-threaded executions and data races — cppreference](https://en.cppreference.com/w/cpp/language/multithread)
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [std::atomic::fetch_add — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/fetch_add)
- [Why undefined semantics for C++ data races? — Hans Boehm](https://www.hboehm.info/c++mm/why_undef.html)
- [ThreadSanitizerCppManual — google/sanitizers wiki](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual)
- [ThreadSanitizerAlgorithm — google/sanitizers wiki](https://github.com/google/sanitizers/wiki/ThreadSanitizerAlgorithm)
- [Instrumentation Options — GCC manual](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)
- [Coffman, Elphick, Shoshani, System Deadlocks, ACM Computing Surveys 3(2), 1971](https://doi.org/10.1145/356586.356588)
- [Williams, C++ Concurrency in Action, 2nd ed, Manning, 2019](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
