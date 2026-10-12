---
chapter: 13
difficulty: intermediate
order: 6
platform: host
reading_time_minutes: 17
tags:
- cpp-modern
- host
- intermediate
title: "Static Libraries: Packaging with ar and Linking with -l/-L"
description: "Pack a pile of .o files into lib<name>.a with ar rcs, list the members with ar t, then read symbols member by member with nm, archive index included; link with -l and -L, and verify member-level extraction against nm and size: a member named by an undefined reference enters the executable whole, and a member nobody names contributes not a single symbol."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/06-static-libraries.md
  source_hash: cce6b5415a1419c47369695e238f73f818af03a859fa653d1bb1562415999e39
  translated_at: '2026-09-30T09:37:43+00:00'
  engine: glm
  token_count: 3040
---

# Static Libraries: Packaging with ar and Linking with -l/-L

At the end of part 05 we set one thing down as a plan: a batch of definition-carrying .o files like b.o could be compiled ahead of time, packed neatly into a single file, and handed to the linker as one package whenever needed, sparing us the work of starting from source every time. Today we get it done. You already know the raw materials: a.o and b.o are part 05's original cast, and the two of them were paired up back there — one carrying the pending-definition U, the other holding the definitions. I have two more pieces of raw material prepared:

```c
// extra.c — new material: two functions living in one file
int bonus_a(void) { return 7; }
int bonus_b(void) { return 100; }
```

```c
// a2.c — caller number two: besides helper, it also names bonus_a
extern int helper(void);
extern int bonus_a(void);
int main(void) { return helper() + bonus_a(); }
```

extra.c is the second piece of material destined for the library: one file housing the two functions bonus_a and bonus_b. a2.c is a spare second caller; the functions it names are not exactly the set a.c names. Where it earns its keep, you will see later on.

The environment disclosure, as usual: my experiments all ran in WSL2, the toolchain is still GCC 16.2.1 paired with binutils 2.47, CMake is 4.4.3, and every snippet of output in this piece was actually run. If your versions differ, details like byte counts may drift, but the mechanics themselves haven't changed in years.

## Packaging with ar: Gathering a Pile of .o into One .a

The packaging tool is called ar, short for archiver — one more member of the binutils family; the nm and readelf we used in part 04 are its siblings. It has exactly one job: take a number of files and collect them, unmodified, into a container called an archive. We compile the four object files, then type one packaging command:

```text
$ gcc -c a.c b.c extra.c a2.c
$ ar rcs libmyutil.a b.o extra.o
$ ar t libmyutil.a
b.o
extra.o
$ file libmyutil.a
libmyutil.a: current ar archive
```

Let's take the rcs in that command one letter at a time. The operation code up front is r, and it means merge with replacement: the files on the list go into the archive, and if a member of the same name is already in there, the old one gets swapped out. The two letters after it are modifiers: c means quietly create the archive if it doesn't exist yet, and lowercase s means write the archive a symbol index; what goes into that index is the next section's subject. t is another operation code, this one for listing: what ar t prints are the members' names. We put in b.o and extra.o, and what it listed was exactly those, not one extra and not one missing.

The file line reports current ar archive: an ar archive in the current format. Take the word archive literally — it is a file whose business is holding other files. Each member's complete bytes lie inside untouched, and all that wraps them is the archive's own management information: member headers, the directory, and the index — the very 244 bytes we are about to check. Hold ls -l against it: b.o is 1256 bytes, extra.o is 1328, the two together come to 2584, while libmyutil.a is 2828 bytes. The difference is exactly those 244 bytes.

A .a like this is a static library. Its defining trait: at link time the library's code is merged wholesale into the executable, and once that is done, running the program no longer requires the library file. Its counterpart, the dynamic library, waits for us at the end.

## Inside the Archive: Members and the Index

nm is not a picky eater: it reads bare .o files and .a files alike, though the two outputs don't look the same. Let's set them side by side:

```text
$ nm b.o
0000000000000000 T helper
0000000000000000 D shared_seed
$ nm extra.o
0000000000000000 T bonus_a
00000000000000b T bonus_b
$ nm libmyutil.a

b.o:
0000000000000000 T helper
0000000000000000 D shared_seed

extra.o:
0000000000000000 T bonus_a
00000000000000b T bonus_b
```

The bare .o output you know well from parts 04 and 05: the symbols line up at the top. For a .a it changes the arrangement: the member's name leads off with a colon, indented below it are that member's symbols, and a blank line stands between members. The archive itself has no symbol table — the symbols all live on the members, and nm has simply read each member in turn. The alphabet is still part 04's: helper and shared_seed still carry their old letters T and D, and bonus_a and bonus_b, the two new faces, carry T as well. Four definitions, all present and accounted for.

Now the index gets its own look. Attach -s to nm and run it again, and an extra stretch appears at the head of the archive:

```text
$ nm -s libmyutil.a

Archive index:
shared_seed in b.o
helper in b.o
bonus_a in extra.o
bonus_b in extra.o
```

Look at the first entry, `shared_seed in b.o`: it states the symbol-to-member correspondence, the symbol's name first and the member's name after. The full nm -s output would go on to repeat every member's symbols after the index, identical to the previous block; I cut that part. This correspondence is the index the s in rcs wrote, and with it the linker doesn't have to open the members one by one. Writing the index is a job with a dedicated tool of its own: ranlib, one more of the binutils family, which does to an archive exactly what ar s does, only as a command in its own right. And there is a fact I owe you: with binutils 2.47, ar writes the index by default even when you spell rc without the s — getting an archive with no index is what now takes an explicit uppercase S. Let's actually build the pair and check:

```text
$ ar rcS noidxS.a b.o extra.o && ls -l libmyutil.a noidxS.a
-rw-r--r-- 1 charliechen charliechen 2828 Sep 30 15:10 libmyutil.a
-rw-r--r-- 1 charliechen charliechen 2712 Sep 30 15:10 noidxS.a
```

The two archives hold exactly the same members, yet their sizes differ by 116 bytes: with the index, libmyutil.a is 2828 bytes; without it, noidxS.a is 2712. The difference is the index, nothing else. An index-less library still links in the hands of the local linker; it just degrades into opening the members one by one and searching by itself. The index's whole use is sparing everyone that page-flipping, so we keep writing rcs as convention has it.

## -l and -L: Asking for a Library by Name

The library has its goods now; how do we call for it at link time? Here's the command:

```text
$ gcc a.o -L. -lmyutil -o lib_app && ./lib_app; echo $?
42
```

Two options stand before us; let's get each one recognized. What follows `-l` is the library name — note, a lowercase L. When the linker sees -lmyutil, it does not go looking for a file called myutil. It follows an agreed spelling: prepend lib to the name, append a suffix to match the library's type, .a for a static library, and the filename it assembles is libmyutil.a. The dynamic library suffix .so sits in the same convention; which of the two suffixes takes precedence we leave for part 10 (Library Search Logic). `-L` means add a directory to the library search path, and `-L.` adds the current directory.

There's a ready-made counterproof of this convention. I copied the library into a directory of its own named naming, renamed it myutil.a — the lib prefix gone — and we link once more:

```text
$ gcc a.o -Lnaming -lmyutil -o naming_app
/usr/bin/ld: cannot find -lmyutil: No such file or directory
/usr/bin/ld: note to link with naming/myutil.a use -l:myutil.a or rename it to libmyutil.a
collect2: error: ld returned 1 exit status
```

When a library can't be found, cannot find is what the linker reports. The note on the second line is the more interesting one: it states the convention out loud. Either do as the note says and point at the file with `-l:myutil.a`, or change the file's name back to libmyutil.a. Why library filenames all start with lib — the whole story is in that one sentence of the note: a name that starts with lib is what the linker's name-assembly rule recognizes; whether that name looks agreeable to us is none of its business. There are ways around the convention, too: a spelling like `gcc a.o ./libmyutil.a` the linker accepts without complaint; everyday projects just don't do it that way. This name assembly comes out of the Unix lineage. On Windows the counterpart is the .lib file, and the tool doing ar's job in the MSVC family is lib.exe; that comparison we save for part 10 (Library Search Logic) and part 12 (ABI Design). As for the ordering of directories along the search path, and every situation where the library plainly exists yet gets reported as missing — all of it goes to part 10 as well. What this piece means to pin down is the convention itself.

## Member Extraction: undefined reference Calls the Shots

Out came 42, so the link succeeded. But the .a in the linker's hands — did it swallow that whole, or is there more to it? Let's run a comparison: one side links the library, the other links the same two .o files loose:

```text
$ gcc a.o -L. -lmyutil -o lib_app && ./lib_app; echo $?
42
$ gcc a.o b.o extra.o -o direct_app && ./direct_app; echo $?
42
$ nm lib_app | grep -c bonus
0
$ nm direct_app | grep bonus
0000000000001137 T bonus_a
0000000000001142 T bonus_b
$ nm lib_app | grep -E "helper|shared_seed"
000000000000112c T helper
0000000000004010 D shared_seed
```

Compare the symbol tables. Both programs printed 42, but the symbol tables are two different worlds. direct_app, linked straight, took in every .o written on the command line, in full: extra.o's two bonus functions, bytes nobody uses included, merged in without one left behind. And lib_app, linked through the library? Not one bonus name can be found; grep counts 0. What went in was only b.o's old pair, helper's T and shared_seed's D.

The discipline at work you already saw in part 05: it is symbol resolution. The linker holds a.o's two U entries, shared_seed and helper, both hanging unresolved. It opens the archive's index, looks once, and both names are registered under b.o, so it pulls out only the b.o member and merges it into the program. When extra.o's turn comes, nobody has named it, and the member stays right where it is, whole. This is member-level extraction: .o files on the command line go in wholesale; members inside an archive go in on demand, led by undefined reference. And that is the essential difference between a static library and a pile of loose .o files.

The smallest unit of extraction is the member; inside a member there is no finer picking. That claim we have to verify, so a2.o takes the stage. Its main names only helper and bonus_a, and nm a2.o indeed reports exactly those two as U. Link it and look:

```text
$ gcc a2.o -L. -lmyutil -o app2 && ./app2; echo $?
39
$ nm app2 | grep bonus
0000000000001141 T bonus_a
000000000000114c T bonus_b
```

The 39 it printed, one round of arithmetic tells you, is helper's 32 plus bonus_a's 7. Yet bonus_b's name is in the symbol table too. Nobody references it, and it came along all the same, because what got named was the member extra.o, and a member that comes in comes in whole. This is one practical lesson worth keeping: split static library members finely by function, keep one .c to one group of functions that travel together, and the granularity of extraction matches what you use. Stuff a grab-bag .c into the library, use half of it, and you still haul the whole package away. One more consideration we note without unfolding: the order of multiple libraries and .o files on the command line, and the order inside circular dependencies, both decide whether extraction succeeds — that is part 10's main event.

## After Extraction: size Comparison and Self-Containment

Let's check once more from the byte-count side:

```text
$ size lib_app direct_app app2
   text	   data	    bss	    dec	    hex	filename
   1249	    516	      4	   1769	    6e9	lib_app
   1351	    516	      4	   1871	    74f	direct_app
   1361	    516	      4	   1881	    759	app2
```

Across the three programs, the main variable sits with extra.o: whether it went in at all, and who it went in for; the difference on main's side we take separately below. lib_app's text is 1249 and direct_app's is 1351; the extra 102 bytes trace to exactly one place, extra.o merged in as a whole member. Don't press that number against the 158 that size extra.o reports on its own: merging shifts section layout and alignment, so a byte-for-byte match won't come out — the direction is clear all the same. The 112 bytes app2 carries over lib_app trace to two places: 102 are extra.o merged in whole, and the remaining 10 are how much a2's main outweighs a's main, one extra call to bonus_a. The check is ready-made as well: size a.o reports text 123 and a2.o reports 133, exactly 10 apart.

The code is in the executable now — does the library file itself still matter? We move libmyutil.a away and run again:

```text
$ mkdir -p stash && mv libmyutil.a stash/ && ./lib_app; echo $?
42
$ ./app2; echo $?
39
```

The library has been moved away, and the two programs in our hands run just the same. This is where the word static lands: the extracted code has become part of the executable's own body. The file carries the full inventory and depends on nobody at run time. That is what a self-contained program means. collect2, whom we glimpsed from a distance in part 01, is the one arranging this static merge. And its shortcoming is planted right here too: lib_app and app2 each carry their own copy of helper; link it into a hundred thousand programs and the copies number a hundred thousand; fix one bug in b.c and every program that ever linked it has to be linked again. Untold programs sharing one copy of the code is something a static library cannot offer; that belongs to another road, the one we take up at the end.

## The CMake Perspective

The routine of typing ar rcs by hand and following up with -l/-L belongs to CMake in everyday builds. Let's sneak a look at its work; five lines make the minimal project:

```cmake
cmake_minimum_required(VERSION 3.16)
project(myutil_demo C)

add_library(myutil STATIC b.c extra.c)
add_executable(app a.c)
target_link_libraries(app PRIVATE myutil)
```

In part 04 we said add_library defaults to STATIC when no type is written. Here we write the type out explicitly, so whoever reads the CMakeLists knows at a glance this target is a static library, with no digging through switches elsewhere. Add a --verbose and the commands show their shape:

```text
$ cmake --build build --verbose 2>&1 | grep -E "ar |ranlib"
/usr/sbin/ar qc libmyutil.a CMakeFiles/myutil.dir/b.c.o CMakeFiles/myutil.dir/extra.c.o
/usr/sbin/ranlib libmyutil.a
```

What we see is two steps. `ar qc` does the member-appending work: q means append to the tail only, with no checking for same-name replacement, and c is the quiet one as before. Writing the index was handed to ranlib on its own. Look at that: it is rcs split in half. The s's job CMake gave to ranlib, and we never touched ar once from beginning to end. On the linking side, for a library born in the same build, CMake simply hands libmyutil.a's path straight to the link command; the -l and -L name-assembly convention it carries on our behalf behind the scenes.

`target_link_libraries(app PRIVATE myutil)` is the same line part 05 completed for us: it expresses a link-time dependency; PRIVATE locks the dependency onto app itself, and downstream targets get none of the benefit. Where the products land has a switch too: give `CMAKE_ARCHIVE_OUTPUT_DIRECTORY` a directory, and the static library's products all land there together. CMake's libraries have plenty of advanced uses beyond these; we are here for the mechanics, and the unfolding belongs to volume seven.

## Reuse, One Copy Each

Here we are, and the idea from the end of part 05 is done: b.o and extra.o are gathered into one libmyutil.a, the name follows the convention, the index is written, the linker extracts members as undefined reference names them, and the extracted code becomes part of the executable's body. What packaging solved is the distribution of .o files. But one class of reuse packaging can't help with: template and inline definitions travel with their headers, everywhere at once, and whether that is legal, part 07 (templates, inline, and the ODR) explains for us. And there is one class we want that static libraries, with a copy in every program, cannot provide: untold programs sharing a single copy of the code, every one of them benefiting the moment the library is upgraded. That is the dynamic library's road, and the libstdc++ and glibc we link every day are used exactly that way. How that road is laid, we see in part 08 (Dynamic Libraries Overview).
