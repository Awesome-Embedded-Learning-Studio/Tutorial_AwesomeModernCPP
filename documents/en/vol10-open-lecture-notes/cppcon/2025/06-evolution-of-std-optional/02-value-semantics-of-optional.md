---
title: "The Value-Semantics Foundation of std::optional"
description: "CppCon 2025 notes — before tackling optional&lt;T&>, get the value version straight: ownership, value semantics, T plus one state, used as a range in C++26, and the overload-set nightmare behind defaulted parameters"
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
  - "Why the optional reference took twenty years"
related:
  - 'optional: Making "Possibly None" a Type'
  - "Why the optional reference took twenty years"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/02-value-semantics-of-optional.md
  source_hash: 2e34f02c243194f5a8a769dd0963a76680d2068589a24d1cfa67af4facfc96a3
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2601
---

# The Value-Semantics Foundation of std::optional

[In the previous piece](./01-why-optional-reference-took-20-years.md) we covered why `optional<T&>` is hard and why it finally landed as a pointer. But before chewing on that bone, I think it's worth getting the plain `optional<T>` straight first. Because the moment you swap `T` for a reference, you'll find nearly every premise of `optional<T>` gets overturned. You need to know what the "normal case" looks like before you can feel where the "reference case" gets weird.

## What kind of type is it, really

First of all, `optional<T>` is an owning type — it actually stores a `T` object inside — and it is value-semantic. That means you can copy it, move it, and everything the underlying `T` allows, it allows. There's no proxy behavior; it's just an honest value type that either holds something or holds nothing.

It's a little like the nullable behavior of a pointer; both express "there might not be a value." A pointer does this with the null pointer — address zero is not a valid address under normal conditions, so the pointer type gets an extra out-of-band value for free that means "nothing here." `optional<T>` does the same thing; algebraically it's `T` plus an extra state. You can think of it as `variant<T, monostate>`, where `monostate` plays the "nothing here" state. A real implementation won't literally use `variant` (that'd be too heavy), but the equivalence is worth remembering, because a lot of the later discussion about optional parameter deduction and the design of `expected` is essentially paving the way for the algebraic data types we actually want to use.

## Tested: what value semantics is actually "worth"

Talk is cheap, let's run it. Write a `Tracer` that prints on every construction, copy, and destruction, stuff it into an optional, and watch when it really constructs, when it destructs, and whether copies are independent of each other.

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

    a.emplace();                                     // the real construction happens here
    std::optional<Tracer> b = a;                     // copy construction
    std::cout << "b has_value=" << b.has_value() << "\n";

    a.reset();                                       // destructs the object inside a
    std::cout << "after reset a has_value=" << a.has_value()
              << " b has_value=" << b.has_value() << "\n";
}
```

`-std=c++17` is enough, and here's the run:

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

Read this output. When you declare `optional<Tracer> a`, `Tracer()` is not called and `has_value` is 0. The object is actually constructed only at `emplace()`. Copying `b = a` goes through `Tracer`'s copy constructor. `a.reset()` destructs the object inside `a`, but `b` is completely unaffected — it has its own copy. That's value-semantic ownership behavior, very clean.

## A use case I really felt: reading config

Steve Downey mentions the scenario of reading a config file, and I really felt it. Back when I used to write config reading, some config key might not exist, so you'd either call `map::find` and check against end, or return a pointer (nullptr meaning "doesn't exist"), or use some `bool` plus an output parameter combination. The problem with these styles is that the information "this value might not exist" is too easy to lose. Five function calls later you might have forgotten to check whether that pointer was null.

Switch to optional and the type system itself is watching you: this value might not be there, you have to deal with it. I once wrote some business code where config keys were nested three or four layers deep; with the pointer style I kept having to scroll back to check "did I actually check that?", and after switching to optional the compiler straight-up forced me to make the judgment before use. That sense of safety is a different thing entirely. There's a dedicated [optional deep dive in vol3](../../../../../vol3-standard-library/error-utils/61-optional.md) that goes into more detail; you can read them side by side.

## C++26: optional as a range

C++26 adds `begin` and `end` to optional so it can be used as a range. The proposal is P3168, the feature-test macro `__cpp_lib_optional_range_support` is `202406L` on GCC 16.1.1. When I first saw this proposal my reaction was "just to iterate over a single value? is that really necessary?" Later I thought carefully about my own code and changed my mind.

Think about this pattern:

```cpp
std::optional<User> maybe_user = find_user(id);
if (maybe_user) {
    // the next few dozen lines all operate on maybe_user.value()
    // there might be more nested checks in between
    // you have to keep remembering "I'm inside the if, it's safe"
}
```

When the if body is long and you read `maybe_user.value()` somewhere in the middle, you have to scroll up to confirm there's an if guarding it. C++26 lets you write it like this instead:

```cpp
for (auto& user : maybe_user) {
    // inside here, user is User&, not an optional
    // use user freely for dozens of lines, it's a definite value
}
```

The principle is simple. If the optional is engaged, `begin()` returns a pointer to the internal object and `end()` returns `begin() + 1`, so the loop runs once; if it's disengaged, `begin()` equals `end()` and the loop runs zero times. It's not a container (a container has a whole pile of requirements), it just happens to provide `begin` and `end` as a range. Let's run it to verify:

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

The engaged optional iterates once and picks up 42, the empty one iterates zero times. Not some earth-shattering feature, but in those long stretches of business logic, being able to forget about the optional entirely inside the loop body and deal only with the bare type — that mental-load reduction is real.

:::warning
When iterating an optional, write the loop variable as `auto&&`, not `auto`. `auto` drops the reference-ness; in the next piece you'll see the `optional<T&>` specialization, where `auto x` would grab a copy rather than a reference and the semantics would be wrong. `auto&&` is a forwarding reference and correctly preserves the original value category. This point was actually a bug in Steve Downey's own slides; someone pointed it out during Q&A before he admitted it.
:::

## Defaulted optional parameters: nice to use, a nightmare to implement

There's another very common use of optional — a function's defaulted parameter:

```cpp
void process(std::optional<int> timeout = std::nullopt);

process();                        // timeout is nullopt
process(42);                      // timeout is optional<int>(42)
process(std::optional<int>{});    // you can also pass it explicitly
```

It feels completely natural to use; an `int` gets promoted to `optional<int>` automatically. But you may not have thought about the fact that to support this implicit conversion, optional's constructor design becomes extremely complex. It has to handle construction from `T`, from `nullopt`, from another `optional<U>`, plus copy and move — these constructors and conversion operators combine into a huge overload set.

I used to think "overload resolution is just finding the best match, right?" But once an overload set gets big enough, the result is often surprising. When the human brain reasons about overload resolution it tends to walk a decision tree — "this type goes down this branch, that type goes down that branch." The compiler doesn't work that way. It spreads all the candidate functions out in a flat set and scores them by rules like implicit-conversion rank and template specialization ordering, then picks the best. Once there are enough candidates, the scores can stop matching intuition. That whole pile of SFINAE and concepts constraints inside optional's implementation is, at its core, taming this giant overload set — making sure "pass an int and it goes down the int path, pass nullopt and it goes down the nullopt path, with no unexpected ambiguity." Behind every line of SFINAE is a bloody history of overload-resolution pitfalls.

## Up next

The foundation of the value version is now in place: it owns, it's value-semantic, it's `T` plus an extra state, in C++26 it can be a range, and its constructors carry a heavy overload set for the sake of implicit conversions. But the moment `T` becomes a reference, all of these premises get overturned. `optional<T&>` doesn't own anything, assignment is no longer a value copy, and the constructor chain has to be redesigned. [In the next piece](./03-optional-reference-and-assignment.md) we officially step into the core of `optional<T&>`: what it actually is, why its assignment has to be a rebind, and the pits that `make_optional` and CTAD dig on references.
