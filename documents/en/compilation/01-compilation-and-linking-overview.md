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
title: "A Deep Dive Into C/C++ Compilation and Linking: Introduction"
description: 'Start from the undefined reference error that makes you jump, and work out the underlying mechanics of compilation and linking — how symbols get produced, how the linker makes its calls, and where exactly static and dynamic libraries differ.'
cpp_standard: [11, 14, 17, 20]
---
# A Deep Dive Into C/C++ Compilation and Linking: Introduction

## Foreword

This is a new series! It is a topic I plan to dig into systematically and in depth this week. Concretely, we are going to talk through and summarize a set of C/C++ topics that most of us gloss right over but that absolutely torture us along the way — compilation and linking. I believe every one of you has run into the headache that is `undefined reference`, and I bet a fair number of you flinch a little the moment you see it (I, for one, was just recently tortured by an `undefined reference` thrown during template instantiation).

When this kind of error shows up, I think most people, at least in the beginning, panic-ask an AI, panic-search the web, but very few actually stop to think — why do we even get errors like `undefined reference` in the first place? Setting aside the cases where we genuinely forgot to hand the source file to the build system (I know many of you have done this; I have too), a lot of the time we really do have it — at least we believe we have it — we did provide the source file, you even watched it link, and yet it just fails.

For example, say you wrote this in a `lib.c` file and turned it into a static library `libutils`.

```c
int int_max(int a, int b) {
 return a > b ? a : b;
}

```

Then, right away, we use `int_max` in a C++ file:

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
 int a = 1, b = 2;
 std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}

```

Then we hammer out that command, expecting our program to compile cleanly, and we get a very strange error —


```cpp

[charliechen@Charliechen linkers]$ g++ usage.cpp -L. -lutils -o usage
/usr/sbin/ld: /tmp/ccdSskJz.o: in function `main':
usage.cpp:(.text+0x88): undefined reference to `int_max(int, int)'
collect2: error: ld returned 1 exit status
[charliechen@Charliechen linkers]$

```

This looks downright bizarre. We clearly linked `libutils` — it even found our `libutils` (no complaint about `/usr/sbin/ld: cannot find -lutils: No such file or directory`, which means it found it), so why the error? And even if the symbol really is missing, why didn't it complain at compile time? Look, if you are the kind of reader who, like the author of [`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html), spots the problem instantly, then this introductory "Deep Dive Into C/C++ Compilation and Linking: Introduction" has nothing new for you. We will get into the real fine details later, not here.

**This post assumes you have at least written some C (the problem above touches C++ but C++ is not the core of this article). If you have hit an `undefined reference` before and had no idea how to fix it, even better.**

## So what do the variables and functions we write actually mean?

This question is not aimed at *you* — this question is aimed at the *computer*. To answer that whole string of questions you might never have thought to ask, we first have to answer one question: "The things we find and fail to find — how does the computer even know about them?" Put more formally: how does the compiler toolchain collect and look up symbols? How does it then turn them into something easier to process? (For instance, we map a function to an address the machine can find, and at that point anyone who knows assembly immediately sees how a function works — once the function name becomes an address, you just `call` that address, and the CPU's instruction pointer jumps there, fetches the instruction, and starts running the code.) At the end of the day, our first step is this: the variables and functions we understand, the ones that carry business meaning — how do they get turned into addresses, into "this is where that thing lives" from the machine's point of view? What happens in the middle? **What do the variables and functions we write actually mean to a computer?**

Any computer science student can rattle off the four classic steps a program goes through from source file to running on the OS — preprocessing, compilation, linking, and **execution**. (Someone is bound to ask: isn't that obvious? Why call out execution separately? Good question! Dynamic loading and load-time linking of dynamic libraries is something we will talk about carefully.)

To answer the question above well, we need to focus on the last three (preprocessing is **a source-code-to-source-code transformation** — for example expanding `#define`s or selecting code via `#if` conditional compilation — and we are not going to discuss it here).

When we write C files — whether it is the Bilibili course UP-zhus, the notes of senior bloggers, or your college professor sleepily reading off his years-old slides — they all tell you the same thing. Writing a C file, we are really only ever doing two things: declaring, and defining. The thing we are talking about is **global variables and functions**, and I have to stress that up front.

- Local variables? Yeah, no point discussing them. Once the program is on the CPU, the OS backend serves them dynamically for your code — maybe a **specific register, maybe a chunk of memory, but they never sit on disk inside the executable!**
- One thing worth calling out specifically — a definition includes a declaration. Not clear? Example: once you have told me what A is, have you not also told me, at the same time, that an A exists here?

A declaration is simple. We are just loudly shouting that something exists here (). You ask me, what is it? What is its value? Sorry, I have no idea, all I can tell you is that this thing definitely exists — where it is, you, compiler, go find it yourself.

A definition is not hard either. We take a declaration (maybe one someone else shouted elsewhere, maybe an inline one like `int a = 2`) and we attach the actual stuff to that declaration. That act is a **definition**. For a global variable, that stuff is data. For a function, it is our executable code. A global variable's definition will make the compiler, when it later produces the executable, allocate concrete space for your variable. And of course, the value you assigned has to come along — otherwise what did you define it for?

We know that the relocatable object file produced after compilation (Locatable Objects) will expose function names and variables. When we write programs, we just take for granted that they can be found (a sharp reader immediately interrupts me — found when, at compile time, or at link/run time? Hold on, getting to it). In serious academic discussion this is called **symbol visibility**. **Visible symbols are accessible!** And this **accessibility of visible symbols** needs to be split into two cases:

- Compile-time accessibility — for example, in a C program, **any symbol not modified by `static`, including global variables and functions**. You have written C, so you obviously know that after writing global `static int a = 1;` and `static int max(int a, int b){return a > b ? a : b;}` in `a.c`, `b.c` cannot reach them at all. Try it yourself.
- Runtime accessibility — here I mean all global variables and functions, whether or not they are decorated with `static`. Because they are all stored in the executable, once on the CPU the OS has to allocate program-lifetime memory storage for every global variable and function whether it is `static` or not. So as far as the CPU is concerned, they are with the program for its whole life. They are still global; it is just that some globals can **only be accessed by specific code** (this is exactly where `static` does its work).

In other words, anything that is an **accessible global variable or function** must live alongside the program for its whole life and be placed into the program's executable, taking up some space (which is exactly why I said only global variables and functions are worth discussing). Everything else is completely unrelated to our question. I wrote a small program here:

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

| Symbol              | Category    | Storage Class                | Linkage               | Typical Segment at Runtime                    | Function                                              |
| ------------------- | ----------- | ---------------------------- | --------------------- | --------------------------------------------- | ----------------------------------------------------- |
| `un_g_initialized_var` | Variable definition | **Global** (`static` duration) | **External** (`External`) | **BSS** (Block Started by Symbol)             | Uninitialized global variable, zero-initialized at runtime. |
| `g_initialized_var`    | Variable definition | **Global** (`static` duration) | **External** (`External`) | **Data** (Initialized Data)                   | Initialized global variable.                          |
| `extern_var`           | Variable declaration | N/A (reference)               | **External** (`External`) | N/A (expected to be defined in another file)  | References a global variable defined in another translation unit. |
| `un_init_local_var`    | Variable definition | **Global** (`static` duration) | **Internal** (`Internal`) | **BSS**                                       | File-scope static variable, uninitialized, zero-initialized at runtime. |
| `init_local_var`       | Variable definition | **Global** (`static` duration) | **Internal** (`Internal`) | **Data**                                      | File-scope static variable, initialized.              |
| `local_func`           | Function definition | **Function**                  | **Internal** (`Internal`) | **Code** (.text)                              | Static function, only callable within the current file. |
| `func`                 | Function definition | **Function**                  | **External** (`External`) | **Code** (.text)                              | Ordinary function, callable from other files.        |
| `extern_func`          | Function declaration | **Function**                  | **External** (`External`) | N/A (expected to be defined in another file)  | References a function defined in another translation unit. |

Have a think about the table above. If anything trips you up, go look it up yourself to make sense of it.

## How the C compiler sees our files

Let's get the C compiler moving. Note that your compile command must be


```cpp

gcc -c demo.c -o demo.o # hey, do not drop the -c, that flag means compile only

```

The compiler quietly chugs along for a bit and hands us the `demo.o` we wanted. So what is the compiler actually doing while it compiles this one C unit?

Whether you are on Apple clang, GNU gcc, or Microsoft's MSVC, they are all **compilers**, and the main job, as you can see, is to turn a C file from human-readable text (mountain of trash code aside) into something the machine can understand. The compiler produces the result as an object file. On UNIX platforms these usually carry a `.o` suffix; on Windows they carry a `.obj` suffix.

Interestingly, our object file — tying back to the topic above — at minimum ends up containing these two parts:

- Machine code: the specific instructions, the 0s and 1s the machine can read.
- Data evolved from global variables: this corresponds to the definitions of global variables in the C file (for initialized globals, the initial value of the variable also has to be stored in the object file).

Now here is the thing. Look carefully at `extern int extern_var;` and `extern int extern_func();`. Anyone familiar with the `extern` keyword immediately flags something wrong — wait, your `extern_var` and `extern_func` have no definition at all, did the compiler not notice?

Here is what I am telling you: it knows. But **C/C++, as a compiled language, lets you get away with only declarations at compile time, no definitions required!** I have to stress this **handy but annoying** trait one more time: **C/C++, as a compiled language, lets you get away with only declarations at compile time, no definitions required!** So when does someone finally decide whether you are intentionally parking the definitions elsewhere, or you just carelessly forgot to write them? The answer is the next stage: linking. We will get to that. For now keep your eyes on the compile stage.

## nm, a handy command

Windows MSVC folks, do not bother. What you should be using is not `nm`, it is `dumpbin` (assuming you actually installed MSVC — what I mean is, you are writing code in Visual Studio). But here, I am going to discuss using `nm` with SystemV output format.

How do we verify, on the executable we just got, the stuff we have been talking about? Simple — we pull out our `nm` tool and analyze it. Come on, let's try:


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

All right, let's look at this table carefully. What you want to focus on is the Class column — it tells us what each entry is.

- The U class marks an undefined reference, one of the "blanks" mentioned earlier. This object has two such entries: "fn_a" and "z_global".
- The t or T class marks the location of a code definition; the case of the letter tells you whether the function is local (t) or non-local (T) — i.e. whether it was originally declared `static`. Likewise, some systems may also show a section, e.g. `.text`.
- The d or D class marks an initialized global variable; again, the case tells you whether the variable is local (d) or non-local (D). If there is a section, it looks something like `.data`.
- For uninitialized global variables, you get b if it is static/local, or B or C if it is not. In this example the section might look like `.bss` or `*COM*`.

Windows friends: you need to open the `x86 Native Tools Command Prompt for VS Insiders`, navigate to your target C file, and type `cl /c <SourceFile>.c`. That tells MSVC to only compile our source file, and the resulting `<SourceFile>.obj` is our relocatable object file. At that point we can use the `dumpbin` utility:


```cpp

dumpbin /symbols <SourceFile>.obj

```

to view the symbols. Let me list out what I got (default toolchain under VS2026):


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

Kicking aside all the other noisy output, what it actually boils down to is this table:

| `dumpbin` output                                     | Meaning                          | Analogous Linux `nm`      |
| ---------------------------------------------------- | -------------------------------- | ------------------------- |
| `SECT4  notype () External \| _func`                 | External function defined in .text | `T _func`                 |
| `SECT3  notype External    \| _g_initialized_var`    | External variable defined in .data | `D _g_initialized_var`    |
| `UNDEF  notype External    \| _extern_func`          | Undefined external function reference | `U _extern_func`          |
| `UNDEF  notype External    \| _extern_var`           | Undefined external variable reference | `U _extern_var`           |
| `UNDEF  notype External    \| _un_g_initialized_var` | Undefined external variable reference | `U _un_g_initialized_var` |

## Resolving the symbols we do not know about: linking

Now let's push the topic one step further. This step is exactly where we resolve the question we left hanging back in "How the C compiler sees our files". Let us assume that, in some other file, those external symbols really are defined:

```c
// demo_extern.c
int extern_var = 10;
int extern_func() {
 return 3;
}

```

These symbols likewise get compiled into a relocatable object file. What is left then is to take this mix — definitions here, undefined symbols there — and combine them, **resolving the indeterminate (name-only, definition-unknown) parts in every file** (our compiler compiled these source files fine, which means we declared these symbols, but we have not yet found their definitions). **That is what linking does.**

Now, after compiling `demo_extern.c` into `demo_extern.o`, we use it to finish the last step of producing our executable:


```cpp

gcc demo_extern.o demo.o -o demo_exe

```

Compilation goes through cleanly, no surprises.


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

Now look — the table got a lot more complicated, but no worries, the bits we care about are:


```cpp

extern_func         |0000000000001119|   T  |              FUNC|000000000000000b|     |.text
extern_var          |0000000000004010|   D  |            OBJECT|0000000000000004|     |.data

```

We have finally found what we were after. They are no longer indeterminate UNDEF entries — they are now properly defined functions and global variables. We can totally try removing the definition of `extern_func`.


```cpp

[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `main':
demo.c:(.text+0x1b): undefined reference to `extern_func'
collect2: error: ld returned 1 exit status

```

There is our old friend! `undefined reference` — it means the linker is complaining that it could not find the definition of `extern_func`. Let's look carefully:


```cpp

[charliechen@Charliechen linkers]$ nm -f sysv demo_extern.o
Symbols from demo_extern.o:

Name                  Value           Class        Type         Size             Line  Section

extern_var          |0000000000000000|   D  |            OBJECT|0000000000000004|     |.data

```

As you can see, `demo_extern` provides the definition of `extern_var`, but the definition of `extern_func` is nowhere to be found, and we only handed the linker those two files. Naturally the linker has no idea where to go look for your `extern_func`, and so it throws this error.

We now understand the linker's key job — resolving the undefined-symbol problem of the minimum executable (why minimum? we will get to that later). Any link where **you failed to provide the concrete content of a definition** (you forgot to write the source code for some function you used) will fail! In the end, after the linker has searched around, as long as there is one undefined symbol left (i.e. any symbol whose Class is U in `nm` or `dumpbin`), the linker will throw an error and list every one of those undefined symbols for you. **At that point the fix is dead simple — find the relocatable file that contains those symbols (in most build systems the source file name and the relocatable file name match, only the suffix differs), and hand it to the linker at link time!** This is the **only** way to fix `undefined reference` in any non-dynamic-library compilation scenario.

Now that we have looked at the `nm` output, we can answer the whole question:

- Q1: How does the compiler toolchain collect and look up symbols? How does it then turn them into something easier to process?
- A: The compiler compiles symbols into machine-readable instructions, and **maps each function symbol to an address**. For global variables, it maps each one to a concrete access location in the data section.
- Q2: **What do the variables and functions we write actually mean to a computer?**
- A: It just associates our addresses with our meaningfully-named variables — what you call them does not matter at all. After the compiler and linker are done with them, by the time they reach the computer, only a string of addresses is left. You ask me what that is — beats me! Go ask `nm`!

## Side topic: what if we define the same thing twice?

The last section said that if the linker cannot find a definition for a symbol to bind its references to, it gives an error. So what happens if, at link time, a symbol has two definitions?

I am not going to give you the answer right away. Try it yourself first. For instance, restore the definition of `extern_func` in `demo_extern`, and at the same time modify our `demo.c` like so:

```c
int un_g_initialized_var;
int g_initialized_var = 1;

extern int extern_var;

static int un_init_local_var;
static int init_local_var = 1;

static int local_func() {
 return 1;
}

int extern_func() { // copy a definition in here, return whatever you like, it does not affect the conclusion
 return 3;
}

int func() {
 return 2;
}

// extern int extern_func(); <- comment out the extern that emphasizes external lookup

int main() {
 return extern_var + extern_func();
}

```

We repeat the same separate-compile-then-link steps. Very quickly we get another error you have probably seen before:


```cpp

[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `extern_func':
demo.c:(.text+0xb): multiple definition of `extern_func'; demo_extern.o:demo_extern.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status

```

Notice — same as before, because the compiler trusts that **the linker can correctly handle any symbol relationship** (it can only compile files one at a time! It cannot see the rest of the source files! **The symbol adjudication for the entire result unit — executable, dynamic library, static library — is decided by the linker!** I have to stress this one more time.)

So at link time the linker finds that two files contain an identical symbol definition. Naturally, the definitions disagree — it is as if you said A is 1 and also said A is 2. Uniqueness is broken, and picking one arbitrarily would just make the program's behavior uncontrollable. So the linker slaps you right back and refuses to let it through. At least under the GNU toolchain's default behavior today, doing this gets you a `multiple definition`.

## And that is all the linker does?

I asked it like that, so obviously that is not all — right? When you see me hammering on this point over and over, do you feel the question forming:

- Why is it that **C/C++, as a compiled language, lets you get away with only declarations at compile time, no definitions required**? Why not force you to know everything right away? What a pain.

Think about it calmly for a second. Say I ask you to drop a letter off at the post office. You obviously would not interrupt me with "shut up buddy, first carry the post office over here so I can see the letter and then I will deliver it for you." Far more likely, you would picture an imaginary post office in your head — "all right, I need to go to a place called the post office to drop off a letter." You would then naturally go look for it somewhere else. It is the exact same idea here. We carve out the unresolved symbols and we manage and promise them ourselves — they will show up where they are supposed to. **That is your responsibility, not the compiler's.** With that, we can keep digging:

- So, besides handing over source code, can we hand over other forms of information?

Ooh, nice catch. If you looked carefully at what I did just now:


```cpp

[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe

```

Did you notice that the linking step has, basically, nothing to do with the source files anymore? After all, we look for undefined symbols in the relocatable files (`*.o`). So could we, ahead of time, prepare a whole bunch of relocatable files plus a set of symbol declaration files, and then when we program we would not have to keep reinventing the wheel — we could just **at programming time use those declaration files to tell the compiler "I promise these symbols exist,"** at compile time **produce our own relocatable files by compiling,** and then **at link time combine those pre-prepared relocatable files with our own relocatable files into an executable?**

Congratulations! You just reinvented the concepts of libraries and interface-based programming! Now you know what header files are for! They are a set of symbol declaration files! And those thousands of relocatable files — instead of leaving them scattered around, let's **bundle them up into a library**, shall we? Of course! And with that you have invented history's **famous static library**. I am a little excited, but I need to lay the concepts out cleanly:

- Header files: i.e. symbol declaration files, **containing the declarations of symbols whose existence we vouch for.**
- Static library: the concrete definitions of those symbols (all of them, or some of them — the unresolved ones might depend on other libraries, fun, right?)

So what I am saying is — the linker can also link libraries. I did not say static library specifically. There are dynamic libraries too. Let's do static first.

## Static libraries: our symbol library

We can use `ar` (on Linux or UNIX systems) or the `LIB` tool to gather all the relocatable files into a static library.

> A quick word on the details:
>
> - On **UNIX** systems, the command used to produce a static library is usually **`ar`**, and the resulting library file usually carries the **`.a`** extension. These library files typically also take **"lib"** as a prefix, and when handed to the linker you use the **`-l`** option followed by the name of the library (without the prefix and the extension). For example, **`-lfred`** would select the **`libfred.a`** file. (Historically, static libraries also needed a program called **`ranlib`** to build a symbol index at the start of the library. These days, **`ar`** usually does this work itself.)
> - On **Windows**, static libraries carry the **`.LIB`** extension and are produced by the **`LIB`** tool. This can get confusing though, because "**import libraries**" use the same extension, and an import library only contains a list of what is available in a given DLL.

For the link stage, when we hand the linker a static library, the linker at that point holds a table of not-yet-resolved symbols, dives into the static library, and pulls those symbols out one by one (for example, symbol A is missing, and it lives in `Obj1.o`, so we pull in all of `Obj1.o`), until we have resolved all the undefined-symbol problems.

Pay attention to the **granularity** of what gets pulled out of the library: if a definition for a particular symbol is needed, the **entire object file** that contains that symbol's definition gets pulled in. This means the process can be "one step forward, one step back" — a newly pulled-in object file might resolve an undefined reference, but it will very likely also bring a whole new set of its own undefined references for the linker to then resolve.

[`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html) has an excellent example, which I will reproduce below for you to read.

Suppose we have the following object files, and the link line contains **`a.o`**, **`b.o`**, **`-lx`**, and **`-ly`**.

| File               | **a.o**    | **b.o** | **libx.a**                             | **liby.a**                   |
| ------------------ | ---------- | ------- | -------------------------------------- | ---------------------------- |
| **Objects**        | a.o        | b.o     | x1.o, x2.o, x3.o                       | y1.o, y2.o, y3.o             |
| **Definitions**    | a1, a2, a3 | b1, b2  | x11, x12, x13; x21, x22, x23; x31, x32 | y11, y12; y21, y22; y31, y32 |
| **Undefined refs** | b2, x12    | a3, y22 | x23, y12; y11; y21                     | x31                          |

1. **Processing `a.o` and `b.o`:**
   - The linker resolves the references to `b2` and `a3`.
   - At this point, the undefined references left are **`x12`** and **`y22`**.
2. **Processing `libx.a`:**
   - The linker checks the first library, `libx.a`, and finds it can pull in **`x1.o`** to satisfy the `x12` reference.
   - However, pulling in `x1.o` also brings new undefined references `x23` and `y12`. (The undefined list is now: `y22`, `x23`, and `y12`.)
   - The linker is still working through `libx.a`, so the `x23` reference is easily satisfied by pulling in **`x2.o`**.
   - But that also adds `y11` to the undefined list. (The undefined list is now: `y22`, `y12`, and `y11`.)
   - No other object file in `libx.a` can resolve these remaining symbols, so the linker moves on to `liby.a`.
3. **Processing `liby.a`:**
   - Same flow — the linker will pull in **`y1.o`** and **`y2.o`**.
   - Pulling in `y1.o` adds a reference to `y21`, but since `y2.o` is being pulled in anyway, that reference is easily resolved.
   - The end result: all undefined references have been resolved, and some (not all) of the object files in the libraries have been included in the final executable.

#### The importance of link order

Note that if (say) `b.o` also had a reference to `y32`, things would go differently.

- The way `libx.a` links would stay the same.
- When processing `liby.a`, the linker would also pull in **`y3.o`** to resolve `y32`.
- Pulling in `y3.o` adds **`x31`** to the unresolved list.
- By that point the linker has already **finished** processing `libx.a`, so it cannot find that symbol's definition (which lives in `x3.o`), and the **link fails**. This example cleanly shows why link order matters (`libx.a` before `liby.a`). In other words, the linker does not backtrack. When you link, you must lay out a clear, layered dependency among your symbols — strictly forward dependencies, no circular ones. Do not make trouble for yourself!

## Dynamic libraries / shared libraries

For now you can simply think of it as a dynamic library. Strictly speaking, the two are slightly different, but in an introduction being that rigorous right out of the gate would just scare people off.

Dynamic libraries exist mostly to fix one obvious flaw of static libraries — every executable carries its own copy of the same code. If every executable contained a copy of functions like `printf` and `fopen`, that would eat a huge amount of disk space for no good reason.

> You can run a fun experiment: statically link the C library and see how big it gets. Look up the exact command yourself; on my machine the result was several hundred MB.

Of course, you might say — I have money, I can just throw SSDs at it. That is not the worst part. The worst part is this: if the provider's code has a bug, you are cooked — all of that code is hard-baked into the executable, and you cannot use that executable at all — not until somebody else waits a few months, finishes recompiling, and hands you a new one!

To solve these painful problems, shared libraries / dynamic libraries showed up (usually denoted with the `.so` extension, `.dll` on Windows, `.dylib` on Mac OS X). At this point the linker takes an "IOU" approach and defers payment of the IOU to the moment the program actually runs. The bottom line: if the linker sees that a symbol's definition lives in a shared library, it will not include that symbol's definition in the final executable. Instead, the linker records, inside the executable, the name of the symbol and which library it is supposed to come from.

When the program runs, the OS arranges for the remaining linking work to be done "just in time" so the program can run. Before `main` runs, a smaller version of the linker (usually called `ld.so`) checks those "IOUs" and immediately finishes the last phase of linking — pulling in the library code and wiring everything together. That means none of the executables has a copy of the `printf` code. If a new, fixed version of `printf` becomes available, you just swap in the new `libc.so` — and the next time any program runs, it gets picked up.

There is one more major way shared libraries differ from static libraries, and it shows up in the granularity of linking. If you pull a particular symbol (say `printf` from `libc.so`) out of a particular shared library, the **entire** shared library gets mapped into the program's address space. This is drastically different from the static library behavior, where only the specific object that contains the undefined symbol gets pulled out.

We will leave shared libraries at that for now. I have on hand a nearly-300-page book, *Advanced C/C++ Compiling Techniques*, that is dedicated entirely to dynamic / shared library technology. That alone tells you how complicated this topic is. We will get into it carefully in later posts. For the introduction, that is enough.

## Other topics: what about C++?

#### C++ name mangling

Back to this `usage.cpp`:

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
 int a = 1, b = 2;
 std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}

```

When you use the `int_max(int a, int b)` function inside the C++ file **`usage.cpp`**, the C++ compiler (`g++`) does not simply map the function name to `int_max` the way a C compiler would. To support features C does not have — **function overloading**, **namespaces**, **class member functions**, and so on — the C++ compiler performs a complex encoding of the function name from the source code. This process is called **name mangling**.


```cpp

int int_max(int a, int b);

```

When the `g++` compiler produces the **`usage.o`** object file, it expects the linker to find a mangled symbol — for example, in a GCC/Linux environment it might look for something like **`_Z7int_maxii`** (the exact mangling varies by compiler and platform, but it is **definitely not** a plain `int_max`).

#### The symbol name in a C library

The catch is that the static library **`libutils.a`** was produced by a **C compiler** (usually `gcc` or `cc`) compiling **`lib.c`**. The C compiler **does not perform name mangling**. So inside **`libutils.a`**, the symbol name for the `int_max` function is simply **`int_max`** (or with an underscore prefix, like `_int_max`).

You can already see the problem coming:


```cpp

g++ usage.cpp -L. -lutils -o usage

```

1. **`g++`** compiles `usage.cpp` and produces `usage.o`, which contains an **undefined reference** to the **mangled name** (e.g. `_Z7int_maxii`).
2. The linker (`ld`) gets to work, looks in `usage.o` for `int_max`, but only finds a need for `_Z7int_maxii`.
3. The linker looks inside **`libutils.a`** for `_Z7int_maxii`, but the symbol in the library is **`int_max`**.
4. The linker cannot find a matching symbol, so it reports the error: `undefined reference to 'int_max(int, int)'` (note: the error message shows the C++-style function signature, but what the linker is actually hunting for is its mangled version).

#### The fix: use `extern "C"`

To fix this you need to tell the C++ compiler: **"Hey, this function was compiled by a C compiler, do not mangle its name!"** All you have to do is wrap the **function declaration** in the C++ file with the **`extern "C"`** linkage specifier:

```cpp
// in usage usage.cpp

#include <iostream>

// Use extern "C" to tell the C++ compiler that this function's symbol name
// should follow C rules — no mangling, look up plain 'int_max' directly.
extern "C" int int_max(int a, int b);

int main() {
    int a = 1, b = 2;
    std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
    return 0; // added the return statement
}

```

Recompile and link, and the program runs successfully, because now the symbol referenced in `usage.o` is the plain `int_max`, which matches what `libutils.a` provides.

## A modern CMake perspective

All this hand-rolled `gcc -c`, `ar rcs`, `-l`/`-L`, `extern "C"`, `-fvisibility` work has, in today's projects, basically been taken over by CMake. You write `add_library(utils STATIC lib.c)` and CMake automatically calls `ar` to pack it into `libutils.a`; `target_link_libraries(myapp PRIVATE utils)` takes over the assembly of `-lutils` and `-L`, and it also works out the correct link order from the dependency topology — that "the linker does not backtrack" rule from earlier, CMake has lined it up for you. Mixing C and C++ is no problem either: set `set_target_properties(utils PROPERTIES POSITION_INDEPENDENT_CODE ON)` on the C target, or just `add_library(utils SHARED ...)` and let CMake turn on `-fPIC` by default, and the C++ side can link against it. Symbol visibility goes to `CXX_VISIBILITY_PRESET hidden` (equivalent to a global `-fvisibility=hidden`), and you only let the interfaces you genuinely want to export out with `__attribute__((visibility("default")))`. The runtime lookup path for dynamic libraries graduates from a hand-written `LD_LIBRARY_PATH` to `CMAKE_INSTALL_RPATH` paired with `$ORIGIN`, so the `.so` travels along with the executable and deployment no longer leans on tweaking environment variables. In other words, not one of the underlying mechanisms this post talks about has gone away — they have just been wrapped by the build system into a single line of declarative config.
