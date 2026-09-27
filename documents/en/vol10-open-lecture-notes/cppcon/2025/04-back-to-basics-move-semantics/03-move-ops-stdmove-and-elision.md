---
chapter: 4
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: CppCon 2025 talk notes — the complete implementation of move construction/assignment,
  what std::move really does, NRVO and C++17 mandatory copy elision, and the moved-from
  state
difficulty: beginner
order: 3
platform: host
reading_time_minutes: 25
speaker: Ben Saks
tags:
- cpp-modern
- host
- beginner
talk_title: 'Back to Basics: Move Semantics'
title: Move Operations, std::move, and Copy Elision
video_bilibili: https://www.bilibili.com/video/BV1X54y1P7uM
video_youtube: https://www.youtube.com/watch?v=szU5b972F7E
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/04-back-to-basics-move-semantics/03-move-ops-stdmove-and-elision.md
  source_hash: 8e320d1591af83418caf425b0fee585c6f2eccd120b36d73b7c37299fffff12f
  translated_at: '2026-09-27T06:20:17+00:00'
  engine: anthropic
  token_count: 5400
---
# Move Operations, std::move, and Copy Elision

:::tip
This article is the third in our series of notes on CppCon 2025's "Back to Basics: Move Semantics". The first two parts covered the cost of copying and the motivation for moves, then lvalues, rvalues, and the reference system. This part zeroes in on the practical core: how to write a move constructor and a move assignment operator, what `std::move` actually does, and how C++17's copy elision changed the rules of the game.
:::

Honestly, I used to think I "understood" move semantics—isn't it just stealing pointers? How hard could it be? Then one day in a code review I saw a colleague write `return std::move(result);`. I casually remarked "nice, an explicit move," and the senior engineer next to me shot me down with one sentence: **"Are you sure that won't block NRVO?"**

It took a whole evening of digging to sort it out—`return std::move(result)` doesn't just fail to help optimization; it turns a return-value handoff the compiler could have completed at zero cost into one extra move construction. That was the day I truly understood that in move semantics, the devil lives entirely in the details.

In this article we'll take those details apart one by one. Our experimental environment is Arch Linux WSL with GCC 16.1.1 and the compile flag `-std=c++20`; if you plan to follow along with the code, have that version or a newer compiler ready.

## The Move Constructor: The Art of Stealing Pointers

In the previous article we finished `MyString`'s copy operations. Now let's add the move constructor. What this function does is, in Ben Saks's words, a "**destructive copy**"—we "steal" the source object's data, then leave the source object in a harmless state.

```cpp
class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // ... previous constructors, destructor, and copy operations ...

    // move constructor
    MyString(MyString&& s) noexcept
        : stored_length_(s.stored_length_)
        , actual_str_(s.actual_str_)
    {
        s.actual_str_ = nullptr;
        s.stored_length_ = 0;
    }
};
```

Let's take this code apart line by line, because every line earns its place.

First comes the parameter type `MyString&& s`—an rvalue reference. An rvalue reference can only bind to an rvalue (a temporary, the result of `std::move`, and so on), which means this constructor is called only when the compiler has confirmed that "the source object is about to die." That is the first layer of move semantics' safety guarantee: the compiler checks for you, through overload resolution.

Next, the initializer list. `stored_length_(s.stored_length_)` simply takes the source's length—`std::size_t` is a built-in type, so this "copy" is a single integer assignment, essentially free. `actual_str_(s.actual_str_)` is the crucial part: we assign the source's pointer directly to the new object, and the new object now points at the heap block the source allocated earlier. At this point the two objects point at the same memory—if we ended things there, that would be a double delete, undefined behavior.

So those two lines in the function body are the soul of the whole thing. `s.actual_str_ = nullptr` nulls out the source's pointer, and `s.stored_length_ = 0` zeroes the length. That way, when the source's destructor runs `delete[] actual_str_`, what actually executes is `delete[] nullptr`—and the standard explicitly states<RefLink :id="1" preview="C++ Standard, [expr.delete] — deleting a null pointer has no effect" /> that deleting a null pointer is a safe no-op.

You may have noticed that although the move constructor's parameter `s` is an rvalue reference, `s`'s destructor still runs. This is a point many people miss: a move is not "take over and never worry about the source again." Quite the opposite—after the move completes, the source object is still a complete, valid object; we have just deliberately set its internal state to "harmless" values. It still gets destructed normally; the destruction simply releases nothing.

## Overload Resolution: How the Compiler Chooses

With both a copy constructor and a move constructor in place, how does the compiler choose when it faces an initialization expression? The answer is overload resolution based on the argument's value category<RefLink :id="2" preview="C++ Standard, [over.match] — overload resolution selects the best viable function" />.

```cpp
MyString s1("hello");

// s1 is an lvalue (it has a name) → calls the copy constructor
MyString s2(s1);

// std::move(s1) is an rvalue → calls the move constructor
MyString s3(std::move(s1));
```

In the first line, `MyString s2(s1)`, `s1` is an lvalue—it has a name, and you can take its address. Seeing an lvalue argument, the compiler looks for a constructor that accepts `const MyString&` and lands on the copy constructor.

In the second line, `MyString s3(std::move(s1))`, the result of `std::move(s1)` is an rvalue reference, so the compiler looks for a constructor that accepts `MyString&&` and lands on the move constructor. This is why the two constructors must coexist: the copy constructor handles "the source will keep being used," while the move constructor handles "the source is doomed anyway."

Ben Saks made a point of stressing this in the talk: **an rvalue reference itself performs no move**. It is only a signal to the compiler at the type-system level—"this reference is bound to an rvalue." What actually decides copy versus move is overload resolution. If our `MyString` had no move constructor, `std::move(s1)` would still trigger nothing but the copy constructor—the compiler would settle for the `const MyString&` version, because a `MyString&&` can be received by `const MyString&`. No error, and no move either. We'll come back to this point later.

## The Move Assignment Operator: Clean Up the Old Object First

Move construction handles the "create a new object" scenario; move assignment handles "overwrite an existing object." The core logic of the two is very similar, but move assignment has one extra step—it must first clean up the target object's old resources.

```cpp
MyString& operator=(MyString&& s) noexcept
{
    if (this != &s) {
        delete[] actual_str_;         // step 1: release our own old resource
        stored_length_ = s.stored_length_;
        actual_str_ = s.actual_str_;  // step 2: steal the source's resource
        s.actual_str_ = nullptr;      // step 3: null out the source object
        s.stored_length_ = 0;
    }
    return *this;
}
```

The order matters. We first `delete[] actual_str_` to release the heap memory we owned before, then take over the source's pointer. If we did it the other way around—assign first, delete after—we would delete the very pointer the source just handed us: a textbook use-after-free.

The self-assignment check `if (this != &s)` matters just as much in move assignment. Admittedly `s` is an rvalue reference, and in theory nobody should write code like `x = std::move(x)`, but the language does not forbid it, and template instantiation can sometimes produce exactly that effect. Without the self-assignment check, `delete[] actual_str_` would free our own memory, and then `actual_str_ = s.actual_str_` would assign a dangling pointer back to itself—instant explosion.

Note that the return type is `MyString&`—an lvalue reference, not an rvalue reference. That's because the target of an assignment operator (the object on the left of `=`) is always an lvalue. Whether or not you use `std::move`, the receiving end of an assignment is invariably "an object with a name and an address."

Also, this implementation is safe with respect to exceptions—`MyString`'s data members are all built-in types (`std::size_t` and `char*`), and operations on those types don't throw. That's why I marked it `noexcept`. If your class has more complicated data members (another `std::string`, say), you'll have to think carefully about exception safety.

## std::move: The Most Misunderstood Function in C++

The name `std::move` is a terrible trap. The first time I saw it, I naturally assumed it "performs a move operation"—it's called "move," after all. But the truth is, **`std::move` itself moves nothing**.

Its true identity is a `static_cast` to an rvalue reference. The standard library's implementation is roughly equivalent to:

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&& move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

Strip away the `remove_reference` template gymnastics and the core is just `static_cast<T&&>(t)`: it converts its argument to an rvalue reference and returns it. That's all. It generates no move code, calls no move constructor, and modifies no object's state.

Ben Saks put it plainly in the talk: **if we could do it over, we'd probably have called it `make_movable` or `as_rvalue`**. At least those names wouldn't lead people to believe it performs a move.

### Why We Need std::move: The Naming Trap in swap

So if `std::move` doesn't move, why do we need it at all? Look at the `swap` function—the scenario that makes the answer clearest.

```cpp
template<typename T>
void swap(T& x, T& y)
{
    T temp(x);              // (1)
    x = y;                  // (2)
    y = temp;               // (3)
}
```

This C++03-style `swap` performs three copies. Of course we want to turn it into a move version—we've spent two articles saying that moves are much faster than copies. But here's the problem: inside the function body, `x`, `y`, and `temp` are all lvalues. They all have names, you can take their addresses, and their lifetimes span multiple statements. The compiler cannot automatically treat them as rvalues—what if you still need `temp` after the third line?

C++ has a general rule: **if it has a name, it's an lvalue**. Only nameless things (temporaries, literals, by-value function return results) can be rvalues. The rule is entirely reasonable—the compiler has to be conservative; it cannot assume `temp` goes unused on the next line.

So we need to tell the compiler explicitly: "I know `temp` won't be used again after this; please treat it as an rvalue." That is precisely what `std::move` is for:

```cpp
template<typename T>
void move_swap(T& x, T& y)
{
    T temp(std::move(x));    // move-construct temp
    x = std::move(y);        // move-assign x
    y = std::move(temp);     // move-assign y
}
```

Each `std::move` passes one message to the compiler: **"Right here, I confirm it is safe to move resources out of this object."** Only once the compiler has that information does it pick the move version during overload resolution.

### std::move Does Not Guarantee a Move

Here is another trap that's easy to miss: `std::move` does not guarantee that a move happens. If a type has only copy operations and no move operations, the result of `std::move` decays into a copy.

```cpp
struct CopyOnly
{
    CopyOnly() = default;
    CopyOnly(const CopyOnly&) { std::cout << "copy\n"; }
    // no move constructor!
};

CopyOnly a;
CopyOnly b(std::move(a));  // prints "copy" — decays to copy construction
```

Here `std::move(a)` converts `a` to an rvalue reference, but `CopyOnly` has no constructor accepting an rvalue reference. The compiler settles for the `const CopyOnly&` copy constructor (because `CopyOnly&&` can bind to `const CopyOnly&`). No error—it's just that the "move" you expected turned into a "copy." And it happens silently.

## The Naming Paradox of Rvalue Reference Parameters

This is the most confusing part of move semantics, and the part Ben Saks spent quite a while emphasizing.

When we write a function that takes an rvalue reference parameter, that parameter is **treated as an lvalue** inside the function:

```cpp
void process(MyString&& s)
{
    // s has a name → s is an lvalue
    MyString copy(s);             // calls the copy constructor, not the move constructor!
    MyString moved(std::move(s)); // THIS one calls the move constructor
}
```

From the perspective outside the function, the argument passed in is an rvalue (say `process(std::move(x))` or `process(MyString("temp"))`). But once execution enters the function body, `s` is a named variable—it exists across multiple statements, and the compiler cannot assume it's used only once. So the "has a name → is an lvalue" rule still applies.

This has a practical consequence: **inside a function, if you want to move resources out of an rvalue reference parameter, you must use `std::move` explicitly**. And once you've moved from it, the parameter's value in the rest of the code is unpredictable—that's the moved-from state, which we'll cover in a later section.

## Implicitly Movable Return Expressions

The good news is that the "has a name → is an lvalue" rule has one important exception—the `return` statement.

```cpp
MyString make_greeting()
{
    MyString temp("hello world");
    // ... do some work on temp ...
    return temp;  // no std::move needed!
}
```

In this code, although `temp` has a name (which would ordinarily make it an lvalue), `return temp;` is the last use of `temp` in the function. The compiler knows `temp`'s lifetime ends the instant the function returns, so the standard allows it to treat `temp` as an implicitly movable entity<RefLink :id="3" preview="C++ Standard, [class.copy.elision] — NRVO and implicit move" />.

This means you do **not** need to write `return std::move(temp);`. A plain `return temp;` is enough—the compiler automatically selects the move constructor (or, better still, eliminates the construction outright—we're getting to that right now).

## NRVO: An Optimization Even Better Than Moving

"Implicitly movable" is still not the end of the story. The compiler can actually do better than a move—it can deliver the return value to the caller at **zero cost**, without so much as a move. This is the **Named Return Value Optimization (NRVO)**.

```cpp
MyString make_greeting()
{
    MyString temp("hello world");
    return temp;
}

MyString s = make_greeting();
```

In a world without NRVO, the execution flow looks like this: construct `temp` on `make_greeting`'s stack frame; construct a temporary at `s`'s location (via a move or a copy); destruct `temp`; then move or copy the temporary into `s`; then destruct the temporary. It sounds wasteful—and it is.

NRVO's idea is remarkably clever: when generating code, the compiler simply constructs `temp` at `s`'s location. Not "construct first, copy afterwards," but "placed in exactly the right spot from the very beginning." `temp` is `s`; they share the same memory. When the function returns, no copy or move is needed—the object is already where it belongs.

Since C++17, this optimization has become **mandatory** in certain contexts<RefLink :id="4" preview="C++ Standard, [class.copy.elision] — mandatory elision in certain contexts" />—the compiler must elide the copy, rather than "may elide it, or may not." This is not an optional optimization; it is defined behavior of the language. It still carries the name "optimization" for historical reasons, but in practice it is a guarantee.

For the complete technical story on NRVO and RVO, we have a dedicated article in vol2: [RVO and NRVO: The Compiler's Return Value Optimization](../../../../vol2-modern-features/ch00-move-semantics/04-rvo-nrvo.md).

## Never Use std::move on a Return Value

This is probably the most common move-semantics mistake I've ever seen. We said that `return temp;` is implicitly movable: the compiler either applies NRVO (zero cost) or automatically falls back to move construction (the cost of one pointer assignment). So someone might think: since `std::move` is a "request to move," wouldn't `return std::move(temp);` be more explicit, and therefore safer?

**Exactly the opposite.**

```cpp
// correct: allows NRVO
MyString make_good()
{
    MyString temp("good");
    return temp;
}

// wrong: blocks NRVO!
MyString make_bad()
{
    MyString temp("bad");
    return std::move(temp);  // actually slower!
}
```

The reason lies in NRVO's trigger conditions<RefLink :id="5" preview="C++ Standard, [class.copy.elision] — the return expression must be the name of a local variable" />: the `return` expression must be the name of a local object. When you write `return std::move(temp);`, the return expression is no longer the name `temp`—it is `std::move(temp)`, a function call expression. The compiler cannot apply NRVO to that expression, so it can only fall back to the move constructor.

In other words, `return std::move(temp);` forces the compiler down the move-construction path, while `return temp;` keeps the NRVO path (zero cost) open. That's why Ben Saks hammered the point repeatedly in his talk: **do not use `std::move` on return values**.

We can use the `-fno-elide-constructors` compiler flag to compare the two. This flag turns off GCC's copy elision optimization and shows us what a world "without NRVO" looks like.

First look at how `return temp;` behaves with elision disabled—it falls back to move construction, because `temp` is implicitly movable. And `return std::move(temp);` is likewise a move construction—with elision off, there is no difference between the two. But once elision is enabled (that is, the default behavior), `return temp;` becomes a zero-op, while `return std::move(temp);` still pays for a move construction. That's exactly where the gap opens.

I ran the actual test on GCC 16.1.1—with print logs added to `MyString`'s various constructors, the comparison looked like this:

```bash
# NRVO on by default
$ g++ -std=c++20 -O2 test.cpp && ./a.out
=== return temp; (NRVO) ===
  构造: "hello"          # the only construction — no move, no copy

=== return std::move(temp); ===
  构造: "hello"
  移动构造: "hello"       # one extra move construction!
  析构: "(null)"
```

See that? `return std::move(temp);` clearly pays one extra move construction. For a class like `MyString` that holds nothing but a pointer and an integer, a move is cheap (one pointer assignment), but for more complex classes—objects containing several dynamic containers, say—the cost of this extra move can no longer be ignored.

```bash
# comparison with NRVO disabled
$ g++ -std=c++20 -O2 -fno-elide-constructors test.cpp && ./a.out
=== return temp; ===
  构造: "hello"
  移动构造: "hello"       # no NRVO — falls back to move construction
  析构: "(null)"

=== return std::move(temp); ===
  构造: "hello"
  移动构造: "hello"       # a move construction here too
  析构: "(null)"
```

With NRVO disabled, the two really do behave identically—one move construction each. But that is precisely what shows how `return std::move(temp);` throws away the NRVO opportunity for nothing under default settings.

:::warning C++20/C++23 further widened the scope of "implicitly movable"
The rule in this section—"don't use `std::move` on a return value"—holds in **every standard version (C++11 through C++26)** and is unconditionally safe advice. But the "implicitly movable" machinery itself has been steadily strengthened in later standards, and it's worth knowing about: C++11 introduced the original implicit move (when returning a local object, the compiler may treat it as movable); C++20 (proposal P1825, "More implicit moves") widened the scope of "implicitly movable entities"—locals bound to rvalue references, and `throw`ing a local object, were brought into implicit move as well; C++23 (proposal P2266) refined things further, letting return values be treated as xvalues in certain scenarios and covering more construction paths.

However these extensions evolve, **the iron rule "never write `std::move` when returning a local object" has never changed**—P1825/P2266 widen the range of what the compiler can move automatically, while `std::move` actually breaks NRVO's trigger conditions. The conclusion stands: write `return temp;` and leave the choice between NRVO and implicit move to the compiler.
:::

## The moved-from State: Valid but Unknowable

After a move operation completes, the source object is in what the standard calls a "**valid but unspecified state**"<RefLink :id="6" preview="C++ Standard, [lib.types.movedfrom] — moved-from objects are in a valid but unspecified state" />. Every word of that phrase deserves unpacking.

"Valid" means: no memory leaks, no resource leaks, no undefined behavior triggered. You can safely let the object destruct—its destructor runs normally, with no double free and no crash. For our `MyString`, after the move `actual_str_` has been set to `nullptr` and `stored_length_` to 0, so the destructor's `delete[] nullptr` does nothing.

"Unspecified" means: you may not make any assumptions about the value a moved-from object holds. The standard does not say a moved-from `std::string` must be an empty string, nor that a moved-from `std::vector` must be empty. Different standard library implementations may behave differently. Our own `MyString` returns `"(null)"` from `c_str()` after a move (that's our own safety net), but a moved-from `std::string` might return an empty string, or the original value—you can't rely on either.

```cpp
MyString a("hello");
MyString b(std::move(a));

// safe operations:
// 1. destruction — always safe
// 2. assigning a new value — always safe
a = MyString("new value");  // OK

// unsafe operations:
// 1. assuming a still holds "hello"
// 2. assuming a.size() is 0
// 3. assuming a.c_str() returns an empty string
// these assumptions may happen to hold on some implementations, but the standard does not guarantee them
```

:::warning Restrictions on using moved-from objects
In the Q&A, Ben Saks was asked whether a moved-from object can keep being used. His answer was refreshingly blunt: **after a move, the only things you should do with the source object are assign it a new value or let it destruct**. Anything else—reading its value, comparing it, passing it to another function—is gambling. You might win (the implementation happens to hand you a predictable value), or you might lose (the implementation changes, or you switch standard libraries). Don't gamble.

Don't confuse "valid" with "useful"—a moved-from object is a legitimate object, but not one with deterministic contents. If you need an empty object, create one explicitly; if you need a specific value, assign it explicitly. Don't count on the move operation to do any of that for you.
:::

## Why noexcept Matters: The Hidden Trap in vector Reallocation

Finally, a problem that real projects constantly overlook even though its impact is huge: **move constructors should be `noexcept`**.

Why? Look at `std::vector` reallocation. When a `vector`'s capacity runs out, it needs to allocate a larger block of memory and transfer the old elements into it. If the element's move constructor is `noexcept`, the `vector` transfers them with moves—very fast. If the move constructor is not `noexcept`, the `vector` falls back to copies<RefLink :id="7" preview="C++ Standard, [vector.modifiers] — if move ctor is not noexcept, vector uses copy during reallocation" />.

This is because `vector` has to provide the strong exception safety guarantee: if an exception is thrown during reallocation, the `vector`'s state must roll back to what it was before the reallocation. With moves, an exception partway through leaves the already-moved elements unrecoverable (their resources have already been stolen). With copies, the original data is still there, and rollback is safe.

Let's write a simple test to verify this behavior:

```cpp
#include <iostream>
#include <vector>
#include <cstring>

class StringNoNoexcept
{
    std::size_t len_;
    char* str_;

public:
    StringNoNoexcept(const char* s)
        : len_(std::strlen(s))
        , str_(new char[len_ + 1])
    {
        std::memcpy(str_, s, len_ + 1);
        std::cout << "  ctor: " << str_ << "\n";
    }

    ~StringNoNoexcept()
    {
        delete[] str_;
    }

    StringNoNoexcept(const StringNoNoexcept& o)
        : len_(o.len_)
        , str_(new char[o.len_ + 1])
    {
        std::memcpy(str_, o.str_, len_ + 1);
        std::cout << "  COPY ctor: " << str_ << "\n";
    }

    // no noexcept!
    StringNoNoexcept(StringNoNoexcept&& o)
        : len_(o.len_)
        , str_(o.str_)
    {
        o.str_ = nullptr;
        o.len_ = 0;
        std::cout << "  MOVE ctor: " << (str_ ? str_ : "(null)") << "\n";
    }

    const char* c_str() const { return str_ ? str_ : "(null)"; }
};

int main()
{
    std::vector<StringNoNoexcept> vec;
    vec.reserve(2);

    std::cout << "=== push 3 elements (triggers reallocation) ===\n";
    vec.emplace_back("AAA");
    vec.emplace_back("BBB");
    vec.emplace_back("CCC");  // this triggers the reallocation

    std::cout << "\n=== final contents ===\n";
    for (const auto& s : vec) {
        std::cout << "  " << s.c_str() << "\n";
    }
    return 0;
}
```

Compile and run, and you'll see output like this (GCC 16.1.1, `-std=c++20 -O2`):

```bash
$ g++ -std=c++20 -O2 test_noexcept.cpp && ./a.out
=== push 3 elements (triggers reallocation) ===
  ctor: AAA
  ctor: BBB
  ctor: CCC
  COPY ctor: AAA    # reallocation uses COPIES, not moves!
  COPY ctor: BBB
```

See it? When the third element triggers reallocation, the `vector` **copies** the first two elements into the new memory—even though we clearly implemented a move constructor. The reason: our move constructor isn't marked `noexcept`.

Now add `noexcept` to the move constructor:

```cpp
StringNoNoexcept(StringNoNoexcept&& o) noexcept  // noexcept added
```

Recompile and run:

```bash
$ g++ -std=c++20 -O2 test_noexcept.cpp && ./a.out
=== push 3 elements (triggers reallocation) ===
  ctor: AAA
  ctor: BBB
  ctor: CCC
  MOVE ctor: AAA    # moves now!
  MOVE ctor: BBB
```

The difference of a single `noexcept` keyword directly decides whether `vector` reallocation copies or moves. For a class holding dynamic memory, under heavy data volumes this difference can mean an order-of-magnitude performance gap.

This is a genuine production-grade trap. Plenty of people write a move constructor, forget the `noexcept`, and then wonder in performance tests why "move semantics didn't kick in." The answer is often that one missing keyword.

## The Complete MyString: The Big Five, All Present

Combining this article with the previous two, we arrive at a complete `MyString` implementation that satisfies the Rule of Five:

```cpp
#include <cstring>
#include <utility>

class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // constructor
    explicit MyString(const char* s = "")
        : stored_length_(std::strlen(s))
        , actual_str_(new char[stored_length_ + 1])
    {
        std::memcpy(actual_str_, s, stored_length_ + 1);
    }

    // destructor
    ~MyString()
    {
        delete[] actual_str_;
    }

    // copy constructor
    MyString(const MyString& other)
        : stored_length_(other.stored_length_)
        , actual_str_(new char[other.stored_length_ + 1])
    {
        std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
    }

    // move constructor — noexcept!
    MyString(MyString&& s) noexcept
        : stored_length_(s.stored_length_)
        , actual_str_(s.actual_str_)
    {
        s.actual_str_ = nullptr;
        s.stored_length_ = 0;
    }

    // copy assignment operator
    MyString& operator=(const MyString& other)
    {
        if (this != &other) {
            delete[] actual_str_;
            stored_length_ = other.stored_length_;
            actual_str_ = new char[stored_length_ + 1];
            std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
        }
        return *this;
    }

    // move assignment operator — noexcept!
    MyString& operator=(MyString&& s) noexcept
    {
        if (this != &s) {
            delete[] actual_str_;
            stored_length_ = s.stored_length_;
            actual_str_ = s.actual_str_;
            s.actual_str_ = nullptr;
            s.stored_length_ = 0;
        }
        return *this;
    }

    const char* c_str() const { return actual_str_ ? actual_str_ : "(null)"; }
    std::size_t size() const { return stored_length_; }
};
```

All five special member functions—destructor, copy constructor, copy assignment, move constructor, move assignment—are present. This is the so-called Rule of Five: if you need to customize any one of them, odds are you need to customize all five. The compiler-generated default versions are unsafe for classes that hold raw pointers.

## What We've Figured Out So Far

Across three articles, we set out from `swap`'s three deep copies, passed through the lvalue/rvalue value-category system, and finally, in this one, took apart every implementation detail of the move operations. Let me close with a compact checklist of this article's key points.

The core of a move constructor is a "destructive copy"—steal the source object's resource pointers, then put the source into a harmless state. Overload resolution automatically selects copy or move; you make no extra judgment at the call site. `std::move` moves nothing—it is only a cast to an rvalue reference, which lets overload resolution select the move version. An rvalue reference parameter is an lvalue inside the function—because it has a name—so you still need `std::move` to move from it. The `return` statement is the exception to the "has a name → is an lvalue" rule; the compiler automatically recognizes implicitly movable return expressions. NRVO can deliver a return value to the caller at zero cost—while `return std::move(temp)` blocks NRVO, so never write that. A moved-from object is in a "valid but unspecified" state, and the only safe operations on it are assigning a new value or destruction. Always mark move constructors `noexcept`—otherwise `std::vector` falls back to copying during reallocation, and the performance gap can be enormous.

If you want to dig further into more applications of move semantics—perfect forwarding, universal references, reference collapsing—see vol2's [Perfect Forwarding: Keeping Value Categories Intact](../../../../vol2-modern-features/ch00-move-semantics/05-perfect-forwarding.md). Move semantics combined with perfect forwarding is the complete foundation of modern C++ template programming.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [expr.delete]"
    :year="2020"
    chapter="Deleting a null pointer is a safe no-op"
  />
  <ReferenceItem
    :id="2"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [over.match]"
    :year="2020"
    chapter="Overload resolution selects copy or move based on value category"
  />
  <ReferenceItem
    :id="3"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [class.copy.elision]"
    :year="2020"
    chapter="Implicitly movable entities in return statements"
  />
  <ReferenceItem
    :id="4"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [class.copy.elision]"
    :year="2020"
    chapter="Mandatory copy elision since C++17"
  />
  <ReferenceItem
    :id="5"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [class.copy.elision]"
    :year="2020"
    chapter="NRVO requires the return expression to be a local variable name"
  />
  <ReferenceItem
    :id="6"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [lib.types.movedfrom]"
    :year="2020"
    chapter="Moved-from objects of standard library types are in a valid but unspecified state"
  />
  <ReferenceItem
    :id="7"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [vector.modifiers]"
    :year="2020"
    chapter="vector uses copy if move ctor is not noexcept"
  />
</ReferenceCard>
