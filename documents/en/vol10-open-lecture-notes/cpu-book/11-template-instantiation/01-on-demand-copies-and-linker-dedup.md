---
title: "Template Instantiation Compared: On-Demand Copies and Linker Deduplication"
chapter: 11
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "One max template, and int and double each get their own machine code. Nothing gets printed when nobody calls it, and when two translation units print overlapping copies, weak symbols plus COMDAT let the linker keep a single one"
reading_time_minutes: 11
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/11-template-instantiation/01-on-demand-copies-and-linker-dedup.md
  source_hash: 7fa9b64a1d436a403fc3d73bb7b3e4be0b815b5fb497288b70f30eab9a3dee6d
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Template Instantiation Compared: On-Demand Copies and Linker Deduplication

In the [previous piece](../10-new-in-assembly/03-array-new-cookie-and-reverse-destruction.md) we followed a single new through the handoff between memory and construction, and everything on screen was code we had written ourselves. This piece changes the point of view to templates. You write `max` once, call it with both `int` and `double`, and the program runs perfectly well, with no runtime magic anywhere picking functions on your behalf. The picking happens at compile time: for every type that gets used, the compiler rewrites the template into an ordinary function, one copy of integer machine code when `T` is `int`, another copy of floating-point machine code when `T` is `double`. The standard's name for producing an entity from a template plus a concrete type is instantiation; in this piece we call it printing a copy.

For the previous ten pieces the question has been "what instructions did my code turn into"; today's question is "what did the code the compiler wrote for me turn into". The tools are the familiar few: `nm` reads the symbol table for us, `objdump` reads the assembly. A template has one and only one source, yet what lands in the object file is a whole stack of functions, and this piece traces that stack back to where every layer came from: how the copies appear, how they multiply as types pile up, and how the duplicates get merged back into one.

We set the copying metaphor up at the start, and everything after leans on it to lead the way. A template's definition is a master that prints nothing by itself. When a type gets used, the compiler runs off a copy of the master for that type. Once the printed copies pile up, the name collisions go to the linker, which keeps one copy per name. The three rules (on demand, per type, deduplication) we'll tick off against measurements when the next piece wraps up.

## Our Test Environment

As usual the environment comes first, so that every block of output below can be reproduced on a machine with the same setup:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

Our machine is x86-64, and the object files it builds are 64-bit ELF relocatable files, the not-yet-linked intermediate artifacts. This piece calls on more tools than the last few did: `nm` reads the symbol table, `objdump` does the disassembly, `readelf` reads the object file's section information, and `size` measures bulk, all one binutils family. Most of them are old acquaintances: `objdump` has been with us since piece 04, `nm` walked us through symbols in 08, `readelf` read section information with us in 09, and only `size` makes its first appearance today.

For assembly we cut the noise with `objdump -d -M att --no-show-raw-insn`, which drops the raw-byte column, the practice carried over from piece 04; the `-M att` flag says out loud that we're using AT&T syntax, the series default since piece 02. Lines beginning with `#` inside the assembly are our own reading; the instructions are pasted verbatim, and anywhere we excerpt we say so.

One more condition deserves a plain statement: every compile command in this piece carries `-O0`, the literal-translation level we met in piece 01. Of everything in this run of pieces, the template piece leans on it hardest, because at `-O2` these small templates get inlined whole, `nm` won't leave a single `max` symbol standing, and the entire piece's subject matter disappears. When you reproduce this, carry `-O0` in the command exactly as written, or what you see won't match what we pasted. What happens at optimized levels gets its own measured treatment in the [next piece](02-size-explicit-and-class-templates.md), when we weigh the sizes.

## One Source, Two Functions Printed

The material is a minimal template: `max` takes two references of the same type and hands back the larger one. In `main` we call it once with `int` and once with `double`:

```cpp
// m1.cpp -- one template, called once for each of two types
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    int    xi = 1,  yi = 2;
    double xd = 1.5, yd = 2.5;

    int    r1 = max(xi, yi);   // instantiates max<int>
    double r2 = max(xd, yd);   // instantiates max<double>

    return r1 + static_cast<int>(r2);
}
```

```text
$ g++ -O0 -c m1.cpp -o m1.o
$ nm m1.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W _Z3maxIdET_RS0_S1_
0000000000000000 W _Z3maxIiET_RS0_S1_
```

In `nm`'s output the letter after the address is the symbol's type. The letter in front of `main` is `T`, an ordinary symbol defined in `.text`, a strong symbol in the trade's jargon. The letter in front of `__stack_chk_fail` is `U`, an undefined reference: this file uses it but holds no definition of it, and we wait for the linker to fetch it out of the runtime library. It's a stub of the stack protector, it has nothing to do with today's topic, and we won't mention it again. The letter in front of the two `max` lines is `W`, for weak. A weak symbol is a definition allowed to coexist with same-named definitions elsewhere, with one copy picked and kept at link time. As for why it's weak, we'll watch the mechanism in measurements when we reach the part about the linker keeping one copy; for now you only need to hold on to one thing: those two `W` lines are two independent functions.

Now let's take the mangled name apart. Name mangling is the step where the compiler encodes the function's name and its type information into the symbol string; piece 02 taught us the pattern with `_Z3addii`, and piece 05 added the `N...E` spelling for class names. Today's new territory is the template part, and we'll read `_Z3maxIiET_RS0_S1_` segment by segment:

- `_Z`: the prefix of Itanium C++ ABI name mangling; seeing it means you're looking at a C++ mangled name. What GCC implements on Linux x86-64 is exactly this ABI.
- `3max`: the function name `max`, where 3 is the character count, the same trick as the `3add` inside `_Z3addii`.
- `IiE`: today's centerpiece. `I` and `E` bracket the template argument list, matching the `<...>` in the source, and the single `i` inside is `int`. Read aloud, the whole string says "max, int edition".
- `T_`: refers to the first template parameter, `int` itself here. When a function template's return type depends on the template parameters, that return type gets encoded at this position.
- `RS0_`: `R` is an lvalue reference, `S0_` is a substitution token pointing back at the `T_` just seen, that is, `int`; together they spell the first parameter, `int&`.
- `S1_`: substitution number one, pointing back at `RS0_`; the second parameter is `int&` all over again.

Assembled, the result is `int max<int>(int&, int&)`. Nobody needs to memorize this encoding, because `c++filt` decodes it and the two accounts corroborate each other, and `nm -C` simply hands you the decoded form:

```text
$ nm -C m1.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W double max<double>(double&, double&)
0000000000000000 W int max<int>(int&, int&)
```

Different types draw different letters for their template arguments, and the common ones are worth listing in pairs:

- `d` corresponds to double
- `c` corresponds to char
- `f` corresponds to float
- `l` corresponds to long
- `s` corresponds to short
- `j` corresponds to unsigned int
- `x` corresponds to long long

The symbol name changes with the type. That tail is exactly what "one separate symbol per instance" refers to at the level of names. One more spot tends to raise suspicion and deserves a word up front: the way the parameters are written rides along inside the symbol string too. Our `max` takes `T&`, and the printed string is `_Z3maxIiET_RS0_S1_`; switch to a pass-by-value version and the string becomes `_Z3maxIiET_S0_S0_`. If a string you find in other material doesn't match the one here, a different signature is the likeliest cause, and one pass through `c++filt` settles it.

Symbols sorted, on to the assembly. The output below is excerpted: we keep the two `max` functions and drop `main` along with the stack protector's stub:

```asm
0000000000000000 <_Z3maxIiET_RS0_S1_>:
   0:	push   %rbp
   1:	mov    %rsp,%rbp
   4:	mov    %rdi,-0x8(%rbp)
   8:	mov    %rsi,-0x10(%rbp)
   c:	mov    -0x8(%rbp),%rax
  10:	mov    (%rax),%edx
  12:	mov    -0x10(%rbp),%rax
  16:	mov    (%rax),%eax
  18:	cmp    %eax,%edx
  1a:	jge    24 <_Z3maxIiET_RS0_S1_+0x24>
  1c:	mov    -0x10(%rbp),%rax
  20:	mov    (%rax),%eax
  22:	jmp    2a <_Z3maxIiET_RS0_S1_+0x2a>
  24:	mov    -0x8(%rbp),%rax
  28:	mov    (%rax),%eax
  2a:	pop    %rbp
  2b:	ret

0000000000000000 <_Z3maxIdET_RS0_S1_>:
   0:	push   %rbp
   1:	mov    %rsp,%rbp
   4:	mov    %rdi,-0x8(%rbp)
   8:	mov    %rsi,-0x10(%rbp)
   c:	mov    -0x8(%rbp),%rax
  10:	movsd  (%rax),%xmm1
  14:	mov    -0x10(%rbp),%rax
  18:	movsd  (%rax),%xmm0
   1c:	comisd %xmm1,%xmm0
   20:	jbe    2c <_Z3maxIdET_RS0_S1_+0x2c>
  22:	mov    -0x10(%rbp),%rax
  26:	movsd  (%rax),%xmm0
   2a:	jmp    34 <_Z3maxIdET_RS0_S1_+0x34>
  2c:	mov    -0x8(%rbp),%rax
  30:	movsd  (%rax),%xmm0
  34:	pop    %rbp
  35:	ret
```

The two functions share an identical skeleton: save rbp on the way in, stash the two addresses arriving in rdi and rsi into local slots, perform one size comparison, then pick which value to return from its result. Piece 03 established that the first two arguments travel in rdi and rsi, and one extra look here reveals what a reference truly is: a reference parameter is an address at the machine level, and what rdi and rsi deliver are the addresses of the two objects themselves. Every difference lies in which instructions move the data and which compare the sizes. The `int` version uses `mov (%rax),%eax` to load a 32-bit integer into a general-purpose register, compares with `cmp %eax,%edx`, branches with `jge`, which jumps only on a signed greater-or-equal, and leaves the return value to deliver sitting in `%eax`. The `double` version swaps in a whole different set: loads go through `movsd (%rax),%xmm1`, so 64-bit doubles enter XMM registers, the comparison is `comisd %xmm1,%xmm0`, which writes its verdict into EFLAGS, the `jbe` after it decides from that verdict whether to jump, and the return value rides in `%xmm0`.

In the source, `a < b ? b : a` was written exactly once, and the compiler, working from `T`'s actual type, wrote that one line as two sets of machine code. Integer comparison and floating-point comparison on x86-64 run on two different instruction sets and two different register sets, the template finishes the dispatch at compile time, and at run time every `call` targets one concrete function, with nothing anywhere consulting a type at run time. This is the physical form of the "code the compiler wrote for you" that our title asks about.

## Nobody Uses It, Nothing Gets Printed

The first property of copying is that it runs on demand. Let's set up the cheapest comparison available: the template definition stays exactly as it was, and `main` makes no call at all:

```cpp
// m2.cpp -- the template definition is present, but never called
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    return 0;   // the definition is here, but never called
}
```

```text
$ g++ -O0 -c m2.cpp -o m2.o
$ nm m2.o
0000000000000000 T main
```

The object file is down to a lone `main`. A template's definition is only a master that prints nothing; where nothing calls it, the compiler emits no machine code at all. Now we add exactly one call line and change nothing else:

```cpp
// m3.cpp -- m2 plus exactly one call line
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    int x = 1, y = 2;
    return max(x, y);   // the one call line we added
}
```

```text
$ g++ -O0 -c m3.cpp -o m3.o
$ nm m3.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W _Z3maxIiET_RS0_S1_

$ nm -C m3.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W int max<int>(int&, int&)
```

The only difference between the two sources is that one call line in `main`, and the symbol for `max<int>` appears out of nothing. Nobody anywhere wrote an explicit `template int max<int>(int&, int&);`; it was the call site that had the compiler deduce `T = int` on its own and generate the instance. Instantiation triggered by use is called implicit instantiation, and implicit means nobody said it out loud: the use itself placed the order. cppreference's [function template page](https://en.cppreference.com/w/cpp/language/function_template) states the condition clearly; we retrieved it on October 13, 2026:

```text
When code refers to a function in context that requires the function definition
to exist, or if the existence of the definition affects the semantics of the
program(since C++11), and this particular function has not been explicitly
instantiated, implicit instantiation occurs.
```

Unpacked: when code refers to the function in a context that requires the definition to exist, and that function has not been explicitly instantiated, implicit instantiation happens. The same page holds a blunter sentence, and m2 is its portrait:

```text
A function template by itself is not a type, or a function. No code is
generated from a source file that contains only template definitions.
```

In plain words again: a function template by itself is neither a type nor a function, and a source file holding only template definitions produces no code. That empty stretch in m2's `nm` output is this sentence made visible.

## Each Prints a Copy, and the Linker Keeps Only One

On-demand copying carries one consequence you can't route around. A template's definition has to live in a header, something you probably filed away as convention back in volume one, and the reason sits inside the compilation pipeline: each .cpp together with the headers it includes is one independent compilation pass, whose jargon name is translation unit, and the TU written in the code comments below names the same thing. For instance, when `a.cpp` needs to instantiate `max<int>`, the compiler must see the template's full text within that pass, and where the full text can't be seen, there's no copying to talk about. So what happens when two translation units both use `max<int>`, each prints a copy, and the names collide at link time? Let's run it:

```cpp
// m4.hpp -- the template definition goes into a header
#ifndef M4_HPP
#define M4_HPP

template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

#endif
```

```cpp
// m4_a.cpp -- this translation unit instantiates max<int>
#include "m4.hpp"

int side_a() {
    int x = 10, y = 20;
    return max(x, y);   // this TU instantiates max<int>
}
```

```cpp
// m4_b.cpp -- this translation unit also instantiates max<int>
#include "m4.hpp"

int side_a();

int main() {
    int x = 1, y = 2;
    return max(x, y) - side_a();   // this TU also instantiates max<int>
}
```

```text
$ g++ -O0 -c m4_a.cpp -o m4_a.o
$ g++ -O0 -c m4_b.cpp -o m4_b.o
$ g++ m4_a.o m4_b.o -o m4
$ nm m4_a.o
                 U __stack_chk_fail
0000000000000000 W _Z3maxIiET_RS0_S1_
0000000000000000 T _Z6side_av

$ nm m4_b.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W _Z3maxIiET_RS0_S1_
                 U _Z6side_av
```

Each of the two object files carries a `W _Z3maxIiET_RS0_S1_` of its own, and both copies of machine code were genuinely generated. On to the linked product:

```text
$ nm m4 | grep -E 'max|main|side_a'
                 U __libc_start_main@GLIBC_2.34
00000000000011b3 T main
0000000000001187 W _Z3maxIiET_RS0_S1_
0000000000001139 T _Z6side_av

$ nm m4 | grep -c '_Z3maxIiET_RS0_S1_'
1
```

In the linked program the symbol appears exactly once, and `grep -c` counts 1. The two same-named definitions raised no error whatsoever; the linker kept one and discarded one. One more detail in the output worth pointing out to you: in the object files the symbol addresses are uniformly 0, while in the linked product `max<int>` was assigned 0x1187 and `main` 0x11b3. Putting addresses in their place is the linker's job to begin with, and the numbers on your own machine will come out different. The mechanism hides in the object file's section information, so let's walk `m4_a.o` by section:

```text
$ readelf -SW m4_a.o | grep -E 'Name|\.text\._Z3max|\.group'
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 1] .group            GROUP           0000000000000000 000040 000008 04     12   5  4
  [ 6] .text._Z3maxIiET_RS0_S1_ PROGBITS        0000000000000000 000096 00002c 00 AXG  0   0  1

$ readelf -g m4_a.o | head -20

COMDAT group section [    1] `.group' [_Z3maxIiET_RS0_S1_] contains 1 sections:
   [Index]    Name
   [    6]   .text._Z3maxIiET_RS0_S1_
```

Both pieces of evidence are in hand. The first is how the sections divide: the machine code of `max<int>` never mixed into the big `.text` section, it received a section of its own, named after the mangled name itself. The second is the `G` flag after the section name and the matching COMDAT group. COMDAT is the object file format's grouping marker, the group's name is the mangled name again, and the linkers' agreement is that same-named groups keep one copy while the rest get discarded whole. Weak symbol `W` and COMDAT come as a set: only because the symbol is marked weak will the linker accept the arrangement of definitions in many places with one kept, and both sides are describing the same arrangement.

The weight of this design shows best against a counterexample. Let's write two ordinary non-template functions and deliberately give them the same name:

```cpp
// n1.cpp
int shared() { return 1; }
// n2.cpp
int shared();
int main() { return shared(); }
int shared() { return 2; }
```

```text
$ nm n1.o
0000000000000000 T _Z6sharedv
$ nm n2.o
0000000000000000 T main
000000000000000c T _Z6sharedv

$ g++ n1.o n2.o -o n_dup
/usr/bin/ld: n2.o: in function `shared()':
n2.cpp:(.text+0xc): multiple definition of `shared()'; n1.o:n1.cpp:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

Both sides are `T`, both are strong symbols, one name has become two copies, and the linker rejects them on the spot, reporting the `multiple definition` you've occasionally run into yourself. Had template instances also traveled the strong-symbol route, the everyday scenario of a header included by two translation units would never link in the first place. Weak symbols plus COMDAT are the precondition for templates working across translation units: print a duplicate and nothing complains, because the deduplication work belongs entirely to the linker. As for how the size grows once the copies pile up, we keep measuring in the next piece.
