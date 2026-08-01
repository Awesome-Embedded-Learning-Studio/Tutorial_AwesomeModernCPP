---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'In a template, T&& is actually a forwarding reference that binds to both lvalues and rvalues, a different animal from an ordinary rvalue reference. This piece works through forwarding references, the four reference-collapsing rules, the conditional cast inside std::forward, and the boundary between std::move and std::forward.'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Variadic Templates: Expanding Parameter Packs'
- 'Move Semantics and Rvalue References'
reading_time_minutes: 14
related:
- 'Variadic Templates: Expanding Parameter Packs'
- 'CTAD: Class Template Argument Deduction'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
- 泛型
- 模板
title: 'Perfect Forwarding: Forwarding References and Reference Collapsing'
---
# Perfect Forwarding: Forwarding References and Reference Collapsing

In the previous piece we saw how `if constexpr` picks a branch at compile time and flattens a pile of overloads. That piece handled "different implementations for different types, inside one template." This piece turns to a different need: passing arguments through, untouched, in generic code. When you write a `make_unique` or a transparent wrapper, you want the outer layer to forward whatever value category it received, an lvalue in producing an lvalue out and an rvalue in producing an rvalue out, without quietly changing it along the way. It looks simple, but a mechanism called "perfect forwarding" sits underneath it, and its two foundations are both easy to misread.

Those two foundations are the **forwarding reference** and **reference collapsing**. Many tutorials call `T&&` an "rvalue reference" flat out, and that description is wrong inside a template; it will mislead you about the intent of an entire class of generic code. This piece pulls it apart: why `T&&` is not an rvalue reference, what the four collapsing rules look like, what `std::forward` is actually casting, and the boundary it draws against `std::move` in generic code.

## `T&&` looks like an rvalue reference, but it isn't

Start with the smallest possible example. Here's a template function whose parameter is written `T&&`:

```cpp
template <typename T>
void show(T&& x);
```

Is this `T&&` an rvalue reference? It looks identical to `int&&`. Let's pass an lvalue, an rvalue, and a `std::move`'d value into it and see what `T` actually deduces to:

```cpp
template <typename T>
void show(T&& x) {
    std::cout << "  T is lvalue ref:" << std::is_lvalue_reference_v<T>
              << "  T is rvalue ref:" << std::is_rvalue_reference_v<T> << "\n";
    using X = decltype(x);
    std::cout << "  x is lvalue ref:" << std::is_lvalue_reference_v<X>
              << "  x is rvalue ref:" << std::is_rvalue_reference_v<X> << "\n";
}

int a = 10;
show(a);              // pass an lvalue
show(20);             // pass an rvalue
show(std::move(a));   // pass an rvalue
```

<OnlineCompilerDemo allow-run
  title="A forwarding reference T&& binds both lvalues and rvalues; T's deduced type shifts with the argument's value category"
  source-path="code/examples/vol4/vol2-modern-cpp17/forwarding_reference_deduce.cpp"
  description="Passing an lvalue deduces T as int& with x of type int&; passing an rvalue deduces T as int with x of type int&&. The same T&& parameter binds two very different things."
/>

Run it:

```text
传左值 a:
  T 是否左值引用:1  T 是否右值引用:0
  x 是否左值引用:1  x 是否右值引用:0
传右值 20:
  T 是否左值引用:0  T 是否右值引用:0
  x 是否左值引用:0  x 是否右值引用:1
传 std::move(a):
  T 是否左值引用:0  T 是否右值引用:0
  x 是否左值引用:0  x 是否右值引用:1
```

Look carefully: this `T&&` parameter accepts an lvalue and an rvalue alike. Pass the lvalue `a`, and `T` deduces to `int&` (an lvalue reference); pass the rvalue `20`, and `T` deduces to `int` (a non-reference type). The same `T&&` syntax binds two very different things, and that is the fundamental difference between it and an rvalue reference.

A real rvalue reference `int&&` binds only rvalues; trying to bind an lvalue is a compile error. But `T&&` in a template is different. A special rule grants it a "binds to anything" power. The standard gives it a specific name: a **forwarding reference** (Scott Meyers' older term **universal reference** is also still in circulation). Two conditions must hold for it to be a forwarding reference: it has to appear in a template-argument-deduction context, and the parameter's form must be exactly `T&&` (or `auto&&`), where `T` is the template parameter being deduced in that context. Drift from that form even slightly and it stops being a forwarding reference; the next section shows a counterexample.

So how does `T&&` bind to anything? Through reference collapsing.

## Reference collapsing: four rules that produce "binds to anything"

Once `T` has been deduced as `int&`, substituting `T` back into the `T&&` parameter gives you `int& &&`. A "reference to a reference" is not legal syntax you can write directly (`int& && x` is a syntax error), but template deduction and a few specific contexts do *generate* such combinations, and the standard collapses them with a set of rules. Four rules in total:

| Combination | Collapses to |
|---|---|
| `T& &`    | `T&`  |
| `T& &&`   | `T&`  |
| `T&& &`   | `T&`  |
| `T&& &&`  | `T&&` |

The mnemonic is a single sentence: **if either operand is an lvalue reference (`&`), the result is an lvalue reference; only when both are rvalue references (`&&`) does the result stay an rvalue reference**.

Walk back through the deduction above. Pass the lvalue `a`: `T` deduces to `int&`, the parameter `T&&` is `int& &&`, which collapses to `int&`, so `x` is an lvalue reference and `is_lvalue_reference_v<decltype(x)>` is 1. Pass the rvalue `20`: `T` deduces to `int`, the parameter `T&&` is just `int&&` (nothing to collapse), `x` is an rvalue reference and `is_rvalue_reference_v<decltype(x)>` is 1. Let's run a tiny snippet that exercises all four collapsing combinations to confirm the table is real:

```cpp
template <typename T> using lref = T&;
template <typename T> using rref = T&&;

// is_lvalue_reference_v / is_rvalue_reference_v results:
// lref<int&>   -> lvalue_ref=1 rvalue_ref=0   (int& &  -> int&)
// rref<int&>   -> lvalue_ref=1 rvalue_ref=0   (int& && -> int&)
// lref<int&&>  -> lvalue_ref=1 rvalue_ref=0   (int&& & -> int&)
// rref<int&&>  -> lvalue_ref=0 rvalue_ref=1   (int&& &&-> int&&)
```

The measured output matches the table exactly: of the four collapsing combinations, only "rvalue-reference of rvalue-reference" preserves an rvalue identity; the other three all collapse into an lvalue reference. Reference collapsing isn't only for forwarding references. `typedef`/`using`, `decltype`, and `auto&&` all trigger it, and it's a rule that runs through the entire type system. The "binds to anything" power of a forwarding reference is, mechanically, deduction followed by collapsing.

::: warning Don't mistake `vector<T>&&` for a forwarding reference
The form requirement for a forwarding reference is strict: it must be the exact shape "the template parameter currently being deduced, directly followed by `&&`". The parameter below has a `T` and a `&&`, but it is not a forwarding reference:

```cpp
template <typename T>
void takes_vec_rvalue(std::vector<T>&& v);
```

Here `T` is being deduced for `vector<T>`; the parameter itself is already a fixed `vector<T>` rvalue reference, not the `T&&` shape. Pass an lvalue vector in and the compiler refuses outright:

```text
error: cannot bind rvalue reference of type 'std::vector<int>&&' to lvalue 'std::vector<int>'
```

The one-line test: is the parameter exactly the "bare, currently-being-deduced parameter plus `&&`" shape (`T&&` or `auto&&`)? Drift at all (`vector<T>&&`, `const T&&`, `T& &&`) and it reverts to an ordinary rvalue reference that won't accept lvalues.
:::

## What `std::forward` does: it restores the value category

Now we have a `T&&` parameter that binds to anything, but that alone isn't enough. The name `x` is itself an lvalue (any named variable is an lvalue, even when its type is an rvalue reference), so if you pass it straight down to the next layer, that layer receives an lvalue and the rvalue overload never gets picked. We need a tool that, based on what `T` deduced to, *conditionally* restores `x` to its original value category. That tool is `std::forward<T>(x)`.

It does exactly two things:

- When `T` deduced to an lvalue reference (`int&`), `std::forward<T>(x)` returns an lvalue reference;
- When `T` is not a reference (`int`), `std::forward<T>(x)` returns an rvalue reference.

In other words, `std::forward` looks at the `T` that was deduced earlier and recovers the value category that the "named variables are lvalues" rule had stripped off `x`. Let's write a transparent forwarder, pair it with the callee's lvalue/rvalue overloads, and check that the value category actually reaches the bottom:

```cpp
void target(std::string& s)  { std::cout << "  [target] lvalue overload:" << s << "\n"; }
void target(std::string&& s) { std::cout << "  [target] rvalue overload:" << s << "\n"; }

template <typename T>
void wrap(T&& x) {
    target(std::forward<T>(x));
}

std::string s = "hello";
wrap(s);                    // pass an lvalue, expect the lvalue overload
wrap(std::string("world")); // pass an rvalue, expect the rvalue overload
wrap(std::move(s));         // pass an rvalue
```

<OnlineCompilerDemo allow-run
  title="wrap forwards with std::forward and the target's lvalue/rvalue overloads are both hit correctly"
  source-path="code/examples/vol4/vol2-modern-cpp17/wrap_with_forward.cpp"
  description="Passing an lvalue deduces T=string&, forward returns an lvalue reference, the lvalue overload is hit; passing an rvalue deduces T=string, forward returns an rvalue reference, the rvalue overload is hit. The value category survives end to end."
/>

Run it:

```text
wrap(s)                传左值:
  [target] 命中左值重载:hello
wrap(string("world")) 传右值:
  [target] 命中右值重载:world
wrap(std::move(s))     传右值:
  [target] 命中右值重载:hello
```

Pass the lvalue `s`: `T` deduces to `std::string&`, `std::forward<std::string&>(x)` takes the first branch and returns an lvalue reference, and `target` hits the lvalue overload. Pass the rvalue `std::string("world")`: `T` deduces to `std::string`, `std::forward<std::string>(x)` takes the second branch and returns an rvalue reference, and `target` hits the rvalue overload. That is what the words "perfect forwarding" mean: **whatever value category the outer layer received, the inner layer gets, with nothing changed in between**.

The most everyday stage for this mechanism is generic factories and transparent wrappers. `std::make_unique<T>(args...)` takes any number of arguments and forwards them untouched to `T`'s constructor. It has to support copy construction (when an argument is an lvalue) and move construction (when an argument is an rvalue), and parameter packs plus `std::forward` are how it does it. We'll see it again in the next piece when we expand parameter packs.

## `std::forward` vs `std::move`: don't use move to forward in generic code

There's a boundary here that has to be stated plainly, because it's the trap new generic code falls into most often. `std::move` and `std::forward` both produce rvalues, but their semantics are entirely different.

`std::move(x)` is an *unconditional* cast: regardless of whether `x` was originally an lvalue or an rvalue, it casts to an rvalue reference. You use it when you know "I am about to move out of this object" — the variable is about to leave scope, or you specifically want to trigger the move constructor. `std::forward<T>(x)` is a *conditional* cast: it casts to an rvalue only when `T` is not a reference, and otherwise preserves an lvalue. You use it for generic forwarding, to carry the value category through.

What happens if you write `std::move(x)` instead of `std::forward<T>(x)` inside a generic forwarder? Let's set up a side-by-side comparison with a `Box` class that owns heap memory and clears itself on move, so we can see directly whether the object was moved out of:

```cpp
template <typename T>
void wrap_move(T&& x)    { consume(std::move(x)); }        // unconditional cast to rvalue
template <typename T>
void wrap_forward(T&& x) { consume(std::forward<T>(x)); }  // conditional forward

Box a("hello");
wrap_forward(a);   // lvalue:forward keeps the lvalue, consume hits the lvalue overload, no move
std::cout << a.raw();   // still "hello"

Box b("hello");
wrap_move(b);      // lvalue:move wrongly casts to an rvalue, consume hits the rvalue overload and moves b out
std::cout << b.raw();   // becomes "<空>"
```

<OnlineCompilerDemo allow-run
  title="std::move wrongly forwards an lvalue and empties it; std::forward keeps the lvalue intact"
  source-path="code/examples/vol4/vol2-modern-cpp17/move_vs_forward.cpp"
  description="Same lvalue argument: wrap_forward hits the lvalue overload and the object stays intact; wrap_move unconditionally casts to an rvalue and the object is emptied. That's the damage of using move to forward in generic code."
/>

Run it:

```text
--- std::forward 转发左值(正确)---
  [consume] 命中左值重载(没搬走):hello
  调用方 a.raw() = hello (完好)

--- std::move 错误转发左值(破坏)---
  [consume] 右值,搬走后源对象="<空>"
  调用方 b.raw() = <空> (被搬空!)
```

The difference is visible. The same lvalue argument, forwarded with `std::forward`, leaves the caller with an intact `"hello"`; forwarded with `std::move`, the caller's object gets quietly emptied. In non-generic code, writing `consume(std::move(local))` is you declaring "I know I don't need local anymore," which is legal and useful. In a generic forwarder, you don't know whether the caller passed an lvalue or an rvalue, and treating an lvalue as an rvalue means moving out of the caller's object behind their back. That's a silent bug, and a painful one to track down.

One-line guideline: **use `std::forward` to forward; use `std::move` on local objects you already know you want to move out of**. They look alike and both return rvalue references, but their semantics and their applicable contexts don't overlap.

## The disaster of forwarding references plus overloads

Once you've seen the "greed" of a forwarding reference, the classic trap is easier to read. Plenty of people write a specialized overload for some concrete type, then add a `T&&` catch-all beside it, expecting the concrete overload to be picked first. What actually happens is the forwarding reference elbows out the overload that was supposed to be the lead.

A minimal example. Three sets of overloads for `Widget`: a const lvalue reference for lvalues, a `Widget&&` for rvalues, and a forwarding reference to catch everything else:

```cpp
struct Widget { int v; };

void tag(const Widget&) { std::cout << "  hit const Widget& overload\n"; }
void tag(Widget&&)      { std::cout << "  hit Widget&& overload\n"; }
template <typename T>
void tag(T&&) {
    std::cout << "  hit T&& forwarding reference:" << __PRETTY_FUNCTION__ << "\n";
}

Widget w{1};
tag(w);            // pass an lvalue, intuitively the const Widget& should be picked
tag(Widget{2});    // pass an rvalue
```

<OnlineCompilerDemo allow-run
  title="A forwarding reference beats const Widget& in overload resolution and grabs the lvalue argument"
  source-path="code/examples/vol4/vol2-modern-cpp17/overload_gotcha.cpp"
  description="Passing an lvalue Widget deduces T as Widget&, which is an exact match for w and doesn't even need to add const. The forwarding reference wins out over the const& overload. For an rvalue the non-template Widget&& wins."
/>

Run it:

```text
tag(w)        传左值(直觉该走 const Widget&):
  命中 T&& 转发引用:void tag(T&&) [with T = Widget&]
tag(Widget{2}) 传右值:
  命中 Widget&& 重载
```

Pass the lvalue `w`: `T&&` deduces to `Widget&`, which after collapsing is an lvalue reference, an exact match for `w`; the `const Widget&` overload would need an extra const qualification to bind to a non-const `w` (the const itself is usually free, but overload resolution looks at the formal match level). Both are exact matches, and on certain key tie-breakers the template version comes out ahead, so it elbows out `const Widget&` and the lvalue that was supposed to hit the specialized overload lands in the catch-all instead. Pass the rvalue `Widget{2}` and the non-template `Widget&&` is an exact match that beats the template, so that path still works.

That's the greed of a forwarding reference: **its pull on lvalue arguments is far stronger than intuition suggests**. Scott Meyers devotes an item to this trap in *Effective Modern C++* (Item 26), and its most dangerous form is the "perfect-forwarding constructor": once a class writes a forwarding constructor taking `T&&`, it can shadow the copy/move constructors the compiler would have generated, producing a pile of confusing errors.

How do you avoid it? A few practical options:

1. **Don't let a forwarding reference and an overload you meant to specialize coexist.** Either keep only the forwarding reference and dispatch inside with `if constexpr` or tag dispatch, or skip the forwarding reference entirely and use a set of named overloads.
2. **Insulate with `const T&`.** Change the catch-all to `const T&` and it stops being a forwarding reference; the greed vanishes. The cost is one extra copy for rvalues, which is often acceptable for lightweight types.
3. **Constrain with a concept.** From C++20, a requires clause like `template <typename T> requires (!std::same_as<std::decay_t<T>, Widget>) void tag(T&&)` keeps the forwarding reference away from the concrete type, preserving forwarding without stealing the lead.

## `auto&&` is also a forwarding reference

One last thing, to cover the important non-template exit for forwarding references: `auto&&`. The deduction `auto&&` triggers is identical to `T&&` — `auto` fills the slot the template parameter `T` would — so `auto&&` also binds to anything and also goes through reference collapsing.

```cpp
int a = 1;
auto&& r1 = a;              // a is an lvalue, auto=int&, r1's type is int& && -> int&
auto&& r2 = std::move(a);   // rvalue, auto=int, r2's type is int&&
auto&& r3 = 42;             // rvalue, auto=int, r3's type is int&&
```

Run it and `r1` is an lvalue reference, `r2` and `r3` are rvalue references, matching the template version. The most common use of `auto&&` is in range-based for loops and in generic lambdas that want to capture an argument of any value category: a `[](auto&& x){ ... }` generic lambda has `x` as a forwarding reference, and forwarding it down with `std::forward<decltype(x)>(x)` carries the value category end to end. The internal implementations of `std::bind` and `std::invoke`, and most "perfect-forwarding caller" wrappers, all rely on this `auto&&` + `std::forward<decltype(...)>(...)` pattern.

## The boundary of perfect forwarding: it isn't 100%

We've said "perfect" a lot, so it's worth stating the boundary, so you don't reach for it as a universal tool. There are a few categories of arguments that perfect forwarding cannot forward.

The first is the **braced-init-list**. `{1, 2, 3}` has no type on its own; template argument deduction cannot deduce it, so a forwarder receiving `{1, 2, 3}` fails outright:

```cpp
template <typename T, typename... Args>
void relay(Args&&... args) { auto p = std::make_unique<T>(std::forward<Args>(args)...); }

relay<std::vector<int>>({1, 2, 3});   // compile error:{1,2,3} can't deduce Args
```

GCC reports `too many arguments to function ... Args = {}` — the braced contents simply didn't participate in deduction, and `Args` came out as an empty pack. The fix is to build the temporary `std::vector<int>{1,2,3}` outside and pass that object as an rvalue, rather than asking the forwarder to construct it for you.

The second is **integer `0` used as a pointer**. You mean to pass a null pointer to some function taking `char*`, you write `0` or `NULL`, and template deduction deduces `0` as `int` rather than as a pointer type:

```cpp
auto lam = [](auto&& x) { using T = std::decay_t<decltype(x)>; /* what is T? */ };
lam(0);    // T deduces to int, not char*/nullptr
```

Once deduced as `int`, forwarding it to a target expecting `char*` doesn't match — either it errors out or it lands on an unexpected overload. To pass a null pointer, write `nullptr` directly; its type is `std::nullptr_t`, and both deduction and overload resolution behave correctly.

The third is **bitfields**. A bitfield member cannot bind to a non-const reference, and a forwarding reference frequently deduces to a non-const lvalue reference, so forwarding a bitfield member fails to compile. This is a language-level restriction that the standard library can't route around either.

Keep these three in mind, and when something "should forward but won't compile" you won't be left scratching your head. The "perfect" in perfect forwarding refers to **zero loss in value category**, not "forwards anything." The root of all three failures is in the deduction phase, not in the forwarding mechanism itself.

In the next piece we weld perfect forwarding together with variadic templates. A forwarder that takes any number of arguments, combined with parameter-pack expansion, is the template underneath interfaces like `std::make_unique` and `std::emplace_back`, and your everyday tool for writing generic factories.
