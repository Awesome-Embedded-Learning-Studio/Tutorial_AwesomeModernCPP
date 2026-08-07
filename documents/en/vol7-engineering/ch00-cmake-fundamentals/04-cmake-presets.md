---
title: "CMakePresets.json: From the cmake -D Old Way to Reproducible --preset"
description: "A thorough walkthrough of how CMakePresets.json pins the old -D workflow into version control: configurePresets/buildPresets/testPresets, hidden + inherits composition, and per-user overrides via CMakeUserPresets.json"
chapter: 7
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 16
prerequisites:
  - "vol7 ch00 01: CMake 是什么——构建系统生成器的两段式流水线"
  - "vol7 ch00 02: Target 心智模型——把 target 当对象，PUBLIC/PRIVATE/INTERFACE 是使用需求"
related:
  - "交叉编译与 CMake"
  - "编译器选项"
---

# CMakePresets.json: From the cmake -D Old Way to Reproducible --preset

In the previous two pieces every configure command we typed looked the same: `cmake -B build -G Ninja`. In a real project that line is rarely that short. Once you add a build type, a toolchain file, and a few cache variables, the command balloons into something like this:

```text
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Once a command gets this long, things start going wrong. I have personally stepped on this: I copied `CMAKE_BUILD_TYPE` as `CMAKE_BUILD-TYPE`, configure did not error, silently produced an empty config, and the resulting binary shipped with a pile of debug symbols. Teammates kept asking each other "what did you put for the vcpkg path". In CI the command got embedded into YAML, and changing one option turned a PR red across the board. CMake 3.19 introduced `CMakePresets.json`, which folds all those `-D` flags, the generator choice, and the build directory scattered across the command line into one JSON file. The command then shrinks to a single `cmake --preset debug`. This piece covers how to use it, and how it hooks into the vcpkg toolchain and the VSCode CMake Tools extension.

## Why Presets: Four Pains of the -D Old Way

Before we touch the JSON, let's nail down why this is worth doing. Going back to the commands we used in the previous pieces, let's pick apart what's wrong with the -D approach one pain at a time.

First, the command is long and easy to mistype. That line above is over 130 characters spanning several lines. Get one letter wrong in `CMAKE_BUILD_TYPE` or `CMAKE_TOOLCHAIN_FILE` and CMake will not complain. It silently writes the unknown variable into the cache, and you end up with a build tree that "looks configured but actually set nothing". The problem usually only surfaces at runtime. I once burned half a day tracking down the `CMAKE_BUILD-TYPE` typo (underscore typed as a hyphen).

Second, it is not reproducible. The command lives only in your terminal history. Switch machines, open a new terminal window, or come back to this project two weeks later and the command is gone. You have to retype it from memory. Even if you remember roughly, the parameter order, whether a particular `-D` was on, you would not bet on any of it.

Third, the team ends up each typing their own. Same project, A uses `Release`, B uses `RelWithDebInfo`, C forgets to set `CMAKE_BUILD_TYPE` at all. Three machines produce three binaries with different behavior. A bug reproduces on B's machine, vanishes on A's, and the post-mortem shows build type mismatch. This kind of back-and-forth is nearly the norm in projects without conventions.

Fourth, it is hard to pin down in CI. The CI script has to copy the command verbatim into YAML, and every `-D` is a potential spelling trap. Changing one compile option means editing it in two places (local command + CI YAML), and over time they will inevitably drift.

The presets mechanism exists to take these four pains head on. Write "which `-D` flags, which generator, which build directory" into `CMakePresets.json`, check that file into version control, and the team and CI share one configuration. Locally you run `cmake --preset debug`, in CI you also run `cmake --preset debug`, the command is identical on both sides, and the build behavior is reproducible.

## CMakePresets.json Structure

The top level of `CMakePresets.json` has three categories of presets, one for each stage of the CMake workflow:

`configurePresets` corresponds to `cmake --preset`, and pins the configure-stage `-D` flags, the generator, and `binaryDir`. This is the most heavily used category.

`buildPresets` corresponds to `cmake --build --preset`, and pins build-stage arguments like `--target`, `--config`, and the parallelism. Added in schema version 2 (CMake 3.20).

`testPresets` corresponds to `ctest --preset`, and pins the test-stage filter, output format, and so on. Also introduced in schema version 2.

Let's look at a complete minimal working example first, then break the fields down. The `CMakePresets.json` below is the one I used while writing and verifying this piece: one hidden `base` preset sets the common fields, and two presets `debug` and `release` that inherit it each set `CMAKE_BUILD_TYPE`:

```json
{
    "version": 3,
    "cmakeMinimumRequired": {
        "major": 3,
        "minor": 21,
        "patch": 0
    },
    "configurePresets": [
        {
            "name": "base",
            "hidden": true,
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "cacheVariables": {
                "CMAKE_CXX_STANDARD": "17",
                "CMAKE_CXX_STANDARD_REQUIRED": "ON",
                "CMAKE_CXX_EXTENSIONS": "OFF"
            }
        },
        {
            "name": "debug",
            "displayName": "Debug (含 -g -O0)",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug"
            }
        },
        {
            "name": "release",
            "displayName": "Release (含 -O3 -DNDEBUG)",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "debug",
            "configurePreset": "debug"
        },
        {
            "name": "release",
            "configurePreset": "release"
        }
    ]
}
```

Field by field. The top-level `version` is **the JSON schema version**, not the CMake version. It currently goes up to 9 (introduced in CMake 3.27). 3 is a sensible floor: it covers the full basic capability of `configurePresets` + `buildPresets` + `testPresets` and is natively supported from CMake 3.21 on. The schema version and `cmakeMinimumRequired` are two different things: the former declares "which version of the schema this JSON was written against", the latter declares "how recent a CMake you need at minimum to run this JSON". A CMake older than that minimum refuses to touch `CMakePresets.json` outright, which keeps an old CMake from silently carrying on after failing to parse a new field.

`configurePresets` is an array, and each element is one preset. The `base` preset has a few key fields.

`name` is the unique identifier of the preset, and it is what follows `cmake --preset`.

`hidden: true` means this preset cannot be used directly by `--preset`, and it does not show up in the `--list-presets` output. It exists only as a base class for other presets to inherit. We will verify this in a moment with `cmake --preset base`, and CMake will refuse it on the spot.

`generator` and `binaryDir` pin down `-G` and `-B` respectively. Note that `binaryDir` is written as `${sourceDir}/build/${presetName}`, which involves two layers of macro expansion: `${sourceDir}` is the absolute path of the project root, and `${presetName}` is the name of the current preset (for example `debug` or `release`). The upside is that each preset lands in its own build directory, `build/debug` and `build/release` do not interfere, and switching build type does not require `rm -rf build` to start over.

`cacheVariables` is the pinned `-D`. Each `key: value` pair is equivalent to `-Dkey=value`. The value can be a string, a boolean, `null` (meaning the `UNINITIALIZED` type), or an object with a `type` field (for precise control over the cache variable type).

Next, how `debug` and `release` inherit from `base`. `inherits: "base"` means "this preset pulls in every field of `base` and overrides a portion of them itself". Here it overrides only `cacheVariables.CMAKE_BUILD_TYPE`: `debug` sets it to `Debug`, `release` to `Release`. The common fields on `base` like `generator`, `binaryDir`, and `CMAKE_CXX_STANDARD` are inherited as-is.

`inherits` accepts a single string or an array of strings. When the array case has multiple parent presets supplying the same field, **the one earlier in the array wins**. That is different from how C++ resolves multiple-inheritance ambiguity: CMake has a deterministic order here.

The `buildPresets` section is straightforward: each build preset is bound to a configure preset through its `configurePreset` field. `cmake --build --preset debug` then knows to run the build in the `binaryDir` of `build/debug`, without you writing `cmake --build build/debug` yourself.

::: details Which schema version should I pick?
The official documentation walks the schema version from 1 up to 9. Which one to pick depends on which new features you actually need. version 1 (CMake 3.19) has only `configurePresets`, no build/test presets; version 2 (3.20) adds `buildPresets`/`testPresets`; version 3 (3.21) brings the `cmakeMinimumRequired` field and more lenient macro expansion. Beyond that, the changes are mostly patches for advanced scenarios like CI integration and conditional includes. My default is 3: it covers the vast majority of project needs and guarantees parsing from CMake 3.21+ onward.
:::

## In Practice: Real Output From configure to build

Reading the JSON is not satisfying enough, so let's run it. The minimal project (the `CMakeLists.txt` + `main.cpp`) that pairs with this `CMakePresets.json` lives in the repo at `code/examples/vol7/cmake-fundamentals/04-presets/`. First, see which presets CMake recognizes:

```text
$ cmake --list-presets
Available configure presets:

  "debug"   - Debug (含 -g -O0)
  "release" - Release (含 -O3 -DNDEBUG)
```

`--list-presets` lists every non-hidden configure preset along with its `displayName`. Note that `base` does not show up, blocked by `hidden`. If you insist on `cmake --preset base`, CMake errors outright:

```text
$ cmake --preset base
CMake Error: Cannot use hidden configure preset in /tmp/cmake-presets-demo: "base"
```

That is exactly the semantics of a hidden preset: base class only, never used directly. The design keeps a teammate from accidentally reaching for a "half-configured" preset.

Run the `debug` preset:

```text
$ cmake --preset debug
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-presets-demo/build/debug
```

The last line is the key evidence: the build files landed in `build/debug`. The `${sourceDir}/build/${presetName}` macro expansion did its job. Run `release` next and the build directory is `build/release`; the two do not interfere:

```text
$ ls build/
debug  release
```

Now run the build through a build preset:

```text
$ cmake --build --preset debug
[1/2] Building CXX object CMakeFiles/app.dir/main.cpp.o
[2/2] Linking CXX executable app
```

`cmake --build --preset debug` is equivalent to `cmake --build build/debug`, but you do not have to remember what `binaryDir` looks like. The preset remembers it for you.

Just running it cleanly is not enough. Let's verify that `CMAKE_BUILD_TYPE` from `cacheVariables` actually flowed into the compile command. In `main.cpp` I dropped in an `#ifdef NDEBUG` to tell the two builds apart. First, look at the flags the `release` binary actually received, by digging into `build.ninja`:

```text
$ grep FLAGS build/release/build.ninja | head -2
  FLAGS = -O3 -DNDEBUG -std=c++17
  FLAGS = -O3 -DNDEBUG

$ grep FLAGS build/debug/build.ninja | head -2
  FLAGS = -g -std=c++17
  FLAGS = -g
```

`release` gets `-O3 -DNDEBUG`, `debug` gets `-g`, and `-std=c++17` shows up on both sides (from `CMAKE_CXX_STANDARD` on `base`). That nails down the causal chain between `CMAKE_BUILD_TYPE: Debug/Release` written in the preset and the actual compiler flags. The two binaries produce matching output when run:

```text
$ ./build/debug/app
debug build (NDEBUG NOT defined)

$ ./build/release/app
release build (NDEBUG defined)
```

One `CMakePresets.json`, two presets, two independent build trees, two binaries with different behavior, and the commands are as short as `cmake --preset debug` / `cmake --preset release`. Set that against the 130-plus-character -D command from earlier, and the gap is right there.

## CMakeUserPresets.json: Per-User Overrides

`CMakePresets.json` is shared by the team and goes into version control. But some things are inherently "machine-local": where vcpkg is installed, whether you have ASan on locally, or me wanting to add a temporary preset to experiment with some flag. Writing those into `CMakePresets.json` pollutes the team configuration. When someone else pulls, either the path is not found or some option that should not be on is suddenly on.

CMake's answer is `CMakeUserPresets.json`. It lives in the same directory as `CMakePresets.json`, has an identical structure, but its semantics are "personal override":

```text
project root/
├── CMakePresets.json        # in git, shared by the team
├── CMakeUserPresets.json    # in .gitignore, local only
├── CMakeLists.txt
└── ...
```

Presets defined in `CMakeUserPresets.json` are merged with those in the main file and shown together. Crucially, **a preset in UserPresets can inherit a hidden preset from the main file**. On my machine I added an `asan` preset that inherits `base` from the main file and layers an ASan flag on top:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "asan",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_FLAGS": "-fsanitize=address -fno-omit-frame-pointer"
            }
        }
    ]
}
```

Run `--list-presets` again:

```text
$ cmake --list-presets
Available configure presets:

  "asan"
  "debug"   - Debug (含 -g -O0)
  "release" - Release (含 -O3 -DNDEBUG)
```

`asan` shows up, on equal footing with `debug` and `release`. A direct `cmake --preset asan` runs cleanly, and the build directory lands at `build/asan` automatically:

```text
$ cmake --preset asan
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-presets-demo/build/asan
```

::: warning CMakeUserPresets.json must go into .gitignore
The official documentation says outright that it "should NOT be checked in". Its whole premise is "every machine has different paths", and once it goes into git, conflicts are guaranteed. The first thing to do when starting a new project is add `CMakeUserPresets.json` to `.gitignore`, before a colleague's PR shows up carrying their own vcpkg path to torment you.
:::

## IDE Integration: VSCode CMake Tools

Beyond the command line, the place presets really land is the IDE. The VSCode CMake Tools extension reads `CMakePresets.json` natively. The status bar lists the available configure presets and build presets, and clicking one switches, no command typing required.

clangd benefits indirectly too. Once CMake Tools has picked a preset, it runs the corresponding configure automatically, and the generated `compile_commands.json` gets picked up by clangd to power completion and jump-to-definition in the editor. Because the preset pins every `-D` and the generator, the compile environment the IDE sees is identical to the command line and to CI. That is the biggest advantage of presets over "the IDE maintaining its own configuration": a single source of truth.

The Remote-WSL case is just as smooth: `CMakePresets.json` travels into the WSL filesystem with the source, and the CMake Tools on the VSCode Remote side reads it directly. No need to configure it once on the Windows side and again on the WSL side.

## Hooking Up Cross-Compilation

By this point you can probably smell the natural fit between presets and cross-compilation. The heart of cross-compilation is the `-DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake` flag, plus a pile of target-board cache variables. Those are exactly what presets are best at pinning down.

`CMakePresets.json` has a dedicated `toolchainFile` field, cleaner than stuffing it into `cacheVariables`:

```json
{
    "name": "f407-debug",
    "inherits": "base",
    "toolchainFile": "${sourceDir}/cmake/arm-none-eabi-gcc.cmake",
    "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "ARM_CORTEX_M": "M4F"
    }
}
```

After that, a single `cmake --preset f407-debug` completes the cross-compilation configuration, and anyone who pulls the repo can reproduce the same toolchain setup. How this mechanism cooperates with `arm-none-eabi-g++`, the sysroot, and the cortex-m link script is something we expand on in detail in the vol7 cross-compilation piece.

## Companion Example

The project scaffold for this piece can be run straight from the example directory in the repo:

```text
code/examples/vol7/cmake-fundamentals/04-presets/
├── CMakeLists.txt
├── main.cpp
└── CMakePresets.json
```

Once you are in that directory, run `cmake --list-presets`, `cmake --preset debug`, `cmake --build --preset debug`, and `./build/debug/app` in order to reproduce every output in this piece. To verify the propagation of `cacheVariables`, change `debug` to `release`, rerun, and compare `FLAGS = -O3 -DNDEBUG` in `build/release/build.ninja` against `FLAGS = -g` in `build/debug/build.ninja`.

That covers the structure of presets, the hidden + inherits combination, per-user overrides via CMakeUserPresets.json, and IDE integration, all backed by real output that verifies the `${presetName}` macro expansion and the propagation of `CMAKE_BUILD_TYPE`. The next piece tackles a question vol7 has been carrying for a while: when the target board moves from x86 Linux to an ARM Cortex-M device like the STM32F407, how do you write `CMakeLists.txt`, what does the toolchain file look like, and how do presets hook into them? That is, the full cross-compilation pipeline.
