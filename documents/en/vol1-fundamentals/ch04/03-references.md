---
title: References
description: 'Deep dive into C++ references: reference syntax, differences between
  references and pointers, and the crucial role of `const` references in function
  parameters.'
chapter: 4
order: 3
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- 指针运算与数组
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch04/03-references.md
  source_hash: 51d34634bf4c2b24846169050a696593069d236041da8b2b0ac5ec7413681b01
  translated_at: '2026-05-26T10:48:52.993332+00:00'
  engine: anthropic
  token_count: 2315
---
# References

Pointers are powerful, but honestly, they are also quite easy to get into trouble with. In the previous chapter, we spent a lot of time dealing with pointers—dereferencing, taking addresses, null pointer checks, the ``->`` operator... As you write more code, you will find that in many scenarios, we do not need the full capabilities of pointers. We simply want to "pass a large object to a function without copying it," or "let a function modify the caller's variable." Pointers can certainly achieve this, but the syntax always feels clunky. C++ gives us a safer, more concise alternative: **references**. In this chapter, we will thoroughly understand references from start to finish.

## Step One — What Exactly Is a Reference

The essence of a reference is an **alias**—another name for an already existing variable. Just like a colleague named "Zhang San" who everyone calls "Lao Zhang," no matter which name you call out, you are referring to the same person. At the underlying implementation level, references are usually implemented via pointers, but the language level completely shields us from those dangerous pointer operations, leaving us with just a clean "another name."

Let's look at the most basic usage:

```cpp
int value = 42;
int& ref = value;  // ref 是 value 的别名

ref = 100;         // 通过别名修改原变量
// 现在 value 也是 100
```

`int& ref = value;` does two things: it declares ``ref`` as a reference bound to ``int``, and immediately binds it to ``value``. From this line of code onward, ``ref`` and ``value`` are the exact same thing—any operation on ``ref`` is equivalent to an operation on ``value``. No extra memory overhead, no syntactic burden of indirection, it is just that simple.

However, references have two very strict constraints, and understanding them is a prerequisite for using references safely. First, **a reference must be initialized when declared**. You cannot write ``int& ref;`` and then later make it point to some variable—this code simply will not compile. Unlike pointers, which can be set to ``nullptr`` and dealt with later, a reference must be bound to a real, tangible object from the moment it is born. Second, **once a reference is bound, it cannot be re-bound to a different target**. This point is particularly easy to trip over, so let's look at it separately:

```cpp
int value = 42;
int& ref = value;

int other = 200;
ref = other;  // 这不是"让 ref 指向 other"！
```

The effect of ``ref = other;`` is to assign the value of ``other`` (200) to the object referenced by ``ref`` (which is ``value``). After execution, ``value`` becomes 200, ``ref`` remains a reference to ``value``, and it has nothing to do with ``other``. The binding of a reference is **one-time** and **irrevocable**; all subsequent assignment operations merely modify the value of the referenced object.

> ⚠️ **Pitfall Warning**
> Many beginners see ``ref = other;`` and mistakenly think this is "re-binding." In reality, C++ has no syntax for "re-binding a reference" at all—all assignments to a reference are assignments to the referenced object. If you need "re-pointable" semantics, what you need is not a reference, but a pointer.

## Step Two — References vs. Pointers, Which One to Choose

Since both references and pointers can achieve "indirect object manipulation," what exactly is the difference between them? Let's compare them point by point:

**Must be initialized vs. can be dangling**. A reference must be bound to an object when declared, so a reference is always "valid" (assuming you haven't created advanced bugs like dangling references). A pointer, on the other hand, can be declared as ``nullptr`` first and assigned later; this flexibility also means you have to consider "could it be null?" every time you use it.

**Cannot be re-bound vs. can be re-pointed**. Once a reference is bound, it never changes; a pointer can point to a different object at any time. If you need to traverse memory in an "iterator-like" fashion, or if you need to express the semantics of "possibly no object," pointers are the only choice.

**No dereferencing syntax vs. needs ``*`` and ``->``**. Using a reference is just like using a normal variable; you simply write its name. Pointers require ``*ptr`` or ``ptr->member`` to access the target, making the code look noticeably more verbose.

**No null references vs. null pointers**. Strictly speaking, "null references" do not exist in C++—a reference must be bound to a valid object. But a pointer can be ``nullptr``, which is both the source of its flexibility and the source of countless bugs.

Let's use a practical example to feel the difference between the two. Suppose we have a struct that needs to be modified inside a function:

```cpp
struct SensorData {
    float temperature;
    float humidity;
    float pressure;
};

// 指针版本：需要空指针检查，用 -> 访问成员
void fix_temperature(SensorData* data)
{
    if (data != nullptr) {      // 每次都得检查
        data->temperature += 0.5f;
    }
}

// 引用版本：干净利落，不需要额外检查
void fix_temperature(SensorData& data)
{
    data.temperature += 0.5f;   // 直接用 . 访问
}
```

So when should we use pointers? My advice is—**use references by default, unless you need something references cannot do**. Specifically, use pointers when you need to express the concept of "possibly no object" (or ``std::optional``, which we will learn about later); use pointers when you need to change the target at runtime; use pointers when you need to do pointer arithmetic to traverse memory. In all other scenarios, references are the safer choice.

> ⚠️ **Pitfall Warning**
> Strictly speaking, through certain "unconventional means," you can create a reference bound to a null address, such as ``int& ref = *static_cast<int*>(nullptr);``. This line of code will compile, but using ``ref`` is undefined behavior. Never write code like this—if someone tells you "references can also be null," they are exploiting loopholes in the language rules, and such code should absolutely never appear in real-world engineering.

## Step Three — References as Function Parameters

The most common use of references is as function parameters. Let's first look at a classic example: swapping the values of two variables. In C, we can only pass pointers:

```cpp
// C 风格：指针版本
void swap_by_pointer(int* a, int* b)
{
    int temp = *a;
    *a = *b;
    *b = temp;
}

int x = 10, y = 20;
swap_by_pointer(&x, &y);  // 调用时需要取地址
```

Rewriting this with references makes the whole world much cleaner:

```cpp
// C++ 风格：引用版本
void swap_by_reference(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

int x = 10, y = 20;
swap_by_reference(x, y);  // 调用时直接传变量，不需要 &
```

Inside the function, we do not need ``*`` for dereferencing, and at the call site, we do not need ``&`` to take the address—the code readability takes a step up. The standard library's ``std::swap`` is also implemented using references, with the exact same principle.

But often, we pass parameters not to modify them, but to **avoid copy overhead**. A struct containing a large amount of data, or a long string, would need to be entirely copied if passed by value, wasting both stack space and time. This is where ``const`` references come into play:

```cpp
// 按值传递：拷贝整个 string，浪费
void print_by_value(std::string s)
{
    std::cout << s << std::endl;
}

// const 引用传递：不拷贝，不修改，完美
void print_by_ref(const std::string& s)
{
    std::cout << s << std::endl;
    // s = "hack";  // 编译错误！const 引用不允许修改
}
```

The combination of ``const std::string&`` appears extremely frequently in C++, and is basically the standard paradigm for "passing read-only large objects." ``const`` tells the compiler and the caller two things: first, this function will not modify the passed-in object; second, the compiler will intercept any attempt to modify it at compile time. When a caller sees that a parameter is ``const&``, they can confidently hand over the data without worrying about it being secretly tampered with.

Of course, there is a practical rule of thumb: for fundamental types (``int``, ``double``, pointers, etc.), just pass by value, because the copy overhead is negligible; for anything larger than a fundamental type—``std::string``, structs, containers—pass by ``const`` reference.

## Step Four — References as Return Values

Functions can also return references, which is a very practical pattern in C++. The most common use case is returning a reference to a class member, allowing external code to directly read from or write to internal data:

```cpp
class Sensor {
    float temperature_;
    float humidity_;

public:
    Sensor(float t, float h) : temperature_(t), humidity_(h) {}

    // 返回成员的引用，允许外部直接读取和修改
    float& temperature() { return temperature_; }

    // const 版本：只读访问
    const float& temperature() const { return temperature_; }
};

Sensor s(25.0f, 60.0f);
s.temperature() = 26.5f;  // 直接通过引用修改内部成员
```

Another classic application of returning references is **chained calls**—having a function return a reference to ``*this``, so that the caller can chain multiple operations in a single line of code. The standard library's ``operator<<`` works exactly like this: ``std::cout << a << b << c;`` can output continuously because each ``<<`` returns a reference to ``std::cout``.

But returning a reference has a **fatal trap**—absolutely never return a reference to a local variable. Local variables are stored on the stack, and once the function returns, the stack frame is reclaimed. At this point, the reference points to a piece of memory that has already been freed:

```cpp
// 危险！返回局部变量的引用
int& dangerous()
{
    int local = 42;
    return local;  // 函数返回后 local 已销毁
    // 引用变成了悬空引用——使用它是未定义行为
}
```

The insidious thing about this bug is that the program might occasionally run perfectly fine, and occasionally crash for no apparent reason, with the crash location and cause showing no pattern. Because when that piece of stack memory happens not to be overwritten, the reference can still read the "correct" value; once it gets overwritten by subsequent function calls, what gets read out is garbage data.

> ⚠️ **Pitfall Warning**
> The rule for determining whether returning a reference is safe is simple—**the lifetime of the referenced object must be longer than the function call itself**. Member variables, global variables, static variables, and objects passed in via parameters are all safe. Local variables inside the function body are absolutely unsafe. Compilers will usually issue a warning for this, but they cannot detect all cases—so this rule must be etched into your mind.

## Step Five — const References and Temporary Objects

C++ has a feature that seems strange at first glance: a ``const`` reference can bind to a temporary object (an rvalue), and it will **extend the lifetime of this temporary object**, making it live as long as the reference.

```cpp
const int& ref = 42;  // OK！42 本来是个临时值
// ref 在整个作用域内有效，值为 42
```

What does this line of code do? The literal ``42`` is originally an rvalue, and logically speaking, it should disappear after the expression ends. But because ``ref`` is a ``const`` reference and is directly bound to this temporary value, C++ dictates that the compiler must extend the lifetime of this temporary value to the end of ``ref``'s scope. In other words, the compiler quietly creates a temporary ``int`` behind the scenes, initializes it with 42, and then lets ``ref`` bind to this temporary ``int``.

For ``int``, this is no big deal, but for complex types, it is crucial:

```cpp
std::string get_name();

const std::string& name = get_name();
// get_name() 返回的临时 string 本来在完整表达式结束后就该销毁
// 但 const 引用绑定了它，生命周期被延长到 name 的作用域结束
// 所以 name 在整个作用域内都是安全的
```

However, there is an important condition here—**the reference must be directly bound to the temporary object** for the lifetime extension to take effect. If there are indirect steps in between, such as function returns, the rule no longer holds. This topic involves return value optimization and move semantics, which will be discussed in detail in later chapters.

You might have noticed that a non-const reference cannot bind to a temporary object: ``int& ref = 42;`` will not compile. The reason is also quite reasonable—if a non-const reference were allowed to bind to a temporary value, then modifying through the reference would modify an object that is about to disappear, making the modification meaningless. The reason ``const`` references can do this is because they promise read-only access; the compiler knows you will not modify that temporary value, so it can safely extend its lifetime for you.

## Hands-On Practice — references.cpp

Let's integrate what we learned above into a complete program, focusing on comparing the usage differences between references and pointers:

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

/// @brief 通过引用交换两个变量的值
void swap_by_ref(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

/// @brief 通过 const 引用打印 SensorData（不拷贝，不修改）
void print_sensor(const SensorData& data)
{
    std::cout << "温度: " << data.temperature << "°C, "
              << "湿度: " << data.humidity << "%, "
              << "气压: " << data.pressure << " hPa"
              << std::endl;
}

/// @brief 返回成员引用，允许外部修改
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
    // --- 交换变量 ---
    int x = 10, y = 20;
    std::cout << "交换前: x=" << x << ", y=" << y << std::endl;
    swap_by_ref(x, y);
    std::cout << "交换后: x=" << x << ", y=" << y << std::endl;

    // --- const 引用传递大对象 ---
    SensorData reading{25.5f, 60.0f, 1013.25f};
    std::cout << "\n传感器读数: ";
    print_sensor(reading);

    // --- 返回成员引用 ---
    Sensor s(22.0f, 55.0f, 1000.0f);
    std::cout << "\n修改前: ";
    print_sensor(s.reading());

    s.temperature() = 30.0f;
    std::cout << "修改后: ";
    print_sensor(s.reading());

    // --- const 引用绑定临时对象 ---
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

Let's review what this program does, section by section. ``swap_by_ref`` uses reference parameters to implement variable swapping, and at the call site, we pass the variable names directly without needing the address-of operator. ``print_sensor`` receives parameters using ``const SensorData&``, which both avoids the copy overhead of the struct and guarantees at the type system level that the function will not modify the passed-in data—callers can feel at ease just by looking at the function signature. ``Sensor::temperature()`` returns a reference to a member variable, and after external code obtains the reference, it can assign values directly, achieving controlled access to internal data. Finally, ``const std::string& label`` demonstrates the ability of a const reference to extend the lifetime of a temporary object—``std::string("温度传感器 #1")`` is originally a temporary object about to disappear, but because it is bound by a const reference, it stays alive until the ``main`` function ends.

## Try It Yourself

### Exercise 1: Refactor a Pointer Function

The following function uses a pointer to implement a simple "double the array elements" feature. Convert it to a reference version:

```cpp
void double_values(int* arr, int n)
{
    for (int i = 0; i < n; ++i) {
        arr[i] *= 2;
    }
}
```

Hint: C-style arrays cannot directly use reference passing to preserve length information; consider using ``std::array<int, N>`` as a replacement.

### Exercise 2: Find the Bugs

The following code has several issues related to references. Find all of them:

```cpp
int& get_value()
{
    int x = 42;
    return x;
}

void process(int& ref) { ref += 10; }

int main()
{
    int& r = get_value();
    int& uninit;              // 行 A
    int a = 10;
    int& ref = a;
    int b = 20;
    ref = &b;                 // 行 B
    process(5);               // 行 C
}
```

Analyze line by line: which lines have compilation errors? Which lines result in runtime undefined behavior?

### Exercise 3: Implement a Simple Chained Configurator

Design a class ``Config`` that contains two ``int`` members: ``width_`` and ``height_``. Provide two methods, ``set_width(int)`` and ``set_height(int)``, that return ``Config&`` to support chained calls:

```cpp
Config c;
c.set_width(800).set_height(600);
```

## Summary

In this chapter, starting from the "pain points of pointers," we learned about the core C++ feature of references. A reference is an alias for an existing object; it must be initialized when declared, and cannot be changed once bound. Compared to pointers, references have no null value, require no dereferencing syntax, and have immutable binding relationships—these constraints are exactly what make them the best choice when "passing an object that definitely exists."

When used as function parameters, references make code cleaner than pointer versions; when combined with the ``const`` qualifier, it becomes the standard paradigm for read-only parameter passing: "no copy, no modification." When returning a reference, we must be extra careful to ensure that the lifetime of the referenced object is longer than the function call—absolutely never return a reference to a local variable. Finally, a ``const`` reference can bind to a temporary object and extend its lifetime; this feature is very common in real-world code, but it is limited to const references only.

In the next chapter, we will touch on the basics of C++ dynamic memory management—although it is not yet time to talk about smart pointers, you can first get an impression: modern C++ thoroughly solves the question of "who is responsible for releasing memory" through RAII and smart pointers. Before that, make sure your foundation in references is solid; it will make things much easier later on.
