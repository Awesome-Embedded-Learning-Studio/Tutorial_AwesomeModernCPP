---
title: "What Is CMake — The Two-Stage Pipeline of a Build System Generator"
description: "Get CMake's role as a build system generator straight: what the configure and build stages each do, and how to pick between the Make, Ninja, and Visual Studio generators"
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
  - "vol1 ch00: 第一个程序"
related:
  - "交叉编译与 CMake"
  - "编译器选项"
---

# What Is CMake — The Two-Stage Pipeline of a Build System Generator

Back in the first-program article of volume one, we dropped CMake's name and had you copy out five lines of `CMakeLists.txt` to get the project running. At the time I left a note saying "we'll use `g++` for now and bring in CMake properly in a later chapter" (03-first-program.md, line 119). That promise has been outstanding for several volumes. This article is here to pay it back.

But today isn't about teaching you to type commands. Anyone can type `cmake -B build`. What we want to nail down is what CMake is actually doing behind the scenes: why there are two steps, "configure" and "build"; how it relates to `g++`; why the same project can produce both a Makefile and `build.ninja`. Get this straight in your head, and later when you study targets, `find_package`, and cross-compilation, it won't feel like you're memorizing incantations.

## CMake Isn't a Compiler — It's a Build System Generator

Let's tear down the most fundamental misunderstanding first.

A lot of people's first reaction to CMake is "it's a compiler" or "it replaces `g++`." Neither. CMake doesn't compile a single line of code itself. What it actually does: read your `CMakeLists.txt`, and based on the current platform and toolchain, generate files for other build systems (Makefile, `build.ninja`, Visual Studio's `.sln`), and then let Make, Ninja, or MSBuild — the "real build tools" — invoke the compiler.

In one line: **CMake generates files that compile code.** It's a layer on top of build systems. The industry calls it a "build system generator," or put another way, a "meta build system."

::: details Where does "meta build system" come from?
An ordinary build system (Make/Ninja) directly describes "which source files to compile, how to link." A meta build system sits one level up: it doesn't describe the build process directly, it describes "what the structure of this project is," and then translates that into files the corresponding build system can read, based on your currently selected toolchain. CMake, Meson, and Bazel all live in this layer.
:::

Why does C++ have this extra layer that Rust and Go don't? It goes back to ISO. The C++ standards committee only governs the language itself (syntax, the standard library) and has never dictated how toolchains are organized or what build files should look like. The result: MSVC on Windows, GCC on Linux, `arm-none-eabi-g++` on embedded, each with its own compiler options and project format. Rust and Go ship as "language + official toolchain (`cargo`/`go`)" and never had this problem.

CMake exists to paper over this legacy: you write one `CMakeLists.txt`, and it spits out a Visual Studio project on Windows, a Makefile on Linux, or Ninja files on a machine that wants speed. Describe the sources once, get the build files appropriate to the platform.

## The Two-Stage Pipeline: configure and build

Once you understand what CMake is, that confusing "why do I type the command twice" question answers itself. CMake's workflow splits naturally into two stages.

The first stage is called **configure**. In this stage CMake reads your `CMakeLists.txt`, finds the compiler, checks whether it runs, what version it is, records the results in `CMakeCache.txt`, and finally writes out the build files. Note: this stage **does not compile a single line of your code**. It's just "putting up the scaffolding."

The second stage is called **build**. This is where the compiler actually gets invoked, compiling each source file into a `.o` and then linking them into an executable or library.

Let's look at real configure output. Below is what I get running `cmake -B build -G Ninja` on a minimal project on my machine (GCC 16.1.1, CMake 4.4.0):

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

Let's go line by line. The first six lines are all CMake "taking stock": identifying the compiler version (`GNU 16.1.1`), probing ABI info, confirming the compiler actually works, and gathering which compile features it supports. This information gets used later. For example, if you wrote `set(CMAKE_CXX_STANDARD 17)` in your `CMakeLists.txt`, CMake needs to know whether the current compiler actually supports C++17, and if not, error out at you immediately. Then `Configuring done` means stock-taking is finished, `Generating done` means the build files have been written to disk, and the last line tells you where they landed.

Notice there's not a single `Building CXX` line in this whole process. That's configure: it sets the stage, it doesn't perform.

Now look at build. `cmake --build build` is the unified entry point, you type it the same way whether the underlying tool is Make or Ninja:

```text
$ cmake --build build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.o
[2/2] Linking CXX executable hello
```

`[1/2]`, `[2/2]` are Ninja's progress markers, meaning "step one of two, step two of two." The first step compiles `main.cpp` into an object file, the second links it into `hello`. This is where `g++` is actually running.

Why split it into two steps at all? The key is that **the performance characteristics are different**. configure is slow — it has to restart the process, re-take stock, regenerate all the build files. But configure only needs to re-run when you've changed `CMakeLists.txt`, added new files, or switched Generator. build is fast — it does incremental compilation, only recompiling files that changed. So in your daily dev loop, configure runs occasionally, build runs countless times.

Let's measure it on the same minimal project to see the cache's effect on configure speed:

```text
$ rm -rf build && time cmake -B build -G Ninja > /dev/null
cmake -B build -G Ninja > /dev/null  0.09s user 0.08s system 93% cpu 0.183 total

$ time cmake -B build -G Ninja
-- Configuring done (0.0s)
-- Generating done (0.0s)
cmake -B build -G Ninja  0.01s user 0.00s system 90% cpu 0.017 total
```

A cold configure takes 0.183 seconds, a cached second configure only 0.017 seconds — ten times faster. This minimal project is too small to really feel it, but in a real project it's normal for the first configure to take a dozen seconds and the second to take a fraction of a second. That's why it's worth pulling configure out as its own stage with a cache. Otherwise every time you compile one line of code you'd have to re-take stock of everything, and nobody could stand that.

## Generator: Make or Ninja

CMake abstracts "which kind of build files to generate" into a concept called a **Generator**. You pick one with the `-G` flag at configure time, and CMake generates the corresponding set of files.

The three most common generators:

Unix Makefiles is the default option. On Linux/macOS, if you don't specify `-G`, this is what you get. It produces a `Makefile` driven by the `make` command. The oldest, most universal option — every Unix system ships `make`. Its downside is speed: `make` is a 1970s design, and its dependency checking and parallel scheduling are not modern.

Ninja is the modern recommendation. It produces `build.ninja`, driven by the `ninja` command. Ninja was designed specifically "to be generated by a meta build system": low startup overhead, aggressive parallel scheduling, fast incremental builds. The cost is that you have to install `ninja` separately (the package is usually just called `ninja` or `ninja-build`).

Visual Studio is what you use for IDE integration on Windows (`-G "Visual Studio 17 2022"`). It produces `.sln` and `.vcxproj` files you can open directly in Visual Studio and F5-debug. If you don't care about the IDE experience, Ninja works fine on Windows too.

The only difference in the command is the `-G` argument. Let's run the same project with both generators and see what each produces:

```text
cmake -B build      -G Ninja            # pick Ninja
cmake -B build-make -G "Unix Makefiles" # pick Make
```

The two configure commands print nearly identical output (both go through that "detect compiler, Configuring done" sequence); the difference is in the generated build files. Let's directly compare what lands in each `build/` directory:

```text
$ ls build/          # Ninja output
build.ninja
cmake_install.cmake
CMakeCache.txt
CMakeFiles
hello

$ ls build-make/     # Make output
cmake_install.cmake
CMakeCache.txt
CMakeFiles
hello
Makefile
```

The Ninja side has an extra `build.ninja`, the Make side has an extra `Makefile`. `CMakeCache.txt`, `CMakeFiles/`, and `cmake_install.cmake` are present in both; they're CMake's own infrastructure.

My advice: default to Ninja for local development. It's not just a little faster, and `build.ninja` is far cleaner than a `Makefile` (cat both files and you'll see). Unless your environment can't install `ninja`, there's no reason to fall back to Make. When we get to cross-compilation and CI later, Ninja is also the more comfortable choice.

## Out-of-source builds: don't pollute the source directory

CMake recommends a build style called **out-of-source build** (keeping the source tree and the build tree separate). The idea: all build artifacts — object files, executables, `CMakeCache.txt`, the generated build files — get dumped into a `build/` subdirectory, and the source directory stays clean.

The `-B` in `cmake -B build` is exactly for this: it tells CMake "put the build tree under `build/`". The directory layout looks like this:

```text
cmake-demo/
├── CMakeLists.txt      # you write this, goes in git
├── main.cpp            # you write this, goes in git
└── build/              # CMake generates this, goes in .gitignore
    ├── CMakeCache.txt
    ├── CMakeFiles/
    ├── build.ninja
    ├── cmake_install.cmake
    └── hello           # the final executable
```

The benefit is direct: in the source directory you won't see a single `.o` file, no `a.out`, no temporary artifacts. Want to wipe and start over? One `rm -rf build/` and the source is untouched. Want to package a release? The source directory is nothing but clean source files, no need to painstakingly pick out what should and shouldn't be archived.

::: details In-source builds work too, but don't
CMake also lets you run `cmake .` directly in the source directory (called an in-source build), which generates a Makefile and a pile of `CMakeFiles/` right there in the current directory. It looks convenient, but once the source directory is polluted, `git status` turns into a wall of red and cleaning it up means hunting down files one by one. Newer CMake versions even restrict this: by default it refuses to configure twice in the same directory, to keep you from messing up the source tree. Get into the `-B build` habit and save yourself the pain later.
:::

Here we need to single out **`CMakeCache.txt`**. It's the key to the speedup in that "cold start vs cached" comparison earlier. On the first configure, CMake stores all its findings in there: the compiler path, the compiler version, the Generator choice, the variables you set via `-D`, the results of various feature probes. On the next configure, CMake reads the cache first and reuses anything that hasn't changed, skipping the re-probing.

Open it up and have a look — it's a key-value format, with the important fields looking like this:

```text
//Path to CXX compiler.
CMAKE_CXX_COMPILER:FILEPATH=/usr/sbin/c++

//Name of CMake project.
CMAKE_PROJECT_NAME:STATIC=hello_cmake

//Name of generator.
CMAKE_GENERATOR:INTERNAL=Ninja
```

Notice the `CMAKE_GENERATOR` line — it remembers the generator you picked. So a second configure doesn't need `-G Ninja` again; CMake knows to keep using Ninja. This is also why sometimes when you want to switch generators, just typing `-G` doesn't work and CMake keeps using the old one: `CMakeCache.txt` has it locked in, and you need `rm -rf build/` to start fresh.

Does `CMakeCache.txt` go in git? Absolutely not. It's tightly bound to the machine environment (compiler paths, absolute paths are all in there), and committing it guarantees a conflict on every machine. Put the entire `build/` directory in `.gitignore` and be done with it.

## The minimal project's three pieces

With the principles out of the way, let's land on a minimal project that actually runs. A legal `CMakeLists.txt` needs at least three lines:

```cmake
cmake_minimum_required(VERSION 3.20)
project(hello_cmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello main.cpp)
```

The first line, `cmake_minimum_required(VERSION 3.20)`, declares the minimum CMake version this project requires. Any CMake older than this bails out with an error on seeing this line. Its real job isn't just version checking — it also flips CMake into the "policy compatibility mode" for that version, ensuring that behavior changes in newer CMake don't silently affect old projects. `3.20` is a safe lower bound: released in 2021, installable on mainstream distros.

The second line, `project(hello_cmake LANGUAGES CXX)`, names the project `hello_cmake` and declares it uses C++ (`CXX`). This `project()` line is what triggers the "detect compiler, probe ABI" stock-taking you saw in the configure output earlier — `LANGUAGES CXX` tells CMake "I need a C++ compiler," and only then does CMake go off hunting for `g++`/`clang++`/`MSVC`.

The third line, `add_executable(hello main.cpp)`, is what actually tells CMake "build an executable called `hello`, with source `main.cpp`". After this line gets baked into `build.ninja`, the build stage turns into `g++ main.cpp -o hello`.

The two lines in the middle, `set(CMAKE_CXX_STANDARD 17)` and `set(CMAKE_CXX_STANDARD_REQUIRED ON)`, set the default C++ standard. The first asks to compile as C++17, the second asks to "error out if the compiler doesn't support it, rather than silently downgrading." We'll come back to these when we cover targets — the more modern way is `target_compile_features()`, but for now this is enough.

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

That last line, `Hello, CMake!`, is the output of running `./build/hello`. The first command puts up the scaffolding, the second actually compiles and links, the third executes. No matter how big the project gets later, the skeleton stays the same.

## Accompanying example

The minimal project from this article can be run directly from the repo's examples directory:

```text
code/examples/vol7/cmake-fundamentals/01-what-is-cmake/
├── CMakeLists.txt
└── main.cpp
```

cd into that directory and copy the three commands above to reproduce all the output.

That covers CMake's role, the two-stage pipeline, choosing a generator, and out-of-source builds, and we've gotten the minimal project running. The next article tackles a more practical problem: when the project has more than one `main.cpp`, needs to be split into multiple modules, and needs to reuse third-party libraries, how do you manage "which headers this target uses, which library it links, what compile options it turns on"? That brings up CMake's core mental model — the **target** — and why you shouldn't keep using the global `include_directories()` "imperative" style, and should move to the `target_include_directories()` "object-oriented" style instead.
