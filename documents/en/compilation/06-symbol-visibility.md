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
title: "Deep Dive into C/C++ Compilation and Linking · Part 6: Dynamic Libraries A3 — Let's Talk About Symbol Visibility"
description: 'A chat about symbol visibility at the ABI layer: inspecting exported symbols with nm/dumpbin, plus four ways to control them — GCC -fvisibility, __attribute__((visibility)), #pragma visibility, and MSVC __declspec(dllexport/dllimport)'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/06-symbol-visibility.md
  source_hash: 160f79976569720f69e451c8cabf92b707c4b807537e5c89f5a7101e213ec989
  translated_at: '2026-09-26T00:01:57+00:00'
  engine: anthropic
  token_count: 2000
---
# Deep Dive into C/C++ Compilation and Linking · Part 6: Dynamic Libraries A3 — Let's Talk About Symbol Visibility

Some friends of mine might find this odd — what on earth is symbol visibility? Is it those C++ keywords of ours, `public` or `private`? Worth pointing out: no, it is not. Those are a baseline feature that the language grammar and the compiler's checks hand to you as a package deal. The symbol visibility we are discussing here is a more aggressive beast: it refers to visibility at the ABI layer of the symbol.

#### Tips: How to Inspect ABI Symbols

> Veterans can skip this one

Since some of you may be reading this article for the first time and may not yet know how to carry out "inspect the visible symbols contained in a given relocatable file, or in an executable or library file assembled from relocatable files", I plan to set aside a moment here and cover how this basic operation is done on the two major platforms, Windows and Linux.

##### GNU/Linux

Simple enough — we just reach for the nm tool. Say we have a library file `libsome_helpers.so` ready for inspection; entering the command below gets it done.


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

This one is easy too. Say the file I want to inspect is CCWidgets.dll — to view its exported symbols, run `dumpbin /EXPORTS CCWidgets.dll`


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
          3    2 00002FB0 ??0AnimationConfig@animation@CCWidgetLibrary@@QEAA@XZ
          4    3 00002FD0 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@$$QEAU012@@Z
          5    4 00003010 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@AEBU012@@Z
          6    5 00003050 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@XZ
          7    6 00012E00 ??0AppearAnimation@animation@CCWidgetLibrary@@QEAA@PEAVQWidget@@@Z
          8    7 000184E0 ??0CCBadgeLabel@@QEAA@PEAVQWidget@@@Z
          9    8 00014130 ??0CCButton@@QEAA@AEBVQIcon@@AEBVQString@@PEAVQWidget@@@Z
         10    9 000141F0 ??0CCButton@@QEAA@AEBVQString@@PEAVQWidget@@@Z、
         ...

```

## How the Mainstream Toolchains Control Symbol Visibility

So, back to the main topic: how do the mainstream toolchains control symbol visibility? We will take them one at a time.

#### Controlling Symbol Visibility Under GNU/Linux

##### Option 1: Pass -fvisibility Straight to the Compiler to Control the Export of All Symbols

The first approach is the bluntest of the bunch. Say we have a private dependency project whose symbols we do not want to expose at all — in that case, we can pass -fvisibility to gcc/g++ at compile time. By default, the GNU C/C++ toolchain treats **any symbol that carries no visibility decoration and no explicitly specified visibility** as public — that is `-fvisibility=default`. If we want things hidden, then in the step that produces the dynamic library we need to specify `-fvisibility=hidden`, and none of the symbols get exported. I have never actually used this one myself, mind you — I merely looked up that the usage exists.

##### Option 2: The Most Common Approach — `__attribute__((visibility(< "default" | "hidden" >)))`

I am quite fond of specifying it this way. Take the simple logging library I once wrote as a toy: for every API that is planned to be public at the ABI layer, I forcibly attach `__attribute__((visibility("default")))`; conversely, any symbol that is not supposed to be used gets `__attribute__((visibility("hidden")))` stamped on it.


```cpp

#ifdef CCLOG_BUILD_SHARED
#define CCLOG_API __attribute__((visibility("default")))
#define CCLOG_PRIVATE_API __attribute__((visibility("hidden")))
#else
#define CCLOG_API
#define CCLOG_PRIVATE_API
#endif

```

##### Option 3: Decorating a Whole Group of Symbols with `#pragma visibility push/pop`

Now, suppose you really do have an enormous pile of symbols on hand whose visibility needs changing, and you do not want to glue the macro from my example above onto them one symbol at a time — you can turn to the compiler's preprocessing directives.

```cpp
#pragma visibility push("hidden")

int private_api_add(int a, int b);
int api_minus(int a, int b);

/* Remember to pop for preventing the leak of unwanted visibility decorations */
#pragma visibility pop

```

#### How It Is Done on Windows with MSVC

Unfortunately, exporting symbols from a Windows DLL dynamic library comes with a comparatively convoluted decoration mechanism. That is, a symbol you plan to export must be decorated with `__declspec(dllexport)` to be exported; and when it comes time to use those symbols, we in turn need to mark them `__declspec(dllimport)`.

```cpp
#ifdef CCLOG_BUILD_SHARED
/* If we plan to export symbols to DLL, we need to decorate symbols by this */
/* Others in case can use the symbols */
#define CCLOG_API __declspec(dllexport)
#else
/* If we plan to import symbols from DLL, we need to decorate symbols by this */
#define CCLOG_API __declspec(dllimport)
#endif

```

## A Modern CMake Perspective

All that hands-on work with `-fvisibility=hidden`, `__attribute__((visibility))`, and `-fPIC` above is, in projects managed with CMake, mostly taken over by the build system. `add_library(foo SHARED ...)` adds `-fPIC` to the target by default (static libraries do not get it by default — turn on `set(CMAKE_POSITION_INDEPENDENT_CODE ON)` when you need it). To hide symbols across the board, set `set_target_properties(foo PROPERTIES CXX_VISIBILITY_PRESET hidden)` on the target, and CMake will feed `-fvisibility=hidden` to the compiler for you; pair it with `VISIBILITY_INLINES_HIDDEN ON` and the inline functions get tucked away too. As for the `dllexport`/`dllimport` toggling dance on Windows, CMake provides `GenerateExportHeader`: one macro generates a cross-platform `FOO_API` macro for you — on Linux it expands to the `visibility` attribute, while on Windows it automatically expands into `dllexport` or `dllimport` depending on whether, at compile time, you are building the library or using it, sparing you the hand-written `#ifdef` plumbing. So if you are writing a library today, most of these low-level decorations no longer need to be typed out by hand — a line or two in the CMake target's property panel sets it all up.
