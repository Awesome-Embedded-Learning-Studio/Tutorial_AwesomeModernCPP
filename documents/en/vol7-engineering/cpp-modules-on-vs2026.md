# How to Quickly Use C++ Modules in VS2026 — A Complete Hands-On Guide

## Preface

Modern C++ introduced a truly breakthrough feature: modules. Although they have been around for a while (this feature debuted in C++20), VS currently has decent module support in some demo cases. I also plan to gradually start introducing modules into my toy projects to simplify dependency management.

------

## Why Use Modules

C++ modules (C++20) are a compilation unit mechanism designed to replace traditional header files. Previously, if a source file changed, it had to be completely recompiled. However, module incremental compilation analyzes changes down to the binary ABI level. MSVC modules (yes, they are not fully interoperable with other compiler vendors) cache compilation artifacts through the Module Binary Interface/BMI. Furthermore, this new export mechanism is much more robust. Later, we will introduce two keywords to show you how module import and export work.

------

## Prerequisites

VS2022 is no longer easy to obtain (at least, it is not straightforward to get), which is why I am using VS2026. To use modules smoothly in VS2026, please confirm the following:

1. **Visual Studio 2026 (or newer) is installed**, including the "Desktop development with C++" workload. VS2026 ships with MSVC Build Tools v14.50 (IDE 18.0), bringing further improvements to module and language compatibility. So there is essentially no burden now — we do not need to manually enable any experimental features, as this has long been mainstreamed.
2. **C++ standard setting**: The project or command line uses `/std:c++20`, or more conservatively, `/std:c++latest` (VS2026's MSVC provides more complete module support). But don't worry, **VS2026 defaults to the options above, so you don't need to change anything. If you are concerned, just take a quick look.**

------

## Minimal Working Example (Code and Step-by-Step Explanation)

Create a small project `vs2026-modules-demo/` containing two files:

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

> Note: In the MSVC community, `.ixx` is a common module interface extension; you can also use `.cppm`, but the default recognition of extensions by the IDE/toolchain may vary.

------

## Using Modules in the Visual Studio IDE (VS2026) — Steps

Visual Studio delegates most module build details to MSBuild/IDE, so we usually just need to add the files to the project:

1. **Create a new project**: `Console App (C++)` (select the Desktop development with C++ workload).
2. **Add module files to the project**: Right-click the project → Add → Existing Item → add `math.ixx` and `main.cpp`.
3. **Confirm language settings**: Right-click the project → Properties → C/C++ → Language → set `C++ Language Standard` to `ISO C++20` or above (selecting `Preview` is also fine). Additionally, under Properties → C/C++ → Language, set the option to build C++23 standard library modules to Yes.
4. **Build and run**: The IDE automatically scans module sources, generates BMIs, and correctly sets the compilation and linking order. We usually do not need to manually specify `.obj`. If module dependencies are complex (cross-project), we can use project references or configure Module References in Project Properties.

------

## Reference

- [Named modules tutorial in C++ | Microsoft Learn](https://learn.microsoft.com/zh-cn/cpp/cpp/tutorial-named-modules-cpp?view=msvc-170)
- [Tutorial: Import standard library (STL) modules in the command line (C++) | Microsoft Learn](https://learn.microsoft.com/zh-cn/cpp/cpp/tutorial-import-stl-named-module?view=msvc-170)
- [Standard C++20 Modules support with MSVC in Visual Studio 2019 version 16.8 - C++ Team Blog](https://devblogs.microsoft.com/cppblog/standard-c20-modules-support-with-msvc-in-visual-studio-2019-version-16-8/)
