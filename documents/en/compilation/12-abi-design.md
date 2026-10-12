---
chapter: 13
difficulty: intermediate
order: 12
platform: host
reading_time_minutes: 32
tags:
- cpp-modern
- host
- intermediate
title: "ABI Design: Defining the Interface, Managing Its Versions"
description: "An ABI break caught in the act with an upgraded libcalc: a field slotted into the middle of a struct, a parameter switched from int to double, the app left un-rebuilt, the numbers quietly wrong, nm and readelf showing no difference, objdump catching both sides fetching parameters by their own calling conventions. The three-piece set answers the version upgrade: bump the official name's generation and old and new libraries live side by side with old programs still running, the realname records the fixes, the linker name always points at the newest. Named nodes in the version script and the double-at @@ default version, two generations of a same-named function coexisting in one library, the COPY relocation freezing the executable's copy at the old size while glibc prints its warning and lets it through anyway, construction order laid bare by trying both link orders and then tidied away with function-local statics, names settled between the Itanium and MSVC mangling schemes (the MSVC side an archived output), extern C paired with a complete ABI header for crossing toolchains, closing from the CMake perspective with SOVERSION and generate_export_header."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/12-abi-design.md
  source_hash: ba2d05ee27a0b412bf52d21a7ba3b954eea438c40910882e566986dc1f790061
  translated_at: '2026-10-01T08:26:07+00:00'
  engine: glm
  token_count: 5450
---

# ABI Design: Defining the Interface, Managing Its Versions

At the end of part 11 we narrowed the export table down to two interfaces, and the three questions still sitting outside were handed on to today exactly as they were: how do parameters get passed, how do a struct's fields get arranged, and once the version moves up, how do old programs keep on working. Back then we also gave these outward commitments a collective name: ABI. Today we take it on as a proper subject: settle the shape, settle the names, settle the versions. The specimen is still our libcalc, upgraded for the occasion: calc_item records the unit count and the unit price, and line_total's job is multiplying the two into a line's total. This little struct pulls two of the three questions right into the scene of the trouble, and you are about to see that it is a far harder customer than a link failure.

The environment disclosure as usual: my experiments all ran in WSL2, on the combination of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, and every stretch of output in this piece was really run. If your versions differ, details like the wrong numbers and the byte counts may come out differently; the mechanics themselves haven't changed in years.

## ABI: The Layer Beyond the Header File

Let's set API and ABI side by side and tell them apart. The few lines of declaration in a header file — what type the parameters take, what type the return is — are a source-level agreement, and the jargon for it is API. Your source includes it and compiles: that is called API compatibility. But compiling only translates the source into machine instructions. At the instruction level there is a whole other set of agreements that have to match up, so we keep asking: how are symbol names encoded? Which register does a parameter get fetched from? How large is the struct, and where do the fields sit? And where does the return value go? This layer is what we call the ABI — the Application Binary Interface — and what it governs is how finished binaries mate up with one another. The split between the two layers is not really twisty: API compatibility means the source still compiles; ABI compatibility means a finished old binary still takes the new library. The former has the compiler checking it for us; for the latter, no tool checks it for you the whole way through — and we will make it go wrong in a moment.

## The Scene of the Break: One Field Slotted In, the Numbers Quietly Go Wrong

We go at the real thing from the very first step. In v1's header, calc_item carries just two fields. We build the app against v1, totaling a line of 3 units at a unit price of 2500:

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

Now the library moves up to v2: the header slots an int discount in between units and price, and the implementation changes to subtracting the discount after the multiply. We do not touch one byte of the app; we only swap libcalc.so for the v2 build:

```text
$ gcc -fPIC -shared calc_v2.c -I. -o libcalc.so && LD_LIBRARY_PATH=. ./app
units=3 price=2500 total=1620446788
$ LD_LIBRARY_PATH=. ./app
units=3 price=2500 total=-1986195468
```

Look hard at these lines: no crash, no complaint, wrong numbers — and differently wrong every time. We brought every tool we have: nm -D on the two versions of the library shows the same exported line_total, stamped from a single mold. We combed the dynamic section with readelf and found not one alarming record anywhere. A compile error at least says the words to your face; an ABI break says nothing at all — it just quietly computes wrong, and that is exactly what makes it more dangerous than a compile error. Let's follow the call site to find where the error lives. The app was built against v1, so the calc_item it knows is 8 bytes, and the machine — following the calling convention (the rules for how parameters get divided among registers; we will go into it below) — puts the whole thing into rdi. The library was built against v2, so the calc_item it knows is 12 bytes: the first 8 bytes come out of rdi, the last 4 out of rsi. So units reads 3, discount reads 2500 (the unit price the app passed in), and price reads whatever stale value was left sitting in rsi — nobody ever promised what would be in there. Three times an unpromised number, minus 2500, and out comes that wrong number, different on every run. And with that, the answer to how a struct's fields get arranged falls out on its own: the field layout is part of the ABI — slot one into the middle, and everything arranged behind it is knocked out of place.

For how parameters get passed, we take an even smaller change to look at: touching only the parameter type. The v1 library exports int boost(int), and the app was built against the int version: 21 goes in, 42 comes back. We have the library's v2 switch the parameter to double, without moving one character of the function name's spelling:

```text
$ gcc -fPIC -shared boost_v1.c -o libboost.so && gcc app.c -L. -lboost -o app && LD_LIBRARY_PATH=. ./app
boost(21)=42
$ gcc -fPIC -shared boost_v2.c -o libboost.so
$ LD_LIBRARY_PATH=. ./app
boost(21)=0
```

That 0 is no luck either. We lined up what both sides say with objdump (the output is an excerpt): the app's side puts `mov edi,0x15`, while the library's side insists on `movsd QWORD PTR [rbp-0x8],xmm0`. The app put 21 into edi; the library fetches the double out of xmm0. What steps forward to take charge here is the calling convention we just named: within one process, which general-purpose register an integer parameter travels in, which XMM register a floating-point one travels in, and where the return value lands — all of it has to follow one shared set of rules, and in this environment of ours the rules are x86-64 SysV (the System V Application Binary Interface, the standard Unix laid down). On ordinary days the two sides live in peace, and the reason is that the compiler translates the same header for both, so their actions agree by nature. Once the header changes while the symbol's name is still the same boost, the linker happily hooks the two sides together, nobody any the wiser that the agreement has already forked. So the answer to how parameters get passed lies not in what the header says, but in whether the fetching actions compiled on both sides are the same set.

## Upgrade Discipline: Three Names, Each Governing Its Own Stretch

Once the interface changes, the library has to bump its version, and in this stretch we pay off the work part 10 left behind. In part 10 we laid out the three-piece set's symlinks — libcalc.so, libcalc.so.1, libcalc.so.1.2.3 — and said that three names each govern one stretch; what each of the version number's three segments records was saved for today. The three names answer to the version number's three segments — the jargon for the whole arrangement is the three-piece set. The realname carries all three segments (libcalc.so.1.2.0); the official name keeps only the head segment (libcalc.so.1, exactly what is registered in the library's SONAME); and the linker name's approach is blunt: libcalc.so, carrying no version segment at all. Let's really do it once:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.1 -o libcalc.so.1.2.0 calc_v1.c -I. && ln -s libcalc.so.1.2.0 libcalc.so.1 && ln -s libcalc.so.1 libcalc.so
$ readelf -d libcalc.so.1.2.0 | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libcalc.so.1]
$ gcc app_v1.c -I. -L. -lcalc -o app_v1 && readelf -d app_v1 | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.1]
```

The linker name governs link time: -lcalc feels out libcalc.so, follows the symlink down to the real body, and copies the official name it registered into NEEDED. The official name governs run time: from head to tail, the dynamic linker recognizes only libcalc.so.1. The realname is the file itself — the complete version is recorded on it, and it is exactly what your ls gets you on the disk. Now v2 arrives, doing the very thing from the scene of the break: the struct gains a field, the ABI has changed. Our proper handling does not take the road of overwriting the old file; the road we take is raising a generation:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.2 -o libcalc.so.2.0.0 calc_v2.c -I. && ln -s libcalc.so.2.0.0 libcalc.so.2 && ln -sf libcalc.so.2 libcalc.so
$ gcc app_v2.c -I. -L. -lcalc -o app_v2 && readelf -d app_v2 | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.2]
$ LD_LIBRARY_PATH=. ./app_v2 && LD_LIBRARY_PATH=. ./app_v1
v2 app: total=7200
v1 app: total=7500
```

Two generations of the library now lie side by side in the same directory. When we let a new program come link, the linker name already points at the second generation, and what NEEDED records is libcalc.so.2. The old program knows nothing of any of this — the libcalc.so.1 it wants is still there, and it goes on printing 7500 as ever. So the answer to how old programs keep on working after the version moves up is exactly this arrangement: all an old program holds in its grip is the official name, and as long as we never pull that symlink out from under it, it stays runnable forever.

Which change should move which segment — let's read it straight off the machinery: if the ABI moved, the official name goes up one segment and the old generation stays in place, untouched. For changes that only fix a bug or add a new function, where the old commitments have not moved, the realname's last two segments walk upward (1.2.0 to 1.2.1, or to 1.3.0 — both fine) and the official name does not move: an old program swapping in the new file has, by that very act, accepted the fix. The linker name forever points at the newest generation; it serves the linking of new programs only. The library-swap accident from the scene of the break has its proper answer here: a field got inserted, so the thing to raise is the official name — libcalc.so.1 stays as it was, and the misalignment never happens.

## Version Nodes: Two Generations of an Interface Living in One Library

Beyond the swap-the-file way of splitting generations, there is one finer grade. In part 11 we used a version script to rein in the exports, and the node went unnamed at the time; if you do name the node, the symbols sprout a version on their tails — that chore lands on us today. A version node is exactly the block a pair of braces wraps inside the map file, and we name two of them:

```text
CALC_1.0 { global: line_total; local: *; };
CALC_2.0 { global: line_total; bulk_total; } CALC_1.0;
```

Look: the 2.0 node acknowledges the 1.0 node as its parent, and the new function bulk_total is registered only under 2.0's name. For two same-named generations, the script alone is not enough — we have to call on .symver, an assembler directive from the compiler, to step forward; it is what delivers the two implementations to their two separate nodes:

```c
__asm__(".symver line_total_1,line_total@CALC_1.0");
__asm__(".symver line_total_2,line_total@@CALC_2.0");
int line_total_1(struct calc_item item) { /* the old algorithm */ }
int line_total_2(struct calc_item item) { /* from 2.0 on: 50 off once the total passes 1000 */ }
```

A single @ means being pinned to a particular node by name; the double @ (@@) additionally declares itself the default version: for a client that picks no version, the linker hands out the @@ one, without exception. We build it and look:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.1 -Wl,--version-script=calc.map calc_v.c -I. -o libcalc.so && nm -D libcalc.so | grep -E 'line_total|bulk_total'
0000000000001126 T bulk_total@@CALC_2.0
00000000000010fc T line_total@@CALC_2.0
00000000000010e9 T line_total@CALC_1.0
```

Two generations of line_total now live in one library. An old client that still wants the old version has the same route available: write an identical .symver line in its own source — pointing the reference at @CALC_1.0 — and then we let the two clients link the very same library, each running its own way:

```text
$ LD_LIBRARY_PATH=. ./app_new && LD_LIBRARY_PATH=. ./app_old
new client: total=7450
old client: total=7500
```

readelf -V photographs the library's version-definition tree for us, and CALC_2.0's parent node is indeed CALC_1.0. In each of the two executables' version-requirements section (the section in an exe that records which generation of the interface it wants — readelf -V shows it), the one registers only CALC_1.0, the other only CALC_2.0, and at run time each takes its own generation. The libc.so.6 on your system does exactly this — I counted: more than two thousand symbols in its export table wear @@GLIBC_2.xx tails, and the interfaces added over the decades that could never be changed again all keep working through this node-splitting arrangement. But you can also see the weight of the chore: an old client has to write a .symver in its source to pick its version, while a new client writes nothing at all. So the everyday workhorse for splitting generations is still the official-name bump; version nodes serve the kind of evolution where, under one unchanged official name, the interface only grows and never changes — glibc being the most famous user.

## The COPY Relocation: A Copy Settled at Link Time

In part 09 we watched a scene: the executable reads the library's variable tax_rate directly, and the linker arranged a copy of it in the executable's .bss — at load time the dynamic linker copies the initial value in, and the library itself ends up referring back to the copy in the exe. We left one sentence behind then: the same variable now has two records in the process, and the questions growing out of that belong to today. Now we make it go wrong, on a specimen resurrected from part 09's rateapp. The v1 library exports int tax_rate = 17; the exe we built ahead of time, and it honestly prints 17 when it runs. The COPY relocation record is photographed too (part 09 called it the copy relocation — same thing):

```text
$ readelf -rW rateapp | grep COPY
0000000000004018  0000000700000005 R_X86_64_COPY          0000000000004018 tax_rate + 0
```

The root of the trouble is already written into the relocation record above: the exe-side copy's size is the 4 bytes it measured off the v1 library when it linked, and once measured, the figure was frozen into the file. The library later changed tax_rate to a double, doubling the size to 8 bytes; we leave the exe untouched, one byte not moved, and simply swap the library in and run:

```text
$ gcc -fPIC -shared rate_v2.c -o libcalc.so && LD_LIBRARY_PATH=. ./rateapp
./rateapp: Symbol `tax_rate' has different size in shared object, consider re-linking
rate=1546188227
```

glibc's dynamic linker does see the size mismatch, shouts one line of consider re-linking, and having shouted, copies as usual and lets it through as usual. What gets copied into the 4-byte slot is the first four bytes of the double 0.17 (in little-endian hexadecimal, that is c3 f5 28 5c); read the int way, it comes to 1546188227. I ran it twice, and both times it was wrong to the digit, without the slightest drift. That is the COPY relocation's price on the ABI: a variable's size and type get frozen by the executable at link time, and however the library side moves afterward, the exe's copy and the real body stand misaligned. The countermeasure is nothing mystical: do not export variables on an interface — if you must offer something, offer a function that reads the value out, and retire the variable into the library's interior. Where old code truly cannot dodge it and a fix has to happen on the spot, the approach we verified on the GCC side is rebuilding the executable with -fPIC as well: in the actual test, the COPY records went to zero, tax_rate's fetch turned into an R_X86_64_GLOB_DAT going through the GOT, and the program printed 17 as ever — the copy never landed at all. On the Clang side there is another switch, -fno-direct-access-external-data, doing the same job.

## Construction Order: The Global Objects Nobody Scheduled

The ABI governs how functions and data mate up, and there is one class of objects we have to single out. Global objects carry constructors, and the moment of construction lands before main. The C++ standard leaves a blank right here: within one translation unit, global objects are constructed in definition order; across translation units — no rule at all. We set two of them out — logger and config — each probing in its constructor whether the other is up yet:

```text
$ g++ main.cpp logger.cpp config.cpp -o app_a && ./app_a
logger ctor: config.ready=0
config ctor: logger.ready=1
$ g++ main.cpp config.cpp logger.cpp -o app_b && ./app_b
config ctor: logger.ready=0
logger ctor: config.ready=1
```

Whichever sits earlier in the line gets constructed one step sooner, and the one sitting later sees ready as 0. If you actually put the other party to use inside a constructor — say config logs through logger, while the logger side is still unconstructed memory — the light outcome is garbage, the heavy one a crash. In practice this order follows the link order, but that is only the current state of this toolchain, not a guarantee the standard hands out; a different build can deal a different arrangement. The fix is an old classic: hide the object inside a function, whose jargon name is the function-local static. Once the object is written into a function body, construction waits until control flow first walks onto that line. We give each of the two objects an access function, put a static instance inside the function body, construct on the first call, and hand back the very same one ever after. When config's constructor needs logger, calling logger() constructs the Logger in passing and then hands it back. We run both link orders again, and the output is one and the same:

```text
$ g++ main.cpp logger.cpp config.cpp -o app_a && ./app_a
logger ctor
config ctor: logger.ready=1
```

We ran the other link order too, and the output matched the one above to the character. The order has passed out of the link order's hands and into the calling relationships': whoever depends on whom, the one being depended on is the one that gets constructed first. Since C++11 this construction carries its own thread safety — even under concurrent first calls, only one thread is really doing the constructing. Global objects inside a dynamic library take the same fix, and the construction order among the library's multiple translation units is likewise something nobody guarantees for you. As for the whole loading procedure, part 14 (Program Startup) will walk us through it for an entire piece; here we stop at the interface layer.

## Settling the Names: Two Encodings That Do Not Recognize Each Other, and One extern "C"

The three questions have all handed in their papers by now, and one old thread set up in part 03 remains: names. In part 03 we used c++filt to decode mangled names headed by _Z, and said that the scheme's birthplace is the Itanium C++ ABI; at the same time we left a question hanging: across compilers, across platforms, is it still the same encoding? Today the answer arrives: the Linux side uses one scheme, and the MSVC side uses another that does not recognize it at all. We take the same source and build it once on the Linux side:

```text
$ g++ -c test.cpp -o test_name.o && nm test_name.o
0000000000000021 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000000 T _ZN3Foo8someFuncEiPKc
```

For the MSVC-side specimen, I ran dumpbin on the same code on a Windows machine, and the symbols grew into what follows (compiler version 19.44.35217; these lines are my archived output from the time — we do not rerun it here on the Linux side):

```text
D:\DownloadFromInternet>dumpbin /SYMBOLS test.obj
00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))
```

The lead character is ?, @@ handles the segmentation, and the whole scheme shares no ancestry with the _Z one. GCC and Clang on Linux both use the Itanium scheme; MSVC is a family of its own, and the two families' symbols do not know each other on sight. So a C++ library that wants to serve users across compilers cannot get there by exporting mangled names directly — it has to switch to a naming both sides recognize: C names. In part 11 we already exported refund with `extern "C"`; once past the mangling gate, the symbol becomes the function's plain name, and you have seen the price: no overload can ever live under that name again. A few more details on the Windows side differ from ELF; in part 10 we compared the library-search order, and the rest gets handed in today, all in one go. The business of the import library .lib — one suffix carrying two identities — we already made acquaintance with in part 10: the static library is it, and the import library fitted to a DLL is it too. The remaining difference only shows its face at run time: what link time recognizes is .lib on both counts, and only at run time does the loader go summon the DLL proper. The export spellings are __declspec(dllexport) and __declspec(dllimport), one assigned to the occasion of building the library, the other to the occasion of using it, doing the same job as part 11's pair of mechanisms — the global default tuck-in plus the point-by-point release. The naming habit differs too: a DLL's file name usually carries no version number — mylib.dll is as far as the name ever goes. The work ELF does with the three-piece set to split generations, the Windows habit hands to the installation layout: different versions each live in a directory of their own.

`extern "C"` settles only the names; a header that can cross toolchains has to pin down the rest as well. We compress what matters into a four-piece set: function declarations, type definitions, macro constants, and include dependencies. We rework libcalc into one such version:

```c
/* calc_abi.h — the complete outward-facing ABI header: complete only when the four-piece set is all there */
#include <stddef.h>                  /* include dependency: where size_t comes from */
#define CALC_MAX_ITEMS 64            /* macro constant: a number the interface uses */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct calc_item {           /* type definition: the layout is written into the header, fixed */
    int units;
    int price;
} calc_item;
typedef struct calc_pool calc_pool;  /* opaque pointer: declared only, never defined */
int          calc_line_total(const calc_item* item);  /* function declaration */
calc_pool*   calc_pool_create(size_t n);
void         calc_pool_destroy(calc_pool* pool);
const char*  calc_build_id(void);
#ifdef __cplusplus
}
#endif
```

The four pieces do not all weigh the same, so let's take them in two grades. The load-bearing grade is function declarations paired with type definitions. Without the declarations, the caller knows neither the names nor the types. Without the type definitions, the compiler cannot work out calc_item's size, the arrangement of parameters on the stack and in the registers falls into disorder — and that is exactly the root of the accident at the scene of the break. The accompanying grade is macro constants and include dependencies; missing them does not turn hostile on the spot: the two sides each keep their own count, the header will not stand up, and the ailment lies buried in the dark. We build one caller each with gcc and g++, both linking the same library (only two lines picked out of nm's export table):

```text
$ gcc -fPIC -shared calc_abi.c -I. -o libcalcabi.so && gcc main_c.c -I. -L. -lcalcabi -o app_c && g++ main_cpp.cpp -I. -L. -lcalcabi -o app_cpp
$ nm -D libcalcabi.so | grep calc_
0000000000001166 T calc_build_id
0000000000001133 T calc_pool_create
$ LD_LIBRARY_PATH=. ./app_c && LD_LIBRARY_PATH=. ./app_cpp
C caller:    total=7500 id=calc-abi-demo
C++ caller:  total=10000 pool=ok id=calc-abi-demo
```

The two callers feed on the same library and the same header, and the exported names are all bare names. You should give the header's calc_pool one more look: it takes the opaque-pointer route. All the caller ever holds is a pointer — carry it around wherever you like, but the caller can neither build a calc_pool instance nor take one apart. However the layout changes inside the library, old code is never implicated — because it never touches the layout at all. Where the factory function builds the object, and how the built thing gets collected by name — that we leave for part 13 (Runtime Loading and Plugins), the main act of looking things up at run time.

## Which Changes Count as ABI Events

Now we can turn part 11's line — the less you export, the freer your hand to change things — into a concrete list. Anything that moves how an old binary fetches its data or how it is arranged counts as an ABI event. Adding a field into the middle of a struct — the scene of the break has already proven that one. Changing a parameter's type — we proved that with our two mismatched ABI pairs too. As for swapping a return type, reordering fields, or changing the alignment — there is no dodging those either; every one of them has to raise the official name. What does not count as an event: fixing a bug, adding a new function, adding comments to a header — a short walk of the realname's last two segments is enough.

The virtual-function matter we pull out and treat on its own. Add a first virtual function to a class that never had one, and the object's head has to take in a vtable pointer — the vtable being the virtual function table; we tested it, and the old exe silently printed garbage. For a leaf class that already has virtual functions and has nobody inheriting from it, appending new virtual functions at the end leaves every old vtable slot untouched: the old exe's results stay correct as ever, only the new functions lie out of its reach. KDE's binary compatibility policy — KDE being the Linux desktop environment project — hands out exactly this yardstick. Look back at part 11's three narrowing moves: what they narrowed was the length of this very list. The fewer exported names, the shorter the list, and the more places you can still move afterward. Paired with the upgrade discipline from before, a library's interface discipline is complete: narrow the outward-facing surface and export no variables; leave the structs alone, or move them only together with a generation bump; once a function signature is settled, do not change it — if you must, add a new one.

## The CMake Perspective

The chore of hand-typing the soname linker switch and hand-raising the symlinks, CMake takes over for us with a pair of target properties. The first is named VERSION, the other SOVERSION. We arrange the specimen into a small project:

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

VERSION records the realname's three segments; SOVERSION records the official name's head segment. We build and check: the three-piece set lies neat and whole inside build, the middle symlink's name is exactly SOVERSION's 1, the SONAME registered in the library is that same one, and the app runs bare, no environment variables needed (CMake writes an rpath into the build tree's app pointing at build — part 10 told of where that comes from):

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

generate_export_header is the patch of topic part 11's CMake section still owed, and today we make good — it generates a calc_export.h in the build directory, and we bring the load-bearing few lines out to look at:

```c
#    ifdef calc_EXPORTS
        /* We are building this library */
#      define CALC_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define CALC_EXPORT __attribute__((visibility("default")))
#    endif
```

calc_EXPORTS is defined by CMake only when the library itself is being built; on the using side it is absent. On Linux, both branches expand into the very same visibility attribute, because the ELF world does not separate importing from exporting. Configure the same pair of branches on Windows and they expand into __declspec(dllexport) and __declspec(dllimport) instead — one macro serving both ends. The half-manual work of point-by-point release from part 11, it does for us automatically (the global default tuck-in — that half part 11's CMake section already put under the target property). One thing it does not do, though: the `extern "C"` wrapper gets not one word of mention in the generated header, and writing it in the header file still falls to you — the pair of #ifdef __cplusplus back in calc_abi.h occupies exactly that position. For releasing, add one more line, install(TARGETS calc ...): the three-piece set goes into lib, the header goes into include, and as for the heavy work of splitting generations — the hand-done round earlier in this piece — you have now met every last piece of it.

## Next Up: Finding Functions at Run Time

Having walked this far, the interface's big matters all have their answers: the shape rests on the calling convention and the struct layout, the names rest on `extern "C"` paired with a complete ABI header, and the versions rest on the three-piece set plus version nodes. But all of them share one thing: the dependencies are written in at link time, and what NEEDED records is the name — the libraries must all be rounded up the moment the program starts. There is another way of working that skips that step: after the program is up, open a library file on demand and fetch functions out by name to use — that is how plugins live. That machinery for finding functions at run time is exactly the subject of part 13 (Runtime Loading and Plugins), and we leave the opaque pointer's factory waiting there for you.
