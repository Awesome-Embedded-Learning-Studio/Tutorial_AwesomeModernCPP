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
title: 'Deep Dive into C/C++ Compilation — Dynamic Libraries A4: Undefined-Symbol Behavior at Link Time and Runtime Dynamic Loading'
description: 'A cross-platform comparison of how tolerant each platform is about undefined symbols at link time, plus a walkthrough of runtime dynamic loading with dlopen / LoadLibrary and a C++ plugin factory pattern.'
cpp_standard: [11, 14, 17, 20]
---
# Deep Dive into C/C++ Compilation — Dynamic Libraries A4: Undefined-Symbol Behavior at Link Time and Runtime Dynamic Loading

This post is going to matter a bit more. What I'm planning to talk through here is how the different platforms (Windows and GNU/Linux) behave when an executable we're building, or another library, depends on a symbol that's left undefined; and then the more interesting topic, which is the programming side of dynamically loading a dynamic library at runtime.

## Platform differences for undefined symbols at link time

This one's genuinely interesting. What we're talking about is, at the moment linking actually happens, how tolerant each platform is of leaving a symbol undefined. On Windows, the moment you produce a dynamic library, you're already required to have zero undefined symbols. The instant an undefined symbol shows up, your toolchain starts complaining that it can't find the symbol.

On Linux, nothing of the sort happens. In fact, Linux's policy is far more permissive; by default, we let symbols stay undefined all the way up to the point the process is launched, at which point the loader goes through every dependency and checks that every important symbol actually gets addressed. Only then does it confirm whether our program really has a serious problem.

Of course, if you want this kind of strict checking, there is a way: when you're producing the relocatable object, pass `-Wl,-no-undefined` to steer the linker's error-reporting behavior down the line.

## What is runtime dynamic loading?

Officially speaking, runtime dynamic linking (dynamic loading) means a program loads a shared library (shared object / dynamic library / DLL) on demand at runtime, looks up the symbols it needs (functions, variables), and then calls them. In my view, this is one of the important implementation mechanisms behind plugin systems, because now:

- We can load plugins dynamically, pulling in different functional modules at runtime based on configuration (internationalization, rendering backends, drivers, and so on).
- The above property means we can load only the dependencies we actually need, saving a bit of space.
- And we get hot-swap / extension support at runtime; at the very least, we can extend functionality without recompiling the main program.

## Lots of upsides, but any trouble?

There really is some. Our error handling has to get more careful, since we end up with a whole string of annoying problems, things like the symbol not matching, the load failing, and so on. I'd also suggest you build a single manager class to handle these exported symbols, and there's a reason for that: the whole point of a plugin is that it can be installed and uninstalled at any time, and once it's unloaded, you absolutely must not keep calling its functions or touching its static resources. I think you could build something like a function-wrapping object with an expire mechanism, similar in spirit to Qt's `QPointer`, to access it through.

## Some system-level APIs

Here's a quick rundown of some of the system-level APIs:

- `void *dlopen(const char *filename, int flag);`
  - Common `flag` values: `RTLD_LAZY` (defer symbol resolution), `RTLD_NOW` (resolve every needed symbol immediately), `RTLD_LOCAL` (keep symbols local), `RTLD_GLOBAL` (symbols can be picked up by libraries loaded afterwards)
- `void *dlsym(void *handle, const char *symbol);` returns a pointer to a function or variable
- `int dlclose(void *handle);` unloads
- `char *dlerror(void);` fetches the error description (a non-thread-safe implementation may return a static string)

The Windows equivalents:

- `HMODULE LoadLibrary(LPCSTR lpFileName);` there's also the Ex version; I'll point you over to Microsoft's MSDN docs if you want to dig in: [LoadLibraryExW function (libloaderapi.h) - Win32 apps | Microsoft Learn](https://learn.microsoft.com/zh-cn/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
- `FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName);`
- `BOOL FreeLibrary(HMODULE hModule);`
- `DWORD GetLastError(void);` plus `FormatMessage` to get a readable string

## A minimal C dynamic library + program (Linux) — exporting C-style functions

For example, I wrote a simple dynamic library:

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

On Linux, we build the dynamic library like this:

```bash

# 生成共享库
gcc -fPIC -shared -o libmylib.so mylib.c

# 编译主程序（下面会用 dlopen）
gcc -o main main.c -ldl

```

Then we write a `main.c` that uses it:

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

    // 查找 symbol
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

**Run it**

```bash

# 确保当前目录可被加载（或设置 LD_LIBRARY_PATH）
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
./main

```

------

## DLLs and LoadLibrary on Windows (MinGW / MSVC)

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

### main.c (using LoadLibrary)

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

**Run it (in the same directory as the DLL, or add the DLL's directory to PATH)**

```cmd
set PATH=%CD%;%PATH%
main_win.exe

```

------

## C++ plugin interfaces and the `extern "C"` factory (the recommended approach)

When you need to export C++ objects or classes, the common strategy is to export a factory function (`extern "C"`) that returns an opaque pointer, or to export a `struct` full of function pointers (an interface table), so that C++ name mangling doesn't get in the way.

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

// 导出工厂：返回函数表指针
PluginAPI* create_plugin_api(void);

#ifdef __cplusplus
}
#endif

```

### plugin_impl.c (the plugin implementation)

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

The main program just needs to grab the `PluginAPI*` through `dlsym(h, "create_plugin_api")`, and it can call into the plugin's functions seamlessly, without ever having to care about C++ name mangling.

## Problems I've hit, and the debugging tricks I've picked up along the way

#### **Why can't `dlsym` find the function I wrote in C++?**

I got bitten by this back when I was hand-rolling a PDF viewer and starting to build out its plugin system. As I talked about in an earlier post, the C++ compiler mangles symbol names (name mangling). The natural fix is to export a C-style interface through `extern "C"`, or use the function-table approach I mentioned above.

#### **How do you debug a failing `GetProcAddress` on Windows?**

Check the exported names (using `dumpbin /EXPORTS` or `nm`), check whether the calling convention matches (`__stdcall` will rewrite the exported name), and check whether C++ name mangling is in play. I'd recommend going with `__declspec(dllexport)` paired with `extern "C"`.

## The modern CMake view

All of that hand-typed `gcc -fPIC -shared`, `-Wl,-no-undefined`, `__declspec(dllexport)` stuff is, in a modern project, basically taken over by CMake. `add_library(mylib SHARED mylib.c)` will add `-fPIC` for position-independent code for you and produce a `.so` / `.dll` / `.dylib` depending on the platform; `STATIC` then goes through `ar` for packaging, and you no longer have to type these two flags by hand. As for Linux's permissive default of letting undefined symbols slide, you can tighten it back up with `set_target_properties(mylib PROPERTIES LINK_FLAGS "-Wl,--no-undefined")` (or `CMAKE_SHARED_LINKER_FLAGS`) to reproduce the strict checking I talked about at the start of this post. On the symbol-visibility side, `CXX_VISIBILITY_PRESET hidden` paired with `VISIBILITY_INLINES_HIDDEN ON` is equivalent to slapping `-fvisibility=hidden` over the entire target; then you only drop `__attribute__((visibility("default")))` (or, on Windows, `__declspec(dllexport)`) onto the factory functions you actually want to export, and the export table comes out clean. Writing that cross-platform is far less of a headache than sprinkling `dllexport` all over the file. As for the runtime library-search chain, that whole `LD_LIBRARY_PATH` / `PATH` song and dance, CMake automates the "wherever it gets installed is where it can be found" part with install-time `CMAKE_INSTALL_RPATH` (on Linux, pair it with `$ORIGIN` so the executable goes looking for its `.so` in its own directory) and, on Windows, the trick of copying the DLL next to the executable. That line of yours, `export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH`, in a properly structured CMake project you basically never have to type by hand.
