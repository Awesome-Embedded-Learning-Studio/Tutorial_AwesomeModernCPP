---
title: "Dependencies and the C++ standard — modern ways to write find_package and cxx_std_NN"
description: "A thorough look at the three ways to set the C++ standard and why hand-stuffing flags is an anti-pattern, how find_package brings a third-party library's usage requirements over through imported targets, and how to troubleshoot when a package can't be found"
chapter: 7
order: 3
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
  - "The target mental model — treat a target as an object, PUBLIC/PRIVATE/INTERFACE are usage requirements"
related:
  - CMakePresets.json — from the old cmake -D way to reproducible --preset builds
  - "Cross-compilation and a Simple Guide to CMake"
translation:
  source: documents/vol7-engineering/ch00-cmake-fundamentals/03-find-package-and-cxx-standard.md
  source_hash: 12c73b35fc51149e761d4c673c692a1cfa917ca2833fa46952c9d622a5785762
  translated_at: '2026-09-26T04:45:25+00:00'
  engine: anthropic
  token_count: 10200
---

# Dependencies and the C++ standard — modern ways to write find_package and cxx_std_NN

In the previous article we worked targets and usage requirements all the way through, with hands-on tests of how the PUBLIC/PRIVATE/INTERFACE three states propagate along the link graph. This one picks up two concrete questions you trip over constantly in real projects: how do you tell CMake you want C++20, and how do you link a third-party library in. Search the web and both questions have answers everywhere, but the old-style recipes still circulate in tons of tutorials, and copying them plants landmines. We will run every way of setting the standard and see clearly why some of these recipes belong in the wastebasket.

## Three ways to set the C++ standard — which one is right

For setting the C++ standard, you can watch three styles coexist in the CMake world. Let's take them one at a time — code on the table first, then the why.

The first, attached to a target:

```cmake
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_20)
```

The second, a directory-level variable — this is the one we used to get the project running back in the getting-started volume [getting-started/04](/getting-started/04-multi-file-cmake):

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

The third, stuffing `-std=c++20` straight into the compile flags:

```cmake
string(APPEND CMAKE_CXX_FLAGS " -std=c++20")   # anti-pattern, don't copy
```

CMake officially recognizes the first two; the third is an anti-pattern. Let measured runs make the case below.

### `target_compile_features` carries "minimum requirement" semantics

Translated into plain words, `target_compile_features(app PRIVATE cxx_std_20)` says: when the app target compiles, the C++ standard must be no lower than C++20. Note the wording — "no lower than", not "exactly equal to".

This semantics matters. Once CMake has the requirement, it compares it against the compiler's default standard and, based on the result, decides whether to add a `-std` flag to the compile command. We ran the test with GCC 16.1.1, whose default standard is `gnu++20` (running `g++ -dM -E -x c++ /dev/null | grep __cplusplus` shows `202002L`, which corresponds to C++20). The following `CMakeLists.txt` creates three targets requiring 17, 20, and 23 respectively:

```cmake
cmake_minimum_required(VERSION 3.20)
project(feat_test LANGUAGES CXX)

add_executable(app_cxx17 main.cpp)
target_compile_features(app_cxx17 PRIVATE cxx_std_17)

add_executable(app_cxx20 main.cpp)
target_compile_features(app_cxx20 PRIVATE cxx_std_20)

add_executable(app_cxx23 main.cpp)
target_compile_features(app_cxx23 PRIVATE cxx_std_23)
```

Using Make as the generator with `CMAKE_VERBOSE_MAKEFILE` on, we can watch the command each target actually sends to `g++`:

```text
$ cmake -S . -B build -G "Unix Makefiles" -DCMAKE_VERBOSE_MAKEFILE=ON > /dev/null
$ cmake --build build --target app_cxx17 2>&1 | grep "/c++"
/usr/sbin/c++    -MD -MT ... -c .../main.cpp
$ cmake --build build --target app_cxx20 2>&1 | grep "/c++"
/usr/sbin/c++    -MD -MT ... -c .../main.cpp
$ cmake --build build --target app_cxx23 2>&1 | grep "/c++"
/usr/sbin/c++   -std=gnu++23 -MD -MT ... -c .../main.cpp
```

Read it word by word. The target asking for 17 has no `-std` in its compile command: the compiler default is already 20, higher than 17, so CMake judges the requirement satisfied and adds no flag. The one asking for 20 doesn't either: the default is exactly 20, right on target. Only the one asking for 23 grows a `-std=gnu++23`: the default of 20 isn't enough, so CMake proactively raises it to 23.

That is the beauty of "minimum requirement" semantics. When you write `cxx_std_20`, you are declaring "this code uses C++20 features; below 20 it will not compile", and CMake adds flags only as needed — it will never secretly downgrade a default of 20 to 17. Move to an older compiler whose default is `gnu++17` (GCC 11, say), and with the very same `CMakeLists.txt` CMake automatically adds `-std=gnu++20`. One configuration, and every compiler version gets the correct standard.

::: details What that gnu++ is, and how to turn it off
`gnu++20` is GCC's "C++20 plus GNU extensions" dialect; the pure-standard spelling is `c++20`. The difference: the former allows GCC-only extras such as `typeof` and zero-length arrays, which makes the code less portable. CMake defaults to `gnu++NN` for the sake of old codebases; you can set `CXX_EXTENSIONS OFF` on the target to push it back to pure `c++NN`:

```cmake
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_23)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)
```

Verified in practice: with the same `cxx_std_23` requirement, turning `CXX_EXTENSIONS OFF` on changes the compile command from `-std=gnu++23` to `-std=c++23`:

```text
$ cmake --build build --target app 2>&1 | grep "/c++"
/usr/sbin/c++   -std=c++23 -MD -MT ... -c .../main.cpp
```

For new projects we recommend OFF by default — cross-compiler behavior becomes more predictable.
:::

`cxx_std_NN` can also go out as PUBLIC, reusing the usage-requirement propagation mechanism from the previous article. A library that itself requires C++20 passes that requirement down automatically to whoever links it:

```cmake
target_compile_features(mylib PUBLIC cxx_std_20)
```

When a downstream project links `mylib`, CMake sees `cxx_std_20` sitting in `INTERFACE_COMPILE_FEATURES` and automatically raises the downstream standard one notch too. This is the target-level style's biggest advantage over the directory-level one: the standard requirement travels with the target and propagates along the dependency graph, so you never have to write `set(CMAKE_CXX_STANDARD 20)` again in every downstream project.

### The directory-level style `set(CMAKE_CXX_STANDARD)`: workable, but with a ceiling

We used the second style in the getting-started volume; it looks like this:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

These three lines are scoped to "all targets in the current `CMakeLists.txt` and its subdirectories" — in essence they set the default value of the `CXX_STANDARD` property for every target under the directory. `CMAKE_CXX_STANDARD_REQUIRED ON` is mandatory: it tells CMake "error out if the compiler can't reach this standard". Without it, when the compiler is too old, CMake silently downgrades and compiles anyway — it compiles, but behaves wrongly, and you debug for half a day before tracing the root cause here.

::: warning Omitting `CMAKE_CXX_STANDARD_REQUIRED ON` silently downgrades
By default `CMAKE_CXX_STANDARD_REQUIRED` is `OFF`, which means "even if the compiler doesn't support this standard, try to compile anyway". The result: you write `set(CMAKE_CXX_STANDARD 20)`, the compiler tops out at 17, and CMake raises no error — it quietly keeps compiling as 17. You used C++20 `concept`s or template lambdas; the compile fails eventually, but the error message doesn't point at "the standard was downgraded", it points at a specific syntax line, and you go around a long loop before finding the root cause. So `CMAKE_CXX_STANDARD` and `CMAKE_CXX_STANDARD_REQUIRED ON` must always be written as a pair.
:::

This style is still acceptable today, because in the vast majority of projects the C++ standard is one value, uniform project-wide. But it falls short of the target-level style in two places. First, it doesn't propagate with targets: when someone downstream links your library, the standard requirement doesn't travel over automatically. Second, its scope is directory-level — at heart it's a global setting, just like the `include_directories()` from the previous article, and it stops being precise once the project grows complex.

Migration advice: for new projects, prefer `target_compile_features(mylib PUBLIC cxx_std_NN)` and make the standard a target usage requirement too; old projects can keep `set(CMAKE_CXX_STANDARD)` without breaking anything and switch at the next refactor.

### Hand-stuffing `-std=c++20`: an anti-pattern, don't write this

The third style looks the most "direct", and it's everywhere in old tutorials online:

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
# or
string(APPEND CMAKE_CXX_FLAGS " -std=c++20")
add_executable(app main.cpp)
```

Tested for real, it does put `-std=c++20` into the compile command:

```text
$ cmake -S . -B build -G "Unix Makefiles" -DCMAKE_VERBOSE_MAKEFILE=ON > /dev/null
$ cmake --build build --target app 2>&1 | grep "/c++"
/usr/sbin/c++   -std=c++20 -MD -MT ... -c .../main.cpp
```

Looks harmless — all the problems hide behind it. First, this line bypasses CMake's standard management. CMake internally maintains a table of "which standards the compiler knows, and which flag each standard maps to", and both `target_compile_features` and `set(CMAKE_CXX_STANDARD)` go through that table. If you hand-stuff `-std=c++20`, CMake doesn't know you set a standard, so the `CMAKE_CXX_STANDARD` variable stays empty; downstream code reading it to make decisions gets an empty value, and the whole chain of standard propagation in the dependency graph is severed.

Second, it isn't consistent across platforms. GCC and Clang use `-std=c++20`, MSVC uses `/std:c++20`; the flags hardcode the GCC spelling, and the moment you move to an MSVC project this recipe fails to compile. CMake's standard management smooths the difference over for you — write `cxx_std_20` and CMake picks the platform's flag itself.

Finally, it is completely disconnected from mechanisms like `CXX_EXTENSIONS` and `CMAKE_CXX_STANDARD_REQUIRED` — bypassing the whole abstraction and hand-rolling your own. The moment a single line like `set(CMAKE_CXX_FLAGS ... -std=...)` shows up in a `CMakeLists.txt`, that file is flagged as still thinking in old-style CMake.

Migration rule: delete every place that hand-stuffs `-std=` and replace it with the first or second style above.

## find_package: how to link a third-party library

With the C++ standard settled, on to third-party libraries. In a real project, a library like `fmt` isn't something we write by hand — it comes in from the system or a package manager, and the command CMake gives you for that is `find_package`.

The modern style is two lines and done:

```cmake
find_package(fmt REQUIRED)
target_link_libraries(app PRIVATE fmt::fmt)
```

What `find_package(fmt REQUIRED)` does is search a handful of standard locations (`<prefix>/lib/cmake/fmt/` under `CMAKE_PREFIX_PATH`, and so on) for an `fmt-config.cmake` (also called a package configuration file) and execute it once found. That config file is installed by fmt itself; it knows where fmt's headers are, where the library files are, and which compile options linking needs. After it runs, a target named `fmt::fmt` appears in your project out of thin air.

This `fmt::fmt` is an **imported target**. An imported target differs from the ordinary targets we discussed in the previous article: it isn't built inside your project — someone else built it, packed it into the config file, and `find_package` carried it in. It carries the same usage-requirement properties, such as `INTERFACE_INCLUDE_DIRECTORIES`, `INTERFACE_COMPILE_DEFINITIONS`, and `IMPORTED_LOCATION`. The moment you `target_link_libraries(app PRIVATE fmt::fmt)`, those properties flow onto `app` automatically, just like PUBLIC did last time.

Let's crack open a real fmt::fmt. The local machine has fmt 12.2.0 installed, its config file sits at `/usr/lib/cmake/fmt/fmt-config.cmake`, and the key lines inside that create `fmt::fmt` (from `fmt-targets.cmake`) look like this:

```cmake
add_library(fmt::fmt SHARED IMPORTED)
set_target_properties(fmt::fmt PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"
  ...
)
```

`SHARED IMPORTED` tells CMake this is an imported target for a dynamic library. `INTERFACE_INCLUDE_DIRECTORIES` is its public header path. The following `CMakeLists.txt` finds fmt and then prints out a few of its properties:

```cmake
find_package(fmt REQUIRED)
foreach(prop TYPE INTERFACE_INCLUDE_DIRECTORIES INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_FEATURES)
    get_target_property(v fmt::fmt ${prop})
    message(STATUS "fmt::fmt.${prop} = ${v}")
endforeach()
```

One run of configure:

```text
$ cmake -S . -B build
-- fmt::fmt.TYPE = SHARED_LIBRARY
-- fmt::fmt.INTERFACE_INCLUDE_DIRECTORIES = /usr/include
-- fmt::fmt.INTERFACE_COMPILE_DEFINITIONS = FMT_SHARED
-- fmt::fmt.INTERFACE_COMPILE_FEATURES = cxx_std_11
```

Read them entry by entry. `TYPE` is SHARED_LIBRARY — a dynamic library. `INTERFACE_INCLUDE_DIRECTORIES` is `/usr/include`, the source of the path downstream uses to include `<fmt/core.h>`. `INTERFACE_COMPILE_DEFINITIONS` is `FMT_SHARED`, a key signal: when fmt is built as a dynamic library, whoever links it downstream must define `FMT_SHARED` for the symbols to import correctly. `INTERFACE_COMPILE_FEATURES` is `cxx_std_11` — fmt itself declares it needs at least C++11.

You link with the single line `target_link_libraries(app PRIVATE fmt::fmt)`, and those four properties automatically become part of `app`'s compile environment. Here is the compile command `app` actually receives:

```text
$ cmake -S . -B build -G Ninja > /dev/null && cmake --build build -v 2>&1 | grep "/c++"
[1/2] /usr/sbin/c++ -DFMT_SHARED   -MD -MT ... -c .../main.cpp
```

Note the `-DFMT_SHARED` that appears out of nowhere. Nowhere in `app`'s `CMakeLists.txt` was that line written; it comes from `fmt::fmt`'s `INTERFACE_COMPILE_DEFINITIONS`. `/usr/include` is a system default path, so it doesn't show up explicitly in the command — but install fmt to a nonstandard prefix (`/opt/fmt`, say), and the `-I/opt/fmt/include` line pops up automatically. The link stage works the same way; look at the actual link command:

```text
[2/2] : && /usr/sbin/c++ ... CMakeFiles/app.dir/main.cpp.o -o app  /usr/lib/libfmt.so.12.2.0  && :
```

`/usr/lib/libfmt.so.12.2.0` is the real library file path that `fmt::fmt`'s `IMPORTED_LOCATION` resolves to. CMake does the whole dirty job for you — finding headers, passing compile definitions, locating library files — and all you write is one name, `fmt::fmt`.

That is the imported target's fundamental advantage over the old style: it packages the library's usage requirements into a single object. Link once and every configuration that should come along arrives automatically; even if the library upgrades and changes paths, you don't change a line of code.

### The old style: the `${fmt_INCLUDE_DIRS}` variable flavor

Another style you see all over old tutorials online looks like this:

```cmake
find_package(fmt REQUIRED)
include_directories(${fmt_INCLUDE_DIRS})                      # anti-pattern
add_executable(app main.cpp)
target_link_libraries(app ${fmt_LIBRARIES})                   # anti-pattern
```

`include_directories(${fmt_INCLUDE_DIRS})` is the directory-level global command from the previous article, contaminating every target under the current directory. The `${fmt_LIBRARIES}` variable style relies on the config file writing the library list into a variable that you read out by hand and pass to `target_link_libraries`. The problem: this style propagates no usage requirements at all. `fmt_LIBRARIES` is just a list of library names — no `-DFMT_SHARED`, no `INTERFACE_INCLUDE_DIRECTORIES`, no `cxx_std_11`; drop one of those and it either fails to compile or behaves wrongly.

Worse, these variable names follow no unified convention. fmt may use `fmt_LIBRARIES`, OpenCV may use `OpenCV_LIBS`, Boost may use `Boost_LIBRARIES` — every library you adopt means digging into its config file to see which variables it provides. Imported targets, by contrast, uniformly live in a `Name::Name` namespace: find the `::`-bearing name in the documentation, link once, done.

Migration rule: delete every `include_directories(${X_INCLUDE_DIRS})`, and change every `target_link_libraries(app ${X_LIBRARIES})` into `target_link_libraries(app PRIVATE X::X)`. The precondition is that the library's config file provides an imported target — all mainstream libraries today (fmt, spdlog, Catch2, nlohmann_json, and so on) do.

::: warning What if the library provides no imported target
A few old libraries — or hand-written config files — may provide only `${X_INCLUDE_DIRS}` variables and no `X::X` imported target. In that case you have two options. One: build an INTERFACE library yourself as a wrapper:

```cmake
find_package(OldLib REQUIRED)
add_library(OldLib::OldLib ALIAS OldLib::OldLib)   # no good: OldLib is not a target
# correct approach: create an interface target that wraps the variables
add_library(oldlib_wrapper INTERFACE)
target_include_directories(oldlib_wrapper INTERFACE ${OldLib_INCLUDE_DIRS})
target_link_libraries(oldlib_wrapper INTERFACE ${OldLib_LIBRARIES})
target_link_libraries(app PRIVATE oldlib_wrapper)
```

Downstream then uniformly links `oldlib_wrapper`, and configuration propagates outward from this single place. Two: talk the library's author into updating the config file — or simply switch libraries.
:::

## What to do when the package isn't found

Troubleshooting a `find_package` error follows a fixed playbook. First, let's see what a real error looks like. The following `CMakeLists.txt` looks for a library that doesn't exist at all:

```cmake
find_package(NonExistentPkg 9.9.9 REQUIRED)
```

configure dies outright, and the error CMake reports is:

```text
CMake Error at CMakeLists.txt:4 (find_package):
  By not providing "FindNonExistentPkg.cmake" in CMAKE_MODULE_PATH this
  project has asked CMake to find a package configuration file provided by
  "NonExistentPkg", but CMake did not find one.

  Could not find a package configuration file provided by "NonExistentPkg"
  (requested version 9.9.9) with any of the following names:

    NonExistentPkg.cps
    nonexistentpkg.cps
    NonExistentPkgConfig.cmake
    nonexistentpkg-config.cmake

  Add the installation prefix of "NonExistentPkg" to CMAKE_PREFIX_PATH or set
  "NonExistentPkg_DIR" to a directory containing one of the above files.

-- Configuring incomplete, errors occurred!
```

This error carries a lot of information; let's take it apart. The first paragraph says "you didn't provide `FindNonExistentPkg.cmake` in `CMAKE_MODULE_PATH`" — meaning CMake first searched in "Module mode" through its built-in and your provided `FindX.cmake` files and came up empty. The second paragraph says "the package configuration file provided by `NonExistentPkg` wasn't found either" and lists the file names it looked for, where `.cps` is the new CPS (CMake Package Specification) format introduced in CMake 3.29 and `.cmake` is the classic format. The third paragraph gives the troubleshooting path.

Following the error's hints, the common reasons `find_package` can't find a package are these, ordered by how often they show up:

First, the library simply isn't installed. Most common — start by confirming whether the library is on the system at all. On Linux, ask the package manager (`apt list --installed | grep fmt`, `pacman -Qs fmt`); on Windows, check `vcpkg list`; on macOS, `brew list`. If it's not installed, install it, and while doing so check whether you got the development package with the `-dev` or `-devel` suffix — some distributions sell the runtime library and the headers separately, and with only the runtime installed, `find_package` still won't find it.

Second, it's installed but `CMAKE_PREFIX_PATH` isn't set. The library lives in a nonstandard prefix (you ran `make install` into `/opt/fmt`, or vcpkg installed under `~/vcpkg/installed/x64-linux`), while CMake by default searches only the standard locations such as `/usr` and `/usr/local` — naturally it comes up empty. The fix is adding `-DCMAKE_PREFIX_PATH=/opt/fmt` at configure time, or setting the `CMAKE_PREFIX_PATH` environment variable. The next article, on CMakePresets, will show how to freeze this kind of `-D` into JSON.

Third, the vcpkg / Conan toolchain file wasn't injected. After these two package managers install a library, it lands inside directories they manage themselves (vcpkg: `installed/`; Conan: `~/.conan2/`), not in the system's standard paths. They hand you a toolchain file to pass in at configure time via `-DCMAKE_TOOLCHAIN_FILE=<path>/vcpkg.cmake`, and that toolchain file automatically points `CMAKE_PREFIX_PATH` at the libraries it installed. Forget to attach the toolchain and the installation goes to waste — `find_package` still finds nothing. This is the trap newcomers step on most often.

Fourth, the library is installed but ships no config file. Say the system has an old fmt 5.x — back then fmt didn't provide `fmt-config.cmake` yet, only a Module-mode finder like `FindFMT.cmake` (or nothing at all). In that case `find_package(fmt)` fails in Config mode, and your choices are: upgrade the library, write a `FindX.cmake` yourself, or bridge through pkg-config.

::: details The two lookup modes of find_package
By default `find_package(X)` goes through two modes, Module first, then Config.

Module mode looks for `FindX.cmake`, a file name starting with `Find`. These files are written by CMake itself (over a hundred `FindX.cmake` for common libraries ship built in) or provided by you under `CMAKE_MODULE_PATH`. Common in old-style code, because back in the day many libraries shipped no config file of their own and relied on community-maintained Modules as the bridge.

Config mode looks for `X-config.cmake` or `XConfig.cmake` (CMake 3.29+ also looks for `.cps` files), file names starting with the library name. These files are installed by the library's author and shipped together with the library, so they are more accurate than community-maintained Modules. Modern mainstream libraries (fmt, spdlog, Catch2, Boost 1.70+, and so on) all carry their own Config file, so in practice `find_package` mostly goes through Config mode.

CMake defaults to Module before Config; you can force exactly one with `find_package(X CONFIG)` or `find_package(X MODULE)`. For new projects we recommend writing `CONFIG` explicitly — the behavior is clearer, and it also prevents some stale built-in `FindX.cmake` from being found ahead of the library's own config file, which would behave inconsistently.
:::

## vcpkg / Conan, briefly

Where third-party libraries come from — we've been silent on that until now. Libraries from system package managers (apt, pacman, brew) are outdated, inconsistent across platforms, and not necessarily installable on CI, so serious projects generally avoid them. The two mainstream package managers in the C++ ecosystem are vcpkg and Conan: what they do is build the libraries for you, install them into their own directories, and hand you a toolchain file so that CMake's `find_package` can find them.

The key to using them is a single configure argument:

```text
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

Everything vcpkg installs lives under `<vcpkg-root>/installed/`, and its toolchain file automatically points `CMAKE_PREFIX_PATH` there — so `find_package(fmt REQUIRED)` in your project keeps working as usual, no different from a system install. Conan works the same way; the toolchain file it generates is called `conan_toolchain.cmake`. The details of this machinery — how to write manifest files, how to lock versions, how it hooks up with the CMakePresets article coming next — we'll leave for a later package-management deep dive. One thing to remember here: after the library is installed, injecting the toolchain file is the step that lets CMake find it.

## The companion example

This article's example project lives in the repository at `code/examples/vol7/cmake-fundamentals/03-find-package/`, structured like this:

```text
03-find-package/
├── CMakeLists.txt    # target_compile_features + optional find_package section
└── main.cpp          # verifies the standard took effect with a C++20 template lambda
```

The three core lines of `CMakeLists.txt`:

```cmake
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_20)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)
```

Three steps to run it:

```text
$ cmake -S . -B build -G Ninja && cmake --build build && ./build/app
3
ab
```

`main.cpp` uses a template lambda that only exists since C++20 (`[]<typename T>(T a, T b) { return a + b; }`) to prove that `cxx_std_20` really carried the standard requirement into the compile command. To verify the "minimum requirement" semantics yourself, change `cxx_std_20` to `cxx_std_23`, re-run configure, then inspect the compile command with `cmake --build build -v` — you will see CMake automatically added a `-std=c++23`.

## What's next

At this point the three ways of setting the C++ standard and `find_package`'s imported-target machinery have all landed in code. In this article our configure command has already grown into this:

```text
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=/opt/fmt ...
```

Once the `-D`s pile up, trouble starts: a mistyped variable name makes configure sail through with an empty configuration and no error, teammates keep asking each other what to fill in for the vcpkg path, and one changed option in CI turns the PR wall red. The next article covers `CMakePresets.json` — the mechanism CMake 3.19 introduced to freeze all these scattered command-line `-D`s, the generator choice, and the toolchain injection into one JSON file, slimming the command down to a single `cmake --preset debug`.
