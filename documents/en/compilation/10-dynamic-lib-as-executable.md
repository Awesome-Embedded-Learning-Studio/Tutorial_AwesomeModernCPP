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
title: 'Deep Dive into C/C++ Compilation and Linking (Bonus): Can Dynamic Libraries
  Be Executed Like Executables?'
description: ''
translation:
  source: documents/compilation/10-dynamic-lib-as-executable.md
  source_hash: 6e132ffb5494a28d5f02b2d94d1894c7dddf3313b093d570498af15271e8f174
  translated_at: '2026-06-16T03:28:12.214179+00:00'
  engine: anthropic
  token_count: 2829
---
# Deep Dive into C/C++ Compilation and Linking (Bonus): Can Dynamic Libraries Be Executed Like Executables?

I know some friends might subconsciously laugh at this topic and think I am talking nonsense. Actually, in the very beginning, I also laughed it off, thinking it was too absurd. However, in reality, dynamic libraries **can indeed be executed like executable files.**

Some people might immediately throw a `Segment Fault` at me, telling me I am spouting nonsense. You can switch to the `/lib` directory yourself, find a library you like, for example, I have my eye on `libcurl` and `libcrypt`, and we can try to execute it directly.

```text
$ /lib/x86_64-linux-gnu/libcurl.so.4.8.0
Segmentation fault (core dumped)
```

Our first thought is—why? Why did things turn out this way? The answer is simple. In subsequent blog posts, I will emphasize that generally speaking, files ending in `.so` are usually dynamic libraries (or shared libraries; I have already explained that in today's operating systems, we no longer need to strictly distinguish between shared libraries and dynamic libraries).

> [Deep Dive into C/C++ Compilation and Linking 2: Intro to Dynamic and Static Libraries - CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/154828385)

Obviously, when we directly input the absolute path of the file, the operating system's shell attempts to treat it as an independently runnable program. However, this is inconsistent with our definition of a dynamic library: a **dynamic shared component** containing a set of functions and data. Since shared libraries are not designed with a standard main entry point (the `main` function) like ordinary programs, when run directly, the execution flow is likely to jump to an invalid memory address. When the operating system detects this **illegal memory access** (attempting to access a memory area that the program has no right to access), it triggers a **segmentation fault**. I think many people, upon seeing this, are convinced that the point I made in this blog post—that dynamic libraries **can be executed like executable files**—is wrong.

However, that is not the case. We can try executing the C library again:

```text
$ /lib/x86_64-linux-gnu/libc.so.6
GNU C Library (Ubuntu GLIBC 2.35-0ubuntu3.4) stable release version 2.35.
Copyright (C) 2022 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.
There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
Compiled by GNU CC 11.3.0.
libc ABIs: UNIQUE IFUNC ABSOLUTE
Default branch protection: none
...
```

Hmm? This is very different from what we thought. This time, the C library not only didn't Segfault, but even printed a very distinctive string and exited gracefully! Very mysterious, right? Don't worry, I will take you step by step to explore exactly what happened.

## So, What Actually Happened?

It's simple. Let's start like this—since this involves the start of program execution, obviously friends familiar with the ELF file format will point out—perhaps our trick lies in the address pointed to by the ELF Header. It's almost easy to guess—it must be that the entry point of `libc`'s ELF Header is **inconsistent** with general component-purpose libraries like `libcurl`. So, the tool for viewing ELF header information is the famous `readelf` tool.

We need to emphasize a basic piece of knowledge about the ELF format—all ELF files (executables and shared libraries) have an "entry point," which is where the CPU starts executing instructions. In other words, it tells the CPU's execution flow (the value of EIP or RIP on x86-64) a definite initial value.

```text
$ readelf -h /lib/x86_64-linux-gnu/libcurl.so.4.8.0
...
Entry point address:               0x12710
...
```

```text
$ readelf -h /lib/x86_64-linux-gnu/libc.so.6
...
Entry point address:               0x27834
...
```

Oh ho, now isn't the truth revealed? If we try to treat `libcurl` as an executable file, the operating system's loader reads the ELF Header and passes general checks, then sets the jump address to `0x12710`. Ah ha, isn't that accessing a null pointer?

This is exactly the same nature as doing this:

```cpp
int main() {
    return ((void (*)())0)();
}
```

Compile and execute it, and you get exactly:

```text
Segmentation fault (core dumped)
```

So what about our libc library?

```text
$ readelf -h /lib/x86_64-linux-gnu/libc.so.6
...
Entry point address:               0x27834
...
```

Hmm? It's really different. Don't worry, with just an address, we know nothing. The next step is to bring out our `objdump` magic to see the details:

> Friends might ask me, why not `nm`? Well, for dynamic libraries, `nm` exposes the addresses of externally exported symbols. Generally speaking, you can't find what exactly corresponds to the EntryPoint. But don't worry, we still have a trick, which is using `objdump` to look at the disassembly.

```text
$ objdump -d /lib/x86_64-linux-gnu/libc.so.6 | grep -A 20 "27834"
...
0000000000027834 <__libc_start@@GLIBC_2.34>:
   27834:       48 8d 3d a5 d8 18 00    lea    0x18d8a5(%rip),%rdi        # 1b50e0 <_dl_discover_osversion+0x2b0>
   2783b:       48 8d 35 57 d8 18 00    lea    0x18d857(%rip),%rsi        # 1b5099 <_rtld_global+0x2d9>
   27842:       31 c0                   xor    %eax,%eax
   27844:       e9 07 00 00 00          jmp    27850 <__libc_start@@GLIBC_2.34+0x1c>
   27849:       0f 1f 84 00 00 00 00    nop    %eax,0x0(%rax)
   27850:       bf 01 00 00 00          mov    $0x1,%edi
   27855:       ba e3 01 00 00          mov    $0x1e3,%edx
   2785a:       be 5a d8 18 00          mov    $0x18d85a,%esi
   2785f:       b8 01 00 00 00          mov    $0x1,%eax
   27864:       0f 05                   syscall
...
```

Don't rush. Now, let's use our memory recall technique. Starting from `0x27834`, the code attempts to do these things:

> [x64.syscall.sh](https://x64.syscall.sh/), I've put the Syscall table here.

- Put `0x01` into `edi`. Here, the first parameter required by the system call is placed.
- Then put the third parameter into `edx`. Come on, isn't that just the length of the string? Decimal **483**.
- Don't worry, we also need to place the string address in `rsi` later, which is the second parameter. Notice that—the instruction is `lea` (Load Effective Address), which adds the offset to the address after the current instruction. So we can't directly look for `0x18d85a`, but we must add the offset of the current instruction.

  Reviewing this, how did `objdump` calculate `0x1b50a0`? First, the base address of the current instruction is at: `0x27834`. The length of the instruction itself is `be 5a d8 18 00`, totaling 7 bytes. So the next instruction is at `0x27834 + 0x7 = 0x2783b`. Adding the given offset address, that is—`0x2783b + 0x18d85a = 0x1b5095`. Wait, let's recheck the `objdump` output. The comment says `# 1b5099`. Let's re-calculate. `0x2783b + 0x18d85a` = `0x1b5095`. The `mov` is at `2785a`. `2785a + 0x5` (length of mov) = `2785f`. `2785f + 0x18d85a` = `0x1b5099`. OK, we are confident `objdump` didn't lie to us (mostly, of course it wouldn't!).

Want to see if it's really put there?

```text
$ xxd -s 0x1b5099 -l 64 /lib/x86_64-linux-gnu/libc.so.6
...
0001b5099: 474e 5520 4320 4c69 6272 6172 7920 2855  GNU C Library (U
0001b50a9: 6275 6e74 7520 474c 4942 4320 322e 3335  buntu GLIBC 2.35
...
```

Enough! The subsequent analysis is obviously putting `0` as the argument for `exit` into `edi` and exiting gracefully.

## Can We Do This Sort of Thing?

Come on! Of course we can! Now, I will accompany you to do this job! But it will be a bit difficult because we can't rely on the libc library now. The initialization of dynamic libraries is inconsistent with our executable programs. For example, it won't actively initialize the C Runtime, it can't actively link the C library (of course, I previously specified a dynamic linker and found it useless, and the code crashed on the stack function jump; I was a bit helpless and couldn't figure it out after a long time), etc.

So, now we can make one:

```cpp
// mylib.c
int add(int a, int b) {
    return a + b;
}

void _start() {
    add(1, 2);
    // exit gracefully
    __asm__ __volatile__(
        "movq $60, %%rax;" // syscall number for exit is 60
        "xorq %%rdi, %%rdi;" // status 0
        "syscall;"
        : // no output
        : // no input
        : "%rax", "%rdi"
    );
}
```

Compile this code:

```bash
gcc -shared -fPIC -o libmylib.so mylib.c
```

Execute it, and you get the result!

```text
$ ./libmylib.so
$ echo $?
0
```

Interested readers can follow my previous analysis to walk through the process again.

So the question is, can our other executable programs use this code like using a library? Yes, they can. We just need to move the visible `add` symbol into a header file: `cclib.h`

```cpp
// cclib.h
#ifndef CCLIB_H
#define CCLIB_H

int add(int a, int b);

#endif
```

And in `main.c`, do this just like our general library programming:

```cpp
// main.c
#include <stdio.h>
#include "cclib.h"

int main() {
    printf("1 + 2 = %d\n", add(1, 2));
    return 0;
}
```

```bash
gcc main.c -L. -lmylib -o test_app
```

No pressure at all!

```text
$ ./test_app
1 + 2 = 3
```
