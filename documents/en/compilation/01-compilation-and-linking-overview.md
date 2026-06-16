---
chapter: 13
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 32
tags:
- cpp-modern
- host
- intermediate
title: 'Deep Dive into C/C++ Compilation and Linking: An Introduction'
description: ''
translation:
  source: documents/compilation/01-compilation-and-linking-overview.md
  source_hash: bbdb171043d85f137308404e077cc2ee8230fb0ab5ef89d11a50885eb1fdd45f
  translated_at: '2026-06-16T03:28:55.663471+00:00'
  engine: anthropic
  token_count: 5806
---
# Deep Dive into C/C++ Compilation and Linking: Introduction

## Preface

This is a new series! It is a topic I plan to research systematically this week. Specifically, we will discuss and summarize a series of topics in C/C++ programming that we often gloss over but which frequently cause us grief—compilation and linking technologies. I believe anyone has encountered headaches like `undefined reference to ...` errors. I know seeing such errors can give many folks a fright (I was recently tormented by template instantiation errors myself).

When solving these problems, I believe many friends initially panic and ask AI or search the web, but few truly think—why do we get these kinds of errors? Putting aside those times when we actually forget to provide source files in the build system (I believe many have encountered this, myself included), in many cases, we actually *did* provide the source file—or at least we think we did—and you even saw it link, but it still failed.

For example, suppose you write code in a `lib.c` file and turn it into a static library `libutils`.

```c
// lib.c
int add(int a, int b) {
    return a + b;
}
```

```bash
gcc -c lib.c -o lib.o
ar rcs libutils.a lib.o
```

Then, we immediately use `add` in a C++ file.

```cpp
// usage.cpp
extern int add(int a, int b);

int main() {
    return add(1, 2);
}
```

Then, we type this command expecting our program to compile successfully, but we get a very strange error:

```text
g++ usage.cpp -L. -lutils -o app
# /usr/bin/ld: /tmp/ccXXX.o: in function `main':
# usage.cpp:(.text+0x10): undefined reference to `add(int, int)'
# collect2: error: ld returned 1 exit status
```

This looks too strange. We clearly linked `libutils`, and it even found our library (it didn't complain about `cannot find -lutils`, which means it found it), so why did it error? And even if it couldn't find the symbol, why didn't it complain during compilation? I think if, like the author of [Linkers and Loaders](https://www.lurklurk.org/linkers/linkers.html), you see the problem immediately, then this introductory "Deep Dive into C/C++ Compilation and Linking: Introduction" holds nothing new for you. We will discuss every detail in depth later, but not here.

**This blog post assumes you have at least written C programs (although the problem above involves C++, the core of this article is not C++). If you have encountered `undefined reference` errors and didn't know how to solve them, even better.**

## So, what do the variables and functions we write actually mean?

This question is not for **you**; we are asking **the computer**. To answer the chain of questions you may never have thought of, we must first answer one question—"How does the computer know the things we find or can't find?" To be more precise—how does the compiler toolchain collect and find symbols? How does it transform them into a more manageable form (for example, we map functions to addresses the computer can find; those familiar with assembly will immediately think of how functions work—once the function name is converted to an address, the computer simply calls (jumps to) that address to fetch instructions and execute code). Ultimately, our first step is—how do the variables and functions we understand, which express business logic, transform into addresses where the computer knows what is where? What happens in the middle? **What do the variables and functions we write actually mean to the computer?**

Any computer science student can undoubtedly immediately spit out the four classic steps of a program from source code to running on an operating system—preprocessing, compilation, linking, and **execution** (someone might ask, isn't that nonsense? Why single out execution? Good question! We will talk about dynamic loading and startup loading of dynamic libraries).

To answer the above question well, we need to focus on the latter three (preprocessing is **source code to source code transformation**, such as `#define` expansion and conditional compilation based on `#if`, which we won't discuss here).

When we write C language files—whether it's Bilibili instructors, big shot blogs, or your university professor sleepily reciting their ancient PPTs—they will tell you. Writing C files involves doing two things—declarations and definitions (implementations). The objects of our discussion are **global variables and functions**, and I must emphasize this here.

- What about local variables? Ah, discussing this is meaningless. They are served by the operating system dynamically for your program code after it runs on the CPU—possibly **assigned to specific registers, or allocated memory, but absolutely not lying in the on-disk executable file!**
- It is worth mentioning that a **definition contains a declaration**. Don't quite understand? For example, if I tell you what A is, haven't I also told you that an A exists here?

A declaration is simple. We just loudly proclaim that something exists here (you ask me what it is? What's the value? Sorry, I don't know, I can only tell you that it indeed exists, and the compiler, you go find it).

A definition is not difficult either. We associate a declaration (which might be the declaration we shouted about elsewhere, or an inline declaration like `int a = 0;`) with its implementation. This action is the **definition**. For global variables, this implementation is data. For functions, it is our execution code. A global variable definition causes the compiler to allocate specific space for your variable in the resulting executable file. Naturally, it also includes the value you assigned, otherwise why define it?

We know that the relocatable object files generated after compilation will expose function names and variables. When writing programs, we subconsciously assume they can be found (astute friends might interrupt me—found by whom? During compilation or during linking/execution? Don't worry, we'll talk about it immediately)—this is called **symbol visibility** in serious academic discussion. **Visible symbols are accessible!** The **accessibility of visible symbols** requires a dichotomous discussion:

- Accessibility during compilation—for example, those symbols in a C program **not modified by `static`, including global variables and functions**. If you've written C programs, you clearly know that after writing global `int a` and `void func(void)` in `file1.c`, `file2.c` cannot access them at all! You can try it yourself.
- Accessibility during execution—this refers to all global variables and functions, whether modified by `static` or not. Because they are all stored in the executable file, once on the CPU, the operating system must allocate memory storage for the program's lifetime for all global variables and functions, `static` or not. Therefore, for the CPU, they actually accompany the program for life. Thus they are still global, only that some global variables must **only be accessible by specific code** (this is where `static` comes into play).

In other words, any **accessible global variable and function** must accompany the program for life and needs to be placed in the program's executable file, occupying a certain amount of space (this is also why I say only discussing global variables and functions is meaningful). The rest has nothing to do with our question. I wrote a program here:

```c
// demo.c
#include <stdio.h>

int g_uninit_var;            // Uninitialized global variable
int g_init_var = 10;          // Initialized global variable
extern int g_extern_var;     // External variable declaration

static int s_uninit_var;     // Static uninitialized variable
static int s_init_var = 20;  // Static initialized variable

static void static_func(void) {
    printf("Static function called\n");
}

void normal_func(void) {
    printf("Normal function called\n");
}

extern void extern_func(void); // External function declaration

int main(void) {
    normal_func();
    // extern_func(); // Uncomment to test linking
    return 0;
}
```

| Symbol | Category | Storage Class | Linkage | Typical Segment | Function |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `g_uninit_var` | Variable Definition | **Global** (static duration) | **External** (external) | **BSS** (Block Started by Symbol) | Uninitialized global variable, initialized to 0 at runtime. |
| `g_init_var` | Variable Definition | **Global** (static duration) | **External** (external) | **Data** (Initialized Data) | Initialized global variable. |
| `g_extern_var` | Variable Declaration | N/A (Reference) | **External** (external) | N/A (Expected to be defined in other files) | References a global variable defined in another compilation unit. |
| `s_uninit_var` | Variable Definition | **Global** (static duration) | **Internal** (none) | **BSS** | Static variable with file scope, uninitialized, initialized to 0 at runtime. |
| `s_init_var` | Variable Definition | **Global** (static duration) | **Internal** (none) | **Data** | Static variable with file scope, initialized. |
| `static_func` | Function Definition | **Function** | **Internal** (none) | **Code** (.text) | Static function, can only be called within the current file. |
| `normal_func` | Function Definition | **Function** | **External** (external) | **Code** (.text) | Normal function, available for other files to call. |
| `extern_func` | Function Declaration | **Function** | **External** (external) | N/A (Expected to be defined in other files) | References a function defined in another compilation unit. |

Think about the table above. If you find anything confusing, feel free to search and understand the table yourself.

## How the C Compiler Views Our Files

Let's get the C compiler moving. Note that your compilation command must be:

```bash
gcc -c demo.c -o demo.o
```

The compiler compiles quietly for a while and gives us the `demo.o` we wanted. So what is the compiler doing when compiling the entire C unit?

Whether you are using Apple Clang, GNU GCC, or Microsoft's MSVC, they are all **compilers**. Their main job, as you see, is to convert C files from text humans can understand (except for "mountain code") into content the computer can understand. The compiler outputs the result as an object file. On UNIX platforms, these object files usually have an `.o` suffix; on Windows, they have a `.obj` suffix.

Interestingly, our object files, looping back to the theme above, ultimately generate at least the following two parts in content:

- Machine code: Machine code is specific instructions made of 0s and 1s that the computer can understand.
- Data evolved from global variables: They correspond to the definitions of global variables in the C file (for initialized global variables, the variable's initial value must also be stored in the object file).

Hmm, the question arises. Look closely at `extern_func` and `g_extern_var`. Friends familiar with the `extern` keyword will immediately cry out something's wrong—Hmm? Your `extern_func` and `g_extern_var` aren't implemented at all. Didn't the compiler notice?

I'm telling you—it knows about this, but **C/C++ compiled languages allow you to have only declarations without implementations during compilation!** I must emphasize this **useful but troublesome** feature again: **C/C++ compiled languages allow you to have only declarations without implementations during compilation!** So when is the verdict reached on whether you intentionally placed these implementations elsewhere or just carelessly missed them? The answer is the next stage: linking. We will discuss that later; for now, let's keep our focus on the compilation stage.

## nm, a Handy Command

Windows MSVC users, don't bother. You should probably use `dumpbin` instead of `nm` (if you installed MSVC, I mean if you are writing code with Visual Studio). But here, I am ready to discuss using `nm` with System V output format.

How do we verify the content we discussed above for the obtained executable file? It's simple. Let's take out our `nm` tool and analyze it. Come on, try it:

```bash
nm demo.o
```

Output:

```text
0000000000000000 T normal_func
                 U extern_func
0000000000000000 D g_init_var
                 U g_extern_var
0000000000000000 B g_uninit_var
0000000000000004 d s_init_var
0000000000000000 b s_uninit_var
0000000000000000 t static_func
```

Okay, let's look at this table carefully. What you need to do is pay attention to the **Class** column; it explains what our symbols are.

- **Class U** represents an **Undefined** reference, one of the "blanks" mentioned earlier. This object has two classes: `extern_func` and `g_extern_var`.
- **Class t or T** represents where code is defined; different classes indicate whether the function is a local function (`t`) or a non-local function (`T`)—that is, whether the function was originally declared with `static`. Similarly, some systems might also show a section, such as `.text`.
- **Class d or D** represents initialized global variables; similarly, the specific class indicates whether the variable is a local variable (`d`) or a non-local variable (`D`). If there is a section, it is similar to `.data`.
- For uninitialized global variables, if it is a static/local variable, it returns `b`; if not, it returns `B` or `C`. In this case, the section might be similar to `.bss` or `*COM*`.

Friends on Windows, you need to open **Developer Command Prompt for VS**, navigate to your target C file, and type `cl /c demo.c`. This way, MSVC will only compile our source file, and the resulting `demo.obj` is our relocatable object file. At this time, we can use the `dumpbin` tool:

```bash
dumpbin /symbols demo.obj
```

Let's look at the symbols. I will enumerate the results I got here (default toolchain in VS 2022):

```text
...
EXTERNAL  |    notype ()         | External   |  | 00000000 | normal_func
EXTERNAL  |    notype ()         | External   |  | 00000000 | g_init_var
EXTERNAL  |    notype ()         | External   |  |          | extern_func
EXTERNAL  |    notype ()         | External   |  |          | g_extern_var
EXTERNAL  |    notype ()         | External   |  |          | g_uninit_var
STATIC    |    notype ()         | Static     |  | 00000000 | s_init_var
STATIC    |    notype ()         | Static     |  | 00000000 | s_uninit_var
STATIC    |    notype ()         | Static     |  | 00000000 | static_func
...
```

Kicking aside other messy outputs, it essentially comes down to the following table:

| `dumpbin` Output | Meaning | Analogy Linux `nm` |
| :--- | :--- | :--- |
| `EXTERNAL ... normal_func` | External function defined in `.text` | `T` |
| `EXTERNAL ... g_init_var` | External variable defined in `.data` | `D` |
| `EXTERNAL ... extern_func` | Undefined external function reference | `U` |
| `EXTERNAL ... g_extern_var` | Undefined external variable reference | `U` |
| `EXTERNAL ... g_uninit_var` | Undefined external variable reference | `U` |

## Resolving Our Unknown Symbols: Linking

Now let's push the topic further. This step solves the problem we left in the section "How the C Compiler Views Our Files". We assume that the definitions for these external symbols really exist in other files:

```c
// demo_extern.c
int g_extern_var = 30;

void extern_func(void) {
    printf("External function called\n");
}
```

We will also compile these symbols into relocatable object files. The rest is to combine these object files, which contain various defined symbols and undefined symbols, to **resolve the parts in each file where symbols are uncertain (only names) and definitions are unknown** (our compiler passed these source files, which means we declared these symbols, but haven't found the definitions yet). **This is what we need to do during linking.**

Now, after compiling `demo_extern.c` into `demo_extern.o`, we use this to complete the last step of our executable file:

```bash
gcc demo.o demo_extern.o -o demo
```

Compilation naturally passes smoothly. No doubt.

```bash
./demo
# Normal function called
```

Now let's look at it. The table becomes very complex, but that's okay. What we care about most is:

```bash
nm demo
```

Output:

```text
...
0000000000000000 T normal_func
0000000000001169 T extern_func
0000000000000000 D g_init_var
0000000000000004 D g_extern_var
...
```

We have finally found the content we care about. They are no longer uncertain `UNDEF` but defined functions and global variables. We can completely try removing the implementation of `extern_func`.

```bash
# Modify demo_extern.c to remove extern_func
gcc -c demo_extern.c -o demo_extern.o
gcc demo.o demo_extern.o -o demo
```

Our familiar error appeared! `undefined reference to extern_func`, indicating the linker complained to us that it couldn't find the definition of `extern_func`. Let's look closely:

```text
/usr/bin/ld: demo.o: in function `main':
demo.c:(.text+0x10): undefined reference to `extern_func'
```

You can see that `demo_extern` resolved the definition of `extern_var`, but the definition of `extern_func` was not found. We only gave these two files, so naturally the linker doesn't know where to find your `extern_func`, and naturally it will throw this error.

We now know the important function of the linker—resolving the undefined symbol problem of the minimal executable file (why minimal? We will continue to discuss). Any link where **you did not provide corresponding information telling the specific content of the definition (the source code for used functions was missed)** will fail! Finally, when the linker searches around, as long as there are undefined symbols (that is, symbols with Class `U` in `nm` or `dumpbin`), the linker will raise an error: telling you all those undefined symbols. **At this time, your solution is very simple—find the relocatable file for these symbols (generally the source code file name and relocatable file name in the build system are the same, only the suffix is different), and provide it during linking!** This is the **only way** to resolve `undefined reference` in all compilation scenarios without dynamic libraries.

Now that we have seen the output of `nm`, we can answer the whole question:

- Q1: How does the compiler toolchain collect and find symbols? How does it further transform them into a more manageable form?
- A: The answer is the compiler compiles symbols into instructions the computer can understand, mapping **function symbols to an address**. For global variables, it maps a global variable to a specific access location in the data segment.
- Q2: **What do the variables and functions we write actually mean to the computer?**
- A: It just associates our addresses with variables of specific meaning. It doesn't matter what name you give it. After processing by the compiler and linker, only a string of addresses remains for the computer—you ask me what that is, I don't know! Ask `nm`!

## Extra Topic: What if We Redefine?

The previous section mentioned that if the linker cannot find the definition of a symbol to connect with a reference to that symbol, it will give an error message. So, what happens if there are two definitions of a symbol during linking?

I won't say the answer immediately. You try it first. For example, restore the definition of `extern_func` in `demo_extern`, and immediately modify our `demo.c` like this:

```c
// demo.c
// ... (previous code)

void extern_func(void) { // Redefine extern_func
    printf("Redefinition in demo.c\n");
}
```

We repeat the separate compilation and linking actions. Soon, we get another error you might be familiar with:

```text
/usr/bin/ld: demo_extern.o: in function `extern_func':
demo_extern.c:(.text+0x0): multiple definition of `extern_func'; demo.o:demo.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

Did you notice? Same as before, because the compiler believes **the linker can correctly handle the relationship of any symbols** (it can only compile files one by one! It can't manage other global source files! **The symbol arbitration of the entire result unit (including executable files, dynamic libraries, and static libraries) is decided by the linker!** This is what I must emphasize again!)

So, during linking, the linker discovers that there are actually identical symbol definitions in two files. Naturally, the definitions are different. Just like you saying A is 1, then saying A is 2. Uniqueness is broken, and rashly deciding will only make the program uncontrollable. So, the linker naturally slaps it back and rejects it! At least with the default behavior of today's GNU toolchain, doing this will only get you a `multiple definition` error.

## Is That All the Linker Does?

Since I asked this way, how could it be just that? I don't know if when you see me repeatedly emphasize this sentence, you feel anything:

- Why is it: **C/C++ compiled languages allow you to have only declarations without implementations during compilation!** Why not require knowing immediately? It's so troublesome.

Think calmly for a moment. For example, I ask you to go to the post office to send a letter. You obviously won't interrupt me: "Shut up, buddy. You carry the post office here first, and I'll help you send it when I see the letter." Instead, you are more likely to draw an imaginary post office in your mind, "Hmm, I need to go to a place called the post office to help send a letter." You will naturally go to other places to find the letter. This is the same principle. We leave the pending symbols, and we manage and promise them ourselves that they will appear in the corresponding place—**this is your responsibility, not the compiler's**. Okay, now we can continue our question:

- So, besides providing source code, can we provide other forms of information?

Hey! Your observation is excellent. If you look closely at my operation here:

```bash
gcc demo.o demo_extern.o -o demo
```

Did you notice that the linking step seems to have nothing to do with source files? After all, we retrieve undefined symbols from relocatable files (`*.o`). So, can we prepare a series of relocatable files and a set of symbol declaration files in advance, so that when we program, we don't repeat the wheel? We directly **use these declaration files during programming to tell the compiler that I guarantee these symbols exist**, **generate our own relocatable files during compilation**, and then **combine these prepared relocatable files with our own relocatable files during linking to form an executable file**?

Congratulations! You have reinvented the concepts of libraries and interface programming! You now know what header files are for! They are just a set of symbol declaration files! And these thousands of relocatable files, don't leave them scattered. Let's **collect them into a library**. How about that? Of course! You have now invented the historically **famous static library**. I'm a bit excited, but I need to reorganize the concepts we proposed:

- Header files: That is, symbol declaration files, **placing symbol declarations we guarantee exist**.
- Static libraries: The specific definitions of these symbols (all or part; remaining unresolved symbols may depend on other libraries, interesting!).

So my point is—the linker can also link libraries. I didn't say static libraries; there are also dynamic libraries. Let's talk about static libraries first.

## Static Libraries: Our Symbol Library

We can use `ar` (on Linux or UNIX systems) or `Lib` tools to collect all relocatable files to generate static libraries.

> Quick details:
>
> - On **UNIX** systems, the command to generate a static library is usually **`ar rcs`**, and the generated library file usually has an **`.a`** extension. These library files usually also have **"lib"** as a prefix, and when passed to the linker, the **`-l`** option is used, followed by the library name (without prefix and extension). For example, **`-lutils`** will select the **`libutils.a`** file. (Historically, static libraries also required a program called **`ranlib`** to build a symbol index at the beginning of the library. Nowadays, the **`ar`** tool usually does this by itself.)
> - On **Windows** systems, static libraries have an **`.lib`** extension and are generated by the **`lib`** tool. But this can be confusing because **"import libraries"** also use the same extension, which only contains a list of what is available in a DLL.

For the linking stage, when we provide a static library to the linker, the linker holds a table of unresolved symbols, dives into the static library, and finds these symbols one by one (for example, if symbol A is missing and it is in `Obj1.o`, at this time we will link the entire `Obj1.o` in), until we solve all the undefined symbol problems.

Please note the **granularity** of extracting content from the library: if the definition of a specific symbol is needed, the **entire object file** containing the definition of that symbol is included. This means the process might be "one step forward, one step back"—the newly added object file may resolve an undefined reference, but it likely also brings a whole new set of its own undefined references, leaving the linker to resolve.

[Linkers and Loaders](https://www.lurklurk.org/linkers/linkers.html) has a very excellent example, which I have placed below for you to read:

Assume we have the following object files, and the link line contains **`a.o`**, **`b.o`**, **`libx.a`**, and **`liby.a`**.

| File | **a.o** | **b.o** | **libx.a** | **liby.a** |
| :--- | :--- | :--- | :--- | :--- |
| **Objects** | a.o | b.o | x1.o, x2.o, x3.o | y1.o, y2.o, y3.o |
| **Defines** | a1, a2, a3 | b1, b2 | x11, x12, x13; x21, x22, x23; x31, x32 | y11, y12; y21, y22; y31, y32 |
| **Undefined References** | b2, x12 | a3, y22 | x23, y12; y11; y21 | x31 |

1. **Processing `a.o` and `b.o`:**
   - The linker will resolve references to `a1`, `a2`, `a3`, `b1`, and `b2`.
   - At this point, the undefined references remaining are **`x12`** and **`y22`**.
2. **Processing `libx.a`:**
   - The linker checks the first library `libx.a` and finds it can pull in **`x2.o`** to satisfy the `x12` reference.
   - However, pulling in `x2.o` also brings new undefined references **`x23`** and **`y12`**. (Undefined list is now: `x23`, `y12`, and `y22`).
   - The linker is still processing `libx.a`, so the `y12` reference is easily satisfied by pulling in **`x3.o`**.
   - But this adds `y21` to the undefined list. (Undefined list is now: `x23`, `y21`, and `y22`).
   - No other object files in `libx.a` can resolve these remaining symbols, and the linker moves on to process `liby.a`.
3. **Processing `liby.a`:**
   - Similar flow, the linker will pull in **`y2.o`** and **`y3.o`**.
   - Pulling in `y2.o` adds a reference to `y21`, but since `y3.o` is going to be pulled in anyway, this reference is easily resolved.
   - Final result: All undefined references are resolved, and some object files from the libraries (not all) are included in the final executable file.

#### The Importance of Link Order

Note that if (for example) `x2.o` also had a reference to `y32`, the situation would be different.

- The linking work for `a.o` and `b.o` remains the same.
- When processing `libx.a`, the linker will also pull in **`x2.o`** to resolve `x12`.
- Pulling in `x2.o` adds **`y32`** to the unresolved symbol list.
- At this point, the linker has **finished** processing `liby.a`, so it cannot find the definition of this symbol (in `y3.o`), resulting in **link failure**. This example clearly illustrates the importance of link order (`libx.a` before `liby.a`). That is, the linker does not go back. When you link, you must clearly define that the dependencies of programming symbols must be progressive dependencies, not circular dependencies. Don't make trouble for yourself!

## Dynamic Libraries/Shared Libraries

Of course, for now, simply understand them as dynamic libraries. Strictly speaking, there is a slight difference between the two, but in an introduction, being too strict will only scare people away.

The existence of dynamic libraries is more to solve an obvious shortcoming of static libraries—every executable program has a copy of the same code. If every executable file contained copies of functions like `printf` and `fopen`, it would take up a lot of unnecessary disk space.

> You can do an interesting experiment. Statically link the C library and see how big it is. Please find the specific command yourself. My result is several hundred MB.

Of course, you say—I have money; I can add SSDs at will. This isn't the most serious problem. The most serious problem is—if the provider's code has a bug, you are done—all the code is written into the executable file. You can't use this executable file at all—until someone else compiles it for a few months and gives it to you!

To solve these troublesome problems, shared libraries/dynamic libraries appeared (usually represented by the `.so` extension, `.dll` on Windows, and `.dylib` on Mac OS X). At this time, the linker adopts an "IOU" method and defers the payment of the IOU to the moment the program actually runs. Ultimately, it is: if the linker finds that the definition of a symbol exists in a shared library, it will not include the definition of that symbol in the final executable file. Instead, the linker records the name of the symbol and which library it should come from in the executable file.

When the program runs, the operating system arranges for these remaining linking tasks to be completed "just in time" so the program can run. Before the main function runs, a smaller version of the linker (usually called `ld.so`) checks these "IOUs" and immediately completes the final stage of linking—pulling in library code and connecting all the code. This means that no executable file has a copy of the `printf` code. If a new, fixed version of `printf` is available, you only need to change `libc.so` to plug it in—the next time any program runs, it will be selected.

There is another major difference in how shared libraries work compared to static libraries, reflected in the granularity of linking. If a specific symbol is extracted from a specific shared library (e.g., `printf` in `libc.so`), the entire shared library is mapped into the program's address space. This is distinctly different from the behavior of static libraries, where only the specific object containing the undefined symbol is extracted.

We'll stop there regarding shared libraries. I have a small 300-page book "Advanced C/C++ Compilation Techniques" that specifically discusses dynamic library/shared library technology. That is enough to show how complex this topic is. We will discuss it carefully in a later blog. For the introduction, let's stop here.

## Other Topics: What About C++?

#### C++ Name Mangling

Back to this `usage.cpp`:

```cpp
// usage.cpp
extern int add(int a, int b);

int main() {
    return add(1, 2);
}
```

When you use the `add` function in this C++ file, the C++ compiler (`g++`) won't simply map the function name to `add` like the C compiler does. To support features C doesn't have, like **function overloading**, **namespaces**, and **class member functions**, the C++ compiler performs complex encoding on the function names in the source code, a process called **Name Mangling**.

```bash
g++ -c usage.cpp -o usage.o
nm usage.o
```

Output:

```text
                 U _Z3addii
...
```

The `g++` compiler, when generating the `usage.o` object file, expects the linker to find a mangled symbol, for example, in a GCC/Linux environment, it might look for a symbol like **`_Z3addii`** (the specific mangling result varies by compiler and platform, but it is **definitely not** a simple `add`).

#### C Library Symbol Names

The problem is that the static library `libutils.a` was generated by the **C compiler** (usually `gcc` or `clang`) compiling the `lib.c` file. The C compiler **does not perform name mangling**. Therefore, in `libutils.a`, the symbol name for the `add` function is simply **`add`** (or with an underscore prefix, like `_add`).

You immediately know the problem below.

```bash
g++ usage.cpp -L. -lutils -o app
```

1. **`g++`** compiles `usage.cpp`, generating `usage.o`, which contains an **undefined reference** to the **mangled name** (e.g., `_Z3addii`).
2. The linker (`ld`) starts working. It looks for `_Z3addii` in `libutils.a` but only finds a demand for `add`.
3. The linker looks for `_Z3addii` in `libutils.a`, but the symbol existing in the library is **`add`**.
4. The linker cannot find a matching symbol and therefore reports an error: `undefined reference to 'add(int, int)'` (Note: the error message shows the C++ style function signature, but what the linker is actually looking for is its mangled version).

#### Solution: Using `extern "C"`

To solve this problem, you need to tell the C++ compiler: **"Hey, this function was compiled with a C compiler, don't mangle its name!"** You only need to use the **`extern "C"`** linkage specifier around the **function declaration** in the C++ file:

```cpp
// usage.cpp
extern "C" int add(int a, int b);

int main() {
    return add(1, 2);
}
```

Recompile and link, and the program will run successfully, because the symbol referenced in `usage.o` will now be the simple `add`, matching the symbol provided in `libutils.a`.
