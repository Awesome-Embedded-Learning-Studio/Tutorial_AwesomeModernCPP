---
chapter: 13
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 18
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 基础
title: 'Compilation and Linking: Four Stages from .cpp to Executable'
description: 'Run g++ through its four stages one by one with -E, -S, -c, and -o, get a clear look at what each stage''s artifact looks like, and meet cc1plus, as, and collect2, the programs that actually do the work.'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/01-compilation-pipeline.md
  source_hash: ff6ccfacd656f114219f79c80245313b33865f7f0db16ca6d9beebb20312cb47
  translated_at: '2026-10-13T03:40:32+00:00'
  engine: anthropic
  token_count: 3300
---

# Compilation and Linking: Four Stages from .cpp to Executable

Just about every day we type out a command like this one, and those of us who don't type it are clicking the build button in an IDE instead:

```bash
g++ hello.cpp -o hello
```

Enter goes down, a few seconds pass, and a runnable `hello` is sitting in the directory. So what did the machine actually do during those seconds? Most tutorials turn the page right here, as if "it runs" were already enough, and you flip along to the next chapter with them. But you have almost certainly run into `undefined reference`, run into duplicate-definition errors, or spent a good while staring blankly at a wall of linker errors. All of the things that give you those headaches happen inside those few seconds in the middle, and those seconds happen to be the least-talked-about part of C++ engineering.

In this volume I want to account for everything that happens in those few seconds. As the opening article of the volume, we'll do something plain and simple: the command you type hides four stages behind it, and we'll run each stage by hand, pulling out the intermediate artifacts one by one to have a look. And once we're done, there's one more thing that may surprise you: the g++ you type every day doesn't actually compile a single line of code itself. The programs doing the real work are someone else, and we'll meet them face to face later on.

## Our Specimen: Six Lines of Code

The whole series starts from this file:

```cpp
// hello.cpp
#include <iostream>

int main() {
    std::cout << "hello, linker!\n";
    return 0;
}
```

Six lines in total, and it can't get any smaller than this. What you type into the terminal is g++, but as you'll see today, the names that actually take the stage are three others.

I work in WSL2 (the Windows Subsystem for Linux), with GCC 16.2.1 as the toolchain, paired with binutils 2.47 and glibc 2.44. binutils is GNU's suite of binary tools; the assembler and the linker we're about to meet both come out of it. glibc is GNU's C library, the runtime library for C/C++ programs on Linux. Every piece of output in this article is something I actually ran. If your versions differ, details like line counts and paths will drift a little, but the four stages themselves haven't changed in decades.

## Preprocessing: Source In, Source Out

g++ also gives us a few switches, and with them we can stop g++ partway through and keep the intermediate artifact. The first one is `-E`: once preprocessing finishes, it stops right there.

```bash
g++ -E hello.cpp -o hello.i
```

The artifact this time is `hello.i`. What did it actually turn into? Let's count the lines with `wc`:

```text
$ wc -l hello.cpp hello.i
 44971 hello.i
     6 hello.cpp
```

Six lines went in, forty-four thousand nine hundred and seventy-one came out. The first time I saw that number, I sat there scrolling the terminal for a good while, and everything scrolling past was header files.

Where did all the extra material come from? From the `#include <iostream>` line we wrote. When the preprocessor sees `#include`, it copies the referenced file in verbatim. iostream itself includes other headers, so the copying keeps going, and it grows past forty thousand lines. Our file contains no preprocessing directives besides `#include`. If we had written macros, they would get expanded at this stage too. Comments are easier: they're simply stripped out.

So what kind of file is `hello.i`? Let's let the `file` command answer:

```text
hello.i:   C++ source, ASCII text
```

See that: it's still C++ source code, honest-to-goodness ASCII text. Preprocessing is a source-to-source text transformation and produces nothing machine-specific. You can open `hello.i` right now and scroll all the way to the bottom: our `main` is lying there untouched, preceded by forty-thousand-odd lines of expanded headers.

> How headers keep themselves from being copied in twice, what other tricks macro expansion has up its sleeve, what conditional compilation is for — all of that belongs to the next article. Those forty-thousand-odd lines of hello.i, we'll come back and deal with them then.

## Compilation: Source Becomes Assembly

Time for the second switch, `-S`: we have g++ stop right after generating the assembly.

```bash
g++ -S hello.cpp -o hello.s
```

```text
hello.s:   assembler source, ASCII text
```

The output is still text, but it's a different language now. Here is how `hello.s` opens; let's read it line by line:

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

Let's start from `main`. It's still called main; only its identity has changed: it's now a label in the assembly, and the two instructions right below it, `pushq` and `movq`, are the usual function-opening routine that sets up this call's stack frame.

Now look at the string literal we wrote, `"hello, linker!\n"`. It has changed shape too: the compiler took it away and gave it an internal label, `.LC0`, and whenever later code needs it, it has to reference the string by address.

That garbled-looking string `_ZSt4cout` is actually `std::cout`. C++ has namespaces and overloads, so far too many things share a name, and the compiler simply encodes all of that information into the symbol name to guarantee no collisions. This move is called name mangling. No need to rush into reading it — hand it to `c++filt` and it reports the original name back to us on the spot. For today, one representative is enough: once cout enters the world of assembly, its name becomes `_ZSt4cout`.

The most interesting line is the call at the end. Let me translate the function it calls for you: it's the very `operator<<` we're using, the overload that sends a string into cout. Two things here are worth stopping for.

First, the implementation of `operator<<` isn't anywhere in our six lines of code. We only called it; its implementation lives somewhere else. Where? I'll keep you in suspense — in the section on g++'s real identity, you'll see it with your own eyes.

The other is the `@PLT` suffix hanging off the called symbol. That suffix has to do with how calls into shared libraries work, and alongside the PLT there's a GOT: the PLT is the trampoline mechanism a function call goes through, and the GOT is the table it consults for addresses. It's completely fine if this makes no sense yet — the article on position independence and GOT/PLT exists precisely for them.

> As for the compiler's internals, every layer from parsing through optimization to instruction selection could fill a whole article on its own. The compiler article doesn't dig into those layers' algorithms; it looks from the other end: your source code goes in, and where in the output assembly the compiler laid its hands on it.

## Assembly: Text Becomes Binary

On to the third switch, `-c`: we have g++ stop after producing the object file.

```bash
g++ -c hello.cpp -o hello.o
```

```text
hello.o:   ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
```

At this point, the artifact is no longer meant for human eyes. `file` says it's an ELF 64-bit relocatable file. ELF stands for Executable and Linkable Format, and it's the container used by both object files and executables on Linux — we'll run into it again and again in the object-file anatomy article and the side article. Relocatable means just what it says: the addresses inside the file are still provisional, waiting for the linking stage to decide where they end up. The assembler's work is entirely mechanical: it takes our `hello.s`, translates it into machine code one instruction at a time, and packages the result in ELF format. That's where the `.o` files we're always talking about come from.

One more word in the output should make us happy: not stripped, meaning the symbol information all stayed in. This is tremendous news for us — when we later dissect object files with tools like `nm` and `objdump`, we'll be leaning entirely on the symbol table it preserved. It's the star of the object-file anatomy article; when we get there, we'll bring the tools and take this hello.o apart from the inside out.

## Linking: Object File Becomes Executable

At the linking step, we take the previous stage's `hello.o` as the raw material:

```bash
g++ hello.o -o hello_app
```

```text
hello_app: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2
```

Compare this output with the `hello.o` line and quite a few differences jump out. The format is still ELF, but the identity has changed from relocatable to pie executable. pie means position-independent; the fine print behind that is a question the position-independence article will answer for us. Either way, it's now a program that genuinely runs. The dynamically linked that follows says it depends on shared libraries at runtime. And at the end there's a new face called interpreter, dragging a long path behind it.

Time to reveal the interpreter's identity: it's the dynamic linker. Our program uses shared libraries like libstdc++, and when the operating system loads it up and runs it, it's the interpreter that fetches each needed library and hooks everything up. Between program loading and the first line of main, there's a whole pipeline, and that's the subject of the program startup article.

For a more tangible look at how much stuff linking stuffed into the program, there's the `size` command:

```text
$ size hello.o hello_app
   text	   data	    bss	    dec	    hex	filename
    156	      0	      0	    156	     9c	hello.o
   1708	    632	    280	   2620	    a3c	hello_app
```

A quick glossary for the columns: text is the code section, data holds initialized global data, bss holds uninitialized global data, and dec and hex are the first three columns summed up in decimal and hexadecimal.

Now the numbers. In hello.o's row, text is 156 and data and bss are a clean 0 — after all, we only wrote six lines of code. What about hello_app? text grew to 1708, and data and bss went from nothing to something. Who's the extra? Our code didn't get any longer; what got added is other object files and the runtime's startup code, merged in at the linking step. Who put them there? Hold on — in the next section you'll see the answer for yourself.

## g++ Itself Is Not the Compiler

We've now run all four stages by hand; time for the reveal: g++ was never the compiler at any point.

Let's put `-v` on g++ and do one complete compile, so it lays out what happens behind the curtain:

```bash
g++ -v hello.cpp -o hello_v
```

The output is quite long; I've cut the parts unrelated to our storyline, and only three commands are actually doing the work. I added the ellipses myself — plenty more parameters were left out:

```text
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/cc1plus -quiet ... -o /tmp/ccC9eT4s.s
 as -v --64 -o /tmp/ccRhctwc.o /tmp/ccC9eT4s.s
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/collect2 ... -o hello_v ... crt*.o ... /tmp/ccRhctwc.o -lstdc++ -lm -lgcc_s ...
```

Look at the first one, cc1plus — that's the real compiler. Here's an interesting detail: the two stages we talked about, preprocessing and compilation, are both handled by this single program. So the four stages we've been counting are a logical division for observation, a convenience that lets us halt partway and extract intermediate artifacts. The real tools' boundaries don't map onto it one to one. The `-E`, `-S`, and `-c` flags we used earlier do exactly this job: make g++ clock out after a given segment, leaving the intermediate artifact behind.

The `as` in the middle is the assembler out of binutils, and its whole job is turning `.s` into `.o`. The finale, `collect2`, is a wrapper around the linker `ld`. All the good stuff is packed into that one line, so let's zoom in on it.

Packed inside are things we never asked for: `Scrt1.o`, `crti.o`, `crtbeginS.o`, `crtendS.o`, `crtn.o` — a set of C runtime startup files. collect2 also took the liberty of bringing along a row of default libraries we never wrote: `-lstdc++`, `-lm`, `-lgcc_s`, and `-lc`, all of them in there. We didn't write a single line of startup code, yet it showed up on its own. What the startup files do before main runs is on the same road as the interpreter, so we'll leave it for the program startup article. As for the implementation of `operator<<`, it lives in the library `-lstdc++` points to — there's the answer to the suspense I built earlier. You can now also explain the size output comparison: the only thing we specified was hello.o, yet collect2 statically merged the startup files in, and the growth in text, data, and bss is their contribution. The row of default libraries is a different story: they're attached as dynamic dependencies, and their code never enters this file. How shared libraries actually get attached, we'll save for the dynamic libraries overview.

Having talked this far, g++'s identity is clear: it's actually a driver. What we usually call "compiling something" is, behind the scenes, g++ summoning cc1plus, as, and collect2 in order, handing each step's output to the next, and translating the arguments we passed into the form each tool understands. So strictly speaking, we never touched the compiler directly today — g++ has been running the errands for us the whole time.

## The Sixteen Articles of This Volume

Today we've given each of the four stages a first touch; the remaining fifteen articles more or less take each one and blow it up:

- The **compilation segment** covers 02 through 05: 02 preprocessing, 03 what the compiler does, 04 object-file anatomy, and 05 the linker. Today we only looked at each stage's artifact; the innards are left to those.
- The **reuse segment** is 06 and 07: one article on packaging static libraries, the other wrangling templates, inline, and the ODR (the One Definition Rule). How do we package the code we write so others can reuse it? How does the same definition avoid being duplicated? The answers live in those two.
- The **shared-library engineering segment** is the longest, running from the dynamic libraries overview in 08 all the way to runtime loading and plugins in 13: 09 covers position independence and GOT/PLT (GOT stands for Global Offset Table), 10 covers library search logic, 11 covers symbol visibility, and 12 covers ABI (Application Binary Interface) design. The dynamically linked and @PLT we brushed against lightly today — the mechanisms behind them all get unfolded in this segment.
- The **advanced segment** closes it out: 14 program startup, 15 LTO (Link Time Optimization) and modern linkers, plus the side article in 16. The side article takes up a fun question: can a shared library be run directly, as an executable? Today we only glimpsed the interpreter from a distance; the whole flow before main belongs to the program startup article.

## The Build System's Perspective

Day to day you probably don't type g++ directly — you build inside a CMake project. That's actually fine, because the build system can lay out the commands it really executes, exactly as they run:

```bash
cmake --build build --verbose
```

With `--verbose` added, every real command run during the build gets printed: each source file has its own compile command, and the finale is a single link command. What we typed by hand today and what it runs are the same kind of thing; the only difference is the length of the paths and the number of arguments.

So there's no magic in a build system: it doesn't compile anything itself. It lines up the four stages for us, schedules the compilation of hundreds of source files in parallel, and fills in the paths, macro definitions, and link options one by one. As for how to use CMake itself, that's Volume 7's (engineering practice) territory. Here we only confirm one thing: what runs when you build every day is exactly today's four segments.

## Back to Daily Life: Where undefined reference Happens

Finally, let's return to a moment you know best: you change a bit of code, type out the build command, the screen fills up with undefined reference, and the program refuses to build.

You can now reason through for yourself which segment this error comes from. Preprocessing isn't to blame — all it does is text substitution. The compiler doesn't actually care either: we saw it with our own eyes in the assembly today — the `operator<<` hello.cpp calls isn't defined anywhere in this file, yet the compiler calmly generated the call instruction anyway. When translating a single file, using things defined elsewhere is perfectly normal; the compiler's only job is to faithfully record the reference.

The real checkpoint is linking. The linker takes each such reference and searches every .o and library for an implementation: found means wired up; not found, and you receive an undefined reference. Why didn't our hello_app error out? The implementation of `operator<<` is in libstdc++, and collect2 brings `-lstdc++` along by default — the name matched, so the link went through.

And what if the library is linked and the symbol still can't be found? In what order does the linker search libraries? Do static or shared libraries come first? Why does it insist a library can't be found when it's plainly there? Those belong to the linker article and the library-search article, and we'll walk through them scenario by scenario when we get there.

The next article picks up from those 44,971 lines of hello.i and goes back to see exactly what the preprocessing step did.
