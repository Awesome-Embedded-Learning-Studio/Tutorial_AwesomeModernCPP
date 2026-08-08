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
title: "Deep Dive into C/C++ Compilation — Dynamic Libraries A3: Let's Talk Symbol Visibility"
description: 'A chat about symbol visibility at the ABI layer: inspecting exported symbols with nm/dumpbin, and the four ways to control it — GCC''s -fvisibility, __attribute__((visibility)), #pragma visibility, and MSVC''s __declspec(dllexport/dllimport).'
cpp_standard: [11, 14, 17, 20]
---
# Deep Dive into C/C++ Compilation — Dynamic Libraries A3: Let's Talk Symbol Visibility

Some of you reading along might be wondering — what exactly *is* symbol visibility? Is it the same as those C++ keywords, `public` or `private`? Worth pointing out: no, it isn't. Those two are a baseline feature handed to you as a package deal by the language syntax and the compiler's checks. What we're discussing here, symbol visibility, is something more aggressive — it refers to visibility at the ABI layer of a symbol.

#### Tips: How to Inspect ABI Symbols

> Veterans can skip this one.

Since some of you might be landing on this article for the first time and may not yet be clear on how to pull off "inspect the visible symbols contained in a given relocatable object file, or in an executable / library file built from relocatable files", I'm planning to take a moment here and fill in how to do this basic operation on the major Windows and Linux platforms.

##### GNU/Linux

Simple enough, we just reach for the `nm` tool. Say we have a library file `libsome_helpers.so` ready to inspect — punch in the command below and you're done.


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ nm -D libsome_helpers.so
00000000000010e9 T add
                 w __cxa_finalize@GLIBC_2.2.5
                 w __gmon_start__
                 w _ITM_deregisterTMCloneTable
                 w _ITM_registerTMCloneTable
00000000000010fd T minus

```

##### Windows

This one's easy too. Say I want to inspect `CCWidget.dll` — to see its exported symbols, it's `dumpbin /EXPORTS CCWidgets.dll`


```cpp

D:\NewQtProjects\CCWidgetLibrary\build\Desktop_Qt_6_10_0_MSVC2022_64bit-Release\widgets>dumpbin /EXPORTS CCWidgets.dll
Microsoft (R) COFF/PE Dumper Version 14.44.35217.0
Copyright (C) Microsoft Corporation.  All rights reserved.

Dump of file CCWidgets.dll

File Type: DLL

  Section contains the following exports for CCWidgets.dll

    00000000 characteristics
    FFFFFFFF time date stamp
        0.00 version
           1 ordinal base
         481 number of functions
         481 number of names

    ordinal hint RVA      name

          1    0 00002F50 ??0AnimationConfig@animation@CCWidgetLibrary@@QEAA@$$QEAU012@@Z
          2    1 00002F80 ??0AnimationConfig@animation@CCWidgetLibrary@@QEAA@AEBU012@@Z
          3    2 00002F50 ??0AnimationConfig@animation@CCWidgetLibrary@@QEAA@XZ
          4    3 00002FD0 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@$$QEAU012@@Z
          5    4 00003010 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@AEBU012@@Z
          6    5 00003050 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@XZ
          7    6 00012E00 ??0AppearAnimation@animation@CCWidgetLibrary@@QEAA@PEAVQWidget@@@Z
          8    7 000184E0 ??0CCBadgeLabel@@QEAA@PEAVQWidget@@@Z
          9    8 00014130 ??0CCButton@@QEAA@AEBVQIcon@@AEBVQString@@PEAVQWidget@@@Z
         10    9 000141F0 ??0CCButton@@QEAA@AEBVQString@@PEAVQWidget@@@Z、
         ...

```

## How Do the Mainstream Toolchains Control Symbol Visibility?

So back on topic — how do the mainstream toolchains control symbol visibility? Let's split it up and take them one at a time.

#### How to Control Symbol Visibility Under GNU Linux

##### Way 1: Pass -fvisibility Straight to the Compiler to Control Export of All Symbols

The first way is the bluntest. Say we have a private dependency project that we absolutely don't want to expose any symbols from — at compile time we can hand `-fvisibility` to gcc/g++. By default, the GNU C/C++ toolchain treats **any symbol that hasn't been given any visibility decoration or an explicit visibility** as public. That is, `-fvisibility=default`. If we want to hide them, then in the step that builds the dynamic library we need to set it to `-fvisibility=hidden`, and all the symbols will go un-exported. I haven't actually used this one myself, for what it's worth — just dug up that the usage exists.

##### Way 2: The Most Common Approach — Using `__attribute__((visibility(< "default" | "hidden" >)))`

I really like specifying it this way. Taking a simple logging library I threw together as a toy project as the example: for every API I plan to make public at the ABI layer, I force `__attribute__((visibility("default")))` on it; conversely, any symbol that shouldn't be used gets slapped with `__attribute__((visibility("hidden")))`.


```cpp

#ifdef CCLOG_BUILD_SHARED
#define CCLOG_API __attribute__((visibility("default")))
#define CCLOG_PRIVATE_API __attribute__((visibility("hidden")))
#else
#define CCLOG_API
#define CCLOG_PRIVATE_API
#endif

```

##### Way 3: Decorating a Cluster of Aggregated Symbols with `#pragma visibility push/pop`

Say you've genuinely got a huge pile of symbols on your hands whose visibility you need to flip, and you don't want to glue the macro I used as an example above onto them one symbol at a time — you can reach for the compiler's preprocessing directive.

```cpp
#pragma visibility push("hidden")

int private_api_add(int a, int b);
int api_minus(int a, int b);

/* Remember to pop for preventing the leak of unwanted visibility decorations */
#pragma visibility pop

```

#### How Windows MSVC Does It

Bad news here — exporting symbols from a Windows DLL dynamic library comes with a comparatively fussy decoration mechanism. That is, every symbol you plan to export needs to be decorated with `__declspec(dllexport)` to be exported; and then when we go to use those symbols, we still have to tag them with `__declspec(dllimport)`.

```cpp
#ifdef CCLOG_BUILD_SHARED
/* If we plan to exports symbols to DLL, we need to decorate symbols by this */
/* Others in case can use the symbols */
#define CCLOG_API __declspec(dllexport)
#else
/* If we plan to import symbols from DLL, we need to decorate symbols by this */
#define CCLOG_API __declspec(dllimport)
#endif

```

## From a Modern CMake Perspective

All that hand-rolled `-fvisibility=hidden`, `__attribute__((visibility))`, `-fPIC` legwork — in a project managed by CMake, the build system has basically taken it off your hands. `add_library(foo SHARED ...)` slaps `-fPIC` onto the target for you by default (static libraries don't get it by default; reach for `set(CMAKE_POSITION_INDEPENDENT_CODE ON)` when you need it). Want to hide symbols across the board? Set `set_target_properties(foo PROPERTIES CXX_VISIBILITY_PRESET hidden)` on the target and CMake will feed `-fvisibility=hidden` to the compiler for you; pair it with `VISIBILITY_INLINES_HIDDEN ON` and the inline functions get tucked away too. As for that whole `dllexport` / `dllimport` back-and-forth on Windows, CMake ships `GenerateExportHeader` — one macro generates a cross-platform `FOO_API` for you: on Linux it expands to the `visibility` attribute, and on Windows it auto-expands into `dllexport` or `dllimport` depending on whether, at compile time, you're building the library or using it, saving you from hand-writing the `#ifdef` plumbing yourself. So if you're writing a library today, most of this low-level decoration is something you don't have to type by hand — a line or two in the CMake target's property panel and you're all set.
