---
title: "The Three Things volatile Bans in a Single Thread: Merging, Elimination, and Reordering"
chapter: 7
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
description: "Starts from a loop where ten reads get merged into a single load, then walks four groups of small functions down to the assembly one by one, covering reads nobody consumes, stores overwritten on the spot, and read-write pairs that trade places, to count out the merging, elimination, and reordering that volatile bans in a single thread"
reading_time_minutes: 9
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/07-what-volatile-guarantees/01-volatile-bans-in-one-thread.md
  source_hash: 1b3bf8c7561c124ce97afe47ff15667d44095b6681f25cb9171e2233308548be
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# The Three Things volatile Bans in a Single Thread: Merging, Elimination, and Reordering

> Here we use `volatile` as our anti-optimization anchor. It demands that every read and write of sink genuinely happen, and that their order stay as written.

That line is a quote from [piece 01's first group of experiments](../01-prefix-vs-postfix-increment/01-int-instructions.md). We were in a hurry back then to compare the instructions behind i++ and ++i, so volatile did one tour of duty as an anti-optimization anchor, and that was that. But by what right does a single keyword get to order the compiler around, no merging, no deleting, no moving? I did not say a word about it at the time. This piece is where we make up for that.

Let me hand you the verdict up front. volatile is a set of prohibitions written for the compiler. Its audience is the compiler, and about the CPU it says not one word. In the assembly of the previous six pieces you will find no trace of volatile (the compiler never turns it into an instruction). What it changes is precisely the instructions that ought to have appeared and didn't. And its most widely circulated use, two threads sharing one volatile variable as a communication channel, falls exactly in the territory it never promised to cover. What it governs and what it doesn't, this whole topic pins down with experiments.

Every example and experiment in this piece was written from scratch by us. Every block of assembly and every line of program output you see comes verbatim from a real run on my machine.

The series' main storyline wrapped up in the previous piece, where a pointer value was traced from a virtual address all the way to its physical spot on the memory chips. This piece steps off that storyline. The subject is no longer which instructions a certain kind of code lands on but which optimizations a single keyword holds back. The method is the one we have been using all along: lay out the source, paste the assembly, run the program. The difference is that half the evidence in this topic consists of instructions that vanished, and the other half of race scenes a running program produces.

## Our Test Environment

Convention as usual, the environment goes first:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

Every experiment in this piece runs in WSL2, on a machine with 16 logical cores, and the experiment later in this topic where two threads fight over one variable runs on exactly that machine. For pulling assembly we settle on `g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables` throughout, and the output syntax is AT&T. When pasting, we follow the convention set in piece 03 and drop the trailing housekeeping lines like `.ident` and `.section`. The instruction portion is untouched, and I won't repeat this further down. `.cfi_startproc` and `.cfi_endproc` at the head and tail of each function stay in, the way piece 05 kept them: they are stack-unwinding metadata fed to the debugger and generate no machine code. Programs with threads get compiled the release way, `g++ -O2 -std=c++20 -pthread`, and for disassembly we use `objdump -d --no-show-raw-insn`, which leaves out the machine-code bytes and reads much cleaner. The v1, v4 and so on in the source file names are just the numbers I gave the experiments while working. Call the files anything you like when you reproduce them.

## Ten Reads Merged into One Load

Let's start from the plainest scenario there is: a function whose loop reads the same global variable over and over, and -O2 merges the repeated loads away. A load is exactly what the word says in the assembly, reading a value from memory into a register. The source we want to look at is only a few lines:

```cpp
// v1_loop_plain.cpp: plain global, read 10 times in a loop and summed. Watch whether -O2 merges the repeated loads
int g;  // plain global, never written within this translation unit

int sum10() {
    int s = 0;
    for (int i = 0; i != 10; ++i)
        s += g;
    return s;
}
```

```bash
g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables v1_loop_plain.cpp -o v1_loop_plain.s
```

```asm
_Z5sum10v:
.LFB0:
	.cfi_startproc
	movl	g(%rip), %eax
	leal	(%rax,%rax,4), %eax
	addl	%eax, %eax
	ret
	.cfi_endproc
```

The 10 `s += g` in the source stand for 10 loads. The assembly has only the one `movl g(%rip), %eax` at the top. The two instructions after it we can take one at a time: `leal (%rax,%rax,4)` computes 5 times g, which is 4 times rax plus one times itself, and `addl %eax, %eax` doubles the value again, so together they compute g * 10 outright. The loop is gone entirely. Let me read the compiler's reasoning out loud for it: nothing inside the loop body could possibly write g (no calls, no stores), so all 10 reads return the same value, merging them into one costs nothing, and one multiplication then stands in for the whole loop. Trading a cheap operation for an expensive one is called strength reduction, one more compile-time craft beyond the constant folding we met in piece 01.

Now we change one word at the declaration, plain int to volatile int, and leave every other character of the function body alone:

```cpp
// v2_loop_volatile.cpp: same code as v1, variable now volatile. Contrast: a fresh load on every iteration
volatile int g;

int sum10() {
    int s = 0;
    for (int i = 0; i != 10; ++i)
        s += g;
    return s;
}
```

```asm
_Z5sum10v:
.LFB0:
	.cfi_startproc
	movl	$10, %eax
	xorl	%edx, %edx
	.p2align 4
	.p2align 4
	.p2align 3
.L2:
	addl	g(%rip), %edx
	subl	$1, %eax
	jne	.L2
	movl	%edx, %eax
	ret
	.cfi_endproc
```

The loop is back. The opening two instructions are setup: `movl $10, %eax` loads 10 into eax to serve as a countdown, and `xorl %edx, %edx` zeroes the accumulator edx. The loop from `.L2` down to the `jne` runs 10 rounds, and each round's body is a single `addl g(%rip), %edx`. Every one of those addl instructions does the same job, going back to memory to load g's value afresh. Ten iterations pile up ten loads, and not one of them can be spared. The source differs by a single word at the declaration, yet the assembly differs by whether the loop exists at all. That is the first thing volatile bans: repeated accesses may not be merged. Who needs a guarantee like this? Some objects get rewritten by things outside our code (devices and signal handlers both belong to that crowd), and the compiler cannot see those writers, so it treats them by default as if they did not exist. volatile is the sentence we supply on the compiler's behalf: for this object, don't assume its value stays unchanged. The last piece of this topic takes that scenario to where it genuinely earns its keep. For now, let's keep counting through the list of bans.

## Reads Nobody Consumes, Stores Overwritten Right Away

Besides merging, the compiler has two more direct moves: one deletes a read whose value nobody consumes, the other deletes a store that gets overwritten immediately. Four small functions put both moves to the test:

```cpp
// v3_dead.cpp: dead read elimination and dead store elimination, plain variables vs volatile
int g;             // plain global
volatile int vg;   // volatile global

void dead_read_plain() {
    int x = g;   // nobody consumes the value we read
    (void)x;
}

void dead_read_volatile() {
    int x = vg;
    (void)x;
}

void dead_store_plain() {
    g = 1;   // overwritten immediately after, so this store is dead
    g = 2;
}

void dead_store_volatile() {
    vg = 1;
    vg = 2;
}
```

```bash
g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables v3_dead.cpp -o v3_dead.s
```

```asm
_Z15dead_read_plainv:
.LFB0:
	.cfi_startproc
	ret
	.cfi_endproc

_Z18dead_read_volatilev:
.LFB1:
	.cfi_startproc
	movl	vg(%rip), %eax
	ret
	.cfi_endproc

_Z16dead_store_plainv:
.LFB2:
	.cfi_startproc
	movl	$2, g(%rip)
	ret
	.cfi_endproc

_Z19dead_store_volatilev:
.LFB3:
	.cfi_startproc
	movl	$1, vg(%rip)
	movl	$2, vg(%rip)
	ret
	.cfi_endproc
```

Let's read them one function at a time. All that is left of `dead_read_plain` is a bare `ret`. The load was deleted without a trace: read a plain variable and never use it, and it is as if the read never happened. In `dead_read_volatile`, the `movl vg(%rip), %eax` survives. Even with nobody consuming the value, this read has to happen. `dead_store_plain` keeps only one of its two stores, `movl $2`. The store of 1 is gone (it was about to be overwritten by 2 anyway). On the `dead_store_volatile` side, both the store of 1 and the store of 2 are present, and dropping either one is not allowed. The second thing volatile bans gets its evidence here: the accesses themselves may not be eliminated. The principle is the same one behind merging. The compiler treats a volatile access as a visible side effect, one entry on the program's list of observable behaviors, and deleting it changes the behavior. When piece 01 used a volatile sink over and over to keep its comparisons meaningful, this was exactly what it was leaning on: the count and the order of the two writes to sink are both on the observable list, and the optimizer has no way around them.

## The Order Must Not Move: A Swap You Can See with Your Own Eyes

The last ban is the subtlest, and it governs order. Between the reads and writes of plain variables, as long as there is no data dependency between the two ends, the compiler may swap their positions. When the accesses are volatile, no two of them may trade places. Order rearrangement is a thin thing to assert in words alone, and you would be forgiven for not feeling it, so here is a group of experiments where the swap is visible in the assembly:

```cpp
// v3_reorder.cpp: moving reads and stores around, plain variables vs volatile
// Case 1: a read of a and a store to b with no dependency between them, read first in source order. Watch whether the compiler swaps them
// Case 2: storing to the same variable on every loop iteration. Watch whether the stores get merged or carried out of the loop
int a, b;
volatile int va, vb;

int ld_st_plain() {
    int v = a;   // read a
    b = 42;      // store to b, no dependency on the read of a
    return v;
}

int ld_st_volatile() {
    int v = va;
    vb = 42;
    return v;
}

void store_loop_plain(int n) {
    for (int i = 0; i != n; ++i)
        a = i;   // n stores, only the last one is visible
}

void store_loop_volatile(int n) {
    for (int i = 0; i != n; ++i)
        va = i;
}
```

```bash
g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables v3_reorder.cpp -o v3_reorder.s
```

```asm
_Z11ld_st_plainv:
.LFB0:
	.cfi_startproc
	movl	$42, b(%rip)
	movl	a(%rip), %eax
	ret
	.cfi_endproc

_Z14ld_st_volatilev:
.LFB1:
	.cfi_startproc
	movl	va(%rip), %eax
	movl	$42, vb(%rip)
	ret
	.cfi_endproc

_Z16store_loop_plaini:
.LFB2:
	.cfi_startproc
	testl	%edi, %edi
	je	.L4
	subl	$1, %edi
	movl	%edi, a(%rip)
.L4:
	ret
	.cfi_endproc

_Z19store_loop_volatilei:
.LFB3:
	.cfi_startproc
	testl	%edi, %edi
	je	.L9
	xorl	%eax, %eax
	testb	$1, %dil
	je	.L11
	movl	$0, va(%rip)
	movl	$1, %eax
	cmpl	$1, %edi
	je	.L21
	.p2align 5
	.p2align 4
	.p2align 3
.L11:
	movl	%eax, va(%rip)
	leal	1(%rax), %edx
	addl	$2, %eax
	movl	%edx, va(%rip)
	cmpl	%eax, %edi
	jne	.L11
.L9:
	ret
.L21:
	ret
	.cfi_endproc
```

The first pair is the startling one. In `ld_st_plain` the source order is read a first, store b second, yet in the assembly `movl $42, b(%rip)` sits ahead of the read of a. The two instructions really did trade places. The justification: within one translation unit (our .cpp together with the headers it includes), GCC can see all of the code, it can prove that a and b do not interfere with each other, and the swap therefore changes nothing observable. In `ld_st_volatile`, the source order of read va first and store vb second is preserved as written. There is the third item: volatile accesses may not be reordered with respect to each other.

The second pair has even more to look at. The loop in `store_loop_plain(n)` writes a n times, and the compiler works out that only the last write is visible, so the entire loop becomes the single statement `if (n > 0) a = n - 1;`. The shape of `testl` and `je` in the assembly is exactly that if. The n writes collapsed into one write outside the loop, carried clear across the loop boundary. On the `store_loop_volatile(n)` side not a single write is missing, but the shape deserves a close look. GCC unrolled the loop by 2, so each round's body lands two `movl` instructions. An even n goes through the main loop, and an odd n gets one extra `movl $0` right after entering the function. The unrolling genuinely changed the loop's structure, yet the total number of writes is still n, in the original `i=0,1,2,…` order. Count them yourself: not one more, not one fewer. This is where we state the boundary precisely: what volatile freezes is the count and the order of the accesses, not every scheduling decision the compiler makes. As long as the observable sequence of accesses stays the same, unrolling, register reuse, and scheduling moves like them are all legal.

That completes the list of bans: merging, elimination, reordering. All three are things volatile forbids on the compiler's behalf. You have also seen the scope of application, because every experiment so far stayed inside one function, in one thread, from start to finish. Does it still hold once you step outside that scope? The next piece puts the question to a real race.
