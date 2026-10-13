---
chapter: 13
difficulty: intermediate
order: 5
platform: host
reading_time_minutes: 20
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: 'The Linker: Symbol Resolution and undefined reference'
description: 'The linker''s two jobs: symbol resolution pairs every U symbol with a unique definition, and relocation fills the empty slots following the records in .rela. Read undefined reference and multiple definition errors line by line and match them one-for-one against the Offset column of readelf -r, then see why weak symbols let strong and weak definitions coexist, why static stays out of the global verdict, and why forgetting main makes the error point at _start.'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/05-linking-symbol-resolution.md
  source_hash: e5b8cb909a0acefc28c773642f23993f99f636d431308278c874f62defb3885d
  translated_at: '2026-10-13T03:59:22+00:00'
  engine: anthropic
  token_count: 4800
---

# The Linker: Symbol Resolution and undefined reference

At the end of the previous part, we put the two empty slots on record together with how they were to be filled, and settled on who would do the filling: the linker. You already hold the anatomy chart of demo.o in your hands, so today we gather right up next to it and watch its job from start to finish. I sort the work in its hands into two items. One is symbol resolution: every name carrying a U inside a .o has to be matched with exactly one definition somewhere else; when no match can be found, what the linker reports is undefined reference, and when several same-named definitions have all shown up, what comes out is multiple definition. The other is relocation: once every name is paired up, the empty slots get filled following the records in .rela. The order of the two jobs is fixed in stone: only after the names are all settled can the linker get to work on addresses.

As usual, the environment disclosure: my experiments all ran in WSL2, the toolchain is still the GCC 16.2.1 plus binutils 2.47 combo, and every piece of output in this article was genuinely produced by a run. If your versions differ, details like error wording and paths may vary a little; the mechanics themselves haven't changed in years.

At the end of the previous part we also made a pact: keep demo.o, don't delete it. Today it goes back to work as raw material, and I keep my word. First order of business: we ask it to link all by itself:

```text
$ gcc demo.o -o demo_app
/usr/bin/ld: demo.o: warning: relocation against `extern_var' in read-only section `.text'
/usr/bin/ld: demo.o: in function `main':
demo.c:(.text+0x1b): undefined reference to `extern_func'
/usr/bin/ld: demo.c:(.text+0x21): undefined reference to `extern_var'
/usr/bin/ld: warning: creating DT_TEXTREL in a PIE
collect2: error: ld returned 1 exit status
```

The link failed, and the two names reported are exactly extern_func and extern_var, our two dangling references. The error for extern_func lands at .text+0x1b, the one for extern_var at .text+0x21; take them to demo.o's relocation table from the previous part and the Offset column matches exactly 0x1b and 0x21. Even the division of labor lines up: 0x1b carries PLT32, in charge of function calls, and 0x21 carries PC32, in charge of data references — the split described in that part, reproduced as is. The DT_TEXTREL warning on the second-to-last line was brought on by the reference to extern_var landing in a read-only section, and we'll say just that much about it. When the compiler has recorded a location but the linker can't find an address to fill it with, the linker reports against the original location. As for the knack of using this to troubleshoot, we'll go into it in detail on a.o.

But demo.o can only play the failure side: we hold no file that could provide the definitions of extern_var and extern_func, so a successful link is beyond its acting range. And we want to see every rule in action, so we switch to a minimal pair of specimens: one file that references, one that defines, and between them the two roads, success and failure, are both walkable. The code is still written in C — we confirmed in the compiler article that C does no name mangling on its side, which saves us a decoding step when reading symbols:

```c
// a.c — declares only; references symbols defined elsewhere
extern int shared_seed;
extern int helper(void);
int main(void) { return shared_seed + helper(); }
```

```c
// b.c — the defining side
int shared_seed = 10;
int helper(void) { return 32; }
```

a.c is the referencing side in the flesh: shared_seed and helper each get one extern declaration, the definitions live in other files, and main uses both anyway. We compile a.c into a.o, and both names turn into U in the symbol table; the same pair of names, over in b.o, has one landing as D and the other as T. What the linker has to do can be told in full from the two symbol tables we hold: lay the names from both sides side by side, and what you get by merging them is the global symbol table in the linker's eyes — then find a definition for every U.

## Symbol Resolution: Pairing Every U with Exactly One Definition

Following the same route as the previous part, we compile a.o and b.o, and the command handed to the linker looks like this:

```text
$ gcc a.o b.o -o ok_app && ./ok_app; echo $?
42
```

The link went through, the program ran, and echo $? printed 42. That number is what main handed back: the 10 from shared_seed plus the 32 from helper(). The number itself is ordinary, but where it came from deserves a pause: every name that carried a U in a.o found its definition in b.o, and the pairing was completed — this is exactly what symbol resolution looks like when it succeeds. Most of the errors the linker reports your way translate to "this step didn't go through." It can fail in two ways, and we'll look at both ends: one where a definition can't be found, and one where there are too many definitions.

## undefined reference: How to Read the Error

On the missing-definition end, we hold b.o back, leaving nothing but a.o in the linker's hands:

```text
$ gcc a.o -o fail_app
/usr/bin/ld: a.o: warning: relocation against `shared_seed' in read-only section `.text'
/usr/bin/ld: a.o: in function `main':
a.c:(.text+0x5): undefined reference to `helper'
/usr/bin/ld: a.c:(.text+0xb): undefined reference to `shared_seed'
/usr/bin/ld: warning: creating DT_TEXTREL in a PIE
collect2: error: ld returned 1 exit status
```

We read from the top down. The first line is a warning that came along for the ride, saying that a relocation pointing at shared_seed lands in the read-only .text; we set it aside, because it's not today's subject. The second line is the opening of the location report, pointing out that the references below come from main in a.o. The two lines in the middle are the real errors: the definition of helper wasn't found, the definition of shared_seed wasn't found either, and the linker reports both. The last two lines, the DT_TEXTREL warning plus collect2's closing line, are identical to the demo.o output at the top, so we won't read them out line by line again.

We matched errors against the relocation table with demo.o at the start; keep that reading method in mind, because we walk it once more with a.o. The two errors in a.o land at .text+0x5 for helper and .text+0xb for shared_seed; run readelf -r on a.o and the Offset column matches exactly 0x5 and 0xb. The meaning of the coordinates is the same as before: the 4 bytes at offset 0x5 are supposed to be filled with a number computed from helper's address, but the linker searched every object file in its hands and found no one who defined helper — the same situation we saw at the start, playing out once more.

Being able to match an error against the relocation table is the most practical debugging skill in this volume; from now on, whenever you hit a link error, one pass of matching will tell you where things stand. The troubleshooting moves come in two steps. First, run nm over the relevant .o files to see in which files the name is a U, confirming where the reference was issued from. The second step asks where the definition should come from: is there a .o that was forgotten on the link command line, or does the definition live in a library that was never linked? We leave libraries to the next article and the article on library search. For today's pair of specimens, the answer is sitting in plain sight: what's missing from the command line is b.o.

## multiple definition: Too Many Definitions Fails Too

For the too-many-definitions end, I have prepared another file, dup.c, that does exactly one thing: define helper a second time.

```c
// dup.c — deliberately defines helper twice
int helper(void) { return 99; }
```

```text
$ gcc a.o b.o dup.o -o dup_app
/usr/bin/ld: dup.o: in function `helper':
dup.c:(.text+0x0): multiple definition of `helper'; b.o:b.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

The second line carries the most information, reporting both sides in a single sentence; let's read them separately. On the dup.c side, helper gets a multiple definition; on the b.o side, the same location is marked first defined here. You don't have to guess which came first: b.o is listed before dup.o on the link command, and the linker recorded the first copy in the order it saw them. The third line is our acquaintance collect2 again — we met it in the opening article — the wrapper around the linker ld, and it closed out this failed link on the linker's behalf.

Why does the linker refuse to budge even a little? Think for a moment on behalf of the call instruction and it becomes clear: the call site in a.o has room for exactly one address, and nothing else. b.o says helper is over here, and dup.o says it's over here too; both are perfectly legitimate definitions, and whichever one gets picked, the other's code goes unreferenced — and picking wrong is even harder to clean up after. So the linker enforces a hard rule: among strong definitions that enter the global symbol table, one name may have exactly one copy; the moment a second copy arrives, the linker stops it on the spot. C++ has a kindred rule at the language level, which we call the ODR, the one definition rule — we named it in the compiler article, and the full treatment is saved for the templates and ODR article. One detail deserves a word: when dup.c compiles on its own, it doesn't utter a word of complaint. We already saw why: the compiler only sees the translation unit in front of it; b.c and dup.c know nothing of each other's existence, and who duplicated whom can only be adjudicated once all the .o files meet at the linker.

## Weak Symbols: Strong and Weak Coexist, and Strong Wins

The rule in the last section sounds hard, but it hides one exception. At the bottom of the alphabet from the previous part, we still owed you introductions to W and V, with a promise to meet them when they showed up. Reporting for duty today is W.

```c
// weak_def.c
__attribute__((weak)) int policy(void) { return 1; }
```

```c
// strong_def.c
int policy(void) { return 2; }
```

```c
// use_policy.c — the caller
#include <stdio.h>
int policy(void);
int main(void) { printf("policy=%d\n", policy()); return 0; }
```

That line `__attribute__((weak))` in weak_def.c may be new to you. It isn't in the ISO C++ standard; it comes from the GNU family of extensions, and both GCC and LLVM's Clang accept it. Its job is to mark the definition of policy as a weak symbol. Strong and weak are a pair of relative labels: the definitions we normally write carry nothing at all, so they all land with strong status, while a definition marked weak drops to weak status. We link the three .o files together and look at their symbols:

```text
$ gcc use_policy.o weak_def.o strong_def.o -o poly_app && ./poly_app
policy=2
$ nm weak_def.o | grep policy
0000000000000000 W policy
$ nm strong_def.o | grep policy
0000000000000000 T policy
```

The run prints policy=2: the strong definition won. The two lines of nm output spell out the identities plainly: policy in weak_def.o is an uppercase W, the marker of a weak symbol, while in strong_def.o it's still our old friend T. In the alphabet, W covers weak functions and V covers weak data objects; today's appearance was W — for V, just remember its face.

The rule fits in one sentence: one strong definition of a name together with several weak definitions is legal, and the linker picks the strong one. If they're all weak, the linker takes whichever copy comes first on the command line. Savor the contrast with the last section: when two strong definitions meet, what gets reported on the spot is multiple definition, yet a strong one paired with weak ones coexists in perfect peace. This exception answers a perfectly respectable need: a library wants to ship a default implementation for some name while leaving room for users to override it, and weak symbols do exactly this job. glibc carries examples of it: the batch of compatibility stubs for the old pthread interfaces (pthread is Linux's thread library), and `__gmon_start__`, the startup hook for performance profiling, both run on this mechanism. We won't unpack each individual usage here; just keep one impression: beyond its hard rule, the linker holds one more set of strong-versus-weak adjudication.

For those of us writing C++, weak symbols carry an even more important identity. The compiler article left a question on file: a header gets pasted into ten translation units, and bigger<int> gets instantiated into ten same-named copies of code — why don't they fight at link time? The direction of the answer is right here. Inline functions and template instantiations are handed weak definitions by the compiler, paired with a mechanism called COMDAT: it's a section-grouping mechanism; among same-named sections in the same group, only one copy survives linking, and duplicates are dropped outright. Ten copies of bigger<int> merge into one this way. The full treatment is saved for the templates and ODR article.

## static: Names That Never Enter the Global Verdict

With strong and weak covered, there is one more class of names that never enters this adjudication at all. Look at two same-named variables:

```c
// s1.c
static int counter = 1;
int get1(void) { return counter; }
```

```c
// s2.c
static int counter = 2;
int get2(void) { return counter; }
```

```c
// s_main.c — the caller
#include <stdio.h>
int get1(void);
int get2(void);
int main(void) { printf("%d %d\n", get1(), get2()); return 0; }
```

Both files write a counter, and both write it as a definition. By the hard rule laid down back in the multiple definition section, shouldn't this be blocked? Let's run it and see:

```text
$ gcc s1.o s2.o s_main.o -o static_app && ./static_app
1 2
$ nm s1.o | grep counter
0000000000000000 d counter
$ nm s2.o | grep counter
0000000000000000 d counter
```

The link went through, and the printed 1 2 is exactly the values of the two counters. Match it against the nm output in our hands: in both lines counter is a lowercase d, and the alphabet from the previous part said lowercase is the marker of a static-private symbol. The formal name is internal linkage: linkage describes how widely a name can be seen, and internal means visible only inside the translation unit it belongs to. The compiler article also threw another word with linkage in it at us: language linkage, which governs how names get mangled — don't conflate the two into one thing. The two counters share the very same name, yet both are local symbols, each sitting in its own symbol table, absent from the global one. So the linker never compared them against each other, and the supposed conflict never had a chance to arise in the first place.

The linker's jurisdiction is now clear: what it adjudicates is only the names that enter the global symbol table, the batch with uppercase T, D, B in the alphabet. The lowercase private symbols, static functions and static global variables, are not its business. The boundary is useful in everyday code too: if you put a helper function in a header and worry about a name clash once several files include it, hang a static on it and it becomes each file's private copy; the C++ equivalent is the anonymous namespace, and the name naturally can't collide anymore.

## Even main Isn't Special

Finally, let's look at an error you have very likely run into for real. This time we link b.o: you know its entire inventory, shared_seed and helper have both been introduced — the only thing missing is main.

```text
$ gcc b.o -o no_main
/usr/bin/ld: /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/Scrt1.o: in function `_start':
(.text+0x1b): undefined reference to `main'
collect2: error: ld returned 1 exit status
```

The long path on the first line is Scrt1.o's real address on my machine. You met Scrt1.o in the very first article of this volume: one of the startup files that collect2 silently adds to every link. Living inside it is `_start`, the program's true entry point, the one we named when we looked at the executable's Entry address in the previous part. `_start`'s code needs to call main, so in Scrt1.o's symbol table, main is one more pending U. b.o happens not to have it, the linker followed procedure, and what it reported is an undefined reference to main from `_start` — the same species of error as a.o's two.

Look closely and this error holds two things worth pondering. The first: the lead actor isn't our code. The reference is issued from inside a startup file, the path hung on the error is Scrt1.o's, the function name is _start's, and the offset 0x1b points into Scrt1.o's own .text — even the reported location is its own, and there is nowhere to hang a source file name at all, because startup files come ready-made with the compiler suite and have no source code of ours to point at. The other thing worth noticing is that the linker treats everyone alike: however special main's name is in C++, once it reaches the symbol resolution step it's just one more ordinary U, and with no definition it gets reported like anything else. The day you fumble and type mian instead of main, the compiler won't make a sound; when linking comes around, the linker catches it for you.

What _start does before main, and what each startup file in that row is responsible for, belong to the program startup article. Today we only need to take away one thing: even the program's entry point goes through symbol resolution.

## Relocation Lands: U Becomes T

Only after symbol resolution passes in full does the linker get to its second job: relocation. In the previous part we read every column of the records, and we wrote the formula out by hand too. The linker has done the math for us; let's inspect the result:

```text
$ nm a.o | grep helper
                 U helper
$ nm ok_app | grep helper
000000000000112c T helper
```

Watch helper's identity change: before linking, in a.o, it was a U with no address at all. After linking, it has become a T with an address. The linker merged b.o's definition into ok_app, and the seat assigned to helper is 0x112c. We met the PIE identity in the opening article: it means helper's 0x112c was settled at link time, and the program will still be shifted as a whole when it loads — the mechanism behind that waits for the position-independence article. The call issued by main in a.o has been filled in by now, following the formula from the previous part. As for whether the empty slots were truly filled, don't take my word for it: one nm query settles it. The results of both symbol resolution and relocation land in the final symbol table.

## From a Pile of .o Files to the Idea of a Library

Look back at the link commands we typed today. To the command gcc a.o b.o -o ok_app, only .o files exist; it never even sees a shadow of the source code. Whether b.o was compiled just now or three months ago and left lying on disk, the command doesn't change by a single character. Follow that property one step further and it gets interesting: for a batch of definition-carrying .o files like b.o, could we compile them ahead of time, tuck them neatly into one file, and hand the whole package to the linker whenever it's needed, sparing ourselves the trip back to the source every time? The finished product of this idea is the library, and the static library is the one that leads the way. How the packaging is actually done, and how the linker picks and chooses when facing a whole package of .o files — all of it waits for the next article.

## The CMake Perspective

In the link commands we assembled by hand today, the .o files are countable on one hand. In a real project, a single link command routinely runs to hundreds of .o files plus a row of libraries, and who assembles a roster that long? That job belongs to the build system. In the previous part's CMake perspective, we hung a target_link_libraries on the OBJECT library demo_core, and today we can finally fill in what it means:

```cmake
target_link_libraries(app PRIVATE demo_core)
```

It expresses a link-time dependency: when app is linked, demo_core's artifacts come along. app is the target being linked and demo_core is the dependency brought along; you met PRIVATE in the preprocessing article and the previous one — it locks the dependency to app itself, leaving nothing for downstream targets to inherit. As for the ordering of hundreds of targets, who goes first and who last, CMake sorts it out for us from the dependency graph and assembles it into one command collect2 understands.

The build system is adept at assembling commands; reading errors is where it can't help you. The day CMake builds its way to the link step and undefined reference floods your screen, all it can do is pass the wall of text straight through to you. At that point you're still using today's reading method: query with nm to see whose U the name is, confirm where the definition should come from, and check whether some object was left off the link. The command is assembled by CMake, the verdict is handed down by the linker, and so the error has to be read in the linker's language.

## The Compilation Half, Complete

From the g++ hello.cpp -o hello line in the very first article of this volume, we have run the whole road segment by segment with our own hands: the forty-four thousand nine hundred and seventy-one lines of hello.i (the preprocessing article), the mangled symbol strings of mangle.cpp and the on-demand bigger (the compiler article), demo.o's section table, symbol table and two relocation records (the previous part) — and today we watched the linker pair a.o with b.o, fill in the empty slots, and merge them into an ok_app that prints 42. From one line of source code to a runnable program, you have seen and checked the true shape of every layer with your own hands; none of it is secondhand anymore.

The compilation half is done, and in the other half the question changes: how do we reuse our code. The first step of reuse is packing a pile of .o files into a static library, and that is exactly what the next article opens with. As for why libstdc++ and glibc, which we link every day, can be shared by countless programs, we leave the answer to the dynamic libraries overview article. Feel free to keep today's .o files around and run them another time — see you in the next article.
