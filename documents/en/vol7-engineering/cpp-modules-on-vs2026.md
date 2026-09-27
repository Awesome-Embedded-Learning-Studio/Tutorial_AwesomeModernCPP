---
chapter: 1
difficulty: intermediate
order: 7
platform: host
reading_time_minutes: 3
tags:
- cpp-modern
- host
- intermediate
title: Up and Running with C++ Modules in VS2026 — A Complete Hands-On Guide
description: ''
translation:
  source: documents/vol7-engineering/cpp-modules-on-vs2026.md
  source_hash: a86a0e8615636b9b711f199c8b5490f79cf3f0d7dac9061cf5a3f7d3df6689d3
  translated_at: '2026-09-27T02:37:40+00:00'
  engine: anthropic
  token_count: 2000
---
# Up and Running with C++ Modules in VS2026 — A Complete Hands-On Guide

## Introduction

Modern C++ brought in a genuinely breakthrough feature: modules. They have had some time to mature by now (this thing shipped with C++20), and in demo cases VS's support for modules is already OK. We are also planning to give it a try — gradually introducing modules into some of our toy projects to simplify how the project's dependency handling works.

------

## Why Use Modules

C++ modules (C++20) are a compilation-unit mechanism meant to replace traditional header files. Before, whenever a source file changed, that entire source file had to be recompiled from scratch. Module-based incremental compilation, however, analyzes things down to the binary ABI level. MSVC's modules (yes, they are in practice not very interoperable with other compiler vendors) cache compilation artifacts via the module binary interface / BMI, and this time around the export story is more robust. Later we will introduce two keywords to show you how module import and export actually work.

------

## Prerequisites

VS2022 is no longer up for download these days (at least it is not easy to get hold of), which is exactly why we went with VS2026. To use modules smoothly on VS2026, confirm the following:

1. **Visual Studio 2026 (or newer) is installed**, including the "Desktop development with C++" workload. VS2026 ships with MSVC Build Tools v14.50 (IDE 18.0), with further improvements to modules and language compatibility. So these days there is essentially no burden — no need to separately enable any experimental features; it went mainstream a long time ago.
2. **C++ standard settings**: the project or command line uses `/std:c++20`, or more conservatively `/std:c++latest` (VS2026's MSVC provides more complete support for modules). But don't worry — **VS2026 already defaults to the option above, so there is nothing to change; if you are the cautious type, just take one look to make sure**

------

## Minimal Runnable Example (Code and Step-by-Step Instructions)

Create a small project `vs2026-modules-demo/` with two files:

`math.ixx` (module interface unit):

```cpp
export module math;

export int add(int a, int b) {
    return a + b;
}

export struct Point { int x, y; };

```

`main.cpp` (consuming the module):

```cpp
import std;
import math;

int main()
{
 std::print("Add Result: {}", add(1, 2));
 Point p{ 1,2 };
 std::print("Point p ({}, {})\n", p.x, p.y);
 return 0;
}

```

> Note: In the MSVC community, `.ixx` is the common module interface extension; you can also use `.cppm` and the like, but the IDE/toolchain may recognize extensions differently by default.

------

## Using Modules in the Visual Studio IDE (VS2026) — Steps

Visual Studio has already handed most of the module build details over to MSBuild/the IDE, so usually all you need to do is add the files to the project:

1. **Create a new project**: `Console App (C++)` (with the Desktop development with C++ workload selected).
2. **Add the module files to the project**: right-click the project → Add → Existing Item → add `math.ixx` and `main.cpp`.
3. **Confirm the language settings**: right-click the project → Properties → C/C++ → Language → for `C++ Language Standard` choose `ISO C++20` or above (choosing `Preview` works too). At the same time, still under Properties → C/C++ → Language, set the "Build C++23 Standard Library Modules" option to Yes.
4. **Build and run**: the IDE automatically scans module sources, generates BMIs, and sets the compilation and linking order correctly; you normally do not need to manually specify `.obj` files. If dependencies between modules get complicated (cross-project), you can use project references or configure Module References in Project Properties.

------

## Reference

- [Named Modules Tutorial in C++ | Microsoft Learn](https://learn.microsoft.com/zh-cn/cpp/cpp/tutorial-named-modules-cpp?view=msvc-170)
- [Tutorial: Import the Standard Library (STL) via Modules from the Command Line (C++) | Microsoft Learn](https://learn.microsoft.com/zh-cn/cpp/cpp/tutorial-import-stl-named-module?view=msvc-170)
- [Standard C++20 Modules support with MSVC in Visual Studio 2019 version 16.8 - C++ Team Blog](https://devblogs.microsoft.com/cppblog/standard-c20-modules-support-with-msvc-in-visual-studio-2019-version-16-8/)
