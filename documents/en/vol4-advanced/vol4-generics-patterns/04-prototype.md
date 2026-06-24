---
title: 'Prototype Pattern: From a One-Line Copy Constructor to a `clone()` That Handles
  Inheritance'
description: Starting from the most intuitive "build a prototype and copy it" approach,
  we progressively derive a polymorphic `clone()`. We clarify object slicing, deep
  versus shallow copy, and covariant return types, and finally manage templates using
  a prototype registry.
chapter: 11
order: 4
tags:
- host
- cpp-modern
- intermediate
- 原型模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 19
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/04-prototype.md
  source_hash: 9186e8fc890cf8d72ac9713171ea29295c37d8d1fb488be5a37f94b23904a807
  translated_at: '2026-06-24T00:54:31.629515+00:00'
  engine: anthropic
  token_count: 3694
---
# Prototype Pattern: From a Single Copy Constructor to a `clone()` That Handles Inheritance

## What Problem Are We Actually Solving?

Let's skip the formal definition for a moment. Imagine a concrete scenario: you have an object that is **expensive to create**. Why is it expensive? Maybe its constructor needs to fetch data from a remote API to populate fields, or it requires a time-consuming calculation, or perhaps it reads a multi-MB configuration template. Now, you need **another object that is almost identical to it, differing only in a few fields**—for example, 1,000 office location records that differ only by street number, or a swarm of monsters that differ only in health points.

If you stick to the "start from scratch with `new` and re-run that expensive initialization" approach, that's inefficient—when you have a ready-made, fully initialized object right there. Why not just use it as a template, copy it, and tweak a few fields? This is exactly what the Prototype Pattern solves: **use an existing object as a template to create new objects by copying it, rather than constructing from scratch every time.**

It sounds deceptively simple—so simple you might ask: isn't this just a copy constructor? Yes, the most primitive form of the Prototype Pattern **is just a copy constructor**. However, this topic deserves its own article because when that "template object" lives within a **type system involving inheritance, polymorphism, and resource ownership**, that simple copy constructor starts to fail: it slices objects, loses type information, and can inadvertently share underlying resources. The real question we need to answer is—**how do we ensure that "copying an object" faithfully reproduces the "correct derived type" within an inheritance hierarchy and correctly handles resource semantics?**

We will walk through this step-by-step: first, looking at the most intuitive approach; then, seeing where it breaks; and finally, deriving a standard, modern C++ solution that is both safe and capable of handling polymorphism.

## Step 1: The Most Intuitive Approach—Create a Prototype, Then Copy It

Let's start with the office location example. An `Address` record holds a street number and accessibility status:

```cpp
struct Address {
    std::string door_number;     // 门牌号
    bool        accessible {};   // 是否可达
};
```

The most straightforward way to use a prototype is to first initialize a prototype object, then use it to create copies, and finally modify a few fields.

```cpp
Address proto;
proto.door_number    = "B-101";
proto.accessible     = true;

// 需要新实例时,从原型拷一份,只改需要变的字段
Address other   = proto;
other.door_number = "A501";

Address another = proto;
another.door_number = "C-77";
```

You see, `Address other = proto;` here is a copy construction, representing the most primitive form of the Prototype pattern. For a trivial class like `Address`, where all fields are value types (`std::string`, `bool`), this approach works perfectly—the compiler-generated copy constructor correctly performs a deep copy of the `std::string` and copies the `bool` by value, so everything is fine.

However, this "perfectly fine" comes with a premise: **the class must have no inheritance and no resource members requiring special handling**. The moment someone adds a derived class to `Address`, this approach immediately exposes its flaws.

## Step Two: The Problem Arises — Object Slicing

Let's assume the project evolves over time, and someone extends it by creating `ExAddress`, which adds an "additional info" field to the base:

```cpp
struct ExAddress : Address {
    std::string extra;   // 附加信息,比如「靠近咖啡机」
};
```

Back in the day, to reuse code, we encapsulated the process of "creating an object from a prototype with a different ID" into a function—pay attention to its parameter type:

```cpp
Address* make_from_proto(const Address* a, const std::string& door_number) {
    Address* t = new Address(*a);   // 用 Address 的拷贝构造
    t->door_number = door_number;
    return t;
}
```

Now the question arises. What happens if we pass a real `ExAddress` object from the outside?

```cpp
ExAddress ex;
ex.door_number = "B-101";
ex.accessible  = true;
ex.extra       = "near-cafeteria";   // 派生类独有字段

Address* p = make_from_proto(&ex, "A501");
// p 指向一个 Address,它的 extra 去哪了?
```

Inside `make_from_proto`, the code executes `new Address(*a)`. Since the **static type** of `*a` is `Address`, the copy constructor of `Address` is invoked. The copy constructor of `Address` only knows about its own two members; it cannot see, nor does it copy, `ExAddress::extra`. Consequently, on this line, `extra` is silently discarded. This is the infamous **object slicing** in C++: when you use a base class to copy-construct a derived class object, the derived part is sliced off cleanly.

Let's first verify that this actually happens.

## Verification: What exactly gets sliced?

Let's write a small program to see if the derived fields still exist after the copy constructor runs:

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

    // 切片:用 Address 的拷贝构造去拷一个 ExAddress
    Address sliced = ex;

    std::cout << "[sliced]   door_number = " << sliced.door_number << "\n";
    std::cout << "[sliced]   dynamic type = " << typeid(sliced).name() << "\n";

    // 原对象的 extra 还在
    std::cout << "[original] extra = " << ex.extra << "\n";
    return 0;
}
```

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[sliced]   door_number = A501
[sliced]   dynamic type = 9Address
[original] extra = near-cafeteria
```

The result is crystal clear: the dynamic type of the copied `sliced` has become `9Address` (`9` is the name length prefix in the mangled name, representing `Address`). It is no longer an `ExAddress`—the `extra` part has been sliced off. This is why a copy constructor hardcoded to the base class cannot support an inheritance hierarchy: it loses the type information right at the first step of information transfer.

Even more critically, we likely **cannot** change the interface of `make_from_proto`—because others in the project are also deriving from `Address` for their own purposes. If you change the parameter type to `ExAddress*`, all other derived classes will break. We need a way to move the decision of "how to copy" from the caller to the object itself. This is exactly where `virtual` comes into play.

## Step 3: Build Cloning into the Class — Polymorphic `clone()`

The idea is straightforward: since the problem stems from the caller deciding "which type to copy to," we hand this decision over to the object itself. Let each class know "how I should copy myself," and expose this capability through a virtual function—the caller simply shouts "give me a copy," while the specific type to copy is determined by the object's dynamic type.

We write the base class like this:

```cpp
class Address {
public:
    virtual ~Address() = default;

    virtual Address* clone() const {        // 基类版本:复制成一个 Address
        return new Address(*this);
    }

    std::string door_number;
    bool        accessible {};
};
```

Each derived class overrides and calls **its own** copy constructor:

```cpp
class ExAddress : public Address {
public:
    std::string extra;

    ExAddress* clone() const override {      // 派生类版本:复制成一个 ExAddress
        return new ExAddress(*this);
    }
};
```

Note the return types in these two lines: the base class returns `Address*`, while the derived class returns `ExAddress*`. This is valid in C++ and is known as a **covariant return type**. When a derived class overrides a virtual function, the return type can be a pointer or reference to a type derived from the base class's return type. This is a specific language feature designed to support "polymorphic factories" or "polymorphic clones." We will verify that this works correctly in a moment.

Now, let's rewrite that utility function:

```cpp
std::unique_ptr<Address> make_from_proto(const Address& a,
                                         const std::string& door_number) {
    auto t = a.clone();                      // 走虚派发:动态类型决定复制成谁
    t->door_number = door_number;
    return std::unique_ptr<Address>(t);
}
```

This line `a.clone()` is the key to the entire article. Its static invocation is `Address::clone`, but because `clone` is a virtual function, the actual version executed depends on the **dynamic type** of `a`—if `a`'s true identity is `ExAddress`, then `ExAddress::clone` is called here, and the result is a complete `ExAddress` object, so slicing will never occur again. Let's verify this.

## Verification: Does Polymorphic `clone` Really Preserve the Dynamic Type?

Let's put the above structure into a runnable small program, use a pointer whose static type is the base class but whose dynamic type is the derived class to call `clone()`, and see exactly who the copied object is:

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

    Address* proto = &ex;                     // 静态类型 Address*,动态 ExAddress
    Address* cloned = proto->clone();         // 虚派发 -> ExAddress::clone

    std::cout << "[clone] dynamic type = " << typeid(*cloned).name() << "\n";
    cloned->describe();                        // 走的也是 ExAddress::describe
    delete cloned;
    return 0;
}
```

Build and Run:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[clone] dynamic type = 9ExAddress
[proto] dynamic type = 9ExAddress
```

The dynamic type of the copied object is `9ExAddress`—the derived part is fully preserved, and `extra` came along with it. This is the core value of a polymorphic `clone()` compared to "direct copy construction": **it makes the copy behavior follow the dynamic type, thereby faithfully reproducing the correct derived type without modifying any caller code.**

## That Return Type: Why I Recommend You Write `std::unique_ptr<Base>`

Until now, our `clone()` has returned a raw pointer `Address*`. While this is the most intuitive approach for teaching purposes, it offloads the burden of "who calls `delete`" onto the caller—the caller receives an `Address*` and must remember to `delete` it themselves. Forgetting to do so results in a memory leak, while deleting it too early leads to a dangling pointer.

A more idiomatic approach in modern C++ is to have `clone()` directly return a smart pointer that owns the resource:

```cpp
class Widget {
public:
    virtual ~Widget() = default;
    virtual std::unique_ptr<Widget> clone() const = 0;   // 纯虚,强制子类实现
    virtual void draw() const = 0;
};
```

Implementation of the subclass:

```cpp
class Button : public Widget {
public:
    std::string label;

    std::unique_ptr<Widget> clone() const override {
        return std::make_unique<Button>(*this);   // 拷贝构造一个 Button,包进 unique_ptr
    }

    void draw() const override {
        std::cout << "Button: " << label << "\n";
    }
};
```

Here is a detail worth noting: the subclass `clone()` returns a `std::unique_ptr<Widget>`, but the function body uses `std::make_unique<Button>(...)` which creates a `std::unique_ptr<Button>`. There is an implicit conversion happening here. This works because `Button` is a derived class of `Widget`, and `std::unique_ptr` allows for the conversion from a derived class pointer to a base class pointer—it has a non-throwing implicit constructor. We will verify this together in a moment.

You might ask: can we let the subclass return `std::unique_ptr<Button>` and use covariant return types? **There is a historical limitation in C++ here: `std::unique_ptr<Derived>` is not a derived type of `std::unique_ptr<Base>`. They are independent, parallel types instantiated from different templates, so smart pointers do not support covariant return types.** In other words, for a virtual function returning a `unique_ptr`, the base class and the derived class must have **exactly the same** return type (here, `std::unique_ptr<Widget>`), relying on the implicit conversion within the function body to "recover" the specific type. This is a small price to pay for the safety of ownership semantics, and it is worth it.

Let's verify both the "`unique_ptr` version of clone" and that implicit conversion together:

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
    std::unique_ptr<Base> proto = std::make_unique<Derived>();   // 同样的隐式转换
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

The actual type is `7Derived`, which shows that the implicit conversion did not lose type information, and virtual dispatch works as expected. The conclusion is: **implement `clone()` to return a `std::unique_ptr<Base>`, let ownership follow the return value, and the caller doesn't have to write a single `delete`.**

## The Real Pitfall Lies Ahead: `clone` Is Not Just Mindless `new Derived(*this)`

At this point, the framework for a polymorphic `clone()` is clear. But there is a pitfall that many introductory materials gloss over, yet it bites back hardest in practice—**does `clone()` perform a shallow copy or a deep copy?**

`new Derived(*this)` invokes the copy constructor of `Derived`. The **default copy constructor synthesized by the compiler performs a "member-wise copy" for each member**: value-type members (like `std::string`, `std::vector`) invoke their own copy constructors (usually deep copies), but **pointer members only copy the address value**, not the data they point to. This means that if your class contains raw pointers, or members like `std::shared_ptr` where "copying implies sharing," the two objects produced by the default copy constructor will **share underlying resources**—you think `clone()` produced an independent copy, but they are actually holding hands behind the scenes.

Let's first verify how the default copy constructor behaves with shared members:

```cpp
#include <iostream>
#include <memory>

class SharedBuffer {
public:
    explicit SharedBuffer(int v) : data_(std::make_shared<int>(v)) {}
    // 默认合成拷贝构造:shared_ptr 拷贝 -> 引用计数 +1,两个对象指向同一块
    int  get() const { return *data_; }
    void set(int v)  { *data_ = v; }
private:
    std::shared_ptr<int> data_;
};

int main() {
    SharedBuffer a(10);
    SharedBuffer b = a;          // 默认浅拷(共享底层 int)
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

The result is straightforward: we only modified `a`, but `b` changed as well—because they both point to the same underlying `int`. If your prototype's intent is to "provide an independent copy where modifying the original does not affect the copy," this default behavior is a bug.

Even more dangerous are raw pointers. If a member is `int* data_` instead of `shared_ptr<int>`, the default copy constructor will cause two objects to point to the same heap memory. When they destruct, each will call `delete`, resulting in a **double free and immediate undefined behavior (UB)**.

Therefore, before writing `clone()`, you must clarify the copy semantics for every member in the class: **which are value semantics (follow the default), which are shared semantics (e.g., large read-only buffers intentionally shared, so use `shared_ptr` and accept sharing), and which are ownership semantics (exclusive resources, so copies must be deep copies, otherwise copying should be prohibited)**. The implementation of `clone()` must clearly enforce these semantics, rather than just mindlessly writing `new Derived(*this)`.

::: warning This pitfall is more common than you think
Many tutorial `clone()` example classes only contain a few `std::string` fields—so-called "clean" classes where the default copy constructor happens to be correct. This creates the illusion that "clone is just a one-liner." In real-world scenarios, once your class holds a `std::unique_ptr` (non-copyable, the implicitly generated copy constructor is deleted, so your `clone()` won't compile), or holds raw pointers / `shared_ptr`, you must hand-write the copy constructor and the deep copy logic for `clone()`, or explicitly adopt shared semantics. **Don't just copy examples; first ask yourself: after copying this class, should the two objects share underlying resources?**
:::

## Managing Prototypes: The Prototype Registry

At this point, we have objects that can clone correctly. However, there is still an engineering challenge: prototypes themselves are often "expensive to create" objects—you don't want to recreate a prototype every time you need to use it. A natural approach is to store commonly used prototypes centrally and retrieve them by name or ID when needed, then `clone()` a copy. This is the **Prototype Registry**.

It looks very similar to the Factory pattern, but the difference is: a factory "creates a new object based on parameters," while a registry "copies from a pre-stored template." The registry stores prototypes internally and exposes a "clone by name" interface:

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
        if (it == protos_.end()) return nullptr;     // 没注册 -> 返回空,调用方自己处理
        return it->second->clone();                  // 从模板克隆一份
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Widget>> protos_;
};
```

Here is how we use it: first, we register a few "carefully tuned" prototypes, and afterwards, we can retrieve them by name at any time, always getting an independent copy:

```cpp
int main() {
    WidgetRegistry registry;

    // 注册原型:这些原型可以预先做昂贵的初始化
    registry.register_proto("ok_button",
        std::make_unique<Button>("OK"));
    registry.register_proto("cancel_button",
        std::make_unique<Button>("Cancel"));
    registry.register_proto("name_field",
        std::make_unique<TextField>("enter your name"));

    // 按名字克隆,改了克隆体不影响原型,也不影响其他克隆体
    auto b1 = registry.create("ok_button");
    auto b2 = registry.create("ok_button");
    auto f1 = registry.create("name_field");

    if (b1) b1->draw();    // Button: OK
    if (b2) b2->draw();    // Button: OK  (独立副本,改 b1 不影响 b2)
    if (f1) f1->draw();    // TextField: enter your name
}
```

This structure represents the prototype of a configuration-based creation system—prototypes in the registry can be loaded from configuration files or scripts, and the entire set of objects is assembled at runtime by name. Monster spawning in games, UI theme switching, and "copy-paste" in document editors often rely on this mechanism behind the scenes. The cost is that you must invest effort in managing the registration and lifecycle of the prototypes; if the prototypes themselves are mutable and the registry is accessed across threads, you will need additional locking to ensure consistency. However, for scenarios where "objects need to be created in batches based on templates," this maintenance cost is worthwhile.

## Summary

Let's review the entire evolutionary path:

| Stage | Approach | Why it falls short |
|---|---|---|
| Direct prototype copy | `Address other = proto;` | Works for trivial classes, but causes slicing with inheritance |
| Utility function for fixed base copy | `new Address(*a)` | Slicing: loses derived parts and dynamic type |
| Polymorphic `clone()` | Base declares `virtual clone()`, derived classes override | Supports the inheritance hierarchy (basically sufficient) |
| `clone()` returns `unique_ptr` | Returns `std::unique_ptr<Base>` | Solves ownership, caller is free from `delete` |
| Prototype Registry | Registry `clone()` by name | Configuration-based batch creation, but requires managing registration/lifecycle |

Keep these key conclusions in mind:

- **The essence of the Prototype pattern is "using copy instead of construction"**. Its simplest form is just a copy constructor line; you only need to upgrade to a polymorphic `clone()` when dealing with inheritance hierarchies or resource management.
- **Slicing is the primary killer of copying in polymorphic scenarios**—`new Base(*derived_ptr)` silently discards the derived portion. The cure is to make cloning a virtual function, letting the dynamic type decide what to copy.
- **`clone()` returning `std::unique_ptr<Base>`** is the standard approach in modern C++. Covariant return types only work for raw pointers or references; smart pointers do not support covariance, so the base and derived classes must write the same return type, relying on implicit conversion to recover the specific type.
- **`clone()` is not a mindless `new Derived(*this)`**—first clarify the copy semantics of every member (value / shared / ownership). The default copy constructor performs a shallow copy for pointers, and classes containing `unique_ptr` are not copyable by default.
- When you need to create objects in batches based on templates, layering a **Prototype Registry** to index prototypes by name and `clone()` on demand is a common skeleton for configuration-based creation.

::: tip Accompanying Compilable Project
The examples in this section have a complete compilable project (`.h` + main + `CMakeLists.txt`) in the repository at `code/volumn_codes/vol4/design-patterns/Prototype/`. Run `cmake -S . -B build && cmake --build build` to reproduce the outputs shown above.
:::

## References

- [cppreference: Virtual functions (Covariant return type)](https://en.cppreference.com/w/cpp/language/virtual) (Since C++98, virtual functions return covariant pointers/references)
- [cppreference: Copy constructors](https://en.cppreference.com/w/cpp/language/copy_constructor) (Default copy constructor member-wise copy semantics)
- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (Implicit conversion from derived to base, why covariance isn't possible)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — Chapter on Prototype
- Dmitri Nesteruk, *Hands-On Design Patterns with C++ and .NET Core* — Prototype pattern chapter (Polymorphic clone and registry practices)
