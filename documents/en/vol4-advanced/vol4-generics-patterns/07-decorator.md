---
title: 'Decorator Pattern: From Deep Inheritance Hell to Chained Wrappers'
description: Starting with the most intuitive approach of "writing a subclass for
  every combination," we progressively derive dynamic decorators, template mixins,
  and atomic hot-swapping, clarifying the trade-offs and applicable boundaries of
  each path.
chapter: 11
order: 7
tags:
- host
- cpp-modern
- intermediate
- 装饰器模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/07-decorator.md
  source_hash: 490058de01c00d5a9eb22d89d79ad647a648fcdb3721f4b30bc437e1716d2bad
  translated_at: '2026-06-24T00:56:21.564687+00:00'
  engine: anthropic
  token_count: 3390
---
# Decorator Pattern: Escaping the Multi-layer Inheritance Hell into Chained Wrappers

## What Problem Are We Actually Solving?

Let's skip the formal definition for a moment. Imagine you are building a text output component. The most basic requirement is to throw a string to standard output, which is trivial—just one `print` statement. But requirements quickly inflate: sometimes you need to wrap the text in quotes, other times sandwich it between asterisks, convert it to all caps, or even combine two or three of these features simultaneously.

What would you do now? The most intuitive reaction is probably to write subclasses. If `PlainTextPrinter` isn't enough, you might derive `QuotedPlainTextPrinter`, then `StarredPlainTextPrinter`, then `UpperQuotedStarredPlainTextPrinter`... Don't laugh yet; I've seen this kind of naming in real projects far too often. The problem is that "quotes," "asterisks," and "uppercase" are logically **orthogonal**—they are independent and can be freely combined. Inheritance, however, expresses a linear "is-a" relationship, which is inherently ill-suited for describing orthogonal features.

Let's do a quick calculation: with N independent features, theoretically you need `2^N` subclasses to exhaust all combinations. This is the textbook definition of **class explosion**. Moreover, every time you add a new feature, you either inherit from an existing combination class (introducing unnecessary coupling) or derive from the root again (losing existing capabilities). Either way, it's messy. Even worse, sometimes you don't want to hardcode certain combinations at compile time. For example, in a logging framework, you might want to decide at runtime, based on configuration, whether to insert a timestamp decorator or a color decorator into the output chain.

The Decorator pattern is designed to solve exactly this class of problems: **without modifying existing classes or relying on疯狂的 inheritance, it turns extra behaviors into stackable "wrapper" units that can be layered onto a base object on demand.** A decorated object still presents the original object's interface to the outside world, but every call is forwarded along the wrapper chain, giving every layer along the way a chance to insert its own logic.

Next, we will go through this step by step. We will first look at why the "write subclasses" path collapses, then force out dynamic decorators and template mixins, and finally discuss runtime hot-swapping.

## Step 1: The Most Intuitive Approach—Writing a Subclass for Every Combination (The Anti-Pattern)

When many friends face the requirement of "text needs quotes and asterisks," their subconscious reaction is often like this:

```cpp
struct PlainTextPrinter {
    void print(const std::string& text) { /* 直接输出 */ }
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

To be honest, if your requirements are limited to just these three combinations forever, writing it this way might actually work. However, things rarely end that simply. The moment product management asks for a "starred version without quotes," you have to go back to `PlainTextPrinter` and derive a new `StarredPrinter`. A few days later, when they want "uppercase with quotes but no stars," that's another new branch. **Every new requirement corresponds to a new inheritance subtree**, and these subtrees cannot share any intermediate results.

Where is the problem? The problem is that **inheritance tightly couples "capabilities" to "types."** "Quoted text" is essentially a small, standalone capability, yet you are forced to create a new type for it. Once it becomes a type, all subsequent combinations are locked into that specific lineage. It's like wanting to put a case, a screen protector, and a lanyard on a phone. Instead of buying accessories, you end up manufacturing a new product category called "Phone-with-case-and-screen-protector-and-lanyard"—as the number of accessory combinations grows, your SKUs explode.

The Decorator pattern takes the opposite approach: **capabilities should not be baked into types; instead, they should be independent units that can be stacked on and peeled off.**

## Step Two: Extract a Unified Interface — Make the "Decorator" Look Like the "Decorated Object"

To create "detachable capability units," the first prerequisite is that no matter how many layers we stack, the external code must see the exact same interface. This is why the Decorator pattern always starts with an abstract base class. Let's establish this abstraction first:

```cpp
struct AbsTextPrinter {
    virtual ~AbsTextPrinter() = default;
    virtual void simple_print(const std::string& text) = 0;
};
```

This abstraction serves two purposes. First, it defines what a "text printer" looks like—any object we want to decorate, and any decorator itself, must implement this interface. Second, and more crucially, because the decorator and the decorated object implement the **same interface**, the decorator can directly replace the decorated object. The external caller cannot tell whether they are holding the "raw object" or an object "wrapped in three layers"—this is the foundation for all subsequent composition.

The base object is simple; we just implement the interface:

```cpp
class PlainTextPrinter : public AbsTextPrinter {
public:
    void simple_print(const std::string& text) override {
        std::print("{}", text);  // 只做最基础的输出
    }
};
```

## Step 3: Create a Decorator Base Class — Solidify the "Forwarding" Logic

Now, the question arises: every concrete decorator (quotes, asterisks, uppercase) must do the exact same thing — **hold the inner decorated object and forward the work to it**. If we let each decorator implement its own "hold + forward" logic, we will end up with code duplication. Therefore, we create a decorator base class to extract this skeleton:

```cpp
class BaseDecorator : public AbsTextPrinter {
protected:
    std::shared_ptr<AbsTextPrinter> inner_;

public:
    explicit BaseDecorator(std::shared_ptr<AbsTextPrinter> ptr)
        : inner_(std::move(ptr)) {}

    // 纯虚:具体装饰器必须自己实现
    void simple_print(const std::string& text) override = 0;
};
```

There are a few design decisions here that are worth expanding upon. First, the decorator base class **itself still inherits from `AbsTextPrinter`**. This reflects the principle mentioned in the previous section: "the decorator and the decorated object share the same interface." This allows decorators to be nested indefinitely. For example, wrapping `QuoteDecorator` with `StarDecorator` still yields an object that acts as an `AbsTextPrinter` externally.

Second, the decorator holds a `std::shared_ptr<AbsTextPrinter>` pointing to the inner object it decorates. We use `shared_ptr` because the outer and inner layers share ownership of the underlying object along the decoration chain. This ensures that destroying any single decorator won't accidentally destroy the entire chain. If you are certain that shared ownership isn't needed, using `std::unique_ptr` to express exclusive ownership would be clearer—a point we will revisit later.

Third, you might notice that `simple_print` is redeclared as pure virtual in the base class. This step might seem a bit odd—shouldn't the base class provide a default forwarding implementation? **We intentionally omit it to force every concrete decorator to explicitly decide "whether to forward, when to forward, and whether to modify arguments before forwarding."** The soul of the Decorator pattern is "injecting logic before or after forwarding." This decision must not be silently erased by a default implementation; otherwise, we risk confusing behavior where "you thought you decorated, but you merely passed through."

## Step 4: Concrete Decorators—That Little Bit of Logic Before and After Forwarding

With the skeleton in place, the concrete decorators are surprisingly lightweight. They only need to do one thing: insert their own logic before (or after) forwarding the request to `inner_`:

```cpp
class QuoteDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;  // 继承构造函数,省得重写

    void simple_print(const std::string& text) override {
        inner_->simple_print("\"" + text + "\"");  // 转发前:加引号
    }
};

class StarDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;

    void simple_print(const std::string& text) override {
        inner_->simple_print("***" + text + "***");  // 转发前:加星号
    }
};

class UpperCaseDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;

    void simple_print(const std::string& text) override {
        std::string result = text;
        // 转发前:把内容改成大写,再把改造后的结果往下传
        std::transform(text.begin(), text.end(), result.begin(),
                       [](unsigned char ch) { return std::toupper(ch); });
        inner_->simple_print(result);
    }
};
```

::: warning A Common Typo Trap
When writing decorators like `UpperCaseDecorator` that follow the "transform-then-forward" pattern, there is a particularly subtle pitfall: you calculate a `result`, but due to a slip of the hand, you write `inner_->simple_print(text)` when forwarding. I actually encountered this pitfall in the initial version of the companion project—the uppercase decorator calculated `HELLO, WORLD!`, but then proceeded to pass the original lowercase `text` down, rendering the decoration useless.

This type of bug will not trigger a compiler error (the types match perfectly), and at runtime, the only symptom is "the output doesn't change," making it easy to dismiss as a "configuration error" and overlook. **When writing decorators, make sure to verify "are you forwarding the transformed parameters, or the original ones?"** This has been corrected in the companion project to forward `result`. Let's verify this first:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra fixed_upper.cpp -o fixed_upper
$ ./fixed_upper
[HELLO, WORLD!]
```

The output is correct. This is the standard idiom for "modifying arguments before forwarding."
:::

## Step 5: Chaining Decorators — Understanding the Nesting Order

Now we reach the most satisfying part of decorators: composition. What we need to do is wrap the decorators layer by layer, just like nesting dolls. The code below demonstrates how to transform a basic `PlainTextPrinter` step-by-step into a fully featured version that handles "uppercase + asterisks + quotes":

```cpp
int main() {
    std::string text = "Hello, World!";

    // 最内层:基础对象
    auto plain = std::make_shared<PlainTextPrinter>();

    // 套一层引号
    auto quoted = std::make_shared<QuoteDecorator>(plain);

    // 再套一层星号(包在 quoted 外面)
    auto starred = std::make_shared<StarDecorator>(quoted);

    // 最外层套大写
    auto full = std::make_shared<UpperCaseDecorator>(starred);

    full->simple_print(text);
}
```

Here is a point that often confuses beginners: **the outer layer acts first**. Let's look at `StarDecorator(quoted)`—`Star` is on the outside, so it receives the original `text` first, wraps it into `***text***`, and then forwards it to the inner layer `quoted`. `quoted` receives `***text***`, wraps it into `"***text***"`, and forwards it to `plain`. Finally, `plain` outputs the result. The outermost `UpperDecorator` executes first, capitalizing `text`, so the final output is `***"HELLO, WORLD!"***`.

Let's run this in the terminal ourselves to confirm the execution order of the chain:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra dynamic_chain.cpp -o dynamic_chain
$ ./dynamic_chain
["***hi***"]
```

In this example, the outer layer is `Star` (wrapped with `***...***`), the inner layer is `Quote` (wrapped with `"..."`), and the innermost layer is `Plain` (output as-is). The outer layer acts first, so `***` wraps the outside, while the quotes wrap the inside. This order aligns with your intuition of "putting on underwear first, then a coat."

::: tip Why we must use `shared_ptr`
You might ask: why does the decorator hold a `shared_ptr` instead of holding the object itself or a reference? There are three reasons. First, **polymorphism requires pointers or references**; you cannot hold an abstract base class object directly, only a pointer to it. Second, **the number of decorator layers is determined at runtime**; you cannot hardcode a concrete type at compile time, so you must use a handle capable of pointing to any concrete implementation—this is exactly what `shared_ptr<AbsTextPrinter>` does. Third, **ownership**. Multiple decorators in a chain point to the same underlying object, and no single layer should exclusively own it. The reference counting of `shared_ptr` naturally expresses this shared ownership. If you determine that the ownership of the entire chain is linear (where only the outermost layer holds ownership), switching the inner layer to `std::unique_ptr<AbsTextPrinter>` would more clearly express "exclusive ownership," at the cost of no longer allowing two decorators to share the same inner layer.
:::

## The Cost of Dynamic Composition: Don't Pretend It's Free

At this point, we have a working, dynamically composable decorator. However, I must be honest with you: this approach comes with a cost, specifically **an extra virtual function call and an extra pointer dereference for every single invocation**.

Let's write a minimal piece of code to run the `Star -> Quote -> Plain` chain described above, and then see what the compiler actually generates at the `-O2` optimization level:

```sh
$ objdump -d -C dynamic_chain | grep -E "call.*simple_print"
   169ab: call 16550 <QuoteDecorator::simple_print(...)>
   169eb: call 16ba0  <StarDecorator::simple_print(...)>
   1707e: call 16550 <QuoteDecorator::simple_print(...)>
   170c6: call 16ba0  <StarDecorator::simple_print(...)>
```

You see, even with `-O2`, every single decorated forwarding step is still a concrete `call`—the compiler cannot optimize it away here. The entire chain is assembled at runtime via `shared_ptr`, so the compiler cannot see "what exactly is the inner layer—is it a `QuoteDecorator` or something else?" and naturally dares not inline. **The longer the chain, the deeper the `call` stack**. On a hot path called tens of thousands of times per second (such as per-frame UI rendering or per-line log output), this overhead accumulates.

There are two more subtle costs. First, **object identity changes**: the outer decorator is a new object; it is not the inner object, so `&decorator != &inner`. If you rely on object addresses for equality comparisons, serialization, or cache keys, this "every layer is a new object" characteristic will bite you. Second, **state modifications must be explicitly forwarded**: `simple_print` is a read-only interface, so forwarding is clean. However, if you have a state-modifying interface (like `resize()`), every layer of the decorator must decide whether to pass this modification down to the inner layer. If it doesn't, you get inconsistencies where "the outer layer changed, but the inner layer didn't."

Therefore, dynamic decorators are best suited for scenarios where "composition is determined at runtime, call frequency is low, and the interface is primarily query/output-based." Logging frameworks, configuration-driven output pipelines, and plugin-based processing chains—these scenarios are naturally suited for dynamic decorators. But once you find yourself running a dozen-layer decorator chain every frame, it's time to consider the next path.

## Step 6: Compile-time Composition — Template Mixins to Optimize Away Overhead

When the composition relationship is fully determined at compile time and performance is critical, we have a distinctly different path: **use templates to turn decorators into type-level wrappers**. The idea is to swap "decorator holds inner object" for "decorator inherits from inner type." This flattens the entire chain into a concrete type at compile time, all forwarding becomes direct function calls, and the compiler can safely inline everything the way down.

Let's drop the abstract base class and write a simple, non-polymorphic base type:

```cpp
struct PlainRaw {
    void simple_print(const std::string& text) const {
        std::print("{}", text);
    }
};
```

Note that it has no virtual functions and does not inherit from anything—it is just a plain struct. Then, we write a template decorator that inherits from `Base` and layers its own logic on top of `Base`'s behavior:

```cpp
template <typename Base>
struct QuoteMixin : Base {
    using Base::Base;  // 继承 Base 的构造函数

    void simple_print(const std::string& text) const {
        Base::simple_print("\"" + text + "\"");  // 调用 Base 的版本
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

`QuoteMixin<Base>` inherits from `Base`, which means it **possesses all capabilities of `Base` while adding its own quotation logic**. Composition stacks them layer by layer:

```cpp
int main() {
    // 嵌套组合,得到一个具体类型
    using Decorated = StarMixin<QuoteMixin<PlainRaw>>;
    Decorated d;
    d.simple_print("hi");
}
```

The output is identical to the dynamic version: `["***hi***"]`. However, the cost is completely different. Let's look at the disassembly:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra mixin.cpp -o mixin
$ objdump -d -C mixin | grep -iE "simple_print|QuoteMixin|StarMixin"
(空)
```

There is **not a single** call to `simple_print`, `QuoteMixin`, or `StarMixin` in `main`. The entire decoration chain—asterisks, quotes, and raw output—is inlined at compile time; the act of "decoration" does not exist at runtime at all. This is the true meaning of **zero-overhead abstraction**: you write code as elegantly as if it were runtime composition, yet the compiler optimizes it into hand-written inline code.

```sh
$ ./mixin
["***hi***"]
```

To confirm that this type is indeed non-polymorphic (has no vtable), let's add another `static_assert`:

```cpp
static_assert(!std::is_polymorphic_v<StarMixin<QuoteMixin<PlainRaw>>>,
              "mixin 链不应该有虚函数");
```

This assertion holds at compile time, proving from the language level that this approach incurs no virtual function overhead.

::: warning The Cost of Mixins: Type Explosion
Static composition is not free; it simply shifts the cost from runtime to the type system. `StarMixin<QuoteMixin<PlainRaw>>` and `QuoteMixin<StarMixin<PlainRaw>>` are **two distinct types**, even if they use the exact same components:

```sh
$ ./type_explosion
T1 == T2 ? false
```

Let's run `std::is_same_v<A<C<Plain>>, C<A<Plain>>>` in the terminal. The result is `false`. This means that every combination results in a brand new, mutually incompatible type. You cannot stuff `StarMixin<Quote<Plain>>` and `Quote<StarMixin<Plain>>` into the same `std::vector<T>`, nor can you swap one chain for another at runtime. **N features theoretically yield an exponential number of types**—this is "type explosion."

In practice, the consequences of type explosion manifest in three main areas: constructor argument forwarding becomes difficult (with deeply nested mixins, arguments must be `std::forward`ed layer by layer, and messing up the order breaks everything); they cannot be placed in a uniform container (unless you wrap them in another layer of type erasure); and combinations cannot be switched at runtime. If you want the performance of static composition but also need to use these types through a unified interface externally, you can write a thin adapter for the static composition. This wraps the static type in a wrapper that implements an abstract base class—internally static and efficient, externally polymorphic. This hybrid approach of "static implementation + polymorphic shell" is extremely practical in systems that "execute inline normally but occasionally expose functionality via polymorphism to a plugin layer."
:::

## Step 7: Runtime Hot-Swapping—Atomically Replacing the Entire Chain

Finally, let's discuss an engineering scenario, and arguably the area where decorators are most useful in real-world systems: **hot-swapping**. Imagine you have written a logging framework. During runtime, you want to dynamically rebuild the entire output decoration chain based on a new configuration (e.g., `["timestamp", "colored", "file_sink"]` in a JSON file), and you must do this **without stopping service or causing lock contention**.

To achieve this, we need two capabilities. The first is to register the "construction method" of the decorator as a discoverable factory. This is essentially the Factory Pattern: each decorator corresponds to a factory function that accepts the inner layer of the current chain and returns the wrapped outer layer. By nesting layers from the tail of the configuration backwards, we can assemble any decoration chain based on runtime configuration.

The second, and more critical, capability is **atomic replacement of the entire chain**. If reader threads are accessing the chain while you are swapping it, a slight misstep leads to use-after-free. C++20 provides a tool that is clean and almost free: `std::atomic<std::shared_ptr<T>>`. It allows you to place the root reference of the entire decoration chain in an atomic `shared_ptr`. After rebuilding the new chain, you perform an atomic `store`. Subsequent accesses will see the new chain, while threads still using the old chain will safely finish their current calls using their held copy of the reference. The old chain is automatically destroyed once its reference count drops to zero.

```cpp
// 根引用:原子化的 shared_ptr(C++20)
std::atomic<std::shared_ptr<Shape>> root;

void reload_config_and_apply(const std::vector<std::string>& cfg) {
    // 用配置拼出新链(略:逐层套装饰器)
    auto base = std::make_shared<Circle>();
    auto new_root = build_from_config(base, cfg);
    root.store(new_root, std::memory_order_release);  // 原子替换
}

std::string read_current() {
    // 每次读都拿到一个 shared_ptr 副本,在副本上操作
    auto p = root.load(std::memory_order_acquire);
    return p->describe();
}
```

Let's verify this first. We will have four reader threads continuously reading, while the main thread performs hot swapping 1,000 times. The program runs without crashing, and the output is correct:

```sh
$ g++ -std=c++23 -O2 -pthread -Wall -Wextra hotswap.cpp -o hotswap
$ ./hotswap
final describe = [[Circle]]
total reads   = 3018 (no crash, no UB)
```

The reader thread ran over three thousand iterations, during which the chain was atomically swapped one thousand times. The final chain read was `[[Circle]]` (a `Circle` wrapped in two layers of `WithBorder`). Throughout the entire process, there were no crashes and no undefined behavior (UB). This is the promise of `std::atomic<std::shared_ptr>`—**hot swapping barely blocks executing threads, and the old chain is automatically reclaimed when its reference count drops to zero.**

::: warning Hidden Premise of Hot Swapping: Decorators Should Ideally Be Stateless
Atomic chain swapping solves concurrency safety for the "chain itself," but it cannot solve concurrency safety for the "decorator's internal state." If a decorator contains mutable shared state (like a counter or a cache), then even if the chain is atomically swapped, that state might still be read and written by multiple threads simultaneously, causing a data race. Therefore, in engineering practice, hot-swappable decorators **should ideally be designed to be stateless**, or their state should be made thread-safe (e.g., by wrapping it in `std::mutex` or using `std::atomic`). If you truly need to share mutable state between decorators, that state should not be hidden inside the decorator; instead, it should be extracted into an independent, thread-safe object referenced by the decorators.
:::

## Trade-offs Between the Three Approaches

At this point, we have walked through all three implementation styles of the Decorator pattern. Let's summarize their trade-offs in a table:

| Style | Composition Time | Performance | Flexibility | Primary Cost |
|---|---|---|---|---|
| Dynamic Decorator | Runtime | Per `call` (virtual call + pointer) | Arbitrary combinations, can be stored in a uniform container | Call overhead, changing object identity, state must be explicitly forwarded |
| Template Mixin | Compile-time | Zero-overhead (fully inlined) | Combinations fixed at compile-time | Type explosion, cannot be placed in uniform containers, complex constructor parameter forwarding |
| Factory + Atomic Hot Swap | Runtime (config/plugin driven) | Same as dynamic (plus atomic overhead) | Configurable at deployment time, hot-updatable | Complex implementation (registration/parsing/concurrency), decorators must be stateless |

In real-world engineering, there is rarely a "single correct answer." If you are writing a hot path for UI rendering that executes tens of thousands of times per frame, template mixins allow you to enjoy the elegance of composition while getting hand-written inline performance. If you are writing a server-side logging framework or a toolchain that needs to change behavior based on configuration or plugins, dynamic decorators with factory registration provide the necessary flexibility. A more common approach is a **hybrid**: implement high-frequency combinations with static mixins and wrap them in a polymorphic adapter, while making truly hot-swappable, on-demand features into factory-based plugins. This preserves performance while gaining runtime configurability.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why It Wasn't Enough |
|---|---|---|
| Subclass Enumeration | Derive a new class for each combination | Class explosion (2^N), features are orthogonal but inheritance is linear |
| Dynamic Decorator | Abstract base class + Decorator holds `shared_ptr<Interface>`, chain forwarding | One virtual call per layer, overhead on hot paths |
| Template Mixin | Decorator inherits Base, compile-time inlining | Type explosion, cannot be placed in uniform containers, cannot switch at runtime |
| Factory + Atomic Hot Swap | Factory registers decorators + `std::atomic<shared_ptr>` for chain swapping | Complex implementation, decorators must be stateless |

Keep these key conclusions in mind:

- **The foundation of the Decorator pattern is "the decorator and the decorated object implement the same interface,"** allowing decorators to be nested infinitely while still presenting the original interface externally.
- **Dynamic decorators (virtual functions + `shared_ptr`) are suitable for scenarios where composition is determined at runtime and calls are infrequent**; the cost is the virtual call overhead and changing object identity per invocation.
- **Template mixins are suitable for scenarios where composition is fixed at compile-time and performance is critical**; the cost lies in the type system—type explosion and the inability to store in uniform containers.
- **For runtime hot swapping, use `std::atomic<std::shared_ptr<T>>` (C++20) to atomically replace the entire chain**. This barely blocks executing threads, provided the decorators themselves are stateless.
- **When writing decorators that "modify parameters before forwarding," ensure you forward the modified parameters**—a typo (forwarding the original parameters) won't cause a compiler error, but will silently disable the decorator.

## References

- [cppreference: `std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (Shared ownership, the default choice for decorator chains)
- [cppreference: `std::atomic<std::shared_ptr<T>>`](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2) (C++20, atomic hot swapping of the entire chain)
- [cppreference: `std::is_polymorphic`](https://en.cppreference.com/w/cpp/types/is_polymorphic) (Compile-time check for virtual functions)
- *Design Patterns* (GoF): Decorator chapter (The original object-oriented description)
- Andrei Alexandrescu, *Modern C++ Design*, Chapter 4 (Policy-based design, theoretical basis for template mixins)
- Companion compilable project: [Decorator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Decorator)
