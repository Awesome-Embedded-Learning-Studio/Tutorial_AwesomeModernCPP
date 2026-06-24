---
title: 'Composite Pattern: Disguising a Tree as an Object'
description: Starting from the primitive approach of "hand-writing a bunch of getters/setters,"
  we gradually derive the Composite pattern, clarify the trade-offs between transparent
  and safe designs, and conveniently use `std::array` and `std::invoke` to handle
  the aggregation of homogeneous objects as well.
chapter: 11
order: 8
tags:
- host
- cpp-modern
- intermediate
- 组合模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 20
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
- 'Chapter 9: 智能指针与所有权'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/08-composite.md
  source_hash: f2ca5ecbc48c3dd50a9bce4bbc9bf371063520a9623e01bc7d1278fb70f0ce44
  translated_at: '2026-06-24T00:56:20.858161+00:00'
  engine: anthropic
  token_count: 3609
---
# Composite Pattern: Disguising a Tree as an Object

## What Problem Are We Actually Solving?

Let's hold off on the formal definition for a moment. Instead, consider a common scenario in game development: you design a character (`Creature`) with three basic attributes—strength (`strength`), agility (`agility`), and intelligence (`intelligence`). At first glance, this seems straightforward; it's just a struct with a few getters and setters:

```cpp
class Creature {
    int strength;
    int agility;
    int intelligence;

public:
    int get_strength() const { return strength; }
    void set_strength(int v) { strength = v; }
    // agility / intelligence 依样画葫芦……
};
```

By this point, you are probably getting a bit annoyed—three fields, six methods, purely manual labor. But what really makes your blood pressure spike is what comes next: the designer says, "We want to analyze this character's ability distribution, to see the total, the average, and the highest stat." So, you instinctively write code like this:

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

It seems to work, but you know there's a landmine buried here: **One day, a designer says, "Let's add a constitution attribute,"** and you'll have to go through `sum`, `avg`, and `max`, manually adding `constitution` to every single calculation. When there are few fields, it's manageable, but once the attributes grow to ten or twenty, you'll be searching the codebase for field names, praying you don't miss any. This is the essence of the problem—**when a bunch of "things that are essentially the same" are split into independent variables with different names, any "do something to all of them" operation requires a manual loop unrolling.**

The Composite pattern exists specifically to address this. The Gang of Four (GoF) defines it as follows: **"Compose objects into tree structures to represent part-whole hierarchies, letting clients treat individual objects and compositions of objects uniformly."** The key here isn't the word "composite" (it is distinct from the "composition relationship" in UML, so don't get them mixed up), but rather "uniformity"—**the client shouldn't care whether it is holding a single leaf node or a subtree full of leaves.**

We will proceed down two paths. The first path addresses the pain point of the `Creature` example directly: **aggregate similar attributes into an array**, allowing standard library algorithms to be used immediately. The second path is the classic GoF tree-like Composite: **using a Group to simulate a multi-way tree**, making leaves and composites look identical from the outside. Both paths share the same motivation, just with different implementations.

## Path 1: Aggregating Similar Objects into an Array

Let's look back at the `sum`/`avg`/`max` pain point. The root of the problem isn't the algorithm, but that `strength`, `agility`, and `intelligence` are three **scattered, unrelated** member variables. But if you look at it from a different angle—in the context of "analyzing ability distribution"—they **are actually the same thing**: they are all "a numerical value of an ability." Since they are the same thing, we can logically aggregate them into an array during analysis, rather than keeping them physically separate.

The GoF book also uses this example, but it mixes the analysis logic directly into the `Creature` class itself. Honestly, I don't agree with this approach—"analyzing abilities" is the client's responsibility, not the character's. Stuffing it into `Creature` breaches the principle of single responsibility. So, we will extract the analyzer and make it an independent `CreatureAnalyzer`:

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

There are a few details here that are worth pausing to consider. First, we copied the three attributes into a `std::array` in one go during construction. This array is the "collection of attributes." From this moment on, `strength`, `agility`, and `intelligence` are no longer three isolated variables, but are **remodeled as a whole** by us. This is the spirit of Composite, except that this time the "container" is an array, not a tree. Second, I intentionally wrote the attribute count `kAbilityCount` as a separate `constexpr` constant. We will discuss this specific point in a moment, as the original book falls into a trap here.

Since the data is now a standard container, the implementation of `sum`/`avg`/`max` is purely a job for the standard library; we don't need to write a single loop manually:

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

Look, after aggregation, all operations that "do one thing to the whole group" automatically become array operations. Now, if the game designer requests adding `constitution`, the changes are limited to stuffing one more value into the constructor and changing `kAbilityCount` to 4. The three algorithms **don't need a single character changed**. This is the payoff for aggregating objects of the same type into a container.

::: warning Don't use the last enum value for count
The original book used this pattern to count abilities: `enum Abilities { strength, agility, intelligence };`, and then used `static_cast<int>(intelligence) + 1` as the array length. This is a trick left over from the old C era—stuffing the "count" into the enum's tail element. However, it has two problems: first, it is a **semantic error**. The enum represents "kinds of abilities", whereas the count is a different concept. Disguising the count as a type of ability confuses anyone reading this code. Second, it is **incompatible with `enum class`**. `enum class` does not allow implicit conversion to `int`, so you would need another `static_cast`, making the code even dirtier. The modern C++ approach is what we did above—count is count, simply write a separate `constexpr std::size_t kAbilityCount`. It's clean and clear. Never stuff metadata like "container size" into the "element collection".
:::

### Taking it a step further: Let the Analyzer pick the fields

The version of `CreatureAnalyzer` above still has a minor regret: it hardcodes `Creature`, and manually lists three getters in the constructor. If we want to reuse this ability to "aggregate a set of values of the same type" to analyze other objects or different field combinations, what do we do? Here, `std::invoke` introduced in C++17, combined with variadic templates, can help us write a generic version:

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

The line `std::invoke(getters, obj)...` is the core of this design. `Getters...` is a pack of "callables," which can be member pointers (like `&Creature::strength`), lambdas, or anything that can be invoked on `obj`. `std::invoke` uniformly turns them into "get an `int` from `obj`." During construction, we pass the object and this pack of getters together, expanding them into the array `vals`. Here is how we use it:

```cpp
Creature hero{10, 20, 30};
Analyzer an(hero, &Creature::get_strength,
            &Creature::get_agility, &Creature::get_intelligence);
an.sum();  // 60
an.avg();  // 20
an.max();  // 30
```

At this point, the first path is complete. Its essence is: **when a group of objects can be treated as a single entity within a specific context, avoid managing them individually. Instead, aggregate them into a standard container and let the algorithms handle the rest.** Templates and `std::invoke` allow this aggregation to be decoupled from concrete types, transforming it into a truly reusable tool. Now, let's move on to the second path—this is the substance of that classic GoF UML diagram.

## The Second Path: The Classic Tree-like Composite

In the first example, the "similar objects" were three integers. Aggregating them was simple: we just stuffed them into an array. However, in reality, the situation is often more complex: instead of multiple values of the same type, you are faced with a collection of **different subclass objects sharing a common base class**, and they possess a **hierarchical** relationship. The most classic example is a Graphical User Interface (GUI)—you have a set of basic primitives (rectangles, circles, text), and you want to bundle several primitives into a group. These groups can contain other groups, ultimately forming a tree structure.

Let's start with the most intuitive approach to see where it falls short.

### Step 1: Naive Polymorphism—No Concept of a "Group"

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
};

class Rectangle : public Graphic {
public:
    void draw() const override { /* 画矩形 */ }
};

class Circle : public Graphic {
public:
    void draw() const override { /* 画圆 */ }
};
```

This polymorphic design works fine, but it only supports "drawing a single primitive." Once you want to move or draw three primitives as a single unit, the caller is forced to maintain a `vector<Graphic*>` and loop through calling `draw` manually. In other words, **the concept of a "group" does not exist in this type system at all**, and the caller has to manually flatten the hierarchy every time. This is manageable when the hierarchy is flat, but once primitives are nested in groups, and groups nested in larger groups, the caller must recursively traverse this tree they manually assembled, and the code quickly spirals out of control.

What we really want is: **to make "a group of primitives" a `Graphic` itself**. This allows the caller to treat it uniformly and call `draw`, without needing to care whether the interior is a single primitive or an entire subtree.

### Step 2: The Group is a Graphic too

Make `Group` inherit from `Graphic` and implement `draw` itself. The implementation of `draw` simply iterates through all children and calls `draw` on each one. The beauty of this step is: **recursion is implicit**. `Group::draw` calls the child's `draw`. If that child is itself a `Group`, it expands again, recursing layer by layer until it reaches all the leaves. The caller is unaware of this; it simply calls `draw` once.

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

Here are a few design choices worth discussing when implementing the Composite pattern in modern C++. First is ownership—we use a `std::vector<std::unique_ptr<Graphic>>` to hold the children. The `add` method takes a `std::unique_ptr`, transferring ownership into the `Group` upon invocation. This step is critical because the original Gang of Four (GoF) book still used raw pointers (`GeoObject*`). In that legacy approach, after adding `new Rectangle()`, the `Group` destructor ignored releasing these children, leading directly to memory leaks. With `unique_ptr`, all children are automatically destroyed recursively when the `Group` is destroyed. The resource management for the entire tree is guaranteed by RAII, and we don't have to write a single manual `delete`.

Next is the line `child->draw()` inside `draw`. It looks unremarkable, but this single line brings the entire tree to life. It performs a virtual call through a base class pointer: if the child is a leaf, it draws the leaf; if the child is a `Group`, it recursively draws its entire subtree. **The uniformity of the call is found right here.**

Let's build a concrete tree to verify this: we attach a `Circle("A")` directly to `root`, then attach a child group `sub`. Inside `sub`, we attach two circles, `B` and `C`. Finally, we attach `D` to `root`:

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

Let's compile and run it:

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

You see, the caller only invokes `root.draw()` once, yet the drawing order fully expands the entire `sub` group—`B` and `C` are nested inside `sub`, and `sub` is nested inside `root`. The hierarchy is perfectly correct. Furthermore, this is completely transparent to `main`; it has no idea that a subtree is hidden inside `root`. **This is the value of Composite: a multi-way tree of arbitrary depth masquerading as a single object to the outside world**.

## Let's verify this first: recursion is indeed doing the work

To ensure that the output above wasn't fabricated, let's attach an indentation counter to `draw` and see if the recursion depth proceeds as expected. Actually, the output already reveals this—`Circle[A]` and `Circle[D]` are indented one level (they hang directly under `root`), while `Circle[B]` and `Circle[C]` are wrapped by `Group[sub]`, making the logical depth two levels. `Group::draw` doesn't explicitly calculate indentation, but it prints a line `Group[sub] (` before calling `child->draw()` and another line `) end Group[sub]` after. These two lines naturally "sandwich" the subtree, creating a visual hierarchy. The real recursion happens at that single virtual call to `child->draw()`—when `child` points to another `Group`, virtual dispatch enters `Group::draw` again, applying the same "print header → traverse children → print footer" structure. This is the most elegant aspect of the Composite pattern: **the traversal logic for the entire tree is compressed into a single `draw` function, with no type checks or manual stacks, relying entirely on polymorphic dispatch**.

## Transparent vs. Safe: where exactly to put that `add`

We aren't quite done yet. Looking back at the code, you will notice something slightly off: the `add` method is **defined only on `Group`**. The leaf `Circle` doesn't have `add` at all. This means if the caller holds a `Graphic&` (a base class reference), they cannot add anything to it—because the base class interface simply doesn't declare `add`.

This leads to an unavoidable design decision in the Composite pattern: **should child management methods (`add`/`remove`/`get_child`) be placed in the base class Component?** GoF presents two classic approaches, known as **Transparent** and **Safe**.

### Transparent: add in base class, leaves throw exceptions

The Transparent approach involves declaring methods like `add` and `remove` directly in the base class `Graphic`, so that leaves also "appear" to possess these methods. Consequently, **the caller holding a `Graphic&` doesn't need to care whether it is a leaf or a group**—this is the origin of the term "transparent." The cost is that leaves must provide a **meaningless** implementation: usually a no-op, or simply throwing an exception.

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
    // 透明式: 基类就声明 add, 叶子实现成抛异常
    virtual void add(std::unique_ptr<Graphic>) {
        throw std::logic_error("add() not supported on a leaf");
    }
};

class Circle : public Graphic {
    // ... 不 override add, 继承基类那个抛异常的版本
};
```

The advantage of this approach is that the interface is completely unified; any `Graphic&` can call `add`, keeping the caller code clean. However, the downside is equally obvious—**type safety is compromised**. You can now call `add` on a `Circle`, and the compiler won't say a word until an exception is thrown at runtime. This effectively postpones an error that should have been caught at compile time to runtime.

Let's run it to verify; it indeed throws:

```sh
$ ./composite_verify
===== leaf.add() throws (transparent) =====
caught: add() not supported on a leaf
```

### Safe Mode: `add` is only on `Group`, leaves don't have it at all

The safe mode is exactly the opposite: **the method for managing children appears only on `Group`, and neither the base class nor the leaves declare `add`**. Now, the leaves simply don't have this method; if you try to call `add` on a `Circle`, it will fail to compile directly. The error is caught at compile time, which makes it "safe". The trade-off is the reverse—when the caller holds a `Graphic&`, if they want to add something to it, they must first `dynamic_cast` it to a `Group&`. The interface is no longer completely uniform.

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
    // 安全式: 基类里没有 add
};

class Circle : public Graphic { /* 没有 add */ };

class Group : public Graphic {
public:
    // ... draw ...
    void add(std::unique_ptr<Graphic> g) {  // add 只在这里
        children_.push_back(std::move(g));
    }
};
```

In this version, if we insist on writing `Circle c("x"); c.add(...)`, the compiler will raise an error directly:

```sh
$ g++ -std=c++23 -O2 -pthread composite_safe_fail.cpp -o composite_safe_fail
composite_safe_fail.cpp:21:7: error: 'class Circle' has no member named 'add'
   21 |     c.add(std::make_unique<Circle>("y"));
      |       ^~~
```

The error occurs at compile time, which is exactly what "safety" means. The working tree example earlier used the safe approach—`Group` has `add`, while the `Graphic` base class does not.

### How to Choose

Neither approach is absolutely superior; it depends entirely on which cost you are more willing to bear in your scenario. If the caller mostly **uses** the tree (calling `draw` or `render`) and rarely **modifies** its structure (calling `add`), the safe approach is more cost-effective—you gain compile-time type safety, and the occasional `dynamic_cast` when modifying the structure is perfectly acceptable. Conversely, if the caller frequently switches between "leaf or group" states and dynamically adds or removes nodes, the transparent approach results in cleaner client code, and the runtime risk of throwing exceptions is tolerable. The GoF book leans towards the transparent approach (because the examples do involve frequent structural modification), whereas in the modern C++ community, developers who value type safety often prefer the safe approach. **Remembering this trade-off is more important than memorizing any "standard answer."**

## Pitfall Warning: Three Things to Remember

::: warning Three Things to Remember
First, **pin down ownership**. The original GoF book uses raw pointers (`GeoObject*` paired with `new`); in the example, the `Group` neither handles release nor performs `delete`, resulting in a genuine memory leak. In modern C++, when writing Composite, **the container must hold `std::unique_ptr<Component>`**. Ownership is transferred upon `add`, and the entire subtree is automatically reclaimed when the `Group` is destroyed. If you need shared ownership (e.g., the same child belongs to multiple groups), consider `std::shared_ptr`, but this introduces the risk of circular references. You would then need `std::weak_ptr` to break cycles, increasing complexity significantly, so it is usually best avoided if possible.

Second, **don't let exceptions in the transparent approach slip away silently**. In the transparent approach, the default implementation of `add` for a leaf is often "throw exception" or "silently ignore." Silent ignoring is the most dangerous—the caller thinks the addition succeeded, but nothing happened, making the bug extremely hard to locate. Even if you choose the transparent approach, the leaf's `add` must throw an exception (or `assert`) to expose the problem at the earliest moment.

Third, **deep recursion can blow the stack**. Composite traversal is recursive, with depth equal to the number of tree layers. The vast majority of UI trees and file system trees are very shallow (a few dozen layers at most), so this is not an issue at all. However, if you use Composite to represent a structure that might degenerate into a long chain (e.g., deeply nested maliciously constructed XML), recursive `draw` might consume the entire call stack. In such extreme scenarios, converting recursion to an explicit stack traversal is safer.
:::

## The Downsides of Composite

Just like with Singleton, we must honestly discuss the costs of Composite, rather than just singing its praises.

**First, type safety is partially consumed by "consistency."** We expanded on this earlier—to unify the interface, the transparent approach merges leaves and composites into the same interface. The cost is that meaningless methods (like `add` on a leaf) are not rejected at compile time and can only rely on runtime exceptions as a fallback. This is the hard price Composite pays for "uniformity"; if you choose the transparent approach, you must accept this cost.

**Second, "meaningless operations" like adding children to a leaf can be more than just throwing an exception.** In some implementations, a leaf's `add` is written as a no-op (does nothing and reports no error). This kind of "tolerance" allows the caller's code to continue running, producing results completely different from expectations without any error. Compared to throwing exceptions, silent failure is the most insidious pitfall in Composite; it must be eliminated during design.

**Third, the complexity of interface design increases.** To allow leaves and composites to share a set of interfaces, the base class `Graphic` has to accommodate both "self-descriptive" responsibilities (`draw`) and potentially "child management" responsibilities (`add`/`remove`). Once you decide on the transparent approach, the base class interface bloats; once you go for the safe approach, the caller has to deal with `dynamic_cast` boilerplate. Both choices have their own inelegance; this is an inherent tension of the pattern itself, not an implementation issue.

**Fourth, performance and stack risks with deep recursion.** As mentioned in the warning, I want to emphasize this again from a cost perspective: Composite's natural traversal method is recursion. In scenarios where the depth is controllable, this is almost free (virtual calls + stack frames are handled easily by modern CPUs). However, once the depth spirals out of control, the cost shifts from "negligible" to "possible stack overflow." In situations where the structure source is untrusted, you must assume it might be very deep.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it wasn't enough |
|---|---|---|
| Scattered variables of the same type | Three `int` variables doing their own thing, hand-written `sum`/`avg`/`max` | Adding fields requires changes everywhere, easy to miss |
| Aggregate into an array | Stuff values of the same type into `std::array`, delegate to algorithms | Only solves "flat homogeneous data", no hierarchy |
| `Analyzer` template | `std::invoke` + variadic templates, decoupled from concrete types | Solves flat aggregation, but cannot express a tree |
| Classic tree-shaped Composite | `Group` is also a `Graphic`, `draw` is recursive | This is the GoF standard answer |
| Transparent / Safe | `add` in base class vs. only in `Group` | Trade-off between consistency and type safety |

Keep these key conclusions in mind:

- **The core of Composite is "consistency", not the word "composition".** It allows the caller to treat leaves and composites equally. This is different from the "composition relationship" (has-a) in UML; don't get misled by the name.
- **Think arrays first for homogeneous objects, trees for hierarchy.** If a group of items are the same thing in your context (a few `int`s, a few graphic primitives), first try to aggregate them into a standard container so you can use standard library algorithms directly. Only when there is true hierarchical nesting between objects should you consider a full tree-shaped Composite.
- **Pin down ownership with `unique_ptr`.** The original GoF's raw pointers + `new` is a breeding ground for memory leaks. In modern C++, the container should hold `std::unique_ptr<Component>`, so the entire subtree is reclaimed automatically upon destruction.
- **Transparent vs. Safe is the core trade-off.** The transparent approach unifies the interface but sacrifices compile-time type safety (`add` on a leaf can only throw exceptions at runtime). The safe approach blocks errors at compile time but requires the caller to perform `dynamic_cast`. Which one to choose depends on which cost you fear more.
- **Never silently ignore `add` on a leaf.** Either throw an exception or `assert`; silence is the most insidious pitfall in Composite.

::: tip Companion Compilable Project
The examples for this section are in the repository at `code/volumn_codes/vol4/design-patterns/Composite/` as a complete, compilable project (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to see the output described above.
:::

## References

- [cppreference: `std::invoke`](https://en.cppreference.com/w/cpp/utility/functional/invoke) (Since C++17, unified invocation for member pointers, lambdas, function objects)
- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (Ownership transfer and recursive destruction)
- Gamma, Helm, Johnson, Vlissides, *Design Patterns: Elements of Reusable Object-Oriented Software*, Composite chapter (Original discussion of transparent and safe approaches)
- Dmitri Nesteruk, *C++20 Design Patterns*, "Composite" section (Modern C++ implementation of array aggregation + tree-shaped Composite, the blueprint for the `CreatureAnalyzer` evolution in this article)
