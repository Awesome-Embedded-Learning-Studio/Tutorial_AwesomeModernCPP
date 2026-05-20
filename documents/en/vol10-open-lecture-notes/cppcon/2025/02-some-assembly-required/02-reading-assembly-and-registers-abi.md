---
title: Reading Assembly and Register ABI
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
conference: cppcon
conference_year: 2025
talk_title: 'C++: Some Assembly Required'
speaker: Matt Godbolt
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 17
- 20
chapter: 2
order: 2
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md
  source_hash: f72bb0aec38545fc3b89c84775652133ee2e5d0e2e401a61e5d984514ad87f16
  translated_at: '2026-05-20T04:37:22.047171+00:00'
  engine: anthropic
  token_count: 5510
---
# Reading Assembly: Building Intuition from Scratch

Faced with a screen full of ``mov``, ``add``, and ``jmp`` alongside a bunch of incomprehensible register names, a beginner's first instinct is usually to close the tab. When a template error pops up, we can at least search Stack Overflow, but assembly output looks like gibberish—there's no obvious starting point. However, if we run some targeted experiments using Compiler Explorer<RefLink :id="1" preview="Matt Godbolt, Compiler Explorer, godbolt.org, 2012" />, we quickly find that assembly can actually be "half-read, half-guessed." We don't need to know how to write it.

## Setting the Stage

All the experiments below were done on Compiler Explorer (godbolt.org). For compilers, we used GCC 16.1.1 for x86-64, the aarch64 version of GCC 16.1.1 for ARM64, and the riscv64 version of GCC 16.1.1 for RISC-V. The operating system is consistently set to Linux, because the calling convention on Windows is different, which leads to differences in the assembly output—we'll discuss this in detail later. We primarily look at the ``-O2`` optimization level, occasionally switching to ``-O0`` for comparison, and we'll explain why later.

## Starting with the Simplest Possible Function

To understand what assembly actually looks like across different architectures, we start with the most basic ``square`` function—taking an input integer, multiplying it by itself, and returning the result. The more plain the function, the better it is for observing compiler behavior: the logic is simple, the assembly is short, and the purpose of every single instruction is obvious.

```cpp
int square(int x) {
    return x * x;
}
```

Intuitively, regardless of the CPU architecture, since we're doing the exact same thing, the compiled assembly should look more or less the same. But when we place the three architectures side by side in Compiler Explorer, we find they look completely different—the instruction formats, register names, and even the way multiplication is implemented are all different. But upon closer observation, a key pattern emerges: although they look different, the skeleton is actually the same—fetch the parameter from somewhere, perform the operation, and put the result in an agreed-upon location to return. Once we understand this skeleton, reading assembly is no longer intimidating.

## The x86-64 Version

Let's look at x86-64 first, since most development machines run this architecture. Under ``-O2`` optimization, GCC generates the following code:

```asm
square(int):
        imul    edi, edi
        mov     eax, edi
        ret
```

Seeing this code for the first time might leave us wondering: shouldn't the parameter be on the stack? Why is it being read directly from ``edi``? This is dictated by the System V AMD64 ABI<RefLink :id="2" preview="System V Application Binary Interface, AMD64 Architecture, x86-64 psABI" /> (the calling convention for x86-64 on Linux)—the first few integer parameters of a function are passed via registers, with the first parameter in ``edi`` and the return value in ``eax``. So the meaning of these three instructions is quite clear: ``imul edi, edi`` is the two-operand form of x86 multiplication—the left operand is both the source and the destination, multiplying the value in ``edi`` by itself and writing the result back to ``edi``. Then it moves the result to ``eax`` as the return value, and finally ``ret`` returns.

A natural question follows: why not let the result of ``imul`` land directly in ``eax``, avoiding the extra ``mov``? In reality, the two-operand form of ``imul`` writes the result back to the first operand (which is ``edi``), and the calling convention requires the return value to be in ``eax``, so that ``mov`` is unavoidable. If we let the compiler use ``imul eax, edi`` (multiplying ``edi`` into ``eax``), we could indeed eliminate ``mov``, but that would require moving ``edi`` into ``eax`` before doing the multiplication. The instruction count would be the same, and GCC chose the former strategy.

Another easy pitfall: if we compile the same code on Windows, the parameter would be in ``ecx`` instead of ``edi``, though the return value would still be in ``eax``. This is one of the biggest differences between Windows x64<RefLink :id="3" preview="Microsoft, x64 Calling Convention, RCX/RDX/R8/R9" /> and Linux x86-64—different calling conventions. If we understand an assembly snippet on Linux and then compile it with MSVC on Windows, we'll find all the registers have changed. We didn't read it wrong; it's just a difference in calling conventions. So when reading assembly, the first step is to confirm the platform and calling convention—this saves a lot of confusion.

## The ARM64 Version

Next, let's look at ARM64, also known as AArch64<RefLink :id="4" preview="ARM, AArch64 Architecture Reference Manual, ARMv8" />. For the same function, GCC aarch64 at ``-O2`` produces this output:

```asm
square(int):
        mul     w0, w0, w0
        ret
```

This code has only two instructions, even cleaner than x86-64. ``w0`` is the register in ARM64 that holds the first integer parameter and the return value (the 32-bit version; the 64-bit version is called ``x0``). Because the parameter is ``int``, 32 bits are sufficient, so the compiler uses the ``w`` register instead of the ``x`` register. The ``mul`` instruction directly puts the result of ``w0`` multiplied by ``w0`` back into ``w0``, then returns—no redundant ``mov`` needed. ARM64's instruction design allows the result to be flexibly placed in any of the operand positions.

It's worth noting that ARM64's register naming is much more regular than x86-64's. On the x86-64 side, ``eax``, ``edi``, and ``rsi`` are all different, requiring rote memorization of each register's special purpose. In ARM64, it's simply ``x0`` through ``x30`` plus a stack pointer ``sp``, with the 32-bit versions uniformly getting a ``w`` prefix—very neat. This regular naming convention lowers the barrier to entry—there's no need to memorize a bunch of legacy names; we just need to know that ``x0``/``w0`` are for parameters and return values.

## The RISC-V Version

Finally, we have RISC-V<RefLink :id="5" preview="RISC-V International, RISC-V ISA Specification, 2019" /> (the V stands for the Roman numeral five, so it's pronounced "risk-five"). Its assembly looks like this:

```asm
square(int):
        mul     a0, a0, a0
        ret
```

Wait, this is almost identical to ARM64? It sure is. ``a0`` in RISC-V is the register that holds the first parameter and the return value (``a`` stands for argument). ``mul`` does the multiplication, the result goes back into ``a0``, and then it returns. Two instructions, clean and simple.

As the youngest instruction set architecture, RISC-V's design drew on the lessons of its predecessors. Its integer registers are simply called ``x0`` through ``x31``, and the ABI assigns them aliases: ``a0``-``a7`` are parameter/return value registers, ``t0``-``t6`` are temporary registers, and ``s0``-``s11`` are callee-saved registers. What we see in assembly are the aliases, but fundamentally they are just ``x`` numbers. This design of "unified underlying numbers + upper-level semantic aliases" is much easier to understand than x86-64's approach where every register has a unique name.

## Looking Back: They're Actually Saying the Same Thing

Placing the three architectures side by side reveals an interesting phenomenon: although the instruction names, register names, and instruction counts are all different, the "semantics" they express are exactly the same—"fetch parameter → multiply → place return value → return." We don't need to recognize every single instruction to read assembly; as long as we grasp which registers the data flows between and what operation is being performed, we can roughly guess what it's doing.

It's like reading a poem written in an unfamiliar language—we don't need to look up every word to feel its rhythm and general meaning from the position and repetition patterns of the words. Assembly is the same way—seeing ``mul`` or ``imul`` tells us a multiplication is happening, seeing ``ret`` tells us the function is about to return, and seeing data move from one register to another tells us something is being passed along. This ability to "half-read, half-guess" is far more practical than rote memorization of every instruction's exact semantics.

## A Crucial Reminder: Optimization Level Completely Changes What You See

Everything shown above was output under ``-O2``. If we turn off optimization (``-O0``), we see a completely different picture—massive amounts of ``push``, ``pop``, memory reads and writes, with parameters being stored to the stack and read back, and intermediate results being repeatedly written to memory. The reason ``-O0`` assembly is so verbose is that ``-O0`` aims to allow debuggers to precisely map every C++ statement to assembly instructions, so it performs no optimization at all, faithfully placing all variables in memory. ``-O2`` is what the compiler "really" wants to generate. If our goal is to understand the compiler's optimization behavior and the actual performance of the code, we must look at the output from ``-O2`` or higher optimization levels; ``-O0`` will only lead us astray.

At this point, we've gone through the assembly of the simplest function across three mainstream architectures. Even though it's just a ``square`` function, it helped us establish an important cognitive framework: knowing where parameters come from, where results go, and which instruction performs the core operation. With this framework in place, we won't be completely lost when looking at more complex function assembly later. Now, let's take this foundation and look at some more realistic scenarios.

---

# What Exactly Is the Relationship Between Machine Code and Assembly

Many people use "machine code" and "assembly code" interchangeably, figuring they're both just incomprehensible stuff anyway. But if we look closely at objdump output, the column of ``0f af ff`` on the left and the column of ``imul edi, edi`` on the right actually have a very straightforward one-to-one mapping relationship—it's just something we rarely think about.

## Clarifying the Concepts: Machine Code Is for Machines, Assembly Is for Humans

That pile of hex numbers on the left—``0f``, ``af``, ``ff``, and the like—is machine code. In essence, it's a string of bytes in memory. The CPU reads these bytes directly and interprets them according to rules hardwired into the hardware: when it reads ``0f af``, it knows this is a multiplication instruction, and the following bytes tell it where the operands are. The CPU doesn't know what ``imul`` is; it only understands numbers.

The column of ``imul edi, edi`` on the right is the assembly code, the human-readable version. It has an essentially one-to-one mapping with machine code—one assembly instruction corresponds to a fixed-format sequence of machine code bytes. So we can "assemble" assembly code into machine code (which is what an assembler does), and we can "disassemble" machine code back into assembly code (which is what tools like objdump and IDA do). Of course, when disassembling back, the comments are gone, the variable names are gone, and semantic information like ``int x = n * n`` is completely lost—all that remains are cold, hard instructions.

But this bidirectional conversion path exists, and it's very direct. Assembly is not a "high-level language" that requires a compiler to do complex translation—it's almost just another way of writing machine code.

## Writing the Simplest Square Function by Hand to See What the Assembly Looks Like

To figure out the register situation, let's start with the most plain square function:

```cpp
// square.cpp
int square(int n) {
    return n * n;
}
```

Then compile it with gcc into an object file, without linking, just to look at the assembly:

```bash
# 我的环境：Arch Linux WSL, x86-64, gcc 16.1.1
g++ -c -O0 square.cpp -o square.o
objdump -d -M intel square.o
```

We add ``-M intel`` because AT&T syntax (where operands come after the instruction and have ``%`` prefixes) isn't very intuitive; Intel syntax at least has operand orders that match our intuition. ``-O0`` turns off all optimization so the compiler won't rewrite the code in any way, letting us see the most raw translation result.

The output looks roughly like this (GCC 16, -O0):

```asm
0000000000000000 <_Z6squarei>:
   0:   55                      push   rbp
   1:   48 89 e5                mov    rbp,rsp
   4:   89 7d fc                mov    DWORD PTR [rbp-0x4],edi
   7:   8b 45 fc                mov    eax,DWORD PTR [rbp-0x4]
   a:   0f af c0                imul   eax,eax
   d:   5d                      pop    rbp
   e:   c3                      ret
```

Our first reaction upon seeing this might be: wait, shouldn't the input parameter be "passed in" from somewhere? A C++ function has a parameter list, but assembly has no such thing. Where did the parameter go?

## Registers Are the CPU's Built-in "Global Variables," But Their Use Has Rules

Inside the CPU, there is a small batch of extremely fast storage units called registers. We can think of them as a kind of "ultra-high-speed global variable"—located directly inside the CPU, requiring no memory access, with reads and writes that are nearly zero-latency. But unlike global variables, the number of registers is extremely limited; on x86-64, there are only a dozen or so general-purpose registers (RAX, RBX, RCX, RDX, RSI, RDI, R8-R15), making it impossible to fit all data into them.

The key question is: who dictates which register does what? If compiler A thinks parameters should go in RAX, and compiler B thinks they should go in RDI, then code compiled by the two couldn't call each other at all. We write a library, someone else writes a program, and because the register usage is inconsistent, the call would fail.

So there must be a set of "traffic rules" that everyone follows, so that code can interoperate. This set of rules is the ABI (Application Binary Interface). The ABI specifies many things, but the most fundamental rule from the perspective of reading assembly is this: during a function call, which register holds the parameters, which register holds the return value, which registers can be freely modified after the call, and which must be restored to their original state.

Linux uses the System V AMD64 ABI, while Windows uses Microsoft's own x64 ABI—the two sets of rules are different. This is one of the reasons why Linux and Windows binaries can't be directly mixed (there are of course more reasons, but the difference in register conventions is the most direct layer).

## Parameters Come In Through EDI, Results Must Go Out Through EAX

Returning to our square function. Under the System V ABI rules, the first integer parameter is placed in the RDI register. Note that we wrote RDI (64-bit), but our parameter is ``int``, which is only 32 bits, so we actually use the lower 32 bits of RDI, which is EDI. The same logic applies to RAX/EAX: RAX is the 64-bit version, and EAX is the 32-bit version.

So when the function is entered, the value of ``n`` is already in EDI—we don't need to "fetch" it from anywhere; it's already there.

Then look at the instruction sequence: ``push rbp; mov rbp, rsp`` is the standard stack frame setup process, and ``mov DWORD PTR [rbp-0x4], edi`` stores the parameter from EDI onto the stack—this is typical ``-O0`` behavior, where the compiler performs no optimization and faithfully places all variables in memory. Next, ``mov eax, DWORD PTR [rbp-0x4]`` reads it back from the stack into EAX, ``imul eax, eax`` does the squaring, ``pop rbp`` restores the stack frame, and finally ``ret`` returns. The verbosity of ``-O0`` perfectly illustrates why we recommended looking at ``-O2`` output earlier—with three extra stack frame manipulation instructions, the core logic gets buried.

Then ``imul eax, eax`` multiplies EAX by EAX, storing the result back in EAX. This is a very distinctive feature of x86: most instructions only accept two operands, and the left operand is both the source and the destination. This is the same idea as ``a *= a`` in C++—read the value on the left, perform an operation with the value on the right, and write the result back to the left. It's a "destructive" operation; once it's done, the original value on the left is overwritten. If we need the original value later, we have to save it beforehand.

Finally, ``ret`` is the return, handing control back to the caller. At this point, EAX holds the squared result, and the caller knows to grab it from EAX—because that's what the ABI dictates.

## Register Names Are Not Arbitrary

Beginners seeing a bunch of names like RAX, EAX, AX, and AL might easily think they are different registers. In reality, they are different "views" of the same physical register: RAX is the full 64 bits, EAX is the lower 32 bits, AX is the lower 16 bits, and AL is the lowest 8 bits. Writing data to EAX overwrites the upper 32 bits of RAX (zeroing them), while writing to AL only changes the lowest byte, leaving the remaining bits unaffected.

This characteristic is particularly prone to causing confusion during debugging. When staring at the register window, we might notice that the value of RAX doesn't match EAX, and wonder if the debugger is glitching. In reality, it's because some instruction only modified the lower 32 bits, and the upper 32 bits are dirty data left over from a previous operation. So when looking at registers, we must be clear about which "view" we're currently looking at.

At this point, the assembly face of the simplest C++ function on x86-64 is clear: parameters are passed in via registers (not the stack—at least the first few parameters aren't), computation happens between registers, and results are returned via registers. The entire process involves no memory access and is extremely fast. Of course, this is just the simplest case; things get much more complicated when there are more parameters, local variables, or when optimization is enabled, but the basic framework remains the same.

---

# Understanding Register Parameter Passing from a Single MOV Instruction: ARM and RISC-V Calling Conventions

In the previous section, we looked at that square function, and after compilation, the core was just a single multiplication instruction. When the function returns, control goes back to the caller. The caller had previously stuffed the parameter into the EDI register (per the x86-64 calling convention), and now it expects to get the return value from the EAX register—this is the x86-64 rule: integer return values go through EAX (or RAX). So what that ``imul edi, edi`` does is very straightforward: it multiplies the value in EDI by itself, writes the result back to EDI, then moves it to EAX, and finally returns. The caller grabs it from EAX, and we're done.

So the question arises: how big is the "felt" difference when doing the same thing on different architectures? If we compile the same function on all three architectures and compare the assembly instruction by instruction, the differences are very obvious.

## The Cleanliness of ARM64

Let's look at ARM64 (AArch64) first. Some people might think ARM assembly is similar to x86, just with different instruction names. But actually opening objdump reveals that the differences far exceed expectations.

```cpp
// square.cpp —— 就这么个简单函数
int square(int value) {
    return value * value;
}
```

Run it with a cross-compilation toolchain:

```bash
# ARM64
aarch64-linux-gnu-g++ -O2 -c square.cpp -o square_arm64.o
aarch64-linux-gnu-objdump -d square_arm64.o
```

The output looks like this:

```asm
square:
    mul w0, w0, w0
    ret
```

That's it. Two instructions, clean as a whistle. One particularly nice thing is that W0 is both the input and the output. In ARM's calling convention, W0 (32-bit) or X0 (64-bit) serves as both the carrier for the first parameter and the return value. So ``mul w0, w0, w0`` reads as "multiply w0 by w0, put the result back in w0"—all three operands are the exact same register, visually extremely uniform.

Next, let's look at the machine code for these instructions, which reveals an important design difference.

```bash
aarch64-linux-gnu-objdump -d -j .text square_arm64.o | grep mul
# 0:   1b007c00    mul w0, w0, w0
```

``1b007c00``, four bytes. Now look at that ``ret``:

```asm
# 4:   d65f03c0    ret
```

``d65f03c0``, also four bytes. Two instructions, both exactly four bytes. This means the instruction decoder's job is particularly simple: the instruction fetch stage always fetches four bytes at a time, without needing to do any length determination. The elegance of this design becomes even more apparent when contrasted with x86.

## x86's Variable-Length Instructions

The same function, compiled for x86-64:

```bash
g++ -O2 -c square.cpp -o square_x64.o
objdump -d square_x64.o
```

```asm
square(int):
    0:   0f af ff                imul   edi,edi
    3:   89 f8                   mov    eax,edi
    6:   c3                      ret
```

The focus here is on the byte length of the instructions:

- ``imul`` instruction: ``0f af ff``, three bytes
- ``mov`` instruction: ``89 f8``, two bytes
- ``ret`` instruction: ``c3``, one byte

Three instructions, three different lengths: 3, 2, 1. If we use a different multiplication variant, like ``imul eax, edi``, its machine code is ``0f af c7``, still three bytes, but with a different suffix than the imul above (``ff`` vs ``c7``) because the operand encoding is different. Change the scenario again, and if the multiplier is an immediate value, the instruction length changes yet again.

"Variable-length instructions" aren't just a textbook concept. Counting bytes against a hex dump reveals that every time the CPU's front end fetches an instruction, it has to read the first few bytes to determine exactly how long that instruction is, and only then can it decide where the next instruction starts. x86's decoder is notoriously complex, and Intel has stuffed massive amounts of pre-decode logic and micro-op caches into its CPUs to solve this problem—essentially using hardware brute force to compensate for the historical baggage of the instruction set design.

## RISC-V's Fixed-Length Instructions

Now let's look at RISC-V (rv64gc):

```bash
riscv64-linux-gnu-g++ -O2 -c square.cpp -o square_rv64.o
riscv64-linux-gnu-objdump -d square_rv64.o
```

```asm
square:
    0:   02b50533    mul a0, a0, a0
    4:   8082        ret
```

Just like ARM, a0 is both the first parameter and the return value, and the ``mul a0, a0, a0`` semantics are completely identical. However, there's a detail here: the ``mul`` instruction is four bytes (``02b50533``), but the ``ret`` instruction is only two bytes (``8082``). RISC-V's base instructions are fixed-length four-byte, but it supports the 16-bit compressed instruction extension (RVC), so common instructions like ``ret`` get compressed into two bytes. This can be considered a compromise between fixed-length and variable-length—still much more disciplined than x86's "completely unpredictable" variable-length approach.

## Operand Count: Not All Instructions Are So Neat

At this point, we might think that instructions are just "opcode + a few operands"—pretty neat and tidy. But after looking through more assembly, we find that reality is far from this rosy.

The ``mul`` and ``imul`` we saw above are typical three-operand instructions (destination + source1 + source2), or two-operand instructions (where the destination is also source1). But there are many instructions that don't follow this pattern at all. Zero-operand instructions are the simplest, like ``ret`` and ``nop``, which don't need any additional information. Single-operand instructions are also common, like various jump instructions. We just saw double-operand and three-operand instructions.

But what's truly confusing is "implicit operands." For example, x86 has an instruction called ``rep stosb``, which "repeatedly writes the value of the AL register to the memory pointed to by RDI (or EDI), automatically incrementing RDI/EDI after each write, with the repeat count controlled by RCX (or ECX)." AL, RDI/EDI, RCX/ECX—not a single one of these three operands is visible in the instruction text; they are all implicit, hardcoded into the instruction definition. Anyone reading the assembly must memorize which registers this instruction uses by default. The "operand count" of such an instruction is actually very hard to define.

## Intel's Historical Baggage

The problem of implicit operands makes x86 a veritable "disaster zone." The reason isn't complicated: the x86 instruction set started with the 8086 in 1978 and evolved all the way to today's x86-64, spanning over 40 years. Each new generation of CPU had to add new things on top of the old instruction set, and it had to maintain backward compatibility—8086 machine code written in 1985 will still run perfectly on a CPU in 2026. This constraint sounds wonderful, but the price is that the instruction set became increasingly bloated and irregular. The encoding space for new instructions was already occupied by old instructions, so they could only use various prefix bytes to extend it, leading to increasingly complex decoding logic.

Does this situation sound familiar? C++'s backward compatibility issues are almost exactly the same—when writing C++26 code today, the compiler still has to handle C89-style declarations, C-style casts, and various legacy features. Every time someone proposes "let's remove some old feature," the answer is always "no, it would break existing code." So we carry this baggage and keep moving forward.

By comparison, ARM and RISC-V are much cleaner. ARM64 was designed around 2011 (AArch64), making it a "clean-room implementation"—not burdened with 32-bit ARM's historical legacy, it redesigned a completely new instruction encoding. RISC-V is even more of an academic project started from scratch in 2010, with excellent instruction orthogonality: the same opcode format can be used just by changing the register number, without any maddening rules like "this instruction implicitly uses EAX, that instruction implicitly uses EDX."

## Register Naming: The Origin of the A Register

We've been talking about names like EAX, W0, and a0, but have you ever wondered why x86 registers have these strange names? There is historical meaning behind these names.

In x86, there is a register called A (Accumulator). In the 8080 era or even earlier with the 8008, the A register was simply "the default register"—many operations defaulted to acting on A, without needing to specify it in the instruction. For example, the instruction encoding for "add a certain value to A" was shorter than "add a certain value to B," because A was the "default destination," saving the few bits needed to specify the destination register.

This design philosophy carried through all the way to x86. Today, when we write ``imul edi, edi``, if we change it to ``imul ebx, ebx``, the machine code might be longer (depending on the specific encoding), because EAX (or rather RAX) is still a "privileged register" in many instructions—it's the default target for implicit instructions and a fixed participant in certain special operations (like the high-order bits of ``mul``'s double-precision result being placed in EDX).

Many tutorials always say "try to use EAX." This isn't some mystical optimization trick; it's a "privilege" granted at the instruction set encoding level—using the A register might make instructions shorter and decoding faster. Of course, on modern CPUs, this difference has been largely smoothed over by various microarchitectural optimizations, but once we understand this background, those instructions with implicit operands no longer seem so baffling.

At this point, we've walked through "what a simple function call actually looks like at the assembly level" from start to finish: from how parameters are passed and return values are placed, to the instruction encoding differences across architectures, to the historical origins of register naming. None of the individual steps are complicated, but when we look at them all together, the entire system connects.

---

---

# Figuring Out Where Parameters Go During Function Calls—From Register Naming to the ABI

When looking at assembly code generated by Compiler Explorer, the biggest psychological barrier is often not the instructions themselves, but those messy register names. RAX, EAX, AX, AL, AH—are these one thing or four things? Once we clarify x86's register layout, this problem is easily solved.

## First, Clarify the Relationship Between RAX, EAX, and AX

Going back to the most fundamental question: what is a register? We can think of it as a small row of ultra-high-speed storage slots inside the CPU, extremely limited in number. In the 8-bit era, the most core register was called the A register, short for Accumulator, around which most arithmetic operations revolved. Later, as CPUs evolved from 8-bit to 16-bit, 32-bit, and 64-bit, this register's width grew along with them, but its "status" never changed—it remained the general-purpose register bearing the primary computational workload.

The key point is this: when we see RAX, we're looking at a 64-bit value. But when we see EAX, we're not looking at another register—we're looking at **the lower 32 bits of the same register**. Similarly, AX is the lower 16 bits, AL is the lowest 8 bits, and AH is the second-to-lowest 8 bits (that is, bits 8-15). They all point to the exact same physical storage, just "slicing" it with different names.

Let's use a simple diagram to illustrate:

```text
63                              31        15  7    0
+--------------------------------+----------+----+----+
|              RAX               |   EAX    | AX      |
|                                |          +----+----+
|                                |          | AH | AL |
+--------------------------------+----------+----+----+
```

So when we see code like this in assembly, there's no need to panic:

```asm
mov rax, rdi      ; 把 64 位参数放进 rax 做计算
shr rax, 32       ; 右移 32 位
mov eax, eax      ; 只保留低 32 位作为返回值
```

Here, switching from rax to eax isn't data being moved back and forth between two registers; it's the compiler saying "the calculation is done, now we only care about the lower 32 bits." Type information from the C++ source code (for example, if the parameter is int64_t but the return value is int32_t) is directly reflected in the assembly's use of different names for the same register. After type information disappears, this is how it "lingers" in the assembly.

## Those Weirdly Named Registers, and Their Easy-to-Remember New Friends

Once we understand the naming pattern of RAX, we might wonder: what about the rest? RAX, RCX, RDX, RSP, RBP, RSI, RDI... these names seem to follow no pattern at all. They are all legacy names inherited from ancient times: A is for Accumulator, C is for Counter, D is for Data, SP is for Stack Pointer, BP is for Base Pointer, and SI and DI are for Source Index and Destination Index, respectively. Knowing the historical background makes them slightly easier to remember, but to a large extent, it still relies on muscle memory built through repeated use.

There is good news, however: when AMD extended the architecture from 32-bit to 64-bit, the eight new general-purpose registers were simply named R8 through R15. Clean and simple. So x86-64 now has a total of 16 general-purpose registers, eight with historically weird names and eight with clean numeric designations.

There are also SIMD/multimedia registers (XMM/YMM/ZMM and such), but those are a whole other topic; today we'll focus on general-purpose registers and function calls.

## Which Register Are Function Parameters Actually In?

One of the biggest confusions when reading assembly is this: we write a function, pass three parameters into it, and the assembly turns into a bunch of mov instructions shuffling data between registers. Where do the parameters actually come from? This involves the ABI (Application Binary Interface).

The ABI specifies many things, but from the perspective of reading assembly, the one thing we care about most is this: **which registers hold the first few parameters of a function**. As long as we know this, we can track what C++ variables have turned into in the assembly.

Take Linux (System V AMD64 ABI) as an example. The first six integer parameters (including pointers) are placed in these registers in order:

```text
第 1 个参数 → RDI
第 2 个参数 → RSI
第 3 个参数 → RDX
第 4 个参数 → RCX
第 5 个参数 → R8
第 6 个参数 → R9
```

Any parameters beyond the first six can only be pushed onto the stack, accessed via stack pointer offsets. When using ``std::forward`` for perfect forwarding, if there are many parameters, we'll see a lot of stack operations in the assembly, because forwarding might "unroll" the parameters, causing the count to suddenly exceed the capacity of the six registers.

Return values are simpler, uniformly placed in RAX (if it's a 128-bit return value, RDX:RAX are combined).

Floating-point parameters are slightly more complex, going through a separate set of registers (XMM0 through XMM7), but the basic idea is the same—the first few go in registers, and any extras go on the stack.

## Windows Has Different Rules

If we use MSVC on Windows, the situation is different. The Windows x64 ABI only provides four registers for passing parameters:

```text
第 1 个参数 → RCX
第 2 个参数 → RDX
第 3 个参数 → R8
第 4 个参数 → R9
```

Note that both the order and the names are different from Linux. This means that for the exact same function, on Linux the first six parameters all go through registers, while on Windows the fifth and sixth parameters already need to be pushed to the stack. When debugging cross-platform performance issues, the exact same C++ code producing completely different assembly on both sides is often caused by ABI differences.

This difference actually has a subtle impact on API design. If we know that only four registers are available on Windows, we'll tend to be more conservative with parameter counts when designing frequently called interfaces. But we'll expand on this topic when we encounter specific scenarios later.

## Let's Verify This Hands-On

Talk is cheap, let's write the simplest function and throw it into Compiler Explorer:

```cpp
// 编译选项：-O1 -m64
// 平台：x86-64 Linux (GCC)

long add_three(long a, long b, long c) {
    return a + b + c;
}
```

The corresponding assembly looks roughly like this (GCC 16, -O1):

```asm
add_three(long, long, long):
    add rdi, rsi           ; rdi(a) += rsi(b)
    lea rax, [rdi + rdx*1] ; rax = rdi + rdx(c)
    ret
```

See? a is in RDI, b is in RSI, c is in RDX—completely in line with the rules we discussed. The return value is in RAX. Clean.

Let's try one with more than six parameters:

```cpp
long sum_seven(long a, long b, long c, long d,
               long e, long f, long g) {
    return a + b + c + d + e + f + g;
}
```

The assembly becomes this:

```asm
sum_seven(long, long, long, long, long, long, long):
    lea rax, [rdi + rsi]       ; a + b
    add rax, rdx               ; + c
    add rax, rcx               ; + d
    add rax, r8                ; + e
    add rax, r9                ; + f
    add rax, QWORD PTR [rsp+8] ; + g，从栈上取！注意偏移 +8，因为 [rsp] 是 call 压入的返回地址
    ret
```

The first six parameters are in RDI, RSI, RDX, RCX, R8, and R9 respectively, and the seventh parameter g ends up on the stack, accessed via ``[rsp+8]`` (the ``call`` instruction pushed the return address into ``[rsp]``, so the first stack parameter requires an 8-byte offset). Once we know the ABI rules, reading assembly is like having a map—it's no longer a screen full of gibberish.

## A Quick Note on ARM64

If we've worked with ARM64 (like Apple Silicon or embedded development), things are much cleaner over there. The general-purpose registers are simply called X0 through X30, with no historical baggage. Function parameters are just X0, X1, X2, and so on, and the return value is in X0. If we want the 32-bit version, we just replace X with W—for example, W0 is the lower 32 bits of X0. The naming logic is the same idea as x86's RAX/EAX, but the names are much easier to
