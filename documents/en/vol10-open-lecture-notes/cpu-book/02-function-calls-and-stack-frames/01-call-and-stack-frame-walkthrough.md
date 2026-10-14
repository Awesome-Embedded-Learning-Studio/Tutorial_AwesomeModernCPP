---
title: "One Function Call, as the CPU Sees It"
chapter: 2
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "A disassembly walkthrough of one minimal function call: call pushing the return address, gdb confirming the true value at the top of the stack, and the stack frame being built and torn down"
reading_time_minutes: 10
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md
  source_hash: 4b9d295a61d4780855b1505327bf69e1d0f320c000595abd57b3a99bcbccee66
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# One Function Call, as the CPU Sees It

In the [previous piece](../01-prefix-vs-postfix-increment/01-int-instructions.md) we practiced on i++ and ++i, compiling the same source at -O0 and -O2, and the instruction sequences came out completely different. But back then we never left the outskirts of the function: the moment `call` showed up we treated it as a black box and moved on. This piece opens that box and follows one complete function call from jumping in to jumping back, watching what the CPU does at each step: how it jumps into the callee, what it relies on to jump back to where it came from, where the local variables live, and how all of it gets cleaned up when the function returns.

Every example and experiment in this piece is original to this site. The assembly and the debugger output you see here come verbatim from real runs on my machine.

Every listing of assembly and every gdb session below was produced in this environment. The gdb in it is a debugger: we use it to stop the program at a chosen instruction and read the values in the registers and in memory, and every session below that carries register readouts is its output. Here is the version information:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ gdb --version | head -1
GNU gdb (GDB) 17.2

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

One bit of noise reduction to declare up front: the `.cfi_*` lines are debug directives for unwinding (unwinding is the mechanism that walks back up the call chain layer by layer to reconstruct it); they generate no machine code and exist only as information for the debugger. I removed those lines when pasting the assembly into this piece, and the instruction stream you see is unchanged, not one character touched.

## Starting from a Minimal Call

We start from the smallest program there is: `main` calls `add` once and hands it 3 and 4 to add up. The whole program is seven lines, yet it already carries the basic ingredients of a function call: arguments, a jump, local variables, a return value.

```cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    int s = add(3, 4);
    return s;
}
```

We compile it to assembly with -O0. -O0 means the compiler does almost no optimization and translates the C++ structures into instructions exactly as written, which suits seeing what the call itself looks like best. The `-fno-asynchronous-unwind-tables` in the command is there to keep the assembly a little cleaner, but GCC 16 still emits the `.cfi_*` directives for C++ exception unwinding, so the listing below has also been through one manual deletion pass:

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S b1_add.cpp -o b1_add.s
```

```asm
	.file	"b1_add.cpp"
	.text
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	movl	-4(%rbp), %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	popq	%rbp
	ret
.LFE0:
	.size	_Z3addii, .-_Z3addii
	.globl	main
	.type	main, @function
main:
.LFB1:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$4, %esi
	movl	$3, %edi
	call	_Z3addii
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	leave
	ret
.LFE1:
	.size	main, .-main
	.ident	"GCC: (GNU) 16.2.1 20260810"
	.section	.note.GNU-stack,"",@progbits
```

The `_Z3addii` in the output is the symbol name `add(int, int)` gets after C++ name mangling: the compiler encodes the function name and the parameter types into the symbol, so that at link time functions with the same name but different signatures don't get mixed up. `main`, as we also saw, keeps the plain symbol name `main`; it takes no part in mangling.

In this listing only two instructions matter: `call _Z3addii` in main, and `ret` at the end of add. call does two things: push the return address onto the stack, then jump to the start of add. ret is the reverse: pop the return address off the top of the stack, then jump back to that address. That sounds like two dry definitions, so next we put gdb on the top of the stack and verify that the return address really is sitting there. Before we start, one thing about how to read this assembly, otherwise you can easily read it backwards on a first pass: this is AT&T syntax, the default throughout this piece. The previous piece's assembly was in Intel syntax, where the two operands sit the other way around, so watch for that when you compare the two. In `movq %rsp, %rbp` the source operand is on the left and the destination on the right; names starting with `%` are registers, names starting with `$` are immediates. The parenthesized form `-4(%rbp)` denotes a memory address: the memory at the location that is rbp's value minus 4.

The stack hasn't introduced itself to us yet; the next section brings it on stage.

## What call Actually Pushes

The stack is a perfectly ordinary stretch of memory with a special rule for its use: data may enter and leave through one end only, and whatever came in last goes out first. Putting data onto the stack is a push; on x86-64 a push clears 8 bytes at a time and moves the top of the stack toward lower addresses. Taking data off is a pop, and the top moves back toward the high side. The rsp register keeps track of where the top is; you can treat rsp as a pointer aimed at the top of the stack.

call promises to push the return address onto the stack, and in machine terms that breaks into three small steps: compute the address of call's own next instruction, move rsp 8 bytes toward lower addresses, write that address into the memory rsp points at, then jump to the target function's entry point. The return address is the marker the CPU leaves for the trip home: once the function has finished its work, the CPU uses it to find the way back. Take main from the first listing: `call _Z3addii` is immediately followed by `movl %eax, -4(%rbp)`, and that instruction's address is exactly the return address pushed this time.

Saying the return address sits at the top of the stack is one thing; better to look directly. Here is how: use gdb to set a breakpoint on add's first machine-code instruction (`break *add`; the asterisk form breaks at the instruction address of the function's entry, not at some source line). At that moment not a single instruction of add has run, and if the claim holds, the memory $rsp points at should be holding the address of the instruction after main's call. We compiled with -g, and the session came out as follows (the debuginfod banner is removed; the `--- [1] ---`-style separator lines are annotations we added afterward; everything else is verbatim):

```text
$ g++ -O0 -g b1_add.cpp -o b1_add
$ gdb --batch -q b1_add -ex 'set debuginfod enabled off' -ex 'break *add' -ex 'run' ...
```

```text
Breakpoint 1 at 0x1119: file b1_add.cpp, line 1.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, add (a=32767, b=-141919601) at b1_add.cpp:1
1	int add(int a, int b) {
--- [1] stopped BEFORE any instruction of add ---
rsp            0x7fffffffd0b8      0x7fffffffd0b8
rip            0x555555555119      0x555555555119 <add(int, int)>
0x7fffffffd0b8:	0x0000555555555144
--- [2] two single steps: push rbp ; mov rbp,rsp ---
0x00005555555511a	1	int add(int, int) {
0x00005555555511d	1	int add(int, int) {
rsp            0x7fffffffd0b0      0x7fffffffd0b0
rbp            0x7fffffffd0b0      0x7fffffffd0b0
--- [3] info frame: where gdb thinks saved return address lives ---
Stack level 0, frame at 0x7fffffffd0c0:
 rip = 0x55555555511d in add (b1_add.cpp:1); saved rip = 0x555555555144
 called by frame at 0x7fffffffd0e0
 source language c++.
 Arglist at 0x7fffffffd0b0, args: a=32767, b=-141919601
 Locals at 0x7fffffffd0b0, Previous frame's sp is 0x7fffffffd0c0
 Saved registers:
  rbp at 0x7fffffffd0b0, rip at 0x7fffffffd0b8
--- [4] read the return address slot at rbp+8 ---
0x7fffffffd0b8:	0x0000555555555144
```

Look at [1] in the session: the program is stopped at add's entry, `rsp` reads 0x7fffffffd0b8, and `x/gx $rsp` read 0x0000555555555144 from that address. Is that number the return address? We check it against the disassembly of main from the same binary:

```text
Dump of assembler code for function main():
   0x000055555555512d <+0>:	push   %rbp
   0x000055555555512e <+1>:	mov    %rsp,%rbp
   0x0000555555555131 <+4>:	sub    $0x10,%rsp
   0x0000555555555135 <+8>:	mov    $0x4,%esi
   0x000055555555513a <+13>:	mov    $0x3,%edi
   0x000055555555513f <+18>:	call   0x555555555119 <_Z3addii>
   0x0000555555555144 <+23>:	mov    %eax,-0x4(%rbp)
   0x0000555555555147 <+26>:	mov    -0x4(%rbp),%eax
   0x000055555555514a <+29>:	leave
   0x000055555555514b <+30>:	ret
End of assembler dump.
```

main's `call` sits at 0x55555555513f, and its next instruction, `mov %eax,-0x4(%rbp)`, sits at 0x5555555555144; the number we read off the top of the stack matches the latter character for character. Next look at [3]: gdb's analysis of the stack frame also says `saved rip = 0x555555555144`, and it pins the return address at 0x7fffffffd0b8. The read in [4] happens after the prologue (the prologue is the handful of instructions at the start of a function that set up its stack; we go through it properly in the stack frame section), and the value read at `rbp+8` is still 0x0000555555555144. Three sources agree: the value at the top of the stack, the address of the instruction after call, and gdb's saved rip are all naming the same address.

At [2] we single-stepped two instructions (`push rbp` and `mov rbp,rsp`): rsp went from 0x7fffffffd0b8 to 0x7fffffffd0b0, exactly 8 bytes toward lower addresses, and rbp was set to the new top of the stack. What they are for is the subject of the next section.

This session also hides something strange: at the entry point gdb prints the arguments as `add (a=32767, b=-141919601)`, a pair of garbage values. The experiment actually worked; what we are looking at is the truth of the matter. At that moment 3 and 4 are still sitting safely in the edi and esi registers, and the stack holds no trace of them yet. gdb reads memory following the debug info's rbp-relative offsets, so what it reads back naturally is not 3 and 4. We have a control for this too: move the breakpoint onto a source line instead (it lands after the prologue; the separator lines here are likewise annotations added afterward, and the banner is removed as well):

```text
Breakpoint 1, add (a=3, b=4) at b1_add.cpp:2
2	    return a + b;
--- at entry of add ---
rdi            0x3                 3
rsi            0x4                 4
rsp            0x7fffffffd0b0      0x7fffffffd0b0
rbp            0x7fffffffd0b0      0x7fffffffd0b0
```

With the breakpoint moved to the `return a + b;` source line, rdi and rsi report 3 and 4. Put the two sessions together and one fact is confirmed: the arguments travel in registers in the first place, and getting them onto the stack is the function's own business. The first two lines of add in the first program, `movl %edi, -4(%rbp)` and `movl %esi, -8(%rbp)`, are exactly this moving job. The full convention for how arguments get into registers is a topic we cover in detail in the next installment of the series.

## The Stack Frame: The Stretch of Stack a Function Occupies

With the return address identified, we turn to the function itself. The moment the callee is entered, the top of the stack holds only the return address call pushed, and everything above it belongs to the caller. A function with work to do has to claim a stretch of the stack for itself: the local variables it will house, the registers it must save, the return addresses from any calls it makes later on, each with its own place in that stretch. That stretch of space together with the conventions that manage it is called a stack frame. You can think of it as the function's private working area on the stack.

At -O0, GCC uses rbp as the anchor point for every function: the location rbp points at holds the previous function's old rbp value, rbp+8 holds the return address, and this frame's locals sit at rbp minus a few offsets. We have already seen half of this layout in the last section: the saved rip gdb reported lives at 0x7fffffffd0b8, which is exactly rbp+8 after the prologue has executed. Every function uses rbp this way, as its own anchor, so the first thing a callee does on entering is store the caller's rbp away and restore it before returning; otherwise the caller's anchor point is lost.

We call the instructions at the start of a function that set up its stack frame the prologue, and the ones at the end that tear it down the epilogue. The best material for showing them is a function with local variables. We write a three that holds 3 ints and one summation, then hands the sum to consume, which adds one to it and returns. consume's and main's assembly are shaped just like add's and main's in the first program, so we look at three alone:

```cpp
int consume(int v) {
    return v + 1;
}

int three() {
    int x = 1;
    int y = 2;
    int z = 3;
    int s = x + y + z;
    return consume(s);
}

int main() {
    return three();
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S b3b_locals_call.cpp -o b3b_locals_call.s
```

Compiled at -O0, the assembly of `_Z5threev` is as follows (the .cfi directives are removed, and the main and consume sections, which mirror the ones in the first program, are dropped along with them):

```asm
_Z5threev:
.LFB1:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$1, -16(%rbp)
	movl	$2, -12(%rbp)
	movl	$3, -8(%rbp)
	movl	-16(%rbp), %edx
	movl	-12(%rbp), %eax
	addl	%eax, %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, %edi
	call	_Z7consumei
	leave
	ret
.LFE1:
	.size	_Z5threev, .-_Z5threev
```

## A Walkthrough, Line by Line: How the Stack Changes at Every Step of three

The walkthrough starts at the moment call has just landed and not one instruction of three has run. The stack looks exactly as it did in the gdb session in the call section: what rsp points at is the return address, and above it sits main's frame. We describe each step in relative positions rather than making up concrete addresses: call the slot holding the return address slot 0, the ones below it slot 1, slot 2, and so on, each slot 8 bytes.

`pushq %rbp` pushes rbp's old value onto the stack: rsp moves down one slot, and main's old rbp value is written into the new top slot (we will call it slot 1). The push itself lowers rsp by 8 bytes, and at the same time the caller's anchor is stored into this frame. From here on, when main wants its rbp back, it comes to slot 1 to fetch it.

`movq %rsp, %rbp` records the position of the stack top as the new anchor: from now on rbp points at slot 1, the slot holding the old rbp, and every stack access within this frame takes it as the reference. The `-4(%rbp)` and `-16(%rbp)` forms we saw earlier are measured against the anchor it sets.

`subq $16, %rsp` makes room for the locals: rsp moves down another 16 bytes from slot 1, yielding four 4-byte slots. Why does three need this step at all? The reason is this: three calls consume inside itself, and call will write the return address into the 8 bytes just below rsp. If rsp had not been lowered, that write would land right inside three's variable area and wipe out the variable s. Only a function that calls somebody else has this third step. Count it out: 3 variables plus 1 summation result fill those 16 bytes exactly.

Next we watch three `movl` instructions write the initial values into the variable slots. The four slots are assigned like this:

| Variable | Location | Initial value |
|---|---|---|
| x | -16(%rbp) | 1 |
| y | -12(%rbp) | 2 |
| z | -8(%rbp) | 3 |
| s (the sum) | -4(%rbp) | 6 |

GCC's arrangement at -O0 is just this literal: each int takes one 4-byte slot, no gaps are left between slots, and the 16 bytes are used up just like that. The sum s lives at -4(%rbp), right at rbp's feet.

We leave a placeholder here for the stack frame layout figure, to be added later: it will show the layout from the top of the stack down to the locals area after the call, that is, the relative positions of the return address, the saved rbp, x, y, z, and s, to be filled in by this site's diagram pipeline.

The six summation instructions should look familiar by now; they are all moving and adding:

```asm
	movl	-16(%rbp), %edx
	movl	-12(%rbp), %eax
	addl	%eax, %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	movl	%eax, -4(%rbp)
```

x and y go into edx and eax, `addl %eax, %edx` leaves the result of x+y in edx, z then lands in eax as well, the second addition leaves the result 6 in eax, and the last movl writes it into s's slot. -O0 does no register optimization: every read and write of every variable goes through memory, which makes for more instructions, but every step is plain.

Then comes this piece's second call. The `movl %eax, %edi` at the top loads the argument s into edi, and `call _Z7consumei` pushes the return address and jumps into consume's entry. Once consume has computed its v+1 and handed the result back in eax, three's `leave` and `ret` take over the closing. leave is one instruction that does the work of two: `mov %rbp, %rsp` plus `pop %rbp`. The first half pulls rsp back to rbp's position in one step, discarding the locals area no matter how much was originally sub'd away. The second half pops the old rbp and hands it back to main. After this move, what sits at the top of the stack is exactly the return address again, `ret` immediately pops it and jumps back, and three's stack frame is gone.

consume, like add at the start, is a leaf function, and that is worth noting: it closes out with a bare pop rbp. The next piece is devoted to it.
