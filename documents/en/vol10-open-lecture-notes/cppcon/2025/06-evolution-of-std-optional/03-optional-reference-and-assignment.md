---
title: "What an optional reference is, and why assignment is always a rebind"
description: "CppCon 2025 notes — the non-owning nature of optional&lt;T&>, the map-lookup pain point, why assignment is always a rebind, the vector<bool> specter, and how make_optional and CTAD really behave with references"
chapter: 6
order: 3
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 16
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "The Value-Semantics Foundation of std::optional"
related:
  - "The Value-Semantics Foundation of std::optional"
  - "Shallow traps of optional references"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/03-optional-reference-and-assignment.md
  source_hash: af0e543d7e0ec86e29e94731eb9b66996bc0175ea990a87436a3808944a0a3d8
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 3512
---

# What an optional reference is, and why assignment is always a rebind

[The previous piece](./02-value-semantics-of-optional.md) covered the value version's foundations thoroughly. This one steps into the heart of `optional<T&>`. The moment `T` becomes a reference, all those assumptions about "ownership" and "value semantics" stop holding, so we need a different mental model to understand it.

## It is not "a box holding a reference"

A lot of people, including the old me, assumed an optional reference is roughly a non-null pointer plus a `has_value` check. That reading is too shallow.

The essential nature of `optional<T&>` is that it is a non-owning type. Note the word "non-owning" — the optional does not hold any actual object inside it; it only points at something that already exists elsewhere. That sounds like a pointer, but the key is that it carries both reference semantics and value semantics at once. A pointer is itself a perfectly good value: you can copy it, you can compare it, it has its own identity (an address), and yet a pointer can also be dereferenced to manipulate the thing it points at, which is the reference-semantics side. `optional<T&>` wants exactly this dual character.

It also comes with an empty state for free, and that empty state needs no extra space to store a flag. Under the hood it is implemented with a null pointer — zero overhead. I used to think storing a reference inside an optional cost an extra bool's worth of space. It does not: the null pointer is itself the best possible representation of "no value".

## The map lookup: the pain point that tells you the most

Theory only goes so far; let us look at a real pain point. If you write C++, you have hit this scenario: look something up in a map, and if you find it, modify it.

```cpp
std::map<std::string, int> enemy_hp{{"goblin", 30}, {"dragon", 500}};

auto it = enemy_hp.find("dragon");
if (it != enemy_hp.end()) {
    it->second -= 50;   // to mutate the value, you have to write .second
}
```

I have written this code hundreds of times, and every time that `.second` feels like noise. A map iterator dereferences to `pair<const Key, Value>&` — you only want the value, yet you are forced to drag the key along with it.

Once `optional<T&>` is in the standard, a lookup function can return `optional<int&>` directly: found, mutate; not found, empty; no `.second`. The standard library has not added this interface to map yet (that is P3091's job, more later), but we can simulate it ourselves with a thin `reference_wrapper` wrapper to feel the effect first:

```cpp
// refwrap_lookup.cpp
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

template<typename Map>
auto try_get(Map& m, const typename Map::key_type& k)
    -> std::optional<std::reference_wrapper<typename Map::mapped_type>>
{
    auto it = m.find(k);
    if (it != m.end()) return std::ref(it->second);
    return std::nullopt;
}

int main() {
    std::unordered_map<std::string, int> scores{{"Alice", 95}, {"Bob", 87}};

    if (auto r = try_get(scores, "Alice")) {
        r->get() += 5;                       // got a reference — mutating the value inside the map
        std::cout << "Alice=" << scores["Alice"] << "\n";
    }
    if (auto r = try_get(scores, "Charlie")) {
        std::cout << "Charlie=" << r->get() << "\n";
    } else {
        std::cout << "Charlie not found, no exception\n";
    }
}
```

```bash
$ g++ -std=c++17 refwrap_lookup.cpp -o refwrap_lookup && ./refwrap_lookup
Alice=100
Charlie not found, no exception
```

Alice's score went from 95 to 100, and Charlie was not found but no exception was thrown. Semantically this is "a reference that might not exist"; it is just that `reference_wrapper` is awkward to use — once you have one you still need a `.get()` to reach through. Once C++26 ships `optional<T&>`, you write it directly as returning `optional<int&>`, far cleaner. The proposal to add optional-reference-returning lookup interfaces to associative containers is P3091 (by Pablo Halpern); it did not make the C++26 train and slipped to C++29. The reason sounds a little funny: pushing C++26 and C++29 forward at the same time would make the people handling the standard document too confused. The C++ standard is itself a three-thousand-plus-page LaTeX document; in principle it could be managed with git branches, but nobody really wants to do that.

## As a function parameter: an implicit contract

`optional<T&>` is also interesting as a function parameter. I used to think passing a pointer versus a reference expressed roughly the same intent, but on closer thought a pointer's semantics are far too vague.

```cpp
void process(Logger* logger);                     // will this function delete it? store it for later? the caller has no idea
void process(std::optional<Logger&> logger);      // intent is far clearer
```

Describing the logger as `optional<Logger&>` tells the function receiving it: I will not own it (no delete), and I will not keep a reference to it after the function returns; you only have to keep it alive for the duration of the call. This is not a formal contract, but within what C++ can express, it counts as clear intent. An optional parameter also handles "not passed" for free — pass no logger, and inside the function an `if (logger)` check skips the logging logic. Steve Downey calls this a minimalist dependency-injection framework, and I think that framing is on the nose.

## Assignment: a rebind, not a value copy

So far, all good news. Next is the spot where `optional<T&>` trips people up the most: what assignment actually does. Let us set the scene first:

```cpp
struct Cat { std::string name; Cat(std::string n) : name(std::move(n)) {} };

Cat finn{"Finn"};
Cat loki{"Loki"};

std::optional<Cat&> a;            // empty
std::optional<Cat&> b = loki;     // already bound to loki

a = finn;     // ?
b = finn;     // ?  b is already bound to loki — does this change loki's name, or rebind b to finn?
```

You might think assignment is nothing to ask about. But `b` is already bound to loki; now you assign finn to it. Does that change loki's name to "Finn", or does it make b rebind to reference finn? If you reason by analogy from `optional<T>`, where assignment is a value copy, you would expect the former. That reading is wrong. Run it:

```cpp
// rebind.cpp
#include <iostream>
#include <optional>
#include <string>
#include <utility>

struct Cat { std::string name; Cat(std::string n) : name(std::move(n)) {} };

int main() {
    Cat finn{"Finn"}, loki{"Loki"};
    std::optional<Cat&> a;            // empty
    std::optional<Cat&> b = loki;     // bound to loki

    a = finn;     // rebind a -> finn
    b = finn;     // rebind b -> finn (not mutating loki)

    std::cout << "a has_value=" << a.has_value() << " a->name=" << a->name << "\n";
    std::cout << "b->name=" << b->name << " loki.name=" << loki.name << "\n";

    int p = 1, q = 2;
    std::optional<int&> oa = p, ob = q;
    std::swap(oa, ob);
    std::cout << "after swap *oa=" << *oa << " *ob=" << *ob
              << " (p=" << p << " q=" << q << " unchanged)\n";
}
```

```bash
$ g++ -std=c++26 rebind.cpp -o rebind && ./rebind
a has_value=1 a->name=Finn
b->name=Finn loki.name=Loki
after swap *oa=2 *ob=1 (p=1 q=2 unchanged)
```

Read the output. Assigning finn to the empty a rebinds a to finn; assigning finn to b, which was bound to loki, also rebinds b to finn, while loki's name stays Loki, untouched. Assignment changes which object the optional references, not the contents of the referenced object. Swap is the same: it swaps two pointers — oa and ob trade their binding targets, while p and q themselves do not change.

This is exactly what pointer assignment does. When you assign to a pointer, you change where the pointer points, not the contents of the pointee. `optional<T&>` is a pointer internally, so assignment is a rebind.

### Why not "copy if engaged, bind if disengaged"

I once considered what looked like a cleverer scheme: if the optional is engaged, do a value copy; if disengaged, do a bind. On reflection that is a nightmare. With that rule, the same assignment operator's behavior would depend on the optional's runtime state. You read `opt = value` in code and have no idea what it does — you have to trace back whether opt currently holds a value. That completely destroys reasonability.

[The first piece](./01-why-optional-reference-took-20-years.md) covered JeanHeyd's key observation: once assignment behavior depends on state, the type can no longer be reasoned about statically. Every implementation that went down this road eventually fell into the trap. The rule "always rebind" means that regardless of what state the optional was in before, after the assignment it is bound to whatever you handed it. Simple, consistent, predictable.

### The vector\<bool> specter

But there is a serious objection here, one I wrestled with too. Assignment on `optional<int>` is a value copy; assignment on `optional<int&>` is a rebind. The same template, different specializations, inconsistent behavior. Is this not another `vector<bool>`?

`vector<bool>` is one of the most notorious designs in the C++ standard library. It is a specialization that makes `vector<bool>` and `vector<any other type>` behave magically differently: it does not store real bools, it does bit-packing, which means you cannot take the address of a single element, and the iterator type is different too. And once it was in the standard it could not be removed — the language has been carrying that historical baggage ever since.

But I came around. References in C++ were never generic, from day one. A reference is not an object; it has no address of its own, there is no "reference to a reference", there is no "array of references". References have been a special creature in the value-semantics world from the start. Stuffing a reference into a template designed for value semantics and expecting it to behave identically is, on its face, unrealistic.

We are not "putting a T& into an optional"; what we want is "an optional with reference semantics", and C++ reference semantics simply have to be implemented differently. This is not manufacturing inconsistency — it is forced by the nature of C++ references themselves.

## The pitfalls make_optional and CTAD dig for references

With assignment semantics settled, there are two more traps that are especially easy to step into: `make_optional` and CTAD. The conclusion first: `std::make_optional` always returns `optional<T>`, even when you pass in a reference.

```cpp
// ctad_truth.cpp
#include <iostream>
#include <optional>
#include <type_traits>

int main() {
    int x = 42;
    auto o1 = std::make_optional(x);     // always optional<int>
    std::optional<int&> o2 = x;           // only the explicit form gives optional<int&>
    std::optional o3{x};                  // what does CTAD actually deduce?

    x = 99;
    std::cout << "make_optional follows x? " << (*o1 == 99) << " (0=copy)\n";
    std::cout << "optional<int&> follows x? " << (*o2 == 99) << " (1=reference)\n";

    if constexpr (std::is_same_v<decltype(o3), std::optional<int&>>)
        std::cout << "CTAD o3 -> optional<int&>\n";
    else if constexpr (std::is_same_v<decltype(o3), std::optional<int>>)
        std::cout << "CTAD o3 -> optional<int> (decays to value, not reference)\n";
}
```

```bash
$ g++ -std=c++26 ctad_truth.cpp -o ctad_truth && ./ctad_truth
make_optional follows x? 0 (0=copy)
optional<int&> follows x? 1 (1=reference)
CTAD o3 -> optional<int> (decays to value, not reference)
```

Three facts in a single run. `make_optional(x)` gives you `optional<int>`; after x changes to 99, the value inside is still the copy holding 42. Only the explicit `std::optional<int&>` has reference semantics and follows x. The most notable line is the third: CTAD on `std::optional o3{x}` deduces `optional<int>`, not `optional<int&>`.

:::warning
Some early materials online claim that `std::optional o{x}` CTAD can deduce the reference version — that was an idea from older proposals. In my testing, the final P2988 that landed does not do this; CTAD still decays to value. If you want reference semantics, write out the full `std::optional<int&> o{x}`. Do not gamble with CTAD — what comes out is a copy.
:::

The fact that `make_optional` decays to a value cannot change. Far too much existing code relies on `make_optional` always returning `optional<T>`; changing it would be a breaking change. Intuitively, `make_optional` is like "making an optional value" — you would not expect it to hand you something with reference semantics. On this point it lines up with how function return values behave: when you return a `T&` from a function and receive it with `auto`, you get a `T`, not a `T&`.

## What is next

We have worked out half of what `optional<T&>` is really about: non-owning, assignment-as-rebind, and make_optional and CTAD refusing to give references. But around this "internal pointer" there is still a pile of corners: where you put const changes the semantics entirely, `value_or` always returns a value, and constructing from a temporary gets deleted outright. [The next piece](./04-shallow-traps-const-value-or-dangling.md) peels these shallow traps open one by one.
