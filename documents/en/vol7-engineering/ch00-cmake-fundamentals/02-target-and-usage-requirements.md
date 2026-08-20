---
title: "The Target Mental Model — Treat a Target as an Object, PUBLIC/PRIVATE/INTERFACE Are Usage Requirements"
description: "Explain what a target really is, why the target_* commands are member methods, how PUBLIC/PRIVATE/INTERFACE propagate, and why directory-level commands are an anti-pattern"
chapter: 7
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 20
prerequisites:
  - "vol7 ch00 01: CMake 是什么——构建系统生成器的两段式流水线"
related:
  - "交叉编译与 CMake"
  - "编译器选项"
---

# The Target Mental Model — Treat a Target as an Object, PUBLIC/PRIVATE/INTERFACE Are Usage Requirements

In the previous article we got a minimal project running, with just one line of real work in `CMakeLists.txt`: `add_executable(hello main.cpp)`. I never gave a name to the thing that line produces. This article hands you that name: **target**.

The word "target" shows up everywhere in the CMake docs, gets crowned the number-one concept in every "modern CMake" tutorial, and the community even has a catchphrase for it: think in targets, not variables. Why does modern CMake lift it so high, and why is the `include_directories()` you copied from an old tutorial already an anti-pattern? This article explains it all the way through. This is the watershed between modern CMake and old-style CMake. Once you cross it, reading any `CMakeLists.txt` afterwards stops feeling like reciting incantations.

## Treat a Target as an Object

"Target" is not an abstract metaphor. It is, literally, a data structure CMake keeps internally. The fastest way to understand it is to think of it as a C++ object.

`add_executable(app main.cpp)` and `add_library(mylib STATIC src/mylib.cpp)` are **constructors**. They create a target object, name it `app` or `mylib`, and record which source files it is built from and whether it should compile into an executable or a library. From that line onward, the names `app` and `mylib` are "alive" in CMake's world, and every later configuration works by treating that name as a handle.

Once created, you give it include search paths, tell it which libraries to link, and switch on compile options. These operations correspond to a family of commands that all start with `target_*`:

```cmake
target_include_directories(mylib PUBLIC include)
target_link_libraries(mylib PRIVATE fmt)
target_compile_options(mylib PRIVATE -Wall -Wextra)
target_compile_features(mylib PUBLIC cxx_std_17)
```

These `target_*` commands are **member methods**. They all do the same thing under the hood: take the target's name and attach a property to that target object. `target_include_directories(mylib PUBLIC include)` translates to "for the object `mylib`, push `include` into its include-path property."

The things hanging off the target (include paths, the list of linked libraries, compile options, the C++ standard requirement) are its **member variables**. Each target manages its own, without bothering the others.

::: details What a target actually is inside CMake
Strictly speaking, a target is a named collection of properties maintained by CMake. You can read the properties attached to it during the configure stage with `get_target_property(v mylib INCLUDE_DIRECTORIES)`. The hands-on section later in this article will use exactly this command to crack the target open and show us. A target is not a black box.
:::

Why does this "object thinking" matter? Because it nails down the scope of any configuration. `target_include_directories(mylib PUBLIC include)` touches only the properties of `mylib`, leaving every other target in the project untouched. That is exactly the core distinction coming next: old-style CMake is "global pollution," modern CMake is "target-private."

## Usage Requirements: The PUBLIC/PRIVATE/INTERFACE Three States

Having the target object alone is not enough. What actually lets modern CMake leap forward is how it models **usage requirements**. The phrase sounds mystical, but it boils down to one sentence: the configuration a target needs when it is compiling itself may differ from what it needs when someone else links against it. CMake uses three keywords to separate the two cases.

PRIVATE means "I need it for my own compile, but whoever links me does not." For example, `mylib` calls the third-party library `fmt` internally for string formatting, but `fmt` leaves no trace in `mylib`'s public header. Downstream users linking `mylib` have no idea `fmt` exists, and naturally do not need `fmt`'s include path. In that case `fmt` is PRIVATE to `mylib`.

INTERFACE means "I do not need it myself, but whoever links me does." A typical case is a header-only library. It has no `.cpp` of its own to compile, so the "self-use" half is empty; but the moment downstream includes its headers, it needs the corresponding include path and C++ standard requirement. Here every configuration goes into INTERFACE.

PUBLIC means "both sides: I use it, and so does whoever links me." The most common case is a type that appears directly in the public header. If the return type of `mylib.h` is `std::string`, then once downstream links `mylib`, the compiler has to find the include path where `<string>` lives in order to parse that return type. `mylib` itself needs that path when compiling its `.cpp`, and downstream needs it when linking `mylib`. That is PUBLIC.

Splitting these three states along "self-use / others-use" sits on top of a simple truth table:

| Keyword | Used when compiling self | Also used when others link |
|--------|:---:|:---:|
| PRIVATE | Yes | No |
| INTERFACE | No | Yes |
| PUBLIC | Yes | Yes |

Memorize this table. It fits every `target_*` command you will ever read.

### A Concrete Example: fmt Is PRIVATE, `<string>` Is INTERFACE

Definitions alone are not enough; let us drop down to code. The project below has three targets: a minimal `fmt` (standing in for a third-party formatting library), a `mylib` static library that exposes an outward-facing API, and a downstream `app` executable. `mylib` uses `fmt::format` internally, but its public header uses only `std::string`.

The public header of `mylib`, `include/mylib/mylib.h`:

```cpp
#pragma once
#include <string>

namespace mylib {

/// @brief 把问候语格式化成带前缀的字符串
/// @note  返回类型用 std::string —— 这是 mylib 公开 API 的一部分,
///        下游 app 也必须看到完整的 std::string 定义,
///        所以 <string> 对应的 include 路径属于 INTERFACE 需求
std::string make_greeting(const std::string& name);

}  // namespace mylib
```

The implementation of `mylib`, `src/mylib.cpp`:

```cpp
#include "mylib/mylib.h"

#include "fmt.h"

namespace mylib {

std::string make_greeting(const std::string& name) {
    // fmt 是 mylib 内部实现细节,公开头文件 mylib.h 里看不到 fmt 的痕迹
    // 所以下游根本不需要知道 fmt 的存在 —— 这正是 fmt 应当为 PRIVATE 的理由
    return fmt::format("hello, {}!", name);
}

}  // namespace mylib
```

The three key lines in `CMakeLists.txt` that attach properties to `mylib`:

```cmake
add_library(mylib STATIC src/mylib.cpp)
target_include_directories(mylib PUBLIC include)
target_link_libraries(mylib PRIVATE fmt)
```

`include` is written as PUBLIC: `mylib` needs to find `mylib/mylib.h` when compiling its own `.cpp` (self-use), and downstream has to find `mylib/mylib.h` to include it after linking `mylib` (others-use). Both halves hold, so it is PUBLIC.

`fmt` is written as PRIVATE: `mylib.cpp` calls `fmt::format` internally (self-use), but `mylib.h` carries no `fmt` symbol and downstream never needs to see `fmt.h` (not others-use), so it is PRIVATE.

### Flip PRIVATE to PUBLIC, Watch Downstream Get "Infected"

Explaining concepts in the abstract never sticks. Let us get our hands dirty and change `fmt` from PRIVATE to PUBLIC, and see what happens to `app`.

First configure the project (using the Make generator, because its `flags.make` file lists the include paths each target actually receives in plain, readable form; Ninja splits the flags into other files to support C++ modules, which is awkward to read by eye):

```text
$ cmake -S . -B build -G "Unix Makefiles"
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
```

Right now `mylib` declares `fmt` as PRIVATE. Look at the include flags CMake generated for each of the three targets:

```text
$ cat build/CMakeFiles/mylib.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include -I/tmp/cmake-target-demo/fmt

$ cat build/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include
```

Read this line by line. `mylib` gets two paths: its own `include` (PUBLIC) plus `fmt` (PRIVATE, also needed when compiling itself). `app` gets only one path, `include`, because it links only `mylib` and therefore inherits `mylib`'s PUBLIC part (which is `include`); `fmt` is `mylib`'s PRIVATE and does not cross over. `app` knows nothing about `fmt`. That is exactly the encapsulation we want.

What if `app`'s `main.cpp` sneaks in an `#include "fmt.h"` now? The compiler cannot find that header and dies immediately. I tried it:

```text
$ cmake --build build --target app
[ 50%] Building CXX object CMakeFiles/app.dir/main.cpp.o
FAILED: CMakeFiles/app.dir/main.cpp.o
/tmp/cmake-target-demo/main.cpp:2:10: fatal error: fmt.h: No such file or directory
    2 | #include "fmt.h"
      |          ^~~~~~~
compilation terminated.
```

That is the physical meaning of PRIVATE: the encapsulation is real, not lip service.

Now change one line, from `target_link_libraries(mylib PRIVATE fmt)` to `target_link_libraries(mylib PUBLIC fmt)`, reconfigure, and look at `app`'s include flags again:

```text
$ sed -i 's/target_link_libraries(mylib PRIVATE fmt)/target_link_libraries(mylib PUBLIC fmt)/' CMakeLists.txt
$ cmake -S . -B build -G "Unix Makefiles" > /dev/null
$ cat build/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include -I/tmp/cmake-target-demo/fmt
```

`app` changed nothing at all, yet because upstream `mylib` flipped `fmt` from PRIVATE to PUBLIC, `app` magically gained a `-I.../fmt`. Now `app` does not have to `find_package(fmt)` itself, does not have to write `target_link_libraries(app PRIVATE fmt)` itself, and can simply `#include "fmt.h"` and compile.

This is the **propagation** of usage requirements: PUBLIC lets configuration seep downstream along the link graph, while PRIVATE locks configuration inside the target. This "automatic propagation" is the root reason modern CMake can write complex dependency relationships so cleanly. As long as you correctly mark each dependency public or private, downstream picks up exactly the configuration it should, automatically, with a single link.

::: warning Do not use PUBLIC as a universal patch
Reading this far you might be tempted: if PUBLIC hands downstream the configuration automatically, why not mark every dependency PUBLIC and be done with it? Please do not. PUBLIC means leaking your internal implementation details downstream. The moment downstream starts depending on the `fmt` path you exposed, the day you swap `fmt` for `std::format`, or upgrade and change the path, downstream breaks with it. Encapsulation is breathing room for the future; the more PUBLIC you sprinkle, the less room you leave yourself to refactor. The rule: if PRIVATE works, do not reach for PUBLIC.
:::

### What Is That LINK_ONLY in INTERFACE_LINK_LIBRARIES?

There is a detail worth expanding on here. I dug into `mylib`'s internal properties with `get_target_property` (with `fmt` configured as PRIVATE):

```text
mylib.INCLUDE_DIRECTORIES         = /tmp/cmake-target-demo/include
mylib.INTERFACE_INCLUDE_DIRECTORIES = /tmp/cmake-target-demo/include
mylib.LINK_LIBRARIES              = fmt
mylib.INTERFACE_LINK_LIBRARIES    = $<LINK_ONLY:fmt>
```

Notice the last line. PRIVATE is supposed to mean "downstream has no idea fmt exists," so why does `fmt` show up in `INTERFACE_LINK_LIBRARIES`?

There is a subtle but sensible distinction here: PRIVATE encapsulates the **include path** (downstream does not need `fmt.h` at compile time), but the **link relationship** cannot be hidden. `mylib` is a static library, and its `.o` files reference `fmt::format` symbols. When the linker finally turns `app` into an executable, it has to be able to find `libfmt.a` to fill those symbols in, or it throws `undefined reference`. So CMake uses the generator expression `$<LINK_ONLY:fmt>` to say "fmt participates in linking for downstream, but not in compilation." That explains why you do not see `-I.../fmt` in `app`'s `flags.make` (the include path did not cross over), yet `app` still links into a working executable (the link relationship did cross over). PUBLIC/PRIVATE controls the propagation of configuration, not the link graph itself.

## Why Directory-Level Commands Are an Anti-Pattern

Once target privacy is clear, going back to old-style CMake makes it obvious why the modern CMake crowd uniformly boycotts these commands.

Old-style CMake uses directory-level, global commands:

```cmake
# 老式 CMake 写法,现代项目里见一次就该重构
include_directories(include)
include_directories(fmt)
add_definitions(-DUSE_FMT)
add_compile_options(-Wall)
```

The semantics of `include_directories(include)` are "every target in the current `CMakeLists.txt` directory and its subdirectories gets `-Iinclude`, no exceptions." `add_definitions(-DUSE_FMT)` works the same way: every target gets the `-DUSE_FMT` macro defined.

In a small project you cannot see the flaw. Scale up and it falls apart. Picture a project with `mylib`, `tests`, `benchmarks`, and `tools` (four or five targets). You write `add_compile_options(-Wall -Wextra -Werror)` in the top-level `CMakeLists.txt` intending to turn on strict warnings for the main library, and the third-party Catch2 code under `tests/` inherits `-Werror` too, flooding the build with red. Now you have to dig into `tests/CMakeLists.txt` and remember a pile of workaround incantations to turn `-Werror` back off there.

Or imagine `mylib` uses `fmt` internally, and to save effort you write `include_directories(fmt)` at the top level. Now `tools`, a target that should have no idea `fmt` exists, also picks up `-Ifmt`. The day its source code accidentally `#include "fmt.h"` it still compiles, and the encapsulation is silently broken. When a maintainer later tries to swap out `fmt`, they have no way to tell which targets used `fmt` on purpose and which got it stained on by a global command.

Modern CMake solves both problems with target-level commands. `target_include_directories(mylib PRIVATE fmt)` bolts `fmt`'s path tightly inside the `mylib` target. It neither leaks to `tools` nor to downstream `app` (because PRIVATE). Each target carries its own configuration boundary; whoever owns the dependency declares it, and the dependency graph stays legible and traceable.

Side-by-side comparison:

```cmake
# 老式(目录级,全局污染)
include_directories(include)
add_definitions(-DMYLIB_EXPORTS)

# 现代(target 级,边界清晰)
target_include_directories(mylib PUBLIC include)
target_compile_definitions(mylib PRIVATE MYLIB_EXPORTS)
```

The migration rule is straightforward: swap every `include_directories()` for `target_include_directories()`, every `add_definitions()` for `target_compile_definitions()`, every `add_compile_options()` for `target_compile_options()`, and prefix each command with a specific target name. It is the cheapest single step for dragging an old project into modern CMake.

::: details Can I still set the C++ standard with a variable at the top level?
You will see many `CMakeLists.txt` files write `set(CMAKE_CXX_STANDARD 17)` at the top. That is also a directory-level (global) setting; it assigns the `CXX_STANDARD` property to every target under the current directory. This usage is still acceptable today, because for most projects the C++ standard is genuinely a project-wide global property. But the more modern, more precise form is `target_compile_features(mylib PUBLIC cxx_std_17)`, which turns the C++ standard into a target usage requirement too: linking `mylib` downstream automatically inherits the C++17 requirement. The next article, on `find_package`, will come back to compare the two forms.
:::

## Hands-On: Tear Down a Two-Target Project

Let us assemble everything from above. We will use a complete, runnable project to demonstrate the two-target setup of a `mylib` static library plus an `app` executable, and see how PUBLIC/PRIVATE actually flows in a real build. The full project lives at `code/examples/vol7/cmake-fundamentals/02-target/`, with this layout:

```text
02-target/
├── CMakeLists.txt
├── fmt/
│   ├── fmt.h          # 模拟第三方库的极简实现
│   └── fmt.cpp
├── include/
│   └── mylib/
│       └── mylib.h    # mylib 公开头文件
├── src/
│   └── mylib.cpp      # mylib 实现
└── main.cpp           # app 可执行
```

The complete `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(target_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# fmt:仅在本工程内部使用的极简"第三方库",真实工程会换成 find_package(fmt REQUIRED)
add_library(fmt STATIC fmt/fmt.cpp)
target_include_directories(fmt PUBLIC fmt)

# mylib:对外暴露的库,公开头文件 include/mylib/mylib.h 用了 std::string
add_library(mylib STATIC src/mylib.cpp)
target_include_directories(mylib PUBLIC include)
# fmt 在这里写成 PRIVATE —— mylib.cpp 内部要用,但 mylib.h 完全不暴露 fmt
target_link_libraries(mylib PRIVATE fmt)

# app:下游可执行,只链接 mylib,对 fmt 一无所知
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mylib)
```

Reading this config bottom-up makes the intent clearer. `app` declares only "I link `mylib`," nothing else. `mylib` exposes its own `include` directory as PUBLIC, so downstream gets that path automatically when linking it; it locks `fmt` into PRIVATE, so downstream had better not find out `fmt` is in use. `fmt` exists as a STATIC library in its own right, with `include` as its own PUBLIC (so `mylib` picks up the `fmt.h` path when linking it).

Three steps to bring the project up:

```text
$ cmake -S . -B build -G Ninja && cmake --build build && ./build/app
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-target-demo/build
[1/6] Building CXX object CMakeFiles/fmt.dir/fmt/fmt.cpp.o
[2/6] Linking CXX static library libfmt.a
[3/6] Building CXX object CMakeFiles/mylib.dir/src/mylib.cpp.o
[4/6] Linking CXX static library libmylib.a
[5/6] Building CXX object CMakeFiles/app.dir/main.cpp.o
[6/6] Linking CXX executable app
hello, world!
```

The six-step order reveals the dependency graph. `fmt` builds first (steps 1-2, it depends on nothing), `mylib` builds next (steps 3-4, it depends on `fmt`), and `app` builds last (steps 5-6, it depends on `mylib`). Ninja orders everything by dependency automatically; you do not lift a finger.

The final line, `hello, world!`, is what `app` prints. In `main.cpp` it only does `#include "mylib/mylib.h"`, yet the compiler finds that header, because `mylib` marked `include` as PUBLIC and `app` inherited `-I.../include` when it linked `mylib`.

If we want to verify this inheritance is really happening, the most direct way is to look at the include flags `app` actually received. Configure once with the Make generator and read `app.dir/flags.make`:

```text
$ cmake -S . -B build-mk -G "Unix Makefiles" > /dev/null
$ cat build-mk/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include
```

`app` never wrote a single line of `target_include_directories`, yet `-I.../include` is sitting right there in its compile command. That is the work PUBLIC usage requirements do quietly behind your back. The `fmt` path is absent, because `mylib` marked `fmt` as PRIVATE, and the encapsulation is airtight.

## Companion Example

The two-target project from this article builds directly out of the repository's example directory:

```text
code/examples/vol7/cmake-fundamentals/02-target/
├── CMakeLists.txt
├── fmt/
│   ├── fmt.h
│   └── fmt.cpp
├── include/mylib/mylib.h
├── src/mylib.cpp
└── main.cpp
```

Step into that directory and run the same three commands from the previous section to reproduce every line of output. To feel the PUBLIC/PRIVATE propagation firsthand, change `target_link_libraries(mylib PRIVATE fmt)` to PUBLIC, reconfigure, and run `cat build-mk/CMakeFiles/app.dir/flags.make | grep INCLUDES` again to see the `-I.../fmt` line `app` gained out of thin air.

That should land the target object, the `target_*` family of member methods, and the three-state PUBLIC/PRIVATE/INTERFACE usage requirements on solid ground. The next article tackles a more practical problem: in a real project `fmt` is not hand-written by us; you bring it in from the system or vcpkg/Conan with `find_package(fmt)`. We will see what the namespaced target like `fmt::fmt` that `find_package` hands back actually is, and how the PUBLIC/INTERFACE configuration on it flows automatically into your project. We will also circle back to a question left open here: for setting the C++ standard, is the directory-level form `set(CMAKE_CXX_STANDARD 17)` better, or the target-level form `target_compile_features(mylib PUBLIC cxx_std_17)`?
