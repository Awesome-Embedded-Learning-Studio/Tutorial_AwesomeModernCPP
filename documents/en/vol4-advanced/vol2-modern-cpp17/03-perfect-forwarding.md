---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'That T&& inside a template is actually a forwarding reference: it binds lvalues and rvalues alike, which makes it a very different animal from an ordinary rvalue reference. This piece works through forwarding references, the four reference-collapsing rules, the conditional cast inside std::forward, and the boundary between std::move and std::forward'
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
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/03-perfect-forwarding.md
  source_hash: bb3b0f0b7336867f2c312f2115f15028fb3ece204c3636ab9234fa366310d634
  translated_at: '2026-09-26T03:10:16+00:00'
  engine: anthropic
  token_count: 7000
---
# Perfect Forwarding: Forwarding References and Reference Collapsing

Last time we saw how `if constexpr` picks a branch at compile time and flattens a pile of overloads. That piece handled "different implementations for different types, inside one template". This one turns to a different need: in generic code, passing arguments along toward the inside exactly as they arrived. When we write a `make_unique`, or a transparent wrapper, what we want is that an lvalue received by the outer layer goes in as an lvalue, an rvalue goes in as an rvalue, and no step in between quietly changes the value category. It looks simple, but a mechanism called "perfect forwarding" is holding it up underneath, and its two cornerstones are remarkably easy to misread.

Those two cornerstones are the **forwarding reference** and **reference collapsing**. Plenty of tutorials flat-out call `T&&` an "rvalue reference", and that claim is wrong inside a template—it will mislead you about the intent of an entire family of generic code. So in this piece we take it apart: why `T&&` is not an rvalue reference, what the four reference-collapsing rules look like, what `std::forward` is actually forwarding, and where its boundary with `std::move` lies in generic code.

## `T&&` Looks Like an Rvalue Reference, but It Isn't

Start with a minimal example. This template function declares its parameter as `T&&`:

```cpp
template <typename T>
void show(T&& x);
```

Is this `T&&` an rvalue reference? It looks exactly like `int&&`. Let's pass in an lvalue, an rvalue, and a `std::move`d value, and see what `T` actually deduces to:

```cpp
template <typename T>
void show(T&& x) {
    std::cout << "  T 是否左值引用:" << std::is_lvalue_reference_v<T>
              << "  T 是否右值引用:" << std::is_rvalue_reference_v<T> << "\n";
    using X = decltype(x);
    std::cout << "  x 是否左值引用:" << std::is_lvalue_reference_v<X>
              << "  x 是否右值引用:" << std::is_rvalue_reference_v<X> << "\n";
}

int a = 10;
show(a);              // pass an lvalue
show(20);             // pass an rvalue
show(std::move(a));   // pass an rvalue
```

<OnlineCompilerDemo allow-run
  title="A forwarding reference T&& binds lvalues and rvalues alike; what T deduces to follows the argument's value category"
  source-path="code/examples/vol4/vol2-modern-cpp17/forwarding_reference_deduce.cpp"
  description="With an lvalue argument, T deduces to int& and x has type int&; with an rvalue, T deduces to int and x has type int&&. The same T&& parameter binds two radically different things."
/>

Output:

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

See it clearly now: this `T&&` parameter accepts lvalues and accepts rvalues. Pass the lvalue `a`, and `T` is deduced as `int&` (an lvalue reference); pass the rvalue `20`, and `T` is deduced as `int` (a non-reference type). The same syntax `T&&` binds two radically different things, and that is its most fundamental difference from an rvalue reference.

A genuine rvalue reference `int&&` binds rvalues only—try to bind an lvalue and you get a compile error. But `T&&` in a template is different: a special rule grants it the ability to swallow lvalues and rvalues alike. The C++ standard gives it a dedicated name, the **forwarding reference**; some people still follow Scott Meyers' older term and call it a **universal reference**. It is a forwarding reference only when two conditions hold: first, it appears in a context where template parameter deduction is happening; second, the parameter's form must be exactly `T&&` (or `auto&&`), where `T` is the template parameter being deduced in that context. Stray from that form even slightly and it stops being a forwarding reference—we'll see a counterexample in the next section.

So how does `T&&` manage to accept both? Through reference collapsing.

## Reference Collapsing: Four Rules That Let It Bind Everything

Once deduction yields `T = int&`, substituting `T` back into the parameter `T&&` gives you the notation `int& &&`. A "reference to a reference" may not be written directly in C++ (`int& && x` is a syntax error), but template deduction and a few other specific contexts do **produce** such combinations, and the standard uses a set of rules called reference collapsing to absorb them. There are four rules in total:

| Combination | Collapsed result |
|---|---|
| `T& &`    | `T&`  |
| `T& &&`   | `T&`  |
| `T&& &`   | `T&`  |
| `T&& &&`  | `T&&` |

The mnemonic is a single sentence: **if either one is an lvalue reference (`&`), the result is an lvalue reference; only when both are rvalue references (`&&`) is the result an rvalue reference**.

Look back at the deduction above. Pass the lvalue `a`: `T` deduces to `int&`, the parameter `T&&` is then `int& &&`, which the table collapses to `int&`—so `x` is an lvalue reference and `is_lvalue_reference_v<decltype(x)>` is 1. Pass the rvalue `20`: `T` deduces to `int`, the parameter `T&&` is then `int&&` (nothing to collapse), `x` is an rvalue reference, and `is_rvalue_reference_v<decltype(x)>` is 1. Let's run a minimal snippet that walks all four collapsing combinations, to confirm the rows in that table are real:

```cpp
template <typename T> using lref = T&;
template <typename T> using rref = T&&;

// Results of is_lvalue_reference_v / is_rvalue_reference_v
// lref<int&>   -> lvalue_ref=1 rvalue_ref=0   (int& &  -> int&)
// rref<int&>   -> lvalue_ref=1 rvalue_ref=0   (int& && -> int&)
// lref<int&&>  -> lvalue_ref=1 rvalue_ref=0   (int&& & -> int&)
// rref<int&&>  -> lvalue_ref=0 rvalue_ref=1   (int&& &&-> int&&)
```

The measured output matches the table exactly: of the four collapses, only "rvalue reference on rvalue reference" keeps its rvalue identity; the other three all collapse to an lvalue reference. Reference collapsing doesn't serve forwarding references alone—it also fires in `typedef`/`using`, `decltype`, and `auto&&`, making it a rule that runs through the entire C++ type system. That "accepts lvalues and rvalues alike" ability of the forwarding reference is, at bottom, achieved by these two steps together: deduction plus collapsing.

::: warning Don't Mistake `vector<T>&&` for a Forwarding Reference
The form of a forwarding reference is demanded strictly: it must be written precisely as "the template parameter currently being deduced, directly followed by `&&`". The parameter below contains both a `T` and an `&&`, yet it is not a forwarding reference:

```cpp
template <typename T>
void takes_vec_rvalue(std::vector<T>&& v);
```

Here `T` is being deduced for `vector<T>`; the parameter itself is already fixed as an rvalue reference to `vector<T>`, not the `T&&` form. Pass in an lvalue vector and the compiler refuses it outright:

```text
error: cannot bind rvalue reference of type 'std::vector<int>&&' to lvalue 'std::vector<int>'
```

The test is a single question: is the parameter exactly of the `T&&` or `auto&&` form—a bare, currently-being-deduced parameter plus `&&`? Vary the form even slightly (`vector<T>&&`, `const T&&`, `T& &&`) and it degenerates back into an ordinary rvalue reference that lvalues cannot bind to.
:::

## What `std::forward` Does: Recovering the Value Category

At this point we have a `T&&` parameter that accepts lvalues and rvalues alike, but that alone is not enough. The name `x` is itself an lvalue (any named variable is an lvalue, even when its type is an rvalue reference), so if you pass it directly to the next layer of functions, that layer receives an lvalue and the rvalue overload is never reached. We need a tool that, based on what `T` deduced to, **conditionally** restores `x` to its original value category. That tool is `std::forward<T>(x)`.

Its job comes down to two lines:

- When `T` is deduced as an lvalue reference (`int&`), `std::forward<T>(x)` returns an lvalue reference;
- When `T` is not a reference (`int`), `std::forward<T>(x)` returns an rvalue reference.

In other words, `std::forward` uses the `T` deduced earlier to restore the value category that the rule "a named variable is an lvalue" swallowed away from `x`. Let's write a transparent forwarder and put it next to the callee's lvalue/rvalue overloads, to see whether the value category really travels all the way down:

```cpp
void target(std::string& s)  { std::cout << "  [target] 命中左值重载:" << s << "\n"; }
void target(std::string&& s) { std::cout << "  [target] 命中右值重载:" << s << "\n"; }

template <typename T>
void wrap(T&& x) {
    target(std::forward<T>(x));
}

std::string s = "hello";
wrap(s);                 // lvalue: expect the lvalue overload
wrap(std::string("world")); // rvalue: expect the rvalue overload
wrap(std::move(s));      // rvalue
```

<OnlineCompilerDemo allow-run
  title="wrap forwards with std::forward, and target's lvalue/rvalue overloads are both hit correctly"
  source-path="code/examples/vol4/vol2-modern-cpp17/wrap_with_forward.cpp"
  description="With an lvalue, T=int& and forward returns an lvalue reference, hitting the lvalue overload; with an rvalue, T=string and forward returns an rvalue reference, hitting the rvalue overload. The value category is preserved the whole way down."
/>

Output:

```text
wrap(s)                传左值:
  [target] 命中左值重载:hello
wrap(string("world")) 传右值:
  [target] 命中右值重载:world
wrap(std::move(s))     传右值:
  [target] 命中右值重载:hello
```

When we pass the lvalue `s`, `T` deduces to `std::string&`, `std::forward<std::string&>(x)` takes the first path and returns an lvalue reference, and `target` hits its lvalue overload; when we pass the rvalue `std::string("world")`, `T` deduces to `std::string`, `std::forward<std::string>(x)` takes the second path and returns an rvalue reference, and `target` hits its rvalue overload. That is what the words "perfect forwarding" mean: **whatever value category the outer layer receives is exactly what the inner layer gets, with not one step in between changed**.

This mechanism's most everyday stage is generic factories and transparent wrappers. `std::make_unique<T>(args...)` accepts any number of arguments and hands them, untouched, to `T`'s constructor—supporting both copy construction (when an argument is an lvalue) and move construction (when it is an rvalue)—by pairing a parameter pack with `std::forward`. We will meet it again when we get to parameter-pack expansion in the next piece.

## `std::forward` vs `std::move`: Don't Forward with move in Generic Code

There is a boundary we must make crisp here, and it is the pitfall newcomers step into most easily when writing generic code. Both `std::move` and `std::forward` can produce rvalues, but their semantics are entirely different.

`std::move(x)` is an **unconditional** cast: no matter whether `x` was originally an lvalue or an rvalue, it is turned into an rvalue reference. You use it when you positively know "I am about to move this object out"—say, the variable in your hands is about to leave its scope, or you simply want to trigger the move constructor. `std::forward<T>(x)` is a **conditional** cast: it converts to an rvalue only when `T` is not a reference; otherwise the value stays an lvalue. You use it for generic forwarding, passing the value category along unchanged.

And what happens if, inside a generic forwarder, you write `std::move(x)` instead of `std::forward<T>(x)`? Let's build a comparison around a `Box` class that owns heap memory and empties itself once moved, so we can see at a glance whether an object got carried off:

```cpp
template <typename T>
void wrap_move(T&& x)    { consume(std::move(x)); }        // unconditional cast to rvalue
template <typename T>
void wrap_forward(T&& x) { consume(std::forward<T>(x)); }  // conditional forwarding

Box a("hello");
wrap_forward(a);   // lvalue: forward keeps it an lvalue, consume takes the lvalue overload, no move
std::cout << a.raw();   // still "hello"

Box b("hello");
wrap_move(b);      // lvalue: move wrongly casts it to an rvalue, consume takes the rvalue overload and empties b
std::cout << b.raw();   // becomes "<空>"
```

<OnlineCompilerDemo allow-run
  title="Forwarding wrongly with std::move empties an lvalue, while std::forward keeps it intact"
  source-path="code/examples/vol4/vol2-modern-cpp17/move_vs_forward.cpp"
  description="With the same lvalue argument, wrap_forward takes the lvalue overload and the object stays intact; wrap_move unconditionally casts to an rvalue and the object is emptied. That is the destructiveness of forwarding with move in generic code."
/>

Output:

```text
--- std::forward 转发左值(正确)---
  [consume] 命中左值重载(没搬走):hello
  调用方 a.raw() = hello (完好)

--- std::move 错误转发左值(破坏)---
  [consume] 右值,搬走后源对象="<空>"
  调用方 b.raw() = <空> (被搬空!)
```

The difference is visible to the naked eye. Forward the same lvalue argument with `std::forward`, and the caller still holds an intact `"hello"`; forward it with `std::move`, and the object in the caller's hands has been quietly emptied. In non-generic code, writing `consume(std::move(local))` announces "I know local is done for"—that is legal and useful. But in generic forwarding you have no idea whether the caller passed in an lvalue or an rvalue; carrying an lvalue off as if it were an rvalue amounts to modifying the caller's object without their knowledge. That is a silent bug, and a uniquely painful one to track down.

The rule of thumb in one sentence: **forward with `std::forward`; use `std::move` on local objects you already know you are moving out**. The two look alike and both return rvalue references, but their semantics and their proper use cases do not overlap at all.

## Forwarding References + Overloading: A Disaster

Once you understand how "greedy" the forwarding reference is, look at a classic trap. Many people write a dedicated overload for some concrete type, then casually add a `T&&` fallback version on top, assuming the concrete-type overload will be preferred. What actually happens is that the forwarding reference crowds out the overload that should have played the lead.

Here is a minimal example. `Widget` gets three sets of overloads: a const lvalue reference to take lvalues, `Widget&&` to take rvalues, and a forwarding reference to catch every other type:

```cpp
struct Widget { int v; };

void tag(const Widget&) { std::cout << "  命中 const Widget& 重载\n"; }
void tag(Widget&&)      { std::cout << "  命中 Widget&& 重载\n"; }
template <typename T>
void tag(T&&) {
    std::cout << "  命中 T&& 转发引用:" << __PRETTY_FUNCTION__ << "\n";
}

Widget w{1};
tag(w);            // lvalue: intuition says const Widget&
tag(Widget{2});    // rvalue
```

<OnlineCompilerDemo allow-run
  title="A forwarding reference beats const Widget& in overload resolution and steals the lvalue argument"
  source-path="code/examples/vol4/vol2-modern-cpp17/overload_gotcha.cpp"
  description="When an lvalue Widget is passed, T&& deduces to Widget&—an exact match that doesn't even need the const qualification that const Widget& would require—so the forwarding reference wins overload resolution and crowds out the const&. For an rvalue, the non-template Widget&& is preferred."
/>

Output:

```text
tag(w)        传左值(直觉该走 const Widget&):
  命中 T&& 转发引用:void tag(T&&) [with T = Widget&]
tag(Widget{2}) 传右值:
  命中 Widget&& 重载
```

When we pass the lvalue `w`, `T&&` deduces to `Widget&`, which collapses into an lvalue reference—an exact match for binding to `w`; `const Widget&`, by contrast, has to add a const qualification to bind to the non-const `w` (that added const is usually free, but overload resolution judges the formal quality of the match). With both being exact matches, the template version with the forwarding reference is better on certain key dimensions, and as a result it crowds out `const Widget&`—the lvalue that should have gone to the specialized overload falls into the fallback version. When we pass the rvalue `Widget{2}`, `Widget&&` is a non-template exact match, which takes priority over the template version, so that path still behaves normally.

Such is the greed of the forwarding reference: **its pull on lvalue arguments far exceeds intuition**. Scott Meyers devoted an entire item to this trap in *Effective Modern C++* (Item 26). Its most dangerous form is the "perfect forwarding constructor": the moment a class writes a forwarding constructor accepting `T&&`, it overshadows the copy/move constructors the compiler would otherwise have generated, producing a pile of baffling compile errors.

How do you steer clear? A few practical rules:

1. **Don't let the forwarding reference coexist with the overload you mean to specialize**. Either keep only the forwarding reference and dispatch inside with `if constexpr` or `tag_dispatch`; or skip the forwarding reference entirely and write a set of named overloads.
2. **Separate them with const T&**. Change the fallback version to `const T&` and it is no longer a forwarding reference—the greed disappears. The cost is one extra copy for rvalues, which is acceptable for lightweight types.
3. **Constrain it with concepts**. Since C++20, a requires clause such as `template <typename T> requires (!std::same_as<std::decay_t<T>, Widget>) void tag(T&&)` can fence the forwarding reference off from concrete types, keeping the forwarding ability without letting it steal the lead.

## `auto&&` Is a Forwarding Reference Too

Finally, let's knock out `auto&&` as well—it is the forwarding reference's major escape into non-template contexts. The deduction that `auto&&` triggers is identical to `T&&`: `auto` occupies the position of the template parameter `T`, so `auto&&` likewise accepts lvalues and rvalues alike, and likewise goes through reference collapsing.

```cpp
int a = 1;
auto&& r1 = a;              // a is an lvalue, auto=int&, r1's type int& && -> int&
auto&& r2 = std::move(a);   // rvalue, auto=int, r2's type int&&
auto&& r3 = 42;             // rvalue, auto=int, r3's type int&&
```

Run it and `r1` comes out an lvalue reference while `r2` and `r3` come out rvalue references—same as the template version. The most common uses of `auto&&` are capturing arguments of any value category in range-based for loops and in generic lambdas: write a generic lambda `[](auto&& x){ ... }` and `x` is a forwarding reference; pass `std::forward<decltype(x)>(x)` to the next layer and the value category survives the whole way down. Inside the standard library, the implementations of `std::bind` and `std::invoke`, along with all manner of "perfect-forwarding invokers", rely on this same combination of `auto&&` plus `std::forward<decltype(...)>(...)`.

## The Limits of Perfect Forwarding: Not a 100% Guarantee

After all this talk of "perfect", we should spell out its limits, lest you take it for a universal tool. There are a few kinds of arguments that perfect forwarding simply cannot forward:

The first kind is the **braced-init-list**. `{1, 2, 3}` has no type of its own, and template argument deduction cannot deduce it, so a forwarder handed `{1, 2, 3}` fails outright:

```cpp
template <typename T, typename... Args>
void relay(Args&&... args) { auto p = std::make_unique<T>(std::forward<Args>(args)...); }

relay<std::vector<int>>({1, 2, 3});   // compile error: {1,2,3} cannot deduce Args
```

GCC's message is `too many arguments to function ... Args = {}`, which means that braced bundle never took part in deduction at all and `Args` deduced to an empty pack. The fix is to build the temporary `std::vector<int>{1,2,3}` in the outer layer first and pass it in as an rvalue, rather than expecting the forwarder to construct it for you.

The second kind is **the integer 0 standing in for a pointer**. You mean to pass a null pointer to some function accepting `char*`, so you write `0` or `NULL`—and template deduction deduces the `0` as `int`, not as a pointer type:

```cpp
auto lam = [](auto&& x) { using T = std::decay_t<decltype(x)>; /* see what T is */ };
lam(0);    // T deduces to int, not char*/nullptr
```

Once it is deduced as `int`, forwarding it to a target accepting `char*` no longer type-checks—you either get an error or land on an unexpected overload. To pass a null pointer, write `nullptr` directly: its type is `std::nullptr_t`, and both deduction and overload resolution do the right thing.

The third kind is the **bitfield**. A bitfield member cannot bind to a non-const reference, while a forwarding reference often deduces to a non-const lvalue reference—so forwarding a bitfield member out fails to compile. This is a language-level restriction that even the standard library cannot route around.

Keep these three in mind, and you won't be left scratching your head when something that "obviously should have forwarded" refuses to compile. The "perfect" in perfect forwarding means zero loss at the **value-category** level, not "forwards anything". The roots of these failures all lie in the deduction stage, not in the forwarding mechanism itself.

In the next piece we weld perfect forwarding together with variadic templates. A forwarder that accepts any number of arguments, plus parameter-pack expansion, is the template machinery underneath interfaces like `std::make_unique` and `std::emplace_back`—and your everyday weapon for writing generic factories.
