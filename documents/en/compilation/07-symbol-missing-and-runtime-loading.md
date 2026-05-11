---
title: 'In-depth Understanding of C/C++ Compilation Technology — Dynamic Library A4:
  Link-time Missing Symbol Behavior and Runtime Dynamic Loading'
description: ''
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
chapter: 13
order: 7
---
# Deep Dive into C/C++ Compilation Technology — Dynamic Libraries A4: Link-Time Missing Symbol Behavior and Runtime Dynamic Loading

This blog post is particularly important. Here, we plan to discuss how different platforms (Windows and GNU/Linux) behave when our executable or other dependent libraries have undefined symbols, as well as the crucial topic of runtime dynamic loading programming.

## Platform Differences in Link-Time Missing Symbol Behavior

This is quite interesting. We are discussing the tolerance levels of different platforms for undefined symbols during the linking process. On Windows, when generating a dynamic library, undefined symbols are strictly prohibited. If any undefined symbol exists, our toolchain will complain that it cannot find the symbol.

On Linux, however, this is not the case. In fact, Linux's strategy is more permissive. By default, we allow symbols to be undefined until the program is loaded, at which point the loader checks all dependencies to ensure all essential symbols are correctly resolved. Only then does it confirm whether our program truly has a critical issue.

Of course, if we want this strict checking, there is a way: simply pass the ``-Wl,-no-undefined`` option when compiling relocatable files to instruct the subsequent linker's error reporting behavior.

## What Is Runtime Dynamic Loading?

Officially speaking, runtime dynamic loading refers to a program loading a shared library (shared object / dynamic library / DLL) **at runtime** on demand, looking up the required symbols (functions, variables), and then calling them. The author believes that **this is a key implementation mechanism for plugin systems.** This is because:

- We can dynamically load plugins, loading different functional modules (internationalization, rendering backends, drivers, etc.) at runtime based on configuration.
- The above feature allows us to load only the dependencies we need, saving some space.
- It also supports hot-swapping/extending at runtime. At the very least, we can extend functionality without recompiling the main program.

## Many Benefits, But Any Drawbacks?

There certainly are. We need to be much more careful with our error handling. After all, we will encounter a series of troublesome issues like symbol mismatches and loading failures. It is also recommended to create a unified management class to handle these exported symbols—there is a good reason for this. The beauty of plugins is that they can be installed and uninstalled at any time; after unloading, we must absolutely not continue to call their functions or access their static resources. The author suggests creating a function wrapper object with an expiration mechanism, similar to `QPointer`, to access them.

## Some System-Level APIs

Here we enumerate a few system-level APIs:

- ``void *dlopen(const char *filename, int flag);``
  - ``flag`` Commonly used: ``RTLD_LAZY`` (lazy symbol resolution), ``RTLD_NOW`` (immediately resolve all required symbols), ``RTLD_LOCAL`` (local symbols), ``RTLD_GLOBAL`` (symbols can be resolved by subsequently loaded libraries)
- ``void *dlsym(void *handle, const char *symbol);`` returns a pointer to a function/variable
- ``int dlclose(void *handle);`` unloads
- ``char *dlerror(void);`` gets an error description (non-thread-safe implementations might return a static string)

Windows equivalents:

- ``HMODULE LoadLibrary(LPCSTR lpFileName);`` There is also an EX version. The author recommends checking Microsoft's MSDN documentation for details: [LoadLibraryExW function (libloaderapi.h) - Win32 apps | Microsoft Learn](https://learn.microsoft.com/zh-cn/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
- ``FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName);``
- ``BOOL FreeLibrary(HMODULE hModule);``
- ``DWORD GetLastError(void);`` + ``FormatMessage`` to get a readable string

## Minimal C Dynamic Library + Program (Linux) — C-Style Function Export

For example, the author wrote a simple dynamic library:

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

Then we write a `main.c` to use it:

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

**Run (in the same directory as the DLL, or add the DLL to PATH)**

```cmd
set PATH=%CD%;%PATH%
main_win.exe

```

------

## C++ Plugin Interfaces and extern "C" Factories (Recommended Approach)

When we need to export C++ objects or classes, a common strategy is to export a factory function (``extern "C"``) that returns an opaque pointer, or to export a ``struct`` function table (interface table), avoiding the impact of C++ name mangling.

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

### plugin_impl.c (Plugin Implementation)

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

The main program only needs to use ``dlsym(h, "create_plugin_api")`` to get ``PluginAPI*``, and it can seamlessly call plugin functions without worrying about C++ name mangling.

## Problems the Author Has Encountered, and Accumulated Troubleshooting Methods

#### **Why can't ``dlsym`` find my C++ function?**

When the author was hand-crafting a PDF viewer and preparing to build a plugin system, they got burned by this. As discussed in previous blog posts, C++ compilers mangle symbol names (name mangling). The natural solution is to export a C-style interface using ``extern "C"``, or to use the approach mentioned above.

#### **How to troubleshoot a failed ``GetProcAddress`` on Windows?**

Check the exported names (using ``dumpbin /EXPORTS`` or ``nm``), verify that the calling convention matches (``__stdcall`` changes the exported name), and check whether C++ name mangling is being used. The author recommends ``__declspec(dllexport)`` + ``extern "C"``.
