---
chapter: 13
difficulty: intermediate
order: 13
platform: host
reading_time_minutes: 18
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "Runtime Loading and Plugins: The Program Finds Libraries, Fetches Symbols, and Unloads"
description: "The program finds the library itself, fetches symbols by name, and unloads when done — the third way of using a library, one that only exists once the program is already running. A minimal example of the four dlopen functions, the clear-old-error/fetch/recheck three steps, the real compile comparison of casting void* to a function pointer under -Wpedantic, the three shapes of not-found compared byte for byte, RTLD_NOW refusing on the spot versus RTLD_LAZY crashing only at the call, link-time -z,defs set against dlopen's stance, the four-cell matrix of RTLD_GLOBAL/LOCAL crossed with LAZY/NOW, a plugin factory with an extern C entry plus an opaque pointer and a function table, one host and two backends swapping .so without a recompile, unload reference counting and dangling function pointers, RTLD_NEXT wrapping malloc to pick up part 09's preloaded tools, -ldl merged into libc since glibc 2.34 tested for real, the three Windows counterparts, and CMake's MODULE with an empty PREFIX."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/13-runtime-loading-plugins.md
  source_hash: 1385be99db0864a5cc91c73a9b33132ef0afa8d360df1b610bb9150fc0144b8b
  translated_at: '2026-10-13T04:26:14+00:00'
  engine: anthropic
  token_count: 6200
---

# Runtime Loading and Plugins: The Program Finds Libraries, Fetches Symbols, and Unloads

Up to this point in the volume, we have really only laid out two ways of using a library. Static libraries follow part 06's route: at link time the members get pulled bodily into the executable. Dynamic libraries follow the route part 08 set out: -lcalc registers the name into NEEDED, and at run time the dynamic linker brings the library up following NEEDED's list. What the two ways share sits right there: the dependency is nailed down at the moment of linking — every dependency the program acknowledges for its whole life is settled by the link command. Today we open a third way: the program is already running, and it loads a library into the process itself, fetches the functions it needs by name, and unloads the thing when it is done. For these jobs, POSIX supplies dlopen, dlsym, dlclose, plus dlerror — one `<dlfcn.h>` header covers the lot — and we will take them up one by one in a moment.

At the end of part 11's stretch on exported names, we left a promise behind: once the export table is cleaned up, how an interface crossing toolchains gets its names settled, and how the job of fetching functions by name with dlsym gets done — those had to come back to this piece. As for settling names, the previous part already established the ABI header's four-piece set as standard practice: function declarations, type definitions, macro constants, include dependencies. Today we put it to a real use: plugins. And this piece carries one more identity: what shape the undefined symbols that slipped past at link time blow up into, once the road switches to runtime loading — we watch those real outputs today as well. The specimen is still libcalc; the algorithm is identical to the old one, a base tax of 100 plus a rate of 17, with capping and rounding still producing 120 — only the backend has changed, from a library linked solid into a .so loaded at run time. My experiments all ran in WSL2, on the full kit of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, and every stretch of output in this piece was really run.

## Minimal Usage: Open the Library, Fetch the Symbol, Unload at the End

```cpp
// four.cpp excerpt — minimal use of the dlopen four, error branches omitted
void* handle = dlopen("./libcalc.so", RTLD_NOW);    // open the library, get a handle back; RTLD_NOW is one setting of flags, more on it later
dlerror();                                          // clear the old error state
int (*total_tax)(void) = (int (*)(void)) dlsym(handle, "total_tax");
const char* err = dlerror();                        // fetch the pointer, then check once more
std::printf("base=100 total=%d\n", total_tax());    // use it like an ordinary function
dlclose(handle);                                    // return the handle
```

```text
$ gcc -fPIC -shared libcalc.c -o libcalc.so
$ g++ -std=c++17 four.cpp -o four
$ ./four; echo $?
base=100 total=120
exit=0
```

Let's meet the four functions one by one. dlopen takes a path plus a set of flags, and once it has loaded the library into the process it hands back a void* — the jargon for which is a handle: every later request to fetch symbols or unload the library shows this very thing. dlsym takes the handle plus a string name and tracks down the symbol's address. dlclose hands the handle back, decrements the load reference count by one, and only when it reaches zero does the true unload happen. dlerror has the most peculiar temperament of the lot: it takes no arguments at all and exists purely to hand over the error text of the most recent dl-family call — and reading it once clears it to zero, so the moment we get it we have to store it in a variable. You may already have noticed that the three lines for fetching the symbol are written in a roundabout way. The empty dlerror() call exists to clear the old error state; dlsym is the one doing the real work of fetching the pointer; and the final dlerror handles the recheck. The roundaboutness traces back to dlsym's failure signal not being enough: on failure it returns NULL, yet a symbol's address can genuinely be NULL — say an exported variable whose value happens to be zero — and from the return value alone we cannot tell the two cases apart. One more little mechanism deserves a naming: dlerror clears to zero after a single read, so reading it twice in a row loses the error — the first version of my host printed (null) exactly there.

One more spot may be nagging at you: what dlsym hands back is a void* — a genuine, bona fide object pointer — and we cast it hard into a function pointer. ISO C++ makes no guarantee for such a conversion, the C standard simply forbids it, and yet POSIX gives a promise: a void* obtained from dlsym may be converted to a function pointer and used. Which side does the compiler take? We compiled the same source once saved as .c and once as .cpp, with -Wpedantic, -Wall, and -Wextra all turned up full:

```text
$ g++ -std=c++17 -Wpedantic -Wall -Wextra -c ped_cast.cpp -o /dev/null; echo $?
0
$ gcc -std=c17 -Wpedantic -Wall -Wextra -c ped_cast.c -o /dev/null; echo $?
ped_cast.c: In function 'main':
ped_cast.c:5:30: warning: ISO C forbids conversion of object pointer to function pointer type [-Wpedantic]
    5 |     int (*total_tax)(void) = (int (*)(void)) dlsym(handle, "total_tax");
      |                              ^
0
```

The same conversion gets different treatment on the two sides: C catches a warning, while C++ answers with perfect silence. The root of the difference sits in the standards' wording: C defines it as non-conforming, so the compiler must issue a diagnostic. C++ files it under conditionally-supported — an implementation that supports it makes it legal as a matter of course, and speaking up or not is not required. If you truly want portability down to the last letter, copy the address into the function pointer with memcpy, and both standards nod. For everyday code running on Linux and macOS, just cast directly — POSIX has your back.

## The Three Shapes of Not Found

What do the errors of this machinery look like? We go looking for trouble three times on purpose. You met the link-time undefined reference back in the linker part; on the runtime-loading road, things that cannot be found show up in three different shapes.

```text
$ ./miss 1
handle=(nil)
dlerror: ./libnope.so: cannot open shared object file: No such file or directory
$ ./miss 2
sym=(nil)
dlerror: ./libcalc.so: undefined symbol: base_taxx
```

The first round of trouble is the library file itself not existing: dlopen hands back NULL, the dlerror text reads at a glance, and the error's lead is exactly the path string we passed in. Both rounds are failures caught by the program, and the exit codes are a uniform 0. Here let us correct a widely circulated recipe: old tutorials often have you export LD_LIBRARY_PATH before running, then dlopen a full path containing a slash — the two things actually do not go together. With a slash in the name, dlopen treats it as an ordinary path and opens it directly; the whole machinery prepared for searching never comes into play, and the environment variable has no way to help. Only when the name carries no slash does the search step on stage — the ladder of settings from environment variable to system cache was part 10's material, and we will not unfold it here. The second round's trouble is in the name: the library sits there perfectly fine, but the name is wrong — we deliberately typed base_tax with an extra x, and dlsym hands back NULL. The error's lead follows whichever side holds the name, the same lesson as the link-time undefined reference whose lead is the object file. The third round is the most interesting: the library itself references a function nobody defines. We write a gap.c whose exported gap_call calls missing_thing, nobody in the whole project provides a definition, and we put no restraints on it at link time either:

```text
$ gcc -shared -fPIC gap.c -o libgap.so
$ ./gap_host N; echo $?
dlopen NULL, dlerror: ./libgap.so: undefined symbol: missing_thing
exit=1
$ ./gap_host L; echo $?
./gap_host: symbol lookup error: ./libgap.so: undefined symbol: missing_thing
exit=127
$ stdbuf -oL ./gap_host L; echo $?
dlopen ok
./gap_host: symbol lookup error: ./libgap.so: undefined symbol: missing_thing
exit=127
```

The same bad library run twice with different flags comes to completely different ends. RTLD_NOW makes dlopen resolve every symbol that needs resolving on the spot: missing even one and it refuses as a whole, handing back NULL with the error text laid out plainly before you. RTLD_LAZY is far more forgiving — resolution of function symbols is put off until the first call — so dlopen passes and the crash lands on the calling line, with the process exiting 127. Note also the fine detail of the stdbuf run: on a default pipe you do not even see dlopen ok — it did execute, but stdout's block buffer had not been flushed before the process died; only with line buffering switched on does it show. Across the handling of the same defect we have now seen three stances, each governing a different stretch. The linker is by default lenient toward undefined symbols in a .so, because resolving them jointly with other libraries is the normal state of affairs — the `-Wl,-z,defs` that showed up in part 11 is the switch you tighten by hand; we link gap.c with it added and it reports undefined reference on the spot, this time with missing_thing in the lead. dlopen's RTLD_NOW is the last line of defense at the moment the library enters the process, and from it the engineering habit of plugin writing finds its footing: load plugins with RTLD_NOW, always, so a missing-symbol error lands on the dlopen line instead of dragging on to crash when the user clicks some feature.

## Two flag Axes: GLOBAL Decides Who Sees, LAZY Decides When

dlopen's second parameter is flags. One axis picks the timing of resolution — the LAZY and NOW we just met; the other picks who gets to use the symbols, where the choices at hand are RTLD_GLOBAL and RTLD_LOCAL. Our specimens are two plugins that cooperate: A exports a plug_a_value, and B references it. The host is invoked as ./matrix plus two letter strings, the first picking A's flags and the last picking B's: the letter G stands for GLOBAL, L for lazy, N for in-a-hurry; without a G it is LOCAL. We ran all four cells:

```text
$ ./matrix LN N; echo $?
B dlopen NULL: ./libplugb.so: undefined symbol: plug_a_value
exit=1
$ ./matrix GN N; echo $?
sum=111
exit=0
$ ./matrix LL L; echo $?
./matrix: symbol lookup error: ./libplugb.so: undefined symbol: plug_a_value
exit=127
$ ./matrix GL N; echo $?
sum=111
exit=0
```

What the two axes do, the four cells of output lay out plainly. GLOBAL versus LOCAL decides whether B can see A when resolving symbols: LOCAL is the default setting, the library's exports stay out of the process's global symbol pool, and B's search for plug_a_value comes up empty. GLOBAL hangs A into the pool, B's reference finds its footing, and 111 is just 100 plus 11. You actually met that pool long ago — it is the very list part 11 spoke of when covering lookup order: the executable at the head, preloaded libraries following, NEEDED entries laid out in order, and libraries loaded with GLOBAL join the queue and take part too. LAZY versus NOW works here exactly as in shape three last section: paired with LOCAL, NOW reports right at dlopen, while LAZY drags on to the call and only then closes with a symbol lookup error. Half a sentence more needs pointing out, because one spot in the four cells is easy to misread: once GLOBAL has delivered the symbols, LAZY and NOW get the same result — the 111 on the GL line is not NOW's doing; it rests on A having entered the global pool, and the timing of resolution changed nothing here. When do you use GLOBAL? A set of plugins with a clear division of labor that reference one another is the candidate — say several plugins all depend on one common component; load that component with GLOBAL. Isolation between plugins is the norm, which is exactly why LOCAL was made the default; when in doubt, leave the G out.

## The Plugin Factory: One Function Table, One Opaque Pointer

Calling dlsym function pointer by function pointer is tolerable at two interfaces; at a dozen-plus it is asking for punishment: names ride on strings, types ride on hand-written casts, and a single mistake waits until runtime to show itself. The standard engineering move is to put the previous part's ABI-header method to work. Host and plugin share one header with the full four-piece set; we turn libcalc's backend into a plugin, and the header looks like this:

```cpp
// calc_plugin.h excerpt — the one ABI header shared by host and plugin
#define CALC_ABI_VERSION 1                       // macro constant: the interface's generation number

struct CalcBackend;                              // type definition: opaque pointer, declaration only

struct CalcPluginApi {                           // function table: the host recognizes only this table
    int abi_version;
    CalcBackend* (*create)(void);
    void         (*destroy)(CalcBackend*);
    int (*base_tax)(const CalcBackend*), (*tax_rate)(const CalcBackend*);   // the fetching pair, same shape
    int          (*total_tax)(const CalcBackend*);
};
const CalcPluginApi* calc_plugin_entry(void);    // function declaration: the entry, wrapped in extern "C"
```

Reading from the top down. CALC_ABI_VERSION's identity is a macro constant: host and plugin each get the same number at their own compile time, and any mismatch refuses cooperation. CalcBackend has a declaration but no definition; a pointer like this is what the jargon calls an opaque pointer: the host knows the thing exists yet cannot touch its insides — it cannot even write sizeof, and all it can do is pass the pointer back exactly as it arrived. In the middle, CalcPluginApi is a function table whose fields are all function pointers: create builds the backend, destroy takes the backend down, and the remaining three are the numbers we have computed all volume long. Only one function is truly exported, calc_plugin_entry, its declaration wrapped with `extern "C"` and `#ifdef __cplusplus` in the previous part's manner, so both the C and C++ ends can include it. That part told us decorated names belong to the compiler; when a module fetches a function by name across the boundary, the name must be bare. Types do not take decoration, though — only the pointer's value crosses the border — so the function table may happily hold C++ functions.

```cpp
// plugin_standard.cpp excerpt — the standard backend; CalcBackend's definition lives only on this side
struct CalcBackend { int base; int rate; };      // completes the opaque declaration from the header
static CalcBackend* create_impl() { return new CalcBackend{100, 17}; }
static int total_tax_impl(const CalcBackend* self) {   // destroy/base_tax same shape, omitted
    int amount = self->base + self->rate; if (amount > 500) amount = 500;   // clamp_cap, same algorithm as part 11
    return (amount + 9) / 10 * 10;               // near_ceil
}
static const CalcPluginApi kApi = { CALC_ABI_VERSION, create_impl, /* the rest same shape */ };
extern "C" const CalcPluginApi* calc_plugin_entry(void) { return &kApi; }
```

The plugin side completes CalcBackend's definition, its members carrying just the base tax and the rate, the cap-and-round algorithm moved into total_tax_impl, the numbers identical to the originals to the last digit. One wreck I have genuinely driven into deserves recording: do not put the struct in an anonymous namespace. That creates a new type visible only inside the anonymous space, which fails to match the global CalcBackend in the header, the function pointers' types instantly stop mating up, and the compiler reports invalid conversion on the spot — marking the functions static is enough. The host side we keep clean: the entire source includes only the one header calc_plugin.h, and not one plugin appears among the link inputs. Fetching the entry still follows the three steps — clear the old error, fetch the symbol, recheck at the end — and the table we get back also gets its generation checked once:

```cpp
// host.cpp excerpt — the host: includes the ABI header only, links no plugin
const CalcPluginApi* api = entry();              // entry comes from dlsym; error checking same as four.cpp
if (api->abi_version != CALC_ABI_VERSION) {      // generation mismatch, refuse on the spot
    std::fprintf(stderr, "ABI 版本不符: 要 %d, 来 %d\n", CALC_ABI_VERSION, api->abi_version);
    return 1;
}
CalcBackend* backend = api->create();
std::printf("base=%d total=%d\n", api->base_tax(backend), api->total_tax(backend));
api->destroy(backend); dlclose(handle);          // wrap-up: return the object, return the handle
```

```text
$ g++ -std=c++17 host.cpp -o host
$ ./host ./standard.so; echo $?
base=100 total=120
exit=0
$ ./host ./discount.so; echo $?
base=80 total=90
exit=0
```

Two identical-shaped -fPIC -shared commands produced standard.so and discount.so; plugin_discount came out of the same mold as the standard version, with just the backend's two numbers swapped to 80 and 9, so we will not paste it again. Everything we wanted to see ran out: the same host binary, not one byte recompiled, swaps its algorithm just by swapping a .so, and base=100 total=120 ties cleanly onto the earlier baseline. The host's NEEDED does not contain a single plugin's name — the dependency has become a runtime path argument, and that is, at bottom, where the power of plugins lives. And what if the entry forgets its `extern "C"`? We really built a version without it, took a look at the export table with the nm -D we met in part 11, and let the host fetch by name:

```text
$ nm -D mangled.so | grep entry
00000000000011f2 T _Z17calc_plugin_entryv
$ ./host ./mangled.so; echo $?
dlsym: ./mangled.so: undefined symbol: calc_plugin_entry
exit=1
```

_Z17calc_plugin_entryv is its new name — the decorated name whose decoding the compiler part taught has shown up again. dlsym asks for the bare name and naturally comes up empty. Visible in the export table yet unreachable by name — the promise part 11 left in its stretch on exported names is redeemed right here. One more relationship deserves pointing out: CalcBackend was created by the plugin's new, so why should the host be the one to collect it? It isn't, actually — destroy is also a function in the plugin, and the host merely passes the pointer back the way it came. The one thing to remember: whichever module the object was born in, the power to finish it off belongs to that module; the paired create and destroy each keep to their own side. A concrete mechanism sits behind this: two modules in one process do not necessarily share the same operator new — say the plugin statically linked its own runtime; a delete reaching across is undefined behavior. The opaque pointer guards the boundary between modules tight and thorough: the host does not even have the complete type, so wanting to overstep, it could not even write the code. The factory usage the previous part left behind is now complete.

## Unloading: Reference Counts and Dangling Function Pointers

A plugin's virtue lies in being unloadable, but the mechanical details of unloading are worth watching with our own eyes twice. The first look is at the reference count: a second dlopen of the same path no longer truly loads — it just bumps the count by one, and dlclose correspondingly only subtracts one, with a true unload only at zero. We gave the plugin a global object that prints its load and unload, then had the host dlopen twice and dlclose twice, still using it in the gap between:

```text
$ ./refcount; echo $?
[trace] 装进进程
h1=0x5e1672763040 h2=0x5e1672763040 同一?yes
第一次 dlclose 返回 0
dlclose 之后还能用: total=120
[trace] 卸出进程
第二次 dlclose 返回 0
main 收尾
exit=0
```

The handle's address differs every run; we keep this run's as-is. The evidence is all in the ordering: loaded-into-process printed once, and the two dlopen calls got back the same handle. The first dlclose amounted only to decrementing: total_tax still produced 120, the library alive and well. The unloaded-from-process print sits between the second dlclose and main's wrap-up — the true unload happened on the second pass. This counting idea runs on the same rails as shared_ptr: take several handles, and unloading requires returning just as many. The second look is the dark side: fully unloaded, the cached function table still in hand, we insist on calling it anyway:

```text
$ ./dangling; echo $?
[trace] 装进进程
[trace] 卸出进程
已 dlclose,再调 base_tax
exit=139
```

We ran it five times straight and got 139 all five times — its provenance is 128 plus 11, the kernel's old encoding for reporting SIGSEGV. dlclose reached zero and the library's mapping came down whole: the function table, the backend object, that stretch of code, all gone; every pointer in our hands is now dangling. So a plugin system has one discipline: after dlclose, every pointer obtained from that library is void — the function table, the objects, the strings handed out of static resources, not one may be touched again. For plugins that truly live as long as the process, many projects simply never call dlclose: the loaded state stays until process exit, and the whole business of reclamation is handed to the operating system, which is, if anything, less to worry about.

## RTLD_NEXT: Wrapping malloc

In part 09 we used LD_PRELOAD to knock base_tax out entirely, and even then we left a note: a good number of the memory-profiling kind of tools got their start by preloading their own malloc and free. Knocking out is actually easy; what a wrapping tool wants is more than knocking out — it wants a layer wrapped around: after tallying, the work still has to be handed to the real malloc. Handed to whom? That is the job of RTLD_NEXT, the special pseudo-handle: it has dlsym keep searching from the modules sitting after the current library in the search order, taking the next same-named definition. On part 11's list, the preloaded mwrap sits ahead of libc; counting forward from mwrap's position, the next house is exactly the real malloc, and the forwarding connects. Here are the key lines:

```c
// mwrap.c excerpt — the preloaded layer: tallies malloc/free, forwards everything else to the next house
static void* (*real_malloc)(size_t);
static char   boot_pool[1 << 16];          /* a crutch during resolution: static memory */
static int    resolving = 0;
static void resolve(void) {
    resolving = 1; real_malloc = (void* (*)(size_t)) dlsym(RTLD_NEXT, "malloc"); resolving = 0;
}
void* malloc(size_t n) {
    if (real_malloc == 0) { if (resolving) return boot_alloc(n); resolve(); }   /* re-entry defense, explained in the text */
    return real_malloc(n);                 /* count, then forward untouched */
}
```

We have the report go through the write system call inside a destructor, bypassing printf, so that the act of printing itself does not march back into malloc. Let's run it:

```text
$ gcc -shared -fPIC mwrap.c -o mwrap.so
$ LD_PRELOAD=$PWD/mwrap.so ./host ./standard.so; echo $?
mwrap: malloc 过 80766 字节, free 调用 11 次
base=100 total=120
exit=0
```

The host computed its usual 120, and the tally came in too: this process pushed 80766 bytes through malloc from end to end, counting even the allocations libstdc++ starts up with. As for where that small boot_pool patch comes from, we need a little history: before glibc 2.34, dlsym lived in a separate libdl, and its internal allocation while resolving symbols would march straight back into the preloaded layer's malloc — the real function pointer not yet in hand — and it spun in place, until a patch of static memory broke the circle. And on today's glibc 2.44? We measured with a counting probe: during resolution the preloaded layer's malloc is not called once; delete the crutch branch entirely and rerun, and the 80766 comes out character-identical, because libc's internal allocations go through internal aliases and never pass the PLT, so the preloaded layer's interception has no way to happen. Is the defense still worth writing? It is: the crutch trick does not pick its implementation — swap in a platform or link layout that still goes through the PLT, and it is the gate that stops the loop. This is the classic checkpoint of writing interposing tools; open the sources of ASan (AddressSanitizer, the memory-error checking tool) or heaptrack (a heap memory profiler) someday and you will run into it right at the door.

## -ldl, Then and Now

In old tutorials' build commands, the linking step always trails a -ldl; this piece never wrote it once from start to finish, and every program still linked. Where does the dl family live in today's glibc? We query libc's export table directly:

```text
$ nm -D /usr/lib/libc.so.6 | grep -E ' T (dlopen|dlsym|dlclose|dlerror)@@'
0000000000093c90 T dlclose@@GLIBC_2.34
0000000000093cd0 T dlerror@@GLIBC_2.34
0000000000094520 T dlopen@@GLIBC_2.34
00000000000945e0 T dlsym@@GLIBC_2.34
```

```text
$ ls -l /usr/lib/libdl.so 2>&1; echo $?
ls: cannot access '/usr/lib/libdl.so': No such file or directory
2
$ nm /usr/lib/libdl.a; echo $?
0
$ readelf -d /usr/lib/libdl.so.2 | grep -E 'SONAME|NEEDED'
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000e (SONAME)             Library soname: [libdl.so.2]
```

The version numbers trailing the four names tell the story: since glibc 2.34, the once-independent libdl has been merged into libc, and on our 2.44 the merge is already years behind us. The development-side remains we also inspected: libdl.so, the development-side link name, is a file that no longer exists, while libdl.a remains as an empty archive — nm reads not a single symbol out of it. libdl.so.2 is just a compatibility shell left for old binaries' NEEDED, and it depends on libc alone. So today, writing -ldl or not makes no difference: written, what gets linked is that empty archive — we tested a program with -ldl added and its NEEDED list came out byte-identical to without. The old tutorials telling you to write it were not writing it for nothing: before 2.34, libdl.so was a real library and dlopen's implementation lived inside it; missing it, the linker would hand you back an undefined reference, with that unfetchable `dlopen' in the lead. The old tutorials were not actually wrong — they were facing the glibc of their day. Today this dependency lives merged into libc, and the artifact comes out the same written or not.

## The Windows Counterparts

On Windows the whole set has a neat row of counterparts: LoadLibraryA and LoadLibraryW stand against dlopen, GetProcAddress against dlsym, FreeLibrary against dlclose, and the handle's type is called HMODULE. Error querying takes two steps: the first, GetLastError, gets the error code; the second, FormatMessage, turns the code into text — none of the one-step convenience of our dlerror here. When hunting export names, dumpbin's /EXPORTS does nm -D's job: checking whether a function is in the export table. There is one more contraption left over from the 32-bit era: the `__stdcall` calling convention decorates the exported name into the shape of `_name@12`, carrying the parameter byte count, and GetProcAddress asking for the bare name then comes up empty — the same happens when the declaration's convention does not match. On the search side, the loader searches the EXE's own directory first; the two platforms' opposite ladders were compared in part 10, and we will not repeat them today.

## The CMake View

Hand-typing -fPIC -shared for plugin builds — day to day, that work goes to CMake. Plugins have a dedicated library type in CMake, MODULE: the artifact is equally a .so, but the semantics are for dlopen's use, not for linking. Here is the project:

```cmake
# CMakeLists.txt excerpt
add_library(standard MODULE plugin_standard.cpp)
set_target_properties(standard PROPERTIES PREFIX "")   # discount same shape
add_executable(host host.cpp)
```

```text
$ cmake -B build -S . && cmake --build build
$ ./build/host ./build/standard.so
base=100 total=120
$ ./build/host ./build/discount.so
base=80 total=90
```

The distinction between MODULE and SHARED, CMake guards by itself: put a MODULE target into target_link_libraries and configure refuses on the spot — we truly tried it once:

```text
CMake Error at CMakeLists.txt:8 (target_link_libraries):
  Target "standard" of type MODULE_LIBRARY may not be linked into another
  target.  One may link only to INTERFACE, OBJECT, STATIC or SHARED
  libraries, or to executables with the ENABLE_EXPORTS property set.
```

The error message says it all: MODULE's whole position is to not be linked. This guard meshes exactly with our semantics: the host should not be linking plugins at all — link one, and the dependency is nailed down again, and all the earlier work is wasted. The empty string for PREFIX governs the artifact's name: a plugin's name is read by people and by config files, and the lib prefix is pure surplus; remove it and standard.so comes out clean and tidy. We paged through the --verbose compile lines: MODULE automatically carries -fPIC, getting the same treatment as SHARED, without a character of your attention. As for the little business of finding plugins at run time, the BUILD_RPATH mechanism was covered in part 10's CMake section — and since our plugin paths travel as arguments, we usually have no use for it. If you want the exported header to serve across Linux and Windows both, CMake's generate_export_header can generate a header that works on both ends — the previous part's CMake section really ran it, and we will not replay it.

## Next: How the Process Gets Itself Started

Everything this piece did, the process did with its own hands while alive: dlopen found the library by itself, dlsym fetched the symbols by itself, and the timing of loading sat in your code's grip. But how does the process get itself from one ./host line to main? System calls of the execve kind are responsible for swapping the on-disk program in as a new process; once swapped, control goes to the dynamic linker registered in the executable's header, which raises the whole dependency tree following NEEDED's list; relocations finished, it is _start's turn to initialize the runtime library, and only at the very end does execution reach main. The interpreter glimpsed from afar in the opening part, the NEEDED read line by line in part 08, the dynamic linker filling the GOT in part 09 — all of them are players in this flow. How does the whole road go? The next part is devoted entirely to it; see you there.
