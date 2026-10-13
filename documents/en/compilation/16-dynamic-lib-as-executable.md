---
chapter: 13
difficulty: intermediate
order: 16
platform: host
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 链接器
- 工具链
title: "Side Story: Can a Dynamic Library Run as an Executable?"
description: "Running a default .so directly ends in a segmentation fault: strace shows execve succeeding with SIGSEGV right behind it, and gdb proves RIP lands on the read-only page holding the file header — Entry 0x0 plus the base address is exactly the landing spot; the counterexample libc.so.6 runs directly and prints a ten-line banner, its entry __libc_main being one write plus _exit, and it carries an INTERP and walks the standard two stages; then -Wl,-e to name an entry and bare system calls to print the banner build libdual.so, one binary serving three uses — run directly, linked against, and dlopen'd — with the stack-protector canary crash at the bare entry run into and solved along the way; ld.so itself is a DYN with an entry; CMake's CXX_VISIBILITY_PRESET and target_link_options land the build in a project, and POST_BUILD runs the artifact directly as a smoke test."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/16-dynamic-lib-as-executable.md
  source_hash: 572c8cb8203dbfab8e5716a515fde18d163da027d089d3d6d56d985be5bc70a1
  translated_at: '2026-10-13T04:37:32+00:00'
  engine: anthropic
  token_count: 12500
---

# Side Story: Can a Dynamic Library Run as an Executable

When we closed out the main body in the last piece, we left one sentence behind: the Entry registered in libmyutil.so's header reads 0x0 — can it actually be run directly, and what happens if you force it? This whole side story plays exactly that game. Today we come to hand in our papers. We put the answer up front, and it has three layers: a .so built by default cannot run — it crashes at the entry. libc.so.6 can run, because its entry is a real one. And we can roll a runnable one ourselves — pointing the linker at an entry is all it takes. The evidence for all three layers was genuinely run on this machine, and we take them one layer at a time.

The environment disclosure as usual: my experiments all ran inside WSL2, the toolchain was GCC 16.2.1 with binutils 2.47, glibc stood at 2.44, CMake at 4.4.3, strace at 7.2 and gdb at 17.2 — the six versions part 14 reported, and not one of them has changed on our machine today. Every stretch of output in this piece came out of a real run.

## Crashing into It Ourselves: Three Files, Three Fates

The specimen is still the volume's familiar old face: the commands part 08 used to build a dynamic library, run once more exactly as they stood, and b.c still holds the two old definitions from part 05:

```text
$ gcc -shared -fPIC -o libmyutil.so b.c
$ ls -l libmyutil.so
-rwxr-xr-x 1 charliechen charliechen 15176 Oct  2 16:40 libmyutil.so
```

One spot needs an honest confession. By most materials' account, a .so leaves the factory without the execute bit, and you have to run a chmod before you can run it. My machine happens not to work that way: the local ld drops its artifact straight at 755, execute bit included from birth. Since we want to see the permission gate with our own eyes, we have no choice but to strip it off ourselves:

```text
$ chmod -x libmyutil.so
$ bash -c './libmyutil.so; echo $?'
bash: line 1: ./libmyutil.so: Permission denied
126
```

This time execve truly reached the kernel's hands: the kernel saw no execute bit and bounced it back, the error code returned was EACCES (insufficient permission), bash translated that code into Permission denied, and the exit code handed back was 126 — the file was found but never really executed, and this old conventional code is exactly what the shell gives for that. We hand the execute bit back and run again:

```text
$ chmod +x libmyutil.so
$ bash -c './libmyutil.so; echo $?'
bash: line 1: 206363 Segmentation fault         ./libmyutil.so
139
```

This time the kernel accepted the file and the process came up, and the ending one instant later was a segfault: exit code 139 comes from 128 plus 11, the kernel's old encoding for reporting SIGSEGV, which we once went over in part 13. One ending isn't a full enough picture, so we also fetch a.o from part 05 and dyn_app from part 08. a.o is an object file produced by gcc -c, dyn_app is the old program linked with -lmyutil, and a.o gets the execute bit too, so that permission doesn't steal the show. We line the three files up:

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

The three files got one step further each: the .o never even passed the kernel's check, the .so passed the kernel and started a process but died halfway, and the normal program walked the whole road and handed in 42. Reading the three headers side by side, the difference shows itself:

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

(readelf -h's full output runs twenty lines; I kept the Type and Entry lines from each.) a.o's Type is REL: the kernel's ELF loader accepts only the EXEC and DYN types, REL is not on its list, and execve bounced it back on the spot with the error code ENOEXEC. The Exec format error in the message is what that looks like; the kernel's meaning is blunt: it does not recognize this ELF's type. bash, though, gets one rescue attempt with this code: it tries the file once more as a shell script, sees binary, and only then reports cannot execute binary file. libmyutil.so and dyn_app carry the very same Type. As we went over in part 04, a PIE program also writes DYN — the difference in the parentheses is just a readelf footnote: it recognizes the PIE by the DF_1_PIE flag in the dynamic segment, while the Type itself never split into two kinds. What really splits them is still Entry: the program has an entry, while the .so's entry is 0. So for a DYN with Entry 0x0 — on what grounds did the kernel let it through, and at which step did the process die? Let's catch it in the act.

## Why It Crashes: execve Succeeds, Death Comes After the Handoff

We bring strace out. Part 14 had it accompany us through the entire startup chain; today we only look at the head and the tail:

```text
$ strace -o /tmp/w3.txt ./libmyutil.so
$ cat /tmp/w3.txt
execve("./libmyutil.so", ["./libmyutil.so"], 0x7fffba6301c0 /* 70 vars */) = 0
--- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_ACCERR, si_addr=0x7c7a94ad4000} ---
+++ killed by SIGSEGV +++
```

(We write it into a file and then look: the full trace is just these three lines, and we trimmed not one of them. si_addr and the env pointer on the stack are values from this particular run and will change on a rerun; the death notice will not.) The first line's content is surprising enough: execve returned 0 — it succeeded. That long string of openat and mmap that followed execve in part 14 — we cannot find a single one: at this moment nobody has yet been called on to do library loading, though it doesn't need it right now either, as we'll understand in a moment. Right behind it, the second line is the death notice, and it brings three fields along: si_signo reports SIGSEGV; si_code gives SEGV_ACCERR, meaning the address has a mapping but the permission is wrong — when the address has no mapping at all, what you get is SEGV_MAPERR instead. si_addr records 0x7c7a94ad4000, which is page-aligned and looks like the base of a mapping. Let's go over what the kernel does inside execve: it maps the file into memory according to the program headers' LOAD segments; a DYN gets the same treatment as a PIE, given a randomized base address. The file before us has no INTERP segment, so the second file needn't be loaded either. Then the kernel hands control to the entry registered in the header. The entry is 0x0? The kernel doesn't actually check this — base plus 0, and it jumps all the same. Where did it land? gdb took the photo for us (it turns address randomization off when starting the process, so the numbers are stable):

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex run -ex 'p/x $rip' -ex 'info proc mappings' ./libmyutil.so
Program received signal SIGSEGV, Segmentation fault.
0x00007ffff7ffa000 in ?? ()
$1 = 0x7ffff7ffa000
0x00007ffff7ffa000 0x00007ffff7ffb000 0x1000  0x0     r--p  /tmp/.../lab/libmyutil.so
0x00007ffff7ffb000 0x00007ffff7ffc000 0x1000  0x1000  r-xp  /tmp/.../lab/libmyutil.so
```

(gdb's few startup banner lines, the process line and the maps header, the vvar, vdso and stack segments, and libmyutil.so's last two LOADs — I trimmed them, shortened the long paths, tightened the whitespace between columns; the addresses are as they were.) RIP reads 0x7ffff7ffa000, and maps' first line is exactly it: libmyutil.so's first LOAD, with file offset 0x0 and permission r--p. The truth is complete here: the so-called entry 0x0 plus the base address lands on the file's first page — the page where the ELF header lives, which is read-only, without execute permission. When the CPU fetches instructions there, what the MMU reports is precisely a permission fault, matching the SEGV_ACCERR in strace. So the default .so never got rejected; the kernel executed it exactly as delivered: execve succeeded, control was handed over, and then the process died on the landing spot of that handoff. In part 08 we wrote down half a sentence: an Entry reading 0x0 says outright that there is no entry. Today I've shown you the other half: it dies precisely on the page holding the file header.

## The Counterexample: libc.so.6 Runs Fine

A default .so can't run — is that where the matter settles? There really is one that runs, and it's the very one you use every day. glibc itself; we type it straight in:

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

Ten lines of banner printed, and the process left the stage with dignity, handing back exit code 0. The same DYN identity — how does this one manage it? We dig out its assets and set them against the previous one's (it too passes the execute-bit gate by birth: ls -l shows 755 there as well):

```text
$ readelf -h /usr/lib/libc.so.6 | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x279c0
$ readelf -l /usr/lib/libc.so.6 | grep -A2 INTERP
  INTERP         0x00000000001c4be0 0x00000000001c4be0 0x00000000001c4be0
                 0x000000000000001e 0x000000000000001e  R      0x10
      [Requesting program interpreter: /usr/lib/ld-linux-x86-64.so.2]
```

The assets laid out amount to fully three. The execute bit is there from birth. Entry registers 0x279c0, an unmistakably non-zero value. And the program header table has one extra entry, INTERP, whose registered interpreter is ld-linux. There is a small difference in the path INTERP registers: in part 14's link line, GCC hands over `/lib64/ld-linux-x86-64.so.2` by default, while the local libc.so.6's INTERP reads `/usr/lib/ld-linux-x86-64.so.2` — the path is fixed at build time, but /lib and /lib64 are both symlinks on this machine, and the two land on the same file. The first two are hard requirements for running at all; the third means that when it runs, it takes the standard two-stage loading that piece described: the kernel loads libc.so.6 itself together with its interpreter, the dynamic linker finishes the relocations, and only then is control handed to 0x279c0. Who lives at this entry? We turn the entry out and look:

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

(objdump's few header lines, the alignment NOPs at the end, and the next function _Unwind_Resume that came along inside the stop address — I trimmed them.) The entry is named `__libc_main`, and its body is one straight line: write's three arguments get ready — edi holds descriptor number 1, rsi points at the banner string, edx takes 0x1e3. The call goes to `__write`, glibc's internal alias, called directly by libc's own people without going through the PLT. write finishes its work, edi is cleared to zero, and the curtain falls with a call to `_exit`. 0x1e3 in decimal is 483; we measured it with wc -c, and the byte count of the ten banner lines above matches without a byte to spare. Notice that it doesn't use printf: by the time the entry gets control, `__libc_start_main` hasn't actually run yet, and not one of stdio's buffers has been set up — so glibc doesn't gamble on it either, using the most primitive write plus an immediate exit. That prudence is exactly what we can put to use next.

## Rolling Our Own: One File, Three Uses

Having seen how libc.so.6 goes about it, we roll our own. We set the goal out neatly: the three uses one .so must serve are run directly, linked against by others, and dlopen'd; what we hand over is the same binary throughout, without changing a single byte. Here is the seventeen-line source in full:

```cpp
// dual.cpp — one file, meant to serve three uses: run directly, linked, dlopen'd
// The outward interface: an extern "C" bare name + default visibility; both the
// linker and the user who fetches symbols by name accept it
extern "C" __attribute__((visibility("default"))) int add(int a, int b) {
    return a + b;
}

// Process entry: when run directly, control lands here. Marked hidden, kept
// out of the export table
extern "C" __attribute__((visibility("hidden"))) void dual_start() {
    const char msg[] = "dual: 作为可执行文件跑起来了\n";
    // write(1, msg, len): syscall number 1. No stdio: the entry's stack is
    // bare, __libc_start_main never ran, so we make just two system calls
    // and gamble on nothing else
    asm volatile("syscall" : : "a"(1L), "D"(1L), "S"(msg), "d"(sizeof(msg) - 1) : "rcx", "r11", "memory");
    // exit_group(0): syscall number 231. Not 60 (exit): 60 reaps only the
    // current thread; 231 reaps the whole process, and a proper exit takes it
    asm volatile("syscall" : : "a"(231L), "D"(0L) : "rcx", "r11", "memory");
    __builtin_unreachable();  // exit_group never returns
}
```

A few words on each of the two. add is the outward interface: `extern "C"` keeps the name bare — forget that, as in part 13, and dlsym goes looking for calc_plugin_entry's name while the table holds _Z17calc_plugin_entryv, and comes up empty. The default visibility is the opening left for it, paired with the build-time global narrowing — exactly the pair part 11 established. dual_start is the entry prepared for running directly, marked hidden so it never leaves the export table. Its body holds just two system calls: write sends the banner to descriptor 1, then exit_group closes the show. In part 14's trace, it was the finale; a single-threaded process can also exit with number 60, but 60 reaps only the current thread — the one that reaps the whole process is 231, so we do it the proper way. Why so scrupulous? Our .so doesn't plan to carry an INTERP: the kernel hands over control right after loading the LOAD segments, nobody does the relocations, nobody fills the GOT, and not one byte of the runtime has been initialized — the entry's code must be self-sufficient: the syscall instruction itself depends on no relocation, and the banner's bytes are carried onto the stack by the compiler with movabs 64-bit immediates, their address taken via rbp-relative addressing, likewise saving every relocation; on the two calls write plus exit_group alone we can stay alive. The build command:

```text
$ g++ -fno-stack-protector -shared -fPIC -o libdual.so dual.cpp -Wl,-e,dual_start
$ readelf -h libdual.so | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x10fd
```

`-Wl,-e,dual_start` means handing the linker one `-e` (entry) switch: that is what registers the entry. The `-Wl` style of passing arguments with commas is the same one part 08's -soname and part 10's -rpath used. Entry went from 0x0 to 0x10fd, dual_start's address. As for where -fno-stack-protector came from — it genuinely blew up on me: the first version of the command left it out, and running directly still produced 139. We had gdb take a look:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex run -ex 'x/2i $pc' ./libdual_sp.so
Program received signal SIGSEGV, Segmentation fault.
0x00007ffff7ffb101 in dual_start ()
=> 0x7ffff7ffb101 <dual_start+4>:	mov    %fs:0x28,%rax
   0x7ffff7ffb10a <dual_start+13>:	mov    %rax,-0x8(%rbp)
```

(gdb's few startup banner lines and the blank line — I trimmed them.) The death point fell at dual_start+4, and our code contains no such mov. It is reading fs:0x28, which is the stack protector's prologue: the canary (a canary — a guard value against stack-smashing rewrites) that the compiler inserts into functions by default lives right here, and reading it requires going in through the FS segment base into thread-local storage. At the process entry, TLS hasn't been established yet and FS reads 0, so reading address 0x28 dies on the spot. Our source never touched a single line of the standard library, yet the default hardening option touched it for us. With -fno-stack-protector added, the prologue is gone, and the program finally runs through. Time to accept delivery — we run the three uses one by one:

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

(host.cpp's job is to link against it and call add; dlhost.cpp does dlopen plus dlsym in the three steps of part 13; both are little programs a few lines long, and I tucked them into the lab package rather than spending the article's lines on them.) The first use goes its whole way without the dynamic linker involved at all: the kernel loads the segments, hands control to dual_start, and it exits once the two system calls finish. In the other two uses, add's treatment is unchanged: resolved and called as usual, one taking -l's old registration road, the other runtime loading's old road. We also verify the export table with one look:

```text
$ nm -D libdual.so
                 w __cxa_finalize@GLIBC_2.2.5
                 w __gmon_start__
00000000000010e9 T add
```

(The weak hooks marked w are the linker's attendants; I trimmed two of the four, and the rest couldn't leave home anyway.) What remains at the door is the clean bare name add — the bare-name practice from part 13 held from birth this time. One file occupying all three ways of living; here the set is complete.

## The Dynamic Linker Is Itself a Program

Last, a party with a special identity. In part 14, ld-linux's post was interpreter, working from inside someone else's program header; today we invite it itself to the other side of execve:

```text
$ /lib64/ld-linux-x86-64.so.2; echo $?
/lib64/ld-linux-x86-64.so.2: missing program name
Try '/lib64/ld-linux-x86-64.so.2 --help' for more information.
1
$ readelf -h /lib64/ld-linux-x86-64.so.2 | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x217e0
```

It hasn't actually crashed; what it complains about is that we gave too few arguments: the usage it expects is itself first, followed by an executable plus arguments — using it to launch a program explicitly. The start of --help is its own self-description: the heading reads exactly program interpreter, speaking of its identity as the interpreter for dynamically linked programs; normally it is invoked automatically, and today we invoked it by hand once. Type reads DYN, Entry is non-zero too, and the execute bit is there as well: the one that does the loading for everyone else is itself a DYN with an entry. The two ways an ELF can live — today we've seen both ends with our own eyes.

## The CMake View

We hand this build to CMake; the template is short enough to take in at a glance:

```cmake
cmake_minimum_required(VERSION 3.16)
project(dual_run CXX)
add_library(dual SHARED dual.cpp)
# Visibility: narrow globally, open the interface at a single point (add in
# dual.cpp carries the default attribute)
set_target_properties(dual PROPERTIES CXX_VISIBILITY_PRESET hidden)
# The entry's stack is bare; switch off the stack-protector canary prologue
target_compile_options(dual PRIVATE -fno-stack-protector)
# -Wl,-e,dual_start: point the process entry at our own dual_start
target_link_options(dual PRIVATE "-Wl,-e,dual_start")
# Execute-bit fallback (the local ld grants it by default); run the artifact
# directly as a smoke test while we're at it
add_custom_command(TARGET dual POST_BUILD
    COMMAND /usr/bin/chmod +x $<TARGET_FILE:dual>
    COMMAND $<TARGET_FILE:dual>)
```

CXX_VISIBILITY_PRESET is the C++ twin of the C_VISIBILITY_PRESET that part 11's CMake section covered; one backward point and we move on. The two switches for the entry and stack protection are the ones we just got burned by; once inside a project, we simply configure them as shown. We build once with --verbose, and both the link line and the smoke test show themselves:

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

(CMake and make's scaffolding lines — the directory enter/leave notices, the dependency checks, the progress bar — together with the final Built target line, I trimmed; long paths and a few mid-sections were shortened.) `-fvisibility=hidden` rides on every compile line, and add's default attribute gets its single-point pass — that is what part 11's pair looks like inside a project. `-Wl,-e,dual_start` landed on the link line — exactly the sight we wanted. About the chmod line in POST_BUILD, I confess plainly: the local ld's artifact already carries the execute bit, so on our machine it spins idle; it stays as the fallback for build environments that don't grant the execute bit by default. The command after it runs the artifact directly, the banner lands in the build log, and the smoke test counts as passed: the acceptance check of "can it run as a program" — the build system handles it for us of its own accord.

## End of the Volume: The Boundary Is at the Entry, Not the Format

When the opening piece laid down the roadmap, this question was exactly what hung under the side-story name: can a dynamic library be run directly as an executable? Today it can go on paper. In format terms there is actually no dividing line: library and program are both ELF, both fit in the DYN column, PIE programs also write DYN, and today's libdual.so is DYN too. What splits them is only entry and usage: the one whose entry points at `_start` and which waits to be exec'd is a program; the one with Entry left at 0x0 and which waits to be mapped is a library — and this boundary is even writable: hand the linker an entry switch, and the library has an entry of its own. Our criterion for runnability was never the suffix; what we look at is whether anyone has taken care of the entry's business: glibc took care of it, so libc.so.6 runs with dignity. We took care of it ourselves, so libdual.so runs. Nobody took care of it for libmyutil.so — its header's Entry sits at 0x0, and forcing a run ends in dying on the file header. The fifteen main pieces taught the mechanism; this piece walked to the mechanism's edge — having seen the boundary with our own eyes, the mechanism reads all the clearer. The volume closes here; we'll see you in the other volumes.
