---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 talk notes — C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 36
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: My Journey and the Awakening from Assembly to C++
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/01-personal-journey-and-from-assembly-to-cpp.md
  source_hash: eac59ebcfd56a36098af19b3410fced3b5c2e69d1f5fe8e1e2a77741d600e427
  translated_at: '2026-09-26T15:22:07+00:00'
  engine: anthropic
  token_count: 7000
---
# Why C++ Programmers Should Care About Assembly

Plenty of C++ tutorials and teachers will tell you: when you write C++, don't concern yourself with the layers underneath—the compiler is smarter than you; just use templates, use smart pointers, use the standard library algorithms, and leave the rest to the optimizer. In practice, though, when you have iterated on slow code over and over without seeing progress, what you actually need to do is look at what your code compiled into—the assembly output. In many cases the compiler never inlined that template function you took for a "zero-cost abstraction"; that lambda you assumed "should be fast" gets constructed and destroyed over and over inside the loop. Assembly doesn't lie; it is literally what your code became.

And this is bound up with the core philosophy of C++. From the day it was born, C++ has pursued exactly one thing: you don't pay for what you don't use<RefLink :id="1" preview="Stroustrup, The C++ Programming Language, 1986, zero-overhead principle" />. But then the question is: how do you know whether you are paying? The compiler won't come out and tell you "this abstraction of yours has overhead"—it just quietly generates code. And that code is the assembly.

The most direct way to understand what a template actually expands into is not to read compiler error messages (important as they are), but to look at the generated assembly. When you see the functions instantiated from your templates perfectly inlined, your loops unrolled, and registers allocated sensibly, you finally understand what "zero-cost abstraction" means. And the other way around: the moment you see a pile of superfluous function calls and memory shuffling, you know immediately where things went wrong.

So don't treat assembly as something unfathomably deep. It's a mirror, showing you what the C++ you wrote really looks like. You don't need to master it, but you do need to be able to read its outline and tell when something is off.

---

# Starting from "Hand-Typed Code": Why We Need to Understand the Layers Below

The speaker brought up the ZX Spectrum<RefLink :id="2" preview="Sinclair Research, ZX Spectrum, 1982, Zilog Z80A" /> and the era of typing code in by hand. For many people just learning to program, compiling, running, and seeing that line of text appear in the terminal already feels like enough. But a realization comes quickly: you don't actually know how that line of text got onto the screen, or even what your code turned into after compilation. This "black box" feeling may not matter while you are writing high-level abstractions, but once a bug shows up—especially one of those eerie memory-related bugs—you have nowhere to start.

Learning to program is not just learning syntax, frameworks, and APIs. C++'s syntax alone is headache enough—rvalue references, perfect forwarding, SFINAE; for a beginner, merely memorizing the names of these thoroughly opaque concepts takes time. But the deeper you go, the more you bump into an awkward fact: you don't truly understand what the code you wrote does at the machine level. When somebody asks "how does the 'Hello World' string get from the executable file onto the CPU" and you can't answer, your understanding of the layers below isn't there yet.

## Hands On: What Your C++ Code Actually Becomes

Compile your own C++ code to assembly and read it line by line—that is the most direct way to understand "what is this code actually doing".

Test environment: Arch Linux WSL, GCC 16.1.1, with `-S -O0` added to the compile command. `-S` tells the compiler to stop after generating assembly, and `-O0` turns every optimization off, because with optimizations on the assembly gets rewritten beyond recognition and a beginner can no longer map it back to the source.

Let's write the simplest possible example:

```cpp
// demo.cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}
```

Compile it:

```bash
g++ -S -O0 -o demo.s demo.cpp
```

Then open `demo.s`. You'll see a whole pile of stuff—don't panic; most of it is auxiliary information the compiler adds, and we only care about the core. On x86-64, the assembly of the `add` function looks roughly like this:

```asm
add(int, int):
    pushq   %rbp            ; save the caller's stack frame base
    movq    %rsp, %rbp      ; set up the current function's stack frame
    movl    %edi, -4(%rbp)  ; store the first argument a on the stack
    movl    %esi, -8(%rbp)  ; store the second argument b on the stack
    movl    -4(%rbp), %edx  ; read a back out
    movl    -8(%rbp), %eax  ; read b back out
    addl    %edx, %eax      ; a + b, the result lands in %eax
    popq    %rbp            ; restore the caller's stack frame base
    ret                     ; return
```

The part of `main` that calls `add`:

```asm
main:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp       ; allocate 16 bytes of local variable space on the stack
    movl    $4, %esi        ; the second argument, 4
    movl    $3, %edi        ; the first argument, 3
    call    add(int, int)   ; call add
    movl    %eax, -4(%rbp)  ; store the return value into result
    movl    -4(%rbp), %eax  ; return result
    leave                   ; equivalent to movq %rbp, %rsp; popq %rbp
    ret
```

The first thing you notice when you see this assembly: at `-O0`, the compiler dutifully compiles `return a + b` by first moving the arguments from registers onto the stack, then reading them back from the stack to do the addition. Not efficient—but this is the raw, unoptimized shape; every line is plainly there, and you can watch how the data flows.

## A Pitfall That's Easy to Fall Into

There's a trap we have to flag here. At first we compiled with `-O1`, and found that `add`'s assembly was only two or three lines—the arguments never touched the stack; the whole thing was computed right in the registers (readers familiar with compiler optimization probably felt nothing there: these were operations that live at the register level to begin with, right?). That's because `-O1` already does register-allocation optimization—the compiler saw no reason to store the arguments to the stack only to read them back, and just used the registers. So if you want to run the experiment along with us, make sure to use `-O0`, or you'll be staring at a pile of things you can't make sense of.

```asm
    .file   "demo.cpp"
    .text
    .globl  _Z3addii
    .type   _Z3addii, @function
_Z3addii:
.LFB0:
    .cfi_startproc
    leal    (%rdi,%rsi), %eax
    ret
    .cfi_endproc
.LFE0:
    .size   _Z3addii, .-_Z3addii
    .globl  main
    .type   main, @function
main:
.LFB1:
    .cfi_startproc
    movl    $7, %eax
    ret
    .cfi_endproc
.LFE1:
    .size   main, .-main
    .ident  "GCC: (GNU) 16.1.1 20260430"
    .section    .note.GNU-stack,"",@progbits
```

The other trap is that calling conventions differ across platforms. What's shown above is the x86-64 System V ABI<RefLink :id="3" preview="System V Application Binary Interface, AMD64, calling convention" />: the first two integer arguments go into `%edi` and `%esi`, and the return value comes back in `%eax`. Compile with MSVC on Windows and argument passing looks different (it uses `%rcx`, `%rdx`<RefLink :id="4" preview="Microsoft, x64 Calling Convention, RCX/RDX/R8/R9" />). So if your output doesn't match, check the platform and the compiler first.

## Why Understanding Assembly Helps You Understand C++

Once you've seen this assembly, many things that used to feel mystical become clear. For example, why the performance gap between pass-by-value and pass-by-reference in C++ is so large—pass-by-value means copying data, and if the object is big, the cost of that copy is, at the assembly level, one `mov` instruction after another, laid out in plain sight. Pass-by-reference? You pass just one address, one 8-byte pointer; no matter how large the object is, 8 bytes is all that travels. You may have merely "known" these things before; after reading assembly, you understand them.

Or take why inline functions improve performance: the `call` instruction itself has overhead—you save the return address, you jump, and after the function returns you jump back. If the compiler expands the function body directly at the call site, all of that overhead disappears. In the assembly you see no `call` and no `ret` at all—the code just executes sequentially.

When you can see the machine instructions behind every line of code, "performance" is no longer the abstract "fast" or "slow", but the concrete "these few instructions could be eliminated", "this memory access could be merged".

## Where to Dig Next

Once this layer is clear, you naturally start wondering: how does the linker stitch multiple object files together? What exactly happens when a dynamic library loads? How do the operating system's syscalls switch from user mode into kernel mode? These things are not "compiler theory" and "operating systems" coursework irrelevant to your business code—they are the foundation, and if the foundation isn't solid, whatever you build on top of it will sway.

If the layers below have always felt vaguely blurry to you, start with "reading assembly". You don't need to study it deeply, and you don't need to be able to write assembly by hand; once you can look at C++ code and roughly guess what the assembly will look like, your programming intuition moves up a level.

## What Exactly "Assembly" Is—Starting from the Birth of Compiler Explorer

Before we dig further down, there's one basic question worth answering: what exactly is this "assembly" we keep talking about?

At the time, the speaker was writing C++ at a company whose boss was extremely conservative and allowed none of the newer C++ features. How far did that go? They were debating whether range-based for loops could replace the most primitive `for (int i = 0; i < sizeof(array); ...)` form. They had just been burned by another programming language, in which those two forms genuinely were not equivalent, so the boss was acutely sensitive to "syntactic sugar". A benchmark was run; the results were ambiguous; the boss slammed the table: hands off.

The speaker didn't give up. He threw together a shell script that flipped compile options back and forth in the terminal so the assembly output kept refreshing. Then it got too messy, so he ran the output through some regex substitutions and formatting, and piped it through `c++filt` to restore the symbol names that name mangling had shredded. When he was done, he realized something: he could edit C++ code in Vim on the left and watch the corresponding assembly output update live on the right.

That tool was the embryo of the later, hugely famous Compiler Explorer<RefLink :id="13" preview="Matt Godbolt, Compiler Explorer (godbolt.org), 2012" /> (that is, godbolt.org). And the story hands us a key insight: **even though we in C++ keep chasing ever higher abstraction, assembly still matters enormously to this language—and to us.** Many developers feel that once they use C++17, once they use `std::optional` and `std::variant`, there is no longer any need to look at assembly: the compiler is smarter than they are, so surely the code it generates is fine. Only after actually starting to read assembly do they find that yes, the compiler is smart—but what it does is frequently not what they assumed it did.

So what exactly is "assembly"? The dictionary gives the word several layers of meaning: it is a set of parts working together as one; it is the act or process of fitting a set of parts together; it is a body of people gathered in one place for a purpose; it is a legislature with ominous political overtones; and, militarily, it is the drum signal calling the troops to fall in. The last sense—the one we actually care about—is that it is the shortened form of assembly language.

In other words, all this time we have been saying "look at the assembly", we have, strictly speaking, been misusing the word. We should say "look at the assembly language". This sounds like pointless wordplay, but give it a moment's thought and it's actually rather apt. Assembly in itself is an action, a process—putting parts together. Assembly language is the thing with a concrete syntax, an instruction set, opcodes. What the compiler does really is "assembly"—assembling the various parts of C++ (variables, functions, template instantiations) into the final machine code. And what we go and read is that "assembly language"—the blueprint produced along the way.

Once this distinction is straight, one thing becomes clear: what we look at is assembly language, the human-readable form of the instructions a CPU can understand, not some abstract "assembling process". And the reason assembly language matters to C++ programmers is that C++'s abstractions carry a cost (paradoxically so—we may be pursuing cost-free abstraction, but that's the goal, not the realized result...), and without looking at assembly language that cost is simply invisible.

Take the simplest example: a `std::function` used inside a function on the hot path, because "the compiler will optimize it anyway". Performance drops. Look at the assembly in Compiler Explorer and—the `std::function` call involves a virtual dispatch, a heap-allocation check, and a pile of type-erased indirect jumps. Switch to a template parameter and the compiler inlines it outright; there isn't even a function call anymore. Without assembly language, you would never know any of this was happening. A benchmark can tell you "it got slower"; only assembly language can tell you "why it got slower".

---

# From Assembly to C: A Forced Paradigm Leap

The talk recounts a highly representative episode: someone, without ever having studied computer science, wrote a program in pure assembly that had reference counting and even a self-invented mark-sweep<RefLink :id="11" preview="John McCarthy, Recursive Functions of Symbolic Expressions, 1960" />. This is not deep theory being presented—it is a real person genuinely falling into pits, discovering problems, and then "inventing" something that had already been invented. Working through that process helps us understand where the concepts we later meet in C++ actually came from.

## The "Monster" Written in Pure Assembly

Picture this scene: a person studying physics, knowing nothing about computer science, wants to write a fully windowed chat program. Not the type-and-hit-enter-in-a-terminal sort of thing, but one with a real windowed interface, communicating over TCP, able to pause and then send messages, format complex strings, and support direct file transfer between clients. And built into it is a scripting language of his own invention, inspired by BASIC—and that scripting language even supports dynamic allocation.

Many beginners' mental image of assembly is writing the occasional interrupt handler or startup code—a few dozen, a few hundred lines at most. But this program is page after page of assembly code, all up on GitHub, with tag names that have drifted so far into absurdity they've lost all sense—the most classic one being `WombleLoopJedi`. Nobody has any idea what it means, but you can tell the person writing it had by then entered some kind of exalted state.

The best part is what comes after: he added dynamic allocation to the scripting language, then thought "reference counting seems like a good idea" and implemented reference counting. Then he discovered the circular-reference problem. And then he reasoned his way to a complete scheme—find the things that are no longer referenced, and delete them by hand. Years later, chatting with a friend about it, the friend said: "oh, so you invented mark-sweep garbage collection."

That is what thinking looks like when no textbook is there to constrain it. He didn't know it was called mark-sweep; starting from the problem, he derived the correct solution step by step. Mark-sweep is not an algorithm somebody dreamed up out of thin air—it is the natural derivation that falls out of solving the concrete problem "reference counting can't handle cycles".

We can reconstruct that thought process with a piece of simplified pseudocode—it's much clearer than explaining concepts in the abstract:

```cpp
// Phase one: reference counting (the first step you'd think of)
struct Object {
    int ref_count = 0;
    char data[256];
};

void acquire(Object* obj) {
    obj->ref_count++;
}

void release(Object* obj) {
    obj->ref_count--;
    if (obj->ref_count == 0) {
        // nothing references it anymore — free it
        free(obj);
    }
}

// Then he ran into this problem:
// A references B, and B references A
// A.ref_count = 1, B.ref_count = 1
// yet nothing outside references A or B anymore
// they will never be freed — that's a circular reference
```

Since the reference count never reaches zero, change the angle—stop starting from "how many things reference me" and start from "can anything still reach me at all". What is reachable is alive; what is not reachable is dead; delete the dead. That is the core idea of mark-sweep: mark marks the reachable ones, sweep sweeps away the unreachable ones.

```cpp
// Phase two: the mark-sweep he "invented" (conceptual reconstruction)
// Suppose we have a set of root objects (globals, stack locals, etc.)
// everything reachable from the roots is alive

Object* roots[64];  // the root set
Object* all_objects[1024];  // every allocated object
int all_count = 0;
bool marked[1024];  // the mark array

// mark phase: starting from the roots, recursively mark every reachable object
void mark(Object* obj) {
    // find obj's index in all_objects
    for (int i = 0; i < all_count; i++) {
        if (all_objects[i] == obj && !marked[i]) {
            marked[i] = true;
            // assume the object stores references to other objects
            // recursively mark every object it references
            // mark(obj->ref1);
            // mark(obj->ref2);
            break;
        }
    }
}

void mark_all_roots() {
    for (int i = 0; i < 64; i++) {
        if (roots[i] != nullptr) {
            mark(roots[i]);
        }
    }
}

// sweep phase: walk every object; anything unmarked is garbage
void sweep() {
    for (int i = 0; i < all_count; i++) {
        if (!marked[i]) {
            free(all_objects[i]);
            all_objects[i] = nullptr;
        }
        marked[i] = false;  // reset the mark, ready for the next cycle
    }
}

// a full GC cycle
void garbage_collect() {
    mark_all_roots();
    sweep();
}
```

Logically it really isn't complicated. Garbage collection looks like black magic, but put it back into this setting—someone writing a scripting language, needing to manage memory, finding reference counting insufficient, and changing the approach—and it becomes entirely natural. The key is not how exquisite the algorithm is, but whether you can start from a real problem and make it all the way here.

## From Assembly to C: The Forced Turning Point

This person kept writing things in assembly; assembly accompanied him the whole way. Until one day, he wanted to run a multi-user dungeon—a MUD<RefLink :id="12" preview="Trubshaw & Bartle, MUD (Multi-User Dungeon), 1978" />.

A MUD is a purely text-based multiplayer online RPG with no graphical interface; everything is described in words. Once you connect, what you see is things like "You are standing at a crossroads; there is a castle to the north and a forest to the east". Type "go north" and you head north; type "attack goblin" and you fight the goblin. You can form a party with friends, fight monsters, cast spells—essentially it is a text-based, online edition of Dungeons & Dragons.

The problem was that he couldn't write an entire MUD from scratch himself. It was too big—even for someone who could write thousands of pages of assembly. So he found some source code circulating online, with a license that raised no issues and allowed direct use. A note on the era: there was no GitHub then, nor anything like it. The way people shared code was passing around tarballs—those `.tar.gz` archives, usually over IRC, person to person, file to file. Someone would shout into an IRC channel, "who's got MUD source?", someone would send an archive over DCC, and the moment you had the archive you started tinkering. No version control, no issue tracker, no pull requests—just bare code files.

And that MUD source code was written in a programming language called C. There is the turning point. A man who had written thousands of pages of assembly was now staring at a body of C code. He had to learn C, or he would have no way to modify that MUD. This is not "I'd like to learn a new language" motivation; it is "I must be able to read this code to do what I want to do" motivation.

Jumping from assembly to C looks like nothing today, but at the time it was a huge paradigm leap. In assembly you manipulate registers, memory addresses, interrupts; in C you start using abstraction-level notions like variables, functions, structs. For someone who had always worked in assembly, "the compiler takes care of your stack frames" alone took some adjusting to. But flip it around: precisely because he came from assembly, his intuition about how C code runs underneath was probably sharper than that of many conventionally schooled programmers—because he knew what machine instructions those C statements would ultimately become.

Sometimes what pushes us forward is not a systematic learning plan, but a project we desperately want to do that the existing toolchain can't deliver.

---

# From Assembly to C++: Why We Need Higher-Level Languages

The talk mentions that at fifteen he wrote programs in pure assembly and submitted them to magazines for money. From that background, one thing becomes understandable: why C++ is designed the way it is, and why it has so many "seemingly redundant" layers of abstraction.

If you look back from the assembly side, many design decisions stop being "mystification" and start being "forced into existence".

## What Assembly Programming Actually Feels Like

A program that "reads two numbers from standard input and adds them" takes close to fifty lines of x86 assembly, and you manage stack alignment yourself, look up syscall numbers yourself, handle the buffer yourself. The programs he wrote at fifteen, the speaker says, ran twenty dense pages of small print in the magazine. Mistype one piece of punctuation and the program blows up—and then you get to hunt for that one error across twenty pages of print.

Once you understand what many of C++'s mechanisms are for, your whole mindset changes. It stops being "one more syntax to memorize" and becomes "look how much work this saved me".

## Same Logic, Different Worlds: How Far Apart Are Assembly and C++

Let's look at a deliberately simple example—call a function, pass an argument, get a return value. In C++ this is beneath mention, but at the assembly level plenty is going on.

```cpp
// simple_call.cpp
// the simplest possible function call: pass arguments, return
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}
```

Compile it and look at the assembly output (the environment I used comes later):

```bash
g++ -O0 -S simple_call.cpp -o simple_call.s
```

`-O0` turns every optimization off, because with optimizations on the compiler folds the whole thing into a constant, and we would no longer see the call unfold. Open `simple_call.s` and you'll see something like this (key excerpt, AT&T syntax):

```asm
add(int, int):
    pushq   %rbp            ; save the caller's stack frame base
    movq    %rsp, %rbp      ; set up our own stack frame
    movl    %edi, -4(%rbp)  ; store the first argument a on the stack
    movl    %esi, -8(%rbp)  ; store the second argument b on the stack
    movl    -4(%rbp), %edx  ; load a into edx
    movl    -8(%rbp), %eax  ; load b into eax
    addl    %edx, %eax      ; eax = edx + eax, i.e. a + b
    popq    %rbp            ; restore the caller's stack frame base
    ret                     ; return; the return value is in eax

main:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp       ; allocate 16 bytes of space on the stack
    movl    $4, %esi        ; put the second argument in esi
    movl    $3, %edi        ; put the first argument in edi
    call    add(int, int)   ; call the function
    movl    %eax, -4(%rbp)  ; store the return value where result lives
    movl    -4(%rbp), %eax  ; return result as main's return value
    leave
    ret
```

For a single `add(3, 4)`, at the assembly level you have to care about: how the stack frame gets built, which registers carry the arguments (the x86-64 System V calling convention assigns the first six integer arguments to rdi/rsi/rdx/rcx/r8/r9), where the return value lands, and how the stack gets restored after the call. In C++ one line of code does it—the compiler took care of all of this for you.

## One Step Further: Arguments That Aren't Simple Integers

The example above is too easy, so let's try passing a string. Now pointers and memory layout are in play.

```cpp
// string_call.cpp
#include <cstring>

// Simulate a simple string operation: convert the entire input string to uppercase
void to_upper(char* dst, const char* src, int max_len) {
    int i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        if (src[i] >= 'a' && src[i] <= 'z') {
            dst[i] = src[i] - ('a' - 'A');
        } else {
            dst[i] = src[i];
        }
        i++;
    }
    dst[i] = '\0';
}

int main() {
    char src[] = "hello world";
    char dst[32];
    to_upper(dst, src, 32);
    return 0;
}
```

This C++ code looks perfectly straightforward. But hand-write this logic in assembly and you're computing the address offsets of `src` and `dst` yourself, managing the loop counter yourself, checking character ranges yourself, appending the terminator yourself. And the killer part—if you compute one offset wrong, the program won't tell you "your array is out of bounds"; it either silently corrupts some other data or crashes outright with a segmentation fault.

So look again at these designs in C++, and something clicks:

**References** — why do they exist? Because passing pointers is too easy to get wrong: null pointers, dangling pointers, miscalculated offsets. A reference says, semantically, "this thing definitely points to a valid object", and the compiler holds that floor for you.

**`std::string`** — why does it exist? Because a bare character array plus manual length management is precisely the breeding ground for the disaster above. You can go without `std::string`, sure—but then you must guarantee that every single site correctly handles length, terminator, copying, and destruction.

**`std::string_view`** — why did C++17 add it? Because sometimes you only want to read a string, not copy it, yet `const std::string&` triggers an implicit construction of a temporary `std::string` when passed a `const char*`. A `string_view` is a lightweight "look, don't touch" view—under the hood it's nothing more than a pointer-plus-length pair—but semantically it is far clearer than raw `const char*` + `size_t`.

If you've never written assembly, never been tortured by pointers and memory layout, these may look like needless ceremony. If you have been tortured, your reaction is "thank heavens somebody thought all this through for me".

## Environment Notes

Here is the environment these examples ran in, for reproducibility:

- Environment: Arch Linux WSL, GCC 16.1.1
- Assembly syntax: GCC's default AT&T syntax (the one whose operand order is reversed relative to Intel syntax—`%rax` rather than `rax`, `movq src, dst` rather than `mov dst, src`)
- If you'd rather see Intel syntax, just add `-masm=intel`: `g++ -O0 -S -masm=intel simple_call.cpp`

## Why Someone Would Write an IRC Client

The talk mentions that he later switched to an Archimedes computer<RefLink :id="8" preview="Acorn Computers, Archimedes, ARM2, 1987" />, with an ARM processor, and there was no ready-made IRC<RefLink :id="9" preview="Jarkko Oikarinen, Internet Relay Chat, 1988" /> client—so he wrote one.

This mindset—"I need a tool, none exists, so I'll build one"—is extremely common in learning to program for real. Because only when you genuinely need to build something do you run into the problems tutorials never tell you about: `std::getline` behaving inconsistently in certain terminals; `std::ofstream` handling newlines differently across platforms; storing Chinese text in a `std::string` and having `length()` return the number of bytes, not characters. If all you ever do is follow a tutorial and type out "Hello World", you'll never hit these. The moment you set out to write something that actually works, they all come out of the woodwork. The fifteen-year-old writing an IRC client in the talk was the same. He didn't first finish learning all of network programming and then start; he thought "I want to get on IRC, I don't have a client, so I'll write one". The knowledge didn't come out of a textbook—it grew out of the desire to do the thing.

## From "Hand-Writing Everything" to "Leaning on Abstractions"

C++ is, in essence, a language that lets you choose which level to work at.

Want to control memory by hand? Sure—pointers, `new`/`delete`, placement new, alignment attributes, all left wide open. Want the compiler to manage it for you? Sure—smart pointers, RAII, containers, `std::string`; you never think about deallocation. Want some things computed at compile time? Sure—`constexpr`, templates, and concepts move runtime cost forward into compile time. Want generic code? Sure—templates let one piece of code handle all kinds of types, and concepts check type constraints at compile time.

These levels don't replace one another—they mix. In the same program you can do raw-pointer, high-performance memory manipulation at the bottom layer and safe data management with `std::vector` and `std::string` at the top. That flexibility was unthinkable in the pure-assembly era—there was one level then, and it was "do everything yourself".

Which explains C++'s design philosophy—"what you don't use, you don't pay for". The language was born from a group of people whom assembly had thoroughly worn out, who wanted a language that could "control the low level without hand-writing every low-level detail". It didn't drop out of the sky; it was forced into being by necessity<RefLink :id="1" preview="Stroustrup, The C++ Programming Language, 1986, zero-overhead principle" />. Once you string this history together with the language's design, many designs that used to seem "inexplicable" suddenly fall into place.

---

# From "Assembly Is the Only Answer" to "The Compiler Can Actually Do the Job"

The talk mentions the stretch where "every new computer meant a different operating system and a different architecture". Back then, when his MUD got banned by the administrator and he was forced to switch machines, what did that mean? It meant your hand-written assembly wouldn't run a single line on a completely different CPU. Writing the MUD in C rather than assembly had a very down-to-earth reason—rewriting your assembly on every machine change simply did not work. Even though in that era the C compilers on different machines could themselves behave differently, C still beat assembly by a mile, because the payoff was enormous. In his words, "rewriting it in assembly simply wasn't viable"—that is not sophisticated software-engineering theory; it is the instinct of somebody the real world had beaten thoroughly.

## Try It Yourself: The Cross-Platform Cost Gap Between Assembly and C

Let's write a minimal example to feel the difference. Suppose we need a feature that reverses a stretch of memory byte by byte. This operation is actually common in game development—handling little-endian/big-endian data across platforms, for instance.

First let's write it the pure-assembly way (x86_64, using GCC inline assembly):

```cpp
// reverse_asm.cpp
// Note: this code only compiles under GCC/Clang on x86_64
// Move to ARM? Move to MSVC? It fails outright — not one byte of it runs

#include <cstdint>
#include <cstdio>
#include <cstring>

void reverse_bytes_asm(void* data, size_t len) {
    // rdi = data, rsi = len (the System V ABI argument-passing convention)
    __asm__ __volatile__(
        "test %rsi, %rsi\n\t"       // if len == 0, return immediately
        "jz 2f\n\t"
        "mov %rdi, %rax\n\t"        // rax = start address
        "lea -1(%rdi, %rsi), %rdx\n" // rdx = end address (data + len - 1)
        "1:\n\t"
        "cmp %rax, %rdx\n\t"        // have the left and right pointers met?
        "jge 2f\n\t"                // met or crossed means we're done
        "movb (%rax), %cl\n\t"      // cl = *left
        "movb (%rdx), %dl\n\t"      // dl = *right (note: this overwrites rdx!)
        // rdx is being used as both pointer and temporary — the most common hand-written assembly trap; register allocation lives entirely in your head
        "movb %cl, (%rdx)\n\t"      // *right = cl (but rdx has already been clobbered!)
        "movb %dl, (%rax)\n\t"      // this line is wrong too
        "inc %rax\n\t"
        "dec %rdx\n\t"
        "jmp 1b\n\t"
        "2:\n\t"
        : /* no output operands */
        : "r"(data), "r"(len)
        : "rax", "rcx", "rdx", "cc", "memory"
    );
}

int main() {
    uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t len = sizeof(buf);

    printf("反转前: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");

    reverse_bytes_asm(buf, len);

    printf("反转后: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");

    return 0;
}
```

The inline assembly above contains a classic register-conflict bug—`rdx` is used simultaneously as pointer and as temporary storage, the most typical hand-written-assembly trap. Even with that bug fixed, this code compiles only under x86_64 + System V ABI. Want to run it on ARM? Sorry—the instruction set is completely different, the register names are different, the calling convention is different; it amounts to writing it from scratch.

Now the same logic in pure C++:

```cpp
// reverse_cpp.cpp
// This code compiles on any platform that has a C++ compiler
// x86_64, ARM, RISC-V, MIPS... take your pick

#include <cstdint>
#include <cstdio>
#include <utility>

void reverse_bytes_cpp(void* data, size_t len) {
    if (len == 0) return;

    auto* bytes = static_cast<uint8_t*>(data);
    size_t left = 0;
    size_t right = len - 1;

    while (left < right) {
        // std::swap has existed since C++11; underneath, the compiler optimizes it into a register swap
        std::swap(bytes[left], bytes[right]);
        ++left;
        --right;
    }
}

int main() {
    uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t len = sizeof(buf);

    printf("反转前: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");

    reverse_bytes_cpp(buf, len);

    printf("反转后: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");

    return 0;
}
```

This C++ code looks too simple—what is there to compare? But that is exactly the point: choosing C over assembly wasn't because C could express more sophisticated algorithms, but because when this kind of "simple logic" moves platforms, the C version only needs a recompile while the assembly version needs a rewrite. When a project has hundreds of pieces of such "simple logic", that gap is the essential difference between "portable" and "not portable".

## 1990s Compilers Weren't Good Enough, So You Had to Hand-Write Assembly—But It's 2026 Now

The talk raises a key piece of historical context: in the 1990s and the early 2000s, compilers weren't smart enough yet, and CPUs carried many game-oriented special instructions (the PS2's VU instructions, the Dreamcast's SH4 extensions) that compilers had no idea how to generate—so hand-written assembly was mandatory. That logic still holds today; only the form has changed. Writing NEON instructions on ARM for SIMD acceleration, or writing GPU kernels in CUDA, is at bottom all "the compiler (still) can't automatically generate the optimal code for you, so you have to spell it out by hand". The difference is that these scenarios are far fewer now than they were, and compilers are improving fast.

Here's a comparison experiment: the same matrix multiplication, run once as a pure C++ loop and once as hand-written AVX2 inline assembly:

```cpp
// matmul_test.cpp
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <immintrin.h>  // AVX2 intrinsics header

// plain C++ scalar version
void matmul_scalar(const float* A, const float* B, float* C, int N) {
    memset(C, 0, N * N * sizeof(float));
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            float aik = A[i * N + k];
            for (int j = 0; j < N; j++) {
                C[i * N + j] += aik * B[k * N + j];
            }
        }
    }
}

// AVX2/FMA intrinsics version (not pure assembly, but the same idea — spelling out the SIMD instructions by hand)
void matmul_avx2(const float* A, const float* B, float* C, int N) {
    memset(C, 0, N * N * sizeof(float));
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            float aik = A[i * N + k];
            __m256 vaik = _mm256_set1_ps(aik);  // broadcast aik to 8 floats
            for (int j = 0; j < N; j += 8) {  // N=256 is a multiple of 8, no tail handling needed
                __m256 vb = _mm256_loadu_ps(&B[k * N + j]);  // load 8 elements of B
                __m256 vc = _mm256_loadu_ps(&C[i * N + j]);  // load the current value of C
                vc = _mm256_fmadd_ps(vaik, vb, vc);          // vc += vaik * vb
                _mm256_storeu_ps(&C[i * N + j], vc);         // store it back
            }
        }
    }
}

// a simple timing helper
#include <chrono>
using Clock = std::chrono::high_resolution_clock;

int main() {
    const int N = 256;  // a 256x256 matrix
    // aligned allocation, to make AVX2 loads convenient
    float* A = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* B = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* C1 = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* C2 = (float*)_mm_malloc(N * N * sizeof(float), 32);

    // fill with random data
    for (int i = 0; i < N * N; i++) {
        A[i] = static_cast<float>(rand()) / RAND_MAX;
        B[i] = static_cast<float>(rand()) / RAND_MAX;
    }

    // test the scalar version
    auto t1 = Clock::now();
    matmul_scalar(A, B, C1, N);
    auto t2 = Clock::now();
    auto scalar_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    // test the AVX2 version
    t1 = Clock::now();
    matmul_avx2(A, B, C2, N);
    t2 = Clock::now();
    auto avx2_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    printf("标量版本: %.2f ms\n", scalar_ms);
    printf("AVX2 版本: %.2f ms\n", avx2_ms);
    printf("加速比: %.2fx\n", scalar_ms / avx2_ms);

    // verify the two results agree
    float max_diff = 0.0f;
    for (int i = 0; i < N * N; i++) {
        float diff = C1[i] - C2[i];
        if (diff < 0) diff = -diff;
        if (diff > max_diff) max_diff = diff;
    }
    printf("最大误差: %e\n", max_diff);

    _mm_free(A); _mm_free(B); _mm_free(C1); _mm_free(C2);
    return 0;
}
```

On an x86_64 machine (GCC 16.1, `-O3 -mavx2 -mfma`), the results are roughly: scalar version around 15 ms, hand-written AVX2/FMA version around 3 ms, a speedup of about 5x. But here's the key: compile the scalar version with `-O3 -mavx2 -mfma` too, and GCC's auto-vectorization optimizes it to roughly 4 ms. In other words, all the fussing with hand-written AVX2/FMA intrinsics bought only about 25% over what the compiler generated on its own.

::: details Actual verification results (Arch Linux WSL, GCC 16.1.1, -O3 -mavx2 -mfma)
In the verification environment, GCC 16.1's auto-vectorization is already so strong that the compiler automatically optimized the scalar version to nearly the level of the manual AVX2/FMA one; the measured speedup was only about 1.16x:

```text
scalar: 1.09 ms
avx2/fma: 0.94 ms
speedup: 1.16x
max_diff: 0.000000e+00
```

If anything, this further confirms the article's central claim: modern compilers' auto-vectorization keeps getting stronger, and the payoff from hand-written SIMD keeps shrinking. Exact numbers vary with hardware and compiler version, but the trend is consistent.

Verification code: [02-00-matmul-test.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/02-00-matmul-test.cpp)
:::

That is the difference between 2026 and the 1990s. In the 1990s, compilers had no idea what SIMD was, and hand-written assembly could be 10x faster; today the compiler is already quite smart, the payoff from hand-writing shrinks by the day, but the costs (readability, maintainability, portability) stay just as large.

## The Tools Change, but "Reality-Driven Learning" Never Does

Back to the talk's central thread: from assembly to C, from C to C++—neither step happened because "the new language was cooler", but because "the old approach could no longer hold up under the new constraints". C was chosen because of portability; C++ was embraced because it turned out C could do far more than "macro assembler" work. From this history we can draw one plain conclusion: **which tool you choose depends on what hurts most right now**. The pain was "rewrite everything on every machine change", so C was chosen. Later the pain became "I want to do more complex things, but C expresses them only with strain", so C++ was embraced. The tools change; the pattern of "learning driven by reality" never has.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup"
    title="Masterminds of Programming"
    publisher="O'Reilly Media"
    :year="2009"
    chapter="Chapter 1: C++ — 'you don't pay for what you don't use'"
    url="https://www.stroustrup.com/masterminds_chapter_1.pdf"
  />
  <ReferenceItem
    :id="2"
    author="Sinclair Research"
    title="ZX Spectrum"
    publisher="Sinclair Research"
    :year="1982"
    chapter="Zilog Z80A @ 3.5 MHz, 8-bit home computer"
    url="https://www.computerhistory.org/revolution/digital-logic/12/284/2334"
  />
  <ReferenceItem
    :id="3"
    author="AMD / System V"
    title="System V Application Binary Interface, AMD64 Architecture"
    publisher="x86-64 psABI"
    :year="2018"
    chapter="calling convention: RDI, RSI, RDX, RCX, R8, R9 for integer args"
    url="https://gitlab.com/x86-psABIs/x86-64-ABI"
  />
  <ReferenceItem
    :id="4"
    author="Microsoft"
    title="x64 Calling Convention"
    publisher="Microsoft Learn"
    :year="2024"
    chapter="integer args in RCX, RDX, R8, R9"
    url="https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention"
  />
  <ReferenceItem
    :id="5"
    author="W3C"
    title="WebAssembly Core Specification"
    publisher="W3C Recommendation"
    :year="2019"
    url="https://www.w3.org/2019/12/pressrelease-wasm-rec.html.en"
  />
  <ReferenceItem
    :id="6"
    author="Alon Zakai"
    title="Emscripten: An LLVM-to-JavaScript Compiler"
    publisher="Mozilla"
    :year="2011"
    url="https://emscripten.org/"
  />
  <ReferenceItem
    :id="7"
    author="Redwood Publishing"
    title="Acorn User"
    publisher="Redwood Publishing"
    :year="1982"
    chapter="British computer magazine for BBC Micro and Archimedes"
  />
  <ReferenceItem
    :id="8"
    author="Acorn Computers"
    title="Acorn Archimedes"
    publisher="Acorn Computers"
    :year="1987"
    chapter="ARM2 CPU, first production RISC-based personal computer"
    url="https://arstechnica.com/features/2020/12/how-an-obscure-british-pc-maker-invented-arm-and-changed-the-world/"
  />
  <ReferenceItem
    :id="9"
    author="Jarkko Oikarinen"
    title="Internet Relay Chat (IRC)"
    publisher="University of Oulu, Finland"
    :year="1988"
    chapter="RFC 1459"
  />
  <ReferenceItem
    :id="10"
    author="Matt Godbolt"
    title="C++: Some Assembly Required"
    publisher="CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=zoYT7R94S3c"
  />
  <ReferenceItem
    :id="11"
    author="John McCarthy"
    title="Recursive Functions of Symbolic Expressions and Their Computation by Machine, Part I"
    publisher="Communications of the ACM"
    :year="1960"
    chapter="first description of mark-sweep garbage collection"
    url="https://dl.acm.org/doi/10.1145/367177.367199"
  />
  <ReferenceItem
    :id="12"
    author="Roy Trubshaw & Richard Bartle"
    title="MUD (Multi-User Dungeon)"
    publisher="University of Essex"
    :year="1978"
    chapter="first multi-user virtual world; ancestor of all MUDs"
    url="https://www.mud.co.uk/richard/mudhist.htm"
  />
  <ReferenceItem
    :id="13"
    author="Matt Godbolt"
    title="Compiler Explorer"
    publisher="godbolt.org"
    :year="2012"
    chapter="interactive compiler output explorer"
    url="https://godbolt.org/"
  />
</ReferenceCard>

---
