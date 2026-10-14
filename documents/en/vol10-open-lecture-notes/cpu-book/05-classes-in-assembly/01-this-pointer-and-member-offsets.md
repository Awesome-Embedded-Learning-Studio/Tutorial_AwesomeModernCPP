---
title: "What Your Class Looks Like in Assembly: The this Pointer and Member Offsets"
chapter: 5
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "A member function and a free function taking the object pointer compile to instruction-for-instruction identical assembly: this rides in rdi, the object holds nothing but member offsets, and memory carries no type tag"
reading_time_minutes: 7
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/05-classes-in-assembly/01-this-pointer-and-member-offsets.md
  source_hash: 6232167ce53378d67f452b4747e103bf1d1c0ec39f3596e8b56a11ce600c6500
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# What Your Class Looks Like in Assembly: The this Pointer and Member Offsets

In the [previous piece](../04-stack-overflow-why-no-crash/01-stack-frame-neighbors.md) we did something dangerous to a stack frame: a negative index wrote past the end of the array, and the program calmly ran to completion anyway. This piece heads back to safe ground and picks up more respectable material: the classes you write every day. What does a dot call like `p.translate(3, 4)` look like in the CPU's eyes? A definition like `Point p{1, 2}`? A polymorphic call like `p->who()`? At the end of 03 we left a thread hanging: when you call a member function, the object's own address travels exactly in the rdi slot, the one we've been calling the this pointer, and we promised back then to recognize it the moment it showed up. The end of 04 gave that promise another nudge. Now we make good on it, and what we're paying out is more than that one item: where this ends up, where the constructor and destructor calls get inserted, and the table of function pointers behind virtual functions. This piece runs all of them through assembly. Once we've been through these, a C++ object holds no magic at the machine level; what's left is a stretch of bytes in memory and a few call instructions doing exactly what they say.

Every example and experiment in this piece is original to this site. Every block of assembly and every debugger session you see here comes verbatim from real runs on my machine.

Environment first, as usual. Besides g++ and gdb, this piece brings in one more tool: objdump, the one that reads the symbol table out of a binary:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux

$ gdb --version | sed -n 1p
GNU gdb (GDB) 17.2

$ objdump --version | sed -n 1p
GNU objdump (GNU Binutils) 2.47
```

The machine is x86-64, and the calling convention carries over from 03: System V AMD64, the one we met there, sends the first three integer arguments through rdi, rsi, and rdx in that order. The experiment conditions are the usual set. Observed functions are all compiled at -O0, `__attribute__((noinline))` goes wherever inlining needs plugging, and volatile variables consume return values and object state, so the optimizer can't delete whole stretches of instructions when nobody reads them. The assembly syntax is AT&T, the series default since 02. One statement of pasting policy, all at once. You met the `.cfi_*` directives in 02: they are stack-unwinding metadata fed to the debugger and they generate no machine code, and this piece keeps them as-is. Comments starting with `#` in the assembly lines are our interpretation; the instructions themselves are pasted verbatim. Where a longer function got excerpted, the text says so, and what occasionally gets dropped are housekeeping lines like `.globl` and `.size`. Not a single instruction was altered, and none of this gets repeated later.

## The this Pointer Sits in rdi

We start from the smallest possible class. `Point` carries two ints and a member function `translate`, and next to it we write a free function `point_translate` that looks deliberately staged: its parameter list spells the object pointer out as the first argument, and its body mirrors `translate` statement for statement. The two do the same job, so the question we ask next is whether the compiler turned them into the same thing:

```cpp
// e1.cc -- member function vs free function; this is the first argument
#include <cstdio>

struct Point {
    int x;
    int y;

    void translate(int dx, int dy) {
        x += dx;
        y += dy;
    }
};

// The free function matching translate parameter for parameter:
// the object address is explicitly the first argument
void point_translate(Point* p, int dx, int dy) {
    p->x += dx;
    p->y += dy;
}

int main() {
    Point p{1, 2};

    p.translate(3, 4);
    point_translate(&p, 5, 6);

    volatile int sink = p.x + p.y;  // consume the object state so the optimizer can't drop it
    (void)sink;

    std::printf("p.x=%d p.y=%d\n", p.x, p.y);
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S e1.cc -o e1.s
$ g++ -O0 -fno-asynchronous-unwind-tables e1.cc -o e1 && ./e1
p.x=9 p.y=12
exit=0
```

Let's check that output first: 1 plus 3 plus 5 is 9, and 2 plus 4 plus 6 is 12, so both functions did their work. Now for the assembly. `Point::translate` compiles to the symbol `_ZN5Point9translateEii`; 02 covered how name mangling works, and the extra `N5Point...E` here is the class name folded into the encoding. Here is the function body:

```asm
_ZN5Point9translateEii:
.LFB2:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	movq	%rdi, -8(%rbp)       # this, from rdi, lands on the stack
	movl	%esi, -12(%rbp)      # dx, from rsi, lands on the stack
	movl	%edx, -16(%rbp)      # dy, from rdx, lands on the stack
	movq	-8(%rbp), %rax
	movl	(%rax), %edx         # x is at this+0
	movl	-12(%rbp), %eax
	addl	%eax, %edx
	movq	-8(%rbp), %rax
	movl	%edx, (%rax)         # x += dx written back to this+0
	movq	-8(%rbp), %rax
	movl	4(%rax), %edx        # y is at this+4
	movl	-16(%rbp), %eax
	addl	%eax, %edx
	movq	-8(%rbp), %rax
	movl	%edx, 4(%rax)        # y += dy written back to this+4
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

The three stores at the top put the answer straight on the table. `movq %rdi, -8(%rbp)` saves rdi's value into a stack slot, and `movl %esi, -12(%rbp)` together with `movl %edx, -16(%rbp)` save rsi and rdx right after. -O0's habit is to move every incoming parameter from its register onto the stack; that's how we identified arguments back in 03. So the origin of all three parameters is out in the open: this lives in rdi, dx in rsi, dy in rdx. A member function has no secret channel for reaching the object. What it receives is the address sitting in rdi, exactly what an ordinary function taking `Point*` would get.

The call sites run on the same order. Here are the instructions for the two calls in `main` (the argument setup for printf and the stack protector are elided; the full text sits at lines 76 through 120 of `e1.s`):

```asm
	leaq	-16(%rbp), %rax      # &p into %rax; p lives at rbp-16
	movl	$4, %edx             # dy = 4   → rdx
	movl	$3, %esi             # dx = 3   → rsi
	movq	%rax, %rdi           # this=&p  → rdi
	call	_ZN5Point9translateEii
	leaq	-16(%rbp), %rax
	movl	$6, %edx             # dy = 6   → rdx
	movl	$5, %esi             # dx = 5   → rsi
	movq	%rax, %rdi           # this=&p  → rdi
	call	_Z15point_translateP5Pointii
```

Look at the shape of the two load sequences: `leaq` computes p's address and sends it into rdi, the arguments 3 and 4 go into esi and edx, and then comes the call. In the source, one line says `p.translate(3, 4)` and the other says `point_translate(&p, 5, 6)`, but the shape they land in, once compiled, is identical. Half of 03's hanging thread is now paid off: this carries no keyword magic. It is the pointer at the head of the parameter list, and it occupies the rdi you already know.

Now the weightier half. Here is the body of `point_translate` as well; give it a rough pass with your own eyes:

```asm
_Z15point_translateP5Pointii:
.LFB3:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	movq	%rdi, -8(%rbp)
	movl	%esi, -12(%rbp)
	movl	%edx, -16(%rbp)
	movq	-8(%rbp), %rax
	movl	(%rax), %edx
	movl	-12(%rbp), %eax
	addl	%eax, %edx
	movq	-8(%rbp), %rax
	movl	%edx, (%rax)
	movq	-8(%rbp), %rax
	movl	4(%rax), %edx
	movl	-16(%rbp), %eax
	addl	%eax, %edx
	movq	-8(%rbp), %rax
	movl	%edx, 4(%rax)
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

Eyeballs stray, so let diff judge. We extracted the two function bodies for a mechanical comparison, and the raw output looks like this:

```text
--- e1.member.asm	2026-10-12 12:04:18.886467048 +0800
+++ e1.free.asm	2026-10-12 12:04:18.888154193 +0800
@@ -1,5 +1,5 @@
-_ZN5Point9translateEii:
-.LFB2:
+_Z15point_translateP5Pointii:
+.LFB3:
 	.cfi_startproc
 	pushq	%rbp
 	.cfi_def_cfa_offset 16
```

The real differences are 2 lines: one function symbol name and the number of a GCC-internal `.LFB` label, and diff boxed them, together with three unchanged context lines, into its 5-line window. Another 23 lines below that are identical word for word. We then flattened the two names (both symbols replaced with FUNC, the `.LFB` numbers erased) and ran diff one more time:

```text
$ diff -u e1.member.norm e1.free.norm
(no output)
```

diff printed not one line, and the exit code is a clean 0. This is the quietest piece of evidence in the whole article: on this build of the compiler, "a member function is an ordinary function that took this" has left the territory of rhetoric, and a single diff command verifies it mechanically. If you want to check it yourself, save the two listings above into files, replace the symbols, and diff again; what you get is the same silence.

## Member Variables: The Object Holds Only Offsets, Not Types

Back to the memory-access instructions in the middle of that function body. `movl (%rax), %edx` reads `(%rax)`, and rax holds this at that moment, so the instruction reads the 4 bytes at this+0, the spot where x sits. `movl 4(%rax), %edx` reads this+4, and that's y. At the machine level, a member variable is a fixed offset counted from the object's address: `x` at +0, `y` at +4, the two ints packed side by side, a layout no different from a C struct.

Turned around, though, the same fact carries another meaning, and it's worth a pause. The memory holding `p` has 8 bytes of capacity in all, and what's stored there is two integers; nowhere in it will you find "I am a Point". The type name, the member names, the access control you wrote in the source: after compilation, none of it is in the object. All of that lives only in the compile-time world, and it is exactly what the compiler uses to compute offsets and check permissions. The CPU at runtime gets an address and a pile of fetch-by-offset instructions. Who does this chunk of memory belong to? Nobody ever asks. This also explains why casting a pointer to the wrong type and dereferencing it sails through at the machine level: the only thing wrong is that the offsets no longer line up. It is the same logic as 04's out-of-bounds write clobbering a neighbor. Memory carries no tag of any type, and the world of types ends at compile time.

One more note, about the syntax flavor. The series' first piece used Intel syntax while this piece defaults to AT&T; if you want the Intel version alongside, compile once more with `-masm=intel`. The same call site in Intel syntax looks like this:

```asm
	lea	rax, -16[rbp]
	mov	edx, 4
	mov	esi, 3
	mov	rdi, rax
	call	"_ZN5Point9translateEii"
```

Same batch of instructions, only with the operands on swapped sides. One version difference deserves a warning: GCC 16 puts quotes around symbol names in Intel mode, which is where the `call "..."` spelling comes from. Older references show no such quotes, so if you meet them, your build isn't wrong.

That covers this and member offsets, both on record. What the constructor and destructor calls that the compiler inserts for you look like is what the next piece takes up.
