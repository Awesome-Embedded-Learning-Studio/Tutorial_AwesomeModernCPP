---
title: 'Deep Dive into C/C++ Compilation and Linking Techniques 2: Introduction to
  Shared and Static Libraries'
description: ''
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
chapter: 13
order: 2
translation:
  source: documents/compilation/02-reuse-concept.md
  source_hash: c42b8e94395b08130fcd7932d2b44078655ee2f1875aa04d131816e753c571f9
  translated_at: '2026-05-26T10:09:04.201407+00:00'
  engine: anthropic
  token_count: 1658
---
# Deep Dive into C/C++ Compilation and Linking Part 2: Introduction to Static and Shared Libraries

## What is reuse, and how does it relate to compilation and linking?

Reuse is everywhere, and I doubt anyone would disagree. The reuse we discuss here is the reutilization of code. We can already catch a glimpse of this in C++ programming:

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

For example, the template code and function code above mean we don't have to copy code every time we call addition or compress whitespace in a string. Looking at it this way, code reuse has been around since the heyday of the C language. However, I would argue that this level of code reuse isn't very advanced—because it relies on source code distribution. In other words, to use our own past work or someone else's code masterpiece, we have to frantically dig up their source files, ensure all dependencies are in place, and add them to our project for compilation. I'm sure you've noticed the problem—in many cases, we simply cannot get the source code (trade secrets, those who know, know). In this situation, we naturally need to think about lower-level code reuse. That is binary-level distribution. This is the role of static and shared libraries, and it serves as the prerequisite for the next few sections dedicated to machine code distribution-level reuse mechanisms.

## What is a static library?

A static library might be much simpler than you think. We know that after the compiler finishes preprocessing and compiling source files, we get relocatable files. Previously, these relocatable files were directly combined into an executable. Now we can take a different approach: these common relocatable files can be assembled into a library on their own. The next time we look up symbols, we simply link against this library. This way, we hide the source code and can distribute it at the binary level. But this raises a question—how do we use it? We always need usable symbols to tell us the exact entry points. For instance, we know there is a function in the library that compresses whitespace in strings, but if we don't know what it's called, we can't use it. So it's obvious. Having just these binary files is completely insufficient; we need to meet another condition—exported header files for us to program against.

The following two diagrams illustrate the role of a static library quite well.

![static_library](./compilation-linking-2-reuse-concept/static_library.png)

However, this introduces a new problem. In reality, the code for `libfoo` is exactly the same, yet two copies exist. We don't always want this kind of hard copy. If `libfoo` is small, it's fine; hard drive capacity is relatively cheap these days, so we could call it a redundancy advantage. But in many other cases, if `libfoo` has an important security update and we want all software to reload it on the next startup, a static library seems powerless. Because it simply shifts distribution from the more difficult source code distribution to binary distribution. It doesn't solve the more important "load when use" problem at all. So it doesn't seem very elegant. In practice, static libraries aren't used all that widely (I personally rarely use them either).

## Shared libraries

So the problem lies in the fact that we perform a deep copy of all binary code, rather than a reference-level shallow copy. If we allow some symbols in the executable code to be lazily resolved at load time (which requires a loader that can dynamically load and modify the addresses of these undefined symbols to the actual shared symbol addresses), we naturally think—since we've already reached the library level, let's take it a step further and turn this code into purely shareable code. When they are needed and available, we load them, and then all executable programs that need this library can smoothly use this shared code segment directly without clumsily copying their own version. This drastically saves our memory space. This sharing characteristic is also why we can say a shared library is a dynamic library (shared code inevitably requires dynamic loading to remap shared symbol addresses, so in this context, "shared library" and "dynamic library" are completely interchangeable—no one deliberately distinguishes between them today).

Of course, for the deeper characteristics of shared libraries—for example, to ensure that any executable needing this library can successfully load symbols from it, we compile all symbols using `-fPIC` (Position Independent Code). This makes relocation very convenient for the loader.

## Overview: How do shared libraries actually work?

### Building a shared library (from source code to `libfoo.so` / versioned `libfoo.so.1.0`)

Goal: Generate a `.so` that can be dynamically loaded by clients and shared across multiple processes, with explicit ABI management (via SONAME/versioning).

The build process is factually almost identical to building an executable, except we don't add the startup headers. Beyond that, we need to ensure a few basic key points:

- **Must use Position Independent Code (PIC)**: `-fPIC` (or `-fpic`) is used to generate code that can run at any address (function memory accesses use relative addresses or go through the GOT). Not using PIC will cause the linker/runtime to encounter relocation conflicts or non-relocatable segments.
- **Use `-shared` to generate a shared object**: The linker marks the type as a shared library (ELF type = DYN).
- **Set the SONAME**: Use the linker option `-Wl,-soname,libfoo.so.1` to specify the ABI name (clients record the SONAME in their DT_NEEDED). The actual file is usually `libfoo.so.1.0`, with symlinks `libfoo.so.1 -> libfoo.so.1.0` and `libfoo.so -> libfoo.so.1` provided (for convenience during development when using `-lfoo`).
- **Control exported symbols (visibility / version script)**: By default, all global symbols are exported. You can use GCC's `-fvisibility=hidden` + `__attribute__((visibility("default")))` to mark the interfaces that need exporting, or use a linker version script to control the symbol table, reducing API pollution and lowering the risk of symbol conflicts.
- **Optional: Symbol versioning**: Used to support different versions of symbols within the same SONAME, facilitating compatibility management (requires a linker version script).

### Building the client executable (based on "trusting the library's ABI/SONAME")

Here, "trusting" means that during the build process, the client trusts that the shared library's ABI/interface (header files, SONAME, symbol semantics) will not break its expectations. The relationship between the build phase and runtime, along with the generated ELF fields, is crucial.

#### What happens at link time (building the client)

- The client uses header file declarations (`foo.h`) and `-lfoo` to link against the corresponding shared library (or the library's development symlink `libfoo.so`).
- The linker will:
  1. Merge the client's own code and object files into an executable (ELF type = EXEC or DYN (Position-Independent Executable)).
  2. **Verify**: Attempt to resolve undefined references (in the case of dynamic linking, the linker typically uses the dynamic symbol table of the specified shared library to satisfy these references; if not found, it throws an undefined reference error).
  3. **Do not copy library code**: Unlike static linking, the linker does not copy `.o` code into the executable. Instead, it records the dependency in `DT_NEEDED` (recording the library's SONAME) and generates the necessary relocations/PLT placeholders.
- Result: The executable contains dynamic section entries like `DT_NEEDED: libfoo.so.1`, but does not contain the library's implementation code.

### Runtime loading and symbol resolution (specific behavior of the dynamic linker / loader)

This is the most complex and critical part—the runtime `ld.so` (or the corresponding platform's loader) combines everything into a runnable process address space and resolves symbol references. Below is a detailed step-by-step and mechanism-based explanation.

#### Startup phase — From the kernel to the dynamic linker

1. **Kernel loads the executable**: The kernel reads the ELF header -> if the `INTERP` segment exists in the ELF (which is the case for the vast majority of dynamic executables, with a value like `/lib64/ld-linux-x86-64.so.2`), the kernel first maps the dynamic linker into the process address space, then maps the executable's PT_LOAD segments, but does not directly run the executable's `_start`.
2. **Dynamic linker (ld.so) takes over**: It is responsible for parsing `DT_NEEDED`, finding the actual library files, recursively loading dependencies and performing relocations, executing initializers (constructors), and finally handing control over to the executable's entry point (`_start` -> `main`).

#### Mapping (mmap) library files

- The loader reads the ELF Program Headers (PT_LOAD) of each dependent `.so`, mapping the executable segments (text) as read-execute and the data segments as read-write, etc. It also handles page alignment and segment protection (mmap + mprotect).
- Each library is generally mapped only once (multiple processes can share the same physical pages, as long as the pages are read-only/shared).

#### Relocations

There are multiple types of relocations, falling into two important categories:

- **Relocations not requiring symbol lookup** (e.g., RELATIVE type): These can be adjusted directly based on the base address (for position-independent code, the runtime adds the library's base address to the relative offset). They are usually processed in batches during the startup phase and are fast.
- **Relocations requiring symbol lookup** (e.g., R_X86_64_JUMP_SLOT / R_*_GLOB_DAT, etc.): These require searching for the corresponding definition location based on the symbol name (which might be in the executable or another library).

#### Symbol lookup order (default ELF search rules, general idea)

To resolve a specific symbol (e.g., the function `foo`), the loader's lookup order is typically:

1. The executable's global symbol table (executable overrides).
2. Traverse the dynamic symbol tables of each loaded library in DT_NEEDED list order, looking for the first matching global/weak symbol (note: actual rules are affected by ELF version, runtime flags, RTLD_LOCAL/RTLD_GLOBAL, symbol visibility, etc.).
3. If symbol versioning exists, the version tag must also match.
4. If loaded using `dlopen` with `RTLD_GLOBAL`, symbols from these libraries might participate in the resolution of subsequent libraries; `RTLD_LOCAL` does not participate in other subsequent resolutions.

> Important: **Symbols in the executable take precedence** over those in shared libraries (this is known as symbol interposition), so the executable can "override" functions in the library (this is also the foundation for `LD_PRELOAD` to replace function implementations).

![dynamic_library](./compilation-linking-2-reuse-concept/dynamic_library.png)

The diagram above clearly illustrates the specific process.

## A comparison

I've put together a comparison table for your reference:

| Comparison Item | Static Library (Static) | Shared Library (Shared / .so/.dll/.dylib) |
| ----------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| Binary file nature | `.a` / `.lib`: An archive of several `.o` object files; at link time, the target code is copied into the executable. | `.so` / `.dll` / `.dylib`: A shared object loadable at runtime, usually Position Independent Code (PIC), with SONAME/version info. |
| Executable integration (linking and runtime) | Resolves and copies the required target code into the executable at link time (static binding); at runtime, it no longer depends on the library file. | Records `DT_NEEDED` (or equivalent) at link time; at runtime, the dynamic linker maps it and relocates/resolves symbols in the process address space (dynamic binding, allowing real-time replacement/loading). |
| Impact on executable size | Increases executable size (contains actual copies of library code); multiple executables will redundantly include the same code. | Executable is smaller (only records dependencies); multiple processes share the same read-only/shared pages of the library; at runtime, extra memory is used for mapping and the GOT/PLT. |
| Portability | Simple deployment: the executable is usually self-contained (easier to port under the same architecture/ABI), but still affected by the OS/kernel/CRT. | Deployment depends on the runtime environment: requires appropriate shared library versions, a loader, and search paths (rpath/LD_LIBRARY_PATH/ldconfig); cross-distro/platform compatibility is more sensitive. |
| Ease of integration | Linking configuration is simple (directly `-l` / -L or merge .o files), no need to consider runtime loading; however, version upgrades require recompiling all clients. | Build and deployment are more complex (requires `-fPIC`, SONAME, rpath, symbol visibility, version scripts, etc.); but it supports runtime replacement, plugins, dlopen, and allows upgrading by replacing only the library file. |
| Ease of binary file processing/transformation | Packaging/inspecting/merging is straightforward (`ar`, `nm`, `objdump`); replacing or substituting local symbols is harder (requires relinking). | Generating and controlling exported symbols is more complex (symbol versioning, visibility); runtime relocation & symbol resolution mechanisms are complex; however, runtime `dlopen/dlsym` provides flexible extension capabilities. |
| Suitability for development | Suitable for: small tools, embedded/single-file distribution, scenarios with no runtime dependencies; convenient for offline/restricted environment deployment. | Suitable for: large projects, modular design, plugin systems, scenarios requiring hot updates or reducing redundant memory/disk usage; beneficial for team collaboration and independent library releases. |
| Other points worth mentioning | - Security/Bug fixes require rebuilding and redistributing all executables. - Copyright/licenses (like GPL) may impose stricter obligations under static linking. - Usually no PLT overhead for runtime performance (calls). | - Can fix/replace the library independently (quick patching). - Risks of runtime hijacking (LD_PRELOAD, RPATH injection) and first-call latency (lazy binding). - Higher requirements for platform ABI/SONAME management and deployment workflows. |

# Reference

This is primarily based on the book: *Advanced C/C++ Compilation Technology*
