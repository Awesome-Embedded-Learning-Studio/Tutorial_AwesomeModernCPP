---
title: "What an Optional Reference Is, and Why Assignment Is Always a Rebind"
description: "CppCon 2025 notes — the non-owning nature of optional<T&>, the map-lookup pain point, why assignment is always a rebind, the vector<bool> specter, and how make_optional and CTAD really behave with references"
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
  - "Shallow Traps of Optional References: const, value_or, and Dangling"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/03-optional-reference-and-assignment.md
  source_hash: af0e543d7e0ec86e29e94731eb9b66996bc0175ea990a87436a3808944a0a3d8
  translated_at: '2026-09-26T16:21:41+00:00'
  engine: anthropic
  token_count: 2600
---

# What an Optional Reference Is, and Why Assignment Is Always a Rebind

[The previous piece](./02-value-semantics-of-optional.md) worked through the foundations of the value version thoroughly. This one steps into the heart of `optional<T&>`. The moment `T` becomes a reference, the premises of "ownership" and "value semantics" all stop holding, so we need a different mental model to understand it.

## It Is Not a "Box Holding a Reference"

A lot of people — including the old me — assumed an optional reference is roughly a pointer that can never be null plus a `has_value` check. That reading is too shallow.

The essential character of `optional<T&>` is that it is a non-owning type. Note the words "non-owning": the optional holds no actual object inside; it only points at something that already exists elsewhere. That sounds like a pointer, but the key point is that it carries reference semantics and value semantics at the same time. A pointer is itself a perfectly good value — you can copy it, compare it, and it has its own identity (its address) — and yet a pointer can also be dereferenced to manipulate the thing it points at, which is the reference-semantics side. `optional<T&>` wants exactly that dual personality.

It also comes with an empty state for free, and that empty state needs no extra space to store a flag. Under the hood it is implemented with a null pointer — zero overhead. I used to assume that storing a reference in an optional cost an extra bool's worth of space; it does not. The null pointer itself is the best possible representation of "no value".

## Map Lookup: The Pain Point That Says It All

Theory only gets you so far; a real pain point makes the case better. If you write C++, you have certainly hit this scenario: look something up in a map, and if you find it, modify it.

```cpp
std::map<std::string, int> enemy_hp{{"goblin", 30}, {"dragon", 500}};

auto it = enemy_hp.find("dragon");
if (it != enemy_hp.end()) {
    it->second -= 50;   // to modify the value, you have to write .second
}
```

I have written this code hundreds of times, and every single time that `.second` feels like redundant noise. A map iterator dereferences to `pair<const Key, Value>&` — you only want the value, yet you are forced to deal with the key bundled alongside it.

Once `optional<T&>` is in the standard, a lookup function can return `optional<int&>` directly: found, mutate it; not found, empty; no `.second` anywhere. The standard library does not offer this interface for map yet (that is P3091's business, more on it later), but we can simulate it ourselves with a thin `reference_wrapper` wrapper and get a feel for the effect first:

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
        r->get() += 5;                       // got the reference — modifying the value inside the map
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

Alice's score went from 95 to 100; Charlie was not found, but no exception was thrown. Semantically this is "a reference that might not exist" — it is just that `reference_wrapper` is awkward to use: once you have it in hand, you still need one `.get()` to reach through. With C++26's `optional<T&>` you write the function as returning `optional<int&>` directly, which is far cleaner. The proposal to add lookup interfaces returning optional references to associative containers is P3091 (by Pablo Halpern); it missed the C++26 train and slipped to C++29. The reason sounds a bit funny: pushing the C++26 and C++29 tracks forward at the same time would confuse the people handling the standard document far too much. The C++ standard is itself a three-thousand-plus-page LaTeX document; technically it could be managed with git branches, but nobody actually wants to do that.

## As a Function Parameter: An Implicit Contract

`optional<T&>` is also interesting as a function parameter. I used to think passing a pointer and passing a reference express roughly the same intent, but think it through and a pointer's semantics are hopelessly vague.

```cpp
void process(Logger* logger);                     // will this function delete it? stash it away for later? the caller cannot tell
void process(std::optional<Logger&> logger);      // the intent is far clearer
```

Describing the logger as `optional<Logger&>` tells the function receiving it: I will not own it (no delete), and I will not still be holding a reference to it after the function returns; your only obligation is to keep it alive for the duration of the call. That is not a formal contract, but within what C++ can express, it already counts as a clear statement of intent. An optional parameter also supports "not passing one" for free: pass no logger, and inside the function a quick `if (logger)` check skips the logging logic. Steve Downey calls this a minimalist dependency-injection framework, and I think that description is spot on.

## Assignment: A Rebind, Not a Value Copy

So far it has all been good news. Next comes the place where `optional<T&>` trips people up the hardest: what assignment actually does. Let us set the scene first:

```cpp
struct Cat { std::string name; Cat(std::string n) : name(std::move(n)) {} };

Cat finn{"Finn"};
Cat loki{"Loki"};

std::optional<Cat&> a;            // empty
std::optional<Cat&> b = loki;     // already bound to loki

a = finn;     // ?
b = finn;     // ?  b is already bound to loki — does this rename loki, or rebind b to finn?
```

You might think there is nothing to ask about assignment. But `b` is already bound to loki, and now we assign finn to it: does that change loki's name to "Finn", or does it make b reference finn instead? By analogy with `optional<T>`, where assignment is a value copy, the answer should be the former. That reading is wrong. Run it:

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

    a = finn;     // rebinds a -> finn
    b = finn;     // rebinds b -> finn (not modifying loki)

    std::cout << "a has_value=" << a.has_value() << " a->name=" << a->name << "\n";
    std::cout << "b->name=" << b->name << " loki.name=" << loki.name << "\n";

    int p = 1, q = 2;
    std::optional<int&> oa = p, ob = q;
    std::swap(oa, ob);
    std::cout << "swap 后 *oa=" << *oa << " *ob=" << *ob
              << " (p=" << p << " q=" << q << " 不变)\n";
}
```

```bash
$ g++ -std=c++26 rebind.cpp -o rebind && ./rebind
a has_value=1 a->name=Finn
b->name=Finn loki.name=Loki
swap 后 *oa=2 *ob=1 (p=1 q=2 不变)
```

Read the output. Assigning finn to the empty a rebinds a to finn; assigning finn to b, which was bound to loki, also rebinds b to finn, while loki's name is still Loki — untouched. Assignment changes which object the optional references, not the contents of the referenced object. Swap works the same way: what gets swapped is two pointers — oa and ob trade their binding targets, while the values of p and q themselves are unchanged.

This is exactly what pointer assignment does. When you assign to a pointer, you change where the pointer points, not the contents of the pointee. `optional<T&>` is a pointer internally, so assignment is a rebind.

### Why Not "Copy When Engaged, Bind When Disengaged"

At first I toyed with a scheme that looked cleverer: if the optional is already engaged, do a value copy; if disengaged, do a bind. Think it through and it is a nightmare. With that rule, the behavior of one and the same assignment operator would depend on the optional's current runtime state. You read `opt = value` in the code and have no idea what it does — you have to trace back whether opt holds a value at this moment. That completely destroys our ability to reason about the code.

[The first piece](./01-why-optional-reference-took-20-years.md) covered JeanHeyd's key observation: once assignment behavior depends on state, the type can no longer be reasoned about statically. Every implementation that walked down this road eventually fell into the pit. The rule "always rebind" means that no matter what state the optional was in before, after the assignment it is bound to whatever you handed it. Simple, consistent, predictable.

### The vector\<bool> Specter

But there is a serious objection here, one I wrestled with too. Assignment on `optional<int>` is a value copy; assignment on `optional<int&>` is a rebind. The same template, different specializations, inconsistent behavior. Is this not another `vector<bool>`?

`vector<bool>` is one of the most notorious designs in the C++ standard library. It is a specialization that makes `vector<bool>` and `vector<any other type>` behave magically differently: it does not store real bools but bit-packs them, which means you cannot take the address of a single element, and the iterator type is different too. And once it was in the standard it could not be removed — the language has been carrying that historical baggage ever since.

But I came around eventually. References in C++ were never generic, not from day one. A reference is not an object; it has no address of its own, there can be no "reference to a reference", no "array of references". References have been a special creature in the value-semantics world from the very beginning. Stuffing a reference into a template designed for value semantics and expecting it to behave identically is unrealistic on its face.

We are not "putting a T& into an optional"; what we want is "an optional with reference semantics", and it is simply that C++ reference semantics have to be implemented differently. This is not manufacturing inconsistency — it is forced by the nature of C++ references themselves.

## The Traps make_optional and CTAD Dig for References

With assignment semantics settled, there are two more traps that are especially easy to step into: `make_optional` and CTAD. The conclusion first: `std::make_optional` always returns `optional<T>`, even when what you pass in is a reference.

```cpp
// ctad_truth.cpp
#include <iostream>
#include <optional>
#include <type_traits>

int main() {
    int x = 42;
    auto o1 = std::make_optional(x);     // always optional<int>
    std::optional<int&> o2 = x;           // only the explicit spelling gives optional<int&>
    std::optional o3{x};                  // what does CTAD actually deduce?

    x = 99;
    std::cout << "make_optional 随 x 变？ " << (*o1 == 99) << " (0=拷贝)\n";
    std::cout << "optional<int&> 随 x 变？ " << (*o2 == 99) << " (1=引用)\n";

    if constexpr (std::is_same_v<decltype(o3), std::optional<int&>>)
        std::cout << "CTAD o3 -> optional<int&>\n";
    else if constexpr (std::is_same_v<decltype(o3), std::optional<int>>)
        std::cout << "CTAD o3 -> optional<int>（退化为值，不是引用）\n";
}
```

```bash
$ g++ -std=c++26 ctad_truth.cpp -o ctad_truth && ./ctad_truth
make_optional 随 x 变？ 0 (0=拷贝)
optional<int&> 随 x 变？ 1 (1=引用)
CTAD o3 -> optional<int>（退化为值，不是引用）
```

Three facts in a single run. `make_optional(x)` gives you `optional<int>`; after x changes to 99, what is inside is still the copy holding 42. Only the explicit `std::optional<int&>` has reference semantics and follows x. The line most worth noticing is the third: CTAD on `std::optional o3{x}` deduces `optional<int>`, not `optional<int&>`.

:::warning
Some early materials online claim that CTAD on `std::optional o{x}` can deduce the reference version — that was an idea from the older proposals. In my actual testing, the P2988 that finally landed does no such thing: CTAD still decays to a value. If you want reference semantics, write the full `std::optional<int&> o{x}` out honestly. Do not gamble on CTAD — the gamble pays out a copy.
:::

The fact that `make_optional` decays to a value cannot be changed. Far too much existing code relies on `make_optional` always returning `optional<T>`; changing it would be a breaking change. Intuitively, `make_optional` is "making an optional value" — you would not expect it to hand you something with reference semantics. On this point it lines up with how function return values behave: return a `T&` from a function and receive it with `auto`, and what you get is a `T`, not a `T&`.

## What Comes Next

We have worked out half of the core of `optional<T&>`: non-owning, assignment-as-rebind, and no references from make_optional or CTAD. But around this "internal pointer" there is still a pile of corners: where const sits changes the semantics completely, `value_or` always returns a value, and constructing from a temporary is deleted outright. [The next piece](./04-shallow-traps-const-value-or-dangling.md) peels these shallow traps open one by one.
