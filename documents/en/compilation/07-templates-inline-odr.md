---
chapter: 13
difficulty: intermediate
order: 7
platform: host
reading_time_minutes: 26
tags:
- cpp-modern
- host
- intermediate
title: "Templates, inline, and ODR: Ten Translation Units, One Entity"
description: "Ten translation units each include the same template header, each contributes a weak symbol in nm, and the linker merges them into one entity. Unfold the conditions the ODR attaches to multiple definitions, reproduce in a real experiment the undefined behavior where two different definitions of the same inline function leave the linker silently picking one, then cover what inline really means, a before-and-after comparison of extern template gathering generation into one place, and how a CMake INTERFACE library spells out a header-only library's whole inventory."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/07-templates-inline-odr.md
  source_hash: 6bdd1f4c62f2297416df69ae49b24d3810d1817eed256209159d33263d47c9e7
  translated_at: '2026-09-30T09:34:41+00:00'
  engine: glm
  token_count: 5739
---

# Templates, inline, and ODR: Ten Translation Units, One Entity

At the end of part 03's section on bigger, we banked a question for today: bigger's definition travels with the header, ten translation units all call bigger<int>, so ten identical copies of machine code would lie, one each, in their own .o files — how do they not fight at link time? What unravels it is a provision at the language level: the one definition rule, ODR for short. We mentioned it by name back in part 02, and the formal unfolding was promised for today.

There is also a misunderstanding to clear out of the way: those ten copies of bigger<int> are not an accident of any kind. They are a situation C++ allows on purpose. The language puts it in plain writing: there is a class of definitions that may appear in multiple translation units. The condition attached is exactly one: the definitions in every place must come out the same. Today we unfold the provision, then follow the toolchain's actual practice through: what identity the compiler stamps on such symbols, and with what the linker folds ten copies into one. At the end we will violate the condition once, on purpose, and watch what happens. That scene is far quieter than the on-the-spot error of multiple definition.

As usual, the environment disclosure: all my experiments ran in WSL2, the toolchain is still the GCC 16.2.1 plus binutils 2.47 combination, CMake is 4.4.3, and every segment of output in this piece was actually run. If your versions differ, details like symbols and timings may drift, but the mechanics themselves haven't changed in years.

## The ODR: Three Tiers, from Strict to Lenient

The ODR is no stranger to us by now; today it is our turn to unfold it properly. What it governs, we will go through in three tiers, from strict to lenient. The first tier governs the single translation unit. Inside one unit, a name may carry at most one definition, and the enforcer is the compiler: in part 02, the unguarded no_guard.h got included twice, struct Point was seen twice over, and redefinition was reported on the spot — linking never even got its turn on stage. The second tier governs the whole program: for ordinary functions and global variables, one name gets exactly one definition in the entire program. This tier's enforcer is the linker, and the multiple definition the two helpers ran into in part 05 was its handiwork.

The third tier is where things loosen up. Class definitions, inline functions, templates together with their instantiations, and constexpr functions are all on the lenient list: we may put one definition in every translation unit, and writing it that way is compliant. You have been living off this leniency for a while already. Part 02's header guard blocks the second inclusion within the same unit, but in the situation that actually arrives, several units each including their own copy, struct Point's definition sits in several units at once, one copy each, and everything compiles just fine. What it rides on is precisely the third tier, and part 03's bigger travels the same road.

The leniency is not unconditional. The conditions are written in the standard's [basic.def.odr] section, and we will walk them one by one. On the side of text: the definitions in every place must consist of exactly the same sequence of tokens. A token is the atomic unit the compiler splits source code into, things like variable names, parentheses, and semicolons; comments and whitespace never make it onto the list, so two definitions whose comments differ are none of our concern. On the side of names: every name appearing in the definitions must, under name lookup, land on the same entity. On the side of location: the class and the namespace the definitions live in must also match. Only when all three sides are satisfied do the definitions in multiple places count as copies of one entity. But let a single token differ — one extra semicolon, one constant swapped, one block sheared off by conditional compilation — and it is no longer the same definition. The program thereupon falls into undefined behavior, and from that moment the standard makes no promise of any kind about the program's behavior. How much that sentence weighs is something we will measure today, with real experiments.

## Why Definitions Cannot Stay Out of Headers

You may hold a natural first thought: since multiple definitions bring this much trouble, why not move the template's definition into a single .cpp and give the other units declarations only — wouldn't the whole program be down to one definition? We test the thought with an experiment straight away. I prepared three materials: bigger_decl.h carrying nothing but a declaration; provider.cpp carrying the definition and explicitly instantiating bigger<int>; and consumer.cpp, which includes only the declaration header and calls bigger(3, 4), plus a decl_main.cpp with a main in it. Once they had compiled into .o files, we looked at the symbols on both sides:

```text
$ nm consumer.o | grep bigger
                 U _Z6biggerIiET_S0_S0_
$ nm provider.o | grep bigger
0000000000000000 W _Z6biggerIiET_S0_S0_
```

consumer has no function body in view. We confirmed in part 03 that the compiler is helpless before a template instantiation it cannot generate, so it hangs a U on the name and leaves it at that, while what provider holds in its hand is a W. U met W, and they paired against all expectation; the program really did run:

```text
$ g++ provider.o consumer.o decl_main.o -o decl_app && ./decl_app
2 4
```

But this road does not carry us far. I wrote one more consumer2.cpp, which calls only the double version of bigger, and provider never instantiated it for double:

```text
$ g++ -c consumer2.cpp && g++ provider.o consumer.o consumer2.o decl_main.o -o decl_app2
/usr/bin/ld: consumer2.o: in function `consumer2_calc()':
consumer2.cpp:(.text+0x1d): undefined reference to `double bigger<double>(double, double)'
collect2: error: ld returned 1 exit status
```

The link snapped on the spot. So the single-.cpp approach asks a frightening price: you would have to maintain, by hand, a list of every type combination the whole project uses inside one .cpp. Let one line of that list slip, and the link breaks once more; once the project grows large, we have no grip on it at all. The template's definition must go into the header. That is the root of the matter: whichever unit uses it sees the definition, and the job of generating code falls to that very unit. At the end of part 02's section on macros we left a sentence behind: if what you truly want to write is short logic, the more respectable choices over macros are inline functions and templates. Today we can make good on it: their definitions living in headers is a form the language formally licenses, not a trick the preprocessor plays. The standard library's std::sort and std::vector, living in headers through and through, ride on this same rule.

## The Compiler Leaves Its Mark: Weak Symbols and COMDAT

By what do ten copies of bigger<int> in ten translation units get the linker to recognize them as one entity? By a mark of identity. You have seen this identity in part 05: the weak symbol, the W in nm's output. We move part 03's bigger into a header unchanged and set up ten identical translation units:

```cpp
// bigger.h (wrapped in the header guard from part 02)
template <typename T>
T bigger(T a, T b) { return a > b ? a : b; }
```

The ten units are cut to the same pattern: calc1 in tu1.cpp calls the int version and the double version of bigger once each, while tu2.cpp through tu10.cpp differ only in their numbering. main.cpp calls calc1 through calc10 and prints the sum of the return values. With everything compiled into .o files, we look at the symbol table, both identities on display:

```text
$ nm tu1.o
0000000000000000 T _Z5calc1v
0000000000000000 W _Z6biggerIdET_S0_S0_
0000000000000000 W _Z6biggerIiET_S0_S0_
```

calc is an ordinary function we wrote by hand, and it takes the strong symbol T. The two instantiations of bigger take one uniform identity: W. Lying in tu2.o through tu10.o are the same two W's, so the copies of bigger<int> across the ten .o files come to exactly ten, as expected. GCC has a collective name for entities of this kind — allowed to be defined in many places, merged into one at link time: vague linkage. You will not find the term in the ISO standard; it comes from the Itanium C++ ABI, the C++ binary interface specification GCC follows on Linux, the one we met in part 03 when we read name mangling. From here on, in GCC documentation and linker articles, you will run into it again and again.

W is only the identity in the symbol table. What actually carries the merge out is COMDAT, the mechanism from part 05 where same-named groups keep only one copy. Today we get a look at it ourselves:

```text
$ readelf -g tu1.o

COMDAT group section [    1] `.group' [_Z6biggerIiET_S0_S0_] contains 1 sections:
   [Index]    Name
   [    7]   .text._Z6biggerIiET_S0_S0_
```

The group immediately after is identical, with the group name swapped for the mangled name of bigger<double>; we trimmed it away. Each template instantiation stayed out of the big .text block: it occupies a dedicated section of its own, wearing a group named after the symbol's mangled name. You can treat the group's name as the linker's duplicate-check key: two groups bearing the same name are two copies of one entity.

## Ten Copies In, One Out

We link the ten .o files together with main:

```text
$ g++ tu1.o tu2.o tu3.o tu4.o tu5.o tu6.o tu7.o tu8.o tu9.o tu10.o main.o -o ten_app && ./ten_app
sum=55
```

The program ran properly. We add the return values of calc1 through calc10 together, and the number that comes out is exactly 55. Now the symbols in the executable:

```text
$ nm ten_app | grep bigger
00000000000011b3 W _Z6biggerIdET_S0_S0_
0000000000001197 W _Z6biggerIiET_S0_S0_
```

Ten copies of bigger<int> went into the link; one came out, and bigger<double> traveled the same road. The two surviving lines still carry W: the copy that lived was chosen from among the weak definitions, and the weak-symbol nature followed it into the final program. One more question from us: what happened to the groups?

```text
$ readelf -g ten_app

There are no section groups in this file.
```

Gone. Once the merge was done, the groups disbanded on the spot, and the dedicated sections were folded into the final .text; run readelf over the executable and not a single group is to be found. What the linker does can be said in one sentence: a group name it has never seen gets accepted; a group of the same name arriving again gets discarded whole; what remains is the copy that stood earlier in the link command. One thing must be made plain: the ten definitions are token-for-token identical, so which copy stays makes no difference anyone can see, and the program's results come out the same. That the earlier one stays is only GNU ld's present behavior; no specification promises it in so many words. And the linker never compared the contents of the ten copies, from beginning to end; it never even performed the act of comparing. Whether the definitions really are the same does not concern it; the one responsible is the person writing the program. And what if they are not? That is precisely the experiment we are about to run with our own hands.

## Two Definitions, One Name: The Silent Undefined Behavior

What comes next is the heaviest passage of this piece: we violate the ODR once, deliberately, and watch what happens. The procedure is direct: two translation units, each carrying a different definition of a same-named inline function.

```cpp
// side_a.cpp
inline int answer() { return 10; }
int from_a() { return answer(); }

// side_b.cpp
inline int answer() { return 20; }
int from_b() { return answer(); }
```

odr_main.cpp prints from_a and from_b both. At compile time we turned on -Wall and -Wextra, both of them, and what came back was silence — not one word of warning. We confirmed this in part 05: before the link, the compiler sees only the translation unit in front of it, and side_a and side_b know nothing of each other's existence. answer's identity in the two .o files is symmetric as well; one grep shows an identical W lying in each.

We link it up and run it for you twice in a row: once with the original order, once with the order flipped.

```text
$ g++ odr_main.o side_a.o side_b.o -o odr_app && ./odr_app
from_a=10 from_b=10
$ g++ odr_main.o side_b.o side_a.o -o odr_app2 && ./odr_app2
from_a=20 from_b=20
```

Both rounds run, and the matter stands clear: the program ran along, steady as ever, and which number answer returns everywhere depends entirely on who stood earlier in the link command. The one most worth a second look is the first round's from_b=10. In side_b.cpp's source, return 20 sits written right beside from_b, and yet what came out was 10. A look at the relocations inside side_b.o tells us why:

```text
$ readelf -r side_b.o | grep answer
000000000005  000400000004 R_X86_64_PLT32    0000000000000000 _Z6answerv - 4
000000000020  000300000002 R_X86_64_PC32     0000000000000000 .text._Z6answerv + 0
```

The call inside from_b lands on the first line, the relocation aimed at the symbol _Z6answerv. The second line is filed under .rela.eh_frame; it serves the stack-unwinding tables pointing back to the function's start, and we may leave it alone. At compile time what it recognized was only a name; where the definition lives in this file was never its concern. At link time, side_b.o's answer was discarded as a duplicate group, and what the relocation ended up with is the address of the survivor, side_a. The function "in my own file" — by the time the link arrives, all it holds in its hand is a name, and it points at whoever lived.

Let's state the case rigorously: this experiment shows what happens after the violation, and the answer is that nothing happens; the silence all the way through is real. We have shown you the violation, but its illegal standing has not changed. The standard rules such a program to be undefined behavior, and every number it printed today owes us no explanation. Change the optimization level, change the linker, change the link order once more, and its behavior may put on another face. When this happens in real projects, the person writing the code mostly never meant it; the seed of the trouble is one and the same header file cut into two faces. The header itself is perfectly healthy — two compile commands carrying different macros are all it takes:

```cpp
// flavor.h (wrapped in a header guard)
#include <cstdio>
#ifdef NEW_STATS
inline int bucket_count() { return 16; }
#else
inline int bucket_count() { return 4; }
#endif
inline void report(int v) { std::printf("bucket=%d\n", v); }
```

We compiled old_code.cpp carrying no macro at all, while new_code.cpp's compile carried -DNEW_STATS; link it and run:

```text
$ g++ old_code.o new_code.o face_main.o -o face_app && ./face_app
bucket=4
bucket=4
```

Both printed lines say 4. The definition written with a 16 was compiled into new_code.o beyond doubt, yet at link time it was discarded as a whole group; even new_code.cpp's own call got rebound to 4, while bucket_count's identity in the two .o files, checked with nm, is still the same W. One -D of difference in the build system can produce a program like this, and no tool calls out from the sidelines, because from each unit's own point of view, everyone is innocent.

Is there a tool that can catch part of it? -flto is link-time optimization; we mentioned it in part 03, and the dedicated unfolding lives in part 15 (LTO and Modern Linkers), which lets all translation units meet at link time. The kind where a type wears two faces is catchable, with help from -Wodr, GCC's warning switch dedicated to reporting ODR violations: I prepared two struct Configs with their fields in different orders and verified it, and the warning called it out by name on the spot. But the kind where an inline function carries two bodies: we tried side_a and side_b with -flto -Wall -Wextra, and this time not a single warning arrived; the program ran on and printed from_a=10 from_b=10 as usual. So the line of defense lies in the writing after all: for one entity's definition we reach into one header only. Conditional compilation may cut the implementation — just do not cut out two faces that share a name but not a body.

## The Modern Meaning of inline: Eligibility First, the Inlining Hint Second

The other half that part 02 left open, we close today as well: by what right does an inline function get to live in a header. The inline keyword hands a function two things. The first is eligibility — the eligibility to have its definition appear in multiple translation units. Write an inline function in a header, let dozens of units include it, and each unit generates its own weak symbol and wraps a COMDAT group around it, machinery identical to the template instantiation's, all of which we verified earlier in the section on weak symbols and COMDAT. constexpr functions and member functions defined inside the class are granted implicit inline treatment directly by the language, and the reason is the same: their definitions are born to live in headers.

The second thing is the literal business of inlining: what it gives is a suggestion, and the suggestion is to rewrite the call into the function body itself. We verify with a set of three:

```cpp
// inl.h (wrapped in a header guard)
inline int twice(int x) { return x * 2; }
constexpr int triple(int x) { return x * 3; }
struct Box { int v; int get() const { return v; } };  // defined in-class, implicitly inline
```

We compiled once under -O0, and all three definitions landed their symbols, honest and obedient:

```text
$ g++ -c use_inl.cpp && nm use_inl.o | grep -v U
0000000000000000 T _Z3runi
0000000000000000 W _Z5twicei
0000000000000000 W _Z6triplei
0000000000000000 W _ZNK3Box3getEv
```

run is the caller we wrote. And twice, though marked inline, still takes a call in the assembly:

```text
$ g++ -S use_inl.cpp -o use_inl.s && grep -E "call.*_Z5twicei" use_inl.s
	call	_Z5twicei
```

We switched to -O2 and compiled again; the three W's are gone without leaving a shadow:

```text
$ g++ -O2 -c use_inl.cpp -o use_inl_O2.o && nm use_inl_O2.o
0000000000000000 T _Z3runi
```

Look closely at this pair. inline was written, the optimization switch was not pulled, and of the three function bodies not one did we see inlined — they were generated as they stood. And the reverse holds just as truly: an ordinary function carrying no inline still gets spread open whenever the optimizer wishes, and the verdict stays in the compiler's hands from beginning to end. So inline's position in modern C++ is clear: it serves the definition's move into the header; it is not a plea of "compiler, unfold me."

## extern template: Gathering Generation into One Place

The ten W's do merge into one at link time, but at compile time the ten copies were still generated each on its own: every translation unit had to instantiate its own bigger<double>. When the template is small, this bit of repeated labor hardly counts; once the template is large, the labor multiplies with the number of translation units. C++11 handed us a way to gather it. The spelling is extern template, the formal name explicit instantiation declaration, and the meaning fits in one sentence: do not generate this entity here — it already exists elsewhere.

We swapped in a template with some real weight for the comparison. heavy_median<T> runs sort, unique, and reverse inside, and we keep its definition in the header as always. Variant one leaves everything alone: the ten translation units (TUs) simply use it, and the directory it compiles into is called noext. Variant two adds one line at the end of the header, `extern template double heavy_median<double>(std::vector<double>);`, and a separate instantiate.cpp writes down the explicit instantiation definition; the directory it compiles into is called withext. The symbols on both sides, set side by side:

```text
$ nm noext/tu1.o | grep heavy_median
0000000000000000 W _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
$ nm withext/tu1.o | grep heavy_median
                 U _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
$ nm withext/instantiate.o | grep heavy_median
0000000000000000 W _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
```

Variant one is the familiar old sight: a W lies in every .o. On variant two's side, tu1.o is down to a single U for heavy_median<double>; the compiler was stopped by that one line of declaration, the entity moved into instantiate.o, and its identity is still W. You see: generating in one place uses the same weak-symbol machinery as ever. extern template only gathers the site of generation from everywhere into one appointed place; not one word of the link-time merging rules changed. Both variants linked successfully and both outputs agree, and the real difference is in size: noext's tu1.o is 46976 bytes, while withext's tu1.o is only 14376 bytes.

We compiled the ten TUs one by one, timing each variant three rounds on WSL2: variant one took 2.5 to 2.9 seconds, variant two took 1.85 to 1.96 seconds, plus a little over 0.2 seconds for instantiate.cpp on its own. For a template of real weight, what gets saved is about half a second. And the heavier the template and the more translation units there are, the more pronounced this gap gets.

The cost must be told in full as well. extern template moves the job of generating elsewhere; it does not cancel the job. If all ten TUs declare it exists elsewhere and nobody ends up doing the generating, the linker reports the error all the same:

```text
$ g++ lonely.cpp -o lonely_app
/usr/bin/ld: /tmp/ccGYaLA0.o: in function `main':
lonely.cpp:(.text+0x49): undefined reference to `double heavy_median<double>(std::vector<double, std::allocator<double> >)'
collect2: error: ld returned 1 exit status
```

Where does it better the primitive scheme of declarations plus a hand-kept list? The definition stays in the header. A type combination off the list is gathered by nobody; the unit using it implicitly instantiates on its own as before, and so no broken link occurs. I really ran this: a unit containing only the extern template version of the header used heavy_median<float>, generated a W of its own, and the link passed as usual. The boundary between gathered and not gathered is drawn, precisely, by that one line of declaration. The standard library uses it too: libstdc++'s bits/basic_string.tcc carries `extern template class basic_string<char>;`, precisely so that not every translation unit instantiates a string of its own. And one fresh footnote: that line carries a `__cplusplus <= 202002L` condition, and from C++23 on libstdc++ removed it, letting string return to on-demand generation. The gains and the costs are both out in the open, and the library authors are weighing the two ends.

## The CMake Perspective: Writing a Header-Only Library as INTERFACE

If today's bigger.h kept growing, it would become a header-only library: the whole inventory lives in include/, without a shadow of a .cpp anywhere. CMake keeps a dedicated target type ready for just such libraries:

```cmake
cmake_minimum_required(VERSION 3.26)
project(iface_demo CXX)
add_library(mathkit INTERFACE)
target_include_directories(mathkit INTERFACE mathkit/include)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mathkit)
```

The type we wrote is INTERFACE, and what it means is simple to say: the target produces no build output of its own; the point of its existence is to pass usage requirements on to the targets that link it — usage requirements is CMake's term. The requirement that lands in front of us is exactly one: add mathkit/include to the include path. mathkit.h holds one inline function plus one template; app includes it, links it, and the build and run came out like this:

```text
$ cmake --build build
[1/2] Building CXX object CMakeFiles/app.dir/main.cpp.o
[2/2] Linking CXX executable app
$ ./build/app
42 40
```

We put the tools through one more acceptance round. What we examined is mathkit's name in the build directory: find went in and came back with nothing — no .a, no .so, no .o in sight; the only linked artifact in the whole build is app. And whether the requirement passed outward landed, we look at next: app's compile command carries one extra -I pointing at mathkit/include, solid and real. We named the Ninja generator when configuring the project, so the build script CMake wrote is build.ninja; open it and you will see that -I line. INTERFACE and the OBJECT library from part 04 stand at opposite ends. The OBJECT library puts out .o files but does no packaging, while INTERFACE has no .o at all; what it passes outward is paths only. STATIC and SHARED stand in between, their output also packaged into libraries. When you write add_library and pick a type, just think through where the entities are to be generated, and the answer comes out on its own.

## The Next Step in Reuse: From a Copy per Program to One Shared Copy

Part 06 packed a pile of .o files into a static library: whoever links it carries one more copy in the executable. Today we rounded out the other half of the story: where the legality comes from when a definition follows a header into ten translation units, and how the linker merges the ten entities into one. Every link in this chain you have verified with your own hands, and the silence under violation you have seen with your own eyes. But up to this point, the shape of reuse has always been one copy per program: with a hundred programs using bigger<int>, a hundred copies lie on the disk, and a hundred lie in memory.

Then by what right do libstdc++ and glibc get shared by untold numbers of programs? At the end of part 05 we banked that question with part 08 (Dynamic Libraries). The direction of the answer: let the library occupy a file of its own while the program hangs a reference off it. The build period does the registering, the run period does the connecting, and the machinery on both sides is part 08's main subject. As for today's ten .o files, keep them for a rerun or throw them away and write them again: either way, a few minutes' work.
