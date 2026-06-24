---
title: 'Factory Method and Abstract Factory: From a Single Switch to Creating a Family
  of Products'
description: Starting from the most intuitive approach of "writing a switch statement
  at the call site to new different objects", we progressively derive the simple factory
  and factory method patterns, leading to the abstract factory. We clarify the specific
  problems each pattern solves, and finally, we present a lightweight, modern alternative
  using functional factories.
chapter: 11
order: 3
tags:
- host
- cpp-modern
- intermediate
- 工厂模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 24
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/03-factory-method-abstract-factory.md
  source_hash: 795e3cee99e366cfbb01b154001c32709243628fc8526e52fdc03f49181648d7
  translated_at: '2026-06-24T00:54:00.475527+00:00'
  engine: anthropic
  token_count: 5476
---
# Factory Method and Abstract Factory: From a Switch Statement to Creating Families of Products

## What problem are we actually solving?

Let's hold off on the class diagrams for a moment. Imagine a scenario you have likely written before. Since the author is a bit hungry, let's use burgers as an example:

In the program, there is an abstract base class `Burger`, with several concrete subclasses derived from it—`CheeseBurger`, `BeefBurger`, and `ChickenBurger`. Now, the business layer needs to create a specific burger based on some preference (selected by the user, read from a configuration file, or retrieved from a database), and then consume it. The most intuitive implementation looks like this:

```cpp
void enjoy_our_meals(std::vector<Person>& crowds) {
    for (auto& each_person : crowds) {
        Burger* p = nullptr;
        switch (each_person.prefer_type) {
            case BurgerType::Cheese:  p = new CheeseBurger;  break;
            case BurgerType::Beef:    p = new BeefBurger;    break;
            case BurgerType::Chicken: p = new ChickenBurger; break;
            // oh shit, 还有几十种汉堡要加
            // 有人会问我缺的智能指针这块谁给我补啊，我说别急，讲设计模式呢。
        }
        each_person.enjoy_burger(p);
        delete p;
    }
}
```

It runs, but one look tells you something is wrong—**the decision of "which concrete subclass to `new`" is hardcoded into the `eat` function, which has absolutely nothing to do with that decision**. The `eat` function should only care about "getting a burger and eating it," but now it has to know about every type of burger, maintain a `switch` statement, and manage `new` and `delete`. Every time a product is added, this `switch` statement must change; if the construction method changes (e.g., suddenly requiring a parameter), this `switch` statement must change; and if one day you want to insert logging into the creation process, that logic will have to be copied into every place where a `switch` is written.

The fundamental contradiction lies here: **"using an object" and "creating an object" are two things with completely opposite coupling directions**. The consumer only wants to depend on a stable abstraction (`Burger`) and prefers concrete types to appear as little as possible in its scope; however, the creator must know every concrete type because "which one to `new`" is precisely its job. Mixing these two things forces the consumer to inherit all the volatility of the creator—the more products there are, the more bloated the consumer becomes.

The Factory Pattern aims to solve exactly this. Its core principle is simple: **extract the decision of "which concrete object to create" from the consumer and delegate it to a dedicated object or function, allowing the consumer to interact only with the abstraction**. Next, we will walk through this step-by-step, starting with the dumbest approach, seeing why each step isn't enough, and finally deriving the GoF classic "Factory Method" and "Abstract Factory," as well as a lighter functional alternative in modern C++.

## Step 1: The Most Primitive Extraction — Simple Factory (Static `switch`)

We quickly spotted the quirk in the code above: the `switch` logic has nothing to do with "eating," so why not just extract it?

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

Look, `enjoy_our_meals` is instantly cleaner: it simply calls `create`, retrieves a `Burger`, and eats it. **The code for "eating" no longer cares which specific subclass is involved.** If we need to add a new burger later, we only modify the `switch` statement inside the factory. If we need to inject logging into all creation steps, we only modify the factory itself. We also conveniently replaced the bare `new` with `std::unique_ptr`, making ownership crystal clear—once created, it is handed to the caller, and the factory holds no reference to it.

This is the **Simple Factory**. It solves the painful problem of "coupling between usage and creation," and in most scenarios, it is sufficient. Let's compile and run this behavior to verify that this step actually works:

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

Let's compile and run it (GCC 16.1.1, C++23):

```sh
$ g++ -std=c++23 -O2 -Wall simple_factory.cpp -o simple_factory
$ ./simple_factory
got BeefBurger, price=32
got CheeseBurger, price=25
got ChickenBurger, price=28
```

The behavior is completely correct. However, the Simple Factory has an unavoidable drawback—**it violates the Open/Closed Principle (OCP)**. That `switch` statement is hardcoded inside the factory; the factory has full knowledge of the "existence of specific products." Every time we add a new burger (like `FishBurger`), we have to **open the factory class and modify its source code**. The more the factory knows and the more frequently we change it, the more fragile it becomes. What we want is this: when adding a new product, we shouldn't have to touch the factory at all; we should only add new code without modifying existing code. This is what we will solve next.

## Step 2: Delegate "Which to Create" to Subclasses — Factory Method

How do we achieve "add products without modifying the factory"? The answer is to **make the factory itself an abstraction, where each product is paired with its own specific concrete factory**. This is exactly how the GoF (Gang of Four) Factory Method pattern comes about:

```cpp
// 工厂接口:只定义「能造一个 Burger」,不规定造哪种
struct BurgerCreator {
    virtual ~BurgerCreator() = default;
    virtual std::unique_ptr<Burger> create() const = 0;
};

// 每种产品配一个具体工厂
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

Here is how we use it. The client holds a `BurgerCreator&`, and has no idea what specific type of burger is being created:

```cpp
void enjoy(const std::vector<std::unique_ptr<BurgerCreator>>& creators) {
    for (auto& creator : creators) {
        auto burger = creator->create();
        std::cout << "got " << burger->name() << "\n";
    }
}
```

Let's first verify that it actually works, and that the client receives only the `Burger` abstraction:

```cpp
int main() {
    std::vector<std::unique_ptr<BurgerCreator>> creators;
    creators.emplace_back(std::make_unique<CheeseBurgerCreator>());
    creators.emplace_back(std::make_unique<BeefBurgerCreator>());
    creators.emplace_back(std::make_unique<ChickenBurgerCreator>());

    int total = 0;
    for (auto& creator : creators) {
        auto burger = creator->create();   // 返回 unique_ptr<Burger>,具体类型被擦除
        std::cout << "got " << burger->name()
                  << ", price=" << burger->price() << "\n";
        total += burger->price();
    }
    std::cout << "total = " << total << "\n";
}
```

It appears you have provided only the phrase "跑出来:" (Run out / Output:), but the actual content to be translated is missing.

Please provide the Markdown text or code output you would like me to translate. I am ready to apply the technical translation rules and terminology guide as soon as you share the content.

```sh
$ g++ -std=c++23 -O2 -Wall factory_method.cpp -o factory_method
$ ./factory_method
got CheeseBurger, price=25
got BeefBurger, price=32
got ChickenBurger, price=28
total = 85
```

### The Real Benefit of the Factory Method: The Extensibility Ledger

Let's calculate its extensibility ledger. **To add a new burger type `FishBurger`**: you add the `FishBurger` class + the `FishBurgerCreator` class, and then insert `FishBurgerCreator` into that `vector` at the usage point — **you don't need to change a single line of the `BurgerCreator` interface, nor any existing `Creator` subclass**. This is exactly what the Open/Closed Principle (OCP) envisions: "open for extension, closed for modification." The Simple Factory cannot achieve this because its `switch` logic is centralized inside the factory; the Factory Method delegates the decision of "which to create" to individual factory subclasses. Thus, adding a product simply means adding a subclass, without touching any existing code.

This is the essential difference between the Factory Method and the Simple Factory, and it is worth remembering: **The Simple Factory is "one factory knows all products," while the Factory Method is "each product has its own factory, and no one needs to know everything."** The former requires modifying one place to add a product (violating OCP), while the latter requires adding one class to add a product (conforming to OCP). The cost is that the Factory Method involves more classes — each product requires a matching `Creator` subclass, doubling the number of files and types. So, it isn't a free lunch; rather, it trades class quantity for OCP compliance to address the specific pain point of "products will continuously increase, and we don't want to frequently modify existing factories."

### Companion Compilable Project: The Real Face of the Factory Method

::: tip Companion Compilable Project
The Factory Method logic discussed above is available as a complete, runnable CMake project in this repository. It uses an even more fitting example than burgers — **`BurgerProvider` is the abstract factory interface, while `McBurgerProvider` and `BurgerKingProvider` are concrete factories for two chains**, each responsible for making their own brand of burgers (sharing the same `create_specifiedBurger("normal"/"cheese")` interface, but producing completely different branded products in different shops). Clone it and run it with CMake: [FactoryBaseMethod / BurgerCreator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/BurgerCreator).
:::

Let's look at its output. Notice that while `McBurgerProvider` and `BurgerKingProvider` have identical interfaces, the objects they create are completely different:

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

The highlight of the `BurgerProvider` design lies here: the client (`process_burger_session`) only calls three abstract methods: `grill()`, `prepare()`, and `wrap()`. It is completely unaware of whether the burger in hand is a `McBurger` or a `BurgerKingBurger`. **The brand differences are completely absorbed by the factory, and the client didn't write a single `if` statement.** This is the value of the Factory Method in real-world business logic—hiding the "same operations, different concrete implementations" layer of variation inside the factory.

## Step 3: Creating a Whole Set at Once — Abstract Factory

We aren't done yet. A burger shop never sells just burgers; it sells meals—a burger plus a drink—and these two products must **share a consistent style**: a classic meal is "beef burger + cola", while a healthy meal is "chicken burger + juice". You can't have cola popping up in a healthy meal, nor can you pair juice with a classic meal—**products within a meal must belong to the same family**.

If you assign a separate factory method to each product (`BurgerCreator` + `DrinkCreator`), the client becomes responsible for "putting a set together". The assembly logic is then scattered across the client, and no one is explicitly "guarding" family consistency. The Abstract Factory pattern exists to plug this hole—**it bundles the creation interfaces for a whole family of related products, where one concrete factory is responsible for the entire family**:

```cpp
struct Drink {
    virtual ~Drink() = default;
    virtual std::string name() const = 0;
};
struct Cola  : Drink { std::string name() const override { return "Cola"; } };
struct Juice : Drink { std::string name() const override { return "Juice"; } };

// 抽象工厂:一族产品的创建接口打包在一起
struct MealFactory {
    virtual ~MealFactory() = default;
    virtual std::unique_ptr<Burger> create_burger() const = 0;
    virtual std::unique_ptr<Drink>  create_drink()  const = 0;
};

// 经典套餐工厂:整套家族保持「经典」风格
struct ClassicMealFactory : MealFactory {
    std::unique_ptr<Burger> create_burger() const override { return std::make_unique<CheeseBurger>(); }
    std::unique_ptr<Drink>  create_drink()  const override { return std::make_unique<Cola>(); }
};

// 健康套餐工厂:整套家族保持「健康」风格
struct HealthyMealFactory : MealFactory {
    std::unique_ptr<Burger> create_burger() const override { return std::make_unique<ChickenBurger>(); }
    std::unique_ptr<Drink>  create_drink()  const override { return std::make_unique<Juice>(); }
};
```

Once the client obtains a `MealFactory`, it can assemble a complete meal with a **guaranteed consistent style** in one go, without needing to verify the details manually.

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

Notice two things. First, **"family consistency" is a strong constraint that the Abstract Factory gives you for free**: as long as you use `ClassicMealFactory`, the result is guaranteed to be the "`CheeseBurger` + `Cola`" set. The client has no chance to mix and match incorrectly. This is something the Factory Method cannot achieve—it only ensures that individual product creation is hidden from the client, but it lacks the structure to express the constraint that "this group of products belongs to the same style."

Second, the cost of this mechanism is immediately apparent: **adding a new product family (like a "Luxury Meal") is easy; just add a new `LuxuryMealFactory`. However, adding a new product type (like suddenly needing to add a "Dessert" to the meal) is troublesome**—you have to go back and modify the abstract factory interface `MealFactory` to add a `create_dessert()`, and then **every existing concrete factory** must be updated to implement it. This trade-off is the exact opposite of the Factory Method: the Abstract Factory is open to "adding families" but closed to "adding product types."

### Factory Method vs. Abstract Factory: Don't Let the Names Fool You

Many resources discuss these two patterns together, but their distinction is actually very clean and can be summarized in one sentence:

- **Factory Method** focuses on **creating "one" product**. The abstract interface contains only one `create()`, and the concrete factory decides which specific product to build. It solves "decoupling creation from use + OCP."
- **Abstract Factory** focuses on **creating "a family" of products**. The abstract interface contains multiple `create_xxx()` methods, and the concrete factory decides which family to build. It additionally solves the problem of "consistent style across this family of products."

There is also a small structural fact that, once understood, helps you completely distinguish them: **the Abstract Factory interface is usually implemented using Factory Methods**—`create_burger()` and `create_drink()` inside `MealFactory`, if viewed individually, are each Factory Methods. The Abstract Factory is not the opposite of the Factory Method; rather, it is the result of "packaging several Factory Methods into a single interface." They are applications of the same concept at different scales.

## Step 4: Don't Write So Many Classes—Functional Factories

At this point, you might frown: Factory Method/Abstract Factory requires adding a new class for every product/family addition, causing serious file bloat. Honestly, most of these `Creator` subclasses contain only one line: `return std::make_unique<...>()`. Building an entire inheritance hierarchy just for that one line is a bit heavy.

Modern C++ offers a lighter path: **a factory is essentially a "function that creates objects," so don't use a class; use `std::function`/lambdas directly**. We maintain a registry (table) where we map "key → object creation function":

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
        if (it == registry().end()) return nullptr;   // key 不存在,安全地返回空
        return (it->second)();
    }

private:
    static std::unordered_map<std::string, Creator>& registry() {
        static std::unordered_map<std::string, Creator> r;  // Meyer's Singleton 持有注册表
        return r;
    }
};
```

We write lambdas directly for both registration and usage, avoiding inheritance and class explosion:

```cpp
// 注册阶段:每加一种汉堡,这里登记一条 lambda
FunctionalBurgerFactory::register_creator("cheese",  [] { return std::make_unique<CheeseBurger>(); });
FunctionalBurgerFactory::register_creator("beef",    [] { return std::make_unique<BeefBurger>(); });
FunctionalBurgerFactory::register_creator("chicken", [] { return std::make_unique<ChickenBurger>(); });

// 使用阶段:按 key 拿产品
auto b  = FunctionalBurgerFactory::create("beef");
auto mx = FunctionalBurgerFactory::create("nope");   // 不存在的 key
```

Let's verify this here, focusing on what happens with a non-existent key:

```cpp
#include <iostream>
// ... FunctionalBurgerFactory 定义 + 三个产品的注册 ...

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

It appears you have provided only the phrase "跑出来:" (Run out / Output:), but the actual content or code output is missing.

Please provide the text, code, or documentation you would like me to translate. I am ready to apply the translation rules and terminology reference as soon as you share the content.

```sh
$ g++ -std=c++23 -O2 -Wall functional_factory.cpp -o functional_factory
$ ./functional_factory
'beef' -> BeefBurger
'nope' -> null
```

For the non-existent key `'nope'`, `create` returned `nullptr` without crashing. This aligns with the standard behavior of `std::unordered_map::find`, which returns `end()` when a key is not found, allowing us to return a null pointer accordingly. **This check happens at runtime, not at compile time**. This represents a tangible cost of the functional factory compared to the factory method, a topic we will discuss in detail shortly.

### Accompanying Compilable Project: A Cleaner Notification System

::: tip Accompanying Compilable Project
In this repository, there is a notification system implemented using a functional factory that is worth a look. Its registry is a member `unordered_map<string, std::function<unique_ptr<AbstractNocification>()>>` of `NocificationCreator`. During initialization, lambdas for the three notifier types—`Email`, `SMS`, and `Push`—are registered. A simple lookup like `notification_creator("Email")` retrieves the corresponding implementation. You can clone it and run it immediately: [FactoryBaseMethod / NotificationSystem](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/NotificationSystem).
:::

Its output is:

```sh
$ ./NotificationSystem
[Email]: Welcome to our platform![SMS]: Welcome to our platform![Push]: Hey, New Message here!
```

Look, the client is completely unaware of the existence of the concrete classes `Email`, `SMS`, or `Push`—it only interacts with `send_message` from `AbstractNotification`. To add a new notifier (`Webhook`), we simply add a lambda to the registry. **Neither the `NotificationCreator` class itself nor the invocation style in `main` requires a single line of change.** This demonstrates how a functional factory implements the Open/Closed Principle (OCP) with the least overhead.

## Let's verify this: the extensibility differences of the three factories are not just theoretical

Talk is cheap. Let's map out the scope of changes required for the three factory patterns when "adding a new product" to demonstrate the difference. This is a structural fact, but to make it concrete, we will write a minimal comparison: when using the Factory Method pattern, how many places in the existing code must we touch to add a `FishBurger`?

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
// 关键:新增 FishBurger 时,下面这行是「新增」,不是「修改既有」
struct FishBurger   : Burger { std::string name() const override { return "FishBurger"; } };

struct BurgerCreator {
    virtual ~BurgerCreator() = default;
    virtual std::unique_ptr<Burger> create() const = 0;
};
struct CheeseBurgerCreator : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<CheeseBurger>(); } };
struct BeefBurgerCreator   : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<BeefBurger>(); } };
// 新增 FishBurgerCreator:依然是「新增」,既有的 Creator 子类和 BurgerCreator 接口都没动
struct FishBurgerCreator   : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<FishBurger>(); } };

int main() {
    std::vector<std::unique_ptr<BurgerCreator>> creators;
    creators.emplace_back(std::make_unique<CheeseBurgerCreator>());
    creators.emplace_back(std::make_unique<BeefBurgerCreator>());
    creators.emplace_back(std::make_unique<FishBurgerCreator>());   // 装配点加一行
    for (auto& c : creators) std::cout << c->create()->name() << "\n";
}
```

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -Wall ocp_check.cpp -o ocp_check
$ ./ocp_check
CheeseBurger
BeefBurger
FishBurger
```

This short section confirms the Open/Closed Principle (OCP) ledger for the Factory Method: **during the process of adding `FishBurger`, the `BurgerCreator` abstract interface remained unchanged, and the two existing factories, `CheeseBurgerCreator` and `BeefBurgerCreator`, remained unchanged**—we only added two new classes, `FishBurger` and `FishBurgerCreator`, and added one line at the assembly point in `main`. Compared to the Simple Factory, adding `FishBurger` would require modifying the `switch` statement inside the factory class; compared to the Abstract Factory, if `FishBurger` is a new "product kind" rather than a new "family," the Abstract Factory would require changing the interface and all concrete factories. The differences in the scope of changes for "adding products" are just that concrete and quantifiable.

## The Other Side of the Factory Pattern: Centralized Creation Tracking

So far, we have focused on "decoupling." However, the Factory Pattern has another often-overlooked benefit: **since all creation is centralized within the factory, the factory serves as a natural object audit point**. Adding logging, counting, or timing to creation requires changing only one place in the factory, rather than scattering changes across every `switch` statement:

```cpp
struct TracingBurgerFactory {
    static std::unique_ptr<Burger> create(BurgerType t) {
        auto start = std::chrono::steady_clock::now();
        auto burger = SimpleBurgerFactory::create(t);   // 委托真实工厂
        auto end   = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cerr << "[factory] created " << burger->name()
                  << " in " << us << " us\n";
        return burger;
    }
};
```

This approach of "wrapping a layer around the factory for cross-cutting concerns" is essentially applying the Decorator or Proxy pattern on top of a factory. Its prerequisite is precisely that "creation has already been centralized"—if you are still writing `switch` statements all over the call sites, there is no way to implement this unified tracking. Therefore, the Factory pattern is not just about "saving the client from writing `switch` statements"; it also provides a **chokepoint that every object must pass through at birth**. Permission checks, monitoring, caching, and object pools can all be hooked here.

## Which One to Choose: A Decision Table

We have worked our way from simple factories to functional factories. Now, let's put them side-by-side to see where each excels:

| Dimension | Simple Factory | Factory Method | Abstract Factory | Functional Factory |
|---|---|---|---|---|
| Products created | Single product | Single product | **Family of products** | Single product (by key) |
| Adding new product | Modify `switch` in factory (violates OCP) | Add a `Creator` subclass (follows OCP) | Modify interface + all concrete factories (high cost) | Add one lambda to registry |
| Adding new family | — | — | Add a concrete factory class (follows OCP) | — |
| Enforce family consistency | No | No | **Yes (Strong structural constraint)** | No |
| Type safety | Compile-time (`switch` missing case warns) | Compile-time (pure virtual forces impl) | Compile-time (pure virtual forces impl) | **Runtime** (typos in key crash at runtime) |
| Class burden | One factory class | One factory subclass per product | One factory subclass per family | Almost no new classes |

How do we choose? I have distilled the logic into a few sentences. **For the vast majority of needs to "conditionally `new` different subclasses," a Simple Factory is sufficient**—don't over-engineer. **When products will be continuously added and you don't want to modify factory source code every time, use Factory Method**, trading class quantity for OCP. **When you are creating a family of products that must share a consistent style (meal combos, cross-platform UI controls, multi-database dialects), use Abstract Factory**; its killer feature is the structural constraint of "family consistency." **When your creation logic is lightweight, you want OCP, but don't want to maintain a bunch of one-line subclasses, use Functional Factory**, reducing the factory to a `key → lambda` registry. However, you must accept its downgrade in type safety—key errors are only discovered at runtime.

::: warning Functional factories downgrade type safety
The type safety of Factory Method and Abstract Factory is compile-time: `BurgerCreator::create()` is a pure virtual function. If you forget to implement it in a concrete factory, that factory becomes abstract and cannot be instantiated, so the compiler stops you immediately. Functional factories don't work this way—its key is a string. If you typo `create("beef")` as `create("beed")`, the compiler can't catch it. It waits until runtime `find` fails and returns `nullptr`, and then crashes when you try to dereference it. Therefore, Functional Factory takes a **tangible step down** in "type safety." If you use it, the calling side must honestly handle "key might not exist" (check if the returned `unique_ptr` is empty, or throw an exception). If your key source is external input (config files, network requests), this check is mandatory.
:::

## Pitfall Warning: Specific Implementation Details

::: warning Factories must return `unique_ptr<base>`, not raw pointers or `unique_ptr<derived>`
A factory method returning `std::unique_ptr<Burger>` (base class) is deliberate. First, **don't return a raw `Burger*`**—the caller gets a raw pointer and must remember to `delete` it. Forgetting this causes a memory leak, and returning a raw pointer blurs ownership semantics (who owns this object?). `std::make_unique` + `unique_ptr<Burger>` cleanly transfers ownership to the caller, with RAII handling reclamation. Second, **`std::make_unique<CheeseBurger>()` implicitly converts to `unique_ptr<Burger>`** because `unique_ptr` has a constructor template for compatible pointer types—but the reverse (`unique_ptr<Burger>` to `unique_ptr<CheeseBurger>`) won't work, so the factory must return a base class pointer. Third, **the base class `Burger` must have a `virtual` destructor** (`virtual ~Burger() = default;`). Otherwise, `delete`ing a derived object via a base class pointer is undefined behavior—we emphasized this in the Singleton and Visitor sections, and it applies here too, because `unique_ptr<Burger>` destroys the object through the base class pointer.
:::

::: warning Abstract Factory "family consistency" is not "free product combination"
Abstract Factory bundles the creation of a family of products. The benefit is "get one concrete factory, and the whole set is guaranteed to be the same style," but the cost is **it locks down the freedom of product combinations**. Suppose you want a mix like "Classic Meal Burger + Healthy Meal Drink." The Abstract Factory structure doesn't support this—you can only pick one factory and take its whole set. If your business requires product-level free combination, Abstract Factory is not the right choice. You should revert to "one factory method per product" and let the client compose them. The cost is that you lose the structural guarantee of "family consistency" and must rely on discipline instead. Don't jump to Abstract Factory as soon as you see "family of products"—ask yourself first: do I want "consistent sets" or "free mixing"?
:::

::: tip Combining factory registries with static local variables ensures thread-safe initialization
For the Functional Factory registry, we used the `static std::unordered_map<...>& registry()` approach with a Meyer's Singleton (function-local `static` variable). This means the registry's initialization itself is thread-safe—C++11 magic statics guarantee that "if multiple threads enter this declaration for the first time simultaneously, only one performs the initialization." However, note: **thread-safe initialization of the registry does not mean thread-safe read/write of the registry's contents**. If registration happens after program startup and multiple threads concurrently call `register_creator`, you still need to lock the registry (`std::shared_mutex` fits "read-many, write-few" scenarios). Most factory registrations happen during the `main` startup phase (single-threaded), so you don't need to worry about locks; once registration is delayed to runtime, locks must be added. This point and the one about magic statics in the Singleton article are two sides of the same coin.
:::

## Factory vs. Builder: Don't Pick the Wrong One

We will also cover the Builder pattern later in this volume. Both Factory and Builder solve "object creation" problems, and beginners often choose the wrong one. Let's clarify the difference:

- **Factory** focuses on **"which one to build"**—returning a **different** concrete subclass based on conditions. The object itself is relatively simple and created in one step.
- **Builder** focuses on **"how to build it"**—spreading out the construction of a **complex** object into steps (a bunch of `set_xxx()` chained calls, ending with `build()`). The object type is fixed, but it has numerous configuration options.

A simple heuristic: if your struggle is "which subclass should I `new`?", use Factory. If your struggle is "this object has over a dozen optional parameters, how do I configure it clearly?", use Builder. They can also be combined—an Abstract Factory can return a Builder, allowing the client to decouple the specific type while configuring it step-by-step.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it wasn't enough |
|---|---|---|
| `switch new` at call site | Directly `switch` at usage to decide which to `new` | Creation coupled with usage; user knows all concrete types |
| Simple Factory | Extract `switch` to a static factory method | Adding products requires modifying factory internals (violates OCP) |
| Factory Method | Abstract factory interface + one concrete factory per product | Decouples single products well, but cannot create families |
| Abstract Factory | Bundle creation of a family of products into one interface | Adding families is easy, but adding product types requires changing the interface and all concrete factories |
| Functional Factory | `key → lambda` registry | Type safety downgraded (runtime lookup) |

Remember these key conclusions:

- The Factory pattern solves the problem of coupling between **"creating objects"** and **"using objects"**—it strips "which concrete subclass to `new`" from the user and delegates it to a dedicated factory, so the user only faces the abstract base class.
- **Simple Factory** (a `switch` inside a static method) solves the most painful coupling but violates OCP—adding products requires changing factory internals.
- **Factory Method** (abstract `Creator` + one concrete `Creator` per product) trades class quantity for OCP—adding products only adds new classes without modifying existing code.
- **Abstract Factory** bundles the creation of related products. Its killer feature is the **structural constraint of "family consistency"** (a whole set from one factory is guaranteed to be uniform); the cost is that adding product types requires changing the interface and all concrete factories.
- In modern C++, prioritize **Functional Factory**: a `key → lambda` registry. It achieves OCP with the lightest weight and almost no new classes; however, type safety is downgraded to runtime (typos in keys crash at runtime), so the calling side must handle "key does not exist."
- Don't forget the other benefit of a factory: **it is a chokepoint for the birth of all objects**. Logging, metrics, permissions, caching, and object pools—these cross-cutting concerns only need to be hooked in one place here.

::: tip Companion compilable projects
The two complete CMake projects for this section are in this repository. Clone and run `cmake` to try them: the Factory Method implementation [BurgerCreator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/BurgerCreator) (concrete factories for two chains making their brand's burgers) and the Functional Factory implementation [NotificationSystem](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/NotificationSystem) (`key → lambda` registry dispatching Email/SMS/Push).
:::

## References

- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (Since C++11, the standard vehicle for transferring ownership from factory return values)
- [cppreference: `std::function`](https://en.cppreference.com/w/cpp/utility/functional/function) (Since C++11, the value type for functional factory registries)
- [cppreference: Virtual destructors](https://en.cppreference.com/w/cpp/language/destructor#Virtual_destructor) (When a factory returns a base class pointer, the base destructor must be `virtual`)
- [refactoring.guru: Factory Method](https://refactoring.guru/design-patterns/factory-method) / [Abstract Factory](https://refactoring.guru/design-patterns/abstract-factory) (Illustrated GoF factory patterns)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — Original definitions of Factory Method and Abstract Factory
- Companion compilable project: [FactoryBaseMethod](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory)
