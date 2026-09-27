---
chapter: 13
cpp_standard:
- 20
description: 'Using strings as template arguments was so hard before C++17 that almost nobody did it. The traps of const char* as an NTTP, C++20 P0732 structural types, the fixed_string idiom, and compile-time hashing as the alternative path that never touches NTTP.'
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'TMP Core Techniques: The World Before Concepts'
- 'Concepts: Putting Constraints in the Signature'
reading_time_minutes: 12
related:
- 'TMP Core Techniques: The World Before Concepts'
- 'Static Reflection Basics: The Reflection Operator and Splice Recomposition'
tags:
- host
- cpp-modern
- intermediate
- 编译期计算
- 模板元编程
- 类型安全
title: 'Compile-Time Strings: NTTP Class Type and fixed_string'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/05-compile-time-strings.md
  source_hash: 46344aaed4633a131ea71d231b31b48b2d6455810fb682252a1a4a69cf40bbea
  translated_at: '2026-09-26T04:53:36+00:00'
  engine: anthropic
  token_count: 3800
---
# Compile-Time Strings: NTTP Class Type and fixed_string

The last piece ended on a teaser: using a string as a template argument. It sounds simple — `template <"hello">`, right? — but in C++17 and earlier it was so hard that almost nobody wanted to do it. The C++20 P0732 proposal pushed that door wide open, letting a class type serve as a non-type template parameter (NTTP), and the classic `fixed_string` idiom is a direct beneficiary. In this piece we walk from "why it used to be hard" to "how it just works now," and along the way make clear what the new concept of a structural type actually constrains.

## First, the Trouble: const char* as an NTTP Before C++17

Before C++20, an NTTP could only be an integer, an enum, a pointer, or a reference — the only way a string could squeeze in was through `const char*`. But `const char*` as a template parameter is a bad fit by nature. Here is the most fatal cut first: a string literal **cannot be written directly in a template parameter list at all**.

```cpp
template <const char* Name>
struct Bad {};
Bad<"hello"> b;   // a literal as an NTTP: fails to compile
```

```text
error: '"hello"' is not a valid template argument for type 'const char*'
       because string literals can never be used in this context
```

GCC's own words spell it out: `string literals can never be used in this context`. The reason is that a template argument is required to have linkage, so that the same template instance can be merged across two translation units; a string literal has no linkage, and the compiler refuses it outright. To make `const char*` work as an NTTP, you first have to store the string in a `constexpr` variable with external linkage:

```cpp
constexpr const char kRed[] = "red";   // an object with linkage
template <const char* Name>
struct Tagged { static constexpr const char* name = Name; };

Tagged<kRed> t;   // this compiles
```

It compiles, sure, but that pulls in a second layer of trouble: the argument in `Tagged<kRed>` is the **address of the object** `kRed`, not the contents of the character sequence `"red"`. If two translation units each define a `kRed`, their addresses differ, and `Tagged<kRed>` instantiates as two different types. That clashes with the template intuition that "same argument means same type." Stack one more layer of maintenance cost on top: every string you want to use as a template argument has to be declared as a variable somewhere outside first, and the code fills up with these boilerplate variables that exist only to serve the compiler. Add it all up, and that is why "strings as template arguments" was essentially unused before C++20.

## The C++20 Antidote: P0732 and Structural Types

P0732 (proposed by Louis Dionne, merged into C++20) opened a new road: a class type can now serve as an NTTP. But there is one precondition — the class has to be a so-called **structural type**. The structural requirement boils down to two sentences: it must be a literal class type (constructible and destructible at compile time), and **all base classes and all non-static data members must be public**. The second half is the line easiest to trip over when writing code, and we will demonstrate it in detail later. The intent is to let the compiler decide whether two template arguments are equivalent "by the values of the data members," without ever calling a user-written `operator==` — invoking user code inside template equivalence, a compile-time mechanism, would stir up a pile of trouble.

`fixed_string` is designed precisely along this rule: wrap a string in a struct whose only member is a public `char` array, and it satisfies structural, so it can serve as an NTTP. Here is a minimal working implementation:

```cpp
template <std::size_t N>
struct FixedString {
    char value[N] = {};

    // The literal "abc" has type const char[4] (including \0), which matches const char(&)[N]
    constexpr FixedString(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) value[i] = str[i];
    }

    constexpr bool operator==(const FixedString& other) const {
        for (std::size_t i = 0; i < N; ++i) {
            if (value[i] != other.value[i]) return false;
        }
        return true;
    }

    constexpr const char* c_str() const { return value; }
};

// CTAD deduction guide: lets the literal "hello" deduce FixedString<6> (6 includes the trailing \0)
template <std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;
```

There is one detail here worth stopping to look at closely. A string literal like `"hello"` has type `const char[6]` — five characters plus a trailing `\0`. So the `N` deduced by CTAD is 6, and `value[6]` holds exactly the full string and the `\0`. The constructor takes `const char(&str)[N]` (a reference to the array), so `N` is deduced from the literal's length — no need to write it by hand.

::: warning operator== does not participate in template equivalence
The `operator==` we wrote for `FixedString` only gets called when "comparing two objects at run time" or when you manually compare inside `if constexpr` — it has **nothing to do with template argument equivalence**. When the compiler decides whether `Named<"abc">` and another `Named<"abc">` are the same type, it goes by the structural rule: it compares the values of the `value` arrays byte by byte and calls no user code whatsoever. In other words, delete the `operator==` entirely, and `Named<"abc">` and `Named<"abc">` are still the same type, while `Named<"abc">` and `Named<"abd">` are still two different types. Get this twisted, and reading other people's NTTP-class-type code will feel off at every turn.
:::

## fixed_string in Practice: Strings as Template Arguments

With the design in place, you can drop `FixedString` straight into `template <...>`:

```cpp
template <FixedString S>
struct Named {
    static constexpr auto name = S;
};

template <FixedString S>
void greet() {
    std::cout << "hello, " << S.c_str() << "\n";
}
```

Run it:

<OnlineCompilerDemo allow-run
  title="fixed_string as an NTTP: a string baked into the type"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/nttp_fixed_string.cpp"
  description="FixedString is a structural type, so it drops straight into a template parameter, and the string gets baked into the type itself."
/>

Output:

```text
world
hello, templates
编译期字符串比较断言通过
```

`Named<"world">{}` instantiates a type whose `name` member holds `"world"` at compile time; `greet<"templates">()` works the same way — the string `"templates"` is baked into the type itself. This was nearly impossible to pull off in C++17; now a single template parameter settles it.

Now look at compile-time string comparison. `FixedString{"abc"} == FixedString{"abc"}` holds inside a `static_assert`, which shows the comparison completes entirely at compile time and yields a constant boolean. That capability is very real in "dispatch by string at compile time" scenarios — picking a different implementation based on a configuration string, for instance. That used to require a detour through an enum; now you can speak directly in string literals.

## The Limits of structural: Why You Cannot Just Grab Any Class as an NTTP

P0732 opened the door, but only the one for structural classes. Let's try a deliberately broken counterexample and shove a class with a private data member into an NTTP:

```cpp
class Secret {
    int x;   // private (class members are private by default)
public:
    constexpr Secret(int v) : x(v) {}
};

template <Secret S>
struct Bad {};
Bad<Secret{1}> bad;
```

```text
error: 'Secret' is not a valid type for a template non-type parameter
       because it is not structural
note: 'Secret::x' is not public
```

The error states the rule plainly: `not structural`, because `Secret::x` is not public. For the compiler to judge whether two NTTPs are equivalent by "looking at the values member by member," the members have to be reachable that directly — if a member is private, the compiler cannot access it so bluntly, and access-control semantics get dragged in, so the rule draws one hard line and requires everything public. So the first step in making a custom class an NTTP is checking whether its data members are all exposed. Do not hold out hope for a class with a `std::string` member either: `std::string` has private data of its own, fails structural, and takes the whole class down with it.

## Compile-Time Hashing: You Do Not Always Need NTTP

One more note as we reach this point: not every "process strings at compile time" need requires baking the string into a type. Plenty of scenarios are satisfied by a plain `constexpr` function — computing a string hash at compile time, for example:

```cpp
constexpr std::uint64_t fnv1a_64(std::string_view s) {
    std::uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
    for (char c : s) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
}
```

<OnlineCompilerDemo allow-run
  title="Compile-time FNV-1a string hash"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/compile_time_hash.cpp"
  description="A pure constexpr function that never touches NTTP: the hash is settled at compile time and fits into a static_assert."
/>

Output:

```text
fnv1a_64("hello") = 11831194018420276491
fnv1a_64("world") = 5717881983045765875
编译期哈希断言通过
```

`fnv1a_64("hello")` is settled at compile time: it can go into a `static_assert`, serve as a `case` in a `switch` (the hash value is a constant), or power compile-time string matching. This approach never touches NTTP, and its generality is actually better — `std::string_view` accepts a string from any source. So here is the plain decision rule: when you need to bake a string into a type, have it participate in overloading, or use it as a type tag, reach for the `fixed_string` NTTP; when you just want to compute a result over a sequence of characters at compile time, a `constexpr` function is enough — do not bring NTTP into it.

In the next piece we glance ahead at C++26: static reflection (P2996) is set to turn things like "enum to string" and "look up a type by name" — jobs that today take `fixed_string` plus a pile of template grunt work — into built-in language capabilities.
