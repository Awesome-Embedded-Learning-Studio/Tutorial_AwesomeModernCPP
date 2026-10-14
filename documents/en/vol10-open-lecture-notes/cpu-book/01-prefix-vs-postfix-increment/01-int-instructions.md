---
title: "i++ vs ++i: Comparing int Assembly at -O0"
chapter: 1
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Sets up the environment and the basics of reading assembly, then compares the -O0 output of int increments: when the value is used, postfix pays one extra instruction to stash the old value; when it is discarded, the two forms match instruction for instruction"
reading_time_minutes: 7
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/01-prefix-vs-postfix-increment/01-int-instructions.md
  source_hash: 6ed01bbe454ff9588bd34c7230571298b0a079e61a332e16e31e41b3b4d6c735
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# i++ vs ++i: Comparing int Assembly at -O0

```cpp
int a = i++;  // a gets the old value, from before the increment
int b = ++i;  // b gets the new value, from after the increment
```

You have written these two lines countless times. The semantic difference between prefix and postfix, and the undefined behavior of things like `i = i++`, are covered in vol1's [Operator Basics: Making Data Move](../../../vol1-fundamentals/c_tutorials/03A-operators-basics.md); here we only review it in one sentence: postfix hands the expression the value from before the increment, prefix hands over the value from after it.

Next to the semantic difference there's also a performance mantra: in loops, write `++i` rather than `i++`, because postfix costs more. I used to recite it too, until one day someone pinned me down with a question: which instruction is slower? I had no answer. So let's stop reciting and put the compiler's output on the table, and go through it one instruction at a time.

Every example and experiment in this piece is original to this site. Every block of assembly and every line of output you see here comes from a real run on my machine.

## Our Test Environment

Following this site's convention, the environment comes first, so that every block of assembly below is something you can reproduce on a machine with the same setup:

```text
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

```text
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

A few environment facts that affect how you read the output. If your own runs don't match what's in this piece, the cause is most likely one of these:

- We don't pass `-std` explicitly. GCC 16's default language standard is gnu++20 (`__cplusplus` is 202002L), and every experiment below runs on that default.
- The distro turns on `-fstack-protector-strong` by default, so some -O0 functions carry stack canary instructions (a sentinel value the stack protector plants, tied to `%fs:40`). It has nothing to do with our topic, so don't be alarmed when you see it.
- My output contains no `endbr64`. That instruction shows up at the start of every function once CET is on (Intel's control-flow enforcement technology, which restricts indirect jumps to legitimate entry points). If your build enables CET you'll see it as an extra line, a perfectly normal difference.
- I originally wanted to quiet the assembly down with `-fno-asynchronous-unwind-tables`, but in testing it has no effect on C++ compiles: the `.cfi_*` directive lines are still there, because when C++ throws an exception it depends on the unwind tables to walk all the way back to the catch, and g++ switches the option back on. So in every listing below I have manually removed the `.cfi_*` lines and pseudo-instructions like `.globl` and `.size`. The instructions themselves are untouched, and I won't repeat this.

The quotes you occasionally run into in the assembly (for instance `"_Z8use_posti"`) are also verbatim: Intel-syntax output puts quotes around symbol names containing special characters. It's a product of the formatting, not an error, so don't read it as one.

## What to Know Before Reading the Assembly

The only tool we need is g++ itself. How do you get assembly? Disassembly in the strict sense means working backward from compiled machine code to assembly text; that's `objdump`'s job. We'll take the easier route and have the compiler write out the assembly directly: hand g++ the `-S` flag and you get a `.s` text file. Both sources read the same way, and `-S` output keeps the function names and labels, which is just right for a side-by-side comparison.

What are -O0 and -O2? The O in these options stands for optimization. `-O0` is the no-optimization setting: the compiler translates every statement literally into instructions, each variable lives in its own stack slot, every read and write goes back to memory, and single-stepping in a debugger is at its most comfortable. `-O2` turns on the everyday optimizations and is the norm for release builds. What we want to see boils down to one thing: how the same code looks under these two settings.

What separates the two assembly syntaxes? x86 assembly has two writing conventions. AT&T syntax is the GNU toolchain default, and its operand direction runs opposite to a C assignment: source on the left, destination on the right, register names prefixed with `%`, immediates prefixed with `$`. Intel syntax runs in the same direction as a C assignment; `mov eax, 1` means putting 1 into eax. This piece uses both. The first group of experiments we read closely in Intel syntax (add `-masm=intel` at compile time), because it's friendlier to people who write C++. From the second group on we switch to AT&T, since that's what tools like objdump will show you from here on. Each assembly block below is labeled with its syntax, so you can read along.

What are registers? Registers are the few extremely fast storage cells the CPU carries inside itself. eax and edx are 32-bit general-purpose registers; rax and rdx are their 64-bit widened versions. rbp usually serves the compiler as the anchor of the stack frame: a function's locals are arranged at negative offsets from it. `-20[rbp]` reads as the memory at the address rbp minus 20, and the `DWORD PTR` in front marks the access as 32 bits wide. As for odd names like `_Z8use_posti`, that's the mangled symbol name GCC builds for a C++ function, encoding the name's length and the parameter types; one pass through `c++filt` and you get `use_post(int)` back.

## int With the Value Taken: Postfix Pays One Extra Instruction at -O0

Let's start from the plainest scenario: the operand is an ordinary int variable, and somebody actually takes the value of the increment expression. You'll see further down why that restriction matters. When you try this yourself, be sure to write it in the value-taking form.

```cpp
volatile int sink;

void use_post(int i) {
    int x = i++;
    sink = x;
}

void use_pre(int i) {
    int x = ++i;
    sink = x;
}

int main() {
    use_post(41);
    use_pre(41);
    return 0;
}
```

Here we use `volatile` as our anti-optimization anchor. It demands that every read and write of sink genuinely happen, and that their order stay as written. So no matter which optimization level we dial up, the movement of x in both functions has somewhere to land, and the comparison keeps its meaning. Our compile command:

```bash
g++ -O0 -fno-asynchronous-unwind-tables -masm=intel -S a1.cpp -o a1_intel.s
```

(The unwind option is a no-op for C++, as we said earlier, and harmless to keep. The a1 in the file name is the number I gave the experiment; in the next two pieces you'll meet a3, a4, a6, and a7. The numbers I skipped belong to comparison scenarios the articles don't show, and you can call the files anything when reproducing.) Not a single warning showed up during the compile. The postfix version's output (Intel syntax):

```asm
"_Z8use_posti":
	push	rbp
	mov	rbp, rsp
	mov	DWORD PTR -20[rbp], edi
	mov	eax, DWORD PTR -20[rbp]
	lea	edx, 1[rax]
	mov	DWORD PTR -20[rbp], edx
	mov	DWORD PTR -4[rbp], eax
	mov	eax, DWORD PTR -4[rbp]
	mov	DWORD PTR "sink"[rip], eax
	nop
	pop	rbp
	ret
```

Now the prefix version's output (Intel syntax):

```asm
"_Z7use_prei":
	push	rbp
	mov	rbp, rsp
	mov	DWORD PTR -20[rbp], edi
	add	DWORD PTR -20[rbp], 1
	mov	eax, DWORD PTR -20[rbp]
	mov	DWORD PTR -4[rbp], eax
	mov	eax, DWORD PTR -4[rbp]
	mov	DWORD PTR "sink"[rip], eax
	nop
	pop	rbp
	ret
```

The opening two lines, `push rbp` and `mov rbp, rsp`, are the standard prologue for any function at -O0, and their job is to stand up each function's stack frame. What they're for gets a dedicated look later in the series, when we cover function calls and stack frames, so we skip them here; the subject you came for starts at the third line.

The third line, `mov DWORD PTR -20[rbp], edi`, deserves a note too. The x86-64 calling convention on Linux says a function's first integer argument arrives in edi, and this instruction stores the argument into a stack slot; from then on, i lives at that address. Here are two tables, one for the core stretch of each function:

| Postfix instruction | What it does |
|---|---|
| `mov eax, DWORD PTR -20[rbp]` | Reads i's old value into eax |
| `lea edx, 1[rax]` | Computes i+1 into edx; the old value in eax is left untouched |
| `mov DWORD PTR -20[rbp], edx` | Writes the new value back to i |
| `mov DWORD PTR -4[rbp], eax` | x is assigned the old value |
| `mov eax, DWORD PTR -4[rbp]` | Reads x back into eax |
| `mov DWORD PTR "sink"[rip], eax` | Stores into sink |

| Prefix instruction | What it does |
|---|---|
| `add DWORD PTR -20[rbp], 1` | Adds one in place in memory; i becomes the new value directly |
| `mov eax, DWORD PTR -20[rbp]` | Reads the new value into eax |
| `mov DWORD PTR -4[rbp], eax` | x is assigned the new value |
| `mov eax, DWORD PTR -4[rbp]` | Reads x back into eax |
| `mov DWORD PTR "sink"[rip], eax` | Stores into sink |

Read the two tables above starting from x: it was just stored into the stack slot `-4[rbp]`, and the very next instruction loads it straight back out, unchanged. Don't take that for the compiler being silly; this is -O0's literal style of translation. Each statement manages its own business, and no shortcuts through registers are left standing between statements. Now look at `lea`: its day job is computing addresses (load effective address), and the postfix version borrows it to do the addition here, because `add` overwrites its own operand while the old value has to stay put in its original register. The new value, then, has no choice but to be computed somewhere else.

Counting by the two tables above, from the step where the parameter lands on the stack through to the finished write of sink, postfix takes six instructions and prefix takes five. The parameter store, the prologue, and the epilogue are common to both sides and stay out of the count. That one extra instruction is the price of stashing the old value. Postfix semantics require the old value to still be alive at the moment the increment happens, so the compiler has no choice but to give it a register (or stack slot) of its own; prefix carries no such burden, and a single `add` settles it in place.

Here is an AT&T copy of the same thing for reference: the very same output, written the other way.

```asm
_Z8use_posti:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -20(%rbp)
	movl	-20(%rbp), %eax
	leal	1(%rax), %edx
	movl	%edx, -20(%rbp)
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	nop
	popq	%rbp
	ret

_Z7use_prei:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -20(%rbp)
	addl	$1, -20(%rbp)
	movl	-20(%rbp), %eax
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	nop
	popq	%rbp
	ret
```

`leal 1(%rax), %edx` and the `lea edx, 1[rax]` above are the same thing, matching one to one instruction for instruction, only written in the opposite direction. From the next group of experiments on, we settle on AT&T. It's the toolchain default and the style you'll see whenever you read assembly with objdump, so this is where Intel exits.

## When Nobody Wants the Old Value: Even -O0 Shows No Difference

In the experiment above, x took the increment's value. What about when nobody wants it? Let's guess first: with a lone `i++;`, the old value is about to be thrown away, but manufacturing it still costs a step of work, so postfix ought to be doing extra work, right? Let's swap the source for two separate functions:

```cpp
volatile int sink;

void post_only(int i) {
    i++;
    sink = i;
}

void pre_only(int i) {
    ++i;
    sink = i;
}

int main() {
    post_only(41);
    pre_only(41);
    return 0;
}
```

We can't leave out the `sink = i;` line; otherwise -O2 would delete the entire function bodies and the comparison would lose its point. Here are the two compiled function bodies, in AT&T syntax (same for the rest):

```asm
_Z9post_onlyi:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	addl	$1, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	nop
	popq	%rbp
	ret

_Z8pre_onlyi:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	addl	$1, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	nop
	popq	%rbp
	ret
```

Even playing spot-the-difference you'll come up empty: the two functions match instruction for instruction (function names excepted). A value-discarding `i++;` at -O0 is exactly one in-memory `addl`, precisely what `++i;` compiles to.

The two forms converged sooner than I expected. -O0, with no optimization turned on at all, should in principle be the setting most likely to expose a difference between the two spellings. But when the old value has no user, the step that stashes it never happens in the first place. GCC's front end (the half that handles syntax and semantics) picks the in-place add while it expands the statement. So if you want to verify this with your own hands, be sure to write it in the value-taking form (`int x = i++;`). A bare `i++;` is identical even at -O0, and no amount of watching it will make a difference appear.
