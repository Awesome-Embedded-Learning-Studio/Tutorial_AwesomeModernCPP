---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 Talk Notes —— C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 2
platform: host
reading_time_minutes: 29
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: Reading Assembly and Register ABI
translation:
  engine: anthropic
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md
  source_hash: bec3d8e1f569731bab727bc9560b1d5e262abad84aecb3225b9b1adff65d7b1f
  token_count: 5565
  translated_at: '2026-06-13T11:47:13.409719+00:00'
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
---
# Reading Assembly: Building Intuition from Scratch

Faced with a screen full of ``mov``, ``add``, and ``jmp`` paired with a bunch of unintelligible register names, a beginner's first reaction is often to close the tab. When a template errors out, we can at least search Stack Overflow, but assembly output looks like gibberish—it is hard to know where to start. However, with some targeted experiments using Compiler Explorer<RefLink :id="1" preview="Matt Godbolt, Compiler Explorer, godbolt.org, 2012" />, we discover that assembly can actually be understood by "half-reading, half-guessing"—we don't need to truly know how to write it.

## Clarifying the Environment First

All experiments below are performed on Compiler Explorer (godbolt.org). Regarding compilers, x86-64 uses GCC 16.1.1, ARM64 uses the aarch64 version of GCC 16.1.1, and RISC-V uses the riscv64 version of GCC 16.1.1. The operating system is uniformly set to Linux because the calling conventions on Windows are different, which leads to variations in assembly output—this will be discussed in detail later. The optimization level primarily focuses on ``-O2``, occasionally switching to ``-O0`` for comparison, the reasons for which will be explained later.

## Start with the Simplest Function

To understand what assembly actually looks like under different architectures, we start with the simplest ``square`` function—taking an input integer, multiplying it by itself, and returning it. The simpler the function, the more suitable it is for observing compiler behavior, because the logic is simple and the assembly is short, making the role of each instruction clear at a glance.

````cpp
int square(int x) {
    return x * x;
}
````

Intuitively, regardless of the CPU architecture, since the same task is being performed, the compiled assembly should be roughly similar. However, when we place the three architectures side-by-side in Compiler Explorer, we find they look completely different—instruction formats, register naming, and even the implementation of multiplication vary. But upon closer observation, a key pattern emerges: although their "appearances" differ, their skeleton is actually the same—fetch parameters from somewhere, perform an operation, and place the result in an agreed-upon location for return. Once we understand this skeleton, reading assembly is no longer intimidating.

## The x86-64 Version

Let's look at x86-64 first, as most development machines run this architecture. Under ``-O2`` optimization, GCC generates the following code:

````asm
square(int):
        imul    edi, edi
        mov     eax, edi
        ret
````

Seeing this code for the first time might raise a question: aren't arguments supposed to be on the stack? Why are they fetched directly from ``edi``? This is stipulated by the System V AMD64 ABI<RefLink :id="2" preview="System V Application Binary Interface, AMD64 Architecture, x86-64 psABI" /> (the calling convention for x86-64 on Linux)—the first few integer arguments of a function are passed via registers, with the first argument in ``edi`` and the return value in ``eax``. So the meaning of these three instructions is clear: ``imul edi, edi`` is the two-operand multiplication form of x86—the left operand is both source and destination. It takes the value in ``edi``, multiplies it by itself, writes the result back to ``edi``, moves it to ``eax`` as the return value, and finally ``ret`` returns.

A natural question is: why not let the result of ``imul`` land directly in ``eax``, avoiding the extra ``mov``? In reality, the two-operand form of ``imul`` writes the result back to the first operand (i.e., ``edi``), while the calling convention requires the return value to be in ``eax``, so this ``mov`` is unavoidable. If we let the compiler use ``imul eax, edi`` (multiplying ``edi`` into ``eax``), we could save the ``mov``, but that would require moving ``edi`` to ``eax`` first before multiplying, resulting in the same instruction count. GCC chose the former strategy.

Another easy pitfall: if you compile the same code on Windows, the arguments will be in ``ecx`` instead of ``edi``, though the return value is still in ``eax``. This is one of the biggest differences between Windows x64<RefLink :id="3" preview="Microsoft, x64 Calling Convention, RCX/RDX/R8/R9" /> and Linux x86-64—different calling conventions. If you understand an assembly snippet on Linux and then compile it with MSVC on Windows, you will find the registers have completely changed. This isn't a mistake; it's a difference in calling conventions. So, when reading assembly, the first step is to confirm the platform and calling convention—this saves a lot of confusion.

## The ARM64 Version

Next, let's look at ARM64, also known as AArch64<RefLink :id="4" preview="ARM, AArch64 Architecture Reference Manual, ARMv8" />. For the same function, GCC aarch64 gives the following output under ``-O2``:

````asm
square(int):
        mul     w0, w0, w0
        ret
````

This code consists of only two instructions, even cleaner than x86-64. ``w0`` is the register in ARM64 that holds the first integer argument and return value (32-bit version; the 64-bit version is called ``x0``). Since the argument is ``int``, 32 bits are sufficient, so the compiler uses the ``w`` register instead of the ``x`` register. The ``mul`` instruction directly places the result of ``w0`` multiplied by ``w0`` back into ``w0``, then returns—no redundant ``mov``. ARM64 instruction design allows the result to be flexibly placed in any operand position.

It is worth noting that ARM64 register naming is much more regular than x86-64. In x86-64, ``eax``, ``edi``, and ``rsi`` are all different, requiring rote memorization of each register's specific purpose. In ARM64, it is simply ``x0`` to ``x30`` plus a stack pointer ``sp``, with 32-bit versions uniformly adding a ``w`` prefix. It is very neat. This regular naming lowers the barrier to reading—no need to remember a pile of legacy names, just knowing that ``x0``/``w0`` are for arguments and return values is enough.

## The RISC-V Version

Finally, there is RISC-V<RefLink :id="5" preview="RISC-V International, RISC-V ISA Specification, 2019" /> (V represents the Roman numeral five, so it is pronounced "Risk-Five"). Its assembly looks like this:

````asm
square(int):
        mul     a0, a0, a0
        ret
````

Wait, isn't this almost identical to ARM64? Indeed it is. ``a0`` in RISC-V is the register holding the first argument and return value (``a`` stands for argument), ``mul`` performs the multiplication, the result is placed back in ``a0``, and then it returns. Two instructions, clean and crisp.

As the youngest instruction set architecture, RISC-V's design draws on past experience. Its integer registers are simply named ``x0`` to ``x31``, and the ABI assigns them aliases: ``a0``-``a7`` are argument/return value registers, ``t0``-``t6`` are temporary registers, and ``s0``-``s11`` are callee-saved registers. What we see in assembly are the aliases, but fundamentally they are ``x`` numbers. This design of "unified underlying numbering + upper-level semantic aliases" is much easier to understand than the x86-64 approach where every register has a unique name.

## Looking Back: They Are Actually Saying the Same Thing

Placing the three architectures side-by-side reveals an interesting phenomenon: although instruction names, register names, and instruction counts differ, the "semantics" they express are exactly the same—"fetch argument → multiply → place return value → return". Reading assembly doesn't require recognizing every instruction; as long as we grasp which registers data flows between and what operation is performed, we can roughly guess what it is doing.

It is like reading a poem written in an unfamiliar language. You don't need to look up every word; you can feel its rhythm and gist through the position of words and repetitive patterns. Assembly is similar—seeing ``mul`` or ``imul`` tells you a multiplication is happening; seeing ``ret`` tells you the function is about to return; seeing data move from one register to another tells you something is being passed. This ability to "half-read, half-guess" is far more practical than rote memorization of the exact semantics of every instruction.

## A Key Reminder: Optimization Levels Radically Change What You See

The outputs shown above are all under ``-O2``. If optimization is turned off (``-O0``), the scene is completely different—massive amounts of ``push``, ``pop``, and memory reads/writes. Arguments are stored to the stack and read back, and intermediate results are repeatedly written to memory. ``-O0`` assembly is so verbose because ``-O0`` aims to allow the debugger to precisely map every C++ statement to assembly instructions, so it performs no optimization, keeping all variables obediently in memory. ``-O2`` is the code the compiler "truly" wants to generate. If the goal is to understand compiler optimization behavior and actual code performance, we must look at ``-O2`` or higher optimization levels; ``-O0`` will only lead us astray.

At this point, we have reviewed the assembly of the simplest functions across three mainstream architectures. Although it is just a ``square`` function, it establishes an important cognitive framework: knowing where parameters come from, where results go, and in which instruction the core computation is completed. With this framework, we will not be completely at a loss when looking at more complex function assembly later. Next, with this foundation in hand, let's look at some more realistic scenarios.

---

# What is the Relationship Between Machine Code and Assembly?

Many people use "machine code" and "assembly code" interchangeably, thinking they are just unintelligible stuff. But looking closely at objdump output, the column of hex on the left (``0f af ff``) and the column of text on the right (``imul edi, edi``) actually has a very straightforward one-to-one mapping, though we rarely think about it seriously.

## Clarify Concepts First: Machine Code is for Machines, Assembly is for Humans

That pile of hexadecimal numbers on the left—``0f``, ``af``, ``ff``, etc.—is machine code. Essentially, it is a string of bytes in memory. The CPU reads these bytes directly and interprets them according to rules hardwired into the hardware: reading ``0f af`` tells it this is a multiplication instruction, and subsequent bytes tell it where the operands are. The CPU doesn't know what ``imul`` is; it only recognizes numbers.

The column of text on the right, ``imul edi, edi``, is assembly code, the version for humans. It has an almost one-to-one mapping with machine code—one assembly instruction corresponds to a fixed-format sequence of machine code bytes. Therefore, we can "assemble" assembly code into machine code (what an assembler does) and "disassemble" machine code back into assembly code (what tools like objdump and IDA do). Of course, when disassembling back, comments are lost, variable names are lost, and semantic information like ``int x = n * n`` is completely gone—only cold instructions remain.

But this bidirectional conversion path exists and is very direct. Assembly is not a "high-level language" requiring a compiler to perform complex translation—it is almost just another way of writing machine code.

## Write a Simple Square Function and See What the Assembly Looks Like

To figure out the register situation, let's start with the most basic square function:

````cpp
// square.cpp
int square(int n) {
    return n * n;
}
````

Then compile it with gcc into an object file, without linking, just to see the assembly:

````bash
# 我的环境：Arch Linux WSL, x86-64, gcc 16.1.1
g++ -c -O0 square.cpp -o square.o
objdump -d -M intel square.o
````

Adding ``-M intel`` is because AT&T syntax (operands at the end, with ``%`` prefixes) is not very intuitive, while Intel syntax at least has operand order consistent with intuition. ``-O0`` turns off all optimizations so the compiler doesn't rewrite the code, allowing us to see the most raw translation result.

The output looks roughly like this (GCC 16, -O0):

````asm
0000000000000000 <_Z6squarei>:
   0:   55                      push   rbp
   1:   48 89 e5                mov    rbp,rsp
   4:   89 7d fc                mov    DWORD PTR [rbp-0x4],edi
   7:   8b 45 fc                mov    eax,DWORD PTR [rbp-0x4]
   a:   0f af c0                imul   eax,eax
   d:   5d                      pop    rbp
   e:   c3                      ret
````

The first reaction to seeing this might be: wait, shouldn't input parameters be "passed in" from somewhere? C++ functions have parameter lists, but assembly has no such thing. Where did the parameters go?

## Registers are the CPU's Built-in "Global Variables", But Their Use Has Rules

Inside the CPU, there is a small batch of extremely fast storage units called registers. We can understand them as a kind of "ultra-high-speed global variable"—directly inside the CPU, no memory access required, and read/write latency is nearly zero. But unlike global variables, the number of registers is extremely limited. In x86-64, there are only a dozen or so general-purpose registers (RAX, RBX, RCX, RDX, RSI, RDI, R8-R15), so it is impossible to stuff all data into them.

The key question is: who dictates which register does what? If compiler A thinks arguments go in RAX, and compiler B thinks they go in RDI, then the code they compile cannot call each other. You write a library, someone else writes a program, and if register usage doesn't match, the call fails.

Therefore, there must be a set of "traffic rules" that everyone follows for code to interoperate. This set of rules is the ABI (Application Binary Interface). The ABI specifies many things, one of the most basic being: when a function is called, which register holds arguments, which register holds the return value, and which registers can be freely modified after a call versus which must be restored to their original state.

Linux uses the System V AMD64 ABI, while Windows uses Microsoft's own x64 ABI. The two sets of rules are different. This is one of the reasons why binaries from Linux and Windows cannot be directly mixed (of course, there are more reasons, but the register convention difference is the most immediate layer).

## Parameters Enter via EDI, Results Must Exit via EAX

Returning to our square function. Under System V ABI rules, the first integer argument is placed in the RDI register. Note I wrote RDI (64-bit), but our parameter is ``int``, only 32 bits, so it actually uses the low 32 bits of RDI, which is EDI. The same applies to RAX/EAX; RAX is the 64-bit version, EAX is the 32-bit version.

So when the function starts, the value of ``n`` is already in EDI; you don't need to "fetch" it from somewhere, it is already there.

Then look at the instruction sequence: ``push rbp; mov rbp, rsp`` is the standard stack frame setup, ``mov DWORD PTR [rbp-0x4], edi`` stores the parameter from EDI onto the stack—this is typical ``-O0`` behavior; the compiler performs no optimization and obediently places all variables in memory. Then ``mov eax, DWORD PTR [rbp-0x4]`` reads it back from the stack into EAX, ``imul eax, eax`` performs the square, ``pop rbp`` restores the stack frame, and finally ``ret`` returns. The verbosity of ``-O0`` precisely illustrates why we recommended looking at ``-O2`` output earlier—three extra stack frame instructions drown out the core logic.

Next, ``imul eax, eax`` multiplies EAX by EAX, storing the result back in EAX. This is a distinctive design of x86: most instructions accept only two operands, and the left operand is both source and destination. This is the same meaning as ``a *= a`` in C++—read the value on the left, operate with the value on the right, and write back to the left. It is a "destructive" operation; after it is done, the original value on the left is overwritten. If the original value is needed later, it must be saved in advance.

Finally, ``ret`` is the return, handing control back to the caller. At this point, EAX holds the square result, and the caller knows to fetch it from EAX—because the ABI so stipulates.

## Register Names Are Not Arbitrary

Beginners seeing RAX, EAX, AX, AL might think they are different registers. In reality, they are different "views" of the same physical register: RAX is the full 64 bits, EAX is the low 32 bits, AX is the low 16 bits, and AL is the lowest 8 bits. Writing to EAX overwrites the high 32 bits of RAX (zeroing them), while writing to AL only changes the lowest byte, leaving the rest unaffected.

This characteristic is particularly prone to causing confusion during debugging. Staring at the register window, you might notice that the value of RAX doesn't match EAX and suspect the debugger is broken, but actually, it is because a certain instruction only modified the low 32 bits, and the high 32 bits are dirty data left over from a previous operation. So when looking at registers, be sure to clarify which "view" you are looking at.

At this point, the assembly face of a simple C++ function under x86-64 is clear: parameters are passed in via registers (not the stack, at least for the first few), computation is done between registers, and results are returned via registers. The whole process involves no memory access and is extremely fast. Of course, this is the simplest case; with more parameters, local variables, and optimizations enabled, things get much more complex, but the basic framework remains this set.

---

# Understanding Register Parameter Passing from a Single MOV Instruction: ARM and RISC-V Calling Conventions

The previous section discussed that square function. After compilation, the core is a single multiplication instruction. When the function returns, control is handed back to the caller. The caller previously stuffed the parameter into the EDI register (the x86-64 calling convention) and now expects to get the return value from the EAX register—this is the x86-64 rule: integer return values go via EAX (or RAX). So that ``imul edi, edi`` instruction does something very straightforward: multiply the value in EDI by itself, write the result back to EDI, then mov to EAX, and finally ret. The caller fetches it from EAX, done.

So the question is: under different architectures, how big is the "perceptual" difference in doing the same thing? Compiling the same function under three architectures and comparing the assembly line by line reveals very obvious differences.

## The Simplicity of ARM64

First, look at ARM64 (AArch64). Some might think ARM assembly is similar to x86, just with different instruction names. Actually opening objdump reveals differences far beyond expectations.

````cpp
// square.cpp —— 就这么个简单函数
int square(int value) {
    return value * value;
}
````

Run it with a cross-compilation toolchain:

````bash
# ARM64
aarch64-linux-gnu-g++ -O2 -c square.cpp -o square_arm64.o
aarch64-linux-gnu-objdump -d square_arm64.o
````

The output is like this:

````asm
square:
    mul w0, w0, w0
    ret
````

That's it. Two instructions, clean and crisp. One particularly comfortable aspect is: W0 is both input and output. In ARM's calling convention, W0 (32-bit) or X0 (64-bit) serves as the carrier for the first argument and also for the return value. So ``mul w0, w0, w0`` reads as "multiply w0 by w0, put the result back in w0". All three operands are the same register; visually, it is extremely unified.

Next, let's look at the machine code for these instructions. This reveals an important design difference.

````bash
aarch64-linux-gnu-objdump -d -j .text square_arm64.o | grep mul
# 0:   1b007c00    mul w0, w0, w0
````

``1b007c00``, four bytes. Now look at that ``ret``:

````asm
# 4:   d65f03c0    ret
````

``d65f03c0``, also four bytes. Two instructions, both exactly four bytes. This means the instruction decoder's job is very simple; the fetch stage fetches a fixed four bytes each time without any length judgment. This design is elegant, especially when contrasted with x86.

## x86 Variable-Length Instructions

The same function compiled under x86-64:

````bash
g++ -O2 -c square.cpp -o square_x64.o
objdump -d square_x64.o
````

````asm
square(int):
    0:   0f af ff                imul   edi,edi
    3:   89 f8                   mov    eax,edi
    6:   c3                      ret
````

The focus is on the byte length of the instructions:

- ``imul`` instruction: ``0f af ff``, three bytes
- ``mov`` instruction: ``89 f8``, two bytes
- ``ret`` instruction: ``c3``, one byte

Three instructions, three lengths: 3, 2, 1. Change the multiplication method, say ``imul eax, edi``, its machine code is ``0f af c7``, still three bytes, but the suffix differs from the imul above (``ff`` vs ``c7``) because the operand encoding is different. Change the scenario again, and if the multiplier is an immediate, the instruction length changes again.

"Variable-length instructions" is not just a textbook concept. Counting bytes against a hex dump reveals that every time the CPU front-end fetches an instruction, it must read the first few bytes to judge how long the instruction actually is before it can decide where the next instruction starts. x86 decoders are notoriously complex; to solve this, Intel stuffed a large amount of pre-decoding logic and micro-op caches into the CPU, essentially using hardware brute force to compensate for the historical baggage of instruction set design.

## RISC-V Fixed-Length Instructions

Now look at RISC-V (rv64gc):

````bash
riscv64-linux-gnu-g++ -O2 -c square.cpp -o square_rv64.o
riscv64-linux-gnu-objdump -d square_rv64.o
````

````asm
square:
    0:   02b50533    mul a0, a0, a0
    4:   8082        ret
````

Like ARM, a0 is both the first argument and the return value, and ``mul a0, a0, a0`` semantics are identical. However, there is a detail: the ``mul`` instruction is four bytes (``02b50533``), but the ``ret`` instruction is only two bytes (``8082``). RISC-V base instructions are fixed four-byte, but it supports a 16-bit compressed instruction extension (RVC), so common instructions like ``ret`` are compressed into two bytes. This is a compromise between fixed-length and variable-length, much more disciplined than x86's "completely unpredictable" variability.

## Number of Operands: Not All Instructions Are So Neat

At this point, you might think instructions are just "opcode + a few operands", quite neat. But looking through more assembly reveals that reality is far less pretty.

The ``mul`` and ``imul`` seen above are typical three-operand instructions (destination + source1 + source2), or two-operand (destination is also source1). But many instructions don't follow the pattern at all. Zero-operand instructions are simplest, like ``ret`` and ``nop``, needing no extra information. Single-operand is also common, like various jump instructions. Two- and three-operand we just saw.

What is truly confusing is "implicit operands". For example, in x86 there is a ``rep stosb`` instruction that functions to "write the value of the AL register repeatedly to the memory pointed to by RDI (or EDI), incrementing RDI/EDI after each write, with the repeat count controlled by RCX (or ECX)". AL, RDI/EDI, RCX/ECX—none of these three operands are visible in the instruction text; they are all implicit, hardcoded in the instruction definition. The person reading the assembly must remember which registers this instruction uses by default. The "number of operands" for such instructions is actually hard to define.

## Intel's Historical Baggage

The implicit operand problem makes x86 a "hard-hit zone". The reason isn't complex: the x86 instruction set evolved from the 8086 in 1978 all the way to today's x86-64, spanning more than 40 years. Each new generation of CPUs had to add new things on top of the old instruction set while maintaining backward compatibility—8086 machine code written in 1985 will still run on a CPU in 2026. This constraint sounds wonderful, but the cost is that the instruction set becomes increasingly bloated and irregular. The encoding space for new instructions is occupied by old instructions, so prefix bytes must be used for expansion, leading to increasingly complex decoding logic.

Does this situation sound familiar? C++'s backward compatibility issues are almost exactly the same—writing C++26 code today, the compiler still has to handle C89-style declarations, C-style casts, and various legacy features. Every time someone proposes "deleting some old feature", the answer is always "no, it will break existing code". So we move forward carrying this baggage.

In contrast, ARM and RISC-V are much cleaner. ARM64 was designed around 2011 (AArch64), a "clean room implementation"—not carrying 32-bit ARM's historical baggage, it redesigned a set of instruction encodings. RISC-V is even an academic project starting from scratch in 2010, with excellent instruction orthogonality: the same opcode format, change the register number and it works; there are no maddening rules like "this instruction implicitly uses EAX, that instruction implicitly uses EDX".

## Register Naming: The Origin of the A Register

We've been talking about EAX, W0, a0, but have you ever thought about why x86 registers have these strange names? There is historical meaning behind these names.

In x86, there is a register called A (Accumulator). In the 8080 or even earlier 8008 era, the A register was "the default register"—many operations defaulted to acting on A, without needing to specify it in the instruction. For example, addition, the instruction encoding for "add a value to A" is shorter than "add a value to B", because A is the "default target", saving the bits needed to specify the target register.

This design philosophy continued into x86. Today writing ``imul edi, edi``, if changed to ``imul ebx, ebx``, the machine code might be longer (depending on the specific encoding), because EAX (or RAX) is still a "privileged register" in many instructions—it is the implicit default target for many instructions, and a fixed participant in certain special operations (like the high bits of the double-precision result of ``mul`` being placed in EDX).

Many tutorials say "try to use EAX". This isn't some mystical optimization trick; it's a "privilege" given at the instruction set encoding level—using the A register can make instructions shorter and decoding faster. Of course, on modern CPUs this difference has been smoothed out by many microarchitectural optimizations, but understanding this background makes those implicit operand instructions seem less baffling.

At this point, "what a simple function call looks like at the assembly level" has been thoroughly worked through: from how parameters are passed and return values placed, to instruction encoding differences across architectures, to the historical origins of register naming. Each step isn't complex, but when pieced together, the whole system connects.

---

---

# Figuring Out Where Parameters Go During Function Calls—From Register Naming to ABI

When looking at assembly code generated by Compiler Explorer, the biggest psychological barrier is often not the instructions themselves, but the messy register names. RAX, EAX, AX, AL, AH—are these one thing or four things? Once we understand the x86 register layout, this problem is solved.

## First, Clarify the Relationship Between RAX, EAX, and AX

Back to the most fundamental question: what is a register? We can understand it as a small row of ultra-high-speed storage cells inside the CPU, extremely limited in quantity. In the 8-bit era, the most core register was the A register, or Accumulator, around which most arithmetic operations revolved. Later, CPUs evolved from 8-bit to 16-bit, 32-bit, and 64-bit. The width of this register grew, but its "status" remained unchanged—it is always the general-purpose register bearing the main computational load.

The key is: when you see RAX, you are seeing a 64-bit value. But when you see EAX, you are not seeing another register, but **the low 32 bits of the same register**. Similarly, AX is the low 16 bits, AL is the lowest 8 bits, and AH is the second lowest 8 bits (bits 8-15). They all point to the same physical storage, just "sliced" by different names.

A simple diagram illustrates this:

````text
63                              31        15  7    0
+--------------------------------+----------+----+----+
|              RAX               |   EAX    | AX      |
|                                |          +----+----+
|                                |          | AH | AL |
+--------------------------------+----------+----+----+
````

So when you see code like this in assembly, don't panic:

````asm
mov rax, rdi      ; 把 64 位参数放进 rax 做计算
shr rax, 32       ; 右移 32 位
mov eax, eax      ; 只保留低 32 位作为返回值
````

Here, switching from rax to eax doesn't mean data is moving between two registers; it is the compiler saying "calculation is done, now we only care about the low 32 bits". Type information from the C++ source (e.g., the parameter is int64_t but the return value is int32_t) is directly reflected in the assembly's use of different names for the same register. After type information disappears, it "lingers" in the assembly in this way.

## Those Weirdly Named Registers, and Easy-to-Remember New Friends

Once you understand the naming pattern of RAX, you might wonder about the others. RAX, RCX, RDX, RSP, RBP, RSI, RDI... these names seem completely lawless. They are all legacy names inherited from ancient times: A is Accumulator, C is Counter, D is Data, SP is Stack Pointer, BP is Base Pointer, SI and DI are Source Index and Destination Index. Knowing the historical background makes them slightly easier to remember, but largely it relies on muscle memory formed through repeated use.

However, there is good news: when AMD extended the architecture from 32-bit to 64-bit, the 8 new general-purpose registers were directly named R8 to R15. Clean and simple. So x86-64 now has 16 general-purpose registers, 8 with weird legacy names and 8 with clean numeric names.

Of course, there are SIMD/multimedia registers (XMM/YMM/ZMM, etc.), but that is another large topic; today we focus on general-purpose registers and function calls.

## Which Register Are Function Arguments In?

One of the biggest confusions in reading assembly is: you write a function, pass three arguments in, and the assembly turns into a bunch of mov instructions shuffling data between registers. Where did the arguments come from? This involves the ABI (Application Binary Interface).

The ABI specifies many things, but from the perspective of reading assembly, the one concern is: **which registers hold the first few arguments of a function**. As long as we know this, we can trace what C++ variables became in the assembly.

Take Linux (System V AMD64 ABI) as an example. The first six integer arguments (including pointers) are placed in these registers in order:

````text
第 1 个参数 → RDI
第 2 个参数 → RSI
第 3 个参数 → RDX
第 4 个参数 → RCX
第 5 个参数 → R8
第 6 个参数 → R9
````

Arguments exceeding six must be pushed onto the stack, accessed via stack pointer offsets. When using ``std::forward`` for perfect forwarding, if there are many parameters, the assembly will show a lot of stack operations because forwarding may "expand" the parameters, suddenly exceeding the capacity of six registers.

Return values are simpler, uniformly placed in RAX (if the return value is 128 bits, RDX:RAX are combined).

Floating-point arguments are slightly more complex, using a separate set of registers (XMM0 to XMM7), but the basic idea is the same—the first few go in registers, the rest go on the stack.

## Windows Rules Are Different

If using MSVC on Windows, the situation is different. The Windows x64 ABI allocates only four registers for passing arguments:

````text
第 1 个参数 → RCX
第 2 个参数 → RDX
第 3 个参数 → R8
第 4 个参数 → R9
````

Note the order and names differ from Linux. This means the same function on Linux passes the first six arguments via registers, while on Windows the fifth and sixth are already pushed to the stack. When debugging performance issues across platforms, the same C++ code looks completely different in assembly on both sides, often caused by ABI differences.

This difference actually has a subtle impact on API design. If you know only four registers are available on Windows, you tend to control the number of parameters when designing high-frequency interfaces. But we will expand on this topic later in specific scenarios.

## Verify It Yourself

Talk is cheap, let's write a simple function and throw it into Compiler Explorer:

````cpp
// 编译选项：-O1 -m64
// 平台：x86-64 Linux (GCC)

long add_three(long a, long b, long c) {
    return a + b + c;
}
````

The corresponding assembly looks roughly like this (GCC 16, -O1):

````asm
add_three(long, long, long):
    add rdi, rsi           ; rdi(a) += rsi(b)
    lea rax, [rdi + rdx*1] ; rax = rdi + rdx(c)
    ret
````

See, a is in RDI, b is in RSI, c is in RDX, completely consistent with our rules. The return value is in RAX. Clean.

Try one with more than six arguments:

````cpp
long sum_seven(long a, long b, long c, long d,
               long e, long f, long g) {
    return a + b + c + d + e + f + g;
}
````

The assembly becomes:

````asm
sum_seven(long, long, long, long, long, long, long):
    lea rax, [rdi + rsi]       ; a + b
    add rax, rdx               ; + c
    add rax, rcx               ; + d
    add rax, r8                ; + e
    add rax, r9                ; + f
    add rax, QWORD PTR [rsp+8] ; + g，从栈上取！注意偏移 +8，因为 [rsp] 是 call 压入的返回地址
    ret
````

The first six arguments are in RDI, RSI, RDX, RCX, R8, R9, and the seventh argument g has run onto the stack, accessed via ``[rsp+8]`` (the ``call`` instruction pushed the return address onto ``[rsp]``, so the first stack argument needs an offset of 8 bytes). Knowing the ABI rules makes reading assembly like having a map—no longer a screen of gibberish.

## By the Way, Mentioning ARM64

If you have touched ARM64 (like Apple Silicon or embedded development), it is much cleaner over there. General-purpose registers are directly called X0 to X30, no historical baggage. Function arguments are X0, X1, X2... in order, return value in X0. If you want to see the 32-bit version, just replace X with W, e.g., W0 is the low 32 bits of X0. The naming logic is the same as x86's RAX/EAX, but the names are much easier to remember.

At this point, register naming and parameter passing rules are thoroughly cleared up. Seeing rax then eax in assembly and getting confused comes from not knowing it is just slicing different widths of the same register. Understanding this brings peace of mind. Next, with this foundation, let's look at more complex assembly patterns.

---

# RISC-V Register Naming—From Numbers to Semantics

When reading RISC-V assembly, opening the disassembly window reveals a screen full of ``t0``, ``a7``, ``s1``, ``ra``. It looks similar to x86's ``rax``, ``rbx``, ``rcx``, seemingly a pile of letter abbreviations to memorize. But once you truly understand it, you realize RISC-V register naming isn't arbitrary abbreviation—it directly tells you what the register **should do**. Understanding the calling convention semantics behind the naming allows you to deduce these names yourself.

## Start with the Most Basic Numbers

RISC-V has 32 general-purpose registers, numbered ``x0`` to ``x31``. Note, it is 32, not 31—``x0`` is indeed an existing register, but it is hardwired to 0; writing anything to it yields 0, reading it always yields 0. This design may seem superfluous at first, but when writing inline assembly, you find having a constant zero directly usable as an operand saves many `__PRES
