---
chapter: 1
cpp_standard:
- 11
description: Simulate classes, encapsulation, inheritance, and polymorphism using
  structs and function pointers to understand the underlying implementation mechanisms
  of OOP.
difficulty: advanced
order: 104
platform: host
prerequisites:
- 指针进阶：多级指针、指针与 const
- 结构体、联合体与内存对齐
- 函数指针与回调机制
reading_time_minutes: 15
tags:
- host
- cpp-modern
- advanced
- 实战
- 基础
title: Implementing Object-Oriented Programming in C
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/04-oop-in-c.md
  source_hash: cdd226d730bc475970462efb2e3de9dc97776608ed00faad7c0e957cff0fb125
  translated_at: '2026-06-16T05:53:08.960397+00:00'
  engine: anthropic
  token_count: 3510
---
# Implementing Object-Oriented Programming in C

To be honest, I debated for a long time whether to write this topic. After all, it's 2026—who is still hand-cranking OOP in C? But then I thought about it—embedded development, the Linux kernel, GTK/GLib, the Lua source code—every one of these heavyweight C projects uses structs and function pointers to do object-oriented programming. More importantly, if you don't understand how OOP is pieced together at the C level, your understanding of virtual function tables (vtables), `vptr`, and dynamic binding in C++ will always be built on shaky ground—you might know the syntax, but you won't know what's happening under the hood.

In this article, we will manually implement encapsulation, inheritance, polymorphism, and interface abstraction in pure C, and finally build a working graphics framework. After writing this, looking back at C++ `class`, `virtual`, and `abstract class`, you will have that "aha" moment of clarity.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Simulate C++ classes using structs and function pointers
> - [ ] Implement encapsulation using opaque pointers
> - [ ] Implement single inheritance using struct nesting
> - [ ] Simulate runtime polymorphism using vtables
> - [ ] Implement interface abstraction using function pointer tables
> - [ ] Complete a graphics framework hands-on project featuring inheritance and polymorphism

## Environment Setup

We can use GCC or Clang to compile directly on the host machine; no third-party libraries are required. The code follows the C11 standard, as we will be using anonymous structs and designated initializers. If you are running on an embedded platform, these techniques are equally portable—structs and function pointers do not rely on any specific runtime features.

```text
平台：Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11
依赖：无
```

## Step 1 — Implementing Encapsulation with Opaque Pointers

The core idea of encapsulation is to hide the internal implementation and expose only the operational interface. While C++ uses `private` and `public`, the answer in C is the opaque pointer pattern.

### Dynamic String Buffer

We will create a dynamic string buffer where the caller can only manipulate it through functions, never seeing the internal structure. The header file only exposes the type name and operation functions:

```c
// strbuf.h — 公开头文件
typedef struct StrBuf StrBuf;

StrBuf*     strbuf_create(int capacity);
void        strbuf_destroy(StrBuf* sb);
int         strbuf_append(StrBuf* sb, const char* data, int len);
int         strbuf_length(const StrBuf* sb);
const char* strbuf_data(const StrBuf* sb);
```

The header file contains only a forward declaration `typedef struct StrBuf StrBuf`. The caller knows that `StrBuf` is a type, but has no idea what it looks like inside—they cannot directly access any fields and must use the functions we provide. Isn't this exactly like C++'s `private`?

The full definition is provided only in the implementation file:

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

The complete definition of `struct StrBuf` appears only in the `.c` file. If a caller attempts to write `sb->length`, the compiler will immediately report an error: "dereferencing pointer to incomplete type". In C, the `.h` file is equivalent to the `public` section in C++, while the `.c` file corresponds to `private` members and function implementations—the difference is that C relies on the compiler's incomplete type checking, whereas C++ relies on language-level access control keywords.

## Step 2 — Simulating Classes with Structs and Function Pointers

With encapsulation settled, we move on to a more fundamental problem: C lacks "methods". In C++, methods are functions bound to a class, invoked via `obj.method()`. C lacks this syntactic sugar, but we can simulate it using a convention: **store function pointers within the struct, with the first parameter always being the `self` pointer**.

### Counter "Object"

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

Structs contain both data members and function pointer members, where function pointers correspond to member functions in C++. However, there is a crucial difference: C function pointers do not automatically bind `this`, so we must manually pass `self`.

Method implementation and "constructor":

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

It becomes very OOP-like when we use it:

```c
Counter c;
counter_init(&c, 0, 100);

c.increment(&c);
c.increment(&c);
printf("value = %d\n", c.get_value(&c));  // value = 2
```

> ⚠️ **Warning**
> Storing the function pointer directly in each instance means that every object holds a copy of that pointer—on a 64-bit system, this `Counter` takes up 32 bytes just for the function pointer. If we create ten thousand objects, we end up with one hundred thousand copies of the exact same pointer. In the next section, we will use a vtable to optimize this issue.

## Step 3 — Implementing Inheritance via Nested Structs

C lacks language-level inheritance, but we can simulate it using **nested structs**—by placing the "base class" as a member at the first field of the "derived class." Why the first field? Because the C standard guarantees that the address of a struct is the same as the address of its first member. This allows us to safely perform type casts between base class pointers and derived class pointers.

### The Animal Family

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

Here is the critical point: since the first member of both `Dog` and `Cat` is `Animal base`, we have `&dog->base == (Animal*)dog`. We can safely cast a `Dog*` to an `Animal*`, and then call it uniformly through the base class pointer:

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

Please provide the Chinese Markdown content you would like me to translate. I am ready to apply the translation rules and terminology reference to generate the English documentation.

```text
[Buddy, age=3] Woof!
[Whiskers, age=2] Meow!
```

Although we invoke the method through an `Animal*` pointer, `Dog` and `Cat` produce different sounds. This is the prototype of polymorphism—the same interface, different behaviors.

> ⚠️ **Warning**
> The base class **must** be placed as the first member. If you place it in the middle or at the end, `&dog == (Animal*)&dog` will no longer hold true. The type conversion will yield an incorrect offset, leading to data corruption at best or a hard crash at worst.

## Step 4 — Implementing Polymorphism with a Virtual Table (vtable)

Previously, we stuffed function pointers directly into every object, which wasted a significant amount of memory. Now, let's implement proper polymorphism using a virtual table (vtable). This is the underlying mechanism C++ compilers use to implement virtual functions, and we will manually reproduce it. The core idea is: **all objects of the same type share a single table of function pointers, while each object only stores a pointer to this table**.

### Shape Base Class + vtable

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

`ShapeVtable` is the virtual function table—an array of function pointers. The `const ShapeVtable* vtable` inside `Shape` is the hidden vptr found inside every object with virtual functions in C++. Now we implement concrete shapes:

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

The implementation for `Rect` follows exactly the same logic: define the `Rect` struct, implement its methods, create `kRectVtable`, and write `rect_create`. We will not repeat the details here.

Now, let's verify that the polymorphism works as expected:

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

It looks like you haven't provided the Chinese Markdown content yet. Please paste the text you would like me to translate, and I will process it according to the rules and style guide provided.

(You seem to have just sent "输出：" which means "Output:" or "Print:". I am ready for the input!)

```text
Circle("Sun", r=5.00)
  area = 78.54
Rectangle("Box", w=3.00, h=4.00)
  area = 12.00
Circle("Moon", r=2.00)
  area = 12.57
```

We call the unified `shape_area()` and `shape_draw()` interfaces, and each call correctly dispatches to the specific implementation. This is runtime polymorphism, and the underlying mechanism is **exactly the same** as C++ virtual functions. The memory layout comparison is shown below:

![C Language Vtable Memory Layout](./04-oop-in-c-vtable.drawio)

## Step 5 — Implementing Interfaces with Function Pointer Tables

Inheritance solves code reuse, but sometimes we need a looser coupling relationship—interfaces. C has no concept of interfaces, but we can simulate them using **pure function pointer structs**. The difference from a vtable is that an interface contains no data members; it only defines behavioral contracts.

### Multiple Interface Implementation and the Offset Trap

A single type can implement multiple interfaces by nesting multiple interface structs. However, there is a major pitfall here:

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

> ⚠️ **Warning**
> In C++, the compiler automatically calculates offsets for multiple inheritance. However, when doing OOP manually in C, you must ensure pointer conversions are correct yourself. This is why many C projects (such as the Linux kernel) tend to stick to single inheritance combined with callback functions, rather than implementing multiple interface inheritance. If you must implement multiple interfaces, always use `&obj->interface` to obtain the pointer; do not cast directly.

## Step 6 — Practice: Building a Graphics Management Framework

Now, let's combine all the techniques we have learned—encapsulation, inheritance, polymorphism, and vtables—to write a graphics management framework. The core of the framework is a `ShapeManager`—encapsulated using an opaque pointer, so the external interface only receives a pointer without knowing how the shapes are stored internally.

### Shape Manager

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

We manage different types of graphic objects through a unified interface, and polymorphic dispatch automatically routes execution to the correct implementation—encapsulation, inheritance, and polymorphism are all in place.

## C++ Connection: What the Compiler is Actually Doing for You

When you write `class Shape { virtual double area() = 0; }` in C++, the compiler handles all the manual work we did above:

| Manual C OOP | What the C++ Compiler Does |
|---|---|
| Define `ShapeVtable` struct | Compiler automatically generates the vtable (in the `.rodata` section) |
| Assign `vtable = &kCircleVtable` in constructor | Constructor automatically sets the vptr |
| Manually write `shape_area()` for virtual dispatch | `s->area()` automatically looks up the table via vptr |
| Manually downcast `(Circle*)shape` | `dynamic_cast<Circle*>(shape)` for safe casting |
| Manually call constructor `counter_init(&c, 0, 100)` | `Counter c(0, 100)` automatic construction |
| Hide fields with opaque pointers | `private:` access control |
| Nest structs for inheritance | `class Derived : public Base` |

C++ OOP syntax is essentially syntactic sugar for C OOP idioms. The compiler automates all the tedious work of wiring up vtables, passing `this`, and performing type conversions. Once you understand this, you can make sense of seemingly strange C++ designs—like why the `sizeof` an empty class isn't zero (it has a vptr), why virtual destructors are important (otherwise the destructor won't reach the derived class's vtable), and why you can't call virtual functions in constructors (the vptr hasn't been set up yet).

### Why Virtual Destructors Matter

In our C implementation, `shape_destroy()` uses the vtable to find the correct `destroy` function to release resources. If `destroy` isn't properly overridden in the vtable, `free()` only releases memory sized for the base class, leaking the extra fields added by the derived class. Virtual destructors in C++ solve the exact same problem—when `delete base_ptr` is called, the vtable must be used to find the derived class's destructor to tear down the derived class before the base class. If the destructor isn't `virtual`, the compiler performs static binding and only calls the base class destructor—leaking the derived class's resources.

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

**Hint:** Use Heron's formula for the triangle area—first calculate the semi-perimeter `s = (a+b+c)/2`, then the area `A = sqrt(s*(s-a)*(s-b)*(s-c))`. Don't forget to fill in the correct function pointers in the vtable.

### Exercise 2: Shape Sorting

Add area sorting functionality to `ShapeManager`:

```c
/// @brief 按面积从小到大排序所有图形
void shape_manager_sort_by_area(ShapeManager* mgr);
```

> **Tip:** We can use the standard library's `qsort()`. However, the comparison function receives `const void*`, which we need to cast to `Shape**` and then dereference to obtain the `Shape*`. We can then compare sizes using `shape_area()`.

### Exercise 3: Opaque Pointer Counter

Refactor the `Counter` from step two into an opaque pointer version. The header file should only expose `typedef struct Counter Counter;` and the operation functions, while the implementation file hides the full definition. Please split the header and implementation files yourself, and provide a `counter_create()` function that returns a heap-allocated object.

## References

- [GLib Object System (GObject) - GNOME](https://docs.gtk.org/gobject/)
- [Linux Kernel Object Model (kobject)](https://docs.kernel.org/core-api/kobject.html)
- [C++ virtual functions - cppreference](https://en.cppreference.com/w/cpp/language/virtual)
