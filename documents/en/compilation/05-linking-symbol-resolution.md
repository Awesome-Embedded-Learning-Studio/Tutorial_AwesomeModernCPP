---
chapter: 13
difficulty: intermediate
order: 5
platform: host
reading_time_minutes: 23
tags:
- cpp-modern
- host
- intermediate
title: "The Linker: Symbol Resolution and undefined reference"
description: "The linker's two jobs: symbol resolution pairs every U symbol with a unique definition, and relocation fills the empty slots following the records in .rela. Read undefined reference and multiple definition errors line by line, match each against the Offset column of readelf -r, see why weak symbols let strong and weak definitions coexist, why static stays out of the global verdict, and why a missing main gets reported against _start."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/05-linking-symbol-resolution.md
  source_hash: 1ce61e4c41031ac026e43e6171cab8080f7c898b5bac7493351f5b98137d6080
  translated_at: '2026-09-30T06:04:05+00:00'
  engine: glm
  token_count: 5014
---

# The Linker: Symbol Resolution and undefined reference

At the end of part 04, we put the two empty slots on record along with how they were to be filled, and settled on who would do the filling: the linker. You already hold the anatomy chart of demo.o in your hands, so today we walk right up to it and watch its job from start to finish. The work in its hands, I would sort into two items. One is symbol resolution: every name marked U inside a .o has to be matched with exactly one definition somewhere else; the ones that can't be matched earn what the linker reports as undefined reference, and when several same-named definitions have all arrived, what comes out is multiple definition. The other is relocation: once the names are all paired, the empty slots get filled following the records in .rela. The order of the two is fixed in stone: only after the names are settled can the linker work out addresses.

As usual, the environment disclosure: all my experiments ran in WSL2, with GCC 16.2.1 plus binutils 2.47 as the toolchain at hand, and every snippet of output in this piece was actually run. If your versions differ, details like error wording and paths may vary, but the mechanics themselves haven't changed in years.

At the end of part 04 we also made a pact: keep demo.o, don't delete it. Today it goes back to work as raw material — I keep my word. First order of business: we ask it to link all by itself:

```text
$ gcc demo.o -o demo_app
/usr/bin/ld: demo.o: warning: relocation against `extern_var' in read-only section `.text'
/usr/bin/ld: demo.o: in function `main':
demo.c:(.text+0x1b): undefined reference to `extern_func'
/usr/bin/ld: demo.c:(.text+0x21): undefined reference to `extern_var'
/usr/bin/ld: warning: creating DT_TEXTREL in a PIE
collect2: error: ld returned 1 exit status
```

The link failed, and the ones named in the errors are exactly extern_func and extern_var, our two dangling references. extern_func's error lands at .text+0x1b, extern_var's at .text+0x21; hold them against demo.o's relocation table from part 04 and the Offset column matches exactly 0x1b and 0x21. Even the division of labor between the types lines up: 0x1b carries PLT32, which handles function calls, and 0x21 carries PC32, which handles data references — the split we walked through in part 04, reproduced as-is. The DT_TEXTREL warning on the second-to-last line is dragged in by extern_var's reference landing in a read-only section; we pass over it in one sentence. A position the compiler registered, but the linker can't find an address to fill, so it reports the spot as it stands. The knack of using this to hunt down errors, we'll go through in detail on a.o.

But demo.o can only play the failure side: we hold no file that provides the definitions of extern_var and extern_func, so a successful link is one scene it cannot act out. We want to watch every rule play out, so we switch to a pair of minimal specimens — one file holding the references, the other holding the definitions — a pairing that can walk both the success road and the failure road. The code is still C; in part 03 we confirmed that the C side does no name mangling, which spares us one round of decoding when we read symbols:

```c
// a.c — declarations only, referencing someone else's symbols
extern int shared_seed;
extern int helper(void);
int main(void) { return shared_seed + helper(); }
```

```c
// b.c — the definer
int shared_seed = 10;
int helper(void) { return 32; }
```

a.c is the referencing side proper: shared_seed and helper each get an extern declaration, the definitions live in another file, and main insists on using both. We compile a.c into a.o, and both names come out U in the symbol table; the same pair of names, arriving in b.o, land one as D and the other as T. What the linker has to do can be stated entirely from the two symbol tables in our hands: lay the names from both sides together, and what you have assembled is the global symbol table as the linker sees it — give every U its definition.

## Symbol Resolution: Pairing Every U with a Unique Definition

We compile a.o and b.o the way part 04 showed, and the command handing them to the linker looks like this:

```text
$ gcc a.o b.o -o ok_app && ./ok_app; echo $?
42
```

The link passed, the program ran, and echo $? printed 42. That number is what main handed back: shared_seed's 10 plus helper()'s 32. The number itself is ordinary, but its origin deserves a pause from us: every name hanging a U in a.o found its definition in b.o, the pairing completed — this is exactly what symbol resolution getting its job done looks like. Most errors the linker reports to you translate, on inspection, to this step not getting done. There are two ways for it to fail, and we'll look at both ends: definitions that can't be found are one, definitions too many are the other.

## undefined reference: How to Read the Error

For the can't-find end, we hold b.o back, and the linker is left holding only a.o:

```text
$ gcc a.o -o fail_app
/usr/bin/ld: a.o: warning: relocation against `shared_seed' in read-only section `.text'
/usr/bin/ld: a.o: in function `main':
a.c:(.text+0x5): undefined reference to `helper'
/usr/bin/ld: a.c:(.text+0xb): undefined reference to `shared_seed'
/usr/bin/ld: warning: creating DT_TEXTREL in a PIE
collect2: error: ld returned 1 exit status
```

We read from the top. The first line is a tagalong warning, saying a relocation aimed at shared_seed sits in the read-only .text; we set it aside, today's real subject isn't there. The second line is the position's opening remarks, pointing out that the references below come from a.o's main. The middle two lines are the actual errors: helper's definition wasn't found, shared_seed's wasn't found either, and the linker reported both. The final two lines, the DT_TEXTREL warning plus collect2's closing, are identical to the demo.o output from the opening, so we won't read them line by line again.

In the opening we used demo.o to match error lines against the relocation table; keep that way of reading firmly in hand, because we're going to walk it again with a.o. Of a.o's two errors, helper's lands at .text+0x5 and shared_seed's at .text+0xb; run readelf -r on a.o, and the Offset column matches exactly 0x5 and 0xb. What the coordinates mean is what it meant just now: the 4 bytes at offset 0x5 are supposed to receive a number computed from helper's address, but the linker searched through every object file in its hands and found nobody defining helper — the scene we saw in the opening, the same scene again here.

Being able to match error lines against the relocation table is the most practical debugging skill in this volume. From here on, link errors won't faze you: one pass against the table and you'll know where you stand. The investigation has two steps. The first step: run nm over the .o files involved, see in which of them the name shows up as U, and confirm where the references are issued from. The second step asks where the definition was supposed to come from: is there a .o forgotten off the link command, or does the definition live in a library that was never named? We leave the library questions to part 06 (Static Libraries: Packaged with ar, Linked with -l/-L) and part 10 (Library Search Logic: How Libraries Are Found at Link Time and Run Time). For today's pair of specimens, the answer is plain as day: what the command line is missing is precisely b.o.

## multiple definition: Too Many Definitions Is No Good Either

For the too-many end, I prepared one more dup.c, and the one thing it does is define helper all over again:

```c
// dup.c — deliberately defines helper a second time
int helper(void) { return 99; }
```

```text
$ gcc a.o b.o dup.o -o dup_app
/usr/bin/ld: dup.o: in function `helper':
dup.c:(.text+0x0): multiple definition of `helper'; b.o:b.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

The second line carries the most information — one sentence reporting both sides — so we take them apart. On dup.c's side, helper picks up a multiple definition, while on b.o's side the same position gets marked first defined here. You don't even have to guess who came first: b.o precedes dup.o in the link command, and the linker recorded the first copy in the order it saw them. The third line is our old acquaintance collect2 again, whom we met in part 01: the wrapper around the linker ld, and it closed out this failed link.

Why is the linker so utterly inflexible? Think on the call instruction's behalf for a moment and it becomes clear: the spot in a.o that calls helper can hold exactly one address. b.o says "helper lives with me," and dup.o says "it lives with me too." Both are perfectly legitimate definitions; whichever one gets picked, the other one's code goes unreferenced, and picking wrong is beyond repair. So the linker enforces a hard rule: among strong definitions entering the global symbol table, one name may legally have exactly one, and the moment the second arrives, the linker blocks it on the spot. C++ has a sibling rule at the language level — we call it the ODR, the one definition rule; we name-dropped it back in part 03, and the full unfolding waits for part 07 (templates, inline, and the ODR). One detail deserves a mention: when dup.c compiles on its own, it doesn't breathe a word of complaint. We already saw the reason earlier: the compiler sees only the translation unit in front of it, b.c and dup.c don't know of each other's existence, and whose name collides with whose can only be adjudicated when all the .o files meet at the linker's desk.

## Weak Symbols: Strong and Weak Coexist, the Strong One Wins

The rule from the last section sounds hard, but it keeps one exception in reserve. Under part 04's alphabet table we still owe you W and V, with a promise to introduce them when they showed up. Today's arrival is W.

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

That `__attribute__((weak))` line in weak_def.c may be your first sight of it. The ISO C++ standard has no place for it; it comes from the GNU family of extension spellings, and both GCC and LLVM's Clang honor it. Its effect is to mark policy's definition as a weak symbol. Strong and weak are a pair of relative titles: the definitions we write every day carry no attribute at all, and the identity they land on is strong; a definition marked weak gets demoted to the weak identity. Link the three .o files together and look at each one's symbols:

```text
$ gcc use_policy.o weak_def.o strong_def.o -o poly_app && ./poly_app
policy=2
$ nm weak_def.o | grep policy
0000000000000000 W policy
$ nm strong_def.o | grep policy
0000000000000000 T policy
```

What came out is policy=2: the strong definition won. nm's two lines of output spell the identities out plainly: policy in weak_def.o is a capital W, the weak-symbol mark, while in strong_def.o it is still our old acquaintance T. In the alphabet, W covers weak functions and V covers weak data objects; today's guest is W — for V, just put a face to the name.

The rule fits in one sentence: one strong definition of a name, plus any number of weak ones, is legal to write, and the linker picks the strong one. All weak, and the linker picks the one earliest on the command line. Set that against the last section and savor the contrast: two strong definitions meet, and multiple definition fires on the spot; strong paired with weak coexist in perfect peace. This exception answers a legitimate class of needs: a library wants to stock a default implementation for some name while leaving room for users to override it, and that is precisely the job weak symbols do. glibc has them on staff: the batch of compatibility stubs for the old pthread interfaces (pthread is Linux's thread library), and the startup hook `__gmon_start__` used for profiling — all running on this same machinery. How each one gets used, we won't unfold here; just carry the impression away: in the linker's hands, besides the hard rules, there is also a strong-versus-weak adjudication.

For those of us writing C++, weak symbols carry one even more important identity. Back in part 03 we banked a question: a header gets pasted into ten translation units, bigger<int> instantiates ten same-named copies of code — how do they not fight at link time? The direction of the answer is right here. Inline functions and template instantiations get exactly this treatment from the compiler: a batch of weak definitions, paired with a mechanism called COMDAT — a section-grouping mechanism where, among same-named sections in one group, only one copy is kept at link time and the duplicates are simply dropped. The ten copies of bigger<int> merge into one this way; the full unfolding we leave to part 07 (templates, inline, and the ODR).

## static: Names That Stay Out of the Global Verdict

With strong and weak settled, there is one more class of names that never enter this adjudication at all. Look at two same-named variables:

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

Two files each write a counter, and both are writing definitions. By the hard rule laid down under multiple definition earlier, shouldn't this be blocked? Let's run it and see:

```text
$ gcc s1.o s2.o s_main.o -o static_app && ./static_app
1 2
$ nm s1.o | grep counter
0000000000000000 d counter
$ nm s2.o | grep counter
0000000000000000 d counter
```

The link passed, and the printed 1 2 are the two counters' values. Check against the nm output in our hands: the counter in both lines is a lowercase d, and part 04's alphabet told us lowercase is the mark of static-private. The formal name is internal linkage: linkage describes how wide a range a name can be seen in, and internal means it is valid only within its own translation unit. In part 03 we met another word carrying linkage: language linkage, which governs how names get mangled — don't lump the two into one thing. The two counters' names are identical, yet both are local symbols, each sitting in its home symbol table, with no trace of them in the global table. So the linker never compared them against each other, and the alleged conflict, naturally, has nothing to stand on.

The linker's jurisdiction is clear by now: what it adjudicates are only the names that enter the global symbol table — the capital T, capital D, capital B crowd from the alphabet. Lowercase private symbols, static functions and static globals, are none of its business. This boundary earns its keep in daily coding too: you put a helper function in a header, worry that it will collide once several files include it, and hanging static on it settles the matter. The C++ equivalent is the anonymous namespace, which turns it into one private copy per file, and the names simply can't collide anymore.

## Even main Isn't Special

Finally, an error you have most likely run into for real. This time we link b.o: you already know its entire inventory — shared_seed and helper are old news — the one thing missing is main.

```text
$ gcc b.o -o no_main
/usr/bin/ld: /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/Scrt1.o: in function `_start':
(.text+0x1b): undefined reference to `main'
collect2: error: ld returned 1 exit status
```

The long path on the first line is Scrt1.o's real address on my machine. You met Scrt1.o in part 01: one of the startup files that collect2 slips into every link by default. Living inside it is _start, the program's true entry point, whose name came up in part 04 when we looked at an executable's Entry address. _start's code has to call main, so in Scrt1.o's symbol table, main is a U awaiting resolution. b.o happens not to have it, the linker followed its rules to the letter, and what it reported is _start's undefined reference to main — the same species of error as a.o's two.

Look closely: this error has two places worth savoring. In the first, the star of the error isn't our code: the reference is issued from a startup file, the path attached to the error belongs to Scrt1.o, the function name is _start's, and the offset 0x1b points into Scrt1.o's own .text. Even the reported location is its own, and there is nowhere to hang a source filename — startup files come prebuilt with the compiler suite, and there is no source of ours to point at. The second sight is the linker treating everyone alike: however special the name main is in C++, by the time it reaches symbol resolution it is just an ordinary U, and lacking a definition, it gets reported like any other. Some day your fingers slip and you type mian instead of main: the compiler won't say a word, and when link time comes, the linker catches it for you.

What _start does ahead of main, and which startup file in that row handles what — those belong to part 14 (program startup), where they get the dedicated treatment. Today we only need to carry one thing away: even the program's entry point goes through symbol resolution.

## Relocation Lands: U Becomes T

Once symbol resolution has fully passed, the linker does its second job: relocation. In part 04 we read every column of the records, and we wrote the formula out with our own hands. The arithmetic the linker did on our behalf; we are here to inspect the results:

```text
$ nm a.o | grep helper
                 U helper
$ nm ok_app | grep helper
000000000000112c T helper
```

Watch helper's change of identity: before the link, in a.o, it was a U without so much as an address. After the link, it became a T with an address. The linker merged b.o's definition into ok_app, and the position it assigned to helper is 0x112c. The PIE identity we saw back in part 01. It means helper's 0x112c was settled at link time, and the whole program will shift again when it loads; the machinery behind that we'll look at in part 09 (position-independent code and GOT/PLT). The call issued from a.o's main is, at this moment, filled in following part 04's formula. Whether the empty slot truly got filled — don't take my word for it; one nm lookup and you'll know: the results of both jobs, symbol resolution and relocation, land in the final symbol table.

## From a Pile of .o to the Idea of a Library

Let's look back at the link commands we typed today. The command gcc a.o b.o -o ok_app sees nothing but .o files — not a shadow of source code. Whether b.o was just compiled by us or has been lying on the disk ready for three months, the command doesn't change a single character. Push this property one step further and it gets interesting: for a batch of definition-carrying .o files like b.o, could we compile them ahead of time, pack them neatly into one file, hand the whole package to the linker when needed, and spare ourselves starting from source every time? The finished product of that idea is the library, and the static library is the first of its kind. The concrete how of packaging, and how the linker draws from a whole pack of .o files, we leave entirely to the next piece: part 06 (Static Libraries: Packaged with ar, Linked with -l/-L).

## The CMake Perspective

The link commands we assembled by hand today count their .o files on the fingers of one hand. In a real project, a single link command routinely runs to hundreds of .o files plus a row of libraries — a list that long, who assembles it? The job of assembling lists belongs to the build system. In part 04's CMake perspective we had the OBJECT library demo_core carry a target_link_libraries line; today is exactly the moment to complete its meaning:

```cmake
target_link_libraries(app PRIVATE demo_core)
```

What it expresses is a link-time dependency: when linking app, demo_core's products come along for the ride. app is the target being linked, demo_core is the dependency brought along, and PRIVATE you have already met in parts 02 and 04 — it locks the dependency onto app itself, and downstream targets get none of the benefit. The ordering of hundreds of targets, who goes before whom, CMake works out for us from the dependency relationships and assembles into a command collect2 understands.

Assembling commands is what build systems are good at; reading errors is beyond their help. Some day you build with CMake, the link step arrives, and undefined reference floods the screen — CMake can only pass it along to you verbatim. At that moment, what you use is still today's way of reading: run nm to find whose U the name is, then confirm where the definition was supposed to come from, and check whether some target was left off. The command was assembled by CMake, the verdict was handed down by the linker, so the error has to be read in the linker's language.

## Finishing the Compilation Half

Setting out from that one line, g++ hello.cpp -o hello, in part 01, we have run the whole road one stretch at a time with our own hands: the 44,971-line hello.i (part 02), mangle.cpp's symbol strings and the on-demand bigger (part 03), demo.o's section table, symbol table, and two relocation records (part 04), and today we watched the linker pair a.o with b.o, fill the empty slots, and merge out an ok_app that reports 42. From one line of source to a running program, you have seen the true shape of every layer with your own eyes and checked it with your own hands — it is no longer hearsay.

The compilation half is now walked. The half that follows swaps in a different question: how do we get our code reused? The first step of reuse is packing a pile of .o files into a static library, and that is exactly what part 06 opens with. As for why the libstdc++ and glibc we link every day can be shared by untold numbers of programs, we leave that answer to part 08 (Dynamic Libraries Overview: Build Time and Run Time). Feel free to keep today's .o files and run them once more; see you in the next piece.
