---
title: "Float Arguments, Struct Packing, and Where the ABI Document Comes From"
chapter: 3
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "double arguments draw from a separate register pool starting at xmm0, structs are cut into eightbyte chunks and classified chunk by chunk, Pair rides out in a single rdi while Mixed splits across the integer and vector pools, and the piece closes with the provenance of the System V AMD64 ABI document and the exact wording of the integer register list"
reading_time_minutes: 10
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/03-arguments-in-registers/03-floats-structs-and-abi-docs.md
  source_hash: 7eb559a46ca224d346fa0b4d301f26051798ce2e6b1905b9fc5e8d298e94fffc
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Float Arguments, Struct Packing, and Where the ABI Document Comes From

In this piece we keep experimenting, now with double and struct arguments, and along the way we settle where the document behind all these conventions comes from. The environment is the same one as in [the first piece](01-integer-args-register-allocation.md). Follow its setup if you want to rerun anything.

## Float Arguments Take a Separate Route: Starting at xmm0

Integers get their own registers, and floats get theirs. Let's swap in three doubles:

```cpp
// c5_double.cpp
__attribute__((noinline)) double avg3(double x, double y, double z) {
    return (x + y + z) / 3.0;
}

volatile double sink = 0;

int main() {
    double r = avg3(1.5, 2.5, 3.5);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S c5_double.cpp -o c5_double.s
```

```asm
_Z4avg3ddd:
	pushq	%rbp
	movq	%rsp, %rbp
	movsd	%xmm0, -8(%rbp)
	movsd	%xmm1, -16(%rbp)
	movsd	%xmm2, -24(%rbp)
	movsd	-8(%rbp), %xmm0
	addsd	-16(%rbp), %xmm0
	addsd	-24(%rbp), %xmm0
	movsd	.LC0(%rip), %xmm1
	divsd	%xmm1, %xmm0
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movsd	.LC1(%rip), %xmm1
	movsd	.LC2(%rip), %xmm0
	movq	.LC3(%rip), %rax
	movapd	%xmm1, %xmm2
	movapd	%xmm0, %xmm1
	movq	%rax, %xmm0
	call	_Z4avg3ddd
	movq	%xmm0, %rax
	movq	%rax, -8(%rbp)
	movsd	-8(%rbp), %xmm0
	movsd	%xmm0, sink(%rip)
	movl	$0, %eax
	leave
	ret

	.section	.rodata
	.align 8
.LC0:
	.long	0
	.long	1074266112
	.align 8
.LC1:
	.long	0
	.long	1074528256
	.align 8
.LC2:
	.long	0
	.long	1074003968
	.align 8
.LC3:
	.long	0
	.long	1073217536
```

The callee's spill sequence can be read straight off the listing: x, y, z occupy xmm0, xmm1 and xmm2 in that order, and `movsd` is SSE's double-precision move instruction. The floating-point return value rides xmm0 as well. The result of `divsd` simply stays there, and main then uses `movq %xmm0, %rax` to move it into a general-purpose register before writing it into sink.

How main loads the arguments is worth a closer look. GCC reads the three constants out of `.rodata` into xmm1, xmm0 and rax, then shuffles them into their final positions with two `movapd` and one `movq`. When the shuffling is done, xmm2 holds 3.5, xmm1 holds 2.5 and xmm0 holds 1.5. The constants themselves can be checked by hand. Take .LC3: its high 32 bits, 1073217536, are 0x3FF80000 in hex, and joined with the low 32 bits of 0 they form the 64-bit pattern 0x3FF8000000000000, which decodes to 1.5 under IEEE 754 double precision. .LC2 is the pattern for 2.5, and .LC1 the one for 3.5. Now the gdb measurements:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break avg3' -ex 'run' -ex 'p $xmm0.v2_double[0]' -ex 'p $xmm1.v2_double[0]' -ex 'p $xmm2.v2_double[0]' -ex 'printf "=== general regs rdi/rsi untouched by FP args: ===\n"' -ex 'info registers rdi rsi' ./c5_double
Breakpoint 1 at 0x112c: file c5_double.cpp, line 3.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, avg3 (x=1.5, y=2.5, z=3.5) at c5_double.cpp:3
3	    return (x + y + z) / 3.0;
$1 = 1.5
$2 = 2.5
$3 = 3.5
=== general regs rdi/rsi untouched by FP args: ===
rdi            0x1                 1
rsi            0x7fffffffd1f8      140737488343544
```

xmm0 through xmm2 report 1.5, 2.5 and 3.5, matching the arguments. The two lines below are the more interesting ones: the 1 and the stack address sitting in rdi and rsi are leftovers, and the floating-point arguments never came near a general-purpose register. The two register pools are independent. The integer pool queues its own integers and the vector pool queues its own vectors, and neither draws on the other's slots. The ABI document's exact words on this route are:

```text
If the class is SSE, the next available vector register is used,
the registers are taken in the order from %xmm0 to %xmm7.
```

When the class is SSE (the floating-point vector class), the registers used run from xmm0 to xmm7. The integer quota is six, and the vector quota is eight. Keep the independence of the two pools in mind, because it decides everything in the struct section that follows.

## Small Structs: Packed Eight Bytes at a Time

The arguments so far have all been scalars. What happens when a struct is passed by value? vol1's [Function Basics and Parameter Passing](../../../vol1-fundamentals/c_tutorials/05-function-basics.md) covers the semantics of pass-by-value: the argument is copied, and the function receives the copy. What we are about to watch is how that copy moves at the machine level. The first example is an 8-byte struct of pure integers:

```cpp
// c6_struct_pair.cpp
struct Pair { int a, b; };

__attribute__((noinline)) long pair_sum(Pair p) {
    return p.a + p.b;
}

volatile long sink = 0;

int main() {
    Pair p{3, 4};
    long r = pair_sum(p);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -S -fno-stack-protector -fno-asynchronous-unwind-tables -o c6_pair_nosp.s c6_struct_pair.cpp
```

```asm
_Z8pair_sum4Pair:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movl	-8(%rbp), %edx
	movl	-4(%rbp), %eax
	addl	%edx, %eax
	cltq
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$3, -16(%rbp)
	movl	$4, -12(%rbp)
	movq	-16(%rbp), %rax
	movq	%rax, %rdi
	call	_Z8pair_sum4Pair
	movq	%rax, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	%rax, sink(%rip)
	movl	$0, %eax
	leave
	ret
```

This section's command likewise passes `-fno-stack-protector`, so main carries no canary. The real action is on the caller side, `movq -16(%rbp), %rax` followed by `movq %rax, %rdi`: main lays p's two ints out on the stack (a at -16, b at -12), then reads all 8 bytes out in one `movq` and hands the whole thing to rdi with one more `movq`. The callee is symmetric: `movq %rdi, -8(%rbp)` spills the 8 bytes into the stack in one motion, and only afterwards does `movl` pick them apart at int granularity. The whole transfer is packed as one 8-byte block, and the members never travel separately. gdb's readings put it out in the open:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break pair_sum' -ex 'run' -ex 'info registers rdi' -ex 'printf "rdi low32 (a)  = %d\n", (int)($rdi & 0xffffffff)' -ex 'printf "rdi high32 (b) = %d\n", (int)($rdi >> 32)' -ex 'p p' ./c6_pair
Breakpoint 1 at 0x1141: file c6_struct_pair.cpp, line 5.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, pair_sum (p=...) at c6_struct_pair.cpp:5
5	    return p.a + p.b;
rdi            0x400000003         17179869187
rdi low32 (a)  = 3
rdi high32 (b) = 4
$1 = {a = 3, b = 4}
```

The full value of rdi is 0x400000003, with a=3 in the low 32 bits and b=4 in the high 32 bits, and you can see both ints in one 64-bit register with your own eyes. That is what the packing looks like.

Why pack eight bytes at a time? Because that is the unit the ABI classifies arguments in, and the rules give it a name: eightbyte. Every argument is cut and classified in eight-byte chunks. A struct's size is rounded up to a multiple of 8, each resulting chunk is classified on its own, chunks holding integers, pointers and the like go to class INTEGER and take general-purpose registers, and purely floating-point chunks go to class SSE and take xmm. An aggregate spanning more than two chunks (in the ordinary case) turns into class MEMORY as a whole and goes on the stack. Return values follow the same rule: make_big's 24 bytes fold into three chunks, past the two-chunk limit, so the whole thing lands in MEMORY and comes back through a hidden pointer the caller passes in. The address relay in the middle of [the previous piece](02-stack-args-and-return-values.md) is exactly that. Pair is exactly one chunk in size, the chunk's class is INTEGER, and so a single rdi carries it. Pages 29-30 of the document also carry an official example. The example function receives two integers e and f plus a struct s, where s has type `struct { int a, b; double d; }`. The official assignment puts e in rdi, f in rsi, the first eightbyte of s (a and b packed together) in rdx, and s's double in xmm0. Notice that what the official demonstration shows is the very packing we just measured: `{int, int}` goes into a single general-purpose register, the double is sent off to xmm, the same shape as this piece's Pair and Mixed experiments.

The second example puts the independence of the two pools to use. We change the struct's members: an int plus a double.

```cpp
// c6_struct_mixed.cpp
struct Mixed { int a; double b; };

__attribute__((noinline)) double mixed_sum(Mixed m) {
    return m.a + m.b;
}

volatile double sink = 0;

int main() {
    Mixed m{5, 6.5};
    double r = mixed_sum(m);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -S -fno-stack-protector -fno-asynchronous-unwind-tables -o c6_mixed_nosp.s c6_struct_mixed.cpp
```

```asm
_Z9mixed_sum5Mixed:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, %eax
	movq	%xmm0, %rdx
	movq	%rax, -16(%rbp)
	movq	%rdx, -8(%rbp)
	movl	-16(%rbp), %eax
	pxor	%xmm1, %xmm1
	cvtsi2sdl	%eax, %xmm1
	movsd	-8(%rbp), %xmm0
	addsd	%xmm1, %xmm0
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movl	$5, -32(%rbp)
	movsd	.LC0(%rip), %xmm0
	movsd	%xmm0, -24(%rbp)
	movl	-32(%rbp), %edx
	movq	-24(%rbp), %rax
	movl	%edx, %edi
	movq	%rax, %xmm0
	call	_Z9mixed_sum5Mixed
	movq	%xmm0, %rax
	movq	%rax, -8(%rbp)
	movsd	-8(%rbp), %xmm0
	movsd	%xmm0, sink(%rip)
	movl	$0, %eax
	leave
	ret

.LC0:
	.long	0
	.long	1075445760
```

Mixed's layout in memory has a occupying bytes 0 to 3, padding bytes 4 to 7, and b bytes 8 to 15 (where the padding comes from is covered in vol1's [Structures and Memory Alignment](../../../vol1-fundamentals/c_tutorials/12-struct-and-memory-alignment.md)). Cut into two eightbytes, here is where each one goes:

| Eightbyte | Bytes covered | Contents | Class | Destination |
|---|---|---|---|---|
| Chunk 1 | 0–7 | a plus padding | INTEGER | rdi (written as edi) |
| Chunk 2 | 8–15 | b | SSE | xmm0 |

One argument split into two halves, each heading for its own pool. The caller-side assembly does exactly that: `movl -32(%rbp), %edx` picks up a, `movq -24(%rbp), %rax` picks up b, and then `movl %edx, %edi` and `movq %rax, %xmm0` each load their own share. On entry the callee takes one half with `movl %edi, %eax` and the other with `movq %xmm0, %rdx`, reassembling its own 16-byte local. The gdb run matches:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break mixed_sum' -ex 'run' -ex 'info registers edi' -ex 'printf "rdi low32 (a) = %d\n", (int)($rdi & 0xffffffff)' -ex 'p $xmm0.v2_double[0]' -ex 'p m' ./c6_mixed
Breakpoint 1 at 0x114c: file c6_struct_mixed.cpp, line 5.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, mixed_sum (m=...) at c6_struct_mixed.cpp:5
5	    return m.a + m.b;
edi            0x5                 5
rdi low32 (a) = 5
$1 = 6.5
$2 = {a = 5, b = 6.5}
```

edi reports 5 and xmm0 reports 6.5, consistent with the arguments. One detail here is easy to misread, so it gets a paragraph of its own: the caller writes only the low 32 bits of edi, and the padding bytes corresponding to rdi's high 32 bits are never transmitted at all. Read rdi's full value in gdb and what you see in the high bits is a leftover from earlier code. That is not GCC cutting corners. The ABI document's exact words on this point are:

```text
When a value of a type of class INTEGER is returned or passed in a
register or on the stack, the excess bits that would not be present
in the memory representation of the type (see figure 3.1) are
unspecified.
```

Restated in plain terms: when a value of class INTEGER is passed in a register or on the stack, the surplus bits beyond the type's in-memory representation are unspecified. The high bits were never inside the agreement, and the only bits the receiver may consume are the valid ones. This is also why we picked Pair to show the packing evidence: in an all-int 8-byte packing the high bits are valid data too, so one rdi in gdb displays both members at once. Mixed's high bits are leftovers, and holding up its full register value as packing evidence would mislead.

## Where These Conventions Are Written Down, and Who Honors Them

After measuring all the way here, the document the rdi-through-r9 list comes from deserves its introduction: it is the System V AMD64 ABI, a document maintained jointly by the various vendors, and its formal title is `System V Application Binary Interface, AMD64 Architecture Processor Supplement (With LP64 and ILP32 Programming Models)`. The version I checked is Version 1.0, dated March 12, 2025, 154 pages in full, with the editors listed as H.J. Lu and others. The parameter-passing rules are in section 3.2.3 Parameter Passing, pages 27 to 29 of the PDF. The document's maintenance repository lives at [gitlab.com/x86-psABIs/x86-64-ABI](https://gitlab.com/x86-psABIs/x86-64-ABI), and the repository has since switched to maintaining LaTeX sources. The first time I went looking, I grabbed the raw link for x86-64-ABI.pdf directly and got a 404 for my trouble. Only afterwards did I discover that the usable PDF hangs off the CI build artifacts the README points to: [abi.pdf](https://gitlab.com/x86-psABIs/x86-64-ABI/-/jobs/artifacts/master/raw/x86-64-ABI/abi.pdf?job=build). We checked on October 12, 2026. The PDF build tracks the repository's latest commit on master, dated 2025-03-12, which agrees with the title-page date.

As for the integer list's exact wording, [the first piece](01-integer-args-register-allocation.md) should have quoted it back where it laid out the allocation table, and here is the original at last:

```text
If the class is INTEGER, the next available register of the sequence
%rdi, %rsi, %rdx, %rcx, %r8 and %r9 is used
```

When the class is INTEGER, the register used is the next still-unused one from the sequence rdi, rsi, rdx, rcx, r8, r9. Everything these three pieces measured is this one sentence plus its companion rules put into practice: the six-argument allocation, the point where stack arguments kick in, the splitting of Mixed. GCC on Linux, the compiler we have been measuring with all along, follows this sentence when it generates calling code. Every argument load we watched in the earlier pieces was code produced from it.

And these conventions were never tied to the hardware to begin with. Windows x64 uses Microsoft's own calling convention, with a register list that does not line up. The experiments in this piece do not cover it, and if you have to deal with it, Microsoft's own documentation is where to look. Take the debugging scenario: on Linux, gdb shows the first integer argument living in rdi. Move to a debugger on Windows and it is no longer in rdi. The method of this piece carries over. The register names do not. Don't assume register destinations from source code either. For ordinary functions there is nothing to assume and no reason to assume it. The places that genuinely have to deal with register assignment are inline assembly, naked functions and similar edge-of-the-language constructs, and cross-platform code there falls back on conditional compilation for each side.

## Filling the Argument Area Back into the Stack Frame Diagram

Finally we return to the stack frame diagram from the previous topic. That diagram had the saved old rbp (rbp+0), the return address (rbp+8), and the locals area at negative offsets. The part above rbp was left empty, and we remember the promise at the end of that piece: where additional arguments go, with the answer already sitting in the experiments before this one. Now it can be filled in. On the positive-offset side, the stack argument area starts at rbp+16: the 7th argument occupies the first slot and the 8th the second, arguments further to the right sit at higher addresses, the area works by the caller pushing the values in and the callee reading them back at the agreed offsets, and the stack cleanup after the call returns belongs to the caller as well. On the negative-offset side live the argument copies the callee spilled in and the local variables, their values poured over from the six integer slots of rdi, rsi and company. The two sides of one rbp divide the work this way, and with the jump skeleton from the previous topic plus the loading and unloading of these pieces, the full picture of one complete call is assembled.

One more thing we leave for later in the series. You have already met rdi today as the first-argument position. When we reach the piece on member functions, we will meet it again: calling a member function puts the object's own address in this position, under the name of the this pointer. Bring today's way of reading into gdb then, and the moment it shows up you will recognize it.
