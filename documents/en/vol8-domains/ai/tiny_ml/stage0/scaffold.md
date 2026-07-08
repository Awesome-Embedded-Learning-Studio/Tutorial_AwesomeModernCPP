---
title: "Project scaffold — pour the toolchain foundation"
description: "Stand up a standalone CMake23 project, pull Catch2 via FetchContent, and get a smoke test passing. Stage 0 writes zero lines of inference code — it only confirms the toolchain + build + test pipeline is wired up."
chapter: 8
order: 6
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 8
prerequisites:
  - "CMake basics"
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
  - 工具链
  - 基础
---

# Project scaffold — pour the toolchain foundation

Stage 0 of TinyInferCpp-Lab does exactly one thing: stand up a standalone CMake project with a Catch2 smoke test that compiles and runs, plus a `.gitignore` that keeps build artifacts out. Not a single line of inference code — this stage only confirms the toolchain, the build system, and the test framework are wired up, so that every time you write code later, `cmake --build` gives you feedback. Companion project at `code/volumn_codes/vol8-labs/ai/tiny_ml/stage0/`.

## Why not just start writing inference code

Getting stuck in week one on "it won't compile / Catch2 won't pull / clangd isn't working" is what kills the project far more often than not knowing how to write some algorithm. Stage 0 clears those prerequisites up front, and forces you to confirm your toolchain actually supports C++23 right now — so you don't get to Stage 5 and discover your compiler is too old, when rolling back is expensive.

## What the project looks like

A standalone CMake project. The directory is just this:

```text
stage0/
├── CMakeLists.txt          # standalone, FetchContent Catch2 v3.5.0
├── .gitignore              # build/ + .cache/
├── tests/smoke.cpp         # toolchain smoke test
└── logs/                   # pitfall ledger (real evidence, see common pitfalls below)
```

## CMake skeleton

The actual `CMakeLists.txt` looks like this, broken down in functional order:

```cmake
cmake_minimum_required(VERSION 4.1)
project(tamcpp_mlinfra LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

The first three lines lock the standard. `CMAKE_CXX_STANDARD 23` with `STANDARD_REQUIRED ON` pins C++23 down so the compiler can't silently downgrade. Write `STANDARD` without `STANDARD_REQUIRED` and some compilers quietly drop to the highest standard they support — then you hit C++23 features later and get a baffling error you'd never trace back to the standard. `CMAKE_EXPORT_COMPILE_COMMANDS ON` makes CMake emit `compile_commands.json` for clangd; IDE jump-to-definition, completion, and diagnostics all depend on it. Leave it off and writing code hurts.

```cmake
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0
)

FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
```

This pulls Catch2 from Git at configure time and compiles it into the project via `add_subdirectory` — no preinstall, no submodule. The version is pinned by `GIT_TAG v3.5.0`.

Each of the three steps does its own job. `FetchContent_Declare` only registers the name, source, and tag — it doesn't trigger a download. `FetchContent_MakeAvailable` is what clones on first configure, runs `add_subdirectory`, and defines the `Catch2::Catch2WithMain` target, along with a `<depname>_SOURCE_DIR` variable you can reference (the rule is depname lowercased, hence `catch2_SOURCE_DIR`). The last `list(APPEND CMAKE_MODULE_PATH .../extras)` line puts Catch2's helper `.cmake` files (`Catch.cmake` etc., which provide `catch_discover_tests`) onto CMake's module search path, so `include(Catch)` can find them later.

Downloaded sources land in `build/_deps/catch2-src/`, intermediate products in `-build/`; a second configure doesn't re-pull, and your source tree stays clean. Real evidence for this mechanism — including how to rescue it when the pull fails — is in `logs/002-fetchcontent-catch2.md`.

```cmake
add_executable(smoke_catch2 tests/smoke.cpp)
target_link_libraries(smoke_catch2 PRIVATE Catch2::Catch2WithMain)

target_compile_options(smoke_catch2 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>
)
```

`target_compile_options` is modern CMake's target-based way: options hang off a specific target rather than polluting everything, unlike the old global `add_compile_options()`. `PRIVATE` means it only applies to compiling this target and doesn't propagate downstream. Warning flags are almost always PRIVATE — downstream doesn't care how you compile this, and warning flags are compiler-specific; make them `PUBLIC` and they explode the moment someone swaps compilers.

The last two generator expressions deserve a breakdown. Conclusion first: `-Wall -Wextra -Wpedantic -g` are all GCC/Clang dialect — MSVC recognizes none of them. MSVC has its own set: warning level via `/W4` (the practical high setting; `/Wall` spams so hard it's unusable), strict conformance via `/permissive-` (disables non-standard extensions), debug info via `/Zi` (into a PDB). So the project splits by compiler ID: MSVC takes one line, non-MSVC takes the other.

The second line is deliberately written as `$<NOT:$<CXX_COMPILER_ID:MSVC>>` rather than enumerating `$<CXX_COMPILER_ID:GNU,Clang>`. The former means "anything that isn't MSVC takes the GCC/Clang set", automatically covering Intel, LLVM, and whatever else understands `-Wall`, without you having to edit the list every time a new compiler shows up. The full flag comparison is in `logs/003-target-compile-options.md`.

## Why C++23 and not C++20

Stage 0 itself doesn't depend on any C++23 feature — you could run this stage on C++20. But from Stage 1 on you'll want `consteval`, fuller `constexpr`, and `std::expected`, so set the standard to 23 now and save yourself a backport later.

## Smoke test: pick a meaner probe

```cpp
#include <catch2/catch_test_macros.hpp>
#include <print>

TEST_CASE("Smoke up the labs") {
    std::print("Our smoke Test");
}
```

The point of a smoke test is to prove the chain works, so picking a meaner probe pays off. Using `std::print` here is deliberate — it needs the C++23 `<print>` header to be reachable, so a single line simultaneously stresses the toolchain, Catch2, CMake, and the C++23 standard library. It probes deeper than an honest `REQUIRE(1 + 1 == 2)`.

## Verification

```bash
cmake -S . -B build           # first-time FetchContent pulls Catch2, needs network
cmake --build build -j
./build/smoke_catch2
```

Step 3 prints `Our smoke Test` and reports `All tests passed`. Then manually confirm in the IDE that clangd can jump into `smoke.cpp` and autocomplete Catch2 macros — that's how you know `compile_commands.json` actually took effect.

## Common pitfalls

::: warning FetchContent can't pull Catch2
WSL's access to GitHub is flaky; the failure log usually reads `Failed to connect to github.com port 443`. The rescue is a manual shallow clone, swapped in as a pre-placed directory to replace the automatic download:

```bash
git clone --depth 1 -b v3.5.0 https://github.com/catchorg/Catch2.git /tmp/catch2
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_CATCH2=/tmp/catch2
```

The variable name rule is `FETCHCONTENT_SOURCE_DIR_<UPPER_DEPNAME>` — it essentially replaces the `GIT_REPOSITORY` download source. Evidence in `logs/002`.
:::

::: warning Compiler doesn't support C++23
`set(CMAKE_CXX_STANDARD 23)` needs GCC 13+ / Clang 16+ / MSVC VS2022 17.6+. Self-test before you start work:

```bash
g++ --version
echo 'int main(){return 0;}' | g++ -std=c++23 -x c++ - -o /tmp/cxx23_smoke && echo "C++23 OK"
```

Locally tested with g++ 16.1.1 and clang++ 22.1.6 (see `logs/003`). If it errors, upgrade the compiler — don't downgrade the standard; from Stage 1 on, the lower standard won't hold up.
:::

::: warning clangd reports "header not found"
Nine times out of ten `compile_commands.json` wasn't generated, or clangd isn't pointed at `build/`. Confirm `CMAKE_EXPORT_COMPILE_COMMANDS ON` is set. `.cache/clangd` is clangd's index storage — don't delete it by accident and don't commit it; it's already in `.gitignore`.
:::

::: warning Running cmake at the repo root
Don't run cmake at the repo root. This project builds inside its own `stage0/` directory; `build/` artifacts stay right there and are already `.gitignore`d.
:::
