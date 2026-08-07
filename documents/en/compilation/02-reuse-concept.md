---
chapter: 13
difficulty: intermediate
order: 2
platform: host
reading_time_minutes: 12
tags:
- cpp-modern
- host
- intermediate
title: "A Deep Dive into C/C++ Compilation and Linking, Part 2: An Introduction to Static and Dynamic Libraries"
description: 'From source reuse to binary distribution: what problems static and dynamic libraries actually solve, and what really happens at build time and runtime for a dynamic library'
cpp_standard: [11, 14, 17, 20]
---
# A Deep Dive into C/C++ Compilation and Linking, Part 2: An Introduction to Static and Dynamic Libraries

## What reuse even is, and what it has to do with compilation and linking

Reuse is everywhere, and I'd like to believe nobody seriously disagrees. The reuse we're talking about here is just reusing code. You can already catch a glimpse of this in plain C++:

```cpp
template<typename AddType>
auto add(const AddType& a, const AddType& b){
    return a + b; // 没有任何技巧的相加
}

std::string
trim_self(const std::string& waited_trim){ // returns the copy of the trimmed string
    size_t i = 0; // left index
 while (i < str.size() && isspace((unsigned char)str[i]))
  i++;
    size_t j = str.size(); // right index
    while (j > 0 && isspace((unsigned char)str[j - 1]))
  j--;
 return str.substr(i, j);
}

int main()
{
    int res = add(1, 2); // deduced as int
    float res2 = add(1.0f, 2.0f); // deduced as floats
}

```

Take the template and the plain function above: thanks to them, we don't have to copy-paste the same add and string-trimming logic every time we call them. So in that sense, code reuse has been around since the era when C ruled the world. But I'd argue this level of reuse still isn't all that high-level, because it's source-level distribution. In other words, if you want to reuse a piece of your own past work, or somebody else's masterpiece, you have to dig up their source files in a sweaty scramble, make sure every dependency is in place, and then pull it all into your own project to compile. And here, I'm sure you've already spotted the problem: in plenty of cases, you simply can't get the source code at all. (Trade secrets — you know how it is.) When that happens, we naturally start thinking about a lower-level kind of reuse. That's binary-level distribution. That's what static and dynamic libraries are for, and it's also the prerequisite for the next few chapters where we'll dig into machine-code-level reuse techniques.

## So what is a static library?

A static library is probably way simpler than you think. We know that after the compiler finishes preprocessing and compiling a source file, you get a relocatable object file. Previously, those relocatable files would just be packed straight into an executable. Now we can flip the idea around: these generic relocatable files can be bundled up into a library of their own, and next time we need a symbol, we just link against that library. Now we've hidden the source away and we're distributing at the binary level. But there's a catch: how do we actually use it? We always need some kind of available symbol to tell us the real entry point. It's like knowing there's a function in the library that trims whitespace off a string, but if we don't know what it's called, we can't call it. So the conclusion is pretty obvious: just having those binary files is nowhere near enough. We still need one more thing — an exported header file we can program against.

The two figures below do a decent job of showing what a static library does.

![static_library](./compilation-linking-2-reuse-concept/static_library.png)

But this introduces a new problem. In reality, libfoo's code is exactly the same in two places, and there are now two copies of it. Sometimes we really don't want this kind of hard copy. If libfoo is small it's fine, and disk space isn't all that expensive anymore, so we can sort of call it redundancy-as-an-advantage. But more often, if libfoo ships an important security update and we want every piece of software to pick it up on its next launch, the static library looks pretty helpless. All it really did was shift distribution from the harder source-distribution model over to binary distribution. It does absolutely nothing about the much more important "load it when you use it" problem. So it's just not that elegant. In practice, static libraries aren't used all that widely (I barely use them myself, either).

## Dynamic libraries

So the real problem is that we deep-copied every binary chunk instead of doing a reference-level shallow copy. If we let some symbols in an executable be lazily resolved at load time (which means we need a loader that can dynamically load and patch those undefined-symbol addresses to point at the real, shared symbol addresses), then the natural thought is: we've already gone to the trouble of making a library, let's go all the way and just turn this code into purely shareable code. When it's needed, load it, and then every executable that depends on this library can calmly use the shared code segment directly, without having to awkwardly keep its own copy. That saves a ton of memory. This shared nature is also why people call dynamic libraries "shared libraries" (shared code inherently has to be loaded dynamically and have the shared symbol addresses patched, so in this sense shared library and dynamic library are completely interchangeable terms; nobody really splits hairs today).

Of course, there's a deeper property of dynamic libraries. So that any executable needing this library can smoothly load its symbols, we compile all the symbols with `-fPIC` (Position Independent Code), which makes life a lot easier for the loader when it does relocations.

## Overview: so how do dynamic libraries actually pull this off?

### Building a dynamic library (from source to `libfoo.so` / a versioned `libfoo.so.1.0`)

Goal: produce a `.so` that clients can dynamically load and that multiple processes can share, with a well-defined ABI (managed through SONAME/versioning).

It's actually almost identical to building an executable, just without the startup header. Beyond that, we need to nail down a few essentials:

- **Must use position-independent code (PIC)**: `-fPIC` (or `-fpic`) generates code that can run at any address (function memory accesses use relative addresses or go through the GOT). Skipping PIC will cause the linker / runtime to hit relocation conflicts or non-relocatable segments.
- **Use `-shared` to produce a shared object**: the linker marks the type as a dynamic library (ELF type = DYN).
- **Set the SONAME**: the linker option `-Wl,-soname,libfoo.so.1` declares the ABI name (the client records this SONAME in DT_NEEDED). The actual file is usually `libfoo.so.1.0`, with symlinks `libfoo.so.1 -> libfoo.so.1.0` and `libfoo.so -> libfoo.so.1` (handy for `-lfoo` during development).
- **Control exported symbols (visibility / version script)**: by default every global symbol is exported. You can use GCC `-fvisibility=hidden` plus `__attribute__((visibility("default")))` to mark the interfaces you actually want to export, or use a linker version script to control the symbol table, which cuts down on API pollution and lowers the risk of symbol clashes.
- **Optional: symbol versioning**: lets you support multiple versions of a symbol within the same SONAME, handy for compatibility management (requires a linker version script).

### Building the client executable (on the basis of "trusting the library's ABI/SONAME")

"Trusting" here means the client believes, at build time, that the dynamic library's ABI/interface (headers, SONAME, symbol semantics) won't break what it expects. The relationship between the build phase and runtime, and which ELF fields get produced, is critical.

#### What happens at link time (building the client)

- The client uses the header declaration (`foo.h`) and links against the corresponding shared library with `-lfoo` (or the library's dev symlink `libfoo.so`).
- The linker will:
  1. Merge the client's own code with the object files into an executable (ELF type = EXEC, or DYN for a position-independent executable).
  2. **Verify**: try to resolve undefined references (for dynamic linking, the linker usually satisfies these against the dynamic symbol table of the specified shared libraries; if it can't find them, you get an undefined reference error).
  3. **Not copy the library code**: unlike static linking, the linker does not copy the `.o` code into the executable; instead it records the dependency in `DT_NEEDED` (recording the library's SONAME) and generates the necessary relocations / PLT stubs.
- Result: the executable contains dynamic-segment entries like `DT_NEEDED: libfoo.so.1`, but no actual library implementation code.

### Runtime loading and symbol resolution (what the dynamic linker / loader actually does)

This is the most complex and most critical part: at runtime `ld.so` (or the platform's loader) stitches everything together into a runnable process address space and resolves symbol references. Step by step and mechanism by mechanism:

#### Startup phase — from the kernel to the dynamic linker

1. **The kernel loads the executable**: the kernel reads the ELF header -> if the `INTERP` segment exists in the ELF (almost every dynamic executable has one, with a value like `/lib64/ld-linux-x86-64.so.2`), the kernel first maps the dynamic linker into the process address space, then maps the executable's PT_LOAD segments, but does not directly run the executable's `_start`.
2. **The dynamic linker (ld.so) takes over**: it's responsible for parsing `DT_NEEDED`, locating the actual library files, recursively loading dependencies, performing relocations, running initializers (constructors), and finally handing control over to the executable's entry point (`_start` -> `main`).

#### Mapping (mmap) the library files

- The loader reads each dependency `.so`'s ELF Program Headers (PT_LOAD), mapping the executable segment (text) as executable-and-read-only and the data segment as read-write, etc.; it also handles page alignment and segment protection (mmap + mprotect).
- Each library is generally mapped only once (multiple processes can share the same physical pages, as long as those pages are read-only / shared).

#### Relocations

There are several relocation types, falling into two important categories:

- **Relocations that don't need a symbol lookup** (e.g. the RELATIVE type): these can be adjusted directly by the base address (for position-independent code, at runtime the library base address is added to the relative offset), usually processed in a batch during startup, which is fast.
- **Relocations that need a symbol lookup** (e.g. R_X86_64_JUMP_SLOT / R_*_GLOB_DAT, etc.): these need to search by symbol name for the corresponding definition location (which may be in the executable or in another library).

#### Symbol lookup order (the default ELF search rules, roughly)

To resolve a given symbol (say the function `foo`), the loader's lookup order is usually:

1. The executable's global symbol table (executable overrides).
2. Walk each loaded library's dynamic symbol table in DT_NEEDED order, looking for the first matching global/weak symbol (note: the actual rules are affected by ELF version, runtime flags, RTLD_LOCAL/RTLD_GLOBAL, symbol visibility, etc.).
3. If symbol versioning is in play, the version tag has to match as well.
4. If a library was loaded via `dlopen` with `RTLD_GLOBAL`, its symbols can participate in resolving later libraries; with `RTLD_LOCAL`, they don't participate in any subsequent resolution.

> Important: **symbols in the executable take priority** over those in shared libraries (this is what's called symbol interposition), so an executable can "override" functions in a library (this is also the foundation of how `LD_PRELOAD` can swap out a function's implementation).

![dynamic_library](./compilation-linking-2-reuse-concept/dynamic_library.png)

That figure above lays the whole flow out clearly.

## Some comparisons

Let me tidy this into a comparison table you can reference:

| Aspect | Static library | Dynamic library (Shared / .so/.dll/.dylib) |
| --- | --- | --- |
| Nature of the binary file | `.a` / `.lib`: a bundle of several `.o` object files in archive form; at link time the object code is copied into the executable. | `.so` / `.dll` / `.dylib`: a shared object that can be loaded at runtime, usually position-independent code (PIC), carrying SONAME/version info. |
| Integration with the executable (linking and running) | Resolved at link time and the needed object code is copied into the executable (static binding); at runtime it no longer depends on the library file. | At link time `DT_NEEDED` (or equivalent) is recorded; at runtime the dynamic linker maps it and relocates / resolves symbols in the process address space (dynamic binding, can be replaced/loaded on the fly). |
| Effect on executable size | The executable gets bigger (it contains an actual copy of the library code); multiple executables will carry the same code redundantly. | The executable stays small (only the dependency is recorded); multiple processes share the same library's read-only/shared pages; at runtime extra memory is used for the mapping and for GOT/PLT. |
| Portability | Simple deployment: the executable is usually self-contained (easier to port within the same arch/ABI), but still affected by the system/kernel/CRT. | Deployment depends on the runtime environment: you need the right library version, loader, and search path (rpath/LD_LIBRARY_PATH/ldconfig); cross-distro/platform compatibility is more sensitive. |
| How easy it is to integrate | Link configuration is simple (a direct `-l` / `-L`, or just merge the `.o` files), no need to worry about runtime loading; but a version bump means recompiling every client. | Build and deployment are more involved (you need `-fPIC`, SONAME, rpath, symbol visibility, version scripts, etc.); but it supports runtime replacement, plugins, and dlopen, and an upgrade can just swap the library file. |
| How easy the binary is to manipulate/convert | Packaging/inspecting/merging is fairly straightforward (`ar`, `nm`, `objdump`); reverse-replacing or swapping out individual symbols is harder (needs a re-link). | Generating and controlling exported symbols is more complex (symbol versioning, visibility), and the runtime relocation & symbol-resolution mechanism is complex; but `dlopen/dlsym` at runtime gives you flexible extension. |
| Suitability for development work | Good fit: small tools, embedded / single-file releases, runtime-dependency-free scenarios; handy for offline / restricted environments. | Good fit: large projects, modular designs, plugin systems, anything needing hot updates or reduced duplicated memory/disk footprint; good for team collaboration and independent library releases. |
| Other things worth noting | - A security/bug fix requires rebuilding and re-releasing every executable. - Licensing (e.g. GPL) may carry stricter obligations under static linking. - Usually no PLT overhead on calls at runtime. | - You can patch/replace the library alone (fast hotfixes). - There's a runtime hijacking risk (LD_PRELOAD, RPATH injection) and a delay on first call (lazy binding). - Demands more from the platform ABI/SONAME management and the deployment process. |

## The modern CMake perspective

All those `-fPIC`, `-shared`, `-Wl,-soname`, `-fvisibility=hidden` flags — back in the days of hand-typing command lines, you really did have to spell each one out yourself. In modern projects this stuff has basically all been taken over by CMake, and when we write CMakeLists we rarely write these flags raw anymore.

`add_library(foo SHARED ${FOO_SOURCES})` produces a `.so` directly, and CMake adds `-fPIC` to SHARED targets by default, saving you the hand-copying; `add_library(foo STATIC ...)` calls `ar` to pack up a `.a` for you, basically scripting the whole archive flow from the previous section. On the client side, `target_link_libraries(myapp PRIVATE foo)` takes over `-lfoo` / `-L<dir>` in one line, and CMake will even string together the library's interface include directories and transitive dependencies for you.

`-fvisibility=hidden` shows up in CMake as `set_target_properties(foo PROPERTIES CXX_VISIBILITY_PRESET hidden)`, paired with `VISIBILITY_INLINES_HIDDEN ON`. The effect is that only the symbols you explicitly tagged `visibility("default")` get exported — the "reduce API pollution and symbol clashes" idea from the previous section, now landed through attributes.

As for the runtime `LD_LIBRARY_PATH` grunt work, CMake takes that over with `CMAKE_INSTALL_RPATH` and `$ORIGIN`: when installing to a non-standard directory, you set `INSTALL_RPATH "$ORIGIN/../lib"`, the executable carries its own rpath, and the loader just follows it, no need for the user to go exporting environment variables. SONAME/versioning is on the thinner side, usually paired with `set_target_properties(... VERSION 1.0 SOVERSION 1)` to generate `libfoo.so.1.0` plus the symlinks, with CMake setting up the soft links for you. One sentence: none of these underlying mechanisms went away, they just got tucked away behind declarative target properties by the build system.

# Reference

Most of this is drawn from the book: *Advanced C/C++ Compilation Techniques* (《高级C/C++编译技术》)
