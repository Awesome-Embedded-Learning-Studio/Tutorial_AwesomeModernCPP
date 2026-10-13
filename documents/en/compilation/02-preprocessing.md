---
chapter: 13
difficulty: intermediate
order: 2
platform: host
reading_time_minutes: 20
tags:
- cpp-modern
- host
- intermediate
- 工具链
title: "Preprocessing: How a Translation Unit Comes to Be"
description: "#include is plain-text pasting, macros are plain-text substitution, and include guards defend against double pasting; use -E, -H, -MM, and -dM to pull out the preprocessor's products and evidence one by one, then cover the standard's nine phases of translation."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/02-preprocessing.md
  source_hash: 4bd0d2d209e7e41f90f47ebf389939f2b9a02273c56c9b9bece3eee2845364c5
  translated_at: '2026-10-13T03:50:54+00:00'
  engine: anthropic
  token_count: 4272
---

# Preprocessing: How a Translation Unit Comes to Be

At the end of the previous piece we left a number sitting there: six lines of `hello.cpp` went through `-E`, and out came a 44,971-line `hello.i`. Which stage that forty-some-thousand-line expansion happens in was already named in that piece: the very first one, preprocessing. Today we're not going anywhere. We'll stay put in this one stage, solidly, and lay each question in front of the evidence, one at a time: what `#include` actually does, what macros turn into, and what an include guard is guarding against.

The usual environment note: my experiments all ran in WSL2 with the GCC 16.2.1 toolchain, and every piece of output in this piece came from a real run. If your versions differ, details like line counts and ordering may vary, but the behavior of this stage itself has been stable for decades.

## What #include Does Is Paste, Verbatim

When we write `#include`, what we say out loud is "including a header," but what usually floats into our heads is an import like the ones in other languages: hook a ready-made module in, and the compiler will recognize the rest on its own. What the preprocessor does is far more direct than that mental picture. It opens the file you point at, copies the contents in without altering a single character, and pastes them on the line where the `#include` sits. `#include <iostream>` copies in the full text of iostream; `#include "guarded.h"` copies in the full text of guarded.h, comments and all.

If the file being copied in contains `#include` lines of its own, the copying keeps going, recursing layer upon layer, and that is how `hello.i` piles up its forty-some-thousand lines. One `.cpp` plus everything copied in along the way has, taken together, a formal name: the translation unit. The title of this piece is about precisely its birth.

Saying just the one word "copy" may not give you much of a feel for it, so let's run the experiment directly: include the same header twice and see what kind of ruckus it stirs up. I prepared a header with no protection of any kind:

```cpp
// no_guard.h
struct Point { int x; int y; };
```

Then we write a `use_twice.cpp` that obediently includes it twice:

```cpp
// use_twice.cpp
#include "no_guard.h"
#include "no_guard.h"
int main() { Point p{1, 2}; return p.x; }
```

If there really were some deduplicating intent hiding behind `#include`, the code we just wrote ought to compile all the same. Let's try it:

```text
$ g++ -c use_twice.cpp
In file included from use_twice.cpp:2:
no_guard.h:1:8: error: redefinition of 'struct Point'
In file included from use_twice.cpp:1:
no_guard.h:1:8: note: previous definition of 'struct Point'
```

Each error and note is followed by two more lines of source excerpt, which we omit here. This time the error comes with no hedging whatsoever: `struct Point` has been defined twice. There is also an interesting detail hiding in this output, worth a half-beat pause: it is the compiler reporting the redefinition, not the preprocessor. The preprocessor itself knows nothing about C++ grammar. It copies that line through twice, exactly as written, hands the text to the compilation stage, and lets go; only when the compiler sees the same struct appear twice does it step in and block it. So what a header must guard against is not the same file being read twice, but the same definition being seen twice by the compiler.

Then what can a header do for itself? The answer is those three lines you have seen at the top of every header file. Their name is the include guard.

```cpp
// guarded.h
#ifndef GUARDED_H
#define GUARDED_H
struct Point { int x; int y; };
#endif
```

Let's follow the preprocessor through its working, two passes. When the first paste begins, `GUARDED_H` is still an undefined macro, so the `#ifndef` condition holds; the preprocessor walks in, defines `GUARDED_H`, and hands the `struct` text through unchanged. When the second paste begins, `GUARDED_H` is already there, the condition no longer holds, and the entire file is skipped as a block; not one character travels downstream. Now we compile `use_twice_ok.cpp`, which includes the same header twice:

```text
$ g++ -c use_twice_ok.cpp
(no output = compilation succeeded)
```

This time the terminal settles into silence; we get no output at all, and the compile simply goes through. There is no magic in the guard: it just arranges for the second paste to come up empty-handed.

Its companion is `#pragma once`, which you have probably seen in quite a few codebases. It is not in the ISO standard, but all mainstream compilers support it, and its meaning is a single sentence: the same file is pasted only once within a translation unit. Pick either spelling and go; in large projects both are in fact common.

## Macros: Substitution Happens at the Text Layer

Macros are the other half of the preprocessor's main business. A minimal example:

```cpp
// macro_demo.cpp
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define GREETING "hello, preprocessor"

int limit = MAX(10, 20);
const char* msg = GREETING;
```

We know the old method by now: stop with `-E` at the step where preprocessing finishes, then let `tail` take only the last few lines:

```text
$ g++ -E macro_demo.cpp | tail -5



int limit = ((10) > (20) ? (10) : (20));
const char* msg = "hello, preprocessor";
```

The expanded result is laid out in front of us; let's identify the pieces one by one. `GREETING` was replaced by that string literal exactly: not one character different. The expansion of `MAX(10, 20)` is more interesting: the arguments 10 and 20 are stuffed back into the macro body, as pure text, at the positions of a and b, so the whole line becomes `((10) > (20) ? (10) : (20))`. Notice that what gets stuffed in is text, not evaluated numbers. The preprocessor has no intention whatsoever of computing anything; the only thing it recognizes is characters.

Read the full `-E` output and you can catch one more small thing: the `//` comment at the top of the source file is nowhere to be found in it. Comments are replaced by whitespace during the preprocessing stage; the compiler never gets its turn with them.

Now move your eyes to that string of parentheses. Suppose a few of the parentheses in the macro body were left out, and it were written as `#define MAX(a, b) a > b ? a : b`. Then `MAX(x, 20) * 2` would expand to `x > 20 ? x : 20 * 2`. Once the text has been pasted together, that `* 2` clings to the 20 alone, not to the whole conditional expression: when x is greater than 20, what gets computed is x itself, not twice x, and the multiply-by-two is lost entirely. The parentheses hugging each parameter in the macro body, together with the pair wrapped around the very outside, defend against exactly this: whatever shape the argument arrives in, the parentheses force it to settle into place as one whole unit. The macro's poor reputation is rooted here too: it knows no types and knows no scopes; the only thing it knows is text.

> If what you truly want to define is a constant, today we have `constexpr`. If what you truly want to write is a short piece of logic, inline functions and templates are a more respectable choice than a macro. Why they deserve to take over, and what the one definition rule (ODR, mentioned once in the roadmap of the volume's opening piece) is about, the piece on templates and ODR will come and sort out in due course.

## The Standard's Account: Nine Phases of Translation

Everything we have used so far is the division by engineering habit, the four stages everyone speaks of: preprocessing, compilation, assembly, linking. The standard's own account is finer than that. ISO C++ prescribes the whole journey from source file to executable program as nine phases of translation. The original text is in the standard's [lex.phases] section; the full draft is on [eel.is](https://eel.is/c++draft/lex.phases), and cppreference also has a tidied-up version, on the page named [Phases of translation](https://en.cppreference.com/w/cpp/language/translation_phases). What each of the nine phases is responsible for, in plain words:

1. Map the bytes of the source file onto the uniform basic source character set.
2. Splice each physical line that ends in a backslash together with the next line, into one logical line.
3. Slice the text into preprocessing tokens, and replace each comment with one space.
4. Execute the preprocessing directives: expand macros, handle conditional compilation, handle `#include`; each included file recursively goes through phases 1 through 4 from the beginning.
5. Convert the characters and escape sequences inside literals to the encoding the program runs under.
6. Concatenate adjacent string literals.
7. Compilation happens at this step: preprocessing tokens become real tokens, followed by syntax analysis and semantic analysis.
8. List the template instantiations that are needed, and complete them.
9. Linking: the individual translation units, the instantiation results, and the libraries combine into one complete program.

Set the two side by side and what we usually call preprocessing corresponds roughly to phases 1 through 4, and what `-E` intercepts is the text handed over when phase 4 finishes. The four-stage account is the coarse division you get after packing the standard into a toolchain, convenient for stopping and observing: in the previous piece we saw with our own eyes that cc1plus, one single program, covers that entire first stretch, and collect2 corresponds to the linking of phase 9.

Match the four stages against the nine phases and one stage is left without a landing place: the assembler. It cannot find a seat for itself in the standard — not because someone left it out, but because the standard only prescribes down to the level of the language; translating into machine instructions is already an implementation detail of each vendor's tools. Look back once more at the close of phase 4: once this step is done, every `#` directive has been processed, the translation unit has officially taken shape, and a complete piece of C++ text is handed to what comes next.

## The # Lines at the Top of -E Output Are Written for the Next Stage

Flip to the beginning of the `-E` output and we see a batch of lines that start with `#`, followed by line numbers and file names:

```text
# 0 "macro_demo.cpp"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3
# 0 "<command-line>" 2
# 1 "macro_demo.cpp"
```

They are not source code and take no part in compilation. Their name is line markers, and they are written for the compiler's next stage to read: from this line onward, which file the content comes from and at which line, all of it is recorded. What are they for? Think of `hello.i`'s situation: forty-some-thousand lines of flattened text. If something goes wrong inside main, the diagnostic has to know which file it is actually talking about. Each time a header is pasted in, a matching line marker appears in the output, and that is how the boundaries between files get recorded. The `<built-in>` line among the markers corresponds to the batch of predefined macros the compiler carries with it; we will meet them shortly in the `-dM` output.

> The end of a line marker sometimes carries additional numbers. That 3 at the end of the stdc-predef.h line above marks precisely a system header, hinting that the compiler should relax its diagnostics a little for that stretch of content. Knowing this exists is enough; no need to memorize the details.

## Conditional Compilation and Predefined Macros

The `#ifndef` the guard used is one application of conditional compilation. Its bigger stage is letting the same source paste in different content under different platforms and different standard versions. Where does the basis for the decision come from? Partly from the predefined macros the compiler brings along. One command fishes them out:

```text
$ echo | g++ -dM -E -x c++ - | grep -E "__cplusplus|__GNUC__|__linux__|__x86_64__"
#define __GNUC__ 16
#define __cplusplus 202002L
#define __x86_64__ 1
#define __linux__ 1
```

Let's read that command a segment at a time: echo feeds in an empty input; `-x c++` has g++ treat standard input as a C++ source file; `-E` still does preprocessing; `-dM` asks for the definitions of all predefined macros to be printed; and I used grep to pick out four of them.

- `__GNUC__` is 16, GCC's major version number. When we write a branch for GCC-specific extensions, this is what we tell them apart by.
- `__cplusplus` is 202002L, the value corresponding to C++20. We passed no `-std`, and GCC 16's default is exactly gnu++20, which is C++20 plus a stack of GNU extensions on top.
- `__x86_64__` and `__linux__` are platform markers; this x86-64 Linux machine of ours has both of them defined.

With these macros in hand, we can write platform branches:

```cpp
#ifdef __linux__
// the implementation on the Linux side
#else
// the implementation on other platforms
#endif
```

By the same reasoning, we can branch on the standard version:

```cpp
#if __cplusplus >= 202002L
// a path only open from C++20 onward
#endif
```

Where conditional compilation differs from if/else becomes clear the moment you look at when the cutting happens: the pruning happens in phase 4, the text that was not chosen simply disappears, so the compiler never sees it at all. Inside a losing branch, you can write code that would not compile on the current platform, or even include headers the current platform does not have, and the program remains perfectly fine. if/else is a different matter: the code on both sides must be legal C++, and the path is only chosen at run time.

There is one more group of macros that report information about themselves, and we should meet them too: `__FILE__` expands to the current file name and `__LINE__` to the current line number; those two report position. `__DATE__` and `__TIME__` expand to the moment of compilation; they report time. They are the most common sights in logging and assertions:

```cpp
std::cout << __FILE__ << ":" << __LINE__ << "\n";
```

The preprocessor fills in the file name and line number of the line this sits on, truthfully. Look back at the include guard once more, and you will see it is really just these few things working together: `#ifndef` does the asking, the macro definition keeps track of the state, and conditional compilation skips the second paste in its entirety.

## -H and -MM: Asking for the Include Relationships

We don't have to dig through files ourselves to find out a header's include relationships; the preprocessor will volunteer the report. Add `-H`, and it prints the entire include tree:

```text
$ g++ -H -c use_twice_ok.cpp
. guarded.h
```

One dot means one layer of inclusion. use_twice_ok.cpp includes exactly one header, guarded.h, so this tree is pitifully short. Run -H on hello.cpp and things get lively: standard headers indent to the right one after another, stacking up layer upon layer. Look at that tree, and the scale of 44,971 lines turns concrete all at once.

Then we call on `-MM`. What it outputs is no longer code but another kind of thing: a dependency rule:

```text
$ g++ -MM use_twice_ok.cpp
use_twice_ok.o: use_twice_ok.cpp guarded.h
```

The format of this line is an old acquaintance: a make dependency rule, target before the colon, dependencies after. This is exactly where a build system's header dependencies come from: the moment you edit guarded.h, the build tool needs only one glance at the matching rule to know that use_twice_ok.o is out of date, and a recompile becomes unavoidable. Without this mechanism, changing one header would mean a full rebuild. The quiet of incremental builds rests on precisely these include relationships the compiler reports in passing.

## The CMake Perspective: Every One of These Jobs Has Someone Handling It

Day to day we mostly don't deal with these switches directly; CMake has taken over the common chores. Let's go through them one by one.

Header search paths are those `-I` flags in the compile command. In CMake, we use the target-based spelling, so the path travels with the target it belongs to:

```cmake
target_include_directories(app PRIVATE include)
```

One more reminder: the other kind is the global include_directories, which affects every target defined after it — old-style usage, and in new code we avoid it where we can: whichever target a header serves, that target should declare its own path.

As for the dependency business, we don't have to run -MM ourselves. CMake's default tier generates Makefiles — the Make generator, as it's commonly called — and what it attaches to the compile commands by default is precisely a dependency-output switch of the -MMD kind. Change a header, and what should be recompiled and what shouldn't is for the build system to look up on its own; not your worry.

What precompiled headers solve is exactly the compile time that pasting costs. We measured this once in the previous piece: six lines of hello.cpp become forty-some-thousand lines once everything is pasted in. If a few hundred .cpp files in a project all did this — each one pasting the full text of the same batch of headers from scratch and then analyzing it from scratch again — the time would all go to that repeated labor. What target_precompile_headers does is turn the processing of common headers into one reusable artifact, which each translation unit then picks up directly:

```cmake
target_precompile_headers(app PRIVATE <vector> <string>)
```

What it saves is time; what it doesn't change is semantics, and on that point you can rest easy: every translation unit still sees the complete text, pasted in full. Which headers are worth putting in, and how to pick the arguments, we'll leave at a pointer here; when you actually put it to use, a flip through CMake's own documentation has the answers.

## What Preprocessing Hands Over Is Pure C++ Text

Those forty-some-thousand lines of hello.i, we can now read every one of them: the headers' contents are pasted in there, the macros are gone, the comments are gone, not a single `#` directive remains, and our main is still lying at the very bottom. Text like this is the translation unit: pure C++.

The preprocessor's work ends here. The next job belongs to the compiler: translate every function and every statement in that text into assembly. That is what the next piece will do, so let's keep hello.i safe — it is that piece's raw material.
