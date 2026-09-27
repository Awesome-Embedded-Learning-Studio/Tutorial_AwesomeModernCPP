---
title: 'Decorator Pattern: From Deep Inheritance Hell to Chained Wrappers'
description: 'Starting from the most intuitive "write a subclass for every combination" approach, we work our way step by step toward dynamic decorators, template mixins, and atomic hot-swapping, spelling out what each path costs and where each one applies'
chapter: 11
order: 7
tags:
  - host
  - cpp-modern
  - intermediate
  - 装饰器模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/07-decorator.md
  source_hash: 490058de01c00d5a9eb22d89d79ad647a648fcdb3721f4b30bc437e1716d2bad
  translated_at: '2026-09-26T05:15:55+00:00'
  engine: anthropic
  token_count: 4500
---

# Decorator Pattern: From Deep Inheritance Hell to Chained Wrappers

## What Problem Are We Actually Solving

Let's not start with a definition. Imagine you are building a text output component. The most basic requirement is to throw a string at standard output—nothing hard there, a single `print` and you're done. But the requirements balloon quickly: sometimes the text needs quotes around it, sometimes a cushion of asterisks fore and aft, sometimes everything uppercased, and sometimes two or even three of those capabilities at once.

So what do you do? The most intuitive reflex is inheritance. `PlainTextPrinter` isn't enough? Derive a `QuotedPlainTextPrinter`, then a `StarredPlainTextPrinter`, then an `UpperQuotedStarredPlainTextPrinter`... Hold your laughter—I have seen exactly this kind of naming far too many times in real projects. The problem is that "quotes," "stars," and "uppercase" are logically **orthogonal**—they are independent of one another and combine freely, while inheritance expresses a linear "is-a" relationship and is simply the wrong tool for describing orthogonal features.

Do the quick math: with N independent features, you theoretically need `2^N` subclasses to enumerate every combination—this is what the textbooks call **class explosion**. And every time a new feature arrives, you either inherit from one of the existing combination classes (dragging in unnecessary coupling) or re-derive from the root (losing the capabilities you already had); either way it ends ugly. Worse still, some combinations you simply do not want to nail down at compile time—in a logging framework, say, you may want to decide at runtime, based on the configuration you just read, whether to insert a timestamp decorator into the output chain, or a coloring decorator.

This is exactly the family of needs the Decorator pattern addresses: **without modifying existing classes and without resorting to runaway inheritance, package each additional behavior as a stackable "wrapper" unit and layer it onto a base object as needed**. A decorated object still presents the original object's interface to the outside world, but every call is forwarded along the wrapper chain, and every layer along the way gets a chance to inject its own logic.

We will take it step by step: first why the "just write subclasses" road caves in, then we force out dynamic decorators and template mixins, and finally we talk about runtime hot-swapping.

## Step 1: The Most Intuitive Approach—Writing a Subclass for Every Combination (the Anti-Pattern)

The first time many people face the requirement "the text needs quotes and stars," their knee-jerk reaction looks like this:

```cpp
struct PlainTextPrinter {
    void print(const std::string& text) { /* direct output */ }
};

struct QuotedPrinter : PlainTextPrinter {
    void print(const std::string& text) {
        PlainTextPrinter::print("\"" + text + "\"");
    }
};

struct StarredQuotedPrinter : QuotedPrinter {
    void print(const std::string& text) {
        QuotedPrinter::print("***" + text + "***");
    }
};

struct UpperStarredQuotedPrinter : StarredQuotedPrinter {
    // ...
};
```

Honestly, if those three combinations were the only ones you would ever need, this could even ship. But it never ends there. The day product says "I want the starred version without quotes," you go back to `PlainTextPrinter` and derive a new `StarredPrinter`; a few days later it's "uppercase with quotes but no stars," and there is yet another new chain. **Every requirement spawns a fresh inheritance subtree**, and the subtrees cannot share any intermediate results.

Where does it go wrong? In the fact that **inheritance welds "capability" to "type"**. "Being quoted" is just a small capability that could exist on its own, yet you insist on minting a new type for it—and once it is a type, every later combination has to follow that bloodline. It's as if you wanted a case for your phone, plus a screen protector, plus a lanyard, and instead of buying accessories you went off and manufactured "the phone-with-case-with-protector-with-lanyard" as a brand-new product category: once the accessory combinations multiply, your SKUs explode.

The Decorator flips this around: **capabilities should not live inside types; they should be standalone units you can slip on and take off**.

## Step 2: Extract a Unified Interface—Making the "Decorator" Look Like the "Decorated Object"

To build "detachable capability units," the first precondition is that no matter how many layers I stack on, the interface external code sees must be the same one. That is why the Decorator pattern always begins with an abstract base class. Let's stand that abstraction up first:

```cpp
struct AbsTextPrinter {
    virtual ~AbsTextPrinter() = default;
    virtual void simple_print(const std::string& text) = 0;
};
```

This abstraction does two things. First, it defines what a "text printer" looks like—any object that wants to be decorated, and every decorator itself, must implement this interface. The second matters more: because the decorator and the decorated object implement **the same interface**, a decorator can stand in for the decorated object directly, and the caller on the outside cannot tell whether it is holding the "raw object" or "the one wrapped in three layers"—this is the bedrock that makes every later composition possible.

The base object is trivial—just implement the interface:

```cpp
class PlainTextPrinter : public AbsTextPrinter {
public:
    void simple_print(const std::string& text) override {
        std::print("{}", text);  // only the most basic output
    }
};
```

## Step 3: Introduce a Decorator Base Class—Locking Down the Forwarding

Now the next question: every concrete decorator (quotes, stars, uppercase) has to do the same thing—**hold the inner decorated object and forward the work to it**. If each decorator implements its own copy of "hold + forward," the code duplicates. So we set up a decorator base class that factors out this skeleton:

```cpp
class BaseDecorator : public AbsTextPrinter {
protected:
    std::shared_ptr<AbsTextPrinter> inner_;

public:
    explicit BaseDecorator(std::shared_ptr<AbsTextPrinter> ptr)
        : inner_(std::move(ptr)) {}

    // pure virtual: concrete decorators must implement it themselves
    void simple_print(const std::string& text) override = 0;
};
```

There are several design decisions here worth unpacking. First, the decorator base class **still inherits `AbsTextPrinter` itself**—this is precisely the "decorator and decorated object share one interface" from the previous section, so decorators can nest without limit: a `StarDecorator` wrapped around a `QuoteDecorator` is still an `AbsTextPrinter` to the outside. Second, the decorator holds a `std::shared_ptr<AbsTextPrinter>` pointing at the inner object it decorates. `shared_ptr`, because on a decoration chain the outer and inner layers share ownership of the same underlying object, and destroying any one decorator must not take the whole chain down with it; if you are sure shared ownership is not needed, switching to `std::unique_ptr` to express exclusive ownership is clearer—a point we will come back to.

Third, you may have noticed that `simple_print` is declared pure virtual again in the base class. This looks a little odd—shouldn't the base class provide a default forwarding implementation? **We deliberately withhold one, to force every concrete decorator to explicitly decide "whether to forward, when to forward, and whether to modify the arguments before forwarding."** The soul of a decorator is "inject logic before or after forwarding"; that decision must not be quietly erased by a default implementation, or you get the baffling behavior of "I thought I decorated it, but it was a bare pass-through."

## Step 4: Concrete Decorators—That Little Bit of Logic Before and After Forwarding

With the skeleton in place, the concrete decorators are almost embarrassingly light. Each has exactly one job: insert its own logic before (or after) forwarding the request to `inner_`:

```cpp
class QuoteDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;  // inherit the constructors, skip rewriting them

    void simple_print(const std::string& text) override {
        inner_->simple_print("\"" + text + "\"");  // before forwarding: add quotes
    }
};

class StarDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;

    void simple_print(const std::string& text) override {
        inner_->simple_print("***" + text + "***");  // before forwarding: add stars
    }
};

class UpperCaseDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;

    void simple_print(const std::string& text) override {
        std::string result = text;
        // before forwarding: uppercase the content, then pass the transformed result down
        std::transform(text.begin(), text.end(), result.begin(),
                       [](unsigned char ch) { return std::toupper(ch); });
        inner_->simple_print(result);
    }
};
```

::: warning A typo that is far too easy to make
When you write a "transform first, forward second" decorator like `UpperCaseDecorator`, there is one especially sneaky pitfall: you compute a `result`, then your hand slips at the forwarding line and you write `inner_->simple_print(text)`. I stepped on exactly this in the first version of the companion project—the uppercase decorator computed `HELLO, WORLD!`, then turned around and passed the original lowercase `text` down the chain, decorating precisely nothing.

The compiler will not flag this bug (the types match perfectly), and at runtime the only symptom is "the output didn't change," so it is easy to write off as "something is misconfigured" and move on. **When writing a decorator, always confirm which one you are forwarding down: the transformed argument, or the original.** The companion project has since been fixed to forward `result`; let's verify:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra fixed_upper.cpp -o fixed_upper
$ ./fixed_upper
[HELLO, WORLD!]
```

Correct output. That is the standard shape of "transform the argument before forwarding."
:::

## Step 5: Chaining Decorators—Seeing Exactly Who Wraps Whom

Now comes the most satisfying moment of the Decorator pattern: composition. All we do is wrap decorators on layer by layer, like nesting dolls. The code below dresses a plain `PlainTextPrinter` up, step by step, into the full-featured "uppercase + stars + quotes" edition:

```cpp
int main() {
    std::string text = "Hello, World!";

    // innermost layer: the base object
    auto plain = std::make_shared<PlainTextPrinter>();

    // wrap on a quote layer
    auto quoted = std::make_shared<QuoteDecorator>(plain);

    // then a star layer (outside quoted)
    auto starred = std::make_shared<StarDecorator>(quoted);

    // outermost layer: uppercase
    auto full = std::make_shared<UpperCaseDecorator>(starred);

    full->simple_print(text);
}
```

Here is the point beginners mix up most easily: **whoever is outermost moves first**. Look at `StarDecorator(quoted)`—`Star` is the outer layer; it receives the raw `text` first, wraps it into `***text***`, and forwards it to the inner `quoted`; `quoted` receives `***text***` already, wraps it into `"***text***"`, and forwards it to `plain`; `plain` finally outputs. The outermost uppercase decorator runs first of all and uppercases `text`, so the final output is `***"HELLO, WORLD!"***`.

Let's run it ourselves in the terminal and confirm the chain's execution order:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra dynamic_chain.cpp -o dynamic_chain
$ ./dynamic_chain
["***hi***"]
```

In this example the outer layer is `Star` (adding `***...***`), the inner layer is `Quote` (adding `"..."`), and the innermost is `Plain` (output as-is). The outer layer moves first, so `***` ends up outermost and the quotes sit inside—an order that matches your intuition of "underwear first, then the coat."

::: tip Why it has to be shared_ptr
You might ask: why does the decorator hold a `shared_ptr` layer instead of the object itself, or a reference? Three reasons. First, **polymorphism needs a pointer or a reference**—you cannot hold an abstract base class object directly, only a pointer to it. Second, **the number of layers in a decoration chain is settled only at runtime**—you cannot hard-code one concrete type at compile time, so you need a handle that can point at any concrete implementation, which is exactly what `shared_ptr<AbsTextPrinter>` does. Third, **ownership**. On one chain, several decorators point at the same underlying object, and no single layer should own it exclusively; `shared_ptr`'s reference counting is a natural fit for this shared ownership. If you know the chain's ownership is linear (only the outermost layer owns), swapping the inner layer to `std::unique_ptr<AbsTextPrinter>` states "exclusive" more clearly, at the price of no longer letting two decorators share the same inner layer.
:::

## The Cost of Dynamic Composition: Don't Pretend It's Free

At this point we have a dynamic decorator that runs and composes freely. But I owe you an honest accounting: this road has a cost, and the cost sits exactly here—**every single call adds one virtual function call plus one pointer dereference**.

Let's write a minimal program that runs the `Star -> Quote -> Plain` chain from above, then look at what the compiler actually generated under `-O2`:

```sh
$ objdump -d -C dynamic_chain | grep -E "call.*simple_print"
   169ab: call 16550 <QuoteDecorator::simple_print(...)>
   169eb: call 16ba0  <StarDecorator::simple_print(...)>
   1707e: call 16550 <QuoteDecorator::simple_print(...)>
   170c6: call 16ba0  <StarDecorator::simple_print(...)>
```

There it is: even at `-O2`, every decorated forwarding step is still a real, solid `call`—the compiler cannot optimize it away here, because the whole chain is assembled at runtime through `shared_ptr`; the compiler cannot see "is the inner layer a `QuoteDecorator` or something else," so it dares not inline. **However long the chain, that is how deep the `call`s go.** On a hot path invoked tens of thousands of times per second (per-frame UI drawing, per-entry log output), this overhead accumulates.

Two more hidden costs. First, **object identity changes**: the outer decorator is a new object, not the inner one—`&decorator != &inner`. If you rely on object addresses for equality comparisons, serialization, or cache keys, this "every layer is a new object" property will quietly bite you. Second, **state modification must be forwarded explicitly**: `simple_print` is a read-only interface, so forwarding stays clean; but with a state-mutating interface (say `resize()`), every decorator layer has to decide for itself whether to pass the modification down—skip it, and you get the inconsistency of "the outer layer changed, the inner one didn't."

So the natural home of the dynamic decorator is the scenario where "composition is settled only at runtime, calls are infrequent, and the interface is mostly queries and output." Logging frameworks, configuration-driven output pipelines, pluggable processing chains—these fit the dynamic decorator by nature. The moment you catch yourself running a dozen-plus-layer decorator chain every frame, it is time for the next road.

## Step 6: Compile-Time Composition—Template Mixins, Optimizing the Overhead Away

When the composition is fully determined at compile time and performance is critical, a radically different road opens up: **use templates to turn decorators into type-level wrappers**. The idea is to swap "the decorator holds the inner object" for "the decorator inherits the inner type." The whole chain is then flattened into one concrete type at compile time, every forwarding becomes a direct function call, and the compiler can inline all the way down with confidence.

First we throw away the abstract base class and write a bare-bones non-polymorphic base type:

```cpp
struct PlainRaw {
    void simple_print(const std::string& text) const {
        std::print("{}", text);
    }
};
```

Note it has no virtual functions and inherits nothing—it is just an ordinary struct. Then we write template decorators that inherit `Base` and layer their own logic on top of `Base`'s behavior:

```cpp
template <typename Base>
struct QuoteMixin : Base {
    using Base::Base;  // inherit Base's constructors

    void simple_print(const std::string& text) const {
        Base::simple_print("\"" + text + "\"");  // call Base's version
    }
};

template <typename Base>
struct StarMixin : Base {
    using Base::Base;

    void simple_print(const std::string& text) const {
        Base::simple_print("***" + text + "***");
    }
};
```

`QuoteMixin<Base>` inherits `Base`, which means it **has every capability of `Base` while adding its own quoting logic**. Composition is just stacking them layer by layer:

```cpp
int main() {
    // nested composition yields one concrete type
    using Decorated = StarMixin<QuoteMixin<PlainRaw>>;
    Decorated d;
    d.simple_print("hi");
}
```

The output is identical to the dynamic version: `["***hi***"]`. But the cost is a different story. Look at the disassembly:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra mixin.cpp -o mixin
$ objdump -d -C mixin | grep -iE "simple_print|QuoteMixin|StarMixin"
(empty)
```

Not **one single** `call` to `simple_print`, `QuoteMixin`, or `StarMixin` anywhere in `main`. The entire decoration chain—stars, quotes, raw output—was inlined away at compile time; at runtime the act of "decorating" simply does not exist. This is what **zero-overhead abstraction** truly means: you write it as elegantly as runtime composition, and the compiler turns it into a piece of hand-written inline code.

```sh
$ ./mixin
["***hi***"]
```

To confirm that this type really is non-polymorphic (no vtable), let's add one more `static_assert`:

```cpp
static_assert(!std::is_polymorphic_v<StarMixin<QuoteMixin<PlainRaw>>>,
              "mixin 链不应该有虚函数");
```

This assertion holds at compile time, proving at the language level that this road carries no virtual-function overhead.

::: warning The cost of mixins: type explosion
Static composition is not cost-free; the cost just moved—it **no longer lives at runtime but in the type system**. `StarMixin<QuoteMixin<PlainRaw>>` and `QuoteMixin<StarMixin<PlainRaw>>` are **two different types**, even though they are built from exactly the same components:

```sh
$ ./type_explosion
T1 == T2 ? false
```

Run `std::is_same_v<A<C<Plain>>, C<A<Plain>>>` in the terminal, and the answer is `false`. That means every combination is a brand-new, mutually incompatible type. You cannot stuff `StarMixin<Quote<Plain>>` and `Quote<StarMixin<Plain>>` into the same `std::vector<T>`, and you cannot swap one chain for another at runtime. **N features can theoretically combine into exponentially many types**—this is "type explosion."

In practice, the fallout of type explosion shows up in three places: constructor-argument forwarding gets harder (with deeply nested mixins, arguments must be `std::forward`ed down layer by layer, and one wrong order and things go wrong); the types cannot go into a uniform container (unless you add another layer of type erasure); and combinations cannot be switched at runtime. If you want the performance of static composition but still need the outside world to use these types through one interface, you can write a thin adapter around the static composition that wraps the static type in a wrapper implementing the abstract base class—static and efficient inside, still polymorphic outside. This "static implementation + polymorphic shell" hybrid is extremely practical in systems that "run inline most of the time, and only occasionally expose themselves polymorphically to a plugin layer."
:::

## Step 7: Runtime Hot-Swapping—Replacing the Whole Chain Atomically

Finally, an engineering scenario—and the place where decorators are most useful in real systems: **hot-swapping**. Imagine you have written a logging framework, and while it runs you want to rebuild the entire output decoration chain from a new configuration (say `["timestamp", "colored", "file_sink"]` in JSON), **without stopping the service and without lock contention**.

Pulling this off takes two capabilities. The first is registering each decorator's "construction recipe" as a lookup-able factory—same story as the Factory pattern: each decorator corresponds to a factory function that receives the current chain's inner layer and returns the wrapped outer layer. Wrapping layer by layer from the tail of the configuration backward assembles an arbitrary decoration chain from a runtime config.

The second, and the more critical one, is **atomic replacement of the entire chain**. If a reader thread is traversing the chain while you swap it, one careless step is a use-after-free. C++20 hands us a tool that is clean to the point of being nearly free: `std::atomic<std::shared_ptr<T>>`. Put the root reference of the whole decoration chain into an atomic `shared_ptr`; once the new chain is rebuilt, do a single atomic `store`, and every subsequent access sees the new chain, while threads still using the old chain safely finish their current call on their own held copy of the reference. The old chain destroys itself automatically once its reference count drops to zero.

```cpp
// the root reference: an atomic shared_ptr (C++20)
std::atomic<std::shared_ptr<Shape>> root;

void reload_config_and_apply(const std::vector<std::string>& cfg) {
    // assemble the new chain from the config (elided: wrap decorators layer by layer)
    auto base = std::make_shared<Circle>();
    auto new_root = build_from_config(base, cfg);
    root.store(new_root, std::memory_order_release);  // atomic replacement
}

std::string read_current() {
    // every read gets its own shared_ptr copy and works on that
    auto p = root.load(std::memory_order_acquire);
    return p->describe();
}
```

Let's verify first: 4 reader threads reading continuously while the main thread hot-swaps 1,000 times—no crash, correct output:

```sh
$ g++ -std=c++23 -O2 -pthread -Wall -Wextra hotswap.cpp -o hotswap
$ ./hotswap
final describe = [[Circle]]
total reads   = 3018 (no crash, no UB)
```

The reader threads ran over three thousand reads while the chain was atomically swapped one thousand times; the chain they finally read was `[[Circle]]` (two `WithBorder` layers around a `Circle`), with no crash and no UB anywhere along the way. That is the promise of `std::atomic<std::shared_ptr>`—**hot-swapping barely blocks running threads, and the old chain is reclaimed automatically as its reference count drops to zero**.

::: warning The hidden premise of hot-swapping: decorators should ideally be stateless
Atomic chain swapping makes "the chain itself" concurrency-safe, but it cannot make "the decorator's internal state" concurrency-safe. If one of your decorators carries mutable shared state inside (a counter, a cache), then even after the chain is atomically swapped, that state can still be read and written by several threads at once, triggering a data race. So in engineering practice, hot-swappable decorators **are best designed stateless**, or with their state made into a thread-safe structure of its own (wrapped in a `std::mutex`, or held via `std::atomic`). If you truly need mutable state shared between decorators, that state should not hide inside the decorators; it should stand alone as a concurrency-safe object that the decorators reference.
:::

## The Trade-Offs Among the Three Paths

We have now walked all three implementation styles of the decorator. Let's nail their trade-offs down in one table:

| Style | Composition time | Performance | Flexibility | Main cost |
|---|---|---|---|---|
| Dynamic decorator | Runtime | One `call` per layer (virtual call + pointer) | Any composition, fits in a uniform container | Call overhead, changed object identity, state must be forwarded explicitly |
| Template mixin | Compile time | Zero overhead (fully inlined) | Composition fixed at compile time | Type explosion, no uniform container, complex constructor-argument forwarding |
| Factory + atomic hot-swap | Runtime (config/plugin driven) | Same as dynamic (plus atomic overhead) | Configurable at deploy time, hot-updatable | Complex implementation (registration/parsing/concurrency), decorators must be stateless |

Real engineering rarely has a "single right answer." If you are writing a UI drawing hot path invoked tens of thousands of times per frame, template mixins give you the elegance of composition and hand-written inline performance at once; if you are writing a server-side logging framework or a toolchain whose behavior must change by configuration or plugin, dynamic decorators with factory registration give you the necessary flexibility; and the more common approach is the **hybrid**: implement the high-frequency compositions as static mixins wrapped in a polymorphic adapter, and turn the features that genuinely need hot-swapping and on-demand loading into factory-made plugins—that keeps the performance while winning runtime configurability.

## Summary

Let's trace the whole evolutionary path once more:

| Stage | Approach | Why it still wasn't enough |
|---|---|---|
| Subclass enumeration | Derive a new class per combination | Class explosion (2^N): features are orthogonal but inheritance is linear |
| Dynamic decorator | Abstract base class + decorator holds a `shared_ptr<Interface>`, chained forwarding | One virtual call per layer, overhead on hot paths |
| Template mixin | Decorator inherits Base, inlined at compile time | Type explosion, no uniform container, no runtime switching |
| Factory + atomic hot-swap | Factory-registered decorators + `std::atomic<shared_ptr>` chain swap | Complex implementation, decorators must be stateless |

File away these key conclusions:

- **The foundation of the Decorator pattern is "the decorator and the decorated object implement the same interface,"** which is why decorators can nest without limit while still presenting the original interface to the outside.
- **The dynamic decorator (virtual functions + `shared_ptr`) suits scenarios where composition is settled only at runtime and calls are infrequent**; its costs are the per-call virtual-call overhead and the changed object identity.
- **The template mixin suits scenarios where composition is fixed at compile time and performance is critical**; its costs live in the type system—type explosion, and no uniform container.
- **Runtime hot-swapping uses `std::atomic<std::shared_ptr<T>>` (C++20) to replace the whole chain atomically**, barely blocking running threads—on the premise that the decorators themselves are best stateless.
- **When writing a decorator that "transforms the argument before forwarding," always confirm that what you forward is the transformed argument**—the typo (forwarding the original argument) will not trip the compiler; it will only silently disable the decorator.

## References

- [cppreference: `std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (shared ownership, the default choice for decoration chains)
- [cppreference: `std::atomic<std::shared_ptr<T>>`](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2) (C++20, atomic hot-swapping of the whole chain)
- [cppreference: `std::is_polymorphic`](https://en.cppreference.com/w/cpp/types/is_polymorphic) (compile-time check for whether a type has virtual functions)
- *Design Patterns* (GoF), the Decorator chapter (the original object-oriented description)
- Andrei Alexandrescu, *Modern C++ Design*, Chapter 4 (policy-based design, the theoretical basis for template mixins)
- Companion compilable project: [Decorator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Decorator)
