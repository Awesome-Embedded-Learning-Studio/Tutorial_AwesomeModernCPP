---
title: "i++ and ++i: Vector Iterators and the Boundary of Side Effects"
chapter: 1
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "At -O2, vector's iterator increments are instruction-for-instruction identical between prefix and postfix; once the copy constructor carries observable side effects, the only thing that cannot be eliminated is the side effect itself, and we close with a rule of thumb for which one to write"
reading_time_minutes: 15
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/01-prefix-vs-postfix-increment/03-vector-and-side-effects.md
  source_hash: 838d28e1b7c6dd65586f24c6c49ad80203f641f3948977d577356dbecf394417
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# i++ and ++i: Vector Iterators and the Boundary of Side Effects

We're carrying the experimental setup from [the first piece](01-int-instructions.md) straight into this one, unchanged.

## The real thing from the standard library: vector's iterator

An iterator we hand-write ourselves only carries so much conviction, so let's put the standard library's vector on stage. The two source files differ in exactly one place: the `it++` in the loop becomes `++it`. The function names are deliberately identical too, so the mangled names come out the same and the diff stays clean:

```cpp
#include <vector>

volatile int sink;

int sum(const std::vector<int>& v) {
    int s = 0;
    for (auto it = v.begin(); it != v.end(); it++)
        s += *it;
    return s;
}

int main() {
    std::vector<int> v{1, 2, 3, 4, 5};
    sink = sum(v);
    return 0;
}
```

The other file writes the loop's increment as `++it`, and not a single other character changes. Before we look at the -O0 output, we need to meet the libstdc++ iterator's true form. Here is the original text of lines 1098 through 1112 of `/usr/include/c++/16/bits/stl_iterator.h`:

```cpp
      __attribute__((__always_inline__))
      _GLIBCXX14_CONSTEXPR
      __normal_iterator&
      operator++() _GLIBCXX_NOEXCEPT
      {
	++_M_current;
	return *this;
      }

      __attribute__((__always_inline__))
      _GLIBCXX14_CONSTEXPR
      __normal_iterator
      operator++(int) _GLIBCXX_NOEXCEPT
      { return __normal_iterator(_M_current++); }
```

We expected to see calls to operator++ in the -O0 assembly. What the library actually does overturns that prediction. The postfix side skips the textbook two-step tmp dance and writes `return __normal_iterator(_M_current++);` instead: the internal raw pointer performs the postfix increment (producing the old value), and that value directly constructs the return object. And since C++17, constructing a returned prvalue involves no copy. It lands straight in the caller's slot. A prvalue, roughly, is a value that has just been computed and has not yet moved into any variable, and that is exactly what `_M_current++` produces in this line. Then there is the matter of the call itself: every one of these members carries `__attribute__((__always_inline__))`, and the attribute holds all the way down to -O0. So in the -O0 assembly you will not find a call to operator++ (begin and end still get plain calls, without this privilege), and the difference surfaces as inlined code.

Here is the -O0 loop increment section (AT&T syntax). The postfix version's loop body (`.L8` is the body label; the first three lines do the `s += *it` summation, and the loop test after them is what got cut):

```asm
.L8:
	movq	-48(%rbp), %rax
	movl	(%rax), %eax
	addl	%eax, -56(%rbp)
	movl	$0, -52(%rbp)
	movq	-48(%rbp), %rax
	leaq	4(%rax), %rdx
	movq	%rdx, -48(%rbp)
	movq	%rax, -40(%rbp)
	movq	-40(%rbp), %rax
	movq	%rax, -32(%rbp)
	nop
	nop
```

Now the prefix version (AT&T syntax):

```asm
.L8:
	movq	-40(%rbp), %rax
	movl	(%rax), %eax
	addl	%eax, -44(%rbp)
	movq	-40(%rbp), %rax
	addq	$4, %rax
	movq	%rax, -40(%rbp)
	nop
```

Counting from `movl $0, -52(%rbp)`, the postfix increment section runs seven instructions; the prefix version gets by with three. Here are the postfix section's seven, laid out in a table:

| Instruction | What it does |
|---|---|
| `movl $0, -52(%rbp)` | Materializes the postfix dummy argument |
| `movq -48(%rbp), %rax` | Reads the pointer's old value into rax |
| `leaq 4(%rax), %rdx` | Computes the pointer-plus-four new value in rdx |
| `movq %rdx, -48(%rbp)` | Writes the new pointer back to the iterator |
| `movq %rax, -40(%rbp)` | Stashes the old value in the raw-pointer increment's result temporary |
| `movq -40(%rbp), %rax` | Reads the result temporary back |
| `movq %rax, -32(%rbp)` | Stores it into the returned iterator object (the return slot after inlining) |

Walk the table once and you can see that all four extra instructions exist to serve an old value nobody wants: the dummy argument gets materialized once, the old value lands in `-40` (the result temporary of the raw pointer's `_M_current++`), moves from `-40` into `-32` (the return slot after inlining), and gets written out to memory twice in all. As for the function's whole stack frame, the postfix version uses 80 bytes and the prefix version only 64. The diff between the two .s files comes out at 67 differing lines, and everything outside the increment section is collateral difference from stack slot offsets shifting around.

Then we turn to -O2. Four compile commands plus two diffs are the entire kit (the 67 that `grep -c` counts is the collateral line count from a moment ago):

```bash
g++ -O0 -S a6_post.cpp -o a6_post_O0.s
g++ -O0 -S a6_pre.cpp  -o a6_pre_O0.s
g++ -O2 -S a6_post.cpp -o a6_post_O2.s
g++ -O2 -S a6_pre.cpp  -o a6_pre_O2.s
diff a6_post_O0.s a6_pre_O0.s | grep -c "^[<>]"
diff a6_post_O2.s a6_pre_O2.s
```

And the complete -O2 diff output amounts to this:

```diff
1c1
< 	.file	"a6_post.cpp"
---
> 	.file	"a6_pre.cpp"
```

The two files are identical except for the `.file` line carrying each one's own name. Here is sum's body (AT&T syntax): the pointer walks from the beginning of the vector to the end, adding as it goes:

```asm
_Z3sumRKSt6vectorIiSaIiEE:
	movq	(%rdi), %rax
	movq	8(%rdi), %rcx
	xorl	%edx, %edx
	cmpq	%rcx, %rax
	je	.L1
	.p2align 4
	.p2align 4
	.p2align 3
.L3:
	addl	(%rax), %edx
	addq	$4, %rax
	cmpq	%rcx, %rax
	jne	.L3
.L1:
	movl	%edx, %eax
	ret
```

Then main, which the compiler folded into three instructions: `movl $15` writes 15 into sink (15 is the sum of 1 through 5), `xorl` zeroes the return-value register, and a final `ret` signs off. The volatile store survived; not one of the called functions ever ran. With the old value left without a user, it vanished along with the dummy argument and the temporary construction, and the diff between the two outputs picked up only that one `.file` line. By this point, the increment form's influence on the generated code has come down to a single filename.

One more detail worth keeping: along the inlined path, the old value passes through a stop on the stack (`-40`) before it reaches the return slot (`-32`). It never received the -O0 NRVO treatment that a standalone function body enjoyed in [the previous piece](02-class-type-cost.md). The frontend integration of always_inline and the compilation of ordinary functions are two separate roads, and we won't chase that further here. Just don't conflate the two.

## The boundary of side effects: when something truly cannot be eliminated

All the way here, postfix has kept giving ground, and you may want to ask on its behalf: does it ever get to hold its ground? It does. Let's turn the copy constructor into one with an observable side effect, so that every copy writes a record into a volatile array:

```cpp
#include <cstdio>

// Copy constructor with an observable side effect: writes two values into volatile globals.
// A volatile write is, by definition, impossible to optimize away.
volatile unsigned long side_log[64];
volatile int side_count = 0;

struct NoisyIter {
    int* p;

    NoisyIter() : p(nullptr) {}
    explicit NoisyIter(int* q) : p(q) {}

    NoisyIter(const NoisyIter& other) : p(other.p) {
        int idx = side_count;      // volatile read
        side_count = idx + 1;      // volatile write: observable side effect
        side_log[idx] = 0xC0DEul;  // volatile write: observable side effect
    }

    NoisyIter& operator++() {
        ++p;
        return *this;
    }

    NoisyIter operator++(int) {
        NoisyIter old(*this);
        ++p;
        return old;
    }
};

__attribute__((noinline, noclone)) void run_prefix(NoisyIter& it) {
    ++it;
}

__attribute__((noinline, noclone)) void run_postfix(NoisyIter& it) {
    it++;
}

int main() {
    int arr[4] = {10, 20, 30, 40};
    NoisyIter it(arr);

    run_prefix(it);
    run_postfix(it);
    run_postfix(it);
    std::printf("side_count=%d p_offset=%ld\n",
                side_count, static_cast<long>(it.p - arr));
    return 0;
}
```

The counter goes read, write, write in three steps instead of `side_count++` so as to dodge the warning about volatile compound operations being deprecated, which started in C++20. We have main call prefix once and postfix twice, then compile and run at both optimization levels:

```bash
g++ -Wall -Wextra -O0 a7.cpp -o a7_O0 && ./a7_O0
g++ -Wall -Wextra -O2 a7.cpp -o a7_O2 && ./a7_O2
g++ -O2 -S a7.cpp -o a7_O2.s
```

```text
--- O0 run ---
side_count=2 p_offset=3
--- O2 run ---
side_count=2 p_offset=3
```

Two postfix calls, two optimization levels, and the side-effect counter reads 2 both times. Here is the -O2 assembly (AT&T syntax):

```asm
_Z10run_prefixR9NoisyIter:
	addq	$4, (%rdi)
	ret

_Z11run_postfixR9NoisyIter:
	movslq	side_count(%rip), %rax
	movq	(%rdi), %rdx
	leal	1(%rax), %ecx
	movl	%ecx, side_count(%rip)
	leaq	side_log(%rip), %rcx
	movq	$49374, (%rcx,%rax,8)
	leaq	4(%rdx), %rax
	movq	%rax, (%rdi)
	ret
```

(49374 is 0xC0DE in decimal.) Let's separate what got spared from what didn't, layer by layer:

- The side-effect instruction sequence lost nothing at all: read side_count, write back the incremented value, write the marker into side_log, fully preserved. A volatile write cannot be eliminated by definition. This part is the core of why the copy has to happen.
- Pure data shuffling still gets spared: the old object's pointer member never enters any old slot. `movq (%rdi), %rdx` only reads the old pointer into a register for later use; nothing is written out.
- The copy constructor on the return path carries side effects too, and it got spared anyway. The proof is that side_count reads 2, not 4: if both copies inside each postfix call executed, the two calls should have counted out to 4.

The third layer is the most counterintuitive one, so let's line it up against what the standard says. Two different rules are stacked here. A copy without side effects may be eliminated under the as-if rule, because you could never observe the difference anyway. A copy with side effects can still be eliminated, but the justification switches to copy elision, an exception the standard spells out in plain text, and as-if gets no say here. The opening of cppreference's copy_elision page reads:

> ```text
> When certain criteria are met, the creation of a class object from a source
> object of the same type (ignoring cv-qualification) can be omitted, even if
> the selected constructor and/or the destructor for the object have side effects.
> ```

In plain terms: when certain criteria are met, the creation of a class object from a source object of the same type can be omitted, even if the selected constructor or destructor has side effects. The Notes section on the same page assigns it its place among optimizations:

> ```text
> Copy elision is the only allowed form of optimization(until C++14) one of the
> two allowed forms of optimization, alongside allocation elision and
> extension,(since C++14) that can change observable side-effects. Because some
> compilers do not perform copy elision in every situation where it is allowed
> (e.g., in debug mode), programs that rely on the side-effects of copy/move
> constructors and destructors are not portable.
> ```

Take the second half of the Notes as a practical reminder: some compilers skip copy elision in debug builds, so a program that relies on the side effects of copy constructors is not portable. As for copy elision's official standing, it goes like this: before C++14 it was the only optimization allowed to change observable side effects, and since C++14 it stands alongside allocation elision and extension, two explicit exceptions in the standard's own text. This is exactly what the reproduction note on the Iter experiment in [the previous piece](02-class-type-cost.md) rested on: an older compiler counting 2 at -O0 needs no explanation beyond this.

I checked the copy_elision page text on purpose, and the term as-if never appears on it. So the rigorous phrasing comes in two sentences: a copy without side effects can be dropped under the as-if rule, and a copy with side effects can be dropped because copy elision grants a specific license. The experiment above happens to demonstrate both layers: the side-effect sequence stays where it is (as-if cannot govern it, because removing it would change observable behavior), the data shuffling disappears under as-if's leave, and the disappearance of the side-effecting copy on the return path rests on copy elision's leave.

## So which one should you write

Do i++ and ++i generate the same instructions after all? The answer depends on the scenario. When the value is discarded, the two are instruction-for-instruction identical starting at -O0. When the value of an int is used, postfix costs one instruction more than prefix at -O0 (six against five), and by -O2 the instruction count and the cost are level, with the sequences differing because the semantics differ. For class types without side effects, -O2 scrubs the old-value shuffling out completely, and what is left sits one counter away from instruction-for-instruction identical, a counter we bolted in purely so we could observe. For class types with side effects, the side-effecting construction has to genuinely happen; only the shuffling and the return-path copy can go.

So our rule of thumb comes down to two questions: is anyone using the expression's value, and is the operand a class type? In the int case, whichever way you answer, performance is no reason to rewrite anything. Write whichever you like; the effort you would spend changing it is better saved for something else. In the class-type case we take ++i as the default habit, and the reason is peace of mind: you never have to revisit the old-value copy again. As for the performance argument, the int experiments already measured it, and those few nanoseconds do not exist. cppreference's Notes chose the words usually more efficient, which speaks of typical cost and never promised a guarantee.

And one last piece of live evidence for you: two vector source files differing only in whether the loop writes `it++` or `++it`, each compiled once at -O2, and the diff comes out at a single line, the `.file` line carrying each file's own name. Next time someone tries to sell you on i++ being slow, hand them this experiment and ask them to run it.
