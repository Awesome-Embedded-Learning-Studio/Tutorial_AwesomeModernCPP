---
title: Move Operations, std::move, and Copy Elision
description: CppCon 2025 Talk Notes — Complete Implementation of Move Construction/Assignment,
  The True Meaning of std::move, NRVO and C++17 Mandatory Copy Elision, and Moved-From
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
  source_hash: 202e126a92dd9bcd611e0fd3c61f57e908558f75363044ec384e65e702c49e25
  translated_at: '2026-06-13T02:18:18.007260+00:00'
  engine: anthropic
  token_count: 4572
---
# Move Operations, std::move, and Copy Elision

:::tip
This article is the third in a series of notes from CppCon 2025's "Back to Basics: Move Semantics" talk. The first two articles discussed copy overhead and the motivation for moving, as well as lvalues, rvalues, and the reference system. This installment focuses on a core practical question: how to write move constructors and move assignment operators, what `std::move` actually does, and how C++17's copy elision changes the game.
:::

Honestly, I used to think I "understood" move semantics — isn't it just stealing pointers, how hard could it be? Until one day in a code review, I saw a colleague write `return std::move(result);`, and I casually said, "Nice, explicitly moved." Then a senior engineer next to me shut me down with one sentence: **"Are you sure writing it that way won't prevent NRVO?"**

It took me a whole evening to figure it out — `return std::move(result)` doesn't help you optimize at all. Instead, it turns a return value transfer that the compiler could have done at zero cost into an extra move construction. From that day on, I truly realized that the devil of move semantics is entirely in the details.

In this article, we will break down these details one by one. Our test environment is Arch Linux WSL, GCC 16.1.1, with the compiler flag `-std=c++20`. If you plan to follow along and run the code, we recommend having this version or a newer compiler ready.

## Move Constructors: The Art of Stealing Pointers

In the previous article, we already had complete `MyString` copy operations. Now let's add a move constructor. What this function does, in Ben Saks' words, is a **"destructive copy"** — we "steal" the source object's data, and then leave the source object in a harmless state.

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

Let's break down this code line by line, because every line exists for a reason.

First is the parameter type `MyString&& s` — this is an rvalue reference. An rvalue reference can only bind to an rvalue (a temporary object, the result of `std::move`, etc.), which means this constructor is only called when the compiler confirms that "the source object is about to die." This is the first layer of safety guarantee in move semantics: the compiler gates it for you through overload resolution.

Next is the initializer list. `stored_length_(s.stored_length_)` directly takes the source object's length — `std::size_t` is a built-in type, so the so-called "copy" is just an integer assignment, at nearly zero cost. `actual_str_(s.actual_str_)` is the key part: we directly assign the source object's pointer to the new object, so the new object now points to the heap memory previously allocated by the source object. So far, both objects point to the same memory — if we ended here, that would be a double delete, which is undefined behavior (UB).

So the two lines in the function body are the soul. `s.actual_str_ = nullptr` nullifies the source object's pointer, and `s.stored_length_ = 0` resets the length to zero. This way, when the source object's destructor executes `delete[] actual_str_`, it actually calls `delete[] nullptr` — and the standard explicitly states <RefLink :id="1" preview="C++ Standard, [expr.delete] — deleting a null pointer has no effect" /> that deleting a null pointer is a safe no-op.

You might have noticed that even though the move constructor's parameter `s` is an rvalue reference, `s`'s destructor will still be called. This is a point many people overlook: a move operation does not mean "once you take over, you don't need to care about the source object anymore." On the contrary, after the move is complete, the source object is still a complete, valid object — it's just that we intentionally set its internal state to "harmless" values. It will still be destructed normally, except that nothing will be freed during destruction.

## Overload Resolution: How Does the Compiler Choose?

With both copy and move constructor versions available, how does the compiler choose when facing an initialization expression? The answer is overload resolution based on the value category of the argument <RefLink :id="2" preview="C++ Standard, [over.match] — overload resolution selects the best viable function" />.

```cpp
MyString s1("hello");

// s1 是左值（有名字）→ 调用拷贝构造函数
MyString s2(s1);

// std::move(s1) 是右值 → 调用移动构造函数
MyString s3(std::move(s1));
```

In the first line, `MyString s2(s1)`, `s1` is an lvalue — it has a name, and you can take its address. The compiler sees that the argument is an lvalue, looks for a constructor that accepts `const MyString&`, and hits the copy constructor.

In the second line, `MyString s3(std::move(s1))`, the result of `std::move(s1)` is an rvalue reference. The compiler looks for a constructor that accepts `MyString&&`, and hits the move constructor. This is why we need both constructors to coexist: the copy constructor handles the case where "the source object will continue to be used," and the move constructor handles the case where "the source object is going to die anyway."

Ben Saks particularly emphasized one point in his talk: **an rvalue reference does not perform a move by itself**. It merely provides a signal to the compiler at the type system level — "this reference is bound to an rvalue." What actually decides between copy and move is overload resolution. If our `MyString` didn't have a move constructor, then `std::move(s1)` would only trigger the copy constructor too — the compiler would fall back to using the `const MyString&` version, because `MyString&&` can be received by `const MyString&`. It won't error out, but it won't move either. We'll mention this point again later.

## Move Assignment Operators: Clean Up the Old Object First

Move constructors handle the "creating a new object" scenario, while move assignment handles the "overwriting an existing object" scenario. The core logic of both is very similar, but move assignment has an extra step — you must clean up the target object's old resources first.

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

This order is important. We first `delete[] actual_str_` release our own previous heap memory, and then take over the source object's pointer. If we did it the other way around — assigning first and then deleting — we would delete the pointer that the source object just gave us, which is a classic use-after-free.

The self-assignment check `if (this != &s)` is equally important in move assignment. Although `s` is an rvalue reference and theoretically nobody should write code like `x = std::move(x)`, the language doesn't prohibit it, and sometimes template instantiation can produce this effect. Without the self-assignment check, `delete[] actual_str_` would release our own memory, and then `actual_str_ = s.actual_str_` would assign a dangling pointer back to ourselves — instant crash.

Note that the return type is `MyString&` — an lvalue reference, not an rvalue reference. This is because the target of the assignment operator (the object on the left side of `=`) is always an lvalue. Whether you use `std::move` or not, the receiving end of an assignment is always "an object with a name and an address."

Additionally, this implementation is exception-safe — `MyString`'s data members are only built-in types (`std::size_t` and `char*`), and operations on these types won't throw exceptions. This is also why I marked it `noexcept`. If your class has more complex data members (such as another `std::string`), you would need to carefully consider exception safety.

## std::move: The Most Misunderstood Function in C++

The name `std::move` is terribly misleading. When I first saw it, I naturally assumed it "performed a move operation" — after all, it's called "move." But the truth is, **`std::move` doesn't move anything at all**.

Its real identity is a type cast to an rvalue reference. The standard library's implementation is roughly equivalent to:

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&& move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

Ignoring the template metaprogramming gymnastics of `remove_reference`, the core is just `static_cast<T&&>(t)`. It casts the passed-in argument to an rvalue reference and returns it. That's it. It doesn't generate any move code, doesn't call any move constructor, and doesn't modify any object's state.

Ben Saks said something very true in his talk: **if we could start over, we'd probably call it `make_movable` or `as_rvalue`**. At least that name wouldn't mislead people into thinking it performs a move.

### Why We Need std::move: The Naming Trap in swap

So if `std::move` doesn't move, why do we still need it? Let's look at the `swap` function. This is the scenario that best illustrates the point.

```cpp
template<typename T>
void swap(T& x, T& y)
{
    T temp(x);              // (1)
    x = y;                  // (2)
    y = temp;               // (3)
}
```

This C++03-style `swap` performs three copies. We naturally want to change it to a move version — after all, our previous two articles kept saying that moving is much faster than copying. But here's the problem: `x`, `y`, and `temp` inside the function body are all lvalues. They all have names, you can take their addresses, and their lifetimes span multiple statements. The compiler can't automatically treat them as rvalues — what if you still use `temp` after the third line?

C++ has a general rule: **if something has a name, it's an lvalue**. Only nameless things (like temporary objects, literals, or by-value function return results) can be rvalues. This rule is very reasonable — the compiler must be conservative; it can't assume that `temp` won't be used on the next line.

So we need to explicitly tell the compiler: "I know `temp` won't be used again after this, please treat it as an rvalue." This is exactly the purpose of `std::move`:

```cpp
template<typename T>
void move_swap(T& x, T& y)
{
    T temp(std::move(x));    // 移动构造 temp
    x = std::move(y);        // 移动赋值 x
    y = std::move(temp);     // 移动赋值 y
}
```

Every `std::move` sends a message to the compiler: **"Here, I confirm it's safe to move resources from this object."** Only after receiving this information will the compiler select the move version during overload resolution.

### std::move Doesn't Guarantee a Move

There's another easily overlooked trap: `std::move` doesn't guarantee that a move will actually happen. If a type only has copy operations and no move operations, the result of `std::move` will degrade to a copy.

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

Here, `std::move(a)` converts `a` to an rvalue reference, but `CopyOnly` doesn't have a constructor that accepts an rvalue reference. The compiler falls back to using the `const CopyOnly&` version of the copy constructor (because `CopyOnly&&` can bind to `const CopyOnly&`). It won't error out, but the "move" you expected silently becomes a "copy."

## The Naming Paradox of Rvalue Reference Parameters

This is the most confusing part of move semantics, and it's something Ben Saks spent considerable time emphasizing.

When we write a function that takes an rvalue reference parameter, that parameter is treated as an **lvalue** inside the function:

```cpp
void process(MyString&& s)
{
    // s 有名字 → s 是左值
    MyString copy(s);             // 调用拷贝构造！不是移动构造！
    MyString moved(std::move(s)); // 这才调用移动构造
}
```

From the perspective outside the function, the argument passed in is an rvalue (like `process(std::move(x))` or `process(MyString("temp"))`). But once inside the function body, `s` becomes a named variable — it exists across multiple statements, and the compiler can't assume it's only used once. So the rule that "if it has a name, it's an lvalue" still applies.

This leads to a practical consequence: **inside a function, if you want to move resources from an rvalue reference parameter, you must explicitly use `std::move`**. And once you move from it, the value of that parameter in subsequent code becomes unpredictable — this is the moved-from state we'll discuss in the next section.

## Implicitly Movable Return Expressions

The good news is that the "if it has a name, it's an lvalue" rule has an important exception — the `return` statement.

```cpp
MyString make_greeting()
{
    MyString temp("hello world");
    // ... 对 temp 做一些操作 ...
    return temp;  // 不需要 std::move！
}
```

In this code, although `temp` has a name (which would normally make it an lvalue), `return temp;` is the last use of `temp` in the function. The compiler knows that `temp`'s lifetime ends immediately after the function returns, so the standard allows it to treat `temp` as an implicitly movable entity <RefLink :id="3" preview="C++ Standard, [class.copy.elision] — NRVO and implicit move" />.

This means you **do not** need to write `return std::move(temp);`. Simply writing `return temp;` is enough — the compiler will automatically select the move constructor (or, even better, eliminate the construction entirely, which we'll cover right below).

## NRVO: An Optimization Better Than Moving

Talking about "implicitly movable" actually isn't the end of the story. The compiler can actually do better than moving — it can deliver the return value to the caller at **zero cost**, without even needing a move. This is what's called **Named Return Value Optimization (NRVO)**.

```cpp
MyString make_greeting()
{
    MyString temp("hello world");
    return temp;
}

MyString s = make_greeting();
```

In a world without NRVO, the execution flow would be: first construct `temp` on `make_greeting`'s stack frame, then construct a temporary object at `s`'s location (via move or copy), then destruct `temp`, then move or copy the temporary into `s`, and finally destruct the temporary. Just hearing about it sounds wasteful.

NRVO's approach is very clever: when generating code, the compiler directly constructs `temp` at `s`'s location. Instead of constructing first and then copying, it puts the object in the right place from the very beginning. `temp` is `s`; they share the same memory. When the function returns, no copy or move is needed — the object is already where it should be.

Starting from C++17, this optimization became **mandatory** in certain scenarios <RefLink :id="4" preview="C++ Standard, [class.copy.elision] — mandatory elision in certain contexts" /> — the compiler must eliminate the copy, rather than "can eliminate it but doesn't have to." This isn't an optional optimization anymore; it's a defined behavior of the language. For historical reasons it's still called an "optimization," but it's actually a guarantee.

For the complete technical details of NRVO and RVO, we previously had a dedicated article in vol2: [RVO and NRVO: Compiler Return Value Optimization](../../../../vol2-modern-features/ch00-move-semantics/03-rvo-nrvo.md).

## Never Use std::move on Return Values

This is probably the most common mistake I've seen related to move semantics. As mentioned earlier, `return temp;` is implicitly movable, so the compiler will either perform NRVO (zero cost) or automatically fall back to move construction (the cost of one pointer assignment). Some people might think: since `std::move` "requests a move," wouldn't `return std::move(temp);` be more explicit and safer?

**Exactly the opposite.**

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

The reason lies in NRVO's trigger conditions <RefLink :id="5" preview="C++ Standard, [class.copy.elision] — the return expression must be the name of a local variable" />: the `return` expression must be the name of a local object. When you write `return std::move(temp);`, the return expression is no longer the name `temp` — it's `std::move(temp)`, a function call expression. The compiler cannot perform NRVO on this expression and can only fall back to choosing move construction.

In other words, `return std::move(temp);` forces the compiler down the move construction path, while `return temp;` gives the compiler the opportunity to take the NRVO path (zero cost). This is why Ben Saks repeatedly emphasized in his talk: **never use `std::move` on a return value**.

We can use the compiler flag `-fno-elide-constructors` to compare the difference between the two. This flag disables GCC's copy elision optimization, letting us see what the world looks like "without NRVO."

First, let's look at `return temp;`'s behavior with elision disabled — it falls back to move construction, because `temp` is implicitly movable. And `return std::move(temp);` is also move construction — there's no difference between the two when elision is disabled. But once elision is enabled (the default behavior), `return temp;` becomes a no-op, while `return std::move(temp);` is still a move construction. That's where the difference lies.

I tested this with GCC 16.1.1, adding print logs to `MyString`'s various constructors, and the comparison results are as follows:

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

See? `return std::move(temp);` clearly has one extra move construction. For a class like `MyString` that only has a pointer and an integer, the cost of a move construction is very low (one pointer assignment), but for more complex classes (like objects containing multiple dynamic containers), the cost of this extra move cannot be ignored.

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

With NRVO disabled, both indeed behave identically — both perform one move construction. But this precisely shows that `return std::move(temp);` wastes the NRVO opportunity for free under default settings.

:::warning C++20/C++23 Further Expand the Scope of "Implicitly Movable"
The rule discussed in this section — "don't use `std::move` on return values" — holds true across **all standard versions (C++11 through C++26)** and is absolutely safe advice. However, the "implicitly movable" mechanism itself has been continuously strengthened in subsequent standards, and it's worth knowing about: C++11 introduced the initial implicit move (when returning a local object, the compiler can treat it as a move); C++20 (proposal P1825, "More implicit moves") expanded the scope of "implicitly movable entities" — for example, local variables bound to rvalue references, and `throw` a local object, were also brought into implicit move territory; C++23 (proposal P2266) further refined this, making return values treated as xvalues in certain scenarios, covering more construction paths.

But no matter how these extensions change, **the iron rule of "don't write `std::move` when returning a local object" has never changed** — P1825/P2266 expand the scope of "what the compiler can automatically move," while `std::move` actually breaks NRVO's trigger conditions. The conclusion remains the same: write `return temp;`, and leave the choice between NRVO and implicit move to the compiler.
:::

## Moved-From State: Valid but Unspecified

After a move operation is complete, the source object is in a state that the standard calls **"valid but unspecified state"** <RefLink :id="6" preview="C++ Standard, [lib.types.movedfrom] — moved-from objects are in a valid but unspecified state" />. These words are worth breaking down one by one.

"Valid" means: no memory leaks, no resource leaks, no undefined behavior (UB). You can safely let this object destruct — its destructor will execute normally, there will be no double free, and it won't crash. For our `MyString`, after moving, `actual_str_` is set to `nullptr`, and `stored_length_` becomes 0, so `delete[] nullptr` does nothing during destruction.

"Unspecified" means: you cannot make any assumptions about the values held by the moved-from object. The standard doesn't mandate that a moved-from `std::string` must be an empty string, nor does it mandate that a moved-from `std::vector` must be empty. Different standard library implementations may have different behaviors. Our own `MyString` returns `"(null)"` after moving (this is our own safety fallback), but a moved-from `std::string` might return an empty string or it might return the original value — you can't rely on it.

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

:::warning Usage Restrictions on Moved-From Objects
When Ben Saks was asked in the Q&A session "can a moved-from object still be used," his answer was very straightforward: **after a move, the only thing you should do with the source object is assign a new value to it or let it destruct**. Any other operation (reading values, comparing, passing to other functions) is a gamble — you might win (the implementation happens to give you a predictable value), or you might lose (the implementation changes or you switch to a different standard library). Don't gamble.

Don't confuse "valid" with "useful" — a moved-from object is a legitimate object, but not one with determined contents. If you need an empty object, create one explicitly; if you need a specific value, assign it explicitly. Don't count on the move operation to do these things for you.
:::

## The Importance of noexcept: The Hidden Trap in Vector Reallocation

Finally, let's discuss an issue that is often overlooked in real-world engineering but has a massive impact: **move constructors should be `noexcept`**.

Why? Let's look at the `std::vector` reallocation scenario. When `vector`'s capacity is insufficient, it needs to allocate a larger block of memory and then transfer the old elements to the new memory. If the element's move constructor is `noexcept`, `vector` will use moving to transfer them — very fast. If the move constructor is not `noexcept`, `vector` will fall back to copying <RefLink :id="7" preview="C++ Standard, [vector.modifiers] — if move ctor is not noexcept, vector uses copy during reallocation" />.

This is because `vector` needs to provide a strong exception safety guarantee: if an exception is thrown during reallocation, `vector`'s state must be rolled back to before the reallocation. If moving is used, once an exception is thrown midway, the already-moved elements cannot be restored (their resources have already been stolen). If copying is used, the original data is still there, and a safe rollback is possible.

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

After compiling and running, you'll see output like this (GCC 16.1.1, `-std=c++20 -O2`):

```bash
$ g++ -std=c++20 -O2 test_noexcept.cpp && ./a.out
=== push 3 elements (triggers reallocation) ===
  ctor: AAA
  ctor: BBB
  ctor: CCC
  COPY ctor: AAA    # 扩容时用的是拷贝！不是移动！
  COPY ctor: BBB
```

See that? When the third element triggers reallocation, `vector` **copies** the first two elements to the new memory — even though we clearly implemented a move constructor. The reason is that our move constructor isn't marked `noexcept`.

Now let's add `noexcept` to the move constructor:

```cpp
StringNoNoexcept(StringNoNoexcept&& o) noexcept  // 加上 noexcept
```

Recompile and run:

```bash
$ g++ -std=c++20 -O2 test_noexcept.cpp && ./a.out
=== push 3 elements (triggers reallocation) ===
  ctor: AAA
  ctor: BBB
  ctor: CCC
  MOVE ctor: AAA    # 现在用移动了！
  MOVE ctor: BBB
```

The difference of a single `noexcept` keyword directly determines whether `vector` uses copy or move during reallocation. For a class that holds dynamic memory, in scenarios with large amounts of data, this difference can mean an order-of-magnitude performance gap.

This is a genuine production-level trap. Many people write move constructors but forget to add `noexcept`, and then are puzzled in performance testing about "why move semantics aren't taking effect." The answer often lies in those two words.

## The Complete MyString: The Rule of Five Assembled

Combining the content of this article with the previous two, we get a complete, Rule of Five-compliant `MyString` implementation:

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

All five special member functions — destructor, copy constructor, copy assignment, move constructor, and move assignment — are present and accounted for. This is the so-called Rule of Five: if you need to customize any one of them, you most likely need to customize all five. The compiler-generated default versions are unsafe for classes that hold raw pointers.

## What We've Cleared Up

Across three articles, we started from the three deep copies of `swap`, went through the value category system of lvalues and rvalues, and finally in this article broke down all the implementation details of move operations. Let me use a concise checklist to review the core points of this article.

The core of a move constructor is "destructive copy" — steal the source object's resource pointer, then set the source object to a harmless state. Overload resolution automatically selects between copy and move; you don't need to make extra judgments at the call site. `std::move` doesn't move anything; it's simply a cast to an rvalue reference that enables overload resolution to select the move version. An rvalue reference parameter is an lvalue inside a function — because it has a name — so you still need `std::move` to move from it. The `return` statement is an exception to the "if it has a name, it's an lvalue" rule; the compiler automatically identifies implicitly movable return expressions. NRVO can deliver return values to the caller at zero cost — and `return std::move(temp)` prevents NRVO, so never write it that way. A moved-from object is in a "valid but unspecified" state; the only safe operations are assigning a new value or destructing it. Move constructors must be marked `noexcept` — otherwise `std::vector` will fall back to copying during reallocation, and the performance gap can be enormous.

If you want to dive deeper into more application scenarios of move semantics — perfect forwarding, universal references, reference collapsing — check out vol2's [Perfect Forwarding: Preserving Exact Value Category Propagation](../../../../vol2-modern-features/ch00-move-semantics/04-perfect-forwarding.md). Move semantics combined with perfect forwarding form the complete foundation of modern C++ template programming.

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
