# Deep Dive into C/C++ Compilation Technology — Shared Libraries A3: A Discussion on Symbol Visibility

Some readers might be wondering—what exactly is symbol visibility? Is it the C++ keywords ``public`` or ``private``? It is worth pointing out that it is neither. The former are basic features provided alongside language syntax and compiler checks. Here, the symbol visibility we discuss is more aggressive, referring to visibility at the symbol ABI level.

#### Tips: How to View ABI Symbols

> Veterans can skip this section.

Since some readers might be encountering this article for the first time, they might not yet know how to "view the visible symbols contained in a given relocatable file, an executable composed of relocatable files, or a library file." I plan to specifically supplement how to perform this basic operation on the major Windows and Linux platforms.

##### GNU/Linux Platform

It is very simple; we only need to use the `nm` tool. Suppose we have a library file ``libsome_helpers.so`` ready for inspection. Entering the following command will do the trick.

```cpp

[charliechen@Charliechen runaable_dynamic_library]$ nm -D libsome_helpers.so
00000000000010e9 T add
                 w __cxa_finalize@GLIBC_2.2.5
                 w __gmon_start__
                 w _ITM_deregisterTMCloneTable
                 w _ITM_registerTMCloneTable
00000000000010fd T minus

```

##### Windows Platform

This is straightforward. Suppose I intend to inspect `CCWidget.dll`. To view the exported symbols, we use ``dumpbin /EXPORTS CCWidgets.dll``

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

## How Do Mainstream Toolchains Control Symbol Visibility?

Getting back to the point, how do mainstream toolchains control symbol visibility? We will discuss this separately.

#### How to Control Symbol Visibility on GNU/Linux

##### Method 1: Directly pass -fvisibility to the compiler to control all symbol exports

The first method is the most brute-force approach. Suppose we have a private dependency project and do not want to expose any symbols at all. In this case, we can pass `-fvisibility` to gcc/g++ during compilation. By default, for the GNU C/C++ toolchain, **any symbol without any visibility modifier or specified visibility** is public. That is, ``-fvisibility=default``. If we want to hide them, we need to specify ``-fvisibility=hidden`` in the step that generates the shared library, and all symbols will not be exported. However, I have not used this myself; I have only found that this usage exists.

##### Method 2: The most common approach: using ``__attribute__((visibility(< "default" | "hidden" >)))``

I really like specifying it this way. Taking a simple logging library I wrote as a toy project as an example, for all APIs planned to be public at the ABI level, I explicitly specify ``__attribute__((visibility("default")))``. Conversely, for any symbol that should not be used, I apply ``__attribute__((visibility("hidden")))``

```cpp

#ifdef CCLOG_BUILD_SHARED
#define CCLOG_API __attribute__((visibility("default")))
#define CCLOG_PRIVATE_API __attribute__((visibility("hidden")))
#else
#define CCLOG_API
#define CCLOG_PRIVATE_API
#endif

```

##### Method 3: Modifying a group of aggregated symbols with ``#pragma visibility push/pop``

If you really need to handle visibility modifications for a massive number of symbols on hand, but do not want to add the macros mentioned in my example above to each symbol one by one, you can use compiler preprocessor directives.

```cpp
#pragma visibility push("hidden")

int private_api_add(int a, int b);
int api_minus(int a, int b);

/* Remember to pop for preventing the leak of unwanted visibility decorations */
#pragma visibility pop

```

#### How Windows MSVC Handles This

Unfortunately, exporting symbols from Windows DLL shared libraries involves a relatively complex decoration mechanism. In other words, symbols planned for export need to be decorated with ``__declspec(dllexport)`` for export. Then, when using these symbols, we also need to mark them with ``__declspec(dllimport)``

```cpp
#ifdef CCLOG_BUILD_SHARED
/* If we plan to exports sysbols to DLL, we need to decorate symbols by this */
/* Others in case can use the symbols */
#define CCLOG_API __declspec(dllexport)
#else
/* If we plan to import sysbols from DLL, we need to decorate symbols by this */
#define CCLOG_API __declspec(dllimport)
#end

```
