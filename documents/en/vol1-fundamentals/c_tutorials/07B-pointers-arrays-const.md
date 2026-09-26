---
chapter: 1
cpp_standard:
  - 11
description: Understand array-to-pointer decay in depth, the four combinations of const and pointers, and how to guard against NULL and wild pointers — laying the groundwork for C++ references and smart pointers
difficulty: beginner
order: 10
platform: host
prerequisites:
  - 'Pointer Basics: The World of Addresses'
reading_time_minutes: 11
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
title: Pointers, Arrays, const, and Null Pointers
translation:
  source: documents/vol1-fundamentals/c_tutorials/07B-pointers-arrays-const.md
  source_hash: df0e77a3de5736ad685d9b47f4296bb4ca776bcb781c70940e88ecda478854fd
  translated_at: '2026-09-25T13:05:18+00:00'
  engine: anthropic
  token_count: 2800
---
# Pointers, Arrays, const, and Null Pointers

In the previous chapter we got comfortable with the basic pointer operations—declaration, initialization, address-of, dereference, and pointer arithmetic. Now let's chew through a few of the more twisted but very important applications: what the relationship between arrays and pointers actually is, how many meanings `const` and pointers can take on when combined, and why NULL pointers and wild pointers are so dangerous.

Don't rush—we'll take them one at a time. There's a lot of material here, but the core logic is actually quite clear.

## Step 1 — What an Array Name Actually Is

### "Decay" — A Core Rule

C has one very important rule here: **in most contexts, an array name automatically decays into a pointer to its first element**. The rule sounds academic, but it's actually easy to understand—the array name `numbers` itself stands for a whole contiguous block of memory, yet when you assign it to a pointer or pass it to a function, the compiler hands over only the starting address of that block, and the array's length information is "lost".

```c
int numbers[5] = {1, 2, 3, 4, 5};
int* ptr = numbers;      // Legal: numbers decays to &numbers[0]
```

The type of `numbers` itself is `int[5]` (an array containing 5 ints), but when assigned to a pointer it automatically converts to `int*` (a pointer to the first element). This means `numbers[i]` and `*(numbers + i)` are fully equivalent—the subscript operator `[]` is essentially syntactic sugar over pointer arithmetic.

This is exactly why we can traverse an array with pointers:

```c
int numbers[5] = {10, 20, 30, 40, 50};

for (int* p = numbers; p < numbers + 5; p++) {
    printf("%d ", *p);
}
```

Let's verify—compile and run:

```bash
gcc -Wall -Wextra -std=c17 array_ptr.c -o array_ptr && ./array_ptr
```

Output:

```text
10 20 30 40 50
```

### But — an Array Is Not a Pointer

Here's the crux: an array name merely "often decays to a pointer"—**an array itself is not a pointer**. There are two contexts where the array name does not decay:

First, the `sizeof` operator. `sizeof(numbers)` returns the size in bytes of the entire array (5 × 4 = 20 bytes), not the size of a pointer (4 or 8 bytes). This is the trick we used in the previous chapter to count array elements: `sizeof(numbers) / sizeof(numbers[0])`.

Second, the `&` operator. The type of `&numbers` is "pointer to the entire array" (`int(*)[5]`), not "pointer to a pointer" (`int**`). It has the same value as `numbers` (both are the address of the array's first byte), but the type differs—and so does the stride of pointer arithmetic.

Let's verify these differences:

```c
#include <stdio.h>

int main(void)
{
    int numbers[5] = {10, 20, 30, 40, 50};

    printf("sizeof(numbers)    = %zu（整个数组）\n", sizeof(numbers));
    printf("sizeof(&numbers)   = %zu（指针大小）\n", sizeof(&numbers));
    printf("numbers 的值       = %p\n", (void*)numbers);
    printf("&numbers 的值      = %p\n", (void*)&numbers);
    printf("numbers + 1        = %p（跳过一个 int）\n", (void*)(numbers + 1));
    printf("&numbers + 1       = %p（跳过整个数组）\n", (void*)(&numbers + 1));

    return 0;
}
```

Output:

```text
sizeof(numbers)    = 20（整个数组）
sizeof(&numbers)   = 8（指针大小）
numbers 的值       = 0x7ffd1234abcd
&numbers 的值      = 0x7ffd1234abcd
numbers + 1        = 0x7ffd1234abd1（跳过一个 int，+4）
&numbers + 1       = 0x7ffd1234abe1（跳过整个数组，+20）
```

As expected, `numbers` and `&numbers` have the same value, but `numbers + 1` skipped only 4 bytes (one `int`), while `&numbers + 1` skipped 20 bytes (the entire array). That's "different type, different stride" in action.

Once an array is passed to a function it is guaranteed to decay to a pointer—inside the function, `sizeof(arr)` returns the size of a pointer, not the size of the array. So if a function needs to know the array's length, you must pass the length in as a separate parameter.

## Step 2 — The Four Combinations of const and Pointers

Combining `const` with pointers is a classic interview question, and something you'll use constantly in real code. There are four combinations in total; let's break them down one by one, starting with the most intuitive.

### 1. A Non-const Pointer to const Data

```c
const int* p1 = &value;
// *p1 = 100;   // Error: cannot modify the pointed-to data through p1
p1 = &other;    // Legal: the pointer itself may point somewhere else
```

`const int*` means "the int that p1 points to is read-only"—you can't change that value through `p1`, but `p1` itself is free to point at other variables. Note that `value` doesn't have to be `const` itself; you're simply promising not to modify it through this particular channel. This pattern is extremely common in function parameters—`void process(const int* data)` is telling the caller, "relax, I promise I won't touch your data".

### 2. A const Pointer to Non-const Data

```c
int* const p2 = &value;
*p2 = 100;      // Legal: the pointed-to data can be modified
// p2 = &other;  // Error: the pointer itself is immutable
```

The pointer itself is `const`—once initialized it points at the same address forever, but you can still modify the data in that memory through it. This is very common in embedded development, for example mapping a hardware register at a fixed address:

```c
volatile unsigned int* const kGpioBase = (volatile unsigned int*)0x40020000;
```

The pointer's value (the address) is fixed, while the register can still be read and written through it.

### 3. A const Pointer to const Data

```c
const int* const p3 = &value;
// *p3 = 100;    // Error
// p3 = &other;  // Error
```

Both sides are locked down—the pointer can't change direction, and the data can't be modified through the pointer. This is typically used for accessing read-only hardware registers or constant lookup tables.

### 4. A Plain `int*`

This is just the ordinary `int* p`—both sides can change, no special constraints.

### How to Read These Declarations

One practical reading trick: look at which side of the `*` the `const` appears on.

- `const` on the **left** of the `*`: it modifies the **pointed-to data** (the data can't change)
- `const` on the **right** of the `*`: it modifies the **pointer itself** (the direction can't change)
- It appears on both sides: neither can change

Reading the declaration from right to left is another good method: `const int* p` → "p is a pointer to int const" (a pointer to a const int); `int* const p` → "p is a const pointer to int" (a const pointer to an int).

## Step 3 — NULL Pointers and Wild Pointers

### NULL — "I'm Not Pointing at Anything"

`NULL` is a macro whose value is `(void*)0`, meaning "points at no valid memory address". Dereferencing a NULL pointer is undefined behavior—on most systems it triggers a segmentation fault (SIGSEGV), and the program crashes on the spot.

A segfault sounds terrible, but it's actually a "good crash"—the problem surfaces immediately, and one look with a debugger tells you it's a null-pointer dereference. Compared with that, the wild pointers we're about to discuss are the genuinely scary thing.

### Wild Pointers — Time Bombs in Your Code

A wild pointer is a pointer that points at invalid memory. It usually has three sources:

The first is the **uninitialized pointer**—declared but never assigned, it holds a random value from the stack, and that address could point anywhere. The second is the **dangling pointer**—a pointer that once pointed at valid memory, but that memory has since been freed (continuing to use the pointer after `free`). The third is **out-of-bounds access**—pointer arithmetic that runs past the legal range.

```c
// Uninitialized — the most classic wild pointer
int* wild;
*wild = 42;   // Undefined behavior: writing 42 to a random address

// Dangling pointer
int* dangling = (int*)malloc(sizeof(int));
free(dangling);
*dangling = 42;  // Undefined behavior: the memory has been freed

// Good habit: set it to NULL after freeing
dangling = NULL;
```

The terrifying part of a wild pointer is that it doesn't necessarily crash right away—it might happen to point at writable memory, your program "appears" to run normally, while some unrelated variable has been quietly overwritten by you. The symptom and the cause of this kind of bug can be a hundred thousand miles apart, and tracking it down will send your blood pressure through the roof.

A wild pointer creates a "Schrödinger's bug"—in your program, everything may look perfectly fine until one day you switch compilers or enable optimization and it suddenly crashes. What's more, the crash site is often far from the real bug, which makes the investigation extremely painful.

### Three Defensive Rules

The best defense is actually quite simple—just remember these three rules:

1. **Initialize a pointer the moment you declare it**—even initializing it to `NULL` counts
2. **Set it to `NULL` immediately after `free`**—prevents accidental misuse later
3. **Check whether a pointer is `NULL` before using it**—adds a layer of protection

```c
int* safe_ptr = NULL;

// ... memory gets allocated somewhere ...

if (safe_ptr != NULL) {
    *safe_ptr = 42;   // Safe: use it only after confirming it's non-null
}
```

These three rules will help you dodge the vast majority of pointer-related disasters. Our sincere advice here: burn these three into your muscle memory, and you'll keep a lot more hair in the years of coding ahead.

## Bridging to C++

Raw pointers in C are powerful, but all the responsibility rests on the programmer. C++ builds on this foundation with a few very significant moves.

First, **references**. `int& r = value` is essentially a const pointer that the compiler dereferences for you automatically—it must be initialized at declaration, cannot be rebound once bound, needs no `*` when used, and syntactically reads like manipulating the original variable directly. A reference can never be NULL (well, strictly speaking you can construct a dangling reference, but that takes deliberate self-destruction), and it can never point at uninitialized memory. For C++ function parameters, passing by reference is preferred over passing pointers.

Then there are **smart pointers**. `std::unique_ptr` and `std::shared_ptr` manage the memory's lifetime automatically through RAII—when the pointer goes out of scope, the memory is released automatically, eliminating at the root the memory leaks and dangling pointers that manual `malloc`/`free` cause.

```cpp
// C++ smart pointers — a sneak preview
#include <memory>

std::unique_ptr<int> p = std::make_unique<int>(42);
// *p == 42, used exactly the same way as a raw pointer
// Automatically deleted on leaving scope; no manual release needed
```

We'll discuss all of this in depth in the C++ tutorials ahead. For now, you only need one core idea: **C++'s philosophy is to make management automatic through the type system and object lifetimes, rather than relying on the programmer's self-discipline**.

## Exercises

### Exercise 1: Linear Search, Pointer Edition

**Difficulty: Basic** · pointer traversal plus a NULL return

Implement a linear search function that returns a pointer to the first occurrence of a target value in an array. If the target is not found, return `NULL`.

```c
/// @brief Linearly search an int array for a target value
/// @param data address of the array's first element
/// @param count number of elements
/// @param target the value to search for
/// @return pointer to the target element, or NULL if not found
const int* linear_search(const int* data, size_t count, int target);
```

::: details Reference solution

```c
#include <stdio.h>
const int* linear_search(const int* data, size_t count, int target);

int main(void) {
    const int arr[] = {10, 21, 32, 43, 54, 65, 76, 87, 98, 9};
    int target = 43;

    // Use sizeof to compute the array size automatically
    size_t count = sizeof(arr) / sizeof(arr[0]);

    const int* result = linear_search(arr, count, target);

    if (result != NULL) {
        printf("找到了目标 %d，位于地址 %p，是数组的第 %llu 个元素。\n",
               target, (void*)result, (unsigned long long)(result - arr));
    } else {
        printf("未找到目标 %d。\n", target);
    }

    return 0;
}

const int* linear_search(const int* data, size_t count, int target) {
    // Defensive programming: if the pointer is null, or the count is 0, return NULL right away
    if (data == NULL || count == 0) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        if (*(data + i) == target) {
            return data + i;
        }
    }
    return NULL;
}
```

After running it you'll see output like the following (that long address number differs on every run—that's the system's address randomization, ASLR, at work, which is perfectly normal):

```text
找到了目标 43，位于地址 0x7ffd02b50bac，是数组的第 3 个元素。
```

The address itself isn't worth memorizing—the key is that trailing part saying the target is element #3 of the array: `result - arr` works out to exactly `3`, and that conclusion is rock solid. By the way: if the Chinese text in your terminal shows up as garbage, that's a terminal-encoding problem (your terminal isn't using UTF-8), not a code problem—switch to WSL2 or a modern terminal and it goes away.

:::

### Exercise 2: Array Reversal, Pointer Edition

**Difficulty: Intermediate** · in-place reversal with two pointers

Implement a function that reverses an array in place, using only pointer arithmetic (two pointers converging from both ends toward the middle) and no array subscripts:

```c
/// @brief Reverse an int array in place
/// @param data address of the array's first element
/// @param count number of elements
void reverse_array(int* data, size_t count);
```

::: details Reference solution

```c
#include <stdio.h>

void reverse_array(int* data, size_t count);

int main(void) {
    int arr[] = {10, 21, 32, 43, 54, 65, 76, 87, 98, 9};
    int arr_size = sizeof(arr) / sizeof(arr[0]);

    printf("before reverse_array:\n");
    for (int i = 0; i < arr_size; i++) {
        printf("%d\n", arr[i]);
    }

    reverse_array(arr, arr_size);

    printf("after reverse_array:\n");
    for (int i = 0; i < arr_size; i++) {
        printf("%d\n", arr[i]);
    }

    return 0;
}

void reverse_array(int* data, size_t count) {
    if (data == NULL) {
        printf("data is null\n");
        return;
    }
    if (count == 0) {
        printf("data is empty\n");
        return;
    }

    int* start = data;
    int* end = data + count - 1;
    int temp = 0;
    while (start < end) {
        temp = *start;
        *start = *end;
        *end = temp;
        start++;
        end--;
    }
}
```

The terminal should print:

```text
before reverse_array:
10
21
32
43
54
65
76
87
98
9
after reverse_array:
9
98
87
76
65
54
43
32
21
10
```

:::

### Exercise 3: const Practice

**Difficulty: Basic** · the four combinations of const and pointers

For each of the declarations below, determine which operations are legal and which would be compile errors:

```c
int value = 42, other = 100;

const int* p1 = &value;
int* const p2 = &value;
const int* const p3 = &value;

// For each pointer p1/p2/p3, determine whether these operations are legal:
// *px = 50;      // modify the data through the pointer
// px = &other;   // change where the pointer points
```

::: details Reference solution

```c
int value = 42, other = 100;

const int* p1 = &value;
int* const p2 = &value;
const int* const p3 = &value;

// The six lines below test legality; read them line by line against the
// declarations above (they're all written as comments, because the illegal
// ones would fail to compile if written out for real):
// *p1 = other;   // Illegal: p1 points to const int, no way to write through it
// *p2 = other;   // Legal: p2 is a const pointer (fixed target), but it points to a plain int, so the value can change
// *p3 = other;   // Illegal: p3 points to const int and is itself const—neither the value nor the direction can change
//
// p1 = &other;   // Legal: p1 itself is not a const pointer, so it can be redirected
// p2 = &other;   // Illegal: p2 is a const pointer, its target cannot change
// p3 = &other;   // Illegal: p3 is likewise a const pointer, its target cannot change

```

:::

## References

- [cppreference: Pointer declaration](https://en.cppreference.com/w/c/language/pointer)
- [cppreference: NULL](https://en.cppreference.com/w/c/types/NULL)
- [cppreference: Array-to-pointer decay](https://en.cppreference.com/w/c/language/conversion)
