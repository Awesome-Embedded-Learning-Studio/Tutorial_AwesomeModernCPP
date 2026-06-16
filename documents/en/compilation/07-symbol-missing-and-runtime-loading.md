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
  source_hash: d44efaef94d6ad2e3a1bb398d9790f03ad6d5396ac5e20d376943e63f9be91a1
  translated_at: '2026-06-16T03:27:41.256202+00:00'
  engine: anthropic
  token_count: 1424
---
# Deep Dive into C/C++ Compilation Technology — Dynamic Libraries A4: Link-Time Symbol Resolution Behavior and Runtime Dynamic Loading

This blog post is particularly important. Here, we plan to discuss the behavior on different platforms (Windows and GNU/Linux) when undefined symbols exist in generated executables or other library dependencies, as well as the significant topic of programming for runtime dynamic library loading.

## Platform Differences in Link-Time Symbol Resolution Behavior

This is quite interesting. We are discussing the tolerance levels of different platforms regarding undefined symbols at the time linking occurs. On Windows, when generating a dynamic library, undefined symbols are strictly prohibited. If an undefined symbol occurs, our toolchain will immediately complain that it cannot find the symbol.

On Linux, however, this does not happen. In fact, Linux's strategy is more permissive. By default, we allow symbols to remain undefined. It is not until the process is launched that the loader checks all dependencies to ensure all critical symbols are correctly resolved. Only then is it confirmed whether our program actually has significant issues.

Of course, if you desire this strict checking, there is a way: pass the `--no-undefined` option when compiling relocatable files to instruct the subsequent linker to report errors.

## What is Runtime Dynamic Loading?

Formally speaking, runtime dynamic loading refers to a program loading a shared library (shared object / dynamic library / DLL) **on demand** at runtime, locating the required symbols (functions, variables), and invoking them. The author believes that **this is a key implementation mechanism for plugin systems.** Because now:

- We can load plugins dynamically, loading different functional modules (internationalization, rendering backends, drivers, etc.) at runtime based on configuration.
- The above features allow us to load dependencies on demand, saving some space.
- Furthermore, it supports hot-swapping/extending at runtime. At the very least, we can extend functionality without recompiling the main program.

## Many Benefits, but What About the Downsides?

There certainly are some. We must be much more careful with error handling. After all, we face a series of troublesome issues like symbol mismatches and load failures. It is also recommended to create a unified management class to handle these exported symbols. There is a reason for this: the beauty of plugins is that they can be installed and uninstalled at any time. After unloading, we must absolutely not continue to call their functions or access their static resources. The author suggests implementing something similar to `QPointer`—a function wrapper object with an expiration mechanism—to access them.

## Some System-Level APIs

Here is a list of some system-level APIs:

- `dlopen`
  - `flags` Commonly used: `RTLD_LAZY` (lazy symbol resolution), `RTLD_NOW` (resolve all symbols immediately), `RTLD_LOCAL` (symbols are not available to subsequently loaded libraries), `RTLD_GLOBAL` (symbols can be resolved by subsequently loaded libraries)
- `dlsym` Returns a pointer to a function/variable
- `dlclose` Unloads
- `dlerror` Gets a description of the error (implementations may return a static string and are not thread-safe)

Windows equivalents:

- `LoadLibrary` (of course, there is an EX version; the author suggests visiting Microsoft's MSDN documentation for details: [LoadLibraryExW function (libloaderapi.h) - Win32 apps | Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw))
- `GetProcAddress`
- `FreeLibrary`
- `GetLastError` + `FormatMessage` to get a readable string

## Minimal C Dynamic Library + Program (Linux) — C-Style Function Export

For example, the author wrote a simple dynamic library:

```c
// mylib.c
#include <stdio.h>

void hello() {
    puts("Hello from mylib!");
}
```

On Linux, we build the dynamic library like this:

```bash
gcc -shared -fPIC -o libmylib.so mylib.c
```

Then, we write a `main.c` to use it:

```c
// main.c
#include <stdio.h>
#include <dlfcn.h>

int main() {
    // Open the library
    void* handle = dlopen("./libmylib.so", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    // Clear any existing error
    dlerror();

    // Locate the symbol
    void (*hello_func)() = dlsym(handle, "hello");
    char* error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "dlsym failed: %s\n", error);
        dlclose(handle);
        return 1;
    }

    // Call the function
    hello_func();

    // Close the library
    dlclose(handle);
    return 0;
}
```

**Run:**

```bash
gcc -o main main.c -ldl
./main
# Output: Hello from mylib!
```

------

## DLL and LoadLibrary on Windows (MinGW / MSVC)

### mylib.c (Windows DLL)

```c
// mylib.c
#include <windows.h>

__declspec(dllexport) void hello() {
    MessageBoxA(NULL, "Hello from mylib!", "DLL Message", MB_OK);
}
```

**Build (MSVC Developer Command Prompt)**

```cmd
cl /LD mylib.c
```

**Build (MinGW)**

```bash
gcc -shared -o mylib.dll mylib.c
```

### main.c (Using LoadLibrary)

```c
// main.c
#include <windows.h>
#include <stdio.h>

typedef void (*HelloFunc)();

int main() {
    HMODULE hModule = LoadLibrary(TEXT("mylib.dll"));
    if (!hModule) {
        printf("LoadLibrary failed (%lu)\n", GetLastError());
        return 1;
    }

    HelloFunc hello_func = (HelloFunc)GetProcAddress(hModule, "hello");
    if (!hello_func) {
        printf("GetProcAddress failed (%lu)\n", GetLastError());
        FreeLibrary(hModule);
        return 1;
    }

    hello_func();

    FreeLibrary(hModule);
    return 0;
}
```

**Run (In the same directory as the DLL or add the DLL to PATH)**

```cmd
cl main.c
main.exe
```

------

## C++ Plugin Interfaces and extern "C" Factories (Recommended Practice)

When exporting C++ objects or classes, a common strategy is to export a factory function (`extern "C"`) that returns an opaque pointer, or to export a table of function pointers (interface table) using `struct`, to avoid C++ name mangling issues.

```cpp
// plugin_interface.h
#pragma once
#include <cstdint>

// Abstract interface (pure virtual functions)
struct IPlugin {
    virtual void initialize() = 0;
    virtual void process(int data) = 0;
    virtual void shutdown() = 0;
    virtual ~IPlugin() = default;
};

// "C" factory function
extern "C" {
    IPlugin* create_plugin();
    void destroy_plugin(IPlugin* p);
}
```

### plugin_impl.c (Plugin Implementation)

```cpp
// plugin_impl.cpp
#include "plugin_interface.h"

struct MyPlugin : public IPlugin {
    void initialize() override { /* ... */ }
    void process(int data) override { /* ... */ }
    void shutdown() override { /* ... */ }
};

extern "C" IPlugin* create_plugin() {
    return new MyPlugin;
}

extern "C" void destroy_plugin(IPlugin* p) {
    delete p;
}
```

The main program only needs to use `dlsym` (or `GetProcAddress`) to obtain `create_plugin`, allowing it to seamlessly call plugin functions without worrying about C++ name mangling.

## Issues I Encountered and My Accumulated Troubleshooting Methods

#### **Why can't `dlsym` find my function in C++?**

When I was hand-writing a PDF viewer and preparing to implement a plugin system, I ran into this. As discussed in previous blog posts, C++ compilers perform name mangling on symbol names. The natural solution is to export a C-style interface using `extern "C"`, or use the solution mentioned above.

#### **How to troubleshoot `GetProcAddress` failures on Windows?**

Check the exported name (using `Dependency Walker` or `dumpbin /exports`), verify that the calling convention matches (e.g., `__stdcall` changes the exported name), or check if C++ name mangling is being used. It is recommended to use `__declspec(dllexport)` + `extern "C"`.
