---
title: Memory layout
description: Understand the memory model of the stack, heap, static area, and code
  segment, and learn to analyze the storage location and lifetime of variables.
chapter: 12
order: 1
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
- STL 常用模式
tags:
- cpp-modern
- host
- intermediate
- 进阶
cpp_standard:
- 11
- 14
- 17
- 20
---
# Memory Layout

We previously spent a lot of time discussing language-level features like types, containers, and templates, but we haven't directly answered a fundamental question: when you write `int x = 42;`, where does this `42` actually live? Where is it located in memory? When is it created, and when is it destroyed? These might seem like "low-level details," but honestly, if you don't know which memory region your data resides in, debugging certain bizarre issues will feel like the blind men and the elephant—the address from a segmentation fault tells you the stack blew up, but you're left completely clueless.

Understanding memory layout essentially comes down to two things: **where data lives**, and **how long it lives**. In this chapter, we break down a program's memory space into several major regions, analyzing the characteristics, typical uses, and common pitfalls of each.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Name the four main memory regions of a program and their responsibilities
> - [ ] Determine which region any given variable resides in
> - [ ] Understand the stack's growth direction, size limits, and the causes of stack overflow
> - [ ] Distinguish between the various cases of static storage duration (global variables, `static` local variables, constants)
> - [ ] Write a program that prints the addresses of variables in each region to verify the memory layout model

## The Four Major Memory Regions

When a C++ program runs, the operating system allocates a block of virtual address space for it. This space is not a single homogeneous region; rather, it is divided into several segments, each with its own purpose and management method. For our purposes, the four most important regions are:

```text
高地址
┌─────────────────────────┐
│       栈 (Stack)         │  ← 局部变量、函数调用帧
│         ↓↓↓↓↓           │     向低地址增长
├─────────────────────────┤
│                          │  ← 未使用空间
│                          │
├─────────────────────────┤
│       堆 (Heap)          │  ← new/malloc 动态分配
│         ↑↑↑↑↑           │     向高地址增长
├─────────────────────────┤
│   BSS 段（未初始化数据）  │  ← 未初始化的全局/static 变量
├─────────────────────────┤
│   数据段（已初始化数据）  │  ← 已初始化的全局/static 变量
├─────────────────────────┤
│   代码段 (Text)          │  ← 机器指令、只读常量
└─────────────────────────┘
低地址
```

The text segment stores compiled machine instructions and some read-only data (such as string literals `"hello"`). This region is typically read-only, and attempting to modify it will directly trigger a segmentation fault. The data segment stores initialized global and `static` variables, whose values are determined at program startup. The BSS segment is part of the data segment, specifically holding uninitialized global and `static` variables—these are automatically initialized to zero, so the executable file doesn't need to store their initial values, only their size. The heap and stack are regions used dynamically at runtime; the former is managed manually by the programmer, while the latter is managed automatically by the compiler.

A key observation about this layout model is that the stack grows from high addresses to low addresses, while the heap grows from low addresses to high addresses, moving toward each other. This means their address ranges won't overlap (unless one side exhausts the available space). Furthermore, if you print the address of a stack variable and a heap variable at the same time, the stack variable will typically have a noticeably larger address value.

## Stack Memory—The Fast Lane of Automatic Management

The stack is the most frequently used memory region in a C++ program. Local variables declared inside functions, function parameters, and return addresses—they all live on the stack. The stack's management is extremely simple and brute-force: a pointer (the stack pointer) points to the current top of the stack. Allocating memory means moving the pointer toward lower addresses, and freeing memory means moving it back toward higher addresses. This "pointer movement" style of allocation doesn't require any searching or merging operations, making stack allocation so fast it has near-zero overhead.

Each time a function is called, the compiler creates a "stack frame" on the stack for that function, containing all of the function's local variables, parameters, and return address. When the function returns, the entire stack frame is popped off, and all local variables are instantly destroyed. This mechanism is called automatic storage duration—the lifetime of a variable is entirely determined by its scope. It is created upon entering the scope and destroyed upon leaving it, without you needing to worry about a thing.

```cpp
#include <iostream>

void foo()
{
    int a = 1;    // 栈上分配
    double b = 2.0; // 紧随 a 之后
    std::cout << "a 的地址: " << &a << "\n";
    std::cout << "b 的地址: " << &b << "\n";
    // 函数返回，a 和 b 的空间自动回收
}

int main()
{
    foo();
    // 这里 a 和 b 已经不存在了
    return 0;
}
```

The stack's downside is also obvious: limited space. On Linux, the default stack size is typically 8 MB (you can check this with `ulimit -s`), and on Windows, it's usually 1 MB. This limit is more than enough for normal function calls, but two scenarios can easily blow past this upper bound.

> **Pitfall Warning**: Allocating large arrays on the stack is one of the most common mistakes beginners make. The seemingly innocent declaration `int arr[10000000];` actually requires about 40 MB of stack space—far exceeding the default limit. The program will immediately segmentation fault on startup, without even having time to output an error message. If you need a large block of memory, please use `std::vector` or allocate on the heap.

Another typical scenario is recursion without a proper termination condition, or recursion that goes too deep. For example, recursively calculating a factorial up to `n = 100000` consumes stack frame space at every level of function call, quickly devouring the entire stack. In a debugger, a stack overflow usually manifests as an abnormally low stack pointer address—for instance, on Linux, you'll see that the value of the `rsp` register has dropped far below the normal stack region range, meaning the stack pointer has crashed straight through the safe boundary heading downward.

There is also a less obvious scenario: in embedded systems, stack space is often much smaller (some RTOS task stacks are only a few KB). In such cases, even ordinary local arrays (like `char buf[512];`) can become hidden dangers. Therefore, we should build a habit when writing code: for data structures exceeding a few hundred bytes, prioritize heap or static allocation instead of defaulting to the stack.

> **Pitfall Warning**: Unlike a failed `new` on the heap, which throws an exception or returns `nullptr`, a stack overflow gives no such grace. When the operating system detects a stack overflow, it directly sends a SIGSEGV signal, and the program terminates immediately. There is no opportunity for graceful handling. When debugging, you can only rely on post-mortem analysis of a core dump or adding counter guards at the recursion entry point.

## Heap Memory—The Free but Dangerous Wilderness

The heap is the largest available memory region in a program—theoretically, it can expand to the maximum value allowed by the operating system (at the TB level on 64-bit systems). When you request memory using `new` or `malloc`, the allocator finds a suitably sized block on the heap and returns it to you. This block will persist until you explicitly release it (using `delete` or `free`). This storage method, where the programmer manually controls the lifetime, is called dynamic storage duration.

The heap's flexibility comes at a cost. The allocator needs to maintain data structures like free lists or buddy systems to track which regions are occupied and which are free. Every `new` requires executing a search algorithm to find a block of the right size, and every `delete` requires executing merge operations to prevent memory fragmentation. This management overhead makes heap allocation orders of magnitude slower than stack allocation. Furthermore, frequently allocating and freeing blocks of different sizes leads to memory fragmentation—even if the total free space is sufficient, the free blocks might be carved into a large number of discontinuous small fragments, making it impossible to satisfy larger allocation requests. This is why high-performance systems use custom allocators or memory pools to bypass the default heap allocation mechanism.

```cpp
#include <iostream>

int main()
{
    // 堆分配
    int* p1 = new int(42);
    int* p2 = new int[1000]; // 数组也在堆上

    std::cout << "p1 指向的地址: " << p1 << "\n";
    std::cout << "p2 指向的地址: " << p2 << "\n";

    // 必须手动释放
    delete p1;
    delete[] p2;

    return 0;
}
```

> **Pitfall Warning**: Forgetting to `delete`, resulting in a memory leak, is one of C++'s most notorious problems. Leaked memory is never reclaimed before the program ends. For short-lived console programs, this usually isn't a big deal (the operating system reclaims all resources when the process exits), but for long-running server programs or embedded systems, a memory leak will slowly eat away at available memory until the system eventually crashes. This is exactly why we repeatedly emphasized RAII in previous chapters—by using smart pointers (`std::unique_ptr`, `std::shared_ptr`) and containers (`std::vector`, `std::string`) to manage dynamic memory, we let destructors automatically release resources, fundamentally eliminating the need for manual `delete`.

## Static and Global Memory—From Program Startup to Termination

Global variables, namespace-scope variables, variables declared with `static`, and class `static` member variables all belong to static storage duration. Their lifetimes span the entire execution of the program: they are created and initialized before `main()` begins executing, and are destroyed only after `main()` returns.

Variables in the static storage area have two initialization methods. If the value is a constant that can be determined at compile time (like `const int kMaxSize = 100;`), the compiler writes the initial value directly into the executable's data segment. If the initial value requires runtime computation (like `static int counter = compute_init_value();`), initialization is completed at program startup, before `main()` executes.

```cpp
#include <iostream>

int global_var = 10;          // 数据段：已初始化全局变量
int global_uninit;             // BSS 段：未初始化全局变量（自动为 0）
const char* kMessage = "hello"; // 数据段：指针本身在数据段
                               // "hello" 字面量在代码段（只读）

void demo()
{
    static int call_count = 0; // 数据段：首次调用时初始化
    ++call_count;
    std::cout << "第 " << call_count << " 次调用\n";
}

int main()
{
    std::cout << "global_var = " << global_var << "\n";
    std::cout << "global_uninit = " << global_uninit << "\n";

    demo(); // 第 1 次
    demo(); // 第 2 次
    demo(); // 第 3 次

    return 0;
}
```

`static` local variables have a very practical feature: lazy initialization. They are only initialized when program execution reaches that declaration statement, not at program startup. Starting with C++11, this initialization is also thread-safe—if multiple threads enter a function containing a `static` local variable for the first time simultaneously, the compiler guarantees that only one thread executes the initialization while the others block and wait. This feature makes `static` local variables the best approach for implementing the thread-safe singleton pattern (Meyer's Singleton).

> **Pitfall Warning**: The construction and destruction order of global variables is undefined across translation units (i.e., different .cpp files). If a global object in `a.cpp` depends on the initialization result of another global object in `b.cpp`, the program might exhibit undefined behavior right at the startup phase—because the standard doesn't guarantee which one is initialized first. This is the notorious "Static Initialization Order Fiasco." The solution is to wrap the global object using a `static` local variable inside a function (Construct On First Use Idiom), leveraging the lazy initialization feature we just discussed to ensure the correct initialization order.

## Hands-on Verification—Printing Addresses of Each Region

After all this theory, let's write a program to verify it in practice. In the following code, we place a variable in each region and print their addresses. By observing the numerical size and relative positions of these addresses, we can intuitively verify the memory layout model.

```cpp
// layout.cpp
// 编译: g++ -std=c++17 -O0 layout.cpp -o layout
// 注意: 使用 -O0 关闭优化，防止编译器对变量做激进优化

#include <cstdint>
#include <iostream>

// 全局变量 —— 数据段（已初始化）
int g_initialized = 42;

// 全局变量 —— BSS 段（未初始化，自动为 0）
int g_uninitialized;

// const 全局 —— 通常在只读段或被编译器内联
constexpr int kGlobalConst = 100;

int main()
{
    // 栈变量
    int stack_var = 1;

    // 堆变量
    int* heap_var = new int(2);

    // static 局部变量 —— 数据段
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

These addresses perfectly verify our layout model: the text segment is at the lowest address, followed closely by the data segment and the BSS segment, the heap is in the lower-middle area growing upward, and the stack is near the highest address growing downward. The address of the `main` function is much smaller than all other variables—it truly lives in the text segment. The addresses of `g_initialized` and `s_static_local` are very close—they both reside in the data segment. The address of `g_uninitialized` is slightly larger than the initialized variables—the BSS segment comes after the data segment. The huge address gap between the stack variable and the heap variable represents the unused space between them.

If you run this program on your own machine, the specific address values will certainly be different (especially the stack address, which changes every run—this is called ASLR, or Address Space Layout Randomization, a security mechanism of the operating system), but the relative size relationships should be consistent. If one day you see the stack address is smaller than the heap address, it's highly likely the compiler performed some special memory layout optimization, or your platform uses a non-traditional memory model—this situation is extremely rare in desktop and server environments.

## Exercises

### Exercise 1: Identify Storage Regions

Determine which memory region (stack, heap, data segment, BSS segment, or text segment) each of the following variables is stored in:

```cpp
const char* msg = "error";    // msg 和 "error" 各在哪里？
static int count;              // ?
int* p = new int[10];         // p 和 p 指向的数组各在哪里？
void func() {
    int local = 0;            // ?
    static int visits = 0;    // ?
}
```

### Exercise 2: Find the Stack Overflow Hazard

What is wrong with the following code? How should it be fixed?

```cpp
void process_image()
{
    // 图像缓冲区：1920 x 1080 x 4 (RGBA) = 约 8 MB
    unsigned char buffer[1920 * 1080 * 4];
    // ... 处理图像 ...
}

int fibonacci(int n)
{
    return fibonacci(n - 1) + fibonacci(n - 2); // 缺少终止条件
}
```

### Exercise 3: Verify the Layout Model

Write a program that declares a local variable, allocates a heap variable, defines a `static` local variable, and prints the address of a global variable, all within the same function. Observe whether their address distribution matches the layout model we described. Then, call a sub-function within that function, and print the address of a local variable inside the sub-function to verify whether the sub-function's stack variable address is smaller than the parent function's (the stack grows toward lower addresses).

## Summary

In this chapter, we broke down a C++ program's memory space into four main regions. The text segment stores compiled machine instructions and read-only constants, and its size is determined at compile time. The data segment and BSS segment store global and `static` variables, which are initialized at program startup and live until the program ends. The stack manages local variables and function call frames; it is automatically managed by the compiler, extremely fast, but limited in space. The heap stores dynamically allocated memory; it is massive but requires manual management (or relies on RAII to let smart pointers do the work).

The key to understanding these regions lies in two dimensions: **where data lives** determines what operations on it are legal (for example, you cannot modify read-only data in the text segment), and **how long data lives** determines when accessing it is safe (for example, stack variables no longer exist after a function returns). Getting these two questions straight provides a solid foundation for subsequently learning dynamic memory management, smart pointers, and memory optimization.

In the next chapter, we will dive into the details of dynamic memory management—what `new` and `delete` actually do, how RAII uses the stack's automatic destruction mechanism to manage heap resources, and how smart pointers let us say goodbye to the nightmare of manual `delete`.
