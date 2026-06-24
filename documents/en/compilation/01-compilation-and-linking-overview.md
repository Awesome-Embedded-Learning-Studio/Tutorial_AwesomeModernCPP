---
chapter: 13
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 32
tags:
- cpp-modern
- host
- intermediate
title: 'Understanding C/C++ Compilation and Linking: An Introduction'
description: ''
translation:
  source: documents/compilation/01-compilation-and-linking-overview.md
  source_hash: 444d119fde649b1365d37e1711b54516f531e260c9771cd7bd3c26518df51e66
  translated_at: '2026-06-24T00:25:35.030490+00:00'
  engine: anthropic
  token_count: 5806
---
# Deep Dive into C/C++ Compilation and Linking: Introduction

## Preface

This is a new series! It is a topic I plan to explore systematically this week. Specifically, we will discuss and summarize a series of topics in C/C++ programming that we often gloss over but which frequently cause us grief—compilation and linking technologies. I believe everyone has encountered headaches like `undefined referenced` errors. I know seeing such errors can be quite daunting (I was recently tormented by `undefined referenced` errors during template instantiation).

When solving these problems, I believe many of us initially panic and ask AI or search the web, but few truly stop to think—why do we get `undefined referenced` errors in the first place? Leaving aside the times we genuinely forget to provide source files in the build system (which I know happens to many, myself included), there are many times when we really have—at least we think we have—provided the source file, and we can even see it being linked, yet the linking still fails.

For example, suppose you write code in a file named `lib.c` and build it into a static library `libutils`.

```c
int int_max(int a, int b) {
 return a > b ? a : b;
}

```

Subsequently, we immediately use `int_max` in a C++ file.

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
 int a = 1, b = 2;
 std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}

```

Then, when we run this command expecting our program to compile successfully, we get a very strange error —

```cpp

[charliechen@Charliechen linkers]$ g++ usage.cpp -L. -lutils -o usage
/usr/sbin/ld: /tmp/ccdSskJz.o: in function `main':
usage.cpp:(.text+0x88): undefined reference to `int_max(int, int)'
collect2: error: ld returned 1 exit status
[charliechen@Charliechen linkers]$

```

This looks strange. We clearly linked `libutils`, and the linker even found it (it didn't complain `/usr/sbin/ld: cannot find -lutils: No such file or directory`, which means it was found). So why did it fail? Furthermore, even if it couldn't find the symbol, why didn't it complain during compilation? I believe that if you can spot the problem immediately, as the author of the [`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html) suggests, then this introductory article, "Deep Dive into C/C++ Compilation and Linking Technology: Introduction," likely holds nothing new for you. We will discuss the details thoroughly later on, but not here.

**This blog post assumes you have at least written C programs (although the issue above involves C++, the core of this article is not C++). It is even better if you have encountered errors like `undefined reference` and didn't know how to solve them.**

## So, what do the variables and functions we write actually mean?

This question is not for **you**; we are asking **the computer**. To answer this series of questions you might never have thought of, we must first answer a prerequisite question: "How does the computer know about the things we find and can't find?" To phrase it more formally: how does the compiler toolchain collect and look up symbols? How does it transform them into a more manageable form (for example, mapping a function to an address the computer can find)? Those familiar with assembly will immediately grasp how functions work—once the function name is resolved to an address, we simply `call` that address, and the processor's execution flow jumps to that location to fetch instructions and execute the code. Ultimately, our first step is to understand: how do variables and functions, which express business logic in a way we understand, get transformed into addresses that tell the machine where things are located? What happens in the middle? **What do the variables and functions we write actually mean to the computer?**

Any computer science student can undoubtedly recite the four classic steps of a program from source code to running on an operating system: preprocessing, compilation, linking, and **execution** (You might ask, isn't execution obvious? Why mention it separately? Good question! We will discuss dynamic loading of dynamic libraries and startup loading in detail later).

To answer the questions above effectively, we need to focus on the latter three stages (preprocessing is a **source-to-source transformation**, such as `#define` expansion and conditional compilation using `#if`, which we will not discuss here).

When writing C files—whether in tutorials from your favorite content creators, notes from expert blogs, or your university professor's droning lecture on ancient PPTs—you will be told that we are essentially doing two things: declarations and definitions. Our subjects of discussion are **global variables and functions**, and I must emphasize this point here.

- What about local variables? Discussing them is meaningless here. They are served dynamically by the operating system backend after the program runs on the CPU—**assigned to specific registers or allocated memory, but they absolutely do not sit in the executable file on disk!**
- It is particularly worth mentioning that a **definition contains a declaration**. Don't quite understand? For example, if I tell you what A is, haven't I simultaneously told you that an A exists here?

A declaration is simple; we are just loudly proclaiming that something exists here. You ask me what it is? What is its value? Sorry, I don't know; I can only tell you that it definitely exists, and the compiler must find it itself.

A definition is not difficult either; we associate a declaration (which might be the declaration we shouted about elsewhere, or an immediate declaration like `int a = 2`) with its implementation. This action is the **definition**. For global variables, this definition is data. For functions, it is our executable code. The definition of a global variable causes the compiler to allocate specific space for your variable in the resulting executable file. Naturally, it also includes the value you assigned, otherwise, why would you define it?

We know that the relocatable objects generated after compilation expose function names and variables. When writing programs, we subconsciously assume they can be found (astute readers might interrupt me—found when? During compilation or during linking/execution? Don't worry, we'll get to that right away). In serious academic discussion, this is called **symbol visibility**. **Visible symbols are accessible!** This **accessibility of visible symbols** requires a dichotomous discussion:

- Accessibility during compilation—this refers to symbols in C programs that are **not modified by `static`, including global variables and functions**. If you have written C programs, you clearly know that after writing `static int a = 1;` and `static int max(int a, int b){return a > b ? a : b;}` in `a.c`, `b.c` cannot access them at all! You can try it yourself.
- Accessibility during execution—this refers to all global variables and functions, regardless of whether they are modified by `static`. Because they are stored in the executable file, once on the CPU, the operating system must allocate memory storage for the program's lifetime for all global variables and functions, `static` or not. Therefore, for the CPU, they exist for the life of the program. Thus, they are still global, only that some global variables must **only be accessible by specific code** (this is where `static` does its work).

In other words, any **accessible global variable or function** must exist for the life of the program and needs to be placed in the program's executable file, occupying a certain amount of space (this is also why I said discussing only global variables and functions is meaningful). The rest of the content is completely irrelevant to our question. I have written a program here:

```c
// demo.c
int un_g_initialized_var;
int g_initialized_var = 1;

extern int extern_var;

static int un_init_local_var;
static int init_local_var = 1;

static int local_func() {
 return 1;
}

int func() {
 return 2;
}

extern int extern_func();

int main() {
 return extern_var + extern_func();
}

```

| Symbol | Category | Storage Class | Linkage | Typical Segment | Function |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `un_g_initialized_var` | Variable definition | **Static** duration | **External** | **BSS** (Block Started by Symbol) | Uninitialized global variable, initialized to zero at runtime. |
| `g_initialized_var` | Variable definition | **Static** duration | **External** | **Data** (Initialized Data) | Initialized global variable. |
| `extern_var` | Variable declaration | N/A (Reference) | **External** | N/A (Expected to be defined in another file) | References a global variable defined in another compilation unit. |
| `un_init_local_var` | Variable definition | **Static** duration | **Internal** | **BSS** | Static variable with file scope, uninitialized, initialized to zero at runtime. |
| `init_local_var` | Variable definition | **Static** duration | **Internal** | **Data** | Static variable with file scope, initialized. |
| `local_func` | Function definition | **Function** | **Internal** | **Code** (.text) | Static function, can only be called within the current file. |
| `func` | Function definition | **Function** | **External** | **Code** (.text) | Regular function, available for other files to call. |
| `extern_func` | Function declaration | **Function** | **External** | N/A (Expected to be defined in another file) | References a function defined in another compilation unit. |

Take a moment to review the table above. If you find anything confusing, feel free to search for explanations to help you understand it.

## How the C Compiler Views Our Files

Let's get the C compiler working. Note that your compilation command must be

```cpp

gcc -c demo.c -o demo.o # 欸，注意可不要掉-c，标识只编译

```

The compiler quietly compiles for a while and gives us the `demo.o` we wanted. So, what exactly is the compiler doing when compiling an entire C translation unit?

Whether you are using Apple Clang, GNU GCC, or Microsoft MSVC, they are all **compilers**. As you have seen, their main job is to convert C files from human-readable text (excluding "mountain-sea" code) into something the computer can understand. The compiler outputs the result as an object file. On UNIX platforms, these object files usually have an `.o` suffix; on Windows, they have a `.obj` suffix.

Interestingly, circling back to our main topic, our object files ultimately generate at least these two sections:

- **Machine code**: Machine code consists of specific instructions made of zeros and ones that the computer can understand.
- **Data from global variables**: These correspond to the definitions of global variables in the C file (for initialized global variables, the initial values must also be stored in the object file).

Now, the question arises. If you look closely at `extern int extern_var;` and `extern int extern_func();`, those familiar with the `extern` keyword will immediately point out that something is wrong—Hmm? Your `extern_var` and `extern_func` aren't implemented at all. Did the compiler not notice this?

I will tell you this: it knows, but **C/C++ compiled languages allow you to have only declarations during compilation without requiring implementations!** I must emphasize this useful yet troublesome feature again: **C/C++ compiled languages allow you to have only declarations during compilation without requiring implementations!** So, when is this issue adjudicated to determine whether you intentionally placed these implementations elsewhere or simply carelessly omitted them? The answer is in the next stage: linking. We will discuss that later; for now, let's keep our focus on the compilation stage.

## nm, a handy command

Windows MSVC users, don't bother; you should be using `dumpbin` instead of `nm` (assuming you installed MSVC, or in other words, you are using Visual Studio to write code). However, here, I will discuss `nm` based on the System V output format.

How do we verify the content discussed above using the resulting executable file? It is simple; we just take our `nm` tool and analyze it. Come on, give it a try:

```cpp

[charliechen@Charliechen linkers]$ nm -f sysv demo.o

Symbols from demo.o:

Name                  Value           Class        Type         Size             Line  Section

extern_func         |                |   U  |            NOTYPE|                |     |*UND*
extern_var          |                |   U  |            NOTYPE|                |     |*UND*
func                |000000000000000b|   T  |              FUNC|000000000000000b|     |.text
g_initialized_var   |0000000000000000|   D  |            OBJECT|0000000000000004|     |.data
init_local_var      |0000000000000004|   d  |            OBJECT|0000000000000004|     |.data
local_func          |0000000000000000|   t  |              FUNC|000000000000000b|     |.text
main                |0000000000000016|   T  |              FUNC|0000000000000013|     |.text
un_g_initialized_var|0000000000000000|   B  |            OBJECT|0000000000000004|     |.bss
un_init_local_var   |0000000000000004|   b  |            OBJECT|0000000000000004|     |.bss

```

Alright, let's take a closer look at this table. What we need to focus on is the **Class** column, as it explains the nature of the entries in this table.

- The **U** class represents **Undefined references**, which corresponds to one of the "blanks" mentioned earlier. In this object, there are two classes: `fn_a` and `z_global`.
- The **t** or **T** class indicates where code is defined; the different classes specify whether the function is a local function (**t**) or a non-local function (**T**)—that is, whether the function was originally declared with `static`. Similarly, some systems might also display a section, such as `.text`.
- The **d** or **D** class represents initialized global variables; similarly, the specific class indicates whether the variable is a local variable (**d**) or a non-local variable (**D**). If a section is present, it resembles `.data`.
- For uninitialized global variables, if it is a static/local variable, it returns **b**; if not, it returns **B** or **C**. In this case, the section likely resembles `.bss` or `*COM*`.

For those on Windows, you need to open the **x86 Native Tools Command Prompt for VS Insiders**, navigate to the directory containing your target C file, and enter `cl /c <SourceFile>.c`. This instructs MSVC to compile only our source file, and the resulting `<SourceFile>.obj` is our relocatable object file. At this point, we can use the `dumpbin` utility:

```cpp

dumpbin /symbols <SourceFile>.obj

```

Let's check the symbols. Here, I will enumerate the results obtained (using the default toolchain in VS2026).

```cpp

D:\Windows_Programming\WindowsProgramming\demos\demos>dumpbin /symbols main.obj
Microsoft (R) COFF/PE Dumper Version 14.50.35615.0
Copyright (C) Microsoft Corporation.  All rights reserved.

Dump of file main.obj

File Type: COFF OBJECT

COFF SYMBOL TABLE
000 01048B1F ABS    notype       Static       | @comp.id
001 80010191 ABS    notype       Static       | @feat.00
002 00000003 ABS    notype       Static       | @vol.md
003 00000000 SECT1  notype       Static       | .drectve
 Section length   2F, #relocs    0, #linenums    0, checksum        0
005 00000000 SECT2  notype       Static       | .debug$S
 Section length   90, #relocs    0, #linenums    0, checksum        0
007 00000004 UNDEF  notype       External     | _un_g_initialized_var
008 00000000 SECT3  notype       Static       | .data
 Section length    4, #relocs    0, #linenums    0, checksum B8BC6765
00A 00000000 SECT3  notype       External     | _g_initialized_var
00B 00000000 SECT4  notype       Static       | .text$mn
 Section length   20, #relocs    2, #linenums    0, checksum EBBC6B4A
00D 00000000 SECT4  notype ()    External     | _func
00E 00000000 UNDEF  notype ()    External     |_extern_func
00F 00000010 SECT4  notype ()    External     |_main
010 00000000 UNDEF  notype       External     | _extern_var
011 00000000 SECT5  notype       Static       | .chks64
 Section length   28, #relocs    0, #linenums    0, checksum        0

String Table Size = 0x46 bytes

Summary

       28 .chks64
        4 .data
       90 .debug$S
       2F .drectve
       20 .text$mn

```

Let's strip away the other messy outputs; essentially, we are left with the following table:

| `dumpbin` Output                                    | Meaning                                   | Analogy to Linux `nm` |
| --------------------------------------------------- | ----------------------------------------- | --------------------- |
| `SECT4  notype () External \| _func`                | External function defined in `.text`     | `T _func`             |
| `SECT3  notype External    \| _g_initialized_var`   | External variable defined in `.data`     | `D _g_initialized_var` |
| `UNDEF  notype External    \| _extern_func`         | Undefined external function reference    | `U _extern_func`      |
| `UNDEF  notype External    \| _extern_var`          | Undefined external variable reference     | `U _extern_var`       |
| `UNDEF  notype External    \| _un_g_initialized_var` | Undefined external variable reference     | `U _un_g_initialized_var` |

## Resolving Unknown Symbols: Linking

Now, let's take this a step further. In this step, we address the problem we left open in the section "How the C Compiler Views Our Files." We assume that these external symbols are actually defined in other files:

```c
// demo_extern.c
int extern_var = 10;
int extern_func() {
 return 3;
}

```

We compile these symbols into relocatable object files as well. The remaining task is to combine these files, which contain a mix of defined and undefined symbols, to **resolve the undefined parts (where only the name is known) in each file** (since our compiler successfully compiled these source files, we know that these symbols were declared, but their definitions have not yet been found). **This is exactly what we need to do during the linking process.**

Now, after compiling `demo_extern.c` into `demo_extern.o`, we use this object file to complete the final step of creating our executable file:

```cpp

gcc demo_extern.o demo.o -o demo_exe

```

The compilation, of course, passes smoothly. There is no doubt about that.

```cpp

charliechen@Charliechen linkers]$ nm -f sysv demo_exe

Symbols from demo_exe:

Name                  Value           Class        Type         Size             Line  Section

__bss_start         |000000000000401c|   B  |            NOTYPE|                |     |.bss
__cxa_finalize@GLIBC_2.2.5|                |   w  |              FUNC|                |     |*UND*
__data_start        |0000000000004000|   D  |            NOTYPE|                |     |.data
data_start          |0000000000004000|   W  |            NOTYPE|                |     |.data
__dso_handle        |0000000000004008|   D  |            OBJECT|                |     |.data
_DYNAMIC            |0000000000003e20|   d  |            OBJECT|                |     |.dynamic
_edata              |000000000000401c|   D  |            NOTYPE|                |     |.data
_end                |0000000000004028|   B  |            NOTYPE|                |     |.bss
extern_func         |0000000000001119|   T  |              FUNC|000000000000000b|     |.text
extern_var          |0000000000004010|   D  |            OBJECT|0000000000000004|     |.data
_fini               |0000000000001150|   T  |              FUNC|                |     |.fini
func                |000000000000112f|   T  |              FUNC|000000000000000b|     |.text
g_initialized_var   |0000000000004014|   D  |            OBJECT|0000000000000004|     |.data
_GLOBAL_OFFSET_TABLE_|0000000000003fe8|   d  |            OBJECT|                |     |.got.plt
__gmon_start__      |                |   w  |            NOTYPE|                |     |*UND*
__GNU_EH_FRAME_HDR  |0000000000002004|   r  |            NOTYPE|                |     |.eh_frame_hdr
_init               |0000000000001000|   T  |              FUNC|                |     |.init
init_local_var      |0000000000004018|   d  |            OBJECT|0000000000000004|     |.data
_IO_stdin_used      |0000000000002000|   R  |            OBJECT|0000000000000004|     |.rodata
_ITM_deregisterTMCloneTable|                |   w  |            NOTYPE|                |     |*UND*
_ITM_registerTMCloneTable|                |   w  |            NOTYPE|                |     |*UND*
__libc_start_main@GLIBC_2.34|                |   U  |              FUNC|                |     |*UND*
local_func          |0000000000001124|   t  |              FUNC|000000000000000b|     |.text
main                |000000000000113a|   T  |              FUNC|0000000000000013|     |.text
_start              |0000000000001020|   T  |              FUNC|0000000000000026|     |.text
__TMC_END__         |0000000000004020|   D  |            OBJECT|                |     |.data
un_g_initialized_var|0000000000004020|   B  |            OBJECT|0000000000000004|     |.bss
un_init_local_var   |0000000000004024|   b  |            OBJECT|0000000000000004|     |.bss
[charliechen@Charliechen linkers]$

```

Now let's look at the table. It has become quite complex, but that's okay. What we care about most is:

```cpp

extern_func         |0000000000001119|   T  |              FUNC|000000000000000b|     |.text
extern_var          |0000000000004010|   D  |            OBJECT|0000000000000004|     |.data

```

We have finally found the content we are looking for. They are no longer uncertain UNDEF symbols, but defined functions and global variables. We can try removing the implementation of `extern_func`.

```cpp

[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `main':
demo.c:(.text+0x1b): undefined reference to `extern_func'
collect2: error: ld returned 1 exit status

```

A familiar error has appeared! `undefined reference`, which means the linker is complaining that it cannot find the definition for `extern_func`. Let's take a closer look:

```cpp

[charliechen@Charliechen linkers]$ nm -f sysv demo_extern.o
Symbols from demo_extern.o:

Name                  Value           Class        Type         Size             Line  Section

extern_var          |0000000000000000|   D  |            OBJECT|0000000000000004|     |.data

```

You can see that `demo_extern` resolves the definition of `extern_var`, but the definition for `extern_func` is missing. Since we only provided these two files, the linker doesn't know where to find your `extern_func`, so it naturally throws this error.

We now understand a key function of the linker: resolving undefined symbols in the smallest possible executable (why "smallest"? We'll discuss that later). Any link where **you fail to provide the corresponding information defining the specific content** (like missing the source code for a used function) will fail! After the linker finishes its search, if any undefined symbols remain (that is, symbols with a Class of `U` in `nm` or `dumpbin`), the linker will raise an error telling you exactly which symbols are undefined. **The solution is quite simple at this point—find the relocatable files for these symbols (generally, build systems keep the source filename and relocatable filename identical, differing only in extension), and provide them during linking!** This is the **only way** to resolve `undefined reference` errors in scenarios without dynamic libraries.

Now that we've looked at the output from `nm`, we can answer the whole question:

- **Q1:** How does the compiler toolchain collect and find symbols? How does it further transform them into a more manageable form?
- **A:** The answer is that the compiler compiles symbols into machine-understandable instructions, **mapping function symbols to an address**. For global variables, it maps a global variable to a specific access location within the data segment.
- **Q2:** **What do the variables and functions we write actually mean to the computer?**
- **A:** It's just associating our addresses with variables that have specific meaning to us; the name you choose doesn't matter. After processing by the compiler and linker, only a string of addresses remains for the computer—if you ask me what that is, I don't know! Ask `nm`!

## Extra Topic: What if we have duplicate definitions?

The previous section mentioned that if the linker cannot find a definition for a symbol to connect with a reference to that symbol, it will give an error message. So, what happens if a symbol has two definitions during linking?

I won't rush to give the answer; try it out yourself first. For example, restore the definition of `extern_func` in `demo_extern`, and then immediately modify our `demo.c` like this:

```c
int un_g_initialized_var;
int g_initialized_var = 1;

extern int extern_var;

static int un_init_local_var;
static int init_local_var = 1;

static int local_func() {
 return 1;
}

int extern_func() { // 拷贝一份定义到这里，return您随意，因为就不影响我们的结论
 return 3;
}

int func() {
 return 2;
}

// extern int extern_func(); <- 注释掉外部查找的强调关键字extern

int main() {
 return extern_var + extern_func();
}

```

We repeat the individual compilation and linking steps above. Soon, we encounter another error you might be familiar with:

```cpp

[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `extern_func':
demo.c:(.text+0xb): multiple definition of `extern_func'; demo_extern.o:demo_extern.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status

```

You might have noticed that it's the same result, because the compiler trusts that **the linker will correctly handle symbol relationships** (it can only compile files one by one! It cannot manage other source files globally! **The symbol resolution for the final compilation unit, including executables, dynamic libraries, and static libraries, is determined by the linker**! I must emphasize this point again!).

Therefore, during linking, the linker discovers that there are identical symbol definitions in two files. Naturally, the definitions conflict—just like saying A is 1, and then also saying A is 2. Uniqueness is broken, and making an arbitrary decision would only make the program uncontrollable. Consequently, the linker rejects this immediately! At least with the default behavior of the GNU toolchain today, doing this will only earn you a `multiple definition` error.

## Is that all the linker does?

Since I'm asking this, it obviously isn't that simple, is it? I wonder if, while seeing me repeatedly emphasize this sentence, you've felt this:

- Why is it that **C/C++ compiled languages allow you to have only declarations without definitions during compilation**! Why isn't it required to know immediately? It seems like such a hassle.

Think about it calmly for a moment. Let me give you an example. If I ask you to go to the post office to mail a letter, you certainly wouldn't interrupt me: "Shut up, pal. Bring the post office here first, and once I see the letter, I'll help you mail it." Instead, you would visualize a hypothetical post office in your mind, "Okay, I need to go to a place called a post office to mail a letter." You would then look for the letter elsewhere. It's the same principle. We leave symbols unresolved and pending; we manage and promise that they will appear in the right places—**this is your responsibility, not the compiler's**. Now, we can continue our question:

- So, besides providing source code, can we provide information in other forms?

Hey! Excellent observation. If you look closely at what I did here...

```cpp

[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe

```

Have you noticed that the linking step we discussed doesn't seem to care about the source files? After all, we search for undefined symbols in relocatable files (`*.o`). So, couldn't we prepare a collection of relocatable files and a set of symbol declaration files in advance? Then, when programming, we wouldn't need to reinvent the wheel. We could simply **inform the compiler via these declaration files that we guarantee these symbols exist**, **generate our own relocatable files during compilation**, and finally **combine these pre-prepared relocatable files with our own during linking to produce an executable file**?

Congratulations! You have reinvented the concepts of libraries and interface programming! Now you know what header files are for! They are simply files containing symbol declarations. And for those thousands of relocatable files, let's not leave them scattered; let's **bundle them together into a library**. How about that? Of course we can! You have just reinvented the historically famous **static library**. I'm getting a bit excited, but I need to reorganize the concepts we've introduced:

- **Header files**: These are symbol declaration files that **place the symbol declarations we guarantee to exist**.
- **Static libraries**: These contain the specific definitions of these symbols (all or part of them; the remaining unresolved symbols might depend on other libraries—interesting, right?).

So my point is—the linker can also link libraries. I didn't just say static libraries; there are dynamic libraries too. Let's talk about static libraries first.

## Static Libraries: Our Symbol Library

We can use AR (on Linux or Unix systems) or the Lib tool to bundle all relocatable files into a static library.

> A quick note on details:
>
> - On **UNIX** systems, the command to generate a static library is usually **`ar`**, and the resulting library file usually has an **`.a`** extension. These library files usually start with **"lib"** as a prefix, and when passed to the linker, the **`"-l"`** option is used followed by the library name (without the prefix and extension). For example, **`"-lfred"`** will select the **`libfred.a`** file. (Historically, static libraries also required a program called **`ranlib`** to build a symbol index at the beginning of the library. Nowadays, the **`ar`** tool usually handles this automatically.)
> - On **Windows** systems, static libraries have a **`.LIB`** extension and are generated by the **`LIB`** tool. This can be confusing because "**import libraries**" also use the same extension, which merely contain a list of what is available in a DLL.

During the linking phase, when we provide a static library to the linker, the linker holds a table of unresolved symbols and dives into the static library to find these symbols one by one (for example, if symbol A is missing and it is in `Obj1.o`, we will link the entirety of `Obj1.o` in) until all undefined symbol problems are resolved.

Please note the **granularity** of extracting content from the library: if the definition of a specific symbol is needed, the **entire object file** containing that symbol definition is included. This means the process can be "one step forward, one step back"—the newly added object file might resolve an undefined reference, but it will likely bring a whole new set of its own undefined references for the linker to resolve.

The [`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html) contains an excellent example, which I have placed below for you to read:

Suppose we have the following object files, and the link command line includes **`a.o`**, **`b.o`**, **`-lx`**, and **`-ly`**.

| File           | **a.o**    | **b.o** | **libx.a**                             | **liby.a**                   |
| -------------- | ---------- | ------- | -------------------------------------- | ---------------------------- |
| **Objects**    | a.o        | b.o     | x1.o, x2.o, x3.o                       | y1.o, y2.o, y3.o             |
| **Definitions**| a1, a2, a3 | b1, b2  | x11, x12, x13; x21, x22, x23; x31, x32 | y11, y12; y21, y22; y31, y32 |
| **Undefined References** | b2, x12    | a3, y22  | x23, y12; y11; y21                     | x31                          |

1. **Processing `a.o` and `b.o`:**
   - The linker will resolve references to `b2` and `a3`.
   - At this point, the undefined references remaining are **`x12`** and **`y22`**.
2. **Processing `libx.a`:**
   - The linker checks the first library `libx.a` and finds it can pull in **`x1.o`** to satisfy the `x12` reference.
   - However, pulling in `x1.o` also brings new undefined references `x23` and `y12`. (The undefined list is now: `y22`, `x23`, and `y12`).
   - The linker is still processing `libx.a`, so the `x23` reference is easily satisfied by pulling in **`x2.o`**.
   - But this adds `y11` to the undefined list. (The undefined list is now: `y22`, `y12`, and `y11`).
   - No other object files in `libx.a` can resolve these remaining symbols, so the linker moves on to `liby.a`.
3. **Processing `liby.a`:**
   - In a similar flow, the linker will pull in **`y1.o`** and **`y2.o`**.
   - Pulling in `y1.o` adds a reference to `y21`, but since `y2.o` is being pulled in anyway, this reference is easily resolved.
   - The final result is: all undefined references are resolved, and some (but not all) object files from the libraries are included in the final executable file.

#### The Importance of Link Order

Note that if (for example) `b.o` also had a reference to `y32`, the situation would be different.

- The way `libx.a` is linked would remain the same.
- When processing `liby.a`, the linker would also pull in **`y3.o`** to resolve `y32`.
- Pulling in `y3.o` adds **`x31`** to the unresolved symbol list.
- At this point, the linker has **finished** processing `libx.a`, so it cannot find the definition for this symbol (in `x3.o`), resulting in a **link failure**. This example clearly illustrates the importance of link order (`libx.a` before `liby.a`). That is to say, the linker does not go backward. When linking, you must clearly define that the dependencies of programming symbols must be progressive dependencies, not circular ones—don't make trouble for yourself!

## Dynamic Libraries/Shared Libraries

Of course, for now, you can simply understand this as a dynamic library. Strictly speaking, there is a slight difference between the two, but in an introduction, being too strict will only scare people away.

Dynamic libraries exist primarily to solve a major drawback of static libraries—every executable program holds a copy of the same code. If every executable file contained a copy of functions like `printf` and `fopen`, it would take up a significant amount of unnecessary disk space.

> You can do an interesting experiment: statically link the C library and see how large it is. Please look up the specific instructions yourself; my result was several hundred MB.

Of course, you might say—"I have money; I can add SSDs freely." That's not the most serious problem. The most serious problem is—if the provider's code has a bug, you are done—all the code is written into the executable file, and you cannot use this executable file at all—until someone else spends months compiling it for you!

To solve these troublesome problems, shared libraries/dynamic libraries appeared (usually represented by the `.so` extension, `.dll` on Windows computers, and `.dylib` on Mac OS X). At this point, the linker adopts an "IOU" approach and defers the payment of the IOU to the moment the program actually runs. Ultimately, it means: if the linker finds that the definition of a symbol exists in a shared library, it will not include the definition of that symbol in the final executable file. Instead, the linker records the name of the symbol in the executable file and which library it should come from.

When the program runs, the operating system arranges for these remaining linking tasks to be completed "just in time" for the program to run. Before the main function runs, a smaller version of the linker (usually called `ld.so`) checks these "IOUs" and immediately completes the final stage of linking—pulling in the library code and connecting all the code. This means that no executable file has a copy of the `printf` code. If a new, fixed version of `printf` is available, you only need to change `libc.so` to plug it in—the next time any program runs, it will be picked up.

Shared libraries also have another major difference in how they work compared to static libraries, which is reflected in the granularity of linking. If a specific symbol is extracted from a specific shared library (e.g., `printf` in `libc.so`), the entire shared library is mapped into the program's address space. This is starkly different from the behavior of static libraries, where only the specific object containing the undefined symbol is extracted.

We will stop here regarding shared libraries. I have a nearly 300-page book called "Advanced C/C++ Compilation Techniques" on hand that specifically discusses dynamic/shared library technology. This is enough to show how complex this topic is. We will discuss it carefully in a later blog post. For the introduction, we will stop here.

## Other Topics: What About C++?

#### C++ Name Mangling

Going back to this `usage.cpp`:

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
 int a = 1, b = 2;
 std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}

```

When we use the `int_max(int a, int b)` function in the C++ file **`usage.cpp`**, the C++ compiler (`g++`) does not simply map the function name to `int_max` like a C compiler would. To support features not found in C, such as **function overloading**, **namespaces**, and **class member functions**, the C++ compiler performs complex encoding on the function names in the source code. This process is called **name mangling**.

```cpp

int int_max(int a, int b);

```

When the `g++` compiler generates the **`usage.o`** object file, it expects the linker to find a mangled symbol. For example, in a GCC/Linux environment, it might look for a symbol like **`_Z7int_maxii`** (the specific mangling result varies by compiler and platform, but it is **definitely not** a simple `int_max`).

#### Symbol Names in C Libraries

The problem is that the static library **`libutils.a`** is generated by compiling the **`lib.c`** file with a **C compiler** (typically `gcc` or `cc`). C compilers **do not perform name mangling**. Therefore, in **`libutils.a`**, the symbol name for the `int_max` function is simply **`int_max`** (or possibly with an underscore prefix, like `_int_max`).

You will see the issue immediately.

```cpp

g++ usage.cpp -L. -lutils -o usage

```

1. **`g++`** compiles `usage.cpp` to generate `usage.o`, which contains an **undefined reference** to a **mangled name** (e.g., `_Z7int_maxii`).
2. The **linker** (`ld`) goes to work. It looks for `int_max` in `usage.o`, but only finds a requirement for `_Z7int_maxii`.
3. The linker searches for `_Z7int_maxii` in **`libutils.a`**, but the symbol existing in the library is **`int_max`**.
4. The linker cannot find a matching symbol, so it reports an error: `undefined reference to 'int_max(int, int)'` (Note: the error message displays the C++ style function signature, but the linker is actually looking for its mangled version).

#### Solution: Using `extern "C"`

To solve this problem, we need to tell the C++ compiler: **"Hey, this function was compiled with a C compiler, don't mangle its name!"** We only need to use the **`extern "C"`** linkage specifier around the **function declaration** in the C++ file:

```cpp
// in usage usage.cpp

#include <iostream>

// 使用 extern "C" 告诉 C++ 编译器，这个函数的符号名要按照 C 语言的方式处理
// 即不进行名称修饰，直接查找 'int_max'
extern "C" int int_max(int a, int b);

int main() {
    int a = 1, b = 2;
    std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
    return 0; // 补充返回语句
}

```

Recompile and link, and the program will run successfully, because the symbol referenced in `usage.o` will now be the simple `int_max`, matching the symbol provided in `libutils.a`.
