---
chapter: 13
difficulty: intermediate
order: 16
platform: host
reading_time_minutes: 24
tags:
- cpp-modern
- host
- intermediate
title: "Side Story: Can a Dynamic Library Run as an Executable?"
description: "Running a default .so directly ends in a segmentation fault: in strace execve succeeds and SIGSEGV follows at once, and gdb proves RIP lands on the read-only page holding the file header — Entry 0x0 plus the base address is exactly the landing spot; the counterexample libc.so.6 runs directly and prints a ten-line banner, its entry __libc_main nothing more than one write plus _exit, and it carries an INTERP and takes the standard two-stage loading; then -Wl,-e to name an entry and bare system calls to print the banner build libdual.so, one binary serving three uses — run directly, linked against, and dlopen'd — with the stack-protector canary crash at the bare entry run into and solved along the way; ld.so itself is a DYN with an entry; CMake's CXX_VISIBILITY_PRESET and target_link_options land the build in a project, and POST_BUILD runs the artifact directly as a smoke test."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/16-dynamic-lib-as-executable.md
  source_hash: 74d1cbf9b77fc1c0848a1b33b5d763c8b62ed4feb2148c61ce24d5afaca5c5e4
  translated_at: '2026-10-02T09:42:24+00:00'
  engine: glm
  token_count: 4271
---

# Side Story: Can a Dynamic Library Run as an Executable?

When we closed out the main body of the last piece, we left a sentence behind: the Entry registered in libmyutil.so's header reads 0x0 — can it actually be run directly, and what happens if you run it anyway? That is the game the side story plays from end to end. Today we come to hand in our papers. The answer we set out up front, three layers of it: the .so that comes out by default cannot run; it crashes at the entry. libc.so.6 can run, because its entry is a real one. And we can roll one that runs ourselves — pointing the linker at an entry is all it takes. The evidence for all three layers was really run on this machine, and we take them one layer at a time.

The environment disclosure as usual: my experiments all ran in WSL2, on the combination of GCC 16.2.1 plus binutils 2.47, with glibc at 2.44 and CMake at 4.4.3, strace at 7.2 and gdb at 17.2 — the six versions part 14 reported, not one of them swapped on our machine today. Every stretch of output in this piece was really run.

## Taking the Crash Ourselves: Three Files, Three Ends

The specimen is still the volume's familiar old face: the command part 08 used for making a dynamic library, run once more exactly as it stood, with b.c still holding the two old definitions from part 05:

```text
$ gcc -shared -fPIC -o libmyutil.so b.c
$ ls -l libmyutil.so
-rwxr-xr-x 1 charliechen charliechen 15176 Oct  2 16:40 libmyutil.so
```

One spot I must report to you honestly. By the telling of most materials, a .so leaves the factory without the execute bit, and running one takes a round of chmod afterward. On my machine it is precisely not so: the local ld drops the artifact at 755 outright, the execute bit born with it. To watch the permission gate with our own eyes, we had to take it off by hand:

```text
$ chmod -x libmyutil.so
$ bash -c './libmyutil.so; echo $?'
bash: line 1: ./libmyutil.so: Permission denied
126
```

This time execve truly reached the kernel's hands: one glance at the missing execute bit and the kernel shoved it right back, returning the error code EACCES (permission denied); bash translated that code into Permission denied, and the exit code handed back is 126 — the file was found, yet never truly executed, and this old conventional code is exactly what the shell gives for that. We put the execute bit back and run again:

```text
$ chmod +x libmyutil.so
$ bash -c './libmyutil.so; echo $?'
bash: line 1: 206363 Segmentation fault         ./libmyutil.so
139
```

This time the kernel took the file in, and the process came up; the end it met the very instant after coming up was a segmentation fault. Exit code 139's origin is 128 plus 11, the kernel's old code for reporting SIGSEGV — the account we settled once in part 13. One end on its own still looks too flat, so we call 05's a.o and 08's dyn_app on stage as well. a.o is the object file gcc -c turned out, dyn_app the old program linked with -lmyutil; a.o got the execute bit added too, so the permission gate would not steal the scene. We set the three files in a row:

```text
$ bash -c './a.o; echo $?'
bash: line 1: ./a.o: cannot execute binary file: Exec format error
126
$ bash -c './libmyutil.so; echo $?'
bash: line 1: 206378 Segmentation fault         ./libmyutil.so
139
$ LD_LIBRARY_PATH=. ./dyn_app; echo $?
42
```

Of the three files' ends, each walked farther than the last: the .o never got past the kernel's checks at all; the .so got past the kernel, brought a process up, and died on the way. The normal program walked the whole road and handed in its papers with 42. We read the three headers side by side, and the differences take shape:

```text
$ readelf -h a.o | grep -E "Type|Entry"
  Type:                              REL (Relocatable file)
  Entry point address:               0x0
$ readelf -h libmyutil.so | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x0
$ readelf -h dyn_app | grep -E "Type|Entry"
  Type:                              DYN (Position-Independent Executable file)
  Entry point address:               0x1040
```

(readelf -h's full output runs twenty lines; from each I kept the two lines Type and Entry.) a.o's Type is REL: the kernel's ELF loader accepts only two types, EXEC and DYN; REL is not on the list of the accepted, and execve backed out on the spot, returning the error code ENOEXEC. The Exec format error in the error text is its face — the kernel's meaning is utterly plain: it does not recognize this ELF's type. Bash, though, having taken this code, gives one more rescue attempt, trying it once more as a shell script; one look, binary, and only then does it report cannot execute binary file. libmyutil.so's and dyn_app's Types are, of all things, the same. We compared this in part 04: a pie program's reads DYN as well, and the difference in the parentheses is only readelf's footnote — going by the DF_1_PIE flag in the dynamic section, it recognized the PIE; Type itself never split into families. What truly split is Entry: a program has an entry, a .so's entry is 0. So by what right did the kernel let this Entry-0x0 DYN through, and at which step did the process die? We catch it in the act.

## Why It Crashes: execve Succeeded, the Death Came After the Handover

We call strace onto the stage. In part 14 it walked the whole startup chain with us; today we watch only the head and the tail:

```text
$ strace -o /tmp/w3.txt ./libmyutil.so
$ cat /tmp/w3.txt
execve("./libmyutil.so", ["./libmyutil.so"], 0x7fffba6301c0 /* 70 vars */) = 0
--- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_ACCERR, si_addr=0x7c7a94ad4000} ---
+++ killed by SIGSEGV +++
```

(We wrote it into a file before reading; the trace's full text is just these three lines, and we trimmed not one of them. si_addr and the env pointer on the stack are the values this one run turned out — a rerun will bring new ones, but the death notice never changes.) The first line's content alone is surprise enough: execve returned 0 — it succeeded. The long string of openat and mmap that followed execve in part 14 — not one of them can we find: at this moment, the job of loading libraries has not yet come round for anybody to do, though for the present it needs none either, as we will understand in a moment. The second line right behind is the death notice, and it carries three fields along: si_signo reports SIGSEGV; si_code gives SEGV_ACCERR, which says the address is mapped and the permission is wrong — when an address has no mapping at all, what gets given would be SEGV_MAPERR. si_addr records 0x7c7a94ad4000: it is page-aligned, and it looks like the base address of a mapping. What the kernel does inside execve, let us go over once more: following the LOAD segments of the program header, the kernel maps the file into memory; the DYN type receives the same treatment a pie gets, namely a randomized base address. The file before us carries no INTERP segment, so there is no second file to load either. Then the kernel hands control to the entry registered in the header. The entry is 0x0? The kernel does not check that, in fact — base plus 0, and it jumps there all the same. Where the jump landed, gdb photographed for us (it starts the process with address randomization off, so the numbers hold steady):

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex run -ex 'p/x $rip' -ex 'info proc mappings' ./libmyutil.so
Program received signal SIGSEGV, Segmentation fault.
0x00007ffff7ffa000 in ?? ()
$1 = 0x7ffff7ffa000
0x00007ffff7ffa000 0x00007ffff7ffb000 0x1000  0x0     r--p  /tmp/.../lab/libmyutil.so
0x00007ffff7ffb000 0x00007ffff7ffc000 0x1000  0x1000  r-xp  /tmp/.../lab/libmyutil.so
```

(gdb's few startup notices, the process line and the maps header, the stretches of vvar, vdso, and the stack, and libmyutil.so's latter two LOADs I trimmed; the long paths I shortened, and the whitespace between columns I tightened — the addresses are as they came.) RIP's value is 0x7ffff7ffa000, and the first line of maps is exactly it: libmyutil.so's first LOAD, file offset recorded as 0x0, permission given as r--p. The truth is whole at this point: the so-called entry 0x0 plus the base address — the landing spot is the file's first page, the page the ELF header lives on, and that page is read-only, with no execute permission. The CPU arrived there to fetch an instruction, the MMU reported exactly the permission mismatch, and that matches the SEGV_ACCERR in strace. So the default .so never met a refusal: the kernel executed it as ordered — execve succeeded, control was handed over, and then the process died on the landing spot of the handover. In part 08 we wrote down half a sentence: Entry reading 0x0 is as good as saying outright that there is no entry. The second half of that sentence I complete for you today: it died precisely on the page the file header lives on.

## The Counterexample: libc.so.6 Runs Just Fine

The default .so cannot run — is the matter settled there? There really is one that runs, and it is the very one you use every day. glibc's real body — we type it straight in:

```text
$ /usr/lib/libc.so.6; echo $?
GNU C Library (GNU libc) stable release version 2.44.
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.
There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
Compiled by GNU CC version 16.2.1 20260810.
libc ABIs: UNIQUE IFUNC ABSOLUTE
Minimum supported kernel: 4.4.0
For bug reporting instructions, please see:
<https://gitlab.archlinux.org/archlinux/packaging/packages/glibc/-/issues>.
0
```

The banner ran ten lines, the process left the stage respectably, and the exit code handed back is 0. The same DYN identity — on what grounds does it manage? We turn its holdings out and set them against the previous one's (it passes the execute-bit gate by birth too: ls -l shows 755 here as well):

```text
$ readelf -h /usr/lib/libc.so.6 | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x279c0
$ readelf -l /usr/lib/libc.so.6 | grep -A2 INTERP
  INTERP         0x00000000001c4be0 0x00000000001c4be0 0x00000000001c4be0
                 0x000000000000001e 0x000000000000001e  R      0x10
      [Requesting program interpreter: /usr/lib/ld-linux-x86-64.so.2]
```

The holdings laid out come to three full items. The execute bit is born with it. The registered Entry is 0x279c0, a non-zero value with no disputing it. And the program header table holds one entry more, an INTERP, registering ld-linux as the interpreter. The path INTERP registers carries one small difference: in part 14's link line, what GCC passed by default is `/lib64/ld-linux-x86-64.so.2`, while this machine's libc.so.6 has `/usr/lib/ld-linux-x86-64.so.2` written in its INTERP — the path is nailed down at build time, but /lib and /lib64 are both symlinks on this machine, and the two spellings land on the same file. The first two items are the hard conditions for running; the third means: when it runs, it takes the standard two-stage loading part 14 told of — the kernel loads the real body of libc.so.6 together with its interpreter, the dynamic linker finishes the relocations, and only then does control pass into the hands of 0x279c0. And who is it that lives at this entry? We turn the entry out for a look:

```text
$ objdump -d --start-address=0x279c0 --stop-address=0x27a20 /usr/lib/libc.so.6
00000000000279c0 <__libc_main>:
   279c0:	f3 0f 1e fa          	endbr64
   279c4:	55                   	push   %rbp
   279c5:	bf 01 00 00 00       	mov    $0x1,%edi
   279ca:	ba e3 01 00 00       	mov    $0x1e3,%edx
   279cf:	48 8d 35 ea aa 19 00 	lea    0x19aaea(%rip),%rsi        # 1c24c0 <banner>
   279d6:	48 89 e5             	mov    %rsp,%rbp
   279d9:	e8 62 ac 0e 00       	call   112640 <__write>
   279de:	31 ff                	xor    %edi,%edi
   279e0:	e8 8b e6 0b 00       	call   e6070 <_exit>
```

(objdump's few header lines, the alignment nops at the tail, and the next function, _Unwind_Resume, that the stop address swept in with it, I trimmed.) The entry's name is __libc_main, and its body is one straight line: write's three arguments made ready — edi holds descriptor 1, rsi points at the banner string, edx carries 0x1e3. What it calls is __write, glibc's internal alias; the people of libc call their own by it directly, and it does not go through the PLT. write finished its work, edi was zeroed, and the ending was collected with call _exit. 0x1e3 in decimal is 483; we measured it with wc -c, and it is the byte count of the ten banner lines above, not one off. Notice that it did not use printf: at the moment the entry takes control, __libc_start_main has in fact never run, and not one of stdio's buffers has been stood up — so glibc does not gamble on that either, using the most primitive write plus a direct exit. Exactly this caution is what we will have a use for shortly.

## Rolling One Ourselves: One File, Three Uses

Having learned libc.so.6's way of it, we roll one ourselves. We set the goal out tidily: the three uses one .so must serve are being run directly, being linked by somebody else, and being dlopen'd — what goes out the door is one and the same binary, and not even one byte needs changing. We paste the seventeen lines of source in full:

```cpp
// dual.cpp — one file meant to serve three uses: run directly, linked, dlopen'd
// Outward interface: extern "C" bare name + default visibility; linkers and by-name fetchers both recognize it
extern "C" __attribute__((visibility("default"))) int add(int a, int b) {
    return a + b;
}

// Process entry: when run directly, control lands here. Marked hidden; stays out of the export table
extern "C" __attribute__((visibility("hidden"))) void dual_start() {
    const char msg[] = "dual: 作为可执行文件跑起来了\n";
    // write(1, msg, len): syscall 1. No stdio: the entry's stack is bare,
    // __libc_start_main never ran; two system calls, gambling on nothing else
    asm volatile("syscall" : : "a"(1L), "D"(1L), "S"(msg), "d"(sizeof(msg) - 1) : "rcx", "r11", "memory");
    // exit_group(0): syscall 231. Not 60 (exit): 60 reaps the current thread only;
    // 231 reaps the whole process — proper endings all go through it
    asm volatile("syscall" : : "a"(231L), "D"(0L) : "rcx", "r11", "memory");
    __builtin_unreachable();  // exit_group never returns
}
```

A couple of sentences on each of the two. add is the outward interface: `extern "C"` keeps the name bare — part 13 once forgot it, dlsym went looking with calc_plugin_entry's name while what lay in the table was _Z17calc_plugin_entryv, and it came up empty. Default visibility is the opening left for it; paired with the build-time global narrowing, it is exactly the pair of partners part 11 established. dual_start is the entry prepared for running directly: marked hidden, it stays out of the export table. Its body holds just two system calls — write sends the banner to descriptor 1, and exit_group collects the ending. In part 14's trace, it was the finale of the show; a single-threaded process can exit with syscall 60's exit too, but 60 reaps only the current thread while 231 reaps the whole process, so we write it the proper way. Why the hair-splitting strictness? Our .so does not mean to carry an INTERP: the kernel finishes mapping the LOAD segments and hands control over on the spot, nobody does the relocations, nobody fills the GOT's tables, and the runtime library has not had one byte initialized — the entry's code must be self-sufficient. The syscall instruction itself depends on no relocation; the banner's bytes the compiler moved bodily onto the stack with movabs (the instruction that loads a 64-bit immediate), and the address-taking goes by rbp-relative addressing — not one relocation spent there either. Two calls, write plus exit_group, are enough for us to live on. The build command:

```text
$ g++ -fno-stack-protector -shared -fPIC -o libdual.so dual.cpp -Wl,-e,dual_start
$ readelf -h libdual.so | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x10fd
```

`-Wl,-e,dual_start` means passing the linker an `-e` (entry) switch: registering the entry is exactly what it is for. The spelling of `-Wl` passing arguments by comma is the one 08's -soname and 10's -rpath both used. Entry changed from 0x0 to 0x10fd — dual_start's address. As for where -fno-stack-protector came from, the story is a crash I genuinely hit: the first version of the command went without it, and running it directly still produced 139. We take one look with gdb:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex run -ex 'x/2i $pc' ./libdual_sp.so
Program received signal SIGSEGV, Segmentation fault.
0x00007ffff7ffb101 in dual_start ()
=> 0x7ffff7ffb101 <dual_start+4>:	mov    %fs:0x28,%rax
   0x7ffff7ffb10a <dual_start+13>:	mov    %rax,-0x8(%rbp)
```

(gdb's few startup notices and the blank lines I trimmed.) The point of death landed at dual_start+4, and there is no such mov in our code. What it went to read is fs:0x28 — that is the stack protector's prologue: the canary — the check value the compiler inserts into functions by default to catch stack-overflow overwrites — is stored right there, and the road to fetching it has to go by the FS segment base into thread-local storage. At a process entry, TLS has not been established yet; FS holds 0, and reading address 0x28 died on the spot. Our source had touched not one line of the standard library — the default hardening option touched it on our behalf. With -fno-stack-protector added, the prologue was withdrawn, and only then did the program run through. Time for acceptance: we run the three uses one after another:

```text
$ ./libdual.so; echo $?
dual: 作为可执行文件跑起来了
0
$ g++ -std=c++17 host.cpp -L. -ldual -o host && LD_LIBRARY_PATH=. ./host; echo $?
host: add(1,2)=3
0
$ g++ -std=c++17 dlhost.cpp -o dlhost && ./dlhost; echo $?
dlhost: add(1,2)=3
0
```

(host.cpp's job is linking against it and calling add; dlhost.cpp walks dlopen plus dlsym by part 13's three steps. Both are small programs of a few lines, which I tucked into the experiment pack so they take no lines from the main text.) In the first use's whole run, the dynamic linker has no part: the kernel mapped the segments, handed control to dual_start, two system calls ran their course, and it left the stage. In the latter two uses, add's treatment did not change — resolved and called as ever, one walking the old -l registration road, the other the old runtime-loading road. We verify the export table with one glance too:

```text
$ nm -D libdual.so
                 w __cxa_finalize@GLIBC_2.2.5
                 w __gmon_start__
00000000000010e9 T add
```

(The w weak hooks are the linker's accompanying pieces; of the four lines I trimmed two, and the ones left cannot get out the door either.) What remains at the door is the clean bare name add — part 13's bare-name discipline was held this time from birth. One file with all three lives to its name — at this point the tally is complete.

## The Dynamic Linker Is a Program Too

Last, a look at one with a special standing. In part 14, ld-linux's post was the interpreter, doing its work inside other people's program headers; today we invite it in its own person to the opposite side of execve:

```text
$ /lib64/ld-linux-x86-64.so.2; echo $?
/lib64/ld-linux-x86-64.so.2: missing program name
Try '/lib64/ld-linux-x86-64.so.2 --help' for more information.
1
$ readelf -h /lib64/ld-linux-x86-64.so.2 | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x217e0
```

It did not crash, in fact; its complaint is that we came up short with the arguments: the usage it expects is itself in the lead, with an executable plus arguments following — using it to start a program explicitly. The opening of --help is its self-introduction: the heading reads program interpreter, and that names exactly its role of being the interpreter for dynamically linked programs. Ordinarily it gets called up automatically; today we called for it once by hand. Its Type reads DYN, its Entry is non-zero too, and it carries the execute bit as well: the one that does the loading for others day in and day out is itself a DYN with an entry. Two lives for one and the same ELF — today we have laid eyes on both.

## The CMake Perspective

We hand this build over to CMake; the template's length you can take in at a glance:

```cmake
cmake_minimum_required(VERSION 3.16)
project(dual_run CXX)
add_library(dual SHARED dual.cpp)
# Visibility: global narrowing, the interface released at one point (add carries the default attribute in dual.cpp)
set_target_properties(dual PROPERTIES CXX_VISIBILITY_PRESET hidden)
# The entry's stack is bare; disable the stack protector's canary prologue
target_compile_options(dual PRIVATE -fno-stack-protector)
# -Wl,-e,dual_start: point the process entry at our own dual_start
target_link_options(dual PRIVATE "-Wl,-e,dual_start")
# Execute-bit safety net (this machine's ld grants it by default); in passing, run the artifact once as a smoke test
add_custom_command(TARGET dual POST_BUILD
    COMMAND /usr/bin/chmod +x $<TARGET_FILE:dual>
    COMMAND $<TARGET_FILE:dual>)
```

CXX_VISIBILITY_PRESET is the C++ counterpart of C_VISIBILITY_PRESET, which part 11's CMake section covered — one back-reference and we are on our way. The two switches, the entry and the stack protector, are exactly where we crashed a moment ago; entering the project, we set them accordingly. We build once with --verbose, and the link line and the smoke test both take shape:

```text
$ cmake -B build -S . && cmake --build build --verbose
[ 50%] Building CXX object CMakeFiles/dual.dir/dual.cpp.o
/usr/sbin/c++ -Ddual_EXPORTS  -fPIC -fvisibility=hidden -fno-stack-protector ... -c .../dual.cpp
[100%] Linking CXX shared library libdual.so
/usr/sbin/c++ -fPIC -shared -Wl,-e,dual_start -Wl,--dependency-file=... -Wl,-soname,libdual.so -o libdual.so CMakeFiles/dual.dir/dual.cpp.o
/usr/bin/chmod +x .../build/libdual.so
./libdual.so
dual: 作为可执行文件跑起来了
```

(The scaffolding lines of CMake and make — the entering-and-leaving-directory notices, the dependency checks, the progress bar all among them — together with the closing Built target line, I trimmed; the long paths and a few middle stretches I shortened.) `-fvisibility=hidden` went onto every compile line, and add's default attribute lets it through at that one point — the partners part 11 established look exactly like this inside a project. `-Wl,-e,dual_start` landed on the link line, and that one glance is what we were after. About the chmod line inside POST_BUILD, an honest word: this machine's ld grants the artifact the execute bit to begin with, so on our machine it spins idle; kept, it is the safety net for build environments whose default does not hand the bit out. The last command ran the artifact directly, the banner went into the build log, and the smoke test counts as passed: the acceptance check of whether the thing can run as a program — the build system took care of it for us on its own.

## The Volume's End: The Boundary Is the Entry, Not the Format

When part 01 set down the roadmap at the volume's opening, the question hanging under the side story's name was this one: can a dynamic library be taken as an executable and run directly? Today it can go down on paper. In format there is in fact no dividing line: library and program are both ELF, and the DYN column takes both spellings — a pie program writes DYN, and today's libdual.so is DYN too. What split is only the entry and the use: the one whose entry points at _start and waits to be exec'd is a program; the one whose Entry sits empty at 0x0 and waits to be mapped is a library — and this boundary is a writable one besides: pass the linker an entry switch, and the library gains an entry of its own. What we judge runnability by was never the suffix; we look at whether somebody has taken care of the entry's business: glibc did, so libc.so.6 runs respectably. We did it ourselves too, so libdual.so runs. libmyutil.so had nobody to do it — the Entry in its header stops at 0x0, and running it anyway ends in dying on the file header. The fifteen pieces of the main body told of mechanisms; today's piece walked out to where the mechanisms end. The boundary we have now seen with our own eyes, and the mechanisms came out the clearer for it. With this, the volume closes — see you in the other volumes.
