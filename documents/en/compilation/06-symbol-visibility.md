---
chapter: 13
difficulty: intermediate
order: 6
platform: host
reading_time_minutes: 4
tags:
- cpp-modern
- host
- intermediate
title: 'Deep Dive into C/C++ Compilation Technology — Dynamic Libraries A3: Discussing
  Symbol Visibility'
description: ''
translation:
  source: documents/compilation/06-symbol-visibility.md
  source_hash: c611694e844be24e2b55b6a8d46b5ea620a65c311e178f2702c25890e864bdcf
  translated_at: '2026-06-16T03:27:02.371993+00:00'
  engine: anthropic
  token_count: 1010
---
# Understanding C/C++ Compilation Technology — Dynamic Libraries A3: A Discussion on Symbol Visibility

Some readers might find this concept strange—what exactly is symbol visibility? Is it related to the C++ keywords `private` or `public`? It is worth noting that it is not; the latter are basic features provided by language syntax and compiler checks. Here, we discuss symbol visibility at a more aggressive level, referring to visibility at the symbol ABI (Application Binary Interface) level.

#### Tips: How to View ABI Symbols

> Veterans can skip this section

Since some readers might be encountering this type of article for the first time, they may not yet know how to "view visible symbols contained in a given relocatable object file, an executable composed of such files, or a library." I plan to supplement this guide with instructions on how to perform this basic operation on major Windows and Linux platforms.

##### GNU/Linux Platform

It is very simple; we only need to use the `nm` tool. Suppose we have a library file `libfoo.so` ready for inspection. Entering the following command will do the trick.

```bash
nm -D libfoo.so
```

##### Windows Platform

This is straightforward. Suppose I intend to check `CCWidget.dll`. To view the exported symbols, use:

```powershell
dumpbin /EXPORTS CCWidget.dll
```

## How Do Mainstream Toolchains Control Symbol Visibility?

Returning to the main topic, how do mainstream toolchains control symbol visibility? Let's discuss them separately.

#### How to Control Symbol Visibility under GNU/Linux

##### Method 1: Directly Passing `-fvisibility` to the Compiler to Control All Symbol Exports

The first method is the most brute-force approach. Suppose we have a private dependency project and do not want to expose any symbols at all. In this case, we can pass `-fvisibility` to gcc/g++ during compilation. By default, for the GNU C/C++ toolchain, **any symbol without explicit visibility modifiers or specifications is public**. That is, `default`. If we want to hide them, we need to specify `hidden` when generating the dynamic library, causing all symbols not to be exported. I haven't used this personally, but I have found documentation on its usage.

##### Method 2: The Most Common Method: Using Attributes

I prefer this method of specification. Taking a simple logging library I wrote as a toy project for example: for all APIs planned to be public at the ABI level, I explicitly specify `__attribute__((visibility("default")))`. Conversely, for any symbol that should not be used, I apply `__attribute__((visibility("hidden")))`.

```cpp
#define API_EXPORT __attribute__((visibility("default")))
#define API_LOCAL __attribute__((visibility("hidden")))

class API_EXPORT Logger {
    // ...
};

void API_LOCAL internal_helper();
```

##### Method 3: Modifying a Group of Aggregated Symbols

If you really need to handle visibility modifications for a massive number of symbols but don't want to add macros to each symbol one by one as in the example above, you can use the compiler's preprocessor directives.

```cpp
#pragma GCC visibility push(default)
// ... public symbols ...
#pragma GCC visibility pop

#pragma GCC visibility push(hidden)
// ... internal symbols ...
#pragma GCC visibility pop
```

#### How Windows MSVC Handles This

Unfortunately, exporting symbols from Windows DLLs involves a relatively complex decoration mechanism. That is, symbols intended for export need to be decorated with `__declspec(dllexport)`, and when using these symbols, we need to mark them with `__declspec(dllimport)`.

```cpp
// In the DLL header
#ifdef BUILDING_DLL
    #define API_PUBLIC __declspec(dllexport)
#else
    #define API_PUBLIC __declspec(dllimport)
#endif

class API_PUBLIC Widget {
    // ...
};
```
