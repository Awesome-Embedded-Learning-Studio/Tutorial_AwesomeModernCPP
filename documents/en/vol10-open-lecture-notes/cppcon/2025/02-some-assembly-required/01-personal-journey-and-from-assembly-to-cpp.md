---
title: My Journey and the Awakening from Assembly to C++
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
order: 1
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/01-personal-journey-and-from-assembly-to-cpp.md
  source_hash: 6dafe831c94d103e7e1fa4397ff5dca81f053647301911f239d237e00900a422
  translated_at: '2026-06-13T11:46:21.095546+00:00'
  engine: anthropic
  token_count: 6122
---
# Why C++ Programmers Should Care About Assembly

Many C++ tutorials and teachers will tell you: when writing C++, you don't need to worry about the underlying details; the compiler is smarter than you. Just use templates, smart pointers, and standard library algorithms, and leave the rest to the optimizer. However, in practice, when you stare at slow code and optimize it repeatedly without seeing progress, what you actually need to do is look at what your code compiles into—that is, the assembly output. In many cases, that template function you thought was a "zero-overhead abstraction" wasn't inlined by the compiler at all; that lambda you thought "should be fast" is being constructed and destroyed repeatedly inside a loop. Assembly doesn't lie; it is exactly what your code becomes.

This is tied to the core philosophy of C++. From its inception, C++ has pursued one thing: you don't pay for what you don't use<RefLink :id="1" preview="Stroustrup, The C++ Programming Language, 1986, zero-overhead principle" />. But the question is, how do you know if you're paying a price? The compiler won't proactively tell you "this abstraction has a cost"; it will silently generate code. And that code is assembly.

The most direct way to understand what code is generated after template expansion is not to read compiler error messages (though that is important too), but to look at the generated assembly. When you see functions instantiated from templates perfectly inlined, loops unrolled, and registers allocated reasonably, you will truly understand what "zero-overhead abstraction" means. Conversely, when you see a bunch of redundant function calls and memory shuffling, you will immediately know where the problem lies.

So don't treat assembly as something mysterious. It is just a mirror reflecting exactly what your C++ code looks like. You don't need to master it, but you need to be able to read its outline and know when something looks wrong.

---

# Starting from "Hand-Coding": Why We Need to Understand the Underlying Layers

The speaker mentioned the ZX Spectrum<RefLink :id="2" preview="Sinclair Research, ZX Spectrum, 1982, Zilog Z80A" /> and the era of manually entering code. For many beginners, compiling, running, and seeing that line in the terminal feels like enough. But a problem quickly becomes apparent: you don't actually know how that line got to the screen, or even what the code turned into after compilation. This "black box feeling" might not matter when writing high-level abstractions, but once a bug appears—especially those weird memory-related bugs—you are helpless.

Learning programming isn't just about learning syntax, frameworks, or APIs. C++ syntax alone is enough to give a headache—rvalue references, perfect forwarding, SFINAE. Just memorizing the names of these obscure concepts takes time for beginners. But the deeper you go, the more you encounter an awkward fact: you don't truly understand what your code is doing at the machine level. When someone asks "How does the 'Hello World' string get from the executable file to the CPU?", if you can't answer, it means your understanding of the underlying layer isn't solid enough.

## Hands-on: What Does C++ Code Actually Become?

Compiling your C++ code into assembly and reading it line by line is the most direct way to understand "what the code is actually doing."

Experiment environment: Arch Linux WSL, GCC 16.1.1, with `-S -O0` added to the compile command. `-S` tells the compiler to only generate assembly and not proceed further. `-O0` turns off all optimizations, because with optimizations enabled, the assembly is altered beyond recognition, making it hard for beginners to map back to the source code.

Let's write a simplest example:

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

Then open `demo.s`. You will see a huge pile of stuff. Don't panic; most of it is auxiliary information added by the compiler. We only care about the core part. Under x86-64, the assembly for the `add` function looks roughly like this:

```asm
add(int, int):
    pushq   %rbp            ; 保存调用者的栈帧基址
    movq    %rsp, %rbp      ; 建立当前函数的栈帧
    movl    %edi, -4(%rbp)  ; 第一个参数 a 存到栈上
    movl    %esi, -8(%rbp)  ; 第二个参数 b 存到栈上
    movl    -4(%rbp), %edx  ; 把 a 读出来
    movl    -8(%rbp), %eax  ; 把 b 读出来
    addl    %edx, %eax      ; a + b，结果在 %eax 里
    popq    %rbp            ; 恢复调用者的栈帧基址
    ret                     ; 返回
```

The part in `main` where `add` is called:

```asm
main:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp       ; 在栈上分配 16 字节局部变量空间
    movl    $4, %esi        ; 第二个参数 4
    movl    $3, %edi        ; 第一个参数 3
    call    add(int, int)   ; 调用 add
    movl    %eax, -4(%rbp)  ; 返回值存到 result
    movl    -4(%rbp), %eax  ; return result
    leave                   ; 等价于 movq %rbp, %rsp; popq %rbp
    ret
```

When you see this assembly for the first time, you will notice: under `-O0`, the compiler honestly moves parameters from registers to the stack first, then reads them back from the stack to do addition. It's not efficient, but this is the original look without optimizations—every line is clear, and you can see how data flows.

## A Common Pitfall

There is a pitfall here I must warn you about. Initially, I used `-O1` to compile, only to find that the assembly for the `add` function was just two or three lines. The parameters never even hit the stack; the calculation was done directly in registers. (Friends familiar with compiler optimization probably won't feel anything about this—after all, it's something that can be operated on at the register level, right!). This is because `-O1` starts doing register allocation optimization—the compiler realized there's no need to store parameters to the stack and read them back, so it just used registers. So if you want to follow along with the experiment, make sure to use `-O0`, otherwise you will see a bunch of incomprehensible stuff.

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

Another pitfall is that calling conventions differ by platform. The example above shows the x86-64 System V ABI<RefLink :id="3" preview="System V Application Binary Interface, AMD64, calling convention" />, where the first two integer arguments are placed in `%edi` and `%esi`, and the return value is in `%eax`. If you compile on Windows with MSVC, the way parameters are passed is different (it uses `%rcx`, `%rdx`<RefLink :id="4" preview="Microsoft, x64 Calling Convention, RCX/RDX/R8/R9" />). So if the results look different, check your platform and compiler first.

## Why Understanding Assembly Helps You Understand C++

After seeing this assembly, many things that previously seemed mysterious become clear. For example, why is the performance difference between passing by value and passing by reference in C++ so huge? Passing by value means copying data. If the object is large, the cost of copying at the assembly level is line after line of `mov` instructions, laid out clearly. Passing by reference? You just pass an address, an 8-byte pointer. No matter how big the object is, you pass 8 bytes. You might have "known" these principles before, but after seeing assembly, you "understand" them.

Another example is why inline functions improve performance: the `call` instruction itself has overhead—saving the return address, jumping, and jumping back after the function returns. If the compiler expands the function body directly at the call site, this overhead disappears completely. In the assembly, you won't see `call` or `ret`; the code just executes sequentially.

When you can see the machine instructions corresponding to every line of code, the concept of "performance" is no longer an abstract "fast" or "slow", but concrete "these instructions can be saved" or "this memory access can be merged".

## Directions to Dig Deeper

After figuring out this layer, you will naturally wonder: how does the linker stitch multiple object files together? What actually happens when a dynamic library is loaded? How do operating system system calls switch from user mode to kernel mode? These things aren't irrelevant content in "Compilers" and "Operating Systems" textbooks—they are the foundation. If the foundation is unstable, everything built on top will wobble.

If you also have a vague feeling about the low-level, I suggest starting with "looking at assembly". You don't need to learn very deeply; you don't need to be able to write assembly by hand. As long as you can "see C++ code and roughly guess what the assembly looks like", your programming intuition will move up a level.

## What Exactly is Assembly—Starting with the Birth of Compiler Explorer

Before figuring out "digging deeper", there is a basic question worth answering: what exactly do we mean by "assembly"?

The speaker was writing C++ at a company where the boss was very conservative and didn't allow using any new C++ features. How conservative? They were arguing whether they could use range-based for loops to replace the most primitive `for (int i = 0; i < sizeof(array); ...)` style. They had just been burned by another programming language where these two styles were indeed not equivalent, so the boss was very sensitive to "syntactic sugar". They ran a benchmark, but the results were ambiguous. The boss slammed the table: don't touch it.

The speaker didn't give up. He casually wrote a shell script, switching compile options in the terminal, causing the assembly output to refresh continuously. Then he thought it was too messy, so he used regex to do some replacement and formatting, and piped it through `c++filt` to restore those symbol names mangled by name mangling. After finishing, he discovered: he could edit C++ code on the left in Vim and see the corresponding assembly output on the right in real-time.

This tool was the prototype of the later famous Compiler Explorer<RefLink :id="13" preview="Matt Godbolt, Compiler Explorer (godbolt.org), 2012" /> (aka godbolt.org). This story reveals a key realization: **even though we constantly pursue higher abstractions in C++, assembly is still super important to this language and to us.** Many developers think that using C++17, `std::optional`, and `std::variant` means they don't need to look at assembly; the compiler is smarter than them, so the generated code must be fine. But only after actually looking at assembly do they realize that while the compiler is indeed smart, what it does is often different from what they assumed.

So what exactly is "assembly"? The dictionary definition of "assembly" has several layers: it is a set of parts working together; it is the act or process of assembling parts together; it is a group of people gathered for a purpose; it is a legislature with ominous political overtones; in the military, it is a drum signal calling an army to gather. Finally, there is the meaning we actually care about—it is the shorthand form of assembly language.

In other words, when we say "look at assembly", strictly speaking, we are using the wrong term. We should say "look at assembly language". This sounds like a boring word game, but think about it—it actually makes sense. "Assembly" itself is an action, a process—putting parts together. "Assembly language" is the thing with specific syntax, an instruction set, and opcodes. What the compiler does is indeed "assembly"—assembling the various parts of C++ (variables, functions, template instantiations) into the final machine code. What we look at is that "assembly language", the blueprint produced during the assembly process.

Once this distinction is clear, we can understand: we are looking at assembly language, the human-readable form of instructions that the CPU understands, not some abstract "assembly process". The reason assembly language is important to C++ programmers is that C++ abstractions have a cost (paradoxically, we might be pursuing abstractions with no cost, but that is the goal, not the actual result...), and this cost is invisible without looking at assembly language.

Here is the simplest example: using a `std::function` in a function on a hot path, thinking "the compiler will optimize it anyway". The result was a performance drop. Looking at the assembly in Compiler Explorer—the call to `std::function` involved a virtual function dispatch, a heap allocation check, and a bunch of type-erased indirect jumps. If a template parameter was used directly, the compiler inlined it directly, with no function call at all. Without looking at assembly language, you would never know what happened. A benchmark can tell you "it got slower", but only assembly language can tell you "why it got slower".

---

# From Assembly to C: A Forced Paradigm Jump

The talk mentioned a very representative experience: someone, without any computer science background, wrote a program purely in assembly that included reference counting and even invented mark-sweep<RefLink :id="11" preview="John McCarthy, Recursive Functions of Symbolic Expressions, 1960" /> garbage collection themselves. This isn't about high theory; it's a real person stepping into real pitfalls, discovering problems, and then "inventing" something that had already been invented. This process helps us understand how the concepts we later encounter in C++ came to be.

## That "Monster" Written in Pure Assembly

Imagine this scene: a person studying physics, knowing nothing about computer science, wants to write a full-windowed chat program. Not the kind where you type text and hit enter in a command line, but one with a windowed interface, communicating via TCP, capable of pausing to send messages, formatting complex strings, and supporting direct file transfer between clients. It even has a built-in scripting language of his own invention, inspired by BASIC, which supports dynamic allocation.

Many beginners' impression of assembly is writing interrupt handlers or startup code, maybe dozens or hundreds of lines at most. But this program is page after page of assembly code, all hosted on GitHub, with tag names so ridiculous they lose all meaning—the most classic one being `WombleLoopJedi`—no idea what it means, but you can feel the person writing the code was in some kind of metaphysical state.

The most interesting part is this: he added dynamic allocation to the scripting language, then thought "reference counting is a good idea", so he implemented reference counting. Then he discovered the circular reference problem. Then he came up with a complete idea—find those things that are no longer referenced and manually delete them. Years later, chatting with a friend, the friend said, "Oh, so you invented mark-sweep garbage collection."

This is pure thinking without the constraints of textbooks. He didn't know it was called mark-sweep, but starting from the problem, he step-by-step derived the correct solution. Mark-sweep wasn't an algorithm someone came up with out of thin air; it is the natural derivation for solving the specific problem "reference counting can't handle circular references".

We can use a simplified pseudocode to reconstruct this thought process, which is much clearer than just explaining concepts:

```cpp
// 第一阶段：引用计数（能想到的第一步）
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
        // 没人引用了，删掉
        free(obj);
    }
}

// 然后他遇到了这个问题：
// A 引用 B，B 引用 A
// A.ref_count = 1, B.ref_count = 1
// 但外面已经没有任何东西引用 A 或 B 了
// 它们永远不会被释放 —— 这就是循环引用
```

Since reference counting can't reach zero, let's change the angle—instead of starting from "how many things reference me", start from "is there anything that can still reach me". Those that can be reached are alive; those that cannot are dead, and the dead ones are deleted. This is the core idea of mark-sweep. Mark marks the reachable, sweep sweeps away the unreachable.

```cpp
// 第二阶段：他"发明"的 mark-sweep（概念还原）
// 假设我们有一组根对象（全局变量、栈上的局部变量等）
// 从根出发，能遍历到的都是活的

Object* roots[64];  // 根集合
Object* all_objects[1024];  // 所有已分配的对象
int all_count = 0;
bool marked[1024];  // 标记数组

// mark 阶段：从根出发，递归标记所有可达对象
void mark(Object* obj) {
    // 找到 obj 在 all_objects 中的索引
    for (int i = 0; i < all_count; i++) {
        if (all_objects[i] == obj && !marked[i]) {
            marked[i] = true;
            // 假设对象里存了指向其他对象的引用
            // 递归标记它引用的所有对象
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

// sweep 阶段：遍历所有对象，没被标记的就是垃圾
void sweep() {
    for (int i = 0; i < all_count; i++) {
        if (!marked[i]) {
            free(all_objects[i]);
            all_objects[i] = nullptr;
        }
        marked[i] = false;  // 重置标记，为下一轮做准备
    }
}

// 完整的 GC 周期
void garbage_collect() {
    mark_all_roots();
    sweep();
}
```

Logically, it's really not complex. Garbage collection looks like black magic, but reducing it to this scenario—a person writing a scripting language, needing to manage memory, reference counting isn't enough, so change the angle—it becomes very natural. The key isn't how clever the algorithm is, but whether you can get to this point starting from a real problem.

## From Assembly to C: A Forced Turn

This person kept writing things in assembly, and assembly stayed with him all the way. Until one day, he wanted to run a Multi-User Dungeon, a MUD<RefLink :id="12" preview="Trubshaw & Bartle, MUD (Multi-User Dungeon), 1978" />.

A MUD is a purely text-based multiplayer online RPG with no graphical interface; everything is described in text. You log in and see "You are standing at a crossroads. To the north is a castle, to the east is a forest." You type "go north" to go north, "attack goblin" to hit a goblin. You can team up with friends, fight monsters, cast spells—essentially it's the online multiplayer version of "Dungeons & Dragons" in text.

The problem was, he couldn't write a whole MUD from scratch by himself. It was too big, even for someone who could write thousands of pages of assembly. So he found some source code online, the license was fine, and he could use it directly. Note the historical context here: there was no GitHub then, nor any similar platform. The way people shared code was passing tarballs—those `.tar.gz` compressed archives, usually on IRC, transferring files directly from person to person. Shouting in an IRC channel "Who has the MUD source code?", then someone sends a compressed file via DCC, and you get the archive and start tinkering. No version control, no issue tracker, no pull requests, just naked code files.

And those MUD source codes were written in a programming language called C. This was the turning point. A person who had written thousands of pages of assembly was now facing a piece of C language code. He had to learn C, otherwise he couldn't modify that MUD. This wasn't the motivation of "I want to learn a new language", but "I must understand this code to do what I want to do".

Jumping from assembly to C might not seem like much today, but at the time, it was a huge paradigm jump. In assembly, you manipulate registers, memory addresses, and interrupts. In C, you start using abstract concepts like variables, functions, and structs. For someone who always used assembly, the idea that "the compiler handles the stack frame for you" required adaptation. But conversely, because he came from assembly, his intuitive understanding of how C code runs at the bottom level might be better than many CS graduates—because he knows what machine instructions those C statements eventually turn into.

Sometimes what drives us forward is not a systematic study plan, but a specific project we really want to do but can't handle with our current toolchain.

---

# From Assembly to C++: Why We Need High-Level Languages

The speaker mentioned he wrote programs in pure assembly at 15 to submit to magazines for money. From this background, we can understand one thing: why the C++ language is designed the way it is, and why it has so many "seemingly superfluous" layers of abstraction.

If you look back from the perspective of assembly, many design decisions aren "deliberately mysterious", but "forced out".

## The Practical Experience of Assembly Programming

Writing a program that "reads two numbers from standard input and adds them" takes nearly 50 lines in x86 assembly, plus you manage stack alignment yourself, fiddle with system call numbers yourself, and handle buffers yourself. The speaker said the programs he wrote at 15 were published in magazines, 20 pages of tiny text densely packed. Type one punctuation mark wrong, the program crashes, and then you have to find that error in 20 pages of print.

Understanding many of C++'s mechanisms completely changes your mindset. It's not "another syntax to memorize", but "how much trouble this thing saved me".

## How Different Are Assembly and C++ for the Same Logic?

Let's look at a very simple example—calling a function, passing a parameter, and getting a return value. This operation is nothing in C++, but a lot happens at the assembly level.

```cpp
// simple_call.cpp
// 就是最简单的函数调用，传参，返回
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}
```

Compile and look at the assembly output (I'll discuss my environment later):

```bash
g++ -O0 -S simple_call.cpp -o simple_call.s
```

`-O0` turns off all optimizations, because with optimizations on, the compiler will fold the whole thing into a constant, and we won't see the function call process. Open `simple_call.s`, and you will see something like this (I've captured the key part, AT&T syntax):

```asm
add(int, int):
    pushq   %rbp            ; 保存调用者的栈帧基址
    movq    %rsp, %rbp      ; 建立自己的栈帧
    movl    %edi, -4(%rbp)  ; 第一个参数 a 存到栈上
    movl    %esi, -8(%rbp)  ; 第二个参数 b 存到栈上
    movl    -4(%rbp), %edx  ; 把 a 加载到 edx
    movl    -8(%rbp), %eax  ; 把 b 加载到 eax
    addl    %edx, %eax      ; eax = edx + eax，即 a + b
    popq    %rbp            ; 恢复调用者的栈帧基址
    ret                     ; 返回，返回值在 eax 里

main:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp       ; 在栈上分配 16 字节空间
    movl    $4, %esi        ; 第二个参数放 esi
    movl    $3, %edi        ; 第一个参数放 edi
    call    add(int, int)   ; 调用函数
    movl    %eax, -4(%rbp)  ; 把返回值存到 result 的位置
    movl    -4(%rbp), %eax  ; 把 result 作为 main 的返回值
    leave
    ret
```

Just for one `add(3, 4)`, at the assembly level you have to care about: how the stack frame is built, which register the parameter is passed through (x86-64 System V calling convention is rdi/rsi/rdx/rcx/r8/r9 for the first six integer arguments), where the return value is placed, and how the stack is restored after the call. In C++, writing one line of code handles all this; the compiler does it all for you.

## Going Further: When Parameters Aren't Simple Integers

The example above is too simple. Let's try passing a string. This involves pointers, memory layout, and such.

```cpp
// string_call.cpp
#include <cstring>

// 模拟一个简单的字符串处理：把输入字符串全部转大写
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

This C++ code looks straightforward. But to write this logic in assembly by hand, you have to calculate address offsets for `src` and `dst` yourself, handle loop counters yourself, judge character ranges yourself, and pad the terminator yourself. And the most deadly thing is—if you calculate an offset wrong, the program won't tell you "you array out of bounds"; it will either silently corrupt other data or just segfault and crash.

So looking at these designs in C++ again, you get an epiphany:

**References** Why do they exist? Because passing pointers is too error-prone: null pointers, dangling pointers, miscalculating offsets. References semantically mean "this thing definitely points to a valid object", and the compiler helps you guard this bottom line.

**`std::string`** Why does it exist? Because bare char arrays plus manual length management are the breeding ground for the disaster above. You don't have to use `std::string`, but you have to guarantee that every single place correctly handles length, terminators, copying, and destruction.

**`std::string_view`** Why did C++17 add it? Because sometimes you just want to read a string without copying, but passing `const std::string&` into `const char*` triggers an implicit `std::string` temporary object construction. `string_view` is a lightweight "I look but don't touch" view; underneath it's just a pair of pointers plus a length, but the semantics are much clearer than bare `const char*` + `size_t`.

If you haven't written assembly and haven't been tortured by pointers and memory layout, you might think these are "gilding the lily". But if you have been tortured, you think "thank god someone figured this out for me".

## Environment Description

The environment for running these examples is as follows, for easy reproduction:

- Environment: Arch Linux WSL, GCC 16.1.1
- Assembly syntax: GCC's default AT&T syntax (the one where operand order is reversed from Intel syntax, `%rax` instead of `rax`, `movq 源, 目的` instead of `mov 目的, 源`)
- If you want to see Intel syntax, just add the `-masm=intel` parameter: `g++ -O0 -S -masm=intel simple_call.cpp`

## Why Someone Would Write an IRC Client

The speaker mentioned he later switched to an Archimedes computer<RefLink :id="8" preview="Acorn Computers, Archimedes, ARM2, 1987" />, with an ARM processor, and there was no ready-made IRC<RefLink :id="9" preview="Jarkko Oikarinen, Internet Relay Chat, 1988" /> client, so he wrote one himself.

This mindset of "I need a tool, but there isn't one, so I'll build one" is very common in actual programming learning. Because when you really need to "build something", you encounter problems tutorials won't tell you about: `std::getline` behaves inconsistently under certain terminals; `std::ofstream` handles newlines differently on different platforms; using `std::string` to store Chinese, `length()` returns bytes not characters. If you just follow tutorials typing "Hello World", you'll never hit these. But when you really want to write "something that works", they all pop up. The 15-year-old who wrote the IRC client in the talk was the same. He didn't learn all network programming knowledge before starting; he thought "I want to get on IRC, but I don't have a client, so I'll write one". Knowledge doesn't come from textbooks; it grows from the desire of "I want to do this".

## From "Hand-Coding Everything" to "Leveraging Abstractions"

C++ is essentially a language that "lets you choose which level to work at".

Want to control memory manually? You can—pointers, `new`/`delete`, placement new, memory alignment attributes, all open to you. Want the compiler to manage it for you? You can—smart pointers, RAII, containers, `std::string`, don't worry about freeing. Want to calculate things at compile time? You can—`constexpr`, templates, concepts, move runtime overhead to compile time. Want to write generic code? You can—templates let you write one code for various types, concepts let you check type constraints at compile time.

These levels aren't mutually exclusive; they can be mixed. You can be in the same program, using raw pointers at the bottom for high-performance memory operations, and using `std::vector` and `std::string` at the top for safe data management. This flexibility was unimaginable in the pure assembly era—back then there was only one level: "do everything yourself".

This explains C++'s design philosophy—"you don't pay for what you don't use". Because the background of the language's creation was a group of people tortured by assembly who wanted a language that "could control the low level but didn't require hand-writing every low-level detail". It didn't fall from the sky; it was forced out by need<RefLink :id="1" preview="Stroustrup, The C++ Programming Language, 1986, zero-overhead principle" />. Connecting this history with language design, many designs that previously seemed "baffling" suddenly become logical.

---

# From "Assembly is the Only Solution" to "The Compiler Can Actually Do the Work"

The talk mentioned the experience of "every time I switch computers, it's a different OS and architecture". Back when the MUD was banned by the admin and he was forced to switch machines, what did that mean in that era? It meant your hand-written assembly code wouldn't run a single line on a completely different CPU. Writing the MUD in C instead of assembly was for a very simple reason—rewriting assembly every time you switched machines was simply impossible. Although C compilers on different machines in that era might behave differently, C was still way better than assembly because the benefits were huge. In his words, "rewriting in assembly is simply impossible"—this isn't some high software engineering theory, just an instinctive choice after being beaten by reality.

## Hands-on Verification: How Much Difference is There in Cross-Platform Costs Between Assembly and C for the Same Logic?

Let's write a minimal example to feel this difference. Suppose we want to implement a feature: reverse data in a segment of memory by byte. This operation is actually common in game development, for example, handling cross-platform little-endian/big-endian data.

First, let's write it using pure assembly thinking (taking x86_64 as an example, using GCC inline assembly):

```cpp
// reverse_asm.cpp
// 注意：这段代码只能在 x86_64 的 GCC/Clang 下编译
// 换到 ARM？换到 MSVC？直接报错，一个字都跑不了

#include <cstdint>
#include <cstdio>
#include <cstring>

void reverse_bytes_asm(void* data, size_t len) {
    // rdi = data, rsi = len（System V ABI 的参数传递约定）
    __asm__ __volatile__(
        "test %rsi, %rsi\n\t"       // 如果 len == 0，直接返回
        "jz 2f\n\t"
        "mov %rdi, %rax\n\t"        // rax = 起始地址
        "lea -1(%rdi, %rsi), %rdx\n" // rdx = 末尾地址 (data + len - 1)
        "1:\n\t"
        "cmp %rax, %rdx\n\t"        // 左右指针相遇了吗？
        "jge 2f\n\t"                // 相遇或交叉就结束
        "movb (%rax), %cl\n\t"      // cl = *left
        "movb (%rdx), %dl\n\t"      // dl = *right（注意这里把 rdx 的值覆盖了！）
        // rdx 既当指针又当临时变量——手写汇编最容易踩的坑，寄存器分配全靠脑子记
        "movb %cl, (%rdx)\n\t"      // *right = cl（但 rdx 已经被破坏了！）
        "movb %dl, (%rax)\n\t"      // 这行也是错的
        "inc %rax\n\t"
        "dec %rdx\n\t"
        "jmp 1b\n\t"
        "2:\n\t"
        : /* 没有输出操作数 */
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

The inline assembly above has a classic register conflict error—`rdx` is used as both a pointer and temporary storage, which is the most typical pitfall of hand-written assembly. Even if you fix this bug, this code can only compile in an x86_64 + System V ABI environment. If you want to run it on ARM? Sorry, the instruction set is completely different, register names are different, and the calling convention is different—start writing from scratch.

Now let's write the same logic in pure C++:

```cpp
// reverse_cpp.cpp
// 这段代码可以在任何有 C++ 编译器的平台上编译
// x86_64、ARM、RISC-V、MIPS……随便你

#include <cstdint>
#include <cstdio>
#include <utility>

void reverse_bytes_cpp(void* data, size_t len) {
    if (len == 0) return;

    auto* bytes = static_cast<uint8_t*>(data);
    size_t left = 0;
    size_t right = len - 1;

    while (left < right) {
        // std::swap 在 C++11 就有了，底层会被编译器优化成寄存器交换
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

This C++ code looks too simple, what is there to compare? But the key point is here—choosing C over assembly isn't because C can write more complex algorithms, but because this "simple logic" only needs recompiling when switching platforms, whereas the assembly version needs rewriting. When a project has hundreds of these "simple logics", this gap is the fundamental difference between "portable" and "not portable".

## Compilers in the 90s Were Bad, So You Had to Write Assembly by Hand—But Now It's 2026

The talk mentioned a very key historical background: in the 90s and early 2000s, compilers weren't smart enough. CPUs had many special instructions for games (like PS2's VU instructions, Dreamcast's SH4 extensions), and compilers didn't know how to generate these instructions at all, so you had to write assembly by hand. This logic still holds today, just the form has changed. For example, writing NEON instructions on ARM for SIMD acceleration, or writing GPU kernels in CUDA, is essentially "the compiler (still) can't automatically generate optimal code for you, so you have to specify it manually". The difference is that these scenarios are much rarer today than back then, and compilers are improving rapidly.

Let's look at a comparison experiment, the same matrix multiplication, running with pure C++ loops versus hand-written AVX2 inline assembly:

```cpp
// matmul_test.cpp
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <immintrin.h>  // AVX2 内联函数头文件

// 纯 C++ 标量版本
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

// 用 AVX2/FMA 内联函数版本（不是纯汇编，但思路类似——手动指定 SIMD 指令）
void matmul_avx2(const float* A, const float* B, float* C, int N) {
    memset(C, 0, N * N * sizeof(float));
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            float aik = A[i * N + k];
            __m256 vaik = _mm256_set1_ps(aik);  // 把 aik 广播到 8 个 float
            for (int j = 0; j < N; j += 8) {  // N=256 是 8 的倍数，无需 tail 处理
                __m256 vb = _mm256_loadu_ps(&B[k * N + j]);  // 加载 B 的 8 个元素
                __m256 vc = _mm256_loadu_ps(&C[i * N + j]);  // 加载 C 的当前值
                vc = _mm256_fmadd_ps(vaik, vb, vc);          // vc += vaik * vb
                _mm256_storeu_ps(&C[i * N + j], vc);         // 存回去
            }
        }
    }
}

// 简单的计时辅助
#include <chrono>
using Clock = std::chrono::high_resolution_clock;

int main() {
    const int N = 256;  // 256x256 矩阵
    // 对齐分配，方便 AVX2 加载
    float* A = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* B = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* C1 = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* C2 = (float*)_mm_malloc(N * N * sizeof(float), 32);

    // 填充随机数据
    for (int i = 0; i < N * N; i++) {
        A[i] = static_cast<float>(rand()) / RAND_MAX;
        B[i] = static_cast<float>(rand()) / RAND_MAX;
    }

    // 测试标量版本
    auto t1 = Clock::now();
    matmul_scalar(A, B, C1, N);
    auto t2 = Clock::now();
    auto scalar_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    // 测试 AVX2 版本
    t1 = Clock::now();
    matmul_avx2(A, B, C2, N);
    t2 = Clock::now();
    auto avx2_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    printf("标量版本: %.2f ms\n", scalar_ms);
    printf("AVX2 版本: %.2f ms\n", avx2_ms);
    printf("加速比: %.2fx\n", scalar_ms / avx2_ms);

    // 验证结果一致性
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

On an x86_64 machine (GCC 16.1, `-O3 -mavx2 -mfma`), the result is roughly: scalar version about 15ms, AVX2/FMA manual version about 3ms, speedup about 5x. But the key is, if the scalar version is also compiled with `-O3 -mavx2 -mfma`, GCC's auto-vectorization can optimize it to about 4ms. That is, hand-writing AVX2/FMA intrinsics for a long time only yielded about a 25% speedup over the compiler's auto-generated code.

::: details Actual Verification Results (Arch Linux WSL, GCC 16.1.1, -O3 -mavx2 -mfma)
In the verification environment, due to GCC 16.1's strong auto-vectorization capabilities, the scalar version was automatically optimized by the compiler to close to the manual AVX2/FMA level, with an actual speedup of only about 1.16x:

```text
scalar: 1.09 ms
avx2/fma: 0.94 ms
speedup: 1.16x
max_diff: 0.000000e+00
```

This further confirms the article's core point: modern compilers' auto-vectorization is getting stronger, and the benefits of hand-writing SIMD are shrinking. Specific numbers vary by hardware and compiler version, but the trend is consistent.

Verification code: [02-00-matmul-test.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/02-00-matmul-test.cpp)
:::

This is the difference between 2026 and the 90s. In the 90s, compilers had no idea what SIMD was, and hand-writing assembly might be 10x faster; today, compilers are quite smart, and the benefits of hand-writing are getting smaller, but the cost (readability, maintainability, portability) remains huge.

## Tools Change, But the "Learning Driven by Reality" Mode Has Never Changed

Returning to the core thread of the talk: from assembly to C, from C to C++, every step wasn't because "the new language is cooler", but because "the old solution couldn't hold up under new constraints". Choosing C was for cross-platform compatibility. Accepting C++ was discovering that C could do much more than just "macro assembler" work. From this historical thread, we get a simple realization: **the choice of tool depends on what the current biggest pain point is**. The pain point was "rewriting every time I switch machines", so we chose C. Later the pain point became "wanting to do more complex things but C is too hard to express", so we accepted C++. Tools change, but the mode of "being driven to learn by reality" has never changed.

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
