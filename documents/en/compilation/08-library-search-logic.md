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
title: 'Deep Dive into C/C++ Compilation and Linking · Part 8: How Executables Find Their Libraries'
description: 'Spells out the priority order in which an executable finds the dynamic libraries it depends on at runtime: LD_PRELOAD, RPATH/RUNPATH, LD_LIBRARY_PATH, the ldconfig cache, system default directories, and the corresponding search rules on Windows'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/08-library-search-logic.md
  source_hash: 81be75c1ea1837bedaca692a48882aec82a01a052353d39e02e4b24f82a6e162
  translated_at: '2026-09-26T00:05:15+00:00'
  engine: anthropic
  token_count: 4900
---
# Deep Dive into C/C++ Compilation and Linking · Part 8: How Executables Find Their Libraries

## Preface

What we need to discuss now is the business of locating library files. "Locating a library file" means this: given an executable that depends on dynamic libraries other than itself, how does it go about finding those other dynamic library files?

This is no small question. Think it over: in modern software engineering we can hardly escape the use of library files. For instance, software we build ourselves — or software we use — integrates third-party libraries into the product; or, to take the package-management model as the representative case, in order for a given piece of software to run correctly, we need to locate the correct library files at runtime.

That is pretty much the story.

## Rules for Locating Libraries

Dynamic libraries on Linux do follow a naming convention. If you have been paying attention, you will have noticed that all static libraries satisfy `lib + <library_name> + .a` — at which point we only need to tell the linker the `<library_name>` part, and the linker will go looking for `lib<library_name>.a` automatically, following its other rules.

Dynamic libraries are a tiny bit more complicated. Because a dynamic library has the hot-swap property (that is, there is no need to rebuild and release the software from scratch), the naming rule is in practice a little more involved. To put it simply:

`lib + <library_name> + .so + <library version information>`

Same as before: we only provide the `<library_name>` part, and the linker will find things automatically, following its other rules.

The `<library version information>` deserves a discussion of its own. Generally speaking, a version number is enough: `<M>.<m>.<p>` — that is, the major version, the minor version, and the patch version. That is the concrete name. And then there is a thing called the soname: the dynamic-library name that keeps only the major-version information. That is: the soname of `libz.so.1.2.3.4` is libz.so.1. This example comes from *Advanced C and C++ Compiling*.

## Let's Talk About Dynamic Library Lookup at Startup

Now we need to get into the rules for locating dynamic libraries when a program starts running. In particular, what you are probably concerned with is the Linux runtime rule for locating dynamic libraries, so let me state it here. When you run a dynamically linked program on Linux, a component called the **dynamic linker / loader** (usually `ld-linux.so` / `ld.so`) is responsible for finding and loading the shared libraries (`.so`) the executable needs. The lookup rules for dynamic libraries look complicated, but they actually have a well-defined priority order and a handful of well-known "control points": `LD_PRELOAD`, the `RPATH`/`RUNPATH` embedded in the executable, the `LD_LIBRARY_PATH` environment variable, the system configuration (`/etc/ld.so.conf.d` + `ldconfig`), and the system default paths (such as `/lib`, `/usr/lib`).

Here is the part you need to know: **when the dynamic linker needs to resolve some dependency** (that is, the dependency name does not contain a `/`), it generally searches in the following order (simplified):

1. Libraries specified by `LD_PRELOAD` (loaded first, used for symbol overriding / injection).
2. If the executable contains `DT_RPATH` and no `DT_RUNPATH`, the `DT_RPATH` paths are used (note: `DT_RPATH` is deprecated, but still supported).
3. The `LD_LIBRARY_PATH` environment variable (**ignored for non-setuid/setgid executables**).
4. If the executable contains `DT_RUNPATH`, that is used (and when `DT_RUNPATH` is present, `DT_RPATH` is generally ignored).
5. The cache `/etc/ld.so.cache` maintained by ldconfig, together with `/lib`, `/usr/lib` (and the architecture-specific `/lib64`, `/usr/lib64`) — the "trusted directories".
6. (If nothing above was found) it ultimately fails with an error (such as `ld.so: cannot find ...`).

> Note: the fine details of the order above (especially the interaction between `RPATH` and `RUNPATH`) are affected by the linker's implementation and by linker options (such as `--enable-new-dtags`, which is the flag that enables the `-R` / `-rpath` linker directives).

------

## In Detail (Each Item Expanded)

#### LD_PRELOAD ("Injecting" or Overriding Symbols on Demand)

`LD_PRELOAD` is an environment variable with which you can designate one or more shared libraries to be force-loaded into the process **before the normal search** takes place, which makes it usable for intercepting / replacing symbols (functions). This one is rarely seen, though, and is generally not recommended — unless you know what you are doing :)

------

#### DT_RPATH and DT_RUNPATH (i.e. "rpath / runpath")

At link time, you can write one or more runtime library search paths into the dynamic section (`.dynamic`) of the executable or a shared library; the corresponding ELF tags are `DT_RPATH` and `DT_RUNPATH`, respectively. Historically, `DT_RPATH` was introduced early, with the usage "takes priority over the environment variable"; later, `DT_RUNPATH` (new-dtags) was introduced. What `DT_RUNPATH` means is: **it is searched after `LD_LIBRARY_PATH`** — that is, `LD_LIBRARY_PATH` can override the paths in RUNPATH; whereas `DT_RPATH`, in some implementations / historically, takes priority over `LD_LIBRARY_PATH` (that is, it is harder to override).

Another important behavioral difference: **DT_RPATH is effective for transitive dependencies**, whereas **DT_RUNPATH may not be used to look up transitive dependencies** (that is, with executable -> libA -> libB, RUNPATH's behavior in certain cases will not provide a path for finding libB, while RPATH will). This is why certain combinations that ran fine under an older linker with RPATH start showing "cannot find the indirect dependency" situations once they use RUNPATH (new-dtags).

In my own Linux experience so far I have genuinely run into this very rarely, so for the majority of testing setups, going with the scheme below is the appropriate choice.

------

#### LD_LIBRARY_PATH (this one is an environment variable)

`LD_LIBRARY_PATH` is a list of runtime library search paths, used by the dynamic linker at a particular stage (see the order). It is extremely commonly used to temporarily override the system paths, or to test a new version of a library. **Same story here**: setuid / setgid executables ignore this variable (for security reasons).

The trouble with an environment variable is that it very easily interferes with everything launched from a shell that has it set. I would not recommend making a production environment depend on `LD_LIBRARY_PATH` over the long term, because it affects every child process started from that shell, and it is not as maintainable as the system configuration (ldconfig).

```bash
export LD_LIBRARY_PATH=/opt/foo/lib:/home/you/sw/lib:$LD_LIBRARY_PATH
./myapp

```

------

#### ldconfig, /etc/ld.so.conf.d, and ld.so.cache

System administrators usually tell `ldconfig` which directories are to be trusted by the system dynamic linker, by putting library directories into `/etc/ld.so.conf` or `/etc/ld.so.conf.d/*.conf`. `ldconfig` scans those directories and produces a binary cache, `/etc/ld.so.cache` (to speed up lookups), and at the same time creates the symbolic links (libXXX.so -> libXXX.so.VERSION). The dynamic linker reads that cache to accelerate its search.

Common operations:

```bash

# Add the new directory to the configuration (as root)
echo "/opt/foo/lib" > /etc/ld.so.conf.d/foo.conf

# Rebuild the cache
sudo ldconfig

# View the cache contents
ldconfig -p | grep foo

```

------

#### System Default Directories (trusted directories)

The dynamic linker usually searches `/lib`, `/usr/lib` by default (plus `/lib64`, `/usr/lib64` on 64-bit systems), and these directories are called the "trusted directories". `ldconfig` processes these directories too. Even if you never write a path into `ld.so.conf`, placing a library into these directories will usually get it found (but watch out for the architecture bits, the ABI, and the version match).

## So What About Windows

Windows' executables / loader and APIs (`LoadLibrary` / `LoadLibraryEx` / automatic loading via the import table) define a search order of their own, along with security improvements.

Generally speaking, Windows has two ways: implicit (the import table) and explicit (runtime APIs).

**Implicit loading** refers to the executable's import table being resolved by the system loader at process startup or when a module is loaded; the system tries, for every `DLL`, to find it and map it into the process address space. The developer specifies the dependencies at the link stage (for example `kernel32.dll`, `mydll.dll`), and the loading is done automatically by the system at process startup.

**Explicit loading** refers to the code manually loading a DLL at runtime with APIs such as `LoadLibrary` / `LoadLibraryEx`, and then obtaining function pointers with `GetProcAddress`. Explicit loading lets you control the search behavior through parameters (for example, by using flags such as `LOAD_LIBRARY_SEARCH_USER_DIRS`).

#### Default Search Order (a Conceptual Order)

> Note: Windows' search order differs in fine detail across OS versions and configurations, and the system provides settings that influence this order (explained below). For now, here is a conceptualized, commonly seen order (the point is simply to understand the priorities):

When a process requests to load something named `foo.dll` (with no absolute path specified), the system usually searches in the following order (conceptual order):

1. **A full path explicitly specified by the caller** (if you call `LoadLibrary("C:\\path\\foo.dll")`, that path is loaded directly — no search takes place).
2. **The loader first checks whether it is an entry in "KnownDLLs"** (KnownDLLs is a set of trusted system libraries registered in the system; the version already present in the system is preferred).
3. **The application directory (Executable directory)**: the directory the executable (.exe) lives in (usually prioritized over the system directories; the specifics are affected by settings such as SafeDllSearchMode).
4. **The system directory** (usually `%SystemRoot%\System32`).
5. **The Windows directory** (usually `%SystemRoot%`).
6. **The current working directory (Current Directory)** (depends on SafeDllSearchMode; if "safe search mode" is enabled, the current directory's position gets pushed back).
7. **The directories listed in the PATH environment variable** (in order).
8. **If application configuration or the Side-by-side (SxS) / manifest features are enabled**, the binding version declared in the manifest, or the side-by-side assembly from WinSxS, gets resolved first.

The key point: **if you use an absolute path, or a path relative to the executable, the system does not go searching PATH**; conversely, if you only hand it the bare name `foo.dll`, it will try the order above.

## The Modern CMake Perspective

All that manual fussing — `export LD_LIBRARY_PATH`, editing `/etc/ld.so.conf.d`, passing `-Wl,-rpath` — basically gets taken off your hands in a project managed by CMake. `target_link_libraries(myapp PRIVATE foo)` turns into `-lfoo` plus the correct `-L` for you; `add_library(foo SHARED)` adds `-fPIC` to the target by default, while a static library, `add_library(foo STATIC)`, goes through `ar` for the packing. As for the runtime lookup part, `set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")` together with `CMAKE_BUILD_WITH_INSTALL_RPATH` writes `$ORIGIN` into the ELF's `DT_RUNPATH`, so the executable you distribute runs following its own directory — the user never has to pollute their shell's `LD_LIBRARY_PATH`. On Windows, you hand the job to `RUNTIME_OUTPUT_DIRECTORY`, which parks the DLLs right next to the `.exe`, hitting the "application directory" rule dead-on. In other words, the rules above are the low-level facts: CMake changes none of them; it just turns "which flag to write, which folder the library goes into" into a couple of lines of declarative configuration.
