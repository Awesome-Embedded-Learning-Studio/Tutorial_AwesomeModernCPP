---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Consolidates the reference rules used across the previous three chapters, compares references with pointers item by item to derive a selection criterion, and adds chained calls that return references plus the boundary conditions under which a const reference extends a temporary's lifetime.
difficulty: beginner
order: 3
platform: host
prerequisites:
- Pointer Arithmetic and Arrays
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: References
translation:
  source: documents/vol1-fundamentals/ch04/03-references.md
  source_hash: d0a29a3bed69e2dec9388efca6987ead652180a5e9fd9622a5e447489f6adec7
  translated_at: '2026-09-25T10:38:02+00:00'
  engine: anthropic
  token_count: 7500
---
# References: Give a Variable an Alias, Suffer Less Pointer Pain

With the two pointer chapters behind us, we've gotten hands-on with dereferencing, address-of, and pointer arithmetic. But before we turn to references, one thing is worth calling out: you've been using them all along. In Chapter 1, covering value categories, we leaned on reference binding rules to decide what can be accepted and what can't. In Chapter 2's range-for, what `auto&` hands you is a reference to the original element. In Chapter 3's parameter passing, `swap` and `const std::string&` were the stars of every scene. References have been on stage the whole time—they just never went face to face with pointers.

That's this article's job: gather the rules scattered across those three chapters, then put references and pointers side by side, item by item. By the end you'll hold a clear selection criterion for when to use a reference and when nothing but a pointer will do.

## Pulling It Together: Three Rules of References

A reference is an **alias** for an already-existing variable. After `int& ref = value;`, `ref` and `value` name the same object—anything you do to `ref` happens to `value`. Under the hood a reference usually goes through a pointer, but at the language level all of the pointer's dangerous operations are taken away, leaving a clean "another name". You saw the prototype of this account back in the Chapter 1 article on value categories; here it takes its final form.

Three rules—we've touched on each of them in earlier chapters, and now we state them together. A reference **must be initialized at declaration**: `int& ref;` doesn't compile, because there is no "leave it empty now, bind it later" option; that's the first contrast with pointers and their `nullptr`. Once bound, a reference **can never retarget**—C++ simply has no syntax for "rebinding a reference". And one more: strictly speaking, **null references do not exist**; the language requires a reference to be bound to a valid object. These three rules are the source of both the reference's safety and the limits of its capabilities, and we'll lean on them again and again when we compare with pointers.

The "can never retarget" point is a particularly easy trap, so let's pull it out and look at it on its own:

```cpp
int value = 42;
int& ref = value;

int other = 200;
ref = other;  // This is NOT "making ref point to other"!
```

What `ref = other;` actually does is assign `other`'s value, 200, to the object `ref` refers to—namely `value`. After it runs, `value` holds 200, `ref` is still a reference to `value`, and `other` is out of the picture. A reference's binding is a one-time deal: every later assignment to `ref` merely modifies the value of the referred-to object. If you need "re-pointing" semantics, the tool for the job is a pointer.

## References vs. Pointers: Which to Choose

Since both achieve "indirect access to an object", let's lay the differences out item by item.

A reference must bind to an object at declaration, so a reference is "valid" from the moment it's born (assuming you haven't pulled off the advanced bug of creating a dangling one); a pointer can start life as `nullptr` and figure things out later—flexible, at the price of weighing whether it's null before every single use. Can the target change after binding? A reference stays bound for life; a pointer can point elsewhere at any time, so walking memory in an "iterator-style" sweep, or expressing "there is no object right now", is pointer-only territory. The syntactic burden differs too: a reference is used like an ordinary variable—just write the name—while a pointer needs `*ptr` or `ptr->member`, visibly more clutter. Add the no-null-references rule on top, and "a pointer can be `nullptr`" turns out to be both its flexibility and the source of a great many bugs.

There's one practical restriction you might overlook: references can't be stored in containers. Something like `std::vector<int&>` won't compile—standard containers require their elements to be objects, and a reference is merely an alias, not an object. To gather "a bundle of re-pointable handles" in one place and manage them together, you still want pointers (or `std::reference_wrapper`, which we'll meet later).

The differences show up most clearly in function calls. Take the old task of "swapping two variables" and write it once with pointers, once with references. C-style code can only pass pointers:

```cpp
// C-style: the pointer version
void swap_by_pointer(int* a, int* b)
{
    int temp = *a;
    *a = *b;
    *b = temp;
}

int x = 10, y = 20;
swap_by_pointer(&x, &y);  // The caller must take addresses
```

Rewritten with references, the world goes quiet:

```cpp
// C++-style: the reference version
void swap_by_reference(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

int x = 10, y = 20;
swap_by_reference(x, y);  // The caller passes variables directly; no & needed
```

No `*` dereferencing inside the function body, no `&` address-taking at the call site. The standard library's `std::swap` is implemented with references too, on exactly the same principle as `swap_by_reference`. Back in Chapter 3 when we wrote it, references were still a "memorize-how-to-use-it tool"; now we can supply the full explanation: `a` and `b` are simply aliases for the caller's two variables.

> PS: Reference or pointer—when all is said and done, it's your call. My own habit: if what I'm expressing is "I'm using this object", I use a reference; if what I'm expressing leans more toward "here is where the object is stored" (go look there!), I use a pointer. It's a semantic split. And don't let anyone tell you references can't dangle—that's flat-out wrong.

## Returning References: Chained Calls and a Precise Dangling Rule

You've written references as function parameters until they're second nature; on the return-value side there are two more patterns worth a look. The first is returning a reference to a class member, letting outside code read and write internal data directly:

```cpp
class Sensor {
    float temperature_;
    float humidity_;

public:
    Sensor(float t, float h) : temperature_(t), humidity_(h) {}

    // Returns a reference to the member, allowing external code
    // to read and modify it directly
    float& temperature() { return temperature_; }

    // const version: read-only access
    const float& temperature() const { return temperature_; }
};

Sensor s(25.0f, 60.0f);
s.temperature() = 26.5f;  // Modify the internal member directly through the reference
```

The other is **chained calls**: have the member function return a reference to `*this`, and the caller can string several operations into a single line. `std::cout << a << b << c;` prints in sequence precisely because every `<<` returns a reference to `std::cout`—a mechanism we use every day.

As for returning references to local variables, Chapter 1 and Chapter 3 each warned about it once, and we fixed such a case with our own hands, so we won't rehash it here—just one precise decision rule:

::: warning The safety rule for returning references
The referred-to object's lifetime must outlast the function call itself. Member variables, global variables, static variables, and objects passed in through parameters are all safe; local variables defined inside the function body are absolutely not. Compilers usually warn on the simple shape of "directly returning a local variable", but they can't cover every path—this rule has to live in our own heads.
:::

## const References and Temporaries: Extension Only Honors Direct Binding

A const reference can bind to a temporary object and **extend its lifetime**—a rule we already used in Chapter 1 when discussing value categories: `const int& ref = 42;` is legal, and `ref` stays valid for the whole scope. A function returning by value, with the result caught outside directly by a const reference, likewise counts as direct binding:

```cpp
std::string get_name();

const std::string& name = get_name();
// The by-value temporary string is caught directly by name;
// its lifetime extends to the end of name's scope—safe
```

But "directly" is the load-bearing word: once the reference changes hands, nobody's minding the store. Here's an example that compiles, runs, and is already dangling:

```cpp
const std::string& pick(const std::string& a, const std::string& b)
{
    return a.size() > b.size() ? a : b;  // Returns one of the reference parameters as-is
}

const std::string& best = pick("hello", "hi");  // Dangling!
```

When `"hello"` and `"hi"` are passed as arguments to `pick`'s reference parameters, those two temporaries live only until the end of the full expression; `pick` returns a reference to one of them, and by the time `best` catches it they are already destroyed. We actually ran this code: GCC 16.2 with `-Wall` emits a `-Wdangling-reference` warning, while Clang 22.1 with `-Wall -Wextra` says nothing at all; and even carrying that warning, the program still compiles and still prints `hello`. A coincidentally correct result doesn't make the behavior defined—that's exactly what makes dangling references scary: they don't crash for your viewing pleasure.

A non-const reference cannot bind to a temporary (`int& ref = 42;` doesn't compile), for the reason we gave in Chapter 3: if it were allowed, what you'd modify through the reference is an object about to vanish, and the modification would mean nothing. Checks like `-Wdangling-reference` are heuristic—change the shape slightly and they can miss it—so the rule itself is what must be memorized; the return-value optimization and move semantics behind it are reserved for later chapters.

## Hands-On Practice — references.cpp

Let's fold what this article has collected into one complete program, focusing on how references and pointers differ in their shape of use:

```cpp
// references.cpp
// Platform: host
// Standard: C++17

#include <iostream>
#include <string>

struct SensorData {
    float temperature;
    float humidity;
    float pressure;
};

/// @brief Swap the values of two variables via references
void swap_by_ref(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

/// @brief Print a SensorData via const reference (no copy, no modification)
void print_sensor(const SensorData& data)
{
    std::cout << "温度: " << data.temperature << "°C, "
              << "湿度: " << data.humidity << "%, "
              << "气压: " << data.pressure << " hPa"
              << std::endl;
}

/// @brief Return a member reference, allowing external modification
class Sensor {
    SensorData data_;

public:
    Sensor(float t, float h, float p)
        : data_{t, h, p}
    {
    }

    float& temperature() { return data_.temperature; }
    const SensorData& reading() const { return data_; }
};

int main()
{
    // --- Swapping variables ---
    int x = 10, y = 20;
    std::cout << "交换前: x=" << x << ", y=" << y << std::endl;
    swap_by_ref(x, y);
    std::cout << "交换后: x=" << x << ", y=" << y << std::endl;

    // --- Passing a big object by const reference ---
    SensorData reading{25.5f, 60.0f, 1013.25f};
    std::cout << "\n传感器读数: ";
    print_sensor(reading);

    // --- Returning a member reference ---
    Sensor s(22.0f, 55.0f, 1000.0f);
    std::cout << "\n修改前: ";
    print_sensor(s.reading());

    s.temperature() = 30.0f;
    std::cout << "修改后: ";
    print_sensor(s.reading());

    // --- A const reference binding a temporary ---
    const std::string& label = std::string("温度传感器 #1");
    std::cout << "\n标签: " << label << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o references references.cpp
./references
```

Output:

```text
交换前: x=10, y=20
交换后: x=20, y=10

传感器读数: 温度: 25.5°C, 湿度: 60%, 气压: 1013.25 hPa

修改前: 温度: 22°C, 湿度: 55%, 气压: 1000 hPa
修改后: 温度: 30°C, 湿度: 55%, 气压: 1000 hPa

标签: 温度传感器 #1
```

Let's walk through what this program does, section by section. `swap_by_ref` swaps variables through reference parameters; at the call site you pass variable names directly, with no address-of operator. `print_sensor` takes its argument as `const SensorData&`, which both avoids the cost of copying the struct and guarantees at the type-system level that the function won't modify the incoming data—callers can relax the moment they see the signature. `Sensor::temperature()` returns a reference to a member variable, so external code holding that reference can assign straight to it: controlled access to internal data. Finally, `const std::string& label` demonstrates lifetime extension of a temporary under direct binding: `std::string("温度传感器 #1")` was a temporary about to evaporate, but caught by the const reference, it stayed alive until the end of `main`.

## Try It Yourself

### Exercise 1: Rework the Pointer Function

The function below uses a pointer to implement a simple "double every element of the array" job. Please convert it to the reference version:

```cpp
void double_values(int* arr, int n)
{
    for (int i = 0; i < n; ++i) {
        arr[i] *= 2;
    }
}
```

Hint: passing a C-style array by reference has to be written as a "reference to an array" like `int (&arr)[5]`—the length is part of the type, so the function can't be generic over arbitrary lengths. The easier alternative for us is `std::array<int, N>`.

::: details Reference Solution

```cpp
#include <iostream>
#include <array>

void double_values(std::array<int, 5>& arr)
{
    for (auto& value : arr) {
        value *= 2;
    }
}

int main()
{
    std::array<int, 5> values{1, 2, 3, 4, 5};

    std::cout << "修改前: ";
    for (const auto& value : values) {
        std::cout << value << " ";
    }
    std::cout << std::endl;
    double_values(values);
    std::cout << "修改后: ";
    for (const auto& value : values) {
        std::cout << value << " ";
    }
    std::cout << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
修改前: 1 2 3 4 5
修改后: 2 4 6 8 10
```

:::

### Exercise 2: Spot the Bugs

The code below has several reference-related problems. Please find them all:

```cpp
int& get_value()
{
    int x = 42;
    return x;
}

void process(int& ref) { ref += 10; }

int main()
{
    int& r = get_value(); // Line A
    int& uninit;          // Line B
    int a = 10;
    int& ref = a;
    int b = 20;
    ref = &b;             // Line C
    process(5);           // Line D
}
```

Please analyze it line by line: which lines are compile errors? Which lines are runtime undefined behavior?

::: details Reference Solution

Our verdict up front: **Lines B, C, and D are compile errors; under the problem's binary split, the runtime hazard is recorded as Line A.** More precisely, `get_value()`'s return statement and Line A compile, but they leave behind a dangling reference; reading or writing through `r` is what triggers runtime undefined behavior.

| Location | Outcome | Reason |
| --- | --- | --- |
| `return x;` | Compiles; compilers usually warn | `x` is a local variable of automatic storage duration whose lifetime ends when the function returns; the returned `int&` does not extend `x`'s lifetime. |
| Line A: `int &r = get_value();` | Compiles, but `r` is a dangling reference | `r` binds to `x`, whose lifetime has already ended. This step by itself merely creates the dangling reference; subsequent reads or writes through `r` (for example `std::cout << r`) are the undefined behavior. |
| Line B: `int &uninit;` | **Compile error** | A reference declaration must be initialized at declaration; you can't declare it first and bind later the way you can with a pointer. |
| `int a = 10;`, `int &ref = a;`, `int b = 20;` | Correct | `ref` binds, at its declaration, to `a`, which is still alive. |
| Line C: `ref = &b;` | **Compile error** | The `ref` expression has type `int`, while `&b` has type `int*`; and assignment never rebinds a reference to a different object. To give `a` the value of `b`, write `ref = b`; to change the target you must declare a new reference or switch to a pointer. |
| Line D: `process(5);` | **Compile error** | `process` requires a modifiable `int&`, but the literal `5` is an rvalue and cannot bind to a non-`const` lvalue reference. Pass a named `int` lvalue instead. |

Line A is the one we most easily get confused about: strictly speaking, **a dangling reference is an erroneous state, and accessing the dangling reference is what constitutes runtime undefined behavior**. For example, with B, C, and D temporarily commented out, the following read triggers UB:

```cpp
int &r = get_value();
std::cout << r;  // Undefined behavior: the x that r points to has already ended its lifetime
```

Here is one safe corrected version: have `get_value` return by value, and give each reference an initial object at declaration; if you genuinely must return a reference, it may only be to an object whose lifetime is long enough (for example a static object, or one handed in by the caller):

```cpp
int get_value()
{
    return 42;
}

void process(int& ref) { ref += 10; }

int main()
{
    int r = get_value();

    int data = 0;
    int& uninit = data;

    int a = 10;
    int& ref = a;
    int b = 20;
    ref = b;

    int value = 5;
    process(value);

    (void)r;
    (void)uninit;
}
```

:::

### Exercise 3: Build a Simple Chained Configurator

Please design a class `Config` with two `int` members, `width_` and `height_`, providing two methods, `set_width(int)` and `set_height(int)`, and have them return `Config&` to support chained calls:

```cpp
Config c;
c.set_width(800).set_height(600);
```

::: details Reference Solution

```cpp
#include <iostream>

class Config {
    int width_{};
    int height_{};

public:
    Config& set_width(int width)
    {
        width_ = width;
        return *this;
    }
    Config& set_height(int height)
    {
        height_ = height;
        return *this;
    }
};
int main()
{
    Config c;
    c.set_width(800).set_height(600);
    return 0;
}
```

:::
