---
chapter: 13
difficulty: intermediate
order: 11
platform: host
reading_time_minutes: 26
tags:
- cpp-modern
- host
- intermediate
title: "Symbol Visibility: Controlling What a Dynamic Library Exports"
description: "The complete order in which glibc's dynamic linker resolves symbols, stood up by three rounds of matched runs: an executable's own definition yields to nobody, LD_PRELOAD steps in ahead of the dependencies, and two ordinary libraries sharing a name are settled by the NEEDED registration order. nm -D lays bare the everything-exported-by-default status quo, then three narrowing moves tested one by one: -fvisibility=hidden paired with the default attribute for point-by-point release, the difference between hidden and static checked against the symbol tables, the stand-in shut out at run time verified with the interposition experiment, -Bsymbolic binding the library's internal references at link time, and the version script roping off exported names in bulk; then the bytes saved by a slimmer export table, the .dynsym that strip cannot peel away, C++ name mangling and extern C, closing with CMake's C_VISIBILITY_PRESET."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/11-symbol-visibility.md
  source_hash: 79478cd3e0335c4bd0a045639b7532b4a99e613cff611bdaadb27a852ea59def
  translated_at: '2026-10-01T08:13:25+00:00'
  engine: glm
  token_count: 4492
---

# Symbol Visibility: Controlling What a Dynamic Library Exports

In part 09 we watched LD_PRELOAD replace even the base_tax() call made from inside libcalc, and total shifted from 117 to 1016. We promised back then that the complete rules for who sits in front, along with the means of narrowing the door, were being saved for a dedicated piece — today is that piece. Part 09 also left one more sentence behind: the door staying open is genuinely convenient, but the danger is real too. Today we make good on both ends: why the order grew into this shape, where exactly the danger walks in, and which moves a library author holds for narrowing the door until only the interface shows.

The order we are after comes down to one sentence: for a name inside a process, who actually gets the last word at run time. Can an executable's own definition be pushed aside by somebody else? On what grounds does a preloaded library always win so easily? And when two ordinary dependencies share a name, whose word counts? We will stand each of these up with experiments. The specimen is still our familiar libcalc, only this time we rewrote the source into two translation units and added two internal helpers — those two are the ones turning up over and over in the matched runs ahead.

The environment disclosure as usual: my experiments all ran in WSL2, on the combination of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, and every stretch of output in this piece was really run. If your versions differ, details like addresses and byte counts may come out differently; the mechanics themselves haven't changed in years.

## Who Sits in Front: Three Rounds of Matched Runs Establish the Complete Order

One glance at the library's skeleton tells the story: calc_api.c handles the outward-facing half, base_tax still returns 100, and once tax_rate's 17 is added on, the result is handed to the internal helpers to finish. The remaining work all falls to calc_core.c, where our two helpers live: one is near_ceil, in charge of rounding, whose job is lifting a number up to the next multiple of ten; the other is clamp_cap, in charge of capping, which intercepts everything past 500. The header holds nothing but the two interface declarations — the helpers' names never once show themselves there, and I never meant to hand them out:

```c
// calc_core.c — the library's internal pieces: two helpers, never meant for outsiders
int near_ceil(int amount) { return (amount + 9) / 10 * 10; }      /* lift to a multiple of ten */
int clamp_cap(int amount) { return amount > 500 ? 500 : amount; } /* cap anything past 500 */
```

The build commands are all old friends of yours — -fPIC and -shared are the ones we drilled in parts 08 and 09. First step: run the baseline. The first round of experiments follows right behind: when the executable carries a definition of its own, can it be pushed aside at all? What I wrote in main_def.c is exactly that, a base_tax returning 55, and we brought part 09's LD_PRELOAD back for the occasion too — the preloaded librival.so holds a base_tax of its own, returning 200.

```text
$ gcc -fPIC -shared calc_api.c calc_core.c -I. -o libcalc.so
$ gcc main.c -L. -lcalc -o app && LD_LIBRARY_PATH=. ./app
base=100 total=120
$ gcc main_def.c -L. -lcalc -o app_def && LD_LIBRARY_PATH=. ./app_def
base=55 total=80
$ nm app_def | grep base_tax
0000000000001149 T base_tax
$ LD_PRELOAD=$PWD/librival.so LD_LIBRARY_PATH=. ./app_def
base=55 total=80
```

In the baseline, 117 passed through clamp_cap unchanged and then near_ceil rounded it to 120 — every matched run from here on takes that as its reference point. base=55 is no surprise; the definition sits right in app's own hand, and that capital-T line we verified with nm above is its real body. What matters is total=80: 55 plus 17 makes 72, and near_ceil rounded it up to 80. Now it is plain: the base_tax() call inside libcalc's own total_tax also called the 55 in the executable, and the 100 the library itself wrote was never used once. The preloaded 200 never even got a turn at being used: the executable cannot be pushed aside — not even by a preloaded library.

The second round of experiments turns to two ordinary dependencies sharing a name. Besides base_tax, librival.so also holds a rival_note that only it has; main_rival.c puts both functions to use, so the linker can spare neither. From the same main_rival.c source we linked one copy in each of two orders, then checked the registration with readelf -d, which we met in part 08; at the end we tag on the third round: preloaded librival taking a run at the head of the NEEDED list:

```text
$ gcc main_rival.c -L. -lcalc -lrival -o app_calc_first
$ gcc main_rival.c -L. -lrival -lcalc -o app_rival_first
$ LD_LIBRARY_PATH=. ./app_calc_first
base=100 note=7
$ LD_LIBRARY_PATH=. ./app_rival_first
base=200 note=7
$ readelf -d app_calc_first | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so]
 0x0000000000000001 (NEEDED)             Shared library: [librival.so]
$ readelf -d app_rival_first | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [librival.so]
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so]
$ LD_PRELOAD=$PWD/librival.so LD_LIBRARY_PATH=. ./app_calc_first
base=200 note=7
```

The winner switched with the order of the link line: whoever sits in front in NEEDED is the one that wins (both sides also carry a third NEEDED, libc.so.6 — we cut it out). And libcalc, the head of the NEEDED list, still lost to the preloaded librival; the part 09 show in which base_tax got replaced plays just the same on today's specimen.

Put the three rounds together and the complete order stands. When glibc's dynamic linker resolves any symbol, it goes asking through the process's modules from the top: the executable first, then the libraries preloaded through LD_PRELOAD, and only after that the dependencies in the NEEDED list, in their registered order. The first module to offer a definition wins; same-named definitions sitting further back never even get asked. That also digs out the root of the part 09 phenomenon: the library's internal calls could be replaced because the lookup walks a process-wide global order, and the dynamic linker does not know anybody as "the library's own people". What we have described is glibc's way of doing it; other platforms may not order things the same way, though the executable-not-being-pushed-aside part is commonly implemented this way.

The danger now has a concrete shape. Picture it: a program links two third-party libraries, each carrying an internal helper it never meant to export, and the two happen to share a name. The library sitting further back quietly pushes aside the other library's internal calls, the linker waves it through without a word, and the run-time behavior just changed. A problem like this neither crashes nor makes noise — all it shows is numbers that don't add up, and tracking it down can eat an afternoon of yours. The countermeasure is already in our hands: shorten the library's export list. Three moves, and we take them one at a time.

## Everything Exported by Default: What You Never Meant to Give, You Gave Anyway

Before laying a hand on any narrowing, we need to size up the status quo, and the tool for that is nm -D. What it reads is the dynamic symbol table, .dynsym — it put in an appearance back in part 08, when we compared sections with size -A. It also carries a plainer identity: the library's export table. Once a name lands in there, every module in the process can see it, and linking against it is a one-line affair. Here is libcalc.so:

```text
$ nm -D libcalc.so
0000000000001129 T base_tax
0000000000001190 T clamp_cap
0000000000001162 T near_ceil
0000000000004020 D tax_rate
0000000000001134 T total_tax
```

The output actually held four more lines carrying w, weak references — miscellaneous items registered by the startup code, nothing to do with our subject, and we cut them. The five lines that remain are what we came for: base_tax and total_tax are the interfaces, and exporting them is exactly their job. But near_ceil and clamp_cap are internal helpers I tucked into a .c file; you could comb the header end to end and never find them — and there they sit in the export table, right alongside tax_rate, neatly lined up with full addresses and capital letters. If this is your first look at this sight, you will probably pause the way I did at the time: a symbol that never made it into a header file can still get exported.

gcc's default visibility is default: an unadorned global symbol is public without exception, so the helper functions and the module-level global variable all travel the public road. Every exported name is an outward commitment — a caller can put it to use with a single extern declaration, and if you want to rename it or delete it, you had better weigh the dependencies sitting behind it first. More names, and the odds of a collision climb with them; the afternoon-eating trouble from the previous section starts right here.

## Narrowing Move One: -fvisibility=hidden, Tucked In by Default, Released Point by Point

The first move is the compiler switch -fvisibility=hidden. Visibility is the very English word sitting in this piece's title, and what it talks about is whether a symbol is allowed to be seen across module boundaries. default and hidden are its two settings: default is the public one, hidden is the concealed one, and this command-line switch governs the whole library's default setting. We rebuild the same source with it added, and you go ahead and try linking the caller:

```text
$ gcc -fvisibility=hidden -fPIC -shared calc_api.c calc_core.c -I. -o libcalc_hidden.so
$ gcc main.c ./libcalc_hidden.so -o app_hidden
/usr/bin/ld: /tmp/ccfzrPfR.o: in function `main':
main.c:(.text+0xa): undefined reference to `total_tax'
/usr/bin/ld: main.c:(.text+0x11): undefined reference to `base_tax'
collect2: error: ld returned 1 exit status
```

We verified with nm -D: of the five names not one remains, all of them left the export table, and the linking turned hostile on the spot. You made acquaintance with undefined reference back in part 05, but this time the definition is plainly in the library's own hands — on what grounds is it still reported? Because when the static linker resolves symbols from a .so, what it looks at is likewise the export table. hidden struck the names off .dynsym's rolls, which amounts to shutting the door on both linkers at once: the static one cannot find them, and neither can the dynamic one. So a blanket hidden on everything will not do; a library must leave a door open for its own interface. The way to leave that door is to hang a visibility attribute on the interface declarations, its value dialed back to default, which puts the two interface functions explicitly out in the open. The header became what follows, and not one character of calc_core.c needs to move:

```c
/* calc.h — with narrowing as the default, the interfaces go public explicitly through the attribute */
#define CALC_API __attribute__((visibility("default")))

CALC_API int base_tax(void);
CALC_API int total_tax(void);
```

We rebuild and compare once more:

```text
$ gcc -fvisibility=hidden -fPIC -shared calc_api.c calc_core.c -I. -o libcalc_hidden_api.so
$ nm -D libcalc_hidden_api.so | grep -v ' w '
0000000000001109 T base_tax
0000000000001114 T total_tax
$ ./app_hidden_api
base=100 total=120
```

Only the two interfaces remain in the export table; the helpers have all stepped off the stage, and the program still runs perfectly well, base and total hugging the baseline to the byte. Those CALC_API and MYLIB_EXPORT macros we keep running into — their real body is a pair of partners: the global default tuck-in comes from -fvisibility=hidden, and the point-by-point release comes from the attribute in the header. Windows' __declspec(dllexport) is doing the same job, and CMake has a way of unifying the two ends — we will get there in the CMake perspective.

## hidden Is Not static: Private to the Whole Library, Usable Across Translation Units

Reading this far, you may be turning it over: isn't this just static? In part 05 we covered static's internal linkage — a name's valid range is only its own translation unit, and it never enters the linker's global adjudication — so no, we really cannot treat this as static. The difference grows right on our specimen: near_ceil lives in calc_core.c, while total_tax, the one calling it, lives in calc_api.c — across translation units. Make near_ceil static, and the call in calc_api.c immediately loses its footing. I tested it: a .so by default tolerates undefined symbols all the way to run time, and only with `-Wl,-z,defs` does it report on the spot the undefined reference we saw in part 05, with near_ceil cast in the lead role. hidden suffers no such restriction; the private range it draws is the whole library: while the .so is being linked, the helper still takes part in the library-internal resolution as a global symbol — it is only the finished product's outward export table that no longer carries its name.

Let's see it with our own eyes: in the .o, the helper is GLOBAL paired with HIDDEN — the binding is global, the visibility hidden. Once linked into a .so, they become LOCAL, and they still live in the ordinary symbol table .symtab (that is what nm reads without -D; a lowercase t is their mark) — only no longer in .dynsym. clamp_cap's two lines are identical, so we set out near_ceil only:

```text
$ gcc -fvisibility=hidden -fPIC -c calc_core.c -o core_hidden.o
$ readelf -sW core_hidden.o | grep near_ceil
     3: 0000000000000000    46 FUNC    GLOBAL HIDDEN     1 near_ceil
$ readelf -sW libcalc_hidden_api.so | grep near_ceil
    15: 000000000000113f    46 FUNC    LOCAL  DEFAULT   11 near_ceil
```

The privatization also rewrote the library's internal call sites along the way. From the disassembly of the two versions' total_tax, we picked out the call lines and the data fetches and laid them open:

```text
$ objdump -d --no-show-raw-insn -M intel libcalc.so        # default version, excerpt
    115b:	call   1040 <near_ceil@plt>
$ objdump -d --no-show-raw-insn -M intel libcalc_hidden_api.so   # hidden version, excerpt
    1121:	mov    edx,DWORD PTR [rip+0x2ee9]        # 4010 <tax_rate>
    1138:	call   113f <near_ceil>
```

In the default version the helpers are exported symbols that somebody might push aside, so the internal calls go through the PLT by the very machinery part 09 laid out. In the hidden version the compiler knows they cannot go anywhere: the call's spelling switches to direct relative addressing, and even the tax_rate fetch saves itself the hop through the GOT, because it went hidden along with everything else. The interesting one is base_tax — in both versions its call reads base_tax@plt: the interface stays public, so the internal call has to keep leaving room for a stand-in. To see what the spelling difference amounts to at run time, we run part 09's interposition experiment once more. Interposition means exactly this: a same-named definition in some other module pushing aside a call made from inside the library. This time the pushed-aside party changes from part 09's base_tax to our helper near_ceil, and the stand-in libfakeceil.so defines a same-named near_ceil with its return value changed to add 1000, preloaded onto each of the two versions in turn:

```text
$ LD_PRELOAD=$PWD/libfakeceil.so LD_LIBRARY_PATH=. ./app
base=100 total=1117
$ LD_PRELOAD=$PWD/libfakeceil.so ./app_hidden_api
base=100 total=120
```

The default version got pushed aside good and proper: 120 turned into 1117. The hidden version never budged — in a direct call there is no seat left for a stand-in. The means of narrowing the door that part 09 spoke of: you have now seen its real body.

## Narrowing Move Two: -Bsymbolic, Settling the Library's Internal Bindings at Link Time

The second move touches neither source nor compile step — it is an instruction handed to the linker, `-Wl,-Bsymbolic`. It governs exactly one thing: wherever the library references its own exported symbols, the binding to its own definitions is finished off at link time, no longer left for the dynamic linker to do. We take the all-default source, add it, and link one more copy:

```text
$ gcc -fPIC -shared -Wl,-Bsymbolic calc_api.c calc_core.c -I. -o libcalc_bsym.so
$ nm -D libcalc_bsym.so | grep -E ' T | D '
00000000000010e9 T base_tax
0000000000001150 T clamp_cap
0000000000001122 T near_ceil
0000000000004008 D tax_rate
00000000000010f4 T total_tax
$ LD_PRELOAD=$PWD/libfakeceil.so ./app_bsym
base=100 total=120
```

Set this beside the previous section and it gets interesting: the export table still holds a full five names, we hid none of them, and the stand-in was shut out all the same. The machinery is written right into the relocation table. The default version kept four dynamic relocations for our four symbols — tax_rate occupies one GLOB_DAT, the three functions one JUMP_SLOT each — the types we read in part 09. In the bsym version not one entry remains: the linker filled the GOT slots at link time, and by run time there is no work left for the dynamic linker:

```text
$ readelf -rW libcalc.so | grep -E 'base_tax|tax_rate|near_ceil|clamp_cap'
0000000000003fd0  0000000700000006 R_X86_64_GLOB_DAT      0000000000004020 tax_rate + 0
0000000000004000  0000000900000007 R_X86_64_JUMP_SLOT     0000000000001129 base_tax + 0
0000000000004008  0000000500000007 R_X86_64_JUMP_SLOT     0000000000001162 near_ceil + 0
0000000000004010  0000000600000007 R_X86_64_JUMP_SLOT     0000000000001190 clamp_cap + 0
$ readelf -rW libcalc_bsym.so | grep -E 'base_tax|tax_rate|near_ceil|clamp_cap'
(no output, not one entry left)
$ readelf -d libcalc_bsym.so | grep FLAGS
 0x000000000000001e (FLAGS)              SYMBOLIC
```

That SYMBOLIC in FLAGS is -Bsymbolic's mark as it lands in the file. How it divides the labor with hidden is worth laying out open and memorizing: hidden changes visibility at compile time, and treats the ailment of "names handed out that shouldn't have been"; -Bsymbolic changes binding at link time — the export table stays exactly as it was — and treats "names handed out whose binding got snatched away by somebody else". There is a price too: it cuts off, in one stroke, the road by which preload-based profiling and tracing tools replace library functions from the outside, so the tools that got their start preloading their own malloc, as part 09 told it, run out of luck against a library carrying -Bsymbolic. Hence some care when a C++ library uses it: exception matching and type identification lean on certain symbols being globally unique, and the more common choice is the narrowed -Bsymbolic-functions, which binds functions only and lets pass the names that must stay globally consistent.

## Narrowing Move Three: The Version Script, Roping Off Names in Bulk Without Touching the Source

The third move is handed to a linker script: the version script, which we write in a map file — a handful of lines is the whole of it: global lets the two interfaces through, the asterisk under local takes away every other name, and our source does not have to move a single character:

```text
$ cat calc.map
{
  global:
    base_tax;
    total_tax;
  local:
    *;
};
$ gcc -fPIC -shared -Wl,--version-script=calc.map calc_api.c calc_core.c -I. -o libcalc_map.so
$ nm -D libcalc_map.so | grep -v ' w '
0000000000001109 T base_tax
0000000000001114 T total_tax
```

The result is as clean as the hidden version's. Where it comes in handy is narrowing in bulk: building a library out of somebody else's whole source tree, where scattering attributes everywhere is awkward — one map file finishes the job. The spelling we use here is the anonymous one: the version node (the block that pair of braces wraps inside the map) goes unnamed, and the exported symbol names come out clean. Name the node, and the symbol names sprout a @@version suffix on the back, turning into versioned symbols — that is the business of managing an interface's generations, the territory of part 12 (ABI Design) as a whole, and we will not take one step past it here.

## How Much Gets Exported: The Verdict in Bytes

Names taken in — how many bytes saved? We compare sections with size -A, the same move we used in part 08:

```text
$ size -A libcalc.so | grep -E '\.dynsym|\.dynstr'
.dynsym               240     776
.dynstr               155    1016
$ size -A libcalc_hidden_api.so | grep -E '\.dynsym|\.dynstr'
.dynsym               168     760
.dynstr               126     928
```

Three fewer names exported, and .dynsym dropped from 240 to 168. On a 64-bit ELF one symbol record is 24 bytes; three of them come to exactly seventy-two — not a byte off. .dynstr is the string table for symbol names; the three names plus their terminating null characters total 29 bytes, and the drop from 155 to 126 matches it exactly. Our library holds five names all told, so the savings are naturally small — but the direction is real: the more you export, the larger the .dynsym and .dynstr sections grow, the more dynamic relocation records there are, and the more resolving the dynamic linker has on its plate at startup.

The heavier burden lands on maintenance: the less you export, the freer your hand to change things later. The way to weigh that trade-off gets its own unfolding in part 12 (ABI Design).

One more question while we are on size: strip, which we met in part 04, can peel away the human-facing .symtab, but it cannot peel away the .dynsym that run time needs. We tested it: after strip, nm -D still counts out our five names in the library, while for the ordinary symbol table even nm says outright no symbols. What .symtab loses is debug information only; .dynsym is the table run-time resolution cannot do without, and strip has to leave that one for us.

## The Exported Names: C++ Mangling and extern C

With the list cleaned up, one question you are bound to run into sooner or later remains: what do the exported names themselves look like? In part 03 we read through the workings of name mangling: C++ encodes namespaces and parameter types wholesale into the symbol name — that is how the linker tells the overloads apart. For what that looks like on an export table, here is a three-line specimen in C++: two overloads of discount, plus a refund exported the C way:

```cpp
// calcpp.cpp — one discount, two signatures
int discount(int percent) { return percent / 2; }
double discount(double ratio) { return ratio * 100.0; }

extern "C" int refund(int amount) { return amount / 10; }
```

```text
$ g++ -fPIC -shared calcpp.cpp -o libcalcpp.so
$ nm -D libcalcpp.so | grep -E 'discount|refund'
00000000000010fe T _Z8discountd
00000000000010e9 T _Z8discounti
000000000000111a T refund
```

Both discounts made it into the export table, their names mangled strings headed by _Z; run them through c++filt (whose whole job is decoding mangled names back into their original shape), and the one ending in i is discount(int), the one ending in d is discount(double). refund, wearing extern "C", keeps its clean bare name. Mangled names carry the overloads, but they are loyal to their compiler: g++ and clang++ follow the same rules, while MSVC runs a different scheme altogether. Wherever names must be recognized across compilers or across languages, the extern "C" declaration is what does the carrying — the price being that no overload can ever live under its name again. How to settle the names on a cross-toolchain interface, and how dlsym goes about fetching functions by name — those are what part 13 (Runtime Loading and Plugins) and part 12 come back to finish; here we only need to know them on sight.

## The CMake Perspective

The chore of typing -fvisibility=hidden by hand is, in CMake, a ready-made target property: C_VISIBILITY_PRESET; on C++ targets the same knob is called CXX_VISIBILITY_PRESET. Here is the project — the header is the version already carrying CALC_API:

```cmake
cmake_minimum_required(VERSION 3.20)
project(calc_vis C)

add_library(calc SHARED calc_api.c calc_core.c)
set_target_properties(calc PROPERTIES C_VISIBILITY_PRESET hidden)

add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
```

We check with --verbose; the real compile line reads:

```text
$ cmake -B build -S . && cmake --build build --verbose
/usr/sbin/cc -Dcalc_EXPORTS  -fPIC -fvisibility=hidden -MD -MT CMakeFiles/calc.dir/calc_api.c.o -MF CMakeFiles/calc.dir/calc_api.c.o.d -o CMakeFiles/calc.dir/calc_api.c.o -c /tmp/calc_vis/calc_api.c
```

-fvisibility=hidden went into every compile line; I trimmed the link line. The -Dcalc_EXPORTS sitting alongside is a macro CMake defines in passing — its original purpose is serving Windows export headers, and our Linux side has no use for it. We verified the product's export table too: it came out of the very same mold as the hand-typed version — what nm -D shows left is exactly the two interfaces, and the program prints base=100 total=120. The division of labor is settled: the target property takes care of the global default narrowing, the macro in the header takes care of the point-by-point release — one pair, exactly the machinery "Narrowing Move One" laid out. The same family holds one more property, VISIBILITY_INLINES_HIDDEN; one sentence for it: it tucks the export of inline functions away too, and fitting it onto a C++ target saves a little more. For one macro that works on both the Linux and the Windows end, go on to CMake's GenerateExportHeader: the header it generates expands into the visibility attribute on Linux, and into dllexport and dllimport on Windows — but that is another patch of ground.

## Next Up: Managing the Interface Itself

Where we stand, both ends of the opening part 09 left are now plugged: on the search order you hold the complete evidence of three rounds of experiments, and each of the three narrowing moves has been put to a real test. But a cleaned-up export table only settles how much gets given and to whom — the interface itself is still scattered: how parameters get passed, how a struct's fields get laid out, how old programs keep on working once the version moves up. These outward commitments taken together have a name — ABI — and that is exactly what our next piece, part 12 (ABI Design), spends its whole length on: taking the interface seriously as a product and designing it properly.
