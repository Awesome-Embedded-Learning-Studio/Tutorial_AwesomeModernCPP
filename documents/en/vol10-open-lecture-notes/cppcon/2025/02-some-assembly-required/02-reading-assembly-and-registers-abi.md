---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 2
platform: host
reading_time_minutes: 38
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: Reading Assembly and Register ABI
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md
  source_hash: bd631e7708036415f7513b79a3d2d35c85e670ebacbfc5864e57a0382bf59701
  translated_at: '2026-06-16T06:09:23.412143+00:00'
  engine: anthropic
  token_count: 5567
---
# Reading Assembly: Building Intuition from Scratch

Faced with a screen full of `mov`, `add`, and `jmp` mixed with indecipherable register names, a beginner's first reaction is often to close the tab. When a template error occurs, we can at least search Stack Overflow, but assembly output looks like gibberish, leaving us unsure where to start. However, by conducting targeted experiments using Compiler Explorer<RefLink :id="1" preview="Matt Godbolt, Compiler Explorer, godbolt.org, 2012" />, we discover that assembly can actually be understood by "reading and guessing"—without truly needing to know how to write it.

## Clarifying the Environment First

All experiments below are performed on Compiler Explorer (godbolt.org). Regarding compilers, we use GCC 16.1.1 for x86-64, the aarch64 version of GCC 16.1.1 for ARM64, and the riscv64 version of GCC 16.1.1 for RISC-V. The operating system is uniformly set to Linux, as calling conventions differ under Windows, leading to variations in assembly output—something we will discuss in detail later. We primarily focus on the `-O2` optimization level, occasionally switching to `-O0` for comparison, for reasons we will explain later.

## Let's Start with the Simplest Function

To understand what assembly actually looks like under different architectures, we start with the simplest `square` function—multiplying an input integer by itself and returning the result. The more plain the function, the better it is for observing compiler behavior, because the logic is simple and the assembly is concise, making the role of every instruction clear at a glance.

```cpp
int square(int x) {
    return x * x;
}
```

Intuitively, regardless of the CPU architecture, since the task is identical, the resulting assembly code should be roughly the same. However, when we place the three architectures side-by-side in Compiler Explorer, we find that they look completely different—the instruction formats, register naming, and even the implementation of multiplication vary. But upon closer inspection, a key pattern emerges: while the "appearance" differs, the skeleton is actually the same—they all retrieve parameters from specific locations, perform operations, and then place the results in agreed-upon locations for return. Once we understand this skeleton, reading assembly code is no longer intimidating.

## The x86-64 Version

Let's start with x86-64, as most development machines run on this architecture. With `-O2` optimization, GCC generates the following code:

```asm
square(int):
        imul    edi, edi
        mov     eax, edi
        ret
```

You might be puzzled when seeing this code for the first time: shouldn't arguments be on the stack? Why are we reading directly from `edi`? This is mandated by the System V AMD64 ABI<RefLink :id="2" preview="System V Application Binary Interface, AMD64 Architecture, x86-64 psABI" /> (the calling convention for x86-64 on Linux)—the first few integer arguments are passed via registers, with the first argument in `edi` and the return value in `eax`. So, the meaning of these three instructions is clear: `imul edi, edi` is the two-operand multiplication form in x86—where the left operand acts as both source and destination. It squares the value in `edi`, writes the result back to `edi`, moves it to `eax` for the return value, and finally returns with `ret`.

A natural question arises: why not let the `imul` result land directly in `eax` and avoid the extra `mov`? In reality, the two-operand form of `imul` writes the result back to the first operand (which is `edi`), and the calling convention requires the return value to be in `eax`, so this `mov` is unavoidable. If the compiler used `imul eax, edi` (multiplying `edi` into `eax`), we could save the `mov`, but that would require moving `edi` to `eax` first before doing the multiplication. The instruction count would be the same, so GCC chose the former strategy.

Another common pitfall: if we compile the same code on Windows, the argument will be in `ecx` instead of `edi`, though the return value remains in `eax`. This is one of the biggest differences between Windows x64<RefLink :id="3" preview="Microsoft, x64 Calling Convention, RCX/RDX/R8/R9" /> and Linux x86-64—the calling conventions differ. You might understand a snippet of assembly on Linux, then compile it with MSVC on Windows and find that all the registers have changed. This isn't a mistake; it's a difference in calling conventions. Therefore, when reading assembly, the first step is to confirm the platform and calling convention. This will save a lot of confusion.

## The ARM64 Version

Next, let's look at ARM64, also known as AArch64<RefLink :id="4" preview="ARM, AArch64 Architecture Reference Manual, ARMv8" />. For the same function, GCC aarch64 with `-O2` produces the following output:

```asm
square(int):
        mul     w0, w0, w0
        ret
```

This code consists of only two instructions, making it even cleaner than x86-64. `w0` is the register in ARM64 that holds the first integer argument and the return value (the 32-bit version; the 64-bit version is called `x0`). Since the parameter is an `int`, 32 bits are sufficient, so the compiler used the `w` register instead of the `x` register. The `mul` instruction directly places the result of `w0` multiplied by `w0` back into `w0` and then returns, with no redundant `mov`—ARM64 instruction design allows the result to be flexibly placed in the position of any operand.

It is worth noting that ARM64 register naming is much more regular than x86-64. In x86-64, `eax`, `edi`, and `rsi` are all distinct, requiring rote memorization of each register's specific purpose; whereas ARM64 simply uses `x0` through `x30` plus a stack pointer `sp`, with the 32-bit versions uniformly adding a `w` prefix, which is very neat. This regular naming convention lowers the barrier to reading—you don't need to memorize a bunch of legacy names, just knowing that `x0`/`w0` is for arguments and return values is enough.

## The RISC-V Version

Finally, we have RISC-V<RefLink :id="5" preview="RISC-V International, RISC-V ISA Specification, 2019" /> (where V represents the Roman numeral five, so it is pronounced "risk-five"). Its assembly looks like this:

```asm
square(int):
        mul     a0, a0, a0
        ret
```

Wait, this looks almost exactly like ARM64? It certainly is. In RISC-V, `a0` is the register designated for the first argument and the return value (the `a` stands for argument). The `mul` instruction performs the multiplication, places the result back into `a0`, and then returns. Two instructions, clean and efficient.

As the youngest instruction set architecture, RISC-V incorporates lessons learned from its predecessors. Its integer registers are simply named `x0` through `x31`, with aliases assigned by the ABI convention: `a0`-`a7` are argument/return registers, `t0`-`t6` are temporary registers, and `s0`-`s11` are callee-saved registers. In assembly, we see the aliases, but fundamentally they are just `x` indices. This design of "unified underlying numbering + semantic upper-layer aliases" is much easier to understand than the x86-64 approach where every register has a unique name.

## Looking Back: They Are Actually Saying the Same Thing

Placing the three architectures side by side reveals an interesting phenomenon: although the instruction names, register names, and instruction counts differ, the "semantics" they express are identical—"fetch argument → multiply → store return value → return." Reading assembly doesn't require recognizing every single instruction; as long as we grasp which registers the data flows between and what operations are performed, we can roughly deduce what the code is doing.

It is like reading a poem written in an unfamiliar language. We don't need to look up every word; we can feel its rhythm and gist through the position of words and repetitive patterns. Assembly is similar: seeing `mul` or `imul` tells us a multiplication is happening; seeing `ret` tells us the function is about to return; seeing data moved from one register to another tells us something is being passed. This ability to "half-read, half-guess" is far more practical than rote memorization of the precise semantics of every instruction.

## A Key Reminder: Optimization Levels Radically Change What You See

The examples above all show output under `-O2`. If optimization is turned off (`-O0`), the picture is completely different—massive amounts of `push`, `pop`, and memory read/write instructions. Parameters are stored to the stack and then read back, and intermediate results are repeatedly written to memory. The assembly at `-O0` is so verbose because its purpose is to allow the debugger to map every C++ statement precisely to assembly instructions. Therefore, it performs no optimizations and keeps all variables strictly in memory. `-O2` represents the code the compiler "truly" wants to generate. If the goal is to understand the compiler's optimization behavior and the actual performance of the code, we must look at `-O2` or higher optimization levels. `-O0` will only lead you astray.

At this point, we have reviewed the assembly of the simplest functions across the three mainstream architectures. Although it is just a `square` function, it establishes an important cognitive framework: knowing where parameters come from, where results go, and in which instruction the core computation is performed. With this framework, we won't be completely lost when looking at more complex function assembly later. Next, equipped with this foundation, we will look at some more realistic scenarios.

---

# What is the Relationship Between Machine Code and Assembly?

Many people use the terms "machine code" and "assembly code" interchangeably, thinking they are just unintelligible gibberish. But if we look closely at the output of objdump, the left column `0f af ff` and the right column `imul edi, edi` actually have a very straightforward one-to-one mapping relationship, though we rarely think about it seriously.

## Clarifying Concepts: Machine Code is for Machines, Assembly is for Humans

That pile of hexadecimal numbers on the left—`0f`, `af`, `ff`, etc.—is machine code. Essentially, it is a string of bytes in memory. The CPU reads these bytes directly and interprets them according to rules hardcoded into the hardware: reading `0f af` tells it this is a multiplication instruction, and subsequent bytes tell it where the operands are. The CPU doesn't know what `imul` is; it only recognizes numbers.

The column on the right, `imul edi, edi`, is assembly code, the version for humans. It has a basically one-to-one mapping relationship with machine code—one assembly instruction corresponds to a fixed-format sequence of machine code bytes. Therefore, we can "assemble" assembly code into machine code (what an assembler does), and we can "disassemble" machine code back into assembly code (what tools like objdump and IDA do). Of course, when disassembling back, comments are lost, variable names are lost, and semantic information like `int x = n * n` is completely gone. All that remains is cold, hard instructions.

However, this bidirectional conversion path exists and is very direct. Assembly is not a "high-level language" requiring a compiler to perform complex translation—it is essentially just another way to write machine code.

## Writing a Simple Square Function to See What Assembly Looks Like

To clarify the register situation, let's start with the most naive square function:

```cpp
// square.cpp
int square(int n) {
    return n * n;
}
```

Then we use GCC to compile into an object file without linking, to inspect the assembly:

```bash
# 我的环境：Arch Linux WSL, x86-64, gcc 16.1.1
g++ -c -O0 square.cpp -o square.o
objdump -d -M intel square.o
```

We add `-M intel` because AT&T syntax (where operands come after the instruction and use the `%` prefix) is not very intuitive. Intel syntax, at the very least, keeps the operand order consistent with our intuition. We use `-O0` to disable all optimizations, ensuring the compiler does not rewrite the code, so we can see the raw translation results.

The output looks something like this (GCC 16, -O0):

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

Your first reaction might be: wait, shouldn't input parameters be "passed in" from somewhere? C++ functions have parameter lists, but assembly has no such thing. So, where exactly do the parameters go?

## Registers are the CPU's built-in "global variables," but there are rules for using them

Inside a CPU, there is a small batch of extremely fast storage units called registers. You can think of them as a kind of "ultra-fast global variable"—located directly inside the CPU, requiring no memory access, with read and write speeds that are virtually zero-latency. However, unlike global variables, registers are extremely limited in quantity. On x86-64, there are only a dozen or so general-purpose registers (RAX, RBX, RCX, RDX, RSI, RDI, R8-R15), making it impossible to stuff all data into them.

The key question is: who dictates which register does what job? If Compiler A decides to put parameters in RAX, and Compiler B decides to put them in RDI, the code they generate cannot call each other. You write a library, someone else writes a program, and because register usage differs, the call fails.

Therefore, there must be a set of "traffic rules" that everyone follows so that code can interoperate. This set of rules is the ABI (Application Binary Interface). The ABI specifies many things, the most basic of which is: during a function call, which register holds parameters, which register holds the return value, which registers can be freely modified after the call, and which must be returned to their original state.

Linux uses the System V AMD64 ABI, while Windows uses its own Microsoft x64 ABI. The two sets of rules are different. This is one of the reasons why binaries for Linux and Windows cannot be directly mixed (of course, there are more reasons, but different register conventions are the most immediate layer).

## Parameters enter via EDI, results must exit via EAX

Let's return to our square function. Under the System V ABI rules, the first integer parameter is placed in the RDI register. Note that I wrote RDI (64-bit), but our parameter is `int`, which is only 32 bits, so we actually use the lower 32 bits of RDI, which is EDI. The same applies to RAX/EAX: RAX is the 64-bit version, and EAX is the 32-bit version.

So, when the function starts, the value of `n` is already in EDI. You don't need to "fetch" it from anywhere; it's already there.

Then look at the instruction sequence: `push rbp; mov rbp, rsp` is the standard stack frame setup process. `mov DWORD PTR [rbp-0x4], edi` stores the parameter from EDI onto the stack—this is typical behavior for `-O0`. The compiler performs no optimization and dutifully places all variables in memory. Next, `mov eax, DWORD PTR [rbp-0x4]` reads it back from the stack into EAX, `imul eax, eax` performs the squaring, `pop rbp` restores the stack frame, and finally `ret` returns. The verbosity of `-O0` precisely explains why the previous section recommended looking at `-O2` output—three extra stack frame instructions drown out the core logic.

Next, `imul eax, eax` multiplies EAX by EAX and stores the result back in EAX. This is a distinctive design feature of x86: most instructions only accept two operands, and the left operand is both the source and the destination. This is equivalent to `a *= a` in C++—read the value on the left, calculate with the value on the right, and write the result back to the left. It is a "destructive" operation; once done, the original value on the left is overwritten. If you need the original value later, you must save it beforehand.

Finally, `ret` returns, handing control back to the caller. At this point, EAX holds the squared result, and the caller knows to fetch it from EAX—because the ABI mandates it.

## Register names are not arbitrary

Beginners seeing RAX, EAX, AX, and AL often assume they are different registers. In reality, they are different "views" of the same physical register: RAX is the full 64 bits, EAX is the lower 32 bits, AX is the lower 16 bits, and AL is the lowest 8 bits. Writing to EAX overwrites the high 32 bits of RAX (zeroing them), while writing to AL only changes the lowest byte, leaving the rest unaffected.

This characteristic can cause confusion during debugging. When staring at a register window, you might notice that the value of RAX doesn't match EAX and suspect the debugger is glitching. Actually, it's because a previous instruction only modified the lower 32 bits, and the high 32 bits are "dirty data" left over from an earlier operation. So, when viewing registers, be sure to clarify which "view" you are looking at.

At this point, the assembly face of a simple C++ function on x86-64 is clear: parameters are passed in via registers (not the stack, at least for the first few), calculations happen between registers, and results are returned via registers. The whole process involves no memory access and is extremely fast. Of course, this is the simplest case; with more parameters, local variables, and optimizations enabled, things get much more complex, but the basic framework remains the same.

---

# Understanding Register Passing via a Single MOV Instruction: Calling Conventions in ARM and RISC-V

The previous section discussed a function that calculates a square. After compilation, the core is a single multiplication instruction. When the function returns, control goes back to the caller. The caller previously stuffed the parameter into the EDI register (the x86-64 calling convention) and now expects to retrieve the return value from the EAX register—this is the rule for x86-64: integer return values go via EAX (or RAX). So, that `imul edi, edi` does something very straightforward: multiply the value in EDI by itself, write the result back to EDI, then `mov` it to EAX, and finally `ret`. The caller grabs it from EAX, and we're done.

So the question arises: how big is the "perceptual" difference for the same task across different architectures? Compiling the same function under three architectures and comparing the assembly line by line reveals very distinct differences.

## The Simplicity of ARM64

Let's look at ARM64 (AArch64) first. Some might assume ARM assembly is similar to x86, just with different instruction names. But actually opening objdump reveals differences far beyond expectations.

```cpp
// square.cpp —— 就这么个简单函数
int square(int value) {
    return value * value;
}
```

Let's run this using the cross-compilation toolchain:

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

That's it. Two instructions, clean and simple. One particularly elegant aspect is that W0 serves as both the input and the output. In the ARM calling convention, W0 (32-bit) or X0 (64-bit) acts as the carrier for both the first argument and the return value. Therefore, `mul w0, w0, w0` reads as "multiply w0 by w0 and put the result back in w0." Since all three operands are the same register, it is visually very consistent.

Next, let's examine the machine code for these instructions, which reveals a key design difference.

```bash
aarch64-linux-gnu-objdump -d -j .text square_arm64.o | grep mul
# 0:   1b007c00    mul w0, w0, w0
```

`1b007c00`, four bytes. Now, let's look at that `ret`:

```asm
# 4:   d65f03c0    ret
```

`d65f03c0`, which is also four bytes. Both instructions are exactly four bytes in length. This means the instruction decoder's job is particularly simple; the fetch stage simply fetches a fixed four bytes at a time, without needing to perform any length checks. The elegance of this design becomes even more apparent when we compare it to x86.

## Variable-Length Instructions in x86

Compiling the same function for x86-64:

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

The key point here is the byte length of the instructions:

- `imul` instruction: `0f af ff`, three bytes
- `mov` instruction: `89 f8`, two bytes
- `ret` instruction: `c3`, one byte

Three instructions, three different lengths: 3, 2, and 1. If we change the multiplication syntax, for example to `imul eax, edi`, the machine code becomes `0f af c7`. It is still three bytes long, but the suffix differs from the previous `imul` instruction (`ff` vs `c7`) due to different operand encoding. If we switch to another scenario, such as using an immediate number as the multiplier, the instruction length changes again.

"Variable-length instructions" are not just a textbook concept. If we count bytes in a hex dump, we discover that the CPU's front-end must read the first few bytes of every instruction to determine its actual length before it can decide where the next instruction begins. The x86 decoder is notoriously complex. To solve this problem, Intel packed the CPU with extensive pre-decoding logic and a micro-op cache, essentially using brute-force hardware to compensate for the historical baggage of the instruction set design.

## RISC-V Fixed-Length Instructions

Let's look at RISC-V (rv64gc):

```bash
riscv64-linux-gnu-g++ -O2 -c square.cpp -o square_rv64.o
riscv64-linux-gnu-objdump -d square_rv64.o
```

```asm
square:
    0:   02b50533    mul a0, a0, a0
    4:   8082        ret
```

Just like with ARM, `a0` serves as both the first parameter and the return value, so the semantics of `mul a0, a0, a0` are identical. However, there is a detail here: the `mul` instruction is four bytes (`02b50533`), whereas the `ret` instruction is only two bytes (`8082`). The base RISC-V instructions are fixed-length four bytes, but the architecture supports the 16-bit Compressed Extension (RVC), so common instructions like `ret` are compressed into two bytes. This represents a compromise between fixed-length and variable-length encoding, making it much more predictable than the "totally unpredictable" variable length of x86.

## Number of Operands: Not All Instructions Are So Regular

At this point, you might think that instructions are just "opcode + a few operands," which seems quite neat. However, looking at more assembly reveals that reality is far less beautiful.

The `mul` and `imul` instructions we saw earlier are typical three-operand instructions (destination + source1 + source2), or two-operand instructions (where the destination is also source1). But many instructions don't follow this pattern at all. Zero-operand instructions are the simplest, like `ret` and `nop`, which require no extra information. Single-operand instructions are also common, such as various jump instructions. We just looked at double and triple-operand instructions.

What is truly confusing, however, is "implicit operands." For example, in x86 there is a `rep stosb` instruction. Its function is to "repeatedly write the value in the AL register to the memory pointed to by RDI (or EDI), incrementing RDI/EDI automatically after each write, with the repeat count controlled by RCX (or ECX)." AL, RDI/EDI, RCX/ECX—you don't see any of these three operands in the instruction text; they are all implicit, hardcoded into the instruction definition. Anyone reading the assembly must remember which registers this instruction uses by default. The "number of operands" for such instructions is actually quite hard to define.

## Intel's Historical Baggage

The problem of implicit operands makes x86 a "heavyweight" zone. The reason isn't complicated: the x86 instruction set evolved from the 8086 in 1978 to today's x86-64, spanning over 40 years. Each generation of new CPUs had to add new features on top of the old instruction set while maintaining backward compatibility—machine code written for an 8086 in 1985 will still run on a CPU in 2026. This constraint sounds wonderful, but the cost is that the instruction set has become increasingly bloated and irregular. The encoding space for new instructions is occupied by old instructions, so various prefix bytes must be used for expansion, making decoding logic increasingly complex.

Does this situation sound familiar? C++'s backward compatibility issues are practically identical—when we write C++26 code today, the compiler still has to handle C89-style declarations, C-style casts, and various legacy features. Whenever someone suggests "let's delete this old feature," the answer is always "no, it will break existing code." So, we carry this baggage and move forward.

By comparison, ARM and RISC-V are much cleaner. ARM64 was designed around 2011 (AArch64) and can be considered a "clean room implementation"—it doesn't carry the historical baggage of 32-bit ARM and redesigned a set of instruction encodings. RISC-V is even more of an academic project started from scratch in 2010, with excellent orthogonality in its instructions: the same opcode format can be used by simply changing the register number. There are no maddening rules like "this instruction implicitly uses EAX, that one implicitly uses EDX."

## Register Naming: The Origin of the A Register

We've been talking about names like EAX, W0, and a0, but have you ever thought about why x86 registers have these strange names? There is historical meaning behind these names.

There is a register in x86 called A (Accumulator). In the era of the 8080 or even the earlier 8008, the A register was "the default register"—many operations targeted A by default without needing to specify it in the instruction. For example, an addition instruction encoding for "add a value to A" is shorter than "add a value to B," because A is the "default target," saving the bits needed to specify the destination register.

This design philosophy has continued into x86. Today, writing `imul edi, edi` versus `imul ebx, ebx` might result in longer machine code for the latter (depending on the specific encoding), because EAX (or RAX) remains a "privileged register" in many instructions—it is the implicit default target for many instructions and a fixed participant in certain special operations (for example, the high bits of the double-precision result of `mul` are placed in EDX).

Many tutorials say "try to use EAX." This isn't a mysterious optimization trick; it's a "privilege" granted at the instruction set encoding level—using the A register can make instructions shorter and decoding faster. Of course, on modern CPUs, this difference has been largely smoothed out by various microarchitectural optimizations, but understanding this background makes those implicit operand instructions seem less baffling.

At this point, we have thoroughly gone through "what a simple function call looks like at the assembly level": from how parameters are passed and return values are placed, to instruction encoding differences across architectures, and finally the historical origins of register naming. Each step isn't complicated, but when viewed together, the entire system connects.

---

---

# Understanding Where Function Parameters Go—From Register Naming to ABI

When looking at assembly code generated by Compiler Explorer, the biggest psychological barrier is often not the instructions themselves, but the messy register names. RAX, EAX, AX, AL, AH—are these one thing or four things? Once you understand the x86 register layout, this problem is easily solved.

## First, Clarify the Relationship Between RAX, EAX, and AX

Let's go back to the most fundamental question: What is a register? You can think of it as a small row of ultra-high-speed storage slots inside the CPU, very limited in quantity. In the 8-bit era, the most core register was the A register, or Accumulator, around which most arithmetic operations revolved. Later, CPUs evolved from 8-bit to 16-bit, 32-bit, and 64-bit. The width of this register increased, but its "status" remained unchanged—it is always the general-purpose register bearing the brunt of computational tasks.

The key point is: When you see RAX, you are looking at a 64-bit value. But when you see EAX, you are not looking at another register, but at the **lower 32 bits of the same register**. Similarly, AX is the lower 16 bits, AL is the lowest 8 bits, and AH is the second lowest 8 bits (bits 8-15). They all point to the same physical storage, just "sliced" using different names.

A simple diagram illustrates this:

```text
63                              31        15  7    0
+--------------------------------+----------+----+----+
|              RAX               |   EAX    | AX      |
|                                |          +----+----+
|                                |          | AH | AL |
+--------------------------------+----------+----+----+
```

So, when we see code like this in assembly, there is no need to panic:

```asm
mov rax, rdi      ; 把 64 位参数放进 rax 做计算
shr rax, 32       ; 右移 32 位
mov eax, eax      ; 只保留低 32 位作为返回值
```

Here, we switch from `rax` to `eax`. This isn't about shuffling data between two registers; rather, the compiler is saying, "The calculation is done, and now we only care about the lower 32 bits." Type information from the C++ source code (for example, a parameter being `int64_t` but the return value being `int32_t`) is directly reflected in the assembly by using different names for the same register. Once the high-level type information is stripped away, it "lingers" in the assembly in this manner.

## Those oddly named registers, and some easy-to-remember new friends

Once we understand the naming convention of `RAX`, we might wonder: what about the others? `RAX`, `RCX`, `RDX`, `RSP`, `RBP`, `RSI`, `RDI`... these names seem completely arbitrary. They are all legacy names inherited from ancient times: A for Accumulator, C for Counter, D for Data, SP for Stack Pointer, BP for Base Pointer, and SI/DI for Source and Destination Index. Knowing the historical background makes them slightly easier to remember, but mostly, it relies on muscle memory built through repeated use.

However, there is good news: when AMD extended the architecture from 32-bit to 64-bit, the eight new general-purpose registers were simply named `R8` through `R15`. Clean and simple. So, x86-64 now has a total of 16 general-purpose registers: eight with historically quirky names, and eight with clean numeric designations.

Of course, there are also SIMD/multimedia registers (like `XMM`/`YMM`/`ZMM`), but those are a whole different topic. For now, let's focus on general-purpose registers and function calls.

## Which register holds function arguments?

One of the biggest confusions when reading assembly is this: we write a function and pass three arguments, but the assembly shows a bunch of `mov` instructions shuffling data between registers. Where do these arguments actually come from? This brings us to the ABI (Application Binary Interface).

The ABI specifies many things, but from the perspective of reading assembly, we care most about one thing: **which registers hold the first few function arguments**. Once we know this, we can trace how C++ variables manifest in the assembly.

Take Linux (System V AMD64 ABI) as an example. The first six integer arguments (including pointers) are placed in these registers, in order:

```text
第 1 个参数 → RDI
第 2 个参数 → RSI
第 3 个参数 → RDX
第 4 个参数 → RCX
第 5 个参数 → R8
第 6 个参数 → R9
```

Any parameters beyond the first six must be pushed onto the stack and accessed via stack pointer offsets. When we use `std::forward` for perfect forwarding, if there are many parameters, we will see extensive stack manipulation in the assembly. This is because forwarding may "unroll" the parameters, causing the count to suddenly exceed the capacity of the six registers.

Return values are simpler: they are uniformly placed in RAX (for 128-bit return values, RDX and RAX are combined).

Floating-point parameters are slightly more complex; they use a separate set of registers (XMM0 through XMM7), but the basic logic is the same—the first few go in registers, and the rest go on the stack.

## Windows Rules Are Different

If we use MSVC on Windows, the situation is different. The Windows x64 ABI provides only four registers for passing parameters:

```text
第 1 个参数 → RCX
第 2 个参数 → RDX
第 3 个参数 → R8
第 4 个参数 → R9
```

Note that the order and naming differ from Linux. This means that for the same function, the first six arguments are passed entirely in registers on Linux, whereas on Windows, the fifth and sixth arguments are already pushed onto the stack. When debugging performance issues across platforms, the same C++ code generates completely different assembly on both sides, which is often caused by ABI differences.

This difference actually has a subtle impact on API design. Knowing that only four registers are available on Windows, we tend to be more conservative with the number of parameters when designing high-frequency interfaces. However, we will expand on this topic when we encounter specific scenarios later.

## Let's Verify

Theory without practice is empty. Let's write a simple function and throw it into Compiler Explorer to see:

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

Look, `a` is in `RDI`, `b` is in `RSI`, and `c` is in `RDX`. This perfectly matches the rules we discussed. The return value is in `RAX`. Clean and simple.

Let's try another example with more than six arguments:

```cpp
long sum_seven(long a, long b, long c, long d,
               long e, long f, long g) {
    return a + b + c + d + e + f + g;
}
```

The assembly turns out like this:

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

The first six parameters are in RDI, RSI, RDX, RCX, R8, and R9, while the seventh parameter, `g`, ends up on the stack, accessed via `[rsp+8]` (the `call` instruction pushed the return address onto `[rsp]`, so the first stack parameter requires an offset of 8 bytes). Once we understand the ABI rules, reading assembly feels like having a map; it's no longer a page full of gibberish.

## A Quick Note on ARM64

If you have used ARM64 (such as Apple Silicon or in embedded development), things are much cleaner over there. The general-purpose registers are simply named X0 through X30, without any historical baggage. Function parameters are just X0, X1, X2, and so on, with the return value in X0. If you want to look at the 32-bit version, just replace X with W; for example, W0 is the low 32 bits of X0. The naming logic follows the same思路 as x86's RAX/EAX, but the names are much easier to remember.

At this point, we have thoroughly clarified register naming and parameter passing rules. If you feel confused seeing `rax` one moment and `eax` the next in assembly code, it is simply because you didn't realize they are just accessing different widths of the same register. Once you understand this, things feel much more settled. Next, with this foundation in place, we can look at more complex assembly patterns.

---

# RISC-V Register Naming — From Numbers to Semantics

When reading RISC-V assembly, opening the disassembly window reveals a screen full of `t0`, `a7`, `s1`, and `ra`. It looks similar to the x86 set of `rax`, `rbx`, and `rcx`, appearing to be a bunch of letter abbreviations that require rote memorization. However, once you truly understand it, you will find that RISC-V register naming is not arbitrary abbreviation at all—it directly tells you what the register **is supposed to do**. Once you understand the calling convention semantics behind the naming, you can derive these names yourself.

## Start with the Most Basic Numbering

RISC-V has a total of 32 general-purpose registers, numbered from `x0` to `x31`. Note that there are 32, not 31—`x0` is indeed a register that exists, except it is hardwired to 0. Writing anything to it results in 0, and reading from it always yields 0. This design may seem superfluous at first glance, but when writing inline assembly, you will find that having the constant zero directly available as an operand saves many `mov` instructions.

Then there is the issue of bit width. RISC-V registers are 64-bit (under the RV64G standard), and the numbers `x0` through `x31` correspond to the full 64-bit values. If you only need to operate on the low 16 bits, you can simply use a mask like `0xFFFF` to perform an AND operation; there is no need for separate 16-bit register aliases as in some architectures. This is quite clean, as there is no need to switch back and forth between register names of different widths.

The previous discussion covered `x0` to `x30`, but actually, all 32 registers from `x0` to `x31` must be discussed. Among them, `x1` is special; it is `ra` (Return Address), which will be discussed in detail later. In any case, with 32 registers laid out, it is much more intuitive than the heavily burdened naming scheme of x86-64—x86 general-purpose register names are inherited from the 16-bit era, `rax` is an extension of `a`, and `r8` to `r15` were hard-added later; the entire system lacks any rhyme or reason.

## What Exactly Are Those Aliases?

Here is the key. When actually writing assembly or viewing disassembly output, you will almost never see pure numeric identifiers like `x0` to `x31`. Compilers and disassemblers rename every register, replacing them with semantic names. Seeing a bunch of things starting with `t`, `s`, and `a` might feel like a set of conventions requiring rote memorization, but as long as you understand the calling convention, you can derive these names yourself.

Let's look at a simple example, a RISC-V 64-bit target compiled with GCC 16.1.1:

```cpp
// test.cpp
long add(long a, long b, long c, long d,
         long e, long f, long g, long h, long i) {
    return a + b + c + d + e + f + g + h + i;
}
```

Build command:

```bash
riscv64-linux-gnu-g++ -O1 -S test.cpp -o test.s
```

Let's look at the generated assembly:

```asm
add:
    add a0, a0, a1    # a0 += a1
    add a0, a0, a2    # a0 += a2
    add a0, a0, a3    # a0 += a3
    add a0, a0, a4    # a0 += a4
    add a0, a0, a5    # a0 += a5
    add a0, a0, a6    # a0 += a6
    add a0, a0, a7    # a0 += a7
    ld  a1, 0(sp)     # 第9个参数在栈上，加载到 a1
    add a0, a0, a1    # a0 += 栈上的参数
    ret
```

See? The first eight arguments are placed in `a0` through `a7`, and the return value is also placed in `a0`. The `a` stands for Argument, so `a0` through `a7` are argument registers, while `a0` doubles as the return value register. This is much easier to memorize than the x86 convention of "RDI for the first argument, RSI for the second, RDX for the third."

## T Registers and S Registers — The Core of the Calling Convention

Once we understand the `a` registers, the rest follows logically. Registers starting with `t` are **Temporary** registers, totaling seven from `t0` to `t6` (specific mappings are listed later). Registers starting with `s` are **Saved** (callee-saved) registers, totaling 12 from `s0` to `s11`.

These two concepts are easily confused. A common pitfall is storing an intermediate value in `t0`, calling another function, and finding the value in `t0` has changed upon return, causing the program to crash. This is because `t` registers are caller-saved—**if you store something in `t0` and then call another function, you must save it to the stack beforehand**. The called function is free to use `t0` and makes no guarantees about preserving its value.

`s` registers work the opposite way; they are callee-saved. If a function uses `s1`, it must restore `s1` to the value the caller expects before returning. In other words, the caller can safely store data in `s1`, call other functions, and the value in `s1` is guaranteed to remain when execution returns.

Let's verify this with an intuitive code example:

```cpp
// caller.cpp
extern "C" long callee();

long caller() {
    register long temp __asm__("t0") = 42;
    register long saved __asm__("s1") = 99;
    long result = callee();
    // temp 可能已经被 callee 破坏了
    // saved 一定还是 99
    return temp + saved + result;
}
```

```cpp
// callee.cpp
extern "C" long callee() {
    // 故意写 t0，这是合法的
    register long t0_val __asm__("t0") = 0;
    // 故意写 s1，但必须恢复
    register long s1_val __asm__("s1") = 0;
    __asm__ volatile("" : "=r"(t0_val) : "0"(t0_val));
    __asm__ volatile("" : "=r"(s1_val) : "0"(s1_val));
    return 1;
}
```

After compiling and running this, we will see that the value of `temp` indeed changes upon return in `caller`, while `saved` remains 99. This demonstrates the power of the calling convention.

## Complete Mapping Table

The speaker mentioned he puts a sticky note in the bottom-left corner of his monitor, and many people do the same. However, once we understand the naming logic, there is actually no need to memorize this table—we can derive it if we understand the principles. For convenience, the complete mapping is listed below as a cheat sheet:

| Number | ABI Name | Meaning | Calling Convention |
|--------|----------|---------|--------------------|
| x0   | zero   | Hardwired to zero | — |
| x1   | ra     | Return address | Caller-saved |
| x2   | sp     | Stack pointer | Callee-saved |
| x3   | gp     | Global pointer | — |
| x4   | tp     | Thread pointer | — |
| x5-x7 | t0-t2 | Temporaries | Caller-saved |
| x8   | s0/fp  | Saved register / Frame pointer | Callee-saved |
| x9   | s1     | Saved register | Callee-saved |
| x10-x17 | a0-a7 | Arguments / Return values | Caller-saved |
| x18-x27 | s2-s11 | Saved registers | Callee-saved |
| x28-x31 | t3-t6 | Temporaries | Caller-saved |

`t` stands for temporary—use and discard; `s` stands for saved—must be preserved; `a` stands for arguments; `ra` remembers where we came from; and `sp` manages the stack. Every name tells you its responsibility.

By the way, if we have used 32-bit ARM before, we will notice that ARM only has 16 general-purpose registers (R0-R15), and arguments can only be placed in four registers (R0-R3); any excess goes entirely on the stack. RISC-V has 32 registers, including 8 argument registers, 7 temporary registers, and 12 callee-saved registers. With more registers, the number of push and pop operations during function calls is reduced, resulting in tangible performance benefits.

## Implicit Arguments — The `this` Pointer and Return Value Optimization

At this point, we might think parameter passing is just `a0` through `a7`, which is simple. But there is one easily overlooked issue: for C++ member functions, where is the `this` pointer stored?

The `this` pointer is simply an implicit first parameter. On RISC-V Linux, it is placed in `a0`, the first declared "real" parameter is placed in `a1`, and so on. This is consistent with the convention on x86-64 Linux (where `this` goes in RDI and the first argument goes in RSI).

A simple verification code:

```cpp
struct Foo {
    long x;
    long bar(long y) { return x + y; }
};

// 编译后看汇编，bar 的签名等价于：
// long Foo_bar(Foo* this, long y)
// a0 = this, a1 = y
```

```asm
_ZN3Foo3barEl:
    ld    a0, 0(a0)     # 从 this->x 加载值到 a0
    add   a0, a0, a1    # a0 += y
    ret
```

It is crystal clear that `a0` initially holds the `this` pointer, which is then immediately overwritten by the value of `this->x`, and finally, `y` from `a1` is added before returning.

However, there are even more complex scenarios. If you write code like this:

```cpp
struct Big {
    long data[4];
};

Big make_big(long a, long b) {
    Big result{};
    result.data[0] = a;
    result.data[1] = b;
    return result;
}
```

`Big` is 32 bytes and cannot fit into a single register. When the compiler performs return value optimization (RVO/NRVO), it does not actually construct a `Big` object inside the function and then copy it out. Instead, it reserves space in the **caller's stack frame**, and passes the address of this space as an implicit parameter to the callee. On RISC-V, this implicit parameter is placed in `a0`, while the declared first parameter `a` is shifted to `a1`, and the second parameter `b` is in `a2`.

```asm
_Z9make_bigll:
    # a0 = 隐式的返回值缓冲区地址
    # a1 = a, a2 = b
    sd    a1, 0(a0)     # result.data[0] = a
    sd    a2, 8(a0)     # result.data[1] = b
    sd    zero, 16(a0)  # result.data[2] = 0
    sd    zero, 24(a0)  # result.data[3] = 0
    ret
```

The assembly at the call site looks roughly like this:

```asm
    # 调用者在栈上预留 32 字节
    addi  sp, sp, -32
    mv    a0, sp        # 把缓冲区地址作为第一个参数
    mv    a1, ...       # 真正的参数 a
    mv    a2, ...       # 真正的参数 b
    call  _Z9make_bigll
    # 现在 sp 指向的位置就是构造好的 Big 对象
```

It can be confusing the first time we see this—why are all the arguments in the wrong positions? The reason is that an implicit pointer parameter is inserted at the very beginning. This is something we would never notice without looking at the assembly, but once we encounter it, not understanding it can lead to a full day of debugging.

At this point, we have completely mastered the RISC-V register naming system. Looking back, it wasn't actually that difficult. The key is to understand the calling convention semantics behind each name, rather than rote-memorizing them as meaningless symbols.

---

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Matt Godbolt"
    title="Compiler Explorer"
    publisher="godbolt.org"
    :year="2012"
    url="https://godbolt.org/"
  />
  <ReferenceItem
    :id="2"
    author="AMD / System V"
    title="System V Application Binary Interface, AMD64 Architecture"
    publisher="x86-64 psABI"
    :year="2018"
    chapter="calling convention: RDI, RSI, RDX, RCX, R8, R9 for integer args"
    url="https://gitlab.com/x86-psABIs/x86-64-ABI"
  />
  <ReferenceItem
    :id="3"
    author="Microsoft"
    title="x64 Calling Convention"
    publisher="Microsoft Learn"
    :year="2024"
    chapter="integer args in RCX, RDX, R8, R9"
    url="https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention"
  />
  <ReferenceItem
    :id="4"
    author="ARM"
    title="ARM Architecture Reference Manual, ARMv8 (AArch64)"
    publisher="ARM Ltd"
    :year="2020"
    chapter="X0-X30 registers, procedure call standard"
  />
  <ReferenceItem
    :id="5"
    author="RISC-V International"
    title="RISC-V Instruction Set Manual"
    publisher="RISC-V International"
    :year="2019"
    chapter="RV64G, integer registers x0-x31, ABI names"
    url="https://riscv.org/specifications/"
  />
</ReferenceCard>

---

## Further Reading

- To understand what assembly the compiler actually spits out at different optimization levels (`-O0` / `-O2` / `-O3`), see [Volume 7: Compiler Options](../../../../vol7-engineering/02-compiler-options.md).
- To dive deeper into how SIMD/AVX reshapes assembly output, see [Volume 6: AVX/AVX2 Deep Dive](../../../../vol6-performance/ch04-tuning-by-bottleneck/04-05-simd.md).
