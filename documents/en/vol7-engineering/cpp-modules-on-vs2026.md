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
title: How to Quickly Use C++ Modules in VS2026 — A Complete Hands-on Guide
description: ''
translation:
  source: documents/vol7-engineering/cpp-modules-on-vs2026.md
  source_hash: a86a0e8615636b9b711f199c8b5490f79cf3f0d7dac9061cf5a3f7d3df6689d3
  translated_at: '2026-06-16T04:08:13.037840+00:00'
  engine: anthropic
  token_count: 623
---
# How to Quickly Use C++ Modules in VS2026 — A Complete Hands-on Guide

## Introduction

Modern C++ introduced a breakthrough feature: modules. Although they have been around for a while (this feature came with C++20), VS support for modules is currently OK in some demo cases. I also plan to gradually try introducing modules into my toy projects to simplify dependency management.

------

## Why Use Modules

C++ Modules (C++20) are a compilation unit mechanism designed to replace traditional header files. Previously, if a source file changed, that source file needed to be completely recompiled. However, module incremental compilation analyzes down to the binary ABI level. MSVC modules (yes, they are not actually very interoperable with other compiler vendors) cache compilation artifacts via the Module Binary Interface (BMI). Furthermore, this export mechanism is more robust. Later, we will introduce two keywords to explain how module import and export work.

------

## Prerequisites

VS2022 is now hard to get (or at least not easy to obtain), which is why I am using VS2026. To successfully use modules in VS2026, please confirm the following items:

1. **Visual Studio 2026 (or later) is installed**, including the "Desktop development with C++" workload. VS2026 comes with MSVC Build Tools v14.50 (IDE 18.0), offering further improvements for modules and language compatibility. So now there is basically no burden, no need to enable any experimental features separately; it is mainstream now.
2. **C++ Standard Settings**: The project or command line uses `/std:c++20` or more conservatively `/std:c++latest` (VS2026's MSVC provides more complete support for modules). But don't worry, **VS2026 defaults to the options above, so you don't need to change anything; just take a look if you are concerned.**

------

## Minimal Runnable Example (Code and Step-by-Step Instructions)

Create a small project `DemoModule`, containing two files:

`Hello.ixx` (module interface unit):

```cpp
export module Hello;

export void SayHello() {
    // System console output
}
```

`main.cpp` (consuming the module):

```cpp
import Hello;

int main() {
    SayHello();
}
```

> Note: In the MSVC community, `.ixx` is the common module interface extension; you can also use `.cppm`, etc., but the default recognition of extensions by the IDE/toolchain may differ.

------

## Using Modules in the Visual Studio IDE (VS2026) — Steps

Visual Studio has handed off most module build details to MSBuild/IDE, so you usually only need to add files to the project:

1. **Create New Project**: `Empty Project` (select the Desktop development with C++ workload).
2. **Add module files to project**: Right-click project → Add → Existing Item → Add `Hello.ixx` and `main.cpp`.
3. **Confirm Language Settings**: Right-click project → Properties → C/C++ → Language → C++ Language Standard select `/std:c++20` or above (selecting `/std:c++latest` is also fine). At the same time, in Properties → C/C++ → Language, enable "Build C++23 Standard Library Modules" and set it to Yes.
4. **Build and Run**: The IDE will automatically scan module sources, generate BMIs, and correctly set the compilation and linking order; you usually do not need to manually specify `/export`. If dependencies between modules are complex (cross-project), you can use project references or configure Module References in Project Properties.

------

## Reference

- [Named Modules Tutorial in C++ | Microsoft Learn](https://learn.microsoft.com/zh-cn/cpp/cpp/tutorial-named-modules-cpp?view=msvc-170)
- [Tutorial: Import the Standard Library (STL) from the Command Line (C++) | Microsoft Learn](https://learn.microsoft.com/zh-cn/cpp/cpp/tutorial-import-stl-named-module?view=msvc-170)
- [Standard C++20 Modules support with MSVC in Visual Studio 2019 version 16.8 - C++ Team Blog](https://devblogs.microsoft.com/cppblog/standard-c20-modules-support-with-msvc-in-visual-studio-2019-version-16-8/)
