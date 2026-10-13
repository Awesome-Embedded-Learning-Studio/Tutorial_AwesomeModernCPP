---
chapter: 13
difficulty: advanced
order: 14
platform: host
reading_time_minutes: 16
tags:
- cpp-modern
- host
- advanced
- 工具链
- 链接器
title: "Program Startup: From exec to main"
description: "The whole stretch between one typed ./app command and the first line of main: strace chasing the system calls after execve line by line; gdb's starti proving the first instruction lands in ld.so and that maps holds only the exe plus the interpreter, two files in all, standing the two-stage loading up; readelf -l formally reading INTERP and LOAD out of the program header table; three gdb breakpoints walking the order of _start, __libc_start_main, and main, the thirteen instructions of _start read with an eye for the ones that matter, main's address carried into rdi by a lea, and the call through a GOT slot being GLOB_DAT resolved at load time; the new shape on glibc 2.44 with init/fini's two arguments zeroed and __libc_csu_init deleted; the collect2 link line photographed live for the division of labor across the row of Scrt1/crti/crtbeginS/crtendS/crtn, with _init stitched from crti's head and crtn's tail and where the __gmon_start__ weak hook lands; the two entries of .init_array, frame_dummy and _GLOBAL__sub_I_, read for real; the -static comparison with 19 lines of trace missing the whole second stage; closing with half a sentence on exit and CMake's pie against -no-pie (Type DYN against EXEC)."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/14-program-startup.md
  source_hash: c5105cb0b3b4b77b925b98f32132242600527aa9555260b0fc187c313acaab12
  translated_at: '2026-10-13T04:28:48+00:00'
  engine: anthropic
  token_count: 4700
---

# Program Startup: From exec to main

At the end of the last piece we previewed the whole road once: from the one command we type to the first line of main executing, what lies between is a long stretch of loading, relocation, and runtime library initialization. Today's piece is here to pay that off. No replay of the preview — we wheel the tools into place and walk the road for real, one step at a time. To watch what happens before main, our eyes have to press right down to the layer of system calls: we bring out strace, a tool whose one and only business is tracing system calls. Which process trapped into the kernel, what it called down — strace writes it all down exactly as it happened.

Our specimen is still the volume's familiar old face libcalc: a dynamic library exporting an int variable plus two functions, paired with an app that calls it, and when we run the app we carry LD_LIBRARY_PATH=. along so it finds the library it uses in the current directory. The environment disclosure as usual: my experiments all ran in WSL2, on the combination of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, with strace at 7.2 and gdb at 17.2, and every stretch of output in this piece was really run. If the versions on your side differ, details like the names of system calls and the addresses may come out differently; the main line of steps has not changed in years.

## execve: The First Line, the Process Becomes Somebody Else

We put app on strace and ran it once; the entire output comes to 51 lines in all, and we read from the top. The first line reads:

```text
execve("./app", ["./app"], 0x7ffe681792c0 /* 70 vars */) = 0
```

The execve on the first line is a system call; its three parameters are the program's path, the list of command-line arguments, and the list of environment variables. The work it does is swapping the current process out wholesale for this program on the disk: the code, data, and stack of the process that was there before are all invalidated. At the moment it returns 0, the child the shell handed over is already gone; the one alive is app. The preview at the end of the last piece named it; today we are looking at the real body.

In the 50 lines that remain, one thing jumps out: turn the whole output inside out and you will not find a single call opening ./app or ld-linux. libcalc and libc each get whole stretches of openat and mmap, yet the exe and the dynamic linker — the principals themselves — never once got an openat. And this is truly not strace missing a record: the kernel maps the first two files inside execve itself, work that is finished inside the kernel, so of course the system-call log has no place for them. If you ask what makes us so sure the kernel loaded exactly those first two — we accept no word-of-mouth claims here; the next section produces the evidence.

## Two Stages: The Kernel Loads Only Two, the Rest Belongs to the Dynamic Linker

We ask gdb for a favor: its starti command can stop the process on the genuine first instruction, a spot earlier than main, and earlier too than the _start entry that the Entry line pointed at in the object-file anatomy piece.

```text
$ gdb -q -batch -ex starti ./app
Program stopped.
0x00007ffff7fdf7e0 in ?? () from /lib64/ld-linux-x86-64.so.2
```

(The few lines of gdb's own startup chatter I trimmed.) Where did it stop? Not inside our app — it landed in /lib64/ld-linux-x86-64.so.2. You read that right: this is the dynamic linker. The file output at the end of the volume's opening piece called it the interpreter, and in the dynamic-libraries overview we uncovered its true identity. While we are here, a quick look at the process's maps:

```text
0x0000555555554000 0x0000555555555000 ... r--p  /tmp/.../lab/app
0x0000555555555000 0x0000555555556000 ... r-xp  /tmp/.../lab/app
0x00007ffff7fbe000 0x00007ffff7fbf000 ... r--p  /usr/lib/ld-linux-x86-64.so.2
0x00007ffff7fbf000 0x00007ffff7fec000 ... r-xp  /usr/lib/ld-linux-x86-64.so.2
```

(The Size, Offset, and a few middle columns of each line I trimmed, and long paths got shortened; the addresses are as they came. I took the first two segments of each file, and the latter two of the four I trimmed away as well. One thing about the paths you may notice: gdb reports the landing under /lib64, while maps records /usr/lib — the two paths name the same file, the former going through a symlink, the latter being the resolved real body.) At this moment the process holds exactly two real bodies: the app's four segments plus the dynamic linker's four. The few small vvar and vdso segments that remain are the kernel's own things; we named vdso back in the dynamic-libraries overview. We turned the whole list over: libcalc's name is not there, and neither is libc's — their entrance has to wait until the dynamic linker gets to work. The two stages of loading stand established right here: the kernel stage recognizes only two files, one being the executable itself, the other the interpreter registered in its header. Not one of the other libraries on the NEEDED list is the kernel's business. The second stage is run by the dynamic linker in user space; from line 2 of the trace onward the work is all its, and what is pasted below is its stretch of loading libcalc:

```text
openat(AT_FDCWD, "./libcalc.so", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0755, st_size=15360, ...}) = 0
mmap(NULL, 16408, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7e9e759d1000
mmap(0x7e9e759d2000, 4096, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x1000) = 0x7e9e759d2000
mmap(0x7e9e759d3000, 4096, PROT_READ, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7e9e759d3000
mmap(0x7e9e759d4000, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7e9e759d4000
close(3)                                = 0
```

(While hunting for the library it also tried CPU-feature subdirectories like glibc-hwcaps along the way and came up empty; those two lines I trimmed. The read and getcwd lines between openat and fstat are trimmed too. The full rules of the search order belong to the library-search piece; we only look at the phenomenon.) The four mmap calls that follow sit address against address, and they are exactly what the run-time shape of the four LOAD entries in readelf -l corresponds to. The program header table makes its formal entrance today: the section table we read with readelf -S in the object-file anatomy piece is the cut seen from the linker's side, while the program header table gives the loading view — one LOAD corresponds to one mapping. Let's read the app's:

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

(The full table runs past a dozen entries — PHDR, four LOADs, several NOTEs are all in my hands; the trimmed parts have nothing to do with the main line, and the ellipsis is mine.) What INTERP registers is the dynamic linker's path, and it is from here that the kernel knows the second file it has to load. DYNAMIC is the loading view of the .dynamic section, which we read line by line with readelf -d in the dynamic-libraries overview; the NEEDED list lives inside it. A half-word more on GNU_RELRO: RELRO in its name reads as relocation read-only, meaning the pages are turned back to read-only once relocation is done. The mprotect calls further down the trace do exactly this work, turning the pages that had the GOT written into them back to read-only. In the position-independent piece we watched the dynamic linker fill in the GOT's table; once these mprotect calls have run, the values in the table are all settled and the written pages are back to read-only.

After libcalc comes libc, loaded by the same recipe; we will not paste it twice. Once everything is loaded, the trace still holds a stretch of arch_prctl, set_tid_address, and getrandom calls: that is the dynamic linker doing its own initialization — the base address of thread-local storage is prepared right here. When all of this has run, only then comes the output we know, start and base=100 total=117, sent out in a single write. You see, the first line of output we wrote queues up behind a long stretch of preparation.

## _start: The First Instruction That Belongs to the Program

Libraries loaded, tables filled — only then comes our program's entrance. When we looked at readelf -h in the object-file anatomy piece, the Entry line pointed at a spot called `_start`, and we said back then that everything before main was its business. Today we cross-check with nm: Entry's 0x1070 corresponds exactly to `_start`'s address.

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

(The ellipsis covers gdb's leading chatter and the thread-library notice lines; the #0 frame of each stop's bt is trimmed too — we kept only the second stop's #1, and it is the one that names the caller.) The order of the three stops is now plain as day: leading is _start, following is `__libc_start_main`, and closing the show is main. In the backtrace taken at the second stop, the caller shows itself too: `_start` called it. Two small details deserve disclosure. `__libc_start_main`'s home is inside libc, yet when gdb starts up there is not a shadow of libc in the process, so that breakpoint has to be hung as pending with set breakpoint pending on, and it only takes root once libc enters the scene. And b main lands on main+9, skipping the function prologue — an old gdb habit.

What does `_start` itself look like? The whole stretch, pasted for you; I trimmed gdb's leading chatter lines, and we pick out what matters:

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

The two leading instructions are routine: endbr64 belongs to the control-flow protection instructions, and the xor zeroes the frame pointer's base register, marking the outermost frame, so backtrace knows to stop when it walks this far. The few in the middle are the real content: under x86-64's calling convention the first six arguments travel in the six registers from rdi to r9 — and who is `_start` preparing arguments for? The call at the end has already confessed: the answer is `__libc_start_main`. Let's go register by register:

- rdi carries main's address, placed there by the lea. You see, main here is just a function pointer being passed along hand to hand.
- rsi and rdx carry argc and argv: pop %rsi pops argc, and mov %rsp,%rdx takes argv. The kernel laid the command line out at the top of the stack by convention, left there for whoever enters to pick up.
- r9 carries the sixth argument: mov %rdx,%r9 moves over the original value rdx held at entry — the cleanup function the dynamic linker entrusted, to be hung on the exit hooks later; we will meet it again when we talk about exit.

The zeroing of rcx and r8 deserves a couple more sentences. If you have been through older textbooks, what you saw was almost certainly another face: rcx carrying `__libc_csu_init` and r8 carrying `__libc_csu_fini`, with initialization and cleanup both farmed out to that pair. On our glibc 2.44 it is not so: since 2.34 glibc deleted them, the walking of that pre-main checklist of preparation moved inside libc itself, and the few files the linker brings along simply zero out and call it done. The textbook got old, and the numbers it matches no longer line up — which is exactly why this piece insists on measured output from start to finish.

Do not let the closing call *0x2f2b(%rip) slip past either. The way it calls `__libc_start_main` goes through the GOT's indirect route, with the target slot sitting at 0x555555557fc0. A check with readelf -r shows this slot's origin is R_X86_64_GLOB_DAT, the kind resolved at load time. In the LD_DEBUG timeline of the position-independent piece we watched with our own eyes `__libc_start_main`'s binding come before the start printout — lazy binding never gets a turn here. The reason is plain enough: the target's real body lives in libc, nobody could know its landing spot at link time, and going through the table was the only way to write it.

## __libc_start_main: After It Takes Over main's Address

`__libc_start_main` takes over main's address — what does it do from there? We wanted to go one level deeper and break on a function named `__libc_start_call_main`, but the break never took: the local libc's symbol table does not carry that name. Among the names we queried, nm -D was willing to hand over only `__libc_start_main`; the local symbols have been stripped. We took a step back and broke by address instead. Turning up `__libc_start_main`'s disassembly, the target of the call inside its body lands at 0x7ffff7c27700, and that is what we break on:

```text
Breakpoint 2, 0x00007ffff7c27700 in ?? () from /usr/lib/libc.so.6
#0  0x00007ffff7c27700 in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff7c278b9 in __libc_start_main () from /usr/lib/libc.so.6
#2  0x0000555555555095 in _start ()
rdi            0x555555555169      93824992235881
rsi            0x1                 1
rdx            0x7fffffffd438      140737488344120
```

(The repeated output of the earlier stops on the way here, along with the disassembly line at the stop and the program output after it, I trimmed together. The name showing as ?? has the cause stated above: stripped symbols. The function at 0x7ffff7c27700 is called `__libc_start_call_main` in glibc's source, split out of `__libc_start_main` since 2.34; the name I cite is the source's.) Lying in the registers are the same old three: rdi's value is 0x555555555169, matching to the character the lea's target in `_start`, carrying precisely main's address. rsi is argc's 1, and rdx is argv's address. Single-step from here and we are inside main.

As for `__libc_start_main`'s own work, what I could see in the disassembly is this: at its opening it registers that cleanup function from r9 with `__cxa_atexit`. The dynamic linker's initialization chores we already saw once at the end of the two-stage trace earlier — the arch_prctl, set_tid_address, and getrandom calls were exactly that. Then it walks the constructor table in .init_array, wakes main, and hands main's return value to exit. Who lives in the constructor table we will meet one by one in the section below. Not one step of this is our code, yet every step queues ahead of main.

## The Row of Startup Files: Who Put _start In

In the linker piece we left some promises: what `_start` does before main, and what each file in that row of startup files is in charge of — today we pay them off. Let's ask the first question backwards: `_start` is not something we wrote, so where does it come from?

The answer hides in the link command line. We hung -v on gcc and relinked once, capturing the collect2 line in full. collect2, introduced in the volume's first piece, is the wrapper around the linker ld:

```text
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/collect2 ... -dynamic-linker /lib64/ld-linux-x86-64.so.2 -pie -o app_v /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/Scrt1.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/crti.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/crtbeginS.o -L. ... /tmp/ccXT99dK.o -lcalc -lgcc -lgcc_s_asneeded -latomic_asneeded -lc -lgcc -lgcc_s_asneeded /usr/lib/gcc/x86_64-pc-linux-gnu/16/crtendS.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/crtn.o
```

(The ellipsis is mine; what got trimmed is the -plugin group, the --build-id group, and a string of -L directories, with the order kept as it came.) The arrangement says it all: leading is Scrt1.o, followed by crti.o and crtbeginS.o, only in the middle come our object file and the row of libraries with -lcalc, and bringing up the rear are crtendS.o and crtn.o. The five files standing in this line, collect2 carried along without consulting us once — in the opening piece we called them the C runtime's startup files. You also got a look at the -dynamic-linker argument along the way: the path it spells out is ld-linux, landing precisely at INTERP in the program header table.

Scrt1.o deserves a close look. Its symbol table, as nm lays it out, is all here:

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

We recognized two old acquaintances on the spot. One is the U-marked main, the other the U-marked `__libc_start_main`. Back when we linked no_main and it failed in the linker piece, the undefined reference to `main` in the error — the one citing it is the very file before us. Its .text is those thirteen instructions we looked at above, and the differences all fall on the relocations:

```text
$ readelf -rW Scrt1.o | head -4
Relocation section '.rela.text' at offset 0x2c8 contains 2 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
000000000000001b  000000040000002a R_X86_64_REX_GOTPCRELX 0000000000000000 main - 4
0000000000000021  0000000800000029 R_X86_64_GOTPCRELX     0000000000000000 __libc_start_main - 4
```

In the .o version, taking the address of main and of `__libc_start_main` both go through the GOT. Once the link has produced the PIE we made the acquaintance of in the position-independent piece, the linker relaxes the through-GOT fetch of main into a direct lea — the lea we saw in gdb is that relaxation's handiwork — while `__libc_start_main`, living inside libc, stays un-relaxed, keeping the through-table form. The GOTPCRELX suffix hanging on the relocation names is exactly the opening left for relaxation; we will not expand on it.

The remaining four in that row we introduce one sentence each:

- crti.o holds the openings of the two functions `_init` and `_fini`. The `__gmon_start__` weak symbol we met in the linker piece — the real body of its hook sits in crti.o's `_init`. Let's look at the linked result for real:

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

  Savor the structure: the leading endbr64 and sub come from crti.o, the closing add and ret come from crtn.o, and `_init`'s body is stitched together by the linker out of fragments from the two ends. The four instructions sandwiched in the middle belong to neither end — they are the `__gmon_start__` hook: call it if a definition turns up, and if the value loaded is zero, jump straight past. In our program it is an undefined weak symbol of the w variety, so these few instructions spin idle here. Once a profiling tool gets linked in, they find their employment. In the linker piece we explained why weak symbols leave an opening; what we see here is where that opening lands in the startup flow.

- crtbeginS.o contributes the first entry of .init_array, frame_dummy, and the first entry of .fini_array, `__do_global_dtors_aux`, plus the two small functions deregister_tm_clones and register_tm_clones. TM is short for transactional memory, and these two are likewise hooks hung on weak references, skipped just the same when nobody defines them.
- crtendS.o puts out two boundary symbols, `__FRAME_END__` and `__TMC_END__`, used as the terminus when the crtbeginS side counts through its tables.
- crtn.o, in nm's output, has not a single symbol. It is the tail of .init and .fini, and the last two instructions of the `_init` above are its entire content.

One more thing to notice: the S suffix in the file names — the S of Scrt1, the S of crtbeginS — marks the PIE versions. For non-PIE links it is crt1.o and crtbegin.o that take the stage, doing the same work.

## .init_array: The Constructor Table Before main

The startup files walked `_start` onto the stage; the table libc walks next is what we look at now. The constructors of C++ global objects register in precisely .init_array. Let's write a global object with a constructor:

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

The constructor ran ahead of main, and the destructor ran behind it. Where does the table live? readelf gives up the answer for us:

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

Sixteen bytes of .init_array, counted eight bytes to an entry, come out to exactly two entries. The frame_dummy at 0x1140 is contributed by crtbeginS.o. That extra line nm showed us, `__frame_dummy_init_array_entry`, sits precisely at the position of the table's first entry, placed there at link time — and with that, frame_dummy's provenance checks out. The `_GLOBAL__sub_I_g_guard` at 0x1199 is the constructor wrapper the compiler generated on g_guard's behalf; under a name grown like that you will not find it in our source — the compiler made it. When libc starts up it calls through the table's entries one by one, and only after finishing does it enter main. Functions marked `__attribute__((constructor))` get stuffed into the same table.

The order of the two entries has its logic too: frame_dummy stands in front and our entry follows behind, matching the link line's arrangement — crtbeginS.o is placed ahead of our object file. Construction order across translation units is something the standard leaves unspecified; the risks of an unguaranteed construction order and the function-local static countermeasure get a dedicated section in the ABI piece. Here we only pin down the vehicle: the construction timing of C++ global objects, mechanically speaking, is precisely the timing of this table being walked.

## The Static Comparison: One Layer Less, One Whole Stage Less

How much work the dynamic loading layer actually adds, we will not estimate by mouth — we link the same specimen fully statically and compare. libcalc gets packed into a .a along the way:

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

(The grep at the end came up empty and the exit code came back 1: PT_INTERP, INTERP's type name in the program header table, no longer exists in this file — I made a point of flagging it with the exit code.) Type has become EXEC: that half-sentence owed from the object-file anatomy piece — a fixed-address executable is EXEC — is paid off here, and Entry's 0x402ea0 is an address inside the fixed-base segments too. What is more satisfying still is the strace comparison:

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

Once execve lands, it is straight into libc's self-initialization: those brk and arch_prctl calls, because libc's code is already baked whole into the file — nothing to load. Not a single openat or mmap in between: ld.so.cache is gone, and so are the libraries and the dynamic linker. The comparison of 19 lines against 51 (we are counting the lines written into the file; running to a terminal adds one ioctl query), the 32 lines gone — what went missing is the whole of the second stage. In the dynamic-libraries overview we saw what shared libraries save in memory; this piece supplies the other side of the ledger, the cost. So the dynamic loading layer is an entire stretch of process appended onto the startup road.

## After main, Half a Look

This whole volume stops at main, but having now seen startup in full, a half-glance past main is all we need. guard's output already showed us: the destructor lands after main. Mechanically it is a one-sentence affair: main's return value is handed to exit, exit cleans up in reverse order of the `__cxa_atexit` registrations — the cleanup function stowed in r9 is on the books too — then .fini_array is walked, and the final exit_group system call brings the curtain down; it is the last system call in the trace.

## The CMake View

Under the topic of startup, the CMake switch most often asked about is one we look at here: pie, and pie alone. We set up two targets to compare, with the library built as STATIC to spare us a second dynamic setup:

```cmake
add_library(calc STATIC libcalc.c)
add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
add_executable(app_fixed main.c)
target_link_libraries(app_fixed PRIVATE calc)
target_link_options(app_fixed PRIVATE -no-pie)
```

(The include directory line for calc.h I trimmed; the complete one is in the experiment package.)

```text
$ cmake -S . -B build && cmake --build build
$ readelf -h build/app | grep -E "Type|Entry"
  Type:                              DYN (Position-Independent Executable file)
  Entry point address:               0x1050
$ readelf -h build/app_fixed | grep -E "Type|Entry"
  Type:                              EXEC (Executable file)
  Entry point address:               0x401050
```

Both targets ran, and their output was identical to the character. What CMake hands GCC by default is pie, and Type records DYN. Hang -no-pie on and it switches back to EXEC, with Entry changing along with it, from a relative address to 0x401050, back in the traditional fixed-base segments starting at 0x40. One easily misunderstood point deserves a line: app_fixed still carries an INTERP, and it still links libc dynamically. What -no-pie moves is the form of the addresses; it does not touch the manner of linking one bit — if you want static, that is -static's job, and the two switches each mind their own. As for the little business of finding libraries at run time, the build-side entry point is BUILD_RPATH, covered in the CMake section of the library-search piece; we will not replay it.

## Next: What Else Link Time Can Squeeze Out

Here we are, and the whole road from execve to main is walked: the kernel loads the first two, the dynamic linker raises the whole dependency tree, `_start` hands main over, `__libc_start_main` walks through .init_array, and only here does the first line of code we wrote actually run. Counting back over the trip, the system calls spent come to a few dozen — yet every step has a name and a face.

The story ends here, but we leave you something to chew on. Reading this volume up to now, the compiler's optimizations have only ever seen a single translation unit at a time; the linker, for its part, takes in all the object files at once, yet all it does is move things around and wire symbols up — beyond the bit of rewriting that relocation does, it does no optimizing at all. What could we squeeze out if we moved optimization to link time, letting it work with each translation unit's complete information? Link-time optimization (LTO) is exactly that, and it is the proper subject of *LTO and Modern Linkers: Moving the Optimization Boundary to Link Time*. See you in the next piece.
