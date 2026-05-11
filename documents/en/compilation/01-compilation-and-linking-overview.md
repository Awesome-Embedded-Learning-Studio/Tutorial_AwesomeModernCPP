---
title: 'In-Depth Understanding of C/C++ Compilation and Linking Technologies: Introduction'
description: ''
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
chapter: 13
order: 1
---
# Deep Dive into C/C++ Compilation and Linking: Introduction

## Preface

This is a new series! It is a topic I plan to systematically and deeply explore this week. Specifically, we will discuss and summarize a series of topics in C/C++ programming that we often gloss over but that inevitably cause us grief—compilation and linking technologies. I believe everyone has encountered headache-inducing `undefined referenced` errors. I know seeing such errors can make anyone jump (the author was recently tormented by `undefined referenced` during template instantiation).

When solving these problems, I believe many of us initially panic, ask AI, or search the internet, but few truly stop to think—why do we get `undefined referenced` errors in the first place? Setting aside cases where we genuinely forgot to provide source code files in our build system (I believe many have encountered this, myself included), there are many situations where we truly did provide the source files—at least, we genuinely believe we did—and you can even see them being linked, but the linking still fails.

For example, suppose you write code in a `lib.c` file and build it into a static library `libutils`.

```c
int int_max(int a, int b) {
 return a > b ? a : b;
}

```

Then, we immediately use `int_max` in a C++ file:

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
 int a = 1, b = 2;
 std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}

```

Next, we type the command expecting our program to compile successfully, but we get a very strange error:

```cpp

[charliechen@Charliechen linkers]$ g++ usage.cpp -L. -lutils -o usage
/usr/sbin/ld: /tmp/ccdSskJz.o: in function `main':
usage.cpp:(.text+0x88): undefined reference to `int_max(int, int)'
collect2: error: ld returned 1 exit status
[charliechen@Charliechen linkers]$

```

This looks bizarre. We clearly linked `libutils`, and the linker even found it (it didn't complain about `/usr/sbin/ld: cannot find -lutils: No such file or directory`, which means it was found). So why the error? And even if the symbol wasn't found, why didn't it complain during compilation? I think if you can immediately spot the problem, just like the author of [`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html) suggests, then this introductory "Deep Dive into C/C++ Compilation and Linking: Introduction" won't offer you anything new. We will dive into the fine details later, not here.

**This blog post assumes you have at least written a C program (although the problem above involves C++, the core of this article is not C++ specific). If you have encountered errors like `undefined referenced` and didn't know how to solve them, even better!**

## So, what do our variables and functions actually mean?

This question isn't for **you**; it's for the **computer**. To answer the string of questions you might never have thought about, we must first answer one question: "How does the computer know about the things we can and cannot find?" More formally—how does the compiler toolchain collect and look up symbols? How does it further transform them into a more manageable form (for example, mapping a function to an address the computer can find? Those familiar with assembly will immediately realize how functions work—once a function name is converted to an address, you simply `call` that address, and the computer's execution flow automatically jumps to the corresponding address to fetch and execute instructions). Ultimately, our first step is—how do the variables and functions we understand, which express business logic, get transformed into addresses telling the machine where everything is? What happens in between? **What do our variables and functions actually mean to the computer?**

Any computer science student can undoubtedly rattle off the four classic steps of taking a program from source code to running on an operating system—preprocessing, compilation, linking, and **execution** (Someone might ask: isn't that obvious? Why single out execution? Good question! We will thoroughly discuss dynamic loading and startup loading of dynamic libraries later).

To answer the above question well, we need to focus on the latter three (preprocessing is a **source-code-to-source-code transformation**, such as `#define` expansion and conditional compilation using `#if`, which we won't discuss here).

When writing C files—whether it's Bilibili educators, expert bloggers' notes, or your half-asleep university professor reading from dusty old PPTs—they will all tell you the same thing. When writing C files, we are essentially doing two things—declarations and definitions. Our subjects of discussion are **global variables and functions**, which I must emphasize here.

- What about local variables? Well, discussing them is meaningless here. Once the program is on the CPU, the entire operating system backend serves your program code dynamically—they might be **assigned to specific registers, or allocated memory, but they absolutely do not lie on disk in the executable file!**
- It's particularly worth mentioning that a definition includes a declaration. Don't quite get it? Think about it: if I've already told you what A is, haven't I simultaneously told you that an A exists here?

A declaration is simple; we are just loudly proclaiming that something exists here (). You ask me what it is? What's its value? Sorry, I don't know; I can only tell you that it definitely exists, and the compiler can go find it itself.

A definition isn't hard either; we associate a declaration (either one we proclaimed elsewhere, or an inline declaration like `int a = 2`) with its actual implementation. This action is the **definition**. For a global variable, this definition is a piece of data. For a function, it's our executable code. A global variable's definition causes the compiler to allocate specific space for your variable in the resulting executable file. Of course, it also includes the value you assigned—otherwise, why define it at all, right?

We know that relocatable files generated after compilation expose function names and variables. When writing programs, we subconsciously assume they can be found (astute readers will immediately interrupt me—found where? During compilation or during linking/execution? Don't worry, we'll get to that soon)—this is formally called **symbol visibility** in serious academic discussions. **Visible symbols are accessible!** The **accessibility of visible symbols** requires a dichotomous discussion:

- Accessibility during compilation—for example, symbols in a C program **not modified by `static`, including global variables and functions**. If you've written C programs, you clearly know that after writing global `static int a = 1;` and `static int max(int a, int b){return a > b ? a : b;}` in `a.c`, `b.c` cannot access them at all! You can try it yourself.
- Accessibility during execution—this refers to all global variables and functions, whether modified by `static` or not. Because they are all stored in the executable file, once on the CPU, the operating system must allocate memory storage for the entire program lifetime for all global variables and functions, regardless of `static` modification. So in practice, for the CPU, they exist for the lifetime of the program. Therefore, they are still global, just that some global variables must **only be accessible by specific code** (this is where `static` does its work).

In other words, any **accessible global variable or function** must exist for the lifetime of the program, needs to be placed in the program's executable file, and occupy a certain amount of space (which is why I said only discussing global variables and functions is meaningful). Everything else is completely irrelevant to our question. I wrote a program here:

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

| Symbol | Category | Storage Class | Linkage | Typical Segment (Runtime on CPU) | Function |
| ---------------------- | --------------- | ---------------------------- | --------------------- | --------------------------------------------- | ------------------------------------------------------ |
| `un_g_initialized_var` | Variable definition | **Global** (`static` duration) | **External** (`External`) | **BSS** (Block Started by Symbol) | Uninitialized global variable, initialized to 0 at runtime. |
| `g_initialized_var` | Variable definition | **Global** (`static` duration) | **External** (`External`) | **Data** (Initialized Data) | Initialized global variable. |
| `extern_var` | Variable declaration | N/A (Reference) | **External** (`External`) | N/A (Expected to be defined in another file) | References a global variable defined in another compilation unit. |
| `un_init_local_var` | Variable definition | **Global** (`static` duration) | **Internal** (`Internal`) | **BSS** | File-scoped static variable, uninitialized, initialized to 0 at runtime. |
| `init_local_var` | Variable definition | **Global** (`static` duration) | **Internal** (`Internal`) | **Data** | File-scoped static variable, initialized. |
| `local_func` | Function definition | **Function** | **Internal** (`Internal`) | **Code** (.text) | Static function, can only be called within the current file. |
| `func` | Function definition | **Function** | **External** (`External`) | **Code** (.text) | Regular function, available for other files to call. |
| `extern_func` | Function declaration | **Function** | **External** (`External`) | N/A (Expected to be defined in another file) | References a function defined in another compilation unit. |

Think about the table above. If you find anything confusing, feel free to search for more information to understand it.

## How the C Compiler Views Our Files

Let's get the C compiler working. Note that your compilation command must be:

```cpp

gcc -c demo.c -o demo.o # 欸，注意可不要掉-c，标识只编译

```

The compiler quietly works for a moment and gives us the `demo.o` we wanted. So what is the compiler doing when compiling an entire C file?

Whether you are using Apple Clang, GNU GCC, or Microsoft's MSVC, they are all **compilers**, and their main job, as you can see, is to convert C files from human-readable text (spaghetti code excepted) into something the computer can understand. The compiler outputs the result as an object file. On UNIX platforms, these object files usually have an `.o` suffix; on Windows platforms, they have an `.obj` suffix.

Interestingly, circling back to our main topic, our object files ultimately generate at least the following two parts in terms of content:

- Machine code: Machine code is specific instructions made up of 0s and 1s that the computer can understand.
- Data evolved from global variables: These correspond to the definitions of global variables in the C file (for initialized global variables, the initial values must also be stored in the object file).

Well, here's the question: look closely at `extern int extern_var;` and `extern int extern_func();`. Those familiar with the `extern` keyword will immediately spot something wrong—wait? Your `extern_var` and `extern_func` have no definitions at all! Didn't the compiler notice?

What I'm telling you is—it knows about this, but **C/C++ compiled languages allow you to have only declarations without definitions during compilation!** I must emphasize this **useful but troublesome** feature again: **C/C++ compiled languages allow you to have only declarations without definitions during compilation!** So when is the final verdict made on whether you intentionally placed these definitions elsewhere, or if you carelessly omitted them? The answer is the next stage: linking. We'll discuss that later; for now, let's keep our focus on the compilation stage.

## nm, a Handy Command

Windows MSVC users, don't bother; you should be using `dumpbin` instead of `nm` (that is, if you have MSVC installed—my other point being that you're using Visual Studio to write code). But here, I'm going to discuss using `nm` with SystemV output format.

How do we verify what we discussed above using the resulting object file? It's simple; let's just use our `nm` tool to analyze it. Come on, give it a try:

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

Alright, let's look at this table carefully. What you need to do is focus on the Class column, which tells us what each entry is.

- A class of `U` represents an undefined reference, one of the "blanks" mentioned earlier. This object has two of these: "fn_a" and "z_global".
- A class of `t` or `T` represents where code is defined; different classes indicate whether the function is local (`t`) or non-local (`T`)—that is, whether the function was originally declared with `static`. Similarly, some systems might also show a segment, such as `.text`.
- A class of `d` or `D` represents initialized global variables; similarly, the specific class indicates whether the variable is local (`d`) or non-local (`D`). If a segment is shown, it's similar to `.data`.
- For uninitialized global variables, it returns `b` if it's a static/local variable, or `B` or `C` if not. In this case, the segment might look like `.bss` or `*COM*`.

For Windows users, you need to open `x86 Native Tools Command Prompt for VS Insiders`, navigate to your target C file, and type `cl /c <SourceFile>.c`. This way, MSVC will only compile our source file, and the resulting `<SourceFile>.obj` is our relocatable object file. At this point, we can use the `dumpbin` tool:

```cpp

dumpbin /symbols <SourceFile>.obj

```

To view the symbols. Let me enumerate the results I got (using the default toolchain in VS2026):

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

Setting aside other messy output, it essentially comes down to this table:

| `dumpbin` Output | Meaning | Linux `nm` Equivalent |
| --------------------------------------------------- | ------------------------- | --------------- |
| `SECT4  notype () External \| _func` | External function defined in .text | `T _func` |
| `SECT3  notype External    \| _g_initialized_var` | External variable defined in .data | `D _g_initialized_var` |
| `UNDEF  notype External    \| _extern_func` | Undefined external function reference | `U _extern_func` |
| `UNDEF  notype External    \| _extern_var` | Undefined external variable reference | `U _extern_var` |
| `UNDEF  notype External    \| _un_g_initialized_var` | Undefined external variable reference | `U _un_g_initialized_var` |

## Resolving Unknown Symbols: Linking

Now let's push the topic further. This step resolves the question we left hanging in the "How the C Compiler Views Our Files" section. We assume that these external symbols are actually defined in other files:

```c
// demo_extern.c
int extern_var = 10;
int extern_func() {
 return 3;
}

```

These symbols will likewise be compiled into relocatable object files. What remains is to combine these files, which are mixed with various defined and undefined symbols, **resolving the uncertain parts (those with only names) in each file where definitions are unknown** (since our compiler successfully compiled these source files, it means we declared these symbols but haven't found their definitions yet). **This is what we need to do during linking.**

Now, after compiling `demo_extern.c` into `demo_extern.o`, we use this to complete the final step of our executable:

```cpp

gcc demo_extern.o demo.o -o demo_exe

```

Compilation naturally passes smoothly. No doubt about it.

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

Now let's look; the table has become very complex, but that's okay. What we mainly care about is:

```cpp

extern_func         |0000000000001119|   T  |              FUNC|000000000000000b|     |.text
extern_var          |0000000000004010|   D  |            OBJECT|0000000000000004|     |.data

```

We have finally found what we're looking for. They are no longer uncertain UNDEF entries, but confirmed defined functions and global variables. We can completely try removing the definition of `extern_func`.

```cpp

[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `main':
demo.c:(.text+0x1b): undefined reference to `extern_func'
collect2: error: ld returned 1 exit status

```

Our familiar error appears! `undefined reference`, indicating that the linker is complaining it cannot find the definition of `extern_func`. Let's look closely:

```cpp

[charliechen@Charliechen linkers]$ nm -f sysv demo_extern.o
Symbols from demo_extern.o:

Name                  Value           Class        Type         Size             Line  Section

extern_var          |0000000000000000|   D  |            OBJECT|0000000000000004|     |.data

```

You can see that `demo_extern` resolves the definition of `extern_var`, but the definition of `extern_func` was not found. Since we only provided these two files, the linker naturally doesn't know where to find your `extern_func`, so it throws this error.

We now understand the important function of the linker—resolving the undefined symbol problems of the minimal executable file (why minimal? We'll discuss this later). Any linking where **you failed to provide the corresponding information telling it the specific contents of the definition (the source code for used functions was left out)** will fail! Finally, after the linker searches around, as long as there are undefined symbols (that is, symbols whose Class is `U` in `nm` or `dumpbin`), the linker will raise an error telling you about all the undefined symbols. **At this point, your solution is very simple—find the relocatable files for these symbols (generally, build systems keep source file names and relocatable file names the same, differing only in extension), and provide them during linking!** This is the **only way** to resolve `undefined reference` in all compilation scenarios without dynamic libraries.

Now that we've looked at the `nm` output, we can answer the entire question:

- Q1: How does the compiler toolchain collect and find symbols? How does it further transform them into a more manageable form?
- A: The answer is that the compiler compiles symbols into instructions the computer can understand, **mapping function symbols to addresses**. For global variables, it maps a global variable to a specific access location in the data segment.
- Q2: **What do our variables and functions actually mean to the computer?**
- A: It's merely associating our addresses with variables that have specific meanings to us. Whatever name you give them doesn't matter. After processing by the compiler and linker, only a string of addresses remains for the computer—if you ask it what that is, it doesn't know! Ask `nm`!

## Extra Topic: What if We Have Duplicate Definitions?

The previous section mentioned that if the linker cannot find a symbol's definition to connect it with a reference to that symbol, it gives an error message. So, what happens if a symbol has two definitions at link time?

I won't rush to give the answer; try it yourself first. For example, restore the definition of `extern_func` in `demo_extern`, and immediately modify our `demo.c` like this:

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

We repeat the separate compilation and linking steps above. Soon, we get another error you might commonly see:

```cpp

[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `extern_func':
demo.c:(.text+0xb): multiple definition of `extern_func'; demo_extern.o:demo_extern.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status

```

Notice that, as before, because the compiler believes **the linker can correctly handle the relationships of any symbols** (it can only compile files one by one! It can't manage other global source files! **The symbol arbitration for the entire result unit (including executables, dynamic libraries, and static libraries) is determined by the linker!** I must emphasize this again!)

So, during linking, the linker discovers that two files contain exactly the same symbol definition. Naturally, the definitions are different, just like saying A is 1 and then saying A is 2; uniqueness is broken, and making a rash decision would only make the program uncontrollable. So, the linker naturally slaps it back and rejects it! At least under today's default GNU toolchain behavior, doing this will only get you a `multiple definition`.

## Is That All the Linker Does?

Since I'm asking it like this, how could that be all? I don't know if, when you saw me repeatedly emphasizing this phrase, you felt anything:

- Why is it that: **C/C++ compiled languages allow you to have only declarations without definitions during compilation!** Why not require knowing immediately? It's so troublesome.

Think about it calmly. For example, if I ask you to go to a post office to deliver mail, you obviously won't interrupt me: "Shut up buddy, you carry the post office over here first so I can see the mail before I help you deliver it." Rather, you would draw an imaginary post office in your mind: "Hmm, I need to go to a place called a post office to help deliver a piece of mail." You would naturally go elsewhere to find the mail. It's the same principle. We leave pending symbols unresolved, and we manage and promise that they will appear in the right places—**this is your responsibility, not the compiler's.** Good, now we can continue with our question:

- So, besides providing source code, can we provide information in other forms?

Hey! Your observation is excellent. If you looked closely at my operation here:

```cpp

[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe

```

Did you notice that the linking step doesn't seem to have anything to do with source files? After all, we search for undefined symbols from relocatable files (`*.o`). So, could we prepare a set of relocatable files and a set of symbol declaration files in advance, so that when programming, we don't have to reinvent the wheel? We could directly **use these declaration files during programming to tell the compiler we guarantee these symbols exist**, **generate our own relocatable files through compilation**, and then **during linking, combine these pre-prepared relocatable files with our own relocatable files to form an executable**?

Congratulations! You've reinvented the concept of libraries and interface programming! Now you know what header files are for! They are simply symbol declaration files! And as for these thousands of relocatable files, let's not leave them scattered; let's **bundle them together into a library**, how about that? Of course we can! You've just invented the historically **famous static library**. I'm a bit excited, but I need to reorganize the concepts we've introduced:

- Header files: That is, symbol declaration files, **containing symbol declarations where we guarantee the symbols exist**
- Static libraries: The specific definitions of these symbols (all or some; the remaining unresolved symbols might depend on other libraries, interesting right!)

So my point is—the linker can also link libraries. I didn't just mean static libraries; there are dynamic libraries too. Let's talk about static libraries first.

## Static Libraries: Our Symbol Libraries

We can use `ar` (on Linux or UNIX systems) or `lib` to bundle all relocatable files into a static library.

> A quick note on the details:
>
> - On **UNIX** systems, the command to generate a static library is usually **`ar`**, and the generated library file usually has the **`.a`** extension. These library files also typically use **"lib"** as a prefix, and when passed to the linker, the **`"-l"`** option is used, followed by the library name (without the prefix and extension). For example, **`"-lfred"`** will select the **`libfred.a`** file. (Historically, static libraries also required a program called **`ranlib`** to build a symbol index at the beginning of the library. Nowadays, the **`ar`** tool usually does this automatically.)
> - On **Windows** systems, static libraries have the **`.LIB`** extension and are generated by the **`LIB`** tool. But this can cause confusion because **"import libraries"** also use the same extension; import libraries merely contain a list of what's available in a DLL.

For the linking stage, when we provide a static library to the linker, our linker holds a table of unresolved symbols and dives into the static library to find these symbols one by one (for example, if symbol A is missing and it's in `Obj1.o`, we will link all of `Obj1.o` in), until we've resolved all undefined symbol problems.

Please note the **granularity** of extracting content from a library: if the definition of a specific symbol is needed, the **entire object file** containing that symbol's definition is brought in. This means the process can be "one step forward, two steps back"—the newly added object file might resolve one undefined reference, but it will likely also bring a whole new set of its own undefined references for the linker to resolve.

There is an excellent example in [`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html), which I've included below. Please read it:

Suppose we have the following object files, and the link line includes **`a.o`**, **`b.o`**, **`-lx`**, and **`-ly`**.

| File | **a.o** | **b.o** | **libx.a** | **liby.a** |
| -------------- | ---------- | ------- | -------------------------------------- | ---------------------------- |
| **Objects** | a.o | b.o | x1.o, x2.o, x3.o | y1.o, y2.o, y3.o |
| **Definitions** | a1, a2, a3 | b1, b2 | x11, x12, x13; x21, x22, x23; x31, x32 | y11, y12; y21, y22; y31, y32 |
| **Undefined References** | b2, x12 | a3, y22 | x23, y12; y11; y21 | x31 |

1. **Processing `a.o` and `b.o`:**
   - The linker will resolve references to `b2` and `a3`.
   - At this point, the undefined references left are **`x12`** and **`y22`**.
2. **Processing `libx.a`:**
   - The linker checks the first library, `libx.a`, and finds it can pull in **`x1.o`** to satisfy the `x12` reference.
   - However, pulling in `x1.o` also brings new undefined references `x23` and `y12`. (The undefined list is now: `y22`, `x23`, and `y12`).
   - The linker is still processing `libx.a`, so the `x23` reference is easily satisfied by pulling in **`x2.o`**.
   - But this also adds `y11` to the undefined list. (The undefined list is now: `y22`, `y12`, and `y11`).
   - No other object files in `libx.a` can resolve these remaining symbols, so the linker moves on to process `liby.a`.
3. **Processing `liby.a`:**
   - In a similar flow, the linker will pull in **`y1.o`** and **`y2.o`**.
   - Pulling in `y1.o` adds a reference to `y21`, but since `y2.o` is going to be pulled in anyway, this reference is easily resolved.
   - The final result is: all undefined references are resolved, and some (but not all) object files from the libraries are included in the final executable.

#### The Importance of Link Order

Note that if (for example) `b.o` also had a reference to `y32`, things would be different.

- The linking of `libx.a` would work the same way.
- When processing `liby.a`, the linker would also pull in **`y3.o`** to resolve `y32`.
- Pulling in `y3.o` would add **`x31`** to the unresolved symbol list.
- At this point, the linker has **finished** processing `libx.a`, so it cannot find the definition for this symbol (which is in `x3.o`), resulting in a **link failure**. This example clearly illustrates the importance of link order (`libx.a` before `liby.a`). That is, the linker does not go backward. When linking, you must clearly structure the dependencies of your programming symbols to be progressive rather than circular—don't make trouble for yourself!

## Dynamic Libraries/Shared Libraries

Of course, for now, you can simply understand them as dynamic libraries. Strictly speaking, there is a slight difference between the two, but in an introduction, being this strict will only scare people away.

The existence of dynamic libraries is more to solve an obvious drawback of static libraries—every executable program has its own copy of the same code. If every executable file contains copies of functions like `printf` and `fopen`, this takes up a lot of unnecessary disk space.

> You can do an interesting experiment: statically link the C library and see how large it gets. Please look up the specific commands yourself; my result was several hundred MB.

Of course, you might say—I have money, I can add SSDs as I please. That's not the most serious problem. The most serious problem is—if the provider's code has a bug, you're done—all the code is hardcoded into the executable file, and you cannot use this executable file at all—until someone else spends months compiling a new version to give you!

To solve these troublesome problems, shared libraries/dynamic libraries emerged (usually denoted by the `.so` extension, `.dll` on Windows computers, and `.dylib` on Mac OS X). At this point, the linker takes an "IOU" approach, deferring the payment of the IOU to the moment the program actually runs. Ultimately, it comes down to this: if the linker finds that a symbol's definition exists in a shared library, it will not include that symbol's definition in the final executable. Instead, the linker records the symbol's name in the executable and which library it should come from.

When the program runs, the operating system arranges for these remaining linking tasks to be completed "just in time" so the program can run. Before the main function runs, a smaller version of the linker (usually called `ld.so`) checks these "IOUs" and immediately completes the final stage of linking—pulling in the library code and connecting everything together. This means no executable has a copy of the `printf` code. If a new, fixed version of `printf` becomes available, you only need to change `libc.so` to plug it in—the next time any program runs, it will be picked up.

There is another major difference in how shared libraries work compared to static libraries, which is reflected in the granularity of linking. If a specific symbol is extracted from a specific shared library (such as `printf` in `libc.so`), the entire shared library is mapped into the program's address space. This is drastically different from the behavior of static libraries, where only the specific object containing the undefined symbol is extracted.

We'll leave shared libraries at that for now. I have a roughly 300-page "Advanced C/C++ Compilation Techniques" book on hand that is dedicated to dynamic library/shared library technology. That's enough to show how complex this topic is. We'll discuss it carefully in later blog posts. For this introduction, we'll stop here.

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

When you use the `int_max(int a, int b)` function in this **`usage.cpp`** C++ file, the C++ compiler (`g++`) won't simply map the function name to `int_max` like a C compiler would. To support features that C doesn't have, like **function overloading**, **namespaces**, and **class member functions**, the C++ compiler performs complex encoding on function names in the source code, a process called **Name Mangling**.

```cpp

int int_max(int a, int b);

```

When the `g++` compiler generates the **`usage.o`** object file, it expects the linker to find a mangled symbol, such as **`_Z7int_maxii`** in a GCC/Linux environment (the exact mangled result varies by compiler and platform, but it is **definitely not** simply `int_max`).

#### C Library Symbol Names

The problem is that the static library **`libutils.a`** was generated by a **C compiler** (usually `gcc` or `cc`) compiling the **`lib.c`** file. The C compiler **does not perform name mangling**. Therefore, in **`libutils.a`**, the symbol name of the `int_max` function is simply **`int_max`** (or with an underscore prefix, like `_int_max`).

You immediately know the problem below:

```cpp

g++ usage.cpp -L. -lutils -o usage

```

1. **`g++`** compiles `usage.cpp`, generating `usage.o`, which contains an **undefined reference** to a **mangled name** (such as `_Z7int_maxii`).
2. The linker (`ld`) starts working; it looks for `int_max` in `usage.o`, but only finds a need for `_Z7int_maxii`.
3. The linker looks for `_Z7int_maxii` in **`libutils.a`**, but the symbol that exists in the library is **`int_max`**.
4. The linker cannot find a matching symbol, so it throws the error: `undefined reference to 'int_max(int, int)'` (Note: the error message shows the C++ style function signature, but what the linker is actually looking for is its mangled version).

#### The Solution: Using `extern "C"`

To solve this problem, you need to tell the C++ compiler: **"Hey, this function was compiled with a C compiler, don't mangle its name!"** You just need to use the **`extern "C"`** linkage specifier around the **function declaration** in your C++ file:

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
