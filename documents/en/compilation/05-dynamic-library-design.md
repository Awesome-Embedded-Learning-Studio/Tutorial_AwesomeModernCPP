---
chapter: 13
difficulty: intermediate
order: 5
platform: host
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
title: 'Deep Dive into C/C++ Compilation and Linking · Part 5: Dynamic Libraries A2 — Designing ABI-Friendly Interfaces'
description: 'Lays out the low-level pitfalls of dynamic library ABI design: why C++ name mangling does not port across compilers, the static-object initialization-order trap, and how a C-style export interface plus a complete ABI header spares you the ABI hookup pain'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/05-dynamic-library-design.md
  source_hash: c4a49ee5824f8a0aadcfd617186d1c8b1f5fbad41260a69a354aa385fc98332a
  translated_at: '2026-09-25T23:47:41+00:00'
  engine: anthropic
  token_count: 6200
---
# Deep Dive into C/C++ Compilation and Linking · Part 5: Dynamic Libraries A2 — Designing ABI-Friendly Interfaces

## Preface

In this post, what I am attempting is to pull together and sum up some of the more important technical points on the **design** side of our dynamic libraries — for instance, designing and exporting the binary interface.

## So, Why the Binary Interface Enters the Picture

Essentially, the ultimate goal of designing a dynamic library (and I believe this is something to keep firmly in mind at all times) is to hand our code over to other people to reuse. That makes the details of code collaboration exactly what we have to think about. In a blog post from quite a long while back, we already boiled the abstract notion of a dynamic library down to an **interface** — a specified set of exported symbols, written down in a header file or a dedicated export file so that other users know how to call into the target functionality — plus a bunch of hidden, concrete machine-code detail behind it.

But we know that the function names under various classes and the global variable names written into a human-readable file, a header for instance, do count as an interface — and we just as clearly know that this is not a **binary interface**. All along, we seem to have grown used to the idea that once we have exported the designated symbols and provided the machine code implementing them, everything is safe and sound. Except that, because of C++'s freewheeling nature (notice that I did not say C — in practice this problem blows up almost entirely on reusable libraries written in C++), **the translation from human-readable API to machine-facing ABI performed by the compilers that different vendors implement is not consistent**! And that gives rise to a whole series of problems that are not one bit funny. Let me enumerate below why, and under which circumstances, our C++ symbol export and ABI hookup develop serious inconsistencies and turn software builds into trouble.

#### More Complicated Naming Rules

The mapping from a C++ function to a linker symbol is decided by the compiler vendor. It is true that some standards exist to push our compiler vendors toward producing symbols that are as interoperable as possible, but unfortunately — taking g++ and MSVC as the example — a gap remains, so much so that a project built with the MSVC compiler cannot painlessly hand its symbols straight to a project built with g++ (my other point being: without taking certain measures, we would have to obtain the source and recompile, and the methods we discuss later on finally let us dodge that move).

Readers will ask: what exactly is going on here? Actually, it is easy to think of a stretch of code like this:

```c++
// In C++, we love putting some of our methods into classes,
// OOP is exactly what advocates doing this!
class Foo {
public:
    void someFunc(int a, const char* b);
};

// Or, we like to put utility-style functions into a separate namespace
namespace charlies_tools {
   std::vector<std::string_view> split(const std::string& waited_splits, const char ch);
   std::vector<std::string_view> split(const std::string& waited_splits, const std::string_view sp_view);
};

```

As C++ programmers, we reach for these features quite naturally — they steer us around symbol-level collisions and make for better readability in software engineering.

Let's take a look at what the symbol names produced by compiling with g++ look like:


```text

0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000022 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000000 T _ZN3Foo8someFuncEiPKc

```

And then let's look at what MSVC produces:


```text

00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))

```

In fact, we can see that the symbols written into the relocatable files look completely different, which shows that there is simply no way to make our symbols interoperable. On top of that, we still have overloading and a whole series of such features — the technique that lets identical function names with different parameter lists coexist inside one object file — which forces our toolchains to burn real effort coping with these problems.

This decoration is called name mangling. Great — now we have no choice but to deal with these miserable problems.

#### The Static-Storage Data Initialization Problem

In C, our data can mostly be considered trivial (aha — I would pick C too, at least it stays controllable), and for legacy-code reasons we are used to initializing these variables as early as the link stage. In C++, however, we know that this data can be objects, which means constructor calls are involved. If all of these objects sit under **initialization-order-independent conditions** (that is, the objects form no dependencies — it is never the case that static object A must be initialized before static object B can be), it is really no big deal. What we fear is order-dependent static objects: once the program is up and running on the CPU, there is generally no fixed constraint on the order in which these objects get initialized, so random program crashes come all too easily.

Fortunately, this one is easy to handle. We know that the initialization of data scattered freely across the data segment is uncertain in timing; but if we place it inside a function, the object is initialized only when execution reaches it. Therefore, supposing static object A really does need to be initialized before static object B, we can do it like this:

```cpp
static void init_a_and_b() {
    static A network_instance;
    static B authentic_networks;
}

auto dummy = [](){
    init_a_and_b();
    return 0;
}();

```

## So, How to Design a Binary Interface with Fewer Headaches

#### Design a C-Style Export Interface

Of course, you are by no means obliged to actually guard against collisions the way a C programmer does, adopting C naming habits — what is being said here is: do not export under the C++-flavored, vendor-divergent ABI symbol rules. The way to do it is to decorate the symbols you have decided to export with the extern "C" marker.

```cpp

#ifdef __cplusplus
extern "C"{
#endif

    int functional_a(int a, int b);

#ifdef __cplusplus
}
#endif

```

With this, the interface as the linker sees it looks far cleaner.

#### Provide a Header with a Complete ABI Declaration

Here, "**a header providing a complete ABI declaration**" refers to a header file (`.h`) that contains all the necessary declarations, enabling the compiler to **fully understand** the interface of a library or module, so that it can:

1. **Correctly compile** the code that calls into the library.
2. **Correctly generate** machine code that interacts with the functions in the library.

The core of this "complete ABI declaration" is that it covers not only the function names, but every detail that affects interaction at the binary level. That is exactly why we have the saying — provide a header with a complete ABI declaration. Next, let's discuss what a header providing a complete ABI declaration contains:

##### Function Declarations

This is the most basic part. It tells the compiler the function's name, return type, and parameter types.

```cpp
// An incomplete declaration - we know the name and types, but problems may be hiding
int do_something(int a, int b);

// A more complete declaration - adds extern "C" and an exception specification
extern "C" int do_something(int a, int b) noexcept;

```

##### Type Definitions

If the interface uses custom structs or classes, their memory layout must be made explicit.

```cpp
// A complete struct declaration - the compiler can pin down its size and memory layout
struct MyData {
    int id;
    double value;
    char name[32];
};

// A function that consumes this struct
extern "C" void process_data(const MyData* data);

```

Without the complete definition of `MyData` in the header, the compiler has no way to know what `sizeof(MyData)` is, and cannot correctly allocate stack space or pass arguments for calls to `process_data`.

##### Macros and Constant Definitions

These define the magic numbers or configuration used by the interface.

```cpp
#define MAX_BUFFER_SIZE 1024
#define LIB_VERSION 0x00010002

extern "C" int initialize_lib(int buffer_capacity = MAX_BUFFER_SIZE);

```

##### Including Other Headers

If a declaration depends on other types (such as the standard library's `size_t`, or your own custom types), the corresponding headers need to be included.

```cpp
#include <stddef.h> // for size_t

extern "C" void* allocate_buffer(size_t size);

```

## A Modern CMake Perspective

The ABI design pitfalls discussed in this piece are, in modern projects, mostly taken over by CMake as the build system. `extern "C"` is still work you write by hand, but symbol visibility can be handled with `set_target_properties(foo PROPERTIES CXX_VISIBILITY_PRESET hidden)` to hide all symbols by default and then export on demand via the macros generated by `generate_export_header`, avoiding accidentally exposing every internal decorated C++ symbol to downstream users. `target_link_libraries(foo PUBLIC bar)` strings together transitive dependencies, header paths, and the `-l`/`-L` flags for you, so downstream only needs to link once. `add_library(foo SHARED)` automatically adds `-fPIC` to all the object files, sparing you the typing. When cross-platform ABI hookup is involved, set the dynamic library up with the `PUBLIC_HEADER` property and combine it with `install(TARGETS ...)`: on Unix, CMake drops the headers into `include/`; on Windows it takes care of distributing the import library together with the `__declspec(dllexport/dllimport)` handling — so that the C-style export interface you wrote truly lands as "one header, usable everywhere".

# Reference

## Verifying the Symbol Names

If you would like to see the symbol differences produced by the MSVC and g++ compilers with your own eyes, let me explain here how the results above were produced:

The MSVC compiler version I used is 19.44.35217, and the g++ version is 15.2.1.

We write the sample code from above into test.cpp:

```cpp
#include <string>
#include <string_view>

class Foo {
public:
 void someFunc(int a, const char* b);
};

namespace charlies_tools {
void split(const std::string& waited_splits, const char ch);
void split(const std::string& waited_splits, const std::string_view sp_view);
};

void Foo::someFunc(int a, const char* b) { }
void charlies_tools::split(const std::string& waited_splits, const char ch) { }
void charlies_tools::split(const std::string& waited_splits, const std::string_view sp_view) { }

```

Then, on a Linux machine, use the `-c` flag to translate test.cpp into machine code only:


```bash

g++ -c test.cpp -o test_name

```

Then, use the `nm` tool to inspect the ABI:


```text

[charliechen@Charliechen runaable_dynamic_library]$ nm test_name
0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000022 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000000 T _ZN3Foo8someFuncEiPKc

```

And that yields exactly the results I listed in the body above.

For MSVC, you need to open the VS Developer Prompt to initialize the MSVC toolchain environment. Again, we assume you saved the code as test.cpp; then, invoking the cl compiler with the compile-only flag and the latest C++ standard flag, you get the output below:


```text

D:\DownloadFromInternet>cl /c /std:c++latest test.cpp
用于 x86 的 Microsoft (R) C/C++ 优化编译器 19.44.35217 版
版权所有(C) Microsoft Corporation。保留所有权利。

/std:c++latest 作为最新的 C++
working 草稿中的语言功能预览提供。我们希望你提供有关 bug 和改进建议的反馈。
但是，请注意，这些功能按原样提供，没有支持，并且会随着工作草稿的变化
而更改或移除。有关详细信息，请参阅
https://go.microsoft.com/fwlink/?linkid=2045807。

test.cpp

```

Afterwards, with the small dumpbin utility, we get:


```text

D:\DownloadFromInternet>dumpbin /SYMBOLS test.obj
Microsoft (R) COFF/PE Dumper Version 14.44.35217.0
Copyright (C) Microsoft Corporation.  All rights reserved.

Dump of file test.obj

File Type: COFF OBJECT

COFF SYMBOL TABLE
000 01058991 ABS    notype       Static       | @comp.id
001 80010191 ABS    notype       Static       | @feat.00
002 00000003 ABS    notype       Static       | @vol.md
003 00000000 SECT1  notype       Static       | .drectve
    Section length  178, #relocs    0, #linenums    0, checksum        0
005 00000000 SECT2  notype       Static       | .debug$S
    Section length   74, #relocs    0, #linenums    0, checksum        0
007 00000000 SECT3  notype       Static       | .bss
    Section length    4, #relocs    0, #linenums    0, checksum        0, selection    2 (pick any)
009 00000000 SECT3  notype       External     | __Avx2WmemEnabledWeakValue
00A 00000000 SECT4  notype       Static       | .text$mn
    Section length   25, #relocs    0, #linenums    0, checksum E54AE742
00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))
00F 00000000 SECT5  notype       Static       | .chks64
    Section length   28, #relocs    0, #linenums    0, checksum        0

String Table Size = 0x123 bytes
  Summary
           4 .bss
          28 .chks64
          74 .debug$S
         178 .drectve
          25 .text$mn

```
