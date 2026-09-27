---
title: "The target mental model — treat a target as an object, PUBLIC/PRIVATE/INTERFACE are usage requirements"
description: "What a target really is, why the target_* commands are member methods, how the PUBLIC/PRIVATE/INTERFACE three states propagate, and why directory-level commands are an anti-pattern"
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
  - "What is CMake — the two-stage pipeline of a build system generator"
related:
  - "Cross-compilation and a Simple Guide to CMake"
  - "Guide to Common Compiler Options"
translation:
  source: documents/vol7-engineering/ch00-cmake-fundamentals/02-target-and-usage-requirements.md
  source_hash: 8d5e2ea8cdf1f06ad95fd4e822ff4fc07492411d7122089b2a7d3464cf41c3a4
  translated_at: '2026-09-26T04:46:03+00:00'
  engine: anthropic
  token_count: 12000
---

# The target mental model — treat a target as an object, PUBLIC/PRIVATE/INTERFACE are usage requirements

In the previous article we got a minimal project running; `CMakeLists.txt` contained exactly one line of real work — `add_executable(hello main.cpp)`. We never gave a name to the thing that one line created. This article hands that name over: **target**.

The word target shows up over and over in the official CMake docs, gets crowned the number-one concept in every "modern CMake" tutorial, and the community even has a catchphrase for it: think in targets, not variables. Why does modern CMake lift it so high, and why is the `include_directories()` you copied out of an old tutorial already an anti-pattern? This article works it through to the bottom. This is the watershed between modern CMake and old-style CMake — cross it, and no `CMakeLists.txt` you read afterwards will feel like reciting incantations.

## Treat a target as an object

A target is not some abstract metaphor; it is, quite literally, a data structure CMake keeps internally. The fastest way to understand it is to think of it as a C++ object.

`add_executable(app main.cpp)` and `add_library(mylib STATIC src/mylib.cpp)` are **constructors**. They create a target object, name it `app` or `mylib`, and record which source files it is built from and whether it should compile into an executable or a library. From that line on, the names `app` and `mylib` are "alive" in CMake's world, and every piece of configuration you write afterwards operates on that name as a handle.

And once the object exists? You give it header search paths, tell it which library to link, switch on compile options. Those operations correspond to a family of commands that all start with `target_*`:

```cmake
target_include_directories(mylib PUBLIC include)
target_link_libraries(mylib PRIVATE fmt)
target_compile_options(mylib PRIVATE -Wall -Wextra)
target_compile_features(mylib PUBLIC cxx_std_17)
```

These `target_*` commands are **member methods**. What they do is essentially the same thing: take a target's name and hang a property on that target object. `target_include_directories(mylib PUBLIC include)` translates to "take the object `mylib` and push `include` into its include-path property."

The things hanging off a target (include paths, the linked-library list, compile options, the C++ standard requirement) are its **member variables**. Each target manages its own and leaves the others alone.

::: details What a target actually is inside CMake
Strictly speaking, a target is a collection of properties maintained by CMake. You can pull the properties off it during the configure stage with `get_target_property(v mylib INCLUDE_DIRECTORIES)`. The hands-on section later in this article uses exactly this command to crack the target open for us to see — internally, a target is not a black box.
:::

Why does this "object thinking" matter? Because it pins down the scope of any configuration for good. `target_include_directories(mylib PUBLIC include)` touches the properties of exactly one target, `mylib`, and nothing else in the project. Which is precisely the core distinction coming next: old-style CMake is "global pollution"; modern CMake is "target-private."

## Usage requirements: the PUBLIC/PRIVATE/INTERFACE three states

The target object alone is not enough. What truly lets modern CMake be reborn is how it models **usage requirements**. The phrase sounds mystical, but it boils down to one sentence: the configuration a target requires when compiling itself may differ from what it requires when someone else links against it. CMake separates the two cases with three keywords.

PRIVATE means "I need it to compile myself, but whoever links me does not." For example, `mylib` internally calls the third-party library `fmt` for string formatting, yet `fmt` leaves no trace in `mylib`'s public header. Whoever links `mylib` downstream has no idea `fmt` exists, and naturally needs none of `fmt`'s include paths. In that case `fmt` is PRIVATE to `mylib`.

INTERFACE means "I don't need it myself, but whoever links me does." A typical case is a header-only library: it has no `.cpp` of its own to compile, so the "self-use" half is empty; but the moment downstream includes its headers, the corresponding include paths and C++ standard requirements must be there. In that case all of the configuration goes into INTERFACE.

PUBLIC means "both: I use it, and whoever links me needs it too." The most common case is a type that appears directly in the public header. Say the return type in `mylib.h` is `std::string`: once downstream links `mylib`, the compiler must be able to find the include path where `<string>` lives in order to parse that return type. `mylib` itself needs that path when compiling its `.cpp`, and downstream needs it when linking `mylib`. That is PUBLIC.

Split these three states along "self-use / others-use" and underneath sits a simple truth table:

| Keyword | Used when compiling itself | Also used when others link |
|--------|:---:|:---:|
| PRIVATE | Yes | No |
| INTERFACE | No | Yes |
| PUBLIC | Yes | Yes |

Memorize this table — it fits every `target_*` command you will ever read.

### A concrete example: fmt is PRIVATE, <string> is INTERFACE

Definitions alone are not enough; let's drop down to code. The project below has three targets: a minimal `fmt` (standing in for a third-party formatting library), a `mylib` static library exposed to the outside, and a downstream `app` executable. `mylib`'s implementation uses `fmt::format` internally, but its public header uses nothing but `std::string`.

`mylib`'s public header, `include/mylib/mylib.h`:

```cpp
#pragma once
#include <string>

namespace mylib {

/// @brief Format a greeting into a prefixed string
/// @note  The return type is std::string — part of mylib's public API,
///        so the downstream app must also see the full std::string definition,
///        which makes the include path for <string> an INTERFACE requirement
std::string make_greeting(const std::string& name);

}  // namespace mylib
```

`mylib`'s implementation, `src/mylib.cpp`:

```cpp
#include "mylib/mylib.h"

#include "fmt.h"

namespace mylib {

std::string make_greeting(const std::string& name) {
    // fmt is an internal implementation detail of mylib; the public header mylib.h shows no trace of it
    // so downstream never needs to know fmt exists — exactly why fmt should be PRIVATE
    return fmt::format("hello, {}!", name);
}

}  // namespace mylib
```

The three key lines in `CMakeLists.txt` that hang properties on `mylib`:

```cmake
add_library(mylib STATIC src/mylib.cpp)
target_include_directories(mylib PUBLIC include)
target_link_libraries(mylib PRIVATE fmt)
```

`include` is written PUBLIC: `mylib` itself has to find `mylib/mylib.h` when compiling its `.cpp` (self-use), and downstream has to find `mylib/mylib.h` to include it after linking `mylib` (others-use). Both halves hold, so it is PUBLIC.

`fmt` is written PRIVATE: `mylib.cpp` calls `fmt::format` internally (self-use), but `mylib.h` carries not a single `fmt` symbol, so downstream never needs to see `fmt.h` (not others-use). Hence PRIVATE.

### Flip PRIVATE to PUBLIC and watch downstream get "infected"

The worst way to teach a concept is in a vacuum. Let's get our hands dirty: change `fmt` from PRIVATE to PUBLIC and see what happens to the downstream `app`.

First, configure the project (with the Make generator, because its `flags.make` file lists the include paths each target actually receives in plain, readable form; Ninja, to support C++ modules, splits the flags off into other files that are painful to read by eye):

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

Right now `mylib` declares `fmt` PRIVATE. Look at the include flags CMake generates for each of the three targets:

```text
$ cat build/CMakeFiles/mylib.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include -I/tmp/cmake-target-demo/fmt

$ cat build/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include
```

Read it word by word. `mylib` gets two paths: its own `include` (PUBLIC) plus `fmt` (PRIVATE — also needed while compiling itself). `app` gets only one path, `include`: it links only `mylib`, so it inherits `mylib`'s PUBLIC portion (which is `include`), while `fmt`, being `mylib`'s PRIVATE, never crosses over. `app` knows nothing about `fmt` — exactly the encapsulation we wanted.

What happens if `app`'s `main.cpp` sneaks in an `#include "fmt.h"` at this point? The compiler cannot find the header and dies on the spot. We actually tried it:

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

Now touch one line — change `target_link_libraries(mylib PRIVATE fmt)` to `target_link_libraries(mylib PUBLIC fmt)`, reconfigure, and look at `app`'s include flags again:

```text
$ sed -i 's/target_link_libraries(mylib PRIVATE fmt)/target_link_libraries(mylib PUBLIC fmt)/' CMakeLists.txt
$ cmake -S . -B build -G "Unix Makefiles" > /dev/null
$ cat build/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include -I/tmp/cmake-target-demo/fmt
```

`app` itself changed nothing, yet purely because upstream `mylib` flipped `fmt` from PRIVATE to PUBLIC, `app` gained a `-I.../fmt` out of thin air. Now `app` doesn't need its own `find_package(fmt)`, doesn't need its own `target_link_libraries(app PRIVATE fmt)` — a bare `#include "fmt.h"` just compiles.

This is the **propagation** of usage requirements: PUBLIC lets configuration seep downstream along the link graph, while PRIVATE seals it inside the target. This "automatic propagation" is the root reason modern CMake can express such complex dependency relationships so cleanly. As long as you correctly mark each dependency public or private, one link is all downstream needs to automatically pick up every bit of configuration it should get.

::: warning Don't use PUBLIC as a universal patch
Reading this far you might be tempted: since PUBLIC hands downstream the configuration automatically, why not mark every dependency PUBLIC and save the effort? Please don't. PUBLIC amounts to leaking internal implementation details downstream. The moment downstream starts depending on the `fmt` path you exposed, the day you swap `fmt` for `std::format`, or upgrade and the path changes, downstream blows up with you. Encapsulation is slack you leave for the future — the more PUBLIC you use, the less room you leave yourself to refactor. The rule: if PRIVATE will do, don't use PUBLIC.
:::

### What the LINK_ONLY in INTERFACE_LINK_LIBRARIES actually is

There is a detail here worth unfolding. We dug through `mylib`'s internal properties with `get_target_property` (with `fmt` configured as PRIVATE):

```text
mylib.INCLUDE_DIRECTORIES         = /tmp/cmake-target-demo/include
mylib.INTERFACE_INCLUDE_DIRECTORIES = /tmp/cmake-target-demo/include
mylib.LINK_LIBRARIES              = fmt
mylib.INTERFACE_LINK_LIBRARIES    = $<LINK_ONLY:fmt>
```

Notice the last line. Isn't PRIVATE supposed to mean "downstream has no idea fmt exists"? Then why does `fmt` show up in `INTERFACE_LINK_LIBRARIES`?

There is a subtle but sensible distinction at work: PRIVATE encapsulates the **include path** (downstream does not need `fmt.h` at compile time), but the **link relationship** cannot be sealed away. `mylib` is a static library; its `.o` files reference `fmt::format`'s symbols, and when the linker finally turns `app` into an executable, it must be able to find `libfmt.a` to fill those symbols in — otherwise the linker throws `undefined reference`. So CMake uses the generator expression `$<LINK_ONLY:fmt>` to say "fmt participates in linking only, not in compilation, for downstream." This explains why you do not see `-I.../fmt` in `app`'s `flags.make` (the include path did not cross over), yet `app` still links into a working executable just fine (the link relationship did cross over). PUBLIC/PRIVATE controls the propagation of configuration, not the link graph itself.

## Why directory-level commands are an anti-pattern

Once target privacy makes sense, look back at old-style CMake and it becomes obvious why the modern CMake crowd boycotts these commands unanimously.

Old-style CMake uses directory-level, global commands:

```cmake
# Old-style CMake; one sighting in a modern project should trigger a refactor
include_directories(include)
include_directories(fmt)
add_definitions(-DUSE_FMT)
add_compile_options(-Wall)
```

The semantics of `include_directories(include)` are "every target in the current `CMakeLists.txt`'s directory and its subdirectories gets `-Iinclude`, bar none." `add_definitions(-DUSE_FMT)` works the same way: the `-DUSE_FMT` macro gets defined on every target.

In a small project you cannot see the flaw; scale up and it collapses. Picture a project with four or five targets — `mylib`, `tests`, `benchmarks`, `tools`. You write `add_compile_options(-Wall -Wextra -Werror)` in the top-level `CMakeLists.txt`, intending strict warnings for the main library, and the pile of third-party Catch2 test code under the `tests` subdirectory inherits `-Werror` too — the build goes red all over. Then you are down in `tests/CMakeLists.txt` figuring out how to turn `-Werror` off again, memorizing a heap of workaround incantations.

Or picture this: `mylib` uses `fmt` internally, and to save effort you write `include_directories(fmt)` at the top level. Now `tools` — a target that was never supposed to know `fmt` exists — picks up `-Ifmt` too; the day its source code accidentally `#include "fmt.h"`, it still compiles, and the encapsulation has been quietly torn open. When a maintainer later wants to swap `fmt` out, they have no way to tell which targets use `fmt` on purpose and which just got stained by a global command.

Modern CMake solves both problems with target-level commands. `target_include_directories(mylib PRIVATE fmt)` locks `fmt`'s path dead inside the `mylib` target — it leaks neither to `tools` nor to the downstream `app` (because PRIVATE). Every target carries its own configuration boundary; whoever owns a dependency declares it, and the dependency graph stays legible and traceable.

Side by side:

```cmake
# Old style (directory-level, global pollution)
include_directories(include)
add_definitions(-DMYLIB_EXPORTS)

# Modern (target-level, clean boundaries)
target_include_directories(mylib PUBLIC include)
target_compile_definitions(mylib PRIVATE MYLIB_EXPORTS)
```

The migration rule is just as plain: swap every `include_directories()` for `target_include_directories()`, every `add_definitions()` for `target_compile_definitions()`, every `add_compile_options()` for `target_compile_options()`, and put a concrete target's name in front of each command. That is the cheapest single step for dragging an old project into modern CMake.

::: details Can you still set the C++ standard with a variable at the top level
You will see plenty of `CMakeLists.txt` files writing `set(CMAKE_CXX_STANDARD 17)` at the top. That is also a directory-level (global) setting: it assigns the `CXX_STANDARD` property to every target under the current directory. This usage is still acceptable today, because for the vast majority of projects the C++ standard genuinely is a project-wide global property. But the more modern, more precise form is `target_compile_features(mylib PUBLIC cxx_std_17)`, which turns the C++ standard into a target usage requirement too — downstream links `mylib` and automatically inherits the C++17 requirement. The next article, on `find_package`, comes back to compare the two forms.
:::

## Hands-on: tearing down a two-target project

Let's assemble everything from above. We will use a complete, runnable project to demonstrate the two-target setup — a `mylib` static library plus an `app` executable — and watch how PUBLIC/PRIVATE actually flows in a real build. The full project lives at `code/examples/vol7/cmake-fundamentals/02-target/`, laid out like this:

```text
02-target/
├── CMakeLists.txt
├── fmt/
│   ├── fmt.h          # minimal stand-in for a third-party library
│   └── fmt.cpp
├── include/
│   └── mylib/
│       └── mylib.h    # mylib's public header
├── src/
│   └── mylib.cpp      # mylib implementation
└── main.cpp           # app executable
```

The complete `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(target_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# fmt: a minimal "third-party library" used only inside this project; a real project would use find_package(fmt REQUIRED)
add_library(fmt STATIC fmt/fmt.cpp)
target_include_directories(fmt PUBLIC fmt)

# mylib: the library exposed outward; its public header include/mylib/mylib.h uses std::string
add_library(mylib STATIC src/mylib.cpp)
target_include_directories(mylib PUBLIC include)
# fmt is PRIVATE here — mylib.cpp needs it internally, but mylib.h exposes no fmt at all
target_link_libraries(mylib PRIVATE fmt)

# app: the downstream executable; links only mylib and knows nothing of fmt
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mylib)
```

Read this config bottom-up and the intent comes into focus. `app` declares only "I link `mylib`" — nothing else. `mylib` exposes its own `include` directory as PUBLIC, so downstream gets that path automatically when linking; it locks `fmt` into PRIVATE, so downstream had better not find out `fmt` is in use. `fmt` itself exists as a STATIC library in its own right, with the include path as its own PUBLIC (so `mylib` picks up the `fmt.h` path when linking it).

Three commands bring the project up:

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

The order of those six steps reveals the dependency graph. `fmt` builds first (steps 1-2, it depends on nobody), `mylib` next (steps 3-4, it depends on `fmt`), and `app` last (steps 5-6, it depends on `mylib`). Ninja orders everything by dependency automatically; you don't lift a finger.

That last line, `hello, world!`, is `app` running. Its `main.cpp` does nothing more than `#include "mylib/mylib.h"`, yet the compiler finds the header — thanks to `mylib` marking `include` PUBLIC, the downstream `app` inherited `-I.../include` automatically when linking `mylib`.

If we want proof that this inheritance is really at work, the most direct route is to look at the include flags `app` actually received. Configure once more with the Make generator and read `app.dir/flags.make`:

```text
$ cmake -S . -B build-mk -G "Unix Makefiles" > /dev/null
$ cat build-mk/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include
```

`app` never wrote a single line of `target_include_directories` itself, yet there it is in its compile command: `-I.../include`. That is PUBLIC usage requirements quietly doing the work behind your back. The `fmt` path is nowhere to be seen, because `mylib` marked `fmt` PRIVATE — the encapsulation is airtight.

## Companion example

This article's two-target project builds straight out of the repository's example directory:

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

Step into that directory and rerun the three commands from the previous section to reproduce every line of output. To feel the PUBLIC/PRIVATE propagation with your own hands, change `target_link_libraries(mylib PRIVATE fmt)` to PUBLIC, reconfigure, then run `cat build-mk/CMakeFiles/app.dir/flags.make | grep INCLUDES` and watch the `-I.../fmt` line `app` gained out of thin air.

By now the target object, the `target_*` family of member methods, and the three-state PUBLIC/PRIVATE/INTERFACE usage requirements should all have landed on solid ground. The next article takes on a more practical problem: in a real project `fmt` is not something we hand-write — you bring the third-party library in from the system or from vcpkg/Conan with `find_package(fmt)`. What exactly is the namespaced target like `fmt::fmt` that `find_package` hands back, and how does the PUBLIC/INTERFACE configuration hanging off it flow automatically into your project? It also returns to a question we left open: for setting the C++ standard, is the directory-level form `set(CMAKE_CXX_STANDARD 17)` better, or the target-level form `target_compile_features(mylib PUBLIC cxx_std_17)`?
