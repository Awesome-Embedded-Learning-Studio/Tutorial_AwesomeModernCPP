---
title: "Function Return Values: the rax Family and Register Pairs"
chapter: 8
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
description: "Measured live: the two register-sized return routes, with int, bool and pointer data itself traveling through the rax family, sixteen-byte aggregates cut into eight-byte segments for register pairs, integers and floats drawing from their own pools, and a gdb session settling that the odd value in the padding bits is process, not outcome"
reading_time_minutes: 9
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/08-return-value-journey/01-rax-family-and-register-pairs.md
  source_hash: 19459d963a5bf975685608664cf08e4187fc7e60e89d37d1c21d9a0f2b2083d3
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Function Return Values: the rax Family and Register Pairs

```cpp
Big b = make_big(100);
```

You write a line like this every day. When topic 03 covered argument passing, we finished the outbound direction, the one where the call instruction sends arguments out the door, and only opened the return direction: the callee is done computing, so how does the result get back into the caller's hands? Two unfinished threads have been piling up in this direction. Back when [topic 01 compared i++ with ++i](../01-prefix-vs-postfix-increment/02-class-type-cost.md), the textbook said the return path carries two copies, yet in the assembly we saw only one construction; we hung the name NRVO on it and moved on, leaving the mechanism for later. In [the part of topic 03 that covered return values](../03-arguments-in-registers/02-stack-args-and-return-values.md) we verified that a big return value's rax carries back nothing but an address, and 03 gave a plain-language version of the two-eightbyte criterion as well; the original wording of the rule, the register roster on the return side, and one exception that pays no attention to size are what this topic is here to deliver. Both threads get closed over the next few pieces.

How a return value travels back splits into three physical routes by size and category. Whatever fits in one register goes as the data itself, through rax. Aggregates around sixteen bytes are cut into eight-byte segments and handed to two registers, with the integer and floating-point halves drawing from their own pools. Anything bigger gets a hidden pointer (a hidden pointer the caller passes) to do the carrying, and the callee writes the data straight into the caller's territory. On top of the three routes C++ stacks one more mechanism, an elision layer named copy elision, which can make a copy that was supposed to happen vanish entirely; over this topic we will chase it all the way down into the assembly.

Every block of assembly, every gdb session, and every line of output in this piece is verbatim from real runs on my machine, and the examples and experiments are all original to this site. The tools are the usual few: g++ produces the assembly, objdump does the disassembly, and gdb owns the breakpoint-and-read-registers work. If you have been through the previous pieces, nothing about how this one reads will feel new.

## Our Test Environment

Following the series' convention, the environment comes first, so that every block of assembly below is something you can reproduce on a machine with the same setup:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

A few more environment facts that affect how you read the output. If your reruns don't match what's in this piece, check them against the list below:

- We don't pass `-std` explicitly. GCC 16's default dialect is gnu++20, and `echo | g++ -dM -E -x c++ - | grep __cplusplus` measures `__cplusplus` at 202002L. The experiments that touch copy elision semantics get an extra `-std=c++14` run for comparison, because C++17 changed that part of the semantics, and numbers from the old dialect must not be reported mixed together with numbers from the new one.
- Assembly is taken with one fixed command, `g++ -S -O0 -fno-asynchronous-unwind-tables`, and every function under test carries `__attribute__((noinline))`, so the function bodies keep their independent shape at -O0. The packing section brings an -O2 view alongside.
- That switch does nothing for C++ compiles: g++ needs the stack unwind tables to find its way back to a catch when an exception is thrown, so it turns the option right back on; topic 01 measured this. The listings here have therefore had the `.cfi_*` lines and file-level metadata like `.ident` removed by hand; the instruction sequences are untouched, and every place something was cut is noted before its block.
- Return values are always consumed through a volatile sink, so the whole stretch cannot be optimized away. Topic 07 covered in full what volatile bans; here we use it purely as an observation tool.
- In the copy-counter experiments the sink only reads members and triggers no copies, so the means of observation does not disturb the thing observed.
- All assembly is in AT&T syntax, source operand on the left and destination on the right, registers prefixed with `%` and immediates with `$`; you already used this reading style back in 01.

## What Fits in One Register Goes Through the rax Family

The first route starts from the smallest return values. int, bool, and bare pointers all weigh eight bytes or fewer, so a single register holds any of them. Let's measure all three, and along the way see which part of rax each one lands in. Here is the source:

```cpp
// Small returns come back through rax: int, bool, and raw pointer
int get_int(int x) __attribute__((noinline));
bool get_bool(int x) __attribute__((noinline));
int* get_ptr(int* p) __attribute__((noinline));

int get_int(int x)   { return x + 1; }
bool get_bool(int x) { return x > 0; }
int* get_ptr(int* p) { return p + 1; }

int main() {
    volatile int  vi = get_int(41);
    volatile bool vb = get_bool(5);
    int arr[4] = {0, 0, 0, 0};
    volatile int* vp = get_ptr(arr);
    return vi + vb + (vp != nullptr);  // consume the sink so nothing is deleted wholesale
}
```

The compile command is `g++ -O0 -S -fno-asynchronous-unwind-tables r1_small.cpp -o r1_small.s`, dialect left at the default gnu++20. Here are the three function bodies verbatim, with the `.cfi_*` lines omitted:

```asm
_Z7get_inti:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	-4(%rbp), %eax
	addl	$1, %eax
	popq	%rbp
	ret

_Z8get_booli:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	cmpl	$0, -4(%rbp)
	setg	%al
	popq	%rbp
	ret

_Z7get_ptrPi:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	-8(%rbp), %rax
	addq	$4, %rax
	popq	%rbp
	ret
```

Let's take them one at a time. `get_int` spills its argument to the stack and reads it back, and the result computed by `addl $1, %eax` lands in eax; a thirty-two-bit write also zeroes the upper thirty-two bits of rax as it goes, which is hard-wired x86-64 behavior, so the whole of rax holds a defined value. `get_bool` uses `setg %al` to write the comparison result into the single byte al. `get_ptr` runs on the full width of rax, where `addq $4, %rax` is a pointer advancing one int-sized step. What the three small cases share is the return lane: the data all comes back through the rax family, and eax, al, and rax are the same register at different widths. In the ABI document's register usage table, rax is listed as the 1st return register, and the rdx column reads as the second return register, one we will be needing shortly.

Reception on main's side is just as plain. Here are the three receiving spots picked out verbatim, with the stack protector's epilogue omitted:

```asm
	movl	$41, %edi
	call	_Z7get_inti
	movl	%eax, -44(%rbp)
	movl	$5, %edi
	call	_Z8get_booli
	movb	%al, -45(%rbp)
	leaq	-32(%rbp), %rax
	movq	%rax, %rdi
	call	_Z7get_ptrPi
	movq	%rax, -40(%rbp)
```

The receiving widths line up exactly with the sending widths: `movl %eax` takes the int, `movb %al` takes bool's single byte, and the pointer comes back at full width in `movq %rax`. What exit code does the program end with? Let's check the semantics:

```bash
g++ -O0 r1_small.cpp -o r1_small && ./r1_small; echo exit=$?
```

```text
exit=44
```

Now the numbers: 44 breaks down as 42 plus 1 plus 1. vi holds 42, and vb and the non-null pointer test contribute one 1 each, so the semantics are still exactly what we wrote.

## Around Sixteen Bytes: Register Pairs and Two Register Pools

At sixteen bytes a single register stops being enough, yet we have not reached the point where memory gets involved; the convention splits the value across two registers. We'll prepare two sixteen-byte structs, one holding pure integers, one with a floating-point member mixed in, and watch how the registers divide them up.

```cpp
// A struct within 16 bytes comes back split across registers
// Pair8: two longs, 16 bytes. Expect INTEGER+INTEGER -> rax+rdx
// Mixed: int + double, 16 bytes (4 + 4 pad + 8). Classified per the ABI, then measured
struct Pair8 { long a; long b; };
struct Mixed { int a; double b; };

static_assert(sizeof(Pair8) == 16, "Pair8 must be 16 bytes");
static_assert(sizeof(Mixed) == 16, "Mixed must be 16 bytes");

Pair8 make_pair8(long a, long b) __attribute__((noinline));
Mixed make_mixed(int a, double b) __attribute__((noinline));

Pair8 make_pair8(long a, long b) { return {a, b}; }
Mixed make_mixed(int a, double b) { return {a, b}; }

int main() {
    Pair8 p = make_pair8(11, 22);
    Mixed  m = make_mixed(33, 4.5);
    volatile long sink1 = p.a + p.b;          // consume, so nothing gets deleted
    volatile double sink2 = m.a + m.b;
    return sink1 == 33 && sink2 == 37.5 ? 0 : 1;
}
```

### Pair8: Two Integer Segments, Two Registers

The compile command is `g++ -O0 -S -fno-asynchronous-unwind-tables r2_struct.cpp -o r2_struct.s`. Here are both function bodies verbatim, with the `.cfi_*` lines omitted:

```asm
_Z10make_pair8ll:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	-8(%rbp), %rax
	movq	-16(%rbp), %rdx
	popq	%rbp
	ret

_Z10make_mixedid:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movsd	%xmm0, -16(%rbp)
	movl	-4(%rbp), %ecx
	movq	%rax, %rdi
	movabsq	$-4294967296, %rsi
	andq	%rdi, %rsi
	orq	%rsi, %rcx
	movq	%rcx, %rax
	movq	-16(%rbp), %rdx
	movl	%eax, %ecx
	movq	%rdx, %xmm0
	movl	%ecx, %eax
	popq	%rbp
	ret
```

make_pair8's return action is two movs and nothing more: the first member goes into rax, the second into rdx. Main's side likewise spends two movs, landing separately into p's two members. Here is the receiving stretch picked out verbatim, with the stack protector and the tail of the floating-point comparison omitted:

```asm
	movl	$22, %esi
	movl	$11, %edi
	call	_Z10make_pair8ll
	movq	%rax, -32(%rbp)
	movq	%rdx, -24(%rbp)
	movq	.LC0(%rip), %rax
	movq	%rax, %xmm0
	movl	$33, %edi
	call	_Z10make_mixedid
	movl	%eax, %edx
	movq	%xmm0, %rax
	movl	%edx, -16(%rbp)
	movq	%rax, -8(%rbp)
```

Nowhere in there is a sixteen-byte bulk move; Pair8 was cut into two segments, each traveling in its own register. You met the unit of the cut when topic 03 covered argument passing: the ABI's classification unit is the eightbyte, which means exactly eight bytes. Pair8 occupies precisely two of them, both holding integer content, so both are classified INTEGER, and INTEGER's register sequence on the return side is rax, rdx, running parallel to the argument side's rdi, rsi sequence. Two independent rosters. Run `./r2_struct` and the exit code comes out 0; the data arrived intact.

### Mixed: the Integer Half Takes rax, the Float Half Takes xmm0

make_mixed's assembly runs one stretch longer than Pair8's; let's go straight to the backbone. Mixed's first eight bytes hold int a plus four bytes of padding; that chunk lands in class INTEGER and takes rax. The last eight bytes hold double b; that one is class SSE and takes xmm0. Reception in main confirms the division of labor: `movl %eax` takes only the four-byte a, and `movq %xmm0` takes b. Integers travel in general-purpose registers and floats in vector registers, and the two pools on the return side behave exactly like the ones topic 03 showed on the argument side: neither draws on the other's quota.

### A False Alarm in the Padding Bits

The string of splicing instructions in make_mixed's middle section nearly had me announcing a discovery the first time I read it; the ones doing the work are `movabsq`, `andq`, and `orq`. They splice the old value sitting in rax at function entry into the first eightbyte's padding bits. What was that incoming old value? gdb tells us: 0x4012000000000000, exactly the bit pattern main left behind in rax when it loaded 4.5 into xmm0. At first glance it looks like garbage data has mixed into the return value and is about to flow out to the caller. Let's read the registers at both entry and exit with gdb; the compile command is `g++ -O0 -g r2_struct.cpp -o r2_dbg`:

```text
$ gdb -q -batch \
    -ex 'set debuginfod enabled off' \
    -ex 'break make_mixed' \
    -ex 'run' \
    -ex 'printf "ENTRY : incoming rax = 0x%016lx\n", $rax' \
    -ex 'finish' \
    -ex 'printf "RETURN: rax          = 0x%016lx\n", $rax' \
    -ex 'printf "RETURN: xmm0 bits    = 0x%016lx\n", (unsigned long)$xmm0.v2_int64[0]' \
    ./r2_dbg
Breakpoint 1 at 0x113b: file r2_struct.cpp, line 14.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, make_mixed (a=33, b=4.5) at r2_struct.cpp:14
14	Mixed make_mixed(int a, double b) { return {a, b}; }
ENTRY : incoming rax = 0x4012000000000000
0x0000555555555198 in main () at r2_struct.cpp:18
18	    Mixed  m = make_mixed(33, 4.5);
Value returned is $1 = {a = 33, b = 4.5}
RETURN: rax          = 0x0000000000000021
RETURN: xmm0 bits    = 0x4012000000000000
```

The session shows the ending. At the moment of return, rax is 0x0000000000000021, and the `movl %ecx, %eax` just before the exit is a thirty-two-bit write that zeroes rax's upper half; the garbage spliced in mid-function never made it out of the function alive. The 0x4012000000000000 in xmm0 is exactly 4.5's bit pattern, and 0x21 is 33 in decimal. I came close to writing "garbage leak" into my notes before crossing it out: the garbage splicing is a real shape of -O0 code generation, the truncation at the exit is just as real, and we cannot take the process for the outcome. The ABI says this in writing too. Footnote 20 reads:

```text
When a value of a type of class INTEGER is returned or passed in a register or
on the stack, the excess bits that would not be present in the memory
representation of the type (see figure 3.1) are unspecified.
```

In plain terms: when a value of class INTEGER is returned or passed in a register or on the stack, the excess bits beyond the type's in-memory representation are unspecified. The padding bits were never inside the agreement to begin with. Today's GCC at -O0 happens to zero them, but under a different compiler or optimization level, whatever value it leaves there is compliant. The next time you read a register at full width in a debugger and spot an odd value in the high bits, give the padding bits a second thought. That is the two register-sized routes verified; how a return value bigger than that makes the trip back is the next piece's business.
