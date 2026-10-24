---
title: "How Arguments Get Into Registers: The Integer Allocation Table"
chapter: 3
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "From two arguments to six, a hands-on table of where integer arguments land on Linux x86-64, from rdi through r9, separating the assignment the ABI fixes from the compiler's own writing habits, then verifying all six registers one by one in gdb"
reading_time_minutes: 7
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/03-arguments-in-registers/01-integer-args-register-allocation.md
  source_hash: e5379de60630b7b3427c01900f2becf7e9f16d04953efd022897e93ea055bbb2
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# How Arguments Get Into Registers: The Integer Allocation Table

When the [previous piece](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md) opened with call and return addresses, gdb set us down at the top of add: the breakpoint landed on the source line `return a + b;`, rdi was holding 3, rsi was holding 4, the arguments sat tight in registers, and the stack showed no sign of them yet. We said back then that arguments travel in registers to begin with, and that putting them on the stack is the callee's own business. This piece finishes the first half of that sentence: what puts 3 into rdi? What puts 4 into rsi? And where does everything go when a seventh argument shows up, or a double, or a struct?

The answers to these questions are not in the CPU's instruction manuals. The x86-64 manuals only catalog a pile of register names; nowhere do they say where the first argument is supposed to live. Take the same chip, and programs running on Linux pass arguments with one set of registers while programs running on Windows use another, and the chip accepts both without a word of complaint. The real source is a document called an ABI. ABI stands for Application Binary Interface, and in plain words it is the handoff agreement that the caller and the callee both obey: which register each argument goes into, where the callee picks it up from, which route the return value takes. The two sides settle all of this in advance, and the machine code the compiler generates follows it. Every function call you write has this agreement running underneath it.

Every example and experiment in this piece is original to this site. Every block of assembly and every debugger session you see here is verbatim from a real run on my machine.

## Our Test Environment

Following this site's convention, the environment comes first. Every block of assembly and every gdb session below comes from this same setup:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

Two defaults in this GCC setup affect what we observe, and if your own runs don't match what's in this piece, one of them is most likely why. One is PIE (position-independent executables, whose load base the system picks at launch); it makes gdb reject attempts to set a breakpoint at a fixed file address with Cannot access memory, and we deal with it properly when we reach the part on stack arguments. The other is the stack protector, on by default: functions with locals like arrays carry extra canary instructions (a sentinel value the stack protector plants), the `%fs:40` ones that piece 1 covered. They have nothing to do with passing arguments, so don't be alarmed if your own runs show them; in the compile commands of a few sections below we add `-fno-stack-protector` to keep them out of the way.

A few more conditions hold from start to finish. Every function under test wears `__attribute__((noinline))`, to block inlining and make sure we see a real call. Every program also has a volatile sink variable (in a test program, sink specifically means a variable that catches the result), and we always write the return value into it, so the whole call doesn't get deleted as dead code. The assembly command is uniformly `g++ -O0 -S` plus `-fno-asynchronous-unwind-tables` to quiet the output, though `.cfi_*` directives still show up in C++ output. When pasting listings I manually delete those along with formatting lines like `.globl`, `.size`, `.file`, and `.ident`, and a few of the longer examples also drop the `.bss` definition of sink. Not a single instruction was touched, and I won't repeat this below.

## Two Arguments, Two Registers

Let's start from the smallest example: the same add that opened the previous topic, with the arguments changed to 11 and 22:

```cpp
// c1_two_int.cpp
__attribute__((noinline)) int add(int a, int b) { return a + b; }

volatile int sink = 0;

int main() {
    int r = add(11, 22);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S c1_two_int.cpp -o c1_two_int.s
```

Here is the compiled assembly. This piece stays with the AT&T syntax the previous piece used (gcc's default), where the source operand sits on the left and the destination on the right; Intel syntax, which you saw in piece 1 of the series, runs in exactly the opposite direction, so be careful not to read the direction backwards when you compare the two side by side:

```asm
_Z3addii:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	movl	-4(%rbp), %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$22, %esi
	movl	$11, %edi
	call	_Z3addii
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	movl	$0, %eax
	leave
	ret
```

Before its `call`, main's whole job is loading the arguments: `movl $11, %edi` writes the first argument, a=11, into edi, the lower 32 bits of rdi. `movl $22, %esi` writes the second argument, b=22, which lands in the lower 32 bits of rsi. An int is 32 bits wide, so the loading uses the 32-bit aliases of the 64-bit registers. Once we jump into add, its first two mov instructions pour the two registers into its own stack frame: `movl %edi, -4(%rbp)` stores a, and `movl %esi, -8(%rbp)` stores b. Writing a register value into a stack slot like this is called a spill in engineer-speak (the value spills out onto the stack); when you meet the word in other material, this is the move it refers to. The summing that follows reads both numbers back out of the two stack slots, and `addl %edx, %eax` leaves the result in eax, so the moment ret hands control back, main's `movl %eax, -4(%rbp)` collects the return value.

Let's pause here. Look closely at how the two loading mov instructions in main are ordered, and you'll spot something odd: the earlier one, `movl $22, %esi`, loads the second argument, while the later one, `movl $11, %edi`, loads the first. That is not a typesetting mistake. At -O0 GCC's habit is to fill the arguments in reverse, starting from the last one, so with two arguments it puts esi before edi; the six-argument example in the next section makes the habit even more visible. Which instruction a listing writes first and which it writes second has nothing to do with the order of the arguments; what is tied to argument order is the register assignment, and that assignment is the only thing we can go by when reading assembly like this. The ABI fixes only which argument goes into which register; the order in which the compiler writes these mov instructions is its own affair, and the ABI has no say in it. If you read the written order as the argument order, then after you switch to a different compiler or optimization level, every correspondence you read out of the listing will be misaligned.

One more word on the spill step: it is -O0's debugging-friendly behavior, not something the specification demands. The compiler's habits and the ABI's agreement govern separate things, and we will meet that boundary again in the next piece on stack arguments. The two-argument handoff ends here: the values set out from main's edi and esi, and the return trip comes back through the same eax. Two slots clearly won't stretch far. How many integer register slots are there in all? The next section counts them for you.

## Six Arguments: The Integer Register Allocation Table

Let's take the arguments up to six and see how much the integer registers have to give:

```cpp
// c2_six_int.cpp
__attribute__((noinline)) int sum6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

volatile int sink = 0;

int main() {
    int r = sum6(1, 2, 3, 4, 5, 6);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S c2_six_int.cpp -o c2_six_int.s
```

Here are the callee's instructions and main's. The long stretch in the middle of sum6, where each value is read back and added in over and over, is omitted; you read it exactly like the summation section for three in the previous topic:

```asm
_Z4sum6iiiiii:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	movl	%edx, -12(%rbp)
	movl	%ecx, -16(%rbp)
	movl	%r8d, -20(%rbp)
	movl	%r9d, -24(%rbp)
	... (six rounds of reading back from the stack slots and adding, omitted)
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$6, %r9d
	movl	$5, %r8d
	movl	$4, %ecx
	movl	$3, %edx
	movl	$2, %esi
	movl	$1, %edi
	call	_Z4sum6iiiiii
	movl	%eax, -4(%rbp)
	... (store to sink, omitted)
	leave
	ret
```

The assignment is now plain to see, so let's lay it out as a table:

| Argument | Value passed | 64-bit register | 32-bit spelling |
|---|---|---|---|
| 1st, a | 1 | rdi | edi |
| 2nd, b | 2 | rsi | esi |
| 3rd, c | 3 | rdx | edx |
| 4th, d | 4 | rcx | ecx |
| 5th, e | 5 | r8 | r8d |
| 6th, f | 6 | r9 | r9d |

One naming detail deserves a second look: r8 and r9 are registers that only arrived with x86-64, carrying none of the historical baggage behind edi, so their 32-bit spellings are simply r8d and r9d, where the trailing d stands for double-word (a double word, 32 bits). Now look at main's loading order: this time it runs r9d, r8d, ecx, edx, esi, edi, filling backwards from the 6th argument all the way to the 1st, and the code-generation habit from the last section shows up in full here. But set the listing against the table, and the assignment follows the very same rule as the two-argument round: the earlier an argument sits in the list, the earlier the register it takes.

Assembly alone doesn't quite settle it for us, so let's bring in gdb to check the registers one by one. The `break _Z4sum6iiiiii` in the command sets a source-level breakpoint; gdb automatically steps over the prologue at the top of the function (prologue, the stack-frame instructions the previous topic covered) and stops the program on the first instruction of the function body, while the arguments in the registers are still untouched:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break _Z4sum6iiiiii' -ex 'run' -ex 'info registers rdi rsi rdx rcx r8 r9' -ex 'p (int)$rdi' -ex 'p (int)$rsi' -ex 'p (int)$rdx' -ex 'p (int)$rcx' -ex 'p (int)$r8' -ex 'p (int)$r9' ./c2_six_int
Breakpoint 1 at 0x1131: file c2_six_int.cpp, line 3.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, sum6 (a=1, b=2, c=3, d=4, e=5, f=6) at c2_six_int.cpp:3
3	    return a + b + c + d + e + f;
rdi            0x1                 1
rsi            0x2                 2
rdx            0x3                 3
rcx            0x4                 4
r8             0x5                 5
r9             0x6                 6
$1 = 1
$2 = 2
$3 = 3
$4 = 4
$5 = 5
$6 = 6
```

The six registers report 1 through 6, matching the arguments one for one, and on the side gdb's own debug information spells out `sum6 (a=1, b=2, ...)`, so we are holding two routes of evidence that agree. Counting up to the 6th argument, the integer register slots run out exactly. Beyond gdb's evidence there is a third route: the one you walk yourself. The executor below loads the scene of add(11, 22) and sum6(1..6). Single-step to just before `call sum6` and look up at the register panel: edi, esi, edx, ecx, r8d, r9d hold 1 through 6, and the loading order runs backwards, the 6th argument entering r9d first. Symbol names lose their mangling, sum6 is not _Z4sum6iiiiii, and the instructions themselves correspond to the listing above.

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/reg_args_7.json" />

So what happens when the 7th argument arrives? The answer is in the next piece.
