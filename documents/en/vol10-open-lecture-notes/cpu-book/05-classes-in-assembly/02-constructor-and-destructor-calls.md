---
title: "What Your Class Looks Like in Assembly: Where Constructor and Destructor Calls Get Inserted"
chapter: 5
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "The compiler plants the constructor call right where the object is defined, new turns out to be two steps (grab memory, then run the constructor), early returns and the normal path share one destructor call at the merge point, and RAII's guarantees rest on this machinery"
reading_time_minutes: 10
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/05-classes-in-assembly/02-constructor-and-destructor-calls.md
  source_hash: 6bc8e042e2c3e459308423e5e1dc068123a1722118906056c53beae971abe5d4
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# What Your Class Looks Like in Assembly: Where Constructor and Destructor Calls Get Inserted

The environment, the tools, and how to read the listings are all the same as in the [previous piece](01-this-pointer-and-member-offsets.md); follow that one if you want to rerun anything. Now that we know our way around `this`, the constructor is next. Plenty of textbooks say "the constructor runs automatically when the object is created". That statement isn't wrong, but the word "automatically" says nothing about what the mechanism actually is. Down in the assembly, it is an ordinary call, one the compiler plants at the spot where the object is defined. Let's verify this with two objects, one living on the stack and one put on the heap with new:

```cpp
// e2.cc -- constructor insertion points: a local object and a new object
struct Point {
    int x;
    int y;

    Point(int a, int b) : x(a), y(b) {}
};

int main() {
    Point p(1, 2);  // (a) local object: allocated on the stack, constructor called directly
    volatile int s1 = p.x + p.y;
    (void)s1;

    Point* q = new Point(3, 4);  // (b) heap object: operator new grabs the memory, then the constructor runs
    volatile int s2 = q->x + q->y;
    (void)s2;

    delete q;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S e2.cc -o e2.s
```

In `main` of `e2.s`, here are the two insertion points (the two key stretches excerpted from lines 50 to 66, with the stack protector lines and the redundant reloads around the volatile consumes left out):

```asm
	leaq	-32(%rbp), %rax      # (a) &p, p lives on the stack at rbp-32
	movl	$2, %edx             # b = 2 → rdx
	movl	$1, %esi             # a = 1 → rsi
	movq	%rax, %rdi           # this=&p → rdi
	call	_ZN5PointC1Eii       # ← the constructor call the compiler inserted

	movl	$8, %edi             # (b) sizeof(Point)=8 → rdi
	call	_Znwm@PLT            # operator new(8): grab 8 bytes of memory, return value in rax
	movq	%rax, %rbx           # stash the new memory's address in rbx
	movl	$4, %edx             # b = 4
	movl	$3, %esi             # a = 3
	movq	%rbx, %rdi           # this=new memory → rdi
	call	_ZN5PointC1Eii       # ← step two: run the constructor on the new memory
```

On the stack route, the spot where the source says `Point p(1, 2);` comes out in assembly as four loads plus one `call _ZN5PointC1Eii`. Nobody has to request p's memory; it simply is the 8 bytes starting at rbp-32 on the stack, and the this pointer the constructor receives points right there. Lay this stretch next to the `p.translate` call site from the previous piece and you'll see that defining an object and calling a function are two spellings of the same thing at the machine level.

The heap route has more to look at. `new Point(3, 4)` is one sentence in the source, yet it comes out of the compiler as two calls. The first lands on `_Znwm@PLT`; `_Znwm` is the mangled name of `operator new(unsigned long)`, and the 8 in the argument position is sizeof(Point). That call fetches 8 bytes of raw memory from the heap, leaves the address in rax, and the value then gets stashed into rbx. The second `call _ZN5PointC1Eii` you've already seen: this now points at the freshly grabbed memory. So new is not one instruction, and it barely qualifies as one action. Its first job is to get memory from the heap, its second is to run the constructor on that memory, and between the two steps that memory is nothing but a few uninitialized bytes. This is also the level where the relationship between new and malloc is clearest: malloc stops at step one, and new walks one step further. If you want to go deeper, [vol1's piece on dynamic memory management](../../../vol1-fundamentals/ch12/02-new-delete.md) covers it in full.

Here is the constructor's own assembly as well; it is short:

```asm
_ZN5PointC2Eii:
.LFB1:
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
	movl	-12(%rbp), %edx
	movl	%edx, (%rax)         # x = a → this+0
	movq	-8(%rbp), %rax
	movl	-16(%rbp), %edx
	movl	%edx, 4(%rax)        # y = b → this+4
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

The arguments come in through rdi, rsi, and rdx and land on the stack as before, and the body does exactly two things: write a to this+0, and write b to this+4. The init list gets no special treatment here; it is just two stores with offsets.

There is one small wrinkle in the symbols worth calling out. You may have noticed that the call sites call `_ZN5PointC1Eii`, while the function body above is labeled `_ZN5PointC2Eii`. The last letter doesn't match. These are the two constructor symbols the ABI defines: C1 is named the complete-object constructor and builds a complete object from scratch, while C2 is named the base-object constructor and builds just the part of a derived-class object that this class contributes. The difference only really shows up under inheritance. Our Point has no virtual base classes, so the two symbols point at identical code, and GCC simply declares them aliases:

```asm
	.weak	_ZN5PointC1Eii
	.set	_ZN5PointC1Eii,_ZN5PointC2Eii
```

So you will meet both names in the assembly, and they point at the same instructions. The destructor's D1 and D2 are arranged the same way.

One more small stretch in main tends to trip readers up, so let's flag it before you run into it. After the new, GCC emitted these lines:

```asm
	movl	$0, %eax
	movq	%rbx, -40(%rbp)
	testb	%al, %al
	je	.L3
	movl	$8, %esi
	movq	%rbx, %rdi
	call	_ZdlPvm@PLT          # this delete is never reached
.L3:
```

`testb %al, %al` is testing whether al is zero, but the instruction right before it just zeroed eax with `movl $0, %eax`, so the test always comes out equal and `je .L3` always jumps. The delete call in between never executes, not once. This is a code-generation artifact of the cleanup path at -O0: the compiler does no dead-code cleanup, so a route that can't logically be reached stays in the output exactly as generated. If you run into it while reading your own disassembly, don't suspect a mistake on your part, and don't spend any time on it. The real delete is `_ZdlPvm`, the sized operator delete, and the 8 in esi is that same sizeof.

## The Destructor: Every Exit Path Has to Pass Through It

The destructor is the mirror image of the constructor, with one extra problem: what happens when return has more than one exit? The rule at the language level is that an object lives to the end of its scope, so on the early-return route the destructor can't be dodged either. Let's verify this with a function that returns early:

```cpp
// e3.cc -- destructor insertion points: the early-return path and the path that falls through to the end
struct Tracer {
    int id;

    explicit Tracer(int i) : id(i) {}
    ~Tracer() {
        volatile int sink = id;  // give the destructor an observable body
        (void)sink;
    }
};

int work(int mode) {
    Tracer t(5);

    if (mode == 1) {
        return 10;  // early return: t still gets destroyed first
    }

    volatile int s = t.id;
    (void)s;
    return 20;  // the normal fall-through return: destroys t
}

int main(int argc, char**) {
    volatile int mode = argc - 1;  // which path we take is only known at runtime
    return work(mode);
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S e3.cc -o e3.s
$ g++ -O0 -fno-asynchronous-unwind-tables e3.cc -o e3
$ ./e3; echo "exit=$?"
exit=20
$ ./e3 1; echo "exit=$?"
exit=10
```

Run it with no argument and we take the normal path, getting return value 20. Run it with an argument and the early return fires, giving return value 10. Here is `work`'s assembly (comments added for the walkthrough):

```asm
_Z4worki:
.LFB6:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%rbx
	subq	$40, %rsp
	.cfi_offset 3, -24
	movl	%edi, -36(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -24(%rbp)
	xorl	%eax, %eax
	leaq	-32(%rbp), %rax
	movl	$5, %esi
	movq	%rax, %rdi
	call	_ZN6TracerC1Ei         # construct t
	cmpl	$1, -36(%rbp)
	jne	.L4
	movl	$10, %ebx              # early return: stash the return value in ebx first
	jmp	.L5                    # ┐ jump to the shared cleanup
.L4:                                   # │
	movl	-32(%rbp), %eax       # │ normal path: the volatile consume of t.id
	movl	%eax, -28(%rbp)       # │
	movl	-28(%rbp), %eax       # │
	movl	$20, %ebx              # │ return value into ebx
.L5:                                   # ┘ both paths arrive here
	leaq	-32(%rbp), %rax
	movq	%rax, %rdi
	call	_ZN6TracerD1Ev         # ← destroy t: the single merge point of both exit paths
	movl	%ebx, %eax
	movq	-24(%rbp), %rdx
	subq	%fs:40, %rdx
	je	.L7
	call	__stack_chk_fail@PLT
.L7:
	movq	-8(%rbp), %rbx
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

There's a detail hiding here that disagrees with plenty of write-ups, so let's get it exactly right. Some say "every return path carries its own destructor call". As a statement about semantics that's fine, but count the calls in the assembly above and there is exactly one physical `call _ZN6TracerD1Ev`, sitting at `.L5`. What GCC does is merge the two exit paths: the early-return branch computes its return value and then flows in through `jmp .L5`, while the normal branch runs all the way down to `.L5` too, and the destructor call sits at the merge point, ending up as one call with two incoming edges. The return value gets stashed in ebx early because eax has other work ahead of it; each path carries its own return value to the merge point, and only after the destructor finishes does it move into eax. So the precise way to put it is: the control flow of every exit path has to pass through the destructor call. The semantics lose nothing at all; it is the shape that shares one call site.

So what do multiple destructor call sites look like, and where do we go to see them? It takes more than one object to tell. Let's move to a version with nested scopes, and fit the destructor with a runtime counter, so that the number of destructor runs each path performs becomes a number we can print:

```cpp
// e3b.cc -- destructor insertion points: nested scopes + early return, countable at runtime
#include <cstdio>

volatile int dtor_calls = 0;

struct Tracer {
    int id;

    explicit Tracer(int i) : id(i) {}
    ~Tracer() {
        volatile int keep = id;  // give the destructor an observable body
        (void)keep;
        dtor_calls = dtor_calls + 1;
    }
};

int work(int mode) {
    Tracer outer(1);
    {
        Tracer inner(2);
        if (mode == 1) {
            return 10;  // early return: this path destroys inner, then outer
        }
    }  // the inner scope ends: destroys inner
    volatile int s = outer.id;
    (void)s;
    return 20;  // normal path: only outer gets destroyed
}

int main(int argc, char**) {
    volatile int mode = argc - 1;
    int rc = work(mode);
    std::printf("work()=%d dtor_calls=%d\n", rc, (int)dtor_calls);
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S e3b.cc -o e3b.s
$ g++ -O0 -fno-asynchronous-unwind-tables e3b.cc -o e3b
$ ./e3b; ./e3b 1
work()=20 dtor_calls=2
work()=10 dtor_calls=2
```

Both paths report a count of 2. One detail in the source needs a word of explanation: the count is written as `dtor_calls = dtor_calls + 1` rather than `++dtor_calls`, because applying ++ to a volatile variable has been deprecated since C++20, the warning GCC raises being `-Wvolatile`, and an explicit assignment is the clean way to write it. Here is `work`'s assembly (comments added for the walkthrough):

```asm
_Z4worki:
.LFB8:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%r12
	pushq	%rbx
	subq	$32, %rsp
	.cfi_offset 12, -24
	.cfi_offset 3, -32
	movl	%edi, -36(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -24(%rbp)
	xorl	%eax, %eax
	leaq	-32(%rbp), %rax
	movl	$1, %esi
	movq	%rax, %rdi
	call	_ZN6TracerC1Ei         # construct outer (rbp-32)
	leaq	-28(%rbp), %rax
	movl	$2, %esi
	movq	%rax, %rdi
	call	_ZN6TracerC1Ei         # construct inner (rbp-28)
	cmpl	$1, -36(%rbp)
	jne	.L4
	movl	$10, %r12d
	movl	$0, %ebx               # ebx=0: remember we took the early return
	jmp	.L5
.L4:
	movl	$1, %ebx               # ebx=1: the normal path
.L5:
	leaq	-28(%rbp), %rax
	movq	%rax, %rdi
	call	_ZN6TracerD1Ev         # ← inner's destructor: early return and end-of-scope merge here
	cmpl	$1, %ebx
	jne	.L7                    # early return: skip the outer-only statements
	nop
	movl	-32(%rbp), %eax
	movl	%eax, -28(%rbp)
	movl	-28(%rbp), %eax
	movl	$20, %r12d
.L7:
	leaq	-32(%rbp), %rax
	movq	%rax, %rdi
	call	_ZN6TracerD1Ev         # ← outer's destructor: both paths pass through it in the end
	movl	%r12d, %eax
	movq	-24(%rbp), %rdx
	subq	%fs:40, %rdx
	je	.L9
	call	__stack_chk_fail@PLT
.L9:
	addq	$32, %rsp
	popq	%rbx
	popq	%r12
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

The two objects buy us two `call _ZN6TracerD1Ev` instructions here, one per object; this is exactly what it looks like when destructor call sites split into several places. inner's destructor sits at `.L5`, and both the early-return `jmp .L5` and the straight fall out of the inner scope converge on it. For the order of construction and destruction, just read the addresses: the two constructor calls operate on rbp-32 and rbp-28, the two destructor calls on rbp-28 and rbp-32, exactly reversed. Construction runs outer first and inner right after it; destruction runs the other way around, inner first and outer last. The rule you learned in C++, "destructors run in the reverse order of constructors", is a question of two addresses' ordering once you are down in the assembly. In this version ebx genuinely becomes the path flag: `cmpl $1, %ebx` plus `jne .L7` decides whether to skip the few outer-only volatile consumes, and the early-return path branches off there, but there is no dodging either of the two destructor calls.

RAII dares to stake resource safety on destructors because of exactly this insertion machinery: whichever exit you leave the scope through, the fall-through exit or the early-return exit, the control flow has to pass through the destructor calls the compiler placed. That is the rule [vol1's piece on destructors and resource management](../../../vol1-fundamentals/ch06/03-destructors.md) states at the language level; today you have seen what it looks like at the instruction level.

That wraps up construction and destruction. In the next piece we put virtual functions into the assembly too, the last piece of real machinery an object carries.
