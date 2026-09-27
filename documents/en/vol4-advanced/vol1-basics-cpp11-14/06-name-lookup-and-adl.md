---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: Name lookup inside templates is nothing like ordinary code — it runs in two phases. This piece explains two-phase lookup, dependent vs non-dependent names, and ADL (argument-dependent lookup), and why the seemingly odd rules from earlier pieces — typename, this->, hidden friends — have to exist
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
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/06-name-lookup-and-adl.md
  source_hash: c86d2a7fd3498b1c3086d03f5cb8d2db33d47d5d75ee51a3abe777997bd9b1da
  translated_at: '2026-09-26T04:13:59+00:00'
  engine: anthropic
  token_count: 2000
---
# Name Lookup and ADL: How Two-Phase Lookup Works

When you write ordinary code, you use a name, the compiler checks it at the current location, and if something is found, it is used. Templates break this arrangement. Name lookup in a template happens in two phases: one when the template is defined, and one when it is instantiated. The mechanism is called two-phase lookup, and it directly explains the rules from the previous few pieces that looked strange: why `typename` cannot be omitted, why `this->` is sometimes mandatory, and why hidden friends are useful. This piece walks through two-phase lookup, dependent and non-dependent names, and ADL all the way down. Once it is done, whenever a template throws one of those inexplicable errors at you, you will know where to dig for the root cause.

## Name Lookup in Ordinary Code: One Pass and Done

Start with an ordinary function. When a name is used inside one, the compiler performs ordinary lookup at the function's point of definition, searching the enclosing namespaces layer by layer from the inside out and stopping once it has a candidate set.

```cpp
void helper(int) {}

void caller() {
    helper(42);   // At caller's definition point, the compiler looks up helper and finds ::helper(int)
}
```

This is intuitive. Ordinary lookup only sees names visible "before the point of definition"; same-named functions declared after that point never enter the picture.

## Templates Are Different: Two Phases

A template splits this job into two phases.

**Phase one** (template definition): while parsing the template definition, the compiler looks up and binds all **non-dependent names**. Nobody knows what `T` is yet at this point, so names that depend on `T` cannot be looked up and get set aside for now.

**Phase two** (template instantiation): when the template is instantiated for a concrete type, `T` is now known, and the compiler goes on to look up the **dependent names**. The main tool in this phase is ADL (argument-dependent lookup).

Why two phases? Because at the point of template definition, the compiler has no idea what `T` will be. Naturally it cannot know whether `T::value_type` is a type or a variable, nor which namespace it should search for the `foo` in `foo(t)`. So it looks up what it can (the non-dependent names) right away, and defers what it cannot (the dependent names) until instantiation.

Here is a classic example that makes the two phases visible directly.

```cpp
#include <iostream>

// helper that already exists before the template definition
void helper(int) { std::cout << "::helper(int), defined before template\n"; }

template <typename T>
void call_it(T x) {
    helper(x);   // helper is a non-dependent name: looked up and bound in phase one (at the definition point)
}

// helper added only after the template definition
void helper(double) { std::cout << "::helper(double), defined after template\n"; }

int main() {
    call_it(3.14);   // T=double. Intuitively, wouldn't you expect helper(double) to be called?
    return 0;
}
```

Run it:

```bash
$ g++ -Wall -Wextra -std=c++20 twophase.cpp -o twophase && ./twophase
::helper(int), defined before template
```

`call_it(3.14)` actually calls `helper(int)`, not the later-defined `helper(double)`. The reason is exactly the two phases: the name `helper` is non-dependent (it carries no `T`), so it was looked up and bound in phase one, at the template's definition point, to the only `helper` visible at that moment—`helper(int)`. The `helper(double)` defined after the template is invisible to phase one; and phase two applies ADL only to dependent names—`double` is a built-in type with no associated namespace, so ADL cannot introduce any new candidate. Thus `helper(x)` stays bound to `helper(int)` forever, and `3.14` gets truncated to `3` on the way in.

Note that this part is actually no different from ordinary functions: non-dependent names are always resolved at the "point of definition". Whether `call_it` is a template or a plain function, as long as it is defined before `helper(double)`, it can only see `helper(int)`. What genuinely separates two-phase lookup from ordinary function lookup is phase two for dependent names—as the next section on ADL will show, a template can find, through its argument types, functions at the point of instantiation that were utterly invisible at the point of definition. That is the trick only two-phase lookup can pull off.

## Dependent Names vs Non-Dependent Names

This dividing line decides which phase a name is looked up in, and it deserves to be singled out.

A non-dependent name is a name that carries no template parameter. Examples: `helper`, `std::cout`, `int`. These are looked up in phase one.

A dependent name is a name that depends, directly or indirectly, on some template parameter. Examples: `T::value_type`, `x.foo()` (when `x` has type `T`), and `foo(t)` (when the type of `t` depends on `T`). These are looked up in phase two, chiefly via ADL.

Whether a call `foo(t)` is dependent hinges on whether the type of the argument `t` depends on `T`. If `t` is of type `T`, then `foo(t)` is a dependent call, and in phase two ADL will look for `foo` in the namespace where `T` lives. This is the doorway through which ADL does its work.

## typename and this->: Direct Consequences of Two-Phase Lookup

Look back at two rules from the previous few pieces—both are direct consequences of two-phase lookup.

`typename` disambiguates. For a dependent name like `T::value_type`, the compiler in phase one (at the definition point) does not know what `T` is, so it dares not assume `value_type` is a type. By default it treats a dependent qualified name as a variable or a function, unless you explicitly declare "this is a type" with `typename`. This is the unavoidable outcome of phase one being stuck, unable to look the name up at the definition point.

`this->` reaches members of a dependent base. When the base class is `Base<T>`, its members are likewise invisible in phase one (because what `Base<T>` actually looks like has to wait until `T` is known). The compiler does not look up non-dependent names inside a dependent base during phase one, so a bare `helper()` fails to find the base class's `helper`; you must write `this->helper()` to defer the lookup to phase two (at instantiation, the type of `this` is known and the base members are visible).

Once you understand that both rules stem from two-phase lookup, you stop seeing them as "redundant syntax"—they are the necessary machinery that keeps template behavior predictable. Without the two phases, a template's lookup results would swing wildly with the context at each point of instantiation, and nobody could write a reliable template library.

## ADL: Argument-Dependent Lookup

ADL (argument-dependent lookup) is the core tool that phase two uses to look up dependent names. The rule itself is plain: **when you call a function, in addition to ordinary lookup, the compiler also searches the namespaces of the argument types for candidate functions**.

```cpp
#include <iostream>

namespace geo {
    struct Point { int x; int y; };
    void draw(const Point&) { std::cout << "geo::draw(Point)\n"; }
}

int main() {
    geo::Point p{1, 2};
    draw(p);   // No geo:: qualifier, no using namespace geo either
    return 0;
}
```

Run it:

```bash
$ g++ -Wall -Wextra -std=c++20 adl.cpp -o adl && ./adl
geo::draw(Point)
```

Inside `main`, the call `draw(p)` carries neither a `geo::` qualifier nor a `using namespace geo`. Ordinary lookup finds no `draw` in the global namespace, so the call should fail. But ADL steps in: the argument `p` has type `geo::Point`, which lives in the `geo` namespace, so the compiler goes to `geo` to look for `draw` and finds `geo::draw`. The call succeeds.

ADL also goes by the name Koenig lookup, after Andrew Koenig, who first proposed the rule. Its design rationale: functions that operate on a type are usually defined in the same namespace as that type. `std::cout << x` works precisely because ADL goes looking for `operator<<` in the namespace of `x`'s type. Without ADL, you would have to write fully qualified calls every single time, and generic code would be unwritable.

## Why ADL Matters in Practice: Operators and Generic Algorithms

ADL is not decorative; it is a load-bearing pillar of several key idioms of modern C++.

**Operator lookup**. `std::cout << myObj` manages to find the `operator<<` matching `myObj`'s type purely thanks to ADL. Whichever namespace `myObj`'s type lives in, that is where ADL searches for `operator<<`. This is the foundation that lets operator overloading work across namespaces.

**The swap idiom**. To swap two values in generic code, the canonical spelling is:

```cpp
using std::swap;
swap(a, b);   // Not std::swap(a,b), but using std::swap first, then an unqualified call
```

`using std::swap` pulls `std::swap` into the candidate set, and then you make the unqualified call `swap(a, b)`. That way, if the type of `a` provides a more efficient `swap` in some namespace (say `ns::swap`), ADL finds it and prefers it; if not, the call falls back to `std::swap`. This is why the standard library writes `using std::swap; swap(a,b);` everywhere instead of `std::swap(a,b)` directly—it leaves optimization room for custom types. The `std::begin`, `std::end`, `std::size`, `std::data` family follows the same idea.

**Designing for discoverability**. If you want a generic algorithm to find one of your functions, just put it in the namespace of the argument type—ADL will discover it automatically. This is an important strand of how C++ namespaces are designed to be used: functions travel with the types they operate on, instead of being scattered across the global namespace.

## Pitfalls of Two-Phase Lookup and Compiler Differences

Two-phase lookup has a few pitfalls, and history piled some baggage on top; both are worth a mention.

**"Overloads added after the definition point stay invisible."** The `helper` example earlier is exactly this pit: same-named overloads declared after the template's definition stay invisible to non-dependent calls inside the template. The fix is either to move the overload declarations ahead of the template, or to make the call dependent (so that ADL can step in).

**MSVC's historical baggage**. For a long time, the MSVC compiler **did not strictly implement two-phase lookup**; it deferred all lookup to instantiation and did it in one go. As a result, some code that GCC and Clang reject (for violating two-phase rules) would compile under MSVC, and sometimes the reverse. This "MSVC passes, everywhere else explodes" (or "everywhere else passes, MSVC explodes") divergence is a classic pain of cross-platform template library development. MSVC later added the `/permissive-` switch to follow the two phases strictly; modern projects essentially all turn it on, but the scars in old code remain.

**ADL's surprise hits**. ADL sometimes finds functions you never thought of. If some type happens to sit in a namespace that also contains a same-named function, ADL may drag it in, producing ambiguity or calling the wrong implementation. Hidden friends (covered in the next piece) are the cure for this disease: define the operator as a hidden friend of the class so that ADL discovers it only when the argument types match exactly, keeping the global overload pool unpolluted.

The next piece moves on to template friends and the Barton-Nackman trick. Hidden friends, and friend injection inside the curiously recurring template pattern, are things that can only be explained clearly once you understand ADL.
