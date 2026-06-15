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
title: 'Deep Dive into C/C++ Compilation and Linking (Bonus): Can a Shared Library
  Be Executed Like an Executable?'
translation:
  engine: anthropic
  source: documents/compilation/10-dynamic-lib-as-executable.md
  source_hash: f2314fe9d950e03f2cf309ca8e5a8a878c8cb3f598e71d53cbcde9fa28c12e57
  token_count: 2823
  translated_at: '2026-05-26T10:12:20.758953+00:00'
description: ''
---
# Deep Dive into C/C++ Compilation and Linking (Bonus): Can a Shared Library Be Executed Like an Executable?

I know some of you might laugh instinctively at this topic and think I'm talking nonsense. To be honest, when I first encountered this idea, I dismissed it with a laugh too, thinking it was absurd. But the truth is, a shared library **can indeed be executed just like an executable.**

Some people might just throw a Segmentation Fault at me and tell me I'm talking nonsense. You can navigate to the `/lib` directory yourself, pick a library you like, and try to execute it directly. For example, I've got my eye on `libcurl` and `libcrypt`. Let's try executing them directly.

```cpp

[charliechen@Charliechen runaable_dynamic_library]$ /lib/libcurl.so
Segmentation fault         (core dumped) /lib/libcurl.so
[charliechen@Charliechen runaable_dynamic_library]$ /lib/libcurl.so.4.8.0
Segmentation fault         (core dumped) /lib/libcurl.so.4.8.0
[charliechen@Charliechen runaable_dynamic_library]$ /lib/libcrypt.so.2.0.0
Segmentation fault         (core dumped) /lib/libcrypt.so.2.0.0

```

Our first thought is—why? Why does this happen? The answer is simple. In later blog posts, I will emphasize that, generally speaking, files ending in `.so` are shared libraries (or dynamic libraries; as I've mentioned before, in modern operating systems, we no longer need to strictly distinguish between shared libraries and dynamic libraries).

> [Deep Dive into C/C++ Compilation and Linking Part 2: Introduction to Dynamic and Static Libraries - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/154828385)

Obviously, when we directly input the absolute path of a file, the operating system's shell attempts to treat it as a standalone executable. However, this contradicts our definition of a shared library: a **dynamically shared component** containing a collection of functions and data. Since a shared library isn't designed with a standard main entry point (`$\text{main}$` function) like a regular program, the execution flow will likely jump to an invalid memory address when run directly. When the operating system detects this **illegal memory access** (attempting to access a memory region the program has no rights to), it triggers a **segmentation fault**. I imagine many of you reading this are now fully convinced that the claim in this blog post—shared libraries **can be executed like executables**—is completely wrong.

However, that's not the case. Let's try executing the C library again:

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

Huh? This isn't what we expected at all. Not only did the C library not segfault this time, but it actually printed a highly recognizable string and exited gracefully! Pretty mysterious, right? Don't worry, I'll walk you through step by step to figure out exactly what happened here.

## So, What Exactly Is Going On?

It's quite simple. Let's start here—since this involves the start of program execution, those familiar with the ELF (Executable and Linkable Format) will quickly point out that the secret might be hidden in the address pointed to by the ELF Header. It's easy to guess that the Entry Point in libc's ELF Header must be **different** from that of a typical component-oriented library like `libcurl`. To check the ELF header information, we turn to the famous ``readelf`` tool.

We need to emphasize a basic fact about the ELF format—all ELF files (executables and shared libraries) have an "entry point," which is where the CPU begins executing instructions. In other words, it provides the CPU's execution flow (the value of EIP or RIP on x86-64) with an exact initial value.

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

Aha! Isn't the truth plain to see now? If we try to treat ``/lib/libcurl.so`` as an executable, the operating system's loader reads ``/lib/libcurl.so``, passes the standard checks, and sets the jump address to ``0x0``. See? We're dereferencing a null pointer!

This is exactly the same in nature as doing this:

```cpp

#include <stdio.h>

int main() {
 printf("Jumping to address 0x0...\n");
 void (*func)() = (void (*)())0x0;
 func();
}

```

Compile and execute it, and we get exactly this:

```cpp

[charliechen@Charliechen runaable_dynamic_library]$ gcc dump.c -o dump
[charliechen@Charliechen runaable_dynamic_library]$ ./dump
Jumping to address 0x0...
Segmentation fault         (core dumped) ./dump

```

So what about our libc library?

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

Huh? It really is different. Don't worry, just seeing a ``0x27830`` doesn't tell us much. The next step is to bring out our `objdump` trick to see the details:

> Some of you might ask me why we don't use `nm`. Well, for shared libraries, `nm` exposes the addresses of exported symbols. Generally, you can't figure out what the Entry Point actually corresponds to. But don't worry, we have another trick up our sleeves: using `objdump` to look at the disassembly.

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

No need to rush. Let's put on our thinking caps. Starting from `0x27834`, the code attempts to do the following:

> [x64.syscall.sh](https://x64.syscall.sh/), I've put the Syscall table here for reference.

- Puts `0x01` into `edi`, which holds the first argument needed for the system call.

- Then puts the third argument into `edx`. Come on, isn't this just the length of the string? Decimal **483**.

- Don't rush, we still need to place the string address in `rsi`, which is the second argument. Notice that the instruction is ``lea`` (Load Effective Address), which adds the offset to the address of the current instruction. So we can't just look for `0x18d85a` directly; we have to add the offset of the current instruction.

  As a refresher, how did `objdump` calculate `1b50a0`? First, the base address of the current instruction is at: ``0x2783f``, and the instruction length itself is ``48 8d 35 5a d8 18 00``, totaling 7 bytes. So the next instruction is at ``0x2783f + 7 = 0x27846``. Adding the given offset address, we get `0x27846 + 0x18d85a = 0x1b50a0`. OK, we are now convinced that `objdump` isn't lying to us (though it obviously wouldn't!).

Want to verify if that's really what's stored there?

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
001b5260  61 63 6b 61 67 69 6e 67  2f 70 61 63 6b 61 67 65  |ackaging/package|
001b5270  73 2f 67 6c 69 62 63 2f  2d 2f 69 73 73 75 65 73  |s/glibc/-/issues|
001b5280  3e 2e 0a                                          |>..|
001b5283

```

That's enough! The subsequent analysis clearly shows that `0` is placed into `edi` as the argument for `exit`, and then it exits gracefully.

## Can We Actually Do This?

Come on! Of course we can! Now I'll walk you through doing it ourselves! It will be a bit tricky, though, because we can't rely on the libc library right now. The initialization of a shared library differs from that of our executable programs—for instance, it won't actively initialize the C Runtime, and there's no way to actively link the C library (I previously tried specifying a dynamic linker, but it didn't work, and the code crashed during a stack function jump. I was pretty powerless after spending ages on it without success), and so on.

So, let's put together our code:

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

Execute it, and we get the result!

```bash
[charliechen@Charliechen runaable_dynamic_library]$ ./libcclib.so
Hey! Welcome CCLibrary! These is a dynamic library helps math calculations
Current Version is 0.1.0
You can process add by using the library!

```

Interested readers can follow my previous analysis to walk through the process again.

So the question arises: can our other executable programs use this code just like a regular library? Yes, they can. Let's move the visible `add` symbol into a header file: `cclib.h`

```cpp

#pragma once

int add(int a, int b);

```

And in `main.c`, we do things just like we normally would in library programming:

```cpp

#include "cclib.h"
#include <stdio.h>

int main() {
 int result = add(1, 2);
 printf("Result of 1 + 2 = %d\n", result);
}


```

No sweat!

```cpp

[charliechen@Charliechen runaable_dynamic_library]$ gcc main.c -o main ./libcclib.so
[charliechen@Charliechen runaable_dynamic_library]$ ./main
Result of 1 + 2 = 3

```
