---
chapter: 13
difficulty: intermediate
order: 8
platform: host
reading_time_minutes: 25
tags:
- cpp-modern
- host
- intermediate
title: "Dynamic Libraries: An Overview of Build-Time and Run-Time"
description: "Build libmyutil.so with gcc -shared -fPIC; the linker copies none of a dynamic library's code, instead writing the dependency into the dynamic section as DT_NEEDED — verified with nm and size side by side. At run time the dynamic linker follows the record to find each library and map it in; ldd shows the list, and moving the .so away shows a real startup error. Then two processes go under the microscope, maps against smaps, to see how one code page lives only once in physical memory. Finally, CMake's SHARED and VERSION/SOVERSION explain the three-piece naming."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/08-dynamic-libraries-overview.md
  source_hash: 7045598177370555bd03b217be02cbafec63dabcfe9cf32502d646cf068d42d8
  translated_at: '2026-09-30T09:39:31+00:00'
  engine: glm
  token_count: 4521
---

# Dynamic Libraries: An Overview of Build-Time and Run-Time

At the end of 07 we handed a question forward to today: libstdc++, glibc — how do they get away with being shared by untold numbers of programs? The question has a back story, too. In 06 we noted down the static library's shortcoming: every executable that links one carries a complete copy of its own, so when a hundred thousand programs link it, there are a hundred thousand copies — and when a bug in b.c gets fixed, not one of a hundred thousand relinks is excused. And in 01 we planted half a sentence: that row of libraries collect2 carries by default is attached as dynamic dependencies, their code never entering our file — just how such attaching works, we left as a cliffhanger at the time. Today we make good on all three at once.

The line of thinking was announced for us in advance: register at build time, connect at run time. Said in full — at build time, the linker facing a dynamic library does nothing but register. It writes the need for it into the executable, and not one byte of the library's code comes in. At run time, when the program starts, the dynamic linker takes the record, finds every library on it, maps it into memory, and gets the addresses wired; only then does our main get its turn. The library itself has become a file of its own, lying in a directory on the disk. The thing we make today is a dynamic library — it also goes by the name shared library — and on Linux its suffix is .so: the unit of distribution has changed. The library stands on its own as a file, what everyone shares is one and the same file, and all a program keeps on its person is a few lines of record.

The environment disclosure, as always: my experiments ran in WSL2, the toolchain is still GCC 16.2.1 with binutils 2.47, glibc is 2.44, and CMake is 4.4.3. Every stretch of output in this piece was genuinely run. If your versions differ, details like byte counts and the look of addresses may drift; the mechanics themselves, though, haven't budged in years.

The two stages have two agents between them. One is the linker, whose whole workday we followed in 05; the other is the dynamic linker — the true identity of the interpreter at the end of file's output in 01. Its identity was unmasked back then; today we simply call it by its real name. What each of them does, and where to watch them do it, one table lays out. The raw material is still 05's old cast: a.c carries the two extern declarations shared_seed and helper, b.c does the defining, and 10 plus 32 makes 42.

| Stage | Agent | What it does | Where to look |
|-------|-------|--------------|---------------|
| Build time | The linker | Finds the .so, checks symbols, records the dependency — copies no code | The NEEDED lines of readelf -d |
| Run time | The dynamic linker | Follows the record to find each library, maps it into memory, wires up addresses | The process's maps file under /proc |

## Building a .so: -shared and -fPIC

The command for making a dynamic library is one line, and we verify the artifact while we're at it:

```text
$ gcc -shared -fPIC -o libmyutil.so b.c
$ readelf -h libmyutil.so | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x0
```

We'll take the two options one at a time. -shared is an instruction to the linker: turn out the artifact to a dynamic library's spec. The ELF type gets marked DYN, the row of startup files doesn't get attached, and it doesn't even go looking for main — b.c has no main to begin with, and the link still passed; the no_main failure we staged in 05 doesn't get an encore here. -fPIC spells out Position Independent Code, and it makes the machine code come out executing correctly no matter which address it gets mapped to. Why is it non-negotiable? The same library will be mapped by different processes to different addresses — you'll see that with your own eyes in the run-time section below — and how the machine code manages it at the machine-code level is exactly the one subject of 09 (Position-Independent Code and GOT/PLT), the whole piece. You've already met the other half of type DYN in 04: a pie executable like hello_app also shows DYN in its Type column. What separates the two is the Entry column: a pie executable has an entry, which 04 said points at _start. The .so in front of us reads Entry 0x0 — as good as declaring outright that it has no entry. It doesn't run itself; being mapped by someone else is its job. And if you're curious whether it can be run as an executable, 16's side story plays exactly that game.

## The Record Lives in the Dynamic Section: DT_NEEDED

The library is here; the caller's turn has come. The link command you just practiced in 06:

```text
$ gcc -c a.c -o a.o
$ gcc a.o -L. -lmyutil -o dyn_app
$ LD_LIBRARY_PATH=. ./dyn_app; echo $?
42
```

The name-assembly convention hasn't changed one character — still the old work of -L adding a directory and -l asking for a library by name. What changed sits at the linker's end. The same -lmyutil: in 06 it unwrapped libmyutil.a, drew out the member, and merged it into the program; this time a .so is lying in the directory, and the job switches to registering. The extra LD_LIBRARY_PATH exists to add a directory to the paths searched for libraries at run time. Why, when link time went smoothly, does run time still need it standing by? The way searching is governed belongs to 10 (Library Search); in this piece it has only one duty, getting ./dyn_app in the experiment directory to run. And 42 is still the familiar 42. Where did the record get written? readelf -d walks us there. What it reads is the dynamic section — the region of the ELF set aside for dynamic linking's use — and our dependency list is lying inside it:

```text
$ readelf -d dyn_app | head -5
 (I trimmed the blank line at the top of the output)
Dynamic section at offset 0x2dd0 contains 27 entries:
 (I trimmed the table's header row)
 0x0000000000000001 (NEEDED)             Shared library: [libmyutil.so]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

We've got two NEEDED lines. Their formal name is DT_NEEDED, the DT_ prefix marking a dynamic tag's name. The first line's libmyutil.so is the one we registered by hand. The second line's libc.so.6 is one member of that row of libraries collect2 slips in by default every time — the row we met in 01. Now you've seen what their being attached looks like: attached means, precisely, that one line of name got written into the dynamic section. That pays off half of 01's cliffhanger. The library wears a dynamic section too: inside libmyutil.so there is likewise a NEEDED line recording libc.so.6. A library depending on a library is a road that works — each keeps its own list.

## Did the Code Come In? nm and size Side by Side

The difference between registering and copying — we test it with three instruments, and the first looks at symbols:

```text
$ nm dyn_app | grep -E "helper|shared_seed"
                 U helper
0000000000004018 B shared_seed
```

helper still gets U. On the static-linking side, 06's lib_app already had helper as a T with an address: b.o's machine code had been merged in. In today's dyn_app it is a reference still hanging, its definition left exactly where it was, inside libmyutil.so. The shared_seed line looks odd: how did it get an address in the executable — and in bss, of all places? This is a separate piece of paperwork the linker does for data symbols: it arranges the variable's storage under the executable's name, and at run time the library's side lines itself up with it. The machine-code-level ins and outs belong to 09; for now, just file the phenomenon away.

The second instrument looks at bulk. We take the same a.o and send it down each road, static and dynamic:

```text
$ gcc -c b.c -o b.o && ar rcs libmyutil.a b.o && gcc a.o ./libmyutil.a -o static_app
$ size static_app dyn_app
   text	   data	    bss	    dec	    hex	filename
   1249	    516	      4	   1769	    6e9	static_app
   1472	    600	      8	   2080	    820	dyn_app
```

Writing the path of libmyutil.a directly is something the linker accepts too — we tried that in 06 — and it spares the linker from picking through a roomful of .a and .so; the rules of that picking belong to 10. The result you probably didn't see coming: the dynamically linked executable came out bigger. By how much demands its yardstick be stated: size's text column went from 1249 up to 1472, a growth of 223 bytes. If you weigh the files on disk with ls -l, what you measure is 15872 against 15992, a difference of 120 bytes — the two numbers are two different yardsticks, each measuring its own thing. The library's code truly didn't come in; what came in is dynamic linking's own gear. We look section by section with size -A: the growth gathers in the batch of sections that serve dynamic linking. .dynamic rose from 416 to 496; .dynsym and .dynstr — the dynamic symbol table and its string table — each put on a few dozen bytes; and there are newcomers, 32 bytes of .plt (the name PLT got one distant glance in 01 — the trampoline function calls go through; the main act is 09's) and 24 bytes of .rela.plt. The genuine code, .text, actually fell from 279 to 268 — and the 11 bytes it shed are precisely helper's machine code. We measured b.o with readelf -S: its .text comes to exactly those 11 bytes. For a library this small, the paperwork of registering costs more than the library's code itself. And what if the library were bigger than this? We feed the library 4KB more and shoot again:

```c
int shared_seed = 10;
int helper(void) { return 32; }
const int big_table[1024] = {1, 2, 3};   // b_fat.c: the 4KB constant table, living in this same member
```

```text
$ gcc -shared -fPIC -o libmyfat.so b_fat.c && gcc -c b_fat.c -o b_fat.o && ar rcs libmyfat.a b_fat.o
$ gcc a.o ./libmyfat.a -o static_fat_app && gcc a.o -L. -lmyfat -o dyn_fat_app && size static_app static_fat_app dyn_app dyn_fat_app
   text	   data	    bss	    dec	    hex	filename
   1249	    516	      4	   1769	    6e9	static_app
   5373	    516	      4	   5893	   1705	static_fat_app
   1472	    600	      8	   2080	    820	dyn_app
   1471	    600	      8	   2079	    81f	dyn_fat_app
```

On the static side, the 4KB table dragged in the entire b_fat.o member (we covered this in 06: the smallest unit of extraction is the member), and text rose by 4124. On the dynamic side, 1472 became 1471 — and that 1 is the registered library name coming up one character shorter: libmyfat.so has one character fewer than libmyutil.so, and .dynstr shrank a byte to match. We put both sides through readelf -S, section against section: apart from .dynstr, nothing moved by a hair — which amounts to nothing having moved. This round of comparing settles the register-don't-copy distinction: an executable's girth rises and falls together with the static library's contents, and has nothing to do with the dynamic library's contents. All it carries off is a few lines of record.

## Run Time: The Dynamic Linker Follows the Record

When the program runs, the first order of business is not main. The interpreter at the tail of file's output in 01 — its true identity is the dynamic linker, a program living at /lib64/ld-linux-x86-64.so.2. The kernel, loading the executable, catches sight of it and loads the dynamic linker ahead of everything, starting the run from there: what the dynamic linker reads is the NEEDED list; it finds each entry, opens it, maps it into its address space, wires up the addresses that need wiring, and only then hands control to the program's _start. The fine detail of this whole pipeline belongs to 14 (Program Startup); today we watch only its two ends. The list itself, ldd shows us:

```text
$ LD_LIBRARY_PATH=. ldd dyn_app
	libmyutil.so => ./libmyutil.so (0x000076c91d16f000)
	libc.so.6 => /usr/lib/libc.so.6 (0x000076c91ce00000)
	/lib64/ld-linux-x86-64.so.2 => /usr/lib64/ld-linux-x86-64.so.2 (0x000076c91d195000)
```

How to read it: the left column is the name, what follows => is the path actually found, and in the parentheses is the address it was assigned. The libmyutil.so we registered was found at ./libmyutil.so by way of LD_LIBRARY_PATH; libc resolved to the system's /usr/lib/libc.so.6. The vdso line I trimmed is a small stretch of code the kernel maps in, letting frequent operations like reading the clock skip the trap into the kernel — beside our subject. The most interesting line is the last one: the dynamic linker put itself on the list too, its own identity also being a .so.

What happens when an entry on the list can't be found? We move the library out of the way and run the thing for real:

```text
$ mkdir -p stash && mv libmyutil.so stash/ && LD_LIBRARY_PATH=. ./dyn_app; echo $?
./dyn_app: error while loading shared libraries: libmyutil.so: cannot open shared object file: No such file or directory
127
```

The error in front of you is probably an old acquaintance. It and 05's undefined reference are errors from two different stages: that one came from the build-time linker, and what it blocked was the link. This one comes from the run-time dynamic linker; the moment it prints, the process exits on the spot, exit code 127, and not one line of main got its turn. We mv the library back and the program runs again immediately — without a single byte of the executable having been touched. There's a more frustrating situation still: the file is sitting right there, and it insists it cannot be found. That is the search paths failing to cover it, and the way to chase that down belongs to 10.

## How Sharing Works: Two Processes, One Page

Now we return to the main question 05 set down: untold numbers of programs — how do they get to share? Our observation window is /proc: every process owns a directory under /proc, the PID is the directory's name, and the maps file inside lists, one line per mapping, everything mapped into that process's address space. I prepared a three-line caller, d_hold.c, to hold a process still for us: it declares helper, calls it once, stashes the return value in a local variable, then suspends itself with pause(). We link it against the dynamic library, start two instances, and note the two process numbers as P1 and P2. That pair of files I moved onto a directory on real disk, plus one extra sync — the reason will meet you at smaps's Clean column below (smaps is maps's sister file; it records an extra set of counters for each mapping):

```text
$ cp libmyutil.so hold_app /var/tmp/dynlib_share/ && sync
$ LD_LIBRARY_PATH=/var/tmp/dynlib_share /var/tmp/dynlib_share/hold_app & P1=$!
$ LD_LIBRARY_PATH=/var/tmp/dynlib_share /var/tmp/dynlib_share/hold_app & P2=$!
$ grep libmyutil.so /proc/$P1/maps
7343e14bd000-7343e14be000 r--p 00000000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
7343e14be000-7343e14bf000 r-xp 00001000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
(I trimmed the two read-only segments in between)
7343e14c1000-7343e14c2000 rw-p 00003000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
$ grep libmyutil.so /proc/$P2/maps
7eef9ec3c000-7eef9ec3d000 r-xp 00001000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
(P2's remaining lines mirror P1's line for line; I trimmed them)
```

Two points deserve your eyes. The first is about addresses: the two processes mapped the same library to addresses of their own. Virtual addresses are each process's own business — every address space minds its own — and the thing actually shared is the physical-memory layer. The second is about the inode: the 08:30 757322 pair of columns in the line records the device number and the inode number. The inode is a file's serial number inside the filesystem, and what the two processes turn up between them is the same 757322 — what got mapped is one and the same file. The physical layer maps cannot see; for that we call on smaps next door: same mappings, but under each segment an extra set of counters, and the lines that start with Shared count the physical memory this segment holds in company with other processes. Let's pull out the code segment (r-xp) of each process:

```text
$ awk '/r-xp.*libmyutil\.so/{p=1;print;next} p&&/^[0-9a-f]+-/{exit} p{print}' /proc/$P1/smaps | grep -E "^[0-9a-f]+-|^(Rss|Shared_Clean|Shared_Dirty|Private_Dirty):"
7343e14be000-7343e14bf000 r-xp 00001000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
Rss:                   4 kB
Shared_Clean:          4 kB
Shared_Dirty:          0 kB
Private_Dirty:         0 kB
$ awk '/r-xp.*libmyutil\.so/{p=1;print;next} p&&/^[0-9a-f]+-/{exit} p{print}' /proc/$P2/smaps | grep -E "^[0-9a-f]+-|^(Rss|Shared_Clean|Shared_Dirty|Private_Dirty):"
7eef9ec3c000-7eef9ec3d000 r-xp 00001000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
Rss:                   4 kB
Shared_Clean:          4 kB
 (the two zero-valued lines match P1's; I trimmed them)
```

This page of code, in both processes, records Shared_Clean: 4 kB — one page of physical memory, recorded under both processes at once. Clean says the page's contents still match the file on disk, and the early sync was aimed squarely at that. If you look right after the file has been written out, or if your /tmp is an in-memory filesystem, that column will read Dirty instead. That's fine; what matters is spotting the word Shared. For contrast we take the data segment (rw-p); here is P1's (P2's three numbers match it exactly):

```text
$ awk '/rw-p.*libmyutil\.so/{p=1;print;next} p&&/^[0-9a-f]+-/{exit} p{print}' /proc/$P1/smaps | grep -E "^[0-9a-f]+-|^(Rss|Shared_Clean|Private_Dirty):"
7343e14c1000-7343e14c2000 rw-p 00003000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
Rss:                   4 kB
Shared_Clean:          0 kB
Private_Dirty:         4 kB
```

The data page is private on both sides: it is written to. At load time the dynamic linker already altered addresses inside it (what it altered — we'll see in 09), and the program's reads and writes into it never stop afterward, each process amending its own. The code page nobody writes, so the kernel points every process that uses it at the same physical page. And here the answer to 05 is complete: across the whole machine, every process mapping libc maps the same one file, and its code pages live exactly once in physical memory. Let the programs using libc number ten thousand — the pages in memory stay one copy, and the file on disk stays one copy. Of the two troubles static libraries left behind — ten thousand copies sitting in memory, and every program needing a relink at upgrade time — the first is solved here; the second's remedy is right below. Swap only the .so file, and not one byte of the executable has to move:

```text
$ gcc -shared -fPIC -o libmyutil.so b_v2.c    # same interface, helper now returns 52
$ LD_LIBRARY_PATH=. ./dyn_app; echo $?
62
```

We didn't recompile dyn_app, did no relinking either, and the next run hands us the new 52 — 10 plus 52 makes 62. Upgrading a dynamic library swaps exactly one file, and every program that ever used it picks the change up at its next launch. The claim stands on one premise: the interface didn't move. What the functions are called, what parameters they take, what they return — these outward commitments taken together are called the ABI (Application Binary Interface; the roadmap in 01 gave the full name). Once the interface changes, the library you swap in no longer matches the old programs — and that is the entire subject of 12 (ABI Design).

## The CMake Perspective

This routine of typing -shared and -fPIC by hand is CMake's department in everyday builds. We fetch 06's static-version project, and the differences amount to just these few spots:

```cmake
cmake_minimum_required(VERSION 3.16)
project(myutil_dyn C)
add_library(myutil SHARED b.c)
set_target_properties(myutil PROPERTIES VERSION 1.2.3 SOVERSION 1)
add_executable(app a.c)
target_link_libraries(app PRIVATE myutil)
```

STATIC traded for SHARED. Only b.c remains in the source list — this piece's demo has no need of a second member, and it's still in 06's project. The added properties line we'll come to shortly. What CMake carried in on our behalf, --verbose lays bare:

```text
$ cmake --build build --verbose 2>&1 | grep -E "\-fPIC"
/usr/sbin/cc -fPIC -shared -Wl,--dependency-file=CMakeFiles/myutil.dir/link.d -Wl,-soname,libmyutil.so.1 -o libmyutil.so.1.2.3 CMakeFiles/myutil.dir/b.c.o
```

The compile line I trimmed carries -fPIC just the same. In the link line that survived, -fPIC and -shared are both there: the two things we typed by hand, both covered, plus one job more — `-Wl,-soname,libmyutil.so.1`, registering the library's official name into it. The artifact comes down as a three-piece set; one glance with ls:

```text
$ ls -l build/libmyutil.so*
lrwxrwxrwx 1 charliechen charliechen    14 Sep 30 15:53 build/libmyutil.so -> libmyutil.so.1
lrwxrwxrwx 1 charliechen charliechen    18 Sep 30 15:53 build/libmyutil.so.1 -> libmyutil.so.1.2.3
-rwxr-xr-x 1 charliechen charliechen 15176 Sep 30 15:53 build/libmyutil.so.1.2.3
```

The real body is the fully versioned libmyutil.so.1.2.3; the two levels in front are symbolic links, and we'll get to know them in order. VERSION 1.2.3 records the complete version; SOVERSION 1 records the interface generation, and the linker worked that into the SONAME (soname is short for shared object name — the official name a library registers for itself):

```text
$ readelf -d build/libmyutil.so.1.2.3 | grep SONAME
  0x000000000000000e (SONAME)             Library soname: [libmyutil.so.1]
$ readelf -d build/app | grep -E "NEEDED|RUNPATH"
 0x0000000000000001 (NEEDED)             Shared library: [libmyutil.so.1]
 (I trimmed the line where app depends on libc)
 0x000000000000001d (RUNPATH)            Library runpath: [/tmp/dynlib_demo/build]
```

What app's NEEDED records is libmyutil.so.1: it doesn't record the development-time libmyutil.so, nor the concrete libmyutil.so.1.2.3 — what it records is precisely the interface-generation tier. The payoff is in plain sight: the day 1.2.4 arrives, swap the real body and repoint the symlink, and not one line of app gets relinked. How the three pieces are arranged and how the version numbers are chosen — the details belong to 10 and 12. The RUNPATH line is the build-directory search path CMake wrote in along the way, which is why our ./build/app runs without any LD_LIBRARY_PATH. We ran it; what came out was still 42. There is also a global switch, BUILD_SHARED_LIBS: set it to ON, and an add_library that doesn't spell out a type flips its default from STATIC to SHARED — the artifact we measured came out libmyutil.so. But a global switch is one cut for everything; it can't match the precision of writing the type explicitly on each target, and that explicit spelling is what projects usually keep.

## What Goes into That call Line

Both stages walked, one last spot remains unseen: registering settles which library is wanted; it does not settle what the address is. The call instruction in a.o, where main calls helper — in 05 we watched the linker fill its offset in on the spot under static linking. In today's dyn_app, what will land in that position? readelf -r had already let the clue out to us:

```text
$ readelf -r dyn_app | grep -E "COPY|JUMP"
000000004018  000700000005 R_X86_64_COPY     0000000000004018 shared_seed + 0
000000004000  000500000007 R_X86_64_JUMP_SLO 0000000000000000 helper + 0
```

You know every one of these columns; we read them column by column in 04: Offset says which spot gets changed, Sym. Name says who is being looked up, Addend plays the correction term, and JUMP_SLO is JUMP_SLOT as truncated by readelf's column width. Only one thing has changed: the filling of these two relocations is not something the linker itself can supply. The addresses exist only once the libraries are mapped in, and the hand doing the filling has changed to the run-time dynamic linker. What exactly sits in that position in the machine code, how the code runs no matter which address it lands at, and which table the address wiring goes through — that one thing is 09's entire piece. Keep libmyutil.so where it is; 09 still uses it.
