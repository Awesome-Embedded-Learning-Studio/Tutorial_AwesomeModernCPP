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
  source_hash: 653d580380abeecf42980549a4ed90d728173508f3bbe9d6c2830b4e43a59d7b
  translated_at: '2026-06-24T00:25:49.615673+00:00'
  engine: anthropic
  token_count: 1227
---
# Deep Dive into C/C++ Compilation and Linking 8: Library File Search Logic

## Introduction

Now, we need to discuss the matter of locating library files. Locating library files refers to how an executable file that depends on other dynamic libraries finds those other dynamic libraries.

This is not a trivial issue. If you think about it carefully, in modern software engineering, we can hardly escape the use of library files. For example, software we make or use integrates third-party libraries into products, or in package management models, to ensure a given piece of software runs correctly, we need to locate the correct library files at runtime.

It is almost exactly like this.

## Naming Rules

Dynamic libraries on Linux follow naming conventions. If you pay attention, you will find that all static libraries satisfy `lib + <library_name> + .a`. In this case, we only need to tell the linker the `<library_name>` part, and the linker will automatically search for `lib<library_name>.a` according to other rules.

Dynamic libraries are slightly more complex. Because dynamic libraries support hot-swapping (meaning software can be released without recompiling from scratch), the naming rules are actually a bit more complex. Simply put:

`lib + <library_name> + .so + <library version information>`

Similarly, we only provide the `<library_name>` part, and the linker will automatically search for it according to other rules.

`<library version information>` is worth discussing separately. Generally speaking, the version number is sufficient: `<M>.<m>.<p>`, which stands for Major, Minor, and Patch version numbers. This is the specific name. There is also something called `soname`, which is the dynamic library name containing only the major version information. For example, the `soname` of `libz.so.1.2.3.4` is `libz.so.1`. This is an example from *Advanced C/C++ Compilation Technology*.

## Runtime Dynamic Library Location Rules

Now we need to talk about the runtime location rules for dynamic library files. Specifically, you might be interested in the runtime dynamic library location rules on Linux. Here is the explanation. When running a dynamically linked program on Linux, a component called the **dynamic linker/loader** (usually `ld-linux.so` / `ld.so`) is responsible for finding and loading the shared libraries (`.so`) required by the executable. The search rules for dynamic libraries look complex, but there are actually clear priorities and a few common "control points": `LD_PRELOAD`, `RPATH`/`RUNPATH` embedded in the executable, the environment variable `LD_LIBRARY_PATH`, system configuration (`/etc/ld.so.conf.d` + `ldconfig`), and system default paths (like `/lib`, `/usr/lib`).

In the following section, here is what you need to understand: **when the dynamic linker needs to resolve a dependency** (i.e., the dependency name does not contain `/`), it usually searches in the following order (simplified):

1. Libraries specified by `LD_PRELOAD` (loaded first, used for symbol overriding/injection).
2. If the executable contains `DT_RPATH` and does not contain `DT_RUNPATH`, use the `DT_RPATH` path (Note: `DT_RPATH` is deprecated but still supported).
3. The environment variable `LD_LIBRARY_PATH` (**ignored for non-setuid/setgid executables**).
4. If the executable contains `DT_RUNPATH`, use `DT_RUNPATH` (and when `DT_RUNPATH` exists, `DT_RPATH` is generally ignored).
5. The cache maintained by ldconfig `/etc/ld.so.cache`, and "trusted directories" like `/lib` and `/usr/lib` (as well as architecture-specific `/lib64`, `/usr/lib64`).
6. (If nothing is found above) It will ultimately fail and report an error (e.g., `ld.so: cannot find ...`).

> Note: The details of the order above (especially the interaction between `RPATH` and `RUNPATH`) are influenced by the linker implementation and linker options (such as `--enable-new-dtags`, which enables the `-R` or `-rpath` linker directive options).

------

## Detailed Explanation (Item Breakdown)

#### LD_PRELOAD (On-demand "Injection" or Symbol Overriding)

`LD_PRELOAD` is an environment variable that can specify one or more shared libraries to be forcibly loaded into the process **before the normal search**, thereby allowing the interception/replacement of symbols (functions). However, this is rare and generally not recommended unless you know what you are doing :)

------

#### DT_RPATH and DT_RUNPATH (i.e., "rpath / runpath")

At link time, one or more runtime library search paths can be written into the dynamic segment (`.dynamic`) of the executable or shared library. The corresponding ELF tags are `DT_RPATH` and `DT_RUNPATH`. Historically, `DT_RPATH` was introduced early with the usage of "precedence over environment variables," but later `DT_RUNPATH` (new-dtags) was introduced. The meaning of `DT_RUNPATH` is: **it is searched after `LD_LIBRARY_PATH`**, meaning `LD_LIBRARY_PATH` can override paths in RUNPATH; whereas `DT_RPATH` in some implementations/historically takes precedence over `LD_LIBRARY_PATH` (i.e., it is harder to override).

Another important behavioral difference: **DT_RPATH is effective for transitive dependencies**, whereas **DT_RUNPATH may not be used to find transitive dependencies** (i.e., when executable -> libA -> libB, the behavior of RUNPATH in some cases will not provide a path for finding libB, while RPATH will). This causes some combinations that worked with RPATH under older linkers to result in "cannot find indirect dependency" errors when using RUNPATH (new-dtags).

In my current Linux experience, I rarely encounter this, so in more test environments, I suggest adopting the following solution:

------

#### LD_LIBRARY_PATH (This is an Environment Variable)

`LD_LIBRARY_PATH` is a list of runtime library search paths used by the dynamic linker at specific stages (see order). It is very commonly used to temporarily override system paths or test new versions of libraries. **Similarly**, setuid / setgid executables will ignore this variable (for security reasons).

The trouble with environment variables is that they easily interfere with all shells that set this variable. It is not recommended to rely on `LD_LIBRARY_PATH` in production environments for a long time, because it affects all child processes started through that shell and is less maintainable than system configuration (ldconfig).

```bash
export LD_LIBRARY_PATH=/opt/foo/lib:/home/you/sw/lib:$LD_LIBRARY_PATH
./myapp

```

------

#### ldconfig, /etc/ld.so.conf.d, and ld.so.cache

System administrators typically inform `ldconfig` about which directories the system dynamic linker should trust by placing library directories in `/etc/ld.so.conf` or `/etc/ld.so.conf.d/*.conf`. `ldconfig` scans these directories and generates a binary cache at `/etc/ld.so.cache` (to improve lookup speed), while simultaneously creating symbolic links (libXXX.so -> libXXX.so.VERSION). The dynamic linker reads this cache to accelerate lookups.

Common operations:

```bash

# 把新目录加入配置（以 root）
echo "/opt/foo/lib" > /etc/ld.so.conf.d/foo.conf

# 重建缓存
sudo ldconfig

# 查看缓存内容
ldconfig -p | grep foo

```

------

#### System Default Directories (Trusted Directories)

The dynamic linker typically searches `/lib`, `/usr/lib` (and `/lib64`, `/usr/lib64` on 64-bit systems) by default. These are referred to as "trusted directories." `ldconfig` also processes these directories. Even if a path is not added to `ld.so.conf`, placing a library in these directories usually allows it to be found (provided the architecture bits, ABI, and version match).

## What About Windows?

The Windows executable loader and APIs (`LoadLibrary` / `LoadLibraryEx` / automatic loading via the import table) define a specific search order and security improvements.

Generally speaking, there are two approaches in Windows: implicit (import table) and explicit (runtime API).

**Implicit loading** refers to the system loader resolving the executable's Import Table when the process starts or a module is loaded. The system attempts to locate and map each `DLL` into the process's address space. Developers specify dependencies during the linking phase (e.g., `kernel32.dll`, `mydll.dll`), and loading is completed automatically by the system at process startup.

**Explicit loading** refers to code manually loading a DLL at runtime using APIs like `LoadLibrary` or `LoadLibraryEx`, and then retrieving function pointers with `GetProcAddress`. Explicit loading allows control over search behavior through parameters (for example, using flags like `LOAD_LIBRARY_SEARCH_USER_DIRS`).

#### Default Search Order (Conceptual Order)

> Note: The Windows search order varies slightly depending on the OS version and configuration, and the system provide settings that affect this order (explained below). Here is a common conceptual order (focusing on priority):

When a process requests to load a DLL named `foo.dll` (without an absolute path), the system typically searches in the following order (conceptual):

1. **Full path explicitly specified by the caller** (if calling `LoadLibrary("C:\\path\\foo.dll")`, that path is loaded directly, bypassing the search).
2. **The loader checks if it is an entry in "KnownDLLs"** (KnownDLLs are a set of trusted system libraries registered in the system; the existing system version is prioritized).
3. **Application Directory**: The directory where the executable (.exe) resides (usually prioritized over system directories, though this is influenced by settings like SafeDllSearchMode).
4. **System Directory** (typically `%SystemRoot%\System32`).
5. **Windows Directory** (typically `%SystemRoot%`).
6. **Current Working Directory** (depends on SafeDllSearchMode; if "Safe Search Mode" is enabled, the current directory is moved lower in priority).
7. **Directories listed in the PATH environment variable** (in order).
8. **If application configuration or Side-by-side (SxS)/manifest features are enabled**, the system prioritizes resolving the binding version declared in the manifest or parallel assemblies from WinSxS.

The key point is: **if you use an absolute path or a path relative to the executable, the system will not search the PATH**; conversely, if only a bare name like `foo.dll` is provided, the system attempts the search in the order listed above.
