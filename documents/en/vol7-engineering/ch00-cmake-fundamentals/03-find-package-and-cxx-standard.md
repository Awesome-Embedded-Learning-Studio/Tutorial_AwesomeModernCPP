---
title: "Dependencies and the C++ Standard — Modern Ways to Write find_package and cxx_std_NN"
description: "Work through the three ways to set the C++ standard, why hand-stuffing flags is an anti-pattern, how find_package brings in a third-party library's usage requirements via imported targets, and how to diagnose it when a package is not found"
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
  - "vol7 ch00 02: Target 心智模型——把 target 当对象，PUBLIC/PRIVATE/INTERFACE 是使用需求"
related:
  - "CMakePresets.json——从 cmake -D 老式到 --preset 可复现"
  - "交叉编译与 CMake"
---

# Dependencies and the C++ Standard — Modern Ways to Write find_package and cxx_std_NN

In the previous article we worked targets and usage requirements all the way through, with hands-on tests of how the PUBLIC/PRIVATE/INTERFACE three states propagate along the link graph. This one picks up two concrete questions you hit constantly in real projects: how do you tell CMake you want C++20, and how do you link a third-party library in. Search the web and you get answers for both instantly, but the old-style recipes are still floating around in tons of tutorials, and copying them plants landmines. We will run every recipe once and see clearly why some of them belong in the wastebasket.

## Three ways to set the C++ standard, which one is right

For setting the C++ standard, you see three styles coexisting in the CMake world. Let us take them one at a time, put the code on the table first, then explain why.

First, attached to a target:

```cmake
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_20)
```

Second, a directory-level variable, the one we used to get the project running back in the getting-started volume [getting-started/04](/getting-started/04-multi-file-cmake):

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

Third, jamming `-std=c++20` straight into the compile flags:

```cmake
string(APPEND CMAKE_CXX_FLAGS " -std=c++20")   # anti-pattern, do not copy
```

CMake officially sanctions the first two; the third is an anti-pattern. Below we use real test output to explain why.

### `target_compile_features` is "minimum requirement" semantics

`target_compile_features(app PRIVATE cxx_std_20)` translated into plain English reads: when the `app` target compiles, the C++ standard must not be lower than C++20. Note the wording. It is "must not be lower than," not "must equal exactly."

That semantics matters. CMake takes this requirement and compares it against the compiler's default standard, then decides based on the comparison whether to inject a `-std` flag into the compile command. Let us test this with GCC 16.1.1, whose default standard is `gnu++20` (run `g++ -dM -E -x c++ /dev/null | grep __cplusplus` and you see `202002L`, which is C++20). The `CMakeLists.txt` below creates three targets that ask for 17, 20, and 23 respectively:

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

Using the Make generator with `CMAKE_VERBOSE_MAKEFILE` on, look at the command CMake actually sends to `g++` for each target:

```text
$ cmake -S . -B build -G "Unix Makefiles" -DCMAKE_VERBOSE_MAKEFILE=ON > /dev/null
$ cmake --build build --target app_cxx17 2>&1 | grep "/c++"
/usr/sbin/c++    -MD -MT ... -c .../main.cpp
$ cmake --build build --target app_cxx20 2>&1 | grep "/c++"
/usr/sbin/c++    -MD -MT ... -c .../main.cpp
$ cmake --build build --target app_cxx23 2>&1 | grep "/c++"
/usr/sbin/c++   -std=gnu++23 -MD -MT ... -c .../main.cpp
```

Read it line by line. The target asking for 17 has no `-std` in its compile command: the compiler default is already 20, which is higher than 17, so CMake judges the requirement satisfied and adds no flag. The one asking for 20 also has none: the default is 20, spot on. Only the one asking for 23 sprouts a `-std=gnu++23`: the default of 20 is not enough, so CMake proactively bumps it to 23.

That is the beauty of "minimum requirement" semantics. When you write `cxx_std_20` you are declaring "this code uses C++20 features, anything below 20 will not compile," and CMake adds flags as needed. It will never secretly lower the default 20 down to 17. Move to an older compiler whose default is `gnu++17` (GCC 11, say), and the same `CMakeLists.txt` makes CMake automatically add `-std=gnu++20`. One configuration, correct standard across compiler versions.

::: details What is that gnu++ thing, can I drop it
`gnu++20` is GCC's "C++20 plus GNU extensions" dialect; the pure-standard spelling is `c++20`. The difference is that the former lets you use GCC-only toys like `typeof` and zero-length arrays, which hurts portability. CMake defaults to `gnu++NN` for old-code compatibility, but you can force it back to pure `c++NN` by setting `CXX_EXTENSIONS OFF` on the target:

```cmake
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_23)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)
```

In testing, with the same `cxx_std_23` requirement and `CXX_EXTENSIONS OFF` turned on, the compile flag changes from `-std=gnu++23` to `-std=c++23`:

```text
$ cmake --build build --target app 2>&1 | grep "/c++"
/usr/sbin/c++   -std=c++23 -MD -MT ... -c .../main.cpp
```

For new projects I recommend defaulting it to OFF. Behavior is more predictable across compilers.
:::

`cxx_std_NN` can also go PUBLIC, reusing the usage-requirement propagation we covered in the previous article. A library that itself requires C++20 hands that requirement down automatically to whoever links it:

```cmake
target_compile_features(mylib PUBLIC cxx_std_20)
```

When downstream links `mylib`, CMake sees `cxx_std_20` sitting in `INTERFACE_COMPILE_FEATURES` and automatically raises downstream's standard a notch. This is the biggest advantage of the target-level style over the directory-level style: the standard requirement rides the target, propagates automatically along the dependency graph, and you stop rewriting `set(CMAKE_CXX_STANDARD 20)` in every downstream project.

### The directory-level `set(CMAKE_CXX_STANDARD)`: works, but has a ceiling

We used the second style back in the getting-started volume. It looks like this:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

These three lines scope over "every target in the current `CMakeLists.txt` and its subdirectories," essentially assigning a default to the `CXX_STANDARD` property of every target under that directory. `CMAKE_CXX_STANDARD_REQUIRED ON` is mandatory. It tells CMake "if the compiler cannot reach this standard, error out," otherwise, when the compiler is too old, CMake silently degrades and compiles past it. It compiles, but the behavior is wrong, and you debug for a long time before finding the root cause here.

::: warning Forgetting `CMAKE_CXX_STANDARD_REQUIRED ON` silently degrades
CMake defaults `CMAKE_CXX_STANDARD_REQUIRED` to `OFF`, meaning "if the compiler does not support this standard, try anyway." The result is that you write `set(CMAKE_CXX_STANDARD 20)`, the compiler tops out at 17, and CMake does not error. It just quietly compiles with 17. You use a C++20 `concept` or a template lambda, it fails to compile, but the error does not point at "the standard got degraded." It points at the specific syntax line, and you go around the long way before tracing it back. So `CMAKE_CXX_STANDARD` and `CMAKE_CXX_STANDARD_REQUIRED ON` have to be written as a pair.
:::

This style is still acceptable today, because in the vast majority of projects the C++ standard is one "project-wide uniform" value. But it has two spots where it loses to the target-level style. First, it does not propagate with the target: someone linking your library does not automatically inherit the standard requirement. Second, its scope is directory-level, which is fundamentally a global setting just like the `include_directories()` from the previous article. Once the project gets complicated, it stops being precise enough.

Migration advice: for new projects, prefer `target_compile_features(mylib PUBLIC cxx_std_NN)` and make the standard a usage requirement of the target. Old projects can keep using `set(CMAKE_CXX_STANDARD)` without breaking anything; swap it out the next time you refactor.

### Hand-stuffing `-std=c++20`: an anti-pattern, do not write it

The third style looks the most "direct," and you see it all over old tutorials on the web:

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
# or
string(APPEND CMAKE_CXX_FLAGS " -std=c++20")
add_executable(app main.cpp)
```

In testing it does put `-std=c++20` into the compile command:

```text
$ cmake -S . -B build -G "Unix Makefiles" -DCMAKE_VERBOSE_MAKEFILE=ON > /dev/null
$ cmake --build build --target app 2>&1 | grep "/c++"
/usr/sbin/c++   -std=c++20 -MD -MT ... -c .../main.cpp
```

Looks fine. The problems are all behind it. First, this line bypasses CMake's standard management. CMake keeps an internal table of "which standards each compiler knows, and which flag each standard maps to," and both `target_compile_features` and `set(CMAKE_CXX_STANDARD)` walk that table. When you hand-stuff `-std=c++20`, CMake does not know you set the standard, so the `CMAKE_CXX_STANDARD` variable stays empty. Downstream trying to read it for its own logic gets an empty value, and the standard-propagation chain through the dependency graph is severed.

Second, it is inconsistent across platforms. GCC and Clang use `-std=c++20`; MSVC uses `/std:c++20`. Hardcoding the GCC-style flag breaks the moment you move to an MSVC project. CMake's standard management papers over that difference for you: write `cxx_std_20`, and CMake picks the right flag for the platform itself.

Finally, it is completely detached from `CXX_EXTENSIONS` and `CMAKE_CXX_STANDARD_REQUIRED`, which means you are bypassing the whole abstraction and rolling your own. The moment a single line like `set(CMAKE_CXX_FLAGS ... -std=...)` shows up in a `CMakeLists.txt`, it flags that the file is still written in old-style CMake thinking.

Migration rule: delete every place that hand-stuffs `-std=`, and replace it with the first or second style above.

## find_package: how to link a third-party library

The C++ standard business settled, on to third-party libraries. In a real project, a library like `fmt` is not something we hand-write. It comes in from the system or a package manager, and the command CMake gives you is `find_package`.

The modern style is two lines and done:

```cmake
find_package(fmt REQUIRED)
target_link_libraries(app PRIVATE fmt::fmt)
```

What `find_package(fmt REQUIRED)` does is go look in a few standard locations (`<prefix>/lib/cmake/fmt/` under `CMAKE_PREFIX_PATH`, and so on) for a `fmt-config.cmake` (also called a package configuration file), and execute it once found. This config file ships with fmt itself, and it knows where fmt's headers are, where the library files are, and which compile options the link needs. After it runs, your project gains a target called `fmt::fmt` out of thin air.

This `fmt::fmt` is an **imported target**. Imported targets differ from the ordinary targets we covered in the previous article. They are not built inside your project; someone else built them, packed them into a config file, and `find_package` carried them in. They carry the same usage-requirement properties: `INTERFACE_INCLUDE_DIRECTORIES`, `INTERFACE_COMPILE_DEFINITIONS`, `IMPORTED_LOCATION`, and friends. The moment you write `target_link_libraries(app PRIVATE fmt::fmt)`, those properties flow onto `app` automatically, just like PUBLIC did in the previous article.

Let us peel a real `fmt::fmt` open and look. The local machine has fmt 12.2.0 installed, and its config file sits at `/usr/lib/cmake/fmt/fmt-config.cmake`. The key lines that create `fmt::fmt` (from `fmt-targets.cmake`) look like this:

```cmake
add_library(fmt::fmt SHARED IMPORTED)
set_target_properties(fmt::fmt PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"
  ...
)
```

`SHARED IMPORTED` tells CMake this is an imported target for a dynamic library. `INTERFACE_INCLUDE_DIRECTORIES` is its public header path. The `CMakeLists.txt` below finds fmt and then prints a few of its properties out:

```cmake
find_package(fmt REQUIRED)
foreach(prop TYPE INTERFACE_INCLUDE_DIRECTORIES INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_FEATURES)
    get_target_property(v fmt::fmt ${prop})
    message(STATUS "fmt::fmt.${prop} = ${v}")
endforeach()
```

Run configure once:

```text
$ cmake -S . -B build
-- fmt::fmt.TYPE = SHARED_LIBRARY
-- fmt::fmt.INTERFACE_INCLUDE_DIRECTORIES = /usr/include
-- fmt::fmt.INTERFACE_COMPILE_DEFINITIONS = FMT_SHARED
-- fmt::fmt.INTERFACE_COMPILE_FEATURES = cxx_std_11
```

Read it line by line. `TYPE` is SHARED_LIBRARY, a dynamic library. `INTERFACE_INCLUDE_DIRECTORIES` is `/usr/include`, the source of the path downstream uses to include `<fmt/core.h>`. `INTERFACE_COMPILE_DEFINITIONS` is `FMT_SHARED`, a crucial signal: when fmt is built as a dynamic library, downstream linking it must define `FMT_SHARED` to import the symbols correctly. `INTERFACE_COMPILE_FEATURES` is `cxx_std_11`, fmt declaring that it needs at least C++11 itself.

You link with one line `target_link_libraries(app PRIVATE fmt::fmt)`, and those four properties become part of `app`'s compile environment automatically. Let us look at the compile command `app` actually receives:

```text
$ cmake -S . -B build -G Ninja > /dev/null && cmake --build build -v 2>&1 | grep "/c++"
[1/2] /usr/sbin/c++ -DFMT_SHARED   -MD -MT ... -c .../main.cpp
```

Note the `-DFMT_SHARED` that appears out of nowhere. `app`'s `CMakeLists.txt` never wrote that line; it comes from `fmt::fmt`'s `INTERFACE_COMPILE_DEFINITIONS`. `/usr/include` is a system default path so it does not show up explicitly in the command, but install fmt to a non-standard path (say `/opt/fmt`) and that `-I/opt/fmt/include` will appear automatically. The link stage is the same. Look at the actual link command:

```text
[2/2] : && /usr/sbin/c++ ... CMakeFiles/app.dir/main.cpp.o -o app  /usr/lib/libfmt.so.12.2.0  && :
```

`/usr/lib/libfmt.so.12.2.0` is the real library file path that `fmt::fmt`'s `IMPORTED_LOCATION` resolves to. CMake does the entire dirty job for you, finding headers, passing compile macros, finding the library file. You only have to write the name `fmt::fmt`.

That is the fundamental advantage of imported targets over the old style: they package the library's "usage requirements" into one object. You link once, every configuration that should come along arrives in place, and when the library upgrades or moves path you do not change a line of code.

### The old style: the `${fmt_INCLUDE_DIRS}` variable flavor

Another recipe you see in old tutorials online looks like this:

```cmake
find_package(fmt REQUIRED)
include_directories(${fmt_INCLUDE_DIRS})                      # anti-pattern
add_executable(app main.cpp)
target_link_libraries(app ${fmt_LIBRARIES})                   # anti-pattern
```

`include_directories(${fmt_INCLUDE_DIRS})` is the directory-level global command from the previous article, polluting every target under the current directory. The `${fmt_LIBRARIES}` variable style relies on the config file writing the library list into a variable, which you then read out by hand and pass to `target_link_libraries`. The problem is that this style does not propagate usage requirements at all. `fmt_LIBRARIES` is only a list of library names; it carries no `-DFMT_SHARED`, no `INTERFACE_INCLUDE_DIRECTORIES`, no `cxx_std_11`. Miss one and it either fails to compile or behaves wrong.

Worse, these variable names follow no unified convention. fmt might use `fmt_LIBRARIES`, OpenCV might use `OpenCV_LIBS`, Boost might use `Boost_LIBRARIES`, and every library you bring in forces you to look up which variables its config file provides. Imported targets, by contrast, are uniformly namespaced as `LibName::LibName`. Once you find that `::`-bearing name in the docs, you link once and you are done.

Migration rule: delete every `include_directories(${X_INCLUDE_DIRS})`, and change every `target_link_libraries(app ${X_LIBRARIES})` into `target_link_libraries(app PRIVATE X::X)`. The precondition is that the library's config file provides an imported target. Mainstream libraries today (fmt, spdlog, Catch2, nlohmann_json, and friends) all do.

::: warning What if the library does not provide an imported target
A handful of old libraries, or config files you hand-wrote yourself, may only provide `${X_INCLUDE_DIRS}` variables and no `X::X` imported target. In that case you have two options. One, build an INTERFACE library yourself as a wrapper:

```cmake
find_package(OldLib REQUIRED)
add_library(OldLib::OldLib ALIAS OldLib::OldLib)   # does not work, OldLib is not a target
# correct approach: build an interface target that wraps the variables
add_library(oldlib_wrapper INTERFACE)
target_include_directories(oldlib_wrapper INTERFACE ${OldLib_INCLUDE_DIRS})
target_link_libraries(oldlib_wrapper INTERFACE ${OldLib_LIBRARIES})
target_link_libraries(app PRIVATE oldlib_wrapper)
```

Now downstream uniformly links `oldlib_wrapper`, and configuration propagates outward from this one place. Two, pester the library author to update the config file, or just switch libraries.
:::

## What to do when the package is not found

The diagnostic path when `find_package` errors has a fixed playbook. First look at what a real error looks like. The `CMakeLists.txt` below asks for a library that does not exist at all:

```cmake
find_package(NonExistentPkg 9.9.9 REQUIRED)
```

Configure dies outright, and CMake reports:

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

That error message carries a lot. Let us break it apart. The first paragraph says "you did not provide `FindNonExistentPkg.cmake` in `CMAKE_MODULE_PATH`," meaning CMake first searched in "Module mode" for a built-in or user-provided `FindX.cmake` and found nothing. The second paragraph says "the package configuration file provided by `NonExistentPkg` was not found either," listing the file names it tried, where `.cps` is the CPS (CMake Package Specification) format introduced in CMake 3.29, and `.cmake` is the classic format. The third paragraph hands you the diagnostic path.

Going by what the error suggests, the common reasons `find_package` cannot find a package are these, ordered by how often they show up:

First, the library is not installed at all. Most common. Confirm the library actually exists on the system first. On Linux, query with the package manager (`apt list --installed | grep fmt`, `pacman -Qs fmt`); on Windows, check `vcpkg list`; on macOS, check `brew list`. If it is not installed, install it, and when you do, watch for whether you need a `-dev` or `-devel` suffixed development package, because some distros split the runtime library and the headers apart. Install only the runtime and `find_package` still cannot find it.

Second, it is installed but `CMAKE_PREFIX_PATH` is not set. The library sits in a non-standard path (you ran `make install` into `/opt/fmt`, or vcpkg installed into `~/vcpkg/installed/x64-linux`). CMake only searches a few standard locations like `/usr` and `/usr/local` by default, so naturally it cannot find it. The fix is to add `-DCMAKE_PREFIX_PATH=/opt/fmt` at configure time, or set the `CMAKE_PREFIX_PATH` environment variable. The next article on CMakePresets will pin this kind of `-D` into JSON.

Third, the vcpkg or Conan toolchain file was not injected. After these two package managers install a library, the library lives in a directory they manage themselves (vcpkg's `installed/`, Conan's `~/.conan2/`), not in the system standard paths. They hand you a toolchain file. You pass it in at configure time via `-DCMAKE_TOOLCHAIN_FILE=<path>/vcpkg.cmake`, and that toolchain file automatically points `CMAKE_PREFIX_PATH` at the libraries it installed. Forget to hook the toolchain in, and the install was wasted. `find_package` still cannot find it. This is the trap beginners hit the most.

Fourth, the library is installed but provides no config file. For example the system has an old fmt 5.x, from before fmt shipped `fmt-config.cmake`. Back then there was only a Module-mode lookup file like `FindFMT.cmake` (or even nothing). In that case `find_package(fmt)` runs in Config mode and finds nothing, so you either upgrade the library, write a `FindX.cmake` yourself, or bridge through pkg-config.

::: details The two lookup modes of find_package
`find_package(X)` walks two modes by default, Module first then Config.

Module mode looks for `FindX.cmake`, a file whose name starts with `Find`. These files are written by CMake itself (over a hundred built-in `FindX.cmake` files for common libraries), or provided by you under `CMAKE_MODULE_PATH`. Common in old-style code, because back then many libraries did not ship their own config files and relied on CMake-community-maintained Modules as a bridge.

Config mode looks for `X-config.cmake` or `XConfig.cmake` (CMake 3.29+ also looks for `.cps` files), files whose name starts with the library name. These files are installed by the library author and ship with the library, so they are more accurate than community-maintained Modules. Modern mainstream libraries (fmt, spdlog, Catch2, Boost 1.70+, and so on) all ship their own Config files, so `find_package` in practice mostly runs in Config mode.

CMake defaults to Module first then Config. You can force only one with `find_package(X CONFIG)` or `find_package(X MODULE)`. For new projects I recommend writing `CONFIG` explicitly. The behavior is clearer, and it avoids a stale built-in `FindX.cmake` getting picked up before the library's own config file, which would make the behavior inconsistent.
:::

## Hooking up vcpkg / Conan in one sentence

We have not said yet where third-party libraries come from. Libraries installed by system package managers (apt, pacman, brew) tend to be old, inconsistent across platforms, and not necessarily installable on CI, so for serious projects you generally do not use them. The two mainstream package managers in the C++ world are vcpkg and Conan. What they do is build the library for you, install it into their own directory, and then hand you a toolchain file so CMake's `find_package` can find it.

The key piece of usage is one configure argument:

```text
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

Libraries installed by vcpkg all live under `<vcpkg-root>/installed/`, and its toolchain file automatically points `CMAKE_PREFIX_PATH` there. So in your project `find_package(fmt REQUIRED)` works just the same as with a system-installed library, no difference. Conan works the same way; the toolchain file it generates is called `conan_toolchain.cmake`. We leave the details of this mechanism, how to write the manifest file, how to pin versions, and how it hooks into the CMakePresets article coming next, for a later package-management topic. The one thing to remember here: after the library is installed, injecting the toolchain file is the step that lets CMake find it.

## The companion example

The example project for this article lives in the repo at `code/examples/vol7/cmake-fundamentals/03-find-package/`, structured like this:

```text
03-find-package/
├── CMakeLists.txt    # target_compile_features + an optional find_package section
└── main.cpp          # uses a C++20 template lambda to prove the standard took effect
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

`main.cpp` uses a template lambda that only exists from C++20 onward (`[]<typename T>(T a, T b) { return a + b; }`) to prove `cxx_std_20` really did propagate the standard requirement into the compile command. If you want to feel the "minimum requirement" semantics in your own hands, change `cxx_std_20` to `cxx_std_23`, reconfigure, and look at the compile command with `cmake --build build -v`. You will see CMake automatically add a `-std=c++23`.

## What comes next

By here, the three ways to set the C++ standard and `find_package`'s imported-target mechanism have all landed on real code. Our configure command in this article has grown into something like:

```text
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=/opt/fmt ...
```

Once the `-D` list grows long, problems start: mistype a variable name and configure does not error, it just silently runs an empty config; teammates keep asking each other what to fill in for the vcpkg path; on CI you change one option and a PR goes red across the board. The next article covers `CMakePresets.json`, the mechanism CMake 3.19 brought in to pin all these scattered `-D` flags, the generator choice, and the toolchain injection into one JSON file, slimming the command down to a single `cmake --preset debug`.
