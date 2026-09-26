---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 talk notes — templates should not be compiled in isolation,
  concepts as compile-time functions for building your own type system, interface
  inheritance and concepts complementing each other, and the ecosystem work ahead
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 28
speaker: Bjarne Stroustrup
tags:
- cpp-modern
- host
- intermediate
talk_title: Concept-based Generic Programming
title: Template Compilation Model and the Road Ahead
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/04-template-compilation-and-future.md
  source_hash: e836440db3417fdb31349353273529f82ab0158b0fec98be20e4f1eb5de8dffe
  translated_at: '2026-09-26T15:17:03+00:00'
  engine: anthropic
  token_count: 6500
---
# Templates Should Not Be Compiled in Isolation

Earlier, when we talked about constraining templates with concepts, a question kept circling in my head: if I put precise concept constraints on `advance`'s parameters—say, requiring a `random_access_iterator`—doesn't that shut `input_iterator` out of the party? Would I have to write a pile of overloads for the different iterator categories, each advancing the iterator in its own way? And doesn't that put us right back on the old road of unstable interfaces—every time I support one more iterator type, I have to go back and change `advance`'s declaration?

Honestly, this question bugged me for quite a while. I used to think that once we had concepts, "splitting templates apart and compiling them in isolation" should be the ideal state—each template checked on its own, passing on its own, then assembled afterwards. But this iterator-advancing example was a wake-up call for me: turns out we can't do that—and we shouldn't.

## First, a Seemingly Simple Question

You might ask: what does "compiling templates in isolation" even mean? The way I understand it, it's this: when the compiler sees a template definition, it judges whether the template is legal purely from the concept constraints on the template's signature, without ever looking at what operations the concrete type actually passed in at the call site can provide.

Sounds lovely, right? But the trouble shows up immediately.

Look at `std::advance`. Its job is to push an iterator forward by n steps. For different iterator categories, the way you advance is completely different: a `random_access_iterator` can just `+= n` and be done in one shot; an `input_iterator` has no `+=` and can only `++` one step at a time.

I always used to think that `input_iterator` not having `+=` was some kind of "defect" in the standard, or at least a limitation that ought to be fixed. Not so—there is a solid reason `input_iterator` doesn't provide `+=`: it stands for the abstraction of "advance one at a time, never jump." It's a feature, not a defect.

## Let's Write an Example to Get a Feel for It

I wrote some code to verify this behavior, running on my Arch Linux WSL, with GCC 16.1.1 and `-std=c++20`.

```cpp
#include <vector>
#include <list>
#include <iostream>

// A simplified advance that mimics the standard library's behavior
template<typename Iter>
void my_advance(Iter& it, int n) {
    // If the iterator supports +=, jump straight there
    if constexpr (requires(Iter i, int m) { i += m; }) {
        it += n;
    } else {
        // Otherwise, walk one step at a time
        for (int i = 0; i < n; ++i) {
            ++it;
        }
    }
}

int main() {
    // vector's iterator is a random_access_iterator and supports +=
    std::vector<int> vec = {10, 20, 30, 40, 50};
    auto vit = vec.begin();
    my_advance(vit, 2);
    std::cout << *vit << "\n";  // outputs 30

    // list's iterator is a bidirectional_iterator and does not support +=
    std::list<int> lst = {10, 20, 30, 40, 50};
    auto lit = lst.begin();
    my_advance(lit, 2);
    std::cout << *lit << "\n";  // outputs 30

    return 0;
}
```

Run it, and the output matches expectations exactly: two 30s. See that? The same `my_advance` uses `+=` for `vector` and a `++` loop for `list`. The reason this works at all is precisely that a template, before being instantiated, doesn't go check "do you or do you not support `+=`" in isolation—it waits until the moment it sees the concrete type, and only then makes the choice via `if constexpr`.

## What If Templates Really Were Compiled in Isolation

Now let's suppose C++ someday implements isolated compilation of templates, with no exemption clause. What happens?

When the compiler sees the definition of `my_advance`, it checks every line of code inside for legality against the types the constraints describe. If my constraint says `input_iterator`, and `input_iterator`'s definition doesn't include `+=`, the compiler rejects the line `it += n` on the spot—even though it would never be executed at runtime (because the `if constexpr` branch blocks it off).

Then I'd have no choice but to split the code into two overloads:

```cpp
// The version for random_access_iterator
template<std::random_access_iterator Iter>
void my_advance(Iter& it, int n) {
    it += n;
}

// The version for other iterators
template<std::input_iterator Iter>
    requires (!std::random_access_iterator<Iter>)
void my_advance(Iter& it, int n) {
    for (int i = 0; i < n; ++i) {
        ++it;
    }
}
```

Doesn't look that bad, does it? But think it through: this takes "one algorithm making different choices based on type capabilities" and pushes it from inside the algorithm out to the interface level. Every additional iterator category that needs special handling means one more overload. The interface bloats, maintenance costs climb, and at bottom I'm duplicating the same logic.

Even more critical is performance. If `advance` can't use `+=` on a `random_access_iterator` and is forced into a `++` loop, the complexity goes from O(1) to O(n). When that gets called inside an algorithm whose own outer loop is also O(n), the whole thing explodes from O(n) to O(n^2). For large data volumes, that's fatal.

A big part of why the STL is efficient is exactly this ability to "branch inside a template based on type capabilities." If isolated compilation walls this path off, the STL's performance advantage takes a heavy hit.

## So What Are Concepts Actually Good For

At this point you might ask: so are concepts useless after all? Earlier you said they catch errors sooner; now you say they can't be checked in isolation. Isn't that self-contradictory?

I was confused at first too, but once I thought it through, it isn't a contradiction at all. The value of concepts is this: when there truly is no type in the system that satisfies the constraint, the error gets caught earlier, and the error message is far clearer—it tells you "the type you passed in doesn't satisfy `input_iterator`," instead of spitting out a screenful of unreadable template-instantiation backtraces.

But "catching errors earlier" and "compiling templates in isolation" are two different things. A template still has to see the concrete type before the final legality judgment can be made; concepts just make that judgment's failure messages readable. In that sense, templates have been type-safe all along—it's just that back then you couldn't understand the errors when they fired, and now you can.

## Pragmatism, Not Dogma

So what's the conclusion? At least at this stage, we shouldn't be chasing isolated compilation of templates. If C++ really does implement this feature one day, there has to be some exemption mechanism so that code shaped like `advance`—"branching internally based on type capabilities"—can legally exist. Because honestly, this pattern is all over the software infrastructure we use every day.

It reminds me of when I was learning templates: I always believed "the stricter the constraints, the better" and wanted to lock down every single template parameter. But after writing plenty of real code, I realized the essence of generic programming is exactly this: you describe a minimal set of requirements, then inside the implementation you adapt flexibly to types with different capabilities. That's not laziness—that's pragmatism.

## Back to the Essence of Generic Programming

After all that wrestling, I went back and re-thought what generic programming actually is. It's no mysticism—it's just programming itself, done in the most general, most efficient, most comfortable-to-write way. The "concept" here doesn't mean the C++20 language feature; it means your general abstraction of an idea: what an iterator is, what a callable is, what a range is.

None of this was invented by C++. Go open Alexander Stepanov and Daniel E. Rose's *From Mathematics to Generic Programming* (published in Chinese as 《数学与泛型编程：高效编程的奥秘》)—it's pure mathematics all the way down: algebraic structures, axioms, theorems. If you don't like math, that book is genuinely painful reading (I confess I put it down after a few pages). But the core idea is really quite plain: find the algebraic structure that different types share, then write algorithms against that structure, not against some specific type.

And generic programming had the uniform use of types built in from day one—how scopes are managed, how names are resolved, how objects are created and destroyed—these matter just as much in generic code as in any other code. It was introduced before C++ ever showed up; C++ merely expressed this body of thought through the mechanism of templates.

At this point I finally worked out "why templates shouldn't be compiled in isolation." Looking back, it isn't complicated at all—just don't sacrifice real-world flexibility and performance for theoretical purity. At the end of the day, we write code to solve problems, not papers proving purity.

---

# Concepts: Building Your Own Type System at Compile Time

Honestly, when I saw this conclusion, it was an epiphany. While learning Concepts, I had always treated them as "more elegant SFINAE"—syntactic sugar for constraining template parameters, just nicer to write than `std::enable_if`. But after an evening of fooling around with them, I finally got it: the essence of Concepts is **compile-time functions**—they take types and values as arguments and return a bool, telling you whether a type satisfies some condition. Once that shift in understanding landed, a whole lot of things clicked into place at once.

## First, Get This Straight: What Concepts Are Actually Doing

I had a long-standing misconception that Concepts describe "what a type looks like"—say, "it must have `begin()` and `end()`." But that's not it at all. Concepts describe "a generic function's requirements on its parameters," and they **do not care how those requirements are satisfied**. This distinction is crucial, and at first I completely missed it.

What do I mean? Suppose you write a Concept requiring "can do addition." You don't have to say "implemented via `operator+`" or "implemented via some member function"—you just say "being addable is enough." The compiler works the rest out for itself. This is a completely different world from classic object-oriented programming's "must inherit from this base class, must override that virtual function"—OOP prescribes, top-down, "how you must provide it," while Concepts say, bottom-up, "here is what I need."

What's more, Concepts can take multiple arguments, not just a single type argument. That means you can express cross-type constraints like "an operation is possible between type A and type B"—something traditional OOP has essentially no elegant way to express.

## Let's Write a Few Concepts to Get a Feel

My test environment is Arch Linux WSL, GCC 16.1.1, compile command with `-std=c++20 -Wall -Wextra`. The code below is my own, written to verify the understanding that "Concepts are compile-time functions":

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>

// The simplest Concept: takes one type parameter, returns bool
template<typename T>
concept HasSize = requires(T t) {
    { t.size() } -> std::convertible_to<std::size_t>;
};

// A Concept taking two type parameters: expresses a cross-type constraint
template<typename T, typename U>
concept CanAdd = requires(T a, U b) {
    a + b;  // only requires that a + b is a valid expression
};

// A Concept taking a type parameter and a value parameter
// sizeof is evaluated at compile time, so no runtime information is needed here
template<typename T, std::size_t N>
concept IsLargeType = (sizeof(T) >= N);

void test_single_param(HasSize auto& container) {
    std::cout << "size = " << container.size() << "\n";
}

// A two-parameter concept cannot use the "CanAdd auto" syntax — that only works for single-parameter concepts
// You must use an explicit requires clause and pass both template arguments in
template<typename T, typename U>
    requires CanAdd<T, U>
void test_cross_add(T a, U b) {
    auto result = a + b;
    std::cout << "a + b = " << result << "\n";
}

int main() {
    std::string s = "hello";
    test_single_param(s);  // OK, string has size()

    // test_single_param(42);  // compile error: int does not satisfy HasSize

    test_cross_add(10, 20);      // OK, int + int -> int
    test_cross_add(10, 3.14);    // OK, int + double -> double

    // test_cross_add("hello", 42);  // compile error: const char* + int is not valid

    static_assert(IsLargeType<double, 4>);  // sizeof(double) == 8 >= 4
    static_assert(!IsLargeType<char, 4>);   // sizeof(char) == 1 < 4
}
```

Run it and you'll see: `HasSize` takes one type parameter, `CanAdd` takes two type parameters, and `IsLargeType` is the most interesting of all—it takes a type and a compile-time value at the same time. These three parameter forms combine freely; the expressive power is enormous.

One pitfall did cost me half a day, though: a multi-parameter concept cannot be written directly in front of `auto` as a constraint the way a single-parameter one can (for instance, `CanAdd auto a` fails to compile on the spot, because `CanAdd` needs two template arguments and you only handed it one). Multi-parameter concepts must use an explicit `requires` clause to pass the arguments in. Single-parameter concepts have no such restriction—`HasSize auto& container` reads perfectly naturally.

## Overloading with Concepts: Simpler Than Plain Overloading

This was the part that confused me the most before. I used to think template overloading was a nightmare—you had to do partial specialization with SFINAE, error messages ran three screens long, and the rules were complex enough to make you question your life choices. But overloading with Concepts has rules that are actually **simpler** than plain function overloading.

I wrote an example to verify these three cases:

```cpp
#include <iostream>
#include <concepts>
#include <vector>
#include <list>

// Constraint A: a sortable container
template<typename T>
concept SortableContainer = requires(T t) {
    requires std::ranges::range<T>;
    requires std::totally_ordered<typename T::value_type>;
};

// Constraint B: a sortable random-access container (stricter than A)
template<typename T>
concept RandomAccessSortable = SortableContainer<T> && requires(T t) {
    requires std::random_access_iterator<typename T::iterator>;
};

// Case 1: only one matches
void process(SortableContainer auto& c) {
    std::cout << "sortable container\n";
}

// Case 2: both match, but one is a subset of the other -> pick the stricter one
void process(RandomAccessSortable auto& c) {
    std::cout << "random access sortable container\n";
}

int main() {
    std::list<int> lst = {3, 1, 2};
    std::vector<int> vec = {3, 1, 2};

    process(lst);  // only matches SortableContainer -> outputs "sortable container"
    process(vec);  // both match, but RandomAccessSortable is stricter -> outputs "random access sortable container"
}
```

See? There are just three rules, crystal clear: if only one matches, use it; if two match and one is a subset of the other, pick the stricter one; everything else is an error. None of the implicit-conversion ranking and ambiguity-resolution machinery that plain overloading drags along. I was stuck on this for ages, convinced Concepts overloading had to have some hidden trap somewhere—then I looked back at the principle, and it really is that simple.

## The Part That Really Lit Me Up: Extending C++'s Type System

This is where it gets genuinely interesting for me. I always thought C++'s type system was fixed—int is int, double is double, narrowing conversions are unsafe, and all you can do is steer around them or get a `-Wnarrowing` warning. But Concepts let you **build your own type system** at compile time, intercepting things that used to need runtime checks right at compilation.

Following that thread, I wrote my own `SafeNumericConvert` Concept to separate "safe numeric conversions" from "narrowing conversions that may lose data" at compile time:

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>
#include <limits>
#include <stdexcept>

// Compile-time judgment: whether converting from From to To might lose data
// Safe condition: To is strictly wider, or the same width with matching signedness
template<typename From, typename To>
concept SafeNumericConvert =
    std::integral<From> && std::integral<To> &&
    (sizeof(From) < sizeof(To) ||
     (sizeof(From) == sizeof(To) &&
      std::is_signed_v<From> == std::is_signed_v<To>));

// A wrapper that only compiles for safe conversions
template<typename To, typename From>
    requires SafeNumericConvert<From, To>
constexpr To safe_cast(From val) {
    return static_cast<To>(val);
}

// The version checked at runtime: handles the cases that cannot be decided at compile time
template<typename To, typename From>
    requires (std::integral<From> && std::integral<To> && !SafeNumericConvert<From, To>)
To checked_cast(From val) {
    if constexpr (std::is_signed_v<From> && std::is_unsigned_v<To>) {
        if (val < 0) throw std::overflow_error("negative to unsigned");
    } else if constexpr (std::is_unsigned_v<From> && std::is_signed_v<To>) {
        // unsigned -> signed, same size: 0 is always valid, only the upper bound needs checking
        if (val > static_cast<From>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("narrowing conversion would overflow");
        }
    } else {
        // signed -> signed narrowing, or other cases: compare safely via the common type
        using Common = std::common_type_t<From, To>;
        if (static_cast<Common>(val) < static_cast<Common>(std::numeric_limits<To>::min()) ||
            static_cast<Common>(val) > static_cast<Common>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("narrowing conversion would overflow");
        }
    }
    return static_cast<To>(val);
}

int main() {
    int x = 42;
    auto y = safe_cast<long long>(x);       // OK, int -> long long is safe
    // auto z = safe_cast<char>(x);          // compile error! int -> char may narrow
    // auto u = safe_cast<int>(uint32_t(0)); // compile error! uint32_t -> int32_t, same size but unsigned -> signed

    // For scenarios that need runtime checks, use checked_cast
    auto w = checked_cast<char>(x);         // runtime check: 42 is within char's range, OK
    // auto q = checked_cast<char>(300);     // throws at runtime
}
```

See that? `safe_cast` stops narrowing conversions at compile time—no need to run the code to discover the problem. And `checked_cast` only brings in runtime overhead when safety can't be decided at compile time. That is what "extending C++'s type system" means: with Concepts, you lay a layer of your own type-safety checks on top of C++'s existing type rules.

I used to think templates were black magic—angle brackets gave me a headache, and I simply refused to read screen after screen of error messages. But looking back now, Concepts have pulled templates up from "internal compiler implementation details" to "a design language for your own type system." You are no longer wrestling with the compiler—you are **designing rules**.

And there it finally clicked. Concepts are not a replacement for SFINAE, not syntactic sugar for `enable_if`; they are a complete mechanism for writing functions, making judgments, choosing branches, and building type constraints at compile time. And where this mechanism ultimately points is: letting you grow new type rules on top of C++'s existing type system, according to your own domain's needs. Looking back, it isn't that hard—but if nobody had pointed out the "compile-time functions" insight, I might have gone on struggling in the SFINAE swamp for a long time.

---

# Value Parameters in Concepts: Breaking the Final Mental Block

Once I had "Concepts are compile-time functions" straight, a question I'd never been able to answer suddenly surfaced: since a concept is, at bottom, a constexpr variable template returning `bool`, can it take non-type parameters? The answer is yes—and it reads perfectly naturally.

## Start from the Basics: What a Concept Actually Is

I used to treat concepts as a "special type-constraint syntax," living in a completely different world from ordinary functions. That misconception is genuinely harmful, because it blocks you from understanding a whole set of more advanced uses.

Let's look at a perfectly ordinary concept definition:

```cpp
#include <concepts>
#include <type_traits>

// The concepts I used to write looked like this — type parameters only
template<typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

Looks very "type-exclusive," right? But translate the concept into what it really is, and it's just a constexpr variable template returning `bool`. The way the compiler regards that code above is roughly equivalent to:

```cpp
template<typename T>
constexpr bool Addable_v = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

Since it's constexpr, and since it's a template, why couldn't it take non-type parameters? There's no reason to forbid it. The only reason I couldn't see it was that I'd stared at the `typename T` spelling for so long that I'd boxed myself in.

## Let's Try It: Passing Values into a Concept

Once that relationship is clear, the code writes itself. Let's define a concept that constrains not just a type but also a concrete numeric condition:

```cpp
#include <iostream>
#include <concepts>
#include <array>

// This concept takes a type parameter and a value parameter
// What it says is: T is an integral type, and the value v must be greater than or equal to 0
template<typename T, T v>
concept NonNegativeIntegral = std::integral<T> && (v >= 0);

// Use it to constrain a function
template<typename T, T v>
    requires NonNegativeIntegral<T, v>
constexpr T safe_value() {
    return v;
}

int main() {
    // This one is fine: type int, value 42, satisfies >= 0
    std::cout << safe_value<int, 42>() << "\n";

    // This one is fine too: value 0, the boundary case
    std::cout << safe_value<int, 0>() << "\n";

    // If you uncomment the next line, compilation fails outright
    // because the value -1 does not satisfy the v >= 0 constraint
    // std::cout << safe_value<int, -1>() << "\n";

    return 0;
}
```

Compile and run: it prints `42` and `0`, exactly as expected. You might say, isn't this just the same as constraining a non-type template parameter? Fair—in simple scenarios it behaves much like a `static_assert` or a `requires` clause on a non-type template parameter. But a concept's advantage is that it can be named, combined, and overloaded—and that changes everything.

## A More Interesting Use: Concept Overloading on Value Parameters

Since concepts can carry value parameters, can I use different values to trigger different overloads? Yes—and it comes out very clean:

```cpp
#include <iostream>
#include <string>

// Define two concepts, distinguished by different values
template<int N>
concept IsSmall = (N <= 10);

template<int N>
concept IsLarge = (N > 10);

// This implementation is chosen when N is less than or equal to 10
template<int N>
    requires IsSmall<N>
std::string describe_size() {
    return "small: " + std::to_string(N);
}

// This implementation is chosen when N is greater than 10
template<int N>
    requires IsLarge<N>
std::string describe_size() {
    return "LARGE: " + std::to_string(N);
}

int main() {
    std::cout << describe_size<3>() << "\n";   // outputs: small: 3
    std::cout << describe_size<50>() << "\n";  // outputs: LARGE: 50
    return 0;
}
```

Seeing this, something suddenly made sense to me. Before, for this kind of compile-time numeric dispatch I would most likely have written `if constexpr`. That works too, but it stuffs all the branches into the same function body—once the number of value branches grows, the function gets long and hard to read. With concept overloading, each branch is an independent function with its logic fully isolated. So much cleaner.

## constexpr vs concept: When to Use Which

You can write value logic in a concept, and you can write value logic in a constexpr function—so which one when?

I chewed on this for a long time before landing on a very simple criterion. Ask yourself one question: what is the result of this computation? If the result is a value—say it computes a `7`—then it naturally belongs in a constexpr function. If the result is "a judgment about a type," a yes or a no, then it belongs in a concept.

Here's an intuitive example. Suppose I want to compute an integer's factorial at compile time:

```cpp
// The result is a value, so a constexpr function is exactly right
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial(5) == 120);
```

You wouldn't write factorial as a concept, because factorial's result is not a boolean—it is not a constraint. Conversely, if you want to express "can this type be used for such-and-such numeric computation," that is a concept's job:

```cpp
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template<Numeric T>
T compute(T x) {
    return x * x + 1;
}
```

So in essence, constexpr/consteval solves "compute values at compile time," while concepts solve "judge types at compile time." Both are compile-time evaluation mechanisms, and they share a lot internally—after all, a concept's constraint expression is itself evaluated in a constexpr context—but their responsibilities are cleanly bounded.

Then again, as I demonstrated earlier, once a concept carries value parameters, this boundary blurs a little. Your concept really is doing some numeric computation (like `v >= 0`); it's just that the final result collapses into a boolean. I think this blurring is a good thing—it gives us more expressive power, as long as you know perfectly well what you're doing.

## By the Way: consteval and constinit

Since constexpr came up, I'll slip in a word about the other two keywords C++20 introduced, because they're often discussed together, and I used to mix them up constantly.

`consteval` is called an "immediate function": the function must execute at compile time—it won't even leave you the possibility of a runtime call. A `constexpr` function is "execute at compile time when possible, but if the arguments aren't compile-time constants, runtime execution is allowed." `constinit` guarantees a variable is initialized at compile time without demanding that it stay unmodifiable afterwards (unlike `const`). All three have their uses, but in my actual projects, `constexpr` is still the one I reach for most; `consteval` has come up a few times in extremely performance-sensitive nested-template scenarios.

---

# Interface Inheritance vs Concepts: Not a Question of Replacement

Someone asked a question I'd also wrestled with for a long time: now that C++20 has concepts, can those old interface classes with nothing but pure virtual functions be retired? Can concepts fully cover the functionality of interface inheritance?

Honestly, I had the same thought while learning concepts. Concepts seemed so elegant—compile-time checks, zero runtime overhead, no writing piles of virtual functions and vtables—that it felt like overkill, in the best way. Only after hearing this answer did I realize I was oversimplifying. Bjarne Stroustrup's answer was blunt: no, concepts cannot fully cover interface inheritance. And he himself uses interface inheritance far more often than implementation inheritance. The key distinction here is that interface inheritance is you defining what a class "looks like," while implementation inheritance is you defining "how a class does its work." The former has always been good practice in C++; the latter is the one everybody grumbles about. Bjarne Stroustrup said there are two fundamentally different ways to specify an interface: one is a fixed, strictly defined interface; the other is a flexible, open interface. You need both—they solve different problems.

I had never straightened out that distinction before. Looking back now, a fixed interface is the "you must provide these five methods, signatures must match exactly, not one fewer" kind of situation. The classic case is a plugin system—the host program defines an `IPlugin` interface, and every plugin must implement it precisely. In that scenario, a virtual-function interface class is actually a natural fit, because the interface itself is a "contract" that spells out in black and white what you must provide.

A flexible interface, on the other hand, is more concepts' home turf. You don't need to match some specific method signature exactly; you just need to satisfy certain "constraint conditions." For example, you don't need a method called `draw`—you just need to be "passable to some function that accepts streamed output." This looser, capability-based kind of constraint really does come more naturally with concepts. Put differently: it's looser than an is-a relationship—you only have to "be able to do it."

As for when to use which, from my own practice, my current rough judgment is this: if your interface is meant for "humans"—that is, another developer needs to know explicitly "which methods am I supposed to implement"—then an interface class is clearer, because the IDE will tell you directly which pure virtual function you haven't implemented yet. If your interface is meant for the "compiler"—that is, constraining inside templates so type checking reports errors earlier and error messages read better—then concepts fit better.

---

# What Comes After Concepts? — From "What More Can the Language Add" to "How We Should Use It"


The next stage is not about making the language more perfect—it's about **writing more libraries that genuinely use concepts well**.

The speaker put it very plainly: the papers do list about ten "things that might be doable," but he doesn't believe those are what's needed. What we need is to **accumulate experience in practice**—to see how concepts and the other parts of the language (constraint subsumption, interaction with SFINAE, cooperation with modules) actually behave in real, large codebases. That observation period could take several years.

My understanding at this point is this: a language feature isn't better merely for being more advanced—it should be driven by **problems that genuinely exist in the real world**. If nobody is actually out there writing libraries with concepts and running into real pain points, then no matter how many proposals pile up, they're toys on paper.

## Another Very Practical Question: Should the Standard Library Add More Concept Constraints

Someone in the audience asked a particularly down-to-earth question: `std::vector`'s type parameter today has essentially no constraints—should something like a `std::copyable` concept be added to rein it in?

I had wondered about this myself before. I've written code like this:

```cpp
#include <vector>
#include <string>

int main() {
    // This thing compiles, but you can barely do anything meaningful with it
    std::vector<std::unique_ptr<int>> v;
    // v.push_back(std::make_unique<int>(42));  // compile error
    // but instantiating the vector itself is perfectly legal
}
```

It struck me as odd at the time—`unique_ptr` isn't copyable; put it into a vector and most operations blow up. Why doesn't the standard library stop it right at the declaration?

The speaker's answer made me understand the standard library maintainers' predicament. He said, "you have to be very careful," because **for years, people have been doing things simply because they could**. He gave an example: people using `std::accumulate` to concatenate strings. That thing was originally a reduction for numeric types, but because no constraint was added, it compiles—and so everyone just went on using it that way.

Now, if you suddenly slap a `std::arithmetic` constraint on `std::accumulate`, every piece of code that uses `accumulate` to glue strings together blows up. You have no way to know whose code you'd break. So the choice facing the standards committee is: either provide two overloads (a numeric version and a non-numeric one), or don't touch anything at all. Either way, it's not a decision anyone gets to make casually.

I ran a small experiment to see what this "accumulate string concatenation" is actually about:

```cpp
#include <numeric>
#include <string>
#include <vector>

int main() {
    std::vector<std::string> words = {"hello", " ", "world"};

    // accumulate's default operation is std::plus, which for string is just operator+
    // The initial value "" is a string, so the whole deduction follows along from there
    auto result = std::accumulate(
        words.begin(), words.end(),
        std::string("")
    );

    // result == "hello world" — it really does run
}
```

See? This code runs, and the result is correct. But if C++20's `<numeric>` had directly given `accumulate` a `std::integral` or `std::floating_point` constraint, this code would die on the spot. This kind of "historical baggage" isn't something you can think away just because you'd like to. (There's just no way around it!)

## So What the "Next Stage" of Concepts Actually Is

Put these two questions together, and my current understanding is this:

Concepts, as a language feature, have landed. C++20 gave us the standard concepts in the `<concepts>` header, the `requires` clause, the `requires` expression, constraint subsumption—the toolbox is already sufficient. **The bottleneck isn't the language; it's the ecosystem.**

What does "ecosystem" mean? This:

First, the standard library itself has to use concepts more sensibly. C++20 already did a great deal—the algorithms in `std::ranges` constrain their iterator types, projection types, and so on with concepts almost across the board. But old workhorses like `std::vector` are another story: touch them and everything moves—they demand extreme caution.

Second, those of us writing application code and third-party libraries need to start using concepts in **our own interfaces**. Not toy examples in blog posts, but in real projects: constrain template parameters with concepts, replace those `static_assert`s, kill off the `std::enable_if` hell of SFINAE. Then accumulate experience along the way—which concept granularity is right, where constraints read clearest, how to give users the best possible error messages.

Third, only once that body of practical experience has piled up will we know "what the language is still missing"—rather than sitting in a chair right now, dreaming it up.
