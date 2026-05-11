# Deep Dive into C/C++ Compilation and Linking Part 9: Shared Library Details (Finale)

## Preface

Next, we are going to discuss the details of shared libraries. In general, day-to-day engineering development might not touch on these topics, but understanding how shared libraries work is always better than not. Therefore, drawing on *Advanced C/C++ Compilation Techniques*, the author will revisit some of the finer details of shared libraries.

## **8.1 The Necessity of Resolving Memory Addresses**

Before moving forward, let's cover a few assembly concepts.

Obviously, we know that the fundamental model of modern computers is the Turing machine: we know where the operands are, fetch them for computation, and put them back.

Taking x86 as an example, we need to know the address of a memory operand so that we can pass data back and forth between memory and the CPU.

```cpp

mov eax, ds:0xBAD10000 ; 将地址0xBAD10000装载到eax中
add eax, 0x1 ; 装载值自增
mov ds:0xBAD10000, eax; 写回操作

```

Great. Knowing this, we must point out that the essence of a function call is also finding the function's address in the code segment—for example, when we want to call a trivial `add` function, we have to tell our `call` instruction where the `add` function is located (that is, we need to provide the code segment address of the `add` function's entry point).

```cpp

add <0x11451400>:
 ... ; Add Procedure

main:
 ... ; Main Procedure
 call 11451400 ; add absolute

```

Of course, sometimes we also use `call` with a relative address, which is a bit more convenient.

## Common Issues in Reference Resolution

Let's look at the simplest scenario! Suppose an executable file can only work further after loading a single shared library. The following points are obvious:

- The client binary provides a portion of the process memory mapping with a fixed, predictable address range.
- Only after dynamic loading is complete does it become a valid part of the process.
- When the executable calls one or more functional implementations provided by the shared library (such as the library's interface), the connection is naturally established at that point.

From these basic scenarios, we can understand one thing: the core issue with shared libraries is that **the runtime location of the library code is uncertain**. Whether it is a Windows DLL, a Linux `.so`, or a macOS `dylib`, they all share one common trait: **shared libraries cannot determine their final load address at compile time.**

Why is that? There are three main reasons:

#### **(1) Address conflicts between multiple shared libraries**

Suppose two `.so` files both want to be mapped to the `0x400000` area in virtual memory; this would cause a conflict.
To avoid conflicts, the operating system's loader must select a different, suitable base address.

#### **(2) ASLR (Address Space Layout Randomization)**

Modern operating systems enable address randomization for security, so the load address of a shared library is different every time.
This means that compilers and linkers cannot assume a shared library will run at a fixed address.

#### **(3) The same shared library loads at different locations in different processes**

Process address spaces are independent of each other, so the library's load location can be completely different in each process.

## Address Translation is the Solution

#### Case: We need to use exported binary symbols

For example, if we need to use those exported symbols, such as the interfaces provided by the library—`create_window`, `init_all`, `deinit_all`, and so on. This is using exported binary symbols. In this case, the client program obviously needs to immediately know the actual loaded address, rather than the library's original symbol address (they are offset from zero!). Therefore, the old approach of having the linker complete all symbol resolution directly is clearly impossible. The determination of symbol addresses must be jointly handled by the loader.

#### Case: Calling private symbols internally

Regardless of the situation, some private symbols cannot be found by the client program. But there is an even more severe problem—what if these symbols are being called by exported symbols? What do we do then?

## Linker-Loader Cooperation—The Old Technique

Now let's dive into the details of linker-loader cooperation. Understanding all the constraints described earlier, we can establish the cooperation between the linker and the loader based on the following rules:

- The linker recognizes the limitations of its own symbol resolution.
- The linker accurately counts the failed symbol references, prepares reference fix-up hints, and embeds these hints into the binary file.
- The loader accurately follows the linker's relocation hints and performs fix-ups based on these hints after completing the address translation.

### The Linker Recognizes the Limitations of Its Symbol Resolution

When creating a shared library, the linker not only needs to clearly distinguish the relationships between different parts of the code, but also needs to accurately identify which symbol references will become invalid when the code segment is loaded into different address ranges.

First, unlike executable files, the memory mapping address range of a shared library starts from zero. When the linker processes an executable file, it mostly does not set the starting point of the address range to zero. Second, before the loading phase, if the linker finds that the addresses of certain symbols cannot be resolved, it will stop resolving them and instead fill the unresolved symbols with temporary values (usually using obviously incorrect values, like `0`). However, this does not mean the linker completely abandons the symbol resolution task. On the contrary, it only gives up on handling those symbols that are truly unresolvable.

### Next Step: The Linker Accurately Counts Failed Symbol References and Prepares Fix-up Hints

We can know exactly which resolved references will become invalid due to the loader's address translation. As long as an assembly instruction requires an absolute address, the reference in that instruction will become invalid. During the linking phase of building the shared library, the linker can identify the places where absolute addresses appear and let the loader know this information through certain methods. To provide linker-loader cooperation support, the linker reserves some hints for the loader. These hints point out to the loader how to fix the errors caused by address translation during dynamic loading. The binary format specification supports some new sections specifically reserved for these hints. Additionally, specific simple syntaxes are designed so that the linker can accurately indicate the actions the loader needs to perform.

These sections in the binary file are called "relocation sections," and the `.rel.dyn` section is the oldest relocation section. Generally speaking, the linker writes relocation hints into the binary file so that the loader can read them. These hints specify the addresses the loader needs to patch after completing the final memory mapping layout of the entire process, and the correct actions the loader needs to take to properly patch the unresolved references.

### The Loader Accurately Follows the Linker's Relocation Hints

The final phase belongs to the loader. The loader reads the shared library created by the linker, reads the loader segments within the shared library (each segment holds multiple linker sections), and places all data into the process memory mapping, stored near the original executable code.

Finally, the loader locates the `.rel.dyn` section, reads the hints left by the linker, and patches the original shared library code based on these hints. Once patching is complete, it is ready to use the memory mapping to start the process. Compared to handling basic tasks, when dealing with shared library loading, we need to provide the loader with more information.

## Modern Linker-Loader Cooperation Implementation: PLT/GOT

#### Internal Mechanism of GOT / PLT

The GOT (Global Offset Table) allows code to avoid relying on fixed addresses, and instead fetches the final address from the table. Of course, this obviously requires us to compile our code with `-fPIC` (do you now understand why Step 1 for shared libraries is to use PIC, position-independent code!)

Now, our call becomes something like `call [GOT + foo]`. To achieve this, once the address of `foo` is determined, the `foo` entry in the GOT is written with the actual address. This way, we simply update it.

The PLT works with the GOT to implement lazy binding:

- First function call → PLT jumps to the resolver → updates the GOT → jumps directly to the correct address (no more resolving)

Benefits of the PLT:

- Speeds up program startup
- Resolves symbols only when needed

------

## **Detailed Explanation of the Lazy Binding Process**

Simply put, lazy binding means deferring the actual setting of the GOT table addresses until the very end; before that, it iteratively resolves all confirmed symbols.

1. `call foo` → jump to `PLT[foo]`
2. `PLT[foo]` calls the resolver `_dl_runtime_resolve`
3. The resolver looks for the symbol `foo` across all shared libraries
4. Update `GOT[foo]` = the real address of `foo`
5. Return to `foo`
6. Subsequent calls jump directly to `GOT[foo]`

------

## Duplicate Symbols in Dynamic Linking

In static linking, if two global symbols share the same name, the linker usually throws an error directly (Multiple Definition Error). But in the world of **dynamic linking**, the rules are completely different. That is why this topic deserves its own discussion.

#### Duplicate Symbol Definitions

In large projects, we often link against multiple third-party libraries. Suppose your program links against `libA.so` and `libB.so`, and coincidentally, the developers of both libraries defined a global function `void init()` or a global variable `int g_config`.

When your main program starts and loads both libraries, two symbols named `init` will exist in memory.

#### Why Does This Happen?

1. **Common naming**: Using overly generic names (like `utils`, `log`, `init`) without using `static` to limit the scope.
2. **Diamond Dependency**: The project depends on library A and library B, and both A and B internally statically link the same base library C (such as an older version of OpenSSL). This results in C's symbols having a separate copy in both A and B.
3. **Header file implementations**: Defining global variables or non-inline functions in header files, which are then included by multiple `.c/.cpp` files.

------

## Default Handling of Duplicate Symbols

The dynamic linker on Linux (`ld-linux`) uses a specific set of rules to handle such conflicts, commonly known as **Symbol Interposition**.

#### Rule: First Match Wins

By default, the dynamic linker uses a **Breadth-First Search (BFS)** order to look up symbols. It binds to the **first** matching symbol it finds in the Global Symbol Table and **ignores** all subsequent symbols with the same name.

#### Load Order Determines Everything

This means that the **Link Order** or **Load Order** determines exactly whose code the program calls.

Suppose `app` depends on `libA` and `libB`, and both have `func()`:

- If the link command is `gcc main.c -lA -lB`: when the main program calls `func()`, it will usually link to the version in `libA`.
- **The dangerous scenario**: If code inside `libB` calls `func()`, according to ELF's global symbol binding rules, `libB` will also call `libA`'s `func()`! This is known as "symbol hijacking." `libB` thinks it is calling its own code, but it actually ends up in `libA`, which can lead to logic errors or even crashes.

> **Use case:** The `LD_PRELOAD` environment variable leverages exactly this mechanism. By preloading a library containing an implementation of `malloc`, we can override libc's standard `malloc`, thereby implementing memory leak detection tools (like Valgrind or jemalloc).

------

## Handling Duplicate Symbols During Shared Library Linking

Since the default behavior is so dangerous, how can we protect our own symbols from being hijacked when developing shared libraries, or avoid hijacking others?

#### 1. Linker Flag: `-Bsymbolic`

When compiling a shared library, we can use the linker flag `-Wl,-Bsymbolic`.

- **Effect**: Forces the shared library to prioritize resolving global symbol references within itself.
- **Result**: If `libB` is compiled with this flag, then when `libB` internally calls `func()`, it will definitely call `libB`'s own version, and it will not be overridden by `libA` or the main program.

#### 2. Symbol Visibility

This is a best practice in modern C++ development. By using the GCC/Clang `-fvisibility=hidden` flag, we hide all symbols by default and only export the necessary interfaces.

- **Code example:**

  ```C
  // 只有标记了 DEFAULT 的符号才会被导出到动态符号表
  __attribute__((visibility("default"))) void public_api();

  // 即使是全局函数，在外部看来也是不可见的，避免冲突
  void internal_helper();

  ```

#### 3. Scope Control with `dlopen`

If we use `dlopen` to manually load a library, we can specify the `RTLD_LOCAL` flag (which is the default value). This prevents the loaded library's symbols from entering the global symbol table, thereby avoiding affecting other libraries.

------

### A Few Classic Examples

#### Custom Memory Allocators

Many high-performance services (like Redis and MySQL) link against `jemalloc` or `tcmalloc`.

- **Phenomenon**: These libraries define symbols like `malloc`, `free`, and `realloc` that are identical to those in Glibc.
- **Mechanism**: Because they are explicitly linked or preloaded, their symbols appear in the global table before Glibc's.
- **Result**: All memory allocations for the entire process (including other third-party libraries that depend on Glibc) are automatically forwarded to `jemalloc`. This is a benign, intentional symbol conflict.

#### C++ STL Version Conflicts

This is a malicious case.

- **Scenario**: The main program is compiled with GCC 4.8 and depends on `libStdOld.so`; a plugin is compiled with GCC 9.0 and depends on `libStdNew.so`.
- **Problem**: The internal implementations of `std::string` or `std::vector` might differ between versions, but their symbol names (Mangled Names) might remain partially compatible or collide.
- **Consequence**: When objects are passed across libraries, because the memory layouts differ but the symbols are the same, the program might exhibit undefined behavior (UB), typically manifesting as inexplicable segfaults.

------

#### Linking Does Not Provide Any Kind of Namespace Inheritance (Tip: No Namespace Inheritance)

This point bears repeating! Many people think: "If I put a function inside a `namespace MyLib { ... }` in my C++ code, or if I compile my code into a `libMyLib.so`, then the library acts like an independent container, and the variable name `count` inside it won't conflict with the outside world."

But in reality, **the linker is "type-blind" and "structure-blind."** We all know that **C++ namespaces are just syntactic sugar:** the compiler uses **name mangling** to turn `MyLib::foo()` into the string `_ZN5MyLib3fooEv`. To the linker, this is just a long string. If two libraries happen to generate the same mangled name, a conflict will still occur. And **a shared library is not a namespace:** a shared library is merely a file organization format. Once loaded into process memory, all exported symbols enter a flat, single-level global symbol pool (Global Symbol Table). The global variable `g_context` in `libA.so` and `g_context` in `libB.so` are the exact same thing in the eyes of the linker, unless you use visibility hiding or local binding.
