# Deep Dive into C/C++ Compilation and Linking Part 8: Library File Search Logic

## Preface

Now, we need to discuss how library files are located. Locating library files means—how does an executable that depends on other dynamic libraries find those libraries at runtime?

This is no trivial matter. If we think about it, we can hardly escape the use of library files in modern software engineering. For example, software we build or use integrates third-party libraries into the product, or in a package-management paradigm, we need to locate the correct library files at runtime for a given piece of software to run properly.

It is almost exactly that.

## Naming Conventions

Dynamic libraries on Linux follow a naming convention. If you pay attention, you will notice that all static libraries follow `lib + <library_name> + .a`. In this case, we only need to tell the linker the `<library_name>` part, and the linker will automatically search for `lib<library_name>.a` according to other rules.

Dynamic libraries are slightly more complex. Because dynamic libraries support hot-swapping (meaning software can be released without recompiling from scratch), the naming rules are actually a bit more involved. Simply put:

`lib + <library_name> + .so + <library version information>`

As before, we only need to provide the `<library_name>` part, and the linker will automatically search for the rest.

`<library version information>` deserves a separate discussion. Generally, a version number is sufficient: `<M>.<m>.<p>`, which represents the major version, minor version, and patch version. This is the specific name. There is also something called a soname, which is the dynamic library name retaining only the major version information. For example, the soname of `libz.so.1.2.3.4` is libz.so.1. This example comes from *Advanced C/C++ Compilation*.

## Runtime Dynamic Library Search Rules

Now we need to talk about the runtime search rules for dynamic library files. Specifically, you might be interested in the runtime dynamic library search rules on Linux. Here is the breakdown. When running a dynamically linked program on Linux, a component called the **dynamic linker / loader** (usually `ld-linux.so` / `ld.so`) is responsible for finding and loading the shared libraries (`.so`) required by the executable. The search rules for dynamic libraries may look complex, but they actually have clear priorities and a few common "control points": `LD_PRELOAD`, the executable's embedded `RPATH`/`RUNPATH`, the environment variable `LD_LIBRARY_PATH`, system configuration (`/etc/ld.so.conf.d` + `ldconfig`), and system default paths (such as `/lib`, `/usr/lib`).

In the following sections, here is what you need to know: **when the dynamic linker needs to resolve a dependency** (i.e., the dependency name does not contain `/`), it usually searches in the following order (simplified):

1. Libraries specified by `LD_PRELOAD` (loaded first, used for symbol overriding/injection).
2. If the executable contains `DT_RPATH` and does not have `DT_RUNPATH`, the `DT_RPATH` paths are used (note: `DT_RPATH` is deprecated but still supported).
3. The environment variable `LD_LIBRARY_PATH` (**ignored for setuid/setgid executables**).
4. If the executable contains `DT_RUNPATH`, `DT_RUNPATH` is used (and when `DT_RUNPATH` is present, `DT_RPATH` is generally ignored).
5. The cache `/etc/ld.so.cache` maintained by ldconfig, along with `/lib`, `/usr/lib` (and architecture-specific `/lib64`, `/usr/lib64`)—these are the "trusted directories."
6. (If not found in any of the above) It ultimately fails and reports an error (such as `ld.so: cannot find ...`).

> Note: The details of this order (especially the interaction between `RPATH` and `RUNPATH`) are influenced by the linker implementation and linker flags (such as `--enable-new-dtags`, which enables the -R or -rpath linker directive).

------

## Detailed Explanation (Expanding on Each Item)

#### LD_PRELOAD ("Injecting" or Overriding Symbols on Demand)

`LD_PRELOAD` is an environment variable that can specify one or more shared libraries to be forcibly loaded into a process **before the normal search** begins. This allows for intercepting or replacing symbols (functions). However, this is rarely seen and generally not recommended unless you know exactly what you are doing :)

------

#### DT_RPATH and DT_RUNPATH (i.e., "rpath / runpath")

At link time, one or more runtime library search paths can be written into the dynamic segment (`.dynamic`) of an executable or shared library, corresponding to the ELF tags `DT_RPATH` and `DT_RUNPATH` respectively. Historically, `DT_RPATH` was introduced early on with the behavior of "taking priority over environment variables." Later, `DT_RUNPATH` (new-dtags) was introduced. The implication of `DT_RUNPATH` is: **it is searched after `LD_LIBRARY_PATH`**, meaning `LD_LIBRARY_PATH` can override the paths in RUNPATH; whereas `DT_RPATH`, in some implementations or historically, takes priority over `LD_LIBRARY_PATH` (making it harder to override).

Another important behavioral difference: **DT_RPATH is effective for transitive dependencies**, while **DT_RUNPATH may not be used to find transitive dependencies** (i.e., when executable -> libA -> libB, the behavior of RUNPATH in certain situations will not provide paths for finding libB, whereas RPATH will). This causes some combinations that worked with RPATH under older linkers to fail with a "cannot find indirect dependency" error after switching to RUNPATH (new-dtags).

In my current Linux experience, I rarely encounter this. Therefore, in more test environments, adopting the following approach is appropriate.

------

#### LD_LIBRARY_PATH (This is an Environment Variable)

`LD_LIBRARY_PATH` is a list of runtime library search paths used by the dynamic linker at a specific stage (see the order above). It is very commonly used to temporarily override system paths or to test new library versions. **Similarly**, setuid/setgid executables ignore this variable (for security reasons).

The trouble with environment variables is that they easily interfere with all processes launched from a shell that has this variable set. We do not recommend relying on `LD_LIBRARY_PATH` long-term in production environments, because it affects all child processes started through that shell and is less maintainable than system configuration (ldconfig).

```bash
export LD_LIBRARY_PATH=/opt/foo/lib:/home/you/sw/lib:$LD_LIBRARY_PATH
./myapp

```

------

#### ldconfig, /etc/ld.so.conf.d, and ld.so.cache

System administrators typically tell `ldconfig` which directories should be trusted by the system dynamic linker by placing library directories in `/etc/ld.so.conf` or `/etc/ld.so.conf.d/*.conf`. `ldconfig` scans these directories and generates a binary cache `/etc/ld.so.cache` (to improve lookup speed), while also creating symbolic links (libXXX.so -> libXXX.so.VERSION). The dynamic linker reads this cache to accelerate lookups.

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

The dynamic linker usually searches `/lib`, `/usr/lib` (and on 64-bit systems, `/lib64`, `/usr/lib64`) by default. These directories are known as "trusted directories." `ldconfig` also processes these directories. Even if a path is not written to `ld.so.conf`, placing a library in these directories usually allows it to be found (but pay attention to architecture bits, ABI, and version matching).

## What About Windows?

The Windows executable/loader and APIs (`LoadLibrary` / `LoadLibraryEx` / automatic loading via the import table) define a set of search orders and security improvements.

Generally, Windows has two approaches: implicit (import table) and explicit (runtime API).

**Implicit loading** means the executable's Import Table is resolved by the system loader during process startup or module loading. The system attempts to find and map each `DLL` into the process address space. Developers specify dependencies during the linking phase (for example, `kernel32.dll`, `mydll.dll`), and loading is automatically completed by the system at process startup.

**Explicit loading** means the code manually loads a DLL at runtime using APIs like `LoadLibrary` / `LoadLibraryEx`, and then obtains function pointers using `GetProcAddress`. Explicit loading allows you to control search behavior through parameters (for example, using flags like `LOAD_LIBRARY_SEARCH_USER_DIRS`).

#### Default Search Order (Conceptual Order)

> Note: The Windows search order has subtle differences across OS versions and configurations, and the system provides settings that affect this order (explained below). Here is a conceptual common order (understanding the priorities is sufficient):

When a process requests loading a library named `foo.dll` (without specifying an absolute path), the system usually searches in the following order (conceptual order):

1. **The full path explicitly specified by the caller** (if calling `LoadLibrary("C:\\path\\foo.dll")`, it loads that path directly without searching).
2. **The loader first checks if it is an entry in "KnownDLLs"** (KnownDLLs are a set of trusted system libraries registered in the system, prioritizing the existing system version).
3. **Application directory (Executable directory)**: The directory where the executable (.exe) is located (usually prioritized over the system directory, specifically influenced by settings like SafeDllSearchMode).
4. **System directory** (usually `%SystemRoot%\System32`).
5. **Windows directory** (usually `%SystemRoot%`).
6. **Current working directory** (depends on SafeDllSearchMode; if "safe search mode" is enabled, the current directory's position is pushed further down).
7. **Directories listed in the PATH environment variable** (in order).
8. **If application configuration or Side-by-side (SxS)/manifest features are enabled**, it prioritizes resolving the binding version declared in the manifest or the side-by-side assembly from WinSxS.

The key takeaway is: **if you use an absolute path or a path relative to the executable, the system will not search the PATH**; conversely, if you only provide a bare name like `foo.dll`, it will attempt the order above.
