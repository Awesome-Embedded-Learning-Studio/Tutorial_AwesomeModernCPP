---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 6
platform: host
reading_time_minutes: 20
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: Compilers, Toolchains, and Project Design Ground Rules
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/06-toolchain-and-project-design.md
  source_hash: 1ae336104031473913510c392f13141c0da0e5eb3517454a6ad397cbad699490
  translated_at: '2026-09-26T15:33:28+00:00'
  engine: anthropic
  token_count: 4700
  notes: '原文一处数字断言按文意译出：中文源「首先是头文件 math_utils.h，就声明一个函数」原文疑为「就声明两个函数」——紧随其后的代码块实际声明了 square 与 add_one 两个函数，故英文作 "a couple of functions"。'
---
# The C++ Assembly Project: Compilers, Toolchains, and Those "Not in the Standard but Excellent" Libraries

Many programmers' understanding of the C++ ecosystem stops at "the language itself plus the standard library"—write code, compile, run, done. But map out the entire engineering workflow and you realize the C++ language itself is only one small piece of the whole project. Actually assembling a set of components into something that runs takes far more than C++ syntax. That is what we want to talk about today: this "assembly" process, and the infrastructure that supports it.

## First, a Correction: Not All Good Things End Up in the Standard

Many people hold a deep-rooted misconception: if a library is good enough and important enough, it "should" be absorbed into the standard library. Seeing `std::optional` land in C++17<RefLink :id="2" preview="std::optional (C++17)" /> and `std::format` land in C++20<RefLink :id="3" preview="std::format (C++20)" />, they take it for granted that this is the destination of every excellent library. In reality, nothing of the sort.

The standardization process has its own logic and its own bar. Some libraries' patterns simply do not fit in the standard, or their maintainers never intended to submit them at all—they exist as independent, high-quality libraries, and you can just use them directly. The most typical example is Abseil<RefLink :id="4" preview="Google Abseil C++ library" />. Google's open-source C++ library suite is full of extremely practical components, such as `absl::StatusOr`, `absl::Span`, enhanced versions of `absl::string_view`, and so on. They never entered the standard, and they do not need to—but the quality is superb, and plenty of production environments run on them.

One more point worth noting: it is not only massive projects backed by big companies that can make it into the standard. Small coalitions, even individuals, can get code into the standard as long as the proposal is solid and the argument thorough. Admittedly, coalitions of GPU vendors and large HPC institutions do push the standard hard, which is why things like parallel computing and SIMD have advanced particularly quickly. But the key point is that the channel is open—it is not a game only giants get to play.

So the right mindset is: stop staring at the standard library waiting for the "official solution," and actively go looking for mature, high-quality third-party libraries. The C++ ecosystem lacks crates.io-style centralized distribution like Rust's, so finding libraries does take more effort—but the good stuff is out there.

## The Real Assembly Starts After the Code Is Written

Alright, suppose the components are chosen and the code is written. What comes next? Turning "C++ code" into "an executable" requires far more than C++ itself.

First, we need a compiler. We are actually quite fortunate today: we have the three major players—GCC, Clang, and MSVC—plus EDG<RefLink :id="5" preview="EDG commercial C++ front end" /> (used mainly for standards-conformance testing and certain commercial settings). These compilers are all high quality, and some of them are open-source projects maintained by the community. You may take that for granted, but a look back at history shows how far we have come.

The earliest C++ compiler was essentially Cfront<RefLink :id="6" preview="Cfront: the earliest C++ compiler" />, written by Bjarne Stroustrup—a C++-to-C translator. It took in C++ code, converted it into C code, and then handed that intermediate product to an ordinary C compiler. C++ originally lived as a "parasite" on C's compilation infrastructure.

Today, of course, it is a completely different story. GCC and Clang both have mature C++ front ends, and support for each standard version keeps improving. Our current main environment runs GCC 16.1.1 on Arch Linux WSL, with Clang 17 for cross-validation, and occasionally MSVC 19.38 on Windows to make sure everything stays cross-platform. We have hit plenty of potholes on toolchain versions—enough for a separate article later.

But the compiler is only the first step. Once the individual translation units are compiled into object files, a linker has to stitch them together. Most people use C++ for years without ever giving the linker a proper look—because in the common case, a single command, `g++ main.cpp other.cpp`, does everything, and the linker works silently in the background, unnoticed. Until you hit a bizarre link error caused by an ODR (One Definition Rule) violation—the same inline function expanded into different versions in two translation units, and the linker reporting a completely unreadable symbol conflict—and only then do you realize how complex and important the linker really is.

The core point: when we complain that "C++ is hard to use," we are often complaining not about the C++ language itself but about some link in this assembly chain—maybe the compiler dumped a pile of unreadable template errors, maybe the linker cannot find a symbol, maybe we have no idea how to integrate a third-party library correctly. Break these links apart and each one has its own tools and solutions; they are just scattered everywhere, and it is on you to assemble them.

## A Simple Example to Experience "Assembly"

Here is a deliberately tiny example—no complex logic at all—just showing what the compiler and the linker each do in the process of going from "multiple source files" to "one executable."

First, the header `math_utils.h`, which just declares a couple of functions:

```cpp
// math_utils.h
// constexpr functions are implicitly inline ([dcl.constexpr]/1), so they can live
// in headers without violating the ODR—the compiler may also evaluate them
// directly at compile time
constexpr int square(int x) {
    return x * x;
}

// This function has a definition living in the header; inline prevents ODR violations
inline int add_one(int x) {
    return x + 1;
}
```

Then another header, `format_utils.h`, which depends on `math_utils.h` above:

```cpp
// format_utils.h
#include "math_utils.h"
#include <string>

// Formats the computed result into a string
// Deliberately not using std::format (C++20) here—std::to_string keeps it simple
inline std::string describe(int x) {
    return "value=" + std::to_string(add_one(square(x)));
}
```

Finally, `main.cpp`:

```cpp
// main.cpp
#include "format_utils.h"
#include <iostream>

int main() {
    int input = 5;
    std::cout << describe(input) << std::endl;
    return 0;
}
```

This example is almost embarrassingly simple, but that makes it perfect for demonstrating the compilation pipeline step by step. You can drive each stage manually with these commands:

```bash
# Step 1: preprocess only, to see what the compiler actually sees
g++ -E main.cpp -o main.ii

# Step 2: compile without linking, producing an object file
g++ -c main.cpp -o main.o

# Step 3: link (this example has a single .o, so linking is trivial)
g++ main.o -o main

# Run
./main
# Output: value=26
```

If you inspect the preprocessed `main.ii` with `-E`, you will find the contents of `math_utils.h` and `format_utils.h` fully expanded into it. That is why function definitions in headers need `inline` or `constexpr`<RefLink :id="7" preview="constexpr is implicitly inline" />—otherwise, if two different `.cpp` files include the same header, the linker sees two copies of the definition and reports an ODR violation outright.

There is a common misconception about `inline`: many people think it is merely a hint that "suggests the compiler inline this call." In fact, `inline`'s real role in C++ is to permit the same function to be defined in multiple translation units without violating the ODR<RefLink :id="8" preview="the inline keyword and the ODR exemption" />. The inlining optimization itself is entirely up to the compiler; whether you write `inline` or not has no necessary bearing on it.

## Compiler Selection: Current Practice

Day-to-day development is mostly GCC first, Clang second. The reason is simple: GCC has the best ecosystem on Linux and its diagnostics feel familiar; Clang's error messages are genuinely friendlier in some situations (especially template-related ones), so when a diagnostic makes no sense, switching to Clang and recompiling gives a second angle on the problem.

```bash
# Compile the same code with both compilers and compare the diagnostics
g++ -std=c++20 -Wall -Wextra main.cpp -o main_gcc
clang++ -std=c++20 -Wall -Wextra main.cpp -o main_clang
```

We strongly recommend building this habit. For the same compile error, GCC may spew a full screen of template instantiation backtraces, while Clang sometimes pinpoints the problem far more concisely. And it goes the other way too—some cases GCC explains more clearly. Cross-validating with two compilers saves a remarkable amount of time.

MSVC gets less use, but if the project needs to be cross-platform, occasionally compiling once with MSVC on Windows is absolutely necessary. Different compilers occasionally read the standard in subtly different ways, and finding that out early beats finding out after shipping.

---

# Editors and Build Systems: From "Good Enough to Type In" to the Pitfalls of Modules

## Editors: Please, Just Help Me Understand This Code

On editor choice, many people really do take the long way around. When first learning C++, they use VS Code with a bare-bones C/C++ plugin: completion takes forever to pop up, and diagnostics are forever that red squiggle that never speaks human. At the time it is easy to conclude, "I guess this is just what C++ development is like—the editor cannot help you much." Then they see CLion's code completion, refactoring, and real-time static analysis, and realize: it was never that C++ was incapable—it was the tools.

But we do not want an editor holy war here. Just one thing to say: **never mix spaces and tabs**. We once took over a project whose files interleaved spaces and tabs; indentation looked perfectly fine in the editor, but the moment it hit CI the formatting exploded, and the reported error positions no longer matched the actual code. Since then, every project of ours ships a `.clang-format`, standardizes on spaces, and gives nobody any room to mix.

As for the editor ecosystem, we are actually at an interesting point right now. The Vim/Neovim crowd in the terminal can get remarkably close to IDE-level experience through clangd + LSP—completion, go-to-definition, hover docs, all there. But as a personal choice, CLion works out of the box, with native-grade CMake integration: create a project, write the CMakeLists.txt, click Run, and it goes. No spending two days configuring an editor. Time should go into understanding C++, not into configuring the editor.

Lately, though, we keep running into a scenario where no editor can help. We write a stretch of fairly intricate logic, register callbacks with several lambdas, and it all feels crystal clear as we write it—then three days later we come back and have no idea what that code is doing. We have even pasted the code into CLion's built-in AI assistant and asked it to explain, and after reading the explanation we still only half understand. What does that tell us? That tools can help you write code and help you find bugs, but they cannot **think** for you. Readability is ultimately guaranteed by how you design the layers of abstraction—and that is a pit we have stepped in far too many times.

## Build Systems: You Thought CMake Was the Hard Part, Until You Met Modules

If the editor is the "experience of writing code," the build system is the "experience of making code run"—and that experience in C++, how shall we put it, regularly makes you want to smash the keyboard.

We used to think CMake was torment enough. The way `target_link_libraries` takes its arguments, which of `PUBLIC`, `PRIVATE`, or `INTERFACE` to use, how to track down `find_package` failures when it cannot find the package—getting reasonably fluent in all of that took the better part of a year. But however hard CMake gets, it is at least something you can learn your way into; the documentation reads like hieroglyphics, but documentation exists.

Then we tried C++20 Modules. When we first heard about Modules we were thrilled—finally, an escape from the compile-speed pain of header inclusion. Then we actually tried it. First, CMake's Modules support in the early versions was very rough: you had to manually spell out how each `.cppm` file becomes a module interface unit or a module implementation unit, and the module file formats differ across compilers—GCC uses `.gcm`<RefLink :id="9" preview="GCC module cache (.gcm)" />, Clang uses `.pcm`<RefLink :id="10" preview="Clang precompiled module (.pcm)" />, and MSVC has yet another scheme. Then come the circular-dependency problems; in the classic header era you could break a cycle with a forward declaration, but in the Modules world that approach does not carry over intact. That pit held us for three days, and the way out turned out to be discovering that our understanding of "module partitions" was simply wrong.

Below is the minimal runnable example we eventually wrestled out. The example itself is not complicated, but getting it working consumed an entire weekend. The global module fragment introduced by `module;` is where traditional headers go<RefLink :id="11" preview="C++20 global module fragment" />:

```cpp
// math_utils.cppm (module interface unit)
module;
#include <cmath>  // traditional headers go in the global module fragment, before the module declaration
export module math_utils;  // declare the module name

export double compute_sqrt(double x) {
    return std::sqrt(x);
}

export namespace stats {
    double mean(const double* data, size_t count) {
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += data[i];
        }
        return sum / count;
    }
}
```

```cpp
// main.cpp (consumer)
import math_utils;  // not #include, but import
#include <iostream>

int main() {
    std::cout << "sqrt(16) = " << compute_sqrt(16.0) << "\n";
    double data[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::cout << "mean = " << stats::mean(data, 5) << "\n";
    return 0;
}
```

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(module_test CXX)

# Must be enabled explicitly, and behavior differs across compilers
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(module_test main.cpp math_utils.cppm)
target_compile_features(module_test PRIVATE cxx_std_20)
```

See—the code itself is actually quite intuitive: `export` marks what is visible to the outside, `import` replaces `#include`, and conceptually it is much cleaner than headers. But to get those few lines running, you need CMake 3.28 or newer, a compiler with solid C++20 modules support, and a CMakeLists.txt with nothing misconfigured. Our first attempt was on CMake 3.25, which flat-out errored saying it could not find the module; we were stuck for two hours before realizing it was a version problem.

There is also an easily missed restriction: CMake 3.28's C++20 modules support covers only the Ninja generator and Visual Studio 2022 and above<RefLink :id="12" preview="CMake 3.28 modules supported-generator limitation" />; the traditional Makefile generator does not work yet. A fairly well-hidden pit—once stepped in, never forgotten.

And that is still the simplest case—one module, no partitions, no dependencies on other modules. Once the project scales up and modules import each other, deriving the build order becomes a nightmare. After talking with quite a few people, we found that everyone has face-planted on Modules build configuration at some point; this is not an isolated case.

---

# Designing for Humans: Project Design Ground Rules

When the talk brought up the idea of "designing for humans," a vague intuition many people held suddenly gained a clear frame.

There was a longstanding misconception that a C++ project's caliber shows in how flashy its template metaprogramming is, or how elaborate its build system is. After being brainwashed by assorted "modern C++ best practices," we come to believe that a proper project deserves a full suite of sophisticated CMake scripts. And the result? We built a few projects like that, felt great at the time, then came back a month later to change some code and found it would not even compile anymore—some dependency had bumped a version, its interface changed, and somewhere in that elaborate script sat a hard-coded version number. Stuck for ages; in the end we deleted the entire build directory and started over, burning another two hours. That is doing your own work a disservice.

The talk made a very key point: if your project is painful to build—if it asks people to install four hundred global packages that then turn out incompatible with their machine—you are shutting potential contributors out. Many people know the feeling: you want to send a PR to a fairly well-known C++ library fixing an obvious issue, but the README reads like hieroglyphics, the dependency list runs two pages, and it demands this exact version of Boost plus that exact version of LLVM. A whole evening of failing to get it to build, and the next day you quietly close the PR page and never go back. It is not that you do not want to contribute—it is that your patience was drained dry.

So when starting a project, hold one line without compromise: a person who has never seen the project before should get from git clone to a running hello world in no more than five minutes. We tested this idea on a small tool we have been writing lately, and the results were surprisingly good.

First, the directory layout, deliberately kept very flat:

```text
my_tool/
├── CMakeLists.txt
├── src/
│   └── main.cpp
├── include/
│   └── my_tool.hpp
└── README.md
```

No submodules, no complicated directory nesting. The CMakeLists.txt is also written to be as plain as possible:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The core is just these few lines: find dependencies, add the executable, link
find_package(fmt REQUIRED)

add_executable(my_tool src/main.cpp)
target_include_directories(my_tool PRIVATE include)
target_link_libraries(my_tool PRIVATE fmt::fmt)
```

The README.md was rewritten too—no more of that "feature list plus a pile of badges" styling; it just tells you directly how to get it running:

```markdown
# my_tool

A small tool that does XXX.

## Build

Prerequisites: you need a compiler with C++20 support, and the fmt library.

Ubuntu/Debian:
    sudo apt install libfmt-dev g++

macOS:
    brew install fmt

Then:
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)

The build artifact lands in build/my_tool.

## Troubleshooting

- If you are on GCC 11 or older, you may hit the XXX issue; upgrading to GCC 12 fixes it
- fmt needs to be >= 9.0; older versions fail with the XXX error
```

Notice that final "Troubleshooting" section—it was added after we stepped in those pits ourselves. We used to think writing this kind of thing looked "unprofessional"; now we think it is the most professional part of all. You are saving time for the next person who arrives, and saving people time is the greatest kindness.

We handed this project to two colleagues—one mainly writes Python, the other mainly writes Java—and both had it running within three minutes. The Python colleague even said, "This is simpler to set up than a lot of Python projects." A C++ project being praised for "simple setup"—there was a time when that was unthinkable.

The talk also raised a remarkably forward-looking point: if you make your project easy to enter and exit, you are helping not only humans but AI agents too. We have genuinely felt this lately. When using Cursor to assist with coding, we noticed that if a project has clear structure, few dependencies, and a simple build, the AI can understand more of the project's context and its suggestions are reliable. Conversely, if the project is full of nested custom compiler flags and implicit macro definitions, the AI keeps offering suggestions that "look right but do not actually run," because it never understood what was really happening inside that complicated build environment.

Template errors give humans headaches, and they give AI headaches too—feed it a two-hundred-line template instantiation error stack and the reply is usually generic boilerplate. But if the project itself is clean and highly modularized, error messages come out far shorter, and both AI (and humans) locate problems much faster. So "designing for humans" and "designing for AI" actually converge on this point: both come down to reducing cognitive load.

Looking back, the principle is simple. We write code, in the end, for people to read and for people to use. The compiler only cares whether the syntax is correct; people care about "can I quickly understand what this project does, and can I fix my bit and leave." Making complicated things simple is the real skill.

And with that, it finally clicked—in the process of assembling a C++ program, those tools, those libraries, those build systems are all parts, but the person holding those parts and doing the assembling matters most. Ignore that, and even the most precisely machined parts are just a pile of scrap metal.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Kitware"
    title="CMake: Cross-Platform Build System"
    publisher="Kitware Inc."
    :year="2000"
    chapter="de facto standard C++ build system; FetchContent, find_package"
    url="https://cmake.org/"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::optional"
    publisher="cppreference.com"
    :year="2017"
    chapter="C++17 standard library optional-value wrapper"
    url="https://en.cppreference.com/cpp/utility/optional"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Formatting library (std::format)"
    publisher="cppreference.com"
    :year="2020"
    chapter="C++20 formatting library, based on Python-style format strings"
    url="https://en.cppreference.com/cpp/utility/format"
  />
  <ReferenceItem
    :id="4"
    author="Google"
    title="Abseil C++ Common Libraries"
    publisher="Google LLC"
    :year="2017"
    chapter="Google's open-source C++ common libraries, including absl::StatusOr, absl::Span, absl::string_view, and more"
    url="https://abseil.io/"
  />
  <ReferenceItem
    :id="5"
    author="Edison Design Group"
    title="EDG C++ Front End"
    publisher="Edison Design Group"
    :year="1994"
    chapter="commercial C/C++ language front end, widely used in compilers and static analysis tools"
    url="https://www.edg.com/"
  />
  <ReferenceItem
    :id="6"
    author="Bjarne Stroustrup"
    title="Cfront — The Original C++ Compiler"
    publisher="AT&T Bell Labs"
    :year="1983"
    chapter="the earliest C++ compiler, which translated C++ source into C code and then compiled it with a C compiler"
    url="https://en.wikipedia.org/wiki/Cfront"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="constexpr specifier (since C++11)"
    publisher="cppreference.com"
    :year="2011"
    chapter="constexpr functions are implicitly inline, allowing definitions in headers without violating the ODR"
    url="https://en.cppreference.com/cpp/language/constexpr"
  />
  <ReferenceItem
    :id="8"
    author="cppreference.com"
    title="inline specifier"
    publisher="cppreference.com"
    :year="2011"
    chapter="the core semantics of inline: the same function may be defined in multiple translation units without violating the ODR"
    url="https://en.cppreference.com/cpp/language/inline"
  />
  <ReferenceItem
    :id="9"
    author="Free Software Foundation"
    title="C++ Module Mapper (GCC)"
    publisher="GNU Project"
    :year="2021"
    chapter="GCC's module cache uses the .gcm format, stored in the gcm.cache directory"
    url="https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Module-Mapper.html"
  />
  <ReferenceItem
    :id="10"
    author="LLVM Project"
    title="Standard C++ Modules — Clang Documentation"
    publisher="LLVM Foundation"
    :year="2021"
    chapter="Clang stores module compilation artifacts in the .pcm (Precompiled Module) format"
    url="https://clang.llvm.org/docs/StandardCPlusPlusModules.html"
  />
  <ReferenceItem
    :id="11"
    author="cppreference.com"
    title="Modules (since C++20)"
    publisher="cppreference.com"
    :year="2020"
    chapter="the C++20 modules system: module declarations, the global module fragment, export and import syntax"
    url="https://en.cppreference.com/cpp/language/modules"
  />
  <ReferenceItem
    :id="12"
    author="Kitware"
    title="CMake 3.28 Release Notes"
    publisher="Kitware Inc."
    :year="2023"
    chapter="C++20 named modules support, limited to the Ninja and Visual Studio (VS 2022+) generators"
    url="https://cmake.org/cmake/help/latest/release/3.28.html"
  />
</ReferenceCard>

---

## Further Reading

- The heart of a toolchain is its compiler options. For a systematic tour of common GCC/Clang compiler flags and the trade-offs between them, see [Volume 7: Guide to Common Compiler Options](../../../../vol7-engineering/02-compiler-options.md).
