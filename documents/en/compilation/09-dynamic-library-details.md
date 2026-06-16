---
chapter: 13
difficulty: intermediate
order: 9
platform: host
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
title: 'Deep Dive into C/C++ Compilation and Linking: Part 9 – Dynamic Library Details
  (Finale)'
description: ''
translation:
  source: documents/compilation/09-dynamic-library-details.md
  source_hash: 315ba24b7cf9d2848735ab94d5d5acf600605787f7b91efd2692f588cac62b3d
  translated_at: '2026-06-16T03:28:05.668438+00:00'
  engine: anthropic
  token_count: 1653
---
# Deep Dive into C/C++ Compilation and Linking Techniques 9: Dynamic Library Details (Finale)

## Introduction

Next, let's discuss the details of dynamic libraries. Generally speaking, engineering development might not involve this level of detail, but knowing how dynamic libraries work is better than not knowing. Therefore, combining "Advanced C/C++ Compilation Technology," I will revisit some details of dynamic libraries.

## **8.1 The Necessity of Resolving Memory Addresses**

Before rushing ahead, let's supplement a few assembly instructions.

Obviously, we know that the basic model of modern computers is the Turing machine; we know where the operands are, fetch them for calculation, and put them back.

Taking X86 as an example, we need to know the address of the memory operand so that we can transfer data back and forth between memory and the CPU.

```cpp

mov eax, ds:0xBAD10000 ; 将地址0xBAD10000装载到eax中
add eax, 0x1 ; 装载值自增
mov ds:0xBAD10000, eax; 写回操作

```

Very good. Knowing this, we must point out that the essence of a function call is also finding the address of the function in the code segment—for example, if we want to call an ordinary `add` function, we must tell our `call` instruction where the `add` function is (that is, we must provide the code segment address of the `add` function's entry point).

```cpp

add <0x11451400>:
 ... ; Add Procedure

main:
 ... ; Main Procedure
 call 11451400 ; add absolute

```

Of course, sometimes we also use relative addresses for calls, which is slightly more convenient.

## Common Issues in Reference Resolution

Let's look at the simplest situation! Suppose an executable file can only work further after loading a single dynamic library. These things are obvious:

- The client binary provides a portion of the process memory map with a fixed and predictable address range.
- Only after dynamic loading is complete does it become a valid part of the process.
- When the executable calls one or more function implementations provided by the dynamic library (such as the library's interface), a connection is naturally established at this time.

From the basic situation above, we can know one thing: the core problem of dynamic libraries is that **the location of library code at runtime is indeterminate**. Whether it is Windows DLLs, Linux .so, or macOS dylib, they all have one thing in common: **dynamic libraries cannot determine their final load address during the compilation phase.**

Why? Mainly for these reasons:

#### **(1) Address conflicts may occur between multiple dynamic libraries**

Assume two .so files both want to map to the 0x400000 area in virtual memory; this will cause a conflict.
To avoid conflict, the operating system's loader must re-select a suitable base address.

#### **(2) ASLR (Address Space Layout Randomization)**

Modern operating systems enable address randomization for security, so dynamic libraries load at different addresses every time.
This means: compilers and linkers cannot assume that dynamic libraries will run at a fixed address.

#### **(3) The same dynamic library loads at different locations in different processes**

The address spaces of processes are independent of each other, and the loading location of the library in each process can be completely different.

## Address Conversion is the Solution

#### Case: We just want to use exported binary symbols

For example, if we just want to use those exported symbols, such as interfaces provided by the library—``create_window``, ``init_all``, ``deinit_all``, etc.—this is using exported binary symbols. At this time, the client program obviously needs to know immediately where the successful load address is, rather than the dynamic library's original symbol address (they are offset from zero!). Therefore, in the past, it was obviously impossible for the linker to complete all symbol resolution work directly. The determination of symbol addresses must be determined by the loader together.

#### Case: Calling your own private symbols

Regardless, some private symbols cannot be found by the client program, but there is a more severe problem—if these symbols are called by exported symbols, what should be done then?

## Linker-Loader Cooperation—Old Technology

Now let's talk carefully about linker-loader cooperation. After understanding all the constraints described earlier, we can establish cooperation between the linker and the loader based on the following rules:

- The linker identifies the limitations of its own symbol resolution.
- The linker accurately counts invalid symbol references, prepares reference fix-up hints, and embeds these hints into the binary file.
- The loader accurately follows the linker's relocation hints and performs fix-ups based on these hints after completing address translation.

### Linker identifies the limitations of its own symbol resolution

When creating a dynamic library, in addition to clearly distinguishing the relationship between different parts of the code, the linker also needs to accurately identify which symbol references will fail when the code segment is loaded into different address ranges.

First, unlike executable files, the address range of the dynamic library memory mapping starts from zero. When processing executable files, the linker will mostly not set the start point of the address range to zero. Secondly, before the loading stage, if the linker finds that the addresses of certain symbols cannot be resolved, it will stop resolving and instead use temporary values to fill the unresolved symbols (usually obviously wrong values, such as 0). However, this does not mean that the linker will completely abandon the symbol resolution task. On the contrary, it will only give up dealing with those symbols that really cannot be figured out.

### Next step: Linker accurately counts invalid symbol references, prepares fix-up hints

We can fully know which resolved references will fail due to loader address translation. Whenever an assembly instruction requires an absolute address, the reference in the instruction will be invalid. At the completion of the link stage of dynamic library construction, the linker can identify where absolute addresses appear and let the loader know this information through some methods. To provide linker-loader cooperation support, the linker will reserve some hints for the loader. These hints point out to the loader how to fix errors caused by address translation during dynamic loading. The binary format specification supports some new sections specifically reserved for this type of hint. In addition, specific simple syntax is designed to facilitate the linker to accurately point out the action the loader needs to perform.

These sections are called "relocation sections" in the binary file, where the `.rel.dyn` section is the oldest relocation section. Generally speaking, the linker writes relocation hints into the binary file so that the loader can read these hints. These hints specify the addresses that the loader needs to patch after completing the final memory map layout of the entire process, and the correct actions the loader needs to perform to correctly patch unresolved references.

### Loader accurately follows linker relocation hints

The last stage belongs to the loader. The loader reads the dynamic library created by the linker, reads the loader segments in the dynamic library (each segment holds multiple linker sections), and places all data into the process memory map, stored near the original executable file code.

Finally, the loader locates the `.rel.dyn` section, reads the hints reserved by the linker, and patches the original dynamic library code according to these hints. After the patching is completed, we are ready to use the memory map to start the process. Compared to handling basic tasks, we need to provide the loader with more information when handling dynamic library loading.

## Modern Linker-Loader Cooperation Implementation Technology: PLT/GOT

#### Internal mechanism of GOT / PLT

GOT (Global Offset Table) is used to allow code to not rely on fixed addresses, but to fetch the final address from the table. Of course, this obviously requires us to compile our code with `-fPIC` (do you understand now why Step 1 for dynamic libraries is to use PIC (Position Independent Code)!)

Now, our call becomes similar to ``call [GOT + foo]``. For this, when the address of `foo` is determined, the `foo` entry in the GOT is written as the actual address. This way we update it directly.

PLT combines with GOT to implement lazy binding:

- First function call → PLT jumps to resolver → Updates GOT → Jumps directly to correct address (no more resolving)

Benefits of PLT:

- Speeds up program startup
- Resolves symbols only when needed

------

## **Detailed Explanation of Lazy Binding Process**

Simply put, lazy binding means not actually setting the GOT table address until the very last moment; before that, it polls and resolves all determined symbols.

1. `call foo` → Jump to `PLT[foo]`
2. `PLT[foo]` calls resolver `_dl_runtime_resolve`
3. Resolver searches for symbol `foo` in all dynamic libraries
4. Update `GOT[foo]` = real address of `foo`
5. Return to `foo`
6. Subsequent calls jump directly to `GOT[foo]`

------

## Duplicate Symbols in Dynamic Linking

In static linking, if two global symbols with the same name appear, the linker usually reports an error directly (Multiple Definition Error). But in the world of **dynamic linking**, the rules are completely different. This is why it is worth discussing separately.

#### Duplicate Symbol Definitions

In large projects, we often link multiple third-party libraries. Suppose your program links `libA.so` and `libB.so`. Coincidentally, the developers of both libraries defined a global function `void init()` or a global variable `int g_config`.

When your main program starts and loads these two libraries, there will be two symbols named `init` in memory.

#### Why does this happen?

1. **Common naming**: Used overly generic names (like ``utils``, ``log``, ``init``) without using ``static`` to limit the scope.
2. **Diamond Dependency**: The project depends on library A and library B, and both A and B internally statically link the same base library C (such as an old version of OpenSSL). This results in C's symbols having a copy in both A and B.
3. **Header file implementation**: Defined global variables or non-inline functions in header files, which were included by multiple ``.c/.cpp`` files.

------

## Default Handling of Duplicate Symbols

The dynamic linker under Linux (`ld-linux`) adopts a specific set of rules to handle such conflicts, usually referred to as **Symbol Interposition**.

#### Rule: First Match Wins

By default, the dynamic linker uses a **Breadth-First Search (BFS)** order to find symbols. It binds to the **first** matching symbol found in the Global Symbol Table and **ignores** all subsequent symbols with the same name.

#### Load Order Decides Everything

This means that **Link Order** or **Load Order** determines whose code the program actually calls.

Assume ``app`` depends on ``libA`` and ``libB``, and both have ``func()``:

- If the link command is ``gcc main.c -lA -lB``: When the main program calls ``func()``, it usually links to ``libA``'s version.
- **Dangerous situation**: If code inside ``libB`` calls ``func()``, following ELF's global symbol binding rules, ``libB`` will also call ``libA``'s ``func()``! This is called "symbol hijacking." ``libB`` thinks it is calling its own code, but actually runs into ``libA``, which can lead to logic errors or even crashes.

> **Application Scenario:** The ``LD_PRELOAD`` environment variable utilizes exactly this mechanism. By preloading a library containing ``malloc`` implementations, we can override libc's standard ``malloc``, thereby implementing memory leak detection tools (like Valgrind or jemalloc).

------

## Handling Duplicates During Dynamic Library Linking

Since the default behavior is so dangerous, how can we protect our symbols from being hijacked when developing dynamic libraries, or avoid hijacking others?

#### 1. Linker parameter: ``-Bsymbolic``

When compiling a dynamic library, you can use the linker parameter ``-Wl,-Bsymbolic``.

- **Function**: Forces the dynamic library to prioritize resolving global symbol references within itself.
- **Effect**: If ``libB`` is compiled with this parameter, then when ``libB`` internally calls ``func()``, it will definitely call ``libB``'s own version and will not be overridden by ``libA`` or the main program.

#### 2. Symbol Visibility

This is a best practice for modern C++ development. Through GCC/Clang's ``-fvisibility=hidden`` parameter, all symbols are hidden by default, and only required interfaces are exported.

- **Code Example**:

  ````C
  // 只有标记了 DEFAULT 的符号才会被导出到动态符号表
  __attribute__((visibility("default"))) void public_api();

  // 即使是全局函数，在外部看来也是不可见的，避免冲突
  void internal_helper();

  ````

#### 3. Scope control of ``dlopen``

If using ``dlopen`` to manually load a library, you can specify the ``RTLD_LOCAL`` flag (this is the default). This causes the loaded library's symbols **not** to enter the global symbol table, thereby avoiding affecting other libraries.

------

### A Few Classic Examples

#### Custom Memory Allocator

Many high-performance services (like Redis, MySQL) will link ``jemalloc`` or ``tcmalloc``.

- **Phenomenon**: These libraries define the same ``malloc``, ``free``, ``realloc`` symbols as Glibc.
- **Mechanism**: Because they are explicitly linked or preloaded, their symbols rank before Glibc in the global table.
- **Result**: All memory allocations for the entire process (including other third-party libraries depending on Glibc) are automatically forwarded to ``jemalloc``. This is a benign, intentional symbol conflict.

#### C++ STL Version Conflict

This is a malignant case.

- **Scenario**: The main program is compiled with GCC 4.8 and depends on ``libStdOld.so``; the plugin is compiled with GCC 9.0 and depends on ``libStdNew.so``.
- **Problem**: The internal implementation of ``std::string`` or ``std::vector`` may differ in different versions, but their symbol names (Mangled Name) may remain consistent through partial compatibility, or conflicts may occur.
- **Consequence**: When objects are passed across libraries, due to different memory layouts but identical symbols, the program may exhibit Undefined Behavior (UB), usually manifesting as inexplicable Segfaults.

------

#### Tip: Linking Does Not Provide Any Namespace Inheritance

This needs to be repeated! Many people think: "I put the function in ``namespace MyLib { ... }`` in my C++ code, or I compiled the code into ``libMyLib.so``, so this library is like an independent container, and the variable name ``count`` inside won't conflict with the outside."

But in reality, **the Linker is "Symbol Type-blind" and "Structure-blind."** We all know **C++ namespaces are just syntactic sugar**: The compiler turns ``MyLib::foo()`` into the string ``_ZN5MyLib3fooEv`` via **Name Mangling**. For the linker, this is just a long string. If two libraries happen to generate the same Mangled Name, conflicts will still occur. And **dynamic libraries are not namespaces**: Dynamic libraries are just a file organization form. Once loaded into process memory, all exported symbols enter a flat, global symbol pool. The global variable ``g_context`` in ``libA.so`` and ``g_context`` in ``libB.so`` are the same thing in the linker's eyes, unless you use Visibility hiding or Local binding.
