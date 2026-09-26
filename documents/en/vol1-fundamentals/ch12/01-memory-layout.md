---
title: "Memory Layout"
description: "Understand the memory model of the stack, heap, static storage, and the code segment, and learn to analyze where variables are stored and how long they live"
chapter: 12
order: 1
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
  - "Common STL Patterns"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol1-fundamentals/ch12/01-memory-layout.md
  source_hash: 95ac5efa22c7a1c11540fd1e6ed04114f3bbd5704937e405b6fa5a2560c4e3c8
  translated_at: '2026-09-25T12:02:09+00:00'
  engine: anthropic
  token_count: 3400
---

# Memory Layout: Where the 42 in `int x = 42` Actually Lives

We have spent an enormous number of pages on language-level matters—types, containers, templates—but one fundamental question has never been answered head-on: when we write `int x = 42;`, where exactly does that `42` live? What position in memory does it occupy? When is it created, and when is it destroyed? These questions look like "low-level details", but if you don't know which region of memory your data calls home, debugging certain bizarre problems turns into the blind men and the elephant—the address from a segmentation fault tells you the stack blew up, and you are still completely lost.

Understanding memory layout really comes down to two things: **where data is stored**, and **how long it lives**. In this chapter we split a program's memory space into a few major regions and walk through each one's characteristics, typical uses, and the traps it sets for the unwary.

## The Four Major Memory Regions

When a C++ program runs, the operating system allocates a block of virtual address space for it. That block is not one homogeneous region; it is carved into several segments, each with its own purpose and way of being managed. For us, the four regions that matter most are these:

![Process virtual address space layout](./01-memory-layout.drawio)

Let's look at what each region holds. The text segment stores the compiled machine instructions plus some read-only data (string literals such as `"hello"`); this region is usually read-only, and any attempt to modify it triggers a segmentation fault on the spot. The data segment holds initialized global and `static` variables, whose values are already settled when the program starts. The BSS segment is the part of the data segment reserved for uninitialized global and `static` variables—these are automatically initialized to zero, so the executable does not need to store their initial values at all; recording their size is enough. The heap and the stack are the regions used dynamically at runtime: the former is managed by hand by the programmer, the latter automatically by the compiler.

One key observation about this layout model: the stack grows from high addresses toward low ones, the heap grows from low addresses toward high ones, and the two march toward each other. That means their address ranges will not overlap (unless one side exhausts the available space), and if we print the address of a stack variable and a heap variable side by side, the stack variable usually shows up with a noticeably larger value.

## Stack Memory—The Auto-Managed Fast Lane

The stack is the most heavily used memory region in a C++ program. Local variables declared inside functions, function parameters, return addresses—they all live on the stack. Stack management is brutally simple: a single pointer (the stack pointer) marks the current top of the stack; allocating memory means moving the pointer toward lower addresses, and freeing memory means moving it back up. This "pointer-shoving" style of allocation needs no searching and no merging, which is why stack allocation is so fast it amounts to nearly zero overhead.

Every time a function is called, the compiler creates a stack frame for it on the stack, holding that function's local variables, parameters, and return address. When the function returns, the whole frame is popped and every local variable is destroyed in an instant. This mechanism is called automatic storage duration: a variable's lifetime is decided entirely by its scope—created on entering the scope, destroyed on leaving it—and we don't have to lift a finger.

```cpp
#include <iostream>

void foo()
{
    int a = 1;    // Allocated on the stack
    double b = 2.0; // Right after a
    std::cout << "a 的地址: " << &a << "\n";
    std::cout << "b 的地址: " << &b << "\n";
    // On return, a's and b's storage is reclaimed automatically
}

int main()
{
    foo();
    // Here, a and b no longer exist
    return 0;
}
```

The stack's drawback is just as obvious: space is limited. On Linux the default stack size is usually 8 MB (check it with `ulimit -s`); on Windows it is usually 1 MB. For normal function calls that limit is more than enough, but two scenarios blow through it with ease.

Allocating a large array on the stack is one of the most common traps beginners step in. The harmless-looking declaration `int arr[10000000];` actually needs roughly 40 MB of stack space—far past the default limit—so the program segfaults the moment it starts, without even getting a chance to print an error message. When we need a large block of memory, use `std::vector` or allocate on the heap.

The other classic scenario is recursion without a correct termination condition, or recursion whose depth is simply too large. Computing a factorial by recursing down to `n = 100000`, for instance, burns stack-frame space at every level of the call and devours the stack in no time. In a debugger, a stack overflow usually shows up as an abnormally low stack pointer address: on Linux we will see the `rsp` register sitting far below the normal stack region—that is the stack pointer having charged straight down past the safety boundary.

There is also a less conspicuous scenario: in embedded systems the stack is often smaller still (some RTOS task stacks are only a few KB), and then even an ordinary local array (say `char buf[512];`) can become a hidden hazard. So build a habit as you write code: for data structures larger than a few hundred bytes, prefer heap or static allocation instead of tossing them onto the stack by default.

A stack overflow (unlike a failed heap `new`, which throws an exception or returns `nullptr`) gets no such courtesy: when the operating system detects a stack overflow it fires SIGSEGV directly and the program terminates immediately. There is no chance for graceful handling; when debugging, all we can rely on is after-the-fact analysis of a core dump, or a counter guard added at the recursion entry.

## Heap Memory—The Free but Dangerous Wilderness

The heap is the largest usable memory region in a program—in theory it can expand to whatever maximum the operating system allows (terabyte scale on 64-bit systems). When we request memory with `new` or `malloc`, the allocator finds a suitably sized block on the heap and returns it; that block keeps existing until it is explicitly released (`delete` or `free`). Storage whose lifetime the programmer controls by hand like this is called dynamic storage duration.

The heap's flexibility comes at a price. The allocator has to maintain data structures such as free lists or buddy systems to track which regions are occupied and which are free; every `new` runs a search algorithm to find a block of the right size, and every `delete` runs merge operations to keep memory from fragmenting. This management overhead makes heap allocation orders of magnitude slower than stack allocation. On top of that, frequently allocating and freeing blocks of different sizes leads to memory fragmentation: the total free space may be sufficient, but it has been carved into masses of tiny, non-contiguous fragments that cannot satisfy a larger request. That is also why high-performance systems use custom allocators or memory pools to bypass the default heap allocation machinery.

```cpp
#include <iostream>

int main()
{
    // Heap allocation
    int* p1 = new int(42);
    int* p2 = new int[1000]; // Arrays go on the heap too

    std::cout << "p1 指向的地址: " << p1 << "\n";
    std::cout << "p2 指向的地址: " << p2 << "\n";

    // Must be released manually
    delete p1;
    delete[] p2;

    return 0;
}
```

Forgetting `delete` and causing a memory leak is one of C++'s most notorious problems. Leaked memory is never reclaimed before the program ends. For a short-lived console program that usually is not a big deal (the operating system reclaims all resources when the process exits), but for long-running server programs or embedded systems, a leak nibbles the available memory away bit by bit until the system finally crashes. This is exactly why earlier chapters hammered on RAII over and over: manage dynamic memory with smart pointers (`std::unique_ptr`, `std::shared_ptr`) and containers (`std::vector`, `std::string`), let destructors release resources automatically, and eliminate the need for manual `delete` at the root.

## Static and Global Memory—From Program Start to Finish

Global variables, namespace-scope variables, variables declared with `static`, and class `static` member variables all have static storage duration. Their lifetimes run through the entire program: they are created and initialized before `main()` begins executing, and destroyed only after `main()` returns.

Variables in static storage are initialized in one of two ways. For constants whose value can be determined at compile time (like `const int kMaxSize = 100;`), the compiler writes the initial value directly into the executable's data segment; for initial values that must be computed at runtime (like `static int counter = compute_init_value();`), initialization is completed at program startup, before `main()` executes.

```cpp
#include <iostream>

int global_var = 10;          // Data segment: initialized global variable
int global_uninit;             // BSS segment: uninitialized global variable (automatically 0)
const char* kMessage = "hello"; // Data segment: the pointer itself lives in the data segment
                               // the "hello" literal lives in the text segment (read-only)

void demo()
{
    static int call_count = 0; // Data segment: initialized on first call
    ++call_count;
    std::cout << "第 " << call_count << " 次调用\n";
}

int main()
{
    std::cout << "global_var = " << global_var << "\n";
    std::cout << "global_uninit = " << global_uninit << "\n";

    demo(); // 1st call
    demo(); // 2nd call
    demo(); // 3rd call

    return 0;
}
```

`static` local variables come with a very practical property: lazy initialization. A `static` local is initialized only when execution reaches its declaration statement, not at program startup. Since C++11, this initialization is thread-safe too—if multiple threads first enter a function containing a `static` local variable at the same time, the compiler guarantees that only one thread performs the initialization while the others block and wait. This property makes the `static` local variable the best vehicle for a thread-safe singleton (the Meyers' Singleton).

The construction and destruction order of global variables is undefined across translation units (that is, across different .cpp files). If a global object in `a.cpp` depends on the initialization result of another global object in `b.cpp`, the program can hit undefined behavior during the startup phase—because the standard guarantees nothing about which one is initialized first. This is the notorious "Static Initialization Order Fiasco". The fix is to wrap the object in a `static` local variable inside a function (the Construct On First Use Idiom), exploiting the lazy initialization we just described to guarantee a correct initialization order.

## Hands-On Verification—Printing the Addresses of Each Region

That was a lot of theory, so let's write a program and check it for real. In the code below we put one variable in each region and print its address. By looking at the addresses' magnitudes and relative positions, we can verify the memory layout model with our own eyes.

```cpp
// layout.cpp
// Build: g++ -std=c++17 -O0 layout.cpp -o layout
// Note: -O0 disables optimization, keeping the compiler from getting aggressive with these variables

#include <cstdint>
#include <iostream>

// Global variable — data segment (initialized)
int g_initialized = 42;

// Global variable — BSS segment (uninitialized, automatically 0)
int g_uninitialized;

// const global — usually in a read-only segment or inlined by the compiler
constexpr int kGlobalConst = 100;

int main()
{
    // Stack variable
    int stack_var = 1;

    // Heap variable
    int* heap_var = new int(2);

    // static local variable — data segment
    static int s_static_local = 3;

    std::cout << "=== 各区域变量地址 ===\n";
    std::cout << "代码段 (函数地址):  main()    @ " << reinterpret_cast<void*>(main) << "\n";
    std::cout << "数据段 (已初始化):  g_initialized  @ " << &g_initialized << "\n";
    std::cout << "BSS段  (未初始化):  g_uninitialized @ " << &g_uninitialized << "\n";
    std::cout << "数据段 (static局部): s_static_local @ " << &s_static_local << "\n";
    std::cout << "栈:                 stack_var  @ " << &stack_var << "\n";
    std::cout << "堆:                 heap_var   @ " << heap_var << "\n";

    std::cout << "\n=== 地址大小关系 ===\n";
    std::cout << "栈地址 > 堆地址? " << (&stack_var > heap_var ? "是" : "否") << "\n";
    std::cout << "栈地址 > 数据段地址? " << (&stack_var > &g_initialized ? "是" : "否") << "\n";
    std::cout << "数据段地址 > 代码段地址? "
              << (&g_initialized > reinterpret_cast<int*>(main) ? "是" : "否") << "\n";

    delete heap_var;
    return 0;
}
```

After compiling and running, the output looks roughly like this (exact values vary by system):

```text
=== 各区域变量地址 ===
代码段 (函数地址):  main()    @ 0x401136
数据段 (已初始化):  g_initialized  @ 0x404010
BSS段  (未初始化):  g_uninitialized @ 0x404030
数据段 (static局部): s_static_local @ 0x404014
栈:                 stack_var  @ 0x7ffd3e8a1b4c
堆:                 heap_var   @ 0x1c5a2b7eac0

=== 地址大小关系 ===
栈地址 > 堆地址? 是
栈地址 > 数据段地址? 是
数据段地址 > 代码段地址? 是
```

These addresses validate our layout model beautifully: the text segment sits at the lowest addresses, with the data and BSS segments right above it; the heap occupies the lower-middle range and grows upward; the stack sits near the highest addresses and grows downward. The address of `main` is far smaller than every other one—it really is inside the text segment. `g_initialized` and `s_static_local` sit at very close addresses; both are in the data segment. `g_uninitialized` has a slightly larger address than the initialized variables, the BSS segment coming after the data segment. And the huge gap between the stack variable and the heap variable is exactly the unused space between the two.

Run this program on your own machine and the specific values will certainly differ (especially the stack address, which changes on every run—that is ASLR, address space layout randomization, an operating system security mechanism), but the relative ordering should be the same. If one day you find a stack address smaller than a heap address, most likely the compiler performed some special memory layout optimization, or your platform uses a non-traditional memory model—either way, that is extremely rare in desktop and server environments.

## Exercises

### Exercise 1: Identify the Storage Region

For each variable below, figure out which memory region it is stored in (stack, heap, data segment, BSS segment, text segment):

```cpp
const char* msg = "error";    // Where do msg and "error" each live?
static int count;              // ?
int* p = new int[10];         // Where do p and the array it points to each live?
void func() {
    int local = 0;            // ?
    static int visits = 0;    // ?
}
```

### Exercise 2: Spot the Stack Overflow Hazard

What is wrong with the following code? Think about how you would fix it.

```cpp
void process_image()
{
    // Image buffer: 1920 x 1080 x 4 (RGBA) = about 8 MB
    unsigned char buffer[1920 * 1080 * 4];
    // ... process the image ...
}

int fibonacci(int n)
{
    return fibonacci(n - 1) + fibonacci(n - 2); // Missing termination condition
}
```

### Exercise 3: Verify the Layout Model

Write a program that, inside one function, declares a local variable, allocates a heap variable, defines a `static` local variable, and prints the address of a global variable. Observe whether the distribution of their addresses matches the layout model we described. Then call a child function from within that function and print the address of a local variable inside the child, verifying that the child function's stack variable has a smaller address than the parent's (the stack grows toward lower addresses).
