---
chapter: 13
difficulty: intermediate
order: 13
platform: host
reading_time_minutes: 31
tags:
- cpp-modern
- host
- intermediate
title: "Runtime Loading and Plugins: The Program Finds Libraries, Fetches Symbols, and Unloads"
description: "The program finds the library itself, fetches symbols, then unloads — a third way of using libraries that only exists once the program is up and running. A minimal example of the four dlopen functions, the three-step clear-the-old-error, fetch, recheck routine, void* cast to a function pointer with the real compile comparison under -Wpedantic, the three shapes of not-found compared byte for byte, RTLD_NOW refusing on the spot against RTLD_LAZY crashing only at the call, link-time -z,defs set against dlopen's stance, the four-cell matrix of RTLD_GLOBAL/LOCAL crossed with LAZY/NOW, a plugin factory with an extern C entry plus an opaque pointer and a function table, one host and two backends swapping .so files without a recompile, unload reference counting and dangling function pointers, RTLD_NEXT wrapping malloc to carry on part 09's preloaded tools, -ldl's merger into libc since glibc 2.34 tested for real, the Windows counterpart mechanisms, and CMake's MODULE with an empty PREFIX."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/13-runtime-loading-plugins.md
  source_hash: cb88eec95d1b835083f9bc3e7bf5ab6b0f5f75d2b4200667bb1177c2c558529a
  translated_at: '2026-10-02T10:05:00+00:00'
  engine: glm
  token_count: 5366
---

# Runtime Loading and Plugins: The Program Finds Libraries, Fetches Symbols, and Unloads

Through this volume so far, we have really only laid out two ways of using a library. The static-library route was part 06's: at link time the members get pulled bodily into the executable. The dynamic-library route runs from part 08 onward: -lcalc registers the name into NEEDED, and at run time the dynamic linker brings the library up following NEEDED's list. What the two ways share sits right there: the dependency is fixed at the moment of linking — every dependency a program acknowledges in its whole life is settled by the link command. Today we open the third way: the program is already running, and it loads a library into the process itself, fetches the functions it wants by name, and unloads the thing when done. For these jobs, POSIX supplies dlopen, dlsym, dlclose, plus dlerror — one <dlfcn.h> header covers the lot — and we will take them up one by one in a moment.

At the end of part 11's stretch on exporting names, we left a promise behind: once the export table is cleaned up, how an interface crossing toolchains gets its names settled, and how the job of dlsym fetching functions by name gets done — those had to come back to this piece. The naming method, part 12 already established as standard practice with the ABI header's four-piece set: function declarations, type definitions, macro constants, include dependencies. Today we put it to a real use: plugins. And this piece carries one more identity: what shape the undefined symbols that slipped past at link time blow up into, once the road switches to runtime loading — we watch those real outputs today as well. The specimen is still libcalc; the algorithm is identical to the old one, the base tax of 100 plus the rate of 17, with capping and rounding still producing 120 — only the backend has changed, from a library linked solid into a .so loaded at run time. My experiments all ran in WSL2, on the full kit of GCC 16.2.1, binutils 2.47, glibc 2.44, and CMake 4.4.3, and every piece of output in this piece was really run.

## Minimal Usage: Open the Library, Fetch the Symbol, Unload at the End

```cpp
// four.cpp (excerpt) — the minimal use of the four dlopen functions, error branches omitted
void* handle = dlopen("./libcalc.so", RTLD_NOW);    // open the library, get a handle back; RTLD_NOW is one of the flags settings, covered below
dlerror();                                          // clear the old error state
int (*total_tax)(void) = (int (*)(void)) dlsym(handle, "total_tax");
const char* err = dlerror();                        // fetch the pointer, then check once more
std::printf("base=100 total=%d\n", total_tax());    // use it like an ordinary function
dlclose(handle);                                    // hand the handle back
```

```text
$ gcc -fPIC -shared libcalc.c -o libcalc.so
$ g++ -std=c++17 four.cpp -o four
$ ./four; echo $?
base=100 total=120
exit=0
```

Let's get acquainted with the four functions one at a time. dlopen takes a path plus a set of flags, loads the library into the process, and hands back a void* — the trade name for it is a handle: everything afterward, fetching symbols and unloading, is done by showing it. dlsym takes a handle plus a string name, looks up the symbol's address, and returns it. dlclose hands the handle back, decrements the load's reference count, and only when it reaches zero is the library really unloaded. dlerror has the oddest temperament: it takes no parameters at all, exists purely to hand out the error text of the most recent dl-family call, and one read zeroes it out — so once we have the text, it has to go into a variable. The three lines that fetch the symbol read in a roundabout way, and you may have already noticed. The bare dlerror() run is there to clear the old error state; dlsym does the real business of fetching the pointer; the final dlerror does the recheck. The root of the detour is that dlsym's failure signal is not enough: on failure it returns NULL, yet a symbol's address genuinely can be NULL — an exported variable whose value happens to be zero, say — and from the return value alone we cannot tell the two cases apart. One more small mechanism deserves naming: dlerror zeroes itself on read, so reading it twice in a row loses the error — the first version of my host printed (null) right there.

One more spot may be nagging at you: what dlsym returns is a void*, a genuine object pointer, and we forcibly cast it to a function pointer. ISO C++ gives no guarantee for such a cast, the C standard flat-out forbids it, yet POSIX makes a promise: a void* obtained from dlsym may be cast to a function pointer and used. Which side does the compiler stand on? We saved the same source as .c and .cpp and compiled each once, with -Wpedantic, -Wall, and -Wextra all fully on:

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

The same cast gets forked treatment: C took a warning, while on the C++ side the reaction is dead silence. The root of the difference is written in the standards' wording: C rules it nonconforming, so the compiler must issue a diagnostic. C++ classifies it as conditionally supported — an implementation that supports it makes it legal as a matter of course, and whether it speaks up is not enforced. If you want portability to the last letter, copy the address into the function pointer with memcpy, and both standards nod. For everyday code running on Linux and macOS, just cast directly — POSIX has our back.

## The Three Shapes of Not Found

What do this kit's failures look like? Let's go looking for trouble three times on purpose. The link-time undefined reference you met back in part 05; on the runtime-loading road, the thing that cannot be found puts on three different shapes.

```text
$ ./miss 1
handle=(nil)
dlerror: ./libnope.so: cannot open shared object file: No such file or directory
$ ./miss 2
sym=(nil)
dlerror: ./libcalc.so: undefined symbol: base_taxx
```

The first trouble we went looking for is the library file itself not existing. dlopen handed back NULL, the dlerror text you can read at a glance, and the error's headline is exactly the path string we passed in. Both failures were caught by the program, and the exit codes are a uniform 0. Here let's correct a widely circulated habit: old tutorials often have you export LD_LIBRARY_PATH before running, then dlopen a slash-bearing full path — these two things cannot actually meet. When the name carries a slash, dlopen treats it as an ordinary path and opens it directly; the whole machinery prepared for searching never happens at all, so the environment variable's help has nothing to attach to. Only when the name carries no slash does the search take the stage — the run of ranks from the environment variable down to the system cache was laid out in part 10, and we will not unfold it here. The second round's trouble lies in the name: the library is sitting there perfectly fine, but the name was taken wrong — we deliberately typed one x too many into base_tax, dlsym handed back NULL, and the error's headline follows the party holding the name, which tells the same story as the link-time undefined reference's headline being the object file. The third round is the most interesting: the library itself references a function nobody defined. We write a gap.c whose exported gap_call calls missing_thing, nobody in the whole project gives its definition, and at link time we apply no restraint either:

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

The same broken library run twice under different flags meets completely different ends. RTLD_NOW has dlopen look up every symbol due for resolution on the spot: one missing and the whole load is refused, what comes back is NULL, and the error text sits plainly in front of your eyes. RTLD_LAZY's temperament is far more forgiving — function-symbol resolution is deferred until the first call, so dlopen passes the gate and the crash lands on the calling line, with the process exiting 127. Note the small detail in the stdbuf round: under the default pipe you cannot see even the dlopen ok — it really did execute, but stdout's block buffering had not been flushed before the process died; switch line buffering on and it shows itself. On handling the same defect we have now met three stances in a row, each governing its own stretch. The linker is by default tolerant of undefined symbols in a .so, because resolving them together with other libraries is the norm; the `-Wl,-z,defs` that showed its face in part 11 is the switch you tighten on purpose — add it when linking gap.c and the undefined reference is reported on the spot, with missing_thing as the lead. dlopen's RTLD_NOW is the last line of defense at the moment of loading into the process, and the engineering habit for writing plugins finds its footing there: always load plugins with RTLD_NOW, so a missing-symbol error lands on the dlopen line instead of blowing up when the user happens to click on some feature.

## Two Flag Axes: GLOBAL Decides for Whom, LAZY Decides When

dlopen's second parameter is flags. One axis picks the timing of resolution — the LAZY and NOW just seen — and the other axis picks who the symbols are for; what you hold to choose from there is RTLD_GLOBAL and RTLD_LOCAL. We take two plugins that cooperate with each other as the specimen: A exports a plug_a_value, and B references it. The host's usage is ./matrix plus two strings of letters, the first string picking A's flags and the last picking B's: the letter G stands for GLOBAL, L for lazy, N for eager; leaving out G means LOCAL. We ran all four cells:

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

What the two axes do, the four cells of output lay out plainly. GLOBAL and LOCAL decide whether B can see A when it resolves symbols: LOCAL is the default setting — the library's exports do not enter the process's global symbol pool, and B's search for plug_a_value comes up empty. GLOBAL hangs A into the pool, B's reference finds its footing, and the 111 is worked out as 100 plus 11. That pool you actually met long ago: it is the very list from part 11's telling of the search order — the executable at the head, the preloaded libraries following, the NEEDED entries lined up in order — and a library loaded GLOBAL joins the queue and takes part too. What LAZY and NOW do here matches the third-shape scene in the previous section: paired with LOCAL, NOW reports on the spot at dlopen, while LAZY drags things out to the moment of calling, and only then closes the scene with a symbol lookup error. One more half-sentence we must make explicit, because the four cells hold a spot easy to misread: once GLOBAL has delivered the symbols, LAZY and NOW land the same result — the 111 on the GL line is not NOW's credit; what does the work is A hanging in the global pool, and the timing of resolution changed nothing about the outcome here. When to use GLOBAL? A set of plugins with a clear division of labor that reference one another is a candidate — several plugins all depending on one common piece, say; load the common piece GLOBAL. Isolation between plugins is the norm, which is exactly why LOCAL was made the default: when we are unsure, we leave the G out.

## The Plugin Factory: One Function Table, One Opaque Pointer

dlsym-ing function pointers one at a time is tolerable with two interfaces; with a dozen it is asking for suffering: the names ride on strings, the types on hand-done casts, and one mistake waits until run time to surface. The standard engineering move is to land the ABI-header method part 12 established. What the host and the plugin share is one header complete with the four-piece set; we rebuild libcalc's backend into a plugin, and the header looks like this:

```cpp
// calc_plugin.h (excerpt) — the one and only ABI header the host and the plugin share
#define CALC_ABI_VERSION 1                       // macro constant: the interface generation number

struct CalcBackend;                              // type definition: an opaque pointer, declaration only

struct CalcPluginApi {                           // function table: the host recognizes this table alone
    int abi_version;
    CalcBackend* (*create)(void);
    void         (*destroy)(CalcBackend*);
    int (*base_tax)(const CalcBackend*), (*tax_rate)(const CalcBackend*);   // the value-fetching pair, same shape
    int          (*total_tax)(const CalcBackend*);
};
const CalcPluginApi* calc_plugin_entry(void);    // function declaration: the entry, wrapped in extern "C"
```

Let's read it from the top. CALC_ABI_VERSION's identity is a macro constant: the host and the plugin each get the same number at compile time, and when the numbers do not match, cooperation is refused. CalcBackend has a declaration but no definition — a pointer of this kind goes by the trade name opaque pointer: the host knows such a thing exists yet cannot touch its insides: it cannot so much as write a sizeof, and all it can do is pass the pointer back untouched. The CalcPluginApi in the middle is a function table whose fields are all function pointers: create builds the backend, destroy takes the backend back, and the remaining three are the numbers we have been computing for a whole volume. The one genuine export is calc_plugin_entry, its declaration wrapped in `extern "C"` and `#ifdef __cplusplus` by part 12's technique, so both the C and C++ ends can include it. As we said in part 12, a mangled name answers to its compiler: when functions are fetched by name across modules, the name has to be bare. Types, though, do not take the mangling — only the pointer's value crosses the border — so the function table may load C++ functions to its heart's content.

```cpp
// plugin_standard.cpp (excerpt) — the standard backend; CalcBackend's definition lives only on this side
struct CalcBackend { int base; int rate; };      // completes that opaque declaration from the header
static CalcBackend* create_impl() { return new CalcBackend{100, 17}; }
static int total_tax_impl(const CalcBackend* self) {   // destroy/base_tax same shape, omitted
    int amount = self->base + self->rate; if (amount > 500) amount = 500;   // clamp_cap, part 11's algorithm
    return (amount + 9) / 10 * 10;               // near_ceil
}
static const CalcPluginApi kApi = { CALC_ABI_VERSION, create_impl, /* the rest same shape */ };
extern "C" const CalcPluginApi* calc_plugin_entry(void) { return &kApi; }
```

On the plugin side, CalcBackend's definition is filled in — the members hold exactly the base tax and the rate — and the rounding-and-capping algorithm moved into total_tax_impl, numbers identical to the original down to the last digit. One crash I genuinely had must go on record: do not put the struct inside an anonymous namespace; that creates a new type visible only inside the anonymous space, it fails to match the global CalcBackend from the header, the function-pointer types immediately refuse to fit, and the compiler reports invalid conversion on the spot — decorating the functions static is enough. The host side we wrote clean: the whole file includes only calc_plugin.h, and not one plugin is among the linked objects. Fetching the entry still walks the three steps — clear the old error, fetch the symbol, recheck at the end — and the table we obtain gets one generation check too:

```cpp
// host.cpp (excerpt) — the host: includes only the ABI header, links no plugins
const CalcPluginApi* api = entry();              // entry comes from dlsym, error-checked as in four.cpp
if (api->abi_version != CALC_ABI_VERSION) {      // generation mismatch, refuse on the spot
    std::fprintf(stderr, "ABI version mismatch: want %d, got %d\n", CALC_ABI_VERSION, api->abi_version);
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

Two same-shaped -fPIC -shared commands built standard.so and discount.so; plugin_discount comes out of the same mold as the standard version, only the backend's two numbers swapped for 80 and 9 — we will not repaste it. Everything we wanted to see ran out: we did not recompile one byte of the same host binary, swapping one .so swaps the whole algorithm, and base=100 total=120 connects steadily onto the earlier baseline. The host's NEEDED holds not a single plugin's name — the dependency has become a path parameter at run time, and the power of plugins, when you get down to it, lands exactly here. And what if the entry forgets its `extern "C"`? We really built a version without it, took a look at the export table with the nm -D we met in part 11, then let the host fetch by name:

```text
$ nm -D mangled.so | grep entry
00000000000011f2 T _Z17calc_plugin_entryv
$ ./host ./mangled.so; echo $?
dlsym: ./mangled.so: undefined symbol: calc_plugin_entry
exit=1
```

_Z17calc_plugin_entryv is its new name — the decodable mangled name from part 03's lesson shows its face again. dlsym takes a bare name and naturally comes up empty. Visible in the export table yet unfetchable by name: the words part 11's stretch on exporting names left behind are honored here. One more relation we must spell out: CalcBackend was created by the plugin's new — on what grounds is the host told to clean it up? It is not; destroy is a plugin-side function too, and the host merely passes the pointer back untouched. What deserves recording is one line: whichever module an object is born in, the authority to finish it belongs to that module — a properly paired create and destroy, each side managing its own. Real machinery stands behind this: two modules in the same process do not necessarily share one operator new — if the plugin statically linked its own runtime library, the delete across the border is undefined behavior. The opaque pointer guards the boundary between modules airtight: the host does not even hold the complete type, so there is no way to even begin writing code that crosses the line. The factory usage part 12 left waiting is complete now.

## Unloading: Reference Counting, Dangling Function Pointers

The virtue of plugins lies exactly in their being unloadable, but the mechanical details of unloading deserve two close looks of our own. The first look is at reference counting: a second dlopen on the same path no longer really loads; it only bumps the count by one, dlclose correspondingly only decrements, and only at zero does the real unload happen. We fitted the plugin with a global object that prints on load and unload, had the host dlopen twice and dlclose twice, and in the gap in between kept right on using it:

```text
$ ./refcount; echo $?
[trace] loaded into the process
h1=0x5e1672763040 h2=0x5e1672763040 same?yes
first dlclose returned 0
still usable after dlclose: total=120
[trace] unloaded from the process
second dlclose returned 0
main wrapping up
exit=0
```

The handle's address differs on every run; we kept this run's exactly as it came. The evidence is all in the ordering: loaded into the process printed only once — the two dlopen calls brought back the same handle. The first dlclose did nothing but decrement the count: total_tax still computed 120, the library was alive and well. The unloaded-from-process print sits between the second dlclose and main's wrap-up — the real unload happened on the second round. This counting scheme is of a piece with shared_ptr: however many handles you took, that many you must hand back at unload time. The second look takes the reverse side: fully unloaded, the cached function table still in hand, and we go and call it anyway:

```text
$ ./dangling; echo $?
[trace] loaded into the process
[trace] unloaded from the process
dlclose done, calling base_tax anyway
exit=139
```

We ran it five times in a row and got 139 all five — its origin is 128 plus 11, the kernel's old code for reporting SIGSEGV. The dlclose count reached zero and the library's mapping was withdrawn wholesale: the function table, the backend object, that stretch of code — all gone, and every pointer in our hand turned dangling. So a plugin system's discipline comes to a single line: after dlclose, every pointer obtained from that library is void — the function table, objects, strings handed out of static resources, not one of them may be touched again. For plugins meant to stay resident, quite a few projects simply never call dlclose: the loaded state stays until process exit, the whole business of reclamation is left to the operating system, and it saves worry instead.

## RTLD_NEXT: Wrapping a Layer Around malloc

In part 09 we used LD_PRELOAD to shove base_tax aside entirely, and we left words behind then too: quite a few of the memory-profiling breed of tools got their start by preloading their own malloc and free. Shoving aside is actually easy, but a wrapping tool wants more than replacement — it wants a layer around: once the counting is done, the work still has to be handed to the real malloc. Handed to whom? By way of RTLD_NEXT, a special pseudo-handle: it has dlsym continue searching from the modules that sit after the current library in the search order, fetching the next same-named definition. On part 11's list, the preloaded mwrap stands in front of libc; counting onward from mwrap's position, the next stop happens to be the real malloc, and the forwarding connects. Here are the key lines:

```c
// mwrap.c (excerpt) — the preload layer: count malloc/free, forward the rest to the next stop
static void* (*real_malloc)(size_t);
static char   boot_pool[1 << 16];          /* stand-in during resolution: static memory */
static int    resolving = 0;
static void resolve(void) {
    resolving = 1; real_malloc = (void* (*)(size_t)) dlsym(RTLD_NEXT, "malloc"); resolving = 0;
}
void* malloc(size_t n) {
    if (real_malloc == 0) { if (resolving) return boot_alloc(n); resolve(); }   /* loop defense, explained in the text */
    return real_malloc(n);                 /* forward unchanged after counting */
}
```

We made the report go through the write system call in a destructor, steering around printf, so that the very act of output would not itself enter malloc. Let's run it:

```text
$ gcc -shared -fPIC mwrap.c -o mwrap.so
$ LD_PRELOAD=$PWD/mwrap.so ./host ./standard.so; echo $?
mwrap: malloc passed 80766 bytes, free called 11 times
base=100 total=120
exit=0
```

The host computed its 120 as usual, and the statistics landed too: over its whole life this process pushed 80766 bytes through malloc, counting even the allocations libstdc++ made getting started. The origin of that little boot_pool stretch demands some history. Before glibc 2.34, dlsym lived in a separate libdl, and the internal allocations it made while resolving a symbol would re-enter the preload layer's malloc — with the real function pointer not yet in hand, it spun in place, and only propping up a slab of static memory broke the loop. And on today's glibc 2.44? We measured with the counting probe: during resolution the preload layer's malloc was not called even once; deleting the stand-in branch entirely and running again produced the very same 80766 to the digit — because libc's internal allocations go through internal aliases and never pass the PLT, so the preload layer's interception has nowhere to happen. Is the defense still worth writing, then? It is: the stand-in technique does not care about the implementation — move to a platform or link layout that still goes through the PLT, and it becomes the gate that stops the loop. The classic hurdle of writing interposition tools sits exactly here; when you someday page through the sources of ASan (AddressSanitizer, a memory-error checking tool) or heaptrack (a heap-memory profiling tool), you will bump into it right at the entrance.

## The Past and Present of -ldl

In the old tutorials' build commands, the linking step always carries a -ldl dangling off it; this piece of ours has not written it once from beginning to end, and the programs we built linked fine all the same. Where does the dl family live in today's glibc? We go straight to libc's export table:

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

The version numbers trailing the four names tell of their origin: since glibc 2.34, the formerly separate libdl has been merged into libc; on our 2.44, the merger is already several years behind us. The remnants on the development side we saw too: libdl.so, the development-side linker name, is not even a file anymore; what remains of libdl.a is an empty archive — nm reads not a single symbol out of it. libdl.so.2 is only a compatibility shell kept for old binaries' NEEDED, and the one thing it depends on is libc. So today's -ldl is optional: written, what gets linked is that empty archive — we tested it, and the program built with -ldl added has a NEEDED list identical to the one without, to the letter. The old tutorials had us write it for good reason: before 2.34, libdl.so was a real library with dlopen's implementation living inside it, and without it the linker would return you an undefined reference whose lead was that unobtainable `dlopen'`. The old tutorials were not wrong — they faced the glibc of their day. Now that this dependency has moved into libc, writing it or not produces the same artifact.

## The Windows Counterpart

On Windows this kit has a tidy set of counterparts: LoadLibraryA and LoadLibraryW answer to dlopen, GetProcAddress to dlsym, FreeLibrary to dlclose, and the handle's type goes by the name HMODULE. Error inquiry takes two steps: the first, GetLastError, fetches the error code; the second, FormatMessage, translates the code into text — none of the one-step convenience of our dlerror. When you are tracking down exported names, dumpbin's /EXPORTS does nm -D's job, checking whether a function sits in the export table. And there is one legacy trick from the 32-bit era: the __stdcall calling convention alters the exported name, decorating it into the _name@12 shape with the parameter byte count attached; GetProcAddress fetching by the bare name comes up empty, and so does a mismatch in the convention at the declaration site. As for the searching end — the loader's first search target is the EXE's own directory — the two sides' opposing ranks were compared in part 10, and we will not re-tell it today.

## The CMake Perspective

For everyday work, the plugin build we typed by hand with -fPIC -shared gets handed to CMake. CMake has a dedicated library type for plugins, MODULE: what it produces is a .so the same as ever, but the semantics are for dlopen, not for linking. Here is the project laid out:

```cmake
# CMakeLists.txt (excerpt)
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

The difference between MODULE and SHARED, CMake polices itself: write a MODULE target into target_link_libraries and configure refuses on the spot — we really tried it:

```text
CMake Error at CMakeLists.txt:8 (target_link_libraries):
  Target "standard" of type MODULE_LIBRARY may not be linked into another
  target.  One may link only to INTERFACE, OBJECT, STATIC or SHARED
  libraries, or to executables with the ENABLE_EXPORTS property set.
```

The error says it in full: MODULE's assigned role is to not be linked. This policing meshes exactly with our semantics — the host should never link a plugin; linked, the dependency is fixed at link time again, and the work just done goes to waste. PREFIX's empty string governs the artifact's name: a plugin's name is for humans and config files to read, the lib prefix is pure surplus, and with it gone, standard.so comes out clean. We paged through the --verbose compile lines: MODULE carries -fPIC automatically, on the same terms as SHARED, and not one word of it needs your attention. The little business of finding plugins at run time — the BUILD_RPATH mechanism, covered in part 10's CMake section — since our plugin path arrives as a parameter, we usually have no call for it either. And if you want the exported header to serve both Linux and Windows, CMake's generate_export_header can produce a header both sides eat — part 12's CMake section really ran it, and we will not re-stage it.

## Next Up: How the Process Itself Comes Up

Everything this piece did was the process's own handiwork while alive: dlopen finding libraries by itself, dlsym fetching symbols by itself, the timing of loading clenched in your code's hand. But how does the process itself get from one ./host to main? System calls of the execve breed take the program on disk and swap it into a new process; the swap done, control passes to the dynamic linker registered in the executable's header; it pulls up the whole dependency tree following NEEDED's list; only when the relocations are done does _start get its turn to initialize the runtime library, and the last thing reached is main. The interpreter glimpsed from afar in part 01, the NEEDED read line by line in part 08, the dynamic linker filling the GOT's tables in part 09 — all of them are actors in this flow. How does the whole road run? Part 14 (Program Startup) spends its entire length on exactly that — see you in the next piece.
