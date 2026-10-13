---
chapter: 13
difficulty: intermediate
order: 8
platform: host
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 工具链
- CMake
title: 'Dynamic Libraries: An Overview of the Build-Time and Run-Time Stages'
description: "gcc -shared -fPIC produces libmyutil.so; the linker copies no code from a dynamic library, instead registering the dependency as DT_NEEDED in the dynamic section, verified with side-by-side runs of nm and size; at run time the dynamic linker finds each registered library and maps it into memory, ldd lists the inventory, and moving the .so away shows the real startup error; two processes and a maps/smaps comparison then show how the code pages live in physical memory only once; finally, CMake's SHARED and VERSION/SOVERSION show the three-piece naming scheme."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/08-dynamic-libraries-overview.md
  source_hash: 6505173f37349aadedd3d77910e0ffbbe8a89c44f3d59ae4308b1c8244885480
  translated_at: '2026-10-13T04:07:57+00:00'
  engine: anthropic
  token_count: 3600
---

# Dynamic Libraries: An Overview of the Build-Time and Run-Time Stages

At the end of the previous article we handed ourselves a question for today: what makes libstdc++ and glibc shareable by countless programs? This question has a backstory. In the static-library article we wrote down its weakness: every executable that links it carries a full copy of its own; once a hundred thousand programs link it, there are a hundred thousand copies, and if a single bug is fixed in b.c, not one of those hundred thousand relinks can be spared. The opening article also planted half a sentence: the row of libraries collect2 brings along by default are attached as dynamic dependencies — none of their code enters our file, and exactly how they attach was left as a cliffhanger back then. Today we make good on all three statements at once.

The approach has been previewed for us too: register at build time, hook up at run time. Unpacked, it reads like this: at build time, the linker facing a dynamic library only registers it — the need for the library is written into the executable, and not a single byte of the library's code comes in. At run time, when the program starts, the dynamic linker goes through the registrations, finds every listed library, maps it into memory, hooks the addresses up, and only then does the turn of the main we wrote arrive. The library has become a file of its own, sitting in a directory on disk. What we are building today is exactly this: a dynamic library — also called a shared library in English, with the suffix .so on Linux: the unit of distribution changed, the library became an independent file, everyone shares the same one, and what remains on the program's body is just a few lines of registration.

As for the environment, the usual disclosure: my experiments all ran in WSL2, the toolchain is still GCC 16.2.1 paired with binutils 2.47, glibc is 2.44, CMake is 4.4.3, and every piece of output in this article came out of a real run. If your versions differ, details like exact byte counts and the shape of addresses may not match; the mechanisms themselves have not changed in years.

The two stages each have an agent in charge. One is the linker we followed through the whole story in the linking article; the other is the dynamic linker — the true identity of the interpreter at the end of the file output in the very first article, whose identity was revealed at the time, and today we simply call it by its own name. What each of them does and where to watch it, laid out in one table; the raw materials are the same cast from the linking article: a.c carries the two extern declarations for shared_seed and helper, b.c holds the definitions, and 10 plus 32 makes 42.

| Stage | Agent in charge | What it does | Where to look |
|------|--------|--------|----------|
| Build time | The linker | Finds the .so, checks symbols, registers the dependency — copies no code | The NEEDED lines of readelf -d |
| Run time | The dynamic linker | Finds the libraries by registration, maps them into memory, hooks up addresses | The process's maps file under /proc |

## Building the .so: -shared and -fPIC

The command that makes a dynamic library is a single one, and we verify the product along the way:

```text
$ gcc -shared -fPIC -o libmyutil.so b.c
$ readelf -h libmyutil.so | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x0
```

Let's take the two options one at a time. -shared is an instruction to the linker: the product comes out to dynamic-library spec, the ELF type is marked DYN, that row of startup files is not attached, and not even main is looked for — b.c has no main at all, yet the link went through anyway; the no_main error scene from the linking article does not get a rerun here. -fPIC stands for Position Independent Code, and it makes the compiled machine code execute correctly no matter what address it gets mapped to. Why is it indispensable? The same library gets mapped by different processes to all different addresses — you will see this with your own eyes in the run-time section later — and how it manages that at the machine-code level is precisely what the next article covers in full. You have met the DYN type before, in the object-file anatomy article, in its other half of a life: pie executables like hello_app also read DYN in the Type column. What separates the two is the Entry column: a pie executable has an entry point, and that article said it points at _start. The .so before us has an Entry of 0x0, which says outright that it has no entry — it does not run by itself; being mapped by someone else is its job. If you are curious whether it can still be run as an executable, the side article plays with exactly that.

## Registered in the Dynamic Section: DT_NEEDED

The library is in hand; now it's the caller's turn. You just practiced the link commands in the static-library article:

```text
$ gcc -c a.c -o a.o
$ gcc a.o -L. -lmyutil -o dyn_app
$ LD_LIBRARY_PATH=. ./dyn_app; echo $?
42
```

The name-assembling convention hasn't changed by a single letter — it's still the old work of -L adding a directory and -l asking for a library by name. What changed is on the linker's side: the same -lmyutil that, in the static-library article, resolved to libmyutil.a and extracted members into the program now finds a .so lying in the directory, and its job switches to registering. The newcomer LD_LIBRARY_PATH adds directories to the run-time library search path. Why does it need to serve at run time when the link went so smoothly? The rules of searching belong to the library-search article; here its only duty is to make ./dyn_app in the experiment directory run. 42 is still the familiar 42. Where did the registration land? readelf -d takes us to see — it reads the dynamic section, a region of the ELF file reserved for dynamic linking, and our dependency list lies inside it:

```text
$ readelf -d dyn_app | head -5
 (I trimmed the blank line at the top of the output)
Dynamic section at offset 0x2dd0 contains 27 entries:
 (I trimmed the table's header row)
 0x0000000000000001 (NEEDED)             Shared library: [libmyutil.so]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

We got two NEEDED lines; their formal name is DT_NEEDED, where DT takes its leading letters from dynamic tag. The libmyutil.so on the first line is the one we registered with our own hands. The libc.so.6 on the second line is a member of that row of libraries collect2 brings along by default every time — the row we met in the opening article. Now you can see what their attachment looks like: to attach is to write one line of names into the dynamic section. Half of the very first article's cliffhanger is redeemed right here. A library carries a dynamic section of its own too: inside libmyutil.so there is likewise a NEEDED line recording libc.so.6 — the road of a library depending on a library works the same way, and each side keeps its own list.

## Did the Code Come In: nm and size, Side by Side

To pin down the difference between registering and copying, we verify with three things; the first one looks at symbols:

```text
$ nm dyn_app | grep -E "helper|shared_seed"
                 U helper
0000000000004018 B shared_seed
```

helper still carries U. On the static side, in the static-library article, lib_app's helper was already a T with an address — b.o's machine code had been merged in. In today's dyn_app it is a reference left hanging, its definition staying untouched inside libmyutil.so. The shared_seed line looks odd: how did it find lodging in the executable, and in bss of all places? This is a separate procedure the linker runs for data symbols, arranging the variable's storage under the executable's name, to be reconciled with the library's side at run time; the machine-code particulars belong to the next article — for now, just take note of the phenomenon.

The second thing to check is size. We take the same a.o and link it down the two roads, static and dynamic:

```text
$ gcc -c b.c -o b.o && ar rcs libmyutil.a b.o && gcc a.o ./libmyutil.a -o static_app
$ size static_app dyn_app
   text	   data	    bss	    dec	    hex	filename
   1249	    516	      4	   1769	    6e9	static_app
   1472	    600	      8	   2080	    820	dyn_app
```

Writing the libmyutil.a path directly works too — the linker takes it as it comes; we tried that in the static-library article, and it even spares the linker the chore of choosing among a roomful of .a and .so files (the rules of choosing belong to the library-search article). The result you probably didn't expect: the dynamically linked executable is, of all things, bigger. And how much bigger deserves a precise yardstick: size's text column grew from 1249 to 1472, up 223 bytes. If you instead weigh the files on disk with ls -l, you measure 15872 versus 15992, a difference of 120 bytes — the two numbers use different yardsticks. The library's code indeed did not come in, but dynamic linking's own luggage did. size -A shows it section by section: the growth concentrates in the sections that serve dynamic linking. .dynamic grew from 416 to 496; .dynsym and .dynstr — the dynamic symbol table and its string table — each thickened by a few dozen bytes; and new arrivals include 32 bytes of .plt (the name PLT got a distant glance in the opening article — the springboard that function calls jump through; the main act is the next article's business) and 24 bytes of .rela.plt. The real code in .text, meanwhile, dropped from 279 to 268, and the 11 bytes that came out are exactly helper's machine code — measure b.o with readelf -S and its .text is those very 11 bytes in total. For a library this small, the overhead of registering costs more than the library code itself. What if the library were more than this? We fatten the library by 4KB and take another shot:

```c
int shared_seed = 10;
int helper(void) { return 32; }
const int big_table[1024] = {1, 2, 3};   // b_fat.c: the 4KB constant table in the same member
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

On the static side, the 4KB table dragged in the entire b_fat.o member (the static-library article covered this: the smallest unit of extraction is the member), and text grew by 4124. On the dynamic side, 1472 became 1471, and the 1 in question comes from the registered library name being one character shorter: libmyfat.so has one fewer character than libmyutil.so, so .dynstr shrank by 1 byte along with it. We cross-checked both sides section by section with readelf -S: apart from .dynstr, not a thing moved — it amounts to no change at all. This side-by-side settles the difference between registering and copying: the executable's girth rises and falls together with the static library's contents and has nothing whatever to do with the dynamic library's contents; all it carries away is a few lines of registration.

## Run Time: The Dynamic Linker Hooks Up Libraries by Registration

When the program starts running, the first order of business is not main. The interpreter at the end of the file output in the very first article is, in the flesh, the dynamic linker — a program living at /lib64/ld-linux-x86-64.so.2. When the kernel loads the executable it spots the interpreter there, and so it loads the dynamic linker in ahead of everything and starts running from it: it reads the NEEDED list, finds every entry, opens it, maps it into the address space, hooks up the addresses that need hooking, and only then hands control to the program's _start. The fine print of this whole pipeline belongs to the program startup article; today we look only at its two ends, and the list itself ldd alone will show us:

```text
$ LD_LIBRARY_PATH=. ldd dyn_app
	libmyutil.so => ./libmyutil.so (0x000076c91d16f000)
	libc.so.6 => /usr/lib/libc.so.6 (0x000076c91ce00000)
	/lib64/ld-linux-x86-64.so.2 => /usr/lib64/ld-linux-x86-64.so.2 (0x000076c91d195000)
```

How to read it: the left column is the name, the right side of => is the path actually found, and inside the parentheses is the address it was assigned. Our registered libmyutil.so was found at ./libmyutil.so by way of LD_LIBRARY_PATH; libc was found at the system's /usr/lib/libc.so.6. The vdso line I trimmed is a small piece of code the kernel maps in so that high-frequency clock reads need not trap into the kernel — unrelated to our topic. The most interesting line is the last one: the dynamic linker lists itself too; its own identity is also a .so.

What if an entry on the list can't be found? We move the library away and run it for real so you can see:

```text
$ mkdir -p stash && mv libmyutil.so stash/ && LD_LIBRARY_PATH=. ./dyn_app; echo $?
./dyn_app: error while loading shared libraries: libmyutil.so: cannot open shared object file: No such file or directory
127
```

The error before you probably looks familiar. It and the linking article's undefined reference are errors from two different stages: that one is reported by the build-time linker, and what it stops is the link. This one is reported by the run-time dynamic linker, and the moment the error appears the process exits outright, with an exit code of 127 — main doesn't get a single line of its turn. mv the library back and the program runs again immediately, without the executable changing by a single byte. There is also a more frustrating scenario: the file is clearly there, yet it insists it can't be found — that's the search path failing to cover it, and the troubleshooting playbook belongs to the library-search article.

## How Sharing Works: Two Processes, One Page

Now back to the main question posed in the linking article: what lets countless programs share? Our observation window is /proc: every process has its own directory under /proc, with the PID as the directory's name, and the maps file inside lists the mappings in that process's address space line by line. I prepared a three-line caller, d_hold.c, so the process would stay alive for us to inspect: it declares helper, calls it once, stores the return value in a local variable, then suspends itself with pause(). We link it against the dynamic library and start two instances; the two process numbers we noted are P1 and P2. I moved this pair of files onto a disk directory and ran an extra sync, and the reason becomes clear once you see the Clean column in the smaps output below (smaps is the sister file next to maps, recording one extra set of counters per mapping):

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
(the rest of P2's lines mirror P1's line for line; I trimmed them)
```

There are two points to watch. The first is about addresses: the two processes mapped the same library at their own respective addresses — virtual addresses are each process's own, since every process's address space keeps to itself to begin with — and what is genuinely shared is the physical-memory layer. The second is about the inode: the two columns 08:30 757322 in each line record the device number and the inode number; an inode is a file's number within the filesystem, and both processes matched the same 757322 — the file being mapped was, all along, the same one. The physical layer is invisible to maps, so we call on its neighbor smaps: for the same mappings, one extra set of counters is recorded under each segment, and the lines starting with Shared count the physical memory in that segment that is shared with other processes. We extract the code segment (r-xp) of each of the two processes:

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

Both processes book this page of code as 4 kB under Shared_Clean: one page of physical memory, counted on two processes' heads. Clean means the page's contents match the file on disk — the extra sync ahead of time was aimed at exactly that. If you look right after the file has just been written out, or your /tmp is a memory-backed disk, that column will read Dirty; no harm done, just make sure the word is Shared. The control group is the data segment (rw-p); we look at P1's (P2's three numbers match it to the digit):

```text
$ awk '/rw-p.*libmyutil\.so/{p=1;print;next} p&&/^[0-9a-f]+-/{exit} p{print}' /proc/$P1/smaps | grep -E "^[0-9a-f]+-|^(Rss|Shared_Clean|Private_Dirty):"
7343e14c1000-7343e14c2000 rw-p 00003000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
Rss:                   4 kB
Shared_Clean:          0 kB
Private_Dirty:         4 kB
```

The data page is a private copy on both sides: it is meant to be written — the dynamic linker already patched addresses into it at load time (what got patched, we'll see in the next article) — and the program's reads and writes to it never stop afterward, each process changing its own. Nobody writes the code page, so the kernel lets every process that uses it point at the same physical page. The linking article's answer is complete as of here: across every process on the machine, the file mapped as libc is the same one, and its code pages live in physical memory exactly once. Once ten thousand programs use libc, there is still one copy of the pages in memory and one copy of the file on disk. Of the two ills the static library left behind — ten thousand copies in memory, and a full relink of everything on upgrade — the first is solved here, and the fix for the second is right below: swap the .so file alone, without touching a single byte of the executable:

```text
$ gcc -shared -fPIC -o libmyutil.so b_v2.c    # same interface, helper now returns 52
$ LD_LIBRARY_PATH=. ./dyn_app; echo $?
62
```

We didn't recompile dyn_app, nor did we do any relinking; run it again and we get the new version's 52 — 10 plus 52 makes 62: upgrading a dynamic library means swapping one file, and every program that ever used it picks up the new version at its next start. This holds on one precondition: the interface didn't move. What the function is named, what arguments it takes, what it returns — these outward agreements together are called the ABI (Application Binary Interface; the opening article's roadmap gave the full name). Once the interface changes, the swapped-in library no longer matches the old programs — that is the ABI article's subject in full.

## The CMake View

The hand-typed -shared and -fPIC routine all falls to CMake in everyday builds. We take the static version of the project from the static-library article; the differences come down to these few spots:

```cmake
cmake_minimum_required(VERSION 3.16)
project(myutil_dyn C)
add_library(myutil SHARED b.c)
set_target_properties(myutil PROPERTIES VERSION 1.2.3 SOVERSION 1)
add_executable(app a.c)
target_link_libraries(app PRIVATE myutil)
```

STATIC became SHARED. The list keeps only b.c — this article's demo has no use for the second member, which still lives in the static-library article's project. The extra properties line we'll get to shortly. What CMake brought along for us, --verbose makes plain to see:

```text
$ cmake --build build --verbose 2>&1 | grep -E "\-fPIC"
/usr/sbin/cc -fPIC -shared -Wl,--dependency-file=CMakeFiles/myutil.dir/link.d -Wl,-soname,libmyutil.so.1 -o libmyutil.so.1.2.3 CMakeFiles/myutil.dir/b.c.o
```

The compile line I trimmed carries -fPIC too. In the link line that remains, both -fPIC and -shared are present — everything we typed by hand is there — and it does one extra job: `-Wl,-soname,libmyutil.so.1`, registering the library's formal name into it. The product came out as a three-piece set; let's take a look with ls:

```text
$ ls -l build/libmyutil.so*
lrwxrwxrwx 1 charliechen charliechen    14 Sep 30 15:53 build/libmyutil.so -> libmyutil.so.1
lrwxrwxrwx 1 charliechen charliechen    18 Sep 30 15:53 build/libmyutil.so.1 -> libmyutil.so.1.2.3
-rwxr-xr-x 1 charliechen charliechen 15176 Sep 30 15:53 build/libmyutil.so.1.2.3
```

The real file is libmyutil.so.1.2.3, carrying the full version number; the two levels in front of it are symlinks, and we take them one by one. VERSION 1.2.3 records the complete version, SOVERSION 1 records the interface generation, and the linker spliced it into the SONAME (soname is short for shared object name — the formal name the library registers for itself):

```text
$ readelf -d build/libmyutil.so.1.2.3 | grep SONAME
  0x000000000000000e (SONAME)             Library soname: [libmyutil.so.1]
$ readelf -d build/app | grep -E "NEEDED|RUNPATH"
 0x0000000000000001 (NEEDED)             Shared library: [libmyutil.so.1]
 (I trimmed the line where app depends on libc)
 0x000000000000001d (RUNPATH)            Library runpath: [/tmp/dynlib_demo/build]
```

app's NEEDED records libmyutil.so.1: it records neither the development-time libmyutil.so nor the concrete libmyutil.so.1.2.3, but precisely the interface-generation rung. You can see the benefit: when 1.2.4 ships some day, swap out the real file, adjust the symlinks, and app doesn't need a single relink. How to arrange the three-piece set and how to settle the version numbers are details left to the library-search article and the ABI article. The RUNPATH line is the build-directory search path CMake writes in as a convenience, which is why our ./build/app runs without LD_LIBRARY_PATH. We ran it, and got the familiar 42. There is also a global switch, BUILD_SHARED_LIBS: once we set it to ON, an add_library call without an explicit type defaults to SHARED instead of STATIC — our measured product was libmyutil.so. But a global switch is an all-or-nothing cut, less precise than stating the type explicitly per target, so real projects usually still state it explicitly.

## What Goes into That call Line

Both stages are done, and one last spot remains unseen: registration settles which library is wanted; what it doesn't settle is what the address is. In a.o, the call line where main calls helper — in the linking article we watched the linker fill in the offset on the spot during static linking. In today's dyn_app, what ends up filled into that spot? readelf -r leaked the clue to us long ago:

```text
$ readelf -r dyn_app | grep -E "COPY|JUMP"
000000004018  000700000005 R_X86_64_COPY     0000000000004018 shared_seed + 0
000000004000  000500000007 R_X86_64_JUMP_SLO 0000000000000000 helper + 0
```

You know all these columns — we read them one column at a time in the object-file anatomy article: Offset says where to patch, Sym. Name says whom to look for, the Addend acts as the correction term, and JUMP_SLO is readelf's column width truncating JUMP_SLOT. Only one thing has changed: for how these two relocations get filled, the linker itself can supply nothing — the addresses don't exist until the library is mapped in, and the one filling the table has been swapped for the run-time dynamic linker. What actually sits at that spot in the machine code, how the code manages to run no matter which address it's mapped to, and which table the address hookup goes through — the next article devotes itself to exactly this one thing. Keep libmyutil.so around; that article uses it again.
