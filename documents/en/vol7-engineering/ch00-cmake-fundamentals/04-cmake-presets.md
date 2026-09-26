---
title: "CMakePresets.json — from the old cmake -D way to reproducible --preset builds"
description: "How CMakePresets.json pins the old -D style into version control: the three preset categories configurePresets/buildPresets/testPresets, the hidden + inherits combination, and personal overrides via CMakeUserPresets.json"
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
  - "vol7 ch00 01: What is CMake — the two-stage pipeline of a build system generator"
  - "vol7 ch00 02: The target mental model — treat a target as an object, PUBLIC/PRIVATE/INTERFACE are usage requirements"
related:
  - "Cross-compilation and a Simple Guide to CMake"
  - "Guide to Common Compiler Options"
translation:
  source: documents/vol7-engineering/ch00-cmake-fundamentals/04-cmake-presets.md
  source_hash: 722591fb811111c2aee15968bc0e284dda67a28d6bac396140e030586ab84bff
  translated_at: '2026-09-26T05:12:48+00:00'
  engine: anthropic
  token_count: 4000
---

# CMakePresets.json — from the old cmake -D way to reproducible --preset builds

In the previous two articles, every configure command we typed looked the same: `cmake -B build -G Ninja`. In a real project that line is usually far from that short. Once you add a build type, a toolchain file, and a few cache variables, the command balloons into something like this:

```text
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Once a command gets this long, problems start. We have stepped on this one ourselves: we copied `CMAKE_BUILD_TYPE` as `CMAKE_BUILD-TYPE`, configure did not report an error, silently ran an empty configuration, and the binary that came out carried a pile of debug symbols; colleagues kept asking each other "what did you fill in for the vcpkg path"; in CI the command got embedded into YAML, and changing one option turned a whole PR red. CMake 3.19 introduced `CMakePresets.json`, which consolidates the `-D` flags, the generator choice, and the build directory scattered across the command line into a single JSON file, and the command finally slims down to one line: `cmake --preset debug`. This article covers how to use it, and how it hooks up with the vcpkg toolchain and VSCode CMake Tools.

## Why presets: four pain points of the old -D way

Before we start writing JSON, let's nail down why this is worth doing. Referring back to the commands we used in the previous articles, let's pick the old -D style apart pain point by pain point.

First, the command is long and easy to mistype. The one above is over 130 characters and spans multiple lines. If a single letter is wrong in key names like `CMAKE_BUILD_TYPE` or `CMAKE_TOOLCHAIN_FILE`, CMake will not complain — it silently writes the unrecognized variable into the cache, and what you end up with is a build tree that "looks configured but actually set nothing". The problem usually does not surface until runtime. We once burned half a day tracking down exactly this `CMAKE_BUILD-TYPE` typo (underscore typed as a hyphen).

Second, it is not reproducible. The command lives only in your terminal history. Switch machines, switch to a new terminal window, or come back to this project half a month later, and the command is long gone — you can only retype it from memory. Even if you remember the rough shape, nobody would vouch for the parameter order or for whether a particular `-D` was actually set.

Third, everyone on the team types their own. Same project: A uses `Release`, B uses `RelWithDebInfo`, C forgot to specify `CMAKE_BUILD_TYPE`, and three machines produce three binaries with different behavior. A bug reproduces on B's machine and vanishes on A's; the post-mortem traces it back to inconsistent build types — this kind of wrangling is nearly the norm in projects without conventions.

Fourth, it is hard to pin down in CI. The CI script has to copy the command verbatim into YAML, and every `-D` is a potential spelling trap. Changing one compile option means editing two places in sync (the local command + the CI YAML), and over time they will inevitably drift.

The presets mechanism exists to go after these four pain points. Write "which `-D` flags, which generator, which build directory" into `CMakePresets.json`, put that file under version control, and the team and CI share one configuration. Locally you type `cmake --preset debug`; in CI it is also `cmake --preset debug`. The command is identical on both sides, and the build behavior is reproducible.

## The structure of CMakePresets.json

The top level of `CMakePresets.json` defines three categories of presets, matching the three stages of the CMake workflow:

`configurePresets` corresponds to `cmake --preset` and pins the configure-stage `-D` flags, the generator, and `binaryDir`. This is the most frequently used category.

`buildPresets` corresponds to `cmake --build --preset` and pins build-stage arguments such as `--target`, `--config`, and the parallelism level. It was only added in schema version 2 (CMake 3.20).

`testPresets` corresponds to `ctest --preset` and pins the test-stage filters, output format, and so on. Also introduced in schema version 2.

Let's look at a complete minimal working example first, then break the fields down. The `CMakePresets.json` below is the one we actually tested this article with: a hidden `base` preset sets the common items, and `debug` and `release`, which inherit from it, each set `CMAKE_BUILD_TYPE`:

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

Field by field. The top-level `version` is **the version number of the JSON schema**, not the version number of CMake. It currently goes up to 9 (introduced in CMake 3.27); 3 is a safe floor that covers the full basic capability of `configurePresets` + `buildPresets` + `testPresets` and is natively supported from CMake 3.21 on. The schema version and `cmakeMinimumRequired` are two different things: the former declares "which version of the schema this JSON was written against", and the latter declares "how new a CMake you need at minimum to run this JSON". A CMake older than that minimum refuses to touch `CMakePresets.json` at all, which keeps an old CMake from failing to parse a new field and yet silently carrying on.

`configurePresets` is an array; each element is one preset. The `base` preset has a few key fields.

`name` is the unique identifier of the preset — it is what follows `cmake --preset`.

`hidden: true` means this preset cannot be used directly by `--preset`, nor does it appear in the `--list-presets` output; it serves only as a base class for other presets to inherit. We will verify this right away with `cmake --preset base`, and CMake will block it with an outright error.

`generator` and `binaryDir` pin down `-G` and `-B` respectively. Note that `binaryDir` is written as `${sourceDir}/build/${presetName}` — there are two layers of macro expansion here: `${sourceDir}` is the absolute path of the project root, and `${presetName}` is the name of the current preset (for example `debug` or `release`). The benefit of writing it this way is that each preset lands in its own build directory: `build/debug` and `build/release` stay out of each other's way, and switching build type does not require `rm -rf build` and starting over.

`cacheVariables` is `-D` made permanent. Each `key: value` entry is equivalent to `-Dkey=value`. The value can be a string, a boolean, `null` (meaning the `UNINITIALIZED` type), or an object with a `type` field (for precise control over the cache variable type).

Next, how `debug` and `release` inherit from `base`. `inherits: "base"` means "this preset pulls in every field of `base` and then overrides part of them itself". Here only `cacheVariables.CMAKE_BUILD_TYPE` is overridden: `debug` sets it to `Debug`, and `release` sets it to `Release`. The common fields on `base` — `generator`, `binaryDir`, `CMAKE_CXX_STANDARD` — are inherited untouched.

`inherits` accepts a single string or an array of strings. In the array case, when multiple parent presets supply the same field, **the one earlier in the array wins** — this differs from how C++ handles multiple-inheritance ambiguity; CMake has a deterministic order here.

The `buildPresets` section is simple: each build preset is bound to a configure preset through its `configurePreset` field. `cmake --build --preset debug` then knows to run the build under the `binaryDir` of `build/debug`, so you never have to write `cmake --build build/debug` yourself.

::: details Which schema version to pick
In the official documentation the schema version climbs all the way from 1 to 9. Which one to pick depends on which new features you need. version 1 (CMake 3.19) has only `configurePresets`, no build/test presets; version 2 (3.20) fills in `buildPresets`/`testPresets`; version 3 (3.21) adds the `cmakeMinimumRequired` field and more lenient macro expansion. Beyond that, the changes are mostly patches for advanced scenarios such as CI integration and conditional includes. My default pick is 3: it covers the vast majority of project needs while guaranteeing that CMake 3.21+ can parse it.
:::

## Putting it to work: real output from configure to build

Just reading the JSON is not satisfying — let's run it once. The minimal project (`CMakeLists.txt` + `main.cpp`) that pairs with this `CMakePresets.json` lives in the repo at `code/examples/vol7/cmake-fundamentals/04-presets/`. First, let's see which presets CMake recognizes:

```text
$ cmake --list-presets
Available configure presets:

  "debug"   - Debug (含 -g -O0)
  "release" - Release (含 -O3 -DNDEBUG)
```

`--list-presets` lists all the non-hidden configure presets together with their `displayName`. Note that `base` does not appear — `hidden` blocked it. If you insist on `cmake --preset base`, CMake errors out directly:

```text
$ cmake --preset base
CMake Error: Cannot use hidden configure preset in /tmp/cmake-presets-demo: "base"
```

That is exactly the semantics of a hidden preset: base class only, never used directly. This design keeps teammates from accidentally reaching for a "half-configured" preset.

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

The last line is the key evidence: the build files landed in `build/debug`. The `${sourceDir}/build/${presetName}` macro expansion did its job. Run `release` next and the build directory is `build/release`; the two stay out of each other's way:

```text
$ ls build/
debug  release
```

Then run the build through a build preset:

```text
$ cmake --build --preset debug
[1/2] Building CXX object CMakeFiles/app.dir/main.cpp.o
[2/2] Linking CXX executable app
```

`cmake --build --preset debug` is equivalent to `cmake --build build/debug`, but you do not have to remember what the `binaryDir` looks like — the preset remembers it for you.

Just running it cleanly is not enough; let's verify that `CMAKE_BUILD_TYPE` from `cacheVariables` really flowed into the compile commands. In `main.cpp` we planted an `#ifdef NDEBUG` to tell the two builds apart. First, let's see the flags the `release` binary actually received, by digging through `build.ninja`:

```text
$ grep FLAGS build/release/build.ninja | head -2
  FLAGS = -O3 -DNDEBUG -std=c++17
  FLAGS = -O3 -DNDEBUG

$ grep FLAGS build/debug/build.ninja | head -2
  FLAGS = -g -std=c++17
  FLAGS = -g
```

`release` gets `-O3 -DNDEBUG`, `debug` gets `-g`, and `-std=c++17` appears on both sides (it comes from `CMAKE_CXX_STANDARD` on `base`). That nails down the causal chain between `CMAKE_BUILD_TYPE: Debug/Release` written in the preset and the actual compiler flags. The two binaries' runtime output also matches:

```text
$ ./build/debug/app
debug build (NDEBUG NOT defined)

$ ./build/release/app
release build (NDEBUG defined)
```

One `CMakePresets.json`, two presets, two independent build trees, two binaries with different behavior — and the commands are as short as `cmake --preset debug` / `cmake --preset release`. Set that against the 130-plus-character old `-D` command from earlier, and the gap is right in front of you.

## CMakeUserPresets.json: personal overrides

`CMakePresets.json` is shared by the team and goes into version control. But some things are inherently "local to this machine" — which directory vcpkg is installed in, whether ASan is turned on locally, or me wanting to add a temporary preset to experiment with some flag. Writing these into `CMakePresets.json` pollutes the team configuration: when others pull, either the path cannot be found or some option that should never be on is mysteriously on.

CMake's answer is `CMakeUserPresets.json`. It sits in the same directory as `CMakePresets.json`, has exactly the same structure, but its semantics are "personal override":

```text
project root/
├── CMakePresets.json        # in git, shared by the team
├── CMakeUserPresets.json    # in .gitignore, local to this machine only
├── CMakeLists.txt
└── ...
```

Presets defined in `CMakeUserPresets.json` and presets in the main file are merged and shown together. More importantly, **a preset in UserPresets can inherit a hidden preset from the main file**. On our machine we added an `asan` preset that inherits `base` from the main file and layers an ASan flag on top:

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

Check `--list-presets` again:

```text
$ cmake --list-presets
Available configure presets:

  "asan"
  "debug"   - Debug (含 -g -O0)
  "release" - Release (含 -O3 -DNDEBUG)
```

`asan` shows up, on equal footing with `debug` and `release`. A direct `cmake --preset asan` runs through, and the build directory lands at `build/asan` automatically:

```text
$ cmake --preset asan
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-presets-demo/build/asan
```

::: warning CMakeUserPresets.json must go into .gitignore
The official documentation's exact words are "should NOT be checked in". Its whole premise is "every machine has different paths"; once it goes into git, conflicts are guaranteed. The first thing to do when starting a new project is add `CMakeUserPresets.json` to `.gitignore` — don't wait for a colleague's PR to arrive carrying his own vcpkg path to torment you.
:::

## IDE integration: VSCode CMake Tools

Beyond the command line, the place presets really land is the IDE. The VSCode CMake Tools extension reads `CMakePresets.json` natively: the status bar directly lists the selectable configure presets and build presets, and one click switches between them — no commands to type.

clangd benefits indirectly as well. Once CMake Tools has picked a preset, it runs the corresponding configure automatically, and the generated `compile_commands.json` gets picked up by clangd to power completion and navigation in the editor. Because the preset pins every `-D` and the generator, the compile environment seen in the IDE is identical to the command line and to CI — this is the biggest advantage of presets over "the IDE maintaining its own configuration": a single source of truth.

The Remote-WSL scenario is just as smooth: `CMakePresets.json` travels into the WSL filesystem along with the source code, and CMake Tools on the VSCode Remote side reads it directly — no need to configure it once on the Windows side and again on the WSL side.

## Hooking up cross-compilation

At this point you can probably already smell the natural fit between presets and cross-compilation. The core of cross-compilation is that one `-D` flag, `-DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake`, plus a pile of cache variables tied to the target board. These are exactly the things presets are best at pinning down.

`CMakePresets.json` provides a dedicated `toolchainFile` field, cleaner than stuffing it into `cacheVariables`:

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

After that, a single `cmake --preset f407-debug` completes the cross-compilation configuration, and anyone on the team who pulls the repo reproduces the same toolchain setup. How this mechanism cooperates with `arm-none-eabi-g++`, the sysroot, and cortex-m linker scripts is something we expand on in detail in the vol7 cross-compilation article.

## Companion example

The project scaffold for this article can be run straight from the example directory in the repo:

```text
code/examples/vol7/cmake-fundamentals/04-presets/
├── CMakeLists.txt
├── main.cpp
└── CMakePresets.json
```

Once you are in that directory, run `cmake --list-presets`, `cmake --preset debug`, `cmake --build --preset debug`, and `./build/debug/app` in turn to reproduce every output in this article. To verify the propagation of `cacheVariables`, change `debug` to `release` and rerun, then compare `FLAGS = -O3 -DNDEBUG` in `build/release/build.ninja` against `FLAGS = -g` in `build/debug/build.ninja`.

At this point we have grounded the preset structure, the hidden + inherits combination, personal overrides via CMakeUserPresets.json, and IDE integration in practice, and we have verified the `${presetName}` macro expansion and the propagation of `CMAKE_BUILD_TYPE` with real output. The next article settles a question vol7 has been carrying for a long time: when the target board moves from x86 Linux to an ARM Cortex-M device like the STM32F407, how do you write the `CMakeLists.txt`, what does the toolchain file look like, and how do presets hook up with them — in other words, the complete cross-compilation pipeline.
