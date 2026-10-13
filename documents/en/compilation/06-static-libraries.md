---
chapter: 13
difficulty: intermediate
order: 6
platform: host
reading_time_minutes: 12
tags:
- cpp-modern
- host
- intermediate
- 工具链
- CMake
title: 'Static Libraries: Packaging with ar and Linking with -l/-L'
description: 'Pack a pile of .o files into lib<name>.a with ar rcs, list the members with ar t, read symbols member by member with nm including the archive index; link with -l and -L, and verify member-level extraction with nm and size: a member named by an undefined reference enters the executable whole, and a member nobody names contributes not a single symbol.'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/06-static-libraries.md
  source_hash: 28e8d4f4f5510ec98f314af562ae68a4e8d4dd163b9c809c02f0e9c922473bb6
  translated_at: '2026-10-13T03:57:14+00:00'
  engine: anthropic
  token_count: 2500
---

# Static Libraries: Packaging with ar and Linking with -l/-L

At the end of the previous article we set ourselves one task: a batch of definition-carrying .o files like b.o can be compiled ahead of time, tucked neatly into a single file, and handed to the linker as one package whenever needed, sparing us the work of starting over from source every time. Today we get that done. You already know the raw materials: a.o and b.o are the same pair from the previous article, where they were matched up — one carrying a pending-definition U, the other holding the definitions. I have prepared two new ingredients:

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

extra.c is the second ingredient destined for the library; one file houses the two functions bonus_a and bonus_b. a2.c is a spare second caller; the functions it names are not exactly the set a.c names, and where it earns its keep you will see later on.

As for the environment, the usual disclosure: my experiments all ran in WSL2, the toolchain is still GCC 16.2.1 paired with binutils 2.47, CMake is 4.4.3, and every line of output in this article came out of a real run. If your versions differ, details like exact byte counts may not match; the mechanisms themselves have not changed in years.

## ar: Packing a Pile of .o Files into One .a

The packing tool is called ar, short for archiver — another member of the binutils family; nm and readelf, which we used in the object-file anatomy article, are its siblings. It handles exactly one job: take a number of files and store them verbatim inside something called an archive. Let's compile the four object files and type one packing command:

```text
$ gcc -c a.c b.c extra.c a2.c
$ ar rcs libmyutil.a b.o extra.o
$ ar t libmyutil.a
b.o
extra.o
$ file libmyutil.a
libmyutil.a: current ar archive
```

Let's take the rcs in the command one letter at a time. The leading operation code is r, which means replace and insert: files in the list go into the archive, and if a member of the same name is already there, the old one gets swapped out. The two that follow are modifiers: c means quietly create the archive if it does not exist yet, and lowercase s means write a symbol index into the archive — what that index contains is something we examine on its own in the next section. t is another operation code, the listing one: what ar t prints are the member names. We put in b.o and extra.o, and what it lists is neither one more nor one less.

The file line reports current ar archive, which says exactly what it is: an ar archive in the current format. Take the notion of an archive file literally — it is a file that exists to hold other files. The members' complete bytes lie inside untouched; wrapped around them is only the archive's own bookkeeping — member headers, the directory, and the index, which is precisely the 244 bytes we are about to reconcile. Check it with ls -l: b.o weighs 1256 bytes and extra.o 1328, the two together 2584, while libmyutil.a is 2828 bytes — the excess is exactly those 244 bytes.

A .a like this is a static library. Its defining trait: at link time the library's code is merged wholesale into the executable, and the library file is no longer needed once the program runs. Its counterpart, the shared library, we will meet at the end.

## Inside the Archive: Members and the Index

nm is not a picky reader — it takes both bare .o files and .a archives — but the shape of its output differs between the two. Look at them side by side:

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

The bare-.o output you know well from the object-file anatomy article and the previous one: symbols lined up at the top. For a .a it switches to a different layout: the member's name with a colon leads, the member's symbols sit indented underneath, and a blank line separates the members. The archive itself has no symbol table; the symbols all live on the members, and nm is simply reading each member in turn. The letter scheme is the one from the object-file anatomy article: helper and shared_seed keep their old letters T and D, and the two newcomers bonus_a and bonus_b also draw T — four definitions in all.

The index deserves its own look. Run nm again with -s, and an extra section appears at the top of the archive's output:

```text
$ nm -s libmyutil.a

Archive index:
shared_seed in b.o
helper in b.o
bonus_a in extra.o
bonus_b in extra.o
```

Take the first line, `shared_seed in b.o`: it states the symbol-to-member mapping, symbol name first, member name second. Full nm -s output repeats every member's symbols verbatim after the index — identical to the block above, so I trimmed it. This mapping is exactly the index that the s in rcs wrote down; consulting it, the linker is spared opening members one by one. Writing the index also has a dedicated tool: ranlib, yet another binutils utility, which does to an archive exactly what ar s does — it just exists as a separate command. One honest disclosure on my part: when binutils 2.47's ar builds a new archive, it writes the index by default even with plain rc; to get an index-free archive you must instead pass the uppercase S explicitly. Let's actually build a pair and check:

```text
$ ar rcS noidxS.a b.o extra.o && ls -l libmyutil.a noidxS.a
-rw-r--r-- 1 charliechen charliechen 2828 Sep 30 15:10 libmyutil.a
-rw-r--r-- 1 charliechen charliechen 2712 Sep 30 15:10 noidxS.a
```

The two archives carry identical members yet differ by 116 bytes: libmyutil.a with the index is 2828 bytes, noidxS.a without it 2712 — the difference is precisely the index. An index-free library still links under the local linker; the linker just degrades to opening members one by one and finding things itself. The index exists to spare it that search. We will stick with convention and write rcs.

## -l and -L: Asking for a Library by Name

The library has goods on the shelf; at link time, how do we call for it? Look at the command:

```text
$ gcc a.o -L. -lmyutil -o lib_app && ./lib_app; echo $?
42
```

Two options sit in front of us; let's take each in turn. What follows `-l` is the library name — mind that it is a lowercase L. When the linker sees -lmyutil, it does not go looking for a file called myutil; it follows an agreed-upon spelling: prefix the name with lib, then append the suffix for the library's kind — .a for a static library — and the file name it spells out is libmyutil.a. The shared-library suffix .so belongs to the same convention; which of the two suffixes gets tried first is a matter we leave to the library-search article. `-L` means adding a directory to the library search path; `-L.` adds the current directory.

There is a ready-made counterexample to this convention. I copied the library into a separate naming directory and renamed it myutil.a — the lib prefix is gone — then linked again:

```text
$ gcc a.o -Lnaming -lmyutil -o naming_app
/usr/bin/ld: cannot find -lmyutil: No such file or directory
/usr/bin/ld: note to link with naming/myutil.a use -l:myutil.a or rename it to libmyutil.a
collect2: error: ld returned 1 exit status
```

When a library cannot be found, what the linker reports is cannot find. The note on the second line is more interesting still: it states the convention out of its own mouth — either follow the note and name the file with `-l:myutil.a`, or rename it back to libmyutil.a. Why library file names all start with lib: the full story sits in that one note line. A name starting with lib is what the linker's name-spelling rule recognizes; whether the result looks pleasing to us is none of its concern. Opting out of the convention is also possible: a form like `gcc a.o ./libmyutil.a` is accepted by the linker too — it just is not how day-to-day projects do it. This name-spelling scheme comes from the Unix lineage; the Windows counterpart is .lib, and MSVC's tool that does ar's kind of work is lib.exe — the side-by-side belongs to the library-search article and the ABI article. As for the ordering of directories in the search path, and the various situations where a library plainly exists yet the link reports it missing, all of that goes to the library-search article too; what this article sets down is the convention itself.

## Member Extraction: Led Around by Undefined References

42 came out, so the link went through — but that .a in the linker's hands, did it swallow the thing whole, or are there finer rules? Let's run a comparison: one side links through the library, the other links the two loose .o files directly:

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

Read against the symbol tables: both programs printed 42, yet the two tables are different worlds. direct_app, linked straight from the command line, takes in every .o listed there, in full — extra.o's two bonus functions together with every byte nobody uses merged into direct_app without dropping a single one. And lib_app, linked through the library? Not one bonus name can be found; grep counts 0. All that got extracted were b.o's old pair — helper's T and shared_seed's D.

The mechanism you saw in the previous article: symbol resolution. The linker holds two Us from a.o, shared_seed and helper, both still unresolved. It opens the archive's index, looks the two names up, finds both registered under b.o, and so pulls out only the b.o member and merges it into the program. When it reaches extra.o, nobody has called its name, and the member stays put in its entirety. This is member-level extraction: .o files on the command line enter in full, while archive members enter on demand, led by undefined references. That is the essential difference between a static library and a pile of loose .o files.

The smallest unit of extraction is the member; inside a member there is no finer picking. This is a claim we should verify, so let's put a2.o on stage. Its main names only helper and bonus_a, and nm a2.o indeed reports just those two Us. Link and look:

```text
$ gcc a2.o -L. -lmyutil -o app2 && ./app2; echo $?
39
$ nm app2 | grep bonus
0000000000001141 T bonus_a
000000000000114c T bonus_b
```

The 39 that came out, you can work out in your head, is helper's 32 plus bonus_a's 7. Yet bonus_b's name is in the symbol table too — nobody references it, and it came in all the same. Because what got named was the member extra.o, and a member enters whole. This yields a practical engineering lesson: split a static library's members finely by function — one .c holds one group of functions that live and fall together — and the granularity of extraction will line up. Stuff one grab-bag .c into the library, and using half of it still hauls in the whole package. One more subtlety we note but do not unfold: the order of multiple libraries and .o files on the command line, and ordering inside circular dependencies, both affect whether extraction succeeds — that is the library-search article's proper subject.

## After Extraction: the size Comparison and Self-Containment

Let's reconcile from the byte-count side once more:

```text
$ size lib_app direct_app app2
   text	   data	    bss	    dec	    hex	filename
   1249	    516	      4	   1769	    6e9	lib_app
   1351	    516	      4	   1871	    74f	direct_app
   1361	    516	      4	   1881	    759	app2
```

The variance across the three lines lands mainly on extra.o: whether it got in, and once in, whom it serves; the difference on the main side we take up separately below. lib_app's text is 1249 and direct_app's 1351 — those extra 102 bytes have exactly one origin: extra.o, merged in as a whole member. Do not pit this number against the 158 that size extra.o reports on its own: when sections merge, their placement and alignment all shift, so a byte-for-byte match is out of reach, but the direction is clear. The 112 bytes by which app2 exceeds lib_app have two origins: 102 are extra.o merged in whole, and the remaining 10 are a2's main being bigger than a's main, because it calls bonus_a one extra time. The evidence is ready-made: size a.o reports a text of 123 and a2.o reports 133 — a difference of exactly 10.

The code is inside the executable now; is the library file itself still needed? Let's move libmyutil.a away and run:

```text
$ mkdir -p stash && mv libmyutil.a stash/ && ./lib_app; echo $?
42
$ ./app2; echo $?
39
```

The library is gone, and both programs in our hands still run. This is where the word static lands: the extracted code has become part of the executable's own body — the file carries the full set itself, depends on nobody at run time. Self-contained is the word for a program like that. collect2, glimpsed from afar in the opening article, is the one staging this static merging. This also plants its shortcoming: lib_app and app2 each carry their own copy of helper; link it into a hundred thousand programs and there are a hundred thousand copies; fix one bug in b.c and every program that ever linked it has to be relinked, all of them. Countless programs sharing one copy of the code is more than a static library can deliver — that is another road's business, and we close on it.

## The CMake View

Typing ar rcs plus -l/-L by hand — in day-to-day builds all of that is CMake's job. Let's take a peek at its work; five lines make the smallest project:

```cmake
cmake_minimum_required(VERSION 3.16)
project(myutil_demo C)

add_library(myutil STATIC b.c extra.c)
add_executable(app a.c)
target_link_libraries(app PRIVATE myutil)
```

We said in the object-file anatomy article that when add_library omits the type, the default is STATIC. Here we write the type explicitly, so anyone reading the CMakeLists knows at a glance that this target is a static library, without hunting for switches elsewhere. Give it a --verbose and the commands show their true form:

```text
$ cmake --build build --verbose 2>&1 | grep -E "ar |ranlib"
/usr/sbin/ar qc libmyutil.a CMakeFiles/myutil.dir/b.c.o CMakeFiles/myutil.dir/extra.c.o
/usr/sbin/ranlib libmyutil.a
```

What we see is two steps. `ar qc` does the job of appending members to the archive: q means append at the tail only, no same-name replacement check, and c still handles being quiet. The index-writing job is handed off separately to ranlib. Notice — this is exactly what rcs split in half looks like: CMake gave s's work to ranlib, and we never touched ar once. On the linking side, for libraries built in the same build, CMake simply hands the path of libmyutil.a straight to the link command; the -l and -L name-spelling convention is carried behind the scenes on our behalf.

`target_link_libraries(app PRIVATE myutil)` is still that line we completed in the previous article: it expresses a link-time dependency, and PRIVATE locks the dependency onto app itself, leaving downstream targets with no share of it. Where the artifacts land has a switch too: give `CMAKE_ARCHIVE_OUTPUT_DIRECTORY` a directory, and the static library's outputs all land there together. CMake's libraries have a whole pile of advanced uses; we only take in the mechanism — the unfolding belongs to volume 7.

## Reuse, Each Carrying Its Own Copy

Here we have brought the previous article's parting idea to completion: b.o and extra.o are packed into one libmyutil.a, the name follows the convention, the index is written, the linker extracts members at the calling of undefined references, and the extracted code becomes the executable's own body. What packing solved for us is the distribution of .o files. But one special kind of reuse is beyond packing's help: template and inline definitions must travel everywhere inside header files, and their legitimacy is the next article's dedicated subject. And one more kind we want is what each-carries-its-own static libraries cannot give: countless programs sharing one single copy of the code, every program benefiting the moment the library upgrades — that road is the shared library's, and the libstdc++ and glibc we link every day are used exactly that way. How that road is paved, we will see in the shared-library overview article.
