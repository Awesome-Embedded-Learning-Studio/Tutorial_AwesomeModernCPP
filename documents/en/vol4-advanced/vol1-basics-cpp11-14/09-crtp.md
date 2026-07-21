---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: CRTP has a derived class pass itself as a template argument to its base,
  achieving compile-time static polymorphism that avoids the vtable and runtime dispatch
  of virtual functions. This piece covers its structure, the assembly proof of zero
  overhead, typical uses like mixins, its pitfalls, and the C++23 deducing-this alternative.
difficulty: intermediate
order: 9
platform: host
prerequisites:
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
- 'Name Lookup and ADL: How Two-Phase Lookup Works'
- 'Alias Templates and using Declarations: Short Names for Types'
reading_time_minutes: 9
related:
- 'Project: fixed_vector<T, N>'
- 'Template Friends and Barton-Nackman: The Hidden Friends Trick'
tags:
- host
- cpp-modern
- intermediate
- 模板
- CRTP
- 泛型
- 零开销抽象
title: 'CRTP: Static Polymorphism with the Curiously Recurring Template Pattern'
---
# CRTP: Static Polymorphism with the Curiously Recurring Template Pattern

CRTP, the Curiously Recurring Template Pattern. Its structure looks odd at first: a derived class passes itself as a template argument to its base. `struct Derived : Base<Derived>`. This "self-reference" trick achieves compile-time **static polymorphism**. When you do not need to decide the concrete type at runtime, it fully avoids the vtable and dispatch cost of virtual functions. Expression templates in Eigen, and the mixin mechanisms in many high-performance libraries, are built on CRTP. This piece covers its structure, the assembly proof of zero overhead, several typical uses, and the pitfalls you cannot avoid.

## What CRTP Looks Like: A Derived Class Passes Itself to Its Base

Start with a minimal example. The base class `Shape` is a template, and its template parameter is "the derived class itself."

```cpp
#include <iostream>

template <typename Derived>
struct Shape {
    const char* name() {
        return static_cast<Derived*>(this)->name_impl();   // cast this to the derived pointer
    }
    double area() {
        return static_cast<Derived*>(this)->area_impl();
    }
};

struct Circle : Shape<Circle> {        // Circle passes itself, Circle, to Shape
    double r;
    explicit Circle(double r_) : r(r_) {}
    const char* name_impl() { return "Circle"; }
    double area_impl() { return 3.14159 * r * r; }
};

struct Square : Shape<Square> {
    double side;
    explicit Square(double s) : side(s) {}
    const char* name_impl() { return "Square"; }
    double area_impl() { return side * side; }
};
```

The key line is `struct Circle : Shape<Circle>`. `Circle` inherits `Shape`, but passes `Circle` itself as the template argument. So in the base `Shape<Derived>`, `Derived` is `Circle`, and the base's `area()` calls `static_cast<Derived*>(this)->area_impl()`, which actually calls `Circle::area_impl`.

Run it.

```bash
$ g++ -Wall -Wextra -std=c++20 crtp_basic.cpp -o crtp_basic && ./crtp_basic
Circle area = 12.5664
Square area = 9
```

`Circle` goes through `Circle::area_impl`, `Square` through `Square::area_impl`, each computing its own. The effect looks the same as virtual-function polymorphism, but the mechanism is entirely different.

## Static Polymorphism: The Concrete Type Is Known at Compile Time

Virtual-function polymorphism is **runtime**. At `base->area()`, the concrete type `base` points to is only known at runtime, so the compiler can only generate "look up the vtable, indirect call" code.

CRTP polymorphism is **compile-time**. When `Shape<Circle>::area()` is instantiated, `Derived` is already pinned as `Circle`. `static_cast<Derived*>(this)->area_impl()` is `static_cast<Circle*>(this)->area_impl()`, and the compiler knows clearly it is calling `Circle::area_impl`, so it can call directly or even inline. No vtable, no runtime dispatch.

This is the core value of CRTP: **when the concrete type is known at compile time, replace virtual functions with static polymorphism for zero overhead.**

## The Assembly Proof of Zero Overhead

Talk is cheap, look at the assembly. Write a CRTP version and a virtual-function version, both with the same behavior (return 42), compile with `-O2`, and compare the disassembly of the `use` function.

```cpp
// crtp_asm.cpp -- CRTP version
template <typename D>
struct Base {
    int compute() { return static_cast<D*>(this)->compute_impl(); }
};
struct Concrete : Base<Concrete> {
    int compute_impl() { return 42; }
};
int use_crtp() { Concrete c; return c.compute(); }
```

The complete disassembly of the CRTP `use_crtp` is just two instructions.

```text
0000000000000000 <_Z8use_crtpv>:
   0:   b8 2a 00 00 00   mov    $0x2a,%eax    ; 0x2a = 42, put the result straight into the return value
   5:   c3               ret
```

`compute()` and `compute_impl()` are both inlined, and the compiler computes the result, 42, directly, skipping even the function call. `use_crtp` is simply "put 42 into eax, return."

Now the virtual-function version (called through a reference to force virtual dispatch).

```cpp
// vtable_asm.cpp -- virtual function version
struct Base {
    virtual int compute() = 0;
    virtual ~Base() = default;
};
struct Concrete : Base {
    int compute() override { return 42; }
};
int use_virtual(Base& b) { return b.compute(); }   // through a reference, the compiler cannot devirtualize
```

The disassembly of `use_virtual` is much longer.

```text
0000000000000000 <_Z11use_virtualR4Base>:
   0:   48 8b 07         mov    (%rdi),%rax        ; load the vtable pointer from the object
   3:   48 8d 15 ...     lea    ...(%rip),%rdx     ; the vtable address of Concrete
   a:   48 8b 00         mov    (%rax),%rax        ; load the function pointer of compute from the vtable
   d:   48 39 d0         cmp    %rdx,%rax          ; speculative devirtualization: is it Concrete?
  10:   75 0e            jne    20                 ; only indirect-call if not
  12:   b8 2a 00 00 00   mov    $0x2a,%eax         ; it is Concrete, return 42 directly
  17:   c3               ret
```

The virtual version has two dereferences of the vtable pointer (`mov (%rdi)` to get the vtable, `mov (%rax)` to get the function pointer), plus a comparison and a conditional jump (GCC's "speculative devirtualization": first guess whether `b` is `Concrete`; if the guess hits, return directly; if it misses, do the real indirect call). Even at `-O2`, this vtable access and devirtualization check still cost something.

The contrast is plain. The CRTP version is two instructions with zero memory access. The virtual version is seven instructions with two memory accesses (the vtable). This is the most direct evidence of "zero-overhead static polymorphism." In a numerical library like Eigen, expression evaluation is flattened entirely into compile time via CRTP, and at runtime it is just a sequence of direct arithmetic instructions with no polymorphic dispatch. That is the root reason it can be as fast as a hand-written loop.

## Typical Uses of CRTP

CRTP is not only for static polymorphism. It has several common uses.

**Static polymorphism** (the focus of this piece). The base defines the interface skeleton, the derived class provides the concrete implementation, bound at compile time with no virtual-function cost. It fits "fixed interface, varied implementations, performance-sensitive" scenarios, like operations in numerical libraries or container iterators.

**Mixin.** The base injects a piece of generic functionality into the derived class, which only needs to provide the underlying data. For example, auto-generate a full set of comparison operators for any type that supports `<`.

```cpp
template <typename Derived>
struct Comparable {
    friend bool operator>(const Derived& a, const Derived& b)  { return b < a; }
    friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
    friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
    friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
};

struct Point : Comparable<Point> {
    int x, y;
    Point(int x_, int y_) : x(x_), y(y_) {}
    friend bool operator<(const Point& a, const Point& b) { return a.x < b.x; }
    friend bool operator==(const Point& a, const Point& b) { return a.x == b.x; }
};
// Point now has > <= >= != automatically, only < and == had to be implemented
```

Run it to confirm the mixin-injected operators actually work.

```bash
$ g++ -Wall -Wextra -std=c++20 comparable.cpp -o comparable && ./comparable
p1 < p2:  true
p1 > p2:  false
p1 <= p2: true
p1 >= p2: false
p1 != p2: true
```

`Point` only implements `<` and `==`; the other four comparison operators are all auto-filled by `Comparable<Point>`.

As long as `Point` implements `<` and `==`, mixing in `Comparable<Point>` fills in all the other comparison operators automatically. This pairs with the Barton-Nackman trick from the previous piece: CRTP provides the structure, friend injection provides the operators.

**Compile-time interface injection / policy injection.** The base can require the derived class to provide certain typedefs or constants, checked at compile time, implementing a "static interface." For example, the base can write `using value_type = typename Derived::value_type;`, and if the derived class does not expose `value_type`, it fails to compile. This kind of "concept-like" compile-time constraint was commonly faked with CRTP before concepts existed.

**Expression templates.** In Eigen, `a + b * c` does not produce temporary matrices. Instead it produces an "expression type" that records the operation, and evaluates it all at once on assignment, avoiding intermediate temporaries. This mechanism is built entirely on CRTP and is its most stunning application. Part three of this volume takes it apart.

## Pitfalls of CRTP

CRTP is powerful, but it has pitfalls to avoid.

**The derived class is incomplete during base construction.** When the base constructor runs, the derived part has not been constructed yet. Calling `static_cast<Derived*>(this)` inside the base constructor to access derived members is undefined behavior (the object is not fully formed). The same goes for the base destructor. CRTP's cross-layer calls are only safe when the object is complete at the moment of the call, not during construction or destruction.

**The safety assumption of `static_cast`.** `static_cast<Derived*>(this)` assumes `this` actually points to a `Derived` object. You guarantee this when you write `struct Derived : Base<Derived>`. The good news is the type system actually catches part of this for you: if someone writes `struct Wrong : Base<Other>` (where `Wrong` and `Other` are unrelated), the `static_cast<Other*>(this)` inside the base usually fails to compile, because `Wrong*` and `Other*` are unrelated types and `static_cast` refuses the conversion. So CRTP is more type-safe than it looks, as long as you honestly pass the derived class itself to the base.

**CRTP does not interoperate with virtual functions.** CRTP's `area()` is not virtual, so you cannot stuff `Circle*` and `Square*` into the same `Shape*` array and call `area()` uniformly. Scenarios that need a runtime heterogeneous collection still require virtual functions. CRTP fits "the concrete type is known at compile time," and that is its boundary.

## C++23 deducing this: A Modern Replacement for CRTP

CRTP is a little convoluted to write (`static_cast<Derived*>(this)`). C++23 offers a more direct replacement called **deducing this** (explicit object parameter). It lets a member function receive a `this` parameter explicitly, and the compiler deduces it from the type of the calling object.

```cpp
// C++23 deducing this style: Shape is no longer a template
struct Shape {
    // the type of self is deduced from the calling object, no longer a hardcoded Derived
    double area(this auto const& self) { return self.area_impl(); }
};

struct Circle : Shape {   // Circle still inherits Shape, but no longer needs to pass Shape<Circle> as a template argument
    double r;
    double area_impl() const { return 3.14159 * r * r; }
};
// Circle c; c.area() deduces self as Circle const&, calling Circle::area_impl
```

deducing this makes static polymorphism read more like ordinary functions, without `static_cast` and without the derived class passing itself to itself. It is a major C++23 feature, covered in detail in part three of this volume (metaprogramming) and in vol2 on modern features. Until then, CRTP remains the standard way to do static polymorphism in C++17 and earlier projects, and you will see it everywhere in Eigen and Boost code.

The next piece closes the concept portion of this volume: a `fixed_vector<T, N>` project. With a compile-time fixed-size contiguous store, it strings together everything from this volume, templates, non-type parameters, iterators, and CRTP (optional), into a fixed-length container with zero dynamic allocation, and compares it with the C++23/26 `std::inplace_vector`.
