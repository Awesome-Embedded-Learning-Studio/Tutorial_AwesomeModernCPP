---
chapter: 4
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: CppCon 2025 Presentation Notes — Complete Implementation of Move Construction/Assignment,
  The True Meaning of std::move, NRVO and C++17 Mandatory Copy Elision, Moved-from
  State
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
  source_hash: a8bf721af9dd5ce9a4ab31451951bcb02d4b7b45889d0cf598f4277a1b27bcfb
  translated_at: '2026-06-24T01:17:26.367523+00:00'
  engine: anthropic
  token_count: 4576
---
# Move Operations, std::move, and Copy Elision

:::tip
This article is the third in the series of notes from CppCon 2025 "Back to Basics: Move Semantics". The previous two parts discussed copy overhead and move motivation, as well as lvalues, rvalues, and the reference system. This part focuses on core practical issues: how to write move constructors and move assignments, what exactly `std::move` does, and how C++17 copy elision changes the rules of the game.
:::

Honestly, I used to think I "understood" move semantics—isn't it just stealing pointers? How hard could it be? Then one day, I saw a colleague write `return std::move(result);` in a code review. I casually remarked, "Nice, explicit move," only to be shut down by a senior engineer next to me: **"Are you sure that won't inhibit NRVO?"**

After spending a whole evening digging into it, I finally realized—`return std::move(result)` doesn't help optimization; instead, it turns a return value transfer that the compiler could have completed at zero cost into an extra move construction. From that day on, I truly realized that the devil in move semantics is entirely in the details.

In this article, we will unpack these details one by one. Our experimental environment is Arch Linux WSL, GCC 16.1.1, with the compiler flag `-std=c++20`. If you plan to follow along and run the code, we recommend using this version or a newer compiler.

## Move Constructor: The Art of Stealing Pointers

In the previous article, we implemented the complete copy operations for `MyString`. Now, let's add a move constructor to it. Using Ben Saks' words, the task of this function is a "**destructive copy**"—we "steal" the data from the source object and then leave the source object in a harmless state.

```cpp
class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // ... 之前的构造函数、析构函数、拷贝操作 ...

    // 移动构造函数
    MyString(MyString&& s) noexcept
        : stored_length_(s.stored_length_)
        , actual_str_(s.actual_str_)
    {
        s.actual_str_ = nullptr;
        s.stored_length_ = 0;
    }
};
```

Let's break down this code line by line, as every line serves a specific purpose.

First, the parameter type `MyString&& s`—this is an rvalue reference. An rvalue reference can only bind to an rvalue (a temporary object, the result of `std::move`, etc.), which means this constructor is called only when the compiler confirms that the "source object is about to die." This is the first layer of safety guarantees provided by move semantics: the compiler helps enforce this via overload resolution.

Next is the member initializer list. `stored_length_(s.stored_length_)` takes the source object's length directly—since `std::size_t` is a built-in type, this "copy" is just an integer assignment, which is virtually zero-cost. `actual_str_(s.actual_str_)` is the key: we assign the source object's pointer directly to the new object, so the new object now points to the heap memory previously allocated by the source. At this point, both objects point to the same memory block—if we stopped here, we would have a double delete, leading to undefined behavior.

Therefore, the two lines inside the function body are the crux of the matter. `s.actual_str_ = nullptr` nullifies the source object's pointer, and `s.stored_length_ = 0` resets the length. This way, when the source object's destructor executes `delete[] actual_str_`, it is actually calling `delete[] nullptr`—and the standard explicitly states<RefLink :id="1" preview="C++ Standard, [expr.delete] — deleting a null pointer has no effect" /> that deleting a null pointer is a safe no-op.

You may have noticed that although the move constructor's parameter `s` is an rvalue reference, `s`'s destructor will still be called. This is a point many overlook: a move operation doesn't mean "take over and forget about the source object." On the contrary, the source object remains a valid, legal object after the move—except we have intentionally set its internal state to a "harmless" value. It will still be destructed normally, but the destructor will release nothing.

## Overload Resolution: How Does the Compiler Choose?

With both copy constructor and move constructor versions available, how does the compiler choose when facing an initialization expression? The answer is overload resolution based on the value category of the argument<RefLink :id="2" preview="C++ Standard, [over.match] — overload resolution selects the best viable function" />.

```cpp
MyString s1("hello");

// s1 是左值（有名字）→ 调用拷贝构造函数
MyString s2(s1);

// std::move(s1) 是右值 → 调用移动构造函数
MyString s3(std::move(s1));
```

In the first line, `MyString s2(s1)`, `s1` is an lvalue—it has a name, and we can take its address. The compiler sees that the argument is an lvalue, looks for a constructor that accepts `const MyString&`, and finds the copy constructor.

In the second line, `MyString s3(std::move(s1))`, the result of `std::move(s1)` is an rvalue reference. The compiler looks for a constructor that accepts `MyString&&`, and finds the move constructor. This is why we need both constructors to coexist: the copy constructor handles cases where "the source object will still be used," while the move constructor handles cases where "the source object is going to die anyway."

Ben Saks emphasized a key point in his talk: **an rvalue reference does not perform a move on its own**. It merely provides a signal to the compiler at the type system level—"this reference is bound to an rvalue." What actually decides between copying or moving is overload resolution. If our `MyString` lacked a move constructor, `std::move(s1)` would only trigger the copy constructor—the compiler would fall back to the `const MyString&` version, because `MyString&&` can be accepted by `const MyString&`. It would not error, but it would not move. We will revisit this point later.

## Move Assignment Operator: Clean Up the Old Object First

The move constructor handles scenarios where we are "creating a new object," whereas move assignment handles scenarios where we are "overwriting an existing object." Their core logic is similar, but move assignment has an extra step: we must clean up the old resources of the target object first.

```cpp
MyString& operator=(MyString&& s) noexcept
{
    if (this != &s) {
        delete[] actual_str_;         // 第一步：清理自己的旧资源
        stored_length_ = s.stored_length_;
        actual_str_ = s.actual_str_;  // 第二步：偷源对象的资源
        s.actual_str_ = nullptr;      // 第三步：置空源对象
        s.stored_length_ = 0;
    }
    return *this;
}
```

The order here is critical. We first release our previous heap memory with `delete[] actual_str_`, and then take over the source object's pointer. If we reversed the order—assigning first and then deleting—we would delete the pointer given to us by the source object, which is a classic use-after-free scenario.

The self-assignment check `if (this != &s)` is equally important in move assignment. Although `s` is an rvalue reference and theoretically no one should write code like `x = std::move(x)`, the language does not prohibit it, and sometimes template instantiation can produce this effect. Without the self-assignment check, `delete[] actual_str_` would free our own memory, and then `actual_str_ = s.actual_str_` would assign a dangling pointer back to ourselves—resulting in an immediate crash.

Note that the return type is `MyString&`—an lvalue reference, not an rvalue reference. This is because the target of the assignment operator (the object on the left side of `=`) is always an lvalue. Whether or not we use `std::move`, the recipient of the assignment is always "a named object with an address."

Additionally, this implementation is exception-safe—the data members of `MyString` are only built-in types (`std::size_t` and `char*`), and operations on these types do not throw exceptions. This is also why I marked it `noexcept`. If your class has more complex data members (like another `std::string`), you need to consider exception safety more carefully.

## std::move: The Most Misunderstood Function in C++

The name `std::move` is terribly misleading. When I first saw it, I naturally assumed it "performs a move operation"—after all, it's called "move". But the fact is, **`std::move` does not move anything itself**.

Its real identity is a type cast to an rvalue reference, equivalent to a `static_cast`. The standard library implementation is roughly equivalent to:

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&& move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

Setting aside the template metaprogramming of `remove_reference`, the core is simply `static_cast<T&&>(t)`. It casts the passed argument to an rvalue reference and returns it. That is all. It generates no move code, invokes no move constructors, and does not modify the state of any object.

Ben Saks stated a plain truth in his talk: **If we could do it all over again, we would probably call it `make_movable` or `as_rvalue`**. At the very least, this name wouldn't mislead anyone into thinking it performs a move.

### Why we need std::move: The naming trap in swap

Since `std::move` doesn't actually move, why do we need it? Let's look at the `swap` function. This scenario illustrates the problem best.

```cpp
template<typename T>
void swap(T& x, T& y)
{
    T temp(x);              // (1)
    x = y;                  // (2)
    y = temp;               // (3)
}
```

This C++03 style `swap` performs three copies. We certainly want to change it to a move version—after all, our previous two articles emphasized that moving is much faster than copying. However, a problem arises: `x`, `y`, and `temp` inside the function body are all lvalues. They all have names, you can take their addresses, and their lifetimes span multiple statements. The compiler cannot automatically treat them as rvalues—what if you still need to use `temp` after the third line?

C++ has a general rule: **if something has a name, it is an lvalue**. Only nameless things (like temporary objects, literals, or function results returned by value) can be rvalues. This rule is very reasonable—the compiler must be conservative; it cannot assume that `temp` will not be used in the next line.

Therefore, we need to explicitly tell the compiler: "I know `temp` will not be used afterwards, so please treat it as an rvalue." This is exactly what `std::move` is for:

```cpp
template<typename T>
void move_swap(T& x, T& y)
{
    T temp(std::move(x));    // 移动构造 temp
    x = std::move(y);        // 移动赋值 x
    y = std::move(temp);     // 移动赋值 y
}
```

Every `std::move` sends a message to the compiler: **"Here, I confirm that it is safe to move resources from this object."** Only after receiving this information will the compiler select the moving version during overload resolution.

### std::move Does Not Guarantee a Move

There is another easily overlooked pitfall: `std::move` does not guarantee that a move will actually occur. If a type only has copy operations and no move operations, the result of `std::move` will degrade to a copy.

```cpp
struct CopyOnly
{
    CopyOnly() = default;
    CopyOnly(const CopyOnly&) { std::cout << "copy\n"; }
    // 没有移动构造函数！
};

CopyOnly a;
CopyOnly b(std::move(a));  // 输出 "copy" —— 退化为拷贝构造
```

Here, `std::move(a)` casts `a` to an rvalue reference, but `CopyOnly` does not have a constructor accepting an rvalue reference. The compiler falls back to the copy constructor taking `const CopyOnly&` (because `CopyOnly&&` can bind to `const CopyOnly&`). This won't cause an error, but the "move" you expected silently turns into a "copy."

## The Naming Paradox of Rvalue Reference Parameters

This is one of the most confusing aspects of move semantics, and it's a point Ben Saks spent considerable time emphasizing.

When we write a function that accepts an rvalue reference parameter, that parameter is treated as an **lvalue** inside the function body:

```cpp
void process(MyString&& s)
{
    // s 有名字 → s 是左值
    MyString copy(s);             // 调用拷贝构造！不是移动构造！
    MyString moved(std::move(s)); // 这才调用移动构造
}
```

From the perspective of the caller, the passed argument is an rvalue (for example, `process(std::move(x))` or `process(MyString("temp"))`). However, once inside the function body, `s` becomes a named variable—it persists across multiple statements, and the compiler cannot assume it will be used only once. Therefore, the rule that "named variables are lvalues" still applies.

This leads to a practical consequence: **inside the function, if you want to move resources from an rvalue reference parameter, you must explicitly use `std::move`**. Furthermore, once you have moved from it, the value of that parameter in subsequent code becomes unpredictable—this is the "moved-from" state we will discuss in the next section.

## Implicitly Movable Return Expressions

The good news is that there is an important exception to the "named variables are lvalues" rule—the `return` statement.

```cpp
MyString make_greeting()
{
    MyString temp("hello world");
    // ... 对 temp 做一些操作 ...
    return temp;  // 不需要 std::move！
}
```

In this code, although `temp` has a name (which technically makes it an lvalue), `return temp;` is the last use of `temp` within the function. The compiler knows that `temp`'s lifetime ends immediately after the function returns, so the standard allows it to treat `temp` as an implicitly movable entity <RefLink :id="3" preview="C++ Standard, [class.copy.elision] — NRVO and implicit move" />.

This means we **do not** need to write `return std::move(temp);`. Simply writing `return temp;` is sufficient—the compiler will automatically select the move constructor (or even better, it will elide the construction entirely, which we will discuss next).

## NRVO: An Optimization Better Than Moving

Discussing "implicitly movable" entities isn't the end of the story. The compiler can actually do better than moving—it can allow the return value to reach the caller at **zero cost**, without even requiring a move. This is known as **Named Return Value Optimization (NRVO)**.

```cpp
MyString make_greeting()
{
    MyString temp("hello world");
    return temp;
}

MyString s = make_greeting();
```

In a world without NRVO, the execution flow looks like this: first, construct `temp` on the stack frame of `make_greeting`, then construct a temporary object at the location of `s` (via move or copy), then destroy `temp`, then move or copy the temporary object into `s`, and finally destroy the temporary object. That sounds wasteful.

The idea behind NRVO is very clever: when generating code, the compiler constructs `temp` directly at the location of `s`. Instead of constructing first and then copying, it places the object in the correct spot from the very beginning. `temp` *is* `s`; they share the exact same memory. When the function returns, no copy or move is necessary—the object is already exactly where it belongs.

Starting with C++17, this optimization became **mandatory** in certain scenarios<RefLink :id="4" preview="C++ Standard, [class.copy.elision] — mandatory elision in certain contexts" />—the compiler must elide the copy, rather than "can elide but might choose not to." This is not an optional optimization, but a defined behavior of the language. Historical reasons keep the name "optimization," but in reality, it is a guarantee.

For the complete technical details regarding NRVO and RVO, we have a dedicated article in Volume 2: [RVO and NRVO: Compiler Return Value Optimization](../../../../vol2-modern-features/ch00-move-semantics/03-rvo-nrvo.md).

## Never Use std::move on Return Values

This is arguably the most common mistake I see related to move semantics. As mentioned earlier, `return temp;` is implicitly movable. The compiler either performs NRVO (zero cost) or automatically falls back to move construction (the cost of a single pointer assignment). One might think: since `std::move` "requests a move," wouldn't `return std::move(temp);` be more explicit and safer?

**Quite the opposite.**

```cpp
// 正确写法：允许 NRVO
MyString make_good()
{
    MyString temp("good");
    return temp;
}

// 错误写法：阻止 NRVO！
MyString make_bad()
{
    MyString temp("bad");
    return std::move(temp);  // 反而更慢！
}
```

The reason lies in the trigger conditions for NRVO<RefLink :id="5" preview="C++ Standard, [class.copy.elision] — the return expression must be the name of a local variable" />: the `return` expression must be the name of a local variable. When we write `return std::move(temp);`, the return expression is no longer the name `temp`—it is `std::move(temp)`, which is a function call expression. The compiler cannot perform NRVO on this expression, so it falls back to move construction.

In other words, `return std::move(temp);` forces the compiler to take the move construction path, whereas `return temp;` gives the compiler a chance to take the NRVO path (zero cost). This is why Ben Saks emphasized repeatedly in his talk: **do not use `std::move` on return values**.

We can use the `-fno-elide-constructors` compiler flag to compare the difference between the two. This flag disables GCC's copy elision optimization, allowing us to see what the world looks like "without NRVO."

First, let's look at the behavior of `return temp;` with elision disabled—it falls back to move construction because `temp` is implicitly movable. Meanwhile, `return std::move(temp);` also results in move construction—there is no difference between the two when elision is disabled. However, once elision is enabled (the default behavior), `return temp;` becomes a no-op, while `return std::move(temp);` still performs move construction. That is where the difference lies.

I tested this with GCC 16.1.1. After adding print logs to the various constructors of `MyString`, the comparison results are as follows:

```bash
# 默认开启 NRVO
$ g++ -std=c++20 -O2 test.cpp && ./a.out
=== return temp; (NRVO) ===
  构造: "hello"          # 只有这一次构造，没有移动，没有拷贝

=== return std::move(temp); ===
  构造: "hello"
  移动构造: "hello"       # 多了一次移动构造！
  析构: "(null)"
```

You see, `return std::move(temp);` explicitly adds one move construction. For a class like `MyString`, which only contains a pointer and an integer, the cost of move construction is very low (just one pointer assignment). However, for more complex classes (such as objects containing multiple dynamic containers), the cost of this extra move cannot be ignored.

```bash
# 关闭 NRVO 后对比
$ g++ -std=c++20 -O2 -fno-elide-constructors test.cpp && ./a.out
=== return temp; ===
  构造: "hello"
  移动构造: "hello"       # 没有 NRVO，退回到移动构造
  析构: "(null)"

=== return std::move(temp); ===
  构造: "hello"
  移动构造: "hello"       # 同样是移动构造
  析构: "(null)"
```

With NRVO disabled, both behaviors are indeed identical—both involve a single move construction. However, this precisely demonstrates that `return std::move(temp);` needlessly wastes an opportunity for NRVO under default settings.

:::warning C++20/C++23 Further Expand the Scope of "Implicitly Movable"
The rule discussed in this section—"Don't use `std::move` on return values"—**holds true across all standard versions (C++11 through C++26)** and is absolutely safe advice. However, the mechanism of "implicit move" itself is continuously strengthened in later standards, and it is worth noting: C++11 introduced the initial implicit move (where the compiler can treat returning a local object as a move); C++20 (proposal P1825 "More implicit moves") expanded the scope of "implicitly movable entities"—for example, local variables bound to rvalue references and throwing a local object are now included in implicit moves; C++23 (proposal P2266) further refined this, allowing return values to be treated as xvalues in certain scenarios, covering more construction paths.

However, regardless of these extensions, **the iron rule "do not write `std::move` when returning a local object" has never changed**—P1825/P2266 expand the scope of "what the compiler can automatically move," whereas `std::move` actually disrupts the conditions for triggering NRVO. The conclusion remains: write `return temp;` and leave the choice between NRVO and implicit move to the compiler.
:::

## Moved-From State: Valid but Unspecified

After a move operation, the source object enters a state that the standard calls "**valid but unspecified state**" <RefLink :id="6" preview="C++ Standard, [lib.types.movedfrom] — moved-from objects are in a valid but unspecified state" />. These words are worth dissecting one by one.

"Valid" means: there will be no memory leaks, no resource leaks, and no undefined behavior. You can safely let this object be destroyed—its destructor will execute normally, without double freeing or crashing. For our `MyString`, after the move, `actual_str_` is set to `nullptr` and `stored_length_` becomes 0, so `delete[] nullptr` does nothing during destruction.

"Unspecified" means: you cannot make any assumptions about the value held by the moved-from object. The standard does not mandate that a moved-from `std::string` must be an empty string, nor does it mandate that a moved-from `std::vector` must be empty. Different standard library implementations may exhibit different behaviors. Our own `MyString` returns `"(null)"` after a move (which is our own safety fallback), but a moved-from `std::string` might return an empty string, or it might return the original value—you cannot rely on it.

```cpp
MyString a("hello");
MyString b(std::move(a));

// 安全操作：
// 1. 析构 —— 永远安全
// 2. 赋新值 —— 永远安全
a = MyString("new value");  // OK

// 不安全操作：
// 1. 假设 a 仍持有 "hello"
// 2. 假设 a.size() 是 0
// 3. 假设 a.c_str() 返回空串
// 这些假设在某些实现上可能碰巧成立，但标准不保证
```

:::warning Restrictions on moved-from objects
In a Q&A session, Ben Saks was asked if a moved-from object can still be used. His answer was unequivocal: **After moving, the only things you should do with the source object are assign a new value to it or let it be destroyed**. Any other operation (reading the value, comparing, passing it to other functions) is a gamble—you might win (if the implementation happens to give you a predictable value), or you might lose (if the implementation changes or you switch standard libraries). Don't gamble.

Do not confuse "valid" with "useful"—a moved-from object is a valid object, but it is not an object with a determined state. If you need an empty object, create one explicitly; if you need a specific value, assign it explicitly. Don't rely on move operations to do this for you.
:::

## The importance of `noexcept`: the hidden trap of vector reallocation

Finally, let's discuss a problem often overlooked in real-world engineering but with significant impact: **move constructors should be `noexcept`**.

Why? Let's look at the scenario of `std::vector` reallocation. When a `vector`'s capacity is insufficient, it needs to allocate a larger block of memory and transfer the old elements to the new memory. If the element's move constructor is `noexcept`, the `vector` will use move operations to transfer them—very fast. If the move constructor is not `noexcept`, the `vector` will fall back to copying<RefLink :id="7" preview="C++ Standard, [vector.modifiers] — if move ctor is not noexcept, vector uses copy during reallocation" />.

This is because `vector` must provide a strong exception safety guarantee: if an exception is thrown during reallocation, the `vector`'s state must be rolled back to before the reallocation. If moving is used, once an exception is thrown midway, the moved elements cannot be restored (their resources have already been stolen). If copying is used, the original data is still intact, allowing for a safe rollback.

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

    // 没有 noexcept！
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
    vec.emplace_back("CCC");  // 这里触发扩容

    std::cout << "\n=== final contents ===\n";
    for (const auto& s : vec) {
        std::cout << "  " << s.c_str() << "\n";
    }
    return 0;
}
```

After compiling and running, you will see output similar to this (GCC 16.1.1, `-std=c++20 -O2`):

```bash
$ g++ -std=c++20 -O2 test_noexcept.cpp && ./a.out
=== push 3 elements (triggers reallocation) ===
  ctor: AAA
  ctor: BBB
  ctor: CCC
  COPY ctor: AAA    # 扩容时用的是拷贝！不是移动！
  COPY ctor: BBB
```

Did you see that? When the third element triggered reallocation, `vector` **copied** the first two elements to the new memory—even though we explicitly implemented a move constructor. The reason is that our move constructor is not marked `noexcept`.

Now, let's add `noexcept` to the move constructor:

```cpp
StringNoNoexcept(StringNoNoexcept&& o) noexcept  // 加上 noexcept
```

Rebuild and run:

```bash
$ g++ -std=c++20 -O2 test_noexcept.cpp && ./a.out
=== push 3 elements (triggers reallocation) ===
  ctor: AAA
  ctor: BBB
  ctor: CCC
  MOVE ctor: AAA    # 现在用移动了！
  MOVE ctor: BBB
```

A single difference with the `noexcept` keyword directly determines whether a `vector` uses copy or move during reallocation. For a class managing dynamic memory, this difference can translate to an order-of-magnitude performance gap when dealing with large volumes of data.

This is a genuine production-level pitfall. Many developers write move constructors but forget to add `noexcept`, only to be puzzled during performance tests by "why move semantics didn't kick in." The answer often lies in these two words.

## Complete MyString: The Rule of Five Assembled

Combining the content from this and the previous two posts, we arrive at a complete `MyString` implementation that adheres to the Rule of Five:

```cpp
#include <cstring>
#include <utility>

class MyString
{
    std::size_t stored_length_;
    char* actual_str_;

public:
    // 构造函数
    explicit MyString(const char* s = "")
        : stored_length_(std::strlen(s))
        , actual_str_(new char[stored_length_ + 1])
    {
        std::memcpy(actual_str_, s, stored_length_ + 1);
    }

    // 析构函数
    ~MyString()
    {
        delete[] actual_str_;
    }

    // 拷贝构造函数
    MyString(const MyString& other)
        : stored_length_(other.stored_length_)
        , actual_str_(new char[other.stored_length_ + 1])
    {
        std::memcpy(actual_str_, other.actual_str_, stored_length_ + 1);
    }

    // 移动构造函数 —— noexcept！
    MyString(MyString&& s) noexcept
        : stored_length_(s.stored_length_)
        , actual_str_(s.actual_str_)
    {
        s.actual_str_ = nullptr;
        s.stored_length_ = 0;
    }

    // 拷贝赋值运算符
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

    // 移动赋值运算符 —— noexcept！
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

The five special member functions—destructor, copy constructor, copy assignment, move constructor, and move assignment—are all present. This is known as the **Rule of Five**: if you need to customize any one of them, you most likely need to customize all five. The compiler-generated default versions are unsafe for classes holding raw pointers.

## What We've Learned So Far

Across three articles, we started with the three copies involved in `swap`, navigated the value category system of lvalues and rvalues, and finally dissected the full implementation details of move operations in this article. Let me use a concise checklist to review the core points of this article.

The core of the move constructor is "destructive copy"—stealing the source object's resource pointer and then leaving the source object in a harmless state. Overload resolution automatically selects between copy and move; you don't need to make extra judgments at the call site. `std::move` doesn't move anything; it is just a cast to an rvalue reference that enables overload resolution to select the move version. An rvalue reference parameter is an lvalue inside a function—because it has a name—so you still need `std::move` to move from it. The `return` statement is an exception to the "named is lvalue" rule; the compiler automatically recognizes implicitly movable return expressions. NRVO (Named Return Value Optimization) allows the return value to reach the caller at zero cost—whereas `return std::move(temp)` inhibits NRVO, so never write it that way. A moved-from object is in a "valid but unspecified" state; the only safe operations are assigning a new value or destruction. Move constructors must be marked `noexcept`—otherwise `std::vector` will fall back to copying during reallocation, which can cause a massive performance difference.

If you want to dive deeper into more applications of move semantics—perfect forwarding, universal references, reference collapsing—check out vol2's [Perfect Forwarding: Preserving Value Categories](../../../../vol2-modern-features/ch00-move-semantics/04-perfect-forwarding.md). Move semantics combined with perfect forwarding form the complete foundation of modern C++ template programming.

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
    chapter="Standard library moved-from objects are in a valid but unspecified state"
  />
  <ReferenceItem
    :id="7"
    author="ISO/IEC 14882:2020"
    title="C++ Standard, [vector.modifiers]"
    :year="2020"
    chapter="vector uses copy if move ctor is not noexcept"
  />
</ReferenceCard>
