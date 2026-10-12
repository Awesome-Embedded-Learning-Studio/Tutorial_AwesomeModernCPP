---
chapter: 13
difficulty: advanced
order: 14
platform: host
reading_time_minutes: 31
tags:
- cpp-modern
- host
- advanced
title: "Program Startup: From exec to main"
description: "The whole stretch between typing ./app and the first line of main, followed on foot: strace chasing the system calls after execve line by line; gdb's starti proving that the first instruction lands in ld.so and that maps holds only the exe plus the interpreter, two files in all, standing the two-stage loading on its feet; readelf -l formally reading INTERP and LOAD out of the program header table; three gdb breakpoints walking the order of _start, __libc_start_main, and main, the thirteen instructions of _start read with an eye for the ones that matter, main's address carried into rdi by a lea, and the call through a GOT slot being GLOB_DAT resolved at load time; the new shape on glibc 2.44 with init and fini's two arguments zeroed and __libc_csu_init deleted; the collect2 link line photographed live for the division of labor across the row of Scrt1, crti, crtbeginS, crtendS, and crtn, with _init stitched from crti's head and crtn's tail and the __gmon_start__ weak hook's landing spot; the two entries of .init_array, frame_dummy and _GLOBAL__sub_I_, read for real; the -static comparison with 19 lines of trace missing the whole second stage; closing with half a sentence on exit and CMake's pie against -no-pie (Type DYN against EXEC)."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/14-program-startup.md
  source_hash: 6f44e9877644eb5f493f9b60c5cc3885eb64a1679a6d267c75db888d7cf14b33
  translated_at: '2026-10-01T08:47:06+00:00'
  engine: glm
  token_count: 5134
---

# Program Startup: From exec to main

At the end of the last piece we previewed the whole road once: from the one command we type to the first line of main executing, what lies between is a long stretch of loading, relocation, and runtime library initialization. Today's piece is here to pay that off. No replay of the preview — we wheel the tools into place and walk the road for real, one step at a time. To watch what happens before main, our eyes have to press right down to the layer of system calls, the doorway through which a process asks the kernel to do things. So we bring out strace: a tool whose one and only business is tracing system calls. Which process trapped into the kernel, what it called down — strace writes it all down exactly as it happened.

Our specimen is still the volume's familiar old face libcalc: a dynamic library exporting an int variable plus two functions, paired with an app that calls it, and when we run the app we carry LD_LIBRARY_PATH=. along so it finds the library it uses in the current directory. The environment disclosure as usual: my experiments all ran in WSL2, on the combination of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, with strace at 7.2 and gdb at 17.2, and every stretch of output in this piece was really run. If the versions on your side differ, details like the names of system calls and the addresses may come out differently; the main line of steps has not changed in years.

## execve: The First Line, the Process Becomes Somebody Else

We put app on strace and ran it once; the entire output comes to 51 lines in all, and we read from the top. The first line reads:

```text
execve("./app", ["./app"], 0x7ffe681792c0 /* 70 vars */) = 0
```

The execve on the first line is a system call; its three parameters are the program's path, the list of command-line arguments, and the list of environment variables. The work it does is swapping the current process out wholesale for this program on the disk: the code, data, and stack of the process that was there before are all invalidated. At the moment it returns 0, the child the shell handed over is already gone; the one alive is app. The preview at the end of the last piece named it; today we are looking at the real body.

In the 50 lines that remain, one thing jumps out: turn the whole output inside out and you will not find a single call opening ./app or ld-linux. libcalc and libc each get whole stretches of openat and mmap, yet the exe and the dynamic linker — the principals themselves — never once got an openat. And this is truly not strace missing a record: the kernel maps the first two files inside execve itself, work that is finished inside the kernel, so of course the system-call log has no place for them. If you ask what makes us so sure the kernel loaded exactly those first two — we accept no word-of-mouth claims here; the next section produces the evidence.

## Two Stages: The Kernel Loads Only Two, the Rest Belongs to the Dynamic Linker

We ask gdb for a hand: its starti command stops the process on the first instruction in the truest sense — earlier than main, and earlier too than _start, the entry that Entry pointed at when we read the header in part 04.

```text
$ gdb -q -batch -ex starti ./app
Program stopped.
0x00007ffff7fdf7e0 in ?? () from /lib64/ld-linux-x86-64.so.2
```

(The few lines of startup chatter from gdb itself I trimmed.) Where did it stop? Not in our app — it landed in /lib64/ld-linux-x86-64.so.2. No misreading: that is the dynamic linker. Part 01's file output called it, at the tail, the interpreter, and in part 08 we unmasked its real body. While we are here, a glance at the process's maps:

```text
0x0000555555554000 0x0000555555555000 ... r--p  /tmp/.../lab/app
0x0000555555555000 0x0000555555556000 ... r-xp  /tmp/.../lab/app
0x00007ffff7fbe000 0x00007ffff7fbf000 ... r--p  /usr/lib/ld-linux-x86-64.so.2
0x00007ffff7fbf000 0x00007ffff7fec000 ... r-xp  /usr/lib/ld-linux-x86-64.so.2
```

(The Size and Offset columns in the middle of each line I trimmed, and the long paths I shortened; the addresses are untouched. For each of the two files I kept the first two segments; the latter two of the four segments I trimmed too. The paths you may have caught: what gdb reported sits under /lib64 while maps records /usr/lib — the two spellings point at the same file, the former traveling by way of a symlink, the latter the resolved real body.) In the process at this moment there are exactly two real bodies: app's four segments plus the dynamic linker's four segments. The remaining small stretches, vvar and vdso, are the kernel's own things, and part 08 already named vdso. We searched the lot: libcalc's name is not there, and neither is libc's; their entrance into the process has to wait until the dynamic linker moves its hands. With that, the two stages of loading stand on their feet: the kernel's stage recognizes only two files — the executable's own body on one side, the interpreter registered in its header on the other. Not one of the other libraries on the NEEDED list is any of the kernel's business. The second stage is the dynamic linker running in user mode; from line 2 of the trace onward, every job is its own, and what we paste below is its stretch for loading libcalc:

```text
openat(AT_FDCWD, "./libcalc.so", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0755, st_size=15360, ...}) = 0
mmap(NULL, 16408, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7e9e759d1000
mmap(0x7e9e759d2000, 4096, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x1000) = 0x7e9e759d2000
mmap(0x7e9e759d3000, 4096, PROT_READ, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7e9e759d3000
mmap(0x7e9e759d4000, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7e9e759d4000
close(3)                                = 0
```

(While hunting for the library it also had a few extra tries at CPU-capability subdirectories of the glibc-hwcaps kind; all came up empty, and those two lines I trimmed. The read and getcwd lines between openat and fstat I trimmed as well. The complete rules of the search order belong to part 10; today we only look at the phenomenon.) The four mmap calls at the back sit at addresses one right after another — they are exactly the run-time shape of the four LOADs in readelf -l. The program header table makes its formal entrance today: the section table we read with readelf -S in part 04 is the slicing drawn up for the linker's eyes, while the program header table hands us the loader's view — one LOAD corresponds to one mapping. We read app's out:

```text
$ readelf -l app
Program Headers:
  Type           Offset             VirtAddr           PhysAddr
                 FileSiz            MemSiz              Flags  Align
  INTERP         0x00000000000003ac 0x00000000000003ac 0x00000000000003ac
      [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
  LOAD           0x0000000000000000 0x0000000000000000 0x0000000000000000
  ...
  DYNAMIC        0x0000000000002dd0 0x0000000000003dd0 0x0000000000003dd0
  GNU_RELRO      0x0000000000002dc0 0x0000000000003dc0 0x0000000000003dc0
```

(The full table runs to a dozen-plus entries; PHDR, the four LOADs, and several NOTEs are all in my hands — the trimmed parts have nothing to do with the main line, and the ellipsis is my own.) What INTERP registers is the dynamic linker's path: it is from here that the kernel learns the second file it must load. DYNAMIC is the loading view of the .dynamic section, which we walked line by line with readelf -d in part 08, and the NEEDED list lives inside it. On GNU_RELRO, half a sentence more: the RELRO in the name reads relocation read-only, meaning that once relocation is done, the pages are turned back to read-only. The few mprotect calls toward the back of the trace are doing exactly that job, turning the pages that had the GOT written in them back to read-only. In part 09 we watched the dynamic linker fill in the GOT's tables; once these mprotects have run, the values in the table are all settled, and the written pages are read-only again.

After libcalc is loaded comes libc, the flow is the same one over again, and we don't paste it a second time. With everything loaded, the trace still holds a stretch of arch_prctl, set_tid_address, and getrandom calls: that is the dynamic linker doing its initialization — the base address of thread-local storage is prepared right there. Only after all of that has run do we reach the output we know, start and base=100 total=117, sent out whole in a single write. Look at that: the first line we wrote takes its place at the back of a long string of preparation.

## _start: The First Instruction That Belongs to the Program

Libraries loaded, tables filled in — only then comes the turn of our program's entry. When we read readelf -h in part 04, the Entry line pointed at a location named _start, and we said at the time that everything before main is its business. Today we cross-check with nm: Entry's 0x1070 is exactly _start's address.

We bring gdb on stage, hang three breakpoints, and run the order for you:

```text
$ gdb -q -batch -x three_bp.gdb ./app        # script: b _start / b __libc_start_main / b main / run / bt / c ...
Breakpoint 1 at 0x1070
Function "__libc_start_main" not defined.
Breakpoint 2 (__libc_start_main) pending.
Breakpoint 3 at 0x1172
...
Breakpoint 1, 0x0000555555555070 in _start ()
Breakpoint 2, 0x00007ffff7c2783c in __libc_start_main () from /usr/lib/libc.so.6
#1  0x0000555555555095 in _start ()
Breakpoint 3, 0x0000555555555172 in main ()
start
base=100 total=117
[Inferior 1 (process 30196) exited normally]
```

(At the ellipsis: gdb's leading notices and the thread library's notice lines; the #0 frame of each stop's bt I trimmed as well, keeping only the second stop's #1 — that is the one that summons the caller out.) The order of the three stops is plain as day: _start in the lead, __libc_start_main next, and main holding the finale. In the backtrace taken at the second stop the caller shows its face too: it was _start that called it. Two small details still owed to you: __libc_start_main's home is in libc, yet at the moment gdb starts up, there is not a shadow of libc in the process, so this breakpoint has to be hung with set breakpoint pending on and stay pending, taking root only once libc has entered the process. And b main lands on main+9, skipping the function prologue — one of gdb's old habits.

What does _start itself look like? The whole of it is pasted for you, gdb's leading notices trimmed; we pick out what matters:

```text
Dump of assembler code for function _start:
=> 0x0000555555555070 <+0>:	f3 0f 1e fa        	endbr64
   0x0000555555555074 <+4>:	31 ed              	xor    %ebp,%ebp
   0x0000555555555076 <+6>:	49 89 d1           	mov    %rdx,%r9
   0x0000555555555079 <+9>:	5e                 	pop    %rsi
   0x000055555555507a <+10>:	48 89 e2           	mov    %rsp,%rdx
   0x000055555555507d <+13>:	48 83 e4 f0        	and    $0xfffffffffffffff0,%rsp
   0x0000555555555081 <+17>:	50                 	push   %rax
   0x0000555555555082 <+18>:	54                 	push   %rsp
   0x0000555555555083 <+19>:	45 31 c0           	xor    %r8d,%r8d
   0x0000555555555086 <+22>:	31 c9              	xor    %ecx,%ecx
   0x0000555555555088 <+24>:	48 8d 3d da 00 00 00	lea    0xda(%rip),%rdi        # 0x555555555169 <main>
   0x000055555555508f <+31>:	ff 15 2b 2f 00 00  	call   *0x2f2b(%rip)        # 0x555555557fc0
   0x0000555555555095 <+37>:	f4                 	hlt
End of assembler dump.
```

The two instructions at the head are routine business: endbr64 belongs to control-flow protection, and the xor beside it zeroes the frame pointer's base register, marking the outermost frame — a backtrace walking this far knows it is time to stop. The instructions in the middle are the real content: under the x86-64 calling convention the first six arguments travel in the six registers rdi through r9, and who is _start preparing arguments for? The call at the end has already confessed: the answer is __libc_start_main. We check them against the registers one by one:

- rdi holds main's address, filled in by that lea instruction. See it plainly: here, main is nothing more than a function pointer being handed from one party to another.
- rsi and rdx hold argc and argv: pop %rsi pops argc, mov %rsp,%rdx takes argv. The kernel laid the command line out at the top of the stack by convention, leaving it there for whoever enters the process to pick up.
- r9 holds the sixth argument: mov %rdx,%r9 moves over the value rdx carried at entry — a wrap-up function entrusted by the dynamic linker, one that later gets hung on the exit hook; we will run into it again when exit comes up.

The zeroing of rcx and r8 deserves a couple more sentences. If you have read the older textbooks, the face you most likely saw is a different one: rcx holding __libc_csu_init, r8 holding __libc_csu_fini, with the initialization and the wrap-up all handled between the two of them. On this glibc 2.44 of ours it is no longer so: from 2.34 on, glibc deleted them — the walk through that string of preparatory work before main moved into libc itself — so the files the linker tows along now simply hand in zeros and call it done. The tutorials have aged, and the numbers no longer match up, which is one more reason this piece speaks with real measurements the whole way.

Don't let the closing call *0x2f2b(%rip) slip past either. The way it calls __libc_start_main is the GOT's indirection: the target slot sits at 0x555555557fc0. A readelf -r lookup shows this slot's origin is R_X86_64_GLOB_DAT — the kind resolved at load time. In part 09's LD_DEBUG timeline we saw it with our own eyes: __libc_start_main's binding ranks ahead of the printing of start, so lazy binding never gets its turn. The reasoning is plain enough: the target's real body lives in libc, nobody at link time knows where it will land, and going through the table is the only way to write it.

## __libc_start_main: After Taking main's Address

__libc_start_main has taken main's address — what does it do from there? We wanted to dig one layer deeper and break on a function named __libc_start_call_main, but the break never took: this machine's libc has no such name in its symbol table. Of the few names we went looking for, the only one nm -D was willing to hand over is __libc_start_main; the local symbols have been stripped. We stepped back and switched to breaking by address. Pulling up __libc_start_main's disassembly, the target of the call inside its body lands at 0x7ffff7c27700, and that is what we break on:

```text
Breakpoint 2, 0x00007ffff7c27700 in ?? () from /usr/lib/libc.so.6
#0  0x00007ffff7c27700 in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff7c278b9 in __libc_start_main () from /usr/lib/libc.so.6
#2  0x0000555555555095 in _start ()
rdi            0x555555555169      93824992235881
rsi            0x1                 1
rdx            0x7fffffffd438      140737488344120
```

(The repeated output of the stops passed on the way here, along with the disassembly at the stop and the program output after it, I trimmed in one lot. The name displays as ?? for the reason given above — the symbols are stripped. The function at 0x7ffff7c27700 is called __libc_start_call_main in glibc's source code, split out of __libc_start_main since 2.34; the name we cite is the source-code one.) Lying in the registers is exactly the old trio: rdi's value is 0x555555555169, matching the lea target in _start to the last digit — what it holds is main's address. rsi is argc's 1, and rdx is argv's address. One more single-step from here and we are inside main.

As for __libc_start_main's own work, what I saw in its disassembly is this: at its head it registers that wrap-up function from r9 through __cxa_atexit. The dynamic linker's initialization chores we already met once at the tail of the two-stage trace — the arch_prctl, set_tid_address, and getrandom calls were speaking of exactly that. It then walks the constructor table in .init_array, calls main up, and hands the return value to exit — as for who lives in that constructor table, the section below takes them up one by one. Not one of these steps is our code, and every one of them stands in front of main.

## The Row of Startup Files: Who Put _start There

In part 05 we left word behind: what _start does ahead of main, and what each member of the row of startup files manages — today we pay that back. And let's ask the first question the other way round: _start is not code we wrote, so where does it come from?

The answer hides in the link command line. We hung -v on gcc and linked once more, catching collect2's line whole. collect2, introduced back in part 01, is the wrapper around the linker ld:

```text
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/collect2 ... -dynamic-linker /lib64/ld-linux-x86-64.so.2 -pie -o app_v /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/Scrt1.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/crti.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/crtbeginS.o -L. ... /tmp/ccXT99dK.o -lcalc -lgcc -lgcc_s_asneeded -latomic_asneeded -lc -lgcc -lgcc_s_asneeded /usr/lib/gcc/x86_64-pc-linux-gnu/16/crtendS.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/crtn.o
```

(The ellipsis is mine; what I cut is the -plugin group, the --build-id group, and a string of -L directories — the order is preserved exactly.) Read the arrangement and the story tells itself: Scrt1.o leads; crti.o and crtbeginS.o follow; only in the middle do our object files and the row of libraries around -lcalc appear; and the closing switch brings in crtendS.o and crtn.o. Of the five files lined up in this string, collect2 consulted us on not a single one — it quietly towed them along. We gave them their name in part 01: the C runtime's startup files. As a bonus you caught the -dynamic-linker argument too: the path it writes is ld-linux, landing exactly in the program header table's INTERP.

Scrt1.o deserves a close look. Its symbol table — everything nm put on display is here:

```text
$ nm Scrt1.o | grep -v ' [a-z] '
                 U _GLOBAL_OFFSET_TABLE_
0000000000000000 R _IO_stdin_used
0000000000000000 D __data_start
                 U __libc_start_main
0000000000000000 T _start
0000000000000 W data_start
                 U main
```

We recognize two old acquaintances on the spot. One is the U of main; the other is the U of __libc_start_main. In part 05, when our link of no_main failed, the undefined reference to `main` in the error — the file issuing that reference is this very one in front of us. Its .text is exactly the thirteen instructions we read above; what differs all lands in the relocations:

```text
$ readelf -rW Scrt1.o | head -4
Relocation section '.rela.text' at offset 0x2c8 contains 2 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
000000000000001b  000000040000002a R_X86_64_REX_GOTPCRELX 0000000000000000 main - 4
0000000000000021  0000000800000029 R_X86_64_GOTPCRELX     0000000000000000 __libc_start_main - 4
```

In the .o version, the address-taking for main and __libc_start_main is written the go-through-the-GOT way. Once the file is linked into the PIE — the position-independent executable whose identity part 09 settled — the linker relaxes the GOT detour for fetching main into a direct lea; the lea we saw in gdb is its handiwork. __libc_start_main, living in libc, cannot be relaxed, and it keeps the through-the-table spelling. The GOTPCRELX suffix hanging on the relocation names is exactly the opening reserved for that relaxation; we don't unfold it here.

For the remaining four of the row, one sentence each:

- crti.o houses the beginnings of the two functions _init and _fini. The hook proper of __gmon_start__ — the weak symbol we met in part 05 — lives in crti.o's _init. The shape it takes once linking is done, we look at for real:

```text
0000000000001000 <_init>:
    1000:	endbr64
    1004:	sub    rsp,0x8
    1008:	mov    rax,QWORD PTR [rip+0x2fc9]        # 3fd8 <__gmon_start__>
    100f:	test   rax,rax
    1012:	je     1016 <_init+0x16>
    1014:	call   rax
    1016:	add    rsp,0x8
    101a:	ret
```

Savor the structure: the endbr64 and sub at the head come from crti.o; the add and ret at the tail come from crtn.o — the body of _init is stitched together by the linker from the two ends' fragments. The four instructions sandwiched in the middle belong to neither end: they are the __gmon_start__ hook. When a definition exists, it is called; when the value fetched turns out to be zero, the jump goes straight past. In our program it is a w-marked undefined weak symbol, so these instructions idle here; once a profiling tool is linked in, they come into their own. Part 05 told of the intent behind the opening weak symbols leave; what we see here is where that opening lands in the startup flow.

- crtbeginS.o contributes the first entry of .init_array, frame_dummy, and the first entry of .fini_array, __do_global_dtors_aux, plus two small functions on the side, deregister_tm_clones and register_tm_clones. TM is short for transactional memory, and these two are likewise hooks hung on weak references — when nobody defines them, they are skipped just the same.
- crtendS.o puts forward two boundary symbols, __FRAME_END__ and __TMC_END__, serving as the end markers when the crtbeginS side counts off its tables.
- crtn.o shows not one symbol in nm's output. It is the tail of .init and .fini — the last two instructions of _init above are the whole of its content.

One more thing to notice, the S suffix in the file names: the S of Scrt1 and the S of crtbeginS mark the PIE versions. On a non-PIE link, the ones taking the stage are crt1.o and crtbegin.o, doing the same job.

## .init_array: The Constructor Table Before main

The startup files have seen _start into the process; the table libc walks next, we follow along and watch. The place where the constructors of C++ global objects are registered is .init_array. We write a global object with a constructor:

```cpp
// guard.cpp — a global object with a constructor
#include <cstdio>

struct Guard {
    Guard() { std::printf("Guard ctor: main 还没进门\n"); }
    ~Guard() { std::printf("Guard dtor: main 已经退场\n"); }
};

Guard g_guard;

int main() {
    std::printf("main: 这才轮到我\n");
    return 0;
}
```

```text
$ g++ guard.cpp -o guard && ./guard
Guard ctor: main 还没进门
main: 这才轮到我
Guard dtor: main 已经退场
```

The constructor ran ahead of main; the destructor ran after it. And where does the table live? readelf gives us the straight answer:

```text
$ readelf -SW guard | grep -E 'init_array|fini_array'
  [21] .init_array       INIT_ARRAY      0000000000003d98 002d98 000010 08  WA  0   0  8
  [22] .fini_array       FINI_ARRAY      0000000000003da8 002da8 000008 08  WA  0   0  8
$ objdump -s -j .init_array guard
Contents of section .init_array:
 3d98 40110000 00000000 99110000 00000000  @...............
$ nm guard | grep -E 'frame_dummy|_GLOBAL__sub_I'
0000000000001199 t _GLOBAL__sub_I_g_guard
0000000000003d98 d __frame_dummy_init_array_entry
0000000000001140 t frame_dummy
```

Sixteen bytes of .init_array, counted at eight bytes an entry, come to exactly two. The frame_dummy at 0x1140 is crtbeginS.o's contribution. The extra line nm pasted, __frame_dummy_init_array_entry, lives at exactly the position of the table's first entry, placed there at link time — and with that, frame_dummy's provenance is matched up. The _GLOBAL__sub_I_g_guard at 0x1199 is the constructor wrapper the compiler generated for g_guard; a name grown into this shape appears nowhere in our source — the compiler made it. At startup, libc calls the table's entries one after another, and only when the calling is done does it enter main. Functions marked __attribute__((constructor)) get tucked into this same table too.

The order of the two entries has its own logic: frame_dummy sits in front, our entry follows behind — consistent with the link line's arrangement, where crtbeginS.o ranks ahead of our object files. The construction order across translation units is something the standard leaves unregulated; the risk of an unguaranteed construction order and the countermeasure of the function-local static got a section to themselves in part 12. Our business here is only the carrier: the timing of when C++ global objects are constructed is, in terms of the machinery, the timing of when this table gets walked.

## The Static Comparison: One Layer Less, One Whole Stage Less

Just how much work the dynamic-loading layer adds, we make no armchair estimate: we link the same specimen fully statically and put the two side by side. libcalc we knock into a .a along the way:

```text
$ gcc -c libcalc.c -o libcalc.o && ar rcs libcalc.a libcalc.o
$ gcc main.c -I. -L. -lcalc -static -o app_static
$ file app_static
app_static: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=5e7babfe8014dfd92d593711a602627c28b22612, for GNU/Linux 4.4.0, not stripped
$ readelf -h app_static | grep -E "Type|Entry"
  Type:                              EXEC (Executable file)
  Entry point address:               0x402ea0
$ readelf -l app_static | grep -E "INTERP|Requesting"
$ echo $?
1
```

(The grep at the end came back empty-handed and its exit code went to 1: PT_INTERP, the type name INTERP carries in the program header table, no longer exists in this file — I made a point of letting the exit code say it.) Type has turned EXEC: the half sentence part 04 still owed — a fixed-address executable would be EXEC — is paid off here, and Entry's 0x402ea0 is likewise an address in the fixed-base segment. Even more satisfying is the strace comparison:

```text
$ strace -o static.strace ./app_static && wc -l static.strace e1.strace
19 static.strace
51 e1.strace
70 total
$ sed -n '1,4p' static.strace
execve("./app_static", ["./app_static"], 0x7fff89cad5c0 /* 69 vars */) = 0
brk(NULL)                               = 0x219f4000
brk(0x219f4d40)                         = 0x219f4d40
arch_prctl(ARCH_SET_FS, 0x219f43c0)     = 0
```

Once execve has landed, what comes next straight away is libc's own initialization: the brk and arch_prctl lines — libc's code is already branded whole into the file, nothing left to load. Not a single openat or mmap in between: ld.so.cache is gone without a trace, and so are the libraries and the dynamic linker. Nineteen lines against 51 (what we counted are the lines written into the file; run at a terminal there would be one extra ioctl query): the 32 missing lines are exactly the whole of the second stage. Part 08 showed what shared libraries save memory-wise; this piece fills in the other side, the cost — the dynamic-loading layer is a whole extra stretch of work appended onto the startup road.

## After main: Half a Glance

The whole volume we have stopped at main; today, with the startup seen end to end, stealing half a glance past main is enough. guard's output already looked on our behalf: the destructor landed behind main. As machinery, it is a one-sentence matter: main's return value goes to exit; exit wraps up in reverse order following the registrations of __cxa_atexit — the wrap-up function handed over in r9 is on that register too — then .fini_array is walked, and the exit_group system call at the very end brings the run to a close; it is the closing system call of the trace.

## The CMake Perspective

Under the heading of startup, of the CMake switches that keep getting asked about, this piece looks at one only: pie. We set two targets side by side to compare, and we built the library STATIC, sparing ourselves a second dynamic setup:

```cmake
add_library(calc STATIC libcalc.c)
add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
add_executable(app_fixed main.c)
target_link_libraries(app_fixed PRIVATE calc)
target_link_options(app_fixed PRIVATE -no-pie)
```

(The one line adding calc.h's include directory I trimmed; the complete version is in the experiment pack)

```text
$ cmake -S . -B build && cmake --build build
$ readelf -h build/app | grep -E "Type|Entry"
  Type:                              DYN (Position-Independent Executable file)
  Entry point address:               0x1050
$ readelf -h build/app_fixed | grep -E "Type|Entry"
  Type:                              EXEC (Executable file)
  Entry point address:               0x401050
```

Both targets ran, and the content of their output agreed to the last character. What CMake hands GCC by default is pie, and Type records DYN. Hang -no-pie on and EXEC comes back, and Entry changes with it, from a relative address to 0x401050 — back to the traditional fixed-base segment starting 0x40. One easily misread point deserves one more sentence: app_fixed still carries an INTERP on its body; it links libc dynamically all the same. -no-pie changes how addresses are written and touches nothing about how the linking is done. If it is static you are after, that is -static's business — the two switches each mind their own. As for the run-time business of finding libraries, the build-side entry point is BUILD_RPATH, which part 10's CMake section covered; we won't re-run it.

## Next Up: What More Can the Link Stage Squeeze Out

Walked this far, we count the whole road from execve to main as traveled: the kernel loaded the first two; the dynamic linker pulled up the whole dependency tree; _start handed main over; __libc_start_main walked .init_array to its end — and the first line of code we wrote only began to run at that point. Count on the way back: the system calls spent along this road are a few dozen at most, yet every step among them has a name to it.

The story is told to here, and we leave ourselves something to look forward to. Reading this volume to this point: the compiler's optimization has all along seen no farther than a single translation unit; the linker, by contrast, takes in all the object files at once — yet what it does is only the work of moving positions and joining symbols, and apart from that bit of rewriting called relocation it does no optimization at all. If we moved optimization into the link stage and let it work holding the complete information of every translation unit, what could be squeezed out? Link Time Optimization (LTO) is the very name of that, and it is the main subject of part 15 (LTO and Modern Linkers). See you in the next piece.
