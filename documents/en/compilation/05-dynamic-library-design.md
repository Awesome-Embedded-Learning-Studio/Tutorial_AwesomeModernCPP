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
title: 'Deep Dive into C/C++ Compilation and Linking, Part 6 — A2: Dynamic Library Design Basics, the ABI Design Interface'
description: 'Get clear on the low-level pain of dynamic library ABI design: why C++ name mangling does not port across compilers, the static-object initialization-order trap, and how a C-style export interface plus a complete ABI header file lets you sidestep the ABI hookup mess.'
cpp_standard: [11, 14, 17, 20]
---
# Deep Dive into C/C++ Compilation and Linking, Part 6 — A2: Dynamic Library Design Basics, the ABI Design Interface

## Preface

In this post I'm trying to pull together some of the more important technical points on the **design** side of dynamic libraries — things like the design and export of the binary interface.

## So, how come we're dragging the binary interface into this

At its core, the whole end goal of designing a dynamic library (and I do think this is something you have to keep firmly in mind) is to hand our code over to other people for them to reuse. So the details of how that code collaboration actually works are exactly what we have to think about. Way back in an earlier post we already boiled the abstract concept of "dynamic library" down to this: a set of exported symbols written down in a header file or a dedicated export file, so other users know how to call into the target functionality — that's the **interface** — plus a bunch of hidden concrete machine code behind it.

But here's the thing. We know that the function names and global variable names sitting under various classes inside a human-readable file (say, a header file) really are an interface, but we also clearly know that's not a **binary interface**. For the longest time we've all sort of gotten used to the idea that as long as we exported the right symbols and shipped the concrete machine code, everything was hunky-dory. Except, thanks to C++'s freewheeling nature (and notice I did not say C — in practice this problem blows up almost entirely on reusable libraries written in C++), the **path from the human-readable API to the machine-to-machine ABI that each compiler vendor produces** is not consistent! And that births a whole series of problems that are not even a little bit funny. Let me lay out, point by point, exactly which situations make our C++ symbol export and ABI hookup go badly inconsistent and turn software builds into a headache.

#### A more complicated naming scheme

The mapping from a C++ function down to a linker symbol is decided by the compiler vendor. Sure, there are some standards out there nudging compiler vendors toward producing symbols that are as portable as possible, but unfortunately, taking g++ and MSVC as the example, there is still a gap — so much so that an MSVC-built project can't painlessly drop its symbols straight into a g++-built project (and by that I also mean: without taking some measures, we'd have to grab the source and recompile. The methods we get to later on are exactly what finally let us dodge that move).

You might be asking: how does that happen? Well, it's pretty easy to picture a chunk of code like this:

```c++
// In C++, we love sticking methods inside classes,
// OOP literally recommends we do this!
class Foo {
public:
    void someFunc(int a, const char* b);
};

// Or, we like putting utility-style functions into a dedicated namespace
namespace charlies_tools {
   std::vector<std::string_view> split(const std::string& waited_splits, const char ch);
   std::vector<std::string_view> split(const std::string& waited_splits, const std::string_view sp_view);
};

```

As C++ programmers, we reach for these features completely naturally — they sidestep a bunch of symbol-level collisions and make the code read better in a real software-engineering context.

Let's see what the symbol names look like coming out of g++:


```text

0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000022 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000000 T _ZN3Foo8someFuncEiPKc

```

And now here's what MSVC spits out:


```text

00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))

```

Honestly you can see the symbols written into the relocatable file look absolutely nothing alike, which tells us straight up that we can't portably share these symbols across the two. On top of that, we've got overloading and a whole pile of features that let us offer the same function name with different parameter lists and have them all coexist in one object file — and that means our toolchain has to bend over backwards to sort all of it out.

This decoration is called **name mangling**. Great. Now we get to deal with this mess.

#### Static-storage initialization

In C, our data can mostly be treated as trivial (honestly, I get why somebody would prefer C too — at least it's controllable). For legacy reasons we've gotten into the habit of initializing those variables back at link time. But in C++, as we know, that data can be an object, which means there's a constructor call involved. Now, if all those objects are **under conditions where initialization order doesn't matter** (meaning, none of them form a dependency — we don't have to insist that static object A get initialized before static object B), then it's honestly fine. The scary case is when you do have order-dependent static objects, because once the program is running on the CPU, the initialization order for those objects has no fixed constraint, and that's a really easy way to give yourself random crashes.

Of course, this one is pretty easy to handle. We know the initialization order of data scattered across the data segment is uncertain, but if we tuck it inside a function, then we only initialize the object at the moment execution actually reaches it. So if static object A really does have to be initialized before static object B, we can do something like:

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

## So, how do you design a binary interface with fewer headaches

#### Design a C-style export interface

Now, you don't have to actually go full C programmer and start dodging collisions using C naming conventions — what I mean here is just: don't export the C++-flavored ABI symbol rules that differ all over the place. The trick is to decorate the symbols you've decided to export with the `extern "C"` marker.

```cpp

#ifdef __cplusplus
extern "C"{
#endif

    int functional_a(int a, int b);

#ifdef __cplusplus
}
#endif

```

That way the interface the linker ends up seeing looks a whole lot cleaner.

#### Ship a header file with a complete ABI declaration

By "**a header file with a complete ABI declaration**" I mean a header file (`.h`) that carries all the declarations the compiler needs to **fully understand** a library's or module's interface, so it can:

1. **Correctly compile** the code that calls into the library.
2. **Correctly generate** the machine code that talks to the functions inside the library.

The heart of this "complete ABI declaration" is that it isn't just the function names — it covers every detail that affects interaction at the binary level. That's exactly why we say things like "ship a header file with a complete ABI declaration." So let's walk through what such a header actually contains:

##### Function declarations

This is the most basic part. It tells the compiler the function's name, its return type, and its parameter types.

```cpp
// Incomplete declaration - you only know the name and types,
// but problems can hide underneath
int do_something(int a, int b);

// A more complete declaration - adds extern "C" and a noexcept spec
extern "C" int do_something(int a, int b) noexcept;

```

##### Type definitions

If the interface uses a custom struct or class, its memory layout has to be pinned down explicitly.

```cpp
// Complete struct declaration - the compiler can pin down its size and memory layout
struct MyData {
    int id;
    double value;
    char name[32];
};

// A function that uses this struct
extern "C" void process_data(const MyData* data);

```

If the header file doesn't carry the complete definition of `MyData`, the compiler has no idea what `sizeof(MyData)` is, and it can't correctly allocate stack space or pass arguments for the call to `process_data`.

##### Macros and constant definitions

These are for the magic numbers or configuration values used inside the interface.

```cpp
#define MAX_BUFFER_SIZE 1024
#define LIB_VERSION 0x00010002

extern "C" int initialize_lib(int buffer_capacity = MAX_BUFFER_SIZE);

```

##### Including other headers

If a declaration depends on other types (like the standard library's `size_t`, or a custom type), you need to pull in the matching headers.

```cpp
#include <stddef.h> // so we can use size_t

extern "C" void* allocate_buffer(size_t size);

```

## A modern CMake perspective

Most of the ABI-design pain covered in this piece gets taken over by the CMake build system in modern projects. The `extern "C"` part is still hand work on your end, but symbol visibility can be driven by `set_target_properties(foo PROPERTIES CXX_VISIBILITY_PRESET hidden)` to hide every symbol by default, then export only the ones you want through the macros that `generate_export_header` spits out — so you don't accidentally leak all your internal C++ mangled symbols downstream. `target_link_libraries(foo PUBLIC bar)` strings together the transitive dependencies, header search paths, and `-l` / `-L` for you, so the downstream side only has to link once. `add_library(foo SHARED)` automatically feeds `-fPIC` to every object file, saving you the typing. When the ABI hookup has to be cross-platform, set the `PUBLIC_HEADER` property on the dynamic library and pair it with `install(TARGETS ...)`; on Unix CMake drops the headers into `include/`, and on Windows it handles the import-library side of `__declspec(dllexport/dllimport)`. That's what actually turns the C-style export interface you wrote by hand into "one header, usable everywhere."

# Reference

## Confirming the names

If you want to see the symbol difference between the MSVC compiler and g++ with your own eyes, let me walk through how I produced the results above.

The MSVC compiler version I used is 19.44.35217, and the g++ version is 15.2.1.

Let's drop the sample code above into test.cpp:

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

Then use `nm` to inspect the ABI:


```text

[charliechen@Charliechen runaable_dynamic_library]$ nm test_name
0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000022 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000000 T _ZN3Foo8someFuncEiPKc

```

And that's the result I quoted in the body of the post.

For MSVC, you need to open the VS Developer Prompt to initialize the MSVC toolchain environment. Same as before, let's say you've saved the code to test.cpp; then, using the `cl` compiler and passing a compile-only flag plus the latest C++ standard flag, you'll get the following output:


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

Then, using the `dumpbin` little tool, you get:


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
