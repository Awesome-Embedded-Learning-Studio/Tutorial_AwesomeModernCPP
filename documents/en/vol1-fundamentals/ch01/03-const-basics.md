---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the various uses of `const` with variables, pointers, and references,
  and get a first taste of `constexpr` compile-time constants.
difficulty: beginner
order: 3
platform: host
prerequisites:
- Type Conversion
reading_time_minutes: 16
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: A First Look at const
translation:
  source: documents/vol1-fundamentals/ch01/03-const-basics.md
  source_hash: 6a5cf88631916bd1f5f9609feefea08e996805f64655fdd9ab7e65287b1a6641
  translated_at: '2026-11-22T16:44:54+00:00'
  engine: glm-5.3-refresh
  token_count: 13182
---
# Long Time No See, `const` — Look How Immutable You've Become

When we write code, **some things simply should not be changed**—a configuration parameter, once set, should not be accidentally overwritten; an array's capacity, once declared, should not change again; and physical constants like pi go without saying. **If we rely purely on "self-discipline" to keep these values untouched, that is no different from walking down a dark road with our eyes closed. Sooner or later someone's hand slips, a critical value gets modified, and half a day goes into chasing down a baffling bug. In other words, a guarantee built into the mechanism beats whatever you keep in your head!**

C++ hands us a safety lock: `const`. The core idea is dead simple—if something should not change, say so explicitly and let the compiler keep watch for us. Any code that tries to modify a `const` value **gets stopped dead at the compilation stage**. Compared with discovering in production that some data was accidentally tampered with, strangling the problem at compile time is clearly the more dependable option. (This is why Rust simply flips the whole thing around: unless you say a variable is mutable, it is immutable! So variables are effectively declared `const` by default!)

## Putting a Lock on Variables — Basic `const` Usage

Suppose we have the maximum capacity of a buffer (a simple way to think about it: a spot where we put things away for use in a moment), a value that should never change for as long as the program runs:

```cpp
const int kMaxBufferSize = 1024;
```

Once `const` is attached, the variable becomes "read-only"—we must give it an initial value at declaration, and from then on any operation that tries to modify it will be rejected by the compiler. Let's give it a try:

```cpp
const int kMaxBufferSize = 1024;
kMaxBufferSize = 2048;  // 编译错误！
```

The compiler will produce a very explicit error message:

```text
error: assignment of read-only variable 'kMaxBufferSize'
```

This is the core value of `const`—it turns "I shouldn't modify this value" from a convention based on self-discipline into a rule enforced by the compiler. **You might ask: isn't this just using the compiler as a bodyguard? Exactly, that is precisely the idea—and this bodyguard never dozes off.**

### What Exactly Is the Difference Between `const` and `#define`

If you have some C under your belt, you might say, "I can do this with `#define` too." True, `#define MAX_SIZE 1024` looks roughly the same in effect, but there are several key differences between the two.

First, a `const` variable **has an explicit type (much cleaner semantics!)**. The `int` in `const int kMaxBufferSize = 1024;` tells the compiler this is an integer; if you later accidentally assign it to a `double`, the compiler can perform type checking and even issue a warning. `#define`, on the other hand, is plain text substitution—the preprocessor does not care about types at all. **It just dutifully replaces every `MAX_SIZE` with `1024`; whether that `1024` is an integer or a floating-point number is none of its business. Whose business is it? Yours!**

Second, `const` variables follow normal scoping rules. A `const` variable declared inside a function is visible only within that function, and a `const` variable declared at global scope has internal linkage by default (in other words, other `.cpp` files cannot see it). A `#define`, once expanded, is in effect from its point of definition all the way to the end of the file, with no scope restriction whatsoever—**which easily breeds name collisions in large projects.**

That is why, in C++, I prefer `const`—or `constexpr`, which we will meet later—for defining constants, **and keep `#define` for the scenarios that genuinely need conditional compilation. That is where `#define` truly earns its place in C++, and especially in modern C++!**

> Sharp-eyed readers may notice that my `const` constants look rather distinctive—why do they start with `k`? The answer is the `kPascalCase` style, as in `kMaxBufferSize`, `kDefaultBaudRate`, `kPi`. This `k` prefix is a fairly common constant-naming convention in the C++ community; you can tell at a glance that this is a value not meant to be modified. In truth, I lifted it from Google Chrome's constant-naming guidelines.

## When `const` Meets Pointers — Not That Commonly Used, but Worth Mentioning

As for `const`, our advice is: if there is a need, add it.

Using `const` to modify a plain variable by itself is simple, but once `const` meets pointers, things start getting interesting. Plenty of people get thoroughly turned around by this part—including the author himself, who got stuck here for a long time when first learning. Don't panic; let's take it apart step by step.

The core question is: does `const` apply to the pointer itself, or to the data the pointer points to? The answer **depends on where the `const` appears**. There are three ways to combine `const` with a pointer declaration in C++, and we will look at them one by one.

### Pointer to a Constant: `const int* p`

```cpp
int value = 42;
const int* p = &value;
```

Here `const` applies to the `int` (let me add the parentheses this way: (const int)* p—does that click now?). In other words, modifying the data `p` points to through `p` is not allowed. But the pointer `p` itself can change—it may point to a different address. You can understand it as "this pointer is well-behaved: it promises not to modify the target data through itself."

```cpp
int x = 10;
int y = 20;
const int* p = &x;

*p = 100;   // 编译错误！不能通过 const int* 修改数据
p = &y;     // 没问题，指针本身可以指向别的地方
```

Note one detail: although you cannot modify `x` through `p`, `x` itself is not `const`. Modifying it directly with `x = 100;` is perfectly legal—`const int*` only says "I won't modify through this pointer"; it does not mean the target data is actually immutable.

### Constant Pointer: `int* const p`

```cpp
int value = 42;
int* const p = &value;
```

Here, let me parenthesize it this way: int* (const p)—`p` itself is the pointer, and it is the `const` one. **So just look to the right and see what it binds to first.** This time `const` applies to the pointer variable `p` itself. That is, once the pointer is initialized, it is glued to that one address and cannot point anywhere else. Modifying the target data through `p`, however, is completely allowed.

```cpp
int x = 10;
int y = 20;
int* const p = &x;

*p = 100;   // 没问题，可以修改数据
p = &y;     // 编译错误！指针本身是 const 的，不能改指向
```

You can think of it as a "one-track-minded pointer"—once it has settled on an address it will not budge, but the contents at that address are fair game for it to modify.

### Both const: `const int* const p`

```cpp
int value = 42;
const int* const p = &value;
```

**This form stacks the two constraints above: the pointer itself cannot be repointed, and the data cannot be modified through the pointer. You actually see this quite often in function parameters—when you pass a pointer to a function and want neither the pointer's target changed inside the function nor the data modified, this is how you write it.**

## A First Look at References — Giving a Variable Another Name

With pointers covered, let's pause a step. If I remember correctly, what walks on stage next is the most elementary spot where C++ differs from C (and if you haven't learned C, feel free to ignore that remark).

Our new friend is the **reference**.

> References are arguably the most common beginner-level C++ feature that sets it apart from C. In my own projects, hardly a single PR goes by without touching a reference.

A reference is **giving an already-existing variable another name**. The notation is simple: add an `&` to the right of the type name:

```cpp
int x = 42;
int& alias = x;   // alias 是 x 的别名，它和 x 是同一块内存的两个名字
```

Wait—that's it? Yes! That's it!

From this line on, modifying `alias` is modifying `x`. And the other way around: after you change `x`, reading `alias` gives you the new value too. It's like a person with a formal name and a nickname—whichever you call, the same person answers.

> I am CharlieChen114514. You set up a reference called Charlie and declare that this Charlie refers to CharlieChen114514. From then on in the conversation, whatever you do to Charlie, you are doing to CharlieChen114514!

```cpp
int CharlieChen114514 = 42;
int& Charlie = CharlieChen114514;
Charlie = 100;                          // 通过别名赋值，等价于 CharlieChen114514 = 100
std::cout << Charlie << std::endl;      // 输出 100，alias 动的就是 CharlieChen114514 本尊
```

References have three properties worth getting right from day one. First, a reference **must be initialized at the moment it is declared**; `int& alias;` doesn't even compile—without an underlying object, the alias has nowhere to live. Second, **once bound, a reference cannot rebind**; when you later write `alias = y;`, what that does is assign `y`'s value to the variable behind `alias` (that is, `x`), not make `alias` switch allegiance to `y`. Third, any operation performed on a reference is equivalent to performing it on the variable behind it. The next chapter, "A First Look at Value Categories" (which discusses whether an expression uses the variable itself or a temporary value), will expand on all this in detail; for now, assembling this minimal working set is enough—the main topic right after leans on it.

> This delightful property frees us from the old function-parameter headache: you can now manipulate the variable itself as if nothing were special, rather than operating on it through a pointer to it. This is something C can hardly match.

## `const` and References

We just made the acquaintance of references in the previous section; now let's team them up with `const`. Pairing `const` with references is much simpler than with pointers, because references themselves are not allowed to rebind—from the moment it is born, a reference is welded to some variable. So there is only one case for combining `const` with a reference:

```cpp
int x = 42;
const int& ref = x;
```

`ref` is an alias for `x`, but you cannot modify `x`'s value through `ref`. Similar to `const int*`, this only says "I won't modify through `ref`"—you are perfectly free to modify `x` itself directly.

This kind of "reference to a constant" has one hugely important use in real-world development—function parameters. Imagine you have a function that needs to take a `std::string` parameter:

```cpp
void print(std::string s)
{
    std::cout << s << std::endl;
}
```

Every call to `print("hello")` triggers a copy of the string. If the string is long, or the function is called frequently, that copying overhead becomes impossible to ignore. Switching to a `const` reference solves it:

> We have not yet covered the move mechanism in modern C++. In the C++98 era, we almost never wrote pass-by-value parameters; it was not until C++11, when `std::move` and rvalues arrived, that we finally had better semantics for this.

```cpp
void print(const std::string& s)
{
    std::cout << s << std::endl;
}
```

`const std::string& s` means: take a reference (no copy), but promise not to modify it. This avoids the copying overhead while assuring the caller of safety. The `const T&` parameter pattern appears at an extremely high frequency in C++; later chapters will run into it again and again, so for now just carry the impression with you.

## `constexpr` — Letting the Compiler Do the Math for You

So far, the `const` we have been talking about only means "this value will not change during execution." But some constants have values that are already settled at the compile stage—`5 * 5` is definitely `25`, so there is no need to wait for the program to run to compute it. C++11 introduced `constexpr` to tell the compiler explicitly: "this is a value you can work out at compile time." If you are familiar with assembly, the meaning becomes plain—the compiler works the result out into a constant baked right into the instruction (assembly calls it an immediate), with nothing left to compute at runtime.

```cpp
constexpr int kSquare = 5 * 5;           // 编译期就算好了，值为 25
constexpr int kBufferSize = 1024 * 64;   // 同样在编译期计算
```

Now, what happens if we swap `constexpr` for an ordinary `const`? Written out, it looks almost identical:

```cpp
const int kSquare = 5 * 5;           // 只承诺"不许改"，没承诺"必须提前算好"
const int kBufferSize = 1024 * 64;   // 提前算还是运行时算，标准没规定，看编译器的心情
```

"Eh? Charliechen114514, let me ask you: isn't `const` also unmodifiable? Why does C++ come up with something so redundant?"

It is not redundant—the difference lies in **whether the initializing value can be settled at compile time**. Let's use array sizes as the touchstone: C++ requires an array's size to be a compile-time constant (arrays whose length varies with input are C's variable-length arrays; in C++ they only get through on compiler extensions):

```cpp
int x = 10;                // 普通 int，x 不是常量表达式
const int cx = x;          // 合法，但 cx 跟着当不了编译期常量
int data[cx];              // g++ 默认放行，开 -pedantic 才提醒一句；严格按标准来的编译器直接拒绝
```

Swap in `constexpr` and the ambiguity disappears. `constexpr int k = x;` fails to compile outright; the compiler reports that "the value of x cannot be used in a constant expression," forcing you to declare your position on the spot—this value must be settled at compile time. Conversely, as long as the initializer really is a constant expression, `const int n = 10;` works just as well as an array size (I tested it myself; even `-pedantic` stays silent).

So the division of labor between the two is clear. `constexpr` writes "compile time" into the semantics, and the compiler must comply. `const` only guarantees the value stays unchanged during execution; whether it gets computed ahead of time or at runtime is each compiler's own choice. There is also a one-way relationship hiding here: `constexpr` implies `const`, so `constexpr int kVal = 42;` pulls double duty—both a compile-time constant and unmodifiable. The reverse does not hold; the `cx` above is just an ordinary `const`.

Where `constexpr` gets more powerful is that it can also be applied to functions. Let's give it a try: a `constexpr` function means that if the arguments passed in are all values determinable at compile time, then the function's return value can also be computed at compile time:

```cpp
constexpr int square(int x)
{
    return x * x;
}

constexpr int kResult = square(5);  // 编译期就算好了，kResult = 25。不信的话，让 AI 教你怎么用 objdump 或者 dumpbin 看汇编，这里不教了
```

Values computed at compile time come with another big benefit: they can be used in the places that must be written as constant expressions. For instance, if you want to declare an array, the array's size must be a compile-time constant:

```cpp
constexpr int kArraySize = square(3);  // 9
int data[kArraySize];                   // 合法，因为 kArraySize 是编译期常量
```

Even here, swapping `constexpr` for an ordinary `const` still passes—`square(3)` is a constant expression, so the `const` variable it initializes keeps full compile-time-constant status; that's exactly the rule described a moment ago. But that status rides on one premise: the initializer must always be a constant expression. The day your hand slips and the initializer becomes a quantity whose value only exists at runtime, the `const` version gets quietly waved through by g++'s variable-length-array extension, and the point of failure moves to runtime. On a compiler that follows the standard strictly, it simply fails to compile. `constexpr` has none of this dangling uncertainty: at the language level it enforces "the initializer must be a constant expression" all the way, so the ambiguity never exists in the first place.

Here we have only exchanged a first glance with `constexpr`. It is one of the most important features of modern C++. By C++14 it allowed more complex logic inside such functions, and C++17 relaxed the restrictions further. C++20 went on to introduce `consteval` (must execute at compile time) and `constinit`. We will use them over and over in embedded C++—**they help us cut down both runtime cost and binary-size overhead.**

## Putting It All Together — const_demo.cpp

Book knowledge only goes so far. Let's now string together every `const` usage discussed above into one complete example program. The logic will not be anything complex, but it covers each `const` combination and verifies the compiler's behavior.

```cpp
// const_demo.cpp —— 演示 const 变量、指针、引用和 constexpr 的各种用法

#include <iostream>

/// @brief constexpr 函数：计算平方
/// @param x 被平方的值
/// @return x 的平方
constexpr int square(int x)
{
    return x * x;
}

int main()
{
    // --- const 变量 ---
    const int kMaxSize = 100;
    // kMaxSize = 200;  // 取消注释会编译错误
    std::cout << "kMaxSize = " << kMaxSize << std::endl;

    // --- constexpr ---
    constexpr int kArraySize = square(5);  // 编译期计算，结果为 25
    std::cout << "kArraySize = " << kArraySize << std::endl;

    // --- 指向常量的指针 ---
    int a = 10;
    int b = 20;
    const int* p_to_const = &a;
    // *p_to_const = 100;  // 取消注释会编译错误
    p_to_const = &b;       // 没问题，指针可以改指向
    std::cout << "*p_to_const = " << *p_to_const << std::endl;

    // --- 常量指针 ---
    int* const const_p = &a;
    *const_p = 100;        // 没问题，可以改数据
    // const_p = &b;       // 取消注释会编译错误
    std::cout << "*const_p = " << *const_p << std::endl;

    // --- 两个都 const ---
    const int* const double_const = &a;
    // *double_const = 1;  // 编译错误
    // double_const = &b;  // 编译错误
    std::cout << "*double_const = " << *double_const << std::endl;

    // --- 引用 ---
    int x = 42;
    int& ref2 = x;         // ref2 是 x 的别名（起名叫 ref2，是把 ref 这个名字留给下面的 const 引用）
    ref2 = 100;            // 通过别名改，等价于 x = 100
    std::cout << "x = " << x << std::endl;      // 输出 100

    // --- const 引用 ---
    const int& ref = x;
    // ref = 200;          // 编译错误！通过 const 引用改不动
    x = 200;               // 直接改 x 是可以的
    std::cout << "ref = " << ref << std::endl;  // 输出 200

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o const_demo const_demo.cpp
./const_demo
```

Expected output:

```text
kMaxSize = 100
kArraySize = 25
*p_to_const = 20
*const_p = 100
*double_const = 100
x = 100
ref = 200
```

You can uncomment those "compile error" lines one at a time and see what error messages the compiler produces. Getting a hands-on feel for how the compiler intercepts these operations leaves a far deeper impression than reading text alone.

## Run Online

Run const_demo.cpp online and observe the actual output of the various `const` usages:

<OnlineCompilerDemo
  title="A First Look at const: Variables, Pointers, References, and constexpr"
  source-path="code/examples/vol1/04_const_demo.cpp"
  description="Run online and observe the actual behavior of const pointers, const references, and constexpr."
  allow-run
/>

## Try It Yourself

That's the theory covered—now it is your turn to get hands-on. The three exercises below help you gauge your understanding of `const`; I suggest writing each one out in full, compiling, and running it.

### Exercise 1: Declare const Pointers and Predict the Behavior

Write out the following declarations, then for each pointer attempt (1) modifying the data the pointer points to and (2) modifying what the pointer itself points to. Before compiling, first predict which operations the compiler will reject, then verify your predictions.

- `const int* p1`
- `int* const p2`
- `const int* const p3`

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>
int main()
{
    int a1 = 0;
    int a2 = 0;
    int a3 = 0;

    const int* p1 = &a1;
    // *p1 = 5;   // 编译错误！不能通过 const int* 修改数据
    p1 = &a2;     // 没问题，指针本身可以指向别的地方
    std::cout << "*p1 = " << *p1 << std::endl;

    int* const p2 = &a2;
    *p2 = 5;      // 没问题，int* const 指向的数据可以修改
    // p2 = &a3;  // 编译错误！int* const 指针本身不能改变指向
    std::cout << "*p2 = " << *p2 << std::endl;

    p1 = &a3;     // 没问题，const int* 指针本身可以指向别的地方

    const int* const p3 = &a3;
    // *p3 = 5;   // 编译错误！const int* const 既不能修改数据，也不能改变指向
    // p3 = &a1;  // 编译错误！const int* const 指针本身不能改变指向
    std::cout << "*p3 = " << *p3 << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
*p1 = 0
*p2 = 5
*p3 = 0
```

:::

### Exercise 2: Convert #define into constexpr

Below is some C-style code using `#define`. Replace all the macro constants with `constexpr` variables, and write a `constexpr` function `circle_area(double radius)` that computes the area of a circle.

```cpp
#define PI 3.14159265
#define MAX_RADIUS 100.0
#define MIN_RADIUS 0.1
```

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>

constexpr double PI = 3.14159265;
constexpr double MAX_RADIUS = 100.0;
constexpr double MIN_RADIUS = 0.1;

constexpr double clamp_radius(double radius)
{
    return radius < MIN_RADIUS
        ? MIN_RADIUS
        : (radius > MAX_RADIUS ? MAX_RADIUS : radius);
}

constexpr double circle_area(double radius)
{
    const double r = clamp_radius(radius);
    return PI * r * r;
}

int main()
{
    double r = 0;
    std::cout << "请你输入所求圆的半径 : ";
    std::cin >> r;
    std::cout << "半径为" << r << "的面积是: " << circle_area(r) << std::endl;
    return 0;
}
```

The exercise only hands you three macros and does not prescribe how `MAX_RADIUS` / `MIN_RADIUS` should be used; converted verbatim to `constexpr`, they would sit idle. So here a fellow `constexpr` function `clamp_radius` is added, clamping the input radius back into the `[0.1, 100]` range so that both constants genuinely take part in the computation—a `constexpr` function may also call another `constexpr` function, and with a constant-expression initialization such as `constexpr double area = circle_area(2.0);`, the entire call chain gets computed at compile time.

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请你输入所求圆的半径 : 2
半径为2的面积是: 12.5664
```

:::

### Exercise 3: Write a Function That Takes const Reference Parameters

Write a function `print_sum` that takes two `const int&` parameters and prints their sum. Then call it inside `main`. Think it over: for a small type like `int`, is there a performance difference between using `const int&` versus plain `int` as the parameter? What kind of arguments is `const T&` best suited for?

::: details Reference answer

**main.cpp**

```cpp
#include <iostream>

void print_sum(const int& a, const int& b)
{
    std::cout << a << " + " << b << " 的值是: " << a + b << std::endl;
}

int main()
{
    int a = 0;
    int b = 0;
    std::cout << "请输入a的值是 :";
    std::cin >> a;
    std::cout << "请输入b的值是 :";
    std::cin >> b;
    print_sum(a, b);
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
请输入a的值是 :1
请输入b的值是 :3
1 + 3 的值是: 4
```

For a small type like `int`, pass-by-value is usually the more appropriate choice: copying a machine-word-sized value costs very little, and the compiler can often pass it directly in a register; using `const int&` is not necessarily faster, and any real difference should be settled by measurement. `const T&` is better suited to larger, read-only objects that do not need to be copied—for example `std::string` or containers; small scalar types can generally just be passed by value.

:::
