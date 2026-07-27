---
chapter: 13
cpp_standard:
- 20
description: 'Using a string as a template parameter was hard enough that almost nobody did it before C++17. The const char* as NTTP trap, C++20 P0732 and structural types, the fixed_string idiom, and compile-time hashing as an alternative that does not touch NTTP.'
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'TMP Core Techniques: The World Before Concepts'
- 'Concepts: Putting Constraints in the Signature'
reading_time_minutes: 12
related:
- 'TMP Core Techniques: The World Before Concepts'
- 'Static Reflection Basics: The Reflection Operator and Splice'
tags:
- host
- cpp-modern
- intermediate
- 编译期计算
- 模板元编程
- 类型安全
title: 'Compile-Time Strings: NTTP Class Type and fixed_string'
---
# Compile-Time Strings: NTTP Class Type and fixed_string

The last piece ended on a teaser: using a string as a template parameter. It sounds simple, `template <"hello">`, right, but before C++17 it was hard enough that almost nobody bothered. The C++20 P0732 proposal blew that door open. It let a class type be used as a non-type template parameter (NTTP), and the `fixed_string` idiom is a direct beneficiary. This piece walks from "why it used to be hard" to "how it feels natural now," and along the way explains what the new "structural type" concept actually constrains.

## First, the trouble: const char* as an NTTP before C++20

Before C++20, an NTTP could only be an integer, an enum, a pointer, or a reference. A string had to come in through `const char*`. But `const char*` as a template parameter is a bad fit. Here's the sharpest edge: a string literal **cannot be written directly in the template parameter list**.

```cpp
template <const char* Name>
struct Bad {};
Bad<"hello"> b;   // literal as NTTP, fails to compile
```

```text
error: '"hello"' is not a valid template argument for type 'const char*'
       because string literals can never be used in this context
```

GCC's wording is blunt: `string literals can never be used in this context`. The reason is that a template argument needs linkage, so the same template instance can be merged across translation units. A string literal has no linkage, so the compiler refuses it outright. To use a `const char*` NTTP, you first have to store the string in a `constexpr` variable with external linkage:

```cpp
constexpr const char kRed[] = "red";   // an object with linkage
template <const char* Name>
struct Tagged { static constexpr const char* name = Name; };

Tagged<kRed> t;   // compiles now
```

It compiles, but this drags in a second problem. The argument in `Tagged<kRed>` is the **address** of the object `kRed`, not the contents of `"red"`. If two translation units each define a `kRed`, their addresses differ, and `Tagged<kRed>` instantiates two different types. That fights the template intuition that "same argument means same type." Pile on the maintenance cost: every string you want as a template parameter needs a variable declared somewhere outside, and the codebase fills up with these boilerplate variables that exist only to serve the compiler. Together that's why "string as a template parameter" was basically unused before C++20.

## The C++20 fix: P0732 and structural types

P0732 (proposed by Louis Dionne, accepted into C++20) opens a new path: a class type can serve as an NTTP. There's a catch. The class has to be a **structural type**. The core requirement, in two sentences: it has to be a literal class type (constructible and destructible at compile time), and **all of its base classes and all of its non-static data members must be public**. The second part is the line you trip over most when writing code, and we'll demonstrate it later. The intent is to let the compiler decide whether two template arguments are equivalent "by the value of the data members," without calling a user-written `operator==`. Calling user code inside the compile-time machinery of template equivalence would invite trouble.

`fixed_string` is designed around exactly that rule: wrap a string in a struct whose only non-static data member is a public `char` array, and it satisfies structural, so it can be an NTTP. Here's a minimal working version:

```cpp
template <std::size_t N>
struct FixedString {
    char value[N] = {};

    // A literal "abc" has type const char[4] (including \0), which matches const char(&)[N]
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

// CTAD deduction guide: a literal "hello" deduces to FixedString<6> (6 includes the trailing \0)
template <std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;
```

One detail worth slowing down on. A string literal like `"hello"` has type `const char[6]`, five characters plus a trailing `\0`. So the `N` deduced by CTAD is 6, and `value[6]` holds the full string and the `\0`. The constructor takes `const char(&str)[N]`, a reference to an array, so `N` is deduced from the literal's size. No need to write it by hand.

::: warning operator== does not participate in template equivalence
The `operator==` we wrote for `FixedString` only gets called when "comparing two objects at runtime" or "manually comparing in an `if constexpr`." It has **nothing to do with template argument equivalence**. When the compiler decides whether `Named<"abc">` and another `Named<"abc">` are the same type, it follows the structural rule: compare the `value` arrays byte by byte. It calls no user code. In other words, delete the `operator==` entirely, and `Named<"abc">` and `Named<"abc">` are still the same type, while `Named<"abc">` and `Named<"abd">` are still two different types. Get this wrong and every piece of NTTP-class-type code you read will feel twisted.
:::

## fixed_string in action: a string as a template parameter

With the design done, you can drop `FixedString` straight into `template <...>`:

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
  title="fixed_string as an NTTP, a string baked into the type"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/nttp_fixed_string.cpp"
  description="FixedString is structural and goes straight into a template parameter. The string is baked into the type itself."
/>

Run it:

```text
world
hello, templates
编译期字符串比较断言通过
```

`Named<"world">{}` instantiates a type whose `name` member holds `"world"` at compile time. `greet<"templates">()` does the same: the string `"templates"` is baked into the type itself. This was nearly impossible in C++17. Now one template parameter solves it.

Now the compile-time string comparison. `FixedString{"abc"} == FixedString{"abc"}` holds inside a `static_assert`, which means the comparison runs entirely at compile time and produces a constant bool. This matters in "dispatch by string at compile time" scenarios. Pick an implementation based on a configuration string. In the past you'd route through an enum. Now you can speak in string literals directly.

## The structural limit: why you can't throw any old class at an NTTP

P0732 opened a door, but only the structural one. Let's try a deliberate counterexample: a class with a private data member, shoved into an NTTP.

```cpp
class Secret {
    int x;   // private (members of a class are private by default)
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

The error states the rule plainly: `not structural`, because `Secret::x` isn't public. The compiler needs to "look at the values member by member" to decide whether two NTTPs are equivalent. If a member is private, the compiler can't reach it that directly (access semantics get involved), so it draws a hard line and requires all public. So the first thing to check when you want a custom class as an NTTP is whether its data members are all exposed. Forget a class with a `std::string` member, too. `std::string` itself has private data, so it isn't structural, and that disqualifies the whole class.

## Compile-time hashing: you don't always need NTTP

One thing to add here: not every "process strings at compile time" need requires baking the string into a type. A `constexpr` function is enough for many cases, like computing a string hash at compile time:

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
  description="A pure constexpr function, no NTTP. The hash is computed at compile time and fits into a static_assert."
/>

Run it:

```text
fnv1a_64("hello") = 11831194018420276491
fnv1a_64("world") = 5717881983045765875
编译期哈希断言通过
```

`fnv1a_64("hello")` is computed at compile time. You can put it in a `static_assert`, use it as a `switch` `case` (the hash is a constant), or do compile-time string matching. This path never touches NTTP, and it's more general: `std::string_view` accepts any string source. So here's the plain decision rule. When you need to bake a string into a type, have it participate in overloading, or use it as a type tag, reach for `fixed_string` NTTP. When you just want to compute a result on a string at compile time, a `constexpr` function is enough. Don't drag in NTTP.

In the next piece we look ahead to C++26. Static reflection (P2996) is about to turn "enum to string" and "look up a type by name," things that today need `fixed_string` plus a pile of template boilerplate, into built-in language capabilities.
