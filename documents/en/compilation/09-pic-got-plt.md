---
chapter: 13
difficulty: advanced
order: 9
platform: host
reading_time_minutes: 26
tags:
- cpp-modern
- host
- advanced
title: "Position-Independent Code and GOT/PLT: Addresses Settled at Load Time"
description: "The linker's real error when a .so is built without PIC, instruction-by-instruction disassembly of a mov through the GOT and a call through the PLT on x86-64, the two dynamic relocation types GLOB_DAT and JUMP_SLOT, lazy binding caught in the act with LD_DEBUG and a gdb before/after comparison, LD_PRELOAD genuinely replacing a call made from inside the library, plus the COPY relocation on the executable side, closing with CMake's POSITION_INDEPENDENT_CODE."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/09-pic-got-plt.md
  source_hash: b06bb74f3819a33fb74116e60e0f30f2e0aee0bfaa0f5cfa5cab8b107d612e5d
  translated_at: '2026-09-30T09:42:36+00:00'
  engine: glm
  token_count: 4457
---

# Position-Independent Code and GOT/PLT: Addresses Settled at Load Time

At the end of part 08 we left an opening hanging: the dynamic library's code got mapped into the process, but just what does that call line in the executable say? We put that question aside unexplained. And the trail leading to it has a history of its own. In part 01's hello.s, an @PLT suffix hung off the tail of a call operand, and file reported that the program we had built was pie; at the time we left it at one sentence, that pie means position-independent. The back half of part 05 named the matter too: a pie program's addresses are settled at link time, and the whole thing shifts again when it loads, machinery deferred to part 09. Today we make good on all of it, one item at a time.

To begin, let's get the problem clearly in view. Which address a shared library will be loaded at, nobody knows at build time. Yet machine code cannot get away from addresses: reading data takes an address, and calling a function takes an address. The compiler and the linker have to write the instructions out without knowing the addresses, and then have somebody patch the real ones in at the moment of loading. The whole piece today comes down to two things: what the writing looks like, and how the one doing the patching patches.

As usual, the environment disclosure: all my experiments ran in WSL2, on the combination of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, and every piece of output in this piece was really run. If your versions differ, details like addresses and paths may drift, but the mechanics themselves haven't changed in years.

## Shifted as a Whole: Two Processes, Two Sets of Addresses

The libmyutil.so that part 08 told us to keep, we have no use for this time. To see the workings of the GOT and PLT clearly, we swap in a slimmer three-line specimen, plus a program that uses it:

```c
// libcalc.c — export one variable and one function, then have another function use both
int tax_rate = 17;

int base_tax(void) { return 100; }

int total_tax(void) { return base_tax() + tax_rate; }
```

Over in main.c, the first thing done is to print a line reading start, and only then come the calls to base_tax and total_tax. The library build command carries a -fPIC; what it is for and why it cannot be left off, we cover in the very next section. For now we simply run the program and watch with our own eyes the wholesale shifting that part 05 talked about:

```text
$ gcc -fPIC -shared libcalc.c -o libcalc.so && gcc main.c -L. -lcalc -o app && LD_LIBRARY_PATH=. ./app
start
base=100 total=117
```

(The LD_LIBRARY_PATH=. we carry along tells the dynamic linker to look for libraries in the current directory; the complete rules of the search order get unfolded in part 10 (Library Search).) We also prepare a sibling app_wait: after printing its result it sleeps for thirty seconds, leaving us a window to observe. We start two of them at once and dig out each one's /proc/PID/maps:

```text
--- P1
5b08b67a6000-5b08b67a7000 r--p 00000000 00:49 103708  …/pic/app_wait
7773306ff000-777330700000 r--p 00000000 00:49 103684  …/pic/libcalc.so
--- P2
5a1c3db5c000-5a1c3db5d000 r--p 00000000 00:49 103708  …/pic/app_wait
78897cfa7000-78897cfa8000 r--p 00000000 00:49 103684  …/pic/libcalc.so
```

(Read them two lines at a time; I trimmed the long paths, and the numbers are untouched.) The first line of each pair is the program itself; the second is the library. The inode numbers at the end of each maps line tell us that P1 and P2 mapped the very same app_wait file, and the libcalc.so mappings are the same file too. And the addresses? The program's two copies sit at 5b08… and 5a1c…, the library's at 7773… and 7889…. One and the same file, two processes, and what they get are two sets of addresses.

I arranged none of this. It is the kernel deliberately shuffling things at every load, and behind it stands a mechanism called ASLR, short for Address Space Layout Randomization. On my system /proc/sys/kernel/randomize_va_space holds the value 2, which is full randomization. Add the fact that several libraries cannot crowd into the same address, and the answer is sitting in front of us: a library's load address is not something build time can hand over.

So what is machine code to do? The cheapest way to write it is to bake the address straight into the instruction, but the address changes at every load, so baking it in comes out wrong. Someone might think: after loading, have the dynamic linker go patch the addresses inside the code one by one — would that do? Of course not. The code segment is shared in physical memory by multiple processes; in part 08 we saw that only one copy lives there. If everyone patched in their own values, the sharing would be gone. Only one road is left: arrange things so that the instructions as written contain no absolute address at all. That is what Position-Independent Code means, and PIC is its usual abbreviation.

## -fPIC: Address-Baked Code Gets Stopped by the Linker

Position independence is not something you get by shouting for it. It is something the compiler builds instruction by instruction, and -fPIC is the switch that does the building. Let's strip it off and see what comes of baking addresses in:

```text
$ gcc -shared libcounter.c -o libcounter_nopic.so
/usr/bin/ld: /tmp/ccF4K1RU.o: warning: relocation against `counter' in read-only section `.text'
/usr/bin/ld: /tmp/ccF4K1RU.o: relocation R_X86_64_PC32 against symbol `counter' can not be used when making a shared object; recompile with -fPIC
/usr/bin/ld: final link failed: bad value
collect2: error: ld returned 1 exit status
```

(libcounter.c we wrote as a sibling of libcalc.c: again one exported variable plus one function that reads it; the temporary file name ccF4K1RU.o is different every run.) This is an error through and through. The link failed on the spot, and no artifact ever landed. Older teaching materials often tell you this is "merely a relocation warning"; on our GCC 16.2.1, that story does not hold. What got stopped? Look at the data-fetching instruction the compiler generates without PIC:

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

What we see is a single mov fetching counter directly. By the rip-relative addressing we got to know in part 04, what it writes down is how far away the variable sits. With that distance baked in, the program has in effect asserted that counter will spend its whole life inside the module that compiled it. But it happens to be an exported symbol, and at run time another module may push it aside; what that looks like, we will actually run through in the dedicated section on symbol interposition later on. Think it over and the linker's move makes sense: it simply sealed off the direct-fetch road. R_X86_64_PC32 is not allowed on a replaceable symbol in a shared library, and the recompile with -fPIC sentence in the error is saying exactly that: change how you write it.

Two exceptions deserve a mention along the way; both linked fine, and between them they are counter-evidence that pins down what got stuck above. Give the variable a static and the link passes on the first try; a private symbol has no standing to be replaced. A library that only calls external functions and never touches an exported variable links fine too: the linker builds PLT entries for the calls (PLT is short for Procedure Linkage Table, and we are getting to it shortly). What you are more likely to meet in real engineering is the .a scenario: you take someone else's static library, compiled without -fPIC — the .a files we packed with ar back in part 06 — and want to link it into your own .so. The error that comes out is the same species as the one above, with the lead role handed to a member of the .a.

On the executable side let's glance at one more switch, -fPIE, whose full name is Position-Independent Executable: a position-independent executable. As for the pie that file reported back in part 01, today we make good on its identity:

```text
$ readelf -h app | grep Type; readelf -h app_nopie | grep Type; readelf -h libcalc.so | grep Type
  Type:                              DYN (Position-Independent Executable file)
  Type:                              EXEC (Executable file)
  Type:                              DYN (Shared object file)
```

(app_nopie is what we built with -no-pie added; the rest of the command is the same.) The difference between the two settings comes down to one question: do you have to guard against being replaced? A library takes -fPIC; its exported symbols can be pushed aside at any moment, so every cross-module reference has to leave room for that. An executable takes -fPIE; in the process it stands first in the symbol search order, its own definitions cannot be displaced by anyone else, and the only thing it needs to guard against is being moved. Look at the Type column once more: the pie program and the .so are both DYN, both built the install-anywhere way; it is the old fixed-address executables that carry EXEC.

## GOT: A Variable's Address, Fetched from a Table

Now for the main act. With total_tax's disassembly in hand, the first thing we look at is the data-fetching part:

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

Reading tax_rate takes two mov instructions; let's take them one at a time. The first is rip-relative, and the real point is the 3fd0 in the comment: that address lands inside a table called the GOT. GOT stands for Global Offset Table; the roster of this volume's sixteen pieces at the end of part 01 already reported its name. What that first mov fetches is not the value of tax_rate. What it wants is the content stored in the slot at 0x3fd0 in the table, which is tax_rate's run-time address. The second mov takes that freshly delivered address, and only then gets the true value 17. Nowhere in the code is an address baked in; the only thing baked in is how far away the GOT slot sits. The GOT lives inside the library itself, and when the library shifts as a whole, it shifts along, so the distance never changes.

Who registered the address for that slot, and who fills it in? We call up part 04's old friend, readelf -r. We filtered the output down to the names we care about; the full table holds a few extra entries the linker registered on behalf of the startup code:

```text
$ readelf -rW libcalc.so | grep -E 'tax_rate|base_tax'
0000000000003fd0  0000000500000006 R_X86_64_GLOB_DAT      0000000000004010 tax_rate + 0
0000000000004000  0000000700000007 R_X86_64_JUMP_SLOT     0000000000001109 base_tax + 0
```

The 0x3fd0 line is exactly tax_rate's registration: the type is R_X86_64_GLOB_DAT, meaning that at load time, tax_rate's real address gets filled into the slot at 0x3fd0. The relocations we read in part 04 were PC32 and PLT32, and that batch of empty slots got filled at link time. GLOB_DAT is the other half of relocation: it is reserved for the dynamic linker, and its filling happens at run time. In the file, this is the form that addresses settled at load time take.

There is a third way to fetch data, and it shows up on the executable's end; we take it in passing. main.c only calls functions, so I wrote a separate ratemain.c whose entire body is a single printf reading tax_rate directly, and the program it links into is called rateapp:

```text
$ LD_LIBRARY_PATH=. ./rateapp
rate=17
$ readelf -rW rateapp | grep COPY
0000000000004018  0000000700000005 R_X86_64_COPY          0000000000004018 tax_rate + 0
$ nm rateapp | grep tax_rate; nm libcalc.so | grep tax_rate
0000000000004018 B tax_rate
0000000000004010 D tax_rate
```

Look at the type: R_X86_64_COPY, the copy relocation. The end of part 08 flashed an R_X86_64_COPY(shared_seed) line, and this is exactly the business it was doing; the curiosity of shared_seed landing in bss gets its answer in this very section. The two nm lines are the most interesting part. The library's tax_rate is a D, living in .data, and it is where the initial value 17 comes from. In rateapp a tax_rate has appeared out of thin air, and its letter is a B; by part 04's alphabet, B is in charge of .bss. At load time the initial value gets copied wholesale by the dynamic linker into the executable's .bss copy, and the copy's authorization is exactly the COPY record. Once the copying is done, both sides use the same one; when we photograph the binding records with LD_DEBUG below, we will see that the tax_rate slot in libcalc's own GOT is filled with the address of the copy inside the executable. The executable, for its part, fetches it without ceremony: one rip-relative direct fetch that never so much as brushes the GOT. Why can it be this frugal? Back then an executable was full of absolute addressing; the linker had to have a definite address in hand, and copying one into .bss was the only choice. PIE inherited the arrangement: standing first in the search order is the executable itself, so directly fetching its own copy is actually the cheapest route. The price is that one variable now has two records in the process, and the questions that grow out of this belong to part 12 (ABI Design).

## PLT: A Function Call Takes One Extra Hop

That takes care of reading data. Now we return to that call line in total_tax. In part 01's hello.s, @PLT hung off the tail of a call operand, and at the time we said only that it had to do with how calls into dynamic libraries work: the PLT a trampoline, the GOT the table it consults for addresses. Once linking is done, it has grown into a real, physical thing. Look at main on the app side; app keeps a PLT set of its own, and here it is:

```text
$ objdump -d --no-show-raw-insn -M intel app (excerpt)
    1180:	call   1060 <base_tax@plt>
    1188:	call   1050 <total_tax@plt>
```

The call's target is 0x1060, and the label reads base_tax@plt. PLT stands for Procedure Linkage Table; in part 04 we glimpsed it from afar as the trampoline that function calls go through, and today we read its entries out one by one. Each entry is a short stretch of fixed jump code, and that is what the trade jargon calls a stub. app and libcalc each keep their own set, each serving their own call sites. The stub in libcalc looks like this, together with the special first entry in front of it:

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

The base_tax@plt stub is three instructions in all; let's read them one at a time. The leading jmp jumps to the address stored in GOT slot 0x4000. The entry that registered it is the JUMP_SLOT, and it works the same way as the GLOB_DAT: one governs functions, the other governs data. Following it, push 0x0 pushes the relocation index onto the stack, which amounts to announcing itself: base_tax, standing at index 0. The closing jmp 1020 jumps to the PLT's first entry, the one the jargon calls PLT0. PLT0 does the shared work: it pushes the GOT+8 slot, where at load time a record describing this module gets filled in (the link_map). Then it hands control to the GOT+16 slot, which holds glibc's resolver; we will call it _dl_runtime_resolve. The resolver takes the index off the stack, looks up base_tax's real address, writes the address it found back into the 0x4000 slot, and only then delivers the flow of execution into base_tax proper.

So what did the 0x4000 slot hold when it left the factory? The handful of slots the PLT uses live off on their own in a section called .got.plt, and let's dump its bytes out raw:

```text
$ readelf -x .got.plt libcalc.so

Hex dump of section '.got.plt':
 NOTE: This section has relocations against it, but these have NOT been applied to this dump.
  0x00003fe8 003e0000 00000000 00000000 00000000 .>..............
  0x00003ff8 00000000 00000000 36100000 00000000 ........6.......
```

Look at what the table holds: four entries in all. 0x3fe8 stores the address of the dynamic section (.dynamic), 0x3e00; that is the thing we were reading with readelf -d for DT_NEEDED back in part 08. 0x3ff0 and 0x3ff8 are two empty seats reserved for the dynamic linker; the link_map and the resolver both get their values only at load time, so in the file they are two zeros for now. Read the entry at 0x4000 little-endian and the value is 0x1036, which happens to be the address of the push 0x0 instruction inside the stub. That factory value at 0x4000 strings the whole thing together: in its factory state, the leading jmp travels in a circle and lands right back on the push instruction in its own stub, and from there it is push, jump into PLT0, into the resolver. Once the resolver has written the real address back into the slot, the next call goes from the leading jmp straight into base_tax; the resolving work is done exactly once in its lifetime. A factory value that circles back to its own stub before setting out: that is the whole of lazy binding's machinery.

## Lazy Binding: Resolved on the First Call

Deferring the resolution of JUMP_SLOTs until the first call is the practice known as lazy binding. GLOB_DAT gets no such treatment; it is finished off during loading. We accept no claims without evidence, so let's have the dynamic linker confess on its own record. Set the environment variable LD_DEBUG=bindings and it logs every single symbol binding. app prints its start line at the top and only then makes its first call, so the dividing line is ready-made:

```text
$ LD_DEBUG=bindings LD_LIBRARY_PATH=. stdbuf -oL ./app 2>&1 | grep -E 'base_tax|tax_rate|start|base=' | grep -v stdbuf
   1578855:	binding file ./libcalc.so [0] to ./libcalc.so [0]: normal symbol `tax_rate'
   1578855:	binding file ./app [0] to /usr/lib/libc.so.6 [0]: normal symbol `__libc_start_main' [GLIBC_2.34]
start
   1578855:	binding file ./app [0] to ./libcalc.so [0]: normal symbol `base_tax'
   1578855:	binding file ./libcalc.so [0] to ./libcalc.so [0]: normal symbol `base_tax'
base=100 total=117
```

(The digit string at the head of each line is the process ID; yours will come out different. We used stdbuf -oL to switch stdout to line buffering; without it, the stderr in the pipeline would jump the gun and the ordering would lie to us.) Watch the dividing line. The bindings of tax_rate and __libc_start_main sit ahead of start: the startup-period work was all finished by then. The two base_tax lines sit after start, and one belongs to app while the other belongs to libcalc: exactly the first call through each of the two PLT sets. Run it once more with LD_BIND_NOW=1 and all four lines move up ahead of start; the line between lazy and eager could not be plainer.

We also caught the slot's before-and-after values, and this time we bring gdb on stage. We set one breakpoint at the top of main and another where the two calls have returned. gdb disables ASLR by default, so the library's load base stays put; handy for taking readings:

```text
libcalc base = 0x7ffff7f9d000
before GOT slot = 0x7ffff7f9e036
   0x7ffff7f9e036 <base_tax@plt+6>:	push   $0x0
after  GOT slot = 0x7ffff7f9e109
   0x7ffff7f9e109 <base_tax>:	push   %rbp
```

Before the first call has happened, what we read in the slot is base plus 0x1036, pointing at the push instruction inside the stub. After the call, the slot has been swapped for base_tax proper (base plus 0x1109), matching the 0x1109 in the relocation table's Sym. Value column exactly. The stub itself has not moved a single byte; all that changed is 8 bytes in the GOT.

So is the default lazy or eager? No need to memorize anybody's claim; one look at readelf -d settles it. FLAGS carrying BIND_NOW means eager; no BIND_NOW means lazy. On my machine, the executable that GCC 16.2.1 links by default carries no such flag, so it goes lazy. When we want to choose eager on purpose, two roads are open. Add `-Wl,-z,now` at link time, and the flag goes straight into the file. Or set LD_BIND_NOW=1 before running, buying a one-off stretch of eagerness. Plenty of distributions and hardening configurations these days make eager binding the default: a little more time spent at startup buys having every resolution finished in advance and a smaller attack surface, which for server-side programs is often a good trade. Which to choose comes down to whether your own program cares more about the speed of starting up or about steadiness at run time.

## Symbol Interposition: The Library's Internal Calls Leave the Door Open

We return to the question put on hold earlier: total_tax calls the library's own base_tax, so why does that call have to go through the PLT as well? The binding records above have already let the answer show through: when libcalc resolved base_tax, it was searching for it as a global symbol. When the dynamic linker resolves a symbol, it lays out every module's export table in the process and searches from the top; whoever sits in front wins. There is no special privilege here, no library always getting to use its own people. A same-named definition in another module pushing aside the library's internal call — the trade jargon for that is symbol interposition. Let's actually run it once; our stand-in is a single line:

```c
// libfake.c — the stand-in: a same-named definition with a deliberately different return value
int base_tax(void) { return 999; }
```

```text
$ LD_PRELOAD=$PWD/libfake.so LD_LIBRARY_PATH=. ./app
start
base=999 total=1016
```

LD_PRELOAD has the dynamic linker load this library ahead of every other library. Set that against the opening's base=100 and the origin of 999 is easy to read: app's own call got replaced. What matters is 1016 = 999 + 17: the base_tax() inside total_tax also called the stand-in. A call the library wrote itself got its target swapped by an outsider, and we did not recompile a single line. And this is no accident. It is part of the machinery: precisely because the library's internal calls go through the PLT and leave that room, replacement is possible at all. Quite a few of the memory-profiling and tracing tools you will meet later on got their start by preloading their own malloc and free, and the whole method sits right here. The door staying open is convenient, but the danger is real too. The complete rules for who sits in front, and the means of narrowing the door — symbol visibility — get their dedicated treatment in part 11 (Symbol Visibility). Where a preloaded library gets found from is a one-sentence matter for part 10 (Library Search).

## The CMake Perspective

These days you seldom get to type -fPIC by hand; the build system watches that setting for us. CMake's master switch is CMAKE_POSITION_INDEPENDENT_CODE, and our minimal project looks like this:

```cmake
cmake_minimum_required(VERSION 3.20)
project(pic_demo C)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(calc STATIC libcalc.c)
add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
```

We dig the real compile commands out with --verbose and check them against what we expect (I trimmed the paths): calc's side carries -fPIC, and app's side carries -fPIE — exactly the two settings this piece has been about.

```text
… -fPIC … -o CMakeFiles/calc.dir/libcalc.c.o …
… -fPIE … -o CMakeFiles/app.dir/main.c.o …
```

When we want finer control, the POSITION_INDEPENDENT_CODE target property earns its keep: with set_target_properties we dial one target at a time, and the exceptions the global variable cannot reach are its jurisdiction. We tested the defaults too. A SHARED library carries -fPIC automatically; not one character is required from you. A STATIC library, on the other hand, goes without by default. When does that hurt? At the moment you take that default static library and link it into a .so, what gets reported is the R_X86_64_PC32 from the top of this piece; the wording matches the opening error, only the lead role changes, from counter to the tax_rate inside a .a member. So for projects whose static libraries serve as raw material for other people's .so files, turning PIC on globally is the worry-free starting configuration. As for PIE for executables, you need not give it a thought: app's link line does not even carry -pie, and the product still landed as a pie executable. Defaults are the toolchain's business, and CMake is happy to leave it at that.

## Next Up: In What Order Does the Linker Look for Libraries

Position independence has delivered install-anywhere execution: no absolute addresses are embedded in the code anymore; data exits through the GOT, functions exit through the PLT, and the dynamic linker patches the real addresses in at load time. In part 05 we watched helper receive 0x112c at link time and then get shifted wholesale at load time. After the shift, every reference still connects, and what it connects through is exactly today's machinery.

With machine code squared away, one question from the engineering side still meets you every day: when linking, in what order does the linker actually look for libraries? Does -lcalc go for libcalc.so or for libcalc.a? And at run time, where do LD_LIBRARY_PATH, the system directories, and the rest each rank? The full unfolding of those rules is exactly what the next piece, part 10 (Library Search), takes up.
