---
title: "C++ engineering on WSL — vscode + clangd in depth, with full debugging"
description: "The deep follow-up to the getting-started piece that installed clangd: fill out the WSL2 toolchain, push .clangd to full power, wire up the launch.json/tasks.json debug chain, and explain Remote-WSL's client/server architecture plus how clangd finds compile_commands.json"
chapter: 1
order: 6
platform: host
difficulty: intermediate
cpp_standard: [17, 20]
tags:
  - host
  - cpp-modern
  - intermediate
  - clangd
reading_time_minutes: 20
prerequisites:
  - "Making vscode Understand Your Code: Install clangd, Watch the Red Lines Vanish"
related:
  - "What is CMake — the two-stage pipeline of a build system generator"
  - CMakePresets.json — from the old cmake -D way to reproducible --preset builds
translation:
  source: documents/vol7-engineering/cpp-development-on-wsl.md
  source_hash: 3772ab37f7f578896f0b5d99ae70294a2857e8f96b006d42e704ad592ad3f0a7
  translated_at: '2026-09-26T05:01:55+00:00'
  engine: anthropic
  token_count: 4500
---

# C++ engineering on WSL — vscode + clangd in depth, with full debugging

For serious C++ engineering on Windows, the smoothest combo right now is **WSL2 + vscode + clangd**. This piece configures the whole stack in one pass: install the full Linux toolchain, push clangd to full power, and wire up the `launch.json` + `tasks.json` debug pipeline. If you're coming straight from [getting-started piece 5](/getting-started/05-vscode-clangd), where three steps installed clangd and killed the red squiggles, this piece digs further — what every entry in the `.clangd` config file actually does, how clang-tidy hooks into clangd, what to do about slow background indexing on a big project, and how gdb displays a `std::vector` once your breakpoint lands.

## Why WSL

Windows does ship its own C++ toolchains; MSVC and MinGW both run. But if you've followed this tutorial into volume 7, you'll have noticed that every command-line example, every `CMakeLists.txt` snippet, and every bit of terminal output assumes a Linux environment. Run things directly on Windows and the tools still work, but every step passes through a "translation" layer: `g++` becomes `g++.exe`, path separators change, and the sysroot path for `arm-none-eabi-g++` has to be reset. WSL2 wipes out that translation entirely.

WSL2 is Microsoft's real Linux kernel running inside Windows (not an emulator). For those of us doing C++ engineering, it brings three direct benefits.

The Linux toolchain is the most complete. `gcc`, `gdb`, `make`, `cmake`, `ninja-build`, `clangd`, `clang-tidy`, `valgrind`, `binutils` — one `apt` command installs them all, and the versions stay current. Volume 6 of this tutorial covers AddressSanitizer and volume 7 covers cross-compilation; on native Windows those tools either require a detour through MSYS2 or simply don't exist.

It matches production. The C++ projects we write will mostly run on Linux servers later. Having the dev environment be Linux too means the whole "works on my machine, crashes on the server" class of environment-mismatch problems never gets a chance to exist.

WSL2 performance is close to native. WSL2 takes the real-Linux-kernel-in-a-lightweight-VM route, completely different from WSL1's syscall translation. Filesystem IO and process scheduling are close to native Linux performance, and compile speed isn't far off a real Linux box. That's the key upgrade of WSL2 over WSL1, and the reason C++ development today defaults to WSL2.

::: warning Don't put the project under `/mnt/c`
WSL2 reaches the Windows filesystem (`/mnt/c/...`) over the 9P protocol, which is an order of magnitude slower on IO. Keep the project in WSL's own filesystem (under `~/projects/`) and both configure and build get much faster. I missed this the first time: a mid-sized project took 40 seconds to configure, and after moving it under `~/` it dropped to 4.
:::

## Installing WSL2 and the C++ toolchain

Installing WSL2 is one command in PowerShell (as administrator):

```powershell
wsl --install
```

That command enables the required Windows feature (Virtual Machine Platform), downloads the default Ubuntu distribution, and installs it. Reboot once afterwards, launch Ubuntu, and the first start asks you to set a username and password. If you'd rather use another distribution (Debian, Fedora), `wsl --list --online` shows the available list and `wsl --install -d <name>` installs the one you pick.

Inside Ubuntu, first refresh the system packages, then install the complete C++ engineering toolset in one shot:

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential cmake ninja-build gdb clangd clang-tidy clang-format
```

`build-essential` is the Debian/Ubuntu metapackage for C/C++; installing it brings `gcc`/`g++`/`make`. `cmake` is the build system generator (covered in detail in volume 7 ch00 01), `ninja-build` provides `ninja` (faster than `make`, and this tutorial's default generator), and `gdb` is the debugger. The last three belong to the LLVM toolchain: `clangd` is the LSP server from the clang project (it's what makes vscode understand your code), `clang-tidy` is static analysis, and `clang-format` is the formatter.

::: details A few useful extras to install along the way

```bash
# valgrind memory checking (used in volume 6, the memory-safety volume)
sudo apt install -y valgrind

# ccache to speed up rebuilds (especially worth it on CI and large projects)
sudo apt install -y ccache

# Several build tools that pair with cmake
sudo apt install -y ninja-build

# See what symbols live in a build artifact and which shared libraries it depends on
sudo apt install -y binutils
```

:::

After installing, verify the versions to confirm everything is there. Below is the output from my machine:

```text
$ gcc --version | head -1
gcc (Ubuntu 13.2.0-23ubuntu4) 13.2.0

$ cmake --version | head -1
cmake version 3.28.3

$ ninja --version
1.11.1

$ gdb --version | head -1
GNU gdb (Ubuntu 14.1-0ubuntu3.1) 14.1

$ clangd --version
clangd version 18.1.3
Features: linux
Platform: x86_64-pc-linux-gnu
```

::: tip clangd must be installed together with the toolchain
Newcomers often forget to install the `clangd` program itself and only install the vscode clangd extension. The extension is just the "remote control"; the `clangd` binary does the actual work. Installing the extension without the program is like having a remote control with no TV. Only when `clangd --version` prints a version number is it actually installed.
:::

Ubuntu 24.04's apt repositories carry clangd 18.x, which is already plenty (inline hints, include-cleaner, external indexing — the feature set is all there). If you insist on chasing the newest version, adding LLVM's official apt repository gets you 19/20, but that's unnecessary for this tutorial.

## vscode Remote-WSL: editor on Windows, work in WSL

vscode running C++ is, under the hood, a client/server architecture: the vscode UI runs on Windows, the processes doing the real work run inside WSL, and the Remote-WSL extension connects the two. Without a clear picture of this architecture, none of the problems that come later can be located.

Two things to do on the Windows side:

- Install vscode (download from [code.visualstudio.com](https://code.visualstudio.com), plain next-next-next)
- Search the vscode extension marketplace for `WSL` (publisher Microsoft) and install it

With that done, there are two ways to open a project living in WSL:

First, open the command palette (`F1` or `Ctrl+Shift+P`), type `Remote-WSL: New Window`, and a fresh vscode window connected to WSL opens up.

Second, in a WSL terminal, cd into the project directory and type:

```bash
code .
```

The `code` command is injected into WSL's PATH automatically once the Remote-WSL extension is installed. It opens vscode on the Windows side and treats the current directory as the workspace.

::: details Why `code .` works
Remote-WSL drops a `code` shell script into WSL (usually at `/usr/bin/code`). What it does is communicate with vscode on the Windows side and have vscode start up and connect over. The first run pulls a vscode server component from Windows into WSL (`~/.vscode-server/`); that server is the process that actually runs extensions, terminals, and language servers. Subsequent opens are instant.
:::

Once connected, look at the bottom-left corner of the vscode window: there should be a green or blue badge reading `WSL: Ubuntu`. It means every file operation, terminal, and extension in this window runs inside WSL.

Next comes the biggest trap for newcomers: **vscode extensions are installed on both sides**. Extensions on the Windows side handle UI (themes, icons, keybindings); extensions on the WSL side handle the Linux work (code understanding, debugging, building). After Remote-WSL connects, the extensions panel splits into two groups: "LOCAL - INSTALLED" (the Windows side) and "WSL: UBUNTU - INSTALLED" (the WSL side). The clangd, C/C++, and CMake Tools extensions you need must go into the WSL group (click "Install in WSL: Ubuntu" next to the extension).

```text
Extensions panel (after connecting to WSL)
├── LOCAL - INSTALLED        ← Windows side: themes, icons, Remote-WSL itself
│   ├── Remote - WSL  ✓
│   ├── Material Icon Theme
│   └── ...
└── WSL: UBUNTU - INSTALLED  ← WSL side: install clangd / C/C++ / CMake Tools here
    ├── clangd           ← code understanding (completion/jump-to-definition/diagnostics)
    ├── C/C++            ← debugging (keep cppdbg, disable IntelliSense)
    └── CMake Tools      ← CMake configure/build/kit selection (optional)
```

The clangd extension must be installed on the WSL side. It needs to invoke the `clangd` binary inside WSL and read the `compile_commands.json` inside WSL — everything lives on the Linux side. Install it on the Windows side by mistake and it goes looking for `clangd.exe` on Windows, which it will certainly never find.

## clangd configuration in depth

[getting-started piece 5](/getting-started/05-vscode-clangd) installed clangd and killed the red squiggles, but it covered only three steps: turn on `CMAKE_EXPORT_COMPILE_COMMANDS`, install the extension, and disable the C/C++ extension's IntelliSense. This piece fills in the rest — how clangd finds compile_commands, what every entry in the `.clangd` config file does, how clang-tidy hooks in, and how to turn on include-cleaner.

### Where compile_commands.json comes from

clangd does its work on the back of a file called `compile_commands.json`. It's the Compilation Database format defined by the Clang community: one record per `.cpp` in the project, recording the complete command used to compile it — the compiler path, the `-std=` standard, every `-I` header search path. With this file in hand, clangd can "stand where the compiler stands" when reading the code, knowing which header `std::vector` lives in and which features are available under `-std=c++17`.

CMake makes this particularly smooth — one line does it. After `project()` in your `CMakeLists.txt`, add:

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

Or, if you'd rather not touch `CMakeLists.txt`, adding `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` on the configure command line works too. Once configure finishes, `build/compile_commands.json` is generated.

::: warning This switch only takes effect for Makefile / Ninja generators
`CMAKE_EXPORT_COMPILE_COMMANDS` only emits `compile_commands.json` under the Makefile or Ninja generators. The Visual Studio generator (`-G "Visual Studio 17 2022"`) and the Xcode generator don't support it. Inside WSL we default to Ninja, so this is a non-issue.
:::

With configure done, `build/compile_commands.json` looks like this (real output from my machine):

```json
[
  {
    "directory": "/home/user/wsl-clangd/build",
    "command": "/usr/bin/c++ -I/home/user/wsl-clangd -std=c++17 -o CMakeFiles/greeter.dir/main.cpp.o -c /home/user/wsl-clangd/main.cpp",
    "file": "/home/user/wsl-clangd/main.cpp",
    "output": "/home/user/wsl-clangd/build/CMakeFiles/greeter.dir/main.cpp.o"
  }
]
```

One entry per `.cpp`. The `command` field is the critical one: clangd parses it to get the compiler, the standard, and the header paths, then understands the code from that vantage point. So after you change `CMakeLists.txt` (adding a `target_include_directories`, say), you must re-configure so `compile_commands.json` refreshes — otherwise clangd keeps using the old vantage point, doesn't know about the new header path, and the red squiggles come back.

### How clangd finds compile_commands.json

The official behavior is this: clangd takes the source file you're editing and walks up the directory chain from its location looking for `compile_commands.json`, using the first one it finds. So if your source file is `~/proj/src/foo.cpp`, clangd looks, in order:

```text
~/proj/src/compile_commands.json
~/proj/compile_commands.json
~/compile_commands.json
~/.../compile_commands.json
```

Since clangd 16 there's one extra rule: at each directory along the way, it also glances into that directory's `build/` subdirectory to see whether a `compile_commands.json` is there. This convenience was added specifically for CMake projects — CMake writes the file into `build/` by default, clangd knows this, and goes digging on its own.

I tested this on clangd 22: with source files under `src/` and `compile_commands.json` under `build/`, and no symlink of any kind at the project root, clangd still finds it:

```text
I[11:23:59.322] Loading compilation database...
I[11:23:59.323] Loaded compilation database from /tmp/clangd-search-test/build/compile_commands.json
```

So by default you don't need to lift a finger. But two scenarios still call for pointing at it manually.

First scenario: you use multiple build directories (say `build-debug/` and `build-release/`), clangd doesn't know which to pick and may flip-flop between the two. Point at one directly in `.clangd`:

```yaml
CompileFlags:
  CompilationDatabase: build-debug
```

The `CompilationDatabase` field can be a directory path (relative to the project root), or `Ancestors` (the default behavior: walk up + dig through `build/`), or `None` (turn it off, fall back to fallback flags only).

Second scenario: old clangd (15 and earlier) lacks the "dig through `build/` subdirectories" rule and genuinely only walks parent directories looking for `compile_commands.json` at their roots. In that case the project root needs a symlink:

```bash
ln -sf build/compile_commands.json compile_commands.json
```

Walking upward, clangd then hits this symlink at the project root and follows it to the real file inside `build/`. New clangd doesn't need this step, but keeping it does no harm and stays compatible with old clangd.

### The .clangd config file, field by field

`.clangd` is clangd's project-level configuration, YAML format, placed at the project root. clangd walks up the source file's directory chain looking for `.clangd` files; all the fragments it hits are merged in order, and the closer to the source file, the higher the priority. The one below is the config I use in practice (it also lives in the repo at `code/examples/vol7/wsl-clangd/.clangd`); let's go through it segment by segment, what each entry does:

```yaml
CompileFlags:
  Add: [-Wall, -Wextra, -Wno-unused-parameter]
  Remove: [-fsanitize=thread]
  Compiler: clang++
  CompilationDatabase: build
```

The `CompileFlags` section post-processes the compile commands from `compile_commands.json`. `Add` appends flags to every command — `-Wall -Wextra` makes clangd flag things as strictly as a real compile, and `-Wno-unused-parameter` lets it forgive parameters that must exist but go unused, like the callback kind in this project. `Remove` kills flags by wildcard; the classic case is a `-fsanitize=thread` sitting in `compile_commands.json` (TSan was covered in volume 5) — clangd doesn't need to rerun it, and rerunning it produces strange diagnostics anyway. `Compiler` swaps the compiler executable name for a value you choose; writing `clang++` makes clangd use Clang's own driver to probe system headers and the ABI, which is especially useful in cross-compilation scenarios (when the original compiler is `arm-none-eabi-g++` and clangd can't probe the sysroot, switching to `clang++` paired with `--query-driver` solves it). `CompilationDatabase` was covered above: the directory where compile_commands lives.

```yaml
Index:
  Background: Build
  StandardLibrary: Yes
```

The `Index` section governs clangd's indexing. `Background: Build` turns on background indexing (that's what's working when a project opens slowly the first time); the index is persisted under `~/.cache/clangd/index/` and reused the next time you open the same project — no starting over from scratch. `StandardLibrary: Yes` pulls standard library symbols into the index, so typing `std::` completes `vector`, `cout`, and friends. Both of these default to on; writing them out just makes it explicit.

```yaml
InlayHints:
  Enabled: Yes
  ParameterNames: Yes
  DeducedTypes: Yes
  Designators: Yes
  BlockEnd: Yes
```

`InlayHints` is clangd 18+'s inline hints: gray ghost text rendered directly inside the code line. `ParameterNames: Yes` shows parameter names at call sites — `greet(/*name=*/"WSL")` — saving you the round trip to the declaration to see what a parameter is called. `DeducedTypes: Yes` shows the type `auto` deduced — `auto /*= int*/ sum`. `Designators: Yes` shows field names in aggregate initialization — `Point{/*.x=*/1, /*.y=*/2}`. `BlockEnd: Yes` shows what a big `}` belongs to — which function or namespace — so the closing brace at the end of a several-thousand-line function is no longer a mystery. This group is off by default in the vscode clangd extension; turning it on steps code readability up a whole level.

```yaml
Diagnostics:
  ClangTidy:
    Add: [modernize-*, bugprone-*, performance-*, readability-*]
    Remove: [modernize-use-trailing-return-type, readability-magic-numbers]
  UnusedIncludes: Strict
  MissingIncludes: Strict
  Suppress: [unused-includes]
```

The `Diagnostics` section governs the red and yellow squiggles. `ClangTidy.Add/Remove` has clangd run clang-tidy checks right in the editor, no manual terminal needed. With `modernize-*` on, write `NULL` and it suggests `nullptr`; write `for (int i = 0; i < v.size(); ++i)` and it suggests switching to a range-based for. `Remove` turns off the noisy checks — `modernize-use-trailing-return-type` mandates the `auto foo() -> int` style, which the community has fought over for years and most projects reject. `UnusedIncludes: Strict` and `MissingIncludes: Strict` enable clangd's built-in include-cleaner, flagging both "included but unused" and "used but not included". **Turn these two off when first picking up a project** — switch them on in an old codebase and the screen floods with yellow squiggles, enough to make you want to uninstall clangd on the spot. `Suppress` silences specific diagnostic codes, more surgical than disabling a check.

```yaml
Hover:
  ShowAKA: Yes
```

The `Hover` section governs mouse-hover tooltips. `ShowAKA: Yes` makes typedef/using aliases show the underlying type on hover as well — hover `size_type` and you can see `std::size_t` underneath.

### Key items in the clangd extension's settings.json

The `.clangd` file governs the behavior of the clangd program itself; the vscode clangd extension additionally has a set of its own settings in `settings.json`. Below are the key items that pair with `.clangd` (the full version is in the repo at `code/examples/vol7/wsl-clangd/.vscode/settings.json`):

```json
{
    "C_Cpp.intelliSenseEngine": "disabled",
    "clangd.arguments": [
        "--background-index",
        "--clang-tidy",
        "--header-insertion=iwyu",
        "--all-scopes-completion",
        "--function-arg-placeholders",
        "--pch-storage=disk",
        "--inlay-hints",
        "--j=4"
    ],
    "clangd.onConfigChanged": "restart"
}
```

`C_Cpp.intelliSenseEngine: disabled` is the core of that step from piece 5 — turning off the C/C++ extension's code understanding so clangd has the field to itself. `clangd.arguments` holds clangd's command-line arguments at startup. `--background-index` explicitly enables the background index; `--clang-tidy` enables the clang-tidy integration (pairing with `.clangd`'s `Diagnostics.ClangTidy` and the `.clang-tidy` file); `--header-insertion=iwyu` auto-adds the `#include` when you accept a completion; `--all-scopes-completion` lets completions cross the current namespace (inside some namespace, you can still complete global symbols); `--function-arg-placeholders` makes function completions carry parameter placeholders; `--pch-storage=disk` persists PCHs to disk to save memory; `--inlay-hints` enables the inline hints (clangd 18+); `--j=4` sets background parallelism.

`clangd.onConfigChanged: restart` is a critical one: after you edit `.clangd`, clangd restarts itself and loads the new config. Without it, every `.clangd` edit needs a manual `Ctrl+Shift+P` run of `clangd: Restart language server` before it takes effect.

### Background Index: a slow first open on a big project is normal

Open a project with tens of thousands of lines, and after startup clangd keeps the status bar spinning for a few minutes, even ten-plus minutes. That's the background index at work: it parses every source file, extracts symbols and reference relations, and writes them to disk under `~/.cache/clangd/index/`. Once the first pass is done, the index gets reused and the second open is fast.

To verify it's actually working, open clangd's output panel (`View → Output → clangd`) and you'll see logs like these:

```text
I[15:32:11.456] Indexing xxx.cpp
I[15:32:11.612] Indexed preamble symbols: 1240
I[15:32:11.738] Background: 1450 indexed, 0 dirty
```

If the project is truly enormous (Chromium, say), the index can eat several gigabytes of memory. If your machine can't take it, disable background indexing with `Background: Skip` or `--background-index=0`. The price is slower cross-file navigation and completion, since no cross-file index exists. For most projects, leaving it on is fine.

### clang-tidy integration

clangd's built-in clang-tidy integration moves static checking directly into the editor, no terminal switching. It works like this:

Put a `.clang-tidy` file (YAML format) at the project root listing which checks to enable:

```yaml
Checks: >
    -*,
    modernize-*,
    bugprone-*,
    performance-*,
    readability-*,
    -modernize-use-trailing-return-type,
    -readability-magic-numbers,
    -readability-identifier-length
WarningsAsErrors: ''
HeaderFilterRegex: '.*'
FormatStyle: file
```

The first entry of `Checks`, `-*`, turns off every default check; the following `modernize-*`-style globs then turn groups back on one by one. A `-` prefix means off. `HeaderFilterRegex` decides which headers clang-tidy checks — `.*` is all of them; if third-party libraries generate too much noise, change it to a regex matching only your own project's headers.

clangd reads this file automatically at startup. With `--clang-tidy` on in `settings.json`, every line you edit has clangd run the relevant clang-tidy checks on the spot, painting the problems as yellow or red squiggles right in the editor.

I tested a snippet that triggers `readability-identifier-length`:

```text
$ cat tidy_demo.cpp
#include <cstdint>
int main() {
    int big = 1000000000;
    long narrowed = big;
    int* p = nullptr;   // ← name too short; the check flags anything under 3 characters
    return 0;
}

$ clang-tidy -p build tidy_demo.cpp
... tidy_demo.cpp:5:10: warning: variable name 'p' is too short,
    expected at least 3 characters [readability-identifier-length]
    5 |     int* p = nullptr;
      |          ^
```

The same diagnostic in vscode is simply a yellow squiggle under the variable name `p`, with `[readability-identifier-length]` shown on hover. With the clangd integration you never open a terminal — the problems appear as you write.

### include-cleaner

The include-cleaner built into clangd (no external clang-tidy dependency) exists to cure exactly the two include diseases: included but unused, and used but not included. The switches live in the `Diagnostics` section of `.clangd`:

```yaml
Diagnostics:
  UnusedIncludes: Strict   # None = off, Strict = strictly on
  MissingIncludes: Strict
```

My recommendation: **enable it from day one on new projects**, so include hygiene is clean at the source; **start with `None` when taking over an old project** — old code carries heavy include baggage, and switching Strict on immediately floods the screen with yellow squiggles until you lose all judgment. Straighten out the code first, then switch it on.

include-cleaner also supports IWYU pragmas, written inside headers to give the tool instructions:

```cpp
#include <vector>  // IWYU pragma: export
#include "detail_helpers.h"  // IWYU pragma: keep  ← don't flag this even if unused
```

`export` says "this header includes `<vector>` on behalf of its users, so users don't need to include it again"; `keep` says "never flag this include as unused". Both pragmas see heavy use in large libraries, keeping include-cleaner from false positives.

### clangd or the C/C++ extension (aligned with the getting-started piece)

At this point you might ask: should the C/C++ extension be uninstalled? No. Consistent with [getting-started piece 5](/getting-started/05-vscode-clangd):

- clangd handles "understanding the code" — completion, navigation, diagnostics, hover, inline hints, clang-tidy. Accurate.
- The C/C++ extension stays for "debugging" — breakpoints, stepping, watching variables, the call stack. Its `cppdbg` debugger is the most mature way to drive gdb/lldb from vscode.

So `C_Cpp.intelliSenseEngine: disabled` disables the C/C++ extension's code understanding; the extension itself stays installed. The two split the labor and don't fight. What we debug below is the C/C++ extension's `cppdbg`.

## Debug configuration: launch.json

Once the project builds and clangd jumps around the code, the last link in the chain is debugging: breakpoints, single-stepping, inspecting variables. This section of the piece finishes what the original draft abandoned — it broke off abruptly at the sentence "switch to the debug panel and click".

Debugging C++ in vscode goes through `.vscode/launch.json`. Here's a complete working configuration (the repo has the same one at `code/examples/vol7/wsl-clangd/.vscode/launch.json`), using the C/C++ extension's `cppdbg` + gdb:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "(gdb) Launch greeter",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/greeter",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "miDebuggerPath": "/usr/bin/gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "build"
        }
    ]
}
```

Field by field. `type: cppdbg` is the debugger type provided by the C/C++ extension, driving gdb through gdb's MI protocol. `program` is the full path to the executable being debugged; `${workspaceFolder}` is the root of the project vscode currently has open. `MIMode: gdb` paired with `miDebuggerPath: /usr/bin/gdb` tells it to use the gdb inside WSL. `preLaunchTask: build` runs a task named `build` (defined in tasks.json below) before F5 launches; if the build fails, the debug session doesn't start — sparing you from debugging a stale binary.

The `-enable-pretty-printing` inside `setupCommands` is the key one. Without it, when you break on a `std::vector<int> v{1,2,3,4,5}`, the variables panel shows a pile of raw members (`_M_start`, `_M_finish`, `_M_end_of_storage` — libstdc++ internal pointers like that), with no way to tell the vector holds `{1,2,3,4,5}`. With it on, gdb uses its Python pretty-printers to format it into readable form. Below is the real gdb output comparison from my machine:

```text
(gdb) print nums        # nums is std::vector<int>{1,2,3,4,5}

without pretty-printing: $1 = {_M_impl = {_M_start = 0x555..., _M_finish = ..., _M_end_of_storage = ...}}
with pretty-printing:    $1 = std::vector of length 5, capacity 5 = {1, 2, 3, 4, 5}
```

In vscode, once `setupCommands` is configured, the variables panel shows the readable form like the second line. This is the step newcomers miss most easily: you can debug, but the variables are unreadable — breakpoints might as well not be there.

::: tip CodeLLDB as an alternative
If you prefer lldb, install the CodeLLDB extension (`vadimcn.vscode-lldb`) plus `sudo apt install lldb` inside WSL, and switch launch.json to `"type": "lldb"`. CodeLLDB skips the MI protocol and drives lldb directly — faster startup, and friendlier display of C++ types (pretty-printing built in, no configuration). But this tutorial standardizes on gdb, and the examples below are all gdb-based.
:::

With everything configured, click in the gutter to the left of line 14 of `main.cpp` (the `for (int x : nums)` line) to drop a red breakpoint dot, then press `F5`. vscode first runs the `build` task to recompile; once the build finishes it starts gdb loading `build/greeter` and runs until the breakpoint stops it. The Run and Debug panel on the left shows the call stack, variables, breakpoints, and watches. In the variables panel, `nums` expands to `std::vector of length 5, capacity 5 = {1, 2, 3, 4, 5}`, and `sum` is the running total. `F10` steps over, `F11` steps into, `F5` continues.

## tasks.json build tasks

That `preLaunchTask: build` in launch.json needs a matching task. Tasks are defined in `.vscode/tasks.json`:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "build",
            "type": "shell",
            "command": "cmake",
            "args": [
                "--build",
                "${workspaceFolder}/build",
                "--config",
                "Debug",
                "--parallel"
            ],
            "options": {
                "cwd": "${workspaceFolder}"
            },
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "configure",
            "type": "shell",
            "command": "cmake",
            "args": [
                "-S", "${workspaceFolder}",
                "-B", "${workspaceFolder}/build",
                "-G", "Ninja",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
            ],
            "options": { "cwd": "${workspaceFolder}" },
            "problemMatcher": []
        },
        {
            "label": "rebuild",
            "dependsOn": ["configure", "build"],
            "dependsOrder": "sequence",
            "group": "build",
            "problemMatcher": []
        }
    ]
}
```

The three tasks split the work. `build` runs the incremental build (`cmake --build build`, Ninja underneath); it's the default build task (`isDefault: true`), so `Ctrl+Shift+B` triggers it directly. `configure` runs the first time or after `CMakeLists.txt` changes, re-configuring once to refresh `compile_commands.json`. `rebuild` uses `dependsOrder: sequence` to run configure then build in order — one command for the whole thing.

`problemMatcher: ["$gcc"]` has vscode parse the compiler output, turning errors and warnings into clickable entries in the Problems panel — one click jumps to the offending line. It's vscode's built-in `$gcc` pattern, matching the gcc/clang error format.

The chain triggered by F5 from launch.json is: run the `build` task → build succeeds → gdb starts and loads `build/greeter` → run to the breakpoint and stop. The debug loop closes on itself — no more manually switching to a terminal to type `cmake --build` every time.

## Where this leaves you

With WSL2 + vscode + clangd + cppdbg all configured, the C++ engineering environment in your hands is nearly indistinguishable from what a seasoned Linux developer uses: accurate completion, fast navigation, strict diagnostics, and a debugger that can show a vector. Reading on to volume 7 ch00's CMake series (the target mental model, CMakePresets.json) and volume 6's memory safety (AddressSanitizer + valgrind), the commands all go straight into the WSL terminal and match the outputs in those articles.

All the companion configuration files (`.clangd`, `.clang-tidy`, `.vscode/settings.json`, `launch.json`, `tasks.json`) live in the repo under `code/examples/vol7/wsl-clangd/` — clone it and they run as-is. The CMake project is minimally reproducible: `cmake -B build -G Ninja && cmake --build build` produces `build/greeter`, and F5 drops you into the debugger.
