---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Gathers the reference rules already used across Chapters 1-3, compares references
  with pointers point by point to give a clear selection rule, and adds returning references
  for chained calls plus the exact boundary of const-reference lifetime extension.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 指针运算与数组
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
  source_hash: 3f31cc1c1040c63ef2403f9cd649634a723e3757ada2231867ab92e0a219d933
  translated_at: '2026-09-17T00:00:00+00:00'
  engine: manual
---
# References: An Alias That Spares You the Pointer Pain

The two pointer articles are behind us, and by now we can handle dereferencing, address-of, and pointer arithmetic. Before moving on, though, one thing deserves to be said out loud: you have been using references all along. In Chapter 1, the binding rules of references were how we told acceptable initializers from rejected ones. In Chapter 2, `auto&` in a range-for handed us a reference to the original element. In Chapter 3, `swap` and `const std::string&` were on stage the whole time. References have been in the room from the start; they just never sat down face to face with pointers.

That is this article's job: gather the rules scattered across three chapters, then put references and pointers side by side. By the end you will hold one clear selection rule: which situations call for a reference, and which ones leave no alternative to a pointer.

## Gathering the Rules

A reference is an **alias** for an existing variable. After `int& ref = value;`, the names `ref` and `value` denote the same object, and every operation on `ref` acts on `value`. Under the hood a reference is usually implemented through a pointer, but the language takes the dangerous pointer operations away and leaves a clean "other name". You saw a prototype of this in the value-categories article of Chapter 1; here it becomes official.

Three rules, each already stepped on in earlier chapters, stated together. A reference **must be initialized at declaration**—`int& ref;` does not compile; there is no "leave it empty, bind later" option, which is the first contrast with a pointer's `nullptr`. A reference **cannot be rebound once bound**—C++ has no syntax for "re-pointing" a reference at all. And strictly speaking, **a null reference does not exist**: the language requires a reference to bind to a valid object. These three are at once the source of reference safety and the source of its limits, and the comparison with pointers below keeps returning to them.

The "cannot rebind" rule is the one that trips people most often, so it gets its own look:

```cpp
int value = 42;
int& ref = value;

int other = 200;
ref = other;  // this is NOT "making ref point to other"!
```

What `ref = other;` actually does is assign the value 200 of `other` to the object referenced by `ref`, which is `value`. After it runs, `value` holds 200, `ref` is still a reference to `value`, and `other` is out of the picture. The binding is one-shot; every later assignment through `ref` only modifies the referenced object's value. When you need "re-pointable" semantics, the tool for the job is a pointer.

## References vs. Pointers: Which One?

Since both provide indirect access to objects, let's lay the differences out item by item.

A reference must bind to an object at declaration, so from birth it is "valid" (assuming you haven't pulled off the advanced bug of creating a dangling one). A pointer can sit at `nullptr` first and be assigned later—flexible, at the cost of weighing "could it be null?" before every use. Can the target change after binding? A reference is bound for life; a pointer can be re-pointed at any time, and iterating memory "iterator-style" or expressing "no object right now" can only be done with pointers. Syntax burden differs too: a reference is used like a plain variable, just write the name; pointers need `*ptr` or `ptr->member`, visibly more wordy. Add the no-null-reference rule, and a pointer's ability to be `nullptr` is at once its flexibility and the origin of a large class of bugs.

One practical limit is easy to miss: references cannot be stored in containers. `std::vector<int&>` does not compile—standard containers require elements to be objects, and a reference is an alias, not an object. To manage "a batch of re-pointable handles", pointers are still the tool (or `std::reference_wrapper`, which we will meet later).

The difference shows up most clearly at function calls. Take the old task "swap two variables" and write it both ways. C style has to pass pointers:

```cpp
// C style: pointer version
void swap_by_pointer(int* a, int* b)
{
    int temp = *a;
    *a = *b;
    *b = temp;
}

int x = 10, y = 20;
swap_by_pointer(&x, &y);  // caller must take addresses
```

Rewritten with references, the world quiets down:

```cpp
// C++ style: reference version
void swap_by_reference(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

int x = 10, y = 20;
swap_by_reference(x, y);  // pass the variables directly, no &
```

No `*` dereferencing inside the function, no `&` address-taking at the call site. The standard library's `std::swap` is implemented with references too, on exactly the same principle. In Chapter 3, when we wrote it, references were still "a tool whose usage you memorize first"; now the full explanation fits: `a` and `b` are aliases of the caller's two variables.

One more example, modifying a member:

```cpp
struct SensorData {
    float temperature;
    float humidity;
    float pressure;
};

// Pointer version: null check required, -> for member access
void fix_temperature(SensorData* data)
{
    if (data != nullptr) {      // check every single time
        data->temperature += 0.5f;
    }
}

// Reference version: clean, no extra check
void fix_temperature(SensorData& data)
{
    data.temperature += 0.5f;   // plain . access
}
```

So when should you use a pointer? My advice: **default to references, unless you need something references cannot do**. Expressing "possibly no object" (that's `std::optional`, coming later), re-pointing at runtime, doing pointer arithmetic over memory, storing handles in a container—those want pointers; everywhere else, a reference is the safer choice.

Strictly speaking, through some "unconventional means" we can create a reference bound to a null address, for example `int& ref = *static_cast<int*>(nullptr);`. It compiles, but using `ref` is undefined behavior. Never write this—if someone claims "references can be null too", they are exploiting a loophole in the language rules, and such code has no place in real engineering.

## Returning References: Chaining, and One Old Warning Made Precise

You have written reference parameters plenty by now; the return-value side has two patterns worth seeing. The first is returning a reference to a class member, letting outside code read and write internal data directly:

```cpp
class Sensor {
    float temperature_;
    float humidity_;

public:
    Sensor(float t, float h) : temperature_(t), humidity_(h) {}

    // returns a member reference: external read and write access
    float& temperature() { return temperature_; }

    // const version: read-only access
    const float& temperature() const { return temperature_; }
};

Sensor s(25.0f, 60.0f);
s.temperature() = 26.5f;  // modify the internal member through the reference
```

The second is **chained calls**: a member function returns a reference to `*this`, so a caller can string several operations into one line. `std::cout << a << b << c;` outputs continuously precisely because every `<<` returns a reference to `std::cout`—we use this mechanism daily.

As for returning a reference to a local variable, Chapter 1 and Chapter 3 each warned once and we fixed a case with our own hands, so the example will not be repeated here—just one precise rule to keep:

::: warning Judging whether returning a reference is safe
The referenced object must outlive the function call itself. Member variables, global variables, static variables, and objects passed in through parameters are all safe; local variables defined in the function body are never safe. Compilers usually warn about the simple "return a local directly" shape, but they cannot cover every path—this rule has to live in our own heads.
:::

## const References and Temporaries: Extension Only on Direct Binding

A const reference can bind to a temporary object and **extend its lifetime**—we used this rule back in Chapter 1's value categories: `const int& ref = 42;` is legal, and `ref` stays valid for its whole scope. A function returning by value, caught directly by a const reference outside, also counts as direct binding:

```cpp
std::string get_name();

const std::string& name = get_name();
// the temporary string returned by value is caught directly by name
// its lifetime extends to the end of name's scope: safe
```

But "direct" is the operative word; once the binding changes hands, nobody manages the temporary. Here is an example that compiles, runs, and is already dangling:

```cpp
const std::string& pick(const std::string& a, const std::string& b)
{
    return a.size() > b.size() ? a : b;  // hands a reference parameter straight back
}

const std::string& best = pick("hello", "hi");  // dangling!
```

When `"hello"` and `"hi"` are passed as arguments to `pick`'s reference parameters, those two temporaries live only until the end of the full expression; `pick` returns a reference to one of them, and by the time `best` catches it they are already destroyed. We actually ran this code: GCC 16.2 with `-Wall` emits a `-Wdangling-reference` warning, Clang 22.1 with `-Wall -Wextra` says nothing at all; and even carrying the warning, the program compiles and prints `hello` all the same. A correct-looking result does not make the behavior defined—this is what makes dangling references nasty: they don't crash for you on cue.

Non-const references cannot bind to temporaries (`int& ref = 42;` does not compile), for the reason Chapter 3 gave: if it were allowed, you could modify through the reference an object that is about to vanish, which is pointless. Checks like `-Wdangling-reference` are heuristic and can miss the moment the shape changes slightly, so the rule itself has to be memorized; the return-value optimization and move semantics behind it wait for later chapters.

## Hands-On: references.cpp

Let's pack what this article gathered into one complete program, watching the difference in shape between references and pointers:

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

/// @brief Swap two variables through references
void swap_by_ref(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

/// @brief Print SensorData via const reference (no copy, no modification)
void print_sensor(const SensorData& data)
{
    std::cout << "temperature: " << data.temperature << "C, "
              << "humidity: " << data.humidity << "%, "
              << "pressure: " << data.pressure << " hPa"
              << std::endl;
}

/// @brief Returns member references, allowing external modification
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
    // --- swapping variables ---
    int x = 10, y = 20;
    std::cout << "before: x=" << x << ", y=" << y << std::endl;
    swap_by_ref(x, y);
    std::cout << "after: x=" << x << ", y=" << y << std::endl;

    // --- passing a large object by const reference ---
    SensorData reading{25.5f, 60.0f, 1013.25f};
    std::cout << "\nsensor reading: ";
    print_sensor(reading);

    // --- returning a member reference ---
    Sensor s(22.0f, 55.0f, 1000.0f);
    std::cout << "\nbefore: ";
    print_sensor(s.reading());

    s.temperature() = 30.0f;
    std::cout << "after: ";
    print_sensor(s.reading());

    // --- const reference bound to a temporary ---
    const std::string& label = std::string("temperature sensor #1");
    std::cout << "\nlabel: " << label << std::endl;

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
before: x=10, y=20
after: x=20, y=10

sensor reading: temperature: 25.5C, humidity: 60%, pressure: 1013.25 hPa

before: temperature: 22C, humidity: 55%, pressure: 1000 hPa
after: temperature: 30C, humidity: 55%, pressure: 1000 hPa

label: temperature sensor #1
```

Let's walk through what this program does. `swap_by_ref` swaps variables through reference parameters, passing variable names at the call site with no address-of operator. `print_sensor` takes `const SensorData&`: no struct copy, plus a type-level guarantee that the function won't modify the input—a caller can relax just by reading the signature. `Sensor::temperature()` returns a reference to a member, so outside code can assign through it directly: controlled access to internal data. The final `const std::string& label` shows lifetime extension under direct binding: `std::string("temperature sensor #1")` was a temporary about to vanish, but caught by a const reference, it lives until the end of `main`.

## Try It Yourself

### Exercise 1: Convert a pointer function

The function below uses a pointer to "double every element of an array". Convert it to a reference version:

```cpp
void double_values(int* arr, int n)
{
    for (int i = 0; i < n; ++i) {
        arr[i] *= 2;
    }
}
```

Hint: passing a C-style array by reference requires the "reference to array" syntax `int (&arr)[5]`—the length becomes part of the type, so the function no longer works for arbitrary lengths; the easier replacement is `std::array<int, N>`.

::: details Reference answer

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

    std::cout << "before: ";
    for (const auto& value : values) {
        std::cout << value << " ";
    }
    std::cout << std::endl;
    double_values(values);
    std::cout << "after: ";
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
before: 1 2 3 4 5
after: 2 4 6 8 10
```

:::

### Exercise 2: Find the bugs

The code below has several reference-related problems. Find them all:

```cpp
int& get_value()
{
    int x = 42;
    return x;
}

void process(int& ref) { ref += 10; }

int main()
{
    int& r = get_value(); // line A
    int& uninit;          // line B
    int a = 10;
    int& ref = a;
    int b = 20;
    ref = &b;             // line C
    process(5);           // line D
}
```

Analyze it line by line: which lines are compile errors? Which are undefined behavior at runtime?

::: details Reference answer

The verdict first: **lines B, C, and D are compile errors; by the exercise's binary sorting, the runtime hazard is line A.** More precisely, the `return` statement inside `get_value()` and line A compile, but they leave behind a dangling reference; it is reading or writing through `r` afterwards that triggers undefined behavior.

| Location | Result | Reason |
| --- | --- | --- |
| `return x;` | Compiles; compilers usually warn | `x` is a local of automatic storage duration; its lifetime ends when the function returns, and returning `int&` does not extend it. |
| Line A: `int &r = get_value();` | Compiles, but `r` is a dangling reference | `r` binds to `x`, whose lifetime has ended. This step merely creates the dangling reference; a later read or write through `r` (e.g. `std::cout << r`) is the undefined behavior. |
| Line B: `int &uninit;` | **Compile error** | A reference must be initialized at declaration; you cannot declare it first and bind later like a pointer. |
| `int a = 10;`, `int &ref = a;`, `int b = 20;` | Correct | `ref` binds at declaration to `a`, which is alive. |
| Line C: `ref = &b;` | **Compile error** | The expression `ref` has type `int` while `&b` has type `int*`; and assignment never rebinds a reference. To assign `b`'s value into `a`, write `ref = b`; to change the target you must declare a new reference or use a pointer. |
| Line D: `process(5);` | **Compile error** | `process` demands a modifiable `int&`, and the literal `5` is an rvalue, which cannot bind to a non-const lvalue reference. Pass a named `int` lvalue instead. |

The easiest to confuse is line A: strictly speaking, **a dangling reference is the broken state; accessing it is what constitutes undefined behavior**. For example, with B, C, D commented out, this read triggers UB:

```cpp
int &r = get_value();
std::cout << r;  // undefined behavior: the x that r refers to has ended its lifetime
```

Here is one safe fix: have `get_value` return by value, and initialize every reference at declaration; if a reference must be returned, return only an object that lives long enough (a static object, or one handed in by the caller):

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

### Exercise 3: A tiny chained configurator

Design a class `Config` with two `int` members, `width_` and `height_`, providing `set_width(int)` and `set_height(int)` methods that return `Config&` to support chaining:

```cpp
Config c;
c.set_width(800).set_height(600);
```

::: details Reference answer

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
