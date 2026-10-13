---
chapter: 13
difficulty: intermediate
order: 12
platform: host
reading_time_minutes: 17
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "ABI Design: Defining the Interface, Managing Its Versions"
description: "An ABI break caught in the act with an upgraded libcalc: a field slotted into the middle of a struct, a parameter switched from int to double, the app left un-rebuilt, the numbers quietly wrong, nm and readelf showing no difference, objdump photographing both sides fetching parameters by their own calling conventions. The three-part name answers the version upgrade: bump the official name's generation and old and new libraries coexist with old programs still running, the real name records the fixes, the linker name always points at the newest. Named nodes in the version script with the @@ double-at default version, two generations of a same-named function coexisting in one library, the COPY relocation freezing the executable's copy at the old size while glibc prints its warning and lets it through anyway, construction order flipped across both link orders and then tidied up with function-local statics, names settled between the Itanium and MSVC mangling schemes (the MSVC side an archived output), extern C paired with a complete ABI header for crossing toolchains, closing with CMake's SOVERSION and generate_export_header."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/12-abi-design.md
  source_hash: aaf67d96814a45b5a0d8129e6f980f287595a5ce3aef9ee4ff6840bcd61b7c0b
  translated_at: '2026-10-13T04:21:23+00:00'
  engine: anthropic
  token_count: 4700
---

# ABI Design: Defining the Interface, Managing Its Versions

At the end of the previous part we narrowed the export table down to two interfaces, and the three questions left dangling outside were handed on to today exactly as they were: how do parameters get passed, how do a struct's fields get laid out, and once the version moves up, how do old programs keep running. Back then we also gave these outward-facing agreements a collective name: the ABI. Today we take it on as a proper subject: settle the shape, settle the names, settle the versions. The specimen is still our libcalc, upgraded for the occasion: calc_item records the unit count and the unit price, and line_total's job is multiplying the two into a line's total. This little struct drags two of the three questions right onto the scene of the trouble, and you are about to see that it is a far tougher customer than a link failure.

The environment disclosure as usual: my experiments all ran in WSL2, on GCC 16.2.1 with binutils 2.47, glibc 2.44, and CMake 4.4.3, and every stretch of output in this piece was really run. If your versions differ, details like the wrong numbers and the byte counts will come out differently; the mechanics themselves have not changed in years.

## ABI: The Layer Beyond the Header File

Let's put API and ABI side by side and tell them apart. Those few lines of declaration in the header — what types the parameters take, what type comes back — are an agreement at the source level; the jargon for it is API. Your source includes them and compiles: that is called API compatibility. But compilation only translated the source into machine instructions. At the instruction level there is a whole other set of agreements that have to match, so we keep asking: how are symbol names encoded? Which register is a parameter fetched from? How large is the struct and where does each field sit? And where does the return value land? This layer is what we mean by ABI — the Application Binary Interface — and what it governs is how already-compiled binaries mate up with one another. The split between the two layers is really not convoluted: API compatibility means the source still compiles; ABI compatibility means a compiled old binary still takes a new library. The former has the compiler standing guard for us; for the latter there is no tool that stands guard for you the whole way through — and in a moment we will make it go wrong.

## The Scene of the Break: One Field Slotted In, the Numbers Quietly Go Wrong

We start with the real thing right away. In v1's header, calc_item carries just two fields. We build the app against v1, totaling a line of 3 units at a unit price of 2500:

```c
/* calc_v1.h — libcalc v1's outward-facing header (include guard omitted; don't learn that from me) */
struct calc_item {
    int units;   /* unit count */
    int price;   /* unit price, in cents */
};

int line_total(struct calc_item item);
```

```text
$ gcc -fPIC -shared calc_v1.c -I. -o libcalc.so && gcc app.c -I. -L. -lcalc -o app && LD_LIBRARY_PATH=. ./app
units=3 price=2500 total=7500
```

Now the library moves up to v2: the header slots an int discount in between units and price, and the implementation becomes multiply first, then subtract the discount. We do not touch a single byte of the app; we just swap libcalc.so for the one built from v2:

```text
$ gcc -fPIC -shared calc_v2.c -I. -o libcalc.so && LD_LIBRARY_PATH=. ./app
units=3 price=2500 total=1620446788
$ LD_LIBRARY_PATH=. ./app
units=3 price=2500 total=-1986195468
```

Stare at these lines for a moment: no crash, no complaint, just wrong numbers — and wrong differently every time. We threw every tool we had at it: nm -D on both versions of the library shows the very same line_total exported, cast from the same mold. We paged through the dynamic section with readelf and could not find a single record that raises an alarm. A compile error at least says its piece to your face; an ABI break says not one word — it just quietly computes the wrong thing, and that is exactly what makes it more dangerous than a compile error. Let's trace the call site to find where it went wrong. The app was built against v1, so its calc_item is 8 bytes, and the machine, following the calling convention (the spec for how parameters are parceled out to registers — more on that below), passes the whole thing in rdi. The library was built against v2, so its calc_item is 12 bytes: the first 8 bytes are fetched from rdi, the last 4 from rsi. So units reads 3, discount reads 2500 (the unit price the app passed in), and price reads whatever stale value is sitting in rsi — nobody ever guaranteed what's in there. 3 times an unguaranteed number, minus 2500, and you get that string of wrong numbers that differs on every run. And there is the answer to how a struct's fields get arranged: field layout is part of the ABI; slot one into the middle and everything after it is misaligned.

For how parameters get passed, let's look at an even smaller change: only the parameter type changes. v1's library exports int boost(int), and the app is built against the int version: 21 goes in, 42 comes back. On the library side, v2 switches the parameter to double while the spelling of the function name does not change by a single character:

```text
$ gcc -fPIC -shared boost_v1.c -o libboost.so && gcc app.c -L. -lboost -o app && LD_LIBRARY_PATH=. ./app
boost(21)=42
$ gcc -fPIC -shared boost_v2.c -o libboost.so
$ LD_LIBRARY_PATH=. ./app
boost(21)=0
```

That 0 is actually not luck. Let's use objdump to line up what each side said (output excerpted): the app's side issues `mov edi,0x15`, while the library's side is `movsd QWORD PTR [rbp-0x8],xmm0`. The app put 21 into edi, but the library fetches its double from xmm0. The one calling the shots here is the calling convention we named just now: within one process, which general-purpose register carries integer parameters, which XMM register carries floating point, and where the return value lands — all of it has to follow one and the same spec, and our environment uses x86-64 SysV (the System V Application Binary Interface, the spec laid down by Unix). On ordinary days the two sides get along because the compilers translate the same header file and the two ends' actions naturally agree. The moment the header changes, the symbol is still named boost, the linker happily wires the two sides together, and nobody knows the conventions have forked. So the answer to how parameters get passed lies not in what the header says, but in whether the two sides' compiled fetching actions are one and the same set.

## Upgrade Discipline: Three Names, Each Governing Its Own Stretch

When the interface changes the library has to move up a version, and in this section we pay off the debt left by the library-search piece. That piece laid out the three-piece set of symlinks: libcalc.so, libcalc.so.1, libcalc.so.1.2.3, and said the three names each govern their own stretch — what each of the three segments of the version number records was left for today. The three names correspond to the three segments of the version number, and the collective jargon for them is the three-part name. The real name carries all three segments (libcalc.so.1.2.0); the official name keeps only the leading segment (libcalc.so.1 — exactly what gets registered in the library's SONAME); and the linker name keeps it simple: libcalc.so, carrying no version segments at all. Let's actually do it once:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.1 -o libcalc.so.1.2.0 calc_v1.c -I. && ln -s libcalc.so.1.2.0 libcalc.so.1 && ln -s libcalc.so.1 libcalc.so
$ readelf -d libcalc.so.1.2.0 | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libcalc.so.1]
$ gcc app_v1.c -I. -L. -lcalc -o app_v1 && readelf -d app_v1 | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.1]
```

The linker name governs link time: -lcalc touches libcalc.so, reads through the symlink to the real body, and copies the official name it registered into NEEDED. The official name governs run time: from start to finish the dynamic linker recognizes nothing but libcalc.so.1. The real name is the file itself — the full version is recorded on it, and it is exactly what you see when you ls your disk. Now v2 arrives, doing precisely the deed from the scene of the break: a field added to the struct, the ABI changed. Our proper handling does not go the route of overwriting the old file; the route we take is to bump the generation:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.2 -o libcalc.so.2.0.0 calc_v2.c -I. && ln -s libcalc.so.2.0.0 libcalc.so.2 && ln -sf libcalc.so.2 libcalc.so
$ gcc app_v2.c -I. -L. -lcalc -o app_v2 && readelf -d app_v2 | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.2]
$ LD_LIBRARY_PATH=. ./app_v2 && LD_LIBRARY_PATH=. ./app_v1
v2 app: total=7200
v1 app: total=7500
```

The two generations of the library now lie side by side in the same directory. When we let a new program link, the linker name already points at the second generation, and what NEEDED records is libcalc.so.2. The old program remains entirely unaware; the libcalc.so.1 it wants is still there — and it still prints 7500 as before. Here is the answer to how old programs keep running when the version moves up: all the old program holds is the official name, and as long as we do not pull that symlink, it keeps on being runnable.

As for which change moves which segment, we read it straight off the mechanism: the ABI changed, the official name moves up a segment, and the old generation is kept as is. When a change merely fixes bugs or adds new functions, the old agreements are untouched and the real name's last two segments step up (1.2.0 to 1.2.1, or 1.3.0, either works) while the official name stays put — swapping in the new file hands the old program the fix. The linker name always points at the newest generation; it serves nothing but the linking of new programs. And the library-swap accident from the scene of the break gets its proper answer here: with a field slotted in, what should move up is the official name — libcalc.so.1 stays untouched, and the misalignment never happens.

## Version Nodes: Two Generations of an Interface Living in One Library

Beyond splitting generations by swapping files there is a finer gear. In the previous part we used a version script to rope off exports; the nodes back then were nameless. If you give the nodes names, versions hang off the tails of the symbols — that piece of work we make up today. A version node is exactly one brace-wrapped block in the map file, and we give two of them names:

```text
CALC_1.0 { global: line_total; local: *; };
CALC_2.0 { global: line_total; bulk_total; } CALC_1.0;
```

Notice that the 2.0 node recognizes 1.0 as its parent, and the new function bulk_total is registered only under 2.0's name. For two generations of the same name, the script alone is not enough; we need one of the compiler's assembly directives, .symver, to step in — it is the one that dispatches the two implementations to the two nodes:

```c
__asm__(".symver line_total_1,line_total@CALC_1.0");
__asm__(".symver line_total_2,line_total@@CALC_2.0");
int line_total_1(struct calc_item item) { /* the old algorithm */ }
int line_total_2(struct calc_item item) { /* from 2.0 on, 50 off for every full 1000 */ }
```

A single @ means attach by name to a given node; the double @ (@@) additionally declares itself the default version: for clients that do not pick a version, the linker always hands them the @@ one. Let's build it and look:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.1 -Wl,--version-script=calc.map calc_v.c -I. -o libcalc.so && nm -D libcalc.so | grep -E 'line_total|bulk_total'
0000000000001126 T bulk_total@@CALC_2.0
00000000000010fc T line_total@@CALC_2.0
00000000000010e9 T line_total@CALC_1.0
```

One library now houses two generations of line_total. An old client that wants to keep recognizing the old version has the same move available: write the same .symver line in its own source, pointing the reference at @CALC_1.0. Then we have the two clients link against the same library and each run its own way:

```text
$ LD_LIBRARY_PATH=. ./app_new && LD_LIBRARY_PATH=. ./app_old
new client: total=7450
old client: total=7500
```

readelf -V photographs the version definition tree inside the library for us, and CALC_2.0's parent is exactly CALC_1.0. In each executable's own version-requirements section (the section in an exe that records which version of the interface it wants — visible with readelf -V), one registers only CALC_1.0 and the other only CALC_2.0, and at run time each takes its own generation. The libc.so.6 on your system does exactly this: I have counted, and over two thousand symbols in its export table trail a @@GLIBC_2.xx suffix — interfaces added across decades that could no longer be changed, all kept running by splitting generations with nodes. But you can also weigh how heavy this work is: the old client has to write a .symver in its source to pick the version, while the new client writes nothing at all. So the everyday workhorse for splitting generations is still bumping the official name's segment; version nodes serve the kind of evolution where, under one and the same official name, interfaces are only added and never changed — and glibc is the most famous user of that.

## The COPY Relocation: A Copy Fixed Down at Link Time

In the position-independence piece we watched one scene: an executable reads the library's variable tax_rate directly, and the linker arranges a copy for it in the exe's .bss — at load time the dynamic linker copies in the initial value, while the library itself, in a twist, has to refer back to the copy inside the exe. We left one sentence hanging back then: the same variable now has two records in the process, and the questions that grow out of that belong to today. Now we make it go wrong; the specimen is rateapp from the resurrection piece. v1's library exports int tax_rate = 17, we build the exe ahead of time, it dutifully prints 17 when run, and we also photographed the COPY relocation record (that piece called it the copy relocation — the same thing):

```text
$ readelf -rW rateapp | grep COPY
0000000000004018  0000000700000005 R_X86_64_COPY          0000000000004018 tax_rate + 0
```

The root of the trouble is already written into the relocation record above: the size of the exe-side copy is the 4 bytes it measured off the v1 library at link time, and once measured it is frozen into the file. The library side later changed tax_rate to a double, doubling the size to 8 bytes; we do not touch a single byte of the exe and just swap in the library and run:

```text
$ gcc -fPIC -shared rate_v2.c -o libcalc.so && LD_LIBRARY_PATH=. ./rateapp
./rateapp: Symbol `tax_rate' has different size in shared object, consider re-linking
rate=1546188227
```

glibc's dynamic linker does see that the sizes do not match; it shouts one line of consider re-linking, and after shouting it copies as usual and lets it through as usual. What gets copied into the 4-byte slot is the first four bytes of that 0.17 double (in little-endian hex, c3 f5 28 5c), and read as an int it comes out 1546188227 — I ran it twice and it was wrong to the letter, both times the same. This is the COPY relocation's price at the ABI level: the variable's size and type are frozen at link time by the executable, and no matter how the library side moves afterward, the exe's copy is out of step with the real body. The countermeasure is nothing mystical: do not export variables through an interface; if you want to hand something out, hand out a function that reads it, and let the variable retreat into the library's interior. If old code truly cannot avoid it and you need a remedy on the spot, the approach we verified on the GCC side is to rebuild the executable with -fPIC as well: in testing, the COPY record drops to zero, tax_rate's fetch switches to R_X86_64_GLOB_DAT via the GOT, and the program still prints 17 — the copy simply never lands. Clang has its own switch for the same deed, -fno-direct-access-external-data.

## Construction Order: The Global Objects Nobody Schedules

The ABI governs how functions and data mate; there is one more class of objects we have to treat on its own. Global objects come with constructors, and construction happens before main. The C++ standard leaves a blank here: within a single translation unit, global objects are constructed in definition order, but the order across translation units — nothing is specified. Let's set out two of them, logger and config, each probing in its constructor whether the other is up yet:

```text
$ g++ main.cpp logger.cpp config.cpp -o app_a && ./app_a
logger ctor: config.ready=0
config ctor: logger.ready=1
$ g++ main.cpp config.cpp logger.cpp -o app_b && ./app_b
config ctor: logger.ready=0
logger ctor: config.ready=1
```

Whoever is ordered earlier finishes constructing first, and whoever is ordered later sees ready as 0. If you genuinely use the other party inside a constructor — say config logs through logger, while logger's side is still unconstructed memory — you get garbage at best and a crash at worst. In practice this order follows link order, but that is only the current state of this toolchain, not a guarantee from the standard; a different build could arrange things differently. The fix is an old classic: hide the object inside a function — the jargon name is the function-local static. Once the object is written into a function body, construction waits until control flow reaches that line for the first time. We give each of the two objects an accessor function with a static instance inside the body; it constructs the first time a call reaches it, and hands back the same one ever after. config's constructor needs logger, so calling logger() constructs the Logger along the way and then hands it back. We run both link orders again, and the output is one and the same:

```text
$ g++ main.cpp logger.cpp config.cpp -o app_a && ./app_a
logger ctor
config ctor: logger.ready=1
```

We ran the other link order too, and its output matches the above character for character. Order has passed from link order's hands into the hands of the call relationships: whoever depends on whom, the depended-upon party is the one constructed first. Since C++11 this construction is also thread-safe out of the box — under a concurrent first call, still only one thread actually constructs. Global objects inside a shared library take the same fix; the construction order among a library's multiple translation units is likewise guaranteed by nobody. As for the whole loading sequence, the program-startup piece will walk us through it from end to end; here we stop at the interface layer.

## Settling the Names: Two Encodings That Do Not Recognize Each Other, Plus One extern "C"

With this, the three questions are all handed in; one old thread from the compiler piece remains: names. In that piece we untangled _Z-prefixed mangled names with c++filt, said that encoding came out of the Itanium C++ ABI, and left a question behind: across compilers and platforms, is it still the same encoding? Today the answer arrives: the Linux side uses one set, and MSVC over there uses another that does not recognize it. Let's compile the same source on the Linux side:

```text
$ g++ -c test.cpp -o test_name.o && nm test_name.o
0000000000000021 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000000 T _ZN3Foo8someFuncEiPKc
```

For the MSVC-side sample, I once ran dumpbin on the same code on a Windows machine, and the symbols came out looking like this (compiler version 19.44.35217; these lines are my archived output from back then — we do not rerun it on this Linux box):

```text
D:\DownloadFromInternet>dumpbin /SYMBOLS test.obj
00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))
```

What leads is ?, @@ handles the segmentation, and there is no kinship at all with the _Z scheme. GCC and Clang on Linux both use the Itanium scheme, while MSVC is its own school — the two families' symbols do not recognize each other. So a C++ library that wants to be usable across compilers cannot get there by exporting mangled names directly; it has to switch to a naming both sides accept: C's names. In the previous part we already exported refund with `extern "C"`; past the mangling gate, the symbol becomes the function's plain name, and you have also seen the price: it admits no overloads under its name. Windows still has a few details that differ from ELF. We compared library-search order back in the library-search piece; the rest we settle today in one go. The matter of the one .lib suffix carrying two identities we already met there: it is the static library, and it is also the import library that pairs with a DLL. The remaining difference only shows at run time: at link time everyone recognizes .lib, and it is at run time that the loader goes and fetches the DLL proper. The export spellings are `__declspec(dllexport)` and `__declspec(dllimport)` — one for building the library, one for using it — doing the same work as the previous part's pair of collapse-globally-to-default and open-up-single-points. Naming habits differ too: a DLL's filename usually carries no version number, and mylib.dll is already the whole name; the work ELF does with three-part-name generations, Windows hands to the install layout, with different versions each living in their own directory.

`extern "C"` only settles the name; a header that can cross toolchains has to pin down the rest as well. We compress what matters into a four-piece set: function declarations, type definitions, macro constants, include dependencies. Let's work up a version of libcalc:

```c
/* calc_abi.h — the complete outward ABI header: complete only when all four pieces are in place */
#include <stddef.h>                  /* include dependency: where size_t comes from */
#define CALC_MAX_ITEMS 64            /* macro constant: the number the interface uses */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct calc_item {           /* type definition: layout written into the header */
    int units;
    int price;
} calc_item;
typedef struct calc_pool calc_pool;  /* opaque pointer: declared, never defined */
int          calc_line_total(const calc_item* item);  /* function declaration */
calc_pool*   calc_pool_create(size_t n);
void         calc_pool_destroy(calc_pool* pool);
const char*  calc_build_id(void);
#ifdef __cplusplus
}
#endif
```

The four pieces are not equal in weight; let's split them into two tiers. The load-bearing tier is function declarations paired with type definitions. Without the declarations, the caller does not know the names and types. Without the type definitions, the compiler cannot work out calc_item's size, and the arrangement of parameters on the stack and in registers falls apart — the scene-of-the-break accident was rooted in exactly this. The supporting tier is macro constants and include dependencies; missing them does not blow up on the spot: each side writes its own version of the constants, the header stops standing up, and the illness is buried out of sight. Let's build one caller with gcc and one with g++, both linking the same library (only two entries picked out of nm's export table):

```text
$ gcc -fPIC -shared calc_abi.c -I. -o libcalcabi.so && gcc main_c.c -I. -L. -lcalcabi -o app_c && g++ main_cpp.cpp -I. -L. -lcalcabi -o app_cpp
$ nm -D libcalcabi.so | grep calc_
0000000000001166 T calc_build_id
0000000000001133 T calc_pool_create
$ LD_LIBRARY_PATH=. ./app_c && LD_LIBRARY_PATH=. ./app_cpp
C caller:    total=7500 id=calc-abi-demo
C++ caller:  total=10000 pool=ok id=calc-abi-demo
```

Both callers are fed by the same library and the same header, and every exported name is a bare name. You should also take a second look at calc_pool in the header — it goes the opaque-pointer route. The caller holds only a pointer — it can pass it around everywhere, yet it can neither construct nor take apart a calc_pool instance. However the library's layout changes, no old code is implicated — because it never touches the layout at all. Where the factory functions construct the objects, and how what gets constructed is claimed by name, we leave for the next part — that is the proper subject of finding people at run time.

## Which Changes Count as ABI Events

Now we can turn the previous part's the-narrower-the-export-the-freer-the-changes into a concrete list. Anything that changes how old binaries fetch data or how things are arranged counts as an ABI event. Slotting a field into the middle of a struct — verified in the scene of the break. Changing a parameter type — verified with the two ABIs as well. As for changing the return type, reordering fields, or altering alignment, we can no more dodge those; they all require bumping the official name. Not events: fixing bugs, adding new functions, adding comments to the header — the real name's last two segments stepping up is enough.

The matter of virtual functions we take out on its own. Adding the first virtual function to a class that had none inserts a vtable pointer into the object's head — a vtable is exactly the table of virtual functions — and in testing we measured an old exe silently printing garbage values. For a leaf class that already has virtual functions and that nobody inherits from, appending a new virtual function at the end leaves every old vtable slot untouched; the old exe's results stay correct, it just cannot reach the new function. KDE's (the Linux desktop project's) binary compatibility policy gives exactly this line. Look back at the previous part's three narrowing moves: what they narrow is the length of this list — the fewer names exported, the shorter the list, and the more room you have to move later. Paired with the upgrade discipline above, a library's interface discipline is complete: narrow the outward-facing surface, never export variables; structs either do not move, or when they move they move together with a generation bump; once a function signature is settled it does not change — to change it, add a new one.

## The CMake View

The work of hand-typing the soname link option and hand-raising symlinks, CMake takes over with a pair of target properties. The first is named VERSION and the other SOVERSION. Let's set the specimen up as a small project:

```cmake
cmake_minimum_required(VERSION 3.20)
project(calc_abi C)
include(GenerateExportHeader)
add_library(calc SHARED calc_abi.c)
set_target_properties(calc PROPERTIES VERSION 1.2.0 SOVERSION 1)
generate_export_header(calc)
add_executable(app_c main_c.c)
target_link_libraries(app_c PRIVATE calc)
```

VERSION records the real name's three segments and SOVERSION records the official name's leading segment. We build it and verify: the three-piece set lies neatly inside build, the middle symlink's name is exactly SOVERSION's 1, the SONAME registered in the library is that same one, and the app runs bare without a hitch (CMake wrote the build tree's app an rpath pointing into build; the library-search piece covered where that comes from):

```text
$ cmake -B build -S . && cmake --build build
$ ls -l build/libcalc.so*
lrwxrwxrwx 1 charliechen charliechen    12 Oct  1 14:46 build/libcalc.so -> libcalc.so.1
lrwxrwxrwx 1 charliechen charliechen    16 Oct  1 14:46 build/libcalc.so.1 -> libcalc.so.1.2.0
-rwxr-xr-x 1 charliechen charliechen 15264 Oct  1 14:46 build/libcalc.so.1.2.0
$ readelf -d build/libcalc.so.1.2.0 | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libcalc.so.1]
$ ./build/app_c
total=7500 id=calc-abi-cmake
```

generate_export_header is the topic the previous part's CMake section owed; today we make good on it. It generates a calc_export.h in the build directory, and we bring out the lines that matter:

```c
#    ifdef calc_EXPORTS
        /* We are building this library */
#      define CALC_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define CALC_EXPORT __attribute__((visibility("default")))
#    endif
```

The calc_EXPORTS definition is only provided by CMake when building the library; the using-the-library case does not get it. On Linux, both branches expand into the very same visibility attribute, because the ELF world does not distinguish importing from exporting. Configure the same pair of branches on Windows instead, and what expands out is `__declspec(dllexport)` and `__declspec(dllimport)` — one macro serves both ends. The single-point-opening half of the manual work from the previous part, it does for us automatically (the collapse-globally-to-default half was already covered by target properties in that part's CMake section). There is one thing it does not handle: the `extern "C"` wrapping — the generated header says not one word about it, and you still have to write that in your own header. The pair of `#ifdef __cplusplus` guards in calc_abi.h earlier occupy exactly that position. For a release, add one line of install(TARGETS calc ...) and the three-piece set goes into lib with the header into include — and as for the heavy lifting of generation-splitting, you already know all of it from the hand-done pass above.

## Next Up: Finding People at Run Time

Having come this far, the big matters of the interface all have their accounts settled: shape rests on the calling convention and struct layout, names rest on `extern "C"` paired with a complete ABI header, and versions rest on the three-part name plus version nodes. But they all share one trait: the dependencies are hard-written at link time — what NEEDED records is the name, so the program must have all its libraries found the moment it starts. There is one more usage that skips this step entirely — after the program is running, open a library file on demand and fetch functions out of it by name; that is exactly how plugins make their living. That mechanism for finding people at run time is the next part's proper subject, and we leave the opaque-pointer factory waiting there for you.
