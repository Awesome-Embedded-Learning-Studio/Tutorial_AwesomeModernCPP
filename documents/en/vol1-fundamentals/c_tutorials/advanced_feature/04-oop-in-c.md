---
chapter: 1
cpp_standard:
- 11
description: Simulating classes, encapsulation, inheritance, and polymorphism with structs and function pointers to understand the low-level machinery behind OOP
difficulty: advanced
order: 104
platform: host
prerequisites:
- Advanced Pointers: Multilevel Pointers, Pointers and const
- Structures, Unions, and Memory Alignment
- Function Pointers and the Callback Pattern
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
  source_hash: 177ae62d939c91967e1f0246ea4b57a7343fea2b0c9cd8e503353521c677b12f
  translated_at: '2026-09-25T13:54:33+00:00'
  engine: anthropic
  token_count: 3700
---
# Implementing Object-Oriented Programming in C

Honestly, we went back and forth for a long time over whether to write this article. It's 2026 — who still hand-rolls OOP in C? But think about it: embedded development, the Linux kernel, GTK/GLib, the Lua source code — which of these heavyweight C projects isn't doing object orientation with struct + function pointers? And here is the more important part: if you have never seen how OOP is assembled at the C level, then when you learn C++, your understanding of the vtable, vptr, and dynamic binding forever remains a castle in the air — you know how to use the syntax, but not what is happening underneath.

In this article we hand-roll encapsulation, inheritance, polymorphism, and interface abstraction in pure C, and finish by assembling a shape framework that actually runs. Once you have written it, looking back at C++'s `class`, `virtual`, and `abstract class` brings that "so that's what it was" moment of clarity.

All we need is GCC or Clang on the host — no third-party libraries required. The code follows C11, because it uses anonymous structs and designated initializers. If you run it on an embedded platform, these idioms are just as portable — structs and function pointers depend on no runtime features.

```text
Platform: Linux / macOS / Windows (MSVC/MinGW)
Compiler: GCC >= 9 or Clang >= 12
Standard: -std=c11
Dependencies: none
```

## Step 1 — Encapsulation with Opaque Pointers

The core idea of encapsulation is to hide the internal implementation and expose only the operating interface. C++ uses `private` and `public`; C's answer is the opaque pointer pattern.

### A Dynamic String Buffer

We will build a dynamic string buffer that callers can manipulate only through functions and whose internal structure they never see. The header file exposes nothing but the type name and the operation functions:

```c
// strbuf.h — public header
typedef struct StrBuf StrBuf;

StrBuf*     strbuf_create(int capacity);
void        strbuf_destroy(StrBuf* sb);
int         strbuf_append(StrBuf* sb, const char* data, int len);
int         strbuf_length(const StrBuf* sb);
const char* strbuf_data(const StrBuf* sb);
```

The header contains just one forward declaration, `typedef struct StrBuf StrBuf`. Callers know `StrBuf` is a type, but have no idea what it looks like inside — no field can be accessed directly, and everything must go through the functions we provide. Isn't that exactly C++'s `private`?

The complete definition appears only in the implementation file:

```c
// strbuf.c — private implementation
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
        return -1;  // insufficient capacity
    }
    memcpy(sb->data + sb->length, data, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    return 0;
}

int strbuf_length(const StrBuf* sb) { return sb->length; }
const char* strbuf_data(const StrBuf* sb) { return sb->data; }
```

The full definition of `struct StrBuf` exists only in the `.c` file. If a caller tries to write `sb->length`, the compiler rejects it on the spot with "dereferencing pointer to incomplete type". The `.h` file plays the role of C++'s `public` part, and the `.c` file the role of the `private` members and function implementations — the difference being that C relies on the compiler's incomplete-type checking, while C++ relies on language-level access-control keywords.

## Step 2 — Simulating Classes with Struct + Function Pointers

Encapsulation, done. Next comes a more fundamental problem: C has no "methods". In C++, a method is a function bound to a class, callable through `obj.method()`. C lacks that syntactic sugar, but we can simulate it with a convention: **store function pointers inside the struct, with the first parameter always the `self` pointer**.

### The Counter "Object"

```c
typedef struct Counter {
    int value;
    int min;
    int max;

    // "methods" — function pointers
    void (*increment)(struct Counter* self);
    void (*decrement)(struct Counter* self);
    int  (*get_value)(const struct Counter* self);
    void (*reset)(struct Counter* self);
} Counter;
```

The struct now mixes data members with function pointer members; the function pointers play the role of C++ member functions. But there is one important difference — a C function pointer does not bind `this` automatically, so `self` has to be passed by hand.

The method implementations and the "constructor":

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

// "constructor" — initialize the object and bind the methods
void counter_init(Counter* self, int min, int max)
{
    self->value = min;
    self->min = min;
    self->max = max;
    self->increment = counter_increment;
    self->get_value = counter_get_value;
    // ... bind the remaining methods
}
```

Using it already feels quite OOP:

```c
Counter c;
counter_init(&c, 0, 100);

c.increment(&c);
c.increment(&c);
printf("value = %d\n", c.get_value(&c));  // value = 2
```

Stuffing function pointers directly into every instance means every object stores its own copy of them — on a 64-bit system, the function pointers alone make this `Counter` 32 bytes. Create ten thousand objects and you have a hundred thousand identical pointers. The next section optimizes this away with a vtable.

## Step 3 — Inheritance via Struct Nesting

C has no inheritance at the syntax level, but we can simulate it with **struct nesting** — put the "base class" as the first field of the "derived class". Why the first? Because the C standard guarantees that a struct's address equals the address of its first member, which lets us convert safely between base-class pointers and derived-class pointers.

### The Animal Family

```c
// "base class" — attributes shared by all animals
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

// "derived class" — dog
typedef struct Dog {
    Animal base;          // base class goes first!
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

// "derived class" — cat
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

Here comes the crucial part — because the first member of both `Dog` and `Cat` is `Animal base`, we have `&dog->base == (Animal*)dog`. We can safely convert a `Dog*` into an `Animal*` and then call uniformly through the base-class pointer:

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

Even though every call went through an `Animal*` pointer, `Dog` and `Cat` each emitted a different cry. This is polymorphism in embryo — one interface, different behaviors.

The base class **must** sit in the first field. Put it in the middle or at the end, and `&dog == (Animal*)&dog` no longer holds; the cast then applies a wrong offset, and at best the data gets scrambled, at worst the program crashes outright.

## Step 4 — Polymorphism with a Virtual Function Table (vtable)

Storing function pointers directly inside every object, as we did before, wastes quite a bit of memory. Now for the real thing — polymorphism through a virtual function table (vtable). This is the low-level mechanism C++ compilers use to implement virtual functions, and we are going to reproduce it by hand. The core idea: **all objects of the same type share one function pointer table, and each object stores only a single pointer to that table**.

### A Shape Base Class + vtable

```c
typedef struct Shape Shape;

// vtable — the function pointer table shared by all Shape "class" objects
typedef struct ShapeVtable {
    double (*area)(const Shape* self);
    double (*perimeter)(const Shape* self);
    void   (*draw)(const Shape* self);
    void   (*destroy)(Shape* self);
} ShapeVtable;

// base class struct
typedef struct Shape {
    const ShapeVtable* vtable;  // pointer to the vtable (this is C++'s vptr)
    const char* name;
} Shape;

// generic virtual function dispatch
double shape_area(const Shape* self)
{
    return self->vtable->area(self);
}
void shape_draw(const Shape* self)
{
    self->vtable->draw(self);
}
// ... shape_perimeter and shape_destroy follow the same pattern
```

`ShapeVtable` is the virtual function table — an array of function pointers. The `const ShapeVtable* vtable` inside `Shape` is precisely the vptr that C++ hides inside every object with virtual functions. Now let's implement a concrete shape:

```c
// circle
typedef struct Circle {
    Shape base;     // base class first
    double radius;
} Circle;

static double circle_area(const Shape* self)
{
    const Circle* c = (const Circle*)self;  // downcast
    return 3.14159265358979 * c->radius * c->radius;
}

static void circle_draw(const Shape* self)
{
    const Circle* c = (const Circle*)self;
    printf("Circle(\"%s\", r=%.2f)\n", self->name, c->radius);
}

static void circle_destroy(Shape* self) { free(self); }

// the circle vtable — const, globally unique
static const ShapeVtable kCircleVtable = {
    .area      = circle_area,
    .perimeter = circle_perimeter,
    .draw      = circle_draw,
    .destroy   = circle_destroy
};

Circle* circle_create(const char* name, double radius)
{
    Circle* c = (Circle*)malloc(sizeof(Circle));
    c->base.vtable = &kCircleVtable;  // bind the vtable
    c->base.name = name;
    c->radius = radius;
    return c;
}
```

The rectangle works exactly the same way — define a `Rect` struct, implement its methods, create a `kRectVtable`, write a `rect_create`. We won't repeat it here.

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

Called through the uniform `shape_area()` and `shape_draw()` interfaces, every dispatch landed on the correct concrete implementation — this is runtime polymorphism, **exactly the same** in its underlying mechanism as C++ virtual functions. The memory layout looks like this:

![Memory layout of a C vtable](./04-oop-in-c-vtable.drawio)

## Step 5 — Interfaces via Function Pointer Tables

Inheritance solves code reuse, but sometimes we need a looser coupling — interfaces. C has no concept of an interface, but we can simulate one with a **pure function-pointer struct**. The difference from a vtable: an interface contains no data members; it defines only a behavioral contract.

### Implementing Multiple Interfaces and the Offset Trap

One type can implement several interfaces at once — by nesting several interface structs. But there is a big trap lurking here:

```c
typedef struct Drawable {
    void (*draw)(const struct Drawable* self);
} Drawable;

typedef struct Serializable {
    char* (*to_string)(const struct Serializable* self);
} Serializable;

// implement both interfaces at once
typedef struct TextShape {
    Drawable    drawable;       // first interface — a direct cast works
    Serializable serializable;  // second interface — you must take its address with &!
    char* text;
} TextShape;
```

```c
// first interface — both spellings are equivalent
Drawable* d1 = (Drawable*)ts;       // OK, because it is the first member
Drawable* d2 = &ts->drawable;       // also OK, and more explicit

// second interface — casting directly is wrong!
// Serializable* s = (Serializable*)ts;  // dangerous! wrong offset
Serializable* s = &ts->serializable;    // correct
```

In C++, the compiler computes the offsets of multiple inheritance automatically; in hand-rolled C OOP, you must guarantee correct pointer conversions yourself. That is why many C projects (the Linux kernel, for example) prefer single inheritance plus callback functions over multiple interface inheritance. If you really must implement multiple interfaces, always obtain the pointer through `&obj->interface`; never cast directly.

## Step 6 — Practice: Assembling a Shape Management Framework

Now we combine everything learned so far — encapsulation, inheritance, polymorphism, vtable — into a shape management framework. At its core is a `ShapeManager`: wrapped behind an opaque pointer, the outside world holds nothing but a handle and has no idea how shapes are stored inside.

### The Shape Manager

```c
// shape_manager.h — opaque pointer encapsulation
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
// shape_manager.c — private implementation
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

We managed shape objects of different types through one uniform interface, and polymorphic dispatch automatically reached the correct implementation — encapsulation, inheritance, and polymorphism, all in place.

## Bridging to C++: What the Compiler Actually Does for You

When you write `class Shape { virtual double area() = 0; }` in C++, the compiler does for you everything we did by hand above:

| What you do by hand in C | What the C++ compiler does for you |
|---|---|
| Define the `ShapeVtable` struct | The compiler generates the vtable automatically (in the `.rodata` section) |
| Assign `vtable = &kCircleVtable` in the constructor | The constructor sets the vptr automatically |
| Write `shape_area()` by hand for virtual dispatch | `s->area()` consults the table through the vptr automatically |
| Downcast manually with `(Circle*)shape` | `dynamic_cast<Circle*>(shape)` for a safe cast |
| Call the constructor manually with `counter_init(&c, 0, 100)` | `Counter c(0, 100)` constructs automatically |
| Hide fields behind opaque pointers | `private:` access control |
| Inherit through struct nesting | `class Derived : public Base` |

C++'s OOP syntax is essentially syntactic sugar over C OOP idioms. The compiler automates all the fiddly work — binding the vtable, passing `this`, performing the conversions. Once you understand this, several seemingly odd C++ design choices start to make sense — why the `sizeof` of an empty class is not 0 (it carries a vptr), why virtual destructors matter (otherwise destruction never reaches the derived class's vtable), and why you can't call virtual functions from a constructor (the vptr isn't set up yet).

### Why Virtual Destructors Matter

In our C implementation, `shape_destroy()` locates the correct `destroy` function through the vtable to release resources. If `destroy` is not properly overridden in the vtable, `free()` releases only base-class-sized memory, and the derived class's extra fields leak. The C++ virtual destructor solves exactly the same problem — when you `delete base_ptr`, the vtable must be consulted to find the derived class's destructor, destroying the derived class first and the base class second. If the destructor is not `virtual`, the compiler binds statically and calls only the base-class destructor — and the derived class's resources leak.

## Exercises

### Exercise 1: Add a Triangle

**Difficulty: beginner** · Add one more shape following the vtable pattern

Add a `Triangle` type (represented by its three side lengths) to the shape framework:

```c
typedef struct Triangle {
    Shape  base;
    double a, b, c;  // side lengths
} Triangle;

Triangle* triangle_create(const char* name, int id,
                           double a, double b, double c);
```

Hint: use Heron's formula for the triangle's area — compute the semi-perimeter first, `s = (a+b+c)/2`, then the area, `A = sqrt(s*(s-a)*(s-b)*(s-c))`. Don't forget to fill in the correct function pointers in the vtable.

### Exercise 2: Sorting the Shapes

**Difficulty: intermediate** · qsort plus a function-pointer comparator

Add area-based sorting to the `ShapeManager`:

```c
/// @brief Sort all shapes by area in ascending order
void shape_manager_sort_by_area(ShapeManager* mgr);
```

Hint: you can use the standard library's `qsort()`, but its comparison function receives `const void*` — cast it to `Shape**`, dereference to get the `Shape*`, then compare through `shape_area()`.

### Exercise 3: An Opaque-Pointer Counter

**Difficulty: intermediate** · Redo the Counter with opaque pointers

Rework the Step 2 `Counter` into an opaque-pointer version — the header file exposes only `typedef struct Counter Counter;` plus the operation functions, while the implementation file hides the full definition. Split the header and implementation files yourself, and provide a `counter_create()` that returns a heap-allocated object.

## References

- [GLib Object System (GObject) - GNOME](https://docs.gtk.org/gobject/)
- [Linux Kernel Object Model (kobject)](https://docs.kernel.org/core-api/kobject.html)
- [C++ virtual functions - cppreference](https://en.cppreference.com/w/cpp/language/virtual)
