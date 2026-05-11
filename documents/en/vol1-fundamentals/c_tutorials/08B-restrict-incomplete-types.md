# restrict, Incomplete Types, and Struct Pointers

In the previous chapter, we covered multi-level pointers and how to read declarations. Now we will look at three relatively independent but highly useful mechanisms: the ``restrict`` qualifier lets the compiler perform more aggressive optimizations, incomplete types and forward declarations allow us to design interfaces without exposing internal details, and the ``->`` operator is an everyday tool for working with struct pointers.

These three might seem unrelated, but they are all highly practical in C language engineering—and they all have modern counterparts in C++.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand what problem the restrict qualifier solves and its usage rules
> - [ ] Use incomplete types and forward declarations to reduce header file dependencies
> - [ ] Implement the opaque pointer pattern to hide implementation details
> - [ ] Use the ``->`` operator to manipulate struct pointers

## Environment Setup

We will run all of the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: ``-Wall -Wextra -std=c17``

## Step 1 — Understanding Why restrict Makes Code Faster

### Pointer Aliasing — The Compiler's Nightmare

Consider this function:

````c
void vector_add(int n, int* a, int* b)
{
    for (int i = 0; i < n; i++) {
        a[i] = a[i] + b[i];
    }
}
````

The compiler faces a problem here: ``a`` and ``b`` might point to the same memory. For example, when calling ``vector_add(10, arr, arr)``, after writing to ``a[i]``, the value of ``b[i]`` also changes. Therefore, the compiler cannot perform aggressive optimizations—it must re-read ``b[i]`` from memory after every write to ``a[i]``.

This is the "pointer aliasing" problem: the compiler cannot determine whether two pointers point to the same memory, so it must handle them conservatively.

### restrict — A Contract Between Programmer and Compiler

``restrict`` is a qualifier introduced in C99 that tells the compiler: "I guarantee that the memory accessed by this pointer will not be accessed through any other pointer."

````c
void vector_add(int n, int* restrict a, int* restrict b)
{
    for (int i = 0; i < n; i++) {
        a[i] = a[i] + b[i];
    }
}
````

With ``restrict`` added, the compiler knows that ``a`` and ``b`` do not overlap, so it can safely perform optimizations like vectorization (SIMD) and loop unrolling.

Let's look at a more intuitive example:

````c
int foo(int* a, int* b)
{
    *a = 5;
    *b = 6;
    return *a + *b;
    // 编译器不敢假设 *a 还是 5，因为 b 可能就是 a
    // 必须重新从内存读 *a
}

int rfoo(int* restrict a, int* restrict b)
{
    *a = 5;
    *b = 6;
    return *a + *b;
    // 编译器知道 a、b 不重叠，*a 一定是 5
    // 直接返回 11，不用重新读内存
}
````

Inside ``rfoo``, the compiler doesn't even need to re-read from memory—it already knows the value of ``*a``.

> ⚠️ **Pitfall Warning**
> ``restrict`` is a one-way promise from the programmer to the compiler; the compiler does not check this at runtime. If you pass overlapping pointers, the behavior is undefined—the optimized code can produce any result, and this kind of bug only surfaces under specific compiler flags, making it extremely painful to track down.

### memcpy vs memmove — A Classic Comparison

The standard library provides a classic example that illustrates the purpose of ``restrict``:

````c
void* memcpy(void* restrict dest, const void* restrict src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
````

``memcpy`` assumes non-overlapping memory and uses ``restrict``, making it faster. ``memmove`` allows overlapping memory and cannot use ``restrict``; it must perform additional checks and buffering internally, making it slightly slower. If you are certain the source and destination do not overlap, prefer ``memcpy``.

## Step 2 — Understanding Incomplete Types and Forward Declarations

### What Is an Incomplete Type

If the compiler knows a type exists but does not know its size or internal structure, that type is incomplete. The most common example:

````c
struct Foo;  // 前向声明：告诉编译器"Foo 是个结构体"，但不说里面有什么

struct Foo* p;    // 合法：指针大小固定，不需要知道 Foo 的完整定义
struct Foo  obj;  // 非法：编译器不知道 Foo 的大小，无法分配空间
````

There are very few things you can do with an incomplete type: declare a pointer to it, or use its pointer in a function declaration. To do anything more (define variables, access members, ``sizeof``), you must provide a complete definition.

### What Are Forward Declarations Good For

The most direct use of forward declarations is to reduce header file dependencies. Let's look at an example:

````c
// car.h
struct Engine;  // 前向声明，不需要 #include "engine.h"

struct Car {
    struct Engine* engine;  // 只需要指针，前向声明就够
    int speed;
};
````

If ``Car`` only contains a pointer to ``Engine``, we do not need ``#include "engine.h"``. This way, users of ``car.h`` are not forced to pull in all the dependencies of ``engine.h``, and compilation speed improves.

> ⚠️ **Pitfall Warning**
> Forward declarations can only be used to declare pointers or references. If you put ``struct Engine engine;`` directly in a header file (not a pointer), the compiler must know the complete definition of ``Engine`` to determine the size of ``Car``—in this case, a forward declaration will not work, and you must ``#include`` the full header.

## Step 3 — Hiding Implementation Details with Opaque Pointers

Incomplete types have a very important application pattern in C: the opaque pointer. The idea is that the header file only exposes the forward declaration and manipulation functions, without exposing the struct's internal details.

````c
// buffer.h — 公开头文件
typedef struct Buffer Buffer;  // 前向声明 + typedef

Buffer* buffer_create(int capacity);
void    buffer_destroy(Buffer* buf);
int     buffer_append(Buffer* buf, const char* data, int len);
int     buffer_length(const Buffer* buf);
````

Callers can only manipulate ``Buffer`` through functions and can never see the internal structure of ``struct Buffer``. The implementation provides the complete definition in the ``.c`` file:

````c
// buffer.c — 实现文件
#include "buffer.h"
#include <stdlib.h>
#include <string.h>

struct Buffer {
    char* data;
    int   capacity;
    int   length;
};

Buffer* buffer_create(int capacity)
{
    Buffer* buf = (Buffer*)malloc(sizeof(Buffer));
    buf->data = (char*)malloc(capacity);
    buf->capacity = capacity;
    buf->length = 0;
    return buf;
}

void buffer_destroy(Buffer* buf)
{
    if (buf) {
        free(buf->data);
        free(buf);
    }
}

int buffer_append(Buffer* buf, const char* data, int len)
{
    if (buf->length + len > buf->capacity) {
        return -1;  // 缓冲区不足
    }
    memcpy(buf->data + buf->length, data, len);
    buf->length += len;
    return 0;
}

int buffer_length(const Buffer* buf)
{
    return buf->length;
}
````

The benefit here is that you can modify the internal implementation of ``Buffer`` (for example, adding a growth strategy), and as long as the function signatures remain unchanged, callers do not need to recompile. The standard library's ``FILE`` is a classic example of this pattern—you never know what ``FILE`` looks like inside, and you only use ``fopen``/``fclose``/``fread``/``fwrite`` to operate on it.

## Step 4 — Using -> to Manipulate Struct Pointers

When passing structs between functions, we typically use pointers to avoid copy overhead. There are two ways to access members pointed to by a struct pointer:

````c
typedef struct {
    float x;
    float y;
} Point;

Point p = {3.0f, 4.0f};
Point* ptr = &p;

// 方式 1：先解引用，再用 . 访问成员
float x1 = (*ptr).x;   // 括号不能省，因为 . 的优先级高于 *

// 方式 2：用 -> 运算符（语法糖）
float x2 = ptr->x;     // 等价于 (*ptr).x
````

``->`` is syntactic sugar invented to save us typing. Just remember the rule: **use ``.`` for struct variables, and ``->`` for struct pointers**.

````c
typedef struct {
    Point center;
    float radius;
} Circle;

Circle c = {{0.0f, 0.0f}, 5.0f};
Circle* cp = &c;

cp->center.x = 1.0f;        // 修改圆心的 x
cp->radius = 10.0f;          // 修改半径

void move_circle(Circle* c, float dx, float dy)
{
    c->center.x += dx;
    c->center.y += dy;
}

move_circle(cp, 2.0f, 3.0f);
````

> ⚠️ **Pitfall Warning**
> Confusing ``.`` and ``->`` is one of the most common mistakes beginners make. ``cp->center.x`` is correct, but ``cp.center.x`` will not compile (``cp`` is a pointer, not a variable), and ``(*cp).center.x``, while equivalent, is easy to mess up because of the parentheses. Simply build the habit of using ``->``.

## C++ Connections

### PIMPL — The Modern Version of Opaque Pointers

PIMPL (Pointer to Implementation) is the direct descendant of the opaque pointer in C++. It hides a class's private implementation behind a pointer to an incomplete type, and the header file only needs a forward declaration:

````cpp
// widget.h — 公开头文件
class Widget {
public:
    Widget();
    ~Widget();
    void do_something();
private:
    struct Impl;          // 前向声明
    Impl* pimpl_;         // 不完整类型的指针
};

// widget.cpp — 实现文件
struct Widget::Impl {
    int internal_state = 0;
    void helper() { /* ... */ }
};

Widget::Widget() : pimpl_(new Impl{}) {}
Widget::~Widget() { delete pimpl_; }

void Widget::do_something() {
    pimpl_->internal_state++;
}
````

Modifying the internal structure of ``Impl`` does not require recompiling all files that include ``widget.h``, drastically reducing compilation time and improving ABI stability.

### Why C++ Never Formally Adopted restrict

The C++ standard has never introduced ``restrict``. C++ class semantics and references make pointer aliasing analysis much more complex—the compiler must consider ``this`` pointers, reference bindings, and object lifetimes, which are issues that C does not have. However, mainstream compilers provide extensions: GCC and Clang use ``__restrict``, and MSVC also uses ``__restrict``. So you can use it in C++, it is just not standard.

## Common Pitfalls

| Pitfall | Description | Solution |
|---------|-------------|----------|
| Passing overlapping pointers under restrict | Undefined behavior; the compiler will not check it | Ensure the memory pointed to by restrict pointers truly does not overlap |
| Accessing members directly after a forward declaration | ``struct Foo; Foo f; f.x = 1;`` will all fail | Forward declarations can only declare pointers; full usage requires a complete definition |
| Confusing ``.`` and ``->`` | Use ``->`` for pointers, ``.`` for variables | ``ptr->member`` is equivalent to ``(*ptr).member`` |
| Mixing up memcpy and memmove | Using memcpy when source and destination overlap is UB | Use memmove when there is a risk of overlap |

## Summary

In this chapter, we looked at three independent but practical mechanisms. ``restrict`` enables the compiler to perform more aggressive optimizations by eliminating pointer aliasing, but it is a "programmer's guarantee to the compiler"—violating it means undefined behavior. Incomplete types and forward declarations allow us to design interfaces without exposing internal details, and the opaque pointer pattern is a classic technique for information hiding in C. ``->`` is an everyday tool for manipulating struct pointers; just remember "use ``.`` for variables, ``->`` for pointers" and you are set.

## Exercises

### Exercise: Implement a Simple Opaque Pointer Module

Use the opaque pointer pattern to implement a simple Stack module. Requirements:

````c
// stack.h — 只暴露接口，不暴露内部结构
typedef struct Stack Stack;

Stack* stack_create(int capacity);
void   stack_destroy(Stack* s);
int    stack_push(Stack* s, int value);   // 成功返回 0，满栈返回 -1
int    stack_pop(Stack* s, int* out);     // 成功返回 0，空栈返回 -1
int    stack_size(const Stack* s);
````

Hint: Define the complete structure of ``struct Stack`` in the ``.c`` file (you can implement it using an array plus a top-of-stack index), and put only the forward declaration and function declarations in the ``.h`` file.

## References

- [restrict qualifier - cppreference](https://en.cppreference.com/w/c/language/restrict)
- [Incomplete types - cppreference](https://en.cppreference.com/w/c/language/type)
