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
title: "Deep Dive into C/C++ Compilation and Linking · Part 1: Introduction"
description: 'Starting from the undefined reference error that never fails to make you jump, this article works out the underlying mechanics of compilation and linking: how symbols come into being, how the linker arbitrates, and where static and dynamic libraries really differ.'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/01-compilation-and-linking-overview.md
  source_hash: 21c5722b678e29c83ea22a0e4148df61f2a25c261d51782e8d6bf91e7615e18f
  translated_at: '2026-09-25T23:50:36+00:00'
  engine: anthropic
  token_count: 9700
---
# Deep Dive into C/C++ Compilation and Linking · Part 1: Introduction

## Preface

This is a brand-new series — a topic I plan to research systematically and in depth this week. Concretely, we will discuss and work through a set of C/C++ topics that most of us breeze right past yet are invariably tortured by: compilation and linking. I believe every one of you has run into headaches like `undefined reference`, and I suspect quite a few of you flinch the moment that error appears on screen (not long ago, I myself was tormented by an `undefined reference` during template instantiation).

When this kind of error strikes, most people's first move, I suspect, is to scramble — asking an AI, searching the web — but few ever stop to think: why do errors like `undefined reference` exist in the first place? Setting aside the cases where we genuinely forgot to feed a source file to the build system (I'm sure many of you have been there; so have I), there are plenty of situations where we really did provide the source file — or at least honestly believed we did — watched it get linked, and the link still failed.

For example, suppose you write the following in a lib.c file and pack it into a static library, libutils.

```c
int int_max(int a, int b) {
 return a > b ? a : b;
}

```

Then we immediately go and use `int_max` in a C++ file:

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
 int a = 1, b = 2;
 std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}

```

Then, we type the command below, fully expecting our program to build, and we get a very strange error:

```cpp

[charliechen@Charliechen linkers]$ g++ usage.cpp -L. -lutils -o usage
/usr/sbin/ld: /tmp/ccdSskJz.o: in function `main':
usage.cpp:(.text+0x88): undefined reference to `int_max(int, int)'
collect2: error: ld returned 1 exit status
[charliechen@Charliechen linkers]$

```

This looks downright bizarre. We did link libutils — the linker even found our libutils (no complaint of `/usr/sbin/ld: cannot find -lutils: No such file or directory`, which means it was found) — so why the error? And if the symbol was missing, why didn't the compiler complain back at compile time? If, as the author of [`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html) puts it, you can spot the problem immediately, then this introductory "Deep Dive into C/C++ Compilation and Linking · Part 1: Introduction" holds nothing new for you; we will only get truly detailed about every little bit later — not here.

**This article assumes you have written at least some C programs (the problem above involves C++, but the core of this article is not C++). If you have run into errors like `undefined reference` and had no idea how to fix them — even better.**

## So, What Do the Variables and Functions We Write Actually Mean

This question is not aimed at **you** — we are asking the **computer**. To answer that chain of questions you may never have thought about, we must first answer one thing: "How does the computer know about the things we find and fail to find?" More formally: how does the compiler toolchain collect and look up symbols, and how does it convert them into something easier to process? (For example, we map functions to addresses the computer can locate — readers who know assembly will instantly see how functions work from there: once the function name becomes an address, you simply call that address, and the computer's processing flow jumps to it, fetches instructions, and starts executing the code.) All told, our first step is this: how do the variables and functions we understand — the ones carrying business meaning — become the addresses that tell the machine what lives where? What does the processing in between look like? **What do the variables and functions we write actually mean to a computer?**

Any computer-science student can rattle off, without hesitation, the four classic stages between a source file and a program running on an operating system: preprocessing, compilation, linking, and **execution**. (Someone will ask: isn't that last one trivial? Why single out execution? Good question! Dynamic loading and load-at-startup of dynamic libraries is something we will talk about properly.)

To answer the questions above well, we focus on the last three (preprocessing is a **source-code-to-source-code transformation** — think `#define` expansion, or selecting what to compile with `#if` — and we won't discuss it here).

When we write C files — whether following the instructors uploading courses on Bilibili, notes from some guru's blog, or your university professor drowsily reading off his years-old slides — the message is always the same: writing a C file comes down to two things, declarations and definitions. And the objects under discussion are **global variables and functions** — I must stress that here.

- What about local variables? Ah, there is no point discussing those: they exist only once the program is on the CPU, serviced dynamically by the operating system's backend for your code — they may be **assigned to a specific register, or allocated in memory, but they absolutely do not sit in the executable file on disk!**
- Especially worth noting: a definition includes the declaration. Not following? An example: if I've already told you what A is, haven't I simultaneously told you that an A exists right here?

A declaration is simple: we just loudly proclaim that something exists here. You ask, what is it? What's its value? Sorry, no idea — all I can tell you is that it does exist; where exactly, compiler, you go find out yourself.

A definition is not hard either: we take a declaration (either one loudly made elsewhere as above, or one made on the spot, like `int a = 2`) and connect it to the substance behind that declaration. That act is the **definition**. For a global variable, that substance is data; for a function, it is our executable code. Defining a global variable makes the compiler allocate concrete space for it in the executable it later generates — plus, of course, the value you assigned it; otherwise, what would you be defining it for, right?

We know that the relocatable files produced after compilation (relocatable objects) expose function names and variables. As we write our programs, we subconsciously assume they can be found (sharp readers will interrupt me at once — found when? During compilation, or during linking and runtime? Patience, we're almost there) — in serious academic discussion this is called **symbol visibility**. **Visible symbols are accessible!** And this **accessibility of visible symbols** needs to be discussed in two parts:

- Accessibility during compilation — for example, symbols in a C program **not decorated with `static`, global variables and functions included**. Having written C, you obviously know that after you write the file-scope `static int a = 1;` and `static int max(int a, int b){return a > b ? a : b;}` in `a.c`, `b.c` cannot reach them at all! Try it yourself.
- Accessibility at runtime — here we mean global variables and functions as a whole, with or without `static`. They all live in the executable file, and once the program is on the CPU, the operating system must allocate memory storage lasting the whole lifetime of the program for every global variable and function, `static` or not. So in practice, as far as the CPU is concerned, they accompany the program for life. They are therefore still global — it's just that some globals must be **accessible only from specific code** (and that is exactly where `static` does its work)

In other words, anything that is an **accessible global variable or function** necessarily accompanies the program for its whole life, must be placed into the program's executable file, and takes up some space there (which is why I said discussing only global variables and functions makes sense). Everything else is entirely beside the point. I wrote a program for this:

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

| Symbol | Category | Storage Class | Linkage | Runtime Memory Region (Typical Segment) | Function |
| ------ | -------- | ------------- | ------- | --------------------------------------- | -------- |
| `un_g_initialized_var` | Variable definition | **Global** (`static` duration) | **External** | **BSS** (Block Started by Symbol) | Uninitialized global variable, zero-initialized at runtime. |
| `g_initialized_var` | Variable definition | **Global** (`static` duration) | **External** | **Data** (initialized data) | Initialized global variable. |
| `extern_var` | Variable declaration | N/A (reference) | **External** | N/A (expected to be defined in another file) | References a global variable defined in another translation unit. |
| `un_init_local_var` | Variable definition | **Global** (`static` duration) | **Internal** | **BSS** | File-scope static variable, uninitialized, zero-initialized at runtime. |
| `init_local_var` | Variable definition | **Global** (`static` duration) | **Internal** | **Data** | File-scope static variable, initialized. |
| `local_func` | Function definition | **Function** | **Internal** | **Code** (.text) | Static function, callable only within the current file. |
| `func` | Function definition | **Function** | **External** | **Code** (.text) | Ordinary function, callable from other files. |
| `extern_func` | Function declaration | **Function** | **External** | N/A (expected to be defined in another file) | References a function defined in another translation unit. |

Think the table above over; if anything in it puzzles you, search the terms yourself to make sense of it.

## How the C Compiler Sees Our Files

Let's get the C compiler moving. Note that your compile command must be:

```cpp

gcc -c demo.c -o demo.o # careful not to drop the -c; it means "compile only"

```

The compiler quietly chews for a moment and hands us the demo.o we wanted. So what is the compiler doing while it compiles a whole unit of C source?

Whether you're using Apple clang, GNU gcc, or Microsoft's MSVC, they are all **compilers**, and their main job, as you can see, is converting C files from text humans can understand (mountains of legacy code excepted) into something a computer can understand. The compiler's output is an object file: on UNIX platforms these usually carry the .o suffix; on Windows, .obj.

Interestingly — tying back to our theme above — our object files end up containing at least the following two parts:

- Machine code: specific instructions built from the 0s and 1s a computer can read.
- Data evolved from global variables: this corresponds to the definitions of the global variables in the C file (for initialized global variables, the variable's initial value must also be stored in the object file).

Right, so here's the question: look carefully at `extern int extern_var;` and `extern int extern_func();`. Anyone familiar with the `extern` keyword will immediately flag something wrong — hmm? Your `extern_var` and `extern_func` have no definitions at all. Did the compiler not notice?

What I'm telling you is: it knows, but **C/C++, being compiled languages, allow declarations to appear at compile time without their definitions!** I must stress this **handy yet troublesome** property one more time: **C/C++, being compiled languages, allow declarations to appear at compile time without their definitions!** So when does the ruling happen — was it that you deliberately placed those definitions somewhere else, or that you carelessly left them out? The answer is the next stage: linking. We'll discuss that later; for now, keep your eyes on the compilation stage.

## nm, a Wonderfully Handy Tool

Windows MSVC users, don't fight it: the tool you should be using is not nm but dumpbin (if what you installed is MSVC — my other point being that you write code with Visual Studio). Here, though, I'm going to discuss things with nm in its System V output format.

How do we verify what we discussed above on the executable file we just produced? Simple — pull out our nm tool and analyze it. Come on, try:

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

Alright, let's pore over this table. What you need to do is watch the Class column — it tells you what each entry in our table is.

- Class U means an undefined reference, one of the "blanks" mentioned earlier. This object has two U-class symbols: `extern_func` and `extern_var`.
- Class t or T marks where code is defined; the particular class tells you whether the function is local (t) or non-local (T) — that is, whether it was originally declared `static`. Some systems may also show a section, such as .text.
- Class d or D marks an initialized global variable; likewise, the particular class says whether the variable is local (d) or non-local (D). If a section is shown, it will be something like .data.
- For uninitialized global variables you get b if it is static/local, and B, or C, if not. In this case the section may look like .bss or *COM*.

Windows folks, you need to open the `x86 Native Tools Command Prompt for VS Insiders`, navigate to your target C file, and type `cl /c <SourceFile>.c`. MSVC will then compile only our source file, and the resulting `<SourceFile>.obj` is our relocatable object file. At that point, we can use the little dumpbin tool:

```cpp

dumpbin /symbols <SourceFile>.obj

```

to look at the symbols. Let me list what I got (the default toolchain under VS2026):

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

Kick away all the noisy output, and what it actually amounts to is this table:

| `dumpbin` output | Meaning | Linux `nm` equivalent |
| ---------------- | ------- | --------------------- |
| `SECT4  notype () External \| _func` | An external function defined in .text | `T _func` |
| `SECT3  notype External    \| _g_initialized_var` | An external variable defined in .data | `D _g_initialized_var` |
| `UNDEF  notype External    \| _extern_func` | Undefined external function reference | `U _extern_func` |
| `UNDEF  notype External    \| _extern_var` | Undefined external variable reference | `U _extern_var` |
| `UNDEF  notype External    \| _un_g_initialized_var` | Undefined external variable reference | `U _un_g_initialized_var` |

## Resolving the Symbols We Don't Know: Linking

Now we push the topic one step further. This step resolves the question the section "How the C Compiler Sees Our Files" left behind. Let's assume those external symbols really are defined in another file:

```c
// demo_extern.c
int extern_var = 10;
int extern_func() {
 return 3;
}

```

These symbols likewise get compiled into relocatable object files. What remains is to take this mixture of defined and undefined symbols, combine it all, and **resolve, for every file, the parts whose symbols are indeterminate (names only) and whose definitions are unknown** (the fact that our compiler accepted these source files means we declared these symbols but have not yet found their definitions). **That is what we do at link time.**

Now, after compiling demo_extern.c into demo_extern.o, we use it to complete the final step toward our executable:

```cpp

gcc demo_extern.o demo.o -o demo_exe

```

Of course the build goes through cleanly. No doubt about it.

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

Now look: the table has become far more complicated, but that's fine — what we care about here is:

```cpp

extern_func         |0000000000001119|   T  |              FUNC|000000000000000b|     |.text
extern_var          |0000000000004010|   D  |            OBJECT|0000000000000004|     |.data

```

We have finally found what we were looking for: they are no longer indeterminate UNDEFs, but a function and a global variable with solid definitions. We can absolutely try removing the definition of extern_func.

```cpp

[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `main':
demo.c:(.text+0x1b): undefined reference to `extern_func'
collect2: error: ld returned 1 exit status

```

There's the error we know so well! `undefined reference` — the linker complaining that it cannot find the definition of `extern_func`. Let's look closely:

```cpp

[charliechen@Charliechen linkers]$ nm -f sysv demo_extern.o
Symbols from demo_extern.o:

Name                  Value           Class        Type         Size             Line  Section

extern_var          |0000000000000000|   D  |            OBJECT|0000000000000004|     |.data

```

As you can see, demo_extern settles the definition of extern_var, but the definition of `extern_func` is nowhere to be found. Since we handed over only these two files, the linker naturally has no idea where to go find your `extern_func` — and so, naturally, it blows up with this error.

We now know the linker's essential function: resolving undefined symbols for the minimal executable (why "minimal"? We'll keep discussing that later). Any link where **you have not provided the information specifying the actual content of a definition** (source code for a used function that went missing) will fail! In the end, once the linker has made its rounds, as long as undefined symbols remain (that is, symbols whose Class is U in nm or dumpbin), the linker raises an error telling you every one of the undefined symbols. **At that point your fix is dead simple — find the relocatable files that define those symbols (in typical build systems the relocatable file has the same name as the source file, differing only in the extension), and supply them at link time**! In every dynamic-library-free compilation scenario, this is the **only way** to resolve `undefined reference`.

Now that we've seen nm's output, we can answer the whole question:

- Q1: How does the compiler toolchain collect and find symbols, and how does it convert them into something easier to process?
- A: The answer is that the compiler compiles symbols into instructions the computer can read, **mapping each function symbol to an address**. For global variables, it maps each one to a specific access location in the data section.
- Q2: **What do the variables and functions we write actually mean to a computer?**
- A: Just addresses tied to the variables we gave meaning to — the names you choose simply don't matter. After the compiler and the linker are done, all that reaches the computer is a string of addresses. You ask me what that one is? Beats me — ask nm!

## A Side Topic: What If We Define Something Twice

The previous section mentioned that the linker issues an error message when it cannot find a symbol's definition to connect with the references to that symbol. So what happens when a symbol has two definitions at link time?

I won't rush to give the answer — try it yourself first. For example, restore the definition of `extern_func` in demo_extern and, at the same time, modify our `demo.c` like this:

```c
int un_g_initialized_var;
int g_initialized_var = 1;

extern int extern_var;

static int un_init_local_var;
static int init_local_var = 1;

static int local_func() {
 return 1;
}

int extern_func() { // copy a definition in here; the return value is up to you, it won't affect our conclusion
 return 3;
}

int func() {
 return 2;
}

// extern int extern_func(); <- comment out extern, the keyword emphasizing external lookup

int main() {
 return extern_var + extern_func();
}

```

We repeat the separate compile-and-link steps from above. Very quickly, we get another error you have probably seen before:

```cpp

[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `extern_func':
demo.c:(.text+0xb): multiple definition of `extern_func'; demo_extern.o:demo_extern.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status

```

You noticed it — same as before, because the compiler believes **the linker can correctly handle the relationships of any symbols** (it can only compile files one piece at a time! It has no control over the other source files! **The symbol arbitration of the entire resulting unit — executable, dynamic library, or static library — is decided by the linker**! That is something I must stress once more!)

So, at link time, the linker finds that two files contain the very same symbol definition. Naturally, two differing definitions are impossible to keep — it's like asserting that A is 1 while also asserting that A is 2; uniqueness is broken, and deciding rashly would only make the program uncontrollable. So the linker slaps it straight back: not approved! At least under the default behavior of today's GNU toolchain, doing this only earns you a `multiple definition`.

## Is That Really All the Linker Does

With a lead like that, how could it be — right? Watching me stress that sentence over and over, did anything occur to you:

- Why is it that **C/C++, being compiled languages, allow declarations to appear at compile time without their definitions**! Why not demand the answer right away? What a hassle.

Think about it calmly, with an example. I ask you to deliver a letter at the post office. You certainly wouldn't interrupt me with "Shut up, buddy — first carry the post office over here so I can see the mail, then I'll deliver it for you." Rather, you would draw an imagined post office in your head: "Right, I need to go to a place called the post office and get a letter delivered." You would naturally go looking for it elsewhere. It is exactly the same reasoning. We leave the pending symbols hanging, managing them ourselves and promising that they will appear in the right places — **that responsibility is yours, not the compiler's**. Very well, then we can continue our questioning:

- So, besides providing source code, could we also provide information in some other form?

Hey! Sharp observation. If you looked closely at this sequence of mine:

```cpp

[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe

```

Have you noticed that our linking step seems to have nothing to do with source files at all? After all, we search for undefined symbols in relocatable files (*.o). So could we prepare, well in advance, a whole set of relocatable files plus a set of symbol declaration files, and then stop reinventing the wheel when we program — directly **using those declaration files while coding to tell the compiler "I vouch that these symbols exist"**, **compiling to produce our own relocatable files**, and then **combining those long-prepared relocatable files with our own at link time to form an executable**?

Congratulations! You have just reinvented the concepts of libraries and interface programming! Now you know what headers are for! They are exactly that set of symbol declaration files! And those thousands upon thousands of relocatable files — instead of leaving them scattered about, shall we **gather them up into a library**? Of course we can! What you have just invented is the historically **famous static library**. Slightly excited here, but I need to tidy up the concepts we have put forward:

- Headers: the symbol declaration files, **holding the declarations for symbols whose existence we vouch for**
- Static libraries: the actual definitions of these symbols (all of them, or only some — the symbols left unresolved may depend on other libraries, fun, right?)

So here is my point — the linker can also link libraries. And no, I didn't say static libraries only; there are dynamic ones too. Let's do static ones first.

## Static Libraries: Our Symbol Library

We can use ar (on Linux or UNIX systems) or the LIB tool to gather all our relocatable files into a static library.

> Some details, quickly:
>
> - On **UNIX** systems, the command used to produce a static library is usually **`ar`**, and the library files it produces usually carry the **`.a`** extension. These library files usually also take **"lib"** as a prefix and are passed to the linker with the **`"-l"`** option followed by the library's name (without the prefix or extension). For example, **`"-lfred"`** selects the file **`libfred.a`**. (Historically, static libraries also needed a program called **`ranlib`** to build a symbol index at the head of the library. These days, the **`ar`** tool usually does that job itself.)
> - On **Windows** systems, static libraries have the **`.LIB`** extension and are produced by the **`LIB`** tool. But this can be confusing, because an "**import library**" also uses the same extension — an import library contains only a list of what is available inside some DLL

For the linking stage: when we hand the linker a static library, the linker holds a table of not-yet-adjudicated symbols, immerses itself in the static library, and picks those symbols out one by one (for example, symbol A is missing and it lives in Obj1.o — we then link all of Obj1.o in), until we have settled every undefined-symbol problem.

Pay attention to the **granularity** of what gets extracted from the library: if the definition of one particular symbol is needed, the **entire object file** containing that definition gets included. This means the process can be "one step forward, one step back" — a newly added object file may resolve an undefined reference, but it may well also drag in a whole new set of undefined references of its own, left for the linker to resolve.

[`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html) has an excellent example; I'll place it below for you to read:

Suppose we have the following object files, and the link line contains **`a.o`**, **`b.o`**, **`-lx`**, and **`-ly`**.

| File | **a.o** | **b.o** | **libx.a** | **liby.a** |
| ---- | ------- | ------- | ---------- | ---------- |
| **Objects** | a.o | b.o | x1.o, x2.o, x3.o | y1.o, y2.o, y3.o |
| **Definitions** | a1, a2, a3 | b1, b2 | x11, x12, x13; x21, x22, x23; x31, x32 | y11, y12; y21, y22; y31, y32 |
| **Undefined references** | b2, x12 | a3, y22 | x23, y12; y11; y21 | x31 |

1. **Processing `a.o` and `b.o`:**
   - The linker resolves the references to `b2` and `a3`.
   - At this point, the undefined references remaining are **`x12`** and **`y22`**.
2. **Processing `libx.a`:**
   - The linker examines the first library, `libx.a`, and finds it can pull in **`x1.o`** to satisfy the `x12` reference.
   - Pulling in `x1.o`, however, also brings new undefined references `x23` and `y12`. (The undefined list is now `y22`, `x23`, and `y12`.)
   - The linker is still working on `libx.a`, so the `x23` reference is easily satisfied by pulling in **`x2.o`**.
   - But that also adds `y11` to the undefined list. (The undefined list is now `y22`, `y12`, and `y11`.)
   - No other object file in `libx.a` can resolve these remaining symbols, so the linker moves on to `liby.a`.
3. **Processing `liby.a`:**
   - By a similar process, the linker pulls in **`y1.o`** and **`y2.o`**.
   - Pulling in `y1.o` adds a reference to `y21`, but since `y2.o` was going to be pulled in anyway, that reference is easily resolved.
   - The end result: all undefined references are resolved, and some — not all — of the object files in the libraries are included in the final executable.

#### The Importance of Link Order

Note how the situation would differ if (for example) `b.o` also had a reference to `y32`.

- The linking of `libx.a` would work exactly as before.
- While processing `liby.a`, the linker would also pull in **`y3.o`** to resolve `y32`.
- Pulling in `y3.o` would add **`x31`** to the unresolved-symbol list.
- At this point the linker has already **finished** processing `libx.a`, so it cannot find the definition of that symbol (which lives in `x3.o`), and the **link fails**. This example shows clearly how much link order (`libx.a` before `liby.a`) matters. In other words, the linker does not go back on its tracks; when you link, you must clearly arrange things so that the dependencies your symbols live in form strictly layered, progressively deeper dependencies rather than circular ones — don't make trouble for yourself!

## Dynamic Libraries / Shared Libraries

For now, just read them as "dynamic libraries"; to be perfectly rigorous, the two terms differ slightly, but in an introduction, piling on that much strictness at once would only scare people away.

Dynamic libraries exist mostly to fix an obvious defect of static libraries — every executable owning a copy of the same code. If every executable contained copies of functions like printf and fopen, that would occupy heaps of unnecessary disk space.

> You can run a fun experiment: statically link the C library and see how big it gets. Please look up the exact command yourself — my result was several hundred MB.

Of course, you say — I've got money, SSDs are trivial to add, so that's not the worst of it. The worst part is: if the provider's code has a bug, you are done for — all of that code is hard-baked into the executable, and you simply cannot use this executable — until someone else has spent months building a fixed one for you!

To solve these troublesome problems, shared libraries / dynamic libraries appeared (usually indicated by the .so extension; .dll on Windows machines, .dylib on Mac OS X). At this point, the linker takes an "IOU" approach and defers payment of those IOUs to the moment the program actually runs. Fundamentally: if the linker finds that a symbol's definition lives in a shared library, it will not include that symbol's definition in the final executable. Instead, the linker records in the executable the symbol's name and which library it should come from.

When the program runs, the operating system arranges for this remaining linking to be finished "just in time" for the program to run. Before the main function runs, a smaller version of the linker (usually called ld.so) inspects those "IOUs" and immediately completes the final stage of linking — pulling in library code and connecting all the code together. This means no executable has a copy of the printf code. If a new, fixed version of printf is available, changing libc.so is all it takes to plug it in — the next time any program runs, it will be picked up.

Shared libraries also behave in one other major way differently from static libraries, and it shows in the granularity of linking. If a specific symbol is taken from a specific shared library (printf from libc.so, say), the entire shared library gets mapped into the program's address space. This is drastically different from the behavior of static libraries, where only the specific objects containing the undefined symbols are extracted.

That's all we'll say about shared libraries for now. I have on hand a nearly-300-page book, *Advanced C/C++ Compiling*, devoted entirely to dynamic/shared library technology — enough to show how complicated the topic is. We'll talk it over carefully in later articles. For the introduction, we stop here.

## One More Topic: What About C++

#### Name Mangling in C++

Back to this usage.cpp:

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
 int a = 1, b = 2;
 std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}

```

When you use the `int_max(int a, int b)` function in the C++ file **`usage.cpp`**, the C++ compiler (`g++`) will not simply map the function name to `int_max` the way a C compiler does. To support features C lacks — **function overloading**, **namespaces**, **class member functions**, and so on — the C++ compiler performs elaborate encoding of the function names in the source code, a process called **name mangling**.

```cpp

int int_max(int a, int b);

```

When the `g++` compiler generates the object file **`usage.o`**, it expects the linker to find a mangled symbol — for example, under GCC/Linux it may look for a symbol like **`_Z7int_maxii`** (the exact mangling varies by compiler and platform, but it is **definitely not** the plain `int_max`).

#### Symbol Names in C Libraries

The problem is that the static library **`libutils.a`** was produced by the **C compiler** (usually `gcc` or `cc`) compiling the **`lib.c`** file. The C compiler **does not perform name mangling**. So in **`libutils.a`**, the symbol name of the `int_max` function is simply **`int_max`** (or with an underscore prefix, like `_int_max`).

You can already see what the problem below will be:

```cpp

g++ usage.cpp -L. -lutils -o usage

```

1. **`g++`** compiles `usage.cpp`, producing `usage.o`, which contains an **undefined reference** to the **mangled name** (for example `_Z7int_maxii`).
2. The linker (`ld`) gets to work, searching `usage.o` for `int_max`, but finds only the need for `_Z7int_maxii`.
3. The linker searches **`libutils.a`** for `_Z7int_maxii`, but the symbol that exists in the library is **`int_max`**.
4. The linker cannot find a matching symbol, so it reports the error: `undefined reference to 'int_max(int, int)'` (note: the error message shows the C++-style function signature, but what the linker actually searched for is its mangled version).

#### The Fix: Using `extern "C"`

To solve this problem, you need to tell the C++ compiler: **"Hey, this function was compiled by a C compiler — don't mangle its name!"** You only need to apply the **`extern "C"`** linkage specifier around the **function declaration** in your C++ file:

```cpp
// in usage usage.cpp

#include <iostream>

// Use extern "C" to tell the C++ compiler to treat this function's symbol name the C way
// i.e., no name mangling — look up 'int_max' directly
extern "C" int int_max(int a, int b);

int main() {
    int a = 1, b = 2;
    std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
    return 0; // add the return statement
}

```

Recompile and link, and the program will run successfully, because the symbol referenced in `usage.o` is now the plain `int_max`, matching the symbol provided in `libutils.a`.

## The Modern CMake Perspective

All this handiwork — `gcc -c`, `ar rcs`, `-l`/`-L`, `extern "C"`, `-fvisibility` — has in today's projects largely been taken over by CMake. You write `add_library(utils STATIC lib.c)`, and CMake automatically invokes `ar` to pack `libutils.a`; `target_link_libraries(myapp PRIVATE utils)` takes over assembling `-lutils` and `-L`, and works out the correct link order from the dependency topology — the "linker never backtracks" iron law from earlier, CMake has it sorted for you. Mixing C and C++ is no problem either: set `set_target_properties(utils PROPERTIES POSITION_INDEPENDENT_CODE ON)` on the C target, or simply use `add_library(utils SHARED ...)` and let CMake turn on `-fPIC` by default, and the C++ side can link against it. Symbol visibility goes to `CXX_VISIBILITY_PRESET hidden` (equivalent to a global `-fvisibility=hidden`), with only the interfaces you genuinely want to export exposed via `__attribute__((visibility("default")))`. The runtime search path for dynamic libraries upgrades from a hand-written `LD_LIBRARY_PATH` to `CMAKE_INSTALL_RPATH` combined with `$ORIGIN`, so the `.so` travels with the executable and deployment no longer relies on tweaking environment variables. In other words, none of the underlying mechanics in this article has disappeared — the build system has simply wrapped them into a line of declarative configuration.
