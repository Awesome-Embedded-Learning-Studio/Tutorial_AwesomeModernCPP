---
chapter: 13
difficulty: intermediate
order: 11
platform: host
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "Symbol Visibility: Controlling What a Dynamic Library Exports"
description: "glibc's complete dynamic symbol resolution order, pinned down by three experiments: a definition the executable carries itself cannot be overridden by anyone, LD_PRELOAD cuts ahead of the dependencies, and when two ordinary libraries collide on a name the NEEDED registration order decides. nm -D exposes the export-everything status quo, and three narrowing moves get measured one by one: -fvisibility=hidden with a default attribute opening single points, the hidden-versus-static difference checked against the symbol tables, the override-blocking effect checked with the interception experiment, -Bsymbolic settling intra-library bindings at link time, version scripts selecting export names in bulk, then the bytes a trimmed export table saves, the .dynsym that strip cannot remove, C++ name mangling and extern C, closing with CMake's C_VISIBILITY_PRESET."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/11-symbol-visibility.md
  source_hash: 1d24b4b4d70ff7116f3753c07e0901b865ecf5ed92b02457d83884cecca013e5
  translated_at: '2026-10-13T04:17:57+00:00'
  engine: anthropic
  token_count: 12000
---

# Symbol Visibility: Controlling What a Dynamic Library Exports

Back in 09 we watched LD_PRELOAD override even the internal base_tax() call inside libcalc, and total changed from 117 to 1016. At the time we promised that the full resolution order — who gets asked first — and the tools for narrowing that opening would get a piece of their own today. That piece also left a warning: while the opening is wide open it is convenient, but the danger is real. Today we make good on both ends: why the order grew the way it did, where the danger creeps in from, and what levers a library author holds to narrow the opening until only the interface shows.

The order we are after boils down to one question: when a name in the process is looked up at run time, who gets the final say. Does the executable's own definition get overridden? Why does a preloaded library always win so easily? When two ordinary dependencies collide on a name, whose definition prevails? We settle each of these with experiments. The specimen is still our familiar libcalc, only this time we rewrote its source as two translation units and added two internal helpers — the next several comparisons keep coming back to those two.

The environment disclosure, as always: my experiments all ran in WSL2, with the combination of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, and every stretch of output in this piece was genuinely run. If your versions differ, details like addresses and byte counts may drift; the mechanics themselves haven't budged in years.

## Who Gets Asked First: Three Experiments Pin Down the Complete Order

One glance at the library's skeleton tells the story: calc_api.c owns the outward-facing half — base_tax still returns 100, tax_rate's 17 gets added on, and the result is handed to the internal helpers to finish. The remaining work all lives in calc_core.c, and the two helpers live there with it: one is near_ceil, in charge of rounding, lifting a number up to the nearest ten; the other is clamp_cap, in charge of capping, cutting everything above 500 back down. The header declares only the two interface functions; the helpers' names never show up even once, and I never meant to hand them out:

```c
// calc_core.c — the library's internals: two helpers, never meant for outsiders
int near_ceil(int amount) { return (amount + 9) / 10 * 10; }      /* round up to tens */
int clamp_cap(int amount) { return amount > 500 ? 500 : amount; } /* cap anything above 500 */
```

You know the build commands by heart — we drilled -fPIC and -shared in 08's dynamic libraries overview and again in 09. First we run the baseline out, and the first experiment follows right behind: the executable carries a definition of its own — can it be overridden? In main_def.c I wrote a base_tax that returns 55, and we brought back LD_PRELOAD from 09: the preloaded librival.so also has a base_tax, returning 200.

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

In the baseline, 117 passes through clamp_cap untouched and near_ceil rounds it up to 120; take that as the reference for every comparison that follows. base=55 is no surprise — the definition sits in the app's own hands, and that capital T line nm turned up is exactly its true body. What matters is total=80: 55 plus 17 is 72, and near_ceil rounds it to 80. Now it is clear: the base_tax() call inside total_tax in libcalc also picked up the executable's 55, and the library's own 100 never got used once. The preloaded 200 never got a single turn either: the executable cannot be overridden — not even by a preloaded library.

The second experiment is up: two ordinary dependencies colliding on a name. Besides base_tax, librival.so carries a rival_note that only it has, and main_rival.c uses both functions, so the linker cannot skip either one. From the same main_rival.c source we link one binary per link order, check the registrations with the readelf -d we met in 08, and tack a third round onto the end: the preloaded librival taking on the head of the NEEDED list:

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

The winner flipped with the link line's order: whoever sat first in NEEDED won (both sides also carry a third NEEDED, libc.so.6, which we trimmed). The head of the NEEDED list, libcalc, still lost to the preloaded librival — the base_tax override from 09 plays out just the same on today's specimen.

Put the three rounds together and the complete order stands. When glibc's dynamic linker resolves any symbol, it walks the process's modules from the front: the executable first, then the libraries preloaded through LD_PRELOAD, then the dependencies on the NEEDED list in registration order. The first module offering a definition wins; same-named definitions further back never even get asked. That also uncovers the root of the phenomenon from 09: a call made from inside a library can be overridden because the lookup follows the process-wide global order — the dynamic linker has no notion of "the library's own people". What we describe is glibc's way; other platforms may order things differently, though the never-override-the-executable part is near-universal practice.

And so the danger takes concrete shape. Picture this: a program links two third-party libraries, each with an internal helper that was never meant to be exported, and the two happen to share a name. The library further back silently overrides the first library's internal call — the linker waves it through without a word — and run-time behavior changes just like that. The problem neither crashes nor complains; the only symptom is numbers that refuse to add up, and chasing it down can eat an afternoon of yours. The countermeasure we already have on the table: shorten the list of what the library exports, in three moves, one at a time.

## Everything Exports by Default: What You Never Meant to Give, You Have Already Given

Before we start narrowing, we need a clear picture of the status quo, and the tool is nm -D. It reads the dynamic symbol table, .dynsym — the same section that showed its face in 08 when we compared sections with size -A. It has a plainer identity too: the library's export table. Any name that lands in it is visible to every module in the process, and linking against it is a one-line affair. Here is libcalc.so:

```text
$ nm -D libcalc.so
0000000000001129 T base_tax
0000000000001190 T clamp_cap
0000000000001162 T near_ceil
0000000000004020 D tax_rate
0000000000001134 T total_tax
```

The output actually has four more lines of weak references marked w — housekeeping registered by the startup code, irrelevant to our topic, so we trimmed them. The five lines left are what to look at: base_tax and total_tax are the interface, and exporting them is their job. But near_ceil and clamp_cap are internal helpers I tucked into the .c file — search the header all you like, you will not find them — and they lie in the export table just as neatly, complete with addresses and capital letters, alongside tax_rate. If this is your first look, you probably freeze for a beat, the way I did: symbols never written into the header can still be exported.

gcc's default visibility is default: unadorned global symbols go public across the board, and helper functions and module-level global variables all travel the public road. Every exported name is an outward promise: a caller can put an extern declaration in one line and start using it, and renaming or deleting it means weighing the dependents first. More names means the odds of a collision climb with them — the trouble from the last section that can eat an afternoon of yours starts right here.

## Move One: -fvisibility=hidden — Tucked In by Default, Opened at Single Points

The first move is the compiler switch -fvisibility=hidden. Visibility is the very word in this piece's title: it says whether a symbol may be seen across modules. default and hidden are its two settings — default is the public one, hidden the concealed one — and this command-line switch sets the whole library's default. We rebuild the same source with it, and you try to link a caller against it:

```text
$ gcc -fvisibility=hidden -fPIC -shared calc_api.c calc_core.c -I. -o libcalc_hidden.so
$ gcc main.c ./libcalc_hidden.so -o app_hidden
/usr/bin/ld: /tmp/ccfzrPfR.o: in function `main':
main.c:(.text+0xa): undefined reference to `total_tax'
/usr/bin/ld: main.c:(.text+0x11): undefined reference to `base_tax'
collect2: error: ld returned 1 exit status
```

We verified with nm -D: not one of the five names is left — they are all out of the export table, and the link turned on us on the spot. You met undefined reference back in the linker piece, 05, but this time the definition is right there in the library's hands — why the complaint? Because when the static linker resolves symbols against a .so, it reads the same export table. hidden struck the names off .dynsym, which slams the door on both linkers at once: the static one cannot find them, and neither can the dynamic one. So hiding everything across the board does not fly — a library has to leave a door for its own interface. The way to leave the door is a visibility attribute hanging on the interface declarations, set back to default, and the two interface functions become explicitly public. The header changes to the following, and the calc_core.c side does not need to change by a single character:

```c
/* calc.h — with the default narrowed, the interface goes public via the attribute */
#define CALC_API __attribute__((visibility("default")))

CALC_API int base_tax(void);
CALC_API int total_tax(void);
```

We rebuild and compare:

```text
$ gcc -fvisibility=hidden -fPIC -shared calc_api.c calc_core.c -I. -o libcalc_hidden_api.so
$ nm -D libcalc_hidden_api.so | grep -v ' w '
0000000000001109 T base_tax
0000000000001114 T total_tax
$ ./app_hidden_api
base=100 total=120
```

The export table is down to the two interfaces, the helpers have all stepped off, the program runs perfectly, and base and total hug the baseline without a hair of difference. Those CALC_API and MYLIB_EXPORT macros we keep running into are, at their core, this pair working together: the global default gets tucked in by -fvisibility=hidden, and the single-point opening lives in the attribute in the header. On the Windows side, __declspec(dllexport) does the same job, and CMake has a way to unify both ends — we will see it in the CMake section.

## hidden Is Not static: Private to the Whole Library, Usable Across Translation Units

Reading this far you may be turning it over in your head: is this not just static? In 05 we covered static's internal linkage — a name whose reach is only its own translation unit, never entering the linker's global adjudication — and we really cannot treat hidden as static. The difference lives right on our specimen: near_ceil resides in calc_core.c while total_tax, its caller, resides in calc_api.c — across translation units. Change near_ceil to static, and that call in calc_api.c immediately loses its footing. I measured it: a .so by default is allowed to leave symbols undefined until run time, and only with `-Wl,-z,defs` does it report on the spot the undefined reference we saw in 05, with near_ceil in the lead role this time. hidden carries no such restriction: the private zone it draws is the whole library. When the .so links, the helpers still take part in intra-library resolution as global symbols; it is only that the finished product's export table no longer carries their names.

Seeing is believing: in the .o, the helper is GLOBAL paired with HIDDEN — global binding, hidden visibility. Once linked into the .so, it becomes LOCAL, still living in the ordinary symbol table .symtab (the one nm reads without -D; the lowercase t entries are them), just no longer in .dynsym. clamp_cap's two lines are identical, so we lay out only near_ceil:

```text
$ gcc -fvisibility=hidden -fPIC -c calc_core.c -o core_hidden.o
$ readelf -sW core_hidden.o | grep near_ceil
     3: 0000000000000000    46 FUNC    GLOBAL HIDDEN     1 near_ceil
$ readelf -sW libcalc_hidden_api.so | grep near_ceil
    15: 000000000000113f    46 FUNC    LOCAL  DEFAULT   11 near_ceil
```

Privatizing also quietly changes how the library's internal calls get written. From the two versions' total_tax disassembly we picked out the call and data-fetch lines and laid them side by side:

```text
$ objdump -d --no-show-raw-insn -M intel libcalc.so        # default version, excerpt
    115b:	call   1040 <near_ceil@plt>
$ objdump -d --no-show-raw-insn -M intel libcalc_hidden_api.so   # hidden version, excerpt
    1121:	mov    edx,DWORD PTR [rip+0x2ee9]        # 4010 <tax_rate>
    1138:	call   113f <near_ceil>
```

In the default version the helpers are exported symbols that can be overridden, so internal calls go through the PLT by the mechanism 09 walked through. In the hidden version the compiler knows they cannot go anywhere, and the call turns into direct relative addressing — even the tax_rate fetch skips the hop through the GOT, because it got hidden along with the rest. The interesting one is base_tax: in both versions its call is base_tax@plt. The interface stays public, so internal calls to it have to keep leaving room for override. To see what this difference in codegen means at run time, we rerun the interception experiment from 09. Interception means a same-named definition in another module overriding a call made from inside the library; this time the target shifts from that piece's base_tax to the helper near_ceil, the interceptor libfakeceil.so defines a same-named near_ceil whose return value is bumped up by 1000, and we preload it onto each version in turn:

```text
$ LD_PRELOAD=$PWD/libfakeceil.so LD_LIBRARY_PATH=. ./app
base=100 total=1117
$ LD_PRELOAD=$PWD/libfakeceil.so ./app_hidden_api
base=100 total=120
```

The default version gets overridden good and hard: 120 becomes 1117. The hidden version does not budge — in a direct call there is nowhere for an interceptor to sit. The narrowing move 09 promised: here is its true body.

## Move Two: -Bsymbolic — Settling Intra-Library Bindings at Link Time

The second move touches neither source nor compilation — it is an instruction to the linker, `-Wl,-Bsymbolic`. It governs exactly one thing: wherever the library references its own exported symbols, the binding is settled onto the library's own definition at link time and no longer left for the dynamic linker to handle. We take the all-default source, add it, and link another copy:

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

Setting this next to the previous section is where it gets interesting: the export table is still the full five names — we hid not one of them — yet the interceptor is still shut out. The mechanism is written in the relocation table. The default version left four dynamic relocations for our four symbols: one GLOB_DAT for tax_rate and one JUMP_SLOT each for the three functions, the very types we read in 09. The bsym version's table has not one left — the linker filled the GOT slots at link time, and when run time arrives the dynamic linker has no work left to do:

```text
$ readelf -rW libcalc.so | grep -E 'base_tax|tax_rate|near_ceil|clamp_cap'
0000000000003fd0  0000000700000006 R_X86_64_GLOB_DAT      0000000000004020 tax_rate + 0
0000000000004000  0000000900000007 R_X86_64_JUMP_SLOT     0000000000001129 base_tax + 0
0000000000004008  0000000500000007 R_X86_64_JUMP_SLOT     0000000000001162 near_ceil + 0
0000000000004010  0000000600000007 R_X86_64_JUMP_SLOT     0000000000001190 clamp_cap + 0
$ readelf -rW libcalc_bsym.so | grep -E 'base_tax|tax_rate|near_ceil|clamp_cap'
(no output, not one line left)
$ readelf -d libcalc_bsym.so | grep FLAGS
 0x000000000000001e (FLAGS)              SYMBOLIC
```

That SYMBOLIC in FLAGS is -Bsymbolic's mark left in the file. Its division of labor with hidden deserves to be laid out side by side and remembered that way: hidden changes compile-time visibility and treats "names that should never have been given away getting given away"; -Bsymbolic changes link-time binding, keeps the export table exactly as it was, and treats "names that were given away getting their bindings snatched by someone else". There is a price too: it cuts off, along with everything else, the road by which preload-based profiling and tracing tools swap out library functions from the outside — the tools 09 mentioned that built their name on preloaded malloc are stymied the moment they meet a library built with -Bsymbolic. So C++ libraries should use it with care: exception matching and type identification depend on certain symbols being globally unique. The more common choice is the narrowed -Bsymbolic-functions, which binds only functions and spares the names that must stay globally identical.

## Move Three: Version Scripts — Selecting the Export List in Bulk Without Touching the Source

The third move hands the job to a linker script: the version script. We write it into a map file, and it is just these few lines: global lets the two interfaces through, and the local star sweeps up every other name — our source does not change by a character:

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

The result is as clean as the hidden version. Where it shines is bulk narrowing: building a library from someone else's entire source tree, you cannot go sprinkling attributes everywhere — one map file does the whole job. What we use here is the anonymous form: the version node (the block wrapped in that pair of braces in the map) carries no name, so the exported symbol names stay clean. Give the node a name, and the symbol names grow a @@version suffix, becoming versioned symbols — that is interface-generation management, the entire subject of the next piece, and we will not take a single extra step here.

## How Much You Exported, the Bytes Tell

How many bytes did the trimmed names save? We compare sections with size -A, the same move we used in 08:

```text
$ size -A libcalc.so | grep -E '\.dynsym|\.dynstr'
.dynsym               240     776
.dynstr               155    1016
$ size -A libcalc_hidden_api.so | grep -E '\.dynsym|\.dynstr'
.dynsym               168     760
.dynstr               126     928
```

Three fewer exported names: .dynsym drops from 240 to 168, one symbol entry in 64-bit ELF is 24 bytes, and three of them come to exactly seventy-two — right on the nose. .dynstr is the string table for symbol names; the three names plus their terminating null characters add up to 29 bytes, and the drop from 155 to 126 matches it seam to seam. Our library has all of five names, so the savings are naturally small — but the direction is real: the more you export, the bigger the .dynsym and .dynstr sections grow, the more dynamic relocation entries pile up, and the more work the dynamic linker has to resolve at startup.

The heavier burden sits on maintenance: the less you export, the freer you are to change things later — the ins and outs of that trade-off get a full unfolding in the next piece.

Following the size thread, one more question: strip, which we met in the object file anatomy piece, can strip away the .symtab meant for human eyes — but can it strip .dynsym, the one run time needs? We measured it: in a stripped library, nm -D still counts our five names, while on the ordinary symbol table nm says outright no symbols. What .symtab loses is only debug info; .dynsym is the table run-time resolution cannot live without, and even strip has to leave it in place for us.

## The Exported Names: C++ Mangling and extern C

With the list cleaned up, one question remains that you will run into sooner or later: what do the exported names themselves look like? In the compiler piece we read through the name-mangling process — C++ bakes namespaces and parameter types into the symbol name, which is how the linker can tell these overloads apart. To see what lands in the export table, we wrote a three-line specimen in C++: two overloads of discount, plus a refund exported the C way:

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

Both discounts make it into the export table under mangled strings beginning with _Z; run them through c++filt (its job is exactly to demangle — turning mangled names back into their original form) and the one ending in i is discount(int), the one ending in d is discount(double). refund, carrying extern C, keeps its clean plain name. Mangled names carry overloads just fine, but they are compiler-particular: g++ and clang++ agree on the rules, MSVC plays a different tune. Where compilers or languages must recognize each other's names, the extern C declaration is what everyone falls back on — at the price that nothing under it may ever be overloaded again. How cross-toolchain interfaces settle their names, and how dlsym goes about fetching functions by name — those are threads the run-time loading piece and the ABI piece will pick back up; here we just get on a first-name basis with them.

## The CMake View

For the hand-typed -fvisibility=hidden work, CMake has a ready-made target property: C_VISIBILITY_PRESET, and the same property on a C++ target is called CXX_VISIBILITY_PRESET. Here is our project, with the header using the version that has CALC_API hung on it:

```cmake
cmake_minimum_required(VERSION 3.20)
project(calc_vis C)

add_library(calc SHARED calc_api.c calc_core.c)
set_target_properties(calc PROPERTIES C_VISIBILITY_PRESET hidden)

add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
```

We compare with --verbose, and the real compile line is:

```text
$ cmake -B build -S . && cmake --build build --verbose
/usr/sbin/cc -Dcalc_EXPORTS  -fPIC -fvisibility=hidden -MD -MT CMakeFiles/calc.dir/calc_api.c.o -MF CMakeFiles/calc.dir/calc_api.c.o.d -o CMakeFiles/calc.dir/calc_api.c.o -c /tmp/calc_vis/calc_api.c
```

-fvisibility=hidden lands in every compile line; I trimmed the link line. The -Dcalc_EXPORTS next to it is a macro CMake defines on its own initiative, originally meant to serve Windows export headers — we on the Linux side have no use for it. We verified the product's export table too, and it is a spitting image of the hand-typed version: nm -D shows exactly the two interfaces, and the program prints base=100 total=120. The division of labor is now clear: the target property handles the global default narrowing, and the macro in the header handles the single-point opening — exactly the pair of mechanisms that move one described. The same family also has a VISIBILITY_INLINES_HIDDEN property; one sentence will do for it: it hides the exports of inline functions as well, and pairing it with a C++ target saves a little more. If you want a single macro that works on both Linux and Windows, go look at CMake's GenerateExportHeader: the header it generates expands to the visibility attribute on Linux and to dllexport and dllimport on Windows — but that is another topic altogether.

## Next: Managing the Interface Itself

By the point we have reached, both ends of the opening 09 left behind are plugged: for the lookup order you hold complete evidence from three experiments, and the three narrowing moves have each been measured on their own. But a cleaned-up export table only settles how much to give and to whom; the interface itself is still loose: how parameters travel, how struct fields are laid out, and how old programs keep working when the version bumps. These outward-facing agreements have a name taken together: ABI — the entire subject of our next piece, where we design the interface as a proper product.
