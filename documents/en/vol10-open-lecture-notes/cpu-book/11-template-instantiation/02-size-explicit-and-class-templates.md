---
title: "Template Instantiation Compared: Size, the Manual Switches, and Class Templates"
chapter: 11
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Types go from 1 to 8 and text grows from 263 to 1108 bytes. Explicit instantiation and extern template control whether a copy is generated, and class templates do on-demand down to the member level"
reading_time_minutes: 10
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/11-template-instantiation/02-size-explicit-and-class-templates.md
  source_hash: a332751c953ee8b905b0a43ab35d53f2e10efd35385eb7c80e839ed286cb07a7
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Template Instantiation Compared: Size, the Manual Switches, and Class Templates

The environment, the tools, and how to read the listings are all the same as in the [previous piece](01-on-demand-copies-and-linker-dedup.md); the compile commands still carry `-O0`, so follow them if you want to rerun anything. At the end of the previous piece, the `max<int>` that two translation units had both instantiated was left with a single copy in the linker's hands. This piece keeps going from there: how the size grows as the copies multiply, how the pair of manual switches, explicit instantiation and `extern template`, get flipped, and by what unit a class template gets instantiated.

## The Size Counts Every Copy

Copies are made per type, and every type gets its own piece of machine code, so as the types pile up the object file should grow piece by piece. We instantiated the same template for 1, 2, 4, and 8 types and measured the real numbers with `size`. The differences between the four variants are purely mechanical: the template part is word-for-word identical, and `main` holds variables and calls laid out in type order, with the variant numbered n stopping at the nth type. The types come in the order int, double, char, long, followed by float, short, unsigned, and finally long long. Here is the two-type version in full:

```cpp
// m5_2.cpp -- the same template, instantiated for two types
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    int x0 = 1, y0 = 2;
    (void)max(x0, y0);
    double x1 = 1, y1 = 2;
    (void)max(x1, y1);
    return 0;
}
```

The four-type and eight-type versions keep walking down the same list in the same order, adding variables and calls until all eight types are in. Here is the eight-type version in full:

```cpp
// m5_8.cpp -- the same template, instantiated for eight types
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    int x0 = 1, y0 = 2;
    (void)max(x0, y0);
    double x1 = 1, y1 = 2;
    (void)max(x1, y1);
    char x2 = 1, y2 = 2;
    (void)max(x2, y2);
    long x3 = 1, y3 = 2;
    (void)max(x3, y3);
    float x4 = 1, y4 = 2;
    (void)max(x4, y4);
    short x5 = 1, y5 = 2;
    (void)max(x5, y5);
    unsigned x6 = 1, y6 = 2;
    (void)max(x6, y6);
    long long x7 = 1, y7 = 2;
    (void)max(x7, y7);
    return 0;
}
```

The compile and measurement commands, with their output:

```text
$ for n in 1 2 4 8; do g++ -O0 -c m5_${n}.cpp -o m5_${n}.o; done
$ size m5_1.o m5_2.o m5_4.o m5_8.o
   text	   data	    bss	    dec	    hex	filename
    263	      0	      0	    263	    107	m5_1.o
    410	      0	      0	    410	    19a	m5_2.o
    633	      0	      0	    633	    279	m5_4.o
   1108	      0	      0	   1108	    454	m5_8.o

$ for n in 1 2 4 8; do printf 'm5_%d.o: ' $n; nm m5_${n}.o | grep -c ' W _Z3max'; done
m5_1.o: 1
m5_2.o: 2
m5_4.o: 4
m5_8.o: 8

$ stat -c '%s  %n' m5_1.o m5_2.o m5_4.o m5_8.o
1792  m5_1.o
2360  m5_2.o
3136  m5_4.o
4760  m5_8.o
```

Counting the symbols shows the tally tracking the type count strictly: instantiate for that many types and `nm` gives back exactly that many `W` lines, and the size climbs along. The code section's text grows from 263 to 1108 bytes, and the file as a whole grows from 1792 to 4760 bytes. The growth can even be pinned on the individual copies, so let's read the eight-type version section by section:

```text
$ size -A m5_8.o | grep -E 'section|\.text\._Z3max'
section                    size   addr
.text._Z3maxIiET_RS0_S1_     44      0
.text._Z3maxIdET_RS0_S1_     54      0
.text._Z3maxIcET_RS0_S1_     48      0
.text._Z3maxIlET_RS0_S1_     49      0
.text._Z3maxIfET_RS0_S1_     53      0
.text._Z3maxIsET_RS0_S1_     49      0
.text._Z3maxIjET_RS0_S1_     44      0
.text._Z3maxIxET_RS0_S1_     49      0
```

Read the output and each of the eight instantiations owns its own little section, the individual copies running from 44 to 54 bytes and together accounting for 390 bytes, while the rest of the growth comes from `main` itself getting longer, more variables and more calls. One detail ties back to the assembly from earlier: the floating-point sections measure 53 and 54 bytes against the int version's 44, because `movsd` and `comisd` simply encode longer than `mov` and `cmp`. What a template does is exactly this, a compile-time expansion producing one independent piece of machine code per type, not one type-parameterized piece of code dispatching at runtime, so when the types multiply, the code section grows roughly by the number of copies.

One condition planted back in the environment section of the previous piece has to come back now, so the story about optimization levels can be told in full. Every number above is a straight `-O0` reading, measuring the shape of copying itself. Compile the same eight-type source at `-O2` instead and the measured text is only 99 bytes, and in the `-O2` object file built from the m1 code, `nm` shows no `max` symbol at all, nothing left but `T main`, the small functions inlined wholesale into their call sites. So here is the honest version: 44 bytes is the cost of one copy at the unoptimized level, and extrapolating from it into "what percentage templates bloat your program" gives a number that will not stand up. These two `-O2` measurements also cover only our few small instances; what becomes of large templates at optimized levels, we have not measured. The deduplication mechanism does not care about instance size, the weak-symbol-plus-COMDAT arrangement is the same for every copy, and the earlier evidence of the linker keeping a single copy is untouched by any of this.

## Explicit Instantiation: Ordering a Copy and Forbidding One

For when copying switches on and when it stops, C++ provides two manual switches. We set up two translation units, one carrying the explicit instantiation definition, one carrying the `extern template` declaration:

```cpp
// m6_def.cpp -- the TU holding the explicit instantiation definition: max<int>'s machine code is generated here
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

template int max<int>(int&, int&);   // explicit instantiation definition
```

```cpp
// m6_use.cpp -- the TU declaring extern template: nothing generated locally, the instance comes from elsewhere
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

extern template int max<int>(int&, int&);   // explicit instantiation declaration

int main() {
    int x = 1, y = 2;
    return max(x, y);   // the template definition is here and the call is here, but extern template suppresses local instantiation
}
```

```text
$ g++ -O0 -c m6_def.cpp -o m6_def.o
$ g++ -O0 -c m6_use.cpp -o m6_use.o
$ g++ m6_def.o m6_use.o -o m6
$ nm m6_def.o
0000000000000000 W _Z3maxIiET_RS0_S1_

$ nm m6_use.o
0000000000000000 T main
                 U __stack_chk_fail
                 U _Z3maxIiET_RS0_S1_

$ nm m6 | grep -E 'max|main'
                 U __libc_start_main@GLIBC_2.34
0000000000001165 T main
0000000000001139 W _Z3maxIiET_RS0_S1_
```

Put m6_use next to the m3 from the previous piece and you have the most rewarding comparison these two pieces offer. The two sources are nearly identical, a template definition plus one call, and the only difference is the extra `extern template` line; on that one line the symbol flips from `W` to `U`. `W` means a definition exists locally, `U` means it does not and the linker has to find one in some other translation unit. One declaration suppresses the local implicit instantiation. On the other side, the explicit instantiation definition in m6_def makes its translation unit generate the instance unconditionally, and that file does not contain a single line of calling code. The final link holds one copy of the symbol, and running the program exits with code 2, exactly the result of `max(1,2)`.

Here is a point even plenty of older references get wrong, so let's get it exactly right with measurements. The symbol an explicit instantiation definition generates is also `W`, not the strong `T`; under GCC 16.2.1 it is shaped exactly like an implicit instantiation. So the precise statement goes: explicit instantiation guarantees generation, `extern template` guarantees non-generation, the two switches govern whether a definition exists, and in both cases the symbol comes out a weak symbol plus COMDAT. Why is the explicit one weak too? Because other translation units may hold implicit instantiations of the same name at the same time, and as long as those units carry no `extern template` declaration, the compiler's uniform practice for such entities is to emit them as weak symbols and hand the name collisions to the linker for deduplication. The Itanium ABI puts the entities allowed to appear repeatedly across translation units into one category, and the category's name is vague linkage; weak symbol plus COMDAT is the shape this kind of entity takes in an object file.

The manual switches earn their keep in compile time: the practice in large projects is to concentrate the instantiations into one .cpp and hand the other translation units `extern template` declarations, and the duplicated generation work is saved. When this pays off, [the piece on controlling template instantiation in vol4's metaprogramming subvolume](../../../vol4-advanced/vol3-metaprogramming-cpp20-23/07-template-instantiation-control.md) gives it a complete evaluation; in a small project the gain is too small to measure, only the accumulation in a large project shows, and today's business is the mechanism at the symbol layer.

## Class Templates: Implicit Goes by the Member, Explicit Takes the Whole Class

Copying a class template raises one question beyond function templates: a class comes with a pile of member functions, so does instantiation produce the whole class at once, or only whichever member gets used? We build a `Box` with two members and call only its `get()` from `main`:

```cpp
// m7.cpp -- class template: only one member gets called
template <typename T>
class Box {
    T v_;
public:
    Box(T v) : v_(v) {}          // the constructor
    T get() const { return v_; } // the member main calls
    T twice() const { return v_ + v_; }  // the member main never calls
};

int main() {
    Box<int> b(40);        // needs the type Box<int>
    return b.get();        // only get() gets called
}
```

```text
$ g++ -O0 -c m7.cpp -o m7.o
$ nm m7.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W _ZN3BoxIiEC1Ei
0000000000000000 W _ZN3BoxIiEC2Ei
0000000000000000 n _ZN3BoxIiEC5Ei
0000000000000000 W _ZNK3BoxIiE3getEv

$ nm -C m7.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W Box<int>::Box(int)
0000000000000000 W Box<int>::Box(int)
0000000000000000 n Box<int>::Box(int)
0000000000000000 W Box<int>::get() const
```

The symbol for `twice()`, `_ZNK3BoxIiE6twiceEv`, is nowhere in sight. `main` built a `Box<int>` object and called `get()`, and the object file generated only the constructor and `get()`. The smallest unit of class template instantiation is the member function: needing the type does not mean needing every member it has. Swap the call site over to `twice()` and compile once more; `m7b.cpp` differs from the file above only in `b.get()` becoming `b.twice()`:

```text
$ g++ -O0 -c m7b.cpp -o m7b.o
$ nm -C m7b.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W Box<int>::Box(int)
0000000000000000 W Box<int>::Box(int)
0000000000000000 n Box<int>::Box(int)
0000000000000000 W Box<int>::twice() const
```

The symbol switched to `twice()`, and this time `get()` is the one missing. Set the two `nm` outputs side by side and on-demand holds at the member level too.

Those three constructor lines in the output deserve a moment, so they do not muddy the main thread. In topic 05 we met C1 and C2 as the two names the ABI gives one and the same constructor code, C1 handling complete-object construction and C2 base-subobject construction, and outside inheritance scenarios both names refer to the same instructions. Let's have `nm -S` measure the sizes:

```text
$ nm -S m7.o
0000000000000000 000000000000004b T main
                 U __stack_chk_fail
0000000000000000 0000000000000017 W _ZN3BoxIiEC1Ei
0000000000000000 0000000000000017 W _ZN3BoxIiEC2Ei
0000000000000000 n _ZN3BoxIiEC5Ei
0000000000000000 0000000000000010 W _ZNK3BoxIiE3getEv
```

C1 and C2 both measure 0x17 bytes, two names for one body of code. The lowercase `n` C5 is a local alias marker with no size, one that takes no part in linking across translation units, and you can ignore it while scanning the output. So for judging member-level on-demand instantiation, ordinary member functions make the cleanest evidence: the comings and goings of `get()` and `twice()` are the whole story.

The standard addresses member-level on-demand in plain words too. From the implicit instantiation section of [the class template page](https://en.cppreference.com/w/cpp/language/class_template), retrieved October 13, 2026:

```text
This applies to the members of the class template: unless the member is used
in the program, it is not instantiated, and does not require a definition.
```

A member the program never uses is not instantiated, and you can even skip giving it a definition. The same section also states the trigger: constructing an object of the type triggers instantiation, while declaring a pointer to it does not, because the former needs the complete type and the latter needs only a pointer. m7 constructed an object, so the constructor and `get()` both showed up; the pointer usage would not have instantiated `Box<int>` fully at all.

That closes the implicit side. Now the explicit side, the class version, and once the two outputs sit next to each other the difference is all out in the open:

```cpp
// m6c.hpp
#ifndef M6C_HPP
#define M6C_HPP

template <typename T>
class Widget {
    T v_;
public:
    Widget(T v) : v_(v) {}
    T value() const { return v_; }
    T doubled() const { return v_ + v_; }
};

#endif
```

```cpp
// m6c_def.cpp -- explicit instantiation definition of the class template: every member of Widget<int> is generated here
#include "m6c.hpp"

template class Widget<int>;   // explicitly instantiate the whole class
```

```cpp
// m6c_use.cpp -- the TU with the extern template declaration: no members of Widget<int> generated locally
#include "m6c.hpp"

extern template class Widget<int>;   // explicit instantiation declaration (the whole class)

int main() {
    Widget<int> w(21);
    return w.value();   // an ordinary call, but no machine code generated locally
}
```

```text
$ g++ -O0 -c m6c_def.cpp -o m6c_def.o
$ g++ -O0 -c m6c_use.cpp -o m6c_use.o
$ g++ m6c_def.o m6c_use.o -o m6c
$ nm -C m6c_def.o
0000000000000000 W Widget<int>::Widget(int)
0000000000000000 W Widget<int>::Widget(int)
0000000000000000 n Widget<int>::Widget(int)
0000000000000000 W Widget<int>::value() const
0000000000000000 W Widget<int>::doubled() const

$ nm -C m6c_use.o
0000000000000000 T main
                 U __stack_chk_fail
                 U Widget<int>::Widget(int)
                 U Widget<int>::value() const

$ nm -C m6c | grep -i widget
000000000000113a W Widget<int>::Widget(int)
000000000000113a W Widget<int>::Widget(int)
0000000000001152 W Widget<int>::value() const
0000000000001162 W Widget<int>::doubled() const
```

Now the explicit side's output. The effect of writing `template class Widget<int>;` is that every member got generated, including `doubled()`, which nobody calls from start to finish. In m7 the never-called `twice()` was absent, and in m6c the never-called `doubled()` is present, and that is exactly the difference between the two directions: implicit instantiation asks whether this translation unit uses a member and prints only the ones used, while explicit instantiation does no picking at all and prints the entire class. The `extern template` output is just as clean: not one member was generated locally, only `U` references to the members that did get used, and `doubled()` does not even have a reference, because nothing anywhere ever calls it. The linked binary holds one copy of each member, and running the program exits with code 21, exactly the result of `Widget<int>(21).value()`.

## The Three Rules: On Demand, Per Type, Deduplicated

Time to gather up the experiments from these two pieces. Copying starts from on-demand: a template definition sitting there produces no code, and a copy appears only once a call site shows up. m2 and m3 differ by one line, and a class template's on-demand reaches all the way down to members, the never-called `twice()` not even getting a symbol. Copies are made per type: each type gets its own independent machine code, the mangled name carrying the type's tail, `IiE` for the int version and `IdE` for the double version, and the instructions come out typed as well, the integer `cmp` against the floating-point `comisd`; more copies means more size, with text growing from 263 to 1108. More types also means name collisions stop being avoidable, and the linker knows how to handle them: same-named instantiations across translation units are weak symbols plus COMDAT, only one copy survives, and that is the 1 `grep -c` counted. Explicit instantiation and `extern template` are the switches over whether copying happens, they do not change the symbol's shape, and the real difference is `W` versus `U`.

For the language-level account of templates, function templates get theirs in [vol1's piece on function templates](../../../vol1-fundamentals/ch09/01-function-templates.md), class templates and specializations in the later pieces of the same volume; the whys of the semantics live over there. For when the instantiation-control tools are worth reaching for, go to [the piece on instantiation control in vol4's metaprogramming subvolume](../../../vol4-advanced/vol3-metaprogramming-cpp20-23/07-template-instantiation-control.md). In this series, symbols and name mangling got started in [the piece on function calls in 02](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md), and [the piece on classes in 05](../05-classes-in-assembly/01-this-pointer-and-member-offsets.md) added how class names are spelled; today the template half of that story got connected: how one source file becomes a stack of functions, which you have now watched end to end in `nm` and `objdump`.

The cheapest self-check goes to you, to close this out. Compile m3.cpp once and note the `W` line in its `nm` output. Then add `extern template int max<int>(int&, int&);` after the template definition and compile again, compiling to object files only, no linking. The old `W` turns into `U` and the local copying stops; the weight of one declaration is written in the difference between those two letters.
