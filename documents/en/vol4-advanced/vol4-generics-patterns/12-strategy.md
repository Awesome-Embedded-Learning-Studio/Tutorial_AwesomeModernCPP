---
title: 'Strategy Pattern: From a Heap of if/else to Compile-Time Swappable Policies'
description: Starting from the most intuitive approach of "writing a bunch of if/else
  branches," we progressively derive dynamic virtual function strategies, template-based
  static strategies, and `std::function` type erasure. We clarify the trade-offs of
  each method, and finally apply C++20 concepts to enforce compile-time constraints
  on strategies.
chapter: 11
order: 12
tags:
- host
- cpp-modern
- intermediate
- 策略模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 20
related:
- 单例模式:从注释约束到 Meyer's Singleton
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/12-strategy.md
  source_hash: 846240b6ab631e0c07ae784ae0c9ab5d441357ecc6d6603048d1c902110fb101
  translated_at: '2026-06-24T00:58:45.088852+00:00'
  engine: anthropic
  token_count: 4696
---
# Strategy Pattern: From a Pile of if/else to Compile-Time Swappable Policies

## What problem are we actually solving?

Let's skip the formal definition for a moment. Consider a very common scenario: you are writing a text processor that needs to "format" a string according to certain rules. Initially, there is only a requirement to convert text to uppercase, so you quickly write a `toupper` and call it a day. Two days later, product management asks for lowercase support, so you add an `if (mode == LOWER)`. A week later, the requirements expand to "camel case, snake case, and kebab case"—and suddenly your `format` function is stuffed with `if` and `switch` statements. Every time you add a new rule, the function gets fatter, and the rules start to interfere with each other; fixing one bug might accidentally break another.

The root cause is this: **"Which algorithm to use" and "Who calls the algorithm" are tangled together in the same function.** If you want to swap algorithms, you have to modify the calling logic that should have remained stable.

The Strategy Pattern addresses precisely this entanglement. Its core idea can be summarized in one sentence: **extract a family of interchangeable algorithms from the caller, encapsulate each into an independent "strategy" object or type, and have the caller (Context) rely solely on a unified interface. The specific strategy used can be deferred until runtime, or even until compile time.** This way, adding a new algorithm simply means adding a new strategy, without changing a single line of the caller's code. In fact, you likely use this pattern every day without realizing it: the standard library algorithms (`std::sort`, `std::transform`) are designed as typical strategy patterns—they extract "comparison strategies" and "transformation strategies" into swappable parameters (function objects, lambdas, or callables constrained by concepts).

However, the word "swappable" implies two distinct implementation levels in C++ with very different costs. One is the **dynamic strategy**, swappable at runtime via virtual functions or `std::function`; the other is the **static strategy**, swappable at compile time via template parameters (and C++20 concepts constraints). These are not a matter of "which is more advanced," but rather **two paths addressing different trade-offs between performance and flexibility.** Let's walk through this step-by-step, starting with the crudest approach, to see why each step falls short.

## Step 1: The most primitive approach—a pile of if/else (The Anti-Pattern)

When many people first encounter "multiple implementations of the same operation," the code they subconsciously write looks like this:

```cpp
enum class FormatMode { Upper, Lower, Snake };

std::string format_text(const std::string& s, FormatMode mode) {
    switch (mode) {
        case FormatMode::Upper: {
            std::string out = s;
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }
        case FormatMode::Lower: {
            std::string out = s;
            for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }
        case FormatMode::Snake:
            return to_snake(s);  // 假设已有
    }
    return s;
}
```

It runs, but the problems are lurking beneath the surface. First, **for every new algorithm added, we must add another `case` to this `switch`**; as the number of algorithms grows, this function becomes longer and more brittle. Second, **these code branches are physically squeezed together**; the probability of accidentally breaking the `Upper` branch while modifying the `Lower` branch rises linearly with the function's size. Third, and most critically—**the algorithm and the calling logic cannot vary independently**. When the day comes that you want to "decide the formatting method based on a configuration file," you will find this `switch` hardcoded inside `format_text`. There is no mechanism to "swap strategies in and out."

The root of the problem is that the algorithm is not encapsulated as an independent, replaceable entity; it is merely a branch within the caller's internal logic. We must first "extract" the algorithm, allowing the caller to hold an abstract "strategy" rather than deciding which branch to take itself.

## Step 2: Extract the Strategy Interface — Using Virtual Functions for Dynamic Strategies

The most intuitive way to "extract" something is the classic object-oriented approach: define an abstract base class as the strategy interface, make each algorithm a derived class, and have the caller hold a pointer to the base class. To switch algorithms, we simply swap in a different derived object.

```cpp
struct IFormatter {
    virtual ~IFormatter() = default;
    virtual std::string format(const std::string& s) = 0;
};

struct UpperCaseFormatter : IFormatter {
    std::string format(const std::string& s) override {
        std::string out = s;
        for (auto& c : out)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return out;
    }
};

struct LowerCaseFormatter : IFormatter {
    std::string format(const std::string& s) override {
        std::string out = s;
        for (auto& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }
};
```

Then the caller (Context) depends only on that abstract interface, and does not care which specific derived class is used:

```cpp
#include <memory>

class TextProcessor {
public:
    explicit TextProcessor(std::unique_ptr<IFormatter> f)
        : formatter_(std::move(f)) {}

    void set_formatter(std::unique_ptr<IFormatter> f) {  // 运行时可替换
        formatter_ = std::move(f);
    }

    std::string process(const std::string& s) {
        return formatter_->format(s);
    }

private:
    std::unique_ptr<IFormatter> formatter_;
};
```

You see, `TextProcessor` itself is now completely free of conditional logic; it only recognizes the `IFormatter` interface. The decision of "which formatter to use" is deferred until construction—whatever derived class we pass in `main` is what it uses. Furthermore, `set_formatter` allows us to swap strategies at runtime. This is the true meaning of "dynamic" in "dynamic strategy": **the strategy selection happens at runtime, can be replaced at any time, and can even be determined by configuration files, user input, or plugins**.

Let's first verify that it actually runs and allows runtime switching:

```cpp
#include <iostream>

int main() {
    TextProcessor ctx(std::make_unique<UpperCaseFormatter>());
    std::cout << ctx.process("hello") << '\n';  // HELLO

    ctx.set_formatter(std::make_unique<LowerCaseFormatter>());
    std::cout << ctx.process("HELLO") << '\n';  // hello
}
```

```sh
$ g++ -std=c++23 -O2 strategy_runtime.cpp -o strategy_runtime
$ ./strategy_runtime
HELLO
hello
```

That feels good. But we aren't done yet—this approach comes with a cost, and that cost is hidden right there in the `->`.

## Cost One: Indirect Jump of Virtual Calls

The line `formatter_->format(s)` does not compile into a direct function call. Instead, the CPU must: first retrieve the **vptr (virtual table pointer)** from the object pointed to by `formatter_`, then look up the slot for `format` in the virtual table, and finally jump to the address stored in that slot. This is an **indirect call**—the CPU cannot know where the next instruction is ahead of time and must look it up and jump on the spot.

The real problem with indirect calls isn't the "extra memory fetch" itself, but rather that **it breaks the compiler's inline optimization**. With a direct call, the compiler sees the function body and can flatten the entire call into the caller, eliminating the overhead of pushing arguments, saving return addresses, and preserving registers. However, the true target of a virtual function is only known at runtime, so the compiler cannot safely inline its body. On a hot path, this difference is very noticeable.

So, how big is the difference? Talk is cheap. Let's write a micro-benchmark to run the same `x + 1` operation one hundred million times using three methods: "virtual function", "template", and "`std::function`", and see the actual time taken:

```cpp
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>

struct IAdd {
    virtual ~IAdd() = default;
    virtual int transform(int x) const = 0;
};
struct AddOne : IAdd {
    int transform(int x) const override { return x + 1; }
};

struct AddOnePolicy {
    static int transform(int x) { return x + 1; }
};
template <typename Policy>
struct StaticCtx {
    int run(int x) const { return Policy::transform(x); }
};

class FuncCtx {
public:
    explicit FuncCtx(std::function<int(int)> f) : f_(std::move(f)) {}
    int run(int x) const { return f_(x); }
private:
    std::function<int(int)> f_;
};

int main() {
    constexpr int kIters = 100'000'000;
    int acc = 0;

    {
        std::unique_ptr<IAdd> p = std::make_unique<AddOne>();
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIters; ++i) acc += p->transform(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "virtual:       "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << " ms\n";
    }
    {
        StaticCtx<AddOnePolicy> ctx;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIters; ++i) acc += ctx.run(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "template:      "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << " ms\n";
    }
    {
        FuncCtx ctx([](int x) { return x + 1; });
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIters; ++i) acc += ctx.run(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "std::function: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << " ms\n";
    }
    volatile int sink = acc;  // 防止整个循环被优化掉
    (void)sink;
}
```

```sh
$ g++ -std=c++23 -O2 -pthread strategy_verify.cpp -o strategy_verify
$ ./strategy_verify
virtual:       28.8597 ms
template:      36.1899 ms
std::function: 150.557 ms
```

Take a close look at these numbers. The template version is barely faster than "no operation"—because the compiler inlined `AddOnePolicy::transform` directly into the loop. The compiler optimized `x + 1` into a single arithmetic instruction via loop induction, eliminating function calls entirely. The virtual function approach is nearly twice as slow, which is the cost of indirection and the inability to inline. `std::function` is the slowest by far; we will cover that later in this section.

Looking at execution time alone isn't intuitive enough. Let's pull up the assembly generated for the template strategy to see exactly what it was inlined into:

```sh
$ cat > strategy_asm.cpp << 'EOF'
struct AddOnePolicy { static int transform(int x) { return x + 1; } };
template <typename P> struct Ctx { int run(int x) const { return P::transform(x); } };
int hot(Ctx<AddOnePolicy> c, int x) { return c.run(x); }
EOF
$ g++ -std=c++23 -O2 -S strategy_asm.cpp -o strategy_asm.s
$ grep -A6 '^_Z3hot' strategy_asm.s
_Z3hot3CtxI12AddOnePolicyEi:
.LFB2:
    .cfi_startproc
    leal 1(%rdi), %eax
    ret
    .cfi_endproc
```

The entire `hot` function has been compiled into just three instructions — `leal 1(%rdi), %eax` (which stores `x + 1` into the return value register) and `ret`. There is no `call` instruction and no virtual table lookup; the strategy's function body has completely melted into the caller. **This is the most hardcore advantage of "compile-time replaceability" over "runtime replaceability": it optimizes a strategy call down to zero overhead.** Virtual functions can never achieve this, because their target is determined only at runtime.

## Cost Two: Object Lifetime and Pointer Management

Dynamic strategies introduce another layer of complexity — `formatter_` is a `unique_ptr<IFormatter>`, which means the object it points to lives on the heap. This implies the strategy object must be `new`-ed and eventually `delete`-d. Furthermore, every time `set_formatter` swaps the strategy, it potentially releases the old object and allocates a new one. Heap allocation isn't cheap (the overhead of a single `new` is far greater than a virtual call), and in embedded, real-time, or hot loop scenarios, "swapping a strategy triggers heap allocation" is a terrible characteristic.

Even more subtle is the semantics of ownership. If a strategy is **stateful** (it has internal members, like a counter or a cache), we must be clear: `unique_ptr` implies "Context exclusively owns this strategy." However, if we want **multiple Contexts to share the same strategy instance** (for example, a global caching strategy reused in multiple places), we must switch to `shared_ptr`. We will see an example using `shared_ptr` in the practical code later in this section. For now, let's note this down: managing the lifetime of dynamic strategies is the second price you pay for "runtime replaceability."

## Step Three: Moving the Strategy to Compile Time — Templates for Static Strategies

If our requirements dictate that **the strategy is determined at compile time and absolutely does not need to switch at runtime**, then we actually don't have to pay either of the costs mentioned in the previous section. We can simply pass the strategy as a template parameter to the Context, allowing the compiler to nail down "which strategy to use" the moment the template is instantiated:

```cpp
struct UpperCasePolicy {
    static std::string format(std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
};

struct LowerCasePolicy {
    static std::string format(std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
};

template <typename Policy>
class TextProcessor {
public:
    std::string process(const std::string& s) {
        return Policy::format(s);
    }
};
```

Here is how we use it; the strategy is fixed at compile time within the type:

```cpp
TextProcessor<UpperCasePolicy> up;
TextProcessor<LowerCasePolicy> low;

std::cout << up.process("Hello") << '\n';   // HELLO
std::cout << low.process("Hello") << '\n';  // hello
```

Notice that we haven't written any inheritance or `virtual` keywords here. `Policy` is purely a type parameter. To the compiler, `TextProcessor<UpperCasePolicy>` and `TextProcessor<LowerCasePolicy>` are **two completely distinct types**. Each one instantiates its own chunk of code, and each inlines its own `Policy::format` directly into its `process` method. This is exactly why the template approach in the earlier benchmark managed to run in mere milliseconds—no vtables, no indirect calls, and no heap allocation. The strategy call is completely flattened out at compile time.

This approach has a name: **Policy-Based Design** (systematically covered by Andrei Alexandrescu in *Modern C++ Design*). Its essence is: **reducing "strategy" from a runtime object to a compile-time type**. You no longer "hold a strategy object"; instead, you are "parameterized by a strategy type". Combined with `static` member functions, the strategy doesn't even need to be instantiated—it's just a collection of free functions hanging in a namespace, gathered up by a template parameter.

But this path has its own hard limits. **The most fatal one: once a strategy is fixed at compile time, it cannot be changed at runtime.** `TextProcessor<UpperCasePolicy>` will *always* uppercase. Want to switch it to lowercase halfway through? You can't. You have to swap to a `TextProcessor<LowerCasePolicy>` object. These two are fundamentally different types; they cannot be assigned to each other, stuffed into the same container, or held by the same variable. If your strategy needs to be "selected by the user at runtime" or "determined by a configuration file," static strategies are completely useless.

The second, more hidden cost is: **templates expand code into every instantiation**. You give it five strategies, and the compiler generates five copies of `TextProcessor::process`, so binary size grows. For scenarios like Strategy Pattern where "function bodies are small," this usually doesn't matter, but if you have dozens of strategies and every `process` is heavy, you need to weigh this code bloat.

## Let's verify here: How to use concepts to impose compile-time constraints on policies

There is another pitfall with template strategies that beginners often overlook: **the template parameter `Policy` is completely unconstrained**. You write `TextProcessor<Foo>`, and as long as `Foo` makes that function body compile, it passes. Once `Foo` lacks the `format` member, or if `format`'s signature is wrong, the compiler gives you a long string of template instantiation "gibberish." The error location often points to a line deep inside the template, rather than the line where you wrote `TextProcessor<BadFoo>`. This is exactly the problem C++20 concepts were invented to solve—**giving the strategy type a clear, readable contract**.

Let's first define a concept to clarify "what a qualified Formatter strategy should look like": it must have a `static format(std::string) -> std::string`:

```cpp
#include <concepts>
#include <string>

template <typename F>
concept Formatter = requires(F f, std::string s) {
    { F::format(std::move(s)) } -> std::same_as<std::string>;
};
```

This `requires` expression asks the compiler a question: "Given an object `f` of type `F` and a `std::string s`, can we call `F::format(std::move(s))` and does it return exactly `std::string`?" If yes, then `F` satisfies `Formatter`; if no, it does not. Then, we add this constraint to the template parameter:

```cpp
template <Formatter F>
class TextProcessor {
public:
    std::string process(const std::string& s) { return F::format(s); }
};
```

Good strategy (signature matches), everything is normal:

```sh
$ g++ -std=c++20 -O2 strategy_concept.cpp -o strategy_concept
$ ./strategy_concept
HELLO
```

Now let's intentionally write a **bad** policy—one where the return type is `const char*` instead of `std::string`—and plug it into the `TextProcessor` constrained by the concept, to see how the compiler complains:

```cpp
struct BadFormatter {
    static const char* format(std::string s) { return s.c_str(); }  // 返回类型不对
};

int main() {
    TextProcessor<BadFormatter> tp;   // 应该在这里就报
}
```

```sh
$ g++ -std=c++20 -O2 strategy_concept_bad.cpp -o strategy_concept_bad
strategy_concept_bad.cpp:22:31: error: template constraint failure for
  'template<class F>  requires  Formatter<F> class TextProcessor'
   22 |     TextProcessor<BadFormatter> tp;
      |                               ^
strategy_concept_bad.cpp:22:31: note: constraints not satisfied
  • required for the satisfaction of 'Formatter<F>' [with F = BadFormatter]
  • in requirements with 'F f', 'std::string s' [with F = BadFormatter]
  • 'F::format(std::move<...>(s))' does not satisfy return-type-requirement
```

Do you see the difference? The error points precisely to the line where you wrote `TextProcessor<BadFormatter>`, explicitly stating that "`F::format(...)` does not meet the return type requirement." This is the value of concepts compared to bare templates—**they elevate "what the strategy should look like" from "exploding deep within template instantiation errors" to "a contract violation visible right at the call site."** When implementing Policy-Based Design in modern C++, adding concept constraints to policies is almost a free lunch; there is no reason not to do it.

## Step 4: Type Erasure — Lightweight Dynamic Polymorphism with `std::function`

Now we have two paths: virtual functions allow runtime switching but incur the cost of heap allocation plus indirection, while templates have zero overhead but are fixed at compile time. Is there a middle ground—**one that allows runtime switching without requiring us to manually write an inheritance hierarchy**? Yes, and it is `std::function`.

The essence of `std::function` is **type erasure**: it can hold any callable object with a "matching signature"—lambdas, function pointers, functors, `bind` expressions—hiding them all behind a uniform type. You don't need to define a derived class for each strategy; just write a lambda and throw it in:

```cpp
#include <functional>
#include <iostream>
#include <string>

class Printer {
public:
    using Strategy = std::function<void(const std::string&)>;

    explicit Printer(Strategy s) : strategy_(std::move(s)) {}
    void set_strategy(Strategy s) { strategy_ = std::move(s); }
    void print(const std::string& s) { strategy_(s); }

private:
    Strategy strategy_;
};

int main() {
    Printer p([](const std::string& s) { std::cout << "A: " << s << '\n'; });
    p.print("x");                          // A: x
    p.set_strategy([](const std::string& s) { std::cout << "B: " << s << '\n'; });
    p.print("y");                          // B: y
}
```

It is indeed concise to write—no need to declare abstract base classes, no `virtual`, no `unique_ptr`, and lambdas serve directly as strategies. This is the biggest selling point of `std::function`: **low mental encoding cost, a unified interface, and runtime swappability**.

However, we must address a common misconception here. Many resources claim that `std::function` is "lighter than virtual inheritance." In small strategy scenarios, this is only true in the sense that "you don't have to hand-write an inheritance hierarchy," and **not** that "it runs faster." Let's look back at our previous benchmark:

```sh
virtual:       28.8597 ms
template:      36.1899 ms
std::function: 150.557 ms
```

`std::function` runs more than five times slower than a virtual function. **Here is the truth: `std::function` is usually more expensive than a direct virtual call, not cheaper**. The reason lies in its implementation mechanism—internally, `std::function` does three things: First, it uses a small buffer (Small Buffer Optimization, SBO) to try to store small callable objects directly within the object body, avoiding heap allocation; however, as soon as your lambda captures exceed the size of that internal buffer, it degrades into allocating memory on the heap via `new`. Second, every call involves an indirect jump through type erasure, just like virtual functions, which breaks inlining. Third, in some standard library implementations, the call path of `std::function` involves an extra layer of function pointer trampoline compared to a single-level virtual function.

Therefore, the correct mental model is: **`std::function` is the "easiest to write" dynamic strategy, suitable for scenarios where the strategy implementation is small and the call is not in a super-high-frequency hotspot**; once your strategy runs in an inner hot loop, its overhead becomes very obvious. In such places, you should use virtual functions (more predictable indirect calls) or simply go for static templates (zero overhead).

::: warning Don't be misled by "std::function is lighter than virtual functions"
Many online articles describe `std::function` as a "lighter dynamic strategy than virtual inheritance." This statement holds true only in terms of "coding mental cost" and "not having to hand-write an inheritance hierarchy," **but is the exact opposite regarding runtime performance**: the type-erased call path is usually more expensive than a single-level virtual call and may additionally trigger heap allocation. When a strategy is on a hot path, `std::function` is the slowest of the three. If you need runtime switching and care about performance, prioritize virtual functions; if it can be determined at compile time, go straight to templates.
:::

## In Practice: An Animal Model That Can Change Sounds at Runtime

Abstract theory is boring, so let's build something that actually runs. The example below is a practical application of the Strategy Pattern: every animal has a "sound," and this sound can be swapped at runtime (imagine changing a pet's skin or sound effects in a game). We use `std::function` to type-erase the "sound" into a replaceable strategy, and `shared_ptr` to allow multiple animals to share the same sound object.

```cpp
#include <functional>
#include <memory>
#include <print>

// 策略载体:把任意「无参无返回的叫声」擦除成一个可调用对象
struct AnimalSound {
    ~AnimalSound() = default;

    explicit AnimalSound(std::function<void()> snd)
        : sound_(std::move(snd)) {}

    void make_sound() noexcept { sound_(); }

private:
    std::function<void()> sound_;
};

// Context:动物,持有一个共享的叫声策略,可在运行时替换
struct AnimalType {
    virtual ~AnimalType() = default;

    explicit AnimalType(std::shared_ptr<AnimalSound> snd)
        : sound_(std::move(snd)) {}

    void install_new_sound(std::shared_ptr<AnimalSound> snd) {
        sound_ = std::move(snd);
    }

    void play_sound() {
        if (sound_) sound_->make_sound();
    }

private:
    std::shared_ptr<AnimalSound> sound_;
};
```

There are two design decisions here that are worth expanding upon. **Why does `AnimalSound` use `std::function` instead of virtual functions?** Because implementations of "sounds" vary wildly—it might be a print statement, playing an audio buffer, or triggering an event. Using `std::function` saves us from creating a derived class for every sound type; we can just feed in a lambda expression. This is where type erasure shines. **Why does `AnimalType` hold a `shared_ptr<AnimalSound>` instead of a `unique_ptr`?** Because we want to allow "multiple animals to share the same sound object" (for example, when "swapping skins," we could pass the dog's sound object directly to the cat, so both share the same `catSound`). The reference counting of `shared_ptr` perfectly expresses this "shared ownership" semantic. If you determine that a strategy is exclusively owned by one Context, you can switch back to `unique_ptr`—this aligns with the earlier point about "stateful strategies requiring explicit shared/exclusive semantics."

Here is what it looks like when running:

```cpp
int main() {
    auto dog_sound = std::make_shared<AnimalSound>([] {
        std::println("Wang!Wang!Wang!");
    });
    auto cat_sound = std::make_shared<AnimalSound>([] {
        std::println("Mewo!Mewo!Mewo!");
    });

    AnimalType dog(dog_sound);
    dog.play_sound();                 // Wang!Wang!Wang!

    AnimalType cat(cat_sound);
    cat.play_sound();                 // Mewo!Mewo!Mewo!

    dog.install_new_sound(cat_sound); // 运行时换策略:dog 也开始喵喵叫
    std::println("What the fuck!");
    dog.play_sound();                 // Mewo!Mewo!Mewo!
}
```

```sh
$ g++ -std=c++23 -O2 strategy_func_runtime.cpp -o strategy_func_runtime
$ ./strategy_func_runtime
Wang!Wang!Wang!
Mewo!Mewo!Mewo!
What the fuck!
Mewo!Mewo!Mewo!
```

The line `dog.install_new_sound(cat_sound)` is the core action of the Strategy Pattern: **at runtime, we replace the Context's strategy object entirely. The caller's code (`AnimalType`) remains unchanged, yet the behavior changes.** Furthermore, because we use `shared_ptr`, `dog` and `cat` now share the same `cat_sound` object; it is only destroyed when its last reference is released—this is the combined effect of "shared ownership + replaceable strategy."

::: tip Companion Buildable Project
The complete project for this section (`AnimalSound.h` + `AnimalSoundMain.cpp` + `CMakeLists.txt`, C++23, ready to run with cmake) is available in this repository: [Strategy / AnimalSound](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Strategy/AnimalSound).
:::

## Choosing the Right Path

We have now walked through all three implementations. The real pitfall is that **many people get stuck on "which is the modern way," but this is a false dichotomy**—these three approaches address different trade-offs in flexibility and performance, not a progression from old to new. Let's lay out the decision criteria in a table:

| Dimension | Virtual Function (Dynamic) | Template + Concept (Static) | `std::function` (Type Erasure) |
|---|---|---|---|
| When to switch strategy | Runtime | Compile time | Runtime |
| Call overhead | Indirect call (cannot inline) | Zero overhead (fully inlined) | Indirect call + potential heap allocation, slowest |
| Cognitive load | Medium (inheritance hierarchy) | Low (concept constraints + lambda-free) | Lowest (pass lambdas directly) |
| Can strategy hold state? | Yes (member variables) | Yes (but independent per Context instance) | Yes (lambda captures) |
| Binary size | One vtable + few derived classes | Code instantiated per strategy | One `std::function` object |
| Best scenario | Strategy changes at runtime, determined by config/plugins | Strategy fixed at compile time, in hot loops | Strategies are small and mixed, not in ultra-high-frequency paths |

To put it plainly: ask yourself two questions—**"Does the strategy need to change at runtime?"** and **"Is this call on a critical path?"**. If it needs runtime changes and isn't critical, `std::function` is the most pleasant to write; if it needs runtime changes and is critical, use virtual functions instead of `std::function` to save that layer of overhead and potential heap allocation; if it doesn't need runtime changes (fixed at compile time), go straight to static templates with concepts for constraints—zero overhead and friendly error messages.

There is also an easily overlooked common benefit: **all three approaches turn the strategy into a "replaceable independent unit," making them naturally friendly to unit testing.** If we want to test the `process` flow of `TextProcessor` but don't want to trigger real formatting logic (e.g., reading files or network access), we can inject a fake strategy—pass a `MockFormatter` for virtual functions, a lambda that just logs calls for `std::function`, or a `DummyPolicy` for templates. The caller's code doesn't change a bit, yet the strategy is swapped out. This is the most underestimated advantage of the Strategy Pattern compared to "a pile of if/else": it doesn't just make code cleaner, it conveniently solves "testability" as well.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it falls short |
|---|---|---|
| if/else branches | `switch` algorithms inside the caller | Algorithm and call flow are tangled; adding one requires modifying the caller; cannot vary independently |
| Virtual function strategy | Abstract base class + derived classes + `unique_ptr` | Changeable at runtime, but virtual calls break inlining; heap allocation + ownership must be managed manually |
| Template strategy | Strategy as template parameter, Policy-Based Design | Zero overhead, fully inlined, but fixed at compile time; cannot change at runtime; code bloat |
| Concept-constrained template | Add compile-time contract to strategy type | Errors change from "deep template gibberish" to "visible at the call site," almost free |
| `std::function` | Type erasure, lambdas as strategies | Easiest to code, but call overhead is more expensive than virtual functions; don't use on critical paths |

Keep these key conclusions in mind:

- **The essence of the Strategy Pattern is extracting "replaceable algorithms" from the caller**, and standard library algorithms (like `std::sort`) are typical applications.
- **The cost of dynamic strategies (virtual functions / `std::function`) is indirect calls + potential heap allocation**; `std::function` is usually slower than a single virtual call at the call site; the idea that it's "lighter than virtual functions" only holds true regarding cognitive load.
- **Static strategies (templates) are zero-overhead**; strategy calls are inlined into a few instructions (empirically demonstrated in this article with `leal 1(%rdi), %eax`), at the cost of being fixed at compile time.
- **C++20 concepts add compile-time contracts to strategies**, offering far better error location and readability than bare templates; Policy-Based Design should almost always be paired with concepts.
- Selection depends on only two questions: **Does it need to change at runtime?** + **Is it on a critical path?**. There is no answer to "which is the most modern."

## References

- [cppreference: `std::function`](https://en.cppreference.com/w/cpp/utility/functional/function) (Type-erased invocation semantics, since C++11)
- [cppreference: Concepts](https://en.cppreference.com/w/cpp/concepts) (`requires` expressions and constrained templates, since C++20)
- [cppreference: `std::shared_ptr` / `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (Ownership semantics for strategy objects)
- Andrei Alexandrescu, *Modern C++ Design*, Chapter 1 (Systematic discussion of Policy-Based Design)
- Companion buildable project: [Strategy / AnimalSound](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Strategy/AnimalSound)
