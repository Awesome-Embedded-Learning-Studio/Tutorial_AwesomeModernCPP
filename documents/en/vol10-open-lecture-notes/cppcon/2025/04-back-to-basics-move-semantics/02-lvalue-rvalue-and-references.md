---
chapter: 4
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: CppCon 2025 talk notes — from the K&R definitions of lvalues and rvalues to the C++11 value category system, covering lvalue references, const reference binding rules, and rvalue references in detail
difficulty: beginner
order: 2
platform: host
reading_time_minutes: 25
speaker: Ben Saks
tags:
- cpp-modern
- host
- beginner
talk_title: 'Back to Basics: Move Semantics'
title: 'Lvalues, Rvalues, and References: The Type System Foundations of Move Semantics'
video_bilibili: https://www.bilibili.com/video/BV1X54y1P7uM
video_youtube: https://www.youtube.com/watch?v=szU5b972F7E
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/04-back-to-basics-move-semantics/02-lvalue-rvalue-and-references.md
  source_hash: 4894c9d47838c8996a6eaef3f4d0316c53573bb1002d56159c070b0cd9824e4a
  translated_at: '2026-09-26T16:09:51+00:00'
  engine: anthropic
  token_count: 5700
---
# Lvalues, Rvalues, and References: The Type System Foundations of Move Semantics

:::tip
This article is a deep-dive remix based on Ben Saks's "Back to Basics: Move Semantics" talk from CppCon 2025. The link above points to YouTube; readers in mainland China can watch the Bilibili version directly. The experiment environment for this article is Arch Linux WSL, GCC 16.1.1.
:::

Last time, our MyString experiments proved that move semantics can collapse a copy's heap allocation + memcpy into a single pointer assignment, speeding up 100,000 concatenations by more than 4x. An exciting result—but the ending left a key cliffhanger: how does the compiler know when it is safe to "steal" resources? It needs a language mechanism to distinguish "this object will still be used" from "this object is about to die". That mechanism is the distinction between lvalues and rvalues.

Honestly, "lvalues and rvalues" used to give me a vague sense of dread. The first time I heard these two terms, my instinctive reaction was: "Isn't that just the left side and the right side of the equals sign?"—and I soon found out that things were not that simple. The `x` in `const int x = 10;` is an lvalue, but you cannot assign to it; `int&& r = 10;` clearly binds to an rvalue, yet `r` itself is an lvalue... These seemingly contradictory phenomena took me quite a while to fully sort out.

## K&R's Original Definition: The Left and Right of the Equals Sign

The two terms "lvalue" and "rvalue" date back to the birth of the C language. K&R introduced these concepts in *The C Programming Language*—the "L" in "L value" comes from the assignment expression `E1 = E2`, where the thing on the **left** (Left) of the assignment operator must have certain specific properties. Concretely, `E1` must be an expression that can be located—the compiler must be able to determine its position in memory before it can write `E2`'s value there.

That is the original intuition: **an lvalue = something that can appear on the left of an assignment**.

Take the simplest example:

```cpp
int n = 1;   // OK: n is an lvalue, 1 is an rvalue
n = 2;       // OK: n is an lvalue, it can appear on the left of an assignment
// 1 = n;    // Error! 1 is an rvalue, it cannot appear on the left of an assignment
```

`n` is a named variable with a definite location in memory; the compiler knows its address, so it can write values into it. The literals `1` and `2`, on the other hand, are pure values—the compiler does not allocate a memory address that you can write to for them. You cannot tell the compiler "please write n's value into the number 1", because the number 1 simply has no concept of an "inside".

This is the first level of understanding lvalues and rvalues. At this level, everything looks fine—an lvalue is something that "has an address and can be assigned to", and an rvalue is something that "has no address and cannot be assigned to".

But wait—do you sense a hidden assumption in this definition? It assumes that "can appear on the left of an assignment" and "has a memory address" are the same thing. In C's earliest days, that assumption mostly held. But C soon introduced `const`, and C++ went on to add references, class types, temporary objects... As the language grew more and more complex, the assumption started to fall apart. Next, we will see how this crack appeared, and why understanding it is crucial for move semantics.

## Basic Classification: Literals and Named Variables

Before we start patching those cracks, let's first get the most basic classification straight, because these rules have not changed from the C era to today.

**Literals are rvalues.** The integer literal `3`, the floating-point literal `3.14`, the character literal `'a'`, enumerator constants—they are all rvalues. They have no memory address (at least not from the programmer's perspective), you cannot assign to them, and they are just "values" in themselves.

**Named variables are lvalues.** `int n;` declares a variable `n` that has a location in memory, and you can both read it and write it. The key point: an lvalue can appear on **either side** of an assignment expression. In `n = 1`, `n` is on the left (being written); in `m = n`, `n` is on the right (being read). But what happens when `n` is on the right? It gets read—the compiler fetches the value stored at `n`'s memory location. This "read" operation has a formal name: **lvalue-to-rvalue conversion**<RefLink :id="1" preview="C++ Standard, [conv.lval] — the standard description of lvalue-to-rvalue conversion" />.

This conversion is practically everywhere; we just don't normally notice it. Whenever you write `int b = a;`, `a` is an lvalue, but in order to assign it to `b`, the compiler must first read out the value stored in `a`—that step is the lvalue-to-rvalue conversion. Understanding that this conversion exists matters, because it explains a subtle fact: **lvalues and rvalues are not two kinds of "things"; they are two "properties" of expressions**. The same variable `a` can exhibit lvalue behavior or rvalue behavior in different contexts.

## const Objects: The First Crack in the K&R Definition

Now the problem arrives. Look at this code:

```cpp
const int max = 100;
// max = 200;    // Error! max is const, cannot be assigned
printf("&max = %p\n", (void*)&max);  // but max has an address!
```

`max` is a const object. You cannot assign to it—`max = 200` is a compile error. By K&R's definition of "lvalue = can appear on the left of an assignment", `max` should not be an lvalue. In reality, though, `max` does have a memory address: you can take its pointer (`&max` is legal), and you can read its value through that pointer.

There is the crack in K&R's definition: **a const object is an lvalue, but not assignable**. The standard terminology calls them "non-modifiable lvalues".

This distinction matters a great deal, because it exposes the true core of the lvalue concept—**having an address**, not **being assignable**. A `const int` object has an address but is not assignable; an integer literal `3` has neither an address nor assignability. The former is a non-modifiable lvalue, the latter an rvalue. The key to telling them apart is not "can it be assigned to", but "does it have a persistent memory location".

The actual output from GCC 16.1.1 confirms this:

```text
max = 100
&max = 0x7ffc47a05dc8
```

`&max` printed a legitimate stack address—this const object genuinely exists in memory.

Here we can draw a contrast to deepen our understanding. The `max` in `const int max = 100;` is a non-modifiable lvalue: it has an address, you cannot assign to it, but you can take its address and read through a pointer. The literal `100`, by contrast, is an rvalue: it has no address, and you cannot assign to it either. What the two share is "not assignable"; the crucial difference is "having a persistent memory location or not". This difference becomes very important in the parts about class types and reference binding—because it is precisely on "whether there is a persistent location" that the compiler decides which references can bind to which expressions.

## Class-Type Rvalues: You Can Call Member Functions on Them

The lvalue/rvalue distinction gets even more interesting with class types. Consider a simple struct:

```cpp
struct Widget
{
    int value;
    void f()
    {
        // this points to the address of the object the function is invoked on
        printf("Widget::f(), value = %d, this = %p\n", value, (void*)this);
    }
};
```

There are two ways to obtain a class-type rvalue. The first is a function return value: a function returning `Widget` by value yields a class rvalue as its return value. The second is a functional cast: `Widget(7)` converts the integer 7 into a temporary object of type `Widget`, which is also a class rvalue.

The interesting part is this: **you can call member functions on a class rvalue**.

```cpp
Widget(7).f();       // OK! Calling f() on a temporary Widget
make_widget(42).f(); // OK! Calling f() on the temporary returned by the function
```

This looks a bit strange—aren't rvalues "address-less"? How can you call a member function on something without an address? The answer is that the compiler does something behind the scenes: it allocates a location in memory for this temporary object—the standard calls this process the **temporary materialization conversion**<RefLink :id="2" preview="C++ Standard, [conv.rval] — temporary materialization conversion" />. The `this` pointer points at that temporarily allocated memory location.

I ran it on GCC 16.1.1, and the result is interesting:

```text
Widget::f(), value = 7, this = 0x7ffc9a466b04
Widget::f(), value = 42, this = 0x7ffc9a466b04
```

Look closely—the `this` addresses of the two calls are exactly the same! That is because the compiler performed NRVO (named return value optimization), placing the temporary returned by `make_widget` directly in the caller's stack space, and the `Widget(7)` temporary happened to be allocated in the same region as well. Short-lived as they are, these temporaries do hold real memory locations while they are alive.

:::warning The version history of temporary materialization — two things to keep apart here
Saying "rvalues have no address" is not really accurate. The accurate statement is—an rvalue does not **need** to have an address; it is not a persistent memory location. But if the compiler temporarily allocates a block of memory for it in order to carry out some operation (calling a member function, binding to a reference), then in that moment it does "have an address". This process of the compiler implicitly allocating memory is temporary materialization.

As for its version history, we have to pull two things apart: the **three-way value category taxonomy** of lvalue / xvalue / prvalue was indeed introduced by C++11; but the "**temporary materialization conversion**" as a named standard conversion was only formally established in **C++17**. Together with C++17's mandatory copy elision (proposal P0135), it was written into the language rules, and the core idea is: **a prvalue is not necessarily an object in itself; only when it is needed as an object (say, to call a member function or bind to a reference) does it "materialize" into a temporary object**. In the C++11 era this mechanism was still brewing and had no official name. So strictly speaking, the temporary materialization in `Widget(7).f()` above has only been the standard semantics since C++17—don't conflate it with C++11's three-way value category taxonomy.
:::

:::warning
The fact that member functions can be called on class rvalues is the foundation of move semantics. Move constructors and move assignment operators are, in essence, "member functions invoked on a temporary object that is about to die"—through rvalue references, we gain the right to modify those temporaries.
:::

## Lvalue References: The First Binding Rule

Now we enter the world of references. Before C++11 introduced rvalue references, what C++ called a "reference" was what we today formally call an "lvalue reference".

"An lvalue reference to T must bind to an lvalue of type T"—that sentence sounds convoluted, but the idea is simple. A reference of type `int&` can only bind to an lvalue of type `int`:

```cpp
int n = 10;
int& ri = n;       // OK: ri binds to the lvalue n
// int& ri2 = 10;  // Error! Cannot bind an lvalue reference to an rvalue (a literal)
```

Why is `int& ri = 10` wrong? Because `10` is an rvalue, and it has no persistent memory location. A reference needs to know the address of the thing it refers to, but an rvalue has no address—there is the contradiction.

But there is a very important exception here: **a const lvalue reference can bind to an rvalue**.

```cpp
const int& cri = 10;    // OK! A const reference can bind to an rvalue
const int& cri2 = 3.14; // OK! It can even bind to a different type (double -> int conversion)
```

The mechanism behind this: the compiler quietly creates a temporary `int` object to hold that value (or the converted value), and then binds the const reference to this temporary object. For `const int& cri2 = 3.14;`, the compiler first performs the `double`-to-`int` conversion (3.14 becomes 3), creates a temporary `int` holding 3, and then binds `cri2` to this temporary. That is why I saw `const lvalue ref to converted: 3` in GCC's output—3.14 got truncated.

You might ask: why must it be `const`? Because if a non-const reference were allowed to bind to an rvalue, you could modify a temporary object through that reference—and that temporary might be destroyed right away; modifying it would be pointless and would easily breed bugs. A const reference bound to a temporary can only be read, not written, so it is safe.

This rule has another important corollary: **a const reference extends the temporary object's lifetime**. Under normal circumstances, the temporary in `Widget(7).f()` is destroyed at the end of the statement. But if a const reference binds to it, this temporary's lifetime gets extended to be as long as the reference's.

Let's give a concrete example of how much this matters. Suppose you write a function returning `std::string` and receive it with a const reference:

```cpp
std::string get_name() { return "hello"; }

const std::string& name = get_name();
// name is still valid here! The temporary's lifetime has been extended
printf("%s\n", name.c_str());  // safe
```

Without the lifetime-extension rule for const references, the temporary `std::string` returned by `get_name()` would be destroyed at the end of the statement, and `name` would become a dangling reference. But because `const std::string&` binds to this temporary, the compiler guarantees that the temporary lives at least until `name` leaves scope.

There is a subtle pit here, though—only the "first" reference that binds directly to the temporary can extend its lifetime; binding indirectly through a chain of references cannot. For instance, in `const std::string& r2 = name;`, `r2` binds to `name` (an lvalue), no temporary is involved, so lifetime extension is not in play. But when multiple layers of indirection onto a temporary are involved, you need to be careful. We have a more detailed discussion in vol2's [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md).

:::warning
Note: the rvalue reference `T&&` likewise extends a temporary object's lifetime. `std::string&& r = get_name();` also keeps the returned temporary alive until `r` leaves scope. This is something rvalue references and const lvalue references have in common—both can bind to temporaries and extend their lifetimes. The difference is that an rvalue reference allows you to modify the temporary, while a const lvalue reference does not.
:::

## Rvalue References: Born for Move Semantics

C++11 introduced a new kind of reference—the rvalue reference, written with the double `&&` syntax.

```cpp
int&& ri = 10;     // OK: an rvalue reference bound to an rvalue (the literal 10)
// int&& ri2 = n;  // Error! An rvalue reference cannot bind to an lvalue
```

The binding rule for rvalue references is exactly the "reverse" of lvalue references: `int&&` can only bind to an rvalue of type `int`. `int&& ri2 = n` is a compile error, because `n` is an lvalue.

:::warning
Even a `const int&&` binds only to rvalues—adding const to an rvalue reference does not suddenly let it bind to lvalues. This point is often mixed up. const rvalue references are hardly ever seen in practice; the standard library has almost no use case for them, but they do exist.
:::

What are rvalue references actually for? The key lies here: **through an rvalue reference, we can modify a temporary object**.

```cpp
int&& ri = 10;  // The compiler creates a temporary int object for the literal 10
ri = 20;        // OK! We modified this temporary object
```

For a simple type like `int`, this has no practical significance. But when we talk about class types—picture a `MyString&&` bound to a temporary `MyString` object, and that temporary holds a dynamically allocated character array inside. Through this rvalue reference, we can "steal" the pointer to that array outright, set the temporary's pointer to `nullptr`, and then have the temporary's destructor do nothing.

That is exactly what the signatures of the move constructor and move assignment operator express: they take their parameter through an rvalue reference, telling the compiler "I know this is a temporary object; I can safely steal its resources". But that part belongs to the next article—first, let's finish rounding out the reference system.

You might also ask a more fundamental question: why did C++11 introduce a brand-new reference type to do this? Why not just reuse lvalue references? The answer: if the move constructor's signature were `MyString(MyString& s)`, it would be ambiguous against the copy constructor `MyString(const MyString& s)`—no, wait, actually it would not be ambiguous, because the const differs. But the real problem is this: if some function accepts both `MyString&` and `const MyString&`, then when the compiler sees `s1 + s2` (an rvalue), it cannot find a matching non-const lvalue reference to bind it to, so a "move" still cannot be triggered. Rvalue references fill that gap: they exist specifically to bind to rvalues, with binding rules that do not overlap those of lvalue references, so that overload resolution can automatically tell apart "this is a persistent object (copy it)" from "this is a temporary object (steal its resources)".

## The C++11 Value Category System: lvalue, xvalue, prvalue

Up to this point I have kept speaking of the two categories "lvalue" and "rvalue", as if the world were strictly black and white. In fact, to support move semantics, C++11 expanded the value category system from a binary one into a ternary one.

Before C++11, every expression was either an lvalue or an rvalue—as simple as that. But C++11 introduced a third category: the **xvalue (expiring value)**. An xvalue says "this object is about to die; its resources can be moved away".

The new taxonomy works like this. First, all expressions are classified along two dimensions: "has identity" (identity—its memory location can be determined) and "can be moved from":

| Category | Has identity | Can be moved from | Examples |
|------|:--------:|:----------:|------|
| **lvalue** | Yes | No | the named variable `n`, `*p`, `++i` |
| **xvalue** | Yes | Yes | the result of `std::move(n)` |
| **prvalue** | No | Yes | the literal `42`, `Widget(7)`, a temporary returned by a function |

On top of that there are two umbrella concepts: **glvalue** (generalized lvalue) = lvalue + xvalue, and **rvalue** = xvalue + prvalue. As a diagram:

```text
            Expression
           /      \
      glvalue    rvalue
      /     \    /    \
  lvalue   xvalue   prvalue
```

- **lvalue**: has identity, cannot be moved from—an ordinary named variable.
- **xvalue**: has identity, can be moved from—the return value of `std::move(x)`. It has a name (or rather, a definite memory location), but the compiler has been told "you may move its resources away".
- **prvalue** (pure rvalue): no identity, can be moved from—a pure temporary value, such as a literal or a temporary returned by a function.

This system looks considerably more complex than the binary classification, but its design logic is clear: move semantics needs a mechanism to express "this thing's resources can be stolen", and xvalue is that bridge. What `std::move` fundamentally does is convert an lvalue into an xvalue, telling the compiler "this object still has a name, but you may move its resources away".

### Value Categories of Common Expressions

Definitions alone may still feel a bit abstract, so let's list the expressions we most often write in day-to-day code and mark which category each belongs to:

| Expression | Value category | Why |
|--------|--------|------|
| `n` (named variable) | lvalue | has a name and a definite memory location |
| `*p` (dereference) | lvalue | the object the pointer points to has a memory location |
| `++i` (pre-increment) | lvalue | returns the modified `i` itself |
| `i++` (post-increment) | prvalue | returns a copy of the old value, which is a temporary |
| `42` (integer literal) | prvalue | a pure value with no memory location |
| `"hello"` (string literal) | lvalue | a string literal is a const char array, which has an address |
| `Widget(7)` (functional cast) | prvalue | creates a temporary Widget object |
| `make_widget()` (return by value) | prvalue | the temporary value returned by the function |
| `std::move(n)` | xvalue | explicitly casts the lvalue into the "movable" state |
| `a.m` (member access, a is an lvalue) | lvalue | follows `a`'s identity property |
| `std::move(a).m` (member access, a is an xvalue) | xvalue | follows `a`'s xvalue property |

A few points deserve special attention. The string literal `"hello"` is an lvalue, which often surprises people—it is actually an array of type `const char[6]`, stored in the program's read-only data segment with a definite address, and hence an lvalue. Postfix `++` returns a copy of the old value (a temporary), so it is a prvalue; prefix `++` returns the modified object itself, so it is an lvalue. The value category of the member access expression `a.m` stays consistent with `a`'s—if `a` is an lvalue, `a.m` is an lvalue; if `a` is an xvalue, `a.m` is an xvalue.

## Verifying Value Categories with the Compiler

That was a lot of theory, so let's verify it for real using `decltype` and type traits. `decltype` has a very useful property: when applied to a **parenthesized** variable name, `decltype((x))`, it yields a different type depending on the expression's value category—`T&` for an lvalue, `T&&` for an xvalue, and `T` for a prvalue.

```cpp
#include <type_traits>
#include <utility>
#include <cstdio>

template<typename T>
void print_category()
{
    printf("  is lvalue ref: %s\n",
           std::is_lvalue_reference_v<T> ? "yes" : "no");
    printf("  is rvalue ref: %s\n",
           std::is_rvalue_reference_v<T> ? "yes" : "no");
}

int main()
{
    int n = 10;

    printf("decltype((n)):\n");          // n is an lvalue
    print_category<decltype((n))>();     // int& → lvalue ref: yes

    printf("decltype(10):\n");           // 10 is a prvalue
    print_category<decltype(10)>();      // int → neither kind of reference

    printf("decltype(std::move(n)):\n"); // std::move(n) is an xvalue
    print_category<decltype(std::move(n))>(); // int&& → rvalue ref: yes

    return 0;
}
```

GCC 16.1.1's output matches the theory perfectly:

```text
decltype((n)):
  is lvalue ref: yes
  is rvalue ref: no
decltype(10):
  is lvalue ref: no
  is rvalue ref: no
decltype(std::move(n)):
  is lvalue ref: no
  is rvalue ref: yes
```

`decltype((n))` yields `int&`, because `(n)` is an lvalue expression. `decltype(10)` yields `int` (the bare type), because `10` is a prvalue. `decltype(std::move(n))` yields `int&&`, because `std::move`'s return value is an xvalue, and an xvalue shows up as `T&&` in `decltype`.

## "Has a Name, Is an lvalue" — the Trap of Rvalue Reference Parameters

At this point we should talk about a pitfall that nearly every C++ beginner steps in. Ben Saks made a point of emphasizing this rule in his talk: **if it has a name, it is an lvalue**.

Consider a function that takes an rvalue reference:

```cpp
void process(MyString&& s)
{
    // Here, is s an lvalue or an rvalue?
}
```

Seen from outside the function, when you call `process(s1 + s2)`, `s1 + s2` is an rvalue, so the call is fine—an rvalue reference can bind to an rvalue. But **inside** the function, the parameter `s` has a name. It is a named object. By the "has a name, is an lvalue" rule, **inside the function body, `s` is treated as an lvalue**.

What does that mean? If you want to move resources out of `s` again inside the function body, you cannot just move it—the compiler treats `s` as an lvalue and picks copy over move. You must explicitly use `std::move(s)` to tell the compiler "I know what I am doing; treat it as an rvalue".

```cpp
void process(MyString&& s)
{
    MyString copy(s);            // Copy! Because s is an lvalue here
    MyString moved(std::move(s)); // Move! std::move turns s into an rvalue
}
```

The logic behind this rule is actually quite reasonable: a function body can span many lines, and `s` might be used on line ten after being moved on line one. The compiler cannot assume "you only use it on the last line", so it plays it safe—named things are not moved automatically; you must grant permission explicitly.

:::tip
This "name = lvalue" rule can be verified with `decltype`. If you write `decltype((s))` inside a function template, then even when `s`'s declared type is `MyString&&`, `decltype((s))` still yields `MyString&` (an lvalue reference), not `MyString&&`. That is because parenthesized `decltype` looks at the expression's value category, and `s`, being a named object, has lvalue as its value category. This is a favorite trick for planting traps in interview questions.
:::

:::tip
The "has a name, is an lvalue" rule has one important exception: **the return statement**. The `s` in `return s;` does have a name, but since C++11 it is treated as an "implicitly movable entity", and the compiler can move from it directly without you writing `std::move(s)`. And in practice the compiler may do even better—through NRVO it can eliminate the copy outright. We leave the full discussion of this topic for the next article.
:::

## Quick Reference Table of Reference Binding Rules

Let's gather all the reference binding rules covered in this article into a single table for easy lookup:

| Reference type | Binds to lvalue? | Binds to rvalue? | Binds to a different type? | Can modify the referent? |
|----------|:-----------------:|:-----------------:|:------------------:|:-----------------:|
| `T&` | Yes | **No** | No | Yes |
| `const T&` | Yes | **Yes** | Yes (with conversion) | No |
| `T&&` | **No** | Yes | No | Yes |
| `const T&&` | **No** | Yes | No | No |

This table packs in a fair amount of information, but a few key conclusions deserve to be remembered specifically. First, `const T&` is the "universal receiver"—it can bind to almost anything (lvalues, rvalues, even different types), at the cost of not letting you modify the referent through it. Second, `T&&` binds only to rvalues, which is exactly what move semantics needs: it guarantees that whatever it binds to is an object whose "resources can be safely stolen". Third, `const T&&` does exist, but it is almost useless—it can bind to rvalues yet cannot modify them, which throws away the rvalue reference's core advantage of "allowing modification of temporary objects".

## What We Have Figured Out So Far

In this article we started from K&R's "left of the equals sign" and step by step built the full picture of C++ value categories. We saw how const objects broke the old "lvalue = assignable" definition, how class rvalues obtain memory locations through temporary materialization, how sharply the binding rules of lvalue references and rvalue references differ, and finally found the theoretical foundation of move semantics in C++11's ternary lvalue/xvalue/prvalue system.

The core takeaways are two. First, the rvalue reference `T&&` binds only to rvalues, which hands the compiler a natural signal—"what is bound here is temporary, and its resources can be safely stolen". Second, the "has a name, is an lvalue" rule means we sometimes need `std::move` to tell the compiler explicitly "please allow the move".

Looking back, the lvalue/rvalue distinction was not invented out of thin air by C++11—it existed back in the C era, only it was much simpler then. C++ introduced const, class types, references, operator overloading, and each of those steps blurred the boundaries of value categories further, until move semantics needed a precise mechanism to distinguish "persistent" from "temporary" objects—and only then did C++11 finally formalize this system into the three-tier lvalue/xvalue/prvalue classification. Once you understand the evolution of this system, learning concepts like `std::move`, move constructors, and perfect forwarding later on will go much more smoothly—because their designs are all answering the same question: "how does the compiler know whether this object can be safely moved?"

With this theoretical foundation in hand, the next article takes us into practice—implementing the move constructor and move assignment operator for MyString, seeing how `std::move` actually works, and under what conditions copy elision lets us skip even the move.

If you want a more systematic treatment of rvalue references, vol2's [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md) is an excellent companion.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [conv.lval] — Lvalue-to-rvalue conversion"
    :year="2020"
    chapter="The standard description of lvalue-to-rvalue conversion"
  />
  <ReferenceItem
    :id="2"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [conv.rval] — Temporary materialization conversion"
    :year="2020"
    chapter="The standard description of temporary materialization conversion"
  />
  <ReferenceItem
    :id="3"
    author="Ben Saks"
    title="Back to Basics: Move Semantics — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=szU5b972F7E"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Value categories"
    url="https://en.cppreference.com/w/cpp/language/value_category"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="Reference declaration"
    url="https://en.cppreference.com/w/cpp/language/reference"
  />
  <ReferenceItem
    :id="6"
    author="Brian W. Kernighan, Dennis M. Ritchie"
    title="The C Programming Language, 2nd Edition"
    :year="1988"
    chapter="The original definition of the lvalue"
  />
</ReferenceCard>
