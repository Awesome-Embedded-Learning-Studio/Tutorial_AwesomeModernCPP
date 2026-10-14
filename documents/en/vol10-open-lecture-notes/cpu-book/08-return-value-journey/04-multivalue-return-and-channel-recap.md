---
title: "Returning Multiple Values: pair Packing and the Full Picture of the Three Routes"
chapter: 8
order: 4
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
description: "A std::pair<int, int> return draws a single rax, the counting experiments place all 2 copies inside the pair constructor, structured bindings catch the return without an extra copy, and the piece closes by gathering the three physical routes together with the one layer of copy elision"
reading_time_minutes: 15
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/08-return-value-journey/04-multivalue-return-and-channel-recap.md
  source_hash: a9d0bcc6526b663aeffed59f1f26803010d666688b987c3073bff9a4883ee66c
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Returning Multiple Values: pair Packing and the Full Picture of the Three Routes

The previous piece finished the chase after copy elision; this one returns to the physical routes and looks at a shape you use every day: returning multiple values. The environment is the same one [the first piece](01-rax-family-and-register-pairs.md) used, so if you want to rerun anything, follow its setup.

## Multiple Return Values: pair Packing and Structured Bindings

C++ has no syntax for returning several values, so the habit is to pack two results into a std::pair. In the ABI's eyes a pair is just an ordinary struct, and it lands in the same classification, so this stretch of the piece doubles as a review of the routing criteria. Our opening prediction went like this: a pair has two members, so its return should travel in rax plus rdx. The measurement beat us.

```cpp
// Return-register packing of a std::pair<int,int> (8 bytes, trivially copyable) + receiving it with a structured binding
#include <utility>

std::pair<int, int> make_ipair(int a, int b) __attribute__((noinline));
std::pair<int, int> make_ipair(int a, int b) { return {a, b}; }

int main() {
    auto [x, y] = make_ipair(3, 4);        // structured binding reception
    volatile int sink = x + y;             // consume, so nothing is deleted
    return sink == 7 ? 0 : 1;
}
```

### Against the Prediction: pair Takes a Single Register

A `std::pair<int, int>` measures 8 bytes, exactly one eightbyte, so it lands in class INTEGER and, by the criteria, is allotted a single register. The compile command is `g++ -O0 -S -fno-asynchronous-unwind-tables r6a_ipair.cpp -o r6a_ipair.s`. Here is the -O0 function body, with the `.cfi_*` lines and the stack protector's canary setup (a sentinel value the stack protector plants) omitted; the canary-checking epilogue at the end is kept as measured:

```asm
_Z10make_ipairii:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movl	%edi, -20(%rbp)
	movl	%esi, -24(%rbp)
	leaq	-24(%rbp), %rdx
	leaq	-20(%rbp), %rcx
	leaq	-16(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	_ZNSt4pairIiiEC1IRiS2_EEOT_OT0_
	movq	-16(%rbp), %rax        ; the whole pair loaded into rax in one go
	movq	-8(%rbp), %rdx         ; (stack protector canary, unrelated to the return)
	subq	%fs:40, %rdx
	je	.L3
	call	__stack_chk_fail@PLT
.L3:
	leave
	ret
```

That call is what -O0 looks like when it declines to inline library functions: the forwarding constructor drops the two ints into the pair on the stack, and the very next `movq -16(%rbp), %rax` carries the whole thing away in one load. The -O2 version is cleaner. Take the same r6a_ipair.cpp, compile it at -O2, and grab the assembly again; the function body that comes out is three instructions long:

```asm
_Z10make_ipairii:
	salq	$32, %rsi
	movl	%edi, %eax
	orq	%rsi, %rax
	ret
```

`salq $32, %rsi` shifts the second int into the high half, `movl %edi, %eax` puts the first int into the low half, and the 32-bit write clears the high half of rax as a side effect; the closing `orq` finishes the splice, and the two ints now live in the same rax. Whether a type splits across registers is decided, from start to finish, by the number of eightbyte chunks, not by the member count and not by whether the type happens to be named pair. If you do want a genuine case of rax plus rdx, Pair8 from the first piece is ready-made: two INTEGER chunks earned two registers. The assumption that a pair returns in segments has already been overturned by measurement in this piece, so watch out for it when you take notes. Run the built `./r6a` and the exit code reports 0.

On the main side the reception uses a structured binding, a C++17 spelling: `auto [x, y]` introduces a hidden variable that takes in the whole return value, and x and y bind to its two members. Here is the -O0 reception stretch, with the `.cfi_*` lines, the canary setup, and the comparing-and-branching epilogue omitted:

```asm
	movl	$4, %esi
	movl	$3, %edi
	call	_Z10make_ipairii
	movq	%rax, -16(%rbp)       ; the hidden variable lands in one move, no second transfer
	leaq	-16(%rbp), %rax
	movq	%rax, %rdi
	call	_ZSt3getILm0EiiEONSt13tuple_elementIXT_ESt4pairIT0_T1_EE4typeEOS4_
	movq	%rax, -32(%rbp)       ; std::get<0> returns the member's address (a reference)
	leaq	-16(%rbp), %rax
	movq	%rax, %rdi
	call	_ZSt3getILm1EiiEONSt13tuple_elementIXT_ESt4pairIT0_T1_EE4typeEOS4_
	movq	%rax, -24(%rbp)
	movq	-32(%rbp), %rax
	movl	(%rax), %edx          ; x read through the address
	movq	-24(%rbp), %rax
	movl	(%rax), %eax          ; y read through the address
	addl	%edx, %eax
	movl	%eax, -36(%rbp)
```

The first move, `movq %rax, -16(%rbp)`, lands the hidden variable in one shot; the binding itself involves no second transfer. The two calls after it come from `std::get<0>` and `std::get<1>`, which reach the members through addresses. Don't take those calls as proof that binding has overhead: at -O0 the compiler will not inline even library functions, so they are a shape of the toolchain, not a semantic of the binding. Overhead is something we judge by counting instead.

### Counting the Reception: the Plus Comes from Inside the pair

For the counting experiment we swap the members for a Counter that keeps a tally, and count from inside the function all the way out to the binding reception:

```cpp
// Copy counting at the structured-binding reception (semantic evidence)
#include <utility>
#include <cstdio>

struct Counter {
    int v;
    static long copies;
    Counter(int v_) : v(v_) {}
    Counter(const Counter& o) : v(o.v) { ++copies; }
};
long Counter::copies = 0;

using CPair = std::pair<Counter, Counter>;

CPair make_cpair(int a, int b) __attribute__((noinline));
CPair make_cpair(int a, int b) { return {Counter{a}, Counter{b}}; }

int main() {
    auto [x, y] = make_cpair(1, 2);   // hidden variable + x/y bound to the members by reference
    volatile int sink = x.v + y.v;    // read members only, no copies
    std::printf("member-copies = %ld, x = %d, y = %d\n", Counter::copies, x.v, y.v);
    return sink == 3 ? 0 : 1;
}
```

Here are all four compile-and-run rounds verbatim, with the structured-binding extension warning from the C++14 round kept exactly as printed:

```text
$ g++ -O0 r6b_binding.cpp -o r6b_O0 && ./r6b_O0
member-copies = 2, x = 1, y = 2
$ g++ -O0 -fno-elide-constructors r6b_binding.cpp -o r6b_O0_noelide && ./r6b_O0_noelide
member-copies = 2, x = 1, y = 2
$ g++ -O2 -fno-elide-constructors r6b_binding.cpp -o r6b_O2_noelide && ./r6b_O2_noelide
member-copies = 2, x = 1, y = 2
$ g++ -std=c++14 -O0 -fno-elide-constructors r6b_binding.cpp -o r6b_cpp14_O0_noelide && ./r6b_cpp14_O0_noelide
r6b_binding.cpp: In function ‘int main()’:
r6b_binding.cpp:20:10: warning: structured bindings only available with ‘-std=c++17’ or ‘-std=gnu++17’ [-Wc++17-extensions]
   20 |     auto [x, y] = make_cpair(1, 2);   // hidden variable + x/y bound to the members by reference
      |          ^
member-copies = 4, x = 1, y = 2
```

Is the 2 under the default settings the return path's fault? Let's pin the copy sites down with objdump, and then check whether either function's own body carries any copy calls:

```text
$ objdump -d r6b_O0 | grep -B4 'call.*_ZN7CounterC1ERKS_'
    133a:	48 89 55 f8          	mov    %rdx,-0x8(%rbp)
    133e:	48 8b 55 f8          	mov    -0x8(%rbp),%rdx
    1342:	48 89 d6             	mov    %rdx,%rsi
    1345:	48 89 c7             	mov    %rax,%rdi
    1348:	e8 2f ff ff ff       	call   127c <_ZN7CounterC1ERKS_>
--
    1359:	48 89 45 f0          	mov    %rax,-0x10(%rbp)
    135d:	48 8b 45 f0          	mov    -0x10(%rbp),%rax
    1361:	48 89 c6             	mov    %rdx,%rsi
    1364:	48 89 d7             	mov    %rdx,%rdi
    1367:	e8 10 ff ff ff       	call   127c <_ZN7CounterC1ERKS_>

$ nm r6b_O0 | grep -E 'CounterC1ERKS_|St4pair.*C[12]'
000000000000127c W _ZN7CounterC1ERKS_
000000000000131e W _ZNSt4pairI7CounterS0_EC1IS0_S0_EEOT_OT0_   <- start address of the pair forwarding constructor, 0x131e

$ objdump -d r6b_O0 | awk '/<_Z11make_cpairii>:/,/^$/' | grep -c 'CounterC1ERKS_'
0
$ objdump -d r6b_O0 | awk '/<main>:/,/^$/' | grep -c 'CounterC1ERKS_'
0
```

The two copy calls sit at 0x1348 and 0x1367, and both land inside the body of the `std::pair<Counter,Counter>` forwarding constructor that starts at 0x131e: handing a temporary Counter to a parameter is mandatorily elided, but the copy from parameter to member is not, and one copy per member adds up to 2. Inside the bodies of make_cpair and main themselves, the count of copy-constructor calls is 0 in both. So the bare count of 2 belongs to the pair constructor; the return, the reception, and the binding all come out clean. The extra 2 copies in the C++14 no-elide build have a location too: after the call, main initializes the hidden variable with the pair's move constructor, and Counter has no move constructor, because the user-declared copy constructor suppressed the implicit one. The pair move constructor's member initialization therefore falls back to copies, and both of those copies happen inside that move constructor's body. Once C++17's mandatory elision took over, this reception-side cost of the old dialect went straight to zero.

Next comes a comparison on the same baseline: swap main in r6b for the two stretches below, save the file as r6c_ab.cpp, and structured-binding reception and named-variable reception get measured back to back in one program:

```cpp
    long before = Counter::copies;
    auto [x, y] = make_cpair(1, 2);              // structured binding reception
    volatile int s1 = x.v + y.v;
    std::printf("binding   reception: +%ld\n", Counter::copies - before);

    before = Counter::copies;
    CPair p = make_cpair(3, 4);                  // named variable reception
    volatile int s2 = p.first.v + p.second.v;
    std::printf("namedvar reception: +%ld\n", Counter::copies - before);
```

The three configurations correspond to ./r6c_O0, ./r6c_O0_noelide, and ./r6c_O2 in the output; here is what we saw:

```text
== -O0 default ==        ./r6c_O0
binding   reception: +2
namedvar reception: +2
== -O0 -fno-elide ==  ./r6c_O0_noelide
binding   reception: +2
namedvar reception: +2
== -O2 default ==        ./r6c_O2
binding   reception: +2
namedvar reception: +2
(exit=0)
```

Read across the three configurations, the increments for the two reception styles are identical word for word: both report +2, and we already traced that +2 to the inside of the pair's construction. So when a structured binding catches a pair return, the extra copies are zero; all the binding does is give the members names. The same reasoning covers tuple: it is an ordinary class type too, it falls under the same eightbyte classification, and once the fields pile up, the size easily passes the two-chunk limit, at which point the route switches to sret on its own. That sentence is an inference from the criteria; we ran no dedicated experiment for tuple. If you want to check it, measure it once with today's toolkit.

## Pulling It Together: the Three Routes Plus One Layer of Elision

Time to gather the whole story of how a return value travels. Size and class fix the three physical routes: for a single eightbyte chunk, the data travels in rax, with bool narrowed down to the one byte of al, int riding in eax, and a pointer taking the full width. Aggregates of two chunks or fewer draw rax and rdx chunk by chunk, integers and floats each from their own pool. Past two chunks, or when the type is non-trivial for calls, the handoff switches to the hidden pointer in rdi, the data is written straight into the caller's stack slot, and rax hands back the address. The elision C++ stacks on top of the routes comes in two layers: NRVO moves a named local's construction into the return slot, and from C++17 on the reception-side elision of prvalues is written into the language itself, so much so that the word elision undersells it.

The threads this piece ties off connect back to pieces 01 and 03. For [the oddity in 01 where the textbook says two copies and the assembly shows one](../01-prefix-vs-postfix-increment/02-class-type-cost.md), we now have the full answer: the frame has no slot for p, the constructor's this is the sret address, return p compiles to a single movq, and the count reads 0. [Piece 03 staged the mechanism of the hidden first argument](../03-arguments-in-registers/02-stack-args-and-return-values.md); this piece brought in the rule's original wording and the return-side roster, added the non-trivial exception 03 never covered, and, in passing, formally introduced you to the name sret. Threads from further back tie off as well: the call and ret of 02 are what carry every one of these return paths, the constructors of 05 are what land this on the caller's stack here, and the volatile sink of 07 is the instrument this whole series observes with. If you want to keep going in the performance direction, vol6 has [the real cost of RVO, NRVO, and move](../../../vol6-performance/ch06-cpp-abstraction-cost/06-05-rvo-move.md), which measures the cost of returning by value with a counting Tracked type and totals up what the `return std::move(local)` antipattern really costs, using the same counting method as this piece.

One last, minimal piece of evidence, and it is where this piece lands. In the -O2 build, make_ipair's whole function body is down to three instructions, `salq`, `movl`, `orq`, and the two ints are spliced into a single rax right there. This is how much arranging the compiler does for a return value, and those three instructions are its answer.
