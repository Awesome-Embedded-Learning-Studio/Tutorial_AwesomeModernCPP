---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: Name lookup inside templates works nothing like ordinary code. It happens
  in two phases. This piece covers two-phase lookup, dependent vs non-dependent names,
  ADL (argument-dependent lookup), and why the typename, this->, and hidden-friends
  rules from earlier pieces have to exist.
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
- 'Non-Type Template Parameters: From Integers to C++20 Floats and Class Types'
reading_time_minutes: 9
related:
- 'Template Friends and Barton-Nackman: The Hidden Friends Trick'
- 'Template Specialization and Partial Specialization: The Art of Pattern Matching'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'Name Lookup and ADL: How Two-Phase Lookup Works'
---
# Name Lookup and ADL: How Two-Phase Lookup Works

In ordinary code, when you use a name, the compiler looks it up at the current position, finds it, and uses it. In templates, that stops working. Name lookup in templates happens in two phases, one at the template definition and one at instantiation. This mechanism is called two-phase lookup, and it is exactly what explains the seemingly strange rules from earlier pieces: why `typename` cannot be dropped, why `this->` is mandatory, and why hidden friends matter. This piece takes two-phase lookup, dependent and non-dependent names, and ADL all the way through. After it, the "inexplicable" errors you hit in templates will all trace back to a root cause you can name.

## Name Lookup in Ordinary Code: All at Once

Start with ordinary functions. In an ordinary function, when you use a name, the compiler does ordinary lookup at the definition point, walking outward through namespaces until it finds a candidate set and stops.

```cpp
void helper(int) {}

void caller() {
    helper(42);   // the compiler looks up helper at caller's definition point and finds ::helper(int)
}
```

This is intuitive. Ordinary lookup only sees names visible "before the definition point." Same-name functions declared later do not count.

## Templates Are Different: Two Phases

Templates split this into two phases.

**Phase one** (template definition): when the compiler parses the template definition, it looks up and binds all **non-dependent names**. At this point `T` is unknown, so names that depend on `T` cannot be looked up yet, and they are set aside.

**Phase two** (template instantiation): when the template is instantiated with a concrete type, `T` is fixed, and the compiler looks up **dependent names**. The main tool at this phase is ADL (argument-dependent lookup).

Why two phases? Because at the template definition, the compiler has no idea what `T` will be. It cannot know whether `T::value_type` is a type or a variable, or which namespace `foo` in `foo(t)` should be searched. So it resolves what it can (non-dependent names) now, and defers what it cannot (dependent names) to instantiation.

Here is a classic example that shows two-phase lookup in action.

```cpp
#include <iostream>

// helper exists before the template definition
void helper(int) { std::cout << "::helper(int), defined before template\n"; }

template <typename T>
void call_it(T x) {
    helper(x);   // helper is non-dependent, looked up and bound in phase one (definition point)
}

// helper added after the template definition
void helper(double) { std::cout << "::helper(double), defined after template\n"; }

int main() {
    call_it(3.14);   // T=double. Intuitively, would you expect helper(double)?
    return 0;
}
```

Run it.

```bash
$ g++ -Wall -Wextra -std=c++20 twophase.cpp -o twophase && ./twophase
::helper(int), defined before template
```

`call_it(3.14)` actually goes to `helper(int)`, not to `helper(double)` defined later. The reason is two-phase lookup. The name `helper` is non-dependent (it carries no `T`), so it is looked up and bound in phase one, at the template definition, to the only `helper` visible then, `helper(int)`. The `helper(double)` defined after the template is invisible in phase one. Phase two only does ADL for dependent names, and `double` is a builtin with no associated namespace, so ADL adds nothing. So `helper(x)` is bound to `helper(int)` for good, and `3.14` is truncated to `3`.

Note that there is actually no difference from an ordinary function here: non-dependent names are looked up at the "definition point" regardless of whether `call_it` is a template or an ordinary function. As long as it is defined before `helper(double)`, it can only see `helper(int)`. Where two-phase lookup genuinely diverges from ordinary functions is the second phase for dependent names — in the next section, on ADL, we will see templates use the argument type at the instantiation point to find functions the definition point cannot see at all. That is two-phase lookup's real specialty.

## Dependent vs Non-dependent Names

This line decides which phase a name is looked up in, and deserves emphasis.

A non-dependent name carries no template parameter. Things like `helper`, `std::cout`, `int`. They are looked up in phase one.

A dependent name depends on some template parameter, directly or indirectly. Things like `T::value_type`, `x.foo()` (when the type of `x` is `T`), `foo(t)` (when the type of `t` depends on `T`). They are looked up in phase two, mainly via ADL.

Whether a call `foo(t)` is dependent depends on whether the type of `t` depends on `T`. If `t` is of type `T`, then `foo(t)` is a dependent call, and phase two will look for `foo` in the namespace where `T` lives via ADL. That is the entry point where ADL does its work.

## typename and this->: Direct Consequences of Two-Phase Lookup

Looking back at two rules from earlier pieces, both are direct consequences of two-phase lookup.

The `typename` disambiguation. For a dependent name like `T::value_type`, the compiler does not know what `T` is at phase one, so it cannot assume `value_type` is a type. It defaults to treating a dependent qualified name as a variable or function, unless you use `typename` to say explicitly "this is a type." This is the inevitable result of phase one being unable to resolve it at the definition point.

`this->` for dependent-base members. When the base is `Base<T>`, its members are invisible at phase one too (because the shape of `Base<T>` depends on `T`). The compiler does not look up non-dependent names inside a dependent base at phase one, so a bare `helper()` cannot find the base's `helper`, and you need `this->helper()` to push the lookup to phase two (at instantiation, the type of `this` is known, and the base members are visible).

Once you see that both rules come from two-phase lookup, they stop feeling like "pointless syntax." They are the mechanism that keeps template behavior predictable. Without two-phase lookup, lookup results would swing wildly with the context at the instantiation point, and nobody could write a reliable template library.

## ADL: Argument-Dependent Lookup

ADL (argument-dependent lookup) is the core tool phase two uses to look up dependent names. The rule is plain: **when calling a function, besides ordinary lookup, the compiler also looks for candidates in the namespaces of the argument types.**

```cpp
#include <iostream>

namespace geo {
    struct Point { int x; int y; };
    void draw(const Point&) { std::cout << "geo::draw(Point)\n"; }
}

int main() {
    geo::Point p{1, 2};
    draw(p);   // no geo:: qualifier, no using namespace geo
    return 0;
}
```

Run it.

```bash
$ g++ -Wall -Wextra -std=c++20 adl.cpp -o adl && ./adl
geo::draw(Point)
```

`main` calls `draw(p)` with no `geo::` qualifier and no `using namespace geo`. Ordinary lookup does not find `draw` in the global namespace, so this should error. But ADL kicks in. The type of `p` is `geo::Point`, which lives in namespace `geo`, so the compiler looks for `draw` in `geo` and finds `geo::draw`. The call succeeds.

ADL is also called Koenig lookup, after Andrew Koenig, who proposed it. The motivation is that functions operating on a type usually live in the namespace of that type. `std::cout << x` works because ADL looks for `operator<<` in the namespace of `x`'s type. Without ADL, you would need full qualification every time, and generic code would be impossible to write.

## Why ADL Matters in Practice: Operators and Generic Algorithms

ADL is not decorative. It underpins several key C++ idioms.

**Operator lookup.** `std::cout << myObj` finds the `operator<<` for `myObj`'s type because of ADL. Whichever namespace the type of `myObj` lives in, ADL searches there for `operator<<`. This is the root reason overloaded operators work across namespaces.

**The swap idiom.** Swapping two values in generic code uses the standard pattern.

```cpp
using std::swap;
swap(a, b);   // not std::swap(a, b), but first using std::swap then a bare call
```

`using std::swap` pulls `std::swap` into the candidate set, then a bare `swap(a, b)` is called. If the type of `a` provides a more efficient `swap` in some namespace (say `ns::swap`), ADL finds it and prefers it. If not, it falls back to `std::swap`. That is why the standard library writes `using std::swap; swap(a, b);` everywhere instead of `std::swap(a, b)` directly, to leave room for custom types to optimize. `std::begin`, `std::end`, `std::size`, and `std::data` follow the same idea.

**Designing for discoverability.** If you want a function to be found by generic algorithms, put it in the namespace of the argument type, and ADL discovers it automatically. This is a big part of C++ namespace design: functions travel with the types they operate on, instead of scattering globally.

## Pitfalls and Compiler Differences in Two-Phase Lookup

Two-phase lookup has a few traps, plus historical baggage, worth mentioning.

**"Overloads added after the definition point are invisible."** The earlier `helper` example is this trap. A same-name overload declared after the template definition is invisible to non-dependent calls inside the template. The fix is either to move the overload declaration before the template, or to make the call dependent (so ADL can kick in).

**MSVC's historical baggage.** For a long time, MSVC **did not strictly implement two-phase lookup**. It deferred all lookup to instantiation and did it in one pass. This meant some code that violated two-phase rules (and errored on GCC and Clang) compiled on MSVC, and the reverse too. This "MSVC accepts it, others break" or "others accept it, MSVC breaks" divergence is a classic pain in cross-platform template libraries. MSVC later added the `/permissive-` switch to follow two-phase lookup strictly, and modern projects turn it on, but old code still carries the traps.

**ADL surprises.** ADL sometimes finds a function you did not expect. If a type happens to live in a namespace with a same-name function, ADL may pull it in and cause ambiguity or call the wrong implementation. Hidden friends (covered in the next piece) are the cure: define an operator as a hidden friend of the class, so it is only discovered by ADL when the arguments match exactly, without polluting the global overload pool.

Next we move into template friends and the Barton-Nackman trick. Hidden friends, and friend injection inside the curiously-recurring-template pattern, only become clear once you understand ADL.
