---
title: 'Strategy Pattern: From a Heap of if/else to Compile-Time Swappable Policies'
description: 'Starting from the most intuitive "write a pile of if/else branches" approach, we work our way step by step toward dynamic virtual-function strategies, static template policies, and `std::function` type erasure, spell out the cost of each of the three styles, and finish by putting compile-time constraints on policies with C++20 concepts'
chapter: 11
order: 12
tags:
  - host
  - cpp-modern
  - intermediate
  - 策略模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/12-strategy.md
  source_hash: 846240b6ab631e0c07ae784ae0c9ab5d441357ecc6d6603048d1c902110fb101
  translated_at: '2026-09-26T05:31:56+00:00'
  engine: anthropic
  token_count: 6900
---
# Strategy Pattern: From a Heap of if/else to Compile-Time Swappable Policies

## What Problem Are We Actually Solving

Let's hold off on definitions. Think of a scenario you've absolutely hit before: you're writing a text processor that has to "format" a string according to some rule. At first there's exactly one requirement—uppercase it—so you dash off a `toupper` and call it done. Two days later, product wants lowercase too, so you add an `if (mode == LOWER)`. Another week goes by, and the requirement becomes "camel case, snake case, kebab case—do them all"—and now your `format` function is stuffed with `if` and `switch`. Every new rule makes the function fatter still, and the rules start interfering with each other: touch one carelessly and you break another.

The root of it all: **"which algorithm to use" and "who invokes that algorithm" are tangled together inside one function.** If you want to swap the algorithm, you have to touch the calling flow that should have stayed stable.

The Strategy Pattern exists to untangle exactly this. Its core idea fits in one sentence: **pull a family of interchangeable algorithms out of the caller, encapsulate each one as an independent "strategy" object or type, and have the caller (the Context) depend on nothing but a single unified interface—which strategy is actually used can be deferred until runtime, or even until compile time.** From then on, adding a new algorithm means adding a new strategy, and the caller doesn't move a line. You've been using this pattern every day without knowing it: the standard library algorithms (`std::sort`, `std::transform`) are designed as textbook strategy patterns—they factor "comparison strategy" and "transformation strategy" out into swappable parameters (function objects, lambdas, callables constrained by concepts).

But "swappable" lands on two entirely different implementation levels in C++, with entirely different costs. One is the **dynamic strategy, swappable at runtime**, built on virtual functions or `std::function`; the other is the **static strategy, swappable at compile time**, built on template parameters (plus C++20 concepts constraints). The two are not a "which one is more advanced" relationship—they are **two paths resolving different performance/flexibility trade-offs**. So let's go step by step, starting from the dumbest version, and see why each step still isn't enough.

## Step One: The Most Primitive Version—A Pile of if/else (A Cautionary Example)

The first time many people face "one operation, several implementations", the code their fingers produce on autopilot looks like this:

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
            return to_snake(s);  // assume this already exists
    }
    return s;
}
```

It runs, but the problems are hiding behind it. First, **every new algorithm forces another branch into this `switch`**—the more algorithms, the longer and more brittle the function gets. Second, **the branches' code is physically squeezed together**; as the function grows, the odds of nicking the Upper branch while editing the Lower branch climb linearly with its size. Third, the fatal one—**the algorithm and the flow that calls it cannot vary independently**. The day you want "the config file decides which formatting to use", you'll discover this `switch` is hard-coded inside `format_text`: there is simply no "swap the strategy in and out" action available to operate on.

The essence of the problem: the algorithm isn't encapsulated as an independent, replaceable thing—it's just a branch inside the caller. We first have to "extract" the algorithm, so that the caller gets hold of an abstract "strategy" instead of judging itself which branch to take.

## Step Two: Extract a Strategy Interface—Virtual Functions for Dynamic Strategies

The most intuitive way to "extract" is the classic object-oriented move: define an abstract base class as the strategy interface, make each algorithm a derived class, and have the caller hold a pointer to the base; to swap algorithms, plug a different derived object in.

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

Then the caller (the Context) depends only on that abstract interface and doesn't care which derived class is behind it:

```cpp
#include <memory>

class TextProcessor {
public:
    explicit TextProcessor(std::unique_ptr<IFormatter> f)
        : formatter_(std::move(f)) {}

    void set_formatter(std::unique_ptr<IFormatter> f) {  // replaceable at runtime
        formatter_ = std::move(f);
    }

    std::string process(const std::string& s) {
        return formatter_->format(s);
    }

private:
    std::unique_ptr<IFormatter> formatter_;
};
```

See—`TextProcessor` no longer contains a single branch of its own; all it knows is the `IFormatter` interface. "Which formatting to use" is deferred until construction: whichever derived class gets stuffed in over in `main` is the one it uses. And `set_formatter` lets us swap strategies while the program is running. That's what the "dynamic" in "dynamic strategy" really means: **the choice of strategy happens at runtime, can be replaced at any moment, and can even be decided by a config file, user input, or a plugin**.

Let's first verify that it really runs and really switches at runtime:

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

Satisfying. But we're not done here—this style has a cost, and the cost hides inside that `->`.

## Cost One: The Indirect Jump of a Virtual Call

The line `formatter_->format(s)` does not compile into a direct function call. What it does is: first fetch the **vtable pointer (vptr)** from the object `formatter_` points to, then look up the `format` slot in the vtable, and finally jump to whatever address that slot holds. That is an **indirect call**—the CPU can't know ahead of time where the next instruction lives; it has to look it up and jump on the spot.

The real problem with an indirect call isn't "one extra memory fetch" per se, but that **it punches through the compiler's inlining**. With a direct call, the compiler sees the function body and can flatten the whole call into the caller, saving the entire rigmarole of pushing arguments, return addresses, and register saves; but a virtual function's true target is known only at runtime, so the compiler doesn't dare inline its body in. On a hot path, this gap gets very visible.

So how big is it, exactly? Talk is cheap, so let's write a micro-benchmark: run the same `x + 1` operation one hundred million times each via "virtual function", "template", and "`std::function`", and look at the real timings:

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
    volatile int sink = acc;  // keep the whole loop from being optimized away
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

Look closely at these numbers. The template entry barely does better than "doing nothing"—the compiler inlined `AddOnePolicy::transform` wholesale into the loop, and `x + 1` got folded by loop induction into a single arithmetic instruction; the function call no longer exists at all. The virtual function is nearly twice as slow—that's the price of the indirect call plus no inlining. And `std::function` is the most absurdly slow of all; it gets its own dedicated discussion later in this section.

Timings alone aren't intuitive enough, so let's drag out the assembly compiled from the template strategy and confirm what it actually got inlined into:

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

The entire `hot` function compiled down to three instructions—`leal 1(%rdi), %eax` (that's `x + 1` stored into the return register) plus `ret`. No `call`, no vtable lookup: the strategy's function body has thoroughly melted into the caller. **This is the most hardcore advantage of "compile-time swappable" over "runtime swappable": it optimizes a strategy call down to zero overhead.** A virtual function can never reach that point, because its target is settled only at runtime.

## Cost Two: Object Lifetime and Pointer Management

Dynamic strategies carry one more layer of hassle—`formatter_` is a `unique_ptr<IFormatter>`, and the object it points to lives on the heap. That means the strategy object has to be `new`ed, then `delete`d when done, and every `set_formatter` may release the old object and allocate a new one. Heap allocation itself isn't cheap (a single `new` costs far more than a single virtual call), and in embedded, real-time, or hot-loop settings, "swapping a strategy triggers a heap allocation" is a rotten property to have.

Subtler still is ownership semantics. If the strategy is **stateful** (it has internal members—say a counter or a cache), you have to think it through: `unique_ptr` says "this Context owns the strategy exclusively", whereas if you want **multiple Contexts to share one strategy instance** (say, a global caching strategy reused in many places), you have to switch to `shared_ptr`. You'll see a `shared_ptr` example in the practical code later in this section; for now, just file it away: lifetime management for dynamic strategies is the second bill you pay for "swappable at runtime".

## Step Three: Move the Strategy into Compile Time—Templates for Static Policies

If, in our requirements, **the strategy is settled at compile time and never needs to switch at runtime**, then neither of the two bills from the previous section has to be paid at all. We can simply pass the strategy in as a template parameter of the Context and let the compiler nail down "which strategy to use" at the moment it instantiates the template:

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

Usage looks like this—the strategy is fixed inside the type:

```cpp
TextProcessor<UpperCasePolicy> up;
TextProcessor<LowerCasePolicy> low;

std::cout << up.process("Hello") << '\n';   // HELLO
std::cout << low.process("Hello") << '\n';  // hello
```

Notice we wrote no inheritance and no `virtual` here. `Policy` is a pure type parameter: in the compiler's eyes, `TextProcessor<UpperCasePolicy>` and `TextProcessor<LowerCasePolicy>` are **two completely different types**, each instantiated with its own copy of the code, each inlining `Policy::format` into its own `process`. That's why the template entry in the earlier benchmark could run in a handful of milliseconds—no vtable, no indirect call, no heap allocation; the strategy call is flattened out entirely at compile time.

This style has a name: **Policy-Based Design** (Andrei Alexandrescu covered it systematically in *Modern C++ Design*). Its essence: **demote "strategy" from a runtime object down to a compile-time type**. You no longer "hold a strategy object"—you are "parameterized by a strategy type". Combined with `static` member functions, the strategy doesn't even need an instance: it's just a bunch of free functions hanging off a namespace, gathered up by a template parameter.

But this road has its own hard flaws. **The most fatal one: once a strategy is nailed down at compile time, it can never be swapped again at runtime.** `TextProcessor<UpperCasePolicy>` uppercases forever; want it to switch to lowercase halfway through? Can't be done—you'd have to grab an object of type `TextProcessor<LowerCasePolicy>` instead. The two aren't the same type at all: they can't be assigned to each other, can't sit in the same container, can't be received by the same variable. If your strategy has to be "picked by the user at runtime" or "decided by a config file", static strategies are simply no help.

The second, sneakier cost: **templates expand code into every instantiation**. Hand it five strategies and the compiler generates five copies of `TextProcessor::process`, and the binary grows. For a pattern like this one, whose function bodies are tiny, that usually doesn't matter—but if you have dozens of strategies and every `process` is heavy, that code bloat deserves a moment on the scales.

## A Quick Verification: How a Concept Puts Compile-Time Constraints on a Policy

Template policies hide one more pitfall that newcomers overlook: **the template parameter `Policy` is completely unconstrained**. Write `TextProcessor<Foo>`, and as long as `Foo` gets that function body to compile, it passes; the moment `Foo` has no `format` member, or `format`'s signature is wrong, the compiler hands you a long scroll of template-instantiation "gibberish", with the error location usually pointing at some line inside the template rather than the line where you wrote `TextProcessor<BadFoo>`. This is exactly the problem C++20 concepts were invented to solve—**giving the strategy type an explicit, readable contract**.

Let's first define a concept that spells out "what a qualified Formatter strategy looks like": it must have a `static format(std::string) -> std::string`:

```cpp
#include <concepts>
#include <string>

template <typename F>
concept Formatter = requires(F f, std::string s) {
    { F::format(std::move(s)) } -> std::same_as<std::string>;
};
```

This `requires` expression asks the compiler a question: "Given an object `f` of type `F` and a `std::string s`, can `F::format(std::move(s))` be called, and does it return exactly `std::string`?" If yes, `F` satisfies `Formatter`; if no, it doesn't. Now attach the constraint to the template parameter:

```cpp
template <Formatter F>
class TextProcessor {
public:
    std::string process(const std::string& s) { return F::format(s); }
};
```

A good strategy (signature matches) works as usual:

```sh
$ g++ -std=c++20 -O2 strategy_concept.cpp -o strategy_concept
$ ./strategy_concept
HELLO
```

Now we deliberately write a **bad** strategy—one whose return type is `const char*` instead of `std::string`—plug it into the concept-constrained `TextProcessor`, and see how the compiler complains:

```cpp
struct BadFormatter {
    static const char* format(std::string s) { return s.c_str(); }  // wrong return type
};

int main() {
    TextProcessor<BadFormatter> tp;   // should fail right here
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

See the difference? The error location points precisely at the line where you wrote `TextProcessor<BadFormatter>`, and it tells you outright that "`F::format(...)` does not satisfy the return-type requirement". That's the value of a concept over a bare template—**it lifts "what a strategy should look like" from "a detonation deep inside the template" up to "a contract violation visible at a glance at the call site"**. Writing Policy-Based Design in modern C++, adding a concept constraint to your policy is pretty much a free lunch; there's no reason to skip it.

## Step Four: Type Erasure—`std::function` as a Lightweight Dynamic Strategy

At this point we're holding two paths: virtual functions switch at runtime but cost heap allocation plus indirect calls, while templates are zero-overhead but frozen at compile time. So is there a middle ground—**runtime switching, without hand-rolling an inheritance hierarchy ourselves**? Yes: `std::function`.

The essence of `std::function` is **type erasure**: it can hold any callable whose "signature matches"—lambda, function pointer, functor, bind expression—all hidden behind one uniform type. You don't define a derived class per strategy; just write a lambda and drop it in:

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

It genuinely is shorter to write—no abstract base class, no `virtual`, no `unique_ptr`; a lambda straight-up serves as the strategy. That's `std::function`'s biggest selling point: **low coding effort, a unified interface, swappable at runtime**.

But there's a misconception here that must be punctured. Plenty of material claims `std::function` is "lighter than virtual inheritance"; the part of that claim that holds in small-strategy scenarios is "you don't hand-write an inheritance hierarchy"—and **not** "it runs faster". Look back at the benchmark from before:

```sh
virtual:       28.8597 ms
template:      36.1899 ms
std::function: 150.557 ms
```

`std::function` came in more than five times slower than the virtual function. **That's the truth: at the call level, `std::function` is usually more expensive than a direct virtual call, not cheaper.** The reason lies in its implementation—internally, `std::function` does three things. First, it uses a small buffer (small buffer optimization, SBO) to try storing small callables directly inside the object body and dodge the heap; but the moment your lambda's captures exceed that internal buffer's size, it degrades into `new`ing a block on the heap to hold it. Second, every call goes through a type-erased indirect jump, which breaks inlining just like a virtual function does. Third, in some standard library implementations, the `std::function` call path carries one more function-pointer trampoline than a single-level virtual call.

So the correct mental model is: **`std::function` is the "cheapest to write" dynamic strategy, fitting scenarios where the strategy implementation is small and the call site isn't a super-high-frequency hotspot**; once your strategy runs inside an inner hot loop, its overhead becomes glaring, and that's where you should reach for virtual functions (a more predictable indirect call) or simply go static with templates (zero overhead).

::: warning Don't Be Misled by the Claim That `std::function` Is Lighter Than a Virtual Function
Plenty of articles online describe `std::function` as "a dynamic strategy lighter than virtual inheritance". That claim holds only in the senses of "coding effort" and "no hand-written inheritance hierarchy"; **on runtime performance it's exactly the opposite**: the type-erased call path is usually more expensive than a single-level virtual call, and it may additionally trigger heap allocation. With a strategy on a hot path, `std::function` is the slowest of the three. Need runtime switching and care about performance? Look at virtual functions first; if the strategy can be fixed at compile time, go straight to templates.
:::

## In Practice: An Animal Model That Swaps Its Sound at Runtime

Abstract talk is hollow, so let's do something that actually runs. The example below is a very down-to-earth landing of the Strategy Pattern: every animal has a "sound", and that sound can be swapped at runtime (picture swapping skins/sound effects for a pet in a game). We use `std::function` to type-erase "sound" into a replaceable strategy, and `shared_ptr` so several animals can share one sound object.

```cpp
#include <functional>
#include <memory>
#include <print>

// Strategy carrier: type-erases any "zero-argument, zero-return sound" into one callable
struct AnimalSound {
    ~AnimalSound() = default;

    explicit AnimalSound(std::function<void()> snd)
        : sound_(std::move(snd)) {}

    void make_sound() noexcept { sound_(); }

private:
    std::function<void()> sound_;
};

// Context: the animal, holding a shared sound strategy, replaceable at runtime
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

Two design decisions here deserve elaboration. **Why does `AnimalSound` use `std::function` instead of virtual functions?** Because implementations of a "sound" come in every shape—it might be a printout, playing an audio buffer, or firing an event—and `std::function` spares us one derived class per sound: just feed a lambda in. That's type erasure's home turf. **Why does `AnimalType` hold `shared_ptr<AnimalSound>` instead of `unique_ptr`?** Because we want "one sound object shared by several animals" to be possible (when "swapping skins", say, hand dog's sound object straight to cat so both share the same `catSound`), and `shared_ptr`'s reference counting expresses exactly that "shared ownership" semantics. If you're certain a strategy belongs to exactly one Context, switch back to `unique_ptr`—this is the earlier point about "stateful strategies demanding explicit shared/exclusive semantics".

Running it looks like this:

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

    dog.install_new_sound(cat_sound); // swap the strategy at runtime: dog starts meowing too
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

The line `dog.install_new_sound(cat_sound)` is the Strategy Pattern's signature move: **at runtime, replace the Context's strategy object wholesale—the caller's (`AnimalType`'s) code doesn't change a single line, yet the behavior changes.** And because `shared_ptr` is what's used, `dog` and `cat` now share the same `cat_sound`; that object destructs only when its last reference is released. That's the effect you get by stacking "shared ownership + replaceable strategy" on top of each other.

::: tip Companion Buildable Project
The complete project for this section (`AnimalSound.h` + `AnimalSoundMain.cpp` + `CMakeLists.txt`, C++23, runs with a single cmake invocation) is in this repository: [Strategy / AnimalSound](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Strategy/AnimalSound).
:::

## Choosing Among the Three Paths

At this point we've walked all three implementations. The real trap is that **many people agonize over "which one is the modern style", when that's a false question**—the three styles resolve different flexibility/performance trade-offs; there's no old-versus-new between them. Let's spread the decision criteria out in a table:

| Dimension | Virtual function (dynamic) | Template + concept (static) | `std::function` (type erasure) |
|---|---|---|---|
| When the strategy switches | Runtime | Compile time | Runtime |
| Call overhead | Indirect call (no inlining) | Zero overhead (fully inlined) | Indirect call + possible heap allocation, slowest |
| Coding effort | Medium (must write an inheritance hierarchy) | Low (concept constraints + lambda-free) | Lowest (feed a lambda directly) |
| Can the strategy hold state | Yes (member variables) | Yes (but independent per Context instance) | Yes (lambda captures) |
| Binary size | One vtable + a few derived classes | One code instantiation per strategy | One `std::function` object |
| Fitting scenarios | Strategy varies, decided at runtime by config/plugins | Strategy known at compile time, running in hot loops | Strategies small and miscellaneous, off the ultra-high-frequency path |

Put plainly, you ask yourself two questions—**"does the strategy need to swap at runtime?"** and **"is this call on a hot path?"**. Runtime swapping and not on a hot path: `std::function` is the most comfortable to write. Runtime swapping and on a hot path: swap `std::function` out for a virtual function and drop that extra layer of overhead and the potential heap allocation. No runtime swapping needed (it can be fixed at compile time): go straight to static templates—and while you're at it, give it a concept contract: zero overhead, friendly errors.

There's also one easily missed benefit shared by all three: **each style turns the strategy into "a replaceable independent unit", so all three are naturally unit-test friendly**. Suppose you want to test `TextProcessor`'s `process` flow without actually triggering the real formatting logic (say it reads files or goes over the network)—stuff in a fake strategy: a `MockFormatter` for the virtual version, a call-logging-only lambda for `std::function`, a `DummyPolicy` for the template. The caller's code doesn't move a line, and the strategy is swapped out. That's the most underrated advantage the Strategy Pattern has over "a pile of if/else": it doesn't just make the code tidier—it solves testability along the way.

## Summary

Let's trace the whole evolutionary path once more:

| Stage | Approach | Why it still isn't enough |
|---|---|---|
| if/else branches | `switch` on the algorithm inside the caller | Algorithm and call flow tangled together; one addition means one edit; no independent variation |
| Virtual-function strategy | Abstract base class + derived classes + `unique_ptr` | Swappable at runtime, but virtual calls break inlining, and heap allocation + ownership are yours to manage |
| Template strategy | Strategy as a template parameter, Policy-Based Design | Zero overhead, fully inlined, but frozen at compile time, unswappable at runtime, code bloat |
| Concept-constrained template | Compile-time contract on the strategy type | Errors go from "gibberish deep inside the template" to "visible at a glance at the call site"; nearly free |
| `std::function` | Type erasure, lambda directly as the strategy | Cheapest to write, but call overhead is higher than a virtual function; keep it off hot paths |

Note down these key conclusions:

- **The essence of the Strategy Pattern is pulling "replaceable algorithms" out of the caller**; the standard library algorithms (`std::sort` and friends) are its classic application.
- **The cost of dynamic strategies (virtual functions / `std::function`) is indirect calls + possible heap allocation**; `std::function` is usually slower than a single-level virtual call at the call level—"lighter than a virtual function" holds only in the coding-effort sense.
- **Static strategies (templates) are zero-overhead**; the strategy call gets inlined down to a few instructions (this article proved it with `leal 1(%rdi), %eax`), at the cost of being frozen at compile time, unswappable at runtime.
- **C++20 concepts give strategies a compile-time contract**; error location and readability are far better than bare templates—writing Policy-Based Design almost always calls for pairing it with a concept.
- Selection comes down to just two questions: **runtime swapping or not** + **hot path or not**. There is no answer of the form "whichever is most modern".

## References

- [cppreference: `std::function`](https://en.cppreference.com/w/cpp/utility/functional/function) (type-erased call semantics, since C++11)
- [cppreference: Concepts](https://en.cppreference.com/w/cpp/concepts) (`requires` expressions and constrained templates, since C++20)
- [cppreference: `std::shared_ptr` / `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (ownership semantics of strategy objects)
- Andrei Alexandrescu, *Modern C++ Design*, Chapter 1 (the systematic treatment of Policy-Based Design)
- Companion buildable project: [Strategy / AnimalSound](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Strategy/AnimalSound)
