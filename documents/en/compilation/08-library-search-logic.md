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
title: 'Deep Dive into C/C++ Compilation and Linking: Part 8 — Library File Search
  Logic'
description: ''
translation:
  source: documents/compilation/08-library-search-logic.md
  source_hash: 6eed2c129945498236a9d7add1cb0de4b828f96f8824076ecbb0ae07c136cd72
  translated_at: '2026-06-16T03:27:54.024247+00:00'
  engine: anthropic
  token_count: 1227
---
# Deep Dive into C/C++ Compilation and Linking: Part 8 — Library File Search Logic

## Introduction

Now, we need to discuss how to locate library files. Locating library files refers to how an executable file that depends on other dynamic libraries finds those libraries.

This is no small issue. If we think about it, in modern software engineering, we can hardly escape the use of libraries. For example, the software we make or use often integrates third-party libraries into the product, or relies on package management models. To ensure a given piece of software runs correctly, we need to locate the correct library files at runtime.

It's basically that.

## Naming Rules

On Linux, dynamic libraries follow naming conventions. If you pay attention, you will notice that all static libraries conform to the `lib<name>.a` pattern. In this case, we only need to tell the linker the `<name>` part, and the linker will automatically search for the full file name based on other rules.

Dynamic libraries are slightly more complex because they support hot-swapping (meaning the software can be updated without recompiling). Consequently, the naming rules are a bit more complex. Simply put:

`lib<name>.so.<major>.<minor>.<patch>`

Similarly, we only provide the `<name>` part, and the linker will automatically find the rest based on other rules.

The version numbers deserve a separate discussion. Generally, the version number consists of: `major.minor.patch`. This is the specific name. There is also a concept called `soname`, which is the name of the dynamic library retaining only the major version number. For example, the `soname` of `libz.so.1.2.3.4` is `libz.so.1`. This example comes from *Advanced C/C++ Compilation Technology*.

## Dynamic Library Location Rules at Runtime

Now let's talk about the runtime location rules for dynamic library files. Specifically, you might be interested in the rules on Linux. Here is the breakdown. When running a dynamically linked program on Linux, a component called the **dynamic linker/loader** (usually `ld-linux.so` or `ld.so`) is responsible for finding and loading the shared libraries (`.so` files) required by the executable.

The search rules for dynamic libraries look complex, but they actually have clear priorities and several common "control points": `LD_PRELOAD`, embedded `DT_RPATH`/`DT_RUNPATH` in the executable, environment variables like `LD_LIBRARY_PATH`, system configuration (`/etc/ld.so.conf` + `ldconfig`), and system default paths (like `/lib`, `/usr/lib`).

Here is what you need to know: **when the dynamic linker needs to resolve a dependency** (i.e., the dependency name does not contain a `/`), it usually searches in the following order (simplified):

1. Libraries specified by `LD_PRELOAD` (loaded first, used for symbol overriding/injection).
2. If the executable contains `DT_RPATH` and does *not* contain `DT_RUNPATH`, the `DT_RPATH` paths are used (Note: `DT_RPATH` is deprecated but still supported).
3. The environment variable `LD_LIBRARY_PATH` (**ignored for setuid/setgid executables**).
4. If the executable contains `DT_RUNPATH`, use those paths (and when `DT_RUNPATH` exists, `LD_LIBRARY_PATH` is generally ignored).
5. The cache maintained by `ldconfig` (`/etc/ld.so.cache`), as well as `/lib`, `/usr/lib` (and architecture-specific directories like `/lib64`, `/usr/lib64`), known as "trusted directories".
6. (If nothing is found above) It will ultimately fail and report an error (e.g., `error while loading shared libraries`).

> Note: The details of the above order (especially the interaction between `DT_RPATH` and `DT_RUNPATH`) are influenced by the linker implementation and linker options (such as `--enable-new-dtags`, the identifier that enables `-R` or `-rpath` linker directives).

------

## Detailed Explanation (Item by Item)

#### LD_PRELOAD ("Inject" or Override Symbols on Demand)

`LD_PRELOAD` is an environment variable that can specify one or more shared libraries to be forcibly loaded into the process **before the normal search**. This allows for intercepting or replacing symbols (functions). However, this is rare and generally not recommended unless you know exactly what you are doing :)

------

#### DT_RPATH and DT_RUNPATH (i.e., "rpath / runpath")

At link time, one or more runtime library search paths can be written into the dynamic segment (`.dynamic`) of the executable or shared library. The corresponding ELF tags are `DT_RPATH` and `DT_RUNPATH`. Historically, `DT_RPATH` was introduced early with the behavior of "taking priority over environment variables," but later `DT_RUNPATH` was introduced (via `new-dtags`). The implication of `DT_RUNPATH` is: **it is searched *after* `LD_LIBRARY_PATH`**, meaning `LD_LIBRARY_PATH` can override paths in `RUNPATH`. Conversely, `DT_RPATH` in some implementations/historically takes priority over `LD_LIBRARY_PATH` (making it harder to override).

Another important behavioral difference: **DT_RPATH is effective for transitive dependencies**, whereas **DT_RUNPATH may not be used to find transitive dependencies** (i.e., when executable -> libA -> libB, RUNPATH behavior in some cases will not provide a path for finding libB, while RPATH will). This leads to situations where combinations that worked with RPATH under older linkers result in "cannot find indirect dependency" errors when using RUNPATH (new-dtags).

In my experience with Linux, I rarely encounter this, so I suggest that in most test environments, the solution below is appropriate.

------

#### LD_LIBRARY_PATH (The Environment Variable)

`LD_LIBRARY_PATH` is a list of runtime library search paths used by the dynamic linker at specific stages (see order). It is very commonly used to temporarily override system paths or test new library versions. **Similarly**, setuid/setgid executables ignore this variable for security reasons.

The trouble with environment variables is that they easily interfere with all child processes started by a shell that sets this variable. It is not recommended to rely on `LD_LIBRARY_PATH` in production environments for long periods, as it affects all child processes started via that shell and is less maintainable than system configuration (`ldconfig`).

```text
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
```

------

#### ldconfig, /etc/ld.so.conf.d, and ld.so.cache

System administrators usually tell `ldconfig` which directories should be trusted by the system dynamic linker by placing library directories in `/etc/ld.so.conf` or `/etc/ld.so.conf.d`. `ldconfig` scans these directories and generates a binary cache `/etc/ld.so.cache` (to improve lookup speed) and creates symbolic links (`libXXX.so -> libXXX.so.VERSION`). The dynamic linker reads this cache to speed up lookups.

Common operations:

```text
sudo ldconfig /path/to/new/lib/dir
```

------

#### System Default Directories (Trusted Directories)

The dynamic linker usually defaults to searching `/lib`, `/usr/lib` (and `/lib64`, `/usr/lib64` on 64-bit systems). These are called "trusted directories". `ldconfig` also handles these directories. Even if a path is not written into `/etc/ld.so.conf`, placing a library in these directories usually allows it to be found (but pay attention to architecture bits, ABI, and version matching).

## What About Windows?

Windows executable/loaders and APIs (`LoadLibraryW` / `LoadLibraryA` / automatic loading via the import table) define a set of search orders and security improvements.

Generally speaking, Windows has two methods: implicit (import table) and explicit (runtime API).

**Implicit loading** means the executable's Import Table is resolved by the system loader when the process starts or when a module is loaded. The system attempts to find and map each `.dll` into the process address space. Developers specify dependencies at the link stage (e.g., `pragma comment(lib, "...")` or linker inputs), and loading is completed automatically by the system at process startup.

**Explicit loading** means the code manually loads a DLL at runtime using APIs like `LoadLibraryW` or `LoadLibraryA`, then obtains function pointers with `GetProcAddress`. Explicit loading allows control over search behavior via parameters (e.g., using the `LOAD_WITH_ALTERED_SEARCH_PATH` flag).

#### Default Search Order (Conceptual Order)

> Note: Windows' search order has subtle differences across OS versions and configurations, and the system provides settings to influence this order (discussed below). Here is a conceptual common order (priorities are what matter):

When a process requests loading a DLL named `foo.dll` (without an absolute path), the system usually searches in the following order (conceptual):

1. **Full path explicitly specified by the caller** (if calling `LoadLibrary("C:\\path\\to\\foo.dll")`, it loads directly without searching).
2. **The loader first checks if it is a "KnownDLLs" entry** (KnownDLLs are a set of trusted system libraries registered in the system, prioritizing the existing system version).
3. **Application Directory**: The directory where the executable (`.exe`) resides (usually prioritized over the system directory, subject to settings like `SafeDllSearchMode`).
4. **System Directory** (usually `C:\Windows\System32`).
5. **Windows Directory** (usually `C:\Windows`).
6. **Current Working Directory** (depends on `SafeDllSearchMode`; if "Safe Search Mode" is enabled, the current directory is pushed later in the order).
7. **Directories listed in the PATH environment variable** (in order).
8. **If Application Configuration or Side-by-side (SxS)/manifest features are enabled**, it prioritizes resolving the binding version declared in the manifest or parallel assemblies from WinSxS.

The key point is: **if you use an absolute path or a path relative to the executable file, the system will not search PATH**; conversely, if only a bare name like `foo.dll` is given, it will try the order above.
