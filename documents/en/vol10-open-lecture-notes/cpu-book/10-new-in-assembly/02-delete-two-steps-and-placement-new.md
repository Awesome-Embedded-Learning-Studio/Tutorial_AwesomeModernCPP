---
title: "delete's Two Steps and placement new: Handing Memory Back and Skipping the Allocation"
chapter: 10
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
description: "In delete's two steps the destructor goes first and operator delete wraps up with the size in hand; when the constructor throws, the landing pad hands the memory back before the unwind continues; placement new keeps the call and removes the substance of the allocation"
reading_time_minutes: 8
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/10-new-in-assembly/02-delete-two-steps-and-placement-new.md
  source_hash: 0d775e9889c2bfd48a03acaa40ecd5886a098f114d4f5a3ab71dbcef807a1588
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# delete's Two Steps and placement new: Handing Memory Back and Skipping the Allocation

The environment, the tools, and how to read the listings are all the same as in the [previous piece](01-allocation-from-operator-new.md); follow that one if you want to rerun anything. The two steps of allocation and construction are already on record from the previous piece; what is left is handing the memory back.

## Handing the Memory Back: delete's Two Steps

The useful life is over, and the memory should go back where it came from. delete's two steps mirror new's two steps, but to see the first one clearly the type needs a non-trivial destructor, so I've swapped in a subject that prints:

```cpp
// N4: delete's two steps -- destructor + operator delete
#include <cstdio>

struct Resource {
    int id;
    explicit Resource(int i) : id(i) { std::printf("Resource(%d) ctor\n", i); }
    ~Resource() { std::printf("Resource(%d) dtor\n", id); }
};

int main() {
    Resource* r = new Resource(7);
    volatile int sink = r->id;
    (void)sink;
    delete r;
    return 0;
}
```

```text
$ g++ -O0 -o n4 n4.cpp && ./n4
Resource(7) ctor
Resource(7) dtor
```

We already know the allocation side's two steps from the previous piece, so let's head straight for the deletion side. Here is `main`'s assembly, with the constructor and destructor bodies cut and the rest pasted verbatim:

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%r13
	pushq	%r12
	pushq	%rbx
	subq	$24, %rsp
	movl	$4, %edi            # sizeof(Resource) = 4
.LEHB0:
	call	_Znwm@PLT           # step one: operator new(4)
.LEHE0:
	movq	%rax, %rbx          # rax → rbx, the pointer stashed away
	movl	$1, %r13d
	movl	$7, %esi            # the argument 7
	movq	%rbx, %rdi          # this
.LEHB1:
	call	_ZN8ResourceC1Ei    # step two: Resource::Resource(this, 7)
.LEHE1:
	movl	$0, %eax
	movq	%rbx, -40(%rbp)
	testb	%al, %al
	je	.L4                     # always taken; the cleanup right after is dead code, never reached
	movl	$4, %esi
	movq	%rbx, %rdi
	call	_ZdlPvm@PLT
.L4:
	movq	-40(%rbp), %rax
	movl	(%rax), %eax
	movl	%eax, -44(%rbp)     # sink = r->id
	movl	-44(%rbp), %eax
	movq	-40(%rbp), %rbx
	testq	%rbx, %rbx          # delete's null-pointer check
	je	.L5
	movq	%rbx, %rdi          # this for the destructor
	call	_ZN8ResourceD1Ev    # step one (deletion side): ~Resource()
	movl	$4, %esi            # the size for sized delete
	movq	%rbx, %rdi          # the pointer argument for operator delete
	call	_ZdlPvm@PLT         # step two (deletion side): operator delete(p, 4)
.L5:
	movl	$0, %eax
	jmp	.L10
.L9:                              # exception landing pad: if the constructor throws, free the memory first, then keep unwinding
	movq	%rax, %r12
	testb	%r13b, %r13b
	je	.L8
	movl	$4, %esi
	movq	%rbx, %rdi
	call	_ZdlPvm@PLT
.L8:
	movq	%r12, %rax
	movq	%rax, %rdi
.LEHB2:
	call	_Unwind_Resume@PLT
.LEHE2:
.L10:
	addq	$24, %rsp
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%rbp
	ret
```

The deletion side's two steps line up neatly before `.L5`: leading off is `call _ZN8ResourceD1Ev`, the destructor call in front, and bringing up the rear is `call _ZdlPvm@PLT`, the call that hands the memory back. The order carries meaning. The destructor reads and writes this very memory (you can see it printing the id), and once the memory has gone back it must not be touched again, so the destructor has to run first. One step further up there is also the `testq %rbx, %rbx` null-pointer check, matching the language rule that deleting a null pointer is a legal no-op. The [operator delete page](https://en.cppreference.com/w/cpp/memory/new/operator_delete) on cppreference puts it this way:

```text
The effects of the default versions are: 1) If ptr is null, does nothing.
Otherwise, reclaims the storage allocated by the earlier call to operator new.
```

If the pointer is null, do nothing; otherwise reclaim the storage allocated by the earlier operator new. This `testq` plus `je` is what the rule looks like once it lands on instructions, and it is also your marker for recognizing the body of a delete statement in -O0 assembly.

There is one symbol-level detail that a lot of material out there is out of date on. The deletion side calls `_ZdlPvm`; decode the mangled name and the real one underneath is `operator delete(void*, unsigned long)`, whose second parameter is the size, and the 4 sitting in esi is exactly that. This is sized deallocation, in the standard since C++14. cppreference's entry reads:

```text
void operator delete ( void* ptr, std::size_t sz ) noexcept; (5) (since C++14)

[__cpp_sized_deallocation] [201309L] (C++14) Sized deallocation
```

GCC 16, in its default gnu++20 mode, emits the sized version whenever the size is known at the delete point. The delete of Point in N1 of the previous piece was this one too, and the 8 in esi was that same sizeof. The unsized `_ZdlPv` only turns up in situations where the size is not available, for example deleting a polymorphic object through a base-class pointer. When you read assembly and meet `_ZdlPvm`, what you should say to yourself is: operator delete plus a size parameter.

One more comparison against N1 in the previous piece, because it also holds evidence of the compiler skipping work. Point's destructor is trivial, so N1's deletion side has no destructor call in it at all; one step of `operator delete` and it is done. The two steps on the deletion side presuppose a non-trivial destructor; a type with a trivial destructor only gets the step after it. The reasoning is the same as in 05, where an implicitly generated constructor does not become a standalone call: the compiler does not emit a call for a destructor with nothing to do.

At the tail of the listing sits one more stretch, `.L9`, and it is the headline evidence of this piece. The paired labels `.LEHB1` and `.LEHE1` bracket the range that got registered in the exception table; if the constructor throws inside that range, the stack-unwinding machinery delivers control to `.L9`, the landing pad. Watch what it does. `testb %r13b, %r13b` tests a flag: r13d was set to 1 right after operator new returned successfully, meaning the memory is in hand and cleanup is owed. So on landing, `call _ZdlPvm@PLT` hands the freshly allocated 4 bytes back, and the `call _Unwind_Resume@PLT` after it keeps the unwind moving upward so an outer catch can take over. Memory allocated by a new-expression does not leak just because the constructor threw; that is the promise at the language level, and what you saw today is its implementation at the instruction level. As for the stack-unwinding machinery behind `_Unwind_Resume`: in [09](../09-manual-backtrace-with-gdb/02-no-frame-pointer-no-symbols.md) we climbed the call chain with the unwind tables, and here the very same set of facilities is what walks the exception back up.

Now look back at that never-reachable `_ZdlPvm` in N1 of the previous piece, and you can name it: it and `.L9` are two generated shapes of the same thing, both there for the case where the constructor throws. Count the `_ZdlPvm` calls in N4's listing and there are exactly three: the stretch before `.L4` is dead cleanup, the stretch before `.L5` is the actual delete statement, and the one inside `.L9` is reached only on an exception. Point's constructor cannot throw, yet GCC, playing strictly by the book at -O0, still emitted the test and branch; the condition it tests was set to a constant zero, so the path is dead. Resource's constructor cannot throw either, so the stretch before `.L4` is dead in the same way, and the cleanup that is genuinely reachable lives in `.L9`. Telling them apart comes back to the same rule as before: recognize the statement body by its `testq` null-pointer check, and recognize the exception path by the `.LEHB` labels.

## The placement new Variant: How the Allocation Disappears

We have come far enough that the two-step framework is standing on its own; now let's watch two variants mess with it. The first one up is placement new, a variant of new: you supply the memory yourself, and the new-expression only runs the construction step on the memory you handed it. The allocation function the standard library pairs with it is `operator new(size_t, void*)`, which takes a byte count and an address. Let's try it with a raw buffer on the stack:

```cpp
// N5: placement new -- construction only, no allocation
#include <new>

struct Point {
    int x;
    int y;
    Point(int x_, int y_) : x(x_), y(y_) {}
};

int main() {
    alignas(Point) char buf[sizeof(Point)];   // raw buffer on the stack
    Point* p = new (buf) Point(3, 4);         // placement new
    volatile int sink = p->x + p->y;          // consume so it can't be optimized away
    (void)sink;
    p->~Point();                              // manual destructor call, no delete
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S -o n5.s n5.cpp
```

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	subq	$40, %rsp
	movq	%fs:40, %rax        # stack canary (tripped by the char buffer on the stack)
	movq	%rax, -24(%rbp)
	xorl	%eax, %eax
	leaq	-32(%rbp), %rax     # the address of buf
	movq	%rax, %rsi          # second argument: the buffer address
	movl	$8, %edi            # first argument: the byte count (still passed, nobody uses it)
	call	_ZnwmPv             # the placement flavor: operator new(size, buf)
	movq	%rax, %rbx
	movl	$4, %edx
	movl	$3, %esi
	movq	%rbx, %rdi
	call	_ZN5PointC1Eii      # construction: this is buf itself
	movl	$0, %eax
	movq	%rbx, -40(%rbp)
	testb	%al, %al
	je	.L6                     # always taken; the dead code starts here
	leaq	-32(%rbp), %rax
	movq	%rax, %rsi
	movq	%rbx, %rdi
	call	_ZdlPvS_            # (unreachable) the placement-paired delete(void*, void*)
.L6:
	movq	-40(%rbp), %rax
	movl	(%rax), %edx
	movq	-40(%rbp), %rax
	movl	4(%rax), %eax
	addl	%edx, %eax
	movl	%eax, -44(%rbp)     # sink
	movl	-44(%rbp), %eax
	movl	$0, %eax
	movq	-24(%rbp), %rdx     # canary check
	subq	%fs:40, %rdx
	je	.L8
	call	__stack_chk_fail@PLT
.L8:
	movq	-8(%rbp), %rbx
	leave
	ret
```

Set this next to N1 from the previous piece and a contradiction seems to jump out. Placement new was supposed to skip the allocation step, yet the assembly still carries one `call _ZnwmPv`, with 8 loaded into edi as usual. We have to take this call apart. First thing: it carries no `@PLT` suffix, which makes it a direct call. A copy of the placement flavor of operator new got compiled into the executable; `nm -C` finds its symbol in the file, and `objdump` lays its body out like this:

```text
00000000000011c7 <_ZnwmPv>:
    11c7:	55                   	push   %rbp
    11c8:	48 89 e5             	mov    %rsp,%rbp
    11cb:	48 89 7d f8          	mov    %rdi,-0x8(%rbp)
    11cf:	48 89 75 f0          	mov    %rsi,-0x10(%rbp)
    11d3:	48 8b 45 f0          	mov    -0x10(%rbp),%rax
    11d7:	5d                   	pop    %rbp
    11d8:	c3                   	ret
```

The whole function holds exactly one effective action: it moves the second argument from the stack back into rax untouched, then returns. The first argument, the 8 that was sitting in edi, gets stored into a stack slot on the way in and is never seen used again. This is an identity function: whatever address goes in, that same address comes out. cppreference's own words:

```text
Such allocation functions are known as "placement new", after the standard
allocation function void* operator new(std::size_t, void*), which simply
returns its second argument unchanged.
```

So the rigorous way to say it: in -O0 assembly, placement new does not reduce to the construction step alone. The call to operator new is still there; what changes is that the function being called does no allocation at all. It hands back exactly the address you gave it, and the constructor goes to work on that address immediately after. What disappears is the substance of asking malloc for memory; what cannot disappear is the form of the call. If you ever explain this to someone, two versions will hold up: say that no allocation happens, or lay the objdump above on the table and walk through the identity function. The one version you must not give is that there is no operator new call in the assembly, because the assembly will contradict you on the spot.

Let's close out the other spots in the listing while we are here. buf is a char array on the stack, so it trips the stack protector's canary; you met that `%fs:40` pair of instructions in 01's environment notes, and it is a normal sight. `p->~Point()` is a manual destructor call, and Point's destructor is trivial, so the assembly carries zero calls for it. The delete that would pair with placement new does not exist in the first place, buf disappears together with the stack frame that holds it, and operator delete has no part anywhere in the story. The `_ZdlPvS_` in the dead-code stretch is the placement-flavored paired deallocation, equally unreachable, and the way to tell is the same as ever.

This example also shows, on the side, why every observation in this series happens at -O0. Compile the same source at -O2 instead and main comes out as this:

```asm
main:
	movl	$7, -4(%rsp)
	movl	-4(%rsp), %eax
	xorl	%eax, %eax
	ret
```

Construction, the addition, and the volatile consume are all computed away at compile time; the 7 in `movl $7` is exactly 3 plus 4. If you want to reproduce any listing in this piece yourself, hold on to the -O0 setting; at -O2 there is nothing left to see. That wraps up the placement new variant. The second variant, new[], keeps the element count in a small header in front of the block, and in the [next piece](03-array-new-cookie-and-reverse-destruction.md) we go read it out.
