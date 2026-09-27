---
title: "Memory Layout"
description: "Understand the memory model of the stack, heap, static storage, and the code segment, and learn to analyze where variables are stored and how long they live"
chapter: 12
order: 1
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
  - "Comparing Error Handling Approaches"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol1-fundamentals/ch12/01-memory-layout.md
  source_hash: 5c5870ff61203d6fd76901fc0f1f4e892a8338151a9b99d48b23fb49c0378972
  translated_at: '2026-09-27T04:12:43+00:00'
  engine: anthropic
  token_count: 9000
---

# Memory Layout: Where the 42 in `int x = 42` Actually Lives

In the previous chapter we compared three styles of error handling, and those failures could still earn the word "handled": division by zero, a file that won't open—return values and exceptions will always catch one of them. But there is one kind of failure that doesn't even give those mechanisms time to walk on stage: the moment a segmentation fault appears, the process is simply gone, and there is no return value left to check.

To understand where this kind of problem comes from, we first have to answer a question that has been hanging in the air since the very beginning: when we write `int x = 42;`, where exactly does that `42` live? **What position does it occupy in memory? When is it created, and when is it destroyed?**

If we don't understand these questions, sooner or later we are in for a big fall, and when something goes wrong all we can do is guess blindly: the crash address clearly points at the stack, yet you can't read that information out of it.

> TL;DR: understanding memory layout essentially comes down to getting two things straight: **where the data is stored**, and **how long it lives**.

## The Four Major Memory Regions

When a C++ program runs, the operating system allocates a block of virtual address space for it. That block is not one homogeneous slab; it is carved into several segments, each with its own purpose and its own way of being managed. For us, the four regions that matter most are these:

![Process virtual address space layout](./01-memory-layout.drawio)

Let's first see what each region holds. The text segment stores the compiled machine instructions and some read-only data (the string literal `"hello"`, for instance); this region is usually read-only, and any attempt to modify it triggers a segmentation fault on the spot. The data segment holds initialized global and `static` variables, whose values are already settled when the program starts. The BSS segment is the part of the data segment reserved for uninitialized global and `static` variables—these variables are automatically initialized to zero, so the executable doesn't need to store their initial values at all; recording their size is enough. The heap and the stack, meanwhile, are the regions used dynamically at runtime: the former is managed by hand by the programmer, the latter automatically by the compiler.

One key observation about this layout model: the stack grows from high addresses toward low ones, the heap grows from low addresses toward high ones, and the two march toward each other. This means their address ranges will not overlap (unless one side exhausts the available space), and if we print the address of a stack variable and a heap variable side by side, the stack variable usually shows up with a noticeably larger value.

## Stack Memory—The Compiler Manages It In and Out

The stack is the most heavily used memory region in a C++ program. **Local variables declared inside functions, function parameters, return addresses—they all live on the stack**. The stack's management style is brutally simple: **a single pointer (the stack pointer) marks the current top of the stack; allocating memory means shoving the pointer toward lower addresses, and freeing memory means moving it back up toward higher ones**. This "pointer-shoving" style of allocation needs no searching and no merging, which is why stack allocation is so fast it amounts to nearly zero overhead.

Every time a function is called, the compiler creates a stack frame for it on the stack, containing all of that function's local variables, its parameters, and the return address. When the function returns, the entire frame is popped and every local variable is destroyed in an instant. This mechanism is called automatic storage duration: a variable's lifetime is decided entirely by its scope—created on entering the scope, destroyed on leaving it—and we don't have to lift a finger.

```cpp
#include <iostream>

void foo()
{
    int a = 1;    // Allocated on the stack
    double b = 2.0; // Right after a
    std::cout << "a 的地址: " << &a << "\n";
    std::cout << "b 的地址: " << &b << "\n";
    // When the function returns, a's and b's storage is reclaimed automatically
}

int main()
{
    foo();
    // Here, a and b no longer exist
    return 0;
}
```

Some readers, seeing how absurdly efficient the stack is, go straight into takeoff mode—stack, stack, stack for everything. I say don't turn it into dogma; dogmatism kills. Let me start with the Linux I know best: the default stack size there is usually 8 MB (you can check with `ulimit -s`), while on Windows it is usually 1 MB. For ordinary function calls that quota is more than enough, but—well—there are two scenarios that can blow through our stack space easily, without any pressure at all.

The first is the mistake beloved of players who got suckered into "C++ is just for grinding algorithm problems". **Writing `int arr[10000000];` directly on the stack looks like nothing, but this seemingly harmless declaration actually needs about 40 MB of stack space**—far beyond the default limit. The program hits a segmentation fault the moment it starts, without even having time to print an error message. When we need a large block of memory, use `std::vector` or allocate on the heap.

> A while ago, while answering a friend's question, I nearly couldn't keep a straight face: he had plopped a very, very gigantic `int arr[10000000];` right inside `main()`, then came over asking, "Hey newbie, why does this code blow up?" When I went over, warm-hearted, to take a look, I couldn't hold it in either—I asked him whether he knew the stack on Windows is only 1 MB while he had just used 40 MB of it. It dawned on him that he had put it in the wrong place (though I later told him that writing it that way anywhere is still a low-grade habit—please use a `vector` instead; the memory cost is more elegant too, no?).
>
> True, an OJ contest is no place for engineering conventions (in fact, rigidly clinging to engineering conventions is just another brand of dogmatism), but in ordinary program design, **this is clearly inappropriate—even downright wrong!**

The other kind—everybody, please raise your guard—because we all wrote recursion when we were getting started. Recursion really is a fine thing: big problems get split into small ones, small ones into no problem at all. It fits our instincts for tackling engineering problems. B! U! T! It only fits the instincts, because in many settings the dataset we face is unknown, and the recursive code we write can easily receive input that sends the recursion chain to extreme depths—and that easily punches through a stack.

For example, recursing down to `n = 100000` when computing a factorial: every level of the function call devours stack-frame space like mad. That kind of operation eats through the stack in no time.

> In a debugger, a stack overflow usually shows up as an abnormally low stack pointer address: on Linux we will see the `rsp` register's value already far below the normal range of the stack region—the stack pointer has charged straight down past the safety boundary.

Actually, there is a third level... any embedded folks out there! Look over here! For embedded systems, stack space is often even smaller (some RTOS task stacks are only a few KB), and at that point you hardly dare to put even an ordinary local array on it (something like `char buf[512];`). Any one of them can become a bomb that detonates right where you like it—or don't. So build a habit while writing code: for data structures larger than a few hundred bytes, prefer heap allocation or static allocation, and don't default to throwing them on the stack.

> Want exception handling? Not a chance~. A stack overflow doesn't throw an exception or return `nullptr` the way a failed heap `new` does; when the operating system detects a stack overflow it sends SIGSEGV directly, and the program terminates immediately. There is no opportunity for graceful handling—when debugging, all we can rely on is after-the-fact analysis of a core dump, or a counter guard at the recursion entry.

## Heap Memory—Tremendous Freedom, and an Accident Hotspot

The heap is the largest usable memory region in a program—in theory it can grow all the way to the maximum the operating system allows (terabyte scale on 64-bit systems). When we request memory with `new` or `malloc`, the allocator finds a suitably sized block on the heap and hands it back; that block keeps existing until it is explicitly released (`delete` or `free`). This style of storage, with its lifetime controlled manually by the programmer, is called dynamic storage duration.

The heap's flexibility, you see, comes at a price. The allocator has to maintain data structures such as free lists or buddy systems to track which regions are occupied and which are free; every `new` runs a search algorithm to find a block of the right size, and every `delete` runs a coalescing operation to keep memory from fragmenting. These management overheads make heap allocation several orders of magnitude slower than stack allocation. What's more, frequently allocating and freeing blocks of different sizes leads to memory fragmentation: the total free space may be sufficient, but because it has been chopped into a large number of disconnected little fragments, it cannot satisfy a bigger allocation request. That is also why high-performance systems use custom allocators or memory pools to bypass the default heap allocation machinery.

```cpp
#include <iostream>

int main()
{
    // Heap allocation
    int* p1 = new int(42);
    int* p2 = new int[1000]; // Arrays live on the heap too

    std::cout << "p1 指向的地址: " << p1 << "\n";
    std::cout << "p2 指向的地址: " << p2 << "\n";

    // Must be released manually
    delete p1;
    delete[] p2;

    return 0;
}
```

Forgetting `delete` and causing a memory leak **is one of C++'s most notorious problems** (whenever I think of how, on my own company's complaint-and-feedback platform, I once saw the professional-grade "Do you people even write C++? How are the memory leaks this bad? Where is the skill?"—I can't help a quiet little snort.).

The serious part is this: **leaked memory is never reclaimed before the program ends**. For a short-lived console program that is usually no big deal (the operating system reclaims all resources when the process exits), but for **long-running server programs or embedded systems**, a memory leak nibbles the available memory away bit by bit until the system finally crashes. This is why the earlier chapters kept hammering on RAII: manage dynamic memory with smart pointers (`std::unique_ptr`, `std::shared_ptr`) and containers (`std::vector`, `std::string`), let destructors release resources automatically, and eliminate the need for manual `delete` at the root.

## Static and Global Memory—From Program Startup to the Very End

Global variables, variables at namespace scope, variables declared `static`, and a class's `static` member variables all belong to static storage duration. Their lifetimes span the entire run of the program: they are created and initialized before `main()` starts executing, and are destroyed only after `main()` returns.

Variables in static storage come with two flavors of initialization. For constants whose values can be settled at compile time (say `const int kMaxSize = 100;`), the compiler writes the initial value straight into the executable's data segment; for initial values that must be computed at runtime (say `static int counter = compute_init_value();`), initialization is completed at program startup, before `main()` runs.

```cpp
#include <iostream>

int global_var = 10;          // Data segment: initialized global variable
int global_uninit;             // BSS segment: uninitialized global variable (automatically 0)
const char* kMessage = "hello"; // Data segment: the pointer itself sits in the data segment
                               // the "hello" literal sits in the code segment (read-only)

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

A `static` local variable has one very practical property: lazy initialization. It is initialized only when program execution reaches that declaration statement, not at program startup. Since C++11 this initialization is also thread-safe—if several threads first enter a function containing a `static` local variable at the same time, the compiler guarantees that only one thread performs the initialization while the others block and wait. This property makes the `static` local variable the best way to implement a thread-safe singleton (the Meyers' Singleton).

The construction and destruction order of global variables is undefined across translation units (that is, across different .cpp files). If a global object in `a.cpp` depends on the initialization result of another global object in `b.cpp`, the program can hit undefined behavior as early as the startup phase—the standard simply doesn't guarantee which one initializes first. This is the infamous "Static Initialization Order Fiasco". The solution is to wrap the dependency in a `static` local variable inside a function (the Construct On First Use idiom), exploiting the lazy-initialization property we just described to guarantee a correct initialization order.

## Hands-On Verification—Printing the Addresses of Each Region

That was a lot of theory, so let's write a program and verify it for real. In the code below, we place one variable in each region and then print their addresses. By observing the magnitudes of the addresses and their relative positions, we can check the memory layout model with our own eyes.

```cpp
// layout.cpp
// Compile: g++ -std=c++17 -O0 layout.cpp -o layout
// Note: -O0 turns optimization off, preventing the compiler from applying aggressive optimizations to these variables

#include <cstdint>
#include <iostream>

// Global variable — data segment (initialized)
int g_initialized = 42;

// Global variable — BSS segment (uninitialized, automatically 0)
int g_uninitialized;

// const global — usually in a read-only segment, or inlined by the compiler
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

We've put the complete code below; hit "Try It Yourself" to run it directly (the compile options are already set to -O0, for the reason given in the text above):

<OnlineCompilerDemo
  title="Hands-On Verification: layout.cpp"
  source-path="code/examples/vol1/27_memory_layout.cpp"
  description="Print the addresses of variables living in the code segment, data segment, BSS, stack, and heap, right here online. Run it several times: the stack and heap addresses change on every run (ASLR), but the three ordering relationships never change."
  run-options="-O0 -std=c++17"
  allow-run
/>

These addresses verify our layout model beautifully: the code segment sits at the lowest addresses, the data segment and BSS follow right above it, the heap grows upward from the low-ish middle of the space, and the stack grows downward from a position near the very top. The address of `main` is far smaller than every other variable—it really is in the code segment. The addresses of `g_initialized` and `s_static_local` are very close together; both are in the data segment. The address of `g_uninitialized` is slightly larger than the initialized ones—the BSS segment comes after the data segment. And the huge address gap between the stack variable and the heap variable is exactly that stretch of unused space between the two.

Run this program on your own machine and the concrete address values will certainly differ (the stack address in particular changes on every run—this is ASLR, address space layout randomization, a security mechanism of the operating system), but the relative orderings should hold. If one day you see the stack address come out smaller than the heap address, most likely the compiler performed some special memory-layout optimization, or your platform uses a non-traditional memory model—both extremely rare in desktop and server environments.

## Exercises

### Exercise 1: Identify the Storage Region

For each variable below, determine which memory region it lives in (stack, heap, data segment, BSS segment, code segment):

```cpp
const char* msg = "error";    // Where do msg and "error" each live?
static int count;              // ?
int* p = new int[10];         // Where do p and the array it points to each live?
void func() {
    int local = 0;            // ?
    static int visits = 0;    // ?
}
```

### Exercise 2: Find the Stack Overflow Hazard

What is wrong with the following code? Think about how it should be fixed:

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

Write a program that, inside a single function, declares a local variable, allocates a heap variable, defines a `static` local variable, and prints the address of a global variable. Observe whether their address distribution matches the layout model we described. Then, from within that function, call a child function and print the address of a local variable in it, verifying that the child function's stack variable has a smaller address than the parent's (the stack grows toward lower addresses).
