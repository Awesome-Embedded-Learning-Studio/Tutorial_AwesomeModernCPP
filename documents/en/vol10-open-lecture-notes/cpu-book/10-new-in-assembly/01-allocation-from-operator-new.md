---
title: "The Two Steps of new in Assembly: Memory Comes from operator new"
chapter: 10
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
description: "Follow a single new down the library call chain into operator new, with address evidence of the two-step handoff, the forwarding to malloc, and where bad_alloc comes from when allocation fails"
reading_time_minutes: 21
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/10-new-in-assembly/01-allocation-from-operator-new.md
  source_hash: d5f2ef22bbcb1d7461564358a8d2eb0becbc0b3283f6fc8f908b74a1827d1ad1
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# The Two Steps of new in Assembly: Memory Comes from operator new

Back in [the constructor piece in 05](../05-classes-in-assembly/02-constructor-and-destructor-calls.md), we took a two-step snapshot of `new Point(3, 4)`: it opens with `call _Znwm@PLT` and closes with `call _ZN5PointC1Eii`. All we said at the time was that it lands on the machine as two steps, and then we moved on to destructors and vtables. The pieces ahead follow that one call from beginning to end: whose hands the memory comes from, what the constructor does on top of it, where the program veers off when allocation fails, and how the memory is handed back in the end. One snapshot, unfolded into a whole journey.

The route is fixed. We set out from a single new in the source code: the first stop watches it ask the library for memory, then we drop inside the library to see the forwarding layer, then comes the failure detour, and after that delete's symmetric two steps carry the memory back. Once the memory is home there are still two variant stops to make: which step placement new skips, and what new[] adds on top. Two stretches of evidence along the way deserve the most attention. One is the handoff photographed inside a single gdb session: the address operator new returns turns around and becomes the constructor's this, without one digit changed. The other sits in the new[] section's assembly, where we read the element count the compiler writes at the head of the block straight out of a memory window. Every block of assembly, every session, and every line of program output you see in this piece comes verbatim from real runs on my machine, and the examples are original to this piece.

## Our Test Environment

As usual, the environment goes first. Every block of output below can be reproduced on a machine with the same setup:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:44:43 UTC 2026 x86_64 GNU/Linux
```

The debugger is GNU gdb (GDB) 17.2, and the platform is an x86_64 environment on WSL2. We never pass `-std` explicitly; GCC 16's default language standard is gnu++20, and we will run into that default again in the next piece, when the delete symbols come up.

The experiment conditions are the series' usual set. Observed code is compiled at `-O0` throughout, the translate-it-as-written mode we got to know in [01](../01-prefix-vs-postfix-increment/01-int-instructions.md): every statement is turned into instructions just as written, with no cleanup and no folding. The assembly is built with `-fno-asynchronous-unwind-tables` across the board to thin out the noise; we measured in 01 that its effect on C++ is limited, the synchronous `.cfi_*` lines still show up, so when pasting assembly in this piece I deleted the `.cfi_*` lines and the `.LFB` and `.LFE` label lines by hand and did not touch a single instruction. `.LEHB`, `.LEHE`, and the exception tables stay exactly as emitted, because the evidence for the exception paths lives in them, and the assembly on the return leg needs them for directions. The syntax is AT&T, source on the left and destination on the right, register names carry `%`, immediates carry `$`, the series default since 02. Comments starting with `#` in the assembly lines are our interpretation; the instructions are pasted verbatim. Addresses in the sessions are kept as recorded too: gdb turns address randomization off by default, which is why addresses line up across sessions taken in one batch. On another machine you will get different values, and that is normal.

One more prop carried over from 01: the volatile sink. Code nobody consumes gets deleted wholesale at optimization levels, so we hand the result of every allocation to a volatile to be consumed; that is how what we observe stays alive. All the observed assembly in this piece comes from `-O0` builds, the sole exception being one `-O2` comparison in the placement section of the next piece, which we will flag separately when we get there. There the sink looks redundant, and it is kept anyway so that the code stays honest if you carry it verbatim to another optimization level.

C++ symbol names go through name mangling, and this piece keeps running into them, so here is the cast list up front, every entry decoded by an actual `c++filt` run:

| Mangled name | Real name |
|---|---|
| `_Znwm` | `operator new(unsigned long)` |
| `_ZnwmPv` | `operator new(unsigned long, void*)`, the placement version |
| `_Znam` | `operator new[](unsigned long)` |
| `_ZdlPvm` | `operator delete(void*, unsigned long)`, the sized version |
| `_ZdlPvS_` | `operator delete(void*, void*)`, the placement counterpart |
| `_ZdaPv` | `operator delete[](void*)` |
| `_ZdaPvm` | `operator delete[](void*, unsigned long)`, the sized version |
| `_ZN5PointC1Eii` | `Point::Point(int, int)` |
| `_ZN8ResourceC1Ei` | `Resource::Resource(int)` |
| `_ZN8ResourceD1Ev` | `Resource::~Resource()` |
| `_ZN6WidgetC1Ev` | `Widget::Widget()` |
| `_ZN6WidgetD1Ev` | `Widget::~Widget()` |
| `_ZN4Pod3C1Ev` | `Pod3::Pod3()` |

## Allocation: Memory Comes from operator new

Time to bring on the main character. operator new is the allocation function the standard library provides, and its name is exactly what you just read. It does one thing: ask the allocator for a block of memory so many bytes wide, and, if the request is granted, hand the address back in rax. Construction is not its business; initialization is a separate errand the new expression runs right after. Let's photograph both steps with a minimal class:

```cpp
// N1 -- the two steps of new: operator new allocates, then the constructor runs
struct Point {
    int x;
    int y;
    Point(int x_, int y_) : x(x_), y(y_) {}
};

int main() {
    Point* p = new Point(3, 4);
    volatile int sink = p->x + p->y;  // anti-optimization consume
    (void)sink;
    delete p;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S -o n1.s n1.cpp
```

Here is `main`'s assembly; the `#` comments are our own reading:

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	subq	$24, %rsp
	movl	$8, %edi            # first step's argument: sizeof(Point) = 8 bytes
	call	_Znwm@PLT           # first step: operator new(8), ask the allocator for memory
	movq	%rax, %rbx          # return value (block address) saved from rax into callee-saved rbx
	movl	$4, %edx            # constructor argument y = 4
	movl	$3, %esi            # constructor argument x = 3
	movq	%rbx, %rdi          # this = the freshly allocated address (rbx → rdi)
	call	_ZN5PointC1Eii      # second step: Point::Point(this, 3, 4)
	movl	$0, %eax
	movq	%rbx, -24(%rbp)
	testb	%al, %al            # al was just zeroed by the line above, so always zero
	je	.L3                     # always jumps; the 3 lines below are unreachable dead code
	movl	$8, %esi
	movq	%rbx, %rdi
	call	_ZdlPvm@PLT         # (unreachable) cleanup delete if the constructor throws
.L3:
	movq	-24(%rbp), %rax
	movl	(%rax), %edx
	movq	-24(%rbp), %rax
	movl	4(%rax), %eax
	addl	%edx, %eax
	movl	%eax, -28(%rbp)     # sink = p->x + p->y
	movl	-28(%rbp), %eax
	movq	-24(%rbp), %rax
	testq	%rax, %rax          # delete's null-pointer check
	je	.L4
	movl	$8, %esi            # sized delete's size argument
	movq	%rax, %rdi
	call	_ZdlPvm@PLT         # delete p → operator delete(p, 8)
.L4:
	movl	$0, %eax
	movq	-8(%rbp), %rbx
	leave
	ret
```

Start from the first step's argument. What `movl $8, %edi` loads into edi is the 8 that sizeof(Point) works out to. The 3 and the 4 are nowhere in its argument list; they wait for the second step, when they enter esi and edx as constructor arguments. The layout puts the division of labor on display: operator new recognizes nothing but a byte count, and it neither knows nor needs to know what lives inside Point. Only once we hold the address do 3 and 4 follow this into the constructor. The `@PLT` after the call target also deserves a sentence of plain words: PLT is the procedure linkage table, the intermediary every cross-shared-library call passes through under dynamic linking, so `call _Znwm@PLT` means this trip crosses into libstdc++.so.6. As for what sits on the other side, that is the next stop, and we step in to look.

Look twice at the shuffling between the two steps, because plenty of write-ups get vague right here. The pointer's path runs from rax to rbx and then to rdi, with one layover at rbx. `movq %rax, %rbx` moves the allocation result out of rax into rbx for safekeeping, and once the constructor arguments are loaded, `movq %rbx, %rdi` finally delivers it into the first argument register rdi as this. Why the layover at all? Because rax does not survive the next call. It is the return-value register, and it belongs to the caller-saved group: across one call its contents are no longer yours to count on, because the callee is fully entitled to fill it with its own things. rbx belongs to the callee-saved group, where the convention is that whoever uses a register puts it back, so a value parked in rbx crosses a call unharmed. The proof sits in the listing we pasted: after the constructor call returns, main runs `movq %rbx, -24(%rbp)`, the pointer drops out of rbx into a stack slot and becomes p, and everything downstream reads the value kept in rbx. From now on, when you read -O0 assembly on your own and see an allocation result land in rbx, you know what it is doing there: waiting out the next call.

There is a stretch of surplus in the listing, and we had better warn you in advance, otherwise your count of deletes will come out one higher than the source's statements. Right after the constructor call comes `movl $0, %eax`, and the `testb %al, %al` behind it asks whether al is zero. But al was just cleared to zero by the instruction before it, so the test comes out equal every time and `je .L3` jumps away every time; the `call _ZdlPvm@PLT` in between never executes, not once in its life. That is the -O0 temperament: translate literally, clean up nothing. 05 already pointed out this same species of artifact when it covered constructors. The real `delete p` statement is the stretch after `.L3`, the one carrying the `testq` null-pointer check, and we will give it a proper look on the return leg in the next piece. So for reading -O0 new and delete there is one test and one test only: the statement proper is the one with a `testq ptr, ptr` null-pointer check; everything else is aftermath paths the compiler generates by the book, some permanently unreachable, some reached only when an exception flies.

## Inside the Library: malloc Sits Under operator new

So what happens after `call _Znwm@PLT` crosses into libstdc++.so.6? We follow it down with gdb and photograph every layer of the call chain. This piece's experiment is recorded as N2. It has no new source: it takes N1's program and builds it once more as an executable with debug information, using `g++ -O0 -g -o n2 n1.cpp`, with the debug script saved as `n2b.gdb`. The script:

```gdb
set pagination off
set confirm off
break operator new(unsigned long)
run
bt
break malloc
continue
bt
finish
finish
p/x $rax
info registers rdi
quit
```

Write the breakpoint with the full signature, exactly as shown: `break operator new(unsigned long)`. Write just operator new and gdb will give you a hard time. The raw session follows; the `== … ==` divider labels are waymarks we inserted while tidying up the transcript, there to help you keep your place, and every other line is gdb's own output:

```text
$ gdb -q -x n2b.gdb ./n2
Reading symbols from ./n2...
Breakpoint 1 at 0x1030

This GDB supports auto-downloading debuginfo from the following URLs:
  <https://debuginfod.archlinux.org>
Debuginfod has been disabled.
To make this setting permanent, add 'set debuginfod enabled off' to your .gdbinit.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, 0x00007ffff7cb5fe0 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
== bt at operator new entry ==
#0  0x00007ffff7cb5fe0 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
#1  0x000055555555515c in main () at n1.cpp:9
Breakpoint 2 at 0x7ffff78a89a0

Breakpoint 2, 0x00007ffff78a89a0 in malloc () from /usr/lib/libc.so.6
== bt at malloc (below it: operator new) ==
#0  0x00007ffff78a89a0 in malloc () from /usr/lib/libc.so.6
#1  0x00007ffff7cb6005 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
#2  0x000055555555515c in main () at n1.cpp:9
== finish malloc, then finish operator new ==
0x00007ffff7cb6005 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
0x000055555555515c in main () at n1.cpp:9
9	Point* p = new Point(3, 4);
== rax = operator new return value ==
$1 = 0x55555556b020
rdi            0x4                 4
```

That `rdi 0x4` on the last line is the output of the script's `info registers rdi`, pasted as it came. What rdi holds after operator new returns is a leftover 4 from libc's internal work, nothing to do with our claim, but it makes one point worth keeping: register contents after a return do not count as evidence. Whatever you actually want to look at, sample it at the right moment.

Two layers inside the session deserve to be kept apart. The first bt is taken at operator new's entry, and it catches only two frames, itself and main; the call into malloc has not happened yet. To see malloc's frame on the stack we have to set a separate breakpoint on malloc, and this is the spot where drawn call chains most often go wrong. Once the second breakpoint hits, the three-level stack is complete: main called operator new, and operator new called malloc. The frame addresses split between two libraries too: `0x00007ffff7cb...` is an address in libstdc++.so.6, and `0x00007ffff78a...` is one in libc.so.6. In this libstdc++ 16.2 implementation, operator new is a thin forwarding layer: all it takes in is a byte count, and it passes the request straight on to malloc.

We should hedge our wording here, because the standard leaves itself room on this point. The cppreference [operator new page](https://en.cppreference.com/w/cpp/memory/new/operator_new) says, as retrieved on October 13, 2026:

```text
It is unspecified whether library versions of operator new make any calls to
std::malloc or std::aligned_alloc (since C++17).
```

Translated into plain words: whether the library versions of operator new call malloc at all is unspecified in the standard. The forwarding relationship we watched today is an implementation fact of libstdc++ 16.2; swap in another standard library and the picture may well come out differently. So every claim in this piece is pinned to libstdc++'s implementation, and we do not write it up as a rule of the standard.

With the chain clear, we come to the scene this piece most wanted to film: the handoff between the two steps. In the session above, `$1 = 0x55555556b020` is operator new's return value. To catch the whole exchange, we put both breakpoints into a single session so that the address comparison cannot slide into a cross-session coincidence; this run's script is saved as `n2c.gdb`, still loading `./n2`. The raw session:

```text
Breakpoint 1, 0x00007ffff7cb5fe0 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
== frame at operator new ==
#0  0x00007ffff7cb5fe0 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
#1  0x000055555555515c in main () at n1.cpp:9
0x000055555555515c in main () at n1.cpp:9
9	Point* p = new Point(3, 4);
== operator new returned (rax) ==
$1 = 0x55555556b020

Breakpoint 2, Point::Point (this=0x55555556b020, x_=3, y_=4) at n1.cpp:5
5	Point(int x_, int y_) : x(x_), y(y_) {}
== constructor this ==
$2 = (Point * const) 0x55555556b020
$3 = 3
$4 = 4
#0  Point::Point (this=0x55555556b020, x_=3, y_=4) at n1.cpp:5
#1  0x0000555555555171 in main () at n1.cpp:9
```

Between the two breakpoints sits one finish and one call, and the address never moved. In `$1` operator new hands back 0x55555556b020; in `$2` the this the constructor receives is still 0x55555556b020. Same run, same address. The rax-through-rbx-into-rdi route in the previous section's assembly cashed out at runtime as exactly this number. The constructor's bt also shows that its direct caller is the new-expression line in main, with no other frame in between: construction is an ordinary call the compiler inserts in place, and we already saw what that function body looks like when 05 covered the insertion points.

## The Failure Path: When the Allocation Fails

A journey is not all smooth sailing, so we stage a failure on purpose and see where the detour leads. To make an allocation fail, new up a block so large it cannot possibly be granted:

```cpp
// N3 -- the failure path of new: std::bad_alloc
#include <cstdio>
#include <new>

int main() {
    // (size_t)-1 / 2 ≈ 2^63 - 1 bytes, necessarily far beyond any allocation limit
    size_t huge = static_cast<size_t>(-1) / 2;
    try {
        char* p = new char[huge];
        std::printf("allocated?! p=%p\n", static_cast<void*>(p));
        delete[] p;
    } catch (const std::bad_alloc& e) {
        std::printf("caught std::bad_alloc, what(): %s\n", e.what());
        return 1;
    }
    return 0;
}
```

```text
$ g++ -O0 -o n3 n3.cpp && ./n3
caught std::bad_alloc, what(): std::bad_alloc
exit code: 1
```

The choice of value needs a note in advance, because `new char[(size_t)-1]` written directly does not compile; GCC stops it at compile time:

```text
n3.cpp: In function 'int main()':
n3.cpp:5:28: error: size '18446744073709551615' of array exceeds maximum object size '9223372036854775807'
    5 |         char* p = new char[(size_t)-1];
      |                            ^~~~~~~~~~~
```

The diagnostic is blunt about it: the array size exceeds the maximum object size, which is 2 to the 63rd power minus 1. `(size_t)-1` is the largest unsigned number 64 bits can hold, over the line the moment you write it, and so it is rejected at compile time. `(size_t)-1 / 2` goes through integer division, and rounded down it is exactly 2 to the 63rd power minus 1, numerically equal to the maximum object size, so it slips under the line, compiles, and only fails to allocate at runtime. If you run this experiment yourself, take the value exactly this way; the full -1 will not get through the compiler.

Now look at what the exception reports when it shows up. The text `what()` prints is the single word `std::bad_alloc` itself, nothing longer. libstdc++ has no interest in explaining the cause inside that string, and we will not invent an explanation on its behalf. On failure behavior, the cppreference [operator new page](https://en.cppreference.com/w/cpp/memory/new/operator_new) reads:

```text
1-4) Throws an exception of a type that would match a handler of type
std::bad_alloc on failure to allocate memory.

Otherwise, if currently no new-handler is installed, throws std::bad_alloc.
```

On allocation failure it throws an exception that a handler of type `std::bad_alloc` would match, and our catch caught exactly that. Put it back into the two-step frame: the failure lands in the first of the two steps, we never come away with any memory at all, the second step's construction never even starts, and so there is no cleanup work to do either. The new expression also has a non-throwing variant that takes a different fork, and the [new expression page](https://en.cppreference.com/w/cpp/language/new) puts it this way:

```text
If a non-throwing allocation function (e.g. the one selected by
new(std::nothrow) T) returns a null pointer because of an allocation failure,
then the new expression returns immediately, it does not attempt to initialize
an object or to call a deallocation function.
```

When the nothrow variant fails, the allocation function returns a null pointer and the new expression ends on the spot; both the object initialization and the deallocation call are skipped. Line it up against the regular version and the difference is only how failure is reported: one throws an exception, the other hands back a null pointer. In both, a failure in the first step rules out the second, and on that point the two agree.

One more failure we have not met yet: the memory is already in hand and the constructor throws midway. At that point the memory is on our account, and the cleanup can no longer be skipped. You can walk this two-step breakdown by hand. The executor below carries main's full flow, with operator new and operator delete replaced by minimal stubs: the allocator hands back a fixed static buffer from .data, and the deallocator only counts its calls. Everything outside the stubs follows the article's scene: first the allocating call, rbx receives the block address, then the constructing call, and even the dead always-taken exception-cleanup code is kept as printed. Single-step past delete's null check and look at rsi holding 8, the size argument that sized delete carries.

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/new_two_steps.json" />

What the emergency code at that fork looks like is something we will walk right past in the [next piece](02-delete-two-steps-and-placement-new.md).
