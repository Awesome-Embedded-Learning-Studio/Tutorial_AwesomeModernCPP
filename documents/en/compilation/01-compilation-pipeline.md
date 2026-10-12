---
chapter: 13
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 17
tags:
- cpp-modern
- host
- intermediate
title: "Compilation and Linking: Four Stages from .cpp to Executable"
description: "Run each of g++'s four stages yourself with -E, -S, -c, and -o, get a good look at what each stage's artifact looks like, and get acquainted with cc1plus, as, and collect2, the programs that actually do the work."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/01-compilation-pipeline.md
  source_hash: 0c39a0c3b0a49fea7b1ec27b1b6b5bb5c9e6bd8d9ace6a03e9c51cc43c022100
  translated_at: '2026-09-30T05:48:45+00:00'
  engine: glm
  token_count: 4031
---

# Compilation and Linking: Four Stages from .cpp to Executable

Just about every day we type a command like this one, and those of us who don't are off clicking a Build button in an IDE instead:

```bash
g++ hello.cpp -o hello
```

Enter goes down, a few seconds pass, and a runnable `hello` is lying in the directory. So what did the machine actually do during those seconds? Most tutorials turn the page right here, as if being able to run were already enough, and you flip to the next chapter along with them. But you have most likely seen `undefined reference`, seen duplicate-definition errors, or spent a long while staring dumbly at a whole string of linker errors. Everything that gives you those headaches happens inside those few seconds, and those seconds happen to be the least-explained part of C++ engineering.

This volume is my attempt to account for what goes on in those seconds. In this first piece we do a plain thing: the command you type has four stages hiding behind it, so we will run every stage with our own hands and pull the intermediate artifacts out one at a time to look at them. And once that's done there's something that may surprise you: the g++ you type every day does not compile a single line of code itself. Someone else does the real work, and later on we will meet them with our own eyes.

## Our test subject: six lines of code

The whole series starts from this:

```cpp
// hello.cpp
#include <iostream>

int main() {
    std::cout << "hello, linker!\n";
    return 0;
}
```

Six lines in all, and it can't be slimmed down any further. What gets typed into the terminal is g++, but today you will see that the names actually taking the stage belong to three other programs.

I work inside WSL2 (the Linux subsystem within Windows), using the GCC 16.2.1 toolchain together with binutils 2.47 and glibc 2.44. binutils is the GNU family's binary toolset; the assembler and the linker we are about to meet both come out of it. glibc is the GNU C library, the runtime library for C/C++ programs on Linux. Every output that shows up in this piece is something I really ran. If your versions differ, details like line counts and paths will drift a bit, but the four stages themselves haven't changed in decades.

## Preprocessing: source in, source out

g++ also hands us a few switches, and with them we can stop g++ partway down the road and keep the intermediate artifacts. The first one we reach for is `-E`: once preprocessing is done, it stops.

```bash
g++ -E hello.cpp -o hello.i
```

This time the artifact is `hello.i`. What does it actually look like? Let's have `wc` count the lines:

```text
$ wc -l hello.cpp hello.i
 44971 hello.i
     6 hello.cpp
```

Six lines went in; forty-four thousand nine hundred seventy-one came out. The first time I saw that number, I sat there scrolling the terminal back for ages, and everything that scrolled past was header files.

Where does all the extra come from? From the one line we wrote, `#include <iostream>`. When the preprocessor meets `#include`, it copies the pointed-to file in verbatim. iostream itself includes other headers, so the copying goes on and on and ends up at forty-odd thousand lines. Apart from `#include`, our file contains no other preprocessor directives. If we had written macros, this is the step where they would get expanded too. Comments have it the easiest: they simply get dropped.

So what kind of file is `hello.i`? Let's give the `file` command the floor:

```text
hello.i:   C++ source, ASCII text
```

You see: it is still C++ source code, genuine ASCII text. Preprocessing is a source-to-source text transformation; nothing machine-specific has been produced. You can open `hello.i` right now, jump straight to the bottom, and find our `main` lying there untouched, with forty-odd thousand lines of expanded headers in front of it.

> How headers keep themselves from being copied twice, what else macro expansion gets up to, and what conditional compilation is for — all of that belongs to piece 02. And those forty-odd thousand lines in hello.i? We will come back and deal with them then.

## Compilation: from source to assembly

Now it is the second switch's turn, `-S`: we have g++ stop after generating the assembly.

```bash
g++ -S hello.cpp -o hello.s
```

```text
hello.s:   assembler source, ASCII text
```

What comes out is still text, but it is already another language. The beginning of `hello.s` looks like this; let's work our way down, line by line:

```asm
	.file	"hello.cpp"
	.text
	.globl	main
	.type	main, @function
main:
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	.LC0(%rip), %rdx
	leaq	_ZSt4cout(%rip), %rax
	call	_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc@PLT
```

Let's start from `main`. Its name is still main; only its identity has changed: it is now a label in the assembly. The two instructions right under the label, `pushq` and `movq`, are the same old routine for opening a function, the pair responsible for setting up this frame's stack.

Next, look at the string literal we wrote, `"hello, linker!\n"` — it changed shape too: the compiler took it away, assigned it an internal number `.LC0`, and from then on, whenever the code below wants it, it refers to it by address.

That `_ZSt4cout` string that looks like garbage is really `std::cout`. C++ has namespaces, it has overloading, far too many things share a name, so the compiler flat-out encodes all of that information into the symbol name to guarantee no collisions. The move is called name mangling. There is no rush to learn how to read it; hand it to `c++filt` and the original gets spat right back out. Today we only need to recognize one representative: once cout enters the world of assembly, its name becomes `_ZSt4cout`.

The one most worth a look is the call at the end. Let me translate the function it calls for you: it is the `operator<<` we have been using, the overload that sends a string into cout. Two places there deserve a stop.

Look: the implementation of `operator<<` is nowhere in our six lines of code. We only called it; its implementation lives somewhere else. Where exactly? I will keep that to myself for now — in the later part where g++ shows its true face, you will see it with your own eyes.

The other place is at the tail of the called symbol, where an `@PLT` suffix hangs. The suffix has to do with how calls into dynamic libraries work, and alongside the PLT there is also a GOT: the PLT is the trampoline mechanism that function calls go through, and the GOT is the table it consults for addresses. It is completely fine if none of this makes sense to you yet; piece 09 (position-independent code and GOT/PLT) was prepared precisely for them.

> As for the compiler's interior — every layer from parsing through optimization to instruction selection — each one is worth a whole piece of its own, and that is exactly what piece 03 does.

## Assembly: from text to binary

The third switch, `-c`, is next: we have g++ stop after producing the object file.

```bash
g++ -c hello.cpp -o hello.o
```

```text
hello.o:   ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
```

At this step the artifact is no longer meant for human eyes. `file` says it is an ELF 64-bit relocatable file. ELF stands for Executable and Linkable Format; on Linux, both object files and executables use this container, and we will be dealing with it over and over in pieces 04 and 16. relocatable means, well, relocatable: the addresses inside the file are still provisional, and it is the linking stage that decides where they end up. The assembler's work is entirely mechanical: it takes our `hello.s`, translates it into machine code one instruction at a time, and packs the result up in ELF format. That is where the `.o` files we are forever talking about come from.

There is one more word in the output worth being happy about: not stripped. The symbol information is all still there. For us that is tremendous news — later, when we dissect object files with tools like `nm` and `objdump`, we will be counting entirely on the symbol table it left behind. It plays the lead in piece 04 (Object File Anatomy: What's Inside a .o), where we will bring the tools along and take this hello.o apart from the inside out.

## Linking: from object file to executable

At the linking step we take the previous stage's `hello.o` as the raw material:

```bash
g++ hello.o -o hello_app
```

```text
hello_app: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2
```

Hold this output next to the `hello.o` line and compare: you can pick out quite a few differences. The format is still ELF, but its identity has gone from relocatable to pie executable. pie means position-independent — the finer points there are a question piece 09 will answer for us. Either way, this is now a real, runnable program. The dynamically linked that follows says it will depend on dynamic libraries while running. And at the end a new face has shown up, named interpreter, with a long path hanging off it.

interpreter's identity we can reveal right now: it is the dynamic linker. Our program uses dynamic libraries such as libstdc++, and when the operating system loads it and gets it running, interpreter is the one that has to track down each library it needs and wire it up. Between loading the program and the first line of main there is a whole further pipeline, and that is the content of piece 14 (program startup).

For how much stuff linking actually packed into the program, we have a more direct way to see it: the `size` command.

```text
$ size hello.o hello_app
   text	   data	    bss	    dec	    hex	filename
    156	      0	      0	    156	     9c	hello.o
   1708	    632	    280	   2620	    a3c	hello_app
```

A quick vocabulary note: text is the code section, data holds initialized global data, bss holds uninitialized global data, and dec and hex are the first three columns summed, in decimal and in hexadecimal.

Now the numbers. In the hello.o row, text comes to 156 and data and bss are clean zeros — we did only write six lines of code. And hello_app? text grew to 1708, and data and bss went from nothing to something. Who is the extra? Our code did not grow; the extra parts are other object files and the runtime startup code, merged in at the linking step. And who put them there? No rush — in the next section you will see the answer yourself.

## g++ itself is not the compiler

We have now run all four stages with our own hands, so let's blow g++'s cover: from start to finish, g++ was never the compiler.

Give g++ a `-v`, compile all the way through one more time, and have it report what it does behind the scenes:

```bash
g++ -v hello.cpp -o hello_v
```

The output runs long. I have cut the parts off the main thread; only three commands are actually doing the work. The ellipses are mine, and plenty of arguments got dropped along the way:

```text
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/cc1plus -quiet ... -o /tmp/ccC9eT4s.s
 as -v --64 -o /tmp/ccRhctwc.o /tmp/ccC9eT4s.s
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/collect2 ... -o hello_v ... crt*.o ... /tmp/ccRhctwc.o -lstdc++ -lm -lgcc_s ...
```

Look at the one leading the pack, cc1plus: that is the real compiler. There is an interesting detail here: the two stages we called preprocessing and compilation are handled entirely by this one program. So the four stages we speak of are a logical division for observing the process, convenient for stopping midway and pulling out intermediates; how the real tools divide themselves up does not map onto it one to one. That is precisely the job of the `-E`, `-S`, and `-c` we used earlier: have g++ work to a certain stretch and then call it a day, keeping the intermediate artifact.

The `as` in the middle is the assembler, born out of binutils, and its job is translating `.s` into `.o`. The finale, `collect2`, is a wrapper around the linker `ld`. All the good stuff is pressed into that one line, so let's zoom in on it.

Stuffed in there is a batch of things we never asked for by name: `Scrt1.o`, `crti.o`, `crtbeginS.o`, `crtendS.o`, `crtn.o` — a set of C runtime startup files. collect2 also took the liberty of bringing along a row of default libraries: the `-lstdc++`, `-lm`, `-lgcc_s`, and `-lc` we never wrote are all in there. We did not write a single line of startup code; it came by itself. What the startup files do before main travels the same road as interpreter, and we leave that for piece 14 (program startup). And the implementation of `operator<<` lives in the library that `-lstdc++` points to — that is the answer to the suspense from earlier. You can now explain the `size` comparison too: the only file we named was hello.o, yet collect2 statically merged in the startup files, and the growth in text, data, and bss is their contribution. That row of default libraries is another matter: they are attached as dynamic dependencies, and their code never enters this file. Just how dynamic libraries get attached — that we leave for piece 08 (Dynamic Libraries Overview: Build Time and Run Time).

With that, g++'s identity is settled: it is a driver. What we casually call "just compile it" is, behind the scenes, g++ summoning cc1plus, as, and collect2 in order, handing each step's output to the next, and translating the arguments we pass into the form each of them understands. So strictly speaking, today we never touched the compiler directly, not once; g++ has been running the errands for us the whole way.

## The sixteen pieces of this volume

Today we touched each of the four stages; the remaining fifteen pieces, broadly, zoom in on them:

- The **compilation segment** covers 02 through 05: 02 Preprocessing: How a Translation Unit Comes to Be, 03 What the Compiler Does: From C++ to Assembly, 04 Object File Anatomy: What's Inside a .o, and 05 The Linker: Symbol Resolution and undefined reference. Today we only looked at each stage's artifact; the internal unfolding is their job.
- The **reuse segment** is about 06 and 07: one piece packages static libraries, and the other takes on templates, inline, and the ODR (the one definition rule). How do we package the code we write for others to reuse? How does a single definition manage not to be duplicated? The answers are sitting in those two pieces.
- The **dynamic library engineering segment** is the longest, running from 08's overview of dynamic libraries all the way to 13's runtime loading and plugins: 09 is about position independence and GOT/PLT (GOT is short for Global Offset Table), 10 about library search logic, 11 about symbol visibility, and 12 about ABI design (ABI stands for Application Binary Interface). The mechanisms behind the dynamically linked and @PLT we lightly brushed today all get unfolded in this segment.
- The **advanced segment** wraps up: 14 program startup, 15 LTO (Link Time Optimization) and modern linkers, plus 16's side story. The side story takes up a fun question: can a dynamic library be run directly as an executable? Today we only glimpsed interpreter from a distance; the whole pipeline before main belongs to piece 14.

## The build system's view

Day to day you probably don't type g++ directly; you build inside a CMake project. That is fine — the build system can print out, exactly as it runs them, the commands it executes:

```bash
cmake --build build --verbose
```

With `--verbose` on, every real command the build runs gets printed: each source file gets its own compile command, and a single link command wraps up the job. What we typed by hand today and what it runs are the same kind of thing; the difference is only in the length of the paths and the number of the arguments.

So there is no magic in the build system: it does no compiling itself. It lines the four stages up for us, arranges the compilation of hundreds of source files in parallel, and gets the paths, macro definitions, and link options filled in correctly, one by one. As for how to use CMake itself, that is volume seven's turf (engineering practice). Here we are confirming just one thing: what runs when you build every day is exactly today's four stages.

## Back to daily life: where undefined reference happens

Finally, let's return to a moment you know all too well: you changed a bit of code, ran the build command, and the screen flooded with undefined reference — and the program didn't come out.

Now you can work out for yourself which stage the error lands in. Preprocessing won't take this blame; all it does is text substitution. The compiler doesn't handle it either. We saw it with our own eyes in the assembly today: the `operator<<` that hello.cpp calls is defined nowhere in this file, and the compiler went right ahead and calmly generated the call instruction. When translating a single file, using something defined elsewhere is perfectly normal business; the compiler's only job is to record the reference faithfully.

The real checkpoint is linking. The linker takes every one of those references and goes looking for the implementation across all the .o files and libraries: the ones it finds get connected, and for the ones it doesn't, you receive an undefined reference. Why didn't our hello_app complain? The implementation of `operator<<` lives in libstdc++, and collect2 carries `-lstdc++` by default; the names matched up, and the link went through.

And what if you linked the library and it still can't be found? In what order does the linker search libraries? Do static libraries or dynamic libraries come first? Why does it claim it can't find a library that clearly exists? Those belong to piece 05 (The Linker: Symbol Resolution and undefined reference) and piece 10 (Library Search Logic: How Libraries Are Found at Link Time and Run Time), and we will go through them one scenario at a time.

The next piece is 02. We will start from those 44,971 lines of hello.i, go back to the preprocessing step, and see what it actually did.
