---
title: "What Your Class Looks Like in Assembly: Virtual Functions and the vtable"
chapter: 5
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "The vptr makes the object 8 bytes fatter, gdb digs out the Base and Derived vtables, matches the address point against the one table that objects of the same class share, then walks the three steps of a virtual call once more: load the vptr, fetch the slot, indirect call"
reading_time_minutes: 24
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/05-classes-in-assembly/03-virtual-functions-and-vtables.md
  source_hash: 9e16404e03d969dd313919d5c0f830cc36028e91706724e6880be090ed6a9576
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# What Your Class Looks Like in Assembly: Virtual Functions and the vtable

We keep working in exactly the environment the [first piece](01-this-pointer-and-member-offsets.md) set up. The previous two pieces both made the same point, that an object is data plus ordinary function calls; virtual functions are the last substantial thing C++ adds to an object. Let's take that same two-int struct and build it twice, once with no virtual function and once with one:

```cpp
// e4.cc -- object layout with virtual functions: the vptr makes the object bigger
#include <cstdio>

struct Plain {  // no virtual functions
    int x;
    int y;
};

struct Virt {  // one virtual function
    int x;
    int y;

    virtual void poke();

    Virt(int a, int b) : x(a), y(b) {}
};

void Virt::poke() {}

static_assert(sizeof(void*) == 8, "this piece assumes a 64-bit target");
static_assert(sizeof(Plain) == 8, "two ints, packed with no alignment tail");
static_assert(sizeof(Virt) == 16, "vptr(8) + x(4) + y(4)");

int main() {
    std::printf("sizeof(void*) = %zu\n", sizeof(void*));
    std::printf("sizeof(Plain) = %zu\n", sizeof(Plain));
    std::printf("sizeof(Virt)  = %zu\n", sizeof(Virt));
    std::printf("alignof(Virt) = %zu\n", alignof(Virt));

    Virt v(1, 2);
    Virt* volatile vp = &v;  // volatile consumption, so the object can't be optimized away
    return (vp->x + vp->y) - 3;
}
```

```text
$ g++ -O0 -S e4.cc -o e4.s     # the virtual function experiments keep the default flags (unwind tables included)
$ g++ -O0 e4.cc -o e4 && ./e4
sizeof(void*) = 8
sizeof(Plain) = 8
sizeof(Virt)  = 16
alignof(Virt) = 8
exit=0
```

All three `static_assert`s passed at compile time, and the runtime printf reported the same thing again: Plain is 8 bytes, Virt is 16, and the extra 8 bytes are the vptr. The vptr's full name is the virtual table pointer; picture it as an extra pointer field at the head of the object, pointing at the vtable of the class the object belongs to. The vtable, in turn, is an array of function pointers that the compiler generates for every class with virtual functions, registering in order the entry addresses of that class's virtual functions. We'll be leaning on the vptr and the vtable over and over below, so here is the plain-words version to remember: the object stores a pointer, the pointer points at a table, the table stores function addresses.

Where do those extra 8 bytes live, and when do they get written? Let's have the constructor's assembly tell us:

```asm
_ZN4VirtC2Eii:
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
	leaq	16+_ZTV4Virt(%rip), %rdx   # ← address of the vtable symbol plus 16
	movq	-8(%rbp), %rax
	movq	%rdx, (%rax)               # ← vptr written into this+0
	movq	-8(%rbp), %rax
	movl	-12(%rbp), %edx
	movl	%edx, 8(%rax)              # x = a → this+8
	movq	-8(%rbp), %rax
	movl	-16(%rbp), %edx
	movl	%edx, 12(%rax)             # y = b → this+12
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

Set this next to the plain constructor e2 from the previous piece and you can pick out three changes. The first is the new pair of instructions at the top of the function body: `leaq 16+_ZTV4Virt(%rip), %rdx` computes the address of the vtable symbol plus 16, and `movq %rdx, (%rax)` writes it into this+0. The first real job the constructor does on entering is store the vptr, and it comes before anything from the user's initializer list. `_ZTV4Virt` is the mangled name for vtable for Virt; when objdump -C translates the symbol table, that is exactly what it prints. As for the 16 it adds, hold onto that number; the next section verifies it with gdb. The second change is that the offsets of x and y moved from +0 and +4 to +8 and +12; the vptr pushed the members back by 8 bytes. The third is a small matter of notation: AT&T output from binutils 2.47 writes symbol-plus-constant as `16+_ZTV4Virt(%rip)`, with the constant in front. The `_ZTV4Virt+16` form you'll meet in older material means the same thing, so don't take them for two different things when you compare.

The body of the vtable lives in `e4.s` too, and the data definition we want is three lines long:

```asm
	.weak	_ZTV4Virt
	.section	.data.rel.ro.local._ZTV4Virt,"awG",@progbits,_ZTV4Virt,comdat
	.align	8
	.type	_ZTV4Virt, @object
	.size	_ZTV4Virt, 24
_ZTV4Virt:
	.quad	0                       # slot -2: offset-to-top (0 for a complete object)
	.quad	_ZTI4Virt               # slot -1: RTTI typeinfo pointer
	.quad	_ZN4Virt4pokeEv          # slot 0: poke -- the very slot the vptr points at
```

The `.size` line reports 24 bytes, exactly three quads (a quad is an 8-byte data unit, and one `.quad` directive places one of them). The first two slots hold no function pointers. The first slot holds the offset-to-top, which stores the displacement from where the vptr sits to the top of the object; in a complete object its value is 0, and it only earns its keep when multiple inheritance starts moving pointers around. The second slot holds the typeinfo pointer. Typeinfo is the runtime's type information, and `dynamic_cast` and `typeid` both depend on it. The real function pointers start at the third quad, and poke's address is registered there. This also explains the 16 in the constructor: the two slots at the head of the table take 8 bytes each, 8 plus 8 makes 16, and the vptr skips both of them to point straight at the first function pointer.

## Dumping the Whole vtable

In the e4 section we pasted the data definition of Virt's vtable, but that was still a static symbol inside the assembly. This section pulls the Base and Derived tables out at runtime and reads them. It also makes good on the gdb reading method promised at the end of topic 03: once the breakpoint stops, the pointer stored in the object and the slots of the vtable are ours to look at, whichever we please. The material is a small inheritance hierarchy: Base declares two virtual functions, Derived overrides second and adds a new one named third, and we build two Base objects and one Derived object as live specimens:

```cpp
// e5.cc -- vtable dump: objects of the same class share one table
struct Base {
    int tag;

    Base(int t) : tag(t) {}
    virtual const char* first();
    virtual const char* second();
};

const char* Base::first() { return "Base::first"; }
const char* Base::second() { return "Base::second"; }

struct Derived : Base {
    Derived() : Base(2) {}
    const char* second() override;  // overrides only the second one
    virtual const char* third();    // and adds a new one
};

const char* Derived::second() { return "Derived::second"; }
const char* Derived::third() { return "Derived::third"; }

Base b1(1);
Base b2(2);  // same class as b1: should share the same vtable
Derived d;

int main() {
    return (b1.tag + b2.tag + d.tag) - 4;
}
```

```text
$ g++ -O0 -g e5.cc -o e5
$ ./e5; echo "exit=$?"
exit=1
$ objdump -t -C e5 | grep -i 'vtable\|typeinfo'
0000000000000000       O *UND*	0000000000000000              vtable for __cxxabiv1::__class_type_info@CXXABI_1.3
0000000000003da8  w    O .data.rel.ro	0000000000000020              vtable for Base
0000000000003dc8  w    O .data.rel.ro	0000000000000018              typeinfo for Derived
0000000000000000       O *UND*	0000000000000000              vtable for __cxxabiv1::__si_class_type_info@CXXABI_1.3
0000000000002040  w    O .rodata	0000000000000009              typeinfo name for Derived
0000000000003d80  w    O .data.rel.ro	0000000000000028              vtable for Derived
0000000000003de0  w    O .data.rel.ro	0000000000000010              typeinfo for Base
0000000000002049  w    O .rodata	0000000000000006              typeinfo name for Base
```

One thing here is easy to misread, so let's pin it down first: the program's exit code is 1, which is main's return value worked out as 1 plus 2 plus 2 minus 4, a return value chosen on purpose, not a crash. Now for the symbol table. Both the vtables and the typeinfo objects are lying in it, and the sizes check out: the 32 bytes of vtable for Base are exactly the two head slots plus two function slots, and the 40 bytes of vtable for Derived are the capacity for two head slots plus three function slots. Add the load base to the file offsets 0x3da8 and 0x3d80 and you have their runtime addresses, which we'll match up in gdb in a moment.

All three objects are globals, constructed before main even runs, so a breakpoint on main catches them the moment we step in. Here is the session verbatim; I trimmed the tool's opening and closing chatter, and every command and every line of output in between is word for word:

```text
(gdb) set pagination off
(gdb) break main
Breakpoint 1 at 0x1165: file e5.cc, line 27.
(gdb) run
Starting program: /tmp/cpu-book-05/e5
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, main () at e5.cc:27
27	    return (b1.tag + b2.tag + d.tag) - 4;
(gdb) p &'vtable for Base'
$1 = (<data variable, no debug info> *) 0x555555557da8 <vtable for Base>
(gdb) p &'vtable for Derived'
$2 = (<data variable, no debug info> *) 0x555555557d80 <vtable for Derived>
(gdb) info vtbl b1
vtable for 'Base' @ 0x555555557db8 (subobject @ 0x555555558020):
[0]: 0x55555555511a <Base::first()>
[1]: 0x55555555512c <Base::second()>
(gdb) info vtbl b2
vtable for 'Base' @ 0x555555557db8 (subobject @ 0x555555558030):
[0]: 0x55555555511a <Base::first()>
[1]: 0x55555555512c <Base::second()>
(gdb) info vtbl d
vtable for 'Derived' @ 0x555555557d90 (subobject @ 0x555555558040):
[0]: 0x55555555511a <Base::first()>
[1]: 0x55555555513e <Derived::second()>
[2]: 0x555555555150 <Derived::third()>
(gdb) p/x *(void**)&b1
$3 = 0x555555557db8
(gdb) p/x *(void**)&b2
$4 = 0x555555557db8
(gdb) p/x *(void**)&d
$5 = 0x555555557d90
(gdb) p *(void**)&b1 == *(void**)&b2
$6 = true
(gdb) p/x (char*)&'vtable for Base' + 16
$7 = 0x555555557db8
(gdb) quit
```

Three layers of things are worth looking at in this session, and let's start with the most satisfying one. `p *(void**)&b1 == *(void**)&b2` asks gdb straight out: are the pointers stored at the heads of the two objects equal? The answer is `$6 = true`, a verdict in one short line. b1 and b2 are two different objects, and in `info vtbl` their subobject addresses really are different (0x...8020 versus 0x...8030), yet the vtable address is 0x555555557db8 for both. Objects of the same class share one and the same vtable, and every object stores that same address. This also clears up, while we're at it, the most exaggerated part of virtual function overhead: there is one table per class, not one per object.

The second layer is that 16 the last section left hanging. `$1` gives the address of the vtable for Base symbol itself, 0x555555557da8; `$7` works out that this address plus 16 is 0x555555557db8; and in `$3` and `$4`, the vptr stored at the heads of b1 and b2 is exactly 0x555555557db8. Three numbers, one equation: the vptr in the object equals the vtable symbol plus 16, and the constructor's `leaq 16+_ZTV4Virt(%rip)` from the last section now has its runtime confirmation. The vptr does not point at the start of the table; it points at the first function pointer after the offset-to-top and typeinfo slots are skipped. That position has an official name: the address point.

The third layer is how Derived's slots are arranged, and the three lines of `info vtbl d` lay out all three moves. Slot 0 is still `Base::first()`: inheritance carries things over untouched, the derived class copying the base class's entries exactly as they were. Slot 1 has become `Derived::second()`: overriding means swapping the pointer in that one slot for your own function. Slot 2 is the new `Derived::third()`, a function declared in the derived class, and the place it gets appended is the end of the table. Look back at the two symbol sizes once more: 32 bytes against 40, and the slot in that gap is third.

We leave a placeholder here for the vtable layout figure, to be added later: it will show the overall layout of the vtable symbol from the offset-to-top and typeinfo slots through the function slot region, mark the position of the address point, and draw how the vptr at the head of the object relates to that address point, to be filled in by this site's diagram pipeline.

This layout is written down in a specification called the Itanium C++ ABI. We met the word ABI back in topic 03: the binary interface agreement that caller and callee both obey. The Itanium C++ ABI governs the C++ layer specifically; how vtables are arranged and how constructor and destructor symbols get their names are both its business, and what GCC implements on Linux x86-64 is exactly this document. The key lines come from [section 2.5, Virtual Table Layout](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable), retrieved on October 12, 2026. Here are the passages we picked out, starting with the two head slots and the address point:

```text
The offset to top holds the displacement to the top of the object from
the location within the object of the virtual table pointer that
addresses this virtual table, as a ptrdiff_t. It is always present.

In a complete object virtual table, and therefore in all of its
primary base virtual tables, the value of this offset will be zero.

The typeinfo pointer points to the typeinfo object used for RTTI. It
is always present.

The virtual table address point points here, i.e. this is the virtual
table address contained in an object's virtual pointer.
```

Translated: the offset-to-top slot holds the displacement from where the vptr sits to the top of the object, stored as a ptrdiff_t, and it is always present; in a complete object's vtable its value is zero, matching the `.quad 0` we saw in `e4.s`. The typeinfo pointer points at the typeinfo object used for RTTI, and it too is always present, matching that `.quad _ZTI4Virt` line. And the vtable's address point falls right after the typeinfo slot: the address an object's vptr carries is this one, matching the plus 16 that `$7` computed in gdb. Another line in the specification explains why the address point is not the start of the table:

```text
In general, what we consider the address of a virtual table (i.e. the
address contained in objects pointing to a virtual table) may not be
the beginning of the virtual table. We call it the address point of
the virtual table. The virtual table may therefore contain components
at either positive or negative offsets from its address point.
```

In plain words: what we usually call the vtable address, meaning the pointer stored in the object, is not necessarily the beginning of the table. The specification names it the address point, and the table's components can sit at positive offsets from it or at negative ones. That is where the negative numbers in the `e4.s` comments, slot -2 and slot -1, come from. The last two passages govern sharing and ordering:

```text
However, the virtual table pointers within all the objects (instances)
of a particular most-derived class point to the same set of virtual
tables.

The order of the virtual function pointers in a virtual table is
the order of declaration of the corresponding member functions in the
class.
```

The first says that all objects of a particular most-derived class carry vptrs pointing at the same set of vtables, and gdb's `$6 = true` is that sentence made real. The second says the order of the function pointers in a vtable follows the declaration order in the class, and first coming before second was arranged by exactly that rule. Every passage matches what we measured today, line for line. Who says an implementation obeying the spec is invisible? What we saw today is precisely it.

## The Three Steps of One Virtual Call

We have read the tables; now for the call. Everything special about virtual functions is concentrated in one place: where the target address comes from. Let's write a minimal inheritance pair, Base and Derived each supplying their own `who()`, plus a function `call_who` that takes a base-class pointer and calls `who()` through it:

```cpp
// e6.cc -- three steps of a polymorphic call: load vptr → fetch slot → indirect call
#include <cstdio>

struct Base {
    virtual const char* who() { return "Base::who"; }
};

struct Derived : Base {
    const char* who() override { return "Derived::who"; }
};

const char* volatile observed = nullptr;  // volatile consumes the return value, blocking a tail call

__attribute__((noinline)) const char* call_who(Base* p) {
    const char* r = p->who();  // the only call site in the whole program, one copy of machine code
    observed = r;
    return r;
}

int main() {
    Base b;
    Derived d;

    std::printf("call_who(&b) -> %s\n", call_who(&b));
    std::printf("call_who(&d) -> %s\n", call_who(&d));
    return 0;
}
```

One unassuming supporting character in the source needs introducing before we read the assembly. The volatile global `observed` is not decoration. What happens without it? The compiler turns our `return p->who();` into a tail call, the call becomes a jmp, and the call we came to see is gone. The write to observed sits right behind the call, and that seals off the tail-call exit. Here is call_who's assembly:

```asm
_Z8call_whoP4Base:
.LFB4:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%rdi, -24(%rbp)         # p lands on the stack
	movq	-24(%rbp), %rax         # read p back
	movq	(%rax), %rax            # ① load the vptr: the 8 bytes at p+0
	movq	(%rax), %rdx            # ② fetch the slot: vptr+0 = vtable slot [0] → rdx
	movq	-24(%rbp), %rax
	movq	%rax, %rdi              # this as the first argument → rdi
	call	*%rdx                   # ③ indirect call: the target comes from a register, not an immediate
	movq	%rax, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	%rax, observed(%rip)    # volatile consumption, which also pins the tail call shut
	movq	-8(%rbp), %rax
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

Let's identify the three steps one by one. Step ①, `movq (%rax), %rax`: read 8 bytes from the memory p points at into rax. What comes back is the vptr; this step is loading the pointer at the head of the object. Step ②, `movq (%rax), %rdx`: read another 8 bytes into rdx, this time from the memory the vptr points at. What comes back is the function address registered in slot 0 of the vtable; this step is the table lookup. Step ③, `call *%rdx`: the asterisk marks an indirect call, meaning the jump target is not baked into the instruction but is whatever rdx holds at that moment. An ordinary call is a direct jump with the target spelled out, like `call _Z8call_whoP4Base`; a virtual call has to consult the table first and only then jumps. After the three steps, look at `movq %rax, %rdi` again: this still travels in rdi, and the channel for handing the object to the target function is unchanged. Virtual functions did not touch the argument convention e1 established; what changed is only where the target address comes from.

We have the runtime results from both sides:

```text
$ g++ -O0 -S e6.cc -o e6.s
$ g++ -O0 e6.cc -o e6 && ./e6
call_who(&b) -> Base::who
call_who(&d) -> Derived::who
```

It is still the same `call_who`, and both calls land on the same call site, yet two different results come out. Where is the difference hiding? Let's let main's assembly speak (an excerpt; the setup of printf's arguments is omitted, and the instructions are verbatim):

```asm
main:
.LFB5:
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
	leaq	16+_ZTV4Base(%rip), %rax    # b's vptr: Base's table plus 16
	movq	%rax, -24(%rbp)
	leaq	16+_ZTV7Derived(%rip), %rax # d's vptr: Derived's table plus 16
	movq	%rax, -16(%rbp)
	leaq	-24(%rbp), %rax
	movq	%rax, %rdi
	call	_Z8call_whoP4Base          # the same call site, first time
	...
	leaq	-16(%rbp), %rax
	movq	%rax, %rdi
	call	_Z8call_whoP4Base          # the same call site, second time
```

The two `leaq`s each mind their own business: b's vptr gets `_ZTV4Base` plus 16, and d's gets `_ZTV7Derived` plus 16. After that, the two objects are sent, one after the other, into the same `call _Z8call_whoP4Base`; not one executed instruction differs, and the only thing that differs is those 8 bytes at the head of the object. The three-step walkthrough is complete here: at the machine level, polymorphism is one data-driven branch, and the branch condition never goes through a cmp instruction; it is the address stored in the vptr. Base and Derived in this section use implicit constructors, so at -O0 there is no standalone constructor to call, and the compiler writes the vptr into the object right at the point of definition. That cross-checks what the e4 constructor did: the first real job of a constructor on entry is storing the vptr, and whichever kind you write, the vptr gets fixed in at construction time.

Whether this roundabout call is expensive, and what -O2 can do about it, is what the next piece takes up.
