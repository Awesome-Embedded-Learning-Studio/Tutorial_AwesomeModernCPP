---
chapter: 13
difficulty: advanced
order: 9
platform: host
reading_time_minutes: 20
tags:
- cpp-modern
- host
- advanced
- 工具链
- 链接器
title: "Position-Independent Code and GOT/PLT: Addresses Settled at Load Time"
description: "The linker's real error when a .so is built without PIC, instruction-by-instruction disassembly of a mov through the GOT and a call through the PLT on x86-64, the two dynamic relocation types GLOB_DAT and JUMP_SLOT, lazy binding caught in the act with LD_DEBUG and a gdb before/after comparison, LD_PRELOAD genuinely replacing a call made from inside the library, plus the COPY relocation on the executable side, closing with CMake's POSITION_INDEPENDENT_CODE."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/09-pic-got-plt.md
  source_hash: ff746950f6ee72532ab057814a6b72912e380de26b84a8d76a079352ae943862
  translated_at: '2026-10-13T04:10:38+00:00'
  engine: anthropic
  token_count: 13500
---

# Position-Independent Code and GOT/PLT: Addresses Settled at Load Time

The end of 08 left an opening we never closed: the dynamic library's code gets mapped into the process, but what exactly the call line in the executable says — we set that question aside at the time. The opening has a back story, too. In 01's hello.s, a call operand wore an @PLT suffix on its tail, and file reported that the program we built was pie; all we said back then was that pie means position-independent. The later stretch of 05 named the matter as well: a pie program's addresses are settled at link time and get shifted wholesale at load time — the mechanism was left for this piece. Today we make good on these debts one by one.

First, let's state the problem plainly. Nobody at build time knows which address a shared library will be loaded at. And yet machine code cannot live without addresses: fetching data takes an address, and calling a function takes an address too. The compiler and the linker have to write out the instructions without knowing the addresses, then have someone fill in the real ones at the moment of loading. The whole piece watches two things: what the writing looks like, and how the one doing the filling does it.

The environment disclosure, as always: my experiments all ran in WSL2, with the combination of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, and every stretch of output in this piece was genuinely run. If your versions differ, details like addresses and paths may drift; the mechanics themselves haven't budged in years.

## Shifted Wholesale: Two Processes, Two Sets of Addresses

The libmyutil.so that 08 asked us to keep around, we won't be using this time. To see the GOT/PLT machinery clearly, we swap in a leaner three-line specimen, plus a program that uses it:

```c
// libcalc.c — exports one variable and one function, then lets another function use them
int tax_rate = 17;

int base_tax(void) { return 100; }

int total_tax(void) { return base_tax() + tax_rate; }
```

The first thing main.c does is print a line of start, and only then do the calls to base_tax and total_tax get their turn. The command that builds the library carries a -fPIC — what it does and why it's non-negotiable we take up in the next section; here, let's just get the program running and see with our own eyes the wholesale shifting 05 spoke of:

```text
$ gcc -fPIC -shared libcalc.c -o libcalc.so && gcc main.c -L. -lcalc -o app && LD_LIBRARY_PATH=. ./app
start
base=100 total=117
```

(The LD_LIBRARY_PATH=. tells the dynamic linker to look for libraries in the current directory; the complete rules of the search order belong to the next piece.) Let's also build an app_wait of the same make: after printing the results it stalls for 30 seconds, leaving us an observation window. We start two copies of it at the same time and pull up each one's /proc/PID/maps:

```text
--- P1
5b08b67a6000-5b08b67a7000 r--p 00000000 00:49 103708  …/pic/app_wait
7773306ff000-777330700000 r--p 00000000 00:49 103684  …/pic/libcalc.so
--- P2
5a1c3db5c000-5a1c3db5d000 r--p 00000000 00:49 103708  …/pic/app_wait
78897cfa7000-78897cfa8000 r--p 00000000 00:49 103684  …/pic/libcalc.so
```

(Read it two lines at a time; I trimmed the long paths, and the numbers are untouched.) In each pair, the first line is the program itself; the second is the library. The inode numbers at the end of each maps line tell us that P1 and P2 mapped the very same app_wait file, and the two libcalc.so mappings are the same file too. And the addresses? The program's two copies sit at 5b08… and 5a1c…, the library's at 7773… and 7889…. One and the same file, two processes in front of us, and what we get are two sets of addresses.

I didn't stage any of this; it's the kernel deliberately shuffling things at every load, driven by a mechanism called ASLR — Address Space Layout Randomization, in full. On our system /proc/sys/kernel/randomize_va_space reads 2, meaning full randomization. Add the fact that multiple libraries cannot crowd onto the same address, and the answer is laid out before us: the library's load address cannot be supplied at build time.

So what about the machine code? The laziest way to write it is to bake the address straight into the instruction, but the address changes with every load, and a baked-in one is wrong. Someone might think: once loading is done, have the dynamic linker patch every address in the code, one by one — would that work? Of course not. The code segment in physical memory is shared among multiple processes; in 08 we saw that it lives in exactly one copy, and if every process patched out its own copy, the sharing would be gone. One road remains: write instructions that contain no absolute addresses at all. Position-Independent Code is exactly that, and PIC is its common abbreviation.

## -fPIC: Bake In an Address and the Linker Stops You

Position independence isn't achieved by proclamation. The compiler rewrites it into the code instruction by instruction, and -fPIC is the switch that turns that work on. Let's take the switch off and see what fate awaits a hardwired address:

```text
$ gcc -shared libcounter.c -o libcounter_nopic.so
/usr/bin/ld: /tmp/ccF4K1RU.o: warning: relocation against `counter' in read-only section `.text'
/usr/bin/ld: /tmp/ccF4K1RU.o: relocation R_X86_64_PC32 against symbol `counter' can not be used when making a shared object; recompile with -fPIC
/usr/bin/ld: final link failed: bad value
collect2: error: ld returned 1 exit status
```

(libcounter.c is written to the same pattern as libcalc.c — one exported variable plus one function that reads it; the temp file name ccF4K1RU.o changes every run.) This is a genuine error: the link fails on the spot, and not even an artifact lands. Old textbooks often carry the line that this is "just a relocation warning"; on our GCC 16.2.1 that no longer holds. What got stopped? Let's look at the load instruction the compiler generates without PIC:

```text
$ objdump -dr --no-show-raw-insn -M intel libcounter.o
0000000000000000 <get_counter>:
   0:	push   rbp
   1:	mov    rbp,rsp
   4:	mov    eax,DWORD PTR [rip+0x0]        # a <get_counter+0xa>
			6: R_X86_64_PC32	counter-0x4
   a:	pop    rbp
   b:	ret
```

What we see is a mov that loads counter directly. By the rip-relative addressing we learned to read in 04, it states how far the variable sits from the instruction itself. With that distance hardwired, the program has effectively asserted that counter will spend its whole life inside the module that compiled it. But it is an exported symbol, and at run time it may be displaced by another module — what displacement looks like we'll actually run through in the dedicated section on interposition later. Thinking it back over, the linker simply sealed off the direct-load route: R_X86_64_PC32 is not allowed on a displaceable symbol in a shared library, and that recompile with -fPIC in the error is the linker telling you to change how it's written.

Two exceptions are worth noting along the way; both linked through fine, which proves from the other side exactly what the case above was blocking. A variable marked static links on the first try — a private symbol has no standing to be displaced. A library that only calls external functions and never touches exported variables also links through, because the linker built PLT entries for the calls (PLT stands for Procedure Linkage Table, which we get to in a moment). The version you're more likely to meet in real work is the .a case: you take someone's static library that was built without -fPIC — the kind of .a we packaged with ar in 06 — and try to link it into your own .so, and the error that comes out is the same make as the one above, with the .a's members playing the lead.

One more glance at the executable side, where there is a second setting called -fPIE — Position-Independent Executable in full, meaning a position-independent executable. That pie that file reported back in the opening piece, today we cash in its identity:

```text
$ readelf -h app | grep Type; readelf -h app_nopie | grep Type; readelf -h libcalc.so | grep Type
  Type:                              DYN (Position-Independent Executable file)
  Type:                              EXEC (Executable file)
  Type:                              DYN (Shared object file)
```

(app_nopie is the one we built with -no-pie added; the other commands are the same make.) The division of labor between the two settings comes down to one question: guarding against displacement. A library uses -fPIC because its exported symbols can be displaced at any moment, so every cross-module reference has to leave room for that. An executable uses -fPIE: within the process it sits at the head of the symbol lookup order, its own definitions won't be displaced by anyone, and the only thing to guard against is being moved. Now look at the Type column: a pie program is DYN just like a .so — both are built to run no matter where they're put — while EXEC is reserved for the old-style executables of fixed address.

## GOT: Data Addresses, Fetched from a Table

Now we take apart the main act. With total_tax's disassembly in hand, the data-loading part is what we look at first:

```text
$ objdump -d --no-show-raw-insn -M intel libcalc.so (excerpt)
0000000000001114 <total_tax>:
    1114:	push   rbp
    1115:	mov    rbp,rsp
    1118:	call   1030 <base_tax@plt>
    111d:	mov    rdx,QWORD PTR [rip+0x2eac]        # 3fd0 <tax_rate@@Base-0x40>
    1124:	mov    edx,DWORD PTR [rdx]
    1126:	add    eax,edx
    1128:	pop    rbp
    1129:	ret
```

Reading tax_rate takes two mov instructions; let's take them one at a time. The first is rip-relative, and the 3fd0 in the comment is the point: that address lands inside a table called the GOT. GOT stands for Global Offset Table — the "sixteen pieces of this volume" checklist at the end of 01 already spoke its name. What it fetches is not the value of tax_rate. What it wants is the content stored in slot 0x3fd0 of the table, which is tax_rate's run-time address. The second mov takes that acquired address and only then loads the real value, 17. Nowhere in the code is an address hardwired; the only hardwired thing is how far away the GOT slot sits. The GOT grows on the library's own body, so when the library shifts wholesale the slot shifts with it, and that distance never changes.

So who registered the address in this slot, and who fills it? Let's call on an old friend from 04, readelf -r. I filtered the output down to the names we care about; the full table also carries a few miscellany the linker registered on behalf of the startup code:

```text
$ readelf -rW libcalc.so | grep -E 'tax_rate|base_tax'
0000000000003fd0  0000000500000006 R_X86_64_GLOB_DAT      0000000000004010 tax_rate + 0
0000000000004000  0000000700000007 R_X86_64_JUMP_SLOT     0000000000001109 base_tax + 0
```

The 0x3fd0 line is precisely tax_rate's registration: the type R_X86_64_GLOB_DAT means that at load time, tax_rate's real address gets filled into slot 0x3fd0. The relocations we read in 04 were PC32 and PLT32 — that batch of placeholders got filled in at link time. GLOB_DAT is the other half of relocation work: it is left to the dynamic linker, and the filling happens at run time. This is what "addresses settled at load time" looks like inside the file.

There is a third way to fetch data, and it shows up on the executable's side of the fence; let's look while we're here. main.c only called functions, so I wrote a separate ratemain.c — the whole file is one printf that reads tax_rate directly — and the program it links into is called rateapp:

```text
$ LD_LIBRARY_PATH=. ./rateapp
rate=17
$ readelf -rW rateapp | grep COPY
0000000000004018  0000000700000005 R_X86_64_COPY          0000000000004018 tax_rate + 0
$ nm rateapp | grep tax_rate; nm libcalc.so | grep tax_rate
0000000000004018 B tax_rate
0000000000004010 D tax_rate
```

Look at the type: R_X86_64_COPY — that is the copy relocation. The end of 08 flashed an R_X86_64_COPY (shared_seed), and this is the job it does; the oddity of shared_seed living in bss finds its answer right here. The two nm lines are the most interesting. In the library, tax_rate is a D: it lives in .data, and the initial value 17 is its doing. In rateapp, a tax_rate has appeared out of thin air, and it holds the letter B — by 04's letter table, B governs .bss. At load time, the initial value got copied wholesale by the dynamic linker into the executable's .bss copy, and the COPY record is the warrant for that copying. Once the copying is done, both sides use one and the same instance — and when we photograph the binding records with LD_DEBUG below, we'll see that libcalc's own GOT slot for tax_rate is filled with the address of the copy made inside the executable. The executable's own access is refreshingly blunt: one rip-relative direct load, never even brushing the edge of the GOT. Why so cheap? Executables of that era were full of absolute addressing, and the linker had to hold a definite address in hand; copying one into .bss was the only choice. PIE kept the arrangement: the one at the head of the lookup order is the executable itself, so a direct load from its own copy is the cheapest of all. The price is that the same variable now has two records in one process — the problems that follow from that belong to 12 (ABI Design).

## PLT: Function Calls Take One Extra Hop

Done with reading data, we return to that call line in total_tax. In the opening piece's hello.s, @PLT hung off the tail of the call operand, and all we said then was that it had something to do with how dynamic-library calls are made: the PLT is a trampoline, and the GOT is a table for looking up addresses. Once linking is finished, it has grown into a real, physical thing. Let's look at main on the app side — app has a PLT of its own, and we'll see it shortly:

```text
$ objdump -d --no-show-raw-insn -M intel app (excerpt)
    1180:	call   1060 <base_tax@plt>
    1188:	call   1050 <total_tax@plt>
```

The call's target is 0x1060, and the label reads base_tax@plt. PLT stands for Procedure Linkage Table; in 04 we glimpsed it from afar, calling it the trampoline that function calls pass through. Today we read its entries out one by one. Each entry is a small fixed stretch of jump code — that is what the jargon calls a stub. app and libcalc each keep their own set, each serving its own call sites. The stub inside libcalc looks like this, together with that special first entry ahead of it:

```text
$ objdump -d --no-show-raw-insn -M intel libcalc.so (excerpt)
    1020:	push   QWORD PTR [rip+0x2fca]        # 3ff0 <_GLOBAL_OFFSET_TABLE_+0x8>
    1026:	jmp    QWORD PTR [rip+0x2fcc]        # 3ff8 <_GLOBAL_OFFSET_TABLE_+0x10>
    102c:	nop    DWORD PTR [rax+0x0]
0000000000001030 <base_tax@plt>:
    1030:	jmp    QWORD PTR [rip+0x2fca]        # 4000 <base_tax@@Base+0x2ef7>
    1036:	push   0x0
    103b:	jmp    1020 <_init+0x20>
```

The base_tax@plt stub is three instructions in all; let's read them one at a time. The lead jmp jumps to whatever address sits in GOT slot 0x4000; the registration for that slot is a JUMP_SLOT, used exactly like GLOB_DAT — one minds functions, the other minds data. The push 0x0 that follows pushes the relocation's index number onto the stack, which amounts to announcing itself: base_tax, number 0 in the queue. The closing jmp 1020 jumps to the PLT's very first entry — PLT0, in the jargon. It does the common work: it pushes the GOT+8 slot, which at load time gets filled with a record describing this module (the link_map). Then it hands control to the GOT+16 slot, which holds glibc's resolver function; we'll call it `_dl_runtime_resolve`. The resolver takes the index off the stack, looks up base_tax's real address, writes the found address back into slot 0x4000, and only then delivers execution into base_tax itself.

So what does slot 0x4000 hold as it ships from the factory? The slots the PLT uses live in a section of their own called .got.plt; let's pour its bytes out exactly as they are:

```text
$ readelf -x .got.plt libcalc.so

Hex dump of section '.got.plt':
 NOTE: This section has relocations against it, but these have NOT been applied to this dump.
  0x00003fe8 003e0000 00000000 00000000 00000000 .>..............
  0x00003ff8 00000000 00000000 36100000 00000000 ........6.......
```

Look at the table's contents: four items in all. 0x3fe8 stores the address of the dynamic segment (.dynamic), 0x3e00 — the very thing we read with readelf -d for DT_NEEDED in the previous piece. 0x3ff0 and 0x3ff8 are two vacancies reserved for the dynamic linker; the link_map and the resolver get their values only at load time, so the file holds two zeros right now. 0x4000, read in little-endian order, comes out as 0x1036 — precisely the address of the stub's push 0x0 instruction. The factory value of 0x4000 strings the whole story together: in its as-shipped state, the lead jmp takes one turn in a circle and lands right back on its own stub's push instruction, and from there it goes push, jump into PLT0, into the resolver. Once the resolver has written the real address back into the slot, the next call flies straight into base_tax from the first jmp — the resolving work happens exactly once in a lifetime. That as-shipped value, looping back around before setting out, is the entire machinery of lazy binding.

## Lazy Binding: Resolved Only on the First Call

Deferring a JUMP_SLOT's resolution to the first call is called lazy binding. GLOB_DAT gets no such treatment — it is fully handled during loading. Verbal claims we don't accept: we'll make the dynamic linker confess on its own. Set the environment variable LD_DEBUG=bindings and it writes down every symbol binding it performs. app prints a line of start before making its first call, so the dividing line is ready-made:

```text
$ LD_DEBUG=bindings LD_LIBRARY_PATH=. stdbuf -oL ./app 2>&1 | grep -E 'base_tax|tax_rate|start|base=' | grep -v stdbuf
   1578855:	binding file ./libcalc.so [0] to ./libcalc.so [0]: normal symbol `tax_rate'
   1578855:	binding file ./app [0] to /usr/lib/libc.so.6 [0]: normal symbol `__libc_start_main' [GLIBC_2.34]
start
   1578855:	binding file ./app [0] to ./libcalc.so [0]: normal symbol `base_tax'
   1578855:	binding file ./libcalc.so [0] to ./libcalc.so [0]: normal symbol `base_tax'
base=100 total=117
```

(That string of digits at the head of each line is the process ID; yours will come out different. We set stdbuf -oL to make stdout line-buffered — otherwise the stderr traveling down the pipe would jump the queue, and the ordering would lie to us.) Now the dividing line: the bindings of tax_rate and `__libc_start_main` sit ahead of start, all the start-up work already done. The two base_tax bindings sit after start, one from app and one from libcalc — exactly each of the two PLTs' own first call. Run it once more with LD_BIND_NOW=1 and all four move up ahead of start; the line between lazy and eager could not be clearer.

We caught the slot's before and after values too, and this time gdb takes the stage. We plant one breakpoint at the top of main and another where the two calls return: gdb turns ASLR off by default, which is what pins the library's load base in place and makes the numbers easy to read:

```text
libcalc base = 0x7ffff7f9d000
before GOT slot = 0x7ffff7f9e036
   0x7ffff7f9e036 <base_tax@plt+6>:	push   $0x0
after  GOT slot = 0x7ffff7f9e109
   0x7ffff7f9e109 <base_tax>:	push   %rbp
```

Before the first call happens, the slot we read holds base plus 0x1036, pointing at the push instruction inside the stub. After the call has happened, the slot holds base_tax itself (base plus 0x1109), matching the 0x1109 in the relocation table's Sym. Value column exactly. The stub hasn't changed by a single byte; the only thing that changed is those 8 bytes in the GOT.

So is the default lazy or eager? We don't have to memorize anyone's word for it — readelf -d settles it at a glance: a FLAGS entry carrying BIND_NOW means eager, and without it, lazy. On my machine, the executable GCC 16.2.1 links by default carries no such flag: it goes lazy. To choose eager deliberately there are two roads: add `-Wl,-z,now` at link time and the flag goes straight into the file, or set LD_BIND_NOW=1 before running for a one-time eager binding. Quite a few distributions and hardening configs now make eager binding the default: you pay a little extra start-up time and get every resolution finished up front plus a smaller attack surface — often a worthwhile trade for server-side programs. How to choose still comes down to whether your program cares more about start-up speed or about steadiness at run time.

## Interposition: Calls Inside the Library Left a Door Open

Back to the question we set aside earlier: total_tax calls its own base_tax — why should that have to go through the PLT? The binding records above have already let the answer slip out: when libcalc resolved base_tax, it looked for it as a global symbol. When the dynamic linker resolves a symbol, it lays out the export tables of every module in the process and searches from the front — whoever comes first wins, and there is no special privilege by which a library always uses its own. A same-named definition in another module displacing a call made inside the library: the jargon for this is interposition. Let's actually run it once; our impostor is one line long:

```c
// libfake.c — the impostor: a same-named definition, return value deliberately different
int base_tax(void) { return 999; }
```

```text
$ LD_PRELOAD=$PWD/libfake.so LD_LIBRARY_PATH=. ./app
start
base=999 total=1016
```

LD_PRELOAD has the dynamic linker load this library ahead of all the others. Set it against the base=100 from the start: the 999 is easy to explain — app's own call got displaced. What matters is 1016 = 999 + 17: the base_tax() inside total_tax called the impostor too. A call the library wrote itself had its target swapped by an outsider, and we didn't have to recompile a single line. This is not an accident. It is part of the design: precisely because calls inside a library go through the PLT and leave room, replacement becomes possible. Plenty of the memory-profiling and tracing tools you'll meet later on got their start by preloading their own malloc and free — the whole trick lives right here. The door being open is convenient; the danger is just as real. The complete rules of who comes first, and the means for narrowing the door — symbol visibility — get their own piece in 11. Where preloaded libraries get found from is one sentence away in the next piece.

## The CMake Perspective

Typing -fPIC by hand is rare these days; the build system minds this setting for us. CMake's master switch is CMAKE_POSITION_INDEPENDENT_CODE, and our minimal project looks like this:

```cmake
cmake_minimum_required(VERSION 3.20)
project(pic_demo C)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(calc STATIC libcalc.c)
add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
```

We dig the real compile commands out with --verbose and check them against each other (paths trimmed): calc's side carries -fPIC, and app's side carries -fPIE — exactly the two settings this piece has been about.

```text
… -fPIC … -o CMakeFiles/calc.dir/libcalc.c.o …
… -fPIE … -o CMakeFiles/app.dir/main.c.o …
```

When we want finer control, the POSITION_INDEPENDENT_CODE target property comes into play: set_target_properties turns it for a single target, and the exceptions the global variable doesn't cover belong to it. We measured the defaults too: a SHARED library carries -fPIC automatically, without a word from you. A STATIC library by default does not — when does that hurt? The moment you take a default static library and link it into a .so, out comes the very R_X86_64_PC32 we met at the top of this piece, in the same error shape as the opening one, with tax_rate inside an .a member playing the lead instead of counter. So for projects whose static libraries go out to be turned into .so's, turning PIC on globally is the low-worry starting configuration. As for the executable's PIE, you don't have to lift a finger: app's link line doesn't even carry -pie, and the artifact still came out a pie executable. Defaults belong to the toolchain, and CMake is happy not to interfere.

## What Comes Next: In What Order Does the Linker Find Libraries

Position independence has delivered the run-anywhere guarantee: no absolute addresses embedded in the code anymore, data exits through the GOT, function calls exit through the PLT, and the real addresses get filled in at load time by the dynamic linker. In 05 we watched helper get its 0x112c at link time and then be shifted wholesale at load. After the shift, every reference still connects — and what connects them is exactly today's machinery.

The machine-code side is settled now, but on the engineering side one question will meet you every day: when linking, in what order does the linker search for libraries? Does -lcalc find libcalc.so or libcalc.a? And at run time, where do LD_LIBRARY_PATH and the system directories each rank? The next piece unfolds those rules in full.
