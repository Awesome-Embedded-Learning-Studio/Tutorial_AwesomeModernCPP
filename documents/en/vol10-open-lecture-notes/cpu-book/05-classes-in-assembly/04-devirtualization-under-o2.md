---
title: "What Your Classes Look Like in Assembly: Devirtualization at -O2"
chapter: 5
order: 4
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "When -O2 can see through a local object's type it turns the virtual call into a direct call with the target written in, and when it can't it compares against candidates and bets, with the fallback indirect call standing as the compiler's own evidence that it knows what it can't see through"
reading_time_minutes: 8
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/05-classes-in-assembly/04-devirtualization-under-o2.md
  source_hash: 209b2f39500a5b1d23dbcd184da005c999d4374a6db1d353d84804cd6e8129bc
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# What Your Classes Look Like in Assembly: Devirtualization at -O2

The environment is still the same set as in the [first piece](01-this-pointer-and-member-offsets.md). A three-step virtual call looks expensive on its face: two extra trips to memory, plus a call that takes a detour. The compiler feels the same way, so -O2 has a family of optimizations aimed squarely at it. The name is devirtualization: turning an indirect virtual call back into a direct one, and when things go well, doing away with the vptr load entirely. Whether a call can be devirtualized depends entirely on whether the compiler can see through the object's dynamic type. We designed a pair of functions that stages both situations:

```cpp
// e7.cc -- -O2 devirtualization: types the compiler can see through vs. can't
#include <cstdio>

struct B {
    virtual int f();
};
struct D : B {
    int f() override;
};

volatile int magic_b = 111;  // volatile: the return value cannot be folded into a constant across calls
volatile int magic_d = 222;
volatile int sink = 0;

// noinline: keeps devirtualization and inlining apart, so the call instruction stays visible in the assembly
__attribute__((noinline)) int B::f() { return magic_b; }
__attribute__((noinline)) int D::f() { return magic_d; }

__attribute__((noinline)) int seen_through() {
    D d;        // a local object, its type sitting right in front of the compiler
    B* p = &d;
    int r = p->f();  // -O2 expectation: a direct call to D::f
    sink = r;
    return r;
}

B* volatile whom = nullptr;  // only known at runtime what it points to

__attribute__((noinline)) int unknown_target() {
    B* p = whom;     // a pointer read out of volatile memory
    int r = p->f();  // -O2 expectation: still an indirect call
    sink = r;
    return r;
}

int main(int argc, char**) {
    B b;
    D d;
    whom = (argc > 1) ? (B*)&b : (B*)&d;  // loaded at runtime

    std::printf("seen_through()   -> %d\n", seen_through());
    std::printf("unknown_target() -> %d\n", unknown_target());
    return 0;
}
```

Two designs in this code keep interference out, and they deserve a word. `f()` is marked noinline; we use it to keep devirtualization and inlining separate, because once a function body gets expanded the call disappears, and the call is exactly what we came to see. The two magic variables carry volatile, so their return values cannot be folded into constants across function boundaries; otherwise the compiler might compute the whole call away. Under -O0, our see-through `seen_through` still shows the standard three steps (excerpt; the stack protector's tail is left out):

```asm
_Z12seen_throughv:
.LFB4:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	leaq	16+_ZTV1D(%rip), %rax    # building d: writing the vptr (the inlined implicit constructor)
	movq	%rax, -24(%rbp)
	leaq	-24(%rbp), %rax
	movq	%rax, -16(%rbp)          # p = &d
	movq	-16(%rbp), %rax
	movq	(%rax), %rax             # load the vptr
	movq	(%rax), %rdx             # fetch the slot
	movq	-16(%rbp), %rax
	movq	%rax, %rdi               # this goes into rdi
	call	*%rdx                    # -O0: indirect call, all three steps present
	movl	%eax, -28(%rbp)
	movl	-28(%rbp), %eax
	movl	%eax, sink(%rip)
	movl	-28(%rbp), %eax
	...
```

Compile the same function at -O2 instead and the picture changes on the spot:

```asm
_Z12seen_throughv:
.LFB17:
	.cfi_startproc
	subq	$24, %rsp
	.cfi_def_cfa_offset 32
	movq	%fs:40, %rdi
	movq	%rdi, 8(%rsp)
	movq	%rsp, %rdi
	call	_ZN1D1fEv               # -O2: direct call to D::f, no vptr load at all
	movl	%eax, sink(%rip)
	movq	8(%rsp), %rdx
	subq	%fs:40, %rdx
	jne	.L7
	addq	$24, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L7:
	.cfi_restore_state
	call	__stack_chk_fail@PLT
	.cfi_endproc
```

Watch what happened: `call *%rdx` became `call _ZN1D1fEv`, the target written straight into the assembly as the symbol for D::f, and the two instructions that loaded the vptr and fetched the slot are gone altogether. Nothing mysterious is going on. d is a local object of this function, p is a pointer to it, and nowhere further along can it be swapped for another type; the compiler proves that p's dynamic type must be D, so the address the vtable lookup produces must be D::f, and why look a known address up in a table at all? The program output goes in the next section, where `seen_through()` reports 222 both times. That is the whole of devirtualization: a virtual call that can be devirtualized is no different from an ordinary call, and the cost of the indirect jump only arises where the compiler cannot pin down the type.

## When GCC Can't See Through, It Bets

The half the compiler can't see through doesn't go this smoothly, and this is where this piece parts ways with quite a few references. Start from `unknown_target` at -O0:

```asm
_Z14unknown_targetv:
.LFB11:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$16, %rsp
	movq	whom(%rip), %rax        # p = whom (only known at runtime)
	movq	%rax, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	(%rax), %rax            # load the vptr
	movq	(%rax), %rdx            # fetch the slot
	movq	-8(%rbp), %rax
	movq	%rax, %rdi              # this goes into rdi
	call	*%rdx                   # indirect call
	movl	%eax, -12(%rbp)
	movl	-12(%rbp), %eax
	movl	%eax, sink(%rip)
	movl	-12(%rbp), %eax
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

p comes out of the volatile variable whom, and at compile time nobody knows whether it points at B or at D, so the three steps stand in line obediently. If you assume it's still these three steps under -O2, one compile on your own GCC will stop you short. Here is the actual output:

```asm
_Z14unknown_targetv:
.LFB24:
	.cfi_startproc
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	movq	whom(%rip), %rdi        # p = whom
	leaq	_ZN1D1fEv(%rip), %rdx   # candidate 1: D::f
	movq	(%rdi), %rax            # load the vptr
	movq	(%rax), %rax            # fetch the slot (the address that will actually be called)
	cmpq	%rdx, %rax
	je	.L13                    # it's D::f, jump to the direct call at .L13
	leaq	_ZN1B1fEv(%rip), %rdx   # candidate 2: B::f
	cmpq	%rdx, %rax
	jne	.L11                    # neither, fall to the fallback indirect call at .L11
	call	_ZN1B1fEv
	movl	%eax, sink(%rip)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
	.p2align 4,,10
	.p2align 3
.L11:
	.cfi_restore_state
	call	*%rax                   # the fallback indirect call: any target the compiler doesn't recognize comes through here
	movl	%eax, sink(%rip)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
	.p2align 4,,10
	.p2align 3
.L13:
	.cfi_restore_state
	call	_ZN1D1fEv
	movl	%eax, sink(%rip)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
	.cfi_endproc
```

Read the instructions in order: the vptr still gets loaded, the real slot still gets fetched, but once the address is in hand the compiler does not call it right away. It compares the address against two candidates one by one. First D::f; a match jumps to `.L13` and its direct call. Then B::f; a match falls straight into the direct call sitting right there. Only when neither matches does it jump to `.L11`, where a `call *%rax` indirect call stands by as the fallback. The technique's name is speculative devirtualization, which in plain words is placing a bet: from the inheritance graph of this translation unit, the compiler sees that the only candidates in sight for B's f are B::f and D::f, so at runtime the target is most likely one of those two. It compares, and a winning bet earns a direct call with the target written in. The stake is small: if the comparison fails, control falls back to the fallback path, whose behavior is identical to the original's to the last bit. The program output below is the proof, and nobody gets hurt:

```text
$ g++ -O2 e7.cc -o e7 && ./e7 && ./e7 extra
seen_through()   -> 222
unknown_target() -> 222
seen_through()   -> 222
unknown_target() -> 111
```

When we run it with no arguments, whom points at d and unknown_target reports 222. Run it with an argument and whom points at b, so it reports 111. The output above came from the default -O2 build. We showed the -O0 assembly earlier: it fetches the real slot through the vptr, and the call's target is decided by the table inside the object no matter what, so -O0 and -O2 behave identically. Win the bet or lose it, what lands in our hands is the same.

What we most want you to look at is that the fallback `call *%rax` in `.L11` is still there. The compiler is clearly willing to bet, so why not keep a direct call and bet all the way? Because it knows what it can't see through: outside this translation unit, other .cc files may define more derived classes of B, and at runtime whom could well point at a type the compiler has never met, in which case the slot holds an address that is neither candidate. The existence of the fallback indirect call is the hardest evidence that the compiler knows it can't see through everything. The branch sits cold and deserted in ordinary runs, and the moment an unfamiliar type arrives it is the only way through. One more boundary while we're at it: this speculation happens inside a single translation unit, and further candidates across units are simply invisible to it.

For a clean version with no speculation, add `-fno-devirtualize-speculatively`:

```text
$ g++ -O2 -fno-devirtualize-speculatively -S e7.cc -o e7-O2-nospec.s
```

```asm
_Z14unknown_targetv:
.LFB24:
	.cfi_startproc
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	movq	whom(%rip), %rdi        # p = whom
	movq	(%rdi), %rax            # load the vptr
	call	*(%rax)                 # slot fetch and indirect call merged: call vtable slot [0]
	movl	%eax, sink(%rip)
	addq	$8, %rsp
	.cfi_def_cfa_offset 8
	ret
	.cfi_endproc
```

Switch speculation off and the comparison ladder disappears entirely, leaving output in textbook form. One detail here is easy to miss and worth spelling out: that final `call *(%rax)` takes a memory operand, and it merges the slot fetch and the call into a single instruction, one step fewer than the -O0 routine of fetching into rdx and then doing `call *%rdx`. Counted strictly, a virtual call at -O0 is three identifiable instructions, the ones we named earlier: load the vptr, fetch the slot, indirect call. -O2 with speculation off compresses that to two: load the vptr, plus the merged indirect call. The instruction count did go down, yet on the essential point, that the target is fetched from memory at runtime, it conceded nothing. If you want to measure the cost of a virtual call in instruction counts, keep the two counting schemes apart: pitting -O0's three steps against the comparison ladder of -O2's default output produces nothing but confusion.

The performance side of virtual functions gets a full set of measurements in the performance volume's piece on [virtual functions and devirtualization](../../../vol6-performance/ch06-cpp-abstraction-cost/06-01-virtual-devirtualization.md), and what it says matches what we watched at the instruction level today: a virtual call that can be devirtualized already costs the same as an ordinary call, and the real expense lives only where the type truly cannot be seen through.

## Objects, Back to Memory and Code

Having walked the whole road, let's close out each question the series opened with, one sentence apiece. this is a pointer in the first slot of a member function's parameter list; on x86-64 Linux it travels in rdi, and once normalized, the assembly of a member function and that of a free function explicitly taking an object pointer leaves an empty diff. Construction and destruction are ordinary calls the compiler plants at the definition point and on the exit paths; new is two steps, take memory from the heap and then run the constructor against that memory; the control flow of every exit path must pass through the destructor, and that is where RAII gets its footing. A virtual function is a vptr at the head of the object plus one function-pointer table shared per class plus a three-step indirect call; -O2 tears the whole thing down when it can see through the type, bets on the likely candidates when it can't, and keeping the fallback indirect call around is the honest part.

For readers who want to go deeper, a few directions. The language-level treatment of classes and objects is in vol1's [class basics](../../../vol1-fundamentals/ch06/01-class-basics.md); the everyday uses of the this pointer are in the same volume's [this and cascading calls](../../../vol1-fundamentals/ch06/06-this-and-cascading.md); virtual functions and polymorphism get a systematic treatment in [virtual functions and polymorphism](../../../vol1-fundamentals/ch08/03-virtual-functions.md). Within this series, pieces 02 and 03 finished the skeleton of ordinary calls and argument passing, and the pieces we've just been through hooked object syntax onto it; next we head for the memory side and look at page tables.

We'll leave you the cheapest self-verification for the finish: compile e7.cc three ways, -O0, -O2, and -O2 plus `-fno-devirtualize-speculatively`, and look at `unknown_target` alone. The first time you'll see the indirect call with all three steps; the second, the comparison ladder plus fallback; the third, the clean two-instruction version. Line the three builds up side by side and the compiler's three attitudes toward the same virtual call are all in front of you.

The two outputs' contrast also moved into the executor. The scene below puts -O0 and -O2 versions of seen_through into one main: in the o0 version you will reach `call *%rdx`, all three steps present, rdx holding the target address fetched from the slot, while in the o2 version all three steps are gone, leaving only a `call` aimed straight at d_poke. Both return 42, and the instructions saved are one single-stepped count away. Symbol names lose their mangling, the vptr points straight at slot 0, the same simplification as the earlier scene.

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/devirt_direct.json" />
