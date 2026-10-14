---
title: "i++ vs ++i: The Copy Cost of Class Types"
chapter: 1
order: 2
tags: [host, cpp-modern, intermediate]
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/01-prefix-vs-postfix-increment/02-class-type-cost.md
  source_hash: e0246537a7fe848a6c442f53f74b9f33307ef9558875e24f9054d91b2e05f1d0
  translated_at: '2026-10-14'
  engine: manual
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "With -O2 having leveled instruction count and cost for the int value-used case, a hand-rolled iterator counts the copies a class-type postfix increment makes: the one the semantics demands is unavoidable, and the data movement is scrubbed clean at -O2"
reading_time_minutes: 10
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# i++ vs ++i: The Copy Cost of Class Types

We keep working in the exact same environment as [the previous article](01-int-instructions.md). That article counted out exactly two differences: when the value is used, postfix pays one extra instruction to stash the old value; when the value is discarded, the two spellings are identical instruction for instruction.

## Turning On -O2: Where Did the Difference Go

Now we turn the optimizer on. The source holds four functions, one pair per scenario: value used and value discarded. Two volatile stores let us observe both x and i, and they also pin down the order of the two stores:

```cpp
volatile int sink;

// Scenario 1: the incremented value is taken (x is used), and the new value of i is used too
void post_val(int i) {
    int x = i++;
    sink = x;
    sink = i;
}

void pre_val(int i) {
    int x = ++i;
    sink = x;
    sink = i;
}

// Scenario 2: the incremented value is discarded; all we care about is that i changed
void post_disc(int i) {
    i++;
    sink = i;
}

void pre_disc(int i) {
    ++i;
    sink = i;
}

int main() {
    post_val(41);
    pre_val(41);
    post_disc(41);
    pre_disc(41);
    return 0;
}
```

```bash
g++ -Wall -Wextra -O2 -fno-asynchronous-unwind-tables -S a3.cpp -o a3.s
```

Scenario 1, the value-used pair (AT&T syntax):

```asm
_Z8post_vali:
	leal	1(%rdi), %eax
	movl	%edi, sink(%rip)
	movl	%eax, sink(%rip)
	ret

_Z7pre_vali:
	addl	$1, %edi
	movl	%edi, sink(%rip)
	movl	%edi, sink(%rip)
	ret
```

And scenario 2, the discard pair (AT&T syntax):

```asm
_Z9post_disci:
	addl	$1, %edi
	movl	%edi, sink(%rip)
	ret

_Z8pre_disci:
	addl	$1, %edi
	movl	%edi, sink(%rip)
	ret
```

The discard pair settles at a glance: `post_disc` and `pre_disc` are byte-for-byte identical. With no consumer of the old value anywhere in the dataflow, -O2 simply treats the whole thing as `i = i + 1`, and the two spellings collapse into the same function.

The value-used pair deserves a closer look. Each version is three instructions, but the sequences differ. In the postfix version, `leal` computes the new value into eax and leaves the old value untouched in edi, so the stores can go old-then-new in the order the semantics demand. In the prefix version, `addl` rewrites edi in place, and both stores then write the same value. Line the costs up side by side: the instruction count matches, the number of memory accesses matches, the register usage matches. The two sides cost the same. The sequences, though, cannot be made to match, because the semantics genuinely differ: x is old on one side and new on the other, and the order of the two volatile stores is forced by the semantics. Within that frame the compiler can only lay out its registers one way: postfix uses lea to carry the new value in a second register, prefix rewrites in place with add. This shape is where the difference finally settles. It is the same root cause as the extra instruction at -O0, except the cost has now been squeezed down to zero.

So the phrase "exactly the same" belongs to the discard scenario alone. The accurate statement for the value-used scenario is: instruction count and cost agree, while the sequence differs.

How is the optimizer allowed to go this far? The rule it invokes is called as-if: the standard only requires that the program's observable behavior (the number and order of volatile accesses, terminal output, and so on) stay unchanged, and the intermediate steps are the compiler's to arrange. In the discard scenario the old value has no observable consumer at all, so stashing it is a purely internal move and can be erased outright; in the dataflow, the two spellings become one and the same thing. In the value-used scenario the old value must be written into sink, so it has an observable use and the stash cannot be skipped, but the shape can be the cheapest one on offer: leave the original register alone, never touch memory, and compute the new value into a different register.

One extra phenomenon shows up here: at -O2, main constant-folds all four calls away, and the four known results turn straight into six volatile stores without a single called function ever running. Constant folding means the compiler finishes at compile time whatever can be finished. We will meet its temper again in the counter experiment later on.

## Where the Folklore Comes From: Postfix on Class Types Really Does More Work

The int experiment found no performance gap, so the old rule of thumb has to come from somewhere. It comes from class types. C++ lets us overload `operator++`, and the semantics the language fixes for postfix is to return the pre-increment old value, which for a class type must be a genuinely constructed object. Here is how cppreference describes the built-in case:

> ```text
> The result of postfix increment or decrement is the value obtained by
> applying the lvalue-to-rvalue conversion to expression (before modification).
> The type of the result is the cv-unqualified version of the type of expression.
> ```

Read plainly: the result of a postfix increment is the value read out before the modification. The Notes on that same page carry one more line that bears directly on this article:

> ```text
> Because a temporary copy of the object is constructed during post-increment
> and post-decrement, pre-increment or pre-decrement operators are usually
> more efficient in contexts where the returned value is not used.
> ```

Because postfix has to construct a temporary copy of the object, pre-increment is usually more efficient in contexts where the returned value goes unused. Notice the word they picked: usually. It speaks of typical cost, not of a guarantee the standard enforces. Whether that sentence still holds today, and how far it holds, is what we will measure ourselves.

## Hand-Rolling an Iterator to Count the Copies

We write a class shaped like an iterator, holding a bare pointer inside, and hang a static counter off the copy constructor to count real invocations. The postfix spelling is the classic textbook shape; you have seen it in print:

```cpp
#include <cstdio>

// A class shaped like an iterator: a bare pointer inside.
// The copy constructor carries a static counter that tracks real calls.
struct Iter {
    int* p;
    static int copies;

    Iter() : p(nullptr) {}
    explicit Iter(int* q) : p(q) {}
    Iter(const Iter& other) : p(other.p) { ++copies; }

    Iter& operator++() {   // prefix: returns a reference to self after the increment
        ++p;
        return *this;
    }

    Iter operator++(int) { // postfix: returns a copy of the old value from before the increment
        Iter old(*this);
        ++p;
        return old;
    }
};

int Iter::copies = 0;

// noinline/noclone: under -O2 these stop the two functions from being
// inlined or clone-specialized, so the assembly evidence lands in standalone
// function bodies and the counter reads in main cannot be constant-folded away.
__attribute__((noinline, noclone)) void run_prefix(Iter& it) {
    ++it;
}

__attribute__((noinline, noclone)) void run_postfix(Iter& it) {
    it++;
}

int main() {
    int arr[4] = {10, 20, 30, 40};
    Iter it(arr);

    run_prefix(it);
    std::printf("prefix  : copies=%d\n", Iter::copies);

    int before = Iter::copies;
    run_postfix(it);
    std::printf("postfix : delta_copies=%d\n", Iter::copies - before);
    std::printf("p offset: %ld\n", static_cast<long>(it.p - arr));
    return 0;
}
```

A few design choices here need spelling out. The int in `operator++(int)` is a dummy parameter: it exists purely to separate the postfix overload from the prefix one at the signature level, it is passed zero at the call site, and it has no other job. `noinline` forbids inlining (inlining is the compiler pasting the callee's body straight into the call site, saving the call and return overhead), and `noclone` forbids the compiler from cloning the function body for specialized calls. Together, the two attributes keep the evidence inside standalone function bodies, where the optimizer cannot stir it into the caller. Let's run it and read the counter:

```bash
g++ -Wall -Wextra -O0 a4.cpp -o a4_O0 && ./a4_O0
```

```text
prefix  : copies=0
postfix : delta_copies=1
p offset: 2
```

The prefix path copies zero times, the postfix path copies once, and the pointer advanced two elements across the two calls in total (offset 2), so the behavior is sound. Now into the assembly to confront it. The copy constructor looks like this (`_ZN4IterC2ERKS_` demangles to `Iter::Iter(const Iter&)`; AT&T syntax):

```asm
_ZN4IterC2ERKS_:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	-16(%rbp), %rax
	movq	(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, (%rax)
	movl	_ZN4Iter6copiesE(%rip), %eax
	addl	$1, %eax
	movl	%eax, _ZN4Iter6copiesE(%rip)
	nop
	popq	%rbp
	ret
```

The first half moves the source object's pointer into the new object; the last three lines bump the counter. Now the prefix version, `operator++()` (AT&T syntax):

```asm
_ZN4IterppEv:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	(%rax), %rax
	leaq	4(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, (%rax)
	movq	-8(%rbp), %rax
	popq	%rbp
	ret
```

Not a single `call` in sight: the pointer advances by 4 (the width of one int), and that is the whole story. Next, the postfix version, `operator++(int)` (AT&T syntax):

```asm
_ZN4IterppEi:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movl	%edx, -20(%rbp)
	movq	-16(%rbp), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	_ZN4IterC1ERKS_
	movq	-16(%rbp), %rax
	movq	(%rax), %rax
	leaq	4(%rax), %rdx
	movq	-16(%rbp), %rax
	movq	%rdx, (%rax)
	nop
	movq	-8(%rbp), %rax
	leave
	ret
```

Here we meet a new player. When a function returns a small object by value, the C++ calling convention has the caller carve a return-value slot on the stack and slip its address into rdi; the real this falls back to rsi, and the dummy int parameter takes edx. So the rdi in this function does not hold a parameter: it holds the address of the return slot. Let's tabulate what this body does:

| Instruction | What it does |
|---|---|
| `movq %rdi, -8(%rbp)` | address of the return slot lands on the stack |
| `movq %rsi, -16(%rbp)` | this lands on the stack |
| `movl %edx, -20(%rbp)` | the dummy parameter lands on the stack |
| the four instructions from `movq -16(%rbp), %rdx` through `movq %rax, %rdi` | set up the arguments: rsi carries this, rdi carries the return slot |
| `call _ZN4IterC1ERKS_` | copy construction, and the object being constructed is the return slot |
| the five instructions starting at `movq -16(%rbp), %rax` | load this, add 4 to the pointer, write it back |
| `movq -8(%rbp), %rax` plus `leave` plus `ret` | put the return-slot address in rax and return |

Count it up: exactly one call in the postfix body, zero in the prefix body, and that matches the runtime's 0-versus-1 from both directions.

Let's also glance at the callers. The two wrapper functions look like this at -O0 (AT&T syntax):

```asm
_Z10run_prefixR4Iter:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movq	%rdi, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	%rax, %rdi
	call	_ZN4IterppEv
	nop
	leave
	ret

_Z11run_postfixR4Iter:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movq	%rdi, -24(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	leaq	-16(%rbp), %rax
	movq	-24(%rbp), %rcx
	movl	$0, %edx
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	_ZN4IterppEi
	nop
	movq	-8(%rbp), %rax
	subq	%fs:40, %rax
	je	.L9
	call	__stack_chk_fail@PLT
.L9:
	leave
	ret
```

There is nothing to say about `run_prefix`: it shuffles the reference parameter aside and forwards to the prefix version. The most eye-catching lines in `run_postfix` are the ones around `%fs:40`: they are the stack canary (a sentinel value the stack protector plants) already mentioned in [the previous article](01-int-instructions.md)'s environment list. On entry, the function reads a sentinel from `%fs:40` and parks it next to the top of the stack; if the comparison before exiting finds a mismatch, it calls `__stack_chk_fail` and takes the process down. Its job is catching stack-smashing-style corruption and has nothing to do with incrementing; seeing the real thing here is all you need. You can also spot `leaq -16(%rbp), %rax` right before the call: the caller carves the return-value slot on the stack and puts its address in rdi before making the call, which lines up with the rdi usage we just saw inside the function body.

The textbook says two copies and we counted one. Where did the other go? By the textbook script, `Iter old(*this)` is one construction and `return old;` copies once more. But the target of that call in the assembly is the return slot itself: `movq -8(%rbp), %rax` fetches the slot's address into rdi, old lives in the return slot from beginning to end, and the return path has nothing left to do. This is NRVO, named return value optimization. The standard grants permission for it: when returning a named local object, that object's construction may land directly in the return slot, and the copy on return disappears entirely. What surprised me is that GCC 16 does this already at -O0, with not one optimization level switched on, and Clang 22 behaves the same here.

Want to see both copies with your own eyes? Switch copy elision off with `-fno-elide-constructors`:

```bash
g++ -O0 -fno-elide-constructors a4.cpp -o a4_O0_noe && ./a4_O0_noe
g++ -O2 -fno-elide-constructors a4.cpp -o a4_O2_noe && ./a4_O2_noe
```

```text
prefix  : copies=0
postfix : delta_copies=2
p offset: 2
```

The result is 2 at both optimization levels. What the default configuration elides is exactly the return-path copy; the construction that stashes the old value is demanded by the semantics, and you cannot switch that one off even if you want to. We have a cross-compiler witness too: clang++ 22.1.8 gives the same result of 1 at both -O0 and -O2 (it emits an unknown attribute warning for GCC's noclone, with no effect on behavior).

Two spots are easy to trip over when you reproduce this experiment, so let me flag them up front:

- Do not write `it++;` directly in main. At -O2 the counter gets constant-folded along with everything else, printf prints a constant, the assembly shows you nothing, and you will suspect you did something wrong. The noinline-plus-noclone wrapping is the precondition for the evidence to stand.
- The compiler version matters. My GCC 16.2.1 and Clang 22.1.8 both do NRVO at -O0, hence 1. Older GCC versions that skip NRVO at -O0 will show you 2. That is not a mistake on your part, it is a version difference, and `-fno-elide-constructors` separates the two copies so you can see each one clearly.

## The Same Class at -O2: The Data Movement Vanishes

We take the same code and rerun it at -O2:

```bash
g++ -Wall -Wextra -O2 a4.cpp -o a4_O2 && ./a4_O2
```

```text
prefix  : copies=0
postfix : delta_copies=1
p offset: 2
```

The counter still reads 1, but the assembly is a different animal altogether (AT&T syntax):

```asm
_Z10run_prefixR4Iter:
	addq	$4, (%rdi)
	ret

_Z11run_postfixR4Iter:
	addl	$1, _ZN4Iter6copiesE(%rip)
	addq	$4, (%rdi)
	ret
```

Look at `run_prefix`: two instructions for the whole function, one adds 4 to the pointer and the other returns. `run_postfix` has three, and the extra `addl $1, _ZN4Iter6copiesE(%rip)` is the counter increment. Why it survives is worth a thought: main prints copies right afterward, and the counter is observable state, so the increment has to happen. The actual old-value movement, the steps that wrote the old value of this's pointer member into the old object, is now down to zero instructions.

So here is what we can say: for an ordinary iterator class with no side effects, postfix compiles instruction-for-instruction identical to prefix at -O2 (the one extra instruction in our example was earned by the counter, not by postfix semantics). The runtime delta of 1 is a requirement at the abstract-machine semantics level, paid in full by that one extra instruction, and no data movement actually happens. The abstract machine is the machine model the standard imagines; a program's behavior on it is the behavior that ought to happen, and every optimization a compiler performs must keep the observable results consistent with it.
