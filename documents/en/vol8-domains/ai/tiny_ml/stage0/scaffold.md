---
title: "Project scaffold — pour the toolchain foundation"
description: "Stand up a standalone CMake23 project, pull Catch2 with FetchContent, and get a smoke test passing. Stage 0 writes not a single line of inference code — it only confirms that the toolchain + build + test foundation is wired up"
chapter: 8
order: 6
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 8
prerequisites:
  - "CMake Fundamentals"
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
  - 工具链
  - 基础
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage0/scaffold.md
  source_hash: acb2721f3283507f790d74021f4870b862a60ccb9c6cc5855c860274436cb656
  translated_at: '2026-09-26T03:50:25+00:00'
  engine: anthropic
  token_count: 2900
---

# Project scaffold — pour the toolchain foundation

With any project, the first order of business is putting up the scaffolding — no rushing straight into hands-on work. Stage 0 of TinyInferCpp-Lab does exactly one thing: stand up a standalone CMake project containing a Catch2 smoke test that compiles and runs, with `.gitignore` keeping build artifacts out. Not a single line of inference code gets written — this stage only confirms that the toolchain + build + test foundation actually holds, so that every time you finish writing code from here on, `cmake --build` hands you feedback. The companion project lives in `code/volumn_codes/vol8-labs/ai/tiny_ml/stage0/`.

## Why not just write the inference code directly

Getting stuck in week one on "won't compile / Catch2 won't pull down / clangd doesn't work" drives people to abandon the whole thing far more surely than some algorithm they can't figure out. Stage 0 clears these obstacles up front, and as a bonus it forces you to confirm right now that your toolchain supports C++23 — better than discovering at Stage 5 that your compiler version falls short, when the cost of backing out is far higher.

## What the project looks like

A standalone CMake project — the directory holds just these few things:

```text
stage0/
├── CMakeLists.txt          # standalone, FetchContent Catch2 v3.5.0
├── .gitignore              # build/ + .cache/
├── tests/smoke.cpp         # toolchain smoke test
└── logs/                   # pitfall ledger (evidence; see common pitfalls below)
```

## The CMake skeleton

The actual `CMakeLists.txt` looks like this; let's take it apart in functional order:

```cmake
cmake_minimum_required(VERSION 3.20)
project(tamcpp_mlinfra LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

The opening `set()` calls lock down the standard first. `CMAKE_CXX_STANDARD 23` paired with `STANDARD_REQUIRED ON` nails C++23 in place so the compiler can't quietly downgrade. Set `STANDARD` without `STANDARD_REQUIRED` and some compilers silently fall back to the highest standard they support; later, when you use a C++23 feature, you get a baffling error that nobody would think to trace back to the standard. `CMAKE_EXPORT_COMPILE_COMMANDS ON` generates `compile_commands.json` for clangd — the IDE's go-to-definition, completion, and diagnostics all live off it. Leave this line off and writing code becomes genuinely painful.

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

This section pulls Catch2 from Git at configure time and compiles it into the project directly through `add_subdirectory` — built as part of the same build, no reliance on a preinstalled copy or a submodule — with the version pinned hard by `GIT_TAG v3.5.0`.

Each of the three steps does its own job. `FetchContent_Declare` only registers the name, the source, and the tag — it triggers no download. `FetchContent_MakeAvailable` is what, on first configure, clones, runs `add_subdirectory`, and defines the `Catch2::Catch2WithMain` target; it also sets a `<depname>_SOURCE_DIR` variable for you to reference, with the naming rule being the depname in all lowercase — hence `catch2_SOURCE_DIR` here. The final line, `list(APPEND CMAKE_MODULE_PATH .../extras)`, hooks Catch2's bundled helper `.cmake` files (`Catch.cmake` and friends, home of `catch_discover_tests`) into CMake's module search path, so a later `include(Catch)` can actually find them.

The downloaded source lands in `build/_deps/catch2-src/`, intermediate artifacts in the matching `-build/` directory; a second configure doesn't re-pull anything, and the source tree stays spotless. The evidence for this mechanism — including how to rescue a pull that won't go through — is recorded in `logs/002-fetchcontent-catch2.md`.

```cmake
add_executable(smoke_catch2 tests/smoke.cpp)
target_link_libraries(smoke_catch2 PRIVATE Catch2::Catch2WithMain)

target_compile_options(smoke_catch2 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>
)
```

`target_compile_options` is modern CMake's target-based way of doing things: the options hang off one specific target instead of polluting every target, unlike the old-school global `add_compile_options()`. `PRIVATE` means it applies only to compiling this target and is not passed down. Warning flags should almost always be PRIVATE — downstream consumers don't care how you compile your code, and warning flags are tightly coupled to the compiler: make them PUBLIC and pass them down, and the first compiler switch blows up immediately.

The two generator expressions at the end deserve an unpacking. Conclusion up front: `-Wall -Wextra -Wpedantic -g` are all private GCC/Clang dialect — MSVC doesn't recognize a single one. MSVC has its own set: warning level via `/W4` (the highest practical setting; `/Wall` floods the output beyond usability), strict standards conformance via `/permissive-` (turns off non-standard extensions), and debug info via `/Zi` (written into the PDB). So the project hangs them separately by compiler ID: MSVC takes one branch, non-MSVC takes the other.

The second line is deliberately written as `$<NOT:$<CXX_COMPILER_ID:MSVC>>` instead of enumerating `$<CXX_COMPILER_ID:GNU,Clang>`. The former means anything that isn't MSVC takes the GCC/Clang set — automatically covering Intel, LLVM, and other compilers that equally understand `-Wall`, with no list to edit every time a new compiler is added. The full flag-by-flag comparison is in `logs/003-target-compile-options.md`.

## Why C++23 and not C++20

Stage 0 itself depends on no C++23 feature — you could get this stage running on C++20 too. But from Stage 1 onward we'll need `consteval`, more complete `constexpr`, and `std::expected`, so we set the standard to 23 now and spare ourselves a retroactive change later.

## Smoke test: pick a sharper probe

```cpp
#include <catch2/catch_test_macros.hpp>
#include <print>

TEST_CASE("Smoke up the labs") {
    std::print("Our smoke Test");
}
```

A smoke test's whole purpose is proving the chain is connected, so it pays to pick a sharper probe. `std::print` is used here deliberately: it requires the C++23 `<print>` header to be reachable, so one single line puts four things under load at once — toolchain, Catch2, CMake, and the C++23 standard library — probing far deeper than a dutiful `REQUIRE(1 + 1 == 2)` ever would.

## Verification

```bash
cmake -S . -B build           # first configure: FetchContent pulls Catch2, network needed
cmake --build build -j
./build/smoke_catch2
```

Step 3 prints `Our smoke Test` and reports `All tests passed`. Then confirm by hand: if clangd in the IDE can jump into `smoke.cpp` and complete Catch2's macros, `compile_commands.json` is genuinely in effect.

## Common pitfalls

::: warning FetchContent can't pull Catch2
WSL's access to GitHub is unstable; the failure log typically reads `Failed to connect to github.com port 443`. The emergency fix is to shallow-clone a copy by hand and substitute that pre-placed directory for the automatic download:

```bash
git clone --depth 1 -b v3.5.0 https://github.com/catchorg/Catch2.git /tmp/catch2
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_CATCH2=/tmp/catch2
```

The variable naming rule is `FETCHCONTENT_SOURCE_DIR_<UPPERCASE_DEPNAME>` — in essence it swaps out the download source declared by `GIT_REPOSITORY`. Evidence in `logs/002`.
:::

::: warning Compiler doesn't support C++23
`set(CMAKE_CXX_STANDARD 23)` requires GCC 13+ / Clang 16+ / MSVC VS2022 17.6+. Run a self-check before starting work:

```bash
g++ --version
echo 'int main(){return 0;}' | g++ -std=c++23 -x c++ - -o /tmp/cxx23_smoke && echo "C++23 OK"
```

Measured locally: g++ 16.1.1 and clang++ 22.1.6 (see `logs/003`). If the check errors out, upgrade the compiler — don't lower the standard; from Stage 1 onward, a lowered standard won't hold up.
:::

::: warning clangd reports "header not found"
Nine times out of ten, `compile_commands.json` wasn't generated, or clangd isn't pointed at `build/`. Confirm `CMAKE_EXPORT_COMPILE_COMMANDS ON` is configured. `.cache/clangd` is where clangd builds its index — don't delete it by mistake and don't commit it; it's already in `.gitignore`.
:::

::: warning Running cmake at the repository root
Don't run cmake at the repository root. This project builds tucked inside its own `stage0/` directory: `build/` artifacts stay right there, and they're already ignored by `.gitignore`.
:::
