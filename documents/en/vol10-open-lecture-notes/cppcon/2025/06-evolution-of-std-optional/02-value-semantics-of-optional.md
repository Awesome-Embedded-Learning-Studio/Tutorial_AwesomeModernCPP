---
title: "The Value-Semantics Foundation of std::optional"
description: "CppCon 2025 notes — before biting into optional<T&>, get the value version thoroughly straight: ownership, value semantics, T plus one extra state, C++26 range support, and the overload-set nightmare hiding behind default parameters"
chapter: 6
order: 2
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 11
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "Why the Optional Reference Took Twenty Years of Wrangling"
related:
  - 'optional: Making "Maybe Nothing" a Type'
  - "Why the Optional Reference Took Twenty Years of Wrangling"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/02-value-semantics-of-optional.md
  source_hash: 2e34f02c243194f5a8a769dd0963a76680d2068589a24d1cfa67af4facfc96a3
  translated_at: '2026-09-26T16:21:07+00:00'
  engine: anthropic
  token_count: 4300
---

# The Value-Semantics Foundation of std::optional

[Last time](./01-why-optional-reference-took-20-years.md) we worked through why `optional<T&>` is so hard and why it ultimately landed on a pointer. But before we bite into that particular bone, I think it's worth getting plain `optional<T>` thoroughly straight first. Because the moment `T` becomes a reference, you'll find that almost every premise of `optional<T>` gets overturned. You have to know what the "normal case" looks like before you can feel exactly how strange the "reference case" gets.

## What Kind of Type Is This, Really

`optional<T>` is, first and foremost, an owning type: it genuinely stores a `T` object inside itself, and it has value semantics. That means you can copy it, you can move it — whatever the underlying `T` permits, it permits. There is no proxy behavior to speak of; it is an honest value type that either holds something or holds nothing.

Its nullable behavior is a little like a pointer's — both express "there might be no value here." A pointer pulls this off with the null pointer: address zero is not a valid address under normal circumstances, which amounts to the pointer type gaining a free out-of-band value that means "nothing here." `optional<T>` does the same job; algebraically, it is `T` plus one extra state. You can picture it as `variant<T, monostate>`, with `monostate` playing the role of that "nothing here" state. Real implementations don't literally use `variant` for this (that would be far too heavy), but the equivalence is worth keeping in mind, because a great deal of the later discussion around optional parameter deduction and the design of `expected` was, at its core, paving the road for the algebraic data types we actually wanted to use.

## Hands-On: Where the "Value" in Value Semantics Shows

Talk is cheap — let's run the thing. Write a `Tracer` that prints on every construction, copy, and destruction, drop it into an optional, and watch when the object actually gets constructed, when it gets destroyed, and whether copies really are independent.

```cpp
// tracer.cpp
#include <iostream>
#include <optional>

struct Tracer {
    Tracer()                   { std::cout << "Tracer()\n"; }
    ~Tracer()                  { std::cout << "~Tracer()\n"; }
    Tracer(const Tracer&)      { std::cout << "copy ctor\n"; }
    Tracer(Tracer&&) noexcept  { std::cout << "move ctor\n"; }
};

int main() {
    std::optional<Tracer> a;
    std::cout << "a has_value=" << a.has_value() << "\n";

    a.emplace();                                     // the object is actually constructed here
    std::optional<Tracer> b = a;                     // copy construction
    std::cout << "b has_value=" << b.has_value() << "\n";

    a.reset();                                       // destroys the object inside a
    std::cout << "after reset a has_value=" << a.has_value()
              << " b has_value=" << b.has_value() << "\n";
}
```

`-std=c++17` is all you need, and here is what comes out:

```bash
$ g++ -std=c++17 tracer.cpp -o tracer && ./tracer
a has_value=0
Tracer()
copy ctor
b has_value=1
~Tracer()
after reset a has_value=0 b has_value=1
~Tracer()
```

Read that output carefully. When we declare `optional<Tracer> a`, `Tracer()` is not called and `has_value` is 0. Not until `emplace()` does the object actually get constructed. Copying `b = a` goes through `Tracer`'s copy constructor. `a.reset()` destroys the object inside `a`, yet `b` is entirely unaffected — it holds its own copy. This is what ownership behavior under value semantics looks like, and it is beautifully clean.

## A Use Case That Hit Home for Me: Reading Configuration

Steve Downey brings up the scenario of reading configuration files, and that one hit me right in the scars. Back when I used to write config-reading code, some config key might not exist, so you would either call `map::find` and compare the iterator against `end`, or return a pointer (nullptr standing in for "doesn't exist"), or cobble together a `bool` plus an output parameter. The problem with all of these styles is that the information "this value might not exist" is far too easy to lose. Five layers of function calls later, you may well have forgotten to check whether that pointer was null.

Switch to optional, and the type system itself is breathing down your neck: this value might be absent, and you must deal with that. In one piece of business code I wrote, config keys were nested three or four levels deep; with the pointer approach I kept scrolling back to ask "wait, did I actually check that?", while after the switch to optional the compiler simply forced a decision before any use. The sense of security is on a different level entirely. Vol3 has a dedicated [optional deep dive](../../../../vol3-standard-library/error-utils/61-optional.md) that covers this in far more detail — well worth reading side by side.

## C++26: optional as a Range

C++26 gives optional `begin` and `end`, which lets it be used as a range. The proposal is P3168, and the feature-test macro `__cpp_lib_optional_range_support` reads `202406L` on GCC 16.1.1. When I first saw this proposal, my gut reaction was "just to iterate over a single value? Seriously?". Then I took a hard look at my own code and came around.

Think about this pattern:

```cpp
std::optional<User> maybe_user = find_user(id);
if (maybe_user) {
    // The next few dozen lines all operate on maybe_user.value()
    // possibly with more nested checks along the way
    // you have to keep remembering "I'm inside the if, it's safe"
}
```

When the if body is long and you run into `maybe_user.value()` on some line in the middle, you have to scroll back up to confirm that an if really is guarding it. C++26 lets you write this instead:

```cpp
for (auto& user : maybe_user) {
    // In here, user is a User&, not an optional
    // use user freely for dozens of lines; it is a definite value
}
```

The mechanism is dead simple. When the optional is engaged, `begin()` returns a pointer to the internal object and `end()` returns `begin() + 1`, so the loop runs exactly once; when it is disengaged, `begin()` equals `end()` and the loop runs zero times. It is not a container (containers carry a whole pile of requirements) — it is merely a range that happens to provide `begin` and `end`. Let's run it to verify:

```cpp
// opt_as_range.cpp
#include <iostream>
#include <optional>

int main() {
    std::optional<int> engaged = 42;
    std::optional<int> empty;

    int count = 0, sum = 0;
    for (auto&& x : engaged) { count++; sum += x; }
    for (auto&& x : empty)    { count++; sum += x; }

    std::cout << "count=" << count << " sum=" << sum << "\n";
}
```

```bash
$ g++ -std=c++26 opt_as_range.cpp -o opt_as_range && ./opt_as_range
count=1 sum=42
```

The engaged optional iterates once and yields 42; the empty one iterates zero times. Not an earth-shattering feature by any means, but in those long stretches of business logic it lets you forget the optional exists inside the loop body and deal only with the bare type — and that reduction in mental load is completely real.

:::warning
When iterating an optional, write the loop variable as `auto&&`, not `auto`. `auto` throws away referenceness — in the next piece you will meet the `optional<T&>` specialization, and there `auto x` grabs a copy instead of a reference, which quietly changes the semantics. `auto&&` is a universal reference and correctly preserves the original value category. Funnily enough, this exact point was a bug on Steve Downey's own slides; someone called it out during Q&A before he conceded it.
:::

## Default optional Parameters: Lovely to Use, a Nightmare to Implement

There is another extremely common use of optional — a function's default parameter:

```cpp
void process(std::optional<int> timeout = std::nullopt);

process();                        // timeout is nullopt
process(42);                      // timeout is optional<int>(42)
process(std::optional<int>{});    // you can also pass one explicitly
```

It is utterly natural to use: the `int` quietly promotes itself into an `optional<int>`. What you may never have considered is how complicated optional's constructor design had to become to support this kind of implicit conversion. It has to simultaneously handle construction from `T`, from `nullopt`, and from some other `optional<U>`, plus copies and moves — and all those constructors and conversion operators combine into one enormous overload set.

I used to think "overload resolution just picks the best match, no?". In practice, once the overload set grows past a certain size, the outcome is routinely surprising. The human brain reasons about overload resolution as a decision tree — "this type goes down this branch, that type goes down that branch." The compiler does no such thing: it spreads every candidate function out in one flat set, scores them against rules like implicit-conversion ranks and template specialization ordering, and picks the winner. Once the candidates pile up, the scores can stop matching your intuition. That whole mountain of SFINAE and concepts constraints inside optional's implementation is, at bottom, an attempt to tame this giant overload set — to guarantee that "an int takes the int path, a nullopt takes the nullopt path, and no unexpected ambiguity ever fires." Behind every single line of SFINAE lies a blood-and-tears history of overload-resolution traps.

## What Comes Next

The value version's foundation is now mapped out: it owns its data, it has value semantics, it is `T` plus one extra state, C++26 lets it act as a range, and its constructors lug around a heavy overload set for the sake of implicit conversions. But the moment `T` becomes a reference, every one of those premises collapses. `optional<T&>` owns nothing at all, assignment is no longer a value copy, and the constructor chain has to be designed from scratch. [Next time](./03-optional-reference-and-assignment.md) we properly enter the heart of `optional<T&>`: what exactly it is, why its assignment is necessarily a rebind, and the pits that `make_optional` and CTAD dig for references.
