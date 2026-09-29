---
title: "OS Threads and Their Cost"
chapter: 4
order: 0
description: "See the kernel thread behind std::thread for what it is, pin down an order of magnitude for each of the three costs of creation, switching, and memory, and let perf stat make a cameo appearance"
tags:
  - host
  - cpp-modern
  - intermediate
  - 进阶
difficulty: intermediate
platform: host
cpp_standard: [11]
reading_time_minutes: 14
prerequisites:
  - "Why Concurrency: A Blocked Main Loop"
  - "Data Races and ThreadSanitizer, Lesson One"
  - "std::thread Basics"
related:
  - "Thread Pool Design"
  - "Lock-Free and Performance Measurement"
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/00-os-threads-and-cost.md
  source_hash: ac8ba05cb8355103b61379466b3c8f950e558e2b23a239bcf2e6d35a6c793709
  translated_at: '2026-09-30T00:00:00+00:00'
  engine: anthropic
  token_count: 8200
---

# OS Threads and Their Cost

Chapter 4's main business is lock-free data structures and performance measurement, and before we lay hands on those, we first step one layer down toward the machine and settle a question that has been hanging since Chapter 0: when you write `std::thread t(func)`, what happens on the operating system's side, and what does it cost. The cost of an empty thread has to be counted in microseconds—how large that number is and how to measure it is exactly what this article is about.

Three things need saying up front: overhead figures **have no universal constant**—the machine, the kernel version, and the load at that moment each add their own hand to the mix—so we are not going to memorize tables of numbers, and what this article hands you is a method for measuring on your own machine; the way we talk about numbers follows the convention on the volume's front page, and where a placeholder has not been filled in, we treat that sentence as unsaid; and as for right and wrong, that is the yardstick of [the TSan article](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)—what this article deals with is cheap versus expensive.

The route is three stops plus one tool: **creation** (one system call, plus a reserved stack), **switching** (registers move house, caches go cold), and **memory** (little resident, much reserved), and finally we invite perf stat to print the overhead out for us.

## Behind std::thread Sits a Kernel Thread

Start from the operating system's point of view. As far as it is concerned, processes and threads handle two separate piles of business: **the process is the container of resources**—the address space, open files, and signal handlers are all the process's family property; **the thread is the unit of CPU scheduling**. Multiple threads inside one process share that property, but each has to keep its own private belongings—its own stack, its own registers, its own program counter. Where the line between shared and private is drawn is exactly where the later synchronization story grows out of, and that is the proper subject of Chapter 2.

So how does `std::thread` get hooked up to a kernel thread? On Linux the call chain is a stack of thin wrappers, one inside the next: the `std::thread` constructor calls `pthread_create()`, and underneath `pthread_create()` is the `clone()` system call; each layer's job is preparing arguments for the layer below.

> Let's set up a glossary card: **system call**. A user program cannot touch the hardware, nor can it create threads; to get any of that done it has to ask the kernel, and the doorway for submitting such a request is the system call. The act of submitting itself has to switch the CPU's privilege level, and that round trip costs nanoseconds to microseconds.

You can understand `clone()` as a finely controlled version of `fork()`: a new process made by `fork()` shares nothing, whereas `clone()` lets the caller pick and choose what to share—what pthread wants is exactly the group "shared address space, shared open file descriptor table, shared signal handler table". As for how user-space threads map onto kernel threads, the textbooks lay out three models; just get familiar with their faces:

::: details The Three Mappings Between User Threads and Kernel Threads

| Model | Mapping                                      | Pros and cons                                                                 |
| ----- | -------------------------------------------- | ----------------------------------------------------------------------------- |
| 1:1   | One user thread to one kernel thread         | Simple and direct, but creation and switching both have to enter the kernel   |
| N:1   | A pile of user threads on one kernel thread  | Cheap to start threads, but one block paralyzes everyone and multi-core goes unused |
| M:N   | Many user threads spread over a few kernel threads | Lightweight and still able to use multiple cores, but the user-space scheduler is the most complex to implement |

Linux's pthread and `std::thread` use 1:1; the other two are choices made by other runtimes—Go's goroutines are the modern flagship of M:N, and we will meet the same idea again in Chapter 6's coroutines: running a large number of suspendable tasks on a small handful of threads. N:1 is not just idle theory either: Java's early green threads were an instance of it, until the official implementation withdrew them in favor of a route built on kernel threads. The lesson is plain—the creation cost a user-space scheduler saves is not worth the risk of "one block paralyzes everyone".

:::

On the map as it stands, you only need to remember one thing: **on mainstream platforms C++'s `std::thread` is 1:1; every one we open really adds one in the kernel, and there is no user-space bargain to be had.** That also means every birth and death of a thread has to trouble the kernel; user space does not get to decide.

## Creation: One System Call, One Reserved Stack

Opening a thread takes a fair amount of joint work by the library and the kernel. The first item is entering the kernel: `clone()` is a system call, and the round trip between user mode and kernel mode costs something by itself. Once inside, the kernel has to allocate a thread record of its own (on Linux it is called `task_struct`, and it holds a snapshot of the registers, the stack pointer, scheduling information, and the signal mask, weighing in at anywhere from a few hundred bytes to a few KB), do the initialization, and hook the new thread into the scheduling queue—none of that work can be skipped.

The second item is the stack. By default `pthread_create` does not pick a size for you; it goes and reads the calling thread's `RLIMIT_STACK`, which is the number you see from `ulimit -s`. **Most distributions set it to 8 MB**, so what you measure will most likely be that number too—but it is a distribution's setting, not a constant hard-coded into the kernel, and plenty of older material explains this too shallowly.

Do not let the 8 MB scare you either: that is a **reservation** in virtual address space, not physical memory taken all at once. The stack is paged in on demand like any other anonymous memory—only for the pages the thread actually touches does the kernel actually hand over memory, and the part never touched is just a hole in the address space. So opening a new thread adds far less than 8 MB of physical memory.

> We will plant a glossary card here: **page fault**. A thread touches a page that has not landed yet, the CPU traps into the kernel, the kernel fills in the physical memory and lets it through—that round trip is what we mean by a page fault. A new thread's first few function calls and first few layers of local variables will step on pages like these.
>
> Sidebar: at the far end of the stack stands a guard page, and if a thread crosses the line the program simply crashes in front of you. A version that crashes on a stack overflow is the lucky one; quietly writing past the boundary and corrupting someone else's data is what is truly frightening.

So what happens when we open a lot of threads? `pthread_create` does not always succeed; on failure it returns an error code, and the most common one is called `EAGAIN`, meaning the resources are temporarily insufficient. There are three places that can run short, and when your program cannot get threads started, this is the table to check:

::: details Three Gates: Where EAGAIN Comes From

| Gate           | What it governs                          | Where to look                  |
| -------------- | ---------------------------------------- | ------------------------------ |
| `RLIMIT_NPROC` | Total processes and threads for one user | `ulimit -u`                    |
| `threads-max`  | Total threads for the whole system       | `/proc/sys/kernel/threads-max` |
| `pid_max`      | The total number of PIDs                 | `/proc/sys/kernel/pid_max`     |

```bash
ulimit -s                          # the stack limit; most distributions set it to 8 MB
ulimit -u                          # this user's process and thread cap (RLIMIT_NPROC)
cat /proc/sys/kernel/threads-max   # the whole system's thread cap
cat /proc/sys/kernel/pid_max       # the total number of PIDs
```

A few commands cost less than a minute, and you have felt out your own machine's gates. What the exact numbers are does not matter; what matters is that you now know where the gates are. If you really want to change a thread's stack size, `pthread_attr_setstacksize` is the proper doorway—here we just leave the name.

:::

### Run a Stopwatch Once

Just saying expensive or not is boring; run the stopwatch once and it is settled. The ready-made benchmark in the companion code repository is there for exactly this: start a thousand empty threads that "do one addition and nothing else", join them one by one to wrap up, and divide the total time by a thousand; then do the same work by calling it directly a thousand times as a control.

```bash
# Build it and run; the numbers drift with the machine, so just look at the order of magnitude
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread 00_os_threads_and_cost.cpp
./a.out 1000
```

```cpp
// Excerpted from 00_os_threads_and_cost.cpp in the code repository
const auto start = std::chrono::steady_clock::now();
long long sink = 0;
for (int i = 0; i < 1000; ++i) {
    std::thread t([i, &sink] { sink += noop_task(i); });  // empty task: one addition
    t.join();  // wrap up as soon as it is done; we are measuring the whole round trip of "create plus wrap up"
}
const auto stop = std::chrono::steady_clock::now();
// Control group: the same loop with no threads, accumulating directly; divide both versions by 1000
```

How far apart the two orders of magnitude are, one run tells you: **a direct call is on the nanosecond scale, starting a thread is on the microsecond scale**, and between them lies a genuine order-of-magnitude gap. There is also a check on `sink` held in reserve: if the totals computed by the two versions disagree it reports an error, which guards against the compiler optimizing the work away.

> Sidebar: for timing we chose `steady_clock`, because it is unaffected by jumps in the system clock. If the wall clock jumps when it resyncs in the middle of the night, the data is ruined.

Measured by the author: WSL2 Arch Linux, kernel 6.18, g++ 16.2.1, AMD Ryzen 7 9700X, with the exact `g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread` command above, `./a.out 1000` run three times in a row, all within the same order of magnitude:

```text
count = 1000 (the numbers drift with machine and load; look at the order of magnitude)
threads : 88.5883 ms total, 88.5883 us per thread
calls   : 0.00022 ms total, 0.22 ns per call
sink check matches: 500500
```

Across the three runs the per-thread figure landed between 75.8 and 88.6 us, and calls held steady at 0.21 to 0.22 ns. The ratio is roughly four hundred thousand to one—three orders of magnitude separate nanoseconds from microseconds, and that is the measured version of the sentence the body wants you to take away. The sink line reads 500500 in both versions, so the compiler did not optimize the work away.

> Sidebar: this number actually has the join wait, the scheduler's arrangements, and the loop's own overhead mixed in—it is not measuring "pure creation". Measurement is never pure; if you can explain clearly what got mixed in, the number becomes useful.

Put this order of magnitude back into the context of task granularity, and the guideline laid down at the volume's start lands right here: **the computation a task does should be significantly more expensive than the overhead of creating and scheduling it.** One addition is nanosecond-scale work, yet it comes paired with a microsecond-scale startup cost; that is exactly why spawning one thread per array element is absurd—the ratio between the two has to flip before it pays off.

## Switching: Registers Move, Caches Go Cold

Once creation is done, the cost that really happens over and over is switching. A core runs only one thread at any given moment (we will leave the fine print of hyper-threading aside for now), and the kernel decides whose turn it is to go on next.

> We will plant a glossary card here: **context**. It means the full set of CPU state at a given moment: the general-purpose registers, the program counter, the stack pointer, and key registers such as the page-table base. Switching swaps exactly this.

The act of changing the cast is called a context switch: save the current thread's register state back into its record, then restore the next thread's register state and jump to where it last paused to carry on. There are plenty of reasons a switch gets triggered; let's pick three common ones: the time slice runs out, a thread blocks on I/O, or a more urgent thread arrives.

> We will plant a glossary card here: **time slice**. The CPU time the scheduler hands each thread is issued in small portions, one portion is called a time slice, and when it runs out the kernel considers swapping in someone else. Its length is set dynamically by the scheduler according to load; there is no hard-coded number of milliseconds.

For us programmers, a switch can cut in at any point between two lines of code, and you cannot predict it. One of the roots of "concurrency bugs are hard to reproduce" from [the TSan article in Chapter 0](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md) is right here: the interleaving window is opened at the scheduler's whim.

The **direct cost** of a switch is the visible part: registers must be saved one by one and then restored one by one. x86-64 has sixteen general-purpose registers, so this part is minor. You might think that even adding the floating-point and SIMD state it still is not much—and we agree it is not much; **the expensive part is not the moving**.

The big expensive part is the **indirect cost**. Cut the camera over to the new thread's side: the moment a switch lands, it faces a world that belonged to someone else just a moment ago. You can think of the TLB as a cache of the page table, and most of the mappings stored in it belong to the previous thread, so they have to be replaced one by one; the new thread's own data is most likely not in the current core's cache either—it is cold. How caches are layered and how coherence is maintained across cores is the proper subject of [Chapter 3](../ch03-atomic-memory-model/); this article records just one thing: **one switch wipes out a good chunk of the warmth you had built up.**

Switches also come in same-core and cross-core flavors, and the cross-core one is more expensive still: on top of the registers, it throws in another helping of cold overhead. In perf stat's output there is a line called `cpu-migrations`, and what it counts is exactly the number of times threads move house between cores.

> From external references we take only the order of magnitude: a public measurement puts a direct switch between two threads in the same process at roughly 1.2 to 1.5 microseconds (with the threads pinned to fixed cores), and about 2.2 microseconds without pinning. The numbers come from Eli Bendersky's 2018 measurements on a Haswell i7-4771; the method is transparent and the environment is a single one, so they bear no direct relation to the machine in front of you—we use them only to pin down an order of magnitude. That is also how the volume's discipline is set: other people's numbers go in the reference box, your own numbers you measure yourself, and the local re-measurement goes in Exercise 3.

One more thing to think through in advance: what perf stat counts is the **number** of switches, not the time each one takes, and you cannot multiply the count into a time. The per-switch time has to be measured with a dedicated design, and the ping-pong method of Exercise 3 is the smallest version of one.

Measured by the author: WSL2 Arch Linux, kernel 6.18, g++ 16.2.1, AMD Ryzen 7 9700X. The minimal ping-pong rig is two threads guarding one `std::atomic<int>`: whichever side sees its own number bats the ball back, flipping back and forth for a million rounds (the waiting side spins with `yield()`), and a sink accumulates the catch count to defeat optimization:

```text
rounds = 1000000, total = 0.41948 s, per-round = 419.48 ns (per-handoff = 209.74 ns)
sink = 1000000 (check: should equal rounds)
```

Across three runs the per-round figure sat between 411 and 419 ns, which works out to roughly 205 to 210 ns per handoff. That is a full order of magnitude below Bendersky's 1.2 to 2.2 microseconds, and we owe you the reason rather than pretending they match: Bendersky measured the complete switch of two **blocking** processes passing a pipe back and forth, where both sleeping and waking go through the kernel; our version spins, so the ball mostly gets caught in user space, and what it measures more closely is the latency of a cache line shuttling between two threads, not a full sleep-plus-wake. It corroborates the body's point from the opposite side—what is expensive is never the register-moving moment. If you want a number closer to the reference, swap the spin for a blocking wait on a condition variable and run it again; the figure will move toward the microsecond scale.

## Memory Footprint: How Much Space One Thread Takes

The memory side gets its own accounting. What is in one thread's resident footprint? `task_struct` takes a few hundred bytes to a few KB; the stack is paged in on demand and only the pages that landed count; and the kernel keeps a kernel stack of its own, also on the KB scale. Add up the odds and ends and the resident part is not frightening.

> Sidebar: the kernel stack and the user stack are two different things. The kernel stack is the stack a thread uses while it is trapped in the kernel getting things done; it travels with the thread and is not something our code touches directly. Just remember that a thread also has a fixed, KB-scale bit of property on the kernel side.

But **the reservation in the address space is real**. At the common 8 MB, a thousand threads means 8 GB of reservation, and ten thousand means on the order of 80 GB. A 64-bit machine's address space can hold that, but the gates will most likely stop you halfway; and even if the gates let you through, the switching overhead will have eaten the gains long before.

If you want to see it with your own eyes there is a way, and the proc filesystem lays the answer out for you:

```bash
# Substitute your own program's pid; the Threads line counts threads, VmRSS counts resident memory
grep -E 'Threads|VmRSS' /proc/<pid>/status
```

Start ten threads and take another look, and the Threads line changes with them: ten new threads plus the main thread comes out to 11, which is the evidence we can observe directly that "threads are real objects". VmRSS's rise and fall is tied to the stack pages landing; the more threads you start and the more pages they touch, the bigger it grows. This observation is easy to do on the fly and needs no special privileges.

Weighing a thread budget follows the same order: **the first thing to check is the gates, the second is the reservation, and only last comes the resident part.** Once all three are cleared does it become our turn to talk about whether the thread count is reasonable.

## More Threads Is Not Better

Who decides when a switch happens? The answer: modern operating systems generally use **preemptive scheduling**; the timer interrupt periodically cuts into the CPU, and the kernel uses that moment to pick the next thread to go on, whether the current thread likes it or not.

> We will plant a glossary card here: **preemption**. The kernel can pull a running thread off the field mid-way without waiting for it to finish the work in hand; the kernel sets the moment of the swap. It is the exact opposite of cooperative scheduling, where the code has to wait for a thread to hand over the CPU itself. The mainstream systems on desktops and servers are all preemptive, so our programs can be paused or resumed at any time, and the timing is not up to us.

The scheduler itself has been changing generations for years: Linux's default scheduler moved from CFS to EEVDF (as of the 6.6 kernel), so for the batch of old material that uses the old name, just convert it in your head as you read. We will not open up the ins and outs of schedulers; the one fact to carry away from this article is this: **who gets the CPU, for how long, and when they are swapped out are all decided by the kernel—and the answer can change at any time.**

Why is more threads not better? Now we can lay it out: there are only so many cores, and when the thread count far exceeds the core count, switches become frequent, part of the CPU's time goes into moving house, and the time actually spent working gets squeezed out instead. The extreme example was signposted back in Chapter 0: an echo service with one thread per connection, where ten thousand connections means ten thousand threads; switching and stacks alone are enough to give you a headache, and the correct answer is the event-driven or coroutine route—that is [Chapter 6](../ch06-async-io-coroutine/)'s territory.

So the common engineering practice runs the other way: **keep a small fixed handful of threads and send the work over in a queue**—that is [the thread pool of Chapter 5](../ch05-future-task-threadpool/); or simply use coroutines and move the pressure of numbers into user space. How to pick the actual thread count is Chapter 5's subject; this article only sets the direction: **better a few threads that stay resident than a big batch switching back and forth.**

## perf stat: Print the Overhead Out and Look at It

We have talked about overhead for quite a while, and everything so far has been qualitative. To turn it into numbers you can look at, perf is the proper tool on Linux, and this article introduces you to its smallest piece: `perf stat`. Lay out its usage and you will get the idea:

```bash
# Point it at the benchmark from the companion repository (an ordinary build is fine; perf does not share a binary with the sanitizer build)
perf stat ./os_threads_and_cost
```

When it finishes, it prints a small table to your terminal, and we will pick the four most useful lines from the default output to get acquainted with:

| Output line      | What it counts                                              |
| ---------------- | ----------------------------------------------------------- |
| task-clock       | How much CPU time this run consumed                         |
| context-switches | How many context switches happened during it                |
| cpu-migrations   | How many times threads moved house between cores            |
| page-faults      | The number of page faults; the stack paging in on demand is counted here |

The default output also has lines like `instructions` and `cycles`, which are closer to the taste of the articles later in this chapter and which this article will leave alone for now; once you are familiar with the four lines, the others will come naturally later.

The way to read them is **by comparison**. Run the two versions of the program and lay the four lines of numbers side by side to watch them rise and fall: the switch count up several times over, page faults up by however much—and the story of overhead turns from a feeling into a record. The four lines happen to catch the two sections above: the on-demand paging from the creation section maps to `page-faults`, the moving house from the switching section maps to `context-switches`, and cross-core moves are recorded in `cpu-migrations`. If you find a single run's numbers unstable, `perf stat -r 5` runs the benchmark five times in a row and hands you both the mean and the variation; on a machine where the numbers jump wildly, your judgment has to be modest along with them.

We cannot give you real numbers for this section's output: perf is not installed on the author's WSL2 machine (`command -v perf` comes back empty), and by the volume's discipline, fabricating a table is worse than having none. There are two ways to install it: on Arch, `sudo pacman -S perf`; on Ubuntu, `sudo apt install linux-tools-common linux-tools-$(uname -r)`. Once installed, run `perf stat ./os_threads_and_cost` as the body says, and the four lines in the table above are your own machine's numbers; check their orders of magnitude against the number of threads you started, and the feel of this section will be trained in.

One honest word also has to be said out loud: perf is not preinstalled everywhere, it is often missing inside WSL2, and even a properly installed environment may find some events invisible because of the `perf_event_paranoid` restriction. Where the tool cannot reach, our volume's alternative route is chrono benchmarks plus sanitizers: stopwatch duty goes to chrono, and the job of watching correctness goes to TSan and ASan.

> Sidebar: do not mix it up with a sanitizer binary—that is a fixed practice in this volume. The TSan build inserts bookkeeping of its own into the code, so pointing perf at it mostly counts the tool's own activity. If you want to run perf, use an ordinary build's binary.

Along the way, get to know two more tools: `perf top` shows the current hotspots interactively, and `top -H` shows how much CPU each thread is eating; neither is expanded on here. The main classroom for sampling (`perf record` and `report`) is in the lock-free articles later in this chapter, and the on-site re-measurement of false sharing (each thread writing its own data, yet crammed onto the same cache line) is placed on the same line as well.

Before leaving, register one more name: **futex**, the core of mutex implementation on Linux—when uncontended everything is finished in user space, and only under real contention does it enter the kernel to sleep and wait. The lock we have mentioned all along is still a black box; its in-depth treatment is in [Chapter 2's synchronization primitives toolkit](../ch02-mutex-condition-sync/05-sync-primitives-toolkit.md).

## What This Article Puts in Our Bag

- `std::thread` is the 1:1 model on mainstream platforms, one user thread to one kernel scheduling entity, and creation goes through the `clone()` system call.
- Creation is microsecond-scale: one system call, one kernel record, and one stack reserved according to `RLIMIT_STACK` (8 MB on most distributions), paged in on demand.
- Switching is microsecond-scale too; moving registers is the minor part, and **the TLB and caches going cold are the major part**.
- There are three gates on thread count: `RLIMIT_NPROC`, `threads-max`, and `pid_max`; hit one and `pthread_create` returns `EAGAIN`.
- perf stat's four lines: `task-clock`, `context-switches`, `cpu-migrations`, `page-faults`; compare them and overhead turns from a feeling into a record.
- futex is on the record as well: uncontended locking and unlocking are finished entirely in user space, and only entering the kernel is the slow path; the authoritative source is Chapter 2's toolkit.

We will also gather the three costs into a quick-reference table, with the orders of magnitude given in qualitative terms:

| Cost                            | Order of magnitude                    | Where to look                   |
| ------------------------------- | ------------------------------------- | ------------------------------- |
| Creation (system call plus stack) | Microseconds                        | The chrono benchmark, Exercise 1 |
| Switching                       | Microseconds                          | perf stat's context-switches    |
| Resident memory                 | KB scale, with the reservation on the MB scale | The status page in /proc |

## Exercises

### Exercise 1: Run a Stopwatch on Creation Cost

Build and run `00_os_threads_and_cost.cpp` from the companion repository, write down the per-call time and the ratio for both versions, and fill the numbers back into this article's placeholders. Then switch the count to 100 and to 10000 and run each once to see whether the order of magnitude holds steady. Once that is done, think one step further: in the thread version, besides creation itself, what else got mixed into this number? The sidebar in the stopwatch section named them—identify them.

On a machine that has perf, add one more step: run the same benchmark under perf stat and check the `context-switches` number against the number of threads you started, to see whether they are the same order of magnitude. This step trains the feel for making counters line up with code.

How you report the numbers matters too: record both versions in full—keeping only a ratio is not enough. With the raw numbers kept, you have something to compare against when you rerun on another machine later.

### Exercise 2: Read the pthread_create Manual Once

Open the `pthread_create(3)` manual page on man7.org (typing `man pthread_create` locally works just as well) and answer two questions: which limit does the default stack size come from? Which conditions for returning `EAGAIN` are listed? Then check this machine's `ulimit -s`, `threads-max`, and `pid_max` against the clauses in the manual before you call it done. What this trains is the feel for consulting primary sources, which is far more reliable than memorizing blog posts.

When checking your answers, pay attention to the wording of the ERRORS section: the manual lists **possible** causes, not a verdict. Which gate you actually hit still has to be identified from the numbers on your machine.

### Exercise 3: Measure Switching with Ping-Pong

Two threads and one `std::atomic<bool>` are enough to set up a rig for measuring switching: thread A sets the flag to true and waits for it to turn back to false before moving on, thread B does exactly the opposite half, and the two go back and forth for a million rounds. Divide the total time by the number of rounds and the cost of one round falls out; then compare it against the numbers in this article's reference box for order of magnitude.

When you are done, ask one more question: which overheads that do not belong to switching itself got mixed into this number? The atomic reads and writes, the loop's comparisons—all of them are in there. If you can explain clearly what got mixed in, the number becomes useful. Please write your measured numbers down next to the reference box: on the same machine, what the ping-pong per-round time and the creation time each come out to—the ratio between them is interesting in its own right; record one more round after changing machines, and whether the ratio holds steady is more worth watching than any single number.

## Next Steps

With the overhead accounted for, Chapter 4's main subject is now properly underway: the articles that follow take this yardstick to lock-free data structures—how to design thread-safe queues, where the cost of lock-free stacks and queues lies and whether it is worth paying—and the on-site re-measurement of false sharing sits on the same line. Readers who want to shore up the basics first will find [Chapter 1's std::thread Basics](../ch01-thread-lifecycle-raii/01-std-thread.md) and [Chapter 2](../ch02-mutex-condition-sync/) behind you; the futex registered in this article gets its in-depth treatment in Chapter 2's synchronization primitives toolkit.

If you want to get hands-on right away, the timing- and perf-related exercises in [the exercise system](../exercises/) pick up exactly where this article's exercises leave off; if you get lost, go back to the [volume front page](../) and take a look at where you are.

> 💡 We put the complete example code in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); you can access `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## Reference Resources

- [pthread_create(3) — Linux man-pages (man7)](https://man7.org/linux/man-pages/man3/pthread_create.3.html) — where the default stack and the EAGAIN clauses come from
- [pthread_attr_setstacksize(3) — Linux man-pages (man7)](https://man7.org/linux/man-pages/man3/pthread_attr_setstacksize.3.html) — the proper doorway for changing stack size
- [clone(2) — Linux man-pages (man7)](https://man7.org/linux/man-pages/man2/clone.2.html) — the CLONE_* flags and the origin of threads
- [The Native POSIX Thread Library for Linux — U. Drepper, I. Molnar](https://www.akkadia.org/drepper/nptl-design.pdf) — the NPTL design document, and where the 1:1 model comes from
- [An EEVDF CPU scheduler for Linux — LWN, 2023-03](https://lwn.net/Articles/925371/) — the full story of the scheduler's generational change
- [proc(5) — Linux man-pages (man7)](https://man7.org/linux/man-pages/man5/proc.5.html) — field descriptions for Threads and VmRSS in /proc
- [perf Examples — Brendan Gregg](https://www.brendangregg.com/perf.html) — the authoritative source for perf stat
- [Measuring context switching and memory overheads for Linux threads — E. Bendersky, 2018](https://eli.thegreenplace.net/2018/measuring-context-switching-and-memory-overheads-for-linux-threads/) — where the numbers in the reference box come from
- [C++ Concurrency in Action, 2nd ed — A. Williams, Manning, 2019](https://www.manning.com/books/c-plus-concurrency-in-action-second-edition) — Chapter 1 has a qualitative discussion of thread overhead
