---
title: "What is CMake — the two-stage pipeline of a build system generator"
description: "Ground CMake's role as a build system generator: what the configure and build stages of the two-stage pipeline each do, and how to choose among the Make, Ninja, and Visual Studio generators"
chapter: 7
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 18
prerequisites:
  - "Your First C++ Program"
related:
  - "Cross-compilation and a Simple Guide to CMake"
  - "Guide to Common Compiler Options"
translation:
  source: documents/vol7-engineering/ch00-cmake-fundamentals/01-what-is-cmake.md
  source_hash: 976148e98f243fc4c3087923eafb4c298a68e0b1a361319725f208d959bdab03
  translated_at: '2026-09-26T04:47:36+00:00'
  engine: anthropic
  token_count: 3300
---

# What is CMake — the two-stage pipeline of a build system generator

Back in the first-program article of volume one, we dropped CMake's name and had you copy out five lines of `CMakeLists.txt` to get the project building. At the time we left a note — "we'll use `g++` for now and bring in CMake properly in a later chapter" (03-first-program.md, line 119). That debt has been outstanding for several volumes now; this article is here to pay it back.

But today is not about teaching you how to type commands — anyone can type `cmake -B build`. What we want to nail down is what CMake actually does behind the scenes: why there are two steps, "configure" and "generate"; what its relationship to `g++` really is; why the same project can produce both a Makefile and `build.ninja`. Get all this straight, and when you later study targets, `find_package`, and cross-compilation, it won't feel like memorizing incantations.

## CMake is not a compiler — it's a build system generator

This section tears down the most fundamental misunderstanding first.

Many people's first reaction on meeting CMake is "it's a compiler" or "it replaces `g++`". Neither. CMake doesn't compile a single line of code itself. What it actually does is read the `CMakeLists.txt` you wrote and, based on the current platform and toolchain, generate files for other build systems — Makefile, `build.ninja`, Visual Studio's `.sln` — and then let Make, Ninja, or MSBuild, the "real build tools", go invoke the compiler.

In one sentence: **CMake generates the files that compile code.** It is a layer on top of build systems; the industry calls it a "build system generator", or put another way, a "meta build system".

::: details Where does the term "meta build system" come from?
An ordinary build system (Make/Ninja) directly describes "which source files to compile, how to link them". A meta build system sits one level up: it doesn't describe the build process directly, it describes "what this project's structure is", and then translates that into files the corresponding build system can read, based on the toolchain you currently selected. CMake, Meson, and Bazel all live at this layer.
:::

Why does C++ carry this extra layer that Rust and Go don't? The root is ISO. The C++ standards committee governs only the standard language itself (syntax, the standard library) and has never had anything to say about how toolchains are organized or what build files look like. The result: MSVC on Windows, GCC on Linux, `arm-none-eabi-g++` on embedded platforms — each with its own compiler options and project formats. Rust and Go ship as "language + official toolchain (`cargo`/`go`)" in one package and simply never had this problem.

CMake exists to paper over this historical legacy: you write one `CMakeLists.txt`, and it spits out a Visual Studio project on Windows, a Makefile on Linux, or Ninja files on whatever machine wants speed. Describe the sources once; the build files adapt to each environment.

## The two-stage pipeline: configure and build

Once you understand where CMake sits, that confusing "why do I have to type the CMake command twice" question answers itself. CMake's workflow splits naturally into two stages.

The first stage is called **configure**. In this stage CMake reads your `CMakeLists.txt`, detects where the compiler is, whether it runs, what version it is, records the results into `CMakeCache.txt`, and finally generates the build files. Note: this stage **does not compile a single line of your code**. It is just "putting up the scaffolding".

The second stage is called **build**. This is the stage that actually invokes the compiler, compiling each source file into a `.o` and then linking them into an executable or a library.

Let's look at real configure output. Below is what we get running `cmake -B build -G Ninja` on a minimal project on our machine (GCC 16.1.1, CMake 4.4.0):

```text
$ cmake -B build -G Ninja
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-demo/build
```

Line by line. The first six lines are all CMake "taking stock": identifying the compiler version (`GNU 16.1.1`), probing ABI info, confirming the compiler actually works, collecting which compile features it supports. This information gets used later — if, say, you wrote `set(CMAKE_CXX_STANDARD 17)` in your `CMakeLists.txt`, CMake has to know whether the current compiler actually supports C++17, and if not, it errors out at you immediately. Then `Configuring done` means the stock-taking is finished, `Generating done` means the build files have been written to disk, and the last line tells you where they landed.

Notice there isn't a single `Building CXX` line in the whole process. That's configure: it sets the stage, it doesn't perform.

Now build. `cmake --build build` is the unified entry point — you type it the same way whether the underlying tool is Make or Ninja:

```text
$ cmake --build build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.o
[2/2] Linking CXX executable hello
```

`[1/2]` and `[2/2]` are Ninja's progress markers, meaning "step one of two, step two of two". The first step compiles `main.cpp` into an object file; the second links it into `hello`. This is where `g++` is genuinely running.

Why insist on two stages? The key is that **their performance characteristics differ**. configure is slow — it has to restart the process, re-take stock of everything, regenerate all the build files. But configure only needs rerunning when you've changed `CMakeLists.txt`, added new files, or switched Generator. build is fast — it does incremental compilation, recompiling only the files that changed. So in the daily dev loop, configure runs once in a while, build runs countless times.

Let's measure it on the same minimal project and see what the cache does to configure speed:

```text
$ rm -rf build && time cmake -B build -G Ninja > /dev/null
cmake -B build -G Ninja > /dev/null  0.09s user 0.08s system 93% cpu 0.183 total

$ time cmake -B build -G Ninja
-- Configuring done (0.0s)
-- Generating done (0.0s)
cmake -B build -G Ninja  0.01s user 0.00s system 90% cpu 0.017 total
```

A cold configure takes 0.183 seconds; a second configure that hits the cache takes only 0.017 seconds — a tenfold difference. This minimal project is too small to feel it, but in a real project it's normal for the first configure to take a dozen-plus seconds and the second to take a fraction of a second. That's why pulling configure out as its own stage with a cache is worthwhile — otherwise every one-line edit would mean re-surveying everything from scratch, and nobody could stand that.

## Generator: Make or Ninja

CMake abstracts "which kind of build files to generate" into a concept called a **Generator**. You pick one via the `-G` argument at configure time, and CMake generates the corresponding set of files.

The three most commonly used Generators:

Unix Makefiles is the default option — on Linux/macOS, if you don't specify `-G`, this is what you get. It generates a `Makefile`, and the `make` command drives the build. The oldest, most universal choice: every Unix system ships `make`. Its downside is speed — `make` is a 1970s design, and its dependency checking and parallel scheduling are far from modern.

Ninja is the modern recommendation. It generates `build.ninja`, driven by the `ninja` command. Ninja was designed specifically "to be generated by a meta build system": small startup overhead, aggressive parallel scheduling, fast incremental builds. The price is installing `ninja` separately (the package is usually just called `ninja` or `ninja-build`).

Visual Studio is the option for IDE integration on Windows (`-G "Visual Studio 17 2022"`). It generates `.sln` and `.vcxproj` files you can open directly in Visual Studio and debug with F5. If you're not after the IDE experience, Ninja works just as well on Windows.

The only difference in the command is the `-G` argument. Let's run the same project through both Generators and see what each produces:

```text
cmake -B build      -G Ninja            # pick Ninja
cmake -B build-make -G "Unix Makefiles" # pick Make
```

The two configure commands print nearly identical output (both go through that "detect the compiler, Configuring done" sequence); the difference is in the generated build files. Let's compare the artifacts in the two `build/` directories directly:

```text
$ ls build/          # generated by Ninja
build.ninja
cmake_install.cmake
CMakeCache.txt
CMakeFiles
hello

$ ls build-make/     # generated by Make
cmake_install.cmake
CMakeCache.txt
CMakeFiles
hello
Makefile
```

The Ninja side has an extra `build.ninja`; the Make side has an extra `Makefile`. `CMakeCache.txt`, `CMakeFiles/`, and `cmake_install.cmake` are present in both — they're CMake's own infrastructure.

Our recommendation: default to Ninja for local development, always. It's not just a little faster, and `build.ninja` is far more concise than a `Makefile` (`cat` both files and you'll see). Unless your environment can't install `ninja`, there's no reason to fall back to Make. When we get to cross-compilation and CI later, Ninja is also the smoother choice.

## out-of-source builds: don't pollute the source directory

CMake recommends by default a build style called an **out-of-source build** (keeping the source tree and the build tree separate). The idea: all build artifacts — object files, executables, `CMakeCache.txt`, the generated build files — get piled into one `build/` subdirectory, and the source directory stays clean.

That's exactly what the `-B` in `cmake -B build` is for: it tells CMake "put the build tree under `build/`". The directory layout looks like this:

```text
cmake-demo/
├── CMakeLists.txt      # you wrote this; it goes in git
├── main.cpp            # you wrote this; it goes in git
└── build/              # CMake generated this; it goes in .gitignore
    ├── CMakeCache.txt
    ├── CMakeFiles/
    ├── build.ninja
    ├── cmake_install.cmake
    └── hello           # the final executable
```

The benefits of this layout are direct: not a single `.o` file in the source directory, no `a.out`, no scratch artifacts. Want to wipe it and start over? One command, `rm -rf build/`, and the source doesn't move an inch. Want to package a release? The source directory is nothing but clean source files — no agonizing over what should and shouldn't go into the archive.

::: details in-source builds work too, but don't
CMake does allow running `cmake .` directly in the source directory (called an in-source build); it generates a Makefile and a pile of `CMakeFiles/` right there in the current directory. It looks like a time-saver, but once the source directory is polluted, `git status` turns into a wall of red, and cleanup means hunting files down one by one. Newer CMake versions even put a restriction on this: by default they refuse a second configure in the same directory, to keep you from wrecking the source tree. Get into the `-B build` habit now and spare yourself the pain later.
:::

This is the place to talk about **`CMakeCache.txt`** specifically. It's the key to the speedup in that "cold start vs cache" comparison earlier. On the first configure, CMake stores all its stock-taking results in there: the compiler path, the compiler version, the Generator choice, the variables you set via `-D`, the results of various feature probes. On the next configure, CMake reads the cache first and reuses anything unchanged, skipping the re-probing time.

Open it and have a look — it's a key-value format, and the important fields look like this:

```text
//Path to CXX compiler.
CMAKE_CXX_COMPILER:FILEPATH=/usr/sbin/c++

//Name of CMake project.
CMAKE_PROJECT_NAME:STATIC=hello_cmake

//Name of generator.
CMAKE_GENERATOR:INTERNAL=Ninja
```

Note the `CMAKE_GENERATOR` line — it remembers the Generator you picked. So a second configure doesn't need `-G Ninja` again; CMake knows on its own to keep using Ninja. This is also why, sometimes, when you want to switch Generators, typing `-G` alone does nothing and CMake keeps using the old one — `CMakeCache.txt` has it locked in, and you need `rm -rf build/` to clear it and start over.

Does `CMakeCache.txt` go into git? Absolutely not. It is tightly bound to the machine's environment (compiler paths, absolute paths are all in there); commit it and conflicts on every machine are guaranteed. Put the entire `build/` directory into `.gitignore` and be done with it once and for all.

## The minimal project in three lines

With the principles covered, let's land on a minimal project that actually runs. A legal `CMakeLists.txt` needs at least three lines:

```cmake
cmake_minimum_required(VERSION 3.20)
project(hello_cmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello main.cpp)
```

The first line, `cmake_minimum_required(VERSION 3.20)`, declares the minimum CMake version this project requires. Any CMake older than that sees this line and bails out with an error. This line's real role goes beyond a version check — it also flips CMake into the "policy compatibility mode" for that version, ensuring that behavior changes in newer CMake releases don't silently affect older projects. `3.20` is a safe lower bound: released in 2021, installable on mainstream distros.

The second line, `project(hello_cmake LANGUAGES CXX)`, names the project `hello_cmake` and declares it will use C++ (`CXX`). This `project()` line is what triggers the "detect the compiler, probe the ABI" stock-taking you saw in the configure output earlier — `LANGUAGES CXX` tells CMake "I need a C++ compiler", and only then does CMake go searching the whole world for `g++`/`clang++`/`MSVC`.

The third line, `add_executable(hello main.cpp)`, is what actually tells CMake "build an executable named `hello`, with source `main.cpp`". Once this line is baked into `build.ninja`, the build stage boils down to `g++ main.cpp -o hello`.

The two lines in the middle, `set(CMAKE_CXX_STANDARD 17)` and `set(CMAKE_CXX_STANDARD_REQUIRED ON)`, set the default C++ standard. The first asks to compile as C++17; the second asks to "error out if the compiler doesn't support it, rather than downgrading". We'll come back to these two lines when we cover targets — the more modern spelling is `target_compile_features()`, but for now this is good enough.

The accompanying `main.cpp`:

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, CMake!\n";
    return 0;
}
```

Three commands run the whole pipeline:

```text
$ cmake -B build -G Ninja && cmake --build build && ./build/hello
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-demo/build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.o
[2/2] Linking CXX executable hello
Hello, CMake!
```

The last line, `Hello, CMake!`, is what `./build/hello` printed. The first command puts up the scaffolding, the second actually compiles and links, the third executes. However big the project gets later, this pipeline keeps the same skeleton.

## The accompanying example

The minimal project from this article can be run straight from the repo's examples directory:

```text
code/examples/vol7/cmake-fundamentals/01-what-is-cmake/
├── CMakeLists.txt
└── main.cpp
```

cd into that directory, copy the three commands from above, and you can reproduce all of the output.

At this point we've thoroughly covered where CMake sits, the two-stage pipeline, choosing a Generator, and out-of-source builds, and we've gotten the minimal project running. The next article takes on a more practical problem: when the project has more than one `main.cpp`, needs splitting into multiple modules, and needs to reuse third-party libraries, how do you manage "which headers this target uses, which library it links, which compile options it turns on"? That leads to CMake's core mental model — the **target** — and to why you shouldn't keep using the global, "imperative" `include_directories()` style, and should move to the "object-oriented" `target_include_directories()` style instead.
