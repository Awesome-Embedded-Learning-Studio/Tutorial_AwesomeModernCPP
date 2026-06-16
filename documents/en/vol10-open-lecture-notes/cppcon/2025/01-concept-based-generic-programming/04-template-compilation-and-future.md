---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 Talk Notes — Templates Should Not Be Compiled in Isolation,
  Concepts as Compile-Time Functions to Build Type Systems, Interface Inheritance
  and Concepts Complement Each Other, Future Ecosystem Development
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
title: Template Compilation Model and Future Outlook
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/04-template-compilation-and-future.md
  source_hash: e836440db3417fdb31349353273529f82ab0158b0fec98be20e4f1eb5de8dffe
  translated_at: '2026-06-16T03:50:05.126034+00:00'
  engine: anthropic
  token_count: 4469
---
# Templates Should Not Be Compiled in Isolation

When we discussed using concepts to constrain templates earlier, a question kept circling in my mind: if I add precise concept constraints to the parameters of `std::advance`, such as requiring it to be a `random_access_iterator`, wouldn't that leave `forward_iterator` out in the cold? Would I have to write a bunch of overloads for different iterator categories, each advancing in a different way? Doesn't this lead us back to the old problem of interface instability—every time I support a new iterator type, I have to go back and modify the declaration of `std::advance`?

Honestly, this problem bothered me for quite a while. I used to think that with concepts, "splitting templates to compile in isolation" should be the ideal state—each template checked independently, passing on its own, and then assembled. But seeing this example of advancing iterators, I had a sudden realization: we can't actually do that, and we shouldn't even try to.

## First, Let's Look at a Seemingly Simple Problem

You might ask, what does "compiling templates in isolation" mean? My understanding is this: when the compiler sees a template definition, it judges whether the template is legal solely based on the concept constraints in the template signature, without looking at what operations the specific type passed in during actual instantiation can provide.

Sounds ideal, right? But problems arise immediately.

Let's look at the `std::advance` function. Its job is to advance an iterator by `n` steps. For different iterator categories, the method of advancement is completely different: a `random_access_iterator` can simply use `+=` to get there in one step; whereas a `forward_iterator` doesn't have `+=` and can only `++` one by one.

I used to think that `forward_iterator` lacking `+=` was some kind of "defect" in the standard, or at least a restriction that should be fixed. But the fact is, there is a perfectly good reason why `forward_iterator` doesn't provide `+=`—it represents an abstraction of "only moving forward one step at a time, not jumping." It's a feature, not a bug.

## Let's Write an Example to Get a Feel for It

I wrote a piece of code to verify this behavior, running on my Arch Linux WSL with GCC 16.1.1 and the `-std=c++23` flag enabled.

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <iterator>

// Concept to check if type supports random access (+= n)
template<typename T>
concept RandomAccess = requires(T it, int n) {
    { it += n } -> std::same_as<T&>;
};

// Generic advance using if constexpr
template<typename Iter>
void my_advance(Iter& it, typename std::iterator_traits<Iter>::difference_type n) {
    if constexpr (RandomAccess<Iter>) {
        it += n; // O(1) for random access
    } else {
        for (auto i = 0; i < n; ++i) {
            ++it; // O(n) for others
        }
    }
}

int main() {
    std::vector<int> v{1, 2, 3, 4, 5};
    std::list<int> l{1, 2, 3, 4, 5};

    auto vec_it = v.begin();
    auto list_it = l.begin();

    my_advance(vec_it, 3);
    my_advance(list_it, 3);

    std::cout << *vec_it << std::endl; // Output: 4
    std::cout << *list_it << std::endl; // Output: 4
}
```

Run it, and the output is exactly as expected: two 4s. See, the same `my_advance` function uses `+=` for `vector<int>::iterator` and a loop with `++` for `list<int>::iterator`. The reason this works is precisely because the template doesn't check in isolation "do you support `+=`" before instantiation—it waits until it sees the specific type, then makes the choice via `if constexpr`.

## What If We Really Did Compile in Isolation?

Now let's assume that C++ eventually implements isolated compilation for templates, without any exemption clauses. What would happen?

When the compiler sees the definition of `my_advance`, it would check every line of code to see if it is legal for all types described by the constraints. If my constraint is `std::forward_iterator` (which doesn't include `+=`), but the definition of `my_advance` contains `it += n`, the compiler would reject that line immediately—even if it would never be reached at runtime (because it's blocked by the `if constexpr` branch).

Then I would be forced to split the code into two overloads:

```cpp
template<typename Iter>
requires RandomAccess<Iter>
void my_advance(Iter& it, std::iter_difference_t<Iter> n) {
    it += n;
}

template<typename Iter>
requires (!RandomAccess<Iter>)
void my_advance(Iter& it, std::iter_difference_t<Iter> n) {
    for (auto i = 0; i < n; ++i) ++it;
}
```

Doesn't look too bad, right? But think about it carefully: this pushes "an algorithm making different choices based on type capabilities" from inside the algorithm to the interface level. For every new iterator category that needs special handling, I have to add another overload. The interface bloats, maintenance costs rise, and essentially, I'm repeating the same logic.

More critically, there's the performance issue. If `my_advance` cannot use `+=` for a `random_access_iterator` and is forced to use a `++` loop, the complexity jumps from O(1) to O(n). When this is called inside an algorithm, if the outer loop is also O(n), the overall complexity explodes from O(n) to O(n²). For large datasets, this is fatal.

A large part of the reason why the STL is efficient is this ability to "branch based on type capabilities inside the template." If isolated compilation blocks this path, the STL's performance advantage would be greatly diminished.

## So, What Are Concepts Actually Good For?

At this point, you might ask: aren't concepts useless then? You said they catch errors earlier, but now you say we can't check in isolation. Isn't that contradictory?

I was confused at first too, but after thinking it through, I realized it's not a contradiction. The value of concepts lies here: when there truly is no type in the system that satisfies the constraint, the error is caught much earlier, and the error message is much clearer—it tells you "the type you passed doesn't satisfy `RandomAccess`," instead of spitting out a screen full of unintelligible template instantiation backtraces.

But "catching errors earlier" and "compiling templates in isolation" are two different things. Templates still need to see the specific type to make the final legality judgment; concepts just make the failure messages of that judgment readable. In this sense, templates have always been type-safe—it's just that in the past, when errors occurred, you couldn't understand them, but now you can.

## Pragmatism Over Dogma

So, what's the conclusion? At least for now, we shouldn't pursue isolated compilation of templates. If C++ does implement this feature in the future, there must be some exemption mechanism that allows patterns like `std::advance`—"branching internally based on type capabilities"—to exist legally. Because honestly, this pattern is everywhere in our software infrastructure.

This reminds me of when I was learning templates; I always thought "stricter constraints are better" and wanted to lock down every template parameter. But after writing more code, I realized the essence of generic programming is: you describe a minimal requirement, then flexibly adapt to types with different capabilities inside the implementation. It's not laziness; it's pragmatism.

## Returning to the Essence of Generic Programming

After all this, I looked back and re-understood what generic programming really is. It's not some mysticism; it's just programming itself—done in the most generic, efficient, and comfortable way possible. The "concept" here doesn't refer to the C++20 language feature, but to your general abstraction of an idea: what is an iterator, what is a callable, what is a range.

These things weren't invented by C++. If you flip through Alexander Stepanov and Daniel E. Rose's *From Mathematics to Generic Programming*, it's full of pure math—algebraic structures, axioms, theorems. If you don't like math, that book is indeed painful to read (I admit I put it down after a few pages). But the core idea is actually very simple: find the common algebraic structure between different types, then write algorithms for that structure, not for a specific type.

Moreover, generic programming has built-in uniform usage of types from the start—how scopes are managed, how names are resolved, how objects are created and destroyed—these are just as important in generic code as in any other code. It existed before C++, and C++ just used the template mechanism to express this set of ideas.

At this point, I finally figured out "why templates shouldn't be compiled in isolation." Looking back, it's actually not complex—just don't sacrifice flexibility and performance in real engineering for the sake of theoretical purity. After all, we write code to solve problems, not to write papers proving purity.

---

# Concepts: Building Your Own Type System at Compile Time

Honestly, seeing this conclusion was a lightbulb moment for me. When I learned Concepts before, I always treated it as "more elegant SFINAE," thinking it was just syntactic sugar to constrain template parameters, looking better than `std::enable_if_t`. But after a night of tinkering, I finally understood: the essence of Concepts is actually **compile-time functions**—it takes types and values as arguments and returns a bool, telling you whether a type satisfies a condition. After this cognitive shift, a lot of things suddenly clicked.

## First, Get This Straight: What Are Concepts Actually Doing?

I used to have a misunderstanding that Concepts were describing "what a type looks like," like "it must have `begin()` and `end()`." But that's not actually it. Concepts describe "a generic function's requirements for its parameters," and it **does not care how those requirements are met**. This distinction is crucial, and I completely missed it at first.

What do I mean? For example, if you write a Concept requiring "can be added," you don't need to say "implemented via `operator+`" or "implemented via a member function," you just say "can be added." The compiler judges for itself. This is completely different from the classic OOP mindset of "must inherit from a certain base class, must override a certain virtual function"—OOP top-down prescribes "how you provide it," while Concepts bottom-up say "what I need."

Plus, Concepts can accept multiple arguments, not just a type argument. This means you can express cross-type constraints like "type A and type B can perform some operation together," which is almost impossible to express elegantly in traditional OOP.

## Let's Write a Few Concepts to Get a Feel

My test environment is Arch Linux WSL, GCC 16.1.1, compile command with `-std=c++20 -fconcepts`. The code below is what I wrote to verify the understanding that "Concepts are compile-time functions":

```cpp
#include <iostream>
#include <concepts>

// 1. Concept taking one type parameter
template<typename T>
concept AlwaysTrue = true;

// 2. Concept taking two type parameters
template<typename T, typename U>
concept SameSize = sizeof(T) == sizeof(U);

// 3. Concept taking type AND value
template<typename T, auto N>
concept SizeEquals = sizeof(T) == N;

int main() {
    std::cout << std::boolalpha;

    std::cout << "int is AlwaysTrue: " << AlwaysTrue<int> << "\n"; // true
    std::cout << "int and long SameSize: " << SameSize<int, long> << "\n"; // true (on most 64-bit platforms)
    std::cout << "char SizeEquals<1>: " << SizeEquals<char, 1> << "\n"; // true
    std::cout << "int SizeEquals<4>: " << SizeEquals<int, 4> << "\n"; // true (on most 32-bit int systems)
}
```

Run it, and you'll see `AlwaysTrue` takes a type parameter, `SameSize` takes two type parameters, and `SizeEquals` is even more interesting, taking both a type and a compile-time value. These three parameter forms can be combined freely, offering very strong expressive power.

However, there's a pitfall I stumbled on for a while: multi-parameter concepts can't be written directly in front of a template like single-parameter ones to act as a constraint (e.g., `template<SameSize T>` will error directly because `SameSize` needs two template parameters and you only gave one). Multi-parameter concepts must use an explicit `requires` clause to pass the parameters in. Single-parameter concepts don't have this restriction; `template<Sortable T>` is very natural.

## Using Concepts for Overloading: Simpler Than Normal Overloading

This was the part that confused me the most. I used to think template overloading was a nightmare—you had to use SFINAE for partial specialization, error messages were three screens long, and the rules were complex enough to make you doubt life. But using Concepts for overloading, the rules are actually **simpler** than normal function overloading.

I wrote an example to verify these three scenarios:

```cpp
#include <iostream>
#include <concepts>
#include <string>

// Concept 1: Integral
template<typename T>
concept IsIntegral = std::integral<T>;

// Concept 2: Signed Integral (Subset of Concept 1)
template<typename T>
concept IsSignedIntegral = std::integral<T> && std::is_signed_v<T>;

// Overload 1: Matches only Integral
void process(IsIntegral auto x) {
    std::cout << "Integral: " << x << "\n";
}

// Overload 2: Matches only Signed Integral
void process(IsSignedIntegral auto x) {
    std::cout << "Signed Integral: " << x << "\n";
}

int main() {
    process(10);    // Matches IsIntegral (int is integral)
                    // Matches IsSignedIntegral (int is signed)
                    // -> Choose IsSignedIntegral (more constrained)

    process(10u);   // Matches IsIntegral (unsigned is integral)
                    // Does not match IsSignedIntegral
                    // -> Choose IsIntegral

    // process(10.5); // Error: matches neither
}
```

See, the rules are just three, crystal clear: if only one matches, use it; if two match and one is a subset of the other, choose the stricter one; everything else is an error. There are no complex rules of implicit conversion ranking or ambiguity resolution found in normal overloading. I was stuck here for a long time, thinking Concepts overloading had some hidden trap, but looking back at the principle, it's really simple.

## The Part That Really Lit Me Up: Extending C++'s Type System

This is where it gets really interesting for me. I used to think C++'s type system was fixed—int is int, double is double, narrowing conversion is unsafe, you just have to work around it or use a `-Wconversion` warning. But Concepts allow you to **build your own type system** at compile time, blocking things that originally needed runtime checks right at the compilation stage.

Following this line of thought, I wrote a Concept for `safe_narrow_cast` to distinguish between "safe numeric conversion" and "narrowing conversion that might lose data" at compile time:

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>
#include <limits>

template<typename To, typename From>
concept SafeNarrowable =
    std::integral<To> && std::integral<From> &&
    (sizeof(To) >= sizeof(From)) || // Size check
    (std::numeric_limits<To>::max() >= std::numeric_limits<From>::max()); // Range check

template<typename To, typename From>
requires SafeNarrowable<To, From>
constexpr To safe_narrow_cast(From from) {
    return static_cast<To>(from);
}

// A version that forces runtime check if not statically safe
template<typename To, typename From>
To narrow_cast(From from) {
    if constexpr (SafeNarrowable<To, From>) {
        return static_cast<To>(from);
    } else {
        if (from > std::numeric_limits<To>::max() ||
            from < std::numeric_limits<To>::min()) {
            throw std::runtime_error("Unsafe narrowing");
        }
        return static_cast<To>(from);
    }
}

int main() {
    // safe_narrow_cast<long, int>(100); // OK
    // safe_narrow_cast<int, long>(100); // Compile error: not SafeNarrowable

    std::cout << narrow_cast<int>(100LL) << "\n"; // OK, compile-time safe
    // std::cout << narrow_cast<int>(999999999999LL) << "\n"; // Runtime throw
}
```

See? `safe_narrow_cast` blocks narrowing conversions at compile time; no need to wait until runtime to find the problem. And `narrow_cast` only introduces runtime overhead when compile-time safety can't be determined. This is what "extending the C++ type system" means—you add a layer of your own type safety checks on top of C++'s existing type rules using Concepts.

I used to think templates were black magic; I got a headache seeing angle brackets, and I didn't even want to look at screen after screen of error messages. But looking back now, Concepts have pulled templates from "compiler internal implementation details" up to the level of "your own type system design language." You are no longer fighting the compiler; you are **designing rules**.

I finally got this. Concepts isn't a replacement for SFINAE, nor is it syntactic sugar for `std::enable_if`. It's a whole mechanism for writing functions, making judgments, choosing branches, and building type constraints at compile time. And this mechanism ultimately points to this goal: letting you grow new type rules on top of C++'s existing type system, according to your own domain needs. Looking back, it's not that hard, but if no one had punctured the "compile-time function" window paper for me, I might have been struggling in the SFINAE mud for a long time.

---

# Value Parameters in Concepts: Breaking the Final Mindset Block

After figuring out "Concepts are compile-time functions," I suddenly thought of a problem I hadn't been able to figure out: since a concept is essentially a `constexpr` variable template returning `bool`, can it accept non-type parameters? The answer is yes, and it writes very naturally.

## Start from the Basics: What is a Concept Anyway?

I used to treat concepts as a "special type constraint syntax," thinking they were in a completely different world from normal functions. This misconception is actually quite harmful because it prevents you from understanding many more advanced usages.

Let's look at a very normal concept definition:

```cpp
template<typename T>
concept Sortable = requires(T t) { t.sort(); };
```

This looks very "type-specific," right? But if you translate a concept into its true form, it is actually just a `constexpr` variable template returning `bool`. The way the compiler sees the code above is roughly equivalent to this:

```cpp
template<typename T>
constexpr bool Sortable = requires(T t) { t.sort(); };
```

Since it is `constexpr`, since it is a template, why can't it accept non-type parameters? There is no reason to prohibit this. I couldn't figure it out before purely because I'd seen too much `template<typename T>` usage and formed a mindset block.

## Let's Try It: Passing Values in Concepts

Once I understood the relationship above, writing code followed naturally. Let's define a concept that constrains not just the type, but also a specific numerical condition:

```cpp
#include <iostream>
#include <concepts>

// Concept constraining a value
template<typename T, auto Threshold>
concept AboveThreshold = (T{} > Threshold); // Requires T to be default constructible and comparable

int main() {
    static_assert(AboveThreshold<int, 0>); // int{} is 0, 0 > 0 is false... wait.
    // Let's adjust logic for clarity
}
```

Wait, let me adjust the logic to be clearer:

```cpp
#include <iostream>
#include <concepts>

template<typename T, auto N>
concept SizeIs = sizeof(T) == N;

int main() {
    std::cout << std::boolalpha;
    std::cout << "int is 4 bytes: " << SizeIs<int, 4> << "\n";
    std::cout << "double is 8 bytes: " << SizeIs<double, 8> << "\n";
    std::cout << "int is 8 bytes: " << SizeIs<int, 8> << "\n";
}
```

Compile and run, output `true` and `false`, exactly as expected. You might say, this looks no different from non-type template parameter constraints? Indeed, in simple scenarios, it's similar to `requires (sizeof(T) == N)`. But the advantage of a concept is that it can be named, combined, and overloaded, which is completely different.

## Even More Interesting: Using Value Parameters for Concept Overloading

Since concepts can carry value parameters, can I trigger different overloads with different values? The answer is yes, and it writes very clearly:

```cpp
#include <iostream>
#include <concepts>

template<int Priority>
concept HighPriority = Priority >= 50;

template<int Priority>
requires HighPriority<Priority>
void execute() {
    std::cout << "Executing high priority task: " << Priority << "\n";
}

template<int Priority>
requires (!HighPriority<Priority>)
void execute() {
    std::cout << "Executing normal task: " << Priority << "\n";
}

int main() {
    execute<10>(); // Normal task
    execute<80>(); // High priority task
}
```

Seeing this, I suddenly realized something. Before, if I wanted to do this kind of dispatch based on compile-time values, I would probably write `if constexpr`. That works too, but that style stuffs all branches into the same function body. If there are many value branches, the function becomes long and hard to read. With concept overloading, each branch is an independent function, logic is completely isolated, much cleaner.

## constexpr vs concept: When to Use Which?

You can write value logic in concepts, and you can write value logic in `constexpr` functions, so when should you use which?

I thought about this for a long time, and later figured out a very simple judgment standard. Ask yourself a question: what is the result of this calculation? If the result is a value, like calculating a `size_t`, then it naturally should be a `constexpr` function. If the result is a "judgment on a type," yes or no, then it is suitable to be written as a concept.

Here's an intuitive example. Suppose I want to calculate the factorial of an integer at compile time:

```cpp
consteval int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}
```

You wouldn't write factorial as a concept, because the result of factorial isn't a boolean value, it's not a constraint. Conversely, if you want to express "this type can be used for a certain kind of numeric calculation," that's the job for a concept:

```cpp
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;
```

So essentially, `constexpr`/`consteval` solves the "calculate value at compile time" problem, while concepts solve the "judge type at compile time" problem. They are both compile-time evaluation mechanisms and share many similarities in implementation—after all, the constraint expression of a concept is evaluated in a `constexpr` context—but their responsibilities are clearly bounded.

Having said that, as I demonstrated earlier, when concepts carry value parameters, this boundary gets a little blurry. Because you are indeed doing some numeric calculation inside your concept (like `sizeof(T) == N`), it's just that the final result is reduced to a boolean value. I think this blurring is a good thing; it gives us more expressiveness, as long as you know what you are doing.

## By the Way, a Word on consteval and constinit

Since I mentioned `constexpr`, I'll briefly mention the other two keywords introduced in C++20, because they are often discussed together, and I used to confuse them too.

`consteval` is called an "immediate function," meaning this function must be executed at compile time, giving no chance for a runtime call. `constexpr` functions are "execute at compile time if possible, but if arguments aren't compile-time constants, runtime execution is allowed." `constinit` guarantees the variable is initialized at compile time but doesn't require it to be immutable afterwards (unlike `const`). These three each have their uses, but in my current projects, I still use `constexpr` the most; I've used `consteval` a few times in extremely performance-sensitive nested template scenarios.

---

# Interface Inheritance vs Concepts: Not a Question of One Replacing the Other

Someone asked a question I also struggled with before: since C++20 has concepts, can those old interface classes with only pure virtual functions be eliminated? Can concepts completely cover the functionality of interface inheritance?

Honestly, when I was learning concepts, I had this thought too. I thought, concepts are so elegant, compile-time checks, zero runtime overhead, no need to write a bunch of virtual functions and vtables, simply a dimensionality reduction attack. But after hearing this answer, I realized I was thinking too simply. Bjarne Stroustrup's answer was direct: no, concepts cannot completely cover interface inheritance. And he himself uses interface inheritance far more often than implementation inheritance. The key distinction here is that interface inheritance defines "what a class looks like," while implementation inheritance defines "how a class does its work." The former has always been a good practice in C++, while the latter is what everyone complains about. Bjarne Stroustrup says there are two fundamentally different ways to specify an interface: one is a fixed, strictly defined interface, and the other is a flexible, open interface. You need both; they solve different problems.

I hadn't thought this distinction through clearly before. Looking back now, a fixed interface is the "you must provide these five methods, signatures must match exactly, not one less" situation. A typical example is a plugin system—the main program defines a `Plugin` interface, and all plugins must implement it precisely. In this scenario, a virtual function interface class is actually a natural fit, because the interface itself is a "contract," clearly stating in black and white what you need.

Flexible interfaces are more where concepts shine. You don't need to match a specific method signature exactly; you just need to satisfy certain "constraints." For example, you don't need a method named `write`; you just need to be "passable to a function accepting stream output." This loose, capability-based constraint is indeed more naturally expressed with concepts. In other words, it's looser than an is-a relationship; you just need to "be able to do it."

As for when to use which, based on my own practice, my current rough judgment is this: if your interface is for "humans"—that is, another developer needs to know clearly "what methods do I need to implement"—then using an interface class is clearer, because the IDE will directly prompt you which pure virtual functions are missing. If your interface is for the "compiler"—that is, constraining in templates to make type checking error earlier and error messages more readable—then concepts are more suitable.

---

# What Comes After Concepts? — From "What Can We Add to the Language" to "How Should We Use It"

The next stage isn't about making the language more perfect, but **writing more libraries that truly use concepts well**.

The speaker was very practical—papers do list about ten "things that might be possible," but he believes what's really needed is not those. We need to **accumulate experience in practice**, see how concepts and other parts of the language (like constraint ordering, interaction with SFINAE, cooperation with modules) actually perform in real large codebases. This observation period might take several years.

My current understanding is: language features aren't better just because they are more advanced; they should be driven by **problems that actually exist in the real world**. If no one is actually using concepts to write libraries and encountering real pain points, then more proposals are just toys on paper.

## Another Very Practical Question: Should the Standard Library Add More Concept Constraints?

Someone at the venue asked a very grounded question: `std::vector`'s type parameters currently have basically no constraints, so should we add a concept like `Copyable` to restrict it?

I've thought about this problem myself before. I wrote code like this:

```cpp
std::vector<std::unique_ptr<int>> v;
v.push_back(std::make_unique<int>(1)); // OK
// v.push_back(v[0]); // Error: unique_ptr is not copyable
```

I thought it was weird at the time—`unique_ptr` isn't copyable, if you put it in a vector, most operations will blow up, why doesn't the standard library block it at declaration?

The speaker's answer made me understand the standard library maintainers' dilemma. He said "you must be very careful," because **people have been doing things for years, just because they could**. He gave an example: someone uses `std::accumulate` to concatenate strings. This thing was originally meant for reduction on numeric types, but because you didn't add constraints, it compiles, so people just used it.

Now if you suddenly add a `Numeric` constraint to `std::accumulate`, all the code using `std::accumulate` to splice strings blows up. You don't know whose code you'll break. So the standards committee faces a choice: either provide two overloads (numeric and non-numeric) or do nothing. Neither is a decision to be made lightly.

I ran a small experiment to verify what this "accumulate splicing strings" is all about:

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <numeric>

int main() {
    std::vector<std::string> words = {"Hello", " ", "World", "!"};
    // string + string works, so accumulate works
    std::string result = std::accumulate(words.begin(), words.end(), std::string{});
    std::cout << result << std::endl; // Output: Hello World!
}
```

See, this code runs, and the result is correct. But if C++20's `std::accumulate` directly added a `std::integral` or `std::arithmetic` constraint, this code would die on the spot. This kind of "historical baggage" isn't something you can just clear away. (There's no way around it!)

## So What IS the "Next Stage" for Concepts

Putting these two questions together, my current understanding is this:

Concepts, as a language feature, has landed. C++20 gave us the standard concepts in the `<concepts>` header, the `requires` clause, the `requires` expression, constraint ordering—the toolbox is sufficient. **The bottleneck isn't the language, it's the ecosystem.**

What is an "ecosystem"? It means:

First, the standard library itself needs to use concepts more reasonably. C++20 has already done a lot—algorithms in `<algorithm>` almost all use concepts to constrain iterator types, projection types, etc. But for old fossils like `std::accumulate`, changing them touches everything and requires extreme caution.

Second, we who write application code and third-party libraries need to start using concepts in **our own interfaces**. Not writing toy examples in blogs, but in real projects, constraining template parameters with concepts, replacing `std::enable_if_t`, killing the SFINAE `decltype` hell. Then accumulate experience in this process—what concept granularity is appropriate, where to write constraints for clarity, how to give users the best error messages.

Third, only after enough practical experience is accumulated will we know "what is still missing from the language." Not sitting in a chair imagining it now.
