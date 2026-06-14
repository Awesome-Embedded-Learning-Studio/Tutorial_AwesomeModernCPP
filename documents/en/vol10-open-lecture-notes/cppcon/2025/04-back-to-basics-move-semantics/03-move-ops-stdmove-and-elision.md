---
title: Move Operations, std::move, and Copy Elision
description: CppCon 2025 Talk Notes — Complete Implementation of Move Construction/Assignment,
  The Real Meaning of std::move, NRVO vs. C++17 Mandatory Copy Elision, and Moved-from
  State
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
order: 3
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/04-back-to-basics-move-semantics/03-move-ops-stdmove-and-elision.md
  source_hash: aa77a7851692af982bd553ebff0a041002f6647fa683abb87a989cc5d3357f06
  translated_at: '2026-06-14T00:17:55.581146+00:00'
  engine: anthropic
  token_count: 4573
---
# Move Operations, std::move, and Copy Elision

:::tip
This article is the third in the CppCon 2025 "Back to Basics: Move Semantics" series notes. The previous two parts discussed copy overhead vs. move motivation, and lvalues, rvalues, and the reference system. This part focuses on core practical issues: how to write move constructors and move assignment, what `std::move` actually does, and how C++17 copy elision changes the game.
:::

Honestly, I used to think I "understood" move semantics—isn't it just stealing pointers? How hard could it be? Until one day I saw a colleague write `return std::move(str);` in a code review. I casually said, "Nice, explicit move." Then a senior engineer next to me shut me down with one sentence: **"Are you sure that won't block NRVO?"**

I spent a whole night figuring it out—`std::move` doesn't help you optimize; instead, it turns a return value transfer that the compiler could have done at zero cost into an extra move construction. From that day on, I truly realized that the devil in move semantics is all in the details.

In this article, we will unpack these details one by one. Our experimental environment is Arch Linux WSL, GCC 16.1.1, with compiler flags `-std=c++23 -O0 -Wall -Wextra -pedantic`. If you plan to follow along and run the code, it is recommended to have this version or a newer compiler ready.

## Move Constructor: The Art of Stealing Pointers

In the previous article, we had complete copy operations for `MyString`. Now, let's add the move constructor. Using Ben Saks' words, what this function does is a **"destructive copy"**—we "steal" the source object's data and then leave the source object in a harmless state.

```cpp
MyString(MyString&& other) noexcept
    : _data{other._data}
    , _size{other._size}
{
    other._data = nullptr;
    other._size = 0;
}
```

Let's break down this code line by line, because every line exists for a reason.

First is the parameter type `MyString&&`—this is an rvalue reference. An rvalue reference can only bind to an rvalue (a temporary object, the result of `std::move`, etc.). This means the compiler will only call this constructor when it confirms the "source object is about to die." This is the first layer of safety guarantee in move semantics: the compiler helps you gatekeep through overload resolution.

Next is the initializer list. `_size{other._size}` takes the source object's length directly—`size_t` is a built-in type, so a "copy" is just an integer assignment, costing almost zero. `_data{other._data}` is the key: we assign the source object's pointer directly to the new object. The new object now points to the heap memory previously allocated by the source object. So far, both objects point to the same memory block—if we ended here, it would be a double delete, which is undefined behavior.

So those two lines in the function body are the soul. `other._data = nullptr;` nullifies the source object's pointer, and `other._size = 0;` resets the length to zero. This way, when the source object's destructor executes `delete _data`, it actually calls `delete nullptr`—and the standard explicitly states that deleting a null pointer is a safe no-op.

You may have noticed that although the move constructor parameter `other` is an rvalue reference, `other`'s destructor will still be called. This is a point many overlook: move operations don't mean "take over and ignore the source object." On the contrary, the source object after being moved is still a complete, valid object—it's just that its internal state was intentionally set by us to "harmless" values. It will still be destructed normally, but the destructor will release nothing.

## Overload Resolution: How Does the Compiler Choose?

With both copy constructor and move constructor versions available, how does the compiler choose when facing an initialization expression? The answer is overload resolution based on the value category of the argument.

```cpp
MyString a{"Hello"};
MyString b{a};      // (1) Copy constructor
MyString c{std::move(a)}; // (2) Move constructor
```

In the first line `MyString b{a};`, `a` is an lvalue—it has a name, and you can take its address. The compiler sees the argument is an lvalue, looks for a constructor that accepts `const MyString&`, and hits the copy constructor.

In the second line `MyString c{std::move(a)};`, the result of `std::move(a)` is an rvalue reference. The compiler looks for a constructor that accepts `MyString&&`, and hits the move constructor. This is why we need two constructors to coexist: the copy constructor handles "the source object will still be used," and the move constructor handles "the source object is going to die anyway."

Ben Saks emphasized a point in the talk: **An rvalue reference itself does not perform a move**. It only provides a signal to the compiler at the type system level—"this reference is bound to an rvalue." What really decides whether to copy or move is overload resolution. If our `MyString` didn't have a move constructor, `std::move(a)` would only trigger the copy constructor—the compiler would settle for the `const MyString&` version because `const MyString&` can accept an rvalue. It won't error, but it won't move either. This point will be mentioned again later.

## Move Assignment Operator: Clean Up the Old Object First

The move constructor handles the "create a new object" scenario, while the move assignment operator handles the "overwrite an existing object" scenario. Their core logic is similar, but move assignment has one extra step—you must clean up the target object's old resources first.

```cpp
MyString& operator=(MyString&& other) noexcept {
    if (this != &other) {
        delete _data;
        _data = other._data;
        _size = other._size;
        other._data = nullptr;
        other._size = 0;
    }
    return *this;
}
```

This order is important. We `delete _data` to release our previous heap memory first, and then take over the source object's pointer. If we did it in reverse—assign first then delete—we would delete the pointer the source object just gave us, which is a classic use-after-free.

The self-assignment check `if (this != &other)` is equally important in move assignment. Although `MyString&&` is an rvalue reference, theoretically no one should write `a = std::move(a);`, but the language doesn't forbid it, and sometimes template instantiation can produce this effect. Without the self-assignment check, `delete _data` would free our own memory, and then `_data = other._data;` would assign a dangling pointer back to us—instant crash.

Note the return type is `MyString&`—an lvalue reference, not an rvalue reference. This is because the target of the assignment operator (the object on the left side of `=`) is always an lvalue. Whether you use `std::move` or not, the receiver of the assignment is always "an object with a name and an address."

Additionally, this implementation is exception-safe—the `MyString` data members are only built-in types (`char*` and `size_t`), and operations on these types won't throw exceptions. This is why I marked it `noexcept`. If your class has more complex data members (like another `std::vector`), you need to consider exception safety carefully.

## std::move: The Most Misunderstood Function in C++

The name `std::move` is terribly misleading. When I first saw it, I naturally assumed it "performed a move operation"—after all, it's called "move." But the fact is, **`std::move` doesn't move anything**.

Its real identity is a cast from an lvalue reference to an rvalue reference. The standard library implementation is roughly equivalent to:

```cpp
template <typename T>
constexpr std::remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);
}
```

Ignoring the template gymnastics of `remove_reference_t`, the core is `static_cast<T&&>(t)`. It casts the passed argument to an rvalue reference and returns it. That's it. It generates no move code, calls no move constructors, and modifies no object state.

Ben Saks said a true thing in the talk: **If we could do it all over again, we'd probably call it `std::rval_cast` or `std::move_cast`**. That name wouldn't mislead people into thinking it performs a move.

### Why We Need std::move: The Naming Trap in swap

So if `std::move` doesn't move, why do we need it? Let's look at the `swap` function. This is the scenario that best illustrates the problem.

```cpp
void swap(MyString& a, MyString& b) {
    MyString tmp{a}; // Copy
    a = b;            // Copy
    b = tmp;          // Copy
}
```

This C++03 style `swap` performs three copies. We naturally want to change it to a move version—after all, our previous articles kept saying move is much faster than copy. But the problem arises: `a`, `b`, and `tmp` inside the function body are all lvalues. They all have names, you can take their addresses, and their lifetimes span multiple statements. The compiler can't automatically treat them as rvalues—what if you use `tmp` after the third line?

C++ has a general rule: **If something has a name, it is an lvalue**. Only things without names (like temporary objects, literals, function return-by-value results) can be rvalues. This rule is very reasonable—the compiler must be conservative; it can't assume `tmp` isn't used on the next line.

So we need to explicitly tell the compiler: "I know `tmp` won't be used after this, please treat it as an rvalue." This is exactly what `std::move` is for:

```cpp
void swap(MyString& a, MyString& b) {
    MyString tmp{std::move(a)};
    a = std::move(b);
    b = std::move(tmp);
}
```

Every `std::move` is passing a message to the compiler: **"Here, I confirm it is safe to move resources from this object."** Only after receiving this information will the compiler choose the move version in overload resolution.

### std::move Doesn't Guarantee a Move

There's another easily overlooked trap: `std::move` doesn't guarantee a move will actually happen. If a type only has copy operations and no move operations, the result of `std::move` will degrade to a copy.

```cpp
struct NoMove {
    int data;
    NoMove(const NoMove&) = default;
};

NoMove src{42};
NoMove dest = std::move(src); // Calls copy constructor!
```

Here `std::move(src)` casts `src` to an rvalue reference, but `NoMove` has no constructor accepting an rvalue reference. The compiler settles for the `const NoMove&` version of the copy constructor (because `const NoMove&` can bind to an rvalue). It won't error, but your expected "move" becomes a "copy"—and silently.

## The Naming Paradox of Rvalue Reference Parameters

This is the most confusing part of move semantics, and the content Ben Saks spent a lot of time emphasizing.

When we write a function that accepts an rvalue reference parameter, the parameter is treated as an **lvalue** inside the function:

```cpp
void sink(MyString&& str) {
    // str is an lvalue here!
    MyString internal{std::move(str)}; // Must use std::move again
}
```

From the perspective outside the function, the passed argument is an rvalue (like `std::move(x)` or a temporary). But once inside the function body, `str` is a named variable—it exists across multiple statements, and the compiler can't assume it's used only once. So the "named means lvalue" rule still applies.

This leads to a practical consequence: **Inside a function, if you want to move resources from an rvalue reference parameter, you must explicitly use `std::move`**. And once you move, the value of that parameter in subsequent code is unpredictable—this is the "moved-from" state discussed in the next section.

## Implicitly Movable Return Expressions

The good news is that the "named means lvalue" rule has an important exception—the `return` statement.

```cpp
MyString make_string() {
    MyString result{"Hello"};
    // ... do stuff ...
    return result; // Implicitly movable
}
```

In this code, `result` has a name (technically an lvalue), but `return result;` is the last use of `result` in the function. The compiler knows `result`'s lifetime ends immediately after the function returns, so the standard allows it to treat `result` as an **implicitly movable entity**.

This means you **do not need** to write `return std::move(result);`. Just `return result;` is enough—the compiler will automatically choose the move constructor (or an even better choice, directly eliminating this construction, discussed next).

## NRVO: An Optimization Better Than Move

Talking about "implicitly movable" actually doesn't go far enough. The compiler can actually do better than move—it can deliver the return value to the caller at **zero cost**, without even needing a move. This is the so-called **Named Return Value Optimization (NRVO)**.

```cpp
MyString create() {
    MyString str{"Hello"};
    return str;
}

MyString s = create();
```

In a world without NRVO, the execution flow is: first construct `str` on `create`'s stack frame, then construct a temporary object at the `return` location (via move or copy), then `str` destructs, then the temporary moves or copies to `s`, then the temporary destructs. Sounds wasteful.

NRVO's idea is very clever: when generating code, the compiler constructs `str` directly at `s`'s location. Not construct then copy, but put it in the right place from the start. `str` *is* `s`; they share the same memory. When the function returns, no copy or move is needed—the object is already where it should be.

Starting with C++17, this optimization became **mandatory** in certain contexts—the compiler must eliminate the copy, not "can eliminate but doesn't have to." This isn't an optional optimization; it's a defined behavior of the language. For historical reasons, it's still called "optimization," but it's actually a guarantee.

For complete technical details on NRVO and RVO, we have a dedicated article in vol2: [RVO and NRVO: Compiler Return Value Optimization](../../../../vol2-modern-features/ch00-move-semantics/03-rvo-nrvo.md).

## Never Use std::move on Return Values

This is probably the most common mistake I've seen related to move semantics. We said earlier that `return result;` is implicitly movable, and the compiler either does NRVO (zero cost) or automatically falls back to move construction (cost of one pointer assignment). Some people think: since `std::move` is "requesting a move," wouldn't `return std::move(result);` be more explicit and safer?

**Completely opposite.**

```cpp
MyString bad_create() {
    MyString result{"Hello"};
    return std::move(result); // Blocks NRVO!
}
```

The reason lies in NRVO's trigger conditions: the `return` expression must be the name of a local variable. When you write `return std::move(result);`, the return expression is no longer the name `result`—it's `std::move(result)`, a function call expression. The compiler cannot perform NRVO on this expression and can only settle for move construction.

In other words, `std::move(result)` forces the compiler down the move construction path, while `return result;` gives the compiler a chance at the NRVO path (zero cost). This is why Ben Saks repeatedly emphasized in the talk: **Don't use `std::move` on return values**.

We can use the `-fno-elide-constructors` compiler flag to compare the difference. This flag turns off GCC's copy elision optimization, letting us see what the world looks like "without NRVO."

First, look at `return result;` behavior with elision disabled—it falls back to move construction because `result` is implicitly movable. And `return std::move(result);` is also move construction—no difference when elision is disabled. But once elision is enabled (the default behavior), `return result;` becomes a no-op, while `return std::move(result);` remains a move construction. The gap is here.

I tested this with GCC 16.1.1, adding print logs to `MyString`'s various constructors. The comparison results are:

```text
// return result; (with NRVO)
Constructor called: 1x

// return std::move(result); (NRVO blocked)
Constructor called: 1x
Move constructor called: 1x
```

You see, `std::move(result)` explicitly adds one move construction. For a class like `MyString` with only pointers and integers, the move cost is low (one pointer assignment), but for more complex classes (like objects with multiple dynamic containers), the cost of this extra move cannot be ignored.

```text
// Both with -fno-elide-constructors
// return result;
Move constructor called: 1x

// return std::move(result);
Move constructor called: 1x
```

With NRVO disabled, both behave the same—both are one move construction. But this precisely shows that `std::move(result)` wastes the NRVO opportunity for free in the default case.

:::warning C++20/C++23 Further Expands "Implicitly Movable" Scope
The rule "Don't use `std::move` on return values" discussed in this section holds true in **all standard versions (C++11 to C++26)** and is absolutely safe advice. However, the "implicitly movable" mechanism itself is continuously strengthened in later standards, worth knowing: C++11 introduced initial implicit move (compiler treats returning a local object as a move); C++20 (proposal P1825 "More implicit moves") expanded the scope of "implicitly movable entities"—for example, local variables bound to rvalue references and `std::move` on a local object are also included in implicit move; C++23 (proposal P2266) further refined this, making return values treated as xvalues in certain scenarios, covering more construction paths.

But however these extensions change, **the iron rule "Don't write `std::move` when returning a local object" has never changed**—P1825/P2266 expand the scope of "what the compiler can automatically move," while `std::move` actually destroys NRVO's trigger conditions. Conclusion remains: write `return result;` and leave the choice of NRVO or implicit move to the compiler.
:::

## Moved-from State: Valid but Unspecified

After a move operation, the source object is in a state the standard calls **"valid but unspecified state"**. These words are worth breaking down one by one.

"Valid" means: no memory leaks, no resource leaks, no undefined behavior triggered. You can safely let this object destruct—its destructor will execute normally, no double free, no crash. For our `MyString`, after moving, `_data` is set to `nullptr` and `_size` becomes 0, so `delete _data` does nothing during destruction.

"Unspecified" means: you cannot make any assumptions about the value held by the moved-from object. The standard doesn't mandate that a moved-from `std::string` must be an empty string, nor that a moved-from `std::vector` must be empty. Different standard library implementations may have different behaviors. Our own `MyString` returns `true` for `empty()` after moving (our own safety fallback), but a moved-from `std::string` might return an empty string or the original value—you can't rely on it.

```cpp
MyString a{"Hello"};
MyString b{std::move(a)};

// a is in a valid but unspecified state
a.empty(); // Returns true (for our implementation)
// But don't rely on it!
```

:::warning Usage Limits of Moved-from Objects
When Ben Saks was asked in the Q&A "Can a moved-from object still be used?", his answer was very blunt: **After moving, the only things you should do to the source object are assign it a new value or let it destruct**. Any other operation (reading values, comparing, passing to other functions) is a gamble—you might win (the implementation happens to give you a predictable value) or you might lose (the implementation changes or you switch standard libraries). Don't gamble.

Don't confuse "valid" with "useful"—a moved-from object is a legal object, but not an object with determined content. If you need an empty object, create one explicitly; if you need a specific value, assign it explicitly. Don't count on move operations to do these for you.
:::

## The Importance of noexcept: The Hidden Trap of Vector Reallocation

Finally, let's talk about a problem often ignored in actual engineering but with huge impact: **move constructors should be `noexcept`**.

Why? Look at the `std::vector` reallocation scenario. When `std::vector`'s capacity is insufficient, it needs to allocate a larger block of memory and then transfer the old elements to the new memory. If the element's move constructor is `noexcept`, `std::vector` will use move to transfer—very fast. If the move constructor is not `noexcept`, `std::vector` will fall back to copy.

This is because `std::vector` must provide a strong exception safety guarantee: if an exception is thrown during reallocation, `std::vector`'s state must roll back to before reallocation. If move is used, once an exception is thrown mid-way, the moved elements can't be restored (their resources have been stolen). If copy is used, the original data is still there and can be safely rolled back.

Let's write a simple test to verify this behavior:

```cpp
std::vector<MyString> vec;
vec.reserve(2); // Reserve space for 2

vec.emplace_back("A");
vec.emplace_back("B");
vec.emplace_back("C"); // Triggers reallocation
```

After compiling and running, you will see output like this (GCC 16.1.1, `-std=c++23 -O0`):

```text
Copy constructor called
Copy constructor called
```

See? When the third element triggers reallocation, `std::vector` **copied** the first two elements to the new memory—even though we explicitly implemented a move constructor. The reason is our move constructor wasn't marked `noexcept`.

Now add `noexcept` to the move constructor:

```cpp
MyString(MyString&& other) noexcept
    : _data{other._data}
    , _size{other._size}
{
    other._data = nullptr;
    other._size = 0;
}
```

Recompile and run:

```text
Move constructor called
Move constructor called
```

The difference of one `noexcept` keyword directly determines whether `std::vector` copies or moves during reallocation. For a class holding dynamic memory, in large data scenarios, this difference can mean an order of magnitude performance gap.

This is a real production-level trap. Many people write move constructors but forget to add `noexcept`, then wonder in performance tests "why move semantics didn't take effect." The answer often lies in these two words.

## Complete MyString: The Big Five Gathered

Combining this article and the previous two, we get a complete, Rule of Five-compliant `MyString` implementation:

```cpp
class MyString {
public:
    // Constructor
    explicit MyString(const char* str = "")
        : _size{std::strlen(str)}
        , _data{new char[_size + 1]}
    {
        std::strcpy(_data, str);
    }

    // Destructor
    ~MyString() {
        delete _data;
    }

    // Copy constructor
    MyString(const MyString& other)
        : _size{other._size}
        , _data{new char[_size + 1]}
    {
        std::strcpy(_data, other._data);
    }

    // Copy assignment
    MyString& operator=(const MyString& other) {
        if (this != &other) {
            delete _data;
            _size = other._size;
            _data = new char[_size + 1];
            std::strcpy(_data, other._data);
        }
        return *this;
    }

    // Move constructor
    MyString(MyString&& other) noexcept
        : _data{other._data}
        , _size{other._size}
    {
        other._data = nullptr;
        other._size = 0;
    }

    // Move assignment
    MyString& operator=(MyString&& other) noexcept {
        if (this != &other) {
            delete _data;
            _data = other._data;
            _size = other._size;
            other._data = nullptr;
            other._size = 0;
        }
        return *this;
    }

    size_t size() const { return _size; }
    bool empty() const { return _size == 0; }

private:
    char* _data;
    size_t _size;
};
```

Five special member functions—destructor, copy constructor, copy assignment, move constructor, move assignment—all present. This is the so-called Rule of Five: if you need to customize any one of them, you most likely need to customize all five. The compiler-generated default versions are unsafe for classes holding raw pointers.

## What We've Cleared Up

Three articles down, we started from the three deep copies of `std::string`, passed through the value category system of lvalues and rvalues, and finally unpacked all implementation details of move operations in this article. Let me use a concise list to review this article's core points.

The move constructor's core is "destructive copy"—steal the source object's resource pointer, then set the source object to a harmless state. Overload resolution automatically selects copy or move; you don't need to make extra judgments at the call site. `std::move` doesn't move anything; it's just a cast to an rvalue reference, enabling overload resolution to select the move version. Rvalue reference parameters are lvalues inside the function—because they have names—so you still need `std::move` to move from them. The `return` statement is the exception to the "named means lvalue" rule; the compiler automatically recognizes implicitly movable return expressions. NRVO can deliver return values to the caller at zero cost—and `std::move` blocks NRVO, so never write it that way. Moved-from objects are in a "valid but unspecified" state; the only safe operations are assign a new value or destruct. Move constructors must be marked `noexcept`—otherwise `std::vector` reallocation falls back to copy, and the performance gap can be huge.

If you want to continue deeper into more application scenarios of move semantics—perfect forwarding, universal references, reference collapsing—check out vol2's [Perfect Forwarding: Precise Transmission of Value Categories](../../../../vol2-modern-features/ch00-move-semantics/04-perfect-forwarding.md). Move semantics combined with perfect forwarding form the complete foundation of modern C++ template programming.

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
    chapter="Standard library types' moved-from objects are in a valid but unspecified state"
  />
  <ReferenceItem
    :id="7"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [vector.modifiers]"
    :year="2020"
    chapter="vector uses copy if move ctor is not noexcept"
  />
</ReferenceCard>
