---
title: 'Prototype Pattern: From a One-Line Copy Constructor to a `clone()` That Can Carry an Inheritance Hierarchy'
description: 'Start from the most intuitive "build a prototype, then copy it" approach, work step by step toward a polymorphic clone(), get object slicing, deep versus shallow copy, and covariant return types straight, and finish by managing the prototypes with a prototype registry.'
chapter: 11
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 原型模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 19
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - 'Chapter 6: Classes and Object-Oriented Programming'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/04-prototype.md
  source_hash: 9186e8fc890cf8d72ac9713171ea29295c37d8d1fb488be5a37f94b23904a807
  translated_at: '2026-09-26T05:00:33+00:00'
  engine: anthropic
  token_count: 4100
---

# Prototype Pattern: From a One-Line Copy Constructor to a `clone()` That Can Carry an Inheritance Hierarchy

## What Problem Are We Actually Solving

Let's not start with a definition. Picture a concrete scenario: you have an object that is **expensive to build**. Where does the expense come from? Maybe the constructor has to call a remote API to fill in certain fields, maybe it has to run a lengthy computation, maybe it has to read a multi-megabyte configuration template. Now you need **one more object almost exactly like it, differing in only a handful of fields** — say, 1,000 office location records that differ only in door number, or a nest of minor monsters that differ only in hit points.

If your answer here is still "`new` from scratch, then re-run that expensive initialization", that's just silly — there is a ready-made, fully initialized object sitting right next to you. Use it as a template, copy it, tweak a few fields, done. That is exactly the problem the Prototype pattern solves: **treat an already-existing object as a template and create new objects by copying it, instead of constructing from scratch every time**.

It sounds almost too simple — simple enough that you might ask: isn't this just copy construction? Right you are: the most primitive form of the Prototype pattern **is one line of copy construction**. But the topic still deserves a whole article, because the moment that "template object" lives inside a type hierarchy **with inheritance, with polymorphism, and possibly holding resources**, that one line of copy construction starts misbehaving: it slices, it loses type information, it silently shares underlying resources. The question we really want to answer is this: **how do we make "copying an object", under an inheritance hierarchy, still faithfully produce the correct derived type, while handling resource semantics correctly**.

So let's go step by step: first the most straightforward version, then where it falls apart, and finally we will corner ourselves into the modern C++ answer that is both safe and able to carry polymorphism.

## Step One: The Most Intuitive Approach — Build a Prototype, Then Copy It

Let's put that office-location example under the knife. An `Address` records a door number and whether it is reachable:

```cpp
struct Address {
    std::string door_number;     // door number
    bool        accessible {};   // whether it is reachable
};
```

The most straightforward use of a prototype looks like this: initialize a prototype object up front, then copy new objects out of it and tweak a few fields.

```cpp
Address proto;
proto.door_number    = "B-101";
proto.accessible     = true;

// When you need a new instance, copy the prototype and change only the fields that differ
Address other   = proto;
other.door_number = "A501";

Address another = proto;
another.door_number = "C-77";
```

See: `Address other = proto;` here is a copy construction, and that is the Prototype pattern in its most primitive form. For a trivial class like `Address`, whose members are all value types (`std::string`, `bool`), this works perfectly fine — the compiler-synthesized copy constructor makes a proper deep copy of the `std::string`, copies the `bool` by value, and all is well.

But this "perfectly fine" comes with a precondition: the class has **no inheritance and no resource members that need special handling**. The day someone adds a derived class to `Address`, this style of writing immediately gives itself away.

## Step Two: Things Go Wrong — Slicing

Suppose the project evolves for a while, and someone extends `Address` into `ExAddress`, adding an "extra information" field on top:

```cpp
struct ExAddress : Address {
    std::string extra;   // extra info, e.g. "near the coffee machine"
};
```

And some time earlier, for the sake of reuse, we had wrapped "make an object from the prototype with a different door number" into a function — pay attention to its parameter type:

```cpp
Address* make_from_proto(const Address* a, const std::string& door_number) {
    Address* t = new Address(*a);   // uses Address's copy constructor
    t->door_number = door_number;
    return t;
}
```

Now here's the problem. Outside, we pass in a real `ExAddress` object:

```cpp
ExAddress ex;
ex.door_number = "B-101";
ex.accessible  = true;
ex.extra       = "near-cafeteria";   // a field only the derived class has

Address* p = make_from_proto(&ex, "A501");
// p points to an Address — where did its extra go?
```

Inside `make_from_proto` we run `new Address(*a)`, and the **static type** of `*a` is `Address`, so what gets invoked is `Address`'s copy constructor. `Address`'s copy constructor only knows about `Address`'s own two members; it can't see `ExAddress::extra`, let alone copy it — so on that line, `extra` is silently dropped. This is the infamous **object slicing** in C++: you copy-construct from a derived object through a base class, and the derived part is lopped off wholesale.

Let's first verify that this really happens.

## First, a Quick Verification: What Slicing Actually Cuts Off

Write a small program and check whether the derived field survives the copy construction:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

class Address {
public:
    virtual ~Address() = default;
    std::string door_number;
};

class ExAddress : public Address {
public:
    std::string extra;
};

int main() {
    ExAddress ex;
    ex.door_number = "A501";
    ex.extra       = "near-cafeteria";

    // slicing: copying an ExAddress with Address's copy constructor
    Address sliced = ex;

    std::cout << "[sliced]   door_number = " << sliced.door_number << "\n";
    std::cout << "[sliced]   dynamic type = " << typeid(sliced).name() << "\n";

    // the original object's extra is still there
    std::cout << "[original] extra = " << ex.extra << "\n";
    return 0;
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[sliced]   door_number = A501
[sliced]   dynamic type = 9Address
[original] extra = near-cafeteria
```

The result is crystal clear: the copied `sliced` has dynamic type `9Address` (the `9` is the name-length prefix in the mangled name, standing for `Address`); it is no longer an `ExAddress` at all — the `extra` part has been sliced off. That is why "a copy constructor hardwired to the base class" cannot carry an inheritance hierarchy: it loses the type information in the very first step of handing the object along.

Worse, we most likely **can't change** this `make_from_proto` interface — other people in the project are deriving their own classes from `Address`, and if you change the parameter type to `ExAddress*`, every other derived class breaks. What we need is a way to move the decision of "how exactly to copy" out of the function caller's hands and into the object's own hands. This is precisely where `virtual` should enter the stage.

## Step Three: Build Cloning Into the Class — the Polymorphic `clone()`

The idea is direct: since the trouble comes from "which type to copy as" being decided by the caller, let's hand that decision to the object itself. Let every class know "how to replicate myself", and expose that capability through a virtual function — the caller just calls out "give me a copy", and which concrete type the copy becomes is decided by the object's dynamic type.

The base class looks like this:

```cpp
class Address {
public:
    virtual ~Address() = default;

    virtual Address* clone() const {        // base version: copy as an Address
        return new Address(*this);
    }

    std::string door_number;
    bool        accessible {};
};
```

Each derived class overrides it on its own, calling **its own** copy constructor:

```cpp
class ExAddress : public Address {
public:
    std::string extra;

    ExAddress* clone() const override {      // derived version: copy as an ExAddress
        return new ExAddress(*this);
    }
};
```

Notice the two return types: the base class returns `Address*`, the derived class returns `ExAddress*`. This is legal C++, and it is called a **covariant return type** — when a derived class overrides a virtual function, the return type may be a pointer or reference to a type **derived** from the base's return type. It is a backdoor the language opened specifically for "polymorphic factories / polymorphic clone". We'll verify shortly that it really works.

Now let's rewrite that helper function:

```cpp
std::unique_ptr<Address> make_from_proto(const Address& a,
                                         const std::string& door_number) {
    auto t = a.clone();                      // virtual dispatch: the dynamic type decides what to copy as
    t->door_number = door_number;
    return std::unique_ptr<Address>(t);
}
```

The line `a.clone()` is the crux of this whole article. Statically it calls `Address::clone`, but because `clone` is a virtual function, which version actually runs depends on `a`'s **dynamic type** — if `a`'s true identity is `ExAddress`, then `ExAddress::clone` runs here, the copy is a complete `ExAddress`, and slicing never happens again. Let's verify that right now.

## Verify It Once More: Does a Polymorphic `clone` Really Preserve the Dynamic Type

Put the structures above into a runnable little program, call `clone()` through a pointer whose static type is the base class and whose dynamic type is the derived class, and see who the copied object really is:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

class Address {
public:
    virtual ~Address() = default;
    virtual Address* clone() const { return new Address(*this); }
    virtual void describe() const { std::cout << "Address door=" << door_number << "\n"; }
    std::string door_number;
};

class ExAddress : public Address {
public:
    std::string extra;
    ExAddress* clone() const override { return new ExAddress(*this); }
    void describe() const override {
        std::cout << "ExAddress door=" << door_number << " extra=" << extra << "\n";
    }
};

int main() {
    ExAddress ex;
    ex.door_number = "A501";
    ex.extra       = "near-cafeteria";

    Address* proto = &ex;                     // static type Address*, dynamic type ExAddress
    Address* cloned = proto->clone();         // virtual dispatch -> ExAddress::clone

    std::cout << "[clone] dynamic type = " << typeid(*cloned).name() << "\n";
    cloned->describe();                        // this also goes through ExAddress::describe
    delete cloned;
    return 0;
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[clone] dynamic type = 9ExAddress
[proto] dynamic type = 9ExAddress
```

The copied object's dynamic type is `9ExAddress` — the derived part survived intact, and `extra` came along for the ride. This is the core value of a polymorphic `clone()` over "plain copy construction": **it makes the copying behavior follow the dynamic type, so the correct derived type is faithfully reproduced without changing any caller-side code**.

## That Return Type: Why I Recommend Writing `std::unique_ptr<Base>`

Up to this point, our `clone()` has been returning a raw pointer `Address*`. That is the most intuitive version for teaching, but it dumps the burden of "who calls `delete`" onto the caller — the caller receives an `Address*`, must remember to `delete` it themselves, one forgotten `delete` is a memory leak, and one premature `delete` is a dangling pointer.

The more presentable modern C++ style is to have `clone()` return an owning smart pointer directly:

```cpp
class Widget {
public:
    virtual ~Widget() = default;
    virtual std::unique_ptr<Widget> clone() const = 0;   // pure virtual: forces subclasses to implement it
    virtual void draw() const = 0;
};
```

The subclass implementation:

```cpp
class Button : public Widget {
public:
    std::string label;

    std::unique_ptr<Widget> clone() const override {
        return std::make_unique<Button>(*this);   // copy-construct a Button, wrap it in a unique_ptr
    }

    void draw() const override {
        std::cout << "Button: " << label << "\n";
    }
};
```

One detail here deserves a callout: the subclass `clone()` returns `std::unique_ptr<Widget>`, but inside the body, `std::make_unique<Button>(...)` produces a `std::unique_ptr<Button>`, and an implicit conversion happens in between. This works because `Button` is a derived class of `Widget`, and `std::unique_ptr` gives the green light to the "derived-class pointer to base-class pointer" conversion — it has an implicit converting constructor that does not throw. We'll verify this along with the rest in a moment.

You might ask: can the subclass return `std::unique_ptr<Button>` and use a covariant return type? **Here we hit a legacy limitation of C++: `std::unique_ptr<Derived>` is not a type derived from `std::unique_ptr<Base>`; the two are peers — independent types instantiated from different templates — so smart pointers do not support covariant return types.** In other words, for a virtual function returning `unique_ptr`, the base and the derived class must write the **exactly identical** return type (here, `std::unique_ptr<Widget>`), and rely on the implicit conversion inside the function body to "recover" the concrete type. That is a small price to pay, but what it buys is ownership safety — worth it.

Let's verify the `unique_ptr`-returning `clone` and that implicit conversion together:

```cpp
#include <iostream>
#include <memory>
#include <typeinfo>

class Base {
public:
    virtual ~Base() = default;
    virtual std::unique_ptr<Base> clone() const = 0;
};

class Derived : public Base {
public:
    std::unique_ptr<Base> clone() const override {
        return std::make_unique<Derived>(*this);   // unique_ptr<Derived> -> unique_ptr<Base>
    }
};

int main() {
    std::unique_ptr<Base> proto = std::make_unique<Derived>();   // the same implicit conversion
    auto cloned = proto->clone();
    std::cout << "[unique_clone] dynamic type = " << typeid(*cloned).name() << "\n";
    return 0;
}
```

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[unique_clone] dynamic type = 7Derived
```

The real type is `7Derived`, which shows the implicit conversion lost no type information, and virtual dispatch works as usual. So the conclusion is: **write `clone()` to return `std::unique_ptr<Base>`, let ownership travel with the return value, and the caller never writes a single line of `delete`**.

## The Real Trap Comes Later: `clone` Is Not a Mindless `new Derived(*this)`

At this point, the skeleton of a polymorphic `clone()` is clear. But there is one more trap that many introductory resources brush past in a single sentence, yet in real projects it bites the hardest — **is your `clone()` doing a shallow copy or a deep copy**.

`new Derived(*this)` invokes `Derived`'s copy constructor. And **the compiler-synthesized default copy constructor does a memberwise copy of every member**: value-type members (`std::string`, `std::vector`) call their own copy constructors (usually a deep copy), but **pointer members get only their address value copied** — the thing they point to is not copied. Which means: if your class holds raw pointers, or members like `std::shared_ptr` where "to copy is to share", the two objects produced by the default copy constructor will **share the underlying resource** — you think `clone()` gave you an independent copy, while in fact the two of them are holding hands behind your back.

Let's first verify how the default copy constructor behaves with a sharing member:

```cpp
#include <iostream>
#include <memory>

class SharedBuffer {
public:
    explicit SharedBuffer(int v) : data_(std::make_shared<int>(v)) {}
    // default synthesized copy constructor: copying the shared_ptr -> refcount +1, both objects point to the same block
    int  get() const { return *data_; }
    void set(int v)  { *data_ = v; }
private:
    std::shared_ptr<int> data_;
};

int main() {
    SharedBuffer a(10);
    SharedBuffer b = a;          // default shallow copy (shares the underlying int)
    a.set(999);
    std::cout << "[shared shallow] b.get() = " << b.get()
              << " (follows a's mutation)\n";
    return 0;
}
```

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[shared shallow] b.get() = 999 (follows a's mutation)
```

The result is blunt: we changed only `a`, and `b` changed with it — because underneath, the two of them are the same `int`. If your prototype's intent was "provide an independent copy, where modifying the prototype does not affect the copy", this default behavior is a bug.

Raw pointers are even more dangerous. If the member were `int* data_` instead of `shared_ptr<int>`, the default copy constructor would leave both objects' pointers aimed at the same heap block, each destructor would `delete` it once, and you'd have a **double free — straight into undefined behavior**.

So before writing `clone()`, think through the copy semantics of every member of the class: **which members have value semantics — for them, going with the defaults is right; which have sharing semantics — say, a big read-only cache that is deliberately shared: use `shared_ptr` and accept the sharing; and which have ownership semantics — an exclusively owned resource must be deep-copied, or else the class shouldn't allow copying at all**. A `clone()` implementation should carry out that semantics explicitly, not just slap down `new Derived(*this)` and call it done.

::: warning This trap is more common than you think
In many tutorials, the example `clone()` classes are "clean" classes with nothing but a few `std::string` fields, where the default copy constructor happens to be correct — creating the illusion that "clone is one line". In real code, as soon as your class holds a `std::unique_ptr` (not copyable: the synthesized copy constructor gets deleted, and your `clone()` flat-out fails to compile), or holds raw pointers / `shared_ptr`, you must hand-write the copy constructor, hand-write the deep-copy logic inside `clone()`, or explicitly adopt sharing semantics. **Don't copy the sample code blindly — ask yourself first: after this class is copied, should the two objects share the underlying resource or not?**
:::

## Reining In the Prototypes: The Prototype Registry

By now we have objects that clone correctly. But there is still an engineering problem: the prototypes themselves are often the "expensive to build" objects — you don't want to rebuild the prototype every time you use it. The natural move is to store the commonly used prototypes centrally, fetch one by name or id when needed, and `clone()` a copy out of it. That is the **Prototype Registry**.

It looks a lot like the Factory pattern; the difference is that a factory "builds a new object from parameters", while a registry "copies one from pre-stored templates". Internally, the registry stores prototypes; outwardly, it exposes "clone by name":

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

class Widget {
public:
    virtual ~Widget() = default;
    virtual std::unique_ptr<Widget> clone() const = 0;
    virtual void draw() const = 0;
};

class Button : public Widget {
public:
    std::string label;
    explicit Button(std::string l) : label(std::move(l)) {}
    std::unique_ptr<Widget> clone() const override {
        return std::make_unique<Button>(*this);
    }
    void draw() const override { std::cout << "Button: " << label << "\n"; }
};

class TextField : public Widget {
public:
    std::string placeholder;
    explicit TextField(std::string p) : placeholder(std::move(p)) {}
    std::unique_ptr<Widget> clone() const override {
        return std::make_unique<TextField>(*this);
    }
    void draw() const override { std::cout << "TextField: " << placeholder << "\n"; }
};

class WidgetRegistry {
public:
    void register_proto(const std::string& name, std::unique_ptr<Widget> proto) {
        protos_[name] = std::move(proto);
    }

    std::unique_ptr<Widget> create(const std::string& name) const {
        auto it = protos_.find(name);
        if (it == protos_.end()) return nullptr;     // not registered -> return null; the caller deals with it
        return it->second->clone();                  // clone a copy from the template
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Widget>> protos_;
};
```

Usage looks like this: register a few carefully tuned prototypes up front, and from then on, fetch by name at any time — what you get is always an independent copy:

```cpp
int main() {
    WidgetRegistry registry;

    // register prototypes: these prototypes can run the expensive initialization up front
    registry.register_proto("ok_button",
        std::make_unique<Button>("OK"));
    registry.register_proto("cancel_button",
        std::make_unique<Button>("Cancel"));
    registry.register_proto("name_field",
        std::make_unique<TextField>("enter your name"));

    // clone by name; mutating a clone affects neither the prototype nor other clones
    auto b1 = registry.create("ok_button");
    auto b2 = registry.create("ok_button");
    auto f1 = registry.create("name_field");

    if (b1) b1->draw();    // Button: OK
    if (b2) b2->draw();    // Button: OK  (an independent copy; changing b1 doesn't affect b2)
    if (f1) f1->draw();    // TextField: enter your name
}
```

This structure is the embryo of configuration-driven creation — the prototypes in the registry can be loaded from config files or scripts, and at runtime, whole sets of objects are assembled by name. Monster spawning in games, UI theme switching, "copy and paste" in document editors: behind the scenes, it is very often exactly this. The cost is that you have to put real thought into managing prototype registration and lifetimes; and if the prototypes themselves are mutable and the registry is accessed across threads, you'll additionally need locking to keep things consistent. But for scenarios that need to "mass-produce objects from templates", that bit of maintenance cost is worth paying.

## Summary

Let's walk the whole evolution path once more:

| Stage | Approach | Why it is still not enough |
|---|---|---|
| Copy the prototype directly | `Address other = proto;` | Fine for trivial classes, but slices once inheritance appears |
| Helper function with the copy hardwired to the base | `new Address(*a)` | Slices: loses the derived part and the dynamic type |
| Polymorphic `clone()` | Base declares `virtual clone()`, each subclass overrides | Carries the inheritance hierarchy (good enough for most cases) |
| `clone()` returning `unique_ptr` | Return `std::unique_ptr<Base>` | Settles ownership; the caller is freed from `delete` |
| Prototype registry | The registry clones by name | Configuration-driven mass creation, but registration/lifetimes need managing |

Note down these key conclusions:

- **The essence of the Prototype pattern is "replace construction with copying"**: its most primitive form is one line of copy construction, and only an inheritance hierarchy or held resources force the upgrade to a polymorphic `clone()`.
- **Slicing is the number-one killer of copying in polymorphic scenarios** — `new Base(*derived_ptr)` silently drops the derived part; the antidote is to make cloning a virtual function and let the dynamic type decide what gets copied.
- **`clone()` returning `std::unique_ptr<Base>`** is the standard modern C++ practice; covariant return types work only for raw pointers/references, and smart pointers do not support covariance among themselves, so the base and derived classes must write the same return type and rely on the implicit conversion to recover the concrete type.
- **`clone()` is not a mindless `new Derived(*this)`** — think through each member's copy semantics first (value / shared / ownership); the default copy constructor only shallow-copies pointers, and a class containing `unique_ptr` is not copyable by default at all.
- When you need to mass-produce objects from templates, layering a **prototype registry** on top — indexing prototypes by name, `clone()`-ing on demand — is a common skeleton for configuration-driven creation.

::: tip Companion Compilable Project
The examples in this section ship as a complete compilable project under `code/volumn_codes/vol4/design-patterns/Prototype/` in the repository (`.h` files + a `main` + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: Virtual functions (covariant return types)](https://en.cppreference.com/w/cpp/language/virtual) (since C++98, virtual functions returning covariant pointers/references)
- [cppreference: Copy constructors](https://en.cppreference.com/w/cpp/language/copy_constructor) (the memberwise copy semantics the default copy constructor applies to members)
- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (implicit derived-to-base conversion, and why it cannot be covariant)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — the Prototype chapter
- Dmitri Nesteruk, *Hands-On Design Patterns with C++ and .NET Core*, the Prototype pattern chapter (polymorphic clone and registry practice)
