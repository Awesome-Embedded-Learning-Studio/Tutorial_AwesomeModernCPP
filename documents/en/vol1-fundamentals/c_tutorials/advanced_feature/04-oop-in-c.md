---
title: Implementing Object-Oriented Programming in C
description: Simulating classes, encapsulation, inheritance, and polymorphism using
  structs and function pointers, to understand the underlying implementation mechanisms
  of OOP
chapter: 1
order: 104
tags:
- host
- cpp-modern
- advanced
- 实战
- 基础
difficulty: advanced
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
prerequisites:
- 指针进阶：多级指针、指针与 const
- 结构体、联合体与内存对齐
- 函数指针与回调机制
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/04-oop-in-c.md
  source_hash: 8dd1037af6bf62c55a07e7df173f77ed185aafce6fe278c3a14060f27ede5a7e
  translated_at: '2026-04-20T03:38:37.070729+00:00'
  engine: anthropic
  token_count: 3591
---
# Implementing OOP in C

Honestly, I debated for a long time whether to write this topic. After all, it's 2026—who's still hand-rolling OOP in C? But then I thought about it—embedded development, the Linux kernel, GTK/GLib, the Lua source code—every one of these heavyweight C projects uses structs + function pointers for OOP. More importantly, if you don't understand how OOP is pieced together at the C level, your understanding of virtual function tables, vptrs, and dynamic binding in C++ will always be built on sand—you'll know the syntax, but not what's happening underneath.

In this chapter, we'll use pure C to hand-roll encapsulation, inheritance, polymorphism, and interface abstraction, and finally assemble a working graphics framework. Once you're done, looking back at C++'s `class`, `virtual`, and `abstract class` will give you a satisfying "aha" moment of clarity.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Simulate C++ classes using structs + function pointers
> - [ ] Implement encapsulation using opaque pointers
> - [ ] Implement single inheritance using struct embedding
> - [ ] Simulate runtime polymorphism using a vtable (virtual function table)
> - [ ] Implement interface abstraction using function pointer tables
> - [ ] Complete a hands-on graphics framework featuring inheritance and polymorphism

## Environment Setup

We can compile with GCC or Clang on the host machine without any third-party libraries. The code follows the C11 standard because we use anonymous structs and designated initializers. If you run this on an embedded platform, these patterns are equally portable—structs and function pointers don't depend on any runtime features.

```text
平台：Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11
依赖：无
```

## Step 1 — Encapsulation with Opaque Pointers

The core idea of encapsulation is to hide the internal implementation and only expose the operational interface. C++ uses `private` and `public`, and the answer in C is the opaque pointer pattern.

### Dynamic String Buffer

Let's build a dynamic string buffer where the caller can only manipulate it through functions and never sees the internal structure. The header file only exposes the type name and operation functions:

```c
// strbuf.h — 公开头文件
typedef struct StrBuf StrBuf;

StrBuf*     strbuf_create(int capacity);
void        strbuf_destroy(StrBuf* sb);
int         strbuf_append(StrBuf* sb, const char* data, int len);
int         strbuf_length(const StrBuf* sb);
const char* strbuf_data(const StrBuf* sb);
```

The header file contains only a forward declaration `typedef struct StrBuf StrBuf`. The caller knows that `StrBuf` is a type, but has no idea what it looks like inside—they can't directly access any fields and must go through the functions we provide. Isn't this exactly C++'s `private`?

The full definition is only provided in the implementation file:

```c
// strbuf.c — 私有实现
#include "strbuf.h"
#include <stdlib.h>
#include <string.h>

struct StrBuf {
    char* data;
    int   capacity;
    int   length;
};

StrBuf* strbuf_create(int capacity)
{
    StrBuf* sb = (StrBuf*)malloc(sizeof(StrBuf));
    if (!sb) return NULL;
    sb->data = (char*)malloc(capacity);
    if (!sb->data) {
        free(sb);
        return NULL;
    }
    sb->capacity = capacity;
    sb->length = 0;
    sb->data[0] = '\0';
    return sb;
}

void strbuf_destroy(StrBuf* sb)
{
    if (sb) {
        free(sb->data);
        free(sb);
    }
}

int strbuf_append(StrBuf* sb, const char* data, int len)
{
    if (sb->length + len >= sb->capacity) {
        return -1;  // 缓冲区不足
    }
    memcpy(sb->data + sb->length, data, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    return 0;
}

int strbuf_length(const StrBuf* sb) { return sb->length; }
const char* strbuf_data(const StrBuf* sb) { return sb->data; }
```

The full definition of `struct StrBuf` only appears in the `.c` file. If a caller tries to write `sb->length`, the compiler will immediately throw an error about "dereferencing pointer to incomplete type." In C, the `.h` file is equivalent to the `public` part in C++, and the `.c` file is equivalent to the `private` members and function implementations—the difference is that C relies on the compiler's incomplete type checking, while C++ relies on language-level access control keywords.

## Step 2 — Simulating Classes with Structs + Function Pointers

With encapsulation out of the way, let's tackle a more fundamental problem: C has no "methods." In C++, methods are functions bound to a class and can be called via `obj.method()`. C lacks this syntactic sugar, but we can simulate it with a convention: **store function pointers inside the struct, and always make the first parameter a `self` pointer**.

### A Counter "Object"

```c
typedef struct Counter {
    int value;
    int min;
    int max;

    // 「方法」——函数指针
    void (*increment)(struct Counter* self);
    void (*decrement)(struct Counter* self);
    int  (*get_value)(const struct Counter* self);
    void (*reset)(struct Counter* self);
} Counter;
```

The struct contains both data members and function pointer members, where the function pointers act as C++ member functions. But there's an important distinction—C function pointers don't automatically bind `this`, so we must manually pass `self`.

Method implementations and the "constructor":

```c
static void counter_increment(Counter* self)
{
    if (self->value < self->max) {
        self->value++;
    }
}

static int counter_get_value(const Counter* self)
{
    return self->value;
}

// 「构造函数」——初始化对象并绑定方法
void counter_init(Counter* self, int min, int max)
{
    self->value = min;
    self->min = min;
    self->max = max;
    self->increment = counter_increment;
    self->get_value = counter_get_value;
    // ...其他方法绑定
}
```

Using it feels very OOP:

```c
Counter c;
counter_init(&c, 0, 100);

c.increment(&c);
c.increment(&c);
printf("value = %d\n", c.get_value(&c));  // value = 2
```

> ⚠️ **Pitfall Warning**
> Stuffing function pointers directly into each instance means every object stores its own copy of the function pointers—on a 64-bit system, this `Counter` takes up 32 bytes just for the function pointers alone. If you create ten thousand objects, that's a hundred thousand identical pointers. In the next section, we'll use a vtable to optimize this.

## Step 3 — Implementing Inheritance with Struct Embedding

C has no language-level inheritance, but we can simulate it using **struct embedding**—placing the "base class" as a member in the first field of the "derived class." Why the first field? Because the C standard guarantees that a struct's address is the same as its first member's address, allowing us to safely cast between base and derived pointers.

### An Animal Family

```c
// 「基类」——所有动物共有的属性
typedef struct Animal {
    const char* name;
    int    age;
    void (*speak)(const struct Animal* self);
} Animal;

void animal_print_info(const Animal* self)
{
    printf("[%s, age=%d] ", self->name, self->age);
    if (self->speak) {
        self->speak(self);
    }
    printf("\n");
}

// 「派生类」——狗
typedef struct Dog {
    Animal base;          // 基类放第一个！
    const char* breed;
} Dog;

void dog_speak(const Animal* self) { printf("Woof!"); }

void dog_init(Dog* self, const char* name, int age, const char* breed)
{
    self->base.name = name;
    self->base.age = age;
    self->base.speak = dog_speak;
    self->breed = breed;
}

// 「派生类」——猫
typedef struct Cat {
    Animal base;
    int lives_remaining;
} Cat;

void cat_speak(const Animal* self) { printf("Meow!"); }

void cat_init(Cat* self, const char* name, int age, int lives)
{
    self->base.name = name;
    self->base.age = age;
    self->base.speak = cat_speak;
    self->lives_remaining = lives;
}
```

Here's the key part—because the first member of both `Dog` and `Cat` is `Animal base`, it follows that `&dog->base == (Animal*)dog`. We can safely cast a `Dog*` to a `Animal*` and call through the base class pointer uniformly:

```c
Dog dog;
dog_init(&dog, "Buddy", 3, "Golden Retriever");
Cat cat;
cat_init(&cat, "Whiskers", 2, 9);

Animal* animals[2] = { (Animal*)&dog, (Animal*)&cat };
for (int i = 0; i < 2; i++) {
    animals[i]->speak(animals[i]);
}
```

Output:

```text
[Buddy, age=3] Woof!
[Whiskers, age=2] Meow!
```

Even though we call through the `Animal*` pointer, `Dog` and `Cat` each make their own unique sound. This is the embryonic form of polymorphism—same interface, different behavior.

> ⚠️ **Pitfall Warning**
> The base class **must** be placed in the first field. If you put it in the middle or at the end, `&dog == (Animal*)&dog` no longer holds true. The type cast will yield an incorrect offset, resulting in garbled data at best and a hard crash at worst.

## Step 4 — Implementing Polymorphism with a Virtual Function Table (vtable)

Earlier, we stuffed function pointers directly into each object, which wasted quite a bit of memory. Now let's do proper polymorphism—using a virtual function table (vtable). This is the underlying mechanism C++ compilers use to implement virtual functions, and we're going to reproduce it manually. The core idea: **all objects of the same type share a single function pointer table, and each object only stores a pointer to that table**.

### A Shape Base Class + vtable

```c
typedef struct Shape Shape;

// 虚函数表——所有 Shape「类」共享的函数指针表
typedef struct ShapeVtable {
    double (*area)(const Shape* self);
    double (*perimeter)(const Shape* self);
    void   (*draw)(const Shape* self);
    void   (*destroy)(Shape* self);
} ShapeVtable;

// 基类结构体
typedef struct Shape {
    const ShapeVtable* vtable;  // 指向虚函数表的指针（就是 C++ 的 vptr）
    const char* name;
} Shape;

// 通用虚函数分派
double shape_area(const Shape* self)
{
    return self->vtable->area(self);
}
void shape_draw(const Shape* self)
{
    self->vtable->draw(self);
}
// ... shape_perimeter、shape_destroy 同理
```

`ShapeVtable` is the virtual function table—an array of function pointers. The `const ShapeVtable* vtable` inside `Shape` is the hidden vptr inside every object with virtual functions in C++. Now let's implement concrete shapes:

```c
// 圆形
typedef struct Circle {
    Shape base;     // 基类放第一个
    double radius;
} Circle;

static double circle_area(const Shape* self)
{
    const Circle* c = (const Circle*)self;  // 向下转型
    return 3.14159265358979 * c->radius * c->radius;
}

static void circle_draw(const Shape* self)
{
    const Circle* c = (const Circle*)self;
    printf("Circle(\"%s\", r=%.2f)\n", self->name, c->radius);
}

static void circle_destroy(Shape* self) { free(self); }

// 圆形的 vtable——const，全局唯一
static const ShapeVtable kCircleVtable = {
    .area      = circle_area,
    .perimeter = circle_perimeter,
    .draw      = circle_draw,
    .destroy   = circle_destroy
};

Circle* circle_create(const char* name, double radius)
{
    Circle* c = (Circle*)malloc(sizeof(Circle));
    c->base.vtable = &kCircleVtable;  // 绑定 vtable
    c->base.name = name;
    c->radius = radius;
    return c;
}
```

The rectangle implementation follows exactly the same pattern—define a `Rect` struct, implement the methods, create a `kRectVtable`, and write a `rect_create`. We'll skip the repetition here.

Let's verify that polymorphism works:

```c
Shape* shapes[3];
shapes[0] = (Shape*)circle_create("Sun", 5.0);
shapes[1] = (Shape*)rect_create("Box", 3.0, 4.0);
shapes[2] = (Shape*)circle_create("Moon", 2.0);

for (int i = 0; i < 3; i++) {
    shape_draw(shapes[i]);
    printf("  area = %.2f\n", shape_area(shapes[i]));
}
```

Output:

```text
Circle("Sun", r=5.00)
  area = 78.54
Rectangle("Box", w=3.00, h=4.00)
  area = 12.00
Circle("Moon", r=2.00)
  area = 12.57
```

Through the unified `shape_area()` and `shape_draw()` interfaces, each call routes to the correct concrete implementation—this is runtime polymorphism, and the underlying mechanism is **exactly the same** as C++ virtual functions. Here is a memory layout comparison:

```text
vtable 方案：每个对象只有 1 个 vptr（8 字节）
┌──────────────────┐
│ vtable ─────────────→ kCircleVtable（全局共享）
│ name             │     ┌──────────────────┐
│ radius           │     │ circle_area      │
└──────────────────┘     │ circle_perimeter │
                         │ circle_draw      │
                         └──────────────────┘
```

## Step 5 — Implementing Interfaces with Function Pointer Tables

Inheritance solves code reuse, but sometimes we need a more loosely coupled relationship—interfaces. C has no interface concept, but we can simulate one using a **pure function pointer struct**. The difference from a vtable is that an interface contains no data members; it only defines behavioral contracts.

### Multiple Interface Implementation and the Offset Trap

A type can implement multiple interfaces simultaneously by embedding multiple interface structs. But there's a major pitfall here:

```c
typedef struct Drawable {
    void (*draw)(const struct Drawable* self);
} Drawable;

typedef struct Serializable {
    char* (*to_string)(const struct Serializable* self);
} Serializable;

// 同时实现两个接口
typedef struct TextShape {
    Drawable    drawable;       // 第一个接口——可以直接 cast
    Serializable serializable;  // 第二个接口——必须用 & 取地址！
    char* text;
} TextShape;
```

```c
// 第一个接口——两种写法等价
Drawable* d1 = (Drawable*)ts;       // OK，因为是第一个成员
Drawable* d2 = &ts->drawable;       // 也 OK，更明确

// 第二个接口——直接 cast 是错的！
// Serializable* s = (Serializable*)ts;  // 危险！偏移不对
Serializable* s = &ts->serializable;    // 正确
```

> ⚠️ **Pitfall Warning**
> In C++, the compiler automatically calculates the offsets for multiple inheritance. But when hand-rolling OOP in C, you must manually guarantee the correctness of pointer conversions. This is why many C projects (like the Linux kernel) tend to stick to single inheritance + callbacks rather than multiple interface inheritance. If you absolutely must use multiple interfaces, make sure to use `&obj->interface` to obtain the pointer instead of casting directly.

## Step 6 — Hands-on: Assembling a Graphics Management Framework

Now let's combine all the techniques we've learned—encapsulation, inheritance, polymorphism, and vtables—to build a graphics management framework. The core of the framework is a `ShapeManager`—encapsulated with an opaque pointer. The outside world only gets a handle and has no idea how shapes are stored internally.

### The Shape Manager

```c
// shape_manager.h — 不透明指针封装
typedef struct ShapeManager ShapeManager;

ShapeManager* shape_manager_create(int max_shapes);
void          shape_manager_destroy(ShapeManager* mgr);
int           shape_manager_add(ShapeManager* mgr, Shape* shape);
void          shape_manager_draw_all(const ShapeManager* mgr);
double        shape_manager_total_area(const ShapeManager* mgr);
Shape*        shape_manager_find_by_name(const ShapeManager* mgr,
                                         const char* name);
```

```c
// shape_manager.c — 私有实现
struct ShapeManager {
    Shape** shapes;
    int     count;
    int     capacity;
};

ShapeManager* shape_manager_create(int max_shapes)
{
    ShapeManager* mgr = (ShapeManager*)malloc(sizeof(ShapeManager));
    if (!mgr) return NULL;
    mgr->shapes = (Shape**)calloc(max_shapes, sizeof(Shape*));
    if (!mgr->shapes) {
        free(mgr);
        return NULL;
    }
    mgr->count = 0;
    mgr->capacity = max_shapes;
    return mgr;
}

void shape_manager_destroy(ShapeManager* mgr)
{
    if (!mgr) return;
    for (int i = 0; i < mgr->count; i++) {
        shape_destroy(mgr->shapes[i]);
    }
    free(mgr->shapes);
    free(mgr);
}

int shape_manager_add(ShapeManager* mgr, Shape* shape)
{
    if (mgr->count >= mgr->capacity) return -1;
    mgr->shapes[mgr->count++] = shape;
    return mgr->count - 1;
}

void shape_manager_draw_all(const ShapeManager* mgr)
{
    printf("=== Drawing %d shapes ===\n", mgr->count);
    for (int i = 0; i < mgr->count; i++) {
        shape_draw(mgr->shapes[i]);
    }
}

double shape_manager_total_area(const ShapeManager* mgr)
{
    double total = 0.0;
    for (int i = 0; i < mgr->count; i++) {
        total += shape_area(mgr->shapes[i]);
    }
    return total;
}
```

### Verification

```c
int main(void)
{
    ShapeManager* mgr = shape_manager_create(10);

    shape_manager_add(mgr, (Shape*)circle_create("Sun", 5.0));
    shape_manager_add(mgr, (Shape*)rect_create("Box", 3.0, 4.0));
    shape_manager_add(mgr, (Shape*)circle_create("Moon", 2.0));
    shape_manager_add(mgr, (Shape*)rect_create("Frame", 10.0, 6.0));

    shape_manager_draw_all(mgr);
    printf("Total area: %.2f\n", shape_manager_total_area(mgr));

    Shape* found = shape_manager_find_by_name(mgr, "Box");
    if (found) {
        printf("Found: ");
        shape_draw(found);
    }

    shape_manager_destroy(mgr);
    return 0;
}
```

```text
=== Drawing 4 shapes ===
Circle("Sun", r=5.00) -> area=78.54
Rectangle("Box", w=3.00, h=4.00) -> area=12.00
Circle("Moon", r=2.00) -> area=12.57
Rectangle("Frame", w=10.00, h=6.00) -> area=60.00
Total area: 163.10
Found: Rectangle("Box", w=3.00, h=4.00) -> area=12.00
```

We manage different types of shape objects through a unified interface, and polymorphic dispatch automatically routes to the correct implementation—encapsulation, inheritance, and polymorphism are all in place.

## Bridging to C++: What the Compiler Is Actually Doing for You

When you write `class Shape { virtual double area() = 0; }` in C++, the compiler does everything we manually did above:

| What You Manually Do in C OOP | What the C++ Compiler Does for You |
|---|---|
| Define a `ShapeVtable` struct | The compiler auto-generates a vtable (in the `.rodata` section) |
| Assign `vtable = &kCircleVtable` in the constructor | The constructor automatically sets the vptr |
| Manually write `shape_area()` for virtual dispatch | `s->area()` automatically looks up the table via the vptr |
| Manually downcast with `(Circle*)shape` | `dynamic_cast<Circle*>(shape)` for safe casting |
| Manually call the constructor with `counter_init(&c, 0, 100)` | `Counter c(0, 100)` for automatic construction |
| Hide fields with opaque pointers | `private:` access control |
| Use struct embedding for inheritance | `class Derived : public Base` |

C++'s OOP syntax is essentially syntactic sugar over C OOP idioms. The compiler automates all the tedious work of binding vtables, passing `this`, and performing type conversions. Understanding this sheds light on some seemingly quirky C++ design choices—like why the `sizeof` of an empty class isn't 0 (there's a vptr), why virtual destructors are important (otherwise destruction won't route to the derived class's vtable), and why you can't call virtual functions from a constructor (the vptr hasn't been set up yet).

### Why Virtual Destructors Matter

In our C implementation, `shape_destroy()` finds the correct `destroy` function through the vtable to release resources. If `destroy` in the vtable isn't properly overridden, `free()` only frees memory for the base class size, and the extra fields in the derived class leak. The C++ virtual destructor solves the exact same problem—when `delete base_ptr` is called, it must find the derived class's destructor through the vtable, destroying the derived class first and then the base class. If the destructor isn't `virtual`, the compiler performs static binding and only calls the base class destructor—the derived class's resources leak.

## Exercises

### Exercise 1: Triangle Extension

Add a `Triangle` type to the graphics framework (represented by three side lengths):

```c
typedef struct Triangle {
    Shape  base;
    double a, b, c;  // 三边长度
} Triangle;

Triangle* triangle_create(const char* name, int id,
                           double a, double b, double c);
```

Hint: Use Heron's formula for the triangle area—first calculate the semi-perimeter `s = (a+b+c)/2`, then the area is `A = sqrt(s*(s-a)*(s-b)*(s-c))`. Don't forget to fill in the correct function pointers in the vtable.

### Exercise 2: Shape Sorting

Add an area-based sorting feature to `ShapeManager`:

```c
/// @brief 按面积从小到大排序所有图形
void shape_manager_sort_by_area(ShapeManager* mgr);
```

Hint: You can use the standard library's `qsort()`, but the comparison function receives `const void*`. You'll need to cast it to `Shape**`, dereference to get the `Shape*`, and then compare sizes via `shape_area()`.

### Exercise 3: Opaque Pointer Version of the Counter

Convert the `Counter` from Step 2 into an opaque pointer version—the header file should only expose `typedef struct Counter Counter;` and operation functions, while the implementation file hides the full definition. Please split the header and implementation files yourself, and provide a `counter_create()` that returns a heap-allocated object.

## References

- [GLib Object System (GObject) - GNOME](https://docs.gtk.org/gobject/)
- [Linux Kernel Object Model (kobject)](https://docs.kernel.org/core-api/kobject.html)
- [C++ Virtual Functions - cppreference](https://en.cppreference.com/w/cpp/language/virtual)
