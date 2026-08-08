---
chapter: 13
difficulty: intermediate
order: 8
platform: host
reading_time_minutes: 8
tags:
- cpp-modern
- host
- intermediate
title: "Deep Dive into C/C++ Compilation and Linking, Part 8: Library Search Logic"
description: 'How an executable actually finds the dynamic libraries it depends on at runtime, in priority order: LD_PRELOAD, RPATH/RUNPATH, LD_LIBRARY_PATH, the ldconfig cache, system default directories, and the corresponding search rules on Windows.'
cpp_standard: [11, 14, 17, 20]
---
# Deep Dive into C/C++ Compilation and Linking, Part 8: Library Search Logic

## Intro

Now we need to talk about how libraries get located. "Locating a library" means — given an executable that depends on a bunch of other dynamic libraries besides itself, how does it actually go about finding those other dynamic libraries?

This is not a small question. Think about it: in modern software engineering we basically can't escape using libraries. For instance, software we build, or software we use, will integrate third-party libraries into the product, or — in the package-manager style — to make a given program run correctly we have to locate the right library files at runtime.

That's basically it.

## Naming Rules

Dynamic libraries on Linux follow a naming convention. If you pay attention you'll notice that all static libraries are `lib + <library_name> + .a`. At that point we just need to tell the linker the `<library_name>` part, and the linker will go look for `lib<library_name>.a` automatically following the rest of its rules.

Dynamic libraries are a tiny bit more complicated, because they have the hot-swap property (you can ship a new version without rebuilding the whole program from scratch), so the naming rule ends up a little more involved. Put simply:

`lib + <library_name> + .so + <library version information>`

Same as before — we only provide the `<library_name>` part, and the linker figures out the rest.

The `<library version information>` is worth a section of its own. Generally a version number is enough: `<M>.<m>.<p>`, that is, major, minor, and patch. That's the concrete name. Then there's also the thing called the soname — the dynamic-library name that only keeps the major version. So: the soname of `libz.so.1.2.3.4` is `libz.so.1`. This example comes from *Advanced C/C++ Compilation Techniques*.

## On the dynamic-library lookup rules at startup

Now we need to get into the runtime lookup rules for dynamic libraries. Specifically, people probably care most about the Linux runtime lookup rules, so let me lay them out. When you run a dynamically linked program on Linux, a component called the **dynamic linker / loader** (usually `ld-linux.so` / `ld.so`) is the one responsible for finding and loading the shared libraries (`.so`) that the executable needs. The lookup rules look complex, but they actually have a clear priority order and a handful of familiar "control points": `LD_PRELOAD`, the `RPATH`/`RUNPATH` baked into the executable, the `LD_LIBRARY_PATH` environment variable, system config (`/etc/ld.so.conf.d` + `ldconfig`), and the system default paths (like `/lib`, `/usr/lib`).

Here's what you need to keep in mind: **when the dynamic linker has to resolve a dependency** (i.e. the dependency name doesn't contain a `/`), it generally searches in this order (simplified):

1. `LD_PRELOAD`-listed libraries (loaded first, used for symbol override / injection).
2. If the executable has `DT_RPATH` and no `DT_RUNPATH`, the `DT_RPATH` paths are used (note: `DT_RPATH` is deprecated but still supported).
3. The `LD_LIBRARY_PATH` environment variable (**ignored for non-setuid/setgid executables**).
4. If the executable has `DT_RUNPATH`, that's used (and when `DT_RUNPATH` is present, `DT_RPATH` is generally ignored).
5. The cache maintained by ldconfig at `/etc/ld.so.cache`, plus `/lib`, `/usr/lib` (and the arch-specific `/lib64`, `/usr/lib64`) — the so-called "trusted directories".
6. (If nothing above matched) it ultimately fails with an error (e.g. `ld.so: cannot find ...`).

> Note: the exact details of the ordering above — especially the interaction between `RPATH` and `RUNPATH` — depend on the linker implementation and linker options (like `--enable-new-dtags`, which is what enables the `-R` / `-rpath` linker directives).

------

## In detail (each item expanded)

#### LD_PRELOAD (inject or override symbols, on demand)

`LD_PRELOAD` is an environment variable that lets you specify one or more shared libraries to be force-loaded into the process **before** the normal search, so you can intercept / replace symbols (functions). Honestly this is pretty rare, and generally not recommended unless you know exactly what you're doing :)

------

#### DT_RPATH and DT_RUNPATH (i.e. "rpath / runpath")

At link time you can write one or more runtime library search paths into the dynamic section (`.dynamic`) of the executable or shared library; the corresponding ELF tags are `DT_RPATH` and `DT_RUNPATH`. The historical `DT_RPATH` was introduced early, with the semantics of "takes priority over the environment variable". Later `DT_RUNPATH` (new-dtags) was introduced, and its meaning is: **it's searched after `LD_LIBRARY_PATH`**, meaning `LD_LIBRARY_PATH` can override paths in RUNPATH; whereas `DT_RPATH`, in some implementations / historically, takes priority over `LD_LIBRARY_PATH` (i.e. it's harder to override).

Another important behavioral difference: **DT_RPATH works for transitive dependencies**, whereas **DT_RUNPATH may not be used to look up transitive dependencies** (meaning, when you have executable -> libA -> libB, RUNPATH's behavior in certain cases won't provide a path for finding libB, while RPATH will). This is exactly why some combinations that ran fine under an older linker with RPATH start showing "can't find indirect dependency" errors once they're built with RUNPATH (new-dtags).

In my own Linux experience I've genuinely run into this very rarely, so for most testing scenarios I'd say the recommendation below is the safe bet.

------

#### LD_LIBRARY_PATH (this one's an environment variable)

`LD_LIBRARY_PATH` is a list of runtime library search paths that the dynamic linker uses at a particular stage (see the ordering above). It's extremely common as a way to temporarily override system paths or test a new version of a library. **Same deal**: setuid / setgid executables ignore this variable (for security reasons).

The trouble with environment variables is they're really easy to leak into everything else spawned from the shell that has them set. I wouldn't recommend leaning on `LD_LIBRARY_PATH` long-term in production — it affects every child process started from that shell, and it's nowhere near as maintainable as the system config (ldconfig).

```bash
export LD_LIBRARY_PATH=/opt/foo/lib:/home/you/sw/lib:$LD_LIBRARY_PATH
./myapp

```

------

#### ldconfig, /etc/ld.so.conf.d, and ld.so.cache

Sysadmins usually tell `ldconfig` which directories the system dynamic linker should trust, by dropping a library directory into `/etc/ld.so.conf` or `/etc/ld.so.conf.d/*.conf`. `ldconfig` scans those directories and produces a binary cache at `/etc/ld.so.cache` (to speed up lookups), and at the same time creates the symlinks (`libXXX.so` -> `libXXX.so.VERSION`). The dynamic linker reads that cache to make lookups fast.

Common operations:

```bash

# Add a new directory to the config (as root)
echo "/opt/foo/lib" > /etc/ld.so.conf.d/foo.conf

# Rebuild the cache
sudo ldconfig

# Inspect the cache contents
ldconfig -p | grep foo

```

------

#### System default directories (trusted directories)

The dynamic linker usually searches `/lib` and `/usr/lib` (and on 64-bit systems, `/lib64` and `/usr/lib64`) by default — these are the "trusted directories". `ldconfig` processes them too. Even if you haven't written a path into `ld.so.conf`, dropping a library into these directories will usually get it found (just watch the arch bits, the ABI, and the version match).

## So what about us on Windows?

Windows' executables / loader and APIs (`LoadLibrary` / `LoadLibraryEx` / auto-loading via the import table) define their own search order and security improvements.

Generally speaking, Windows has two flavors: implicit (import table) and explicit (runtime API).

**Implicit loading** means the executable's Import Table gets resolved by the system loader at process startup or when a module is loaded — the system tries to find each `DLL` and map it into the process address space. The developer specifies the dependencies at link time (e.g. `kernel32.dll`, `mydll.dll`), and the loading is done automatically by the system at process startup.

**Explicit loading** means the code uses APIs like `LoadLibrary` / `LoadLibraryEx` to manually load a DLL at runtime, and then grabs function pointers with `GetProcAddress`. Explicit loading lets you control the search behavior through parameters (e.g. flags like `LOAD_LIBRARY_SEARCH_USER_DIRS`).

#### Default search order (conceptual order)

> Note: Windows' search order has subtle differences across OS versions and configurations, and the system provides settings that influence this order (covered below). For now, here's a conceptual, commonly-seen order (the point is just to understand the priorities):

When a process asks to load a name like `foo.dll` (with no absolute path specified), the system generally searches in this order (conceptual):

1. **An explicit full path from the caller** (if you call `LoadLibrary("C:\\path\\foo.dll")`, that path is loaded directly — no search happens).
2. **The loader first checks whether it's an entry in "KnownDLLs"** (KnownDLLs is a set of trusted system libraries registered in the system; the already-present system version is preferred).
3. **The application directory (Executable directory)**: the directory the executable (`.exe`) lives in (this usually takes priority over the system directories, subject to settings like SafeDllSearchMode).
4. **The system directory** (usually `%SystemRoot%\System32`).
5. **The Windows directory** (usually `%SystemRoot%`).
6. **The current working directory** (depends on SafeDllSearchMode; if "safe search mode" is on, the current directory gets pushed further back).
7. **The directories listed in the PATH environment variable** (in order).
8. **If application config or Side-by-side (SxS) / manifest features are enabled**, the binding version declared in the manifest, or the side-by-side assembly from WinSxS, takes precedence.

The key point: **if you use an absolute path or a path relative to the executable, the system does not go searching PATH**; conversely, if you only hand it a bare name like `foo.dll`, it tries the order above.

## From a modern CMake perspective

All that manual fussing — `export LD_LIBRARY_PATH`, editing `/etc/ld.so.conf.d`, threading `-Wl,-rpath` — basically gets taken off your hands in a project managed by CMake. `target_link_libraries(myapp PRIVATE foo)` turns into `-lfoo` plus the right `-L` for you; `add_library(foo SHARED)` slaps `-fPIC` on the target by default, while `add_library(foo STATIC)` goes through `ar` for packing. On the runtime-lookup side, `set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")` together with `CMAKE_BUILD_WITH_INSTALL_RPATH` writes `$ORIGIN` into the ELF's `DT_RUNPATH`, so the executable you ship runs alongside its own directory and the user never has to pollute their shell's `LD_LIBRARY_PATH`. On Windows you point `RUNTIME_OUTPUT_DIRECTORY` at where the `.exe` is, dropping the DLLs right next to it, hitting the "application directory" priority rule dead-on. In other words, all the rules above are the low-level facts — CMake doesn't change any of them; it just turns "which flag to write, which folder to drop the library into" into a couple of lines of declarative config.
