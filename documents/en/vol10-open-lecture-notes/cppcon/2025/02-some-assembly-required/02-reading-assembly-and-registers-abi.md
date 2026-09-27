---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 talk notes — C++: Some Assembly Required by Matt Godbolt'
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
title: Reading Assembly and the Register ABI
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md
  source_hash: 6ced5414af699993bbf643bbbe0d2f11aaca5bc7c94e42dc6172f2400685efb2
  translated_at: '2026-09-26T15:37:27+00:00'
  engine: anthropic
  token_count: 8500
---
# Reading Assembly: Building Intuition from Scratch

Faced with a screen full of `mov`, `add`, and `jmp` mixed with a pile of unreadable register names, a beginner's first reaction is often to close the tab. When a template error fires, at least we can go search Stack Overflow; assembly output, by contrast, looks like scripture in an alien alphabet, with no obvious place to start reading. And yet, with a few targeted experiments on Compiler Explorer<RefLink :id="1" preview="Matt Godbolt, Compiler Explorer, godbolt.org, 2012" />, it turns out that assembly can be understood "half reading, half guessing" — we never actually need to know how to write it.

## First, the Environment

Every experiment below was done on Compiler Explorer (godbolt.org). On the compiler side, x86-64 uses GCC 16.1.1, ARM64 uses the aarch64 build of GCC 16.1.1, and RISC-V uses the riscv64 build of GCC 16.1.1. The operating system is set to Linux across the board, because the calling convention under Windows differs and so does the assembly output — we will come back to that in detail later. For optimization we mostly look at `-O2`, occasionally dropping to `-O0` for comparison; the reason comes up shortly.

## Start with the Simplest Possible Function

To see what assembly actually looks like across architectures, we start with the simplest possible `square` function — take an integer input, multiply it by itself, and return it. The plainer the function, the better it is for observing compiler behavior: the logic is simple, the assembly is short, and every instruction's job is visible at a glance.

```cpp
int square(int x) {
    return x * x;
}
```

Intuitively, whatever the CPU architecture, the job is the same, so the compiled assembly should come out more or less alike. Yet when we line the three architectures up side by side in Compiler Explorer, they look completely different — instruction formats, register names, even the way multiplication is implemented. But look closer and a key pattern surfaces: different as they look, the skeleton is the same — take the argument from some agreed place, do the arithmetic, then put the result in some other agreed place and return. Once that skeleton clicks, reading assembly stops being intimidating.

## The x86-64 Version

x86-64 first, since most development machines run this architecture. Under `-O2`, GCC generates the following code:

```asm
square(int):
        imul    edi, edi
        mov     eax, edi
        ret
```

A first look at this code raises a puzzle: shouldn't arguments live on the stack? Why is the code reading straight from `edi`? That is what the System V AMD64 ABI<RefLink :id="2" preview="System V Application Binary Interface, AMD64 Architecture, x86-64 psABI" /> — the calling convention for x86-64 on Linux — prescribes: the first few integer arguments are passed in registers, with the first argument in `edi` and the return value in `eax`. So the three instructions read plainly: `imul edi, edi` is x86's two-operand multiply form — the left operand is both source and destination; it multiplies the value in `edi` by itself and writes the result back to `edi`; then that value is moved into `eax` as the return value, and finally `ret` returns.

A natural follow-up question: why not let the `imul` result land directly in `eax` and skip the extra `mov`? As it happens, the two-operand form of `imul` writes its result to the first operand (`edi`), while the calling convention demands the return value sit in `eax` — so that `mov` is unavoidable. GCC could have emitted `imul eax, edi` (multiplying `edi` into `eax`), which would drop the `mov`, but then `edi` would first have to be moved into `eax` before the multiply — the instruction count comes out the same, and GCC picked the former strategy.

Another easy trap: compile the same code on Windows and the argument arrives in `ecx` instead of `edi`, though the return value still lands in `eax`. This is one of the biggest differences between Windows x64<RefLink :id="3" preview="Microsoft, x64 Calling Convention, RCX/RDX/R8/R9" /> and Linux x86-64 — a different calling convention. Read a piece of assembly fluently on Linux, then compile the same code with MSVC on Windows, and every register seems to have moved — that is not misreading; it is the ABI difference. So step one when reading assembly: confirm the platform and calling convention first. It saves a lot of confusion.

## The ARM64 Version

Next up, ARM64, also known as AArch64<RefLink :id="4" preview="ARM, AArch64 Architecture Reference Manual, ARMv8" />. For the same function, GCC aarch64 at `-O2` produces this output:

```asm
square(int):
        mul     w0, w0, w0
        ret
```

Two instructions — even cleaner than x86-64. `w0` is the ARM64 register that carries the first integer argument and the return value (the 32-bit view; the 64-bit one is called `x0`). Because the argument is an `int`, 32 bits are enough, so the compiler used a `w` register rather than an `x` register. `mul` puts the product of `w0` times `w0` right back into `w0`, then the function returns — no leftover `mov`, because ARM64's instruction design lets the result land flexibly in any operand slot.

Worth noting: ARM64's register naming is far more regular than x86-64's. Over there, `eax`, `edi`, and `rsi` are all distinct names with no system behind them — each register's special role has to be memorized. ARM64 is simply `x0` through `x30` plus a stack pointer `sp`, and the 32-bit views uniformly take a `w` prefix. Very tidy. That regular naming lowers the barrier — no pile of historically accreted names to memorize; knowing that `x0`/`w0` handles arguments and return values is enough to get started.

## The RISC-V Version

Finally, RISC-V<RefLink :id="5" preview="RISC-V International, RISC-V ISA Specification, 2019" /> (the V is the Roman numeral five, so it is pronounced "risk-five"). Its assembly looks like this:

```asm
square(int):
        mul     a0, a0, a0
        ret
```

Wait — isn't this nearly identical to ARM64? It is. In RISC-V, `a0` is the register that holds the first argument and the return value (`a` stands for argument), `mul` does the multiplication, the result lands back in `a0`, and the function returns. Two instructions, crisp and clean.

As the youngest instruction set architecture, RISC-V was designed with everyone else's lessons baked in. Its integer registers are simply `x0` through `x31`, and the ABI assigns them aliases: `a0`-`a7` are the argument/return-value registers, `t0`-`t6` are temporaries, and `s0`-`s11` are callee-saved registers. What we see in disassembly is the aliases, but underneath they are just numbered `x` registers. This "uniform numbering underneath, semantic aliases on top" design is far easier to grasp than x86-64's every-register-has-a-unique-name scheme.

## Looking Back: They Are All Saying the Same Thing

Put the three architectures side by side and an interesting phenomenon emerges: different instruction names, different register names, different instruction counts — and yet the "semantics" they express are exactly the same: fetch the argument → multiply → place the return value → return. Reading assembly does not require recognizing every single instruction; just grab the thread of which register the data flows through and what arithmetic gets done, and we can roughly guess what it is up to.

It is like reading a poem in a language we half-know: we do not need to look up every word — word placement and repeated patterns carry the rhythm and the gist. Assembly is the same: see `mul` or `imul` and we know it is multiplying; see `ret` and we know the function is about to return; see data moving from one register to another and we know something is being passed along. This "half reading, half guessing" ability is far more practical than rote-memorizing the exact semantics of every instruction.

## A Key Reminder: the Optimization Level Radically Changes What You See

Everything shown above is `-O2` output. Turn optimization off (`-O0`) and the picture changes completely — a flood of `push`, `pop`, and memory traffic; arguments get stored to the stack and read back, and intermediate results get written to memory over and over. `-O0` assembly is so verbose because the purpose of `-O0` is to let the debugger map every C++ statement precisely onto machine instructions, so it performs no optimization at all and dutifully keeps every variable in memory. `-O2` is what the compiler "really" wants to generate. If the goal is to understand the compiler's optimization behavior and the code's actual performance, always look at `-O2` or higher; `-O0` only leads us astray.

That wraps up the assembly of the simplest possible function on three mainstream architectures. It is only a `square` function, but it established an important mental frame: where arguments come from, where results go, and in which instruction the core computation happens. With that frame in hand, the assembly of more complex functions later on will not feel like a dead end. Next, we carry this foundation into some more realistic scenarios.

---

# Machine Code and Assembly: What the Relationship Actually Is

Plenty of people use "machine code" and "assembly code" interchangeably — both are just unreadable stuff, right? But look carefully at objdump's output and the left-hand column of `0f af ff` and the right-hand column of `imul edi, edi` turn out to have a perfectly straightforward one-to-one mapping — it is just that nobody usually stops to think about it.

## Getting the Concepts Straight: Machine Code Is for Machines, Assembly Is for Humans

That left-hand column of hexadecimal digits — `0f`, `af`, `ff` and friends — is machine code. It is fundamentally a string of bytes in memory; the CPU reads those bytes and interprets them according to rules fixed in hardware: see `0f af` and it knows this is a multiply instruction, and the bytes that follow tell it where the operands are. The CPU has never heard of `imul` — it only understands numbers.

The right-hand column, `imul edi, edi`, is assembly code — the edition for humans. It maps to machine code essentially one to one: one assembly instruction corresponds to one fixed-format run of machine-code bytes. That is why assembly can be "assembled" into machine code (which is what the assembler does), and machine code can be "disassembled" back into assembly (which is what objdump, IDA, and friends do). Of course, on the way back the comments are gone, the variable names are gone, and all the semantic information from something like `int x = n * n` is gone too — nothing left but bare instructions.

But this two-way road exists, and it is remarkably direct. Assembly is not a "high-level language" that needs a compiler's heavy translation — it is practically another spelling of machine code.

## Hands On: the Simplest Square Function, and What Its Assembly Looks Like

To sort out the whole register question, start from the plainest square function possible:

```cpp
// square.cpp
int square(int n) {
    return n * n;
}
```

Then compile it with gcc into an object file — no linking — and look at just the assembly:

```bash
# My environment: Arch Linux WSL, x86-64, gcc 16.1.1
g++ -c -O0 square.cpp -o square.o
objdump -d -M intel square.o
```

`-M intel` is there because AT&T syntax (operands after the mnemonic, `%` prefixes everywhere) is unintuitive; with Intel syntax at least the operand order matches intuition. `-O0` turns off every optimization, so the compiler rewrites nothing and we get to see the most literal translation.

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

The first reaction on seeing this might be: wait, isn't the input argument supposed to be "passed in" from somewhere? C++ functions have parameter lists, but there is no such thing as a parameter list in assembly. So where did the argument go?

## Registers Are the CPU's Built-in "Global Variables", but Their Use Has Rules

Inside the CPU sits a small batch of extremely fast storage cells called registers. Think of them as a kind of "ultra-fast global variable" — right inside the CPU, no memory access involved, reads and writes at practically zero latency. Unlike global variables, though, their number is severely limited: under x86-64 there are only a dozen-odd general-purpose registers (RAX, RBX, RCX, RDX, RSI, RDI, R8-R15 and the rest). There is no way to cram all data into them.

The crux is: who decides which register does which job? If compiler A decided arguments go in RAX while compiler B decided they go in RDI, code compiled by the two could never call each other. Ship a library, have someone else write a program against it, and a mismatched register convention breaks the call outright.

So there has to be a set of "traffic rules" that everyone follows, or code cannot interoperate. That rulebook is the ABI (Application Binary Interface). The ABI specifies a great deal, and the most basic clause is this: on a function call, which register carries which argument, which register returns the value, which registers may be freely clobbered after the call, and which must be handed back untouched.

Linux uses the System V AMD64 ABI; Windows uses Microsoft's own x64 ABI — two different rulebooks. This is one of the reasons Linux and Windows binaries cannot be mixed directly (there are more reasons, of course, but differing register conventions are the most visible layer).

## Arguments Come In Through EDI, and Results Must Leave Through EAX

Back to our square function. Under System V ABI rules, the first integer argument sits in the RDI register. Note that I wrote RDI (64-bit), but our argument is an `int` — only 32 bits — so what actually gets used is the low 32 bits of RDI, namely EDI. Same story for RAX/EAX: RAX is the 64-bit version, EAX the 32-bit version.

So the moment execution enters the function, the value of `n` is already in EDI — there is nothing to "fetch" from anywhere; it is simply there.

Now walk the instruction sequence: `push rbp; mov rbp, rsp` is the standard stack-frame setup; `mov DWORD PTR [rbp-0x4], edi` stores the argument from EDI onto the stack — classic `-O0` behavior, no optimization, every variable dutifully placed in memory. Then `mov eax, DWORD PTR [rbp-0x4]` reads it back into EAX, `imul eax, eax` squares it, `pop rbp` restores the frame, and `ret` returns. All this `-O0` verbosity is exactly why we recommended looking at `-O2` output earlier — three extra stack-frame instructions that drown the core logic.

Then `imul eax, eax` multiplies EAX by EAX and stores the product back into EAX. This is a characteristically x86 design: most instructions take only two operands, and the left operand is both source and destination. It is the same idea as `a *= a` in C++ — read the value on the left, combine it with the value on the right, and write the result back to the left. It is a "destructive" operation: once it completes, the original left-hand value is overwritten. If that value is still needed later, it must be saved beforehand.

Finally, `ret` returns and hands control back to the caller. At that point EAX holds the squared result, and the caller knows to pick it up from EAX — because the ABI says so.

## Register Names Are Not Arbitrary

Beginners seeing the pile of names RAX, EAX, AX, and AL easily assume they are different registers. In fact, they are different "views" of one and the same physical register: RAX is the full 64 bits, EAX the low 32, AX the low 16, and AL the lowest 8. Writing to EAX overwrites (zeroes out) the upper 32 bits of RAX; writing to AL changes only the lowest byte, leaving the rest untouched.

This property causes no end of confusion during debugging. Staring at the register window, we might notice RAX and EAX disagreeing and suspect the debugger is broken — when in truth some instruction touched only the low 32 bits, and the upper 32 hold dirty data left over from an earlier operation. So whenever reading registers, always be clear about which "view" is currently on screen.

At this point, the assembly face of the simplest C++ function under x86-64 is clear: arguments arrive through registers (not the stack — at least the first several do), the computation happens between registers, and the result returns through a register. The whole process touches no memory and is blazingly fast. Granted, this is the simplest case — more arguments, local variables, or optimization will complicate things a great deal — but this is the basic frame.

---

# Reading Register Argument Passing from a Single MOV Instruction: the ARM and RISC-V Calling Conventions

In the previous section, our squaring function compiled down to a single multiply instruction at its core. When the function returns, control goes back to the caller. The caller had stuffed the argument into the EDI register (that is the x86-64 calling convention), and now it expects to find the return value in the EAX register — that is the x86-64 rule: integer return values travel in EAX (or RAX). So what that `imul edi, edi` does is completely plain: multiply the value in EDI by itself, write the result back to EDI, then mov it into EAX, and finally ret. The caller picks it up from EAX. Done.

So here is the question: across different architectures, how big is the "felt" difference when doing the same thing? Compile the same function for all three architectures and compare the assembly line by line — the differences are striking.

## The Simplicity of ARM64

ARM64 (AArch64) first. One might assume ARM assembly is roughly x86 with different instruction names and nothing more. Actually open objdump, and the differences far exceed expectations.

```cpp
// square.cpp — just this trivial function
int square(int value) {
    return value * value;
}
```

Run it through a cross-compilation toolchain:

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

That is it. Two instructions, spotless. One especially pleasant property: W0 is both the input and the output. Under ARM's calling convention, W0 (32-bit) or X0 (64-bit) is both the carrier of the first argument and the carrier of the return value. So `mul w0, w0, w0` reads as "multiply w0 by w0, put the result back in w0" — all three operands are the same register, visually perfectly uniform.

Next, take a look at the machine code of these instructions; it reveals an important design difference.

```bash
aarch64-linux-gnu-objdump -d -j .text square_arm64.o | grep mul
# 0:   1b007c00    mul w0, w0, w0
```

`1b007c00` — four bytes. Now that `ret`:

```asm
# 4:   d65f03c0    ret
```

`d65f03c0`, also four bytes. Two instructions, both exactly four bytes. This means the instruction decoder's job is dead simple: the fetch stage grabs a fixed four bytes every time, with no length judgment at all. Why this design is elegant becomes even clearer after the comparison with x86.

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

The interesting part is the byte length of each instruction:

- the `imul` instruction: `0f af ff`, three bytes
- the `mov` instruction: `89 f8`, two bytes
- the `ret` instruction: `c3`, one byte

Three instructions, three lengths: 3, 2, 1. Swap in a different spelling of the multiply, say `imul eax, edi`, and its machine code is `0f af c7` — still three bytes, but with a different suffix from the imul above (`ff` vs `c7`), because the operands encode differently. Change the scenario again — say the multiplier is an immediate — and the length changes once more.

"Variable-length instructions" is not just some textbook concept. Count bytes against the hex dump and it becomes obvious: every time the CPU front end fetches an instruction, it has to read the first few bytes to figure out how long this instruction even is before it can decide where the next one starts. x86's decoder is famously complicated; Intel has stuffed enormous amounts of pre-decode logic and micro-op caches into the CPU — essentially using hardware brute force to compensate for the instruction set's historical baggage.

## RISC-V's Fixed-Length Instructions

Now take a look at RISC-V (rv64gc):

```bash
riscv64-linux-gnu-g++ -O2 -c square.cpp -o square_rv64.o
riscv64-linux-gnu-objdump -d square_rv64.o
```

```asm
square:
    0:   02b50533    mul a0, a0, a0
    4:   8082        ret
```

Just like ARM, a0 is both the first argument and the return value, and `mul a0, a0, a0` means exactly the same thing. One detail, though: the `mul` instruction is four bytes (`02b50533`), but the `ret` instruction is only two (`8082`). RISC-V's base instructions are fixed-length four-byte, but it supports a 16-bit compressed-instruction extension (RVC), so common instructions like `ret` get squeezed down to two bytes. It is a compromise between fixed and variable length — still far more orderly than x86's "completely unpredictable" variability.

## Operand Counts: Not Every Instruction Is That Tidy

At this point one might think an instruction is just "opcode + a few operands" — nice and uniform. Read more assembly, though, and reality turns out far less pretty.

The `mul` and `imul` seen above are classic three-operand instructions (destination + source1 + source2), or two-operand (destination doubling as source1). But plenty of instructions simply refuse to follow the template. Zero-operand instructions are the simplest — `ret`, `nop` — needing no extra information at all. One-operand instructions are common too, all the jump instructions for instance. Two-operand and three-operand ones we just saw.

What genuinely confuses people are "implicit operands". x86 has an instruction `rep stosb`, whose job is "repeatedly write the value of the AL register to the memory pointed to by RDI (or EDI); after each write RDI/EDI increments automatically; the repeat count is controlled by RCX (or ECX)". AL, RDI/EDI, RCX/ECX — not one of these three operands appears anywhere in the instruction text; all of them are implicit, hard-coded into the instruction's definition. Whoever reads the assembly has to remember which registers that instruction uses by default. The "operand count" of such an instruction is genuinely hard to define.

## Intel's Historical Baggage

When it comes to implicit operands, x86 is the "hardest-hit zone". The reason is not complicated: this instruction set started with the 8086 in 1978 and has evolved all the way to today's x86-64, through more than forty years. Each new CPU generation has to add new things on top of the old instruction set while staying backward compatible — 8086 machine code written in 1985 still runs on a 2026 CPU. That constraint sounds wonderful, but the price is an instruction set that grows ever more bloated and irregular. With the encoding space crowded out by old instructions, new ones can only extend through various prefix bytes, and the decoding logic grows ever more complicated.

Sound familiar? C++'s backward-compatibility trouble is a carbon copy of this: writing C++26 code today, the compiler still has to digest C89-style declarations, C-style casts, and all manner of historical remains. Every time someone proposes "let's finally delete such-and-such old feature", the answer is forever "no — it would break existing code". And so the load gets carried forward.

By contrast, ARM and RISC-V are refreshingly unburdened. ARM64 was designed around 2011 (AArch64), effectively a "clean-room" implementation — a freshly designed instruction encoding that does not carry 32-bit ARM's historical baggage. RISC-V, even more so, started from zero as an academic project in 2010, and the orthogonality of its instructions is superb: the same opcode format serves everywhere — change the register numbers and off you go. None of those maddening "this instruction implicitly uses EAX, that one implicitly uses EDX" rules.

## Register Naming: Where the A Register Came From

We have been tossing around EAX, W0, and a0 all along — but has it ever occurred to you why x86's registers carry these strange names? There is history behind those names.

One x86 register is named A (Accumulator). Back in the 8080 era, and even earlier on the 8008, the A register was "the default register" — many operations targeted A implicitly, with no need to spell it out in the instruction. For addition, the encoding of "add some value onto A" was shorter than the encoding of "add some value onto B", because A was the "default destination" — the few bits that would name the destination register were saved.

That design idea carries straight through into x86 today. Write `imul edi, edi` now, swap it to `imul ebx, ebx`, and the machine code may come out longer (depending on the exact encoding), because EAX (or rather RAX) remains the "privileged register" in many instructions — the default destination of implicit instructions and a fixed participant in certain special operations (for one-operand `mul`, for instance, the high half of the double-precision result lands in EDX).

Tutorials love to say "prefer EAX whenever possible". That is not some mystical optimization trick — it is a favor granted at the instruction-encoding level: using the A register can mean a shorter instruction and potentially a faster decode. Of course, on modern CPUs microarchitectural optimizations have flattened most of this difference, but with this background in mind, those implicit-operand instructions stop looking inexplicable.

At this point, "what a simple function call actually looks like at the assembly level" has been traced end to end: how arguments travel, where return values sit, how instruction encodings differ across architectures, and where register names come from. No single step is hard; but pieced together and viewed as a whole, the entire system connects.

---

---

# Figuring Out Where Function Arguments Actually Go: from Register Naming to the ABI

When reading the assembly code that Compiler Explorer generates, the biggest psychological barrier is often not the instructions themselves but the chaotic register names. RAX, EAX, AX, AL, AH — is that one thing, or four? Once x86's register layout is sorted out, the question dissolves on its own.

## First, Sort Out What RAX, EAX, and AX Actually Are

Back to the most fundamental question: what is a register? Think of it as a small row of ultra-fast storage slots inside the CPU, very limited in number. In the 8-bit era, the most central one was the A register — the Accumulator — around which most arithmetic revolved. As CPUs evolved from 8 to 16, 32, and 64 bits, this register's width grew along with them, but its "status" never changed — always that general-purpose workhorse carrying the main computation.

The key point: when we see RAX, we are seeing a 64-bit value. But when we see EAX, we are not seeing a different register — we are seeing **the low 32 bits of the same register**. Likewise, AX is the low 16 bits, AL the lowest 8, and AH the second-lowest 8 (that is, bits 8-15). They all point at the same physical storage, just "sliced" under different names.

A simple diagram to illustrate:

```text
63                              31        15  7    0
+--------------------------------+----------+----+----+
|              RAX               |   EAX    | AX      |
|                                |          +----+----+
|                                |          | AH | AL |
+--------------------------------+----------+----+----+
```

So when we see code like this in assembly, there is no need to panic:

```asm
mov rax, rdi      ; put the 64-bit argument into rax for computation
shr rax, 32       ; shift right by 32 bits
mov eax, eax      ; keep only the low 32 bits as the return value
```

Switching from rax to eax here is not data being shuffled between two registers — it is the compiler saying "the computation is done, only the low 32 bits matter now". The type information in the C++ source (say, an `int64_t` parameter but an `int32_t` return value) maps directly onto which name of the same register the assembly uses. Once type information disappears, this is how it "survives" in the assembly.

## The Oddly Named Registers, and the Easy-to-Remember Newcomers

With RAX's naming pattern decoded, the natural next question is: what about all the others? RAX, RCX, RDX, RSP, RBP, RSI, RDI... There is no pattern whatsoever. They are all historical names inherited from antiquity: A is the accumulator, C is the counter, D is data, SP is the stack pointer, BP is the base pointer, and SI and DI are the source index and destination index respectively. Knowing the history makes them a little easier to remember, but to a large extent it still comes down to muscle memory built through repeated use.

There is one piece of good news, though: when AMD widened the architecture from 32 to 64 bits, the eight new general-purpose registers were simply named R8 through R15. Clean and done. So x86-64 now has sixteen general-purpose registers in total: eight with weird legacy names, eight with tidy numeric labels.

There are also the SIMD/multimedia registers (XMM/YMM/ZMM and the like), but those are a whole other topic — today we stay focused on general-purpose registers and function calls.

## Which Register Holds Which Function Argument

One of the biggest confusions in reading assembly: we write a function, pass three arguments into it, and in the assembly it turns into a pile of mov instructions shuffling things between registers. Where do the arguments come from? That is where the ABI (Application Binary Interface) comes in.

The ABI specifies a great deal, but from a reading-assembly standpoint, exactly one thing matters: **which register holds each of the first few arguments**. Know that, and we can trace what any C++ variable turned into in the assembly.

Take Linux (the System V AMD64 ABI). The first six integer arguments (pointers included) go, in order, into these registers:

```text
1st argument → RDI
2nd argument → RSI
3rd argument → RDX
4th argument → RCX
5th argument → R8
6th argument → R9
```

Arguments beyond six can only be pushed onto the stack, accessed through stack-pointer offsets. When using `std::forward` for perfect forwarding with a particularly large number of arguments, the assembly will show heavy stack traffic, because forwarding can "expand" the arguments — and the count instantly exceeds what six registers can hold.

Return values are simpler: uniformly placed in RAX (a 128-bit return value uses RDX:RAX stitched together).

Floating-point arguments are slightly more involved — they go through a separate register set (XMM0 through XMM7) — but the basic idea is the same: the first several travel in registers, the overflow goes on the stack.

## Windows Plays by Different Rules

On Windows with MSVC, things look different. The Windows x64 ABI allots only four registers for passing arguments:

```text
1st argument → RCX
2nd argument → RDX
3rd argument → R8
4th argument → R9
```

Note that both the order and the names differ from Linux. This means that the very same function keeps all six arguments in registers on Linux, while on Windows the fifth and sixth are already being pushed onto the stack. When debugging performance issues across platforms, the same C++ code producing completely different assembly on each side is often exactly this ABI divergence at work.

This difference actually has a subtle influence on API design, too: knowing that only four registers are available on Windows nudges us toward keeping parameter counts tight on frequently called interfaces. But let's expand on that when a concrete scenario shows up.

## Verify It Yourself

All talk and no action gets us nowhere — drop the simplest function into Compiler Explorer and see:

```cpp
// Compile options: -O1 -m64
// Platform: x86-64 Linux (GCC)

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

See? a is in RDI, b in RSI, c in RDX — exactly matching the rules we described. The return value is in RAX. Clean.

Now try one with more than six arguments:

```cpp
long sum_seven(long a, long b, long c, long d,
               long e, long f, long g) {
    return a + b + c + d + e + f + g;
}
```

The assembly becomes:

```asm
sum_seven(long, long, long, long, long, long, long):
    lea rax, [rdi + rsi]       ; a + b
    add rax, rdx               ; + c
    add rax, rcx               ; + d
    add rax, r8                ; + e
    add rax, r9                ; + f
    add rax, QWORD PTR [rsp+8] ; + g, fetched from the stack! Note the +8 offset: [rsp] holds the return address pushed by call
    ret
```

The first six arguments sit in RDI, RSI, RDX, RCX, R8, and R9 respectively, while the seventh argument, g, has gone to the stack, accessed through `[rsp+8]` (the `call` instruction pushed the return address at `[rsp]`, so the first stack argument is offset by 8 bytes). With the ABI rules known, reading assembly is like having a map — no more wall of gibberish.

## A Word on ARM64

If you have touched ARM64 (on Apple Silicon or in embedded work), that side is far cleaner. The general-purpose registers are simply called X0 through X30 — no historical baggage. Function arguments just march down X0, X1, X2..., with the return value in X0. Want the 32-bit view? Swap X for W: W0 is the low 32 bits of X0. The naming logic is the same idea as x86's RAX/EAX, but the names are far easier to remember.

At this point, register naming and the argument-passing rules are fully untangled. Feeling dizzy at the alternating rax and eax in assembly came from not knowing that it is the same register being sliced at different widths. Understanding that brings real peace of mind. Next, we take this foundation into more complex assembly patterns.

---

# RISC-V Register Naming: from Numbers to Semantics

When reading RISC-V assembly, opening the disassembly pane reveals a screen full of `t0`, `a7`, `s1`, `ra` — which looks just like x86's `rax`, `rbx`, `rcx`: apparently a heap of letter abbreviations to rote-memorize. But once it truly clicks, RISC-V's register naming turns out to be anything but arbitrary abbreviation — it tells us outright what the register **is for**. Understand the calling-convention semantics behind the names, and the names can be derived on the spot.

## Start from the Basic Numbering

RISC-V has thirty-two general-purpose registers in total, numbered `x0` through `x31`. Note: thirty-two, not thirty-one — `x0` really is an existing register; it is just hard-wired to zero. Write anything into it and the result is zero; read it and it is always zero. This design looks superfluous at first glance, but when writing inline assembly, having a constant zero directly usable as an operand saves quite a few `mov` instructions.

Then there is the question of width. RISC-V registers are 64-bit (the RV64G standard), and the numbers `x0` through `x31` correspond to full 64-bit values. If only the low 16 bits are needed, just mask with `0xFFFF` — no separate 16-bit register aliases like some architectures have. Quite refreshing: no hopping back and forth between register names of different widths.

The text above said `x0` through `x30`, but really all 32 registers, `x0` through `x31`, deserve mention. Among them `x1` is special — it is `ra` (Return Address), which we will cover in detail later. Either way, thirty-two registers laid out plainly beat x86-64's baggage-laden naming — x86's general-purpose register names were inherited all the way from the 16-bit era, `rax` grew out of `a`, `r8` through `r15` were bolted on later, and the whole scheme has no pattern to speak of.

## So What Exactly Are Those Aliases

Here comes the key. When actually writing assembly or reading disassembly output, the pure numeric names `x0` through `x31` almost never appear. Compilers and disassemblers have renamed every register, swapping in names with semantics. A wall of things starting with `t`, `s`, and `a` feels like a convention to memorize, but once the calling convention is understood, these names derive themselves.

Take a simple example, a RISC-V 64-bit target compiled with GCC 16.1.1:

```cpp
// test.cpp
long add(long a, long b, long c, long d,
         long e, long f, long g, long h, long i) {
    return a + b + c + d + e + f + g + h + i;
}
```

The compile command:

```bash
riscv64-linux-gnu-g++ -O1 -S test.cpp -o test.s
```

The assembly output:

```asm
add:
    add a0, a0, a1    # a0 += a1
    add a0, a0, a2    # a0 += a2
    add a0, a0, a3    # a0 += a3
    add a0, a0, a4    # a0 += a4
    add a0, a0, a5    # a0 += a5
    add a0, a0, a6    # a0 += a6
    add a0, a0, a7    # a0 += a7
    ld  a1, 0(sp)     # the 9th argument is on the stack; load it into a1
    add a0, a0, a1    # a0 += the stack-passed argument
    ret
```

See it? The first eight arguments sit in `a0` through `a7`, and the return value also goes in `a0`. `a` stands for Argument: `a0` through `a7` are the argument registers, and `a0` doubles as the return-value register. Far friendlier than x86's scattered "RDI holds the first argument, RSI the second, RDX the third" naming.

## T and S Registers: the Heart of the Calling Convention

With the `a` registers sorted, the rest falls into place naturally. The `t`-prefixed ones are Temporary registers, `t0` through `t6`, seven in total (the exact mapping gets a table shortly). The `s`-prefixed ones are Saved (callee-saved) registers, `s0` through `s11`, twelve in total.

These two concepts are easily mixed up. A classic pitfall: store an intermediate value in `t0`, call another function, come back to find `t0` changed and the program flying off the rails. That is because `t` registers are caller-saved — **if we store something in `t0` and then call another function, we must save it to the stack ourselves beforehand**, because the callee is free to use `t0` however it likes and guarantees nothing about its value.

The `s` registers are precisely the reverse: callee-saved. If a function uses `s1`, it must restore `s1` to the value the caller expects before returning. In other words, the caller may confidently keep something in `s1` across a call, and the value will still be there when it comes back.

Here is a concrete code example to verify:

```cpp
// caller.cpp
extern "C" long callee();

long caller() {
    register long temp __asm__("t0") = 42;
    register long saved __asm__("s1") = 99;
    long result = callee();
    // temp may already have been clobbered by callee
    // saved is guaranteed to still be 99
    return temp + saved + result;
}
```

```cpp
// callee.cpp
extern "C" long callee() {
    // deliberately writes t0 — this is perfectly legal
    register long t0_val __asm__("t0") = 0;
    // deliberately writes s1 — but it must be restored
    register long s1_val __asm__("s1") = 0;
    __asm__ volatile("" : "=r"(t0_val) : "0"(t0_val));
    __asm__ volatile("" : "=r"(s1_val) : "0"(s1_val));
    return 1;
}
```

Compile and run, and it turns out exactly as promised: back inside `caller`, the value of `temp` has indeed changed, while `saved` is still 99. Such is the power of the calling convention.

## The Full Mapping Table

The speaker says he keeps sticky notes in the bottom-left corner of his monitor, and plenty of people do the same. But once the naming logic clicks, this table actually does not need memorizing — understand it and it derives itself. For convenience, though, here is the complete mapping as a cheat sheet:

| Number | ABI Name | Meaning | Calling Convention |
|------|--------|------|----------|
| x0   | zero   | hard-wired to zero | — |
| x1   | ra     | return address | caller-saved |
| x2   | sp     | stack pointer | callee-saved |
| x3   | gp     | global pointer | — |
| x4   | tp     | thread pointer | — |
| x5-x7 | t0-t2 | temporaries | caller-saved |
| x8   | s0/fp  | saved register / frame pointer | callee-saved |
| x9   | s1     | saved register | callee-saved |
| x10-x17 | a0-a7 | arguments / return values | caller-saved |
| x18-x27 | s2-s11 | saved registers | callee-saved |
| x28-x31 | t3-t6 | temporaries | caller-saved |

`t` is use-and-toss scratch, `s` is keep-it-safe, `a` is carry-the-arguments, `ra` is remember-where-we-came-from, and `sp` is mind-the-stack. Every name is telling us its duty.

As an aside: anyone who has used 32-bit ARM before will recall that ARM has only sixteen general-purpose registers (R0-R15), with arguments fitting into just R0-R3 and everything else going through the stack. RISC-V has thirty-two registers — eight argument registers alone, plus seven temporaries and twelve callee-saved ones. More registers means fewer pushes and pops around function calls, and the performance benefit is rock solid.

## Implicit Arguments: the this Pointer and Return Value Optimization

At this point, argument passing may look solved — `a0` through `a7`, simple. But there is one easily overlooked question left: for C++ member functions, where does the `this` pointer go?

The `this` pointer is simply an implicit first argument. On RISC-V Linux, it travels in `a0`; the first declared "real" argument then goes in `a1`, and so on down the line. This is the same arrangement as on x86-64 Linux (where `this` takes RDI and the first argument takes RSI).

A simple piece of verification code:

```cpp
struct Foo {
    long x;
    long bar(long y) { return x + y; }
};

// Looking at the assembly after compiling, bar's signature is equivalent to:
// long Foo_bar(Foo* this, long y)
// a0 = this, a1 = y
```

```asm
_ZN3Foo3barEl:
    ld    a0, 0(a0)     # load the value from this->x into a0
    add   a0, a0, a1    # a0 += y
    ret
```

Crystal clear: `a0` starts out holding the `this` pointer, is immediately overwritten with the value of `this->x`, and finally the `y` from `a1` is added before returning.

But there are trickier cases. Suppose we write this:

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

`Big` is 32 bytes — it does not fit in a single register. When the compiler performs return value optimization (RVO/NRVO), it does not actually construct a `Big` inside the function and copy it out; instead, it reserves the space **in the caller's stack frame** and passes the address of that space to the callee as a hidden argument. On RISC-V, this hidden argument rides in `a0`; the declared first parameter `a` gets pushed over to `a1`, and the second parameter `b` to `a2`.

```asm
_Z9make_bigll:
    # a0 = the hidden return-buffer address
    # a1 = a, a2 = b
    sd    a1, 0(a0)     # result.data[0] = a
    sd    a2, 8(a0)     # result.data[1] = b
    sd    zero, 16(a0)  # result.data[2] = 0
    sd    zero, 24(a0)  # result.data[3] = 0
    ret
```

The assembly at the call site looks roughly like this:

```asm
    # the caller reserves 32 bytes on the stack
    addi  sp, sp, -32
    mv    a0, sp        # pass the buffer address as the first argument
    mv    a1, ...       # the real argument a
    mv    a2, ...       # the real argument b
    call  _Z9make_bigll
    # where sp now points is the constructed Big object
```

Seeing this for the first time is easily confusing — why is every argument shifted over by one slot? The reason is that a hidden pointer argument was inserted at the very front. This is the kind of thing nobody ever notices without reading the assembly — but once it shows up, not understanding it can eat an entire day of debugging.

At this point, RISC-V's register naming system is fully untangled. Looking back, it really was not that hard — the key is to understand the calling-convention semantics behind each name, rather than treating them as meaningless symbols to rote-memorize.

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

- To understand what assembly the compiler actually emits at different optimization levels (`-O0` / `-O2` / `-O3`), see [Volume 7: Compiler Options](../../../../vol7-engineering/02-compiler-options.md).
- To dive deeper into how SIMD/AVX reshapes assembly output, see [Volume 6: AVX/AVX2 Deep Dive](../../../../vol6-performance/ch04-tuning-by-bottleneck/04-05-simd.md).
