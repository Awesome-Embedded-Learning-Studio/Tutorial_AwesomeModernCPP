---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: 'CRTP has the derived class pass itself to the base class as a template
  argument, producing compile-time static polymorphism that avoids the vtable and
  runtime dispatch of virtual functions. This piece covers its structure, the assembly-level
  proof of zero overhead, typical uses such as mixins, its pitfalls, and the modern
  C++23 replacement, deducing this.'
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
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/09-crtp.md
  source_hash: 80e9ad7057a9fe050d09a08a46770574e39805307427e7464893ba30635da17a
  translated_at: '2026-09-26T04:14:20+00:00'
  engine: anthropic
  token_count: 5400
---
# CRTP: Static Polymorphism with the Curiously Recurring Template Pattern

CRTP, short for Curiously Recurring Template Pattern. Its structure looks odd at first glance: when the derived class inherits the base class, it passes itself as the template argument to the base. `struct Derived : Base<Derived>`. This "self-referencing" trick can produce compile-time **static polymorphism**: when there is no need to settle on the concrete type at runtime, it sidesteps the vtable and dispatch cost of virtual functions entirely. Eigen's expression templates and the mixin mechanisms of many high-performance libraries are all built on CRTP. This piece sorts out its structure, the assembly-level evidence that it is zero-overhead, several typical uses, and the pitfalls you cannot get around.

## What CRTP Looks Like: The Derived Class Passes Itself to the Base

First, the minimal example. The base class `Shape` is a template, and its template parameter is "the derived class itself."

```cpp
#include <iostream>

template <typename Derived>
struct Shape {
    const char* name() {
        return static_cast<Derived*>(this)->name_impl();   // cast this to a pointer to the derived class
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

The key is the line `struct Circle : Shape<Circle>`. `Circle` inherits `Shape`, but the template argument it hands to `Shape` is `Circle` itself. So inside the base class `Shape<Derived>`, `Derived` is `Circle`, and the base class's `area()` calls through `static_cast<Derived*>(this)->area_impl()` — which actually lands in `Circle::area_impl`.

Run it:

```bash
$ g++ -Wall -Wextra -std=c++20 crtp_basic.cpp -o crtp_basic && ./crtp_basic
Circle area = 12.5664
Square area = 9
```

`Circle` goes through `Circle::area_impl`, `Square` goes through `Square::area_impl`, each computing its own. The effect looks the same as virtual-function polymorphism, but the mechanism is completely different.

## Static Polymorphism: The Concrete Type Is Known at Compile Time

Virtual-function polymorphism happens at **runtime**. At a `base->area()` call, the concrete type `base` points to is not known until runtime, so the compiler can only generate "look up the vtable, call indirectly" code.

CRTP's polymorphism happens at **compile time**. When `Shape<Circle>::area()` is instantiated, `Derived` is already pinned down as `Circle`: `static_cast<Derived*>(this)->area_impl()` is literally `static_cast<Circle*>(this)->area_impl()`. The compiler knows perfectly well that it is calling `Circle::area_impl`, so it can call it directly — even inline it. No vtable, no runtime dispatch.

This is the core value of CRTP: **when the concrete type can be pinned down at compile time, replace virtual functions with static polymorphism and trade for zero overhead**.

## Zero Overhead, Proven in Assembly

Talk is cheap; let's look at the assembly. Write a CRTP version and a virtual-function version that do the same thing (return 42), compile with `-O2`, and compare the disassembly of the `use` functions.

```cpp
// crtp_asm.cpp — the CRTP version
template <typename D>
struct Base {
    int compute() { return static_cast<D*>(this)->compute_impl(); }
};
struct Concrete : Base<Concrete> {
    int compute_impl() { return 42; }
};
int use_crtp() { Concrete c; return c.compute(); }
```

The complete disassembly of the CRTP `use_crtp` is just two instructions:

```text
0000000000000000 <_Z8use_crtpv>:
   0:   b8 2a 00 00 00   mov    $0x2a,%eax    ; 0x2a = 42, the result goes straight into the return value
   5:   c3               ret
```

`compute()` and `compute_impl()` are fully inlined; the compiler works out the result 42 directly, and even the function call is gone. `use_crtp` is simply "put 42 into eax, return."

Now the virtual-function version (called through a reference, to force virtual dispatch):

```cpp
// vtable_asm.cpp — the virtual-function version
struct Base {
    virtual int compute() = 0;
    virtual ~Base() = default;
};
struct Concrete : Base {
    int compute() override { return 42; }
};
int use_virtual(Base& b) { return b.compute(); }   // through a reference, the compiler cannot devirtualize
```

The disassembly of `use_virtual` is much longer:

```text
0000000000000000 <_Z11use_virtualR4Base>:
   0:   48 8b 07         mov    (%rdi),%rax        ; load the vtable pointer from the object
   3:   48 8d 15 ...     lea    ...(%rip),%rdx     ; the address of Concrete's vtable
   a:   48 8b 00         mov    (%rax),%rax        ; load compute's function pointer from the vtable
   d:   48 39 d0         cmp    %rdx,%rax          ; speculative devirtualization: is it Concrete?
  10:   75 0e            jne    20                 ; only take the indirect call if it is not
  12:   b8 2a 00 00 00   mov    $0x2a,%eax         ; it is Concrete, return 42 directly
  17:   c3               ret
```

The virtual-function version does two dereferences of the vtable pointer (`mov (%rdi)` to fetch the vtable, `mov (%rax)` to fetch the function pointer), plus one comparison and one conditional jump (GCC's "speculative devirtualization" optimization: first guess whether `b` is `Concrete`; if the guess hits, return directly, and only on a miss take the real indirect call). Even with `-O2` on, the cost of this vtable access and the devirtualization check is still there.

The contrast is plain: the CRTP version is two instructions with zero memory accesses; the virtual-function version is seven instructions with two memory accesses (the vtable). This is the most direct evidence that "static polymorphism is zero-overhead." In a numerical library like Eigen, expression evaluation is flattened by CRTP entirely into compile time — at runtime it is just a run of direct arithmetic instructions with no polymorphic dispatch at all, and that is the fundamental reason it can be as fast as hand-written loops.

## Typical Uses of CRTP

CRTP is not only for static polymorphism; it has several other high-frequency uses.

**Static polymorphism** (this piece's focus). The base class defines the interface skeleton, derived classes provide the concrete implementations, binding happens at compile time, and there is no virtual-function overhead. It fits "fixed interface, many implementations, performance-sensitive" scenarios — the operations of numerical libraries, or the iterators of containers, for example.

**Mixins**. The base class "injects" a chunk of generic functionality into the derived class, and the derived class only needs to supply the underlying data. For example, automatically generating the full set of comparison operators for every type that supports `<`:

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
// Point now automatically has > <= >= !=; you only implement < and ==
```

Run it, to confirm the mixin-injected operators really work:

```bash
$ g++ -Wall -Wextra -std=c++20 comparable.cpp -o comparable && ./comparable
p1 < p2:  true
p1 > p2:  false
p1 <= p2: true
p1 >= p2: false
p1 != p2: true
```

With `Point` you only write `<` and `==`; the other four comparison operators are all filled in automatically by `Comparable<Point>`.

As long as `Point` implements `<` and `==`, mixing in `Comparable<Point>` auto-completes all the remaining comparison operators. This goes hand in hand with the Barton-Nackman trick covered in Part 3: CRTP provides the structure, and friend injection provides the operators.

**Compile-time interface injection / policy injection**. The base class can require the derived class to provide certain typedefs or constants and check them at compile time, implementing a "static interface." For example, if the base class writes `using value_type = typename Derived::value_type;`, a derived class that does not expose `value_type` will not compile. Before concepts existed, this kind of "concept-like" compile-time constraint was often emulated with CRTP.

**Expression templates**. Eigen's `a + b * c` produces no temporary matrix; instead it builds an "expression type" that records the operation and evaluates everything in one pass at the final assignment, avoiding intermediate temporaries. This mechanism is built entirely on CRTP and is CRTP's most dazzling application — the vol3 metaprogramming part will take it apart in detail.

## CRTP's Pitfalls

CRTP is powerful, but there are a few pitfalls to steer around.

**The derived class is not complete while the base class constructs**. While the base class's constructor runs, the derived part has not been constructed yet. Calling `static_cast<Derived*>(this)` in the base-class constructor at that point to access derived members is undefined behavior (the object is not fully formed yet). By the same logic, do not call derived-class methods in the base-class destructor either. CRTP's cross-layer calls are safe only when the object is already complete at the moment of the call — not during construction or destruction.

**The safety assumption of `static_cast`**. `static_cast<Derived*>(this)` assumes `this` really points to a `Derived` object, and that assumption is guaranteed by you (when you write `struct Derived : Base<Derived>`). The good news is that the type system actually blocks part of this for you: if you get it wrong — say `struct Wrong : Base<Other>`, where `Wrong` and `Other` are utterly unrelated — the base class's `static_cast<Other*>(this)` will usually fail to compile, because `Wrong*` and `Other*` are unrelated types and `static_cast` refuses the conversion. So CRTP's type safety is stronger than it looks — on the condition that you honestly pass the derived class itself to the base.

**Virtual functions and CRTP do not interoperate**. CRTP's `area()` is not a virtual function; you cannot stuff `Circle*` and `Square*` into the same `Shape*` array and call `area()` uniformly. Scenarios that need runtime heterogeneous collections still have to use virtual functions. CRTP fits scenarios where "the concrete type is known at compile time" — that is its boundary.

## C++23 deducing this: CRTP's Modern Replacement

CRTP is a bit roundabout to write (`static_cast<Derived*>(this)`), and C++23 offers a more intuitive replacement called **deducing this** (the explicit object parameter). It lets a member function explicitly take a `this` parameter, which the compiler deduces from the type of the calling object:

```cpp
// C++23 deducing this style: Shape is no longer a template
struct Shape {
    // self's type is deduced from the calling object, no longer a hardcoded Derived
    double area(this auto const& self) { return self.area_impl(); }
};

struct Circle : Shape {   // Circle still inherits Shape, but no longer passes Shape<Circle> as a template argument
    double r;
    double area_impl() const { return 3.14159 * r * r; }
};
// in Circle c; c.area(), self deduces to Circle const&, calling Circle::area_impl
```

deducing this lets static polymorphism be written much more like ordinary functions — no `static_cast`, no passing the derived class to itself. It is a major C++23 feature, and Part 3 of this volume (metaprogramming) and vol2's coverage of modern features will cover it in detail. Before that, CRTP remains the standard way to do static polymorphism in C++17-and-earlier projects — read Eigen, read Boost, and you will find it everywhere.

The next piece closes out the concepts part of this volume: a `fixed_vector<T, N>` capstone project. Using compile-time fixed-size contiguous storage, it chains together everything learned before — templates, non-type parameters, iterators, and (optionally) CRTP — to implement a fixed-length container with zero dynamic allocation, and compares it against C++23/26's `std::inplace_vector`.
