---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'A capstone project that hand-writes a type-safe mini any, welding together the previous three pieces — if constexpr, variadic templates, and perfect forwarding — and works type erasure, the type_info comparison behind any_cast, in_place forwarding construction, and the small buffer optimization SBO all the way through'
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'if constexpr: Compile-Time Branching'
- 'Variadic Templates: Expanding Parameter Packs'
- 'Perfect Forwarding: Forwarding References and Reference Collapsing'
- 'CTAD: Class Template Argument Deduction'
reading_time_minutes: 18
related:
- 'if constexpr: Compile-Time Branching'
- 'Perfect Forwarding: Forwarding References and Reference Collapsing'
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 模板
- 泛型
- RAII
title: 'Capstone Project: A Type-Safe any'
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/05-type-safe-any.md
  source_hash: 0d439d5819be17c4e28554b4d6a5ccb4d1e3486a11f44dd80f1ef9fdeaa04840
  translated_at: '2026-09-26T03:28:17+00:00'
  engine: anthropic
  token_count: 4500
---
# Capstone Project: A Type-Safe any

By this point we have three tools in hand: `if constexpr` picks a branch at compile time, variadic templates expand any number of arguments, and perfect forwarding passes each argument along with its value category intact. Each is well-behaved on its own; put together, they can pull off something genuinely flashy — building our own type-safe `any`, that little container from the standard library which can hold "a value of any type".

This capstone project isn't aiming for size; it's aiming to make type erasure thoroughly clear. `std::any` looks mysterious, but underneath it is exactly a combination of the tools from the previous pieces. We start from the most naive "I want to store an arbitrary type" and hand-write, step by step: the pits `void*` falls into, how `type_info` rescues the situation, how a virtual copy makes any copyable, how in_place saves a redundant move, and how SBO saves a heap allocation. After reading this piece, open the standard library's `<any>` header and you'll find it's the industrial-grade implementation of this very mini version of ours.

## First, Why `void*` Isn't Good Enough

The requirement is straightforward: one variable that sometimes holds an `int`, sometimes a `std::string`, sometimes a custom struct. The first reflex is usually `void*`:

```cpp
void* data_;
data_ = new int(42);
data_ = new std::string("hello");
```

Storing works; getting the value back out is where the trouble starts. `void*` throws the type information away entirely. To retrieve that `int`, you have to write:

```cpp
int value = *static_cast<int*>(data_);
```

This cast relies entirely on a human remembering "what went in was an int". What if the memory fails? Let's store an `int` in an any and read it back as a `double`, and see what happens. Here is a "fake any" that stores nothing but a `void*` and records nothing:

```cpp
class UnsafeAny {
    void* data_;
public:
    template <typename T>
    explicit UnsafeAny(T value) : data_(new T(value)) {}
    ~UnsafeAny() {}                      // can't even get destruction right; this simply leaks

    template <typename T>
    T get_as() const { return *static_cast<T*>(data_); }   // cast to whatever you like
};
```

Run it — store `42`, read it back as `double`:

<OnlineCompilerDemo allow-run
  title="Fake any: void* casting with no type check at all — store an int, read a double, get nonsense"
  source-path="code/examples/vol4/vol2-modern-cpp17/any_cast_safety.cpp"
  description="UnsafeAny stores only a void*; get_as<double> reads the 4-byte int as the bit pattern of an 8-byte double, produces a garbage value, and the program never says a word."
/>

Output (excerpt):

```text
=== 假 any:存 int,取成 double,程序不报错但胡说 ===
  存 42,取成 double 得到: 2.07508e-322
```

`2.07508e-322`, a garbage number close to zero (the number you get on your own machine will almost certainly differ — it depends on what happens to be left in that memory). That `42` occupies only 4 bytes in memory; reading it as an 8-byte `double` bit pattern means every high bit read is uninitialized garbage. The result is completely wrong, and the program doesn't so much as complain. That is the fundamental problem with the `void*` approach: **the type information is gone, and the cast becomes an act of faith**. Destruction is the same pit: you don't know which destructor the object `data_` points to needs, so you can't even write the `delete`.

To fix both problems, an any has to carry, alongside the value, the whole group of type-related operations — "what type is this", "how to destroy it", "how to copy it", "how to cast back safely". That is exactly the problem type erasure solves.

## Type Erasure: One Uniform Shell to Hide the Concrete Type

The term type erasure sounds mystical, but the core is one sentence: **the outer layer exposes a uniform interface, the inside uses templates to remember the concrete type, and the differences are hidden inside the shell**.

`std::any` is the textbook case of this pattern. To every type, the outer `any` class looks the same: one fixed-size object holding a "handle" inside. Only the object behind that handle — the one actually storing the value — knows whether it contains an `int` or a `string`. The outer layer talks to the handle only through a set of type-agnostic operations:

- **Type query**: `type()` returns `type_info` and tells the outside "what type I currently hold"
- **Destruction**: the handle knows how to destroy the object it holds
- **Cloning**: the handle can copy out a new instance of itself
- **Safe casting**: when the outside wants the value via `any_cast<T>`, the `type_info` is compared first; only on a match does the cast happen

There are two mainstream ways to implement this "uniform outside, concrete-type memory inside". We pick one and work it through thoroughly, then point out how the other differs.

## Route A: The Virtual-Function Style — Base-Class Pointer plus Template Derivation

This route is the most intuitive. The outer any holds a pointer to a "concept base class"; the base class defines that group of operations as virtual functions. A template-derived class `data_holder<T>` is what actually stores the value and implements those virtual functions. When `data_holder<int>` is instantiated, the compiler has generated the derived class "built specifically to hold an int".

```cpp
class Any {
private:
    struct concept_any_base {                              // the concept base class
        virtual ~concept_any_base() = default;
        virtual const std::type_info& type() const noexcept = 0;
        virtual std::unique_ptr<concept_any_base> clone() const = 0;
        virtual const void* untyped() const noexcept = 0;  // hands out the internal pointer
    };

    template <typename T>
    struct data_holder final : concept_any_base {          // the derived class that actually stores T
        T data;
        // ... implement those virtual functions
    };

    std::unique_ptr<concept_any_base> holder_;             // the outer layer sees only a base-class pointer
};
```

The outer layer has no idea what `data_holder<T>` looks like; all it holds is a `concept_any_base*`. This is type erasure at work: the information about `T` is locked inside the derived class, and the outer layer manipulates it through one uniform base-class interface.

`data_holder<T>` implements those virtual functions. `type()` returns `typeid(T)`; `clone()` uses `make_unique` to build a fresh derived class holding the same `T`; `untyped()` hands out the address of the internal `data` as a `const void*`:

```cpp
template <typename T>
struct data_holder final : concept_any_base {
    T data;
    template <typename... Args>
    explicit data_holder(Args&&... args) : data(std::forward<Args>(args)...) {}

    const std::type_info& type() const noexcept override { return typeid(T); }
    std::unique_ptr<concept_any_base> clone() const override {
        return std::make_unique<data_holder<T>>(data);
    }
    const void* untyped() const noexcept override { return &data; }
};
```

One detail here deserves a pause. `data_holder`'s constructor is a variadic template paired with `std::forward<Args>(args)...` (the perfect forwarding from the previous piece). It isn't picky: it accepts any number of arguments of any value category and forwards them as-is to `T`'s constructor. This one constructor supports both "constructing an any from an existing value" and the in_place direct construction we're about to cover — one piece of code, two uses.

::: warning Route B: The Function-Pointer Table — What the Standard Library Actually Does
`std::any` in mainstream implementations takes the other road and uses no virtual functions. Internally it holds a `void*` plus a set of function pointers (`destroy`, `copy`, `move`, `cast`); these pointers are generated from `T` by template functions and stored into a table at construction. The difference: the virtual-function style dispatches through a vtable and pays an extra virtual call; the function-pointer-table style flattens dispatch into a few function-pointer calls, has a tighter layout, and makes SBO (small buffer optimization) easier to build. Functionally the two are equivalent. We chose Route A because it presents the "base-class interface + template derivation" OO paradigm most clearly — and that paradigm is the common skeleton behind type-erasure scenes like `std::function` and the `std::shared_ptr` deleter.
:::

## Constructing from Any Value, Perfectly Forwarding It In

With the shell built, the next step is letting the any hold arbitrary values. The most basic form is constructing from an existing value:

```cpp
template <typename T,
          typename DT = std::decay_t<T>,
          typename = std::enable_if_t<!std::is_same_v<DT, Any>>>
Any(T&& value)
    : holder_(std::make_unique<data_holder<DT>>(std::forward<T>(value))) {}
```

Look at the three pieces together and it's clear. `T&&` is a forwarding reference: with an lvalue argument it deduces to an lvalue reference, with an rvalue to an rvalue reference (the reference collapsing from the previous piece). `DT = decay_t<T>` strips references and top-level `const` off the deduced type, yielding "the type we actually want to store" — pass `const int&`, for instance, and `DT` is `int`. Finally, `forward<T>(value)` forwards the value with its original category to `data_holder`'s constructor.

That `enable_if` exists to fence off the copy constructor: in `Any a = b;`, `T` deduces to `Any&`; without this constraint, the template constructor would be a better match than the predefined copy constructor and compilation would go sideways. Blocking the `DT == Any` case hands copying back to the ordinary copy constructor.

This alone still isn't ideal. Suppose the held type is heavy and all we have are its constructor arguments, not a ready-made object. A line like `Any a = Big{"x", 2};` first constructs the `Big` temporary, then moves it into `data_holder` — one move, purely wasted. The standard library prescribes in_place construction for exactly this:

```cpp
template <typename T, typename... Args>
explicit Any(std::in_place_type_t<T>, Args&&... args)
    : holder_(std::make_unique<data_holder<T>>(std::forward<Args>(args)...)) {}
```

`std::in_place_type_t<T>` is an empty tag type; its job is to distinguish this constructor's signature from the construct-from-value overload above. `Args...` is a parameter pack, and `std::forward<Args>(args)...` forwards any number of constructor arguments, untouched, to `T`'s constructor. `T` grows "in place" inside `data_holder`, and the intermediate move disappears.

Count the moves and the difference jumps out. The next example uses a type with a move counter to compare the two construction styles:

<OnlineCompilerDemo allow-run
  title="in_place plus perfect forwarding: the held object is constructed in place, saving the extra move"
  source-path="code/examples/vol4/vol2-modern-cpp17/in_place_forward.cpp"
  description="Style 1 constructs from a ready-made Tracked, so the temporary must be moved into the holder; style 2 uses in_place to forward the constructor arguments directly, and Tracked is constructed in place inside the holder — zero moves."
/>

Output:

```text
=== 方式 1:先有 Tracked 临时对象,再 move 进 any ===
  [Tracked(string)] 直接构造, payload=hello
  [Tracked(&&)] 移动 #1
  方式 1 总移动次数 = 1, 总拷贝次数 = 0

=== 方式 2:in_place 把构造参数直接转发 ===
  [Tracked(string)] 直接构造, payload=hello
  方式 2 总移动次数 = 0, 总拷贝次数 = 0
```

In style 1, `Tracked{"hello"}` first constructs a temporary, and that temporary is then moved into `holder`'s `data` — hence one move. Style 2 forwards the constructor argument `string("hello")` straight to `Tracked`'s constructor; `Tracked` is constructed in place inside `holder`, and the whole run has zero extra moves or copies. That is why `std::any::emplace`, `std::make_unique`, and `std::vector::emplace_back` all use in_place — for a heavy object, saving one move is a real, tangible gain. Here the variadic template's `Args...` and perfect forwarding's `forward<Args>(args)...` are stitched together; the tools from the previous three pieces converge at this point.

## any_cast: Safe Casting via `type_info` Comparison

Getting a stored value back out safely is the most critical link in any's design. The lesson of the `void*` version was a cast with zero validation — store an `int`, read a `double`, and get pure nonsense. `any_cast`'s approach: compare `type_info` before casting, refuse on mismatch, and never perform a `reinterpret_cast` without validation.

That internal `untyped()` hands out the held object's address as a `const void*`. Once `any_cast<T>` has this pointer, it first confirms the stored type really is `T`, then `static_cast`s it back. This `static_cast` is safe, because what `untyped()` returns is, by construction, the address of `data_holder<T>::data`. We provide two overloads: the pointer version returns `nullptr` on mismatch; the value version throws on mismatch.

```cpp
// Pointer overload: returns nullptr on mismatch, doesn't throw
template <typename T>
const T* any_cast(const Any* a) noexcept {
    if (!a || !a->has_value() || a->type() != typeid(T)) {
        return nullptr;
    }
    return static_cast<const T*>(a->holder_->untyped());
}

// Value overload: throws bad_cast on mismatch (the standard library uses bad_any_cast)
template <typename T>
T any_cast(const Any& a) {
    const T* p = any_cast<T>(&a);
    if (!p) throw std::bad_cast{};
    return *p;
}
```

Run it and watch safe casting intercept mistakes in practice:

<OnlineCompilerDemo allow-run
  title="The complete mini any: store and retrieve, throw on mismatch, pointer form returns nullptr, deep copies stay independent"
  source-path="code/examples/vol4/vol2-modern-cpp17/mini_any.cpp"
  description="int/string/Point all come back correctly; storing an int and reading a double is caught by the type_info comparison and throws bad_cast; the pointer form returns nullptr on mismatch; after a copy the two anys leave each other alone."
/>

Output:

```text
=== 存取基础类型 ===
any_cast<int>(a)    = 42
any_cast<string>(b) = hello
any_cast<Point>(c)  = {1, 2}

=== 类型不匹配 -> 抛异常 ===
  bad_cast:存 int 取 double,被 type_info 比对拦下

=== 指针重载:不匹配返回 nullptr ===
  any_cast<int>(pa)  非空? 1
  any_cast<long>(pa) 非空? 0

=== 深拷贝:两个 any 独立 ===
  d 改成 string 后,a 仍是 int = 42

=== in_place 构造 ===
  e = "xxxx"
```

Storing `int` and reading `int` works fine; storing `int` and reading `double` is stopped by `type() != typeid(T)` before any cast happens and throws `bad_cast`. Compare that with the fake any earlier, which stored 42 and handed back a garbage double: the same mistaken operation, and the real any shouts a halt before the cast, while the fake any silently hands you a garbage value.

### A Trap That's Easy to Step Into: `any_cast` Demands an Exact Match and Does No Implicit Conversion

`any_cast` compares with `typeid`, and `typeid` has a property: **top-level `const` is ignored, but the type itself must match exactly**. That means `any_cast<const int>` can retrieve a stored `int`, but `any_cast<long>` on a stored `int` fails — even though `int` converts implicitly to `long`. The same example verifies all of these boundaries:

```text
typeid(int) == typeid(int):        1
typeid(int) == typeid(const int):  1  (顶层 const 被 typeid 忽略,可取)
typeid(int) == typeid(long):       0  (不同类型,拒)
typeid(int) == typeid(unsigned):   0  (不同类型,拒)
typeid(string) == typeid(const char*): 0  (完全不同,拒)
```

This boundary is a different animal from polymorphic casts like `dynamic_cast`. `dynamic_cast` walks the inheritance chain — a base-class pointer can become a derived-class pointer; `any_cast` recognizes no inheritance and performs no implicit numeric conversions; it accepts one `type_info` and one only. Store `int` and want `long`, store `std::string` and want `const char*` — you must convert the type explicitly first, then `any_cast`, or it fails across the board. I specifically verified this against the standard library's `std::any`, and the behavior matches our mini version exactly: with an `int` stored, `any_cast<long>` returns `nullptr`, `any_cast<unsigned>` also returns `nullptr`, and only `any_cast<int>` (and its top-level-const variants) retrieves the value.

## A Copyable any: Carried by a Virtual clone

Storing and retrieving isn't enough; an any must also be copyable. For `Any d = a;` to work, the any has to know how to duplicate the object it holds. The trouble is that the outer any doesn't know the held type and can't copy it directly. Enter the virtual `clone()`.

The base class declares `clone()` pure virtual; the derived `data_holder<T>` implements it as `make_unique<data_holder<T>>(data)` — copy the held object and wrap it in a fresh `data_holder`. The outer any's copy constructor just calls the virtual `clone()`:

```cpp
Any(const Any& other)
    : holder_(other.holder_ ? other.holder_->clone() : nullptr) {}
```

What happens behind that one line is the classic C++ polymorphism scenario. Although `other.holder_` is a base-class pointer, calling `clone()` dynamically dispatches to the right `data_holder<T>::clone`, producing a new handle with the same type and the same value. In the deep-copy output above, `d` was copied from `a` (which held `42`); later `d` was assigned a `string`, and the original `a` was untouched, still `42` — each any holds its own handle and they leave each other alone.

::: warning Copyability Is a Requirement on the Held Type
Implementing `clone()` as `make_unique<data_holder<T>>(data)` requires `T` to be copy-constructible. If you store a move-only, non-copyable type (say `std::unique_ptr`), instantiating `data_holder<unique_ptr>::clone` fails to compile. An any's copyability is "contagious": whether the whole any can be copied depends on whether the type it currently holds can be copied. The standard library's `std::any` throws at runtime when copying a non-copyable held type (because it uses the type-erased function-pointer table, with the clone slot registered as a throwing stub), while our virtual-function version stops it at compile time. The two approaches trade off differently, but the principle is the same: copy semantics must be guaranteed by the held type.
:::

## Small Object Optimization: Small Types Stored In Place, Large Types Go to the Heap

Our current implementation calls `make_unique` on every store; even a single `int` costs a heap allocation. Heap allocation isn't cheap, and the standard library's `std::any` avoids it by broadly implementing the **small object optimization (Small Buffer Optimization, SBO)**: reserve a fixed-size internal buffer, placement-new small types directly into it, and only allocate on the heap for large types.

This optimization is a natural habitat for `if constexpr`. We give the any a buffer and pick a path at compile time based on `sizeof(model<T>)`:

```cpp
template <typename T, typename D = std::decay_t<T>,
          typename = std::enable_if_t<!std::is_same_v<D, SboAny>>>
SboAny(T&& v) {
    if constexpr (sizeof(model<D>) <= BUF) {
        ptr_ = new (buffer_) model<D>(std::forward<T>(v));   // in place, no heap allocation
        owns_heap_ = false;
    } else {
        ptr_ = new model<D>(std::forward<T>(v));             // too big, heap
        owns_heap_ = true;
    }
}
```

The condition `if constexpr (sizeof(model<D>) <= BUF)` is settled at compile time, because `model<D>`'s size is known at instantiation. Small types take the first branch: `new (buffer_)` is placement new, constructing the object on the existing buffer without calling global `operator new`. Large types take the second branch, a normal heap allocation. The discarded branch is never instantiated at all — exactly the `if constexpr`-replaces-a-pile-of-partial-specializations scenario from the first piece.

Picking a path at construction isn't enough; copying has to dispatch too. We add two virtual functions to the base class: `clone_into(buf)` tries to fit into the passed-in buffer and returns `nullptr` if it doesn't fit; `clone_heap()` clones on the heap. The derived class uses `if constexpr` to decide how `clone_into` behaves:

```cpp
concept_base* clone_into(char* buf) const override {
    if constexpr (sizeof(model<T>) <= BUF) {
        return new (buf) model<T>(data);   // fits, in place
    }
    return nullptr;                         // doesn't fit, tell the outer layer to use the heap
}
```

The outer copy constructor tries `clone_into` first and falls back to `clone_heap` on failure:

```cpp
SboAny(const SboAny& o) {
    if (!o.ptr_) return;
    ptr_ = o.ptr_->clone_into(buffer_);   // first try to fit into our own buffer
    if (!ptr_) { ptr_ = o.ptr_->clone_heap(); owns_heap_ = true; }
}
```

We overload global `operator new` to count heap allocations and see SBO's effect directly:

<OnlineCompilerDemo allow-run
  title="SBO: zero heap allocations for small types, the heap only for large ones; if constexpr picks the path at construction and clone time"
  source-path="code/examples/vol4/vol2-modern-cpp17/sbo_any.cpp"
  description="Overloading operator new to count allocations: storing an int (small) costs 0, storing a Big (128 bytes) costs 1; copying the int is 0 again, copying the Big is 1. if constexpr picks the path at compile time based on sizeof(model<T>)."
/>

Output:

```text
BUF = 24 bytes (sizeof(void*)*3 on 64-bit)

=== 存 int:model<int> 很小,就地存 ===
  type==int? 1, on_heap? 0, 堆分配次数 = 0 (期望 0)

=== 存 Big:sizeof(model<Big>) > BUF,堆上存 ===
  type==Big? 1, on_heap? 1, 堆分配次数 = 1 (期望 1)

=== 拷贝 int:小类型拷贝走 clone_into,也不分配 ===
  b.on_heap? 0, 拷贝堆分配次数 = 0 (期望 0)

=== 拷贝 Big:大类型 clone_into 返回 nullptr,落回 clone_heap ===
  b.on_heap? 1, 拷贝堆分配次数 = 1 (期望 1)
```

The `int` goes end to end with zero heap allocations; only `Big` (128 bytes, over the 24-byte buffer) allocates once. Copying dispatches the same way: copying a small type still goes through `clone_into` and doesn't allocate, while copying a large type, whose `clone_into` returns `nullptr`, falls back to `clone_heap` and hits the heap. This is the textbook use of `if constexpr` with `sizeof` for compile-time dispatch: the condition depends on the template parameter `T`, every instantiation gets its own outcome — and that is exactly how it differs from an ordinary `if`.

::: warning SBO Changes sizeof, and the Standard Library Promises No Buffer Size
With SBO added, `sizeof(SboAny)` grows (at least the buffer size plus a few pointers/flags). This is the common trade of space for heap allocation — `std::function` and `std::string`'s short-string optimization follow the same idea. The standard library's `std::any` broadly does SBO, but **promises nothing about the buffer's size**, so you cannot assume how large a type fits inside `sizeof(std::any)`; that is implementation-defined. Our mini version sets the buffer to `sizeof(void*) * 3` (24 bytes on 64-bit) purely for demonstration.
:::

## The Tools, Seen Together

At this point the mini any has put all three previous pieces to work. The variadic template `template <typename... Args>` lets one constructor serve both "construct from a value" and "construct via in_place forwarding"; perfect forwarding, `std::forward<Args>(args)...`, hands constructor arguments untouched to the held object's constructor and saves the extra move; `if constexpr` with `sizeof` picks the storage path at compile time inside SBO according to the held type. Type erasure itself rests on the "concept base class + template derivation" OO paradigm, hiding the type-related operations — what type is stored, how to destroy, how to copy, how to cast safely — behind one uniform shell.

The real `std::any` is more refined than this mini version: it replaces virtual functions with a function-pointer table to save the vtable overhead, carefully tunes SBO's buffer size and its fit condition, and `any_cast` has to handle a whole family of reference and pointer overloads. But the skeleton is what we wrote here. Later, when you read `std::function` holding an arbitrary callable, `std::shared_ptr` with a custom deleter, or `std::move_only_function` holding a move-only callback, you'll find them all using the same type-erasure skeleton: a uniform interface outside, template derivation remembering the concrete type inside. This piece wraps up the vol2 sub-volume; welding these tools into a small thing that actually runs is also how Modern C++'s whole "doing tricks on types" line of thinking lands on solid ground.
