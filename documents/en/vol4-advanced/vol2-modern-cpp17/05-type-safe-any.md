---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: A capstone project that hand-rolls a type-safe mini any, welding together if constexpr, variadic templates, and perfect forwarding from the previous three pieces, to make type erasure, any_cast's type_info check, in-place forwarding construction, and small buffer optimization click.
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
---
# Capstone Project: A Type-Safe any

By this piece we have three tools in hand: `if constexpr` picks a branch at compile time, variadic templates expand an arbitrary number of arguments, and perfect forwarding hands argument value categories through untouched. Each is ordinary on its own; together they let us do something genuinely satisfying: build our own type-safe `any`, the little container from the standard library that holds "a value of any type."

This capstone isn't about scale, it's about making type erasure click. `std::any` looks mysterious, but underneath it's a composition of exactly these tools. We start from the plain "I want to store any type," and hand-write our way through every trap `void*` walks into, how `type_info` rescues us, how a virtual clone makes any copyable, how in_place saves a redundant move, and how SBO saves a heap allocation. After this, when you open the standard library's `<any>` header, you'll find it's an industrial-grade version of exactly this mini implementation.

## First, why `void*` falls short

The need is plain. A variable that sometimes holds an `int`, sometimes a `std::string`, sometimes a custom struct. The first instinct is usually `void*`:

```cpp
void* data_;
data_ = new int(42);
data_ = new std::string("hello");
```

Storing works; getting the value back is where it hurts. `void*` throws away all type information. To get the `int` back you have to write:

```cpp
int value = *static_cast<int*>(data_);
```

That cast relies entirely on a human remembering "what we stored was an int." What if you remember wrong? Let's store an `int` and read it back as a `double`. Here's a "fake any" that only keeps a `void*` and nothing else:

```cpp
class UnsafeAny {
    void* data_;
public:
    template <typename T>
    explicit UnsafeAny(T value) : data_(new T(value)) {}
    ~UnsafeAny() {}                      // can't even destruct correctly; leak here

    template <typename T>
    T get_as() const { return *static_cast<T*>(data_); }   // cast to whatever you want
};
```

Run it, store `42`, read as `double`:

<OnlineCompilerDemo allow-run
  title="Fake any: void* casts with no type check, storing int and reading double gives garbage"
  source-path="code/examples/vol4/vol2-modern-cpp17/any_cast_safety.cpp"
  description="UnsafeAny only stores void*; get_as<double> reads a 4-byte int as an 8-byte double's bit pattern, producing garbage, and the program says nothing."
/>

Run output (excerpt):

```text
=== 假 any:存 int,取成 double,程序不报错但胡说 ===
  存 42,取成 double 得到: 2.07508e-322
```

`2.07508e-322`, a garbage number near zero. That `42` in memory occupies 4 bytes; `double` insists on reading 8 bytes of bit pattern, the high bytes are uninitialized garbage, and the result is complete nonsense that the program never flags. This is the fundamental flaw of the `void*` approach: **the type information is gone, and casting becomes an act of faith.** Destruction is the same trap. You don't know which destructor `data_`'s object needs, so you can't even write the `delete`.

To fix both, any has to carry, alongside the value, a bundle of type-specific operations: "what type am I," "how do I destruct," "how do I copy myself," "how do I cast back safely." That's what type erasure is about.

## Type erasure: a uniform shell that hides the concrete type

Type erasure sounds abstract; its core is one sentence: **the outer layer offers a uniform interface, the inner layer uses a template to remember the concrete type, and the differences live behind the shell.**

`std::any` is the textbook example. The outer `any` class looks the same for every type: a fixed-size object holding a "handle." The object behind the handle, which actually stores the value, is the only thing that knows whether it's an `int` or a `string`. The outer layer talks to the handle only through a set of type-agnostic operations:

- **Type query**: `type()` returns `type_info`, telling the outside "what type I currently hold"
- **Destruction**: the handle knows how to destruct its own object
- **Clone**: the handle can copy itself into a new one
- **Safe cast**: the outside uses `any_cast<T>` to read; it checks `type_info` first and only casts if they match

There are two mainstream ways to implement this "uniform outside, type-remembering inside." We pick one and work it through, then point out the other.

## Route A: virtual-function style, a base pointer plus a templated derived class

This route is the most intuitive. The outer any holds a pointer to a "concept base class," which declares those operations as virtual functions; a templated derived class `data_holder<T>` actually stores the value and implements them. When you instantiate `data_holder<int>`, the compiler generates the derived class "specialized for int."

```cpp
class Any {
private:
    struct concept_any_base {                              // concept base
        virtual ~concept_any_base() = default;
        virtual const std::type_info& type() const noexcept = 0;
        virtual std::unique_ptr<concept_any_base> clone() const = 0;
        virtual const void* untyped() const noexcept = 0;  // hand out inner pointer
    };

    template <typename T>
    struct data_holder final : concept_any_base {          // the derived class that stores T
        T data;
        // ... implement the virtuals above
    };

    std::unique_ptr<concept_any_base> holder_;             // outer only sees the base pointer
};
```

The outer layer has no idea what `data_holder<T>` looks like; it holds a `concept_any_base*`. That's type erasure at work: the `T` information is locked inside the derived class, and the outer layer operates on it through a uniform base interface.

`data_holder<T>` implements those virtuals. `type()` returns `typeid(T)`, `clone()` builds a fresh derived class holding the same `T` via `make_unique`, and `untyped()` hands out the address of `data` as `const void*`:

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

One detail worth pausing on. `data_holder`'s constructor is a variadic template paired with `std::forward<Args>(args)...` (perfect forwarding from the previous piece). It's not picky: any number of arguments of any value category, forwarded unchanged to `T`'s constructor. This one constructor backs both "construct any from an existing value" and the in_place direct construction we'll get to, one piece of code serving two purposes.

::: warning Route B: function-pointer table, what the standard library actually does
`std::any` in mainstream implementations takes the other route, without virtual functions. It holds a `void*` plus a set of function pointers (`destroy`, `copy`, `move`, `cast`); these are generated per `T` by template functions and stored in a table at construction. The difference: the virtual-function style dispatches through a vtable, paying one virtual call; the function-pointer-table style flattens dispatch into a few function-pointer calls, has a tighter layout, and makes SBO (small buffer optimization) easier. Functionally they're equivalent. We pick route A because it makes the "base interface plus templated derived class" OO pattern clearest, and that pattern is the common skeleton behind type erasure in `std::function`, `std::shared_ptr`'s deleter, and friends.
:::

## Constructing any type, perfect-forwarded in

With the shell in place, let any hold an arbitrary value. The plain version constructs from an existing value:

```cpp
template <typename T,
          typename DT = std::decay_t<T>,
          typename = std::enable_if_t<!std::is_same_v<DT, Any>>>
Any(T&& value)
    : holder_(std::make_unique<data_holder<DT>>(std::forward<T>(value))) {}
```

Read the three parts together. `T&&` is a forwarding reference: an lvalue argument deduces to an lvalue reference, an rvalue to an rvalue reference (reference collapsing from the previous piece). `DT = decay_t<T>` strips references and top-level `const` from the deduced type to get "the type we actually store": pass `const int&` and `DT` is `int`. Finally `forward<T>(value)` forwards the value in its original category to `data_holder`'s constructor.

That `enable_if` is there to fend off the copy constructor: in `Any a = b;`, `T` deduces to `Any&`, and without this constraint this template constructor would be a better match than the predefined copy constructor and wreck compilation. Excluding `DT == Any` hands copy construction back to the normal copy constructor.

This alone isn't ideal. Suppose the held type is heavy and you have only its construction arguments, not a finished object. `Any a = Big{"x", 2};` first constructs a temporary `Big`, then moves it into `data_holder`, wasting a move. The standard library offers in_place construction to fix this:

```cpp
template <typename T, typename... Args>
explicit Any(std::in_place_type_t<T>, Args&&... args)
    : holder_(std::make_unique<data_holder<T>>(std::forward<Args>(args)...)) {}
```

`std::in_place_type_t<T>` is an empty tag type; its job is to give this constructor a signature distinct from the value-constructor overload. `Args...` is a parameter pack; `std::forward<Args>(args)...` forwards an arbitrary number of construction arguments unchanged to `T`'s constructor. `T` is constructed in place inside `data_holder`, skipping the intermediate move.

Let's count moves and the difference shows. This example uses a type with move counting to compare the two construction paths:

<OnlineCompilerDemo allow-run
  title="in_place + perfect forwarding: the held object is constructed in place, skipping the extra move"
  source-path="code/examples/vol4/vol2-modern-cpp17/in_place_forward.cpp"
  description="Path 1 constructs from an existing Tracked; the temporary must be moved into the holder. Path 2 uses in_place to forward the construction arguments directly; Tracked is constructed inside the holder, with zero moves."
/>

Run output:

```text
=== 方式 1:先有 Tracked 临时对象,再 move 进 any ===
  [Tracked(string)] 直接构造, payload=hello
  [Tracked(&&)] 移动 #1
  方式 1 总移动次数 = 1, 总拷贝次数 = 0

=== 方式 2:in_place 把构造参数直接转发 ===
  [Tracked(string)] 直接构造, payload=hello
  方式 2 总移动次数 = 0, 总拷贝次数 = 0
```

In path 1, `Tracked{"hello"}` first constructs a temporary, which is then moved into the holder's `data`, so one move. In path 2, `string("hello")` is forwarded straight to `Tracked`'s constructor, and `Tracked` is built in place inside the holder: zero extra moves or copies the whole way. That's why `std::any::emplace`, `std::make_unique`, and `std::vector::emplace_back` all use in_place: saving one move is a real win for heavy types. Here the variadic template's `Args...` and perfect forwarding's `forward<Args>(args)...` are welded together, the three previous pieces converging.

## any_cast: a type_info check for safe casting

How to get the stored value back safely is the most critical part of any's design. The lesson from the `void*` version was that casting had no check, so storing `int` and reading `double` produced garbage. `any_cast`'s approach is to compare `type_info` before casting and refuse on mismatch — never `reinterpret_cast` without the check.

The `untyped()` inside any hands out the held object's address as `const void*`. After `any_cast<T>` gets this pointer, it first confirms the stored type is exactly `T`, then `static_cast`s back. This `static_cast` is safe, because `untyped()` returns the address of `data_holder<T>::data`. We provide two overloads: the pointer version returns `nullptr` on mismatch, the value version throws.

```cpp
// Pointer overload: returns nullptr on mismatch, no throw
template <typename T>
const T* any_cast(const Any* a) noexcept {
    if (!a || !a->has_value() || a->type() != typeid(T)) {
        return nullptr;
    }
    return static_cast<const T*>(a->holder_->untyped());
}

// Value overload: throws bad_cast on mismatch (the standard uses bad_any_cast)
template <typename T>
T any_cast(const Any& a) {
    const T* p = any_cast<T>(&a);
    if (!p) throw std::bad_cast{};
    return *p;
}
```

Let's run it and watch safe casting catch the error in practice:

<OnlineCompilerDemo allow-run
  title="Complete mini any: store/get, throw on mismatch, nullptr from pointer form, independent deep copy"
  source-path="code/examples/vol4/vol2-modern-cpp17/mini_any.cpp"
  description="Storing int/string/Point all read back correctly; storing int and reading double is caught by the type_info check and throws bad_cast; the pointer form returns nullptr on mismatch; after copy the two anys are independent."
/>

Run output:

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

Storing `int` and reading `int` works; storing `int` and reading `double` is blocked before the cast by `type() != typeid(T)` and throws `bad_cast`. Contrast the fake any earlier that stored 42 and read back `2.07508e-322`: the same wrong operation, and the real any stops it cold before the cast, while the fake any silently hands you garbage.

### An easy trap: any_cast needs an exact match, no implicit conversions

`any_cast` compares with `typeid`, and `typeid` has a property: **top-level `const` is ignored, but the type itself must match exactly.** That means `any_cast<const int>` reads back a stored `int`, but `any_cast<long>` reading a stored `int` fails, even though `int` converts implicitly to `long`. We verified all these boundaries in the same example:

```text
typeid(int) == typeid(int):        1
typeid(int) == typeid(const int):  1  (顶层 const 被 typeid 忽略,可取)
typeid(int) == typeid(long):       0  (不同类型,拒)
typeid(int) == typeid(unsigned):   0  (不同类型,拒)
typeid(string) == typeid(const char*): 0  (完全不同,拒)
```

This boundary is a different beast from a polymorphic `dynamic_cast`. `dynamic_cast` walks the inheritance chain, letting a base pointer become a derived pointer; `any_cast` recognizes no inheritance and does no implicit numeric conversion: it accepts only one `type_info`. To read a stored `int` as `long`, or a stored `std::string` as `const char*`, you have to convert the type explicitly first, or `any_cast` fails. I verified this specifically against the standard library's `std::any`: its behavior matches our mini version exactly: store `int`, and `any_cast<long>` returns `nullptr`, `any_cast<unsigned>` returns `nullptr`, only `any_cast<int>` (and its top-level-const variants) reads back.

## A copyable any: implemented through a virtual clone

Storing and getting isn't enough; any has to be copyable. `Any d = a;` needs any to know how to duplicate the object it holds. The problem is the outer any doesn't know the held type, so it can't copy directly. That's exactly where a virtual `clone()` earns its keep.

The base declares `clone()` as pure virtual; the derived `data_holder<T>` implements it as `make_unique<data_holder<T>>(data)`: copy the held object and wrap it in a fresh `data_holder`. The outer any's copy constructor just calls the virtual `clone()`:

```cpp
Any(const Any& other)
    : holder_(other.holder_ ? other.holder_->clone() : nullptr) {}
```

What happens behind this line is a textbook case of C++ polymorphism. `other.holder_` is a base pointer, but calling `clone()` dynamically dispatches to the correct `data_holder<T>::clone`, producing a new handle with the same type and value. In the deep-copy run output earlier, `d` is copied from `a` (holding `42`); later `d` is reassigned to a `string`, and the original `a` is untouched and still `42`. The two anys each hold their own handle, independent.

::: warning Copyability is a requirement on the held type
`clone()` is implemented as `make_unique<data_holder<T>>(data)`, which requires `T` to be copy-constructible. If you store a move-only, non-copyable type (like `std::unique_ptr`), instantiating `data_holder<unique_ptr>::clone` fails to compile. any's copyability is "contagious": whether the whole any can be copied depends on whether the type it currently holds can be copied. The standard `std::any` throws at runtime when you copy a non-copyable held type (because its type-erased function-pointer table registers a clone stub that throws); our virtual-function version stops you at compile time. Each approach has its tradeoff, but the principle is the same: copy semantics must be guaranteed by the held type.
:::

## Small buffer optimization: small types in place, large types on the heap

Our implementation so far calls `make_unique` on every store, hitting the heap even for a single `int`. Heap allocation isn't cheap, and the standard `std::any` generally applies **small buffer optimization (SBO)** to avoid it: reserve a fixed-size internal buffer, placement-new small types directly into it, and only hit the heap for large types.

This optimization is a natural fit for `if constexpr`. We give any a buffer and, at construction, pick the path at compile time based on `sizeof(model<T>)`:

```cpp
template <typename T, typename D = std::decay_t<T>,
          typename = std::enable_if_t<!std::is_same_v<D, SboAny>>>
SboAny(T&& v) {
    if constexpr (sizeof(model<D>) <= BUF) {
        ptr_ = new (buffer_) model<D>(std::forward<T>(v));   // in place, no heap
        owns_heap_ = false;
    } else {
        ptr_ = new model<D>(std::forward<T>(v));             // too big, heap
        owns_heap_ = true;
    }
}
```

The condition `if constexpr (sizeof(model<D>) <= BUF)` is settled at compile time, since `model<D>`'s size is known at instantiation. Small types take the first branch; `new (buffer_)` is placement new, constructing the object in the existing buffer without calling global `operator new`. Large types take the second branch and allocate normally. The discarded branch is never instantiated, exactly the "if constexpr replaces a pile of partial specializations" scenario from the first piece.

Construction isn't enough; copying has to dispatch the same way. We add two virtuals to the base: `clone_into(buf)` tries to fit into the passed buffer and returns `nullptr` if it can't; `clone_heap()` clones on the heap. The derived class uses `if constexpr` to decide what `clone_into` does:

```cpp
concept_base* clone_into(char* buf) const override {
    if constexpr (sizeof(model<T>) <= BUF) {
        return new (buf) model<T>(data);   // fits, in place
    }
    return nullptr;                         // doesn't fit, tell outer to use heap
}
```

The outer copy constructor tries `clone_into` first, falling back to `clone_heap` on failure:

```cpp
SboAny(const SboAny& o) {
    if (!o.ptr_) return;
    ptr_ = o.ptr_->clone_into(buffer_);   // try our own buffer first
    if (!ptr_) { ptr_ = o.ptr_->clone_heap(); owns_heap_ = true; }
}
```

We overload global `operator new` to count allocations and watch SBO in action:

<OnlineCompilerDemo allow-run
  title="SBO: small types zero heap allocations, large types on the heap; if constexpr picks the path in construction and clone"
  source-path="code/examples/vol4/vol2-modern-cpp17/sbo_any.cpp"
  description="Overload operator new to count: storing int (small) is 0 allocations, Big (128 bytes) is 1; copying int is also 0, copying Big is 1. if constexpr picks the path at compile time based on sizeof(model<T>)."
/>

Run output:

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

`int` makes zero heap allocations the whole way; `Big` (128 bytes, over the 24-byte buffer) allocates once. Copying dispatches accordingly: a small-type copy goes through `clone_into` and still allocates nothing; a large-type copy, since `clone_into` returns `nullptr`, falls back to `clone_heap` on the heap. This is a textbook use of `if constexpr` with `sizeof` for compile-time dispatch: the condition depends on the template parameter `T`, the result shifts with each instantiation, which is exactly what sets it apart from a plain `if`.

::: warning SBO changes sizeof; the standard library doesn't promise the buffer size
Adding SBO makes `sizeof(SboAny)` larger (at least the buffer plus a couple of pointers/flags). This is the usual space-for-heap-allocation tradeoff, the same pattern as `std::function` and `std::string`'s small-string optimization. The standard `std::any` generally does SBO, but **does not promise how large the buffer is**, so you can't assume how big a type fits in `sizeof(std::any)`: it's implementation-defined. Our mini version sets the buffer to `sizeof(void*) * 3` (24 bytes on 64-bit) purely for demonstration.
:::

## The tools, brought together

By now this mini any has used everything from the previous three pieces. The variadic template `template <typename... Args>` lets one constructor serve both "construct from a value" and "in_place forwarding construction." Perfect forwarding `std::forward<Args>(args)...` hands construction arguments untouched to the held object's constructor, skipping redundant moves. `if constexpr` with `sizeof` picks the storage path at compile time in SBO, based on the held type. Type erasure itself rests on the "concept base plus templated derived class" OO pattern, hiding the type-specific bundle — what type is stored, how to destruct, how to copy, how to cast safely — behind a uniform shell.

The real `std::any` is more refined than this mini version: it uses a function-pointer table instead of virtual functions to save the vtable cost, tunes the SBO buffer size and criteria carefully, and `any_cast` handles references, pointers, and more overloads. But the skeleton is what we wrote. When you later read `std::function` holding any callable, `std::shared_ptr` with a custom deleter, or `std::move_only_function` holding a move-only callback, you'll find they all use the same type-erasure skeleton: a uniform outer interface, a templated inner derived class remembering the concrete type. This piece closes the vol2 sub-volume by welding these tools into a working little thing, putting Modern C++'s "play games on types" line of thinking into practice.
