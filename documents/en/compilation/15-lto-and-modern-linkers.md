---
chapter: 13
difficulty: advanced
order: 15
platform: host
reading_time_minutes: 15
tags:
- cpp-modern
- host
- advanced
- 工具链
- 链接器
title: "LTO and Modern Linkers: Moving the Optimization Boundary to Link Time"
description: "One -flto added at compiling, and optimization moves from inside the compiler into the link stage: a scale call across two translation units keeps its call under -O2, folds into the mov $0x2b immediate under -flto -O2, and the function vanishes whole, body and symbol together; readelf dissecting a slim object — .text at zero bytes, the function's body living in a .gnu.lto_* section as intermediate representation, set against -ffat-lto-objects' fat objects; the -v output photographed live, collect2 handing over liblto_plugin as it always does, and lto1's two rounds of -fwpa and -fltrans; ar against gcc-ar with the index identical to the last character, plus the local fact that a bare-ar library links fine without -flto; -Wodr naming struct Config's two faces at link time while the inline half stays silent; the 41-file chain specimen with text shrunk from 3377 to 1437 and all 40 step functions at zero, the price of a doubled link time and the link cost incremental builds cannot save; bfd against gold really run (-print-prog-name verifying the swap, threefold timings, the LTO plugin eaten just the same), lld 22.1.8 and mold 2.42.1, the last C++ release, pulled in and really run; CMake 3.9's three IPO names and 3.29's LINKER_TYPE really run, with MOLD's no-install failure pasted as it came and one PATH addition running the link straight through; closing with nm -D proving the export side unmoved by LTO."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/15-lto-and-modern-linkers.md
  source_hash: 634298c1f2954cb5b5fa950d29ee87037870d09bd8b9b436eca1e319cbbf80e0
  translated_at: '2026-10-13T04:37:26+00:00'
  engine: anthropic
  token_count: 4700
---

# LTO and Modern Linkers: Moving the Optimization Boundary to Link Time

At the end of the last piece we handed down a rather absolute verdict: what the linker does is only the work of moving positions and joining symbols, and apart from that bit of rewriting called relocation, it does no optimization at all. Today we set about overturning it with our own hands. The means of overturning is a single switch: add -flto when compiling, and that is all. The practice's full name is Link Time Optimization — LTO in our everyday shorthand. Back in part 07, in its telling of the two faces, we previewed it in one sentence, saying it lets all the translation units meet at link time. What we watch today is exactly that meeting: from what the compiler hands over, to the product after the meeting, we walk it through from head to tail.

Why the meeting pays, let's lay one sentence of old knowledge underneath first. However clever the ordinary compiler, it sees only one translation unit at a time: when main.cpp calls a function living in util.cpp, at the moment main.cpp is compiled, what that function's body looks like is something it simply does not know; all it can do is generate a call and leave the work it cannot match up to the linker. In part 03 we compared the function bodies of -O0 and -O2; however heavy the rewriting, it all happened inside a single unit. The linker does take in all the object files at once, but optimization is precisely the thing it does not understand — its world holds nothing but symbols and addresses. LTO moves exactly this step: the compiler no longer hands over finished machine code; what it hands over is the intermediate representation — IR, as we usually say. What the linker receives has changed, and the optimization work still owed, the link stage makes up on the spot.

The environment disclosure as usual: my experiments all ran in WSL2, on the combination of GCC 16.2.1 plus binutils 2.47, with glibc at 2.44 and CMake at 4.4.3, and every stretch of output in this piece was really run. mold and lld, the two newer linkers, really ran today as well. My machine, to this day, has neither of them installed into the system: mold came as the official static package, pulled down to /tmp and unpacked for use; lld was taken from the Arch archive repository as 22.1.8, paired with the LLVM 22 this machine already has, and unpacked into /tmp the same way. In the paragraphs that follow, wherever the two of them come up, what is laid out is output that was really run. The default linker bfd, and gold — sentenced to deprecation yet still shipped in binutils 2.47 — were both really run.

## Cross-Unit Inlining: How That call Disappeared

For the specimen we use the smallest two files:

```cpp
// util.cpp: a small function, its implementation living in another translation unit
int scale(int v) { return v * 7 + 1; }
```

```cpp
// main.cpp: calls scale, which lives in util.cpp
#include <cstdio>
int scale(int);
int main() { std::printf("%d\n", scale(6)); }
```

We go the old road: compile with -O2, link, then open main up for you with objdump:

```text
$ g++ -O2 -c util.cpp -o util_o2.o && g++ -O2 -c main.cpp -o main_o2.o
$ g++ util_o2.o main_o2.o -o app_o2
$ objdump -d app_o2
    1044:	bf 06 00 00 00       	mov    $0x6,%edi
    1049:	e8 22 01 00 00       	call   1170 <_Z5scalei>
    104e:	48 8d 3d af 0f 00 00 	lea    0xfaf(%rip),%rdi
    1055:	89 c6                	mov    %eax,%esi
    1057:	31 c0                	xor    %eax,%eax
    1059:	e8 d2 ff ff ff       	call   1030 <printf@plt>
```

(The few lines at the head and the tail I trimmed.) This is the world we have watched for a dozen-plus pieces: the argument 6 loaded into %edi, the call naming _Z5scalei point-blank, and the result handed over into printf's hands. However high -O2's dial goes, it cannot rescue this one, because at the moment main.cpp is compiled, scale's function body is out of its sight. Now we hang -flto on both the compiling and the linking:

```text
$ g++ -O2 -flto -c util.cpp -o util_lto.o && g++ -O2 -flto -c main.cpp -o main_lto.o
$ g++ -O2 -flto util_lto.o main_lto.o -o app_lto
$ objdump -d app_lto
    1044:	be 2b 00 00 00       	mov    $0x2b,%esi
    1049:	48 8d 3d b4 0f 00 00 	lea    0xfb4(%rip),%rdi
    1050:	31 c0                	xor    %eax,%eax
    1052:	e8 d9 ff ff ff       	call   1030 <printf@plt>
```

(The few lines at the head and the tail I trimmed likewise.) The call is gone; printf's second argument is loaded straight up with 0x2b. Hexadecimal 2b is 43 — precisely the answer to scale(6): 6 times 7 plus 1, worked out on the spot at the link stage and folded into an immediate inside a single instruction. The finding that follows is more interesting still: search the whole disassembly of app_lto for `_Z5scalei` and not one line turns up. The function, body and symbol together, is gone entirely: inlined, it no longer has anyone referencing it. Run the program and it prints 43 all the same; semantically, not one scrap has moved.

Worth seeing clearly: neither of these two moves is one you meet for the first time. Constant folding you watched with your own eyes in part 03's -O2, and the name of inlining got its mention in 03 as well. What is actually new is the field of view: optimization used to be fenced inside a single unit; now the whole program's function bodies have entered into the same one optimization.

## What the Linker Receives Is Not Machine Code

You see, the precondition for that call being swallowed is that at the moment of linking, somebody can see the function body. We take util_lto.o, compiled with -flto, off to the dissecting table and look at what the compiler actually handed over:

```text
$ readelf -SW util_lto.o
  [ 1] .text             PROGBITS        0000000000000000 000040 000000 00  AX   0   0   1
  [12] .gnu.lto__Z5scalei.0.6703735e1136341f PROGBITS        0000000000000000 000122 0000f8 00   E  0   0   1
  [18] .gnu.lto_.opts    PROGBITS        0000000000000000 0004af 00006b 00   E  0   0   1
  [22] .symtab           SYMTAB          0000000000000000 000568 000048 18     23   2  8
```

(Between .text and .gnu.lto_.opts sit another dozen-plus sections whose names begin with .gnu.lto_; I trimmed them.) Both pieces of big news sit in the lines above. The first finding is .text's size: 000000 — not one byte of machine code. The function's body has taken up residence in a section whose name begins with .gnu.lto_; one section's very name carries `_Z5scalei` in it, and what it holds is GCC's intermediate representation (GCC's family IR goes by the name GIMPLE; its grammar we will not be learning — recognizing that it is not machine code is enough). You will find .symtab has not left either: nm still reports the 00000000 T _Z5scalei entry; only its address is 0. The symbol table is alive and well; the symbol resolution we practiced in part 05 still turns as ever, and the only thing changed is what hangs behind the names.

GNU ld on its own cannot read .gnu.lto_ sections; somebody has to do the translating in between, and the one stepping forward for the job is the plugin — not the same thing as the run-time plugins of the dlopen breed we handled in part 13. We open the link command up with -v, and the handover sits right before our eyes:

```text
$ g++ -O2 -flto util_lto.o main_lto.o -o app -v
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/collect2 -plugin /usr/lib/gcc/x86_64-pc-linux-gnu/16/liblto_plugin.so \
   -plugin-opt=/usr/lib/gcc/x86_64-pc-linux-gnu/16/lto-wrapper ...(a long tail follows, which I trimmed)
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/lto1 -quiet -dumpbase ./app.wpa ... -fwpa -fresolution=/tmp/ccdZwZ4R.res ...
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/lto1 -quiet -dumpbase ./app.ltrans0.ltrans ... -fltrans @/tmp/ccX1B1sV -o /tmp/cc6ZjdaB.s
```

(The temporary files' names I kept exactly as they came; run it yourself and the string you get will be another one — it does not get in the way of our watching.) collect2 hands liblto_plugin.so over to the linker; the plugin calls lto-wrapper out; and lto-wrapper pulls up lto1, the compiler proper, for two rounds. The first round, -fwpa, does the whole-program analysis — the verdicts on inlining are handed down in this round. The second round, -fltrans, translates the analyzed partitions back into real machine code. The -fresolution file in the middle has a story of its own: the linker writes the symbol resolution it had settled into it — which names got merged with which, which definition survived — all written out on the list, and the compiler does its work following the list. You see: of the optimization work itself the linker truly touched not one item, yet it ushered the whole optimization pipeline in through the door — and with that, the last piece's verdict stands overturned.

Object files carrying only IR have a name of their own: we call them slim objects. Carrying both is doable too — we have to add -ffat-lto-objects explicitly, and the product goes by the name fat objects. Lay the three files side by side and compare sizes: the ordinary util_o2.o is 1224 bytes, the slim util_lto.o is, of all things, 3680, and the fat util_fat.o is 3920. The slim one actually larger than the ordinary one — you read that right: the function is too small, and the registration information the IR carries with it got the upper hand. (The slim and fat byte counts, and the hexadecimal on the tail of the .gnu.lto_ section names, will drift by a few bytes from run to run — the IR registration numbers carry randomness. What does not change is the order: ordinary smallest, slim in the middle, fat the largest.) Slim, that is to say, speaks of the contents — no finished machine code in them; it is not a remark about the file's size. We open the fat object up: the .gnu.lto_ sections are all there, and .text holds another honest-to-goodness 13 bytes — real code held in reserve for settings that cannot read IR. Our volume's ordinary builds have no use for it; CMake even asks for the slim ones on our behalf, by name — you will see it with your own eyes in the CMake section.

On the static-library end there is one more stretch of joining-up. We verified in part 06 that binutils 2.47's ar writes the symbol index into a fresh archive by default, even under bare rc. Once LTO objects enter an archive, who should write the index? GCC keeps a trio of its own: gcc-ar, gcc-nm, and gcc-ranlib, doing the same work as their original names; the difference is that they enter the archive carrying the plugin, so the symbols inside the IR get counted into the index too. We really built a pair of archives out of the slim object:

```text
$ ar rc libplain.a util_lto.o && gcc-ar rc libgccar.a util_lto.o
$ nm -s libplain.a
Archive index:
_Z5scalei in util_lto.o
$ nm -s libgccar.a
Archive index:
_Z5scalei in util_lto.o
```

(The two lines listing members on each side I trimmed.) We compared the two sides, and the result is identical to the last character — no great surprise: the slim object's symbols are registered in the ELF symbol table at the same time, and the index bare ar wrote following the symbol table recorded everything that needed recording. The genuine surprise sits on the linking side: we take the archive bare ar built and link with it, with no -flto anywhere on the link command line:

```text
$ g++ -O2 main_o2.o libplain.a -o app_nolto && ./app_nolto
43
```

The link simply passed — and printed 43. Looking back at -v explains it: on this GCC 16 driver, handing -plugin to collect2 is standing practice; whether -flto sits on the link command line or not, what gets handed over is one and the same. So the claim in some older material — that a slim library will not link without -flto — does not hold on this toolchain: the plugin claimed the IR all the same and made the machine code up on the spot. We open app_nolto's disassembly: scale's function body is there, the three lines lea, sub, add — the v*8-v+1 formulation — regenerated by -O2. Where gcc-ar earns its keep is the setting where bare ar cannot see the symbols: when only the plugin can read a symbol out, the bare index will leave it unwritten. Our little specimen cannot manufacture a trickier situation; knowing where the boundary sits is enough — as for what temperaments other toolchains have, let the -v output on your own machine be the authority.

## Two Faces, and This Time -Wodr Calls It Out

The meeting brings one more by-product with it. In part 07 we verified two precedents. The kind where a type wears two faces, -Wodr can catch: it is GCC's warning switch dedicated to reporting ODR violations — ODR's full name is the One Definition Rule, the single-definition rule — and once LTO is on, it names names on the spot. The kind where an inline function carries two bodies: we hung -flto -Wall -Wextra on that one, and its performance was total silence. Rerunning the first costs little, so we take it through once as the controlled comparison. In config.h we let the macro cut out the two faces:

```cpp
// config.h (wrapped in a header guard)
struct Config {
    int limit;
#ifdef FLIP
    char tag;
#else
    long tag;
#endif
};
```

We compile one unit as usual and the other with -DFLIP carried along, and at the end we link the lot with -flto -Wall -Wextra:

```text
$ g++ -O2 -flto -Wall -Wextra old_cfg.o new_cfg.o cfg_main.o -o cfg_app
config.h:2:8: warning: type 'struct Config' violates the C++ One Definition Rule [-Wodr]
    2 | struct Config {
      |        ^
config.h:2:8: note: a different type is defined in another translation unit
    2 | struct Config {
      |        ^
config.h:7:10: note: the first difference of corresponding definitions is field 'tag'
    7 |     long tag;
      |          ^
```

(Below I trimmed two further note stretches; what they report is the same-named field, with a mismatched type, inside the other face.) These lines were spat out at the link step: we compiled each of the three .o files on its own, and every one came back with zero output; the warning appears only at the final gathering. The reasoning is not deep: ordinarily the two units never meet, and either one's Config is legal. LTO lets them bump heads, and the difference — same name, different body — finally becomes visible to a tool. We really ran the control group too: the same two faces, compiled and linked once without -flto, and its performance was silence all the same; the program ran on and printed 16 0 as usual. Of course, the standard's sentence on two-faced programs has not changed — their status is undefined behavior; today the warning caught it, which counts as our profit, and where it does not catch it, it owes us no explanation. As for the inline half, part 07 verified that it stays silent, so the line of defense does not lie in the tools: for one entity's definition, reach into one and the same header only.

## Forty Units: How Much Gets Squeezed Out

On the two-file specimen, the gain we can see is small: app_o2's text is 1505, app_lto's text is 1437. To see LTO's strength, we have to spread the cross-unit boundaries out. I generated a chained specimen: 40 files chain_i.cpp, each step_i doing a little loop and then calling the next unit's step_{i+1}, with main calling step_0 alone. Every function is called exactly once — conditions unusually friendly to inlining, and we say so up front: when you come back later to cross-examine with a real project's numbers, do not take it as the benchmark. The forty-one objects joined into one single chain; we set -O2 against -O2 -flto:

```text
$ size app_o2 app_lto
   text	   data	    bss	    dec	    hex	filename
   3377	    632	      8	   4017	    fb1	app_o2
   1437	    632	      8	   2077	    81d	app_lto
$ nm app_o2 | grep -c "T _Z[0-9]*step"
40
$ nm app_lto | grep -c "T _Z[0-9]*step"
0
```

Not one of the 40 step functions remains, and text shrank to a little over forty percent. The ordinary linker dares not touch an external function, for fear that the name is referenced by somebody else. The -fresolution list told the compiler: the product is the final executable, and the 40 names carry no reference from outside — so the compiler deleted them with an easy conscience. The wholesale vanishing of scale earlier used exactly the same mechanism. The honest words we must say in full as well: the two-file specimen's difference is 4.5%, the chained specimen's is 57%, and a real project lands somewhere between the two. LTO is worth turning on, but it cannot promise a guaranteed profit — it is where unit boundaries run dense and calls run deep that we have the most to dig.

## The Price: The Link End Gets More Expensive

The gain carries a price, and the price lands on the link end. On the same 41-file specimen we timed three rounds each with the shell's time: the ordinary link runs 0.032, 0.035, 0.032 seconds; the -flto link runs 0.067, 0.067, 0.071 seconds. Doubled — but the absolute values are millisecond-scale, and we do not exaggerate. The ones that truly need to weigh it are large projects; the gap grows together with the scale. The reason is plain enough: an ordinary link moves symbols and addresses, while an LTO link has to rerun the whole-program analysis and the code generation. LTO's code generation supports parallelism — -flto=auto hands the degree of parallelism over to the machine's core count. We really ran that three rounds too, and what we got was 0.076, 0.069, 0.067 seconds: on this specimen it earned nothing. The 41 IR files ended up cut into a single ltrans partition — the very .ltrans0.ltrans temporary from the -v output — so there was little parallel work to be handed out in the first place, while the fixed cost of starting up the parallelism got paid all the same.

The spot incremental builds cannot save is more specific still. We changed chain_5.cpp alone; recompiling it took only 0.009 seconds — the incremental saving still holds on the compile end. But the LTO link that followed took 0.063 seconds, the same price as the full one: GCC keeps no cache for LTO by default, so the 41 IR files start over from scratch at every link, and once the project grows, the link end sits heavier and heavier. So on projects with LTO on, the common practice is to turn it off in the debug builds and turn it back on for the release builds. We leave the trade-off to the CMake section, where a single switch of yours toggles it.

## Swapping Linkers: -fuse-ld and the Families

After all this talk of LTO, the linkers themselves get their turn. The ld we speak of every day — its real body is ld.bfd. bfd is the binutils family's library layer for reading ELF, and its full name is Binary File Descriptor. The same house holds one more linker born for speed, gold; outside the gate there is lld, of the LLVM house; and the newest, begun in 2020, is mold. The switch for changing linkers is -fuse-ld=name, and the driver's way of doing it is plain: look for the program under the altered name. One command verifies this for us:

```text
$ g++ -print-prog-name=ld
ld
$ g++ -fuse-ld=gold -print-prog-name=ld
ld.gold
```

gold we really ran. Its background takes one sentence: built by the binutils house to speed linking up, its headline feature is multicore parallelism, and it recognizes only a few targets of the x86 kind. The officials have already sentenced it to deprecation — by binutils 2.44, not even the source package takes it in anymore — yet my 2.47 still ships it, just right for us to run a comparison on. The specimen is still the 41 objects we have been using; three rounds each, as before: bfd's scores are 0.031, 0.032, 0.032 seconds, gold's are 0.011, 0.011, 0.012 seconds — three times as fast on the small specimen, and the millisecond-scale absolute values we report as they are. The product came out a notch smaller too: 18880 bytes from bfd lost to 14840 from gold. We carried LTO over and verified it as well: hand the 41 -flto objects to gold, it links, it runs, and the result is the same value. That is no coincidence: the linker's plugin interface was first implemented in gold, and the LTO of those days rose on its shoulders.

lld and mold really ran today as well. lld is the LLVM house's linker and the clang toolchain's default partner; -fuse-ld=lld switches us over, and CMake's LINKER_TYPE LLD does the job too. mold is the youngest of the family; its author was in his day a principal author of lld, and its goal is to push linking speed to the very fastest. Its 2.42.1, released in September 2026, is in all likelihood its last version written in C++ — the author is rewriting it into a new Rust version, with 3.0 already on the calendar. Both were put to use from where they were unpacked, in /tmp: lld came as the 22.1.8 from the archive repository, paired with the LLVM 22 this machine already has. To run them, we point the directory at PATH for the moment, and on lld's side LD_LIBRARY_PATH has to point out the libraries' search path as well — with that, they run. The specimen is still the 41 objects we have been using; we had all four run three rounds each in the same sitting, and the numbers are from that sitting: what we report is the steady state of the latter two rounds, the cold-start first round set aside — bfd's scores are 0.035 and 0.033 seconds, gold's are 0.012 and 0.013, and the steady states of mold and lld land at 0.021 and 0.022 seconds and at 0.019 and 0.018 seconds respectively. Rerun them yourself and the numbers you get will shift with how cold or warm the run is. The fastest of this sitting turned out to be gold, in fact; still, the gaps between the four all sit at the millisecond scale, and who beats whom on this specimen does not say much. Where things did pull apart is size: beyond bfd's 18880 and gold's 14840, mold laid out 13200 and lld laid out 10920 — the same 41 objects, packed tighter by the newer generation; how the sections are laid out, how the alignment is handed out, each family has its own way of arranging. The 41 LTO objects were carried over as well: both linked, both ran, and what they printed was the same value as ever, 552886381. But this is still a small specimen — the timings are millisecond-scale and the size differences amount to a few thousand bytes; it is in a real project that these gaps truly pull apart.

## The CMake Perspective

On CMake's side, LTO goes by the name IPO — Interprocedural Optimization in full — with LTO its concrete shape on GCC. The names for us to know are three, all standing since CMake 3.9: the global variable CMAKE_INTERPROCEDURAL_OPTIMIZATION, the target-side property INTERPROCEDURAL_OPTIMIZATION, and the module named CheckIPOSupported, whose check_ipo_supported() runs the check for us. The minimal project looks like this:

```cmake
cmake_minimum_required(VERSION 3.9)
project(ipodemo CXX)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_out)
if(ipo_ok)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
else()
  message(WARNING "IPO not supported: ${ipo_out}")
endif()
add_executable(app main.cpp util.cpp)
```

What check_ipo_supported does is a probe that really compiles and really links once: meet a compiler that does not acknowledge IPO, and what you get is FALSE in ipo_ok plus the reason inside ipo_out — not a build that fails halfway through. Policy CMP0069 governs the same set of behavior and arrived in the same year, 3.9; when an old project's upgrade coughs up its warning, the place to check is exactly this one. We build once, then open it up with --verbose:

```text
$ cmake -S . -B build && cmake --build build --verbose
/usr/sbin/c++ -flto=auto -fno-fat-lto-objects -MD ... -c .../main.cpp
/usr/sbin/c++ -flto=auto -fno-fat-lto-objects ... main.cpp.o util.cpp.o -o app
```

(The stretches of paths and dependency files I trimmed.) Two spots deserve your eyes: the compile line and the link line both carry -flto=auto — the -flto we typed by hand earlier, CMake filled in at both ends for us. The -fno-fat-lto-objects sitting alongside is CMake asking for slim objects by name, and it matches the fat-and-slim we dissected earlier. Picking the linker has a proper road now too: since 3.29 there is the LINKER_TYPE target property, with the variable version, CMAKE_LINKER_TYPE, governing the whole project:

```cmake
cmake_minimum_required(VERSION 3.29)
project(ldtype CXX)
add_executable(app_bfd main.cpp util.cpp)
set_property(TARGET app_bfd PROPERTY LINKER_TYPE BFD)
add_executable(app_gold main.cpp util.cpp)
set_property(TARGET app_gold PROPERTY LINKER_TYPE GOLD)
```

```text
$ cmake -S . -B build && cmake --build build --verbose
/usr/sbin/c++ -fuse-ld=bfd ... -o app_bfd
/usr/sbin/c++ -fuse-ld=gold ... -o app_gold
```

The roster of values holds BFD, GOLD, LLD, MOLD, and the like, and where it lands on the link command line, it is still the -fuse-ld we just used. I really configured a project for the MOLD setting too, really ran the build, and the failure is pasted for you as it came:

```text
$ cmake --build build --verbose
collect2: fatal error: cannot find 'ld'
/usr/sbin/c++ -fuse-ld=mold ... -o app
```

The reason is not the least bit mysterious: mold is not installed on my machine; collect2 could not find a program named ld.mold and called it a day on the spot. But without installing, we can still feed it to CMake all the same: the way is to put the unpacked mold's bin at the very front of PATH for the moment — the build directory stays the one it was, the command stays the very same line — and we run it once more, and the link goes through; this way of feeding it never needs mold installed into the system:

```text
$ PATH=/tmp/mold-run/mold-2.42.1-x86_64-linux/bin:$PATH cmake --build build --verbose
/usr/sbin/c++ -fuse-ld=mold -Wl,--dependency-file=CMakeFiles/app.dir/link.d CMakeFiles/app.dir/main.cpp.o CMakeFiles/app.dir/util.cpp.o -o app
[100%] Built target app
$ ./build/app
43
```

For the off-in-debug, on-in-release toggling, CMake has a documented way too: the variable CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE opens the Release tier alone. We configured and linked Debug and Release each on its own: the release build's link line carries -flto=auto, the debug build's carries not one -flto, and what both sides printed was 43.

## The Export Side: LTO Has Not Budged It One Notch

One more worry comes easy, and we press it down while we are here: with optimization now seeing through the whole program, will it touch the export side? By the old method from part 11, we mark helper hidden and scale_one default, then build a dynamic library with -flto -fvisibility=hidden:

```text
$ nm -D libvis.so
00000000000010f0 T _Z9scale_onei
                 w __gmon_start__
```

(The three other run-time weak hooks I trimmed.) What we worried about did not come to pass: nothing outward-facing remains in the dynamic symbol table but scale_one; the hidden helper did not show one bit of itself. However the optimizer folds things inside the library is its own business — which names get out the door is still visibility's call. The version script part 11 covered keeps working as before, and the ABI header's four-piece set part 12 established earns no exemption just because -flto is hung on.

## Next Up: The Side Story

With this piece, the main body of our volume is gathered in full. The four stages, static and dynamic libraries, symbols and visibility, the ABI, program startup, and today the moving of optimization into the link stage — we have walked one line down it all, and the linker, from a position of only carrying and moving, has taken up half a compiler's work. The one piece left is the side story, whose title the roadmap part 01 laid down at the opening planted long ago: can a dynamic library be taken as an executable and run directly? We caught a foretaste in part 08 as well — the Entry registered in libmyutil.so's header reads 0x0, which amounts to saying outright that it has no intention of running. Whether it really can be run directly, and what happens if you run it anyway — that is exactly what part 16 (the side story: can a dynamic library run as an executable?) spends its whole length playing with. See you at the side story.
