---
chapter: 13
difficulty: intermediate
order: 7
platform: host
reading_time_minutes: 17
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 模板
title: "Templates, inline, and ODR: Ten Translation Units, One Entity"
description: 'Ten translation units each include the same template header and each contribute a weak symbol in nm; after linking they merge into one entity. We unfold the conditions the ODR attaches to multiple definitions, reproduce in a real experiment the undefined behavior where two different definitions of the same inline function leave the linker silently picking one, then cover what inline really means, a before-and-after comparison of extern template gathering generation into one place, and how a CMake INTERFACE library expresses the whole inventory of a header-only library.'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/07-templates-inline-odr.md
  source_hash: 8bbd67f24197955ac2420aa0544d5993c7e63be5c86b20dcc3872d8bea4ca1ed
  translated_at: '2026-10-13T04:04:40+00:00'
  engine: anthropic
  token_count: 5234
---

# Templates, inline, and ODR: Ten Translation Units, One Entity

When the compiler article wrapped up its section on `bigger`, we stashed one question away for today: `bigger`'s definition travels with the header, ten translation units all call `bigger<int>`, and ten identical copies of the machine code each take a nap in their own `.o` file — why don't they fight at link time? The way out is a rule at the language level: the One Definition Rule, ODR for us, the single definition rule if you translate it literally. We named it in the preprocessing article and promised the formal unfolding for today.

We also need to clear up a misconception: the ten copies of `bigger<int>` are not some accident — they are a situation C++ deliberately allows. The language has explicit provisions saying that one class of definitions may appear in multiple translation units, and it attaches exactly one condition: the definitions in every place must be identical. Today we unfold that provision, then walk through what the toolchain actually does: what identity the compiler stamps on such symbols, and what the linker uses to merge ten copies into one. At the end we deliberately violate the condition once to see what happens, and that scene is far quieter than the immediate `multiple definition` error.

The usual environment note: my experiments all ran in WSL2, the toolchain is still GCC 16.2.1 paired with binutils 2.47, CMake is 4.4.3, and every piece of output in this article is real. If your versions differ, details like symbols and timings may shift a little; the mechanisms themselves have not changed in years.

## ODR: Three Tiers of Requirements, Strict to Lenient

We are no strangers to the ODR; today it is our turn to unfold it formally. What it governs, we will cover in three tiers from strictest to most lenient. The first tier governs a single translation unit: inside the unit, a given name has at most one definition, and the compiler is the enforcer — in the preprocessing article, the unguarded `no_guard.h` was included twice, `struct Point` was seen two times, and `redefinition` was reported on the spot; linking never even got a chance to appear. The second tier governs the whole program: for ordinary functions and global variables, a name's definition exists exactly once across the program. The enforcer at this tier is the linker — the `multiple definition` that two copies of `helper` collided into in the linking article was its handiwork.

The third tier is where things relax. Class definitions, inline functions, templates together with their instantiations, constexpr functions — all sit on the relaxed list: we may place one definition in each translation unit, and that is compliant. You have in fact been using this relaxation all along: the include guard in the preprocessing article only blocks a second inclusion within the same unit, but once multiple units each include their own copy, the definition of `struct Point` lives once in each of them and everything still compiles — that is precisely the third tier's relaxation at work, and `bigger` in the compiler article took the same road.

The relaxation is not unconditional; the conditions are written in the standard's [basic.def.odr] section, and we will go through them one by one. On the text side: the definitions in each place must consist of exactly the same sequence of tokens. A token is the atomic unit the compiler splits source code into — variable names, brackets, semicolons, things like that — and comments and whitespace never make the list, so if the comments in two definitions differ, we need not care. On the name side: names appearing in the definition must resolve, after lookup, to the same entity. On the location side: the class and namespace the definition sits in must match too. When all three sides hold, the multiple definitions count as multiple copies of the same entity. But miss by a single token — one extra semicolon, one changed constant, one block cut away by conditional compilation — and it is no longer the same definition, and the program falls into undefined behavior, from which point the standard makes no promises whatsoever about the program's behavior. We will weigh the full force of that sentence with a real experiment today.

## Why Definitions Cannot Avoid Header Files

You may have an intuitive idea: since multiple definitions cause so much trouble, why not tuck the template's definition into one `.cpp`, give every other unit just a declaration, and the whole program ends up with a single definition? Let's test it directly. I prepared three pieces of material: a `bigger_decl.h` with only a declaration, a `provider.cpp` carrying the definition and explicitly instantiating `bigger<int>`, a `consumer.cpp` that includes only the declaration header and calls `bigger(3, 4)`, plus a `decl_main.cpp` with `main`. Once they are compiled into `.o` files, we look at the symbols on both sides:

```text
$ nm consumer.o | grep bigger
                 U _Z6biggerIiET_S0_S0_
$ nm provider.o | grep bigger
0000000000000000 W _Z6biggerIiET_S0_S0_
```

`consumer` has no function body in sight — we confirmed in the compiler article that the compiler is helpless against a template instance it cannot generate, so it hangs up a `U` and calls it done, while what `provider` holds is a `W`. And yet `U` met `W`, they matched, and the program really ran:

```text
$ g++ provider.o consumer.o decl_main.o -o decl_app && ./decl_app
2 4
```

But this road does not take us far. I wrote another `consumer2.cpp` that only calls the `double` version of `bigger`, and `provider` never instantiated for `double`:

```text
$ g++ -c consumer2.cpp && g++ provider.o consumer.o consumer2.o decl_main.o -o decl_app2
/usr/bin/ld: consumer2.o: in function `consumer2_calc()':
consumer2.cpp:(.text+0x1d): undefined reference to `double bigger<double>(double, double)'
collect2: error: ld returned 1 exit status
```

The link broke on the spot. So the tuck-it-into-one-.cpp route costs a frightening amount: you have to hand-maintain, in one `.cpp`, a list of every type combination the whole project uses, and each missing line in the list means one more broken link — and the bigger the project, the less confident we get. Template definitions must go into header files, and this is the root of it: the units that use it can see the definition, and the generation work belongs to them too. At the end of the macro section in the preprocessing article we left one sentence: if what you really want to write is short logic, the more respectable choices over macros are inline functions and templates. Today we can make good on that: their definitions going into headers is officially permitted by the language, not a trick the preprocessor plays. The `std::sort` and `std::vector` filling standard-library headers ride on the same rule.

## The Compiler Stamps an Identity: Weak Symbols and COMDAT

What makes the linker recognize the ten copies of `bigger<int>` in ten translation units as the same entity? An identity stamp. You have seen this identity in the linking article: the weak symbol, the `W` in `nm` output. Let's move `bigger` from the compiler article into a header unchanged, plus ten identical translation units:

```cpp
// bigger.h (wrapped in the include guard from the preprocessing article)
template <typename T>
T bigger(T a, T b) { return a > b ? a : b; }
```

The ten units are identical: `calc1` in `tu1.cpp` calls both the `int` and `double` versions of `bigger` once each, and `tu2.cpp` through `tu10.cpp` differ only in numbering. `main.cpp` calls `calc1` through `calc10` in turn and prints the total of the return values. Once everything is compiled into `.o` files, we look at both symbol tables:

```text
$ nm tu1.o
0000000000000000 T _Z5calc1v
0000000000000000 W _Z6biggerIdET_S0_S0_
0000000000000000 W _Z6biggerIiET_S0_S0_
```

`calc` is an ordinary function we wrote ourselves and gets the strong symbol `T`. The two `bigger` instantiations all carry the identity `W`, and `tu2.o` through `tu10.o` hold the same two `W`'s — ten copies of `bigger<int>` across the ten `.o` files, exactly as predicted. GCC lumps entities of this kind, which may be defined in many places and merge into one at link time, under the name vague linkage. You will not find the term in the ISO standard; it comes from the Itanium C++ ABI, the C++ binary interface specification GCC follows on Linux — we met it in the compiler article's name mangling section. You will run into it again and again in GCC docs and linker write-ups.

`W` is only the identity in the symbol table; what actually carries out the merge is COMDAT, which the linking article described as keeping one copy per same-named group. Today let's look at one with our own eyes:

```text
$ readelf -g tu1.o

COMDAT group section [    1] `.group' [_Z6biggerIiET_S0_S0_] contains 1 sections:
   [Index]    Name
   [    7]   .text._Z6biggerIiET_S0_S0_
```

The second group right after is identical except that its group name is the mangled name of `bigger<double>`; we trimmed it. Each template instance does not mix into the big `.text` — it occupies a section of its own, wrapped in a group named after its mangled symbol. Treat the group's name as what the linker deduplicates on: two groups with the same name are two copies of the same entity.

## Ten In, One Out

Let's link the ten `.o` files and `main` together:

```text
$ g++ tu1.o tu2.o tu3.o tu4.o tu5.o tu6.o tu7.o tu8.o tu9.o tu10.o main.o -o ten_app && ./ten_app
sum=55
```

The program runs fine: adding the return values of `calc1` through `calc10` gives exactly 55. Now the symbols in the executable:

```text
$ nm ten_app | grep bigger
00000000000011b3 W _Z6biggerIdET_S0_S0_
0000000000001197 W _Z6biggerIiET_S0_S0_
```

Ten copies of `bigger<int>` went in; only one came out, and `bigger<double>` took the same road. The two surviving entries are still marked `W` — the surviving copy was picked from the weak definitions, and the weak symbol's nature followed it into the final program. One more question: what about the groups?

```text
$ readelf -g ten_app

There are no section groups in this file.
```

Gone. Once the merge completes, the groups dissolve with it and their dedicated sections fold into the final `.text` — dig through the executable with `readelf` and not a single group remains. What the linker does, we can say in one sentence: a group name it has not seen gets taken in; the next group with the same name gets dropped whole, and the one kept is whichever came first in the link command. One thing we must get straight: with ten token-identical definitions, it makes no observable difference who survives — the program runs the same either way. Keeping the copy that comes first is merely GNU ld's current behavior; the specification does not promise it in writing. And the linker never compared the ten copies' contents — it never even performed a comparison. Whether the definitions really are identical is not its business; that responsibility sits with the person writing the program. And what if they are not identical? That is exactly the experiment we are about to run by hand.

## Two Definitions, One Name: Silent Undefined Behavior

Next comes the heaviest passage of the whole article: we violate the ODR on purpose and watch what happens. The method is direct: we open two translation units and give an inline function of the same name a different definition in each.

```cpp
// side_a.cpp
inline int answer() { return 10; }
int from_a() { return answer(); }

// side_b.cpp
inline int answer() { return 20; }
int from_b() { return answer(); }
```

`odr_main.cpp` prints both `from_a` and `from_b`. At compile time we turned on `-Wall` and `-Wextra` together and got silence back — not one character of warning. The linking article confirmed that the pre-link compiler only sees the translation unit in front of it; `side_a` and `side_b` know nothing of each other's existence. And `answer`'s identity in the two `.o` files is symmetric: one grep, and each side holds one identical `W`.

Let's link it up and run both rounds for you: once in the original order, once in the reversed order.

```text
$ g++ odr_main.o side_a.o side_b.o -o odr_app && ./odr_app
from_a=10 from_b=10
$ g++ odr_main.o side_b.o side_a.o -o odr_app2 && ./odr_app2
from_a=20 from_b=20
```

Both rounds are done and the picture is clear: the program runs comfortably, and what `answer` returns everywhere depends entirely on who came first in the link command. The line worth a second look is the first round's `from_b=10`. In `side_b.cpp`'s source, `return 20` sits right beside `from_b`, yet the run printed 10. The relocations in `side_b.o` tell us why:

```text
$ readelf -r side_b.o | grep answer
000000000005  000400000004 R_X86_64_PLT32    0000000000000000 _Z6answerv - 4
000000000020  000300000002 R_X86_64_PC32     0000000000000000 .text._Z6answerv + 0
```

The call in `from_b` lands on the first relocation, aimed at the symbol `_Z6answerv`. The second hangs under `.rela.eh_frame` — the stack unwinding table pointing back to the function's start; we can ignore it. At compile time it only knew a name; where this file's own definition lived, it did not care. At link time, `answer` in `side_b.o` was dropped as a duplicate group, and what the relocation finally got filled in with was the survivor `side_a`'s address. So much for "the function in my own file": by the time the link step arrives, all it holds is a name, and it points at whoever survived.

Let's state this precisely: what the experiment demonstrates is what happens after a violation, and the answer is that nothing happens — the silence throughout is real. We showed you the violation, but its illegal status has not changed: the standard rules such a program undefined behavior, and every number it printed today owes us no explanation. Change the optimization level, change the linker, change the link order once more, and its behavior may put on a completely different face. When this bites in real projects, the person writing the code rarely planned it; the root of the trouble is the same header file being trimmed into two faces. The header itself is fine — two compile commands carrying different macros produce this:

```cpp
// flavor.h (wrapped in an include guard)
#include <cstdio>
#ifdef NEW_STATS
inline int bucket_count() { return 16; }
#else
inline int bucket_count() { return 4; }
#endif
inline void report(int v) { std::printf("bucket=%d\n", v); }
```

We compile `old_code.cpp` with no macros at all, while `new_code.cpp`'s compile carries `-DNEW_STATS`; once linked, run it:

```text
$ g++ old_code.o new_code.o face_main.o -o face_app && ./face_app
bucket=4
bucket=4
```

Both lines print 4. The definition that returns 16 was clearly compiled into `new_code.o`, yet it was dropped whole at link time — even `new_code.cpp`'s own call got rebound to 4 — and `bucket_count`'s identity in the two `.o` files, checked with `nm`, is still the same `W`. A single `-D` difference in the build system can manufacture such a program, and no tool cries out, because from each unit's own point of view, everyone is innocent.

Can any tool catch part of this? `-flto` is link-time optimization; we mentioned it in the compiler article, and the full unfolding lives in the LTO-and-linkers article — it lets all translation units meet at link time. The class where a type grows two faces can be caught by pairing it with `-Wodr`, GCC's warning switch dedicated to ODR violations; we verified this with two `struct Config` differing in field order, and the warning named them on the spot. But the class where an inline function grows two bodies — we tried `side_a` and `side_b` with `-flto -Wall -Wextra`, and this time not one warning arrived; the program ran on as usual printing `from_a=10 from_b=10`. So the last line of defense is how you write: for the definition of one entity, take it from one and the same header file. Conditional compilation may trim the implementation, but do not trim it into two same-named, different-bodied faces.

## The Modern Meaning of inline: Linkage Qualification First, Inlining Hint Second

The other half left over from the preprocessing article, we settle today too: why can an inline function live in a header at all? The `inline` keyword gives a function two things. The first is qualification — the qualification for its definition to appear in multiple translation units. You write one inline function in a header, dozens of units include it, and every unit generates its own weak symbol wrapped in its own COMDAT group — exactly the mechanism we verified in the weak-symbols-and-COMDAT section earlier. constexpr functions and member functions defined in-class are granted implicit inline status by the language for the same reason: their definitions are headed for header files from birth.

The second thing is inlining in the literal sense: it gives a suggestion, and the suggestion is to rewrite the call into the function body itself. Let's verify with a trio:

```cpp
// inl.h (wrapped in an include guard)
inline int twice(int x) { return x * 2; }
constexpr int triple(int x) { return x * 3; }
struct Box { int v; int get() const { return v; } };  // defined in-class, implicitly inline
```

We compile once at `-O0`, and all three definitions dutifully land symbols:

```text
$ g++ -c use_inl.cpp && nm use_inl.o | grep -v U
0000000000000000 T _Z3runi
0000000000000000 W _Z5twicei
0000000000000000 W _Z6triplei
0000000000000000 W _ZNK3Box3getEv
```

`run` is the caller we wrote. `twice`, marked `inline`, still takes a `call` in the assembly:

```text
$ g++ -S use_inl.cpp -o use_inl.s && grep -E "call.*_Z5twicei" use_inl.s
	call	_Z5twicei
```

We switch to `-O2` and compile again: the three `W`'s vanish without a trace:

```text
$ g++ -O2 -c use_inl.cpp -o use_inl_O2.o && nm use_inl_O2.o
0000000000000000 T _Z3runi
```

Look closely at this comparison: `inline` written, the optimization switch not pulled, and not one of the three function bodies got inlined — all generated as-is. And the reverse holds too: an ordinary function without `inline` gets spread out just the same when the optimizer wants to spread it; the ruling power sits with the compiler from start to finish. So the place of `inline` in modern C++ is clear: it serves definitions going into header files; it is not a plea for the compiler to spread me open.

## extern template: Gathering Generation Everywhere Into Generation in One Place

The ten `W`'s do merge into one at link time, but during compilation, the ten still each generate their own — every translation unit has to instantiate its own `bigger<double>`. When the template is small, this repeated labor hardly matters; when the template is big, the workload multiplies by the number of translation units. C++11 provided a gathering notation called extern template, formally the explicit instantiation declaration, and one sentence says what it means: do not generate this entity here, it already exists elsewhere.

Let's switch to a heavier template for the comparison. Inside `heavy_median<T>` sit the trio `sort`, `unique`, `reverse`, with the definition in the header file as usual. Variant one changes nothing: ten translation units (TU) just use it directly, and the compiled directory is called `noext`. Variant two adds one line at the end of the header, `extern template double heavy_median<double>(std::vector<double>);`, plus a separate `instantiate.cpp` writing down the explicit instantiation definition, and the compiled directory is called `withext`. Symbols on both sides, side by side:

```text
$ nm noext/tu1.o | grep heavy_median
0000000000000000 W _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
$ nm withext/tu1.o | grep heavy_median
                 U _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
$ nm withext/instantiate.o | grep heavy_median
0000000000000000 W _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
```

Variant one is the old sight we have seen: every `.o` holds its own `W`. On variant two's side, `tu1.o` has only a `U` left for `heavy_median<double>` — the compiler was stopped by that declaration line, the entity moved into `instantiate.o`, and its identity is still `W`. Notice: one-place generation still runs on the very same weak-symbol mechanism; extern template only moved the place of generation from everywhere to a designated somewhere, and the link-time merge rules changed by not one word. Both variants linked successfully with identical output, and the real difference is size: the `noext` side's `tu1.o` is 46976 bytes, while the `withext` side's `tu1.o` is only 14376 bytes.

Compiling the ten TUs one by one, we timed three rounds each on WSL2: variant one took 2.5 to 2.9 seconds, variant two took 1.85 to 1.96 seconds, plus `instantiate.cpp` on its own at just over 0.2 seconds. For a heavyweight template, what is saved is around half a second, and the heavier the template and the more translation units, the more visibly the gap amplifies.

We should also state the cost in full. extern template diverts the generation work; it does not cancel it: if ten TUs all declare it lives elsewhere and nobody actually does the generation, the linker still reports an error:

```text
$ g++ lonely.cpp -o lonely_app
/usr/bin/ld: /tmp/ccGYaLA0.o: in function `main':
lonely.cpp:(.text+0x49): undefined reference to `double heavy_median<double>(std::vector<double, std::allocator<double> >)'
collect2: error: ld returned 1 exit status
```

Where does it beat the primitive declaration-plus-manual-list approach? The definition is still in the header file. Type combinations outside the list nobody gathers, the units using them still implicitly instantiate their own, and the broken link never happens — we really ran it: a unit including only the extern-template version of the header used `heavy_median<float>`, generated its own `W`, and linked fine. The boundary between gathering and not gathering is drawn precisely by that one declaration line. And the standard library uses this itself: libstdc++'s `bits/basic_string.tcc` contains `extern template class basic_string<char>;`, precisely so that every translation unit does not instantiate its own `string`. One fresh footnote: that declaration line carries a `__cplusplus <= 202002L` condition — from C++23 on, libstdc++ removed it, letting `string` return to generate-on-demand; both the gain and the cost are on the table, and library authors are weighing both ends.

## The CMake View: A Header-Only Library Written as INTERFACE

If today's `bigger.h` piles up, you have a header-only library, its entire inventory in `include/`, without a shadow of a `.cpp`. CMake has a dedicated target type for such libraries:

```cmake
cmake_minimum_required(VERSION 3.26)
project(iface_demo CXX)
add_library(mathkit INTERFACE)
target_include_directories(mathkit INTERFACE mathkit/include)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mathkit)
```

The type we wrote is INTERFACE, and its meaning is simple to state: the target itself produces no build artifacts; its reason for existence is passing usage requirements to the targets that link it — CMake's term is usage requirements. The requirement in front of us is exactly one: add `mathkit/include` to the include path. `mathkit.h` holds one inline function plus one template; `app` includes it and links it, and the build and run look like this:

```text
$ cmake --build build
[1/2] Building CXX object CMakeFiles/app.dir/main.cpp.o
[2/2] Linking CXX executable app
$ ./build/app
42 40
```

One more round of acceptance with tools. We look under `mathkit`'s name in the build directory: we search with `find` and turn up nothing — no shadow of a `.a`, `.so`, or `.o`; the only link artifact in the whole build is `app`. Now whether the passed-on requirement actually landed. `app`'s compile command genuinely gains a `-I` pointing at `mathkit/include`. We configured the project naming the Ninja generator, so the build script CMake writes is `build.ninja`, and you can find that `-I` right in there. INTERFACE and the OBJECT library from the object-file-anatomy article stand at exactly opposite ends: an OBJECT library produces `.o` files but does not do the packaging, while INTERFACE does not even have `.o` files — all it passes out is a path. STATIC and SHARED stand in the middle, producing things that also get packaged into libraries. When you pick a type for `add_library`, think through where the entity gets generated, and the answer falls out on its own.

## The Next Step in Reuse: From One Copy Each to One Copy Shared

The previous article packed a pile of `.o` files into a static library: whoever links it carries one more copy in their executable. Today we rounded out the other half: definitions travel with header files into ten translation units, where the legality of ten entities comes from, and how the link merges them into one — you have verified every link in that chain by hand, down to the silence when the rules get broken. But up to this point, the shape of reuse is still one copy per program: a hundred programs using `bigger<int>`, and a hundred copies lie on disk, and a hundred lie in memory.

Then how can libstdc++ and glibc be shared by countless programs? At the end of the linking article, we stashed this question away for the dynamic libraries overview. The direction of the answer is to give the library a file of its own while programs carry only a reference: registration happens at build time, hookup happens at run time, and the mechanisms on both sides are the proper subject of the dynamic libraries overview. Today's ten `.o` files — keep them to rerun, or toss them and rewrite; it is a few minutes either way.
