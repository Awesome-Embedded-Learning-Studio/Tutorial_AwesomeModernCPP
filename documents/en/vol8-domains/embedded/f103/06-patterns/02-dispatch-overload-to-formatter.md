---
title: "From overload sets to Formatter specializations: choosing the dispatch mechanism for an embedded logger"
description: "A record of the three-stage evolution of the libestdx logger's line-assembly dispatch mechanism: the member overload set (with two iron rules learned the hard way) was only an intermediate form, CPO/tag_invoke was pretty but a turn-off, and we finally landed on a Formatter<T> specialization in the shape of std::formatter — the code, the real error messages, and the trade-off reasoning of all three forms are archived in full, along with the decoupling of LineBuffer from formatting"
chapter: 6
order: 2
tags:
  - stm32f1
  - intermediate
  - concepts
  - 模板
  - 零开销抽象
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 18
prerequisites:
  - "A zero-overhead logger: the complete pitfall log, from hitting the source_location wall to acceptance by disassembly"
related:
  - "UART: interrupt-driven, ring buffer, expected"
translation:
  source: documents/vol8-domains/embedded/f103/06-patterns/02-dispatch-overload-to-formatter.md
  source_hash: 7f56dcf6fb1be75e45567cba0187e441708563e7bba2b826282b8d7858558448
  translated_at: '2026-09-27T05:53:35+00:00'
  engine: anthropic
  token_count: 2700
---

# From overload sets to Formatter specializations: choosing the dispatch mechanism for an embedded logger

## Introduction: this article documents an "intermediate form"

In the previous article, when we wrote the line assembler `LineBuffer` for libestdx's logging component, the final form of type dispatch was a `Formatter<T>` specialization. But it evolved from two predecessors: an `if constexpr` chain that got roasted for reading like plumbing, and a **member overload set** version that could genuinely hold its own. In between, we also seriously discussed the CPO / `tag_invoke` route — the same one `ranges` uses — and ultimately backed away.

This article archives the whole three-stage evolution — not "what the correct answer is", but "where each form is strong, where it dies, and why, for this particular embedded scenario, the final vote went to the shape of `std::formatter`". All three forms are legitimate designs, and plenty of well-known libraries stop at the intermediate form; the value of the discussion is in seeing each one's bill.

First, let's pin down what we are dispatching. A log call looks like this:

```cpp
Log::info("led", "count=", n, " hex=", log::Hex(n), " ok=", true);
```

The line assembler has to answer one question: **given the type of each argument, how does it become text bytes**? Strings are copied straight across, integers go through `to_chars`, the `Hex` wrapper goes hexadecimal, user types speak for themselves, and single characters and floating-point values must be rejected with targeted messages. That is the entire problem statement of the "dispatch mechanism".

## Form zero: the if constexpr chain, and why it died first

The most intuitive version is one giant single-entry template with an `if constexpr` branch per category:

```cpp
template <typename V>
void append(V&& value) {
    using U = std::remove_cvref_t<V>;
    if constexpr (std::same_as<U, char>) { /* ... */ }
    else if constexpr (std::same_as<U, bool>) { /* ... */ }
    else if constexpr (std::convertible_to<const U&, std::string_view>) { /* ... */ }
    else if constexpr (requires { typename U::is_log_hex; }) { /* ... */ }
    // ...six or seven branches
}
```

It runs, with zero runtime overhead, but it reads like a tangle of plumbing — the branch order carries hidden semantics (`string_view` must come before `convertible_to`, or you get infinite recursion), and the reader has to mentally simulate the dispatch to confirm which path fires. After the roasting we immediately switched to form one. Looking back now, the chain's real problem is that **it turns "dispatch order" — something the language's rules should own — into a convention of code layout**.

## Form one: the member overload set — an intermediate form, and harder to keep happy than it looks

Flatten the chain into an overload set, one `append` overload per type — the classic shape of the whole `operator<<` family, and much easier on the eyes:

```cpp
struct LineBuffer {
    void append(char) = delete;                        // targeted rejection
    void append(bool b);
    void append(std::string_view view);
    void append(std::string_view s, std::size_t width);

    template <TextSource T>          void append(T s);   // literals/string
    template <IsHex H>               void append(H h);
    template <std::integral T>       void append(T v);

    template <typename T>
        requires requires { std::declval<const T&>().append_to(std::declval<LineBuffer&>()); }
    void append(T t) { t.append_to(*this); }            // extension point for user types

    template <std::floating_point T> void append(T) = delete;  // reject float
    template <typename T>            void append(T) = delete;  // catch-all rejection
};
```

We actually wrote this version and ran every smoke gate green. But it carries two **iron rules that must be guarded by hand** — both discovered by crashing into them for real, not derived from theory:

**Iron rule one: the parameter forms of the template overloads must be uniform (pass by value across the board).** Our catch-all rejection was first written as `const T&`; sitting next to the by-value integer overload, `append(42)` went straight to ambiguity:

```text
error: call of overloaded 'append(int)' is ambiguous
  • candidate 1: 'void LineBuffer<N>::append(T) [with T = int]'
  • candidate 2: 'void LineBuffer<N>::append(const T&) [with T = int]' (deleted)
```

Once the parameter forms are mixed, partial ordering cannot rank the candidates, and "the more constrained one wins" cannot save the day either.

**Iron rule two: no `*this` inside a member function's constraints.** To express "this type can `append_to` my buffer", the intuitive spelling is `requires requires(const T& t) { t.append_to(*this); }`, which errors out with:

```text
error: invalid use of 'this' at top level [-Wtemplate-body]
```

Constraints are evaluated in a context where `this` is not available; the only way out is to draft `std::declval<LineBuffer&>()` as a stand-in.

Both rules can be held in place by writing them into comments, but they expose a deeper problem: **the `LineBuffer` class is working two jobs** — byte-window management (cursor, clamping, the truncation flag, the line ending) and the type-to-text formatting policy (the overload set, `to_chars`, the rejection list). With both jobs sewn into one class and the overload set living as members, that is exactly why the iron rules must exist; the extension point receives a concrete `LineBuffer&`, so the constraint has to know that concrete type, and the `declval` dance never goes away.

## The form-two candidate: CPO / tag_invoke — pretty, but a turn-off

One very modern route came up in the discussion — customization point objects (CPOs) plus the `tag_invoke` protocol, the same machinery as `std::ranges` and `std::execution`:

```cpp
inline constexpr struct append_value_t {
    template <typename Buffer, typename T>
    void operator()(Buffer& buffer, const T& value) const
        noexcept(noexcept(tag_invoke(*this, buffer, value)))
    {
        tag_invoke(*this, buffer, value);
    }
} append_value;

// user type: specialize tag_invoke as a hidden friend
struct Foo {
    int x;
    friend void tag_invoke(append_value_t, auto& buffer, const Foo& foo) {
        buffer.append(foo.x);
    }
};
```

To be fair, this machinery solves real problems: one unified protocol, no occupation of global function names, constraints that can be probed (`is_tag_invocable`). But its cognitive price for the **embedded reader** is a three-piece bundle all at once: CPO function objects, hidden friends, and two-phase ADL lookup — an engineer writing microcontroller firmware who sees `tag_invoke(*this, buffer, value)` will most likely not react with "ah, a customization point" but with "what is this". And the community's criticisms of it are already on record: [Barry Revzin's classic critique](https://brevzin.github.io/2020/11/30/tag-invoke/) points out that `tag_invoke` itself becomes a globally reserved identifier (ironically, exactly the problem it meant to solve), that error messages get worse, and that customization is invisible at the call site; even the std::execution community, its heaviest user, is [seeking member customization points in the follow-up P2300 discussions](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3281r0.html) to sidestep this layer of complexity. However elegant the protocol layer, the "readers get it at a glance" vote simply carries more weight in a tutorial library. **Turned down, and recorded as such.**

## Form three: the Formatter<T> specialization — the final form

The final landing spot is a shape everyone already knows — a replica of `std::formatter`. The library provides only a primary template, built-in types are implemented via (constrained) specializations, and user types specialize it the same way:

```cpp
// primary template: unspecialized = unsupported, the message points the way
template <typename T>
struct Formatter {
    static void format(auto&, const T&) {
        static_assert(!sizeof(T),
                      "logger: no formatter for T; specialize logger::Formatter<T> or wrap in Hex");
    }
};

template <>           struct Formatter<bool>  { /* "true"/"false" */ };
template <TextSource T> struct Formatter<T>   { /* convert to string_view */ };
template <IsHex T>    struct Formatter<T>     { /* "0x" + to_chars 16 */ };
template <typename T>
    requires std::integral<T> && (!std::same_as<T, char>) && (!std::same_as<T, bool>)
struct Formatter<T>   { /* to_chars 10 */ };
```

The extension point for a user type becomes a specialization, understandable at a glance:

```cpp
template <>
struct estdx::logger::Formatter<Point> {
    static void format(auto& out, const Point& p) {
        out.append("("); out.append(p.x); out.append(","); out.append(p.y); out.append(")");
    }
};
```

Meanwhile `LineBuffer` is split into a pure byte layer (clamped copies, `pad`, the `window()`/`commit()` pair handed to `to_chars`, `finish`), and the only seam between the two layers is a one-line forward:

```cpp
template <typename T>
void append(const T& value) {
    Formatter<std::remove_cvref_t<T>>::format(*this, value);   // a forward declaration of Formatter is enough
}
```

This form settles form one's bill in one go:

- **Both iron rules vanish.** No overload set, no overload-resolution pitfalls; dispatch becomes partial-specialization ordering, and the set of specializations is mutually exclusive by construction (the integral specialization explicitly excludes `char`/`bool`) — there are no "rules a human must guard";
- **The `declval` dance vanishes.** `format(auto& out, ...)` is a function parameter free for the taking, no longer a `*this` inside a member constraint;
- **Rejection quality holds up.** The three negative cases each point at their own specialization line; the real error messages:

```text
format.hpp:76  error: static assertion failed:
               logger: single chars are not in the logging vocabulary; pass a string_view
format.hpp:86  error: static assertion failed:
               logger: float rejected (FP formatting tables cost flash); use Hex or fixed-point
format.hpp:35  error: static assertion failed:
               logger: no formatter for T; specialize logger::Formatter<T> or wrap in Hex
```

- **Clean responsibility boundaries**: `LineBuffer` answers only "how do I push bytes into a fixed window safely", and `Formatter<T>` answers only "how does a type become log text". Swapping the formatting policy (say, adding a formatter for a binary channel later) never touches a single line of the buffer.

Of course it has costs too, recorded honestly: a specialization carries a bit more ceremony than "write a member function"; dispatch moves from overload resolution to template instantiation, adding one more layer to the error stack (though the message backstop actually becomes more controllable); and two language details have to be stepped on correctly —

**Detail one: when rejecting via `static_assert`, the condition must depend on a template parameter.** `static_assert(false)` written inside a function template detonates unconditionally wherever it is included; it has to be spelled `static_assert(!sizeof(T), ...)` (`T` is a template parameter — only the dependence defers the failure to instantiation). Inside the explicit specialization `Formatter<char>`, `char` is already pinned down, so we borrow the `auto& out` parameter of `format`: `static_assert(sizeof(out) == 0, ...)`.

**Detail two: `!concept<T>` in a requires clause is not a primary expression.** When a constrained partial specialization is written as `requires std::integral<T> && !std::same_as<T, char>`, clangd immediately flags it red:

```text
Parentheses are required around this expression in a requires clause
```

The correct spelling parenthesizes the non-primary operand: `std::integral<T> && (!std::same_as<T, char>)`. GCC is more lenient than the standard here and compiles it either way, but portable code has to follow the grammar.

## The ledger for the three forms

| | if constexpr chain | member overload set | CPO / tag_invoke | Formatter specialization |
|---|---|---|---|---|
| Scattering | All in one place (which is also all its fault) | One member per type | hidden friend + CPO | One specialization per type |
| Rules a human must guard | Branch order | Two iron rules (uniform parameters / no this) | Protocol conventions + ADL mental load | Specialization set mutually exclusive (correct by construction) |
| Rejection error messages | static_assert wording | `= delete`, reason per case | Weak (a community-acknowledged pain point) | static_assert, reason per case |
| Reader threshold | Low (just long) | Medium (understand overload resolution) | High (CPO + ADL + two-phase) | Low (`std::formatter` is a familiar face) |
| Coupling to the buffer | Fully coupled | Extension point nailed to a concrete type | Decoupled | A one-line forwarding seam |

Our scenario's weights were "tutorial library + embedded readers + rejection errors that point the way", so the final vote went to the Formatter specialization. If your library is a generic framework and your readers are library authors, the CPO's unified protocol may well be worth it; if your set of types is closed and small, an overload set or even a chain is entirely sufficient — no form is right or wrong, only ones whose books don't balance.

## Summary

- The intermediate form (the member overload set) is not a failed product — it ran every gate green; what forced the move was the **responsibility coupling** it exposed — buffer management and formatting policy sewn into one class, with the iron rules and `declval` both complications of that seam;
- CPO / `tag_invoke` was born for "a unified protocol across a generic framework"; in an embedded tutorial library "readers get it at a glance" weighs more, and the community's criticisms of its error messages and invisibility are well founded;
- The `Formatter<T>` specialization borrows `std::formatter`'s familiar face: the specialization set is mutually exclusive by construction, rejection messages each fall where they belong, `LineBuffer` returns to a pure byte layer, and the seam is a one-line forward behind a forward declaration;
- Two language details are worth remembering: a `static_assert` rejection must make the condition depend on a template parameter; and parenthesize `!concept<T>` in a requires clause.

The code lives in [libestdx/logger/](https://github.com/Charliechen114514/libestdx/tree/main/include/libestdx/logger) (`buffer.hpp` for the byte layer + `format.hpp` for the Formatter layer); the intermediate versions from the evolution are permanently archived in this article.

## References

- [Why tag_invoke is not the solution I want — Barry Revzin](https://brevzin.github.io/2020/11/30/tag-invoke/)
- [P3281: Member customization points (the std::execution community's response to tag_invoke's complexity)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3281r0.html)
- [Customization point object - cppreference](https://en.cppreference.com/w/cpp/named_req/CustomizationPointObject)
- [std::formatter - cppreference](https://en.cppreference.com/w/cpp/utility/format/formatter)
- [the libestdx repository](https://github.com/Charliechen114514/libestdx)
