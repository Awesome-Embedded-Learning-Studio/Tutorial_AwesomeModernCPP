---
title: "Leaf Functions, Stack Direction, and Call Sites at -O2"
chapter: 2
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard:
  - 11
  - 17
description: "Leaf functions skip the sub rsp thanks to the red zone; addresses from three nested calls prove the stack grows downward; -O2 computes the call site away yet keeps the external definition; take the function's address and even the call site has to stay"
reading_time_minutes: 8
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/02-function-calls-and-stack-frames/02-leaf-functions-stack-direction-and-o2.md
  source_hash: 74b9033d7c8538362733dbd96cabaef5ffdfdde506df7e17743aeaa077fc06d4
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Leaf Functions, Stack Direction, and Call Sites at -O2

When we walked through three in [the previous piece](01-call-and-stack-frame-walkthrough.md), the ending left a loose thread: functions like consume and add that never call anybody else finish with a bare pop rbp. That is where this piece picks up, to see how small the stack frame of a function that calls nothing can get. The assembly in this piece runs in the same environment as [the previous one](01-call-and-stack-frame-walkthrough.md).

## The Leaf Function With No Third Step

A leaf function is a function with not a single call inside it. It calls nothing else, and it sits at the tail end of a call chain. Strip the call out of three and add a volatile variable to catch the sum so it cannot be optimized away, and you get a pure leaf version:

```cpp
int three() {
    int x = 1;
    int y = 2;
    int z = 3;
    volatile int sink = x + y + z;
    return sink;
}

int main() {
    return three();
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S b3_locals.cpp -o b3_locals.s
```

```asm
_Z5threev:
.LFB0:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	$1, -12(%rbp)
	movl	$2, -8(%rbp)
	movl	$3, -4(%rbp)
	movl	-12(%rbp), %edx
	movl	-8(%rbp), %eax
	addl	%eax, %edx
	movl	-4(%rbp), %eax
	addl	%edx, %eax
	movl	%eax, -16(%rbp)
	movl	-16(%rbp), %eax
	popq	%rbp
	ret
.LFE0:
	.size	_Z5threev, .-_Z5threev
```

There is no trace of subq anywhere in the assembly: the prologue is down to the two steps push rbp and mov rbp,rsp, and all four variables (x, y, z, sink) live directly below rbp, at offsets -12, -8, -4 and -16(%rbp). What makes it legal to live there? That is where the red zone from the System V ABI comes in. The System V ABI is the convention manual that C and C++ compilers on Linux both follow: which register carries each argument, how the stack is used, it is all written down in there. The red zone's entry lives in section 3.2.2 of the manual (The Stack Frame), whose source text is in the [x86-64 ABI repository](https://gitlab.com/x86-psABIs/x86-64-ABI). The red zone's rule is that the 128 bytes below rsp belong to the current function for its own use. As long as this function issues no call, nobody else's frame gets pushed down on top of them to overwrite anything, so variables living below rsp are safe, and the sub rsp step is simply dropped. The epilogue simplifies along with it: by that point rsp is already sitting at rbp, so leave's pull-back work would be redundant, and a bare popq %rbp plus ret is enough.

Plenty of tutorials present those three steps from the previous piece (`push rbp`, `mov rbp,rsp`, `sub rsp`) as if every function had them. Disassemble a leaf function yourself and you will find the story does not hold. The accurate statement is this: only a function that calls somebody else has the sub rsp step. A leaf function's -O0 assembly does not have it, and its locals live in the red zone. Take any small function you have on hand; as long as it calls nothing, -O0 will most likely compile it into exactly the shape above.

## The Stack Grows Downward: Address Evidence From Nested Calls

Up to now we have only been saying that things move toward lower addresses. Time to check that against numbers from an actual run. The method is direct: a three-layer chain where main calls outer and outer calls inner, each layer declaring one local variable of its own and printing that variable's address. If the stack really does grow downward the way we said, the deeper the layer, the smaller its address should be.

```cpp
#include <cstdio>

void inner() {
    int i = 0;
    std::printf("inner: &i = %p\n", (void*)&i);
}

void outer() {
    int o = 0;
    std::printf("outer: &o = %p\n", (void*)&o);
    inner();
}

int main() {
    int m = 0;
    std::printf("main:  &m = %p\n", (void*)&m);
    outer();
    return 0;
}
```

```text
$ g++ -O0 b4_nest.cpp -o b4_nest
$ ./b4_nest
$ ./b4_nest
```

```text
main:  &m = 0x7fff5d59f9d4
outer: &o = 0x7fff5d59f9b4
inner: &i = 0x7fff5d59f994
main:  &m = 0x7ffd56d4ae94
outer: &o = 0x7ffd56d4ae74
inner: &i = 0x7ffd56d4ae54
```

The numbers from both runs are laid out above. In the first run, main's m sits at 0x7fff5d59f9d4, outer's o at 0x7fff5d59f9b4, inner's i at 0x7fff5d59f994. One layer deeper and the address drops by 0x20, which comes to 32 bytes. Those 32 bytes are something we can account for: the return address is 8 bytes, the saved rbp is 8 bytes, and the sub $16 local area is 16 bytes, and the total adds up to exactly 32.

Now the second run: all three addresses have moved to the neighborhood of 0x7ffd56d4ae94, nothing like the first run at all. The one doing this is address space layout randomization (ASLR), which hands the stack a different baseline on every run. But no matter where the baseline moves, the deeper-layer-is-smaller ordering has not changed once, and that unchanging ordering is exactly what we are after.

## At -O2 the Call Site Vanishes, the Function Body Stays

Having seen -O0's honest face, let us turn optimization up to -O2 and watch what one function call turns into. The most common change: the call site of a small function disappears outright. We add volatile to the s in the first program, forcing the result to land in memory:

```cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    volatile int s = add(3, 4);
    return s;
}
```

```text
$ g++ -O2 -fno-asynchronous-unwind-tables -S b5a_inline.cpp -o b5a_inline.s
```

```asm
	.file	"b5a_inline.cpp"
	.text
	.p2align 4
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	leal	(%rdi,%rsi), %eax
	ret
.LFE0:
	.size	_Z3addii, .-_Z3addii
	.section	.text.startup,"ax",@progbits
	.p2align 4
	.globl	main
	.type	main, @function
main:
.LFB1:
	movl	$7, -4(%rsp)
	movl	-4(%rsp), %eax
	ret
.LFE1:
	.size	main, .-main
	.ident	"GCC: (GNU) 16.2.1 20260810"
	.section	.note.GNU-stack,"",@progbits
```

main has no call left in it. `add(3, 4)` was computed into 7 at compile time, and the volatile store has to land in memory, so this call left no instruction of its own behind in main. What remains is only the `movl $7, -4(%rsp)` that the volatile store needs, and the `movl -4(%rsp), %eax` right after it, which loads s into eax to serve as the return value. We never even got to inlining: constant folding (evaluating the expression straight to its result at compile time) happens earlier than inline expansion, and it ate the call first. The `-4(%rsp)` spelling differs from the `-4(%rbp)` in the leaf function section by nothing but the base register. main has become a leaf function here too, its variable still lives in the red zone, only the addressing base has switched from rbp to rsp.

Inlining is an optimization where the compiler expands a function's body directly into the call site, which is how the call gets saved. The interesting part is that in this assembly inline never got its turn at all. Now look at the other side: add's body is still there. `_Z3addii` still carries its `.globl` export marker, the body's instructions have been trimmed to a single `leal (%rdi,%rsi), %eax` plus `ret`, and push rbp and the frame are both gone. The reason hides in linkage: add has external linkage, other translation units might reference it, so GCC has to keep the body around. The precise way to say it is this: what -O2 removes is the call site inside main; the function's body has not disappeared, there is simply not one instruction calling it anywhere in the whole output. So "once something is inlined, the function is gone" is saying too much. When you run into claims like that from now on, look twice.

## Take the Function's Address and the Body Must Be Generated

A call site can only vanish when the compiler can see its target clearly. Take the function's address and call through a function pointer instead, and at compile time there is no telling who the pointer points at; the function's body has to be generated. Here is the code:

```cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    int (* volatile fp)(int, int) = add;
    volatile int s = fp(3, 4);
    return s;
}
```

One spot here is easy to get wrong, and it is worth stopping for: the volatile qualifies the pointer itself, and the correct spelling is `int (* volatile fp)(int, int)`. Write it as `volatile int (*fp)(int, int)` instead and the volatile migrates onto the return type, and GCC 16 reports a compile error outright. If you plan to run this one yourself, write the declaration exactly as above.

```text
$ g++ -O2 -fno-asynchronous-unwind-tables -S b5c_fptr.cpp -o b5c_fptr.s
```

```asm
	.file	"b5c_fptr.cpp"
	.text
	.p2align 4
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	leal	(%rdi,%rsi), %eax
	ret
.LFE0:
	.size	_Z3addii, .-_Z3addii
	.section	.text.startup,"ax",@progbits
	.p2align 4
	.globl	main
	.type	main, @function
main:
.LFB1:
	subq	$24, %rsp
	movl	$4, %esi
	movl	$3, %edi
	leaq	_Z3addii(%rip), %rax
	movq	%rax, 8(%rsp)
	call	*8(%rsp)
	movl	%eax, 4(%rsp)
	movl	4(%rsp), %eax
	addq	$24, %rsp
	ret
.LFE1:
	.size	main, .-main
	.ident	"GCC: (GNU) 16.2.1 20260810"
	.section	.note.GNU-stack,"",@progbits
```

Look at main this time: it has opened a 24-byte frame, which it never did in any earlier listing. `leaq _Z3addii(%rip), %rax` computes the function's entry address, `movq %rax, 8(%rsp)` stores it into the volatile function pointer (which must land in memory), and the `call *8(%rsp)` after that is an indirect call: the target address is read out of memory. volatile's semantics forced this round trip through memory, and add's body had to be generated too, because its address has been taken. Take one address, and both the call site and the body are left standing.
