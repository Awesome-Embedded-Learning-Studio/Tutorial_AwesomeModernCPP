---
title: "The 7th Argument Goes on the Stack, and the Two Routes a Return Value Takes"
chapter: 3
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Measured live: how the 7th integer argument lands on the stack and why multiple stack arguments line up right to left, two gdb sessions catching the one address the caller writes and the callee reads, then the two routes for return values verified end to end, small values in rax, values over 16 bytes through a hidden pointer"
reading_time_minutes: 10
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/03-arguments-in-registers/02-stack-args-and-return-values.md
  source_hash: ebd9daf8c6007433f3b1e5de25c1b825a1d62c9b0a947f226aef2818c6291384
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# The 7th Argument Goes on the Stack, and the Two Routes a Return Value Takes

The [previous piece](01-integer-args-register-allocation.md) counted arguments up to the 6th, which is exactly where the integer registers run out. This piece picks up from there: what happens when the 7th argument shows up, and which route a return value takes back. The environment is the same one the previous piece used; if you want to rerun anything, follow its setup.

## The 7th Argument Lands on the Stack

We can already guess the answer: it moves to the stack. Let's raise the count to seven arguments:

```cpp
// c3_seven_int.cpp
__attribute__((noinline)) int sum7(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}

volatile int sink = 0;

int main() {
    int r = sum7(1, 2, 3, 4, 5, 6, 7);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S c3_seven_int.cpp -o c3_seven_int.s
```

```asm
_Z4sum7iiiiiii:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	movl	%edx, -12(%rbp)
	movl	%ecx, -16(%rbp)
	movl	%r8d, -20(%rbp)
	movl	%r9d, -24(%rbp)
	... (first six arguments read back and summed, omitted)
	movl	16(%rbp), %eax
	addl	%edx, %eax
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	pushq	$7
	movl	$6, %r9d
	movl	$5, %r8d
	movl	$4, %ecx
	movl	$3, %edx
	movl	$2, %esi
	movl	$1, %edi
	call	_Z4sum7iiiiiii
	addq	$8, %rsp
	movl	%eax, -4(%rbp)
	... (store into sink, omitted)
	leave
	ret
```

Three new moves show up in main's sequence of actions. `pushq $7` pushes the 7th argument onto the stack, and only then comes the loading of the six registers and the `call`; after the return there is a stack-cleaning `addq $8, %rsp` following along. On the callee side the first six arguments still arrive in registers and still spill into their own stack slots as before, but the 7th argument g is read straight out of memory by `movl 16(%rbp), %eax`, addressed with a positive offset from rbp, while the locals sit at negative offsets. The two live on opposite sides of it.

One detail in `pushq $7` is easy to miss: the q suffix marks a 64-bit push. g has type int and is only 4 bytes wide, yet the slot it occupies on the stack is a full 8 bytes, and when the callee reads it back with `movl` only the low 32 bits get consumed. Stack arguments are laid out in 8-byte slots too: however small an argument's type is, it fills one whole slot. We will meet this arrangement again in the next piece, in the part about structs.

Where does the offset 16 come from? Let's derive it with the stack frame knowledge from the last topic. The instant call lands, rsp points at the return address. The callee's first act on entering is `pushq %rbp`, which moves the top of the stack another 8 bytes downward. The `movq %rsp, %rbp` that follows fixes the frame base. So rbp+0 holds the saved old rbp, rbp+8 holds the return address, and the space from rbp+16 on is the first slot of the stack argument area, which is exactly where the 7th argument lives.

We leave a placeholder here for the argument-area stack layout figure, to be added later: it will show the sum7 call site laid out from high addresses down to low ones, that is, the relative positions of the stack argument g, the return address, the saved rbp, and the callee's locals area, to be filled in by this site's diagram pipeline.

`addq $8, %rsp` is also worth noting: cleaning up stack arguments is the caller's job. The moment call returns, main hands those 8 bytes back, and the callee never has to deal with it. That differs from some older conventions where the callee cleans the stack; the System V scheme has the caller do the work.

Right now the stack holds just the lone 7th argument, not enough to reveal how multiple stack arguments arrange themselves, so let's add an 8-argument version. The source takes sum7 and adds one more int h plus an 8 in the argument list; everything else keeps the same shape as sum7. The key stretch of its main looks like this:

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	pushq	$8
	pushq	$7
	movl	$6, %r9d
	movl	$5, %r8d
	movl	$4, %ecx
	movl	$3, %edx
	movl	$2, %esi
	movl	$1, %edi
	call	_Z4sum8iiiiiiii
	addq	$16, %rsp
```

The push order we measured puts `pushq $8` first and `pushq $7` after it, in other words the pushing starts from the rightmost argument. The stack grows toward low addresses, so the earlier a value gets pushed the higher it ends up sitting, and the offsets the callee reads are g at `16(%rbp)` and h at `24(%rbp)`. The two lines where sum8's callee reads its stack arguments are exactly `movl 16(%rbp), %eax` and `movl 24(%rbp), %eax`. The argument further to the right sits at the higher address: that is the right-to-left layout. Here is what the ABI document says on this point:

```text
Once registers are assigned, the arguments passed in memory are
pushed on the stack in reversed (right-to-left) order.
```

Translated: once the registers are assigned, the arguments that travel through memory are pushed on the stack in reverse (right-to-left) order. The document adds a footnote explaining the motive: with the arguments arranged right to left, the address of the leftmost stack argument can be computed exactly at compile time, which is what makes fetching arguments convenient when handling variadic functions (printf and its kind, where the number of arguments is not fixed). One piece of wording deserves care here: what the ABI actually constrains is the final layout of the arguments on the stack, meaning the argument further right sits at the higher address. The order in which the push instructions execute is the implementation's own choice, and GCC's choice happens to match the document's wording, which is why we can use the push order as evidence. If elsewhere you read someone stretching this into a claim that the ABI dictates instruction execution order, that has already gone past what the document means.

We don't do take-my-word-for-it here; let's have gdb catch the one address the caller writes and the callee reads. In the first session we enter sum7 through a source-level breakpoint. By then the prologue is behind us and rbp already holds sum7's frame base, so we can measure the stack with it directly:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break sum7' -ex 'run' -ex 'printf "=== prologue done (rbp is sum7 frame). stack layout: ===\n"' -ex 'printf "[rbp+0x00] saved rbp  = "' -ex 'x/1gx $rbp' -ex 'printf "[rbp+0x08] return addr = "' -ex 'x/1gx $rbp+8' -ex 'printf "[rbp+0x10] 7th arg g  = "' -ex 'x/1dw $rbp+16' -ex 'p g' ./c3_seven_int
Breakpoint 1 at 0x1131: file c3_seven_int.cpp, line 3.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, sum7 (a=1, b=2, c=3, d=4, e=5, f=6, g=7) at c3_seven_int.cpp:3
3	    return a + b + c + d + e + f + g;
=== prologue done (rbp is sum7 frame). stack layout: ===
[rbp+0x00] saved rbp  = 0x7fffffffd098:	0x00007fffffffd0c0
[rbp+0x08] return addr = 0x7fffffffd0a0:	0x0000555555555183
[rbp+0x10] 7th arg g  = 0x7fffffffd0a8:	7
$1 = 7
```

The address rbp+16 is 0x7fffffffd0a8, and the value lying there is 7. In the second session we cross over to the caller's side and put the breakpoint on the address of the call instruction itself. GCC on this machine turns PIE on by default, so the base address where the program loads differs from run to run; set a breakpoint straight at a file offset and gdb reports a Cannot access memory error. The right way is to compute the address as the runtime base plus the offset. Fortunately gdb disables address randomization (ASLR) by default, so the load base stays fixed at 0x555555554000 and the runtime addresses become reproducible. The call's file offset comes from the output of `disassemble main`; here is the relevant stretch:

```text
   0x000000000000115c <+8>:	push   $0x7
   0x000000000000115e <+10>:	mov    $0x6,%r9d
   0x0000000000001164 <+16>:	mov    $0x5,%r8d
   0x000000000000116a <+22>:	mov    $0x4,%ecx
   0x000000000000116f <+27>:	mov    $0x3,%edx
   0x0000000000001174 <+32>:	mov    $0x2,%esi
   0x0000000000001179 <+37>:	mov    $0x1,%edi
   0x000000000000117e <+42>:	call   0x1119 <_Z4sum7iiiiiii>
   0x0000000000001183 <+47>:	add    $0x8,%rsp
```

As the listing shows, call sits at offset +42, the file offset 0x117e, so its runtime address is 0x555555554000 plus that:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break *(0x555555554000+0x117e)' -ex 'run' -ex 'info registers rsp' -ex 'x/1dw $rsp' ./c3_seven_int
Breakpoint 1 at 0x55555555517e
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, 0x000055555555517e in main () at c3_seven_int.cpp:9
9	    int r = sum7(1, 2, 3, 4, 5, 6, 7);
rsp            0x7fffffffd0a8      0x7fffffffd0a8
0x7fffffffd0a8:	7
```

The instant before call executes, rsp still points at 0x7fffffffd0a8, and the value at that address is 7. Put the two sessions side by side and the chain closes: the caller writes 7 at 0x7fffffffd0a8, and what the callee reads out of rbp+16 is the same 7 at the same 0x7fffffffd0a8. Writer and reader use one address, each side navigating by its own offset, so the handoff has no ambiguity in it. In the last topic we watched the return address change hands exactly this way; the arguments in this piece follow the same pattern.

## Return Values: Small Ones in rax, Large Ones by Address

That covers where arguments go; now let's chase the return value. The smallest case needs little explanation, a function that returns an int:

```cpp
// c4a_ret_int.cpp
__attribute__((noinline)) int answer() { return 42; }

volatile int sink = 0;

int main() {
    int r = answer();
    sink = r;
    return 0;
}
```

The core of the callee and of main is one line each; straight to the instructions:

```asm
_Z6answerv:
	... (prologue, omitted)
	movl	$42, %eax
	popq	%rbp
	ret

main:
	... (prologue and subq, omitted)
	call	_Z6answerv
	movl	%eax, -4(%rbp)
	... (store into sink, omitted)
```

`movl $42, %eax` puts the return value into eax, and after ret main's `movl %eax, -4(%rbp)` picks it up immediately. The route is the low 32 bits of rax, the same idea as arguments traveling in edi. Where it gets interesting is once the return value grows, so let's keep going.

Let's have the function return a 24-byte `struct Big { long a, b, c; }`:

```cpp
// c4b_ret_big.cpp
struct Big { long a, b, c; };  // 24 bytes > 16 -> MEMORY class

__attribute__((noinline)) Big make_big(long x) {
    Big b;
    b.a = x;
    b.b = x + 1;
    b.c = x + 2;
    return b;
}

volatile long sink = 0;

int main() {
    Big r = make_big(10);
    sink = r.a + r.b + r.c;  // consume so nothing is optimized away
    return 0;
}
```

The compile command in this section gains one more flag, `-fno-stack-protector`, so that the canary trio (a sentinel value the stack protector plants) stays out of the picture. Without it, the default configuration stuffs main with the `movq %fs:40, %rax` load and the end-of-function check. The core sequence under test stays the same either way; if you are curious, drop the flag and compile a comparison version yourself. What follows is the clean version:

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -fno-stack-protector -S c4b_ret_big.cpp -o c4b_ret_big_nosp.s
```

```asm
_Z8make_bigl:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	-8(%rbp), %rax
	movq	-16(%rbp), %rdx
	movq	%rdx, (%rax)
	movq	-16(%rbp), %rax
	leaq	1(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, 8(%rax)
	movq	-16(%rbp), %rax
	leaq	2(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, 16(%rax)
	nop
	movq	-8(%rbp), %rax
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	leaq	-32(%rbp), %rax
	movl	$10, %esi
	movq	%rax, %rdi
	call	_Z8make_bigl
	movq	-32(%rbp), %rdx
	movq	-24(%rbp), %rax
	addq	%rax, %rdx
	movq	-16(%rbp), %rax
	addq	%rdx, %rax
	movq	%rax, sink(%rip)
	movl	$0, %eax
	leave
	ret
```

At first glance this makes your eyes glaze over, so let's take it apart as three stages of one address being handed along.

The first handoff is in main's hands. `leaq -32(%rbp), %rax` takes the address of r, the receiver on main's stack; r is 24 bytes, so its spot is arranged starting at -32(%rbp) and going down. Then `movl $10, %esi` loads the argument x=10 into esi. Did you notice? The argument no longer travels in edi but in the 2nd integer register, because rdi's spot has been taken by the return value's buffer address: what `movq %rax, %rdi` hands over is exactly r's address. A function returning a large value grows an invisible first argument, and the real first argument gets squeezed over into rsi.

For the second handoff we step into the callee make_big. `movq %rdi, -8(%rbp)` spills the hidden pointer (a hidden pointer the caller passes) into a stack slot, and the whole string of instructions after it is written around that pointer: `movq %rdx, (%rax)` writes a, `movq %rdx, 8(%rax)` writes b, `movq %rdx, 16(%rax)` writes c. All three members go straight into r's space on the caller's stack, and nowhere in the entire sequence is there a 24-byte bulk copy. The return value does not travel a data route through registers; what travels in the register is an address.

The third stage does the wrap-up. `movq -8(%rbp), %rax` puts the hidden pointer into rax unchanged, and ret carries that same address back. Let's use gdb to verify all three stages in one connected run: read rdi when the breakpoint brings us into the function, then catch rax at the point of return with a temporary breakpoint:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break make_big' -ex 'run' -ex 'info registers rdi rsi' -ex 'set $buf = $rdi' -ex 'tbreak *_Z8make_bigl+92' -ex 'continue' -ex 'info registers rax' -ex 'printf "rax = 0x%x, equals hidden ptr? %d\n", $rax, ($rax == $buf)' -ex 'x/3dg $rax' ./c4b_ret_big
Breakpoint 1 at 0x1125: file c4b_ret_big.cpp, line 6.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, make_big (x=10) at c4b_ret_big.cpp:6
6	    b.a = x;
rdi            0x7fffffffd0a0      140737488343200
rsi            0xa                 10
Temporary breakpoint 2 at 0x555555555175: file c4b_ret_big.cpp, line 16.

Temporary breakpoint 2, 0x0000555555555175 in main () at c4b_ret_big.cpp:16
16	    sink = r.a + r.b + r.c;  // consume so nothing is optimized away
rax            0x7fffffffd0a0      140737488343200
rax = 0xffffd0a0, equals hidden ptr? 1
0x7fffffffd0a0:	10	11
0x7fffffffd0b0:	12
```

Read the session: at the entry rdi is 0x7fffffffd0a0, rsi holds 10, rax at the point of return is still 0x7fffffffd0a0, and the `equals hidden ptr?` check comes out 1. The address handed in at the entry is the one ret carries back, and the memory lying there holds exactly 10, 11, 12. The basis for this mechanism is written in the Returning of Values section of the ABI document; here is the original:

```text
2. If the type has class MEMORY, then the caller provides space for
   the return value and passes the address of this storage in %rdi
   as if it were the first argument to the function. In effect, this
   address becomes a "hidden" first argument. ... On return %rax will
   contain the address that has been passed in by the caller in %rdi.
```

Translated: when the type has class MEMORY, the caller provides space for the return value and passes the address of that space in rdi, treated exactly like the first argument to the function; in effect it becomes a hidden first argument. On return, what rax holds is the very address the caller passed in through rdi. What the document states in writing is what we just verified item by item in the assembly and in gdb. As for which return values fall into class MEMORY and under what conditions a value travels in registers, those rules get their own expansion in the next piece on structs. Right now you already hold the two opposite samples for comparison: a small return value (int) puts the data itself in rax, while a large one (over 16 bytes) uses rax to hand back only the address, with the data traveling through memory. This is where we leave integer arguments and return values; the next piece belongs to double and structs.
