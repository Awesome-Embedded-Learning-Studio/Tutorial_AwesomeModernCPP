---
title: "C++ Engineering on WSL — vscode + clangd in depth, with full debugging"
description: "The deeper follow-up to the getting-started clangd piece: install the full WSL2 toolchain, push .clangd to full power, wire up launch.json/tasks.json for debugging, and explain Remote-WSL's client/server architecture and how clangd finds compile_commands.json"
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
  - "起步卷篇 5: 让 vscode 看懂您的代码——装 clangd"
related:
  - "CMake 是什么——构建系统生成器的两段式流水线"
  - "CMakePresets.json——从 cmake -D 老式到 --preset 可复现"
---

# C++ Engineering on WSL — vscode + clangd in depth, with full debugging

For serious C++ work on Windows, the smoothest combo today is **WSL2 + vscode + clangd**. This piece sets the whole stack up in one pass: a full Linux toolchain, clangd pushed to full power, and a working `launch.json` + `tasks.json` debug pipeline. If you came over from [getting-started piece 5](/getting-started/05-vscode-clangd), where three steps installed clangd and killed the red squiggles, this is the deeper dig: what every field in `.clangd` actually does, how clang-tidy plugs into clangd, what to do when background indexing crawls on a big project, and how gdb shows a `std::vector` once your breakpoint hits.

## Why WSL

Windows does ship C++ toolchains. MSVC and MinGW both work. But read this tutorial through volume 7 and you'll notice every command-line example, every `CMakeLists.txt` snippet, every bit of terminal output assumes Linux. Run it natively on Windows and the tools still work, but every step goes through a "translation" layer: `g++` becomes `g++.exe`, path separators flip, the sysroot path for `arm-none-eabi-g++` has to be reset. WSL2 wipes out that translation entirely.

WSL2 is Microsoft's real Linux kernel running inside Windows (not an emulator). For our C++ engineering use case it gives three direct wins.

The Linux toolchain is the most complete. `gcc`, `gdb`, `make`, `cmake`, `ninja-build`, `clangd`, `clang-tidy`, `valgrind`, `binutils` all land in one `apt` command, and the versions stay current. Volume 6 covers AddressSanitizer, volume 7 covers cross-compilation, and on native Windows those tools either need a detour through MSYS2 or just don't exist.

It matches production. The C++ projects we write will mostly run on Linux servers. Having the dev environment be Linux too means the "works on my machine, crashes on the server" class of environment-mismatch bugs simply never gets a chance to exist.

WSL2 performance is close to native. WSL2 uses a real Linux kernel in a lightweight VM, totally different from WSL1's syscall translation. Filesystem IO and process scheduling are close to native Linux speed, and compile times aren't far off a real Linux box. That's the key upgrade from WSL1, and the reason everyone doing C++ now defaults to WSL2.

::: warning Don't put the project under `/mnt/c`
WSL2 reaches the Windows filesystem (`/mnt/c/...`) over the 9P protocol, which is about an order of magnitude slower on IO. Put the project in WSL's own filesystem (under `~/projects/`) and both configure and build get noticeably faster. I missed this the first time and a mid-sized project took 40 seconds to configure; after moving it under `~/` it dropped to 4.
:::

## Installing WSL2 and the C++ toolchain

WSL2 installs in one PowerShell (admin) command:

```powershell
wsl --install
```

That enables the required Windows feature (Virtual Machine Platform), downloads the default Ubuntu distribution, and installs it. Reboot once after it finishes, launch Ubuntu, and the first run asks for a username and password. If you want a different distro (Debian, Fedora), `wsl --list --online` shows the options and `wsl --install -d <name>` installs a specific one.

Once inside Ubuntu, refresh the system packages and pull in the full C++ toolchain in one shot:

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential cmake ninja-build gdb clangd clang-tidy clang-format
```

`build-essential` is Debian/Ubuntu's C/C++ meta-package, and pulling it in brings `gcc`/`g++`/`make`. `cmake` is the build-system generator (covered in volume 7 ch00/01), `ninja-build` provides `ninja` (faster than `make`, the default generator in this tutorial), `gdb` is the debugger. The last three are the LLVM toolchain: `clangd` is clang's LSP server (what lets vscode understand your code), `clang-tidy` is the static analyzer, `clang-format` is the formatter.

::: details A few useful extras to grab while you're at it

```bash
# valgrind memory checking (used in volume 6's memory-safety chapter)
sudo apt install -y valgrind

# ccache to speed up rebuilds (especially worth it on CI and large projects)
sudo apt install -y ccache

# Several build tools that go with cmake
sudo apt install -y ninja-build

# Inspect what symbols live in a build artifact and which shared libs it depends on
sudo apt install -y binutils
```

:::

After installing, check the versions to confirm everything landed. Here's the output on my machine:

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

::: tip Always install clangd alongside the toolchain
A common newbie mistake is to install only the vscode clangd extension and forget the `clangd` binary. The extension is just a remote control; the `clangd` binary is what actually does the work. Extension without binary means a remote with no TV. `clangd --version` printing a version number is the only proof it's actually installed.
:::

Ubuntu 24.04's apt ships clangd 18.x, which is plenty (InlayHints, include-cleaner, External index, all there). If you insist on chasing the latest, adding LLVM's official apt source gets you 19/20, but for this tutorial it's unnecessary.

## vscode Remote-WSL: editor on Windows, work in WSL

The way vscode does C++ is a client/server architecture: the vscode UI runs on Windows, the processes doing the real work run in WSL, and the Remote-WSL extension bridges the two. Get this architecture straight, or you won't be able to diagnose any later problem.

On the Windows side, do two things:

- Install vscode (download from [code.visualstudio.com](https://code.visualstudio.com), normal next-next-next)
- In the vscode extension marketplace, search `WSL` (publisher Microsoft) and install it

With that done, there are two ways to open a project that lives in WSL:

First way, in the command palette (`F1` or `Ctrl+Shift+P`) type `Remote-WSL: New Window`, which spins up a new vscode window connected to WSL.

Second way, in a WSL terminal, `cd` into the project directory and type:

```bash
code .
```

The `code` command is injected into WSL's PATH automatically once the Remote-WSL extension is installed. It launches the Windows-side vscode and treats the current directory as the workspace.

::: details Why `code .` works at all
Remote-WSL drops a `code` shell script into WSL (usually at `/usr/bin/code`). That script talks to the Windows-side vscode and tells it to launch and connect back. The first run pulls a vscode server component from Windows into WSL (`~/.vscode-server/`), and that server is the process that actually runs extensions, terminals, and language servers. Subsequent opens are instant.
:::

Once connected, look at the bottom-left corner of the vscode window. You should see a green or blue badge reading `WSL: Ubuntu`. That means every file operation, terminal, and extension in this window is running in WSL.

Now the biggest trap for newcomers: **vscode extensions install on both sides**. Windows-side extensions handle UI (themes, icons, keybindings); WSL-side extensions handle the Linux work (code understanding, debugging, building). Once Remote-WSL connects, the extensions panel splits into "LOCAL - INSTALLED" (Windows side) and "WSL: UBUNTU - INSTALLED" (WSL side). The clangd, C/C++, and CMake Tools extensions you want all have to go into the WSL column (click "Install in WSL: Ubuntu" next to each).

```text
Extensions panel (after connecting to WSL)
├── LOCAL - INSTALLED        ← Windows side: themes, icons, Remote-WSL itself
│   ├── Remote - WSL  ✓
│   ├── Material Icon Theme
│   └── ...
└── WSL: UBUNTU - INSTALLED  ← WSL side: install clangd / C/C++ / CMake Tools here
    ├── clangd           ← code understanding (completion / jump-to-def / errors)
    ├── C/C++            ← debugging (keep cppdbg, turn IntelliSense off)
    └── CMake Tools      ← CMake configure / build / kit selection (optional)
```

The clangd extension has to go on the WSL side. It calls the `clangd` binary inside WSL, it reads `compile_commands.json` from inside WSL, all of it is on the Linux side. Install it on the Windows side by mistake and it'll go looking for `clangd.exe` on Windows, which it absolutely will not find.

## clangd configuration in depth

[Getting-started piece 5](/getting-started/05-vscode-clangd) installed clangd and killed the red squiggles, but covered only three steps: turn on `CMAKE_EXPORT_COMPILE_COMMANDS`, install the extension, switch off the C/C++ extension's IntelliSense. This piece fills in the rest: how clangd finds compile_commands, what every field in `.clangd` does, how clang-tidy plugs in, how to turn on include-cleaner.

### Where compile_commands.json comes from

clangd's work depends on a file called `compile_commands.json`. This is the Compilation Database format defined by the Clang community: one record per `.cpp` in the project, recording the full command used to compile it, the compiler path, the `-std=` standard, all the `-I` header search paths. With that file, clangd can "stand where the compiler stands" and look at the code, knowing which header `std::vector` comes from and which features are available under `-std=c++17`.

CMake makes this trivial, one line. After `project()` in `CMakeLists.txt`, add:

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

Or, if you'd rather not touch `CMakeLists.txt`, pass `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` on the configure command line. After configure, `build/compile_commands.json` is generated.

::: warning This switch only works for Makefile / Ninja generators
`CMAKE_EXPORT_COMPILE_COMMANDS` only emits `compile_commands.json` when you're using a Makefile or Ninja generator. The Visual Studio generator (`-G "Visual Studio 17 2022"`) and the Xcode generator don't support it. In WSL we default to Ninja, so this is a non-issue.
:::

After configure, `build/compile_commands.json` looks like this (real output from my machine):

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

One entry per `.cpp`. The `command` field is the load-bearing one: clangd parses it to get the compiler, the standard, the header paths, then understands the code from that viewpoint. So if you change `CMakeLists.txt` (say, adding a new `target_include_directories`), you have to reconfigure to refresh `compile_commands.json`, or clangd keeps using the old viewpoint, never learns the new header path, and the red squiggles come back.

### How clangd finds compile_commands.json

The official behavior is: clangd takes the source file you're editing, walks up its directory chain looking for `compile_commands.json`, and uses the first one it finds. So if your source is at `~/proj/src/foo.cpp`, clangd checks in this order:

```text
~/proj/src/compile_commands.json
~/proj/compile_commands.json
~/compile_commands.json
~/.../compile_commands.json
```

clangd 16 added one more rule: at each directory along the way, it also peeks at that directory's `build/` subdirectory for a `compile_commands.json`. This was added specifically as a convenience for CMake projects, since CMake writes the file into `build/` by default and clangd knows to look there.

I tested on clangd 22 with the source in `src/` and `compile_commands.json` in `build/`, no symlink at the project root, and clangd still found it:

```text
I[11:23:59.322] Loading compilation database...
I[11:23:59.323] Loaded compilation database from /tmp/clangd-search-test/build/compile_commands.json
```

So in the default case you don't need to do anything. But two situations still call for pointing at it manually.

First situation: you use multiple build directories (say `build-debug/` and `build-release/`), and clangd can't tell which to pick and may bounce between them. Pin it down in `.clangd`:

```yaml
CompileFlags:
  CompilationDatabase: build-debug
```

The `CompilationDatabase` field takes a directory path (relative to the project root), or `Ancestors` (the default behavior, walk up + peek into `build/`), or `None` (turn it off, fall back only).

Second situation: an old clangd (15 or earlier) doesn't have the "peek into `build/`" rule and really only walks parent directories looking for `compile_commands.json` at the root. In that case the project root needs a symlink:

```bash
ln -sf build/compile_commands.json compile_commands.json
```

When clangd walks up, it hits the symlink at the root and follows it to the real file in `build/`. New clangd doesn't need this, but it does no harm and keeps old clangd happy.

### The .clangd config file, field by field

`.clangd` is clangd's project-level config, YAML format, placed at the project root. clangd walks up the source file's directory chain looking for `.clangd`, merges every hit in order, and the one closest to the source file wins. The config below is what I use in practice (also in the repo at `code/examples/vol7/wsl-clangd/.clangd`); I'll walk through each section and what it does:

```yaml
CompileFlags:
  Add: [-Wall, -Wextra, -Wno-unused-parameter]
  Remove: [-fsanitize=thread]
  Compiler: clang++
  CompilationDatabase: build
```

The `CompileFlags` section post-processes the compile command out of `compile_commands.json`. `Add` appends flags to every command: `-Wall -Wextra` makes clangd's diagnostics as strict as a real compile, and `-Wno-unused-parameter` lets it ignore the kind of parameter that has to exist but doesn't get used (typical in callbacks). `Remove` wipes flags via wildcard; the canonical case is `-fsanitize=thread` showing up in `compile_commands.json` (TSan, covered in volume 5), which clangd doesn't need to re-run and re-running it produces bizarre diagnostics. `Compiler` swaps the compiler executable name for a specified value; writing `clang++` makes clangd use Clang's own driver to probe system headers and ABI, which is especially handy in cross-compilation (when the original compiler is `arm-none-eabi-g++` and clangd can't find the sysroot, swapping in `clang++` with `--query-driver` fixes it). `CompilationDatabase` was covered above, the directory holding compile_commands.

```yaml
Index:
  Background: Build
  StandardLibrary: Yes
```

The `Index` section governs clangd's index. `Background: Build` turns on the background index (the thing grinding away the first time you open a project), and the index lands on disk under `~/.cache/clangd/index/` and gets reused next time you open the same project, so it doesn't start from scratch. `StandardLibrary: Yes` folds the standard library symbols into the index, so typing `std::` actually completes `vector`, `cout`, and friends. Both are on by default; spelling them out is just for explicitness.

```yaml
InlayHints:
  Enabled: Yes
  ParameterNames: Yes
  DeducedTypes: Yes
  Designators: Yes
  BlockEnd: Yes
```

`InlayHints` is clangd 18+'s inline hints, gray dashed text rendered right inside the code line. `ParameterNames: Yes` shows the parameter name at call sites, `greet(/*name=*/"WSL")`, so you don't have to keep flipping back to the declaration to check what a parameter is called. `DeducedTypes: Yes` shows the type `auto` deduced, `auto /*= int*/ sum`. `Designators: Yes` shows field names in aggregate initialization, `Point{/*.x=*/1, /*.y=*/2}`. `BlockEnd: Yes` shows what a closing `}` belongs to (which function, which namespace), so the `}` at the end of a multi-thousand-line function is no longer a mystery. The vscode clangd extension doesn't enable this group by default; turning it on lifts code readability a noticeable step.

```yaml
Diagnostics:
  ClangTidy:
    Add: [modernize-*, bugprone-*, performance-*, readability-*]
    Remove: [modernize-use-trailing-return-type, readability-magic-numbers]
  UnusedIncludes: Strict
  MissingIncludes: Strict
  Suppress: [unused-includes]
```

The `Diagnostics` section governs the red/yellow squiggles. `ClangTidy.Add/Remove` makes clangd run clang-tidy checks right in the editor, no terminal needed. With `modernize-*` on, writing `NULL` prompts a suggestion to use `nullptr`, writing `for (int i = 0; i < v.size(); ++i)` prompts a suggestion to use a range-based for. `Remove` silences noisy checks: `modernize-use-trailing-return-type` forces the `auto foo() -> int` style, the community has argued about it for years, and most projects don't want it. `UnusedIncludes: Strict` and `MissingIncludes: Strict` turn on clangd's built-in include-cleaner, flagging both "included but unused" and "used but not included". **When you're new to a project, leave these two off first**, or one toggle lights the screen up with yellow squiggles and makes you want to uninstall clangd outright. `Suppress` silences a specific diagnostic code, more precise than toggling a check.

```yaml
Hover:
  ShowAKA: Yes
```

The `Hover` section governs the mouse-hover tooltip. `ShowAKA: Yes` makes typedef/using aliases show the underlying type on hover, so hovering over `size_type` reveals `std::size_t` underneath.

### Key items in the clangd extension's settings.json

The `.clangd` file controls the clangd program's behavior. The vscode clangd extension has its own set of options in `settings.json`. Below are the key items that pair with `.clangd` (the full version is at `code/examples/vol7/wsl-clangd/.vscode/settings.json`):

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

`C_Cpp.intelliSenseEngine: disabled` is the core step from piece 5, switching off the C/C++ extension's code understanding so clangd owns it. `clangd.arguments` is the command-line args clangd starts with. `--background-index` explicitly turns on the background index, `--clang-tidy` turns on the clang-tidy integration (paired with `.clangd`'s `Diagnostics.ClangTidy` and the `.clang-tidy` file), `--header-insertion=iwyu` makes completion auto-add the `#include`, `--all-scopes-completion` lets completion cross namespace boundaries (you can complete global symbols from inside a namespace), `--function-arg-placeholders` makes function completion carry parameter placeholders, `--pch-storage=disk` writes PCH to disk to save memory, `--inlay-hints` enables the inline hints (clangd 18+), `--j=4` is the background parallelism.

`clangd.onConfigChanged: restart` is the load-bearing one: when you change `.clangd`, clangd restarts itself and picks up the new config. Without it, every `.clangd` edit needs a manual `Ctrl+Shift+P` → `clangd: Restart language server` to take effect.

### Background Index: a slow first open on a big project is normal

Open a project with tens of thousands of lines and clangd will pin the status bar spinning for several minutes after startup. That's the background index running: it's parsing every source file, extracting symbols and reference relationships, and writing them to `~/.cache/clangd/index/`. After the first run, the index gets reused and the second open is fast.

To verify it's actually doing work, look at the clangd output panel (`View → Output → clangd`); you'll see logs like this:

```text
I[15:32:11.456] Indexing xxx.cpp
I[15:32:11.612] Indexed preamble symbols: 1240
I[15:32:11.738] Background: 1450 indexed, 0 dirty
```

If the project is genuinely huge (something like Chromium), the index can eat several GB of memory. If your machine can't take it, turn off the background index with `Background: Skip` or `--background-index=0`. The cost is slower cross-file jumps and completion, since no cross-file index gets built. For most projects, leaving it on is fine.

### clang-tidy integration

clangd's built-in clang-tidy integration puts static checks directly in the editor, no terminal switching. The way it works:

Drop a `.clang-tidy` file (YAML) at the project root listing which checks to enable:

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

The first item in `Checks`, `-*`, turns off all default checks; after that, globs like `modernize-*` turn groups on. The `-` prefix means off. `HeaderFilterRegex` decides which headers clang-tidy inspects; `.*` means all of them, and you'd narrow it to a regex matching only your own headers if third-party libraries generate too much noise.

clangd reads this file automatically at startup. With `--clang-tidy` on in `settings.json`, every line you edit, clangd runs the relevant clang-tidy checks alongside, and problems get drawn as yellow/red squiggles in the editor.

I tested a snippet that triggers `readability-identifier-length`:

```text
$ cat tidy_demo.cpp
#include <cstdint>
int main() {
    int big = 1000000000;
    long narrowed = big;
    int* p = nullptr;   // ← name too short, under 3 chars gets flagged by the check
    return 0;
}

$ clang-tidy -p build tidy_demo.cpp
... tidy_demo.cpp:5:10: warning: variable name 'p' is too short,
    expected at least 3 characters [readability-identifier-length]
    5 |     int* p = nullptr;
      |          ^
```

The same diagnostic shows up in vscode as a yellow squiggle under the variable name `p`, with `[readability-identifier-length]` on hover. With the clangd integration, you don't open a terminal; you write the code and the problem just appears.

### include-cleaner

clangd's built-in include-cleaner (no external clang-tidy needed) targets exactly two include pathologies: included but unused, and used but not included. The switches live in the `Diagnostics` section of `.clangd`:

```yaml
Diagnostics:
  UnusedIncludes: Strict   # None = off, Strict = strict on
  MissingIncludes: Strict
```

My advice: **for new projects, turn it on from day one** so include hygiene is clean from the source; **for taking over an old project, start with `None`**, since legacy code carries heavy include baggage and flipping to Strict lights up the screen with yellow and robs you of judgment. Tidy the code first, then turn it on.

include-cleaner also supports IWYU pragmas, written in headers to instruct the tool:

```cpp
#include <vector>  // IWYU pragma: export
#include "detail_helpers.h"  // IWYU pragma: keep  ← don't flag this even if unused
```

`export` says "this header includes `<vector>` on behalf of users, so users don't need to include it themselves"; `keep` says "don't mark this include as unused". Both pragmas see heavy use in large libraries to suppress false positives from include-cleaner.

### clangd or the C/C++ extension (aligned with the getting-started piece)

At this point you might ask: should I uninstall the C/C++ extension? No. Consistent with [getting-started piece 5](/getting-started/05-vscode-clangd):

- clangd handles "understanding the code": completion, jump-to-def, errors, hover, inlay hints, clang-tidy. Accurate.
- The C/C++ extension stays for "debugging": breakpoints, stepping, variable inspection, call stack. Its `cppdbg` debugger is the most mature gdb/lldb solution in vscode.

So `C_Cpp.intelliSenseEngine: disabled` switches off the C/C++ extension's code understanding; the extension itself stays installed. The two divide labor and don't fight. The debugging below uses the C/C++ extension's `cppdbg`.

## Debug configuration: launch.json

Once the project builds and clangd can jump around, the last link is debugging: set breakpoints, step, inspect variables. This section finishes the spot where the original draft cut off at "switch to the debug panel and click".

Debugging C++ in vscode goes through `.vscode/launch.json`. Here's a complete, working config (also in the repo at `code/examples/vol7/wsl-clangd/.vscode/launch.json`), using the C/C++ extension's `cppdbg` + gdb:

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

Field by field. `type: cppdbg` is the debugger type the C/C++ extension provides, driving gdb over gdb's MI protocol. `program` is the full path to the executable you're debugging; `${workspaceFolder}` is the project root vscode currently has open. `MIMode: gdb` paired with `miDebuggerPath: /usr/bin/gdb` tells it to use the gdb inside WSL. `preLaunchTask: build` runs a task named `build` (defined in tasks.json below) before F5 fires; if the build fails, debugging doesn't start, saving you from debugging a stale binary.

The `-enable-pretty-printing` in `setupCommands` is the key item. Without it, when you break on a `std::vector<int> v{1,2,3,4,5}`, the variables panel shows a pile of raw members (`_M_start`, `_M_finish`, `_M_end_of_storage`, those libstdc++ internal pointers) and you have no way to tell the vector actually holds `{1,2,3,4,5}`. With it on, gdb uses its Python pretty-printers to format it into something readable. Here's the real gdb output comparison on my machine:

```text
(gdb) print nums        # nums is std::vector<int>{1,2,3,4,5}

Without pretty-printing:   $1 = {_M_impl = {_M_start = 0x555..., _M_finish = ..., _M_end_of_storage = ...}}
With pretty-printing:      $1 = std::vector of length 5, capacity 5 = {1, 2, 3, 4, 5}
```

Once `setupCommands` is wired up, the variables panel shows the readable second form. This is the step newbies miss most often: debugging works but variables are unreadable, so the breakpoint might as well not be there.

::: tip CodeLLDB as an alternative
If you prefer lldb, install the CodeLLDB extension (`vadimcn.vscode-lldb`) plus `sudo apt install lldb` in WSL, and switch launch.json to `"type": "lldb"`. CodeLLDB doesn't go through the MI protocol; it drives lldb directly, starts faster, and renders C++ types more nicely (no pretty-printing config needed, it's built in). This tutorial standardizes on gdb, though, so the examples below all assume gdb.
:::

With that configured, click in the gutter to the left of `main.cpp` line 14 (the `for (int x : nums)` line) to set a red breakpoint, then press `F5`. vscode first runs the `build` task to recompile, then launches gdb to load `build/greeter`, and stops at the breakpoint. The Run and Debug panel on the left shows the call stack, variables, breakpoints, and watch. Expand `nums` in the variables panel and you get `std::vector of length 5, capacity 5 = {1, 2, 3, 4, 5}`, and `sum` is the current accumulated value. `F10` steps over, `F11` steps into, `F5` continues.

## tasks.json build tasks

The `preLaunchTask: build` in launch.json needs a matching task. Tasks live in `.vscode/tasks.json`:

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

Three tasks, each with a job. `build` runs the incremental build (`cmake --build build`, with Ninja underneath); it's the default build task (`isDefault: true`), so `Ctrl+Shift+B` triggers it directly. `configure` runs on first build or after you've changed `CMakeLists.txt`, reconfiguring once to refresh `compile_commands.json`. `rebuild` uses `dependsOrder: sequence` to run configure then build in order, all in one shot.

`problemMatcher: ["$gcc"]` makes vscode parse the compiler output, turning errors/warnings into clickable items in the Problems panel, where a click jumps to the corresponding line. This is vscode's built-in `$gcc` matcher, which matches the gcc/clang error format.

The chain triggered by F5 in launch.json is: run the `build` task → build succeeds → launch gdb to load `build/greeter` → run to the breakpoint and stop. The whole debug loop closes up, with no need to flip over to a terminal and type `cmake --build` each time.

## Where this leaves you

With WSL2 + vscode + clangd + cppdbg all wired up, your C++ engineering environment is barely distinguishable from what a seasoned Linux developer uses: accurate completion, fast jumps, strict errors, and debugging that can actually show a vector. From here, reading volume 7 ch00's CMake series (the target mental model, CMakePresets.json) and volume 6's memory safety (AddressSanitizer + valgrind), all the commands go straight into the WSL terminal and the output matches what's in the articles.

Every config file that goes with this piece (`.clangd`, `.clang-tidy`, `.vscode/settings.json`, `launch.json`, `tasks.json`) lives in the repo at `code/examples/vol7/wsl-clangd/`. Clone it and it runs out of the box. The CMake project is minimal and reproducible: `cmake -B build -G Ninja && cmake --build build` produces `build/greeter`, and F5 drops you into the debugger.
