---
title: "Functions That Keep Their Stack Frames, and Recursion That Fills the Stack"
chapter: 2
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard:
  - 11
  - 17
description: "Only tree recursion keeps its frame; the frame pointer is omitted by default from -O1 on; recursion with no termination condition fills the 8 MiB stack and crashes on call's push"
reading_time_minutes: 10
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/02-function-calls-and-stack-frames/03-kept-frames-and-stack-exhaustion.md
  source_hash: e2e6743d1a9d64de2345668ee115622e16a937f2053eee42d9e533aea2ee06f0
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Functions That Keep Their Stack Frames, and Recursion That Fills the Stack

In the previous piece we watched -O2 compute the call sites of small functions away entirely, while taking a function's address forced a real body to be emitted. This piece pushes the question onto the stack frame: which functions still have to set up a proper frame at -O2, and then we drive recursion with no termination condition all the way to the stack's limit and watch how it ends. The assembly and debugger sessions in this piece run in the same environment as [the first piece](01-call-and-stack-frame-walkthrough.md).

## Which Functions Keep a Stack Frame at -O2

So which functions get to keep a genuine stack frame at -O2? The intuitive answer is a big function body, or recursion. We built the experiment on that intuition, and the compiler took it apart point by point; all three counterexamples come from attempts we actually ran.

Our first attempt was a loop-summing function with the upper bound hard-coded as the constant 100: the compiler evaluated the whole loop at compile time, `sum100` turned straight into `movl $5050, %eax` plus `ret`, and main kept nothing but the constant 5050. With the function computed empty, a stack frame never even enters the picture.

The second attempt swapped the upper bound for a volatile parameter n. The loop survived, but `sum_up_to` became a leaf function with no call in the loop body, so GCC housed the local variable in the red zone (`movl %edi, -4(%rsp)`), the conventional stack frame still never appeared, and the call in main got partially inlined and rewritten into vectorized form besides. Vectorization means rewriting a loop into SIMD instructions that compute several elements at once. So a big function body does not buy you a frame, and this is where we hold the live evidence.

The third attempt went recursive, with a function shaped like `return n * fact(n-1)`: at -O2 GCC applied the accumulator transformation and rewrote the entire recursion into a loop, and the call disappeared along with it. What the accumulator transformation means: the multiply-down-to-n process becomes a loop that carries the running product along. Even this non-tail-recursive spelling is no guarantee; the recursive shape by itself does not stop the compiler.

What actually kept the frame was tree recursion, and our run with it succeeded. Here is fib's source:

```cpp
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    volatile int n = 10;
    volatile int s = fib(n);
    return s;
}
```

The two recursive calls `fib(n-1)` and `fib(n-2)` cannot be folded into a simple loop; every call needs a return address and a frame of its own, so the call cannot be eliminated and the stack frame has to be set up in earnest. The assembly below is an excerpt (the loop body in the middle is deleted; compile the source above yourself if you want to check). The compile command and the excerpt:

```text
$ g++ -O2 -fno-asynchronous-unwind-tables -S b5b2_recur.cpp -o b5b2_recur.s
```

```asm
_Z3fibi:
.LFB0:
	subq	$152, %rsp
	movq	%r14, 136(%rsp)
	movl	%edi, %r14d
	cmpl	$1, %edi
	jle	.L37
	...
.L30:
	leal	-1(%r12), %edi
	subl	$2, %r12d
	call	_Z3fibi
	addl	%eax, %r15d
	cmpl	$1, %r12d
	jg	.L30
	...
.L37:
	movl	%r14d, %eax
	movq	136(%rsp), %r14
	addq	$152, %rsp
	ret
```

Now main's excerpt (`call _Z3fibi` is there too, partly rewritten the same way; the elided instructions after it are not shown):

```asm
main:
.LFB1:
	pushq	%rbx
	xorl	%r9d, %r9d
	subq	$16, %rsp
	movl	$10, 8(%rsp)
	movl	8(%rsp), %ebx
	cmpl	$1, %ebx
	jle	.L57
	movl	%ebx, %r8d
.L56:
	leal	-1(%r8), %edi
	subl	$2, %r8d
	call	_Z3fibi
	...
```

fib's frame is the real thing: `subq $152, %rsp` opens 152 bytes in one step, and it also saves a run of registers that the convention requires to keep their original values before use (the jargon term is callee-saved, registers the callee has to preserve before using them; the ones we caught in the frame this time are r14, rbx, r12, r13, rbp, and r15), each parked at an offset from rsp. `call _Z3fibi` stays inside the `.L30` loop. The interesting part is that GCC still carried out partial iteration, meaning it turned one side of the recursion into a loop: the `fib(n-2)` side is the one rewritten as a loop (the `subl $2, %r12d` marching through `.L30` is it), and only the `fib(n-1)` side keeps its call. At this point the criterion can be said outright: a frame survives because the call cannot be eliminated. Every call needs a return address of its own, and the compiler cannot conjure up a second frame to share. fib keeps at least one call, and so the frame stays.

## The Frame Pointer Can Be Omitted Too

The first two steps of every prologue exist to serve rbp. Reading this far you may be wondering: is this anchor truly indispensable? GCC's answer is not necessarily. The -fomit-frame-pointer switch governs exactly this, and the [GCC manual's optimization options page](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) puts it as "Omit the frame pointer in functions that don't need one", leaving it out where a function needs no frame pointer, and it has been on by default since -O1.

What does the assembly look like once it is gone? We take fib as the comparison subject: at -O2 it has the biggest frame and the most registers stashed on the stack, so the contrast shows best. We compiled the same fib source twice, once with -O2's defaults and once with -fno-omit-frame-pointer added (the switch that asks for the frame pointer to be kept):

```text
$ g++ -O2 -fno-asynchronous-unwind-tables -S b5b2_recur.cpp -o b7_fib_default.s
$ g++ -O2 -fno-omit-frame-pointer -fno-asynchronous-unwind-tables -S b5b2_recur.cpp -o b7_fip_fip.s
```

From both files we paste only the head and tail of `_Z3fibi`; the loop body in the middle is deleted, marked with `...`. The first one is the -O2 default (frame pointer omitted):

```asm
_Z3fibi:
.LFB0:
	subq	$152, %rsp
	movq	%r14, 136(%rsp)
	movl	%edi, %r14d
	...
.L37:
	movl	%r14d, %eax
	movq	136(%rsp), %r14
	addq	$152, %rsp
	ret
```

Now the other one, with -fno-omit-frame-pointer added:

```asm
_Z3fibi:
.LFB0:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$144, %rsp
	movq	%rbx, -40(%rbp)
	movl	%edi, %ebx
	...
.L37:
	movl	%ebx, %eax
	movq	-40(%rbp), %rbx
	leave
	ret
```

The default version's prologue is down to a bare `subq $152, %rsp`: `push rbp` and `mov rbp,rsp` are gone, and all addressing into the stack has switched to rsp-relative forms (`136(%rsp)`, `112(%rsp)`, that family). The epilogue changes shape along with it: no more leave, back to a hand-written `addq $152, %rsp` plus `ret`. The frame going from 152 to 144 makes sense too: the frame-pointer version pushes an extra 8 bytes for rbp, and the register allocation differs. The default version keeps n in r14, the frame-pointer version switches to rbx.

Even more convincing is where rbp ended up. The default version's assembly contains these lines (the inline comments are ours; the original assembly file has none):

```asm
	movq	%rbp, 112(%rsp)     # the old rbp is stored in the frame like ordinary callee-saved data
	movl	%eax, %ebp          # rbp serves as the loop variable
	subl	%edx, %ebp
	movl	%ebp, 56(%rsp)
	xorl	%ebp, %ebp
	addl	%eax, %ebp          # joins the addition
```

`movq %rbp, 112(%rsp)` stores the old rbp in the frame like ordinary data, and then `movl %eax, %ebp` and `addl %eax, %ebp` put rbp to work as a loop variable that joins the addition. Here lies the payoff of dropping the frame pointer: what you gain is one more register anyone can use. The manual's exact words are that it "makes an extra register available", and we watched it happen in the instructions.

Why would a debugger care? Let's lay the mechanism out. With a frame pointer, each frame stores the previous frame's rbp at [rbp], and following rbp upward one step at a time recovers the complete call chain. With it omitted, rbp is no longer trustworthy; to rebuild the call chain from the current instruction position you need the unwind information, debugging data that records each function's stack layout, and the `.cfi` pseudo-instructions are where it comes from. So any setting where you want to walk the rbp chain needs -fno-omit-frame-pointer added explicitly, at the price of two extra instructions per function and one usable register fewer. The manual's wording warns about one more thing: the negative form -fno-omit-frame-pointer does not guarantee every function gets a frame pointer, because some targets always omit it in leaf functions. That is not the same omission we saw in the leaf-function section of [the previous piece](02-leaf-functions-stack-direction-and-o2.md): there the missing piece was sub rsp, with `push rbp` and `mov rbp,rsp` still sitting at the top as usual, while the manual's omission drops the frame pointer itself.

## After Recursion Fills the Stack

One last thing to watch, and this one genuinely breaks: recursion without a termination condition. Every level of countdown pushes a return address and opens a small frame; the stack reaches lower one level at a time, and sooner or later the probing hits the end of the limit.

```cpp
int countdown(int n) {
    return countdown(n - 1);
}

int main() {
    return countdown(0);
}
```

The stack limit and the core dump configuration, pasted here exactly as they read:

```text
$ ulimit -s
8192
$ ulimit -c
0
$ cat /proc/sys/kernel/core_pattern
|/usr/lib/systemd/systemd-coredump %P %u %g %s %t %c %h %d %F %I
```

```text
$ g++ -O0 -g b6_countdown.cpp -o b6_countdown
$ ulimit -c 0; ./b6_countdown; echo "=== exit status: $? ==="
```

```text
=== exit status: 139 ===
```

The program died, and at the shell level the entire body of evidence is that one exit status line. 139 minus 128 gives 11, and 11 is the signal number of SIGSEGV. This machine runs WSL2, and under a non-interactive shell neither bash nor zsh prints the Segmentation fault line, so you will not see any crash text when you reproduce this; treat the exit status as your shell-level evidence. There is no core file either: ulimit -c is 0 on this machine, and core_pattern points into the systemd-coredump pipe, so crash dumps are handled by systemd. To see the crash site we call in gdb (the === separator lines in the session are printed by the printf commands; they are not gdb's own output):

```text
$ gdb --batch -q b6_countdown -ex 'set debuginfod enabled off' -ex 'run' \
      -ex 'printf "=== program stopped by crash ===\n"' -ex 'info program' \
      -ex 'printf "=== registers around crash ===\n"' -ex 'info registers rsp rip' \
      -ex 'printf "=== backtrace head (repeats elided by tool) ===\n"' -ex 'bt 5' \
      -ex 'printf "=== can we read the stack page at rsp? ===\n"' -ex 'x/gx $rsp'

[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Program received signal SIGSEGV, Segmentation fault.
0x000055555555512c in countdown (n=-261893) at b6_countdown.cpp:2
2	    return countdown(n - 1);
=== program stopped by crash ===
Last stopped for thread 1 (Thread 0x7ffff7f66780 (LWP 97996)).
	Using the running image of child process 97996.
Program stopped at 0x55555555512c.
It stopped with signal SIGSEGV, Segmentation fault.
=== registers around crash ===
rsp            0x7fffff7ff000      0x7fffff7ff000
rip            0x55555555512c      0x55555555512c <countdown(int)+19>
=== backtrace head (repeats elided by tool) ===
#0  0x000055555555512c in countdown (n=-261893) at b6_countdown.cpp:2
#1  0x0000555555555131 in countdown (n=-261892) at b6_countdown.cpp:2
#2  0x0000555555555131 in countdown (n=-261891) at b6_countdown.cpp:2
#3  0x0000555555555131 in countdown (n=-261890) at b6_countdown.cpp:2
#4  0x0000555555555131 in countdown (n=-261889) at b6_countdown.cpp:2
=== can we read the stack page at rsp? ===
0x7fffff7ff000:	0x0000000000000000
```

We also lifted countdown's disassembly from the same binary:

```text
Dump of assembler code for function _Z9countdowni:
   0x0000000000001119 <+0>:	push   %rbp
   0x000000000000111a <+1>:	mov    %rsp,%rbp
   0x000000000000111d <+4>:	sub    $0x10,%rsp
   0x0000000000001121 <+8>:	mov    %edi,-0x4(%rbp)
   0x0000000000001124 <+11>:	mov    -0x4(%rbp),%eax
   0x0000000000001127 <+14>:	sub    $0x1,%eax
   0x000000000000112a <+17>:	mov    %eax,%edi
   0x000000000000112c <+19>:	call   0x1119 <_Z9countdowni>
   0x0000000000001131 <+24>:	leave
   0x0000000000001132 <+25>:	ret
End of assembler dump.
```

The program crashed on the instruction at countdown+19, which is the call line: it has to write the return address it just computed into [rsp-8] = 0x7fffff7feff8, an address already past the stack region's lower bound, and the write triggered SIGSEGV on the spot. The direct killer that uses up the stack top is call's push. rsp came to rest at 0x7fffff7ff000, a whole-page boundary; subtract it from the stack region's upper bound 0x7ffffffff000 and the difference is 0x800000, or 8 MiB, exactly matching the 8192 KiB that ulimit -s reported (the rsp 0x7fffffffd0b8 we saw in the gdb session in the call section of [the first piece](01-call-and-stack-frame-walkthrough.md) sits just under that upper bound). Each frame is 24 bytes (8 from push rbp plus 16 from sub $0x10), n counted down from 0 all the way to -261893, a depth of about 262,000 levels taking up roughly 6 MiB; the gap to 8 MiB was eaten by the initial stack contents a process starts with (the environment block, argv, that sort of thing), which we did not measure item by item.

The tail of the session holds one more piece of evidence worth a close look: `x/gx $rsp` succeeded in reading 0x7fffff7ff000, and what it read was 0, so that page is still readable, while call wants to write [rsp-8], 8 bytes further down, which lands outside the stack region. One read and one write pin the stack's lower bound down from both sides. The 5 frames from `bt 5` are the frame count the command explicitly requested; a complete backtrace of this 260,000-level stack would scroll all the way back to main, and every frame's source line would stop on the same endless recursion.

Running the same thing again under AddressSanitizer makes the diagnosis far easier to read. ASan is a memory-error detector wired in at compile time with -fsanitize=address; it adds poison zones to the frame of every level, the frames grow, and the ceiling arrives at about 247 levels, which is why the report holds only a couple hundred frames. The full report runs to 253 lines, nearly all of them repeated countdown frames; we keep the head and tail and cut the repeated frames #3 through #245 in between (rerun it yourself for the complete log):

```text
$ g++ -O0 -g -fsanitize=address b6_countdown.cpp -o b6_countdown_asan
$ ./b6_countdown_asan
```

```text
AddressSanitizer:DEADLYSIGNAL
=================================================================
==97889==ERROR: AddressSanitizer: stack-overflow on address 0x7ffd66f89ff8 (pc 0x58e9a061915c bp 0x7ffd66f8a010 sp 0x7ffd66f8a000 T0)
    #0 0x58e9a061915c in countdown(int) /tmp/cpu-book-02/b6_countdown.cpp:2
    #1 0x58e9a0619160 in countdown(int) /tmp/cpu-book-02/b6_countdown.cpp:2
    #2 0x58e9a0619160 in countdown(int) /tmp/cpu-book-02/b6_countdown.cpp:2
    ...(frames #3 through #245 are all the same countdown(int) frame, elided)...
    #246 0x58e9a0619160 in countdown(int) /tmp/cpu-book-02/b6_countdown.cpp:2

SUMMARY: AddressSanitizer: stack-overflow /tmp/cpu-book-02/b6_countdown.cpp:2 in countdown(int)
==97889==ABORTING
```

ASan writes the stack-overflow verdict on the first line of the report, far easier to read than the bare crash's single SIGSEGV line, and the process winds up with ABORTING and an exit status of 1. We captured this stack overflow along two roads: the bare version runs 24 bytes per level and stacks to roughly 260,000 levels, while the ASan version hits the ceiling at about 247. Filling the full 8 MiB stack is not something to stage in a browser, so we take just the first few layers to look at the structure. The executor below loads -O0-style fib(5): every level of recursion pushes rbp and the return address and subs out a locals area, the stack window piles layer upon layer toward lower addresses, and every ret peels one layer back. The whole run is 15 calls and a bit over two hundred steps, but you can also single-step just the first two levels and watch the three things each frame adds: the return address, the saved rbp, and the argument slot. Symbol names lose their mangling, fib is not _Z3fibi.

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/recursion_frames.json" />

If you want to turn crash forensics into a craft of your own, head over to [crash-lab's hands-on crash scenes](../../../crash-lab/a-memory-safety/01-null-deref.md). The next piece in the series is about loading arguments: behind `movl $3, %edi` and `movl $4, %esi` stands a single argument-passing convention, and where further arguments go is a question we take back to the experiments there.
