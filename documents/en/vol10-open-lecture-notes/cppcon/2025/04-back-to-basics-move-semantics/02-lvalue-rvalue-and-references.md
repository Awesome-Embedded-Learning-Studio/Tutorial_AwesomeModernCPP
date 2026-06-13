---
title: 'Lvalues, Rvalues, and References: The Type System Foundation of Move Semantics'
description: CppCon 2025 talk notes — from the K&R definition of lvalues and rvalues
  to the C++11 value category system, with a detailed look at lvalue reference, `const`
  reference binding rules, and rvalue references
conference: cppcon
conference_year: 2025
talk_title: 'Back to Basics: Move Semantics'
speaker: Ben Saks
video_bilibili: https://www.bilibili.com/video/BV1X54y1P7uM
video_youtube: https://www.youtube.com/watch?v=szU5b972F7E
tags:
- cpp-modern
- host
- beginner
difficulty: beginner
platform: host
cpp_standard:
- 11
- 17
- 20
chapter: 4
order: 2
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/04-back-to-basics-move-semantics/02-lvalue-rvalue-and-references.md
  source_hash: ac81d12b8e0b09e31f41614f4a56bc363b1fdf1298f7c85f642befb8254cd351
  translated_at: '2026-06-13T02:16:38.094525+00:00'
  engine: anthropic
  token_count: 3726
---
# Lvalues, Rvalues, and References: The Type System Foundations of Move Semantics

:::tip
This article is an in-depth adaptation of Ben Saks's "Back to Basics: Move Semantics" talk at CppCon 2025. Above is the YouTube link; users in China can watch the Bilibili version instead. The experimental environment for this article is Arch Linux WSL, GCC 16.1.1.
:::

In the previous article, we used MyString experiments to prove that move semantics can reduce a heap allocation + memcpy to a single pointer assignment, speeding up 100,000 concatenations by more than 4x. The conclusion was exciting, but we left a key question hanging at the end—how does the compiler know when it's safe to "steal" resources? It needs a language mechanism to distinguish between "this object will still be used" and "this object is about to die." That distinction mechanism is lvalues and rvalues.

Honestly, I used to have a vague sense of dread about "lvalues and rvalues." The first time I heard those two terms, my instinctive reaction was: "Isn't this just the left side and right side of the equals sign?"—and then I quickly realized things weren't that simple. The `x` in `const int x = 10;` is an lvalue, but you can't assign to it; `int&& r = 10;` is clearly bound to an rvalue, but `r` itself is an lvalue... These seemingly contradictory phenomena took me a while to fully figure out.

## K&R's Original Definition: Left and Right of the Equals Sign

The terms lvalue and rvalue trace back to the birth of the C language. K&R introduced these concepts in *The C Programming Language*—the "L" in "L value" comes from the assignment expression `E1 = E2`, where the thing on the **Left** of the assignment operator must have certain specific properties. Specifically, `E1` must be an expression that can be located—the compiler must be able to determine its position in memory so it can write the value of `E2` into it.

This is the most primitive intuition: **lvalue = something that can appear on the left side of an assignment**.

Take the simplest example:

```cpp
int n = 1;   // OK: n 是左值，1 是右值
n = 2;       // OK: n 是左值，可以出现在赋值左边
// 1 = n;    // 错误！1 是右值，不能出现在赋值左边
```

`n` is a named variable; it has a definite location in memory, the compiler knows its address, so a value can be written into it. But the literals `1` and `2`—they are pure values, the compiler doesn't allocate a writable memory address for them. You can't tell the compiler "please write n's value into the number 1," because the number 1 simply doesn't have an "inside."

This is the first level of understanding lvalues and rvalues. At this level, everything looks fine—lvalues are "things with an address that can be assigned to," and rvalues are "things without an address that can't be assigned to."

But wait—have you noticed that this definition carries an implicit assumption? It assumes that "can appear on the left side of an assignment" and "has a memory address" are the same thing. In the very early days of C, this assumption basically held. But C soon introduced `const`, and C++ introduced references, class types, temporary objects... As the language grew more complex, this assumption started to fall apart. Next, we'll see how this crack appeared, and why understanding it is crucial for move semantics.

## Basic Classification: Literals and Named Variables

Before we start patching those cracks, let's get the basic classification straight, because these rules haven't changed from the C era to today.

**Literals are rvalues.** Integer literals like `3`, floating-point literals like `3.14`, character literals like `'a'`, enumeration constants—they are all rvalues. They have no memory address (at least not from the programmer's perspective), you can't assign to them, they are simply "values" themselves.

**Named variables are lvalues.** `int n;` declares a variable `n` that has a location in memory; you can both read from and write to it. The key point is: an lvalue can appear on **either side** of an assignment expression. In `n = 1`, `n` is on the left (being written to); in `m = n`, `n` is on the right (being read). But what happens when `n` is on the right? It gets read—the compiler extracts the value stored at `n`'s memory location. This "read" operation has a formal name: **lvalue-to-rvalue conversion**<RefLink :id="1" preview="C++ Standard, [conv.lval] — 左值到右值转换的标准描述" />.

This conversion is almost everywhere, we just don't usually notice it. Whenever you write `int b = a;`, `a` is an lvalue, but to assign it to `b`, the compiler must first read out the value stored in `a`—this step is the lvalue-to-rvalue conversion. Understanding that this conversion exists is important because it explains a subtle fact: **lvalues and rvalues are not two kinds of "things," but two "properties" of expressions**. The same variable `a` can exhibit lvalue properties or rvalue properties in different contexts.

## const Objects: The First Crack in K&R's Definition

Now here's the problem. Look at this code:

```cpp
const int max = 100;
// max = 200;    // 错误！max 是 const，不能赋值
printf("&max = %p\n", (void*)&max);  // 但 max 有地址！
```

`max` is a const object. You can't assign to it—`max = 200` is a compiler error. According to K&R's definition of "lvalue = can appear on the left side of an assignment," `max` shouldn't be an lvalue. But in reality, `max` does have a memory address; you can take its pointer (`&max` is legal), and you can read its value through that pointer.

This is the crack in K&R's definition: **const objects are lvalues, but are not assignable**. The standard terminology calls them "non-modifiable lvalues."

This distinction is very important because it reveals the true core of the lvalue concept—**having an address**, not **being assignable**. A `const int` object has an address but is not assignable; an integer literal `3` has neither an address nor is it assignable. The former is a non-modifiable lvalue, the latter is an rvalue. The key to distinguishing them isn't "can you assign to it," but "does it have a persistent memory location."

The actual output from GCC 16.1.1 confirms this:

```text
max = 100
&max = 0x7ffc47a05dc8
```

`&max` prints a valid stack address—this const object genuinely exists in memory.

We can draw a comparison here to deepen our understanding. The `max` in `const int max = 100;` is a non-modifiable lvalue: it has an address, you can't assign to it, but you can take its address and read through a pointer. The literal `100` is an rvalue: it has no address, and you can't assign to it either. What they share is "not assignable," but the crucial difference lies in "having a persistent memory location." This difference becomes very important when we get to class types and reference binding—because the compiler uses "having a persistent location" to decide which references can bind to which expressions.

## Class-Type Rvalues: Can Call Member Functions

The distinction between lvalues and rvalues gets more interesting with class types. Consider a simple struct:

```cpp
struct Widget
{
    int value;
    void f()
    {
        // this 指向调用对象的地址
        printf("Widget::f(), value = %d, this = %p\n", value, (void*)this);
    }
};
```

We have two ways to get a class-type rvalue. The first is a function return value: a function that returns a `Widget` by value has a class rvalue as its return value. The second is functional-style cast: `Widget(7)` converts the integer 7 into a temporary object of type `Widget`, which is also a class rvalue.

The interesting part is: **you can call member functions on a class rvalue**.

```cpp
Widget(7).f();       // OK！在临时 Widget 上调用 f()
make_widget(42).f(); // OK！在函数返回的临时对象上调用 f()
```

This seems a bit strange—isn't an rvalue something "without an address"? How can you call a member function on something without an address? The answer is that the compiler does something behind the scenes: it allocates a location in memory for this temporary object—the standard calls this process **temporary materialization conversion**<RefLink :id="2" preview="C++ Standard, [conv.rval] — 临时实体化转换" />. The `this` pointer points to that temporarily allocated memory location.

I ran this on GCC 16.1.1, and the results are quite interesting:

```text
Widget::f(), value = 7, this = 0x7ffc9a466b04
Widget::f(), value = 42, this = 0x7ffc9a466b04
```

Notice—the `this` addresses from both calls are exactly the same! This is because the compiler applied NRVO (Named Return Value Optimization), placing the temporary object returned by `make_widget` directly in the caller's stack space, and the temporary object for `Widget(7)` happened to be allocated in the same region. These temporary objects have short lifetimes, but they do have real memory locations while they're alive.

:::warning The version history of temporary materialization—two things need to be distinguished here
Saying "rvalues have no address" isn't quite accurate. The precise statement is—an rvalue **doesn't need** to have an address; it is not a persistent memory location. But if the compiler temporarily allocates a block of memory for it to implement some operation (like calling a member function, or binding to a reference), then in that instant it "has an address." This process of the compiler implicitly allocating memory is temporary materialization.

As for its version history, we need to separate two things: the lvalue / xvalue / prvalue **value category triad** was indeed introduced in C++11; but "**temporary materialization conversion**" as a named standard conversion was only formally established in **C++17**. It was written into the language rules alongside C++17's mandatory copy elision (proposal P0135), with the core idea being: **a prvalue isn't necessarily an object itself; it only "materializes" into a temporary object when it needs to be used as one (like calling a member function, or binding to a reference)**. In the C++11 era, this mechanism was still gestating and hadn't been formally named. So strictly speaking, the temporary materialization in `Widget(7).f()` above is standard semantics only from C++17 onward—don't conflate it with C++11's value category triad.
:::

:::warning
Class rvalues being able to call member functions is the foundation of move semantics. Move constructors and move assignment operators are essentially "member functions called on temporary objects about to be destroyed"—through rvalue references, we gain the ability to modify these temporary objects.
:::

## Lvalue References: The First Rule of Binding

Now we enter the world of references. Before C++11 introduced rvalue references, what C++ called a "reference" was what we now formally call an "lvalue reference."

"An lvalue reference to T must bind to a T-type lvalue"—this sentence sounds convoluted, but the meaning is simple. A reference of type `int&` can only bind to an lvalue of type `int`:

```cpp
int n = 10;
int& ri = n;       // OK: ri 绑定到左值 n
// int& ri2 = 10;  // 错误！不能把左值引用绑定到右值（字面量）
```

Why is `int& ri = 10` an error? Because `10` is an rvalue; it has no persistent memory location. A reference needs to know the address of what it's referencing, but an rvalue has no address—hence the contradiction.

But there's a very important exception here: **a const lvalue reference can bind to an rvalue**.

```cpp
const int& cri = 10;    // OK！const 引用可以绑定到右值
const int& cri2 = 3.14; // OK！甚至可以绑定到不同类型（double -> int 转换）
```

The mechanism behind this is: the compiler quietly creates a temporary `int` object to store that value (or the converted value), and then lets the const reference bind to this temporary object. For `const int& cri2 = 3.14;`, the compiler first does the conversion from `double` to `int` (3.14 becomes 3), creates a temporary `int` holding 3, and then `cri2` binds to this temporary object. That's why I saw `const lvalue ref to converted: 3` in the GCC output—3.14 was truncated.

You might ask: why must it be `const`? Because if you allowed a non-const reference to bind to an rvalue, you could modify a temporary object through that reference—and that temporary object might be destroyed immediately, making the modification pointless and prone to bugs. A const reference binding to a temporary object means you can only read it, not modify it, so it's safe.

This rule has another important corollary: **a const reference extends the lifetime of a temporary object**. Normally, the temporary object in `Widget(7).f()` would be destroyed after the statement ends. But if a const reference binds to it, the temporary object's lifetime is extended to be as long as the reference.

Here's a concrete example to show how important this is. Suppose you wrote a function that returns a `std::string`, and you receive it with a const reference:

```cpp
std::string get_name() { return "hello"; }

const std::string& name = get_name();
// name 在这里仍然有效！临时对象的生命周期被延长了
printf("%s\n", name.c_str());  // 安全
```

Without the const reference's lifetime extension rule, the temporary `std::string` returned by `get_name()` would be destroyed after the statement ends, and `name` would become a dangling reference. But because `const std::string&` binds to this temporary object, the compiler guarantees the temporary lives at least until `name` goes out of scope.

There's a subtle pitfall here, though—only the "first" reference that directly binds to the temporary object extends its lifetime; indirect binding through a reference chain doesn't count. For example, in `const std::string& r2 = name;`, `r2` binds to `name` (an lvalue), which doesn't involve a temporary object, so there's no lifetime extension. But if you have a situation involving multiple levels of indirect binding to a temporary object, you need to be careful. We discuss this in more detail in vol2's [Rvalue References: From Copy to Move](../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md).

:::warning
Note: An rvalue reference `T&&` also has the effect of extending a temporary object's lifetime. `std::string&& r = get_name();` will also keep the returned temporary object alive until `r` goes out of scope. This is a commonality between rvalue references and const lvalue references—they can both bind to temporary objects and extend their lifetimes. The difference is that an rvalue reference allows you to modify the temporary object, while a const lvalue reference does not.
:::

## Rvalue References: Born for Move Semantics

C++11 introduced a new reference type—the rvalue reference, denoted with double `&&` syntax.

```cpp
int&& ri = 10;     // OK: 右值引用绑定到右值（字面量 10）
// int&& ri2 = n;  // 错误！右值引用不能绑定到左值
```

The binding rules for rvalue references are the "reverse" of lvalue references: `int&&` can only bind to an rvalue of type `int`. `int&& ri2 = n` is a compiler error because `n` is an lvalue.

:::warning
Even `const int&&` can only bind to rvalues—adding const to an rvalue reference doesn't suddenly let it bind to lvalues. This point is often confused. const rvalue references are almost never seen in practice, and the standard library has virtually no use cases for them, but they do exist.
:::

What's the actual use of rvalue references? The key point is this: **through an rvalue reference, we can modify temporary objects**.

```cpp
int&& ri = 10;  // 编译器为字面量 10 创建一个临时 int 对象
ri = 20;        // OK！我们修改了这个临时对象
```

For simple types like `int`, this has no practical significance. But when we talk about class types—imagine a `MyString&&` that binds to a temporary `MyString` object, and that temporary object internally has a dynamically allocated character array. Through this rvalue reference, we can directly "steal" the pointer to that array, set the temporary object's pointer to `nullptr`, and then let the temporary object's destructor do nothing.

This is exactly what the signatures of move constructors and move assignment operators express: they receive parameters through rvalue references, telling the compiler "I know this is a temporary object, and I can safely steal its resources." But that's the topic for the next article; let's first finish completing our understanding of the reference system.

You might also ask a more fundamental question: why did C++11 introduce an entirely new reference type to do this? Why not just reuse lvalue references? The answer is: if the move constructor's signature were `MyString(MyString& s)`, it would create ambiguity with the copy constructor `MyString(const MyString& s)`—actually, no, it wouldn't be ambiguous because the const is different. But the real problem is: if a function accepts both `MyString&` and `const MyString&`, when the compiler sees `s1 + s2` (an rvalue), it can't find a matching non-const lvalue reference to bind to it, so it still can't trigger a "move." Rvalue references fill this gap: they're specifically designed to bind to rvalues, and their binding rules don't overlap with lvalue references, so overload resolution can automatically distinguish between "this is a persistent object (copy it)" and "this is a temporary object (steal its resources)."

## C++11's Value Category System: lvalue, xvalue, prvalue

So far I've been talking about just two categories, "lvalue" and "rvalue," as if the whole world were black and white. But in reality, to support move semantics, C++11 expanded the value category system from binary to ternary.

Before C++11, every expression was either an lvalue or an rvalue—simple as that. But C++11 introduced a third category: **xvalue (expiring value)**. An xvalue represents "this object is about to expire, and its resources can be moved."

The new classification system works like this. First, all expressions are categorized along two dimensions: "has identity" (can determine a memory location) and "can be moved":

| Category | Has Identity | Can Be Moved | Examples |
|------|:--------:|:----------:|------|
| **lvalue** | Yes | No | Named variable `n`, `*p`, `++i` |
| **xvalue** | Yes | Yes | Result of `std::move(n)` |
| **prvalue** | No | Yes | Literal `42`, `Widget(7)`, temporary object returned by a function |

Then there are two composite concepts: **glvalue** (generalized lvalue) = lvalue + xvalue, **rvalue** = xvalue + prvalue. Represented as a diagram:

```text
            表达式
           /      \
      glvalue    rvalue
      /     \    /    \
  lvalue   xvalue   prvalue
```

- **lvalue**: Has identity, cannot be moved—ordinary named variables.
- **xvalue**: Has identity, can be moved—the return value of `std::move(x)`. It has a name (or rather, a definite memory location), but the compiler is told "you can move its resources away."
- **prvalue** (pure rvalue): No identity, can be moved—pure temporary values, like literals and temporary objects returned by functions.

This system looks considerably more complex than the binary classification, but its design logic is clear: move semantics needs a mechanism to express "this thing's resources can be stolen," and xvalue is that bridge. What `std::move` essentially does is convert an lvalue into an xvalue, telling the compiler "although this object still has a name, you can move its resources away."

### Value Categories of Common Expressions

Looking at just the definitions might still feel abstract, so let's list the most common expressions we use in daily coding and mark which category each belongs to:

| Expression | Value Category | Reason |
|--------|--------|------|
| `n` (named variable) | lvalue | Has a name, has a definite memory location |
| `*p` (dereference) | lvalue | The object pointed to has a memory location |
| `++i` (pre-increment) | lvalue | Returns the modified `i` itself |
| `i++` (post-increment) | prvalue | Returns a copy of the old value, a temporary |
| `42` (integer literal) | prvalue | Pure value with no memory location |
| `"hello"` (string literal) | lvalue | String literals are const char arrays with an address |
| `Widget(7)` (functional-style cast) | prvalue | Creates a temporary Widget object |
| `make_widget()` (return by value) | prvalue | Temporary value returned by a function |
| `std::move(n)` | xvalue | Explicitly converts an lvalue to a "movable" state |
| `a.m` (member access, a is lvalue) | lvalue | Follows the identity property of `a` |
| `std::move(a).m` (member access, a is xvalue) | xvalue | Follows the xvalue property of `a` |

A few points are worth special attention. The string literal `"hello"` is an lvalue, which often surprises people—it's actually an array of type `const char[6]`, stored in the program's read-only data segment, has a definite address, and is therefore an lvalue. Post-increment `++` returns a copy of the old value (a temporary), so it's a prvalue; while pre-increment `++` returns the modified object itself, so it's an lvalue. The value category of the member access expression `a.m` follows the value category of `a`—if `a` is an lvalue, `a.m` is an lvalue; if `a` is an xvalue, `a.m` is an xvalue.

## Verifying Value Categories with the Compiler

We've discussed a lot of theory; now let's actually verify things using `decltype` and type traits. `decltype` has a useful property: when applied to a **parenthesized** variable name `decltype((x))`, it gives different types depending on the expression's value category—lvalues yield `T&`, xvalues yield `T&&`, and prvalues yield `T`.

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

    printf("decltype((n)):\n");          // n 是 lvalue
    print_category<decltype((n))>();     // int& → lvalue ref: yes

    printf("decltype(10):\n");           // 10 是 prvalue
    print_category<decltype(10)>();      // int → 都不是引用

    printf("decltype(std::move(n)):\n"); // std::move(n) 是 xvalue
    print_category<decltype(std::move(n))>(); // int&& → rvalue ref: yes

    return 0;
}
```

The output from GCC 16.1.1 perfectly confirms the theory:

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

`decltype((n))` yields `int&` because `(n)` is an lvalue expression. `decltype(10)` yields `int` (the bare type) because `10` is a prvalue. `decltype(std::move(n))` yields `int&&` because the return value of `std::move` is an xvalue, and xvalues manifest as `T&&` in `decltype`.

## "If It Has a Name, It's an Lvalue"—The Trap of Rvalue Reference Parameters

Now it's time to talk about a pitfall that almost every C++ newcomer falls into. Ben Saks specifically emphasized this rule in his talk: **if something has a name, it's an lvalue**.

Consider a function that receives an rvalue reference:

```cpp
void process(MyString&& s)
{
    // 在这里，s 是左值还是右值？
}
```

From outside the function, when you call `process(s1 + s2)`, `s1 + s2` is an rvalue, so this call is fine—an rvalue reference can bind to an rvalue. But **inside** the function, the parameter `s` has a name. It's a named object. According to the "if it has a name, it's an lvalue" rule, **within the function body, `s` is treated as an lvalue**.

What does this mean? If you want to move resources from `s` again inside the function body, you can't do it directly—the compiler will treat `s` as an lvalue and choose copy instead of move. You must explicitly use `std::move(s)` to tell the compiler "I know what I'm doing, please treat it as an rvalue."

```cpp
void process(MyString&& s)
{
    MyString copy(s);            // 拷贝！因为 s 在这里是左值
    MyString moved(std::move(s)); // 移动！std::move 把 s 转为右值
}
```

The logic behind this rule is actually quite reasonable: the function body might have many lines of code, and `s` might still be used on line ten after being moved on line one. The compiler can't assume "you only use it on the last line," so it chooses the conservative strategy—things with names aren't automatically moved; you must explicitly authorize it.

:::tip
This "name = lvalue" rule can be verified with `decltype`. If you write `decltype((s))` in a function template, when `s`'s declared type is `MyString&&`, `decltype((s))` will still yield `MyString&` (lvalue reference), not `MyString&&`. Because the parenthesized `decltype` looks at the expression's value category, and `s` as a named object has the value category lvalue. This is often used to set traps in interview questions.
:::

:::tip
This "if it has a name, it's an lvalue" rule has one important exception: **return statements**. The `s` in `return s;` has a name, but since C++11 it's treated as an "implicitly movable entity," and the compiler can directly move from it without you needing to write `std::move(s)`. And in fact, the compiler might do even better—eliminating the copy entirely through NRVO. We'll save the full discussion of this topic for the next article.
:::

## Reference Binding Rules Cheat Sheet

Let's organize all the reference binding rules covered in this article into a single table for easy reference:

| Reference Type | Can Bind to lvalue? | Can Bind to rvalue? | Can Bind to Different Type? | Can Modify Referenced Object? |
|----------|:-----------------:|:-----------------:|:------------------:|:-----------------:|
| `T&` | Yes | **No** | No | Yes |
| `const T&` | Yes | **Yes** | Yes (with conversion) | No |
| `T&&` | **No** | Yes | No | Yes |
| `const T&&` | **No** | Yes | No | No |

This table packs in a lot of information, but a few key conclusions are worth remembering. First, `const T&` is a "universal receiver"—it can bind to almost anything (lvalue, rvalue, even different types), at the cost of not being able to modify the referenced object through it. Second, `T&&` only binds to rvalues, which is exactly what move semantics needs: it guarantees that what's bound is always an object whose "resources can be safely stolen." Third, `const T&&` exists but is virtually useless—it can bind to rvalues but can't modify them, which loses the core advantage of rvalue references: "allowing modification of temporary objects."

## What We've Figured Out So Far

In this article, starting from K&R's "left side of the equals sign," we step by step built the complete picture of C++ value categories. We saw how const objects broke the old definition of "lvalue = assignable," how class rvalues gain memory locations through temporary materialization, how lvalue references and rvalue references have starkly different binding rules, and finally how we found the theoretical foundation for move semantics in C++11's lvalue/xvalue/prvalue ternary system.

The core takeaways are two: first, an rvalue reference `T&&` only binds to rvalues, which gives the compiler a natural signal—"the thing bound to it is temporary, and its resources can be safely stolen." Second, the "if it has a name, it's an lvalue" rule means we sometimes need `std::move` to explicitly tell the compiler "please allow moving."

Looking back, the distinction between lvalues and rvalues wasn't invented out of thin air by C++11—it has existed since the C language era, just in a much simpler form. C++ introduced const, class types, references, operator overloading, and each step made the boundaries of value categories more blurred, until move semantics needed a precise mechanism to distinguish "persistent" from "temporary" objects, and C++11 finally formalized this system into the three-level classification of lvalue/xvalue/prvalue. Understanding the evolutionary logic of this system will make learning `std::move`, move constructors, perfect forwarding, and other concepts much smoother—because their designs are all responding to the same question: "How does the compiler know whether this object can be safely moved?"

With this theoretical foundation, in the next article we can move into practice—implementing a move constructor and move assignment operator for MyString, seeing exactly how `std::move` works, and under what conditions copy elision lets us skip moving entirely.

If you want a more systematic explanation of rvalue references, vol2's [Rvalue References: From Copy to Move](../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md) is excellent supplementary material.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [conv.lval] — Lvalue-to-rvalue conversion"
    :year="2020"
    chapter="Standard description of lvalue-to-rvalue conversion"
  />
  <ReferenceItem
    :id="2"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [conv.rval] — Temporary materialization conversion"
    :year="2020"
    chapter="Standard description of temporary materialization conversion"
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
    chapter="Original definition of lvalue"
  />
</ReferenceCard>
