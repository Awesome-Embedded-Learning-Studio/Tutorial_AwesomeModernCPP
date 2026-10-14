---
title: "new[] in Assembly: The Array Cookie, the Construction Loop, and Reverse-Order Destruction"
chapter: 10
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
description: "new[] asks for 8 extra bytes of cookie at the head of the block to store the element count, construction loops forward through the addresses, destruction walks backward from past the end, trivially destructible element types skip the cookie entirely, and we finish by connecting the whole journey"
reading_time_minutes: 8
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/10-new-in-assembly/03-array-new-cookie-and-reverse-destruction.md
  source_hash: 6288e29a641c53eb14b9a0ece10a7576d1c16aa9b34146de7f6b0ce3fb4221ae
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# new[] in Assembly: The Array Cookie, the Construction Loop, and Reverse-Order Destruction

The environment, the tools, and how to read the listings are all the same as in the [first piece](01-allocation-from-operator-new.md), so follow that one if you want to rerun anything. The previous piece kept placement new's call and stripped out the allocation, and of the variants there is one left we haven't looked at.

## The Other Variant: new[], the Array Cookie, and Reverse Destruction

The second variant turns one object into a row of them. The new[] story adds a cookie at the head of the block, a loop that constructs element by element, and destruction that runs in reverse. Let's be precise about what the cookie means here: it is 8 extra bytes the compiler asks for at the head of the array block, holding the element count, and it is how the future delete[] knows how many elements to destroy. Our test material is a Widget that prints:

```cpp
// N6: new[] and delete[] -- per-element construction/destruction and the header cookie
#include <cstdio>

struct Widget {
    int id;
    Widget() : id(0) { std::printf("Widget() ctor\n"); }
    explicit Widget(int i) : id(i) { std::printf("Widget(%d) ctor\n", i); }
    ~Widget() { std::printf("Widget(%d) dtor\n", id); }
};

int main() {
    Widget* w = new Widget[3];
    volatile int sink = w[2].id;
    (void)sink;
    delete[] w;
    return 0;
}
```

There is a mistake of my own hiding in this source, and I should own up to it. The first version only provided `explicit Widget(int)`, and the compile was rejected on the spot. The error, verbatim:

```text
n6.cpp: In function 'int main()':
n6.cpp:11:29: error: no matching function for call to 'Widget::Widget()'
   11 |     Widget* w = new Widget[3];
      |                             ^
  • there are 2 candidates
    • candidate 1: 'Widget::Widget(int)'
      n6.cpp:6:14:
          6 |     explicit Widget(int i) : id(i) { std::printf("Widget(%d) ctor\n", i); }
            |              ^~~~~~~
    • candidate expects 1 argument, 0 provided
    • candidate 2: 'constexpr Widget::Widget(const Widget&)'
```

What new[] has to do is default-construct every element, and with the default constructor missing it simply doesn't compile. When you write your own example, don't leave out the default constructor version.

```text
$ g++ -O0 -o n6 n6.cpp && ./n6
Widget() ctor
Widget() ctor
Widget() ctor
Widget(0) dtor
Widget(0) dtor
Widget(0) dtor
```

The three construction prints come first and the three destruction prints after, and look closely at the order on the destruction side: with ids this identical you can't tell anything from the prints, so we'll read the cursor's direction in the assembly instead. Here is `main`'s assembly, with the constructor and destructor bodies elided:

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%rbx
	subq	$24, %rsp
	movl	$20, %edi           # note: not 12! 3*4 + the 8-byte cookie = 20
.LEHB0:
	call	_Znam@PLT           # operator new[](20)
.LEHE0:
	movq	%rax, %rbx
	movq	$3, (%rbx)          # cookie: the element count 3 goes into the 8 bytes at the head
	movl	$1, %r15d
	leaq	8(%rbx), %r12       # first element's address = block start + 8 (what the new[] expression evaluates to)
	movl	$2, %r13d           # loop counter (starts at 3-1)
	movq	%r12, %r14          # the cursor pointer
	jmp	.L4
.L5:
	movq	%r14, %rdi
.LEHB1:
	call	_ZN6WidgetC1Ev      # element-by-element construction (the loop body)
.LEHE1:
	subq	$1, %r13            # decrement the counter
	addq	$4, %r14            # advance the cursor by sizeof(Widget)
.L4:
	testq	%r13, %r13
	jns	.L5                     # keep going while the counter is >= 0 → 3 iterations in total
	movl	$0, %edx
	leaq	8(%rbx), %rax
	movq	%rax, -56(%rbp)
	testb	%dl, %dl
	je	.L6                     # always taken, dead code starts here
	movl	$20, %esi
	movq	%rbx, %rdi
	call	_ZdaPvm@PLT
.L6:
	movq	-56(%rbp), %rax
	addq	$8, %rax
	movl	(%rax), %eax
	movl	%eax, -60(%rbp)     # sink = w[2].id
	movl	-60(%rbp), %eax
	cmpq	$0, -56(%rbp)
	je	.L7
	movq	-56(%rbp), %rax
	subq	$8, %rax            # w - 8: back to the cookie
	movq	(%rax), %rax        # read out the element count n = 3
	leaq	0(,%rax,4), %rdx    # n * sizeof(Widget)
	movq	-56(%rbp), %rax
	leaq	(%rdx,%rax), %rbx   # one-past-the-end pointer = w + n*4
.L9:
	cmpq	-56(%rbp), %rbx
	je	.L8
	subq	$4, %rbx            # step back from the last element
	movq	%rbx, %rdi
	call	_ZN6WidgetD1Ev      # element-by-element destruction (in reverse)
	jmp	.L9
.L8:
	movq	-56(%rbp), %rax
	subq	$8, %rax
	movq	(%rax), %rax        # read the cookie once more: n
	addq	$2, %rax
	leaq	0(,%rax,4), %rdx    # (n + 2) * 4 = n*4 + 8 = 20, the whole block's size
	movq	-56(%rbp), %rax
	subq	$8, %rax            # the block start (w - 8), not w
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	_ZdaPvm@PLT         # operator delete[](block start, 20)
.L7:
	movl	$0, %eax
	jmp	.L16
.L15:                             # exception landing: if construction throws, destroy only the elements that finished building
	movq	%rax, %r14
	testq	%r12, %r12
	je	.L12
	movl	$2, %eax
	subq	%r13, %rax
	salq	$2, %rax
	leaq	(%r12,%rax), %r13
.L13:
	cmpq	%r12, %r13
	je	.L12
	subq	$4, %r13
	movq	%r13, %rdi
	call	_ZN6WidgetD1Ev
	jmp	.L13
.L12:
	movq	%r14, %r12
	testb	%r15b, %r15b
	je	.L14
	movl	$20, %esi
	movq	%rbx, %rdi
	call	_ZdaPvm@PLT
.L14:
	movq	%r12, %rax
	movq	%rax, %rdi
.LEHB2:
	call	_Unwind_Resume@PLT
.LEHE2:
.L16:
	addq	$24, %rsp
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
	popq	%rbp
	ret
```

The very first instruction is where the cookie shows up. `movl $20, %edi` asks for 20 bytes when three ints plainly occupy only 12, and the extra 8 are the cookie at the head of the block. The first thing to happen once the allocation comes back is `movq $3, (%rbx)`, which writes the element count 3 into that 64-bit word at the head. The `leaq 8(%rbx), %r12` right after computes the address of the array's first element, block start plus 8, stepping over the cookie. Here is the point that is easy to mis-remember: the value the new[] expression evaluates to is not the block start. Where it lands is the block start plus 8. Let's watch this and the cookie with our own eyes in gdb, with the breakpoint on the `volatile int sink = w[2].id;` line:

```text
Breakpoint 1, main () at n6.cpp:13
13	    volatile int sink = w[2].id;
== w and and-w[0] ==
$1 = (Widget *) 0x55555556b028
$2 = (Widget *) 0x55555556b028
== raw memory at w-8 (two giant words) ==
0x55555556b020:	0x0000000000000003	0x0000000000000000
== same as 32-bit words ==
0x55555556b020:	0x00000003	0x00000000	0x00000000	0x00000000
0x55555556b030:	0x00000000	0x00000000
```

w holds 0x55555556b028, and we point the memory window at the 8 bytes in front of it: the first 64-bit word at address 0x55555556b020 is `0x0000000000000003`. The cookie has gone from a noun in the documentation to bytes you can see, living exactly 8 bytes ahead of your array pointer.

The shape of the construction side deserves a look too. It is a real loop, not three unrolled copies of the constructor call. The counter r13d starts at 2, and that 2 is the element count minus one. The loop's exit is guarded by `testq` paired with `jns`, and as long as the counter holds a non-negative value it takes us back to `.L5` for another round. The cursor r14 gains 4 per round, moving toward the element that sits next in memory. All three elements come out of one and the same loop body, and if you new a hundred of them, it is still that same loop body running. Construction follows increasing addresses, front to back the whole way.

On the delete[] side, let's take the steps one at a time. It never keeps a record of the count anywhere. It goes back to the cookie and reads it on the spot: `subq $8` steps back to the head of the block, `movq (%rax), %rax` reads n out, and multiplying by 4 yields the one-past-the-end pointer. The `.L9` loop that follows starts from that one-past-the-end pointer, pulls the cursor back by one element's width each round, calls the destructor `_ZN6WidgetD1Ev`, and keeps retreating until it reaches the start of the array. Construction runs front to back and destruction back to front, the same principle as for objects on the stack, and we watched exactly this ordering on a stack frame when article 05 covered nested scopes. Once everything is destroyed, a final `subq $8` recomputes the block start, the cookie is read one more time for n, the expression n plus 2, times 4, assembles the whole block's size of 20, and the block start and 20 together go to the sized `_ZdaPvm`. Note that this size is the whole block's 20, not the elements' 12. Whatever the allocation asked for is exactly what the release hands back.

Let's also have a glance at what the exception landing zone `.L15` is for: if an exception is thrown partway through construction, it destroys in reverse only the elements that finished constructing, then releases the whole block and hands the job of propagating onward to `_Unwind_Resume`. The count is computed on the spot from what is left in r13, so not one element too many is destroyed and not one too few either.

Is the cookie always there? One controlled experiment settles it. Swap Widget for a struct of the same shape without a user-provided destructor, and the key lines of `main` are these:

```asm
	movl	$12, %edi           # 3*4, no cookie
.LEHB0:
	call	_Znam@PLT           # operator new[](12)
...
	call	_ZN4Pod3C1Ev        # the construction loop is still there (default construction is non-trivial)
...
	movq	%rbx, %rdi
	call	_ZdaPv@PLT          # no cookie → no count read, no destruction loop, unsized _ZdaPv
```

Now swap the element type for one that is trivially destructible and look again: the allocation asks for exactly 12 bytes, the cookie is gone, the destruction loop is gone, and delete[] has degraded to the unsized `_ZdaPv`. Once the principle clicks, none of this is surprising. There is no destructor work to do, so delete[] has no need to know the count, and the cookie's one remaining reason to exist is helping delete[] count how many destructors to run. The original wording from the Itanium C++ ABI, as quoted on cppreference, draws the line between having a cookie and not having one with total clarity. GCC on Linux follows exactly this ABI, and MSVC does the same:

```text
Itanium C++ ABI requires that the array allocation overhead is zero if the
element type of the created array is trivially destructible. So does MSVC.
```

The [new expression page](https://en.cppreference.com/w/cpp/language/new) is also worth a read on the array overhead. We retrieved it on October 13, 2026:

```text
Array allocation may supply unspecified overhead, which may vary from one call
to new to the next, unless the allocation function selected is the standard
non-allocating form. The pointer returned by the new expression will be offset
by that value from the pointer returned by the allocation function. Many
implementations use the array overhead to store the number of objects in the
array which is used by the delete[] expression to call the correct number of
destructors.
```

In plain words: array allocation may carry unspecified overhead, the pointer returned by the new expression is offset from the allocation function's return value by that amount, and many implementations use it to store the element count for delete[] to pick up. The 8-byte offset we measured today and that 3 in gdb are this paragraph's concrete values on this machine.

The cookie's existence also settles an old question in passing: why a pointer from new[] must not be handed to delete. Run w through delete, and the delete site never reads the count, the destruction loop never happens at all, and the pointer it hands to operator delete is w itself, 8 bytes past the real block start. To the allocator that is an address it never handed you in the first place. The two sides' instructions mismatch at every single step, so using them as a matched pair is a hard constraint at the instruction level.

## Connecting the Whole Journey

Starting from one line of source, `new Point(3, 4)`, we can now tell its whole life in one stretch. On the machine it is a relay: operator new crosses into libstdc++ through the PLT, the forwarding code inside libstdc++ hands the job to malloc, the address moves from rax into rbx and then into rdi as this, and the constructor goes to work on that still-raw memory. When its useful life ends, delete lets the destructor go first, and operator delete then returns the block with the size attached. Failure ends one of two ways. If the memory can't be had, bad_alloc is thrown and everything ends at the first step, with construction never entering the picture. If the memory arrives but the constructor throws, the cleanup code in the landing zone returns the memory and lets the unwind continue. And the two variants each touch a different part: placement new kept the call but hollowed out the allocation, while new[] adds, on top of those two steps, a cookie and a row of loop-driven constructions.

If you want to go deeper at the language level, here are a few destinations. The semantics of new and delete, failure handling, and the full treatment of placement new are in [vol1's piece on dynamic memory management](../../../vol1-fundamentals/ch12/02-new-delete.md), and the relationship between destructors and resource management is in [vol1's piece on destructors and resource management](../../../vol1-fundamentals/ch06/03-destructors.md). Within this series, article 05 showed you where this comes from and where constructor calls get inserted, article 09 walked you up the call chain using the unwind tables, and today we filled in the handoff between the two.

For the finish, I'm leaving you the cheapest self-verification there is. Open a gdb session, load the N1 program from the first piece, and write the breakpoint with the full signature, `break operator new(unsigned long)`. When it hits, run `finish`, note the address at that moment with `p/x $rax`, then set a breakpoint with `break Point::Point`, continue, and read this from the breakpoint report. The two values will be exactly the same, just like the `0x55555556b020` in our session. Then run N6 once more, stop on the sink line, point the memory window at the 8 bytes in front of w, and the first 64-bit word there holds 3. With both numbers in hand, the two-step handoff and the block-header cookie are no longer things other people told you. You took the readings yourself.
