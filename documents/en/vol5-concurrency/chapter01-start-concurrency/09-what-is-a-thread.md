---
title: "What a Thread Is on the Machine"
description: "Descend from std::thread to kernel threads: the 1:1 model and the real cost of threads, how Linux's pthread, clone, and futex cooperate, a comparison of three threading models, and how the scheduler orders threads and for how long they run."
chapter: 5
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20, 23]
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/09-what-is-a-thread.md
  source_hash: 4d4494c103e3093b8ae02d5516d8c85f874d4cd39f42ce9bda7692c7dd6f22a2
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 13252
---
# What a Thread Is on the Machine

> This article was written by this author for friends who want a first look at what actually sits behind the `std::thread` black box!

We have written `std::thread` and drawn the diagram of threads taking turns on the CPU. What does that one line of code actually do on Linux? This optional read walks you down the chase, and at the bottom you will see one slightly counterintuitive thing: inside the kernel there is, in fact, no special species called a "thread". At the system-call level we observed with strace (a tool that traces every system call a process makes), and claims about Linux behavior were verified against the corresponding manual pages.

## From `std::thread` to the Kernel Thread: The 1:1 Model

Start the chase from the smallest piece of code:

<OnlineCompilerDemo
  title="The smallest std::thread program"
  source-path="code/examples/vol5/chapter01/05_minimal_thread.cpp"
  description="Construction is launch: once the line declaring the std::thread finishes executing, one more flow of execution exists in the system; join waits for it to end."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 最小的 std::thread 程序:构造即启动,join 等待
#include <print>
#include <thread>

void work() {
    std::println("working");
}

int main() {
    std::thread t(work); // 这一行执行完，系统里多了一条执行流
    t.join();
}
```

</OnlineCompilerDemo>

Construction is launch. Once the `std::thread` constructor (since C++11) takes the work function, it does not register and queue — it creates a new flow of execution on the spot, and the new thread starts running `work` immediately. No arguing from thin air: we traced `std::thread t(work);` with strace to see what it triggered (output abridged; addresses vary by machine; long lines wrapped):

```text
clone3({flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|
        CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID,
        exit_signal=0, stack=0x7ddac21ff000, stack_size=0x7fff40, ...})
```

What the constructor triggered is one clone3 system call — the modern interface to clone, and Linux's primitive for creating flows of execution. Every item in the flags declares what the new flow shares with the old one: `CLONE_VM` shares the virtual address space, `CLONE_FILES` shares the open-file descriptor table; we will pick out the important few of these flags in the next section. In other words, the moment you write `std::thread t(work);`, one more independently schedulable flow of execution exists in the kernel.

The operating-system textbook saying goes: the thread is the basic unit of CPU scheduling, the process the basic unit of resource allocation. We verified that sentence with code, having the main thread and the two threads it creates each report which process it belongs to and what its thread ID is:

<OnlineCompilerDemo
  title="Same process, different tids: getpid and gettid"
  source-path="code/examples/vol5/chapter01/06_pid_and_tid.cpp"
  description="The main thread and two worker threads report their process IDs and thread IDs — same process number, distinct tids; the thread is the basic unit of CPU scheduling."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 线程与进程编号:同一个进程里的多条执行流,tid 各不相同
#include <print>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

static long my_tid() {
    return syscall(SYS_gettid);
}

int main() {
    std::println("main    : getpid = {}, tid = {}", getpid(), my_tid());
    std::thread t1([] { std::println("worker-1: getpid = {}, tid = {}", getpid(), my_tid()); });
    std::thread t2([] { std::println("worker-2: getpid = {}, tid = {}", getpid(), my_tid()); });
    t1.join();
    t2.join();
}
```

</OnlineCompilerDemo>

```text
main    : getpid = 46, tid = 46
worker-1: getpid = 46, tid = 47
worker-2: getpid = 46, tid = 48
```

Put the three output lines together. All three flows share the same `getpid`, while the `tid`s differ. `getpid` answers which process a flow belongs to, and the three threads give the same answer — they are members of one process. `tid` is the number the kernel issues to each flow of execution, and since these numbers differ, in the kernel's eyes they are three separately schedulable flows. The shared parts make communication between threads cheap (one address space; reading and writing the same variables needs no cross-process machinery), while the private parts let each advance on its own.

This mapping is called the 1:1 model: every `std::thread` you write corresponds to one real scheduling entity in the kernel, with no layer in between. The direct consequence: every cent of a thread's cost is real, kernel-level cost. Let us split the cost into two items.

The first item is the stack. Every thread needs its own stack — function calls and local variables live there, unshareable with other threads. How big is it? We checked rather than guessed:

<OnlineCompilerDemo
  title="How big each thread's stack is"
  source-path="code/examples/vol5/chapter01/07_thread_stack_size.cpp"
  description="pthread_getattr_np reports the stack sizes of the main and worker threads; this number was looked up, not guessed."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 每条线程的栈有多大:pthread_getattr_np 查实际栈大小
#include <cstddef>
#include <print>
#include <pthread.h>
#include <thread>

static void report(const char* who) {
    pthread_attr_t attr;
    void* addr = nullptr;
    size_t size = 0;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &addr, &size);
    std::println("{} stack size = {} KB", who, size / 1024);
    pthread_attr_destroy(&attr);
}

int main() {
    report("main  ");
    std::thread t([] { report("worker"); });
    t.join();
}
```

</OnlineCompilerDemo>

```text
main   stack size = 8184 KB
worker stack size = 8192 KB
```

The new thread's stack is 8192 KB, that is, 8 MB — exactly the value of `ulimit -s` on our machine. That is because the pthreads manual page states: as long as the stack-size soft limit (`RLIMIT_STACK`) is not unlimited, it determines the default stack size of new threads. So "Linux defaults to an 8 MB thread stack" holds only for the default configuration; run `ulimit -s` against your own machine and you will know.

The 8184 KB reported for main is the process's own stack — a different origin; one glance is enough, no need to mind it. One more detail to state precisely: the 8 MB is reserved virtual address space; physical memory grows with the pages actually used, and a newborn thread has not truly consumed 8 MB of RAM. But the reservation itself is a constraint. What happens with ten thousand threads? Lay out the numbers: stack reservations alone make 80 GB of address space, plus the scheduling information the kernel maintains per thread — the order of magnitude tells you `std::thread` is not something to scatter around.

The second item sits in switching. The kernel's scheduler switches back and forth among the flows of execution so each advances a little. Switching has two parts. The direct part: save the current thread's register state and restore the next thread's. The indirect part is often larger: the new thread's data is probably not in this core's cache, the address-translation cache (the TLB) still holds the previous thread's mappings, and the core taking over must reload the working set into cache. As a rule of thumb, the direct plus indirect cost of one switch runs from a few microseconds to a few dozen — a figure we did not measure in isolation; it is an order of magnitude from experience. What we did measure is a neighboring thing: two threads notifying each other through a condition variable, each falling asleep and being woken, a hundred thousand round trips. To time each round trip we wrote a ping-pong program with a condition variable and a mutex:

<OnlineCompilerDemo
  title="Condition-variable ping-pong: how much one wake round trip costs"
  source-path="code/examples/vol5/chapter01/08_cv_pingpong.cpp"
  description="Two threads notify each other with a mutex plus condition variable, timed over a hundred thousand round trips, measuring the cost of waking and switching."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 条件变量乒乓:两个线程互相通知十万次,量一次唤醒往返的开销
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <print>
#include <thread>

int main() {
    std::mutex m;
    std::condition_variable cv;
    int turn = 0;
    constexpr long kRounds = 100000;
    std::thread worker([&] {
        std::unique_lock<std::mutex> lk(m);
        for (long i = 0; i < kRounds; ++i) {
            cv.wait(lk, [&] { return turn == 1; });
            turn = 0;
            cv.notify_one();
        }
    });
    auto start = std::chrono::steady_clock::now();
    {
        std::unique_lock<std::mutex> lk(m);
        for (long i = 0; i < kRounds; ++i) {
            turn = 1;
            cv.notify_one();
            cv.wait(lk, [&] { return turn == 0; });
        }
    }
    auto end = std::chrono::steady_clock::now();
    worker.join();
    const long us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::println("rounds = {}, total = {} us, per round trip = {} us", kRounds, us,
                 static_cast<double>(us) / kRounds);
}
```

</OnlineCompilerDemo>

```text
rounds = 100000, total = 5634019 us, per round trip = 56.3402 us
rounds = 100000, total = 5641756 us, per round trip = 56.4176 us
rounds = 100000, total = 5764721 us, per round trip = 57.6472 us
```

We ran it three times, all between 56 and 58 microseconds (WSL2 virtualized environment; bare metal would mostly be lower). One round trip packs two sleeps and wakes, two thread switches, and the lock's own overhead, so it gives a somewhat high reading of switching cost. Estimating at tens of microseconds per switch, switching is real money. We once said: cut tasks too fine, and the gains may be eaten by overhead. Where does the money go? Here.

## Linux's Implementation: pthread, clone, and futex

The layer under `std::thread` is, on Linux, pthread. pthread is not something inside the kernel; it is a set of thread interfaces specified by the POSIX standard — `pthread_create`, `pthread_join`, and friends are usable everywhere, each system implementing its own. Linux's modern implementation is NPTL (Native POSIX Thread Library), available since glibc 2.3.2, dependent on features the Linux 2.6 kernel provided, and the default ever since. You can check which one your machine uses with `getconf GNU_LIBPTHREAD_VERSION`; ours answers NPTL 2.44. The manual page summarizes this implementation as 1:1 (each thread maps to one kernel scheduling entity) and states plainly that it is built on the clone system call.

clone is Linux's lowest-level primitive for creating flows of execution. `fork()` creates a brand-new process with parent and child sharing nothing; clone lets the caller specify item by item what to share. Address space, file descriptor table, signal dispositions, filesystem information (current directory, umask, and the like) can each go on the list. One saying is worth passing along: inside the kernel there is really no special species called "thread" — it schedules task after task, and the difference between process and thread is only how much is shared. The longer the sharing list, the more thread-like the new flow; share nothing at all, and it is a traditional process. pthread_create asks for the mostly-shared tier, and the flags string we saw in the last section's strace is the list itself: `CLONE_VM` shares the address space, `CLONE_FILES` shares the file descriptor table, `CLONE_SIGHAND` shares signal dispositions, and `CLONE_THREAD` puts the new flow into the same thread group so the three threads share one process number. The `CLONE_CHILD_CLEARTID` at the end of the list is unremarkable day to day but deserves a single glance: it concerns a thread's ending, and we will connect it back when we get to futex.

Creation chased down; on to synchronization. A mutex has one natural puzzle: when the lock is not being fought over, should lock and unlock make a trip into the kernel? Linux's answer is the futex (fast userspace mutex), the idea in one sentence: no contention, stay in user mode the whole way — a few atomic instructions and done. Real contention, only then enter the kernel and let it suspend the lock-waiting thread, waking it when the holder releases. The futex(7) manual page, translated, says "in the absence of contention, the entire operation proceeds in user space" — the kernel steps on stage only when there is contention.

We tested both paths for real. First trip: the main thread alone locks and unlocks an uncontended mutex a million times, counting system calls with `strace -c` (excerpt of the `strace -c` output):

```text
  8.88    0.000027          27         1           futex
100.00    0.000304           4        65         1 total
```

Sixty-five system calls in total, of which futex appears exactly once — and that one was the runtime's own doing at program startup. A million lock-unlock pairs, zero system calls: the fast path lives up to its name. Second trip, we arranged real contention: one thread holds the lock and sleeps 100 milliseconds, another reaches for the lock 10 milliseconds in. That one reach produced system calls (output abridged; addresses vary by machine):

```text
[pid 66] futex(0x60dc3522b0a0, FUTEX_WAIT_PRIVATE, 2, NULL <unfinished ...>
[pid 67] futex(0x60dc3522b0a0, FUTEX_WAKE_PRIVATE, 1 <unfinished ...>
```

`FUTEX_WAIT` is the lock-waiting thread asking the kernel to suspend itself; `FUTEX_WAKE` is the releasing thread asking the kernel to wake the waiters. The `_PRIVATE` suffix in the names says the lock is used only inside the process, not shared across processes. From here on, reading manuals and source, we will meet `FUTEX_WAIT` and `FUTEX_WAKE` again and again. `std::mutex` on Linux is wrapped from exactly this machinery, so "an uncontended lock is cheap, a fiercely contended lock is expensive" is no metaphor — it is the difference between two code paths: the former makes not a single system call; the latter enters the kernel for every wait and wake.

The second experiment's full output also carried a `FUTEX_WAIT_BITSET` — held back during the abridgment, and now it can be finished. The address it acts on lies outside the lock-waiting address: it is the address near the new thread's stack top that records the thread's number — the very address `CLONE_CHILD_CLEARTID` in the last section's clone list pointed at. The clone(2) manual page states: when the new thread exits, the kernel clears this address and performs a futex wake on it. What `pthread_join` waits for, when waiting for a thread to end, is exactly this event. When we write `t.join()` (since C++11), this kernel-level signal is what we wait for. Creation by clone, ending by futex — both ends of a thread's life on Linux, seen.

## Comparing Thread Models: 1:1, M:N, N:1 (Extended)

This section carries the (Extended) marker: knowing the three mapping relationships, when you later meet thread pools and coroutines you will recognize which class of problem they solve. Skip it, and neither this article's main line nor the rest of Unit 1 is affected.

The previous two sections both looked at 1:1 — one user-side thread mapping to one kernel scheduling entity — which is the model C++ programmers get by default. Pull the view higher, and two other wirings exist between user-side flows of execution and kernel ones.

N:1 is one extreme: many user threads all hanging off a single kernel thread. Creation and switching complete entirely in user mode, never entering the kernel — light, quick, cheap. But the price is structural: the moment any user thread makes a blocking call, the kernel thread underneath is stuck, and every user thread stops together. And with only one flow of execution in the kernel's eyes, extra cores cannot help — true parallelism is out of the question. Some early green-threads implementations were built N:1; knowing it as history is enough.

M:N tries to hold both ends: M user threads mapped onto N kernel threads, M usually far larger than N. The scheduler runs in user mode, assigning user threads onto kernel threads to execute; user threads stay lightweight, while the count of kernel threads is still enough to spread across cores. Go's goroutine is this model's representative — its initial stack is only a few KB, arranged by the Go runtime's scheduler onto a small number of OS threads. The price is the runtime's own complexity: preemption, wrapping system calls, managing two-level stacks — the runtime must handle all of it, which is precisely what the kernel would otherwise be doing for the 1:1 model. In C++ we usually do not touch that complexity unless we build a runtime ourselves.

C++'s position is 1:1. In the standard implementations on mainstream platforms, `std::thread` lands directly on the operating system's native thread interface (in the Linux context we have verified this by experiment — straight down to clone), so the cost analysis of the previous two sections holds for it item by item. When task counts far exceed core counts and each task is light, one thread per task stops being economical, and two ready-made roads exist. Thread pools use a fixed number of worker threads plus a task queue, amortizing the cost of thread creation; a teaching-grade complete thread pool I plan to write into the cookbook's optional section, adding a link in the reference resources once it is done. Coroutines make "pausable work" a language mechanism, covered properly by the coroutine unit later in this volume. Both are, in essence, scheduling policies built atop 1:1 — taking part of the "who runs next" decision back from the kernel, into the hands of a library or of you. When Is It Worth Bringing In Concurrency covered the "what you need is asynchronous I/O" case; coroutines are exactly one road for it.

## Scheduling: Who Runs When, and for How Long

The last stop is the operating system's daily business: thousands of threads on the machine, a dozen-odd cores — how are they ordered, and how long does each run? Writing concurrent programs, we never see this decision being made, yet it determines every timing property the program exhibits. Worth getting acquainted with its temperament.

A modern operating system can preempt a running thread and hand the CPU to another. Exactly when the changeover happens is decided by the scheduling policy. In A First std::thread Program we saw several threads' outputs interleave; looking back now, you hold a more concrete explanation.

You will often see the word time slice; textbooks on preemptive scheduling use it for "that small stretch of time allotted to one thread". Linux's ordinary scheduler has no fixed time slice — let us get its actual practice straight. Threads created in the ordinary way all default to the SCHED_OTHER policy (called SCHED_NORMAL in the kernel source). The scheduler implementing that policy was, since 2.6.23, CFS (the Completely Fair Scheduler): its idea is to record a virtual runtime for each thread and run next whichever thread's virtual runtime has fallen behind, so same-priority threads share the CPU fairly. Since kernel 6.6 it was replaced by EEVDF: the fairness goal unchanged, it additionally orders candidate threads by virtual deadline when picking the next one, letting latency-sensitive tasks request shorter slices. Knowing the two names CFS and EEVDF to this degree is enough — SCHED_OTHER's semantics are stable across both implementations, and the programs we write need not care which one sits underneath.

To order a batch of ordinary threads, what you lean on is the nice value. Range −20 to +19, default 0, lower meaning more favored. The manual has one useful concrete number: in the current implementation, each step of nice multiplies the scheduler's favor toward the higher-priority side by a factor of 1.25 — one step, one factor — and tens of steps accumulate considerably. One more easily mistaken point: the POSIX standard calls nice a process attribute, while Linux implements it per thread, so threads within one process can carry different nice values. To adjust, use the `nice` command or the `setpriority()` system call.

The last piece is affinity (CPU affinity) — the binding relationship between a thread and a core. By default the scheduler may move a thread from core to core: a thread that ran on core A for a while may next run on core B. Moving has a cost — the new core's cache and TLB must be reloaded — exactly the indirect part of the switching cost discussed in the 1:1 model section. If you know a thread's working set is large and cache hits matter, you can pin it to a fixed core with `sched_setaffinity()`. This is a Linux-specific interface managing a per-thread attribute; the C++ standard library has no counterpart, but fortunately `std::thread::native_handle()` (since C++11) fetches the underlying pthread_t, and the POSIX interfaces carry on from there. We tested it, pinning a freshly created thread to a core:

<OnlineCompilerDemo
  title="sched_setaffinity: pinning to a core"
  source-path="code/examples/vol5/chapter01/09_cpu_affinity.cpp"
  description="Fetch the pthread_t via native_handle and pin the thread to a fixed core with the POSIX interface. The online sandbox has only 2 cores, so the sample pins cpu 0; on a local machine, other cores can be pinned."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// sched_setaffinity 绑核:经 native_handle 拿 pthread_t,用 POSIX 接口绑固定核心
// 在线沙箱只有 2 个逻辑核,这里绑 cpu 0;本地机器可改绑其他核心
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <print>
#include <pthread.h>
#include <sched.h>
#include <thread>

int main() {
    std::thread t([] {
        for (int i = 0; i < 3; ++i)
            std::println("worker on cpu {}", sched_getcpu());
    });
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    pthread_t native = t.native_handle();
    int rc = pthread_setaffinity_np(native, sizeof(set), &set);
    std::println("pin worker to cpu 0 via native_handle, rc = {}", rc);
    t.join();
}
```

</OnlineCompilerDemo>

```text
pin worker to cpu 5 via native_handle, rc = 0
worker on cpu 5
worker on cpu 5
worker on cpu 5
```

`rc` being 0 says the pin was set successfully; asked three times which core it is on, the thread answered cpu 5 every time — the handle from `native_handle` really does reach straight through to the POSIX interface. Pinning is a performance tool, not a default action; for most programs, letting the scheduler arrange freely is fine. Two sentences to take away. Threads can be moved between cores, and moving costs. The means to pin exists; the interface is platform-specific, and the standard library does not manage this for you.

## Next Steps

We have seen how the scheduler allots time to threads. Back inside C++ programs, two threads executing in a different order must still produce correct results. The next unit starts from a counter that computes wrong, to see what happens when two threads touch the same data. [Same Counter, Two Threads — Why Does It Compute Wrong?](../chapter02-sharing-data/01-from-wrong-programs)

## Reference Resources

While writing this article I drew on the second edition of Anthony Williams' C++ Concurrency in Action for its chapter progression around threads. The examples and prose are my own. Claims about C++ behavior were compiled and tested or verified against cppreference as noted in the text; claims about Linux behavior were verified against manual pages. Compiler versions, standard options, and dates for the experiments are all stated in the text.

- [std::thread (cppreference)][cppref-thread] — the thread interface since C++11; construction is launch.
- [std::thread::join (cppreference)][cppref-join] — waiting for thread completion and its synchronization guarantee.
- [std::thread::native_handle (cppreference)][cppref-native] — fetching the underlying thread handle (since C++11).
- [clone(2) — Linux manual page][man-clone] — the primitive for creating flows of execution, the meanings of the sharing flags, and how thread libraries use it.
- [futex(7) — Linux manual page][man-futex] — the two paths of fast userspace locking: uncontended stays entirely in user mode; contention enters the kernel.
- [pthreads(7) — Linux manual page][man-pthreads] — Linux's implementation of POSIX threads, NPTL's origins, the list of shared vs. per-thread attributes, and how the default stack size relates to RLIMIT_STACK.
- [sched(7) — Linux manual page][man-sched] — scheduling policies, nice-value semantics and its per-thread nature, and CPU affinity.
- [EEVDF Scheduler — kernel.org documentation][kernel-eevdf] — notes on the fair scheduler's replacement of CFS by EEVDF since kernel 6.6.
- [The Native POSIX Thread Library for Linux — Ulrich Drepper, Ingo Molnar][nptl-design] — NPTL's design document.

[cppref-thread]: https://en.cppreference.com/w/cpp/thread/thread
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[cppref-native]: https://en.cppreference.com/w/cpp/thread/thread/native_handle
[man-clone]: https://man7.org/linux/man-pages/man2/clone.2.html
[man-futex]: https://man7.org/linux/man-pages/man7/futex.7.html
[man-pthreads]: https://man7.org/linux/man-pages/man7/pthreads.7.html
[man-sched]: https://man7.org/linux/man-pages/man7/sched.7.html
[kernel-eevdf]: https://docs.kernel.org/scheduler/sched-eevdf.html
[nptl-design]: https://www.akkadia.org/drepper/nptl-design.pdf
