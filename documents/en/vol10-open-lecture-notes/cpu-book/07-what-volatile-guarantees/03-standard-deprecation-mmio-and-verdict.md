---
title: "volatile's Standard Boundary: The Deprecation List, Device Registers, and a Decision Rule"
chapter: 7
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
description: "Checks the original cppreference text and P1152R4's deprecation list sentence by sentence against the earlier experiments, then moves to MMIO device registers to see the use volatile is legitimately meant for, and closes on the decision rule of asking who else changes the value"
reading_time_minutes: 16
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/07-what-volatile-guarantees/03-standard-deprecation-mmio-and-verdict.md
  source_hash: 1c6c13026350b2b28929c693273a0a50cbb8ddbe2d2492dea4c706f212228a97
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# volatile's Standard Boundary: The Deprecation List, Device Registers, and a Decision Rule

The previous piece closed on the lock prefix: volatile's addl and atomic's lock addl, the instruction bodies identical, the dividing line falling inside the machine code. This piece finishes what's left. We bring out the original text of cppreference and the proposal and set them against the experiments, see which spellings C++20 took back, then head over to device registers to watch volatile do the job it was actually meant for, and finally fold the three pieces into one decision rule. We use the same environment as the [first piece](01-volatile-bans-in-one-thread.md); follow it if you want to rerun anything.

## What the Standard's Own Text Says

The experiments are done, so it's time to bring the standard's own text out and check it against them. The [cv qualifiers page on cppreference](https://en.cppreference.com/w/cpp/language/cv) (retrieved October 12, 2026) has a paragraph in the page's main text that writes down volatile's semantics and its boundary in one pass:

```text
Every access (read or write operation, member function call, etc.) made
through a glvalue expression of volatile-qualified type is treated as a
visible side-effect for the purposes of optimization (that is, within a
single thread of execution, volatile accesses cannot be optimized out or
reordered with another visible side effect that is sequenced-before or
sequenced-after the volatile access.
```

Rendered into plain language: every access made through an expression of volatile-qualified type (a read, a write, a member function call, all of them count) is treated as a visible side effect. The scope for all of this is inside a single thread of execution: a volatile access cannot be optimized out, and it may not be reordered against another visible side effect sequenced before or after it. This whole topic has been making good on that one sentence. The loads that refused to merge in the first piece's loop experiment, the accesses that refused to disappear in the dead read and dead write experiment, and the order that held in the ordering experiment are all that sentence at work in front of you. Notice the qualifying phrase in the sentence as well, `within a single thread of execution`: that is the single-thread boundary spelled out. The scope of the ban is written plainly, and on any guarantee between threads the sentence doesn't spend one word. The suitability sentence right after is blunter still:

```text
This makes volatile objects suitable for communication with a signal
handler, but not with another thread of execution, see std::memory_order).
```

Here is our translation: volatile objects are fit for communicating with a signal handler, and the thing they are not fit for is another thread of execution. A signal handler is a function the operating system calls asynchronously when a signal arrives, and it runs concurrently with the main flow of the program; that is a legal use the standard itself signs off on. For another thread of execution the standard says outright that volatile doesn't fit, and the race experiment in the previous piece is the second half of that sentence playing out live. The same page carries one more caution about direction, and this one is stated as a prohibition:

```text
Any attempt to access a volatile object through a glvalue of non-volatile
type (e.g. through a reference or pointer to non-volatile type) results in
undefined behavior.
```

Reach for a reference or pointer of non-volatile type to touch a volatile object, and what you get back is undefined behavior. volatile goes by the type of the expression, not by whether the object is genuinely in danger. Cast a volatile object's address into an ordinary pointer and use it, and the ban stops working entirely, and the ban stopping working is itself UB.

## The Boundary C++20 Took Back

The standard's other move on volatile was pulling things back. C++20 accepted a proposal called P1152R4 whose title says it outright, Deprecating volatile; retrieved October 12, 2026, and we have checked the original text too: [the proposal itself](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1152r4.html). Two sentences from its abstract, verbatim:

```text
We propose deprecating most of `volatile`.
The proposed deprecation preserves the useful parts of `volatile`, and
removes the dubious / already broken ones.
```

Our straight translation: the proposal recommends deprecating most uses of volatile; the deprecation keeps the useful parts of volatile and drops the dubious, already broken parts. Deprecated means the language sticks a label on a spelling to talk you out of it, the compiler's warnings arrive along with the label, and only the step after that is true removal. The motivation reads even more directly:

```text
This paper aims at breaking at compile-time code which is today subtly
broken at runtime or through a compiler update.
```

The aim is code that today breaks quietly at runtime, or breaks with one compiler update, and gets its error reported at compile time instead. As for exactly what got deprecated, the Notes on the same cppreference page supply a list, which we carry over word for word:

```text
Some uses of volatile are deprecated:
- lvalue of volatile type as operand of built-in increment/decrement operators;
- lvalue of volatile type as left operand of built-in direct assignment,
  unless the direct assignment expression appears in an unevaluated context
  or is a discarded-value expression;
- volatile object type as function parameter type or return type;
- volatile qualifier in structured binding declaration.
```

Matching it item by item: the list names `++` and `--` on volatile, direct assignment with its exemption, plus volatile function parameter and return types, and volatile in a structured binding declaration is in there too. Compound assignment isn't listed separately among cppreference's four entries; it comes from proposal P1152R4's own list of changes, where `E1 op= E2` with E1 volatile gets the deprecated mark as well. The second entry deserves a close look, because direct assignment lands in the deprecated zone too, but it carries one exemption: it goes unpunished when the assignment expression appears in an unevaluated context, or when it is a discarded-value expression. A discarded-value expression, said plainly, is an expression whose value nobody picks up; the statement sits there and that's the end of it. `++counter` in v4_race.cpp fits neither exemption, which is why GCC 16 warns by default, the two -Wvolatile lines we pasted in the previous piece. The split-into-three-statements version writes `counter = v`, and the version with the barrier writes `counter = v + 1`; both are statement-level assignments and nobody takes their value, so checked against the list, neither falls inside the deprecated range. Back in the first topic, when we wrote NoisyIter, we deliberately used a read, a write, and another write to get around `side_count++`, dodging exactly this deprecation; this time you've seen the warning in its true form.

Deprecated does not mean deleted. Compile these spellings today and they still pass, except the compiler sets its doubts down in front of you. The direction is clear too: the proposal says its deprecation keeps the useful parts, which points straight at the device-access kind of use we're about to visit, while on the thread-communication side the standard said in its own words that volatile doesn't fit.

## Where volatile Belongs: Memory That Changes on Its Own

Now the place where volatile stands on solid ground: memory-mapped device registers. The jargon term is MMIO, short for memory-mapped I/O, and the move is to map a device's registers into memory addresses, so that a program reading and writing those addresses is reading and writing the device. Picture a polling loop: in the device's status register, a ready flag gets set by the device itself, and our program reads it lap after lap, waiting for it to change. In this code the thing changing the value has neither another thread nor any line of our own program behind it; it is hardware the compiler cannot see at all. Let's stage the scene with two functions that differ only in pointer type:

```cpp
// v6_mmio.cpp -- device status register polling: volatile pointer vs plain pointer, assembly side by side
// compile only, never run; both functions have external linkage, their bodies must be kept

int poll_volatile(volatile int* status, int n) {
    int s = 0;
    for (int i = 0; i != n; ++i)
        s += *status;   // volatile read: one load per iteration is the expectation
    return s;
}

int poll_plain(int* status, int n) {
    int s = 0;
    for (int i = 0; i != n; ++i)
        s += *status;   // plain read: the load is expected to be hoisted/merged
    return s;
}
```

```bash
g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables v6_mmio.cpp -o v6_mmio.s
```

```asm
_Z13poll_volatilePVii:
.LFB0:
	.cfi_startproc
	testl	%esi, %esi
	je	.L4
	xorl	%eax, %eax
	xorl	%edx, %edx
	.p2align 4
	.p2align 4
	.p2align 3
.L3:
	addl	$1, %eax
	addl	(%rdi), %edx
	cmpl	%eax, %esi
	jne	.L3
	movl	%edx, %eax
	ret
	.p2align 4,,10
	.p2align 3
.L4:
	xorl	%edx, %edx
	movl	%edx, %eax
	ret
	.cfi_endproc

_Z10poll_plainPii:
.LFB1:
	.cfi_startproc
	testl	%esi, %esi
	je	.L9
	movl	(%rdi), %eax
	imull	%esi, %eax
	ret
	.p2align 4,,10
	.p2align 3
.L9:
	xorl	%eax, %eax
	ret
	.cfi_endproc
```

We already know the reasoning behind the plain-pointer version from the first piece's v1_loop_plain experiment: nothing inside the loop can possibly write this memory, so the repeated reads are all equivalent, and the loop gets swapped for one `movl (%rdi), %eax` plus one `imull %esi, %eax`. Read once, multiply by n, done. The `testl %esi, %esi` and `je` at the top are the early return for n equal to zero, and the loop condition lives in the `cmpl` at the bottom, comparing the counter against n. If a device were really sitting behind `(%rdi)`, that one lone load would miss every later change of the device's state, and we would watch the poll run all the way to the end on a single stale value. The volatile-pointer version spends one `addl (%rdi), %edx` per iteration: every lap's load goes back through the pointer, so once the device changes the value, the next lap reads the new one. The pointer arrives as a runtime argument, and the only thing the assembly can see is `(%rdi)`; at compile time nobody knows where it points, but the volatile on the type brings the ban along with the pointer.

When the value never changes, do the two versions differ at runtime? Let's add a runnable check to find out: mmap one page of legal memory to stand in for the register, and set the value to 7:

```cpp
// v6_mmio_run.cpp -- mmap one page of legal memory to stand in for the "device register", the result consumed through printf
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>

int poll_volatile(volatile int* status, int n) {
    int s = 0;
    for (int i = 0; i != n; ++i)
        s += *status;
    return s;
}

int poll_plain(int* status, int n) {
    int s = 0;
    for (int i = 0; i != n; ++i)
        s += *status;
    return s;
}

int main() {
    long pgsz = sysconf(_SC_PAGESIZE);
    void* page = mmap(nullptr, pgsz, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }

    auto status = static_cast<volatile int*>(page);
    *status = 7;   // the device sets the status register to 7

    int sv = poll_volatile(status, 10);
    int sp = poll_plain(const_cast<int*>(static_cast<volatile int*>(page)), 10);
    std::printf("poll_volatile = %d, poll_plain = %d (7*10 = 70)\n", sv, sp);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 v6_mmio_run.cpp -o v6_mmio_run && ./v6_mmio_run
```

```text
poll_volatile = 70, poll_plain = 70 (7*10 = 70)
```

Both versions come out at 70. In a single thread the value never changed, so merging the loads and repeating the loads compute the same total, and that settles a side question along the way: the volatile version is not going to read out some different value. The string of `static_cast` and `const_cast` at the call does nothing but convert types between the volatile pointer and the plain one, so each function receives its own parameter type while both look at the same page of memory. The behavioral difference only shows when the value genuinely changes, and what it looks like we have already seen clearly in the assembly.

This is the daily bread of embedded development. Open a CMSIS device header and you'll find the register struct's members uniformly declared `__IO uint32_t`; the `__IO` macro expands to volatile, and the GPIO configuration, status, and data registers are each and every one of them memory that changes on its own. The embedded track's F103 line has a [piece on what MMIO really is](../../../vol8-domains/embedded/f103/01-led/02-mmio-essence.md) that expands `GPIOC->CRH |= x` all the way down to the read-modify-write of `*(volatile uint32_t*)0x40011004`, goes through the ldr, orr, and str lines of the disassembly one by one, and verifies everything with Renode simulation plus examples that run on the host, where you can watch a wait loop get deleted outright under -O2. On the embedded track volatile carries a precondition of correctness, and performance has nothing to do with it: where it's missing, the register reads get merged, and the program can no longer see the device's state changing.

## The Decision Rule: Who Else Changes the Value

Let's fold the three pieces into a single decision rule. Two questions to ask: first, besides this thread's own code, who else changes this object's value? The second: on how many threads does the code reading and writing it run?

- The writer is the device, or an asynchronous signal handler, and all the reading and writing stays in one thread: use volatile. The compiler cannot see those writers, so the keyword tells it for you that the value can change. Write the reads and writes plainly, and keep `++` and compound assignment off volatile; C++20 deprecated them, and GCC will warn.
- The writer is another thread: switch to `std::atomic`. Atomicity is delivered in hardware by the lock prefix, visibility and ordering are declared through the memory order parameter, and volatile promised none of these; you have already seen what that dividing line looks like in the previous piece's machine code.
- This thread is the only writer, and nothing asynchronous steps in: add nothing, and let the compiler do its work. Merging and eliminating ordinary variables is exactly the optimization you want; the volatile int sink in 01 is an observation tool, not a spelling to copy.

To close, let's set the most valuable piece of evidence from the three pieces on the table one more time. The same scene, two threads incrementing a hundred thousand times each; here is the increment instruction from the thread body of the volatile version and of the atomic version, one from each:

```asm
addl   $0x1,0x2cdd(%rip)        # 4064 <counter>
lock addl $0x1,0x2c5c(%rip)        # 4064 <counter>
```

One from the volatile version, one from the atomic version, and the instruction body is identical (counter lands at the same address on both sides, both assembly comments say 4064, and the 129 gap between the offsets is exactly the difference between the two instructions' rip baselines, since rip-relative addressing takes its own next instruction's position as the baseline). The one word that differs is lock. volatile talks to the compiler, and everything it says lands at compile time, with nothing to be seen in the machine code; the lock prefix talks to the CPU, and what it says is written into the instruction itself. Next time you see someone teaching volatile as thread synchronization, set these two lines of assembly down in front of them.
