---
title: 'Lvalues, Rvalues, and References: Type System Foundations of Move Semantics'
description: CppCon 2025 Talk Notes — From K&R's lvalue and rvalue definitions to
  the C++11 value category system, a detailed explanation of lvalue references, const
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
  source_hash: 4f05a25d8d24a6f4994777434ed41472bf0ffa294ea7d73c1723ab0624097041
  translated_at: '2026-06-14T00:17:27.683604+00:00'
  engine: anthropic
  token_count: 3728
---
# Lvalues, Rvalues, and References: The Type System Foundations of Move Semantics

:::tip
This article is based on a deep dive into Ben Saks' "Back to Basics: Move Semantics" talk from CppCon 2025. Above is the YouTube link; users in China can watch the Bilibili version. The experimental environment for this article is Arch Linux WSL with GCC 16.1.1.
:::

In the previous post, we used experiments with `MyString` to prove that move semantics can reduce a heap allocation plus `memcpy` to a single pointer assignment, speeding up string concatenation by over 4x in 100,000 operations. The conclusion was exciting, but we left a crucial suspense at the end—how does the compiler know when it is safe to "steal" resources? It needs a language mechanism to distinguish between "this object will still be used" and "this object is about to die." This distinction mechanism is lvalues and rvalues.

Honestly, I used to have a vague fear of "lvalues and rvalues." When I first heard these two terms, my instinctive reaction was: "Isn't this just the left and right side of the equal sign?"—and then I quickly realized things weren't that simple. `const int` in ``const int x = 10;`` is an lvalue, but you cannot assign to it; an ``x`` binds to an rvalue, but the ``int&& r = 10;`` itself is an lvalue... These seemingly contradictory phenomena took me a while to fully figure out.

## K&R's Original Definition: Left and Right of the Equal Sign

The terms lvalue and rvalue can be traced back to the birth of the C language. K&R introduced these concepts in *The C Programming Language*—"L value" comes from the assignment expression ``r``, where the thing on the **Left** of the assignment sign must have certain specific attributes. Specifically, ``E1 = E2`` must be an expression that can be located—the compiler must be able to determine its position in memory to write the value of ``E1`` into it.

This is the most primitive intuition: **lvalue = something that can appear on the left side of an assignment**.

Take the simplest example:

```cpp
int n = 1;   // OK: n 是左值，1 是右值
n = 2;       // OK: n 是左值，可以出现在赋值左边
// 1 = n;    // 错误！1 是右值，不能出现在赋值左边
```

``E2`` is a named variable; it has a definite location in memory, the compiler knows its address, so it can write a value into it. Literals like ``n`` and ``1``—they are pure values, the compiler doesn't allocate a memory address you can write to for them. You can't tell the compiler "please write n's value into the number 1," because the number 1 doesn't even have the concept of "inside."

This is the first level of understanding lvalues and rvalues. At this level, everything looks good—lvalues are things "with an address, can be assigned," rvalues are things "without an address, cannot be assigned."

But wait—do you feel this definition implies an assumption? It assumes "can appear on the left of an assignment" and "has a memory address" are the same thing. In the very early days of C, this assumption basically held. But C quickly introduced ``2``, and C++ introduced references, class types, temporary objects... As the language became more complex, this assumption started to falter. Next, we will see how this crack appeared and why understanding it is crucial for move semantics.

## Basic Classification: Literals and Named Variables

Before patching those cracks, let's get the basic classifications clear, as these rules haven't changed since the C era.

**Literals are rvalues.** Integer literals ``const``, floating-point literals ``3``, character literals ``3.14``, enumeration constants—they are all rvalues. They have no memory address (at least not from the programmer's perspective), you cannot assign to them, they are just "values" themselves.

**Named variables are lvalues.** ``'a'`` declares a variable ``int n;``, it has a location in memory, you can both read and write to it. A key point is: an lvalue can appear on **either side** of an assignment expression. In ``n``, ``n = 1`` is on the left (being written); in ``n``, ``m = n`` is on the right (being read). But what happens when ``n`` is on the right? It is read—the compiler retrieves the value stored at the memory location of ``n``. This "read" operation has a formal name: **lvalue-to-rvalue conversion**<RefLink :id="1" preview="C++ Standard, [conv.lval] — Standard description of lvalue-to-rvalue conversion" />.

This conversion is almost everywhere, we just don't usually realize it. Every time you write ``n``, ``int b = a;`` is an lvalue, but to assign it to ``a``, the compiler must first read the value stored by ``b``—this step is lvalue-to-rvalue conversion. Understanding the existence of this conversion is important because it explains a subtle fact: **lvalues and rvalues are not two "things," but two "properties" of expressions**. The same variable ``a`` can exhibit lvalue properties or rvalue properties in different contexts.

## const Objects: The First Crack in the K&R Definition

Now the problem arises. Let's look at this code:

```cpp
const int max = 100;
// max = 200;    // 错误！max 是 const，不能赋值
printf("&max = %p\n", (void*)&max);  // 但 max 有地址！
```

``a`` is a const object. You cannot assign to it—``max`` is a compiler error. According to K&R's definition of "lvalue = can appear on the left of an assignment," ``max = 200`` shouldn't be an lvalue. But actually, ``max`` does have a memory address; you can take its pointer (``max`` is legal), and you can read its value through the pointer.

This is the crack in the K&R definition: **const objects are lvalues, but not assignable**. The standard terminology calls them "non-modifiable lvalues."

This distinction is very important because it reveals the true core of the lvalue concept—**having an address**, not **being assignable**. A ``&max`` object has an address but is not assignable; an integer literal ``const int`` has neither an address nor is assignable. The former is a non-modifiable lvalue, the latter is an rvalue. The key to distinguishing them is not "can it be assigned," but "does it have a persistent memory location."

Actual results from GCC 16.1.1 confirm this:

```text
max = 100
&max = 0x7ffc47a05dc8
```

``3`` prints out a legal stack address—this const object genuinely exists in memory.

Here we can make a comparison to deepen understanding. ``&max``'s ``const int max = 100;`` is a non-modifiable lvalue: it has an address, you can't assign to it, but you can take the address and read through a pointer. The literal ``max`` is an rvalue: it has no address, and you can't assign to it. The commonality is "cannot be assigned," but the key difference lies in "having a persistent memory location." This difference becomes very important when we get to class types and reference binding—because the compiler decides which references can bind to which expressions based on "whether there is a persistent location."

## Rvalues of Class Types: Can Call Member Functions

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

We have two ways to obtain an rvalue of class type. The first is a function return value: a function returning ``Widget`` by value has a return value that is a class rvalue. The second is functional cast: ``Widget(7)`` converts the integer 7 into a temporary object of type ``Widget``, which is also a class rvalue.

The interesting part is: **you can call member functions on class rvalues**.

```cpp
Widget(7).f();       // OK！在临时 Widget 上调用 f()
make_widget(42).f(); // OK！在函数返回的临时对象上调用 f()
```

This looks a bit strange—don't rvalues "have no address"? How can you call a member function on something without an address? The answer is that the compiler does something behind the scenes: it allocates a location in memory for this temporary object—the standard calls this process **temporary materialization conversion**<RefLink :id="2" preview="C++ Standard, [conv.rval] — Temporary materialization conversion" />. The ``this`` pointer points to that temporarily allocated memory location.

I ran this on GCC 16.1.1, and the results were interesting:

```text
Widget::f(), value = 7, this = 0x7ffc9a466b04
Widget::f(), value = 42, this = 0x7ffc9a466b04
```

Notice—the ``this`` addresses of the two calls are exactly the same! This is because the compiler performed NRVO (Named Return Value Optimization), placing the temporary object returned by ``make_widget`` directly in the caller's stack space, and the temporary object for ``Widget(7)`` happened to be allocated in the same area. Although these temporary objects have short lifecycles, they do possess real memory locations while alive.

:::warning The origin of temporary materialization, distinguish two things here
Saying "rvalues have no address" isn't quite accurate. The accurate way to put it is—an rvalue **doesn't need** an address; it is not a persistent memory location. But if the compiler temporarily allocates a block of memory for it to implement an operation (like calling a member function, binding to a reference), then at that instant it "has an address." This process of implicitly allocating memory by the compiler is temporary materialization.

Regarding its origin, we need to separate two things: the **value category triad** of lvalue / xvalue / prvalue was indeed introduced in C++11; but "**temporary materialization conversion**" as a named standard conversion was only formally established in **C++17**. It was written into the language rules alongside C++17's mandatory copy elision (proposal P0135), with the core idea being: **a prvalue itself isn't necessarily an object; only when it is needed as an object (e.g., calling a member function, binding to a reference) is it "materialized" into a temporary object**. In the C++11 era, this mechanism was still brewing and hadn't been formally named. So strictly speaking, the temporary materialization in ``Widget(7).f()`` above is standard semantics from C++17 onwards—don't confuse it with the C++11 value category triad.
:::

:::warning
Class rvalues can call member functions; this feature is the foundation of move semantics. Move constructors and move assignment operators are essentially "member functions called on temporary objects about to die"—through rvalue references, we gain the ability to modify these temporary objects.
:::

## Lvalue References: The First Binding Rule

Now we enter the world of references. Before C++11 introduced rvalue references, what C++ called "references" is what we now formally call "lvalue references."

"A lvalue reference to T must bind to a T lvalue"—this sounds convoluted, but the meaning is simple. A reference of type ``int&`` can only bind to an lvalue of type ``int``:

```cpp
int n = 10;
int& ri = n;       // OK: ri 绑定到左值 n
// int& ri2 = 10;  // 错误！不能把左值引用绑定到右值（字面量）
```

Why is ``int& ri = 10`` an error? Because ``10`` is an rvalue; it has no persistent memory location. A reference needs to know the address of the thing it references, but an rvalue has no address—this is a contradiction.

But there is a very important exception here: **a const lvalue reference can bind to an rvalue**.

```cpp
const int& cri = 10;    // OK！const 引用可以绑定到右值
const int& cri2 = 3.14; // OK！甚至可以绑定到不同类型（double -> int 转换）
```

The mechanism behind this is: the compiler quietly creates a temporary ``int`` object to store that value (or converted value), and then lets the const reference bind to this temporary object. For ``const int& cri2 = 3.14;``, the compiler first performs the conversion from ``double`` to ``int`` (3.14 becomes 3), creates a temporary ``int`` holding 3, and then ``cri2`` binds to this temporary object. This is why I saw ``const lvalue ref to converted: 3`` in the GCC output—3.14 was truncated.

You might ask: why must it be ``const``? Because if non-const references were allowed to bind to rvalues, you could modify a temporary object through that reference—and that temporary object might be destroyed immediately, modifying it is meaningless and prone to bugs. A const reference binds to a temporary object; you can only read it, not modify it, so it is safe.

This rule has an important corollary: **const references extend the lifetime of temporary objects**. Normally, the temporary object in ``Widget(7).f()`` is destroyed after the statement ends. But if a const reference binds to it, the temporary object's lifetime is extended to be as long as the reference.

Let's take a concrete example to show how important this is. Suppose you write a function that returns ``std::string`` and receive it with a const reference:

```cpp
std::string get_name() { return "hello"; }

const std::string& name = get_name();
// name 在这里仍然有效！临时对象的生命周期被延长了
printf("%s\n", name.c_str());  // 安全
```

Without the const reference lifetime extension rule, the temporary ``get_name()`` returned by ``std::string`` would be destroyed after the statement ends, and ``name`` would become a dangling reference. But because ``const std::string&`` binds to this temporary object, the compiler guarantees the temporary object lives at least until ``name`` leaves scope.

However, there is a subtle pitfall here—only the "first" reference that directly binds to the temporary object extends its lifetime; indirect binding through a reference chain doesn't count. For example, in ``const std::string& r2 = name;``, ``r2`` binds to ``name`` (an lvalue), which doesn't involve a temporary object, so there is no lifetime extension. But if multi-level indirect binding to temporary objects is involved, be careful. We have a more detailed discussion in vol2's [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md).

:::warning
Note: Rvalue references ``T&&`` also have the effect of extending temporary object lifetime. ``std::string&& r = get_name();`` will also keep the returned temporary object alive until ``r`` leaves scope. This is a commonality between rvalue references and const lvalue references—they can both bind to temporary objects and extend their lifetime. The difference is that rvalue references allow you to modify the temporary object, while const lvalue references do not.
:::

## Rvalue References: Born for Move Semantics

C++11 introduced a new reference type—the rvalue reference, denoted by the double ``&&`` syntax.

```cpp
int&& ri = 10;     // OK: 右值引用绑定到右值（字面量 10）
// int&& ri2 = n;  // 错误！右值引用不能绑定到左值
```

The binding rules for rvalue references are exactly the "reverse" of lvalue references: ``int&&`` can only bind to an rvalue of type ``int``. ``int&& ri2 = n`` is a compiler error because ``n`` is an lvalue.

:::warning
Even ``const int&&`` can only bind to rvalues—adding const to an rvalue reference doesn't suddenly make it able to bind to lvalues. This is often confused. const rvalue references are rarely seen in practice; the standard library almost never uses them, but they do exist.
:::

What is the use of rvalue references? The key lies in this: **through an rvalue reference, we can modify temporary objects**.

```cpp
int&& ri = 10;  // 编译器为字面量 10 创建一个临时 int 对象
ri = 20;        // OK！我们修改了这个临时对象
```

For simple types like ``int``, this has no practical meaning. But when we discuss class types—imagine ``MyString&&``, it binds to a temporary ``MyString`` object, and that temporary object has a dynamically allocated character array inside. Through this rvalue reference, we can directly "steal" the pointer to that array, set the temporary object's pointer to ``nullptr``, and then let the temporary object's destructor do nothing.

This is exactly what the signatures of move constructors and move assignment operators express: they receive parameters via rvalue references, telling the compiler "I know this is a temporary object, I can safely steal its resources." But that's for the next post; let's finish the reference system first.

You might also ask a more fundamental question: why did C++11 introduce a brand new reference type to do this? Why not reuse lvalue references? The answer is: if the move constructor signature were ``MyString(MyString& s)``, it would be ambiguous with the copy constructor ``MyString(const MyString& s)``—no, actually it wouldn't be ambiguous because const is different. But the real problem is: if a function accepts both ``MyString&`` and ``const MyString&``, when the compiler sees ``s1 + s2`` (an rvalue), it can't find a matching non-const lvalue reference to bind to it, so it still can't trigger "move." Rvalue references fill this gap: they are specifically used to bind to rvalues, with binding rules that don't overlap with lvalue references, so overload resolution can automatically distinguish between "this is a persistent object (copy it)" and "this is a temporary object (steal its resources)."

## C++11 Value Category System: lvalue, xvalue, prvalue

So far I've been talking about the two categories of "lvalue" and "rvalue," as if the whole world were black and white. But actually, to support move semantics, C++11 expanded the value category system from binary to ternary.

Before C++11, every expression was either an lvalue or an rvalue—simple as that. But C++11 introduced a third category: **xvalue (expiring value)**. An xvalue represents "this object is about to die, its resources can be moved away."

The new classification system looks like this. First, all expressions are divided by two dimensions: "has identity" (identity, can determine memory location) and "can be moved":

| Category | Has Identity | Can be Moved | Example |
|------|:--------:|:----------:|------|
| **lvalue** | Yes | No | Named variable ``n``, ``*p``, ``++i`` |
| **xvalue** | Yes | Yes | Result of ``std::move(n)`` |
| **prvalue** | No | Yes | Literal ``42``, ``Widget(7)``, temporary object returned by function |

Then there are two combined concepts: **glvalue** (generalized lvalue) = lvalue + xvalue, **rvalue** = xvalue + prvalue. Here is a diagram:

```text
            表达式
           /      \
      glvalue    rvalue
      /     \    /    \
  lvalue   xvalue   prvalue
```

- **lvalue**: Has identity, cannot be moved—ordinary named variables.
- **xvalue**: Has identity, can be moved—the return value of ``std::move(x)``. It has a name (or a definite memory location), but the compiler is told "you can move its resources away."
- **prvalue** (pure rvalue): No identity, can be moved—pure temporary values, like literals and temporary objects returned by functions.

This system looks much more complex than the binary classification, but its design logic is clear: move semantics needs a mechanism to express "this thing's resources can be stolen," and xvalue is that bridge. ``std::move`` essentially converts an lvalue to an xvalue, telling the compiler "although this object still has a name, you can move its resources."

### Value Categories of Common Expressions

Just looking at definitions might still be abstract, so let's list the most common expressions we write in daily code and mark which category they belong to:

| Expression | Value Category | Reason |
|--------|--------|------|
| ``n`` (named variable) | lvalue | Has a name, has a definite memory location |
| ``*p`` (dereference) | lvalue | The object pointed to has a memory location |
| ``++i`` (pre-increment) | lvalue | Returns the modified ``i`` itself |
| ``i++`` (post-increment) | prvalue | Returns a copy of the old value, a temporary value |
| ``42`` (integer literal) | prvalue | Pure value without memory location |
| ``"hello"`` (string literal) | lvalue | String literal is a const char array, has an address |
| ``Widget(7)`` (functional cast) | prvalue | Creates a temporary Widget object |
| ``make_widget()`` (return by value) | prvalue | Temporary value returned by function |
| ``std::move(n)`` | xvalue | Explicitly converts lvalue to "movable" state |
| ``a.m`` (member access, a is lvalue) | lvalue | Follows ``a``'s identity property |
| ``std::move(a).m`` (member access, a is xvalue) | xvalue | Follows ``a``'s xvalue property |

There are a few points worth special attention. String literals ``"hello"`` are lvalues, which often surprises people—it is actually an array of type ``const char[6]``, stored in the read-only data segment of the program, has a definite address, so it is an lvalue. Postfix ``++`` returns a copy of the old value (a temporary value), so it is a prvalue; while prefix ``++`` returns the modified object itself, so it is an lvalue. The value category of the member access expression ``a.m`` follows the value category of ``a``—if ``a`` is an lvalue, ``a.m`` is an lvalue; if ``a`` is an xvalue, ``a.m`` is an xvalue.

## Verifying Value Categories with the Compiler

We've talked a lot about theory; let's use ``decltype`` and type traits to actually verify it. ``decltype`` has a useful feature: when applied to a **parenthesized** variable name ``decltype((x))``, it gives different types based on the expression's value category—lvalue gives ``T&``, xvalue gives ``T&&``, prvalue gives ``T``.

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

Output from GCC 16.1.1 perfectly confirms the theory:

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

``decltype((n))`` yields ``int&`` because ``(n)`` is an lvalue expression. ``decltype(10)`` yields ``int`` (bare type) because ``10`` is a prvalue. ``decltype(std::move(n))`` yields ``int&&`` because the return value of ``std::move`` is an xvalue, and an xvalue manifests as ``T&&`` in ``decltype``.

## "If it has a name, it's an lvalue"—The Trap of Rvalue Reference Parameters

Now we should talk about a pitfall almost every C++ newbie steps into. Ben Saks emphasized this rule in the talk: **if something has a name, it is an lvalue**.

Consider a function that takes an rvalue reference:

```cpp
void process(MyString&& s)
{
    // 在这里，s 是左值还是右值？
}
```

From the outside of the function, when you call ``process(s1 + s2)``, ``s1 + s2`` is an rvalue, so this call is fine—an rvalue reference can bind to an rvalue. But **inside** the function, the parameter ``s`` has a name. It is a named object. According to the "if it has a name, it's an lvalue" rule, **inside the function body, ``s`` is treated as an lvalue**.

What does this mean? If you want to move resources from ``s`` again inside the function body, you can't move directly—the compiler will treat ``s`` as an lvalue and choose copy instead of move. You must explicitly use ``std::move(s)`` to tell the compiler "I know what I'm doing, please treat it as an rvalue."

```cpp
void process(MyString&& s)
{
    MyString copy(s);            // 拷贝！因为 s 在这里是左值
    MyString moved(std::move(s)); // 移动！std::move 把 s 转为右值
}
```

The logic behind this rule is actually quite reasonable: the function body might have many lines of code; ``s`` might be used again on line ten after being moved on line one. The compiler can't assume "you only use it on the last line," so it chooses a conservative strategy—named objects aren't automatically moved; you must explicitly authorize it.

:::tip
This "name = lvalue" rule can be verified with ``decltype``. If you write ``decltype((s))`` in a function template, when ``s``'s declared type is ``MyString&&``, ``decltype((s))`` will still give ``MyString&`` (lvalue reference), not ``MyString&&``. Because parenthesized ``decltype`` looks at the expression's value category, and ``s`` as a named object has the value category lvalue. This is often used to dig traps in interview questions.
:::

:::tip
This "if it has a name, it's an lvalue" rule has an important exception: **return statements**. ``return s;``'s ``s`` has a name, but since C++11 it is considered an "implicitly movable entity," and the compiler can directly move it without you writing ``std::move(s)``. And actually, the compiler might do even better—eliminate the copy entirely via NRVO. We'll save the full discussion of this topic for the next post.
:::

## Reference Binding Rules Cheat Sheet

Let's organize all the reference binding rules covered in this post into a table for easy reference:

| Reference Type | Can bind to lvalue? | Can bind to rvalue? | Can bind to different type? | Can modify referenced object? |
|----------|:-----------------:|:-----------------:|:------------------:|:-----------------:|
| ``T&`` | Yes | **No** | No | Yes |
| ``const T&`` | Yes | **Yes** | Yes (with conversion) | No |
| ``T&&`` | **No** | Yes | No | Yes |
| ``const T&&`` | **No** | Yes | No | No |

This table has a lot of information, but there are a few key conclusions worth remembering. First, ``const T&`` is a "universal receiver"—it can bind to almost anything (lvalue, rvalue, even different types), at the cost that you cannot modify the referenced object through it. Second, ``T&&`` only binds to rvalues, which is exactly what move semantics needs: it guarantees that what is bound is definitely an object "from which resources can be safely stolen." Third, ``const T&&`` exists but is almost useless—it can bind to rvalues but can't modify them, losing the core advantage of rvalue references "allowing modification of temporary objects."

## What We've Cleared Up Here

In this post, starting from K&R's "left of the equal sign," we built a complete picture of C++ value categories step by step. We saw how const objects broke the old definition of "lvalue = assignable," how class rvalues gain memory locations through temporary materialization, the distinct binding rules of lvalue references and rvalue references, and finally found the theoretical basis for move semantics in the C++11 lvalue/xvalue/prvalue triad.

The core takeaways are two: first, rvalue references ``T&&`` only bind to rvalues, giving the compiler a natural signal—"the bound thing is temporary, its resources can be safely stolen." Second, the "if it has a name, it's an lvalue" rule means we sometimes need ``std::move`` to explicitly tell the compiler "please allow moving."

Looking back, the distinction between lvalues and rvalues wasn't invented out of thin air by C++11—it has existed since the C language era, just much simpler then. C++ introduced const, class types, references, operator overloading, and each step blurred the boundaries of value categories, until move semantics needed a precise mechanism to distinguish "persistent" and "temporary" objects, and C++11 finally formalized this system into the three-level classification of lvalue/xvalue/prvalue. Understanding the evolution logic of this system makes learning ``std::move``, move constructors, perfect forwarding, and other concepts much smoother later—because their designs all respond to the same question: "How does the compiler know if this object can be safely moved?"

With this theoretical foundation, in the next post we can enter actual combat—implementing move constructors and move assignment operators for MyString, seeing exactly how ``std::move`` works, and under what conditions copy elision lets us skip moving entirely.

If you want a more systematic explanation of rvalue references, vol2's [Rvalue References: From Copy to Move](../../../../vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md) is a great supplementary material.

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
    chapter="Original definition of Lvalue"
  />
</ReferenceCard>
