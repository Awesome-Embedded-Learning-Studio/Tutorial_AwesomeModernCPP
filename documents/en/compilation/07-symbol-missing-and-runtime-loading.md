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
title: 'In-depth Understanding of C/C++ Compilation Technology — Dynamic Libraries
  A4: Link-Time Symbol Missing Behavior and Runtime Dynamic Loading'
description: ''
translation:
  source: documents/compilation/07-symbol-missing-and-runtime-loading.md
  source_hash: 2f848caece654c8136cefb8c2fc7d988f0af62905153ff32c6c990871169e250
  translated_at: '2026-06-24T00:25:31.082918+00:00'
  engine: anthropic
  token_count: 1424
---
# In-Depth Understanding of C/C++ Compilation Technology—Dynamic Libraries A4: Link-Time Symbol Missing Behavior and Runtime Dynamic Loading

This blog post is particularly important. Here, we plan to discuss the behavior on different platforms (Windows and GNU/Linux) when undefined symbols exist during the generation of our executable files or when other library files depend on them. We will also cover the fairly significant topic of programming for runtime dynamic library loading.

## Platform Differences in Link-Time Symbol Missing Behavior

This is quite interesting. We are discussing the tolerance levels of different platforms for undefined symbols when linking occurs. On Windows, when a dynamic library is generated, undefined symbols are strictly prohibited. Once an undefined symbol appears, our toolchain will complain that it cannot find the symbol.

This is not the case on Linux. In fact, Linux's strategy is more permissive. By default, we allow symbols to remain undefined until the process is launched. At that point, the loader checks all dependencies to ensure all essential symbols are correctly resolved. It is only then that we confirm whether our program truly has critical issues.

Of course, if you prefer this strict checking, there is a way: pass the `-Wl,-no-undefined` option when compiling the relocatable files to instruct the subsequent linker to report errors.

## What is Runtime Dynamic Loading?

Officially, runtime dynamic loading refers to a program loading a shared library (shared object / dynamic library / DLL) **at runtime** on demand, finding the required symbols (functions, variables), and then calling them. In the author's opinion, **this is a key implementation mechanism for plugin systems**. Because now:

- We can load plugins dynamically, loading different functional modules (internationalization, rendering backends, drivers, etc.) at runtime based on configuration.
- The above features allow us to load only the dependencies we need, saving some space.
- Furthermore, it supports hot-swapping/extending at runtime. At the very least, we can extend functionality without recompiling the main program.

## Many Benefits, But Are There Drawbacks?

There certainly are. We need to be much more careful with error handling. After all, we will face a series of troublesome issues, such as mismatched symbols or loading failures. It is also recommended to create a unified management class to handle these exported symbols. There is a reason for this: the beauty of plugins is that they can be installed and uninstalled at any time. After unloading, we must absolutely avoid continuing to call their functions or accessing their static resources. The author suggests creating a function wrapper object similar to `QPointer` that includes an expiration mechanism to access them.

## Some System-Level APIs

Here is a list of some system-level APIs.

- `void *dlopen(const char *filename, int flag);`
  - Common `flag` values: `RTLD_LAZY` (lazy symbol resolution), `RTLD_NOW` (resolve all required symbols immediately), `RTLD_LOCAL` (symbols are local), `RTLD_GLOBAL` (symbols can be resolved by subsequently loaded libraries)
- `void *dlsym(void *handle, const char *symbol);` Returns a pointer to the function/variable
- `int dlclose(void *handle);` Unloads the library
- `char *dlerror(void);` Retrieves error description (implementations that are not thread-safe may return a static string)

Windows equivalents:

- `HMODULE LoadLibrary(LPCSTR lpFileName);` Of course, there is also an EX version. Here, the author suggests you head over to Microsoft's MSDN documentation to find out more: [LoadLibraryExW function (libloaderapi.h) - Win32 apps | Microsoft Learn](https://learn.microsoft.com/zh-cn/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
- `FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName);`
- `BOOL FreeLibrary(HMODULE hModule);`
- `DWORD GetLastError(void);` + `FormatMessage` to get a readable string

## Minimal C Dynamic Library + Program (Linux) — C-Style Function Export

For example, the author has written a simple dynamic library

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

On Linux, we build a shared library like this

```bash

# 生成共享库
gcc -fPIC -shared -o libmylib.so mylib.c

# 编译主程序（下面会用 dlopen）
gcc -o main main.c -ldl

```

Next, we write a `main.c` to use it:

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

**Run**

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

**Run (in the same directory as the DLL or add the DLL to PATH)**

```cmd
set PATH=%CD%;%PATH%
main_win.exe

```

## C++ Plugin Interfaces and extern "C" Factories (Recommended Practice)

When we need to export C++ objects or classes, a common strategy is to export a factory function (`extern "C"`) that returns an opaque pointer, or to export a `struct` function table (interface table), to avoid C++ name mangling issues.

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

The main program only needs to obtain the `PluginAPI*` via `dlsym(h, "create_plugin_api")` to seamlessly call plugin functions, without worrying about C++ name mangling.

## Issues I Encountered and Troubleshooting Techniques

### **Why can't `dlsym` find my function in C++?**

When I was hand-writing a PDF viewer and preparing to implement a plugin system, I ran into this issue. As discussed in my previous blog posts, C++ compilers perform name mangling on symbol names. The natural solution is to export a C-style interface using `extern "C"`, or use the solution mentioned above.

### **How to troubleshoot `GetProcAddress` failures on Windows?**

Check the exported names (using `dumpbin /EXPORTS` or `nm`), verify that the calling conventions match (`__stdcall` changes the exported name), or check if C++ name mangling is being used. I recommend using `__declspec(dllexport)` + `extern "C"`.
