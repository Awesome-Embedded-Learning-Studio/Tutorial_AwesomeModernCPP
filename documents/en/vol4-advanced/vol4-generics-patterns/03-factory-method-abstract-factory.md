---
title: 'Factory Method and Abstract Factory: From a Single Switch to Creating a Family of Products'
description: 'Starting from the most intuitive version — a switch at the call site that news up different objects — we squeeze out the simple factory and the factory method step by step, then reach the abstract factory, get clear on what problem each one actually solves, and finish with the functional factory as a lighter, modern alternative'
chapter: 11
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 工厂模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 24
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - 'Chapter 6: Classes and Object-Oriented Programming'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/03-factory-method-abstract-factory.md
  source_hash: 795e3cee99e366cfbb01b154001c32709243628fc8526e52fdc03f49181648d7
  translated_at: '2026-09-26T05:04:02+00:00'
  engine: anthropic
  token_count: 7000
---
# Factory Method and Abstract Factory: From a Single Switch to Creating a Family of Products

## What Problem Are We Actually Solving

Let's not rush into class diagrams. Picture a scenario you have almost certainly written something like — your author is a bit hungry, so let's use burgers as the example:

There is an abstract base class `Burger` in the program, with several concrete subclasses hanging under it — `CheeseBurger`, `BeefBurger`, `ChickenBurger`. Now the business layer has to produce a concrete burger based on some preference (chosen by the user, read from a config file, looked up in a database) and then eat it. The most intuitive way to write this looks like:

```cpp
void enjoy_our_meals(std::vector<Person>& crowds) {
    for (auto& each_person : crowds) {
        Burger* p = nullptr;
        switch (each_person.prefer_type) {
            case BurgerType::Cheese:  p = new CheeseBurger;  break;
            case BurgerType::Beef:    p = new BeefBurger;    break;
            case BurgerType::Chicken: p = new ChickenBurger; break;
            // oh shit, there are still dozens of burgers to add
            // someone will ask who patches the missing smart-pointer part for me — hold on, we're doing design patterns.
        }
        each_person.enjoy_burger(p);
        delete p;
    }
}
```

It runs, but one glance tells you something is off — **the decision of "which concrete subclass to `new`" has been welded into the "eat a meal" function, which has nothing whatsoever to do with that decision**. The meal-eating function should only care about "get a burger, eat it"; instead it now has to know that every kind of burger exists, maintain a `switch`, and manage `new` and `delete`. Add one product and this `switch` has to change; change how a product gets constructed (say it suddenly needs a parameter) and this `switch` has to change again; and the day you want to insert a logging step into the creation process, that logging logic gets copied into every single place that wrote a `switch`.

The essential tension is this: **using an object and creating an object are two jobs whose coupling directions are diametrically opposed**. The user side wants to depend on a stable abstraction (`Burger`) and wants concrete types to appear in its field of view as little as possible; the creating side must know every concrete type, because "which one to `new`" is precisely its job description. Mix the two together, and the user side is forced to inherit all of the creator's volatility — the more products there are, the more bloated the user side gets.

That is exactly what the factory pattern family solves. The core idea in one sentence: **strip the decision of "which concrete object to create" out of the user's code and hand it to a dedicated object/function, so the user side only ever faces the abstraction**. From here we walk forward step by step, starting from the dumbest version, seeing why each step is still not enough, until we finally force out the classic GoF Factory Method and Abstract Factory — plus a lighter functional alternative from modern C++.

## Step One: The Crudest Extraction — the Simple Factory (a Static switch)

We quickly spot the sleight of hand in that code: the `switch` logic has nothing to do with eating a meal, so why not just pull it out?

```cpp
struct SimpleBurgerFactory {
    static std::unique_ptr<Burger> create(BurgerType t) {
        switch (t) {
            case BurgerType::Cheese:  return std::make_unique<CheeseBurger>();
            case BurgerType::Beef:    return std::make_unique<BeefBurger>();
            case BurgerType::Chicken: return std::make_unique<ChickenBurger>();
        }
        return nullptr;
    }
};

void enjoy_our_meals(std::vector<Person>& crowds) {
    for (auto& each_person : crowds) {
        auto burger = SimpleBurgerFactory::create(each_person.prefer_type);
        each_person.enjoy_burger(*burger);
    }
}
```

Look at that: `enjoy_our_meals` is instantly cleaner. It just calls `create`, gets a `Burger`, and eats. **Which concrete subclass it is, the "eating" code never cares about again**. Add a new kind of burger later and the only thing that changes is that one `switch` inside the factory; insert a logging step into every creation and the only thing that changes is that one spot in the factory. While we're at it, we also swapped the raw `new` for `std::unique_ptr`, so ownership is crystal clear — the object is handed to the caller the moment it is created, and the factory does not hold onto it.

That is the **Simple Factory (also called a static factory)**. It fixes the most painful problem — the use/creation coupling — and for the vast majority of scenarios it is enough. Let's first run its behavior through the compiler and confirm this step actually works:

```cpp
#include <iostream>
#include <memory>
#include <vector>

struct Burger {
    virtual ~Burger() = default;
    virtual std::string name() const = 0;
    virtual int price() const = 0;
};
struct CheeseBurger : Burger { std::string name() const override { return "CheeseBurger"; }
                                int price() const override { return 25; } };
struct BeefBurger   : Burger { std::string name() const override { return "BeefBurger"; }
                                int price() const override { return 32; } };
struct ChickenBurger: Burger { std::string name() const override { return "ChickenBurger"; }
                                int price() const override { return 28; } };

enum class BurgerType { Cheese, Beef, Chicken };

struct SimpleBurgerFactory {
    static std::unique_ptr<Burger> create(BurgerType t) {
        switch (t) {
            case BurgerType::Cheese:  return std::make_unique<CheeseBurger>();
            case BurgerType::Beef:    return std::make_unique<BeefBurger>();
            case BurgerType::Chicken: return std::make_unique<ChickenBurger>();
        }
        return nullptr;
    }
};

int main() {
    for (auto t : {BurgerType::Beef, BurgerType::Cheese, BurgerType::Chicken}) {
        auto b = SimpleBurgerFactory::create(t);
        std::cout << "got " << b->name() << ", price=" << b->price() << "\n";
    }
}
```

Compile and run it (GCC 16.1.1, C++23):

```sh
$ g++ -std=c++23 -O2 -Wall simple_factory.cpp -o simple_factory
$ ./simple_factory
got BeefBurger, price=32
got CheeseBurger, price=25
got ChickenBurger, price=28
```

The behavior is exactly right. But the simple factory has a flaw it cannot dodge — **it violates the Open-Closed Principle (OCP)**. That `switch` lives inside the factory, and the factory is omniscient about which concrete products exist; every time you add a new burger (`FishBurger`) you have to **open up the factory class and modify its source code**. The more the factory knows and the more often it gets edited, the more fragile it becomes. What we want is this: adding a new product should ideally not touch the factory at all — only new code gets added, no existing code gets modified. That is the next step's job.

## Step Two: Handing "Which One to Create" Down to Subclasses — the Factory Method

How do we manage "add a product without touching the factory"? The answer: **make the factory itself an abstraction too, and give each product its own concrete factory**. That is where GoF's Factory Method pattern comes from:

```cpp
// Factory interface: only defines "can make a Burger", without dictating which kind
struct BurgerCreator {
    virtual ~BurgerCreator() = default;
    virtual std::unique_ptr<Burger> create() const = 0;
};

// Each product gets its own concrete factory
struct CheeseBurgerCreator : BurgerCreator {
    std::unique_ptr<Burger> create() const override { return std::make_unique<CheeseBurger>(); }
};
struct BeefBurgerCreator : BurgerCreator {
    std::unique_ptr<Burger> create() const override { return std::make_unique<BeefBurger>(); }
};
struct ChickenBurgerCreator : BurgerCreator {
    std::unique_ptr<Burger> create() const override { return std::make_unique<ChickenBurger>(); }
};
```

Used like this — the client holds a `BurgerCreator&` and has no idea which concrete burger gets made:

```cpp
void enjoy(const std::vector<std::unique_ptr<BurgerCreator>>& creators) {
    for (auto& creator : creators) {
        auto burger = creator->create();
        std::cout << "got " << burger->name() << "\n";
    }
}
```

Let's verify here that it really works, and that everything the client gets its hands on is the `Burger` abstraction:

```cpp
int main() {
    std::vector<std::unique_ptr<BurgerCreator>> creators;
    creators.emplace_back(std::make_unique<CheeseBurgerCreator>());
    creators.emplace_back(std::make_unique<BeefBurgerCreator>());
    creators.emplace_back(std::make_unique<ChickenBurgerCreator>());

    int total = 0;
    for (auto& creator : creators) {
        auto burger = creator->create();   // returns unique_ptr<Burger>; the concrete type is erased
        std::cout << "got " << burger->name()
                  << ", price=" << burger->price() << "\n";
        total += burger->price();
    }
    std::cout << "total = " << total << "\n";
}
```

Run it:

```sh
$ g++ -std=c++23 -O2 -Wall factory_method.cpp -o factory_method
$ ./factory_method
got CheeseBurger, price=25
got BeefBurger, price=32
got ChickenBurger, price=28
total = 85
```

### What Exactly Makes the Factory Method Good: the Extensibility Ledger

Let's do its extensibility ledger. **Add a new burger, `FishBurger`**: you add a `FishBurger` class plus a `FishBurgerCreator` class, then at the point of use you stuff the `FishBurgerCreator` into that `vector` — **not a single line of the `BurgerCreator` interface changes, not a single line of any existing `Creator` subclass changes**. That is exactly the "open for extension, closed for modification" that OCP asks for. The simple factory cannot do this, because its `switch` is concentrated inside the factory; the factory method pushes the "which one to create" decision down into independent factory subclasses, so adding a product is just adding a subclass, and no existing code is ever touched again.

This is the essential difference between the factory method and the simple factory, and it is worth burning into memory: **the simple factory is "one factory that knows all the products"; the factory method is "every product has its own factory, and nobody has to be omniscient"**. The former adds a product by editing one place (violating OCP); the latter adds a product by adding a class (satisfying OCP). The price is more classes — one `Creator` subclass per product, so the number of files and types doubles. It is not a free lunch: it trades class count for OCP, specifically on the pain point of "products will keep coming, and we don't want to keep editing the existing factory".

### Compilable Companion Project: What the Factory Method Really Looks Like

::: tip Compilable Companion Project
The factory method above ships as a complete, runnable CMake project in this repository. It uses an example even more fitting than burgers — **`BurgerProvider` is the abstract factory interface, while `McBurgerProvider` and `BurgerKingProvider` are the concrete factories of two burger chains**, each responsible for making its own brand of burgers (the same `create_specifiedBurger("normal"/"cheese")` interface produces completely different branded products in different stores). Clone it, run cmake once, and it just works: [FactoryBaseMethod / BurgerCreator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/BurgerCreator).
:::

Here is an excerpt of its output. Notice how `McBurgerProvider` and `BurgerKingProvider`, despite having byte-for-byte identical interfaces, produce completely different things:

```sh
$ ./BurgerCreator
Grilling McBurger...
Preparing McBurger with lettuce, tomato, and special sauce...
Wrapping McBurger in a paper wrapper...
Grilling McCheeseBurger...
Preparing McCheeseBurger with lettuce, tomato, cheese, and special sauce...
Wrapping McCheeseBurger in a paper wrapper...
Grilling Burger King Cheese Burger...
Wrapping Burger King Cheese Burger in a paper wrapper...
Grilling Burger King Burger...
Wrapping Burger King Burger in a paper wrapper...
```

The design masterstroke of `BurgerProvider` here: the client (`process_burger_session`) calls only the three abstract methods `grill()/prepare()/wrap()`, and it has absolutely no idea whether the burger in its hands is a `McBurger` or a `BurgerKingBurger`; **the brand differences are completely absorbed by the factories, and the client never writes a single `if`**. That is the factory method's value in real business code — the "same set of operations, different concrete implementations" layer of variance gets hidden inside the factory.

## Step Three: Making a Whole Set at Once — the Abstract Factory

The story is not over yet. A burger shop never sells just burgers — it sells combo meals: one burger plus one drink, and those two products must be **style-consistent**: the classic combo is "beef burger + cola", the healthy combo is "chicken burger + juice". You cannot have cola popping up in the healthy combo, and you cannot pair juice with the classic one — **the products inside a combo must belong to the same family**.

If you give each product its own factory method (`BurgerCreator` + `DrinkCreator`), the client has to "assemble a full set" itself, and the assembly logic ends up scattered across the client, with nobody left guarding family consistency. The Abstract Factory pattern plugs exactly that hole — **it bundles the creation interfaces of a family of related products together, with one concrete factory responsible for the whole family**:

```cpp
struct Drink {
    virtual ~Drink() = default;
    virtual std::string name() const = 0;
};
struct Cola  : Drink { std::string name() const override { return "Cola"; } };
struct Juice : Drink { std::string name() const override { return "Juice"; } };

// Abstract factory: the creation interfaces for a family of products, bundled together
struct MealFactory {
    virtual ~MealFactory() = default;
    virtual std::unique_ptr<Burger> create_burger() const = 0;
    virtual std::unique_ptr<Drink>  create_drink()  const = 0;
};

// Classic combo factory: keeps the whole family in the "classic" style
struct ClassicMealFactory : MealFactory {
    std::unique_ptr<Burger> create_burger() const override { return std::make_unique<CheeseBurger>(); }
    std::unique_ptr<Drink>  create_drink()  const override { return std::make_unique<Cola>(); }
};

// Healthy combo factory: keeps the whole family in the "healthy" style
struct HealthyMealFactory : MealFactory {
    std::unique_ptr<Burger> create_burger() const override { return std::make_unique<ChickenBurger>(); }
    std::unique_ptr<Drink>  create_drink()  const override { return std::make_unique<Juice>(); }
};
```

Hand the client a `MealFactory` and it can assemble, in one shot, a combo meal whose **style is guaranteed consistent** — no manual cross-checking needed:

```cpp
void serve_meal(const MealFactory& factory) {
    auto burger = factory.create_burger();
    auto drink  = factory.create_drink();
    std::cout << "serving " << burger->name() << " + " << drink->name() << "\n";
}

int main() {
    ClassicMealFactory classic;
    HealthyMealFactory healthy;
    serve_meal(classic);
    serve_meal(healthy);
}
```

Verify it:

```sh
$ g++ -std=c++23 -O2 -Wall abstract_factory.cpp -o abstract_factory
$ ./abstract_factory
serving CheeseBurger + Cola
serving ChickenBurger + Juice
```

Two things to notice. First, **family consistency is a strong constraint the abstract factory hands you for free**: as long as you go through `ClassicMealFactory`, what comes out is necessarily the "`CheeseBurger` + `Cola`" set — the client never even gets a chance to assemble it wrong. The factory method cannot do this: it can only hide a single product from the client, but it has no structure for expressing the constraint "this group of products shares one style".

Second, the cost of this mechanism shows up immediately: **adding a new product family (say, a "luxury combo") is easy — just add a `LuxuryMealFactory`; but adding a new kind of product (say, combos suddenly need a "dessert" too) is painful** — you have to go back and modify the abstract factory interface `MealFactory` to add a `create_dessert()`, and then **every existing concrete factory** has to go back and supply an implementation. This ledger runs exactly opposite to the factory method's: the abstract factory is open for "add a family" and closed for "add a product kind".

### Factory Method vs Abstract Factory: Don't Be Fooled by the Names

Plenty of material lumps the two together, but the difference between them is actually crisp — one sentence settles it:

- The **Factory Method** cares about making **one** product. The abstract interface has a single `create()`, and the concrete factory decides which concrete product gets made. What it solves is "decoupling creation from use + OCP".
- The **Abstract Factory** cares about making **a family** of products. The abstract interface has multiple `create_xxx()` methods, and the concrete factory decides which family gets made. What it additionally solves is "this family of products is style-consistent".

There is also a small structural fact that, once understood, separates them cleanly for good: **the abstract factory's interface is usually implemented with factory methods** — each of `create_burger()` and `create_drink()` in `MealFactory`, viewed on its own, is a factory method. The abstract factory is not the opposite of the factory method; it is the result of "packing several factory methods into one interface". They are the same idea applied at different scales.

## Step Four: Stop Writing So Many Classes — the Functional Factory

By this point you may be frowning: with the factory method / abstract factory, every new product or family means a new class, and the file count balloons badly. Honestly, most of these `Creator` subclasses contain a single line, `return std::make_unique<...>()` — building a whole inheritance hierarchy for that one line is a bit heavy.

Modern C++ offers a lighter path: **a factory is at heart just "a function that can make an object", so drop the class and use `std::function`/lambdas directly**. We maintain a table, registering "key → object-making function" entries into it:

```cpp
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

struct FunctionalBurgerFactory {
    using Creator = std::function<std::unique_ptr<Burger>()>;

    static void register_creator(const std::string& key, Creator c) {
        registry()[key] = std::move(c);
    }

    static std::unique_ptr<Burger> create(const std::string& key) {
        auto it = registry().find(key);
        if (it == registry().end()) return nullptr;   // key doesn't exist; safely return empty
        return (it->second)();
    }

private:
    static std::unordered_map<std::string, Creator>& registry() {
        static std::unordered_map<std::string, Creator> r;  // a Meyer's Singleton holds the registry
        return r;
    }
};
```

Registration and use are both plain lambdas — no inheritance, no class explosion:

```cpp
// Registration phase: each new burger registers one lambda here
FunctionalBurgerFactory::register_creator("cheese",  [] { return std::make_unique<CheeseBurger>(); });
FunctionalBurgerFactory::register_creator("beef",    [] { return std::make_unique<BeefBurger>(); });
FunctionalBurgerFactory::register_creator("chicken", [] { return std::make_unique<ChickenBurger>(); });

// Usage phase: fetch a product by key
auto b  = FunctionalBurgerFactory::create("beef");
auto mx = FunctionalBurgerFactory::create("nope");   // a nonexistent key
```

Let's verify it here, with special attention to what happens on a nonexistent key:

```cpp
#include <iostream>
// ... FunctionalBurgerFactory definition + registration of the three products ...

int main() {
    FunctionalBurgerFactory::register_creator("cheese",  [] { return std::make_unique<CheeseBurger>(); });
    FunctionalBurgerFactory::register_creator("beef",    [] { return std::make_unique<BeefBurger>(); });
    FunctionalBurgerFactory::register_creator("chicken", [] { return std::make_unique<ChickenBurger>(); });

    auto b  = FunctionalBurgerFactory::create("beef");
    auto mx = FunctionalBurgerFactory::create("nope");
    std::cout << "'beef' -> " << (b  ? b->name() : std::string{"null"}) << "\n";
    std::cout << "'nope' -> " << (mx ? mx->name() : std::string{"null"}) << "\n";
}
```

Run it:

```sh
$ g++ -std=c++23 -O2 -Wall functional_factory.cpp -o functional_factory
$ ./functional_factory
'beef' -> BeefBurger
'nope' -> null
```

For the nonexistent key `'nope'`, `create` returned `nullptr` instead of crashing — that is the standard behavior of `std::unordered_map::find` returning `end()` on a miss, and we chose to return a null pointer on that basis. **This happens at runtime, not compile time**, and it is a very real cost of the functional factory compared to the factory method; we'll come back to it specifically below.

### Compilable Companion Project: a Tidier Notification System

::: tip Compilable Companion Project
This repository contains a notification system built on the functional factory, worth a look on its own. Its registry is the member `unordered_map<string, std::function<unique_ptr<AbstractNocification>()>>` of `NocificationCreator`; at initialization it registers one lambda per notifier — `Email/SMS/Push` — and a `notification_creator("Email")` lookup pulls out the corresponding implementation. Clone it and it runs in one shot: [FactoryBaseMethod / NotificationSystem](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/NotificationSystem).
:::

Its output is:

```sh
$ ./NotificationSystem
[Email]: Welcome to our platform![SMS]: Welcome to our platform![Push]: Hey, New Message here!
```

See how the client has no idea the three concrete classes `Email`, `SMS`, `Push` even exist — it only talks to `AbstractNocification`'s `send_message`. Add a new notifier (`Webhook`), and all it takes is one more lambda in the registry — **the `NocificationCreator` class itself and the way `main` calls it change not one line**. That is the functional factory taking OCP to its lightest form.

## Let's Verify: the Extensibility Gaps Among the Three Factories Are Not Just Talk

Talk is cheap, so let's lay the three factories side by side and compare the blast radius of "adding one new product". This is a structural fact, but to make it concrete we write a minimal contrast: with the factory method, how many places of existing code does adding `FishBurger` need to touch?

```cpp
#include <iostream>
#include <memory>
#include <vector>

struct Burger {
    virtual ~Burger() = default;
    virtual std::string name() const = 0;
};
struct CheeseBurger : Burger { std::string name() const override { return "CheeseBurger"; } };
struct BeefBurger   : Burger { std::string name() const override { return "BeefBurger"; } };
// Key point: when adding FishBurger, this line is an addition, not a modification of existing code
struct FishBurger   : Burger { std::string name() const override { return "FishBurger"; } };

struct BurgerCreator {
    virtual ~BurgerCreator() = default;
    virtual std::unique_ptr<Burger> create() const = 0;
};
struct CheeseBurgerCreator : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<CheeseBurger>(); } };
struct BeefBurgerCreator   : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<BeefBurger>(); } };
// New FishBurgerCreator: still an addition — the existing Creator subclasses and the BurgerCreator interface stay untouched
struct FishBurgerCreator   : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<FishBurger>(); } };

int main() {
    std::vector<std::unique_ptr<BurgerCreator>> creators;
    creators.emplace_back(std::make_unique<CheeseBurgerCreator>());
    creators.emplace_back(std::make_unique<BeefBurgerCreator>());
    creators.emplace_back(std::make_unique<FishBurgerCreator>());   // one more line at the assembly point
    for (auto& c : creators) std::cout << c->create()->name() << "\n";
}
```

Compile and run it:

```sh
$ g++ -std=c++23 -O2 -Wall ocp_check.cpp -o ocp_check
$ ./ocp_check
CheeseBurger
BeefBurger
FishBurger
```

This little snippet nails down the factory method's OCP ledger: **across the whole process of adding `FishBurger`, the `BurgerCreator` abstract interface did not change, and neither did the two existing factories `CheeseBurgerCreator` and `BeefBurgerCreator`** — we only added the two classes `FishBurger` and `FishBurgerCreator`, plus one line at the assembly point in `main`. Contrast the simple factory, where adding `FishBurger` forces you to edit the `switch` inside the factory class; contrast the abstract factory, where if `FishBurger` were a new product kind rather than a new family, you would still have to change the interface and all the concrete factories. That is how concrete and how quantifiable the differences in blast radius are across the three when it comes to "adding a product".

## The Other Face of the Factory Pattern: Centralized Tracking of Creation

Everything so far has been about decoupling. But the factory pattern has another frequently overlooked benefit: **since all creation is concentrated in the factory, the factory is a natural auditing point for objects**. Logging, counting, or timing the creation process only requires changing the factory in one place, instead of scattering into every `switch`:

```cpp
struct TracingBurgerFactory {
    static std::unique_ptr<Burger> create(BurgerType t) {
        auto start = std::chrono::steady_clock::now();
        auto burger = SimpleBurgerFactory::create(t);   // delegate to the real factory
        auto end   = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cerr << "[factory] created " << burger->name()
                  << " in " << us << " us\n";
        return burger;
    }
};
```

This "wrap another layer around the factory for cross-cutting concerns" idiom is, in essence, the decorator/proxy pattern stacked on top of a factory. Its precondition is precisely that "creation has already been centralized" — if you are still writing `switch` statements all over the call sites, this kind of unified tracking has nowhere to start. So the factory pattern is not merely about "letting the client write fewer `switch` statements"; it also hands you a **choke point that every object passes through at birth**, where permission checks, monitoring, caching, and object pools can all be hooked in.

## Which One to Choose: a Decision Table

We have walked all the way from the simple factory to the functional factory; now let's lay them side by side and see clearly what each is good for:

| Dimension | Simple factory | Factory method | Abstract factory | Functional factory |
|---|---|---|---|---|
| What it makes | a single product | a single product | **a family of products** | a single product (by key) |
| Adding a new product | edit the factory's `switch` (violates OCP) | add a `Creator` subclass (satisfies OCP) | change the interface + all concrete factories (costly) | add one lambda to the registry |
| Adding a new family | — | — | add a concrete factory class (satisfies OCP) | — |
| Guarantees family consistency | No | No | **Yes (a structural hard constraint)** | No |
| Type safety | compile time (a missed `switch` case warns) | compile time (pure virtual forces implementation) | compile time (pure virtual forces implementation) | **runtime** (a mistyped key only blows up at runtime) |
| Class overhead | one factory class | one factory subclass per product | one factory subclass per family | almost no new classes |

How to choose? I'll compress the decision logic into a few sentences. **For the vast majority of "new different subclasses by condition" needs, the simple factory is enough** — don't over-engineer. **When products will keep arriving and you don't want to revisit the factory's source every time one is added, go factory method**, trading class count for OCP. **When what you are making is a family of products that must be style-consistent (combo meals, cross-platform UI widgets, multiple database dialects), go abstract factory** — its killer feature is the structural constraint of family consistency. **When your creation logic is light, you want OCP, and you don't want to raise a brood of one-line subclasses, go functional factory**, reducing the factory to a `key → lambda` registry — but stay clear-eyed about its demoted type safety: a wrong key is only discovered at runtime.

::: warning The functional factory's type safety is demoted
The factory method / abstract factory are type-safe at compile time: `BurgerCreator::create()` is a pure virtual function — forget to implement it in some concrete factory, and that factory becomes abstract, impossible to instantiate, and the compiler stops you on the spot. The functional factory does not work that way — its key is a string. Type `create("beed")` instead of `create("beef")`, and nothing is caught at compile time; it waits until runtime, when `find` misses and returns `nullptr`, and then it crashes on you the moment you dereference it. So on the "type safety" line the functional factory **genuinely drops one tier**; if you use it, the calling side has to honestly handle "the key may not exist" (check whether the returned `unique_ptr` is null, or simply throw). If your keys come from external input (config files, network requests), this check is absolutely not optional.
:::

## Pitfall Warnings: a Few Small Traps in the Details

::: warning The factory must return `unique_ptr<base class>`, not a raw pointer or `unique_ptr<derived class>`
Having the factory method return `std::unique_ptr<Burger>` (the base class) is deliberate. First, **don't return a raw `Burger*`** — the caller has to remember to `delete` it themselves; one slip and it's a memory leak, and returning a raw pointer blurs the ownership semantics (who owns this object?). `std::make_unique` + `unique_ptr<Burger>` cleanly transfers ownership to the caller, with RAII doing the recycling. Second, **`std::make_unique<CheeseBurger>()` can implicitly convert into `unique_ptr<Burger>`** because `unique_ptr` has a converting constructor template for compatible pointer types — but the reverse (`unique_ptr<Burger>` into `unique_ptr<CheeseBurger>`) does not work, so what the factory returns must be the base-class pointer. Third, **the base class `Burger` must have a `virtual` destructor** (`virtual ~Burger() = default;`), otherwise deleting a derived object through a base-class pointer is undefined behavior — we hammered on this repeatedly in the singleton and visitor pieces, and it holds just as much under factory patterns, because a `unique_ptr<Burger>` destroys its object through the base-class pointer at destruction time.
:::

::: warning The abstract factory's "family consistency" is not "freedom to combine products"
The abstract factory bundles the creation of a family of products together; the benefit is "hand me a concrete factory, and whatever comes out is guaranteed to be one coherent style", but the cost is **it locks down your freedom to combine products**. Suppose you want a mix-and-match like "the classic combo's burger + the healthy combo's drink" — the abstract factory's structure does not support it: you can only pick one factory and take its whole set. If your business needs product-level free combination, the abstract factory is not the right choice; you should go back to "one factory method per product" and let the client do the assembling, at the price that the family-consistency constraint is now guaranteed by discipline instead of by structure. Don't reach for the abstract factory the moment you see "a family of products" — first ask yourself: do I want "the whole set consistent" or "free mix-and-match"?
:::

::: tip Combine the factory registry with a function-local static, and initialization is thread-safe
For the functional factory's registry we used the `static std::unordered_map<...>& registry()` paired with a Meyer's Singleton (a `static` local inside the function). That means the registry's own initialization is thread-safe — C++11 magic statics guarantee that "if several threads first hit this declaration simultaneously, only one of them runs the initializer". But note: **thread-safe initialization of the registry is not the same as thread-safe reads and writes of its contents**. If registration happens after program startup, with multiple threads calling `register_creator` concurrently, you still need to lock the registry (`std::shared_mutex` suits the read-heavy, write-rare scenario). The vast majority of factory registrations happen during `main`'s startup phase (single-threaded), where no lock is needed; the moment registration is deferred to runtime, the lock has to be added. This and the magic statics discussed in the singleton piece are two faces of the same coin.
:::

## Factory vs Builder: Don't Pick the Wrong One

This sub-volume of ours also covers the Builder pattern. Factories and builders both attack the "object creation" problem, beginners easily pick the wrong one, so let's nail the difference down here:

- The **factory** cares about **which one to make** — return a **different** concrete subclass depending on a condition; the object itself is relatively simple and gets made in one step.
- The **builder** cares about **how to make it** — unroll the construction of a **complex** object into steps (a pile of chained `set_xxx()` calls, then `build()` at the end); the object's type is fixed, but the configuration options are legion.

A simple test: if your agony is "which subclass should I `new`", use a factory; if your agony is "this object has a dozen optional parameters, how do I configure them legibly", use a builder. The two also combine — an abstract factory can return a builder, giving the client both decoupling from the concrete type and step-by-step configuration.

## Summary

Let's trace the whole evolutionary path once more:

| Stage | Approach | Why it's still not enough |
|---|---|---|
| `switch` + `new` at the call site | the user directly `switch`es to decide which one to `new` | creation and use are coupled; the user knows every concrete type |
| Simple factory | pull the `switch` into a static factory method | adding a product means editing the factory's internals (violates OCP) |
| Factory method | abstract factory interface + one concrete factory per product | good enough decoupling for a single product, but it cannot make a family |
| Abstract factory | bundle a family's creation into one interface | adding a family is easy, but adding a product kind means changing the interface and all concrete factories |
| Functional factory | a `key → lambda` registry | type safety demoted (runtime table lookup) |

Note down these key conclusions:

- The factory pattern solves the coupling between **creating an object and using an object** — peel "which concrete subclass to `new`" away from the user, hand it to a dedicated factory, and the user faces only the abstract base class.
- The **simple factory** (a `switch` inside one static method) fixes the most painful coupling, but violates OCP — adding a product means editing the factory's internals.
- The **factory method** (abstract `Creator` + one concrete `Creator` per product) trades class count for OCP — adding a product only adds classes, never modifies existing code.
- The **abstract factory** bundles the creation of a family of related products; its killer feature is **the structural hard constraint of family consistency** (everything one factory produces is necessarily one coherent style); the cost is that adding a product kind means changing the interface and all concrete factories.
- In modern code, reach first for the **functional factory**: a `key → lambda` registry that makes OCP nearly weightless and adds almost no classes; but its type safety is demoted to runtime (a mistyped key only blows up then), so the calling side must handle "key not found".
- And don't forget the factory's other dividend: **it is the choke point every object passes through at birth** — logging, counting, permissions, caching, object pools, all these cross-cutting concerns need hooking in only once, right here.

::: tip Compilable Companion Project
The two complete CMake projects for this section live in this repository — clone, run cmake once, and they go: [BurgerCreator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/BurgerCreator) built on the factory method (two chains' concrete factories, each making its own brand of burgers), and [NotificationSystem](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/NotificationSystem) built on the functional factory (a `key → lambda` registry dispatching Email/SMS/Push).
:::

## References

- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) — since C++11, the standard vehicle for transferring ownership of a factory's return value
- [cppreference: `std::function`](https://en.cppreference.com/w/cpp/utility/functional/function) — since C++11, the value type of a functional factory's registry
- [cppreference: Virtual destructors](https://en.cppreference.com/w/cpp/language/destructor#Virtual_destructor) — when the factory returns base-class pointers, the base destructor must be `virtual`
- [refactoring.guru: Factory Method](https://refactoring.guru/design-patterns/factory-method) / [Abstract Factory](https://refactoring.guru/design-patterns/abstract-factory) — illustrated walkthroughs of the GoF factory patterns
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — the original definitions of Factory Method and Abstract Factory
- Compilable companion projects: [FactoryBaseMethod](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory)
