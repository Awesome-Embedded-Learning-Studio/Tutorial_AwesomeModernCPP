---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Understand the C++ value category system and master the binding rules and core semantics of rvalue references
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Volume 1: C++ Fundamentals'
reading_time_minutes: 24
related:
- Move Construction and Move Assignment
- 'Perfect Forwarding: Keeping Value Categories Intact'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 'Rvalue References: From Copy to Move'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md
  source_hash: 74e3d0b1100124fb369e44ab63c20f08ccc8eb1eb01a42537f4453dddebdcbb9
  translated_at: '2026-09-27T04:34:42+00:00'
  engine: anthropic
  token_count: 17000
---
# Rvalue References: From Copy to Move

Welcome to modern C++! When this tutorial series says "modern C++", we generally mean C++11 and everything that came after it.

> Some readers will push back on this — I've been grilled about it in person: "C++11 still counts as modern?" Hmm... fair enough, actually. Counting from 2026, when I'm writing this, these features have been with us for over a decade, so chronologically speaking they aren't exactly new. But compared with the C++98 relics, they represent a dramatic change in the language's feature set. That's exactly why this volume gets its own slot!

When I first learned C++, I ground my way through *Effective Modern C++*<RefLink :id="1" preview="Scott Meyers, Effective Modern C++, 2014 — Items 23-25: rvalue references, universal references, std::move" /> and never felt I truly understood "rvalue references". And no wonder — the name alone gives off an indescribable academic vibe. What exactly is `T&&`? Where is the line between lvalues and rvalues? Does `std::move` actually *move* anything? Every time `std::move` showed up in someone else's code, I copied it over with half an understanding and left the rest to the compiler's mercy. Now that it's my turn to write the tutorial, I owe it to you to work through these topics together — at minimum, without embarrassing myself or making rookie mistakes!

> Same old aside: I'm rather afraid of C++ language lawyers — every time I sit down to write, I worry these folks will dig faults out of my text. Rigor is always a good thing, though. Write C++ without rigor and, best case, you stare blankly at a screenful of compiler errors; worst case, a memory error shakes you out of bed at midnight. Teaching, though, is a different matter: there's no need to nitpick every detail up front, and we shouldn't miss the forest for the trees.

## Starting with a Problem That Spikes Your Blood Pressure

The scenario we have in mind is string processing — everyone knows that one, right? Plenty of people feel that std::string is sometimes too heavy and want a read-only view of a string instead. const char* looks like a decent fit, but C strings rely on a trailing \0 terminator, and that's sometimes shaky: the content can't contain a \0, and the length can only be found by counting from the front. So let's build our own StringWrapper!

```cpp
class StringWrapper {
    char* data_;
    std::size_t size_;

public:
    StringWrapper(const char* str)
    {
        size_ = std::strlen(str);
        data_ = new char[size_ + 1];
        std::memcpy(data_, str, size_ + 1);
    }

    // Copy constructor: deep copy
    StringWrapper(const StringWrapper& other)
        : size_(other.size_)
    {
        data_ = new char[size_ + 1];
        std::memcpy(data_, other.data_, size_ + 1);
    }

    ~StringWrapper()
    {
        delete[] data_;
    }
};
```

Then we write a piece of code that looks perfectly innocent:

```cpp
StringWrapper build_greeting(const std::string& name)
{
    StringWrapper result(("Hello, " + name + "!").c_str());
    return result;
}

int main()
{
    StringWrapper greeting = build_greeting("World");
    return 0;
}
```

Let's assume the worst case: no move semantics, and let's also assume the compiler does no NRVO (named return value optimization). Following that assumption through, when `build_greeting` returns `result`, a copy construction has to fire: we allocate a new block of memory and copy the string inside `result` over byte by byte. Then look at `result` itself — its destructor frees the original block too. (Think about it: doesn't that waste make you wince?)

> To be fair, we should complete the picture. GCC and MSVC back in the C++03 era already supported NRVO widely. The standard has always allowed the compiler to elide this copy — it just never required it — so the analysis above discusses the worst case where NRVO doesn't kick in.

We spent one memory allocation plus one byte-by-byte copy, all to "relocate" the data of an object that is about to be destroyed anyway. If the string is long — say a JSON payload of several KB — the waste becomes glaring: **the source object is about to die regardless; the data sitting in that memory will do no one any good, so why not simply take over control of that memory?**

Move semantics exists precisely to eliminate the waste we just saw. And to understand move semantics, we have to go back to a more fundamental question: how does C++ classify expressions? The standard's answer is what we call **value categories**<RefLink :id="2" preview="cppreference Value categories — lvalue / xvalue / prvalue taxonomy since C++11" />.

## Value Categories: From a Two-Way Split to a Three-Way Split

Before C++11, we sorted expressions into just two buckets: **lvalues and rvalues** — a simple two-way split. Once C++11 brought "an object's resources can be safely transferred" into the language, the left/right distinction alone was no longer enough, and the taxonomy grew more complex accordingly. The system we have now looks like this: every expression belongs to exactly one of three categories. As for the three-way split, **lvalue** is the has-identity class, **xvalue** is the about-to-expire class, and **prvalue** is the pure-temporary class. Merge them upward and you get two broader categories. The one governing the lvalue half we call **glvalue** (generalized lvalue), which subsumes lvalue plus xvalue. The one governing the rvalue half we call **rvalue**, which subsumes xvalue plus prvalue.

If this taxonomy feels a bit convoluted, don't worry — I went around in circles with it for quite a while at first too. We can understand it through two properties. The first we call **has identity**: the expression has a name and you can take its address. The other we call **can be moved from**: the expression is temporary, and its resources can be safely "stolen" by us. Cross the two properties, and the three expression classes fall right into place:

|              | Not movable | Movable                 |
| ------------ | ----------- | ----------------------- |
| **Has identity**   | lvalue   | xvalue (expiring value) |
| **No identity** | ——       | prvalue (pure rvalue)   |

Take an ordinary example: in the variable `int x = 10;`, `x` has a name, has an address, and its lifetime isn't over yet, so it lands in the "has identity, not movable" cell — an lvalue. Later code will still use it, so of course you can't just steal its resources. The result of `std::move(x)` still refers to that same object `x`; it has merely been flagged as "expiring soon, resources free to take", landing on the "has identity, movable" crossing as an xvalue. The literal `42`, or a temporary returned by value from a function, has no name to begin with, so it falls into the prvalue cell — after moving it, you don't have to worry about anyone still accessing it. As for the "no identity and not movable" combination, the standard simply reserves no slot for such an expression; the three classes cover every expression there is.

Let's look at a concrete set of examples to draw the boundaries between the three classes.

```cpp
int x = 10;            // x is an lvalue
int&& r = std::move(x); // std::move(x) is an xvalue
int y = x + 1;         // x + 1 is a prvalue
int z = 42;            // 42 is a prvalue
```

Here `x` is the most typical lvalue — it has a name and an address, and `&x` is a legal expression (of course you can take this variable's address on the stack!). `std::move(x)` produces an xvalue that still points to the same memory as `x`; semantically it has merely been marked in its "about to expire" form. `x + 1` and `42` are both prvalues — temporary, unnamed values.

We also need to dismantle a classic misconception — that old line passed around forever: "lvalues can appear on the left of the assignment sign, rvalues must stay on the right". Back in the C era that was roughly true, but in the C++ era it is neither sufficient nor necessary. Two counterexamples. For the first, take `const int cx = 10;`: `cx` is an lvalue, yet `cx = 20;` won't compile — const restricts modification, it doesn't change the value category. In the other direction, construct a `std::string("hello")`: it's a temporary prvalue, and it really can sit on the left of an assignment — `std::string("hello") = "world";` is perfectly legal. The reason isn't complicated: the assignment operator of a class type is a member function, and that line of ours is, in essence, calling a member function on a temporary object, which has held since C++98. What genuinely can't be written on the left of an assignment is a built-in type — write `42 = x;` and no standard will compile it.

### Determining the Value Category of Any Expression with decltype

We basically understand the rules, but when we meet an unfamiliar expression, how do we actually confirm its identity — lvalue, xvalue, or prvalue? We can't just wing it every time. There's a ready-made trick here: `decltype` deduces different types for an **identifier** than for a **parenthesized expression**.

Put `x` itself and `(x)` side by side: `decltype(x)` is an unparenthesized identifier, and it yields `x`'s **declared type**. With `decltype((x))`, once you add the parentheses, the deduction rule switches to judging by value category: an lvalue yields `T&`, an xvalue yields `T&&`, and a prvalue yields `T` itself.

Plug this difference into `is_lvalue_reference_v` / `is_rvalue_reference_v`, and we can determine the value category of any expression:

```cpp
// value_category_probe.cpp -- determine the value category of any expression with decltype
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <utility>

template <class T>
constexpr const char* value_category()
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return "lvalue";
    } else if constexpr (std::is_rvalue_reference_v<T>) {
        return "xvalue";
    } else {
        return "prvalue";
    }
}

// decltype((expr)) deduces the type by the expression's value category: lvalue gives T&, xvalue gives T&&, prvalue gives T
#define SHOW(expr) \
    std::cout << "  " #expr "  ->  " << value_category<decltype((expr))>() << "\n"

int g = 100;  // global variable

int main()
{
    int x = 10;        // ordinary variable
    int& lref = x;     // lvalue reference
    int&& rref = 20;   // rvalue reference (but the name rref itself is an lvalue!)

    std::cout << "--- 变量与引用 ---\n";
    SHOW(x);
    SHOW(lref);
    SHOW(rref);          // counterintuitive: a named rvalue reference is an lvalue

    std::cout << "\n--- 字面量与运算 ---\n";
    SHOW(42);
    SHOW(x + 1);
    SHOW(std::move(x));  // the result of std::move is an xvalue

    std::cout << "\n--- 解引用与成员 ---\n";
    SHOW(*(&x));         // dereferencing yields an lvalue
    SHOW(g);

    return 0;
}
```

The complete probe code is right below — click "Try It Out" and it runs on the spot; we even save you the terminal:

<OnlineCompilerDemo
  title="Hands-On: value_category_probe.cpp"
  source-path="code/examples/vol2/14_value_category_probe.cpp"
  description="Determine the value category of a batch of expressions online. The rref line deserves your full attention: a variable declared as int&& has a name that is itself an lvalue."
  run-options="-O0 -std=c++17"
  allow-run
/>

Go over every line of the output; the one most worth understanding is the `rref` line — it is clearly declared as an rvalue reference `int&& rref`, yet the verdict is `lvalue`. This is not a bug: `rref` is a variable **with a name**, and C++'s rule is "a named expression is an lvalue". The xvalue produced by `std::move(x)`, once you give it a name (assign it to a `T&&` variable, or pass it in as a function parameter), gets "demoted" back to the lvalue side, and moves no longer fire automatically. Perfect forwarding is what solves this — our fourth article will take it apart in detail.

With this ready-made tool, any time you meet an expression you're unsure about, you can let the code confirm it for us — no more guessing on gut feeling.

## The Binding Rules of Rvalue References

With value categories understood, let's see what an rvalue reference — `T&&` — can actually bind to. The rule is quite simple: **an rvalue reference binds only to rvalues, namely prvalues or xvalues, never to lvalues**.

```cpp
int x = 10;

int&& r1 = 42;           // OK: 42 is a prvalue
int&& r2 = x + 1;        // OK: x + 1 is a prvalue
int&& r3 = std::move(x); // OK: std::move(x) is an xvalue

// int&& r4 = x;         // compile error: x is an lvalue and cannot bind to an rvalue reference
```

If you uncomment the last line, GCC hands you a rather blunt error message:

```text
error: cannot bind rvalue reference of type 'int&&' to lvalue of type 'int'
```

The value category taxonomy and the rvalue reference binding rules are animated below — you can play, pause, or step through frame by frame:

<Anim id="lvalue-rvalue" />

Why was the rule designed this way? The intent isn't hard to guess: the purpose of rvalue references is to let you "take over" a temporary object's resources. If you ask why they aren't allowed to bind lvalues as well — we'll come back to that after comparing them with const lvalue references.

Here's a heads-up from me: the rule "`T&&` binds only rvalues" refers to rvalue references with a **fixed, concrete type**, such as `int&&` or `std::string&&`. When we get to perfect forwarding in the fourth article, we'll meet `T&&` in a template, which we call a **forwarding reference** — it binds both lvalues and rvalues, under a completely different set of rules. If you apply this article's rules to a templated `T&&`, the compiler will throw the error right back in your face.

Now let's put rvalue references and const lvalue references side by side — this comparison directly determines what the move constructor's signature will look like later.

The const lvalue reference `const T&` binds to everything: lvalues, rvalues, const, non-const — nothing is turned away. The rvalue reference `T&&`, by contrast, binds only rvalues. The difference looks simple, but it leads to a critically important practical distinction: when you receive an rvalue through `const T&`, you have promised not to modify it, so you cannot steal its resources. Switch to receiving it through `T&&`, and you now have the right to modify it — which is what makes transferring its resources safe.

```cpp
void process_const_ref(const std::string& s)
{
    // we can read s, but not modify it
    // so we cannot "steal" s's internal buffer
    std::cout << s.size() << "\n";
}

void process_rvalue_ref(std::string&& s)
{
    // s is a non-const rvalue reference, so we can modify it
    // so we can safely transfer s's internal resources
    std::string stolen = std::move(s);
    // s is now in a "valid but unspecified" state
}
```

Now let's circle back to the question we set aside: why not let rvalue references bind lvalues too? Reason backward from the intent of move semantics and it becomes clear. Move semantics expresses a transfer of resource ownership, while an lvalue is an object with its own address that is actively managing its own resources — nobody ever said it planned to be hollowed out. If `T&&` could bind anything, you could no longer tell apart "this object is safe to move" from "this object is still in use". Lose that distinction, and move semantics stops being meaningful.

## The Essence of std::move: A Type Conversion

The name `std::move` is, I'd say, one of the most misleading names in the history of C++. Hearing it, you'd probably assume it actually *moves* something — but in reality it moves nothing at all. `std::move` does exactly one thing: **it converts its argument to an rvalue reference** — it is just a `static_cast<T&&>`<RefLink :id="3" preview="cppreference std::move — equivalent to static_cast to rvalue reference; moves nothing" />. Really, that's all there is.

We can implement an equivalent `move` ourselves:

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&&
my_move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

What this code does is completely straightforward: whatever type `T` comes in, we strip off any reference it might carry with `remove_reference`, then use `static_cast` to turn it into an rvalue reference. That's the entire function body — these few lines are doing precisely the standard library `std::move`'s job.

So where's the payoff? The payoff lands in **the signatures of the move constructor and the move assignment operator**. When you write `std::string a = std::move(b);`, it is `std::move(b)` that converts `b` into a `std::string&&`, so the match lands on the move constructor. What does that look like? Its signature is `std::string(std::string&& other)`. The one actually performing the "resource transfer" is the move constructor: it takes over `other`'s internal buffer pointer and then empties `other` — that's what a typical implementation does<RefLink :id="4" preview="cppreference Move constructor — transfer resources, leave source valid but unspecified" />. `std::move`'s entire contribution here is marking `b` as an rvalue that "may be carted away", so the compiler picks the latter of the two constructors — copy and move — on our behalf.

> Short strings are the one exception: they live directly in a small buffer inside the object itself (SSO, Short String Optimization), so there is no separate heap buffer for us to take over, and the move degrades into an ordinary copy.

```cpp
std::string a = "Hello";
std::string b = std::move(a);  // std::move only converts the type
                                  // the move constructor does the actual resource transfer
// a is now in a "valid but unspecified" state
// in most implementations a becomes an empty string, but you should not rely on that
```

Here is another misconception worth staying alert to: **using `std::move` on fundamental types brings zero performance gain**. Look at `std::move(42)` again — it merely converts the `int` into an `int&&`, but for an `int`, "moving" and "copying" were always the same thing: copying four bytes. That is a fact at the level of the language definition, and it has nothing to do with whether the compiler optimizes anything. The power of move semantics shows up only in **classes that manage resources** — classes holding dynamic memory, file handles, network connections, and the like.

## The Lifetime of Temporary Objects: What Rvalue References Extend

In C++, a temporary object (a prvalue) lives by default only until the end of the full expression containing it: the moment the expression ends, it destructs. But rvalue references and const lvalue references give us a special privilege: when they bind to a temporary object, they extend that temporary's lifetime so it lives until the reference's scope ends<RefLink :id="5" preview="cppreference Reference initialization — temporary lifetime extension applies only to direct binding" />.

```cpp
const int& cr = 42;       // the const reference extends the lifetime of 42
std::cout << cr << "\n";   // OK: 42 is still alive

int&& rr = 100;            // the rvalue reference also extends the lifetime of 100
std::cout << rr << "\n";   // OK: 100 is still alive
```

The two behave identically as far as lifetime extension goes; the difference is that `rr` is non-const — you can modify it. That may look a little odd: a literal `100`, and we can modify it? In fact, behind the scenes the compiler has placed that temporary value into a piece of storage, and `rr` points at exactly that storage.

```cpp
int&& rr = 100;
rr = 200;                  // legal! the storage rr points to is modified
std::cout << rr << "\n";   // prints 200
```

This feature doesn't come up much in real code, but understanding it helps you shake off the fear that "an rvalue reference dangles right away". When you write `std::string&& ref` to catch `std::move(name)`, the object `ref` points to will not vanish on the next line — it lives until `ref`'s scope ends.

But let's push one step further: both const lvalue references and rvalue references can extend a temporary's lifetime to the end of the reference's own scope — does the privilege survive if the binding passes through a function? Let the code answer. The classic way to trip over this looks like the following:

```cpp
// lifetime_dangle.cpp -- the limits of temporary lifetime extension
// Standard: C++17
// Compile (ASan, short for AddressSanitizer, is a memory-error detection tool; enable its use-after-scope detection to catch the dangling read):
//   g++ -std=c++17 -O0 -g -fsanitize=address -fsanitize-address-use-after-scope \
//       -o lifetime_dangle lifetime_dangle.cpp

#include <iostream>
#include <string>

// return the parameter's reference unchanged
const std::string& pass_through(const std::string& s)
{
    return s;
}

int main()
{
    std::cout << std::unitbuf;  // flush eagerly so output is visible before ASan aborts

    // Case 1: const& directly binds a temporary -- lifetime extended, safe
    const std::string& safe = std::string("I am a temp");
    std::cout << "1) 直接绑定临时对象: \"" << safe << "\"\n";

    // Case 2: const& binds "a reference returned from a function" that points to a temporary -- no extension, dangling
    const std::string& dead = pass_through(std::string("I am passed"));
    std::cout << "2) 经函数返回的引用: \"" << dead << "\n";  // use-after-scope

    return 0;
}
```

Case 1 is fine: the `const&` catches the temporary securely, and its lifetime extends all the way until `safe` leaves scope. Case 2 goes wrong — the temporary `"I am passed"` binds to the function parameter `s`, and per the standard it lives only until **the end of the full expression that contains it**. What `dead` receives is the reference returned by `pass_through`, and a binding "relayed through a function" does not trigger extension. So the instant the expression ends, the temporary destructs. When we read `dead` on the next line, we are reading an already-destroyed object, and that step's result is undefined.

Errors like this are very hard to spot by eye — without ASan it might "happen" to still print the old contents, because that memory still holds what was there before, and that's exactly what makes dangling references so insidious. Run it again with ASan's use-after-scope detection on, and the truth reveals itself at once:

```text
1) 直接绑定临时对象: "I am a temp"
2) 经函数返回的引用: "=================================================================
==PID==ERROR: AddressSanitizer: stack-use-after-scope on address 0x... at pc 0x...
    #2 ... in main lifetime_dangle.cpp:26
SUMMARY: AddressSanitizer: stack-use-after-scope ... in std::__ostream_insert
```

(The process id `PID` and the address `0x...` differ from run to run; I've omitted everything that can be omitted — the error type, the line number, and the SUMMARY are fixed.) Case 1 prints normally; Case 2 is caught red-handed by ASan the moment we try to read `dead`. With the evidence in hand, we can formally answer the question we asked earlier: **lifetime extension applies only to "direct binding" and does not cross function boundaries** — once a temporary passes through a function relay, the extension rule stops applying with it.

## In Practice: Copies and Moves in String Concatenation

Let's put what we've learned together and look at a real example. Suppose we are building a log message:

```cpp
#include <iostream>
#include <string>
#include <vector>

std::string build_log_message(
    const std::string& level,
    const std::string& module,
    const std::string& detail)
{
    std::string msg = "[" + level + "] " + module + ": " + detail;
    return msg;
}

int main()
{
    std::string log = build_log_message("ERROR", "Network", "Connection timeout");
    std::cout << log << "\n";
    return 0;
}
```

That `"[" + level + "] " + module + ": " + detail` is a chain of `+` operations. In the C++03 world, every `+` we perform yields a brand-new temporary string! Each link in the chain costs a fresh memory allocation plus copying the entire accumulated left-hand content over, so the further along we concatenate, the more it costs — think about how wasteful that is!

From C++11 on, the standard library added a batch of `operator+` overloads that take rvalue references (for example `operator+(std::string&&, const std::string&)`). With them, we can keep using the temporary in the middle of the chain: append the next segment directly into its buffer and hand it on, instead of starting a fresh string at every link and re-copying all the preceding characters. Counting it up, only the first link creates a new temporary string; everything after that appends into its buffer, and it only reallocates when capacity runs out. The cost of reallocation amortizes across every append, and the total number of characters copied along the whole chain is proportional only to the final message's length.

By C++17, guaranteed copy elision arrives and turns "returning a prvalue" into a hard guarantee: the result `operator+` spits out is constructed directly in the final receiver's location, saving even the single move at reception. Back in the C++11 era, compilers would generally save that move for us too, but that was always convention, never obligation. C++17 wrote it into the language's rules, so what we get now is a guarantee.

The more direct payoff comes from returning from functions: look again at `build_log_message`, which returns `msg` — the compiler has two ready-made optimization tools in hand. NRVO can elide this copy outright. And failing that, in settings where NRVO doesn't apply, C++11 also automatically treats `msg` as an rvalue (implicit move), invoking `std::string`'s move constructor. That step transfers only the internal pointers and copies no character data — see how much lighter the cost suddenly is.

Let's look at another example, moving elements into a container:

```cpp
std::vector<std::string> names;

std::string name = "Alice";
names.push_back(std::move(name));  // move: name's internal data is transferred into the vector
// name is now in a valid but unspecified state; do not use it again

names.push_back("Bob");   // first constructs a temporary from const char*, then moves it into the vector
```

Look at the first `push_back`: it uses move semantics. `std::move(name)` turns `name` into an rvalue reference, and the vector invokes `std::string`'s move constructor to build the new element. Its cost is transferring one pointer plus two `size_t`s — not copying the entire string contents. The second one, `push_back("Bob")`, looks like it constructs directly, but it actually has one step more than `push_back(std::move(name))`. Which step is the extra one, and what exactly did the move save us? Let's run it with a tracked class and let the output tell you.

We'll wire the `TrackedString` below with tracking: every construction, copy, move, and destruction leaves a line of its own on the screen:

```cpp
// push_back_vs_emplace.cpp -- construction/move/destruction tracing for push_back vs emplace_back
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <vector>

class TrackedString
{
    std::string data_;

public:
    explicit TrackedString(const char* s) : data_(s)
    {
        std::cout << "  [ctor from const char*] \"" << data_ << "\"\n";
    }

    TrackedString(const TrackedString& other) : data_(other.data_)
    {
        std::cout << "  [copy ctor] \"" << data_ << "\"\n";
    }

    TrackedString(TrackedString&& other) noexcept : data_(std::move(other.data_))
    {
        std::cout << "  [move ctor] \"" << data_ << "\"\n";
    }

    ~TrackedString()
    {
        std::cout << "  [dtor] \"" << data_ << "\"\n";
    }
};

int main()
{
    std::cout << "=== push_back(TrackedString(\"Bob\")) ===\n";
    {
        std::vector<TrackedString> v;
        v.push_back(TrackedString("Bob"));
        std::cout << "=== done ===\n";
    }

    std::cout << "\n=== emplace_back(\"Alice\") ===\n";
    {
        std::vector<TrackedString> v;
        v.emplace_back("Alice");
        std::cout << "=== done ===\n";
    }

    return 0;
}
```

The tracking program is in the demo below — click "Try It Out" to run it, and let's count the lines of output:

<OnlineCompilerDemo
  title="Hands-On: push_back_vs_emplace.cpp"
  source-path="code/examples/vol2/15_push_back_vs_emplace.cpp"
  description="Compare the construction chains of push_back and emplace_back online. Count the lines each section prints before === done ===: three for push_back, one for emplace_back."
  run-options="-O0 -std=c++17"
  allow-run
/>

Look at the `push_back(TrackedString("Bob"))` section: before `=== done ===` you can count three lines. The first constructs the temporary, the second moves it into the vector, and the third is the temporary's own destruction. Counting it up, the construction chain actually took two steps. The `[dtor] "Bob"` line appearing after `=== done ===` is the vector destroying its held element when it leaves the `{ }` scope — not part of the construction process.

The answer to which step the earlier `push_back("Bob")` added is hidden in those three lines of output: `"Bob"` first goes through the `const char*` constructor, implicitly converting into a temporary `std::string`, then lands in the `push_back(T&&)` overload as an rvalue, and finally gets moved into the vector. The extra step is precisely the creation of the temporary. The move happened exactly once in the whole process — note that `[move ctor]` appears exactly once — and a deep copy never happened at any point. Meanwhile `emplace_back("Alice")` shows only one `ctor` line before `=== done ===`: it constructs in place, directly in the vector's storage, saving both the temporary and the move in one stroke. If you truly want to avoid creating even the temporary, switch to `emplace_back`.

## Hands-On Lab: rvalue_demo.cpp

In the `emplace_back` output just now, we only watched one action at a time. This time let's go bigger: write a complete program that puts everything this article has covered into a single `main`, and run it start to finish.

```cpp
// rvalue_demo.cpp -- rvalue references and value categories demo
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>

class Tracker
{
    std::string name_;

public:
    explicit Tracker(std::string name)
        : name_(std::move(name))
    {
        std::cout << "  [" << name_ << "] 构造\n";
    }

    Tracker(const Tracker& other)
        : name_(other.name_ + "_copy")
    {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }

    Tracker(Tracker&& other) noexcept
        : name_(std::move(other.name_))
    {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }

    ~Tracker()
    {
        std::cout << "  [" << name_ << "] 析构\n";
    }

    Tracker& operator=(const Tracker& other)
    {
        name_ = other.name_ + "_copy";
        std::cout << "  [" << name_ << "] 拷贝赋值\n";
        return *this;
    }

    Tracker& operator=(Tracker&& other) noexcept
    {
        name_ = std::move(other.name_);
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动赋值\n";
        return *this;
    }

    const std::string& name() const { return name_; }
};

/// @brief Returns a temporary object (prvalue)
Tracker make_tracker(std::string name)
{
    return Tracker(std::move(name));
}

int main()
{
    std::cout << "=== 1. 基本构造 ===\n";
    Tracker a("A");
    std::cout << '\n';

    std::cout << "=== 2. 拷贝构造 ===\n";
    Tracker b = a;
    std::cout << "  a.name = " << a.name() << "\n";
    std::cout << "  b.name = " << b.name() << "\n\n";

    std::cout << "=== 3. 移动构造（显式 std::move）===\n";
    Tracker c = std::move(a);
    std::cout << "  a.name = " << a.name() << "\n";
    std::cout << "  c.name = " << c.name() << "\n\n";

    std::cout << "=== 4. 返回临时对象 ===\n";
    Tracker d = make_tracker("D");
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 5. 移动赋值 ===\n";
    d = std::move(b);
    std::cout << "  b.name = " << b.name() << "\n";
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 6. 程序结束，析构顺序 ===\n";
    return 0;
}
```

The complete program is in the demo below — click "Try It Out" and run it. This time we skip nothing, checking even the end-of-program destructions against the output:

<OnlineCompilerDemo
  title="Hands-On Lab: rvalue_demo.cpp"
  source-path="code/examples/vol2/01_rvalue_reference.cpp"
  description="Run online and observe the construction, copy construction, move construction, and destruction order of Tracker objects."
  run-options="-O0 -std=c++17"
  allow-run
/>

Step 1 we can dispatch in one sentence: `Tracker a("A");` is an ordinary construction. In step 2's output, `Tracker b = a;` triggers the copy constructor — `a` is an lvalue, and the only constructor it can match is the copy constructor, so `b`'s name becomes `"A_copy"`. In step 3, `std::move(a)` converts `a` into an rvalue reference to match the move constructor — `c`'s name becomes `"A"` (stolen from `a`), while `a`'s name becomes `"(moved-from)"`.

Step 4 is the most interesting one, so let's linger on it. `make_tracker("D")` constructs a `Tracker("D")` inside the function, then performs the return. Notice the output shows exactly one construction — no copy, no move. That's C++17's **guaranteed elision**: when returning a prvalue, the compiler constructs the object directly in the caller's space, saving even that move. And that's only the returning-a-prvalue case. How much more the compiler can save when returning a named local variable is exactly what the third article, on RVO and NRVO, will take apart.

Step 5 is move assignment's turn. `d = std::move(b);` transfers `b`'s resources to `d` — `d`'s original name `"D"` is overwritten with `"A_copy"`, and `b` is left with nothing but the name `"(moved-from)"`. Notice also that `d`'s original resources (the memory holding `"D"`) were properly released, because the move assignment operator we wrote must ensure the old resources get cleaned up before overwriting.

In step 6 we watch the program wrap up: these objects destruct in reverse order of construction when leaving `main` — `d` first, then `c`, `b`, `a`; that order is guaranteed by the language. Also note `a` and `b` along the way: they were gutted long ago, their names already read `"(moved-from)"`, yet their destructors still ran. The "valid" in "valid but unspecified", made concrete here, is that even the destruction runs to completion — we can't pretend a moved-from object doesn't exist.

The next article turns these rules into code — we'll write move constructors and move assignment for a resource-owning class with our own hands, make the "take the pointer, empty the source" moves rock solid, and round things out with the so-called Rule of Five (the five special members: destructor, copy constructor, copy assignment, move constructor, move assignment — either we declare none of the five, or if we declare any, we have to consider all five together).

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Scott Meyers"
    title="Effective Modern C++: 42 Specific Ways to Improve Your Use of C++11 and C++14"
    publisher="O'Reilly Media"
    :year="2014"
    chapter="Items 23-25: rvalue references, universal references, std::move"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="Value Categories"
    url="https://en.cppreference.com/w/cpp/language/value_category"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::move"
    chapter="Notes: moved-from objects stay valid but unspecified"
    url="https://en.cppreference.com/w/cpp/utility/move"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Move Constructor"
    url="https://en.cppreference.com/w/cpp/language/move_constructor"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="Reference Initialization"
    chapter="Temporary lifetime extension"
    url="https://en.cppreference.com/w/cpp/language/reference_initialization"
  />
</ReferenceCard>
