# Multi-Level Pointers and Reading Declarations

In the previous chapter, we clarified the relationships between pointers, arrays, `void*`, and `NULL`. Now let's tackle the trickier parts of pointers—multi-level pointers (pointers to pointers), the "confusing twins" of pointer arrays and array pointers, and a method to keep your brain from crashing when you see declarations like `int (*(*fp)(int))[10]`.

Honestly, these concepts are easy to mix up when you're just starting out. But in my experience, don't try to rote-memorize them. Once you master a methodology for reading declarations, you can break down even the most complex ones. More importantly, C++ features like `unique_ptr`, `shared_ptr`, and pointer transfers via move semantics are all built on these underlying mechanisms.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the memory model and practical use cases of multi-level pointers
> - [ ] Distinguish between pointer arrays and array pointers
> - [ ] Break down any C declaration using the cdecl reading method
> - [ ] Correctly read and write multi-level `const` pointer declarations

## Environment Setup

We will run all the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c++17 -Wall -Wextra -g`

## Step 1 — Understand What Multi-Level Pointers Actually Point To

### Memory Model: Chains Within Chains

If the address stored in a pointer points to another pointer, that's a multi-level pointer. `int**` points to `int*`, `int***` points to `int**`, and `int****` points to `int***`, and so on. In memory, they form a chain:

![Multi-level pointer memory chain](images/multi-level-pointer-chain.svg)

Each level stores the address of the next level. Dereferencing `pp` yields `p` (an `int*`), dereferencing `p` yields `val` (an `int`), and only then do we get the final `42`. Let's verify this:

```cpp
#include <cstdio>

int main() {
    int val = 42;
    int* p = &val;
    int** pp = &p;

    printf("val  = %d\n", val);
    printf("*p   = %d\n", *p);
    printf("**pp = %d\n", **pp);

    printf("&val = %p\n", (void*)&val);
    printf("p    = %p\n", (void*)p);
    printf("*pp  = %p\n", (void*)*pp);
    printf("&p   = %p\n", (void*)&p);
    printf("pp   = %p\n", (void*)pp);
}
```

```text
val  = 42
*p   = 42
**pp = 42
&val = 0x7ffd12345678
p    = 0x7ffd12345678
*pp  = 0x7ffd12345678
&p   = 0x7ffd12345680
pp   = 0x7ffd12345680
```

Great, each level of dereferencing moves downstream along the chain, ultimately fetching `42`.

### When to Use Multi-Level Pointers

To be honest, situations requiring more than two levels are rare in normal projects. The most common scenario is: **when you want to modify a pointer variable itself inside a function** (not the data it points to), you need to pass the address of that pointer into the function:

```cpp
void alloc_int(int** pp) {
    *pp = new int(42);  // Modify the pointer variable itself
}

int main() {
    int* p = nullptr;
    alloc_int(&p);      // Pass the address of p
    printf("%d\n", *p); // 42
    delete p;
}
```

C only supports pass-by-value. To modify the `p` variable itself, we must pass `&p`—which is an `int**`.

> ⚠️ **Pitfall Warning**
> Multi-level pointers are not for showing off. Pointers with three or more levels should not appear in the vast majority of projects—if you find yourself writing `int****`, there is likely a design flaw. Use structs to encapsulate data instead of using raw multi-level pointers.

### argv — The Most Common Double Pointer

The `argv` parameter of the `main` function is an `char**`:

```cpp
int main(int argc, char* argv[]) { /* ... */ }
int main(int argc, char** argv)  { /* ... */ }
```

`char* argv[]` in a parameter list decays to `char** argv`, so the two forms are exactly the same. `argv` points to a `char*` array, where each element points to a command-line argument string, terminated by a `NULL` sentinel:

![argv memory layout](images/argv-memory-layout.svg)

## Step 2 — Distinguish Between Pointer Arrays and Array Pointers

`int* arr[10]` and `int (*arr)[10]` look like they only differ by a pair of parentheses, but their meanings are completely different. This is the most classic pair of "confusing twins" in C declaration syntax.

### Pointer Array: `int* arr[10]`

`int* arr[10]` declares an **array** containing 10 `int*` elements:

```cpp
int a = 1, b = 2, c = 3;
int* arr[3] = {&a, &b, &c};

printf("%d\n", *arr[0]); // 1
printf("%d\n", *arr[1]); // 2
printf("%d\n", *arr[2]); // 3
```

Memory layout—the array contiguously stores three pointer values, and each pointer points to a different `int`:

![Pointer array memory layout](images/pointer-array-layout.svg)

### Array Pointer: `int (*arr)[10]`

`int (*arr)[10]` declares a **pointer** that points to an entire row of an array containing 10 `int` elements. The most common use case is working with 2D arrays:

```cpp
int matrix[3][10] = {0};
int (*arr)[10] = matrix; // arr points to the first row

arr[0][0] = 42;
arr[1][5] = 99; // arr + 1 skips an entire row (10 ints = 40 bytes)
```

`arr + 1` skips an entire row (10 `int`s = 40 bytes), pointing to the next row.

> ⚠️ **Pitfall Warning**
> `*arr + 1` is not the answer you want—`[]` has higher precedence than `*`, so this first evaluates `arr[1]` and then dereferences it, yielding completely wrong results. The correct way must include parentheses: `(*arr)[1]`. Precedence issues are one of the most common sources of bugs in C.

## Step 3 — Master the cdecl Reading Method

There is a systematic way to read any C declaration, called the "right-left rule" (also known as the spiral rule). The core principle: **start from the identifier, read to the right, then read to the left, and jump to the next level when you encounter parentheses.**

Take `int* a[10]` as an example:

1. Find the identifier `a`
2. Go right: `[10]` — "a is an array of 10 elements"
3. Go left: `int*` — "of type pointer to int"
4. Combined: **a is an array of 10 elements of type pointer to int (pointer array)**

Take `int (*a)[10]` as an example:

1. Identifier `a`
2. Blocked by parentheses on the right, go left first: `*` — "a is a pointer"
3. Exit parentheses, go right: `[10]` — "to an array of 10 elements"
4. Go left: `int` — "of type int"
5. Combined: **a is a pointer to an array of 10 int elements (array pointer)**

Now let's look at a function pointer: `int (*func)(double)`

1. Identifier `func`
2. Blocked by parentheses, go left: `*` — "func is a pointer"
3. Exit parentheses, go right: `(double)` — "to a function taking a double parameter"
4. Go left: `int` — "returning int"
5. Combined: **func is a function pointer pointing to a function that takes a double and returns an int**

You'll get the hang of this method after a few practice rounds, and you won't panic when you see any weird declaration in the future. You can also use the online tool [cdecl.org](https://cdecl.org/) to verify your reading.

> ⚠️ **Pitfall Warning**
> In the declaration `int* a, b;`, `a` is an `int*`, but `b` is just an `int`—not two pointers. The `*` follows the declarator, not the type. If you really want to declare two pointers, you must write `int *a, *b;`. This trap has tripped up countless people.

## Step 4 — Combinations of const and Multi-Level Pointers

The combinations of `const` and single-level pointers were covered in the previous chapter. Now let's look at multi-level cases—the core principle remains the same: **`const` modifies the type immediately to its left (if it's at the far left, it modifies the type to its right)**.

### Review: Single-Level const Pointers

```cpp
const int* p1;       // Pointer to const int (can't modify *p1)
int* const p2;       // Const pointer to int (can't modify p2)
const int* const p3; // Const pointer to const int (can't modify either)
```

### Multi-Level const Pointers

When `int**` is involved, `const` can be added at different positions:

```cpp
const int** pp1;       // Pointer to pointer to const int
int* const* pp2;       // Pointer to const pointer to int
int** const pp3;       // Const pointer to pointer to int
const int* const* pp4; // Pointer to const pointer to const int
const int** const pp5; // Const pointer to pointer to const int
int* const* const pp6; // Const pointer to const pointer to int
const int* const* const pp7; // The ultimate form: everything is const
```

We still use the right-left rule to break it down layer by layer. Take `int* const* pp2` as an example: `pp2` is a pointer → to a `const` pointer → to an `int`.

This kind of thing is indeed uncommon in practice, but understanding how to read it is very important—similar complex types frequently appear in C++ standard library function signatures and template error messages.

## C++ Connections

The multi-level pointer mechanisms in C all have modern counterparts in C++. Understanding the underlying principles helps us better use these high-level tools.

`std::vector` automatically manages dynamic arrays, eliminating the need for manual `new`/`delete`. The pain of manually managing 2D arrays with `malloc` in C (allocating, freeing row by row, easily forgetting)—can be done in a single line in C++:

```cpp
#include <vector>

// A 2D array of 3 rows and 10 columns, all initialized to 0
std::vector<std::vector<int>> matrix(3, std::vector<int>(10, 0));

matrix[1][5] = 42; // Type-safe access
```

Move semantics are essentially pointer transfers—instead of copying data, the ownership of the resource is "stolen" and the source object is nullified. This is exactly the same as manually swapping pointers and nullifying them in C, except C++ has standardized this pattern.

`std::span` packages the classic C combination of "pointer + length" into a single type-safe object. There is no need to manually manage the length, and it can be automatically constructed from arrays, vectors, and `std::array`.

`std::reference_wrapper` provides rebindable reference semantics, and can replace multi-level pointers when storing "references" inside containers.

We will dive deep into these topics in subsequent C++ tutorials. For now, just remember the core idea: **the philosophy of C++ is to use the type system to automatically manage resources, rather than relying on the programmer's discipline**.

## Summary

The core logic of multi-level pointers is actually quite simple: each level stores the address of the next level, and dereferencing means moving downstream along the chain. What's truly easy to confuse are pointer arrays and array pointers—just remember to "look at the parentheses first, then read in the direction." The cdecl reading method is the most important practical skill in this chapter; practice it a few times and you'll be able to break down any declaration. For multi-level `const`, use the right-left rule to analyze layer by layer, don't try to read it all in one breath.

## Exercises

### Exercise: Allocation and Deallocation of a Dynamic 2D Array

Use multi-level pointers to implement the allocation, population, and deallocation of a dynamic 2D array. Please implement the following three functions yourself:

```cpp
int** create_2d_array(int rows, int cols);
void fill_2d_array(int** arr, int rows, int cols);
void free_2d_array(int** arr, int rows);
```

Hint: When allocating, first allocate a pointer array (the dimension that `int**` points to), then `new` each row individually. When freeing, do the reverse order—free each row first, then free the pointer array itself.

## References

- [C declaration syntax - cppreference](https://en.cppreference.com/w/c/language/declarations)
- [cdecl: C declaration translator](https://cdecl.org/)
