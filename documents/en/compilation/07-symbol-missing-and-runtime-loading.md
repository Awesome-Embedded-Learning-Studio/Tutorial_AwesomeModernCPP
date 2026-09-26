---
chapter: 13
difficulty: intermediate
order: 7
platform: host
reading_time_minutes: 7
tags:
- cpp-modern
- host
- intermediate
title: 'Deep Dive into C/C++ Compilation and Linking · Part 7: Dynamic Libraries A4 — Undefined Symbols at Link Time, Dynamic Loading at Runtime'
description: 'A cross-platform comparison of how tolerant Windows and GNU/Linux are of undefined symbols at link time, plus a hands-on walkthrough of runtime dynamic loading with dlopen/LoadLibrary and the C++ plugin factory pattern'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/07-symbol-missing-and-runtime-loading.md
  source_hash: 791ab0abb7dfe60525400253f9cc1abbf62a08252c863edfcf250a358d25e640
  translated_at: '2026-09-26T00:02:09+00:00'
  engine: anthropic
  token_count: 4000
---
# Deep Dive into C/C++ Compilation and Linking · Part 7: Dynamic Libraries A4 — Undefined Symbols at Link Time, Dynamic Loading at Runtime

This installment matters a bit more than the previous ones. What we plan to cover here is how the various platforms (Windows and GNU/Linux) behave when the symbols depended on by the executable being produced — or by other library files — are left undefined; plus the rather important business of programming dynamic loading of dynamic libraries.

## Platform Differences in Undefined-Symbol Behavior at Link Time

This one's interesting: what we're discussing is how tolerant each platform is of undefined symbols when linking happens. On Windows, by the time a dynamic library is produced, undefined symbols are already forbidden — the instant one appears, our toolchain complains that it can't find the symbol.

On Linux, no such thing happens. In fact, the Linux strategy is more forgiving: by default, undefined symbols are allowed, and it is only when the process is launched that the loader checks all the dependencies to make sure every important symbol has been correctly resolved. Only at that point does it get confirmed whether our program truly has a serious problem.

Of course, if you want this kind of strict checking, there is a way: pass the `-Wl,-no-undefined` option when compiling the relocatable files, and that steers the error-reporting behavior of the linker downstream.

## What Is Runtime Dynamic Loading

To put it formally, runtime dynamic linking (dynamic loading) means a program loads a shared library (shared object / dynamic library / DLL) on demand **at runtime**, looks up the symbols it needs (functions, variables), and then calls them. In my view, **this is one of the key implementation mechanisms behind plugin systems**, because now:

- We can load plugins dynamically, pulling in different feature modules at runtime based on configuration (internationalization, rendering backends, drivers, and so on).
- Those properties let us load only the dependencies we actually need, saving some space
- And they enable hot-swapping/extending at runtime — at the very least, we can extend functionality without recompiling the main program.

## Plenty of Benefits — but Any Trouble

There really is. Our error handling has to get more careful — after all, we now face a whole series of pesky problems like symbols not matching up, loads failing, and so on. I'd also suggest building one unified manager class to handle these exported symbols, and there's a reason for that: the beauty of plugins is precisely that they can be installed and uninstalled at any time, and once one is uninstalled, we absolutely must not keep calling its functions or accessing its static resources. My thought is that you could reach for a function-wrapper object with a QPointer-style expire mechanism to access them.

## Some System-Level APIs

Let me enumerate some of the system-level APIs:

- `void *dlopen(const char *filename, int flag);`
  - Common `flag` values: `RTLD_LAZY` (defers symbol resolution), `RTLD_NOW` (resolves all needed symbols immediately), `RTLD_LOCAL` (symbols stay local), `RTLD_GLOBAL` (symbols can be resolved by subsequently loaded libraries)
- `void *dlsym(void *handle, const char *symbol);` — returns a pointer to the function/variable
- `int dlclose(void *handle);` — unloads
- `char *dlerror(void);` — gets the error description (non-thread-safe implementations may return a static string)

The Windows counterparts:

- `HMODULE LoadLibrary(LPCSTR lpFileName);` — there's an Ex version too; here I'd suggest heading over to Microsoft's MSDN documentation to dig in: [LoadLibraryExW function (libloaderapi.h) - Win32 apps | Microsoft Learn](https://learn.microsoft.com/zh-cn/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
- `FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName);`
- `BOOL FreeLibrary(HMODULE hModule);`
- `DWORD GetLastError(void);` + `FormatMessage` to get a readable string

## A Minimal C Dynamic Library + Program (Linux) — C-Style Function Exports

As an example, I wrote a simple dynamic library:

```c
// mylib.c
#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

const char *hello(void) {
    return "Hello from mylib";
}

```

Under Linux, we build the dynamic library like this:

```bash

# Build the shared library
gcc -fPIC -shared -o libmylib.so mylib.c

# Compile the main program (dlopen gets used below)
gcc -o main main.c -ldl

```

Then we write a main.c that uses it:

```c
// main.c
#include <stdio.h>
#include <dlfcn.h>

int main(void) {
    /* Pass here a valid path */
    /* So place the dynamic library same place */
    void *h = dlopen("./libmylib.so", RTLD_NOW);
    if (!h) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    // Look up symbols
    int (*add)(int,int) = (int(*)(int,int))dlsym(h, "add");
    const char *(*hello)(void) = (const char*(*)(void))dlsym(h, "hello");
    char *err = dlerror();
    if (err) {
        fprintf(stderr, "dlsym error: %s\n", err);
        dlclose(h);
        return 1;
    }

    printf("add(2,3) = %d\n", add(2,3));
    printf("%s\n", hello());

    dlclose(h);
    return 0;
}

```

**Run**

```bash

# Make sure the current directory can be searched at load time (or set LD_LIBRARY_PATH)
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
./main

```

------

## DLLs and LoadLibrary Under Windows (MinGW / MSVC)

### mylib.c (Windows DLL)

```c
// mylib.c
#include <windows.h>

__declspec(dllexport) int add(int a, int b) {
    return a + b;
}

__declspec(dllexport) const char* hello(void) {
    return "Hello from mylib.dll";
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    return TRUE;
}

```

**Build (MSVC Developer Command Prompt)**

```cmd
cl /LD mylib.c /Fe:mylib.dll

```

**Build (MinGW)**

```bash
gcc -shared -o mylib.dll -Wl,--out-implib,libmylib.a -Wl,--export-all-symbols -fPIC mylib.c

```

### main.c (Using LoadLibrary)

```c
// main_win.c
#include <windows.h>
#include <stdio.h>

typedef int (*add_t)(int,int);
typedef const char* (*hello_t)(void);

int main(void) {
    HMODULE h = LoadLibraryA("mylib.dll");
    if (!h) {
        DWORD e = GetLastError();
        printf("LoadLibrary failed: %lu\n", e);
        return 1;
    }

    add_t add = (add_t)GetProcAddress(h, "add");
    hello_t hello = (hello_t)GetProcAddress(h, "hello");
    if (!add || !hello) {
        printf("GetProcAddress failed\n");
        FreeLibrary(h);
        return 1;
    }
    printf("add(10,20) = %d\n", add(10,20));
    printf("%s\n", hello());

    FreeLibrary(h);
    return 0;
}

```

**Run (from the DLL's own directory, or with the DLL added to PATH)**

```cmd
set PATH=%CD%;%PATH%
main_win.exe

```

------

## C++ Plugin Interfaces and an extern "C" Factory (the Recommended Approach)

When you need to export C++ objects or classes, the common strategy is to export a factory function (`extern "C"`) that returns an opaque pointer, or to export a `struct` function table (an interface table), sidestepping the effects of C++ name mangling.

```c
// plugin.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct PluginAPI {
    int (*init)(void);
    void (*shutdown)(void);
    int (*do_work)(int arg);
} PluginAPI;

// Exported factory: returns a pointer to the function table
PluginAPI* create_plugin_api(void);

#ifdef __cplusplus
}
#endif

```

### plugin_impl.c (the Plugin Implementation)

```c
// plugin_impl.c
#include "plugin.h"
#include <stdio.h>

static int my_init(void) { printf("plugin init\n"); return 0; }
static void my_shutdown(void) { printf("plugin shutdown\n"); }
static int my_do_work(int arg) { printf("plugin do work %d\n", arg); return arg*2; }

static PluginAPI api = {
    .init = my_init,
    .shutdown = my_shutdown,
    .do_work = my_do_work
};

PluginAPI* create_plugin_api(void) {
    return &api;
}

```

The main program only has to grab the `PluginAPI*` via `dlsym(h, "create_plugin_api")` and it can call the plugin functions seamlessly, with no need to care about C++ name mangling.

## Problems I've Run Into, and the Troubleshooting Tricks I've Accumulated

#### **Why `dlsym` Can't Get at My C++ Functions**

Back when I was hand-rolling a PDF viewer and about to build its plugin system, this one got me. In an earlier blog post I mentioned that C++ compilers decorate symbol names (name mangling). Naturally, the solution is to export a C-style interface with `extern "C"`, or to go with the approach I laid out above.

#### **How to Troubleshoot a Failing `GetProcAddress` on Windows**

Check the exported names (using `dumpbin /EXPORTS` or `nm`), check whether the calling conventions match (`__stdcall` changes the exported name), and check whether C++ name mangling is at play. The recommendation: `__declspec(dllexport)` + `extern "C"`.

## From a Modern CMake Perspective

All that hand-rolled `gcc -fPIC -shared`, `-Wl,-no-undefined`, `__declspec(dllexport)` work above is basically taken over by CMake in a modern project. `add_library(mylib SHARED mylib.c)` automatically adds `-fPIC` for position-independent code and produces `.so`/`.dll`/`.dylib` depending on the platform, while `STATIC` goes through `ar` packaging — you no longer need to type those two flags by hand. Linux's lenient default of letting undefined symbols pass can be tightened back up with `set_target_properties(mylib PROPERTIES LINK_FLAGS "-Wl,--no-undefined")` (or `CMAKE_SHARED_LINKER_FLAGS`), recreating the strict check described at the beginning of this article. On the symbol-visibility front, `CXX_VISIBILITY_PRESET hidden` + `VISIBILITY_INLINES_HIDDEN ON` is equivalent to wrapping the whole target in `-fvisibility=hidden`; you then only slap `__attribute__((visibility("default")))` (or Windows' `__declspec(dllexport)`) onto the factory functions that actually need exporting, and the export table comes out clean and tidy — writing it cross-platform is far less of a headache than scattering `dllexport` all over your files. As for that runtime library-hunting `LD_LIBRARY_PATH` / `PATH` fiddling chain, CMake automates the whole 'install it wherever, find it wherever' problem with two moves: install-time `CMAKE_INSTALL_RPATH` (on Linux, configure `$ORIGIN` so the executable looks for its `.so` in its own directory) and, on Windows, copying the DLL next to the executable. That `export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH` line from earlier in this article is basically never hand-typed in a well-formed CMake project.
