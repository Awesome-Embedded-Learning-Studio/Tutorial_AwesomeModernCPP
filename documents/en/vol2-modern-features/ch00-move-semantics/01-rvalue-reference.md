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
- Volume One: C++ Fundamentals
reading_time_minutes: 24
related:
- Move Construction and Move Assignment
- Perfect Forwarding
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 'Rvalue References: From Copy to Move'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/01-rvalue-reference.md
  source_hash: 09526073611567a0f7452f05ec8d53e23e564c56c13d6b8c6fbb21a5c002f11c
  translated_at: '2026-11-22T16:36:41+00:00'
  engine: glm-5.3-refresh
  token_count: 21050
---
# Rvalue References: From Copy to Move

Welcome to modern C++! When this tutorial says "modern C++," it generally means C++11 and everything after it.

> Some folks will take issue with this. I've been flamed in conversation myself for asking whether C++11 even counts as "modern." Well... fair point. From 2026, when I'm writing this, these features have already accompanied us for over a decade—chronologically speaking, they really aren't modern anymore. But compared with relics like C++98, the feature changes are substantial. And that is exactly why this volume gets its own separate treatment!

When I first got into C++ and gnawed my way through *Effective Modern C++*<RefLink :id="1" preview="Scott Meyers, Effective Modern C++, 2014 — Items 23-25: rvalue references, universal references, std::move" />, I never quite understood the concept of "rvalue references." The very phrase "rvalue reference" exuded an indescribably academic odor—what exactly is `T&&`? Where is the line between lvalues and rvalues? Does `std::move` really "move" anything? Every time I saw `std::move` in other people's code, I would copy it over with half an understanding, leaving the rest to the compiler's mercy. Now that it's my turn to write the tutorial for you, I want to get these things straight together with you—or at the very least, not embarrass myself with low-level mistakes!

> Still rambling: I'm honestly a bit scared of C++ language lawyers. Every time I pick up the pen to write something, I'm afraid these heavyweights will dig my mistakes out. But rigor is always a good thing—write C++ without rigor, and at best you'll be staring blankly at a screenful of compiler errors; at worst you'll be shaken out of bed at night by a memory error. Teaching, though, is another matter entirely: there's no need to nitpick every detail right from the start, and we mustn't miss the forest for the trees.

## Starting from a Problem That Spikes Your Blood Pressure

Consider a scenario everyone knows: string processing. Quite a few people feel that std::string is sometimes too heavy and wish for a read-only view of a string. const char* looks like a decent fit, but a C string has to end with a trailing `\0`, and that business is sometimes just unreliable: the content can't hold a `\0`, and the length can only be found by counting from the beginning all over again. So let's build our own StringWrapper!

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

    // 拷贝构造：深拷贝
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

Let's assume the worst case: no move semantics, and assume the compiler does no NRVO (named return value optimization) either. Follow that assumption through, and returning `result` from `build_greeting` must trigger a copy constructor: we allocate a fresh block of memory and copy the string inside `result` over byte by byte. Then look at `result` itself: when it's destroyed, it releases its original block of memory too. (Think about it—isn't that operation a numbing loss?)

> Of course, let's put this in full: GCC and MSVC of the C++03 era already supported NRVO widely back then. The standard has always allowed compilers to elide this copy—it just never mandated it—so the analysis above is about the worst case, the one where NRVO doesn't kick in.

We spend one memory allocation plus one byte-by-byte copy just to "relocate" the data of an object that is about to be destroyed anyway. If the string is long—say, a JSON payload of several KB—this copy looks downright wasteful: **the source object is dying anyway; the data sitting in that memory will simply go to waste—why not just take over control of that memory directly?**

This is the core problem move semantics set out to solve. And to understand move semantics, we must go back to a more basic question: how does C++ classify expressions? The standard's answer is what we call the **value category**<RefLink :id="2" preview="cppreference Value categories — lvalue / xvalue / prvalue taxonomy since C++11" />.

## Value Categories—From a Two-Way Split to a Three-Way One

Before C++11, we sorted expressions into just two bins: **lvalue** and **rvalue**—a simple two-way split. Once C++11 brought "an object's resources can be safely transferred" into the language, the left-right distinction was no longer enough, and the taxonomy grew more elaborate along with it. The system we see now is this: every expression belongs to exactly one of three categories. As for the three-way split: **lvalue** is the has-identity kind, **xvalue** is the about-to-expire kind, and **prvalue** is the purely temporary kind. Merge them upward and you get two broader categories. The one governing the left half we call **glvalue** (generalized lvalue)—it absorbs lvalue plus xvalue. The one governing the right half we call **rvalue**—it absorbs xvalue plus prvalue.

If this taxonomy feels a bit convoluted, don't worry—it took me a long time to untangle it at first, too. We can understand it through two properties. The first property we'll call **has identity**: the expression has a name, and you can take its address. The second property we'll call **can be moved from**: the expression is temporary, and its resources can be safely "stolen" by us. Cross the two properties, and the three kinds of expressions each fall into place:

|              | Not movable | Movable                   |
| ------------ | ----------- | ------------------------- |
| **Has identity**   | lvalue | xvalue (expiring value) |
| **No identity** | ——       | prvalue (pure rvalue)   |

Take an ordinary example: `x` in the variable `int x = 10;` has a name, has an address, and its lifetime isn't over yet, so it lands in the "has identity, not movable" cell—an lvalue. Later code will still use it, so of course you can't just steal its resources. The result of `std::move(x)` still points at the very same object `x`; it has merely been marked into the shape of "about to expire, resources may be hauled away," landing at the crossing of "has identity, movable" as an xvalue. The literal `42`, or a temporary object returned by value from a function, never had a name in the first place, so it drops into the prvalue cell—you needn't worry that anyone will access it after the haul. As for the "no identity and not movable" combination, the standard simply never reserved a slot for such an expression; in any case, the three categories cover every expression there is.

Let's look at a concrete set of examples to sort the three categories out clearly.

```cpp
int x = 10;            // x 是 lvalue
int&& r = std::move(x); // std::move(x) 是 xvalue
int y = x + 1;         // x + 1 是 prvalue
int z = 42;            // 42 是 prvalue
```

Here `x` is the most typical lvalue—it has a name, has an address, and `&x` is a valid expression (of course you can grab this variable's address on the stack!). `std::move(x)` produces an xvalue: it points to the same memory as `x`, but it is semantically marked as "about to expire." `x + 1` and `42` are both prvalues—temporary, unnamed values.

There's also a classic misconception to dismantle—the old line that has circulated for ages: "lvalues can appear on the left of the assignment sign, while rvalues can only stay on the right." In the C era that claim was basically true, but by the C++ era it is neither sufficient nor necessary. Let's look at two counterexamples. For the first, take `const int cx = 10;`: here `cx` is an lvalue, yet `cx = 20;` won't compile—const restricts modification; it doesn't change the value category. Conversely, construct `std::string("hello")`, a temporary prvalue that really can sit on the left of the assignment sign: `std::string("hello") = "world";` is perfectly legal. The reason isn't complicated: for class types, the assignment operator is a member function to begin with, and that line of ours is, in essence, calling a member function on a temporary object—something that has held since C++98. What genuinely can't go on the left of the assignment sign is built-in types—write `42 = x;`, and no standard will compile it.

### Giving Any Expression a Value Category Checkup

So the rules are clear now—but when you're handed an unfamiliar expression, how do you actually confirm its identity as an lvalue, an xvalue, or a prvalue? You can't just wing it every time. Here's a ready-made trick: `decltype` deduces different types for an **identifier** than for a **parenthesized expression**.

Put `x` itself and `(x)` side by side: `decltype(x)` is the unparenthesized identifier, and what you get is `x`'s **declared type**. With `decltype((x))`, the moment you bring in the parentheses, the type-deduction rule switches to judging by value category: an lvalue yields `T&`, an xvalue yields `T&&`, and a prvalue yields `T` itself.

Plug this difference into `is_lvalue_reference_v` / `is_rvalue_reference_v`, and you can give any expression a "value category checkup":

```cpp
// value_category_probe.cpp -- 用 decltype 判断任意表达式的值类别
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

// decltype((expr)) 按表达式的值类别求类型：lvalue 得 T&，xvalue 得 T&&，prvalue 得 T
#define SHOW(expr) \
    std::cout << "  " #expr "  ->  " << value_category<decltype((expr))>() << "\n"

int g = 100;  // 全局变量

int main()
{
    int x = 10;        // 普通变量
    int& lref = x;     // 左值引用
    int&& rref = 20;   // 右值引用（但 rref 这个名字本身是左值！）

    std::cout << "--- 变量与引用 ---\n";
    SHOW(x);
    SHOW(lref);
    SHOW(rref);          // 反直觉点：命名的右值引用是左值

    std::cout << "\n--- 字面量与运算 ---\n";
    SHOW(42);
    SHOW(x + 1);
    SHOW(std::move(x));  // std::move 的产物是 xvalue

    std::cout << "\n--- 解引用与成员 ---\n";
    SHOW(*(&x));         // 解引用得到左值
    SHOW(g);

    return 0;
}
```

The probe's complete code sits in the demo below—click "Try It" and it runs on the spot; you don't even need to open a terminal:

<OnlineCompilerDemo
  title="动手验证：value_category_probe.cpp"
  source-path="code/examples/vol2/14_value_category_probe.cpp"
  description="在线判定一批表达式的值类别。rref 那一行最值得咱们打起精神：声明成 int&& 的变量，名字本身却是左值。"
  run-options="-O0 -std=c++17"
  allow-run
/>

Go over every line of the output, and the one most worth understanding is the `rref` line—it is declared as an rvalue reference `int&& rref`, yet the checkup says `lvalue`. This is not a bug: `rref` is a variable **with a name**, and C++'s rule is "an expression with a name is an lvalue." The xvalue produced by `std::move(x)`, once you give it a name (assign it to a `T&&` variable, or pass it through as a function parameter), "demotes" back to the lvalue side and will never automatically trigger a move again. Perfect forwarding is what solves this—we'll take it apart in the fourth article.

With this ready-made tool in hand, any expression you're unsure about can be confirmed by running code. No more guessing.

## The Binding Rules of Rvalue References

With value categories under our belt, let's see what an rvalue reference—`T&&`—can actually bind to. The rule is really simple: **an rvalue reference can only bind to rvalues (prvalues or xvalues), never to lvalues**.

```cpp
int x = 10;

int&& r1 = 42;           // OK：42 是 prvalue
int&& r2 = x + 1;        // OK：x + 1 是 prvalue
int&& r3 = std::move(x); // OK：std::move(x) 是 xvalue

// int&& r4 = x;         // 编译错误：x 是 lvalue，不能绑定到右值引用
```

If you uncomment the last line, GCC will hand you a rather blunt error message:

```text
error: cannot bind rvalue reference of type 'int&&' to lvalue of type 'int'
```

You don't have to take our word for the wording, either. The binding snippet above sits below as a runnable online program—remove the comment on `r4` and hit Run, and the result pane hands you GCC's message verbatim:

<OnlineCompilerDemo
  title="Hands-On: Rvalue References Bind Only to Rvalues"
  source-path="code/examples/vol2/54_rvalue_bind.cpp"
  description="Verify the binding rule online: in the default state all three bindings compile and the program prints 42 11 10; uncomment the r4 line and hit Run—the result pane shows GCC's binding error verbatim. The online environment is Compiler Explorer's GCC 15.2."
  run-options="-O2 -std=c++17"
  allow-run
/>

The value category taxonomy and the rvalue reference binding rules have been turned into an animation—you can play it, pause it, or single-step through it with the step controls:

<Anim id="lvalue-rvalue" />

Why design the rule this way? The intent isn't hard to guess: rvalue references exist precisely so you can "take over" a temporary object's resources. And why not let them bind lvalues too? We'll come back to that question after we finish the comparison with const lvalue references.

One heads-up I owe you here: the "`T&&` binds only rvalues" rule speaks of rvalue references with a **hard-coded type**, such as `int&&` or `std::string&&`. When we reach perfect forwarding in the fourth article, we'll run into `T&&` inside templates—what we call a **forwarding reference**—which binds to both lvalues and rvalues and plays by a different set of rules entirely. Take this article's rules and apply them to `T&&` in templates, and the compiler will shove the error right back in your face.

Now let's put rvalue references and const lvalue references side by side and compare them—this comparison directly decides what the move constructor's signature will look like later on.

The const lvalue reference `const T&` binds to everything: lvalues, rvalues, const, non-const—no questions asked. The rvalue reference `T&&` binds to rvalues only. This difference looks simple, but it leads to a very important practical distinction: when you receive an rvalue through `const T&`, you have promised not to modify it, so there is no way to steal its resources. When you receive an rvalue through `T&&`, you hold the permission to modify it, and only then can you safely transfer the resources away.

```cpp
void process_const_ref(const std::string& s)
{
    // 可以读取 s，但不能修改它
    // 所以无法"偷走" s 的内部缓冲区
    std::cout << s.size() << "\n";
}

void process_rvalue_ref(std::string&& s)
{
    // s 是非 const 的右值引用，可以修改它
    // 所以可以安全地转移 s 的内部资源
    std::string stolen = std::move(s);
    // 此时 s 处于"有效但未指定"的状态
}
```

Now back to the question we shelved a moment ago: why not let rvalue references bind to lvalues too? Reason backward from the intent of move semantics and it becomes clear. Move semantics expresses a transfer of resource ownership, and an lvalue is an object with its own address, currently in charge of its own resources—nobody ever said it intended to be drained. If `T&&` could bind to anything, you would never again be able to distinguish "this object can be safely hauled away" from "this object is still in use by someone." Once that distinction is gone, move semantics has nothing left to stand on.

## The Essence of std::move—A Carefully Wrapped Type Conversion

The name `std::move` deserves, in my view, a spot among the most misleading names in the history of C++. Hearing it, you'd probably assume it really "moves" something—but in fact it moves nothing at all. `std::move` does exactly one thing: **convert its argument to an rvalue reference**—it's really just a `static_cast<T&&>`<RefLink :id="3" preview="cppreference std::move — equivalent to static_cast to rvalue reference; moves nothing" />. Truly, that's all.

We can implement an equivalent `move` ourselves:

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&&
my_move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

What this code does is very direct: whatever type `T` comes in as, we strip off any reference it might carry with `remove_reference`, then use `static_cast` to turn it into an rvalue reference. That's where the function body ends—these few lines are doing exactly the job of the standard library's `std::move`.

So where's the payoff? The payoff lands on **the signatures of the move constructor and move assignment operator**. When you write `std::string a = std::move(b);`, it is `std::move(b)` that converts `b` into a `std::string&&`, and the match lands on the move constructor. What does it look like? Its signature is `std::string(std::string&& other)`. The one actually carrying out the "resource transfer" is the move constructor: it takes over `other`'s internal buffer pointer and sets `other` to empty—that's how the typical implementation does it<RefLink :id="4" preview="cppreference Move constructor — transfer resources, leave source valid but unspecified" />. `std::move`'s entire contribution here is marking `b` as an rvalue that "may be hauled away," so the compiler picks the move constructor over the copy constructor on our behalf.

> Short strings are the one exception: they live directly in a small buffer inside the object itself (SSO—Short String Optimization), with no separate heap buffer for us to take over, so moving degenerates into an ordinary copy.

```cpp
std::string a = "Hello";
std::string b = std::move(a);  // std::move 只是转换类型
                                  // 移动构造函数做了实际的资源转移
// 此刻 a 处于"有效但未指定"的状态
// 在大多数实现中 a 变成空字符串，但您不应该依赖这个行为
```

And here's another trap worth staying alert to: **using `std::move` on fundamental types brings no performance gain whatsoever**. Look at `std::move(42)` again—it merely converts the `int` into an `int&&`, but for an `int`, "moving" and "copying" were the same thing all along—both come down to copying four bytes. That is a fact at the level of the definition, with nothing to do with whether the compiler optimizes. The power of move semantics shows only in **classes that manage resources**—classes holding dynamic memory, file handles, network connections, and the like.

## The Lifetime of Temporary Objects—What Rvalue References Extend

In C++, a temporary object (a prvalue) has, by default, a lifetime that ends with the full expression containing it: the moment the expression ends, it is destroyed. But rvalue references and const lvalue references grant us a special privilege: when they bind to a temporary object, they extend that temporary's lifetime, letting it live until the end of the reference's scope<RefLink :id="5" preview="cppreference Reference initialization — temporary lifetime extension applies only to direct binding" />.

```cpp
const int& cr = 42;       // const 引用延长了 42 的生命周期
std::cout << cr << "\n";   // OK：42 还活着

int&& rr = 100;            // 右值引用也延长了 100 的生命周期
std::cout << rr << "\n";   // OK：100 还活着
```

The two behave identically when it comes to lifetime extension; the difference is that `rr` is non-const—you can modify it. This looks a bit strange: how can the literal `100` be modified? In reality, behind the scenes the compiler puts this temporary value into a piece of storage, and `rr` points at exactly that storage.

```cpp
int&& rr = 100;
rr = 200;                  // 合法！rr 指向的存储空间被修改了
std::cout << rr << "\n";   // 输出 200
```

This feature rarely comes up in practice, but understanding it helps you shake off the fear of "does an rvalue reference dangle right away?" When you write `std::string&& ref` to catch `std::move(name)`, the object `ref` points to won't vanish on the next line—it lives all the way to the end of `ref`'s scope.

But let's push one more question here: both const lvalue references and rvalue references can extend a temporary's lifetime to the end of the reference's own scope—so if the binding passes through a function along the way, does the perk survive? Let the code answer. The easiest way to faceplant is this:

```cpp
// lifetime_dangle.cpp -- 临时对象生命周期延长的边界
// Standard: C++17
// 编译（ASan 即 AddressSanitizer，内存错误检测工具；开它的 use-after-scope 检测才能抓到悬空读）：
//   g++ -std=c++17 -O0 -g -fsanitize=address -fsanitize-address-use-after-scope \
//       -o lifetime_dangle lifetime_dangle.cpp

#include <iostream>
#include <string>

// 把参数引用原样返回
const std::string& pass_through(const std::string& s)
{
    return s;
}

int main()
{
    std::cout << std::unitbuf;  // 实时冲刷，确保 ASan 中断前的输出可见

    // 情况 1：const& 直接绑定一个临时对象——生命周期被延长，安全
    const std::string& safe = std::string("I am a temp");
    std::cout << "1) 直接绑定临时对象: \"" << safe << "\"\n";

    // 情况 2：const& 绑的是"函数返回的引用"，而这个引用指向一个临时对象——不延长，悬空
    const std::string& dead = pass_through(std::string("I am passed"));
    std::cout << "2) 经函数返回的引用: \"" << dead << "\"\n";  // use-after-scope

    return 0;
}
```

Case 1 is perfectly fine: the `const&` catches the temporary squarely, and its lifetime extends until `safe` leaves scope. Case 2 is where it goes wrong—the temporary `"I am passed"` is bound to the function parameter `s`, and per the standard it lives only until **the end of the full expression containing it**. Meanwhile `dead` receives the reference returned by `pass_through`, and a binding "relayed through a function" like this triggers no extension. So the instant the expression ends, the temporary is destroyed. When the next line reads `dead`, what it reads is an already-destroyed object—this step has undefined behavior.

This kind of mistake is hard to spot with the naked eye—without ASan it might "happen to" still print the old contents, because that memory hasn't been overwritten yet, and that is exactly what makes dangling references so insidious. Turn on ASan's use-after-scope detection and run it again, and the truth shows itself instantly:

```text
1) 直接绑定临时对象: "I am a temp"
2) 经函数返回的引用: "=================================================================
==PID==ERROR: AddressSanitizer: stack-use-after-scope on address 0x... at pc 0x...
    #2 ... in main lifetime_dangle.cpp:26
SUMMARY: AddressSanitizer: stack-use-after-scope ... in std::__ostream_insert
```

(The process ID `PID` and the address `0x...` differ on every run, so I've elided what could be elided; the error type, the line numbers, and the SUMMARY are fixed.) Case 1 prints normally; Case 2 gets caught red-handed by ASan the instant it tries to read `dead`. Now that the evidence is in hand, we can formally answer the question asked a moment ago: **lifetime extension recognizes "direct binding" only—it does not cross function boundaries**. Once a temporary has been relayed through a function, the extension rule goes with it.

## A Worked Example—Copy and Move in String Concatenation

Let's put what we've learned together and look at a realistic example. Suppose we are building a log message:

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

Inside it, `"[" + level + "] " + module + ": " + detail` is a chained string of `+` operations. In the C++03 world, every `+` we perform produces a brand-new temporary string! Each link in the chain costs a fresh memory allocation plus copying everything accumulated on the left over in one piece—the further along we concatenate, the more it costs. Think about how wasteful that is!

After C++11, the standard library equipped `operator+` with a batch of overloads taking rvalue references (for example `operator+(std::string&&, const std::string&)`). With them, we can keep using the temporary objects in the middle of the chain: the next segment gets appended straight into the temporary's buffer and handed onward, instead of starting a new string at every link and re-copying all the preceding characters. Count it up: only the first link starts a new temporary; everything after appends into its buffer, growing capacity only when it runs out. The cost of those growths gets amortized across every append, and the total number of characters copied along the whole chain is proportional only to the final message's length.

By the time we reach C++17, guaranteed copy elision arrives and turns "returning a prvalue" into a hard guarantee: the result `operator+` produces is constructed directly in the final receiver's location, saving even the move that receiving would have cost. In the C++11 era compilers would generally save that move for us as a matter of course, but it was only ever a convention. C++17 wrote it into the language's provisions—what we hold now is a guarantee.

The more direct payoff comes from returning from a function. Look at `build_log_message` next: it returns `msg`, and the compiler has two ready-made optimization tools in hand. NRVO can eliminate this copy outright. Taking a step back: in a setting without NRVO, C++11 will also automatically treat `msg` as an rvalue (implicit move), invoking `std::string`'s move constructor. That step transfers the internal pointers only, without copying character data—see how much lighter the cost suddenly is.

Now for an example of transferring elements into a container:

```cpp
std::vector<std::string> names;

std::string name = "Alice";
names.push_back(std::move(name));  // 移动：name 的内部数据转移到 vector 中
// name 现在处于有效但未指定的状态，不要再使用它

names.push_back("Bob");   // 先从 const char* 构造临时对象，再移动进 vector
```

Look at the first `push_back`: it puts move semantics to work—`std::move(name)` converts `name` into an rvalue reference, and the vector calls `std::string`'s move constructor to build the new element. The cost is transferring one pointer plus two `size_t`s, not copying the string's content. The second `push_back("Bob")`, on the other hand, looks like direct construction, yet it actually has one step more than `push_back(std::move(name))`. Which step is the extra one, and what exactly did the move save us? Let's run a tracing class and let the output tell you.

The `TrackedString` below is fitted with tracking: every step—construction, copy, move, destruction—leaves a line of its own on the screen:

```cpp
// push_back_vs_emplace.cpp -- push_back vs emplace_back 的构造/移动/析构追踪
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

The tracing program sits in the demo below—click "Try It" to run it, and then we'll count the lines of output:

<OnlineCompilerDemo
  title="动手验证：push_back_vs_emplace.cpp"
  source-path="code/examples/vol2/15_push_back_vs_emplace.cpp"
  description="在线对比 push_back 与 emplace_back 的构造链路。数一数两段在 === done === 之前各打印了几行：push_back 三行，emplace_back 一行。"
  run-options="-O0 -std=c++17"
  allow-run
/>

Look at the `push_back(TrackedString("Bob"))` segment: before `=== done ===` it counts out three lines for you—the first constructs the temporary, the second moves it into the vector, and only the third is the temporary's own destruction. Counted up, the construction chain actually took two steps. The `[dtor] "Bob"` line that appears after `=== done ===` is the vector destroying the element it holds when it leaves the `{ }` scope; it isn't part of the construction process.

The answer to the extra step in `push_back("Bob")` hides in those three lines of output above: `"Bob"`'s first step goes through the `const char*` constructor, implicitly converting into a temporary `std::string`; that then lands, as an rvalue, in the `push_back(T&&)` overload, and finally gets moved into the vector. The extra step is precisely the making of the temporary. The move happened exactly once for the whole trip—see how the `[move ctor]` line appears exactly once—and a deep copy never happened from start to finish. Meanwhile `emplace_back("Alice")` shows only one `ctor` line before `=== done ===`: it constructs in place, directly in the vector's storage, sparing both the temporary and the move in one stroke. If you truly want to skip even the temporary's construction, we switch to `emplace_back`.

## Hands-On Experiment—rvalue_demo.cpp

In the `emplace_back` output just now, we watched one action at a time. This time let's go bigger: write a complete program that puts everything this article covered into one `main`, and run it through from start to finish.

```cpp
// rvalue_demo.cpp -- 右值引用与值类别演示
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

/// @brief 返回临时对象（prvalue）
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

The complete program hangs in the demo below—click "Try It" to run it. This time we skip not a single step: we'll check even the program's closing destructions against the output.

<OnlineCompilerDemo
  title="动手实验：rvalue_demo.cpp"
  source-path="code/examples/vol2/01_rvalue_reference.cpp"
  description="在线运行并观察 Tracker 对象的构造、拷贝构造、移动构造和析构顺序。"
  run-options="-O0 -std=c++17"
  allow-run
/>

Step 1 we can pass over in one sentence: `Tracker a("A");` is an ordinary construction. In step 2's output, `Tracker b = a;` triggers the copy constructor—`a` is an lvalue, so the only match is the copy constructor, and `b`'s name becomes `"A_copy"`. In step 3, we use `std::move(a)` to convert `a` into an rvalue reference, matching the move constructor—`c`'s name becomes `"A"` (stolen from `a`), while `a`'s name becomes `"(moved-from)"`.

Step 4 is the most interesting one, worth an extra look. `make_tracker("D")` constructs a `Tracker("D")` inside the function, and only then performs the return. Notice there is exactly one construction in the output—no copy, and no move. This is C++17's **guaranteed copy elision**: when returning a prvalue, the compiler constructs the object directly in the caller's space, saving even that move. And that's only the returning-a-prvalue case. How much further the compiler can save when returning a named local variable is exactly what the third article's RVO and NRVO will take apart.

Step 5 brings the move assignment. `d = std::move(b);` transfers `b`'s resources to `d`—`d`'s original name `"D"` gets overwritten with `"A_copy"`, and `b` is left with only the name `"(moved-from)"`. Notice, too, that `d`'s original resource (the memory holding `"D"`) is properly released, because the move assignment operator we wrote must ensure the old resources are cleaned up before overwriting.

Step 6 is the program's closing act: these objects are destroyed in reverse order of construction as execution leaves `main`—`d` goes first, then come `c`, `b`, and `a`; that order is guaranteed by the language. Notice `a` and `b` along the way: they were drained long ago—their names already read `"(moved-from)"`—yet their destructors run all the same. The "valid" in "valid but unspecified," landed here, means even the destructor runs to its completion; we can't pretend a moved-from object doesn't exist.

The next article turns these rules into code—we'll write the move constructor and move assignment for a resource-managing class with our own hands, nail down the "take the pointer, null out the source" maneuver, and, while we're at it, fill in the so-called Rule of Five (destructor, copy constructor, copy assignment, move constructor, move assignment: either declare none of the five, or declare them with all five considered together).

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
