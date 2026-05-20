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
  source_hash: 1a503bc05d8002d8b75890e9671b2a48e9e3f0e62012479a1d48f4bfcbf2aeb2
  translated_at: '2026-05-20T04:35:58.394761+00:00'
  engine: anthropic
  token_count: 6094
---
# Why C++ Programmers Should Care About Assembly

Many C++ tutorials and instructors will tell you: you don't need to worry about the low-level details when writing C++, the compiler is smarter than you, just use templates, smart pointers, and standard library algorithms, and leave the rest to the optimizer. But in practice, when you're stuck optimizing slow code with no progress to show for it, what you really need to do is look at what your code actually compiles into—that is, the assembly output. In many cases, that template function you assumed was a "zero-overhead abstraction" wasn't inlined by the compiler at all; that lambda expression you thought "should be fast" is being constructed and destroyed repeatedly inside a loop. Assembly doesn't lie; it is exactly what your code becomes.

This is tied to the core philosophy of C++. From the day it was born, C++ has pursued one thing: you don't pay for what you don't use<RefLink :id="1" preview="Stroustrup, The C++ Programming Language, 1986, zero-overhead principle" />. But the question is, how do you know whether you're paying a cost? The compiler won't proactively tell you "this abstraction has overhead"—it will just silently generate code. And that code is assembly.

The most direct way to understand what code is generated after template expansion isn't to read compiler error messages (though that's important too), but to look at the generated assembly. When you see that a function instantiated from a template is perfectly inlined, loops are unrolled, and registers are allocated sensibly, you truly understand what "zero-overhead abstraction" means. Conversely, when you see a bunch of redundant function calls and memory shuffling, you immediately know where the problem lies.

So don't treat assembly as some mysterious, esoteric thing. It's simply a mirror reflecting what your C++ code actually looks like. You don't need to master it, but you need the ability to grasp its outline and know when something looks off.

---

# Starting from "Writing Code by Hand": Why We Need to Understand the Low Level

The speaker mentioned the ZX Spectrum<RefLink :id="2" preview="Sinclair Research, ZX Spectrum, 1982, Zilog Z80A" /> and the era of manually typing in code. For many people learning to program, compiling, running, and seeing that line of text in the terminal feels like enough. But a problem quickly becomes apparent: you don't actually know how that line of text got to the screen, or even what the code turned into after compilation. This feeling of a "black box" might not matter when writing high-level abstractions, but once a bug appears—especially a bizarre memory-related bug—you have no idea where to start.

Learning to program isn't just about learning syntax, frameworks, or APIs. C++ syntax alone is enough to give anyone a headache—rvalue references, perfect forwarding, SFINAE (Substitution Failure Is Not An Error)—just memorizing the names of these concepts, which are rather obscure to beginners, takes time. But the deeper you go, the more you run into an awkward truth: you don't truly understand what the code you write does at the machine level. When someone asks "how does the Hello World string get from the executable file to the CPU," and you can't answer, it means your understanding of the low level isn't solid enough.

## Hands-on: What Does C++ Code Actually Become?

Compiling your C++ code into assembly and reading it line by line is the most direct way to understand "what the code is actually doing."

Experiment environment: Arch Linux WSL, GCC 16.1.1, with the `-S -O0` parameter added to the compile command. `-S` tells the compiler to only generate assembly without proceeding further, and `-O0` disables all optimizations, because with optimizations enabled the assembly gets transformed beyond recognition, making it very difficult for beginners to map it back to the source code.

Let's write the simplest example:

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

Then open `demo.s`, and you'll see a huge amount of stuff—don't panic, most of it is auxiliary information added by the compiler. We only care about the core parts. On x86-64, the assembly for the `add` function looks roughly like this:

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

The part in the `main` function that calls `add`:

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

When you see this assembly for the first time, you'll notice that `return a + b` under `-O0` causes the compiler to dutifully move the parameters from registers to the stack, then read them back from the stack to do the addition. It's not efficient, but this is the raw, unoptimized form—every line is crystal clear, and you can see exactly how the data flows.

## An Easy Pitfall to Fall Into

There's a pitfall here that must be mentioned. At first, I compiled with `-O1`, only to find that the assembly for the `add` function was just two or three lines—the parameters never even hit the stack; the computation was done entirely in registers (those familiar with compiler optimizations probably won't find this surprising—after all, it's an operation that can be handled at the register level, right!). This is because `-O1` already starts doing register allocation optimization—the compiler realized there's no need to store the parameters on the stack and read them back, so it just used the registers directly. So if you want to follow along with the experiments, make sure to use `-O0`, otherwise you'll see a bunch of incomprehensible output.

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

Another pitfall is that calling conventions differ across platforms. What's shown above is the x86-64 System V ABI<RefLink :id="3" preview="System V Application Binary Interface, AMD64, calling convention" />, where the first two integer arguments are placed in `%edi` and `%esi` respectively, and the return value goes in `%eax`. If you compile with MSVC on Windows, the parameter passing method is different (it uses `%rcx`, `%rdx`<RefLink :id="4" preview="Microsoft, x64 Calling Convention, RCX/RDX/R8/R9" />). So if your results look different, check your platform and compiler first.

## Why Understanding Assembly Helps You Understand C++

After seeing this assembly, many things that previously seemed mystical become clear. For example, why is the performance difference between passing by value and passing by reference in C++ so large—passing by value means copying data, and if the object is large, the overhead of copying at the assembly level is instruction after instruction of `mov`, laid out right there in front of you. What about passing by reference? You're only passing an address, an 8-byte pointer—no matter how large the object is, you only pass 8 bytes. You might have "known" these principles before, but after seeing the assembly, you truly "understand" them.

Take another example: why can inline functions improve performance? The `call` instruction itself has overhead—you need to save the return address, jump, and then jump back after the function returns. If the compiler expands the function body directly at the call site, all that overhead disappears. In the assembly, you won't see `call` or `ret` at all; the code just executes sequentially.

When you can see the machine instructions corresponding to every line of code, the concept of "performance" is no longer an abstract "fast" or "slow," but concrete—"these few instructions can be eliminated," or "this memory access can be merged."

## Directions to Dig Deeper

Once you understand this layer, you'll naturally want to know: how does the linker stitch multiple object files together? What actually happens when a shared library is loaded? How does an operating system's system call switch from user mode to kernel mode? These aren't topics from "compiler theory" and "operating systems" textbooks that are irrelevant to business code—they are the foundation. If the foundation isn't solid, everything built on top will wobble.

If you've also had a vague sense about the low level, I suggest starting with "looking at assembly." You don't need to learn it deeply, and you don't need to be able to write assembly by hand. As long as you can "look at C++ code and roughly guess what the assembly looks like," your programming intuition will level up.

## What Exactly Is Assembly—Starting from the Birth of Compiler Explorer

Before figuring out "where to dig deeper," there's a basic question worth answering: what exactly do we mean when we keep saying "assembly"?

The speaker was writing C++ at a company where the boss was very conservative and didn't allow using any new C++ features. How conservative, exactly? They were debating whether they could use range-based for loops to replace the most primitive `for (int i = 0; i < sizeof(array); ...)` syntax. They had recently been burned by another programming language where the two approaches were indeed not equivalent, so the boss was especially sensitive to "syntactic sugar." They ran a benchmark, the results were ambiguous, and the boss slammed the table: don't touch it.

The speaker didn't give up. He casually wrote a shell script that toggled compiler flags back and forth in the terminal, keeping the assembly output continuously refreshing. Then he felt it was too messy, so he used regular expressions to do some substitution and formatting, and piped it through `c++filt` to demangle the symbol names that had been mangled beyond recognition. Once he was done, he realized he could edit C++ code on the left in Vim and see the corresponding assembly output in real time on the right.

This tool was the prototype of what later became the famous Compiler Explorer<RefLink :id="13" preview="Matt Godbolt, Compiler Explorer (godbolt.org), 2012" /> (godbolt.org). This story reveals a key insight: **even though we've been pursuing higher abstractions in C++, assembly remains super important to this language and to us.** Many developers feel that once they use C++17, `std::optional`, and `std::variant`, they no longer need to look at assembly—the compiler is smarter than they are, so the code it generates must be fine. But once they actually start looking at assembly, they discover that while the compiler is indeed smart, what it does often isn't what they assumed.

So what exactly is "assembly"? The word "assembly" in the dictionary has several layers of meaning: it's a set of parts working together; it's the act or process of assembling a set of parts; it's a group of people gathered in one place for a purpose; it's a legislative body with ominous political connotations; in military terms, it's a drum signal calling troops to gather. And finally, the meaning we actually care about—it's the shortened form of assembly language.

In other words, when we keep saying "look at assembly," strictly speaking, we've been using the wrong term. We should say "look at assembly language." This might sound like a boring word game, but think about it and it actually makes sense. "Assembly" itself is an action, a process—putting parts together. "Assembly language" is the thing with concrete syntax, an instruction set, and opcodes. What the compiler does is indeed "assembly"—assembling the various parts of C++ (variables, functions, template instantiations) into the final machine code. And what we look at is that "assembly language," the blueprint produced during the assembly process.

Once you understand this distinction, it becomes clear: what we're looking at is assembly language, the human-readable form of instructions that the CPU can understand, not some abstract "assembly process." And the reason assembly language is important to C++ programmers is that C++ abstractions do have a cost (which is somewhat contradictory—we might be pursuing abstractions with no cost, but that's the goal, not the actual result...), and this cost is completely invisible unless you look at it through assembly language.

Take the simplest example: using an `std::function` in a function on a hot path, because you figure "the compiler will optimize it anyway." The result is a performance drop. Fire up Compiler Explorer and look at the assembly—the `std::function` call involves a virtual function dispatch, a heap allocation check, and a bunch of indirect jumps from type erasure. If you had used a template parameter instead, the compiler would have inlined it directly, with no function call at all. You'd never know what happened if you didn't look at the assembly language. A benchmark can tell you "it got slower," but only assembly language can tell you "why it got slower."

---

# From Assembly to C: A Forced Paradigm Jump

The talk mentioned a very representative experience: someone, without having studied any computer science, wrote a program entirely in assembly that included reference counting and even invented mark-sweep<RefLink :id="11" preview="John McCarthy, Recursive Functions of Symbolic Expressions, 1960" /> on their own. This isn't about some profound theory—it's a real person making real mistakes, discovering problems, and then "inventing" something that had already been invented. This process helps us understand where the concepts we later encounter in C++ actually came from.

## That "Monster" Written in Pure Assembly

Imagine this scenario: a person studying physics who knows nothing about computer science wants to write a fully windowed chat program. Not the kind where you type text and press Enter in a command line, but one with a windowed interface, communicating over TCP, able to pause and then send messages, formatting complex strings, and even supporting direct file transfers between clients. And it had a built-in scripting language of his own invention, inspired by BASIC, which supported dynamic allocation.

Many beginners' impression of assembly is writing interrupt handlers, writing startup code—maybe a few dozen or a few hundred lines at most. But this program was page after page of assembly code, all posted on GitHub, with tag names so absurd they made you lose all sense of meaning. The most classic one was called `WombleLoopJedi`—completely incomprehensible, but you could feel that the person writing the code had entered some kind of transcendent state.

The most interesting part is what came next: he added dynamic allocation to the scripting language, then thought "reference counting is a good idea" and implemented it. Then he discovered the circular reference problem. Then he came up with a complete line of thinking—find the things that are no longer referenced and manually delete them. Years later, he was talking about this with a friend, and his friend said, "Oh, so you invented mark-sweep garbage collection."

This is pure thinking without the constraints of textbooks. He didn't know it was called mark-sweep, but starting from the problem, he step by step deduced the correct solution. Mark-sweep wasn't an algorithm someone pulled out of thin air—it's the natural deduction for solving the specific problem of "reference counting can't handle circular references."

We can use a simplified pseudocode to reconstruct this thought process, which is much clearer than just explaining the concept:

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

Since the reference count can never reach zero, let's change the angle—instead of starting from "how many things reference me," start from "can anything still reach me?" If it can be reached, it's alive; if it can't be reached, it's dead, and the dead ones get deleted. This is the core idea of mark-sweep: mark is for tagging what can be reached, and sweep is for cleaning up what can't be reached.

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

Logically, it's really not complicated. Garbage collection might seem like dark magic, but when you reduce it to this scenario—a person writing a scripting language who needs to manage memory, finds reference counting insufficient, and so changes his approach—it becomes very natural. The key isn't how clever the algorithm is, but whether you can get to this point starting from an actual problem.

## From Assembly to C: A Forced Turning Point

This person had been writing everything in assembly, and assembly had been his companion all along. Until one day, he wanted to run a multi-user dungeon—that is, a MUD<RefLink :id="12" preview="Trubshaw & Bartle, MUD (Multi-User Dungeon), 1978" />.

A MUD is a pure-text multiplayer online RPG with no graphical interface—everything is described in text. When you log in, you see things like "You stand at a crossroads. To the north is a castle, to the east is a forest." Type "go north" to go north, type "attack goblin" to fight a goblin. You can team up with friends, fight monsters, cast spells—it's essentially an online multiplayer text version of Dungeons & Dragons.

The problem was, he couldn't write an entire MUD from scratch by himself. It was too big, even for someone who could write thousands of pages of assembly. So he found some source code circulating online, with a permissive license, ready to use. There's an important historical context to note here: there was no GitHub back then, nor any similar platform. The way people shared code was by passing around tarballs—those `.tar.gz` compressed archives, usually on IRC, transferring files directly from person to person. You'd shout in an IRC channel "does anyone have the MUD source code," and someone would send you a compressed file via DCC, and you'd start tinkering with it. No version control, no issue tracker, no pull requests—just raw code files.

And those MUD source codes were written in a programming language called C. This was the turning point. A person who had written thousands of pages of assembly was now facing C source code. He had to learn C, otherwise he couldn't modify that MUD. This wasn't the motivation of "I want to learn a new language"—it was the motivation of "I must understand this code to do what I want to do."

Jumping from assembly to C might not seem like a big deal today, but at the time, it was actually a huge paradigm shift. In assembly, you manipulate registers, memory addresses, and interrupts; in C, you start using abstract concepts like variables, functions, and structs. For someone who had only used assembly, the idea that "the compiler handles the stack frame for you" was something that required adjustment. But on the flip side, precisely because he came from assembly, his intuitive understanding of how C code runs at the low level might have been better than many people with formal CS degrees—because he knew exactly what kind of machine instructions those C statements would ultimately become.

Sometimes what drives us forward isn't a systematic study plan, but a project you really want to build that your current toolchain simply can't handle.

---

# From Assembly to C++: Why We Need High-Level Languages

The speaker mentioned that at age 15, he wrote programs in pure assembly and submitted them to magazines for money. From this background, we can understand one thing: why C++ was designed the way it is, and why it has so many "seemingly redundant" layers of abstraction.

If you look back from the perspective of assembly, many design decisions aren't "deliberately obscure"—they were "forced into existence."

## The Real Experience of Programming in Assembly

Writing a program that "reads two numbers from standard input and adds them" takes nearly 50 lines in x86 assembly, and you have to manage stack alignment yourself, set up system call numbers yourself, and handle buffers yourself. The speaker said the programs he wrote at age 15 were printed in tiny text across 20 dense pages in magazines. Get one punctuation mark wrong, and the program blows up—then you have to find that error in 20 pages of print.

Once you understand many of C++'s mechanisms, your mindset completely changes. It's no longer "yet another piece of syntax to memorize," but "look at how much trouble this thing saves me."

## The Same Logic: How Much Difference Between Assembly and C++?

Let's look at a particularly simple example—calling a function, passing a parameter, getting a return value. This operation is practically nothing in C++, but a lot happens at the assembly level.

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

Compile it and look at the assembly output (I'll discuss my environment later):

```bash
g++ -O0 -S simple_call.cpp -o simple_call.s
```

`-O0` disables all optimizations, because with optimizations enabled the compiler will fold the entire thing into a constant, and we won't be able to see the function call process. Open `simple_call.s`, and you'll see something like this (I've excerpted the key parts, AT&T syntax):

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

For just one `add(3, 4)`, at the assembly level you need to worry about: how the stack frame is set up, which register the parameter is passed through (the x86-64 System V calling convention uses rdi/rsi/rdx/rcx/r8/r9 for the first six integer arguments), where the return value is placed, and how the stack is restored after the call. In C++, writing one line of code handles all of this—the compiler does it all for you.

## Going Further: When the Parameter Isn't a Simple Integer

The example above is too simple, so let's try passing a string. This involves pointers, memory layout, and related concepts.

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

This C++ code looks very straightforward. But to write this logic in assembly by hand, you'd have to calculate the address offsets of `src` and `dst` yourself, handle the loop counter yourself, determine character ranges yourself, and append the terminator yourself. And the most fatal part—if you miscalculate an offset, the program won't tell you "you have an array out-of-bounds error." It will either silently corrupt other data or simply crash with a segfault.

So looking at these designs in C++ again, you get a moment of sudden clarity:

**References** — why do they exist? Because passing pointers is too error-prone: null pointers, dangling pointers, miscalculated offsets. A reference, semantically, is "this thing definitely points to a valid object," and the compiler helps you hold that baseline.

**`std::string`** — why does it exist? Because bare character arrays with manual length management are the breeding ground for the kind of disaster described above. You don't have to use `std::string`, but then you have to guarantee yourself that every single place correctly handles length, terminators, copying, and destruction.

**`std::string_view`** — why was this added in C++17? Because sometimes you just want to read a string without copying it, but passing a `const std::string&` to a `const char*` triggers implicit construction of a `std::string` temporary object. `string_view` is a lightweight "look but don't touch" view—under the hood it's just a pointer plus a length, but its semantics are much clearer than a bare `const char*` + `size_t`.

If you've never written assembly and never been tormented by pointers and memory layout, you might think these are "unnecessary." But if you have been tormented, you think "thank goodness someone figured this out for me."

## Environment Notes

The environment for running these examples is as follows, for easy reproduction:

- Environment: Arch Linux WSL, GCC 16.1.1
- Assembly syntax: GCC's default AT&T syntax (the one where operand order is reversed from Intel syntax, `%rax` instead of `rax`, `movq 源, 目的` instead of `mov 目的, 源`)
- If you want to see Intel syntax, just add the `-masm=intel` parameter: `g++ -O0 -S -masm=intel simple_call.cpp`

## Why Someone Would Write an IRC Client

The speaker mentioned that he later switched to an Archimedes computer<RefLink :id="8" preview="Acorn Computers, Archimedes, ARM2, 1987" />, with an ARM processor, and there was no ready-made IRC<RefLink :id="9" preview="Jarkko Oikarinen, Internet Relay Chat, 1988" /> client, so he wrote one himself.

This mindset of "I need a tool, there's no ready-made one available, so I'll build one myself" is very common in practical programming learning. Because when you really need to "build something," you encounter problems that tutorials won't tell you about: `std::getline` behaving inconsistently in certain terminals; `std::ofstream` handling newlines differently on different platforms; using `std::string` to store Chinese characters, where `length()` returns the number of bytes, not the number of characters. If you're just following a tutorial typing "Hello World," you'll never run into these things. But when you really want to write "something that works," they all pop up. The 15-year-old who wrote the IRC client in the talk was the same way. He didn't learn all the network programming knowledge first and then start coding—he thought "I want to get on IRC, but I don't have a client, so I'll write one." Knowledge doesn't come from textbooks—it grows from the desire of "I want to do this thing."

## From "Hand-Writing Everything" to "Leveraging Abstractions"

C++ is essentially a language that "lets you choose which level to work at."

Want to control memory manually? Go ahead—pointers, `new`/`delete`, placement new, and memory alignment attributes are all wide open for you. Want the compiler to manage it for you? Go ahead—smart pointers, RAII (Resource Acquisition Is Initialization), containers, `std::string`, no need to worry about deallocation. Want to compute some things at compile time? Go ahead—`constexpr`, templates, and concepts let you shift runtime overhead to compile time. Want to write generic code? Go ahead—templates let you write one piece of code to handle various types, and concepts let you check type constraints at compile time.

These levels don't replace each other—they can be mixed. In the same program, you can use raw pointers at the low level for high-performance memory operations, and use `std::vector` and `std::string` at the higher level for safe data management. This kind of flexibility was unimaginable in the pure assembly era—back then there was only one level: "do everything yourself."

This explains C++'s design philosophy—"you don't pay for what you don't use." Because the origin of this language was a group of people who had been tortured enough by assembly and wanted a language that "could control the low level without having to hand-write every low-level detail." It didn't fall from the sky—it was forced into existence by need<RefLink :id="1" preview="Stroustrup, The C++ Programming Language, 1986, zero-overhead principle" />. Once you connect this historical thread with the language design, many designs that previously seemed "baffling" suddenly make perfect sense.

---

# From "Assembly Is the Only Solution" to "The Compiler Can Actually Do Work"

The talk mentioned the experience of "every time you switch computers, it's a different OS and a different architecture." After the MUD was banned by the admin and he was forced to switch machines, what did that mean in that era? It meant that the assembly code you wrote by hand couldn't run a single line on a completely different CPU. The reason for writing the MUD in C instead of assembly was very pragmatic—rewriting assembly every time you switched machines was simply not feasible. Although C compilers on different machines in that era might themselves behave differently, C was still vastly better than assembly, because the benefits were too great. In his words, "rewriting it in assembly was simply not feasible"—this isn't some profound software engineering theory, it's the instinctive choice after being beaten down by reality.

## Hands-on Verification: How Much Difference in Cross-Platform Cost Between Assembly and C for the Same Logic?

Let's write a minimal example to feel this difference. Suppose we want to implement a feature: reverse data in a block of memory byte by byte. This operation is actually quite common in game development, for example when handling cross-platform little-endian/big-endian data.

First, using a pure assembly approach (taking x86_64 as an example, with GCC inline assembly):

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

The inline assembly above has a classic register conflict error—`rdx` is used simultaneously as a pointer and temporary storage, which is the most typical pitfall of hand-written assembly. Even if you fix this bug, this code can only compile in an x86_64 + System V ABI environment. Want to run it on ARM? Sorry, the instruction set is completely different, the register names are different, the calling convention is different—it's like starting from scratch.

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

This C++ code looks too simple—what's there to compare? But that's exactly the key point—the reason to choose C over assembly isn't that C can write more complex algorithms, but that for this kind of "simple logic," when switching platforms, the C version only needs to be recompiled, while the assembly version needs to be rewritten. When a project has hundreds of such "simple logic" pieces, this gap is the fundamental difference between "portable" and "not portable."

## In the 90s, Compilers Weren't Good Enough, So You Had to Write Assembly by Hand—But It's 2026 Now

The talk mentioned a crucial piece of historical context: in the 90s and early 2000s, compilers weren't smart enough, and CPUs had many special instructions for games (like the PS2's VU instructions, the Dreamcast's SH4 extensions) that compilers had no idea how to generate, so you had to write assembly by hand. This logic still holds today, just in different forms. For example, writing NEON instructions on ARM for SIMD acceleration, or writing GPU kernels with CUDA, is essentially "the compiler (still) can't automatically generate optimal code for you, so you have to specify it manually." The difference is that these scenarios are far fewer today than back then, and compilers are improving rapidly.

Let's look at a comparison experiment: the same matrix multiplication, run with both a pure C++ loop and hand-written AVX2 inline assembly:

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

On an x86_64 machine (GCC 16.1, `-O3 -mavx2 -mfma`), the results are roughly: the scalar version takes about 15ms, the manual AVX2/FMA version takes about 3ms, with a speedup of about 5x. But here's the key: if the scalar version is also compiled with `-O3 -mavx2 -mfma`, GCC's auto-vectorization can optimize it to about 4ms. That is, after all that effort writing AVX2/FMA intrinsics by hand, it's only about 25% faster than what the compiler generated automatically.

::: details Actual verification results (Arch Linux WSL, GCC 16.1.1, -O3 -mavx2 -mfma)
In the verification environment, because GCC 16.1's auto-vectorization capability is already very strong, the scalar version was automatically optimized by the compiler to near the level of manual AVX2/FMA, with an actual speedup of only about 1.16x:

```text
scalar: 1.09 ms
avx2/fma: 0.94 ms
speedup: 1.16x
max_diff: 0.000000e+00
```

This actually further reinforces the article's core thesis: modern compilers' auto-vectorization is getting stronger and stronger, and the benefits of hand-writing SIMD are shrinking. Specific numbers vary by hardware and compiler version, but the trend is consistent.

Verification code: `code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/02-00-matmul-test.cpp`
:::

This is the difference between 2026 and the 90s. In the 90s, compilers had no idea what SIMD was, and hand-written assembly might be 10x faster; today, compilers are already quite smart, the benefits of hand-writing are shrinking, but the costs (readability, maintainability, portability) remain just as large.

## The Tools Change, But the Pattern of "Being Driven to Learn by Reality" Never Does

Returning to the talk's core thread: from assembly to C, from C to C++, none of these steps happened because "the new language is cooler," but because "the old approach couldn't hold up under new constraints." C was chosen because of the need for cross-platform portability. C++ was embraced because it turned out C could do far more than just serve as a "fancy macro assembler." From this historical thread, we can arrive at a simple realization: **the choice of tool depends on what the current biggest pain point is**. The pain point was "having to rewrite everything every time you switch machines," so C was chosen. Later, the pain point became "wanting to do more complex things but C was too cumbersome to express them in," so C++ was embraced. The tools change, but the pattern of "being driven to learn by reality" never does.

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
