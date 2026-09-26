---
chapter: 13
difficulty: intermediate
order: 10
platform: host
reading_time_minutes: 10
tags:
- cpp-modern
- host
- intermediate
title: 'Deep Dive into C/C++ Compilation and Linking · Side Story: How a Dynamic Library Can Run Like an Executable'
description: 'Why directly executing a .so ends in a segfault while libc manages to politely print its version info — a complete teardown from ELF entry points to hand-rolled syscalls'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/10-dynamic-lib-as-executable.md
  source_hash: c6ee0127bd343e190182b6825fa9c4890e7e45c0e06a365db3514060df3c683a
  translated_at: '2026-09-26T00:01:53+00:00'
  engine: anthropic
  token_count: 3200
---
# Deep Dive into C/C++ Compilation and Linking · Side Story: How a Dynamic Library Can Run Like an Executable

I know some readers will chuckle the moment they see this topic and conclude that I am talking nonsense. To be honest, at the very very beginning I laughed this off too — it simply sounded absurd. But the truth is, a dynamic library **can be executed just like an executable.**

Someone is bound to throw a Segmentation Fault straight in my face and tell me I am indeed talking nonsense. You can switch to the /lib directory yourself, pick whichever library you fancy — my own eye fell on libcurl and libcrypt — and we can simply try executing them.


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ /lib/libcurl.so
Segmentation fault         (core dumped) /lib/libcurl.so
[charliechen@Charliechen runaable_dynamic_library]$ /lib/libcurl.so.4.8.0
Segmentation fault         (core dumped) /lib/libcurl.so.4.8.0
[charliechen@Charliechen runaable_dynamic_library]$ /lib/libcrypt.so.2.0.0
Segmentation fault         (core dumped) /lib/libcrypt.so.2.0.0

```

Our first thought is — why? Why did things turn out this way? The answer is simple. In later posts I will stress that, generally speaking, anything ending in .so is a dynamic library (or shared library — as I have already noted, on today's operating systems there is no longer any need to deliberately distinguish between shared libraries and dynamic libraries)

> [Deep Dive into C/C++ Compilation and Linking · Part 2: An Introduction to Static and Dynamic Libraries — CSDN blog](https://blog.csdn.net/charliechen114514191/article/details/154828385)

Clearly, when we type in a file's absolute path directly, the operating system's bash will try to treat it as a program that can run on its own. That, however, clashes with our definition of a dynamic library: a **dynamically shared component** bundling a set of functions and data. Because a shared library is not designed with a standard main entry point the way an ordinary program is (the $\text{main}$ function), running one directly will very likely send the execution flow jumping to an invalid memory address. When the operating system detects this kind of **illegal memory access** (an attempt to touch memory regions the program has no right to access), it triggers a **segmentation fault**. I imagine that by this point many readers have already made up their minds that the claim made in this post — that a dynamic library **can be executed like an executable** — is simply wrong.

Except it is not. Let's try executing the C library once more:


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ /lib/libc.so.6
GNU C Library (GNU libc) stable release version 2.42.
Copyright (C) 2025 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.
There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
Compiled by GNU CC version 15.2.1 20250813.
libc ABIs: UNIQUE IFUNC ABSOLUTE
Minimum supported kernel: 4.4.0
For bug reporting instructions, please see:
<https://gitlab.archlinux.org/archlinux/packaging/packages/glibc/-/issues>.

```

Huh? That is nothing like what we expected. This time, not only did the C library avoid a segmentation fault, it even printed out a highly identifiable string and exited gracefully! Pretty mysterious, isn't it? No matter — I will walk you through, step by step, exactly what happened.

## So, What Exactly Is Going On Here

Simple. Let's start like this — since the whole affair concerns where a program's execution begins, friends familiar with the ELF file format will readily point out that the trick probably hides in the address the ELF Header points to. It is almost too easy to guess: the Entry Point that libc's ELF Header points to must be **different** from that of an ordinary component-purpose library such as libcurl. And the tool for inspecting ELF header information is none other than the famous `readelf`.

One piece of ELF fundamentals deserves emphasis here — every ELF file (executable or shared library) has an "entry point", which is where the CPU starts executing instructions. Put differently, it gives the CPU's execution flow (the value of EIP or RIP on x86-64) a definite initial value.


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ readelf -h /lib/libcurl.so
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Shared object file)
  Machine:                           Advanced Micro Devices X86-64
  Version:                           0x1
  Entry point address:               0x0
  Start of program headers:          64 (bytes into file)
  Start of section headers:          945200 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         11
  Size of section headers:           64 (bytes)
  Number of section headers:         28
  Section header string table index: 27

```

Well, would you look at that — mystery solved, no? If we try to treat `/lib/libcurl.so` as an executable, then at this point the operating system's loader reads `/lib/libcurl.so`, gets through the usual checks, and sets the jump address to `0x0`. Aha — isn't that exactly a null pointer access?

This is exactly the same in nature as doing something like this!


```cpp

#include <stdio.h>

int main() {
 printf("Jumping to address 0x0...\n");
 void (*func)() = (void (*)())0x0;
 func();
}

```

Compile and run it, and what you get is precisely:


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ gcc dump.c -o dump
[charliechen@Charliechen runaable_dynamic_library]$ ./dump
Jumping to address 0x0...
Segmentation fault         (core dumped) ./dump

```

So how about our libc?


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ readelf -h /lib/libc.so.6
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 03 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - GNU
  ABI Version:                       0
  Type:                              DYN (Shared object file)
  Machine:                           Advanced Micro Devices X86-64
  Version:                           0x1
  Entry point address:               0x27830
  Start of program headers:          64 (bytes into file)
  Start of section headers:          2145632 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         16
  Size of section headers:           64 (bytes)
  Number of section headers:         64
  Section header string table index: 63

```

Huh? So it really is different. Don't rush — all we have is a lone `0x27830`, which tells us nothing on its own. The next step is to bring out our mighty objdump technique and look at the details:

> Some readers will ask me: why not nm? Well, for dynamic libraries, what nm exposes are the addresses of the symbols exported to the outside; generally speaking, you won't find out what the EntryPoint actually corresponds to. But don't worry — we have one more trick up our sleeve, and that is reading the disassembly with objdump.


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ objdump -d /lib/libc.so.6 --start-address=0x27830 --stop-address=0x27860

/lib/libc.so.6:     file format elf64-x86-64

Disassembly of section .text:

0000000000027830 <gnu_get_libc_version@@GLIBC_2.2.5+0x10>:
   27830:       f3 0f 1e fa             endbr64
   27834:       55                      push   %rbp
   27835:       bf 01 00 00 00          mov    $0x1,%edi
   2783a:       ba e3 01 00 00          mov    $0x1e3,%edx
   2783f:       48 8d 35 5a d8 18 00    lea    0x18d85a(%rip),%rsi        # 1b50a0 <__nptl_version@@GLIBC_PRIVATE+0x2b2d>
   27846:       48 89 e5                mov    %rsp,%rbp
   27849:       e8 d2 6c 0e 00          call   10e520 <__write@@GLIBC_2.2.5>
   2784e:       31 ff                   xor    %edi,%edi
   27850:       e8 7b d8 0b 00          call   e50d0 <_exit@@GLIBC_2.2.5>
   27855:       66 2e 0f 1f 84 00 00    cs nopw 0x0(%rax,%rax,1)
   2785c:       00 00 00
   2785f:       90                      nop

```

No need to hurry. Let's fire up our memory powers now: starting from 0x27834, here is what the code is trying to do:

> [x64.syscall.sh](https://x64.syscall.sh/) — the syscall table reference, which I am leaving right here

- Put 0x01 into edi — this is where the first parameter the syscall needs is placed.

- Then the third parameter goes into edx. Come on — isn't that just the length of the string? Decimal **483**.

- Hold on, what we still need to place, a bit later, is the string address in rsi, which is the second parameter. Note this: the instruction is `lea` (Load Effective Address), which adds the offset to the address right after the current instruction. So you can't go looking for 0x18d85a directly — you have to add the current instruction's offset.

  As a refresher: how did objdump work out 1b50a0? First, the current instruction's base address sits at `0x2783f`, and the instruction itself, `48 8d 35 5a d8 18 00`, is 7 bytes long. So the next instruction is at `0x2783f + 7 = 0x27846`. Add the given offset address, and that gives — 0x27846 + 0x18d85a = 0x1b50a0. OK, we are now confident objdump did not lie to us (not that it ever would, most likely!)

Want to check whether it was really placed there?


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ hexdump -C -s 0x1b50a0 -n 483 /lib/libc.so.6
001b50a0  47 4e 55 20 43 20 4c 69  62 72 61 72 79 20 28 47  |GNU C Library (G|
001b50b0  4e 55 20 6c 69 62 63 29  20 73 74 61 62 6c 65 20  |NU libc) stable |
001b50c0  72 65 6c 65 61 73 65 20  76 65 72 73 69 6f 6e 20  |release version |
001b50d0  32 2e 34 32 2e 0a 43 6f  70 79 72 69 67 68 74 20  |2.42..Copyright |
001b50e0  28 43 29 20 32 30 32 35  20 46 72 65 65 20 53 6f  |(C) 2025 Free So|
001b50f0  66 74 77 61 72 65 20 46  6f 75 6e 64 61 74 69 6f  |ftware Foundatio|
001b5100  6e 2c 20 49 6e 63 2e 0a  54 68 69 73 20 69 73 20  |n, Inc..This is |
001b5110  66 72 65 65 20 73 6f 66  74 77 61 72 65 3b 20 73  |free software; s|
001b5120  65 65 20 74 68 65 20 73  6f 75 72 63 65 20 66 6f  |ee the source fo|
001b5130  72 20 63 6f 70 79 69 6e  67 20 63 6f 6e 64 69 74  |r copying condit|
001b5140  69 6f 6e 73 2e 0a 54 68  65 72 65 20 69 73 20 4e  |ions..There is N|
001b5150  4f 20 77 61 72 72 61 6e  74 79 3b 20 6e 6f 74 20  |O warranty; not |
001b5160  65 76 65 6e 20 66 6f 72  20 4d 45 52 43 48 41 4e  |even for MERCHAN|
001b5170  54 41 42 49 4c 49 54 59  20 6f 72 20 46 49 54 4e  |TABILITY or FITN|
001b5180  45 53 53 20 46 4f 52 20  41 0a 50 41 52 54 49 43  |ESS FOR A.PARTIC|
001b5190  55 4c 41 52 20 50 55 52  50 4f 53 45 2e 0a 43 6f  |ULAR PURPOSE..Co|
001b51a0  6d 70 69 6c 65 64 20 62  79 20 47 4e 55 20 43 43  |mpiled by GNU CC|
001b51b0  20 76 65 72 73 69 6f 6e  20 31 35 2e 32 2e 31 20  | version 15.2.1 |
001b51c0  32 30 32 35 30 38 31 33  2e 0a 6c 69 62 63 20 41  |20250813..libc A|
001b51d0  42 49 73 3a 20 55 4e 49  51 55 45 20 49 46 55 4e  |BIs: UNIQUE IFUN|
001b51e0  43 20 41 42 53 4f 4c 55  54 45 0a 4d 69 6e 69 6d  |C ABSOLUTE.Minim|
001b51f0  75 6d 20 73 75 70 70 6f  72 74 65 64 20 6b 65 72  |um supported ker|
001b5200  6e 65 6c 3a 20 34 2e 34  2e 30 0a 46 6f 72 20 62  |nel: 4.4.0.For b|
001b5210  75 67 20 72 65 70 6f 72  74 69 6e 67 20 69 6e 73  |ug reporting ins|
001b5220  74 72 75 63 74 69 6f 6e  73 2c 20 70 6c 65 61 73  |tructions, pleas|
001b5230  65 20 73 65 65 3a 0a 3c  68 74 74 70 73 3a 2f 2f  |e see:.<https://|
001b5240  67 69 74 6c 61 62 2e 61  72 63 68 6c 69 6e 75 78  |gitlab.archlinux|
001b5250  2e 6f 72 67 2f 61 72 63  68 6c 69 6e 75 78 2f 70  |.org/archlinux/p|
001b5260  61 63 6b 61 67 69 6e  67 2f 70 61 63 6b 61 67 65  |ackaging/package|
001b5270  73 2f 67 6c 69 62 63  2f 2d 2f 69 73 73 75 65 73  |s/glibc/-/issues|
001b5280  3e 2e 0a                                          |>..|
001b5283

```

That's enough! The rest of the analysis is plain to see: 0 is placed into edi as the argument for exit, and the library bows out gracefully.

## Can We Pull Off the Same Trick Ourselves

Come on — of course we can! Let me pull off this heist together with you right now! It will be a little hard, though, because this time we cannot lean on the libc library: a dynamic library's initialization differs from that of our ordinary executables — for instance, it does not proactively initialize the C Runtime, and there is no way to proactively link against the C library (I did previously try specifying a dynamic linker, and it turned out useless — the code crashed on a stack function jump, which left me a bit helpless; I fiddled with it forever and never got it working), and so on.

So, here is something we can cobble together now:


```cpp

#define NOT_API __attribute__((visibility("hidden")))

long NOT_API syscall_write(int fd, const char* buf, unsigned long len) {
 long ret;
 asm volatile(
     "syscall"
     : "=a"(ret)
     : "a"(1), "D"(fd), "S"(buf), "d"(len) // 1 is sys_write
     : "rcx", "r11", "memory");
 return ret;
}

void NOT_API syscall_exit(int code) {
 asm volatile(
     "syscall"
     :
     : "a"(60), "D"(code) // 60 is sys_exit
     : "memory");
}

unsigned long NOT_API ccstrlen(const char* s) {
 unsigned long i = 0;
 while (s[i])
  i++;
 return i;
}

int add(int a, int b) {
 return a + b;
}

void NOT_API _printf(const char* msg) {
 syscall_write(1, msg, ccstrlen(msg));
}

int NOT_API direct_load_helper_main() {
 _printf("Hey! Welcome CCLibrary! "
         "These is a dynamic library helps math calculations\n");
 _printf("Current Version is 0.1.0\n");
 _printf("You can process add by using the library!\n");

 // Must Call these to remind linux
 // to clear the stack
 syscall_exit(0);
}


```

Compile this code:

```bash
gcc -shared -fPIC -o libcclib.so cclib.c -Wl,-e,direct_load_helper_main

```

Run it, and there is your result!

```bash
[charliechen@Charliechen runaable_dynamic_library]$ ./libcclib.so
Hey! Welcome CCLibrary! These is a dynamic library helps math calculations
Current Version is 0.1.0
You can process add by using the library!

```

Interested readers can retrace the whole flow following my earlier analysis.

Then the question arises: can our other executable programs use this code the way they would use a library? Yes, they can. Let's lift the visible add symbol out into a header: cclib.h


```cpp

#pragma once

int add(int a, int b);

```

And in main.c, do the deed just as we do in our ordinary library programming:


```cpp

#include "cclib.h"
#include <stdio.h>

int main() {
 int result = add(1, 2);
 printf("Result of 1 + 2 = %d\n", result);
}


```

No pressure at all!


```cpp

[charliechen@Charliechen runaable_dynamic_library]$ gcc main.c -o main ./libcclib.so
[charliechen@Charliechen runaable_dynamic_library]$ ./main
Result of 1 + 2 = 3

```

## The Modern CMake Perspective

That `gcc -shared -fPIC -Wl,-e,direct_load_helper_main` line demonstrated in this post is essentially never hand-typed in a modern project; it gets handed over to CMake instead. `add_library(cclib SHARED cclib.c)` automatically adds `-fPIC` to the shared library and produces the `.so`; the `visibility("hidden")` move for symbol-visibility control corresponds to `set_target_properties(cclib PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)`, which CMake converts into `-fvisibility=hidden` for you. Changing the entry point (`-Wl,-e`) is a fairly rare special need — CMake has no built-in target property to cover it directly, so you normally shove it at the linker explicitly via `target_link_options(cclib PRIVATE "-Wl,-e,direct_load_helper_main")`. And on the other end, the executable `gcc main.c -o main ./libcclib.so` corresponds to `add_executable(main main.c)` plus `target_link_libraries(main PRIVATE cclib)`, where the link paths and `-lcclib` are all worked out automatically by CMake from the target dependency graph — no more hand-picking `-L`/`-l`. Once you understand the underlying mechanics of ELF entry points and symbol visibility, looking back at these CMake commands, you can see exactly which stretch of the linker's originally hand-written work each of them has taken over.
