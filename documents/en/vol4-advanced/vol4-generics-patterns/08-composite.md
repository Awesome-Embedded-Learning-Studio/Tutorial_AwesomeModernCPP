---
title: 'Composite Pattern: Disguising a Whole Tree as a Single Object'
description: 'Starting from the most primitive "hand-write a pile of getters and setters" version, we work our way step by step into the Composite pattern, get the transparent-versus-safe trade-off straight, and along the way also solve aggregating same-kind objects with std::array and std::invoke'
chapter: 11
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - 组合模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - 'Chapter 6: Classes and Object-Oriented Programming'
  - 'Chapter 9: Smart Pointers and Ownership'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/08-composite.md
  source_hash: f2ca5ecbc48c3dd50a9bce4bbc9bf371063520a9623e01bc7d1278fb70f0ce44
  translated_at: '2026-09-26T05:15:39+00:00'
  engine: anthropic
  token_count: 10400
---

# Composite Pattern: Disguising a Whole Tree as a Single Object

## What problem are we actually solving

Let's not rush to a definition. Think of the most common scenario in a game: you've designed three basic attributes for a character (`Creature`) — strength (`strength`), agility (`agility`), and intelligence (`intelligence`). At first it seems harmless: it's just a struct plus a few getters/setters:

```cpp
class Creature {
    int strength;
    int agility;
    int intelligence;

public:
    int get_strength() const { return strength; }
    void set_strength(int v) { strength = v; }
    // agility / intelligence follow the same pattern...
};
```

By this point you're already a bit tired of it — three fields, six methods, pure manual labor. But what really sends your blood pressure through the roof is what comes next: the designers say they want to analyze this character's ability distribution — the sum, the average, the best single attribute. So you reflexively write:

```cpp
int sum() const {
    return strength + agility + intelligence;
}

double avg() const {
    return sum() / 3.0;
}

int max() const {
    return std::max(std::max(strength, agility), intelligence);
}
```

It looks like it runs, but you know full well there's a landmine buried here: **the day the designers say "add a constitution attribute"**, you'll have to dig through `sum`, `avg`, and `max`, manually adding `constitution` to every single computation. With few fields you can still cope; once attributes grow to ten or twenty, all you can do is search for field names all over the place and pray you miss nothing. That is the essence of the problem — **when a bunch of things that are fundamentally the same kind get split into a pile of independently named variables, every "do one thing to all of them" operation has to be hand-unrolled**.

The Composite pattern exists precisely for this. GoF defines it as: **compose objects into tree structures to represent part-whole hierarchies, such that clients can treat individual objects and compositions of objects uniformly**. The key to that sentence is not the word "composite" (which is a different thing from the UML "composition relationship" that denotes has-a — don't confuse them), but "uniformly" — **the caller should not have to care at all whether it is holding a single leaf or an entire subtree full of leaves**.

We'll take two routes below. The first stays closest to the `Creature` pain point just now: **aggregate same-kind attributes into an array**, so the standard library's algorithms can be brought to bear directly. The second is the classic GoF tree-shaped Composite: **use a Group to model a multi-way tree**, so that leaves and compositions look identical from the outside. Both routes are driven by the same motive; they just land in different places.

## Route one: aggregating same-kind objects into an array

Let's go back to the `sum`/`avg`/`max` pain point. The root of the problem is not the algorithms, but the fact that `strength`, `agility`, and `intelligence` are three **scattered, mutually unrelated** member variables. Look at it from another angle, though — in the context of "analyzing the ability distribution", they **are actually the same thing**: each is "the value of one ability". Since they are the same thing, when analyzing we can perfectly well aggregate them **logically** into one array, instead of leaving each to fend for itself physically.

The GoF book uses this very example, but it kneads the analysis logic directly into `Creature` itself. Frankly, I don't agree with that way of writing it — "analyzing abilities" is the caller's business, not one of the character's own responsibilities; stuffing it into `Creature` crosses a responsibility boundary. So we split the analyzer out into a standalone `CreatureAnalyzer`:

```cpp
#include <array>
#include <cstddef>

class Creature {
    int strength;
    int agility;
    int intelligence;

public:
    Creature(int s, int a, int i) : strength(s), agility(a), intelligence(i) {}
    int get_strength() const { return strength; }
    int get_agility() const { return agility; }
    int get_intelligence() const { return intelligence; }
};

class CreatureAnalyzer {
public:
    explicit CreatureAnalyzer(const Creature& c)
        : abilities_{c.get_strength(), c.get_agility(),
                     c.get_intelligence()} {}

    int sum() const;
    double avg() const;
    int max() const;

private:
    static constexpr std::size_t kAbilityCount = 3;
    std::array<int, kAbilityCount> abilities_;
};
```

Several details here deserve a pause. First, we copy the three abilities into a `std::array` in one shot at construction, and this array is "the set of abilities". From this moment on, `strength`, `agility`, and `intelligence` are no longer three lonely variables — they have been **remodeled as a single whole**. That is exactly the spirit of Composite; it's just that this time the "container" is an array rather than a tree. Second, I deliberately wrote the ability count `kAbilityCount` as a standalone `constexpr` constant. More on that shortly, because the original book stumbled right there.

With the data now in a standard container, `sum`/`avg`/`max` become entirely the standard library's job — not one hand-written loop needed:

```cpp
#include <algorithm>
#include <numeric>

int CreatureAnalyzer::sum() const {
    return std::accumulate(abilities_.begin(), abilities_.end(), 0);
}

double CreatureAnalyzer::avg() const {
    return sum() / static_cast<double>(kAbilityCount);
}

int CreatureAnalyzer::max() const {
    return *std::max_element(abilities_.begin(), abilities_.end());
}
```

See? After aggregation, every "do one thing to all of them" operation automatically becomes an operation on the array. Now when the designers want to add `constitution`, the changes amount to one more value in the constructor and changing `kAbilityCount` to 4 — the three algorithms **don't change by a single character**. That's the payoff of aggregating same-kind objects into a container.

::: warning Don't use the last enum value as the count
When counting the number of abilities, the original book used a formulation like this: `enum Abilities { strength, agility, intelligence };` and then `static_cast<int>(intelligence) + 1` as the array length. It's a technique left over from the old C days — stuffing the "count" into the enum's tail element. But it has two problems. First, it's a **semantic error**: the enum enumerates "kinds of abilities", while the count is another matter entirely; disguising the count as one more ability leaves anyone who reads this code utterly baffled. Second, it's **incompatible with enum class**: `enum class` doesn't allow implicit conversion to `int`, so you need yet another `static_cast` and the code gets dirtier and dirtier. The modern C++ way is what we wrote above — the count is the count, standing on its own as a `constexpr std::size_t kAbilityCount`, clean and tidy. Never stuff metadata like "container size" into the "set of elements".
:::

### Going further: letting the analyzer pick its own fields

This version of `CreatureAnalyzer` still has one small regret: it hard-codes `Creature`, and its constructor manually lists the three getters. What if we want to reuse the same "aggregate a group of same-type values" capability to analyze other objects, other combinations of fields? Here `std::invoke`, introduced in C++17, together with variadic templates, helps us write a generic version:

```cpp
#include <array>
#include <cstddef>
#include <functional>
#include <numeric>
#include <algorithm>

template <typename T, typename... Getters>
class Analyzer {
public:
    static constexpr std::size_t N = sizeof...(Getters);
    std::array<int, N> vals;

    Analyzer(const T& obj, Getters... getters)
        : vals{std::invoke(getters, obj)...} {}

    int sum() const {
        return std::accumulate(vals.begin(), vals.end(), 0);
    }
    double avg() const {
        return sum() / static_cast<double>(N);
    }
    int max() const {
        return *std::max_element(vals.begin(), vals.end());
    }
};
```

The line `std::invoke(getters, obj)...` is the heart of the whole design. `Getters...` is a pack of "callables": each can be a member pointer (`&Creature::strength`), a lambda, or anything else that can be invoked against `obj`; `std::invoke` uniformly turns them all into "fetch one int from `obj`". At construction we pass in the object together with this pack of getters, expanded into the array `vals`. Usage looks like this:

```cpp
Creature hero{10, 20, 30};
Analyzer an(hero, &Creature::get_strength,
            &Creature::get_agility, &Creature::get_intelligence);
an.sum();  // 60
an.avg();  // 20
an.max();  // 30
```

At this point route one is complete. Its essence: **when a group of objects can be viewed as the same thing within the context you care about, don't leave each on its own — aggregate them into a standard container and leave the rest to the algorithms**. Templates and `std::invoke` let this aggregation break free of concrete types and become a genuinely reusable tool. Now we move on to route two — the real subject of that classic GoF UML diagram.

## Route two: the classic tree-shaped Composite

In the first example the "same-kind objects" were three ints; aggregating them is trivial — stuff them into an array. But reality more often looks like this: what you face is not three values of the same type, but a pile of **objects of different subclasses under a common base class**, with **hierarchical** relationships among them. The classic case is a graphical interface — you have a bunch of primitives (rectangles, circles, text), you want to bundle several primitives into a group, groups can contain further groups, and in the end you have a tree.

Let's start from the most intuitive version and see where it falls short.

### Step one: naive polymorphism, with no concept of a group

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
};

class Rectangle : public Graphic {
public:
    void draw() const override { /* draw a rectangle */ }
};

class Circle : public Graphic {
public:
    void draw() const override { /* draw a circle */ }
};
```

There is nothing wrong with this polymorphism, but it only supports "draw one primitive". The moment you want to move or draw three primitives as one whole, the caller has to maintain a `vector<Graphic*>` itself and loop over it calling `draw` itself. In other words, **the concept of "a whole" simply does not exist in this type system**, and the caller must manually disassemble the hierarchy every single time. With only one level you can still bear it; once primitives nest inside groups and groups nest inside bigger groups, the caller has to recursively traverse this hand-built tree, and the code spirals out of control.

What we really want is: **make "a group of primitives" itself a `Graphic`**, so the caller can `draw` it just like anything else, without caring at all whether what's inside is one primitive or an entire subtree.

### Step two: a Group is itself a Graphic

Let `Group` inherit from `Graphic` and implement `draw` itself, where the implementation of `draw` is "iterate over all the children and call `draw` on each". The beauty of this step is that **the recursion is implicit** — `Group::draw` calls the child's `draw`; if the child is itself a `Group`, it expands again, recursing layer by layer all the way down to the leaves. The caller knows nothing about any of this; it just calls `draw` once.

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
};

class Circle : public Graphic {
public:
    explicit Circle(std::string name) : name_(std::move(name)) {}
    void draw() const override {
        std::cout << "  Circle[" << name_ << "] drawn\n";
    }

private:
    std::string name_;
};

class Group : public Graphic {
public:
    explicit Group(std::string name) : name_(std::move(name)) {}

    void draw() const override {
        std::cout << "Group[" << name_ << "] (\n";
        for (const auto& child : children_) {
            child->draw();
        }
        std::cout << ") end Group[" << name_ << "]\n";
    }

    void add(std::unique_ptr<Graphic> g) {
        children_.push_back(std::move(g));
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<Graphic>> children_;
};
```

Several of the choices here are worth talking about for writing Composite in modern C++. First, ownership — we hold the children in a `std::vector<std::unique_ptr<Graphic>>`, and `add` takes a `std::unique_ptr`, with ownership transferred into the `Group` by the call. This step matters a lot, because the original GoF book still uses raw pointers (`GeoObject*`); with that code, once you add `new Rectangle()`, `Group`'s destructor does nothing about releasing those children — an outright memory leak. With `unique_ptr`, when a `Group` is destroyed, all of its children are destroyed recursively and automatically; the resource management of the whole tree is backed by RAII, and you never write a single manual `delete`.

Second, that line `child->draw()` inside `draw`. It looks utterly unremarkable, yet it is precisely this line that brings the whole tree to life — it is a virtual call through a base-class pointer: if the child is a leaf, it draws the leaf; if the child is a `Group`, it recursively draws that Group's entire subtree. **The uniformity of invocation lives in this one line**.

Let's build a real tree to verify: hang a `Circle("A")` directly under `root`, plus a subgroup `sub` holding two circles `B` and `C`, and finally one more `D` under `root`:

```cpp
#include <iostream>

int main() {
    Group root("root");
    root.add(std::make_unique<Circle>("A"));

    auto sub = std::make_unique<Group>("sub");
    sub->add(std::make_unique<Circle>("B"));
    sub->add(std::make_unique<Circle>("C"));
    root.add(std::move(sub));

    root.add(std::make_unique<Circle>("D"));

    root.draw();
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -pthread composite_verify.cpp -o composite_verify
$ ./composite_verify
Group[root] (
  Circle[A] drawn
Group[sub] (
  Circle[B] drawn
  Circle[C] drawn
) end Group[sub]
  Circle[D] drawn
) end Group[root]
```

See — the caller invoked `root.draw()` just once, yet in the drawing order the entire `sub` group gets fully expanded: `B` and `C` nest inside `sub`, `sub` nests inside `root`, and the hierarchy comes out exactly right. All of this is transparent to `main`, which has no idea a subtree is hiding inside `root`. **That is the value of Composite: a multi-way tree of arbitrary depth, disguised from the outside as a single object**.

## Let's verify first: the recursion really is doing the work

To convince you that the output above wasn't conjured up, we could hook an indentation counter into `draw` and check whether the recursion depth advances as expected. Actually, you can already tell from the output — `Circle[A]` and `Circle[D]` are indented one level (they hang directly under `root`), while `Circle[B]` and `Circle[C]` are wrapped by `Group[sub]`, two logical levels deep. `Group::draw` never explicitly computes indentation, but before calling `child->draw()` it prints the line `Group[sub] (`, and after the call it prints `) end Group[sub]`; these two lines naturally "sandwich" the subtree between them, producing a visual hierarchy. The real recursion happens in that single virtual call `child->draw()` — when `child` points to another `Group`, virtual dispatch enters `Group::draw` once again, and the same "print header → iterate children → print footer" structure gets applied one more time. This is the most elegant part of the Composite pattern: **the traversal logic of the entire tree is compressed into the same `draw` function — no type-checking ifs, no hand-written stack, polymorphic dispatch all the way down**.

## Transparent vs safe: where exactly should that `add` live

The story doesn't end here. Look back at the code just written and you'll notice something off: the `add` method **is defined only on `Group`** — the leaf `Circle` has no `add` at all. Which means that if the caller is holding a `Graphic&` (a base-class reference), it cannot add anything in — because the base-class interface never declared `add` in the first place.

This raises a design decision the Composite pattern cannot dodge: **should the child-management methods (`add`/`remove`/`get_child`) go into the base Component class or not**? GoF gives two classic answers, called the **transparent** and **safe** styles respectively.

### Transparent: `add` goes into the base class, leaves throw

The transparent style declares `add`, `remove`, and friends directly in the base class `Graphic`, so that leaves also "appear" to have these methods, and therefore **a caller holding a `Graphic&` never needs to care whether it is a leaf or a group** — that is where the word "transparent" comes from. The price is that leaves must provide a **meaningless** implementation: usually a no-op, or a direct throw.

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
    // Transparent style: the base class declares add; the leaf implements it as a throw
    virtual void add(std::unique_ptr<Graphic>) {
        throw std::logic_error("add() not supported on a leaf");
    }
};

class Circle : public Graphic {
    // ... does not override add, inherits the base class's throwing version
};
```

The benefit of this style is a completely unified interface: any `Graphic&` accepts `add`, and caller code stays clean. The drawback is just as obvious — **type safety is broken**. You can now call `add` on a `Circle`, and the compiler won't say a word; the exception comes only at run time. Effectively, an error that should have been stopped at compile time is postponed all the way to run time.

Let's run it to verify — it indeed throws:

```sh
$ ./composite_verify
===== leaf.add() throws (transparent) =====
caught: add() not supported on a leaf
```

### Safe: `add` lives only on Group, leaves simply don't have it

The safe style is precisely the opposite: **the child-management methods appear only on `Group`; neither the base class nor leaves declare `add`**. Now a leaf simply doesn't have the method — call `add` on a `Circle` and compilation fails outright, the error is stopped at compile time, and that is the "safety". The cost flips around: when a caller holding a `Graphic&` wants to add something, it must first `dynamic_cast` it to `Group&`, and the interface is no longer fully unified.

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
    // Safe style: no add in the base class
};

class Circle : public Graphic { /* no add */ };

class Group : public Graphic {
public:
    // ... draw ...
    void add(std::unique_ptr<Graphic> g) {  // add lives only here
        children_.push_back(std::move(g));
    }
};
```

In this version, if you insist on writing `Circle c("x"); c.add(...)`, the compiler rejects it outright:

```sh
$ g++ -std=c++23 -O2 -pthread composite_safe_fail.cpp -o composite_safe_fail
composite_safe_fail.cpp:21:7: error: 'class Circle' has no member named 'add'
   21 |     c.add(std::make_unique<Circle>("y"));
      |       ^~~
```

The error happens at compile time — that is the whole meaning of the word "safe". The runnable tree earlier in this article uses the safe style: `Group` has `add`, the `Graphic` base class does not.

### How to choose

Neither style is absolutely superior; it comes down to which cost your scenario fears more. If callers spend the vast majority of their time **using** the tree (calling `draw`, calling `render`) and rarely **modify** the structure (calling `add`), the safe style is the better deal — you get compile-time type safety, and the occasional `dynamic_cast` when the structure does need changing is perfectly acceptable. Conversely, if callers constantly switch back and forth between "is this a leaf or a group" and dynamically add and remove nodes all the time, the transparent style keeps caller code flatter, and the small run-time risk of a throw is tolerable. The GoF book leans transparent (because in its examples the caller really is adding and removing nodes heavily), while in modern C++ circles, developers who value type safety tend to prefer the safe style. **Remembering the trade-off itself matters more than remembering any "standard answer"**.

## Pitfall warning: don't forget these three things

::: warning Don't forget these three things
First, **nail down ownership**. The original GoF book uses raw pointers (`GeoObject*` paired with `new`); in its sample code `Group` neither takes responsibility for releasing nor calls `delete`, so running it is a bona fide memory leak. When writing Composite in modern C++, **what goes into the container is always `std::unique_ptr<Component>`**: ownership transfers with `add`, and when the `Group` is destroyed the entire subtree is reclaimed automatically. If you genuinely need shared ownership (say, the same child hanging under several groups), then consider `std::shared_ptr` — but that introduces circular-reference risk, forcing you to bring in `std::weak_ptr` to break the cycles; the complexity jumps immediately, so avoid it if you can.

Second, **don't let the transparent style's exceptions slip away quietly**. In the transparent style, the leaf's default `add` implementation is often "throw an exception" or "silently ignore". Silently ignoring is the most dangerous — the caller believes the child was added when in fact nothing happened, a bug that is extremely hard to pin down. Even if you go transparent, the leaf's `add` must throw (or `assert`), exposing the problem at the earliest possible moment.

Third, **deep recursion can blow the stack**. Composite traversal is recursive, with depth equal to the tree's number of levels. The vast majority of UI trees and filesystem trees are shallow (a few dozen levels at most) and pose no problem at all; but if you use Composite to represent a structure that can degenerate into a long chain (say, a maliciously crafted deeply nested XML), the recursive `draw` can eat up the entire call stack. In such extreme scenarios, replacing recursion with an explicit-stack traversal is the safer bet.
:::

## What's not to like about Composite

Like Singleton, Composite also deserves an honest account of its costs — we can't only say nice things.

**First, type safety gets partially eaten by "uniformity".** We expanded on this earlier — for the sake of a unified interface, the transparent style kneads leaves and compositions into the same interface, at the cost that meaningless methods (an `add` on a leaf) are not rejected at compile time and can only be caught by a run-time throw as the last line of defense. This is a hard cost Composite pays for uniformity; choose the transparent style and you must accept that bill.

**Second, the "meaningless operation" of adding a child to a leaf is sometimes more than just a lightweight throw.** In some implementations the leaf's `add` is written as a no-op (do nothing, report nothing); this "leniency" lets the caller's code keep running, producing results completely at odds with expectations with no error whatsoever. Compared with throwing, silent failure is Composite's most treacherous pitfall — stamp it out at design time.

**Third, interface design complexity rises.** To give leaves and compositions one shared interface, the base class `Graphic` has to accommodate both the "self-description" responsibility (`draw`) and possibly the "child management" responsibility (`add`/`remove`). Go transparent and the base-class interface bloats; go safe and callers must shoulder the `dynamic_cast` boilerplate. Both choices have their own inelegance — this is an inherent tension of the pattern itself, not a matter of how you write it.

**Fourth, the performance and stack risk of deep recursion.** The earlier warning mentioned this; from the cost angle it bears repeating: Composite's natural traversal is recursive, which is nearly free when the number of levels is bounded (a virtual call plus a stack frame — trivial work for a modern CPU), but once depth runs out of control, the cost jumps from "negligible" to "may blow the stack". Wherever the structure comes from an untrusted source, assume it may be deep.

## Summary

Let's trace the whole evolutionary path once:

| Stage | Approach | Why it still isn't enough |
|---|---|---|
| Scattered same-kind variables | Three ints each on their own, hand-written `sum`/`avg`/`max` | Adding a field means editing every site; easy to miss one |
| Aggregated into an array | Same-kind values stuffed into a `std::array`, handed to algorithms | Only solves "flat same-kind"; no hierarchy |
| The `Analyzer` template | `std::invoke` + variadics, decoupled from concrete types | Solves flat aggregation but still cannot express a tree |
| Classic tree-shaped Composite | `Group` is itself a `Graphic`; `draw` recurses | This is GoF's canonical answer |
| Transparent / Safe | `add` in the base class vs only on `Group` | Uniformity vs type safety, the trade-off |

Jot down these key conclusions:

- **The heart of Composite is "uniformity", not the word "composite"**. It makes callers treat leaves and compositions alike; it is a different thing from the UML "composition relationship" that denotes has-a — don't be misled by the name.
- **For same-kind objects, think array first; reach for a tree only when there is hierarchy**. If a group of things is the same thing in the context you care about (a few ints, a few primitives), first try aggregating them into a standard container — the standard library's algorithms work on it immediately; only when there is genuine hierarchical nesting between objects is the full tree-shaped Composite worth it.
- **Pin down ownership with `unique_ptr`**. The original GoF's raw pointers + `new` are a breeding ground for memory leaks; in modern C++ the container holds `std::unique_ptr<Component>`, and destruction automatically reclaims the entire subtree.
- **Transparent vs safe is the core trade-off**. The transparent style unifies the interface but sacrifices compile-time type safety (an `add` on a leaf can only throw at run time); the safe style stops errors at compile time but forces callers into `dynamic_cast`. Which one you pick depends on which cost you fear more.
- **Never make the leaf's `add` silently ignore**. Either throw or `assert`; silence is Composite's most treacherous pitfall.

::: tip An accompanying compilable project
This section's examples ship as a complete compilable project under `code/volumn_codes/vol4/design-patterns/Composite/` in the repository (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: `std::invoke`](https://en.cppreference.com/w/cpp/utility/functional/invoke) (since C++17; uniformly invokes member pointers, lambdas, and function objects)
- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (ownership transfer and recursive destruction)
- Gamma, Helm, Johnson, Vlissides, Design Patterns: Elements of Reusable Object-Oriented Software, the Composite chapter (the original discussion of the transparent and safe styles)
- Dmitri Nesteruk, C++20 Design Patterns, the "Composite" section (modern C++ treatments of array aggregation + tree-shaped Composite; the blueprint for this article's `CreatureAnalyzer` evolution)
