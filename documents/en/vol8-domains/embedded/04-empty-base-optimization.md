# EBO (Empty Base Optimization): C++'s Slimming Trick

There is a low-profile yet highly effective memory optimization that silently saves bytes for you behind the scenes—**EBO (Empty Base Optimization)**. When writing libraries, we often use empty classes as "policies, tags, or stateless behavior objects." EBO squeezes these stateless base classes out of the object layout, saving space and improving locality.

------

## TL;DR

- **EBO allows the compiler to omit the storage of an empty base class subobject (i.e., it takes zero extra bytes), thereby reducing the `sizeof` the derived class.**
- **Empty member variables cannot be compressed by EBO by default, but `[[no_unique_address]]` introduced in C++20 achieves a similar compression effect for members.**
- **Do not rely on object address uniqueness to identify empty subobjects—their addresses might be identical (which is an allowed side effect of this optimization). Making assumptions about addresses leads to bugs.**
- In practice: library implementations commonly use the "inheriting from an empty policy class" or "compressed pair" trick. C++20 makes things cleaner, but understanding traditional EBO remains highly useful.

------

## The Concept: A Real-World Analogy

Imagine a container object with two members: one is an actual storage warehouse (like an `int` or a pointer), and the other is an empty "tag" that represents behavior but holds no data. Intuitively, you might allocate space for each member, but the language standard allows the compiler to place the "empty tag" base class subobject in a location that requires no extra space (such as reusing the first byte of the derived object). This makes the derived object smaller overall and more cache-friendly—which is the core of EBO.

The standard applies the "most-derived object must have non-zero size" requirement to the most-derived object, but **base class subobjects are exempt from this restriction**. The compiler can treat the size of an empty base class subobject as 0 (i.e., taking no extra bytes). This is the exact legal basis for EBO.

------

## A Simple Example

```cpp
struct Empty {}; // 空类

struct A {
    Empty e;     // 成员，通常会占 1 字节
    int x;
};

struct B : Empty { // 继承 Empty —— EBO 有机会发生
    int x;
};

static_assert(sizeof(A) >= sizeof(int) + 1);
static_assert(sizeof(B) == sizeof(int)); // 在支持 EBO 的编译器上通常成立

```

In the example above, ``Empty e`` in ``A`` is a data member. By language rules, it must occupy a non-zero number of bytes (to guarantee semantics like array indexing). However, ``B`` inherits from ``Empty`` as a base class, so the compiler can "compress" it into the layout of ``B``. As a result, ``sizeof(B)`` typically equals ``sizeof(int)`` (details may vary across different compilers/ABIs).

------

## Why Do We Often See the "Inheriting from Empty Classes" Pattern in the STL and Libraries?

In the standard library, types like allocators, comparators, and deleters are often stateless empty classes. If we use them as members, they waste space; using them as base classes (typically via **private inheritance**) enables EBO and reduces object size. Many implementations wrap the "pointer + empty deleter" scenario into a "compressed pair" or similar utility to achieve minimal object size. Microsoft's STL blog and other implementations demonstrate the ubiquity of this approach.

------

## C++20: `[[no_unique_address]]` Makes "Empty Member Optimization" Formal and Safe

Traditional EBO can only be achieved through inheritance (members cannot be compressed). The `[[no_unique_address]]` attribute introduced in C++20 allows **members** to share an address with other subobjects (i.e., allowing zero-size semantics). This achieves an EBO-like effect using member syntax, making the code more intuitive and the semantics clearer. For example:

```cpp
struct Empty {};
struct Holder {
    [[no_unique_address]] Empty e; // 现在可以和其它成员共享地址
    int x;
};

```

This looks much better in implementation than private inheritance, and it avoids the potential interface exposure that comes with inheritance. cppreference and various implementation articles summarize the semantics and limitations of `[[no_unique_address]]`. We strongly recommend prioritizing this approach wherever C++20 is available.

------

## Common Misconceptions and Pitfalls (Pay Attention)

- **"Empty class subobjects definitely don't have an address"—Wrong.** The standard allows a base class subobject to share the starting address of the most-derived object. This means the address of the base class subobject might be identical to that of another subobject (or the object as a whole). Do not write code that relies on subobject address uniqueness.
- **Why can't ``std::pair`` directly leverage EBO?** Because ``std::pair`` uses ``first`` and ``second`` as **members**, not empty base classes. Therefore, traditional EBO cannot apply to members (unless using ``[[no_unique_address]]`` or refactoring the implementation into a compressed-pair style). This is exactly why internal implementation tricks like "compressed pair" exist.
- **Multiple empty base classes can sometimes interfere with each other**: If you inherit from multiple empty types, the compiler will try to apply EBO to all of them. However, in certain situations (such as duplicate base class types, or identical types caused by ABI or nested templates), the optimization is restricted. A common practice is to make each empty base class type "unique" to the compiler (e.g., by parameterizing with a template) to ensure the compression takes effect. Some refer to this issue as "needing to differentiate base class types."

------

## Practical Advice

1. **Don't prematurely optimize by default**: Writing policy classes as empty classes using either members or inheritance is fine; prioritize readability.
2. **If you need minimal memory or are implementing a library (like smart pointers or containers), prioritize ``[[no_unique_address]]`` (C++20) or controlled private inheritance EBO tricks.** C++20 makes the code more intuitive.
3. **Don't rely on object or subobject address uniqueness**: When writing debugging, serialization, or comparison logic, avoid using addresses to distinguish empty subobjects. Addresses might be identical, and the standard permits this reuse.

------

## Summary

EBO is a micro-optimization in C++ that "produces visible results without showing off": it prevents empty policy classes from wasting bytes. Historically, we implemented EBO using private inheritance. Modern C++ (C++20) uses `[[no_unique_address]]` to allow empty members to be compressed as well, making the code more intuitive and safer. In real-world engineering, prioritize writing clear, maintainable code. When object size becomes sensitive, then apply EBO, `[[no_unique_address]]`, or compressed-pair tricks to manually optimize, and verify the behavior on your target compiler.
